# Verifier image for step 04 (04-upsert), built with
# steps/04-upsert/tests/ as the Docker build context: it cannot reach the task
# root or another step, so shared helpers are copied into each step. Harbor
# skips its usual tests upload here and runs /tests/test.sh from this image.
# Pinned by digest, not by tag: grading must not change under a step that
# only re-runs `docker build`. The digest names the multi-architecture
# index for gcc:12-bookworm.
FROM gcc:12-bookworm@sha256:47d8aaf12eec4c8b23092c158ee44b6305b801a352121d50f960ba8d108b7433

# The verifier owns its toolchain independently of environment/Dockerfile, so a
# candidate that modified its own workspace cannot change how it is graded.
# There is deliberately no sqlite3 package here: grading never consults a
# reference implementation, only this step's pinned cases.
#
# Every tool the harness and the cases invoke is already in the pinned base
# image, so nothing is installed and this image BUILDS offline as well as
# grading offline - `apt-get update` would otherwise resolve against whatever
# the Debian pool holds on the day of the build and make the image
# irreproducible. The list is asserted rather than installed, so a base image
# that ever stopped shipping one of them fails the build loudly instead of
# failing a scored group silently. (`awk` here is mawk; both awk expressions
# in helpers.sh and test.sh are POSIX and were checked against it.)
RUN set -eu; \
    for t in bash sh awk sed grep basename dirname cat cp rm mkdir mktemp \
             find sort xargs head tail wc tr od cmp diff sha256sum timeout \
             chmod ls printf cc ar strip make; do \
        command -v "$t" > /dev/null || { echo "verifier image lacks $t" >&2; exit 1; }; \
    done

# Grading is fully offline: every case, its expected output and its checksum
# live in this image.
ENV LC_ALL=C LANG=C

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

# Step identity, for the binding check at the top of test.sh.
#
# Written BEFORE the COPY below, so it names this IMAGE even when the copy is
# served from another step's cache entry. The builder identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's verifier gets baked in at an ordinary-looking
# score. Nothing inside a Dockerfile prevents that; this only reports it.
#
# Written as a LITERAL, substituted when this file is generated, rather than
# as an ARG: harbor builds the verifier through docker-compose-build.yaml,
# which declares no `args:`, so no --build-arg can reach this build and an
# ARG default would be an override surface nothing can use.
#
# Keep this number, the directory's number and test.sh's check equal.
RUN echo "04" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app
