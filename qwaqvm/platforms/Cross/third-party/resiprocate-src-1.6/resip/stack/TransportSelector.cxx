#if defined(HAVE_CONFIG_H)
#include "resip/stack/config.hxx"
#endif

#ifdef WIN32
#include <winsock2.h> 
#include <ws2tcpip.h> 
#include <wspiapi.h>   // Required for freeaddrinfo implementation in Windows 2000, NT, Me/95/98
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include "resip/stack/NameAddr.hxx"
#include "resip/stack/Uri.hxx"

#include "resip/stack/ExtensionParameter.hxx"
#include "resip/stack/Compression.hxx"
#include "resip/stack/SipMessage.hxx"
#include "resip/stack/TransactionState.hxx"
#include "resip/stack/TransportFailure.hxx"
#include "resip/stack/TransportSelector.hxx"
#include "resip/stack/InternalTransport.hxx"
#include "resip/stack/TcpBaseTransport.hxx"
#include "resip/stack/TcpTransport.hxx"
#include "resip/stack/UdpTransport.hxx"
#include "resip/stack/Uri.hxx"

#include "rutil/DataStream.hxx"
#include "rutil/DnsUtil.hxx"
#include "rutil/Inserter.hxx"
#include "rutil/Logger.hxx"
#include "rutil/Socket.hxx"
#include "rutil/WinLeakCheck.hxx"
#include "rutil/dns/DnsStub.hxx"

#ifdef USE_SIGCOMP
#include <osc/Stack.h>
#include <osc/SigcompMessage.h>
#endif

#ifdef USE_SSL
#include "resip/stack/ssl/DtlsTransport.hxx"
#include "resip/stack/ssl/Security.hxx"
#include "resip/stack/ssl/TlsTransport.hxx"
#endif

#ifdef WIN32
#include "rutil/WinCompat.hxx"
#endif

#ifdef __MINGW32__
#define gai_strerror strerror
#endif

#include <sys/types.h>

using namespace resip;

#define RESIPROCATE_SUBSYSTEM Subsystem::TRANSPORT

TransportSelector::TransportSelector(Fifo<TransactionMessage>& fifo, Security* security, DnsStub& dnsStub, Compression &compression) :
   mDns(dnsStub),
   mStateMacFifo(fifo),
   mSecurity(security),
   mSocket( INVALID_SOCKET ),
   mSocket6( INVALID_SOCKET ),
   mCompression(compression),
   mSigcompStack (0)
{
   memset(&mUnspecified.v4Address, 0, sizeof(sockaddr_in));
   mUnspecified.v4Address.sin_family = AF_UNSPEC;

#ifdef USE_IPV6
   memset(&mUnspecified6.v6Address, 0, sizeof(sockaddr_in6));
   mUnspecified6.v6Address.sin6_family = AF_UNSPEC;
#endif

#ifdef USE_SIGCOMP
   if (mCompression.isEnabled())
   {
      DebugLog (<< "Compression enabled for Transport Selector");
      mSigcompStack = new osc::Stack(mCompression.getStateHandler());
      mCompression.addCompressorsToStack(mSigcompStack);
   }
   else
   {
      DebugLog (<< "Compression disabled for Transport Selector");
   }
#else
   DebugLog (<< "No compression library available");
#endif
}

template<class T> void 
deleteMap(T& m)
{
   for (typename T::iterator it = m.begin(); it != m.end(); it++)
   {
      delete it->second;
   }
   m.clear();
}

TransportSelector::~TransportSelector()
{
   mConnectionlessMap.clear();
   deleteMap(mExactTransports);
   deleteMap(mAnyInterfaceTransports);
   deleteMap(mTlsTransports);
#ifdef USE_SIGCOMP
   delete mSigcompStack;
#endif

    closeSocket( mSocket );
    closeSocket( mSocket6 );

}

void
TransportSelector::shutdown()
{
    //!dcm! repeat shutodwn template pattern in all loop over all tranport functions, refactor to functor?
    for (ExactTupleMap::iterator i=mExactTransports.begin(); i!=mExactTransports.end(); ++i)
   {
      i->second->shutdown();
   }
   for (AnyInterfaceTupleMap::iterator i=mAnyInterfaceTransports.begin(); i!=mAnyInterfaceTransports.end(); ++i)
   {
      i->second->shutdown();
   }
}

bool
TransportSelector::isFinished() const
{
   for (ExactTupleMap::const_iterator i=mExactTransports.begin(); i!=mExactTransports.end(); ++i)
   {
      if (!i->second->isFinished()) return false;
   }
   for (AnyInterfaceTupleMap::const_iterator i=mAnyInterfaceTransports.begin(); i!=mAnyInterfaceTransports.end(); ++i)
   {
      if (!i->second->isFinished()) return false;
   }
   return true;
}


