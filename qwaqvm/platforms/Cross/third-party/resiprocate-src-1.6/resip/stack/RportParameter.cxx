#if defined(HAVE_CONFIG_H)
#include "resip/stack/config.hxx"
#endif

#include "resip/stack/RportParameter.hxx"
#include "resip/stack/Symbols.hxx"
#include "rutil/ParseBuffer.hxx"
#include "rutil/Logger.hxx"
#include "rutil/BaseException.hxx"
#include "rutil/WinLeakCheck.hxx"

using namespace resip;
using namespace std;

#define RESIPROCATE_SUBSYSTEM resip::Subsystem::SIP

RportParameter::RportParameter(ParameterTypes::Type type,
                               ParseBuffer& pb, 
                               const char* terminators)
   : Parameter(type),
     mValue(0),
     mHasValue(false)
{
   pb.skipWhitespace();
   if (!pb.eof() && *pb.position() == Symbols::EQUALS[0])
   {
      mHasValue = true;
      
      pb.skipChar(Symbols::EQUALS[0]);
      pb.skipWhitespace();

      // Catch the exception gracefully to handle seeing ;rport=
      try
      {
      mValue = pb.integer();
      }
      catch(BaseException&  e )
      {
         ErrLog(<<"Caught exception: "<< e);
         mHasValue = false;
      }
   }
}

RportParameter::RportParameter(ParameterTypes::Type type, int value)
   : Parameter(type),
     mValue(value),
     mHasValue(true)
{
}

RportParameter::RportParameter(ParameterTypes::Type type)
   : Parameter(type),
     mValue(-1),
     mHasValue(false)
{
}

Parameter* 
RportParameter::clone() const
{
   return new RportParameter(*this);
}

EncodeStream&
RportParameter::encode(EncodeStream& stream) const
{
   if (mHasValue || mValue > 0)
   {
      return stream << getName() << Symbols::EQUALS << mValue;
   }
   else
   {
      return stream << getName();
   }
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
