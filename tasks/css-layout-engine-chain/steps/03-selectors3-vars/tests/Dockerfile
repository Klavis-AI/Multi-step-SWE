# Verifier image for one step. Built with this step's tests/ as the Docker build
# context, so it cannot reach the task root or any other step; every shared file
# is copied into each step's tests/ and kept byte-identical.
FROM debian:bookworm-slim

# Only what grading needs, installed independently of environment/Dockerfile:
# the verifier rebuilds the candidate's remote end from source, so it carries
# its own C toolchain and must not inherit one the candidate could have broken.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils diffutils gawk procps \
        gcc make libc6-dev python3 \
    && rm -rf /var/lib/apt/lists/*

# The unprivileged identity the candidate's build and the candidate's own
# process run under. Created here AND re-created at run time by test.sh: a
# container restart is a silent re-provisioning event, and a privilege drop
# that has quietly become a no-op would still score 1.0.
RUN useradd --uid 4242 --create-home --shell /usr/sbin/nologin grader

RUN mkdir -p /app /logs/verifier

COPY . /tests
RUN chmod +x /tests/test.sh \
    # Expectations are readable by root only. The candidate runs as uid 4242 in
    # this same container, so "can the thing under test read the answers?" is
    # answered by the filesystem, not by trust.
    && chmod 0700 /tests/expected \
    && chmod 0600 /tests/expected/* \
    # Documents under test are inputs, not answers: the candidate must be able
    # to open them.
    && chmod -R a+rX /tests/fixtures \
    && chown -R root:root /tests \
    && chmod 0755 /tests && chmod 0700 /tests/expected


# Content fingerprint of the grading material, computed at image build time and
# itself read-only. `docker run <verifier-image> touch /tests/score.py` succeeds
# for root in any image (root ignores mode bits), so mode alone is not a control.
# This manifest is the backstop that actually detects it: test.sh verifies
# /tests against it before AND after scoring, and any difference scores the
# step 0 rather than grading with altered material.
RUN find /tests -type f ! -name '.manifest' -print0 | sort -z \
      | xargs -0 sha256sum > /tests/.manifest \
    && chmod 0444 /tests/.manifest \
    && echo "manifest entries: $(wc -l < /tests/.manifest)"

WORKDIR /app
