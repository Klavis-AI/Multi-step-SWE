#!/usr/bin/env bash
# PRODUCT SCORING (2026-10-13): the reward is now T1 x T2 x penalty -- see
# `Product scoring` at the end of main(). The part weights below still set
# the relative weight of each part inside its factor; any floor arithmetic
# below describes the superseded weighted mean.
# Step 06 scorer: is `distutils` gone from the root distribution's own code, and
# does this repository describe itself in the formats an index, an installer and
# a consumer actually read?
#
# Thirty-four parts, a hundred and sixty-three units. Twenty-one of them are
# inherited -- five from step 01, three from step 02, four from step 03, four
# from step 04 and five from step 05 -- re-run unchanged and recorded against
# the family that first required them, because a step that quietly loses what an
# earlier one established is the failure mode a multi-step task exists to catch.
# Thirteen are this step's.
#
#   build      1  inherited (01): the declaration, the tools' verdicts, the artifacts
#   metadata   1  inherited (01): every field the pinned tree declares about itself
#   contents   1  inherited (01): what the wheel ships, the sdist, the rebuild
#   migrated   1  inherited (01): the properties that make this a migration
#   behaviour 49  inherited (01): 5715 recorded observations, from the installed wheel
#   layout     1  inherited (02): the src/ trees, and the root that imports nothing
#   version    1  inherited (02): one declared source, reported by the install
#   hello      1  inherited (02): the example extension, still a distribution
#   lock       1  inherited (03): the lockfile, hash-pinned, current, and complete
#                 enough to build an environment with the index unreachable
#   workspace  1  inherited (03): the members, `yaku` resolved from here
#   groups     1  inherited (03): the declared groups, each one still installing
#   unvendor   1  inherited (03): the four trees gone, the dependencies real
#   abi3       1  inherited (04): the stable-ABI tag, true of the interpreters it
#                 claims, over a floor the distribution does not contradict
#   gil        1  inherited (04): the extension still says it does not need the
#                 lock, and an interpreter that had none still has none
#   ft         1  inherited (04): the free-threaded wheel, still built and still
#                 importable there
#   pure       1  inherited (04): the two distributions with no compiled code in
#                 them still say so
#   repro-wheel     1  inherited (05): every wheel byte-identical across two builds
#   repro-sdist     1  inherited (05): and every sdist, from two copies of the tree
#                 in two differently-named directories
#   repro-epoch     1  inherited (05): every timestamp inside every artifact is the
#                 `SOURCE_DATE_EPOCH` the build was given
#   pin-requires    1  inherited (05): every `[build-system]` requirement pinned exact
#   pin-effective   1  inherited (05): and the generator a built wheel reports is the
#                 version that pin names
#   distutils-imports   24  own: nothing the root distribution ships imports
#                 `distutils` outside the three documented keepers, and nothing
#                 imports `setuptools._distutils` at all -- statement,
#                 function-local, `try`-wrapped, dotted, and through `importlib`
#                 with a literal or a computed name
#   distutils-meta       1  own: and the class that writes this distribution's
#                 PKG-INFO is this repository's own, asked of an installed copy
#                 rather than of the source. The import row above cannot see
#                 this one: `convert/core.py` is allowed `distutils.dist`, so a
#                 tree that takes `DistributionMetadata` from there and
#                 re-exports it through `bento/compat/dist.py` passes every
#                 static row while still borrowing the writer
#   distutils-trees     12  own: the subpackage that existed only to serve that
#                 dependency is gone, its own `tests/` with it, wherever the
#                 tree now puts `bento/`
#   distutils-names      6  own, guard: `pkg_to_distutils`, `pkg_to_distutils_meta`
#                 and `pkg_to_distutils_meta_pkg_info` still exist, at module
#                 level, in one module of a built wheel. Free until broken, and
#                 it is what makes the import row safe to pass: without it the
#                 cheapest way to empty the import scan is to delete the callers
#   setuppy              5  own: no `setup.py` anywhere in the repository
#   license-expression  12  own: each distribution whose wheel is `py3-none-any`
#                 declares an SPDX expression at core metadata 2.4 or later, and
#                 no free-text `License:` beside it
#   license-files        6  own: and every file its `License-File:` fields name is
#                 inside the wheel, under `.dist-info/licenses/` -- by either of
#                 the two spellings in circulation, the basename `hatchling`
#                 writes and the `.dist-info`-relative path `setuptools` writes,
#                 which name the same member
#   typed                9  own: `py.typed` at the root of every package every
#                 built wheel installs -- three distributions, two back ends,
#                 and one wheel that installs two packages
#   pylock               6  own: every exported lock in the tree is current with
#                 `uv.lock` in both directions, and pins a version for every
#                 package that is not sourced from a directory, a VCS or an
#                 archive
#   reqtxt               1  own, guard: no `*requirements*.txt` at the repository
#                 root -- neither the three files step 03's milestone turned into
#                 dependency groups coming back, nor an export of the lock in the
#                 older format landing beside the one this step asks for, which is
#                 what `uv export`'s default output filename does to a candidate
#                 who exports twice
#   sbom-present         3  own: a CycloneDX document in the tree, 1.5 or later,
#                 found by content and not by filename
#   sbom-validates       8  own: and it validates against the schema of the version
#                 it declares, and its components agree with the lock by name
#                 and by version, and cover the root's closure
#   twine                1  own, guard: `twine check` over everything this scorer
#                 built exits 0. Design note 4 Q5 measured the tree this step
#                 starts from already passing, so it distinguishes nothing; it
#                 is here because this step edits the very metadata `twine
#                 check` reads, and a guard that was green before the step and
#                 red after it is the regression no other row in this group can
#                 see
#
# Two scored families. "Still works" is `behaviour` -- 49/163 = 30.1%, over the
# line -- and "migrated" is everything else, 114/163 = 69.9%.
#
# The floor is the arithmetic that decides whether this step is a step. A tree
# that solved step 05 and stopped keeps every one of the twenty inherited
# guards, keeps the whole behaviour corpus -- this milestone changes no
# behaviour, which is the point of it -- and passes three of this step's own
# rows for nothing: `twine` (see above), `distutils-names`, which is an
# anti-rename guard and is supposed to be free until you break it, and `reqtxt`,
# which is the same shape -- step 03 removed the three files it names and nothing
# between there and here puts one back. So the floor is
# 20 + 49 + 1 + 6 + 1 = 77/163 = 0.4724, still under item 6's half mark.
# `distutils-meta` is not in that sum: the tree that stopped at step 05 still
# has `bento/compat/dist.py` aliasing the standard library's class, which is
# the one thing that row asks about.
#
# That number is why the twenty inherited guards are worth one unit each rather
# than the two and three units the earlier steps gave them. Step 06 is the only
# step in this task whose predecessor already passes everything the predecessor
# was scored on, so rule 5's "neither family below ~30%" and item 6's "at most
# 0.5" become two inequalities over the same weights that very nearly have no
# solution; `authoring/design/06-standards-closure.md` §8 works it out. The room
# had to come out of the guards, which is where it should come from: by this
# step they are asking twenty questions four earlier steps already answered.
#
# `distutils-imports` is one row and not one per module, for the reason
# `repro.sdist` is one row in step 05: scored per module it would hand the
# untouched tree the thirteen-fourteenths of its weight that the modules it
# never touches represent. The detail string names the first six offending
# sites with file and line, so the row is diagnosable without being divisible.
#
# The import scan is scoped to the root distribution's own code, and the scope
# is computed rather than written down: `[tool.uv.workspace] members` in the
# root `pyproject.toml`, kept where the member declares its own `[project]
# name`. `yaku` is the member that matters -- it reaches for `distutils`'s
# compiler driver, `ccompiler.new_compiler` and `sysconfig.customize_compiler`,
# the one corner PEP 632 hands to `setuptools._distutils` rather than to a
# standard-library successor, and the one this suite's inherited `escapes` row
# forbids the tree to depend on. Design §9 has the measurement: 56 sites in the
# tree, 41 in scope, 13 of them `yaku`'s. Design §9.1 narrows it once more:
# three modules in the root -- `convert/core.py`, `commands/build_distutils.py`
# and `commands/wininst_utils.py` -- drive `distutils` as a third-party tool
# rather than using it to package this distribution, and `DISTUTILS_KEPT` in
# `step06_gates.py` names each with the submodules it may import. The allowance
# is a subset test: dropping one passes, adding an unlisted one fails, and
# `setuptools._distutils` is on no list at all. 19 of the 41 are keepers, so 22
# sites are what the step actually removes.
#
# The allowance is over import sites, and an import site is less than a use.
# `convert/core.py` may import `distutils.dist`, for `Distribution`; the same
# module also holds `DistributionMetadata`, and a tree that takes that from
# there and re-exports it satisfies every static row in this group. What it
# does not satisfy is `distutils-meta`, which imports the installed
# distribution and reads the class off it. Two rows rather than a cleverer one,
# because the static row's job is to be diagnosable per file and line and the
# runtime row's job is to be true regardless of spelling.
#
# The whole thing is multiplied by step 01's build-escape suppression penalty,
# on the same terms as steps 02, 03, 04 and 05: the switches are still
# forbidden, so the penalty is still applied.
#
# Five ordering notes that are load-bearing:
#
#   * All five static gates run FIRST, before anything is built. A build leaves
#     `build/` and `*.egg-info` behind, which are copies of the tree that would
#     be scanned twice and counted twice -- and this step's import scan is the
#     row in this task with the most to lose from that.
#   * The behaviour venv is given the root wheel AND a wheel built from every
#     workspace member the root wheel requires. `[tool.uv.sources]` is uv's own
#     metadata and does not travel into a wheel, so a root wheel that declares
#     `yaku` asks pip for a distribution that exists on no index.
#   * Step 04's four builds and step 05's three builds are kept exactly as those
#     steps spelled them. An inherited row is inherited only if it is taken the
#     way the step that wrote it took it; reading an inherited row out of an
#     artifact produced by a command that step never ran makes this step's copy
#     of it a different measurement wearing the same label.
#   * This step's own artifact rows read step 04's three wheel directories and
#     not step 05's reproducibility output, which holds further copies of all
#     three distributions. A `py.typed` row that read the same distribution
#     twice would be reporting one fact as two.
#   * `twine check` runs over everything -- step 01's front-end wheel and sdist,
#     step 04's four wheels, and step 05's first reproducibility build, which is
#     the only place all three distributions' sdists exist at once. Plain, not
#     `--strict`: design 4 Q5 measured the extension distribution passing with
#     two warnings, for a `readme` nothing in this repository ever asked it to
#     have, and `--strict` would make that a row no candidate can pass without
#     inventing one.
set -u
. /tests/helpers.sh
. /tests/lib/harness.sh

