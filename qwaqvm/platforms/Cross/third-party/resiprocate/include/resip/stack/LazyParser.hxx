#if !defined(RESIP_LAZYPARSER_HXX)
#define RESIP_LAZYPARSER_HXX 

#include <iosfwd>
#include "resip/stack/HeaderFieldValue.hxx"
#include "rutil/resipfaststreams.hxx"

namespace resip
{

class ParseBuffer;
class Data;

/**
   @ingroup resip_crit
   @ingroup sip_parse
   @brief The base-class for all lazily-parsed SIP grammar elements.

   Subclasses of this are parse-on-access; the parse will be carried out (if
   it hasn't already) the first time one of the members is accessed. Right now,
   all header-field-values and SIP bodies are lazily parsed.
*/
class LazyParser
{
   public:
      LazyParser(HeaderFieldValue* headerFieldValue);
      LazyParser(const LazyParser& rhs);
      LazyParser(const LazyParser& rhs,HeaderFieldValue::CopyPaddingEnum e);
      LazyParser& operator=(const LazyParser& rhs);
      virtual ~LazyParser();

      virtual EncodeStream& encodeParsed(EncodeStream& str) const = 0;
      virtual void parse(ParseBuffer& pb) = 0;

      EncodeStream& encode(EncodeStream& str) const;
      EncodeStream& encodeFromHeaderFieldValue(EncodeStream& str) const;
      bool isParsed() const {return (mState!=NOT_PARSED);}

      HeaderFieldValue& getHeaderField() { return *mHeaderField; }

      // call (internally) before every access 
      void checkParsed() const;
      
      bool isWellFormed() const;
   protected:
      LazyParser();

      // called in destructor and on assignment 
      void clear();

      // context for error messages
      virtual const Data& errorContext() const = 0;
      
   private:
      // !dlb! bit of a hack until the dust settles
      friend class Contents;

      HeaderFieldValue* mHeaderField;

      typedef enum
      {
         NOT_PARSED,
         WELL_FORMED,
         MALFORMED,
         EMPTY 
      } ParseState;
      ParseState mState;
      bool mIsMine;
};

#ifndef  RESIP_USE_STL_STREAMS
EncodeStream&
operator<<(EncodeStream&, const LazyParser& lp);
#endif

//need this for MD5Stream
std::ostream&
operator<<(std::ostream&, const LazyParser& lp);

}

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
