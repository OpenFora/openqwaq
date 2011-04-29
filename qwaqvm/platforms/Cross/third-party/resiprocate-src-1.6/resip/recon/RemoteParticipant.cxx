#include "ConversationManager.hxx"

#include "sdp/SdpHelperResip.hxx"
#include "sdp/Sdp.hxx"

#include <sdp/SdpCodec.h>  // sipX SdpCodec

#include "RemoteParticipant.hxx"
#include "Conversation.hxx"
#include "UserAgent.hxx"
#include "DtmfEvent.hxx"
#include "ReconSubsystem.hxx"

#include <rutil/Log.hxx>
#include <rutil/Logger.hxx>
#include <rutil/DnsUtil.hxx>
#include <rutil/Random.hxx>
#include <resip/stack/SipFrag.hxx>
#include <resip/stack/ExtensionHeader.hxx>
#include <resip/dum/DialogUsageManager.hxx>
#include <resip/dum/ClientInviteSession.hxx>
#include <resip/dum/ServerInviteSession.hxx>
#include <resip/dum/ClientSubscription.hxx>
#include <resip/dum/ServerOutOfDialogReq.hxx>
#include <resip/dum/ServerSubscription.hxx>

#include <rutil/WinLeakCheck.hxx>

using namespace recon;
using namespace sdpcontainer;
using namespace resip;
using namespace std;

#define RESIPROCATE_SUBSYSTEM ReconSubsystem::RECON

// UAC
RemoteParticipant::RemoteParticipant(ParticipantHandle partHandle,
                                     ConversationManager& conversationManager, 
                                     DialogUsageManager& dum,
                                     RemoteParticipantDialogSet& remoteParticipantDialogSet)
: Participant(partHandle, conversationManager),
  AppDialog(dum),
  mDum(dum),
  mDialogSet(remoteParticipantDialogSet),
  mDialogId(Data::Empty, Data::Empty, Data::Empty),
  mState(Connecting),
  mOfferRequired(false),
  mLocalHold(true),
  mRemoteHold(false),
  mLocalSdp(0),
  mRemoteSdp(0)
{
   InfoLog(<< "RemoteParticipant created (UAC), handle=" << mHandle);
}

// UAS - or forked leg
RemoteParticipant::RemoteParticipant(ConversationManager& conversationManager, 
                                     DialogUsageManager& dum, 
                                     RemoteParticipantDialogSet& remoteParticipantDialogSet)
: Participant(conversationManager),
  AppDialog(dum),
  mDum(dum),
  mDialogSet(remoteParticipantDialogSet),
  mDialogId(Data::Empty, Data::Empty, Data::Empty),
  mState(Connecting),
  mOfferRequired(false),
  mLocalHold(true),
  mLocalSdp(0),
  mRemoteSdp(0)
{
   InfoLog(<< "RemoteParticipant created (UAS or forked leg), handle=" << mHandle);
}

RemoteParticipant::~RemoteParticipant()
{
   if(!mDialogId.getCallId().empty())  
   {
      mDialogSet.removeDialog(mDialogId);
   }
   // unregister from Conversations
   // Note:  ideally this functionality would exist in Participant Base class - but dynamic_cast required in unregisterParticipant will not work
   ConversationMap::iterator it;
   for(it = mConversations.begin(); it != mConversations.end(); it++)
   {
      it->second->unregisterParticipant(this);
   }
   mConversations.clear();

   // Delete Sdp memory
   if(mLocalSdp) delete mLocalSdp;
   if(mRemoteSdp) delete mRemoteSdp;

   InfoLog(<< "RemoteParticipant destroyed, handle=" << mHandle);
}

unsigned int 
RemoteParticipant::getLocalRTPPort()
{
   return mDialogSet.getLocalRTPPort();
}

//static const resip::ExtensionHeader h_AlertInfo("Alert-Info");

void 
RemoteParticipant::initiateRemoteCall(const NameAddr& destination)
{
   SdpContents offer;
   SharedPtr<ConversationProfile> profile = mConversationManager.getUserAgent()->getDefaultOutgoingConversationProfile();
   buildSdpOffer(mLocalHold, offer);
   SharedPtr<SipMessage> invitemsg = mDum.makeInviteSession(
      destination, 
      profile,
      &offer, 
      &mDialogSet);

   mDialogSet.sendInvite(invitemsg);

   // Clear any pending hold/unhold requests since our offer/answer here will handle it
   if(mPendingRequest.mType == Hold ||
      mPendingRequest.mType == Unhold)
   {
      mPendingRequest.mType = None;
   }

   // Adjust RTP streams
   adjustRTPStreams(true);

   // Special case of this call - since call in addToConversation will not work, since we didn't know our bridge port at that time
   mConversationManager.getBridgeMixer().calculateMixWeightsForParticipant(this);
}

int 
RemoteParticipant::getConnectionPortOnBridge()
{
   if(mDialogSet.getActiveRemoteParticipantHandle() == mHandle)
   {
      return mDialogSet.getConnectionPortOnBridge();
   }
   else
   {
      // If this is not active fork leg, then we don't want to effect the bridge mixer.  
      // Note:  All forked endpoints/participants have the same connection port on the bridge
      return -1;
   }
}

int 
RemoteParticipant::getMediaConnectionId() 
{ 
   return mDialogSet.getMediaConnectionId();
}

void 
RemoteParticipant::destroyParticipant()
{
   try
   {
      if(mState != Terminating)
      {
         stateTransition(Terminating);
         if(mInviteSessionHandle.isValid())
         {
            mInviteSessionHandle->end();
         }
         else
         { 
            mDialogSet.end();
         }
      }
   }
   catch(BaseException &e)
   {
      WarningLog(<< "RemoteParticipant::destroyParticipant exception: " << e);
   }
   catch(...)
   {
      WarningLog(<< "RemoteParticipant::destroyParticipant unknown exception");
   }
}

void 
RemoteParticipant::addToConversation(Conversation* conversation, unsigned int inputGain, unsigned int outputGain)
{
   Participant::addToConversation(conversation, inputGain, outputGain);
   if(mLocalHold && !conversation->shouldHold())  // If we are on hold and we now shouldn't be, then unhold
   {
      unhold();
   }
}

void 
RemoteParticipant::removeFromConversation(Conversation *conversation)
{
   Participant::removeFromConversation(conversation);
   checkHoldCondition();
}

void
RemoteParticipant::checkHoldCondition()
{
   // Return to Offer a hold sdp if we are not in any conversations, or all the conversations we are in have conditions such that a hold is required
   bool shouldHold = true;
   ConversationMap::iterator it;
   for(it = mConversations.begin(); it != mConversations.end(); it++)
   {
      if(!it->second->shouldHold())
      {
         shouldHold = false;
         break;
      }
   }
   if(mLocalHold != shouldHold)
   {
      if(shouldHold)
      {
         hold();
      }
      else
      {
         unhold();
      }
   }
}

void 
RemoteParticipant::stateTransition(State state)
{
   Data stateName;

   switch(state)
   {
   case Connecting:
      stateName = "Connecting"; break;
   case Connected:
      stateName = "Connected"; break;
   case Redirecting:
      stateName = "Redirecting"; break;
   case Holding:
      stateName = "Holding"; break;
   case Unholding:
      stateName = "Unholding"; break;
   case Replacing:
      stateName = "Replacing"; break;
   case PendingOODRefer:
      stateName = "PendingOODRefer"; break;
   case Terminating:
      stateName = "Terminating"; break;
   default:
      stateName = "Unknown: " + Data(state); break;
   }
   InfoLog( << "RemoteParticipant::stateTransition of handle=" << mHandle << " to state=" << stateName );
   mState = state;

   if(mState == Connected && mPendingRequest.mType != None)
   {
      PendingRequestType type = mPendingRequest.mType;
      mPendingRequest.mType = None;
      switch(type)
      {
      case Hold:
         hold();
         break;
      case Unhold:
         unhold();
         break;
      case Redirect:
         redirect(mPendingRequest.mDestination);
         break;
      case RedirectTo:
         redirectToParticipant(mPendingRequest.mDestInviteSessionHandle);
         break;
      case None:
         break;
      }
   }
}

void
RemoteParticipant::accept()
{
   try
   {
      // Accept SIP call if required
      if(mState == Connecting && mInviteSessionHandle.isValid())
      {
         ServerInviteSession* sis = dynamic_cast<ServerInviteSession*>(mInviteSessionHandle.get());
         if(sis && !sis->isAccepted())
         { 
            // Clear any pending hold/unhold requests since our offer/answer here will handle it
            if(mPendingRequest.mType == Hold ||
               mPendingRequest.mType == Unhold)
            {
               mPendingRequest.mType = None;
            }
            if(mOfferRequired)
            {
               provideOffer(true /* postOfferAccept */);
            }
            else if(mPendingOffer.get() != 0)
            {
               provideAnswer(*mPendingOffer.get(), true /* postAnswerAccept */, false /* postAnswerAlert */);
            }
            else  
            {
               // It is possible to get here if the app calls alert with early true.  There is special logic in
               // RemoteParticipantDialogSet::accept to handle the case then an alert call followed immediately by 
               // accept.  In this case the answer from the alert will be queued waiting on the flow to be ready, and 
               // we need to ensure the accept call is also delayed until the answer completes.
               mDialogSet.accept(mInviteSessionHandle);
            }
         }
      }
      // Accept Pending OOD Refer if required
      else if(mState == PendingOODRefer)
      {
         acceptPendingOODRefer();
      }
      else
      {
         WarningLog(<< "RemoteParticipant::accept called in invalid state: " << mState);
      }
   }
   catch(BaseException &e)
   {
      WarningLog(<< "RemoteParticipant::accept exception: " << e);
   }
   catch(...)
   {
      WarningLog(<< "RemoteParticipant::accept unknown exception");
   }
}

void 
RemoteParticipant::alert(bool earlyFlag)
{
   try
   {
      if(mState == Connecting && mInviteSessionHandle.isValid())
      {
         ServerInviteSession* sis = dynamic_cast<ServerInviteSession*>(mInviteSessionHandle.get());
         if(sis && !sis->isAccepted())
         {
            if(earlyFlag && mPendingOffer.get() != 0)
            {
               provideAnswer(*mPendingOffer.get(), false /* postAnswerAccept */, true /* postAnswerAlert */);
               mPendingOffer.release();               
            }
            else
            {
               sis->provisional(180, earlyFlag);
            }
         }
      }
      else
      {
         WarningLog(<< "RemoteParticipant::alert called in invalid state: " << mState);
      }
   }
   catch(BaseException &e)
   {
      WarningLog(<< "RemoteParticipant::alert exception: " << e);
   }
   catch(...)
   {
      WarningLog(<< "RemoteParticipant::alert unknown exception");
   }
}

