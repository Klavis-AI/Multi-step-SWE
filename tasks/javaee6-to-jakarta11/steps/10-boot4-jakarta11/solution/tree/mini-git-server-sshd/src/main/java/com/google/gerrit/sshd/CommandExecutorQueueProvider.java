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

/**
 * The two queues an SSH command can be given, sized from {@code gerrit.config}.
 * <p>
 * {@code sshd.threads} and {@code sshd.batchThreads} are read exactly as they
 * always were, and still mean how much of the server one kind of command may
 * occupy at once. What they no longer buy is threads: a queue starts a virtual
 * thread per task, so these are the permits a task takes while it runs rather
 * than the size of a pool it waits in front of.
 * <p>
 * The queues used to hand their workers out at {@link Thread#MIN_PRIORITY}, so
 * that a command which turned CPU-bound lost to the daemon's own I/O threads.
 * A virtual thread has no priority to set -- {@code setPriority} is ignored and
 * {@code getPriority} always answers {@code NORM_PRIORITY} -- so that knob is
 * gone rather than ported, and the scheduling it asked for is now the
 * concurrency limit's job.
 */
public class CommandExecutorQueueProvider implements QueueProvider {

  private int poolSize;
  private final int batchThreads;
  private final WorkQueue.Executor interactiveExecutor;
  private final WorkQueue.Executor batchExecutor;

  public CommandExecutorQueueProvider(@GerritServerConfig final Config config,
      final WorkQueue queues) {
    final int cores = Runtime.getRuntime().availableProcessors();
    poolSize = config.getInt("sshd", "threads", 3 * cores / 2);
    batchThreads = config.getInt("sshd", "batchThreads", 0);
    if (batchThreads > poolSize) {
      poolSize += batchThreads;
    }
    int interactiveThreads = Math.max(1, poolSize - batchThreads);
    interactiveExecutor = queues.createQueue(interactiveThreads,
        "SSH-Interactive-Worker");
    if (batchThreads !=  0) {
      batchExecutor = queues.createQueue(batchThreads, "SSH-Batch-Worker");
    } else {
      batchExecutor = interactiveExecutor;
    }
  }

  @Override
  public WorkQueue.Executor getInteractiveQueue() {
    return interactiveExecutor;
  }

  @Override
  public WorkQueue.Executor getBatchQueue() {
    return batchExecutor;
  }

}
