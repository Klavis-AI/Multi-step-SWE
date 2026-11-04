#!/usr/bin/env bash
# Verifier for step 01-pdf13-core.
#
# Grades a snapshot of /app in a container that has a C toolchain and NO PDF
# implementation of any kind: no poppler, no qpdf, no mupdf, no ghostscript, no
# pdfium. The utilities are rebuilt from the candidate's own sources with `make`,
# so a candidate that wrapped a system PDF library cannot be graded at all.
set -u

# ------------------------------------------------------- whole-verifier bound
# A verifier still running at the task's verifier timeout (3600 s) is discarded, and
# Harbor then drops the step from the trial mean, so any one stage below that a
# candidate could make block, or run on, would lock in the earlier steps' average.
# Each stage has its own bound; this one covers whatever they miss. The body runs
# again as a child of this shell under a single bound of 3300 s: above the slowest
# expected run (the 2700 s step deadline - the 180 s reserve + the runner's 300 s
# bound, plus seconds of setup and scoring around them) and, with the 15 s kill
# grace, well inside 3600 s. The body writes to a file, not to the stream Harbor
# reads, so nothing it leaves behind can hold that stream open. If the bound is
# reached, or the body ends without a reward, this shell writes reward 0 and exits 0.
if [ "${PDFL_VERIFIER_BODY:-}" != 1 ]; then
    umask 0022
    mkdir -p /logs/verifier
    PDFL_VERIFIER_BODY=1 timeout -k 15 3300s bash "$0" "$@" \
        > /logs/verifier/verifier-body.log 2>&1 < /dev/null
    rc=$?
    # Harbor downloads this directory (on some backends inside the verifier timeout),
    # so no file in it may be larger than 16 MiB, whatever wrote it.
    find /logs/verifier -maxdepth 1 -type f ! -name reward.json -size +16M \
        -exec truncate -s 16M -- {} + 2>/dev/null
    tail -c 65536 /logs/verifier/verifier-body.log 2>/dev/null
    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
        printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
        echo "verifier bound of 3300 s reached (status $rc); wrote reward 0" \
            | tee -a /logs/verifier/verifier.log
    elif [ ! -s /logs/verifier/reward.json ]; then
        printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
        echo "verifier body ended without a reward (status $rc); wrote reward 0" \
            | tee -a /logs/verifier/verifier.log
    fi
    exit 0
fi
unset PDFL_VERIFIER_BODY
source /tests/helpers.sh

# Set modes from this package rather than inheriting the runner's umask: every file
# the unprivileged scoring uid has to read would otherwise land 0600 and report as
# "the candidate is wrong" instead of "the candidate could not open its input".
umask 0022

export SCORE_UID=65534
# The privilege drop is the only thing standing between a candidate-authored Makefile
# and root. If it were conditional, a missing setpriv would silently turn it into a
# no-op and every candidate recipe would run as root with no error, so its absence is
# fatal.
if ! command -v setpriv > /dev/null 2>&1; then
    mkdir -p /logs/verifier
    printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
    echo "FATAL: setpriv missing from the verifier image; refusing to run candidate code as root" \
        | tee -a /logs/verifier/verifier.log >&2
    exit 0
fi
RUN="setpriv --reuid=$SCORE_UID --regid=$SCORE_UID --clear-groups"

LOG=/logs/verifier
mkdir -p "$LOG"
# GKE creates /logs/verifier as mode 0777 root:root. Everything below runs candidate
# code as $SCORE_UID, so on GKE that code could write /logs/verifier/reward.json
# itself -- and a detached child writing AFTER we do would win, because last writer wins.
# Every other check here guards what the candidate produces; this guards where the reward
# is written. A harness that creates this directory itself gets sane ownership, so the
# problem only appears on backends that pre-create it.
chown root:root /logs/verifier 2>/dev/null || true
chmod 0755      /logs/verifier 2>/dev/null || true
rm -f /logs/verifier/reward.json

# Prove it held rather than assume it. Attempt the forbidden write AS THE SCORING UID:
# success here means the reward file is forgeable and no score from this run means
# anything, so it is fatal. Unguarded by design -- $RUN is already fatal if setpriv is
# missing, so this probe cannot silently vanish with the thing it tests.
if $RUN sh -c 'printf x > /logs/verifier/.probe' > /dev/null 2>&1; then
    rm -f /logs/verifier/.probe
    printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
    echo "FATAL: /logs/verifier is writable by the scoring uid -- reward.json is forgeable"
    exit 0