void 
RemoteParticipant::reject(unsigned int rejectCode)
{
   try
   {
      // Reject UAS Invite Session if required
      if(mState == Connecting && mInviteSessionHandle.isValid())
      {
         ServerInviteSession* sis = dynamic_cast<ServerInviteSession*>(mInviteSessionHandle.get());
         if(sis && !sis->isAccepted())
         {
            sis->reject(rejectCode);
         }
      }
      // Reject Pending OOD Refer request if required
      else if(mState == PendingOODRefer)
      {
         rejectPendingOODRefer(rejectCode);
      }
      else
      {
         WarningLog(<< "RemoteParticipant::reject called in invalid state: " << mState);
      }
   }
   catch(BaseException &e)
   {
      WarningLog(<< "RemoteParticipant::reject exception: " << e);
   }
   catch(...)
   {
      WarningLog(<< "RemoteParticipant::reject unknown exception");
   }
}

void
RemoteParticipant::redirect(NameAddr& destination)
{
   try
   {
      if(mPendingRequest.mType == None)
      {
         if((mState == Connecting || mState == Connected) && mInviteSessionHandle.isValid())
         {
            ServerInviteSession* sis = dynamic_cast<ServerInviteSession*>(mInviteSessionHandle.get());
            // If this is a UAS session and we haven't sent a final response yet - then redirect via 302 response
            if(sis && !sis->isAccepted())
            {
               NameAddrs destinations;
               destinations.push_back(destination);
               mConversationManager.onParticipantRedirectSuccess(mHandle);
               sis->redirect(destinations);
            }
            else if(mInviteSessionHandle->isConnected()) // redirect via blind transfer 
            {
               mInviteSessionHandle->refer(destination, true /* refersub */);
               stateTransition(Redirecting);
            }
            else
            {
               mPendingRequest.mType = Redirect;
               mPendingRequest.mDestination = destination;
            }
         }
         else
         {
            mPendingRequest.mType = Redirect;
            mPendingRequest.mDestination = destination;
         }
      }
      else
      {
         WarningLog(<< "RemoteParticipant::redirect error: request pending");
         mConversationManager.onParticipantRedirectFailure(mHandle, 406 /* Not Acceptable */);
      }
   }
   catch(BaseException &e)
   {
      WarningLog(<< "RemoteParticipant::redirect exception: " << e);
   }
   catch(...)
   {
      WarningLog(<< "RemoteParticipant::redirect unknown exception");
   }
}

void
RemoteParticipant::redirectToParticipant(InviteSessionHandle& destParticipantInviteSessionHandle)
{
   try
   {
      if(destParticipantInviteSessionHandle.isValid())
      {
         if(mPendingRequest.mType == None)
         {
            if((mState == Connecting || mState == Connected) && mInviteSessionHandle.isValid())
            {
               ServerInviteSession* sis = dynamic_cast<ServerInviteSession*>(mInviteSessionHandle.get());
               // If this is a UAS session and we haven't sent a final response yet - then redirect via 302 response
               if(sis && !sis->isAccepted())
               {
                  NameAddrs destinations;
                  destinations.push_back(destParticipantInviteSessionHandle->peerAddr());
                  mConversationManager.onParticipantRedirectSuccess(mHandle);
                  sis->redirect(destinations);
               }
               else if(mInviteSessionHandle->isConnected()) // redirect via attended transfer (with replaces)
               {
                  mInviteSessionHandle->refer(NameAddr(destParticipantInviteSessionHandle->peerAddr().uri()) /* remove tags */, destParticipantInviteSessionHandle /* session to replace)  */, true /* refersub */);
                  stateTransition(Redirecting);
               }
               else
               {
                  mPendingRequest.mType = RedirectTo;
                  mPendingRequest.mDestInviteSessionHandle = destParticipantInviteSessionHandle;
               }
            }
            else
            {
               mPendingRequest.mType = RedirectTo;
               mPendingRequest.mDestInviteSessionHandle = destParticipantInviteSessionHandle;
            }
         }
         else
         {
            WarningLog(<< "RemoteParticipant::redirectToParticipant error: request pending");
            mConversationManager.onParticipantRedirectFailure(mHandle, 406 /* Not Acceptable */);
         }
      }
      else
      {
         WarningLog(<< "RemoteParticipant::redirectToParticipant error: destParticipant has no valid InviteSession");
         mConversationManager.onParticipantRedirectFailure(mHandle, 406 /* Not Acceptable */);
      }
   }
   catch(BaseException &e)
   {
      WarningLog(<< "RemoteParticipant::redirectToParticipant exception: " << e);
   }
   catch(...)
   {
      WarningLog(<< "RemoteParticipant::redirectToParticipant unknown exception");
   }
}

void 
RemoteParticipant::hold()
{
   mLocalHold=true;

   InfoLog(<< "RemoteParticipant::hold request: handle=" << mHandle);

   try
   {
      if(mPendingRequest.mType == None)
      {
         if(mState == Connected && mInviteSessionHandle.isValid())
         {
            provideOffer(false /* postOfferAccept */);
            stateTransition(Holding);
         }
         else
         {
            mPendingRequest.mType = Hold;
         }
      }
      else if(mPendingRequest.mType == Unhold)
      {
         mPendingRequest.mType = None;  // Unhold pending, so move to do nothing
         return;
      } 
      else if(mPendingRequest.mType == Hold)
      {
         return;  // Hold already pending
      }
      else
      {
         WarningLog(<< "RemoteParticipant::hold error: request already pending");
      }
   }
   catch(BaseException &e)
   {
      WarningLog(<< "RemoteParticipant::hold exception: " << e);
   }   
   catch(...)
   {
      WarningLog(<< "RemoteParticipant::hold unknown exception");
   }
}

void 
RemoteParticipant::unhold()
{
   mLocalHold=false;

   InfoLog(<< "RemoteParticipant::unhold request: handle=" << mHandle);

   try
   {
      if(mPendingRequest.mType == None)
      {
         if(mState == Connected && mInviteSessionHandle.isValid())
         {
            provideOffer(false /* postOfferAccept */);
            stateTransition(Unholding);
         }
         else
         {
            mPendingRequest.mType = Unhold;
         }
      }
      else if(mPendingRequest.mType == Hold)
      {
         mPendingRequest.mType = None;  // Hold pending, so move do nothing
         return;
      } 
      else if(mPendingRequest.mType == Unhold)
      {
         return;  // Unhold already pending
      }
      else
      {
         WarningLog(<< "RemoteParticipant::unhold error: request already pending");
      }
   }
   catch(BaseException &e)
   {
      WarningLog(<< "RemoteParticipant::unhold exception: " << e);
   }
   catch(...)
   {
      WarningLog(<< "RemoteParticipant::unhold unknown exception");
   }
}

void 
RemoteParticipant::setPendingOODReferInfo(ServerOutOfDialogReqHandle ood, const SipMessage& referMsg)
{
   stateTransition(PendingOODRefer);
   mPendingOODReferMsg = referMsg;
   mPendingOODReferNoSubHandle = ood;
}

void 
RemoteParticipant::setPendingOODReferInfo(ServerSubscriptionHandle ss, const SipMessage& referMsg)
{
   stateTransition(PendingOODRefer);
   mPendingOODReferMsg = referMsg;
   mPendingOODReferSubHandle = ss;
}

void 
RemoteParticipant::acceptPendingOODRefer()
{
   if(mState == PendingOODRefer)
   {
      bool accepted = false;
      if(mPendingOODReferNoSubHandle.isValid())
      {
         mPendingOODReferNoSubHandle->send(mPendingOODReferNoSubHandle->accept(202));  // Accept OOD Refer
         accepted = true;
      }
      else if(mPendingOODReferSubHandle.isValid())
      {
         mPendingOODReferSubHandle->send(mPendingOODReferSubHandle->accept(202));  // Accept OOD Refer
         accepted = true;
      }
      if(accepted)
      {
         // Create offer
         SdpContents offer;
         buildSdpOffer(mLocalHold, offer);

         // Build the Invite
         SharedPtr<SipMessage> invitemsg = mDum.makeInviteSessionFromRefer(mPendingOODReferMsg, 
                                                                           mDialogSet.getUserProfile(),
                                                                           &offer, 
                                                                           &mDialogSet);
         mDialogSet.sendInvite(invitemsg); 

         adjustRTPStreams(true);

         stateTransition(Connecting);
      }
      else
      {
         WarningLog(<< "acceptPendingOODRefer - no valid handles");
         mConversationManager.onParticipantTerminated(mHandle, 500);
         delete this;
      }
   }
}

void 
RemoteParticipant::rejectPendingOODRefer(unsigned int statusCode)
{
   if(mState == PendingOODRefer)
   {
      if(mPendingOODReferNoSubHandle.isValid())
      {
         mPendingOODReferNoSubHandle->send(mPendingOODReferNoSubHandle->reject(statusCode));
         mConversationManager.onParticipantTerminated(mHandle, statusCode);
      }
      else if(mPendingOODReferSubHandle.isValid())
      {
         mPendingOODReferSubHandle->send(mPendingOODReferSubHandle->reject(statusCode));  
         mConversationManager.onParticipantTerminated(mHandle, statusCode);
      }
      else
      {
         WarningLog(<< "rejectPendingOODRefer - no valid handles");
         mConversationManager.onParticipantTerminated(mHandle, 500);
      }
      delete this;
   }
}

void
RemoteParticipant::processReferNotify(const SipMessage& notify)
{
   unsigned int code = 400;  // Bad Request - default if for some reason a valid sipfrag is not present

   SipFrag* frag  = dynamic_cast<SipFrag*>(notify.getContents());
   if (frag)
   {
      // Get StatusCode from SipFrag
      if (frag->message().isResponse())
      {
         code = frag->message().header(h_StatusLine).statusCode();
      }
   }

   // Check if success or failure response code was in SipFrag
   if(code >= 200 && code < 300)
   {
      if(mState == Redirecting)
      {
         if (mHandle) mConversationManager.onParticipantRedirectSuccess(mHandle);
         stateTransition(Connected);
      }
   }
   else if(code >= 300)
   {
      if(mState == Redirecting)
      {
         if (mHandle) mConversationManager.onParticipantRedirectFailure(mHandle, code);
         stateTransition(Connected);
      }
   }
}

void 
RemoteParticipant::provideOffer(bool postOfferAccept)
{
   std::auto_ptr<SdpContents> offer(new SdpContents);
   assert(mInviteSessionHandle.isValid());
   
   buildSdpOffer(mLocalHold, *offer);

   adjustRTPStreams(true);
   mDialogSet.provideOffer(offer, mInviteSessionHandle, postOfferAccept);
   mOfferRequired = false;
}

