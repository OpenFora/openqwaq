

#include "resip/stack/SipMessage.hxx"
#include "resip/dum/ClientAuthManager.hxx"
#include "resip/dum/DialogUsageManager.hxx"
#include "resip/dum/MasterProfile.hxx"
#include "resip/dum/ServerAuthManager.hxx"

#include "B2BUA.hxx"
#include "DefaultAuthorizationManager.hxx"
#include "DialogUsageManagerRecurringTask.hxx"
#include "Logging.hxx"
#include "MyAppDialog.hxx"
#include "MyDialogSetHandler.hxx"
#include "MyInviteSessionHandler.hxx"

using namespace b2bua;
using namespace resip;
using namespace std;

B2BUA::B2BUA(AuthorizationManager *authorizationManager, CDRHandler& cdrHandler) {

  if(authorizationManager == NULL) {
    authorizationManager = new DefaultAuthorizationManager();
  }

  taskManager = new TaskManager();

  sipStack = new SipStack();
  dialogUsageManager = new DialogUsageManager(*sipStack);
  uasMasterProfile = SharedPtr<MasterProfile>(new MasterProfile);
  dialogUsageManager->setMasterProfile(uasMasterProfile);
  auto_ptr<AppDialogSetFactory> myAppDialogSetFactory(new MyAppDialogSetFactory);
  dialogUsageManager->setAppDialogSetFactory(myAppDialogSetFactory);

  // Set up authentication when we act as UAC
  auto_ptr<ClientAuthManager> clientAuth(new ClientAuthManager);
  dialogUsageManager->setClientAuthManager(clientAuth);

  dialogUsageManager->setDialogSetHandler(new MyDialogSetHandler());

  DialogUsageManagerRecurringTask *dialogUsageManagerTask = new DialogUsageManagerRecurringTask(*sipStack, *dialogUsageManager);
  taskManager->addRecurringTask(dialogUsageManagerTask);
  callManager = new B2BCallManager(*dialogUsageManager, authorizationManager, cdrHandler);
  taskManager->addRecurringTask(callManager);

  MyInviteSessionHandler *uas = new MyInviteSessionHandler(*dialogUsageManager, *callManager);
  dialogUsageManager->setInviteSessionHandler(uas);

}

B2BUA::~B2BUA() {
}

void B2BUA::setAuthorizationManager(AuthorizationManager *authorizationManager) {
  this->authorizationManager = authorizationManager;
  callManager->setAuthorizationManager(authorizationManager);
}

void B2BUA::run() {
  taskManager->start(); 
}

void B2BUA::logStats() {
  callManager->logStats();
}

void B2BUA::stop() {
  //B2BUA_LOG_CRIT("B2BUA::stop not implemented!");
  //assert(0);
  B2BUA_LOG_NOTICE("B2BUA beginning shutdown process");
  taskManager->stop();
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