fi
rm -f /logs/verifier/.probe
# The reward file is the one artefact a candidate would most like to write. Print what
# the directory ACTUALLY is rather than assuming the uid drop settles it, so a
# permissive mode shows up in the log.
printf 'reward dir: %s\n' "$(stat -c '%n mode=%a owner=%U:%G' /logs/verifier 2>/dev/null)" >&2
: > "$LOG/groups.txt"
: > "$LOG/verifier.log"
export LC_ALL=C.UTF-8

# Whatever happens below, this step must leave a reward.json behind: a verifier that
# exits without one is a crashed verifier, and Harbor then skips every step after it.
# The trap writes a floor of 0 if and only if nothing else has, so `report` (which
# exits 0 after writing) is unaffected and every deliberate zero still comes from it.
_last_resort() {
    local rc=$?
    if [ ! -s "$LOG/reward.json" ]; then
        printf '{"reward":0.000000}\n' > "$LOG/reward.json"
        printf 'verifier exited without reporting (status %s); wrote reward 0\n' \
               "$rc" >> "$LOG/verifier.log"
    fi
}
trap _last_resort EXIT

log() { printf '%s\n' "$*" | tee -a "$LOG/verifier.log"; }

# --------------------------------------------------- step identity / build context
# Docker's build-context sync treats a file as unchanged when (relpath, size, mtime)
# all match, so a SIBLING step's test.sh can be delivered into this image instead of
# this step's own when two siblings share a size and an mtime and differ only in a
# comment.
#
# Three defences, because any one of them alone is thin:
#  1. every test.sh in this task has a byte size no sibling shares (the collision-pad
#     line below). Distinct mtimes are not enough on their own: GNU tar's
#     default/gnu/ustar formats store mtime at 1 s granularity, so sub-second
#     separation is erased the moment the package ships as a tarball;
#  2. this script derives nothing from its own identity -- corpus/, cases.json,
#     weights.json and step_deadline are all read from /tests -- so a mis-delivered
#     sibling still grades THIS step correctly;
#  3. it says out loud which step's script is running, next to a digest of the
#     step-local data it is about to grade. If those two ever name different steps,
#     the log shows the substitution instead of hiding it.
# collision-pad: ..
VERIFIER_STEP_ID='02-pdf14-transparency'
log "verifier step-id: $VERIFIER_STEP_ID"
log "step data: cases.json=$(md5sum /tests/cases.json 2>/dev/null | cut -c1-12) weights.json=$(md5sum /tests/weights.json 2>/dev/null | cut -c1-12) deadline=$(cat /tests/step_deadline 2>/dev/null)"

# ------------------------------------------------------------------ lockdown
# /tests holds the scorer AND the answers, and `make` runs a recipe the candidate
# wrote. As root that recipe could overwrite cases.json and score itself whatever it
# likes, so the lockdown happens BEFORE the build and the build runs under the drop
# like everything else the candidate controls. Mode alone cannot do this: root
# ignores it, which is why the build is not run as root.
chmod -R go-rwx /tests 2>/dev/null || true
chmod a+rx / /tests 2>/dev/null || true
FP_BEFORE=$(find /tests -type f -exec sha256sum {} + 2>/dev/null | sort | sha256sum | cut -d' ' -f1)

# The candidate's binaries run as $SCORE_UID and must be able to open the PDFs, but
# must never be able to read the expectations. So the corpus -- and only the corpus --
# is copied out to a world-readable path; cases.json stays root-only inside /tests.
CORPUS=/run/corpus
mkdir -p "$CORPUS"
cp -r /tests/corpus/. "$CORPUS"/ 2>/dev/null || true
chmod -R a+rX "$CORPUS"
log "corpus: $(find "$CORPUS" -name '*.pdf' | wc -l) files"

# ------------------------------------------------------------------ launcher
# Every utility is started through the verifier's own launcher (helpers.sh,
# launch.c): the same privilege drop as setpriv, then no further program, no inet
# socket. Built by root before any candidate code runs, into a root-only path. A
# verifier that cannot build it refuses to run candidate binaries at all.
if ! build_launcher >> "$LOG/verifier.log" 2>&1; then
    log "FATAL: the verifier launcher did not build; refusing to run candidate binaries"
    report 0
fi
export CAND_LAUNCH

