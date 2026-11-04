#!/bin/bash
# Step 08's runtime probe: settle the audit log, then put work in flight and
# ask the running server what kind of thread is doing it.
#
# run_behaviour.sh calls this once, after drive.py has finished all 428
# observations and while every server instance is still up. It is the only
# place in the verifier that can see a running process: score.py runs after
# the EXIT trap, and that trap sends SIGTERM, waits three seconds, and then
# sends SIGKILL.
#
# Two sections, and the first one is step 06's unchanged.
#
# Section 1, the audit log. The gate it serves -- "the ssh audit log is still
# written" -- reads a file a logging backend is free to buffer. Logback's
# AsyncAppender drains its queue in stop() under a bounded maxFlushTime, and
# three seconds is also exactly the harness's SIGKILL deadline, so a candidate
# must not be charged for losing a race with a teardown it does not control.
# This waits for the file to stop moving while the process is still alive and
# unhurried. score.py reads /score/site-*/logs/sshd_log itself and decides.
#
# Section 2, the threads. This rung's claim is that the work runs on virtual
# threads and no longer on pools of platform ones, and that claim exists only
# in a live process: a thread is not in the source, not in the class files and
# not in the deployable. Nor can it be read off an idle server -- an idle
# server has no thread doing anything -- so the probe creates the work first:
#
#   * four ssh `git-upload-pack` sessions against four different repositories,
#     each holding its stdin open and never sending the `want` lines the
#     server is waiting for, so each one occupies a command for as long as the
#     probe wants it to. Four against this fixture's `sshd.threads = 4` and
#     `sshd.batchThreads = 2`, which is two interactive permits: two commands
#     run and two wait, which is what makes the concurrency bound measurable
#     as well as the thread kind;
#   * two `POST /p/demo.git/git-upload-pack` requests that promise 4096 bytes
#     of body and send four, so each one occupies a request handler inside the
#     servlet the same way.
#
# Then `Thread.dump_to_file -format=json` and `Thread.print`, and the pair goes
# to score.py, which classifies. Two dumps because neither alone answers the
# question: the JSON lists every thread including virtual ones, Thread.print
# lists exactly the platform ones, and on JDK 21 the difference between them is
# the only way to tell which is which.
#
# What this probe does NOT do: judge. It asserts nothing, it fails nothing,
# and its exit status is about its own mechanics, not about what it found --
# "every thread was a platform thread" is a perfectly successful probe run and
# a perfectly failed gate. Keeping the judgement in one file keeps the two from
# drifting: there is one definition of "ran the command", and it is in
# score.py, which reads the frames rather than the names. Names are not
# evidence here and the reason is measured: JGit's InterruptTimer names a
# platform helper after the command it is timing, and BaseCommand$TaskThunk
# renames whatever thread it is on for the duration of a command, so on the
# tree this rung starts from there is no thread named for a worker pool to
# find even though every command is running on one.
#
# Output is tab-separated key/value lines on stdout, which the caller
# redirects to /score/log/probe.log. Parsed by score.py; readable by a person.
source /tests/helpers.sh

PROFILES="main alt ssl"
LOG_NAME="sshd_log"

# Stability window for section 1. Three consecutive identical sizes, 400 ms
# apart, is the settle condition; 50 polls is the ceiling, so a file that never
# stops growing costs 20 s rather than the whole verifier timeout.
STABLE_READS=3
POLL_SECONDS=0.4
MAX_POLLS=50

# Section 2. The load runs against the main profile, which is the one whose
# gerrit.config sizes the queues, and every client is capped so that nothing
# this probe starts can outlive it by more than HOLD_SECONDS.
SSH_PORT=29418
HTTP_PORT=8080
REPOS="demo big many unicode"
POSTS=2
HOLD_SECONDS=45
READY_MIN=2
READY_POLLS=60
READY_SLEEP=0.5
SETTLE_SECONDS=3
LOADDIR=/score/out/probe-load

size_of() {
    # stat, not `wc -c`, so that a missing file is distinguishable from an
    # empty one: an absent file reports -1 and an empty one reports 0.
    if [ -f "$1" ]; then
        stat -c %s "$1" 2>/dev/null || echo -1
    else
        echo -1
    fi
}

printf 'probe.version\t2\n'
printf 'probe.started\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# ---------------------------------------------------------------------------
# Section 1: the audit log, settled
# ---------------------------------------------------------------------------

