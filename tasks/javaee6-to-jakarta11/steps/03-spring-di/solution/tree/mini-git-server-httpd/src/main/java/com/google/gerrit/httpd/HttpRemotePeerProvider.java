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

package com.google.gerrit.httpd;

import org.springframework.beans.factory.BeanCreationException;
import org.springframework.beans.factory.ObjectFactory;
import org.springframework.beans.factory.annotation.Autowired;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.net.UnknownHostException;

import javax.servlet.http.HttpServletRequest;

class HttpRemotePeerProvider implements ObjectFactory<SocketAddress> {
  /** Proxy onto the request being served on this thread. */
  private final HttpServletRequest req;

  @Autowired
  HttpRemotePeerProvider(final HttpServletRequest r) {
    req = r;
  }

  @Override
  public SocketAddress getObject() {
    final String addr = req.getRemoteAddr();
    final int port = req.getRemotePort();
    try {
      return new InetSocketAddress(InetAddress.getByName(addr), port);
    } catch (UnknownHostException e) {
      throw new BeanCreationException("Cannot get @RemotePeer", e);
    }
  }
}
