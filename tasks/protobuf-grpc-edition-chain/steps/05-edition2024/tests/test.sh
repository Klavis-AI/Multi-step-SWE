#!/usr/bin/env bash
# Verifier for step 05-edition2024.
#
# Runs in this step's own container on a fresh copy of /app. Nothing written
# here reaches the next step; only /logs/verifier is read back.
#
# Everything scored comes from /tests (pristine, baked into this image at build
# time). Nothing under /app is trusted except the source that gets rebuilt.
set -u
source /tests/helpers.sh

# --- whole-run bound ---------------------------------------------------------
# task.toml gives every step's verifier timeout_sec = 3600. Harbor discards a
# step whose verifier overruns -- reward.json or not -- and aborts every later
# step, so a build, a gate walk or a scored run the candidate can stretch would
# otherwise trade a step it cannot pass for a chain it cannot lose. This script
# re-runs itself under a hard bound well inside that; on expiry the candidate
# account is swept and the step scores 0. Worst case: 3300 + 30 kill grace +
# 10 s sweep < 3360. The reward directory is root's alone before anything of
# the candidate's runs (see the reward-directory section below).
if [ "${1:-}" != "--bounded" ]; then
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 755 /logs/verifier 2>/dev/null || true
    rm -f /logs/verifier/reward.json
    cd /
    timeout -k 30 3300 bash "$0" --bounded
    rc=$?
    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ] || [ ! -s /logs/verifier/reward.json ]; then
        sweep_candidate
        printf 'verifier: the run did not finish inside its bound (exit %s); reward 0\n' \
            "$rc" >> /logs/verifier/bound.log
        report 0
    fi
    exit 0
fi
# Never from /app: nothing here may resolve a name against the submitted tree.
cd /

mkdir -p /logs/verifier
LOG=/logs/verifier/step05.log
: > "$LOG"

# No code path may exit without a reward. `report` writes the file and exits,
# so if EXIT is reached with no reward.json, something died between the report
# points -- and an absent reward is indistinguishable from a killed pod. Write
# a zero instead, and say so.
on_exit() {
    local rc=$?
    if [ ! -f /logs/verifier/reward.json ]; then
        printf '{"reward":0.000000}\n' > /logs/verifier/reward.json
        printf 'verifier exited (status %s) without reporting; wrote reward 0\n' \
            "$rc" >> "$LOG" 2>/dev/null || true
    fi
}
trap on_exit EXIT

say() { printf '%s\n' "$*" | tee -a "$LOG"; }

# Which step's files are actually inside this image? test.sh and the Dockerfile are
# separate entries in the build context and docker's sync can substitute either one
# from a sibling step when (relpath, size, mtime) match. This line makes such a
# substitution visible in the log rather than silent.
# collision-pad: .....
VERIFIER_STEP_ID='05-edition2024'
say "verifier step-id: test.sh=$VERIFIER_STEP_ID dockerfile=$(cat /opt/verifier/dockerfile-step-id 2>/dev/null || echo UNRECORDED)"

# The candidate is rebuilt from source with this image's toolchain. Any binary
# carried in the snapshot is discarded first, so a prebuilt or vendored
# executable cannot be what gets graded.
# --- reward-directory integrity --------------------------------------------
# The candidate's own Makefile runs in this container. Anything it can write to
# /logs/verifier it can forge a reward with, and a detached child can outlive
# this script. So: record what the reward directory actually IS before the
# candidate's build runs, run that build as an unprivileged user, and re-check
# afterwards. Printed, never assumed.
say "=== reward directory, before candidate build ==="
say "  $(stat -c 'path=%n mode=%a owner=%U:%G' /logs/verifier 2>&1)"
say "  euid=$(id -u) ($(id -un))"
# Harbor sets 0777 with no sticky bit on the verifier directory before every
# step, so unlinking and recreating a root-owned reward.json needs only
# directory write. Tightening the DIRECTORY closes the reward.txt plant, the
# unlink, and a late detached writer at once; file-level guards must enumerate.
chown root:root /logs/verifier 2>/dev/null || true
chmod 755 /logs/verifier 2>/dev/null || true
say "  reward dir after tightening: $(stat -c 'mode=%a owner=%U:%G' /logs/verifier 2>&1)"
REWARD_GUARD=/logs/verifier/.guard
: > "$REWARD_GUARD" 2>/dev/null || true

# An unprivileged builder, created here rather than assumed to exist.
if ! id candidate >/dev/null 2>&1; then
    useradd -M -s /bin/sh candidate 2>/dev/null || true
