#!/usr/bin/env bash
# Shared verifier machinery.  Per-step copy: the verifier image is built with
# this step's tests/ as the Docker build context, so it cannot COPY from the
# task root.  Every step carries an identical copy; keep them in sync.
set -u

# Unprivileged identity for candidate code.  nobody:nogroup already exists in
# debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

LIB=/tests/lib          # root-only: the recordings live here
SLIB=/opt/harness       # root-owned, world-readable: what nobody may execute
WORK=/tmp/verify        # nobody-owned scratch: build tree and candidate output
LOG=/logs/verifier

# The declared budget, not what the kernel reports.  `--cpus 7` is a quota, so
# nproc inside the container still answers with the whole host and a -j taken
# from it oversubscribes the quota it was meant to respect.  Keep this equal to
# `cpus` under [verifier.environment] in task.toml.
NCPU=7

# Wall-clock bounds.  Harbor discards a step whose verifier outlives
# [steps.verifier] timeout_sec (1800 s in every step) even when reward.json is
# already written, and then aborts every later step -- so a candidate whose
# build or Simulator never returns would lock in the mean of the steps before
# it.  The whole of test.sh therefore runs under VERIFY_BUDGET (see
# supervise_verifier), which leaves 300 s of the 1800 for the image build and
# the artifact round trip that harbor's timeout also covers; and every
# candidate invocation runs under UNPRIV_TIMEOUT (see run_unprivileged), so one
# hung phase costs that phase rather than the step.  The slowest golden run
# measured is step 10 at ~400-800 s end to end, its longest single phase the
# sanitized e2e replay at ~3 min.
VERIFY_BUDGET=1500
UNPRIV_TIMEOUT=900

# py_compile and any stray import drop __pycache__ beside the script that was
# imported.  Here that means inside /tests, which is the verifier image's build
# context on disk.  Off everywhere, root included.
export PYTHONDONTWRITEBYTECODE=1

# Run from a directory the candidate cannot have written to.  Harbor execs this
# verifier with the working directory set to /app -- the candidate's own tree --
# and `python3 -c` and `python3 -` put that directory first on sys.path, so a
# `/app/json.py` the build dropped (give_to_unprivileged hands /app to nobody)
# would be imported as root by the first `import json` below and could rewrite
# reward.json after the score is written.  / is root's, and nothing here uses a
# path relative to the working directory (the scanners and replays take
# absolute arguments; the Simulator sets its own cwd).
cd / || exit 1

# ---------------------------------------------------------------- scaffolding

# Lock the reward directory to root.  Call this first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs.  Writing the real score last does not help: a
# detached child outlives the scorer and writes after it.
harden_reward_dir() {
    mkdir -p "$LOG"
    chown root:root "$LOG" 2>/dev/null || true
    chmod 700 "$LOG"
    # The first call re-runs the calling test.sh under the deadline and never
    # returns; the re-run lands here again and carries on as before.
    [ -n "${OSSL_VERIFY_INNER:-}" ] || supervise_verifier "${BASH_SOURCE[1]}"
    trap reward_backstop EXIT
}

# supervise_verifier <test.sh>
#
# Run the step's test.sh again, as a child, under `timeout VERIFY_BUDGET`, and
# be the process that is still there when it is over.  On expiry timeout sends
# TERM to the child's whole process group (the child's EXIT trap writes 0 if
# nothing was written yet) and KILL 20 s later; candidate processes that left
# the group, and per-call timeouts that made groups of their own, are uid
# 65534 and go in the reap below.  Whatever happened, a reward.json exists and
# the directory is reopened before this exits 0: the worst case is
# VERIFY_BUDGET + 20 s + one reap, under 1560 s.
supervise_verifier() {
    local script=$1 rc
    OSSL_VERIFY_INNER=1 timeout -k 20 "$VERIFY_BUDGET" bash "$script"
    rc=$?
    reap_unprivileged
    if [ ! -f "$LOG/reward.json" ]; then
        printf '{"reward":0.000000}\n' > "$LOG/reward.json"
        vlog "VERIFIER DEADLINE: test.sh ended with status $rc and wrote no" \
             "reward within ${VERIFY_BUDGET}s -- reward.json written as 0"
    elif [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        vlog "VERIFIER DEADLINE: test.sh was stopped at ${VERIFY_BUDGET}s;" \
             "keeping the reward.json it had already written"
    fi
    chmod 755 "$LOG" 2>/dev/null || true
    chmod 644 "$LOG/reward.json" 2>/dev/null || true
    exit 0
}

# reap_unprivileged -- kill every process uid 65534 has in this container.
#
# kill(-1) sent *as* nobody reaches exactly the processes nobody may signal --
# in this PID namespace, every candidate process and nothing of root's -- in
# one call, so a candidate that keeps forking is swept in the same pass that
# finds it.  Looped until pgrep (which reads the real ids out of
# /proc/<pid>/status, so a non-dumpable process is still seen) finds none.
reap_unprivileged() {
    local i
    for i in 1 2 3 4 5 6 7 8 9 10; do
        pgrep -u "$SCORE_UID" > /dev/null 2>&1 || return 0
        setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
            kill -9 -1 > /dev/null 2>&1
        sleep 0.2
    done
    pgrep -u "$SCORE_UID" > /dev/null 2>&1 || return 0
    vlog "reap: uid $SCORE_UID still has processes after 10 rounds"
    return 0
}

# A step *always* writes reward.json and exits 0.  `report` does
# both on every path the verifier knows it is on; this covers the paths it does
# not -- an unbound variable under `set -u`, a helper that dies, a cmake that
# takes the shell down with it.  Without it such a run leaves no reward.json at
# all, and a step with no reward.json is a step that vanishes from the mean
# rather than a step that scored zero.
#
# It never overwrites a real score: `report` writes the file and then exits,
# which is what runs this, and the file existing is the signal to stand down.
# Bash resets EXIT traps in subshells, so a `( ... )` or a $( ... ) that exits
# non-zero does not trip it either.
reward_backstop() {
    local st=$?
    [ -f "$LOG/reward.json" ] && exit 0
    printf '{"reward":0.000000}\n' > "$LOG/reward.json"
    chmod 755 "$LOG" 2>/dev/null || true
    chmod 644 "$LOG/reward.json" 2>/dev/null || true
    printf 'VERIFIER ABORTED at line %s with status %s -- reward.json written\n' \
        "${BASH_LINENO[0]:-?}" "$st" | tee -a "$LOG/verify.log" >&2
    exit 0
}

# Run candidate-authored code as nobody instead of root.  Needed for
# harden_reward_dir to mean anything: root can write a root-owned directory
# whatever its mode.  Use it everywhere candidate code runs -- a Makefile, a
# linked library's static initializer, a simulator on a socket -- not just once.
# PYTHONDONTWRITEBYTECODE because $SLIB is root-owned: without it every
# unprivileged python3 would try to drop __pycache__ beside a script it cannot
# write.  Harmless, but it turns a clean run into one with errors in it.
#
# PYTHONNOUSERSITE because the candidate's own build runs first as this same
# uid: a usercustomize.py or .pth it left in a user site directory would
# otherwise be imported by every scanner and replay below at interpreter
# start, before their own code, and could print any verdict.  The
# reap afterwards is the same argument for processes: nothing a call started
# outlives it, so nothing the build left running can rewrite a scanner's
# output in $WORK between the scanner writing it and root reading it.  The
# timeout is the wall-clock half (see UNPRIV_TIMEOUT); it kills the call's
# whole process group, and the reap gets whatever left the group.
#
# HOME is a fresh, empty directory per call that root creates and removes,
# so nothing one call's candidate code leaves in a home
# directory -- a ~/.cmake/packages registry entry, a ~/.config file a tool
# reads, a dotfile -- reaches a later call whose output is scored.  TMPDIR
# stays /tmp; it is scratch, not configuration.
run_unprivileged() {
    local rc home
    home=$(mktemp -d /tmp/.verifier-home.XXXXXXXX 2>/dev/null) || home=/nonexistent
    [ "$home" = /nonexistent ] || chown "$SCORE_UID:$SCORE_GID" "$home"
    timeout -k 10 "$UNPRIV_TIMEOUT" \
        setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME="$home" TMPDIR=/tmp PATH=/usr/local/bin:/usr/bin:/bin \
            PYTHONDONTWRITEBYTECODE=1 PYTHONNOUSERSITE=1 "$@"
    rc=$?
    [ "$rc" = 124 ] || [ "$rc" = 137 ] \
        && vlog "timeout: '$1 ...' was stopped at ${UNPRIV_TIMEOUT}s"
    reap_unprivileged
    [ "$home" = /nonexistent ] || rm -rf "$home" 2>/dev/null
    return "$rc"
}

give_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" "$@" 2>/dev/null || true
}

