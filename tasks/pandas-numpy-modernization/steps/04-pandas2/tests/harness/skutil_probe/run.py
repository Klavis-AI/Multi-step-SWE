"""Record and replay the behavior corpus.

    python -m skutil_probe.run record  --out part.json [--shard i/n]
    python -m skutil_probe.run merge   --out all.json part-*.json
    python -m skutil_probe.run compare --expected base.json --actual cand.json \
                                       [--flips flips.json ...] --out report.json
    python -m skutil_probe.run list

The recorder and the replayer are the same code path: `record` against the
untouched baseline produces the expectations, `record` against a candidate tree
produces the observations, and `compare` is the only thing that knows which is
which.
"""
from __future__ import print_function, absolute_import, division

import os
import sys

# Fixed before NumPy is imported: BLAS thread count changes floating-point
# summation order, which is the one thing that makes an otherwise deterministic
# linear-algebra result differ between two runs on the same machine.
for _var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
             "NUMEXPR_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
    os.environ[_var] = "1"
os.environ.setdefault("MPLBACKEND", "Agg")
os.environ.setdefault("TZ", "UTC")
os.environ.setdefault("SOURCE_DATE_EPOCH", "1514764800")  # 2018-01-01T00:00:00Z
os.environ.setdefault("LC_ALL", "C.UTF-8")

# PYTHONHASHSEED has to be in the environment before the interpreter starts;
# assigning it from inside a running process does nothing at all. skutil builds
# one fitted attribute out of a ``set`` of column names, and set iteration over
# strings follows ``hash()``, so without this two runs of the same code on the
# same machine disagree. Hand the process over once, and only when this module
# is the entry point -- importing it as a library must never exec.
if __name__ == "__main__" and os.environ.get("PYTHONHASHSEED") != "0":
    os.environ["PYTHONHASHSEED"] = "0"
    os.execv(sys.executable,
             [sys.executable, "-m", "skutil_probe.run"] + sys.argv[1:])

import argparse  # noqa: E402
import glob  # noqa: E402
import json  # noqa: E402
import platform  # noqa: E402
import re  # noqa: E402
import time  # noqa: E402
import traceback  # noqa: E402
import warnings  # noqa: E402
import zlib  # noqa: E402

import numpy as np  # noqa: E402

from . import canon  # noqa: E402
from .registry import LOAD_ERRORS, load_all  # noqa: E402

SKUTIL_MARK = os.sep + "skutil" + os.sep


def case_seed(cid):
    """Deterministic per-case seed. zlib.crc32, not hash(), because hash() of a
    string is salted per process unless PYTHONHASHSEED is pinned."""
    return zlib.crc32(cid.encode("utf-8")) & 0x7FFFFFFF


_ADDR = re.compile(r"0x[0-9a-fA-F]{4,}")


def _msg(exc):
    """The exception text with object addresses blanked.

    ``repr``-in-a-message is common (``delegate (<Inner object at 0x7f..>) is
    not an instance of..``) and the address differs on every run, so it must not
    reach the recording.
    """
    return _ADDR.sub("0xADDR", str(exc))


def _skutil_frame(tb):
    """Innermost skutil frame, and whether the exception was raised *from* it.

    The distinction matters: ``corr_plot`` calls seaborn which calls matplotlib,
    and a matplotlib ``ValueError`` surfacing through ``corr_plot`` still has
    matplotlib's wording. Only a message whose deepest frame is skutil's own is
    skutil's message, and only that one is part of the contract.
    """
    found = None
    raised_here = False
    while tb is not None:
        fn = tb.tb_frame.f_code.co_filename
        is_sk = SKUTIL_MARK in fn and os.sep + "tests" + os.sep not in fn
        if is_sk:
            idx = fn.rfind(SKUTIL_MARK)
            found = (fn[idx + 1:], tb.tb_frame.f_code.co_name)
        raised_here = is_sk
        tb = tb.tb_next
    return found, raised_here


