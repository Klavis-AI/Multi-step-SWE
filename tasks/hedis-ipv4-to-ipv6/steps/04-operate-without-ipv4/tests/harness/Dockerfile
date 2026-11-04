# Verifier image. Built with this step's tests/ directory as the Docker build
# context, so it can reach nothing outside it; sync.sh keeps every step's copy
# identical.
#
# Nothing is inherited from the agent image: the verifier installs its own
# toolchain so a candidate cannot change how it is graded.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash \
        binutils \
        build-essential \
        ca-certificates \
        clang \
        coreutils \
        diffutils \
        file \
        gawk \
        libclang-14-dev \
        libseccomp-dev \
        make \
        procps \
        python3 \
        python3-clang-14 \
        strace \
        tcl \
        tcpdump \
        util-linux \
    && rm -rf /var/lib/apt/lists/*

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time.
RUN mkdir -p /app /logs/verifier

COPY . /tests

# The no-IPv4 launcher: a seccomp filter that fails socket(AF_INET, ...) with
# EAFNOSUPPORT under PR_SET_NO_NEW_PRIVS, so a process tree started through it
# runs as if the kernel had no IPv4 at all. Built here, from the verifier's own
# source, never from anything the candidate supplies.
RUN gcc -O2 -o /usr/local/bin/no-ipv4 /tests/tools/no_ipv4.c -lseccomp \
    && chmod 755 /usr/local/bin/no-ipv4

RUN chmod +x /tests/test.sh /tests/tools/*.sh 2>/dev/null || true

WORKDIR /app
