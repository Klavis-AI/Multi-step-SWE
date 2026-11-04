# Verifier image for one rung of this task.  Built with that step's own tests/
# as the Docker build context: it cannot reach the task root, the agent image,
# or another step.  Every step ships the same image definition; only
# lib/score.py and the corpora under lib/ differ.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash build-essential ca-certificates coreutils curl diffutils file gawk \
        iproute2 jq libc6-dev make openjdk-17-jre-headless procps python3 tar \
        binutils \
    && rm -rf /var/lib/apt/lists/*

# The published FHIR packages, in the cache layout the validator and the
# candidate both read.
COPY fhir-packages.txt install-packages.sh /opt/fhir-setup/
RUN chmod +x /opt/fhir-setup/install-packages.sh && /opt/fhir-setup/install-packages.sh

# The official HL7 FHIR validator, pinned to the signed release.
ARG VALIDATOR_URL=https://github.com/hapifhir/org.hl7.fhir.core/releases/download/6.10.4/validator_cli.jar
ARG VALIDATOR_SHA=1106b9d58f9e363e47bea7c4fc065841e5fc91fe9d062775c3bfdd212bd653cc
RUN for i in 1 2 3 4 5; do curl -sSfL --retry 3 --max-time 1200 -o /opt/validator_cli.jar "$VALIDATOR_URL" && break || sleep 20; done \
    && echo "${VALIDATOR_SHA}  /opt/validator_cli.jar" | sha256sum -c -

COPY . /tests
RUN chmod +x /tests/test.sh

# The candidate server runs in this container, beside the expectations it is
# being scored against, so it runs as `blazeuser` and `/tests` is readable only
# by root.  A server that replayed the recorded answers instead of computing
# them would otherwise score most of the step without implementing anything.
# The FHIR package cache stays world-readable: the instruction tells the agent
# to read it, and it contains no expectations.
RUN useradd -r -u 4242 -m -d /home/blazeuser blazeuser \
    && chmod 700 /tests \
    && chmod -R a+rX /root/.fhir && chmod 755 /root

# Warm the validator's own package cache so scoring never depends on a live
# network, and prove the pinned command works in this image.
RUN mkdir -p /tmp/warm && cp /tests/data/warm/*.json /tmp/warm/ \
    && (java -Xmx4g -jar /opt/validator_cli.jar /tmp/warm -version 4.0.1 -tx n/a \
         -output /tmp/warm-out.json > /tmp/warm.log 2>&1 || true) \
    && grep -qE "Validate|Success|FAILURE|Information" /tmp/warm.log \
    && test -s /tmp/warm-out.json \
    && rm -rf /tmp/warm /tmp/warm-out.json

RUN mkdir -p /app /logs/verifier
WORKDIR /app
