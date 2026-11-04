#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# The unprivileged account every piece of candidate code runs as (nobody:nogroup).
CAND_UID=65534

# Take /logs/verifier back to root:root 0700.  Call first in test.sh, before any
# candidate code runs.
#
# Harbor chmods /logs/verifier to 0777 just before this container starts, so
# without this the candidate account could unlink and replace reward.json, and a
# process it left behind would outlive the scorer and write last.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# candidate_alive: true while a live (non-zombie) process of CAND_UID exists.
candidate_alive() {
    local f k v rest st uids
    for f in /proc/[0-9]*/status; do
        st=""; uids=""
        while read -r k v rest; do
            case $k in
                State:) st=$v ;;
                Uid:)   uids="$v $rest"; break ;;
            esac
        done 2>/dev/null < "$f"
        case $st in Z|X|"") continue ;; esac
        case " $uids " in *" $CAND_UID "*) return 0 ;; esac
    done
    return 1
}

# sweep_candidate: SIGKILL every process of the candidate account, looping until
# none is left.  kill(-1) issued *as* that account reaches exactly the processes
# it owns, detached and re-parented ones included, and nothing else.
sweep_candidate() {
    local i
    for i in $(seq 1 100); do
        setpriv --reuid="$CAND_UID" --regid="$CAND_UID" --clear-groups \
            bash -c 'kill -KILL -1' >/dev/null 2>&1
        candidate_alive || return 0
        sleep 0.1
    done
    echo "candidate processes survived 100 sweeps" >&2
    return 0
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
    sweep_candidate
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    # Reopen to 0755 so a host-side reader collects it.  Still root-owned, and
    # the candidate account has been swept, so nothing can replace it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}
