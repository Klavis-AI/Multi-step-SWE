#!/usr/bin/env bash
# Group merkletree-package-withdrawn.
#
# v1.0.17 deletes merkletree/ outright (5 files, commit 3c3d22ce "Use Merkle
# Tree verification from Trillian") and takes inclusion/consistency proof
# verification from the backend library instead. Both halves are observed here,
# and neither is observed with ls: a package that is gone is a package a probe
# cannot import.
#
# Order matters, and is the one AUTHORING_BRIEF's "Scoring a removal" fixes:
#   1. control  -- a probe importing a package the milestone still has must
#                  build. If it does not, the module graph is broken and every
#                  later case is meaningless, so the group fails here.
#   2. probe    -- a probe importing .../merkletree must fail to build, and the
#                  failure text must name that exact import path. A build that
#                  failed for any other reason is not evidence of a removal.
#   3. consumer -- a probe importing the *candidate's* own ctutil, and naming
#                  its inclusion-proof entry point at full signature, must
#                  build. ctutil is one of the three packages that imported
#                  .../merkletree at v1.0.13, so this is what separates
#                  "deleted and repointed" from "deleted and left dangling":
#                  it links candidate code, and a tree that removed the package
#                  without rehoming its consumers fails here.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

GONE=github.com/google/certificate-transparency-go/merkletree

mod="$CT_SCRATCH/mod"
cp -r "$CT_ASSETS/verifier" "$mod" 2>/dev/null || {
    for id in control-probe-builds merkletree-probe-does-not-build \
              merkletree-named-in-build-error proof-verifying-consumer-still-builds; do
        fail "$id" "could not copy \$CT_ASSETS/verifier"
    done
    printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
    exit 0
}
chmod -R u+w "$mod"
export GOFLAGS=-mod=mod GOPROXY=off GOSUMDB=off GOWORK=off GOMAXPROCS=4

# build <dirname> -> writes $mod/<dirname>.log, returns go build's status.
build() {
    ( cd "$mod" && timeout 600 go build -p 4 -o /dev/null "./$1" ) > "$mod/$1.log" 2>&1
}

# --- 1. control: a package this milestone still has -------------------------
mkdir -p "$mod/probe_control"
cat > "$mod/probe_control/main.go" <<'EOF'
package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/tls"
)

func main() {
	b, err := tls.Marshal(uint16(0x0102))
	fmt.Println(b, err)
}
EOF
if build probe_control; then
    pass control-probe-builds
else
    fail control-probe-builds "the surviving-package control does not build; module graph broken: $(tr '\n' ' ' < "$mod/probe_control.log" | cut -c1-400)"
    # Without a working control every later case is vacuous, so stop here and
    # let the executed-count assertion fail the group.
    printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
    exit 0
fi

# --- 2 and 3. the deleted package ------------------------------------------
mkdir -p "$mod/probe_merkletree"
cat > "$mod/probe_merkletree/main.go" <<EOF
package main

import (
	"crypto/sha256"
	"fmt"

	"$GONE"
)

func main() {
	v := merkletree.NewMerkleVerifier(func(data []byte) []byte {
		h := sha256.Sum256(data)
		return h[:]
	})
	fmt.Println(v)
}
EOF
if build probe_merkletree; then
    fail merkletree-probe-does-not-build "a probe importing $GONE still builds"
    fail merkletree-named-in-build-error "no build error to inspect"
else
    pass merkletree-probe-does-not-build
    if grep -q -- "$GONE" "$mod/probe_merkletree.log"; then
        pass merkletree-named-in-build-error
    else
        fail merkletree-named-in-build-error "build failed but never named $GONE: $(tr '\n' ' ' < "$mod/probe_merkletree.log" | cut -c1-400)"
    fi
fi

# --- 4. the consumer that used to import it -------------------------------
# Nothing here names a replacement library: whichever verifier the candidate
# rehomed ctutil onto is its own business. What is asserted is that the
# consumer still compiles and still offers the same inclusion entry point, at
# the same signature -- the capability survived the deletion.
mkdir -p "$mod/probe_consumer"
cat > "$mod/probe_consumer/main.go" <<'EOF'
package main

import (
	"context"
	"fmt"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
)

// Spelled out at full signature, so the probe fails if the entry point changes
// shape and not only if it disappears.
var verifyInclusionAt func(*ctutil.LogInfo, context.Context, ct.MerkleTreeLeaf, uint64, uint64, []byte) (int64, error) = (*ctutil.LogInfo).VerifyInclusionAt

func main() { fmt.Println(verifyInclusionAt != nil) }
EOF
if build probe_consumer; then
    pass proof-verifying-consumer-still-builds
else
    fail proof-verifying-consumer-still-builds "the candidate's own proof-verifying consumer does not build after the removal: $(tr '\n' ' ' < "$mod/probe_consumer.log" | cut -c1-400)"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
