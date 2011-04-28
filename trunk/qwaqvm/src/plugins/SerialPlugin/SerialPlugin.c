/* Automatically generated by
	SmartSyntaxPluginCodeGenerator VMMaker-eem.524 uuid: 9b748596-0986-4ca7-ac5b-b7a050a08431
   from
	SerialPlugin VMMaker-eem.524 uuid: 9b748596-0986-4ca7-ac5b-b7a050a08431
 */
static char __buildInfo[] = "SerialPlugin VMMaker-eem.524 uuid: 9b748596-0986-4ca7-ac5b-b7a050a08431 " __DATE__ ;




#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Default EXPORT macro that does nothing (see comment in sq.h): */
#define EXPORT(returnType) returnType

/* Do not include the entire sq.h file but just those parts needed. */
/*  The virtual machine proxy definition */
#include "sqVirtualMachine.h"
/* Configuration options */
#include "sqConfig.h"
/* Platform specific definitions */
#include "sqPlatformSpecific.h"

#define true 1
#define false 0
#define null 0  /* using 'null' because nil is predefined in Think C */
#ifdef SQUEAK_BUILTIN_PLUGIN
#undef EXPORT
// was #undef EXPORT(returnType) but screws NorCroft cc
#define EXPORT(returnType) static returnType
#endif
#include "SerialPlugin.h"

#include "sqMemoryAccess.h"



/*** Constants ***/


/*** Function Prototypes ***/
static VirtualMachine * getInterpreter(void);
EXPORT(const char*) getModuleName(void);
static sqInt halt(void);
EXPORT(sqInt) initialiseModule(void);
static sqInt msg(char *s);
EXPORT(sqInt) primitiveSerialPortClose(void);
EXPORT(sqInt) primitiveSerialPortOpen(void);
EXPORT(sqInt) primitiveSerialPortRead(void);
EXPORT(sqInt) primitiveSerialPortWrite(void);
EXPORT(sqInt) setInterpreter(struct VirtualMachine*anInterpreter);
EXPORT(sqInt) shutdownModule(void);
static void sqAssert(sqInt aBool);


/*** Variables ***/

#ifdef SQUEAK_BUILTIN_PLUGIN
extern
#endif
struct VirtualMachine* interpreterProxy;
static const char *moduleName =
#ifdef SQUEAK_BUILTIN_PLUGIN
	"SerialPlugin VMMaker-eem.524 (i)"
#else
	"SerialPlugin VMMaker-eem.524 (e)"
#endif
;



/*	Note: This is coded so that plugins can be run from Squeak. */

static VirtualMachine *
getInterpreter(void) {
	return interpreterProxy;
}


/*	Note: This is hardcoded so it can be run from Squeak.
	The module name is used for validating a module *after*
	it is loaded to check if it does really contain the module
	we're thinking it contains. This is important! */

EXPORT(const char*)
getModuleName(void) {
	return moduleName;
}

static sqInt
halt(void) {
	;
	return 0;
}

EXPORT(sqInt)
initialiseModule(void) {
	return serialPortInit();
}

static sqInt
msg(char *s) {
	fprintf(stderr, "\n%s: %s", moduleName, s);
	return 0;
}

EXPORT(sqInt)
primitiveSerialPortClose(void) {
	sqInt portNum;

	portNum = interpreterProxy->stackIntegerValue(0);
	if (interpreterProxy->failed()) {
		return null;
	}
	serialPortClose(portNum);
	if (interpreterProxy->failed()) {
		return null;
	}
	interpreterProxy->pop(1);
	return null;
}

EXPORT(sqInt)
primitiveSerialPortOpen(void) {
	sqInt portNum;
	sqInt baudRate;
	sqInt stopBitsType;
	sqInt parityType;
	sqInt dataBits;
	sqInt inFlowControl;
	sqInt outFlowControl;
	sqInt xOnChar;
	sqInt xOffChar;

	portNum = interpreterProxy->stackIntegerValue(8);
	baudRate = interpreterProxy->stackIntegerValue(7);
	stopBitsType = interpreterProxy->stackIntegerValue(6);
	parityType = interpreterProxy->stackIntegerValue(5);
	dataBits = interpreterProxy->stackIntegerValue(4);
	inFlowControl = interpreterProxy->stackIntegerValue(3);
	outFlowControl = interpreterProxy->stackIntegerValue(2);
	xOnChar = interpreterProxy->stackIntegerValue(1);
	xOffChar = interpreterProxy->stackIntegerValue(0);
	if (interpreterProxy->failed()) {
		return null;
	}
	serialPortOpen(
			portNum, baudRate, stopBitsType, parityType, dataBits,
			inFlowControl, outFlowControl, xOnChar, xOffChar);
	if (interpreterProxy->failed()) {
		return null;
	}
	interpreterProxy->pop(9);
	return null;
}

