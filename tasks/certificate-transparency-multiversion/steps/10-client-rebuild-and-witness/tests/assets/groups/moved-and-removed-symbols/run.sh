#!/usr/bin/env bash
# moved-and-removed-symbols
#
# v1.1.3 moves one type and deletes two packages, and both kinds of change
# are only real if the old name is gone. TEST_INTERFACE.md section 3.3 fixes
# how that is observed: never with `ls` and never by reading a source file, but
# as a build that fails *because the name is not there*, with a positive
# control first so the group cannot pass for any other reason a build can fail.
#
# The same mechanism grades the retyping that rides along with the move. A
# holder's field type is not observable at run time once the program compiles,
# so each of the three holders CLIENT-API.md section 8.3 and section 4.2 name
# is asserted as a compile-time identity against the moved type: if
# NewCertValidationOpts still took the old pool, or loglist2.LogRoots still
# mapped to it, these probes would not build.
#
# Nothing here runs a candidate binary; every probe is benchmark-owned source
# compiled inside the throwaway ctverify module, whose replace directive
# already points the product at the candidate's tree.
#
# At v1.1.2 x509util.PEMCertPool does not exist, so the control fails and the
# group stops there with nothing passed.
set -u

n=0; f=0
pass() { printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; n=$((n + 1)); }
fail() { printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; n=$((n + 1)); f=$((f + 1)); }
finish() { printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"; exit 0; }
brief() { tr '\n' ' ' < "$1" | cut -c1-400; }

export GOFLAGS=-mod=mod GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4

mod="$CT_SCRATCH/mod"
mkdir -p "$mod"
cp -R "$CT_ASSETS/verifier/." "$mod/" 2>/dev/null || true

gone='does not contain package'
ct='github.com/google/certificate-transparency-go'
ranges="$ct/ingestor/ranges"
watcher="$ct/watcher"

# build <dir-name> <source> -- writes the probe and builds it, leaving the
# compiler's output in $out and its status in $rc.
out=""
rc=0
build() {
	local name="$1" src="$2"
	mkdir -p "$mod/$name"
	printf '%s' "$src" > "$mod/$name/main.go"
	out="$CT_SCRATCH/$name.out"
	if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/$name.bin" "./$name" ) > "$out" 2>&1; then
		rc=0
	else
		rc=1
	fi
}

# buildpath <tag> <import-path> -- builds a product package as a target.
buildpath() {
	local tag="$1" path="$2"
	out="$CT_SCRATCH/$tag.out"
	if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/$tag.bin" "$path" ) > "$out" 2>&1; then
		rc=0
	else
		rc=1
	fi
}

# --- Control --------------------------------------------------------------
# The type at its new home, and the personality package that used to own it,
# in one program. If this will not build the module graph is broken and every
# probe below would fail for the wrong reason.
build probe_control 'package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/x509util"
)

func main() {
	p := x509util.NewPEMCertPool()
	fmt.Println(len(p.RawCertificates()), ctfe.GetSTHName)
}
'
if [ "$rc" -eq 0 ]; then
	pass control-moved-type-and-old-package-build
else
	fail control-moved-type-and-old-package-build "control probe did not build: $(brief "$out")"
	finish
fi

# --- Probe 1: the old name is not merely deprecated, it is absent ---------
build probe_old_pool 'package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
)

func main() {
	var p *ctfe.PEMCertPool
	fmt.Println(p)
}
'
if [ "$rc" -eq 0 ]; then
	fail ctfe-pemcertpool-gone "a program naming ctfe.PEMCertPool still builds"
elif grep -q 'ctfe.PEMCertPool' "$out" && grep -qi 'undefined' "$out"; then
	pass ctfe-pemcertpool-gone
else
	fail ctfe-pemcertpool-gone "build failed without reporting ctfe.PEMCertPool as undefined: $(brief "$out")"
fi

# --- Probe 2: the validation options took the move with them --------------
build probe_valopts 'package main

import (
	"fmt"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"
)

func main() {
	var pool *x509util.PEMCertPool = x509util.NewPEMCertPool()
	opts := ctfe.NewCertValidationOpts(pool, time.Unix(0, 0), false, false, nil, nil, false, []x509.ExtKeyUsage(nil))
	fmt.Println(opts)
}
'
if [ "$rc" -eq 0 ]; then
	pass cert-validation-opts-take-the-moved-pool
else
	fail cert-validation-opts-take-the-moved-pool "NewCertValidationOpts does not accept *x509util.PEMCertPool as its first parameter: $(brief "$out")"
fi

# --- Probe 3: so did the v2 log-list root map -----------------------------
build probe_logroots 'package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/loglist2"
	"github.com/google/certificate-transparency-go/loglist3"
	"github.com/google/certificate-transparency-go/x509util"
)

func main() {
	var r2 map[string]*x509util.PEMCertPool = loglist2.LogRoots{}
	var r3 map[string]*x509util.PEMCertPool = loglist3.LogRoots{}
	fmt.Println(len(r2), len(r3))
}
'
if [ "$rc" -eq 0 ]; then
	pass loglist-logroots-map-to-the-moved-pool
