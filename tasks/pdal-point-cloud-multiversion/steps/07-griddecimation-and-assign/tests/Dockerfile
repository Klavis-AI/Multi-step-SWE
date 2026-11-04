# Verifier image for pdal-point-cloud-multiversion, step 07.
#
# Same system stack as the agent environment -- the verifier rebuilds the
# candidate's /app from source -- plus an INDEPENDENT point-cloud reader
# (laspy + lazrs) so that written LAS/LAZ/COPC files are inspected without
# running the candidate's own binary. Versions are pinned; the image build is
# the only moment a network is touched.
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
      python3-pip \
      ca-certificates \
      coreutils \
 && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --no-cache-dir \
      "numpy==1.26.4" \
      "laspy==2.5.4" \
      "lazrs==0.6.1" \
 && python3 -c "import laspy, numpy; print(laspy.__version__, numpy.__version__)"

# Bind this image to its step BEFORE the first COPY. Docker's local build
# context cache keys on (path, size, mtime-to-the-second) and never on content,
# so without this a step can silently be graded with another step's tests/.
ARG HARBOR_STEP_ID=07
RUN echo "${HARBOR_STEP_ID}" > /etc/harbor-step-id

COPY . /tests
WORKDIR /tests
