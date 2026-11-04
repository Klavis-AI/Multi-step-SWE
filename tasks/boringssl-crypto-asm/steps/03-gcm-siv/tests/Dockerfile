# Verifier image for one milestone, built with that milestone's tests/ as the
# Docker build context.
#
# It reconstructs THIS MILESTONE'S BASE from the pinned upstream commit and
# builds it, and keeps that build as the reference: the speedup denominator is
# measured here, in the same container and on the same pinned core as the
# candidate, so the ratio is self-calibrating and does not depend on how fast
# the grading machine is.
#
# It also reconstructs the LADDER'S FLOOR (L0) -- the same pin with every
# stage removed, tests/STRIP_STAGES.all, a shared file byte-identical in every
# milestone's context -- and builds it into /l0bld. The non-regression guard in
# test.sh is baselined on that floor, not on this milestone's base; the guard's
# own comment gives the reason, which is that a no-op agent's tree is L0 at
# every step and a guard baselined on the step's own base reports a tree that
# changed nothing as catastrophically slow.
#
# This file is identical for every milestone except for the two literals below
# and the stage list in the comment above them. What differs is
# tests/STRIP_STAGES.step<id>, tests/FUNCS.step<id> and tests/REF_FILES.step<id>,
# and every per-milestone file carries the milestone's number in its NAME.
# BuildKit identifies a build-context entry by (path, size, mtime) and never
# reads its bytes, so two milestones whose contexts share a filename, a size and
# a second are indistinguishable to it -- and the wrong milestone's expectations
# get baked in at an ordinary-looking score. Distinct names cannot collide, and
# the binding check at the top of test.sh reports the case where the Dockerfile
# itself came from the wrong context.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash binutils ca-certificates cmake coreutils diffutils g++ gcc git \
       make ninja-build patch procps python3 rsync util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier /ref

# Step identity, for the binding check at the top of test.sh. Written as a
# LITERAL, substituted when this file is generated, rather than as an ARG:
# harbor builds the verifier through docker-compose-build.yaml, which declares no
# `args:`, so no --build-arg can reach this build and an ARG default would be an
# override surface nothing can use.
RUN echo "03" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

# This milestone's base: the pin, minus the stages this milestone and every
# later one must implement --
#   gcm_siv
#   bn_mont
#   p256

#
# /src is the tree a candidate's work is copied onto and rebuilt in. It is a
# hardlink farm of the base, so it costs no image space; the copy replaces the
# links of files whose contents differ, which is exactly what makes the rebuild
# incremental instead of a second full build of the library.
RUN . /tests/vendor/PIN \
    && git clone --quiet "$BORINGSSL_REPO" /base \
    && cd /base \
    && git checkout --quiet "$BORINGSSL_SHA" \
    && test "$(git rev-parse HEAD)" = "$BORINGSSL_SHA" \
    && rm -rf .git \
    && python3 /tests/vendor/code_lines.py --index /base /tests/REF_FILES.step03 /upstream_code_lines.step03 \
    && tar -C /base -cf /tmp/ref.tar -T /tests/REF_FILES.all \
    && tar -C /ref -xf /tmp/ref.tar \
    && rm -f /tmp/ref.tar \
    && python3 /tests/vendor/strip.py /base $(cat /tests/STRIP_STAGES.step03) \
    && cp -al /base /src

# The line above indexes upstream's own copy of every file this milestone's
# reference restores -- REF_FILES.step03 -- as sorted "<path> <sha1 of the
# normalised line>" lines. The verifier's non-scored similarity diagnostic reads
# it to answer "of the code lines the candidate added, what fraction appears
# verbatim in upstream's version of the same file".
#
# It is inside the clone RUN, ahead of the stripper, because this is the only
# moment in the image's life when /base is upstream: after strip.py it is the
# stripped tree, and /src is a hardlink farm of it. That ordering is the whole
# instrument. Built from the stripped tree the index holds the lines that are
# still present, while a transcriber's added lines are precisely the ones that
# were removed -- so the oracle itself would read 0.0, and so would everyone.
# It was written as a separate RUN after this one first, which is exactly that
# defect: layer caching means a later RUN starts from the previous RUN's result.
# Being a step of this chain, executed by the same shell, is what makes the
# order structural rather than a comment.
#
# A candidate's added lines in a file outside REF_FILES have no upstream
# counterpart to match and are counted as novel.
#
# The same RUN also keeps upstream's own copy of every file ANY reference in
# this ladder restores -- the union, REF_FILES.all -- under /ref. That is the
# capability-envelope check's instrument: a changed file is exempt from the
# out-of-envelope instruction scan exactly while it is byte-identical to the
# copy here, because then it is the reference's own code rather than the
# candidate's. It has to be taken before strip.py, in this chain, for the same
# reason the similarity index does: after the strip those files are gone, and a
# later RUN would start from the stripped tree. The copy is a real copy (tar),
# not a hardlink farm like /src, because strip.py rewrites /base in place.