# ------------------------------------------------------------- time budget
START=$(date +%s)
DEADLINE=$(cat /tests/step_deadline 2>/dev/null || echo 2700)
RESERVE=180
left() { echo $(( DEADLINE - ($(date +%s) - START) - RESERVE )); }

# ------------------------------------------------------------------- build
# Built from source, as the candidate left it, under the unprivileged uid. The
# workspace is chowned to that uid first: /app arrives owned by whoever wrote the
# artifact, so without this the build cannot create its own output directory and the
# step reports "the candidate did not build" when the truth is "the verifier did not
# let it". Safe, because /app is the candidate's own tree in a throwaway container --
# what protects the answers is the drop plus the mode on /tests, not /app's owner.
chown -R "$SCORE_UID:$SCORE_UID" /app 2>/dev/null || true
chmod -R a+rX /app 2>/dev/null || true
BUILD_T=$(left); [ "$BUILD_T" -gt 900 ] && BUILD_T=900; [ "$BUILD_T" -lt 60 ] && BUILD_T=60

# Every build product the candidate shipped is destroyed before the scored build. The
# agent image has a compiler, a network and a package manager, so without this a
# prebuilt binary -- a real poppler or qpdf copied into build/ under the expected name,
# with a no-op Makefile -- would be graded in place of the candidate's own source.
# The utilities have to come out of THIS make.
rm -rf /app/build
find /app \( -name '*.o' -o -name '*.a' \) -delete 2>/dev/null || true
chown -R "$SCORE_UID:$SCORE_UID" /app 2>/dev/null || true
chmod -R a+rX /app 2>/dev/null || true


log "== build (timeout ${BUILD_T}s) =="
BUILD_START=$(date +%s)
BUILD_LOG=/run/pdfl/build.log
if [ -f /app/Makefile ] || [ -f /app/makefile ]; then
    # Exactly the command the workspace documents: `make`, run in /app. No added
    # flags -- a Makefile the candidate validated with plain `make` must be built the
    # same way here.
    # make inherits a writable descriptor on its output file, so that file lives in
    # a root-owned directory OUTSIDE /logs/verifier: nothing the candidate can write
    # through may sit in the directory Harbor downloads. A capped tail is copied in
    # once the uid has been reaped, below.
    # shellcheck disable=SC2086
    timeout -k 30 "${BUILD_T}s" $RUN sh -c 'cd /app && make' > "$BUILD_LOG" 2>&1
    BUILD_RC=$?
else
    BUILD_RC=127
    log "no Makefile at /app -- nothing was built; every case below fails for that reason"
fi
log "build exit: $BUILD_RC"
# Nothing the build started may outlive it: a process left running could swap the
# utilities after the checks below. From here on the workspace is root's.
if ! reap_uid "$SCORE_UID"; then
    log "FATAL: processes of the scoring uid survived the build: $(_uid_pids "$SCORE_UID" | tr '\n' ' ')"
    report 0
fi
# Nothing of the uid is left to write to the build output; keep its last MiB.
tail -c 1048576 "$BUILD_LOG" > "$LOG/build.log" 2>/dev/null || true
lock_workspace

# Diagnostic only, deliberately NOT fatal: a missing utility costs exactly the cases
# that invoke it and leaves the rest of the milestone its partial credit -- zeroing here
# would throw away a real measurement of the other three. But a run that scores near
# zero because the entry points are absent is indistinguishable, in the reward alone,
# from one that scores near zero because the parser is wrong. So say which it was, and
# say where the binary was expected.
_missing=
for t in pdfinfo pdftotext pdffonts qpdf; do
    [ -x "/app/build/$t" ] || _missing="$_missing $t"
done
if [ -n "$_missing" ]; then
    log "MISSING ENTRY POINTS:$_missing (expected at /app/build/<name>, built by \`make\` in /app)"
    log "  /app top level: $(ls /app 2>/dev/null | tr '\n' ' ')"
    log "  /app/build:     $(ls /app/build 2>/dev/null | tr '\n' ' ' || echo '<no such directory>')"
fi
chmod -R a+rX /app 2>/dev/null || true

# A utility that predates this build did not come out of it.
for t in pdfinfo pdftotext pdffonts qpdf; do
    if [ -x "/app/build/$t" ]; then
        mt=$(stat -c %Y "/app/build/$t" 2>/dev/null || echo 0)
        if [ "$mt" -lt "$BUILD_START" ]; then
            log "FATAL: /app/build/$t was not produced by this build (mtime $mt < $BUILD_START)"
            report 0
        fi
    fi
