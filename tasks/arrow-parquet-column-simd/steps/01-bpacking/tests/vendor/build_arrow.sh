#!/usr/bin/env bash
# The one build configuration for this task.  Every side uses it verbatim: the
# agent's own build, the verifier's base build and the verifier's candidate
# build.  Keeping them identical is what makes the verifier's ccache hot, and
# it is what the instruction tells the agent to run.
#
#   build_arrow.sh <tree> <builddir> [extra cmake args...]
#     <tree> is the repository root, so the CMake source dir is <tree>/cpp.
#     Extra arguments go to the configure step only.  Set BUILD_TARGETS to a
#     space-separated list to build only those targets; unset builds everything.
#
# Release, static, no compression codecs (nothing in the ladder needs them and
# they cost a bundled download each), tests on so the project's own suite can be
# run, benchmarks off.  Thrift comes from the system package: building Arrow's
# bundled Thrift needs CMake >= 3.26 and bookworm ships 3.25.
set -euo pipefail
SRC=$1
BLD=$2
shift 2

: "${CCACHE_DIR:=/ccache}"
export CCACHE_DIR
: "${CCACHE_MAXSIZE:=30G}"
export CCACHE_MAXSIZE

cmake -S "$SRC/cpp" -B "$BLD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DARROW_PARQUET=ON \
    -DARROW_ACERO=ON \
    -DARROW_COMPUTE=ON \
    -DARROW_FILESYSTEM=ON \
    -DARROW_IPC=ON \
    -DARROW_DATASET=OFF \
    -DARROW_JSON=OFF \
    -DARROW_CSV=OFF \
    -DARROW_BUILD_TESTS=ON \
    -DARROW_BUILD_BENCHMARKS=OFF \
    -DARROW_TEST_MEMCHECK=OFF \
    -DARROW_BUILD_STATIC=ON \
    -DARROW_BUILD_SHARED=OFF \
    -DARROW_DEPENDENCY_SOURCE=AUTO \
    -DThrift_SOURCE=SYSTEM \
    -DARROW_WITH_SNAPPY=OFF -DARROW_WITH_ZSTD=OFF -DARROW_WITH_LZ4=OFF \
    -DARROW_WITH_ZLIB=OFF -DARROW_WITH_BROTLI=OFF -DARROW_WITH_BZ2=OFF \
    -DARROW_WITH_UTF8PROC=OFF -DARROW_WITH_RE2=OFF \
    "$@" > "${CONF_LOG:=${TMPDIR:-/tmp}/arrow-configure.$$.log}" 2>&1 \
    || { echo "arrow configure failed; last 40 lines of ${CONF_LOG}:"; tail -40 "${CONF_LOG}"; exit 1; }

if [ -n "${BUILD_TARGETS:-}" ]; then
    # shellcheck disable=SC2086
    cmake --build "$BLD" -j"${BUILD_JOBS:-4}" --target $BUILD_TARGETS
else
    cmake --build "$BLD" -j"${BUILD_JOBS:-4}"
fi
