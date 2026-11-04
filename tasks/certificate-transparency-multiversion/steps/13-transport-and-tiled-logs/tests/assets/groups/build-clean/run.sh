#!/bin/bash
# build-clean
#
# The milestone has to be a clean build target, not just a tree that happens
# to hold the right files. Everything below is done from the throwaway
# ctverify module, whose "replace" already points the product at the
# candidate's tree, so nothing is written into that tree.
#
# This is a GATE group. Cases 1 and 3 are pure regression: the module graph
# resolves, and the non-test sources of the packages this release rewrites are
# vet-clean -- both verified to hold at v1.2.0 as well as at v1.3.3, which is
# what makes them regression rather than new obligations.
#
# Case 2 is not pure regression, and the documents say so rather than pretend
# otherwise. v1.3.3's pinned manifest -- the go.mod and go.sum shipped in the
# workspace, which the candidate may not edit -- no longer provides
# github.com/gorilla/mux or github.com/mattn/go-sqlite3. The only packages that
# imported them were under internal/witness. So on a tree that still carries
# internal/witness, "go build ./..." cannot resolve those two imports and the
# whole-tree build fails. Deleting internal/witness is therefore a gate
# condition at this step as well as a scored one (witness-package-removed
# grades the deletion's externally visible consequences). The failure is not
# satisfiable by the deletion alone either -- every inherited package still has
# to compile -- but it is not satisfiable without it, and a candidate who does
# nothing gets gate=0, not gate=1. TEST_INTERFACE section 3.5 states this.
#
# The consequence is load-bearing for the other gate groups and is the reason
# case 2 stays whole-tree instead of being narrowed to a package list: the
# harness builds the candidate's binaries with "go build -o $W/bin/ ./...",
# and that command writes NO binaries at all when any package in the pattern
# has an unresolvable import. $CT_BIN is then empty, so gate-endtoend and
# inherited-ctfe-endpoint-contract have no server to run and report
# executed=0. Narrowing this group would hide the cause without removing the
# veto.
#
# The v1.3.3 API surface itself is graded by the scored groups that own it:
# the six tiled-log finders and AllLogListSignatureURL by
# loglist3-tiled-log-finders, Operator.TiledLogs and the TiledLog fields by
# loglist3-tiled-log-filters, jsonclient.Options.Authorization by
# jsonclient-authorization-header, postgresql.NewIssuanceChainStorage and its
# storage.IssuanceChainStorage conformance by
# ctfe-postgresql-issuance-chain-config, and ct.LogV3JSONPath /
# ctfe.LogV3JSONName by ctfe-logv3-json-endpoint. Each of those drivers names
# those symbols directly, so each already carries the compile evidence this
# group used to duplicate.
set -u
n=0; f=0
pass() { printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; n=$((n+1)); }
fail() { printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; n=$((n+1)); f=$((f+1)); }

export GOFLAGS="-mod=mod"
export GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4

MOD="$CT_SCRATCH/mod"
mkdir -p "$MOD"
cp -a "$CT_ASSETS/verifier/." "$MOD/"

mkdir -p "$MOD/probe/control"
cat > "$MOD/probe/control/main.go" <<'EOF'
package main

import (
	"fmt"

	ct "github.com/google/certificate-transparency-go"
)

func main() { fmt.Println(ct.V1) }
EOF

# --- case 1: control. The module graph is intact and the product's root
# package is reachable through the replace directive.
if ( cd "$MOD" && go build -p 4 -o "$CT_SCRATCH/control.bin" ./probe/control ) \
     > "$CT_SCRATCH/control.log" 2>&1; then
  pass control-product-root-package-builds
else
  fail control-product-root-package-builds "$(tr '\n' '|' < "$CT_SCRATCH/control.log" | cut -c1-400)"
  printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
  exit 0
fi

# --- case 2: every package in the product compiles.
if ( cd "$MOD" && go build -p 4 github.com/google/certificate-transparency-go/... ) \
     > "$CT_SCRATCH/all.log" 2>&1; then
  pass whole-product-compiles
else
  fail whole-product-compiles "$(tr '\n' '|' < "$CT_SCRATCH/all.log" | cut -c1-600)"
fi

# --- case 3: the packages this milestone rewrote pass go vet.
#
# Vet is run over each package's non-test files, enumerated by "go list",
# rather than over the package pattern. "go vet <pattern>" also analyses
# *_test.go, and vetting inherited test files would put an obligation in the
# GATE that no document states: TEST_INTERFACE section 1's build obligation is
# "go build ./...", which does not compile test files at all, and nothing in
# the corpus asks the candidate to touch one. Concretely, v1.2.0's
# jsonclient/client_test.go has four printf findings
# ("non-constant format string in call to (*testing.common).Fatalf") that
# upstream happens to have cleaned up by v1.3.3 -- so the pattern form of this
# case would veto an otherwise perfect submission for leaving an inherited
# test file alone. Over non-test files the case is clean at v1.2.0 and at
# v1.3.3, which is what a regression case has to be.
vet_pkgs="github.com/google/certificate-transparency-go/loglist3
github.com/google/certificate-transparency-go/jsonclient
github.com/google/certificate-transparency-go/x509
github.com/google/certificate-transparency-go/trillian/ctfe
github.com/google/certificate-transparency-go/trillian/ctfe/storage"
: > "$CT_SCRATCH/vet.log"
vet_bad=''
for vp in $vet_pkgs; do
  vf=$( cd "$MOD" && go list -f '{{$d:=.Dir}}{{range .GoFiles}}{{$d}}/{{.}} {{end}}' \
          "$vp" 2>> "$CT_SCRATCH/vet.log" )
  if [ -z "$vf" ]; then
    printf 'no non-test Go files listed for %s\n' "$vp" >> "$CT_SCRATCH/vet.log"
    vet_bad="$vet_bad $vp"
    continue
  fi
  # shellcheck disable=SC2086
  ( cd "$MOD" && go vet -p 4 $vf ) >> "$CT_SCRATCH/vet.log" 2>&1 || vet_bad="$vet_bad $vp"
done
if [ -z "$vet_bad" ]; then
  pass changed-packages-vet-clean
else
  fail changed-packages-vet-clean "$(printf '%s |' "$vet_bad"; tr '\n' '|' < "$CT_SCRATCH/vet.log" | cut -c1-500)"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
