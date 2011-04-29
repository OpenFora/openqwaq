#if defined(HAVE_CONFIG_H)
#include "resip/stack/config.hxx"
#endif

#include "resip/stack/Message.hxx"
#include "rutil/DataStream.hxx"
#include <cassert>

using namespace resip;


Message::Message() : tu(0) 
{}

Message::Brief
Message::brief() const
{
   return Message::Brief(*this);
}

Message::Brief::Brief(const Message& source) :
   mSource(source)
{}

std::ostream& 
resip::operator<<(std::ostream& strm, 
                  const resip::Message& msg)
{

   // .dlb. what a bad idea..
   // escaping should be handled at Data 
   // msg scanner should indicate that it saw escaped characters
   // Data's coming from a marked buffer should assume they need to be escaped.
   // Data's coming from user should assume they need to be escaped.
   // Otherwise, Data's should NOT escape.

   Data encoded;

   DataStream encodeStream(encoded);
   msg.encode(encodeStream);
   encodeStream.flush();

  // strm << encoded.escaped();
	
	strm << encoded.c_str();
   //msg.encode(strm);
   
   return strm;
}

std::ostream& 
resip::operator<<(std::ostream& strm, 
                  const resip::Message::Brief& brief)
{
	Data encoded;

   DataStream encodeStream(encoded);
   brief.mSource.encodeBrief(encodeStream);
   encodeStream.flush();

   strm << encoded.c_str();

   return strm;
}

#ifndef  RESIP_USE_STL_STREAMS
EncodeStream& 
resip::operator<<(EncodeStream& strm, 
                  const resip::Message& msg)
{
msg.encode(strm);
return strm;
}

EncodeStream& 
resip::operator<<(EncodeStream& strm, 
                  const resip::Message::Brief& brief)
{
   return brief.mSource.encodeBrief(strm);
}
#endif

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
