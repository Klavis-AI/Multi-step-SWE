#!/usr/bin/env bash
# Group dnsclient-withdrawn-from-sct-tools.
#
# v1.1.2 deletes dnsclient/ outright. Its only two consumers were sctscan and
# sctcheck, whose --dns switch chose the DNS transport; both lose the switch in
# the same step and both gain --align_getentries from the get-entries work.
#
# ctclient carried three switches of its own for the same transport -- --dns,
# --dns_base and --dns_server -- and loses all three here.
#
# Observed at the process boundary: --dns must now be an undefined flag (exit
# status 2) rather than a runtime failure of a tool that understood it (exit
# status 1), and the usage text must agree. The exit status and the usage line
# are the observables; the `flag` package's own wording is not compared,
# because it belongs to the standard library rather than to this product.
#
# The library half of the same subtraction is observed the only way a removal
# ever is here: a positive control that must build, then probes that must fail
# to build *because* the path or the identifier is gone. Never with ls.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

TOOLS="sctscan sctcheck"

ALL_IDS="sctscan-dns-flag-undefined sctcheck-dns-flag-undefined
         dns-absent-from-both-usages align_getentries-present-in-both-usages
         log_list-still-accepted
         ctclient-dns-flag-withdrawn ctclient-dns_base-flag-withdrawn
         ctclient-dns_server-flag-withdrawn
         control-ctutil-package-builds dnsclient-import-unresolved
         ctutil-NewLogInfoOverDNS-gone ctutil-NewLogInfoOverDNSWrapper-gone
         ctutil-LogInfoByKeyHashOverDNS-gone"

missing=""
for b in $TOOLS ctclient; do
    [ -x "$CT_BIN/$b" ] || missing="$missing $b"
done
if [ -n "$missing" ]; then
    for id in $ALL_IDS; do
        fail "$id" "not built:$missing"
    done
    printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
    exit 0
fi

for b in $TOOLS; do
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/$b" --help \
        > "$CT_SCRATCH/usage-$b.txt" 2>&1 < /dev/null
done

# --- 1 and 2. --dns is undefined in each tool ------------------------------
# The log list path does not exist, so a milestone that still defines --dns
# gets as far as failing to read it, which is exit status 1.
for b in $TOOLS; do
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/$b" --dns \
        --log_list="$CT_SCRATCH/no-such-log-list.json" \
        > "$CT_SCRATCH/dns-$b.txt" 2>&1 < /dev/null
    rc=$?
    if [ "$rc" = 2 ]; then
        pass "$b-dns-flag-undefined"
    else
        fail "$b-dns-flag-undefined" "exit status $rc, want 2"
    fi
done

# --- 3. neither usage documents it any more --------------------------------
still=""
for b in $TOOLS; do
    grep -q '^  -dns' "$CT_SCRATCH/usage-$b.txt" && still="$still $b"
done
if [ -n "$still" ]; then
    fail dns-absent-from-both-usages "usage still documents -dns:$still"
else
    pass dns-absent-from-both-usages
fi

# --- 4. both gained the get-entries alignment switch in the same step ------
absent=""
for b in $TOOLS; do
    grep -q -x -e '  -align_getentries' "$CT_SCRATCH/usage-$b.txt" || absent="$absent $b"
done
if [ -n "$absent" ]; then
    fail align_getentries-present-in-both-usages "no '  -align_getentries' line in:$absent"
else
    pass align_getentries-present-in-both-usages
fi

# --- 5. control: the surviving flag both tools share is untouched ----------
absent=""
for b in $TOOLS; do
    grep -q -x -e '  -log_list string' "$CT_SCRATCH/usage-$b.txt" || absent="$absent $b"
done
if [ -n "$absent" ]; then
    fail log_list-still-accepted "no '  -log_list string' line in:$absent"
else
    pass log_list-still-accepted
fi

# --- 6, 7, 8. ctclient loses its own three DNS switches --------------------
# Same subtraction, third tool. Each flag is checked twice: it must be gone
# from the usage block, and offering it must be a parse error (2) rather than
# the runtime failure (1) that a milestone still carrying the transport gives.
env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/ctclient" --help \
    > "$CT_SCRATCH/usage-ctclient.txt" 2>&1 < /dev/null

ctclient_flag_gone() {   # $1 = flag name, $2.. = argv that offers it
    id="ctclient-$1-flag-withdrawn"
    name="$1"; shift
    if grep -q -E "^  -$name([ 	]|\$)" "$CT_SCRATCH/usage-ctclient.txt"; then
        fail "$id" "ctclient usage still documents -$name"
        return
    fi
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/ctclient" "$@" \
        > "$CT_SCRATCH/ctclient-$name.txt" 2>&1 < /dev/null
    rc=$?
    if [ "$rc" = 2 ]; then
        pass "$id"
    else
        fail "$id" "exit status $rc, want 2"
    fi
}

