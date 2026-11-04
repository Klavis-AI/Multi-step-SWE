#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Every step
# carries a byte-identical copy.
set -u

# report <score>
#   Writes this step's local score to /logs/verifier/reward.json and exits 0.
#   A non-zero exit is a verifier crash: Harbor then skips the remaining steps.
report() {
    local reward
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
    # Nothing of the candidate's outlives grading: kill every process of the
    # grader uid before the score is written (provenance.py sweep, from inside
    # that uid, so a forking process cannot outrun a pid-by-pid kill).
    python3 -I /tests/provenance.py sweep --user grader >/dev/null 2>&1 || true
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    chmod 0644 /logs/verifier/reward.json 2>/dev/null || true
    # Reopen the directory, still root-owned, so a harness that collects the
    # reward as an ordinary user can read it; the candidate still cannot write.
    chmod 0755 /logs/verifier 2>/dev/null || true
    echo "REWARD ${reward}"
    exit 0
}

# harden_reward_dir
#   The reward file is the harness's to write. A candidate that leaves a
#   detached child behind can otherwise write /logs/verifier/reward.json after
#   its own process is killed and score 1.0 having implemented nothing.
#   Root-owned 0700 with the mode SET here — not inherited — is the only
#   layer that survives SIGKILL of the candidate.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier
    chmod 0700 /logs/verifier
    rm -f /logs/verifier/reward.json
}

# ensure_grader
#   Re-create the unprivileged identity if the container was re-provisioned.
#   Fails closed: if we cannot end up with a usable non-root uid, say so.
ensure_grader() {
    if ! id -u grader >/dev/null 2>&1; then
        useradd --uid 4242 --create-home --shell /usr/sbin/nologin grader \
            || { echo "GATE FAIL: cannot create grader user"; return 1; }
    fi
    [ "$(id -u grader)" = "4242" ] || { echo "GATE FAIL: grader uid is $(id -u grader)"; return 1; }
    return 0
}

# gate_fields
#   Printed on EVERY step of EVERY run, so a posture that silently degraded is
#   visible in the log rather than inferred from a reward.
gate_fields() {
    echo "--- gate fields ---"
    echo "euid=$(id -u) grader_uid=$(id -u grader 2>/dev/null || echo ABSENT)"
    echo "reward_dir=$(stat -c '%U:%G %a' /logs/verifier 2>/dev/null || echo MISSING)"
    echo "expected_dir=$(stat -c '%U:%G %a' /tests/expected 2>/dev/null || echo MISSING)"
    echo "expected_files=$(ls -1 /tests/expected 2>/dev/null | wc -l)"
    echo "fixtures=$(find /tests/fixtures -name '*.html' 2>/dev/null | wc -l)"
    echo "free_kb=$(df -Pk /app | awk 'NR==2{print $4}')"
    echo "-------------------"
}

# candidate_cannot_read_answers
#   A runtime assertion, not a build-time assumption: prove, in this container,
#   on this run, that uid 4242 cannot read an expectation file and cannot write
#   the reward directory. Two-armed: it must also confirm it CAN read a fixture,
#   or a check that passes because the path is simply wrong looks identical.
candidate_cannot_read_answers() {
    local exp fix rc=0
    exp=$(ls -1 /tests/expected/* 2>/dev/null | head -1)
    fix=$(find /tests/fixtures -name '*.html' 2>/dev/null | head -1)
    if [ -z "$exp" ] || [ -z "$fix" ]; then
        echo "GATE FAIL: population empty (expected='$exp' fixture='$fix')"; return 1
    fi
    if runuser -u grader -- head -c 1 "$exp" >/dev/null 2>&1; then
        echo "GATE FAIL: grader CAN read $exp"; rc=1
    else
        echo "gate ok: grader cannot read expectations"
    fi
    if runuser -u grader -- sh -c 'echo x > /logs/verifier/reward.json' 2>/dev/null; then
        echo "GATE FAIL: grader CAN write /logs/verifier/reward.json"; rm -f /logs/verifier/reward.json; rc=1
    else
        echo "gate ok: grader cannot write the reward directory"
    fi
    if runuser -u grader -- head -c 1 "$fix" >/dev/null 2>&1; then
        echo "gate ok: grader CAN read fixtures (positive arm)"
    else
        echo "GATE FAIL: grader cannot read $fix - fixtures unreadable, scoring would be meaningless"; rc=1
    fi
    return $rc
}

# verify_tests_manifest
#   /tests is root-owned, but root can write it regardless of mode, so the mode
#   is not the control - this is. Compares every grading file against the
#   fingerprint baked into the image. Reports a COUNT, so a manifest that failed
#   to generate cannot read as "clean".
verify_tests_manifest() {
    local m=/tests/.manifest n
    if [ ! -f "$m" ]; then echo "GATE FAIL: /tests/.manifest missing"; return 1; fi
    n=$(wc -l < "$m")
    if [ "$n" -lt 5 ]; then echo "GATE FAIL: manifest has only $n entries"; return 1; fi
    if (cd / && sha256sum -c "$m" --quiet) >/dev/null 2>&1; then
        echo "gate ok: /tests intact (${n} files verified against the image manifest)"
        return 0
    fi
    echo "GATE FAIL: /tests differs from the image manifest:"
    (cd / && sha256sum -c "$m" 2>&1) | grep -v ': OK$' | head -8
    return 1
}
