#if defined(HAVE_CONFIG_H)
#include "resip/stack/config.hxx"
#endif

#include <iostream>

#include "resip/stack/ExtensionParameter.hxx"
#include "resip/stack/InteropHelper.hxx"
#include "resip/stack/SipMessage.hxx"
#include "resip/stack/SipStack.hxx"
#include "rutil/DnsUtil.hxx"
#include "rutil/Inserter.hxx"
#include "resip/stack/Helper.hxx"
#include "rutil/Logger.hxx"
#include "repro/Proxy.hxx"
#include "repro/ResponseContext.hxx"
#include "repro/RequestContext.hxx"
#include "repro/Ack200DoneMessage.hxx"
#include "rutil/WinLeakCheck.hxx"

#define RESIPROCATE_SUBSYSTEM resip::Subsystem::REPRO

using namespace resip;
using namespace repro;
using namespace std;

ResponseContext::ResponseContext(RequestContext& context) : 
   mRequestContext(context),
   mBestPriority(50),
   mSecure(false) //context.getOriginalRequest().header(h_RequestLine).uri().scheme() == Symbols::Sips)
{
}


ResponseContext::~ResponseContext()
{
   TransactionMap::iterator i;
   
   for(i=mTerminatedTransactionMap.begin(); i!=mTerminatedTransactionMap.end();++i)
   {
      delete i->second;
   }
   mTerminatedTransactionMap.clear();
   
   for(i=mActiveTransactionMap.begin(); i!=mActiveTransactionMap.end();++i)
   {
      delete i->second;
   }
   mActiveTransactionMap.clear();
   
   for(i=mCandidateTransactionMap.begin(); i!=mCandidateTransactionMap.end();++i)
   {
      delete i->second;
   }
   mCandidateTransactionMap.clear();
}

resip::Data
ResponseContext::addTarget(const NameAddr& addr, bool beginImmediately, bool addToFirstBatch)
{
   InfoLog (<< "Adding candidate " << addr);
   std::auto_ptr<Target> target(new Target(addr));
   Data tid=target->tid();
   addTarget(target, beginImmediately, addToFirstBatch);
   return tid;
}

bool
ResponseContext::addTarget(std::auto_ptr<repro::Target> target, bool beginImmediately, bool addToFirstBatch)
{
   if(mRequestContext.mHaveSentFinalResponse || !target.get())
   {
      return false;
   }

   //Disallow sip: if secure
   if(mSecure && target->uri().scheme() != Symbols::Sips)
   {
      return false;
   }
   
   //Make sure we don't have Targets with an invalid initial state.
   if(target->status() != Target::Candidate)
   {
      return false;
   }
   
   if(beginImmediately)
   {
      if(isDuplicate(target.get()))
      {
         return false;
      }
   
      mTargetList.push_back(target->rec());
      
      beginClientTransaction(target.get());
      target->status()=Target::Started;
      Target* toAdd=target.release();
      mActiveTransactionMap[toAdd->tid()]=toAdd;
   }
   else
   {
      if(target->mShouldAutoProcess)
      {
         if(addToFirstBatch && !mTransactionQueueCollection.empty())
         {
            mTransactionQueueCollection.front().push_back(target->tid());
         }
         else
         {
            std::list<resip::Data> queue;
            queue.push_back(target->tid());
            mTransactionQueueCollection.push_back(queue);
         }
      }

      Target* toAdd=target.release();
      mCandidateTransactionMap[toAdd->tid()]=toAdd;
   }
   
   return true;
}

bool
ResponseContext::addTargetBatch(std::list<Target*>& targets,
                                 bool highPriority,
                                 bool addToFirstBatch)
{
   std::list<resip::Data> queue;
   Target* target=0;
   std::list<Target*>::iterator it;

   if(mRequestContext.mHaveSentFinalResponse || targets.empty())
   {
      for(it=targets.begin();it!=targets.end();it++)
      {
         delete *it;
      }
      
      targets.clear();
      return false;
   }

   
   for(it=targets.begin();it!=targets.end();it++)
   {
      target=*it;
      
      if((!mSecure || target->uri().scheme() == Symbols::Sips) &&
         target->status() == Target::Candidate)
      {
         if(target->mShouldAutoProcess)
         {
            queue.push_back(target->tid());
         }
         DebugLog(<<"Adding Target to Candidates: " << target->uri() << " tid=" << target->tid());
         mCandidateTransactionMap[target->tid()]=target;
      }
      else
      {
         DebugLog(<<"Bad Target: " << target->uri());
         delete target;
      }
   }

   targets.clear();
   
   if(addToFirstBatch)
   {
      if(!mTransactionQueueCollection.empty())
      {
         //mTransactionQueueCollection.front().merge(queue); !slg! list merge requires that both lists first be sorted - implementing manual merge
         std::list<resip::Data>::iterator it = queue.begin();
         for(; it != queue.end(); it++)
         {
            mTransactionQueueCollection.front().push_back(*it);
         }
      }
      else
      {
         mTransactionQueueCollection.push_back(queue);
      }
   }
   else
   {
      if(highPriority)
      {
         mTransactionQueueCollection.push_front(queue);
      }
      else
      {
         mTransactionQueueCollection.push_back(queue);
      }
   }
   
   return true;
}

