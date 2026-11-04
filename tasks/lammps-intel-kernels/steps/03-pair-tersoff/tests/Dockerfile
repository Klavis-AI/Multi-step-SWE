# Verifier image. One identical Dockerfile for every step; the ONLY thing that
# varies is the step id written below and the per-step data files that carry the
# step number in their name.
#
# Build context is this step's tests/ directory, so nothing here can reach the
# task root.
#
# What it does, in order:
#   1. install the same toolchain and compiler as the agent image (the
#      comparison is only meaningful if both sides are built the same way);
#   2. reconstruct THIS STEP'S pristine base from the pin -- the levels below
#      this step applied, this step and every later one absent -- and build it;
#   3. reconstruct the tree the candidate was handed (/start: the pin with
#      every stage stripped) -- the diff-scope gate's reference;
#   4. generate the scored inputs inside the image, from /tests/vendor;
#   5. record the pristine base's own reference trajectory, which is what the
#      candidate's reference path must reproduce. (The base's own timings are
#      NOT read from here: the numerator guard and the end-to-end baseline are
#      measured fresh in the grading container, beside the candidate's runs --
#      see test.sh. Only the trajectory -- a property of the code, not of the
#      machine -- is taken from the recording.)
#
# The step id is written BEFORE `COPY . /tests` and repeated as a literal in
# test.sh; test.sh refuses to grade unless the two agree and this step's own
# data files are present under their step-numbered names.

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        bash ca-certificates cmake coreutils curl diffutils g++ gawk gcc git \
        gnupg make patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
      | gpg --dearmor -o /usr/share/keyrings/oneapi-archive-keyring.gpg \
    && echo 'deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main' \
         > /etc/apt/sources.list.d/intel-oneapi.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends intel-oneapi-compiler-dpcpp-cpp-2026.1 \
    && rm -rf /var/lib/apt/lists/*

ENV ONEAPI_ROOT=/opt/intel/oneapi/compiler/2026.1
ENV PATH=/opt/intel/oneapi/compiler/2026.1/bin:$PATH
ENV LD_LIBRARY_PATH=/opt/intel/oneapi/compiler/2026.1/lib
ENV CC=icx
ENV CXX=icpx

RUN mkdir -p /app /logs/verifier /base /workload
RUN echo "03-pair-tersoff" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh /tests/vendor/*.sh

# Pristine base for this step: stages still stripped are listed, one per line,
# in BASE_STAGES.stepNN. If that line and the copy above are ever reordered,
# the diagnostic silently reads 0 -- which is what the no-op reads, so it is
# indistinguishable from "the candidate copied nothing".
# The unstripped package is kept in the image for the non-scored verbatim-line
# similarity diagnostic (test.sh reports it; nothing is graded on it). It is
# captured BEFORE the strip below, and it lives only in the verifier image,
# which the candidate never sees. If this and the strip are ever reordered the
# diagnostic reads 0 -- which is what a tree that added nothing reads, so the
# two are indistinguishable and the number must be read with that in mind.
RUN . /tests/vendor/PIN \
    && git clone --quiet "$LMP_REPO" /base \
    && cd /base \
    && git checkout --quiet "$LMP_SHA" \
    && test "$(git rev-parse HEAD)" = "$LMP_SHA" \
    && rm -rf .git \
    && mkdir -p /upstream-src/src \
    && cp -a /base/src/INTEL /upstream-src/src/INTEL \
    && python3 /tests/vendor/strip.py /base $(cat /tests/BASE_STAGES.step03-pair-tersoff) \
    && rm -rf /tests/../.git

# The tree the candidate was handed, and the reference the diff-scope gate
# (test.sh, gate 5) compares the candidate's tree against: the pin with ALL
# five stages stripped, by the same script and the same argument list the agent
# image's Dockerfile uses. It is built here by copying /base -- which is the
# same clone, only with this step's own stages still in it -- and re-running
# strip.py with the full stage list; the all-stages strip of any step's base is
# byte-identical to the agent image's tree (the stage list is the only
# difference between the two strip invocations, and strip.py is idempotent in
# it), so this is the candidate's starting point exactly and not an
# approximation of it. /base itself is NOT that reference: at step k it still
# carries the earlier stages as upstream code.
RUN cp -a /base /start \
    && python3 /tests/vendor/strip.py /start \
         01-pair-lj-cut 02-pair-sw 03-pair-tersoff 04-pair-lj-cut-coul-long 05-pair-airebo

RUN bash /tests/vendor/build_base.sh /base /basebld

# The scored inputs are generated here, inside the verifier, never read from the
# candidate's workspace.
RUN bash /tests/vendor/make_inputs.sh /tests /workload \
    && ls -l /workload

# The pristine base's own reference timings, measured in this image on this
# machine. The candidate's own reference timing is compared against these.
RUN bash /tests/vendor/record_base.sh /basebld /workload /base_times/step03-pair-tersoff/

WORKDIR /app
