// Copyright (C) 2010 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.google.gerrit.sshd;

import org.springframework.beans.factory.ObjectFactory;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.beans.factory.annotation.Autowired;

import java.net.SocketAddress;

class SshRemotePeerProvider implements ObjectFactory<SocketAddress> {
  private final ObjectProvider<SshSession> session;

  @Autowired
  SshRemotePeerProvider(final ObjectProvider<SshSession> s) {
    session = s;
  }

  @Override
  public SocketAddress getObject() {
    return session.getObject().getRemoteAddress();
  }
}
