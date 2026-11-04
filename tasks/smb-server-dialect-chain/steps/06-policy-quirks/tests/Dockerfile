# Verifier image for one milestone of the SMB dialect chain. Built with this step's tests/ as
# the Docker build context, so it cannot reach the task root: every shared
# helper is copied in per step and the copies are kept in sync.
#
# It carries Samba's *client* tooling (smbclient, smbtorture) and the Node.js
# runtime the candidate's service needs, and deliberately no SMB server: the
# build asserts that /usr/sbin/smbd does not exist, so a candidate that shells
# out to a real smbd cannot be graded as if it had written one.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
         bash ca-certificates coreutils curl diffutils file findutils gawk \
         procps psmisc python3 sed tar xz-utils \
         smbclient samba-testsuite \
    && rm -rf /var/lib/apt/lists/* \
    && test ! -e /usr/sbin/smbd \
    && test ! -e /usr/sbin/nmbd \
    && rm -f /usr/sbin/winbindd

ARG NODE_VERSION=20.20.2
ARG NODE_SHA256=df770b2a6f130ed8627c9782c988fda9669fa23898329a61a871e32f965e007d
RUN curl -fsSLo /tmp/node.tar.xz \
      "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-x64.tar.xz" \
    && echo "${NODE_SHA256}  /tmp/node.tar.xz" | sha256sum -c - \
    && tar -xJf /tmp/node.tar.xz -C /usr/local --strip-components=1 \
         --exclude CHANGELOG.md --exclude LICENSE --exclude README.md \
    && rm -f /tmp/node.tar.xz \
    && node --version

RUN mkdir -p /app /logs/verifier /scratch

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app