EXPORT(sqInt)
primitiveSerialPortRead(void) {
	sqInt bytesRead;
	sqInt arrayPtr;
	sqInt portNum;
	char *array;
	sqInt startIndex;
	sqInt count;
	sqInt _return_value;

	portNum = interpreterProxy->stackIntegerValue(3);
	interpreterProxy->success(interpreterProxy->isBytes(interpreterProxy->stackValue(2)));
	array = ((char *) (interpreterProxy->firstIndexableField(interpreterProxy->stackValue(2))));
	startIndex = interpreterProxy->stackIntegerValue(1);
	count = interpreterProxy->stackIntegerValue(0);
	if (interpreterProxy->failed()) {
		return null;
	}
	interpreterProxy->success((startIndex >= 1)
	 && (((startIndex + count) - 1) <= (interpreterProxy->byteSizeOf(((sqInt)(long)(array) - 4)))));
	arrayPtr = ((((sqInt)array)) + startIndex) - 1;
	bytesRead = serialPortReadInto( portNum, count, arrayPtr);
	_return_value = interpreterProxy->integerObjectOf(bytesRead);
	if (interpreterProxy->failed()) {
		return null;
	}
	interpreterProxy->popthenPush(5, _return_value);
	return null;
}

EXPORT(sqInt)
primitiveSerialPortWrite(void) {
	sqInt bytesWritten;
	sqInt arrayPtr;
	sqInt portNum;
	char *array;
	sqInt startIndex;
	sqInt count;
	sqInt _return_value;

	portNum = interpreterProxy->stackIntegerValue(3);
	interpreterProxy->success(interpreterProxy->isBytes(interpreterProxy->stackValue(2)));
	array = ((char *) (interpreterProxy->firstIndexableField(interpreterProxy->stackValue(2))));
	startIndex = interpreterProxy->stackIntegerValue(1);
	count = interpreterProxy->stackIntegerValue(0);
	if (interpreterProxy->failed()) {
		return null;
	}
	interpreterProxy->success((startIndex >= 1)
	 && (((startIndex + count) - 1) <= (interpreterProxy->byteSizeOf(((sqInt)(long)(array) - 4)))));
	if (!(interpreterProxy->failed())) {
		arrayPtr = ((((sqInt)array)) + startIndex) - 1;
		bytesWritten = serialPortWriteFrom(portNum, count, arrayPtr);
	}
	_return_value = interpreterProxy->integerObjectOf(bytesWritten);
	if (interpreterProxy->failed()) {
		return null;
	}
	interpreterProxy->popthenPush(5, _return_value);
	return null;
}


/*	Note: This is coded so that is can be run from Squeak. */

EXPORT(sqInt)
setInterpreter(struct VirtualMachine*anInterpreter) {
	sqInt ok;

	interpreterProxy = anInterpreter;
	ok = interpreterProxy->majorVersion() == VM_PROXY_MAJOR;
	if (ok == 0) {
		return 0;
	}
	ok = interpreterProxy->minorVersion() >= VM_PROXY_MINOR;
	return ok;
}

EXPORT(sqInt)
shutdownModule(void) {
	return serialPortShutdown();
}

static void
sqAssert(sqInt aBool) {
	/* missing DebugCode */;
}


#ifdef SQUEAK_BUILTIN_PLUGIN

void* SerialPlugin_exports[][3] = {
	{"SerialPlugin", "getModuleName", (void*)getModuleName},
	{"SerialPlugin", "initialiseModule", (void*)initialiseModule},
	{"SerialPlugin", "primitiveSerialPortClose", (void*)primitiveSerialPortClose},
	{"SerialPlugin", "primitiveSerialPortOpen", (void*)primitiveSerialPortOpen},
	{"SerialPlugin", "primitiveSerialPortRead", (void*)primitiveSerialPortRead},
	{"SerialPlugin", "primitiveSerialPortWrite", (void*)primitiveSerialPortWrite},
	{"SerialPlugin", "setInterpreter", (void*)setInterpreter},
	{"SerialPlugin", "shutdownModule", (void*)shutdownModule},
	{NULL, NULL, NULL}
};

#endif /* ifdef SQ_BUILTIN_PLUGIN */
