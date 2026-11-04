# Agent environment for ooxml-spreadsheet-engine-multiversion.
#
# Pinned by digest so a rebuild months apart gives the same toolchain: the
# candidate's Go version decides which language features compile, and grading
# is done by a verifier image pinned to the SAME digest. The digest names the
# multi-architecture index for golang:1.25-bookworm.
FROM golang:1.25-bookworm@sha256:3b4a11519ad929d1e1d261a12cff056f0c85b735253d7d861346b9c6f8b36437

# poppler-utils is here because the normative specifications seeded into the
# workspace are PDFs: without `pdftotext` the agent cannot read ECMA-376 or
# [MS-OFFCRYPTO] at all. patch is here because each milestone's Oracle applies
# a release-to-release unified diff with `patch -p1`; without it the Oracle
# ladder dies at exit 127 on every step after the first, which is exactly how
# its absence was found. The rest is ordinary navigation.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates curl git less patch poppler-utils procps python3 \
       ripgrep unzip xz-utils zip \
    && rm -rf /var/lib/apt/lists/*

# GOTOOLCHAIN=local: go.mod names `go 1.25.0`, and without this the go command
# would silently download and use a different toolchain than the verifier's.
# GOFLAGS drops the VCS stamp so /app need not be a git checkout.
ENV GOTOOLCHAIN=local \
    GOFLAGS=-buildvcs=false \
    CGO_ENABLED=0 \
    GOPATH=/go \
    GOCACHE=/root/.cache/go-build

# Warm the standard-library build cache. Go 1.20 and later ship no precompiled
# stdlib, so the agent's first build would otherwise spend a minute on it.
RUN go build std

# Prefetch the frozen dependency set into the module cache. The agent phase
# reaches nothing but the model's own endpoint, so every path that resolves
# modules -- `go build -mod=mod`, `go mod download`, `go mod vendor`, `go mod
# verify`, `go list -m all` -- would otherwise die on proxy.golang.org with a
# DNS error that reads like a broken task rather than a missing cache. (The
# `-mod=vendor` build the grader runs is already offline-clean against the
# seeded /app/vendor/; this covers the rest, including a package of a pinned
# module that the seeded vendor tree does not carry.)
#
# The modules fetched are the ones the frozen manifests pin: the nine of
# go.mod, which /app/vendor/ was built from, plus the nine go.sum-only entries
# the wider module graph pins. That is no dependency the task had not already
# declared, and no capability the workspace did not have; only the offline
# failure goes away.
#
# assets/prefetch/NN/ holds one stand-in module per step that ships a dependency
# manifest, and there is exactly one (step 01): the dependency set is a single
# pinned union seeded at step 01 and never widened, which the ten identical
# verifier pins and every solve.sh assert. See assets/prefetch/README.md. The
# tree is deleted once the fetch is done, so only /go/pkg/mod survives into the
# image, and the fetch is rooted in the manifests -- the upstream implementation
# this task asks for is not in them and cannot arrive this way.
#
# The count check is the point of the step, not decoration: a short fetch is
# silent. The image still builds, the agent's first offline resolution fails,
# and the step scores zero for what looks like a model failure.
COPY assets/prefetch /tmp/prefetch
RUN set -eux; \
    for d in /tmp/prefetch/*/; do \
        cd "$d"; \
        go mod download all; \
        go mod verify; \
    done; \
    want=$(cat /tmp/prefetch/*/go.sum | awk '{print $1}' | sort -u | wc -l); \
    got=$(find /go/pkg/mod/cache/download -name '*.zip' | wc -l); \
    [ "$got" -ge "$want" ] || { \
        echo "prefetch incomplete: $got module zips for $want pinned modules" >&2; \
        exit 1; \
    }; \
    cd /; \
    rm -rf /tmp/prefetch; \
    chmod -R a+rX /go/pkg/mod; \
    find /go/pkg/mod -type d -exec chmod a+w {} +

WORKDIR /app

# Keep this script and every [steps.agent].timeout_sec in task.toml
# synchronized. The script is an agent-visible countdown; Harbor enforces the
# actual timeout.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