bool
ResponseContext::addOutboundBatch(std::map<resip::Data, std::list<Target*> > batch)
{
   std::map<resip::Data, std::list<Target*> >::iterator i;
   for(i=batch.begin();i!=batch.end();++i)
   {
      std::list<resip::Data>& subList=mOutboundMap[i->first];
      while(!i->second.empty())
      {
         Target* target = i->second.front();
         mCandidateTransactionMap[target->tid()]=target;
         i->second.pop_front();
         subList.push_back(target->tid());
      }
   }
   
   return true;
}

bool 
ResponseContext::beginClientTransactions()
{
   
   bool result=false;
   
   if(mCandidateTransactionMap.empty())
   {
      return result;
   }
   
   for (TransactionMap::iterator i=mCandidateTransactionMap.begin(); i != mCandidateTransactionMap.end(); )
   {
      if(!isDuplicate(i->second) && !mRequestContext.mHaveSentFinalResponse)
      {
         mTargetList.push_back(i->second->rec());
         beginClientTransaction(i->second);
         result=true;
         // see rfc 3261 section 16.6
         //This code moves the Target from mCandidateTransactionMap to mActiveTransactionMap,
         //and begins the transaction.
         mActiveTransactionMap[i->second->tid()] = i->second;
         InfoLog (<< "Creating new client transaction " << i->second->tid() << " -> " << i->second->uri());
      }
      else
      {
         i->second->status() = Target::Terminated;
         mTerminatedTransactionMap[i->second->tid()] = i->second;
         DebugLog(<<"Found a repeated target.");
      }
      
      TransactionMap::iterator temp=i;
      i++;
      mCandidateTransactionMap.erase(temp);
   }
   
   return result;
}

bool 
ResponseContext::beginClientTransaction(const resip::Data& tid)
{
   TransactionMap::iterator i = mCandidateTransactionMap.find(tid);
   if(i==mCandidateTransactionMap.end())
   {
      return false;
   }
   
   if(isDuplicate(i->second) || mRequestContext.mHaveSentFinalResponse)
   {
      i->second->status() = Target::Terminated;
      mTerminatedTransactionMap[i->second->tid()] = i->second;
      mCandidateTransactionMap.erase(i);
      return false;
   }
   
   mTargetList.push_back(i->second->rec());
   
   beginClientTransaction(i->second);
   mActiveTransactionMap[i->second->tid()] = i->second;
   InfoLog(<< "Creating new client transaction " << i->second->tid() << " -> " << i->second->uri());
   mCandidateTransactionMap.erase(i);
   
   return true;
}

bool 
ResponseContext::cancelActiveClientTransactions()
{
   if(mRequestContext.mHaveSentFinalResponse)
   {
      return false;
   }

   InfoLog (<< "Cancel all proceeding client transactions: " << (mCandidateTransactionMap.size() + 
            mActiveTransactionMap.size()));

   if(mActiveTransactionMap.empty())
   {
      return false;
   }

   // CANCEL INVITE branches
   for (TransactionMap::iterator i = mActiveTransactionMap.begin(); 
        i != mActiveTransactionMap.end(); ++i)
   {
      cancelClientTransaction(i->second);
   }
      
   return true;

}

bool
ResponseContext::cancelAllClientTransactions()
{

   InfoLog (<< "Cancel ALL client transactions: " << mCandidateTransactionMap.size()
            << " pending, " << mActiveTransactionMap.size() << " active.");

   if(mActiveTransactionMap.empty() && mCandidateTransactionMap.empty())
   {
      return false;
   }

   // CANCEL INVITE branches
   if(mRequestContext.getOriginalRequest().method()==INVITE)
   {
      for (TransactionMap::iterator i = mActiveTransactionMap.begin(); 
           i != mActiveTransactionMap.end(); ++i)
      {
         cancelClientTransaction(i->second);
      }
   }

   for (TransactionMap::iterator j = mCandidateTransactionMap.begin(); 
        j != mCandidateTransactionMap.end();)
   {
      cancelClientTransaction(j->second);
      mTerminatedTransactionMap[j->second->tid()] = j->second;
      TransactionMap::iterator temp = j;
      j++;
      mCandidateTransactionMap.erase(temp);
   }
   
   return true;

}

bool
ResponseContext::clearCandidateTransactions()
{
   bool result=false;
   for (TransactionMap::iterator j = mCandidateTransactionMap.begin(); 
        j != mCandidateTransactionMap.end();)
   {
      result=true;
      cancelClientTransaction(j->second);
      mTerminatedTransactionMap[j->second->tid()] = j->second;
      TransactionMap::iterator temp = j;
      j++;
      mCandidateTransactionMap.erase(temp);
   }
   
   return result;

}