fi
BUILD_AS=""
if id candidate >/dev/null 2>&1 && [ "$(id -u)" = "0" ]; then
    chown -R candidate /app 2>/dev/null || true
    # Verify the drop rather than assume it. /app arrives owned by whoever
    # Harbor collected it as; if the unprivileged user cannot write there, an
    # unprivileged build would fail for EVERY candidate -- which looks from
    # outside exactly like a hard task instead of a broken image.
    if su -s /bin/sh candidate -c 'touch /app/.wtest 2>/dev/null && rm -f /app/.wtest' 2>/dev/null; then
        BUILD_AS="candidate"
        say "  candidate build will run as: candidate (uid $(id -u candidate)), write access verified"
    else
        # Fail closed. Building as root would hand the candidate's Makefile
        # the reward directory; a loud zero on every candidate is the
        # visible failure, a silent root build is not.
        say "VERIFIER DEFECT: candidate cannot write /app ($(stat -c '%U:%G %a' /app 2>/dev/null)); refusing to build as $(id -un) -- reward 0"
        report 0
    fi
else
    say "VERIFIER DEFECT: no unprivileged candidate account, or not root ($(id -un)); refusing to run the candidate's build -- reward 0"
    report 0
fi

# Checksum every file the candidate shipped, BEFORE its build runs. If
# /app/bin/protoc afterwards matches one of them byte for byte, the build
# copied a binary rather than producing one -- which is how a downloaded
# official protoc would score 1.0 against expectations that ARE its output.
BUILD_START=$(date +%s)
say "=== fingerprinting the submitted tree ==="
# md5sum prefixes the line with a backslash when the name holds a newline or
# a backslash; strip it, or such a file's digest never matches below.
find /app -type f ! -path '/app/bin/*' -exec md5sum {} + 2>/dev/null \
    | awk '{h = $1; sub(/^\\/, "", h); print h}' | sort -u > /tmp/pre_build.md5
say "  $(wc -l < /tmp/pre_build.md5) distinct file digests recorded"

say "=== removing candidate build products ==="
rm -rf /app/bin 2>/dev/null || true
find /app -name '*.o' -delete 2>/dev/null || true
find /app -name '*.a' -delete 2>/dev/null || true

say "=== make ==="
build_candidate() {
    if [ -n "$BUILD_AS" ]; then
        su -s /bin/sh "$BUILD_AS" -c 'cd /app && timeout -k 30 900 make' >>"$LOG" 2>&1
    else
        ( cd /app && timeout -k 30 900 make ) >>"$LOG" 2>&1
    fi
}
if ! build_candidate; then
    say "BUILD FAILED — reward 0"
    report 0
fi

# Nothing the build started may still be running while the gates read the tree
# or the scorer runs it, and nothing of the candidate's may change the tree once
# the gates have read it: sweep the account, then give /app to root, read-only
# to everyone else. The scored programs run as `candidate` and only read /app;
# their outputs go to scorer-made temporary directories.
sweep_candidate
chown -R root:root /app 2>/dev/null || true
chmod -R a+rX,go-w /app 2>/dev/null || true

if [ ! -x /app/bin/protoc ]; then
    say "/app/bin/protoc missing or not executable — reward 0"
    report 0
fi
say "build ok: $(ls -l /app/bin/protoc | awk '{print $5}') bytes"

# --- the artifact must be BUILT here, not vendored --------------------------
# The instruction requires the candidate's own implementation and forbids a
# third-party protobuf. Three independent signals, any one of which is fatal.
vendor_fail() { say "VENDORED IMPLEMENTATION: $1"; report 0; }

# A gate whose tool is absent is not a lenient gate, it is no gate: the
# pipeline produces nothing, grep -q is false, and the check passes while
# every other family still scores. Absence is therefore fatal, and the image
# asserts these at build time so it cannot ship without them.
require_tools() {
    miss=""
    for t in "$@"; do command -v "$t" >/dev/null 2>&1 || miss="$miss $t"; done
    [ -z "$miss" ] || { say "VERIFIER DEFECT: missing tool(s):$miss - gates cannot run"; report 0; }
}
require_tools nm strings readelf file md5sum python3 od find

built_digest=$(md5sum /app/bin/protoc | awk '{print $1}')
if grep -qx "$built_digest" /tmp/pre_build.md5; then
    vendor_fail "/app/bin/protoc is byte-identical to a file that was already in the submitted tree; the build copied it rather than producing it"
fi

