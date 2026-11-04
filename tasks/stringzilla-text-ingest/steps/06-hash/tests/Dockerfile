# Verifier image for one step, built with that step's tests/ as the Docker
# build context. It reconstructs THIS STEP'S BASE from the pinned upstream
# commit, builds it, and keeps it as the reference: the speedup denominator is
# measured here, in the same container and on the same pinned cores as the
# candidate, so the ratio is self-calibrating and does not depend on how fast
# the grading machine is.
#
# This file is identical for every step. What differs is tests/SCOPE.stepNN
# (this step's family, for the envelope check) and tests/FUNCS.stepNN (the
# entry points it is scored on) -- per-step data files with the step number in
# their names, so two steps can never be served each other's copy from the
# builder's build-context cache.
FROM debian:bookworm-slim

# The verifier's toolchain is pinned here and is independent of the agent's: a
# candidate who breaks the compiler in /app does not change how it is graded.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates cmake coreutils diffutils g++ gcc gawk git make \
       patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

# Step identity, for the binding check at the top of test.sh.
#
# Written BEFORE the COPY below, so it names this IMAGE even when the copy is
# served from another step's cache entry. The builder identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's verifier gets baked in at an ordinary-looking
# score. Nothing inside a Dockerfile prevents that; this only reports it.
#
# Written as a per-step LITERAL rather than as an ARG: harbor builds the
# verifier through docker-compose-build.yaml,
# which declares no `args:`, so no --build-arg can reach this build and an
# ARG default would be an override surface nothing can use.
#
# Keep this number, the directory's number and test.sh's check equal.
RUN echo "06" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

# This step's base: upstream at the pin with the ladder's stages removed --
# the same tree the agent starts from. The ladder's baseline is L0 for every
# step, because scoring is per function and scoped to the step: only this
# step's entry points are timed, and for those the earlier stages' kernels are
# irrelevant. Using one base for every step also means a candidate cannot
# influence the denominator.
RUN . /tests/vendor/PIN \
    && git clone --quiet --recurse-submodules --shallow-submodules "$SZ_REPO" /base \
    && cd /base \
    && git checkout --quiet "$SZ_SHA" \
    && test "$(git rev-parse HEAD)" = "$SZ_SHA" \
    && rm -rf .git forkunion/.git \
    && python3 /tests/vendor/strip.py /base cipher intersect memory sort utf8_graphemes utf8_norm utf8_tokens utf8_wordbreaks utf8_runes utf8_linebreaks utf8_sentences utf8_uncased_fold utf8_uncased hash

# The reference harness, compiled here from this step's base headers. The
# library is header-only, so this is the whole build the measurement side needs.
RUN gcc -O2 -Wno-unknown-pragmas -mavx2 -mbmi -mbmi2 -maes -mpclmul -msse4.2 -mlzcnt -mpopcnt \
        -o /baseh /tests/vendor/bench_sz.c -I /base/include

# Record this base tree's OWN ctest failures. The suite's expectations depend on
# which optimized backends are present, so a stripped tree can fail tests that
# have nothing to do with the candidate. Correctness is judged relative to this
# list: no NEW failures.
#
# Narrowed to the project's own C++20 test binary -- the standard its benchmarks
# and this task's kernels are written in. The gate is about collateral damage,
# and the same binary compiles the whole library: building the other three
# standards as well would triple the image build for a gate that already reads
# the candidate's tree through its own headers.  The build is capped at four
# jobs so that it does not starve other containers on a shared host and make
# every timing on the machine, including this task's own, noisy.
RUN cmake -S /base -B /basebld -DCMAKE_BUILD_TYPE=Release -DSTRINGZILLA_BUILD_TEST=ON \
        -DSTRINGZILLA_BUILD_BENCHMARK=OFF -DSTRINGZILLA_BUILD_SHARED=OFF \
        -DSTRINGZILLA_USE_SANITIZERS=OFF \
        -DCMAKE_C_FLAGS="-mavx2 -mbmi -mbmi2 -maes -mpclmul -msse4.2 -mlzcnt -mpopcnt -Wno-error=maybe-uninitialized -Wno-error=uninitialized" \
        -DCMAKE_CXX_FLAGS="-mavx2 -mbmi -mbmi2 -maes -mpclmul -msse4.2 -mlzcnt -mpopcnt -Wno-error=maybe-uninitialized -Wno-error=uninitialized" \
        > /dev/null 2>&1 \
    && cmake --build /basebld --target stringzilla_test_cpp20 -j4 > /dev/null 2>&1 \
    && ctest --test-dir /basebld -R stringzilla_test_cpp20 > /base_ctest.log 2>&1 || true
RUN sed -n '/tests FAILED/,$p' /base_ctest.log \
      | grep -oE '^[[:space:]]+[0-9]+ - [^ ]+' | awk '{print $3}' | sort -u \
      > /base_ctest_failures.txt; \
    echo "base ctest failures: $(wc -l < /base_ctest_failures.txt)"; \
    cat /base_ctest_failures.txt

# The base suite's own inventory, for the test-integrity gate in test.sh.  A
# baseline-relative failure list cannot see a test that was deleted, renamed or
# filtered out, so the list of tests that actually ran is recorded here -- and
# the suite's runner prints exactly one "- <name> ..." line per test it
# executes.  Taken from the binary directly rather than from ctest's log,
# because ctest hides the output of a passing test; the runner's filter knobs
# are unset, so the inventory is what the base's suite really contains.
# Recorded together with the base's own pass/fail, so the gate below is "no new
# failures" here too.  An empty inventory would make the gate vacuous, so the
# build fails instead of shipping one.
RUN env -u SZ_TESTS_FILTER -u SZ_TESTS_MULTIPLIER -u SZ_TESTS_SEED \
        /basebld/stringzilla_test_cpp20 > /base_suite.log 2>&1; \
    echo "$?" > /base_suite_rc; \
    sed -n 's/^- \(.*\) \.\.\.$/\1/p' /base_suite.log | sort -u > /base_suite_inventory.txt; \
    echo "base suite: exit $(cat /base_suite_rc), $(wc -l < /base_suite_inventory.txt) tests executed"; \
    test "$(wc -l < /base_suite_inventory.txt)" -gt 0

WORKDIR /app
