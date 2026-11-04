#!/usr/bin/env bash
# Reward helpers, one copy per step.
#
# The verifier image is built with a step's tests/ directory as the Docker
# build context, so it cannot COPY from the task root: every step carries its
# own copy of this file. The copies are written by authoring/render_package.py
# from authoring/verifier/helpers.sh; edit that one.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# the Debian base the toolchain layer is built on.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root. Call this first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs. Writing the real score last does not help: a
# detached child outlives the scorer and writes after it.
#
# This runs after Harbor's chmod, so this is the mode that sticks.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# Run candidate-authored code as nobody instead of root.
#
# Needed for harden_reward_dir to mean anything: root can write a root-owned
# directory whatever its mode. Use it everywhere candidate code runs -- the
# CMake configure step, which executes the project's own CMake scripts, the
# compiler driving the project's own custom commands, every example the
# behaviour suite starts, and the characterisation driver, which is linked
# against the candidate's library and runs its static initialisers.
# ISEFIX_GRAPH_DIR is unset for the same reason it is mode 700: nothing the
# candidate runs has any business with the directory root stamps the build
# graph into, including its name. ISEFIX_TREE_SNAPSHOT and ISEFIX_TRACE_FILE
# name root-only directories on the same terms.
#
# ISEFIX_TRACE_PATH and ISEFIX_TRACE_GUARD are stripped here too, and nothing
# exports them any more. The trace switch is the descriptor itself: fd 3 is a
# regular file on the configure and /dev/null everywhere else, which is a
# question build_candidate.sh can ask of its own file table. A variable set for
# the traced configure and not for the regenerate CMake that ninja runs during
# the compile phase would be a variable that tells a listfile which of the two
# it is in -- the two-configure hole one level down, and traceguard.c's
# unsetenv only cleans the process it is preloaded into. What is not in the
# environment at all cannot be read out of it.
run_unprivileged() {
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env -u ISEFIX_GRAPH_DIR -u ISEFIX_TREE_SNAPSHOT -u ISEFIX_TRACE_FILE \
            -u ISEFIX_TRACE_PATH -u ISEFIX_TRACE_GUARD \
            -u ISEFIX_LAUNCH_PREFIX \
            HOME=/tmp TMPDIR=/tmp \
            PATH="$PATH" \
            CMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE:-}" \
            LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}" \
            PYTHONDONTWRITEBYTECODE=1 \
            PYTHONPYCACHEPREFIX=/tmp/pycache \
            "$@"
}

# The same privilege drop as a single string, for handing to a Python
# subprocess as a launch prefix. Keep the two in step -- literally: this used
# to stop at --clear-groups, so everything launched through it kept the
# variables run_unprivileged strips, ISEFIX_GRAPH_DIR included. The rest of
# what the function sets is deliberately NOT here: HOME, TMPDIR and PATH are
# the environment a *build* needs, and the things launched through this string
# are the candidate's own programs under the behaviour suite, whose
# environment is engine.py's to decide. What both spellings must agree on is
# which directories are root's alone.
#
# ISEFIX_LAUNCH_PREFIX is stripped only here, and it is not a directory: it is
# how grade.py tells engine.py to drop privilege, and the programs engine.py
# starts through it are the candidate's own. Left in place it would be the one
# thing a candidate binary could read to know it is being graded rather than
# run -- and a library that behaves as the suite expects only when the suite
# is watching passes the behaviour half without doing any of it. grade.py's
# own environment still has it; the candidate's processes do not.
UNPRIVILEGED_PREFIX="setpriv --reuid ${SCORE_UID} --regid ${SCORE_GID} \
--clear-groups env -u ISEFIX_GRAPH_DIR -u ISEFIX_TREE_SNAPSHOT \
-u ISEFIX_TRACE_FILE -u ISEFIX_TRACE_PATH -u ISEFIX_TRACE_GUARD \
-u ISEFIX_LAUNCH_PREFIX"

