#!/usr/bin/env bash
# PRODUCT SCORING (2026-10-13): the reward is now T1 x T2 x penalty -- see
# `Product scoring` at the end of main(). The part weights below still set
# the relative weight of each part inside its factor; any floor arithmetic
# below describes the superseded weighted mean.
# Step 03 scorer: is there one lock for one workspace, and did the four
# vendored trees become dependencies without moving a single observation?
#
# Twelve parts, seventy units. Eight of them are inherited -- five from step 01
# and three from step 02 -- re-run unchanged and recorded against the family
# that first required them, because a step that quietly loses what an earlier
# one established is the failure mode a multi-step task exists to catch. Four
# are this step's.
#
#   build      1  inherited (01): the declaration, the tools' verdicts, the artifacts
#   metadata   1  inherited (01): every field the pinned tree declares about itself
#   contents   1  inherited (01): what the wheel ships, the sdist, the rebuild
#   migrated   1  inherited (01): the properties that make this a migration
#   behaviour 22  inherited (01): 5715 recorded observations, from the installed wheel
#   layout     1  inherited (02): the src/ trees, and the root that imports nothing
#   version    1  inherited (02): one declared source, reported by the install
#   hello      4  inherited (02): the example extension, still a distribution
#   lock      14  own: the lockfile, hash-pinned, current, and complete enough
#                 to build an environment with the index unreachable
#   workspace  8  own: the members, `yaku` resolved from here and carrying the
#                 real build system, `hello` inside
#   groups     8  own: `dev` and `doc`, one including the other, both installing
#   unvendor   8  own: the four trees gone, Bento's own `private` code kept, and
#                 ply/six/simplejson arriving as installed distributions
#
# Two scored families, and the split between them is the point of the numbers.
# "Still works" is `behaviour` -- 22/70 = 31.4% on its own, 37.1% counting the
# `hello` example it also exercises -- and "migrated" is this step's own
# thirty-eight units, 54.3%. The behaviour weight is large because this step is
# the one that can break the library without touching a line of it: every
# dependency stops being a directory in the tree and starts being a resolved
# version, and a lock that picks the wrong `ply` takes the parser with it. A
# tree that de-vendors, workspaces, groups and locks, and breaks Bento doing it,
# has not done the step.
#
# The inherited thirty-two weights out of seventy are deliberately not
# free-standing: a tree that solved step 02 and stopped scores them in full plus
# exactly one of this step's sixteen own rows -- `unvendor.own-code-kept`, which
# is a regression guard and which by construction an untouched tree passes --
# for 34/70 = 0.4857, under the half mark. The guard is kept and paid for rather
# than deleted, because an `unvendor` family with no regression guard in it is a
# family that rewards deleting `bento/private/` wholesale.
#
# The whole thing is multiplied by step 01's build-escape suppression penalty,
# on the same terms as step 02: the switches are still forbidden, so the penalty
# is still applied.
#
# Two ordering notes that are load-bearing:
#
#   * Both static gates run FIRST, before anything is built. `python -m build`
#     leaves `build/` and `*.egg-info` behind, which are copies of the tree that
#     would be scanned twice and counted twice.
#   * The behaviour venv is given the root wheel AND a wheel built from every
#     workspace member the root wheel requires. `[tool.uv.sources]` is uv's own
#     metadata and does not travel into a wheel, so a bento wheel that declares
#     `yaku` asks pip for a distribution that exists on no index. Without the
#     member wheels this step would take every inherited install and every one
#     of the 5715 observations to zero for the reference solution too. Steps 01
#     and 02 declare no workspace, so the list is empty there.
set -u
. /tests/helpers.sh
. /tests/lib/harness.sh

STEP_NUM=03
harden_reward_dir

# An index that is not there. `unshare` needs CAP_SYS_ADMIN, which this
# container does not have, so the only honest way to take the network away from
# one command is to point it at a closed port: 9 is discard, nothing listens,
# and the connection is refused rather than hanging.
DEAD_PROXY="http://127.0.0.1:9"
offline_env() {
    printf '%s\n' "HTTP_PROXY=$DEAD_PROXY" "HTTPS_PROXY=$DEAD_PROXY" \
        "ALL_PROXY=$DEAD_PROXY" "http_proxy=$DEAD_PROXY" \
        "https_proxy=$DEAD_PROXY" "all_proxy=$DEAD_PROXY" \
        "NO_PROXY=" "no_proxy=" "UV_OFFLINE=1"
}

