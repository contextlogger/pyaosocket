// -*- symbian-c++ -*-

//
// apnimmediate.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Implements an active object whose requests complete as
// soon as the active scheduler lets the task run.
//

// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation files
// (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "local_epoc_py_utils.h"
#include "settings.h"
#include "panic.h"
#include <e32base.h>

// --------------------------------------------------------------------
// CAoImmediate...

NONSHARABLE_CLASS(CAoImmediate) : public CActive
	{
public:
	static CAoImmediate* NewL();
	~CAoImmediate();
	void Complete(PyObject* aCallback, PyObject* aParam);
private:
	CAoImmediate();
	void RunL();
	void DoCancel();
private:
	void Free();
	PyObject* iCallback;
	PyObject* iParam;

	/* Apparently, when there is only a single thread in the runtime,
	   the thread state management functions do nothing really.
	   But when more than one thread is in use, we must have
	   the interpreter lock before we may access the runtime.
	   We _never_ have it in a RunL(), as it gets called
	   from "outside" the runtime.

	   We use this thread state handle to acquire the interpreter
	   lock as required.
	*/
	PyThreadState* iThreadState;

	CTC_DEF_HANDLE(ctc);
	};

void CAoImmediate::Free()
	{
	if (iCallback)
		{
		Py_DECREF(iCallback);
		iCallback = NULL;
		}
	if (iParam)
		{
		Py_DECREF(iParam);
		iParam = NULL;
		}
	}

CAoImmediate* CAoImmediate::NewL()
	{
	return new (ELeave) CAoImmediate;
	}

CAoImmediate::CAoImmediate() :
	CActive(EPriorityStandard)
	{
	CActiveScheduler::Add(this);
	CTC_STORE_HANDLE(ctc);
	}

CAoImmediate::~CAoImmediate()
	{
	CTC_CHECK(ctc);
	Cancel();
	Free();

	/* The CActive destructor will remove this object from
	   the active scheduler's list of active object, which
	   is appropriate to do here since we registered in
	   the constructor. The destructor will get called once
	   the garbage collector kicks in. We must merely make
	   sure there are no cyclic references keeping it from
	   doing so. If that happens, there will be a "memory
	   leak" anyway, not only because a pointer to this
	   object won't get removed from the active scheduler,
	   but also because this object won't get freed. */
	}

/** Note that when the active scheduler calls this function,
	we never have the interpreter lock, and must acquire
	it before accessing Python.
*/
void CAoImmediate::RunL()
	{
	TInt error = iStatus.Int();

	AssertNonNull(iCallback);
	AssertNonNull(iParam);

	PyEval_RestoreThread(iThreadState);

	PyObject* arg = Py_BuildValue("(iO)", error, iParam);
	if (arg)
		{
		PyObject* result = PyObject_CallObject(iCallback, arg);
		Py_DECREF(arg);
		Py_XDECREF(result);
		if (!result)
			{
			// Callbacks are not supposed to throw exceptions.
			// Make sure that the error get noticed.
			PyErr_Clear();
			AoSocketPanic(EPanicExceptionInCallback);
			}
		}
	else
		{
		// It is misleading for an exception stack trace
		// to pop up later in some other context.
		// Perhaps we shall simply accept that an out
		// of memory condition will cause all sorts of
		// weird problems that we cannot properly act on.
		// We will just put a stop to things right here.
		PyErr_Clear();
		AoSocketPanic(EPanicOutOfMemory);
		}

	PyEval_SaveThread();

	// the callback may have done anything, including
	// deleting the object whose method we are in,
	// so do not attempt to access any property anymore
	}

void CAoImmediate::Complete(PyObject* aCallback, PyObject* aParam)
	{
	if (IsActive())
		{
		AoSocketPanic(EPanicRequestAlreadyPending);
		}

	Free();
	AssertNonNull(aCallback);
	AssertNonNull(aParam);
	iCallback = aCallback;
	Py_INCREF(aCallback);
	iParam = aParam;
	Py_INCREF(aParam);

	iStatus = KRequestPending;
	SetActive();

	// We have the interpreter lock here, so it is okay to do this.
	iThreadState = PyThreadState_Get();

	TRequestStatus* status = &iStatus;
	User::RequestComplete(status, KErrNone);
	// RunL should run fairly soon
	}

void CAoImmediate::DoCancel()
	{
	// note that it is important that we do not signal
	// the same request twice; in our case, actually,
	// the status flag has always already been signaled when
	// control reaches this spot, so we would actually
	// not need to do anything here; this code is just
	// here for clarity
	if (iStatus == KRequestPending)
		{
		TRequestStatus* status = &iStatus;
		User::RequestComplete(status, KErrCancel);
		}
	}

