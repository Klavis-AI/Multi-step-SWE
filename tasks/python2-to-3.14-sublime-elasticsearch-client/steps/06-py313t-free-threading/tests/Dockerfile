# Verifier image for step 06-py313t-free-threading, built with steps/06-py313t-free-threading/tests/ as the
# Docker build context: it cannot reach the task root or another step, which is
# why build/sync.sh copies the harness and the recording into each one.
#
# Rendered from build/shared/Dockerfile.verifier. A step needing more than this
# (a type checker, a second interpreter, a linter) ships an override at
# build/dockerfiles/06-py313t-free-threading.Dockerfile, which sync.sh installs instead.
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
RUN uv python install --no-progress 3.13.15+freethreaded \
    && python3.13t -c "import sys; print(sys.version)" \
    && ! command -v python2 \
    && ! command -v python2.7

# Read by test.sh. Named rather than assumed so that the free-threaded step,
# whose binary is python3.13t and not python3.13, needs no special case.
ENV VERIFIER_PYTHON=python3.13t

# /app is repopulated from the collected artifact and /logs/verifier is
# bind-mounted at run time; creating them keeps workdir valid at container
# start.
RUN mkdir -p /app /logs/verifier

# ---------------------------------------------------------------------------
# This rung's extra instrument: a static type checker. The shared template's
# own header provides for exactly this -- "a step needing more than this (a
# type checker, a second interpreter, a linter) ships an override at
# build/dockerfiles/06-py313t-free-threading.Dockerfile, which sync.sh installs instead" --
# and rungs 05, 06 and 07 all take it up, because `types-check-clean` is
# cumulative from 05 upward. The three overrides are generated from this
# fragment and the shared template by build/dockerfiles/render.sh, so a change
# to either reaches all three: everything above this block is the template
# with 3.13.15+freethreaded substituted, byte for byte, and everything below it is too.
#
# The checker lives in its own virtualenv rather than in the interpreter's
# site-packages, and the reason is rule 6 rather than tidiness. score.py
# imports the candidate tree on $VERIFIER_PYTHON; if the checker's own
# dependencies sat in that interpreter's site-packages, a candidate could
# `import typing_extensions` and have it work in the verifier while being
# absent from the image it was given -- a tree that only passes because the
# scorer's toolchain leaked into the thing being scored.
#
# The virtualenv is built on this rung's own interpreter and no other, which
# keeps the template's one-interpreter-per-verifier invariant intact on every
# rung including the free-threaded one. That is a measured choice, not an
# assumption: on 3.13t the wheel is the pure-Python build (`compiled: no`) and
# the same pass over the client core takes 13.5s against 3.4s, but the finding
# sets are identical -- 0 on the rung-05 golden and the same 165 findings, in
# the same order, on its predecessor. Four times slower against an 1800s
# verifier budget buys a verifier that holds exactly the interpreter its step
# names. What the checker *targets* is separate again and comes from
# $VERIFIER_PYTHON (harness/gates.py, _target_version).
#
# Pinned exactly, for the reason NOTES section 5.1 pins the interpreters: a
# floating checker version silently changes what "clean" means between the run
# that calibrated the golden and the run that scores a candidate.
ENV TYPECHECK_VENV=/opt/typecheck
RUN uv venv --python python3.13t "$TYPECHECK_VENV" \
    && VIRTUAL_ENV="$TYPECHECK_VENV" uv pip install --no-progress \
         'mypy==1.18.2' \
    && "$TYPECHECK_VENV/bin/mypy" --version \
    && chmod -R a+rX "$TYPECHECK_VENV" \
    && ! "$VERIFIER_PYTHON" -c "import mypy" 2>/dev/null

# Rehearse the instrument against a known answer before trusting a finding
# (INSTRUCTIONS trap 6): a clean control and a synthetic failure, both built
# here, both asserted. A checker that has silently stopped reporting -- a
# missing dependency, a config it declined to read, a version whose default
# changed -- reports zero findings on the candidate tree and hands it this
# rung's whole own bar. That failure has to happen at image build, not at
# score time, and it has to be a *pair*: the clean file alone proves only that
# the process exits 0, which is also what a checker that checks nothing does.
#
# The pair is checked at the language level this rung's verifier grades at,
# for the same reason: a checker that refuses `--python-version 3.13t`
# outright would otherwise be discovered by a scoring run rather than here.
RUN set -eu; d=$(mktemp -d); \
    printf 'def f(x):\n    return x\n' > "$d/bad.py"; \
    printf 'from __future__ import annotations\n\n\ndef f(x: int) -> int:\n    return x\n' \
        > "$d/good.py"; \
    v=$(printf '%s' "$VERIFIER_PYTHON" | sed 's/^python//; s/t$//'); \
    if "$TYPECHECK_VENV/bin/mypy" --strict --no-incremental --python-version "$v" \
           --cache-dir=/tmp/mypy-selftest "$d/bad.py" > "$d/out" 2>&1; then \
        cat "$d/out"; echo "selftest: checker accepted an unannotated def"; exit 1; \
    fi; \
    grep -q 'no-untyped-def' "$d/out" \
      || { cat "$d/out"; echo "selftest: expected finding not reported"; exit 1; }; \
    "$TYPECHECK_VENV/bin/mypy" --strict --no-incremental --python-version "$v" \
        --cache-dir=/tmp/mypy-selftest "$d/good.py" \
      || { echo "selftest: checker rejected a clean file"; exit 1; }; \
    rm -rf "$d" /tmp/mypy-selftest; echo "type-checker selftest ok at $v"

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
