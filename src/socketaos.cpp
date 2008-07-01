// -*- symbian-c++ -*-

//
// socketaos.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Some native active objects used for making and handling asynchronous
// socket requests.
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

#include <in_sock.h>
#include "panic.h"
#include "socketaos.h"

// -----------------------------------------------------------
// CDnsResolver...

CDnsResolver::CDnsResolver(MGenericAoObserver& aObserver,
						   RSocketServ& aSocketServ) :
	CActive(EPriorityStandard),
	iObserver(aObserver),
	iSocketServ(aSocketServ)
	{
	CActiveScheduler::Add(this);
	}

CDnsResolver::~CDnsResolver()
	{
	// the CActive destructor will remove us from the active
	// scheduler list, right after this destructor has finished
	// executing
	Cancel();

	if (IS_SUBSESSION_OPEN(iHostResolver))
		{
		iHostResolver.Close();
		SET_SESSION_CLOSED(iHostResolver);
		}

	ClearData();
	}

void CDnsResolver::Resolve(const TDesC& aHostName)
	{
	if (IsActive())
		{
		AssertFail();
		return;
		}

	_LIT(KLocalHostName, "localhost");
	if (aHostName == KLocalHostName)
		{
		const TUint32 KLocalIpAddr = INET_ADDR(127,0,0,1);
		TInetAddr localIpAddr;
		localIpAddr.SetAddress(KLocalIpAddr);
		iNameEntry().iAddr = localIpAddr; // copy

		iStatus = KRequestPending;
		SetActive();
		TRequestStatus* status = &iStatus;
		User::RequestComplete(status, KErrNone);
		return;
		}

	// ensure that we have a resolver session we can use
	if (!IS_SUBSESSION_OPEN(iHostResolver))
		{
		TInt error = iHostResolver.Open(
			iSocketServ, KAfInet, KProtocolInetUdp);
		if (error != KErrNone && error != KErrAlreadyExists)
			{
			iStatus = KRequestPending;
			SetActive();
			TRequestStatus* status = &iStatus;
			User::RequestComplete(status, error);
			return;
			}
		}
	SET_SESSION_OPEN(iHostResolver);

	// store the hostname for the duration of the request,
	// so that the caller does not have to worry about that
	ClearData();
	iData = HBufC::New(aHostName.Length());
	if (!iData)
		{
		iStatus = KRequestPending;
		SetActive();
		TRequestStatus* status = &iStatus;
		User::RequestComplete(status, KErrNoMemory);
		return;
		}
	TPtr ptr(iData->Des());
	ptr.Copy(aHostName);

	iHostResolver.GetByName(*iData, iNameEntry, iStatus);
	SetActive();
	}

void CDnsResolver::DoCancel()
	{
	if (IS_SUBSESSION_OPEN(iHostResolver))
		{
		iHostResolver.Cancel();
		}
	}

void CDnsResolver::RunL()
	{
	iObserver.AoEventOccurred(this, iStatus.Int());
	// note that the callback might do anything, such
	// as destroying this object, so it is imperative
	// that we do not do anything here -- certainly
	// not anything involving the property of this object
	}

void CDnsResolver::ClearData()
	{
	if (iData)
		{
		delete iData;
		iData = NULL;
		}
	}

void CDnsResolver::GetResult(TSockAddr& aResult) const
	{
	TNameRecord record = iNameEntry();
	TSockAddr& addr = record.iAddr;
	aResult = addr; // copy
	}

// -----------------------------------------------------------
// CSocketConnecter...

CSocketConnecter::CSocketConnecter(MGenericAoObserver& aObserver,
								   RSocket& aSocket,
								   RSocketServ& aSocketServ) :
	CActive(EPriorityStandard),
	iObserver(aObserver),
	iSocket(aSocket),
	iSocketServ(aSocketServ)
	{
	CActiveScheduler::Add(this);
	}

CSocketConnecter::~CSocketConnecter()
	{
	// the CActive destructor will remove us from the active
	// scheduler list, right after this destructor has finished
	// executing
	Cancel();
	}