# -j4, not -j$(nproc): the host is shared with other grading containers and a
# 16-wide build from one of them starves the others.
RUN cmake -S /src -B /basebld -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null \
    && cmake --build /basebld -j4 --target crypto crypto_test > /dev/null

# The ladder's L0 floor: the pin with EVERY stage removed (tests/STRIP_STAGES.all),
# built here because test.sh's end-to-end non-regression guard is baselined on
# it -- see the guard's comment there for why the baseline must be the ladder's
# floor and not this milestone's own base (a no-op agent's tree is L0 at every
# step; a guard baselined on the step's own base zeroes it).
#
# Step 01's own base IS the floor (its stage list is the whole ladder), so there
# the clone is skipped and /l0bld is a hardlink farm of the base build already
# made above. cmp compares content, so the test does not depend on the names.
RUN if cmp -s /tests/STRIP_STAGES.all "/tests/STRIP_STAGES.step$(cat /etc/harbor-step-id)"; then \
      cp -al /base /l0 \
      && cp -al /basebld /l0bld \
      && echo "L0 floor == this milestone's base; /l0bld hardlinks /basebld"; \
    else \
      . /tests/vendor/PIN \
      && git clone --quiet "$BORINGSSL_REPO" /l0 \
      && cd /l0 \
      && git checkout --quiet "$BORINGSSL_SHA" \
      && test "$(git rev-parse HEAD)" = "$BORINGSSL_SHA" \
      && rm -rf .git \
      && python3 /tests/vendor/strip.py /l0 $(cat /tests/STRIP_STAGES.all) \
      && cmake -S /l0 -B /l0bld -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null \
      && cmake --build /l0bld -j4 --target crypto crypto_test > /dev/null \
      && echo "L0 floor built into /l0bld"; \
    fi

# The floor's own suite run, for the same reason as its build: test.sh's "no
# test the floor ran has been removed" check is baselined on the floor, not on
# this milestone's base (each stage's restore widens an ABI-test guard the
# stripper had narrowed, so the levels run more tests the higher they are --
# measured, 126 to 131 from L0 up). Recorded with the same filter and the same
# procedure the base's list is recorded with, so the two are comparable.
RUN cd /l0 && /l0bld/crypto_test --gtest_filter="$(cat /tests/SUITE_FILTER)" \
      > /l0_crypto_test.log 2>&1 || true; \
    grep -oE '^\[ RUN[ ]+\] [A-Za-z0-9_./]+' /l0_crypto_test.log \
      | awk '{print $4}' | sort -u > /l0_crypto_test_ran.txt; \
    echo "L0 floor suite: $(wc -l < /l0_crypto_test_ran.txt) tests ran"

# Record this base tree's OWN failures in the project's suite. Some tests depend
# on which optimized kernels are present, so a stripped tree can fail tests that
# have nothing to do with the candidate. Correctness is judged relative to this
# list: no NEW failures.
RUN cd /base && /basebld/crypto_test --gtest_filter="$(cat /tests/SUITE_FILTER)" \
      > /base_crypto_test.log 2>&1 || true; \
    grep -oE '^\[  FAILED  \] [A-Za-z0-9_.]+' /base_crypto_test.log \
      | awk '{print $4}' | sort -u > /base_crypto_test_failures.txt; \
    grep -oE '^\[ RUN[ ]+\] [A-Za-z0-9_./]+' /base_crypto_test.log \
      | awk '{print $4}' | sort -u > /base_crypto_test_ran.txt; \
    echo "base suite failures: $(wc -l < /base_crypto_test_failures.txt)"; \
    cat /base_crypto_test_failures.txt; \
    echo "base tests that ran: $(wc -l < /base_crypto_test_ran.txt)"

WORKDIR /app