STEP_NUM=06
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

# --- step 04's read of the extension's C, also before anything is built -----
#
# The three distribution names the builds at the bottom need come back from the
# same gate on stderr, each derived where it was already derived: the example's
# own declaration in the pinned tree, the recorded metadata every step checks
# the root's name against, and the constant step 03's own gates use. Written
# down here they would be a second copy, free to disagree with the rows.
python /tests/step04_gates.py source "$GRADE_APP" "$GRADE_PRISTINE" \
    > "$ROWS" 2> "$GRADE_LOG/gates04-source.err"
rows_from_gates "$ROWS"
PKG_HELLO=$(awk '$1 == "PKG" && $2 == "hello" { print $3 }' "$GRADE_LOG/gates04-source.err")
PKG_ROOT=$(awk '$1 == "PKG" && $2 == "root" { print $3 }' "$GRADE_LOG/gates04-source.err")
PKG_YAKU=$(awk '$1 == "PKG" && $2 == "yaku" { print $3 }' "$GRADE_LOG/gates04-source.err")
printf 'distributions to build: hello=%s root=%s yaku=%s\n' \
    "${PKG_HELLO:-?}" "${PKG_ROOT:-?}" "${PKG_YAKU:-?}"
if [ -z "${PKG_HELLO:-}" ] || [ -z "${PKG_ROOT:-}" ] || [ -z "${PKG_YAKU:-}" ]; then
    printf 'HARNESS FAULT: the three distribution names could not be derived from the pinned tree and the recording; see gates04-source.err\n'
    sed -n '1,20p' "$GRADE_LOG/gates04-source.err"
    emit_detail
    report 0
    return 0
fi

# --- step 05's read of the build-system tables, also before anything is built
#
# It also reports where the distributions are, on stderr, derived from the same
# whole-repo scan the row itself uses. This step builds "every distribution in
# the workspace" twice, and the set has to be the set the row scored, not a
# list written down here that a candidate could put a fourth distribution
# outside of.
python /tests/step05_gates.py source "$GRADE_APP" \
    > "$ROWS" 2> "$GRADE_LOG/gates05-source.err"
