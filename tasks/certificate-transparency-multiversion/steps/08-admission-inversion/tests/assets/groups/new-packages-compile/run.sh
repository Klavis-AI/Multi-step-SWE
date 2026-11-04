#!/usr/bin/env bash
# Group new-packages-compile. KIND=scored.
#
# The five packages v1.1.0 brings into being -- ctpolicy/, submission/,
# schedule/, watcher/ and loglist2/ (CLIENT-API.md sections 7 and 4.2). A
# v1.0.21 tree has none of them, so none of these cases can pass against it,
# which is exactly why they cannot sit in the gate: a gate case that only the
# new capability satisfies is an unpriced veto.
#
# Observed only by building: one probe per package inside the throwaway
# ctverify module, whose `replace` points the product at the candidate's own
# tree. Each probe imports the package under a local name and uses an exported
# name from it, so the import cannot be elided and a package that exists but is
# empty does not pass. Nothing here reads a candidate source file, runs `ls`,
# or looks at a directory listing.
#
# The controls that make a probe result evidence rather than noise -- a package
# every milestone has, and a path no milestone has -- stay in the gate group
# build-clean, which also proves the whole tree compiles.
#
# SCOPE-BUILD: go.mod/go.sum are benchmark-owned; this group never edits them.
set -u

N="$CT_VERIFY_NONCE"
ex=0
fa=0
pass() { ex=$((ex + 1)); printf '%s CASE %s: PASS\n' "$N" "$1"; }
fail() { ex=$((ex + 1)); fa=$((fa + 1)); printf '%s CASE %s: FAIL %s\n' "$N" "$1" "$2"; }

MOD="$CT_SCRATCH/mod"
mkdir -p "$MOD"
cp -RL "$CT_ASSETS/verifier/." "$MOD/" 2>/dev/null || cp -R "$CT_ASSETS/verifier/." "$MOD/"
rm -rf "$MOD/g"
mkdir -p "$MOD/probe"

CTGO=github.com/google/certificate-transparency-go

# build_probe <case-id> <import-path-suffix> <body-line> <want: ok|fail>
# Writes a one-file main package that imports the path and uses an exported
# name from it, so the import cannot be elided, then builds it.
build_probe() {
    local id="$1" sub="$2" body="$3" want="$4" log="$CT_SCRATCH/$1.log"
    cat > "$MOD/probe/main.go" <<EOF
package main

import p "$CTGO/$sub"

func main() {
$body
}
EOF
    ( cd "$MOD" && timeout 600 go build -o "$CT_SCRATCH/probe.bin" ./probe ) > "$log" 2>&1
    local rc=$?
    rm -f "$CT_SCRATCH/probe.bin"
    if [ "$want" = ok ]; then
        if [ "$rc" -eq 0 ]; then
            pass "$id"
        else
            fail "$id" "importing $CTGO/$sub did not build: $(tr '\n' ' ' < "$log" | cut -c1-240)"
        fi
        return
    fi
    if [ "$rc" -eq 0 ]; then
        fail "$id" "importing $CTGO/$sub built, but no milestone has that package"
    elif grep -qF "$CTGO/$sub" "$log"; then
        pass "$id"
    else
        fail "$id" "build failed without naming $CTGO/$sub: $(tr '\n' ' ' < "$log" | cut -c1-240)"
    fi
}

# --- the five packages v1.1.0 adds ------------------------------------------
build_probe new-package-ctpolicy ctpolicy \
    '	var v p.CTPolicy
	_ = v
	_ = p.ChromeCTPolicy{}
	_ = p.AppleCTPolicy{}' ok

build_probe new-package-submission submission \
    '	var v p.Submitter
	_ = v
	_ = p.AssignedSCT{}' ok

build_probe new-package-schedule schedule \
    '	var f = p.Every
	_ = f' ok

build_probe new-package-watcher watcher \
    '	var f = p.NewDiff
	_ = f
	_ = p.DiffEvent{}' ok

build_probe new-package-loglist2 loglist2 \
    '	var v p.LogRoots
	_ = v
	_ = p.LogList{}' ok

printf '%s SUMMARY executed=%d failed=%d\n' "$N" "$ex" "$fa"
exit 0
