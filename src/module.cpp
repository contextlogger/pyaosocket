// -*- symbian-c++ -*-

//
// module.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Code for initializing the "pyaosocket" Python module.
// Also defines most of the module methods.
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

#include <e32base.h> // active scheduler
#include <f32file.h> // RFs
#include "btengine.h"
#include "local_epoc_py_utils.h"
#include "apnsocketserv.h"
#include "apnconnection.h"



/** A module method.
 */
extern PyObject* apn_immediate_new(PyObject* /*self*/,
								   PyObject* /*args*/);

/** A module method.
 */
extern PyObject* apn_socket_new(PyObject* /*self*/,
								PyObject* /*args*/);

/** A module method.
 */
extern PyObject* apn_loop_new(PyObject* /*self*/,
							  PyObject* /*args*/);

/** A module method.
 */
extern PyObject* apn_itc_new(PyObject* /*self*/,
							 PyObject* /*args*/);

/** A module method.
 */
#ifdef __HAS_FLOGGER__
extern PyObject* apn_flogger_new(PyObject* /*self*/,
								 PyObject* /*args*/);
#endif

/** A module method.
 */
extern PyObject* apn_resolver_new(PyObject* /*self*/,
								  PyObject* /*args*/);

/** A module method.
 */
extern PyObject* apn_portdisc_new(PyObject* /*self*/,
								  PyObject* /*args*/);


/** A module method.

	Returns True iff the current thread has an active scheduler
	installed.
*/
static PyObject *apn_HasActSched(PyObject* /*self*/,
								 PyObject* /*args*/)
	{
	CActiveScheduler* curr = CActiveScheduler::Current();
	if (curr)
		{
		RETURN_TRUE;
		}
	else
		{
		RETURN_FALSE;
		}
	}

/** Returns True if compiled for WINS, and False otherwise.
 */
static PyObject *apn_OnWins(PyObject* /*self*/,
							PyObject* /*args*/)
	{
#ifdef __WINS__
	RETURN_TRUE;
#else
	RETURN_FALSE;
#endif
	}

/** Returns:
	KErrNotReady if no disk in drive;
	KErrNone if disk not corrupt;
	otherwise disk is corrupt
*/
static PyObject* apn_CheckDisk(PyObject* /*self*/,
							   PyObject* args)
	{
	int l;
	char* b;
	if (!PyArg_ParseTuple(args, "u#", &b, &l))
		{
		return NULL;
		}
	TPtrC drive((TUint16*)b, l);

	RFs fs;
	TInt error = fs.Connect();
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}

	error = fs.CheckDisk(drive);

	fs.Close();

	return Py_BuildValue("i", error);
	}

/** Corrects errors on the specified FAT drive.
 */
static PyObject* apn_ScanDrive(PyObject* /*self*/,
							   PyObject* args)
	{
	int l;
	char* b;
	if (!PyArg_ParseTuple(args, "u#", &b, &l))
		{
		return NULL;
		}
	TPtrC drive((TUint16*)b, l);

	RFs fs;
	TInt error = fs.Connect();
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}

	error = fs.ScanDrive(drive);

	fs.Close();

	RETURN_ERROR_OR_PYNONE(error);
	}

/** Module method table.
 */
static const PyMethodDef apn_methods[] =
	{
	{"AoImmediate", (PyCFunction)apn_immediate_new, METH_NOARGS},
	{"AoSocket", (PyCFunction)apn_socket_new, METH_NOARGS},
	{"AoSocketServ", (PyCFunction)apn_socketserv_new, METH_NOARGS},
	{"AoConnection", (PyCFunction)apn_connection_new, METH_NOARGS},
	{"AoLoop", (PyCFunction)apn_loop_new, METH_NOARGS},
	{"AoItc", (PyCFunction)apn_itc_new, METH_NOARGS},
#ifdef __HAS_FLOGGER__
	{"AoFlogger", (PyCFunction)apn_flogger_new, METH_NOARGS},
#endif
	{"AoResolver", (PyCFunction)apn_resolver_new, METH_NOARGS},
	{"AoPortDiscoverer", (PyCFunction)apn_portdisc_new, METH_NOARGS},
	{"has_act_sched", (PyCFunction)apn_HasActSched, METH_NOARGS},
	{"on_wins", (PyCFunction)apn_OnWins, METH_NOARGS},
	{"check_disk", (PyCFunction)apn_CheckDisk, METH_VARARGS},
	{"scan_fat_disk", (PyCFunction)apn_ScanDrive, METH_VARARGS},
	{NULL, NULL}		   /* sentinel */
	};




extern TInt apn_immediate_ConstructType();
extern TInt apn_socket_ConstructType();
extern TInt apn_loop_ConstructType();
extern TInt apn_itc_ConstructType();
#ifdef __HAS_FLOGGER__
extern TInt apn_flogger_ConstructType();
#endif
extern TInt apn_resolver_ConstructType();
extern TInt apn_portdisc_ConstructType();


/** Module initializer function.

	This function has no return value, but hopefully the caller
	checks for any exceptions that may be set here.
 */
DL_EXPORT(void) initpyaosocket()
	{
	PyObject* module = Py_InitModule(
		"pyaosocket", // module name
		const_cast<PyMethodDef*>(&apn_methods[0]));
	if (!module)
		{
		// Hopefully an exception has been set.
		return;
		}

	// If any of these fail, hopefully an exception will be set.
	if (apn_immediate_ConstructType() < 0) return;
	if (apn_socket_ConstructType() < 0) return;
	if (apn_socketserv_ConstructType() < 0) return;
	if (apn_connection_ConstructType() < 0) return;
	if (apn_loop_ConstructType() < 0) return;
	if (apn_itc_ConstructType() < 0) return;
#ifdef __HAS_FLOGGER__
	if (apn_flogger_ConstructType() < 0) return;
#endif
	if (apn_resolver_ConstructType() < 0) return;
	if (apn_portdisc_ConstructType() < 0) return;
	}


#ifndef EKA2
GLDEF_C TInt E32Dll(TDllReason)
{
  return KErrNone;
}
#endif
