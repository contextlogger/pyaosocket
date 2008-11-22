// -*- symbian-c++ -*-

//
// apnsocket.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// A wrapper for the native RSocket class, with selected functionality
// made available. Supports TCP and Bluetooth.
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

#include <apgcli.h> // apgrfx.lib
#include <eikenv.h>
#include <e32std.h>
#include <f32file.h>
#include <es_sock.h>
#include <in_sock.h>

#include "btengine.h"
#include "local_epoc_py_utils.h"
#include "logging.h"
#include "panic.h"
#include "resolution.h"
#include "settings.h"
#include "socketaos.h"
#include "apnsocketserv.h"
#include "apnconnection.h"

// --------------------------------------------------------------------
// CAoSocket interface...

NONSHARABLE_CLASS(CAoSocket) : public CBase, public MAoSockObserver
	{
public:
	static CAoSocket* NewL();
	~CAoSocket();

	//// socket server session accessors
	void SetSocketServ(PyObject* aSocketServ);
	RSocketServ& SocketServ() const;

	//// RConnection accessor(s).
	void SetConnection(PyObject* aConnection);

	//// socket opening methods (synchronous)
	TInt Blank();
	TInt OpenTcp();
	TInt OpenBt();

	void GetAvailableBtPortL(TInt& aPort);

	//// listening methods (synchronous)
	TInt ListenTcp(const TDesC& aHostName, TInt aPort, TInt aQueueSize);
	void ListenBtL(TInt aPort, TInt aQueueSize,
				   TUint aServiceId, const TDesC& aServiceName);

	//// methods for connecting to a server (asynchronously)
	void ConnectTcpL(const TDesC& aHostName,
					 TInt aPort,
					 PyObject* aCallback,
					 PyObject* aParam);
	void ConnectBtL(const TDesC& aBtAddress,
					TInt aPort,
					PyObject* aCallback,
					PyObject* aParam);

	void ConfigBtL(PyObject* aCallback, PyObject* aParam);

	//// for accepting a client (asynchronously)
	void AcceptL(PyObject* aBlankSocket, PyObject* aCallback,
				 PyObject* aParam);

	// closes the socket (okay to call even if not open);
	// the flag indicates whether should also get rid of
	// the socket server session
	void Close(TBool aFull);

	// synchronous -- returns an error code
	TInt SendEof();

	void WriteDataL(const TDesC8& aData, PyObject* aCallback,
					PyObject* aParam);
	void ReadSomeL(TInt aMaxSize, PyObject* aCallback,
				   PyObject* aParam);
	void ReadExactL(TInt aSize, PyObject* aCallback,
					PyObject* aParam);

	//// these are safe to call even if the socket is not open
	void CancelWrite();
	void CancelRead();
	void CancelAccept();
	void CancelConnect();
	void CancelAll();

	TInt WriteSync(const TDesC8& aData);
	TInt ReadSync(TDes8& aData);

	void ApplyAccepter(CSocketAccepter& anAccepter);
	void ApplyAccepterL(CBtAccepter& anAccepter);

private:
	CAoSocket();
	void ConstructL();

	RSocket iRSocket;
	DEF_SESSION_OPEN(iRSocket);

	TBool IsSocketOpen() const { return IS_SUBSESSION_OPEN(iRSocket); }
	TBool HaveSocketServ() const { return (iSocketServ != NULL); }

	// calls Close() with the specified parameter if a session exists
	void EnsureNoSession(TBool aFull);

	// Non-NULL between SetSocketServ() and Close().
	// Note that we refer to this object, but do not own it.
	// We maintain a reference to it mostly to ensure that
	// it does not get garbage collected while we still need
	// the session.
	PyObject* iSocketServ;

	// NULL if not set. Cleared by Close().
	PyObject* iConnection;

	// these are created lazily, and then exist until Close()
	// gets called
	CSocketReader* iSocketReader;
	CSocketWriter* iSocketWriter;
	CSocketAccepter* iTcpAccepter; // for TCP only
	CResolvingConnecter* iTcpConnecter; // for TCP only
	CBtConnecter* iBtConnecter; // for BT only
	CBtAccepter* iBtAccepter; // for BT only

	enum TMode
		{
		EPipeMode = 1,
		ETcpMode,
		EBtMode
		};
	TMode iMode;

	// these are set (and refcounts incremented) upon making
	// a request; these are freed (and refcounts decremented)
	// prior to recording new values, or upon the destruction
	// of an instance of this class
	PyObject* iReadCallback; // for Recv()
	PyObject* iReadCallbackParam; // for Recv()
	void FreeReadParams();
	PyObject* iWriteCallback; // for Write()
	PyObject* iWriteCallbackParam; // for Write()
	void FreeWriteParams();
	PyObject* iAcceptCallback; // for Accept()
	PyObject* iAcceptCallbackParam; // for Accept()
	PyObject* iBlankSocket; // for Accept()
	void FreeAcceptParams();
	PyObject* iConnectCallback; // for Connect()
	PyObject* iConnectCallbackParam; // for Connect()
	void FreeConnectParams();

	// may not be valid if there is no request pending
	PyThreadState* iThreadState;

	CTC_DEF_HANDLE(ctc);

private: // MAoSockObserver
	void DataWritten(TInt aError);
	void DataRead(TInt aError, const TDesC8& aData);
	void ClientAccepted(TInt aError);
	void ClientConnected(TInt aError);
	void SocketConfigured(TInt aError);
	};

