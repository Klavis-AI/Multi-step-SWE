# Agent environment for openscenario-engine-multiversion.
#
# The product is a C++17 CMake project. The toolchain is pinned by the base
# image; nothing here is fetched from a moving tag. The verifier images
# (steps/NN-*/tests/Dockerfile) install the same compiler and CMake, so a build
# that works here works there.
FROM debian:bookworm-slim

# `cmake`, `g++` and `make` are the build. `git` is needed because the step
# solutions ship as patches and because esmini's own CMake shells out to it.
# `python3` is used by upstream's helper scripts under scripts/ and by odrplot.
# The rest is ordinary developer tooling the agent is expected to have.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash build-essential ca-certificates cmake coreutils diffutils file \
       gawk git jq less make patch pkg-config procps python3 python3-venv \
       ripgrep unzip xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Third-party sources that upstream carries as git submodules, materialised at
# the exact commits upstream pins so no build in this task needs the network.
# They are NOT part of the workspace and are NOT graded: copy from here into
# `externals/` if and when the project you are building needs them.
#
#   fmt     MIT, https://github.com/fmtlib/fmt      @ 40626af88bd7df9a5fb80be7b25ac85b122d6c21
#   spdlog  MIT, https://github.com/gabime/spdlog   @ e593f6695c6065e6b345fe2862f04a519ed484e0
COPY vendor /opt/esmini-vendor
RUN chmod -R a+rX /opt/esmini-vendor

WORKDIR /app

# Agent-visible, informational countdown for the per-step timeout configured in
# task.toml. Harbor remains responsible for enforcing the hard limit.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