bool 
RemoteParticipant::provideAnswer(const SdpContents& offer, bool postAnswerAccept, bool postAnswerAlert)
{
   auto_ptr<SdpContents> answer(new SdpContents);
   assert(mInviteSessionHandle.isValid());
   bool answerOk = buildSdpAnswer(offer, *answer);

   if(answerOk)
   {
      adjustRTPStreams();
      mDialogSet.provideAnswer(answer, mInviteSessionHandle, postAnswerAccept, postAnswerAlert);
   }
   else
   {
      mInviteSessionHandle->reject(488);
   }

   return answerOk;
}

void
RemoteParticipant::buildSdpOffer(bool holdSdp, SdpContents& offer)
{
   SdpContents::Session::Medium *audioMedium = 0;
   ConversationProfile *profile = dynamic_cast<ConversationProfile*>(mDialogSet.getUserProfile().get());
   if(!profile) // This can happen for UAC calls
   {
      profile = mConversationManager.getUserAgent()->getDefaultOutgoingConversationProfile().get();
   }

   // If we already have a local sdp for this sesion, then use this to form the next offer - doing so will ensure
   // that we do not switch codecs or payload id's mid session.  
   if(mInviteSessionHandle.isValid() && mInviteSessionHandle->getLocalSdp().session().media().size() != 0)
   {
      offer = mInviteSessionHandle->getLocalSdp();

      // Set sessionid and version for this sdp
      UInt64 currentTime = Timer::getTimeMicroSec();
      offer.session().origin().getSessionId() = currentTime;
      offer.session().origin().getVersion() = currentTime;  

      // Find the audio medium
      for (std::list<SdpContents::Session::Medium>::iterator mediaIt = offer.session().media().begin();
           mediaIt != offer.session().media().end(); mediaIt++)
      {
         if(mediaIt->name() == "audio" && 
            (mediaIt->protocol() == Symbols::RTP_AVP ||
             mediaIt->protocol() == Symbols::RTP_SAVP ||
             mediaIt->protocol() == Symbols::UDP_TLS_RTP_SAVP))
         {
            audioMedium = &(*mediaIt);
            break;
         }
      }
      assert(audioMedium);

      // Add any codecs from our capabilities that may not be in current local sdp - since endpoint may have changed and may now be capable 
      // of handling codecs that it previously could not (common when endpoint is a B2BUA).

      SdpContents& sessionCaps = dynamic_cast<ConversationProfile*>(mDialogSet.getUserProfile().get())->sessionCaps();
      int highPayloadId = 96;  // Note:  static payload id's are in range of 0-96
      // Iterate through codecs in session caps and check if already in offer
      for (std::list<SdpContents::Session::Codec>::iterator codecsIt = sessionCaps.session().media().front().codecs().begin();
           codecsIt != sessionCaps.session().media().front().codecs().end(); codecsIt++)
      {		
         bool found=false;
         bool payloadIdCollision=false;
         for (std::list<SdpContents::Session::Codec>::iterator codecsIt2 = audioMedium->codecs().begin();
              codecsIt2 != audioMedium->codecs().end(); codecsIt2++)
         {
            if(isEqualNoCase(codecsIt->getName(), codecsIt2->getName()) &&
               codecsIt->getRate() == codecsIt2->getRate())
            {
               found = true;
            }
            else if(codecsIt->payloadType() == codecsIt2->payloadType())
            {
               payloadIdCollision = true;
            }
            // Keep track of highest payload id in offer - used if we need to resolve a payload id conflict
            if(codecsIt2->payloadType() > highPayloadId)
            {
               highPayloadId = codecsIt2->payloadType();
            }
         }
         if(!found)
         {
            if(payloadIdCollision)
            {
               highPayloadId++;
               codecsIt->payloadType() = highPayloadId;
            }
            else if(codecsIt->payloadType() > highPayloadId)
            {
               highPayloadId = codecsIt->payloadType();
            }
            audioMedium->addCodec(*codecsIt);
         }
      }
   }
   else
   {
      // Build base offer
      mConversationManager.buildSdpOffer(profile, offer);

      // Assumes there is only 1 media stream in session caps and it the audio one
      audioMedium = &offer.session().media().front();
      assert(audioMedium);

      // Set the local RTP Port
      audioMedium->port() = mDialogSet.getLocalRTPPort();
   }

   // Add Crypto attributes (if required) - assumes there is only 1 media stream
   audioMedium->clearAttribute("crypto");
   audioMedium->clearAttribute("encryption");
   audioMedium->clearAttribute("tcap");
   audioMedium->clearAttribute("pcfg");
   offer.session().clearAttribute("fingerprint");
   offer.session().clearAttribute("setup");
   if(mDialogSet.getSecureMediaMode() == ConversationProfile::Srtp)
   {
      // Note:  We could add the crypto attribute to the "SDP Capabilties Negotiation" 
      //        potential configuration if secure media is not required - but other implementations 
      //        should ignore them any way if just plain RTP is used.  It is thought the 
      //        current implementation will increase interopability. (ie. SNOM Phones)

      Data crypto;

      switch(mDialogSet.getSrtpCryptoSuite())
      {
      case flowmanager::MediaStream::SRTP_AES_CM_128_HMAC_SHA1_32:
         crypto = "1 AES_CM_128_HMAC_SHA1_32 inline:" + mDialogSet.getLocalSrtpSessionKey().base64encode();  
         audioMedium->addAttribute("crypto", crypto);
         crypto = "2 AES_CM_128_HMAC_SHA1_80 inline:" + mDialogSet.getLocalSrtpSessionKey().base64encode();
         audioMedium->addAttribute("crypto", crypto);
         break;
      default:
         crypto = "1 AES_CM_128_HMAC_SHA1_80 inline:" + mDialogSet.getLocalSrtpSessionKey().base64encode();
         audioMedium->addAttribute("crypto", crypto);
         crypto = "2 AES_CM_128_HMAC_SHA1_32 inline:" + mDialogSet.getLocalSrtpSessionKey().base64encode();
         audioMedium->addAttribute("crypto", crypto);
         break;
      }
      if(mDialogSet.getSecureMediaRequired())
      {
         audioMedium->protocol() = Symbols::RTP_SAVP;
      }
      else
      {
         audioMedium->protocol() = Symbols::RTP_AVP;
         audioMedium->addAttribute("encryption", "optional");  // Used by SNOM phones?
         audioMedium->addAttribute("tcap", "1 RTP/SAVP");      // draft-ietf-mmusic-sdp-capability-negotiation-08
         audioMedium->addAttribute("pcfg", "1 t=1");
      }
   }
   else if(mDialogSet.getSecureMediaMode() == ConversationProfile::SrtpDtls)
   {
      if(mConversationManager.getFlowManager().getDtlsFactory())
      {
         // Note:  We could add the fingerprint and setup attributes to the "SDP Capabilties Negotiation" 
         //        potential configuration if secure media is not required - but other implementations 
         //        should ignore them any way if just plain RTP is used.  It is thought the 
         //        current implementation will increase interopability.

         // Add fingerprint attribute
         char fingerprint[100];
         mConversationManager.getFlowManager().getDtlsFactory()->getMyCertFingerprint(fingerprint);
         offer.session().addAttribute("fingerprint", "SHA-1 " + Data(fingerprint));
         //offer.session().addAttribute("acap", "1 fingerprint:SHA-1 " + Data(fingerprint));

         // Add setup attribute
         offer.session().addAttribute("setup", "actpass"); 

         if(mDialogSet.getSecureMediaRequired())
         {
            audioMedium->protocol() = Symbols::UDP_TLS_RTP_SAVP;
         }
         else
         {
            audioMedium->protocol() = Symbols::RTP_AVP;
            audioMedium->addAttribute("tcap", "1 UDP/TLS/RTP/SAVP");      // draft-ietf-mmusic-sdp-capability-negotiation-08
            audioMedium->addAttribute("pcfg", "1 t=1");
            //audioMedium->addAttribute("pcfg", "1 t=1 a=1");
         }
      }
   }

   audioMedium->clearAttribute("sendrecv");
   audioMedium->clearAttribute("sendonly");
   audioMedium->clearAttribute("recvonly");
   audioMedium->clearAttribute("inactive");

   if(holdSdp)
   {
      if(mRemoteHold)
      {
         audioMedium->addAttribute("inactive");
      }
      else
      {
         audioMedium->addAttribute("sendonly");
      }
   }
   else
   {
      if(mRemoteHold)
      {
         audioMedium->addAttribute("recvonly");
      }
      else
      {
         audioMedium->addAttribute("sendrecv");
      }
   }
   setProposedSdp(offer);
}

