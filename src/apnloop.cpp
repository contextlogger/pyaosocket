// -*- symbian-c++ -*-

//
// apnloop.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Wraps the CActiveSchedulerWait API for use by Python programs.
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
// CAoLoop...

/* We would not actually require this wrapper class, but
   we have it just in case we will need to do something
   more complex later on.
*/
NONSHARABLE_CLASS(CAoLoop) : public CBase
	{
public:
	static CAoLoop* NewL();
	~CAoLoop();
	void Start();
	void AsyncStop();
private:
	CAoLoop() {} // just to declare as private
	void ConstructL();
private:
	CActiveSchedulerWait* iWait;
	CTC_DEF_HANDLE(ctc);
	};

CAoLoop* CAoLoop::NewL()
	{
	CAoLoop* object = new (ELeave) CAoLoop;
	CleanupStack::PushL(object);
	object->ConstructL();
	CleanupStack::Pop();
	return object;
	}

void CAoLoop::ConstructL()
	{
	iWait = new (ELeave) CActiveSchedulerWait;
	CTC_STORE_HANDLE(ctc);
	}

CAoLoop::~CAoLoop()
	{
	CTC_CHECK(ctc);
	// Stops any looping prior to destruction.
	delete iWait;
	}

// The loop is run within this method call, so do not hold
// your breath waiting for it to return.
void CAoLoop::Start()
	{
	CTC_CHECK(ctc);
	iWait->Start();
	}

// Call this from within a RunL() to cause a break from the loop.
// As this class is not thread safe, you must call this function
// from within a RunL() of the active scheduler that an instance
// of this class is dealing with. If you want to cause a call
// to AsyncStop() from another thread, then you must have an
// AO that can be signalled complete from another thread that
// you can use to get the job done.
void CAoLoop::AsyncStop()
	{
	CTC_CHECK(ctc);
	iWait->AsyncStop();
	}

// --------------------------------------------------------------------
// object structure...

// we store the state we require in a Python object
typedef struct
	{
	PyObject_VAR_HEAD;
	CAoLoop* iLoop;
	} apn_loop_object;

// --------------------------------------------------------------------
// instance methods...

static PyObject* apn_loop_start(apn_loop_object* self,
								PyObject* /*args*/)
	{
	AssertNonNull(self->iLoop);
	// Now that we dive into Symbian native code, we'd better
	// release the thread lock to allow any other threads to run.
	// Just have to keep in mind that all the RunL's will be
	// called without possession of the thread lock.
	Py_BEGIN_ALLOW_THREADS;
	self->iLoop->Start();
	Py_END_ALLOW_THREADS;
	RETURN_NO_VALUE;
	}

static PyObject* apn_loop_stop(apn_loop_object* self,
							   PyObject* /*args*/)
	{
	AssertNonNull(self->iLoop);
	self->iLoop->AsyncStop();
	RETURN_NO_VALUE;
	}

static PyObject* apn_loop_open(apn_loop_object* self,
							   PyObject* /*args*/)
	{
	AssertNull(self->iLoop);
	TRAPD(error, self->iLoop = CAoLoop::NewL());
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}
	RETURN_NO_VALUE;
	}

/** It is okay to call this method multiple times,
	or without having ever called ``open``.
*/
static PyObject* apn_loop_close(apn_loop_object* self,
								PyObject* /*args*/)
	{
	delete self->iLoop;
	self->iLoop = NULL;
	RETURN_NO_VALUE;
	}

const static PyMethodDef apn_loop_methods[] =
	{
	{"start", (PyCFunction)apn_loop_start, METH_NOARGS},
	{"stop", (PyCFunction)apn_loop_stop, METH_NOARGS},
	{"open", (PyCFunction)apn_loop_open, METH_NOARGS},
	{"close", (PyCFunction)apn_loop_close, METH_NOARGS},
	{NULL, NULL} // sentinel
	};

static void apn_dealloc_loop(apn_loop_object *self)
	{
	delete self->iLoop;
	self->iLoop = NULL;
	PyObject_Del(self);
	}

static PyObject *apn_loop_getattr(apn_loop_object *self,
								  char *name)
	{
	return Py_FindMethod((PyMethodDef*)apn_loop_methods,
						 (PyObject*)self, name);
	}

// --------------------------------------------------------------------
// type...

const PyTypeObject apn_loop_typetmpl =
	{
	PyObject_HEAD_INIT(NULL)
	0,										   /*ob_size*/
	"pyaosocket.AoLoop",			  /*tp_name*/
	sizeof(apn_loop_object),					  /*tp_basicsize*/
	0,										   /*tp_itemsize*/
	/* methods */
	(destructor)apn_dealloc_loop,				  /*tp_dealloc*/
	0,										   /*tp_print*/
	(getattrfunc)apn_loop_getattr,				  /*tp_getattr*/
	0,										   /*tp_setattr*/
	0,										   /*tp_compare*/
	0,										   /*tp_repr*/
	0,										   /*tp_as_number*/
	0,										   /*tp_as_sequence*/
	0,										   /*tp_as_mapping*/
	0										  /*tp_hash*/
	};

TInt apn_loop_ConstructType()
	{
	return ConstructType(&apn_loop_typetmpl, "AoLoop");
	}

// --------------------------------------------------------------------
// module methods...

#define AoLoopType \
	((PyTypeObject*)SPyGetGlobalString("AoLoop"))

// Returns NULL if cannot allocate.
// The reference count of any returned object will be 1.
// The created socket object will be initialized,
// but the socket will not be open.
static apn_loop_object* NewLoopObject()
	{
	apn_loop_object* newLoop =
		// sets refcount to 1 if successful,
		// so decrefing should delete
		PyObject_New(apn_loop_object, AoLoopType);
	if (newLoop == NULL)
		{
		// raise an exception with the reason set by PyObject_New
		return NULL;
		}

	newLoop->iLoop = NULL;

	return newLoop;
	}

// allocates a new AoLoop object, or raises and exception
PyObject* apn_loop_new(PyObject* /*self*/,
					   PyObject* /*args*/)
	{
	return reinterpret_cast<PyObject*>(NewLoopObject());
	}