void
TransportSelector::addTransport(std::auto_ptr<Transport> autoTransport)
{
   Transport* transport = autoTransport.release();
   mDns.addTransportType(transport->transport(), transport->ipVersion());

   // !bwc! This is a multimap from TransportType/IpVersion to Transport*.
   // Make _extra_ sure that no garbage goes in here.
   if(transport->transport()==TCP)
   {
      assert(dynamic_cast<TcpTransport*>(transport));
   }
#ifdef USE_SSL
   else if(transport->transport()==TLS)
   {
      assert(dynamic_cast<TlsTransport*>(transport));
   }
#endif
   else if(transport->transport()==UDP)
   {
      assert(dynamic_cast<UdpTransport*>(transport));
   }
#if USE_DTLS && USE_SSL
   else if(transport->transport()==DTLS)
   {
      assert(dynamic_cast<DtlsTransport*>(transport));
   }
#endif
   else
   {
      assert(0);
   }
   
   Tuple tuple(transport->interfaceName(), transport->port(), 
                   transport->ipVersion(), transport->transport());
   mTypeToTransportMap.insert(std::make_pair(tuple,transport));
   
   switch (transport->transport())
   {
      case UDP:
      case TCP:
      {
         assert(mExactTransports.find(tuple) == mExactTransports.end() &&
                mAnyInterfaceTransports.find(tuple) == mAnyInterfaceTransports.end());

         DebugLog (<< "Adding transport: " << tuple);
         
         // Store the transport in the ANY interface maps if the tuple specifies ANY
         // interface. Store the transport in the specific interface maps if the tuple
         // specifies an interface. See TransportSelector::findTransport.
         if (transport->interfaceName().empty() ||
             transport->hasSpecificContact() )
         {
            mAnyInterfaceTransports[tuple] = transport;
            mAnyPortAnyInterfaceTransports[tuple] = transport;
         }
         else
         {
            mExactTransports[tuple] = transport;
            mAnyPortTransports[tuple] = transport;
         }
      }
      break;
      case TLS:
      case DTLS:
      {
         TlsTransportKey key(transport->tlsDomain(),transport->transport(),transport->ipVersion());
         mTlsTransports[key]=transport;
      }
         break;
      default:
         assert(0);
         break;
   }

   if(transport->transport()==UDP || transport->transport()==DTLS)
   {
      mConnectionlessMap[transport->getTuple().mFlowKey]=transport;
   }

   if (transport->shareStackProcessAndSelect())
   {
      mSharedProcessTransports.push_back(transport);
   }
   else
   {
      mHasOwnProcessTransports.push_back(transport);
      mHasOwnProcessTransports.back()->startOwnProcessing();
   }
}
  
void
TransportSelector::buildFdSet(FdSet& fdset)
{
   for(TransportList::iterator it = mSharedProcessTransports.begin(); 
       it != mSharedProcessTransports.end(); it++)
   {
      (*it)->buildFdSet(fdset);
   }
}

void
TransportSelector::process(FdSet& fdset)
{
   for(TransportList::iterator it = mSharedProcessTransports.begin(); 
       it != mSharedProcessTransports.end(); it++)
   {
      try
      {
         (*it)->process(fdset);
      }
      catch (BaseException& e)
      {
         ErrLog (<< "Exception thrown from Transport::process: " << e);
      }
   }
}

bool
TransportSelector::hasDataToSend() const
{
   for(TransportList::const_iterator it = mSharedProcessTransports.begin();
       it != mSharedProcessTransports.end(); it++)
   {
      if ((*it)->hasDataToSend())
      {
         return true;
      }
   }
   return false;
}

//!jf! the problem here is that DnsResult is returned after looking
//mDns.lookup() but this can result in a synchronous call to handle() which
//assumes that dnsresult has been assigned to the TransactionState
//!dcm! -- now 2-phase to fix this
DnsResult*
TransportSelector::createDnsResult(DnsHandler* handler)
{
   return mDns.createDnsResult(handler);
}

void
TransportSelector::dnsResolve(DnsResult* result,
                              SipMessage* msg)
{
   // Picking the target destination:
   //   - for request, use forced target if set
   //     otherwise use loose routing behaviour (route or, if none, request-uri)
   //   - for response, use forced target if set, otherwise look at via

   if (msg->isRequest())
   {
      // If this is an ACK we need to fix the tid to reflect that
      if (msg->hasForceTarget())
      {
         //DebugLog(<< "!ah! RESOLVING request with force target : " << msg->getForceTarget() );
         mDns.lookup(result, msg->getForceTarget());
      }
      else if (msg->exists(h_Routes) && !msg->header(h_Routes).empty())
      {
         // put this into the target, in case the send later fails, so we don't
         // lose the target
         msg->setForceTarget(msg->header(h_Routes).front().uri());
         DebugLog (<< "Looking up dns entries (from route) for " << msg->getForceTarget());
         mDns.lookup(result, msg->getForceTarget());
      }
      else
      {
         DebugLog (<< "Looking up dns entries for " << msg->header(h_RequestLine).uri());
         mDns.lookup(result, msg->header(h_RequestLine).uri());
      }
   }
   else if (msg->isResponse())
   {
      ErrLog(<<"unimplemented response dns");
      assert(0);
   }
   else
   {
      assert(0);
   }
}