# Upstream protobuf is C++. These symbols and strings cannot occur in a C
# implementation, and deliberately avoid anything this task's own binary
# legitimately contains (it embeds the .proto sources and prints a protoc
# version string, so "google/protobuf" and "libprotoc" are NOT usable signals).
# A four-line C wrapper that execs a renamed official protoc is a real ELF with no
# C++ symbols of its own, so scanning /app/bin/protoc alone is not enough, and the
# real binary need not stay under /app: it can be unpacked to /tmp during make.
# Roots are the complete candidate-writable set, established by census of the
# image: /tmp /var/tmp /dev/shm /run/lock are the only world-writable dirs, and
# /app is candidate-owned after chown. Everything else is 755 root and carries
# zero protobuf files, so this cannot false-positive on the image itself.
# /opt/verifier (the graders) and /tests are deliberately NOT roots: unwritable,
# and conformance_test_runner would otherwise flag itself.
# NUL-separated: a file name may hold any byte except NUL, newlines included.
while IFS= read -r -d '' _elf; do
    [ -n "$_elf" ] || continue
    if nm -C "$_elf" 2>/dev/null | grep -qE 'google::protobuf::|google::protobuf_'; then
        vendor_fail "$_elf exports google::protobuf C++ symbols; an upstream binary in any candidate-writable location is a vendored implementation"
    fi
    if strings -a "$_elf" 2>/dev/null | grep -qE 'CodedInputStream|GeneratedMessageReflection|upb_MiniTable'; then
        vendor_fail "$_elf carries upstream protobuf runtime strings"
    fi
    # The project is C. No ELF the walk finds -- a built program, a helper, an
    # object, anything left in a scratch directory -- may carry a C++ runtime:
    # statically linked it shows in no NEEDED entry, and renaming identifiers
    # does not remove it.
    if strings -a "$_elf" 2>/dev/null | grep -qE '__cxxabiv1|__gxx_personality_v0|St9exception'; then
        vendor_fail "$_elf carries the C++ runtime; the implementation is to be written in C"
    fi
done < <(find /app /tmp /var/tmp /dev/shm /run/lock -type f -exec sh -c 'head -c4 "$1" | od -An -tx1 | tr -d " \n" | grep -q "^7f454c46"' _ {} \; -print0 2>/dev/null)

# The ELF walk above sees only files that are ELF AT GATE TIME. Upstream protoc
# shipped as a compressed blob and unpacked to /tmp during `make` is never an ELF
# under /app, so the walk alone does not see it.
# blobscan decompresses non-ELF files and looks for upstream markers in the
# INFLATED bytes. Resolved relative to this script so it does not depend on where
# the image happens to mount tests/.
_bs=$(dirname "$0")/blobscan.py
if [ -f "$_bs" ]; then
    _blob=$(python3 -I "$_bs" /app 2>>"$LOG" | head -1)
    if [ -n "$_blob" ]; then
        vendor_fail "compressed upstream payload in the submitted tree -- $_blob"
    fi
else
    say "VERIFIER DEFECT: blobscan.py missing beside test.sh - compressed-payload gate cannot run"
    report 0
fi

if nm -C /app/bin/protoc 2>/dev/null | grep -qE 'google::protobuf::|google::protobuf_'; then
    vendor_fail "/app/bin/protoc exports google::protobuf C++ symbols"
fi
if strings -a /app/bin/protoc 2>/dev/null \
     | grep -qE 'CodedInputStream|CodedOutputStream|protobuf_AssignDescriptors|DescriptorPool::|WireFormatLite|GOOGLE_PROTOBUF_VERSION'; then
    vendor_fail "/app/bin/protoc contains upstream C++ protobuf runtime strings"
fi
if find /app \( -name '*.pb.cc' -o -name '*.pb.h' -o -name 'libprotobuf*' \
                -o -name 'libprotoc*' -o -name 'protoc-*-linux*' \
                -o -name 'protoc-*.zip' \) -print -quit 2>/dev/null | grep -q .; then
    vendor_fail "the tree carries a third-party protobuf implementation or distribution"
fi
# The artifact must be an ELF produced by this build. A shell or python
# wrapper around some other tool has magic 2321 ("#!") and fails here.
magic=$(head -c4 /app/bin/protoc 2>/dev/null | od -An -tx1 | tr -d ' \n')
if [ "$magic" != "7f454c46" ]; then
    vendor_fail "/app/bin/protoc is not an ELF executable (magic '$magic'); a script or wrapper cannot be the artifact"
fi

if [ "$(stat -c %Y /app/bin/protoc)" -lt "$BUILD_START" ]; then
    vendor_fail "/app/bin/protoc is older than this build; it was shipped, not compiled here"
