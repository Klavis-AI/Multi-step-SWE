#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Keep in sync.
#
# This file is byte-identical in every step: it names no milestone and reads no
# per-step data. What varies per step lives in /tests/STEP.stepNN,
# /tests/UNITS.stepNN and /tests/BASE_ROWS.stepNN, each named for its step so
# that the docker build context cannot serve one step's file to another.
set -u

# ---------------------------------------------------------------------------
# reward
# ---------------------------------------------------------------------------
# report <correctness 0|1> <speedup> [<speedup> ...]
#
# Writes /logs/verifier/reward.json. The step reward is binary and
# correctness-gated: 1 only when correctness is 1 and the geometric mean of the
# per-unit speedups is above ORACLE_BASELINE, otherwise 0.
#
# One speedup per measured unit in this step's scope. The geometric mean is the
# right aggregator for ratios, and it penalises neglect: a unit left alone
# contributes ln(1) = 0 and drags the step down, so a big win on one unit cannot
# hide doing nothing on another.
#
# max(1, .) floors a regression at 0 rather than going negative. Correctness is
# binary and gates the reward, so a tree that changes behaviour is rejected
# however fast it is.
ORACLE_BASELINE=2.7996000000
report() {
    local correct="${1:-0}"; shift || true
    local reward
    local geo; geo=$(awk 'BEGIN {
        n = ARGC - 1; if (n <= 0) { printf "%.4f", 1; exit }
        s = 0
        for (i = 1; i <= n; i++) { v = ARGV[i] + 0; if (v < 1) v = 1; s += log(v) }
        printf "%.4f", exp(s / n)
    }' "$@")
    mkdir -p /logs/verifier
    # Binary step reward: 1 only above the baseline.
    if [[ "${correct:-0}" != "1" ]] || ! awk -v g="${geo:-0}" -v b="$ORACLE_BASELINE" 'BEGIN { exit !(g > b) }'; then
        reward="0.000000"
    else
        reward="1.000000"
    fi
printf '{"reward":%s,"correctness":%s,"geomean_speedup":%s}\n' \
        "$reward" "${correct:-0}" "$geo" > /logs/verifier/reward.json
    # Reopen the directory on the way out, staying root-owned.  harden_reward_dir
    # leaves it 0700 so that candidate code running as nobody cannot write it;
    # but /logs is bind-mounted from the trial directory, so a host-side reader
    # running as an ordinary user sees a 0700 directory as a *missing* reward
    # and the trial dies with PermissionError after the step has completed
    # correctly.  Root-owned 0755 keeps the protection and is readable.
    chmod 0755 /logs/verifier 2>/dev/null || true
    chmod 0644 /logs/verifier/reward.json 2>/dev/null || true
    echo "REWARD correctness=${correct} speedups=[$*] geomean=${geo} reward=${reward}"
    exit 0
}

# ---------------------------------------------------------------------------
# protecting the reward from candidate code
# ---------------------------------------------------------------------------
# The check builds and runs the candidate's own library, so candidate code
# executes in this container. The session runtime chmods /logs/verifier to 777
# as root just before the tests start, and a detached child of candidate code
# outlives the reporting process -- so without both of these a candidate that
# implements nothing can write its own reward.json and claim 1.0.
#
#   harden_reward_dir   takes the directory back to root:root 0700 (it runs
#                       after that chmod, so it wins);
#   run_unprivileged    runs candidate code as `nobody`, because root can write
#                       a root-owned directory whatever the mode.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 0700 /logs/verifier 2>/dev/null || true
}

give_app_to_unprivileged() {
    chown -R nobody:nogroup /app 2>/dev/null || chown -R nobody:nobody /app 2>/dev/null || true
}

run_unprivileged() {
    setpriv --reuid=nobody --regid=nogroup --clear-groups "$@" 2>/dev/null \
        || setpriv --reuid=nobody --regid=nobody --clear-groups "$@"
}

# Pin to one core when the container's affinity mask allows it.  The timing
# itself uses CPU time rather than wall time, so pinning is not what makes the
# measurement stable -- but a stable core keeps the cache state repeatable, and
# a container given fewer than three cores must not fail the whole step.
AVS_PIN=()
if taskset -c 2 true 2>/dev/null; then AVS_PIN=(taskset -c 2); fi

