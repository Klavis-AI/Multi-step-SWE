# Verifier image for one step of the chain.  Built with that step's tests/ as the
# Docker build context: it cannot reach the task root or another step, so the
# corpus runner and the recorded expectations are copied in here.
#
# Nothing in this file is per-step: keep it, and test.sh, byte-identical across
# all nine steps, with no per-step ARG or comment.  All nine steps' build
# contexts are directories named tests/ holding the same relative paths, and
# Docker's context sync treats a file as unchanged when its (path, size) match a
# sibling's and the mtime looks unchanged.  mtime survives neither a git
# checkout nor the packaging tar, so two same-sized per-step copies can be
# swapped for each other; identical copies make that a no-op.  Everything
# release-specific is read at build time from release-1.<minor>.env and
# expected/v1.<minor>.<patch>-hid.json, which exist in exactly one of the nine
# contexts each.
FROM debian:bookworm-slim

ARG ETCD_VERSION=v3.5.21

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash build-essential ca-certificates coreutils curl diffutils gawk jq \
       libssl-dev openssl pkg-config procps python3 \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /tmp/etcd.tgz \
      "https://github.com/etcd-io/etcd/releases/download/${ETCD_VERSION}/etcd-${ETCD_VERSION}-linux-amd64.tar.gz" \
    && tar -xzf /tmp/etcd.tgz -C /usr/local/bin --strip-components=1 \
       "etcd-${ETCD_VERSION}-linux-amd64/etcd" "etcd-${ETCD_VERSION}-linux-amd64/etcdctl" \
    && rm -f /tmp/etcd.tgz

RUN mkdir -p /app /logs/verifier /run/kcp

COPY . /tests
RUN chmod +x /tests/test.sh

# kubectl must be the release this image's corpus was recorded from, so take it
# from the corpus rather than from a constant written here, and fail the BUILD if
# the context is not self-consistent -- a mismatch caught here costs a build, the
# same mismatch caught at score time costs a step.
RUN set -eu; \
    hid=$(ls /tests/expected/v1.*-hid.json); \
    [ "$(printf '%s\n' "$hid" | wc -l)" -eq 1 ] \
      || { echo "expected exactly one /tests/expected/v1.*-hid.json, got: $hid" >&2; exit 1; }; \
    env=$(ls /tests/release-1.*.env); \
    [ "$(printf '%s\n' "$env" | wc -l)" -eq 1 ] \
      || { echo "expected exactly one /tests/release-1.*.env, got: $env" >&2; exit 1; }; \
    grep -qx "KCP_EXPECTED=$(basename "$hid")" "$env" \
      || { echo "$env does not name $(basename "$hid")" >&2; exit 1; }; \
    v=$(basename "$hid" -hid.json); \
    case "$v" in v1.*.*) ;; *) echo "no kubectl version in $hid" >&2; exit 1 ;; esac; \
    curl -fsSL -o /usr/local/bin/kubectl \
      "https://dl.k8s.io/release/${v}/bin/linux/amd64/kubectl"; \
    chmod +x /usr/local/bin/kubectl; \
    echo "verifier image built for ${v} from $(basename "$env")"

WORKDIR /app