fi

# The instruction states the toolchain aloud: the C standard library and zlib.
# Anything else linked in is a third-party implementation by definition.
for lib in $(readelf -d /app/bin/protoc 2>/dev/null | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p'); do
    case "$lib" in
        libc.so.6|libm.so.6|libz.so.1|libgcc_s.so.1|ld-linux-*.so.*) ;;
        *) vendor_fail "/app/bin/protoc links $lib; the stated toolchain is the C standard library and zlib only" ;;
    esac
done

# The three checks above all pass for a candidate that vendors upstream
# SOURCES and compiles them: the binary is genuinely built here, links only
# libc, and matches no file in the tree. So scan the sources themselves.
# These tokens are upstream-specific on purpose -- "FileDescriptorProto" and
# "google/protobuf" are NOT usable, because any correct implementation of this
# task contains both, including the reference one.
if grep -rlIE 'upb_MiniTable|upb_Message|upb_Arena|google::protobuf|PROTOBUF_NAMESPACE|GOOGLE_PROTOBUF_VERIFY_VERSION|WireFormatLite|CodedInputStream|GeneratedMessageReflection|absl/strings|#include <google/' \
      /app 2>/dev/null | head -1 | grep -q .; then
    vendor_fail "candidate sources carry upstream protobuf engine signatures; a vendored implementation compiled from source is still a vendored implementation"
fi

# This step has a second artifact: the conformance test program, which speaks
# the length-prefixed ConformanceRequest/ConformanceResponse protocol on stdin
# and stdout. Its ABSENCE is not fatal -- the conformance families simply score
# zero, and failing the whole step here would discard the credit the compiler
# genuinely earned. Its PRESENCE means it gets the same gates.
if [ -x /app/bin/conformance ]; then
    cdigest=$(md5sum /app/bin/conformance | awk '{print $1}')
    if grep -qx "$cdigest" /tmp/pre_build.md5; then
        vendor_fail "/app/bin/conformance is byte-identical to a file that was already in the submitted tree"
    fi
    cmagic=$(head -c4 /app/bin/conformance 2>/dev/null | od -An -tx1 | tr -d ' \n')
    if [ "$cmagic" != "7f454c46" ]; then
        vendor_fail "/app/bin/conformance is not an ELF executable (magic '$cmagic')"
    fi
    if [ "$(stat -c %Y /app/bin/conformance)" -lt "$BUILD_START" ]; then
        vendor_fail "/app/bin/conformance is older than this build; it was shipped, not compiled here"
    fi
    for lib in $(readelf -d /app/bin/conformance 2>/dev/null | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p'); do
        case "$lib" in
            libc.so.6|libm.so.6|libz.so.1|libgcc_s.so.1|ld-linux-*.so.*) ;;
            *) vendor_fail "/app/bin/conformance links $lib; the stated toolchain is the C standard library and zlib only" ;;
        esac
    done
    if nm -C /app/bin/conformance 2>/dev/null | grep -qE 'google::protobuf::|google::protobuf_'; then
        vendor_fail "/app/bin/conformance exports google::protobuf C++ symbols"
    fi
    say "conformance program: present and built here"
else
    say "NOTE: /app/bin/conformance is absent -- both conformance families will"
    say "      score 0. The compiler families are still scored."
fi

say "provenance: built here, no third-party protobuf detected"

# If the candidate's build wrote a reward, this run is void. Re-derive rather
# than trust: remove anything it left and report it loudly.
if [ -e /logs/verifier/reward.json ]; then
    say "REWARD FORGERY: candidate build wrote /logs/verifier/reward.json"
    rm -f /logs/verifier/reward.json
    report 0
fi
say "=== reward directory, after candidate build ==="
say "  $(stat -c 'mode=%a owner=%U:%G' /logs/verifier 2>&1)"

# Scored runs never see /app on PATH: a vendored protoc cannot be resolved by
# name, and the only tools reachable are this image's own.
export PATH=/opt/verifier/bin:/usr/bin:/bin
export LC_ALL=C.UTF-8

say "=== scoring ==="
SCORE=$(timeout -k 30 2100 python3 -I /tests/run_step05.py --app /app --tests /tests --log "$LOG" 2>>"$LOG")
rc=$?
# The scorer stops what it started; a double-forked child escapes that.
sweep_candidate
if [ $rc -ne 0 ] || [ -z "${SCORE:-}" ]; then
    say "scorer exited $rc with score '${SCORE:-}' — reward 0"
    report 0
fi

say "reward=$SCORE"
report "$SCORE"
