#!/usr/bin/env bash
# run-cases.sh <wild-binary> <cases-root> [case-id ...]
# Runs each case in a private scratch directory and prints "<case-id> <status>"
# where status is ok | fail | skip | timeout. Called by the verifier driver,
# test.sh.
set -uo pipefail
WILD=$(readlink -f "$1"); CASES=$(readlink -f "$2"); shift 2
LIB=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SCRATCH=${SCRATCH:-$(mktemp -d)}
PER_CASE_TIMEOUT=${PER_CASE_TIMEOUT:-60}

all_ids() { (cd "$CASES" && find . -name '*.case' -printf '%P\n' | sed 's/\.case$//' | LC_ALL=C sort); }

# A selector is either a full case id (group/name) or a group directory name --
# or any prefix of an id, which is what makes "s04-linker-scripts" work.
list() {
  if [ $# -eq 0 ]; then all_ids; return; fi
  local sel hit any
  for sel in "$@"; do
    if [ -f "$CASES/$sel.case" ]; then printf '%s\n' "$sel"; continue; fi
    # First try the selector as a whole path component -- a case id or a group
    # name. Failing that, treat it as a bare prefix, which is what makes
    # `s03-` (every group of step 3) work as well as `s03-tls`.
    hit=$(all_ids | grep -E "^${sel}(/|$)" || true)
    [ -n "$hit" ] || hit=$(all_ids | grep -E "^${sel}" || true)
    if [ -n "$hit" ]; then printf '%s\n' "$hit"
    else echo "$sel" ; fi
  done
}

for id in $(list "$@"); do
  f=$CASES/$id.case
  [ -f "$f" ] || { echo "$id missing"; continue; }
  arch=$(sed -n 's/^# *arch: *//p' "$f" | head -1); : "${arch:=x86_64}"
  d=$SCRATCH/$(echo "$id" | tr '/' '_')
  rm -rf "$d"; mkdir -p "$d"
  out=$( cd "$d" && WILD=$WILD ARCH=$arch LIB=$LIB \
           timeout "$PER_CASE_TIMEOUT" bash -c 'source "$LIB/caselib.sh"; source "$0"' "$f" 2>&1 )
  rc=$?
  # A bash word-expansion error -- classically `$(( 16#$(helper) ))` where the
  # helper returned nothing, so the expansion is the invalid literal `16#` --
  # abandons that one command and lets the case continue. The assertion is
  # skipped rather than run, and the case still exits 0. `asserts_at_least`
  # catches that in a case with counted assertions; this catches it in a case
  # that verifies only through run_expect or `... || die`, which bump no
  # counter and so can carry no floor.
  # Matched against bash's own `<file>: line N: <message>` diagnostic shape, so
  # a linker diagnostic that happens to contain "syntax error" -- which the
  # linker-script cases do provoke deliberately -- cannot trip it.
  if [ $rc -eq 0 ] && expansion=$(printf '%s' "$out" | grep -m1 -E \
       ': line [0-9]+: .*(invalid integer constant|value too great for base|syntax error in expression|syntax error: operand expected|invalid arithmetic operator|division by 0|unbound variable)'); then
    echo "$id fail skipped-assertion: $(printf '%s' "$expansion" | head -c 180)"
    [ -n "${KEEP_SCRATCH:-}" ] || rm -rf "$d"
    continue
  fi
  case $rc in
    0)   echo "$id ok" ;;
    77)  echo "$id skip" ;;
    124) echo "$id timeout" ;;
    *)   echo "$id fail $(printf '%s' "$out" | grep -m1 -E 'CASE-FAIL|CASE-SKIP' | head -c 220)" ;;
  esac
  [ -n "${KEEP_SCRATCH:-}" ] || rm -rf "$d"
done
