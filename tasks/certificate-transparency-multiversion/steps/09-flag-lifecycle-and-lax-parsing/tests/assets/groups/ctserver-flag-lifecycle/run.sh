#!/usr/bin/env bash
# Group ctserver-flag-lifecycle.
#
# --by_range has its whole life inside this step: v1.1.1 changed its default to
# true, v1.1.2 removed it. At the v1.1.2 anchor ct_server must not define it at
# all, so passing it is an undefined-flag error (exit status 2) rather than a
# configuration error (exit status 1).
#
# The two flags that arrive in the same step, --align_getentries (default true)
# and --pkcs11_module_path, must be documented in the usage text.
#
# Observed only at the process boundary: exit status and the usage the binary
# writes to stderr -- the two streams are captured separately, never merged
# (see run() below). Nothing the binary prints reaches this group's stdout, and
# the nonce is stripped from its environment.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

BIN="$CT_BIN/ct_server"
if [ ! -x "$BIN" ]; then
    for id in by_range-flag-is-undefined by_range-absent-from-usage \
              align_getentries-documented-with-default-true \
              pkcs11_module_path-documented undefined-flag-exit-status \
              declared-flag-set-is-exactly-nineteen; do
        fail "$id" "ct_server was not built"
    done
    printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
    exit 0
fi

run() { # run <stdout-file> <stderr-file> <args...>; echoes the exit status
    # The two streams are captured SEPARATELY and never merged. CLI.md's
    # preamble puts the `Usage of <prog>:` block on **stderr** on both paths --
    # the -h/--help one and the unrecognised-flag one -- and at this release
    # ct_server writes nothing whatever to stdout for either: it calls plain
    # flag.Parse() and nothing in the tree calls flag.CommandLine.SetOutput, so
    # the block goes to the flag package's default output, os.Stderr. Grepping
    # the union of the two streams would pass a binary that dumped its usage to
    # stdout instead, which is not what the contract says.
    local out="$1" err="$2"; shift 2
    env -u CT_VERIFY_NONCE timeout 60 "$BIN" "$@" > "$out" 2> "$err" < /dev/null
    echo $?
}

# Every usage assertion below reads this file, which is stderr and only stderr.
USAGE="$CT_SCRATCH/usage.err"
run "$CT_SCRATCH/usage.out" "$USAGE" --help > /dev/null

# --- 1. --by_range is no longer a flag -------------------------------------
# Exit status 2 is the flag package refusing an undefined flag. A milestone
# that still defines --by_range accepts it and then fails on the missing config
# file instead, which is exit status 1.
rc=$(run "$CT_SCRATCH/byrange.out" "$CT_SCRATCH/byrange.err" \
         --by_range=true --log_config="$CT_SCRATCH/no-such-config")
if [ "$rc" = 2 ]; then
    pass by_range-flag-is-undefined
else
    fail by_range-flag-is-undefined "exit status $rc, want 2"
fi

# --- 2. and it is gone from the usage text ---------------------------------
# Anchored on the block being present. Now that only stderr is graded, an
# empty stderr would satisfy an absence test vacuously, so the documented first
# line of the block (TEST_INTERFACE.md section 4) has to be there before
# anything is concluded from what is not.
if ! grep -q '^Usage of ' "$USAGE"; then
    fail by_range-absent-from-usage "no 'Usage of ...:' block on stderr"
elif grep -q '^  -by_range' "$USAGE"; then
    fail by_range-absent-from-usage "usage still documents -by_range"
else
    pass by_range-absent-from-usage
fi

# --- 3. --align_getentries is documented, and defaults to true -------------
# Contract here is the flag's name, that it is a Bool -- the flag package prints
# no type word for one, so the line is bare -- and that its default is true,
# which the flag package renders as a trailing "(default true)" in the
# description block (CLI.md §2.1). The wording of the description is the
# implementation's own prose and is deliberately NOT compared: no group in this
# package grades the text of a message it did not specify.
if ! grep -q -x -e '  -align_getentries' "$USAGE"; then
    fail align_getentries-documented-with-default-true \
         "usage has no bare '  -align_getentries' line"
else
    desc=$(awk '/^  -align_getentries$/ { inblk = 1; next }
                inblk && /^  -/         { exit }
                inblk                   { print }' "$USAGE")
    case $desc in
        *"(default true)"*)
            pass align_getentries-documented-with-default-true ;;
        *)
            fail align_getentries-documented-with-default-true \
                 "description block does not show '(default true)': [$desc]" ;;
    esac
fi

# --- 4. --pkcs11_module_path is documented as a string flag ----------------
if grep -q -x -e '  -pkcs11_module_path string' "$USAGE"; then
    pass pkcs11_module_path-documented
else
    fail pkcs11_module_path-documented "usage has no '  -pkcs11_module_path string' line"
fi

# --- 5. control: 2 really is what this binary returns for an undefined flag -
rc=$(run "$CT_SCRATCH/bogus.out" "$CT_SCRATCH/bogus.err" \
         --definitely_not_a_ct_server_flag=1)
if [ "$rc" = 2 ]; then
    pass undefined-flag-exit-status
else
    fail undefined-flag-exit-status "exit status $rc, want 2"
fi

# --- 6. the whole declared set, not just the three flags above -------------
# CLI.md §2.1's totals row puts ct_server at 19 flags at this release, counting
# every flag in --help except the logging library's own seven. Checking the
# exact sorted set -- not just that the three interesting ones are right -- is
# what catches a package-scope `flag` global left behind anywhere in the ctfe
# import graph, which is the failure mode this milestone's subtraction invites.
# Step 08 graded the same list at 18 for the same reason.
GLOG="alsologtostderr log_backtrace_at log_dir logtostderr stderrthreshold v vmodule"
WANT="align_getentries etcd_http_service etcd_metrics_service etcd_servers
      get_sth_interval handler_prefix http_endpoint log_config log_rpc_server
      mask_internal_errors max_get_entries metrics_endpoint pkcs11_module_path
      quota_intermediate quota_remote rpc_deadline tracing tracing_percent
      tracing_project_id"

want=$(printf '%s\n' $WANT | LC_ALL=C sort | tr '\n' ' ')
got=$(sed -n 's/^  -\([A-Za-z0-9_]*\).*/\1/p' "$USAGE" \
      | grep -v -x -F "$(printf '%s\n' $GLOG)" \
      | LC_ALL=C sort -u | tr '\n' ' ')
if [ "$got" = "$want" ]; then
    pass declared-flag-set-is-exactly-nineteen
else
    fail declared-flag-set-is-exactly-nineteen "non-glog flags are [$got], want [$want]"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