def run_case(entry):
    cid = entry["id"]
    rec = {"id": cid, "tags": entry["tags"]}
    if entry.get("rtol") is not None:
        rec["rtol"] = entry["rtol"]
    if entry.get("atol") is not None:
        rec["atol"] = entry["atol"]

    np.random.seed(case_seed(cid))
    started = time.time()
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        try:
            value = entry["fn"]()
            rec["status"] = "ok"
            rec["value"] = canon.canon(value)
        except BaseException as exc:  # a raise is a recordable outcome
            if isinstance(exc, (KeyboardInterrupt, SystemExit)):
                raise
            where, raised_here = _skutil_frame(sys.exc_info()[2])
            rec["status"] = "raised"
            rec["exc"] = {
                "type": type(exc).__name__,
                "msg": _msg(exc),
                "in_skutil": raised_here,
                # Only skutil's own message text is part of the contract: a
                # message raised by pandas or scikit-learn is their wording and
                # changes with their version.
                "msg_cmp": _msg(exc) if raised_here else None,
                "where": "%s:%s" % where if where else None,
            }
    rec["elapsed_ms"] = int(round((time.time() - started) * 1000))
    rec["warnings"] = sorted(set(
        w.category.__name__ for w in caught
        if SKUTIL_MARK in (w.filename or "")))
    return rec


def _versions():
    import pandas
    import scipy
    import sklearn
    out = {
        "python": platform.python_version(),
        "numpy": np.__version__,
        "scipy": scipy.__version__,
        "pandas": pandas.__version__,
        "sklearn": sklearn.__version__,
        "platform": platform.platform(),
    }
    try:
        import matplotlib
        out["matplotlib"] = matplotlib.__version__
    except Exception:
        pass
    try:
        import seaborn
        out["seaborn"] = seaborn.__version__
    except Exception:
        pass
    try:
        import skutil
        out["skutil"] = getattr(skutil, "__version__", "?")
        out["skutil_path"] = os.path.dirname(os.path.abspath(skutil.__file__))
    except Exception:
        pass
    return out


def cmd_record(args):
    cases = load_all()
    if args.shard:
        i, n = (int(x) for x in args.shard.split("/"))
        cases = [c for k, c in enumerate(cases) if k % n == i]
    if args.only:
        wanted = set(args.only.split(","))
        cases = [c for c in cases if c["id"] in wanted]
    records = [run_case(c) for c in cases]
    payload = {"versions": _versions(), "records": records,
               "load_errors": list(LOAD_ERRORS)}
    with open(args.out, "w") as fh:
        json.dump(payload, fh, sort_keys=True)
    print("recorded %d cases -> %s" % (len(records), args.out))
    # Loud, because the alternative is a corpus that is quietly short. Every
    # case in a casebook that would not import is absent from this file, and
    # `compare` will score all of them as missing against the baseline.
    for err in LOAD_ERRORS:
        print("  CASEBOOK %s DID NOT IMPORT: %s" % (err["module"],
                                                    err["error"]))
    return 0


def cmd_merge(args):
    merged = {}
    versions = {}
    for pattern in args.parts:
        for path in sorted(glob.glob(pattern)) or [pattern]:
            with open(path) as fh:
                payload = json.load(fh)
            versions.update(payload.get("versions", {}))
            for rec in payload["records"]:
                merged[rec["id"]] = rec
    out = {"versions": versions,
           "records": [merged[k] for k in sorted(merged)]}
    with open(args.out, "w") as fh:
        json.dump(out, fh, sort_keys=True)
    print("merged %d cases -> %s" % (len(out["records"]), args.out))
    return 0


COMPARED_EXC_FIELDS = ("type", "in_skutil", "msg_cmp")


def comparable(rec):
    """The part of a record that is the contract."""
    out = {"status": rec["status"]}
    if rec["status"] == "ok":
        out["value"] = rec["value"]
    else:
        exc = rec.get("exc", {})
        out["exc"] = dict((k, exc.get(k)) for k in COMPARED_EXC_FIELDS)
    return out


def apply_flips(expected, flips):
    """Overlay the enumerated, documented semantic changes of later steps.

    A flip replaces one case's whole comparable record. The untouched tree
    therefore fails it, and so does a shim that restores the old behavior.

    A *drop* removes a case from the corpus entirely, and is the rarer, more
    dangerous instrument: it is the only way a case can leave the denominator,
    so every drop is a hole in the oracle and has to be paid for by a gate
    somewhere else. It exists for the one situation a flip cannot express --
    when the step's own requirements make the case unreachable rather than
    different. ``load_boston`` is the example: scikit-learn removed the dataset
    outright in 1.2, so from step 3 on there is no behavior left to record, not
    even an exception that the step is free to keep raising.
    """
    applied = []
    dropped = []
    for flip in flips:
        for entry in flip.get("flips", []):
            cid = entry["case"]
            if cid not in expected:
                raise KeyError("flip names an unknown case: %s" % cid)
            expected[cid] = entry["expect"]
            applied.append({"case": cid, "reason": entry["reason"],
                            "citation": entry["citation"]})
        for entry in flip.get("drops", []):
            cid = entry["case"]
            if cid not in expected:
                raise KeyError("drop names an unknown case: %s" % cid)
            del expected[cid]
            dropped.append({"case": cid, "reason": entry["reason"],
                            "citation": entry["citation"]})
    return applied, dropped


