#!/usr/bin/env bash
# Record the behaviour suite against the untouched baseline.
#
#     record.sh [app-dir] [out-dir]
#
# Produces <out-dir>/expected.txt, the regression contract every step is scored
# against.  Nothing here predicts an answer: the corpus is inputs only, and the
# expectation is whatever the baseline did with them.
#
# It refuses to produce a recording that cannot be trusted:
#   * the corpus is replayed REPLAYS times (300 by default) and every replay
#     must come out byte-identical, so a case whose answer moves between runs
#     cannot become an expectation -- two replays cannot see a 1-in-256 flip;
#   * every case must produce a line -- a short result file means the driver
#     died, and a missing expectation would otherwise silently shrink the set;
#   * no line may carry signal=, died=1, argtrunc=1 or unknownop=1;
#   * the instrument is rehearsed first against smoke.expected, whose answers
#     are known independently of the library (FIPS-197, SP 800-38A, RFC 2202,
#     RFC 4231, the NIST curve generators, plain integer arithmetic);
#   * the comparator is shown to fail on a deliberately corrupted recording
#     before its verdict on a real one is believed;
#   * line coverage of the engine sources must clear the bar.
#
# PROVENANCE.  A recording is a fact about one tree built one way, and is only
# re-derivable if both are written down:
#
#   tree     the frozen baseline snapshot in build/baseline, which
#            make_baseline.sh extracts from
#            https://github.com/stwagnr/tpm2simulator at commit
#            e45324eba268723d39856111e7933c5c76238481 (2017-10-15), linking
#            the OpenSSL 1.0.2b vendored in that tree;
#   build    mkdrv.sh, i.e. cmake -S <app>/cmake -B <build> -DSPEC_VERSION=116,
#            which is the verifier's own configure line -- see mkdrv.sh's
#            PROVENANCE for why the recording is taken under the compile it is
#            graded under rather than under a chosen optimisation level;
#   inputs   cases.txt from gencases.py over keys.json from genkeys.py, both
#            derived from labelled SHA-256 counter streams -- no RNG, no clock
#            and no machine-dependent value anywhere in the corpus;
#   replays  REPLAYS, 300 by default, every one of which has to come out
#            byte-identical before any of them is allowed to be an expectation.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
APP=${1:-$(cd "$HERE/../baseline" && pwd)}
OUT=${2:-$(cd "$HERE/.." && pwd)/record}
COV_MIN=${COV_MIN:-75}

mkdir -p "$OUT"
say() { printf '\n== %s\n' "$*"; }
die() { printf '\nrecord: %s\n' "$*" >&2; exit 1; }

say "key material"
python3 "$HERE/genkeys.py"

say "case generation"
python3 "$HERE/gencases.py"
CASES="$HERE/cases.txt"
NCASES=$(grep -cve '^[[:space:]]*#' -e '^[[:space:]]*$' "$CASES")

say "driver build (the verifier's configure)"
rm -rf "$OUT/build"
JOBS=${JOBS:-7} "$HERE/mkdrv.sh" "$APP" "$OUT/build" >"$OUT/build.log" 2>&1 \
    || { tail -40 "$OUT/build.log"; die "driver build failed"; }
DRV="$OUT/build/cpridrv"

say "instrument rehearsal"
python3 "$HERE/runner.py" "$DRV" "$HERE/smoke.txt" "$OUT/smoke.txt" --quiet
if [ -f "$HERE/smoke.expected" ]; then
    diff -u "$HERE/smoke.expected" "$OUT/smoke.txt" \
        || die "the driver no longer reproduces the known answers in
smoke.expected.  Fix the driver or the toolchain before trusting any
recording made with it -- this file is the control, not an expectation."
    echo "smoke: $(wc -l <"$OUT/smoke.txt") rehearsal cases reproduced"
else
    cp "$OUT/smoke.txt" "$HERE/smoke.expected"
    echo "smoke: no control on file; froze this run as smoke.expected."
    echo "       Hand-verify it before relying on it."
fi

# How many times the corpus is replayed before a recording is believed.  Two is
# not enough and the number is not a matter of taste: a case that flips once in
# 256 runs survives a two-run diff about 99.6% of the time, and three such cases
# did survive it here -- an entropy self-test in _cpri__Startup, an ECDSA round
# trip sunk by the digest truncation at CpriECC.c:1909, and a one-byte draw
# compared against another one-byte draw.  All three were found by sweeping, not
# by diffing twice, and the sweep is what stays.
REPLAYS=${REPLAYS:-300}

# The replays are scratch -- hundreds of copies of the corpus are evidence of
# nothing once they agree, and the one that survives becomes expected.txt.
RUNS=$(mktemp -d)
trap 'rm -rf "$RUNS"' EXIT

say "corpus, $REPLAYS replays"
for n in $(seq 1 "$REPLAYS"); do
    python3 "$HERE/runner.py" "$DRV" "$CASES" "$RUNS/run$n.txt" --quiet
    got=$(wc -l <"$RUNS/run$n.txt")
    [ "$got" -eq "$NCASES" ] \
        || die "replay $n produced $got lines for $NCASES cases: the driver died
