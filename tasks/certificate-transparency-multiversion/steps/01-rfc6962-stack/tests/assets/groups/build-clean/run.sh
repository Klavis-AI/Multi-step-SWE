#!/usr/bin/env bash
# Group build-clean.
#
# The whole product must compile, and the eight command-line programs this
# milestone ships must come out of `go build ./...`. Nothing the build prints
# is allowed near this script's stdout: every command is redirected to a file
# in $CT_SCRATCH and the verdicts are read back from there.
set -u

n=0; f=0
rep() {  # rep <id> <ok:0|1> [why]
    n=$((n+1))
    if [ "$2" = 0 ]; then
        printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"
    else
        f=$((f+1))
        printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "${3:-}"
    fi
}
finish() { printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"; exit 0; }

W="$CT_SCRATCH/bc"; mkdir -p "$W/log" "$W/bin"
export GOFLAGS=-mod=mod GOWORK=off GOMAXPROCS=4

# The candidate's tree is wherever the throwaway module's replace points.
tree=$(sed -n 's#^replace github.com/google/certificate-transparency-go => ##p' \
       "$CT_ASSETS/verifier/go.mod" | tail -1 | tr -d ' \r')
[ -n "$tree" ] || tree=/app

# ---- positive control: the module graph is usable at all -----------------------
cp -R "$CT_ASSETS/verifier" "$W/mod" 2>/dev/null
mkdir -p "$W/mod/g/probe"
cat > "$W/mod/g/probe/main.go" <<'GOEOF'
package main

import (
	_ "github.com/google/certificate-transparency-go"
	_ "github.com/google/certificate-transparency-go/asn1"
	_ "github.com/google/certificate-transparency-go/client"
	_ "github.com/google/certificate-transparency-go/jsonclient"
	_ "github.com/google/certificate-transparency-go/merkletree"
	_ "github.com/google/certificate-transparency-go/tls"
	_ "github.com/google/certificate-transparency-go/trillian/ctfe"
	_ "github.com/google/certificate-transparency-go/x509"
	_ "github.com/google/certificate-transparency-go/x509util"
)

func main() {}
GOEOF
# No inner timeout on either build below: the harness already runs this whole
# script under `timeout 600`, so a hung build is killed at 600s regardless. An
# inner limit larger than that can never fire, and a smaller one would make a
# gate group's verdict depend on machine load.
ctl=1
( cd "$W/mod" && go build -p 4 -o "$W/probe" ./g/probe ) > "$W/log/control.log" 2>&1 && ctl=0
rep control-core-packages-are-importable "$ctl" "a probe importing ct, asn1, client, jsonclient, merkletree, tls, trillian/ctfe, x509 and x509util did not build"
if [ "$ctl" != 0 ]; then finish; fi

# ---- the build ------------------------------------------------------------------
rc=1
( cd "$tree" && go build -p 4 -o "$W/bin/" ./... ) > "$W/log/build.log" 2>&1 && rc=0
rep every-package-in-the-tree-compiles "$rc" "go build ./... failed; $(head -c 400 "$W/log/build.log" | tr '\n' ' ')"

# Third-party cgo dependencies of this vintage emit compiler warnings of their
# own; only diagnostics naming the product's own packages are the product's.
grep -F "github.com/google/certificate-transparency-go" "$W/log/build.log" > "$W/log/own.log" 2>/dev/null
quiet=1
[ -s "$W/log/own.log" ] || quiet=0
rep the-build-emits-no-diagnostics-about-the-products-own-packages "$quiet" \
    "go build printed: $(head -c 400 "$W/log/own.log" | tr '\n' ' ')"

# ---- the shipped programs ---------------------------------------------------------
want="chainfix ct_hammer ct_server ctclient dumpscts gossip_server preloader scanlog"
for b in $want; do
    ok=1
    [ -x "$W/bin/$b" ] && [ -s "$W/bin/$b" ] && ok=0
    rep "builds-the-$b-command" "$ok" "no executable $b was produced by go build ./..."
done

# ---- the flag grammar, on the one binary this group already has --------------------
# TEST_INTERFACE.md section 4: an unrecognised flag is the standard flag package's
# exit 2 with the usage dump on stderr. Only the "Usage of <prog>:" banner and the
# status are graded; the flag list under it and the diagnostic line above it are the
# standard library's own rendering, which no page fixes.
if [ -x "$W/bin/ct_server" ]; then
    "$W/bin/ct_server" --no_such_flag > "$W/log/flag.out" 2> "$W/log/flag.err"
    fc=$?
    gram=1
    [ "$fc" = 2 ] && grep -q "Usage of " "$W/log/flag.err" && [ ! -s "$W/log/flag.out" ] && gram=0
    rep ct_server-rejects-an-unrecognised-flag-with-exit-2 "$gram" \
        "exit=$fc stdout=$(head -c 120 "$W/log/flag.out" | tr '\n' ' ') stderr=$(head -c 200 "$W/log/flag.err" | tr '\n' ' ')"
    # The root package declares allow_verification_with_non_compliant_keys as a
    # `flag` global, so it reaches the usage dump of every program that links the
    # root package -- ct_server among them (CLI.md section 2.1, the note under the
    # table). Only the flag's presence in that listing is graded, not its help text.
    ncf=1
    grep -q -- '-allow_verification_with_non_compliant_keys' "$W/log/flag.err" && ncf=0
    rep the-root-package-flag-reaches-ct_server-usage "$ncf" \
        "not listed among $(grep -c -- '^  -' "$W/log/flag.err" 2>/dev/null || echo 0) flags in the usage dump"
else
    rep ct_server-rejects-an-unrecognised-flag-with-exit-2 1 "no ct_server binary was produced"
    rep the-root-package-flag-reaches-ct_server-usage 1 "no ct_server binary was produced"
fi

got=$( (cd "$W/bin" && ls -1) 2>/dev/null | sort | tr '\n' ' ' | sed 's/ $//')
exp=$(printf '%s\n' $want | sort | tr '\n' ' ' | sed 's/ $//')
sameset=1
[ "$got" = "$exp" ] && sameset=0
rep the-tree-ships-exactly-these-eight-commands "$sameset" "built [$got], expected [$exp]"

finish
