# Verifier image. Independent of the agent image: grading rebuilds the
# candidate's toolchain from source with its own C compiler and make.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash binutils ca-certificates coreutils diffutils file findutils \
        gcc make libc6-dev python3 sed gawk grep tar \
    && rm -rf /var/lib/apt/lists/*

# The candidate's toolchain must build from source using only a C compiler,
# make, ar and POSIX shell utilities. This directory is the PATH used for that
# build, so nothing else is reachable.
RUN mkdir -p /tests/toolpath \
    && for t in sh bash cc gcc cpp as ld ar ranlib nm strip objcopy make \
                env rm mkdir rmdir cp mv ln install echo printf test true false \
                pwd ls find xargs dirname basename cat head tail tr sort uniq wc \
                sed awk grep egrep fgrep chmod touch date expr uname nproc which \
                sleep tee od cmp diff; do \
         p=$(command -v $t 2>/dev/null || true); \
         if [ -n "$p" ]; then ln -sf "$p" /tests/toolpath/$t; fi; \
       done

RUN mkdir -p /app /logs/verifier

COPY . /tests
RUN chmod +x /tests/test.sh

# The recorded expectations sit on the same filesystem as the toolchain
# being graded. A toolchain that can read them does not have to implement
# Go -- it looks up the case it was handed and prints the answer. So they
# move to a directory only root can open, and runner.py drops every case
# subprocess to an unprivileged user. Either half alone leaves the file
# reachable. /tests itself stays readable: a candidate toolchain may need
# /tests/toolpath to link.
RUN useradd -r -u 65533 -M -d /nonexistent -s /usr/sbin/nologin gocase \
    || true
RUN mkdir -p /grading \
    && mv /tests/cases.json /grading/cases.json \
    && chmod 700 /grading \
    && chmod 600 /grading/cases.json

WORKDIR /app
