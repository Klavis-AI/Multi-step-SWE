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
# Verifier image for step 10 (cxx20-coroutines).
#
# Everything above this line is the shared toolchain layer, byte for byte the
# same as in the agent image and in the other ten verifiers, so Docker builds
# it once per machine and every later step reuses the cached layers.
#
# The build context is this step's tests/ directory. It cannot reach the task
# root, environment/, or another step, so the harness, the driver sources, the
# recorded expectations and the pristine tree are checked in once per step.
# Every copy is generated from authoring/ by render_package.py; edit the
# source, never the copy.
# ---------------------------------------------------------------------------

# setpriv (util-linux) drops privilege for everything the candidate authored.
# The compiler, clang-tidy, clang-query, nm and CMake come from the layer
# above, so the tree is graded by a toolchain the candidate never had write
# access to.
#
# valgrind and abigail-tools are installed here and not up there, even though
# that means naming them in eleven images instead of one. They are instruments
# of the verifier -- a leak check and an ABI comparison -- and an agent that
# finds them on its own PATH has been told what its work will be measured with.
# The layer is still byte for byte identical across every image, so this costs
# one cached layer and nothing else.
RUN apt-get update -qq \
 && apt-get install -y -qq --no-install-recommends \
      util-linux gawk diffutils coreutils valgrind abigail-tools \
 && rm -rf /var/lib/apt/lists/*

# /app is repopulated from the collected artifact and /logs/verifier is
# bind-mounted at run time; creating them keeps WORKDIR valid at container
# start.
RUN mkdir -p /app /logs/verifier

# Never let a Python run write __pycache__ beside the sources it imports. A
# cache directory that appears inside /tests during grading is a difference
# between two runs of the same verifier, and one that appears inside a Docker
# build context silently changes the context's checksum.
ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONPYCACHEPREFIX=/tmp/pycache

COPY . /tests
RUN chmod +x /tests/test.sh

# The guard the traced configure is preloaded with. Built here rather than
# shipped as an object, so that what runs is the source in the package and
# nothing else, and built outside /tests so that the image's /tests stays byte
# for byte the package on disk -- which is what build_verifier_image.sh
# asserts after every build. isefix.gates names this path; check_trace_guard.py
# measures that it is here and that it does what it is for.
RUN mkdir -p /opt/ise \
 && clang -shared -fPIC -O2 -Wall -Wextra -o /opt/ise/traceguard.so \
      /tests/traceguard.c

WORKDIR /app

# build-context guard (2026-10-05): every step's verifier build context is a
# directory named tests, and BuildKit reuses its cached copy of a context
# file whose relpath, size, mode and mtime all match, so a file that differs
# between steps only in its step id can reach an image as another step's
# copy. The pad below keeps this file's size unique per step (sizes survive
# git and tar; mtimes do not). It is a comment and changes no behaviour; keep
# a per-step pad like it if this file is edited or regenerated.
# step 10 pad: ====================================================================================================
