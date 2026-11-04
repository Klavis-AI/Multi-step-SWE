// Copyright (C) 2009 The Android Open Source Project
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

package com.google.gerrit.server;

import com.google.gerrit.server.config.GerritServerConfig;

import org.eclipse.jgit.lib.Config;
import org.eclipse.jgit.lib.PersonIdent;
import org.eclipse.jgit.lib.UserConfig;
import org.springframework.beans.factory.ObjectFactory;
import org.springframework.beans.factory.annotation.Autowired;

/** Provides {@link PersonIdent} annotated with {@link GerritPersonIdent}. */
public class GerritPersonIdentProvider implements ObjectFactory<PersonIdent> {
  private final String name;
  private final String email;

  @Autowired
  public GerritPersonIdentProvider(@GerritServerConfig final Config cfg) {
    String name = cfg.getString("user", null, "name");
    if (name == null) {
      name = "Gerrit Code Review";
    }
    this.name = name;
    email = cfg.get(UserConfig.KEY).getCommitterEmail();
  }

  @Override
  public PersonIdent getObject() {
    return new PersonIdent(name, email);
  }

}
