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

import com.google.gerrit.server.RequestCleanup;

import org.springframework.beans.factory.ObjectFactory;
import org.springframework.beans.factory.config.Scope;

import java.util.HashMap;
import java.util.Map;

/** Spring scopes for state during an SSH connection. */
class SshScope {
  /** Name {@link #REQUEST} is registered under, and the one to scope beans to. */
  static final String NAME = "request";

  static class Context {
    private final RequestCleanup cleanup;
    private final SshSession session;
    private final String commandLine;
    private final Map<String, Object> map;

    final long created;
    volatile long started;
    volatile long finished;

    Context(final SshSession s, final String c) {
      cleanup = new RequestCleanup();
      session = s;
      commandLine = c;

      map = new HashMap<>();

      final long now = System.currentTimeMillis();
      created = now;
      started = now;
      finished = now;
    }

    private Context(Context p, SshSession s, String c) {
      cleanup = new RequestCleanup();
      session = s;
      commandLine = c;

      map = new HashMap<>();

      created = p.created;
      started = p.started;
      finished = p.finished;

      p.cleanup.add(cleanup);
    }

    SshSession getSession() {
      return session;
    }

    RequestCleanup getCleanup() {
      return cleanup;
    }

    String getCommandLine() {
      return commandLine;
    }

    synchronized Object get(String name, ObjectFactory<?> creator) {
      Object t = map.get(name);
      if (t == null) {
        t = creator.getObject();
        map.put(name, t);
      }
      return t;
    }

    synchronized Object remove(String name) {
      return map.remove(name);
    }

    synchronized Context subContext(SshSession newSession, String newCommandLine) {
      return new Context(this, newSession, newCommandLine);
    }
  }

  private static final ThreadLocal<Context> current =
      new ThreadLocal<>();

  static Context getContext() {
    final Context ctx = current.get();
    if (ctx == null) {
      throw new IllegalStateException("Not in command/request");
    }
    return ctx;
  }

  static Context set(Context ctx) {
    Context old = current.get();
    current.set(ctx);
    return old;
  }

  /** Returns exactly one instance per command executed. */
  static final Scope REQUEST = new Scope() {
    @Override
    public Object get(final String name, final ObjectFactory<?> objectFactory) {
      return getContext().get(name, objectFactory);
    }

    @Override
    public Object remove(final String name) {
      return getContext().remove(name);
    }

    @Override
    public void registerDestructionCallback(final String name,
        final Runnable callback) {
      // Nothing scoped to a command declares a destruction callback: a
      // command is torn down by the RequestCleanup its context owns.
    }

    @Override
    public Object resolveContextualObject(final String key) {
      return null;
    }

    @Override
    public String getConversationId() {
      return null;
    }

    @Override
    public String toString() {
      return "SshScopes.REQUEST";
    }
  };

  private SshScope() {
  }
}