main() {
grade_prepare

# An empty /app is a broken artifact restore, not a candidate who did nothing,
# and the two produce the same 0. Say which.
if ! app_is_present; then
    emit_detail
    report 0
    return 0
fi

if ! defect_ledger_intact 5763; then
    emit_detail
    report 0
    return 0
fi

give_app_to_unprivileged "$GRADE_APP"

ROWS=$GRADE/gate-rows.tsv
PENALTY=1

# --- step 02's static reads, before anything is built -----------------------
python /tests/step02_gates.py static "$GRADE_APP" "$GRADE_PRISTINE" \
    > "$ROWS" 2> "$GRADE_LOG/gates02-static.err"
rows_from_gates "$ROWS"
HELLODIR=$(awk '$1 == "HELLODIR" { print $2 }' "$GRADE_LOG/gates02-static.err")
HELLO_NAME=$(awk '$1 == "HELLO" { print $2 }' "$GRADE_LOG/gates02-static.err")
HELLO_VERSION=$(awk '$1 == "HELLO" { print $3 }' "$GRADE_LOG/gates02-static.err")
# The strings the pinned tree prints, derived from the pinned tree by the gate
# rather than written down here. They contain spaces, so they are cut by prefix
# and not by field.
HELLO_C_OUT=$(sed -n 's/^HELLO_C_OUT //p' "$GRADE_LOG/gates02-static.err" | tail -n 1)
HELLO_PY_OUT=$(sed -n 's/^HELLO_PY_OUT //p' "$GRADE_LOG/gates02-static.err" | tail -n 1)
BANNERLINE=$(sed -n 's/^BANNERLINE //p' "$GRADE_LOG/gates02-static.err" | tail -n 1)
printf 'hello: name=%s version=%s dir=%s\n' \
    "${HELLO_NAME:-?}" "${HELLO_VERSION:-?}" "${HELLODIR:-<not found>}"
printf 'pinned expectations: C=%s python=%s banner=%s\n' \
    "${HELLO_C_OUT:-?}" "${HELLO_PY_OUT:-?}" "${BANNERLINE:-?}"

# The identity and the three expectations come from the pinned example read by
# the pinned parser and the pinned sources, so an empty one means the gate died
# before it got there and every `hello.` row below would be scoring the harness
# rather than the candidate.
if [ -z "${HELLO_NAME:-}" ] || [ -z "${HELLO_VERSION:-}" ] \
   || [ -z "${HELLO_C_OUT:-}" ] || [ -z "${HELLO_PY_OUT:-}" ] \
   || [ -z "${BANNERLINE:-}" ]; then
    printf 'HARNESS FAULT: the pinned example declared no name/version, or the pinned expectations could not be derived; see gates02-static.err\n'
    sed -n '1,20p' "$GRADE_LOG/gates02-static.err"
    emit_detail
    report 0
    return 0
fi

# --- step 03's static reads, also before anything is built ------------------
python /tests/step03_gates.py static "$GRADE_APP" "$GRADE_PRISTINE" \
    > "$ROWS" 2> "$GRADE_LOG/gates03-static.err"
rows_from_gates "$ROWS"
DECL_GROUPS=$(awk '$1 == "GROUP" { print $2 }' "$GRADE_LOG/gates03-static.err")
printf 'declared dependency groups: %s\n' "$(echo $DECL_GROUPS | tr '\n' ' ')"
sed -n 's/^MEMBER /workspace member: /p' "$GRADE_LOG/gates03-static.err"

# --- step 01's precondition -------------------------------------------------
python /tests/step01_gates.py precondition "$GRADE_APP" \
    > "$ROWS" 2> "$GRADE_LOG/gates-precondition.err"
precond=$?
rows_from_gates "$ROWS"

# --- the tools' own verdicts and the build ----------------------------------
built=0
if [ "$precond" -eq 0 ]; then
    run_unprivileged validate-pyproject "$GRADE_APP/pyproject.toml" \
        > "$GRADE_LOG/validate-pyproject.log" 2>&1
    record 01 tool.validate-pyproject "$(test $? -eq 0 && echo 1 || echo 0)" \
        "$(tail -c 200 "$GRADE_LOG/validate-pyproject.log" | tr '\n' ' ')"

    build_dists "$GRADE_APP" "$GRADE/dist"
    build_rc=$?
    record 01 tool.build-exit "$(test $build_rc -eq 0 && echo 1 || echo 0)" \
        "python -m build exit $build_rc; $(tail -c 200 "$GRADE_LOG/build.dist.log" | tr '\n' ' ')"

    wheel=$(newest_wheel "$GRADE/dist")
    sdist=$(newest_sdist "$GRADE/dist")
    if [ $build_rc -eq 0 ] && [ -n "$wheel" ] && [ -n "$sdist" ]; then built=1; fi

    if [ -n "$wheel" ] && [ -n "$sdist" ]; then
        run_unprivileged env PIP_CACHE_DIR=/opt/cache/pip \
            twine check --strict "$wheel" "$sdist" \
            > "$GRADE_LOG/twine-check.log" 2>&1
        record 01 tool.twine-check "$(test $? -eq 0 && echo 1 || echo 0)" \
            "$(tail -c 200 "$GRADE_LOG/twine-check.log" | tr '\n' ' ')"
    else
        record 01 tool.twine-check 0 "no wheel and sdist to check"
    fi

    # The rebuild from the sdist. A tree builds happily from files the sdist
    # forgot to carry; only unpacking the archive somewhere else and building
    # again finds out.
    mkdir -p "$GRADE/sdist-src"
    if [ -n "$sdist" ]; then
        unpack_sdist "$sdist" "$GRADE/sdist-src" 2> "$GRADE_LOG/sdist-unpack.log"
        unpacked=$(find "$GRADE/sdist-src" -mindepth 1 -maxdepth 1 -type d | head -n 1)
        if [ -n "$unpacked" ]; then
            build_dists "$unpacked" "$GRADE/sdist-dist"
            record 01 sdist.builds "$(test $? -eq 0 && echo 1 || echo 0)" \
                "$(tail -c 200 "$GRADE_LOG/build.sdist-dist.log" | tr '\n' ' ')"
        else
            record 01 sdist.builds 0 "sdist unpacked to no directory"
        fi
    else
        record 01 sdist.builds 0 "no sdist to unpack"
    fi
else
    record 01 tool.validate-pyproject 0 "precondition not met: see the pyproject./build-system./migrated. rows"
    record 01 tool.build-exit 0 "build not attempted: precondition not met"
    record 01 tool.twine-check 0 "precondition not met: see the pyproject./build-system./migrated. rows"
    record 01 sdist.builds 0 "precondition not met: see the pyproject./build-system./migrated. rows"
fi

# --- what came out ----------------------------------------------------------
mkdir -p "$GRADE/dist" "$GRADE/sdist-dist"
if [ "$precond" -eq 0 ]; then
    python /tests/step01_gates.py post "$GRADE_APP" "$GRADE/dist" \
        "$GRADE/sdist-dist" "$GRADE_PRISTINE" "$built" \
        > "$ROWS" 2> "$GRADE_LOG/gates-post.err"
else
    python /tests/step01_gates.py skipped "$GRADE_APP" \
        > "$ROWS" 2> "$GRADE_LOG/gates-post.err"
fi
rows_from_gates "$ROWS"
PENALTY=$(awk '/^PENALTY /{print $2}' "$GRADE_LOG/gates-post.err")
[ -n "$PENALTY" ] || PENALTY=0      # the gate did not finish; do not award its absence

wheel=$(newest_wheel "$GRADE/dist")
sdist=$(newest_sdist "$GRADE/dist")

# The move must be invisible in the wheel and visible in the sdist. Both are
# read off the archives step 01 just built, so this costs no second build.
python /tests/step02_gates.py artifacts "${wheel:-}" "${sdist:-}" \
    > "$ROWS" 2> "$GRADE_LOG/gates02-artifacts.err"
rows_from_gates "$ROWS"

# --- the workspace members the root wheel cannot install without ------------
#
# See the header. The list comes from the candidate's own wheel metadata
# intersected with the workspace they declared, so a tree with no workspace
# contributes nothing and a tree that declares one gets exactly the members it
# made itself depend on.
MEMBER_WHEELS=""
if [ -n "$wheel" ]; then
    python /tests/step03_gates.py localdeps "$wheel" "$GRADE_APP" \
        > "$GRADE/localdeps.txt" 2> "$GRADE_LOG/gates03-localdeps.err"
    member_n=0
    while read -r memberdir; do
        [ -n "$memberdir" ] || continue
        member_n=$((member_n + 1))
        build_dists "$memberdir" "$GRADE/member-$member_n"
        member_rc=$?
        member_wheel=$(newest_wheel "$GRADE/member-$member_n")
        printf 'workspace member %s: build exit %s, wheel %s\n' \
            "$memberdir" "$member_rc" "$(basename "${member_wheel:-<none>}")"
        if [ -n "$member_wheel" ]; then
            MEMBER_WHEELS="$MEMBER_WHEELS $member_wheel"
        fi
    done < "$GRADE/localdeps.txt"
    printf 'local dependency wheels: %s\n' "${MEMBER_WHEELS:-<none>}"
fi

# --- install it and replay the recorded behaviour ---------------------------
corpus_pass=0; corpus_total=0; globals_pass=0; globals_total=0
installed=0
if [ "$built" = 1 ] && [ -n "$wheel" ] && make_venv python "$GRADE/venv"; then
    if venv_install "$GRADE/venv" "$wheel" $MEMBER_WHEELS; then
        installed=1
        record 01 install.wheel 1 "$(basename "$wheel")$MEMBER_WHEELS"
        run_unprivileged "$GRADE/venv/bin/python" -c 'import bento, bentomakerlib' \
            > "$GRADE_LOG/import.log" 2>&1
        record 01 install.imports "$(test $? -eq 0 && echo 1 || echo 0)" \
            "$(tail -c 200 "$GRADE_LOG/import.log" | tr '\n' ' ')"
        run_unprivileged "$GRADE/venv/bin/bentomaker" --version \
            > "$GRADE_LOG/bentomaker-version.log" 2>&1
        record 01 install.console-script "$(test $? -eq 0 && echo 1 || echo 0)" \
            "$(tr '\n' ' ' < "$GRADE_LOG/bentomaker-version.log" | tail -c 200)"

        # No PYTHONPATH: the suite has to run against the installed
        # distribution, which is the whole point of installing it.
        export GRADE_PYTHONPATH=
        run_corpus "$GRADE/venv/bin/python" "$GRADE/actual-corpus.json"
        compare_observations "$GRADE_SUITE/expected.json" \
            "$GRADE/actual-corpus.json" "$GRADE/report-corpus.json" > /dev/null
        read -r corpus_pass corpus_total <<< "$(observations_tally "$GRADE/report-corpus.json")"
        defect_marker_tally "$GRADE/actual-corpus.json"

        run_globals "$GRADE/venv/bin/python" "$GRADE/actual-globals.json"
        compare_observations "$GRADE_SUITE/globals-expected.json" \
            "$GRADE/actual-globals.json" "$GRADE/report-globals.json" > /dev/null
        read -r globals_pass globals_total <<< "$(observations_tally "$GRADE/report-globals.json")"
    else
        record 01 install.wheel 0 "pip install failed; see venv.venv.log"
        record 01 install.imports 0 "not installed"
        record 01 install.console-script 0 "not installed"
    fi
else
    record 01 install.wheel 0 "no wheel to install"
    record 01 install.imports 0 "no wheel to install"
    record 01 install.console-script 0 "no wheel to install"
fi

# --- the version, as the installed distribution reports it ------------------
#
# Against the pinned recording's value, not just against the wheel's own
# METADATA: a candidate who declares `version = "nobuilt"` would otherwise make
# the three rows below agree with each other while changing nothing at all.
ver_expected=$(python -c "import json;print(json.load(open('/tests/data/meta-expected.json'))['version'])")
ver_wheel=
if [ -n "$wheel" ]; then
    ver_wheel=$(python - "$wheel" <<'PY'
import sys
sys.path.insert(0, "/tests/lib")
import gatelib
meta, _wheel, _names = gatelib.wheel_metadata(sys.argv[1])
print(((meta.get("Version") if meta else "") or "").strip())
PY
)
fi

# A scratch directory with no `bento.info` in it: `bentomaker -h` prints the
# banner and exits there, where in a directory holding a `bento.info` whose
# `DescriptionFromFile` is missing it raises before printing anything. The
# banner is a property of the installed distribution, so it is read somewhere
# that belongs to neither the candidate nor the pinned tree.
BANNER=$GRADE/banner
mkdir -p "$BANNER"
chmod 777 "$BANNER"

if [ "$installed" = 1 ]; then
    ver_runtime=$(run_unprivileged "$GRADE/venv/bin/python" -c \
        'import bento; print(bento.__version__)' \
        2> "$GRADE_LOG/version-runtime.err" | tr -d '\r')
    ok=0
    if [ -n "$ver_expected" ] && [ "$ver_runtime" = "$ver_expected" ] \
       && [ "$ver_wheel" = "$ver_expected" ]; then ok=1; fi
    record 02 version.runtime "$ok" \
        "bento.__version__=$(printf %s "${ver_runtime:-<none>}" | tail -c 60); METADATA Version=${ver_wheel:-<none>}; expected=$ver_expected"

    ( cd "$BANNER" && run_unprivileged "$GRADE/venv/bin/bentomaker" --version ) \
        > "$GRADE_LOG/version-console.log" 2>&1
    console=$(head -n 1 "$GRADE_LOG/version-console.log" | tr -d '\r')
    record 02 version.console-script \
        "$(test "$console" = "$ver_expected" && echo 1 || echo 0)" \
        "bentomaker --version printed $(printf %s "${console:-<nothing>}" | tail -c 80)"

    ( cd "$BANNER" && run_unprivileged "$GRADE/venv/bin/bentomaker" -h ) \
        > "$GRADE_LOG/version-banner.log" 2>&1
    banner=$(grep -m 1 '^bentomaker ' "$GRADE_LOG/version-banner.log" | tr -d '\r')
    record 02 version.banner \
        "$(test "$banner" = "$BANNERLINE" && echo 1 || echo 0)" \
        "first banner line: ${banner:-<none>}"

    revtype=$(run_unprivileged "$GRADE/venv/bin/python" -c \
        'import bento; print(type(bento.__git_revision__).__name__)' \
        2> "$GRADE_LOG/version-revision.err" | tr -d '\r')
    record 02 version.git-revision \
        "$(test "$revtype" = "str" && echo 1 || echo 0)" \
        "type(bento.__git_revision__) is ${revtype:-<missing>} $(tail -c 100 "$GRADE_LOG/version-revision.err" | tr '\n' ' ')"

    # Agreeing with the expected version is not the same as being read from the
    # metadata: a literal that happens to say 0.2.0 agrees with it too. So the
    # installed metadata is rewritten to a version nobody would ever declare,
    # the question is asked again, and the original bytes are put back. A
    # distribution that reports its own installed metadata follows the change;
    # one carrying a literal, or a copy made at build time, does not.
    meta_files=$(find "$GRADE/venv/lib" -maxdepth 4 \
        -path '*/site-packages/bento-*.dist-info/METADATA' 2>/dev/null | sort)
    meta_count=$(printf '%s' "$meta_files" | grep -c . )
    ok=0
    if [ "$meta_count" != 1 ]; then
        detail="expected exactly one installed bento-*.dist-info/METADATA, found $meta_count: $(printf '%s' "$meta_files" | tr '\n' ' ' | tail -c 140)"
    else
        # The save, the rewrite and the restore all run as nobody: the venv is
        # world-writable (bento writes its parse tables into it), so root
        # writing through a path in it would write wherever a candidate's
        # symlink pointed. The saved copy stays in the venv for the same reason.
        # (Migration rubric review round 2, 2026-10-05.)
        run_unprivileged cp -p "$meta_files" "$GRADE/venv/metadata.orig"
        run_unprivileged python - "$meta_files" <<'PY'
import sys
path = sys.argv[1]
with open(path, "rb") as fh:
    lines = fh.read().split(b"\n")
done = False
for i, line in enumerate(lines):
    if not done and line.startswith(b"Version:"):
        lines[i] = b"Version: 9.9.9"
        done = True
with open(path, "wb") as fh:
    fh.write(b"\n".join(lines))
sys.exit(0 if done else 3)
PY
        rewrote=$?
        forced=$(run_unprivileged "$GRADE/venv/bin/python" -c \
            'import bento; print(bento.__version__)' \
            2> "$GRADE_LOG/version-from-metadata.err" | tr -d '\r')
        run_unprivileged cp -p "$GRADE/venv/metadata.orig" "$meta_files"
        if [ "$rewrote" -eq 0 ] && [ "$forced" = "9.9.9" ]; then ok=1; fi
        detail="METADATA Version: 9.9.9 (rewrite exit $rewrote) -> bento.__version__=$(printf %s "${forced:-<none>}" | tail -c 60) $(tail -c 100 "$GRADE_LOG/version-from-metadata.err" | tr '\n' ' ')"
    fi
    record 02 version.from-metadata "$ok" "$detail"
else
    for label in version.runtime version.console-script version.banner \
                 version.git-revision version.from-metadata; do
        record 02 "$label" 0 "the root distribution was not installed"
    done
fi

# --- the repository root is not an import root any more ---------------------
#
# A bare interpreter, not the venv's: the question is whether the checkout
# shadows the installed distribution, and in the venv the answer is "there is
# an installed one" either way. The control run says the image itself does not
# provide `bento`, so a pass here means the root stopped providing it rather
# than that nothing ever did.
( cd /tmp && run_unprivileged env PYTHONPATH= python -c 'import bento' ) \
    > "$GRADE_LOG/root-import-control.log" 2>&1
control_rc=$?
( cd "$GRADE_APP" && run_unprivileged env PYTHONPATH= python -c 'import bento' ) \
    > "$GRADE_LOG/root-import-bento.log" 2>&1
root_bento_rc=$?
( cd "$GRADE_APP" && run_unprivileged env PYTHONPATH= python -c 'import bentomakerlib' ) \
    > "$GRADE_LOG/root-import-bentomakerlib.log" 2>&1
root_lib_rc=$?
if [ "$control_rc" -eq 0 ]; then
    printf 'HARNESS FAULT: the image itself imports bento, so the root-import row measures nothing\n'
    emit_detail
    report 0
    return 0
fi
record 02 layout.root-import-fails \
    "$(test "$root_bento_rc" -ne 0 -a "$root_lib_rc" -ne 0 && echo 1 || echo 0)" \
    "from the repository root with an empty PYTHONPATH: import bento exit $root_bento_rc, import bentomakerlib exit $root_lib_rc (control from /tmp: exit $control_rc)"

# --- the example extension, as a distribution -------------------------------
mkdir -p "$GRADE/hello-dist" "$GRADE/hello-sdist-dist"
if [ -n "${HELLODIR:-}" ]; then
    build_dists "$HELLODIR" "$GRADE/hello-dist"
    hello_rc=$?
    record 02 hello.builds "$(test $hello_rc -eq 0 && echo 1 || echo 0)" \
        "python -m build exit $hello_rc; $(tail -c 200 "$GRADE_LOG/build.hello-dist.log" | tr '\n' ' ')"

    hello_sdist=$(newest_sdist "$GRADE/hello-dist")
    mkdir -p "$GRADE/hello-sdist-src"
    if [ -n "$hello_sdist" ]; then
        unpack_sdist "$hello_sdist" "$GRADE/hello-sdist-src" \
            2> "$GRADE_LOG/hello-sdist-unpack.log"
        hello_unpacked=$(find "$GRADE/hello-sdist-src" -mindepth 1 -maxdepth 1 -type d | head -n 1)
        if [ -n "$hello_unpacked" ]; then
            build_dists "$hello_unpacked" "$GRADE/hello-sdist-dist"
        fi
    fi
else
    record 02 hello.builds 0 "no directory declares the name the example declares"
fi

python /tests/step02_gates.py hellodist "$GRADE/hello-dist" \
    "$GRADE/hello-sdist-dist" "$HELLO_NAME" "$HELLO_VERSION" \
    > "$ROWS" 2> "$GRADE_LOG/gates02-hellodist.err"
rows_from_gates "$ROWS"

hello_wheel=$(newest_wheel "$GRADE/hello-dist")
if [ -n "$hello_wheel" ] && make_venv python "$GRADE/hello-venv" \
        && venv_install "$GRADE/hello-venv" "$hello_wheel"; then
    record 02 hello.installs 1 "$(basename "$hello_wheel")"

    # Two processes, one call each. `hello.hello()` prints from C and
    # `hello.foo()` prints from Python, and the two stdio layers flush in an
    # order nothing in the instruction pins -- so each is read on its own,
    # where the order is not a question.
    ( cd "$BANNER" && run_unprivileged "$GRADE/hello-venv/bin/python" -P -c \
        'import sys, hello; r = hello.hello(); sys.stderr.write("RETURNED %r\n" % (r,))' ) \
        > "$GRADE_LOG/hello-c.out" 2> "$GRADE_LOG/hello-c.err"
    c_rc=$?
    ( cd "$BANNER" && run_unprivileged "$GRADE/hello-venv/bin/python" -P -c \
        'import sys, hello; r = hello.foo(); sys.stderr.write("RETURNED %r\n" % (r,))' ) \
        > "$GRADE_LOG/hello-py.out" 2> "$GRADE_LOG/hello-py.err"
    py_rc=$?
    c_out=$(cat "$GRADE_LOG/hello-c.out")
    py_out=$(cat "$GRADE_LOG/hello-py.out")
    # `$(cat ...)` strips every trailing newline, so a greeting printed with no
    # newline at all compares equal to one printed on its own line. The
    # instruction says each string is printed on its own line, so the
    # comparison is on bytes: the whole stream is the pinned string and its
    # terminator, and nothing else. The pinned strings still come from the
    # gate's stderr -- neither is written down here.
    printf '%s\n' "$HELLO_C_OUT" > "$GRADE_LOG/hello-c.want"
    printf '%s\n' "$HELLO_PY_OUT" > "$GRADE_LOG/hello-py.want"
    cmp -s "$GRADE_LOG/hello-c.out" "$GRADE_LOG/hello-c.want" \
        && c_bytes=1 || c_bytes=0
    cmp -s "$GRADE_LOG/hello-py.out" "$GRADE_LOG/hello-py.want" \
        && py_bytes=1 || py_bytes=0
    c_ret=$(grep -c '^RETURNED None$' "$GRADE_LOG/hello-c.err")
    py_ret=$(grep -c '^RETURNED None$' "$GRADE_LOG/hello-py.err")
    ok=0
    if [ "$c_rc" -eq 0 ] && [ "$py_rc" -eq 0 ] \
       && [ "$c_bytes" = 1 ] && [ "$py_bytes" = 1 ] \
       && [ "$c_ret" = 1 ] && [ "$py_ret" = 1 ]; then ok=1; fi
    record 02 hello.behaviour "$ok" \
        "hello.hello() -> $(printf %s "${c_out:-<nothing>}" | tr '\n' '|' | tail -c 60) bytes=$c_bytes returns-None=$c_ret exit $c_rc; hello.foo() -> $(printf %s "${py_out:-<nothing>}" | tr '\n' '|' | tail -c 60) bytes=$py_bytes returns-None=$py_ret exit $py_rc"

    ( cd "$BANNER" && run_unprivileged "$GRADE/hello-venv/bin/python" -P -c \
        'import hello; print(hello.bar.foo is hello.foo)' ) \
        > "$GRADE_LOG/hello-alias.log" 2>&1
    alias_out=$(head -n 1 "$GRADE_LOG/hello-alias.log" | tr -d '\r')
    record 02 hello.bar-alias \
        "$(test "$alias_out" = "True" && echo 1 || echo 0)" \
        "hello.bar.foo is hello.foo -> $(tail -c 120 "$GRADE_LOG/hello-alias.log" | tr '\n' ' ')"

    # Where `hello.hello` comes from. Printing the right line is not enough on
    # its own -- a Python function printing the same string passes that -- so
    # this asks whether the name the package exports is the one the extension
    # provides, and whether that extension is a module this interpreter loaded
    # from compiled code. The suffixes come from the running interpreter, not
    # from a filename written down here.
    ( cd "$BANNER" && run_unprivileged "$GRADE/hello-venv/bin/python" -P -c \
        'import hello, hello._bar, importlib.machinery as m; print(hello.hello is hello._bar.hello, hello._bar.__file__.endswith(tuple(m.EXTENSION_SUFFIXES)))' ) \
        > "$GRADE_LOG/hello-extension.log" 2>&1
    ext_out=$(head -n 1 "$GRADE_LOG/hello-extension.log" | tr -d '\r')
    record 02 hello.extension-backed \
        "$(test "$ext_out" = "True True" && echo 1 || echo 0)" \
        "hello.hello is hello._bar.hello, _bar loaded from an extension -> $(tail -c 140 "$GRADE_LOG/hello-extension.log" | tr '\n' ' ')"

    # The pinned C registers the method METH_VARARGS and never looks at `args`,
    # so extra positional arguments are accepted and ignored. A port that
    # tightened this to METH_NOARGS changes what the example does.
    ( cd "$BANNER" && run_unprivileged "$GRADE/hello-venv/bin/python" -P -c \
        'import sys, hello; r = hello.hello(1, 2, 3); sys.stderr.write("RETURNED %r\n" % (r,))' ) \
        > "$GRADE_LOG/hello-varargs.out" 2> "$GRADE_LOG/hello-varargs.err"
    var_rc=$?
    var_out=$(cat "$GRADE_LOG/hello-varargs.out")
    printf '%s\n' "$HELLO_C_OUT" > "$GRADE_LOG/hello-varargs.want"
    cmp -s "$GRADE_LOG/hello-varargs.out" "$GRADE_LOG/hello-varargs.want" \
        && var_bytes=1 || var_bytes=0
    var_ret=$(grep -c '^RETURNED None$' "$GRADE_LOG/hello-varargs.err")
    ok=0
    if [ "$var_rc" -eq 0 ] && [ "$var_bytes" = 1 ] \
       && [ "$var_ret" = 1 ]; then ok=1; fi
    record 02 hello.varargs "$ok" \
        "hello.hello(1, 2, 3) -> $(printf %s "${var_out:-<nothing>}" | tr '\n' '|' | tail -c 60) bytes=$var_bytes returns-None=$var_ret exit $var_rc; $(tail -c 100 "$GRADE_LOG/hello-varargs.err" | tr '\n' ' ')"
else
    record 02 hello.installs 0 "no wheel to install; see build.hello-dist.log"
    record 02 hello.behaviour 0 "not installed"
    record 02 hello.bar-alias 0 "not installed"
    record 02 hello.extension-backed 0 "not installed"
    record 02 hello.varargs 0 "not installed"
fi

# --- a scratch tree, a scratch cache and a scratch environment for uv -------
#
# A copy of the tree, because `uv sync` writes a `.venv` into the project and
# the scored snapshot is not the place for it. A cache of the candidate's own,
# because the image's warm cache holds whatever `ply` was newest when it was
# built: syncing against it would answer questions the candidate's lock never
# asked. It also has to be a cache the unprivileged runner can write to --
# `/opt/cache/uv` is root's, and `uv` refuses to start when it cannot
# initialise its cache directory, which is an error about permissions and not
# about the lock.
#
# And an environment of its own, outside the copy, with every environment the
# candidate left behind deleted out of the copy. `uv sync` is a sync: against a
# `.venv` that already holds the locked set it installs nothing, and therefore
# downloads nothing. A candidate who ran `uv sync` or `uv run` while working --
# which this step asks them to do -- leaves that `.venv` in `/app`, and the
# warm-up below would then warm nothing and the offline sync after it would
# fail for want of a cache the warm-up was supposed to fill. That is a question
# about what is lying around in `/app`, not a question about the lock. Measured
# on the `ply-unpinned` fixture, which is the one that ends in `uv run`: warm
# sync exit 0 installing nothing, then `--offline` exit 1 wanting a `simplejson`
# wheel nothing had ever fetched.
SYNC=$GRADE/sync
UVCACHE=$GRADE/uvcache
SYNCENV=$GRADE/syncenv
SYNCVENV=$SYNCENV/venv
rm -rf "$SYNC" "$SYNCENV"
cp -a "$GRADE_APP" "$SYNC"
find "$SYNC" -name pyvenv.cfg -printf '%h\n' 2>/dev/null | sort -u \
    | while read -r leftover; do
          printf 'scratch tree: dropped the environment at %s\n' \
              "${leftover#"$SYNC/"}"
          rm -rf "$leftover"
      done
mkdir -p "$UVCACHE" "$SYNCENV"
give_app_to_unprivileged "$SYNC"
give_app_to_unprivileged "$UVCACHE"
give_app_to_unprivileged "$SYNCENV"

# Every `uv` call below gets the same four, written once so that no row can
# quietly end up using a different cache, a different environment or a
# different interpreter.
#
# The interpreter is pinned because this image has four: `python3.11` from the
# base, and the 3.12/3.13/3.13t that `uv python install` put in
# `/opt/pythons` for the build-backend matrix. Which one `uv` picks for a sync
# depends on what the candidate declared and on the order `uv` happens to
# discover them in, and a row that can change interpreter between two runs of
# the same tree is not measuring the tree. 3.11 is the reference interpreter --
# the one the corpus was recorded on, the one the behaviour venv uses, and the
# one `meta.requires-python` already requires every passing tree to admit -- so
# pinning it takes nothing away. `UV_PYTHON_DOWNLOADS=never` is the other half:
# without it `uv` answers an interpreter it cannot find by fetching one, which
# is a network dependency hiding inside an interpreter choice.
uv_env() {
    printf '%s\n' "UV_CACHE_DIR=$UVCACHE" "UV_PROJECT_ENVIRONMENT=$SYNCVENV" \
        "UV_PYTHON=3.11" "UV_PYTHON_DOWNLOADS=never"
}

# --- fill the cache once, scored by nothing ---------------------------------
#
# Every row below that needs a distribution needs it fetched, and the index is
# the one thing in this scorer that is neither pinned nor local. Left alone,
# each of `lock.offline-sync`'s warm half and `groups.install`'s per-group syncs
# is its own set of index round-trips, so a run of this step asks the network
# the same question four or five times and fails the tree if any one of them is
# refused. This pass asks once, for everything the lock names including every
# group, into an environment nothing scores. It is allowed to fail: if the lock
# is broken or absent that IS the finding, and the rows below are the ones that
# say so.
WARMVENV=$SYNCENV/warm
( cd "$SYNC" && run_unprivileged env $(uv_env) \
    UV_PROJECT_ENVIRONMENT="$WARMVENV" \
    uv sync --frozen --all-groups ) > "$GRADE_LOG/uv-cache-warm.log" 2>&1
printf 'cache warm-up (unscored): exit %s\n' "$?"
rm -rf "$WARMVENV"

# --- is the lock current? ---------------------------------------------------
#
# With the network UP, deliberately. Under the dead proxy a drifted
# `pyproject.toml` and an unreachable index produce the same exit code, so the
# proxy would turn a staleness gate into a network gate that happens to give
# the right answer for the wrong reason. `uv lock --check` against a lock that
# already answers the question never reaches the index -- which is what the
# next row proves -- so the network being up costs this row nothing.
( cd "$GRADE_APP" && run_unprivileged env $(uv_env) \
    uv lock --check ) > "$GRADE_LOG/uv-lock-check.log" 2>&1
lock_check_rc=$?
record 03 lock.up-to-date "$(test $lock_check_rc -eq 0 && echo 1 || echo 0)" \
    "uv lock --check exit $lock_check_rc; $(tail -c 220 "$GRADE_LOG/uv-lock-check.log" | tr '\n' ' ')"

# --- can an environment be built from it with no index? ---------------------
#
# The first sync is allowed the network and only has to work; the second is the
# assertion -- a lock that is complete and hash-pinned needs no resolution and
# no index, and a lock missing a platform or one `uv` would want to re-resolve
# does not survive it.
( cd "$SYNC" && run_unprivileged env $(uv_env) \
    uv sync --frozen ) > "$GRADE_LOG/uv-sync-warm.log" 2>&1
sync_warm_rc=$?
if [ "$sync_warm_rc" -ne 0 ]; then
    # One retry, because the warm half is the only part of this row that can
    # touch the index and a single refused connection is not a fact about the
    # candidate's lock. The offline half is never retried: it is the assertion.
    printf 'warm sync exit %s, retrying once\n' "$sync_warm_rc"
    rm -rf "$SYNCVENV"
    ( cd "$SYNC" && run_unprivileged env $(uv_env) \
        uv sync --frozen ) > "$GRADE_LOG/uv-sync-warm.log" 2>&1
    sync_warm_rc=$?
fi
rm -rf "$SYNCVENV"
( cd "$SYNC" && run_unprivileged env $(uv_env) $(offline_env) \
    uv sync --frozen --offline ) > "$GRADE_LOG/uv-sync-offline.log" 2>&1
sync_offline_rc=$?
ok=0
if [ "$sync_warm_rc" -eq 0 ] && [ "$sync_offline_rc" -eq 0 ]; then ok=1; fi
record 03 lock.offline-sync "$ok" \
    "uv sync --frozen exit $sync_warm_rc, then --offline with the index unreachable exit $sync_offline_rc; $(tail -c 180 "$GRADE_LOG/uv-sync-offline.log" | tr '\n' ' ')"

# --- does every declared group install? -------------------------------------
#
# Each group on its own, so a group that names something unresolvable is found
# rather than masked by another that resolves. Which interpreter `uv` selected
# is not asked: the project chooses it, and §10.3 of the design notes measured
# that the answer depends on whether the candidate pinned one. What is asked is
# that the group installed, and -- for `doc`, which the instruction says
# provides Sphinx -- that the headline module is importable afterwards.
if [ -z "$DECL_GROUPS" ]; then
    record 03 groups.install 0 "no dependency group is declared"
else
    group_faults=""
    for group in $DECL_GROUPS; do
        # Retried once, for the same reason the warm sync above is: a group
        # that names a distribution nothing has fetched yet is an index
        # round-trip, and a refused connection is not a fact about the
        # candidate's groups. A group that names something unresolvable fails
        # both times.
        ( cd "$SYNC" && run_unprivileged env $(uv_env) \
            uv sync --frozen --only-group "$group" ) \
            > "$GRADE_LOG/uv-sync-group-$group.log" 2>&1
        group_rc=$?
        if [ "$group_rc" -ne 0 ]; then
            printf 'group %s exit %s, retrying once\n' "$group" "$group_rc"
            ( cd "$SYNC" && run_unprivileged env $(uv_env) \
                uv sync --frozen --only-group "$group" ) \
                >> "$GRADE_LOG/uv-sync-group-$group.log" 2>&1
            group_rc=$?
        fi
        if [ "$group_rc" -ne 0 ]; then
            group_faults="$group_faults $group(sync exit $group_rc)"
            continue
        fi
        if [ "$group" = "doc" ]; then
            run_unprivileged "$SYNCVENV/bin/python" -c 'import sphinx' \
                > "$GRADE_LOG/uv-sync-group-doc-import.log" 2>&1
            if [ $? -ne 0 ]; then
                group_faults="$group_faults doc(import sphinx failed)"
            fi
        fi
    done
    record 03 groups.install \
        "$(test -z "$group_faults" && echo 1 || echo 0)" \
        "$(test -n "$group_faults" && echo "did not install:$group_faults" || echo "every declared group synced from the lock: $(echo $DECL_GROUPS | tr '\n' ' ')")"
fi

# --- Bento's own code under bento/private/ ----------------------------------
#
# The regression guard of the `unvendor` family. `bytecode.py`, `_bytecode_2.py`,
# `_bytecode_3.py` and `version.py` are this project's own, they live in the
# directory whose other four entries this step deletes, and `bento/core/meta.py`
# and three command modules import them.
#
# The expectation is not written down: the same probe is run against the pinned
# tree on PYTHONPATH and against the candidate's installed distribution, and the
# two outputs are compared as bytes. Whatever the pinned tree does on this
# interpreter -- including the places where it raises -- is the answer.
cat > "$GRADE/private-probe.py" <<'PY'
"""What bento's own `private` modules do, asked identically of two trees."""
import os
import sys
import tempfile

import bento.private.bytecode as bytecode
import bento.private.version as version

print("bytecode", bytecode.bcompile.__name__, bytecode.PyCompileError.__name__)
print("version", version.NormalizedVersion("1.2.3a1"),
      version.is_valid_version("1.2.3"), version.is_valid_version("nope"))
print("suggest", version.suggest_normalized_version("1.0-alpha1"),
      version.suggest_normalized_version("2.0b1"))
try:
    version.NormalizedVersion("not a version")
except Exception as exc:
    print("raises", type(exc).__name__)
else:
    print("raises", None)
handle, path = tempfile.mkstemp(suffix=".py")
os.write(handle, b"x = 1\n")
os.close(handle)
try:
    blob = bytecode.bcompile(path)
    print("compiled", type(blob).__name__, bool(blob))
except Exception as exc:
    print("compiled", type(exc).__name__)
finally:
    os.unlink(path)
sys.stdout.flush()
PY
chmod 644 "$GRADE/private-probe.py"

( cd "$BANNER" && run_unprivileged env PYTHONPATH="$GRADE_PRISTINE" \
    PYTHONDONTWRITEBYTECODE=1 python "$GRADE/private-probe.py" ) \
    > "$GRADE_LOG/private-pinned.out" 2> "$GRADE_LOG/private-pinned.err"
private_control_rc=$?
if [ "$private_control_rc" -ne 0 ] || [ ! -s "$GRADE_LOG/private-pinned.out" ]; then
    printf 'HARNESS FAULT: the private-module probe does not run against the pinned tree (exit %s); it would score every candidate 0 for the same reason\n' \
        "$private_control_rc"
    tail -c 400 "$GRADE_LOG/private-pinned.err"
    emit_detail
    report 0
    return 0
fi
printf 'pinned private-module behaviour:\n'
sed 's/^/    /' "$GRADE_LOG/private-pinned.out"

if [ "$installed" = 1 ]; then
    ( cd "$BANNER" && run_unprivileged env PYTHONDONTWRITEBYTECODE=1 \
        "$GRADE/venv/bin/python" "$GRADE/private-probe.py" ) \
        > "$GRADE_LOG/private-candidate.out" 2> "$GRADE_LOG/private-candidate.err"
    private_rc=$?
    if cmp -s "$GRADE_LOG/private-pinned.out" "$GRADE_LOG/private-candidate.out"; then
        private_same=1
    else
        private_same=0
    fi
    ok=0
    if [ "$private_rc" -eq 0 ] && [ "$private_same" = 1 ]; then ok=1; fi
    record 03 unvendor.own-code-kept "$ok" \
        "probe exit $private_rc, output identical to the pinned tree=$private_same; $(tail -c 160 "$GRADE_LOG/private-candidate.err" | tr '\n' ' ')$(head -c 100 "$GRADE_LOG/private-candidate.out" | tr '\n' '|')"
else
    record 03 unvendor.own-code-kept 0 "the root distribution was not installed"
fi

# --- do the dependencies arrive as distributions? ---------------------------
#
# The other half of the unvendoring, asked of the installed environment rather
# than of the tree: a copy moved to another directory and put on `sys.path` by
# some other means still answers `import ply`. So the question is where the
# module came from -- outside the installed `bento` package -- and whether the
# environment has a distribution of that name at all.
cat > "$GRADE/dists-probe.py" <<'PY'
"""Are ply, six and simplejson installed distributions, not carried copies?"""
import importlib
import importlib.metadata as metadata
import os

import bento

root = os.path.dirname(os.path.abspath(bento.__file__))
faults = []
for name in ("ply", "six", "simplejson"):
    try:
        module = importlib.import_module(name)
    except Exception as exc:
        faults.append("%s does not import (%s)" % (name, type(exc).__name__))
        continue
    where = os.path.abspath(getattr(module, "__file__", "") or "")
    if where == root or where.startswith(root + os.sep):
        faults.append("%s is inside the installed bento package: %s"
                      % (name, where[-60:]))
    try:
        metadata.version(name)
    except Exception as exc:
        faults.append("%s has no installed distribution (%s)"
                      % (name, type(exc).__name__))
print("; ".join(faults) if faults else
      "ply, six and simplejson import from outside the bento package and each "
      "has installed metadata")
raise SystemExit(1 if faults else 0)
PY
chmod 644 "$GRADE/dists-probe.py"

if [ "$installed" = 1 ]; then
    ( cd "$BANNER" && run_unprivileged env PYTHONDONTWRITEBYTECODE=1 \
        "$GRADE/venv/bin/python" "$GRADE/dists-probe.py" ) \
        > "$GRADE_LOG/dists-probe.log" 2>&1
    dists_rc=$?
    record 03 unvendor.real-dists \
        "$(test $dists_rc -eq 0 && echo 1 || echo 0)" \
        "$(tail -c 250 "$GRADE_LOG/dists-probe.log" | tr '\n' ' ')"
else
    record 03 unvendor.real-dists 0 "the root distribution was not installed"
fi

# --- weigh it ---------------------------------------------------------------
read -r v_build n_build <<< "$(group_fraction pyproject. build-system. tool. dist.)"
read -r v_meta  n_meta  <<< "$(group_fraction meta.)"
read -r v_cont  n_cont  <<< "$(group_fraction wheel. sdist. install.)"
read -r v_migr  n_migr  <<< "$(group_fraction migrated.)"
read -r v_lay   n_lay   <<< "$(group_fraction layout.)"
read -r v_ver   n_ver   <<< "$(group_fraction version.)"
read -r v_hel   n_hel   <<< "$(group_fraction hello.)"
read -r v_lock  n_lock  <<< "$(group_fraction lock.)"
read -r v_ws    n_ws    <<< "$(group_fraction workspace.)"
read -r v_grp   n_grp   <<< "$(group_fraction groups.)"
read -r v_unv   n_unv   <<< "$(group_fraction unvendor.)"
behaviour=$(fraction "$((corpus_pass + globals_pass))" "$((corpus_total + globals_total))")

part build      1 "$v_build"
part metadata   1 "$v_meta"
part contents   1 "$v_cont"
part migrated   1 "$v_migr"
part behaviour 22 "$behaviour"
part layout     1 "$v_lay"
part version    1 "$v_ver"
part hello      4 "$v_hel"
part lock      14 "$v_lock"
part workspace  8 "$v_ws"
part groups     8 "$v_grp"
part unvendor   8 "$v_unv"
printf 'group sizes: build=%s metadata=%s contents=%s migrated=%s layout=%s version=%s hello=%s lock=%s workspace=%s groups=%s unvendor=%s\n' \
    "$n_build" "$n_meta" "$n_cont" "$n_migr" "$n_lay" "$n_ver" "$n_hel" \
    "$n_lock" "$n_ws" "$n_grp" "$n_unv"
printf 'observations: corpus %s/%s  globals %s/%s\n' \
    "$corpus_pass" "$corpus_total" "$globals_pass" "$globals_total"
printf 'suppression penalty: %s\n' "$PENALTY"

# A prefix that matches nothing scores 0 and is indistinguishable from a group
# that failed, so the sizes are asserted rather than printed and hoped for.
# The seven inherited sizes are steps 01 and 02's own, unchanged; a drift there
# means this step is re-running a different earlier step than the one it
# inherited.
#
# lock = 5: exists, hashed, no-requirements-txt, up-to-date, offline-sync.
# workspace = 4: members, yaku-path-source, yaku-real, hello-member.
# groups = 3: declared, include-group, install.
# unvendor = 4: dirs-gone, syspath-hack-gone, own-code-kept, real-dists.
for pair in "build $n_build 12" "metadata $n_meta 14" "contents $n_cont 14" \
            "migrated $n_migr 4" "layout $n_lay 5" "version $n_ver 6" \
            "hello $n_hel 12" "lock $n_lock 5" "workspace $n_ws 4" \
            "groups $n_grp 3" "unvendor $n_unv 4"; do
    set -- $pair
    have=${2#*/}
    if [ "$have" != "$3" ]; then
        printf 'HARNESS FAULT: group %s has %s checks, expected %s\n' "$1" "$have" "$3"
        emit_detail
        report 0
        return 0
    fi
done

# Product scoring (user decision 2026-10-13): reward = T1 x T2 x penalty.
# T1, migrated-ness, is this step's own families: lock workspace groups unvendor.
# T2, preservation, is every other part above -- the recorded behaviour and the
# families an earlier step introduced -- at the weights `part` gave them. The
# weighted mean of all parts is printed for comparison and scores nothing.
read -r prod t1 t2 t1_missing <<< "$(product_score lock workspace groups unvendor)"
if [ "${t1_missing:-1}" != 0 ]; then
    printf 'HARNESS FAULT: %s of the T1 parts were never recorded\n' "${t1_missing:-?}"
    emit_detail
    report 0
    return 0
fi
printf 'T1 (this step) %s x T2 (behaviour + inherited) %s = %s; weighted mean of all parts %s\n' \
    "$t1" "$t2" "$prod" "$(total_score)"
score=$(awk -v s="$prod" -v p="$PENALTY" 'BEGIN { printf "%.6f", s * p }')
printf 'total %s x penalty %s = %s\n' "$prod" "$PENALTY" "$score"
emit_detail
report "$score"
}

main 2>&1 | tee "/logs/verifier/step$STEP_NUM-stdout.log"
ensure_reward
