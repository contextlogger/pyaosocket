// 
// Copyright 2004-2008 Helsinki Institute for Information Technology
// (HIIT) and the authors. All rights reserved.
// 
// Authors: Tero Hasu <tero.hasu@hut.fi>
// 
// A wrapper for the native RConnection class.
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

#include "apnconnection.h"
#include <commdbconnpref.h>
#include "local_epoc_py_utils.h"
#include "panic.h"
#include "apnsocketserv.h"

#define EXPLICIT_SESSION_TRACKING 1
#include "settings.h"

// --------------------------------------------------------------------
// object structure...

// Python object wrapper for RConnection. An RConnection is a socket
// server sub-session, and hence an RSocketServ reference is required.
struct apn_connection_object {
  PyObject_VAR_HEAD;
  PyObject* iSocketServ; // wraps RSocketServ
  RConnection iConnection;
  DEF_SESSION_OPEN(iConnection);
  CTC_DEF_HANDLE(ctc);
};

// used from apnsocket.cpp
RConnection& ToCxxConnection(PyObject* aObject)
{
  AssertNonNull(aObject);
  return (reinterpret_cast<apn_connection_object*>(aObject))->iConnection;
}

// --------------------------------------------------------------------
// instance methods...

// Takes a socket server instance and an access point (AP) ID as the
// arguments. Applies to outbound connections only.
// 
// Note that the PyS60 socket.access_points and
// sock.select_access_point functions can be used for getting an ID,
// we do not provide functionality for that purpose.
static PyObject* apn_connection_open(apn_connection_object* self,
				     PyObject* args)
{
  AssertNonNull(self);

  PyObject* socketServ;
  TInt apId;
  if (!PyArg_ParseTuple(args, "Oi", &socketServ, &apId))
    {
      return NULL;
    }

  if (IS_SESSION_OPEN(self->iConnection))
    AoSocketPanic(EPanicSessionAlreadyExists);

  TInt err = self->iConnection.Open(ToSocketServ(socketServ));
  if (err != KErrNone)
    {
      //PyErr_SetString(PyExc_RuntimeError, "Open failed"); return NULL;
      return SPyErr_SetFromSymbianOSErr(err);
    }

  TCommDbConnPref prefs;
  prefs.SetDialogPreference(ECommDbDialogPrefDoNotPrompt);
  prefs.SetDirection(ECommDbConnectionDirectionOutgoing);
  prefs.SetIapId(apId);

  // Starts a connection synchronously.
  err = self->iConnection.Start(prefs);
  if(err != KErrNone) {
    self->iConnection.Close();

    //PyErr_SetString(PyExc_RuntimeError, "Start failed"); return NULL;
    return SPyErr_SetFromSymbianOSErr(err);
  }

  SET_SESSION_OPEN(self->iConnection);
  CTC_STORE_HANDLE(self->ctc);

  self->iSocketServ = socketServ;
  Py_INCREF(socketServ);
  
  RETURN_NO_VALUE;
}

static void EnsureClosed(apn_connection_object* self)
{
  AssertNonNull(self);
  if (IS_SESSION_OPEN(self->iConnection))
    {
      CTC_CHECK(self->ctc);
      self->iConnection.Close();
      Py_XDECREF(self->iSocketServ);
      self->iSocketServ = NULL;
      SET_SESSION_CLOSED(self->iConnection);
    }
}

static PyObject* apn_connection_close(apn_connection_object* self,
				      PyObject* /*args*/)
{
  EnsureClosed(self);
  RETURN_NO_VALUE;
}

const static PyMethodDef apn_connection_methods[] =
  {
    {"open", (PyCFunction)apn_connection_open, METH_VARARGS},
    {"close", (PyCFunction)apn_connection_close, METH_NOARGS},
    {NULL, NULL} // sentinel
  };

static void apn_dealloc_connection(apn_connection_object *self)
{
  EnsureClosed(self);
  PyObject_Del(self);
}

static PyObject *apn_connection_getattr(apn_connection_object *self,
					char *name)
{
  AssertNonNull(self);
  return Py_FindMethod((PyMethodDef*)apn_connection_methods,
		       (PyObject*)self, name);
}

// --------------------------------------------------------------------
// type...

const PyTypeObject apn_connection_typetmpl =
  {
    PyObject_HEAD_INIT(NULL)
    0,										   /*ob_size*/
    "pyaosocket.AoConnection",			  /*tp_name*/
    sizeof(apn_connection_object),					  /*tp_basicsize*/
    0,										   /*tp_itemsize*/
    /* methods */
    (destructor)apn_dealloc_connection,				  /*tp_dealloc*/
    0,										   /*tp_print*/
    (getattrfunc)apn_connection_getattr,				  /*tp_getattr*/
    0,										   /*tp_setattr*/
    0,										   /*tp_compare*/
    0,										   /*tp_repr*/
    0,										   /*tp_as_number*/
    0,										   /*tp_as_sequence*/
    0,										   /*tp_as_mapping*/
    0										  /*tp_hash*/
  };

// used from module.cpp
TInt apn_connection_ConstructType()
{
  return ConstructType(&apn_connection_typetmpl, "AoConnection");
}

// --------------------------------------------------------------------
// module methods...

#define AoConnectionType \
	((PyTypeObject*)SPyGetGlobalString("AoConnection"))

// Returns NULL if cannot allocate.
// The reference count of any returned object will be 1.
// The created socket server object will be initialized,
// but there will be no open session.
static apn_connection_object* NewConnectionObject()
{
  apn_connection_object* newConnection =
    // sets refcount to 1 if successful,
    // so decrefing should delete
    PyObject_New(apn_connection_object, AoConnectionType);
  if (newConnection == NULL)
    {
      // raise an exception with the reason set by PyObject_New
      return NULL;
    }
  SET_SESSION_CLOSED(newConnection->iConnection);
  return newConnection;
}

// used from module.cpp
PyObject* apn_connection_new(PyObject* /*self*/, PyObject* /*args*/)
{
  return reinterpret_cast<PyObject*>(NewConnectionObject());
}
