FROM debian:bookworm-slim

# Toolchain for simdutf and for the per-function measurement harness.  simdutf
# is a single translation unit that dispatches at runtime, so no assembler is
# needed; cmake and a C++17 compiler are the whole of it.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash bc ca-certificates cmake coreutils diffutils g++ gcc gawk git make \
       patch procps python3 ripgrep time util-linux \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY timer.sh /app/timer.sh
COPY vendor/ /tmp/vendor/
RUN chmod +x /app/timer.sh

# The starting tree: upstream at the pinned commit with the SIMD implementations
# of all seven operation families routed at the portable reference, and the
# non-x86 back-ends removed.  The library is complete and correct -- it builds,
# passes its own 90-test suite, and transcodes everything correctly through the
# scalar code; it is simply slow in the places whose optimized code is missing.
#
# strip.py lists every family of the ladder and the file set each owns, so it
# must not reach the candidate: it is deleted with the rest of /tmp/vendor at
# the bottom of this file, after the trees and the tools are built.
RUN . /tmp/vendor/PIN \
    && git clone --quiet "$SIMDUTF_REPO" /app/simdutf \
    && cd /app/simdutf \
    && git checkout --quiet "$SIMDUTF_SHA" \
    && test "$(git rev-parse HEAD)" = "$SIMDUTF_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /app/simdutf \
         utf8_to_utf16_and_utf32 utf16_to_utf8 utf32_to_utf8 \
         utf16_utf32 latin1 base64 validate_misc \
    && python3 /tmp/vendor/relativize-fastbase64-symlink.py /app/simdutf

# One upstream build rule plants an ABSOLUTE symlink in EVERY CMake build tree
# of this source (`tools/fastbase64.coreutils -> <absolute>/tools/fastbase64`),
# and the harness's /app workspace snapshot extracts the tree host-side with
# Python's tarfile "data" filter, which aborts on an absolute link
# (`tarfile.AbsoluteLinkError`) -- for whichever build directory the candidate
# happened to use, so no artifact `exclude` list can cover it (three graded
# runs chose `tools/bld`, `m7/bld_base` and `dev/base_bld`; the last two abort
# the snapshot, the step then grades `missing /app/simdutf` -> correctness 0).
# The line above rewrites that rule to the relative target upstream's own
# install-time rule already uses, so no build tree under /app can contain an
# absolute symlink at all.  The rule itself is in tools/CMakeLists.txt, which
# no oracle patch touches, so the candidate's tree is consistent at every
# level.  See task.toml's artifacts stanza for the (now size-only) exclude
# list and docs/optimization/taskgen/taskgen-instruction.md §10.

# A sample workload, drawn from the same distribution the grader draws from but
# with a different seed.  The distribution is public -- the generator itself is
# shipped beside it -- and the draw is not: the grader regenerates its corpus
# inside its own container from a seed this container never sees, so a kernel
# tuned to these exact bytes has tuned to nothing.
RUN mkdir -p /app/workload /app/tools \
    && python3 /tmp/vendor/make_corpus.py /app/workload 4711 > /app/workload/README.txt

# The measuring tool and the corpus generator, shipped as source rather than
# hidden, deliberately: the transcoding contract is total, so there is no stable
# secret a compiled binary would protect, and an agent that cannot reproduce the
# measurement -- or draw more input of the same shape -- is guessing at both
# correctness and progress.  What the grader draws is still its own.
COPY vendor/bench_stage.cpp /app/tools/bench_stage.cpp
COPY vendor/make_corpus.py /app/tools/make_corpus.py

# Two build directories, on purpose: /app/build belongs to the project's test
# suite, and the timer links the library from its own tree under /app/tools/bld.
# Pointing the timer at /app/build would reconfigure that directory with the
# tests off and silently turn the documented ctest behaviour check into a no-op.
RUN printf '%s\n' \
    '#!/usr/bin/env bash' \
    '# Build the per-function timer against the tree in /app/simdutf.' \
    '# Re-run this after every rebuild: the harness links the library, so a' \
    '# stale binary measures the tree as it used to be.' \
    'set -eu' \
    'cmake -S /app/simdutf -B /app/tools/bld -DCMAKE_BUILD_TYPE=Release \' \
    '      -DSIMDUTF_TESTS=OFF -DSIMDUTF_BENCHMARKS=OFF \' \
    '      -DSIMDUTF_ALWAYS_INCLUDE_FALLBACK=ON' \
    'cmake --build /app/tools/bld -j4' \
    'g++ -O2 -std=c++17 -o /app/tools/measure /app/tools/bench_stage.cpp \' \
    '    -I /app/simdutf/include -I /app/simdutf/src /app/tools/bld/src/libsimdutf.a' \
    'echo "built /app/tools/measure"' \
    > /app/tools/build-measure.sh \
    && chmod +x /app/tools/build-measure.sh

# Nothing here reaches the candidate: the pinned revision, the ladder's stage
# list and the stripper that produces the starting tree.
RUN rm -rf /tmp/vendor