bool 
ResponseContext::cancelClientTransaction(const resip::Data& tid)
{

   TransactionMap::iterator i = mActiveTransactionMap.find(tid);
   if(mRequestContext.getOriginalRequest().method()==INVITE)
   {
      if(i!=mActiveTransactionMap.end())
      {
         cancelClientTransaction(i->second);      
         return true;
      }
   }
   
   TransactionMap::iterator j = mCandidateTransactionMap.find(tid);
   if(j != mCandidateTransactionMap.end())
   {
      cancelClientTransaction(j->second);
      mTerminatedTransactionMap[tid] = j->second;
      mCandidateTransactionMap.erase(j);
      return true;
   }
   
   return false;
}

Target* 
ResponseContext::getTarget(const resip::Data& tid) const
{
   // .bwc. This tid is most likely to be found in either the Candidate targets,
   // or the Active targets.
   TransactionMap::const_iterator pend = mCandidateTransactionMap.find(tid);
   if(pend != mCandidateTransactionMap.end())
   {
      assert(pend->second->status()==Target::Candidate);
      return pend->second;
   }
   
   TransactionMap::const_iterator act = mActiveTransactionMap.find(tid);
   if(act != mActiveTransactionMap.end())
   {
      assert(!(act->second->status()==Target::Candidate || act->second->status()==Target::Terminated));
      return act->second;
   }

   TransactionMap::const_iterator term = mTerminatedTransactionMap.find(tid);
   if(term != mTerminatedTransactionMap.end())
   {
      assert(term->second->status()==Target::Terminated);
      return term->second;
   }

   return 0;
}

const ResponseContext::TransactionMap& 
ResponseContext::getCandidateTransactionMap() const
{
   return mCandidateTransactionMap;
}

bool 
ResponseContext::hasCandidateTransactions() const
{
   return !mRequestContext.mHaveSentFinalResponse && !mCandidateTransactionMap.empty();
}

bool 
ResponseContext::hasActiveTransactions() const
{
   return !mActiveTransactionMap.empty();
}

bool 
ResponseContext::hasTerminatedTransactions() const
{
   return !mTerminatedTransactionMap.empty();
}

bool
ResponseContext::hasTargets() const
{
   return (hasCandidateTransactions() ||
            hasActiveTransactions() ||
            hasTerminatedTransactions());
}

bool 
ResponseContext::areAllTransactionsTerminated() const
{
   return (mCandidateTransactionMap.empty() && mActiveTransactionMap.empty());
}

bool
ResponseContext::isCandidate(const resip::Data& tid) const
{
   TransactionMap::const_iterator i=mCandidateTransactionMap.find(tid);
   return i!=mCandidateTransactionMap.end();
}

bool
ResponseContext::isActive(const resip::Data& tid) const
{
   TransactionMap::const_iterator i=mActiveTransactionMap.find(tid);
   return i!=mActiveTransactionMap.end();
}

bool
ResponseContext::isTerminated(const resip::Data& tid) const
{
   TransactionMap::const_iterator i=mTerminatedTransactionMap.find(tid);
   return i!=mTerminatedTransactionMap.end();
}

void 
ResponseContext::removeClientTransaction(const resip::Data& transactionId)
{
   // .bwc. This tid will most likely be found in the map of terminated
   // transactions, under normal circumstances.
   // NOTE: This does not remove the corresponding entry in mTargetList.
   // This is the intended behavior, because the same target should not
   // be added again later.

   TransactionMap::iterator i = mTerminatedTransactionMap.find(transactionId);
   if(i!=mTerminatedTransactionMap.end())
   {
      delete i->second;
      mTerminatedTransactionMap.erase(i);
      return;
   }

   i=mCandidateTransactionMap.find(transactionId);
   if(i!=mCandidateTransactionMap.end())
   {
      delete i->second;
      mCandidateTransactionMap.erase(i);
      return;
   }
   
   i=mActiveTransactionMap.find(transactionId);
   if(i!=mActiveTransactionMap.end())
   {
      delete i->second;
      mActiveTransactionMap.erase(i);
      WarningLog(<< "Something removed an active transaction, " << transactionId
               << ". It is very likely that something is broken here. ");
      return;
   }
         
}

bool
ResponseContext::isDuplicate(const repro::Target* target) const
{
   resip::ContactList::const_iterator i;
   // make sure each target is only inserted once

   // !bwc! We can not optimize this by using stl, because operator
   // == does not conform to the partial-ordering established by operator
   // <  (We can very easily have a < b and a==b simultaneously).
   // [TODO] Once we have a canonicalized form, we can improve this.

   for(i=mTargetList.begin();i!=mTargetList.end();i++)
   {
      if(*i==target->rec())
      {
         return true;
      }
   }

   return false;
}