# ---------------------------------------------------------------------------
# measuring
# ---------------------------------------------------------------------------
# measure <libdir> <chain.avs> <source.avs> <frames> <reps> <key>
# prints the value of <key> from avsbench's output, or empty on failure.
measure() {
    run_unprivileged "${AVS_PIN[@]}" /tests/vendor/avsbench "$1" "$2" "$3" "$4" "$5" 2>/dev/null \
        | awk -v k="$6" '$1 == k { print $2; exit }'
}

# run_unit <libdir> <unitdir> <frames> <reps> [audio] [seed0]
# Prints "<min_chain_ms> <min_source_ms> <digest_chain> <window_first>" or "" on
# failure; the digest and the window are the last repetition's.
run_unit() {
    local libdir="$1" udir="$2" frames="$3" reps="$4" audio="${5:-0}"
    local seed0="${6:-0}"
    local out
    out=$(run_unprivileged "${AVS_PIN[@]}" /tests/vendor/avsbench "$libdir" \
              "$udir/chain.avs" "$udir/source.avs" "$frames" "$reps" "$audio" "$seed0" 2>/dev/null)
    if ! printf '%s\n' "$out" | grep -q '^STATUS ok$'; then
        printf '%s\n' "$out" | awk '$1=="ERROR"{print "ERROR: " substr($0, index($0,$2))}' >&2
        return 1
    fi
    local dig chain src win
    dig=$(printf '%s\n' "$out" | awk '$1=="DIGEST_CHAIN"{print $2}')
    chain=$(printf '%s\n' "$out" | awk '$1=="MIN_CHAIN_MS"{print $2}')
    src=$(printf '%s\n' "$out" | awk '$1=="MIN_SOURCE_MS"{print $2}')
    win=$(printf '%s\n' "$out" | awk '$1=="WINDOW"{print $2}')
    # The figure of merit is min(chain) - min(source), not the minimum of the
    # per-repetition differences.  Both minima are one-sided: contention on a
    # shared host can only ever add time, so each is an estimate of the
    # uncontended cost.  A per-repetition difference is two-sided -- a stall
    # during the *source* pass deflates it -- and taking the minimum of those
    # picked the worst such event, which is how a run on a loaded box produced
    # a negative filter cost.
    printf '%s %s %s %s\n' "$chain" "$src" "$dig" "${win:-0}"
}

# ---------------------------------------------------------------------------
# telling "the cut removed it" from "there was nothing to cut"
# ---------------------------------------------------------------------------
# tree_fingerprint <tree> -- one hash over the content of every source file in
# the tree, in path order.  mtime cannot be used: mklevel.py stamps one
# timestamp over the whole tree on purpose, so every file looks equally new.
#
# Only the file types the stripper can edit or remove are hashed, so the reading
# is bounded and does not depend on whether the candidate left a build tree
# inside the source directory.
tree_fingerprint() {
    ( cd "$1" 2>/dev/null || exit 1
      find . -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 \
          | LC_ALL=C sort -z \
          | xargs -0 -r sha256sum \
          | sha256sum ) | awk '{print $1}'
}