// --------------------------------------------------------------------
// object structure...

// we store the state we require in a Python object
typedef struct
	{
	PyObject_VAR_HEAD;
	CAoSocket* iAoSocket;
	} apn_socket_object;

// --------------------------------------------------------------------
// CAoSocket implementation...

static apn_socket_object* NewSocketObject();

TInt CAoSocket::WriteSync(const TDesC8& aData)
	{
	if (!IsSocketOpen())
		{
		AoSocketPanic(EPanicSocketNotOpen);
		}

	TRequestStatus status;
	iRSocket.Write(aData, status);
	WAIT_STAT(status);
	return status.Int();
	}

TInt CAoSocket::ReadSync(TDes8& aData)
	{
	if (!IsSocketOpen())
		{
		AoSocketPanic(EPanicSocketNotOpen);
		}

	TRequestStatus status;
	TSockXfrLength len;
	iRSocket.RecvOneOrMore(aData, 0, status, len);
	WAIT_STAT(status);
	return status.Int();
	}

TInt CAoSocket::SendEof()
	{
	if (!IsSocketOpen())
		{
		AoSocketPanic(EPanicSocketNotOpen);
		}

	TRequestStatus status;
	iRSocket.Shutdown(RSocket::EStopOutput, status);
	WAIT_STAT(status);
	return status.Int();
	}

// takes ownership of aArgList;
// returns a true value iff successful
static TBool CallCallback(PyObject* aCallback, PyObject* aArgList)
	{
	if (!aCallback)
		{
		// The callback has not been set.
		// This is a programmer error.
		AssertFail();
		return EFalse;
		}
	if (!aArgList)
		{
		// Could not allocate memory for the argument list.
		// A Python exception will have been set,
		// but the stack trace might look odd,
		// so clear the exception.
		PyErr_Clear();
		AoSocketPanic(EPanicOutOfMemory);
		return EFalse;
		}
	PyObject* result = PyObject_CallObject(aCallback, aArgList);
	Py_DECREF(aArgList);
	if (result == NULL)
		{
		// The call caused an exception.
		// A Python exception will have been set,
		// but the stack trace might look odd.
		PyErr_Clear();
		AoSocketPanic(EPanicExceptionInCallback);
		return EFalse;
		}
	else
		{
		// This should also free the result object.
		Py_DECREF(result);
		}
	return ETrue;
	}

void CAoSocket::FreeReadParams()
	{
	if (iReadCallback)
		{
		Py_DECREF(iReadCallback);
		iReadCallback = NULL;
		}
	if (iReadCallbackParam)
		{
		Py_DECREF(iReadCallbackParam);
		iReadCallbackParam = NULL;
		}
	}

void CAoSocket::FreeWriteParams()
	{
	if (iWriteCallback)
		{
		Py_DECREF(iWriteCallback);
		iWriteCallback = NULL;
		}
	if (iWriteCallbackParam)
		{
		Py_DECREF(iWriteCallbackParam);
		iWriteCallbackParam = NULL;
		}
	}

void CAoSocket::FreeAcceptParams()
	{
	if (iAcceptCallback)
		{
		Py_DECREF(iAcceptCallback);
		iAcceptCallback = NULL;
		}
	if (iAcceptCallbackParam)
		{
		Py_DECREF(iAcceptCallbackParam);
		iAcceptCallbackParam = NULL;
		}
	if (iBlankSocket)
		{
		Py_DECREF(iBlankSocket);
		iBlankSocket = NULL;
		}
	}

void CAoSocket::FreeConnectParams()
	{
	if (iConnectCallback)
		{
		Py_DECREF(iConnectCallback);
		iConnectCallback = NULL;
		}
	if (iConnectCallbackParam)
		{
		Py_DECREF(iConnectCallbackParam);
		iConnectCallbackParam = NULL;
		}
	}

TInt CAoSocket::OpenTcp()
	{
	if (!HaveSocketServ())
		{
		AoSocketPanic(EPanicSocketServNotSet);
		}
	RSocketServ& socketServ = SocketServ();

	EnsureNoSession(EFalse);
	iMode = ETcpMode;

	TInt error;
	if (iConnection)
		{
		error = iRSocket.Open(socketServ, KAfInet,
							  KSockStream, KProtocolInetTcp,
							  ToCxxConnection(iConnection));
		}
	else
		{
		error = iRSocket.Open(socketServ, KAfInet,
							  KSockStream, KProtocolInetTcp);
		}
	if (!error)
		{
		SET_SESSION_OPEN(iRSocket);
		}
	return error;
	}

TInt CAoSocket::OpenBt()
	{
	if (!HaveSocketServ())
		{
		AoSocketPanic(EPanicSocketServNotSet);
		}
	RSocketServ& socketServ = SocketServ();

	EnsureNoSession(EFalse);
	iMode = EBtMode;

	TInt error = BtEngine::OpenSocket(socketServ, iRSocket);
	if (!error)
		{
		SET_SESSION_OPEN(iRSocket);
		}
	return error;
	}

/** Note that this method is not dependent on transport type.
 */
TInt CAoSocket::Blank()
	{
	if (!HaveSocketServ())
		{
		AoSocketPanic(EPanicSocketServNotSet);
		}
	RSocketServ& socketServ = SocketServ();

	EnsureNoSession(EFalse);
	iMode = EPipeMode;

	TInt error = iRSocket.Open(socketServ);
	if (!error)
		{
		SET_SESSION_OPEN(iRSocket);
		}
	return error;
	}

