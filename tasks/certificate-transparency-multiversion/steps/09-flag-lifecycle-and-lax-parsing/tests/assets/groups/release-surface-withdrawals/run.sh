#!/usr/bin/env bash
# Group release-surface-withdrawals.
#
# What v1.1.2 stops shipping, and the one set of constants it re-points --
# four questions about what this release *is*, every one of them answered by
# building or by running, never by reading a source file (NOTES.md,
# "Structural removals are observed, never listed").
#
#   * gossip/ moves out to trillian-examples except minimal/x509ext, so the
#     three gossip commands (goshawk, gosmin, gossip_server) stop being built
#     from this repository (RELEASE-MAP.md §9).
#   * client.LogClient loses AddJSON (CLIENT-API.md §4.1).
#   * trillian/util loses resolver.go, because google.golang.org/grpc/naming
#     is not in the pinned dependency set at this milestone: a question about
#     what this release can still link (ARCHITECTURE.md §2.1).
#   * loglist2's three URL constants stop naming the v2beta log list and name
#     v2. Graded as string constants only: nothing here fetches one.
#
# All four were cases of `build-clean` until the 2026-09-21 re-tier. They are
# this step's new capability, not proof of life, which is exactly why they
# cannot sit in the gate: the step-08 tree satisfies none of them. They are
# peripheral breadth rather than the headline capability, so weight 2.
#
# TWO OF THE FIVE ARE COMPILER-FORCED. Do not split this group, drop a case
# from it, or re-tier it without reading this paragraph. v1.1.2's pinned
# manifest stops providing the modules `gossip/` imports, and grpc 1.40 deletes
# `google.golang.org/grpc/naming`, so any tree that compiles at all has already
# lost the three gossip binaries (case 2) and `util.FixedBackendResolver`
# (case 5). Both are still this step's own work -- they are RELEASE-MAP.md §9
# bullets, not inherited behaviour -- so the operator's rule that nothing
# scored may be satisfiable by inherited work alone is not broken. What keeps
# it safe is that the harness scores a group all-or-nothing (`executed == want`
# and `failed == 0`), so 2 of 5 is worth exactly 0, and cases 3 and 4 -- the
# `AddJSON` removal and the loglist2 URLs -- are forced by nothing and are
# where a null candidate fails. Split the five into per-case groups, or remove
# case 3 or case 4, and the dependency bump alone would start paying out.
#
# Case 1 is the positive control the other three lean on: without it a probe
# that must fail to build could pass for any reason a build can fail. It is
# also what runs to print the log-list constants for case 4.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

# Withdrawn with gossip/minimal.
GONE="goshawk gosmin gossip_server"

export GOFLAGS=-mod=mod GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4
mod="$CT_SCRATCH/mod"
mkdir -p "$mod"
cp -R "$CT_ASSETS/verifier/." "$mod/" 2>/dev/null || true

# --- 1. control: the client and loglist2 surface that survives -------------
# client.New must still make a LogClient and loglist2's three constants must
# still be readable. The program prints them, so the same build answers case 4.
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

# --- 2. the three gossip commands are gone ---------------------------------
present=""
for b in $GONE; do
    if [ -e "$CT_BIN/$b" ]; then
        present="$present $b"
    fi
done
if [ -n "$present" ]; then
    fail withdrawn-gossip-binaries-absent "still built:$present"
else
    pass withdrawn-gossip-binaries-absent
fi

# --- 3. AddJSON is gone from LogClient -------------------------------------
mkdir -p "$mod/probe_addjson"
cat > "$mod/probe_addjson/main.go" <<'GO'
package main

import (
	"context"
	"fmt"
	"net/http"

	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/jsonclient"
)

func main() {
	lc, err := client.New("http://ct.example.invalid", &http.Client{}, jsonclient.Options{})
	if err != nil {
		panic(err)
	}
	sct, err := lc.AddJSON(context.Background(), struct{}{})
	fmt.Println(sct, err)
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/addjson.bin" ./probe_addjson ) \
        > "$CT_SCRATCH/addjson.out" 2>&1; then
    fail client-AddJSON-withdrawn "a program calling (*client.LogClient).AddJSON still builds"
elif grep -q 'AddJSON' "$CT_SCRATCH/addjson.out"; then
    pass client-AddJSON-withdrawn
else
    fail client-AddJSON-withdrawn \
        "build failed without naming AddJSON: $(tr '\n' ' ' < "$CT_SCRATCH/addjson.out" | cut -c1-400)"
fi

# --- 4. the three log-list URLs now name v2, not v2beta --------------------
# Graded as string constants only: nothing here fetches one.
if [ -x "$CT_SCRATCH/surface.bin" ]; then
    env -u CT_VERIFY_NONCE timeout 60 "$CT_SCRATCH/surface.bin" \
        > "$CT_SCRATCH/surface.txt" 2>&1 < /dev/null
    want="https://www.gstatic.com/ct/log_list/v2/log_list.json
https://www.gstatic.com/ct/log_list/v2/log_list.sig
https://www.gstatic.com/ct/log_list/v2/all_logs_list.json"
    got=$(cat "$CT_SCRATCH/surface.txt")
    if [ "$got" = "$want" ]; then
        pass loglist2-urls-target-v2
    else
        fail loglist2-urls-target-v2 "constants are [$(echo "$got" | tr '\n' ' ')]"
    fi
else
    fail loglist2-urls-target-v2 "the control program did not build"
fi

# --- 5. the grpc/naming backend resolver is gone from trillian/util --------
# google.golang.org/grpc/naming is not in the pinned dependency set at this
# milestone, so resolver.go cannot survive it. Asked the same way as case 3:
# a program naming the symbol, built against the candidate's own tree.
mkdir -p "$mod/probe_resolver"
cat > "$mod/probe_resolver/main.go" <<'GO'
package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/trillian/util"
)

func main() {
	var r util.FixedBackendResolver
	fmt.Println(r)
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/resolver.bin" ./probe_resolver ) \
        > "$CT_SCRATCH/resolver.out" 2>&1; then
    fail util-FixedBackendResolver-withdrawn \
        "a program naming util.FixedBackendResolver still builds"
elif grep -q 'FixedBackendResolver' "$CT_SCRATCH/resolver.out"; then
    pass util-FixedBackendResolver-withdrawn
else
    fail util-FixedBackendResolver-withdrawn \
        "build failed without naming FixedBackendResolver: $(tr '\n' ' ' < "$CT_SCRATCH/resolver.out" | cut -c1-400)"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
