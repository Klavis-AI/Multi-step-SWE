#!/bin/bash
# Reconstruct the task's base tree inside the verifier image, and record what
# it needs to be judged against.
#
# Identical in every step. Its output for a given pin is always the same tree,
# so building it once per image and letting BuildKit serve the layer from cache
# for the remaining steps is exactly right -- and every step still verifies its
# own /etc/harbor-step-id against its own bound per-step files.
#
# -j4 rather than -j$(nproc): this host is shared with other sessions, and a
# sixteen-way build from one container starves the rest of the box.
set -euo pipefail

. /tmp/vendor/PIN

clone_and_keep_upstream() {
    git clone --quiet "$ISAL_REPO" /base
    cd /base
    git checkout --quiet "$ISAL_SHA"
    test "$(git rev-parse HEAD)" = "$ISAL_SHA"
    rm -rf .git

    # Keep upstream's sources for the ladder's directories before they are
    # removed: they are the reference for the non-scored similarity diagnostic
    # ("of the lines the candidate added, how many occur verbatim upstream").
    # Only the derived, sorted line set survives into the image.
    mkdir -p /opt/upstream
    for d in $LADDER_DIRS; do
        cp -a "/base/$d" "/opt/upstream/$d"
    done
    find /opt/upstream -type f \
         \( -name '*.asm' -o -name '*.S' -o -name '*.c' -o -name '*.h' \) \
         -exec cat {} + \
        | sed -E 's/^[ \t]+//; s/[ \t]+$//' | awk 'length($0) > 0' | sort -u \
        > /opt/upstream_lines.txt
    echo "upstream line set: $(wc -l < /opt/upstream_lines.txt) distinct lines"
    rm -rf /opt/upstream
}

LADDER_DIRS="sha256_mb sha1_mb sha512_mb md5_mb sm3_mb mh_sha1 mh_sha1_murmur3_x64_128 mh_sha256 rolling_hash"

clone_and_keep_upstream

python3 /tmp/vendor/strip.py /base $LADDER_DIRS

# Upstream's Windows makefile is generated from the same make variable lists,
# so it has to be regenerated rather than left naming deleted sources.
make -f Makefile.unx Makefile.nmake > /dev/null 2>&1

echo "== base: static library =="
if ! make -f Makefile.unx lib -j4 > /tmp/base_lib.log 2>&1; then
    echo "BASE LIB BUILD FAILED"; tail -40 /tmp/base_lib.log; exit 1
fi
test -f /base/bin/isa-l_crypto.a || { echo "BASE LIB MISSING after a clean build"; exit 1; }

echo "== base: its own test suite =="
if ! make -f Makefile.unx checks -j4 > /tmp/base_checks.log 2>&1; then
    echo "BASE CHECKS BUILD FAILED"; tail -40 /tmp/base_checks.log; exit 1
fi
( make -k -f Makefile.unx check > /tmp/base_check.log 2>&1 || true )
# `set -o pipefail` plus an empty match set would abort the script here: a base
# with no failures makes grep exit 1, which is the healthy case.
grep -oE '\[[^]]*: ([^] ]+)\.run\]' /tmp/base_check.log \
    | sed -E 's/.*: (.*)\.run\]/\1/' | sort -u > /base_check_failures.txt || true
base_ran=$(grep -c 'Completed run:' /tmp/base_check.log || true)
base_failed=$(wc -l < /base_check_failures.txt)
echo "base: ran ${base_ran:-0} check tests, ${base_failed} fail"
[ "${base_ran:-0}" -gt 0 ] || { echo "BASE RAN NO CHECK TESTS -- the suite or its runner is broken"; exit 1; }
# The candidate-side gate compares its own count against this one: a candidate
# check that stops building reports no failure at all, so the count is the
# evidence that the suite still runs.
echo "${base_ran:-0}" > /base_check_ran.txt
cat /base_check_failures.txt

rm -rf /tmp/vendor
