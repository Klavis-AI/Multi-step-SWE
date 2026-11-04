#!/usr/bin/env bash
# Group binaries-eighteen-mains.
#
# The package's Verified structural record (REQUIREMENTS.md) says the product
# holds 16 `package main` directories at v1.0.21 and 18 at v1.1.0; CLI.md
# section 1 states the same two counts in the agent-visible corpus. The two arrivals are submission/server and submission/hammer, which
# build to $CT_BIN/server and $CT_BIN/hammer.
#
# Each binary is observed as a subprocess: `-h` must exit with the status
# pinned below (0 for seventeen of them; chainfix's own argument handling
# exits 2), and the two new binaries must additionally present exactly the
# flag set upstream gives them. Nothing here inspects a source file.
#
# A v1.0.21 tree builds neither `server` nor `hammer`, so four cases below
# cannot pass against it.
set -u

N="$CT_VERIFY_NONCE"
ex=0
fa=0
pass() { ex=$((ex + 1)); printf '%s CASE %s: PASS\n' "$N" "$1"; }
fail() { ex=$((ex + 1)); fa=$((fa + 1)); printf '%s CASE %s: FAIL %s\n' "$N" "$1" "$2"; }

OUT="$CT_SCRATCH/help.out"

# help_status <binary> <expected exit status>
help_status() {
    local b="$1" want="$2" got
    if [ ! -x "$CT_BIN/$b" ]; then
        fail "binary-$b" "\$CT_BIN/$b was not built"
        return
    fi
    timeout 60 env -u CT_VERIFY_NONCE "$CT_BIN/$b" -h >"$OUT" 2>&1
    got=$?
    if [ "$got" -eq "$want" ]; then
        pass "binary-$b"
    else
        fail "binary-$b" "'$b -h' exited $got, want $want"
    fi
}

# own_flags <binary> <case-id> <expected comma-separated flag list>
# The flag list is the binary's own flags. Two kinds of name are removed
# first, because neither is declared by the binary under test:
#   * glog's seven registers, present in every program in the tree
#     (CLI.md section 2.1);
#   * package-scope `flag` globals declared elsewhere and pulled onto
#     flag.CommandLine through the import graph. At v1.1.0 that is by_range,
#     declared in trillian/ctfe (CLI.md section 2.1 lists it among the flags
#     "not declared in ct_server at all"). Upstream's submission/distributor.go
#     happens to import trillian/ctfe for its root pool, so both new binaries
#     show it; a proxy that keeps no ctfe dependency would not. Which flags a
#     binary inherits from its import graph is not a requirement of this step,
#     so it is filtered rather than graded.
own_flags() {
    local b="$1" id="$2" want="$3" got
    if [ ! -x "$CT_BIN/$b" ]; then
        fail "$id" "\$CT_BIN/$b was not built"
        return
    fi
    timeout 60 env -u CT_VERIFY_NONCE "$CT_BIN/$b" -h >"$OUT" 2>&1
    got=$(sed -n 's/^  -\([a-z_0-9]*\).*/\1/p' "$OUT" \
        | grep -vxE 'alsologtostderr|log_backtrace_at|log_dir|logtostderr|stderrthreshold|v|vmodule' \
        | grep -vxE 'by_range' \
        | LC_ALL=C sort -u | paste -sd, -)
    if [ "$got" = "$want" ]; then
        pass "$id"
    else
        fail "$id" "$b's own flags are [$got], want [$want]"
    fi
}

# --- the sixteen binaries that already existed at v1.0.21 --------------------
help_status certcheck      0
help_status chainfix       2
help_status crlcheck       0
help_status ct_hammer      0
help_status ct_server      0
help_status ctclient       0
help_status dumpscts       0
help_status findlog        0
help_status goshawk        0
help_status gosmin         0
help_status gossip_server  0
help_status migrillian     0
help_status preloader      0
help_status scanlog        0
help_status sctcheck       0
help_status sctscan        0

# --- the two that arrive with v1.1.0 -----------------------------------------
help_status server         0
help_status hammer         0

own_flags server submission-server-flag-set \
    'add_prechain_timeout,dry_run,http_endpoint,load_pending_qualified_logs,loglist_path,loglist_refresh_interval,policy_type,roots_refresh_interval'
own_flags hammer submission-hammer-flag-set \
    'count,duration,proxy_endpoint,qps'

printf '%s SUMMARY executed=%d failed=%d\n' "$N" "$ex" "$fa"
exit 0
