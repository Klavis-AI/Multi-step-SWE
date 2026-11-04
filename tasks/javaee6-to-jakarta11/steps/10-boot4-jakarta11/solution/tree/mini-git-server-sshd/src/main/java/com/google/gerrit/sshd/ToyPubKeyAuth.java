// Copyright (C) 2008 The Android Open Source Project
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

import com.google.gerrit.server.IdentifiedUser;
import com.google.gerrit.server.config.SitePaths;

import org.apache.sshd.server.auth.pubkey.PublickeyAuthenticator;
import org.apache.sshd.server.session.ServerSession;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.IOException;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.PublicKey;
import java.util.ArrayList;
import java.util.List;

/**
 * Authenticates by public key really permissively
 */
class ToyPubKeyAuth implements PublickeyAuthenticator {
    private static final Logger log =
      LoggerFactory.getLogger(ToyPubKeyAuth.class);

    private final IdentifiedUser.GenericFactory userFactory;
    private final SitePaths sitePaths;

  ToyPubKeyAuth(IdentifiedUser.GenericFactory uf, final SitePaths sitePaths) {
    userFactory = uf;
        this.sitePaths = sitePaths;
    }
	@Override
	public boolean authenticate(String username, PublicKey publicKey, ServerSession serverSession) {
		final SshSession sd = serverSession.getAttribute(SshSession.KEY);
        File usersDir = new File(sitePaths.etc_dir, "users"); //sitePaths.
        File userDir = new File(usersDir,username);
        if (!userDir.exists() || !userDir.isDirectory()) {
            String error = "User ssh key folder not found " + userDir;
            log.warn(error);
            sd.authenticationError(username, error);
            return false;
        }
        // Was FileUtils.listFiles(userDir, {"pub"}, false): the glob is the same
        // suffix match, and isRegularFile is commons-io's FILE filter. The
        // listing is still materialised so the "no good keys" line below can
        // name the files it looked at.
        final List<Path> files = new ArrayList<>();
        try (DirectoryStream<Path> pubKeys =
                 Files.newDirectoryStream(userDir.toPath(), "*.pub")) {
            for (Path candidate : pubKeys) {
                if (Files.isRegularFile(candidate)) {
                    files.add(candidate);
                }
            }
        } catch (IOException e) {
            log.error("Cannot list " + userDir, e);
        }
        for (Path publicKeyFile : files) {
            log.info("Trying " + publicKeyFile);
            try {
                PublicKey authorisedKey=SshUtil.parseOpenSSHKey(Files.readString(publicKeyFile));

                if (authorisedKey.equals(publicKey)) {
                    sd.authenticationSuccess(username, userFactory.create(username));
                    return true;  //To change body of implemented methods use File | Settings | File Templates.
                }
            } catch (Exception e) {
                log.error("Problem with " + publicKeyFile, e);
            }
        }
        log.warn("No good keys found from "+files);
        sd.authenticationError(username, "No matching key found");
        return false;
	}
}