dead="http://127.0.0.1:1/nope"
ctclient_flag_gone dns        --dns --log_uri="$dead" sth
ctclient_flag_gone dns_base   --dns_base=x --dns_server=127.0.0.1:9 --log_uri="$dead" sth
ctclient_flag_gone dns_server --dns_server=127.0.0.1:9 --log_uri="$dead" sth

# --- 9 to 13. the library the switches drove ------------------------------
# dnsclient/ is deleted and ctutil loses the three DNS-flavoured constructors
# that wrapped it. Compiled probes inside the throwaway ctverify module, whose
# replace directive already points the product at the candidate's tree.
export GOFLAGS=-mod=mod GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4
mod="$CT_SCRATCH/mod"
mkdir -p "$mod"
cp -R "$CT_ASSETS/verifier/." "$mod/" 2>/dev/null || true

gone='does not contain package'
dnspkg='github.com/google/certificate-transparency-go/dnsclient'

# Control: ctutil itself, and the two by-hash/by-log constructors that survive
# the cut, must still build. Without this the two probes below could pass for
# any reason a build can fail.
mkdir -p "$mod/probe_ctutil_control"
cat > "$mod/probe_ctutil_control/main.go" <<'GO'
package main

import (
	"fmt"
	"net/http"

	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/loglist"
)

func main() {
	var l loglist.Log
	var ll loglist.LogList
	hc := &http.Client{}
	li, err := ctutil.NewLogInfo(&l, hc)
	m, err2 := ctutil.LogInfoByKeyHash(&ll, hc)
	fmt.Println(li, err, len(m), err2)
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/ctutil-control.bin" ./probe_ctutil_control ) \
        > "$CT_SCRATCH/ctutil-control.out" 2>&1; then
    pass control-ctutil-package-builds
else
    fail control-ctutil-package-builds \
        "surviving ctutil constructors did not build: $(tr '\n' ' ' < "$CT_SCRATCH/ctutil-control.out" | cut -c1-400)"
    for id in dnsclient-import-unresolved ctutil-NewLogInfoOverDNS-gone \
              ctutil-NewLogInfoOverDNSWrapper-gone ctutil-LogInfoByKeyHashOverDNS-gone; do
        fail "$id" "skipped: the positive control did not build"
    done
    printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
    exit 0
fi

# Probe A: the package path itself no longer resolves.
mkdir -p "$mod/probe_dnsclient"
cat > "$mod/probe_dnsclient/main.go" <<'GO'
package main

import (
	"fmt"

	"github.com/google/certificate-transparency-go/dnsclient"
	"github.com/google/certificate-transparency-go/jsonclient"
)

func main() {
	c, err := dnsclient.New("ct.example.invalid", jsonclient.Options{})
	fmt.Println(c, err)
}
GO
if ( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/dnsclient.bin" ./probe_dnsclient ) \
        > "$CT_SCRATCH/dnsclient.out" 2>&1; then
    fail dnsclient-import-unresolved "a program importing $dnspkg still builds"
elif grep -q "$gone $dnspkg\$" "$CT_SCRATCH/dnsclient.out"; then
    pass dnsclient-import-unresolved
else
    fail dnsclient-import-unresolved \
        "build failed without naming $dnspkg: $(tr '\n' ' ' < "$CT_SCRATCH/dnsclient.out" | cut -c1-400)"
fi

# Probe B: the three ctutil wrappers. One probe names all three, so a single
# failed build carries one diagnostic per identifier and each is graded on its
# own -- a milestone that dropped only two of the three fails only that case.
mkdir -p "$mod/probe_ctutil_dns"
cat > "$mod/probe_ctutil_dns/main.go" <<'GO'
package main

import (
	"fmt"
	"net/http"

	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/loglist"
)

func main() {
	var l loglist.Log
	var ll loglist.LogList
	hc := &http.Client{}
	a, _ := ctutil.NewLogInfoOverDNS(&l)
	b, _ := ctutil.NewLogInfoOverDNSWrapper(&l, hc)
	c, _ := ctutil.LogInfoByKeyHashOverDNS(&ll, hc)
	fmt.Println(a, b, len(c))
}
GO
( cd "$mod" && timeout 600 go build -o "$CT_SCRATCH/ctutil-dns.bin" ./probe_ctutil_dns ) \
    > "$CT_SCRATCH/ctutil-dns.out" 2>&1
built=$?
for sym in NewLogInfoOverDNS NewLogInfoOverDNSWrapper LogInfoByKeyHashOverDNS; do
    id="ctutil-$sym-gone"
    if [ "$built" = 0 ]; then
        fail "$id" "a program calling ctutil.$sym still builds"
    elif grep -q -E "undefined: ctutil\.$sym([^A-Za-z0-9_]|\$)" "$CT_SCRATCH/ctutil-dns.out"; then
        pass "$id"
    else
        fail "$id" \
            "build failed but ctutil.$sym was not reported undefined: $(tr '\n' ' ' < "$CT_SCRATCH/ctutil-dns.out" | cut -c1-400)"
    fi
done

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
