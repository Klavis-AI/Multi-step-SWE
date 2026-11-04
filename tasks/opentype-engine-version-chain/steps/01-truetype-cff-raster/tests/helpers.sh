#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root.  Keep in sync.
set -u

# Unprivileged identity for every candidate process (nobody:nogroup exists in
# the bookworm base).  score.py uses the same number; keep the two in step.
CAND_UID=65534

# Lock the reward directory to root before any candidate code runs.  Harbor
# creates it 0777; the candidate runs as CAND_UID, so root 0700 is a fence
# the filesystem enforces.  report() reopens it to 0755 at the very end.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 0700 /logs/verifier 2>/dev/null || true
}

# Shut the recorded answers to the candidate's uid.  Everything under /tests
# becomes root-only except what the candidate's process must open: the fonts
# it is handed (corpus/) and the guard it preloads.  /tests itself stays
# traversable so those two remain reachable by path, but not listable.  The
# reference implementations this image carries are shut the same way: the
# scorer runs them as root, the candidate may not.
lockdown_tests() {
    chmod -R go-rwx /tests 2>/dev/null || true
    chmod 0711 /tests 2>/dev/null || true
    chmod 0644 /tests/guard.cjs 2>/dev/null || true
    [ -d /tests/corpus ] && chmod -R a+rX /tests/corpus 2>/dev/null
    local f
    for f in /usr/local/bin/ftlint /usr/local/bin/ftsbit /usr/local/bin/ftdump \
             /usr/local/bin/hb-* /usr/local/bin/ots-* /usr/local/bin/fonttools \
             /usr/local/bin/pyft* /usr/local/bin/ttx \
             /usr/local/lib/x86_64-linux-gnu/libfreetype* \
             /usr/local/lib/x86_64-linux-gnu/libharfbuzz* \
             /usr/lib/x86_64-linux-gnu/libfreetype* /usr/lib/x86_64-linux-gnu/libharfbuzz* \
             /usr/local/lib/python3*/dist-packages/fontTools; do
        [ -e "$f" ] && [ ! -L "$f" ] && chmod -R go-rwx "$f" 2>/dev/null
    done
    # /src holds the source trees this image was built from (FreeType,
    # freetype-demos, HarfBuzz, OTS), and with them the upstream test corpora
    # whose checked-in expectations are the graded answers. Root-only, like
    # /tests; the root scorer does not need it. 0700 on the root-owned top
    # directory closes every path into it.
    [ -d /src ] && chown root:root /src 2>/dev/null && chmod 0700 /src 2>/dev/null
    return 0
}

_cand_can_read() {
    setpriv --reuid="$CAND_UID" --regid="$CAND_UID" --clear-groups --inh-caps=-all \
        --bounding-set=-all /bin/sh -c 'head -c 1 "$1" >/dev/null 2>&1' sh "$1"
}

# Ask the filesystem as the candidate's uid; do not trust the chmods above.
# Every answer file must be unreadable, and the guard plus a font must stay
# readable: that positive control is what proves the probe itself works.
lockdown_verify() {
    local f n=0 leaked=0 ctl=0 font
    for f in /tests/*.json /tests/score.py; do
        [ -f "$f" ] || continue
        n=$((n + 1))
        if _cand_can_read "$f"; then
            echo "FAIL lockdown: uid $CAND_UID can read $f" >&2
            leaked=$((leaked + 1))
        fi
    done
    if [ -d /src ]; then
        f=$(find /src -type f -name '*.tests' 2>/dev/null | head -1)
        [ -n "$f" ] || f=$(find /src -type f 2>/dev/null | head -1)
        if [ -n "$f" ]; then
            n=$((n + 1))
            if _cand_can_read "$f"; then
                echo "FAIL lockdown: uid $CAND_UID can read $f" >&2
                leaked=$((leaked + 1))
            fi
        fi
    fi
    font=$(find /tests/corpus -type f 2>/dev/null | head -1)
    if [ -n "$font" ] && _cand_can_read /tests/guard.cjs && _cand_can_read "$font"; then
        ctl=1
    fi
    echo "lockdown: answer files examined=$n still-readable=$leaked controls-readable=$ctl" >&2
    [ "$n" -gt 0 ] && [ "$leaked" -eq 0 ] && [ "$ctl" -eq 1 ]
}

# Kill every process of the candidate uid (inside this verifier container),
# until none is left.
sweep_candidate() {
    local i
    for i in $(seq 1 50); do
        pgrep -u "$CAND_UID" >/dev/null 2>&1 || return 0
        pkill -KILL -u "$CAND_UID" 2>/dev/null || true
        sleep 0.1
    done
    echo "WARNING: uid $CAND_UID processes survived the sweep" >&2
}

# report <score>: clamp to [0,1], write /logs/verifier/reward.json, exit 0.
# A non-zero exit is a verifier crash and would skip the remaining steps.
report() {
    local reward
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    # Reopen so a local docker run, which reads this from the host, can collect
    # it.  Still root-owned: the candidate cannot write it.
    chmod 0755 /logs/verifier 2>/dev/null || true
    chmod 0644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}
