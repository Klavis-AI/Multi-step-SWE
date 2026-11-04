FROM debian:bookworm-slim

# Toolchain for a three-width, JIT-free PCRE2 build (gcc, make, ar, libc
# headers), plus the ordinary things an agent needs to navigate a C tree.
# cmake and autotools are deliberately absent: the workspace builds with the
# task's own /app/Makefile, which is stable across all thirteen milestones,
# and pulling upstream's build system into the milestone would make the
# candidate maintain CMakeLists.txt instead of the engine.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        bash \
        binutils \
        ca-certificates \
        curl \
        diffutils \
        file \
        gawk \
        gcc \
        gdb \
        git \
        libc6-dev \
        make \
        patch \
        procps \
        python3 \
        ripgrep \
        vim-tiny \
    && rm -rf /var/lib/apt/lists/*

COPY timer.sh /usr/local/bin/timer.sh
RUN chmod +x /usr/local/bin/timer.sh

WORKDIR /app
