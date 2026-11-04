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

import org.springframework.beans.factory.BeanFactory;
import org.springframework.beans.factory.ObjectFactory;
import org.springframework.beans.factory.annotation.Autowired;

/**
 * Miniature DSL to support declaring {@link Cache} instances.
 * <p>
 * A subclass is a {@code @Configuration} class whose {@code @Bean} methods
 * return the bindings built here; each binding is itself the
 * {@link ObjectFactory} that creates the cache, so a bean method declares a
 * cache by returning {@code core("name").maxAge(...).getObject()}.
 */
public abstract class CacheModule {
  @Autowired
  private CachePool pool;

  @Autowired
  private BeanFactory beanFactory;

  /**
   * Declare an unnamed in-memory cache.
   *
   * @param <K> type of key used to lookup entries.
   * @param <V> type of value stored by the cache.
   * @return binding to describe the cache. Caller must set at least the name on
   *         the returned binding.
   */
  protected <K, V> UnnamedCacheBinding<K, V> core() {
    return new CacheProvider<>(this, pool);
  }

  /**
   * Declare a named in-memory cache.
   *
   * @param <K> type of key used to lookup entries.
   * @param <V> type of value stored by the cache.
   * @param name name of the cache, as it appears in the configuration file.
   * @return binding to describe the cache.
   */
  protected <K, V> NamedCacheBinding<K, V> core(final String name) {
    return this.<K, V> core().name(name);
  }

  <K, V> ObjectFactory<EntryCreator<K, V>> getEntryCreator(
      CacheProvider<K, V> cp, final Class<? extends EntryCreator<K, V>> type) {
    return () -> beanFactory.getBean(type);
  }
}