done

# ...but a fresh mtime only proves something was PLACED there. The agent image has
# root, a package manager and a public network, so a Makefile can `apt-get install
# poppler-utils` and copy the real binaries in, or drop a shell wrapper, and still pass
# the check above. So each utility must also look like this candidate's own C:
#   * not a script,
#   * an ELF,
#   * linking nothing beyond the C runtime this milestone's toolchain provides,
#   * and not a byte-for-byte copy of some other binary carried in the workspace.
ALLOWED_NEEDED='^(libc\.so|libz\.so|libm\.so|libgcc_s\.so|libpthread\.so|libdl\.so|ld-linux)'
# Root reads below are bounded: regular files only, none above BIN_CAP bytes, and
# every hash or string pass under its own timeout. A utility above the cap is treated
# as not built, so no file it could be identical to is skipped by the cap here. The
# utilities this milestone builds are a few megabytes; the cap is 256 MiB.
BIN_CAP=$((256 * 1024 * 1024))
OTHER_ELF_SHAS=$(find /app -type f -not -path '/app/build/*' -size +0 -size -$((BIN_CAP + 1))c \
                   -exec sh -c 'head -c4 "$1" | od -An -tx1 | grep -q "7f 45 4c 46" && timeout 60 sha256sum "$1"' _ {} \; \
                 2>/dev/null | cut -d' ' -f1 | sort -u)
for t in pdfinfo pdftotext pdffonts qpdf; do
    f=/app/build/$t
    # Only a regular file is opened below: a symlink or a special file in place of a
    # utility is not something make builds, and reading a FIFO would block.
    if [ -L "$f" ] || { [ -e "$f" ] && [ ! -f "$f" ]; }; then
        log "$t is not a regular file; treated as not built"
        rm -f -- "$f"
    fi
    if [ -f "$f" ] && [ "$(stat -c %s -- "$f" 2>/dev/null || echo 0)" -gt "$BIN_CAP" ]; then
        log "$t is larger than $BIN_CAP bytes; treated as not built"
        rm -f -- "$f"
    fi
    [ -x "$f" ] || continue
    if [ "$(head -c2 "$f")" = '#!' ]; then
        log "FATAL: $t is a script, not a compiled binary"; report 0
    fi
    if ! head -c4 "$f" | od -An -tx1 | grep -q "7f 45 4c 46"; then
        log "FATAL: $t is not an ELF binary"; report 0
    fi
    bad=$(readelf -d "$f" 2>/dev/null | awk '/NEEDED/{gsub(/[][]/,"",$5); print $5}' \
          | grep -Ev "$ALLOWED_NEEDED" || true)
    if [ -n "$bad" ]; then
        log "FATAL: $t links libraries beyond the C runtime: $(echo $bad | tr '\n' ' ')"
        report 0
    fi
    sha=$(timeout 60 sha256sum "$f" | cut -d' ' -f1)
    if [ -n "$OTHER_ELF_SHAS" ] && echo "$OTHER_ELF_SHAS" | grep -qx "$sha"; then
        log "FATAL: $t is byte-identical to another binary shipped in the workspace"
        report 0
    fi
done

# Interpreters are locked AFTER the build, deliberately: the agent image provides
# python3 and perl, so a candidate Makefile may legitimately use them for codegen. A
# recipe that fetches a prebuilt payload into /tmp, outside the /app byte-identity scan,
# is caught by the binary signature scan further down. What this lock buys: the scored
# binaries cannot shell out to an interpreter at run time to do the PDF work.
# PATH isolation does not stop execv("/usr/bin/python3", ...). Deny the scoring uid
# execute permission on every interpreter that could host a PDF implementation; root
# (which runs runner.py) is unaffected by the mode. /bin/sh is deliberately NOT in this
# list -- the candidate's own `make` needs it, and it cannot parse PDF.
for interp in /usr/bin/python3 /usr/bin/python3.* /usr/bin/python /usr/bin/perl \
              /usr/bin/ruby /usr/bin/node /usr/bin/php /usr/bin/tclsh; do
    [ -e "$interp" ] && chmod o-x "$interp" 2>/dev/null || true
done

