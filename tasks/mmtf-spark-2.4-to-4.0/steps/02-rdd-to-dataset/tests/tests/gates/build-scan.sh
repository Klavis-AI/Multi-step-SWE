#!/usr/bin/env bash
# Build a tree twice and write down what happened, then hand the notes to
# build-scan.py.
#
# Two builds rather than one, because the question is reproducibility and one
# build cannot answer it. `clean` between them, because the question is whether
# the *build* is reproducible and not whether Maven skips work it has already
# done.
#
# Online by default, unlike pom-scan.sh. That scanner asks Maven to describe a
# dependency graph the verifier's warm cache was built from, so a download
# means the warm layer stopped working and should be loud. This one compiles
# whatever the candidate wrote, and a candidate that pinned a different -- but
# perfectly legitimate -- plugin version must not fail the gate because the
# author's cache had never heard of it. Rule 9 gives the verifier a public
# network for exactly this. BUILD_SCAN_OFFLINE=1 forces `-o`, for rehearsing
# the cold case.
#
# -DskipTests, deliberately. "Green" is already measured twice in the behavior
# family -- 140 replayed jobs need a working uber jar, and the scorer runs the
# repository's own tests from its own pristine copy -- so paying for two more
# full test runs here buys nothing and costs eight minutes of a thirty-minute
# budget (rule 10).
#
# Exits 0 always. A tree that does not build is something the gate reports.
#
#   usage: build-scan.sh <tree> [<workdir>]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=${1:?usage: build-scan.sh <tree> [workdir]}
WORK=${2:-$(mktemp -d /tmp/build-scan.XXXXXX)}
ROUNDS=${BUILD_SCAN_ROUNDS:-2}
OFF=
[ "${BUILD_SCAN_OFFLINE:-0}" = 1 ] && OFF=-o

mkdir -p "$WORK"

# The tree's .mvn/{jvm,maven}.config and any ~/.mavenrc or /tmp/.mavenrc do
# not apply to this build: the POMs alone describe it (root-tool-config sweep,
# 2026-10-13). The base directory is an empty one of ours and rc files are off.
mkdir -p "$WORK/basedir"
export MAVEN_SKIP_RC=1 MAVEN_BASEDIR="$WORK/basedir"
OBS=$WORK/observed.tsv
: > "$OBS"

note() { printf '%s\n' "$*" >> "$OBS"; }

if [ ! -f "$TREE/pom.xml" ]; then
    note "$(printf 'error\tpom.xml\tno pom.xml under %s' "$TREE")"
    exec python3 -s -E "$HERE/build-scan.py" "$OBS"
fi
command -v mvn > /dev/null 2>&1 || {
    note "$(printf 'error\tmvn\tnot on PATH')"
    exec python3 -s -E "$HERE/build-scan.py" "$OBS"
}

# The marker's mtime is the "before" the stray check compares against. A
# content hash would not do: maven-shade-plugin rewrites
# dependency-reduced-pom.xml with byte-identical content on the untouched tree,
# so the only thing that moves is the timestamp.
MARK=$WORK/mark
touch "$MARK"

# Entry count as well as digest. Two jars with the same digest are the same
# jar, so the count adds nothing there -- but a jar that lost 4000 entries and
# is reproducibly tiny would otherwise report `same` and say nothing else, and
# the count is what makes that visible in the evidence a reviewer reads.
digest_jars() {
    local round=$1
    python3 -I - "$TREE/target" "$round" <<'PY' >> "$OBS"
import hashlib, os, sys, zipfile
target, rnd = sys.argv[1], sys.argv[2]
if not os.path.isdir(target):
    raise SystemExit(0)
for name in sorted(os.listdir(target)):
    if not name.endswith('.jar'):
        continue
    path = os.path.join(target, name)
    with open(path, 'rb') as fh:
        digest = hashlib.sha256(fh.read()).hexdigest()
    try:
        with zipfile.ZipFile(path) as zf:
            entries = len(zf.namelist())
    except Exception as exc:                       # a truncated jar is a fact
        print('error\t%s\t%s' % (name, str(exc)[:120].replace('\t', ' ')))
        continue
    print('jar\t%s\t%s\t%s\t%d' % (rnd, name, digest, entries))
PY
}

r=1
while [ "$r" -le "$ROUNDS" ]; do
    log=$WORK/round$r.log
    start=$(date +%s)
    # Bounded, because this runs the tree's own pom: a round that never ends
    # would hold the verifier past its limit. 900 s is ~60x a measured round.
    ( cd "$TREE" && timeout -k 30 900 mvn -B $OFF clean package -DskipTests ) > "$log" 2>&1
    rc=$?
    note "$(printf 'round\t%d\t%d\t%d' "$r" "$rc" "$(( $(date +%s) - start ))")"
    if [ "$rc" != 0 ]; then
        # Flattened to one line, same contract as pom-scan.sh: a fact is a
        # line, and a Maven stack trace is not one.
        note "$(printf 'error\tmvn:round%d\t%s' "$r" \
            "$(grep -m1 '\[ERROR\]' "$log" 2>/dev/null \
               | tr '\t\n' '  ' | cut -c1-200)")"
    fi
    digest_jars "$r"
    r=$((r + 1))
done

# target/ is where a build is supposed to write. .git is Harbor's or nobody's.
# Everything else that moved, the build moved.
find "$TREE" -newer "$MARK" -type f \
     -not -path "$TREE/target/*" -not -path "$TREE/.git/*" 2>/dev/null \
    | sed "s#^$TREE/##" | sort \
    | while IFS= read -r p; do note "$(printf 'stray\t%s' "$p")"; done

exec python3 -s -E "$HERE/build-scan.py" "$OBS"