bool
RemoteParticipant::answerMediaLine(SdpContents::Session::Medium& mediaSessionCaps, const SdpMediaLine& sdpMediaLine, SdpContents& answer, bool potential)
{
   SdpMediaLine::SdpTransportProtocolType protocolType = sdpMediaLine.getTransportProtocolType();
   bool valid = false;

   // If this is a valid audio medium then process it
   if(sdpMediaLine.getMediaType() == SdpMediaLine::MEDIA_TYPE_AUDIO && 
      (protocolType == SdpMediaLine::PROTOCOL_TYPE_RTP_AVP ||
       protocolType == SdpMediaLine::PROTOCOL_TYPE_RTP_SAVP ||
       protocolType == SdpMediaLine::PROTOCOL_TYPE_UDP_TLS_RTP_SAVP) && 
      sdpMediaLine.getConnections().size() != 0 &&
      sdpMediaLine.getConnections().front().getPort() != 0)
   {
      SdpContents::Session::Medium medium("audio", getLocalRTPPort(), 1, 
                                          protocolType == SdpMediaLine::PROTOCOL_TYPE_RTP_SAVP ? Symbols::RTP_SAVP :
                                          (protocolType == SdpMediaLine::PROTOCOL_TYPE_UDP_TLS_RTP_SAVP ? Symbols::UDP_TLS_RTP_SAVP :
                                           Symbols::RTP_AVP));

      // Check secure media properties and requirements
      bool secureMediaRequired = mDialogSet.getSecureMediaRequired() || protocolType != SdpMediaLine::PROTOCOL_TYPE_RTP_AVP;

      if(mDialogSet.getSecureMediaMode() == ConversationProfile::Srtp || 
         protocolType == SdpMediaLine::PROTOCOL_TYPE_RTP_SAVP)  // allow accepting of SAVP profiles, even if SRTP is not enabled as a SecureMedia mode
      {
         bool supportedCryptoSuite = false;
         SdpMediaLine::CryptoList::const_iterator itCrypto = sdpMediaLine.getCryptos().begin();
         for(; !supportedCryptoSuite && itCrypto!=sdpMediaLine.getCryptos().end(); itCrypto++)
         {
            Data cryptoKeyB64(itCrypto->getCryptoKeyParams().front().getKeyValue());
            Data cryptoKey = cryptoKeyB64.base64decode();
                  
            if(cryptoKey.size() == SRTP_MASTER_KEY_LEN)
            {
               switch(itCrypto->getSuite())
               {
               case SdpMediaLine::CRYPTO_SUITE_TYPE_AES_CM_128_HMAC_SHA1_80:   
                  medium.addAttribute("crypto", Data(itCrypto->getTag()) + " AES_CM_128_HMAC_SHA1_80 inline:" + mDialogSet.getLocalSrtpSessionKey().base64encode());
                  supportedCryptoSuite = true;
                  break;
               case SdpMediaLine::CRYPTO_SUITE_TYPE_AES_CM_128_HMAC_SHA1_32:
                  medium.addAttribute("crypto", Data(itCrypto->getTag()) + " AES_CM_128_HMAC_SHA1_32 inline:" + mDialogSet.getLocalSrtpSessionKey().base64encode());
                  supportedCryptoSuite = true;
                  break;
               default:
                  break;
               }
            }
            else
            {
               InfoLog(<< "SDES crypto key found in SDP, but is not of correct length after base 64 decode: " << cryptoKey.size());
            }
         }
         if(!supportedCryptoSuite && secureMediaRequired)
         {
            InfoLog(<< "Secure media stream is required, but there is no supported crypto attributes in the offer - skipping this stream...");
            return false;
         }
      }
      else if(mConversationManager.getFlowManager().getDtlsFactory() &&
              (mDialogSet.getSecureMediaMode() == ConversationProfile::SrtpDtls || 
               protocolType == SdpMediaLine::PROTOCOL_TYPE_UDP_TLS_RTP_SAVP))  // allow accepting of DTLS SAVP profiles, even if DTLS-SRTP is not enabled as a SecureMedia mode
      {
         bool supportedFingerprint = false;

         // We will only process Dtls-Srtp if fingerprint is in SHA-1 format
         if(sdpMediaLine.getFingerPrintHashFunction() == SdpMediaLine::FINGERPRINT_HASH_FUNC_SHA_1)
         {
            answer.session().clearAttribute("fingerprint");  // ensure we don't add these twice
            answer.session().clearAttribute("setup");  // ensure we don't add these twice

            // Add fingerprint attribute to answer
            char fingerprint[100];
            mConversationManager.getFlowManager().getDtlsFactory()->getMyCertFingerprint(fingerprint);                        
            answer.session().addAttribute("fingerprint", "SHA-1 " + Data(fingerprint));

            // Add setup attribute
            if(sdpMediaLine.getTcpSetupAttribute() == SdpMediaLine::TCP_SETUP_ATTRIBUTE_ACTIVE)
            {
               answer.session().addAttribute("setup", "passive");
            }
            else
            {
               answer.session().addAttribute("setup", "active");
            }

            supportedFingerprint = true;
         }         
         if(!supportedFingerprint && secureMediaRequired)
         {
            InfoLog(<< "Secure media stream is required, but there is no supported fingerprint attributes in the offer - skipping this stream...");
            return false;
         }
      }

      if(potential && !sdpMediaLine.getPotentialMediaViewString().empty())
      {
         medium.addAttribute("acfg", sdpMediaLine.getPotentialMediaViewString());
      }
      
      // Iterate through codecs and look for supported codecs - tag found ones by storing their payload id
      SdpMediaLine::CodecList::const_iterator itCodec = sdpMediaLine.getCodecs().begin();
      for(; itCodec != sdpMediaLine.getCodecs().end(); itCodec++)
      {
         std::list<SdpContents::Session::Codec>::iterator bestCapsCodecMatchIt = mediaSessionCaps.codecs().end();
         bool modeInOffer = itCodec->getFormatParameters().prefix("mode=");

         // Loop through allowed codec list and see if codec is supported locally
         for (std::list<SdpContents::Session::Codec>::iterator capsCodecsIt = mediaSessionCaps.codecs().begin();
              capsCodecsIt != mediaSessionCaps.codecs().end(); capsCodecsIt++)
         {
            if(isEqualNoCase(capsCodecsIt->getName(), itCodec->getMimeSubtype()) &&
               (unsigned int)capsCodecsIt->getRate() == itCodec->getRate())
            {
               bool modeInCaps = capsCodecsIt->parameters().prefix("mode=");
               if(!modeInOffer && !modeInCaps)
               {
                  // If mode is not specified in either - then we have a match
                  bestCapsCodecMatchIt = capsCodecsIt;
                  break;
               }
               else if(modeInOffer && modeInCaps)
               {
                  if(isEqualNoCase(capsCodecsIt->parameters(), itCodec->getFormatParameters()))
                  {
                     bestCapsCodecMatchIt = capsCodecsIt;
                     break;
                  }
                  // If mode is specified in both, and doesn't match - then we have no match
               }
               else
               {
                  // Mode is specified on either offer or caps - this match is a potential candidate
                  // As a rule - use first match of this kind only
                  if(bestCapsCodecMatchIt == mediaSessionCaps.codecs().end())
                  {
                     bestCapsCodecMatchIt = capsCodecsIt;
                  }
               }
            }
         } 

         if(bestCapsCodecMatchIt != mediaSessionCaps.codecs().end())
         {
            SdpContents::Session::Codec codec(*bestCapsCodecMatchIt);
            codec.payloadType() = itCodec->getPayloadType();  // honour offered payload id - just to be nice  :)
            medium.addCodec(codec);
            if(!valid && !isEqualNoCase(bestCapsCodecMatchIt->getName(), "telephone-event"))
            {
               // Consider offer valid if we see any matching codec other than telephone-event
               valid = true;
            }
         }
      }
      
      if(valid)
      {
         // copy ptime attribute from session caps (if exists)
         if(mediaSessionCaps.exists("ptime"))
         {
            medium.addAttribute("ptime", mediaSessionCaps.getValues("ptime").front());
         }

         // Check requested direction
         unsigned int remoteRtpPort = sdpMediaLine.getConnections().front().getPort();
         if(sdpMediaLine.getDirection() == SdpMediaLine::DIRECTION_TYPE_INACTIVE || 
           (mLocalHold && (sdpMediaLine.getDirection() == SdpMediaLine::DIRECTION_TYPE_SENDONLY || remoteRtpPort == 0)))  // If remote inactive or both sides are holding
         {
            medium.addAttribute("inactive");
         }
         else if(sdpMediaLine.getDirection() == SdpMediaLine::DIRECTION_TYPE_SENDONLY || remoteRtpPort == 0 /* old RFC 2543 hold */)
         {
            medium.addAttribute("recvonly");
         }
         else if(sdpMediaLine.getDirection() == SdpMediaLine::DIRECTION_TYPE_RECVONLY || mLocalHold)
         {
            medium.addAttribute("sendonly");
         }
         else
         {
            // Note:  sendrecv is the default in SDP
            medium.addAttribute("sendrecv");
         }
         answer.session().addMedium(medium);
      }
   }
   return valid;
}

bool
RemoteParticipant::buildSdpAnswer(const SdpContents& offer, SdpContents& answer)
{
   // Note: this implementation has minimal support for draft-ietf-mmusic-sdp-capabilities-negotiation
   //       for responding "best-effort" / optional SRTP (Dtls-SRTP) offers

   bool valid = false;
   Sdp* remoteSdp = SdpHelperResip::createSdpFromResipSdp(offer);

   try
   {
      // copy over session capabilities
      answer = dynamic_cast<ConversationProfile*>(mDialogSet.getUserProfile().get())->sessionCaps();

      // Set sessionid and version for this answer
      UInt64 currentTime = Timer::getTimeMicroSec();
      answer.session().origin().getSessionId() = currentTime;
      answer.session().origin().getVersion() = currentTime;  

      // Set local port in answer
      // for now we only allow 1 audio media
      assert(answer.session().media().size() == 1);
      SdpContents::Session::Medium& mediaSessionCaps = dynamic_cast<ConversationProfile*>(mDialogSet.getUserProfile().get())->sessionCaps().session().media().front();
      assert(mediaSessionCaps.name() == "audio");
      assert(mediaSessionCaps.codecs().size() > 0);

      // Copy t= field from sdp (RFC3264)
      assert(answer.session().getTimes().size() > 0);
      if(offer.session().getTimes().size() >= 1)
      {
         answer.session().getTimes().clear();
         answer.session().addTime(offer.session().getTimes().front());
      }

      // Clear out m= lines in answer then populate below
      answer.session().media().clear();

      // Loop through each offered m= line and provide a response
      Sdp::MediaLineList::const_iterator itMediaLine = remoteSdp->getMediaLines().begin();
      for(; itMediaLine != remoteSdp->getMediaLines().end(); itMediaLine++)
      {
         bool mediaLineValid = false;

         // We only process one media stream - so if we already have a valid - just reject the rest
         if(valid)
         {
            SdpContents::Session::Medium rejmedium((*itMediaLine)->getMediaTypeString(), 0, 1,  // Reject medium by specifying port 0 (RFC3264)	
                                                   (*itMediaLine)->getTransportProtocolTypeString());
            answer.session().addMedium(rejmedium);
            continue;
         }

         // Give preference to potential configuration first - if there are any
         SdpMediaLine::SdpMediaLineList::const_iterator itPotentialMediaLine = (*itMediaLine)->getPotentialMediaViews().begin();
         for(; itPotentialMediaLine != (*itMediaLine)->getPotentialMediaViews().end(); itPotentialMediaLine++)
         {
            mediaLineValid = answerMediaLine(mediaSessionCaps, *itPotentialMediaLine, answer, true);
            if(mediaLineValid)
            {
               // We have a valid potential media - line - copy over normal media line to make 
               // further processing easier
               *(*itMediaLine) = *itPotentialMediaLine;  
               valid = true;
               break;
            }
         }         
         if(!mediaLineValid) 
         {
            // Process SDP normally
            mediaLineValid = answerMediaLine(mediaSessionCaps, *(*itMediaLine), answer, false);
            if(!mediaLineValid)
            {
               SdpContents::Session::Medium rejmedium((*itMediaLine)->getMediaTypeString(), 0, 1,  // Reject medium by specifying port 0 (RFC3264)	
                                                      (*itMediaLine)->getTransportProtocolTypeString());
               answer.session().addMedium(rejmedium);
            }
            else
            {
               valid = true;
            }
         }
      }  // end loop through m= offers
   }
   catch(BaseException &e)
   {
      WarningLog( << "buildSdpAnswer: exception parsing SDP offer: " << e.getMessage());
      valid = false;
   }
   catch(...)
   {
      WarningLog( << "buildSdpAnswer: unknown exception parsing SDP offer");
      valid = false;
   }

   //InfoLog( << "SDPOffer: " << offer);
   //InfoLog( << "SDPAnswer: " << answer);
   if(valid)
   {
      setLocalSdp(answer);
      setRemoteSdp(offer, remoteSdp);
   }
   else
   {
      delete remoteSdp;
   }
   return valid;
}

