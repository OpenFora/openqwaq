#if !defined(RESIP_TRANSACTION_CONTROLLER_HXX)
#define RESIP_TRANSACTION_CONTROLLER_HXX

#include "resip/stack/TuSelector.hxx"
#include "resip/stack/TransactionMap.hxx"
#include "resip/stack/TransportSelector.hxx"
#include "resip/stack/TimerQueue.hxx"

namespace resip
{

class TransactionMessage;
class ApplicationMessage;
class StatisticsManager;
class SipStack;
class Compression;

class TransactionController
{
   public:
      // set after starting at your peril
      static unsigned int MaxTUFifoSize;
      static unsigned int MaxTUFifoTimeDepthSecs;

      TransactionController(SipStack& stack);
      ~TransactionController();

      void process(FdSet& fdset);
      unsigned int getTimeTillNextProcessMS();
      void buildFdSet(FdSet& fdset);
      
      // graceful shutdown (eventually)
      void shutdown();

      TransportSelector& transportSelector() { return mTransportSelector; }
      const TransportSelector& transportSelector() const { return mTransportSelector; }

      bool isTUOverloaded() const;
      
      void send(SipMessage* msg);

      unsigned int getTuFifoSize() const;
      unsigned int sumTransportFifoSizes() const;
      unsigned int getTransactionFifoSize() const;
      unsigned int getNumClientTransactions() const;
      unsigned int getNumServerTransactions() const;
      unsigned int getTimerQueueSize() const;
      //void setStatisticsInterval(unsigned long seconds) const;

      void registerMarkListener(MarkListener* listener);
      void unregisterMarkListener(MarkListener* listener);

      inline bool getFixBadDialogIdentifiers() const 
      {
         return mFixBadDialogIdentifiers;
      }

      inline void setFixBadDialogIdentifiers(bool pFixBadDialogIdentifiers) 
      {
         mFixBadDialogIdentifiers = pFixBadDialogIdentifiers;
      }

      inline bool getFixBadCSeqNumbers() const { return mFixBadCSeqNumbers;} 
      inline void setFixBadCSeqNumbers(bool pFixBadCSeqNumbers)
      {
         mFixBadCSeqNumbers = pFixBadCSeqNumbers;
      }

      void abandonServerTransaction(const Data& tid);

      void cancelClientInviteTransaction(const Data& tid);

   private:
      TransactionController(const TransactionController& rhs);
      TransactionController& operator=(const TransactionController& rhs);
      SipStack& mStack;
      
      // If true, indicate to the Transaction to ignore responses for which
      // there is no transaction. 
      // !jf! Probably should transmit stray responses statelessly. see RFC3261
      bool mDiscardStrayResponses;

      bool mFixBadDialogIdentifiers;

      bool mFixBadCSeqNumbers;
      // fifo used to communicate to the transaction state machine within the
      // stack. Not for external use by the application. May contain, sip
      // messages (requests and responses), timers (used by state machines),
      // asynchronous dns responses, transport errors from the underlying
      // transports, etc. 
      Fifo<TransactionMessage> mStateMacFifo;

      // from the sipstack (for convenience)
      TuSelector& mTuSelector;

      // Used to decide which transport to send a sip message on. 
      TransportSelector mTransportSelector;

      // stores all of the transactions that are currently active in this stack 
      TransactionMap mClientTransactionMap;
      TransactionMap mServerTransactionMap;

      // timers associated with the transactions. When a timer fires, it is
      // placed in the mStateMacFifo
      TimerQueue  mTimers;

      bool mShuttingDown;
      
      StatisticsManager& mStatsManager;

      friend class SipStack; // for debug only
      friend class StatelessHandler;
      friend class TransactionState;
      friend class TransportSelector;

      friend class TestDnsResolver;
      friend class TestFSM;
};


}


#endif
/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2004 Vovida Networks, Inc.  All rights reserved.
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
