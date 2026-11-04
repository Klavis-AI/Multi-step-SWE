#!/usr/bin/env bash
# Group build-clean.
#
# GATE: proof of life. It earns nothing and only vetoes, so it asserts only
# binaries, flags and library surface that already exist at the predecessor
# v1.1.0 and are still there at v1.1.2. Every case below therefore passes
# against both trees, and fails only when the candidate's tree does not build
# or its artifacts do not run.
#
# The fifteen commands in product scope at this milestone must build and be
# runnable, and each must answer --help with its own usage.
#
# Two library survivals are asked the same way, because a removal milestone is
# only half stated by what went: gossip/minimal/x509ext is the one part of
# gossip/ that stays (RELEASE-MAP.md §9, CLIENT-API.md §6.2), and the add-json
# request type and path constant outlive the client method that used them
# (CLIENT-API.md §4.1, HTTP-API.md §1). With client.New and the loglist2
# package they are the positive controls: they must still build.
#
# What this release *withdraws* -- the three gossip commands, AddJSON,
# trillian/util.FixedBackendResolver -- and the log-list URLs it re-points at
# v2 were graded here until the 2026-09-21 re-tier. They are this step's new
# capability, so a gate case asserting them would make the predecessor tree
# fail the gate; they moved, verbatim, to the scored group
# `release-surface-withdrawals`.
#
# Observed at the process boundary only: which names the prerequisite build
# produced in $CT_BIN, what each one does when asked for its usage, and what a
# benchmark-owned program compiled against the candidate's tree does or does
# not get to link.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

# The fifteen package main directories in product scope at this milestone.
KEPT="certcheck chainfix crlcheck ct_hammer ct_server ctclient dumpscts findlog
      hammer migrillian preloader scanlog sctcheck sctscan server"

# --- 1. every surviving command was built and is runnable ------------------
missing=""
for b in $KEPT; do
    if [ ! -f "$CT_BIN/$b" ] || [ ! -x "$CT_BIN/$b" ]; then
        missing="$missing $b"
    fi
done
if [ -n "$missing" ]; then
    fail product-binaries-present "not built or not executable:$missing"
else
    pass product-binaries-present
fi

# --- 2. each flag-parsing command prints its own usage and exits 0 ---------
# chainfix parses its positional arguments before the flag package runs, so it
# is checked separately below.
bad=""
for b in $KEPT; do
    [ "$b" = chainfix ] && continue
    [ -x "$CT_BIN/$b" ] || { bad="$bad $b:not-built"; continue; }
    out="$CT_SCRATCH/usage-$b.txt"
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/$b" --help > "$out" 2>&1 < /dev/null
    rc=$?
    want="Usage of $CT_BIN/$b:"
    got=$(head -n 1 "$out")
    if [ "$rc" -ne 0 ]; then
        bad="$bad $b:rc=$rc"
    elif [ "$got" != "$want" ]; then
        bad="$bad $b:first-line-mismatch"
    fi
done
if [ -n "$bad" ]; then
    fail binaries-report-their-own-usage "$bad"
else
    pass binaries-report-their-own-usage
fi

# --- 3. chainfix reads argv before the flag package and aborts -------------
if [ -x "$CT_BIN/chainfix" ]; then
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/chainfix" --help > "$CT_SCRATCH/usage-chainfix.txt" 2>&1 < /dev/null
    rc=$?
    if [ "$rc" -eq 2 ]; then
        pass chainfix-aborts-without-its-positional-arguments
    else
        fail chainfix-aborts-without-its-positional-arguments "exit status $rc, want 2"
    fi
else
    fail chainfix-aborts-without-its-positional-arguments "chainfix was not built"
fi