void CAoSocket::ConnectBtL(const TDesC& aBtAddress,
						   TInt aPort,
						   PyObject* aCallback,
						   PyObject* aParam)
	{
	if (!IsSocketOpen())
		{
		AoSocketPanic(EPanicSocketNotOpen);
		}
	if (iMode != EBtMode)
		{
		AoSocketPanic(EPanicWrongTransportMode);
		}
	if (!iBtConnecter)
		{
		iBtConnecter = CBtConnecter::NewL(*this, iRSocket);
		}

	Py_INCREF(aCallback);
	Py_INCREF(aParam);
	FreeConnectParams();
	iConnectCallback = aCallback;
	iConnectCallbackParam = aParam;

	iThreadState = PyThreadState_Get();

	TBTDevAddr btDevAddr;
	BtEngine::StringToDevAddr(btDevAddr, aBtAddress);

	iBtConnecter->ConnectL(btDevAddr, aPort);
	}

void CAoSocket::ConnectTcpL(const TDesC& aHostName,
							TInt aPort,
							PyObject* aCallback,
							PyObject* aParam)
	{
	if (!IsSocketOpen())
		{
		AoSocketPanic(EPanicSocketNotOpen);
		}
	if (iMode != ETcpMode)
		{
		AoSocketPanic(EPanicWrongTransportMode);
		}
	if (!iTcpConnecter)
		{
		iTcpConnecter = CResolvingConnecter::NewL(
			*this, iRSocket, SocketServ(),
			iConnection ? (&ToCxxConnection(iConnection)) : NULL);
		}

	Py_INCREF(aCallback);
	Py_INCREF(aParam);
	FreeConnectParams();
	iConnectCallback = aCallback;
	iConnectCallbackParam = aParam;

	iThreadState = PyThreadState_Get();

	iTcpConnecter->Connect(aHostName, aPort);
	}

void CAoSocket::CancelAll()
	{
	CancelRead();
	CancelWrite();
	CancelAccept();
	CancelConnect();
	}

/** It is okay to call this method even when there is
	no request pending, or even when the socket is closed.
*/
void CAoSocket::CancelConnect()
	{
	if (IsSocketOpen())
		{
		if (iMode == ETcpMode && iTcpConnecter)
			{
			iTcpConnecter->Cancel();
			}
		if (iMode == EBtMode && iBtConnecter)
			{
			iBtConnecter->Cancel();
			}
		}
	}

void CAoSocket::SocketConfigured(TInt aError)
	{
	AssertNonNull(iAcceptCallback);
	AssertNonNull(iAcceptCallbackParam);

	PyEval_RestoreThread(iThreadState);

	PyObject* arg;
	arg = Py_BuildValue("(iO)", aError, iAcceptCallbackParam);
	CallCallback(iAcceptCallback, arg); // owns 'arg'

	PyEval_SaveThread();

	// the callback may have done anything, including
	// deleting the object whose method we are in,
	// so do not attempt to access any property anymore
	}

/** Note that even if a connect fails, we will not close
	the socket. In such a situation, the user may want
	to do a close explicitly to ensure that the socket
	is ready for reuse, say for some other purpose.
*/
void CAoSocket::ClientConnected(TInt aError)
	{
	AssertNonNull(iConnectCallback);
	AssertNonNull(iConnectCallbackParam);

	PyEval_RestoreThread(iThreadState);

	PyObject* arg;
	arg = Py_BuildValue("(iO)", aError, iConnectCallbackParam);
	CallCallback(iConnectCallback, arg); // owns 'arg'

	PyEval_SaveThread();

	// the callback may have done anything, including
	// deleting the object whose method we are in,
	// so do not attempt to access any property anymore
	}

/** It is okay to call this method even when there is
	no request pending, or even when the socket is closed.
*/
void CAoSocket::CancelAccept()
	{
	if (IsSocketOpen())
		{
		if (iMode == ETcpMode && iTcpAccepter)
			{
			iTcpAccepter->Cancel();
			}
		if (iMode == EBtMode && iBtAccepter)
			{
			iBtAccepter->Cancel();
			}
		}
	}

/** Always takes ownership of the parameters
	(i.e. will take care of the refcounts).
*/
void CAoSocket::AcceptL(PyObject* aBlankSocket,
						PyObject* aCallback,
						PyObject* aParam)
	{
	if (iMode == ETcpMode)
		{
		if (!iTcpAccepter)
			{
			iTcpAccepter = new (ELeave) CSocketAccepter(*this, iRSocket);
			}
		}
	else if (iMode == EBtMode)
		{
		if (!iBtAccepter)
			{
			AoSocketPanic(EPanicAcceptBeforeListen);
			}
		}
	else
		{
		AoSocketPanic(EPanicAcceptBeforeListen);
		}

	Py_INCREF(aBlankSocket);
	Py_INCREF(aCallback);
	Py_INCREF(aParam);
	FreeAcceptParams();
	iAcceptCallback = aCallback;
	iAcceptCallbackParam = aParam;
	iBlankSocket = aBlankSocket;

	iThreadState = PyThreadState_Get();

	apn_socket_object* blsock =
		reinterpret_cast<apn_socket_object*>(aBlankSocket);
	AssertNonNull(blsock->iAoSocket);
	if (iMode == ETcpMode)
		{
		blsock->iAoSocket->ApplyAccepter(*iTcpAccepter);
		}
	else
		{
		blsock->iAoSocket->ApplyAccepterL(*iBtAccepter);
		}
	}

