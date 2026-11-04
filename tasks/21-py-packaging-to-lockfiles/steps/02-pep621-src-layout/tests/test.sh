#!/usr/bin/env bash
# PRODUCT SCORING (2026-10-13): the reward is now T1 x T2 x penalty -- see
# `Product scoring` at the end of main(). The part weights below still set
# the relative weight of each part inside its factor; any floor arithmetic
# below describes the superseded weighted mean.
# Step 02 scorer: did the two importable trees move without the distribution
# changing, does the installed package know its own version, and is the example
# extension a distribution that builds and runs on this interpreter?
#
# Eight parts. Five of them are step 01's, re-run unchanged and recorded against
# family 01, because a step that quietly loses what the previous one established
# is the failure mode a multi-step task exists to catch. Three are this step's.
#
#   build     1   inherited: the declaration, the tools' verdicts, the artifacts
#   metadata  1   inherited: every field the pinned tree declares about itself
#   contents  1   inherited: what the wheel ships, the sdist, the rebuild
#   migrated  1   inherited: the properties that make this a migration
#   behaviour 6   inherited: 5715 recorded observations, from the installed wheel
#   layout    3   own: where the trees live, and what did not change because
#   version   4   own: one declared source, reported by the installed package
#   hello     5   own: examples/simples/single_extension as a real distribution
#
# The inherited ten weights out of twenty-two are deliberately not
# free-standing: a tree that solved step 01 and stopped scores them in full and
# nothing else, which is 10.667/22 = 0.4848 -- under the half mark, because five
# of this step's twenty-three own rows are the ones a step-01 tree could pass for
# nothing and four of those five were merged into rows that also state the new
# requirement. The one row it does keep is `version.git-revision`, which is there
# to say that the attribute survives the rewrite rather than to be hard.
#
# The whole thing is multiplied by step 01's build-escape suppression penalty,
# on the same terms: the switches are still forbidden, so the penalty is still
# applied, and a tree that grows one after step 01 loses the step it grew it in.
#
# Ordering note that is load-bearing: step 02's `static` gate runs FIRST, before
# any build. It reads the tree for the version literal, for `__package_info`
# imports and for the one `pyproject.toml` that declares `hello` -- and
# `python -m build` leaves `build/` and `*.egg-info` behind, which are copies of
# the tree that would be scanned twice and counted twice.
set -u
. /tests/helpers.sh
. /tests/lib/harness.sh

STEP_NUM=02
harden_reward_dir

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

# --- install it and replay the recorded behaviour ---------------------------
corpus_pass=0; corpus_total=0; globals_pass=0; globals_total=0
installed=0
if [ "$built" = 1 ] && [ -n "$wheel" ] && make_venv python "$GRADE/venv"; then
    if venv_install "$GRADE/venv" "$wheel"; then
        installed=1
        record 01 install.wheel 1 "$(basename "$wheel")"
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

# --- weigh it ---------------------------------------------------------------
read -r v_build n_build <<< "$(group_fraction pyproject. build-system. tool. dist.)"
read -r v_meta  n_meta  <<< "$(group_fraction meta.)"
read -r v_cont  n_cont  <<< "$(group_fraction wheel. sdist. install.)"
read -r v_migr  n_migr  <<< "$(group_fraction migrated.)"
read -r v_lay   n_lay   <<< "$(group_fraction layout.)"
read -r v_ver   n_ver   <<< "$(group_fraction version.)"
read -r v_hel   n_hel   <<< "$(group_fraction hello.)"
behaviour=$(fraction "$((corpus_pass + globals_pass))" "$((corpus_total + globals_total))")

part build     1 "$v_build"
part metadata  1 "$v_meta"
part contents  1 "$v_cont"
part migrated  1 "$v_migr"
part behaviour 6 "$behaviour"
part layout    3 "$v_lay"
part version   4 "$v_ver"
part hello     5 "$v_hel"
printf 'group sizes: build=%s metadata=%s contents=%s migrated=%s layout=%s version=%s hello=%s\n' \
    "$n_build" "$n_meta" "$n_cont" "$n_migr" "$n_lay" "$n_ver" "$n_hel"
printf 'observations: corpus %s/%s  globals %s/%s\n' \
    "$corpus_pass" "$corpus_total" "$globals_pass" "$globals_total"
printf 'suppression penalty: %s\n' "$PENALTY"

# A prefix that matches nothing scores 0 and is indistinguishable from a group
# that failed, so the sizes are asserted rather than printed and hoped for.
# The four inherited sizes are step 01's own, unchanged; a drift there means
# this step is re-running a different step 01 than the one it inherited.
#
# layout = 5: src-tree, root-clean, core-packages-moved, artifacts-unmoved,
# root-import-fails.
# version = 6: single-source, runtime, console-script, banner, git-revision,
# from-metadata.
# hello = 12: declares, c-ported, builds, wheel-name, wheel-tagged,
# extension-member, sdist-rebuilds, installs, behaviour, bar-alias,
# extension-backed, varargs.
for pair in "build $n_build 12" "metadata $n_meta 14" "contents $n_cont 14" \
            "migrated $n_migr 4" "layout $n_lay 5" "version $n_ver 6" \
            "hello $n_hel 12"; do
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
# T1, migrated-ness, is this step's own families: layout version hello.
# T2, preservation, is every other part above -- the recorded behaviour and the
# families an earlier step introduced -- at the weights `part` gave them. The
# weighted mean of all parts is printed for comparison and scores nothing.
read -r prod t1 t2 t1_missing <<< "$(product_score layout version hello)"
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
