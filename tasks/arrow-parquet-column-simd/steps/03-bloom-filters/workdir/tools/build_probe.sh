#!/usr/bin/env bash
# Build one measurement probe against one arrow tree.
#
#   build_probe.sh <tree> <builddir> <probe.cc> <out-binary>
#
# The probe is compiled with exactly the defines and include paths Arrow used
# for its own translation units, read out of the generated flags.make -- the
# ARROW_HAVE_* macros are add_definitions(), not entries in arrow/util/config.h,
# so a probe built with a different set is a different program (the AVX2
# extern-template instantiations and ARROW_HAVE_SIMD_SPLIT both vanish).
set -euo pipefail
SRC=$1; BLD=$2; SRC_CC=$3; OUT=$4

FLAGS=$(find "$BLD" -name flags.make -path '*arrow_util.dir*' | head -1)
[ -n "$FLAGS" ] || { echo "build_probe: no flags.make under $BLD" >&2; exit 1; }

DEFS=$(sed -n 's/^CXX_DEFINES = //p' "$FLAGS")
INCS=$(sed -n 's/^CXX_INCLUDES = //p' "$FLAGS")

# Arrow's static libraries, plus whichever of its bundled/allocator archives
# this configuration produced.  Which ones exist depends on the option set --
# the allocator in particular is not optional: libarrow.a references
# mi_malloc_aligned when mimalloc is on, and the link fails without it.
LIBS=""
for a in "$BLD/release/libarrow.a" \
         "$BLD/release/libarrow_compute.a" \
         "$BLD/release/libparquet.a" \
         "$BLD/release/libarrow_acero.a" \
         "$BLD/release/libarrow_bundled_dependencies.a" \
         "$BLD"/mimalloc_ep/src/mimalloc_ep/lib/libmimalloc.a \
         "$BLD"/jemalloc_ep/dist/lib/libjemalloc_pic.a; do
    [ -f "$a" ] && LIBS="$LIBS $a"
done
[ -n "$LIBS" ] || { echo "build_probe: no Arrow static libraries under $BLD" >&2; exit 1; }

# The timing budget is a compile-time constant in the harness.  The shipped
# defaults (one 20 ms sample, median of 15) are for investigating a single
# kernel; the measurement times every scoped function twenty-five times over, so it
# asks for 5 ms samples and pools every sample from every repetition, taking the
# median per function per side -- repetitions buy far more than samples-per-run
# does on a contended host, which was measured.
TIME_OPTS="-DKBENCH_TARGET_NS=5000000.0 -DKBENCH_SAMPLES=7"

# Some images carry zlib's development package and some do not; nothing in the
# Arrow components this task builds actually needs zlib, so a link that fails on
# -lz is retried without it rather than making the harness buildable only in one
# of the two images.
link() {  # $1 = extra libs
    # shellcheck disable=SC2086
    g++ -O2 -std=c++20 $TIME_OPTS $DEFS $INCS \
        -o "$OUT" "$SRC_CC" \
        -Wl,--start-group $LIBS -Wl,--end-group \
        -lpthread -ldl -lrt -lm $1 -lthrift
}
link "-lz" || { echo "build_probe: retrying without -lz" >&2; link ""; }
