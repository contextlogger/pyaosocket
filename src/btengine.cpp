//
// btengine.cpp
// 
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.  All rights reserved.
// 
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Contains most of the BT functionality required by this native library.
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

#include <e32std.h>

#include "btengine.h"
#include "panic.h"

// -------------------------------------------------------------------
// service advertising...

// the standard service class IDs are listed at
// https://www.bluetooth.org/foundry/assignnumb/document/service_discovery;
// like Nokia, we are using the RFCOMM ID as the service class ID
static const TInt KPdisServiceClassId = KSerialPortUUID;

// we must advertise our service or the client will not know
// which port to connect to

CPdisAdvertiser* CPdisAdvertiser::NewL()
{
  CPdisAdvertiser* self = new (ELeave) CPdisAdvertiser;
  CleanupStack::PushL(self);
  self->ConstructL();
  CleanupStack::Pop();
  return self;
}

CPdisAdvertiser::~CPdisAdvertiser()
{
  if (iRecordAdded) {
    TRAPD(error, DeleteRecordL()); 
  }
  if (iSdpDatabase.SubSessionHandle() != 0)
    iSdpDatabase.Close();
  if (iSdp.Handle() != 0)
    iSdp.Close();

#if BTENGINE_LOGGING
  if (iLogger) {
    iLogger->CloseLog();
    iLogger->Close();
    delete iLogger;
  }
#endif
}

void CPdisAdvertiser::ConstructL() 
{
#if BTENGINE_LOGGING
  iLogger = new (ELeave) RFileLogger;
  if (iLogger->Connect() != KErrNone) {
    delete iLogger;
    iLogger = NULL;
  } else {
    _LIT(KLoggerDir, "aosocket");
    _LIT(KLoggerFile, "advertiser.txt");
    iLogger->CreateLog(KLoggerDir, KLoggerFile, EFileLoggingModeOverwrite);
    iLogger->Write(_L("logging started"));
  }
#endif

  User::LeaveIfError(iSdp.Connect());
  User::LeaveIfError(iSdpDatabase.Open(iSdp));
}

CPdisAdvertiser::CPdisAdvertiser() 
{
  // nothing
}

void CPdisAdvertiser::AddRecordL(TInt aPort, TUint aServiceId,
				 const TDesC& aServiceName)
{
  if (iRecordAdded) {
    // already advertising, so stop that first --
    // we only manage one record at a time
    DeleteRecordL();
  }

#if BTENGINE_LOGGING
  if (iLogger) iLogger->Write(_L("creating service record"));
#endif
  iSdpDatabase.CreateServiceRecordL(KPdisServiceClassId, 
				    iSdpServRecordHandle);
  iRecordAdded = ETrue;

  // this call fills in iSdpServRecordHandle
#if BTENGINE_LOGGING
  if (iLogger) iLogger->Write(_L("initializing record"));
#endif
  TRAPD(error, InitRecordL(aPort, aServiceId, aServiceName));
  if (error) {
    // we created a record but did not manage to fill in its fields,
    // so try to delete it before leaving
    DeleteRecordL();
    User::Leave(error);
  } else {
#if BTENGINE_LOGGING
    if (iLogger) iLogger->Write(_L("record initialized okay"));
#endif
  }
}