void
ResponseContext::beginClientTransaction(repro::Target* target)
{
      // .bwc. This is a private function, and if anything calls this with a
      // target in an invalid state, it is a bug.
      assert(target->status() == Target::Candidate);

      SipMessage& orig=mRequestContext.getOriginalRequest();
      SipMessage request(orig);
      
      request.header(h_RequestLine).uri() = target->uri(); 

      // .bwc. Proxy checks whether this is valid, and rejects if not.
      request.header(h_MaxForwards).value()--;
      
      bool inDialog=false;
      
      try
      {
         inDialog=request.header(h_To).exists(p_tag);
      }
      catch(resip::ParseException&)
      {
         // ?bwc? Do we ignore this and just say this is a dialog-creating
         // request?
      }
      
      // Potential source Record-Route addition only for new dialogs
      // !bwc! It looks like we really ought to be record-routing in-dialog
      // stuff.
      if ( !inDialog &&  // only for dialog-creating request
           (request.method() == INVITE ||
            request.method() == SUBSCRIBE ) &&
           !mRequestContext.mProxy.getRecordRoute().uri().host().empty())  // only add record route if configured to do so
      {
         insertRecordRoute(request,
                           orig.getReceivedTransport()->getTuple(),
                           target);
      }
      else if(request.method()==REGISTER)
      {
         // .bwc. Edge-proxy stuff for outbound.
         if(request.header(h_Vias).size() == 1 
            && resip::InteropHelper::getOutboundSupported()
            && !request.empty(h_Contacts)
            && request.header(h_Contacts).front().exists(p_regid))
         {
            resip::NameAddr rt;
            if(orig.getReceivedTransport()->transport()==resip::TLS ||
               orig.getReceivedTransport()->transport()==resip::DTLS )
            {
               // Use FQDN
               rt = mRequestContext.mProxy.getRecordRoute();
               rt.uri().scheme() = "sips";
            }
            else
            {
               rt.uri().host()=orig.getReceivedTransport()->interfaceName();
               rt.uri().port()=orig.getReceivedTransport()->port();
               rt.uri().param(resip::p_transport)=resip::Tuple::toData(orig.getReceivedTransport()->transport());
            }
            resip::Helper::massageRoute(request,rt);
            resip::Data binaryFlowToken;
            Tuple::writeBinaryToken(orig.getSource(),binaryFlowToken);
            
            // !bwc! TODO encrypt this binary token to self.
            rt.uri().user()=binaryFlowToken.base64encode();
            rt.param(p_ob);
            request.header(h_Paths).push_front(rt);
            InfoLog (<< "Added Path: " << rt);
            request.header(h_Supporteds).push_back(Token("path"));
         }
      }
      
      if( (resip::InteropHelper::getOutboundSupported() ||
          resip::InteropHelper::getRRTokenHackEnabled()) 
          && target->rec().mReceivedFrom.mFlowKey)
      {
         // .bwc. We only override the destination if we are sending to an
         // outbound contact. If this is not an outbound contact, but the
         // endpoint has given us a Contact with the correct ip-address and 
         // port, we might be able to find the connection they formed when they
         // registered earlier, but that will happen down in TransportSelector.
         request.setDestination(target->rec().mReceivedFrom);
      }

      DebugLog(<<"Set tuple dest: " << request.getDestination());

      // .bwc. Path header addition.
      if(!target->rec().mSipPath.empty())
      {
         request.header(h_Routes).append(target->rec().mSipPath);
      }

      // !jf! unleash the baboons here
      // a baboon might adorn the message, record call logs or CDRs, might
      // insert loose routes on the way to the next hop
      
      Helper::processStrictRoute(request);
      
      //This is where the request acquires the tid of the Target. The tids 
      //should be the same from here on out.
      request.header(h_Vias).push_front(target->via());

      if(!mRequestContext.mInitialTimerCSet)
      {
         mRequestContext.mInitialTimerCSet=true;
         mRequestContext.updateTimerC();
      }
      
      // the rest of 16.6 is implemented by the transaction layer of resip
      // - determining the next hop (tuple)
      // - adding a content-length if needed
      // - sending the request
      sendRequest(request); 

      target->status() = Target::Started;
}

