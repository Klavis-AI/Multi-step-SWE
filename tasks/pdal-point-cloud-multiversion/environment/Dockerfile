# Agent environment for pdal-point-cloud-multiversion.
#
# ubuntu:22.04 is chosen deliberately: its GDAL 3.4.1 / PROJ 8.2.1 / GEOS 3.10
# floor is the one system stack that configures and builds EVERY anchor in this
# task's window (PDAL 2.0.1 through 2.7.1). PDAL 2.7.2 moved to
# `find_package(PROJ 9 CONFIG)` and does not configure here, which is why the
# ladder stops at 2.7.1. Measured, not assumed:
# tmp/pdal-point-cloud-multiversion/probe-late/cfglog-*.txt.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV LC_ALL=C
ENV TZ=UTC

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      ninja-build \
      pkg-config \
      libgdal-dev \
      libgeotiff-dev \
      libproj-dev \
      libgeos-dev \
      libgeos++-dev \
      libtiff-dev \
      libcurl4-openssl-dev \
      libxml2-dev \
      zlib1g-dev \
      liblzma-dev \
      libzstd-dev \
      libsqlite3-dev \
      libjsoncpp-dev \
      libeigen3-dev \
      python3 \
      python3-dev \
      python3-numpy \
      ca-certificates \
      git \
      curl \
      unzip \
      less \
      vim-tiny \
 && rm -rf /var/lib/apt/lists/*

# Offline dependency prefetch.
#
# The agent phase runs against a network allowlist: the model's own inference
# endpoint is the only reachable host, so anything the agent's build needs has
# to be in the image already. The system libraries above cover the base build
# of every anchor in the ladder (2.0.1 through 2.7.1). One thing they do not
# carry is a LAZ codec, and the ladder needs one from step 04 (PDAL 2.4.0) on:
# COPC is LAZ-compressed, steps 04-07 write LAZ and COPC files that the
# verifier reads back with an independent reader (laspy + lazrs), and steps
# 05-07 also read a `.laz` fixture, so with no codec the graded build produces
# files no case can open and the gate vetoes four whole steps.
#
# assets/lazperf/ is the source tree of laz-perf 3.2.0 -- the general-purpose
# LAZ library upstream PDAL itself vendors from 2.4.0 on, and the version its
# 2.5.0 tree carries -- plus a build file and a provenance record. It is placed here as source, not installed into the
# system, because the score is taken by rebuilding /app in a container that has
# no network and no copy of /opt: a codec has to be vendored into /app to
# survive grading. PROVENANCE.txt carries a sha256 for every file. It is a
# dependency, not the milestone: no PDAL source, no filter, reader or writer of
# this task, and no COPC code is in it.
COPY assets/lazperf /opt/laz-perf
RUN chmod -R a+rX /opt/laz-perf
# The image proves its own prefetch: build the codec once here, so a broken or
# incomplete asset fails the image build instead of the agent's first build.
RUN cmake -S /opt/laz-perf -B /tmp/laz-perf-check -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /tmp/laz-perf-check \
 && test -f /tmp/laz-perf-check/liblazperf.a \
 && rm -rf /tmp/laz-perf-check

WORKDIR /app
