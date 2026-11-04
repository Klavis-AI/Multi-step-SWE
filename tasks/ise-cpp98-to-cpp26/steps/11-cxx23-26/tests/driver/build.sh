#!/bin/sh
# Build the characterisation driver. Runs inside the container, against a tree
# that is already built.
#
#   sh build.sh <app-dir> <out-dir>
#
# CXX, STD, OPT, COV and SAN are the knobs. STD and OPT exist so the driver's few
# compiler-sensitive helpers -- c_classes.cpp's encodeWith, which conditions
# the stack frame encodeDateTime reads an indeterminate tm_isdst from -- can be
# checked against every toolchain and optimisation level the task uses rather
# than the one that happened to be handy. COV=1 links --coverage, which a build
# against a gcov-instrumented libise.a needs. SAN carries whatever flags
# sanitize.py built the library with, so the driver is instrumented the same
# way the code it calls is.
#
# Only *.cpp in this directory is compiled. probes/ holds the known-answer
# fixtures the auditors are checked against; they define cases of their own and
# must never reach a build.
set -e

APP=${1:?usage: build.sh <app-dir> <out-dir>}
OUT=${2:?usage: build.sh <app-dir> <out-dir>}
CXX=${CXX:-g++}
STD=${STD:-gnu++98}
OPT=${OPT:-O0}
COV=${COV:-0}
SAN=${SAN:-}
OBJ=${OBJ:-/tmp/drvb}
HERE=$(cd "$(dirname "$0")" && pwd)

COVFLAG=
[ "$COV" = "1" ] && COVFLAG=--coverage

# The standard library, selected the same way the CMake toolchain file selects
# it for the library itself. Clang's own default in this image is the distro's
# GCC 14, and a driver built against that one is not characterising the library
# it links: GCC 14's libstdc++ has no std::span to find at -std=gnu++17, and it
# defines none of the std::format instantiations that a GCC 16 <format> emits
# calls to, so from the milestone that introduces either the link fails with
# undefined references into the standard library rather than into ise. The flag
# is clang's; g++ rejects it, so it is only passed to clang.
GCCDIR=${GCCDIR:-/usr/local/lib/gcc/x86_64-linux-gnu/16.2.0}
GCCFLAG=
case "$CXX" in
    *clang*) [ -d "$GCCDIR" ] && GCCFLAG="--gcc-install-dir=$GCCDIR" ;;
esac

rm -rf "$OBJ"
mkdir -p "$OBJ" "$OUT"
cd "$OBJ"

for f in "$HERE"/*.cpp; do
    # Boost 1.8x greets every C++03 translation unit with five #pragma message
    # banners. They are not diagnostics about this code and they would bury one
    # that was. The filtering goes through a file rather than a pipe so the
    # status that survives is the compiler's: a translation unit that fails to
    # compile registers no cases, so the link still succeeds and the driver
    # just quietly has fewer of them.
    if ! $CXX $GCCFLAG -std="$STD" -"$OPT" -g -Wall $COVFLAG $SAN \
            -I"$HERE" -I"$APP" -I"$APP/ise/main" \
            -c "$f" -o "$(basename "$f" .cpp).o" >"$OBJ/cc.log" 2>&1; then
        grep -v 'boost\|BOOST_PRAGMA_MESSAGE\|^In file included\|^ \|^  *|' \
            "$OBJ/cc.log" || true
        echo "build.sh: $(basename "$f") did not compile" >&2
        exit 1
    fi
    grep -v 'boost\|BOOST_PRAGMA_MESSAGE\|^In file included\|^ \|^  *|' \
        "$OBJ/cc.log" || true
done

# Where the library is depends on how the tree was configured, which from step
# 1 onward is the agent's business: `cmake -S . -B build` puts a target where
# the project's own CMakeLists says, and nothing asks for a particular
# directory. The caller passes the path it located; the legacy layout is only
# the default so that an authoring-time build with no LIB set still works.
LIB=${LIB:-$APP/build/linux/debug/lib/libise.a}

$CXX $GCCFLAG $COVFLAG $SAN ./*.o "$LIB" -lpthread -o isedrv
cp isedrv "$OUT/isedrv"
