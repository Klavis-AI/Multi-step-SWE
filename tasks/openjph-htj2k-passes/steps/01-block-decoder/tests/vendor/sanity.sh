#!/bin/bash
# sanity.sh -- build bench_stage against L0 (all SIMD stages stripped) and
# against L6 (all six stages), generate the codeblock fixtures with the L0
# build, and print, per fnid:
#
#   <fnid>  L0_median_ns  L6_median_ns  ratio  SAME|DIFF
#
# ratio is L0_median_ns / L6_median_ns, i.e. the L6 speed-up over L0; a value
# of 1.00 means the dispatch pointer was NOT installed with the SIMD kernel.
# SAME/DIFF compares the `dump` bytes of the two builds.
#
# The host is shared and oversubscribed, so two environmental controls are
# applied and reported: the runs are pinned to the logical CPU on which this
# harness itself is fastest, and each cell is the smallest median over REPS
# repeat runs (the median of R timed calls inside one run is the harness
# statistic; the repeat is only there to step around a neighbour's activity).
set -u
cd "$(dirname "$0")"

SEED=${SEED:-7}
REPS=${REPS:-3}
TMP=tmp_sanity
FXDIR=$TMP/fixtures
rm -rf "$TMP"
mkdir -p "$TMP/L0" "$TMP/L6" "$TMP/dump" "$FXDIR"

# The ladder trees are shared with other sessions; a tree that is being
# rebuilt has no libopenjph.so, and a run that straddles a rebuild measures
# nothing.  Refuse to start in that case and shout if it happens mid-run.
for bd in b-L0 b-L6; do
    if [ ! -e "$bd/src/core/libopenjph.so" ]; then
        echo "sanity.sh: $bd/src/core/libopenjph.so is missing -- that tree is" \
             "being rebuilt by another session; re-run when it is idle" >&2
        exit 1
    fi
done
so_id() { stat -c '%Y %s' "$1/src/core/libopenjph.so"; }
SO_BEFORE="$(so_id b-L0) $(so_id b-L6)"

bash runbench.sh L0 b-L0 "$TMP/L0" >/dev/null || exit 1
bash runbench.sh L6 b-L6 "$TMP/L6" >/dev/null || exit 1

# ---- fixtures, generated with the base (L0) build ------------------------
INC="-I L0/src/core/openjph -I L0/src/core/codestream \
     -I L0/src/core/transform -I L0/src/core/coding"
g++ -O2 -std=c++14 -o "$TMP/gen_fixtures" gen_fixtures.cpp $INC \
    -L b-L0/src/core -lopenjph -Wl,-rpath,"$PWD/b-L0/src/core" -ldl || exit 1
"$TMP/gen_fixtures" "$FXDIR" > "$TMP/fixtures.txt" || exit 1
echo "fixtures (generated with L0):"
cat "$TMP/fixtures.txt"

# ---- pick the quietest physical core -------------------------------------
# logical CPUs 0..7 are the first siblings of the 8 physical cores here, so
# one per physical core.  A neighbour can make one core several times slower
# than the next, and which core we sit on is an environmental control, not
# part of the measurement.
TASKSET=""
CORE="-"
NODE_CORE_VAL=""
if command -v taskset >/dev/null 2>&1; then
    for c in 0 1 2 3 4 5 6 7; do
        [ -d "/sys/devices/system/cpu/cpu$c" ] || continue
        v=""
        for i in 1 2; do
            w=$(taskset -c "$c" "$TMP/L0/bench_stage" rct_forward 1 bench 2>/dev/null)
            [ -z "$w" ] && continue
            if [ -z "$v" ] || [ "$w" -lt "$v" ]; then v=$w; fi
        done
        [ -z "$v" ] && continue
        if [ -z "$NODE_CORE_VAL" ] || [ "$v" -lt "$NODE_CORE_VAL" ]; then
            NODE_CORE_VAL=$v; CORE=$c
        fi
    done
    TASKSET="taskset -c $CORE"
    echo "pinned to logical cpu $CORE (rct_forward reference: $NODE_CORE_VAL ns)"
fi

mapfile -t FNIDS < <("$TMP/L0/bench_stage" list)

# bestof <binary> <fnid> <arg> : smallest of REPS medians
bestof()
{
    local best="" v r
    for ((r = 0; r < REPS; ++r)); do
        v=$($TASKSET "$1" "$2" "$3" bench 2>/dev/null)
        if [ -z "$v" ]; then echo ""; return 0; fi
        if [ -z "$best" ] || [ "$v" -lt "$best" ]; then best=$v; fi
    done
    echo "$best"
    return 0
}

