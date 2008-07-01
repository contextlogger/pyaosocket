// -*- symbian-c++ -*-

//
// resolution.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Implements synchronous DNS name resolution.
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
#include "resolution.h"

// resolves a name synchronously, returning an error code
TInt Resolve(RSocketServ& aSocketServ, const TDesC& aHostName,
			 TSockAddr& aResult)
	{
	// This is a special case that it looks like the resolver
	// cannot handle. It is also common enough for this code
	// to perhaps improve efficiency, as we do not require
	// a resolver session.
	_LIT(KLocalHostName, "localhost");
	if (aHostName == KLocalHostName)
		{
		const TUint32 KLocalIpAddr = INET_ADDR(127,0,0,1);
		TInetAddr localIpAddr;
		localIpAddr.SetAddress(KLocalIpAddr);
		aResult = localIpAddr; // copy
		return KErrNone;
		}

	RHostResolver resolver;
	TInt error = resolver.Open(aSocketServ, KAfInet, KProtocolInetUdp);
	if (error)
		{
		return error;
		}

	TNameEntry nameEntry;
	TRequestStatus resolvStatus;
	resolver.GetByName(aHostName, nameEntry, resolvStatus);
	User::WaitForRequest(resolvStatus);
	error = resolvStatus.Int();
	resolver.Close();
	if (error)
		{
		return error;
		}

	TNameRecord record = nameEntry();
	TSockAddr& addr = record.iAddr;
	aResult = addr; // copy
	return KErrNone;
	}


