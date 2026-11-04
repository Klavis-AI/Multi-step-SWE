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

package com.google.gerrit.server.ssh;

import java.util.Objects;

/**
 * One public host key of the SSH daemon, paired with the address it is
 * advertised for.
 * <p>
 * This used to be jsch's host-key class, which was on the classpath only so
 * that a server could name its own keys in the shape of a {@code known_hosts}
 * line. Nothing here is a client concern, so nothing here needs an SSH client
 * library.
 */
public class HostKey {
  private final String host;
  private final String type;
  private final String key;

  /**
   * @param host the address this key is advertised for, in
   *        {@code known_hosts} notation ({@code host}, {@code host:port} or
   *        {@code [ipv6]:port}).
   * @param type the SSH key type name, e.g. {@code ssh-rsa}.
   * @param key the Base64 encoding of the key in SSH wire format.
   */
  public HostKey(final String host, final String type, final String key) {
    this.host = Objects.requireNonNull(host, "host");
    this.type = Objects.requireNonNull(type, "type");
    this.key = Objects.requireNonNull(key, "key");
  }

  public String getHost() {
    return host;
  }

  public String getType() {
    return type;
  }

  public String getKey() {
    return key;
  }

  @Override
  public String toString() {
    return host + " " + type + " " + key;
  }
}