// --------------------------------------------------------------------
// object structure...

// we store the state we require in a Python object
typedef struct
	{
	PyObject_VAR_HEAD;
	CAoImmediate* iImmediate;
	} apn_immediate_object;

// --------------------------------------------------------------------
// instance methods...

static PyObject* apn_immediate_complete(apn_immediate_object* self,
										PyObject* args)
	{
	PyObject* cb;
	PyObject* param;
	if (!PyArg_ParseTuple(args, "OO", &cb, &param))
		{
		return NULL;
		}
	if (!PyCallable_Check(cb))
		{
		PyErr_SetString(PyExc_TypeError, "parameter must be callable");
		return NULL;
		}

	if (!self->iImmediate)
		{
		AoSocketPanic(EPanicUseBeforeInit);
		}
	self->iImmediate->Complete(cb, param);

	RETURN_NO_VALUE;
	}

static PyObject* apn_immediate_cancel(apn_immediate_object* self,
									  PyObject* /*args*/)
	{
	AssertNonNull(self->iImmediate);
	self->iImmediate->Cancel();
	RETURN_NO_VALUE;
	}

/** Creates the Symbian object (the Python object has already
	been created). This must be done in the thread that will
	be using the object, as we want to register with the active
	scheduler of that thread.
*/
static PyObject* apn_immediate_open(apn_immediate_object* self,
									PyObject* /*args*/)
	{
	AssertNull(self->iImmediate);
	TRAPD(error, self->iImmediate = CAoImmediate::NewL());
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}
	RETURN_NO_VALUE;
	}

/** Destroys the Symbian object, but not the Python object.
	This must be done in the thread that used the object,
	as we must deregister with the correct active scheduler.
*/
static PyObject* apn_immediate_close(apn_immediate_object* self,
									 PyObject* /*args*/)
	{
	delete self->iImmediate;
	self->iImmediate = NULL;
	RETURN_NO_VALUE;
	}

const static PyMethodDef apn_immediate_methods[] =
	{
	{"open", (PyCFunction)apn_immediate_open, METH_NOARGS},
	{"complete", (PyCFunction)apn_immediate_complete, METH_VARARGS},
	{"cancel", (PyCFunction)apn_immediate_cancel, METH_NOARGS},
	{"close", (PyCFunction)apn_immediate_close, METH_NOARGS},
	{NULL, NULL} // sentinel
	};

static void apn_dealloc_immediate(apn_immediate_object *self)
	{
	delete self->iImmediate;
	self->iImmediate = NULL;
	PyObject_Del(self);
	}

static PyObject *apn_immediate_getattr(apn_immediate_object *self,
									   char *name)
	{
	return Py_FindMethod((PyMethodDef*)apn_immediate_methods,
						 (PyObject*)self, name);
	}

// --------------------------------------------------------------------
// type...

const PyTypeObject apn_immediate_typetmpl =
	{
	PyObject_HEAD_INIT(NULL)
	0,										   /*ob_size*/
	"pyaosocket.AoImmediate",			  /*tp_name*/
	sizeof(apn_immediate_object),					  /*tp_basicsize*/
	0,										   /*tp_itemsize*/
	/* methods */
	(destructor)apn_dealloc_immediate,				  /*tp_dealloc*/
	0,										   /*tp_print*/
	(getattrfunc)apn_immediate_getattr,				  /*tp_getattr*/
	0,										   /*tp_setattr*/
	0,										   /*tp_compare*/
	0,										   /*tp_repr*/
	0,										   /*tp_as_number*/
	0,										   /*tp_as_sequence*/
	0,										   /*tp_as_mapping*/
	0										  /*tp_hash*/
	};

TInt apn_immediate_ConstructType()
	{
	return ConstructType(&apn_immediate_typetmpl, "AoImmediate");
	}

// --------------------------------------------------------------------
// module methods...

#define AoImmediateType \
	((PyTypeObject*)SPyGetGlobalString("AoImmediate"))

// Returns NULL if cannot allocate.
// The reference count of any returned object will be 1.
// The created socket object will be initialized,
// but the socket will not be open.
static apn_immediate_object* NewImmediateObject()
	{
	apn_immediate_object* newImmediate =
		// sets refcount to 1 if successful,
		// so decrefing should delete
		PyObject_New(apn_immediate_object,
					 AoImmediateType);
	if (newImmediate == NULL)
		{
		// raise an exception with the reason set by PyObject_New
		return NULL;
		}

	newImmediate->iImmediate = NULL;

	return newImmediate;
	}

// allocates a new AoImmediate object, or raises and exception
PyObject* apn_immediate_new(PyObject* /*self*/,
							PyObject* /*args*/)
	{
	return reinterpret_cast<PyObject*>(NewImmediateObject());
	}
