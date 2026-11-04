#!/usr/bin/env bash
# loglist-v2-survives-with-no-consumers
#
# The v1 package is deleted at v1.1.4; the v2 package next to it is *not*. It
# stays in the tree, still published and still compiling, and what changes is
# that every one of its consumers moves to v3 in the same release, leaving it
# with none. Both halves of that are part of the milestone and neither implies
# the other: a tree that deletes loglist2 a release early satisfies "no
# consumer" trivially, and a tree that leaves its consumers on v2 satisfies
# "still compiles" trivially. This group asserts both, and asserts the
# consumers landed on v3 rather than on nothing.
#
# It discriminates against v1.1.3 twice over: there, ctpolicy, submission and
# two of the witness programs all depend on loglist2, and none of them depends
# on loglist3.
#
# Cases 5 and 6 arrived here from build-clean, which is a GATE group: they
# assert that a single benchmark-owned file naming the v1.1.4 signatures of
# ctpolicy, submission and ctutil at once compiles and runs. That is this
# step's own new capability -- at v1.1.3 those three are declared over
# loglist (v1) and loglist2, so one file cannot name all three v3 signatures
# and still compile -- and a gate group may not assert it (gate rule 3). They
# are the type-level form of case 4: case 4 asks the import graph whether the
# migrated consumers reach loglist3, these two ask the type checker whether
# their exported signatures are actually written in it.
#
# Nothing here runs a candidate binary and nothing reads a candidate source
# file: every verdict is the toolchain's own answer about the module's import
# graph or type graph, taken from the throwaway ctverify module whose replace
# directive already points the product at the candidate's tree. The one binary
# that is executed is benchmark-owned source, built above.
set -u

n=0; f=0
pass() { printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; n=$((n + 1)); }
fail() { printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; n=$((n + 1)); f=$((f + 1)); }
finish() { printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"; exit 0; }

export GOFLAGS=-mod=mod GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4

mod="$CT_SCRATCH/mod"
mkdir -p "$mod"
cp -R "$CT_ASSETS/verifier/." "$mod/" 2>/dev/null || true

prod='github.com/google/certificate-transparency-go'
v2="$prod/loglist2"
v3="$prod/loglist3"

# --- case 1: control. The product's package list is readable through the
# replace directive, and it is not empty. Everything below is a question about
# that list, so if this fails the rest would fail for the wrong reason.
if ( cd "$mod" && timeout 600 go list -f '{{.ImportPath}}|{{join .Deps " "}}' "$prod/..." ) \
     > "$CT_SCRATCH/pkgs.txt" 2> "$CT_SCRATCH/pkgs.err" \
   && [ -s "$CT_SCRATCH/pkgs.txt" ]; then
	pass control-product-package-graph-is-readable
else
	fail control-product-package-graph-is-readable "go list failed: $(tr '\n' ' ' < "$CT_SCRATCH/pkgs.err" | cut -c1-400)"
	finish
fi

# --- case 2: the v2 package is still there and still compiles. This is the
# half a tree that deleted it a release early cannot satisfy.
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/v2pkg.bin" "$v2" ) \
     > "$CT_SCRATCH/v2pkg.out" 2>&1; then
	pass loglist-v2-is-still-a-build-target
else
	fail loglist-v2-is-still-a-build-target "building $v2 failed: $(tr '\n' ' ' < "$CT_SCRATCH/v2pkg.out" | cut -c1-400)"
fi

# --- case 3: nothing in the product depends on it any more. The dependency
# closure is the toolchain's, so this catches an indirect consumer as well as
# a direct import; loglist2 itself is excluded, since a package trivially
# belongs to its own package list.
consumers=$(awk -F'|' -v v2="$v2" '
	$1 == v2 { next }
	index($1, v2 "/") == 1 { next }
	{
		nd = split($2, d, " ")
		for (i = 1; i <= nd; i++) if (d[i] == v2) { print $1; break }
	}' "$CT_SCRATCH/pkgs.txt" | sort | tr '\n' ' ')
if [ -z "$consumers" ]; then
	pass no-product-package-depends-on-loglist-v2
else
	fail no-product-package-depends-on-loglist-v2 "still reached from: $consumers"
fi

# --- case 4: and the four packages the release moved off v2 landed on v3,
# rather than on nothing at all. Without this, deleting ctpolicy and
# submission outright would satisfy case 3.
missing=
for p in "$prod/ctpolicy" "$prod/submission" \
         "$prod/internal/witness/cmd/feeder" \
         "$prod/internal/witness/cmd/witness/config"; do
	line=$(awk -F'|' -v p="$p" '$1 == p { print $2 }' "$CT_SCRATCH/pkgs.txt")
	if [ -z "$line" ]; then
		missing="$missing ${p##*/}(absent)"
	elif ! printf '%s\n' "$line" | tr ' ' '\n' | grep -qxF "$v3"; then
		missing="$missing ${p##*/}(no-v3)"
	fi
done
if [ -z "$missing" ]; then
	pass migrated-consumers-now-depend-on-loglist-v3
else
	fail migrated-consumers-now-depend-on-loglist-v3 "not on v3:$missing"
fi

# --- case 5: a single consumer written against the v1.1.4 signatures of all
# three migrated packages compiles. The union of the per-package groups does
# not imply this: it is one compilation unit, so ctpolicy, submission and
# ctutil must all be declared over loglist3 *at the same time*. Moved here
# from build-clean, which may not assert a v1.1.4-only fact.
mkdir -p "$mod/probe_consumer"
cat > "$mod/probe_consumer/main.go" <<'GO'
package main

import (
	"fmt"
	"net/http"

	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/ctpolicy"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/loglist3"
	"github.com/google/certificate-transparency-go/submission"
)

// Every signature below is the v1.1.4 one.
var (
	_ func(*loglist3.LogList, int) (*ctpolicy.LogGroupInfo, error) = ctpolicy.BaseGroupFor
	_ func(*loglist3.Log) (client.AddLogClient, error)             = submission.BuildLogClient
	_ func(*loglist3.Log, *http.Client) (*ctutil.LogInfo, error)   = ctutil.NewLogInfo
	_ submission.LogClientBuilder                                  = submission.BuildLogClient
	_ ctpolicy.CTPolicy                                            = ctpolicy.ChromeCTPolicy{}
	_ ctpolicy.CTPolicy                                            = ctpolicy.AppleCTPolicy{}
)

func main() {
	ll := &loglist3.LogList{}
	m, err := ctutil.LogInfoByKeyHash(ll, http.DefaultClient)
	fmt.Println(len(m), err)
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/consumer.bin" ./probe_consumer ) \
     > "$CT_SCRATCH/consumer.out" 2>&1; then
	pass v3-consumer-compiles-against-the-product
else
	fail v3-consumer-compiles-against-the-product "$(tr '\n' ' ' < "$CT_SCRATCH/consumer.out" | cut -c1-600)"
fi

# --- case 6: and runs. ctutil.LogInfoByKeyHash over an empty v3 list is an
# empty index and no error; the point is that the linked program is a real
# program, not just a type-checked one. No network is reached: the list has no
# operators, so no log is contacted.
if [ -x "$CT_SCRATCH/consumer.bin" ] \
   && out=$(env -u CT_VERIFY_NONCE "$CT_SCRATCH/consumer.bin" 2>&1) \
   && [ "$out" = "0 <nil>" ]; then
	pass v3-consumer-runs
else
	fail v3-consumer-runs "out=[${out:-<none>}]"
fi

finish