echo
printf '%-22s %12s %12s %8s %s\n' fnid L0_ns L6_ns ratio SAME/DIFF
ndiff=0; nfail=0
for f in "${FNIDS[@]}"; do
    if [ "$f" = decode_cb32 ]; then
        arg=$FXDIR/$(basename "$(head -1 "$TMP/fixtures.txt" | awk '{print $1}')")
    else
        arg=$SEED
    fi

    a0=$(bestof "$TMP/L0/bench_stage" "$f" "$arg"); ra=$?
    a6=$(bestof "$TMP/L6/bench_stage" "$f" "$arg"); rb=$?
    : > "$TMP/e0"; : > "$TMP/e6"

    $TASKSET "$TMP/L0/bench_stage" "$f" "$arg" dump "$TMP/dump/L0_$f.bin" 2>>"$TMP/e0" || ra=99
    $TASKSET "$TMP/L6/bench_stage" "$f" "$arg" dump "$TMP/dump/L6_$f.bin" 2>>"$TMP/e6" || rb=99

    sz0=$(stat -c%s "$TMP/dump/L0_$f.bin" 2>/dev/null || echo 0)
    sz6=$(stat -c%s "$TMP/dump/L6_$f.bin" 2>/dev/null || echo 0)
    if cmp -s "$TMP/dump/L0_$f.bin" "$TMP/dump/L6_$f.bin"; then
        eq=SAME
    else
        off=$(cmp "$TMP/dump/L0_$f.bin" "$TMP/dump/L6_$f.bin" 2>&1 | head -1 |
              sed 's/.*byte \([0-9]*\).*/\1/')
        eq="DIFF@$((off-1))"; ndiff=$((ndiff+1))
    fi
    [ "$sz0" = 0 ] || [ "$sz6" = 0 ] && { eq="$eq EMPTY"; nfail=$((nfail+1)); }

    if [ $ra -ne 0 ] || [ $rb -ne 0 ]; then
        nfail=$((nfail+1))
        printf '%-22s %12s %12s %8s %s\n' "$f" "ERR($ra)" "ERR($rb)" - "$eq"
        sed -n 1,2p "$TMP/e0" "$TMP/e6" | sed 's/^/    /'
        continue
    fi
    ratio=$(awk -v x="$a0" -v y="$a6" 'BEGIN{printf "%.2f", (y>0 ? x/y : 0)}')
    printf '%-22s %12s %12s %8s %s\n' "$f" "$a0" "$a6" "$ratio" "$eq"
done

echo
echo "fnids: ${#FNIDS[@]}   DIFF dumps: $ndiff   failures: $nfail"

# ---- every generated fixture must decode in both builds ------------------
echo
echo "fixtures: decode check (both builds must return true, dumps must agree)"
nfx=0
while read -r path w h len1; do
    nfx=$((nfx+1))
    ok0=ok; ok6=ok
    $TASKSET "$TMP/L0/bench_stage" decode_cb32 "$path" dump "$TMP/dump/fx0.bin" \
        2>"$TMP/e0" || ok0=FAIL
    $TASKSET "$TMP/L6/bench_stage" decode_cb32 "$path" dump "$TMP/dump/fx6.bin" \
        2>"$TMP/e6" || ok6=FAIL
    if cmp -s "$TMP/dump/fx0.bin" "$TMP/dump/fx6.bin"; then
        eq=SAME
    else
        off=$(cmp "$TMP/dump/fx0.bin" "$TMP/dump/fx6.bin" 2>&1 | head -1 |
              sed 's/.*byte \([0-9]*\).*/\1/')
        eq="DIFF@$((off-1))"
    fi
    if [ "$ok0$ok6" = okok ] && [ "$eq" = SAME ]; then
        printf '  %-34s %3ux%-4u len1=%-4s decode L0=ok L6=ok  %s\n' \
               "$(basename "$path")" "$w" "$h" "$len1" "$eq"
    else
        nfail=$((nfail+1))
        printf '  %-34s %3ux%-4u len1=%-4s decode L0=%s L6=%s  %s\n' \
               "$(basename "$path")" "$w" "$h" "$len1" "$ok0" "$ok6" "$eq"
        sed -n 1,2p "$TMP/e0" "$TMP/e6" | sed 's/^/      /'
    fi
done < "$TMP/fixtures.txt"
echo "  fixtures: $nfx"

# ---- repeat-run stability of one binary ----------------------------------
# Criterion: repeating `bench` on the SAME binary must not move the number
# around.  N runs of a mid-sized fnid, each of which is already a median of
# thousands of timed calls; spread is (max-min)/min.
STAB_RUNS=${STAB_RUNS:-10}
STAB_FNIDS=${STAB_FNIDS:-"decode_cb32 ict_forward"}
echo
echo "repeat-run stability ($STAB_RUNS runs of the same binary, cpu $CORE):"
printf '%-22s %5s %10s %10s %10s %8s\n' fnid build min_ns med_ns max_ns spread
for f in $STAB_FNIDS; do
    if [ "$f" = decode_cb32 ]; then
        arg=$FXDIR/$(basename "$(head -1 "$TMP/fixtures.txt" | awk '{print $1}')")
    else
        arg=$SEED
    fi
    for b in L0 L6; do
        : > "$TMP/stab.txt"
        for ((i = 0; i < STAB_RUNS; ++i)); do
            $TASKSET "$TMP/$b/bench_stage" "$f" "$arg" bench >> "$TMP/stab.txt" || true
        done
        awk -v fn="$f" -v bd="$b" '
            { v[n++] = $1 }
            END {
                if (n == 0) { printf "%-22s %5s %10s %10s %10s %8s\n", fn, bd,
                                      "ERR", "ERR", "ERR", "-"; exit }
                for (i = 0; i < n; ++i) for (j = i+1; j < n; ++j)
                    if (v[j] < v[i]) { t = v[i]; v[i] = v[j]; v[j] = t }
                med = (n % 2) ? v[int(n/2)] : (v[n/2-1] + v[n/2]) / 2
                printf "%-22s %5s %10d %10d %10d %7.1f%%\n", fn, bd, v[0], med,
                       v[n-1], 100.0 * (v[n-1] - v[0]) / v[0]
            }' "$TMP/stab.txt"
    done
done

[ $nfail -eq 0 ] || exit 1
if [ "$SO_BEFORE" != "$(so_id b-L0) $(so_id b-L6)" ]; then
    echo "sanity.sh: a ladder library changed while the table was being" \
         "measured -- the numbers above are not trustworthy" >&2
    exit 2
fi
exit 0
