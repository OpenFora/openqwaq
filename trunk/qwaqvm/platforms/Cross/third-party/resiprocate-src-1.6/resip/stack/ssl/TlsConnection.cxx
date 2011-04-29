#if defined(HAVE_CONFIG_H)
#include "resip/stack/config.hxx"
#endif

#if defined(USE_SSL)

#include "resip/stack/ssl/TlsConnection.hxx"
#include "resip/stack/ssl/Security.hxx"
#include "rutil/Logger.hxx"
#include "resip/stack/Uri.hxx"
#include "rutil/Socket.hxx"

#include <openssl/e_os2.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/x509v3.h>
#include <openssl/ssl.h>
#endif

using namespace resip;

#define RESIPROCATE_SUBSYSTEM Subsystem::TRANSPORT

TlsConnection::TlsConnection( Transport* transport, const Tuple& tuple, 
                              Socket fd, Security* security, 
                              bool server, Data domain,  SecurityTypes::SSLType sslType ,
                              Compression &compression) :
   Connection(transport,tuple, fd, compression),
   mServer(server),
   mSecurity(security),
   mSslType( sslType ),
   mDomain(domain)
{
#if defined(USE_SSL)
   InfoLog (<< "Creating TLS connection for domain " 
            << mDomain 
            << " " << tuple 
            << " on " << fd);

   mSsl = NULL;
   mBio= NULL;
  
   if (mServer)
   {
      DebugLog( << "Trying to form TLS connection - acting as server" );
      if ( mDomain.empty() )
      {
         ErrLog(<< "Tranport was not created with a server domain so can not act as server" ); 
         throw Security::Exception("Trying to act as server but no domain specified",
                                   __FILE__,__LINE__);
      }
   }
   else
   {
      DebugLog( << "Trying to form TLS connection - acting as client" );
   }

   assert( mSecurity );
   SSL_CTX* ctx=NULL;
   if ( mSslType ==  SecurityTypes::SSLv23 )
   {
      ctx = mSecurity->getSslCtx();
   }
   else
   {
      ctx = mSecurity->getTlsCtx();
   }   
   assert(ctx);
   
   mSsl = SSL_new(ctx);
   assert(mSsl);

   assert( mSecurity );

   if(!mDomain.empty())
   {
      X509* cert = mSecurity->getDomainCert(mDomain); //mDomainCerts[mDomain];
      if (!cert)
      {
         if(mServer)
         {
            ErrLog(<< "Don't have certificate for domain " << mDomain );
            throw Security::Exception("getDomainCert failed",
                                      __FILE__,__LINE__);
         }
      }
      else
      {      
         if( !SSL_use_certificate(mSsl, cert) )
         {
            throw Security::Exception("SSL_use_certificate failed",
                                      __FILE__,__LINE__);
         }
      }
      
      EVP_PKEY* pKey = mSecurity->getDomainKey(mDomain); //mDomainPrivateKeys[mDomain];
      if (!pKey)
      {
         if(mServer)
         {
            ErrLog(<< "Don't have private key for domain " << mDomain );
            throw Security::Exception("getDomainKey failed.",
                                      __FILE__,__LINE__);
         }
      }
      else
      {
         if ( !SSL_use_PrivateKey(mSsl, pKey) )
         {
            throw Security::Exception("SSL_use_PrivateKey failed.",
                                      __FILE__,__LINE__);
         }
      }
   }

   if(mServer)
   {
      // clear SSL_VERIFY_PEER|SSL_VERIFY_CLIENT_ONCE set in SSL_CTX if we are a server
      SSL_set_verify(mSsl, 0, 0);
   }

   mBio = BIO_new_socket(fd,0/*close flag*/);
   assert( mBio );
   
   SSL_set_bio( mSsl, mBio, mBio );

   mTlsState = Initial;
   mHandShakeWantsRead = false;

#endif // USE_SSL   
}

TlsConnection::~TlsConnection()
{
#if defined(USE_SSL)
   SSL_shutdown(mSsl);
   SSL_free(mSsl);
#endif // USE_SSL   
}


const char*
TlsConnection::fromState(TlsConnection::TlsState s)
{
   switch(s)
   {
      case Initial: return "Initial"; break;
      case Handshaking: return "Handshaking"; break;
      case Broken: return "Broken"; break;
      case Up: return "Up"; break;
   }
   return "????";
}

