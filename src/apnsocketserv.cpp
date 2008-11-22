// -*- symbian-c++ -*-

//
// apnsocketserv.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// A wrapper for the native RSocketServ class.
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
#include "logging.h"
#include "settings.h"
#include "panic.h"
#include "apnsocketserv.h"

// --------------------------------------------------------------------
// object structure...

typedef struct
	{
	PyObject_VAR_HEAD;
	RSocketServ iSocketServ;
	DEF_SESSION_OPEN(iSocketServ);
	CTC_DEF_HANDLE(ctc);
	} apn_socketserv_object;

RSocketServ& ToSocketServ(PyObject* aObject)
	{
	AssertNonNull(aObject);
	return (reinterpret_cast<apn_socketserv_object*>(aObject))->iSocketServ;
	}

// --------------------------------------------------------------------
// instance methods...

/** Connects to the socket server, unless there already is an open
	session, in which case does nothing. Throws an exception if fails.
*/
static PyObject* apn_socketserv_connect(apn_socketserv_object* self,
										PyObject* /*args*/)
	{
	AssertNonNull(self);
	TInt err = self->iSocketServ.Connect();
	if (err != KErrNone && err != KErrAlreadyExists)
		{
		return SPyErr_SetFromSymbianOSErr(err);
		}
	SET_SESSION_OPEN(self->iSocketServ);
	CTC_STORE_HANDLE(self->ctc);
	RETURN_NO_VALUE;
	}

/** Closes the socket server session.
	Does nothing if there is no open session.
*/
static PyObject* apn_socketserv_close(apn_socketserv_object* self,
									  PyObject* /*args*/)
	{
	AssertNonNull(self);
	if (IS_SESSION_OPEN(self->iSocketServ))
		{
		CTC_CHECK(self->ctc);
		self->iSocketServ.Close();
		SET_SESSION_CLOSED(self->iSocketServ);
		}
	RETURN_NO_VALUE;
	}

const static PyMethodDef apn_socketserv_methods[] =
	{
	{"connect", (PyCFunction)apn_socketserv_connect, METH_NOARGS},
	{"close", (PyCFunction)apn_socketserv_close, METH_NOARGS},
	{NULL, NULL} // sentinel
	};

/** Deallocates the native object.
	Any open socket server session is closed before deallocation.
*/
static void apn_dealloc_socketserv(apn_socketserv_object *self)
	{
	AssertNonNull(self);
	if (IS_SESSION_OPEN(self->iSocketServ))
		{
		CTC_CHECK(self->ctc);
		self->iSocketServ.Close();
		SET_SESSION_CLOSED(self->iSocketServ);
		}
	PyObject_Del(self);
	}

static PyObject *apn_socketserv_getattr(apn_socketserv_object *self,
										char *name)
	{
	AssertNonNull(self);
	return Py_FindMethod((PyMethodDef*)apn_socketserv_methods,
						 (PyObject*)self, name);
	}

// --------------------------------------------------------------------
// type...

const PyTypeObject apn_socketserv_typetmpl =
	{
	PyObject_HEAD_INIT(NULL)
	0,										   /*ob_size*/
	"pyaosocket.AoSocketServ",			  /*tp_name*/
	sizeof(apn_socketserv_object),					  /*tp_basicsize*/
	0,										   /*tp_itemsize*/
	/* methods */
	(destructor)apn_dealloc_socketserv,				  /*tp_dealloc*/
	0,										   /*tp_print*/
	(getattrfunc)apn_socketserv_getattr,				  /*tp_getattr*/
	0,										   /*tp_setattr*/
	0,										   /*tp_compare*/
	0,										   /*tp_repr*/
	0,										   /*tp_as_number*/
	0,										   /*tp_as_sequence*/
	0,										   /*tp_as_mapping*/
	0										  /*tp_hash*/
	};

TInt apn_socketserv_ConstructType()
	{
	return ConstructType(&apn_socketserv_typetmpl, "AoSocketServ");
	}

// --------------------------------------------------------------------
// module methods...

#define AoSocketServType \
	((PyTypeObject*)SPyGetGlobalString("AoSocketServ"))

// Returns NULL if cannot allocate.
// The reference count of any returned object will be 1.
// The created socket server object will be initialized,
// but there will be no open session.
static apn_socketserv_object* NewSocketServObject()
	{
	apn_socketserv_object* newSocketServ =
		// sets refcount to 1 if successful,
		// so decrefing should delete
		PyObject_New(apn_socketserv_object, AoSocketServType);
	if (newSocketServ == NULL)
		{
		// raise an exception with the reason set by PyObject_New
		return NULL;
		}
	SET_SESSION_CLOSED(newSocketServ->iSocketServ);
	return newSocketServ;
	}

PyObject* apn_socketserv_new(PyObject* /*self*/, PyObject* /*args*/)
	{
	return reinterpret_cast<PyObject*>(NewSocketServObject());
	}
