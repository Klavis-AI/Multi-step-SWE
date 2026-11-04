# ---------------------------------------------------------------------------
# Toolchain layer.
#
# Byte for byte identical in the agent image and in all eleven verifier
# images, so Docker's layer cache builds it once per machine and every later
# step reuses it. Nothing below this marker may differ between them.
#
# What is deliberately absent: any CMake older than 4, and any -std= default
# that would let the tree keep building as C++98 once it has been migrated.
# ---------------------------------------------------------------------------
FROM gcc:16

ENV DEBIAN_FRONTEND=noninteractive

# LLVM 22 from apt.llvm.org, matching the Debian trixie base of gcc:16.
# Clang is the project's compiler from the first step onward: the modules and
# clang-tidy gates later in the line are defined over Clang's AST.
RUN apt-get update -qq \
 && apt-get install -y -qq --no-install-recommends \
      ca-certificates curl gnupg git procps ripgrep xz-utils python3 \
 && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
      | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
 && echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/trixie/ llvm-toolchain-trixie-22 main" \
      > /etc/apt/sources.list.d/llvm22.list \
 && apt-get update -qq \
 && apt-get install -y -qq --no-install-recommends \
      clang-22 clang-tidy-22 clang-tools-22 lld-22 llvm-22 libclang-rt-22-dev \
      ninja-build libboost-dev binutils \
 && rm -rf /var/lib/apt/lists/*

# CMake 4.1.2 from Kitware. The pinned tree cannot be configured by it -- that
# is the first step's forced break -- and no CMake 3 exists in the image to
# fall back to.
RUN curl -fsSL https://github.com/Kitware/CMake/releases/download/v4.1.2/cmake-4.1.2-linux-x86_64.tar.gz \
      -o /tmp/cmake.tgz \
 && mkdir -p /opt/cmake \
 && tar -xzf /tmp/cmake.tgz -C /opt/cmake --strip-components=1 \
 && rm /tmp/cmake.tgz
ENV PATH="/opt/cmake/bin:${PATH}"

# ...and reachable without it. `ENV PATH` is not read by a login shell: Debian's
# /etc/profile assigns PATH outright, so `bash -lc cmake` in this image reports
# `command not found` while `bash -c cmake` works -- which reads like CMake is
# missing rather than like the shell was started with a flag. /usr/local/bin is
# on the profile's PATH, so a link there is on every shell's.
RUN for tool in cmake ctest cpack; do \
      ln -sf /opt/cmake/bin/$tool /usr/local/bin/$tool; \
    done

# Unversioned names for the tools the migrated-ness gates are written against.
RUN for tool in clang clang++ clang-tidy clang-query clang-apply-replacements \
                llvm-nm llvm-objdump; do \
      if [ -e /usr/lib/llvm-22/bin/$tool ]; then \
        ln -sf /usr/lib/llvm-22/bin/$tool /usr/local/bin/$tool; \
      fi; \
    done

# The toolchain description. Clang 22 finds no libstdc++ under /usr/local by
# itself, so the standard library selection is stated once, here, and reaches
# the compile database through CMAKE_CXX_FLAGS_INIT. A project that overwrites
# CMAKE_CXX_FLAGS instead of appending to it will not compile.
RUN mkdir -p /opt/ise && cat > /opt/ise/toolchain-clang22.cmake <<'TOOLCHAIN'
# Clang 22 with the libstdc++ that ships with the GCC 16 in this image.
set(CMAKE_C_COMPILER   clang-22)
set(CMAKE_CXX_COMPILER clang++-22)