TlsConnection::TlsState
TlsConnection::checkState()
{
#if defined(USE_SSL)
   //DebugLog(<<"state is " << fromTlsState(mTlsState));

   if (mTlsState == Up || mTlsState == Broken)
   {
      return mTlsState;
   }
   
   int ok=0;
   
   ERR_clear_error();
   
   if (mTlsState != Handshaking)
   {
      if (mServer)
      {
         InfoLog( << "TLS handshake starting (Server mode)" );
         SSL_set_accept_state(mSsl);
         mTlsState = Handshaking;
      }
      else
      {
         InfoLog( << "TLS handshake starting (client mode)" );
         SSL_set_connect_state(mSsl);
         mTlsState = Handshaking;
      }

      InfoLog( << "TLS connected" ); 
      mTlsState = Handshaking;
   }

   mHandShakeWantsRead = false;
   ok = SSL_do_handshake(mSsl);
      
   if ( ok <= 0 )
   {
      int err = SSL_get_error(mSsl,ok);
         
      switch (err)
      {
         case SSL_ERROR_WANT_READ:
            StackLog( << "TLS handshake want read" );
            mHandShakeWantsRead = true;

            return mTlsState;
         case SSL_ERROR_WANT_WRITE:
            StackLog( << "TLS handshake want write" );
            ensureWritable();
            return mTlsState;

         case SSL_ERROR_ZERO_RETURN:
            StackLog( << "TLS connection closed cleanly");
            return mTlsState;

         case SSL_ERROR_WANT_CONNECT:
            StackLog( << "BIO not connected, try later");
            return mTlsState;

#if  ( OPENSSL_VERSION_NUMBER >= 0x0090702fL )
         case SSL_ERROR_WANT_ACCEPT:
            StackLog( << "TLS connection want accept" );
            return mTlsState;
#endif

         case SSL_ERROR_WANT_X509_LOOKUP:
            StackLog( << "Try later");
            return mTlsState;
         default:
            if(err == SSL_ERROR_SYSCALL)
            {
               int e = getErrno();
               switch(e)
               {
                  case EINTR:
                  case EAGAIN:
                     StackLog( << "try later");
                     return mTlsState;
               }
               ErrLog( << "socket error " << e);
               Transport::error(e);
            }
            else if (err == SSL_ERROR_SSL)
            {
               mFailureReason = TransportFailure::CertValidationFailure;
            }
            ErrLog( << "TLS handshake failed ");
            while (true)
            {
               const char* file;
               int line;

               unsigned long code = ERR_get_error_line(&file,&line);
               if ( code == 0 )
               {
                  break;
               }

               char buf[256];
               ERR_error_string_n(code,buf,sizeof(buf));
               ErrLog( << buf  );
               ErrLog( << "Error code = "
                        << code << " file=" << file << " line=" << line );
            }
            mBio = NULL;
            mTlsState = Broken;
            return mTlsState;
      }
   }
   else // ok > 1
   {
      InfoLog( << "TLS connected" );
   }

   // force peer name to get checked and perhaps cert loaded
   computePeerName();

   //post-connection verification: check that certificate name matches domain name
   if (!mServer)
   {
      bool matches = false;
      for(std::list<BaseSecurity::PeerName>::iterator it = mPeerNames.begin(); it != mPeerNames.end(); it++)
      {
         if(it->mType == BaseSecurity::CommonName)
         {
            //allow wildcard match for subdomain name (RFC 2459)
            if(BaseSecurity::matchHostName(it->mName, who().getTargetDomain()))
            {
               matches=true;
               break;
            }
         }
         else //it->mType == SubjectAltName
      {
            //no wildcards for SubjectAltName
            if(isEqualNoCase(it->mName, who().getTargetDomain()))
         {
             matches=true;
             break;
         }
      }
      }
      if(!matches)
      {
         mTlsState = Broken;
         mBio = NULL;
         ErrLog (<< "Certificate name mismatch: trying to connect to <" 
                 << who().getTargetDomain()
                 << "> remote cert domain(s) are <" 
                 << getPeerNamesData() << ">" );
         mFailureReason = TransportFailure::CertNameMismatch;         
         return mTlsState;
      }
   }

   InfoLog( << "TLS handshake done for peer " << getPeerNamesData()); 
   mTlsState = Up;
   if (!mOutstandingSends.empty())
   {
   ensureWritable();
   }
#endif // USE_SSL   
   return mTlsState;
}

      
int 
TlsConnection::read(char* buf, int count )
{
#if defined(USE_SSL)
   assert( mSsl ); 
   assert( buf );

   switch(checkState())
   {
      case Broken:
         return -1;
         break;
      case Up:
         break;
      default:
         return 0;
         break;
   }

   if (!mBio)
   {
      DebugLog( << "Got TLS read bad bio  " );
      return 0;
   }
      
   if ( !isGood() )
   {
      return -1;
   }

   int bytesRead = SSL_read(mSsl,buf,count);
   StackLog(<< "SSL_read returned " << bytesRead << " bytes [" << Data(Data::Borrow, buf, (bytesRead > 0)?(bytesRead):(0)) << "]");

   int bytesPending = SSL_pending(mSsl);

   if ((bytesRead > 0) && (bytesPending > 0))
   {
      char* buffer = getWriteBufferForExtraBytes(bytesPending);
      if (buffer)
      {
         StackLog(<< "reading remaining buffered bytes");
         bytesPending = SSL_read(mSsl, buffer, bytesPending);
         StackLog(<< "SSL_read returned  " << bytesPending << " bytes [" << Data(Data::Borrow, buffer, (bytesPending > 0)?(bytesPending):(0)) << "]");
         
         if (bytesPending > 0)
         {
            bytesRead += bytesPending;
         }
         else
         {
            bytesRead = bytesPending;
         }
      }
      else
      {
         assert(0);
      }
   }

   if (bytesRead <= 0)
   {
      int err = SSL_get_error(mSsl,bytesRead);
      switch (err)
      {
         case SSL_ERROR_WANT_READ:
         case SSL_ERROR_WANT_WRITE:
         case SSL_ERROR_NONE:
         {
            StackLog( << "Got TLS read got condition of " << err  );
            return 0;
         }
         break;
         default:
         {
            char buf[256];
            ERR_error_string_n(err,buf,sizeof(buf));
            ErrLog( << "Got TLS read ret=" << bytesRead << " error=" << err  << " " << buf  );
            return -1;
         }
         break;
      }
      assert(0);
   }
   StackLog(<<"SSL bytesRead="<<bytesRead);
   return bytesRead;
#endif // USE_SSL
   return -1;
}

