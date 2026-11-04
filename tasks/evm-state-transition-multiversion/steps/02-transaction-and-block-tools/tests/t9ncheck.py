#!/usr/bin/env python3
"""Score one `t9n` case.

    t9ncheck.py --case <casedir> --stdout <file> --exit-status N

Prints one `PASS|FAIL <check> [detail]` line per check; the case passes only if
every check does.  Checks are `exit`, `json`, `count`, and then, per
transaction, whichever of `address`, `hash` and `intrinsicGas` the case
declares -- or `error`, and then whichever of `address` and `intrinsicGas` the
case declares beside it (the under-funded shape carries both).

An error entry's `hash` is graded in **both** directions: a case that declares
one requires it (the *unrecoverable* shape, where the element decoded but its
signature does not recover a sender), and a case that declares none forbids it
(the *undecodable* shape, where there are no transaction bytes to hash).

`t9n` writes its result to stdout and takes no output flags, so the captured
stdout *is* the result document.  A tool that prints anything else to stdout --
a log line, a warning -- fails `json`, which is intended: the document has to be
machine-readable on its own.

Invalid transactions are graded on *whether* an error is reported, never on its
wording.  The consensus rules fix that a transaction is rejected; they do not
fix the sentence an implementation prints.
"""
import argparse, json, sys


def norm_hex(s):
    if s is None:
        return None
    s = str(s).lower()
    return s if s.startswith("0x") else "0x" + s


def norm_qty(s):
    try:
        return int(str(s), 16 if str(s).lower().startswith("0x") else 10)
    except Exception:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", required=True)
    ap.add_argument("--stdout", required=True)
    ap.add_argument("--exit-status", type=int, required=True)
    a = ap.parse_args()

    case = json.load(open(a.case + "/case.json"))
    want = case["expect"]
    out = []

    if a.exit_status == 0:
        out.append(("PASS", "exit", ""))
    else:
        out.append(("FAIL", "exit", "tool exited %d" % a.exit_status))

    raw = open(a.stdout, "rb").read().decode("utf-8", "replace")
    try:
        got = json.loads(raw)
        # A well-formed but empty list is not refusable, and the contract
        # accepts either spelling of its report: "either an empty array or
        # `null` is accepted for that report" (TEST_INTERFACE.md, and the same
        # sentence in instruction.md). A Go tool that builds a nil slice emits
        # `null`, so both spellings have to be read. A `null` for a *non-empty*
        # expectation is still wrong and fails `count` below.
        if got is None:
            got = []
        if not isinstance(got, list):
            raise ValueError("result is %s, want a JSON array" % type(got).__name__)
        out.append(("PASS", "json", ""))
    except Exception as e:
        out.append(("FAIL", "json", "stdout is not a JSON array: %s" % str(e)[:80]))
        for r in out:
            print("%s %s %s" % r)
        sys.exit(0)

    if len(got) == len(want):
        out.append(("PASS", "count", ""))
    else:
        out.append(("FAIL", "count", "%d entries, want %d" % (len(got), len(want))))
        for r in out:
            print("%s %s %s" % r)
        sys.exit(0)

    for i, (g, w) in enumerate(zip(got, want)):
        if w.get("error"):
            if "error" in g:
                out.append(("PASS", "tx%d.error" % i, ""))
            else:
                out.append(("FAIL", "tx%d.error" % i,
                            "expected a rejection (%s), got %s" % (w["why"], sorted(g))))
            # A rejected transaction still carries its hash when it decoded --
            # the *unrecoverable* shape: the bytes are a transaction, the
            # signature simply does not recover a sender.  An expectation that
            # declares a hash is graded against it; one that declares none is
            # asserting the *undecodable* shape, which the documents define as
            # `error` and nothing else, so a hash there is a defect rather than
            # an implementation choice.
            if "hash" in w:
                if "hash" in g and norm_hex(g["hash"]) != norm_hex(w["hash"]):
                    out.append(("FAIL", "tx%d.error.hash" % i,
                                "%s != %s" % (g.get("hash"), w["hash"])))
            elif "hash" in g:
                out.append(("FAIL", "tx%d.error.hash" % i,
                            "undecodable entry carries a hash it cannot have"))
            # An *under-funded* transaction is a documented shape of its own:
            # it decodes and recovers, so it reports `address` and
            # `intrinsicGas` beside the error rather than the bare error of a
            # transaction that never decoded.  A case declares those keys only
            # when the shape requires them, and then they are graded like any
            # other declared key.
            for key, norm in (("address", norm_hex), ("intrinsicGas", norm_qty)):
                if key not in w:
                    continue
                if key not in g:
                    out.append(("FAIL", "tx%d.error.%s" % (i, key), "absent"))
                elif norm(g[key]) != norm(w[key]):
                    out.append(("FAIL", "tx%d.error.%s" % (i, key),
                                "%s != %s" % (g[key], w[key])))
                else:
                    out.append(("PASS", "tx%d.error.%s" % (i, key), ""))
            continue
        if "error" in g:
            out.append(("FAIL", "tx%d.accepted" % i,
                        "rejected with %r, expected acceptance" % g["error"][:60]))
            continue
        # Only the keys the case actually declares are graded.  A case whose
        # transaction was derived rather than collected knows its own hash and
        # its own intrinsic gas exactly, but not the address the signature
        # recovers to, and it declares the two it knows rather than guessing
        # the third.
        for key, norm in (("address", norm_hex), ("hash", norm_hex),
                          ("intrinsicGas", norm_qty)):
            if key not in w:
                continue
            if key not in g:
                out.append(("FAIL", "tx%d.%s" % (i, key), "absent"))
            elif norm(g[key]) != norm(w[key]):
                out.append(("FAIL", "tx%d.%s" % (i, key),
                            "%s != %s" % (g[key], w[key])))
            else:
                out.append(("PASS", "tx%d.%s" % (i, key), ""))

    for r in out:
        print("%s %s %s" % r)


if __name__ == "__main__":
    main()
