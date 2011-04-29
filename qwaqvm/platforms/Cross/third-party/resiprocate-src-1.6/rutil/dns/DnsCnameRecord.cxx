#if defined(HAVE_CONFIG_H)
#include "rutil/config.hxx"
#endif

#include <stdlib.h>

#include "AresCompat.hxx"

#ifndef __CYGWIN__
#ifndef RRFIXEDSZ
#define RRFIXEDSZ 10
#endif
#ifndef NS_RRFIXEDSZ
#define NS_RRFIXEDSZ 10
#endif
#endif

#include "rutil/Data.hxx"
#include "rutil/BaseException.hxx"
#include "RROverlay.hxx"
#include "DnsResourceRecord.hxx"
#include "DnsCnameRecord.hxx"

using namespace resip;

DnsCnameRecord::DnsCnameRecord(const RROverlay& overlay)
{
   char* name = 0;
   long len = 0;
   if (ARES_SUCCESS != ares_expand_name(overlay.data()-overlay.nameLength()-RRFIXEDSZ, overlay.msg(), overlay.msgLength(), &name, &len))
   {
      throw CnameException("Failed parse of CNAME record", __FILE__, __LINE__);
   }
   mName = name;
   free(name);

   if (ARES_SUCCESS != ares_expand_name(overlay.data(), overlay.msg(), overlay.msgLength(), &name, &len))
   {
      throw CnameException("Failed parse of CNAME record", __FILE__, __LINE__);
   }
   
   mCname = name;
   free(name);
}

bool DnsCnameRecord::isSameValue(const Data& value) const
{
   return mCname==value;
}

EncodeStream&
DnsCnameRecord::dump(EncodeStream& strm) const
{
   strm << mName << " (CNAME) --> " << mCname;
   return strm;
}
