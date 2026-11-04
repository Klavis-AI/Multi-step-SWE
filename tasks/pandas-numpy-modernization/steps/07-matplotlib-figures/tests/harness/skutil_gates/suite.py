"""The repository's own test suite, as a scored signal rather than a gate.

Rule 5 counts the codebase's own tests toward behaviour equivalence, and rule 6
says the scorer runs them from its own copy so that nothing the agent writes is
read as evidence. Those two sentences pull in opposite directions for *this*
migration, and the tension is worth stating because the resolution is a
deviation:

the tests are part of what the migration has to modernise. ``as_matrix()`` has
31 call sites and 6 of them are in ``skutil/**/tests/`` -- four in
``decomposition/tests/test_decompose.py``, two in
``preprocessing/tests/test_transform.py``; step 2's gate asks for all 31. A
verifier that overlaid its own pristine copy of those test files would be
running pandas-0.23 test code against a pandas-1.3 tree and failing a correct
submission for work it was told to do. So the suite that runs here is the
**candidate's own**, and what is scored is its *outcome table* against the one
recorded from the tree the step was handed:

* every node id recorded on the entry tree must still be collected -- a suite
  cannot be passed by deleting it;
* every node id that *passed* on the entry tree must still pass;
* a node id that *failed* on the entry tree may pass or fail. Repairing one is
  neither required nor rewarded, and must never be penalised: some of these are
  inherited bugs, and a step's own requirements can turn a failure into a pass
  as a side effect. Measured at step 2, where all six recorded failures stay
  failures under the reference solution -- ``odr.tests.test_odr::test_qr``, for
  instance, asserts a ``qraux`` vector to six decimals and the pinned LAPACK
  pivots differently in the fourth, which is not something the migration is
  being asked to change.

New tests the candidate adds are neither required nor rewarded. They are not
evidence -- rule 6 -- and the recorded behaviour corpus is the oracle that
actually decides whether the tree still does what it did. This is a floor.

    python -m skutil_gates.suite record  --out expected/inherited-suite.json
    python -m skutil_gates.suite capture --out observed.json
    python -m skutil_gates.suite compare --expected ... --observed ... \
                                         --out suite.json
    python -m skutil_gates.suite score   --expected ... --out suite.json

``capture`` and ``compare`` are ``score`` split in two, and the split is a
privilege boundary rather than a convenience. ``capture`` runs pytest, which is
the candidate's code, so a verifier runs it unprivileged; ``compare`` reads the
recorded expectation, which a candidate must not be able to read at all -- the
node ids it names are answers. Nothing in this module imports ``skutil``, so
``compare`` is safe to run as root with the expectation kept root-only.
``score`` does both halves in one process and stays for author-side use.

Python 3.7 compatible; runs in every verifier image on the ladder.
"""
from __future__ import print_function, absolute_import, division

import argparse
import json
import os
import subprocess
import sys
import tempfile
try:
    from xml.etree import cElementTree as ElementTree
except ImportError:                               # py3.9+ removed cElementTree
    from xml.etree import ElementTree


def _pytest_argv(junit, extra):
    # --pyargs: the *installed* package, not the source tree. The verifier
    # installs the candidate and grades what it installed, and a run from /app
    # would import the source directory instead and score a submission whose
    # build is broken as if it worked.
    #
    # -p no:cacheprovider: no .pytest_cache anywhere, so a second run of the
    # same container cannot inherit the first one's last-failed list.
    argv = [sys.executable, "-m", "pytest", "--pyargs", "skutil",
            "-p", "no:cacheprovider", "-q", "--tb=no", "-rN",
            "--junit-xml", junit]
    return argv + list(extra or [])