void
ResponseContext::insertRecordRoute(resip::SipMessage& outgoing,
                                    const Tuple& receivedTransport,
                                    Target* target)
{
   resip::Data inboundFlowToken=getInboundFlowToken();
   bool needsOutboundFlowToken=outboundFlowTokenNeeded(target);

   // .bwc. If we have a flow-token we need to insert, we need to record-route.
   // Also, we might record-route if we are configured to do so.
   if( !inboundFlowToken.empty() 
      || needsOutboundFlowToken 
      || mRequestContext.mProxy.getRecordRouteEnabled() )
   {
      resip::NameAddr rt;
      if(inboundFlowToken.empty())
      {
         rt=mRequestContext.mProxy.getRecordRoute();
      }
      else
      {
         if(receivedTransport.getType()==TLS || 
            receivedTransport.getType()==DTLS)
         {
            // .bwc. Debatable. Should we be willing to reuse a TLS connection
            // at the behest of a Route header with no hostname in it?
            rt=mRequestContext.mProxy.getRecordRoute();
            rt.uri().scheme() = "sips";
         }
         else
         {
            rt.uri().host()=resip::Tuple::inet_ntop(receivedTransport);
            rt.uri().port()=receivedTransport.getPort();
            rt.uri().param(resip::p_transport)=resip::Tuple::toData(receivedTransport.getType());
         }
         rt.uri().user()=inboundFlowToken;
      }
      Helper::massageRoute(outgoing,rt);

#ifdef USE_SIGCOMP
      if(mRequestContext.getProxy().compressionEnabled() &&
         target->uri().exists(p_comp) &&
         target->uri().param(p_comp)=="sigcomp")
      {
         rt.uri().param(p_comp)="sigcomp";
      }
#endif

      outgoing.header(h_RecordRoutes).push_front(rt);
      InfoLog (<< "Added Record-Route: " << rt);
   }

   // .bwc. We only double record-route if we are putting in a flow-token.
   // (if we are configured to always record-route, we will have already record-
   // routed once above, no sense in putting a second, identical RR in.)
   // !bwc! TODO some logic or config to allow double record-routing in other
   // circumstances (on transport switch, for instance)
   if(!inboundFlowToken.empty() || needsOutboundFlowToken)
   {
      outgoing.addOutboundDecorator(mRequestContext.mProxy.makeRRDecorator());
   }
}

resip::Data
ResponseContext::getInboundFlowToken()
{
   resip::Data flowToken=resip::Data::Empty;
   resip::SipMessage& orig=mRequestContext.getOriginalRequest();
   
   if(InteropHelper::getOutboundSupported() && 
      orig.header(h_Contacts).front().uri().exists(p_ob) &&
      orig.header(h_Vias).size()==1)
   {
      // This arrived over an outbound flow (or so the endpoint claims)
      // (See outbound-09 Sec 4.3 para 3)
      resip::Data binaryFlowToken;
      Tuple::writeBinaryToken(orig.getSource(),binaryFlowToken);
      // !bwc! TODO encrypt to self
      flowToken = binaryFlowToken.base64encode();
   }
   else if(resip::InteropHelper::getRRTokenHackEnabled()
      && !selfAlreadyRecordRouted() )
   {
      // !bwc! TODO remove this when flow-token hack is no longer needed.
      // Poor-man's outbound. Shouldn't be our default behavior, because it
      // breaks target-refreshes (once a flow-token is in the Route-Set, the 
      // flow-token cannot be changed, and will override any update to the 
      // Contact)
      resip::Data binaryFlowToken;
      Tuple::writeBinaryToken(orig.getSource(),binaryFlowToken);
      // !bwc! TODO encrypt to self
      flowToken = binaryFlowToken.base64encode();
   }
   
   return flowToken;
}

bool
ResponseContext::outboundFlowTokenNeeded(Target* target)
{
   if(mRequestContext.mProxy.isMyUri(target->uri()))
   {
      // .bwc. We don't need to put flow-tokens pointed at ourselves.
      return false;
   }

   if(target->rec().mReceivedFrom.onlyUseExistingConnection
      || resip::InteropHelper::getRRTokenHackEnabled())
   {
      return true;
   }

   return false;
}

bool
ResponseContext::selfAlreadyRecordRouted()
{
   resip::SipMessage& orig=mRequestContext.getOriginalRequest();
   return (!orig.empty(h_RecordRoutes) 
            && orig.header(h_RecordRoutes).front().isWellFormed()
            && mRequestContext.mProxy.isMyUri(
                        orig.header(h_RecordRoutes).front().uri()));
}

bool
ResponseContext::sendingToSelf(Target* target)
{
   if(mRequestContext.mProxy.isMyUri(target->uri()))
   {
      return true;
   }
   return false;
}

void 
ResponseContext::sendRequest(const resip::SipMessage& request)
{
   assert (request.isRequest());

   if (request.method() != CANCEL && 
       request.method() != ACK)
   {
      mRequestContext.getProxy().addClientTransaction(request.getTransactionId(), &mRequestContext);
      mRequestContext.mTransactionCount++;
//      if(!mRequestContext.getDigestIdentity().empty())
//      {
//         requestPtr->header(h_Identity);
//         // !bwc! Need to fill in the Identity-Info header telling where our 
//         // cert can be found.
//      }
   }

   if (request.method() == ACK)
   {
     DebugLog(<<"Posting Ack200DoneMessage");
     mRequestContext.getProxy().post(new Ack200DoneMessage(mRequestContext.getTransactionId()));
   }

   mRequestContext.mProxy.send(request);
}


void
ResponseContext::processCancel(const SipMessage& request)
{
   assert(request.isRequest());
   assert(request.method() == CANCEL);

   std::auto_ptr<SipMessage> ok(Helper::makeResponse(request, 200));   
   mRequestContext.sendResponse(*ok);

   if (!mRequestContext.mHaveSentFinalResponse)
   {
      cancelAllClientTransactions();
      if(!hasActiveTransactions())
      {
         SipMessage reqterm;
         Helper::makeResponse(reqterm,mRequestContext.getOriginalRequest(),487);
         mRequestContext.sendResponse(reqterm);
      }
   }
}

