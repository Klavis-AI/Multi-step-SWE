#!/usr/bin/env bash
# Record the end-to-end behaviour suite against the untouched baseline.
#
#     record-e2e.sh [app-dir] [out-dir]
#
# Produces <out-dir>/expected-e2e.txt, the half of the regression contract that
# speaks the simulator's TCP protocol instead of libCryptoEngine's C entry
# points.  Nothing here predicts an answer: e2e.py is inputs only, and the
# expectation is whatever the baseline did with them.
#
# The engine half has record.sh, which refuses to emit a recording it cannot
# vouch for.  This half is held to the same refusals, for the same reasons:
#   * the suite is replayed REPLAYS times (100 by default) and every replay
#     must come out byte-identical, so a case whose answer moves between runs
#     cannot become an expectation -- two replays cannot see a 1-in-256 flip,
#     and a 1-in-256 flip is exactly what this suite turned out to have;
#   * every replay must carry the case count the first one had, and no replay
#     may contain a death, a stall, a raised exception or a sanitizer report --
#     checked on all REPLAYS of them, not just on the one that gets kept;
#   * every registered session must appear with at least one case;
#   * the instrument is rehearsed first against e2echeck.py, which recomputes
#     every digest in the recording with hashlib and requires them to agree;
#   * every clean control is paired with a synthetic failure -- a planted
#     unstable replay, a planted corrupted digest, a corrupted line and a
#     half-length replay -- and the check that passed on the real file must
#     fail on the planted one before its verdict is believed.
#
# PROVENANCE.  A recording is a fact about one tree built one way, and is only
# re-derivable if both are written down:
#
#   tree     the frozen baseline snapshot in build/baseline, which
#            make_baseline.sh extracts from
#            https://github.com/stwagnr/tpm2simulator at commit
#            e45324eba268723d39856111e7933c5c76238481 (2017-10-15), linking
#            the OpenSSL 1.0.2b vendored in that tree;
#   build    the configure below, i.e. cmake -S <app>/cmake -B <build>
#            -DSPEC_VERSION=116 followed by cmake --build -- the two lines the
#            instruction names and the verifier runs, and the same configure
#            record.sh's mkdrv.sh uses, so both halves of the contract come off
#            one compile and that compile is the one they are graded under
#            (mkdrv.sh's PROVENANCE has the measurement that forced this);
#   inputs   e2e.py's sessions over keys.json from genkeys.py, both derived
#            from labelled SHA-256 counter streams -- no RNG, no clock and no
#            machine-dependent value anywhere in the corpus;
#   replays  REPLAYS, 100 by default, every one of which has to come out
#            byte-identical before any of them is allowed to be an expectation.
#
# REPLAYS ARE SERIAL, AND THAT IS NOT A TUNING DECISION.  The simulator's NV
# store is the absolute path /tmp/NVChip (src/platform/NVMem.c:119,139), shared
# by every process in a mount namespace.  _plat__NVEnable truncates it, writes
# NV_MEMORY_SIZE bytes, and asserts that an existing file is exactly that long
# (:151) -- so a second simulator starting inside the same /tmp either aborts
# before it ever listens, or, worse, shares one process's NV with another's and
# answers from it.  The first version of this script ran four replays at a time
# in one container and replay 23 lost a whole session to that race; the count
# check caught it, but a count check is not a reason to keep a hazard that can
# also move an answer without changing a count.  Parallelism here belongs at the
# container level -- one replay per /tmp -- which is also exactly how the
# verifier replays this suite, so the recording is made the way it is graded.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
APP=${1:-$(cd "$HERE/../baseline" && pwd)}
OUT=${2:-$(cd "$HERE/.." && pwd)/record}
REPLAYS=${REPLAYS:-100}
PORT=${PORT:-22000}

mkdir -p "$OUT"
say() { printf '\n== %s\n' "$*"; }
die() { printf '\nrecord-e2e: %s\n' "$*" >&2; exit 1; }

# The NV path is shared, so anything else already driving a simulator in this
# namespace would corrupt this recording silently.  Refuse to start.
if pgrep -c '^Simulator$' >/dev/null 2>&1; then
    die "another Simulator is running in this container.  /tmp/NVChip is
shared by absolute path, so two recorders cannot share a namespace."
fi

say "key material"
python3 "$HERE/genkeys.py"

say "simulator build (the verifier's two lines)"
BLD=$OUT/e2ebuild
rm -rf "$BLD"; mkdir -p "$BLD"
cmake -S "$APP/cmake" -B "$BLD" -DSPEC_VERSION=116 \
      > "$OUT/e2e-build.log" 2>&1 \
    || { tail -40 "$OUT/e2e-build.log"; die "cmake configure failed"; }
