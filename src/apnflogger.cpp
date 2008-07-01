// -*- symbian-c++ -*-

//
// apnflogger.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Wraps the native RFileLogger API for use in Python.
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
#include <flogger.h> // flogger.lib required, Symbian 7.0-up

// --------------------------------------------------------------------
// object structure...

typedef struct
	{
	PyObject_VAR_HEAD;
	RFileLogger* iLogger;
	TBool iLogOpen;
	CTC_DEF_HANDLE(ctc);
	} apn_flogger_object;

// --------------------------------------------------------------------
// instance methods...

/** Connects to the flogger server.
	Throws an exception that fails.
	It is an error to call this method if there already
	is a connection in place.
*/
static PyObject* apn_flogger_connect(apn_flogger_object* self,
									 PyObject* /*args*/)
	{
	AssertNonNull(self);
	if (self->iLogger)
		{
		AoSocketPanic(EPanicSessionAlreadyExists);
		return NULL;
		}

	// note that the constructor must get called;
	// with "automatics" in a struct, that does not appear to happen;
	// no such problems when we create the object explicitly
	self->iLogger = new RFileLogger;
	if (!self->iLogger)
		{
		return SPyErr_SetFromSymbianOSErr(KErrNoMemory);
		}

	TInt error = self->iLogger->Connect();
	if (error != KErrNone)
		{
		delete self->iLogger;
		self->iLogger = NULL;
		return SPyErr_SetFromSymbianOSErr(error);
		}

	self->iLogOpen = EFalse;
	CTC_STORE_HANDLE(self->ctc);

	RETURN_NO_VALUE;
	}

static void CloseSession(apn_flogger_object* self)
	{
	AssertNonNull(self);
	if (self->iLogger)
		{
		CTC_CHECK(self->ctc);

		// Do not know if can assume Close() will also do CloseLog() for
		// any open logfile; also do not know if it's safe to
		// call CloseLog() without having called CreateLog().
		// So we have to keep track of things, and make calls explicitly.
		if (self->iLogOpen)
			{
			self->iLogger->CloseLog();
			self->iLogOpen = EFalse;
			}

		self->iLogger->Close();

		// this would probably also do a Close(), but there's
		// little documentation about flogger, and do not want
		// to take any chances
		delete self->iLogger;
		self->iLogger = NULL;
		}
	}

/** Closes the flogger server session.
	Does nothing if there is no open session.
	It is okay to call this method even if no session
	has ever been created.
*/
static PyObject* apn_flogger_close(apn_flogger_object* self,
								   PyObject* /*args*/)
	{
	CloseSession(self);
	RETURN_NO_VALUE;
	}

static PyObject* apn_flogger_createlog(apn_flogger_object* self,
									   PyObject* args)
	{
	Py_UNICODE* fb;
	int fl;
	Py_UNICODE* tb;
	int tl;
	if (!PyArg_ParseTuple(args, "u#u#", &fb, &fl, &tb, &tl))
		{
		return NULL;
		}
	// TInt sizeOfUni = sizeof(Py_UNICODE); // 2
	TPtrC dirName((TUint16*)fb, fl);
	TPtrC fileName((TUint16*)tb, tl);

	AssertNonNull(self);
	if (!self->iLogger)
		{
		AoSocketPanic(EPanicSessionDoesNotExist);
		return NULL;
		}
	CTC_CHECK(self->ctc);

	// If this function gives you a USER 7, it means that the constructor
	// of your RFileLogger has not been called. Not very informative,
	// and not sure if R-classes are even supposed to have constructors.
	// Note that this function has no return value, so presumably any
	// failures are silent. Again, the documentation does not say much.
	self->iLogger->CreateLog(dirName, fileName, EFileLoggingModeOverwrite);
	self->iLogOpen = ETrue;

	RETURN_NO_VALUE;
	}

static PyObject* apn_flogger_write(apn_flogger_object* self,
								   PyObject* args)
	{
	char* sb;
	int sl;
	if (!PyArg_ParseTuple(args, "u#", &sb, &sl))
		{
		return NULL;
		}
	TPtrC sss((TUint16*)sb, sl);

	AssertNonNull(self);
	if (!self->iLogger)
		{
		AoSocketPanic(EPanicSessionDoesNotExist);
		return NULL;
		}
	CTC_CHECK(self->ctc);
	self->iLogger->Write(sss);
	RETURN_NO_VALUE;
	}

const static PyMethodDef apn_flogger_methods[] =
	{
	{"connect", (PyCFunction)apn_flogger_connect, METH_NOARGS},
	{"close", (PyCFunction)apn_flogger_close, METH_NOARGS},
	{"create_log", (PyCFunction)apn_flogger_createlog, METH_VARARGS},
	{"write", (PyCFunction)apn_flogger_write, METH_VARARGS},
	{NULL, NULL} // sentinel
	};

/** Deallocates the native object.
	Any open flogger server session is closed before deallocation.
*/
static void apn_dealloc_flogger(apn_flogger_object *self)
	{
	CloseSession(self);
	PyObject_Del(self);
	}

static PyObject *apn_flogger_getattr(apn_flogger_object *self,
									 char *name)
	{
	AssertNonNull(self);
	return Py_FindMethod((PyMethodDef*)apn_flogger_methods,
						 (PyObject*)self, name);
	}

// --------------------------------------------------------------------
// type...

const PyTypeObject apn_flogger_typetmpl =
	{
	PyObject_HEAD_INIT(NULL)
	0,										   /*ob_size*/
	"pyaosocket.AoFlogger",			  /*tp_name*/
	sizeof(apn_flogger_object),					  /*tp_basicsize*/
	0,										   /*tp_itemsize*/
	/* methods */
	(destructor)apn_dealloc_flogger,				  /*tp_dealloc*/
	0,										   /*tp_print*/
	(getattrfunc)apn_flogger_getattr,				  /*tp_getattr*/
	0,										   /*tp_setattr*/
	0,										   /*tp_compare*/
	0,										   /*tp_repr*/
	0,										   /*tp_as_number*/
	0,										   /*tp_as_sequence*/
	0,										   /*tp_as_mapping*/
	0										  /*tp_hash*/
	};

TInt apn_flogger_ConstructType()
	{
	return ConstructType(&apn_flogger_typetmpl, "AoFlogger");
	}

// --------------------------------------------------------------------
// module methods...

#define AoFloggerType \
	((PyTypeObject*)SPyGetGlobalString("AoFlogger"))

// Returns NULL if cannot allocate.
// The reference count of any returned object will be 1.
// The created socket server object will be initialized,
// but there will be no open session.
static apn_flogger_object* NewFloggerObject()
	{
	apn_flogger_object* newFlogger =
		// sets refcount to 1 if successful,
		// so decrefing should delete
		PyObject_New(apn_flogger_object, AoFloggerType);
	if (newFlogger == NULL)
		{
		// raise an exception with the reason set by PyObject_New
		return NULL;
		}
	newFlogger->iLogger = NULL;
	return newFlogger;
	}

PyObject* apn_flogger_new(PyObject* /*self*/, PyObject* /*args*/)
	{
	return reinterpret_cast<PyObject*>(NewFloggerObject());
	}