# ---------------------------------------------------------------------------
# capability envelope (test.sh sources this file and calls the checks)
# ---------------------------------------------------------------------------
# The target host reports `avx avx2 sse4_2` and no AVX-512, so the ceiling for
# both the reference and the candidate is AVX2 / 256-bit. The reference uses
# AVX-512 in exactly one file, which is out of scope for that reason and is not
# restored at any level of the ladder -- so this check must pass the reference.
#
# Scoped to the files THIS step owns, never to the whole tree: in a cumulative
# ladder the candidate's tree also carries everything earlier steps built, and a
# wholesale check would charge a late step for early work.
#
# Scoped again to the files the candidate CHANGED.  The envelope is a rule on
# what a candidate may write, not a description of the tree it was handed: the
# tree this step starts from is the portable implementation, every capability
# the rule forbids is absent from it by construction, and a candidate that
# changed nothing in this step's scope has written nothing for the rule to
# govern.  Reading the whole scope regardless is what once made this gate fail a
# tree that changed no bytes at all -- a behaviour verdict on an untouched tree,
# which is not a thing the correctness half is allowed to say.
#
# So the check examines every scoped file the candidate wrote (added, modified
# or removed, versus <base>), and skips the ones it inherited.  A candidate that
# does write into this step's scope is checked exactly as strictly as before:
# every file it wrote is scanned for the banned constructs, and one hit fails
# the step.  Both counts are printed so a reader can tell a pass that examined
# something from a pass that examined nothing:
#
#   envelope: 3 of 17 scoped file(s) changed by the candidate, 0 with AVX-512
#   envelope: 17 of 17 scoped file(s) unchanged by the candidate -- check skipped
#
# <base> is the task base tree: the pin with every family stripped, i.e. the
# tree every candidate starts from at step 1 and the no-op never leaves.
#
# avx512_hits <file> -- true when the file's *code* uses an AVX-512 construct.
# Comments are stripped before the scan: the rule itself is written in comments
# everywhere it is stated (the instruction, this file, the project's own
# dispatch sites, which name the AVX-512 tier to say it is not carried), and a
# candidate who explains in a comment why it stays on ymm must not fail a
# capability check for saying so.  Commented-out code is not compiled and not a
# capability, so nothing enforceable is lost; AVX-512 that is a capability
# appears in the code and is caught.  The scan also catches the feature named
# as a target -- `#pragma GCC target("avx512f")` and
# `__attribute__((target("avx512f")))` -- which is how AVX-512 can be reached
# without writing a single intrinsic.
avx512_hits() {
    python3 -I - "$1" <<'PY'
import re, sys
try:
    src = open(sys.argv[1], encoding="utf-8", errors="surrogateescape").read()
except OSError:
    sys.exit(1)
src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
src = re.sub(r'//[^\n]*', ' ', src)
sys.exit(0 if re.search(r'_mm512_|__m512|__m512i|__m512d|\bzmm\b|avx512', src, re.I) else 1)
PY
}

check_isa_envelope() {
    local cand="$1" base="$2"; shift 2
    local bad=0 n=0 nchan=0 rel f pat
    local -A seen=()
    for pat in "$@"; do
        # Scoped files the candidate has: matched here, and scanned when they
        # differ from the base's copy of the same path.
        while IFS= read -r f; do
            [ -n "$f" ] || continue
            rel="${f#"$cand"/}"
            if [ -n "${seen[$rel]:-}" ]; then continue; fi
            seen[$rel]=1
            n=$((n + 1))
            if [ -f "$base/$rel" ] && cmp -s "$f" "$base/$rel"; then
                continue            # inherited unchanged -- nothing to enforce
            fi
            nchan=$((nchan + 1))
            if avx512_hits "$f"; then
                echo "  AVX-512 in $f"
                bad=1
            fi
        done < <(find "$cand" -path "$cand/$pat" -type f 2>/dev/null)
        # A scoped file the candidate removed is a change too.  There is nothing
        # left in it to scan, but it must not turn the gate vacuous.
        while IFS= read -r f; do
            [ -n "$f" ] || continue
            rel="${f#"$base"/}"
            if [ -n "${seen[$rel]:-}" ]; then continue; fi
            seen[$rel]=1
            n=$((n + 1)); nchan=$((nchan + 1))
            echo "  removed by the candidate: $rel"
        done < <(find "$base" -path "$base/$pat" -type f 2>/dev/null)
    done
    if [ "$n" -eq 0 ]; then
        echo "  no files matched this step's scope -- gate is vacuous"
        return 1
    fi
    if [ "$nchan" -eq 0 ]; then
        echo "  envelope: $n of $n scoped file(s) unchanged by the candidate -- check skipped"
        return 0
    fi
    echo "  envelope: $nchan of $n scoped file(s) changed by the candidate, $bad with AVX-512"
    return "$bad"
}

