# Verifier image for step 03-py312-tls-and-retired-stdlib, built with steps/03-py312-tls-and-retired-stdlib/tests/ as the
# Docker build context: it cannot reach the task root or another step, which is
# why build/sync.sh copies the harness and the recording into each one.
#
# Rendered from build/shared/Dockerfile.verifier. A step needing more than this
# (a type checker, a second interpreter, a linter) ships an override at
# build/dockerfiles/03-py312-tls-and-retired-stdlib.Dockerfile, which sync.sh installs instead.
FROM debian:bookworm-slim

# Only what grading needs, installed independently of environment/Dockerfile:
# the verifier must not depend on a toolchain the candidate could have
# modified. openssl is here because the TLS fixture mints its CAs at run time
# rather than shipping certificates that would expire (NOTES section 4.8);
# util-linux is here for setpriv, which is what runs candidate code as nobody.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils diffutils gawk openssl procps \
        util-linux \
    && rm -rf /var/lib/apt/lists/*

# The step's own interpreter and no other. A verifier holding a newer one would
# let a tree that only works on 3.14 pass an earlier rung; a verifier holding
# an older one would score the step on an interpreter the step never named.
# uv is pinned for the same reason the agent image pins it: a floating tag
# changes which CPython patch releases are installable, and the pins in NOTES
# section 5.1 are exact.
COPY --from=ghcr.io/astral-sh/uv:0.12.15 /uv /usr/local/bin/uv
ENV UV_PYTHON_INSTALL_DIR=/opt/python \
    UV_PYTHON_BIN_DIR=/usr/local/bin \
    UV_NO_CACHE=1
RUN uv python install --no-progress 3.12.14 \
    && python3.12 -c "import sys; print(sys.version)" \
    && ! command -v python2 \
    && ! command -v python2.7

# Read by test.sh. Named rather than assumed so that the free-threaded step,
# whose binary is python3.13t and not python3.13, needs no special case.
ENV VERIFIER_PYTHON=python3.12

# /app is repopulated from the collected artifact and /logs/verifier is
# bind-mounted at run time; creating them keeps workdir valid at container
# start.
RUN mkdir -p /app /logs/verifier

COPY . /tests

# The authoring workspace runs at umask 0077, so COPY carries mode 600/700 into
# the image -- and test.sh runs the scorer as uid 65534, which then cannot read
# /tests at all. The failure mode is a verifier that dies on "No module named
# harness.score" and reports a well-formed 0.000 for a perfectly correct tree.
# It survived every local calibration because those ran as root. Found on the
# item-6 oracle run; NOTES-authoring.md section 7.6.
RUN chmod -R a+rX /tests && chmod +x /tests/test.sh /tests/helpers.sh

# Build-time smoke: the harness imports on *this* interpreter and registers the
# whole suite. A sync that shipped a truncated cases/ directory, or a harness
# module that does not parse on 3.14, fails the image build here rather than
# surfacing later as a verifier that scored 300 cases and reported a plausible
# number (INSTRUCTIONS trap 3). PYTHONDONTWRITEBYTECODE so the check does not
# leave __pycache__ in /tests (trap 8).
#
# normalize.selftest() runs here too. That module is deliberately the same code
# on both sides of the comparison -- rule 5 requires it, since a rule that fired
# at record time and not at replay time would fail every case -- so the risk it
# carries is not drift but over-reach: a pattern broad enough to erase a real
# difference erases it symmetrically and a wrong tree passes. The two-pass
# recording catches the opposite failure (too little normalisation shows up as
# an unstable case and is dropped); nothing but the fixture table catches too
# much, and its rows include strings that must survive untouched. Running it in
# the image build makes the table an invariant of every verifier that ships
# rather than something an author ran once (NOTES-authoring.md section 8.6).
#
# Run through the same setpriv test.sh uses. Rehearsing the check under a
# different identity than the one that grades is how the permission bug above
# reached a score in the first place (INSTRUCTIONS trap 6): the instrument has
# to be the instrument.
RUN setpriv --reuid 65534 --regid 65534 --clear-groups \
    env HOME=/tmp TMPDIR=/tmp \
        PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=/tests "$VERIFIER_PYTHON" -c "\
import json, sys; \
from harness import driver, gates, normalize, registry, score; \
registry.load_all(); \
n = len(registry.ids()); \
want = len(json.load(__import__('gzip').open('/tests/recording/baseline.json.gz', 'rt'))['cases']); \
print('harness on', sys.version.split()[0], n, 'cases,', want, 'recorded'); \
bad = normalize.selftest(); \
sys.exit('normalize selftest failed' if bad else \
         (0 if n == want else 'registry has %d cases, recording holds %d' % (n, want)))"

# The recording and the overlays are what every case is compared against, and
# only the scorer, which runs as root, reads them; a case runs the candidate's
# tree as uid 65534 and is handed its case id and nothing else. Locked after the
# smoke above, which counts the recording as uid 65534 on purpose.
RUN chmod -R go-rwx /tests/recording /tests/flips

WORKDIR /app