void CAoSocket::ApplyAccepter(CSocketAccepter& anAccepter)
	{
	if (iMode != EPipeMode) AssertFail();
	anAccepter.Accept(iRSocket);
	}

void CAoSocket::ApplyAccepterL(CBtAccepter& anAccepter)
	{
	if (iMode != EPipeMode) AssertFail();
	anAccepter.AcceptL(iRSocket);
	}

void CAoSocket::ClientAccepted(TInt aError)
	{
	AssertNonNull(iAcceptCallback);
	AssertNonNull(iAcceptCallbackParam);
	AssertNonNull(iBlankSocket);

	PyEval_RestoreThread(iThreadState);

	PyObject* arg;
	if (aError == KErrNone)
		{
		// this call increments the 'cbParam' and
		// 'blankSocket' reference counts
		arg = Py_BuildValue("(iOO)", aError, iBlankSocket,
							iAcceptCallbackParam);
		}
	else
		{
		arg = Py_BuildValue("(iOO)", aError, Py_None,
							iAcceptCallbackParam);
		}

	CallCallback(iAcceptCallback, arg); // owns 'arg'

	PyEval_SaveThread();

	// the callback may have done anything, including
	// deleting the object whose method we are in,
	// so do not attempt to access any property anymore
	}

/** It is okay to call this method even when there is
	no request pending, or even when the socket is closed.
*/
void CAoSocket::CancelRead()
	{
	if (IsSocketOpen() && iSocketReader)
		{
		iSocketReader->Cancel();
		}
	}

/** Always takes ownership of the parameters
	(i.e. will take care of the refcounts).
*/
void CAoSocket::ReadExactL(TInt aSize,
						   PyObject* aCallback,
						   PyObject* aParam)
	{
	if (!iSocketReader)
		{
		//// note that neither CActive (the base class) or this
		//// class has ConstructL() to call
		iSocketReader = new (ELeave) CSocketReader(*this, iRSocket);
		}

	Py_INCREF(aCallback);
	Py_INCREF(aParam);
	FreeReadParams();
	iReadCallback = aCallback;
	iReadCallbackParam = aParam;

	iThreadState = PyThreadState_Get();

	iSocketReader->ReadExactL(aSize);
	}

/** Always takes ownership of the parameters
	(i.e. will take care of the refcounts).
*/
void CAoSocket::ReadSomeL(TInt aMaxSize,
						  PyObject* aCallback,
						  PyObject* aParam)
	{
	if (!iSocketReader)
		{
		iSocketReader = new (ELeave) CSocketReader(*this, iRSocket);
		}

	Py_INCREF(aCallback);
	Py_INCREF(aParam);
	FreeReadParams();
	iReadCallback = aCallback;
	iReadCallbackParam = aParam;

	iThreadState = PyThreadState_Get();

	iSocketReader->ReadSomeL(aMaxSize);
	}

void CAoSocket::DataRead(TInt aError, const TDesC8& aData)
	{
	AssertNonNull(iReadCallback);
	AssertNonNull(iReadCallbackParam);

	PyEval_RestoreThread(iThreadState);

	PyObject* arg;
	if (aError == KErrNone)
		{
		// this call increments the 'cbParam' reference count
		arg = Py_BuildValue("(is#O)", aError,
							aData.Ptr(), aData.Length(),
							iReadCallbackParam);
		}
#if 0
	else if (aError == KErrEof)
		{
		arg = Py_BuildValue("(is#O)", 0, NULL, 0, iReadCallbackParam);
		}
#endif
	else
		{
		arg = Py_BuildValue("(is#O)", aError, NULL,
							0, iReadCallbackParam);
		}

	CallCallback(iReadCallback, arg); // owns 'arg'

	PyEval_SaveThread();

	// the callback may have done anything, including
	// deleting the object whose method we are in,
	// so do not attempt to access any property anymore
	}

/** It is okay to call this method even when there is
	no request pending, or even when the socket is closed.
*/
void CAoSocket::CancelWrite()
	{
	if (IsSocketOpen() && iSocketWriter)
		{
		iSocketWriter->Cancel();
		}
	}

/** Always takes ownership of the parameters
	(i.e. will take care of the refcounts).
*/
void CAoSocket::WriteDataL(const TDesC8& aData,
						   PyObject* aCallback,
						   PyObject* aParam)
	{
	if (!iSocketWriter)
		{
		iSocketWriter = new (ELeave) CSocketWriter(*this, iRSocket);
		}

	Py_INCREF(aCallback);
	Py_INCREF(aParam);
	FreeWriteParams();
	iWriteCallback = aCallback;
	iWriteCallbackParam = aParam;

	iThreadState = PyThreadState_Get();

	iSocketWriter->WriteDataL(aData);
	}

void CAoSocket::DataWritten(TInt aError)
	{
	AssertNonNull(iWriteCallback);
	AssertNonNull(iWriteCallbackParam);

	PyEval_RestoreThread(iThreadState);

	PyObject* arg = Py_BuildValue("(iO)", aError, iWriteCallbackParam);

	CallCallback(iWriteCallback, arg); // owns 'arg'

	PyEval_SaveThread();

	// the callback may have done anything, including
	// deleting the object whose method we are in,
	// so do not attempt to access any property anymore
	}

