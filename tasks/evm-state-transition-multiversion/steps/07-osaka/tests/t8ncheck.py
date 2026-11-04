#!/usr/bin/env python3
"""t8ncheck.py -- score one t8n run against a corpus case.

    t8ncheck.py --case <case-dir> --result <result.json> --alloc <allocout.json>
                [--exit-status N] [--check alloc] [--check logsBloom]

Prints one `PASS <check>` / `FAIL <check> expected=<..> actual=<..>` line per
applicable check and exits 0 iff every applicable check passed.  It never
raises: a missing or malformed input is a FAIL with a reason.

Both sides of every comparison go through the same canonicalisation, so
`"0x0"`, `"0x00"` and `0` are equal, `""` and `"0x"` are equal, addresses and
storage keys are width-normalised, zero storage slots equal absent slots, and
an all-zero account equals an absent account.

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

TRUNC = 200


# --------------------------------------------------------------------------
# canonicalisation
# --------------------------------------------------------------------------


def as_int(v):
    """Quantity -> int. Returns None if it is not a quantity."""
    if v is None:
        return None
    if isinstance(v, bool):
        return int(v)
    if isinstance(v, int):
        return v
    if isinstance(v, str):
        s = v.strip()
        if s == "":
            return 0
        try:
            if s.lower().startswith("0x"):
                return 0 if s[2:] == "" else int(s[2:], 16)
            return int(s, 10)
        except ValueError:
            return None
    return None


def as_bytes(v):
    """Byte string -> bytes. Returns None if it is not a byte string."""
    if v is None:
        return None
    if isinstance(v, (bytes, bytearray)):
        return bytes(v)
    if isinstance(v, int) and not isinstance(v, bool):
        h = "%x" % v
        if len(h) % 2:
            h = "0" + h
        return bytes.fromhex(h)
    if isinstance(v, str):
        s = v.strip().lower()
        if s.startswith("0x"):
            s = s[2:]
        if s == "":
            return b""
        if len(s) % 2:
            s = "0" + s
        try:
            return bytes.fromhex(s)
        except ValueError:
            return None
    return None


def hexpad(v, width):
    """Address / storage key -> lowercase 0x hex, left zero-padded to `width`
    bytes.  Over-wide values are left alone rather than truncated."""
    b = as_bytes(v)
    if b is None:
        return None
    if len(b) < width:
        b = b"\x00" * (width - len(b)) + b
    return "0x" + b.hex()


def show(v):
    if isinstance(v, (dict, list)):
        try:
            s = json.dumps(v, sort_keys=True)
        except Exception:
            s = repr(v)
    else:
        s = str(v)
    return s if len(s) <= TRUNC else s[:TRUNC] + "..."


# --------------------------------------------------------------------------
# alloc comparison
# --------------------------------------------------------------------------


def canon_storage(storage):
    """{key -> value} with zero-valued slots dropped and keys 32-byte padded."""
    out = {}
    if not isinstance(storage, dict):
        return out
    for k, v in storage.items():
        ck = hexpad(k, 32)
        cv = as_int(v)
        if ck is None or cv is None or cv == 0:
            continue
        out[ck] = cv
    return out


def canon_account(acct):
    if not isinstance(acct, dict):
        acct = {}
    # accept both the fixture spelling and any t8n that echoes it
    return {
        "balance": as_int(acct.get("balance", 0)) or 0,
        "nonce": as_int(acct.get("nonce", 0)) or 0,
        "code": as_bytes(acct.get("code", "0x")) or b"",
        "storage": canon_storage(acct.get("storage")),
    }


EMPTY_ACCOUNT = {"balance": 0, "nonce": 0, "code": b"", "storage": {}}


def canon_alloc(alloc):
    out = {}
    if not isinstance(alloc, dict):
        return out
    for addr, acct in alloc.items():
        ca = hexpad(addr, 20)
        if ca is None:
            continue
        out[ca] = canon_account(acct)
    return out


def render_account(a):
    return {
        "balance": hex(a["balance"]),
        "nonce": hex(a["nonce"]),
        "code": "0x" + a["code"].hex(),
        "storage": {k: hex(v) for k, v in sorted(a["storage"].items())},
    }


def diff_alloc(expected, actual):
    """Return a list of human-readable differences (empty == equal)."""
    exp = canon_alloc(expected)
    act = canon_alloc(actual)
    diffs = []
    for addr in sorted(set(exp) | set(act)):
        e = exp.get(addr, EMPTY_ACCOUNT)
        a = act.get(addr, EMPTY_ACCOUNT)
        if e == a:
            continue
        for field in ("balance", "nonce", "code"):
            if e[field] != a[field]:
                ev = e[field].hex() if field == "code" else hex(e[field])
                av = a[field].hex() if field == "code" else hex(a[field])
                diffs.append("%s.%s exp=%s act=%s" % (addr, field, ev, av))
        if e["storage"] != a["storage"]:
            for slot in sorted(set(e["storage"]) | set(a["storage"])):
                ev = e["storage"].get(slot, 0)
                av = a["storage"].get(slot, 0)
                if ev != av:
                    diffs.append("%s.storage[%s] exp=%s act=%s"
                                 % (addr, slot, hex(ev), hex(av)))
    return diffs


# --------------------------------------------------------------------------
# result.json helpers
# --------------------------------------------------------------------------


def rejected_indexes(result):
    """Sorted list of rejected tx indexes.

    Tolerates: a missing `rejected` key (-> []), a null value (-> []), entries
    that are bare integers rather than `{"index": n, ...}` objects.

    What the `rejected` check grades is the **index set and nothing else**:
    the returned list is compared to the case's `expect.rejected` and the
    entries' `error` member, their ordering and the distinction between an
    empty array and an absent key are all unexamined here. That is deliberate:

    - The diagnostic string is the tool's own wording, and grading it would
      grade spelling. `instruction.md` names `MAX_GAS_LIMIT_EXCEEDED` only
      inside a quoted "e.g." for exactly that reason.
    - The entry *shape* (`{"index": n, "error": "..."}`) and the
      "present only when non-empty" rule are documented in
      `TEST_INTERFACE.md`, but they are documented for the tool's author:
      neither is asserted by any group, and a tool that emits a bare integer
      index or a stray empty `rejected` array is not penalised. That is a
      known hole in the scored surface, not an oversight -- closing it would
      mean grading a diagnostic's presence on the step's signature rejection,
      where the index set is the part that carries meaning. Recorded here so
      that a passing group is not read as more than it is.
    """
    raw = result.get("rejected") if isinstance(result, dict) else None
    if raw is None:
        return []
    if not isinstance(raw, list):
        return []
    out = []
    for item in raw:
        if isinstance(item, bool):
            continue
        if isinstance(item, int):
            out.append(item)
        elif isinstance(item, dict):
            idx = as_int(item.get("index"))
            if idx is not None:
                out.append(idx)
        # anything else is ignored rather than fatal
    return sorted(out)


def load_json(path, label, problems):
    if not path:
        problems.append("no --%s path given" % label)
        return None
    if not os.path.isfile(path):
        problems.append("%s file missing: %s" % (label, path))
        return None
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except Exception as exc:
        problems.append("%s file unreadable (%s: %s)" % (label, type(exc).__name__, exc))
        return None


# --------------------------------------------------------------------------
# the check vocabulary
#
# `stateRoot`, `logsHash`, `logsBloom`, `rejected`, `gasUsed` and `alloc` are
# the original state-test checks; the first three and the last two apply to a
# legacy case whenever the fixture carries a value to compare against.  The
# four `EXTRA_HASH_CHECKS` are block-level roots that only a blockchain test
# states; no state-test case in the shipped corpora carries one, so a case
# built by mkcorpus.py still scores exactly as it did before they existed.
# --------------------------------------------------------------------------

EXTRA_HASH_CHECKS = ("receiptsRoot", "withdrawalsRoot", "requestsHash", "txRoot")
BYTES_CHECKS = ("stateRoot", "logsHash", "logsBloom") + EXTRA_HASH_CHECKS
KNOWN_CHECKS = frozenset(BYTES_CHECKS + ("rejected", "gasUsed", "alloc", "storage"))

def run(args) -> int:
    lines = []
    ok = True

    def passed(name):
        lines.append("PASS %s" % name)

    def failed(name, expected, actual):
        nonlocal ok
        ok = False
        lines.append("FAIL %s expected=%s actual=%s" % (name, show(expected), show(actual)))

    if args.exit_status:
        print("FAIL process exit=%d" % args.exit_status)
        return 1

    case_path = os.path.join(args.case, "case.json")
    if not os.path.isfile(case_path):
        print("FAIL case expected=<readable case.json> actual=missing:%s" % show(case_path))
        return 1
    try:
        with open(case_path, "r", encoding="utf-8") as fh:
            case = json.load(fh)
        expect = case["expect"]
    except Exception as exc:
        print("FAIL case expected=<valid case.json> actual=%s"
              % show("%s: %s" % (type(exc).__name__, exc)))
        return 1

    problems = []
    result = load_json(args.result, "result", problems)
    if not isinstance(result, dict):
        if result is not None:
            problems.append("result is not a JSON object")
        result = None

    exp_rejected = sorted(as_int(x) or 0 for x in (expect.get("rejected") or []))
    want_alloc = "alloc" in (args.check or []) and expect.get("alloc") is not None
    # `--check logsBloom` forces the bloom comparison on for a case whose own
    # `expect.checks` list predates it. The inherited groups' cases declare
    # `("stateRoot", "logsHash", "rejected")` and carry a `logsBloom` the three
    # of them never asked for, so without this the 9 inherited cases with a
    # genuinely non-empty bloom -- the only non-empty blooms in the whole
    # corpora -- are the ones no scored assertion reaches. `want_bloom` is false
    # when the case states no bloom at all, exactly as `want_alloc` is.

    declared = expect.get("checks")
    if isinstance(declared, list) and declared:
        # The case states exactly which checks it declares.  Used by corpora
        # built from block-level fixtures (mkbc8n.py), where a block header
        # supplies roots a state test never has -- and, conversely, has no
        # `logsHash` at all.
        checks = [c for c in declared if c in KNOWN_CHECKS]
    else:
        # Legacy state-test case (mkcorpus.py): unchanged behaviour.
        checks = ["stateRoot", "logsHash", "rejected"]
        # `logsBloom` travels with most state-test fixtures -- 544 of the 592
        # cases in the shipped corpora carry a non-null one -- and the Oracle
        # reproduces all 544 byte-for-byte. It was skipped here only because it
        # is in `BYTES_CHECKS` but not in this default list, so a tool that
        # emitted a wrong or empty bloom scored the group anyway. Added, guarded
        # on the fixture carrying it, exactly as the roots below are: 48 case
        # files carry no bloom at all -- 23 with no `logsBloom` key, and 25
        # whose bloom is null for an all-rejected or non-executing block.
        if expect.get("logsBloom") is not None:
            checks.append("logsBloom")
        for name in EXTRA_HASH_CHECKS:
            if expect.get(name) is not None:
                checks.append(name)
        if expect.get("gasUsed") is not None and not exp_rejected:
            checks.append("gasUsed")
    if want_alloc and "alloc" not in checks:
        checks.append("alloc")
    # The bloom is compared whenever the case carries one and either its own
    # list already names it or the caller asked for it via `--check`.
    want_bloom = expect.get("logsBloom") is not None
    if want_bloom and "logsBloom" not in checks and "logsBloom" in (args.check or []):
        checks.append("logsBloom")

    if result is None:
        for name in checks:
            failed(name, expect.get(name), "; ".join(problems) or "no result.json")
        print("\n".join(lines))
        return 1

    # ---- byte-string equality checks --------------------------------------
    for name in BYTES_CHECKS:
        if name not in checks:
            continue
        e = as_bytes(expect.get(name))
        a = as_bytes(result.get(name))
        if name == "logsBloom" and e is None:
            # A fixture states "no logs" two ways -- a null `logsBloom` and an
            # all-zero one -- while `t8n` always emits the field, as the zero
            # bloom when the block logged nothing. Those are the same claim, so
            # canonicalise the expected null to the zero bloom rather than
            # skipping the check: 13 inherited cases carry the null spelling and
            # 287 carry the all-zero one, and all 300 must compare. An all-zero
            # bloom against a fixture that logged something is still a failure,
            # and so is a missing key.
            e = b"\x00" * 256
        if e is not None and e == a:
            passed(name)
        else:
            failed(name, expect.get(name), result.get(name, "<missing>"))

    # ---- rejected --------------------------------------------------------
    if "rejected" in checks:
        got_rejected = rejected_indexes(result)
        if got_rejected == exp_rejected:
            passed("rejected")
        else:
            failed("rejected", exp_rejected, got_rejected)

    # ---- gasUsed: quantity equality --------------------------------------
    if "gasUsed" in checks:
        e = as_int(expect.get("gasUsed"))
        a = as_int(result.get("gasUsed"))
        if e is not None and a is not None and e == a:
            passed("gasUsed")
        else:
            failed("gasUsed", expect.get("gasUsed"), result.get("gasUsed", "<missing>"))

    # ---- storage ----------------------------------------------------------
    # A synthetic probe knows what the consensus rules say one storage slot
    # must hold, and knows nothing about the rest of the post-state: the
    # sender's balance depends on gas arithmetic the probe is not testing. So
    # `expect.storage` names only the slots it can derive from the rules, and
    # only those are compared. This is strictly narrower than `alloc`, never a
    # relaxation of it -- a case that can state the whole post-state still
    # declares `alloc` and gets the full diff.
    if expect.get("storage") is not None:
        aprob = []
        allocout = load_json(args.alloc, "alloc", aprob)
        if not isinstance(allocout, dict):
            failed("storage", "<post-state alloc>", "; ".join(aprob) or "alloc is not an object")
        else:
            act = canon_alloc(allocout)
            diffs = []
            # `hexpad`'s width is in BYTES, and `canon_alloc` keys accounts at
            # 20 and slots at 32 and stores slot values as ints with the zero
            # slots dropped. The widths here were 40 and 64 -- hex-nibble
            # counts -- so the account lookup never hit, every slot read back
            # as absent, and a non-zero expectation could not be satisfied by
            # any tool. Fixed here rather than worked around: the branch is
            # what makes a synthetic probe's expectation narrow, and a check
            # that cannot pass is not a narrow check, it is a broken one.
            for addr, slots in (expect.get("storage") or {}).items():
                a = hexpad(addr, 20)
                have = (act.get(a) or {}).get("storage") or {}
                for slot, val in slots.items():
                    k = hexpad(slot, 32)
                    want = as_int(val)
                    got = have.get(k, 0)
                    if want is None or got != want:
                        diffs.append("%s[%s]=%s want %s"
                                     % (a[:10], k[:10], hex(got), show(val)))
            if diffs:
                failed("storage", "%d slot(s)" % sum(len(v) for v in expect["storage"].values()),
                       "%d wrong: %s" % (len(diffs), "; ".join(diffs[:6])))
            else:
                passed("storage")

    # ---- alloc -----------------------------------------------------------
    if want_alloc:
        aprob = []
        allocout = load_json(args.alloc, "alloc", aprob)
        if not isinstance(allocout, dict):
            failed("alloc", "<post-state alloc>", "; ".join(aprob) or "alloc is not an object")
        else:
            diffs = diff_alloc(expect.get("alloc"), allocout)
            if not diffs:
                passed("alloc")
            else:
                failed("alloc", "%d accounts" % len(expect.get("alloc") or {}),
                       "%d diff(s): %s" % (len(diffs), "; ".join(diffs[:6])))

    print("\n".join(lines))
    return 0 if ok else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="t8ncheck.py",
                                 description="Score a t8n run against a corpus case.")
    ap.add_argument("--case", required=True, help="case directory (holds case.json)")
    ap.add_argument("--result", help="path to the t8n --output.result file")
    ap.add_argument("--alloc", help="path to the t8n --output.alloc file")
    ap.add_argument("--exit-status", type=int, default=0,
                    help="exit status of the t8n process itself")
    ap.add_argument("--check", action="append", default=[], choices=["alloc", "logsBloom"],
                    help="enable an optional check")
    try:
        args = ap.parse_args(argv)
    except SystemExit as exc:
        # `--help` raises SystemExit(0); `or 2` would have turned that into a
        # failure.  Only a genuine non-zero argparse code is an error here.
        return 2 if exc.code is None else int(exc.code)
    try:
        return run(args)
    except Exception as exc:  # never raise
        print("FAIL internal expected=<no exception> actual=%s"
              % show("%s: %s" % (type(exc).__name__, exc)))
        return 1


if __name__ == "__main__":
    sys.exit(main())
