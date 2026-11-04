#!/usr/bin/env bash
# Shared grading machinery. Per-step copy; all six copies are identical.
#
# Every function here works on /grade, never on /app directly: a check that
# mutated the snapshot would change what the next check sees, and the order of
# the checks is not part of the task.
set -u

GRADE=/grade
GRADE_APP=$GRADE/app
GRADE_PRISTINE=$GRADE/pristine
GRADE_LOG=$GRADE/log

# Where the runner, the corpus and the frozen expectations are unpacked. Under
# /grade rather than /tests so nothing candidate-reachable ever sees them, and
# so a probe that chdir()s cannot write into the read-only image layer.
GRADE_SUITE=$GRADE/suite

grade_prepare() {
    rm -rf "$GRADE"
    mkdir -p "$GRADE_APP" "$GRADE_PRISTINE" "$GRADE_LOG" "$GRADE_SUITE"
    cp -a /app/. "$GRADE_APP"/ 2>/dev/null || true
    # Set-id files. Candidate code runs as nobody in this container, and
    # `cp -a` keeps a root-owned setuid or setgid bit. The chown in
    # give_app_to_unprivileged clears them on the copy too, but only once it
    # runs; this clears them here. /app itself is never read again
    # except by root, so it is locked to root rather than left for a nobody
    # process to find a set-id file in.
    find "$GRADE_APP" -xdev -type f -perm /6000 -exec chmod ug-s {} + 2>/dev/null
    chmod 0700 /app 2>/dev/null || true
    tar -C "$GRADE_PRISTINE" -xzf /tests/data/pristine.tar.gz
    cp /tests/lib/replay.py "$GRADE_SUITE"/
    cp /tests/data/corpus.json "$GRADE_SUITE"/
    cp /tests/data/globals-fixtures.json "$GRADE_SUITE"/
    gzip -dc /tests/data/expected.json.gz > "$GRADE_SUITE/expected.json"
    gzip -dc /tests/data/globals.json.gz > "$GRADE_SUITE/globals-expected.json"
    gzip -dc /tests/data/defects.json.gz > "$GRADE_SUITE/defects.json"
    chmod -R a+rX "$GRADE_SUITE" "$GRADE_PRISTINE"
    # The answers are root's. Candidate code runs as nobody in this container --
    # in-process with the replay, among other places -- and only the root-run
    # `compare` and ledger checks read these three files, so nobody can copy an
    # expected value into an observation.
    chmod 600 "$GRADE_SUITE/expected.json" "$GRADE_SUITE/globals-expected.json" \
        "$GRADE_SUITE/defects.json"
    # Sticky, so that nothing running as nobody can unlink or rename a file
    # root wrote here and put its own in its place before root reads it back
    # (a process a build backend left behind outlives the step that started
    # it).
    chmod 1777 "$GRADE_LOG"
    # Score from a directory the unprivileged user can read. Every tool below
    # inherits this shell's cwd, and the image's WORKDIR is /app -- which is
    # whatever the artifact restore left behind, in practice root-owned 0700.
    # That is enough to make `twine check` die inside docutils, which probes a
    # relative `minimal.css` and gets EACCES instead of ENOENT. The failure
    # looks like a candidate whose long description does not render.
    chmod 755 "$GRADE"
    cd "$GRADE"
}

# The empty-/app trap. A verifier container handed an empty workspace scores 0
# on every check, and that 0 is indistinguishable from a candidate who did
# nothing -- so a broken run reads as a real measurement. `/app` is never empty
# for any candidate: harbor copies the step workdir into it before the agent
# starts, so even a candidate that deletes the entire repository leaves the
# instruction's own reference documents behind. An empty tree therefore means
# the artifact restore failed, and the run is marked as such.
#
# Prints the fault and returns 1; the caller reports 0 and stops. The 0 is still
# a 0, but the log now says which kind.
app_is_present() {
    if [ -n "$(ls -A "$GRADE_APP" 2>/dev/null)" ]; then
        return 0
    fi
    printf 'HARNESS FAULT: /app is empty; this run measures nothing\n'
    return 1
}

