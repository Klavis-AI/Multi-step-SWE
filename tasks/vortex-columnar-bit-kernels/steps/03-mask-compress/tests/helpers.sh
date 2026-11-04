#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
#
# The oracle baseline bound just above report() is the only per-step value;
# every other line is common to all steps.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

# Lock the reward directory to root. Call this first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs. Writing the real score last does not help: a
# detached child outlives the scorer and writes after it.
#
# This is only worth anything because the two things that run candidate code --
# the candidate's cargo build and the probe that links its crate -- are both
# dropped to nobody, and root's PATH is reset to the system directories before
# either of them can have written anywhere. See the PATH reset in test.sh.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# Run candidate-authored code as nobody instead of root.
#
# Needed for harden_reward_dir to mean anything: root can write a root-owned
# directory whatever its mode. Used for the candidate's cargo build and for
# every run of a binary that links the candidate's crate.
run_unprivileged() {
    # Absolute: this helper is called by root, and by then the candidate may have
    # written into the toolchain directory that PATH used to lead with.
    /usr/bin/setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        /usr/bin/env HOME=/tmp TMPDIR=/tmp "$@"
}

# Give /app and the candidate build tree to the unprivileged user. Call before
# an unprivileged build: /app arrives owned by whoever wrote the artifact, and
# without this the build cannot create its output directory, so every candidate
# scores 0 and the step looks merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
    mkdir -p /work
    chown -R "$SCORE_UID:$SCORE_GID" /work 2>/dev/null || true
    # The candidate builds offline against the registry cache rustup installed as
    # root, and cargo opens its package-cache lock read-WRITE even when it has
    # nothing to download. A root-only cache therefore makes an unprivileged build
    # fail with a permission error that reads like a broken candidate tree.
    # Open the toolchain up: it is read-only input, and it lives only in this
    # container, which is discarded after grading.
    chmod -R a+rwX /usr/local/cargo /usr/local/rustup 2>/dev/null || true
}

# require_step <NN>
#
# Fails loudly when this step's tests/ tree is not the one baked into this
# step's verifier image. /etc/harbor-step-id is written by the Dockerfile before
# `COPY . /tests`, so it names the *image*; "$1" names the *tree*.
#
# Reports instead of crashing: a non-zero exit makes Harbor skip the remaining
# steps, which buries the fault under a second symptom.
require_step() {
    local want="$1" got msg
    got=$(cat /etc/harbor-step-id 2>/dev/null) || got=MISSING
    [ "$got" = "$want" ] && return 0

    msg="VERIFIER/STEP MISMATCH: image is step ${got}, /tests tree is step ${want}."
    printf '%s\n' "$msg" >&2
    mkdir -p /logs/verifier 2>/dev/null || true
    printf '%s\n' "$msg" > /logs/verifier/step-mismatch.log
    # Numbers only. Harbor parses every key of this file as a float, and a
    # string field "kept for readability" aborting a trial after an earlier step
    # had graded correctly is a defect this category has shipped once. The
    # readable reason lives beside it in step-mismatch.log and on stderr.
    printf '{"reward":0,"correctness":0,"geomean_speedup":0,"similarity":0}\n' \
        > /logs/verifier/reward.json
    exit 0
}

# fail <reason> -- score this step zero, with a reason a fetched run can be read
# for. Never a bare 0: that is indistinguishable from an agent that did nothing.
fail() {
    local reason="$1"
    printf 'VERIFIER FAILURE: %s\n' "$reason" >&2
    mkdir -p /logs/verifier 2>/dev/null || true
    printf '%s\n' "$reason" > /logs/verifier/failure.log
    # Numbers only -- see the note in require_step. The reason is in failure.log.
    printf '{"reward":0,"correctness":0,"geomean_speedup":0,"similarity":0}\n' \
        > /logs/verifier/reward.json
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}

# report <correctness 0|1> <speedup> [<speedup> ...]
#
#   reward = 0 if correctness != 1 or geomean <= 1; otherwise geomean is
#          scored against this step's oracle baseline B: 1 at or above B,
#          ln(geomean)/ln(B) in between, clamped to [0, 1].
#   geomean = exp( mean_f( ln(max(1, speedup_f)) ) )
#
# One speedup per function in this step's scope. The geometric mean is the right
# aggregator for ratios, and it penalises neglect: a function left alone
# contributes ln(1) = 0 and drags the step down, so a big win on one function
# cannot hide doing nothing on another.
#
# max(1, .) floors a regression at 0 rather than going negative. Correctness is
# binary and multiplies, so a tree that changes behaviour scores 0 however fast.
#
# The reward is normalized by this step's hardcoded oracle baseline and
# clamped to [0, 1]: it reaches 1 at the oracle speedup and scales
# logarithmically below it.
ORACLE_BASELINE=1.9985000000
report() {
    local correct="${1:-0}"; shift || true
    local reward
    # E2E_GATE=0 zeroes the reward without touching `correctness`. Set when the
    # end-to-end non-regression bound fired: getting slower is a score
    # question, not a behaviour question, and `correctness` is reserved for
    # "this tree behaves like the tree it started from". The candidate's score
    # is the same either way -- 0 -- which is the whole point: the guard still
    # fires, it is just filed under the half of the contract it belongs to.
    reward=$(awk -v c="$correct" -v e="${E2E_GATE:-1}" -v b="$ORACLE_BASELINE" 'BEGIN {
        c += 0; if (c != 1) c = 0
        if (e + 0 != 1) c = 0
        n = ARGC - 1; if (n <= 0) { printf "%.6f", 0; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        g = sprintf("%.4f", exp(s / n)) + 0
        if (c != 1 || g <= 1) { printf "%.6f", 0; exit }
        if (b <= 1 || g >= b) { printf "%.6f", 1; exit }
        r = log(g) / log(b)
        if (r < 0) r = 0; if (r > 1) r = 1
        printf "%.6f", r
    }' "$@")
    # The geometric mean is computed from the speedups themselves, not from the
    # reward: when the end-to-end gate has zeroed the reward the field must
    # still report what the per-function measurement actually saw.
    local geo
    geo=$(awk 'BEGIN {
        n = ARGC - 1; if (n <= 0) { printf "0.0000"; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        printf "%.4f", exp(s / n)
    }' "$@")
    mkdir -p /logs/verifier
    # `similarity` is a non-scored diagnostic, not a gate: the oracle IS
    # upstream, so any similarity gate would fail the reference solution. It is
    # a verbatim-line measure, so cosmetically rewritten code reads low and a
    # launderer is indistinguishable from an honest reimplementer. A lower
    # bound on copying, never a clean bill of health. Numeric because Harbor
    # parses every key under `rewards` as a float.
        # oracle binary step reward: 1 only above the hardcoded baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s,"similarity":%s}\n' \
        "$reward" "${correct:-0}" "$geo" "${SIMILARITY:-0}" > /logs/verifier/reward.json
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward} e2e_gate=${E2E_GATE:-1}"
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}
