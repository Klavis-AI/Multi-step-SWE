# Verifier image for step 06 (PHP 7.4), built with this step's tests/
# directory as the Docker build context: it cannot reach the task root or another
# step, so the runner, the helpers and the test pool are copied into every step.
#
# Deliberately absent: any PHP binary, and every tool that could fetch or build one
# (curl, wget, git, bison, re2c, autoconf).  The candidate's interpreter is rebuilt
# here from its own C sources with the toolchain this image pins.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash binutils build-essential coreutils diffutils findutils gawk gcc \
        libc6-dev make python3 tar \
    && apt-get purge -y --auto-remove curl wget git bison re2c autoconf automake \
    && rm -rf /var/lib/apt/lists/* \
    && rm -f /usr/bin/php /usr/bin/php8* /usr/local/bin/php

RUN mkdir -p /app /logs/verifier /build

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app

# packaging guard: unique file SIZE: without the pad all eleven verifier Dockerfiles
# have the same size with different contents, which lets docker's build-context sync
# serve one step's Dockerfile for another. Size is the only differentiator that
# survives a git clone or a tar round-trip, so keep this pad unique.
# step 06-php-7-4 size-pad ------------------------------------