for p in $PROFILES; do
    site="/score/site-$p"
    log="$site/logs/$LOG_NAME"

    prev=""
    same=0
    polls=0
    while [ "$polls" -lt "$MAX_POLLS" ]; do
        cur="$(size_of "$log")"
        if [ "$cur" = "$prev" ]; then
            same=$((same + 1))
            [ "$same" -ge "$((STABLE_READS - 1))" ] && break
        else
            same=0
        fi
        prev="$cur"
        polls=$((polls + 1))
        sleep "$POLL_SECONDS"
    done

    bytes="$(size_of "$log")"
    if [ "$bytes" -lt 0 ] 2>/dev/null; then
        state="missing"
        lines=0
        bytes=0
    elif [ "$polls" -ge "$MAX_POLLS" ]; then
        state="growing"
        lines=$(wc -l < "$log" 2>/dev/null || echo 0)
    else
        state="settled"
        lines=$(wc -l < "$log" 2>/dev/null || echo 0)
    fi

    printf 'probe.site\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$p" "$log" "$lines" "$bytes" "$state" "$polls"
done

# One line of context per site, so that a failure report says what the file
# looked like and not only how big it was. Truncated hard: this goes into
# gate evidence, and evidence that can grow without bound is a report that
# cannot be read.
for p in $PROFILES; do
    log="/score/site-$p/logs/$LOG_NAME"
    if [ -s "$log" ]; then
        printf 'probe.first\t%s\t%s\n' "$p" "$(head -1 "$log" | cut -c1-200)"
        printf 'probe.last\t%s\t%s\n' "$p" "$(tail -1 "$log" | cut -c1-200)"
    fi
done

# What else is in each logs directory. A candidate who points the appender at
# `sshd_log.0` or `audit.log` fails the gate in score.py; this is what tells
# the author's eye which of the two mistakes it was.
for p in $PROFILES; do
    d="/score/site-$p/logs"
    if [ -d "$d" ]; then
        printf 'probe.logsdir\t%s\t%s\n' "$p" \
            "$(ls -1 "$d" 2>/dev/null | head -20 | tr '\n' ' ')"
    else
        printf 'probe.logsdir\t%s\t(no logs directory)\n' "$p"
    fi
done

# ---------------------------------------------------------------------------
# Section 2: work in flight, and the threads doing it
# ---------------------------------------------------------------------------

# The pid of the main instance, from the journal launch.sh wrote rather than
# from a pattern match over the process table: `pgrep -f mini-git-server`
# matches the shell that started it as readily as the JVM, and attaching to
# the wrong one fails in a way that reads like a JVM that refused the dump.
# The last matching line wins, because launch.sh relaunches once on a slow
# start and the second pid is the live one.
PID="$(awk -F'\t' '$1 == "launch" && $2 ~ /^main /' /score/out/journal.tsv \
        2>/dev/null | sed -n 's/.*pid=\([0-9][0-9]*\).*/\1/p' | tail -1)"
if [ -z "${PID:-}" ] || ! kill -0 "$PID" 2>/dev/null; then
    printf 'probe.pid\t%s\talive=0\n' "${PID:-none}"
    printf 'probe.done\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    exit 0
fi
printf 'probe.pid\t%s\talive=1\n' "$PID"

# Root's, under root's /score/out: everything written here is opened by this
# shell, and a directory the server's user could write would let it plant a
# link for those redirections to follow.
rm -rf "$LOADDIR"
mkdir -p "$LOADDIR"
: > "$LOADDIR/pids"

# Held open with `sleep` on the far end of a pipe rather than with a here-doc:
# the server's own timeout is what ends an upload-pack whose client has gone
# quiet, and a client that has closed its stdin is a client that has gone away
# rather than one that is thinking. `UNPRIVILEGED_EXEC=exec` is helpers.sh's
# own idiom -- it makes the backgrounded shell *become* setpriv, so the pid
# recorded here is the ssh that has to be killed and not a subshell wrapping
# it. Unprivileged because /tests/keys belongs to that user and because it is
# how drive.py reached the same daemon a moment ago.
hold_ssh() {                            # hold_ssh <repo>
    sleep "$HOLD_SECONDS" | {
        UNPRIVILEGED_EXEC=exec run_unprivileged \
            timeout "$HOLD_SECONDS" ssh \
            -o KexAlgorithms=+diffie-hellman-group14-sha1 \
            -o HostKeyAlgorithms=+ssh-rsa \
            -o PubkeyAcceptedAlgorithms=+ssh-rsa \
            -o Ciphers=+aes128-cbc \
            -o MACs=+hmac-sha1 \
            -o StrictHostKeyChecking=no \
            -o UserKnownHostsFile=/dev/null \
            -o GlobalKnownHostsFile=/dev/null \
            -o IdentitiesOnly=yes \
            -o BatchMode=yes \
            -o LogLevel=ERROR \
            -o ConnectTimeout=20 \
            -i /tests/keys/tester -p "$SSH_PORT" tester@127.0.0.1 \
            -- "git-upload-pack '/$1.git'"
    } > "$LOADDIR/up-$1.out" 2>&1 &
    echo "$!" >> "$LOADDIR/pids"
}