void CAoSocket::SetSocketServ(PyObject* aSocketServ)
	{
	// The socket server session is shared by RConnection,
	// RHostResolver, and RSocket, and really cannot be changed on
	// the fly. Everything must be closed before the sesion can be
	// changed.
	EnsureNoSession(ETrue);
	Py_INCREF(aSocketServ);
	iSocketServ = aSocketServ;
	}

RSocketServ& CAoSocket::SocketServ() const
	{
	AssertNonNull(iSocketServ);
	RSocketServ& socketServ = ToSocketServ(iSocketServ);
	return socketServ;
	}

void CAoSocket::SetConnection(PyObject* aConnection)
	{
	// RConnection is shared by RHostResolver and RSocket, and
	// again, we must ensure those are closed before we can go
	// change the connection.
	//
	// Also, do not know if it would be okay for say an RSocket
	// and an RConnection to have a different RSocketServ session,
	// but if that is not okay then some things will error out, but
	// reference counting and all other internal consistency
	// should be fine within AoSocket.
	EnsureNoSession(EFalse);

	if (aConnection != Py_None)
		{
		Py_INCREF(aConnection);
		iConnection = aConnection;
		}
	}

void CAoSocket::EnsureNoSession(TBool aFull)
	{
	if (IS_SUBSESSION_OPEN(iRSocket))
		{
		Close(aFull);
		}
	}

CAoSocket* CAoSocket::NewL()
	{
	CAoSocket* object = new (ELeave) CAoSocket;
	CleanupStack::PushL(object);
	object->ConstructL();
	CleanupStack::Pop();
	return object;
	}

CAoSocket::CAoSocket()
	{
	// Doing this here to make sure it is available when
	// calling Close(), even though doing it here means
	// that it becomes impossible to change ownership
	// of this object between threads.
	CTC_STORE_HANDLE(ctc);
	}

CAoSocket::~CAoSocket()
	{
	Close(ETrue);
	}

void CAoSocket::Close(TBool aFull)
	{
	CTC_CHECK(ctc);

	CancelRead();
	delete iSocketReader;
	iSocketReader = NULL;

	CancelWrite();
	delete iSocketWriter;
	iSocketWriter = NULL;

	CancelAccept();
	delete iTcpAccepter;
	iTcpAccepter = NULL;
	delete iBtAccepter;
	iBtAccepter = NULL;

	CancelConnect();
	delete iTcpConnecter;
	iTcpConnecter = NULL;
	delete iBtConnecter;
	iBtConnecter = NULL;

	FreeReadParams();
	FreeWriteParams();
	FreeAcceptParams();
	FreeConnectParams();

	if (IS_SUBSESSION_OPEN(iRSocket))
		{
		iRSocket.Close();
		SET_SESSION_CLOSED(iRSocket);
		}

	Py_XDECREF(iConnection);
	iConnection = NULL;

	if (iSocketServ && aFull)
		{
		Py_DECREF(iSocketServ);
		iSocketServ = NULL;
		}
	}

void CAoSocket::ConstructL()
	{
	// nothing
	}

/** Always takes ownership of the parameters
	(i.e. will take care of the refcounts).
*/
void CAoSocket::ConfigBtL(PyObject* aCallback, PyObject* aParam)

	{
	if (!iBtAccepter)
		{
		AoSocketPanic(EPanicConfigBeforeListen);
		}

	Py_INCREF(aCallback);
	Py_INCREF(aParam);
	FreeAcceptParams();
	iAcceptCallback = aCallback;
	iAcceptCallbackParam = aParam;

	iThreadState = PyThreadState_Get();

	iBtAccepter->ConfigureL();
	}

void CAoSocket::GetAvailableBtPortL(TInt& aPort)
	{
	if (!IsSocketOpen())
		{
		AoSocketPanic(EPanicSocketNotOpen);
		}
	if (iMode != EBtMode)
		{
		AoSocketPanic(EPanicWrongTransportMode);
		}
	if (!iBtAccepter)
		{
		// note that the socket does not need to be open
		// when creating this object
		iBtAccepter = CBtAccepter::NewL(*this, SocketServ(), iRSocket);
		}
	User::LeaveIfError(iBtAccepter->GetAvailablePort(aPort));
	}

void CAoSocket::ListenBtL(TInt aPort, TInt aQueueSize,
						  TUint aServiceId, const TDesC& aServiceName)
	{
	if (!IsSocketOpen())
		{
		AoSocketPanic(EPanicSocketNotOpen);
		}
	if (iMode != EBtMode)
		{
		AoSocketPanic(EPanicWrongTransportMode);
		}
	if (!iBtAccepter)
		{
		// note that the socket does not need to be open
		// when creating this object
		iBtAccepter = CBtAccepter::NewL(*this, SocketServ(), iRSocket);
		}

	iBtAccepter->ListenL(aPort, aQueueSize, aServiceId, aServiceName);
	}

TInt CAoSocket::ListenTcp(const TDesC& aHostName,
						  TInt aPort,
						  TInt aQueueSize)
	{
	if (!IsSocketOpen())
		{
		AoSocketPanic(EPanicSocketNotOpen);
		}
	if (iMode != ETcpMode)
		{
		AoSocketPanic(EPanicWrongTransportMode);
		}

	RSocketServ& socketServ = SocketServ();

	TSockAddr sockAddr;
	/* In theory, this could take a long time, too, but we
	   are assuming local addresses to resolve quickly.
	   If we felt we couldn't assume that, we could always
	   implement an asynchronous version of ``ListenTcp``, too. */
	TInt error = Resolve(socketServ, aHostName, sockAddr);
	if (error)
		{
		Close(EFalse);
		return error;
		}

	sockAddr.SetPort(aPort);
	error = iRSocket.Bind(sockAddr);
	if (error)
		{
		Close(EFalse);
		return error;
		}

	error = iRSocket.Listen(aQueueSize);
	if (error)
		{
		Close(EFalse);
		return error;
		}

	return KErrNone;
	}

