# Verifier image for one step, built with that step's tests/ as the Docker
# build context. It reconstructs THIS STEP'S BASE from the pinned upstream
# commit, builds it, runs the project's own suites on it to record which of
# them the base already fails, and keeps it as the noop reference: the speedup
# denominator is measured here, in the same container and on the same pinned
# cores as the candidate, so the ratio is self-calibrating and does not depend
# on how fast the grading machine is.
#
# This file is byte-identical for every step. What differs is
# tests/STEP.step<N> -- one file per step, whose *name* carries the step
# number, so BuildKit's (path, size, mtime) context dedup cannot serve a
# sibling's copy of it.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils diffutils gawk gcc git libc6-dev make patch \
       procps \
       python3 util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

COPY . /tests
RUN chmod +x /tests/test.sh /tests/vendor/harness.sh

# Step identity, derived from the name of this step's data file rather than
# written as a literal, so this Dockerfile stays identical across steps and
# nothing can be baked in from a sibling context. The check at the top of
# test.sh compares it against the same file's `step=` field.
RUN set -e; \
    n=$(ls /tests/STEP.step* | wc -l); test "$n" = 1; \
    f=$(ls /tests/STEP.step*); \
    id=${f#/tests/STEP.step}; \
    echo "$id" > /etc/harbor-step-id; \
    echo "verifier image for step $id (base level $(sed -n 's/^level=//p' "$f"))"

# This step's base tree: upstream at the pin, minus the stages this step and
# every later step must implement.
RUN . /tests/vendor/PIN \
    && git clone --quiet --depth 1 --branch "$MLK_TAG" "$MLK_REPO" /base \
    && cd /base \
    && test "$(git rev-parse HEAD)" = "$MLK_SHA" \
    && rm -rf .git \
    && python3 /tests/vendor/strip.py /base "$(sed -n 's/^level=//p' /tests/STEP.step*)"

RUN cd /base && make lib OPT=1 AUTO=1 -j4 BUILD_DIR=/basebld > /dev/null

# The base's own suite failures. A stripped tree can fail some of upstream's
# expectations by itself, so correctness is judged relative to this list: no
# NEW failures. The base harnesses are built here too, so a harness that does
# not compile fails the image build rather than a grading run.
RUN : > /base_suite_failures.txt; \
    for t in run_func_512 run_func_768 run_func_1024 \
             run_kat_512 run_kat_768 run_kat_1024; do \
        ( cd /base && make "$t" OPT=1 AUTO=1 -j4 BUILD_DIR=/basebld ) \
            > "/tmp/$t.log" 2>&1 || echo "$t" >> /base_suite_failures.txt; \
    done; \
    echo "base suite failures: $(wc -l < /base_suite_failures.txt)"; \
    cat /base_suite_failures.txt

RUN for s in 512 768 1024; do \
        bash /tests/vendor/harness.sh /base /basebld "$s" "/base_harness_$s" \
            || exit 1; \
    done

WORKDIR /app
