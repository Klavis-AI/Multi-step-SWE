#!/bin/bash
# build-clean
#
# The milestone has to be a clean build target, not just a tree that happens
# to hold the right files. Everything below is done from the throwaway
# ctverify module, whose "replace" already points the product at the
# candidate's tree, so nothing is written into that tree.
#
# Nothing here reads the candidate's source: every verdict is the toolchain's
# answer to a build, or the output of a binary that was built.
#
# This is a GATE group. It earns nothing and only vetoes, so it asks only
# questions a v1.1.3 tree already answers the same way: the module graph is
# intact, every package in the product compiles, and loglist2 -- which this
# release does *not* delete -- is still a build target. Every verdict is the
# toolchain's, so none of it can depend on a clock, a port, a host or an
# iteration order.
#
# It used to carry two further cases, v3-consumer-compiles-against-the-product
# and v3-consumer-runs, which built a single file naming the v1.1.4 signatures
# of ctpolicy, submission and ctutil at once. That is this step's own new
# capability and cannot hold at v1.1.3, so it was a rule-3 migration sitting in
# the gate: it made the Oracle fail its own step, and left a no-op continuation
# reporting a failed gate rather than a zero score. Both cases were moved
# verbatim into the scored group loglist-v2-survives-with-no-consumers, whose
# subject is exactly which log-list version each consumer speaks.
set -u
n=0; f=0
pass() { printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; n=$((n+1)); }
fail() { printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; n=$((n+1)); f=$((f+1)); }

export GOFLAGS="-mod=mod"
export GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4

MOD="$CT_SCRATCH/mod"
mkdir -p "$MOD"
cp -a "$CT_ASSETS/verifier/." "$MOD/"

mkdir -p "$MOD/probe/control" "$MOD/probe/loglist2"
cat > "$MOD/probe/control/main.go" <<'EOF'
package main

import (
	"fmt"

	ct "github.com/google/certificate-transparency-go"
)

func main() { fmt.Println(ct.V1) }
EOF

cat > "$MOD/probe/loglist2/main.go" <<'EOF'
package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/loglist2"
)

func main() {
	ll, err := loglist2.NewFromJSON([]byte(`{"operators":[]}`))
	if err != nil {
		fmt.Println("parse", err)
		return
	}
	fmt.Println(len(ll.Operators), err)
}
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

# --- case 3: the v2 package survives this release, still compiles, and still
# works. It has no consumer left in the product -- that is asserted by its own
# group -- but it is still published, so a driver written against it must
# build, run, and get an answer out of it. The probe parses a v2 document with
# an empty operator list and prints the operator count and the error, which
# must be exactly `0 <nil>` (TEST_INTERFACE section 6.1 item 12). This is the
# positive half of the pair; the negative half is the v1 import probe in
# loglist-v1-package-removed.
if ( cd "$MOD" && go build -p 4 -o "$CT_SCRATCH/v2probe.bin" ./probe/loglist2 ) \
     > "$CT_SCRATCH/v2.log" 2>&1; then
  v2out=$(timeout 60 env -u CT_VERIFY_NONCE "$CT_SCRATCH/v2probe.bin" 2>&1)
  if [ "$v2out" = '0 <nil>' ]; then
    pass loglist-v2-package-still-compiles
  else
    fail loglist-v2-package-still-compiles "v2 probe printed [$v2out], want [0 <nil>]"
  fi
else
  fail loglist-v2-package-still-compiles "$(tr '\n' '|' < "$CT_SCRATCH/v2.log" | cut -c1-600)"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