bool isDgramTransport (TransportType type)
{
   static const bool unknown_transport = false;
   switch(type)
   {
      case UDP:
      case DTLS:
      case DCCP:
      case SCTP:
         return   true;

      case TCP:
      case TLS:
         return   false;

      default:
         assert(unknown_transport);
         return unknown_transport;  // !kh! just to make it compile wo/warning.
   }
}

Tuple
TransportSelector::getFirstInterface(bool is_v4, TransportType type)
{
// !kh! both getaddrinfo() and IPv6 are not supported by cygwin, yet.
#ifdef __CYGWIN__
   assert(0);
   return Tuple();
#else
   // !kh!
   // 1. Query local hostname.
   char hostname[256] = "";
   if(gethostname(hostname, sizeof(hostname)) != 0)
   {
      int e = getErrno();
      Transport::error( e );
      InfoLog(<< "Can't query local hostname : [" << e << "] " << strerror(e) );
      throw Transport::Exception("Can't query local hostname", __FILE__, __LINE__);
   }
   InfoLog(<< "Local hostname is [" << hostname << "]");

   // !kh!
   // 2. Resolve address(es) of local hostname for specified transport.
   const bool is_dgram = isDgramTransport(type);
   addrinfo hint;
   memset(&hint, 0, sizeof(hint));
   hint.ai_family    = is_v4 ? PF_INET : PF_INET6;
   hint.ai_flags     = AI_PASSIVE;
   hint.ai_socktype  = is_dgram ? SOCK_DGRAM : SOCK_STREAM;

   addrinfo* results;
   int ret = getaddrinfo(
      hostname,
      0,
      &hint,
      &results);

   if(ret != 0)
   {
      Transport::error( ret ); // !kh! is this the correct sematics? ret is not errno.
      InfoLog(<< "Can't resolve " << hostname << "'s address : [" << ret << "] " << gai_strerror(ret) );
      throw Transport::Exception("Can't resolve hostname", __FILE__,__LINE__);
   }

   // !kh!
   // 3. Use first address resolved if there are more than one.
   // What should I do if there are more than one address?
   // i.e. results->ai_next != 0.
   Tuple source(*(results->ai_addr), type);
   InfoLog(<< "Local address is " << source);
   addrinfo* ai = results->ai_next;
   for(; ai; ai = ai->ai_next)
   {
      Tuple addr(*(ai->ai_addr), type);
      InfoLog(<<"Additional address " << addr);
   }
   freeaddrinfo(results);
   
   return source;
#endif
}

Tuple
TransportSelector::determineSourceInterface(SipMessage* msg, const Tuple& target) const
{
   assert(msg->exists(h_Vias));
   assert(!msg->header(h_Vias).empty());
   const Via& via = msg->header(h_Vias).front();

   if (msg->isRequest() && !via.sentHost().empty())
      // hint provided in sent-by of via by application
   {
      DebugLog( << "hint provided by app: " <<  msg->header(h_Vias).front());      
      return Tuple(via.sentHost(), via.sentPort(), target.ipVersion(), target.getType());
   }
   else
   {
      Tuple source(target);
#if defined(WIN32) && !defined(NO_IPHLPAPI)
      try
      {
         GenericIPAddress addr = WinCompat::determineSourceInterface(target.toGenericIPAddress());
         source.setSockaddr(addr);
      }
      catch (WinCompat::Exception&)
      {
         ErrLog (<< "Can't find source interface to use");
         throw Transport::Exception("Can't find source interface", __FILE__, __LINE__);
      }
#else
      // !kh!
      // The connected UDP technique doesn't work all the time.
      // 1. Might not work on all implementaions as stated in UNP vol.1 8.14.
      // 2. Might not work under unspecified condition on Windows,
      //    search "getsockname" in MSDN library.
      // 3. We've experienced this issue on our production software.
      
      // this process will determine which interface the kernel would use to
      // send a packet to the target by making a connect call on a udp socket.
      Socket tmp = INVALID_SOCKET;
      if (target.isV4())
      {
         if (mSocket == INVALID_SOCKET)
         {
            mSocket = InternalTransport::socket(UDP, V4); // may throw
         }
         tmp = mSocket;
      }
      else
      {
         if (mSocket6 == INVALID_SOCKET)
         {
            mSocket6 = InternalTransport::socket(UDP, V6); // may throw
         }
         tmp = mSocket6;
      }

      int ret = connect(tmp,&target.getSockaddr(), target.length());
      if (ret < 0)
      {
         int e = getErrno();
         Transport::error( e );
         InfoLog(<< "Unable to route to " << target << " : [" << e << "] " << strerror(e) );
         throw Transport::Exception("Can't find source address for Via", __FILE__,__LINE__);
      }

      socklen_t len = source.length();
      ret = getsockname(tmp,&source.getMutableSockaddr(), &len);
      if (ret < 0)
      {
         int e = getErrno();
         Transport::error(e);
         InfoLog(<< "Can't determine name of socket " << target << " : " << strerror(e) );
         throw Transport::Exception("Can't find source address for Via", __FILE__,__LINE__);
      }

      // !kh! test if connected UDP technique results INADDR_ANY, i.e. 0.0.0.0.
      // if it does, assume the first avaiable interface.
      if(source.isV4())
      {
         long src = (reinterpret_cast<const sockaddr_in*>(&source.getSockaddr())->sin_addr.s_addr);
         if(src == INADDR_ANY)
         {
            InfoLog(<< "Connected UDP failed to determine source address, use first address instaed.");
            source = getFirstInterface(true, target.getType());
         }
      }
      else  // IPv6
      {
//should never reach here in WIN32 w/ V6 support
#if defined(USE_IPV6) && !defined(WIN32) 
         if (source.isAnyInterface())  //!dcm! -- when could this happen?
         {
            source = getFirstInterface(false, target.getType());
         }
# endif
      }
      // Unconnect.
      // !jf! This is necessary, but I am not sure what we can do if this
      // fails. I'm not sure the stack can recover from this error condition.
      if (target.isV4())
      {
         ret = connect(mSocket,
                       (struct sockaddr*)&mUnspecified.v4Address,
                       sizeof(mUnspecified.v4Address));
      }
#ifdef USE_IPV6
      else
      {
         ret = connect(mSocket6,
                       (struct sockaddr*)&mUnspecified6.v6Address,
                       sizeof(mUnspecified6.v6Address));
      }
#else
      else
      {
         assert(0);
      }
#endif

      if ( ret<0 )
      {
         int e =  getErrno();
         //.dcm. OS X 10.5 workaround, we could #ifdef for specific OS X version.
         if  (!(e ==EAFNOSUPPORT || e == EADDRNOTAVAIL))
         {
            ErrLog(<< "Can't disconnect socket :  " << strerror(e) );
            Transport::error(e);
            throw Transport::Exception("Can't disconnect socket", __FILE__,__LINE__);
         }
      }
#endif
      
      // This is the port that the request will get sent out from. By default, 
      // this value will be 0, since the Helper that creates the request will not
      // assign it. In this case, the stack will pick an arbitrary (but appropriate)
      // transport. If it is non-zero, it will only match transports that are bound to
      // the specified port (and fail if none are available)

      if(msg->isRequest())
      {
         source.setPort(via.sentPort());
      }
      else
      {
         source.setPort(0);
      }
 
      DebugLog (<< "Looked up source for destination: " << target
                << " -> " << source
                << " sent-by=" << via.sentHost()
                << " sent-port=" << via.sentPort());
      
      return source;
   }
}