#ifdef OLD_CODE
// Note:  This old code used to serve 2 purposes - 
// 1 - that we do not change payload id's mid session
// 2 - that we do not add codecs or media streams that have previously rejected
// Purpose 2 is not correct.  RFC3264 states we need purpose 1, but you are allowed to add new codecs mid-session
//
// Decision to comment out this code and implement purpose 1 elsewhere - leaving this code here for reference (for now)
// as it may be useful for something in the future.
bool
RemoteParticipant::formMidDialogSdpOfferOrAnswer(const SdpContents& localSdp, const SdpContents& remoteSdp, SdpContents& newSdp, bool offer)
{
   bool valid = false;

   try
   {
      // start with current localSdp
      newSdp = localSdp;

      // Clear all m= lines are rebuild
      newSdp.session().media().clear();

      // Set sessionid and version for this sdp
      UInt64 currentTime = Timer::getTimeMicroSec();
      newSdp.session().origin().getSessionId() = currentTime;
      newSdp.session().origin().getVersion() = currentTime;  

      // Loop through each m= line in local Sdp and remove or disable if not in remote
      for (std::list<SdpContents::Session::Medium>::const_iterator localMediaIt = localSdp.session().media().begin();
           localMediaIt != localSdp.session().media().end(); localMediaIt++)
      {
         for (std::list<SdpContents::Session::Medium>::const_iterator remoteMediaIt = remoteSdp.session().media().begin();
              remoteMediaIt != remoteSdp.session().media().end(); remoteMediaIt++)
         {
            if(localMediaIt->name() == remoteMediaIt->name() && localMediaIt->protocol() == remoteMediaIt->protocol())
            {
               // Found an m= line match, proceed to process codecs
               SdpContents::Session::Medium medium(localMediaIt->name(), localMediaIt->port(), localMediaIt->multicast(), localMediaIt->protocol());

               // Iterate through local codecs and look for remote supported codecs
               for (std::list<SdpContents::Session::Codec>::const_iterator localCodecsIt = localMediaIt->codecs().begin();
                    localCodecsIt != localMediaIt->codecs().end(); localCodecsIt++)
               {						
                  // Loop through remote supported codec list and see if codec is supported
                  for (std::list<SdpContents::Session::Codec>::const_iterator remoteCodecsIt = remoteMediaIt->codecs().begin();
                       remoteCodecsIt != remoteMediaIt->codecs().end(); remoteCodecsIt++)
                  {
                     if(isEqualNoCase(localCodecsIt->getName(), remoteCodecsIt->getName()) &&
                        localCodecsIt->getRate() == remoteCodecsIt->getRate())
                     {
                        // matching supported codec found - add to newSdp
                        medium.addCodec(*localCodecsIt);
                        if(!valid && !isEqualNoCase(localCodecsIt->getName(), "telephone-event"))
                        {
                           // Consider valid if we see any matching codec other than telephone-event
                           valid = true;
                        }
                        break;
                     }
                  }
               }

               // copy ptime attribute from session caps (if exists)
               if(localMediaIt->exists("ptime"))
               {
                  medium.addAttribute("ptime", localMediaIt->getValues("ptime").front());
               }

               if(offer)
               {
                  if(mLocalHold)
                  {
                     if(remoteMediaIt->exists("inactive") || 
                        remoteMediaIt->exists("sendonly") || 
                        remoteMediaIt->port() == 0)  // If remote inactive or both sides are holding
                     {
                        medium.addAttribute("inactive");
                     }
                     else
                     {
                        medium.addAttribute("sendonly");
                     }
                  }
                  else
                  {
                     if(remoteMediaIt->exists("inactive") || remoteMediaIt->exists("sendonly") || remoteMediaIt->port() == 0 /* old RFC 2543 hold */)
                     {
                        medium.addAttribute("recvonly");
                     }
                     else
                     {
                        medium.addAttribute("sendrecv");
                     }
                  }
               }
               else  // This is an sdp answer
               {
                  // Check requested direction
                  if(remoteMediaIt->exists("inactive") || 
                     (mLocalHold && (remoteMediaIt->exists("sendonly") || remoteMediaIt->port() == 0)))  // If remote inactive or both sides are holding
                  {
                     medium.addAttribute("inactive");
                  }
                  else if(remoteMediaIt->exists("sendonly") || remoteMediaIt->port() == 0 /* old RFC 2543 hold */)
                  {
                     medium.addAttribute("recvonly");
                  }
                  else if(remoteMediaIt->exists("recvonly") || mLocalHold)
                  {
                     medium.addAttribute("sendonly");
                  }
                  else
                  {
                     // Note:  sendrecv is the default in SDP
                     medium.addAttribute("sendrecv");
                  }
               }

               newSdp.session().addMedium(medium);
               break;
            }
         }
      }
   }
   catch(BaseException &e)
   {
      WarningLog( << "formMidDialogSdpOfferOrAnswer: exception: " << e.getMessage());
      valid = false;
   }
   catch(...)
   {
      WarningLog( << "formMidDialogSdpOfferOrAnswer: unknown exception");
      valid = false;
   }

   return valid;
}
#endif

void 
RemoteParticipant::destroyConversations()
{
   ConversationMap temp = mConversations;  // Copy since we may end up being destroyed
   ConversationMap::iterator it;
   for(it = temp.begin(); it != temp.end(); it++)
   {
      it->second->destroy();
   }
}

void 
RemoteParticipant::setProposedSdp(const resip::SdpContents& sdp)
{
   mDialogSet.setProposedSdp(mHandle, sdp);
}

void 
RemoteParticipant::setLocalSdp(const resip::SdpContents& sdp)
{
   if(mLocalSdp) delete mLocalSdp;
   mLocalSdp = 0;
   InfoLog(<< "setLocalSdp: handle=" << mHandle << ", localSdp=" << sdp);
   mLocalSdp = SdpHelperResip::createSdpFromResipSdp(sdp);
}

void 
RemoteParticipant::setRemoteSdp(const resip::SdpContents& sdp, bool answer)
{
   if(mRemoteSdp) delete mRemoteSdp;
   mRemoteSdp = 0;
   InfoLog(<< "setRemoteSdp: handle=" << mHandle << ", remoteSdp=" << sdp);
   mRemoteSdp = SdpHelperResip::createSdpFromResipSdp(sdp);
   if(answer && mDialogSet.getProposedSdp())
   {
      if(mLocalSdp) delete mLocalSdp;
      mLocalSdp = new sdpcontainer::Sdp(*mDialogSet.getProposedSdp());  // copied
   }
}

void 
RemoteParticipant::setRemoteSdp(const resip::SdpContents& sdp, Sdp* remoteSdp) // Note: sdp only passed for logging purposes
{
   if(mRemoteSdp) delete mRemoteSdp;
   InfoLog(<< "setRemoteSdp: handle=" << mHandle << ", remoteSdp=" << sdp);
   mRemoteSdp = remoteSdp;
}

void
RemoteParticipant::adjustRTPStreams(bool sendingOffer)
{
   //if(mHandle) mConversationManager.onParticipantMediaUpdate(mHandle, localSdp, remoteSdp);   
   int mediaDirection = SdpMediaLine::DIRECTION_TYPE_INACTIVE;
   Data remoteIPAddress;
   unsigned int remoteRtpPort=0;
   unsigned int remoteRtcpPort=0;
   Sdp *localSdp = sendingOffer ? mDialogSet.getProposedSdp() : mLocalSdp;
   Sdp *remoteSdp = sendingOffer ? 0 : mRemoteSdp;
   const SdpMediaLine::CodecList* localCodecs;
   const SdpMediaLine::CodecList* remoteCodecs;
   bool supportedCryptoSuite = false;
   bool supportedFingerprint = false;

   assert(localSdp);

   /*
   InfoLog(<< "adjustRTPStreams: handle=" << mHandle << ", localSdp=" << localSdp);
   if(remoteSdp)
   {
      InfoLog(<< "adjustRTPStreams: handle=" << mHandle << ", remoteSdp=" << *remoteSdp);
   }*/

   int localMediaDirection = SdpMediaLine::DIRECTION_TYPE_INACTIVE;

   Sdp::MediaLineList::const_iterator itMediaLine = localSdp->getMediaLines().begin();
   for(; itMediaLine != localSdp->getMediaLines().end(); itMediaLine++)
   {
      DebugLog(<< "adjustRTPStreams: handle=" << mHandle << ", found media line in local sdp, mediaType=" << (*itMediaLine)->getMediaType() << 
                 ", transportType=" << (*itMediaLine)->getTransportProtocolType() << ", numConnections=" << (*itMediaLine)->getConnections().size() <<
                 ", port=" << ((*itMediaLine)->getConnections().size() > 0 ? (*itMediaLine)->getConnections().front().getPort() : 0));
      if((*itMediaLine)->getMediaType() == SdpMediaLine::MEDIA_TYPE_AUDIO && 
         ((*itMediaLine)->getTransportProtocolType() == SdpMediaLine::PROTOCOL_TYPE_RTP_AVP ||
          (*itMediaLine)->getTransportProtocolType() == SdpMediaLine::PROTOCOL_TYPE_RTP_SAVP ||
          (*itMediaLine)->getTransportProtocolType() == SdpMediaLine::PROTOCOL_TYPE_UDP_TLS_RTP_SAVP) && 
         (*itMediaLine)->getConnections().size() != 0 &&
         (*itMediaLine)->getConnections().front().getPort() != 0)
      {
         //InfoLog(<< "adjustRTPStreams: handle=" << mHandle << ", found audio media line in local sdp");
         localMediaDirection = (*itMediaLine)->getDirection();
         localCodecs = &(*itMediaLine)->getCodecs();
         break;
      }
   }
 
   if(remoteSdp)
   {
      int remoteMediaDirection = SdpMediaLine::DIRECTION_TYPE_INACTIVE;

      Sdp::MediaLineList::const_iterator itRemMediaLine = remoteSdp->getMediaLines().begin();
      for(; itRemMediaLine != remoteSdp->getMediaLines().end(); itRemMediaLine++)
      {
         //InfoLog(<< "adjustRTPStreams: handle=" << mHandle << ", found media line in remote sdp");
         if((*itRemMediaLine)->getMediaType() == SdpMediaLine::MEDIA_TYPE_AUDIO && 
            ((*itRemMediaLine)->getTransportProtocolType() == SdpMediaLine::PROTOCOL_TYPE_RTP_AVP ||
             (*itRemMediaLine)->getTransportProtocolType() == SdpMediaLine::PROTOCOL_TYPE_RTP_SAVP ||
             (*itRemMediaLine)->getTransportProtocolType() == SdpMediaLine::PROTOCOL_TYPE_UDP_TLS_RTP_SAVP) && 
            (*itRemMediaLine)->getConnections().size() != 0 &&
            (*itRemMediaLine)->getConnections().front().getPort() != 0)
         {
            //InfoLog(<< "adjustRTPStreams: handle=" << mHandle << ", found audio media line in remote sdp");
            remoteMediaDirection = (*itRemMediaLine)->getDirection();
            remoteRtpPort = (*itRemMediaLine)->getConnections().front().getPort();
            remoteRtcpPort = (*itRemMediaLine)->getRtcpConnections().front().getPort();
            remoteIPAddress = (*itRemMediaLine)->getConnections().front().getAddress();
            remoteCodecs = &(*itRemMediaLine)->getCodecs();

            // Process Crypto settings (if required) - createSRTPSession using remote key
            // Note:  Top crypto in remote sdp will always be the correct suite/key
            if(mDialogSet.getSecureMediaMode() == ConversationProfile::Srtp || 
               (*itRemMediaLine)->getTransportProtocolType() == SdpMediaLine::PROTOCOL_TYPE_RTP_SAVP)
            {
               SdpMediaLine::CryptoList::const_iterator itCrypto = (*itRemMediaLine)->getCryptos().begin();
               for(; itCrypto != (*itRemMediaLine)->getCryptos().end(); itCrypto++)
               {
                  Data cryptoKeyB64(itCrypto->getCryptoKeyParams().front().getKeyValue());
                  Data cryptoKey = cryptoKeyB64.base64decode();
                  
                  if(cryptoKey.size() == SRTP_MASTER_KEY_LEN)
                  {
                     switch(itCrypto->getSuite())
                     {
                     case SdpMediaLine::CRYPTO_SUITE_TYPE_AES_CM_128_HMAC_SHA1_80:   
                        mDialogSet.createSRTPSession(flowmanager::MediaStream::SRTP_AES_CM_128_HMAC_SHA1_80, cryptoKey.data(), cryptoKey.size());
                        supportedCryptoSuite = true;
                        break;
                     case SdpMediaLine::CRYPTO_SUITE_TYPE_AES_CM_128_HMAC_SHA1_32:
                        mDialogSet.createSRTPSession(flowmanager::MediaStream::SRTP_AES_CM_128_HMAC_SHA1_32, cryptoKey.data(), cryptoKey.size());
                        supportedCryptoSuite = true;
                        break;
                     default:
                        break;
                     }
                  }
                  else
                  {
                     InfoLog(<< "SDES crypto key found in SDP, but is not of correct length after base 64 decode: " << cryptoKey.size());
                  }
                  if(supportedCryptoSuite)
                  {
                     break;
                  }
               }
            }
            // Process Fingerprint and setup settings (if required) 
            else if((*itRemMediaLine)->getTransportProtocolType() == SdpMediaLine::PROTOCOL_TYPE_UDP_TLS_RTP_SAVP)
            {
               // We will only process Dtls-Srtp if fingerprint is in SHA-1 format
               if((*itRemMediaLine)->getFingerPrintHashFunction() == SdpMediaLine::FINGERPRINT_HASH_FUNC_SHA_1)
               {
                  if(!(*itRemMediaLine)->getFingerPrint().empty())
                  {
                     InfoLog(<< "Fingerprint retrieved from remote SDP: " << (*itRemMediaLine)->getFingerPrint());
                     // ensure we only accept media streams with this fingerprint
                     mDialogSet.setRemoteSDPFingerprint((*itRemMediaLine)->getFingerPrint());

                     // If remote setup value is not active then we must be the Dtls client  - ensure client DtlsSocket is create
                     if((*itRemMediaLine)->getTcpSetupAttribute() != SdpMediaLine::TCP_SETUP_ATTRIBUTE_ACTIVE)
                     {
                        // If we are the active end, then kick start the DTLS handshake
                        mDialogSet.startDtlsClient(remoteIPAddress.c_str(), remoteRtpPort, remoteRtcpPort);
                     }

                     supportedFingerprint = true;
                  }
               }
               else if((*itRemMediaLine)->getFingerPrintHashFunction() != SdpMediaLine::FINGERPRINT_HASH_FUNC_NONE)
               {
                  InfoLog(<< "Fingerprint found, but is not using SHA-1 hash.");
               }
            }

            break;
         }
      }

	  if(remoteMediaDirection == SdpMediaLine::DIRECTION_TYPE_INACTIVE ||
	     remoteMediaDirection == SdpMediaLine::DIRECTION_TYPE_SENDONLY)
	  {
		  mRemoteHold = true;
	  }
	  else
	  {
		  mRemoteHold = false;
	  }
      // Aggregate local and remote direction attributes to determine overall media direction
      if(mLocalHold ||
         localMediaDirection == SdpMediaLine::DIRECTION_TYPE_INACTIVE || 
         remoteMediaDirection == SdpMediaLine::DIRECTION_TYPE_INACTIVE)
      {
         mediaDirection = SdpMediaLine::DIRECTION_TYPE_INACTIVE;
      }
      else if(localMediaDirection == SdpMediaLine::DIRECTION_TYPE_SENDONLY)
      {
         mediaDirection = SdpMediaLine::DIRECTION_TYPE_SENDONLY;
      }
      else if(localMediaDirection == SdpMediaLine::DIRECTION_TYPE_RECVONLY)
      {
         mediaDirection = SdpMediaLine::DIRECTION_TYPE_RECVONLY;
      }
      else if(remoteMediaDirection == SdpMediaLine::DIRECTION_TYPE_SENDONLY)
      {
         mediaDirection = SdpMediaLine::DIRECTION_TYPE_RECVONLY;
      }
      else if(remoteMediaDirection == SdpMediaLine::DIRECTION_TYPE_SENDONLY)
      {
         mediaDirection = SdpMediaLine::DIRECTION_TYPE_RECVONLY;
      }
      else
      {
         mediaDirection = SdpMediaLine::DIRECTION_TYPE_SENDRECV;
      }
   }
   else
   {
      // No remote SDP info - so put direction into receive only mode (unless inactive)
      if(mLocalHold ||
         localMediaDirection == SdpMediaLine::DIRECTION_TYPE_INACTIVE || 
         localMediaDirection == SdpMediaLine::DIRECTION_TYPE_SENDONLY)
      {
         mediaDirection = SdpMediaLine::DIRECTION_TYPE_INACTIVE;
      }
      else
      {
         mediaDirection = SdpMediaLine::DIRECTION_TYPE_RECVONLY;
      }
   }

   if(remoteSdp && mDialogSet.getSecureMediaRequired() && !supportedCryptoSuite && !supportedFingerprint)
   {
      InfoLog(<< "Secure media is required and no valid support found in remote sdp - ending call.");
      destroyParticipant();
      return;
   }

   InfoLog(<< "adjustRTPStreams: handle=" << mHandle << ", mediaDirection=" << mediaDirection << ", remoteIp=" << remoteIPAddress << ", remotePort=" << remoteRtpPort);

   if(!remoteIPAddress.empty() && remoteRtpPort != 0)
   {
      mDialogSet.setActiveDestination(remoteIPAddress.c_str(), remoteRtpPort, remoteRtcpPort);
   }

   if((mediaDirection == SdpMediaLine::DIRECTION_TYPE_SENDRECV ||
       mediaDirection == SdpMediaLine::DIRECTION_TYPE_SENDONLY) &&
       !remoteIPAddress.empty() && remoteRtpPort != 0 && 
       remoteCodecs && localCodecs)
   {
      // Calculate intersection of local and remote codecs, and pass remote codecs that exist locally to RTP send fn
      int numCodecs=0;
      ::SdpCodec** codecs = new ::SdpCodec*[remoteCodecs->size()];
      SdpMediaLine::CodecList::const_iterator itRemoteCodec = remoteCodecs->begin();
      for(; itRemoteCodec != remoteCodecs->end(); itRemoteCodec++)
      {
         bool modeInRemote = itRemoteCodec->getFormatParameters().prefix("mode=");
         SdpMediaLine::CodecList::const_iterator bestCapsCodecMatchIt = localCodecs->end();
         SdpMediaLine::CodecList::const_iterator itLocalCodec = localCodecs->begin();
         for(; itLocalCodec != localCodecs->end(); itLocalCodec++)
         {
            if(isEqualNoCase(itRemoteCodec->getMimeSubtype(), itLocalCodec->getMimeSubtype()) &&
               itRemoteCodec->getRate() == itLocalCodec->getRate())
            {
               bool modeInLocal = itLocalCodec->getFormatParameters().prefix("mode=");
               if(!modeInLocal && !modeInRemote)
               {
                  // If mode is not specified in either - then we have a match
                  bestCapsCodecMatchIt = itLocalCodec;
                  break;
               }
               else if(modeInLocal && modeInRemote)
               {
                  if(isEqualNoCase(itRemoteCodec->getFormatParameters(), itLocalCodec->getFormatParameters()))
                  {
                     bestCapsCodecMatchIt = itLocalCodec;
                     break;
                  }
                  // If mode is specified in both, and doesn't match - then we have no match
               }
               else
               {
                  // Mode is specified on either offer or caps - this match is a potential candidate
                  // As a rule - use first match of this kind only
                  if(bestCapsCodecMatchIt == localCodecs->end())
                  {
                     bestCapsCodecMatchIt = itLocalCodec;
                  }
               }
            }
         }
         if(bestCapsCodecMatchIt != localCodecs->end())
         {
            codecs[numCodecs++] = new ::SdpCodec(itRemoteCodec->getPayloadType(), 
                                                 itRemoteCodec->getMimeType().c_str(), 
                                                 itRemoteCodec->getMimeSubtype().c_str(), 
                                                 itRemoteCodec->getRate(), 
                                                 itRemoteCodec->getPacketTime(), 
                                                 itRemoteCodec->getNumChannels(), 
                                                 itRemoteCodec->getFormatParameters().c_str());

            UtlString codecString;
            codecs[numCodecs-1]->toString(codecString);

            InfoLog(<< "adjustRTPStreams: handle=" << mHandle << ", sending to destination address " << remoteIPAddress << ":" << 
                       remoteRtpPort << " (RTCP on " << remoteRtcpPort << "): " << codecString.data());
         }
      }

      if(numCodecs > 0)
      {
         mConversationManager.getMediaInterface()->startRtpSend(mDialogSet.getMediaConnectionId(), numCodecs, codecs);
      }
      else
      {
         WarningLog(<< "adjustRTPStreams: handle=" << mHandle << ", something went wrong during SDP negotiations, no common codec found.");
      }
      for(int i = 0; i < numCodecs; i++)
      {
         delete codecs[i];
      }
      delete [] codecs;
   }
   else
   {
      if(mConversationManager.getMediaInterface()->isSendingRtpAudio(mDialogSet.getMediaConnectionId()))
      {
         mConversationManager.getMediaInterface()->stopRtpSend(mDialogSet.getMediaConnectionId());
      }
      InfoLog(<< "adjustRTPStreams: handle=" << mHandle << ", stop sending.");
   }

   if(mediaDirection == SdpMediaLine::DIRECTION_TYPE_SENDRECV ||
      mediaDirection == SdpMediaLine::DIRECTION_TYPE_RECVONLY)
   {
      if(!mConversationManager.getMediaInterface()->isReceivingRtpAudio(mDialogSet.getMediaConnectionId()))
      {
         // !SLG! - we could make this better, no need to recalculate this every time
         // We are always willing to receive any of our locally supported codecs
         int numCodecs=0;
         ::SdpCodec** codecs = new ::SdpCodec*[localCodecs->size()];
         SdpMediaLine::CodecList::const_iterator itLocalCodec = localCodecs->begin();
         for(; itLocalCodec != localCodecs->end(); itLocalCodec++)
         {
            codecs[numCodecs++] = new ::SdpCodec(itLocalCodec->getPayloadType(), 
                                                 itLocalCodec->getMimeType().c_str(), 
                                                 itLocalCodec->getMimeSubtype().c_str(), 
                                                 itLocalCodec->getRate(), 
                                                 itLocalCodec->getPacketTime(), 
                                                 itLocalCodec->getNumChannels(), 
                                                 itLocalCodec->getFormatParameters().c_str());
            UtlString codecString;
            codecs[numCodecs-1]->toString(codecString);
            InfoLog(<< "adjustRTPStreams: handle=" << mHandle << ", receving: " << codecString.data());            
         }
          
         mConversationManager.getMediaInterface()->startRtpReceive(mDialogSet.getMediaConnectionId(), numCodecs, codecs);
         for(int i = 0; i < numCodecs; i++)
         {
            delete codecs[i];
         }
         delete [] codecs;
      }
      InfoLog(<< "adjustRTPStreams: handle=" << mHandle << ", receiving...");
   }
   else
   {
      // Never stop receiving - keep reading buffers and let mixing matrix handle supression of audio output
      //if(mConversationManager.getMediaInterface()->isReceivingRtpAudio(mDialogSet.getMediaConnectionId()))
      //{
      //   mConversationManager.getMediaInterface()->stopRtpReceive(mDialogSet.getMediaConnectionId());
      //}
      InfoLog(<< "adjustRTPStreams: handle=" << mHandle << ", stop receiving (mLocalHold=" << mLocalHold << ").");
   }
}

