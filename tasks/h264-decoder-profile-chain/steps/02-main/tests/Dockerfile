# Verifier image for one step of the H.264 decoder chain.
#
# Node and nothing else: no ffmpeg, no libav, no gcc, no JM, no python. The
# candidate is JavaScript and is run as-is, so the verifier needs no toolchain
# the candidate could have tampered with -- and there is nothing here for a
# candidate's decoder to shell out to.
# PINNED BY DIGEST. Every property the capability layer relies on is a Node
# INTERNALS property -- undici's llhttp shape, the CJS named-export lexer, the
# builtin id table, test-runner flag propagation. A floating tag means a routine
# base refresh could invalidate every measurement recorded for this task, with
# nothing here noticing. This digest is node 22.23.2 / undici 6.28.0, which is
# what the gate was measured against.
FROM node:22-bookworm-slim@sha256:83f487e0a63425e5b4d146fb5e5be574bcbe1b7b843d3ebafdd95eaf7767a7e5

RUN mkdir -p /app /logs/verifier

COPY . /tests
RUN chmod +x /tests/test.sh

# The scorer runs as root -- it must write /logs/verifier and reap the decode --
# but the DECODE does not. As root the candidate's decoder could overwrite
# /tests/nonet.mjs (the capability layer is re-read on every spawn, so
# neutering it once frees every later case) or replace /usr/local/bin/node with
# a wrapper around a planted native binary, and score without a line of decoder
# being written.
#
# score.mjs spawns the decode as this unprivileged user. Everything the scorer
# relies on stays root-owned and read-only to it: /tests (the scorer and its
# preload), /usr/local/bin and /usr/local/lib (the interpreter itself).
RUN id -u decode >/dev/null 2>&1 || useradd -m -u 65533 -s /usr/sbin/nologin decode \
 && chown -R root:root /tests /usr/local/bin /usr/local/lib \
 && chmod -R go-w /tests /usr/local/bin /usr/local/lib

# The decode is given "-i /tests/cases/streams/<file>" and must read that, but
# it has no business reading the case metadata sitting beside it. manifest.json
# carries every case name, frame geometry and expected MD5. The MD5s are not
# invertible into frames, but total_frames is a real edge: the reward denominator
# is max(expected, produced), and the masked path adds |produced - want|, so a
# decoder that knows `want` can stop paying for over-production. instruction.md
# forbids reading it; this makes the forbidding true. Only the root scorer opens
# these, and root ignores the mode bits.
RUN chmod 600 /tests/cases/*.json 2>/dev/null; true

# `chmod -R go-w` strips WRITE, not read, so the decode could still read the
# scorer's own source -- every scan pattern and the reward denominator.
# nonet.mjs must stay readable: node loads it as uid 65533 via --import. score.mjs
# need not be: only the root scorer runs it.
RUN chmod 640 /tests/score.mjs

WORKDIR /app
