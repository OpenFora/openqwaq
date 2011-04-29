#if defined(HAVE_CONFIG_H)
#include "resip/stack/config.hxx"
#endif

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include "resip/stack/HeaderTypes.hxx"
#include "resip/stack/SipMessage.hxx"
#include "resip/stack/MsgHeaderScanner.hxx"
#include "rutil/WinLeakCheck.hxx"

namespace resip 
{

///////////////////////////////////////////////////////////////////////////////
//   Any character could be used as the chunk terminating sentinel, as long as
//   it would otherwise be character category "other".  The null character
//   was chosen because it is unlikely to occur naturally -- but it's OK if it
//   does.

enum { chunkTermSentinelChar = '\0' };

enum CharCategoryEnum
{
   ccChunkTermSentinel,
   ccOther,
   ccFieldName,
   ccWhitespace,
   ccColon,
   ccDoubleQuotationMark,
   ccLeftAngleBracket,
   ccRightAngleBracket,
   ccBackslash,
   ccComma,
   ccCarriageReturn,
   ccLineFeed,
   numCharCategories
};
typedef char CharCategory;

char* 
MsgHeaderScanner::allocateBuffer(int size)
{
   return new char[size + MaxNumCharsChunkOverflow];
}

struct CharInfo
{
      CharCategory category;
      MsgHeaderScanner::TextPropBitMask textPropBitMask;
};
    
static CharInfo charInfoArray[UCHAR_MAX+1];
    
static inline int c2i(unsigned char c)
{
   return static_cast<int>(c); 
}

static void initCharInfoArray()
{
   for(unsigned int charIndex = 0; charIndex <= UCHAR_MAX; ++charIndex) 
   {
      charInfoArray[charIndex].category = ccOther;
      charInfoArray[charIndex].textPropBitMask = 0;
   }

   for(const char *charPtr = "abcdefghijklmnopqrstuvwxyz"
          "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.!%*_+`'~";
       *charPtr;
       ++charPtr)
   {
      charInfoArray[c2i(*charPtr)].category = ccFieldName;
   }

   charInfoArray[c2i(' ')].category  = ccWhitespace;
   charInfoArray[c2i('\t')].category = ccWhitespace;
   charInfoArray[c2i(':')].category  = ccColon;
   charInfoArray[c2i('"')].category  = ccDoubleQuotationMark;
   charInfoArray[c2i('<')].category  = ccLeftAngleBracket;
   charInfoArray[c2i('>')].category  = ccRightAngleBracket;
   charInfoArray[c2i('\\')].category  = ccBackslash;
   charInfoArray[c2i(',')].category  = ccComma;
   charInfoArray[c2i('\r')].category = ccCarriageReturn;
   charInfoArray[c2i('\n')].category = ccLineFeed;
   // Assert: "chunkTermSentinelChar"'s category is still the default "ccOther".
   charInfoArray[c2i(chunkTermSentinelChar)].category = ccChunkTermSentinel;
   // Init text property bit masks.
   charInfoArray[c2i('\r')].textPropBitMask =
      MsgHeaderScanner::tpbmContainsLineBreak;
   charInfoArray[c2i('\n')].textPropBitMask =
      MsgHeaderScanner::tpbmContainsLineBreak;
   charInfoArray[c2i(' ')].textPropBitMask =
      MsgHeaderScanner::tpbmContainsWhitespace;
   charInfoArray[c2i('\t')].textPropBitMask =
      MsgHeaderScanner::tpbmContainsWhitespace;
   charInfoArray[c2i('\\')].textPropBitMask =
      MsgHeaderScanner::tpbmContainsBackslash;
   charInfoArray[c2i('%')].textPropBitMask =
      MsgHeaderScanner::tpbmContainsPercent;
   charInfoArray[c2i(';')].textPropBitMask =
      MsgHeaderScanner::tpbmContainsSemicolon;
   charInfoArray[c2i('(')].textPropBitMask =
      MsgHeaderScanner::tpbmContainsParen;
   charInfoArray[c2i(')')].textPropBitMask =
      MsgHeaderScanner::tpbmContainsParen;
}

///////////////////////////////////////////////////////////////////////////////
//   States marked '1' scan normal values.  States marked 'N' scan multi-values.

enum StateEnum
{
   sMsgStart,
   sHalfLineBreakAtMsgStart,
   sScanStatusLine,
   sHalfLineBreakAfterStatusLine,
   sAfterLineBreakAfterStatusLine,
   sScanFieldName,
   sScanWhitespaceAfter1FieldName,
   sScanWhitespaceAfterNFieldName,
   sScanWhitespaceOr1Value,
   sScanWhitespaceOrNValue,
   sHalfLineBreakInWhitespaceBefore1Value,
   sHalfLineBreakInWhitespaceBeforeNValue,
   sAfterLineBreakInWhitespaceBefore1Value,
   sAfterLineBreakInWhitespaceBeforeNValue,
   sScan1Value,
   sScanNValue,
   sHalfLineBreakIn1Value,
   sHalfLineBreakInNValue,
   sAfterLineBreakIn1Value,
   sAfterLineBreakInNValue,
   sScanNValueInQuotes,
   sAfterEscCharInQuotesInNValue,
   sHalfLineBreakInQuotesInNValue,
   sAfterLineBreakInQuotesInNValue,
   sScanNValueInAngles,
   sHalfLineBreakInAnglesInNValue,
   sAfterLineBreakInAnglesInNValue,
   sHalfLineBreakAfterLineBreak,
   numStates
};

typedef char State;

// For each '1' state, the 'N' state is "deltaOfNStateFrom1State" larger.
enum { deltaOfNStateFrom1State = 1 };

/////
    
enum TransitionActionEnum {
   taNone,
   taTermStatusLine,       // The current character terminates the status
   //     line.
   taTermFieldName,        // The current character terminates a field name.
   //     If the field supports multi-values, shift
   //     the state machine into multi-value scanning.
   taBeyondEmptyValue,     // The current character terminates an empty value.
   //     Implies taStartText.
   taTermValueAfterLineBreak, 
   // The previous two characters are a linebreak
   //      terminating a value.  Implies taStartText.
   taTermValue,            // The current character terminates a value.
   taStartText,            // The current character starts a text unit.
   //     (The status line, a field name, or a value.)
   taEndHeader,            // The current character mEnds_ the header.
   taChunkTermSentinel,    // Either the current character terminates the
   //    current chunk or it is an ordinary character.
   taError                 // The input is erroneous.
};
typedef char TransitionAction;


struct TransitionInfo
{
      TransitionAction  action;
      State             nextState;
};

static TransitionInfo stateMachine[numStates][numCharCategories];

inline void specTransition(State state,
                           CharCategory charCategory,
                           TransitionAction action,
                           State nextState)
{
   stateMachine[c2i(state)][c2i(charCategory)].action = action;
   stateMachine[c2i(state)][c2i(charCategory)].nextState = nextState;
}

static void specDefaultTransition(State state,
                                  TransitionAction action,
                                  State nextState)
{
   for (int charCategory = 0;
        charCategory < numCharCategories;
        ++charCategory) 
   {
      specTransition(state, charCategory, action, nextState);
   }
   specTransition(state, ccCarriageReturn, taError, state);
   specTransition(state, ccLineFeed, taError, state);
   specTransition(state, ccChunkTermSentinel, taChunkTermSentinel, state);
}

static void specHalfLineBreakState(State halfLineBreakState,
                                   State  afterLineBreakState)
{
   specDefaultTransition(halfLineBreakState, taError, halfLineBreakState);
   specTransition(halfLineBreakState, ccLineFeed, taNone, afterLineBreakState);
}


//   Single-value (1) scanning and multi-value (N) scanning involves several nearly
//   identical states.
//   "stateDelta" is either 0 or "deltaOfNStateFrom1State".

static void specXValueStates(int  stateDelta)
{
   specDefaultTransition(sScanWhitespaceAfter1FieldName + stateDelta,
                         taError,
                         sScanWhitespaceAfter1FieldName + stateDelta);
   specTransition(sScanWhitespaceAfter1FieldName + stateDelta,
                  ccWhitespace,
                  taNone,
                  sScanWhitespaceAfter1FieldName + stateDelta);
   specTransition(sScanWhitespaceAfter1FieldName + stateDelta,
                  ccColon,
                  taNone,
                  sScanWhitespaceOr1Value + stateDelta);
   specDefaultTransition(sScanWhitespaceOr1Value + stateDelta,
                         taStartText,
                         sScan1Value + stateDelta);
   specTransition(sScanWhitespaceOr1Value + stateDelta,
                  ccWhitespace,
                  taNone,
                  sScanWhitespaceOr1Value + stateDelta);
   if (stateDelta == deltaOfNStateFrom1State)
   {
      specTransition(sScanWhitespaceOr1Value + stateDelta,
                     ccComma,
                     taError,
                     sScanWhitespaceOr1Value + stateDelta);
      specTransition(sScanWhitespaceOr1Value + stateDelta,
                     ccLeftAngleBracket,
                     taStartText,
                     sScanNValueInAngles);
      specTransition(sScanWhitespaceOr1Value + stateDelta,
                     ccDoubleQuotationMark,
                     taStartText,
                     sScanNValueInQuotes);
   }
   specTransition(sScanWhitespaceOr1Value + stateDelta,
                  ccCarriageReturn,
                  taNone,
                  sHalfLineBreakInWhitespaceBefore1Value + stateDelta);
   specHalfLineBreakState(sHalfLineBreakInWhitespaceBefore1Value + stateDelta,
                          sAfterLineBreakInWhitespaceBefore1Value + stateDelta);
   specDefaultTransition(sAfterLineBreakInWhitespaceBefore1Value + stateDelta,
                         taError,
                         sAfterLineBreakInWhitespaceBefore1Value + stateDelta);
   specTransition(sAfterLineBreakInWhitespaceBefore1Value + stateDelta,
                  ccFieldName,
                  taBeyondEmptyValue,
                  sScanFieldName);
   specTransition(sAfterLineBreakInWhitespaceBefore1Value + stateDelta,
                  ccWhitespace,
                  taNone,
                  sScanWhitespaceOr1Value + stateDelta);
   specTransition(sAfterLineBreakInWhitespaceBefore1Value + stateDelta,
                  ccCarriageReturn,
                  taBeyondEmptyValue,
                  sHalfLineBreakAfterLineBreak);
   specDefaultTransition(sScan1Value + stateDelta,
                         taNone,
                         sScan1Value + stateDelta);
   if (stateDelta == deltaOfNStateFrom1State)
   {
      specTransition(sScan1Value + stateDelta,
                     ccComma,
                     taTermValue,
                     sScanWhitespaceOr1Value + stateDelta);
      specTransition(sScan1Value + stateDelta,
                     ccLeftAngleBracket,
                     taNone,
                     sScanNValueInAngles);
      specTransition(sScan1Value + stateDelta,
                     ccDoubleQuotationMark,
                     taNone,
                     sScanNValueInQuotes);
   }
   specTransition(sScan1Value + stateDelta,
                  ccCarriageReturn,
                  taNone,
                  sHalfLineBreakIn1Value + stateDelta);
   specHalfLineBreakState(sHalfLineBreakIn1Value + stateDelta,
                          sAfterLineBreakIn1Value + stateDelta);
   specDefaultTransition(sAfterLineBreakIn1Value + stateDelta,
                         taError,
                         sAfterLineBreakIn1Value + stateDelta);
   specTransition(sAfterLineBreakIn1Value + stateDelta,
                  ccFieldName,
                  taTermValueAfterLineBreak,
                  sScanFieldName);
   specTransition(sAfterLineBreakIn1Value + stateDelta,
                  ccWhitespace,
                  taNone,
                  sScan1Value + stateDelta);
   specTransition(sAfterLineBreakIn1Value + stateDelta,
                  ccCarriageReturn,
                  taTermValueAfterLineBreak,
                  sHalfLineBreakAfterLineBreak);
}

static void initStateMachine()
{
   // By convention, error transitions maintain the same state.
   specDefaultTransition(sMsgStart, taStartText, sScanStatusLine);
   specTransition(sMsgStart,
                  ccCarriageReturn,
                  taNone,
                  sHalfLineBreakAtMsgStart);
   specTransition(sMsgStart, ccLineFeed, taError, sMsgStart);
   specHalfLineBreakState(sHalfLineBreakAtMsgStart, sMsgStart);
   specDefaultTransition(sScanStatusLine, taNone, sScanStatusLine);
   specTransition(sScanStatusLine,
                  ccCarriageReturn,
                  taTermStatusLine,
                  sHalfLineBreakAfterStatusLine);
   specHalfLineBreakState(sHalfLineBreakAfterStatusLine,
                          sAfterLineBreakAfterStatusLine);
   specDefaultTransition(sAfterLineBreakAfterStatusLine,
                         taError,
                         sAfterLineBreakAfterStatusLine);
   specTransition(sAfterLineBreakAfterStatusLine,
                  ccFieldName,
                  taStartText,
                  sScanFieldName);
   specTransition(sAfterLineBreakAfterStatusLine,
                  ccWhitespace,
                  taError,
                  sAfterLineBreakAfterStatusLine);
   specTransition(sAfterLineBreakAfterStatusLine,
                  ccCarriageReturn,
                  taNone,
                  sHalfLineBreakAfterLineBreak);
   specDefaultTransition(sScanFieldName, taError, sScanFieldName);
   specTransition(sScanFieldName, ccFieldName, taNone, sScanFieldName);
   specTransition(sScanFieldName,
                  ccWhitespace,
                  taTermFieldName,
                  sScanWhitespaceAfter1FieldName);
   specTransition(sScanFieldName,
                  ccColon,
                  taTermFieldName,
                  sScanWhitespaceOr1Value);
   specXValueStates(0);
   specXValueStates(deltaOfNStateFrom1State);
   specDefaultTransition(sScanNValueInQuotes, taNone, sScanNValueInQuotes);
   specTransition(sScanNValueInQuotes,
                  ccDoubleQuotationMark,
                  taNone,
                  sScanNValue);
   specTransition(sScanNValueInQuotes,
                  ccBackslash,
                  taNone,
                  sAfterEscCharInQuotesInNValue);
   specTransition(sScanNValueInQuotes,
                  ccCarriageReturn,
                  taNone,
                  sHalfLineBreakInQuotesInNValue);
   specDefaultTransition(sAfterEscCharInQuotesInNValue,
                         taNone,
                         sScanNValueInQuotes);
   specHalfLineBreakState(sHalfLineBreakInQuotesInNValue,
                          sAfterLineBreakInQuotesInNValue);
   specDefaultTransition(sAfterLineBreakInQuotesInNValue,
                         taError,
                         sAfterLineBreakInQuotesInNValue);
   specTransition(sAfterLineBreakInQuotesInNValue,
                  ccWhitespace,
                  taNone,
                  sScanNValueInQuotes);
   specDefaultTransition(sScanNValueInAngles, taNone, sScanNValueInAngles);
   specTransition(sScanNValueInAngles,
                  ccRightAngleBracket,
                  taNone,
                  sScanNValue);
   specTransition(sScanNValueInAngles,
                  ccCarriageReturn,
                  taNone,
                  sHalfLineBreakInAnglesInNValue);
   specHalfLineBreakState(sHalfLineBreakInAnglesInNValue,
                          sAfterLineBreakInAnglesInNValue);
   specDefaultTransition(sAfterLineBreakInAnglesInNValue,
                         taError,
                         sAfterLineBreakInAnglesInNValue);
   specTransition(sAfterLineBreakInAnglesInNValue,
                  ccWhitespace,
                  taNone,
                  sScanNValueInAngles);
   specHalfLineBreakState(sHalfLineBreakAfterLineBreak, sMsgStart);

   // Most half-line-break states do nothing when they read a line feed,
   // but sHalfLineBreakAfterLineBreak must end the message header scanning.

   specTransition(sHalfLineBreakAfterLineBreak,
                  ccLineFeed,
                  taEndHeader,
                  sMsgStart); // Arbitrary but possibly handy.
}

// Debug follows
#if defined(RESIP_MSG_HEADER_SCANNER_DEBUG)  

static void printText(const char *  text,
                      unsigned int  textLength)
{
   const char *charPtr = text;
   for (unsigned int counter = 0; counter < textLength; ++charPtr, ++counter)
   {
      char c = *charPtr;
      switch (c)
      {
         case '\\': printf("\\\\");
            break;
         case '\r': printf("\\r");
            break;
         case '\n': printf("\\n");
            break;
         case '\t': printf("\\t");
            break;
         case '\0': printf("\\0");
            break;
         default:   putchar(c);
      }
   }
}

static const char *
categorySymbol(CharCategory c)
{
   switch(c)
   {
      case ccChunkTermSentinel: return "TERM";
      case ccOther: return "*";
      case ccFieldName: return "FName";
      case ccWhitespace: return "WS";
      case ccColon: return "\\\":\\\"";
      case ccDoubleQuotationMark: return "\\\"";
      case ccLeftAngleBracket: return "\\\"<\\\"";
      case ccRightAngleBracket: return "\\\">\\\"";
      case ccBackslash: return "\\\"\\\\\\\"";
      case ccComma: return "\\\",\\\"";
      case ccCarriageReturn: return "CR";
      case ccLineFeed: return "LF";
   }
   return "??CC??";
}

static const char *
categoryName(CharCategory c)
{
   switch(c)
   {
      case ccChunkTermSentinel: return "ccChunkTermSentinel";
      case ccOther: return "ccOther";
      case ccFieldName: return "ccFieldName";
      case ccWhitespace: return "ccWhitespace";
      case ccColon: return "ccColon";
      case ccDoubleQuotationMark: return "ccDoubleQuotationMark";
      case ccLeftAngleBracket: return "ccLeftAngleBracket";
      case ccRightAngleBracket: return "ccRightAngleBracket";
      case ccBackslash: return "ccBackslash";
      case ccComma: return "ccComma";
      case ccCarriageReturn: return "ccCarriageReturn";
      case ccLineFeed: return "ccLineFeed";
   }
   return "UNKNOWNCC";
}

static const char *
cleanName(const char * name)
{
   // Remove leading type-noise from name
   static char *leaders[] = {
      "cc",
      "s",
      "taChunkTerm", // hack to make ChunkTermSentinel smaller
      "ta"
   };
   const int nLeaders = sizeof(leaders)/sizeof(*leaders);
   int offset = 0;
   for(int i = 0 ; i < nLeaders ; i++)
   {
      unsigned int l = strlen(leaders[i]);
      if (strstr(name,leaders[i]) == name &&
          strlen(name) > l && 
          isupper(name[l]))
      {
         offset = l;
         break;
      }
   }
   return &name[offset];
}

static const char * 
stateName(State state)
{
   const char *stateName;
   switch (state) 
   {
      case sMsgStart:
         stateName = "sMsgStart";
         break;
      case sHalfLineBreakAtMsgStart:
         stateName = "sHalfLineBreakAtMsgStart";
         break;
      case sScanStatusLine:
         stateName = "sScanStatusLine";
         break;
      case sHalfLineBreakAfterStatusLine:
         stateName = "sHalfLineBreakAfterStatusLine";
         break;
      case sAfterLineBreakAfterStatusLine:
         stateName = "sAfterLineBreakAfterStatusLine";
         break;
      case sScanFieldName:
         stateName = "sScanFieldName";
         break;
      case sScanWhitespaceAfter1FieldName:
         stateName = "sScanWhitespaceAfter1FieldName";
         break;
      case sScanWhitespaceAfterNFieldName:
         stateName = "sScanWhitespaceAfterNFieldName";
         break;
      case sScanWhitespaceOr1Value:
         stateName = "sScanWhitespaceOr1Value";
         break;
      case sScanWhitespaceOrNValue:
         stateName = "sScanWhitespaceOrNValue";
         break;
      case sHalfLineBreakInWhitespaceBefore1Value:
         stateName = "sHalfLineBreakInWhitespaceBefore1Value";
         break;
      case sHalfLineBreakInWhitespaceBeforeNValue:
         stateName = "sHalfLineBreakInWhitespaceBeforeNValue";
         break;
      case sAfterLineBreakInWhitespaceBefore1Value:
         stateName = "sAfterLineBreakInWhitespaceBefore1Value";
         break;
      case sAfterLineBreakInWhitespaceBeforeNValue:
         stateName = "sAfterLineBreakInWhitespaceBeforeNValue";
         break;
      case sScan1Value:
         stateName = "sScan1Value";
         break;
      case sScanNValue:
         stateName = "sScanNValue";
         break;
      case sHalfLineBreakIn1Value:
         stateName = "sHalfLineBreakIn1Value";
         break;
      case sHalfLineBreakInNValue:
         stateName = "sHalfLineBreakInNValue";
         break;
      case sAfterLineBreakIn1Value:
         stateName = "sAfterLineBreakIn1Value";
         break;
      case sAfterLineBreakInNValue:
         stateName = "sAfterLineBreakInNValue";
         break;
      case sScanNValueInQuotes:
         stateName = "sScanNValueInQuotes";
         break;
      case sAfterEscCharInQuotesInNValue:
         stateName = "sAfterEscCharInQuotesInNValue";
         break;
      case sHalfLineBreakInQuotesInNValue:
         stateName = "sHalfLineBreakInQuotesInNValue";
         break;
      case sAfterLineBreakInQuotesInNValue:
         stateName = "sAfterLineBreakInQuotesInNValue";
         break;
      case sScanNValueInAngles:
         stateName = "sScanNValueInAngles";
         break;
      case sHalfLineBreakInAnglesInNValue:
         stateName = "sHalfLineBreakInAnglesInNValue";
         break;
      case sAfterLineBreakInAnglesInNValue:
         stateName = "sAfterLineBreakInAnglesInNValue";
         break;
      case sHalfLineBreakAfterLineBreak:
         stateName = "sHalfLineBreakAfterLineBreak";
         break;
      default:
         stateName = "<unknown>";
   }//switch
   return stateName;
}

static const char *
trActionName(TransitionAction transitionAction)
{  
   const char *transitionActionName;
   switch (transitionAction)
   {
      case taNone:
         transitionActionName = "taNone";
         break;
      case taTermStatusLine:
         transitionActionName = "taTermStatusLine";
         break;
      case taTermFieldName:
         transitionActionName = "taTermFieldName";
         break;
      case taBeyondEmptyValue:
         transitionActionName = "taBeyondEmptyValue";
         break;
      case taTermValueAfterLineBreak:
         transitionActionName = "taTermValueAfterLineBreak";
         break;
      case taTermValue:
         transitionActionName = "taTermValue";
         break;
      case taStartText:
         transitionActionName = "taStartText";
         break;
      case taEndHeader:
         transitionActionName = "taEndHeader";
         break;
      case taChunkTermSentinel:
         transitionActionName = "taChunkTermSentinel";
         break;
      case taError:
         transitionActionName = "taError";
         break;
      default:
         transitionActionName = "<unknown>";
   }
   return transitionActionName;
}

static void
printStateTransition(State state,
                     char character,
                     TransitionAction transitionAction)
{
   printf("                %s['", cleanName(stateName(state)));
   printText(&character, 1);
   printf("']: %s\n", cleanName(trActionName(transitionAction)));
}
#if !defined(RESIP_MSG_HEADER_SCANNER_DEBUG)
static const char* stateName(const char*)
{ return "RECOMPILE_WITH_SCANNER_DEBUG"; }
static const char* trActionName(const char*)
{ return stateName(0); }
#endif
/// START OF MEMBER METHODS



int
MsgHeaderScanner::dumpStateMachine(int fd)
{
   FILE *fp = fdopen(fd,"w");
   if (!fp) 
   {
      fprintf(stderr,"MsgHeaderScanner:: unable to open output file\n");
      return -1;
   }
   // Force instance so things are initialized -- YUCK! 
   MsgHeaderScanner scanner;(void)scanner;
   fprintf(fp,"digraph MsgHeaderScannerFSM {\n");
   fprintf(fp,"\tnode[shape=record\n\t\tfontsize=8\n\t\tfontname=\"Helvetica\"\n\t]\n");
   fprintf(fp,"\tedge [ fontsize=6 fontname=\"Helvetica\"]\n");
   
   fprintf(fp,"\tgraph [ ratio=0.8\n\t\tfontsize=6 compound=true ]");
   for(int state  = 0 ; state < numStates; ++state)
   {
      fprintf(fp,
              "  %s [ label = \"%d|%s\" ]\n",
              cleanName(stateName(state)),
              state,
              cleanName(stateName(state))
         );
      for(int category = 0 ; category < numCharCategories; ++category)
      {
         // Skip Verbose Error or Empty Transitions
         if (stateMachine[state][category].nextState == state &&
             (stateMachine[state][category].action == taError ||
              stateMachine[state][category].action == taNone
                )) continue;
              
         fprintf(fp,
                 "    %s -> %s [label=\"%s\\n%s\" ]\n",
                 cleanName(stateName(state)),
                 cleanName(stateName(stateMachine[state][category].nextState)),
                 categorySymbol(category),
                 cleanName(trActionName(stateMachine[state][category].action)));
      }
      fprintf(fp,"\n");
   }
   fprintf(fp,"}\n");

   return 0;
}

#endif //defined(RESIP_MSG_HEADER_SCANNER_DEBUG) 



#if defined(RESIP_MSG_HEADER_SCANNER_DEBUG)  

static const char *const multiValuedFieldNameArray[] = {
   "allow-events",
   "accept-encoding",
   "accept-language",
   "allow",
   "content-language",
   "proxy-require",
   "require",
   "supported",
   "subscription-state",
   "unsupported",
   "security-client",
   "security-server",
   "security-verify",
   "accept",
   "call-info",
   "alert-info",
   "error-info",
   "record-route",
   "route",
   "contact",
   "authorization",
   "proxy-authenticate",
   "proxy-authorization",
   "www-authenticate",
   "via",
   0
};

extern
void
lookupMsgHeaderFieldInfo(
   char *                             fieldName,               //inout
   unsigned int                       *fieldNameLength,        //inout
   MsgHeaderScanner::TextPropBitMask  fieldNameTextPropBitMask,
   int                                *fieldKind,              //out
   bool                               *isMultiValueAllowed)    //out
{
   *isMultiValueAllowed = false;
   const char *const *multiValuedFieldNamePtr = multiValuedFieldNameArray;
   for (;;)
   {
      const char *multiValuedFieldName = *multiValuedFieldNamePtr;
      if (!multiValuedFieldName) 
      {
         break;
      }
      if (strncmp(fieldName, multiValuedFieldName, *fieldNameLength) == 0) 
      {
         *isMultiValueAllowed = true;
         break;
      }
      ++multiValuedFieldNamePtr;
   }//for
}

static
bool
processMsgHeaderStatusLine(
   SipMessage *                       msg,
   char *                             lineText,
   unsigned int                       lineTextLength,
   MsgHeaderScanner::TextPropBitMask  lineTextPropBitMask)
{
   printf("status line: ");
   printText(lineText, lineTextLength);
   printf("\n");
   return true;
}

static
void
processMsgHeaderFieldNameAndValue(
   SipMessage *                       msg,
   int                                fieldKind,
   const char *                       fieldName,
   unsigned int                       fieldNameLength,
   char *                             valueText,
   unsigned int                       valueTextLength,
   MsgHeaderScanner::TextPropBitMask  valueTextPropBitMask)
{
   printText(fieldName, fieldNameLength);
   printf(": [[[[");
   printText(valueText, valueTextLength);
   printf("]]]]\n");
}

#else //!defined(RESIP_MSG_HEADER_SCANNER_DEBUG) } {


//   Determine a field's kind and whether it allows (comma separated) multi-values.
//   "fieldName" is not empty and contains only legal characters.
//   The text in "fieldName" may be canonicalized (eg, translating % escapes),
//   including shrinking it if necessary.

inline void
lookupMsgHeaderFieldInfo(char * fieldName,
                         unsigned int *fieldNameLength,   
                         MsgHeaderScanner::TextPropBitMask fieldNameTextPropBitMask,
                         int *fieldKind,             
                         bool *isMultiValueAllowed)    
{
   //.jacob. Don't ignore fieldNameTextPropBitMask.
   *fieldKind = Headers::getType(fieldName, *fieldNameLength);
   *isMultiValueAllowed =
      Headers::isCommaTokenizing(static_cast<Headers::Type>(*fieldKind));
}


// "lineText" contains no carriage returns and no line feeds.
// Return true on success, false on failure.

inline bool
processMsgHeaderStatusLine(SipMessage * msg,
                           char * lineText,
                           unsigned int lineTextLength,
                           MsgHeaderScanner::TextPropBitMask lineTextPropBitMask)
{
   //.jacob. Don't ignore valueTextPropBitMask, and don't always return true.
   msg->setStartLine(lineText, lineTextLength);
   return true;
}

// This function is called once for a field with one value.  (The value could be
// several values, but separated by something other than commas.)
// This function is called once for a field with 0 comma-separated values, with
// an empty value.
// This function is called N times for a field with N comma-separated values,
// but with the same value of "fieldName" each time.
// "fieldName" is not empty and contains only legal characters.
// "valueText" may be empty, has no leading whitespace, may contain trailing
// whitespace, contains carriage returns and line feeds only in correct pairs
// and followed by whitespace, and, if the field is multi-valued, contains
// balanced '<'/'>' and '"' pairs, contains ',' only within '<'/'>' or '"'
// pairs, and respects '\\'s within '"' pairs.
// The text in "valueText" may be canonicalized (eg, translating % escapes),
// including shrinking it if necessary.

inline void
processMsgHeaderFieldNameAndValue(SipMessage * msg,
                                  int fieldKind,
                                  const char * fieldName,
                                  unsigned int fieldNameLength,
                                  char * valueText,
                                  unsigned int valueTextLength,
                                  MsgHeaderScanner::TextPropBitMask valueTextPropBitMask)
{
   //.jacob. Don't ignore valueTextPropBitMask, particularly for '\r' & '\n'.
   msg->addHeader(static_cast<Headers::Type>(fieldKind),
                  fieldName,
                  fieldNameLength,
                  valueText,
                  valueTextLength);
}

#endif //!defined(RESIP_MSG_HEADER_SCANNER_DEBUG) }

bool MsgHeaderScanner::mInitialized = false;

MsgHeaderScanner::MsgHeaderScanner()
{
   if (!mInitialized)
   {
      mInitialized = true;
      initialize();
   }
}

void
MsgHeaderScanner::prepareForMessage(SipMessage *  msg)
{
   mMsg = msg;
   mState = sMsgStart;
   mPrevScanChunkNumSavedTextChars = 0;
}

void
MsgHeaderScanner::prepareForFrag(SipMessage *  msg, bool hasStartLine)
{
   mMsg = msg;
   if (hasStartLine)
   {
      mState = sMsgStart;
   }
   else
   {
      mState = sAfterLineBreakAfterStatusLine;
   }
   mPrevScanChunkNumSavedTextChars = 0;
}

MsgHeaderScanner::ScanChunkResult
MsgHeaderScanner::scanChunk(char * chunk,
                            unsigned int chunkLength,
                            char ** unprocessedCharPtr)
{
   MsgHeaderScanner::ScanChunkResult result;
   CharInfo* localCharInfoArray = charInfoArray;
   TransitionInfo (*localStateMachine)[numCharCategories] = stateMachine;
   State localState = mState;
   char *charPtr = chunk + mPrevScanChunkNumSavedTextChars;
   char *termCharPtr = chunk + chunkLength;
   char saveChunkTermChar = *termCharPtr;
   *termCharPtr = chunkTermSentinelChar;
   char *textStartCharPtr;
   MsgHeaderScanner::TextPropBitMask localTextPropBitMask = mTextPropBitMask;
   if (mPrevScanChunkNumSavedTextChars == 0)
   {
      textStartCharPtr = 0;
   }
   else
   {
      textStartCharPtr = chunk;
   }
   --charPtr;  // The loop starts by advancing "charPtr", so pre-adjust it.
   for (;;)
   {
      // BEGIN message header character scan block BEGIN
      // The code in this block is executed once per message header character.
      // This entire file is designed specifically to minimize this block's size.
      ++charPtr;
      CharInfo *charInfo = &localCharInfoArray[((unsigned char) (*charPtr))];
      CharCategory charCategory = charInfo->category;
      localTextPropBitMask |= charInfo->textPropBitMask;
     determineTransitionFromCharCategory:
      TransitionInfo *transitionInfo =
         &(localStateMachine[localState][(size_t)charCategory]);
      TransitionAction transitionAction = transitionInfo->action;
#if defined(RESIP_MSG_HEADER_SCANNER_DEBUG)  
      printStateTransition(localState, *charPtr, transitionAction);
#endif
      localState = transitionInfo->nextState;
      if (transitionAction == taNone) continue;
      // END message header character scan block END
      // The loop remainder is executed about 4-5 times per message header line.
      switch (transitionAction)
      {
         case taTermStatusLine:
            if (!processMsgHeaderStatusLine(mMsg,
                                            textStartCharPtr,
                                            charPtr - textStartCharPtr,
                                            localTextPropBitMask))
            {
               result = MsgHeaderScanner::scrError;
               *unprocessedCharPtr = charPtr;
               goto endOfFunction;
            }
            textStartCharPtr = 0;
            break;
         case taTermFieldName:
         {
            mFieldNameLength = charPtr - textStartCharPtr;
            bool isMultiValueAllowed;
            lookupMsgHeaderFieldInfo(textStartCharPtr,
                                     &mFieldNameLength,
                                     localTextPropBitMask,
                                     &mFieldKind,
                                     &isMultiValueAllowed);
            mFieldName = textStartCharPtr;
            textStartCharPtr = 0;
            if (isMultiValueAllowed) 
            {
               localState += deltaOfNStateFrom1State;
            }
         }
         break;
         case taBeyondEmptyValue:
            processMsgHeaderFieldNameAndValue(mMsg,
                                              mFieldKind,
                                              mFieldName,
                                              mFieldNameLength,
                                              0,
                                              0,
                                              0);
            goto performStartTextAction;
         case taTermValueAfterLineBreak:
            processMsgHeaderFieldNameAndValue(mMsg,
                                              mFieldKind,
                                              mFieldName,
                                              mFieldNameLength,
                                              textStartCharPtr,
                                              (charPtr - textStartCharPtr) - 2,
                                              localTextPropBitMask);       //^:CRLF
            goto performStartTextAction;
         case taTermValue:
            processMsgHeaderFieldNameAndValue(mMsg,
                                              mFieldKind,
                                              mFieldName,
                                              mFieldNameLength,
                                              textStartCharPtr,
                                              charPtr - textStartCharPtr,
                                              localTextPropBitMask);
            textStartCharPtr = 0;
            break;
         case taStartText:
        performStartTextAction:
            textStartCharPtr = charPtr;
            localTextPropBitMask = 0;
            break;
         case taEndHeader:
            // textStartCharPtr is not 0.  Not currently relevant.
            result = MsgHeaderScanner::scrEnd;
            *unprocessedCharPtr = charPtr + 1;  // The current char is processed.
            goto endOfFunction;
            break;
         case taChunkTermSentinel:
            if (charPtr == termCharPtr)
            {
               // The chunk has been consumed.  Save some state and request another.
               mState = localState;
               if (textStartCharPtr == 0) 
               {
                  mPrevScanChunkNumSavedTextChars = 0;
               }
               else
               {
                  mPrevScanChunkNumSavedTextChars = termCharPtr - textStartCharPtr;
               }
               mTextPropBitMask = localTextPropBitMask;
               result = MsgHeaderScanner::scrNextChunk;
               *unprocessedCharPtr = termCharPtr - mPrevScanChunkNumSavedTextChars;
               goto endOfFunction;
            }
            else
            {
               // The character is not the sentinel.  Treat it like any other.
               charCategory = ccOther;
               goto determineTransitionFromCharCategory;
            }
            break;
         default:
            result = MsgHeaderScanner::scrError;
            *unprocessedCharPtr = charPtr;
            goto endOfFunction;
      }//switch
   }//for
  endOfFunction:
   *termCharPtr = saveChunkTermChar;
   return result;
}

bool
MsgHeaderScanner::initialize()
{
   initCharInfoArray();
   initStateMachine();
   return true;
}


} //namespace resip