# Kill anything the build left running as the unprivileged user.
#
# A POST_BUILD custom command runs as nobody and can detach a process that
# outlives `cmake --build`. Such a process still has nobody's write access
# to every scratch directory the scorer uses next, so it can rewrite a
# re-derived compile database or a trace file between the moment the
# scorer produces it and the moment the scorer reads it. Nothing
# legitimate is running as nobody at this point: the build has exited and
# the behaviour suite has not started.
#
# Until none is left, not once: one pkill is a snapshot, and a process that
# forks between the listing and the kill is not in it. Zombies are not counted
# -- this container's PID 1 does not reap, so a dead process can stay listed
# without being able to do anything. Bounded, so a fork loop costs seconds and
# not the step.
reap_unprivileged() {
    local i
    for i in $(seq 1 50); do
        pkill -KILL -u "$SCORE_UID" 2>/dev/null || true
        sleep 0.1
        ps -u "$SCORE_UID" -o stat= 2>/dev/null | grep -qv '^Z' || return 0
    done
}

# Take /app back from the unprivileged user once the last build phase is over.
#
# Everything after this reads the tree and nothing writes it: the gates read
# the sources, stage copies the artefacts out as root, and the programs under
# the behaviour suite run from root's bindir in directories of their own. A
# tree its user still owns is a tree any of those programs can edit between
# the digest comparison and the gate that reads the file (reward-hacking audit
# 2026-10-12, theme T3). chown -h and chmod -R do not follow symlinks.
seal_app() {
    chown -R -h 0:0 /app 2>/dev/null || true
    chmod -R go-w /app 2>/dev/null || true
}

# The recorded expectations are read by grade.py, as root, and by nothing the
# candidate runs. The image ships them world-readable (COPY), and a program
# under the behaviour suite runs in the same container, so they are closed
# here, before any candidate code starts (reward-hacking audit 2026-10-12, F2).
lock_expectations() {
    chown -R 0:0 /tests/fixtures 2>/dev/null || true
    chmod -R go-rwx /tests/fixtures 2>/dev/null || true
}

