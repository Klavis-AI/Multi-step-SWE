#!/usr/bin/env bash
# binaries-fourteen-mains
#
# v1.3.3 is where the program inventory stops moving, and it stops one subtree
# smaller: `internal/witness` is deleted with its four `package main`
# directories (cmd/client, cmd/feeder, cmd/witness, cmd/witness/config), so the
# tree falls from 18 programs at v1.2.0 to 14 (CLI.md section 1, RELEASE-MAP.md
# section 14). WORKSPACE_HANDOFF.md hands the candidate that number as a
# checkpoint; this group is what grades it.
#
# The inventory is observed the way the checkpoint says to observe it -- by
# building every `package main` directory and looking at the binaries produced
# -- never with `ls` over the source tree. The build happens inside the
# throwaway ctverify module, whose "replace" already points the product at the
# candidate's tree, so nothing is written into that tree and the output
# directory holds the candidate's programs and nothing else.
#
# It deliberately does NOT count $CT_BIN: that directory is the verifier's own
# workspace, and by the time a group runs it also holds every driver binary
# compiled so far. $CT_BIN is only used for what it is good for -- running a
# named program.
#
# Cases 6-7 are the other command-line milestone of this release that nothing
# else observes: ct_hammer's --bearer_token, which is the command-line face of
# jsonclient.Options.Authorization (CLI.md section 4.8). --start_index is
# checked beside it as an inherited control: it has been there since v1.1.0
# and must not disappear with the change.
#
# Cases 8-9 are ct_server's flag set, which CLI.md section 2.1 enumerates for
# every release and totals at 31 for v1.3.3. Both halves of that total are
# graded from the table alone: all 31 names appear in the help output, and the
# two flags the table marks as gone by v1.3.3 -- by_range and
# allow_verification_with_non_compliant_keys -- do not. The logging library's
# own flags are deliberately not counted, exactly as the table says, so this
# never asserts a bare total over whatever klog happens to register.
#
# Against v1.2.0 this group cannot pass: that tree builds 18 programs, four of
# them the witness ones, and its ct_hammer has no --bearer_token.
set -u
N="$CT_VERIFY_NONCE"
ex=0
fa=0
pass() { ex=$((ex + 1)); printf '%s CASE %s: PASS\n' "$N" "$1"; }
fail() { ex=$((ex + 1)); fa=$((fa + 1)); printf '%s CASE %s: FAIL %s\n' "$N" "$1" "$2"; }

export GOFLAGS="-mod=mod"
export GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4

MOD="$CT_SCRATCH/mod"
BINS="$CT_SCRATCH/mains"
mkdir -p "$MOD" "$BINS"
cp -a "$CT_ASSETS/verifier/." "$MOD/"

# The fourteen programs of this release, sorted, and the exit status each one
# gives its own `-h`: 0 everywhere except chainfix, whose argument handling
# treats a help request as a usage error (this is unchanged since v1.0).
want_list='certcheck chainfix crlcheck ct_hammer ct_server ctclient dumpscts hammer migrillian preloader scanlog sctcheck sctscan server'
want_n=14

# --- case 1: control. Every `package main` directory in the product builds,
# and the binaries land where the rest of this group can look at them.
if ( cd "$MOD" && go build -p 4 -o "$BINS/" \
        github.com/google/certificate-transparency-go/... ) \
     > "$CT_SCRATCH/mains.log" 2>&1; then
  pass control-every-package-main-directory-builds
else
  fail control-every-package-main-directory-builds \
       "$(tr '\n' '|' < "$CT_SCRATCH/mains.log" | cut -c1-600)"
  printf '%s SUMMARY executed=%d failed=%d\n' "$N" "$ex" "$fa"
  exit 0
fi