// !jf! there may be an extra copy of a tuple here. can probably get rid of it
// but there are some const issues.
void
TransportSelector::transmit(SipMessage* msg, Tuple& target)
{
   assert(msg);

   if(msg->mIsDecorated)
   {
      msg->rollbackOutboundDecorators();
   }

   try
   {
      // !ah! You NEED to do this for responses too -- the transport doesn't
      // !ah! know it's IP addres(es) in all cases, AND it's function of the dest.
      // (imagine a synthetic message...)

      Tuple source;
      // .bwc. We need 3 things here:
      // 1) A Transport* to call send() on.
      // 2) A complete Tuple to pass in this call (target).
      // 3) A host, port, and protocol for filling out the topmost via, and
      //    possibly stuff like Contact, Referred-By, and other headers that
      //    must specify a hostname that the TU was unable to supply, because
      //    it didn't know what interface/port the message would be sent on.
      //    (source)
      /*
         Our Transport* might be found in target. If so, we can skip this block
         of stuff.
         Alternatively, we might not have the transport to start with. However,
         given a connection id, we will be able to find the Connection we
         should use, we can get the Transport we want. If we have no connection
         id, but we know we are using TLS or DTLS and have a tls hostname, we
         can use the hostname to find the appropriate transport. If all else
         fails, we must resort to the connected UDP trick to fill out source,
         which in turn is used to look up a matching transport.

         Given the transport, it is always possible to get the port/protocol, 
         and usually possible to get the host (if it is bound to INADDR_ANY, we
         can't tell). However, if we had to fill out source in order to find 
         the transport in the first place, this is not an issue.
      */

      Data remoteSigcompId;

      if (msg->isRequest())
      {
         Transport* transport=0;

         transport = findTransportByDest(msg,target);
         
         if(!transport && target.mFlowKey && target.onlyUseExistingConnection)
         {
            // .bwc. Connection info was specified, and use of this connection 
            // was mandatory, but connection is no longer around. We fail.
         }
         else if (transport)// .bwc. Here we use transport to find source.
         {
            source = transport->getTuple();

            // .bwc. If the transport has an ambiguous interface, we need to
            //look a little closer.
            if(source.isAnyInterface())
            {
               Tuple temp = determineSourceInterface(msg,target);

               // .bwc. determineSourceInterface() can give us a port, if the TU
               // put one in the topmost Via.
               assert(source.ipVersion()==temp.ipVersion() &&
                        source.getType()==temp.getType());
               source=temp;

               /* determineSourceInterface might return an arbitrary port 
                  here, so use the port specified in target.transport->port().
               */
               if(source.getPort()==0)
               {
                  source.setPort(transport->port());
               }
            }
         }
         // .bwc. Here we use source to find transport.
         else
         {
            source = determineSourceInterface(msg, target);
            transport = findTransportBySource(source);
            
            // .bwc. determineSourceInterface might give us a port
            if(transport && source.getPort()==0)
            {
               source.setPort(transport->port());
            }
         }
                  
         target.transport=transport;
         
         // .bwc. Topmost Via is only filled out in the request case. Also, if
         // we don't have a transport at this point, we're going to fail,
         // so don't bother doing the work.
         if(target.transport)
         {
            Via& topVia(msg->header(h_Vias).front());
            topVia.remove(p_maddr); // !jf! why do this?

            // insert the via
            if (topVia.transport().empty())
            {
               topVia.transport() = Tuple::toData(source.getType());
            }
            if (!topVia.sentHost().size())
            {
               msg->header(h_Vias).front().sentHost() = Tuple::inet_ntop(source);
            }
            if (!topVia.sentPort())
            {
               msg->header(h_Vias).front().sentPort() = source.getPort();
            }

            if (mCompression.isEnabled())
            {
               // Indicate support for SigComp, if appropriate.
               if (!topVia.exists(p_comp))
               {
                  topVia.param(p_comp) = "sigcomp";
               }
               if (!topVia.exists(p_sigcompId))
               {
                  topVia.param(p_sigcompId) = mCompression.getSigcompId();
               }

               // Figure out remote identifier (from Route header
               // field, if present; otherwise, from Request-URI).
               // XXX rohc-sip-sigcomp-03 says to use +sip.instance,
               // but this is impossible to actually do if you're
               // not actually the registrar, and really hard even
               // if you are.
               Uri destination;// (*reinterpret_cast<Uri*>(0)); // !bwc! What?

               if(msg->exists(h_Routes) && 
                  !msg->header(h_Routes).empty())
               {
                  destination = msg->header(h_Routes).front().uri();
               }
               else
               {
                  destination = msg->header(h_RequestLine).uri();
               }

               if (destination.exists(p_comp) &&
                   destination.param(p_comp) == "sigcomp")
               {
                  if (destination.exists(p_sigcompId))
                  {
                      remoteSigcompId = destination.param(p_sigcompId);
                  }
                  else
                  {
                      remoteSigcompId = destination.host();
                  }
               }
               // Squirrel the compartment away in the branch so that
               // we can figure out (pre-transaction-matching) what
               // compartment incoming requests are associated with.
               topVia.param(p_branch).setSigcompCompartment(remoteSigcompId);
            }
         }
         
      }
      else if (msg->isResponse())
      {
         // We assume that all stray responses have been discarded, so we always
         // know the transport that the corresponding request was received on
         // and this has been copied by TransactionState::sendToWire into target.transport
         assert(target.transport);
         
         source = target.transport->getTuple();

         // .bwc. If the transport has an ambiguous interface, we need to
         //look a little closer.
         if(source.isAnyInterface())
         {
            Tuple temp = source;
            source = determineSourceInterface(msg,target);
            assert(source.ipVersion()==temp.ipVersion() &&
                     source.getType()==temp.getType());

            /* determineSourceInterface might return an arbitrary port here,
               so use the port specified in target.transport->port().
            */
            if(source.getPort()==0)
            {
               source.setPort(target.transport->port());
            }
         }
         if (mCompression.isEnabled())
         {
            // Figure out remote identifier (from Via header field).
            Via& topVia(msg->header(h_Vias).front());

            if(topVia.exists(p_comp) &&
               topVia.param(p_comp) == "sigcomp")
            {
               if (topVia.exists(p_sigcompId))
               {
                   remoteSigcompId = topVia.param(p_sigcompId);
               }
               else
               {
                   // XXX rohc-sigcomp-sip-03 says "sent-by",
                   // but this should probably be "received" if present,
                   // and "sent-by" otherwise.
                   // XXX Also, the spec is ambiguous about whether
                   // to include the port in this identifier.
                   remoteSigcompId = topVia.sentHost();
               }
            }
         }
      }
      else
      {
         assert(0);
      }

      // .bwc. At this point, source, target.transport, and target should be
      // _fully_ specified.

      if (target.transport)
      {
         // !bwc! TODO This filling in of stuff really should be handled with
         // the callOutboundDecorators() callback. (Or, at the very least,
         // we should allow this code to be turned off through configuration.
         // There are plenty of cases where this stuff is not at all necessary.)
         // There is a contact header and it contains exactly one entry
         if (msg->exists(h_Contacts) && msg->header(h_Contacts).size()==1)
         {
            for (NameAddrs::iterator i=msg->header(h_Contacts).begin(); i != msg->header(h_Contacts).end(); i++)
            {
               NameAddr& contact = *i;
               // No host specified, so use the ip address and port of the
               // transport used. Otherwise, leave it as is.
               if (contact.uri().host().empty())
               {
                  contact.uri().host() = (target.transport->hasSpecificContact() ? 
                                          target.transport->interfaceName() : 
                                          Tuple::inet_ntop(source) );
                  contact.uri().port() = target.transport->port();

                  if (target.transport->transport() != UDP && !contact.uri().exists(p_gr))
                  {
                     contact.uri().param(p_transport) = Tuple::toData(target.transport->transport());
                  }

                  // Add comp=sigcomp to contact URI
                  // Also, If no +sip.instance on contact HEADER,
                  // add sigcomp-id="<urn>" to contact URI.
                  if (mCompression.isEnabled())
                  {
                     if (!contact.uri().exists(p_comp))
                     {
                        contact.uri().param(p_comp) = "sigcomp";
                     }
                     if (!contact.exists(p_Instance) &&
                         !contact.uri().exists(p_sigcompId))
                     {
                        contact.uri().param(p_sigcompId) 
                          = mCompression.getSigcompId();
                     }
                  }
               } 
               else
               {
                  if (contact.uri().exists(p_addTransport))
                  {
                     if (target.getType() != UDP)
                     {
                        contact.uri().param(p_transport) = Tuple::toData(target.getType());
                     }
                     contact.uri().remove(p_addTransport);
                  }
               }

            }
         }

         // Fix the Referred-By header if no host specified.
         // If malformed, leave it alone.
         if (msg->exists(h_ReferredBy) 
               && msg->header(h_ReferredBy).isWellFormed())
         {
            if (msg->header(h_ReferredBy).uri().host().empty())
            {
               msg->header(h_ReferredBy).uri().host() = Tuple::inet_ntop(source);
               msg->header(h_ReferredBy).uri().port() = target.transport->port();
            }
         }

         // .bwc. Only try fiddling with this is if the Record-Route is well-
         // formed. If the topmost Record-Route is malformed, we have no idea
         // whether it came from something the TU synthesized or from the wire.
         // We shouldn't touch it. Frankly, I take issue with the method we have
         // chosen to signal to the stack that we want it to fill out various
         // header-field-values. 
         if (msg->exists(h_RecordRoutes) 
               && !msg->header(h_RecordRoutes).empty() 
               && msg->header(h_RecordRoutes).front().isWellFormed())
         {
            NameAddr& rr = msg->header(h_RecordRoutes).front();
            if (rr.uri().host().empty())
            {
               rr.uri().host() = Tuple::inet_ntop(source);
               rr.uri().port() = target.transport->port();
               if (target.getType() != UDP && !rr.uri().exists(p_transport))
               {
                  rr.uri().param(p_transport) = Tuple::toData(target.getType());
               }
               // Add comp=sigcomp and sigcomp-id="<urn>" to Record-Route
               // XXX This isn't quite right -- should be set only
               // on routes facing the client. Doing this correctly
               // requires double-record-routing
               if (mCompression.isEnabled())
               {
                  if (!rr.uri().exists(p_comp))
                  {
                     rr.uri().param(p_comp) = "sigcomp";
                  }
                  if (!rr.uri().exists(p_sigcompId))
                  {
                     rr.uri().param(p_sigcompId) = mCompression.getSigcompId();
                  }
               }
            }
         }
         
         // See draft-ietf-sip-identity
         if (mSecurity && msg->exists(h_Identity) && msg->header(h_Identity).value().empty())
         {
            DateCategory now;
            msg->header(h_Date) = now;
#if defined(USE_SSL)
            try
            {
               const Data& domain = msg->header(h_From).uri().host();
               msg->header(h_Identity).value() = mSecurity->computeIdentity( domain,
                                                                             msg->getCanonicalIdentityString());
            }
            catch (Security::Exception& e)
            {
               InfoLog (<< "Couldn't add identity header: " << e);
               msg->remove(h_Identity);
               if (msg->exists(h_IdentityInfo)) 
               {
                  msg->remove(h_IdentityInfo);
               }                  
            }
#endif
         }

         // Call back anyone who wants to perform outbound decoration
         msg->callOutboundDecorators(source, target,remoteSigcompId);

         Data& encoded = msg->getEncoded();
         encoded.clear();
         DataStream encodeStream(encoded);
         msg->encode(encodeStream);
         encodeStream.flush();
         msg->getCompartmentId() = remoteSigcompId;
         
         assert(!msg->getEncoded().empty());
         DebugLog (<< "Transmitting to " << target
                   << " tlsDomain=" << msg->getTlsDomain()
                   << " via " << source
				   << std::endl << std::endl << encoded.escaped()
				   << "sigcomp id=" << remoteSigcompId);

         target.transport->send(target, encoded, msg->getTransactionId(),
                                remoteSigcompId);
      }
      else
      {
         InfoLog (<< "tid=" << msg->getTransactionId() << " failed to find a transport to " << target);
         mStateMacFifo.add(new TransportFailure(msg->getTransactionId(), TransportFailure::NoTransport));
      }

   }
   catch (Transport::Exception& )
   {
      InfoLog (<< "tid=" << msg->getTransactionId() << " no route to target: " << target);
      mStateMacFifo.add(new TransportFailure(msg->getTransactionId(), TransportFailure::NoRoute));
      return;
   }
}