set(ISE_GCC_INSTALL_DIR /usr/local/lib/gcc/x86_64-linux-gnu/16.2.0)
set(CMAKE_C_FLAGS_INIT             "--gcc-install-dir=${ISE_GCC_INSTALL_DIR}")
set(CMAKE_CXX_FLAGS_INIT           "--gcc-install-dir=${ISE_GCC_INSTALL_DIR}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "--gcc-install-dir=${ISE_GCC_INSTALL_DIR}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "--gcc-install-dir=${ISE_GCC_INSTALL_DIR}")
TOOLCHAIN
ENV CMAKE_TOOLCHAIN_FILE=/opt/ise/toolchain-clang22.cmake
ENV LD_LIBRARY_PATH=/usr/local/lib64

# Ninja rather than Make, for two reasons that are not preferences. `cmake
# --build . -- -k 0` -- how a complete warning inventory is collected in one
# pass instead of stopping at the first error -- is Ninja syntax; the Makefile
# generator reads the 0 as a target and fails. And C++20 named modules need a
# generator that can scan module dependencies, which Make cannot. Set here so
# that a candidate who writes a plain `cmake -S . -B build` gets it.
ENV CMAKE_GENERATOR=Ninja

# The rest of this environment's build defaults, set here rather than passed on
# a command line. The two commands the task names are the two commands that get
# run -- by the candidate and by the verifier alike -- so anything either of
# them needs beyond those commands has to be a property of the image, or the
# verifier would be building the project differently from the way the task
# says the project is built. A compile database is how clangd and clang-tidy
# find their flags; the parallel level is this container's cpu budget.
ENV CMAKE_EXPORT_COMPILE_COMMANDS=ON
ENV CMAKE_BUILD_PARALLEL_LEVEL=7

# ---------------------------------------------------------------------------
# Agent image.
#
# Everything above this line is the shared toolchain layer, byte for byte the
# same as in all eleven verifier images. Everything below is what only the
# agent container has: the agent harness's node and npm, the pinned tree and
# the advisory timer.
#
# What is deliberately absent: the recorded behaviour suite, the driver that
# produces it, the gate scripts, and every other part of the verifier. The
# agent inherits the repository exactly as the pin has it and no test it did
# not already carry.
# ---------------------------------------------------------------------------

# Agent harnesses need node and npm on PATH; without them Harbor's setup runs
# `apt-get install nodejs npm` (no --no-install-recommends) into this image.
# curl, bash and pgrep come from the apt block, node and npm from this block.
# Keep this block (or anything else that provides `node` and `npm`) unless the
# task deliberately wants Debian's node packages.
ARG NODE_VERSION=24.22.0
RUN set -eux; \
    arch="$(dpkg --print-architecture)"; \
    case "$arch" in \
      amd64) node_arch=x64 ;; \
      arm64) node_arch=arm64 ;; \
      *) echo "unsupported architecture: $arch" >&2; exit 1 ;; \
    esac; \
    tarball="node-v${NODE_VERSION}-linux-${node_arch}.tar.xz"; \
    curl -fsSLo "/tmp/${tarball}" "https://nodejs.org/dist/v${NODE_VERSION}/${tarball}"; \
    curl -fsSLo /tmp/SHASUMS256.txt "https://nodejs.org/dist/v${NODE_VERSION}/SHASUMS256.txt"; \
    (cd /tmp && grep " ${tarball}\$" SHASUMS256.txt | sha256sum -c -); \
    tar -xJf "/tmp/${tarball}" -C /usr/local --strip-components=1 --no-same-owner; \
    rm -f "/tmp/${tarball}" /tmp/SHASUMS256.txt; \
    test ! -e /usr/share/nodejs; \
    node --version; npm --version

# The pinned tree, unmodified: github.com/haoxingeng/ise at
# 9a785fec10e2eaa42210d27606bbd23d05e9e95d, re-encoded to UTF-8 and trimmed as
# recorded in the authoring notes. Vendored rather than cloned at build time so
# the image is reproducible on a machine with no network and the pin cannot
# drift underneath the recordings.
COPY tree/ /app/

# Keep this script and every [steps.agent].timeout_sec in task.toml
# synchronized. The script is an agent-visible countdown; Harbor's timeout is
# what actually enforces the budget.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

WORKDIR /app
