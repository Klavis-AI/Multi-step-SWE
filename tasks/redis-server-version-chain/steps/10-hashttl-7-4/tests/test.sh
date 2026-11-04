#!/usr/bin/env bash
# Runs in this step's verifier container on a fresh copy of /app.
set -u
# Harbor starts this in the verifier workdir, /app -- the candidate's tree.
# Nothing here may run with it as the cwd (wave-A 2026-10-12).
cd /
source /tests/helpers.sh

# --- why this file is one function -------------------------------------------
#
# bash reads a script INCREMENTALLY as it executes: it parses as far as it needs
# to run the next command, then comes back to the file for more. So a candidate
# that can write this file -- and it can, it is root in this container and
# /tests is just a directory -- can rewrite the part that has not been read yet
# and change what runs after the score is decided.
#
# Every defence in this file lives in that tail. The nonce check, the sweep, the
# replace-don't-keep, the final write: all of it is below the line the shell has
# reached while run.py is running. Rewriting the tail does not forge a score, it
# deletes the code that would have replaced one -- and what stands is whatever a
# detached writer last put in reward.json.
#
# Wrapping the body in a function closes it: bash must parse to the closing
# brace before it can run `_verify` at all, so the whole of it is in memory
# before the candidate's first instruction executes. Changing the file after
# that changes nothing about this run. (FLEET-ADVISORY-21; the payload smb
# found and nobody else had.)
_verify() {
    # A provisional score, written before anything else runs.
    #
    # Harbor aborts the remaining steps of a trial when a step produces no reward
    # file at all -- and a verifier that is killed rather than failing (the kernel
    # OOM-killing the pod while sixteen servers are up, say) produces none. On an
    # eleven-step chain that turns one kill at rung 3 into eight forfeited rungs.
    # With this line the worst case is a step that scores 0 and a trial that
    # continues, which is the difference between a result and no result.
    mkdir -p /logs/verifier
    # Root-owned and unwritable for the candidate before any candidate code runs
    # (wave-A 2026-10-12). Harbor makes this directory 0777; run.py now runs
    # every candidate process as an unprivileged uid, so a root 0755 directory
    # is a fence the kernel enforces rather than an ordering.
    harden_reward_dir
    printf '{"reward": 0.0}\n' > /logs/verifier/reward.json

    # ...and, beside it, who wrote that number.
    #
    # A 0.000000 that means "the verifier died" and a 0.000000 that means "the
    # candidate passed nothing" are the same number and completely different facts.
    # The marker is a separate file, never a key in reward.json: Harbor types that
    # file as dict[str, float | int] and hands the whole of it to pydantic, so a
    # string value there fails validation outright and a numeric one is averaged
    # across steps as if it were a second reward (harbor/verifier/verifier.py
    # _parse_reward_json, harbor/trial/multi_step.py). Verified both, in-process.
    #
    # The marker is a NONCE this shell invents, not the constant "run.py".
    #
    # matrix's finding: `/logs/verifier` is writable by everything in this
    # container, and the candidate's Makefile runs after the provisional zero is
    # written. A candidate that writes `{"reward":1.0}` and a matching marker
    # during its build, and a verifier that then crashes, leaves the candidate's
    # score standing -- the guard that exists for a crashing verifier turns the
    # crash into the candidate's reward. Existence is not provenance: the question
    # "is the marker there" meant "did I put it there", and those differ exactly
    # when someone else can write.
    #
    # run.py echoes this value back only if it reached the end. The candidate never
    # sees it: it is a HARBOR_* variable, and clean_env() strips every one of those
    # from the build and from every server this verifier starts.
    HARBOR_SCORE_NONCE="$(head -c 18 /dev/urandom | od -An -tx1 | tr -d ' \n')"
    export HARBOR_SCORE_NONCE
    echo none > /logs/verifier/scored_by

    on_exit() {
        if [ "$(cat /logs/verifier/scored_by 2>/dev/null)" != "$HARBOR_SCORE_NONCE" ]; then
            echo "==============================================================="
            echo "VERIFIER EXITED WITHOUT A SCORE -- this is a harness failure,"
            echo "not a measurement. The 0.000000 in reward.json is about this"
            echo "verifier, not about the candidate. Look at the log above for"
            echo "the kill, the exception or the timeout that ended the run."
            echo "==============================================================="
        fi
        # Reopen the directory for the host-side archiver on every path that
        # never reaches report() -- the provisional 0.0, an early shell exit.
        # Harbor moves trial/verifier/* out of this bind mount as the trial's
        # user, and a root-owned 0755 directory refuses the rename with EACCES;
        # the step then ends with no reward and the trial aborts. report()
        # reopens it on the paths that do reach it.
        chmod 0777 /logs/verifier 2>/dev/null || true
        chmod 0644 /logs/verifier/reward.json /logs/verifier/scored_by 2>/dev/null || true
    }
    trap on_exit EXIT

    # Everything this step scores is computed by /tests/run.py. The score comes back
    # on STDOUT, carrying the nonce, and this shell writes the file -- last.
    #
    # Why not let run.py write it: the candidate is a SERVER, running as root in
    # this container, and /logs/verifier is writable by it. A detached child that
    # loops `echo '{"reward": 1.0}' > /logs/verifier/reward.json` outlives the
    # scorer and writes after it. smb demonstrated exactly that against a real
    # verifier and got 1.0 for a service implementing nothing. "The harness writes
    # afterwards" is not a defence, it is an ordering, and orderings lose races.
    #
    # A shell variable in this entrypoint is the one place a root candidate cannot
    # reach: not a file, not an environment variable it is given, not a descriptor
    # it can open.
    # -I: no user site, no PYTHON* variables, no script dir on sys.path (run.py
    # puts /tests there itself). The outer bound is the backstop for run.py's
    # own 3150 s budget: Harbor DISCARDS a verifier that outlives timeout_sec
    # (3600 s) and aborts every later step, while an expired run.py still ends
    # below in `report 0 verifier-crash`.
    out=$(timeout -k 15 3330 python3 -I /tests/run.py 2>&1)
    rc=$?
    printf '%s\n' "$out"

    # Reopen the reward directory BEFORE the process sweep. If the sweep
    # terminates this shell, run.py's reward.json must already be readable and
    # deletable by the host-side archiver; a root-owned 0700/0755 directory is
    # not. report() reopens it again on the normal path.
    chmod 0777 /logs/verifier 2>/dev/null || true
    chmod 0644 /logs/verifier/reward.json /logs/verifier/scored_by 2>/dev/null || true

    # Sweep everything the candidate left running, BEFORE writing the score, so
    # nothing can write after this shell does.
    #
    # The first version of this matched command lines containing `/app` -- the
    # candidate's own tree -- and the attacker walked straight through it, because
    # its detached child is
    #
    #     /bin/sh -c while :; do echo '{"reward": 1.0}' > ... ; sleep 0.2; done
    #
    # which names no path in /app at all. Matching on what the process looks like is
    # the same mistake as matching on what a call site is called (NOTES 42): the
    # attacker chooses the string. So the rule is inverted -- keep this shell and its
    # ancestors, kill everything else. `run.py` has already exited and this is a
    # disposable container, so nothing else in it is ours.
    # ...but only inside a container. Run anywhere else -- a local harness test, say
    # -- "kill everything that is not me or my parent" kills the machine's session.
    # It did exactly that once, from a negcheck case that executed this file
    # directly (NOTES 60). The verifier always runs containerised; if this marker is
    # absent we are not where this loop is safe.
    # Am I somewhere the sweep is safe? Asked the right way round this time.
    #
    # The first version asked "does /.dockerenv exist". On GKE it does NOT --
    # Harbor's verifier pods are started by containerd, which writes no such
    # file -- so in production this guard silently switched the sweep OFF, and
    # every local test passed because `docker run` DOES write it. The defence
    # was inert exactly where it was needed, and said so in a line nobody reads:
    # "not containerised: skipping the process sweep", in the GKE log, while the
    # attacker test was green on this box. A guard whose failure mode is
    # "disable the protection quietly" has to fail the other way.
    #
    # So: sweep unless PID 1 is POSITIVELY identified as the host's init. A
    # container's PID 1 is whatever its image started; this box's is /sbin/init.
    # An unrecognised PID 1 sweeps, which is the safe direction.
    pid1=$(tr '\0' ' ' < /proc/1/cmdline 2>/dev/null)
    case "$pid1" in
        /sbin/init*|/usr/sbin/init*|/lib/systemd/systemd*|/usr/lib/systemd/systemd*|systemd*)
            echo "PID 1 is the host init ($pid1): skipping the process sweep" >&2
            HARBOR_SKIP_SWEEP=host-init ;;
    esac
    keep=" 1 $$ "
    p=$$
    while [ -n "$p" ] && [ "$p" != "1" ]; do
        p=$(awk '{print $4}' "/proc/$p/stat" 2>/dev/null)
        [ -n "$p" ] && keep="$keep $p "
    done
    # ...and say what it did. A sweep that does not run looks exactly like a
    # sweep that found nothing, and that ambiguity is what hid the bug above for
    # a whole build (advisory 24: silence is not a result).
    if [ -z "${HARBOR_SKIP_SWEEP:-}" ]; then
        swept=0
        for c in /proc/[0-9]*; do
            pid=${c#/proc/}
            case "$keep" in *" $pid "*) continue ;; esac
            kill -9 "$pid" 2>/dev/null && swept=$((swept + 1))
        done
        echo "swept $swept" > /logs/verifier/swept
        echo "process sweep: killed $swept stray process(es)" >&2
    else
        echo "skipped ${HARBOR_SKIP_SWEEP}" > /logs/verifier/swept
    fi

    line=$(printf '%s\n' "$out" | grep -E "^HARBOR_REWARD $HARBOR_SCORE_NONCE " | tail -1)
    if [ -n "$line" ]; then
        report "${line##* }" "$HARBOR_SCORE_NONCE"
    else
        # No line carrying this shell's nonce: run.py did not reach the end, or
        # something else produced whatever is in reward.json. An unexplained reward
        # file is exactly what the attack produces, so it is REPLACED, not kept.
        echo "SCORING FAILED: /tests/run.py exited $rc without reporting a score."
        report 0 verifier-crash
    fi
    exit 0
}

_verify "$@"
