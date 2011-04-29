#if !defined(Thread_hxx)
#define Thread_hxx 

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <winsock2.h>
#undef WIN32_LEAN_AND_MEAN
#else
#  include <pthread.h>
#endif

namespace gateway
{

class Thread
{
   public:
      Thread();
      virtual ~Thread();

      virtual void run();
      void join();
      virtual void shutdown();
      bool isShutdown() const;

#ifdef WIN32
      typedef unsigned int Id;
#else
      typedef pthread_t Id;
#endif

      virtual void thread() = 0;

   protected:
#ifdef WIN32
      HANDLE mThread;
#endif
      Id mId;
      volatile bool mShutdown;

   private:
      // Suppress copying
      Thread(const Thread &);
      const Thread & operator=(const Thread &);
};

}

#endif  

/* ====================================================================

 Copyright (c) 2009, SIP Spectrum, Inc.
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are 
 met:

 1. Redistributions of source code must retain the above copyright 
    notice, this list of conditions and the following disclaimer. 

 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution. 

 3. Neither the name of SIP Spectrum nor the names of its contributors 
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