def run(cwd=None, timeout=1800, extra=()):
    """Run the installed package's suite; return {node_id: outcome}."""
    cwd = cwd or tempfile.gettempdir()
    handle, junit = tempfile.mkstemp(prefix="junit-", suffix=".xml")
    os.close(handle)
    env = dict(os.environ)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env.pop("PYTHONPATH", None)
    try:
        proc = subprocess.Popen(_pytest_argv(junit, extra), cwd=cwd, env=env,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        try:
            out, _ = proc.communicate(timeout=timeout)
        except Exception:
            proc.kill()
            out = proc.communicate()[0]
            return {"error": "timeout", "outcomes": {}, "tail": []}
        tail = out.decode("utf-8", "replace").strip().splitlines()[-6:]
        outcomes = _parse_junit(junit)
        if outcomes is None:
            return {"error": "pytest produced no report", "outcomes": {},
                    "rc": proc.returncode, "tail": tail}
        return {"error": None, "outcomes": outcomes, "rc": proc.returncode,
                "tail": tail}
    finally:
        if os.path.exists(junit):
            os.remove(junit)


def _normalize_classname(classname):
    """Drop everything up to and including the first ``skutil`` component.

    pytest builds the junit ``classname`` from the test file's path relative to
    whatever it picked as rootdir, and rootdir for ``--pyargs skutil`` is
    inferred from where the package landed. On the baseline that yields
    ``utils.tests.test_util``; one added ``pyproject.toml`` or one changed
    install layout can just as easily yield ``skutil.utils.tests.test_util``,
    and the two spellings are the same test. Left unnormalised, a step whose
    packaging metadata changed would report all 65 recorded ids as *missing* --
    a zero for the inherited-suite floor earned by nothing.
    """
    parts = [p for p in (classname or "").split(".") if p]
    if "skutil" in parts:
        parts = parts[parts.index("skutil") + 1:]
    return ".".join(parts)


def _parse_junit(path):
    """{'module.path::test_name': 'passed'|'failed'|'error'|'skipped'}.

    The node id is rebuilt from ``classname`` + ``name`` rather than taken from
    pytest's own ``-v`` output, because ``--pyargs`` prints file-system paths
    into ``site-packages`` and those move between steps as the interpreter
    version changes. ``utils.tests.test_util::test_shuffle`` does not.
    """
    try:
        tree = ElementTree.parse(path)
    except Exception:
        return None
    out = {}
    for case in tree.iter("testcase"):
        classname = _normalize_classname(case.get("classname"))
        name = case.get("name") or ""
        if not name:
            continue
        node = (classname + "::" + name) if classname else name
        outcome = "passed"
        for child in case:
            tag = child.tag
            if tag == "failure":
                outcome = "failed"
            elif tag == "error":
                outcome = "error"
            elif tag == "skipped":
                outcome = "skipped"
        # A parameterised id can repeat across classes; worst outcome wins so
        # one failing parameterisation is never averaged away by a passing one.
        rank = {"passed": 0, "skipped": 1, "failed": 2, "error": 3}
        if node in out and rank[out[node]] >= rank[outcome]:
            continue
        out[node] = outcome
    return out


def score(expected, observed):
    """The floor, as one number in [0, 1], plus what moved.

    Denominator is the recorded node count, so deleting a test lowers the
    score by exactly the weight that test carried. There is no credit for
    tests the candidate added: rule 6 says what the agent writes is not
    evidence.
    """
    want = expected.get("outcomes") or {}
    got = (observed.get("outcomes") or {}) if observed else {}
    missing = []
    regressed = []
    repaired = []
    still_failing = []
    for node in sorted(want):
        before = want[node]
        if node not in got:
            missing.append(node)
            continue
        after = got[node]
        if before == "passed":
            if after != "passed":
                regressed.append([node, after])
        else:
            if after == "passed":
                repaired.append(node)
            else:
                still_failing.append(node)

    total = len(want)
    if not total:
        # An empty expectation cannot be evidence of anything. Trap 4 of the
        # authoring prompt in miniature: a mean over an empty set is not a 1.
        return {"score": 0.0, "total": 0, "error": "empty expectation",
                "missing": [], "regressed": [], "repaired": [],
                "still_failing": []}
    if observed and observed.get("error"):
        return {"score": 0.0, "total": total,
                "error": "suite did not run: %s" % observed["error"],
                "missing": sorted(want), "regressed": [], "repaired": [],
                "still_failing": [], "tail": observed.get("tail", [])}

    lost = len(missing) + len(regressed)
    return {"score": round(max(0.0, (total - lost) / float(total)), 6),
            "total": total, "error": None,
            "missing": missing, "regressed": regressed,
            "repaired": repaired, "still_failing": still_failing,
            "collected": len(got)}


def _write(path, payload):
    if not path:
        return
    with open(path, "w") as fh:
        json.dump(payload, fh, indent=1, sort_keys=True)


def _census(outcomes):
    out = {}
    for outcome in outcomes.values():
        out[outcome] = out.get(outcome, 0) + 1
    return out


def _print_report(report):
    print("inherited suite: %.4f (%d recorded, %d collected, %d missing, "
          "%d regressed, %d repaired)"
          % (report["score"], report["total"], report.get("collected", 0),
             len(report["missing"]), len(report["regressed"]),
             len(report["repaired"])))
    if report.get("error"):
        print("  !! %s" % report["error"])
    for node in report["missing"][:10]:
        print("  missing    %s" % node)
    for node, outcome in report["regressed"][:10]:
        print("  regressed  %s -> %s" % (node, outcome))


def main(argv=None):
    p = argparse.ArgumentParser(prog="skutil_gates.suite")
    sub = p.add_subparsers(dest="cmd")

    r = sub.add_parser("record", help="author-side: capture the entry tree's "
                                     "outcome table")
    r.add_argument("--out", required=True)
    r.add_argument("--timeout", type=int, default=1800)

    c = sub.add_parser("capture", help="verifier-side, unprivileged: run the "
                                       "candidate's suite, write the raw table")
    c.add_argument("--out", required=True)
    c.add_argument("--timeout", type=int, default=1800)

    m = sub.add_parser("compare", help="verifier-side, privileged: score a "
                                       "captured table against the expectation")
    m.add_argument("--expected", required=True)
    m.add_argument("--observed", required=True)
    m.add_argument("--out")

    s = sub.add_parser("score")
    s.add_argument("--expected", required=True)
    s.add_argument("--out")
    s.add_argument("--timeout", type=int, default=1800)

    args = p.parse_args(argv)
    if args.cmd == "capture":
        # Always writes, always exits 0. A suite that did not run is a 0 for
        # this family and a line in the report saying why -- never a missing
        # file that the next stage has to guess the meaning of.
        got = run(timeout=args.timeout)
        got["census"] = _census(got.get("outcomes") or {})
        _write(args.out, got)
        print("captured %d test outcomes -> %s  %s%s"
              % (len(got.get("outcomes") or {}), args.out,
                 json.dumps(got["census"], sort_keys=True),
                 "" if not got.get("error") else "  error=" + got["error"]))
        return 0

    if args.cmd == "compare":
        with open(args.expected) as fh:
            expected = json.load(fh)
        try:
            with open(args.observed) as fh:
                observed = json.load(fh)
        except (IOError, ValueError) as exc:
            observed = {"error": "unreadable capture: %s" % exc,
                        "outcomes": {}}
        report = score(expected, observed)
        report["tail"] = observed.get("tail", [])
        _write(args.out, report)
        _print_report(report)
        return 0

    if args.cmd == "record":
        got = run(timeout=args.timeout)
        if got["error"]:
            print("suite did not run: %s" % got["error"])
            for line in got.get("tail", []):
                print("    %s" % line)
            return 1
        census = _census(got["outcomes"])
        _write(args.out, {"outcomes": got["outcomes"], "census": census})
        print("recorded %d test outcomes -> %s  %s"
              % (len(got["outcomes"]), args.out,
                 json.dumps(census, sort_keys=True)))
        return 0

    if args.cmd == "score":
        with open(args.expected) as fh:
            expected = json.load(fh)
        got = run(timeout=args.timeout)
        report = score(expected, got)
        report["tail"] = got.get("tail", [])
        _write(args.out, report)
        _print_report(report)
        return 0

    p.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
