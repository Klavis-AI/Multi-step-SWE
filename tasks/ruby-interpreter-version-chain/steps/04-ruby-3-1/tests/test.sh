#!/usr/bin/env bash
# The score must leave the scorer in a channel the candidate cannot reach, and
# this script must not be re-readable mid-execution.  bash reads a script
# incrementally as it runs, so a writable entrypoint can have its tail rewritten
# after the score is decided; defining the whole body as a function and calling
# it at the end means bash has parsed all of it before any of it runs.
set -u
_verify() {
  mkdir -p /logs/verifier
  # Harbor creates /logs/verifier 0777 without the sticky bit for each step, so
  # the candidate uid (4242) could unlink this run's root-owned run.log and
  # recreate it with a forged REWARD line carrying the inherited nonce, which the
  # pid sweep (it only catches surviving processes) would not prevent. The
  # exposure is DIRECTORY write, not file write. Remove world/group write before
  # any candidate code runs, then check that it held; if it did not, score 0.
  chmod 755 /logs/verifier
  # Primary check: attempt a create AS uid 4242 via setpriv (no PAM). Only a
  # confirmed create fails closed.
  if command -v setpriv >/dev/null 2>&1; then
    setpriv --reuid 4242 --regid 4242 --clear-groups /bin/sh -c ": > /logs/verifier/.hardencheck" 2>/dev/null || true
    if [ -e /logs/verifier/.hardencheck ]; then
      rm -f /logs/verifier/.hardencheck
      echo "FATAL: candidate uid could create a file in /logs/verifier -- forgeable"
      printf '{"reward": 0.0}\n' > /logs/verifier/reward.json; exit 0
    fi
  fi
  # Backstop: mode bits must show no group/other write (correct on a mode-bit FS).
  perm=$(stat -c '%a' /logs/verifier)
  case "$perm" in *[2367]) echo "FATAL: /logs/verifier still group/other-writable ($perm)"; printf '{"reward": 0.0}\n' > /logs/verifier/reward.json; exit 0;; esac
  local NONCE RESULT
  NONCE=$(head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n')

  # every pid alive before the scorer starts is legitimate; anything new that is
  # still alive afterwards is swept.  An allowlist, not a pattern match: a
  # payload whose command line names no candidate path defeats a denylist.
  local BEFORE
  BEFORE=" $(ls /proc | grep -E '^[0-9]+$' | tr '\n' ' ')"

  # Bounded as a whole, below Harbor's 3600 s: a verifier that outlives
  # timeout_sec is discarded with its reward and aborts the steps after it.
  # runner.py keeps its own schedule well inside this; the bound is the
  # backstop, and a run it cuts short has no nonce line, so it scores 0.
  # `cd /` and -I: nothing on the candidate's side of the run is on sys.path.
  cd /
  RUBYCHAIN_NONCE="$NONCE" timeout -k 20 3320 python3 -I /tests/verify/runner.py 2>&1 | tee /logs/verifier/run.log

  local pid
  for pid in $(ls /proc | grep -E '^[0-9]+$'); do
    [ "$pid" = "$$" ] && continue
    case "$BEFORE" in *" $pid "*) continue;; esac
    kill -9 "$pid" 2>/dev/null || true
  done

  RESULT=$(grep -a "^REWARD $NONCE " /logs/verifier/run.log | tail -1 | awk '{print $3}')
  if [ -z "${RESULT:-}" ]; then
    echo "no scorer line carrying this run's nonce; reporting 0.0"
    RESULT=0.0
  fi
  RESULT=$(awk -v r="${RESULT:-0}" 'BEGIN { print (r >= 1.0) ? "1.0" : "0.0" }')
  printf '{"reward": %s}\n' "$RESULT" > /logs/verifier/reward.json
}
_verify "$@"
exit 0
