// Copyright (C) 2010 The Android Open Source Project
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

import com.google.gerrit.server.config.GerritServerConfig;
import com.google.gerrit.server.git.WorkQueue;

import org.eclipse.jgit.lib.Config;
import org.springframework.beans.factory.ObjectFactory;
import org.springframework.beans.factory.annotation.Autowired;

/**
 * The queue behind {@code gerrit stream-events}, sized by
 * {@code sshd.streamThreads}.
 * <p>
 * As with the interactive and batch queues, the number is now how many stream
 * commands may run at once rather than how many threads stand ready for them,
 * and the {@link Thread#MIN_PRIORITY} the workers used to be given has gone
 * with the platform threads that could carry it.
 */
class StreamCommandExecutorProvider implements ObjectFactory<WorkQueue.Executor> {
  private final int poolSize;
  private final WorkQueue queues;

  @Autowired
  StreamCommandExecutorProvider(@GerritServerConfig final Config config,
      final WorkQueue wq) {
    final int cores = Runtime.getRuntime().availableProcessors();
    poolSize = config.getInt("sshd", "streamThreads", cores + 1);
    queues = wq;
  }

  @Override
  public WorkQueue.Executor getObject() {
    return queues.createQueue(poolSize, "SSH-Stream-Worker");
  }
}