void CPdisAdvertiser::InitRecordL(TInt aPort, TUint aServiceId,
				  const TDesC& aServiceName) 
{
  // channel
  TBuf8<1> channel;
  channel.Append((TChar)aPort);

  // protocol
#if BTENGINE_LOGGING
  if (iLogger) iLogger->Write(_L("creating descriptor list"));
#endif
  CSdpAttrValueDES* protocolDescriptorList = CSdpAttrValueDES::NewDESL(NULL);
  CleanupStack::PushL(protocolDescriptorList);
  protocolDescriptorList
    ->StartListL()
    ->BuildDESL()
    ->StartListL()
    ->BuildUUIDL(KL2CAP)
    ->EndListL()
    ->BuildDESL()
    ->StartListL()
    ->BuildUUIDL(KRFCOMM)
    ->BuildUintL(channel)
    ->EndListL()
    ->EndListL();
#if BTENGINE_LOGGING
  if (iLogger) iLogger->Write(_L("setting protocol"));
#endif
  iSdpDatabase.UpdateAttributeL(iSdpServRecordHandle,
				KSdpAttrIdProtocolDescriptorList, 
				*protocolDescriptorList);
  CleanupStack::PopAndDestroy(); // protocolDescriptorList

  // service ID (not sure what the significance of this is,
  // probably not essential)
#if BTENGINE_LOGGING
  if (iLogger) iLogger->Write(_L("setting service id"));
#endif
  TUUID serviceIdUuid(aServiceId);
  CSdpAttrValueUUID* uuidAttr = CSdpAttrValueUUID::NewUUIDL(serviceIdUuid);
  CleanupStack::PushL(uuidAttr);
  iSdpDatabase.UpdateAttributeL(iSdpServRecordHandle,
				KSdpAttrIdServiceID,
				*uuidAttr);
  CleanupStack::PopAndDestroy(); // uuidAttr

  // service name
  //_LIT(KServiceName, "PDIS");
#if BTENGINE_LOGGING
  if (iLogger) iLogger->Write(_L("setting service name"));
#endif
  iSdpDatabase.UpdateAttributeL(iSdpServRecordHandle,
				KSdpAttrIdBasePrimaryLanguage +
				KSdpAttrIdOffsetServiceName,
				aServiceName);

#if 0
  // service description
  _LIT(KServiceDescription, "PDIS repository service");
#if BTENGINE_LOGGING
  if (iLogger) iLogger->Write(_L("setting service desc"));
#endif
  iSdpDatabase.UpdateAttributeL(iSdpServRecordHandle,
				KSdpAttrIdBasePrimaryLanguage +
				KSdpAttrIdOffsetServiceDescription,
				KServiceDescription);
#endif
  
#if BTENGINE_LOGGING
  if (iLogger) iLogger->Write(_L("marking"));
#endif
  MarkAvailableL(ETrue);
}

void CPdisAdvertiser::DeleteRecordL() 
{
  if (iRecordAdded) {
    iSdpDatabase.DeleteRecordL(iSdpServRecordHandle);
    iRecordAdded = EFalse;
  }
}

void CPdisAdvertiser::MarkAvailableL(TBool aAvail) 
{
  if (!iRecordAdded)
    User::Invariant();

  // black magic (these values were used in the EMCC example)
  TUint value = aAvail ? 0xff : 0x00;
  iSdpDatabase.UpdateAttributeL(iSdpServRecordHandle,
				KSdpAttrIdServiceAvailability, value);
  // presumably we need to change the state for the change to get noticed
  iSdpDatabase.UpdateAttributeL(iSdpServRecordHandle,
				KSdpAttrIdServiceRecordState,
				++iRecordState);

  iMarkedAvailable = aAvail;
}

// -------------------------------------------------------------------
// port discovery...

// The API docs on this topic are not very detailed, but see
// * Nokia BT document pp.32
// * header files
// * SDK BTDiscovery example
// * EMCC bluetoothservicesearcher example

CPortDiscoverer* CPortDiscoverer::NewL(MPortDiscoveryObserver& anObserver)
{
  CPortDiscoverer* self = new (ELeave) CPortDiscoverer(anObserver);
  CleanupStack::PushL(self);
  self->ConstructL();
  CleanupStack::Pop();
  return self;
}

CPortDiscoverer::CPortDiscoverer(MPortDiscoveryObserver& anObserver) :
  iObserver(anObserver)
{
}

