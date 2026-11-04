#!/usr/bin/env bash
# Drive record-e2e.sh as N container-level groups instead of one long serial run.
#
#     record-e2e-groups.sh [groups] [replays-per-group]      (default 4 x 25)
#
# WHY THIS EXISTS, AND WHY IT IS NOT A WEAKENING OF THE 100-REPLAY RULE.
#
# record-e2e.sh replays the suite REPLAYS times inside one container, requires
# every replay to be byte-identical to replay 1, and keeps replay 1 as the
# recording.  Its own PROVENANCE block says why the replays inside a container
# are serial -- /tmp/NVChip is an absolute path, so two simulators in one mount
# namespace either abort or answer from each other's NV -- and says in as many
# words where the parallelism does belong: "at the container level -- one replay
# per /tmp -- which is also exactly how the verifier replays this suite".
#
# So: G containers, R replays each, G*R = 100.  What comes out is exactly the
# same guarantee, by transitivity and not by approximation:
#
#   within group g   runs 1..R are byte-identical to group g's kept file
#                    (record-e2e.sh's own stability sweep, unchanged, and its
#                    own planted-unstable fixture still has to catch a wobble
#                    inside each group before that sweep is believed)
#   across groups    the G kept files are byte-identical (checked below)
#   therefore        all G*R replays are byte-identical to one another
#
# and it is stronger in two ways that cost nothing: the groups run in G distinct
# mount namespaces with G distinct /tmp and G distinct port ranges, and each
# group builds the tree from source and derives keys.json from genkeys.py
# independently, so a recording that depended on the build directory, on the
# port numbers, on NV left behind by a previous replay, or on one particular
# keys.json would differ across groups instead of agreeing with itself.
#
# Each group gets its own copy of suite/ and baseline/ because genkeys.py writes
# suite/keys.json: four containers sharing one mount would race on it.  That is
# the same reason the engine half was recorded out of /work/rec-engine.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$(cd "$HERE/.." && pwd)
NGROUP=${1:-4}
REPLAYS=${2:-25}
IMAGE=${IMAGE:-tpmbase:bookworm}
SCRATCH=${SCRATCH:-/work/rece2e}
JOBS=${JOBS:-2}

say() { printf '\n== %s\n' "$*"; }
die() { printf '\nrecord-e2e-groups: %s\n' "$*" >&2; exit 1; }

command -v docker >/dev/null || die "no docker"
docker image inspect "$IMAGE" >/dev/null 2>&1 || die "no image $IMAGE"

if docker ps --format '{{.Names}}' | grep -q '^m30rece2e'; then
    die "a recorder container is already running; stop it first"
fi

say "$NGROUP groups x $REPLAYS replays = $((NGROUP * REPLAYS)) replays"
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"

for g in $(seq 1 "$NGROUP"); do
    d=$SCRATCH/g$g
    mkdir -p "$d/record"
    cp -a "$BUILD/suite"    "$d/suite"
    cp -a "$BUILD/baseline" "$d/baseline"
    rm -f "$d/suite/keys.json"          # each group derives its own
    docker run -d --name "m30rece2e-g$g" --cpus 2 \
        -v "$d:/w" -w /w \
        -e REPLAYS="$REPLAYS" -e PORT=$((22000 + g * 2000)) -e JOBS="$JOBS" \
        "$IMAGE" bash -c \
        'bash suite/record-e2e.sh /w/baseline /w/record > /w/record.log 2>&1;
         echo "EXIT=$?" >> /w/record.log' >/dev/null
    echo "  group $g  container m30rece2e-g$g  port base $((22000 + g * 2000))"
done

say "waiting"
while docker ps --format '{{.Names}}' | grep -q '^m30rece2e-g'; do
    printf '\r  %s  %s' "$(date -u +%H:%M:%S)" \
        "$(for g in $(seq 1 "$NGROUP"); do
               printf '%s ' "g$g:$(tail -1 "$SCRATCH/g$g/record.log" \
                                   2>/dev/null | tr -cd '.' | wc -c)"
           done)"
    sleep 20
done
printf '\n'

say "group results"
bad=0
for g in $(seq 1 "$NGROUP"); do
    st=$(grep -c '^EXIT=0$' "$SCRATCH/g$g/record.log" || true)
    f=$SCRATCH/g$g/record/expected-e2e.txt
    if [ "$st" != 1 ] || [ ! -s "$f" ]; then
        echo "  group $g  FAILED"; tail -15 "$SCRATCH/g$g/record.log"; bad=1
    else
        printf '  group %s  %s cases  %s\n' "$g" \
            "$(grep -vc '^#' "$f")" "$(sha256sum "$f" | cut -c1-16)"
    fi
done
[ "$bad" = 0 ] || die "at least one group did not record"

say "cross-group identity"
ref=$SCRATCH/g1/record/expected-e2e.txt
for g in $(seq 2 "$NGROUP"); do
    if ! cmp -s "$ref" "$SCRATCH/g$g/record/expected-e2e.txt"; then
        diff "$ref" "$SCRATCH/g$g/record/expected-e2e.txt" | head -20
        die "group $g disagrees with group 1.  Every replay in every group has
to be byte-identical or this is not an expectation."
    fi
done
echo "  $NGROUP group files byte-identical"

# The clean control proves nothing unless it can fail: plant a difference in a
# copy of one group's file and require the same comparison to reject it.
tmp=$(mktemp)
awk 'NR == 3 && !/^#/ {print $0 " wobble=1"; next} {print}' "$ref" > "$tmp"
if cmp -s "$ref" "$tmp"; then
    rm -f "$tmp"; die "the planted difference did not land"
fi
rm -f "$tmp"
echo "  fixture: a planted one-line difference is rejected by the same cmp"

say "recorded"
printf 'expected-e2e.txt  %s cases, %s sessions\n' \
    "$(grep -vc '^#' "$ref")" \
    "$(grep -v '^#' "$ref" | cut -d/ -f1 | sort -u | wc -l)"
echo "sha256 $(sha256sum "$ref" | cut -d' ' -f1)"
echo
echo "not installed automatically -- inspect it, then:"
echo "  cp $ref $BUILD/record/expected-e2e.txt"
