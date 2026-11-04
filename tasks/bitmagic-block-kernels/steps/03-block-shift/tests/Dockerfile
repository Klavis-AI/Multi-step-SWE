# Verifier image for 03-block-shift -- Whole-block left and right shift, and the fused shift-and-mask.
#
# Built with that step's tests/ as the Docker build context.  It reconstructs
# the LADDER'S BASE -- BitMagic v9.3.2 with all ten kernel families the ladder
# measures or excludes removed from every backend header -- builds it, keeps it
# as the speedup denominator.  The denominator is therefore measured here, in
# the same container and on the same pinned cores as the candidate, and no
# candidate can influence it.
#
# This file is identical for every step apart from this header and the step-id
# literal below.  The step number also appears in the NAME of every per-step
# file this context carries (FUNCS.stepNN, STEPID.stepNN,
# bench_kernels.stepNN.cpp), which is what makes a context mis-binding
# impossible: the builder keys its content check on (relative path, size,
# mtime), and no two steps share a path.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates cmake coreutils diffutils g++ gcc gawk git make \
       patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

# Step identity, for the binding check at the top of test.sh.  Written BEFORE
# the COPY below, so it names this IMAGE even when the copy is served from
# another step's cache entry.  A `cp` from /tests would not do: it would then
# name the CONTEXT, and a mis-bound build would be self-consistent.
RUN echo "03" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh /tests/check_isa.sh

# This step's base: the pin, minus every kernel family the ladder measures or
# excludes.  The excluded families are stripped too because they are written
# in the same idiom in the same headers, and leaving them would hand the
# candidate a worked example of the file it has to write.
RUN . /tests/vendor/PIN \
    && git clone --quiet "$BM_REPO" /base \
    && cd /base \
    && git checkout --quiet "$BM_SHA" \
    && test "$(git rev-parse HEAD)" = "$BM_SHA" \
    && rm -rf .git \
    && python3 /tests/vendor/strip.py /base $(cat /tests/ALL_GROUPS)

# The envelope both sides are built with: upstream's own BMAVX2OPT preset flags
# (`-march=skylake -mavx2 -O2`) plus the macro that selects the AVX2 backend.
# The macro selects CODE and the flags enable the INSTRUCTIONS; docs/build.md
# says so explicitly.  This is also why no candidate can widen the instruction
# set: the checks compile every harness themselves, with their own flags.
RUN g++ -std=c++17 -O2 -DNDEBUG -march=skylake -mavx2 -mpopcnt -DBMAVX2OPT \
        -I /base/src -o /base_harness $(ls /tests/bench_kernels.step*.cpp) \
    && g++ -std=c++17 -O2 -DNDEBUG -march=skylake -mavx2 -mpopcnt -DBMAVX2OPT \
        -I /base/src -o /base_e2e /tests/e2e_bench.cpp

# Why the project's own acceptance suite is not run here.  tests/stress/t.cpp
# is a long randomised soak test: every selectable group (-ll, -bvb, -bvo, -bvs,
# -bvser) exceeds a 240 s cap without completing, and the full run sits for
# more than half an hour inside Test bit_find_first_diff(), which exercises the
# `search` kernels this ladder
# strips out of every level.  A capped run never reaches the block tests it
# would be guarding, and a check that examines nothing is worse than no check.
# The collateral guard is the composite end-to-end driver built above (public
# bvector API, aggregator, representation-sensitive paths, serialization) plus
# the per-entry byte-identical comparison in test.sh.

WORKDIR /app