void CPortDiscoverer::ConstructL()
{
#if BTENGINE_LOGGING
  iLogger = new (ELeave) RFileLogger;
  if (iLogger->Connect() != KErrNone) {
    delete iLogger;
    iLogger = NULL;
  } else {
    _LIT(KLoggerDir, "aosocket");
    _LIT(KLoggerFile, "portdiscoverer.txt");
    iLogger->CreateLog(KLoggerDir, KLoggerFile, EFileLoggingModeOverwrite);
    iLogger->Write(_L("logging started"));
  }
#endif

  iSdpSearchPattern = CSdpSearchPattern::NewL();
  iSdpSearchPattern->AddL(KPdisServiceClassId);

  iSdpAttrIdMatchList = CSdpAttrIdMatchList::NewL();
#if 0
  // we want the service ID of the service;
  // FILTERING APPEARS TO BE BROKEN -- WE DO NOT GET THE
  // SERVICE ID BY ASKING FOR THIS ATTRIBUTE, SO WE MUST
  // ASK FOR ALL ATTRIBUTES INSTEAD
  iSdpAttrIdMatchList->AddL(KSdpAttrIdServiceID);
  // we want to know whether the service is available
  iSdpAttrIdMatchList->AddL(KSdpAttrIdServiceAvailability);
  // and we want the port number of the service
  iSdpAttrIdMatchList->AddL(KSdpAttrIdProtocolDescriptorList);
#else
  // this gets every attribute
  iSdpAttrIdMatchList->AddL(TAttrRange(0, 0xffff));
#endif
}

/** asynchronous --
    any outstanding request will be cancelled
 */
void CPortDiscoverer::DiscoverL(const TBTDevAddr& anAddress,
				TUint aServiceId)
{
  if (iSdpAgent) {
    // this should definitely cancel any outstanding request
    delete iSdpAgent;
    iSdpAgent = NULL;
  }

  iServiceId = aServiceId;

  // passing an MSdpAgentNotifier
  iSdpAgent = CSdpAgent::NewL(*this, anAddress);
  iSdpAgent->SetRecordFilterL(*iSdpSearchPattern);
  iSdpAgent->NextRecordRequestL();
}

CPortDiscoverer::~CPortDiscoverer()
{
  delete iSdpAgent;
  delete iSdpSearchPattern;

#if BTENGINE_LOGGING
  if (iLogger) {
    iLogger->CloseLog();
    iLogger->Close();
    delete iLogger;
  }
#endif
}

// a NextRecordRequestL() call has completed
void CPortDiscoverer::NextRecordRequestComplete(TInt aError, 
						TSdpServRecordHandle aHandle, 
						TInt aTotalRecordsCount)
{
  // we must ensure that there both was no error,
  // and that some record(s) were found
  if (aError == KErrNone && aTotalRecordsCount > 0) {
    // we want these to become true, and then we're done
    iIsWanted = EFalse;
    iAvailable = EFalse;
    iGotPort = EFalse;

    // there are many variants of AttributeRequestL, but
    // this one is nice and flexible -- we just define
    // what we want when we define the list
    TRAP(aError, iSdpAgent->AttributeRequestL(aHandle, *iSdpAttrIdMatchList));
    if (aError)
      // could not request next record, so fail
      iObserver.PortDiscovered(aError, 0);
  } else {
    if (aError == KErrNone)
      aError = KErrNotFound;
    // could not get any more records, so fail
    iObserver.PortDiscovered(aError, 0);
  }
}

