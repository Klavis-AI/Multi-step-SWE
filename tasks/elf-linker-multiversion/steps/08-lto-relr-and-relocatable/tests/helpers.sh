#!/usr/bin/env bash
# Verifier helpers. `report` is the only thing Harbor reads: it writes
# /logs/verifier/reward.json and always exits 0, because a non-zero exit from a
# verifier is a crash and costs every remaining step in the trial.
set -u

report() {
    local raw="${1:-0}"
    local clamped
    clamped=$(awk -v v="$raw" 'BEGIN {
        if (v == "" || v + 0 != v) v = 0;
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        printf "%.6f", v;
    }')
    mkdir -p /logs/verifier
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward": %s}\n' "$clamped" > /logs/verifier/reward.json
    echo "REWARD $clamped"
    exit 0
}
