#!/usr/bin/env bash
# PRODUCT SCORING (2026-10-13): the reward is now T1 x T2 x penalty -- see
# `Product scoring` at the end of main(). The part weights below still set
# the relative weight of each part inside its factor; any floor arithmetic
# below describes the superseded weighted mean.
# Step 01 scorer: does this tree build itself through PEP 517, and is what comes
# out still Bento?
#
# Five parts, weighted. Four of them are checks, counted by
# `group_fraction <label prefix>`; the fifth replays the recorded behaviour
# suite against the wheel this tree produced, installed into a venv.
#
#   build     3   the declaration, the tools' own verdicts, the two artifacts
#   metadata  3   every field the pinned tree declares about itself
#   contents  2   what the wheel ships, the sdist's sources, the rebuild from it
#   migrated  2   the properties that make this a migration rather than a rename
#   behaviour 6   5715 recorded observations, replayed from the installed wheel
#
# The whole thing is then multiplied by a penalty for build-escape suppressions:
# `--no-build-isolation`, `--skip-dependency-check` and
# `SETUPTOOLS_USE_DISTUTILS` -- the three the instruction's "No escapes"
# paragraph names, and nothing else -- and only where one of them is a live
# switch rather than a line that names it in a comment, because the instruction
# says *leaving a switch in the tree* and a line saying not to pass a flag is
# the opposite of one.
# The pinned tree has zero suppression sites of any kind, so "none present" is
# free and cannot be a check; adding one is not free, because each of those
# flags turns the property this step states into one the tooling has stopped
# checking.
#
# Two of the families are a precondition, not just checks: the `[build-system]`
# declaration, and two structural `migrated.` properties (`no-setup-py`,
# `no-monkey-patch`). Either half failing means the build is not attempted at
# all, and everything downstream of it is recorded as a failure with the
# reason. The third tree-read row, `migrated.declarative-metadata`, is scored
# and does not gate (wave R, 2026-10-21: a version listed in `dynamic` costs
# that row, not the build).
#
# The reason is specific to this repository. `setup.py` here is
# `bento.distutils`'s self-build, and the metadata it computes from `bento.info`
# is exactly the metadata this step asks for -- so setuptools' legacy path
# produces a *correct* wheel from a tree nobody migrated. Scoring that wheel
# would hand an untouched tree with a three-line `pyproject.toml` the metadata,
# contents and behaviour families, which together are 11 of the 16 weight.
set -u
. /tests/helpers.sh
. /tests/lib/harness.sh

STEP_NUM=01
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

# The freeze's two halves, checked before anything is scored against either.
# 5646 scored observations plus the 117 the defect ledger holds is the whole
# recording; if that stops being true, this verifier is measuring a different
# suite than the one NOTES describes and the number it would print is not a
# finding about the candidate.
if ! defect_ledger_intact 5763; then
    emit_detail
    report 0
    return 0
fi

give_app_to_unprivileged "$GRADE_APP"

ROWS=$GRADE/gate-rows.tsv
PENALTY=1

# --- the precondition -------------------------------------------------------
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

# --- install it and replay the recorded behaviour ---------------------------
corpus_pass=0; corpus_total=0; globals_pass=0; globals_total=0
wheel=$(newest_wheel "$GRADE/dist")
if [ "$built" = 1 ] && [ -n "$wheel" ] && make_venv python "$GRADE/venv"; then
    if venv_install "$GRADE/venv" "$wheel"; then
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
        # Unscored, and deliberately so: the two PEP 479 crashes were lifted out
        # of the expectation, so a tree that still has them and a tree that
        # fixed them score the same. Printing the count keeps a run where that
        # behaviour moved visible in the log.
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

# --- weigh it ---------------------------------------------------------------
read -r v_build n_build <<< "$(group_fraction pyproject. build-system. tool. dist.)"
read -r v_meta  n_meta  <<< "$(group_fraction meta.)"
read -r v_cont  n_cont  <<< "$(group_fraction wheel. sdist. install.)"
read -r v_migr  n_migr  <<< "$(group_fraction migrated.)"
behaviour=$(fraction "$((corpus_pass + globals_pass))" "$((corpus_total + globals_total))")

part build     3 "$v_build"
part metadata  3 "$v_meta"
part contents  2 "$v_cont"
part migrated  2 "$v_migr"
part behaviour 6 "$behaviour"
printf 'group sizes: build=%s metadata=%s contents=%s migrated=%s\n' \
    "$n_build" "$n_meta" "$n_cont" "$n_migr"
printf 'observations: corpus %s/%s  globals %s/%s\n' \
    "$corpus_pass" "$corpus_total" "$globals_pass" "$globals_total"
printf 'suppression penalty: %s\n' "$PENALTY"

# A prefix that matches nothing scores 0 and is indistinguishable from a group
# that failed, so the sizes are asserted rather than printed and hoped for.
#
# contents = 14: wheel.declared-packages, wheel.core-packages, five wheel.data.*,
# wheel.licence-file,
# sdist.sources, sdist.rebuilds and sdist.builds, plus install.wheel,
# install.imports and install.console-script.
for pair in "build $n_build 12" "metadata $n_meta 14" "contents $n_cont 14" \
            "migrated $n_migr 4"; do
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
# T1, migrated-ness, is this step's own families: build metadata contents migrated.
# T2, preservation, is every other part above -- the recorded behaviour and the
# families an earlier step introduced -- at the weights `part` gave them. The
# weighted mean of all parts is printed for comparison and scores nothing.
read -r prod t1 t2 t1_missing <<< "$(product_score build metadata contents migrated)"
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
