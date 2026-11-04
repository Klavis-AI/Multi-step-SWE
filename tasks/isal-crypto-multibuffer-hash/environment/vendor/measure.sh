#!/usr/bin/env bash
#
# measure.sh -- practice speed measurement for one multi-buffer hashing
# stage of the isa-l_crypto tree under /app/isal_crypto.
#
#   /app/tools/measure.sh <stage>
#
# <stage> is one of
#
#   sha256_mb sha1_mb sha512_mb md5_mb sm3_mb
#   mh_sha1 mh_sha1_murmur3_x64_128 mh_sha256 rolling_hash
#
# The script builds the tree, compiles practice.c once for the requested
# stage against the library that build just produced, and runs it.  The
# table it prints has one line per scored unit:
#
#   unit   installed_ns   reference_ns   ratio
#
# installed_ns is what the public API costs right now, reference_ns is
# what the portable C reference for the same unit costs, ratio is
# reference_ns / installed_ns (higher is better).  Nothing in the output
# depends on the number of CPUs.

set -u

STAGES="sha256_mb sha1_mb sha512_mb md5_mb sm3_mb mh_sha1 mh_sha1_murmur3_x64_128 mh_sha256 rolling_hash"

usage() {
    echo "usage: $0 <stage>" >&2
    echo "  stage is one of: $STAGES" >&2
    exit 2
}

stage="${1:-}"
if [ "$#" -ne 1 ]; then
    usage
fi

case " $STAGES " in
    *" $stage "*) ;;
    *) echo "measure.sh: unknown stage '$stage'" >&2; usage ;;
esac

# Overridable so that the same script can be pointed at another checkout
# (the defaults are the paths inside the task image).
tree="${ISA_L_TREE:-/app/isal_crypto}"
practice_c="${PRACTICE_C:-/app/tools/practice.c}"
log="${PRACTICE_LOG:-/tmp/isal_crypto_build.log}"
binary="${PRACTICE_BIN:-/tmp/practice_${stage}}"

macro="PRACTICE_$(echo "$stage" | tr '[:lower:]' '[:upper:]')"

if [ ! -f "$tree/Makefile.unx" ]; then
    echo "measure.sh: no isa-l_crypto tree at $tree (expected $tree/Makefile.unx)" >&2
    exit 1
fi
if [ ! -f "$practice_c" ]; then
    echo "measure.sh: no practice.c at $practice_c" >&2
    exit 1
fi

# 1. Build the tree.  The stage directories carry their own include path,
#    so the compiler also needs the one for this stage.
echo "measure.sh: building $tree (log: $log)" >&2
if ! (cd "$tree" && make -f "$tree/Makefile.unx" lib -j"$(nproc)") >"$log" 2>&1; then
    echo "measure.sh: the isa-l_crypto tree does not build; tail of $log:" >&2
    tail -n 40 "$log" >&2
    exit 1
fi

# 2. Compile the practice tool for this stage against the freshly built
#    library.  Both the public headers and the stage's private headers
#    are needed.
if ! gcc -O2 -D"$macro" "$practice_c" -o "$binary" \
    -I "$tree" \
    -I "$tree/include" \
    -I "$tree/include/isa-l_crypto" \
    -I "$tree/include/internal" \
    -I "$tree/$stage" \
    "$tree/bin/isa-l_crypto.a"; then
    echo "measure.sh: could not compile $practice_c for $stage" >&2
    exit 1
fi

# 3. Run it.  practice.c prints the table; its exit status is passed on
#    so a failed measurement cannot be mistaken for a slow one.
"$binary"
status=$?
if [ "$status" -ne 0 ]; then
    echo "measure.sh: the $stage measurement failed (status $status)" >&2
fi
exit "$status"