# defect_ledger_intact <total-observations>
#
# The other half of the freeze. `lib/freeze.py` splits the baseline
# recording in two: the expectation, which is scored, and the defect ledger --
# the observations of the tree's two PEP 479 generators, lifted out so that a
# candidate who fixes them is neither rewarded nor punished. That split is only
# valid if it is a partition, so the scorer checks it on every run: the two
# halves must add up to the recording, and no observation may sit on both sides.
#
# Prints the fault and returns 1; the caller reports 0 and stops, because a
# ledger that has drifted means the scored set is no longer the set the freeze
# describes and every number downstream of it is about something else.
defect_ledger_intact() {
    local want="$1"
    python - "$GRADE_SUITE/expected.json" "$GRADE_SUITE/defects.json" "$want" <<'PY'
import json, sys
expected_path, defects_path, want = sys.argv[1], sys.argv[2], int(sys.argv[3])
with open(expected_path) as fh:
    expected = json.load(fh)
with open(defects_path) as fh:
    defects = json.load(fh)
total = len(expected) + len(defects)
if total != want:
    print("HARNESS FAULT: the freeze holds %d scored + %d ledgered = %d "
          "observations, expected %d" % (len(expected), len(defects), total, want))
    raise SystemExit(1)
both = sorted(set(expected) & set(defects))
if both:
    print("HARNESS FAULT: %d observation(s) are both scored and ledgered: %s"
          % (len(both), both[:4]))
    raise SystemExit(1)
print("freeze: %d scored + %d ledgered = %d observations, disjoint"
      % (len(expected), len(defects), total))
PY
}

# defect_marker_tally <actual-observations.json>
#
# Unscored, and printed rather than recorded. The ledger says which observations
# the pinned tree produced a PEP 479 crash for; if a candidate's tree no longer
# produces them -- because the generators were fixed, or because that code path
# stopped being reached at all -- that is worth seeing in the verifier log. It is
# not worth scoring in either direction, which is why the freeze lifted it out.
defect_marker_tally() {
    if [ ! -s "$1" ]; then
        printf 'defect markers: no observations to count\n'
        return 0
    fi
    python - "$GRADE_SUITE/defects.json" "$1" <<'PY'
import json, sys
defects_path, actual_path = sys.argv[1:3]
with open(defects_path) as fh:
    defects = json.load(fh)
with open(actual_path) as fh:
    actual = json.load(fh)
for marker in sorted(set(entry["marker"] for entry in defects.values())):
    keys = [key for key, entry in defects.items() if entry["marker"] == marker]
    still = sum(1 for key in keys if marker in (actual.get(key) or ""))
    print("defect marker (unscored) %-42r %d/%d still present"
          % (marker, still, len(keys)))
PY
}

# --------------------------------------------------------------------------
# candidate artifacts
# --------------------------------------------------------------------------

# build_dists <source-dir> <out-dir>
#
# The standard PEP 517 front end, run the way its own documentation says and
# with nothing added: no --no-isolation, no --skip-dependency-check. A tree that
# only builds with isolation switched off has not declared its build
# requirements, which is the property being measured.
#
# `--sdist --wheel` and not the default. The default is not "build both": it is
# "build the sdist, then build the wheel *from that sdist*", which is a useful
# consistency check for a release but is the wrong instrument here. Every
# `sdist.rebuilds` row in this suite unpacks the sdist somewhere else and
# compares the wheel built there against the wheel built from the tree -- and
# under the default both wheels come from the same sdist, so the row would
# compare an archive with itself and pass for a tree whose sdist is missing half
# its sources. Asking for the two artifacts explicitly builds each from the tree,
# which is what the row means to compare.
build_dists() {
    local src="$1" out="$2"
    mkdir -p "$out"
    give_app_to_unprivileged "$src"
    give_app_to_unprivileged "$out"
    run_unprivileged env SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1409491949}" \
        PIP_CACHE_DIR=/opt/cache/pip UV_CACHE_DIR=/opt/cache/uv \
        python -m build --sdist --wheel --outdir "$out" "$src" \
        > "$GRADE_LOG/build.$(basename "$out").log" 2>&1
    return $?
}

# newest_wheel <dir> [<name-prefix>]
newest_wheel() {
    local dir="$1" prefix="${2:-}"
    ls -1 "$dir"/${prefix}*.whl 2>/dev/null | sort | tail -n 1
}

