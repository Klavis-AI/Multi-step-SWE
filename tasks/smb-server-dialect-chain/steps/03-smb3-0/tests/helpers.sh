#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
set -u

# The uid the candidate's service runs as (nobody). harness/runner.py starts
# it under this uid; keep the two in step.
SERVICE_UID=65534

# Lock the reward directory to root. Called first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here could otherwise unlink reward.json and put its own in
# its place -- including a detached child of the service, which outlives the
# scorer and writes after it. This runs after Harbor's chmod, so this is the
# mode that sticks; the service is not root, so the mode binds it.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
}

# The service reads its own source from /app as the unprivileged uid.
give_app_to_service() {
    chown -R "$SERVICE_UID:$SERVICE_UID" /app 2>/dev/null || true
}

# Kill every process of the service uid, looping until none is left: a
# respawning chain survives one snapshot, not a loop. `kill -KILL -1` sent AS
# that uid reaches all of them in one pass that new forks cannot race, and
# pkill catches anything that slips through. At most 30 s, and every command in
# it is bounded, so the sweep cannot be what overruns the step.
sweep_service_uid() {
    local end=$((SECONDS + 30))
    while [ "$SECONDS" -lt "$end" ]; do
        timeout -k 1 5 setpriv --reuid "$SERVICE_UID" --regid "$SERVICE_UID" \
            --clear-groups bash -c 'kill -KILL -1' >/dev/null 2>&1 || true
        timeout -k 1 5 pkill -KILL -u "$SERVICE_UID" >/dev/null 2>&1 || true
        # a zombie is already dead; only a live process can still write
        timeout -k 1 5 ps -u "$SERVICE_UID" -o stat= 2>/dev/null \
            | grep -qv '^Z' || return 0
        sleep 0.5
    done
    echo "WARNING: uid $SERVICE_UID still has processes after the sweep" >&2
}

# Reopen the reward directory once scoring is done so the host can collect it.
#
# Harbor bind-mounts this directory from the host trial directory and, after
# this container exits, archives its contents as the ordinary host user. That
# user does not own the root-owned directory, and rename/unlink of the archived
# files needs write permission on the DIRECTORY, not on the 0644 files. A 0755
# directory made the host archive fail with EACCES and the trial ended with no
# collected reward, so the directory is reopened 0777.
#
# This runs after the caller has swept every process of the service uid and
# after the authoritative reward.json was written, so no candidate process is
# left to use the write bit; root ownership and the 0644 reward files remain.
release_reward_dir() {
    chmod 0777 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json /logs/verifier/report.json \
        /logs/verifier/report.txt 2>/dev/null || true
}