void
RemoteParticipant::onDtmfEvent(int dtmf, int duration, bool up)
{
   if(mHandle) mConversationManager.onDtmfEvent(mHandle, dtmf, duration, up);
}

void
RemoteParticipant::onNewSession(ClientInviteSessionHandle h, InviteSession::OfferAnswerType oat, const SipMessage& msg)
{
   InfoLog(<< "onNewSession(Client): handle=" << mHandle << ", " << msg.brief());
   mInviteSessionHandle = h->getSessionHandle();
   mDialogId = getDialogId();
}

void
RemoteParticipant::onNewSession(ServerInviteSessionHandle h, InviteSession::OfferAnswerType oat, const SipMessage& msg)
{
   InfoLog(<< "onNewSession(Server): handle=" << mHandle << ", " << msg.brief());
   mInviteSessionHandle = h->getSessionHandle();         
   mDialogId = getDialogId();
            
   // First check if this INVITE is to replace an existing session
   if(msg.exists(h_Replaces))
   {
      pair<InviteSessionHandle, int> presult;
      presult = mDum.findInviteSession(msg.header(h_Replaces));
      if(!(presult.first == InviteSessionHandle::NotValid())) 
      {         
         RemoteParticipant* participantToReplace = dynamic_cast<RemoteParticipant *>(presult.first->getAppDialog().get());
         InfoLog(<< "onNewSession(Server): handle=" << mHandle << ", to replace handle=" << participantToReplace->getParticipantHandle() << ", " << msg.brief());

         // Assume Participant Handle of old call
         participantToReplace->replaceWithParticipant(this);      // adjust conversation mappings

         // Session to replace was found - end old session and flag to auto-answer this session after SDP offer-answer is complete
         participantToReplace->destroyParticipant();
         stateTransition(Replacing);
         return;
      }      
   }

   // Check for Auto-Answer indication - support draft-ietf-answer-mode-01 
   // and Answer-After parameter of Call-Info header
   ConversationProfile* profile = dynamic_cast<ConversationProfile*>(h->getUserProfile().get());
   assert(profile);
   bool autoAnswerRequired;
   bool autoAnswer = profile->shouldAutoAnswer(msg, &autoAnswerRequired);
   if(!autoAnswer && autoAnswerRequired)  // If we can't autoAnswer but it was required, we must reject the call
   {
      WarningCategory warning;
      warning.hostname() = DnsUtil::getLocalHostName();
      warning.code() = 399; /* Misc. */
      warning.text() = "automatic answer forbidden";
      setHandle(0); // Don't generate any callbacks for this rejected invite
      h->reject(403 /* Forbidden */, &warning);
      return;
   }
  
   // notify of new participant
   if(mHandle) mConversationManager.onIncomingParticipant(mHandle, msg, autoAnswer);
}

void
RemoteParticipant::onFailure(ClientInviteSessionHandle h, const SipMessage& msg)
{
   stateTransition(Terminating);
   InfoLog(<< "onFailure: handle=" << mHandle << ", " << msg.brief());
   // If ForkSelectMode is automatic, then ensure we destory any conversations, except the original
   if(mDialogSet.getForkSelectMode() == ConversationManager::ForkSelectAutomatic &&
      mHandle != mDialogSet.getActiveRemoteParticipantHandle())
   {
      destroyConversations();
   }
}
      
void
RemoteParticipant::onEarlyMedia(ClientInviteSessionHandle h, const SipMessage& msg, const SdpContents& sdp)
{
   InfoLog(<< "onEarlyMedia: handle=" << mHandle << ", " << msg.brief());
   if(!mDialogSet.isStaleFork(getDialogId()))
   {
      setRemoteSdp(sdp, true);
      adjustRTPStreams();
   }
}

void
RemoteParticipant::onProvisional(ClientInviteSessionHandle h, const SipMessage& msg)
{
   InfoLog(<< "onProvisional: handle=" << mHandle << ", " << msg.brief());
   assert(msg.header(h_StatusLine).responseCode() != 100);

   if(!mDialogSet.isStaleFork(getDialogId()))
   {
      if(mHandle) mConversationManager.onParticipantAlerting(mHandle, msg);
   }
}

void
RemoteParticipant::onConnected(ClientInviteSessionHandle h, const SipMessage& msg)
{
   InfoLog(<< "onConnected(Client): handle=" << mHandle << ", " << msg.brief());
   
   // Check if this is the first leg in a potentially forked call to send a 200
   if(!mDialogSet.isUACConnected())  
   {
      if(mHandle) mConversationManager.onParticipantConnected(mHandle, msg);

      mDialogSet.setUACConnected(getDialogId(), mHandle);
      stateTransition(Connected);
   }
   else
   {
      // We already have a 200 response - send a BYE to this leg
      h->end();
   }
}

void
RemoteParticipant::onConnected(InviteSessionHandle, const SipMessage& msg)
{
   InfoLog(<< "onConnected: handle=" << mHandle << ", " << msg.brief());
   stateTransition(Connected);
}

void
RemoteParticipant::onStaleCallTimeout(ClientInviteSessionHandle)
{
   WarningLog(<< "onStaleCallTimeout: handle=" << mHandle);
}

void
RemoteParticipant::onTerminated(InviteSessionHandle h, InviteSessionHandler::TerminatedReason reason, const SipMessage* msg)
{
   stateTransition(Terminating);
   switch(reason)
   {
   case InviteSessionHandler::RemoteBye:
      InfoLog(<< "onTerminated: handle=" << mHandle << ", received a BYE from peer");
      break;
   case InviteSessionHandler::RemoteCancel:
      InfoLog(<< "onTerminated: handle=" << mHandle << ", received a CANCEL from peer");
      break;
   case InviteSessionHandler::Rejected:
      InfoLog(<< "onTerminated: handle=" << mHandle << ", received a rejection from peer");
      break;
   case InviteSessionHandler::LocalBye:
      InfoLog(<< "onTerminated: handle=" << mHandle << ", ended locally via BYE");
      break;
   case InviteSessionHandler::LocalCancel:
      InfoLog(<< "onTerminated: handle=" << mHandle << ", ended locally via CANCEL");
      break;
   case InviteSessionHandler::Replaced:
      InfoLog(<< "onTerminated: handle=" << mHandle << ", ended due to being replaced");
      break;
   case InviteSessionHandler::Referred:
      InfoLog(<< "onTerminated: handle=" << mHandle << ", ended due to being reffered");
      break;
   case InviteSessionHandler::Error:
      InfoLog(<< "onTerminated: handle=" << mHandle << ", ended due to an error");
      break;
   case InviteSessionHandler::Timeout:
      InfoLog(<< "onTerminated: handle=" << mHandle << ", ended due to a timeout");
      break;
   default:
      assert(false);
      break;
   }
   unsigned int statusCode = 0;
   if(msg)
   {
      if(msg->isResponse())
      {
         statusCode = msg->header(h_StatusLine).responseCode();
      }
   }

   // If this is a referred call and the refer is still around - then switch back to referrer (ie. failed transfer recovery)
   if(mHandle && mReferringAppDialog.isValid())
   {
      RemoteParticipant* participant = (RemoteParticipant*)mReferringAppDialog.get();
      replaceWithParticipant(participant);      // adjust conversation mappings
      if(participant->getParticipantHandle())
      {
         participant->adjustRTPStreams();
         return;
      }
   }

   // Ensure terminating party is from answered fork before generating event
   if(!mDialogSet.isStaleFork(getDialogId()))
   {
      if(mHandle) mConversationManager.onParticipantTerminated(mHandle, statusCode);
   }
}

void
RemoteParticipant::onRedirected(ClientInviteSessionHandle, const SipMessage& msg)
{
   InfoLog(<< "onRedirected: handle=" << mHandle << ", " << msg.brief());
}

void
RemoteParticipant::onAnswer(InviteSessionHandle h, const SipMessage& msg, const SdpContents& sdp)
{
   InfoLog(<< "onAnswer: handle=" << mHandle << ", " << msg.brief());

   // Ensure answering party is from answered fork before generating event
   if(!mDialogSet.isStaleFork(getDialogId()))
   {
      setRemoteSdp(sdp, true);
      adjustRTPStreams();
   }
   stateTransition(Connected);  // This is valid until PRACK is implemented
}