void
TransportSelector::retransmit(SipMessage* msg, Tuple& target)
{
   assert(target.transport);

   // !jf! The previous call to transmit may have blocked or failed (It seems to
   // block in windows when the network is disconnected - don't know why just
   // yet.

   // !kh!
   // My speculation for blocking is; when network is disconnected, WinSock sets
   // a timer (to give up) and tries to defer reporting the error, in hope of
   // the network would be recovered. Before the timer fires, applications could
   // still select() (or the likes) their socket descriptors and find they are
   // writable if there is still room in buffer (per socket). If the send()s or
   // sendto()s made during this time period overflows the buffer, it blocks.
   // But I somewhat doubt this would be noticed, because block would be brief,
   // once the timer fires, the blocked call would return error.
   // Note that the block applies to both UDP and TCP sockets.
   // Quote from Linux man page:
   // When the message does not fit into the send buffer of the socket,  send
   // normally  blocks, unless the socket has been placed in non-blocking I/O
   // mode. In non-blocking mode it would return EAGAIN in this case.
   // Quote from MSDN library:
   // If no buffer space is available within the transport system to hold the
   // data to be transmitted, sendto will block unless the socket has been
   // placed in a nonblocking mode.

   if(!msg->getEncoded().empty())
   {
      //DebugLog(<<"!ah! retransmit to " << target);
      target.transport->send(target, msg->getEncoded(), msg->getTransactionId(), msg->getCompartmentId());
   }
}

