# Agent environment for the SMB dialect chain.
#
# Contains: the Node.js runtime the service is written in, Samba's *client*
# tooling (smbclient, smbtorture) so the candidate can drive its own server with
# the same public suite the scorer uses, the Microsoft Open Specification
# documents under /app/SPEC, and under /opt/docs the standards those documents
# cite (RFCs, NIST SP 800-108, The Open Group C706; see docs-manifest.txt).
#
# Deliberately absent: any SMB *server*. `samba-testsuite` + `smbclient` pull in
# samba-common-bin/samba-libs/winbind but not /usr/sbin/smbd, and the build
# asserts that.


FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
         bash ca-certificates coreutils curl diffutils file findutils gawk git \
         gnupg less procps psmisc python3 ripgrep sed tar xz-utils \
         smbclient samba-testsuite \
    && rm -rf /var/lib/apt/lists/* \
    && test ! -e /usr/sbin/smbd \
    && test ! -e /usr/sbin/nmbd \
    && rm -f /usr/sbin/winbindd

# Node.js 20 LTS, pinned by sha256.
ARG NODE_VERSION=20.20.2
ARG NODE_SHA256=df770b2a6f130ed8627c9782c988fda9669fa23898329a61a871e32f965e007d
RUN curl -fsSLo /tmp/node.tar.xz \
      "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-x64.tar.xz" \
    && echo "${NODE_SHA256}  /tmp/node.tar.xz" | sha256sum -c - \
    && tar -xJf /tmp/node.tar.xz -C /usr/local --strip-components=1 \
         --exclude CHANGELOG.md --exclude LICENSE --exclude README.md \
    && rm -f /tmp/node.tar.xz \
    && node --version && npm --version

WORKDIR /app

# Normative documents, unmodified vendor text. See SPEC/PROVENANCE.md.
COPY spec /app/SPEC

COPY WORKSPACE_HANDOFF.md /app/WORKSPACE_HANDOFF.md

# Keep this script and every [steps.agent].timeout_sec in task.toml in sync.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# Everything under /app is collected at the end of every milestone and handed to
# the next one. The collection path decodes the stream as UTF-8 with
# errors="replace" before the tar is parsed, so a byte >= 0x80 in a file that
# ships here would reach the next milestone destroyed. Fail the build instead.
RUN ! LC_ALL=C grep -rlP '[\x80-\xFF]' /app 2>/dev/null | grep . \
    || (echo "non-ASCII bytes in a file that ships under /app; run asciify.py" \
        && LC_ALL=C grep -rlP '[\x80-\xFF]' /app && exit 1)

# Documentation for the agent, which works with no network: the specs and
# manuals the instructions cite, stored in environment/docs/. They were
# downloaded once from their official sources and checked against pinned
# sha256 sums; docs-provenance.txt records the URLs, sums and conversions.
# /opt/docs is outside /app because /app is the workspace collected for
# grading.
COPY docs/ /opt/docs/
