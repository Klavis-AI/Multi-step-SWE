#!/usr/bin/env bash
# Run the scanners over a tree and evaluate a step's rules against the facts.
#
# One entry point so a step's test.sh has one line of gate code and no opinion
# about how a fact is produced. Everything it writes lands under --out and is
# meant to be kept with the run: the four fact files are the evidence a finding
# cites, and a reviewer who disagrees with a verdict can re-evaluate different
# rules against the same facts without re-running anything.
#
# The probe is the one scanner that cannot be run from here -- its facts come
# out of the job under test, via GATE_PROBE_OUT, minutes earlier and in another
# container. So it is passed in, and a step that needs it and forgot gets
# ERROR from every probe rule rather than a quiet pass (rules.py).
#
# Exits 0 always, like every other gate here: a finding is output, not a
# process failure, and the verifier decides what a finding is worth.
#
# --build is the one scanner that changes the tree it reads: it compiles it,
# twice. It is opt-in for that reason, and it is deliberately a separate flag
# from --tree even though it takes the same directory -- a step whose rules ask
# nothing about the build should not pay forty seconds and a mutated target/
# for the privilege. It is also the first scanner to run, and the block below
# says why that is not a detail.
#
#   usage: gate-run.sh --rules <file> --out <dir>
#                      [--tree <dir>] [--build <dir>] [--classes <dir|jar|glob>]...
#                      [--probe <tsv>]... [--probe-labelled <label>=<tsv>]...
#                      [--only <binary/prefix>[,<prefix>]...]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
RULES= OUT= TREE= BUILD= ONLY=edu/sdsc/mmtf
CLASSES=()
PROBE=()
# One per --probe, index for index: the name the caller vouches for, or empty
# for a file that speaks for itself. A replay's files are named by root from
# its own record of which job it issued which token to; a standalone probe
# runs one program into a file of its own and needs no vouching.
PLABEL=()
while [ $# -gt 0 ]; do
    case $1 in
        --rules)   RULES=$2; shift 2 ;;
        --out)     OUT=$2; shift 2 ;;
        --tree)    TREE=$2; shift 2 ;;
        --build)   BUILD=$2; shift 2 ;;
        --classes) CLASSES+=("$2"); shift 2 ;;
        --probe)   PROBE+=("$2"); PLABEL+=(""); shift 2 ;;
        --probe-labelled)
                   case $2 in
                       *=*) PLABEL+=("${2%%=*}"); PROBE+=("${2#*=}") ;;
                       *) echo "gate-run: --probe-labelled wants <label>=<path>, got $2" >&2
                          exit 2 ;;
                   esac
                   shift 2 ;;
        --only)    ONLY=$2; shift 2 ;;
        *) echo "gate-run: unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$RULES" ] && [ -n "$OUT" ] || {
    echo "gate-run: --rules and --out are required" >&2; exit 2; }
# An empty --only is not a narrower scan, it is no scan restriction at all:
# class-scan.py reads an empty prefix list as "every class", which on a shaded
# uber jar means every dependency's constant pool as well. A caller that
# computed its list and got nothing back is a caller whose derivation broke,
# and turning that into the widest possible reading is the wrong direction.
[ -n "$ONLY" ] || {
    echo "gate-run: --only was given empty; refusing to scan unrestricted" >&2
    exit 2; }
mkdir -p "$OUT"

# GATE_AS, when set, is the command prefix that the three scanners which run
# Maven on the tree or compile against it -- build-scan.sh, ast-scan.sh and
# pom-scan.sh -- are started under, while this script, its redirections,
# class-scan.py and rules.py stay with the caller. The verifier runs this script
# as root and sets GATE_AS to its unprivileged identity, so every fact file
# below is opened by root, in a directory root owns, and nothing the
# candidate's build leaves running can rewrite one after its scanner has
# exited. The scanners' own work directories ($OUT/build, $OUT/pom) must then
# already exist and belong to that identity; the caller makes them. Unset --
# every authoring rehearsal -- everything runs as the caller, as before.
AS=()
[ -n "${GATE_AS:-}" ] && read -r -a AS <<< "$GATE_AS"

