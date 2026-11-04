# Verifier image for this step, built with steps/NN-<fork>/tests/ as the Docker
# build context.  It deliberately contains a C toolchain and nothing else: no
# Go, Rust, JVM, Node or Python-based Ethereum client, and no network clients,
# so the candidate's `make` cannot fetch or build a third-party EVM at grading
# time.  The scored corpus lives only in this image.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash coreutils diffutils file findutils gawk grep sed tar util-linux \
       make binutils build-essential libc6-dev pkg-config \
       libssl-dev libsecp256k1-dev libgmp-dev \
       python3 \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

COPY . /tests
RUN chmod +x /tests/test.sh && chmod -R go-rwx /tests

WORKDIR /app