bool
TlsConnection::transportWrite()
{
   switch(mTlsState)
   {
      case Handshaking:
      case Initial:
         checkState();
         if (mTlsState == Handshaking)
         {
            DebugLog(<< "Transportwrite--Handshaking--remove from write: " << mHandShakeWantsRead);
            return mHandShakeWantsRead;
         }
         else
         {
            DebugLog(<< "Transportwrite--Handshake complete, in " << fromState(mTlsState) << " calling write");
            return false;
         }
      case Up:
      case Broken:
         DebugLog(<< "Transportwrite--" << fromState(mTlsState) << " fall through to write");
         return false;
   }
   assert(0);
   return false;
}

int 
TlsConnection::write( const char* buf, int count )
{
#if defined(USE_SSL)
   assert( mSsl );
   assert( buf );
   int ret;
 
   switch(checkState())
   {
      case Broken:
         return -1;
         break;
      case Up:
         break;
      default:
         DebugLog( << "Tried to Tls write - but connection is not Up"  );
         return 0;
         break;
   }

   if (!mBio)
   {
      DebugLog( << "Got TLS write bad bio "  );
      return 0;
   }
        
   ret = SSL_write(mSsl,(const char*)buf,count);
   if (ret < 0 )
   {
      int err = SSL_get_error(mSsl,ret);
      switch (err)
      {
         case SSL_ERROR_WANT_READ:
         case SSL_ERROR_WANT_WRITE:
         case SSL_ERROR_NONE:
         {
            StackLog( << "Got TLS write got condition of " << err  );
            return 0;
         }
         break;
         default:
         {
            while (true)
            {
               const char* file;
               int line;
               
               unsigned long code = ERR_get_error_line(&file,&line);
               if ( code == 0 )
               {
                  break;
               }
               
               char buf[256];
               ERR_error_string_n(code,buf,sizeof(buf));
               ErrLog( << buf  );
               DebugLog( << "Error code = " << code << " file=" << file << " line=" << line );
            }
            ErrLog( << "Got TLS write error=" << err << " ret=" << ret  );
            return -1;
         }
         break;
      }
   }

   Data monkey(Data::Borrow, buf, count);

   StackLog( << "Did TLS write " << ret << " " << count << " " << "[[" << monkey << "]]" );

   return ret;
#endif // USE_SSL
   return -1;
}