rows_from_gates "$ROWS"
DIST_DIRS=$(awk '$1 == "DIST" { print $3 }' "$GRADE_LOG/gates05-source.err")
printf 'distributions in the workspace: %s\n' \
    "$(sed -n 's/^DIST /  /p' "$GRADE_LOG/gates05-source.err" | tr '\n' ' ')"
# An empty set is a candidate condition and not a harness one, so it is reported
# and not bailed on: a tree with no `[build-system]` table anywhere has already
# lost `pyproject.`, `build-system.` and `pin.requires` above, and the four
# artifact rows below fail on their own terms -- `compare_builds` says "neither
# build produced a wheel", `gate_epoch` says "no artifacts were built",
# `gate_pin_effective` says "no wheels were built, so nothing reports a
# generator". Returning 0 here instead would replace four specific findings with
# one number and would hand a candidate the same reward for an empty tree as for
# a broken one.
if [ -z "$DIST_DIRS" ]; then
    printf 'no distribution could be located in the tree; the builds below have nothing to build and the repro rows say so\n'
    sed -n '1,20p' "$GRADE_LOG/gates05-source.err"
fi

# --- step 06's read of the tree itself, also before anything is built -------
#
# Six rows, all of them text and ASTs: every import of `distutils` in the root
# distribution's own code outside the documented keepers, the subpackage that
# existed only to serve it, every
# `setup.py`, the exported lock against `uv.lock`, and whatever CycloneDX
# documents the tree carries. Nothing here builds and nothing here executes
# candidate code.
#
# It runs here, with the other four static gates, for the reason the header's
# first ordering note gives. It is also the gate that reports which directories
# it exempted from the import scan, on stderr, derived from the candidate's own
# `[tool.uv.workspace] members` -- so a run's log says what was in scope rather
# than leaving it to be inferred from a score.
python /tests/step06_gates.py source "$GRADE_APP" \
    > "$ROWS" 2> "$GRADE_LOG/gates06-source.err"
rows_from_gates "$ROWS"
sed -n 's/^EXEMPT /exempt from the import scan: /p' "$GRADE_LOG/gates06-source.err"

# --- step 01's precondition -------------------------------------------------
python /tests/step01_gates.py precondition "$GRADE_APP" \
    > "$ROWS" 2> "$GRADE_LOG/gates-precondition.err"
precond=$?
rows_from_gates "$ROWS"

# --- the tools' own verdicts and the build ----------------------------------
built=0
if [ "$precond" -eq 0 ]; then
    # The root's table and every workspace member's: this milestone edits all
    # three, and the line's gate is that the project tables validate, not one
    # of them. The members are the ones step 03's static read resolved
    # (`MEMBER <name> <dir>`, relative to the tree). (Migration rubric review
    # round 2, 2026-10-05.)
    vpp_rc=0
    : > "$GRADE_LOG/validate-pyproject.log"
    for vpp_rel in . $(awk '$1 == "MEMBER" { print $3 }' "$GRADE_LOG/gates03-static.err"); do
        printf '== %s/pyproject.toml\n' "$vpp_rel" >> "$GRADE_LOG/validate-pyproject.log"
        run_unprivileged validate-pyproject "$GRADE_APP/$vpp_rel/pyproject.toml" \
            >> "$GRADE_LOG/validate-pyproject.log" 2>&1 || vpp_rc=1
    done
    record 01 tool.validate-pyproject "$(test $vpp_rc -eq 0 && echo 1 || echo 0)" \
        "$(grep -v '^==' "$GRADE_LOG/validate-pyproject.log" | tail -c 200 | tr '\n' ' ')"

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

        # --- step 06: where the PKG-INFO class was defined ------------------
        #
        # The static import rows cannot answer this and the header says why:
        # one of the three documented keepers is allowed `distutils.dist`, so
        # the standard library's `DistributionMetadata` can be taken from there
        # and re-exported under this distribution's own name without any AST
        # scan seeing a thing. So it is asked of the installed copy instead,
        # by the same import the repository's own `test_bentomaker.py` makes,
        # and answered from `__mro__`: a class defined in this repository, a
        # subclass of one that is not, and a bare alias of one that is not are
        # three different answers and only the first passes.
        #
        # `setuptools` is on the reject list beside `distutils` for the reason
        # `DISTUTILS_KEPT` gives: the vendored copy is the same removed code
        # under another name.
        #
        # The second half of the probe is what the ancestry test cannot see. A
        # class built out of the removed one rather than from it --
        #
        #     ns = dict(vars(distutils.dist.DistributionMetadata))
        #     DistributionMetadata = type("DistributionMetadata", (object,), ns)
        #
        # -- has `object` for an ancestor and this repository for a
        # `__module__`, because `type()` takes the name from its argument and
        # the module from the caller. Its `__mro__` is clean and every line of
        # code in it is still the standard library's. So the callables the class
        # holds are asked where they were defined, which is the one thing
        # copying a namespace cannot change: a function carries the
        # `__module__` of the file it was written in wherever it is later
        # attached. `__func__` is unwrapped first so that a `staticmethod` or a
        # `classmethod` answers for the function inside it rather than for the
        # wrapper, and non-callables are skipped -- `_METHOD_BASENAMES` is a
        # tuple of strings and a tuple was defined nowhere.
        run_unprivileged "$GRADE/venv/bin/python" -c '
import sys
from bento.compat.dist import DistributionMetadata as M

def removed(mod):
    return (mod == "distutils" or mod.startswith("distutils.")
            or mod == "setuptools" or mod.startswith("setuptools."))

mods = [(getattr(c, "__module__", "") or "?") for c in M.__mro__]
held = []
for name, value in sorted(vars(M).items()):
    value = getattr(value, "__func__", value)
    if callable(value):
        held.append((name, getattr(value, "__module__", "") or "?"))
bad = ["mro " + m for m in mods if removed(m)]
bad += ["%s() from %s" % (n, m) for n, m in held if removed(m)]
print("DistributionMetadata -> %s.%s; mro %s; %d callables held: %s"
      % (M.__module__, M.__name__, ", ".join(mods), len(held),
         ", ".join(sorted(set(m for _n, m in held))) or "none"))
if bad:
    print("still the removed code: " + ", ".join(bad[:6]))
