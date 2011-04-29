#include "rutil/Mutex.hxx"
#include "rutil/Lock.hxx"
#include "rutil/Logger.hxx"

#ifdef USE_SSL

#include "rutil/ssl/OpenSSLInit.hxx"

#include <openssl/e_os2.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>

#define OPENSSL_THREAD_DEFINES
#include <openssl/opensslconf.h>

#define RESIPROCATE_SUBSYSTEM Subsystem::SIP

using namespace resip;
using namespace std;

#include <iostream>

static bool invokeOpenSSLInit = OpenSSLInit::init(); //.dcm. - only in hxx
volatile bool OpenSSLInit::mInitialized = false;
Mutex* OpenSSLInit::mMutexes;

bool
OpenSSLInit::init()
{
	static OpenSSLInit instance;
	return true;
}

OpenSSLInit::OpenSSLInit()
{
	int locks = CRYPTO_num_locks();
	mMutexes = new Mutex[locks];
	CRYPTO_set_locking_callback(::resip_OpenSSLInit_lockingFunction);

#if !defined(WIN32)
#if defined(_POSIX_THREADS)
	CRYPTO_set_id_callback(::resip_OpenSSLInit_threadIdFunction);
#else
#error Can't set OpenSSL up to be threadsafe!
#endif
#endif

#if 0 //?dcm? -- not used by OpenSSL yet?
	CRYPTO_set_dynlock_create_callback(::resip_OpenSSLInit_dynCreateFunction);
	CRYPTO_set_dynlock_destroy_callback(::resip_OpenSSLInit_dynDestroyFunction);
	CRYPTO_set_dynlock_lock_callback(::resip_OpenSSLInit_dynLockFunction);
#endif

	CRYPTO_malloc_debug_init();
	CRYPTO_set_mem_debug_options(V_CRYPTO_MDEBUG_ALL);
	CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);

	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	assert(EVP_des_ede3_cbc());
   mInitialized = true;
}

OpenSSLInit::~OpenSSLInit()
{
   mInitialized = false;
	ERR_free_strings();// Clean up data allocated during SSL_load_error_strings
	ERR_remove_state(0);// free thread error queue
	CRYPTO_cleanup_all_ex_data();
	EVP_cleanup();// Clean up data allocated during OpenSSL_add_all_algorithms

    //!dcm! We know we have a leak; see BaseSecurity::~BaseSecurity for
    //!details.
//	CRYPTO_mem_leaks_fp(stderr);

	delete [] mMutexes;
}

void
resip_OpenSSLInit_lockingFunction(int mode, int n, const char* file, int line)
{
   if(!resip::OpenSSLInit::mInitialized) return;
   if (mode & CRYPTO_LOCK)
   {
      OpenSSLInit::mMutexes[n].lock();
   }
   else
   {      
      OpenSSLInit::mMutexes[n].unlock();
   }
}

unsigned long 
resip_OpenSSLInit_threadIdFunction()
{
#if defined(WIN32)
   assert(0);
#else
#ifndef _POSIX_THREADS
   assert(0);
#endif
   unsigned long ret;
   ret= (unsigned long)pthread_self();
   return ret;
#endif
   return 0;
}

CRYPTO_dynlock_value* 
resip_OpenSSLInit_dynCreateFunction(char* file, int line)
{
   CRYPTO_dynlock_value* dynLock = new CRYPTO_dynlock_value;
   dynLock->mutex = new Mutex;
   return dynLock;   
}

void
resip_OpenSSLInit_dynDestroyFunction(CRYPTO_dynlock_value* dynlock, const char* file, int line)
{
   delete dynlock->mutex;
   delete dynlock;
}

void 
resip_OpenSSLInit_dynLockFunction(int mode, struct CRYPTO_dynlock_value* dynlock, const char* file, int line)
{
   if (mode & CRYPTO_LOCK)
   {
      dynlock->mutex->lock();
   }
   else
   {
      dynlock->mutex->unlock();
   }
}

#endif