# Copy the parts of the harness that the unprivileged side has to read into a
# root-owned, world-readable directory, and leave the rest of /tests unreadable.
#
# Two separate reasons, both of which have to hold at once:
#
#   readable  -- the replay scripts and the driver source are executed as
#                nobody, and /tests is mode 0700 root.  The image makes it so
#                deliberately: expected.txt and expected-e2e.txt are the
#                answers, and candidate code runs in this container.
#   unwritable -- staging into $WORK would not do, because $WORK is handed to
#                nobody for the build, and a Makefile that rewrote runner.py
#                between the build and the replay would be scoring itself.
#                /opt/harness is root-owned 0755: readable by all, writable by
#                root alone.
#
# The two scanners are staged for the same reason the replays are: they read
# candidate-authored input -- an ELF the build emitted, a compile database the
# build wrote -- so they run unprivileged, and an unprivileged process cannot
# read /tests.  score.py is deliberately NOT staged: it has to read the
# recordings, so it runs as root straight out of /tests/lib, where nothing the
# candidate can reach has been able to touch it.
#
# Idempotent and self-calling from every function that needs it, so that ten
# hand-written test.sh files cannot each forget it in their own way.
stage_lib() {
    [ -x "$SLIB/.staged" ] && return 0
    rm -rf "$SLIB"
    install -d -m 0755 -o root -g root "$SLIB"
    local f
    for f in cpridrv.c cases.txt keys.json tpm2.py; do
        install -m 0644 -o root -g root "$LIB/$f" "$SLIB/$f"
    done
    for f in runner.py e2e.py astscan.py elfscan.py vendorscan.py; do
        install -m 0755 -o root -g root "$LIB/$f" "$SLIB/$f"
    done
    install -d -m 0755 -o root -g root "$WORK"
    give_to_unprivileged "$WORK"
    : > "$SLIB/.staged"
    chmod 0755 "$SLIB/.staged"
}

vlog() { printf '%s\n' "$*" | tee -a "$LOG/verify.log" >&2; }

