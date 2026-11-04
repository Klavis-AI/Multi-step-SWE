#!/usr/bin/env bash
# loglist2-removed
#
# v1.2.0 deletes the second log-list generation outright (loglist2/loglist2.go,
# logfilter.go, and the bundled schema/test data), leaving loglist3 as the only
# log-list package in the tree. Step 11 removed the *first* generation the same
# way; this is that probe carried forward one release.
#
# A structural removal is never decided with ls or by reading a source file: it
# is observed as a build that fails *because the package is gone*, with a
# positive control first so the group cannot pass for any other reason a build
# can fail. SCOPE-DIAGNOSTIC point 3 applies -- the message *is* the
# observable here, so the diagnostic is matched and not merely the exit status.
#
# The matched string is the toolchain's "does not contain package <path>", the
# same form steps 09, 10, 11 and 13 use for the same job. It was loosened to
# "the output merely names the path" during round 5 and is restored here: a
# package left in place but BROKEN also produces errors naming its own path, so
# the loose form stops distinguishing "removed" from "does not compile", which
# is the only thing this group exists to decide. The premise offered for the
# loosening -- that an emptied-but-present directory would say "no Go files in"
# instead -- was tested at step 13 against this harness's build shape and is
# false: these probes build by IMPORT PATH inside a module with a replace
# directive, and that path emits "does not contain package" whether the
# directory is absent or merely empty.
#
# Nothing here runs a candidate binary; the two probes are benchmark-owned
# source compiled inside the throwaway ctverify module, whose replace directive
# already points the product at the candidate's tree.
set -u

n=0; f=0
pass() { printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; n=$((n + 1)); }
fail() { printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; n=$((n + 1)); f=$((f + 1)); }
finish() { printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"; exit 0; }

export GOFLAGS=-mod=mod GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4

mod="$CT_SCRATCH/mod"
mkdir -p "$mod"
cp -R "$CT_ASSETS/verifier/." "$mod/" 2>/dev/null || true

gone='does not contain package'
v2='github.com/google/certificate-transparency-go/loglist2'

# --- Control -------------------------------------------------------------
# The surviving generation. If this will not build the module graph is broken
# and every probe below would fail for the wrong reason.
mkdir -p "$mod/probe_control"
cat > "$mod/probe_control/main.go" <<'GO'
package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/loglist3"
)

func main() {
	var ll loglist3.LogList
	fmt.Println(len(ll.Operators), loglist3.LogListURL)
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/control.bin" ./probe_control ) > "$CT_SCRATCH/control.out" 2>&1; then
	pass control-successor-package-builds
else
	fail control-successor-package-builds "loglist3 probe did not build: $(tr '\n' ' ' < "$CT_SCRATCH/control.out" | cut -c1-400)"
	finish
fi

# --- The one field this release adds to the surviving package -------------
# loglist3.LogList gains IsAllLogs at v1.2.0 (CLIENT-API.md 4.3), with the json
# tag is_all_logs,omitempty -- the only change this milestone makes to the
# package that survives. It is built *and run*, so the field is exercised
# through the encoder a caller reaches rather than merely named; and it is a
# probe of its own rather than part of the control, so a tree that removed
# loglist2 but forgot the field still scores the two removal cases.
mkdir -p "$mod/probe_isalllogs"
cat > "$mod/probe_isalllogs/main.go" <<'GO'
package main

import (
	"bytes"
	"encoding/json"
	"fmt"

	"github.com/google/certificate-transparency-go/loglist3"
)

func main() {
	var on loglist3.LogList
	on.Version = "1"
	on.IsAllLogs = true
	onJSON, err := json.Marshal(&on)
	if err != nil {
		fmt.Println("ISALLLOGS marshal-error", err)
		return
	}
	var back loglist3.LogList
	if err := json.Unmarshal(onJSON, &back); err != nil {
		fmt.Println("ISALLLOGS unmarshal-error", err)
		return
	}
	var off loglist3.LogList
	off.Version = "1"
	offJSON, err := json.Marshal(&off)
	if err != nil {
		fmt.Println("ISALLLOGS marshal-error", err)
		return
	}
	fmt.Printf("ISALLLOGS set=%t back=%t omitted=%t\n",
		bytes.Contains(onJSON, []byte(`"is_all_logs":true`)),
		back.IsAllLogs,
		!bytes.Contains(offJSON, []byte("is_all_logs")))
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/isall.bin" ./probe_isalllogs ) > "$CT_SCRATCH/isall.out" 2>&1 \
	&& timeout 60 "$CT_SCRATCH/isall.bin" >> "$CT_SCRATCH/isall.out" 2>&1 \
	&& grep -q '^ISALLLOGS set=true back=true omitted=true$' "$CT_SCRATCH/isall.out"; then
	pass loglist3-is-all-logs-round-trips
else
	fail loglist3-is-all-logs-round-trips "LogList.IsAllLogs did not round-trip as is_all_logs,omitempty: $(tr '\n' ' ' < "$CT_SCRATCH/isall.out" | cut -c1-400)"
fi

# --- Probe 1: the import path no longer resolves -------------------------
mkdir -p "$mod/probe_v2"
cat > "$mod/probe_v2/main.go" <<'GO'
package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/loglist2"
)

func main() {
	var ll loglist2.LogList
	fmt.Println(len(ll.Operators))
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/v2.bin" ./probe_v2 ) > "$CT_SCRATCH/v2.out" 2>&1; then
	fail loglist-v2-import-unresolved "a program importing $v2 still builds"
elif grep -q "$gone $v2\$" "$CT_SCRATCH/v2.out"; then
	pass loglist-v2-import-unresolved
else
	fail loglist-v2-import-unresolved "build failed without naming $v2: $(tr '\n' ' ' < "$CT_SCRATCH/v2.out" | cut -c1-400)"
fi

# --- Probe 2: the package is not a build target either --------------------
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/v2pkg.bin" "$v2" ) > "$CT_SCRATCH/v2pkg.out" 2>&1; then
	fail loglist-v2-not-a-build-target "$v2 is still a buildable package in the product module"
elif grep -q "$gone $v2\$" "$CT_SCRATCH/v2pkg.out"; then
	pass loglist-v2-not-a-build-target
else
	fail loglist-v2-not-a-build-target "build failed without naming $v2: $(tr '\n' ' ' < "$CT_SCRATCH/v2pkg.out" | cut -c1-400)"
fi

finish
