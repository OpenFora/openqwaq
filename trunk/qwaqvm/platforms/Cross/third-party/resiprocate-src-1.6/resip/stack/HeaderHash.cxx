/* C++ code produced by gperf version 3.0.3 */
/* Command-line: gperf -D -E -L C++ -t -k '*' --compare-strncmp -Z HeaderHash HeaderHash.gperf  */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

#line 1 "HeaderHash.gperf"

#include <string.h>
#include <ctype.h>
#include "resip/stack/HeaderTypes.hxx"

namespace resip
{
using namespace std;
#line 10 "HeaderHash.gperf"
struct headers { const char *name; Headers::Type type; };
/* maximum key range = 430, duplicates = 4 */

class HeaderHash
{
private:
  static inline unsigned int hash (const char *str, unsigned int len);
public:
  static struct headers *in_word_set (const char *str, unsigned int len);
};

inline unsigned int
HeaderHash::hash (register const char *str, register unsigned int len)
{
  static unsigned short asso_values[] =
    {
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431,   0, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431,   0,  55,  15,
        0,  30,  35,  10,   0,  25,   5,  70,  40,  75,
        0,   5,   0,  25,  10,  50,   0,  10,  45,  15,
       60,  20,  25, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431, 431, 431, 431, 431,
      431, 431, 431, 431, 431, 431
    };
  register int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)tolower(str[24])];
      /*FALLTHROUGH*/
      case 24:
        hval += asso_values[(unsigned char)tolower(str[23])];
      /*FALLTHROUGH*/
      case 23:
        hval += asso_values[(unsigned char)tolower(str[22])];
      /*FALLTHROUGH*/
      case 22:
        hval += asso_values[(unsigned char)tolower(str[21])];
      /*FALLTHROUGH*/
      case 21:
        hval += asso_values[(unsigned char)tolower(str[20])];
      /*FALLTHROUGH*/
      case 20:
        hval += asso_values[(unsigned char)tolower(str[19])];
      /*FALLTHROUGH*/
      case 19:
        hval += asso_values[(unsigned char)tolower(str[18])];
      /*FALLTHROUGH*/
      case 18:
        hval += asso_values[(unsigned char)tolower(str[17])];
      /*FALLTHROUGH*/
      case 17:
        hval += asso_values[(unsigned char)tolower(str[16])];
      /*FALLTHROUGH*/
      case 16:
        hval += asso_values[(unsigned char)tolower(str[15])];
      /*FALLTHROUGH*/
      case 15:
        hval += asso_values[(unsigned char)tolower(str[14])];
      /*FALLTHROUGH*/
      case 14:
        hval += asso_values[(unsigned char)tolower(str[13])];
      /*FALLTHROUGH*/
      case 13:
        hval += asso_values[(unsigned char)tolower(str[12])];
      /*FALLTHROUGH*/
      case 12:
        hval += asso_values[(unsigned char)tolower(str[11])];
      /*FALLTHROUGH*/
      case 11:
        hval += asso_values[(unsigned char)tolower(str[10])];
      /*FALLTHROUGH*/
      case 10:
        hval += asso_values[(unsigned char)tolower(str[9])];
      /*FALLTHROUGH*/
      case 9:
        hval += asso_values[(unsigned char)tolower(str[8])];
      /*FALLTHROUGH*/
      case 8:
        hval += asso_values[(unsigned char)tolower(str[7])];
      /*FALLTHROUGH*/
      case 7:
        hval += asso_values[(unsigned char)tolower(str[6])];
      /*FALLTHROUGH*/
      case 6:
        hval += asso_values[(unsigned char)tolower(str[5])];
      /*FALLTHROUGH*/
      case 5:
        hval += asso_values[(unsigned char)tolower(str[4])];
      /*FALLTHROUGH*/
      case 4:
        hval += asso_values[(unsigned char)tolower(str[3])];
      /*FALLTHROUGH*/
      case 3:
        hval += asso_values[(unsigned char)tolower(str[2])];
      /*FALLTHROUGH*/
      case 2:
        hval += asso_values[(unsigned char)tolower(str[1])];
      /*FALLTHROUGH*/
      case 1:
        hval += asso_values[(unsigned char)tolower(str[0])];
        break;
    }
  return hval;
}

