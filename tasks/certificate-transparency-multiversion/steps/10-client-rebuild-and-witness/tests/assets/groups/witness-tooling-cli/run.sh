#!/usr/bin/env bash
# Group witness-tooling-cli.
#
# v1.1.3 adds internal/witness/, and with it four new `package main`
# directories: the witness server itself, a feeder that pushes STHs into it, a
# client that reads cosigned STHs back out, and a tool that writes the
# witness's config. CLI.md section 4.10 fixes each program's flag set, the
# default of every flag that has one, and the fact that a required flag left
# at its empty default stops the program before it does anything.
#
# These seven cases were part of this step's `build-clean` until the
# 2026-09-21 tier audit. build-clean is proof of life and may assert only what
# already exists at the predecessor anchor; at v1.1.2 none of these four
# binaries is built, so every case here failed against the step-09 tree and
# made a no-op continuation fail the gate instead of scoring zero with the gate
# passing (gate rule 3). They are graded here instead. Nothing was deleted.
#
# Cases 1-7 are observed at the process boundary only: which names the
# prerequisite build produced in $CT_BIN, what each one prints when asked for
# its usage, and the status it exits with.
#
# Case 8 is the program inventory itself, and it is here for the same reason
# the other seven are: 19 is not the count at v1.1.2, so it cannot be asserted
# from the gate (gate rule 3). CLI.md section 1's `present at` column fixes the
# inventory at every release and TEST_INTERFACE.md section 4 totals it at 19
# here -- the fifteen carried forward plus these four. build-clean names the
# fifteen and cases 1-2 above name these four; what neither can see is that the
# tree produces *those nineteen and nothing else*, which is what case 8 adds.
# It is observed the way WORKSPACE_HANDOFF.md's checkpoint says to observe it,
# by building every `package main` directory and reading back the names, never
# with `ls` over the source tree. The build runs inside the throwaway ctverify
# module, whose `replace` already points the product at the candidate's tree,
# so nothing is written into that tree and the output directory holds the
# candidate's programs and nothing else. It deliberately does NOT count
# $CT_BIN: that is the verifier's own workspace and by the time a group runs it
# also holds every driver binary compiled so far.
#
# Nothing here reads a clock or the network -- the one address any of these
# programs is given is 127.0.0.1:1, which no unprivileged process can be
# listening on.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

# The four introduced with internal/witness at this milestone.
NEW="client config feeder witness"

# --- 1. the four witness commands were built and are runnable --------------
missing=""
for b in $NEW; do
    if [ ! -s "$CT_BIN/$b" ] || [ ! -x "$CT_BIN/$b" ]; then
        missing="$missing $b"
    fi
done
if [ -n "$missing" ]; then
    fail witness-tooling-binaries-present "not built, empty or not executable:$missing"
else
    pass witness-tooling-binaries-present
fi

# --- 2. each is a flag-parsing main of its own -----------------------------
# The usage block is written by the standard library, so its sentence is not
# graded: what is graded is that --help succeeds and that the block the binary
# prints names *that binary's own* path, which is what makes it a main of its
# own rather than a shim onto someone else's.
bad=""
for b in $NEW; do
    [ -x "$CT_BIN/$b" ] || { bad="$bad $b:not-built"; continue; }
    out="$CT_SCRATCH/usage-$b.txt"
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/$b" --help > "$out" 2>&1 < /dev/null
    rc=$?
    got=$(head -n 1 "$out")
    if [ "$rc" -ne 0 ]; then
        bad="$bad $b:rc=$rc"
    elif ! printf '%s' "$got" | grep -qF -- "$CT_BIN/$b"; then
        bad="$bad $b:usage-does-not-name-the-binary"
    fi
done
if [ -n "$bad" ]; then
    fail witness-tooling-reports-its-own-usage "$bad"
else
    pass witness-tooling-reports-its-own-usage
fi

# --- 3. the witness documents the four flags that configure the server -----
if [ -x "$CT_BIN/witness" ]; then
    out="$CT_SCRATCH/usage-witness.txt"
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/witness" --help > "$out" 2>&1 < /dev/null
    absent=""
    for flag in "-config_file string" "-db_file string" "-listen string" "-private_key string"; do
        grep -q -- "^  $flag\$" "$out" || absent="$absent [$flag]"
    done
    if [ -n "$absent" ]; then
        fail witness-documents-its-server-flags "undocumented:$absent"
    else
        pass witness-documents-its-server-flags
    fi