# newest_sdist <dir> [<name-prefix>]
newest_sdist() {
    local dir="$1" prefix="${2:-}"
    ls -1 "$dir"/${prefix}*.tar.gz 2>/dev/null | sort | tail -n 1
}

# unpack_sdist <sdist> <dest>
#
# The rebuild-from-sdist rows unpack an archive the candidate's build backend
# wrote, so every member's owner and mode is the candidate's to choose. GNU tar
# run as root restores both: a member recorded as uid 0 with mode 4755 comes out
# a set-uid-root file that the nobody build after it could execute. So tar runs
# as nobody, into a directory root has just made fresh and handed over, and
# cannot create a device node or give a file to anyone.
# -p keeps the archive's permission bits, so the rebuild sees the modes a root
# unpack gave it; the set-id bits that leaves on nobody's own files are then
# cleared by root, once nothing of nobody's is running (run_unprivileged reaps
# before it returns). Returns tar's status; <dest> is left empty if a set-id
# file somehow survives, which the caller reads as an sdist that unpacked to no
# directory.
unpack_sdist() {
    local archive="$1" dest="$2" rc
    case "$dest" in "$GRADE"/?*) ;; *) echo "unpack_sdist: refusing $dest" >&2; return 2 ;; esac
    rm -rf "$dest"
    mkdir "$dest" || return 1
    chown "$SCORE_UID:$SCORE_GID" "$dest"
    chmod 0755 "$dest"
    run_unprivileged tar --no-same-owner --same-permissions \
        -C "$dest" -xzf "$archive"
    rc=$?
    find "$dest" -xdev -type f -perm /6000 -exec chmod ug-s {} + 2>/dev/null
    if [ -n "$(find "$dest" -xdev -type f -perm /6000 -print -quit 2>/dev/null)" ]; then
        echo "unpack_sdist: a set-id file survived in $dest; discarding the unpack" >&2
        rm -rf "$dest"
        mkdir "$dest"
        return 1
    fi
    return "$rc"
}

# make_venv <python> <dir>
make_venv() {
    local py="$1" dir="$2"
    # -I: this runs as root with the working directory /grade on sys.path[0]
    # for -m, and every venv below is made world-writable for the scorer --
    # without it a `venv/__init__.py` that candidate code wrote into the first
    # venv would be imported as root by the next make_venv.
    "$py" -I -m venv "$dir" > "$GRADE_LOG/venv.$(basename "$dir").log" 2>&1 || return 1
    # bento regenerates <package>/parsetab inside its own install directory on
    # first parse, exactly as it does from a checkout, so the unprivileged
    # scorer needs the venv writable. This is the installed tree, not the
    # candidate's source: nothing it writes is read back as an answer.
    chmod -R a+rwX "$dir"
    return 0
}

# venv_install <venv-dir> <artifact>...
#
# Retried once. The image warms `/opt/cache/pip` with the unpinned `ply six
# simplejson` of its build day, so a candidate who pins any other version --
# `ply==3.4`, which is what this tree actually shipped -- is a cache miss and a
# real fetch from the index. That fetch is on the scoring path of the recorded
# behaviour, the heaviest part in step 03, so a single index blip would cost a
# correct solution its score. One retry does not make the
# network reliable; it makes one blip cost time instead of score. A genuine
# failure -- an unsatisfiable pin, a broken wheel -- fails twice and is still a
# failure, which is why the count is one and not a loop.
venv_install() {
    local dir="$1" log attempt; shift
    log="$GRADE_LOG/venv.$(basename "$dir").log"
    for attempt in 1 2; do
        if run_unprivileged env PIP_CACHE_DIR=/opt/cache/pip \
               "$dir/bin/python" -m pip install --no-warn-script-location "$@" \
               >> "$log" 2>&1; then
            chmod -R a+rwX "$dir"
            return 0
        fi
        printf '\n--- pip install attempt %s failed ---\n' "$attempt" >> "$log"
        [ "$attempt" = 1 ] && sleep 3
    done
    return 1
}

# --------------------------------------------------------------------------
# the recorded behaviour suite
# --------------------------------------------------------------------------