and the runner could not recover every case."
    printf '.'
    [ $((n % 50)) -eq 0 ] && printf ' %d\n' "$n"
done
printf '\n'
echo "completeness: $NCASES cases, $NCASES results, in each of $REPLAYS replays"

say "recording checks"
# Name every case whose answer moved anywhere in the sweep, rather than stopping
# at the first difference: a corpus with three unstable cases should say three.
unstable() {
    local n
    for n in $(seq 2 "$REPLAYS"); do
        diff "$RUNS/run1.txt" "$RUNS/run$n.txt" \
            | sed -n 's/^[<>] \([^ ]*\) .*/\1/p'
    done | sort -u
}
unstable >"$RUNS/unstable.txt"
if [ -s "$RUNS/unstable.txt" ]; then
    head -20 "$RUNS/unstable.txt"
    die "$(wc -l <"$RUNS/unstable.txt") case(s) gave different answers across
$REPLAYS replays.  Each is non-deterministic and must be fixed or dropped in
gencases.py -- an unstable answer is not an expectation."
fi
echo "stability:   $REPLAYS replays byte-identical"

# The clean sweep above proves nothing unless the sweep can fail.  Plant one
# altered replay and require the same comparison to name exactly that case.
PLANT=$(sed -n '3{s/ .*//;p}' "$RUNS/run1.txt")
cp "$RUNS/run$REPLAYS.txt" "$RUNS/keep.txt"
sed '3s/$/ wobble=1/' "$RUNS/run1.txt" >"$RUNS/run$REPLAYS.txt"
[ "$(unstable)" = "$PLANT" ] \
    || die "the stability sweep did not notice a planted unstable answer
($PLANT): it cannot be trusted to have noticed a real one either."
mv "$RUNS/keep.txt" "$RUNS/run$REPLAYS.txt"
rm -f "$RUNS/unstable.txt"
echo "fixture:     planted unstable answer caught, named $PLANT"

if grep -nE 'signal=|died=1|argtrunc=1|unknownop=1' "$RUNS/run1.txt" >"$OUT/bad.txt"; then
    head -20 "$OUT/bad.txt"
    die "$(wc -l <"$OUT/bad.txt") case(s) crashed, truncated an argument, or
named an op the driver does not implement.  A crash is not a specification: a
correct port is free to return an error instead, so these cannot be scored."
fi
rm -f "$OUT/bad.txt"
echo "health:      no crashes, truncations or unknown ops"

cp "$RUNS/run1.txt" "$OUT/expected.txt"

say "comparator control"
# A clean control proves nothing on its own; each is paired with a synthetic
# failure that the comparator must catch.
python3 "$HERE/score.py" "$OUT/expected.txt" "$OUT/expected.txt" --quiet \
    | grep -qx 'reward=1.000000 ('"$NCASES"'/'"$NCASES"')' \
    || die "the comparator does not score the recording against itself as 1.0"
echo "control:     identical replay scores 1.0"

VICTIM=$(sed -n '2{s/ .*//;p}' "$OUT/expected.txt")
sed '2s/$/ tampered=1/' "$OUT/expected.txt" >"$OUT/mutant.txt"
python3 "$HERE/score.py" "$OUT/expected.txt" "$OUT/mutant.txt" \
    | grep -q "! $VICTIM" \
    || die "the comparator missed a corrupted line ($VICTIM): it cannot be
trusted to notice a real regression either."
echo "fixture:     one corrupted line caught, named $VICTIM"

head -n $((NCASES / 2)) "$OUT/expected.txt" >"$OUT/short.txt"
short=$(python3 "$HERE/score.py" "$OUT/expected.txt" "$OUT/short.txt" --quiet \
        | sed 's/reward=\([0-9.]*\).*/\1/')
awk -v r="$short" 'BEGIN{exit !(r > 0.45 && r < 0.55)}' \
    || die "a half-length replay scored $short, not about 0.5: absent cases
are not being counted as failures."
echo "fixture:     half-length replay scores $short"
rm -f "$OUT/mutant.txt" "$OUT/short.txt"

say "coverage build (--coverage)"
rm -rf "$OUT/cov"
JOBS=${JOBS:-7} "$HERE/mkdrv.sh" "$APP" "$OUT/cov" --coverage \
    >"$OUT/cov-build.log" 2>&1 \
    || { tail -40 "$OUT/cov-build.log"; die "coverage build failed"; }
python3 "$HERE/runner.py" "$OUT/cov/cpridrv" "$HERE/smoke.txt" \
    "$OUT/cov/smoke.txt" --quiet
python3 "$HERE/runner.py" "$OUT/cov/cpridrv" "$CASES" "$OUT/cov/cases.txt" --quiet

say "line coverage of $APP/src/OsslCryptoEngine"
python3 "$HERE/coverage.py" "$OUT/cov" "$APP/src/OsslCryptoEngine" \
    --min "$COV_MIN" | tee "$OUT/coverage.txt"

say "recorded"
printf 'expected.txt  %s cases\ncoverage      %s\n' \
    "$NCASES" "$(awk '/TOTAL/{print $2}' "$OUT/coverage.txt")"
