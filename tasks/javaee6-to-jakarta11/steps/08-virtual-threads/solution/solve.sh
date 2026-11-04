#!/usr/bin/env bash
#
# Reference solution, step 08 -- the threads that run the work become virtual.
#
# Two pools go, and nothing replaces them with another pool:
#
#   * `WorkQueue.Executor` was a `ScheduledThreadPoolExecutor` subclass with a
#     `ThreadFactory` that named and numbered its workers. It becomes a
#     `SimpleAsyncTaskScheduler` with `setVirtualThreads(true)`, which starts
#     one virtual thread per task and keeps no pool at all. The queue's size
#     argument survives as a meaning rather than as a thread count: a
#     `Semaphore` the task acquires on its own thread, so `sshd.threads` and
#     `sshd.batchThreads` still bound how many commands run at once. It is not
#     `setConcurrencyLimit`, which blocks in `submit` -- that would stall the
#     sshd i/o thread that submits the command rather than the command itself;
#   * Tomcat's `http-nio-*-exec-N` pool goes the way Boot documents,
#     `spring.threads.virtual.enabled=true`, and every request is served on a
#     virtual thread instead.
#
# `BaseCommand` was the third place a thread was created. An admin command
# (`gerrit gsql`, `gerrit show-caches`) ran on `new Thread(tt, tt.toString())`,
# outside every queue, so that a command which blocks on the console could not
# occupy a worker. A dedicated unbounded queue says the same thing without a
# thread constructor: `createQueue(0, "SSH-Admin-Worker")`, published under a
# new `@AdminCommandExecutor` qualifier alongside the two that already exist.
#
# What does not change, and is asserted below: the task registry. `getTasks`,
# `getTask(int)`, the six-state `State` enum, `CancelableRunnable` and
# `ProjectTask` are what the `kill` command reads and writes -- and `kill` is
# the only door onto the registry this fixture has: `gerrit`'s subcommands are
# `receive-pack` and `show-connections`, and no `show-queue` is registered at
# all. On an idle server `kill` answers `No such task` whether the registry is
# there or not, so the suite cannot tell; the rows in score.py can, and do.
# `Task` keeps implementing `RunnableScheduledFuture` over the scheduler's own
# future.
#
# The whole tree moves to release 21: a virtual thread is a 21 API and a 21
# runtime, so the class files are major 65 from here on.
#
# Eight files overlaid, two of them new, none deleted. Nothing is generated at
# solve time, so the oracle run and the tree in git are the same bytes;
# `tree.sha256` is what says so out loud.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="${1:-/app}"

[ -d "$app" ] || { echo "solve: no $app" >&2; exit 1; }
[ -f "$app/pom.xml" ] || { echo "solve: $app holds no reactor root" >&2; exit 1; }
[ -s "$here/tree.sha256" ] || { echo "solve: no manifest beside solve.sh" >&2; exit 1; }

# --- what this rung rewrites --------------------------------------------
#
# Driven from the manifest rather than from the directory: Harbor's oracle
# agent uploads each step's `solution/` into one shared directory inside the
# environment, merging and never deleting, so by the time this rung runs
# `$here/tree` is the union of steps 01 through 08 and still holds files later
# rungs retired. The manifest names exactly the eight paths step 08 owns.
mani() { sed 's/^[0-9a-f]\{64\}  //' "$here/tree.sha256"; }
mani | tar -C "$here/tree" --no-recursion -T - -cf - | tar -C "$app" -xf -

# --- assert every edit landed -------------------------------------------
missing=0
while read -r want path; do
  [ -f "$app/$path" ] || { echo "solve: MISSING $path" >&2; missing=$((missing + 1)); continue; }
  got="$(sha256sum "$app/$path" | cut -d' ' -f1)"
  [ "$got" = "$want" ] || { echo "solve: MISMATCH $path" >&2; missing=$((missing + 1)); }
done < <(sed 's/  /\t/' "$here/tree.sha256" | tr '\t' ' ')