void
ResponseContext::processTimerC()
{
   if (!mRequestContext.mHaveSentFinalResponse)
   {
      InfoLog(<<"Canceling client transactions due to timer C.");
      cancelAllClientTransactions();
   }
}

void
ResponseContext::processResponse(SipMessage& response)
{
   InfoLog (<< "processResponse: " << endl << response);

   // store this before we pop the via and lose the branch tag
   mCurrentResponseTid = response.getTransactionId();
   
   assert (response.isResponse());
   assert (response.exists(h_Vias) && !response.header(h_Vias).empty());
   response.header(h_Vias).pop_front();

   // Stop processing responses that have nowhere else to go
   if (response.header(h_Vias).empty())
   {
      // CANCEL/200s only have one Via.  Likewise 100s only have one Via  
      // Silently stop processing the CANCEL responses.
      // We will handle the 100 responses later
      // Log other responses we can't forward

      if(response.method()==CANCEL)
      {
         return;
      }
      else if (response.header(h_StatusLine).statusCode() > 199)
      {
         InfoLog( << "Received final response, but can't forward as there are no more Vias: " << response.brief() );
         // .bwc. Treat as server error.
         terminateClientTransaction(mCurrentResponseTid);
         return;
      }
      else if(response.header(h_StatusLine).statusCode() != 100)
      {
         InfoLog( << "Received response, but can't forward as there are no more Vias: " << response.brief() );
         return;
      }
   }
   else
   {
      const Via& via = response.header(h_Vias).front();

      if(!via.isWellFormed())
      {
         // .bwc. Garbage via. Unrecoverable. Ignore if provisional, terminate
         // transaction if not.
         DebugLog(<<"Some endpoint has corrupted one of our Vias"
            " in their response. (Via is malformed) This is not fixable.");
         if(response.header(h_StatusLine).statusCode() > 199)
         {
            terminateClientTransaction(mCurrentResponseTid);
         }
         
         return;
      }
   }
   
   InfoLog (<< "Search for " << mCurrentResponseTid << " in " << Inserter(mActiveTransactionMap));

   TransactionMap::iterator i = mActiveTransactionMap.find(mCurrentResponseTid);

   int code = response.header(h_StatusLine).statusCode();
   if (i == mActiveTransactionMap.end())
   {
      // This is a response for a transaction that is no longer/was never active.  
      // This is probably a useless response (at best) or a malicious response (at worst).
      // Log the response here:
      if ((code / 100) != 2)
      {
         InfoLog( << "Discarding stray response" );
      }
      // Even though this is a tremendously bad idea, some developers may
      // decide they want to statelessly forward the response
      // Here is the gun.  Don't say we didn't warn you!
      else
      {
         // !abr! Because we don't run timers on the transaction after
         //       it has terminated and because the ACKs on INVITE
         //       200-class responses are end-to-end, we don't discard
         //       200 responses. To do this properly, we should run a
         //       transaction timer for 64*T1 and remove transactions from
         //       the ActiveTransactionMap *only* after that timer expires.
         //       IN OTHER WORDS, REMOVE THIS CODE.
         mRequestContext.sendResponse(response);
      }
      return;
   }

   switch (code / 100)
   {
      case 1:
         mRequestContext.updateTimerC();

         if  (!mRequestContext.mHaveSentFinalResponse)
         {
            if (code == 100)
            {
               return;  // stop processing 100 responses
            }
               
            mRequestContext.sendResponse(response);            
         }
         break;
         
      case 2:
         terminateClientTransaction(mCurrentResponseTid);
         if (mRequestContext.getOriginalRequest().method() == INVITE)
         {
            cancelAllClientTransactions();
            mRequestContext.mHaveSentFinalResponse = true;
            mBestResponse.header(h_StatusLine).statusCode()=
               response.header(h_StatusLine).statusCode();
            mRequestContext.sendResponse(response);
         }
         else if (!mRequestContext.mHaveSentFinalResponse)
         {
            clearCandidateTransactions();
            mRequestContext.mHaveSentFinalResponse = true;
            mBestResponse.header(h_StatusLine).statusCode()=
               response.header(h_StatusLine).statusCode();
            mRequestContext.sendResponse(response);            
         }
         break;
         
      case 3:
      case 4:
      case 5:
         DebugLog (<< "forwardedFinal=" << mRequestContext.mHaveSentFinalResponse 
                   << " outstanding client transactions: " << Inserter(mActiveTransactionMap));
         terminateClientTransaction(mCurrentResponseTid);
         if (!mRequestContext.mHaveSentFinalResponse)
         {
            int priority = getPriority(response);
            if (priority == mBestPriority)
            {
               if (code == 401 || code == 407)
               {
                  if (response.exists(h_WWWAuthenticates))
                  {
                     for ( Auths::iterator i=response.header(h_WWWAuthenticates).begin(); 
                           i != response.header(h_WWWAuthenticates).end() ; ++i)
                     {                     
                        mBestResponse.header(h_WWWAuthenticates).push_back(*i);
                     }
                  }
                  
                  if (response.exists(h_ProxyAuthenticates))
                  {
                     for ( Auths::iterator i=response.header(h_ProxyAuthenticates).begin(); 
                           i != response.header(h_ProxyAuthenticates).end() ; ++i)
                     {                     
                        mBestResponse.header(h_ProxyAuthenticates).push_back(*i);
                     }
                     mBestResponse.header(h_StatusLine).statusCode() = 407;
                  }
               }
               else if (code / 100 == 3) // merge 3xx
               {

                  if(mBestResponse.header(h_StatusLine).statusCode()/100!=3)
                  {
                     // .bwc. Do not merge contacts in 3xx with contacts from a
                     // previous 4xx or 5xx
                     mBestResponse.header(h_Contacts).clear();
                  }

                  for (NameAddrs::iterator i=response.header(h_Contacts).begin(); 
                       i != response.header(h_Contacts).end(); ++i)
                  {
                     // TODO ?bwc? if we are going to be checking whether
                     // this is "*", we should see if it is well-formed
                     // first. If we shouldn't be doing any checks on
                     // the contacts, we should simply remove all of this
                     // checking code and just blindly copy the contacts over.

                     if(!i->isWellFormed() || i->isAllContacts())
                     {
                        // .bwc. Garbage contact; ignore it.
                        continue;
                     }
                     
                     mBestResponse.header(h_Contacts).push_back(*i);
                  }

                  // ?bwc? it is possible for this code to end up with a 300
                  // with no Contacts in it (because they were all malformed)
                  // Is this acceptable? Also, is 300 the code we want here?
                  mBestResponse.header(h_StatusLine).statusCode() = 300;
               }
            }
            else if (priority < mBestPriority)
            {
               mBestPriority = priority;
               mBestResponse = response;
            }
            
            if (areAllTransactionsTerminated())
            {
               forwardBestResponse();
            }
         }
         break;
         
      case 6:
         terminateClientTransaction(mCurrentResponseTid);
         if (!mRequestContext.mHaveSentFinalResponse)
         {
            if (mBestResponse.header(h_StatusLine).statusCode() / 100 != 6)
            {
               mBestResponse = response;
               mBestPriority=0; //6xx class responses take precedence over all 3xx,4xx, and 5xx
               if (mRequestContext.getOriginalRequest().method() == INVITE)
               {
                  // CANCEL INVITE branches
                  cancelAllClientTransactions();
               }
            }
            
            if (areAllTransactionsTerminated())
            {
               forwardBestResponse();
            }
         }
         break;
         
      default:
         assert(0);
         break;
   }
}

