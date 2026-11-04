# Step 6's verifier image.
#
# `environment_mode = "separate"` in task.toml, so this image never inherits the
# agent container: whatever the candidate did to /opt/venv, deleted from /app or
# installed from PyPI is invisible here. The only thing that crosses over is the
# `/app` artifact, and this image supplies its own copy of every version the
# step is scored under.
#
# It is deliberately the *same* interpreter build and the *same* pins as
# the agent's step-6 stack -- and, beneath them, the same transitive closure,
# since `stack.txt` pins what pip chose as well as what anyone named. The
# behaviour family replays 1236 recorded values through 318 declared flips;
# replaying them under a different numpy or pandas would report the
# library's own changes as the candidate's regressions, and would let a flip
# pass that the pinned stack does not actually produce. `selfcheck.py` asserts
# both at build time -- the pins by name and the closure against this image's
# own `pip freeze` -- so an image built against anything other than the stack
# the flips were measured under cannot exist.
#
# Nothing from a *later* step is installed here either.
# Steps 7 and 8 build on this stack rather than replacing it, so whatever they
# add or re-pin is absent here and a step-6 submission is scored only against
# the toolchain step 6 was given.

FROM ghcr.io/astral-sh/ruff:0.14.0 AS ruffbin

# The same base as the agent image, and for the same two reasons.
#
# The first is apt: python:3.7-slim-bullseye is the last published bullseye
# image whose installed libc6 and perl-base still match what archive.debian.org
# serves for `bullseye main`, and archive.debian.org is now the only mirror
# that still has bullseye. On a current debian:bullseye-slim -- libc6
# 2.31-13+deb11u14 against the archive's u11 -- `apt-get install libc6-dev`
# cannot be satisfied at all, and deb.debian.org still publishes the
# bullseye-security indices while 404ing the pool files behind them.
#
# The second is parity, which is the reason that would still apply if apt were
# healthy. This verifier compiles the candidate's Fortran and Cython from
# source. Compiling them against a different libc and a different gcc than the
# agent had would make "it builds here and not there" a property of the images
# rather than of the submission.
#
# The 3.7 in the tag is not the interpreter anything runs on. Step 6 is where
# the interpreter itself moves: pandas 3 requires Python 3.10+, so the pinned
# python-build-standalone 3.9.23 the earlier steps ran on gives way to a pinned
# 3.12.11 (same release channel, same tarball-and-sha pinning). interpreter.sh
# below fetches that 3.12.11 build from its pin so the verifier stays
# self-contained, and selfcheck.py asserts the running interpreter is 3.12.11,
# so a slip back to the base image's 3.7 -- or to the earlier 3.9 -- fails the
# build rather than grading anything.
FROM python:3.7-slim-bullseye

