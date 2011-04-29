#include <iostream>
#include "resip/stack/TransactionMessage.hxx"
#include "resip/stack/TimerQueue.hxx"
#include "resip/stack/TuSelector.hxx"
#include "rutil/Fifo.hxx"
#include "rutil/TimeLimitFifo.hxx"
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#define sleep(x) Sleep(x*1000)
#endif

using namespace resip;
using namespace std;

bool
isNear(int value, int reference, int epsilon=250)
{
   int diff = ::abs(value-reference);
   return (diff < epsilon);
}


int
main()
{

   TimeLimitFifo<Message> f(0, 0);
   Fifo<TransactionMessage> r;
   
   TimerQueue timer(r);
   TimeLimitTimerQueue timer2(f);

   cerr << "Before Fifo size: " << f.size() << endl;
   assert(f.size() == 0);
   cerr << "next timer = " << timer.msTillNextTimer() << endl;
   assert(timer.msTillNextTimer() == INT_MAX);

   // throw a few events in the queue
   timer.add(Timer::TimerA, "first", 1000);
   cerr << "next timer will fire in " << timer.msTillNextTimer() << "ms" << endl;
   assert(isNear(timer.msTillNextTimer(), 1000));
   timer.add(Timer::TimerA, "second", 2000);
   assert(isNear(timer.msTillNextTimer(), 1000));
   timer.add(Timer::TimerA, "third", 4000);
   assert(isNear(timer.msTillNextTimer(), 1000));
   timer.add(Timer::TimerA, "fourth", 8000);
   assert(isNear(timer.msTillNextTimer(), 1000));
   timer.add(Timer::TimerA, "fifth", 16000);
   assert(isNear(timer.msTillNextTimer(), 1000));

   cerr << timer;
   assert(r.size() == 0);
   assert(timer.size() == 5);
   
   timer.process();

   assert(r.size() == 0);
   assert(timer.size() == 5);

   cerr << timer;
   sleep(1);
   cerr << timer;
   timer.process();
   
   cerr << timer;
   assert(r.size() == 1);
   assert(timer.size() == 4);


   cerr << "next timer will fire in " << timer.msTillNextTimer() << "ms" << endl;
   timer.process();
   cerr << "next timer will fire in " << timer.msTillNextTimer() << "ms" << endl;
   cerr << "timer queue size=" << timer.size() << endl;
   cerr << "fired event queue size=" << r.size() << endl;
   assert(r.size() == 1);
   timer.process();   
   assert(r.size() == 1);

   sleep(1);
   timer.process();
   assert(r.size() == 2);
   timer.process();   
   assert(r.size() == 2);

   sleep(2);
   timer.process();
   assert(r.size() == 3);
   timer.process();   
   assert(r.size() == 3);

   sleep(4);
   timer.process();
   assert(r.size() == 4);
   timer.process();   
   assert(r.size() == 4);


   cerr << "next timer will fire in " << timer.msTillNextTimer() << "ms" << endl;
   assert(isNear(timer.msTillNextTimer(), 8000));

   sleep(8);
   timer.process();
   cerr << timer;
   
   assert(r.size() == 5);
   timer.process();   
   assert(r.size() == 5);

   sleep(1);
   timer.process();
   assert(r.size() == 5);
   timer.process();   
   assert(r.size() == 5);

   cerr << "All OK" << endl;
   return 0;
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
