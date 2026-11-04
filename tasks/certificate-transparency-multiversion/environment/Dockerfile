# Agent environment. The candidate writes Go, so the image carries the same Go
# toolchain the verifier uses, pinned by the same digest, plus the ordinary
# shell tooling an agent expects. Nothing here affects grading: the verifier
# owns its own image (steps/*/tests/Dockerfile) and rebuilds from source.
FROM golang:1.25-bookworm@sha256:3b4a11519ad929d1e1d261a12cff056f0c85b735253d7d861346b9c6f8b36437

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates curl git procps python3 ripgrep jq openssl xxd protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

ENV GOTOOLCHAIN=local GOFLAGS=-mod=mod GOPATH=/go GOMODCACHE=/go/pkg/mod

# The agent phase has no network and the candidate builds Go, so the module
# cache must be warm before the container hands over. It is populated here, at
# image build time, from the union of every step's pinned manifest
# (assets/gomod/<step>/, byte-identical to the go.mod/go.sum the workspace
# ships) -- the same fetch every verifier image does for itself. The fetch
# runs with the network up; GOPROXY is switched off only afterwards, so a
# candidate build resolves from the cache and a module outside it fails as a
# Go resolution error rather than a DNS one. GOWORK=off matches the grader.
COPY assets/gomod /opt/gomod
RUN set -eu; \
    mkdir -p /app; \
    for d in /opt/gomod/*/; do \
        cp "$d/go.mod" "$d/go.sum" /app/; \
        cd /app; \
        go list -m -f '{{if and (not .Main) (not .Replace)}}{{.Path}}@{{.Version}}{{end}}' all > /tmp/mods.txt; \
        sed -i '/^$/d' /tmp/mods.txt; \
        xargs -r -n 32 go mod download < /tmp/mods.txt; \
    done; \
    rm -rf /opt/gomod /tmp/mods.txt /app/go.mod /app/go.sum; \
    chmod -R a+rX /go/pkg/mod
ENV GOPROXY=off GOSUMDB=off GOWORK=off

WORKDIR /app

# Keep the script and every step's timeout_sec synchronized. The script is an
# agent-visible countdown; Harbor enforces the actual timeout.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
