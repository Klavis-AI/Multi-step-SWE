# Verifier image. Built with this step's tests/ as the Docker build context, so
# it can reach neither the task root nor another step -- every shared file below
# is checked in once per step, and this Dockerfile is byte-identical across the
# six steps. With the only COPY at the end, all six steps share every build
# layer above it.
#
# Nothing here is inherited from environment/Dockerfile. The verifier installs
# its own toolchain so a candidate that uninstalls, downgrades or reconfigures a
# tool in /app cannot change how it is graded.
FROM python:3.11-slim-bookworm

# CPython 3.11 is the reference interpreter: the recorded behaviour suite was
# observed on it, and it is this image's own `python`, exactly as in the agent
# image.

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash binutils build-essential ca-certificates coreutils curl diffutils \
        file gawk git jq less pkg-config procps unzip util-linux \
        xz-utils zip \
    && rm -rf /var/lib/apt/lists/*

# Same uv the agent image pins. Reading a uv.lock, replaying a sync and
# exporting PEP 751 all go through it.
COPY --from=ghcr.io/astral-sh/uv:0.12.13 /uv /uvx /usr/local/bin/

ENV PIP_CACHE_DIR=/opt/cache/pip \
    UV_CACHE_DIR=/opt/cache/uv \
    PIP_DISABLE_PIP_VERSION_CHECK=1

# The other three interpreters the task targets, including the 3.13
# free-threaded build. Grading installs and imports the candidate's own wheels
# on each of them.
ENV UV_PYTHON_INSTALL_DIR=/opt/pythons \
    UV_PYTHON_BIN_DIR=/usr/local/bin
RUN uv python install 3.12 3.13 3.13t \
    && python3.12 --version && python3.13 --version && python3.13t --version

# `patchelf` is the PyPI wheel, not bookworm's apt package, and the version is
# the point: bookworm ships 0.14.3 and `auditwheel repair` refuses to start
# below 0.14.5 --
#   ValueError: patchelf 0.14.3 found. auditwheel repair requires patchelf >= 0.14.5.
# -- so with the apt package the image carries the standard manylinux tool and
# no way to run it. (`auditwheel show` needs no patcher and works either way.)
# Grading toolchain, pinned to the same versions the agent image carries.
RUN pip install \
        "setuptools==79.0.1" \
        "wheel==0.46.3" \
        "build==1.6.1" \
        "twine==7.0.0" \
        "check-manifest==0.51" \
        "validate-pyproject[all]==0.26" \
        "auditwheel==6.8.2" \
        "patchelf==0.17.2.4" \
        "cyclonedx-bom==7.4.0" \
        "packaging==26.3" \
        "pkginfo==1.13" \
        "tomli-w==1.2.0" \
        "pytest==8.4.2" \
        "mock==5.2.0" \
        "coverage==7.16.1" \
        "setuptools-scm==10.2.3" \
        "hatchling==1.32.0" \
        "hatch-vcs==0.5.0" \
        "scikit-build-core==1.0.3" \
        "meson-python==0.21.1" \
        "meson==1.12.0" \
        "ninja==1.13.2" \
        "cmake==4.4.3" \
        "pybind11==3.1.0" \
        "flit-core==4.0.2" \
        "pdm-backend==2.4.9"

# Warm caches, identical to the agent image. A PEP 517 build resolves its
# backend from here, so a slow or unreachable index is never scored as a
# candidate failure.
RUN uv pip install --system --target /tmp/warm \
        setuptools wheel setuptools-scm hatchling hatch-vcs scikit-build-core \
        meson-python ninja cmake pybind11 flit-core pdm-backend \
        ply six simplejson \
    && rm -rf /tmp/warm \
    && pip download --dest /tmp/warmpip \
        setuptools wheel setuptools-scm hatchling hatch-vcs flit-core \
        pdm-backend scikit-build-core meson-python ninja cmake pybind11 \
        ply six simplejson \
    && rm -rf /tmp/warmpip

# /app is repopulated from the collected artifact and /logs/verifier is
# bind-mounted at run time; creating them keeps workdir valid at container
# start.
RUN mkdir -p /app /logs/verifier

COPY . /tests
# Root's alone: candidate code runs as nobody in this container and has no
# business reading the recordings, the gates or the pinned tree. Nothing
# unprivileged reads /tests -- the harness copies what the runners need into
# /grade first.
RUN chmod +x /tests/test.sh /tests/lib/*.sh && chmod -R go-rwx /tests

WORKDIR /app