bool 
TlsConnection::hasDataToRead() // has data that can be read 
{
#if defined(USE_SSL)
   //hack (for now)
   if(mTlsState == Initial)
      return false;

   if (checkState() != Up)
   {
      return false;
   }

   int p = SSL_pending(mSsl);
   //DebugLog(<<"hasDataToRead(): " <<p);
   return (p>0);
#else // USE_SSL
   return false;
#endif 
}


bool 
TlsConnection::isGood() // has data that can be read 
{
#if defined(USE_SSL)
   if ( mBio == 0 )
   {
      return false;
   }

   int mode = SSL_get_shutdown(mSsl);
   if ( mode != 0 ) 
   {
      return false;
   }

#endif       
   return true;
}

bool 
TlsConnection::isWritable() 
{
#if defined(USE_SSL)
   switch(mTlsState)
   {
      case Handshaking:
         return mHandShakeWantsRead ? false : true;
      case Initial:
      case Up:
         return isGood();
      default:
         return false;
   }
   //dragos -- to remove
   DebugLog( << "Current state: " << fromState(mTlsState));
   //end dragos
#endif 
   return false;
}

void TlsConnection::getPeerNames(std::list<Data> &peerNames) const
{
   for(std::list<BaseSecurity::PeerName>::const_iterator it = mPeerNames.begin(); it != mPeerNames.end(); it++)
{
      peerNames.push_back(it->mName);
   }
}

Data
TlsConnection::getPeerNamesData() const
{
   Data peerNamesString;
   for(std::list<BaseSecurity::PeerName>::const_iterator it = mPeerNames.begin(); it != mPeerNames.end(); it++)
   {
      if(it == mPeerNames.begin())
      {
         peerNamesString += it->mName;
      }
      else
      {
         peerNamesString += ", " + it->mName;
      }
   }
   return peerNamesString;
}


void
TlsConnection::computePeerName()
{
#if defined(USE_SSL)
   Data commonName;

   assert(mSsl);

   if (!mBio)
   {
      ErrLog( << "bad bio" );
      return;
   }

   // print session infor       
   SSL_CIPHER *ciph;
   ciph=SSL_get_current_cipher(mSsl);
   InfoLog( << "TLS sessions set up with " 
            <<  SSL_get_version(mSsl) << " "
            <<  SSL_CIPHER_get_version(ciph) << " "
            <<  SSL_CIPHER_get_name(ciph) << " " );

   // get the certificate if other side has one 
   X509* cert = SSL_get_peer_certificate(mSsl);
   if ( !cert )
   {
      DebugLog(<< "No peer certificate in TLS connection" );
      return;
   }

   // check that this certificate is valid 
   if (X509_V_OK != SSL_get_verify_result(mSsl))
   {
      DebugLog(<< "Peer certificate in TLS connection is not valid" );
      X509_free(cert); cert=NULL;
      return;
   }

   mPeerNames.clear();
   BaseSecurity::getCertNames(cert, mPeerNames);
   if(mPeerNames.empty())
   {
      ErrLog(<< "Invalid certificate: no subjectAltName/CommonName found");
      return;
   }

   // add the certificate to the Security store
   unsigned char* buf = NULL;
   int len = i2d_X509( cert, &buf );
   Data derCert( buf, len );
   for(std::list<BaseSecurity::PeerName>::iterator it = mPeerNames.begin(); it != mPeerNames.end(); it++)
   {
      if ( !mSecurity->hasDomainCert( it->mName ) )
      {
         mSecurity->addDomainCertDER(it->mName,derCert);
      }
   }
   OPENSSL_free(buf); buf=NULL;

   X509_free(cert); cert=NULL;
#endif // USE_SSL
}

/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000-2005 Vovida Networks, Inc.  All rights reserved.
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
