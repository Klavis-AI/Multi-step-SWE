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

import com.github.benmanes.caffeine.cache.Cache;

import java.util.function.Function;

/**
 * A named cache whose backing store is installed after the bean is created.
 * <p>
 * {@link CachePool} hands one of these back at registration time, long before
 * the pool has been started and the real caches exist. The pool binds the
 * backing store during startup; every method here fails loudly rather than
 * quietly if it is called before that happens.
 */
final class ProxyCache<K, V> {
  private final String name;
  private volatile Cache<K, V> self;
  private volatile long ttlSeconds;

  ProxyCache(final String name) {
    this.name = name;
  }

  void bind(final Cache<K, V> backing, final long timeToLiveSeconds) {
    this.self = backing;
    this.ttlSeconds = timeToLiveSeconds;
  }

  void unbind() {
    final Cache<K, V> c = self;
    if (c != null) {
      c.invalidateAll();
    }
    self = null;
  }

  String getName() {
    return name;
  }

  long getTimeToLiveSeconds() {
    return ttlSeconds;
  }

  private Cache<K, V> backing() {
    final Cache<K, V> c = self;
    if (c == null) {
      throw new IllegalStateException("Cache \"" + name + "\" is not running");
    }
    return c;
  }

  V getIfPresent(final K key) {
    return backing().getIfPresent(key);
  }

  V get(final K key, final Function<? super K, ? extends V> creator) {
    return backing().get(key, creator);
  }

  void put(final K key, final V value) {
    backing().put(key, value);
  }

  void invalidate(final K key) {
    backing().invalidate(key);
  }

  void invalidateAll() {
    backing().invalidateAll();
  }
}