// called in response to AttributeRequestL();
// used to pass the value of a single attribute;
// note that may be called multiple times per request
void CPortDiscoverer::AttributeRequestResult(TSdpServRecordHandle /*aHandle*/, 
					     TSdpAttributeID aAttrID, 
					     CSdpAttrValue* aAttrValue)
{
  if (aAttrID == KSdpAttrIdServiceID) {
    if (aAttrValue->Type() == ETypeUUID) {
      TUUID wanted(iServiceId);
      iIsWanted = (aAttrValue->UUID() == wanted);
    }
  } else if (aAttrID == KSdpAttrIdServiceAvailability) {
    if (aAttrValue->Type() == ETypeUint) {
      iAvailable = static_cast<TBool>(aAttrValue->Uint());
    }
  } else if (aAttrID == KSdpAttrIdProtocolDescriptorList) {
    // we must do some parsing to get the port
    iList = 0;
    TRAPD(error, aAttrValue->AcceptVisitorL(*this));
    // we ignore any error -- it will later become clear
    // that we did not get the port number, and an error
    // will be triggered unless we can get it from some
    // other record
  } else {
#if BTENGINE_LOGGING
      if (iLogger) {
	iLogger->WriteFormat(_L("ignoring attr value of type %d"), 
			     aAttrValue->Type());
      }
#else
    // did not ask for it
    User::Invariant();
#endif
  }
  
  // apparently we must free aAttrValue, although the API documentation
  // does not say so; no wonder people are having trouble with the
  // Symbian Bluetooth API
  delete aAttrValue;
}

// called in response to AttributeRequestL(),
// when no more attributes in record
void CPortDiscoverer::AttributeRequestComplete
(TSdpServRecordHandle /*aHandle*/, TInt aError)
{
#if BTENGINE_LOGGING
  if (iLogger) {
    iLogger->WriteFormat(_L("idmatch, avail, gotport: %d %d %d %d"), 
			 iIsWanted, iAvailable, iGotPort, iPort);
  }
#endif

  if (iIsWanted && iAvailable && iGotPort) {
    iObserver.PortDiscovered(KErrNone, iPort);
  } else if (aError == KErrNone) {
    TRAPD(error, iSdpAgent->NextRecordRequestL());
    if (error)
      iObserver.PortDiscovered(error, 0);
  } else {
    iObserver.PortDiscovered(aError, 0);
  }
}

void CPortDiscoverer::VisitAttributeValueL(CSdpAttrValue &aValue, 
					   TSdpElementType aType)
{
  // we are perhaps relying too much on the way we know
  // we packed the protocol description, but for now anyway...
  if (iList == 3 && aType == ETypeUint) {
    iPort = aValue.Uint();
    iGotPort = ETrue;
  }
}

// one attribute may contain multiple lists,
// and this method gets called as visiting each
// such list begins
void CPortDiscoverer::StartListL(CSdpAttrValueList& /*aList*/)
{
  iList++;
}

void CPortDiscoverer::EndListL()
{
  // nothing
}

// -------------------------------------------------------------------
// connecter...

CBtConnecter* CBtConnecter::NewL(MAoSockObserver& anObs,
			   RSocket& aSocket)
{
  CBtConnecter* self = new (ELeave) CBtConnecter(anObs, aSocket);
  CleanupStack::PushL(self);
  self->ConstructL();
  CleanupStack::Pop();
  return self;
}

CBtConnecter::CBtConnecter(MAoSockObserver& anObs,
			   RSocket& aSocket) :
  CActive(EPriorityStandard),
  iObserver(anObs),
  iSocket(aSocket)
{
  CActiveScheduler::Add(this);
}

void CBtConnecter::ConstructL()
{
#if BTENGINE_LOGGING
  iLogger = new (ELeave) RFileLogger;
  if (iLogger->Connect() != KErrNone) {
    delete iLogger;
    iLogger = NULL;
  } else {
    _LIT(KLoggerDir, "aosocket");
    _LIT(KLoggerFile, "btconnecter.txt");
    iLogger->CreateLog(KLoggerDir, KLoggerFile, EFileLoggingModeOverwrite);
    iLogger->Write(_L("logging started"));
  }
#endif
}

CBtConnecter::~CBtConnecter()
{
  Cancel();

#if BTENGINE_LOGGING
  if (iLogger) {
    iLogger->CloseLog();
    iLogger->Close();
    delete iLogger;
  }
#endif
}

void CBtConnecter::ConnectL(const TBTDevAddr& aBtDevAddr, TInt aPort)
{
  if (IsActive())
    {
      AssertFail();
      return;
    }
  
  iBtSockAddr.SetBTAddr(aBtDevAddr);
  iBtSockAddr.SetPort(aPort);

  iSocket.Connect(iBtSockAddr, iStatus);

  iStatus = KRequestPending;
  SetActive();
}