# Each scanner writes its facts even when it has nothing to say, so a missing
# file means the scanner was not asked for rather than found nothing -- and
# rules.py tells those two apart. An empty file is a reading; no file is not.
#
# Which leaves the third case, the one that reads like the second and is not:
# a scanner that *died*. Its stdout was redirected here before it ran, so it
# leaves a file, and the file is empty or half-written -- and an `absent` rule
# over an empty reading is a rule that PASSes for want of evidence. An
# ast-scan.sh that cannot find javac made every ast rule green once, on this
# box, which is how this block came to exist.
#
# So a non-zero exit is turned into that scanner's own `error` fact, which the
# standing rules already watch for. The reason then names the scanner rather
# than the tree -- the distinction Hole 3 of NOTES 6.1.14 is about: a candidate
# who deleted every source leaves a scanner that exited 0 with nothing to say,
# and that is `ast-nonempty`'s FAIL and not this.
#
# `scanned` also refuses to let a *silent* failure through for the four
# scanners that cannot legitimately be quiet: pom-scan always knows the
# project's coordinates, build-scan always knows how its rounds exited,
# class-scan always names the containers it opened, and a probe file is
# concatenated from jobs that ran. Only ast may legitimately emit nothing, and
# only because an empty source tree is a real (and separately scored) answer.
scanned() {  # scanned <scanner> <rc> <fact-file> <err-file> <subject> [--may-be-empty]
    local scanner=$1 rc=$2 out=$3 err=$4 subject=$5 empty_ok=${6:-}
    local why=
    if [ "$rc" != 0 ]; then
        why="$scanner exited $rc: $(head -1 "$err" 2>/dev/null | tr '\t' ' ' | cut -c1-120)"
    elif [ ! -s "$out" ] && [ "$empty_ok" != --may-be-empty ]; then
        why="$scanner exited 0 and emitted no facts at all"
    else
        return 0
    fi
    case $scanner in
        ast-scan)   printf 'error\t%s\t0\tscanner\t%s\n' "$subject" "$why" >> "$out" ;;
        class-scan) printf 'error\t%s\t%s\n' "$subject" "$why" >> "$out" ;;
        *)          printf 'error\t%s\t%s\n' scanner "$why" >> "$out" ;;
    esac
    echo "gate-run: $why" >&2
}

sources=()
if [ -n "$BUILD" ]; then
    # First, and this is the ordering that matters most in the file.
    #
    # --build is the only scanner that executes the candidate's code: a Maven
    # plugin the candidate declared runs with the same identity as this script.
    # Every fact file already on disk when that happens is a file that code can
    # rewrite, and a gate whose pom.tsv was replaced between being written and
    # being read reports whatever the candidate wanted it to. So the build goes
    # first and every other scanner writes its facts after it -- the scanners
    # that read a tree, and then --classes, which must in any case read the
    # artifact the second round produced rather than whatever was lying in
    # target/ when the verifier started.
    #
    # This does not make the output tamper-proof on its own; nothing run beside
    # candidate code can be. It closes the window in which a fact is written
    # early and read late, and leaves the verifier one thing to guard: the
    # findings, which a step re-derives from a root-owned copy of these files
    # after the unprivileged half of the run is over.
    ${AS[@]+"${AS[@]}"} "$HERE/build-scan.sh" "$BUILD" "$OUT/build" > "$OUT/build.tsv" 2> "$OUT/build.err"
    scanned build-scan $? "$OUT/build.tsv" "$OUT/build.err" "$BUILD"
    sources+=("build=$OUT/build.tsv")
fi
if [ -n "$TREE" ]; then
    ${AS[@]+"${AS[@]}"} "$HERE/ast-scan.sh" "$TREE/src" > "$OUT/ast.tsv" 2> "$OUT/ast.err"
    scanned ast-scan $? "$OUT/ast.tsv" "$OUT/ast.err" "$TREE/src" --may-be-empty
    sources+=("ast=$OUT/ast.tsv")
    # pom-scan's Maven base directory and repository head are made here, by
    # the caller, fresh and in $OUT, which the caller owns (wave H round 3,
    # G2-m1). In the scanner's own work directory they were whatever the
    # candidate's build -- the same uid, run before this -- had left there: a
    # .mvn/ in the base directory, a plugin jar in the head ahead of the
    # pristine tail. The base directory stays the caller's and empty. The head
    # goes to the scanner's uid only when the scan may download
    # (POM_SCAN_ONLINE=1), since a download is a write; offline, the tail
    # serves everything. Not made means not scanned as ours: an error fact.
    penv=$OUT/pom.env penv_ok=1
    { rm -rf "$penv" && mkdir -m 0755 "$penv" "$penv/basedir" "$penv/m2"; } || penv_ok=0
    if [ "$penv_ok" = 1 ] && [ "${POM_SCAN_ONLINE:-0}" = 1 ] && [ "${#AS[@]}" -gt 0 ]; then
        chown "$("${AS[@]}" id -u):$("${AS[@]}" id -g)" "$penv/m2" || penv_ok=0
    fi
    POM_SCAN_BASEDIR=$penv/basedir POM_SCAN_HEAD=$penv/m2 \
        ${AS[@]+"${AS[@]}"} "$HERE/pom-scan.sh" "$TREE" "$OUT/pom" > "$OUT/pom.tsv" 2> "$OUT/pom.err"
    scanned pom-scan $? "$OUT/pom.tsv" "$OUT/pom.err" "$TREE"
    [ "$penv_ok" = 1 ] || printf 'error\tscanner\t%s\n' \
        "the caller could not make pom-scan's base directory and repository head in $OUT" >> "$OUT/pom.tsv"
    sources+=("pom=$OUT/pom.tsv")