# The link allowlist does not stop a candidate vendoring an entire third-party PDF
# engine written in C -- MuPDF links only libc/zlib and would pass every check above.
# So scan the SOURCES for the signatures of the known engines. This is a grep over the
# candidate's own tree, not over its output, so it cannot be satisfied by a comment.
# Library-SPECIFIC tokens only: a generic name such as `pdf_load_page` is one a
# from-scratch C library would plausibly choose, and as a raw substring it would zero
# the whole milestone for original work.
ENGINE_SIGS='fz_new_context|fz_open_document|FPDF_LoadDocument|FPDFText_GetText|poppler_document|PDFDoc::|GfxState::|QPDF::|QPDFObjectHandle|hb_buffer_create'
# Require word boundaries, so a substring is not an identifier, and never match inside
# a comment, so citing poppler in one is not vendoring it (TEST_INTERFACE: the scan
# "ignores comments").
# The scan reads C as a compiler does, so a comment is never matched: one isolated
# python3 per file (run from /, standard library only) reads a regular file without
# waiting -- a FIFO or a device is refused -- and at most 64 MiB of it, joins line
# splices, turns every // and /* */ comment into a space, and keeps string,
# character and number tokens whole so a comment marker inside one is not a
# comment. grep then matches what is left, whole identifiers only.
_SRCSCAN_PY='
import os, re, stat, sys
CAP = 64 << 20
fd = os.open(sys.argv[1], os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC)
try:
    if not stat.S_ISREG(os.fstat(fd).st_mode):
        sys.exit(1)
    chunks, n = [], 0
    while n < CAP:
        b = os.read(fd, min(1 << 20, CAP - n))
        if not b:
            break
        chunks.append(b)
        n += len(b)
finally:
    os.close(fd)
src = b"".join(chunks).replace(b"\\\r\n", b"").replace(b"\\\n", b"")
LEX = re.compile(
    rb"//[^\n]*"
    rb"|/\*[^*]*\*+(?:[^/*][^*]*\*+)*/"
    rb"|/\*.*"
    rb"|\"[^\"\\\n]*(?:\\.[^\"\\\n]*)*\"?"
    rb"|\x27[^\x27\\\n]*(?:\\.[^\x27\\\n]*)*\x27?"
    rb"|\b[0-9](?:[eEpP][+-]|[0-9A-Za-z_.])*\x27[0-9A-Za-z_](?:[eEpP][+-]|[0-9A-Za-z_.]|\x27[0-9A-Za-z_])*",
    re.S)
def keep(m):
    t = m.group(0)
    if t[:2] == b"//":
        return b" "
    if t[:2] == b"/*":
        return b" " + b"\n" * t.count(b"\n")
    return t
sys.stdout.buffer.write(LEX.sub(keep, src))
'
# _srcscan <file>: status 0 when an engine signature occurs outside the comments.
_srcscan() {
    ( cd / && timeout 30 python3 -I -c "$_SRCSCAN_PY" "$1" < /dev/null 2>/dev/null ) \
      | grep -qE "(^|[^A-Za-z0-9_])($ENGINE_SIGS)([^A-Za-z0-9_]|$)"
}
_engine_hits() {
    local f
    find /app \( -type f -o -type l \) \( -name '*.c' -o -name '*.h' -o -name '*.cc' -o -name '*.cpp' \) \
         -print0 2>/dev/null |
    while IFS= read -r -d '' f; do
        [ -f "$f" ] || continue
        _srcscan "$f" && echo "$f"
    done | head -5
}
# Only a path that is (or links to) a regular file is opened, and a link to a regular
# file is still scanned: leaving links out would let `engine.c -> engine.txt` past
# the scan.
# The scan must be able to find a signature and must not find one in a comment:
# prove both on two files written here before trusting a silence from it.
_sig1=${ENGINE_SIGS%%|*}
printf 'void *p = %s(0);\n' "$_sig1" > /run/pdfl/scan-code.c
printf '/*\n * %s\n */\nint x;\n' "$_sig1" > /run/pdfl/scan-comment.c
if ! _srcscan /run/pdfl/scan-code.c || _srcscan /run/pdfl/scan-comment.c; then
    log "FATAL: the source scan failed its self-test; it cannot be trusted"
    report 0
fi
if hits=$(_engine_hits) && [ -n "$hits" ]; then
    log "FATAL: third-party PDF engine sources in the workspace: $(echo $hits | tr '\n' ' ')"
    report 0
fi