# A request that has begun and will not end: the headers promise 4096 bytes
# and four arrive, so the servlet is inside the request while the dump is
# taken. `0000` rather than nothing, so that a server which reads before it
# blocks has something to read.
hold_post() {                           # hold_post <n>
    {
        printf 'POST /p/demo.git/git-upload-pack HTTP/1.1\r\n'
        printf 'Host: 127.0.0.1:%s\r\n' "$HTTP_PORT"
        printf 'Content-Type: application/x-git-upload-pack-request\r\n'
        printf 'Content-Length: 4096\r\n'
        printf 'Connection: close\r\n\r\n0000'
        sleep "$HOLD_SECONDS"
    } | {
        UNPRIVILEGED_EXEC=exec run_unprivileged \
            timeout "$HOLD_SECONDS" nc 127.0.0.1 "$HTTP_PORT"
    } > "$LOADDIR/post-$1.out" 2>&1 &
    echo "$!" >> "$LOADDIR/pids"
}

for r in $REPOS; do hold_ssh "$r"; done
for i in $(seq 1 "$POSTS"); do hold_post "$i"; done

# Wait for the load to be real rather than merely started. A command that has
# reached the point of advertising refs has been given a permit and is running;
# one that is still waiting for a permit has written nothing. READY_MIN is two
# because two is what this fixture's configuration allows to run at once, so
# "two have written" is the condition that the queue is full and the rest are
# queued -- which is the state the concurrency row is about.
advertised=0
polls=0
while [ "$polls" -lt "$READY_POLLS" ]; do
    advertised=0
    for r in $REPOS; do
        [ -s "$LOADDIR/up-$r.out" ] && advertised=$((advertised + 1))
    done
    [ "$advertised" -ge "$READY_MIN" ] && break
    polls=$((polls + 1))
    sleep "$READY_SLEEP"
done

# Settle before dumping. The point is to catch the threads that are *running*
# the work, and for a few milliseconds after a channel opens the daemon's own
# i/o thread is still inside the frames that create a command. Three seconds
# is far longer than that and far shorter than HOLD_SECONDS.
sleep "$SETTLE_SECONDS"

printf 'probe.load\tssh=%s\thttp=%s\tadvertised=%s\tpolls=%s\n' \
    "$(printf '%s\n' $REPOS | wc -l)" "$POSTS" "$advertised" "$polls"

# The dump itself, through the same helper score.py reads it back with, so
# that the file format and the reader cannot drift apart. It runs jcmd as the
# unprivileged user the server runs as, which HotSpot's attach handshake
# requires.
python3 -I - "$PID" <<'PY'
import sys
sys.path.insert(0, "/tests")
import gate_lib as G

pid = sys.argv[1]
ok = G.take_thread_dump(pid, "/score/out/threads.json", "/score/out/threads.txt")
recs = G.threads_in("/score/out/threads.json", "/score/out/threads.txt")
virt = [r for r in recs if r["virtual"]]
print("probe.dump\ttaken=%s\tthreads=%d" % (1 if ok else 0, len(recs)))
print("probe.threads\ttotal=%d\tvirtual=%d\tplatform=%d"
      % (len(recs), len(virt), len(recs) - len(virt)))
# A reader's view of what was in flight, by the same frames score.py judges on
# and with no judgement attached: names first, frames for the few that matter.
CMD = (("com.google.gerrit.sshd.",), ("org.eclipse.jgit.transport.",))
REQ = (("jakarta.servlet.http.HttpServlet.service",
        "org.apache.catalina.core.ApplicationFilterChain."),
       ("com.google.gerrit.httpd.",))
for what, groups in (("command", CMD), ("request", REQ)):
    for r in recs:
        if G.on_stack(r, groups):
            print("probe.inflight\t%s\t%s\t%s\t%s"
                  % (what, "virtual" if r["virtual"] else "platform",
                     r["tid"], (r["name"] or "(unnamed)")[:60]))
PY

# Give the load back. Every client is capped by `timeout` as well, so this is
# tidiness rather than the only way they end -- but a probe that leaves four
# ssh sessions occupying commands changes what the next thing to look at the
# server sees, and there is no reason to make anyone reason about that.
while read -r p; do
    [ -n "$p" ] || continue
    kill "$p" 2>/dev/null || true
done < "$LOADDIR/pids"

printf 'probe.done\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
exit 0
