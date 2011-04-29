#include "UdpServer.hxx"
#include "StunMessage.hxx"
#include <boost/bind.hpp>
#include <rutil/WinLeakCheck.hxx>
#include <rutil/Logger.hxx>
#include "ReTurnSubsystem.hxx"

#define RESIPROCATE_SUBSYSTEM ReTurnSubsystem::RETURN

using namespace std;
using namespace resip;

namespace reTurn {

UdpServer::UdpServer(asio::io_service& ioService, RequestHandler& requestHandler, const asio::ip::address& address, unsigned short port)
: AsyncUdpSocketBase(ioService),
  mRequestHandler(requestHandler),
  mAlternatePortUdpServer(0),
  mAlternateIpUdpServer(0),
  mAlternateIpPortUdpServer(0)
{
   InfoLog(<< "UdpServer started.  Listening on " << address.to_string() << ":" << port);

   bind(address, port);
}

UdpServer::~UdpServer()
{
   ResponseMap::iterator it = mResponseMap.begin();
   for(;it != mResponseMap.end(); it++)
   {
      delete it->second;
   }
   mResponseMap.clear();
}

void 
UdpServer::start()
{
   doReceive();
}

void 
UdpServer::setAlternateUdpServers(UdpServer* alternatePort, UdpServer* alternateIp, UdpServer* alternateIpPort)
{
   assert(!mAlternatePortUdpServer);
   assert(!mAlternateIpUdpServer);
   assert(!mAlternateIpPortUdpServer);
   mAlternatePortUdpServer = alternatePort;
   mAlternateIpUdpServer = alternateIp;
   mAlternateIpPortUdpServer = alternateIpPort;
}

bool 
UdpServer::isRFC3489BackwardsCompatServer()
{
   return mAlternatePortUdpServer != 0;  // Just check that any of the alternate servers is populated - if so, we are running in back compat mode
}

asio::ip::udp::socket& 
UdpServer::getSocket()
{
   return mSocket;
}

void 
UdpServer::onReceiveSuccess(const asio::ip::address& address, unsigned short port, boost::shared_ptr<DataBuffer>& data)
{
   if (data->size() > 4)
   {
      /*
      std::cout << "Read " << bytesTransferred << " bytes from udp socket (" << address.to_string() << ":" << port << "): " << std::endl;
      cout << std::hex;
      for(int i = 0; i < data->size(); i++)
      {
         std::cout << (char)(*data)[i] << "(" << int((*data)[i]) << ") ";
      }
      std::cout << std::dec << std::endl;
      */

      if(((*data)[0] & 0xC0) == 0)  // Stun/Turn Messages always have bits 0 and 1 as 00 - otherwise ChannelData message
      {
         // Try to parse stun message
         StunMessage request(StunTuple(StunTuple::UDP, mSocket.local_endpoint().address(), mSocket.local_endpoint().port()),
                             StunTuple(StunTuple::UDP, address, port),
                             (char*)&(*data)[0], data->size());
         if(request.isValid())
         {
            StunMessage* response;
            asio::ip::udp::socket* responseSocket;
            ResponseMap::iterator it = mResponseMap.find(request.mHeader.magicCookieAndTid);
            if(it == mResponseMap.end())
            {
               response = new StunMessage;
               RequestHandler::ProcessResult result = mRequestHandler.processStunMessage(this, request, *response, isRFC3489BackwardsCompatServer());

               switch(result)
               {
               case RequestHandler::NoResponseToSend:
                  // No response to send - just receive next message
                  delete response;
                  doReceive();
                  return;
               case RequestHandler::RespondFromAlternatePort:
                  responseSocket = &mAlternatePortUdpServer->getSocket();
                  break;
               case RequestHandler::RespondFromAlternateIp:
                  responseSocket = &mAlternateIpUdpServer->getSocket();
                  break;
               case RequestHandler::RespondFromAlternateIpPort:
                  responseSocket = &mAlternateIpPortUdpServer->getSocket();
                  break;
               case RequestHandler::RespondFromReceiving:
               default:
                  responseSocket = &mSocket;            
                  break;
               }

               // Store response in Map - to be resent if a retranmission is received
               mResponseMap[response->mHeader.magicCookieAndTid] = new ResponseEntry(this, responseSocket, response);
            }
            else
            {
               InfoLog(<< "UdpServer: received retransmission of request with tid: " << request.mHeader.magicCookieAndTid);
               response = it->second->mResponseMessage;
               responseSocket = it->second->mResponseSocket;
            }

#define RESPONSE_BUFFER_SIZE 1024
            boost::shared_ptr<DataBuffer> buffer = allocateBuffer(RESPONSE_BUFFER_SIZE);
            unsigned int responseSize;
            responseSize = response->stunEncodeMessage((char*)buffer->data(), RESPONSE_BUFFER_SIZE);
            buffer->truncate(responseSize);  // set size to real size

            doSend(response->mRemoteTuple, buffer);
         }            
      }
      else // ChannelData message
      {
         unsigned short channelNumber;
         memcpy(&channelNumber, &(*data)[0], 2);
         channelNumber = ntohs(channelNumber);

         unsigned short dataLen;
         memcpy(&dataLen, &(*data)[2], 2);
         dataLen = ntohs(dataLen);

         // Check if the UDP datagram size is too short to contain the claimed length of the ChannelData message, then discard
         if(data->size() < dataLen + 4)
         {
            WarningLog(<< "ChannelData message size=" << dataLen+4 << " too larger for UDP packet size=" << data->size() <<".  Dropping.");
         }
         else
         {
            mRequestHandler.processTurnData(channelNumber,
                                          StunTuple(StunTuple::UDP, mSocket.local_endpoint().address(), mSocket.local_endpoint().port()),
                                          StunTuple(StunTuple::UDP, address, port),
                                          data);
         }
      }
   }
   else
   {
      WarningLog(<< "Not enough data for stun message or framed message.  Dropping.");
   }

   doReceive();
}

void 
UdpServer::onReceiveFailure(const asio::error_code& e)
{
   if(e != asio::error::operation_aborted)
   {
      InfoLog(<< "UdpServer::onReceiveFailure: " << e.value() << "-" << e.message());

      doReceive();
   }
}

void
UdpServer::onSendSuccess()
{
}

void
UdpServer::onSendFailure(const asio::error_code& error)
{
   if(error != asio::error::operation_aborted)
   {
      InfoLog(<< "UdpServer::onSendFailure: " << error.value() << "-" << error.message());
   }
}

UdpServer::ResponseEntry::ResponseEntry(UdpServer* udpServer, asio::ip::udp::socket* responseSocket, StunMessage* responseMessage) :
   mResponseSocket(responseSocket),
   mResponseMessage(responseMessage),
   mCleanupTimer(udpServer->mIOService)
{
   // start timer
   mCleanupTimer.expires_from_now(boost::posix_time::seconds(10));  // Transaction Responses are cached for 10 seconds
   mCleanupTimer.async_wait(boost::bind(&UdpServer::cleanupResponseMap, udpServer, asio::placeholders::error, responseMessage->mHeader.magicCookieAndTid));
}

UdpServer::ResponseEntry::~ResponseEntry() 
{ 
   delete mResponseMessage; 
}

void 
UdpServer::cleanupResponseMap(const asio::error_code& e, UInt128 tid)
{
   ResponseMap::iterator it = mResponseMap.find(tid);
   if(it != mResponseMap.end())
   {
      DebugLog(<< "UdpServer::cleanupResponseMap - removing transaction id=" << tid);
      delete it->second;
      mResponseMap.erase(it);
   }
}

}


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
