# Verifier image for step 01. Build context is this tests/ directory.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils diffutils gawk python3 \
       build-essential cmake make g++ binutils \
       libc6-dev libssl-dev pkg-config \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

COPY . /tests
RUN chmod +x /tests/test.sh /tests/adapter/run_suite.py

WORKDIR /app
