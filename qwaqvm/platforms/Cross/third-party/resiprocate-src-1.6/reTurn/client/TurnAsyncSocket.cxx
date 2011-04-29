#include "TurnAsyncSocket.hxx"
#include "../AsyncSocketBase.hxx"
#include "ErrorCode.hxx"
#include <boost/bind.hpp>
#include <rutil/WinLeakCheck.hxx>
#include <rutil/Logger.hxx>
#include "../ReTurnSubsystem.hxx"

#define RESIPROCATE_SUBSYSTEM ReTurnSubsystem::RETURN

using namespace std;
using namespace resip;

#define UDP_RT0 100  // RTO - Estimate of Roundtrip time - 100ms is recommened for fixed line transport - the initial value should be configurable
                     // Should also be calculation this on the fly
#define UDP_MAX_RETRANSMITS    7       // Defined by RFC5389 (Rc) - should be configurable
#define TCP_RESPONSE_TIME      39500   // Defined by RFC5389 (Ti) - should be configurable
#define UDP_Rm                 16      // Defined by RFC5389 - should be configurable
#define UDP_FINAL_REQUEST_TIME (UDP_RT0 * UDP_Rm)  // Defined by RFC5389

//#define TURN_CHANNEL_BINDING_REFRESH_SECONDS 20   // TESTING only
#define TURN_CHANNEL_BINDING_REFRESH_SECONDS 240   // 4 minuntes - this is one minute before the permission will expire, Note:  ChannelBinding refreshes also refresh permissions

#define SOFTWARE_STRING "reTURN Async Client 0.3 - RFC5389/turn-12   "  // Note padding size to a multiple of 4, to help compatibility with older clients

