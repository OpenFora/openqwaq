#if !defined(RESIP_FiniteFifo_hxx)
#define RESIP_FiniteFifo_hxx 

#include "rutil/AbstractFifo.hxx"

// efficiency note: use a circular buffer do avoid list node allocation

// what happens to timers that can't be queued?

namespace resip
{

/**
   @brief A templated, threadsafe message-queue class with a fixed size.
*/
template < class Msg >
class FiniteFifo : public AbstractFifo
{
   public:
      FiniteFifo(unsigned int maxSize);
      virtual ~FiniteFifo();
      
      // Add a message to the fifo.
      // return true if succeed, false if full
      bool add(Msg* msg);

      /** Returns the first message available. It will wait if no
       *  messages are available. If a signal interrupts the wait,
       *  it will retry the wait. Signals can therefore not be caught
       *  via getNext. If you need to detect a signal, use block
       *  prior to calling getNext.
       */
      Msg* getNext();
};

template <class Msg>
FiniteFifo<Msg>::FiniteFifo(unsigned int maxSize)
   : AbstractFifo(maxSize)
{
}

template <class Msg>
FiniteFifo<Msg>::~FiniteFifo()
{
   Lock lock(mMutex); (void)lock;
   while ( ! mFifo.empty() )
   {
      delete static_cast<Msg*>(mFifo.front());
      mFifo.pop_front();
   }
   mSize = NoSize;
}

template <class Msg>
bool
FiniteFifo<Msg>::add(Msg* msg)
{
   Lock lock(mMutex); (void)lock;
   if (mMaxSize != NoSize &&
       mSize >= mMaxSize)
   {
      return false;
   }
   else
   {
      mFifo.push_back(msg);
      mSize++;
      mCondition.signal();
      return true;
   }
}

template <class Msg>
Msg*
FiniteFifo<Msg> ::getNext()
{
   return static_cast<Msg*>(AbstractFifo::getNext());
}

} // namespace resip

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