// --------------------------------------------------------------------
// instance methods...

/** If you have called ``open``, you must call ``close``
	to clean up afterwards. */
static PyObject* apn_socket_close(apn_socket_object* self,
								  PyObject* /*args*/)
	{
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	self->iAoSocket->Close(ETrue);
	RETURN_NO_VALUE;
	}

static PyObject* apn_socket_setss(apn_socket_object* self,
								 PyObject* args)
	{
	PyObject* ss;
	if (!PyArg_ParseTuple(args, "O", &ss))
		{
		return NULL;
		}
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	self->iAoSocket->SetSocketServ(ss);
	RETURN_NO_VALUE;
	}

static PyObject* apn_socket_setconn(apn_socket_object* self,
									PyObject* args)
	{
	PyObject* obj;
	if (!PyArg_ParseTuple(args, "O", &obj))
		{
		return NULL;
		}
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	self->iAoSocket->SetConnection(obj);
	RETURN_NO_VALUE;
	}

static PyObject* apn_socket_openbt(apn_socket_object* self,
								   PyObject* /*args*/)
	{
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TInt error = self->iAoSocket->OpenBt();
	RETURN_ERROR_OR_PYNONE(error);
	}

// xxx we should maybe add an optional or keyword argument here for taking an RConnection object as an argument, or alternatively we could have a separate method for setting an RConnection handle here prior to calling open (similar to set_socket_serv) -- in any case python refcounting should make sure it stays alive for long as required
// xxx we will hopefully get ap selection code from pys60, but have to read the apache license
static PyObject* apn_socket_opentcp(apn_socket_object* self,
								   PyObject* /*args*/)
	{
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TInt error = self->iAoSocket->OpenTcp();
	RETURN_ERROR_OR_PYNONE(error);
	}

static PyObject* apn_socket_blank(apn_socket_object* self,
								  PyObject* /*args*/)
	{
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TInt error = self->iAoSocket->Blank();
	RETURN_ERROR_OR_PYNONE(error);
	}

static PyObject* apn_socket_getbtport(apn_socket_object* self,
									  PyObject* /*args*/)
	{
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TInt port = 0;
	TRAPD(error, self->iAoSocket->GetAvailableBtPortL(port));
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}
	return Py_BuildValue("i", port);
	}

static PyObject* apn_socket_listenbt(apn_socket_object* self,
									 PyObject* args)
	{
	TInt port;
	TInt qSize;
	unsigned int sid; // would like to use "I"
	char* b;
	int l;
	if (!PyArg_ParseTuple(args, "iiiu#", &port, &qSize, &sid, &b, &l))
		{
		return NULL;
		}
	TPtrC serviceName((TUint16*)b, l);

	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TRAPD(error, self->iAoSocket->ListenBtL(port, qSize, sid, serviceName));
	RETURN_ERROR_OR_PYNONE(error);
	}

static PyObject* apn_socket_listentcp(apn_socket_object* self,
									  PyObject* args)
	{
	int l;
	char* b;
	TInt port;
	TInt qs;
	if (!PyArg_ParseTuple(args, "u#ii", &b, &l, &port, &qs))
		{
		return NULL;
		}
	TPtrC hostName((TUint16*)b, l);

	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TInt error = self->iAoSocket->ListenTcp(hostName, port, qs);
	RETURN_ERROR_OR_PYNONE(error);
	}

// signals and EOF, and blocks until the counterpart does the same
static PyObject* apn_socket_sendeof(apn_socket_object* self,
									PyObject* /*args*/)
	{
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TInt error = self->iAoSocket->SendEof();
	RETURN_ERROR_OR_PYNONE(error);
	}

static PyObject* apn_socket_configbt(apn_socket_object* self,
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

	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TRAPD(error, self->iAoSocket->ConfigBtL(cb, param));
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}

	RETURN_NO_VALUE;
	}

static PyObject* apn_socket_connectbt(apn_socket_object* self,
									  PyObject* args)
	{
	char* b;
	int l;
	TInt port;
	PyObject* cb;
	PyObject* param;
	if (!PyArg_ParseTuple(args, "u#iOO", &b, &l, &port, &cb, &param))
		{
		return NULL;
		}
	if (!PyCallable_Check(cb))
		{
		PyErr_SetString(PyExc_TypeError, "parameter must be callable");
		return NULL;
		}
	TPtrC host((TUint16*)b, l);

	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TRAPD(error, self->iAoSocket->ConnectBtL(host, port, cb, param));
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}

	RETURN_NO_VALUE;
	}

static PyObject* apn_socket_connecttcp(apn_socket_object* self,
									   PyObject* args)
	{
	char* b;
	int l;
	TInt port;
	PyObject* cb;
	PyObject* param;
	if (!PyArg_ParseTuple(args, "u#iOO", &b, &l, &port, &cb, &param))
		{
		return NULL;
		}
	if (!PyCallable_Check(cb))
		{
		PyErr_SetString(PyExc_TypeError, "parameter must be callable");
		return NULL;
		}
	TPtrC host((TUint16*)b, l);

	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TRAPD(error, self->iAoSocket->ConnectTcpL(host, port, cb, param));
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}

	RETURN_NO_VALUE;
	}

