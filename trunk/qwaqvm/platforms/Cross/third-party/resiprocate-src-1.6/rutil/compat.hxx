#if !defined(RESIP_COMPAT_HXX)
#define RESIP_COMPAT_HXX 

/**
   @file
   This file is used to handle compatibility fixes/tweaks so reSIProcate can 
   function on multiple platforms.
*/

#if defined(__INTEL_COMPILER ) && defined( __OPTIMIZE__ )
#  undef __OPTIMIZE__ // wierd intel bug with ntohs and htons macros
#endif

//#if defined(HAVE_SYS_INT_TYPES_H)
//#include <sys/int_types.h>
//#endif

#include <cstring>

#ifndef WIN32
#  include <netdb.h>
#  include <sys/types.h>
#  include <sys/time.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <pthread.h>
#  include <limits.h>
#endif

#ifdef WIN32
// !cj! TODO would be nice to remove this 
#  ifndef __GNUC__
#    pragma warning(disable : 4996)
#  endif
#define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <winsock2.h>
#undef WIN32_LEAN_AND_MEAN
#  include <errno.h>
#  include <io.h>
#ifdef UNDER_CE
#include "wince/WceCompat.hxx"
#endif // UNDER_CE
#endif

#if defined(__APPLE__) 
#  if !defined(MAC_OS_X_VERSION_MIN_REQUIRED) || MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_2
      // you don't include the SDK or you're running 10.3 or above
      // note: this will fail on 10.2 if you don't include the SDK
#     include <arpa/nameser_compat.h>
#  else
      // you include the SDK and you're running Mac OS 10.2 or below
      typedef int socklen_t;
#  endif
#  ifdef __MWERKS__ /* this is a <limits.h> bug filed with Apple, Radar# 3657629. */
#    ifndef __SCHAR_MAX__ 
#      define __SCHAR_MAX__ 127
#    endif
#  endif
#endif

#if defined(__SUNPRO_CC)
#  if defined(_TIME_T)
    using std::time_t;
#  endif
#  include <time.h>
#  include <memory.h>
#  include <string.h>
#endif

#if !defined(T_NAPTR)
#  define T_NAPTR 35
#endif

#if !defined(T_SRV)
#  define T_SRV 33
#endif

#if !defined(T_AAAA)
#  define T_AAAA 28
#endif

#if !defined(T_A)
#  define T_A 1
#endif

namespace resip
{

#if defined(WIN32) || defined(__QNX__)
#ifndef strcasecmp
#  define strcasecmp(a,b)    stricmp(a,b)
#endif
#ifndef strncasecmp
#  define strncasecmp(a,b,c) strnicmp(a,b,c)
#endif
#endif

#if defined(__QNX__) || defined(__sun) || defined(WIN32)
  typedef unsigned int u_int32_t;
#endif

template<typename _Tp>
inline const _Tp&
resipMin(const _Tp& __a, const _Tp& __b)
{
   if (__b < __a) return __b; return __a;
}

template<typename _Tp>
inline const _Tp&
resipMax(const _Tp& __a, const _Tp& __b) 
{
   if (__a < __b) return __b; return __a;
}

}

// Mac OS X: UInt32 definition conflicts with the Mac OS SDK.
// If you've included the SDK then TARGET_OS_MAC will be defined.
#ifndef TARGET_OS_MAC
typedef unsigned char  UInt8;
typedef unsigned short UInt16;
typedef unsigned int   UInt32;
#endif

#if defined( WIN32 )
  typedef unsigned __int64 UInt64;
#else
  typedef unsigned long long UInt64;
#endif
//typedef struct { unsigned char octet[16]; }  UInt128;

//template "levels; ie REASONABLE and COMPLETE
//reasonable allows most things such as partial template specialization,
//etc...like most compilers and VC++2003+.
//COMPLETE would allow template metaprogramming, template< template< > > tricks,
//etc...REASONABLE should always be defined when COMPLETE is defined.

//#if !defined(__SUNPRO_CC) && !defined(__INTEL_COMPILER)
#if !defined(__INTEL_COMPILER)
#  define REASONABLE_TEMPLATES
#endif

// .bwc. This is the only place we check for USE_IPV6 in a header file. This 
// code has no effect if USE_IPV6 is not set, so this should only kick in when
// we're building the resip libs. If someone sets USE_IPV6 while building
// against the resip libs, no resip header file will care. 
#ifdef USE_IPV6
#ifndef IPPROTO_IPV6
#if(_WIN32_WINNT >= 0x0501)   // Some versions of the windows SDK define IPPROTO_IPV6 differently - always enable IP v6 if USE_IPV6 and _WIN32_WINNT >= 0x0501
#define IPPROTO_IPV6 ::IPPROTO_IPV6  
#else
#ifdef _MSC_VER
#define __STR2__(x) #x
#define __STR1__(x) __STR2__(x)
#define __LOC__ __FILE__ "("__STR1__(__LINE__)"): "
#pragma message (__LOC__ " IPv6 support requested, but IPPROTO_V6 undefined; this platform does not appear to support IPv6 ")
#else
#warning IPv6 support requested, but IPPROTO_IPV6 undefined; this platform does not appear to support IPv6
#endif
// .bwc. Don't do this; someone might have defined it for their own code.
// #undef USE_IPV6
#endif
#endif
#endif

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
 */