# ---------------------------------------------------------------------------
# capability envelope, other half: the compiler flags
# ---------------------------------------------------------------------------
# The envelope pins the ISA ceiling at AVX2 for both sides, and the check above
# only sees AVX-512 that is *written down* in this step's sources.  A global
# `-mavx2` (or `-march=native`, which is AVX2 on the pin's machine) reaches the
# same capability through the other door: it lifts the ISA for every
# translation unit, the compiler's auto-vectorizer writes the kernel, and the
# tree's behaviour does not change by one byte -- so the digest gate cannot see
# it, and a candidate whose speedup comes from auto-vectorized portable C has
# not done the work the milestone is about.  The instruction states the rule
# ("Do not add `-march=native` or a global `-mavx2`"); this is the check that
# makes it a constraint.
#
# The pin raises the ISA per source file from its *name*
# (avs_core/CMakeLists.txt's handle_arch_flags: `*_ssse3.cpp` -> -mssse3,
# `*_sse41.cpp` -> -msse4.1, `*_avx.cpp` -> -mavx, `*_avx2.cpp` -> -mavx2
# -mfma), and compiles the whole target with -msse2.  So this reads the flags
# the candidate's own configure produced -- compile_commands.json, which
# test.sh asks CMake to write -- and fails on any ISA-raising token a
# translation unit was compiled with that its filename does not justify:
#
#   * a global flag lands on every unit, including the portable C bodies of
#     every family, and fails at the first one;
#   * a hand-set per-file flag on a file whose name does not say `_avx2` fails
#     on that file, which is the same convention the instruction states;
#   * the flags the project itself sets by convention pass, so a candidate who
#     writes `foo_avx2.cpp` and lets the build do its job is untouched.
#
# Reading the compiler's command line rather than the CMake text means the flag
# cannot be assembled from pieces at configure time -- what is checked is what
# was compiled with.
#
# Every translation unit in the candidate's build is examined, not only the
# ones whose source changed: a global flag lands on the inherited sources too,
# and a check scoped to "the files the candidate edited" would read the exploit
# as "nothing to examine".  Nothing is charged for being inherited -- the
# allowed set is the pin's own convention, so the files the candidate started
# from pass it by construction.
check_compile_flags() {
    local src="$1" cbuild="$2"
    local ccjson="$cbuild/compile_commands.json"
    if [ ! -s "$ccjson" ]; then
        echo "  no compile_commands.json in the candidate build -- gate has no input"
        return 1
    fi
    python3 -I - "$ccjson" "$src" <<'PY'
import json, os, re, sys

ccjson, src = sys.argv[1], sys.argv[2]

# The tokens that raise the ISA.  `-mno-...` lowers it and does not match;
# -m64/-m32/-mfpmath are not matched either -- they are ABI/FP choices, not
# capabilities.
RAISE = re.compile(r'^-m(?:arch=|avx|sse|ssse3|mmx|fma|f16c|bmi|lzcnt|popcnt|'
                   r'pclmul|aes|sha|gfni|vaes|vpclmul|crc32|rdrnd|rdseed|adx|'
                   r'rtm|movbe|xop|amx|tune=|prefer-vector-width)')
# What the file's name justifies, exactly as the pin's handle_arch_flags does,
# plus the target-wide -msse2 baseline.
CONVENTION = (('_avx2.cpp',  {'-mavx2', '-mfma'}),
              ('_avx.cpp',   {'-mavx'}),
              ('_sse41.cpp', {'-msse4.1'}),
              ('_ssse3.cpp', {'-mssse3'}))
BASELINE = {'-msse2'}


def allowed(name):
    ok = set(BASELINE)
    for suffix, toks in CONVENTION:
        if name.endswith(suffix):
            ok |= toks
    return ok


try:
    entries = json.load(open(ccjson))
except Exception as exc:                                     # noqa: BLE001
    print(f"  compile_commands.json could not be read ({exc}) -- gate has no input")
    sys.exit(1)

checked = 0
bad = 0
for entry in entries:
    path = entry.get("file") or ""
    cmd = entry.get("command")
    if cmd is None:
        cmd = " ".join(entry.get("arguments") or [])
    if not path or not cmd:
        continue
    rel = os.path.relpath(path, src)
    if rel.startswith(".."):
        continue                      # not this tree's source -- not the candidate's
    checked += 1
    ok = allowed(os.path.basename(path))
    named = sorted({t.strip('"\'') for t in cmd.split()
                    if RAISE.match(t.strip('"\''))})
    for tok in named:
        if tok not in ok:
            print(f"  ISA flag {tok} on {rel} (its name does not justify it)")
            bad += 1

if checked == 0:
    print("  no translation unit in the candidate's build -- gate has no input")
    sys.exit(1)
print(f"  compiler flags: {checked} translation unit(s) checked, {bad} unexplained ISA flag(s)")
sys.exit(1 if bad else 0)
PY
}
