#!/usr/bin/env bash
# Per-step copy: the test image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync with
# every other step's copy -- they must stay byte-identical.
set -u

# report <correctness 0|1> <speedup> [<speedup> ...]
#
# Writes /logs/verifier/reward.json. The step reward is binary and
# correctness-gated: 1 only when correctness is 1 and the geometric mean of the
# per-function speedups is above ORACLE_BASELINE, otherwise 0.
#
# One speedup per function in this step's scope. The geometric mean is the right
# aggregator for ratios, and it penalises neglect: a function left alone
# contributes ln(1) = 0 and drags the step down, so a big win on one function
# cannot hide doing nothing on another.
#
# max(1, .) floors a regression at 0 rather than going negative. Correctness is
# binary and gates the reward, so a tree that changes behaviour is rejected
# however fast it is.
ORACLE_BASELINE=1.2486000000
report() {
    local correct="${1:-0}"; shift || true
    local reward
    local geo; geo=$(awk 'BEGIN {
        n = ARGC - 1; if (n <= 0) { printf "%.4f", 1; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        printf "%.4f", exp(s / n)
    }' "$@")
    mkdir -p /logs/verifier
    # Binary step reward: 1 only above the baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s,"functions":%s}\n' \
        "$reward" "${correct:-0}" "$geo" "${#}" > /logs/verifier/reward.json
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward}"
    # Leave the directory readable for the host-side collector but still
    # root-owned, so candidate code cannot rewrite the number after the fact.
    # The file's own mode too: the collector reads it as an ordinary user from
    # the host, and a umask of 077 would otherwise hand it a reward that reads
    # as missing.  Both are `|| true`: on the reward path no mode can make the
    # reward wrong, only unreadable.
    chmod 0755 /logs/verifier 2>/dev/null || true
    chmod 0644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}

# Take the reward directory back from the candidate's reach. The session
# runtime chmods /logs/verifier 777 as root just before the tests start; this
# runs after, so it wins. Candidate code is run with run_unprivileged on top.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 0700 /logs/verifier 2>/dev/null || true
}

# Run candidate-controlled work as nobody, so a detached child cannot outlive
# the scorer and rewrite reward.json.
run_unprivileged() {
    if command -v setpriv > /dev/null && [ "$(id -u)" = 0 ]; then
        setpriv --reuid=nobody --regid=nogroup --clear-groups "$@"
    else
        "$@"
    fi
}

# Give an output directory to the unprivileged user a build will run as.
give_dir_to_unprivileged() {
    mkdir -p "$1"
    chown -R nobody:nogroup "$1" 2>/dev/null || true
    chmod -R u+rwX "$1" 2>/dev/null || true
}

# median_of <file-of-numbers>  -> median on stdout
median_of() {
    sort -g "$1" | awk '{v[NR]=$1} END { if (NR==0) { print 0; exit }
        if (NR % 2) print v[(NR+1)/2]; else printf "%.9g", (v[NR/2]+v[NR/2+1])/2 }'
}

# pool_min <file-of-numbers> -> minimum on stdout
pool_min() {
    sort -g "$1" | head -1
}
