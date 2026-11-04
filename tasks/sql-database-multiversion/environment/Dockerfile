FROM debian:bookworm-slim

# Toolchain for a C99 implementation of the product, plus the ordinary
# development utilities an agent expects. The verifier owns its own image, so
# nothing here affects grading.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash binutils build-essential ca-certificates coreutils curl diffutils \
        file findutils gawk gcc git less make patch pkg-config procps python3 \
        ripgrep sed tar xxd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Keep the script and every step's timeout_sec synchronized. The script is an
# agent-visible countdown; Harbor enforces the actual timeout.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
