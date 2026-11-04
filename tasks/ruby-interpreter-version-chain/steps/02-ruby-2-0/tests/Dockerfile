# Verifier image: a C toolchain to rebuild the candidate from source, python3 to
# drive the cases, and no Ruby of any kind.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils diffutils file findutils gawk grep make sed \
       build-essential libc6-dev python3 \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

# The candidate runs as this unprivileged user, which owns nothing and cannot
# read /tests.  Without that separation an "interpreter" that never implements
# the language can read the recorded expectations and print them back.
RUN useradd --uid 4242 --no-create-home --shell /usr/sbin/nologin candidate

COPY . /tests
RUN chmod +x /tests/test.sh \
    && chown -R root:root /tests \
    && chmod -R go-rwx /tests \
    && chmod 0700 /tests

WORKDIR /app
