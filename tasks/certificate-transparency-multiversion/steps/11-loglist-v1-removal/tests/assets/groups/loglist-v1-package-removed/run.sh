#!/usr/bin/env bash
# loglist-v1-package-removed
#
# v1.1.4 deletes the v1 log-list package outright (loglist/loglist.go,
# logfilter.go, diff_check.go and the bundled chrome-list-pubkey.pem). A
# structural removal is never decided with ls or by reading a source file: it
# is observed as a build that fails *because the package is gone*, with a
# positive control first so the group cannot pass for any other reason a build
# can fail.
#
# Nothing here runs a candidate binary; the two probes are benchmark-owned
# source compiled inside the throwaway ctverify module, whose replace directive
# already points the product at the candidate's tree.
set -u

n=0; f=0
pass() { printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; n=$((n + 1)); }
fail() { printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; n=$((n + 1)); f=$((f + 1)); }
finish() { printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"; exit 0; }

export GOFLAGS=-mod=mod GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4

mod="$CT_SCRATCH/mod"
mkdir -p "$mod"
cp -R "$CT_ASSETS/verifier/." "$mod/" 2>/dev/null || true

gone='does not contain package'
v1='github.com/google/certificate-transparency-go/loglist'

# --- Control -------------------------------------------------------------
# A package this milestone still has. If this will not build the module graph
# is broken and every probe below would fail for the wrong reason.
mkdir -p "$mod/probe_control"
cat > "$mod/probe_control/main.go" <<'GO'
package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/loglist3"
)

func main() {
	var ll loglist3.LogList
	fmt.Println(len(ll.Operators), loglist3.LogListURL)
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/control.bin" ./probe_control ) > "$CT_SCRATCH/control.out" 2>&1; then
	pass control-successor-package-builds
else
	fail control-successor-package-builds "loglist3 probe did not build: $(tr '\n' ' ' < "$CT_SCRATCH/control.out" | cut -c1-400)"
	finish
fi

# --- Probe 1: the import path no longer resolves -------------------------
mkdir -p "$mod/probe_v1"
cat > "$mod/probe_v1/main.go" <<'GO'
package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/loglist"
)

func main() {
	var ll loglist.LogList
	fmt.Println(len(ll.Logs))
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/v1.bin" ./probe_v1 ) > "$CT_SCRATCH/v1.out" 2>&1; then
	fail loglist-v1-import-unresolved "a program importing $v1 still builds"
elif grep -q "$gone $v1\$" "$CT_SCRATCH/v1.out"; then
	pass loglist-v1-import-unresolved
else
	fail loglist-v1-import-unresolved "build failed without naming $v1: $(tr '\n' ' ' < "$CT_SCRATCH/v1.out" | cut -c1-400)"
fi

# --- Probe 2: the package is not a build target either --------------------
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/v1pkg.bin" "$v1" ) > "$CT_SCRATCH/v1pkg.out" 2>&1; then
	fail loglist-v1-not-a-build-target "$v1 is still a buildable package in the product module"
elif grep -q "$gone $v1\$" "$CT_SCRATCH/v1pkg.out"; then
	pass loglist-v1-not-a-build-target
else
	fail loglist-v1-not-a-build-target "build failed without naming $v1: $(tr '\n' ' ' < "$CT_SCRATCH/v1pkg.out" | cut -c1-400)"
fi

finish