void CSocketConnecter::Connect(const TSockAddr& aServerAddress)
	{
	if (IsActive())
		{
		AssertFail();
		return;
		}

	iServerAddress = aServerAddress;
	iSocket.Connect(iServerAddress, iStatus);
	SetActive();
	}

void CSocketConnecter::DoCancel()
	{
	iSocket.CancelConnect();
	}

void CSocketConnecter::RunL()
	{
	TInt status = iStatus.Int();
	iObserver.AoEventOccurred(this, status);
	// note that the callback might do anything, such
	// as destroying this object, so it is imperative
	// that we do not do anything here -- certainly
	// not anything involving the property of this object
	}

// -----------------------------------------------------------
// CResolvingConnecter...

CResolvingConnecter* CResolvingConnecter::NewL(MAoSockObserver& aObserver,
											   RSocket& aSocket,
											   RSocketServ& aSocketServ)
	{
	CResolvingConnecter* object = new (ELeave)
		CResolvingConnecter(aObserver, aSocket);
	CleanupStack::PushL(object);
	object->ConstructL(aSocketServ);
	CleanupStack::Pop();
	return object;
	}

CResolvingConnecter::CResolvingConnecter(MAoSockObserver& aObserver,
										 RSocket& aSocket) :
	CActive(EPriorityStandard),
	iObserver(aObserver),
	iSocket(aSocket)
	{
	CActiveScheduler::Add(this);
	}

void CResolvingConnecter::ConstructL(RSocketServ& aSocketServ)
	{
	iDnsResolver = new (ELeave) CDnsResolver(*this, aSocketServ);
	iSocketConnecter = new (ELeave)
		CSocketConnecter(*this, iSocket, aSocketServ);
	}

CResolvingConnecter::~CResolvingConnecter()
	{
	// the CActive destructor will remove us from the active
	// scheduler list, right after this destructor has finished
	// executing
	Cancel();

	delete iDnsResolver;
	delete iSocketConnecter;
	}

void CResolvingConnecter::Connect(const TDesC& aHostName, TInt aPort)
	{
	if (IsActive())
		{
		AssertFail();
		return;
		}

	iPort = aPort;

	iDnsResolver->Resolve(aHostName);
	iState = 1;

	iStatus = KRequestPending;
	SetActive();
	}

void CResolvingConnecter::DoCancel()
	{
	iDnsResolver->Cancel();
	iSocketConnecter->Cancel();

	// note that it is important that we do not signal
	// the same request twice
	if (iStatus == KRequestPending)
		{
		TRequestStatus* status = &iStatus;
		User::RequestComplete(status, KErrCancel);
		}
	}

void CResolvingConnecter::RunL()
	{
	iObserver.ClientConnected(iStatus.Int());
	// note that the callback might do anything, such
	// as destroying this object, so it is imperative
	// that we do not do anything here -- certainly
	// not anything involving the property of this object
	}

void CResolvingConnecter::AoEventOccurred(CActive* aOrig, TInt aError)
	{
	if (!IsActive())
		{
		AssertFail();
		return;
		}
	if ((iState == 1) && (aOrig == iDnsResolver))
		{
		if (aError == KErrNone)
			{
			TSockAddr addr;
			iDnsResolver->GetResult(addr);
			addr.SetPort(iPort);
			iSocketConnecter->Connect(addr);
			iState = 2;

			return;
			}
		}
	else if ((iState == 2) && (aOrig == iSocketConnecter))
		{
		iState = 3;
		}
	else
		{
		AssertFail();
		}
	TRequestStatus* status = &iStatus;
	User::RequestComplete(status, aError);
	}

// -----------------------------------------------------------
// CSocketAccepter...

CSocketAccepter::CSocketAccepter(MAoSockObserver& aObserver,
								 RSocket& aServerSocket) :
	CActive(EPriorityStandard),
	iObserver(aObserver),
	iServerSocket(aServerSocket)
	{
	CActiveScheduler::Add(this);
	}

CSocketAccepter::~CSocketAccepter()
	{
	// the CActive destructor will remove us from the active
	// scheduler list, right after this destructor has finished
	// executing
	Cancel();
	}

