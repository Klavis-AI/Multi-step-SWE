# Agent environment for the LAMMPS accelerated-style ladder.
#
# The tree this image ships is LAMMPS at a pinned upstream tag with every
# accelerated style family in src/INTEL/ removed. It is a complete, working MD
# code: it builds and it runs every one of the shipped benchmark inputs on the
# reference (non-accelerated) styles. What is missing is the accelerated twin of
# each family.
#
# The compiler is Intel's (icx/icpx). That is not decoration: LAMMPS enables
# its vectorisation machinery only for Intel compilers (see
# src/INTEL/intel_preprocess.h), so with any other compiler the accelerated
# styles compile to scalar code and are slower than the reference rather than
# faster. Both sides of every comparison -- the candidate and the reference --
# are built with the same compiler, flags and target, so the envelope is a
# fairness rule and not a handicap.

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        bash bc ca-certificates cmake coreutils curl diffutils g++ gawk gcc git \
        gnupg make patch procps python3 time util-linux \
    && rm -rf /var/lib/apt/lists/*

# Intel oneAPI DPC++/C++ compiler (icx/icpx). The repository suite is "all";
# every dependency is an intra-repo intel-oneapi-* package, so nothing from
# Debian has to be satisfied. Pinned to a minor version so the compiler path
# /opt/intel/oneapi/compiler/2026.1 is stable.
RUN curl -fsSL https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
      | gpg --dearmor -o /usr/share/keyrings/oneapi-archive-keyring.gpg \
    && echo 'deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main' \
         > /etc/apt/sources.list.d/intel-oneapi.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends intel-oneapi-compiler-dpcpp-cpp-2026.1 \
    && rm -rf /var/lib/apt/lists/*

# The versioned binaries have their rpath baked in, but the shared OpenMP
# runtime they link against (libiomp5) does not, so put it on the loader path.
ENV ONEAPI_ROOT=/opt/intel/oneapi/compiler/2026.1
ENV PATH=/opt/intel/oneapi/compiler/2026.1/bin:$PATH
ENV LD_LIBRARY_PATH=/opt/intel/oneapi/compiler/2026.1/lib
ENV CC=icx
ENV CXX=icpx

WORKDIR /app
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

COPY vendor/ /tmp/vendor/

# Base tree: the pinned tag minus every accelerated style family.
RUN . /tmp/vendor/PIN \
    && git clone --quiet "$LMP_REPO" /app/lammps \
    && cd /app/lammps \
    && git checkout --quiet "$LMP_SHA" \
    && test "$(git rev-parse HEAD)" = "$LMP_SHA" \
    && rm -rf .git \
    && python3 /tmp/vendor/strip.py /app/lammps \
         01-pair-lj-cut 02-pair-sw 03-pair-tersoff 04-pair-lj-cut-coul-long 05-pair-airebo

# Prove the shipped tree configures, builds and runs. The build directory is
# thrown away: the candidate builds their own, and a stale tree on top of it
# would only confuse the incremental build.
RUN cmake -S /app/lammps/cmake -B /tmp/seedbld \
        -C /app/lammps/cmake/presets/basic.cmake \
        -D PKG_INTEL=on -D PKG_ML-SNAP=on -D PKG_ASPHERE=on -D PKG_DPD-BASIC=on \
        -D BUILD_MPI=no -D BUILD_OMP=on -D CMAKE_BUILD_TYPE=Release \
        -D CMAKE_CXX_FLAGS="-mavx2 -mfma -mbmi -mbmi2 -O3" \
        -D CMAKE_C_FLAGS="-mavx2 -mfma -mbmi -mbmi2 -O3" \
        > /tmp/seedcfg.log 2>&1 \
    || { echo "SEED CONFIGURE FAILED"; tail -40 /tmp/seedcfg.log; exit 1; }
RUN cmake --build /tmp/seedbld -j4 > /tmp/seedbuild.log 2>&1 \
    || { echo "SEED BUILD FAILED"; grep -m20 -i "error" /tmp/seedbuild.log; tail -40 /tmp/seedbuild.log; exit 1; }
RUN rm -rf /tmp/seedbld /tmp/seedcfg.log /tmp/seedbuild.log

# A small sample system the candidate can time against, and the measuring
# helper. These are not the measurement workload.
RUN mkdir -p /app/workload \
    && printf '%s\n' \
      '# small Lennard-Jones melt (sample system, not the measurement workload)' \
      'units           lj' \
      'atom_style      atomic' \
      'lattice         fcc 0.8442' \
      'region          box block 0 12 0 12 0 12' \
      'create_box      1 box' \
      'create_atoms    1 box' \
      'mass            1 1.0' \
      'velocity        all create 1.44 87287 loop geom' \
      'pair_style      lj/cut 2.5' \
      'pair_coeff      1 1 1.0 1.0 2.5' \
      'neighbor        0.3 bin' \
      'neigh_modify    delay 0 every 20 check no' \
      'fix             1 all nve' \
      'timestep        0.005' \
      'thermo          100' \
      'run             200' \
      > /app/workload/sample.in

RUN mkdir -p /app/tools \
    && cp /tmp/vendor/rowtime.sh /app/tools/rowtime.sh \
    && chmod +x /app/tools/rowtime.sh \
    && rm -rf /tmp/vendor

RUN echo "step-id 00 (environment image; not a milestone verifier)" > /etc/harbor-env-image
