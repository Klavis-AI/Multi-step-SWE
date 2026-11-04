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

package com.google.gerrit.server.git;

import com.google.gerrit.server.util.IdGenerator;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.SmartLifecycle;
import org.springframework.scheduling.concurrent.SimpleAsyncTaskScheduler;

import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Delayed execution of tasks, one virtual thread per task.
 * <p>
 * A queue used to be a pool of worker threads with a queue of runnables in
 * front of it, and its size was the number of tasks that could be in flight at
 * once because a task owned its thread for as long as it ran. A queue is now a
 * {@link SimpleAsyncTaskScheduler} started on virtual threads: every task gets
 * its own, immediately, and a task that blocks on a socket or a lock costs a
 * parked continuation rather than a platform thread.
 * <p>
 * The size a queue is created with therefore no longer buys threads. It is
 * kept, and is now a concurrency limit: the permits in {@link Executor#slots},
 * taken by the task on its own virtual thread rather than by whoever submitted
 * it. That distinction is the whole reason the limit is not
 * {@code SimpleAsyncTaskExecutor#setConcurrencyLimit}, which throttles by
 * blocking the submitting thread -- here an SSH daemon thread, which must not
 * be made to wait for a command to finish.
 */
public class WorkQueue {
  public static class Lifecycle implements SmartLifecycle {
    /** Third up and third from last down: it runs work against both below. */
    private static final int PHASE = 3;

    private final WorkQueue workQueue;
    private volatile boolean running;

    public Lifecycle(final WorkQueue workQeueue) {
      this.workQueue = workQeueue;
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
      running = true;
    }

    @Override
    public void stop() {
      running = false;
      workQueue.stop();
    }
  }

  private static final Logger log = LoggerFactory.getLogger(WorkQueue.class);

  /** How long a closing queue waits for the tasks still running in it. */
  private static final long TERMINATION_MILLIS = 10_000L;

  private Executor defaultQueue;
  private final IdGenerator idGenerator;
  private final CopyOnWriteArrayList<Executor> queues;

  @Autowired
  WorkQueue(final IdGenerator idGenerator) {
    this.idGenerator = idGenerator;
    this.queues = new CopyOnWriteArrayList<>();
  }

  /** Get the default work queue, for miscellaneous tasks. */
  public synchronized Executor getDefaultQueue() {
    if (defaultQueue == null) {
      defaultQueue = createQueue(1, "WorkQueue");
    }
    return defaultQueue;
  }

  /**
   * Create a new executor queue.
   *
   * @param concurrency how many of this queue's tasks may run at the same
   *        time; zero or less for as many as have been submitted.
   * @param prefix the name every thread the queue starts is given, with a
   *        number after it.
   */
  public Executor createQueue(final int concurrency, final String prefix) {
    final Executor r = new Executor(concurrency, prefix);
    queues.add(r);
    return r;
  }

  /** Get all of the tasks currently scheduled in any work queue. */
  public List<Task<?>> getTasks() {
    final List<Task<?>> r = new ArrayList<>();
    for (final Executor e : queues) {
      e.addAllTo(r);
    }
    return r;
  }

  /** Locate a task by its unique id, null if no task matches. */
  public Task<?> getTask(final int id) {
    Task<?> result = null;
    for (final Executor e : queues) {
      final Task<?> t = e.getTask(id);
      if (t != null) {
        if (result != null) {
          // Don't return the task if we have a duplicate. Lie instead.
          return null;
        } else {
          result = t;
        }
      }
    }
    return result;
  }

  private void stop() {
    for (final Executor p : queues) {
      p.shutdown();
    }
    queues.clear();
  }

  /** An isolated queue. */
  public class Executor {
    private final ConcurrentHashMap<Integer, Task<?>> all;
    private final SimpleAsyncTaskScheduler scheduler;

    /** Permits for the tasks allowed to run at once, null if unlimited. */
    private final Semaphore slots;

    Executor(final int concurrency, final String prefix) {
      scheduler = new SimpleAsyncTaskScheduler();
      scheduler.setVirtualThreads(true);
      scheduler.setThreadNamePrefix(prefix + "-");
      scheduler.setTaskTerminationTimeout(TERMINATION_MILLIS);
      scheduler.setErrorHandler(e -> log.error("WorkQueue thread "
          + Thread.currentThread().getName() + " threw exception", e));

      slots = 0 < concurrency ? new Semaphore(concurrency) : null;

      all = new ConcurrentHashMap<>( //
          Math.max(concurrency, 1) << 1, // table size
          0.75f, // load factor
          Math.max(concurrency, 1) + 4 // concurrency level
          );
    }

    /** Run a task as soon as a slot in this queue is free. */
    public Future<?> submit(final Runnable runnable) {
      final Task<?> task = register(runnable, System.nanoTime());
      task.attach(scheduler.submit(task));
      return task;
    }

    /** Run a task once the delay has passed and a slot in this queue is free. */
    public ScheduledFuture<?> schedule(final Runnable runnable,
        final long delay, final TimeUnit unit) {
      final long nanos = unit.toNanos(delay);
      final Task<?> task = register(runnable, System.nanoTime() + nanos);
      task.attach(scheduler.schedule(task, Instant.now().plusNanos(nanos)));
      return task;
    }

    /**
     * Stop accepting tasks, then wait out the ones already running.
     * <p>
     * Stopping the scheduler drops what has not started yet, which is the
     * old queue's shutdown policy for delayed and periodic tasks; closing it
     * waits {@link WorkQueue#TERMINATION_MILLIS} for the rest.
     */
    void shutdown() {
      scheduler.stop();
      scheduler.close();
    }

    private <V> Task<V> register(final Runnable runnable, final long startAt) {
      for (;;) {
        final int id = idGenerator.next();

        Task<V> task;

        if (runnable instanceof final ProjectRunnable pr) {
          task = new ProjectTask<>(pr, this, id, startAt);
        } else {
          task = new Task<>(runnable, this, id, startAt);
        }

        if (all.putIfAbsent(task.getTaskId(), task) == null) {
          return task;
        }
      }
    }

    /**
     * Take one of this queue's slots, on the task's own virtual thread.
     *
     * @return false if the wait was interrupted, which is how a task that was
     *         cancelled before it started finds out.
     */
    boolean acquire() {
      if (slots == null) {
        return true;
      }
      try {
        slots.acquire();
        return true;
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        return false;
      }
    }

    void release() {
      if (slots != null) {
        slots.release();
      }
    }

    void remove(final Task<?> task) {
      all.remove(task.getTaskId(), task);
    }

    Task<?> getTask(final int id) {
      return all.get(id);
    }

    void addAllTo(final List<Task<?>> list) {
      list.addAll(all.values()); // iterator is thread safe
    }
  }

  /** Runnable needing to know it was canceled. */
  public interface CancelableRunnable extends Runnable {
    /** Notifies the runnable it was canceled. */
    public void cancel();
  }

  /** A wrapper around a scheduled Runnable, as maintained in the queue. */
  public static class Task<V> implements RunnableScheduledFuture<V> {
    /**
     * Summarized status of a single task.
     * <p>
     * Tasks have the following state flow:
     * <ol>
     * <li>{@link #SLEEPING}: if scheduled with a non-zero delay.</li>
     * <li>{@link #READY}: waiting for a free slot in its queue.</li>
     * <li>{@link #RUNNING}: actively executing on its own virtual thread.</li>
     * <li>{@link #DONE}: finished executing, if not periodic.</li>
     * </ol>
     */
    public static enum State {
      // Ordered like this so ordinal matches the order we would
      // prefer to see tasks sorted in: done before running,
      // running before ready, ready before sleeping.
      //
      DONE, CANCELLED, RUNNING, READY, SLEEPING, OTHER;
    }

    private final Runnable runnable;
    private final Executor executor;
    private final int taskId;

    /** {@link System#nanoTime()} this task is due at. */
    private final long startAt;

    private final AtomicBoolean running;
    private final AtomicBoolean cancelled;
    private final CountDownLatch finished;

    /** The scheduler's own handle on this task, for cancellation. */
    private volatile Future<?> handle;

    Task(Runnable runnable, Executor executor, int taskId, long startAt) {
      this.runnable = runnable;
      this.executor = executor;
      this.taskId = taskId;
      this.startAt = startAt;
      this.running = new AtomicBoolean();
      this.cancelled = new AtomicBoolean();
      this.finished = new CountDownLatch(1);
    }

    /**
     * Hand the task the future the scheduler made for it.
     * <p>
     * The task may be running, or even finished, by the time this arrives,
     * and it may already have been cancelled through {@link #cancel(boolean)}
     * by a command that had it out of the queue; the late handle is cancelled
     * in turn rather than dropped.
     */
    void attach(final Future<?> handle) {
      this.handle = handle;
      if (cancelled.get()) {
        handle.cancel(true);
      }
    }

    public int getTaskId() {
      return taskId;
    }

    public State getState() {
      if (isCancelled()) {
        return State.CANCELLED;
      } else if (isDone() && !isPeriodic()) {
        return State.DONE;
      } else if (running.get()) {
        return State.RUNNING;
      }

      final long delay = getDelay(TimeUnit.MILLISECONDS);
      if (delay <= 0) {
        return State.READY;
      } else if (0 < delay) {
        return State.SLEEPING;
      }

      return State.OTHER;
    }

    public boolean cancel(boolean mayInterruptIfRunning) {
      // A task that has already run cannot be cancelled, and the two answers
      // are decided under the same lock the last line of run() takes so that
      // a task finishing right now gets one of them and not both.
      //
      synchronized (finished) {
        if (isDone() || !cancelled.compareAndSet(false, true)) {
          return false;
        }
      }

      final Future<?> h = handle;
      if (h != null) {
        h.cancel(mayInterruptIfRunning);
      }

      // Tiny abuse of running: if the task needs to know it was
      // canceled (to clean up resources) and it hasn't started
      // yet the task's run method won't execute. So we tag it
      // as running and allow it to clean up. This ensures we do
      // not invoke cancel twice.
      //
      if (runnable instanceof final CancelableRunnable cr
          && running.compareAndSet(false, true)) {
        cr.cancel();
      }
      executor.remove(this);
      finished.countDown();
      return true;
    }

    public int compareTo(Delayed o) {
      return Long.compare(getDelay(TimeUnit.NANOSECONDS),
          o.getDelay(TimeUnit.NANOSECONDS));
    }

    public V get() throws InterruptedException, ExecutionException {
      finished.await();
      return result();
    }

    public V get(long timeout, TimeUnit unit) throws InterruptedException,
        ExecutionException, TimeoutException {
      if (!finished.await(timeout, unit)) {
        throw new TimeoutException();
      }
      return result();
    }

    private V result() {
      if (isCancelled()) {
        throw new CancellationException();
      }
      return null;
    }

    public long getDelay(TimeUnit unit) {
      return unit.convert(startAt - System.nanoTime(), TimeUnit.NANOSECONDS);
    }

    public boolean isCancelled() {
      return cancelled.get();
    }

    public boolean isDone() {
      return finished.getCount() == 0;
    }

    /**
     * Nothing in this server schedules a repeating task, so no task is
     * periodic; the queue keeps the question because {@link Task} has always
     * answered it, and {@link #getState()} still asks.
     */
    public boolean isPeriodic() {
      return false;
    }

    public void run() {
      try {
        if (isCancelled() || !executor.acquire()) {
          return;
        }
        try {
          if (running.compareAndSet(false, true)) {
            runnable.run();
          }
        } finally {
          executor.release();
        }
      } finally {
        executor.remove(this);
        synchronized (finished) {
          finished.countDown();
        }
      }
    }

    @Override
    public String toString() {
      return runnable.toString();
    }
  }

  /** Same as Task class, but with a reference to ProjectRunnable, used to retrieve
   *  the project name from the operation queued
   **/
  public static class ProjectTask<V> extends Task<V> implements ProjectRunnable {

    private final ProjectRunnable runnable;

    ProjectTask(ProjectRunnable runnable, Executor executor, int taskId,
        long startAt) {
      super(runnable, executor, taskId, startAt);
      this.runnable = runnable;
    }

    @Override
    public String getProjectNameKey() {
      return runnable.getProjectNameKey();
    }

    @Override
    public String getRemoteName() {
      return runnable.getRemoteName();
    }

    @Override
    public boolean hasCustomizedPrint() {
      return runnable.hasCustomizedPrint();
    }
  }
}