# --- 4. the library surface, built rather than read ------------------------
# One control program exercises both survivors: client.New must still make a
# LogClient, and loglist2's three constants must still be readable. What those
# constants *say* changes at this milestone and is graded in the scored tier;
# here only the build is asserted, which holds at both anchors.
export GOFLAGS=-mod=mod GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4
mod="$CT_SCRATCH/mod"
mkdir -p "$mod"
cp -R "$CT_ASSETS/verifier/." "$mod/" 2>/dev/null || true

mkdir -p "$mod/probe_surface_control"
cat > "$mod/probe_surface_control/main.go" <<'GO'
package main

import (
	"fmt"
	"net/http"

	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/jsonclient"
	"github.com/google/certificate-transparency-go/loglist2"
)

func main() {
	lc, err := client.New("http://ct.example.invalid", &http.Client{}, jsonclient.Options{})
	if err != nil || lc == nil {
		fmt.Println("client.New failed:", err)
		return
	}
	fmt.Println(loglist2.LogListURL)
	fmt.Println(loglist2.LogListSignatureURL)
	fmt.Println(loglist2.AllLogListURL)
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/surface.bin" ./probe_surface_control ) \
        > "$CT_SCRATCH/surface-build.out" 2>&1; then
    pass control-client-and-loglist2-build
else
    fail control-client-and-loglist2-build \
        "surviving client/loglist2 surface did not build: $(tr '\n' ' ' < "$CT_SCRATCH/surface-build.out" | cut -c1-400)"
fi

# --- 5. gossip/minimal/x509ext survives the collapse -----------------------
# Everything else under gossip/ leaves at this milestone; this one package
# stays and stays importable. A program naming the two OIDs, the LogSTHInfo
# struct and the three functions CLIENT-API.md §6.2 lists must build.
mkdir -p "$mod/probe_x509ext"
cat > "$mod/probe_x509ext/main.go" <<'GO'
package main

import (
	"fmt"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/gossip/minimal/x509ext"
	"github.com/google/certificate-transparency-go/x509"
)

// The three entry points of CLIENT-API.md 6.2, pinned by signature. Nothing
// is called: the question is whether the package survived, not what it does.
var (
	_ func(*x509.Certificate) (*x509ext.LogSTHInfo, error) = x509ext.LogSTHInfoFromCert
	_ func(*x509.Certificate) bool                         = x509ext.HasSTHInfo
	_ func(*x509.Certificate) (*ct.SignedTreeHead, error)  = x509ext.STHFromCert
)

func main() {
	var info x509ext.LogSTHInfo
	fmt.Println(x509ext.OIDExtensionCTSTH, x509ext.OIDExtKeyUsageCTMinimalGossip, info.TreeSize)
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/x509ext.bin" ./probe_x509ext ) \
        > "$CT_SCRATCH/x509ext.out" 2>&1; then
    pass gossip-minimal-x509ext-survives
else
    fail gossip-minimal-x509ext-survives \
        "the surviving gossip package did not build: $(tr '\n' ' ' < "$CT_SCRATCH/x509ext.out" | cut -c1-400)"
fi

# --- 6. the add-json request type and path outlive LogClient.AddJSON ------
# `release-surface-withdrawals` requires the method to be gone. The type it
# used and the path constant beside it are declared at every release and must
# not go with it, at this anchor or at the predecessor.
mkdir -p "$mod/probe_addjson_types"
cat > "$mod/probe_addjson_types/main.go" <<'GO'
package main

import (
	"fmt"

	ct "github.com/google/certificate-transparency-go"
)

func main() {
	req := ct.AddJSONRequest{Data: struct{}{}}
	fmt.Println(ct.AddJSONPath, req.Data)
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/addjsontypes.bin" ./probe_addjson_types ) \
        > "$CT_SCRATCH/addjsontypes.out" 2>&1; then
    pass add-json-request-type-and-path-survive
else
    fail add-json-request-type-and-path-survive \
        "AddJSONRequest/AddJSONPath did not build: $(tr '\n' ' ' < "$CT_SCRATCH/addjsontypes.out" | cut -c1-400)"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
