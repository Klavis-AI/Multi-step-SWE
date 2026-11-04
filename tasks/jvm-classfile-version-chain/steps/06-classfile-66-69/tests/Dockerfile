# Verifier image for the class-file 66-69 milestone.
#
# There is deliberately no JDK and no `java` on PATH here: the artifact is built
# from source in this image and must run the corpus on its own.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils diffutils gawk gcc g++ make \
        libc6-dev libffi-dev libz-dev pkg-config python3 binutils valgrind \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier /decoy /var/lib/jvm-decoy \
    && touch /var/lib/jvm-decoy/log \
    && chmod 0755 /var/lib/jvm-decoy && chmod 0622 /var/lib/jvm-decoy/log

# A decoy launcher: if the artifact shells out to another JVM, the marker shows.
# The marker is root's, in a directory only root can change: the candidate uid
# can append to it but not remove or replace it (test.sh compares its ctime).
RUN printf '#!/bin/sh\necho "EXTERNAL-JVM-INVOKED $0" >> /var/lib/jvm-decoy/log\nexit 42\n' > /decoy/java \
    && for t in javac jshell java_vm python3 python perl node ruby; do cp /decoy/java /decoy/$t; done \
    && chmod +x /decoy/*

COPY . /tests
# The answer key is root's.  Git records only the executable bit, so the modes
# a checkout produces are not the modes this image should trust.
RUN chmod +x /tests/test.sh /tests/*.sh 2>/dev/null; chmod -R go-rwx /tests

WORKDIR /app

# Size pad: these six verifier Dockerfiles differ only in the milestone named
# on line 1, so they are the same length and would collide in a build context
# keyed on (path, size, mtime).  One dash per rung: ------
