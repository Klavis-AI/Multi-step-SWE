# Verifier image for step 10 (10-client-rebuild-and-witness), built with steps/10-client-rebuild-and-witness/tests/ as the
# Docker build context: it cannot reach the task root or another step, so the
# shared harness is copied into each step. Harbor skips its usual tests upload
# here and runs /tests/test.sh from this image.
#
# Pinned by digest, not by tag: grading must not change under a step that only
# re-runs `docker build`. The digest names the multi-architecture index for
# golang:1.25-bookworm (go1.25.14).
FROM golang:1.25-bookworm@sha256:3b4a11519ad929d1e1d261a12cff056f0c85b735253d7d861346b9c6f8b36437

# The verifier owns its toolchain independently of environment/Dockerfile, so a
# candidate that modified its own workspace cannot change how it is graded.
# Every tool the harness and the groups invoke is already in the pinned base
# image, so nothing is installed and this image BUILDS offline as well as
# grading offline - `apt-get update` would otherwise resolve against whatever
# the Debian pool holds on the day of the build and make the image
# irreproducible. The list is asserted rather than installed, so a base image
# that ever stopped shipping one of them fails the build loudly instead of
# failing a scored group silently.
RUN set -eu; \
    for t in bash sha256sum setpriv timeout od awk sed grep find install \
             openssl gcc go git xargs diff cmp; do \
        command -v "$t" >/dev/null || { echo "base image lacks $t" >&2; exit 1; }; \
    done

# Grading is fully offline. The module cache is populated HERE, where the build
# has network, from the same pinned manifest the workspace ships; at grade time
# GOPROXY=off and nothing resolves. See TEST_INTERFACE.md section 2.
ENV GOTOOLCHAIN=local GOPATH=/go GOMODCACHE=/go/pkg/mod
COPY assets/gomod/go.mod assets/gomod/go.sum /seed/product/
COPY assets/verifier/go.mod assets/verifier/go.sum /seed/verifier/

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier /tests/work

# A replaced module is skipped: `go mod download path@version` resolves the
# version literally and ignores replacements, so asking it for the product at
# its placeholder v0.0.0 is a request for a revision that does not exist. The
# replacement target is a directory and needs no download.
#
# The verifier module declares `replace <product> => /app`, so resolving its
# module graph needs a go.mod at /app. At grade time that is the candidate's
# tree; during this build /app is empty, so it is seeded with the same pinned
# manifest the product uses. Nothing is compiled against it - test.sh
# reinstalls both files from assets/gomod before it builds anything.
#
# (The comment lives here rather than inside the RUN: a Dockerfile joins
# backslash-continued lines into one shell command, so a `#` in the middle of
# one would comment out the rest of the command.)
RUN set -eu; \
    cp /seed/product/go.mod /seed/product/go.sum /app/; \
    for m in /seed/product /seed/verifier; do \
        cd "$m"; \
        go list -m -f '{{if and (not .Main) (not .Replace)}}{{.Path}}@{{.Version}}{{end}}' all > mods.txt; \
        sed -i '/^$/d' mods.txt; \
        xargs -r -n 32 go mod download < mods.txt; \
    done; \
    chmod -R a+rX /go/pkg/mod

WORKDIR /app

# Step identity, for the binding check at the top of test.sh.
#
# Written BEFORE the COPY below, so it names this IMAGE even when the copy is
# served from another step's cache entry. The builder identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's verifier gets baked in at an ordinary-looking
# score. Nothing inside a Dockerfile prevents that; this only reports it.
#
# Written as a LITERAL, substituted when this file is generated, rather than as
# an ARG: harbor builds the verifier through docker-compose-build.yaml, which
# declares no `args:`, so no --build-arg can reach this build and an ARG default
# would be an override surface nothing can use.
#
# Keep this number, the directory's number and test.sh's check equal.
RUN printf '10' > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh
