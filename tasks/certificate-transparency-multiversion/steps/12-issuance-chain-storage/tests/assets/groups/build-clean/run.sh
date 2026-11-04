#!/bin/bash
# build-clean
#
# Proof of life. The milestone has to be a clean build target, not just a tree
# that happens to hold the right files. Everything below is done from the
# throwaway ctverify module, whose "replace" already points the product at the
# candidate's tree, so nothing is written into that tree.
#
# Nothing here reads the candidate's source: every verdict is either the
# toolchain's answer to a build, or the answer a program the prerequisite build
# produced gives when it is executed.
#
# This is a GATE group. It earns nothing and only vetoes, and every case is
# proof of life and nothing more: the module graph resolves, every package in
# the product compiles, and the eighteen programs v1.1.4 shipped were built and
# still run. No case names anything v1.2.0 introduces.
#
# It does NOT follow that the predecessor anchor answers all three the same
# way, and an earlier version of this header claimed it did. It does not.
# Measured against upstream v1.1.4 staged under THIS step's pinned manifest:
# case 1 still passes -- the break is in one package and the product root is
# not it -- but case 2 fails and case 3 reports seventeen of eighteen. grpc
# moves v1.48.0 -> v1.63.2 with this release, which takes the `Type` field off
# `resolver.Address` and removes `resolver.Backend` altogether, so
# trillian/ctfe/ct_server's multiple-backend branch no longer type-checks and
# ct_server is the one program the build does not produce.
#
# So this group starts RED at this milestone and is meant to. TEST_INTERFACE
# section 3.5 states that as contract, and section 7 names it as the third of
# its three shapes: the module is still supplied and the symbol still resolves,
# only its shape has changed, so the gate fails on the very first run before
# the candidate has changed anything. The repair is small and the compiler
# names the file and the line; it is the cost of the pin moving rather than an
# item on the feature list, and until it is made the step scores zero however
# much of the milestone is finished. What the group vetoes after that is the
# regression: a tree that stops building, or that stops producing a runnable
# ct_server, must veto rather than be scored.
#
# Do not "fix" the gate to make it green against v1.1.4. It is correct as
# written -- it is red there for the manifest's reason, not because any case
# reaches for this release's surface, and the surface it deliberately does not
# reach for is listed next.
#
# Deliberately NOT asserted here, because none of it is true at v1.1.4 -- this
# is the defect step 13's build-clean had, where two cases naming the new
# release's surface made the group unpassable on the predecessor and had to be
# removed:
#
#   * trillian/ctfe/storage and trillian/ctfe/cache (with lru/ and noop/) --
#     graded by storage-issuance-chain-interface, cache-factory-validation,
#     cache-lru-issuance-chain and cache-noop-issuance-chain;
#   * ct_server's --cache_type / --cache_size / --cache_ttl -- graded by
#     ctserver-cache-flags, which runs the binary and reads its exit status;
#   * the absence of loglist2 -- graded by loglist2-removed, which owns both
#     halves of that probe (a positive control on loglist3 and an import of the
#     removed path that must fail naming it).
#
# The program list in case 3 is the v1.1.4 set, which is also the v1.2.0 set:
# CLI.md section 1, and this step's own TEST_INTERFACE section 4, both say no
# program arrives at v1.2.0 and none leaves. It is NOT the v1.1.3 set -- step 11
# deleted loglist/findlog, so `findlog` exists at v1.1.3 and at neither anchor
# here, and naming it would make this group fail against both trees. Nor is a
# total asserted: the count moves at v1.3.3 when internal/witness goes, and a
# bare total is step 13's group binaries-fourteen-mains to grade, not a gate's.
#
# Determinism (gate rule 2): the only inputs are two `go build` exit statuses
# and eighteen process exit statuses. No case reads a clock, measures elapsed
# time, sleeps, binds a port, touches the network, iterates a map or a
# directory as if ordered, or looks at the host's CPU count, locale or
# hostname. Case 3 does not assert *which* status a program exits with -- only
# that it was produced by the build, loaded, and terminated on its own -- so
# the one program whose help request is a usage error (chainfix, unchanged
# since v1.0) needs no special case and cannot drift.
set -u
n=0; f=0
pass() { printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; n=$((n+1)); }
fail() { printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; n=$((n+1)); f=$((f+1)); }

export GOFLAGS="-mod=mod"
export GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4

MOD="$CT_SCRATCH/mod"
mkdir -p "$MOD"
cp -a "$CT_ASSETS/verifier/." "$MOD/" 2>/dev/null
# g/ is the shared module's driver-slot directory; another author's step may
# legitimately hold a slot there while this runs. It is no part of this group.
rm -rf "$MOD/g"

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
# package is reachable through the replace directive. If this fails every
# case below would fail for the wrong reason, so the group stops here.
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

# --- case 3: every program carried forward from v1.1.4 was produced by the
# prerequisite build and still runs. "Runs" is the weakest claim that is still
# proof of life: the file is non-empty and executable, the loader accepts it,
# and asking it for its own usage returns control rather than hanging. The
# status it chooses is its own business and is not graded here -- the flag
# grammar belongs to the groups that own each program.
INHERITED="certcheck chainfix client config crlcheck ct_hammer ct_server
           ctclient dumpscts feeder hammer migrillian preloader scanlog
           sctcheck sctscan server witness"
bad=""
for b in $INHERITED; do
  if [ ! -s "$CT_BIN/$b" ] || [ ! -x "$CT_BIN/$b" ]; then
    bad="$bad $b:not-built-or-not-executable"
    continue
  fi
  env -u CT_VERIFY_NONCE timeout 20 "$CT_BIN/$b" --help \
      > "$CT_SCRATCH/usage-$b.txt" 2>&1 < /dev/null
  rc=$?
  case "$rc" in
    124|137) bad="$bad $b:did-not-terminate" ;;
    125)     bad="$bad $b:could-not-be-launched" ;;
    126)     bad="$bad $b:not-executable" ;;
    127)     bad="$bad $b:could-not-be-loaded" ;;
  esac
done
if [ -n "$bad" ]; then
  fail inherited-programs-built-and-runnable "$bad"
else
  pass inherited-programs-built-and-runnable
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
