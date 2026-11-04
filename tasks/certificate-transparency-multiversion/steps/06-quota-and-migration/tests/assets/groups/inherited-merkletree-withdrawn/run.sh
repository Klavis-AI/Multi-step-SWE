#!/usr/bin/env bash
# Group inherited-merkletree-withdrawn.
#
# merkletree/ was deleted at the previous milestone (v1.0.17, commit 3c3d22ce
# "Use Merkle Tree verification from Trillian"), and it stays deleted here:
# v1.0.19 adds a new trillian/util package and a new migrillian command, and
# none of that may bring the withdrawn package back. A removal is not observed
# with ls -- a package that is gone is a package a probe cannot import.
#
# Order matters, and is the one AUTHORING_BRIEF's "Scoring a removal" fixes:
#   1. control  -- a probe importing a package the milestone still has must
#                  build. If it does not, the module graph is broken and every
#                  later case is meaningless, so the group fails here.
#   2. probe    -- a probe importing .../merkletree must fail to build, and the
#                  failure text must name that exact import path. A build that
#                  failed for any other reason is not evidence of a removal.
#   3. successor-- a probe importing the backend library's verifier must build,
#                  which is where the capability went.
#
# The control probe deliberately imports only surfaces this group INHERITS --
# the root package and tls/, both present since step 1, plus LeafHashForLeaf
# which arrived at the previous milestone. It must not reach for anything new
# at this release: an inherited- group has to be passable by the tree the
# previous step left behind, or a do-nothing agent is scored on work it was
# never asked to do here (AUTHORING_BRIEF, "inherited- groups").
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

GONE=github.com/google/certificate-transparency-go/merkletree

mod="$CT_SCRATCH/mod"
cp -r "$CT_ASSETS/verifier" "$mod" 2>/dev/null || {
    for id in control-probe-builds merkletree-probe-does-not-build \
              merkletree-named-in-build-error trillian-merkle-probe-builds; do
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

# --- 1. control: a package this milestone has -------------------------------
mkdir -p "$mod/probe_control"
cat > "$mod/probe_control/main.go" <<'EOF'
package main

import (
	"fmt"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
)

func main() {
	leaf := &ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: 1,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: []byte{0x01}},
		},
	}
	b, err := tls.Marshal(*leaf)
	if err != nil {
		fmt.Println(err)
		return
	}
	h, err := ct.LeafHashForLeaf(leaf)
	fmt.Println(len(b), h, err)
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

# --- 2 and 3. the deleted package -------------------------------------------
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

# --- 4. where the capability went -------------------------------------------
mkdir -p "$mod/probe_trillian"
cat > "$mod/probe_trillian/main.go" <<'EOF'
package main

import (
	"fmt"

	"github.com/google/trillian/merkle"
	"github.com/google/trillian/merkle/rfc6962"
)

func main() {
	v := merkle.NewLogVerifier(rfc6962.DefaultHasher)
	fmt.Println(v.VerifyInclusionProof(0, 1, nil, make([]byte, 32), make([]byte, 32)))
}
EOF
if build probe_trillian; then
    pass trillian-merkle-probe-builds
else
    fail trillian-merkle-probe-builds "the backend library's verifier does not build: $(tr '\n' ' ' < "$mod/probe_trillian.log" | cut -c1-400)"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