# --------------------------------------------------------- the detail record
#
# `check_oracle_nop.py` looks for the second, dangerous face of the empty-/app
# transient -- step k compiling step k-1's tree and returning a plausible
# partial score rather than an obvious zero -- by the shape it leaves behind:
# the checks the step INTRODUCED collapse while every check it INHERITED from
# an earlier step still passes.  It reads that shape out of
# `<STEP_ID>-detail.json`: one `results<NN>` array per step number NN, each
# entry `{"name": ..., "ok": ...}`.
#
# Ownership is by the step that first demanded the property.  Behaviour belongs
# to step 01 -- the corpus is fixed and every later step must still reproduce
# it -- so families stay recorded under 01 forever, and a step's own gates
# carry its own number.  Step 01 therefore writes `results01` and nothing else,
# and the detector is inert on it by construction: at k = 1 there is nothing to
# inherit.  It has teeth from step 02 on.
#
# `CHECK_OWNER` is the number a check is filed under.  It defaults to the
# running step, so a step that says nothing files everything as its own; a step
# that re-checks an earlier step's property sets it around that block.
CHECK_OWNER=${STEP_ID#step}
DETAIL=""

# detail_add <NN> <name> <0|1>
detail_add() {
    DETAIL="${DETAIL}${1}	${2}	${3}
"
}

# detail_families <NN> <report.json>...
#
# One entry per family, ok only if every case in it matched.  A family with no
# cases in it is not ok: "nothing to be wrong about" is not a pass.
detail_families() {
    local owner=$1; shift
    local name ok
    while IFS="$(printf '\t')" read -r name ok; do
        [ -n "$name" ] && detail_add "$owner" "family:$name" "$ok"
    done < <(python3 - "$@" <<'PYDF'
import json
import sys

for path in sys.argv[1:]:
    try:
        fams = json.load(open(path))["families"]
    except Exception:
        continue
    for name, v in sorted(fams.items()):
        ok = 1 if v["total"] > 0 and v["matched"] == v["total"] else 0
        print("%s\t%d" % (name, ok))
PYDF
    )
}

# Written by report(), so every exit path leaves one -- a precondition that
# scores 0 still says which precondition it was.
detail_write() {
    printf '%s' "$DETAIL" | python3 -c '
import json
import sys

buckets = {}
for line in sys.stdin:
    line = line.rstrip("\n")
    if not line:
        continue
    num, name, ok = line.split("\t", 2)
    buckets.setdefault("results" + num, []).append({"name": name,
                                                    "ok": ok == "1"})
with open(sys.argv[1], "w") as fh:
    json.dump(buckets, fh, indent=1, sort_keys=True)
' "$LOG/${STEP_ID}-detail.json" 2>/dev/null || true
    chmod 644 "$LOG/${STEP_ID}-detail.json" 2>/dev/null || true
}

# report <score>
#
# Harbor's multi_step_reward_strategy = "mean" averages the `reward` key across
# every step that produced a verifier_result.  Always exits 0: a non-zero exit
# is a verifier crash, and Harbor then skips remaining steps even without
# min_reward.
report() {
    local reward
    # Nothing of the candidate's is still running when the score is written.
    reap_unprivileged
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
    detail_write
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > "$LOG/reward.json"
    vlog "REWARD $reward"
    # Reopen to 0755 so a local docker run, which reads this from the host as
    # an ordinary user, can collect it.  Still root-owned, so candidate code
    # still cannot write it.
    chmod 755 "$LOG" 2>/dev/null || true
    chmod 644 "$LOG/reward.json" 2>/dev/null || true
    exit 0
}

# ------------------------------------------------------------------ preflight

# Name the image that is running, in a filename.
#
# A multi-step local run does not always build one verifier image per step, and
# when it does not, a later step silently runs an earlier step's test.sh and
# reports its score.  STEP_ID comes from the Dockerfile, not from the mounted
# tree, so this stamp names the image that actually ran and
# `ls .../verifier | grep -oE 'step[0-9]+'` tells the two cases apart.
stamp_step() {
    : > "$LOG/${STEP_ID}.stamp"
    vlog "=== ${STEP_ID} verifier, OpenSSL ${OSSL_VERSION} ==="
}

# Refuse to score an /app that did not arrive.
#
# An artifact round trip that fails leaves the verifier with an empty /app, and
# every check then reports "not migrated" -- a plausible-looking zero that is
# really a harness failure.  Distinguish them here, loudly, before any check
# runs.
assert_app() {
    local missing=""
    for p in cmake/CMakeLists.txt src/OsslCryptoEngine src/tpm src/platform \
             src/simulator; do
        [ -e "/app/$p" ] || missing="$missing /app/$p"
    done
    if [ -n "$missing" ]; then
        vlog "FATAL: /app did not arrive intact; missing:$missing"
        vlog "FATAL: this is an environment failure, not a candidate failure"
        detail_add "$CHECK_OWNER" "precondition:app-arrived" 0
        report 0
    fi
    detail_add "$CHECK_OWNER" "precondition:app-arrived" 1
    vlog "/app: $(find /app -name '*.c' | wc -l) C files, $(find /app -name '*.h' | wc -l) headers"
}

# The image's OpenSSL is the one this step grades against.  Measured at run
# time from the library the loader actually binds, not from a label.
assert_ossl() {
    local got
    got=$("$LIB/osslprobe.sh" 2>/dev/null) || got="(probe failed)"
    if [ "$got" != "$OSSL_VERSION" ]; then
        vlog "FATAL: verifier image binds OpenSSL '$got', expected '$OSSL_VERSION'"
        detail_add "$CHECK_OWNER" "precondition:openssl-version" 0
        report 0
    fi
    detail_add "$CHECK_OWNER" "precondition:openssl-version" 1
    vlog "OpenSSL: $got (runtime-resolved)"
}

# ---------------------------------------------------------------------- build

# Build the candidate's tree exactly the way the instruction says it is built.
# Returns 0 on success.  Everything runs unprivileged: a Makefile is candidate
# code.
#
# The two commands below are the two the instruction names, with nothing added
# to either of them.  A flag the ticket does not carry either changes nothing,
# in which case it does not belong on the command that decides whether the tree
# builds, or it changes something, in which case it is a requirement the
# candidate was never given.
#
# The compilation database the AST gates need is obtained afterwards, by a
# second configure of the same directory, and is not part of this build.  It is
# kept only if both generates agree on what the compiler would be told, because
# a description of a build is only evidence about that build.
#
# The build directory is deliberately outside /app.  A candidate that commits
# build output cannot then have it mistaken for a fresh build, and the tree the
# gates scan stays the tree the candidate wrote.
build_app() {
    stage_lib
    rm -rf "$WORK/build"
    mkdir -p "$WORK/build"
    give_to_unprivileged /app "$WORK"
    if ! run_unprivileged cmake -S /app/cmake -B "$WORK/build" \
            -DSPEC_VERSION=116 \
            > "$LOG/cmake.log" 2>&1; then
        vlog "build: cmake configure failed (see cmake.log)"
        tail -25 "$LOG/cmake.log" >&2
        return 1
    fi
    # The rules this build is about to run, in root's custody before any of
    # the candidate's build-time code has (see cdb_hold).
    local held=1
    cdb_hold build "$WORK/build" || held=0
    # A database this configure wrote (a tree may switch the export on itself)
    # is not the one the readers get: theirs is the re-configure's, and only
    # once it has passed every check below.  Withdrawn here, so that every path
    # that does not end in that -- a build that fails, a re-configure that
    # fails or writes none -- leaves no database to read.
    rm -f "$CUSTODY/build/compile_commands.json"
    if ! run_unprivileged cmake --build "$WORK/build" \
            > "$LOG/build.log" 2>&1; then
        vlog "build: compile failed (see build.log)"
        tail -40 "$LOG/build.log" >&2
        return 1
    fi
    local want
    for want in platform/libplatform.so OsslCryptoEngine/libCryptoEngine.so \
                tpm/libTPM.so Simulator; do
        if [ ! -e "$WORK/build/$want" ]; then
            vlog "build: $want was not produced"
            return 1
        fi
    done
    vlog "build: ok"

    # What the generate that was just BUILT told the compiler, held by root
    # before anything ran (cdb_hold above).  CMake's Makefile generator writes
    # each target's flags into CMakeFiles/<target>.dir/flags.make and its
    # compile rules into build.make at generate time, and the build reads them
    # from there.  They are the record of the compile lines of the objects
    # that were just judged -- provided the build did not rewrite them while
    # it ran, which is the first comparison.  The copy is root's, not in this
    # nobody-owned directory, where the re-configure below could rewrite it to
    # match itself.
    local drift=""
    if [ "$held" != 1 ]; then
        drift="the generated rules could not be taken into custody"
    else
        drift=$(cdb_drift build "$WORK/build")
        [ -z "$drift" ] || drift="the build rewrote its own rules ($drift)"
    fi

    # A second, separate configure of the same directory, run only after the
    # build the instruction names has already succeeded and been judged.  The
    # AST gates read what the build compiled out of a compilation database, and
    # CMAKE_EXPORT_COMPILE_COMMANDS is CMake's own way of asking a project for
    # one -- but it is not a flag the ticket names, so it cannot sit on the
    # command that decides whether the tree builds.  What the instruction does
    # state is that the tree stays an ordinary CMake project, and re-configuring
    # it with a stock CMake option is exactly that property being exercised.
    #
    # And then the two generates are compared, because a re-configure is only a
    # description of the build it followed if the tree did not change its mind
    # in between.  CMAKE_EXPORT_COMPILE_COMMANDS is an ordinary CMake variable
    # that CMakeLists.txt can read, and add_compile_options() is a directory
    # property re-evaluated at every generate, so
    #
    #     if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    #       add_compile_options(-DSOMETHING)
    #     endif()
    #
    # builds one set of preprocessor flags and describes another.  The AST scan
    # parses each translation unit with the flags out of this database; a tree
    # that can choose those flags by whether it is being described can show the
    # scan a source the build never compiled.  Byte-identical flags.make AND
    # build.make on both sides -- build.make is where a per-source option and
    # the list of sources live -- is the statement that it did not, and it is
    # the statement a correct tree makes for free: the option changes what
    # CMake writes out, not what it tells the compiler.  (link.txt is not
    # compared here: once the libraries exist the re-configure links them by
    # path rather than by -l, measured on the golden.)  Then the database is
    # taken into custody the moment the re-configure returns, and checked
    # against the held rules entry by entry (cdb_check): a translation unit the
    # rules compile and the database leaves out is one no reader would see.
    #
    # A failure here is not a build failure and must not be scored as one.  The
    # database simply does not appear, scan_ast says so, and the gates that
    # depend on it take their own "the AST scan is not trustworthy on this tree"
    # path -- the honest verdict about a tree whose AST could not be read,
    # rather than a zero for a tree that built.
    if ! run_unprivileged cmake -S /app/cmake -B "$WORK/build" \
            -DSPEC_VERSION=116 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            > "$LOG/cmake-compdb.log" 2>&1; then
        vlog "build: re-configuring for a compile database failed; the AST" \
             "gates will score 0 (see cmake-compdb.log)"
    elif [ ! -f "$WORK/build/compile_commands.json" ]; then
        vlog "build: the re-configure wrote no compile_commands.json; the AST" \
             "gates will score 0"
    else
        if [ -z "$drift" ]; then
            drift=$(cdb_drift build "$WORK/build" nolink)
            [ -z "$drift" ] || drift="$drift differs from the generate that was built"
        fi
        if [ -z "$drift" ]; then
            cdb_take build "$WORK/build" \
                || drift="the compile database could not be taken into custody"
        fi
        if [ -z "$drift" ] && ! cdb_check build "$WORK/build"; then
            drift="the compile database does not match the rules that were built ($CDB_FINDINGS)"
        fi
        if [ -n "$drift" ]; then
            # Not a build failure and not a zero for the build: the database is
            # withdrawn, and the AST gates take the same path they take when
            # there is none.  Said out loud, because a tree that does this has
            # done something no correct tree does.
            rm -f "$CUSTODY/build/compile_commands.json"
            vlog "build: $drift, so the compile database does not describe the" \
                 "objects that were judged and is not used; the AST gates will" \
                 "score 0"
        else
            vlog "build: compile database written by a separate re-configure;" \
                 "both generates agree on every flags.make and build.make, and" \
                 "its $CDB_ENTRIES entries are the $CDB_RULES compile rules"
        fi
    fi
    return 0
}

# ------------------------------------------------- compile-database custody
#
# Every compile database this verifier reads is written by CMake running the
# tree's CMakeLists.txt as nobody, into a directory nobody owns -- and was read
# back from there after more of the tree's code had run as nobody: its build,
# a later build, the engine, the simulator.  Anything in between could rewrite
# it: drop an entry, so a translation unit is never replayed or parsed, or
# rewrite one, so a reader sees a clean line the objects were not built from.
# And CMake itself leaves a target out of the database when the target asks it
# to (the EXPORT_COMPILE_COMMANDS property), while still building it.
#
# So the database and the generated rules are taken into root's custody the
# moment the generate that wrote them returns -- before any build-time code of
# the tree's has run -- and every reader reads root's copy:
#
#   cdb_hold <name> <dir>    root's copy of every CMakeFiles/**/flags.make,
#                            build.make and link.txt, plus compile_commands.json
#                            if the generate wrote one
#   cdb_take <name> <dir>    root's copy of the database alone, for a database
#                            written by a later generate of rules already held
#   cdb_drift <name> <dir> [nolink]
#                            the first rule file that is not now what root
#                            holds, or nothing
#   cdb_check <name> <dir>   the database against the held rules, both ways:
#                            every compile rule has its entry and every entry
#                            its rule, with the same command once the rule's
#                            flag variables are expanded, and no target or
#                            link line takes an object no held rule compiles
#                            or an archive no held rule makes (outside /usr)
#
# Root-owned 0755 under /opt: the unprivileged readers can read it, and nothing
# of the tree's can write it.
CUSTODY=/opt/ossl-custody
CDB_FINDINGS=""
CDB_RULES=0
CDB_ENTRIES=0

cdb_rule_files() {      # <dir> [nolink] -> NUL-separated relative paths
    if [ "${2:-}" = nolink ]; then
        (cd "$1" 2>/dev/null && find . -path '*/CMakeFiles/*' -type f \
            \( -name flags.make -o -name build.make \) -print0 | LC_ALL=C sort -z)
    else
        (cd "$1" 2>/dev/null && find . -path '*/CMakeFiles/*' -type f \
            \( -name flags.make -o -name build.make -o -name link.txt \
               -o -name 'objects[0-9]' -o -name 'objects[0-9][0-9]' \
               -o -name 'objects[0-9].rsp' -o -name 'linkLibs.rsp' \) \
            -print0 | LC_ALL=C sort -z)
    fi
}

cdb_hold() {
    local name=$1 dir=$2 dst=$CUSTODY/$1 rel
    install -d -m 0755 -o root -g root "$CUSTODY" || return 1
    rm -rf "$dst"
    install -d -m 0755 -o root -g root "$dst" "$dst/rules" || return 1
    while IFS= read -r -d '' rel; do
        install -D -m 0644 -o root -g root "$dir/$rel" "$dst/rules/$rel" \
            || return 1
    done < <(cdb_rule_files "$dir")
    if [ -e "$dir/compile_commands.json" ]; then
        cdb_take "$name" "$dir" || return 1
    fi
    return 0
}

cdb_take() {
    local src=$2/compile_commands.json
    [ -f "$src" ] && [ ! -L "$src" ] || return 1
    install -m 0644 -o root -g root "$src" "$CUSTODY/$1/compile_commands.json"
}

cdb_drift() {
    local held=$CUSTODY/$1/rules dir=$2 rel
    if ! cmp -s <(cdb_rule_files "$held" "${3:-}") \
                <(cdb_rule_files "$dir" "${3:-}"); then
        echo "the set of generated rule files"
        return 0
    fi
    while IFS= read -r -d '' rel; do
        if ! cmp -s "$held/$rel" "$dir/$rel"; then
            printf '%s\n' "${rel#./}"
            return 0
        fi
    done < <(cdb_rule_files "$held" "${3:-}")
    return 0
}

# cdb_check <name> <dir> -> 0 when the held database and rules agree; sets
# CDB_RULES, CDB_ENTRIES and CDB_FINDINGS (up to six, space-separated) either
# way.  Run unprivileged: the files are the tree's generate's, even in root's
# copy.  A database that cannot be read, or rules with no compile rule in them,
# is a check that was not made, and fails like a finding.
cdb_check() {
    local out
    CDB_FINDINGS="" CDB_RULES=0 CDB_ENTRIES=0
    out=$(run_unprivileged python3 - "$CUSTODY/$1" "$2" <<'PYCDB' 2>&1
import json, os, re, shlex, sys

root = sys.argv[1]
# make runs every rule from the top of the build directory, and CMake writes no
# `cd' in front of a rule for a target declared there.
top = os.path.normpath(sys.argv[2])
VAR = re.compile(r"^\$\((\w+)_(DEFINES|INCLUDES|FLAGS)\)$")
FLAGVAR = re.compile(r"^(\w+_(?:DEFINES|INCLUDES|FLAGS)) = ?(.*)$")


def split(s):
    return shlex.split(s.replace("$$", "$"))


def system(directory, path):
    p = os.path.normpath(os.path.join(directory, path))
    head = "/" + p.split("/")[1] if p.startswith("/") and p.count("/") > 1 else ""
    if head in ("/lib", "/lib64", "/bin", "/sbin") and os.path.islink(head) \
            and os.path.realpath(head).startswith("/usr/"):
        p = os.path.realpath(head) + p[len(head):]
    return p.startswith("/usr/")


ARTOOL = re.compile(r"(?:.*-)?ar(?:-[0-9.]+)?")
RANLIB = re.compile(r"(?:.*-)?ranlib(?:-[0-9.]+)?")


def rel(key):
    for top in ("/tmp/verify/", "/app/"):
        if key.startswith(top):
            return key[len(top):]
    return key


findings, rules, external, links = [], {}, [], []
for dp, dn, fn in os.walk(os.path.join(root, "rules")):
    dn.sort()
    if "build.make" not in fn:
        continue
    flags = {}
    if "flags.make" in fn:
        with open(os.path.join(dp, "flags.make"), errors="replace") as fh:
            for line in fh:
                m = FLAGVAR.match(line.rstrip("\n"))
                if m:
                    flags[m.group(1)] = m.group(2)
    with open(os.path.join(dp, "build.make"), errors="replace") as fh:
        text = fh.read()
    # Objects a target links that its own rules do not compile.  A CMake
    # OBJECT library's objects are listed here in every target that uses them,
    # and each is compiled by a rule of the OBJECT library's own, so this is
    # only collected here and decided below, once every rule is known.
    for m in re.finditer(r"^(\S+)_EXTERNAL_OBJECTS =((?:.*\\\n)*.*)$", text, re.M):
        try:
            objs = split(m.group(2).replace("\\\n", " "))
        except ValueError:
            objs = [m.group(2)]
        external += [(m.group(1), o) for o in objs]
    for line in text.splitlines():
        if "cmake_link_script" in line and os.path.isfile(
                os.path.join(dp, "link.txt")):
            try:
                argv = shlex.split(line.strip().lstrip("@"))
            except ValueError:
                argv = line.split()
            cwd = top
            while len(argv) > 2 and argv[0] == "cd" and argv[2] == "&&":
                cwd, argv = argv[1], argv[3:]
            links.append((os.path.join(dp, "link.txt"), os.path.normpath(cwd)))
            continue
        if not line.startswith("\t") or not re.search(r"\$\(\w+_FLAGS\)", line):
            continue
        try:
            argv = shlex.split(line.strip().lstrip("@"))
        except ValueError:
            argv = line.split()
        cwd = top
        while len(argv) > 2 and argv[0] == "cd" and argv[2] == "&&":
            cwd, argv = argv[1], argv[3:]
        if "-c" not in argv or "-o" not in argv:
            continue            # the .i and .s convenience rules
        out, i = [], 0
        while i < len(argv):
            a = argv[i]
            v = VAR.match(a)
            if v:
                out += split(flags.get("%s_%s" % (v.group(1), v.group(2)), ""))
            elif a in ("-MT", "-MF", "-MQ"):
                i += 2
                continue
            elif a not in ("-MD", "-MMD"):
                out.append(a.replace("$$", "$"))
            i += 1
        if "-o" not in out or out.index("-o") + 1 >= len(out):
            findings.append("unreadable-rule:%s" % rel(dp))
            continue
        key = os.path.normpath(os.path.join(cwd, out[out.index("-o") + 1]))
        rules[key] = (os.path.normpath(cwd), out)

# An external object is an object no rule compiled -- its source is in no
# database at all -- unless a held rule did compile it.
for target, obj in external:
    if os.path.normpath(os.path.join(top, obj)) not in rules:
        findings.append("external:%s:%s" % (target, rel(obj)))

# The same question asked of what the link lines actually hand the linker
# (the part a held rule can answer): every object a link.txt names is a
# held rule's output, and every archive is one a held link.txt creates or one
# from the installed system.  A response file cannot be read here, so it is a
# finding.
made, lines = set(), []
for path, cwd in links:
    with open(path, errors="replace") as fh:
        for line in fh:
            try:
                argv = shlex.split(line)
            except ValueError:
                argv = line.split()
            if not argv:
                continue
            tool = os.path.basename(argv[0])
            if RANLIB.fullmatch(tool) and system(cwd, argv[0]):
                continue
            if ARTOOL.fullmatch(tool) and system(cwd, argv[0]):
                outs = [a for a in argv[1:] if a.endswith(".a")]
                if outs:
                    made.add(os.path.normpath(os.path.join(cwd, outs[0])))
                    argv = [a for a in argv if a != outs[0]]
            lines.append((path, cwd, argv[1:]))
def expand(cwd, args):
    out = []
    for a in args:
        if a.startswith("@"):
            held = os.path.normpath(os.path.join(root, "rules", os.path.relpath(
                os.path.normpath(os.path.join(cwd, a[1:])), top)))
            if not held.startswith(os.path.join(root, "rules") + "/") or not os.path.isfile(held):
                out.append(a)
                continue
            with open(held, errors="replace") as fh:
                try:
                    out += shlex.split(fh.read())
                except ValueError:
                    out.append(a)
            continue
        out.append(a)
    return out


lines = [(p, c, expand(c, a)) for p, c, a in lines]
for path, cwd, args in lines:
    where = os.path.relpath(os.path.dirname(path), os.path.join(root, "rules"))
    for k, a in enumerate(args):
        if k > 0 and args[k - 1] == "-o":
            continue            # what the line makes, not what it reads
        if a.startswith("@"):
            findings.append("link-unread:%s:%s" % (where, a))
        elif a.startswith("-"):
            continue
        elif a.endswith((".o", ".obj")):
            if os.path.normpath(os.path.join(cwd, a)) not in rules:
                findings.append("link-object:%s:%s" % (where, a))
        elif a.endswith(".a"):
            p = os.path.normpath(os.path.join(cwd, a))
            if p not in made and not system(cwd, a):
                findings.append("link-archive:%s:%s" % (where, a))

entries = {}
try:
    with open(os.path.join(root, "compile_commands.json")) as fh:
        db = json.load(fh)
    for e in db:
        d = os.path.normpath(e.get("directory", "/"))
        argv = e.get("arguments") or shlex.split(e.get("command", ""))
        if "-o" not in argv or argv.index("-o") + 1 >= len(argv):
            findings.append("no-object:%s" % rel(e.get("file", "?")))
            continue
        key = os.path.normpath(os.path.join(d, argv[argv.index("-o") + 1]))
        if key in entries:
            findings.append("twice:%s" % rel(key))
        entries[key] = (d, argv)
except Exception as exc:  # noqa: BLE001 -- unreadable is not checked
    print("CDB 0 0 1")
    print("unreadable:%s" % exc)
    sys.exit(0)

for key in sorted(set(rules) - set(entries)):
    findings.append("unlisted:%s" % rel(key))
for key in sorted(set(entries) - set(rules)):
    findings.append("unbuilt:%s" % rel(key))
for key in sorted(set(rules) & set(entries)):
    (rd, ra), (ed, ea) = rules[key], entries[key]
    # The rule may run the compiler through a launcher the database does not
    # show (CMake 3.25 writes none into it); one from the installed system is
    # the system's own and is not this reading's business.
    lead = len(ra) - len(ea)
    same = rd == ed and lead >= 0 and ra[lead:] == ea \
        and all(system(rd, a) for a in ra[:lead])
    if not same:
        findings.append("differs:%s" % rel(key))
if not rules:
    findings.append("no-compile-rule")
print("CDB %d %d %d" % (len(rules), len(entries), len(findings)))
for f in findings[:6]:
    print(f)
PYCDB
)
    CDB_RULES=$(printf '%s\n' "$out" | sed -n 's/^CDB \([0-9]*\) [0-9]* [0-9]*$/\1/p' | head -1)
    CDB_ENTRIES=$(printf '%s\n' "$out" | sed -n 's/^CDB [0-9]* \([0-9]*\) [0-9]*$/\1/p' | head -1)
    local n
    n=$(printf '%s\n' "$out" | sed -n 's/^CDB [0-9]* [0-9]* \([0-9]*\)$/\1/p' | head -1)
    CDB_FINDINGS=$(printf '%s\n' "$out" | sed -n '/^CDB /,$p' | sed 1d | head -6 \
                   | tr '\n' ' ' | sed 's/ *$//')
    CDB_RULES=${CDB_RULES:-0}
    CDB_ENTRIES=${CDB_ENTRIES:-0}
    if [ -z "$n" ]; then
        CDB_FINDINGS="the database check did not run: $(printf '%s' "$out" | tail -1)"
        return 1
    fi
    [ "$n" = 0 ]
}

# Compile the engine driver against the tree that was just built.
#
# The driver is the interface half of the behaviour suite: it includes
# CryptoEngine.h and links -lCryptoEngine -lplatform, and nothing else, so it
# cannot see a private symbol even by accident.  It lives in the verifier image
# and never in /app -- the candidate does not get to read the harness.
#
# Compiled as root into root-owned $SLIB, unlike everything else here.  gcc only
# reads the candidate's headers, it does not run them, so there is nothing to
# contain; and a driver binary sitting in nobody-writable scratch could be
# replaced between the build and the replay by anything the build left running.
build_driver() {
    stage_lib
    local ce="$WORK/build/OsslCryptoEngine/libCryptoEngine.so"
    local pf="$WORK/build/platform/libplatform.so"
    # Bounded like a candidate call: the headers it reads are the candidate's,
    # and an #include of something endless never finishes compiling.
    if ! timeout -k 10 300 gcc -std=gnu99 -O1 -g -Wall -o "$SLIB/cpridrv" \
            "$SLIB/cpridrv.c" \
            -I/app/src/include \
            -I/app/src/tpm/include \
            -I/app/src/tpm/include/prototypes \
            -I/app/src/platform/include \
            -I/app/src/platform/include/prototypes \
            "$ce" "$pf" \
            -Wl,-rpath,"$(dirname "$ce")" -Wl,-rpath,"$(dirname "$pf")" \
            > "$LOG/driver.log" 2>&1; then
        vlog "driver: did not compile against the candidate's headers"
        tail -30 "$LOG/driver.log" >&2
        return 1
    fi
    chmod 0755 "$SLIB/cpridrv"
    vlog "driver: ok"
    return 0
}

# --------------------------------------------------------------------- replay

# Replay and scoring are separate calls.  A step scores the same
# recording more than once -- the whole corpus for regression breadth, a named
# subset for the area its own leap lives in -- and running the simulator twice
# to produce two numbers from one set of answers would double the step's cost
# for nothing.

# assert_no_surplus <half> <got> <want> <file>
#
# Fewer results than expectations is a candidate failure -- a session died, and
# the expectations it did not answer score as misses, which is right.  MORE
# results than expectations cannot come from the candidate at all: cpridrv
# writes its own output file with its stdout on /dev/null, e2e.py writes its
# own, and the driver, the case file and the recording are all baked into this
# image.  A surplus therefore means the recording in this image is older than
# the script that drives it, and scoring the overlap would report a confident
# number for a corpus nobody recorded.  Fail where it is visible instead: the
# golden calibration reads 0.000000 and names the file to re-record.
assert_no_surplus() {
    [ "$2" -le "$3" ] && return 0
    vlog "FATAL: $1: $2 result lines against $3 expectations in $4"
    vlog "FATAL: this image's $4 predates its driver -- mispackaged verifier,"
    vlog "FATAL: not a candidate failure.  Re-record and rebuild the image."
    detail_add "$CHECK_OWNER" "precondition:recording-current" 0
    report 0
}

# run_engine -- replay the whole case file once into $WORK/engine.txt.
#
# The whole file is always replayed, never only the families a step scores: a
# step that ran just its own families would stop noticing the day an earlier
# one broke, and the rest of the corpus costs about a second.
run_engine() {
    stage_lib
    rm -f "$WORK/engine.txt"        # never score a leftover from an earlier run
    run_unprivileged python3 "$SLIB/runner.py" "$SLIB/cpridrv" \
        "$SLIB/cases.txt" "$WORK/engine.txt" --stall 300 --quiet \
        >> "$LOG/engine-run.log" 2>&1 \
        || vlog "engine: runner.py exited $? (see engine-run.log)"
    local want got
    want=$(grep -vc '^#' "$LIB/cases.txt")
    got=$(wc -l < "$WORK/engine.txt" 2>/dev/null || echo 0)
    vlog "engine: $got result lines for $want cases"
    assert_no_surplus engine "$got" "$want" cases.txt
}

# run_e2e -- drive the candidate's own Simulator over its TCP protocol, one
# process per session, into $WORK/e2e.txt.  Everything here is candidate code
# on a socket, so it runs unprivileged too.
run_e2e() {
    stage_lib
    rm -f "$WORK/e2e.txt"
    run_unprivileged python3 "$SLIB/e2e.py" "$WORK/build/Simulator" \
        "$WORK/e2e.txt" --port 21000 --quiet \
        >> "$LOG/e2e-run.log" 2>&1 \
        || vlog "e2e: e2e.py exited $? (see e2e-run.log)"
    local want got
    want=$(grep -vc '^#' "$LIB/expected-e2e.txt")
    got=$(grep -vc '^#' "$WORK/e2e.txt" 2>/dev/null || echo 0)
    vlog "e2e: $got result lines for $want expectations"
    assert_no_surplus e2e "$got" "$want" expected-e2e.txt
}

# ---------------------------------------------------------------------- score

# score_engine <families-csv|-> <report-name> -> prints reward on stdout
score_engine() {
    local families=$1 name=$2 args=()
    [ "$families" != "-" ] && args=(--families "$families")
    score_against "$LIB/expected.txt" "$WORK/engine.txt" \
                  "$LOG/$name.json" "${args[@]}"
}

# score_e2e <families-csv|-> <report-name> -> prints reward on stdout
score_e2e() {
    local families=$1 name=$2 args=()
    [ "$families" != "-" ] && args=(--families "$families")
    score_against "$LIB/expected-e2e.txt" "$WORK/e2e.txt" \
                  "$LOG/$name.json" "${args[@]}"
}

# score_against <expected> <actual> <report> [--families csv] -> prints reward
#
# score.py's last line is "reward=<f> (<hits>/<total>)".  The counts belong in
# the log -- a family list that matched nothing scores 1.0 over zero cases,
# which only the counts reveal -- and only the number belongs on stdout.
#
# A missing actual file is scored as a file of no answers rather than allowed
# to abort the verifier: every expectation is then absent, which is 0.0, which
# is the truth about a replay that did not run.
score_against() {
    local exp=$1 act=$2 rep=$3; shift 3
    if [ ! -f "$act" ]; then
        vlog "score: $act was never written; scoring it as empty"
        : > "$act"
    fi
    local line
    line=$(python3 "$LIB/score.py" "$exp" "$act" "$@" --report "$rep" --quiet \
           | sed -n 's/^reward=//p')
    if [ -z "$line" ]; then
        vlog "score: score.py produced no reward line for $act"
        printf '0.000000'
        return
    fi
    vlog "score: $(basename "$rep" .json) $line"
    printf '%s' "${line%% *}"
}

# worst_family <report.json>... -> prints "<fraction> <family>"
#
# The lowest pass fraction any single family reaches, across every report given.
# A mean over thousands of cases cannot see one broken behaviour; this can, and
# it picks no favourites -- whichever family the defect lands in costs the same.
#
# Everything that would make the answer meaningless prints 0 and names why: an
# unreadable report, a report with no families in it, a family with no cases.
# "Nothing to be worst at" is not a perfect score.
worst_family() {
    python3 - "$@" <<'PYWF'
import json
import sys

worst = (2.0, None)
for path in sys.argv[1:]:
    try:
        fams = json.load(open(path))["families"]
    except Exception as exc:
        print("0.000000 unreadable:%s:%s" % (path.rsplit("/", 1)[-1],
                                             type(exc).__name__))
        sys.exit(0)
    if not fams:
        print("0.000000 no-families:%s" % path.rsplit("/", 1)[-1])
        sys.exit(0)
    for name, v in fams.items():
        if v["total"] <= 0:
            print("0.000000 empty-family:%s" % name)
            sys.exit(0)
        frac = v["matched"] / v["total"]
        if frac < worst[0]:
            worst = (frac, name)
if worst[1] is None:
    print("0.000000 no-reports")
else:
    print("%.6f %s" % worst)
PYWF
}

# mean <a> <b> ... -> arithmetic mean, or 0 for an empty list
mean() {
    printf '%s\n' "$@" | awk '{s += $1; n++} END {printf "%.6f", n ? s / n : 0}'
}

# clamp01 <x>
clamp01() {
    awk -v v="${1:-0}" 'BEGIN {v += 0; if (v < 0) v = 0; if (v > 1) v = 1; printf "%.6f", v}'
}

# ---------------------------------------------------------------- the gates

# The migrated-ness half of a step's score.  Each gate is one property, worth
# 0 or 1, named in the log whichever way it goes -- a gate that passes silently
# is a gate nobody can check later from the transcript alone.
GATES=()

# gate <0|1> <name> [detail ...]
gate() {
    local ok=$1 name=$2
    shift 2
    GATES+=("$ok")
    detail_add "$CHECK_OWNER" "gate:$name" "$ok"
    if [ "$ok" = 1 ]; then
        vlog "  gate ok    $name${1:+ -- $*}"
    else
        vlog "  gate FAIL  $name${1:+ -- $*}"
    fi
}

# gates_mean -> the fraction of gates that passed.
#
# An empty set scores 0, not 1.  A mean over no gates looks like a pass and
# means "nothing was measured".
gates_mean() {
    if [ ${#GATES[@]} -eq 0 ]; then
        vlog "gates: none were registered -- scoring the family 0"
        printf '0.000000'
        return
    fi
    mean "${GATES[@]}"
}

# ------------------------------------------------- the gates an earlier step
# ------------------------------------------------- introduced, re-asked here
#
# From step 02 on, a step's submission arrives having already satisfied its
# predecessors.  Those properties still have to hold -- a port that deletes
# the vendored OpenSSL at step 1 and puts a copy back at step 2 has undone
# step 1 -- but they are not what step k is being asked to do, and if they
# score in the same family as step k's own gates then the predecessor's
# finished tree collects a large fraction of step k for work it did earlier.
# At step 02 that arithmetic is 0.45*1 + 0.55*(6/12) = 0.725 against a floor
# of 0.5 (build-process item 6).
#
# So they score in a family of their own, worth a token: enough that a
# regression is visible and costs something, not enough to pay for the leap.
# The step's test.sh sets CHECK_OWNER to the step that first demanded the
# property around the block, so the detail record files each one under its
# author and `check_oracle_nop.py` reads the inherited/introduced split it
# looks for.
RGATES=()

# rgate <0|1> <name> [detail ...]
rgate() {
    local ok=$1 name=$2
    shift 2
    RGATES+=("$ok")
    detail_add "$CHECK_OWNER" "gate:$name" "$ok"
    if [ "$ok" = 1 ]; then
        vlog "  kept ok    $name${1:+ -- $*}"
    else
        vlog "  kept FAIL  $name${1:+ -- $*}"
    fi
}

# rgates_mean -> the fraction of inherited gates that still hold.
#
# Unlike gates_mean, an empty set scores 1: at step 01 there is nothing to
# inherit, and "no predecessor has been undone" is true of a tree with no
# predecessor.  A step that meant to re-ask something and registered nothing
# is caught by its own count, which the caller logs.
rgates_mean() {
    if [ ${#RGATES[@]} -eq 0 ]; then
        printf '1.000000'
        return
    fi
    mean "${RGATES[@]}"
}

# ------------------------------------------------------------- the scanners

# Both scanners read candidate-authored input -- an ELF the candidate's build
# emitted, a compile database the candidate's CMakeLists wrote -- and both are
# therefore run unprivileged, with root doing nothing but holding the output
# file open.  The scanners print their machine-readable result on stdout and
# their human summary on stderr, so `scan > json 2>> log` splits them.

# scan_elf <object> -> elfscan JSON on stdout
scan_elf() {
    stage_lib
    run_unprivileged python3 "$SLIB/elfscan.py" "$1"
}

# scan_vendor -> vendorscan JSON on stdout: how much OpenSSL is still in the
# tree, decided by reading the files rather than by matching their paths.
#
# `find -path '*/openssl/*'` and `find -name libcrypto.a` are checks on the
# name, and the instruction says "not moved aside" in as many words: a rename
# would have passed both of the gates they feed, which are 2 of the 6 in the
# migrated-ness family.  vendorscan.py asks instead whether a file declares
# OpenSSL's API and whether an archive defines OpenSSL's symbols.  Measured on
# the untouched baseline it reports the same 76 and 7 the name-based check did,
# and it still reports them after the directory and the archives are renamed.
scan_vendor() {
    stage_lib
    run_unprivileged python3 "$SLIB/vendorscan.py" --root /app \
        --out "$WORK/vendor.json"
}

# scan_ast [extra astscan args ...] -> astscan counts JSON on stdout
#
# The database is the one build_app obtained from its separate re-configure, in
# the same directory the build used, so the scan still sees exactly the
# translation units the build compiled with the flags it compiled them with.  A
# tree that built but would not re-configure has no database here, and this
# returns 1 rather than scanning something else.  It is read from root's
# custody copy (cdb_take), never from the nobody-owned build directory that
# every later candidate run can still write.
#
# Extra arguments are passed through to astscan.py so that a step can ask it for
# a fact only that step's gates need -- a named call site, say.  Which fact is
# the step's own decision, not shared machinery's: helpers.sh is one file for
# ten steps, and a probe hard-coded here would outlive the step that wanted it.
#
# The compile database is candidate-authored and its `arguments` become
# clang's argv, so -fplugin= and `-Xclang -load` would make the scanner dlopen
# a candidate-supplied object inside the process that decides the gate.
# Running unprivileged contains what such a plugin could reach; refusing the
# database outright is what stops it forging the answer on stdout.  A refusal
# prints nothing, so the caller's `jnum` reads 0 fields and the gates that
# depend on it fail -- which is the right verdict for a tree whose build
# arguments cannot be replayed safely.
scan_ast() {
    stage_lib
    local db=$CUSTODY/build/compile_commands.json
    if [ ! -f "$db" ]; then
        vlog "ast: the build produced no compile_commands.json"
        return 1
    fi
    if ! python3 - "$db" <<'PY'
import json, re, sys
# Only the forms that make clang load an object or read argv from a file.
# -Xclang is here because the vector spells itself `-Xclang -load` across two
# argv entries; -fsanitize deliberately is not, since it loads nothing and
# step 10's tree is entitled to carry it.
BAD = re.compile(r"^(-fplugin.*|-load|-Xclang|@.*)$")
try:
    db = json.load(open(sys.argv[1]))
except Exception as exc:
    sys.stderr.write("ast: compile database will not parse: %s\n" % exc)
    sys.exit(1)
for e in db:
    argv = e.get("arguments") or e.get("command", "").split()
    for a in argv:
        if BAD.search(a):
            sys.stderr.write("ast: refusing a compile database that asks "
                             "clang to load code: %r in %s\n"
                             % (a, e.get("file")))
            sys.exit(1)
sys.stderr.write("ast: %d entries, none of them load a plugin\n" % len(db))
PY
    then
        vlog "ast: compile database rejected (see verify.log)"
        return 1
    fi
    # Deliberately not --quiet: the counts go to stdout for the caller to read,
    # and the per-count human summary goes to stderr, which the caller appends
    # to the log.  A gate that fails on a number nobody can see beside it is a
    # gate nobody can check afterwards.
    run_unprivileged python3 "$SLIB/astscan.py" --compdb "$CUSTODY/build" \
        --root /app --out "$WORK/ast.json" --jobs "$NCPU" "$@"
}

# jnum <json-file> <key> -> the count under <key>: the integer itself if the
# value is a number, the length if it is a list, and 0 if the file is missing
# or unparseable.  Absent is 0 because a scanner that produced nothing has
# measured nothing, and no gate should read that as a clean tree.
jnum() {
    python3 - "$1" "$2" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print(0); sys.exit(0)
v = d.get(sys.argv[2], 0)
print(len(v) if isinstance(v, (list, dict)) else int(v))
PY
}

# jreach <json-file> <caller> <callee> -> 1 if astscan reported that CALLER
# reaches CALLEE, else 0.
#
# call_reach is a list of {caller, callee, via} records rather than a list of
# strings, so jhas cannot read it, and a step that asks about a dozen pairs
# needs an answer per pair rather than a count.  Absence is the answer 0: a
# pair that was never asked for and a pair that was asked for and not found
# both mean "this gate has not seen it", and the gate says which it was by
# also logging how many pairs came back.
jreach() {
    python3 - "$1" "$2" "$3" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print(0); sys.exit(0)
caller, callee = sys.argv[2], sys.argv[3]
print(1 if any(r.get("caller") == caller and r.get("callee") == callee
               for r in d.get("call_reach", [])) else 0)
PY
}

# jreachx <json-file> <caller> <callee> -> 1 if astscan reported that CALLER
# reaches CALLEE through any number of the tree's own functions, else 0.  Reads
# call_reach_deep, which astscan only fills when it was run with --reach-deep,
# so a step that did not ask for the walk reads 0 here and must not use it.
#
# The bounded answer and this one are different sentences and a gate picks one:
# "the work is done here or in the helper this calls" is jreach, and "the work
# is done on this entry point's account, however the port divided its own
# helpers up" is this.  A positive question asked with a bound fails a correct
# tree for its factoring; a ban asked without one fails it for a utility three
# hops down that has nothing to do with the ban.
jreachx() {
    python3 - "$1" "$2" "$3" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print(0); sys.exit(0)
caller, callee = sys.argv[2], sys.argv[3]
print(1 if any(r.get("caller") == caller and r.get("callee") == callee
               for r in d.get("call_reach_deep", [])) else 0)
PY
}

# jcall <json-file> <caller> <callee> -> 1 if CALLER's own body calls CALLEE,
# else 0.  jreach's answer includes one hop through a helper; this one does
# not, so a gate can say "written here" rather than "arrived at somehow".
jcall() {
    python3 - "$1" "$2" "$3" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print(0); sys.exit(0)
caller, callee = sys.argv[2], sys.argv[3]
print(1 if any(r.get("caller") == caller and r.get("callee") == callee
               and not r.get("via") for r in d.get("call_reach", [])) else 0)
PY
}

# jhas <json-file> <key> <regex> -> 1 if any entry in the list under <key>
# matches, else 0.
jhas() {
    python3 - "$1" "$2" "$3" <<'PY'
import json, re, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print(0); sys.exit(0)
rx = re.compile(sys.argv[3])
print(1 if any(rx.search(x) for x in d.get(sys.argv[2], [])) else 0)
PY
}
