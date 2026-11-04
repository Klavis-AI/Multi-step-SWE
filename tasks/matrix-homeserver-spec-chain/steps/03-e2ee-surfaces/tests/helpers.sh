#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

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
    _reported=1
    # The clamp below turns anything unparseable into 0.000000, which is the
    # right fault tolerance -- a verifier that cannot write a reward at all
    # stops the whole trial -- but a silent zero is indistinguishable from a
    # candidate that earned one, so say which it is.
    case "${1:-}" in
        *[!0-9.eE+-]* | "" )
            echo "SCORING FAILED: the score handed to report() was '${1:-}'." \
                 "A reward of 0 from here is about the verifier, not the" \
                 "candidate." >&2 ;;
    esac
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    exit 0
}

# If `test.sh` exits without ever calling `report` -- a crash, `set -u` on an
# unset variable, this shell killed -- Harbor finds no reward.json at all and
# stops the trial *at this step*, which costs every step after it too.  The
# trap turns that into a scored zero with a reason attached, so the failure is
# this step's and is legible as the verifier's rather than the candidate's.
# Whether `report` ran, not whether the file exists: a candidate's own build or
# server could have written a reward.json of its own, and "the file is there"
# would then hand it the score it wrote.  The flag is set inside `report`, in
# this same shell, where nothing the candidate runs can reach it.
_reported=0

_report_guard() {
    local rc=$?
    if [ "$_reported" = 1 ]; then exit "$rc"; fi
    echo "SCORING FAILED: the verifier exited ($rc) without writing a reward." \
         "A reward of 0 from here is about the verifier, not the candidate." >&2
    mkdir -p /logs/verifier
    printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
    exit 0
}
trap _report_guard EXIT