unsigned int
TransportSelector::sumTransportFifoSizes() const
{
   unsigned int sum = 0;

   for (AnyPortTupleMap::const_iterator i = mAnyPortTransports.begin();
        i != mAnyPortTransports.end(); ++i)
   {
      sum += i->second->getFifoSize();
   }

   for (AnyPortAnyInterfaceTupleMap::const_iterator i = mAnyPortAnyInterfaceTransports.begin();
        i != mAnyPortAnyInterfaceTransports.end(); ++i)
   {
      sum += i->second->getFifoSize();
   }

   for (TlsTransportMap::const_iterator i = mTlsTransports.begin();
        i != mTlsTransports.end(); ++i)
   {
      sum += i->second->getFifoSize();
   }

   return sum;
}

bool
TransportSelector::connectionAlive(const Tuple& target) const
{
   return (findConnection(target)!=0);
}

const Connection*
TransportSelector::findConnection(const Tuple& target) const
{
   // !bwc! If we can find a match in the ConnectionManager, we can
   // determine what Transport this needs to be sent on. This may also let
   // us know immediately what our source needs to be.
   if(target.getType()==TCP || target.getType()==TLS)
   {
      TcpBaseTransport* tcpb=0;
      Connection* conn=0;
      TypeToTransportMap::const_iterator i;
      TypeToTransportMap::const_iterator l=mTypeToTransportMap.lower_bound(target);
      TypeToTransportMap::const_iterator u=mTypeToTransportMap.upper_bound(target);
      for(i=l;i!=u;++i)
      {
         tcpb=static_cast<TcpBaseTransport*>(i->second);
         conn = tcpb->getConnectionManager().findConnection(target);
         if(conn)
         {
            return conn;
         }
      }
   }
   
   return 0;
}


