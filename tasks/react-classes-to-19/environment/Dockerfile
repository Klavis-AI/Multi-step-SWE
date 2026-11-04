# The agent's image: a current toolchain and the baseline repository, unmodified.
#
# What is deliberately *absent* is as much of the design as what is present.
# Node 8 is not here, and neither is the python2/node-gyp stack that node-sass 4
# needs to compile its bindings, so the legacy webpack-2 build cannot be made to
# work by reaching for an old interpreter (rule 6: retired toolchains are absent
# from the agent's image and the verifier's alike).  The migration is the only
# way forward, which is the point of the exercise.
#
# Nothing of the author's is in this tree: no recording, no expectation, no
# runner, no gate script, no golden solution, no scorer config.  /app is the
# pinned repository and nothing else.
FROM node:22-bookworm-slim

# git and ripgrep because a migration is mostly reading; curl to exercise the
# mock API by hand; procps for the timer's process walk; ca-certificates because
# the network is open by design (rule 9) and npm needs to trust the registry.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates curl git procps python3 ripgrep \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# The repository at commit 2148e9e29a617e189cd902ef5c9d02a737572b81 (2017-10-07),
# byte-for-byte as published, including its Express mock API under server/demo1
# and its `lib/` of hand-vendored runtime assets.  No .git: the pin is the
# starting state, and a history to check out other commits from would only be a
# way to get a different starting state.
COPY repo/ /app/

# Keep this synchronized with every [steps.agent].timeout_sec in task.toml.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# Assert the image is the tree it claims to be.  A COPY that silently brought in
# the wrong directory - or one that had been built in, dependencies and all -
# would be invisible until a step scored strangely.
RUN test -f /app/package.json \
 && test -f /app/webpack.config.js \
 && test -d /app/src/components \
 && test -d /app/server/demo1 \
 && test ! -e /app/node_modules \
 && test ! -e /app/dist \
 && test ! -e /app/.git \
 && test "$(find /app/src -name '*.js' | wc -l)" = 79
