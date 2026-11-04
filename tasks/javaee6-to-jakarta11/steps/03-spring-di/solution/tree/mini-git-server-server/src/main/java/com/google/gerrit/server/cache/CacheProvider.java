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

import static com.google.gerrit.server.cache.EvictionPolicy.LFU;
import static java.util.concurrent.TimeUnit.DAYS;
import static java.util.concurrent.TimeUnit.SECONDS;

import org.springframework.beans.factory.BeanCreationException;
import org.springframework.beans.factory.ObjectFactory;

import java.util.concurrent.TimeUnit;

import net.sf.ehcache.Ehcache;

final class CacheProvider<K, V> implements ObjectFactory<Cache<K, V>>,
    NamedCacheBinding<K, V>, UnnamedCacheBinding<K, V> {
  private final CacheModule module;
  private final boolean disk;
  private int memoryLimit;
  private int diskLimit;
  private long maxAge;
  private EvictionPolicy evictionPolicy;
  private String cacheName;
  private ProxyEhcache cache;
  private ObjectFactory<EntryCreator<K, V>> entryCreator;

  CacheProvider(final boolean disk, final CacheModule module,
      final CachePool pool) {
    this.disk = disk;
    this.module = module;
    this.cache = pool.register(this);

    memoryLimit(1024);
    maxAge(90, DAYS);
    evictionPolicy(LFU);

    if (disk) {
      diskLimit(16384);
    }
  }

  void bind(final Ehcache ehcache) {
    cache.bind(ehcache);
  }

  String getName() {
    if (cacheName == null) {
      throw new BeanCreationException("Cache has no name");
    }
    return cacheName;
  }

  boolean disk() {
    return disk;
  }

  int memoryLimit() {
    return memoryLimit;
  }

  int diskLimit() {
    return diskLimit;
  }

  long maxAge() {
    return maxAge;
  }

  EvictionPolicy evictionPolicy() {
    return evictionPolicy;
  }

  public NamedCacheBinding<K, V> name(final String name) {
    if (cacheName != null) {
      throw new IllegalStateException("Cache name already set");
    }
    cacheName = name;
    return this;
  }

  public NamedCacheBinding<K, V> memoryLimit(final int objects) {
    memoryLimit = objects;
    return this;
  }

  public NamedCacheBinding<K, V> diskLimit(final int objects) {
    if (!disk) {
      // TODO This should really be a compile time type error, but I'm
      // too lazy to create the mess of permutations required to setup
      // type safe returns for bindings in our little DSL.
      //
      throw new IllegalStateException("Cache is not disk based");
    }
    diskLimit = objects;
    return this;
  }

  public NamedCacheBinding<K, V> maxAge(final long duration, final TimeUnit unit) {
    maxAge = SECONDS.convert(duration, unit);
    return this;
  }

  @Override
  public NamedCacheBinding<K, V> evictionPolicy(final EvictionPolicy policy) {
    evictionPolicy = policy;
    return this;
  }

  public NamedCacheBinding<K, V> populateWith(
      Class<? extends EntryCreator<K, V>> creator) {
    entryCreator = module.getEntryCreator(this, creator);
    return this;
  }

  public Cache<K, V> getObject() {
    if (cache == null) {
      throw new BeanCreationException("Cache \"" + cacheName + "\" not available");
    }
    if (entryCreator != null) {
      return new PopulatingCache<>(cache, entryCreator.getObject());
    }
    return new SimpleCache<>(cache);
  }
}