Transport*
TransportSelector::findTransportByDest(SipMessage* msg, Tuple& target)
{
   if(!target.transport)
   {
      if(target.getType()==UDP || target.getType()==DTLS)
      {
         if(target.mFlowKey)
         {
            std::map<FlowKey,Transport*>::iterator i=mConnectionlessMap.find(target.mFlowKey);
            if(i!=mConnectionlessMap.end())
            {
               return i->second;
            }
         }
      }
      else if(target.getType()==TCP || target.getType()==TLS)
      {
         // .bwc. We might find a match by the cid, or maybe using the
         // tuple itself.
         const Connection* conn = findConnection(target);
         
         if(conn) // .bwc. Woohoo! Home free!
         {
            return conn->transport();
         }
         else if(target.onlyUseExistingConnection)
         {
            // .bwc. Connection no longer exists, so we fail.
            return 0;
         }
      }
      
      // !bwc! No luck finding with a FlowKey.
      if(target.getType()==TLS || target.getType()==DTLS)
      {
         return findTlsTransport(msg->getTlsDomain(),target.getType(),target.ipVersion());
      }
            
   }
   else // .bwc. Easy as pie.
   {
      return target.transport;
   }

   // .bwc. No luck here. Maybe findTransportBySource will end up working.
   return 0; 
}

