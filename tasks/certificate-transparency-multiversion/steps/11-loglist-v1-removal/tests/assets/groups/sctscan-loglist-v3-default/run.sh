#!/bin/bash
# sctscan-loglist-v3-default
#
# sctscan validates the SCTs embedded in a log's certificates against the
# public log list. At v1.1.4 it reads that list with loglist3 and builds its
# key-hash map with ctutil.LogInfoByKeyHash; at v1.1.3 it read it with the v1
# loglist package, whose schema has no per-operator "logs" member, so a v3
# file yields an empty map and every entry in the file is silently ignored.
# The observation below is a v3 list with one deliberately corrupt key: only
# a reader that actually walks the v3 operators can notice it, and the error
# it prints names that log's v3 description.
#
# Fully offline: --log_uri is loopback port 1, which refuses instantly.
set -u
n=0; f=0
pass() { printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; n=$((n+1)); }
fail() { printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; n=$((n+1)); f=$((f+1)); }

BIN="$CT_BIN/sctscan"
GOOD="$CT_GROUP_DIR/bench-loglist3.json"
BAD="$CT_GROUP_DIR/badkey-loglist3.json"
DEAD=http://127.0.0.1:1/

env -u CT_VERIFY_NONCE "$BIN" --help > "$CT_SCRATCH/help.txt" 2>&1
helprc=$?

# --- case 1: control. The binary runs and documents --log_list.
if [ "$helprc" -eq 0 ] && grep -qF 'Location of master CT log list (URL or filename)' "$CT_SCRATCH/help.txt"; then
  # The usage text above is the one CLI.md section 4.4 gives for sctcheck and
  # sctscan's --log_list, and is stable at every release that has them.
  pass control-sctscan-help-exits-zero
else
  fail control-sctscan-help-exits-zero "rc=$helprc"
  printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
  exit 0
fi

# --- case 2: the documented default list is the v3 one.
if grep -qF '(default "https://www.gstatic.com/ct/log_list/v3/all_logs_list.json")' "$CT_SCRATCH/help.txt"; then
  pass default-log-list-is-the-v3-all-logs-url
else
  fail default-log-list-is-the-v3-all-logs-url "[$(grep -A1 -F -- '  -log_list ' "$CT_SCRATCH/help.txt" | tr '\n' '|')]"
fi

# --- case 3: the flag set is exactly v1.1.4's -- klog's six extra flags are
# present and nothing else changed.
flags=$(sed -n 's/^  -\([A-Za-z_0-9]*\).*/\1/p' "$CT_SCRATCH/help.txt" | sort | tr '\n' ' ')
want='add_dir_header alsologtostderr batch_size deadline inclusion log_backtrace_at log_dir log_file log_file_max_size log_list log_uri logtostderr num_workers one_output parallel_fetch skip_headers skip_log_headers start_index stderrthreshold v vmodule '
if [ "$flags" = "$want" ]; then
  pass flag-set-is-exactly-the-klog-surface
else
  fail flag-set-is-exactly-the-klog-surface "flags=[$flags]"
fi

# --- case 4: a corrupt key inside the v3 list is reported, and the report
# names the log by its v3 description.
timeout 60 env -u CT_VERIFY_NONCE "$BIN" --log_list="$BAD" --log_uri="$DEAD" --logtostderr \
    > "$CT_SCRATCH/bad.txt" 2>&1
rc=$?
# Graded: it refused, and the log it refused over is identified by the v3
# description only a v3 reader ever sees. The wording upstream wraps that
# description in is not graded.
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ] \
   && grep -qF 'Shop Usable Log' "$CT_SCRATCH/bad.txt"; then
  pass corrupt-v3-key-is-reported-by-description
else
  fail corrupt-v3-key-is-reported-by-description "rc=$rc out=[$(tr '\n' '|' < "$CT_SCRATCH/bad.txt")]"
fi

# --- case 5: the map is never built when the corrupt key stops it, so the
# scan itself is not reached.
if ! grep -qF '/ct/v1/get-sth' "$CT_SCRATCH/bad.txt"; then
  pass corrupt-v3-key-stops-before-the-scan
else
  fail corrupt-v3-key-stops-before-the-scan "scan was attempted"
fi

# --- case 6: with every key well formed the map is built and the scan is the
# next thing that happens.
timeout 60 env -u CT_VERIFY_NONCE "$BIN" --log_list="$GOOD" --log_uri="$DEAD" --logtostderr \
    > "$CT_SCRATCH/good.txt" 2>&1
rc=$?
# Graded: the map was built (no log was refused over its key) and the process
# got as far as asking --log_uri for an STH. What is matched is the documented
# endpoint path and nothing else -- not the wording around the dial failure,
# and not the rendering the standard library gives a failed URL, which is no
# part of this step's contract. Case 5 above is the same string as a negative.
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ] \
   && grep -qF '/ct/v1/get-sth' "$CT_SCRATCH/good.txt"; then
  pass well-formed-v3-list-reaches-the-scan
else
  fail well-formed-v3-list-reaches-the-scan "rc=$rc out=[$(tr '\n' '|' < "$CT_SCRATCH/good.txt")]"
fi

# --- case 7: sctcheck, sctscan's sibling in ctutil/, moved its own --log_list
# default to the same v3 constant in the same release. It is the other half of
# TEST_INTERFACE requirement R4 and nothing else in this step observes it. The
# usage line and the default are both CLI.md section 4.4's.
env -u CT_VERIFY_NONCE "$CT_BIN/sctcheck" --help > "$CT_SCRATCH/chkhelp.txt" 2>&1
chkrc=$?
if [ "$chkrc" -eq 0 ] \
   && grep -qF 'Location of master CT log list (URL or filename)' "$CT_SCRATCH/chkhelp.txt" \
   && grep -qF '(default "https://www.gstatic.com/ct/log_list/v3/all_logs_list.json")' "$CT_SCRATCH/chkhelp.txt"; then
  pass sctcheck-default-log-list-is-the-v3-all-logs-url
else
  fail sctcheck-default-log-list-is-the-v3-all-logs-url "rc=$chkrc [$(grep -A1 -F -- '  -log_list ' "$CT_SCRATCH/chkhelp.txt" | tr '\n' '|')]"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