# The environment both runners pin. Everything here is a thing the recording
# fixed; leaving any of it to the container would make an observation depend on
# the host rather than on the candidate's code.
#
# GRADE_PYTHONPATH is normally empty -- scoring imports the candidate's
# *installed* distribution from a venv, not a directory. It exists for the
# control run, where the instrument is rehearsed against the pinned tree on
# PYTHONPATH, which is how the freeze itself was recorded.
GRADE_PYTHONPATH=${GRADE_PYTHONPATH:-}

# Keep in step with SUBPROCESS_ENV in suite/replay.py. SETUPTOOLS_USE_DISTUTILS
# in particular is read by setuptools' .pth at interpreter start, so replay.py
# cannot set it for itself; it refuses to run without it rather than silently
# recording against whichever distutils the venv happened to shadow in.
_runner_env() {
    printf '%s\n' PYTHONHASHSEED=0 PYTHONWARNINGS=ignore \
        PYTHONDONTWRITEBYTECODE=1 LC_ALL=C.UTF-8 LANG=C.UTF-8 TZ=UTC \
        COLUMNS=80 TERM=dumb SETUPTOOLS_USE_DISTUTILS=stdlib
    if [ -n "$GRADE_PYTHONPATH" ]; then
        printf 'PYTHONPATH=%s\n' "$GRADE_PYTHONPATH"
    fi
}

# _read_back_unprivileged <nobody's file> <root's copy>
#
# The runners' scratch directory is nobody's, and candidate code ran in the
# same process as the runner -- an atexit hook, or a child it left behind, gets
# the last word on what sits at --out. Copied by root, a symlink planted there
# would be followed into the answers in $GRADE_SUITE, which only root can read,
# and they would come back as the observations. So the result is read by the
# identity that wrote it: a link to something nobody cannot read reads as no
# result at all.
_read_back_unprivileged() {
    local src="$1" dest="$2"
    if run_unprivileged test -s "$src"; then
        run_unprivileged cat "$src" > "$dest" 2>/dev/null || rm -f "$dest"
    fi
}

# run_corpus <python> <out.json>
#
# The runner is the same file that produced the freeze, so a canonicalisation
# bug cannot make a candidate look wrong without having made the baseline look
# wrong too. It runs unprivileged from a scratch directory it owns.
run_corpus() {
    local py="$1" out="$2" work tag
    tag=$(basename "$out" .json)
    work=$(mktemp -d "$GRADE/run-corpus-XXXX")
    chmod 777 "$work"
    # --out lands inside the work directory, not next to it: /grade is
    # root-owned 0755 and the runner is uid 65534, so writing the result
    # directly would fail with EACCES after the whole replay had already run.
    ( cd "$work" && run_unprivileged env $(_runner_env) \
        "$py" "$GRADE_SUITE/replay.py" observe \
            --corpus "$GRADE_SUITE/corpus.json" --out "$work/out.json" \
            --timeout 30 ) \
        > "$GRADE_LOG/corpus.$tag.log" 2>&1
    local rc=$?
    _read_back_unprivileged "$work/out.json" "$out"
    rm -rf "$work"
    return $rc
}

# run_globals <python> <out.json>
run_globals() {
    local py="$1" out="$2" work tag
    tag=$(basename "$out" .json)
    work=$(mktemp -d "$GRADE/run-globals-XXXX")
    chmod 777 "$work"
    ( cd "$work" && run_unprivileged env $(_runner_env) \
        "$py" "$GRADE_SUITE/replay.py" globals \
            --fixtures "$GRADE_SUITE/globals-fixtures.json" \
            --out "$work/out.json" --timeout 60 ) \
        > "$GRADE_LOG/globals.$tag.log" 2>&1
    local rc=$?
    _read_back_unprivileged "$work/out.json" "$out"
    rm -rf "$work"
    return $rc
}