# The source scan above only sees what is IN /app. The network cannot be taken away from
# this environment -- GKE rejects no-network outside compose mode, aborting the task while
# still printing a mean -- so a Makefile recipe can fetch a prebuilt engine
# into /tmp and install it as /app/build/pdfinfo. That binary is an ELF, links only
# libc/zlib, has a fresh mtime, and is byte-identical to nothing in /app, so every gate
# above passes it. Close it where the evidence actually is: in the produced binary.
BIN_SIGS='MuPDF|mupdf|Artifex|Ghostscript|poppler|pdfium|PDFium|libqpdf|qpdf version|xpdf|Xpdf|HarfBuzz|FreeType'
for t in pdfinfo pdftotext pdffonts qpdf; do
    f=/app/build/$t
    [ -x "$f" ] || continue
    if hit=$(timeout 120 strings -a "$f" 2>/dev/null | grep -oE "$BIN_SIGS" | sort -u | head -3 | tr '\n' ' ') \
       && [ -n "$hit" ]; then
        log "FATAL: $t contains third-party PDF engine signatures: $hit"
        report 0
    fi
    # Only unambiguous vendor prefixes belong here. `pdf_new_` is MuPDF's convention but
    # also the obvious name for any PDF library's object constructors (the reference
    # solution exports pdf_new_array/pdf_new_bool/pdf_new_dict), so it is not listed.
    if sym=$(readelf -sW "$f" 2>/dev/null | awk '{print $8}' \
             | grep -E '^(fz_|FPDF|FPDFText|opj_|jbig2_|hb_)' | sort -u | head -3 | tr '\n' ' ') \
       && [ -n "$sym" ]; then
        log "FATAL: $t exports third-party engine symbols: $sym"
        report 0
    fi
done


# The checks above saw the utilities. What they did not see -- any other compiled
# file in the workspace, anything the scoring uid left elsewhere, an interpreter
# or JVM loaded in-process -- is removed or locked before a single case runs.
log "foreign ELF files removed from /app: $(purge_foreign_code pdfinfo pdftotext pdffonts qpdf)"
wipe_uid_files "$SCORE_UID"
lock_foreign_runtimes

# A build that produced no utilities scores 0 on every group, but the groups are
# still emitted with their true denominators so the report says what was missing.
MISSING=""
for t in pdfinfo pdftotext pdffonts qpdf; do
    [ -x "/app/build/$t" ] || MISSING="$MISSING $t"
done
[ -n "$MISSING" ] && log "missing utilities:$MISSING"

# ------------------------------------------------------------------- score
CASE_T=$(left); [ "$CASE_T" -lt 60 ] && CASE_T=60
log "== scored cases (deadline ${CASE_T}s) =="
# The runner stops starting cases at CASE_T and finishes the ones in flight, each
# inside its own budget; it cannot bound a root open or read that the candidate's
# programs made block, and a verifier still running at the task's verifier timeout
# is discarded, which drops the step from the trial mean. So the run is also bounded
# from outside: DEADLINE - RESERVE + 300 = 2820 s stays inside the 3300 s bound on
# the whole verifier (top of this file). A runner stopped here records nothing, and
# the step scores 0.
RUNNER_T=$(( CASE_T + 300 ))
# Root scorer: from / and isolated (-I), so neither the cwd (/app, the candidate's
# tree) nor any PYTHON* variable or user site reaches its sys.path or its children.
( cd / && CASE_DEADLINE="$CASE_T" CASE_TIMEOUT=25 CASE_WORKERS="$(nproc)" \
    TESTS=/tests CORPUS_DIR="$CORPUS" CAND_BUILD=/app/build LOG="$LOG" \
    timeout -k 15 "${RUNNER_T}s" python3 -I /tests/runner.py ) >> "$LOG/verifier.log" 2>&1 || \
    log "runner exited $? (124/137: outer bound ${RUNNER_T}s reached); scoring what it recorded"

# -------------------------------------------------------------- tamper check
# Reading the expectations earns a good score; writing them earns any score at all,
# so a commitment that can be overwritten is not a commitment.
FP_AFTER=$(find /tests -type f -exec sha256sum {} + 2>/dev/null | sort | sha256sum | cut -d' ' -f1)
if [ "$FP_BEFORE" != "$FP_AFTER" ]; then
    log "FATAL: /tests changed during the run ($FP_BEFORE -> $FP_AFTER)"
    report 0
fi

SCORE=$(score_from_groups "$LOG/groups.txt" /tests/weights.json 2>>"$LOG/verifier.log")
log "groups:"; cat "$LOG/groups.txt" >> "$LOG/verifier.log"
log "score: $SCORE"
report "$SCORE"