sys.exit(1 if bad else 0)
' > "$GRADE_LOG/meta-ported.log" 2>&1
        record 06 distutils.meta-ported "$(test $? -eq 0 && echo 1 || echo 0)" \
            "$(tail -c 200 "$GRADE_LOG/meta-ported.log" | tr '\n' ' ')"

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
        record 06 distutils.meta-ported 0 "not installed"
    fi
else
    record 01 install.wheel 0 "no wheel to install"
    record 01 install.imports 0 "no wheel to install"
    record 01 install.console-script 0 "no wheel to install"
    record 06 distutils.meta-ported 0 "no wheel to install"
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
# which any tree carrying a lock invites -- leaves that `.venv` in `/app`, and the
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
# directory whose other four entries the `unvendor.dirs-gone` row above requires
# to stay gone, and `bento/core/meta.py` and three command modules import them.
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

# --- step 04's four builds, taken the way step 04 took them -----------------
#
# Four builds of three distributions, every one of them through the tool step
# 03 put in this tree and every one from the workspace root, because that is
# where a workspace is built from. Two differ only in which interpreter they
# are told to use, and that difference was the subject of step 04: one wheel
# that claims to serve several interpreters, one built for the interpreter that
# cannot be served that way. They are kept here verbatim -- see the fourth
# ordering note in the header -- and this step's own builds are further down.
#
# They run from the scratch copy rather than from the scored tree, because a
# build writes `*.egg-info` beside what it built and every row above reads
# `/grade/app`. `UV_PYTHON_DOWNLOADS=never` is the other half of the
# interpreter pin -- an interpreter this image does not have should be
# reported, not fetched.
#
# The cache here is the image's warm one and NOT the scratch cache the syncs
# use, which is the opposite choice from the section above and for the reason
# that section gives. A sync must be answered out of a cache the candidate's
# own lock filled, or `lock.offline-sync` is a question about the image. A
# build must be answered out of the cache the image was built with, because
# what a build fetches is its BACKEND -- `scikit-build-core`, `cmake`, `ninja`,
# `setuptools`, whichever the candidate declared -- and those are pinned in
# this image and warmed into `/opt/cache/uv` precisely so that a slow index is
# never scored as a candidate failure. Four builds against a cold cache would
# be four more index round-trips, and every one of them is a way for a slow
# index to take down rows that have nothing to do with the index. The cache is
# root's and `uv` refuses to start when it cannot
# write its own, so it is opened up for the unprivileged runner rather than
# copied: it holds no distribution any row below resolves, only build backends.
BUILDCACHE=/opt/cache/uv
mkdir -p "$BUILDCACHE" && chmod -R a+rwX "$BUILDCACHE"
UVBUILD=$GRADE/uv-build
ABI3DIR=$UVBUILD/abi3
FTDIR=$UVBUILD/ft
PUREDIR=$UVBUILD/pure
rm -rf "$UVBUILD"
mkdir -p "$ABI3DIR" "$FTDIR" "$PUREDIR"
give_app_to_unprivileged "$UVBUILD"

uv_build_env() {
    printf '%s\n' "UV_CACHE_DIR=$BUILDCACHE" "UV_PYTHON_DOWNLOADS=never" \
        "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1409491949}"
}

# uv_build <out-dir> <label> <package> [flag...]
uv_build() {
    local out="$1" label="$2" pkg="$3"; shift 3
    local log="$GRADE_LOG/uv-build-$label.log" rc
    ( cd "$SYNC" && run_unprivileged env $(uv_build_env) \
        uv build --wheel --package "$pkg" --out-dir "$out" "$@" ) > "$log" 2>&1
    rc=$?
    printf 'uv build --wheel --package %s %s: exit %s; %s holds %s\n' \
        "$pkg" "$*" "$rc" "$label" \
        "$(ls -1 "$out" 2>/dev/null | tr '\n' ' ' | tail -c 140)"
    [ "$rc" -eq 0 ] || printf '    %s\n' "$(tail -c 300 "$log" | tr '\n' ' ')"
    return $rc
}

uv_build "$ABI3DIR" abi3 "$PKG_HELLO" --python 3.11
uv_build "$FTDIR"   ft   "$PKG_HELLO" --python 3.13t
uv_build "$PUREDIR" root "$PKG_ROOT"
uv_build "$PUREDIR" yaku "$PKG_YAKU"

python /tests/step04_gates.py wheels "$ABI3DIR" "$FTDIR" "$PUREDIR" \
    "$GRADE_PRISTINE" > "$ROWS" 2> "$GRADE_LOG/gates04-wheels.err"
rows_from_gates "$ROWS"
ABI3WHEEL=$(awk '$1 == "ABI3WHEEL" { print $2 }' "$GRADE_LOG/gates04-wheels.err")
FTWHEEL=$(awk '$1 == "FTWHEEL" { print $2 }' "$GRADE_LOG/gates04-wheels.err")
printf 'wheels to run: abi3=%s free-threaded=%s\n' \
    "$(basename "${ABI3WHEEL:-<none>}")" "$(basename "${FTWHEEL:-<none>}")"

# What the ELF inside each wheel actually requires of a system, which is a
# different question from what the wheel's tag claims. Recorded and scored by
# nothing: repairing a wheel for redistribution is a release-time act, so
# neither running it here nor asking the candidate to commit its output belongs
# in this tree. The verdict is in the log for whoever reads a run.
for w in "$ABI3WHEEL" "$FTWHEEL"; do
    [ -n "$w" ] || continue
    printf '%s\n' "$(basename "$w")" >> "$GRADE_LOG/auditwheel.txt"
    auditwheel show "$w" >> "$GRADE_LOG/auditwheel.txt" 2>&1 \
        || echo "auditwheel show exited $?" >> "$GRADE_LOG/auditwheel.txt"
done
sed -n '1,40p' "$GRADE_LOG/auditwheel.txt" 2>/dev/null \
    | sed 's/^/  auditwheel: /'

# --- does that one wheel serve the interpreters it claims? ------------------
#
# The tag is a claim and this is the fact behind it: one file, installed into a
# fresh environment of each interpreter this image carries that has a global
# lock, and asked for the two things step 02 already pins about this example.
# The environments are made first, all of them, because an interpreter that
# cannot host one is a fact about the image and not about the candidate, and a
# row that failed for that reason would read as a wheel that does not install.
ABI3_PYTHONS="python3.11 python3.12 python3.13"
for py in $ABI3_PYTHONS; do
    if ! make_venv "$py" "$GRADE/abi3-$py"; then
        printf 'HARNESS FAULT: %s cannot make a virtual environment in this image, so abi3.serves measures nothing\n' "$py"
        tail -c 300 "$GRADE_LOG/venv.abi3-$py.log"
        emit_detail
        report 0
        return 0
    fi