struct headers *
HeaderHash::in_word_set (register const char *str, register unsigned int len)
{
  enum
    {
      TOTAL_KEYWORDS = 104,
      MIN_WORD_LENGTH = 1,
      MAX_WORD_LENGTH = 25,
      MIN_HASH_VALUE = 1,
      MAX_HASH_VALUE = 430
    };

  static struct headers wordlist[] =
    {
#line 20 "HeaderHash.gperf"
      {"t", Headers::To},
#line 91 "HeaderHash.gperf"
      {"path", Headers::Path},
#line 26 "HeaderHash.gperf"
      {"o", Headers::Event},
#line 36 "HeaderHash.gperf"
      {"to", Headers::To},
#line 22 "HeaderHash.gperf"
      {"r", Headers::ReferTo},
#line 16 "HeaderHash.gperf"
      {"c", Headers::ContentType},
#line 25 "HeaderHash.gperf"
      {"y", Headers::Identity},
#line 12 "HeaderHash.gperf"
      {"i", Headers::CallID},
#line 14 "HeaderHash.gperf"
      {"e", Headers::ContentEncoding},
#line 52 "HeaderHash.gperf"
      {"date", Headers::Date},
#line 17 "HeaderHash.gperf"
      {"f", Headers::From},
#line 85 "HeaderHash.gperf"
      {"join", Headers::Join},
#line 92 "HeaderHash.gperf"
      {"join", Headers::Join},
#line 15 "HeaderHash.gperf"
      {"l", Headers::ContentLength},
#line 29 "HeaderHash.gperf"
      {"contact", Headers::Contact},
#line 21 "HeaderHash.gperf"
      {"v", Headers::Via},
#line 18 "HeaderHash.gperf"
      {"s", Headers::Subject},
#line 23 "HeaderHash.gperf"
      {"b", Headers::ReferredBy},
#line 82 "HeaderHash.gperf"
      {"hide", Headers::UNKNOWN},
#line 34 "HeaderHash.gperf"
      {"route", Headers::Route},
#line 24 "HeaderHash.gperf"
      {"x", Headers::SessionExpires},
#line 38 "HeaderHash.gperf"
      {"accept", Headers::Accept},
#line 75 "HeaderHash.gperf"
      {"warning", Headers::Warning},
#line 19 "HeaderHash.gperf"
      {"k", Headers::Supported},
#line 37 "HeaderHash.gperf"
      {"via", Headers::Via},
#line 13 "HeaderHash.gperf"
      {"m", Headers::Contact},
#line 47 "HeaderHash.gperf"
      {"content-id", Headers::ContentId},
#line 95 "HeaderHash.gperf"
      {"rack", Headers::RAck},
#line 96 "HeaderHash.gperf"
      {"reason", Headers::Reason},
#line 58 "HeaderHash.gperf"
      {"priority", Headers::Priority},
#line 43 "HeaderHash.gperf"
      {"allow", Headers::Allow},
#line 83 "HeaderHash.gperf"
      {"identity", Headers::Identity},
#line 39 "HeaderHash.gperf"
      {"accept-contact", Headers::AcceptContact},
#line 81 "HeaderHash.gperf"
      {"event", Headers::Event},
#line 50 "HeaderHash.gperf"
      {"content-type", Headers::ContentType},
#line 63 "HeaderHash.gperf"
      {"reply-to", Headers::ReplyTo},
#line 69 "HeaderHash.gperf"
      {"supported", Headers::Supported},
#line 80 "HeaderHash.gperf"
      {"encryption", Headers::UNKNOWN},
#line 57 "HeaderHash.gperf"
      {"organization", Headers::Organization},
#line 78 "HeaderHash.gperf"
      {"authorization", Headers::Authorization},
#line 106 "HeaderHash.gperf"
      {"rseq", Headers::RSeq},
#line 94 "HeaderHash.gperf"
      {"privacy", Headers::Privacy},
#line 67 "HeaderHash.gperf"
      {"sip-etag", Headers::SIPETag},
#line 27 "HeaderHash.gperf"
      {"cseq", Headers::CSeq},
#line 73 "HeaderHash.gperf"
      {"unsupported", Headers::Unsupported},
#line 28 "HeaderHash.gperf"
      {"call-id", Headers::CallID},
#line 97 "HeaderHash.gperf"
      {"refer-to",Headers::ReferTo},
#line 32 "HeaderHash.gperf"
      {"from", Headers::From},
#line 62 "HeaderHash.gperf"
      {"record-route", Headers::RecordRoute},
#line 100 "HeaderHash.gperf"
      {"reject-contact", Headers::RejectContact},
#line 53 "HeaderHash.gperf"
      {"error-info", Headers::ErrorInfo},
#line 54 "HeaderHash.gperf"
      {"in-reply-to", Headers::InReplyTo},
#line 93 "HeaderHash.gperf"
      {"target-dialog", Headers::TargetDialog},
#line 30 "HeaderHash.gperf"
      {"content-length", Headers::ContentLength},
#line 64 "HeaderHash.gperf"
      {"require", Headers::Require},
#line 74 "HeaderHash.gperf"
      {"user-agent", Headers::UserAgent},
#line 48 "HeaderHash.gperf"
      {"content-encoding", Headers::ContentEncoding},
#line 42 "HeaderHash.gperf"
      {"alert-info",Headers::AlertInfo},
#line 65 "HeaderHash.gperf"
      {"retry-after", Headers::RetryAfter},
#line 40 "HeaderHash.gperf"
      {"accept-encoding", Headers::AcceptEncoding},
#line 49 "HeaderHash.gperf"
      {"content-language", Headers::ContentLanguage},
#line 45 "HeaderHash.gperf"
      {"call-info", Headers::CallInfo},
#line 76 "HeaderHash.gperf"
      {"www-authenticate",Headers::WWWAuthenticate},
#line 35 "HeaderHash.gperf"
      {"subject", Headers::Subject},
#line 41 "HeaderHash.gperf"
      {"accept-language", Headers::AcceptLanguage},
#line 84 "HeaderHash.gperf"
      {"identity-info", Headers::IdentityInfo},
#line 66 "HeaderHash.gperf"
      {"server", Headers::Server},
#line 99 "HeaderHash.gperf"
      {"replaces",Headers::Replaces},
#line 112 "HeaderHash.gperf"
      {"min-se", Headers::MinSE},
#line 115 "HeaderHash.gperf"
      {"history-info", Headers::HistoryInfo},
#line 44 "HeaderHash.gperf"
      {"authentication-info", Headers::AuthenticationInfo},
#line 88 "HeaderHash.gperf"
      {"p-called-party-id", Headers::PCalledPartyId},
#line 101 "HeaderHash.gperf"
      {"p-called-party-id", Headers::PCalledPartyId},
#line 31 "HeaderHash.gperf"
      {"expires", Headers::Expires},
#line 60 "HeaderHash.gperf"
      {"proxy-authorization", Headers::ProxyAuthorization},
#line 114 "HeaderHash.gperf"
      {"remote-party-id", Headers::RemotePartyId},
#line 59 "HeaderHash.gperf"
      {"proxy-authenticate", Headers::ProxyAuthenticate},
#line 71 "HeaderHash.gperf"
      {"answer-mode", Headers::AnswerMode},
#line 87 "HeaderHash.gperf"
      {"p-associated-uri", Headers::PAssociatedUri},
#line 102 "HeaderHash.gperf"
      {"p-associated-uri", Headers::PAssociatedUri},
#line 68 "HeaderHash.gperf"
      {"sip-if-match", Headers::SIPIfMatch},
#line 113 "HeaderHash.gperf"
      {"refer-sub", Headers::ReferSub},
#line 98 "HeaderHash.gperf"
      {"referred-by",Headers::ReferredBy},
#line 61 "HeaderHash.gperf"
      {"proxy-require", Headers::ProxyRequire},
#line 46 "HeaderHash.gperf"
      {"content-disposition", Headers::ContentDisposition},
#line 89 "HeaderHash.gperf"
      {"p-media-authorization", Headers::PMediaAuthorization},
#line 70 "HeaderHash.gperf"
      {"timestamp", Headers::Timestamp},
#line 79 "HeaderHash.gperf"
      {"allow-events", Headers::AllowEvents},
#line 33 "HeaderHash.gperf"
      {"max-forwards", Headers::MaxForwards},
#line 103 "HeaderHash.gperf"
      {"service-route", Headers::ServiceRoute},
#line 110 "HeaderHash.gperf"
      {"service-route", Headers::ServiceRoute},
#line 90 "HeaderHash.gperf"
      {"p-preferred-identity", Headers::PPreferredIdentity},
#line 107 "HeaderHash.gperf"
      {"security-client", Headers::SecurityClient},
#line 86 "HeaderHash.gperf"
      {"p-asserted-identity", Headers::PAssertedIdentity},
#line 51 "HeaderHash.gperf"
      {"content-transfer-encoding", Headers::ContentTransferEncoding},
#line 105 "HeaderHash.gperf"
      {"response-key", Headers::UNKNOWN},
#line 72 "HeaderHash.gperf"
      {"priv-answer-mode", Headers::PrivAnswerMode},
#line 55 "HeaderHash.gperf"
      {"min-expires", Headers::MinExpires},
#line 109 "HeaderHash.gperf"
      {"security-verify", Headers::SecurityVerify},
#line 77 "HeaderHash.gperf"
      {"subscription-state",Headers::SubscriptionState},
#line 108 "HeaderHash.gperf"
      {"security-server", Headers::SecurityServer},
#line 104 "HeaderHash.gperf"
      {"request-disposition", Headers::RequestDisposition},
#line 56 "HeaderHash.gperf"
      {"mime-version", Headers::MIMEVersion},
#line 111 "HeaderHash.gperf"
      {"session-expires", Headers::SessionExpires}
    };

  static short lookup[] =
    {
        -1,    0,   -1,   -1,    1,   -1,    2,    3,
        -1,   -1,   -1,    4,   -1,   -1,   -1,   -1,
         5,   -1,   -1,   -1,   -1,    6,   -1,   -1,
        -1,   -1,    7,   -1,   -1,   -1,   -1,    8,
        -1,   -1,    9,   -1,   10,   -1,   -1, -148,
        -1,   13,   14,  -93,   -2,   -1,   15,   -1,
        -1,   -1,   -1,   16,   -1,   -1,   -1,   -1,
        17,   -1,   -1,   18,   19,   20,   -1,   -1,
        -1,   -1,   21,   22,   -1,   -1,   -1,   23,
        -1,   24,   -1,   -1,   25,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   26,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   27,   -1,   28,   -1,   29,
        -1,   30,   -1,   -1,   31,   32,   33,   -1,
        34,   35,   36,   37,   -1,   38,   39,   40,
        -1,   -1,   41,   42,   43,   -1,   44,   45,
        46,   47,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   48,   -1,   49,   50,   51,   -1,   52,
        53,   -1,   -1,   54,   -1,   -1,   55,   56,
        -1,   -1,   -1,   57,   58,   -1,   -1,   -1,
        59,   -1,   -1,   -1,   -1,   -1,   60,   -1,
        -1,   61,   -1,   62,   63,   -1,   -1,   64,
        -1,   -1,   65,   -1,   -1,   66,   -1,   67,
        -1,   -1,   68,   69,   -1,   -1,   -1,   -1,
        -1,   -1,   70,   -1,   -1, -303,  -33,   -2,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   73,   -1,   -1,   -1,
        -1,   -1,   -1,   74,   75,   -1,   -1,   76,
        -1,   -1,   77,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1, -347,   80,   -1,   81,
        -1,   82,  -26,   -2,   -1,   -1,   -1,   -1,
        83,   -1,   -1,   -1,   -1,   -1,   84,   -1,
        85,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        86,   -1,   -1,   87,   -1,   -1,   -1,   -1,
        88, -381,   -1,   91,  -15,   -2,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   92,   -1,   -1,
        -1,   93,   -1,   -1,   -1,   -1,   -1,   94,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   95,   -1,   -1,   -1,   96,
        -1,   -1,   -1,   -1,   97,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   98,   -1,   -1,   99,
        -1,   -1,   -1,   -1,   -1,   -1,  100,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,  101,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,  102,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,  103
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register int key = hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          register int index = lookup[key];

          if (index >= 0)
            {
              register const char *s = wordlist[index].name;

              if (tolower(*str) == *s && !strncasecmp (str + 1, s + 1, len - 1) && s[len] == '\0')
                return &wordlist[index];
            }
          else if (index < -TOTAL_KEYWORDS)
            {
              register int offset = - 1 - TOTAL_KEYWORDS - index;
              register struct headers *wordptr = &wordlist[TOTAL_KEYWORDS + lookup[offset]];
              register struct headers *wordendptr = wordptr + -lookup[offset + 1];

              while (wordptr < wordendptr)
                {
                  register const char *s = wordptr->name;

                  if (tolower(*str) == *s && !strncasecmp (str + 1, s + 1, len - 1) && s[len] == '\0')
                    return wordptr;
                  wordptr++;
                }
            }
        }
    }
  return 0;
}
#line 116 "HeaderHash.gperf"

}
