#!/usr/bin/env bash
# Verifier for step 13 -- resolution, environments and index precedence.
#
#   reward = gate * score
#
# `gate` is 1 only when the candidate's tree builds at all and every gate
# group passes: proof of life plus every earlier step's behaviour, unchanged.
# It earns nothing on its own; one failing gate probe zeroes the step.
# `score` is the weighted fraction of *this* step's new-capability groups that
# pass, weights summing to 1.0. A tree carried over from the previous step
# without further work passes the gate and scores exactly 0.
#
# Nothing below reads the candidate's build output: the workspace is rebuilt
# from source into a target directory the candidate never sees, with the
# registry that was prewarmed into this image and the network switched off.
set -u
source /tests/helpers.sh

# Before hardening, and before anything else: this runs no candidate code, and
# a mis-bound image should bail rather than grade the wrong milestone.
require_step 13

# Lock the reward directory before anything else. Everything below that
# touches candidate code runs through run_unprivileged, so it cannot write the
# score even though the runner chmods this directory to 777 just before we start.
harden_reward_dir

STEP=13
BUILD_DEADLINE=3600
GRADE_DEADLINE=2700
LOG=/tmp/verifier.log
DETAILS=/tmp/reward_details.json
: > "$LOG"

say() { printf '%s %s\n' "$(date -u +%H:%M:%S)" "$*" >> "$LOG"; printf '%s\n' "$*" >&2; }

# Publish the transcript and the per-group attribution next to the reward, then
# report. `report` chmods the directory back to 755 and exits 0; do the copies
# first so a reader gets the evidence whatever the score was.
finish() {
    local reward="$1"
    cp "$DETAILS" /logs/verifier/reward_details.json 2>/dev/null || true
    tail -c 400000 "$LOG" > /logs/verifier/verifier.log 2>/dev/null || true
    chmod 644 /logs/verifier/reward_details.json /logs/verifier/verifier.log 2>/dev/null || true
    say "reward=$reward"
    report "$reward"
}

# A stage that never produced a score still has to say why, in the same file a
# green run writes, so a zero is never ambiguous with "the agent did nothing".
bail() {
    local stage="$1" note="$2"
    say "FAILED at $stage: $note"
    printf '{"gate_passed":false,"score":0,"reward":0,"verifier_stage":"%s","note":"%s"}\n' \
        "$stage" "$note" > "$DETAILS"
    finish 0
}

# --- build the candidate ------------------------------------------------- #
#
# The artifact arrives owned by whoever wrote it; hand it to nobody so the
# unprivileged build can read it and any build script can write beside it.
# The target directory lives under /tmp, not /app: a `target/` the candidate
# shipped is then never linked against, and cannot be mistaken for this
# build's output.
give_app_to_unprivileged

export CARGO_HOME=/opt/cargo
export CARGO_TARGET_DIR=/tmp/target
export CARGO_NET_OFFLINE=true
export CARGO_TERM_COLOR=never
export RUSTUP_TOOLCHAIN="$(cat /etc/rust-toolchain-id)"

# Dev profile with overrides rather than `--release`. uv's own
# `[profile.release]` sets `lto = "fat"`, whose single-threaded link ran past
# 22 minutes here on 20 vCPU; the dev profile below was swept against a
# release build over the probe corpus and graded identically.
export CARGO_PROFILE_DEV_OPT_LEVEL=1
export CARGO_PROFILE_DEV_DEBUG=0
export CARGO_PROFILE_DEV_STRIP=debuginfo
export CARGO_PROFILE_DEV_LTO=off
export CARGO_PROFILE_DEV_INCREMENTAL=false
export CARGO_PROFILE_DEV_CODEGEN_UNITS=16