# Stamp the build graph, as root, between the configure and the build.
#
#   stamp_build_graph <build-dir> <log>
#
# The compile database says what the compiler was told, and every copy of it
# the candidate can reach is a copy the candidate can write. The one under the
# build directory is theirs outright. `ninja -t compdb`, re-derived after the
# build, reads build.ninja, which they can rewrite -- and the check that used
# to stand in the way of that, `ninja -n` reporting an up-to-date build, is a
# comparison against hashes in .ninja_log that the same rewrite can recompute:
# MurmurHash64A, fixed seed, twenty lines of Python. Measured on a tree whose
# every translation unit was compiled with -w: up to date, database clean, 14
# of 14 gates, reward 1.000000.
#
# Here, there is nothing to rewrite afterwards. The configure has exited and
# been reaped, the build has not started, and ninja loads its graph at startup
# -- so what this copies out is what the compiler is about to be driven from.
# build.ninja's sha256 goes with it, because ninja's own regenerate edge can
# reload the graph mid-build, and that edge writes the file it is the output
# of: a hash that has moved by the time the gates run is a second configure
# this never saw. derive_db reads both.
#
# mktemp for the name and 700 for the mode: the candidate's user can neither
# guess the directory nor enter it, and /tmp's sticky bit stops them
# unlinking a root-owned entry to put their own in its place.
stamp_build_graph() {
    local build=$1 log=$2
    # A failed mktemp must not arrive at the scorer as "no stamp was meant to
    # exist". That was the one path where an absent stamp used to buy the
    # weaker post-build derivation, so the export happens either way and names
    # a directory that cannot be there: derive_db then refuses the database
    # instead of deriving one from the finished build.
    GRAPH_DIR=$(mktemp -d /tmp/isegraph.XXXXXXXXXXXX) \
        || GRAPH_DIR=/nonexistent/isegraph-unavailable
    chmod 700 "$GRAPH_DIR" 2>/dev/null || true
    export ISEFIX_GRAPH_DIR="$GRAPH_DIR"
    {
        printf '=== build graph stamped %s\n' "$(date -u +%FT%TZ)"
        # A regular file and not a symlink, and not implausibly large, before
        # root reads it (hang-path sweep, 2026-10-13). $build is the candidate's
        # tree; a build.ninja that is a symlink to a huge file or to a FIFO, or
        # a multi-gigabyte regular file, made the sha256sum and wc below read
        # unboundedly as root. No real build graph is a symlink or over a few
        # megabytes; a tree that ships one has nothing here to stamp. The read
        # itself is also bounded.
        nj="$build/build.ninja"
        nj_sz=$(stat -c %s "$nj" 2>/dev/null || echo -1)
        if [ ! -f "$nj" ] || [ -L "$nj" ]; then
            printf 'no %s/build.ninja (or not a regular file): nothing to stamp\n' "$build"
        elif [ "$nj_sz" -lt 0 ] || [ "$nj_sz" -gt 268435456 ]; then
            rm -f "$GRAPH_DIR/compdb.json"
            printf 'build.ninja is %s bytes (not a plausible build graph); not stamped\n' "$nj_sz"
        elif timeout --signal=KILL 120 \
                ninja -C "$build" -t compdb > "$GRAPH_DIR/compdb.json"; then
            timeout --signal=KILL 60 sha256sum "$nj" | awk '{print $1}' \
                > "$GRAPH_DIR/build.ninja.sha256"
            printf 'build.ninja %s bytes, sha256 %s\n' \
                "$nj_sz" \
                "$(cat "$GRAPH_DIR/build.ninja.sha256")"
            printf 'compdb %s bytes\n' "$(wc -c < "$GRAPH_DIR/compdb.json")"
        else
            # A partial or empty database is worse than none: derive_db must
            # be able to tell "nothing was stamped" from "this is the stamp".
            rm -f "$GRAPH_DIR/compdb.json"
            printf 'ninja -t compdb failed in %s\n' "$build"
        fi
    } > "$log" 2>&1
}

