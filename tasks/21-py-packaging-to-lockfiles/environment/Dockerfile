FROM python:3.11-slim-bookworm

# CPython 3.11 is the reference interpreter for this task: it is this image's
# own `python`, and it is the last release on which the 2014 codebase's
# `import imp` and bare `distutils` imports still resolve.

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash binutils build-essential ca-certificates coreutils curl diffutils \
        file gawk git jq less nodejs npm pkg-config procps ripgrep \
        unzip util-linux xz-utils zip \
    && rm -rf /var/lib/apt/lists/*

# uv, pinned. Workspaces, the lockfile, dependency groups and the PEP 751
# export are all driven through it.
COPY --from=ghcr.io/astral-sh/uv:0.12.13 /uv /uvx /usr/local/bin/

# Shared, warm package caches. Build backends resolved inside a PEP 517
# isolated environment come from here instead of the network.
ENV PIP_CACHE_DIR=/opt/cache/pip \
    UV_CACHE_DIR=/opt/cache/uv \
    PIP_DISABLE_PIP_VERSION_CHECK=1

# The other three interpreters this task targets, including the 3.13
# free-threaded build. python-build-standalone ships headers, so C extensions
# build against all four.
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
# Packaging toolchain, pinned.
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

# Warm the uv cache with the same distributions, and with the runtime
# dependencies the vendored copies under bento/private/ correspond to.
RUN uv pip install --system --target /tmp/warm \
        setuptools wheel setuptools-scm hatchling hatch-vcs scikit-build-core \
        meson-python ninja cmake pybind11 flit-core pdm-backend \
        ply six simplejson \
    && rm -rf /tmp/warm

# The candidate's workspace: the pinned Bento tree, exactly as
# `git archive 37de23d784407a7c98a4a15770ffc570d5f32d70` exports it.
COPY app/ /app/

# A release checkout is a tagged VCS checkout. Committed with fixed author and
# committer dates so the tree, the commit and `git describe` are identical in
# every container.
RUN cd /app \
    && git init -q -b main \
    && git config user.name "Bento Packagers" \
    && git config user.email "packagers@bento.invalid" \
    && git add -A \
    && GIT_AUTHOR_DATE="1409491949 +0100" GIT_COMMITTER_DATE="1409491949 +0100" \
       git commit -q -m "bento 0.2.0" \
    && GIT_COMMITTER_DATE="1409491949 +0100" \
       git tag -a v0.2.0 -m "bento 0.2.0" \
    && git describe --tags

# Advisory countdown. Kept outside /app: a file inside the workspace
# would show up in `git status`, in the sdist, and in check-manifest's report.
COPY timer.sh /opt/timer.sh
RUN chmod +x /opt/timer.sh

WORKDIR /app
