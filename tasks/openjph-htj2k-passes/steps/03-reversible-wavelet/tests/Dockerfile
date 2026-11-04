# Verifier image for step 03-reversible-wavelet, built with that step's tests/ as the Docker build
# context.
#
# It reconstructs THIS STEP'S BASE from the pinned upstream commit, builds it,
# and keeps it as the reference: the speedup denominator and the correctness
# reference are measured here, in the same container and on the same cores as
# the candidate, so the ratio is self-calibrating and does not depend on how
# fast the grading machine is.
#
# This file is identical for every step except for the step number and the
# step's own implementation file checked below.  What differs between steps is
# the per-step data files, and those carry the step number in their NAME
# (BASE_STAGES.step01, FUNCS.step01, ISA.step01)
# so that BuildKit -- which dedups context entries keyed on (path, size, mtime)
# and never reads the bytes -- cannot serve one step's file from a sibling's
# cache entry.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates cmake coreutils diffutils g++ gcc gawk git make \
       patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier /workload /opt/ojph-deps

# Step identity, for the binding check at the top of test.sh.  Written BEFORE
# the COPY below, so it names this IMAGE even when the copy was served from
# another step's cache entry.
#
# Written as a LITERAL rather than as an ARG: harbor builds the verifier
# through docker-compose-build.yaml, which declares no `args:`, so no
# --build-arg can reach this build.
RUN echo "03" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh /tests/*.sh

# Bind the step identity to the bytes of this step's data files.  The names
# already carry the step number, so a file cannot be served from a sibling's
# context entry; this makes a substitution that gets the name right and the
# content wrong a loud failure rather than a plausible number.
RUN cat /tests/FUNCS.step03 /tests/ISA.step03 /tests/BASE_STAGES.step03 \
        /tests/vendor/oracle_lines.step03 2>/dev/null | sha256sum | cut -d" " -f1 \
        > /etc/harbor-tests-digest

# This step's base tree: upstream at the pin, minus the stages this step and
# every later step must implement.  BASE_STAGES.step03 names them.
RUN . /tests/vendor/PIN \
    && git clone --quiet --depth 1 --branch "$OJPH_TAG" "$OJPH_REPO" /base \
    && cd /base \
    && git checkout --quiet "$OJPH_SHA" \
    && test "$(git rev-parse HEAD)" = "$OJPH_SHA" \
    && rm -rf .git \
    && STEP=$(cat /etc/harbor-step-id) \
    && STAGES=$(cat "/tests/BASE_STAGES.step$STEP") \
    && test -n "$STAGES" \
    && python3 /tests/vendor/strip.py /base $STAGES \
    && test ! -e "/base/src/core/transform/ojph_transform_avx2.cpp" \
    && echo "base tree drops this step's own implementation: src/core/transform/ojph_transform_avx2.cpp gone" 

RUN cmake -S /base -B /basebld -DCMAKE_BUILD_TYPE=Release > /dev/null \
    && cmake --build /basebld -j"$(nproc)" > /dev/null

# The ladder's floor: upstream with every stage removed.  The end-to-end
# non-regression gate is baselined HERE, not on this step's base -- a no-op
# agent's tree is the floor at every step, so a guard baselined on the step's
# own base would report an idle agent as catastrophically slow.
RUN . /tests/vendor/PIN \
    && git clone --quiet --depth 1 --branch "$OJPH_TAG" "$OJPH_REPO" /zero \
    && cd /zero \
    && git checkout --quiet "$OJPH_SHA" \
    && rm -rf .git \
    && python3 /tests/vendor/strip.py /zero \
         blk_decode blk_encode rev_wavelet irv_wavelet colour codestream

RUN cmake -S /zero -B /zerobld -DCMAKE_BUILD_TYPE=Release > /dev/null \
    && cmake --build /zerobld -j"$(nproc)" > /dev/null

# Held-out end-to-end workload: a DIFFERENT draw from the sample the candidate
# has in /app, so a tree tuned to the visible image is not scored on it.  The
# two codestreams are produced by THIS step's base build, so the decode side has
# an input the candidate never encoded.
RUN python3 /tests/vendor/make_workload.py /workload/heldout.ppm 768 512 771117 \
    && /basebld/src/apps/ojph_compress/ojph_compress -i /workload/heldout.ppm \
         -o /workload/heldout_rev.j2c -reversible true -num_decomps 5 \
    && /basebld/src/apps/ojph_compress/ojph_compress -i /workload/heldout.ppm \
         -o /workload/heldout_irv.j2c -reversible false -num_decomps 5 -qstep 0.02 \
    && ls -la /workload

# Held-out codeblock fixtures, generated INSIDE this image from this step's own
# base build -- never read from /app, which the candidate controlled for two
# hours and must not be allowed to choose the measurement's input.
RUN g++ -O2 -std=c++14 -o /tmp/gen_fixtures /tests/vendor/gen_fixtures.cpp \
        -I /base/src/core/openjph -I /base/src/core/codestream \
        -I /base/src/core/transform -I /base/src/core/coding \
        -I /basebld/src/core -L /basebld/src/core -lopenjph \
        -Wl,-rpath,/basebld/src/core \
    && /tmp/gen_fixtures /workload \
    && rm -f /tmp/gen_fixtures \
    && ls /workload | head -20

# Record this base tree's OWN upstream-suite failures.  The suite's expectations
# depend on which optimized kernels are present, so a stripped tree can fail
# tests that have nothing to do with the candidate.  Correctness is judged
# relative to this list: no NEW failures.
#
# FETCHCONTENT_BASE_DIR is shared with the candidate's test build in test.sh so
# googletest and the codestream corpus are downloaded once, at image build.
RUN cmake -S /base -B /basetchk -DCMAKE_BUILD_TYPE=Release \
        -DOJPH_BUILD_TESTS=ON -DFETCHCONTENT_BASE_DIR=/opt/ojph-deps \
        > /tmp/base_testcfg.log 2>&1 \
    && cmake --build /basetchk -j"$(nproc)" > /tmp/base_testbuild.log 2>&1 \
    && (ctest --test-dir /basetchk > /base_ctest.log 2>&1 || true) \
    && sed -n '/tests FAILED/,$p' /base_ctest.log \
         | grep -oE '^[[:space:]]+[0-9]+ - [^ ]+' | awk '{print $3}' | sort -u \
         > /base_ctest_failures.txt \
    && echo "base ctest failures: $(wc -l < /base_ctest_failures.txt)" \
    && cat /base_ctest_failures.txt

WORKDIR /app