void CSocketAccepter::Accept(RSocket& aBlankSocket)
	{
	if (IsActive())
		{
		AssertFail();
		return;
		}
	iServerSocket.Accept(aBlankSocket, iStatus);
	SetActive();
	}

void CSocketAccepter::DoCancel()
	{
	iServerSocket.CancelAccept();
	}

void CSocketAccepter::RunL()
	{
	iObserver.ClientAccepted(iStatus.Int());
	// note that the callback might do anything, such
	// as destroying this object, so it is imperative
	// that we do not do anything here -- certainly
	// not anything involving the property of this object
	}

// -----------------------------------------------------------
// CSocketWriter...

CSocketWriter::CSocketWriter(MAoSockObserver& aObserver,
							 RSocket& aSocket) :
	CActive(EPriorityStandard),
	iObserver(aObserver),
	iSocket(aSocket)
	{
	CActiveScheduler::Add(this);
	}

CSocketWriter::~CSocketWriter()
	{
	Cancel();
	ClearData();
	}

void CSocketWriter::DoCancel()
	{
	iSocket.CancelWrite();
	}

void CSocketWriter::RunL()
	{
	iObserver.DataWritten(iStatus.Int());
	// note that the callback might do anything, such
	// as destroying this object, so it is imperative
	// that we do not do anything here
	}

void CSocketWriter::WriteDataL(const TDesC8& aData)
	{
	if (IsActive())
		{
		AssertFail();
		return;
		}
	ClearData();
	iData = HBufC8::NewL(aData.Length());
	TPtr8 ptr(iData->Des());
	ptr.Copy(aData);
	iSocket.Write(*iData, iStatus);
	SetActive();
	}

void CSocketWriter::ClearData()
	{
	if (iData)
		{
		delete iData;
		iData = NULL;
		}
	}

// -----------------------------------------------------------
// CSocketReader...

CSocketReader::CSocketReader(MAoSockObserver& aObserver,
							 RSocket& aSocket) :
	CActive(EPriorityStandard),
	iObserver(aObserver),
	iSocket(aSocket),
	// must initialize as has no default constructor
	iDataPtr(NULL, 0, 0)
	{
	CActiveScheduler::Add(this);
	}

CSocketReader::~CSocketReader()
	{
	Cancel();
	ClearData();
	}

void CSocketReader::DoCancel()
	{
	iSocket.CancelRecv();
	}

void CSocketReader::RunL()
	{
	iObserver.DataRead(iStatus.Int(), iDataPtr);
	//iObserver.DataRead(iStatus.Int(), *iData);

	// note that the callback might do anything, such
	// as destroying this object, so it is imperative
	// that we do not do anything here
	}

void CSocketReader::ReadSomeL(TInt aMaxSize)
	{
	if (IsActive())
		{
		AssertFail();
		return;
		}
	ClearData();

	// may have MaxLength() larger than requested
	iData = HBufC8::NewL(aMaxSize);
	iDataPtr.Set(const_cast<TUint8*>(iData->Ptr()), 0, aMaxSize);
	iDataPtr.FillZ(); // not necessary
	iSocket.RecvOneOrMore(iDataPtr, 0, iStatus, iDummyLen);
	//iSocket.RecvOneOrMore(iData->Des(), 0, iStatus, iDummyLen);

	SetActive();
	}

void CSocketReader::ReadExactL(TInt aSize)
	{
	if (IsActive())
		{
		AssertFail();
		return;
		}
	ClearData();

	// may have MaxLength() larger than requested
	iData = HBufC8::NewL(aSize);
	iDataPtr.Set(const_cast<TUint8*>(iData->Ptr()), 0, aSize);
	iDataPtr.FillZ(); // not necessary
	// note that we want to use Recv() here instead of Read(),
	// as in the event of cancellation, we use CancelRecv();
	// do not know whether a Read() can be cancelled with that
	iSocket.Recv(iDataPtr, 0, iStatus);

	SetActive();
	}

void CSocketReader::ClearData()
	{
	if (iData)
		{
		iDataPtr.Set(NULL, 0, 0);
		delete iData;
		iData = NULL;
		}
	}