# gcc/g++/gfortran and the BLAS headers are not optional: the scorer builds the
# candidate's package from source -- through meson-python, the backend step 5
# moved to when NumPy 2.0 removed numpy.distutils -- and skutil ships a Fortran file
# (skutil/odr/dqrsl.f) and a Cython extension (skutil/metrics/_kernel_fast.pyx).
# meson and ninja are pip-installed from stack.txt; they find gcc/gfortran and
# pkg-config here through the environment, so the apt set does not change.
# util-linux supplies setpriv, which is how helpers.sh drops candidate code to
# an unprivileged uid. curl and xz-utils are what interpreter.sh fetches with.
RUN printf 'deb http://archive.debian.org/debian bullseye main\n' > /etc/apt/sources.list \
 && apt-get -o Acquire::Check-Valid-Until=false update -qq \
 && apt-get install -y --no-install-recommends \
      bash ca-certificates coreutils curl diffutils file gawk make patch \
      procps util-linux xz-utils \
      gcc g++ gfortran libc6-dev pkg-config \
      libatlas-base-dev libopenblas-dev liblapack-dev \
 && rm -rf /var/lib/apt/lists/*

# A pinned ruff, as a static binary rather than a pip install, so the gate's
# entry counts cannot move under the manifest that was measured against them.
COPY --from=ruffbin /ruff /usr/local/bin/ruff

# The determinism freeze, identical to export_frozen_env() in helpers.sh. Set
# here as well so that anything run at *build* time -- the rehearsal, the
# selfcheck -- sees the same environment the scoring run will.
ENV OMP_NUM_THREADS=1 \
    OPENBLAS_NUM_THREADS=1 \
    MKL_NUM_THREADS=1 \
    NUMEXPR_NUM_THREADS=1 \
    VECLIB_MAXIMUM_THREADS=1 \
    PYTHONHASHSEED=0 \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONPYCACHEPREFIX=/tmp/pyc \
    MPLBACKEND=Agg \
    TZ=UTC \
    LC_ALL=C.UTF-8 \
    SOURCE_DATE_EPOCH=1514764800

COPY interpreter.sh /tmp/interpreter.sh
RUN bash /tmp/interpreter.sh && rm -f /tmp/interpreter.sh

# A venv, for the same reason the agent container has one: it is what makes
# `python` and `pip` resolve to this interpreter from any shell, including the
# unprivileged ones run_stage() spawns.
ENV PATH=/opt/venv/bin:$PATH
COPY stack.txt /tmp/stack.txt
RUN set -eux; \
    /opt/py312/bin/python3.12 -m venv /opt/venv; \
    python -m pip install -q --no-cache-dir --upgrade 'pip<25'; \
    python -m pip install -q --no-cache-dir -r /tmp/stack.txt; \
    rm -f /tmp/stack.txt; \
    rm -rf /root/.cache/pip; \
    test "$(command -v python)" = /opt/venv/bin/python

# /logs/verifier is Harbor's; creating it here means harden_reward_dir() has
# something to chmod even if a run somehow starts without it.
RUN mkdir -p /app /logs/verifier

COPY . /tests

# build-context guard (BuildKit collision): this step ships its recorded suite
# as expected/inherited-suite-06.json, so no two steps' tests/ hold
# the same (relpath, size) with different bytes. Restore the one name every
# reader uses, and fail the build unless exactly this step's copy is present.
RUN set -e; n=$(ls -d /tests/expected/inherited-suite*.json | wc -l); [ "$n" = 1 ]; \
    mv /tests/expected/inherited-suite-06.json /tests/expected/inherited-suite.json

# Two different audiences for two halves of /tests, and getting this wrong is
# silent both ways.
#
# The scoring *code* must be readable by uid 65534, because that is who runs
# every stage that touches candidate code. Left at the build context's 0700 it
# is not, and every stage dies with ModuleNotFoundError -- which looks exactly
# like a submission that broke the package.
#
# The *expectations* must not be readable by uid 65534 at all. The candidate
# controls what `import skutil` executes, and that code runs under the same uid
# inside this container; a recorded corpus it can read is a corpus it can echo,
# and a flips file it can read is a list of the answers the step is graded on.
# So /tests is traversable but not listable, harness/ is world-readable, and
# expected/ and manifest.json stay root-only. test.sh runs the stages that read
# them as root, and nothing those stages import touches skutil.
RUN chmod 711 /tests \
 && chmod -R a+rX /tests/harness /tests/helpers.sh /tests/test.sh \
 && chmod -R go-rwx /tests/expected /tests/manifest.json /tests/selfcheck.py \
      /tests/stack.txt /tests/Dockerfile /tests/interpreter.sh \
 && chmod +x /tests/test.sh

# Can the gates tell a clean tree from a dirty one? Every assertion in
# rehearsal/ is a hand-computed answer, including both scoring combiners, the
# import precondition and the casebook loader. A scorer that cannot distinguish
# them must not be built into an image.
RUN cd /tests/harness && python -m rehearsal.rehearse

# Is this the image the manifest's numbers were measured against? Pins,
# semantics, ruff version, corpus size, the flips file, recorded suite census
# (an empty expectation is not a pass), and no stray bytecode.
RUN python /tests/selfcheck.py

WORKDIR /app
