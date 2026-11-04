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

import com.google.gerrit.server.CurrentUser;
import com.google.gerrit.server.IdentifiedUser;

import org.springframework.beans.factory.BeanCreationException;
import org.springframework.beans.factory.ObjectFactory;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.beans.factory.annotation.Autowired;

class SshIdentifiedUserProvider implements ObjectFactory<IdentifiedUser> {
  private final ObjectProvider<SshSession> session;
  private final IdentifiedUser.GenericFactory factory;

  @Autowired
  SshIdentifiedUserProvider(ObjectProvider<SshSession> s,
      IdentifiedUser.GenericFactory f) {
    session = s;
    factory = f;
  }

  @Override
  public IdentifiedUser getObject() {
    final CurrentUser user = session.getObject().getCurrentUser();
    if (user instanceof final IdentifiedUser u) {
      return factory.create(u.getUserName());
    }
    throw new BeanCreationException("Not signed in SshIdentifiedUserProvider");
  }
}
