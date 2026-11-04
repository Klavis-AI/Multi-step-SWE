#!/usr/bin/env bash
# Group ctfe-gettreehead-withdrawn.
#
# v1.0.21 withdraws the free function ctfe.GetTreeHead. What used to be "fetch
# a tree head" split in two: the STHGetter interface took over serving, and
# what is left of the free function is ctfe.PingTreeHead, which only refreshes
# the internal view and returns an error (CLIENT-API.md section 8.3).
#
# A removal cannot be graded from a driver that imports the removed name: the
# harness scores "driver did not compile" as a group failure, so the group
# would fail exactly when the candidate is right. It is graded here as
# AUTHORING_BRIEF.md prescribes -- a build probe with a positive control
# first, and never with ls or by reading a source file.
#
#   1. control  : a probe that uses names ctfe has at every anchor must build,
#                 so a broken module graph fails loudly instead of being
#                 mistaken for a withdrawal.
#   2. negative : a probe that uses a name no anchor has must fail with that
#                 name in the message, so "it did not build" is evidence.
#   3. probe    : a probe that uses ctfe.GetTreeHead must fail, naming it.
#   4. arrival  : a probe that uses ctfe.PingTreeHead with its v1.0.21
#                 signature must build.
#
# At v1.0.19 GetTreeHead is present and PingTreeHead does not exist, so two of
# the four cases below invert against that tree.
#
# SCOPE-BUILD: go.mod/go.sum are benchmark-owned; this group copies the
# verifier module and never edits them.
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

# symbol_probe <case-id> <body> <want: ok|fail> [text that must appear]
symbol_probe() {
    local id="$1" body="$2" want="$3" want_text="${4:-}" log="$CT_SCRATCH/$1.log"
    cat > "$MOD/probe/main.go" <<EOF
package main

import (
	"context"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/trillian"
)

var _ = context.Background
var _ trillian.TrillianLogClient

func main() {
$body
}
EOF
    # 120 s each, not the group's whole 600 s ceiling: four probes run here,
    # so a per-probe timeout equal to the outer one could never fire before it
    # and a single slow build would cost the group rather than one case. The
    # deadline bounds waiting and is never an assertion -- a cold-cache build
    # of this probe measures about 30 s on four cores.
    ( cd "$MOD" && timeout 120 go build -o "$CT_SCRATCH/probe.bin" ./probe ) > "$log" 2>&1
    local rc=$?
    rm -f "$CT_SCRATCH/probe.bin"
    if [ "$want" = ok ]; then
        if [ "$rc" -eq 0 ]; then
            pass "$id"
        else
            fail "$id" "probe did not build: $(tr '\n' ' ' < "$log" | cut -c1-240)"
        fi
        return
    fi
    if [ "$rc" -eq 0 ]; then
        fail "$id" "probe built, but this release must not have that symbol"
    elif grep -qF "$want_text" "$log"; then
        pass "$id"
    else
        fail "$id" "build failed without naming $want_text: $(tr '\n' ' ' < "$log" | cut -c1-240)"
    fi
}

# --- 1. positive control: the package and the module graph are usable -------
symbol_probe control-ctfe-package-usable \
'	_ = ctfe.NewPEMCertPool()
	var h ctfe.PathHandlers
	_ = h' ok

# --- 2. negative control: "it did not build" is not true of everything ------
symbol_probe negative-control-bogus-symbol-fails \
'	ctfe.NoSuchFunctionAtAnyAnchor()' fail 'ctfe.NoSuchFunctionAtAnyAnchor'

# --- 3. the withdrawal ------------------------------------------------------
symbol_probe gettreehead-withdrawn \
'	_ = ctfe.GetTreeHead' fail 'ctfe.GetTreeHead'

# --- 4. what replaced it ----------------------------------------------------
symbol_probe pingtreehead-present \
'	var f func(context.Context, trillian.TrillianLogClient, int64, string) error = ctfe.PingTreeHead
	_ = f' ok

printf '%s SUMMARY executed=%d failed=%d\n' "$N" "$ex" "$fa"
exit 0
