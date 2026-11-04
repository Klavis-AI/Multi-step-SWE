#!/bin/bash
# ctclient-loglist-v3-default
#
# At v1.1.4 ctclient resolves --log_name against a v3 log list: cmd/root.go
# imports loglist3, its --log_list default is loglist3.AllLogListURL, and the
# whole glog flag set disappeared from its persistent flags because klog's
# flags are now registered after cobra has already copied flag.CommandLine.
# v1.1.3's ctclient parses the same file with the v1 loglist package, which
# has no "operators" member at all, so it finds no log in it whatsoever.
# Everything below is offline: no --log_name here resolves to exactly one log,
# so ctclient never opens a connection.
set -u
n=0; f=0
pass() { printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; n=$((n+1)); }
fail() { printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; n=$((n+1)); f=$((f+1)); }

LIST="$CT_GROUP_DIR/bench-loglist3.json"
BIN="$CT_BIN/ctclient"

env -u CT_VERIFY_NONCE "$BIN" --help > "$CT_SCRATCH/help.txt" 2>&1
helprc=$?

# --- case 1: control. The binary exists, runs, and is the CT client.
if [ "$helprc" -eq 0 ] && grep -qxF 'A command line client for Certificate Transparency logs' "$CT_SCRATCH/help.txt"; then
  pass control-ctclient-help-exits-zero
else
  fail control-ctclient-help-exits-zero "rc=$helprc"
  printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
  exit 0
fi

# --- case 2: the documented default list is the v3 one.
if grep -qF '(default "https://www.gstatic.com/ct/log_list/v3/all_logs_list.json")' "$CT_SCRATCH/help.txt"; then
  pass default-log-list-is-the-v3-all-logs-url
else
  fail default-log-list-is-the-v3-all-logs-url "help does not offer the v3 default: [$(grep -F -- '--log_list' "$CT_SCRATCH/help.txt")]"
fi

# --- case 3: the persistent flag set is exactly the six v1.1.4 flags.
flags=$(sed -n '/^Flags:$/,/^$/p' "$CT_SCRATCH/help.txt" \
        | sed -n 's/^ *\(-[A-Za-z], \)\?--\([A-Za-z_0-9]*\).*/\2/p' | sort | tr '\n' ' ')
want='help log_list log_name log_uri pub_key skip_https_verify '
if [ "$flags" = "$want" ]; then
  pass persistent-flag-set-is-exactly-six
else
  fail persistent-flag-set-is-exactly-six "flags=[$flags] want=[$want]"
fi

# --- case 4: the v3 list really is read -- an ambiguous --log_name is
# answered with the descriptions of the three logs that matched, in list
# order.
env -u CT_VERIFY_NONCE "$BIN" get-sth --log_list="$LIST" --log_name=Bench \
    > "$CT_SCRATCH/multi.txt" 2>&1
rc=$?
# The graded fact is *which* v3 descriptions were matched and in what order,
# not the sentence the binary wraps them in: the diagnostic's wording is
# upstream's and no document gives it.
got4=$(grep -oE 'Google Bench (Usable|Readonly|Qualified) Log' "$CT_SCRATCH/multi.txt" | tr '\n' ',')
want4='Google Bench Usable Log,Google Bench Readonly Log,Google Bench Qualified Log,'
if [ "$rc" -ne 0 ] && [ "$got4" = "$want4" ]; then
  pass ambiguous-log-name-lists-every-match
else
  fail ambiguous-log-name-lists-every-match "rc=$rc matched=[$got4] out=[$(tr '\n' '|' < "$CT_SCRATCH/multi.txt")]"
fi

# --- case 5: a substring that spans two operators matches one log in each.
env -u CT_VERIFY_NONCE "$BIN" get-sth --log_list="$LIST" --log_name=Usable \
    > "$CT_SCRATCH/two.txt" 2>&1
rc=$?
got5=$(grep -oE 'Google Bench Usable Log|Shop Usable Log' "$CT_SCRATCH/two.txt" | tr '\n' ',')
if [ "$rc" -ne 0 ] && [ "$got5" = 'Google Bench Usable Log,Shop Usable Log,' ]; then
  pass match-spans-both-operators
else
  fail match-spans-both-operators "rc=$rc matched=[$got5] out=[$(tr '\n' '|' < "$CT_SCRATCH/two.txt")]"
fi

# --- case 6: an unknown name is reported against the list that was read.
env -u CT_VERIFY_NONCE "$BIN" get-sth --log_list="$LIST" --log_name='No Such Log' \
    > "$CT_SCRATCH/none.txt" 2>&1
rc=$?
# Graded: it refused, it named the list it had been told to read, and it
# matched nothing. The wording of the refusal is not graded.
if [ "$rc" -ne 0 ] \
   && grep -qF "$LIST" "$CT_SCRATCH/none.txt" \
   && grep -qF 'No Such Log' "$CT_SCRATCH/none.txt" \
   && ! grep -qE 'Google Bench|Shop Usable' "$CT_SCRATCH/none.txt"; then
  pass unknown-log-name-names-the-list
else
  fail unknown-log-name-names-the-list "rc=$rc out=[$(tr '\n' '|' < "$CT_SCRATCH/none.txt")]"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
