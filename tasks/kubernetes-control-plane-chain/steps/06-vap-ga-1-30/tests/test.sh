#!/usr/bin/env bash
set -u
source /tests/helpers.sh

# Read the whole verifier before running any of it: bash reads a script
# incrementally, so a body left at top level could in principle be rewritten
# mid-execution. /tests is not candidate-writable, but this closes the class
# rather than resting on the permission.
_run_verifier() {

    # Everything written below is read by something else -- the scorer's own logs by
    # whatever collects them, the components' inputs by an unprivileged uid. Set the
    # modes here rather than inheriting whatever umask launched this.
    umask 0022

    # Every command run as root here and in score.py is found through this
    # PATH, so it names only root-owned system directories, none of which the
    # candidate uid can write.
    export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

    # Nothing below is run from a directory the candidate owns: the scorer's
    # own imports and every relative path resolve from /, never from /app.
    cd / || report 0

    # Harbor leaves /logs/verifier world-writable. Take it back before any
    # candidate code runs, so neither the build nor a component -- nor anything
    # they leave running -- can replace reward.json (helpers.sh).
    harden_reward_dir

    # A packaging fault is not a candidate result. Record it in
    # precondition_failed.json; reward.json is still written as 0.0, for the
    # reason the message below gives.
    fail_precondition() {
        {
            echo "VERIFIER PRECONDITION FAILURE: $1"
            echo "This is a packaging/environment fault, not a graded result."
            echo "precondition_failed.json records that. reward.json is still"
            echo "written, as a scored 0.0: to Harbor a non-zero exit is a"
            echo "verifier crash, and it would then skip every later step of"
            echo "the chain rather than just declining to score this one."
        } > /logs/verifier/score.log
        printf '{"precondition_failed": true, "reason": "%s"}\n' "$1" \
            > /logs/verifier/precondition_failed.json
        report 0
    }

    # This script is byte-identical in all nine steps. The per-release constants
    # live in release-1.<minor>.env -- a path that exists in exactly one of the
    # nine build contexts -- because every step's tests/ directory is a separate
    # Docker build context with the same name and the same relative paths inside
    # it, and Docker's context sync treats (path, size, mtime) as identity, so a
    # per-step test.sh could be built with a neighbouring step's copy.
    # A path unique to this step cannot be confused with another step's, and the
    # values are cross-checked below against the expectations actually present.
    shopt -s nullglob
    rel=(/tests/release-*.env)
    hid=(/tests/expected/v1.*-hid.json)
    shopt -u nullglob
    [ "${#rel[@]}" -eq 1 ] \
        || fail_precondition "expected exactly one /tests/release-*.env, found ${#rel[@]}"
    [ "${#hid[@]}" -eq 1 ] \
        || fail_precondition "expected exactly one /tests/expected/v1.*-hid.json, found ${#hid[@]}"

    KCP_MINOR=""
    KCP_EXPECTED=""
    KCP_APISERVER_ARGS=""
    # shellcheck source=/dev/null
    . "${rel[0]}"
    export KCP_MINOR KCP_EXPECTED KCP_APISERVER_ARGS

    hid_base=${hid[0]##*/}
    hid_minor=$(printf '%s' "$hid_base" | sed -n 's/^v1\.\([0-9][0-9]*\)\..*$/\1/p')
    [ -n "$KCP_MINOR" ] \
        || fail_precondition "${rel[0]##*/} sets no KCP_MINOR"
    [ "$KCP_EXPECTED" = "$hid_base" ] \
        || fail_precondition "constants name $KCP_EXPECTED but this image holds $hid_base"
    [ "$KCP_MINOR" = "$hid_minor" ] \
        || fail_precondition "KCP_MINOR=$KCP_MINOR does not match $hid_base"
    [ -f /tests/expected/plan.json ] \
        || fail_precondition "missing /tests/expected/plan.json"
    [ -f /tests/expected/milestone.json ] \
        || fail_precondition "missing /tests/expected/milestone.json"

    # Graded from here. A reward exists before scoring starts, so that a verifier
    # the kernel kills -- this one runs etcd and three components -- still reports
    # a result rather than an absence, which would abort every later step.
    printf '{"reward": 0.0}\n' > /logs/verifier/reward.json

    # The whole scorer is bounded inside the step's own verifier timeout
    # (3600 s), which Harbor enforces by discarding the step -- and aborting
    # every later one -- even when a reward.json exists. score.py stops itself
    # at 3180 s and still writes a score from what it observed; this outer
    # bound only catches a scorer that wedges anyway: 3260 s + 20 s of grace,
    # then at most ~36 s of sweep, which is ~3320 s against the 3600 s budget.
    # -I: the scorer's imports come from the paths it sets itself, never from
    # the environment, the user site or the working directory.
    timeout -k 20 3260 python3 -I /tests/score.py > /logs/verifier/score.log 2>&1
    rc=$?

    # score.py aborts the same way on the same preconditions if it is ever run
    # directly: rc 3 plus this file means nothing was graded. That is still
    # reported as a scored 0.0, for the reason fail_precondition gives above.
    if [ "$rc" -eq 3 ] && [ -f /logs/verifier/precondition_failed.json ]; then
        report 0
    fi

    # score.py writes report.json only when it reaches its own `finally`, so its
    # presence is what separates "scored and earned nothing" from "never scored".
    if [ -f /logs/verifier/report.json ]; then
        echo "scorer completed (rc=$rc); reward.json is a graded result" \
            >> /logs/verifier/score.log
        finish
    fi
    echo "SCORER DID NOT COMPLETE (rc=$rc): the reward left in reward.json is the" \
         "provisional 0.0 written before scoring began, NOT a graded result" \
        >> /logs/verifier/score.log
    printf '{"scorer_completed": false, "rc": %s}\n' "$rc" \
        > /logs/verifier/scorer_status.json
    if [ -f /logs/verifier/reward.json ]; then
        finish
    fi
    report 0

}

_run_verifier "$@"