void
ResponseContext::cancelClientTransaction(repro::Target* target)
{
   if (target->status() == Target::Started)
   {
      InfoLog (<< "Cancel client transaction: " << target);
      mRequestContext.getProxy().getStack().cancelClientInviteTransaction(target->via().param(p_branch).getTransactionId());

      DebugLog(<< "Canceling a transaction with uri: " 
               << resip::Data::from(target->uri()) << " , to host: " 
               << target->via().sentHost());
      target->status() = Target::Cancelled;
   }
   else if (target->status() == Target::Candidate)
   {
      target->status() = Target::Terminated;
   }

}

void 
ResponseContext::terminateClientTransaction(const resip::Data& tid)
{

   InfoLog (<< "Terminating client transaction: " << tid << " all = " << areAllTransactionsTerminated());

   TransactionMap::iterator i = mActiveTransactionMap.find(tid);
   if(i != mActiveTransactionMap.end())
   {
      InfoLog (<< "client transactions: " << Inserter(mActiveTransactionMap));
      i->second->status() = Target::Terminated;
      mTerminatedTransactionMap[tid] = i->second;
      mActiveTransactionMap.erase(i);
      return;
   }
   
   TransactionMap::iterator j = mCandidateTransactionMap.find(tid);
   if(j != mCandidateTransactionMap.end())
   {
      InfoLog (<< "client transactions: " << Inserter(mCandidateTransactionMap));
      j->second->status() = Target::Terminated;
      mTerminatedTransactionMap[tid] = j->second;
      mCandidateTransactionMap.erase(j);
      return;   
   }
      
}


