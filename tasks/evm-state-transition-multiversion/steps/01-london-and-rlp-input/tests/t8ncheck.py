#!/usr/bin/env python3
"""t8ncheck.py -- score one t8n run against a corpus case.

    t8ncheck.py --case <case-dir> --result <result.json> --alloc <allocout.json>
                [--exit-status N] [--check alloc]

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
# how many transactions did --input.txs carry?
#
# `receipts` and `rejected` together account for every transaction in the
# input (TEST_INTERFACE.md). Counting against the *input* rather than against
# the result itself is what catches a tool that silently drops a transaction.
# The schema probe asserts this on one case; doing it here spreads the same
# requirement across every corpus case that can state it, so the requirement
# no longer lives or dies with a single group.
#
# The case directory says which input form it uses by the same rule
# run_group.sh applies: a `txs.json` beside a `txs.rlp` wins.
# --------------------------------------------------------------------------


def _rlp_items(b, i, end):
    """Yield the start offset of each RLP item in b[i:end]."""
    while i < end:
        yield i
        p = b[i]
        if p < 0x80:
            i += 1
        elif p < 0xb8:
            i += 1 + (p - 0x80)
        elif p < 0xc0:
            n = p - 0xb7
            i += 1 + n + int.from_bytes(b[i + 1:i + 1 + n], "big")
        elif p < 0xf8:
            i += 1 + (p - 0xc0)
        else:
            n = p - 0xf7
            i += 1 + n + int.from_bytes(b[i + 1:i + 1 + n], "big")


def count_input_txs(case_dir):
    """Transactions in the case's --input.txs document, or None if the count
    cannot be established.  None means "do not assert it", never "it is zero":
    a check nobody can satisfy is worse than a check that does not run."""
    jpath = os.path.join(case_dir, "txs.json")
    rpath = os.path.join(case_dir, "txs.rlp")
    try:
        if os.path.isfile(jpath):
            with open(jpath, "r", encoding="utf-8") as fh:
                doc = json.load(fh)
            return len(doc) if isinstance(doc, list) else None
        if not os.path.isfile(rpath):
            return None
        with open(rpath, "r", encoding="utf-8") as fh:
            doc = json.load(fh)
        if not isinstance(doc, str):
            return None
        raw = as_bytes(doc)
        if not raw:
            return None
        p = raw[0]
        if p < 0xc0:
            return None                      # not an RLP list at all
        if p < 0xf8:
            start, end = 1, 1 + (p - 0xc0)
        else:
            n = p - 0xf7
            start = 1 + n
            end = start + int.from_bytes(raw[1:1 + n], "big")
        if end > len(raw):
            return None
        return sum(1 for _ in _rlp_items(raw, start, end))
    except Exception:
        return None


# --------------------------------------------------------------------------
# the check vocabulary
#
# `stateRoot`, `logsHash`, `rejected`, `gasUsed` and `alloc` are the original
# state-test checks.  The rest are block-level roots that only a blockchain
# test states; a state-test case never carries them, so a case built by
# mkcorpus.py scores exactly as it did before they existed.
# --------------------------------------------------------------------------

EXTRA_HASH_CHECKS = ("receiptsRoot", "withdrawalsRoot", "requestsHash", "txRoot")
BYTES_CHECKS = ("stateRoot", "logsHash", "logsBloom") + EXTRA_HASH_CHECKS

# Auto-added to a legacy case's check list whenever the case carries the value.
# `logsBloom` is read off the fixture's own `post[].receipt.bloom`, exactly as
# `gasUsed` is read off `cumulativeGasUsed`, so asserting it costs nothing and
# closes the one result field that was otherwise never compared by value
# anywhere in the suite. This rule is carried forward on the same path in every
# later step's copy (step 04 spells it out beside its `SCHEMA_ALWAYS`;
# 02/03/05/06/07 add it to the `EXTRA_HASH_CHECKS + ("logsBloom",)` loop), so a
# legacy case's bloom is compared by value at every step.
AUTO_CHECKS = EXTRA_HASH_CHECKS + ("logsBloom",)
KNOWN_CHECKS = frozenset(BYTES_CHECKS
                         + ("rejected", "gasUsed", "alloc", "storage",
                            "receiptsCover"))

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

    # The same tolerant reader used on the tool's own output. A case may
    # state `rejected` as bare indexes or as the documented
    # `{"index": N, "error": "..."}` objects; reading an object with
    # `as_int` would have silently yielded 0 for every one of them, so an
    # object-form expectation at any index but 0 could not have failed.
    exp_rejected = rejected_indexes({"rejected": expect.get("rejected")})
    want_alloc = "alloc" in (args.check or []) and expect.get("alloc") is not None

    n_input_txs = count_input_txs(args.case)

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
        for name in AUTO_CHECKS:
            if expect.get(name) is not None:
                checks.append(name)
        if expect.get("gasUsed") is not None and not exp_rejected:
            checks.append("gasUsed")
        if n_input_txs:
            checks.append("receiptsCover")
    if want_alloc and "alloc" not in checks:
        checks.append("alloc")

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

    # ---- receipts + rejected == every transaction in --input.txs ----------
    if "receiptsCover" in checks:
        got = len(result.get("receipts") or []) + len(result.get("rejected") or [])
        if n_input_txs is not None and got == n_input_txs:
            passed("receiptsCover")
        else:
            failed("receiptsCover", "%s tx(s) in --input.txs" % n_input_txs,
                   "%d receipts + %d rejected"
                   % (len(result.get("receipts") or []),
                      len(result.get("rejected") or [])))

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
    ap.add_argument("--check", action="append", default=[], choices=["alloc"],
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
