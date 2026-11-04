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

package com.google.gerrit.server.config;

import com.google.gerrit.server.AnonymousUser;
import com.google.gerrit.server.GerritPersonIdent;
import com.google.gerrit.server.GerritPersonIdentProvider;
import com.google.gerrit.server.IdentifiedUser;
import com.google.gerrit.server.cache.CachePool;
import com.google.gerrit.server.git.LocalDiskRepositoryManager;
import com.google.gerrit.server.git.TransferConfig;
import com.google.gerrit.server.git.WorkQueue;
import com.google.gerrit.server.util.IdGenerator;

import org.eclipse.jgit.lib.Config;
import org.eclipse.jgit.lib.PersonIdent;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.SmartLifecycle;
import org.springframework.context.annotation.Import;

/**
 * Starts global state with standard dependencies.
 * <p>
 * {@link LocalDiskRepositoryManager} is the only implementation of
 * {@code GitRepositoryManager} registered here, so an injection point asking
 * for the interface resolves to it.
 */
@Configuration(proxyBeanMethods = false)
@Import({AnonymousUser.class, IdentifiedUser.GenericFactory.class,
    IdGenerator.class, CachePool.class, LocalDiskRepositoryManager.class,
    WorkQueue.class, TransferConfig.class})
public class GerritGlobalConfiguration {
  @Bean
  @GerritPersonIdent
  PersonIdent gerritPersonIdent(@GerritServerConfig final Config config) {
    return new GerritPersonIdentProvider(config).getObject();
  }

  // The three below are started when this context is refreshed and stopped
  // when it is closed, in the ascending order of the phase each one declares
  // and in the reverse of it on the way down. The order is the repositories,
  // then the caches they are read through, and then the queue that runs work
  // against both.

  @Bean
  SmartLifecycle repositoryManagerLifecycle(
      @GerritServerConfig final Config config) {
    return new LocalDiskRepositoryManager.Lifecycle(config);
  }

  @Bean
  SmartLifecycle cachePoolLifecycle(final CachePool cachePool) {
    return new CachePool.Lifecycle(cachePool);
  }

  @Bean
  SmartLifecycle workQueueLifecycle(final WorkQueue workQueue) {
    return new WorkQueue.Lifecycle(workQueue);
  }
}