# Open the trace sink, as root, before the configure that writes it.
#
#   open_trace_sink <log>
#
# What CMake executed is read from CMake's own --trace-redirect stream, and a
# record the project appends to that stream is byte for byte a record CMake
# could have written -- which is what every `require` rule in a traced gate
# counts. So the stream has to be somewhere the project cannot reach, and
# "somewhere" has to survive the project running as the same user as the
# CMake that writes it.
#
# The sink is a regular file that is root's and mode 600, inside a directory
# that is root's and mode 700. Root opens it here and test.sh passes the
# descriptor to the configure as fd 3; the configure is told
# `--trace-redirect=/dev/fd/3`, and traceguard.so -- preloaded into that one
# CMake -- answers that open with a close-on-exec copy of fd 3 rather than
# letting it open anything, then closes fd 3 behind it.
#
# The ownership is the enforcement and the shim is what makes the ownership
# possible. /dev/fd/3 and /proc/<pid>/fd/3 are fresh opens of the file they
# name, checked against that file's own mode and not against the directory
# it sits in -- so a sink the configure can open for itself is a sink every
# other process of that user can open too, through the descriptor tables of
# the unprivileged `timeout` and `bash` between root and CMake. Measured in
# this image, with the sink owned by root: writing down the inherited
# descriptor works, and opening it by name, through /proc/<holder>/fd/3 and
# through /proc/self/fd/3 are all EACCES -- including file(APPEND) from
# inside a listfile, which becomes a fatal CMake error naming the path.
# Owned by the candidate instead, that same /proc route writes into the
# trace. See traceguard.c for the rest of the table.
#
# The earlier shape was a fifo in a 0711 directory, drained by the grader and
# chowned back to root on the first read. It was forgeable twice over: the
# path was on cmake's command line, which /proc/<pid>/cmdline hands to any
# child that asks, and the chown raced a writer that retried every 20ms.
# Measured, on the package as it stood: eleven forged records in the trace and
# a gate flipped from fail to pass.
#
# A failed mktemp must not arrive at the grader as "no trace was meant", for
# the reason stamp_build_graph gives: ISEFIX_TRACE_FILE is exported either way
# and names a directory that cannot be there, so the grader refuses the trace
# rather than scoring the traced gates off nothing. The other half is the
# descriptor: test.sh points fd 3 at this file only when it is really here,
# and at /dev/null otherwise, so a configure is never asked to redirect into a
# descriptor root never opened -- and build_candidate.sh decides whether to ask
# for a trace by looking at fd 3 rather than at a variable. Nothing about the
# sink is exported to the processes that run candidate code; see
# run_unprivileged for why that matters.
open_trace_sink() {
    local log=$1 dir
    dir=$(mktemp -d /tmp/isetrace.XXXXXXXXXXXX) \
        || dir=/nonexistent/isetrace-unavailable
    chmod 700 "$dir" 2>/dev/null || true
    export ISEFIX_TRACE_FILE="$dir/trace.json"
    {
        printf '=== trace sink %s\n' "$(date -u +%FT%TZ)"
        # No guard, no trace. traceguard.so is what turns CMake's open of
        # /dev/fd/3 into a copy of the descriptor; without it that open is an
        # ordinary open of a file this user may not open, and the configure
        # dies having written nothing. It is also what closes fd 3 behind the
        # copy, so that nothing the configure starts inherits the sink. build_verifier_image.sh asserts the image carries it,
        # so this is the case where that assertion has already failed -- the
        # sink is left uncreated and the grader refuses the traced gates,
        # which is loud where a quietly forgeable trace would not be.
        if [ ! -e /opt/ise/traceguard.so ]; then
            printf 'no /opt/ise/traceguard.so: the trace will be refused\n'
        elif : > "$ISEFIX_TRACE_FILE" 2>/dev/null; then
            # Root's own file, and deliberately not chowned to the user that
            # writes it: the configure never opens this file, it is handed a
            # descriptor onto it. The mode is what refuses every re-open the
            # candidate's user can reach, and the directory's is what stops
            # the path resolving for anything that has not already got the
            # descriptor.
            chmod 600 "$ISEFIX_TRACE_FILE"
            printf 'sink %s, guard %s\n' "$ISEFIX_TRACE_FILE" \
                /opt/ise/traceguard.so
            ls -ldn "$dir" "$ISEFIX_TRACE_FILE"
        else
            printf 'could not create %s: the trace will be refused\n' \
                "$ISEFIX_TRACE_FILE"
        fi
    } > "$log" 2>&1
}

# Digest the source tree, as root, after the stale-output sweep and before the
# configure.
#
#   snapshot_tree <src> <log>
#
# The text gates read the tree at grading time; the compiler read it during
# the build. Everything between the two is candidate code running as the
# candidate's user over a tree it owns -- the project's own CMake during the
# configure, its custom commands during the build -- so a tree that rewrites
# itself in between shows the scorer one set of sources and the compiler
# another. The digest is taken here, where nothing of the candidate's is
# running, and compared in run_gates; a difference fails every gate, the same
# answer and for the same reason as a suppression.
#
# After the sweep, not before: build_candidate.sh's clean phase deletes stale
# CMakeCache.txt files, and CMakeCache.txt is a .txt, so a snapshot taken
# first would report the verifier's own housekeeping as a rewrite.
snapshot_tree() {
    local src=$1 log=$2 dir
    dir=$(mktemp -d /tmp/isesnap.XXXXXXXXXXXX) \
        || dir=/nonexistent/isesnap-unavailable
    chmod 700 "$dir" 2>/dev/null || true
    export ISEFIX_TREE_SNAPSHOT="$dir/tree.json"
    # Bounded: root opens every file walk() lists, and a FIFO or a device
    # under one of those names would otherwise block this read for good and
    # leave the step without a reward.json. A snapshot cut short is a
    # snapshot that is not there, which run_gates refuses.
    PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=/tmp/pycache \
        timeout --signal=KILL 300 \
        python3 -s -E -B /tests/snapshot_tree.py "$src" "$ISEFIX_TREE_SNAPSHOT" \
        > "$log" 2>&1 || rm -f "$ISEFIX_TREE_SNAPSHOT"
}