else
	fail loglist-logroots-map-to-the-moved-pool "loglist2.LogRoots / loglist3.LogRoots are not map[string]*x509util.PEMCertPool: $(brief "$out")"
fi

# --- Probe 3b: all three log-list packages are present at once -------------
# v1.1.3 is the only release at which they are (RELEASE-MAP.md section 14):
# loglist3 does not exist at v1.1.2 and loglist is deleted at v1.1.4, so a
# program that resolves a constructor out of each of the three can only build
# here. Nothing is asserted about what they parse -- the schema groups do
# that -- only that all three are linkable.
build probe_three_lists 'package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/loglist"
	"github.com/google/certificate-transparency-go/loglist2"
	"github.com/google/certificate-transparency-go/loglist3"
)

func main() {
	var v1 func([]byte) (*loglist.LogList, error) = loglist.NewFromJSON
	var v2 func([]byte) (*loglist2.LogList, error) = loglist2.NewFromJSON
	var v3 func([]byte) (*loglist3.LogList, error) = loglist3.NewFromJSON
	fmt.Println(v1 == nil, v2 == nil, v3 == nil)
}
'
if [ "$rc" -eq 0 ]; then
	pass all-three-log-list-packages-coexist
else
	fail all-three-log-list-packages-coexist "loglist, loglist2 and loglist3 do not all expose NewFromJSON here: $(brief "$out")"
fi

# --- Probe 3c: loglist3 arrives whole, minus the one field that comes later -
# CLIENT-API.md section 4.3: the v3 package is complete at v1.1.3 except for
# IsAllLogs, which is added at v1.2.0. An absent struct field is only
# observable the way an absent type is -- as a build that fails naming it --
# and this is the pair of probe 1 above. It also pins the boundary in the
# other direction: a candidate that copies a later loglist3 wholesale passes
# every positive schema case and fails here.
build probe_isalllogs 'package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/loglist3"
)

func main() {
	var l loglist3.LogList
	fmt.Println(l.IsAllLogs)
}
'
if [ "$rc" -eq 0 ]; then
	fail loglist3-isalllogs-arrives-later "a program naming loglist3.LogList.IsAllLogs still builds; that field belongs to v1.2.0"
elif grep -q 'IsAllLogs' "$out" && grep -qi 'undefined\|unknown field\|has no field' "$out"; then
	pass loglist3-isalllogs-arrives-later
else
	fail loglist3-isalllogs-arrives-later "build failed without reporting IsAllLogs as absent: $(brief "$out")"
fi

# --- Probe 4: the hammer entry point leads with a context ------------------
build probe_hammer 'package main

import (
	"context"
	"fmt"

	"github.com/google/certificate-transparency-go/trillian/integration"
)

func main() {
	var fn func(context.Context, integration.HammerConfig) error = integration.HammerCTLog
	fmt.Println(fn == nil)
}
'
if [ "$rc" -eq 0 ]; then
	pass hammerctlog-leads-with-a-context
else
	fail hammerctlog-leads-with-a-context "HammerCTLog is not func(context.Context, HammerConfig) error: $(brief "$out")"
fi

# --- Probes 5-8: the two deleted packages ---------------------------------
build probe_ranges 'package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/ingestor/ranges"
)

func main() {
	fmt.Println(ranges.Range{})
}
'
if [ "$rc" -eq 0 ]; then
	fail ingestor-ranges-import-unresolved "a program importing $ranges still builds"
elif grep -q "$gone $ranges\$" "$out"; then
	pass ingestor-ranges-import-unresolved
else
	fail ingestor-ranges-import-unresolved "build failed without naming $ranges: $(brief "$out")"
fi

buildpath ranges_pkg "$ranges"
if [ "$rc" -eq 0 ]; then
	fail ingestor-ranges-not-a-build-target "$ranges is still a buildable package in the product module"
elif grep -q "$gone $ranges\$" "$out"; then
	pass ingestor-ranges-not-a-build-target
else
	fail ingestor-ranges-not-a-build-target "build failed without naming $ranges: $(brief "$out")"
fi

build probe_watcher 'package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/watcher"
)

func main() {
	fmt.Println(watcher.Options{})
}
'
if [ "$rc" -eq 0 ]; then
	fail watcher-import-unresolved "a program importing $watcher still builds"
elif grep -q "$gone $watcher\$" "$out"; then
	pass watcher-import-unresolved
else
	fail watcher-import-unresolved "build failed without naming $watcher: $(brief "$out")"
fi

buildpath watcher_pkg "$watcher"
if [ "$rc" -eq 0 ]; then
	fail watcher-not-a-build-target "$watcher is still a buildable package in the product module"
elif grep -q "$gone $watcher\$" "$out"; then
	pass watcher-not-a-build-target
else
	fail watcher-not-a-build-target "build failed without naming $watcher: $(brief "$out")"
fi

finish
