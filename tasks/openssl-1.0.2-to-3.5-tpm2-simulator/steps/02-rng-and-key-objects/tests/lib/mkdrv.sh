#!/usr/bin/env bash
# Build the engine libraries out of an /app tree and link cpridrv against them.
#
#   mkdrv.sh <app-dir> <build-dir> [--coverage|--sanitize]
#
# Only the two libraries the driver needs are built (CryptoEngine, platform).
# That is not about build time -- the whole tree links in about three seconds
# on this box -- but about reach: the driver is compiled with the public
# include path only, so it cannot accidentally read the engine's internals,
# and building no more than it links against keeps that honest.
#
# PROVENANCE.  This is the build under which every recording in build/record was
# taken, so the configure below is part of those recordings' provenance rather
# than a local preference, and re-deriving expected.txt or expected-e2e.txt
# means building the tree this way again:
#
#     cmake -S <app>/cmake -B <build> -DSPEC_VERSION=116
#
# against the frozen baseline snapshot in build/baseline, which
# make_baseline.sh extracts from https://github.com/stwagnr/tpm2simulator at
# commit e45324eba268723d39856111e7933c5c76238481 (2017-10-15) and which
# links the OpenSSL 1.0.2b vendored in that tree.
#
# THAT LINE IS THE VERIFIER'S, VERBATIM: the recordings and the graded tree
# have to be the same compile on the two sides of a byte-identical
# comparison.  A tree configured -DCMAKE_BUILD_TYPE=None
# -DCMAKE_C_FLAGS="-O1 -g" answers one case in 2723 differently from one
# configured with the two plain cmake lines the instruction names, and that
# answer is uninitialised stack memory.  The optimisation level is the
# verifier's to choose, not this script's.
set -euo pipefail

APP=${1:?app dir}
BLD=${2:?build dir}
MODE=${3:-}

# Empty CFLAGS means "configure exactly the way the verifier configures", which
# is the default; the two instrumented modes have to name their flags and so
# cannot.  Neither is ever the source of a recording.
CFLAGS=""
LDFLAGS=""
DRVFLAGS=""
case "$MODE" in
  --coverage)
    CFLAGS="--coverage -O0 -g"
    LDFLAGS="--coverage"
    ;;
  --sanitize)
    # Recoverable, so one replay enumerates every case that trips a check
    # rather than stopping at the first.  The driver carries the flags too:
    # ASan expects its runtime linked into the executable, not reached only
    # through a shared library's DT_NEEDED.
    SAN="-fsanitize=address,undefined -fsanitize-recover=address,undefined"
    SAN="$SAN -fno-omit-frame-pointer"
    CFLAGS="-O1 -g $SAN"
    LDFLAGS="$SAN"
    DRVFLAGS="$SAN"
    ;;
esac

mkdir -p "$BLD"
if [ -z "$CFLAGS" ]; then
    cmake -S "$APP/cmake" -B "$BLD" -DSPEC_VERSION=116 > "$BLD/cmake.log" 2>&1 \
      || { tail -40 "$BLD/cmake.log"; exit 1; }
else
    cmake -S "$APP/cmake" -B "$BLD" -G "Unix Makefiles" -DSPEC_VERSION=116 \
          -DCMAKE_BUILD_TYPE=None \
          -DCMAKE_C_FLAGS="$CFLAGS" \
          -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS" \
          -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS" > "$BLD/cmake.log" 2>&1 \
      || { tail -40 "$BLD/cmake.log"; exit 1; }
fi

# Asserted from the build system's own record rather than from this script's
# memory of what it passed, and printed, because it is provenance.
printf 'configure: CMAKE_C_FLAGS=[%s] BUILD_TYPE=[%s] generator=[%s]\n' \
    "$(awk -F= '/^CMAKE_C_FLAGS:/{print $2}' "$BLD/CMakeCache.txt")" \
    "$(awk -F= '/^CMAKE_BUILD_TYPE:/{print $2}' "$BLD/CMakeCache.txt")" \
    "$(awk -F= '/^CMAKE_GENERATOR:/{print $2}' "$BLD/CMakeCache.txt")"

make -C "$BLD" -j"${JOBS:-7}" platform CryptoEngine > "$BLD/make.log" 2>&1 \
  || { tail -60 "$BLD/make.log"; exit 1; }

LIBCE=$(find "$BLD" -name 'libCryptoEngine.so' | head -1)
LIBPF=$(find "$BLD" -name 'libplatform.so' | head -1)
[ -n "$LIBCE" ] || { echo "libCryptoEngine.so was not produced"; exit 1; }
[ -n "$LIBPF" ] || { echo "libplatform.so was not produced"; exit 1; }

gcc -std=gnu99 -O1 -g -Wall $DRVFLAGS -o "$BLD/cpridrv" \
    "$(dirname "$0")/cpridrv.c" \
    -I"$APP/src/include" \
    -I"$APP/src/tpm/include" \
    -I"$APP/src/tpm/include/prototypes" \
    -I"$APP/src/platform/include" \
    -I"$APP/src/platform/include/prototypes" \
    "$LIBCE" "$LIBPF" \
    -Wl,-rpath,"$(dirname "$LIBCE")" -Wl,-rpath,"$(dirname "$LIBPF")"

echo "driver: $BLD/cpridrv"
