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

package com.google.gerrit.sshd;

import com.google.gerrit.server.config.SitePaths;
import com.google.inject.Inject;
import com.google.inject.Provider;
import com.google.inject.ProvisionException;

import org.apache.sshd.common.keyprovider.FileKeyPairProvider;
import org.apache.sshd.common.keyprovider.KeyPairProvider;
import org.apache.sshd.server.keyprovider.SimpleGeneratorHostKeyProvider;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

class HostKeyProvider implements Provider<KeyPairProvider> {
  private final SitePaths site;

  @Inject
  HostKeyProvider(final SitePaths site) {
    this.site = site;
  }

  @Override
  public KeyPairProvider get() {
    final Path objKey = site.ssh_key.toPath();
    final Path rsaKey = site.ssh_rsa.toPath();
    final Path dsaKey = site.ssh_dsa.toPath();

    final List<Path> stdKeys = new ArrayList<>(2);
    if (Files.exists(rsaKey)) {
      stdKeys.add(absolutePathOf(rsaKey));
    }
    if (Files.exists(dsaKey)) {
      stdKeys.add(absolutePathOf(dsaKey));
    }

    if (Files.exists(objKey)) {
      if (stdKeys.isEmpty()) {
        return new SimpleGeneratorHostKeyProvider(absolutePathOf(objKey));

      } else {
        // Both formats of host key exist, we don't know which format
        // should be authoritative. Complain and abort.
        //
        stdKeys.add(absolutePathOf(objKey));
        throw new ProvisionException("Multiple host keys exist: " + stdKeys);
      }

    } else {
      if (stdKeys.isEmpty()) {
        throw new ProvisionException("No SSH keys under " + site.etc_dir);
      }
      // No Bouncy Castle check here any more. The PEM parsers ship with the
      // library now, so an OpenSSL-format host key is readable out of the box
      // and the provider is handed the paths directly rather than their names.
      //
      return new FileKeyPairProvider(stdKeys);
    }
  }

  private static Path absolutePathOf(final Path key) {
    return key.toAbsolutePath();
  }
}
