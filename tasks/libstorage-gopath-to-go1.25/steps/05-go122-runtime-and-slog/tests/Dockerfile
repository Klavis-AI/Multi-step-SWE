# Verifier image for this step, built with steps/NN-*/tests/ as the Docker
# build context: it cannot reach the task root or another step, so the harness,
# the expectations, the gate sources and the module mirror are copied into every
# step. The files are byte-identical between steps, so the layer cache builds
# them once.
#
# The toolchain here is the verifier's own. Nothing in /app influences it:
# GOTOOLCHAIN=local pins the compiler, GOFLAGS is empty so a candidate cannot
# smuggle a build flag in through the environment, and GOPROXY points at the
# same offline mirror the agent had, so grading resolves modules without a
# network.
FROM golang:1.25.14-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils diffutils gawk python3 util-linux \
    && rm -rf /var/lib/apt/lists/*

ENV GOTOOLCHAIN=local \
    GOFLAGS= \
    GOSUMDB=off \
    GOPROXY=file:///opt/gomodmirror

# The dependency mirror, and a warm module cache: third-party caches are warm,
# the project itself is always built from clean. Both are made world-readable
# because scoring runs unprivileged (65534) and the mirror's subdirectories
# arrive from the host as root-only, which makes every fetch through
# GOPROXY=file:///opt/gomodmirror fail `permission denied`.
COPY gomodmirror /opt/gomodmirror
COPY warm_module_cache.sh /usr/local/bin/warm_module_cache.sh
RUN chmod +x /usr/local/bin/warm_module_cache.sh \
    && GOPATH=/tmp/gopath /usr/local/bin/warm_module_cache.sh \
    && chmod -R a+rwX /tmp/gopath \
    && chmod -R a+rX /opt/gomodmirror

# The gate tools, built from source inside the image so no binary of unknown
# provenance is shipped: the untyped-container analyzer, the error-handling
# analyzer, the AST census, the typed-clone scanner, the typed loop scanner
# and staticcheck. Their own dependencies come from a second offline mirror,
# which is deleted once the binaries exist.
COPY gatesrc /opt/gatesrc
RUN cd /opt/gatesrc \
    && GOPATH=/tmp/gopath GOFLAGS=-mod=mod \
       GOPROXY=file:///opt/gatesrc/toolsmirror \
       GOBIN=/opt/gates/bin go install \
         ./ifacescan ./errscan ./astcount ./dupscan ./loopscan \
         honnef.co/go/tools/cmd/staticcheck \
    && chmod -R a+rX /opt/gates/bin \
    && rm -rf /opt/gatesrc

# The behavior suite and the scorer: the harness, the scenario list, the
# recorded answers and this step's gate specification. None of it was ever
# inside the agent's container.
COPY lib /tests/lib
# The probe packages: ordinary Go test packages the scorer compiles
# inside a copy of the candidate's tree, so a behavioural requirement
# can be graded by running it rather than by recognising its spelling.
COPY probe /tests/probe
COPY test.sh helpers.sh /tests/
RUN chmod +x /tests/test.sh \
    && mkdir -p /app /logs/verifier \
    && chmod -R a+rX /tests \
    && mkdir -p /tmp/gocache && chmod 0777 /tmp/gocache

WORKDIR /app
