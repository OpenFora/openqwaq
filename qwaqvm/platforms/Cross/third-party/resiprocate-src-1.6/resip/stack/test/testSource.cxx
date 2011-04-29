#ifdef HAVE_CONFIG_H
# include "resiprocate/config.hxx"
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include "rutil/Data.hxx"
#include "rutil/DnsUtil.hxx"
#include "rutil/Tuple.hxx"

int
main(int argc, char *argv[])
{

   int sockfd;
   char buffer[1024];

   socklen_t len;
   struct sockaddr_in cliaddr, servaddr;

   if (argc != 2)
   {
      exit(0);
   }
   using namespace resip;

   sockfd = socket(AF_INET, SOCK_DGRAM, 0);

   memset(&servaddr, 0, sizeof(servaddr));
   servaddr.sin_family = AF_INET;
   const int serv_port = 9999;
   servaddr.sin_port = htons(9999);

   inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
   inet_ntop(AF_INET, &servaddr.sin_addr, buffer,1024);
   
   std::cout << "Sending to : " << buffer << std::endl;
   
   connect(sockfd, (const sockaddr*)&servaddr, sizeof(servaddr));

   len = sizeof(cliaddr);
   getsockname(sockfd, (sockaddr*)&cliaddr, &len);
   inet_ntop(AF_INET, &cliaddr.sin_addr, buffer, 1024);
   std::cout << "new local source is: " << buffer << std::endl;
   std::cout << "DnsUtil::inet_ntop = " 
             << DnsUtil::inet_ntop(cliaddr.sin_addr)
             << std::endl;


   return 0;
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
