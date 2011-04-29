


#include "resip/stack/ExtensionHeader.hxx"
#include "resip/stack/SipMessage.hxx"
#include "resip/stack/Tuple.hxx"
#include "rutil/Data.hxx"

#include "DialInstance.hxx"
#include "MyInviteSessionHandler.hxx"

using namespace resip;
using namespace std;

MyInviteSessionHandler::MyInviteSessionHandler(DialInstance& dialInstance) : 
   mDialInstance(dialInstance)
{
}

MyInviteSessionHandler::~MyInviteSessionHandler()
{
}

void MyInviteSessionHandler::onSuccess(ClientRegistrationHandle h, const SipMessage& response) 
{
}

void MyInviteSessionHandler::onFailure(ClientRegistrationHandle, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onMessage(InviteSessionHandle, const resip::SipMessage& msg) 
{
}

void MyInviteSessionHandler::onMessageSuccess(InviteSessionHandle, const resip::SipMessage&) 
{
}

void MyInviteSessionHandler::onMessageFailure(InviteSessionHandle, const resip::SipMessage&) 
{
}

void MyInviteSessionHandler::onFailure(ClientInviteSessionHandle cis, const SipMessage& msg) 
{
   mDialInstance.onFailure();
}

void MyInviteSessionHandler::onForkDestroyed(ClientInviteSessionHandle) 
{
}

void MyInviteSessionHandler::onInfoSuccess(InviteSessionHandle, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onInfoFailure(InviteSessionHandle, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onProvisional(ClientInviteSessionHandle cis, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onConnected(ClientInviteSessionHandle cis, const SipMessage& msg) 
{
   mDialInstance.onConnected(cis);

   SdpContents *sdp = (SdpContents*)msg.getContents();
   cis->provideAnswer(*sdp);
}

void MyInviteSessionHandler::onStaleCallTimeout(ClientInviteSessionHandle) 
{
}

void MyInviteSessionHandler::onConnected(InviteSessionHandle, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onRedirected(ClientInviteSessionHandle, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onAnswer(InviteSessionHandle is, const SipMessage& msg, const SdpContents& sdp) 
{
}

void MyInviteSessionHandler::onEarlyMedia(ClientInviteSessionHandle cis, const SipMessage& msg, const SdpContents& sdp) 
{
}

void MyInviteSessionHandler::onOfferRequired(InviteSessionHandle, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onOfferRejected(InviteSessionHandle, const SipMessage *msg) 
{
}

void MyInviteSessionHandler::onDialogModified(InviteSessionHandle, InviteSession::OfferAnswerType oat, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onInfo(InviteSessionHandle, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onRefer(InviteSessionHandle, ServerSubscriptionHandle, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onReferAccepted(InviteSessionHandle, ClientSubscriptionHandle, const SipMessage& msg) 
{
   mDialInstance.onReferSuccess();
}

void MyInviteSessionHandler::onReferRejected(InviteSessionHandle, const SipMessage& msg) 
{
   mDialInstance.onReferFailed();
}

void MyInviteSessionHandler::onReferNoSub(InviteSessionHandle is, const resip::SipMessage& msg) 
{
}

void MyInviteSessionHandler::onRemoved(ClientRegistrationHandle) 
{
}

int MyInviteSessionHandler::onRequestRetry(ClientRegistrationHandle, int retrySeconds, const SipMessage& response) 
{
  return -1;
}

void MyInviteSessionHandler::onNewSession(ServerInviteSessionHandle sis, InviteSession::OfferAnswerType oat, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onNewSession(ClientInviteSessionHandle cis, InviteSession::OfferAnswerType oat, const SipMessage& msg) 
{
}

void MyInviteSessionHandler::onTerminated(InviteSessionHandle is, InviteSessionHandler::TerminatedReason reason, const SipMessage* msg) 
{
   mDialInstance.onTerminated();
}

void MyInviteSessionHandler::onOffer(InviteSessionHandle is, const SipMessage& msg, const SdpContents& sdp) 
{
}


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