// takes a byte string to send, as well as a callback function
// and its parameter
static PyObject* apn_socket_write(apn_socket_object* self,
								  PyObject* args)
	{
	char* b;
	int l;
	PyObject* cb;
	PyObject* param;
	if (!PyArg_ParseTuple(args, "s#OO", &b, &l, &cb, &param))
		{
		return NULL;
		}
	if (!PyCallable_Check(cb))
		{
		PyErr_SetString(PyExc_TypeError, "parameter must be callable");
		return NULL;
		}
	TPtrC8 data((TUint8*)b, l);

	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TRAPD(error, self->iAoSocket->WriteDataL(data, cb, param));
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}

	RETURN_NO_VALUE;
	}

// takes max size and callback function and its parameter
static PyObject* apn_socket_readsome(apn_socket_object* self,
									 PyObject* args)
	{
	int maxSize;
	PyObject* cb;
	PyObject* param;
	// Note that PyArg_ParseTuple does not increase the refcount
	// for "O" parameters.
	if (!PyArg_ParseTuple(args, "iOO", &maxSize, &cb, &param))
		{
		return NULL;
		}
	if (!PyCallable_Check(cb))
		{
		PyErr_SetString(PyExc_TypeError, "parameter must be callable");
		return NULL;
		}

	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TRAPD(error, self->iAoSocket->ReadSomeL(maxSize, cb, param));
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}

	RETURN_NO_VALUE;
	}

// takes size and callback function and its parameter
static PyObject* apn_socket_readexact(apn_socket_object* self,
									  PyObject* args)
	{
	int size;
	PyObject* cb;
	PyObject* param;
	if (!PyArg_ParseTuple(args, "iOO", &size, &cb, &param))
		{
		return NULL;
		}
	if (!PyCallable_Check(cb))
		{
		PyErr_SetString(PyExc_TypeError, "parameter must be callable");
		return NULL;
		}

	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TRAPD(error, self->iAoSocket->ReadExactL(size, cb, param));
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}

	RETURN_NO_VALUE;
	}

// takes a blank socket, and a callback function and its parameter
static PyObject* apn_socket_accept(apn_socket_object* self,
								   PyObject* args)
	{
	//// get and check arguments
	PyObject* bs;
	PyObject* cb;
	PyObject* param;
	if (!PyArg_ParseTuple(args, "OOO", &bs, &cb, &param))
		{
		return NULL;
		}
	if (!PyCallable_Check(cb))
		{
		PyErr_SetString(PyExc_TypeError, "parameter must be callable");
		return NULL;
		}

	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	TRAPD(error, self->iAoSocket->AcceptL(bs, cb, param));
	RETURN_ERROR_OR_PYNONE(error);
	}

static PyObject* apn_socket_cancelwrite(apn_socket_object* self,
										PyObject* /*args*/)
	{
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	self->iAoSocket->CancelWrite();
	RETURN_NO_VALUE;
	}

static PyObject* apn_socket_cancelread(apn_socket_object* self,
									   PyObject* /*args*/)
	{
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	self->iAoSocket->CancelRead();
	RETURN_NO_VALUE;
	}

static PyObject* apn_socket_cancelaccept(apn_socket_object* self,
										 PyObject* /*args*/)
	{
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	self->iAoSocket->CancelAccept();
	RETURN_NO_VALUE;
	}

static PyObject* apn_socket_cancelconnect(apn_socket_object* self,
										  PyObject* /*args*/)
	{
	AssertNonNull(self);
	AssertNonNull(self->iAoSocket);
	self->iAoSocket->CancelConnect();
	RETURN_NO_VALUE;
	}

static PyObject* apn_socket_syncwrite(apn_socket_object* self,
									  PyObject* args)
	{
	AssertNonNull(self);

	char* b;
	int l;
	if (!PyArg_ParseTuple(args, "s#", &b, &l))
		{
		return NULL;
		}
	TPtrC8 data((TUint8*)b, l);

	AssertNonNull(self->iAoSocket);
	TInt error = self->iAoSocket->WriteSync(data);
	RETURN_ERROR_OR_PYNONE(error);
	}

static PyObject* apn_socket_syncread(apn_socket_object* self,
									 PyObject* args)
	{
	AssertNonNull(self);

	int maxSize;
	if (!PyArg_ParseTuple(args, "i", &maxSize))
		{
		return NULL;
		}

	HBufC8* buf = HBufC8::New(maxSize);
	if (buf == NULL)
		{
		return SPyErr_SetFromSymbianOSErr(KErrNoMemory);
		}
	// this sets length to zero
	TPtr8 ptr(const_cast<TUint8*>(buf->Ptr()), maxSize);

	AssertNonNull(self->iAoSocket);
	TInt error = self->iAoSocket->ReadSync(ptr);
	if (error == KErrNone)
		{
		PyObject* ret = Py_BuildValue("s#", ptr.Ptr(), ptr.Length());
		delete buf;
		return ret;
		}
	else if (error == KErrEof)
		{
		delete buf;
		// we return an empty string when we get an EOF,
		// as seems to be typical in Python libraries
		return Py_BuildValue("s#", NULL, 0);
		}
	else // some other error
		{
		delete buf;
		return SPyErr_SetFromSymbianOSErr(error);
		}
	}

