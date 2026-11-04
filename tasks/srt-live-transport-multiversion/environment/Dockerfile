# Agent environment. The candidate implements a C/C++ CMake project at /app, so
# the image carries the same toolchain the verifier uses: the library the agent
# builds here is the library the verifier will configure, build and install.
#
# ubuntu:20.04 rather than a newer base, and this is load-bearing. The Oracle
# reconstructs each Haivision/srt release byte for byte, and `v1.4.2` does not
# compile under GCC 11 -- it relied on <limits> arriving transitively through
# another libstdc++ header, which GCC 11 stopped supplying and which upstream
# only added explicitly in v1.4.3. GCC 9 compiles every release in the chain.
FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends \
       bash build-essential ca-certificates cmake coreutils curl diffutils file \
       gawk git grep jq less pkg-config procps python3 sed tar tclsh vim-tiny \
       libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Keep the script and every step's timeout_sec synchronized. The script is an
# agent-visible countdown; Harbor enforces the actual timeout.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
