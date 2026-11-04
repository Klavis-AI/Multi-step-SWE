#!/bin/bash
# build_bench.sh <tree> <build-dir> <harness-src-dir> <out-binary>
#
# Builds the per-function bench harness against a source tree.  The tree is
# configured and compiled here if <build-dir> is not already built, then the
# harness is linked against that build's own object files, so it necessarily
# calls whatever implementation the tree installed.
set -eu
TREE=$1
BLD=$2
HSRC=$3
OUT=$4

if [ ! -f "$BLD/CMakeCache.txt" ]; then
    cmake -S "$TREE" -B "$BLD" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
        -DBUILD_TESTING=ON \
        -DCMAKE_OUTPUT_DIRECTORY="$BLD/out" > "$BLD.cmake.log" 2>&1 \
        || { echo "configure failed; see $BLD.cmake.log" >&2; tail -20 "$BLD.cmake.log" >&2; exit 1; }
fi
ninja -C "$BLD" >> "$BLD.cmake.log" 2>&1 \
    || { echo "build failed; see $BLD.cmake.log" >&2; grep -m20 -E "error" "$BLD.cmake.log" >&2; exit 1; }

INCS=""
for d in "$TREE/Source/API" \
         "$TREE/Source/Lib/Common/Codec" "$TREE/Source/Lib/Common/ASM_SSE2" "$TREE/Source/Lib/Common/ASM_AVX2" \
         "$TREE/Source/Lib/Encoder/Codec" "$TREE/Source/Lib/Encoder/ASM_SSE2" "$TREE/Source/Lib/Encoder/ASM_SSE4_1" "$TREE/Source/Lib/Encoder/ASM_AVX2" \
         "$TREE/Source/Lib/Decoder/Codec" "$TREE/Source/Lib/Decoder/ASM_SSE4_1" "$TREE/Source/Lib/Decoder/ASM_AVX2" \
         "$TREE/third_party/cpuinfo/include"; do
    [ -d "$d" ] && INCS="$INCS -I$d"
done

OBJS=$(find "$BLD/Source/Lib" "$BLD/third_party/cpuinfo" -name '*.o' 2>/dev/null | sort)
[ -n "$OBJS" ] || { echo "no object files under $BLD" >&2; exit 1; }

gcc -O2 -std=gnu99 -D_GNU_SOURCE -o "$OUT" \
    "$HSRC"/bench_main.c "$HSRC"/bench_cases_*.c \
    $INCS $OBJS -lpthread -lm -Wl,-z,noexecstack 2>&1 | grep -v "GNU-stack\|deprecated" || true
[ -x "$OUT" ] || { echo "harness link failed" >&2; exit 1; }
echo "harness built: $OUT"
