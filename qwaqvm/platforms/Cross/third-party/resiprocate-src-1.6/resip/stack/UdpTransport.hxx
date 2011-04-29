#if !defined(RESIP_UDPTRANSPORT_HXX)
#define RESIP_UDPTRANSPORT_HXX

#include <memory>
#include "resip/stack/InternalTransport.hxx"
#include "resip/stack/MsgHeaderScanner.hxx"
#include "rutil/HeapInstanceCounter.hxx"
#include "resip/stack/Compression.hxx"

namespace osc { class Stack; }

namespace resip
{
class UdpTransport;

/** Interface functor for external unrecognized datagram handling. 
  * User can catch datagram messages recevied that are not recognized by 
  * the stack.
  */ 
class ExternalUnknownDatagramHandler {
public:
   virtual ~ExternalUnknownDatagramHandler(){};
   
   /** .
    * @param transport contains a pointer to the specific UdpTransport object that 
    *                  received the unknown packet.
    * @param unknownDatagram contains the actual contents of unknown data received. */
   virtual void operator()(UdpTransport* transport, const Tuple& source, std::auto_ptr<Data> unknownDatagram) = 0; 
};

class UdpTransport : public InternalTransport
{
public:
   RESIP_HeapCount(UdpTransport);
   // Specify which udp port to use for send and receive
   // interface can be an ip address or dns name. If it is an ip address,
   // only bind to that interface.
   UdpTransport(Fifo<TransactionMessage>& fifo,
                int portNum,
                IpVersion version,
                StunSetting stun,
                const Data& interfaceObj,
                AfterSocketCreationFuncPtr socketFunc = 0,
                Compression &compression = Compression::Disabled);
   virtual  ~UdpTransport();

   void process(FdSet& fdset);
   bool isReliable() const { return false; }
   bool isDatagram() const { return true; }
   TransportType transport() const { return UDP; }
   virtual void buildFdSet( FdSet& fdset);

   static const int MaxBufferSize = 8192;

   // STUN client functionality
   bool stunSendTest(const Tuple& dest);
   bool stunResult(Tuple& mappedAddress); 

   /// Installs a handler for the unknown datagrams arriving on the udp transport.
   void setExternalUnknownDatagramHandler(ExternalUnknownDatagramHandler *handler);

protected:
   osc::Stack *mSigcompStack;

private:
   MsgHeaderScanner mMsgHeaderScanner;
   mutable resip::Mutex  myMutex;
   Tuple mStunMappedAddress;
   bool mStunSuccess;
   ExternalUnknownDatagramHandler* mExternalUnknownDatagramHandler;
};

}

#endif

/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000 Vovida Networks, Inc.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * 3. The names "VOCAL", "Vovida Open Communication Application Library",
 *    and "Vovida Open Communication Application Library (VOCAL)" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact vocal@vovida.org.
 *
 * 4. Products derived from this software may not be called "VOCAL", nor
 *    may "VOCAL" appear in their name, without prior written
 *    permission of Vovida Networks, Inc.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL VOVIDA
 * NETWORKS, INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT DAMAGES
 * IN EXCESS OF $1,000, NOR FOR ANY INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * 
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by Vovida
 * Networks, Inc. and many individuals on behalf of Vovida Networks,
 * Inc.  For more information on Vovida Networks, Inc., please see
 * <http://www.vovida.org/>.
 *
 */
