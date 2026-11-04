# Verifier image for this step. The build context is this step's tests/
# directory, so the suites, selections and fixtures are copied from here and
# nothing is inherited from the agent image.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils diffutils findutils gawk grep make \
       openssl procps python3 sed tar util-linux \
       openjdk-17-jdk-headless \
       tcl tcl-tls \
    && rm -rf /var/lib/apt/lists/*

# The same third-party dependency the development image provides, at the same
# path, so `make` in /app resolves it without a network.
RUN mkdir -p /opt/redis-deps
COPY deps/luaj-jse-3.0.1.jar /opt/redis-deps/luaj-jse-3.0.1.jar

# Pristine copies of Redis's own test suites. The candidate never sees these:
# the grader always runs the suite from here, never from /app.
COPY suites /suites

RUN mkdir -p /app /logs/verifier /work

COPY . /tests
RUN chmod +x /tests/test.sh && rm -rf /tests/suites /tests/deps

WORKDIR /app