done

printf '%s\n' "$HELLO_C_OUT" > "$GRADE_LOG/abi3-hello.want"
if [ -n "$ABI3WHEEL" ]; then
    serves_faults=""
    for py in $ABI3_PYTHONS; do
        if ! venv_install "$GRADE/abi3-$py" "$ABI3WHEEL"; then
            serves_faults="$serves_faults $py(install refused: $(tail -c 90 "$GRADE_LOG/venv.abi3-$py.log" | tr '\n' ' '))"
            continue
        fi
        ( cd "$BANNER" && run_unprivileged "$GRADE/abi3-$py/bin/python" -P -c \
            'import sys, hello; r = hello.hello(); sys.stderr.write("ALIAS %r RETURNED %r\n" % (hello.bar.foo is hello.foo, r))' ) \
            > "$GRADE_LOG/abi3-$py.out" 2> "$GRADE_LOG/abi3-$py.err"
        serves_rc=$?
        cmp -s "$GRADE_LOG/abi3-$py.out" "$GRADE_LOG/abi3-hello.want" \
            && serves_bytes=1 || serves_bytes=0
        serves_alias=$(grep -c '^ALIAS True RETURNED None$' "$GRADE_LOG/abi3-$py.err")
        if [ "$serves_rc" -ne 0 ] || [ "$serves_bytes" != 1 ] \
           || [ "$serves_alias" != 1 ]; then
            serves_faults="$serves_faults $py(exit $serves_rc bytes=$serves_bytes alias=$serves_alias: $(tail -c 90 "$GRADE_LOG/abi3-$py.err" | tr '\n' ' '))"
        fi
    done
    record 04 abi3.serves "$(test -z "$serves_faults" && echo 1 || echo 0)" \
        "$(test -n "$serves_faults" \
            && echo "$(basename "$ABI3WHEEL") did not serve:$serves_faults" \
            || echo "$(basename "$ABI3WHEEL") installed and printed the pinned greeting on $ABI3_PYTHONS")"
else
    record 04 abi3.serves 0 "the build on the reference interpreter produced no wheel to install"
fi

# --- the free-threaded interpreter, before and after the import -------------
#
# The control first. This image's free-threaded interpreter has to be one, and
# has to start with no lock held, or `gil.stays-off` would pass for any module
# at all -- including one that never says anything about the lock.
FTPY=python3.13t
ftcontrol=$( ( cd "$BANNER" && run_unprivileged "$FTPY" -P -c \
    'import sys, sysconfig; print(sysconfig.get_config_var("Py_GIL_DISABLED"), sys._is_gil_enabled())' ) \
    2> "$GRADE_LOG/ft-control.err" | head -n 1 | tr -d '\r')
if [ "$ftcontrol" != "1 False" ]; then
    printf 'HARNESS FAULT: %s answers "%s" rather than "1 False", so gil.stays-off measures nothing\n' \
        "$FTPY" "${ftcontrol:-<nothing>}"
    tail -c 300 "$GRADE_LOG/ft-control.err"
    emit_detail
    report 0
    return 0
fi

ft_installed=0
if [ -n "$FTWHEEL" ] && make_venv "$FTPY" "$GRADE/ft-venv" \
        && venv_install "$GRADE/ft-venv" "$FTWHEEL"; then
    ft_installed=1
fi

if [ "$ft_installed" = 1 ]; then
    ( cd "$BANNER" && run_unprivileged "$GRADE/ft-venv/bin/python" -P -c \
        'import sys, hello; r = hello.hello(); sys.stderr.write("RETURNED %r\n" % (r,))' ) \
        > "$GRADE_LOG/ft-hello.out" 2> "$GRADE_LOG/ft-hello.err"
    ft_rc=$?
    cmp -s "$GRADE_LOG/ft-hello.out" "$GRADE_LOG/abi3-hello.want" \
        && ft_bytes=1 || ft_bytes=0
    ft_ret=$(grep -c '^RETURNED None$' "$GRADE_LOG/ft-hello.err")
    ok=0
    if [ "$ft_rc" -eq 0 ] && [ "$ft_bytes" = 1 ] && [ "$ft_ret" = 1 ]; then ok=1; fi
    record 04 ft.imports "$ok" \
        "$(basename "$FTWHEEL") on $FTPY: hello.hello() -> $(printf %s "$(cat "$GRADE_LOG/ft-hello.out")" | tr '\n' '|' | tail -c 60) bytes=$ft_bytes returns-None=$ft_ret exit $ft_rc; $(tail -c 100 "$GRADE_LOG/ft-hello.err" | tr '\n' ' ')"

    # Two questions in one process, because the answer to the second is only
    # meaningful next to the first: the interpreter had no lock, and after the
    # extension was imported it still has none. The warning is read as well as
    # the flag -- an interpreter that turns the lock back on says so on stderr,
    # and a module that declared nothing is exactly what makes it say it.
    #
    # The pattern and the detail string below are step 04's, character for
    # character. Step 04 owns this row, and it matches the text CPython really
    # emits -- `RuntimeWarning:` and the phrase anchored together on the one
    # line the message occupies -- rather than a phrase that is merely nearby.
    # An inherited row read through a looser pattern is a different measurement
    # wearing the same label: the two steps would report different numbers for
    # the same tree the first time an unrelated line mentioned the GIL.
    ( cd "$BANNER" && run_unprivileged "$GRADE/ft-venv/bin/python" -P -c \
        'import sys; before = sys._is_gil_enabled(); import hello, hello._bar; print("BEFORE", before, "AFTER", sys._is_gil_enabled())' ) \
        > "$GRADE_LOG/ft-gil.out" 2> "$GRADE_LOG/ft-gil.err"
    gil_rc=$?
    gil_line=$(grep -m 1 '^BEFORE ' "$GRADE_LOG/ft-gil.out" | tr -d '\r')
    gil_warned=$(grep -cE 'RuntimeWarning: The global interpreter lock \(GIL\) has been enabled' "$GRADE_LOG/ft-gil.err")
    ok=0
    if [ "$gil_rc" -eq 0 ] && [ "$gil_line" = "BEFORE False AFTER False" ] \
       && [ "$gil_warned" = 0 ]; then ok=1; fi
    record 04 gil.stays-off "$ok" \
        "importing the extension on $FTPY: ${gil_line:-<no answer>}, exit $gil_rc, '(GIL) has been enabled' RuntimeWarnings=$gil_warned; $(tail -c 120 "$GRADE_LOG/ft-gil.err" | tr '\n' ' ')"