int
ResponseContext::getPriority(const resip::SipMessage& msg)
{
   int responseCode = msg.header(h_StatusLine).statusCode();
   int p = 0;  // "p" is the relative priority of the response

      assert(responseCode >= 300 && responseCode <= 599);
      if (responseCode <= 399)  // 3xx response
      { 
         return 5;  // response priority is 5
      }
      if (responseCode >= 500)
      {
         switch(responseCode)
         {
            case 501:	// these four have different priorities
            case 503:   // which are addressed in the case statement
            case 580:	// below (with the 4xx responses)
            case 513:
                  break;
            default:
                  return 42; // response priority of other 5xx is 42
         }
      }

      switch(responseCode)
      {
         // Easy to Repair Responses: 412, 484, 422, 423, 407, 401, 300..399, 402
         case 412:		// Publish ETag was stale
            return 1;
         case 484:		// overlap dialing
            return 2;
         case 422:		// Session-Timer duration too long
         case 423:		// Expires too short
            return 3;
         case 407:		// Proxy-Auth
         case 401:		// UA Digest challenge
            return 4;
                  
         // 3xx responses have p = 5
         case 402:		// Payment required
            return 6;

         // Responses used for negotiation: 493, 429, 420, 406, 415, 488
         case 493:		// Undecipherable, try again unencrypted 
            return 10;

         case 420:		// Required Extension not supported, try again without
            return 12;

         case 406:		// Not Acceptable
         case 415:		// Unsupported Media Type
         case 488:		// Not Acceptable Here
            return 13;
                  
         // Possibly useful for negotiation, but less likely: 421, 416, 417, 494, 580, 485, 405, 501, 413, 414
         
         case 416:		// Unsupported scheme
         case 417:		// Unknown Resource-Priority
            return 20;

         case 405:		// Method not allowed (both used for negotiating REFER, PUBLISH, etc..
         case 501:		// Usually used when the method is not OK
            return 21;

         case 580:		// Preconditions failure
            return 22;

         case 485:		// Ambiguous userpart.  A bit better than 404?
            return 23;

         case 428:		// Use Identity header
         case 429:		// Provide Referrer Identity 
         case 494:		// Use the sec-agree mechanism
            return 24;

         case 413:		// Request too big
         case 414:		// URI too big
            return 25;

         case 421:		// An extension required by the server was not in the Supported header
            return 26;
         
         // The request isn't repairable, but at least we can try to provide some 
         // useful information: 486, 480, 410, 404, 403, 487
         
         case 486:		// Busy Here
            return 30;

         case 480:		// Temporarily unavailable
            return 31;

         case 410:		// Gone
            return 32;

         case 436:		// Bad Identity-Info 
         case 437:		// Unsupported Certificate
         case 513:      // Message too large
            return 33;

         case 403:		// Forbidden
            return 34;

         case 404:		// Not Found
            return 35;

         case 487:		// Some branches were cancelled, if the UAC sent a CANCEL this is good news
            return 36;

         // We are hosed: 503, 483, 482, 481, other 5xx, 400, 491, 408  // totally useless

         case 503:	// bad news, we should never forward this back anyway
            return 43;

         case 483:	// loops, encountered
         case 482:
            return 41;
                  
         // other 5xx   p = 42

         // UAS is seriously confused: p = 43
         // case 481:	
         // case 400:
         // case 491:
         // default:
         
         case 408:	// very, very bad  (even worse than the remaining 4xx responses)
            return 49;
         
         default:
            return 43;
      }
   return p;
}

bool 
ResponseContext::CompareStatus::operator()(const resip::SipMessage& lhs, const resip::SipMessage& rhs) const
{
   assert(lhs.isResponse());
   assert(rhs.isResponse());
   
   // !rwm! replace with correct thingy here
   return lhs.header(h_StatusLine).statusCode() < rhs.header(h_StatusLine).statusCode();
}

void
ResponseContext::forwardBestResponse()
{
   InfoLog (<< "Forwarding best response: " << mBestResponse.brief());
   
   clearCandidateTransactions();
   
   if(mRequestContext.getOriginalRequest().method()==INVITE)
   {
      cancelActiveClientTransactions();
   }
   
   
   if(mBestResponse.header(h_StatusLine).statusCode() == 503)
   {
      //See RFC 3261 sec 16.7, page 110, paragraph 2
      mBestResponse.header(h_StatusLine).statusCode() = 480;
   }

   if(mBestResponse.header(h_StatusLine).statusCode() == 408 &&
      mBestResponse.method()!=INVITE)
   {
      // We don't forward back NIT 408; we just silently abandon the transaction
      DebugLog(<< "Got NIT 408, abandoning: "<<mRequestContext.getTransactionId());
      mRequestContext.getProxy().getStack().abandonServerTransaction(mRequestContext.getTransactionId());
   }
   else
   {
      mRequestContext.sendResponse(mBestResponse);
   }
}


EncodeStream& 
repro::operator<<(EncodeStream& strm, const repro::Target* t)
{
   strm << "Target: " << t->uri() << " " <<" status=" << t->status();
   return strm;
}


EncodeStream&
repro::operator<<(EncodeStream& strm, const ResponseContext& rc)
{
   strm << "ResponseContext: "
        << " identity=" << rc.mRequestContext.getDigestIdentity()
        << " best=" << rc.mBestPriority << " " << rc.mBestResponse.brief()
        << " forwarded=" << rc.mRequestContext.mHaveSentFinalResponse
        << " pending=" << Inserter(rc.mCandidateTransactionMap)
        << " active=" << Inserter(rc.mActiveTransactionMap)
        << " terminated=" << Inserter(rc.mTerminatedTransactionMap);
      //<< " targets=" << Inserter(rc.mTargetSet)
      //<< " clients=" << Inserter(rc.mClientTransactions);

   return strm;
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