#if defined(RESIP_MSG_HEADER_SCANNER_DEBUG) && defined(MSG_SCANNER_STANDALONE)

extern
int
main(unsigned int   numArgs,
     const char * * argVector)
{
   ::resip::MsgHeaderScanner scanner;
   scanner.prepareForMessage(0);
   char *text =
      "status\r\n"
      "bobby: dummy\r\n"
      "allow: foo, bar, \"don,\\\"xyz\r\n zy\", buzz\r\n\r\n";
   unsigned int textLength = strlen(text);
   char chunk[10000];
   strcpy(chunk, text);
   ::resip::MsgHeaderScanner::ScanChunkResult scanChunkResult;
   char *unprocessedCharPtr;
   scanChunkResult = scanner.scanChunk(chunk, 21, &unprocessedCharPtr);
   if (scanChunkResult == ::resip::MsgHeaderScanner::scrNextChunk)
   {
      printf("Scanning another chunk '.");
      ::resip::printText(unprocessedCharPtr, 1);
      printf("'\n");
      scanChunkResult =
         scanner.scanChunk(unprocessedCharPtr,
                           (chunk + textLength) - unprocessedCharPtr,
                           &unprocessedCharPtr);
   }
   if (scanChunkResult != ::resip::MsgHeaderScanner::scrEnd)
   {
      printf("Error %d at character %d.\n",
             scanChunkResult,
             unprocessedCharPtr - chunk);
   }
   return 0;
}

#endif //!defined(RESIP_MSG_HEADER_SCANNER_DEBUG) }

/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000-2005
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