void
RemoteParticipant::onOffer(InviteSessionHandle h, const SipMessage& msg, const SdpContents& offer)
{         
   InfoLog(<< "onOffer: handle=" << mHandle << ", " << msg.brief());
   unsigned int localRTPPort = getLocalRTPPort();
   if(localRTPPort == 0)
   {
      h->reject(486);  // Busy-Here? - is this the best return code?
   }
   else
   {
      if(mState == Connecting && mInviteSessionHandle.isValid())
      {
         ServerInviteSession* sis = dynamic_cast<ServerInviteSession*>(mInviteSessionHandle.get());
         if(sis && !sis->isAccepted())
         {
            // Don't set answer now - store offer and set when needed - so that sendHoldSdp() can be calculated at the right instant
            // we need to allow time for app to add to a conversation before alerting(with early flag) or answering
            mPendingOffer = std::auto_ptr<SdpContents>(static_cast<SdpContents*>(offer.clone()));
            return;
         }
      }

      if(provideAnswer(offer, mState==Replacing /* postAnswerAccept */, false /* postAnswerAlert */))
      {
         if(mState == Replacing)
         {
            stateTransition(Connecting);
         }
      }
   }
}

void
RemoteParticipant::onOfferRequired(InviteSessionHandle h, const SipMessage& msg)
{
   InfoLog(<< "onOfferRequired: handle=" << mHandle << ", " << msg.brief());
   unsigned int localRTPPort = getLocalRTPPort();
   if(localRTPPort == 0)
   {
      h->reject(486);  // Busy-Here? - is this the best return code?
   }
   else
   {
      if(mState == Connecting && !h->isAccepted())  
      {
         // If we haven't accepted yet - delay providing the offer until accept is called (this allows time 
         // for a localParticipant to be added before generating the offer)
         mOfferRequired = true;
      }
      else
      {
         provideOffer(mState == Replacing /* postOfferAccept */);
         if(mState == Replacing)
         {
            stateTransition(Connecting);
         }
      }
   }
}

void
RemoteParticipant::onOfferRejected(InviteSessionHandle, const SipMessage* msg)
{
   if(msg)
   {
      InfoLog(<< "onOfferRejected: handle=" << mHandle << ", " << msg->brief());
   }
   else
   {
      InfoLog(<< "onOfferRejected: handle=" << mHandle);
   }
}

void
RemoteParticipant::onOfferRequestRejected(InviteSessionHandle h, const SipMessage& msg)
{
   InfoLog(<< "onOfferRequestRejected: handle=" << mHandle << ", " << msg.brief());
   assert(0);  // We never send a request for an offer (ie. Invite with no SDP)
}

void
RemoteParticipant::onRemoteSdpChanged(InviteSessionHandle h, const SipMessage& msg, const SdpContents& sdp)
{
   InfoLog(<< "onRemoteSdpChanged: handle=" << mHandle << ", " << msg.brief());      
   setRemoteSdp(sdp);
   adjustRTPStreams();
}

void
RemoteParticipant::onInfo(InviteSessionHandle, const SipMessage& msg)
{
   InfoLog(<< "onInfo: handle=" << mHandle << ", " << msg.brief());
   //assert(0);
}

void
RemoteParticipant::onInfoSuccess(InviteSessionHandle, const SipMessage& msg)
{
   InfoLog(<< "onInfoSuccess: handle=" << mHandle << ", " << msg.brief());
   assert(0);  // We never send an info request
}

void
RemoteParticipant::onInfoFailure(InviteSessionHandle, const SipMessage& msg)
{
   InfoLog(<< "onInfoFailure: handle=" << mHandle << ", " << msg.brief());
   assert(0);  // We never send an info request
}

void
RemoteParticipant::onRefer(InviteSessionHandle is, ServerSubscriptionHandle ss, const SipMessage& msg)
{
   InfoLog(<< "onRefer: handle=" << mHandle << ", " << msg.brief());

   try
   {
      // Accept the Refer
      ss->send(ss->accept(202 /* Refer Accepted */));

      // Figure out hold SDP before removing ourselves from the conversation
      bool holdSdp = mLocalHold;  

      // Create new Participant - but use same participant handle
      RemoteParticipantDialogSet* participantDialogSet = new RemoteParticipantDialogSet(mConversationManager, mDialogSet.getForkSelectMode());
      RemoteParticipant *participant = participantDialogSet->createUACOriginalRemoteParticipant(mHandle); // This will replace old participant in ConversationManager map
      participant->mReferringAppDialog = getHandle();

      // Create offer
      SdpContents offer;
      participant->buildSdpOffer(holdSdp, offer);

      replaceWithParticipant(participant);      // adjust conversation mappings - do this after buildSdpOffer, so that we have a bridge port

      // Build the Invite
      SharedPtr<SipMessage> NewInviteMsg = mDum.makeInviteSessionFromRefer(msg, ss->getHandle(), &offer, participantDialogSet);
      mDialogSet.sendInvite(NewInviteMsg); 

      // Set RTP stack to listen
      participant->adjustRTPStreams(true);
   }
   catch(BaseException &e)
   {
      WarningLog(<< "onRefer exception: " << e);
   }
   catch(...)
   {
      WarningLog(<< "onRefer unknown exception");
   }
}

void 
RemoteParticipant::doReferNoSub(const SipMessage& msg)
{
   // Figure out hold SDP before removing ourselves from the conversation
   bool holdSdp = mLocalHold;  

   // Create new Participant - but use same participant handle
   RemoteParticipantDialogSet* participantDialogSet = new RemoteParticipantDialogSet(mConversationManager, mDialogSet.getForkSelectMode());
   RemoteParticipant *participant = participantDialogSet->createUACOriginalRemoteParticipant(mHandle); // This will replace old participant in ConversationManager map
   participant->mReferringAppDialog = getHandle();

   // Create offer
   SdpContents offer;
   participant->buildSdpOffer(holdSdp, offer);

   replaceWithParticipant(participant);      // adjust conversation mappings - do this after buildSdpOffer, so that we have a bridge port

   // Build the Invite
   SharedPtr<SipMessage> NewInviteMsg = mDum.makeInviteSessionFromRefer(msg, mDialogSet.getUserProfile(), &offer, participantDialogSet);
   mDialogSet.sendInvite(NewInviteMsg); 

   // Set RTP stack to listen
   participant->adjustRTPStreams(true);
}

void
RemoteParticipant::onReferNoSub(InviteSessionHandle is, const SipMessage& msg)
{
   InfoLog(<< "onReferNoSub: handle=" << mHandle << ", " << msg.brief());

   try
   {
      // Accept the Refer
		is->acceptReferNoSub(202 /* Refer Accepted */);

      doReferNoSub(msg);
   }
   catch(BaseException &e)
   {
      WarningLog(<< "onReferNoSub exception: " << e);
   }
   catch(...)
   {
      WarningLog(<< "onReferNoSub unknown exception");
   }
}

void
RemoteParticipant::onReferAccepted(InviteSessionHandle, ClientSubscriptionHandle, const SipMessage& msg)
{
   InfoLog(<< "onReferAccepted: handle=" << mHandle << ", " << msg.brief());
}

void
RemoteParticipant::onReferRejected(InviteSessionHandle, const SipMessage& msg)
{
   InfoLog(<< "onReferRejected: handle=" << mHandle << ", " << msg.brief());
   if(msg.isResponse() && mState == Redirecting)
   {
      if(mHandle) mConversationManager.onParticipantRedirectFailure(mHandle, msg.header(h_StatusLine).responseCode());
      stateTransition(Connected);
   }
}

void
RemoteParticipant::onMessage(InviteSessionHandle, const SipMessage& msg)
{
   InfoLog(<< "onMessage: handle=" << mHandle << ", " << msg.brief());
}

void
RemoteParticipant::onMessageSuccess(InviteSessionHandle, const SipMessage& msg)
{
   InfoLog(<< "onMessageSuccess: handle=" << mHandle << ", " << msg.brief());
}

void
RemoteParticipant::onMessageFailure(InviteSessionHandle, const SipMessage& msg)
{
   InfoLog(<< "onMessageFailure: handle=" << mHandle << ", " << msg.brief());
}

void
RemoteParticipant::onForkDestroyed(ClientInviteSessionHandle)
{
   InfoLog(<< "onForkDestroyed: handle=" << mHandle);
}


////////////////////////////////////////////////////////////////////////////////
// ClientSubscriptionHandler ///////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void
RemoteParticipant::onUpdatePending(ClientSubscriptionHandle h, const SipMessage& notify, bool outOfOrder)
{
   InfoLog(<< "onUpdatePending(ClientSub): handle=" << mHandle << ", " << notify.brief());
   if (notify.exists(h_Event) && notify.header(h_Event).value() == "refer")
   {
      h->acceptUpdate();
      processReferNotify(notify);
   }
   else
   {
      h->rejectUpdate(400, Data("Only notifies for refers are allowed."));
   }
}

void
RemoteParticipant::onUpdateActive(ClientSubscriptionHandle h, const SipMessage& notify, bool outOfOrder)
{
   InfoLog(<< "onUpdateActive(ClientSub): handle=" << mHandle << ", " << notify.brief());
   if (notify.exists(h_Event) && notify.header(h_Event).value() == "refer")
   {
      h->acceptUpdate();
      processReferNotify(notify);
   }
   else
   {
      h->rejectUpdate(400, Data("Only notifies for refers are allowed."));
   }
}

void
RemoteParticipant::onUpdateExtension(ClientSubscriptionHandle h, const SipMessage& notify, bool outOfOrder)
{
   InfoLog(<< "onUpdateExtension(ClientSub): handle=" << mHandle << ", " << notify.brief());
   if (notify.exists(h_Event) && notify.header(h_Event).value() == "refer")
   {
      h->acceptUpdate();
      processReferNotify(notify);
   }
   else
   {
      h->rejectUpdate(400, Data("Only notifies for refers are allowed."));
   }
}

void
RemoteParticipant::onTerminated(ClientSubscriptionHandle h, const SipMessage* notify)
{
   if(notify)
   {
      InfoLog(<< "onTerminated(ClientSub): handle=" << mHandle << ", " << notify->brief());
      if (notify->isRequest() && notify->exists(h_Event) && notify->header(h_Event).value() == "refer")
      {
         // Note:  Final notify is sometimes only passed in the onTerminated callback
         processReferNotify(*notify);
      }
      else if(notify->isResponse() && mState == Redirecting)
      {
         if(mHandle) mConversationManager.onParticipantRedirectFailure(mHandle, notify->header(h_StatusLine).responseCode());
         stateTransition(Connected);
      }
   }
   else
   {
      // Timed out waiting for notify
      InfoLog(<< "onTerminated(ClientSub): handle=" << mHandle);
      if(mState == Redirecting)
      {
         if(mHandle) mConversationManager.onParticipantRedirectFailure(mHandle, 408);
         stateTransition(Connected);
      }
   }
}

void
RemoteParticipant::onNewSubscription(ClientSubscriptionHandle h, const SipMessage& notify)
{
   InfoLog(<< "onNewSubscription(ClientSub): handle=" << mHandle << ", " << notify.brief());
}

int 
RemoteParticipant::onRequestRetry(ClientSubscriptionHandle h, int retryMinimum, const SipMessage& notify)
{
   InfoLog(<< "onRequestRetry(ClientSub): handle=" << mHandle << ", " << notify.brief());
   return -1;
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