else
    fail witness-documents-its-server-flags "witness was not built"
fi

# --- 4. the three witness tools declare the flags and defaults CLI.md 4.10
#        fixes for them. Only the flag's name, its type and its default are
#        matched: the one-line description beside them is wording and is not
#        graded (TEST_INTERFACE.md section 3.3). glog's own flags are ignored
#        here -- they are step 09's subject and reach every one of these
#        programs by the same route.
V3LIST='(default "https://www.gstatic.com/ct/log_list/v3/log_list.json")'

declares() {           # declares <binary> <case-name> <needle>...
    local b="$1" case="$2"; shift 2
    if [ ! -x "$CT_BIN/$b" ]; then
        fail "$case" "$b was not built"
        return
    fi
    local out="$CT_SCRATCH/usage-tool-$b.txt"
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/$b" --help > "$out" 2>&1 < /dev/null
    local absent=""
    local needle
    for needle in "$@"; do
        grep -qF -- "$needle" "$out" || absent="$absent [$needle]"
    done
    if [ -n "$absent" ]; then
        fail "$case" "undocumented:$absent"
    else
        pass "$case"
    fi
}

declares feeder feeder-documents-its-flags \
    "-log_list_url string" "$V3LIST" \
    "-poll duration" "(default 10s)" \
    "-witness_url string"

declares client client-documents-its-flags \
    "-log_list_url string" "$V3LIST" \
    "-poll duration" "(default 10s)" \
    "-witness_url string" "-witness_pk string"

declares config config-tool-documents-its-flags \
    "-log_list_url string" "$V3LIST" \
    "-config_file string" '(default "config.yaml")'

# --- 5. a required flag left at its empty default stops the program before
#        it does anything, with the status CLI.md 4.10 fixes. The sentence it
#        prints is not graded, only that it printed something and stopped.
bad=""
for spec in "witness:" "feeder:" "client:" "client:--witness_url=http://127.0.0.1:1"; do
    b=${spec%%:*}
    arg=${spec#*:}
    if [ ! -x "$CT_BIN/$b" ]; then
        bad="$bad $b:not-built"
        continue
    fi
    out="$CT_SCRATCH/empty-required-$b.txt"
    if [ -n "$arg" ]; then
        env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/$b" "$arg" > "$out" 2>&1 < /dev/null
    else
        env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/$b" > "$out" 2>&1 < /dev/null
    fi
    rc=$?
    if [ "$rc" -ne 1 ]; then
        bad="$bad $b${arg:+($arg)}:rc=$rc"
    elif [ ! -s "$out" ]; then
        bad="$bad $b${arg:+($arg)}:said-nothing"
    fi
done
if [ -n "$bad" ]; then
    fail witness-tooling-refuses-empty-required-flags "$bad"
else
    pass witness-tooling-refuses-empty-required-flags
fi

# --- 6. the tree produces this release's nineteen programs, and no others.
#        Built from the verifier's own module so that the candidate's tree is
#        never written to, and compared as a sorted set of names: the usage
#        text each of them prints is not part of this assertion.
WANT_19='certcheck chainfix client config crlcheck ct_hammer ct_server ctclient dumpscts feeder findlog hammer migrillian preloader scanlog sctcheck sctscan server witness'

mod19="$CT_SCRATCH/mod19"
bins19="$CT_SCRATCH/mains"
mkdir -p "$mod19" "$bins19"
cp -R "$CT_ASSETS/verifier/." "$mod19/" 2>/dev/null || true
if ( cd "$mod19" && GOFLAGS=-mod=mod GOWORK=off GOSUMDB=off GOMAXPROCS=4 \
        timeout 900 go build -p 4 -o "$bins19/" \
        github.com/google/certificate-transparency-go/... ) \
     > "$CT_SCRATCH/mains.log" 2>&1; then
    got19=$(ls "$bins19" | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//')
    if [ "$got19" = "$WANT_19" ]; then
        pass the-tree-produces-this-releases-nineteen-programs
    else
        fail the-tree-produces-this-releases-nineteen-programs \
             "built $(ls "$bins19" | wc -l) programs [$got19], want 19 [$WANT_19]"
    fi
else
    fail the-tree-produces-this-releases-nineteen-programs \
         "building every package main in the product failed: $(tr '\n' '|' < "$CT_SCRATCH/mains.log" | cut -c1-500)"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
