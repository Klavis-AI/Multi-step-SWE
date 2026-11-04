#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync --
# tools_pkgcheck.py hashes every step's copy against this one and fails on any
# difference (NOTES 26).
set -u

# Lock the reward directory to root before any candidate code runs. Harbor
# chmods it 0777; every candidate process runs as an unprivileged uid (run.py),
# so a root-owned 0755 directory holds. (wave-A 2026-10-12; after
# harbor_template's harden_reward_dir.)
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    # 0755, not 0700: test.sh's process sweep stops the container before
    # report() can reopen the directory, and a host-side reader must still be
    # able to collect run.py's reward.json. Root-owned 0755 is already
    # unwritable for the candidate's uid.
    chmod 0755 /logs/verifier 2>/dev/null || true
}

# report <score> [origin]
#
#   reward  this step's local score in [0, 1]. Harbor's
#           multi_step_reward_strategy = "mean" averages this key across every
#           step that produced a verifier_result. reward.json holds this key and
#           nothing else -- see test.sh for why a companion key there is not
#           free.
#   origin  who produced the number, written to /logs/verifier/scored_by.
#           Defaults to "fallback", because run.py writes its own file and never
#           calls this: every reward that comes through here is one the scorer
#           did not compute. A silent clamp turns a scorer crash into a
#           plausible 0.000000, and nothing downstream can tell which kind of
#           zero it is looking at unless the verifier says so.
report() {
    local reward
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
    if ! awk -v a="${1:-}" -v b="$reward" 'BEGIN {
            ok = (a ~ /^[+-]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?$/) \
                 && (a + 0 == b + 0)
            exit !ok
         }'; then
        echo "report: ${1:-<empty>} is not a score in [0, 1]; clamped to $reward" >&2
    fi
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    echo "${2:-fallback}" > /logs/verifier/scored_by
    # Reopen the directory harden_reward_dir closed. 0777, not 0755: Harbor
    # bind-mounts /logs/verifier, and after this step a host-side process
    # running as the trial's user archives trial/verifier/* into
    # steps/<step>/verifier/ with shutil.move. Renaming a file out of a
    # directory needs write permission on the directory itself, and this one is
    # root-owned, so 0755 gives the host user EACCES: shutil.move falls back to
    # copy2 and the os.unlink(src) that follows fails the same way, aborting
    # the step with no reward. The score is already written and the candidate's
    # processes have been swept, so reopening here costs nothing.
    chmod 0777 /logs/verifier 2>/dev/null || true
    chmod 0644 /logs/verifier/reward.json /logs/verifier/scored_by 2>/dev/null || true
    sync 2>/dev/null || true
    exit 0
}