# compare_observations <expected.json> <actual.json> <report.json>
# Prints the fraction that matched, in [0, 1]. A missing actual file is 0.
#
# A report is written on every path, and its total is always the size of the
# expectation. Callers sum `observations_tally` over the corpus and the globals,
# so a run that left no observations and wrote no report would drop out of the
# denominator: a replay that exited, was killed at the per-call cap, or wrote
# something that does not parse would leave the step scored on the other
# family alone. It counts as every expected observation missed instead.
compare_observations() {
    local expected="$1" actual="$2" report="$3"
    rm -f "$report"
    if [ -s "$actual" ]; then
        python "$GRADE_SUITE/replay.py" compare \
            --expected "$expected" --actual "$actual" --out "$report" \
            >> "$GRADE_LOG/compare.log" 2>&1 || true
    else
        printf 'compare: %s is missing or empty; every expected observation is a miss\n' \
            "$actual" >> "$GRADE_LOG/compare.log"
    fi
    python - "$expected" "$report" <<'PY'
import json, sys
expected_path, report_path = sys.argv[1:3]
with open(expected_path) as fh:
    total = len(json.load(fh))
try:
    with open(report_path) as fh:
        report = json.load(fh)
    if int(report["total"]) != total:
        raise ValueError("report total %r != %d expected" % (report["total"], total))
    print("%.6f" % float(report["score"]))
except Exception as exc:
    with open(report_path, "w") as fh:
        json.dump({"total": total, "passed": 0, "failed": total, "score": 0.0,
                   "results": [], "fault": "no comparison: %s" % exc}, fh)
        fh.write("\n")
    print("0")
PY
}

# --------------------------------------------------------------------------
# scoring arithmetic
# --------------------------------------------------------------------------

# A step's score is built from weighted, named parts (see product_score). Parts
# are recorded as `name weight value` lines so the breakdown lands in the verifier log and a
# reward can always be traced back to the checks that produced it.
GRADE_PARTS=$GRADE/parts.txt

part() {
    local name="$1" weight="$2" value="$3"
    printf '%s %s %s\n' "$name" "$weight" "$value" >> "$GRADE_PARTS"
    printf 'PART %-34s weight=%-5s value=%s\n' "$name" "$weight" "$value"
}

total_score() {
    awk '{ w += $2; s += $2 * $3 } END { if (w <= 0) print 0; else printf "%.6f", s / w }' \
        "$GRADE_PARTS"
}

# product_score <T1 part>... -- prints "<T1 x T2> <T1> <T2> <missing>".
#
# The step's reward before the suppression penalty. T1, migrated-ness, is the weighted mean of the parts
# named here: the families this step introduces. T2, preservation, is the
# weighted mean of every other part: the recorded behaviour and the families an
# earlier step introduced, which this step must not lose. The relative weights
# inside each factor are the ones `part` recorded. A factor with no parts is 0,
# and <missing> counts the named parts that were never recorded, which the
# caller treats as a harness fault rather than a score.
product_score() {
    awk -v own=" $* " '
        BEGIN { n = split(own, names, " ") }
        {
            if (index(own, " " $1 " ")) { w1 += $2; s1 += $2 * $3; seen[$1] = 1 }
            else { w2 += $2; s2 += $2 * $3 }
        }
        END {
            missing = 0
            for (i = 1; i <= n; i++) if (!(names[i] in seen)) missing++
            t1 = (w1 > 0) ? s1 / w1 : 0
            t2 = (w2 > 0) ? s2 / w2 : 0
            printf "%.6f %.6f %.6f %d", t1 * t2, t1, t2, missing
        }' "$GRADE_PARTS"
}

# fraction <passed> <total>
fraction() {
    awk -v p="$1" -v t="$2" 'BEGIN { if (t <= 0) print 0; else printf "%.6f", p / t }'
}

# --------------------------------------------------------------------------
# the per-step detail file
# --------------------------------------------------------------------------
#
# Every check is recorded against the step that FIRST required it, so step 4's
# verifier emits results01..results04 and a reader can tell "this step's own new
# requirement" from "a requirement it inherited". Two failure modes are only
# visible in that split:
#
#   * A step that ran an EARLIER step's test.sh. Its verifier directory then
#     holds only that step's stepNN-* files. Naming the artifacts after the step
#     number is what makes the mismatch detectable from the outside --
#     `ls jobs/<job>/*/*/steps/<NN>-*/verifier | grep -oE 'step[0-9]+' | sort -u`
#     must print exactly `step<NN>`.
#   * A step handed a stale /app holding the PREVIOUS step's solution. It then
#     returns a plausible partial score whose failures sit entirely in its own
#     new family while every inherited family passes -- which reads like a
#     genuine finding and is not one.
#
# STEP_NUM must be set by each test.sh before the first `record`.
STEP_NUM=${STEP_NUM:-00}
GRADE_RECORDS=$GRADE/records.jsonl

