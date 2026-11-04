# Verifier image for this step, built with the step's tests/ directory as the
# Docker build context. It installs its own toolchain so that nothing the
# candidate did to the agent container can change how the build is graded.
FROM eclipse-temurin:21-jdk

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils diffutils file findutils make python3 \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app /logs/verifier

# The graded interpreter runs as this user, which cannot read /tests. Without
# that, a submission could open the recorded expectations by absolute path and
# print the answer instead of computing it.
RUN useradd -m -u 4242 -s /usr/sbin/nologin candidate

# `make` runs a makefile the candidate wrote, so it is candidate-supplied code
# too and gets its own unprivileged user. It is a different one: the workspace
# is handed to the build user to write in, then handed back to root before any
# graded program runs, so a scored program cannot rewrite what it is graded on.
RUN useradd -m -u 4243 -s /bin/sh builder

COPY . /tests
RUN chmod +x /tests/test.sh \
    && chown -R root:root /tests \
    && chmod -R go-rwx /tests

WORKDIR /app
