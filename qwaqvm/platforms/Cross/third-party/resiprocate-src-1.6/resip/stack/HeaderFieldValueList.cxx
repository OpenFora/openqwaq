#if defined(HAVE_CONFIG_H)
#include "resip/stack/config.hxx"
#endif


#include <cassert>

#include "resip/stack/HeaderFieldValue.hxx"
#include "resip/stack/HeaderFieldValueList.hxx"
#include "resip/stack/ParserContainerBase.hxx"
#include "resip/stack/Embedded.hxx"
#include "rutil/WinLeakCheck.hxx"

using namespace resip;

HeaderFieldValueList::~HeaderFieldValueList()
{
   for (iterator i = begin(); i != end(); i++)
   {
      delete *i;
   }
   delete mParserContainer;
}

HeaderFieldValueList::HeaderFieldValueList(const HeaderFieldValueList& rhs)
   : mHeaders(0),
     mParserContainer(0)
{
   if (rhs.mParserContainer != 0)
   {
      mParserContainer = rhs.mParserContainer->clone();
   }
   else
   {
      for (const_iterator i = rhs.begin(); i != rhs.end(); i++)
      {
         push_back(new HeaderFieldValue(**i));
      }
   }
}

EncodeStream&
HeaderFieldValueList::encode(int headerEnum, EncodeStream& str)
{
   const Data& headerName = Headers::getHeaderName(static_cast<Headers::Type>(headerEnum));

   if (getParserContainer() != 0)
   {
      getParserContainer()->encode(headerName, str);
   }
   else
   {
      if (!headerName.empty())
      {
         str << headerName << Symbols::COLON[0] << Symbols::SPACE[0];
      }

      for (HeaderFieldValueList::const_iterator j = begin();
           j != end(); j++)
      {
         if (j != begin())
         {
            if (Headers::isCommaEncoding(static_cast<Headers::Type>(headerEnum)))
            {
               str << Symbols::COMMA[0] << Symbols::SPACE[0];
            }
            else
            {
               str << Symbols::CRLF << headerName << Symbols::COLON << Symbols::SPACE;
            }
         }
         (*j)->encode(str);
      }
      str << Symbols::CRLF;
   }
   return str;
}

EncodeStream&
HeaderFieldValueList::encode(const Data& headerName, EncodeStream& str)
{
   if (getParserContainer() != 0)
   {
      getParserContainer()->encode(headerName, str);
   }
   else
   {
      if (!headerName.empty())
      {
         str << headerName << Symbols::COLON << Symbols::SPACE;
      }
      for (HeaderFieldValueList::const_iterator j = begin();
           j != end(); j++)
      {
         if (j != begin())
         {
            str << Symbols::COMMA[0] << Symbols::SPACE[0];
         }
         (*j)->encode(str);
      }
      str << Symbols::CRLF;
   }
   return str;
}

EncodeStream&
HeaderFieldValueList::encodeEmbedded(const Data& headerName, EncodeStream& str)
{
  assert(!headerName.empty());

   if (getParserContainer() != 0)
   {
      getParserContainer()->encodeEmbedded(headerName, str);
   }
   else
   {
      bool first = true;
      for (HeaderFieldValueList::const_iterator j = begin();
           j != end(); j++)
      {
         if (first)
         {
            first = false;
         }
         else
         {
            str << Symbols::AMPERSAND;
         }

         str << headerName << Symbols::EQUALS;
         Data buf;
         {
            DataStream s(buf);
            (*j)->encode(s);
         }
         str << Embedded::encode(buf);
      }
   }
   return str;
}

bool
HeaderFieldValueList::parsedEmpty() const
{
   if (mParserContainer)
   {
      return mParserContainer->empty();
   }
   else
   {
      return mHeaders.empty();
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