# The keys above buy build speed; these two buy *correctness of the
# measurement*.  Cargo's dev profile turns debug assertions and integer
# overflow checks on, and `--release` turns both off -- so a dev build can
# panic where the release binary this task's ground truth was recorded from
# runs clean.  uv 0.1.0 does exactly that: a `debug_assert!` in
# `uv-interpreter` rejects our interpreter fixture, whose short-named
# directory is a symlink, so `sys.base_prefix != sys.base_exec_prefix`.
# Grading a binary that differs from the recorded one measures the build
# profile, not the candidate.
export CARGO_PROFILE_DEV_DEBUG_ASSERTIONS=false
export CARGO_PROFILE_DEV_OVERFLOW_CHECKS=false

# `nproc` reports the host's cores; the runner gives this container a CFS quota
# instead. Oversubscribing measurably costs both time and peak memory
# (-j20 against -j8 on this workload: 236s/3.87GB against 166s/3.20GB), so
# derive the real width from the cgroup and fall back to nproc.
JOBS="$(awk '{ if ($1 == "max") print 0; else printf "%d", ($1 + $2 - 1) / $2 }' \
        /sys/fs/cgroup/cpu.max 2>/dev/null || echo 0)"
[ "${JOBS:-0}" -gt 0 ] 2>/dev/null || JOBS="$(nproc)"
say "building candidate: toolchain=$RUSTUP_TOOLCHAIN jobs=$JOBS deadline=${BUILD_DEADLINE}s"
rc=0
run_unprivileged timeout "$BUILD_DEADLINE" \
        cargo build --manifest-path /app/Cargo.toml --bin uv -j "$JOBS" >> "$LOG" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    if [ "$rc" -eq 124 ]; then
        bail "build" "cargo build exceeded ${BUILD_DEADLINE}s"
    fi
    bail "build" "cargo build failed with status $rc; see verifier.log"
fi

UV=/tmp/target/debug/uv
[ -x "$UV" ] || bail "build" "cargo build succeeded but $UV is not executable"

# --- grade --------------------------------------------------------------- #
#
# One runner.py pass evaluates every group from one set of probe observations.
# It runs unprivileged: it executes the candidate's binary, and the binary must
# not be able to reach the reward.
say "grading: deadline=${GRADE_DEADLINE}s"
rc=0
run_unprivileged timeout "$GRADE_DEADLINE" python3 /tests/runner.py \
        --uv "$UV" \
        --pythons /opt/pythons \
        --groups /tests/groups/step-13.toml \
        --probes /tests/probes/gate \
        --asserts /tests/asserts/step-00.json \
        --probes /tests/probes/step-01 \
        --asserts /tests/asserts/step-01.json \
        --probes /tests/probes/step-02 \
        --asserts /tests/asserts/step-02.json \
        --probes /tests/probes/step-03 \
        --asserts /tests/asserts/step-03.json \
        --probes /tests/probes/step-04 \
        --asserts /tests/asserts/step-04.json \
        --probes /tests/probes/step-05 \
        --asserts /tests/asserts/step-05.json \
        --probes /tests/probes/step-06 \
        --asserts /tests/asserts/step-06.json \
        --probes /tests/probes/step-07 \
        --asserts /tests/asserts/step-07.json \
        --probes /tests/probes/step-08 \
        --asserts /tests/asserts/step-08.json \
        --probes /tests/probes/step-09 \
        --asserts /tests/asserts/step-09.json \
        --probes /tests/probes/step-10 \
        --asserts /tests/asserts/step-10.json \
        --probes /tests/probes/step-11 \
        --asserts /tests/asserts/step-11.json \
        --probes /tests/probes/step-12 \
        --asserts /tests/asserts/step-12.json \
        --probes /tests/probes/step-13 \
        --asserts /tests/asserts/step-13.json \
        --out /tmp/reward.json \
        --details "$DETAILS" \
        --jobs 4 --quiet >> "$LOG" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    if [ "$rc" -eq 124 ]; then
        bail "grade" "runner.py exceeded ${GRADE_DEADLINE}s"
    fi
    bail "grade" "runner.py failed with status $rc; see verifier.log"
fi

REWARD="$(python3 -c 'import json,sys; print(json.load(open("/tmp/reward.json"))["reward"])' 2>/dev/null)" \
    || bail "grade" "runner.py produced no readable reward"

finish "$REWARD"