void CBtConnecter::DoCancel()
{
  iSocket.CancelConnect();
}

void CBtConnecter::RunL()
{
  iObserver.ClientConnected(iStatus.Int());
  // note that the callback might do anything, such
  // as destroying this object, so it is imperative
  // that we do not do anything here -- certainly
  // not anything involving the property of this object
}

#if BTENGINE_LOGGING
TInt CBtConnecter::RunError(TInt aError)
#else
TInt CBtConnecter::RunError(TInt)
#endif
{
#if BTENGINE_LOGGING
  if (iLogger) {
    iLogger->WriteFormat(_L("leave %d in RunL"), aError);
  }
#endif

  // the RunL() was supposed to be coded so as not to leave
  User::Invariant();

  // to indicate that we handled the error,
  // and the active scheduler does not need to
  return KErrNone;
}

// -------------------------------------------------------------------
// utilities...

TInt BtEngine::OpenSocket(RSocketServ& aSocketServ, RSocket& aSocket)
{
  _LIT(KRfcommTransportName, "RFCOMM");
  return aSocket.Open(aSocketServ, KRfcommTransportName);
}

void BtEngine::StringToDevAddr(TBTDevAddr& aBtDevAddr, const TDesC& aString)
{
  if (aString.Length() != (6*2+5))
    AoSocketPanic(EPanicArgumentError);

  TLex lex;
  TUint val;
  // xx:xx:xx:xx:xx:xx
  // 01 34 67 90 23 56
  lex.Assign(aString.Mid(0,2));
  lex.Val(val, EHex);
  aBtDevAddr[0] = static_cast<TUint8>(val);
  lex.Assign(aString.Mid(3,2));
  lex.Val(val, EHex);
  aBtDevAddr[1] = static_cast<TUint8>(val);
  lex.Assign(aString.Mid(6,2));
  lex.Val(val, EHex);
  aBtDevAddr[2] = static_cast<TUint8>(val);
  lex.Assign(aString.Mid(9,2));
  lex.Val(val, EHex);
  aBtDevAddr[3] = static_cast<TUint8>(val);
  lex.Assign(aString.Mid(12,2));
  lex.Val(val, EHex);
  aBtDevAddr[4] = static_cast<TUint8>(val);
  lex.Assign(aString.Mid(15,2));
  lex.Val(val, EHex);
  aBtDevAddr[5] = static_cast<TUint8>(val);
}

// -------------------------------------------------------------------
// accepter...

/*

Security settings and advertising
---------------------------------

This object configures security automatically so that the configuring
is done when ConfigureL() is called, and those settings persist until
the object is deleted.

This object does service advertising automatically. The advertisement
record is added when ConfigureL() is called, and the service is marked
as available. The record will persist up until the point when the
CBtAccepter instance is deleted.

*/

CBtAccepter* CBtAccepter::NewL(MAoSockObserver& anObs,
			       RSocketServ& aSocketServ,
			       RSocket& aSocket)
{
  CBtAccepter* self = new (ELeave) CBtAccepter(anObs, aSocketServ, aSocket);
  CleanupStack::PushL(self);
  self->ConstructL();
  CleanupStack::Pop();
  return self;
}

CBtAccepter::CBtAccepter(MAoSockObserver& anObs,
			 RSocketServ& aSocketServ,
			 RSocket& aSocket) :
  CActive(EPriorityStandard),
  iObserver(anObs),
  iSocketServ(aSocketServ),
  iSocket(aSocket)
{
  CActiveScheduler::Add(this);
}