Transport*
TransportSelector::findTransportBySource(Tuple& search)
{
   DebugLog(<< "findTransportBySource(" << search << ")");

   if (search.getPort() != 0)
   {
      //0. When we are sending to a loopback address, the kernel makes an
      //(effectively) arbitrary choice of which loopback address to send
      //from. (Since any loopback address can be used to send to any other
      //loopback address) This choice may not agree with our idea of what
      //address we should be sending from, so we need to just choose the
      //loopback address we like, and ignore what the kernel told us to do.
      if( search.isLoopback() )
      {
         ExactTupleMap::const_iterator i;
         for (i=mExactTransports.begin();i != mExactTransports.end();i++)
         {
            DebugLog(<<"search: " << search << " elem: " << i->first);
            if(i->first.ipVersion()==V4)
            {
               //Compare only the first byte (the 127)
               if(i->first.isEqualWithMask(search,8,false))
               {
                  search=i->first;
                  DebugLog(<<"Match!");
                  return i->second;
               }
            }
#ifdef USE_IPV6
            else if(i->first.ipVersion()==V6)
            {
               //What to do?
            }
#endif
            else
            {
               assert(0);
            }
         }
      }

      // 1. search for matching port on a specific interface
      {
         ExactTupleMap::const_iterator i = mExactTransports.find(search);
         if (i != mExactTransports.end())
         {
            DebugLog(<< "findTransport (exact) => " << *(i->second));
            return i->second;
         }
      }

      // 2. search for specific port on ANY interface
      {
         AnyInterfaceTupleMap::const_iterator i = mAnyInterfaceTransports.find(search);
         if (i != mAnyInterfaceTransports.end())
         {
            DebugLog(<< "findTransport (any interface) => " << *(i->second));
            return i->second;
         }
      }
   }
   else
   {
      //0. When we are sending to a loopback address, the kernel makes an
      //(effectively) arbitrary choice of which loopback address to send
      //from. (Since any loopback address can be used to send to any other
      //loopback address) This choice may not agree with our idea of what
      //address we should be sending from, so we need to just choose the
      //loopback address we like, and ignore what the kernel told us to do.
      if( search.isLoopback() )
      {
         ExactTupleMap::const_iterator i;
         for (i=mExactTransports.begin();i != mExactTransports.end();i++)
         {
            DebugLog(<<"search: " << search << " elem: " << i->first);
            if(i->first.ipVersion()==V4)
            {
               //Compare only the first byte (the 127)
               if(i->first.isEqualWithMask(search,8,true))
               {
                  search=i->first;
                  DebugLog(<<"Match!");
                  return i->second;
               }
            }
#ifdef USE_IPV6
            else if(i->first.ipVersion()==V6)
            {
               //What to do?
            }
#endif
            else
            {
               assert(0);
            }
         }
      }

      // 1. search for ANY port on specific interface
      {
         AnyPortTupleMap::const_iterator i = mAnyPortTransports.find(search);
         if (i != mAnyPortTransports.end())
         {
            DebugLog(<< "findTransport (any port, specific interface) => " << *(i->second));
            return i->second;
         }
      }

      // 2. search for ANY port on ANY interface
      {
         //CerrLog(<< "Trying AnyPortAnyInterfaceTupleMap " << mAnyPortAnyInterfaceTransports.size());
         AnyPortAnyInterfaceTupleMap::const_iterator i = mAnyPortAnyInterfaceTransports.find(search);
         if (i != mAnyPortAnyInterfaceTransports.end())
         {
            DebugLog(<< "findTransport (any port, any interface) => " << *(i->second));
            return i->second;
         }
      }
   }

   DebugLog (<< "Exact interface / Specific port: " << Inserter(mExactTransports));
   DebugLog (<< "Any interface / Specific port: " << Inserter(mAnyInterfaceTransports));
   DebugLog (<< "Exact interface / Any port: " << Inserter(mAnyPortTransports));
   DebugLog (<< "Any interface / Any port: " << Inserter(mAnyPortAnyInterfaceTransports));

   WarningLog(<< "Can't find matching transport " << search);
   return 0;
}


Transport*
TransportSelector::findTlsTransport(const Data& domainname,resip::TransportType type,resip::IpVersion version)
{
   assert(type==TLS || type==DTLS);
   DebugLog (<< "Searching for " << ((type==TLS) ? "TLS" : "DTLS") << " transport for domain='" 
                  << domainname << "'" << " have " << mTlsTransports.size());

   if (domainname == Data::Empty)
   {
      for(TlsTransportMap::iterator i=mTlsTransports.begin();
            i!=mTlsTransports.end();++i)
      {
         if(i->first.mType==type && i->first.mVersion==version)
         {
            DebugLog(<<"Found a default transport.");
            return i->second;
         }
      }
   }
   else
   {
      TlsTransportKey key(domainname,type,version);
   
      TlsTransportMap::iterator i=mTlsTransports.find(key);
      
      if(i!=mTlsTransports.end())
      {
         DebugLog(<< "Found a transport.");
         return i->second;
      }
   }
   
   DebugLog(<<"No transport found.");
   return 0;
}

unsigned int 
TransportSelector::getTimeTillNextProcessMS()
{
   return INT_MAX;
}

void
TransportSelector::registerMarkListener(MarkListener* listener)
{
   mDns.getMarkManager().registerMarkListener(listener);
}

void TransportSelector::unregisterMarkListener(MarkListener* listener)
{
   mDns.getMarkManager().unregisterMarkListener(listener);
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
