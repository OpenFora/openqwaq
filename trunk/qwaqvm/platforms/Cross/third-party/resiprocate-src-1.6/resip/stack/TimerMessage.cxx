#if defined(HAVE_CONFIG_H)
#include "resip/stack/config.hxx"
#endif

#include <cassert>
#include "resip/stack/TimerMessage.hxx"

using namespace resip;

TimerMessage::TimerMessage(Data transactionId, Timer::Type type, unsigned long duration)
   : mTransactionId(transactionId),
     mType(type),
     mDuration(duration) {}

const Data&
TimerMessage::getTransactionId() const
{
   return mTransactionId;
}

Timer::Type 
TimerMessage::getType() const
{
   return mType;
}

unsigned long
TimerMessage::getDuration() const 
{
   return mDuration;
}

TimerMessage::~TimerMessage()
{}

bool 
TimerMessage::isClientTransaction() const
{
   switch (mType)
   {
      case Timer::TimerA:
      case Timer::TimerB:
      case Timer::TimerD:
      case Timer::TimerE1:
      case Timer::TimerE2:
      case Timer::TimerF:
      case Timer::TimerK:
      case Timer::TimerStaleClient:
      case Timer::TimerCleanUp:
      case Timer::TimerStateless:
         return true;

      case Timer::TimerG:
      case Timer::TimerH:
      case Timer::TimerI:
      case Timer::TimerJ:
      case Timer::TimerStaleServer:
      case Timer::TimerTrying:
         return false;

      case Timer::TimerC:
         assert(0);
         break;

	  default:
         assert(0);
         break;
   }
   assert(0);
   return false;
}



EncodeStream&
TimerMessage::encodeBrief(EncodeStream& str) const
{
   return str << "Timer: " << Timer::toData(mType) << " " << mDuration;
}

EncodeStream& TimerMessage::encode(EncodeStream& strm) const
{
   return strm << "TimerMessage TransactionId[" << mTransactionId << "] "
               << " Type[" << Timer::toData(mType) << "]"
               << " duration[" << mDuration << "]";
}

/* ====================================================================
 * The Vovida Software License, Version 1.0 
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
