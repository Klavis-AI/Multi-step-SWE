#!/usr/bin/env bash
# witness-package-removed
#
# v1.3.3 deletes the whole internal/witness subtree that v1.1.3 introduced and
# v1.2.0 still carried: the API types, the HTTP client, the verifier, the
# witness/feeder/client commands and the omniwitness deployment. A structural
# removal is never decided with ls or by reading a source file; it is observed
# as builds that fail *because those packages are gone*, and only after two
# positive controls establish that a build which should succeed does succeed.
#
# No candidate binary runs here. Both probes and both controls are
# benchmark-owned source compiled inside the throwaway ctverify module, whose
# replace directive already points the product at the candidate's tree.
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
wapi='github.com/google/certificate-transparency-go/internal/witness/api'
wcmd='github.com/google/certificate-transparency-go/internal/witness/cmd/witness'
wcli='github.com/google/certificate-transparency-go/internal/witness/client/http'
wver='github.com/google/certificate-transparency-go/internal/witness/verifier'

# --- Control 1: an ordinary import of a package this milestone still has ---
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
	pass control-surviving-package-builds
else
	fail control-surviving-package-builds "loglist3 probe did not build: $(tr '\n' ' ' < "$CT_SCRATCH/control.out" | cut -c1-400)"
	finish
fi

# --- Control 2: naming a product package by import path resolves ----------
# Without this the probes below could "pass" merely because `go build <path>`
# never works against the replaced module.
if ( cd "$mod" && timeout 600 go build github.com/google/certificate-transparency-go/x509util ) > "$CT_SCRATCH/control2.out" 2>&1; then
	pass control-product-package-is-a-build-target
else
	fail control-product-package-is-a-build-target "x509util is not a build target: $(tr '\n' ' ' < "$CT_SCRATCH/control2.out" | cut -c1-400)"
	finish
fi

# --- Probe 1: the witness command is gone ---------------------------------
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/w.bin" "$wcmd" ) > "$CT_SCRATCH/wcmd.out" 2>&1; then
	fail witness-command-removed "$wcmd is still a buildable command"
elif grep -q "$gone $wcmd\$" "$CT_SCRATCH/wcmd.out"; then
	pass witness-command-removed
else
	fail witness-command-removed "build failed without naming $wcmd: $(tr '\n' ' ' < "$CT_SCRATCH/wcmd.out" | cut -c1-400)"
fi

# --- Probe 2: the witness HTTP client library is gone ---------------------
if ( cd "$mod" && timeout 600 go build "$wcli" ) > "$CT_SCRATCH/wcli.out" 2>&1; then
	fail witness-http-client-removed "$wcli is still a buildable package"
elif grep -q "$gone $wcli\$" "$CT_SCRATCH/wcli.out"; then
	pass witness-http-client-removed
else
	fail witness-http-client-removed "build failed without naming $wcli: $(tr '\n' ' ' < "$CT_SCRATCH/wcli.out" | cut -c1-400)"
fi

# --- Probe 3: the witness note verifier is gone ---------------------------
if ( cd "$mod" && timeout 600 go build "$wver" ) > "$CT_SCRATCH/wver.out" 2>&1; then
	fail witness-verifier-removed "$wver is still a buildable package"
elif grep -q "$gone $wver\$" "$CT_SCRATCH/wver.out"; then
	pass witness-verifier-removed
else
	fail witness-verifier-removed "build failed without naming $wver: $(tr '\n' ' ' < "$CT_SCRATCH/wver.out" | cut -c1-400)"
fi

# --- Probe 4: the API package is gone --------------------------------------
if ( cd "$mod" && timeout 600 go build "$wapi" ) > "$CT_SCRATCH/wapi.out" 2>&1; then
	fail witness-api-removed "$wapi is still a buildable package"
elif grep -q "$gone $wapi\$" "$CT_SCRATCH/wapi.out"; then
	pass witness-api-removed
else
	fail witness-api-removed "build failed without naming $wapi: $(tr '\n' ' ' < "$CT_SCRATCH/wapi.out" | cut -c1-400)"
fi

finish