# The rung's own postcondition, asserted over the whole merged workspace
# rather than over the eight files above -- which is what a partially applied
# overlay, or a later rung's file left behind, looks like.
src() { find "$app" -path '*/target' -prune -o -name '*.java' -print; }

pools="$(src | xargs grep -lE 'ScheduledThreadPoolExecutor|ThreadPoolExecutor|implements +ThreadFactory|new +ThreadFactory|ThreadFactory *\(' 2>/dev/null | sort || true)"
if [ -n "$pools" ]; then
  echo "solve: STILL BUILDS A THREAD POOL:" >&2
  printf '  %s\n' $pools >&2
  missing=$((missing + $(printf '%s\n' "$pools" | wc -l)))
fi

# `new Thread` survives in exactly one place, and it is not a worker: the
# launcher's shutdown hook, which the runtime requires be an unstarted Thread.
threads="$(src | xargs grep -lE 'new +Thread *\(' 2>/dev/null | sort || true)"
for f in $threads; do
  case "$f" in
    */MiniGitServer.java) ;;
    *) echo "solve: constructs a Thread: $f" >&2; missing=$((missing + 1)) ;;
  esac
done

grep -q '<maven.compiler.release>21</maven.compiler.release>' "$app/pom.xml" || {
  echo "solve: the reactor root does not target release 21" >&2; missing=$((missing + 1)); }

grep -q 'setVirtualThreads' \
  "$app/mini-git-server-server/src/main/java/com/google/gerrit/server/git/WorkQueue.java" || {
  echo "solve: WorkQueue does not ask its scheduler for virtual threads" >&2
  missing=$((missing + 1)); }

grep -q '^spring\.threads\.virtual\.enabled *= *true' \
  "$app/mini-git-server-app/src/main/resources/application.properties" || {
  echo "solve: the packaged properties do not enable virtual threads" >&2
  missing=$((missing + 1)); }

# The registry the ssh commands read and write, by name, so that a failure
# says which part of it a rewrite dropped.
WQ="$app/mini-git-server-server/src/main/java/com/google/gerrit/server/git/WorkQueue.java"
for sig in 'List<Task<?>> getTasks()' 'Task<?> getTask(final int id)' \
           'enum State' 'DONE' 'CANCELLED' 'RUNNING' 'READY' 'SLEEPING' 'OTHER' \
           'class ProjectTask' 'CancelableRunnable'; do
  grep -qF "$sig" "$WQ" || {
    echo "solve: WorkQueue no longer declares $sig" >&2; missing=$((missing + 1)); }
done

# The two knobs still reach the queues that bound the commands.
CQ="$app/mini-git-server-sshd/src/main/java/com/google/gerrit/sshd/CommandExecutorQueueProvider.java"
for k in 'sshd' 'threads' 'batchThreads'; do
  grep -qF "\"$k\"" "$CQ" || {
    echo "solve: CommandExecutorQueueProvider no longer reads $k" >&2
    missing=$((missing + 1)); }
done

# Tripwire, as in the rungs below: nothing here extracts a retired path, so if
# one reappears the extraction went back to copying the whole directory.
back="$(find "$app" -path '*/target' -prune -o -type f \
          \( -name 'UrlModule.java' -o -name 'WebModule.java' \
             -o -name 'web.xml' -o -name '*SshModule.java' \
             -o -name 'WebAppInitializer.java' -o -name 'log4j.properties' \
             -o -name 'ProxyEhcache.java' -o -name 'EvictionPolicy.java' \) \
          -print | sort)"
if [ -n "$back" ]; then
  echo "solve: STALE (retired at or below this rung, back in the tree):" >&2
  printf '  %s\n' $back >&2
  missing=$((missing + $(printf '%s\n' "$back" | wc -l)))
fi

expected="$(wc -l < "$here/tree.sha256")"
echo "solve: $expected files in manifest, $missing did not land"
[ "$missing" -eq 0 ]
