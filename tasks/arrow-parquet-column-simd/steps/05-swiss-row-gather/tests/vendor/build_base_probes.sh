#!/usr/bin/env bash
# Second half of the base build, and the fixture side of it.
#
# This is separate from build_base_libs.sh purely so that changing it costs a
# few seconds instead of the ~50-minute Arrow build underneath: the Arrow build
# does not read any of these files, so a harness edit must not invalidate it.
#
# It leaves behind:
#   /probe/{kbench,e2e}.base   the two probes linked against the base build
#   /td/arrow-testing          upstream test data
#   /td/parquet-testing        upstream parquet test data
#   /baseline/                 the base's own test failures, its assertion
#                              counts, and the cpu facts
set -euo pipefail
. /tests/vendor/PIN

echo "== build the base probes =="
mkdir -p /probe
bash /tests/vendor/build_probe.sh /src /bld /tests/vendor/kbench.cc /probe/kbench.base
bash /tests/vendor/build_probe.sh /src /bld /tests/vendor/e2e.cc /probe/e2e.base

echo "== upstream test data =="
git clone --quiet "$ARROW_TESTING_REPO" /td/arrow-testing
git -C /td/arrow-testing checkout --quiet "$ARROW_TESTING_SHA"
git clone --quiet "$PARQUET_TESTING_REPO" /td/parquet-testing
git -C /td/parquet-testing checkout --quiet "$PARQUET_TESTING_SHA"
export ARROW_TEST_DATA=/td/arrow-testing/data
export PARQUET_TEST_DATA=/td/parquet-testing/data

echo "== record this base tree's own test failures =="
# The candidate's suite is run as the unprivileged user, from a writable working
# directory, because the verifier builds candidate code that way.  Several of
# Arrow's tests write files relative to the working directory, so the baseline
# has to be recorded under exactly the same conditions or the difference shows
# up as "new failures" the candidate did not cause.
mkdir -p /baseline /tmp/nb
chown nobody:nogroup /tmp/nb 2>/dev/null || true
: > /baseline/test_failures.txt
: > /baseline/test_counts.txt
for t in $(cat /tests/vendor/test_targets.txt); do
    [ -x "/bld/release/$t" ] || { echo "MISSING TEST TARGET $t"; exit 1; }
    setpriv --reuid=nobody --regid=nogroup --clear-groups \
        env ARROW_TEST_DATA=/td/arrow-testing/data PARQUET_TEST_DATA=/td/parquet-testing/data \
        bash -c "cd /tmp/nb && /bld/release/$t" > "/baseline/$t.log" 2>&1 || true
    printf '%s %s\n' "$t" \
        "$(grep -cE '^\[       OK \]' "/baseline/$t.log")" >> /baseline/test_counts.txt
    # gtest prints "[  FAILED  ] 10 tests, listed below:" before the names; that
    # line is a summary, not a test, and counting it as one charges the candidate
    # for a test named "10 tests, listed below".
    grep -E '^\[  FAILED  \]' "/baseline/$t.log" \
        | sed -E 's/^\[  FAILED  \] //; s/ \([0-9]+ ms\)$//' \
        | grep -vE '^[0-9]+ tests?, listed below:$' >> /baseline/test_failures.txt || true
done
sort -u -o /baseline/test_failures.txt /baseline/test_failures.txt

echo "== cpu facts (the capability envelope is capped at these) =="
{ echo "nproc $(nproc)"; echo "flags $(grep -m1 ^flags /proc/cpuinfo | cut -d: -f2-)"; \
  echo "model $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-)"; } > /baseline/cpu.txt

# The pristine upstream tree is kept ONLY for the similarity diagnostic, and
# the candidate's own build runs in this container as an
# unprivileged user -- so it must not be readable by it.  test.sh re-asserts
# this before every run; novelty.py runs as root.
chmod -R go-rwx /upstream
chmod 0700 /upstream

base_total=$(awk '{s+=$2} END {print s+0}' /baseline/test_counts.txt)
echo "base assertions planned: $base_total"
[ "$base_total" -gt 0 ] || { echo "BASE SUITE RECORDED NOTHING -- refusing to ship a gate that cannot fire"; exit 1; }
echo "base failures: $(wc -l < /baseline/test_failures.txt)"