else
    record 04 ft.imports 0 "the build on the free-threaded interpreter left no wheel to install; see uv-build-ft.log"
    record 04 gil.stays-off 0 "nothing was installed on $FTPY, so nothing could be imported there"
fi

# --- this step's own builds -------------------------------------------------
#
# The same workspace built twice at one epoch, from two copies of the tree that
# share no directory, into two output directories, and then compared byte for
# byte -- and a third time, from a third copy, at a second epoch, which nothing
# compares with anything and which only `repro.epoch` reads.
#
# Through `build_dists`, which is the shared helper every `01.tool.build-exit`
# and every `sdist.rebuilds` row in this suite already goes through: the
# standard PEP 517 front end with isolation left on, `--sdist --wheel` so each
# artifact is built from the tree rather than the wheel from the sdist, and
# `SOURCE_DATE_EPOCH` exported by the helper itself. Using the same helper is
# not tidiness -- it is the only way this step's rows and the inherited rows are
# talking about the same build. A second spelling of "build this tree" here
# would let `repro.sdist` be true of a command nothing else in the task runs.
#
# The two directories differ in name and not in length. What the instruction
# asks for is "a second copy of the tree in a different directory"; a build
# whose output depends on how many characters the path has is a different and
# stronger property, it is not what the instruction says, and scoring it would
# be rule 4 in the direction that hurts -- a row failing for a reason the
# candidate was never told about. `repro.wheel`'s detail string names the first
# differing member either way, so a path leak that does show up is diagnosable.
# The third copy's directory name is a different length again, and that costs
# nothing: nothing is compared against its bytes, because a build at a different
# epoch is supposed to produce different bytes.
#
# Every environment the candidate left in the tree is dropped from all three
# copies, for the reason the scratch tree gives above: a `.venv` in `/app` is a
# question about what is lying around, not a question about the build.
REPRO=$GRADE/repro
REPRO_ONE=$REPRO/one
REPRO_TWO=$REPRO/two
REPRO_THREE=$REPRO/three
REPRO_OUT1=$REPRO/dist-one
REPRO_OUT2=$REPRO/dist-two
REPRO_OUT3=$REPRO/dist-three
rm -rf "$REPRO"
mkdir -p "$REPRO_ONE" "$REPRO_TWO" "$REPRO_THREE" \
         "$REPRO_OUT1" "$REPRO_OUT2" "$REPRO_OUT3"
cp -a "$GRADE_APP/." "$REPRO_ONE"
cp -a "$GRADE_APP/." "$REPRO_TWO"
cp -a "$GRADE_APP/." "$REPRO_THREE"
for copy in "$REPRO_ONE" "$REPRO_TWO" "$REPRO_THREE"; do
    find "$copy" -name pyvenv.cfg -printf '%h\n' 2>/dev/null | sort -u \
        | while read -r leftover; do rm -rf "$leftover"; done
done
give_app_to_unprivileged "$REPRO"

# The same expression `build_dists` itself uses, so the value the gate is told
# to expect cannot drift from the value the builds were given.
REPRO_EPOCH=${SOURCE_DATE_EPOCH:-1409491949}

# And the second epoch, which the third build is given. Every clause of this
# integer is deliberate:
#
#   * it is not 1409491949. That number is the example in the
#     `SOURCE_DATE_EPOCH` specification the instruction links, the git author
#     and committer date the environment image stamps onto `/app`, and the
#     default above -- a back end frozen to it is precisely what the third build
#     exists to catch, and a second epoch that repeated it would catch nothing;
#   * it is not 0, which archive writers use as their own "no timestamp"
#     sentinel and which a zip entry cannot even hold, DOS times beginning in
#     1980. A tree that failed at 0 would be failing the format rather than the
#     requirement, and this row is not about the format;
#   * 1234567891 is 2009-02-13, older than every source mtime in the image, so
#     the two lawful readings of the specification agree on it: a back end that
#     clamps each file's mtime down to the epoch and a back end that writes the
#     epoch over all of them produce the same answer here, and neither is
#     scored as though it were the other's bug;
#   * it is odd, like the default, so the two-second granularity of a DOS
#     timestamp is exercised at a second value as well -- a wheel has to carry
#     1234567890 here, and a reader that had quietly hard-coded the first
#     epoch's even second would show up instead of passing twice.
REPRO_EPOCH2=1234567891
printf 'reproducibility: two builds at SOURCE_DATE_EPOCH=%s, a third at %s\n' \
    "$REPRO_EPOCH" "$REPRO_EPOCH2"

