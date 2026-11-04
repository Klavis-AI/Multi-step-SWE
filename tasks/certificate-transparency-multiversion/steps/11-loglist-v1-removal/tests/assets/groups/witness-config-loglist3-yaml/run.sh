#!/bin/bash
# witness-config-loglist3-yaml
#
# The witness "config" tool (internal/witness/cmd/witness/config) is the one
# product binary whose whole job is to turn a CT log list into a file. At
# v1.1.4 it reads the list with loglist3 and marshals with gopkg.in/yaml.v3,
# and it no longer installs a logging flag set at all; at v1.1.3 it read the
# list with loglist2 and marshalled with gopkg.in/yaml.v2, whose block
# sequences are not indented under their key, and it carried glog's seven
# flags. Both differences are observed from outside the process: the exact
# bytes of the file it writes, and the flag grammar it accepts.
set -u
n=0; f=0
pass() { printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; n=$((n+1)); }
fail() { printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; n=$((n+1)); f=$((f+1)); }

LIST="$CT_GROUP_DIR/bench-loglist3.json"
CFG="$CT_SCRATCH/out.yaml"
BIN="$CT_BIN/config"

# --- case 1: the tool runs at all and reports success.
env -u CT_VERIFY_NONCE "$BIN" \
    --log_list_url="file://$LIST" \
    --config_file="$CFG" > "$CT_SCRATCH/run1.txt" 2>&1
rc=$?
if [ "$rc" -eq 0 ] && [ -f "$CFG" ]; then
  pass control-config-tool-writes-a-config
else
  fail control-config-tool-writes-a-config "rc=$rc, config written: $([ -f "$CFG" ] && echo yes || echo no)"
  printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
  exit 0
fi

# --- case 2: exactly the two usable logs, in log-list order.
want_keys='MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEV2CUU8bUtg09CCZpDyuAr3O44pJPOlTfjIsGUR5TcsVOIb/LLXf/KbMFpvpPl4gTjBRqlpwPPXTLCWWa15H6yQ==
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEmXg8sUUzwBYaWrRb+V0IopzQ6o3UyEJ04r5ZrRXGdpYM8K+hB0pXrGRLI0eeWz+3skXrS0IO83AhA3GpRL6s6w=='
got_keys=$(sed -n 's/^ *- PubKey: //p' "$CFG")
if [ "$got_keys" = "$want_keys" ]; then
  pass usable-logs-only-in-list-order
else
  fail usable-logs-only-in-list-order "got [$got_keys]"
fi

# --- case 3: no key belonging to a non-usable log leaked in. Every "key" in
# the pinned list is distinct, so any PubKey line other than the two wanted
# ones names a readonly, retired, qualified or pending log.
extra=$(sed -n 's/^ *- PubKey: //p' "$CFG" | grep -vxF "$want_keys" || true)
count=$(sed -n 's/^ *- PubKey: //p' "$CFG" | wc -l)
if [ -z "$extra" ] && [ "$count" -eq 2 ]; then
  pass non-usable-logs-are-excluded
else
  fail non-usable-logs-are-excluded "count=$count unexpected=[$extra]"
fi

# --- case 4: the file is byte-for-byte the yaml.v3 rendering.
want_sha=d460e303ed2ea47e9e0d646c38e43f6aab39da81bcc2bcf4300fb36baa3f8d70
got_sha=$(sha256sum "$CFG" | cut -d' ' -f1)
if [ "$got_sha" = "$want_sha" ]; then
  pass config-file-is-exact-bytes
else
  fail config-file-is-exact-bytes "sha256=$got_sha want=$want_sha"
fi

# --- case 5: block sequence entries are indented under their key (yaml.v3).
first=$(sed -n '2p' "$CFG")
if [ "$(sed -n '1p' "$CFG")" = "Logs:" ] && [ "${first:0:12}" = "    - PubKey" ]; then
  pass sequence-is-indented-under-its-key
else
  fail sequence-is-indented-under-its-key "line1=[$(sed -n '1p' "$CFG")] line2=[$first]"
fi

# --- case 6: the flag grammar is exactly the two documented flags, with the
# default CLI.md 4.10 gives --log_list_url. That default does not move at this
# release -- it already named a v3 document at the previous one -- so it is a
# control on the retype: the tool changes the parser behind the flag and
# nothing about the flag itself.
env -u CT_VERIFY_NONCE "$BIN" --help > "$CT_SCRATCH/help.txt" 2>&1
flags=$(sed -n 's/^  -\([A-Za-z_0-9]*\).*/\1/p' "$CT_SCRATCH/help.txt" | sort | tr '\n' ' ')
if [ "$flags" = "config_file log_list_url " ] \
   && grep -qF '(default "https://www.gstatic.com/ct/log_list/v3/log_list.json")' "$CT_SCRATCH/help.txt"; then
  pass flag-set-is-exactly-config-file-and-log-list-url
else
  fail flag-set-is-exactly-config-file-and-log-list-url "flags=[$flags]"
fi

# --- case 7: an unusable URL scheme is refused, and nothing is written.
rm -f "$CT_SCRATCH/never.yaml"
env -u CT_VERIFY_NONCE "$BIN" \
    --log_list_url='ftp://bench.invalid/log_list.json' \
    --config_file="$CT_SCRATCH/never.yaml" > "$CT_SCRATCH/run2.txt" 2>&1
rc=$?
# Graded: the scheme is refused (CLI.md 4.10 gives the accepted set as http,
# https and file), nothing is written, and the refusal names the URL it was
# given. The wording of the refusal is upstream's and is not graded.
if [ "$rc" -ne 0 ] && [ ! -f "$CT_SCRATCH/never.yaml" ] \
   && grep -qF 'ftp://bench.invalid/log_list.json' "$CT_SCRATCH/run2.txt"; then
  pass unsupported-url-scheme-is-refused
else
  fail unsupported-url-scheme-is-refused "rc=$rc out=[$(tr '\n' '|' < "$CT_SCRATCH/run2.txt")]"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