cmake --build "$BLD" -j "${JOBS:-7}" >> "$OUT/e2e-build.log" 2>&1 \
    || { tail -60 "$OUT/e2e-build.log"; die "simulator build failed"; }
printf 'configure: CMAKE_C_FLAGS=[%s] BUILD_TYPE=[%s] generator=[%s]\n' \
    "$(awk -F= '/^CMAKE_C_FLAGS:/{print $2}' "$BLD/CMakeCache.txt")" \
    "$(awk -F= '/^CMAKE_BUILD_TYPE:/{print $2}' "$BLD/CMakeCache.txt")" \
    "$(awk -F= '/^CMAKE_GENERATOR:/{print $2}' "$BLD/CMakeCache.txt")"
SIM=$BLD/Simulator
[ -x "$SIM" ] || die "no Simulator at $SIM"

# Each replay gets its own port range.  Serial replays that reused one range
# would be binding on ports the previous replay's accepted sockets still hold in
# TIME_WAIT, which is an EADDRINUSE that looks exactly like a simulator that
# would not start.  A session takes two ports, and a session that has to restart
# its simulator takes two more; the stride is 64, which is headroom over the
# most any replay of this suite has ever consumed (21 starts, 42 ports).
replay() {                                      # replay <n> <out>
    python3 "$HERE/e2e.py" "$SIM" "$2" \
        --port $((PORT + ($1 % 150) * 64)) --stall 900 --quiet
}

RUNS=$(mktemp -d)
# Kept on failure: a replay that lost a session is the only evidence of why,
# and the first version of this script deleted it on the way out.
trap 'if [ "${OK:-0}" = 1 ]; then rm -rf "$RUNS"; else
        rm -rf "$OUT/e2e-failed"
        cp -r "$RUNS" "$OUT/e2e-failed" 2>/dev/null || true
        rm -rf "$RUNS"
        printf "kept the failing replays in %s/e2e-failed\n" "$OUT" >&2
      fi' EXIT

say "instrument rehearsal"
replay 0 "$RUNS/rehearse.txt"
python3 "$HERE/e2echeck.py" "$RUNS/rehearse.txt" \
    || die "the simulator this recording would be taken from does not agree
with hashlib about SHA.  Fix the build or the toolchain before trusting any
recording made with it -- this is the control, not an expectation."
# The clean control proves nothing unless it can fail.
awk '!hit && / t=hash-SHA256-64 / {sub(/out=[0-9a-f]+/, "out=00"); hit = 1}
     {print}' "$RUNS/rehearse.txt" > "$RUNS/planted.txt"
if python3 "$HERE/e2echeck.py" "$RUNS/planted.txt" --quiet >"$RUNS/pl.log" 2>&1
then
    die "e2echeck did not notice a planted wrong digest: it cannot be trusted