# repro_build <tree-copy> <out-dir> <label> <epoch>
#
# `build_dists` writes its log under a name derived from the output directory,
# and three distributions land in one output directory, so each build's log is
# taken out of the way -- appended to the log this distribution keeps, then
# removed -- before the next build writes over it.
#
# The epoch goes into a subshell around the call rather than into an assignment
# in front of it. `build_dists` reads `$SOURCE_DATE_EPOCH` out of the
# environment and defaults it when it is unset, so an export that escaped this
# wrapper would silently re-epoch every later caller of that helper in the run
# -- and the whole point of the third build is that the epoch it is given is not
# the one the other two were.
#
# `TZ`, `LC_ALL` and `PYTHONHASHSEED` ride in the same subshell. They are the
# three the corpus runner pins a few hundred lines down, pinned here for its
# reason and for one of their own: a zip entry stores a LOCAL DOS time, so a
# host clock in another zone would make the gate's `calendar.timegm` read of it
# disagree with a back end that converted the epoch in local time, and
# `repro.epoch` would fail a correct tree for the container's timezone. This
# machine is UTC, so the pin changes nothing that has ever been measured here,
# which is exactly why it was missing. They go on this wrapper and not into
# `build_dists`: that helper is the shared one every step's build rows go
# through, steps 01 to 04 are measured against it as it stands, and moving the
# environment there would mean re-taking their numbers.
#
# One retry, in the shape `venv_install` uses a few hundred lines down and for
# the reason written out above it. An isolated `python -m build` fetches the
# back end its table pins from the index, and a pin that is not in the image's
# warm `/opt/cache/pip` is a real fetch: one refused connection would take
# `repro.sdist`, `repro.epoch` and `pin.effective` down together -- 44 of this
# step's 137 units -- for a tree that is correct. A retry does not make the
# network reliable; it makes one blip cost time instead of score, and a tree
# that genuinely will not build fails twice and still fails, which is why the
# count is one and not a loop. The retry is here and not inside `build_dists`
# for the same reason the environment is. Both attempts are appended to the one
# log this distribution gets, so the second cannot overwrite the first's banner
# and the log says what happened both times.
repro_build() {
    local copy="$1" out="$2" label="$3" epoch="$4" sub rc=0 one log attempt brc
    for sub in $DIST_DIRS; do
        one=$(printf '%s' "$sub" | tr '/.' '__')
        log=$GRADE_LOG/repro-$label-$one.log
        : > "$log"
        for attempt in 1 2; do
            (
                export SOURCE_DATE_EPOCH="$epoch"
                export TZ=UTC LC_ALL=C.UTF-8 PYTHONHASHSEED=0
                build_dists "$copy/$sub" "$out"
            )
            brc=$?
            cat "$GRADE_LOG/build.$(basename "$out").log" >> "$log" 2>/dev/null
            rm -f "$GRADE_LOG/build.$(basename "$out").log"
            [ "$brc" -eq 0 ] && break
            printf '\n--- build attempt %s of %s at epoch %s failed (exit %s) ---\n' \
                "$attempt" "$sub" "$epoch" "$brc" >> "$log"
            [ "$attempt" = 1 ] && sleep 3
        done
        [ "$brc" -eq 0 ] || rc=$brc
        printf '  build %s of %s at epoch %s: %s\n' "$label" "$sub" "$epoch" \
            "$(ls -1 "$out" 2>/dev/null | tr '\n' ' ' | tail -c 200)"
    done
    return $rc
}

repro_build "$REPRO_ONE" "$REPRO_OUT1" one "$REPRO_EPOCH"
repro_one_rc=$?
repro_build "$REPRO_TWO" "$REPRO_OUT2" two "$REPRO_EPOCH"
repro_two_rc=$?
repro_build "$REPRO_THREE" "$REPRO_OUT3" three "$REPRO_EPOCH2"
repro_three_rc=$?
if [ "$repro_one_rc" -ne 0 ] || [ "$repro_two_rc" -ne 0 ] \
   || [ "$repro_three_rc" -ne 0 ]; then
    # Not a harness fault and not a zero: a tree that will not build is a tree
    # whose artifacts cannot be compared, and the rows below say so with the
    # build log's own last words. The inherited rows above have already been
    # taken and keep whatever they earned.
    printf 'at least one of the three builds failed (%s, %s, %s); the repro rows read what did come out\n' \
        "$repro_one_rc" "$repro_two_rc" "$repro_three_rc"
    for f in "$GRADE_LOG"/repro-*.log; do
        [ -e "$f" ] || continue
        printf '  %s: %s\n' "$(basename "$f")" "$(tail -c 200 "$f" | tr '\n' ' ')"
    done
fi

# The two same-epoch directories are the pair the byte-identity rows compare;
# every (directory, epoch) pair, including the first, goes to `repro.epoch`.
# The third is not compared with anything, because a build at another epoch is
# meant to differ from these two and comparing it would score the opposite of
# the property.
python /tests/step05_gates.py artifacts "$GRADE_APP" \
    "$REPRO_OUT1" "$REPRO_OUT2" "$REPRO_EPOCH" "$REPRO_OUT3" "$REPRO_EPOCH2" \
    > "$ROWS" 2> "$GRADE_LOG/gates05-artifacts.err"
rows_from_gates "$ROWS"