# A scratch directory only root may write, created before any candidate code
# runs. Whatever is at the path first is removed, symlink included: /tmp is
# sticky so nothing unprivileged can unlink a root-owned entry afterwards, but
# an entry that is already there when this starts was put there by something
# else, and `mkdir -p` over a symlink silently accepts wherever it points.
root_scratch() {
    local path=$1
    rm -rf -- "$path" 2>/dev/null || true
    mkdir -p "$path" || return 1
    chown root:root "$path" 2>/dev/null || true
    chmod 755 "$path"
}

# Give /app and the scratch directories to the users that must write them.
#
# /app arrives owned by whoever wrote the artifact. Without this the build
# cannot create its output directory, so every candidate scores 0 and the step
# looks merely very hard instead of broken.
#
# The bindirs are root's. grade.py says so -- "root-owned and not writable by
# the user they run as: isedrv lives here, and it is the one binary in the
# directory that the verifier owns and the candidate does not" -- and this
# function used to chown them to that very user, which made the sentence
# false. Measured in the verifier image: as 65534, `rm /tmp/isebin/isedrv`
# succeeds and a four-byte file takes its place, because 0755 on a directory
# owned by 65534 is write permission for 65534. Everything that writes a
# bindir is root (isefix.stage from grade.py, the driver copy, engine's
# per-session log directory), so nothing is lost by saying so here; what is
# gained is that a program the candidate wrote, started by an early session of
# the behaviour suite, cannot leave a replacement driver behind for the later
# sessions that check the API is frozen. See authoring/check_scratch.py.
#
# /tmp/isework is the exception and stays theirs: the characterisation driver
# is compiled by an unprivileged clang that writes its objects there.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
    chmod 1777 /tmp
    root_scratch /tmp/isebin
    root_scratch /tmp/isesanbin
    rm -rf -- /tmp/isework 2>/dev/null || true
    mkdir -p /tmp/isework
    chown "$SCORE_UID:$SCORE_GID" /tmp/isework 2>/dev/null || true
    chmod 700 /tmp/isework
}

# report <score>
#
#   reward  this step's local score in [0, 1]. Harbor's
#           multi_step_reward_strategy = "mean" averages this key across every
#           step that produced a verifier_result.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips
# remaining steps even without min_reward.
report() {
    local reward
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    # Reopen to 0755 so a local docker run, which reads this from the host as
    # an ordinary user, can collect it. Still root-owned, so the candidate
    # still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}

# Refuse to grade an /app that never arrived.
#
# A verifier container whose artifact upload failed sees an empty /app, builds
# nothing, and scores 0 -- which is indistinguishable from a candidate that did
# nothing. The two are not the same finding, so the difference is written down
# where a later reader will see it.
# Counted rather than named: which files the tree holds is the agent's
# business by step 9, when every project header becomes a module interface
# unit, so no single path can stand in for "the artifact arrived". The pinned
# tree has 113 files and a build adds to it; an /app with fewer than 40 did not
# come from an agent container.
require_app() {
    local count
    count=$(find /app -type f 2>/dev/null | head -100 | wc -l)
    if [ "$count" -lt 40 ]; then
        {
            printf 'VERIFIER-PROBLEM: /app holds %s files.\n' "$count"
            printf 'This container did not receive the collected artifact.\n'
            ls -la /app 2>&1 | head -40
        } > /logs/verifier/app-missing.log
        report 0
    fi
}
