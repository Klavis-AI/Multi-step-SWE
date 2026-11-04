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

import org.apache.sshd.common.SshException;
import org.apache.sshd.common.config.keys.AuthorizedKeyEntry;
import org.apache.sshd.common.config.keys.PublicKeyEntryResolver;
import org.apache.sshd.common.keyprovider.KeyPairProvider;
import org.apache.sshd.common.util.buffer.ByteArrayBuffer;
import org.eclipse.jgit.lib.Constants;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.StringReader;
import java.security.GeneralSecurityException;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.PublicKey;
import java.security.interfaces.DSAPublicKey;
import java.security.interfaces.RSAPublicKey;
import java.security.spec.InvalidKeySpecException;
import java.util.Base64;

/** Utilities to support SSH operations. */
public class SshUtil {
  /**
   * Parse a public key into its Java type.
   *
   * @param encodedKeyString the account key to parse.
   * @return the valid public key object.
   * @throws InvalidKeySpecException the key supplied is not a valid SSH key.
   * @throws NoSuchAlgorithmException the JVM is missing the key algorithm.
   * @throws NoSuchProviderException the JVM is missing the provider.
   */
  public static PublicKey parse(final String encodedKeyString)
      throws NoSuchAlgorithmException, InvalidKeySpecException,
      NoSuchProviderException {
    try {
      if (encodedKeyString == null) {
        throw new InvalidKeySpecException("No key string");
      }
      final byte[] bin = Base64.getMimeDecoder().decode(
          Constants.encodeASCII(encodedKeyString));
      return new ByteArrayBuffer(bin).getRawPublicKey();
    } catch (RuntimeException | SshException e) {
      throw new InvalidKeySpecException("Cannot parse key", e);
    }
  }



  /**
   * Convert an RFC 4716 style key to an OpenSSH style key.
   *
   * @param keyStr the key string to convert.
   * @return <code>keyStr</code> if conversion failed; otherwise the converted
   *         key, in OpenSSH key format.
   */
  public static String toOpenSshPublicKey(final String keyStr) {
    try {
      final StringBuilder strBuf = new StringBuilder();
      final BufferedReader br = new BufferedReader(new StringReader(keyStr));
      String line = br.readLine(); // BEGIN SSH2 line...
      if (!line.equals("---- BEGIN SSH2 PUBLIC KEY ----")) {
        return keyStr;
      }

      while ((line = br.readLine()) != null) {
        if (line.indexOf(':') == -1) {
          strBuf.append(line);
          break;
        }
      }

      while ((line = br.readLine()) != null) {
        if (line.startsWith("---- ")) {
          break;
        }
        strBuf.append(line);
      }

      final PublicKey key =
          new ByteArrayBuffer(Base64.getMimeDecoder().decode(
              Constants.encodeASCII(strBuf.toString()))).getRawPublicKey();
      if (key instanceof RSAPublicKey) {
        strBuf.insert(0, KeyPairProvider.SSH_RSA + " ");

      } else if (key instanceof DSAPublicKey) {
        strBuf.insert(0, KeyPairProvider.SSH_DSS + " ");

      } else {
        return keyStr;
      }

      strBuf.append(' ');
      strBuf.append("converted-key");
      return strBuf.toString();
    } catch (IOException | RuntimeException e) {
      return keyStr;
    }
  }


    /**
     * Parse one line of an {@code authorized_keys} file into its Java type.
     * <p>
     * The SSH library understands this format itself, so splitting the line by
     * hand and feeding the second field to a client library is no longer
     * necessary: an entry may legitimately carry login options before the key
     * type, and {@link AuthorizedKeyEntry} accounts for them.
     *
     * @param line the {@code authorized_keys} entry to parse.
     * @return the valid public key object.
     * @throws IOException the entry could not be decoded.
     * @throws GeneralSecurityException the key itself is not valid.
     */
    public static PublicKey parseOpenSSHKey(String line)
        throws IOException, GeneralSecurityException {
      return AuthorizedKeyEntry.parseAuthorizedKeyEntry(line)
          .resolvePublicKey(null, PublicKeyEntryResolver.FAILING);
    }
}
