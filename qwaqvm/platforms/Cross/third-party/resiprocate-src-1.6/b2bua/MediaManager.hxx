
#ifndef __MediaManager_h
#define __MediaManager_h

#include "resip/stack/SdpContents.hxx"
#include "rutil/Data.hxx"

namespace b2bua
{

class MediaManager;

}

#include "B2BCall.hxx"
#include "MediaProxy.hxx"
#include "RtpProxyUtil.hxx"

namespace b2bua
{

/*
  Each B2BCall requires a MediaManager to handle the SDP and media 
  (typically RTP/AVP).

  MediaManager is responsible for:
  - validating the content of the SDP (IP4 only, UDP only, etc)
  - creating and destroying pairs of MediaProxy instances as needed
  - rewriting the SDP connection addresses and port numbers
  - hiding information about the remote party (e.g. phone, web, fax)
*/

// FIXME - use enum types
#define MM_SDP_OK 0			// The SDP was acceptable
#define MM_SDP_BAD 1			// The SDP was not acceptable

class B2BCall;

class MediaManager : public RtpProxyUtil::TimeoutListener {

private:

  static resip::Data proxyAddress;

  B2BCall& b2BCall;

  resip::Data callId;				// from A Leg
  resip::Data fromTag;				// `from' tag for A leg party
  resip::Data toTag;				// `to' tag for B leg party
  resip::SdpContents aLegSdp;
  resip::SdpContents newALegSdp;
  resip::SdpContents bLegSdp;
  resip::SdpContents newBLegSdp;
 
  RtpProxyUtil *rtpProxyUtil;			// Access to rtpproxy

  MediaProxy *aLegProxy;			// Proxies on behalf of A leg
  MediaProxy *bLegProxy;			// Proxies on behalf of B leg

public:
  friend class MediaProxy;

  // Set the proxyAddress
  static void setProxyAddress(const resip::Data& proxyAddress);

  // Instantiate a MediaManager
  MediaManager(B2BCall& b2BCall);
  MediaManager(B2BCall& b2BCall, const resip::Data& callId, const resip::Data& fromTag, const resip::Data& toTag);
  virtual ~MediaManager();

  // Set the From tag
  void setFromTag(const resip::Data& fromTag);
  // Set the To tag
  void setToTag(const resip::Data& toTag);

  // inspect and save an offer (A leg)
  int setALegSdp(const resip::SdpContents& sdp, const in_addr_t& msgSourceAddress);
  // generate an offer (to send to B leg)
  resip::SdpContents& getALegSdp();
  
  // inspect and save an answer (B leg)
  int setBLegSdp(const resip::SdpContents& sdp, const in_addr_t& msgSourceAddress);
  // generate an answer (to send to A leg)
  resip::SdpContents& getBLegSdp();

  void onMediaTimeout();

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