void CBtAccepter::ConstructL()
{
#if BTENGINE_LOGGING
  iLogger = new (ELeave) RFileLogger;
  if (iLogger->Connect() != KErrNone) {
    delete iLogger;
    iLogger = NULL;
  } else {
    _LIT(KLoggerDir, "aosocket");
    _LIT(KLoggerFile, "btaccepter.txt");
    iLogger->CreateLog(KLoggerDir, KLoggerFile, EFileLoggingModeOverwrite);
    iLogger->Write(_L("logging started"));
  }
#endif

#ifndef __HAS_BT_SET_SECURITY__
  User::LeaveIfError(iBtMan.Connect());
  User::LeaveIfError(iBtSecuritySettings.Open(iBtMan));
#endif
}

CBtAccepter::~CBtAccepter()
{
  Cancel();

  delete iAdvertiser;
  UnsetSecurity();

#ifndef __HAS_BT_SET_SECURITY__
  if (iBtSecuritySettings.SubSessionHandle() != 0)
    iBtSecuritySettings.Close();
  if (iBtMan.Handle() != 0)
    iBtMan.Close();
#endif

#if BTENGINE_LOGGING
  if (iLogger) {
    iLogger->CloseLog();
    iLogger->Close();
    delete iLogger;
  }
#endif
}

/* No harm in calling when security has not been set. */
void CBtAccepter::UnsetSecurity()
{
#ifndef __HAS_BT_SET_SECURITY__
  if (iSecuritySet) {
    TUid uid;
    uid.iUid = iServiceId;
    // only the UID is required here
    TBTServiceSecurity serviceSecurity(uid, 0, 0);
    TRequestStatus status;
    iBtSecuritySettings.UnregisterService(serviceSecurity, status);
    // now we have to block
    User::WaitForRequest(status);

    iSecuritySet = EFalse;
  }
#endif
}

/* Note the ordering:
 1. GetAvailablePort (if desired)
 2. ListenL
 3. ConfigureL
 4. AcceptL
*/

TInt CBtAccepter::GetAvailablePort(TInt& aPort)
{
  // in case of Symbian 8, it would be better to use
  // KRFCOMMPassiveAutoBind instead, but this still works
  return iSocket.GetOpt(KRFCOMMGetAvailableServerChannel,
			KSolBtRFCOMM, aPort);
}

#ifdef __HAS_BT_SET_SECURITY__
static void AddrSetSecurity(TRfcommSockAddr& aAddr, TUint aServiceId)
{
  TBTServiceSecurity sec;
  TUid uid;
  uid.iUid = aServiceId;
  sec.SetUid(uid);
  sec.SetAuthentication(ETrue);
  sec.SetAuthorisation(ETrue);
  sec.SetEncryption(ETrue);
  sec.SetDenied(EFalse); // access to the service is allowed
  aAddr.SetSecurity(sec);
}
#endif

/** Does a Bind() and a Listen().
    This is a synchronous call.
 */
void CBtAccepter::ListenL(TInt aPort, TInt aQueueSize,
			  TUint aServiceId, const TDesC& aServiceName)
{
  iServerPort = aPort;
  iServiceId = aServiceId;
  iServiceName.Copy(aServiceName);

#ifdef __HAS_BT_SET_SECURITY__
  TRfcommSockAddr addr;
  // this could also be done for outgoing sockets, apparently...
  // perhaps we should be more insistent on security
  AddrSetSecurity(addr, iServiceId);
#else
  TBTSockAddr addr;
#endif

  addr.SetPort(iServerPort);

#if BTENGINE_LOGGING
  if (iLogger) {
    iLogger->WriteFormat(_L("doing a Bind to port %d"), iServerPort);
  }
#endif
  User::LeaveIfError(iSocket.Bind(addr));

#if BTENGINE_LOGGING
  if (iLogger) {
    iLogger->Write(_L("doing a Listen"));
  }
#endif
  User::LeaveIfError(iSocket.Listen(aQueueSize));
}

void CBtAccepter::CompleteImmediately()
{
  iStatus = KRequestPending;
  SetActive();
  TRequestStatus* status = &iStatus;
  User::RequestComplete(status, KErrNone);
}

