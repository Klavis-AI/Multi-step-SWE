# Verifier image for one step, built with that step's tests/ directory as the
# Docker build context.  The context cannot reach the task root or another step,
# so everything shared - the runner, the gate scanners, the driver, the
# recordings - is copied into each step's tests/ directory rather than referenced
# from one place.  Eight copies is the price of the build contract; a symlink out
# of the context is silently empty in the image, which is worse.
#
# Which step it is for is deliberately not written here or in test.sh: the image
# reads it out of the spec.json it ships (spec_step() in runner/run.sh, which
# says why).  So this file and test.sh are byte-identical in every step, and
# spec.json is the only per-step file in any of these build contexts.
#
# Base: the same Playwright release the expectations were recorded with.  The
# recordings include canvas draw-op transcripts, and a chart's label coordinates
# follow the browser's font metrics, so a different release would report
# differences that are the image's fault rather than the candidate's.
# The candidate builds with Node 22 (the agent image is node:22-bookworm-slim,
# and the instruction says Node 22); this base ships Node 24. The scored
# `npm ci` / `npm run build` must run on the toolchain the candidate had, so the
# same image's Node 22 is copied in ahead of the base's on PATH
# (toolchain-gap sweep, 2026-10-14).
FROM node:22-bookworm-slim AS node22

FROM mcr.microsoft.com/playwright:v1.63.0-noble
COPY --from=node22 /usr/local/bin/node /usr/local/bin/node
COPY --from=node22 /usr/local/lib/node_modules /usr/local/lib/node_modules
RUN ln -sf ../lib/node_modules/npm/bin/npm-cli.js /usr/local/bin/npm \
    && ln -sf ../lib/node_modules/npm/bin/npx-cli.js /usr/local/bin/npx \
    && hash -r && node --version | grep -q '^v22\.' && npm --version

# The verifier must not depend on a toolchain the candidate could have modified,
# so it installs its own.  setpriv comes from util-linux and is what
# run_unprivileged uses to drop to nobody.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils diffutils gawk util-linux curl \
    && rm -rf /var/lib/apt/lists/*

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

WORKDIR /tests

# Dependencies first, from the lockfile, so the layer is cached across steps and
# the resolved tree is identical in all eight images.  npm ci fails rather than
# resolving anything itself, which is the point.
COPY package.json package-lock.json /tests/
RUN npm ci --no-audit --no-fund >/tmp/npm.log 2>&1 || (tail -40 /tmp/npm.log; exit 1)

# The mock API server: the fixed half of the interface, pristine, never taken
# from the candidate's tree.  A candidate that edits its own copy of the server
# is scored against this one.
COPY server/ /tests/server/
RUN cd /tests/server/demo1 \
 && npm install --no-audit --no-fund >/tmp/srv.log 2>&1 || (tail -40 /tmp/srv.log; exit 1) \
 && npm install --no-audit --no-fund mockjs@1.0.1-beta3 >>/tmp/srv.log 2>&1 || (tail -40 /tmp/srv.log; exit 1) \
 && rm -rf /tests/server/demo1/public

COPY . /tests

# The recordings ship compressed - 8 MB of JSON per step becomes 310 KB - and are
# unpacked once here rather than on every run.  Unpacking at image build also
# means a corrupt archive fails the build instead of scoring the step 0.
RUN tar -xzf /tests/expected.tar.gz -C /tests \
 && rm -f /tests/expected.tar.gz \
 && test -d /tests/expected \
 && test "$(ls /tests/expected | wc -l)" -gt 0

RUN chmod +x /tests/test.sh

# The build context is a root-only authoring workspace, so every file arrives
# mode 600/700.  Everything that executes candidate code runs as `nobody` - the
# dependency install, the build, the data server, the browser - and all four
# need to traverse and read /tests: the server's cwd is /tests/server/demo1, the
# driver's is /tests/runner.  `a+rX` adds read and directory-traverse only, so
# the scorer's own files, the recordings and the gate spec stay unwritable by
# the unprivileged user; /logs/verifier is hardened separately at run time and
# is not touched by this.
RUN chmod -R a+rX /tests

# Except the recordings. They are the answers the behaviour family compares
# against, and only compare.mjs reads them, as root; nothing that runs as
# `nobody` needs them. Readable, a build that left a process behind could copy
# them into the observation directory, and a dist/ symlink served by the data
# server could hand them to the page. (wave A 2026-10-12, reward-hacking F1)
RUN chmod -R go-rwx /tests/expected

WORKDIR /app
