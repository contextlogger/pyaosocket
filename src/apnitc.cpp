// -*- symbian-c++ -*-

//
// apnitc.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Implements a facility that allows other one thread to complete
// an AO request of another.
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
// CAoItc...

NONSHARABLE_CLASS(CAoItc) : public CActive
	{
public:
	static CAoItc* NewL();
	~CAoItc();
	void Request(PyObject* aCallback, PyObject* aParam);
	void Complete();
private:
	CAoItc();
	void RunL();
	void DoCancel();
private:
	void Free();
	PyObject* iCallback;
	PyObject* iParam;
	PyThreadState* iThreadState;
	TThreadId iThreadId;
	CTC_DEF_HANDLE(ctc);
	};

void CAoItc::Free()
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

CAoItc* CAoItc::NewL()
	{
	return new (ELeave) CAoItc;
	}

CAoItc::CAoItc() :
	CActive(EPriorityStandard)
	{
	CActiveScheduler::Add(this);
	CTC_STORE_HANDLE(ctc);
	}

CAoItc::~CAoItc()
	{
	CTC_CHECK(ctc);
	Cancel();
	Free();
	}

/** Note that when the active scheduler calls this function,
	we never have the interpreter lock, and must acquire
	it before accessing Python.
*/
void CAoItc::RunL()
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

void CAoItc::Complete()
	{
	// Given that this method is called from a thread
	// that does not usually operate on this object,
	// we do not want to read or modify any property
	// of this object that might be changed by another
	// thread.
	RThread thread;
	TInt error = thread.Open(iThreadId, EOwnerThread);
	// If there was an error, perhaps the thread that we
	// were supposed to signal has died, in which case
	// I suppose we should just not try to signal it.
	if (!error)
		{
		TRequestStatus* status = &iStatus;
		thread.RequestComplete(status, KErrNone);
		thread.Close();
		}
	}

void CAoItc::Request(PyObject* aCallback, PyObject* aParam)
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

	// Note that we cannot just copy RThread(), as although
	// the handle should be valid for the whole process,
	// the default constructor initializes it in such
	// a manner that it will always point to the current
	// thread -- I think, maybe. Anyway, the thread ID here
	// identifies the thread that is current now, which is
	// what we want.
	iThreadId = RThread().Id();

	iStatus = KRequestPending;
	SetActive();

	// We have the interpreter lock here, so it is okay to do this.
	iThreadState = PyThreadState_Get();
	}

void CAoItc::DoCancel()
	{
	// This method will not get called unless iActive is ETrue,
	// but the value of iActive will not be reset until the active
	// scheduler gets to run. So it is possible that we have
	// already completed the request, and that iActive is still
	// ETrue. We can use iStatus to determine if we have completed
	// the request or not.
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
	CAoItc* iItc;
	} apn_itc_object;

// --------------------------------------------------------------------
// instance methods...

// This method must be called by the thread whose active
// scheduler is managing this active object.
static PyObject* apn_itc_request(apn_itc_object* self,
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

	AssertNonNull(self->iItc);
	self->iItc->Request(cb, param);

	RETURN_NO_VALUE;
	}

// This method signals completion of this active object.
// The active object must indeed be active when this method
// is called. This method may only be called by a thread
// other than the one that made the request.
static PyObject* apn_itc_complete(apn_itc_object* self,
								  PyObject* /*args*/)
	{
	AssertNonNull(self->iItc);
	self->iItc->Complete();
	RETURN_NO_VALUE;
	}

// We are providing a cancel method in case the thread that
// is supposed to signal completion cannot be started or
// something. This may only be called by the thread that
// issued the request. Note that this object is not thread
// safe, and thus calling ``cancel`` when there is
// another thread calling ``complete`` is _bad_.
static PyObject* apn_itc_cancel(apn_itc_object* self,
								PyObject* /*args*/)
	{
	AssertNonNull(self->iItc);
	self->iItc->Cancel();
	RETURN_NO_VALUE;
	}

/** It is not necessary to call this method if you
	created the object in a thread that is going
	to do the garbage collection. Otherwise you'd
	better.
*/
static PyObject* apn_itc_close(apn_itc_object* self,
							   PyObject* /*args*/)
	{
	delete self->iItc;
	self->iItc = NULL;
	RETURN_NO_VALUE;
	}

static PyObject* apn_itc_open(apn_itc_object* self,
							  PyObject* /*args*/)
	{
	AssertNull(self->iItc);
	TRAPD(error, self->iItc = CAoItc::NewL());
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}
	RETURN_NO_VALUE;
	}

const static PyMethodDef apn_itc_methods[] =
	{
	{"request", (PyCFunction)apn_itc_request, METH_VARARGS},
	{"complete", (PyCFunction)apn_itc_complete, METH_NOARGS},
	{"cancel", (PyCFunction)apn_itc_cancel, METH_NOARGS},
	{"close", (PyCFunction)apn_itc_close, METH_NOARGS},
	{"open", (PyCFunction)apn_itc_open, METH_NOARGS},
	{NULL, NULL} // sentinel
	};

static void apn_dealloc_itc(apn_itc_object *self)
	{
	delete self->iItc;
	self->iItc = NULL;
	PyObject_Del(self);
	}

static PyObject *apn_itc_getattr(apn_itc_object *self,
								 char *name)
	{
	return Py_FindMethod((PyMethodDef*)apn_itc_methods,
						 (PyObject*)self, name);
	}

// --------------------------------------------------------------------
// type...

const PyTypeObject apn_itc_typetmpl =
	{
	PyObject_HEAD_INIT(NULL)
	0,										   /*ob_size*/
	"pyaosocket.AoItc",			  /*tp_name*/
	sizeof(apn_itc_object),					  /*tp_basicsize*/
	0,										   /*tp_itemsize*/
	/* methods */
	(destructor)apn_dealloc_itc,				  /*tp_dealloc*/
	0,										   /*tp_print*/
	(getattrfunc)apn_itc_getattr,				  /*tp_getattr*/
	0,										   /*tp_setattr*/
	0,										   /*tp_compare*/
	0,										   /*tp_repr*/
	0,										   /*tp_as_number*/
	0,										   /*tp_as_sequence*/
	0,										   /*tp_as_mapping*/
	0										  /*tp_hash*/
	};

TInt apn_itc_ConstructType()
	{
	return ConstructType(&apn_itc_typetmpl, "AoItc");
	}

// --------------------------------------------------------------------
// module methods...

#define AoItcType \
	((PyTypeObject*)SPyGetGlobalString("AoItc"))

// Returns NULL if cannot allocate.
// The reference count of any returned object will be 1.
// The created socket object will be initialized,
// but the socket will not be open.
static apn_itc_object* NewItcObject()
	{
	apn_itc_object* newItc =
		// sets refcount to 1 if successful,
		// so decrefing should delete
		PyObject_New(apn_itc_object,
					 AoItcType);
	if (newItc == NULL)
		{
		// raise an exception with the reason set by PyObject_New
		return NULL;
		}

	newItc->iItc = NULL;

	return newItc;
	}

// allocates a new AoItc object, or raises and exception
PyObject* apn_itc_new(PyObject* /*self*/,
					  PyObject* /*args*/)
	{
	return reinterpret_cast<PyObject*>(NewItcObject());
	}
