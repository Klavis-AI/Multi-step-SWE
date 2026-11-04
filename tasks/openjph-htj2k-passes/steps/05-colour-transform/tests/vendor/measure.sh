#!/usr/bin/env bash
# End-to-end timing, run inside the verifier on a held-out image.
#
#   measure.sh roundtrip <build-dir> <ppm>
#
# Prints milliseconds (best of N) for a reversible encode + decode round trip.
# Whole-program wall clock is one-sided noise, so best-of-N is the right
# aggregator here; the per-function measurement uses a median instead.
set -u

# roundtrip2 <buildA> <buildB> <ppm> [root|unprivileged]  -- interleaved, so a
# contended interval falls on both sides instead of inflating one of them.
# Prints "bestA bestB".
#
# The unprivileged form drops privileges for side B only: side B is the
# candidate's own build, which must not run as root (test.sh's helpers.sh has
# the same identity), while side A -- the ladder's floor -- is verifier-owned
# work and keeps uid 0.  Both sides go through the same launcher in that form,
# so the launcher's cost is shared and nothing else changes; keeping side A at
# uid 0 also keeps the candidate's processes from stopping or tracing the
# floor's timing -- it is the reference this comparison is drawn against.
#
# Each side writes its own temp files, so neither side's output can end up in
# the other's way -- a root-made file is not writable by the unprivileged side.
roundtrip2() {
    local a="$1" b="$2" ppm="$3" who="${4:-root}"
    local bestA=999999 bestB=999999 s e ms
    local rounds="${RT_ROUNDS:-5}"
    for _ in $(seq "$rounds"); do
        for pair in "A $a" "B $b"; do
            set -- $pair
            local tag="$1" build="$2" run=()
            # setpriv, not a shell function: this script is its own process and
            # cannot see the caller's helpers.sh.  When the drop is asked for,
            # BOTH sides go through the same two execs: the wrapper costs a
            # millisecond or so, and on one side only it would eat into the 5%
            # band the caller compares the pair with.  Side A keeps uid 0 --
            # the floor is verifier-owned work -- and side B runs as nobody.
            if [ "$who" = unprivileged ]; then
                if [ "$tag" = B ]; then
                    run=(setpriv --reuid 65534 --regid 65534 --clear-groups \
                                env HOME=/tmp TMPDIR=/tmp)
                else
                    run=(setpriv --reuid 0 --regid 0 --clear-groups \
                                env HOME=/tmp TMPDIR=/tmp)
                fi
            fi
            s=$(date +%s%N)
            "${run[@]}" "$build/src/apps/ojph_compress/ojph_compress" -i "$ppm" \
                -o "/tmp/rt2_$tag.j2c" -reversible true -num_decomps 5 \
                >/dev/null 2>&1 || { echo "ENCFAIL"; return 1; }
            "${run[@]}" "$build/src/apps/ojph_expand/ojph_expand" \
                -i "/tmp/rt2_$tag.j2c" -o "/tmp/rt2_$tag.ppm" \
                >/dev/null 2>&1 || { echo "DECFAIL"; return 1; }
            e=$(date +%s%N); ms=$(( (e - s) / 1000000 ))
            if [ "$tag" = "A" ]; then [ "$ms" -lt "$bestA" ] && bestA=$ms
            else [ "$ms" -lt "$bestB" ] && bestB=$ms; fi
        done
    done
    echo "$bestA $bestB"
}

if [ "${1:-}" = "roundtrip2" ]; then
    roundtrip2 "$2" "$3" "$4" "${5:-root}"
    exit $?
fi

mode="$1"; bld="$2"; ppm="$3"
C="$bld/src/apps/ojph_compress/ojph_compress"
E="$bld/src/apps/ojph_expand/ojph_expand"
j2c=$(mktemp /tmp/rt.XXXXXX.j2c); out=$(mktemp /tmp/rt.XXXXXX.ppm)
trap 'rm -f "$j2c" "$out"' EXIT

best=999999
for _ in 1 2 3 4 5; do
    s=$(date +%s%N)
    taskset -c 2 "$C" -i "$ppm" -o "$j2c" -reversible true -num_decomps 5 >/dev/null 2>&1 || { echo "ENCFAIL"; exit 1; }
    taskset -c 2 "$E" -i "$j2c" -o "$out" >/dev/null 2>&1 || { echo "DECFAIL"; exit 1; }
    e=$(date +%s%N)
    ms=$(( (e - s) / 1000000 ))
    [ "$ms" -lt "$best" ] && best=$ms
done
echo "$best"