fi
if [ "${#CLASSES[@]}" -gt 0 ]; then
    # Globbed here rather than by the caller's shell. A step that reads the
    # artifact reads one --build has just produced, and `target/*.jar` does not
    # exist yet at the moment the caller writes it -- expanded there it would
    # arrive as the literal pattern and class-scan would report no classes,
    # which reads exactly like a jar with nothing in it.
    globbed=()
    for c in "${CLASSES[@]}"; do
        for m in $c; do [ -e "$m" ] && globbed+=("$m"); done
    done
    # A pattern that matched nothing is passed through unexpanded on purpose:
    # class-scan then emits an `error` fact naming it, the standing
    # classes-readable rule goes red, and the reading is void rather than
    # quietly empty.
    [ "${#globbed[@]}" -gt 0 ] || globbed=("${CLASSES[@]}")
    python3 "$HERE/class-scan.py" --only "$ONLY" "${globbed[@]}" \
        > "$OUT/bytecode.tsv" 2> "$OUT/bytecode.err"
    scanned class-scan $? "$OUT/bytecode.tsv" "$OUT/bytecode.err" "${globbed[0]}"
    sources+=("bytecode=$OUT/bytecode.tsv")
fi
if [ "${#PROBE[@]}" -gt 0 ]; then
    # Concatenated rather than referenced, and --probe repeats: a step runs many
    # jobs and each writes its own file, and a rule that counts "how many scored
    # jobs carried a SQL execution" has to see all of them as one reading.
    cat "${PROBE[@]}" > "$OUT/probe.tsv" 2>/dev/null || : > "$OUT/probe.tsv"
    # And then, per file and before the flattening is final, the three derived
    # kinds. A rule that wants to say "every one of this repository's programs
    # reported X" needs to know which rows are whose, and after the cat above
    # nothing does. probe-derive.py judges one file at a time and re-emits its
    # rows under `appsession` / `appconf` / `appsetconf`; a file the replay did
    # not label, the gate's own session probe, and the suite's HarnessDriver
    # rows contribute nothing. Appended rather than interleaved because rules.py
    # reads a table, not a sequence, and because a probe file that is empty or
    # malformed must not be able to stop the concatenation it follows.
    for i in "${!PROBE[@]}"; do
        f=${PROBE[$i]} lab=${PLABEL[$i]}
        if [ -n "$lab" ]; then
            python3 "$HERE/probe-derive.py" --label "$lab" < "$f" \
                >> "$OUT/probe.tsv" 2>/dev/null || true
        else
            python3 "$HERE/probe-derive.py" < "$f" >> "$OUT/probe.tsv" \
                2>/dev/null || true
        fi
    done
    scanned session-probe 0 "$OUT/probe.tsv" /dev/null "${PROBE[0]}"
    sources+=("probe=$OUT/probe.tsv")
fi

python3 "$HERE/rules.py" --rules "$RULES" "${sources[@]}" > "$OUT/findings.tsv" \
    2> "$OUT/rules.err"
if [ -s "$OUT/rules.err" ]; then
    # A malformed rules file or a fact whose arity the schema denies. That is a
    # broken gate, not a finding, and it must not read as "nothing was found".
    printf 'ERROR\tgate-run\t0\t%s\n' \
        "$(head -1 "$OUT/rules.err" | cut -c1-200)" >> "$OUT/findings.tsv"
fi
awk -F'\t' '$1 == "PASS" { p++ } $1 == "FAIL" { f++ } $1 == "ERROR" { e++ }
     END { printf "gate-run: %d pass, %d fail, %d error\n", p, f, e }' \
    "$OUT/findings.tsv"
exit 0
