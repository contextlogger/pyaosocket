// -*- symbian-c++ -*-

//
// socketaos.h
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
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

#ifndef __SOCKETAOS_H__
#define __SOCKETAOS_H__

#include <e32std.h>
#include <es_sock.h>
#include "settings.h"

// --------------------------------------------------------------------
// MAoSockObserver...

class MAoSockObserver
	{
public:
	virtual void DataWritten(TInt aError) = 0;
	virtual void DataRead(TInt aError, const TDesC8& aData) = 0;
	virtual void ClientAccepted(TInt aError) = 0;
	virtual void ClientConnected(TInt aError) = 0;
	virtual void SocketConfigured(TInt aError) = 0;
	};

// --------------------------------------------------------------------
// for use internally only...

class MGenericAoObserver
	{
public:
	virtual void AoEventOccurred(CActive* aOrig, TInt aError) = 0;
	};

// --------------------------------------------------------------------
// CDnsResolver (active object)...

NONSHARABLE_CLASS(CDnsResolver) : public CActive
	{
public:
	CDnsResolver(MGenericAoObserver& aObserver,
				 RSocketServ& aSocketServ);
	~CDnsResolver();
	/** aHostName need not persist after call */
	void Resolve(const TDesC& aHostName);
	/** may call this after successful completion, before the next request */
	void GetResult(TSockAddr& aResult) const;
protected:
	void DoCancel();
	void RunL();
private:
	MGenericAoObserver& iObserver;
	RSocketServ& iSocketServ;
	RHostResolver iHostResolver;
	DEF_SESSION_OPEN(iHostResolver);
	HBufC* iData;
	void ClearData();
	TNameEntry iNameEntry; // the result stored here
	};

// --------------------------------------------------------------------
// CSocketConnecter (active object)...

NONSHARABLE_CLASS(CSocketConnecter) : public CActive
	{
public:
	/** takes an open socket as a parameter;
		this object attempts to connect it,
		but will not close or reopen the provided
		socket */
	CSocketConnecter(MGenericAoObserver& aObserver,
					 RSocket& aSocket,
					 RSocketServ& aSocketServ);
	~CSocketConnecter();
	/** aServerAddress need not persist after call */
	void Connect(const TSockAddr& aServerAddress);
protected:
	void DoCancel();
	void RunL();
private:
	MGenericAoObserver& iObserver;
	RSocket& iSocket;
	RSocketServ& iSocketServ;
	TSockAddr iServerAddress;
	};

// --------------------------------------------------------------------
// CResolvingConnecter (active object)...

NONSHARABLE_CLASS(CResolvingConnecter) : public CActive,
	public MGenericAoObserver
	{
public:
	/** takes an open socket as a parameter;
		this object attempts to connect it,
		but will not close or reopen the provided
		socket */
	static CResolvingConnecter* NewL(MAoSockObserver& aObserver,
									 RSocket& aSocket,
									 RSocketServ& aSocketServ);
	~CResolvingConnecter();
	/** aServerAddress need not persist after call */
	void Connect(const TDesC& aHostName, TInt aPort);
protected:
	void DoCancel();
	void RunL();
private:
	CResolvingConnecter(MAoSockObserver& aObserver,
						RSocket& aSocket);
	void ConstructL(RSocketServ& aSocketServ);

	void AoEventOccurred(CActive* aOrig, TInt aError);
	MAoSockObserver& iObserver;
	RSocket& iSocket;
	CDnsResolver* iDnsResolver;
	CSocketConnecter* iSocketConnecter;
	TInt iPort;
	TInt iState; // 1 = resolving, 2 = connecting
	};

// --------------------------------------------------------------------
// CSocketAccepter (active object)...

NONSHARABLE_CLASS(CSocketAccepter) : public CActive
	{
public:
	/** aServerSocket must be open and listening already */
	CSocketAccepter(MAoSockObserver& aObserver,
					RSocket& aServerSocket);
	~CSocketAccepter();
	/** aBlankSocket must already have been initialized
		as a blank one; if request gets successfully completed,
		it will then be the pipe socket; the passed socket
		must persist until the accept task completes */
	void Accept(RSocket& aBlankSocket);
protected:
	void DoCancel();
	void RunL();
private:
	MAoSockObserver& iObserver;
	RSocket& iServerSocket;
	};

// --------------------------------------------------------------------
// CSocketWriter (active object)...
//
// Note that the Series 60 SDK 'sockets' example provides
// a useful example for an active object like this.

NONSHARABLE_CLASS(CSocketWriter) : public CActive
	{
public:
	CSocketWriter(MAoSockObserver& aObserver, RSocket& aSocket);
	~CSocketWriter();
	// the passed data need not persist after call
	void WriteDataL(const TDesC8& aData);
protected:
	void DoCancel();
	void RunL();
private:
	MAoSockObserver& iObserver;
	RSocket& iSocket;
	HBufC8* iData;
	void ClearData();
	};

// --------------------------------------------------------------------
// CSocketReader (active object)...
//
// Note that the Series 60 SDK 'sockets' example provides
// a useful example for an active object like this.

NONSHARABLE_CLASS(CSocketReader) : public CActive
	{
public:
	CSocketReader(MAoSockObserver& aObserver, RSocket& aSocket);
	~CSocketReader();
	void ReadSomeL(TInt aMaxSize);
	void ReadExactL(TInt aSize);
protected:
	void DoCancel();
	void RunL();
private:
	MAoSockObserver& iObserver;
	RSocket& iSocket;
	HBufC8* iData;
	TPtr8 iDataPtr;
	TSockXfrLength iDummyLen;
	void ClearData();
	};

#endif // __SOCKETAOS_H__