void CBtAccepter::ConfigureL()
{
  if (IsActive())
    {
      AssertFail();
      return;
    }
  
#ifdef __HAS_BT_SET_SECURITY__
  // we will instead set security when doing Bind()
  AdvertiseL();
  CompleteImmediately();
#else
  if (iSecuritySet) {
    // if security has already been configured, we
    // will merely pretend to do something
    CompleteImmediately();
  } else {
    // make a request for setting security;
    // we will not start advertising before we have
    // successfully set security
    TUid uid;
    uid.iUid = iServiceId;
    TBTServiceSecurity serviceSecurity(uid, KSolBtRFCOMM, iServerPort);
    serviceSecurity.SetAuthentication(ETrue);
    serviceSecurity.SetEncryption(ETrue);
    serviceSecurity.SetAuthorisation(ETrue);
    iBtSecuritySettings.RegisterService(serviceSecurity, iStatus);

    SetActive();
  }
#endif

  iState = EConfiguring;
}

/** Does an Accept() on the RSocket. A blank socket must be passed; it
    will serve as an endpoint for the newly created connection. This
    is an asynchronous call.
 */
void CBtAccepter::AcceptL(RSocket& aBlankSocket)
{
  if (IsActive())
    {
      AssertFail();
      return;
    }
  
  // accept a connection -- asynchronous
#if BTENGINE_LOGGING
  if (iLogger) {
    iLogger->Write(_L("making accept request"));
  }
#endif
  iSocket.Accept(aBlankSocket, iStatus);
  iState = EAccepting;
  SetActive();
}

void CBtAccepter::DoCancel()
{
  if (iState == EAccepting) {
    iSocket.CancelAccept();
  } else if (iState == EConfiguring) {
#ifndef __HAS_BT_SET_SECURITY__
    iBtSecuritySettings.CancelRequest(iStatus);
#endif
  } else {
    User::Invariant();
  }
}

void CBtAccepter::RunL()
{
  TInt status = iStatus.Int();
#if BTENGINE_LOGGING
  if (iLogger) {
    iLogger->WriteFormat(_L("RunL result %d"), status);
  }
#endif

  if (iState == EAccepting) {
#if BTENGINE_LOGGING
    if (iLogger) {
      iLogger->Write(_L("accept completed"));
    }
#endif

    iObserver.ClientAccepted(status);
  } else if (iState == EConfiguring) {
#if BTENGINE_LOGGING
    if (iLogger) {
      iLogger->Write(_L("configure completed"));
    }
#endif

#ifndef __HAS_BT_SET_SECURITY__
    if (status == KErrNone) {
      iSecuritySet = ETrue;
     
      TRAP(status, AdvertiseL());
    }
#endif

    iObserver.SocketConfigured(status);
  } else {
    User::Invariant();
  }
}

void CBtAccepter::AdvertiseL()
{
  // we must advertise the service, or most likely
  // no one will ever connect, as they do not know
  // the port
  if (!iAdvertiser) {
#if BTENGINE_LOGGING
    if (iLogger) {
      iLogger->Write(_L("creating advertiser"));
    }
#endif
    iAdvertiser = CPdisAdvertiser::NewL();
  }
  if (!iAdvertiser->IsRecordAdded()) {
#if BTENGINE_LOGGING
    if (iLogger) {
      iLogger->Write(_L("starting advertising"));
    }
#endif
    iAdvertiser->AddRecordL(iServerPort, iServiceId, iServiceName);
  }
}

#if BTENGINE_LOGGING
TInt CBtAccepter::RunError(TInt aError)
#else
TInt CBtAccepter::RunError(TInt)
#endif
{
#if BTENGINE_LOGGING
  if (iLogger) {
    iLogger->WriteFormat(_L("leave %d in RunL"), aError);
  }
#endif

  // the RunL() was supposed to be coded so as not to leave
  User::Invariant();

  // to indicate that we handled the error,
  // and the active scheduler does not need to
  return KErrNone;
}
