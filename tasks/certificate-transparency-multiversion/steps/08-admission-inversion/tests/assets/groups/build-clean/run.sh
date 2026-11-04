#!/usr/bin/env bash
# Group build-clean. KIND=gate: proof of life. It earns nothing and only vetoes.
#
# The whole product tree compiles, and the module graph the drivers link
# through resolves. Nothing here asserts a package, a binary or a flag that
# v1.0.21 did not already have, so every case passes against the previous
# milestone's tree as well as this one -- which is what a gate group must do.
#
# Observed only by building: two control probes inside the throwaway ctverify
# module, whose `replace` points the product at the candidate's own tree, plus
# a build of every package in that tree. Nothing here reads a candidate source
# file, runs `ls`, or looks at a directory listing.
#
# Two controls make the tree build result evidence rather than noise:
#   - a positive control that imports a package every milestone has, so a
#     broken module graph fails loudly instead of passing silently;
#   - a negative control that imports a path no milestone has, which must fail
#     with that path named, so "it built" cannot be true for everything.
#
# The five packages v1.1.0 adds -- ctpolicy/, submission/, schedule/, watcher/
# and loglist2/ -- were probed here too. They are this milestone's new
# capability, not proof of life, so they moved to the scored group
# new-packages-compile.
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

# --- positive control: the module graph resolves at all ---------------------
build_probe control-existing-package-compiles tls \
    '	_, _ = p.Marshal(uint8(1))' ok

# --- negative control: "it built" is not true of everything -----------------
build_probe negative-control-bogus-import-fails no-such-package-at-any-anchor \
    '	_ = p.Nothing' fail

# --- every package in the candidate's own tree compiles ---------------------
# The tree is located through the module graph, never by a hard-coded path.
tree=$( cd "$MOD" && go list -m -f '{{with .Replace}}{{.Dir}}{{end}}' "$CTGO" 2>/dev/null )
if [ -z "$tree" ] || [ ! -d "$tree" ]; then
    fail product-tree-builds "could not locate the product tree through the module graph"
else
    mkdir -p "$CT_SCRATCH/treebin"
    ( cd "$tree" && timeout 1200 go build -o "$CT_SCRATCH/treebin/" ./... ) \
        > "$CT_SCRATCH/tree.log" 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        pass product-tree-builds
    else
        # Prefer the toolchain's own error lines over a plain head of the log.
        # A head is useless whenever cgo is in the dependency graph: building
        # this tree pulls mattn/go-sqlite3, whose C compiler emits hundreds of
        # warning lines before the first Go error, and they consume the entire
        # character budget -- the reported diagnostic ends up naming nothing
        # but sqlite3-binding.c. Measured against the step-07 tree, where the
        # 240-character message was 100% C warnings. Fall back to the head so
        # an unrecognised failure shape still reports something.
        diag=$( grep -E '\.go:[0-9]+:|cannot find module|no required module|^go: ' "$CT_SCRATCH/tree.log" \
                    | grep -vE 'warning:|\[-W' | head -20 )
        [ -n "$diag" ] || diag=$( cat "$CT_SCRATCH/tree.log" )
        fail product-tree-builds "go build ./... exited $rc: $(printf '%s\n' "$diag" | tr '\n' ' ' | cut -c1-400)"
    fi
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$N" "$ex" "$fa"
exit 0