# record <family> <label> <ok:0|1> [detail]
#
# <family> is the two-digit step number that introduced the requirement, not the
# step doing the checking.
record() {
    local family="$1" label="$2" ok="$3" detail="${4:-}"
    mkdir -p "$GRADE"
    python - "$family" "$label" "$ok" "$detail" >> "$GRADE_RECORDS" <<'PY'
import json, sys
family, label, ok, detail = sys.argv[1:5]
print(json.dumps({"family": family, "label": label,
                  "ok": ok not in ("0", "", "false", "False"),
                  "detail": detail}))
PY
    printf '  [%s] %s.%s%s\n' \
        "$(test "$ok" = 1 && echo PASS || echo FAIL)" "$family" "$label" \
        "$(test -n "$detail" && echo "  -- $detail")"
}

# emit_detail -- write /logs/verifier/step<NN>-detail.json from the records.
emit_detail() {
    python - "$GRADE_RECORDS" "/logs/verifier/step$STEP_NUM-detail.json" "$STEP_NUM" <<'PY'
import collections, json, os, sys
src, dest, step = sys.argv[1:4]
groups = collections.defaultdict(list)
if os.path.exists(src):
    with open(src) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            groups["results" + row["family"]].append(
                {"label": row["label"], "ok": row["ok"], "detail": row["detail"]})
out = {"step": step}
for key in sorted(groups):
    out[key] = groups[key]
own = groups.get("results" + step, [])
out["summary"] = {
    "own_total": len(own),
    "own_failed": sum(1 for x in own if not x["ok"]),
    "inherited_total": sum(len(v) for k, v in groups.items()
                           if k != "results" + step),
    "inherited_failed": sum(1 for k, v in groups.items()
                            if k != "results" + step
                            for x in v if not x["ok"]),
}
with open(dest, "w") as fh:
    json.dump(out, fh, indent=1, sort_keys=True)
    fh.write("\n")
print("detail: %s" % json.dumps(out["summary"], sort_keys=True))
PY
}

# group_fraction <prefix>... -- pass rate over the records whose label starts
# with any of the given prefixes, printed as "<value> <passed>/<total>".
#
# `record`'s <family> is the step number that FIRST required a check, which is
# the axis emit_detail splits own from inherited by. The scoring groups a step
# weighs its checks into are a second, orthogonal axis -- a step 4 verifier
# weighs an inherited step 1 metadata check into its own `metadata` part -- and
# this is that axis. The count is printed alongside the value because a prefix
# that matches nothing scores 0 and looks exactly like a group that failed.
group_fraction() {
    python - "$GRADE_RECORDS" "$@" <<'PY'
import json, os, sys
src, prefixes = sys.argv[1], tuple(sys.argv[2:])
rows = []
if os.path.exists(src):
    with open(src) as fh:
        for line in fh:
            line = line.strip()
            if line:
                row = json.loads(line)
                if row["label"].startswith(prefixes):
                    rows.append(row)
passed = sum(1 for r in rows if r["ok"])
print("%.6f %d/%d" % ((passed / float(len(rows))) if rows else 0.0, passed, len(rows)))
PY
}

# rows_from_gates <tsv> -- turn a gate script's `family<TAB>label<TAB>ok<TAB>detail`
# lines into `record` calls. The gate scripts read the candidate tree and the
# artifacts; they never score, so a change to the weighting never means editing
# Python.
rows_from_gates() {
    local tsv="$1" n=0
    while IFS=$'\t' read -r family label ok detail; do
        [ -n "${label:-}" ] || continue
        record "$family" "$label" "$ok" "${detail:-}"
        n=$((n + 1))
    done < "$tsv"
    printf 'gate rows: %d from %s\n' "$n" "$(basename "$tsv")"
    [ "$n" -gt 0 ]
}

# observations_tally <report> -- "<passed> <total>" from a compare report, so a
# behaviour part can be a single fraction over the corpus and the globals
# together rather than an unweighted average of two families of very different
# sizes.
observations_tally() {
    python - "$1" <<'PY'
import json, sys
try:
    report = json.load(open(sys.argv[1]))
    print("%d %d" % (report["passed"], report["total"]))
except Exception:
    print("0 0")
PY
}