got_list=$(ls "$BINS" | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//')
got_n=$(ls "$BINS" | wc -l)

# --- case 2: and there are exactly fourteen of them.
if [ "$got_n" -eq "$want_n" ]; then
  pass exactly-fourteen-package-main-directories
else
  fail exactly-fourteen-package-main-directories \
       "the product built $got_n programs, want $want_n: [$got_list]"
fi

# --- case 3: the inventory is this release's, name for name.
if [ "$got_list" = "$want_list" ]; then
  pass the-program-inventory-is-this-releases-fourteen
else
  fail the-program-inventory-is-this-releases-fourteen \
       "built [$got_list], want [$want_list]"
fi

# --- case 4: and none of the four withdrawn witness programs is among them.
# Stated on its own because it is the milestone, not a side effect of case 3:
# `internal/witness/cmd/{client,feeder,witness,witness/config}` all leave here.
found=''
for w in client config feeder witness; do
  [ -e "$BINS/$w" ] && found="$found $w"
done
if [ -z "$found" ]; then
  pass none-of-the-four-witness-programs-is-built
else
  fail none-of-the-four-witness-programs-is-built \
       "the deleted subtree still builds:$found"
fi

# --- case 5: every one of them runs and answers a help request.
bad=''
for b in $want_list; do
  want_rc=0
  [ "$b" = chainfix ] && want_rc=2
  if [ ! -x "$BINS/$b" ]; then
    bad="$bad $b(absent)"
    continue
  fi
  timeout 60 env -u CT_VERIFY_NONCE "$BINS/$b" -h > "$CT_SCRATCH/help.out" 2>&1
  rc=$?
  [ "$rc" -eq "$want_rc" ] || bad="$bad $b(exit $rc, want $want_rc)"
done
if [ -z "$bad" ]; then
  pass every-program-answers-a-help-request
else
  fail every-program-answers-a-help-request "$bad"
fi

# --- cases 6-7: ct_hammer's own flag set. The binary run here is the one the
# verifier built from the candidate's tree before any group started.
hammer_flags() {  # hammer_flags <flag-name>
  timeout 60 env -u CT_VERIFY_NONCE "$CT_BIN/ct_hammer" -h 2>&1 \
    | sed -n 's/^  -\([a-z_0-9]*\).*/\1/p' | grep -qx "$1"
}
if [ ! -x "$CT_BIN/ct_hammer" ]; then
  fail ct-hammer-answers-to-the-new-bearer-token-flag "\$CT_BIN/ct_hammer was not built"
  fail ct-hammer-still-answers-to-start-index "\$CT_BIN/ct_hammer was not built"
else
  if hammer_flags bearer_token; then
    pass ct-hammer-answers-to-the-new-bearer-token-flag
  else
    fail ct-hammer-answers-to-the-new-bearer-token-flag \
         "ct_hammer -h does not list a bearer_token flag"
  fi
  if hammer_flags start_index; then
    pass ct-hammer-still-answers-to-start-index
  else
    fail ct-hammer-still-answers-to-start-index \
         "ct_hammer -h no longer lists the start_index flag"
  fi
fi

# --- cases 8-9: ct_server's flag set, name for name against CLI.md 2.1.
# ct_server_flags is the flag names its own help output lists, one per line.
ct_server_flags() {
  timeout 60 env -u CT_VERIFY_NONCE "$CT_BIN/ct_server" -h 2>&1 \
    | sed -n 's/^  -\([a-z_0-9]*\).*/\1/p'
}
# The 31 of CLI.md 2.1's v1.3.3 column, in the table's own order.
want_flags='http_endpoint metrics_endpoint log_rpc_server rpc_deadline
get_sth_interval log_config max_get_entries etcd_servers etcd_http_service
etcd_metrics_service tracing tracing_project_id tracing_percent quota_remote
quota_intermediate handler_prefix mask_internal_errors pkcs11_module_path
cache_type cache_size cache_ttl http_idle_timeout tls_certificate tls_key
trillian_tls_ca_cert_file non_fresh_submission_age non_fresh_submission_burst
non_fresh_submission_limit max_cert_chain_size align_getentries
getentries_metrics'
# The two the same table marks absent at v1.3.3.
gone_flags='by_range allow_verification_with_non_compliant_keys'

if [ ! -x "$CT_BIN/ct_server" ]; then
  fail ct-server-lists-every-flag-of-this-release "\$CT_BIN/ct_server was not built"
  fail ct-server-lists-neither-withdrawn-flag "\$CT_BIN/ct_server was not built"
else
  ct_server_flags > "$CT_SCRATCH/ctserver-flags.txt"
  missing=''
  n=0
  for f in $want_flags; do
    n=$((n + 1))
    grep -qx "$f" "$CT_SCRATCH/ctserver-flags.txt" || missing="$missing $f"
  done
  if [ -z "$missing" ] && [ "$n" -eq 31 ]; then
    pass ct-server-lists-every-flag-of-this-release
  else
    fail ct-server-lists-every-flag-of-this-release \
         "ct_server -h is missing $(echo $missing | wc -w) of the 31 flags CLI.md 2.1 gives v1.3.3:$missing"
  fi
  present=''
  for f in $gone_flags; do
    grep -qx "$f" "$CT_SCRATCH/ctserver-flags.txt" && present="$present $f"
  done
  if [ -z "$present" ]; then
    pass ct-server-lists-neither-withdrawn-flag
  else
    fail ct-server-lists-neither-withdrawn-flag \
         "ct_server -h still lists:$present"
  fi
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$N" "$ex" "$fa"
exit 0
