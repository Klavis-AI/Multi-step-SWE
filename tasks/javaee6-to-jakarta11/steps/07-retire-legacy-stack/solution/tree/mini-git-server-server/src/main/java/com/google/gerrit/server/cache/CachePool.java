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

package com.google.gerrit.server.cache;

import static java.util.concurrent.TimeUnit.MINUTES;
import static java.util.concurrent.TimeUnit.SECONDS;

import com.github.benmanes.caffeine.cache.Caffeine;
import com.google.gerrit.server.config.ConfigUtil;
import com.google.gerrit.server.config.GerritServerConfig;

import org.eclipse.jgit.lib.Config;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.SmartLifecycle;

import java.time.Duration;
import java.util.HashMap;
import java.util.Map;

/** Pool of all declared caches created by {@link CacheModule}s. */
public class CachePool {
  public static class Lifecycle implements SmartLifecycle {
    /** Second up and second from last down: the caches read repositories. */
    private static final int PHASE = 2;

    private final CachePool cachePool;
    private volatile boolean running;

    public Lifecycle(final CachePool cachePool) {
      this.cachePool = cachePool;
    }

    @Override
    public int getPhase() {
      return PHASE;
    }

    @Override
    public boolean isRunning() {
      return running;
    }

    @Override
    public void start() {
      cachePool.start();
      running = true;
    }

    @Override
    public void stop() {
      running = false;
      cachePool.stop();
    }
  }

  private final Config config;

  private final Object lock = new Object();
  private final Map<String, CacheProvider<?, ?>> caches;
  private boolean started;

  @Autowired
  CachePool(@GerritServerConfig final Config cfg) {
    this.config = cfg;
    this.caches = new HashMap<>();
  }

  private void start() {
    synchronized (lock) {
      if (started) {
        throw new IllegalStateException("Cache pool has already been started");
      }

      final Factory factory = new Factory();
      for (CacheProvider<?, ?> p : caches.values()) {
        factory.open(p);
      }
      started = true;
    }
  }

  private void stop() {
    synchronized (lock) {
      for (CacheProvider<?, ?> p : caches.values()) {
        p.unbind();
      }
      started = false;
    }
  }

  <K, V> ProxyCache<K, V> register(final CacheProvider<K, V> provider) {
    synchronized (lock) {
      if (started) {
        throw new IllegalStateException("Cache pool has already been started");
      }

      final String n = provider.getName();
      if (caches.containsKey(n) && caches.get(n) != provider) {
        throw new IllegalStateException("Cache \"" + n + "\" already defined");
      }
      caches.put(n, provider);
      return new ProxyCache<>(n);
    }
  }

  private class Factory {
    /**
     * Build the backing cache described by {@code p} and hand it to the proxy
     * the module handed out at declaration time.
     */
    <K, V> void open(final CacheProvider<K, V> p) {
      final String name = p.getName();
      final long ttl = getSeconds(name, "maxage", p.maxAge());

      final Caffeine<Object, Object> b = Caffeine.newBuilder();
      b.maximumSize(getInt(name, "memorylimit", p.memoryLimit()));
      if (ttl > 0) {
        b.expireAfterWrite(Duration.ofSeconds(ttl));
      }

      p.bind(b.<K, V> build(), ttl);
    }

    private int getInt(String n, String s, int d) {
      return config.getInt("cache", n, s, d);
    }

    private long getSeconds(String n, String s, long d) {
      d = MINUTES.convert(d, SECONDS);
      long m = ConfigUtil.getTimeUnit(config, "cache", n, s, d, MINUTES);
      return SECONDS.convert(m, MINUTES);
    }
  }
}