const static PyMethodDef apn_socket_methods[] =
	{
	//// synchronous calls
	{"set_socket_serv", (PyCFunction)apn_socket_setss, METH_VARARGS},
	{"set_connection", (PyCFunction)apn_socket_setconn, METH_VARARGS},
	{"blank", (PyCFunction)apn_socket_blank, METH_NOARGS},
	{"open_tcp", (PyCFunction)apn_socket_opentcp, METH_NOARGS},
	{"open_bt", (PyCFunction)apn_socket_openbt, METH_NOARGS},
	{"close", (PyCFunction)apn_socket_close, METH_NOARGS},
	{"listen_tcp", (PyCFunction)apn_socket_listentcp, METH_VARARGS},
	{"listen_bt", (PyCFunction)apn_socket_listenbt, METH_VARARGS},
	{"send_eof", (PyCFunction)apn_socket_sendeof, METH_NOARGS},
	{"get_available_bt_port", (PyCFunction)apn_socket_getbtport, METH_NOARGS},

	//// asynchronous requests
	{"write_data", (PyCFunction)apn_socket_write, METH_VARARGS},
	{"read_some", (PyCFunction)apn_socket_readsome, METH_VARARGS},
	{"read_exact", (PyCFunction)apn_socket_readexact, METH_VARARGS},
	{"accept_client", (PyCFunction)apn_socket_accept, METH_VARARGS},
	{"connect_bt", (PyCFunction)apn_socket_connectbt, METH_VARARGS},
	{"connect_tcp", (PyCFunction)apn_socket_connecttcp, METH_VARARGS},
	{"config_bt", (PyCFunction)apn_socket_configbt, METH_VARARGS},

	//// asynchronous cancellation requests
	{"cancel_write", (PyCFunction)apn_socket_cancelwrite, METH_NOARGS},
	{"cancel_read", (PyCFunction)apn_socket_cancelread, METH_NOARGS},
	{"cancel_accept", (PyCFunction)apn_socket_cancelaccept, METH_NOARGS},
	{"cancel_config", (PyCFunction)apn_socket_cancelaccept, METH_NOARGS},
	{"cancel_connect", (PyCFunction)apn_socket_cancelconnect, METH_NOARGS},

	//// synchronous reads and writes
	{"sync_write", (PyCFunction)apn_socket_syncwrite, METH_VARARGS},
	{"sync_read", (PyCFunction)apn_socket_syncread, METH_VARARGS},

	{NULL, NULL} // sentinel
	};

static void apn_dealloc_socket(apn_socket_object *self)
	{
	AssertNonNull(self);
	delete self->iAoSocket;
	self->iAoSocket = NULL;

	/* This call releases memory allocated to an object using
	 * PyObject_New() or PyObject_NewVar(). This is normally called
	 * from the tp_dealloc handler specified in the object's type. The
	 * fields of the object should not be accessed after this call as
	 * the memory is no longer a valid Python object. */
	PyObject_Del(self);
	}

static PyObject *apn_socket_getattr(apn_socket_object *self,
									char *name)
	{
	AssertNonNull(self);
	return Py_FindMethod((PyMethodDef*)apn_socket_methods,
						 (PyObject*)self, name);
	}

// --------------------------------------------------------------------
// type...

/** A template for the socket type.
 */
const PyTypeObject apn_socket_typetmpl =
	{
	PyObject_HEAD_INIT(NULL)
	0,										   /*ob_size*/
	"pyaosocket.AoSocket",			  /*tp_name*/
	sizeof(apn_socket_object),					  /*tp_basicsize*/
	0,										   /*tp_itemsize*/
	/* methods */
	(destructor)apn_dealloc_socket,				  /*tp_dealloc*/
	0,										   /*tp_print*/
	(getattrfunc)apn_socket_getattr,				  /*tp_getattr*/
	0,										   /*tp_setattr*/
	0,										   /*tp_compare*/
	0,										   /*tp_repr*/
	0,										   /*tp_as_number*/
	0,										   /*tp_as_sequence*/
	0,										   /*tp_as_mapping*/
	0										  /*tp_hash*/
	};

TInt apn_socket_ConstructType()
	{
	return ConstructType(&apn_socket_typetmpl, "AoSocket");
	}

#define AoSocketType \
	((PyTypeObject*)SPyGetGlobalString("AoSocket"))

// --------------------------------------------------------------------
// module methods...

// Returns NULL if cannot allocate.
// The reference count of any returned object will be 1.
// The created socket object will be initialized,
// but the socket will not be open.
static apn_socket_object* NewSocketObject()
	{
	apn_socket_object* newSocket =
		// sets refcount to 1 if successful,
		// so decrefing should delete
		PyObject_New(apn_socket_object,
					 AoSocketType);
	if (newSocket == NULL)
		{
		// raise an exception with the reason set by PyObject_New
		return NULL;
		}

	TRAPD(error, newSocket->iAoSocket = CAoSocket::NewL());
	if (error)
		{
		Py_DECREF(newSocket);
		SPyErr_SetFromSymbianOSErr(error);
		return NULL;
		}

	return newSocket;
	}

// allocates a new socket object, or raises and exception
PyObject* apn_socket_new(PyObject* /*self*/,
						 PyObject* /*args*/)
	{
	return reinterpret_cast<PyObject*>(NewSocketObject());
	}
