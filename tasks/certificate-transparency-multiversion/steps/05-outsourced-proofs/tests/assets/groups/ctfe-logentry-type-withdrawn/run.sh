#!/usr/bin/env bash
# Group ctfe-logentry-type-withdrawn.
#
# v1.0.17 drops the CTFE's own front-end/back-end container type: commit
# 84ed8d8b "trillian/ctfe: drop unused code" removes the exported type
# ctfe.LogEntry and its constructor ctfe.NewLogEntry, leaving the root
# package's ct.LogEntry as the only such type. Both names were exported at
# v1.0.13, so their disappearance is observable exactly the way a deleted
# package is: a probe that names them stops compiling.
#
# Same order as every removal group: control first (a probe using a symbol of
# the same package that survives -- ctfe.GetCTLogID), then the probes that must
# fail, with the failure text required to name the withdrawn identifier so that
# an unrelated build break cannot be mistaken for evidence.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

mod="$CT_SCRATCH/mod"
cp -r "$CT_ASSETS/verifier" "$mod" 2>/dev/null || {
    for id in control-probe-builds new-log-entry-probe-does-not-build \
              new-log-entry-named-in-build-error log-entry-type-probe-does-not-build; do
        fail "$id" "could not copy \$CT_ASSETS/verifier"
    done
    printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
    exit 0
}
chmod -R u+w "$mod"
export GOFLAGS=-mod=mod GOPROXY=off GOSUMDB=off GOWORK=off GOMAXPROCS=4

build() {
    ( cd "$mod" && timeout 600 go build -p 4 -o /dev/null "./$1" ) > "$mod/$1.log" 2>&1
}
head400() { tr '\n' ' ' < "$1" | cut -c1-400; }

# --- 1. control: the same package, a symbol this milestone keeps ------------
mkdir -p "$mod/probe_control"
cat > "$mod/probe_control/main.go" <<'EOF'
package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"fmt"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
)

func main() {
	k, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		panic(err)
	}
	id, err := ctfe.GetCTLogID(&k.PublicKey)
	fmt.Println(len(id), err)
}
EOF
if build probe_control; then
    pass control-probe-builds
else
    fail control-probe-builds "the surviving-symbol control does not build; module graph broken: $(head400 "$mod/probe_control.log")"
    printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
    exit 0
fi

# --- 2 and 3. the withdrawn constructor ------------------------------------
mkdir -p "$mod/probe_newlogentry"
cat > "$mod/probe_newlogentry/main.go" <<'EOF'
package main

import (
	"fmt"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/x509"
)

func main() {
	leaf := ct.MerkleTreeLeaf{Version: ct.V1, LeafType: ct.TimestampedEntryLeafType}
	fmt.Println(ctfe.NewLogEntry(leaf, []*x509.Certificate{}))
}
EOF
if build probe_newlogentry; then
    fail new-log-entry-probe-does-not-build "a probe calling ctfe.NewLogEntry still builds"
    fail new-log-entry-named-in-build-error "no build error to inspect"
else
    pass new-log-entry-probe-does-not-build
    if grep -q "NewLogEntry" "$mod/probe_newlogentry.log"; then
        pass new-log-entry-named-in-build-error
    else
        fail new-log-entry-named-in-build-error "build failed but never named NewLogEntry: $(head400 "$mod/probe_newlogentry.log")"
    fi
fi

# --- 4. the withdrawn type -------------------------------------------------
mkdir -p "$mod/probe_logentry"
cat > "$mod/probe_logentry/main.go" <<'EOF'
package main

import (
	"fmt"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
)

func main() {
	e := ctfe.LogEntry{Chain: []ct.ASN1Cert{}}
	fmt.Println(e)
}
EOF
if build probe_logentry; then
    fail log-entry-type-probe-does-not-build "a probe naming the ctfe.LogEntry type still builds"
else
    if grep -q "LogEntry" "$mod/probe_logentry.log"; then
        pass log-entry-type-probe-does-not-build
    else
        fail log-entry-type-probe-does-not-build "build failed but never named LogEntry: $(head400 "$mod/probe_logentry.log")"
    fi
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
