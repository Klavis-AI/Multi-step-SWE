# Step 1's verifier image.
#
# `environment_mode = "separate"` in task.toml, so this image never inherits the
# agent container: whatever the candidate did to /opt/venv, deleted from /app or
# installed from PyPI is invisible here. The only thing that crosses over is the
# `/app` artifact, and this image supplies its own copy of every version the
# corpus was recorded under.
#
# It is deliberately the *same* interpreter and the *same* eleven pins as the
# agent image's step-1 stack -- and, beneath them, the same transitive closure,
# since `stack.txt` pins what pip chose as well as what anyone named. The
# behaviour family replays 1236 recorded values; replaying them under a
# different pandas would report pandas' own changes as the candidate's
# regressions, and pytz publishes a release whenever a government changes a
# timezone rule. `selfcheck.py` asserts both at build time -- the pins by name
# and the closure against this image's own `pip freeze` -- so an image built
# against anything other than the recorded stack cannot exist.
#
# Nothing from a *later* step is installed here either.
# There is no pandas 3, no NumPy 2 and no Python 3.9/3.12 in this image, so a
# step-1 submission cannot be scored against a toolchain it was never given.

FROM ghcr.io/astral-sh/ruff:0.14.0 AS ruffbin

# Same base as the agent image, for the same reason: the last bullseye-based
# CPython whose installed libc6/perl-base still match what archive.debian.org
# serves, and the newest interpreter with wheels for scikit-learn 0.20.4 +
# pandas 0.23.4 + NumPy 1.14.6.
FROM python:3.7-slim-bullseye

# gcc/g++/gfortran and the BLAS headers are not optional: the scorer builds the
# candidate's package from source, and skutil ships a Fortran file
# (skutil/odr/dqrsl.f) and a Cython extension (skutil/metrics/_kernel_fast.pyx).
# util-linux supplies setpriv, which is how helpers.sh drops candidate code to
# an unprivileged uid.
RUN printf 'deb http://archive.debian.org/debian bullseye main\n' > /etc/apt/sources.list \
 && apt-get -o Acquire::Check-Valid-Until=false update -qq \
 && apt-get install -y --no-install-recommends \
      bash ca-certificates coreutils diffutils file gawk make patch \
      procps util-linux \
      gcc g++ gfortran libc6-dev pkg-config \
      libatlas-base-dev libopenblas-dev liblapack-dev \
 && rm -rf /var/lib/apt/lists/*

# A pinned ruff, as a static binary rather than a pip install: ruff's own wheels
# stopped supporting Python 3.7 long before 0.14, and a floating ruff would move
# the gate's entry counts under the manifest that was measured against them.
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

COPY stack.txt /tmp/stack.txt
RUN set -eux; \
    python -m pip install -q --no-cache-dir --upgrade 'pip<25'; \
    python -m pip install -q --no-cache-dir -r /tmp/stack.txt; \
    rm -f /tmp/stack.txt

# /logs/verifier is Harbor's; creating it here means harden_reward_dir() has
# something to chmod even if a run somehow starts without it.
RUN mkdir -p /app /logs/verifier

COPY . /tests

# build-context guard (BuildKit collision): this step ships its recorded suite
# as expected/inherited-suite-01.json, so no two steps' tests/ hold
# the same (relpath, size) with different bytes. Restore the one name every
# reader uses, and fail the build unless exactly this step's copy is present.
RUN set -e; n=$(ls -d /tests/expected/inherited-suite*.json | wc -l); [ "$n" = 1 ]; \
    mv /tests/expected/inherited-suite-01.json /tests/expected/inherited-suite.json

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
# inside this container; a recorded corpus it can read is a corpus it can echo.
# So /tests is traversable but not listable, harness/ is world-readable, and
# expected/ and manifest.json stay root-only. test.sh runs the stages that read
# them as root, and nothing those stages import touches skutil.
RUN chmod 711 /tests \
 && chmod -R a+rX /tests/harness /tests/helpers.sh /tests/test.sh \
 && chmod -R go-rwx /tests/expected /tests/manifest.json /tests/selfcheck.py \
      /tests/stack.txt /tests/Dockerfile \
 && chmod +x /tests/test.sh

# Can the gates tell a clean tree from a dirty one? Every assertion in
# rehearsal/ is a hand-computed answer, including both scoring combiners, the
# import precondition and the casebook loader. A scorer that cannot distinguish
# them must not be built into an image. The count is printed by this layer
# rather than written down here.
RUN cd /tests/harness && python -m rehearsal.rehearse

# Is this the image the manifest's numbers were measured against? Pins,
# semantics, ruff version, corpus size, recorded suite census (an empty
# expectation is not a pass), and no stray bytecode.
RUN python /tests/selfcheck.py

WORKDIR /app
