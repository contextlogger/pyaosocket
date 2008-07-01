// -*- c++ -*-

//
// btengine.h
// 
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.  All rights reserved.
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

#ifndef BTENGINE_H
#define BTENGINE_H

#ifdef __DO_LOGGING__
#define BTENGINE_LOGGING 1
#else
#define BTENGINE_LOGGING 0
#endif

#include <btmanclient.h>
#include <btsdp.h>
#include <bt_sock.h>
#include <bttypes.h>
#include <es_sock.h>

#if BTENGINE_LOGGING
#include <flogger.h> // flogger.lib (Symbian 7.0-up)
#endif

#include "socketaos.h"



NONSHARABLE_CLASS(CPdisAdvertiser) : public CBase
{
public:
  static CPdisAdvertiser* NewL();
  ~CPdisAdvertiser();
private:
  void ConstructL();
  CPdisAdvertiser();
public:
  void AddRecordL(TInt aPort, TUint aServiceId, const TDesC& aServiceName);
  void DeleteRecordL();
  void MarkAvailableL(TBool aAvail);
  TBool IsRecordAdded() { return iRecordAdded; }
  TBool IsMarkedAvailable() { return iMarkedAvailable; }
private:
  void InitRecordL(TInt aPort, TUint aServiceId, const TDesC& aServiceName);
private:
  RSdp iSdp;
  RSdpDatabase iSdpDatabase;
  TSdpServRecordHandle iSdpServRecordHandle;
  TBool iRecordAdded;
  TBool iMarkedAvailable;
  TInt iRecordState;
#if BTENGINE_LOGGING
  RFileLogger* iLogger; // NULL if not available
#endif
};

class MPortDiscoveryObserver
{
public:
  // aPort only valid if anError is KErrNone
  virtual void PortDiscovered(TInt anError, TInt aPort) = 0;
};




/** an instance of this class can be used to discover
    the port number of an RFCOMM service;
    the result will be delivered via
    the MPortDiscoveryObserver interface
*/
NONSHARABLE_CLASS(CPortDiscoverer) : public CBase, public MSdpAgentNotifier,
			public MSdpAttributeValueVisitor
{
public:
  static CPortDiscoverer* NewL(MPortDiscoveryObserver& anObserver);
  ~CPortDiscoverer();
public:
  /** Discovers a serial port service with the given service ID
      at the given address.
      And no, this API does not support 128-bit service IDs. */
  void DiscoverL(const TBTDevAddr& anAddress, TUint aServiceId);
private:
  void ConstructL();
  CPortDiscoverer(MPortDiscoveryObserver& anObserver);
private:
  CSdpSearchPattern* iSdpSearchPattern;
  CSdpAttrIdMatchList* iSdpAttrIdMatchList;
  CSdpAgent* iSdpAgent;
  MPortDiscoveryObserver& iObserver;
  TUint iServiceId;
  TBool iIsWanted;
  TBool iAvailable;
  TBool iGotPort;
  TInt iPort;
  TInt iList;
#if BTENGINE_LOGGING
  RFileLogger* iLogger; // NULL if not available
#endif
private: // MSdpAgentNotifier
  virtual void AttributeRequestComplete(TSdpServRecordHandle aHandle, 
					TInt aError);
  virtual void AttributeRequestResult(TSdpServRecordHandle aHandle, 
				      TSdpAttributeID aAttrID, 
				      CSdpAttrValue* aAttrValue);
  virtual void NextRecordRequestComplete(TInt aError, 
					 TSdpServRecordHandle aHandle, 
					 TInt aTotalRecordsCount);
private: // MSdpAttributeValueVisitor
  virtual void VisitAttributeValueL(CSdpAttrValue &aValue, 
				    TSdpElementType aType);
  virtual void StartListL(CSdpAttrValueList &aList);
  virtual void EndListL();
};




NONSHARABLE_CLASS(BtEngine)
{
public:
  static TInt OpenSocket(RSocketServ& aSocketServ, RSocket& aSocket);
  static void StringToDevAddr(TBTDevAddr& aBtDevAddr, const TDesC& aString);
};




NONSHARABLE_CLASS(CBtConnecter) : public CActive
{
public:
  static CBtConnecter* NewL(MAoSockObserver& anObs,
			    RSocket& aSocket);
  ~CBtConnecter();
private:
  CBtConnecter(MAoSockObserver& anObs,
	       RSocket& aSocket);
  void ConstructL();
public:
  // makes an asynch. connect request;
  // the socket must already be open
  void ConnectL(const TBTDevAddr& aBtDevAddr, TInt aPort);
protected: // CActive
  virtual void DoCancel();
  virtual void RunL();
  virtual TInt RunError(TInt aError);
private:
  MAoSockObserver& iObserver;
  RSocket& iSocket;
  TBTSockAddr iBtSockAddr;
#if BTENGINE_LOGGING
  RFileLogger* iLogger; // NULL if not available
#endif
};





NONSHARABLE_CLASS(CBtAccepter) : public CActive
{
public:
  static CBtAccepter* NewL(MAoSockObserver& anObs,
			   RSocketServ& aSocketServ,
			   RSocket& aSocket);
  ~CBtAccepter();
private:
  CBtAccepter(MAoSockObserver& anObs,
	      RSocketServ& aSocketServ,
	      RSocket& aSocket);
  void ConstructL();
public:
  /** Returns an error value.
   */
  TInt GetAvailablePort(TInt& aPort);
  /** Does a Bind() and a Listen(). 
  */
  void ListenL(TInt aPort, TInt aQueueSize,
	       TUint aServiceId, const TDesC& aServiceName);
  /** Configures security, and adds an SDP record. */
  void ConfigureL();
  /** aBlankSocket must already have been initialized
      as a blank one; if request gets successfully completed,
      it will then be the pipe socket; the passed socket
      must persist until the accept task completes */
  void AcceptL(RSocket& aBlankSocket);
protected: // CActive
  virtual void DoCancel();
  virtual void RunL();
  virtual TInt RunError(TInt aError);
private:
  void CompleteImmediately();
  void UnsetSecurity();
  void AdvertiseL();
private:
  MAoSockObserver& iObserver;
  RSocketServ& iSocketServ;
  RSocket& iSocket;
#if BTENGINE_LOGGING
  RFileLogger* iLogger; // NULL if not available
#endif
  TInt iServerPort;
  TUint iServiceId;
  TBuf<32> iServiceName; // an arbitrarily chosen size
#ifndef __HAS_BT_SET_SECURITY__
  RBTMan iBtMan;
  RBTSecuritySettings iBtSecuritySettings;
  TBool iSecuritySet;
#endif
  CPdisAdvertiser* iAdvertiser;
  TInt iError;
private: // active object state, checked in RunL only
  enum TState {
    EConfiguring = 1,
    EAccepting
  };
  TState iState;
};

#endif // BTENGINE_H