namespace reTurn {

// Initialize static members
unsigned int TurnAsyncSocket::UnspecifiedLifetime = 0xFFFFFFFF;
unsigned int TurnAsyncSocket::UnspecifiedBandwidth = 0xFFFFFFFF; 
unsigned short TurnAsyncSocket::UnspecifiedToken = 0;
asio::ip::address TurnAsyncSocket::UnspecifiedIpAddress = asio::ip::address::from_string("0.0.0.0");

TurnAsyncSocket::TurnAsyncSocket(asio::io_service& ioService, 
                                 AsyncSocketBase& asyncSocketBase,
                                 TurnAsyncSocketHandler* turnAsyncSocketHandler,
                                 const asio::ip::address& address, 
                                 unsigned short port) : 
   mIOService(ioService),
   mTurnAsyncSocketHandler(turnAsyncSocketHandler),
   mLocalBinding(StunTuple::None /* Set properly by sub class */, address, port),
   mHaveAllocation(false),
   mActiveDestination(0),
   mAsyncSocketBase(asyncSocketBase),
   mCloseAfterDestroyAllocationFinishes(false),
   mAllocationTimer(ioService)
{
}

TurnAsyncSocket::~TurnAsyncSocket()
{
   clearActiveRequestMap();
   cancelAllocationTimer();
   cancelChannelBindingTimers();

   DebugLog(<< "TurnAsyncSocket::~TurnAsyncSocket destroyed!");
}

void
TurnAsyncSocket::disableTurnAsyncHandler()
{
   mTurnAsyncSocketHandler = 0;
}

void
TurnAsyncSocket::requestSharedSecret()
{
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mIOService.post(boost::bind(&TurnAsyncSocket::doRequestSharedSecret, this));
}

void
TurnAsyncSocket::doRequestSharedSecret()
{
   GuardReleaser guardReleaser(mGuards);
   // Should we check here if TLS and deny?

   // Ensure Connected
   if(!mAsyncSocketBase.isConnected())
   {
      if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onSharedSecretFailure(getSocketDescriptor(), asio::error_code(reTurn::NotConnected, asio::error::misc_category));
   }
   else
   {
      // Form Shared Secret request
      StunMessage* request = createNewStunMessage(StunMessage::StunClassRequest, StunMessage::SharedSecretMethod);

      // Send the Request and start transaction timers
      sendStunMessage(request);
   }
}

void
TurnAsyncSocket::setUsernameAndPassword(const char* username, const char* password, bool shortTermAuth)
{
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mIOService.post(boost::bind(&TurnAsyncSocket::doSetUsernameAndPassword, this, new Data(username), new Data(password), shortTermAuth));
}

void 
TurnAsyncSocket::doSetUsernameAndPassword(Data* username, Data* password, bool shortTermAuth)
{
   GuardReleaser guardReleaser(mGuards);
   mUsername = *username;
   mPassword = *password;
   if(shortTermAuth)
   {
      // If we are using short term auth, then use short term password as HMAC key
      mHmacKey = *password;
   }
   delete username;
   delete password;
}

void 
TurnAsyncSocket::bindRequest()
{
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mIOService.post(boost::bind(&TurnAsyncSocket::doBindRequest, this));
}

void 
TurnAsyncSocket::doBindRequest()
{
   GuardReleaser guardReleaser(mGuards);
   // Ensure Connected
   if(!mAsyncSocketBase.isConnected())
   {
      if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onBindFailure(getSocketDescriptor(), asio::error_code(reTurn::NotConnected, asio::error::misc_category));
   }
   else
   {
      // Form Stun Bind request
      StunMessage* request = createNewStunMessage(StunMessage::StunClassRequest, StunMessage::BindMethod);
      sendStunMessage(request);
   }
}

void
TurnAsyncSocket::createAllocation(unsigned int lifetime,
                                  unsigned int bandwidth,
                                  unsigned char requestedProps, 
                                  UInt64 reservationToken,
                                  StunTuple::TransportType requestedTransportType)
{
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mIOService.post(boost::bind(&TurnAsyncSocket::doCreateAllocation, this, lifetime, 
                                                                           bandwidth, 
                                                                           requestedProps, 
                                                                           reservationToken, 
                                                                           requestedTransportType));
}

void
TurnAsyncSocket::doCreateAllocation(unsigned int lifetime,
                                    unsigned int bandwidth,
                                    unsigned char requestedProps, 
                                    UInt64 reservationToken,
                                    StunTuple::TransportType requestedTransportType)
{
   GuardReleaser guardReleaser(mGuards);

   // Store Allocation Properties
   mRequestedTransportType = requestedTransportType;

   // Relay Transport Type is requested type or socket type
   if(mRequestedTransportType != StunTuple::None)
   {
      mRelayTransportType = mRequestedTransportType;
   }
   else
   {
      mRelayTransportType = mLocalBinding.getTransportType();
   }

   // Ensure Connected
   if(!mAsyncSocketBase.isConnected())
   {
      if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onAllocationFailure(getSocketDescriptor(), asio::error_code(reTurn::NotConnected, asio::error::misc_category));
      return;
   }

   if(mHaveAllocation)
   {
      if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onAllocationFailure(getSocketDescriptor(), asio::error_code(reTurn::AlreadyAllocated, asio::error::misc_category));
      return;
   }

   // Form Turn Allocate request
   StunMessage* request = createNewStunMessage(StunMessage::StunClassRequest, StunMessage::TurnAllocateMethod);
   if(lifetime != UnspecifiedLifetime)
   {
      request->mHasTurnLifetime = true;
      request->mTurnLifetime = lifetime;
   }

   if(bandwidth != UnspecifiedBandwidth)
   {
      request->mHasTurnBandwidth = true;
      request->mTurnBandwidth = bandwidth;
   }

   if(requestedTransportType == StunTuple::None)
   {
      requestedTransportType = mLocalBinding.getTransportType();
   }
   request->mHasTurnRequestedTransport = true;
   if(requestedTransportType == StunTuple::UDP)
   {
      request->mTurnRequestedTransport = StunMessage::RequestedTransportUdp;
   }
   else if(requestedTransportType == StunTuple::TCP &&
           mLocalBinding.getTransportType() != StunTuple::UDP)  // Ensure client is not requesting TCP over a UDP transport
   {
      request->mTurnRequestedTransport = StunMessage::RequestedTransportTcp;
   }
   else
   {
      if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onAllocationFailure(getSocketDescriptor(), asio::error_code(reTurn::InvalidRequestedTransport, asio::error::misc_category));
      delete request;
      return;
   }
   
   if(requestedProps != StunMessage::PropsNone)
   {
      request->mHasTurnEvenPort = true;
      request->mTurnEvenPort.propType = requestedProps;
   }
   else if(reservationToken != 0)
   {
      request->mHasTurnReservationToken = true;
      request->mTurnReservationToken = reservationToken;
   }
      
   sendStunMessage(request);
}
   
void 
TurnAsyncSocket::refreshAllocation(unsigned int lifetime)
{
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mIOService.post(boost::bind(&TurnAsyncSocket::doRefreshAllocation, this, lifetime));
}

void 
TurnAsyncSocket::doRefreshAllocation(unsigned int lifetime)
{
   GuardReleaser guardReleaser(mGuards);
   if(!mHaveAllocation)
   {
      if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onRefreshFailure(getSocketDescriptor(), asio::error_code(NoAllocation, asio::error::misc_category));
      if(mCloseAfterDestroyAllocationFinishes)
      {
         mHaveAllocation = false;
         actualClose();
      }
      return;
   }

   // Form Turn Refresh request
   StunMessage* request = createNewStunMessage(StunMessage::StunClassRequest, StunMessage::TurnRefreshMethod);
   if(lifetime != UnspecifiedLifetime)
   {
      request->mHasTurnLifetime = true;
      request->mTurnLifetime = lifetime;
   }
   //if(mRequestedBandwidth != UnspecifiedBandwidth)
   //{
   //   request.mHasTurnBandwidth = true;
   //   request.mTurnBandwidth = mRequestedBandwidth;
   //}

   sendStunMessage(request);
}

void 
TurnAsyncSocket::destroyAllocation()
{
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mIOService.post(boost::bind(&TurnAsyncSocket::doDestroyAllocation, this));
}

void 
TurnAsyncSocket::doDestroyAllocation()
{
   doRefreshAllocation(0);
}

void
TurnAsyncSocket::setActiveDestination(const asio::ip::address& address, unsigned short port)
{
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mIOService.post(boost::bind(&TurnAsyncSocket::doSetActiveDestination, this, address, port));
}

void
TurnAsyncSocket::doSetActiveDestination(const asio::ip::address& address, unsigned short port)
{
   GuardReleaser guardReleaser(mGuards);

   // Setup Remote Peer 
   StunTuple remoteTuple(mRelayTransportType, address, port);
   RemotePeer* remotePeer = mChannelManager.findRemotePeerByPeerAddress(remoteTuple);
   if(remotePeer)
   {
      mActiveDestination = remotePeer;
   }
   else
   {
      // No channel binding yet (ie. not data sent or received from remote peer) - so create one
      mActiveDestination = mChannelManager.createChannelBinding(remoteTuple);
      assert(mActiveDestination);
      doChannelBinding(*mActiveDestination);
   }
   DebugLog(<< "TurnAsyncSocket::doSetActiveDestination: Active Destination set to: " << remoteTuple);
   if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onSetActiveDestinationSuccess(getSocketDescriptor());
}

void TurnAsyncSocket::doChannelBinding(RemotePeer& remotePeer)
{
   // Form Channel Bind request
   StunMessage* request = createNewStunMessage(StunMessage::StunClassRequest, StunMessage::TurnChannelBindMethod);

   // Set headers
   request->mHasTurnChannelNumber = true;
   request->mTurnChannelNumber = remotePeer.getChannel();
   request->mHasTurnXorPeerAddress = true;
   StunMessage::setStunAtrAddressFromTuple(request->mTurnXorPeerAddress, remotePeer.getPeerTuple());

   // Send the Request and start transaction timers
   sendStunMessage(request);

   // If not using UDP - then mark channel as confirmed - otherwise wait for ChannelBind response
   if(mLocalBinding.getTransportType() != StunTuple::UDP)
   {
      remotePeer.setChannelConfirmed();
   }
}

void
TurnAsyncSocket::clearActiveDestination()
{
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mIOService.post(boost::bind(&TurnAsyncSocket::doClearActiveDestination, this));
}

void
TurnAsyncSocket::doClearActiveDestination()
{
   GuardReleaser guardReleaser(mGuards);

   // ensure there is an allocation
   if(!mHaveAllocation)
   {
      if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onClearActiveDestinationFailure(getSocketDescriptor(), asio::error_code(reTurn::NoAllocation, asio::error::misc_category));
      return;
   }

   mActiveDestination = 0;
   if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onClearActiveDestinationSuccess(getSocketDescriptor());
}

StunMessage* 
TurnAsyncSocket::createNewStunMessage(UInt16 stunclass, UInt16 method, bool addAuthInfo)
{
   StunMessage* msg = new StunMessage();
   msg->createHeader(stunclass, method);

   // Add Software Attribute
   msg->setSoftware(SOFTWARE_STRING);

   if(addAuthInfo && !mUsername.empty() && !mHmacKey.empty())
   {
      msg->mHasMessageIntegrity = true;
      msg->setUsername(mUsername.c_str()); 
      msg->mHmacKey = mHmacKey;
      if(!mRealm.empty())
      {
         msg->setRealm(mRealm.c_str());
      }
      if(!mNonce.empty())
      {
         msg->setNonce(mNonce.c_str());
      }
   }
   return msg;
}

void
TurnAsyncSocket::sendStunMessage(StunMessage* message, bool reTransmission)
{
#define REQUEST_BUFFER_SIZE 1024
   boost::shared_ptr<DataBuffer> buffer = AsyncSocketBase::allocateBuffer(REQUEST_BUFFER_SIZE);
   unsigned int bufferSize;
   bufferSize = message->stunEncodeMessage((char*)buffer->data(), REQUEST_BUFFER_SIZE);
   buffer->truncate(bufferSize);  // set size to real size

   if(!reTransmission)
   {
      // If message is a request, then start appropriate transaction and retranmission timers
      if(message->mClass == StunMessage::StunClassRequest)
      {
         boost::shared_ptr<RequestEntry> requestEntry(new RequestEntry(mIOService, this, message));
         mActiveRequestMap[message->mHeader.magicCookieAndTid] = requestEntry;
         requestEntry->startTimer();
      }
      else
      {
         delete message;
      }
   }

   send(buffer);
}

void 
TurnAsyncSocket::handleReceivedData(const asio::ip::address& address, unsigned short port, boost::shared_ptr<DataBuffer>& data)
{
   if(data->size() > 4)
   {
      // Stun Message has first two bits as 00 
      if((((*data)[0]) & 0xC0) == 0)
      {
         StunMessage* stunMsg = new StunMessage(mLocalBinding, 
                                                StunTuple(mLocalBinding.getTransportType(), mAsyncSocketBase.getConnectedAddress(), mAsyncSocketBase.getConnectedPort()), 
                                                &(*data)[0], data->size());
         if(stunMsg->isValid())
         {
            handleStunMessage(*stunMsg);
            delete stunMsg;
            return;
         }
         delete stunMsg;

         // Not a stun message so assume normal data
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onReceiveSuccess(getSocketDescriptor(), 
                                                      address, 
                                                      port, 
                                                      data);
      }
      else if(mHaveAllocation) // If we have an allocation then this is a Turn Channel Data Message
      {
         // Get Channel number
         unsigned short channelNumber;
         memcpy(&channelNumber, &(*data)[0], 2);
         channelNumber = ntohs(channelNumber);

         if(mLocalBinding.getTransportType() == StunTuple::UDP)
         {
            // Check if the UDP datagram size is too short to contain the claimed length of the ChannelData message, then discard
            unsigned short dataLen;
            memcpy(&dataLen, &(*data)[2], 2);
            dataLen = ntohs(dataLen);

            if(data->size() < (unsigned int)dataLen+4)
            {
               WarningLog(<< "ChannelData message size=" << dataLen+4 << " too large for UDP packet size=" << data->size() <<".  Dropping.");
               return;
            }
         }

         RemotePeer* remotePeer = mChannelManager.findRemotePeerByChannel(channelNumber);
         if(remotePeer)
         {
            data->offset(4);  // move buffer start past framing for callback
            if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onReceiveSuccess(getSocketDescriptor(), 
                                                      remotePeer->getPeerTuple().getAddress(), 
                                                      remotePeer->getPeerTuple().getPort(), 
                                                      data);
         }
         else
         {
            WarningLog(<< "TurnAsyncSocket::handleReceivedData: receive channel data for non-existing channel - discarding!");
         }
      }
      else
      {
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onReceiveSuccess(getSocketDescriptor(), 
                                                   address, 
                                                   port, 
                                                   data);
      }
   }
   else  // size <= 4
   {
      WarningLog(<< "TurnAsyncSocket::handleReceivedData: not enough data received (" << data->size() << " bytes) for stun or channel data message - discarding!");
      if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onReceiveFailure(getSocketDescriptor(), asio::error_code(reTurn::FrameError, asio::error::misc_category));         
   }
}

asio::error_code 
TurnAsyncSocket::handleStunMessage(StunMessage& stunMessage)
{
   asio::error_code errorCode;
   if(stunMessage.isValid())
   {
      if(!stunMessage.checkMessageIntegrity(mHmacKey))
      {
         WarningLog(<< "TurnAsyncSocket::handleStunMessage: Stun message integrity is bad!");
         return asio::error_code(reTurn::BadMessageIntegrity, asio::error::misc_category);
      }

      // Request is authenticated, process it
      switch(stunMessage.mClass)
      { 
      case StunMessage::StunClassRequest:
         switch (stunMessage.mMethod) 
         {
         case StunMessage::BindMethod:
            if(stunMessage.mUnknownRequiredAttributes.numAttributes > 0)
            {
               // There were unknown comprehension-required attributes in the request
               StunMessage* response = new StunMessage();
               response->mClass = StunMessage::StunClassErrorResponse;
               response->mMethod = stunMessage.mMethod;
               response->setErrorCode(420, "Unknown Attribute");  
               // Copy over TransactionId
               response->mHeader.magicCookieAndTid = stunMessage.mHeader.magicCookieAndTid;
               // Add Unknown Attributes
               response->mHasUnknownAttributes = true;
               response->mUnknownAttributes = stunMessage.mUnknownRequiredAttributes;
               // Add Software Attribute
               response->setSoftware(SOFTWARE_STRING);
               sendStunMessage(response);
            }
            else
            {
               errorCode = handleBindRequest(stunMessage);
            }
            break;
         case StunMessage::SharedSecretMethod:
         case StunMessage::TurnAllocateMethod:
         case StunMessage::TurnRefreshMethod:
         default:
            // These requests are not handled by a client
            StunMessage* response = new StunMessage();
            response->mClass = StunMessage::StunClassErrorResponse;
            response->mMethod = stunMessage.mMethod;
            response->setErrorCode(400, "Invalid Request Method");  
            // Copy over TransactionId
            response->mHeader.magicCookieAndTid = stunMessage.mHeader.magicCookieAndTid;
            // Add Software Attribute
            response->setSoftware(SOFTWARE_STRING);
            sendStunMessage(response);
            break;
         }
         break;

      case StunMessage::StunClassIndication:
         switch (stunMessage.mMethod) 
         {
         case StunMessage::TurnDataMethod: 
            if(stunMessage.mUnknownRequiredAttributes.numAttributes > 0)
            {
               // Unknown Comprehension-Required Attributes found
               WarningLog(<< "Ignoring DataInd with unknown comprehension required attributes.");
               errorCode = asio::error_code(reTurn::UnknownRequiredAttributes, asio::error::misc_category);
            }
            else
            {
               errorCode = handleDataInd(stunMessage);
            }
            break;
         case StunMessage::BindMethod:
            // A Bind indication is simply a keepalive with no response required
            break;
         case StunMessage::TurnSendMethod:  // Don't need to handle these - only sent by client, never received
         default:
            // Unknown indication - just ignore
            break;
         }
         break;
   
      case StunMessage::StunClassSuccessResponse:
      case StunMessage::StunClassErrorResponse:
      {
         if(stunMessage.mUnknownRequiredAttributes.numAttributes > 0)
         {
            // Unknown Comprehension-Required Attributes found
            WarningLog(<< "Ignoring Response with unknown comprehension required attributes.");
            return asio::error_code(reTurn::UnknownRequiredAttributes, asio::error::misc_category);
         }

         // First check if this response is for an active request
         RequestMap::iterator it = mActiveRequestMap.find(stunMessage.mHeader.magicCookieAndTid);
         if(it == mActiveRequestMap.end())
         {
            // Stray response - dropping
            return asio::error_code(reTurn::StrayResponse, asio::error::misc_category);
         }
         else
         {
            it->second->stopTimer();

            // If a realm and nonce attributes are present and the response is a 401 or 438 (Nonce Expired), 
            // then re-issue request with new auth attributes
            if(stunMessage.mHasRealm &&
               stunMessage.mHasNonce &&
               stunMessage.mHasErrorCode && 
               stunMessage.mErrorCode.errorClass == 4 &&
               ((stunMessage.mErrorCode.number == 1 && mHmacKey.empty()) ||  // Note if 401 error then ensure we haven't already tried once - if we've tried then mHmacKey will be populated
               stunMessage.mErrorCode.number == 38))
            {
               mNonce = *stunMessage.mNonce;
               mRealm = *stunMessage.mRealm;
               stunMessage.calculateHmacKey(mHmacKey, mUsername, mRealm, mPassword);

               // Create a new transaction - by starting with old request
               StunMessage* newRequest = it->second->mRequestMessage;
               it->second->mRequestMessage = 0;  // clear out pointer in mActiveRequestMap so that it will not be deleted
               mActiveRequestMap.erase(it);

               newRequest->createHeader(newRequest->mClass, newRequest->mMethod);  // updates TID
               newRequest->mHasMessageIntegrity = true;
               newRequest->setUsername(mUsername.c_str()); 
               newRequest->mHmacKey = mHmacKey;
               newRequest->setRealm(mRealm.c_str());
               newRequest->setNonce(mNonce.c_str());
               sendStunMessage(newRequest);
               return errorCode;
            }          
         }

         switch (stunMessage.mMethod) 
         {
         case StunMessage::BindMethod:
            errorCode = handleBindResponse(*it->second->mRequestMessage, stunMessage);
            break;
         case StunMessage::SharedSecretMethod:
            errorCode = handleSharedSecretResponse(*it->second->mRequestMessage, stunMessage);
            break;
         case StunMessage::TurnAllocateMethod:
            errorCode = handleAllocateResponse(*it->second->mRequestMessage, stunMessage);
            break;
         case StunMessage::TurnRefreshMethod:
            errorCode = handleRefreshResponse(*it->second->mRequestMessage, stunMessage);
            break;
         case StunMessage::TurnChannelBindMethod:
            errorCode = handleChannelBindResponse(*it->second->mRequestMessage, stunMessage);
            break;
         default:
            // Unknown method - just ignore
            break;
         }

         // Remove request from map if we haven't already cleared the map above;
         // (the handlers may have cleared the ActiveRequestMap when trying to close the connection)
		 if (!mActiveRequestMap.empty())
			mActiveRequestMap.erase(it);
      }
      break;

      default:
         // Illegal message class - ignore
         break;
      }
   }
   else
   {
      WarningLog(<< "TurnAsyncSocket::handleStunMessage: Read Invalid StunMsg.");
      return asio::error_code(reTurn::ErrorParsingMessage, asio::error::misc_category);
   }
   return errorCode;
}

asio::error_code
TurnAsyncSocket::handleDataInd(StunMessage& stunMessage)
{
   if(!stunMessage.mHasTurnXorPeerAddress || !stunMessage.mHasTurnData)
   {
      // Missing RemoteAddress or TurnData attribute
      WarningLog(<< "TurnAsyncSocket::handleDataInd: DataInd missing attributes.");
      return asio::error_code(reTurn::MissingAttributes, asio::error::misc_category);
   }

   StunTuple remoteTuple;
   remoteTuple.setTransportType(mRelayTransportType);
   StunMessage::setTupleFromStunAtrAddress(remoteTuple, stunMessage.mTurnXorPeerAddress);

   RemotePeer* remotePeer = mChannelManager.findRemotePeerByPeerAddress(remoteTuple);
   if(!remotePeer)
   {
      // Remote Peer not found - discard data
      WarningLog(<< "TurnAsyncSocket::handleDataInd: Data received from unknown RemotePeer " << remoteTuple << " - discarding");
      return asio::error_code(reTurn::UnknownRemoteAddress, asio::error::misc_category);
   }

   boost::shared_ptr<DataBuffer> data(new DataBuffer(stunMessage.mTurnData->data(), stunMessage.mTurnData->size()));
   if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onReceiveSuccess(getSocketDescriptor(), 
      remoteTuple.getAddress(), 
      remoteTuple.getPort(), 
      data);

   return asio::error_code();
}

asio::error_code
TurnAsyncSocket::handleChannelBindResponse(StunMessage &request, StunMessage &response)
{
   if(response.mClass == StunMessage::StunClassSuccessResponse)
   {
      assert(request.mHasTurnChannelNumber);

      RemotePeer* remotePeer = mChannelManager.findRemotePeerByChannel(request.mTurnChannelNumber);
      if(!remotePeer)
      {
         // Remote Peer not found - discard
         WarningLog(<< "TurnAsyncSocket::handleChannelBindResponse: Received ChannelBindResponse for unknown channel (" << response.mTurnChannelNumber << ") - discarding");
         return asio::error_code(reTurn::InvalidChannelNumberReceived, asio::error::misc_category);
      }

      DebugLog(<< "TurnAsyncSocket::handleChannelBindResponse: Channel " << remotePeer->getChannel() << " is now bound to " << remotePeer->getPeerTuple());
      remotePeer->refresh();
      remotePeer->setChannelConfirmed();
      startChannelBindingTimer(remotePeer->getChannel());
   }
   else
   {
      // Check error code
      if(response.mHasErrorCode)
      {
         ErrLog(<< "TurnAsyncSocket::handleChannelBindResponse: Received ChannelBindResponse error: " << response.mErrorCode.errorClass * 100 + response.mErrorCode.number);
         return asio::error_code(response.mErrorCode.errorClass * 100 + response.mErrorCode.number, asio::error::misc_category);
      }
      else
      {
         ErrLog(<< "TurnAsyncSocket::handleChannelBindResponse: Received ChannelBindResponse error but no error code attribute found.");
         return asio::error_code(MissingAttributes, asio::error::misc_category);
      }
   }
   return asio::error_code();
}

asio::error_code 
TurnAsyncSocket::handleSharedSecretResponse(StunMessage &request, StunMessage &response)
{
   if(response.mClass == StunMessage::StunClassSuccessResponse)
   {
      // Copy username and password to callers buffer - checking sizes first
      if(!response.mHasUsername || !response.mHasPassword)
      {
         WarningLog(<< "TurnAsyncSocket::handleSharedSecretResponse: Stun response message for SharedSecretRequest is missing username and/or password!");
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onSharedSecretFailure(getSocketDescriptor(), asio::error_code(MissingAttributes, asio::error::misc_category));
         return asio::error_code(MissingAttributes, asio::error::misc_category);
      }

      if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onSharedSecretSuccess(getSocketDescriptor(), response.mUsername->c_str(), response.mUsername->size(), 
                                                                            response.mPassword->c_str(), response.mPassword->size());
   }
   else
   {
      // Check error code
      if(response.mHasErrorCode)
      {
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onSharedSecretFailure(getSocketDescriptor(), asio::error_code(response.mErrorCode.errorClass * 100 + response.mErrorCode.number, asio::error::misc_category));
      }
      else
      {
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onSharedSecretFailure(getSocketDescriptor(), asio::error_code(MissingAttributes, asio::error::misc_category));
         return asio::error_code(MissingAttributes, asio::error::misc_category);
      }
   }
   return asio::error_code();
}

asio::error_code
TurnAsyncSocket::handleBindRequest(StunMessage& stunMessage)
{
   // Note: handling of BindRequest is not fully backwards compatible with RFC3489 - it is inline with RFC5389
   StunMessage* response = new StunMessage();

   // form the outgoing message
   response->mClass = StunMessage::StunClassSuccessResponse;
   response->mMethod = StunMessage::BindMethod;

   // Copy over TransactionId
   response->mHeader.magicCookieAndTid = stunMessage.mHeader.magicCookieAndTid;

   // Add XOrMappedAddress to response 
   response->mHasXorMappedAddress = true;
   StunMessage::setStunAtrAddressFromTuple(response->mXorMappedAddress, stunMessage.mRemoteTuple);

   // Add Software Attribute
   response->setSoftware(SOFTWARE_STRING);

   // send bindResponse to local client
   sendStunMessage(response);

   return asio::error_code();
}

asio::error_code 
TurnAsyncSocket::handleBindResponse(StunMessage &request, StunMessage &response)
{
   if(response.mClass == StunMessage::StunClassSuccessResponse)
   {
      StunTuple reflexiveTuple;
      reflexiveTuple.setTransportType(mLocalBinding.getTransportType());
      if(response.mHasXorMappedAddress)
      {
         StunMessage::setTupleFromStunAtrAddress(reflexiveTuple, response.mXorMappedAddress);
      }
      else if(response.mHasMappedAddress)  // Only look at MappedAddress if XorMappedAddress is not found - for backwards compatibility
      {
         StunMessage::setTupleFromStunAtrAddress(reflexiveTuple, response.mMappedAddress);
      }
      else
      {
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onBindFailure(getSocketDescriptor(), asio::error_code(MissingAttributes, asio::error::misc_category));
         return asio::error_code(MissingAttributes, asio::error::misc_category);
      }
      if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onBindSuccess(getSocketDescriptor(), reflexiveTuple);
   }
   else
   {
      // Check if success or not
      if(response.mHasErrorCode)
      {
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onBindFailure(getSocketDescriptor(), asio::error_code(response.mErrorCode.errorClass * 100 + response.mErrorCode.number, asio::error::misc_category));
      }
      else
      {
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onBindFailure(getSocketDescriptor(), asio::error_code(MissingAttributes, asio::error::misc_category));
         return asio::error_code(MissingAttributes, asio::error::misc_category);
      }
   }
   return asio::error_code();
}

asio::error_code 
TurnAsyncSocket::handleAllocateResponse(StunMessage &request, StunMessage &response)
{
   if(response.mClass == StunMessage::StunClassSuccessResponse)
   {
      StunTuple reflexiveTuple;
      StunTuple relayTuple;
      if(response.mHasXorMappedAddress)
      {
         reflexiveTuple.setTransportType(mLocalBinding.getTransportType());
         StunMessage::setTupleFromStunAtrAddress(reflexiveTuple, response.mXorMappedAddress);
      }
      if(response.mHasTurnXorRelayedAddress)
      {
         relayTuple.setTransportType(mRelayTransportType);
         StunMessage::setTupleFromStunAtrAddress(relayTuple, response.mTurnXorRelayedAddress);
      }
      if(response.mHasTurnLifetime)
      {
         mLifetime = response.mTurnLifetime;
      }
      else
      {
         mLifetime = 0;
      }

      // All was well - return 0 errorCode
      if(mLifetime != 0)
      {
         mHaveAllocation = true;
         startAllocationTimer();
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onAllocationSuccess(getSocketDescriptor(), 
                                                                                  reflexiveTuple, 
                                                                                  relayTuple, 
                                                                                  mLifetime, 
                                                                                  response.mHasTurnBandwidth ? response.mTurnBandwidth : 0,
                                                                                  response.mHasTurnReservationToken ? response.mTurnReservationToken : 0);
      }
      else
      {
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onAllocationFailure(getSocketDescriptor(), asio::error_code(MissingAttributes, asio::error::misc_category));
      }
   }
   else
   {
      // Check if success or not
      if(response.mHasErrorCode)
      {
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onAllocationFailure(getSocketDescriptor(), asio::error_code(response.mErrorCode.errorClass * 100 + response.mErrorCode.number, asio::error::misc_category));
      }
      else
      {
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onAllocationFailure(getSocketDescriptor(), asio::error_code(MissingAttributes, asio::error::misc_category));
         return asio::error_code(MissingAttributes, asio::error::misc_category);
      }
   }
   return asio::error_code();
}

asio::error_code 
TurnAsyncSocket::handleRefreshResponse(StunMessage &request, StunMessage &response)
{
   if(response.mClass == StunMessage::StunClassSuccessResponse)
   {
      if(response.mHasTurnLifetime)
      {
         mLifetime = response.mTurnLifetime;
      }
      else
      {
         mLifetime = 0;
      }
      if(mLifetime != 0)
      {
         mHaveAllocation = true;
         startAllocationTimer();
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onRefreshSuccess(getSocketDescriptor(), mLifetime);
         if(mCloseAfterDestroyAllocationFinishes)
         {
            mHaveAllocation = false;
            actualClose();
         }
      }
      else
      {
         cancelAllocationTimer();
         mHaveAllocation = false;
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onRefreshSuccess(getSocketDescriptor(), 0);
         if(mCloseAfterDestroyAllocationFinishes)
         {
            actualClose();
         }
      }
   }
   else
   {
      // Check if success or not
      if(response.mHasErrorCode)
      {
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onRefreshFailure(getSocketDescriptor(), asio::error_code(response.mErrorCode.errorClass * 100 + response.mErrorCode.number, asio::error::misc_category));
         if(mCloseAfterDestroyAllocationFinishes)
         {
            cancelAllocationTimer();
            mHaveAllocation = false;
            actualClose();
         }
         else if(response.mErrorCode.errorClass == 4 && response.mErrorCode.number == 37) // response is 437, then remove allocation
         {
            cancelAllocationTimer();
            mHaveAllocation = false;
         }
      }
      else
      {
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onRefreshFailure(getSocketDescriptor(), asio::error_code(MissingAttributes, asio::error::misc_category));
         if(mCloseAfterDestroyAllocationFinishes)
         {
            cancelAllocationTimer();
            mHaveAllocation = false;
            actualClose();
         }
         return asio::error_code(MissingAttributes, asio::error::misc_category);
      }
   }
   return asio::error_code();
}

void
TurnAsyncSocket::send(const char* buffer, unsigned int size)
{
   boost::shared_ptr<DataBuffer> data(new DataBuffer(buffer, size));
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mIOService.post(boost::bind(&TurnAsyncSocket::doSend, this, data));
}

void
TurnAsyncSocket::doSend(boost::shared_ptr<DataBuffer>& data)
{
   GuardReleaser guardReleaser(mGuards);

   // Allow raw data to be sent if there is no allocation
   if(!mHaveAllocation)
   {
      send(data);
      return;
   }

   return sendTo(*mActiveDestination, data);
}

void 
TurnAsyncSocket::sendTo(const asio::ip::address& address, unsigned short port, const char* buffer, unsigned int size)
{
   boost::shared_ptr<DataBuffer> data(new DataBuffer(buffer, size));
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mIOService.post(boost::bind(&TurnAsyncSocket::doSendTo, this, address, port, data));
}

void 
TurnAsyncSocket::doSendTo(const asio::ip::address& address, unsigned short port, boost::shared_ptr<DataBuffer>& data)
{
   GuardReleaser guardReleaser(mGuards);

   // Allow raw data to be sent if there is no allocation
   if(!mHaveAllocation)
   {
      StunTuple destination(mLocalBinding.getTransportType(), address, port);
      mAsyncSocketBase.send(destination, data);
      return;
   }

   // Setup Remote Peer 
   StunTuple remoteTuple(mRelayTransportType, address, port);
   RemotePeer* remotePeer = mChannelManager.findRemotePeerByPeerAddress(remoteTuple);
   if(!remotePeer)
   {
      // No remote peer yet (ie. no data sent or received from remote peer) - so create one
      remotePeer = mChannelManager.createChannelBinding(remoteTuple);
      assert(remotePeer);
      doChannelBinding(*remotePeer);
   }
   return sendTo(*remotePeer, data);
}

void
TurnAsyncSocket::sendTo(RemotePeer& remotePeer, boost::shared_ptr<DataBuffer>& data)
{
   if(remotePeer.isChannelConfirmed())
   {
      // send framed data to active destination
      send(remotePeer.getChannel(), data);
      //InfoLog( << "TurnAsyncSocket::sendTo: using channel " << remotePeer.getChannel() << " to send " << data->size() << " bytes.");
   }
   else
   {
      // Data must be wrapped in a Send Indication
      // Wrap data in a SendInd
      StunMessage* ind = createNewStunMessage(StunMessage::StunClassIndication, StunMessage::TurnSendMethod, false);
      ind->mHasTurnXorPeerAddress = true;
      StunMessage::setStunAtrAddressFromTuple(ind->mTurnXorPeerAddress, remotePeer.getPeerTuple());
      if(data->size() > 0)
      {
         ind->setTurnData(data->data(), data->size());
      }

      // Send indication to Turn Server
      sendStunMessage(ind);
   }
}

void
TurnAsyncSocket::connect(const std::string& address, unsigned short port)
{
   mAsyncSocketBase.connect(address,port);
}

void
TurnAsyncSocket::close()
{
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mIOService.post(boost::bind(&TurnAsyncSocket::doClose, this));
}

void
TurnAsyncSocket::doClose()
{
   GuardReleaser guardReleaser(mGuards);

   // If we have an allocation over UDP then we should send a refresh with lifetime 0 to destroy the allocation
   // Note:  For TCP and TLS, the socket disconnection will destroy the allocation automatically
   if(mHaveAllocation && mLocalBinding.getTransportType() == StunTuple::UDP)
   {
      mCloseAfterDestroyAllocationFinishes = true;
      destroyAllocation();
   }
   else
   {
      actualClose();
   }
}

void
TurnAsyncSocket::actualClose()
{
   clearActiveRequestMap();
   cancelAllocationTimer();
   cancelChannelBindingTimers();
   mAsyncSocketBase.close();
}

void 
TurnAsyncSocket::turnReceive()
{
   if(mLocalBinding.getTransportType() == StunTuple::UDP)
   {
      //mAsyncSocketBase.receive();
      mAsyncSocketBase.doReceive();
   }
   else
   {
      //mAsyncSocketBase.framedReceive();
      mAsyncSocketBase.doFramedReceive();
   }
}

void 
TurnAsyncSocket::send(boost::shared_ptr<DataBuffer>& data)
{
   StunTuple destination(mLocalBinding.getTransportType(), mAsyncSocketBase.getConnectedAddress(), mAsyncSocketBase.getConnectedPort());
   mAsyncSocketBase.send(destination, data);
}

void 
TurnAsyncSocket::send(unsigned short channel, boost::shared_ptr<DataBuffer>& data)
{
   StunTuple destination(mLocalBinding.getTransportType(), mAsyncSocketBase.getConnectedAddress(), mAsyncSocketBase.getConnectedPort());
   mAsyncSocketBase.send(destination, channel, data);
}

TurnAsyncSocket::RequestEntry::RequestEntry(asio::io_service& ioService, 
                                            TurnAsyncSocket* turnAsyncSocket, 
                                            StunMessage* requestMessage) : 
   mIOService(ioService), 
   mTurnAsyncSocket(turnAsyncSocket), 
   mRequestMessage(requestMessage), 
   mRequestTimer(ioService),
   mRequestsSent(1)
{
   mTimeout = mTurnAsyncSocket->mLocalBinding.getTransportType() == StunTuple::UDP ? UDP_RT0 : TCP_RESPONSE_TIME;
}

void
TurnAsyncSocket::RequestEntry::startTimer()
{
   // start the request timer
   mRequestTimer.expires_from_now(boost::posix_time::milliseconds(mTimeout));  
   mRequestTimer.async_wait(boost::bind(&TurnAsyncSocket::RequestEntry::requestTimerExpired, shared_from_this(), asio::placeholders::error));
}

void
TurnAsyncSocket::RequestEntry::stopTimer()
{
   // stop the request timer
   mRequestTimer.cancel();
}

TurnAsyncSocket::RequestEntry::~RequestEntry() 
{ 
   delete mRequestMessage; 
}

void 
TurnAsyncSocket::RequestEntry::requestTimerExpired(const asio::error_code& e)
{
   if(!e && mRequestMessage)  // Note:  There is a race condition with clearing out of mRequestMessage when 401 is received - check that mRequestMessage is not 0 avoids any resulting badness
   {
      if(mTurnAsyncSocket->mLocalBinding.getTransportType() != StunTuple::UDP || mRequestsSent == UDP_MAX_RETRANSMITS)
      {
         mTurnAsyncSocket->requestTimeout(mRequestMessage->mHeader.magicCookieAndTid);
         return;
      }
      // timed out and should retransmit - calculate next timeout
      if(mRequestsSent == UDP_MAX_RETRANSMITS - 1)
      {
          mTimeout = UDP_FINAL_REQUEST_TIME;
      } 
      else
      {
          mTimeout = (mTimeout*2);
      }
      // retransmit
      DebugLog(<< "RequestEntry::requestTimerExpired: retransmitting...");
      mRequestsSent++;
      mTurnAsyncSocket->sendStunMessage(mRequestMessage, true);

      startTimer();
   }
}

void 
TurnAsyncSocket::requestTimeout(UInt128 tid)
{
   RequestMap::iterator it = mActiveRequestMap.find(tid);
   if(it != mActiveRequestMap.end())
   {
      switch(it->second->mRequestMessage->mMethod)
      {
      case StunMessage::BindMethod:
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onBindFailure(getSocketDescriptor(), asio::error_code(reTurn::ResponseTimeout, asio::error::misc_category));
         break;
      case StunMessage::SharedSecretMethod:
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onSharedSecretFailure(getSocketDescriptor(), asio::error_code(reTurn::ResponseTimeout, asio::error::misc_category));
         break;
      case StunMessage::TurnAllocateMethod:
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onAllocationFailure(getSocketDescriptor(), asio::error_code(reTurn::ResponseTimeout, asio::error::misc_category));
         break;
      case StunMessage::TurnRefreshMethod:
         if(mTurnAsyncSocketHandler) mTurnAsyncSocketHandler->onRefreshFailure(getSocketDescriptor(), asio::error_code(reTurn::ResponseTimeout, asio::error::misc_category));
         if(mCloseAfterDestroyAllocationFinishes)
         {
            mHaveAllocation = false;
            actualClose();
         }
         break;
      default:
         assert(false);
      }

	  // Remove request from map if we haven't already cleared the map above
	  // (the handlers may have cleared the ActiveRequestMap when trying to close the connection)
	  if (!mActiveRequestMap.empty())
	      mActiveRequestMap.erase(it);
   }
}

void
TurnAsyncSocket::clearActiveRequestMap()
{
   // Clear out active request map - !slg! TODO this really should happen from the io service thread
   RequestMap::iterator it = mActiveRequestMap.begin();
   for(;it != mActiveRequestMap.end(); it++)
   {
      it->second->stopTimer();
   }
   mActiveRequestMap.clear();
}

void
TurnAsyncSocket::startAllocationTimer()
{
   mAllocationTimer.expires_from_now(boost::posix_time::seconds((mLifetime*5)/8));  // Allocation refresh should sent before 3/4 lifetime - use 5/8 lifetime 
   mGuards.push(mAsyncSocketBase.shared_from_this());
   mAllocationTimer.async_wait(boost::bind(&TurnAsyncSocket::allocationTimerExpired, this, asio::placeholders::error));
}

void
TurnAsyncSocket::cancelAllocationTimer()
{
   mAllocationTimer.cancel();
}

void 
TurnAsyncSocket::allocationTimerExpired(const asio::error_code& e)
{
   if(!e)
   {
      doRefreshAllocation(mLifetime);
   }
   else
   {
      // Note:  only release guard if not calling doRefreshAllocation - since
      // doRefreshAllocation will release the guard
      GuardReleaser guardReleaser(mGuards);
   }
}

void
TurnAsyncSocket::startChannelBindingTimer(unsigned short channel)
{
   ChannelBindingTimerMap::iterator it = mChannelBindingTimers.find(channel);
   if(it==mChannelBindingTimers.end())
   {
      std::pair<ChannelBindingTimerMap::iterator,bool> ret = 
         mChannelBindingTimers.insert(std::pair<unsigned short, asio::deadline_timer*>(channel, new asio::deadline_timer(mIOService)));
      assert(ret.second);
      it = ret.first;
   }
   it->second->expires_from_now(boost::posix_time::seconds(TURN_CHANNEL_BINDING_REFRESH_SECONDS));  
   mGuards.push(mAsyncSocketBase.shared_from_this());
   it->second->async_wait(boost::bind(&TurnAsyncSocket::channelBindingTimerExpired, this, asio::placeholders::error, channel));
}

void
TurnAsyncSocket::cancelChannelBindingTimers()
{
   // Cleanup ChannelBinding Timers
   ChannelBindingTimerMap::iterator it = mChannelBindingTimers.begin();
   for(;it!=mChannelBindingTimers.end();it++)
   {
      it->second->cancel();
      delete it->second;
   }
   mChannelBindingTimers.clear();
}

void 
TurnAsyncSocket::channelBindingTimerExpired(const asio::error_code& e, unsigned short channel)
{
   GuardReleaser guardReleaser(mGuards);
   if(!e)
   {
      RemotePeer* remotePeer = mChannelManager.findRemotePeerByChannel(channel);
      if(remotePeer)
      {
         doChannelBinding(*remotePeer);
      }
   }
}

} // namespace


/* ====================================================================

 Copyright (c) 2007-2008, Plantronics, Inc.
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are 
 met:

 1. Redistributions of source code must retain the above copyright 
    notice, this list of conditions and the following disclaimer. 

 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution. 

 3. Neither the name of Plantronics nor the names of its contributors 
    may be used to endorse or promote products derived from this 
    software without specific prior written permission. 

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ==================================================================== */
