#!/usr/bin/env bash
# Runs inside one step's verifier image. Reconstructs THIS STEP'S reference
# tree from the pin, builds it, records which of the project's own suites it
# already fails, and bakes the per-parameter-set measurement harnesses for it.
#
# Everything here is the reference side of the comparison and must not depend
# on anything the candidate controls.
set -eu

. /tests/vendor/PIN

git clone --quiet --depth 1 --branch "$MLD_TAG" "$MLD_REPO" /base
cd /base
test "$(git rev-parse HEAD)" = "$MLD_SHA" || {
    echo "PIN MISMATCH: $(git rev-parse HEAD) != $MLD_SHA"; exit 1; }
rm -rf .git

# An unstripped copy, kept only for the non-scored similarity diagnostic, and
# the ladder's floor: the same tree with every stage removed. The floor is what
# the end-to-end non-regression gate is baselined on -- see test.sh.
cp -a /base /upstream
cp -a /base /l0

# This step's base: the same tree the candidate was handed.
python3 /tests/vendor/strip.py /base $(cat /tests/BASE_STAGES.step06)

# Four, not nproc: this box is shared with other author sessions, and a
# 16-way build from inside a verifier container starves them and makes every
# measurement on the box noisier, including this task's own.
JOBS=4
make -C /base lib OPT=1 AUTO=1 BUILD_DIR=/basebld -j"$JOBS" > /build_lib.log 2>&1
make -C /base func_44 func_65 func_87 unit_44 unit_65 unit_87 kat_44 kat_65 kat_87 \
     bench_65 OPT=1 AUTO=1 BUILD_DIR=/basebld -j"$JOBS" CYCLES=PMU > /build_tests.log 2>&1

: > /base_suite_failures.txt
for ps in 44 65 87; do
    /basebld/mldsa$ps/bin/test_mldsa$ps > /dev/null 2>&1 || echo "func$ps" >> /base_suite_failures.txt
    /basebld/mldsa$ps/bin/test_unit$ps  > /dev/null 2>&1 || echo "unit$ps" >> /base_suite_failures.txt
    ( cd /base && /basebld/mldsa$ps/bin/gen_KAT$ps | python3 META.py --scheme $ps ) \
        > /dev/null 2>&1 || echo "kat$ps" >> /base_suite_failures.txt
done
echo "reference tree fails: $(grep -c . /base_suite_failures.txt || true) suite(s)"

# The ladder's floor, and enough of it built to time the pipeline end to end.
python3 /tests/vendor/strip.py /l0 ntt intt pointwise decompose32 decompose88 rej unpack
make -C /l0 bench_65 OPT=1 AUTO=1 BUILD_DIR=/l0bld -j"$JOBS" CYCLES=PMU \
    > /l0build.log 2>&1
test -x /l0bld/mldsa65/bin/bench_mldsa65 || { echo "floor tree did not build"; exit 1; }

# Measurement harnesses for the reference side, one per parameter set this step
# scores. Compiled against the reference tree's own headers and library.
for ps in $(awk '{print $2}' /tests/FUNCS.step06 | sort -u); do
    gcc -c -O2 -DPMU_CYCLES -DMLD_FORCE_X86_64 -DMLD_CONFIG_PARAMETER_SET="$ps" \
        -I/base -I/base/mldsa -I/base/test/hal \
        /base/test/hal/hal.c -o "/basebld/hal$ps.o"
    gcc -O2 -std=c99 -fomit-frame-pointer -mavx2 -mbmi2 \
        -DPMU_CYCLES -DMLD_FORCE_X86_64 \
        -DMLD_CONFIG_USE_NATIVE_BACKEND_ARITH \
        -DMLD_CONFIG_USE_NATIVE_BACKEND_FIPS202 \
        -DMLD_CONFIG_PARAMETER_SET="$ps" \
        -I/base -I/base/mldsa -I/base/test/hal \
        -o "/tmp/h_base_$ps" /tests/vendor/bench_funcs.c \
        "/basebld/libmldsa$ps.a" "/basebld/hal$ps.o"
    # and prove it runs, so a broken reference is loud at image-build time
    "/tmp/h_base_$ps" list > /dev/null
    echo "reference harness for ML-DSA-$ps: $("/tmp/h_base_$ps" list | wc -l) entries"
done

rm -f /build_lib.log /build_tests.log
