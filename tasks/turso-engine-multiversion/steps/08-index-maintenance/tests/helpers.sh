#!/usr/bin/env bash
# Reward reporting. report() is the only writer of /logs/verifier/reward.json
# and it always exits 0: a verifier that exits non-zero costs the candidate
# every remaining step of the trial.

report() {
    local raw="${1:-0}"
    local clamped
    clamped="$(awk -v r="$raw" 'BEGIN{
        if (r == "" || r != r + 0) r = 0;
        if (r < 0) r = 0;
        if (r > 1) r = 1;
        printf "%.6f", r;
    }')"
    mkdir -p /logs/verifier
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward": %s}\n' "$clamped" > /logs/verifier/reward.json
    printf 'REWARD %s\n' "$clamped"
    exit 0
}
