// -*- symbian-c++ -*-

//
// apnportdiscoverer.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Implements Bluetooth port discovery for the PDIS service.
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

#include "btengine.h"
#include "local_epoc_py_utils.h"
#include "panic.h"
#include "settings.h"

// --------------------------------------------------------------------
// CAoPortDiscoverer...

NONSHARABLE_CLASS(CAoPortDiscoverer) : public CBase, public MPortDiscoveryObserver
	{
public:
	static CAoPortDiscoverer* NewL();
	virtual ~CAoPortDiscoverer();
	// starts discovery
	void DiscoverL(const TBTDevAddr& aAddress,
				   TUint aServiceId,
				   PyObject* aCallback, PyObject* aParam);
	// stops any ongoing discovery
	void Cancel();
private:
	CAoPortDiscoverer();
	void ConstructL();
private: // MPortDiscoveryObserver
	virtual void PortDiscovered(TInt anError, TInt aPort);
private:
	CPortDiscoverer* iPortDiscoverer;

	void Free();
	PyObject* iCallback;
	PyObject* iParam;

	PyThreadState* iThreadState;

	CTC_DEF_HANDLE(ctc);
	};

void CAoPortDiscoverer::Free()
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

CAoPortDiscoverer* CAoPortDiscoverer::NewL()
	{
	CAoPortDiscoverer* obj = new (ELeave) CAoPortDiscoverer;
	CleanupStack::PushL(obj);
	obj->ConstructL();
	CleanupStack::Pop();
	return obj;
	}

CAoPortDiscoverer::CAoPortDiscoverer()
	{
	CTC_STORE_HANDLE(ctc);
	}

void CAoPortDiscoverer::ConstructL()
	{
	// nothing
	}

CAoPortDiscoverer::~CAoPortDiscoverer()
	{
	CTC_CHECK(ctc);
	Cancel();
	Free();
	}

void CAoPortDiscoverer::DiscoverL(const TBTDevAddr& aAddress,
								  TUint aServiceId,
								  PyObject* aCallback, PyObject* aParam)
	{
	Cancel();
	iPortDiscoverer = CPortDiscoverer::NewL(*this);

	Free();
	AssertNonNull(aCallback);
	AssertNonNull(aParam);
	iCallback = aCallback;
	Py_INCREF(aCallback);
	iParam = aParam;
	Py_INCREF(aParam);

	iThreadState = PyThreadState_Get();

	iPortDiscoverer->DiscoverL(aAddress, aServiceId);
	}