to have noticed a real one either."
fi
grep -q 'MISMATCH .* t=hash-SHA256-64' "$RUNS/pl.log" \
    || { cat "$RUNS/pl.log"; die "e2echeck failed on the planted recording but
did not name the case it planted in."; }
echo "fixture:     planted wrong digest caught, named hash-SHA256-64"
rm -f "$RUNS/planted.txt" "$RUNS/pl.log" "$RUNS/rehearse.txt"

# Name the session that lost cases rather than only the count that moved: 72
# absent cases is a number, "e2ecap recorded 0 of its 72" is a diagnosis.
lost() {                                        # lost <reference> <suspect>
    python3 - "$1" "$2" <<'PY'
import collections, sys


def fams(path):
    c = collections.Counter()
    for line in open(path, errors="replace"):
        if line.strip() and not line.startswith("#"):
            c[line.split("/", 1)[0]] += 1
    return c


want, got = fams(sys.argv[1]), fams(sys.argv[2])
for fam in sorted(want):
    if got[fam] != want[fam]:
        print("  %-16s recorded %d of its %d" % (fam, got[fam], want[fam]))
PY
}

say "suite, $REPLAYS replays, one at a time"
CASES=0
for n in $(seq 1 "$REPLAYS"); do
    replay "$n" "$RUNS/run$n.txt"
    got=$(grep -vc '^#' "$RUNS/run$n.txt")
    [ "$n" -eq 1 ] && CASES=$got
    if [ "$got" -ne "$CASES" ]; then
        lost "$RUNS/run1.txt" "$RUNS/run$n.txt"
        die "replay $n produced $got cases against $CASES in replay 1: a
session died, and a recording is not allowed to lose cases."
    fi
    if grep -nE '^# (session|stalled)|^# SAN' "$RUNS/run$n.txt" \
           >"$RUNS/bad$n.txt"; then
        head -20 "$RUNS/bad$n.txt"
        die "replay $n contains $(wc -l <"$RUNS/bad$n.txt") death(s), stall(s),
raised exception(s) or sanitizer report(s).  A crash is not a specification: a
correct port is free to return an error instead, so these cannot be scored."
    fi
    rm -f "$RUNS/bad$n.txt"
    printf '.'
    if [ $((n % 50)) -eq 0 ]; then printf ' %d\n' "$n"; fi
done
printf '\n'
echo "completeness: $CASES cases in each of $REPLAYS replays, no deaths"

say "recording checks"
# Every session e2e.py registers has to be in the recording.  Equal case counts
# across replays do not establish this on their own: a session that dies at its
# first command is missing from all of them equally.
for fam in $(python3 -c 'import sys; sys.dont_write_bytecode = True
sys.path.insert(0, "'"$HERE"'")
import e2e; print(" ".join(f for f, _ in e2e.SESSIONS))'); do
    c=$(grep -c "^$fam/" "$RUNS/run1.txt" || true)
    [ "$c" -gt 0 ] || die "session $fam registered but recorded no cases at all"
    printf '  %-16s %4d cases\n' "$fam" "$c"
done

# Name every case whose answer moved anywhere in the sweep, rather than
# stopping at the first difference.
unstable() {
    local n
    for n in $(seq 2 "$REPLAYS"); do
        diff "$RUNS/run1.txt" "$RUNS/run$n.txt" \
            | sed -n 's/^[<>] \([^# ][^ ]*\) .*/\1/p'
    done | sort -u
}
unstable >"$RUNS/unstable.txt"
if [ -s "$RUNS/unstable.txt" ]; then
    head -20 "$RUNS/unstable.txt"
    die "$(wc -l <"$RUNS/unstable.txt") case(s) gave different answers across
$REPLAYS replays.  Each is non-deterministic and must be fixed or dropped in
e2e.py -- an unstable answer is not an expectation."
fi
echo "stability:   $REPLAYS replays byte-identical"

PLANT=$(grep -v '^#' "$RUNS/run1.txt" | sed -n '3{s/ .*//;p}')
cp "$RUNS/run$REPLAYS.txt" "$RUNS/keep.txt"
awk -v c="$PLANT" '$1 == c {print $0 " wobble=1"; next} {print}' \
    "$RUNS/run1.txt" > "$RUNS/run$REPLAYS.txt"
[ "$(unstable)" = "$PLANT" ] \
    || die "the stability sweep did not notice a planted unstable answer
($PLANT): it cannot be trusted to have noticed a real one either."
mv "$RUNS/keep.txt" "$RUNS/run$REPLAYS.txt"
rm -f "$RUNS/unstable.txt"
echo "fixture:     planted unstable answer caught, named $PLANT"

cp "$RUNS/run1.txt" "$OUT/expected-e2e.txt"

say "comparator control"
python3 "$HERE/score.py" "$OUT/expected-e2e.txt" "$OUT/expected-e2e.txt" \
    --quiet | grep -qx 'reward=1.000000 ('"$CASES"'/'"$CASES"')' \
    || die "the comparator does not score the recording against itself as 1.0"
echo "control:     identical replay scores 1.0"

VICTIM=$(grep -v '^#' "$OUT/expected-e2e.txt" | sed -n '2{s/ .*//;p}')
awk -v c="$VICTIM" '$1 == c {print $0 " tampered=1"; next} {print}' \
    "$OUT/expected-e2e.txt" > "$OUT/e2e-mutant.txt"
python3 "$HERE/score.py" "$OUT/expected-e2e.txt" "$OUT/e2e-mutant.txt" \
    | grep -q "! $VICTIM" \
    || die "the comparator missed a corrupted line ($VICTIM): it cannot be
trusted to notice a real regression either."
echo "fixture:     one corrupted line caught, named $VICTIM"

grep -v '^#' "$OUT/expected-e2e.txt" | head -n $((CASES / 2)) \
    > "$OUT/e2e-short.txt"
short=$(python3 "$HERE/score.py" "$OUT/expected-e2e.txt" "$OUT/e2e-short.txt" \
        --quiet | sed 's/reward=\([0-9.]*\).*/\1/')
awk -v r="$short" 'BEGIN{exit !(r > 0.45 && r < 0.55)}' \
    || die "a half-length replay scored $short, not about 0.5: absent cases
are not being counted as failures."
echo "fixture:     half-length replay scores $short"
rm -f "$OUT/e2e-mutant.txt" "$OUT/e2e-short.txt"

OK=1
say "recorded"
printf 'expected-e2e.txt  %s cases, %s sessions\n' "$CASES" \
    "$(grep -v '^#' "$OUT/expected-e2e.txt" | cut -d/ -f1 | sort -u | wc -l)"