# --- step 06's read of what was built ---------------------------------------
#
# See the last two ordering notes in the header for the two choices made here:
# `twine check` sees every artifact this scorer produced, and the four
# row-bearing gates see step 04's three wheel directories and not step 05's
# reproducibility output.
#
# A tree that built nothing reaches here with an empty list. That is a candidate
# condition and not a harness one -- every build row above has already said so
# in its own words -- so it is recorded as a failed guard with that sentence in
# the log, rather than handed to `twine check` with no arguments, which exits 2
# on its own argument parser and would put a usage message in the detail.
TWINE_ALL=""
for artifact in "$GRADE/dist"/* "$ABI3DIR"/*.whl "$FTDIR"/*.whl \
                "$PUREDIR"/*.whl "$REPRO_OUT1"/*; do
    [ -f "$artifact" ] && TWINE_ALL="$TWINE_ALL $artifact"
done
if [ -n "$TWINE_ALL" ]; then
    run_unprivileged env PIP_CACHE_DIR=/opt/cache/pip \
        twine check $TWINE_ALL > "$GRADE_LOG/twine-all.log" 2>&1
    twine_all_rc=$?
else
    printf 'no artifact was built, so there is nothing for twine to check\n' \
        > "$GRADE_LOG/twine-all.log"
    twine_all_rc=1
fi
printf 'twine check over %s artifacts: exit %s\n' \
    "$(printf '%s' "$TWINE_ALL" | wc -w)" "$twine_all_rc"
sed -n '1,12p' "$GRADE_LOG/twine-all.log" | sed 's/^/  twine: /'

python /tests/step06_gates.py artifacts "$twine_all_rc" \
    "$GRADE_LOG/twine-all.log" "$ABI3DIR" "$FTDIR" "$PUREDIR" \
    > "$ROWS" 2> "$GRADE_LOG/gates06-artifacts.err"
rows_from_gates "$ROWS"

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
read -r v_abi3  n_abi3  <<< "$(group_fraction abi3.)"
read -r v_gil   n_gil   <<< "$(group_fraction gil.)"
read -r v_ft    n_ft    <<< "$(group_fraction ft.)"
read -r v_pure  n_pure  <<< "$(group_fraction pure.)"
read -r v_rwh   n_rwh   <<< "$(group_fraction repro.wheel)"
read -r v_rsd   n_rsd   <<< "$(group_fraction repro.sdist)"
read -r v_rep   n_rep   <<< "$(group_fraction repro.epoch)"
read -r v_pinr  n_pinr  <<< "$(group_fraction pin.requires)"
read -r v_pine  n_pine  <<< "$(group_fraction pin.effective)"
read -r v_dimp  n_dimp  <<< "$(group_fraction distutils.imports)"
read -r v_dmet  n_dmet  <<< "$(group_fraction distutils.meta-)"
read -r v_dtre  n_dtre  <<< "$(group_fraction distutils.trees)"
read -r v_dnam  n_dnam  <<< "$(group_fraction distutils.public)"
read -r v_setp  n_setp  <<< "$(group_fraction setuppy.)"
read -r v_lexp  n_lexp  <<< "$(group_fraction license.expression)"
read -r v_lfil  n_lfil  <<< "$(group_fraction license.files)"
read -r v_typd  n_typd  <<< "$(group_fraction typed.)"
read -r v_pylk  n_pylk  <<< "$(group_fraction pylock.)"
read -r v_reqt  n_reqt  <<< "$(group_fraction reqtxt.)"
read -r v_sbmp  n_sbmp  <<< "$(group_fraction sbom.present)"
read -r v_sbmv  n_sbmv  <<< "$(group_fraction sbom.validates)"
read -r v_twin  n_twin  <<< "$(group_fraction twine.)"
behaviour=$(fraction "$((corpus_pass + globals_pass))" "$((corpus_total + globals_total))")

part build                1 "$v_build"
part metadata             1 "$v_meta"
part contents             1 "$v_cont"
part migrated             1 "$v_migr"
part behaviour           49 "$behaviour"
part layout               1 "$v_lay"
part version              1 "$v_ver"
part hello                1 "$v_hel"
part lock                 1 "$v_lock"
part workspace            1 "$v_ws"
part groups               1 "$v_grp"
part unvendor             1 "$v_unv"
part abi3                 1 "$v_abi3"
part gil                  1 "$v_gil"
part ft                   1 "$v_ft"
part pure                 1 "$v_pure"
part repro-wheel          1 "$v_rwh"
part repro-sdist          1 "$v_rsd"
part repro-epoch          1 "$v_rep"
part pin-requires         1 "$v_pinr"
part pin-effective        1 "$v_pine"
part distutils-imports   24 "$v_dimp"
part distutils-meta       1 "$v_dmet"
part distutils-trees     12 "$v_dtre"
part distutils-names      6 "$v_dnam"
part setuppy              5 "$v_setp"
part license-expression  12 "$v_lexp"
part license-files        6 "$v_lfil"
part typed                9 "$v_typd"
part pylock               6 "$v_pylk"
part reqtxt               1 "$v_reqt"
part sbom-present         3 "$v_sbmp"
part sbom-validates       8 "$v_sbmv"
part twine                1 "$v_twin"
printf 'group sizes: build=%s metadata=%s contents=%s migrated=%s layout=%s version=%s hello=%s lock=%s workspace=%s groups=%s unvendor=%s abi3=%s gil=%s ft=%s pure=%s repro.wheel=%s repro.sdist=%s repro.epoch=%s pin.requires=%s pin.effective=%s\n' \
    "$n_build" "$n_meta" "$n_cont" "$n_migr" "$n_lay" "$n_ver" "$n_hel" \
    "$n_lock" "$n_ws" "$n_grp" "$n_unv" "$n_abi3" "$n_gil" "$n_ft" "$n_pure" \
    "$n_rwh" "$n_rsd" "$n_rep" "$n_pinr" "$n_pine"
printf 'this step: distutils.imports=%s distutils.meta=%s distutils.trees=%s distutils.public=%s setuppy=%s license.expression=%s license.files=%s typed=%s pylock=%s reqtxt=%s sbom.present=%s sbom.validates=%s twine=%s\n' \
    "$n_dimp" "$n_dmet" "$n_dtre" "$n_dnam" "$n_setp" "$n_lexp" "$n_lfil" "$n_typd" \
    "$n_pylk" "$n_reqt" "$n_sbmp" "$n_sbmv" "$n_twin"
printf 'observations: corpus %s/%s  globals %s/%s\n' \
    "$corpus_pass" "$corpus_total" "$globals_pass" "$globals_total"
printf 'suppression penalty: %s\n' "$PENALTY"

# A prefix that matches nothing scores 0 and is indistinguishable from a group
# that failed, so the sizes are asserted rather than printed and hoped for.
# The twenty inherited sizes are steps 01 to 05's own, unchanged; a drift there
# means this step is re-running a different earlier step than the one it
# inherited. This step's own thirteen are one row each: ten of them are
# all-or-nothing by design -- see the header on `distutils-imports` -- and the
# other three, `distutils.public`, `reqtxt` and `twine`, are guards.
for pair in "build $n_build 12" "metadata $n_meta 14" "contents $n_cont 14" \
            "migrated $n_migr 4" "layout $n_lay 5" "version $n_ver 6" \
            "hello $n_hel 12" "lock $n_lock 5" "workspace $n_ws 4" \
            "groups $n_grp 3" "unvendor $n_unv 4" "abi3 $n_abi3 3" \
            "gil $n_gil 2" "ft $n_ft 2" "pure $n_pure 2" \
            "repro.wheel $n_rwh 1" "repro.sdist $n_rsd 1" \
            "repro.epoch $n_rep 1" "pin.requires $n_pinr 1" \
            "pin.effective $n_pine 1" \
            "distutils.imports $n_dimp 1" "distutils.meta $n_dmet 1" \
            "distutils.trees $n_dtre 1" \
            "distutils.public $n_dnam 1" "setuppy $n_setp 1" \
            "license.expression $n_lexp 1" "license.files $n_lfil 1" \
            "typed $n_typd 1" "pylock $n_pylk 1" "reqtxt $n_reqt 1" \
            "sbom.present $n_sbmp 1" "sbom.validates $n_sbmv 1" \
            "twine $n_twin 1"; do
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
# T1, migrated-ness, is this step's own families: distutils-imports distutils-meta distutils-trees distutils-names setuppy license-expression license-files typed pylock reqtxt sbom-present sbom-validates twine.
# T2, preservation, is every other part above -- the recorded behaviour and the
# families an earlier step introduced -- at the weights `part` gave them. The
# weighted mean of all parts is printed for comparison and scores nothing.
read -r prod t1 t2 t1_missing <<< "$(product_score distutils-imports distutils-meta distutils-trees distutils-names setuppy license-expression license-files typed pylock reqtxt sbom-present sbom-validates twine)"
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
