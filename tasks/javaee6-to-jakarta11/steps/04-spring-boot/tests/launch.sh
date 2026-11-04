#!/usr/bin/env bash
# Start one instance of the candidate's artifact and wait for it to answer.
#
#   launch.sh <artifact> <site> <http-port> <ssh-port> <tag>
#
# Per-step copy: every step's tests/ holds an identical copy of this file;
# keep them in sync.
#
# Two artifact shapes, both started the way their own platform documents:
#
#   war       deployed at context path "/" in a servlet container. The verifier
#             image ships Jetty's jetty-runner, which is a documented way to
#             run a war and is not part of the candidate's build.
#   bootjar   `java -jar`, with the HTTP port supplied as Spring Boot's own
#             `server.port` property.
#
# Everything else -- the site directory, the ssh listen address, the repository
# root -- comes from the run interface the instructions name, which does not
# change across the ladder: one JVM system property and the project's own
# etc/gerrit.config.
set -u
source /tests/helpers.sh

ART="${1:?artifact}"; SITE="${2:?site}"; HP="${3:?http port}"
SP="${4:?ssh port}"; TAG="${5:-$HP}"
LOG="/score/log/server-$TAG.log"
: "${ARTIFACT_KIND:?}"
: "${LAUNCH_TIMEOUT:=180}"

case "$ARTIFACT_KIND" in
    war)
        CMD=(java -Dgerrit.site_path="$SITE" -Duser.timezone=UTC
             -jar /opt/jetty-runner.jar --port "$HP" --path / "$ART") ;;
    bootjar)
        CMD=(java -Dgerrit.site_path="$SITE" -Duser.timezone=UTC
             -Dserver.port="$HP" -jar "$ART") ;;
    *)  say "launch: unknown ARTIFACT_KIND=$ARTIFACT_KIND"; exit 1 ;;
esac

rm -f "$LOG"
touch "$LOG"
chown "$SCORE_UID:$SCORE_GID" "$LOG"

# Candidate code, so unprivileged for the same reason the build is: a static
# initializer in a dependency the candidate chose runs here.
start_instance() {                          # start_instance <try>
    printf '=== launch %s try %s ===\n' "$TAG" "$1" >> "$LOG"
    run_unprivileged_bg "${CMD[@]}" >> "$LOG" 2>&1
    PID=$!
    echo "$PID" >> /score/out/server.pids
    record launch "$TAG try=$1 pid=$PID http=$HP ssh=$SP kind=$ARTIFACT_KIND"
}

# Phase 1, hard: the container has to be serving HTTP. Any answer at all
# counts, including the 301 the ssl profile gives every request -- readiness is
# "something is listening and speaking HTTP", not "this route returns 200". If
# this never happens the artifact did not deploy and there is nothing to
# measure.
#
# Sets `up` to the second it answered in, or 0. A non-zero return is a verdict
# on this attempt and not yet on the artifact.
wait_for_http() {                           # wait_for_http <try>
    up=0
    for i in $(seq 1 "$LAUNCH_TIMEOUT"); do
        if ! kill -0 "$PID" 2>/dev/null; then
            say "launch $TAG try $1: process exited after ${i}s"
            record launch.fail "$TAG try=$1 exited"
            return 1
        fi
        if curl -s -o /dev/null --max-time 5 "http://127.0.0.1:$HP/ssh_info"; then
            up=$i
            return 0
        fi
        sleep 1
    done
    say "launch $TAG try $1: no HTTP answer in ${LAUNCH_TIMEOUT}s"
    record launch.fail "$TAG try=$1 http-timeout"
    return 1
}

# Give the ports back before asking for them again: a JVM still holding $HP
# while its successor starts turns a slow launch into an "address already in
# use" on the retry, which reads as a defect in the candidate's artifact and is
# not one. The pid stays in server.pids either way -- stop_servers tolerates a
# pid that is already gone, and an instance missing from that file is the one
# that would still be on the port when the next verifier runs.
discard_instance() {
    kill "$PID" 2>/dev/null || true
    for _ in $(seq 1 10); do
        kill -0 "$PID" 2>/dev/null || break
        sleep 1
    done
    kill -9 "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
}

# One relaunch, and only one. This phase is the single point where a flake
# costs everything: run_behaviour.sh turns a launch failure into behaviour 0
# over every recorded observation, not just the ones this instance would have
# served, so one slow start under load outweighs every other timing surface in
# the suite. A second attempt costs at most one more $LAUNCH_TIMEOUT and tells
# "this machine was busy" apart from "this artifact does not deploy", which
# fails twice. Both attempts are journalled and both append to the same log, so
# a run that needed the retry says so instead of reading like a clean start.
start_instance 1
if ! wait_for_http 1; then
    tail -60 "$LOG" >&2 || true
    say "launch $TAG: first attempt did not come up, relaunching once"
    record launch.retry "$TAG"
    discard_instance
    start_instance 2
    if ! wait_for_http 2; then
        say "launch $TAG: no HTTP answer in two attempts of ${LAUNCH_TIMEOUT}s"
        record launch.fail "$TAG gave-up after 2 tries"
        tail -60 "$LOG" >&2 || true
        exit 1
    fi
    say "launch $TAG: second attempt came up after ${up}s"
fi

# Phase 2, soft: wait for the ssh daemon, but do not make it a condition of
# running the suite. A candidate whose HTTP half works and whose sshd never
# binds reproduces the HTTP observations and not the SSH ones, and that
# fraction is a truer measurement of how far the migration got than a flat
# zero. Failing hard here would also throw away the run over exactly the
# defect the suite is best placed to describe.
: "${SSH_GRACE:=60}"
sshup=0
for i in $(seq 1 "$SSH_GRACE"); do
    nc -z 127.0.0.1 "$SP" 2>/dev/null && { sshup=$i; break; }
    kill -0 "$PID" 2>/dev/null || break
    sleep 1
done
if [ "$sshup" -eq 0 ]; then
    say "launch $TAG: WARNING http up after ${up}s but ssh port $SP never bound"
    record launch.nossh "$TAG port=$SP grace=${SSH_GRACE}s"
    tail -30 "$LOG" >&2 || true
else
    record launch.ssh "$TAG ${sshup}s"
fi

say "launch $TAG: up after ${up}s (ssh $( [ "$sshup" -gt 0 ] && echo "${sshup}s" || echo none ))"
record launch.up "$TAG ${up}s"
exit 0