def cmd_compare(args):
    with open(args.expected) as fh:
        exp_payload = json.load(fh)
    with open(args.actual) as fh:
        act_payload = json.load(fh)

    expected = dict((r["id"], comparable(r)) for r in exp_payload["records"])
    tol = dict((r["id"], (r.get("rtol"), r.get("atol")))
               for r in exp_payload["records"])
    tags = dict((r["id"], r.get("tags", [])) for r in exp_payload["records"])

    flips = []
    for path in args.flips or []:
        with open(path) as fh:
            flips.append(json.load(fh))
    applied, dropped = apply_flips(expected, flips)

    actual = dict((r["id"], comparable(r)) for r in act_payload["records"])

    results = []
    for cid in sorted(expected):
        if cid not in actual:
            results.append({"case": cid, "ok": False, "why": "missing",
                            "tags": tags.get(cid, [])})
            continue
        rtol, atol = tol.get(cid, (None, None))
        mism = canon.compare(
            expected[cid], actual[cid],
            rtol=args.rtol if rtol is None else rtol,
            atol=args.atol if atol is None else atol,
        )
        results.append({
            "case": cid,
            "ok": not mism,
            "tags": tags.get(cid, []),
            "mismatches": [m.as_dict() for m in mism[:8]],
            "n_mismatches": len(mism),
        })

    passed = sum(1 for r in results if r["ok"])
    report = {
        "expected_versions": exp_payload.get("versions", {}),
        "actual_versions": act_payload.get("versions", {}),
        "actual_load_errors": act_payload.get("load_errors", []),
        "flips_applied": applied,
        "cases_dropped": dropped,
        "total": len(results),
        "passed": passed,
        "failed": len(results) - passed,
        "results": results,
    }
    if args.out:
        with open(args.out, "w") as fh:
            json.dump(report, fh, sort_keys=True)
    print("behavior: %d/%d passed (%d flipped, %d dropped)"
          % (passed, len(results), len(applied), len(dropped)))
    for err in report["actual_load_errors"]:
        print("  casebook %s did not import against the candidate tree: %s"
              % (err.get("module"), err.get("error")))
    for r in results:
        if not r["ok"] and args.verbose:
            print("  FAIL %s %s" % (r["case"], r.get("why") or
                                    (r["mismatches"][0] if r["mismatches"] else "")))
    return 0


def cmd_list(args):
    cases = load_all()
    for err in LOAD_ERRORS:
        print("CASEBOOK %s DID NOT IMPORT: %s" % (err["module"],
                                                  err["error"]))
    adv = 0
    for c in cases:
        if "adversarial" in c["tags"]:
            adv += 1
        if args.verbose:
            print("%-52s %s" % (c["id"], ",".join(c["tags"])))
    print("cases: %d  adversarial: %d (%.1f%%)"
          % (len(cases), adv, 100.0 * adv / max(1, len(cases))))
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(prog="skutil_probe.run")
    sub = p.add_subparsers(dest="cmd")

    r = sub.add_parser("record")
    r.add_argument("--out", required=True)
    r.add_argument("--shard")
    r.add_argument("--only")
    r.set_defaults(func=cmd_record)

    m = sub.add_parser("merge")
    m.add_argument("--out", required=True)
    m.add_argument("parts", nargs="+")
    m.set_defaults(func=cmd_merge)

    c = sub.add_parser("compare")
    c.add_argument("--expected", required=True)
    c.add_argument("--actual", required=True)
    c.add_argument("--flips", action="append")
    c.add_argument("--out")
    c.add_argument("--rtol", type=float, default=1e-7)
    c.add_argument("--atol", type=float, default=1e-10)
    c.add_argument("--verbose", action="store_true")
    c.set_defaults(func=cmd_compare)

    l = sub.add_parser("list")
    l.add_argument("--verbose", action="store_true")
    l.set_defaults(func=cmd_list)

    args = p.parse_args(argv)
    if not getattr(args, "func", None):
        p.print_help()
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