void CAoPortDiscoverer::PortDiscovered(TInt anError, TInt aPort)
	{
	AssertNonNull(iCallback);
	AssertNonNull(iParam);

	PyEval_RestoreThread(iThreadState);

	PyObject* arg;
	arg = Py_BuildValue("(iiO)", anError, aPort, iParam);

	if (arg)
		{
		PyObject* result = PyObject_CallObject(iCallback, arg);
		Py_DECREF(arg);
		Py_XDECREF(result);
		if (!result)
			{
			// Callbacks are not supposed to throw exceptions.
			// Make sure that the error gets noticed.
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

void CAoPortDiscoverer::Cancel()
	{
	// there is no other way to cancel
	delete iPortDiscoverer;
	iPortDiscoverer = NULL;
	}

// --------------------------------------------------------------------
// object structure...

// we store the state we require in a Python object
typedef struct
	{
	PyObject_VAR_HEAD;
	CAoPortDiscoverer* iDiscoverer;
	} apn_portdisc_object;

// --------------------------------------------------------------------
// instance methods...

static PyObject* apn_portdisc_discover(apn_portdisc_object* self,
									   PyObject* args)
	{
	int l;
	char* b;
	unsigned int sid;
	PyObject* cb;
	PyObject* param;
	// would like to use "I" instead of "i" here, but only
	// supported from Python version 2.3
	if (!PyArg_ParseTuple(args, "u#iOO", &b, &l, &sid, &cb, &param))
		{
		return NULL;
		}
	if (!PyCallable_Check(cb))
		{
		PyErr_SetString(PyExc_TypeError, "parameter must be callable");
		return NULL;
		}
	TPtrC address((TUint16*)b, l);

	if (!self->iDiscoverer)
		{
		AoSocketPanic(EPanicUseBeforeInit);
		}

	TBTDevAddr btDevAddr;
	BtEngine::StringToDevAddr(btDevAddr, address);

	TRAPD(error, self->iDiscoverer->DiscoverL(btDevAddr, sid, cb, param));
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}

	RETURN_NO_VALUE;
	}

static PyObject* apn_portdisc_cancel(apn_portdisc_object* self,
									 PyObject* /*args*/)
	{
	if (self->iDiscoverer)
		{
		self->iDiscoverer->Cancel();
		}
	RETURN_NO_VALUE;
	}

static PyObject* apn_portdisc_open(apn_portdisc_object* self,
								   PyObject* /*args*/)
	{
	AssertNull(self->iDiscoverer);
	TRAPD(error, self->iDiscoverer = CAoPortDiscoverer::NewL());
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
static PyObject* apn_portdisc_close(apn_portdisc_object* self,
									PyObject* /*args*/)
	{
	delete self->iDiscoverer;
	self->iDiscoverer = NULL;
	RETURN_NO_VALUE;
	}

const static PyMethodDef apn_portdisc_methods[] =
	{
	{"open", (PyCFunction)apn_portdisc_open, METH_NOARGS},
	{"discover", (PyCFunction)apn_portdisc_discover, METH_VARARGS},
	{"cancel", (PyCFunction)apn_portdisc_cancel, METH_NOARGS},
	{"close", (PyCFunction)apn_portdisc_close, METH_NOARGS},
	{NULL, NULL} // sentinel
	};

static void apn_dealloc_portdisc(apn_portdisc_object *self)
	{
	delete self->iDiscoverer;
	self->iDiscoverer = NULL;
	PyObject_Del(self);
	}

static PyObject *apn_portdisc_getattr(apn_portdisc_object *self,
									  char *name)
	{
	return Py_FindMethod((PyMethodDef*)apn_portdisc_methods,
						 (PyObject*)self, name);
	}

// --------------------------------------------------------------------
// type...

const PyTypeObject apn_portdisc_typetmpl =
	{
	PyObject_HEAD_INIT(NULL)
	0,										   /*ob_size*/
	"pyaosocket.AoPortDiscoverer",			  /*tp_name*/
	sizeof(apn_portdisc_object),					  /*tp_basicsize*/
	0,										   /*tp_itemsize*/
	/* methods */
	(destructor)apn_dealloc_portdisc,				  /*tp_dealloc*/
	0,										   /*tp_print*/
	(getattrfunc)apn_portdisc_getattr,				  /*tp_getattr*/
	0,										   /*tp_setattr*/
	0,										   /*tp_compare*/
	0,										   /*tp_repr*/
	0,										   /*tp_as_number*/
	0,										   /*tp_as_sequence*/
	0,										   /*tp_as_mapping*/
	0										  /*tp_hash*/
	};

TInt apn_portdisc_ConstructType()
	{
	return ConstructType(&apn_portdisc_typetmpl, "AoPortDiscoverer");
	}

// --------------------------------------------------------------------
// module methods...

#define AoDiscovererType \
((PyTypeObject*)SPyGetGlobalString("AoPortDiscoverer"))

// Returns NULL if cannot allocate.
// The reference count of any returned object will be 1.
// The created socket object will be initialized,
// but the socket will not be open.
static apn_portdisc_object* NewDiscovererObject()
	{
	apn_portdisc_object* newDiscoverer =
		// sets refcount to 1 if successful,
		// so decrefing should delete
		PyObject_New(apn_portdisc_object, AoDiscovererType);
	if (newDiscoverer == NULL)
		{
		// raise an exception with the reason set by PyObject_New
		return NULL;
		}

	newDiscoverer->iDiscoverer = NULL;

	return newDiscoverer;
	}

// allocates a new object, or raises and exception
PyObject* apn_portdisc_new(PyObject* /*self*/, PyObject* /*args*/)
	{
	return reinterpret_cast<PyObject*>(NewDiscovererObject());
	}
