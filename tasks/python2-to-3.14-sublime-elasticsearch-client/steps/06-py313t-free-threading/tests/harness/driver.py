# -*- coding: utf-8 -*-
"""Record the baseline, and replay the recording against a candidate tree.

    python -m harness.driver record <tree> <out.json>  [--python P] [--jobs N]
    python -m harness.driver replay <tree> <rec.json> <report.json>
                                    [--flips 01.json,02.json] [...]
    python -m harness.driver audit  <rec.json>
    python -m harness.driver list

Recording runs every case twice, in two passes separated in wall-clock time, and
keeps only the cases whose two observations are identical. The two-pass rule is
also what proves the
normalisation in ``normalize.py`` is complete — an unnormalised clock reading
differs between the passes and the case is dropped rather than shipped flaky.

``audit`` reads a recording back and reports every case whose observation is a
*top-level* raise. Some of those are the point — the transport's error mapping
is scored by the exception it raises — but ``AttributeError`` / ``TypeError`` /
``NameError`` / ``ImportError`` at the top level of a case almost always means
the case body called something the vendored library of this vintage does not
have, and the "observation" is then a pin on a mistake in the case rather than
on the tree. Run it over every new recording.
"""
import argparse
import gzip
import json
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
PKG_PARENT = os.path.dirname(HERE)


def _open(path, mode="rt"):
    """``open``, except that ``.gz`` is transparently (de)compressed.

    The recording is ~36 MB of JSON and ~1.4 MB gzipped, and it is copied into
    the docker build context of *every* step's verifier. Shipping it compressed
    takes about 245 MB of build context out of the package. Keyed on the
    extension rather than on a flag so that one path string is the whole
    contract: the file is named ``.json.gz`` and the recorder, the replayer and
    ``audit`` all agree without another argument to get wrong.
    """
    if path.endswith(".gz"):
        return gzip.open(path, mode, **({"compresslevel": 9}
                                        if "w" in mode else {}))
    return open(path, mode)


#: The identity a case runs as when the driver is root (the verifier). Same
#: uid score.py drops the import probe to; see score.unprivileged.
CASE_UID = "65534"
CASE_GID = "65534"


def _case_argv(argv):
    """A case runs the tree, so it never runs as root. The driver itself does
    when the verifier starts it -- it holds the recording, which no case may
    read -- and drops each case here. Recording as an ordinary user runs the
    case as that user, unchanged."""
    if os.geteuid() != 0:
        return list(argv)
    return ["setpriv", "--reuid", CASE_UID, "--regid", CASE_GID,
            "--clear-groups", "env", "HOME=/tmp", "TMPDIR=/tmp"] + list(argv)


def _run_one(python, tree, cid, timeout, deadline=0.0, cwd=None):
    envv = dict(os.environ)
    envv["PYTHONPATH"] = PKG_PARENT + os.pathsep + envv.get("PYTHONPATH", "")
    envv["PYTHONHASHSEED"] = "0"
    envv["TZ"] = "UTC"
    envv["PYTHONDONTWRITEBYTECODE"] = "1"
    envv.setdefault("PYTHONPYCACHEPREFIX", "/tmp/hpyc")
    # The whole replay is held to the scorer's deadline, not only each case to
    # its own timeout: 544 cases at 120 s each over 8 workers is far longer
    # than the verifier's budget. A case that would start after the deadline
    # does not start, and one that starts near it gets only what is left.
    # Either way it fails, which is what a case that never answered is.
    if deadline:
        left = deadline - time.time()
        if left < 1:
            return cid, {"harness_error": "not run: the scorer's time budget "
                                          "was spent before this case"}
        timeout = min(timeout, left)
    cmd = _case_argv([python, "-W", "ignore", "-m", "harness.runcase",
                      tree, cid])
    try:
        p = subprocess.run(cmd, capture_output=True, env=envv, timeout=timeout,
                           cwd=cwd)
    except subprocess.TimeoutExpired:
        return cid, {"harness_error": "timeout after %ss" % int(timeout)}
    out = p.stdout.decode("utf-8", "replace").strip()
    if not out:
        return cid, {"harness_error": "no output (rc=%d): %s"
                     % (p.returncode, p.stderr.decode("utf-8", "replace")[-800:])}
    try:
        payload = json.loads(out.splitlines()[-1])
    except ValueError:
        return cid, {"harness_error": "unparseable output: %s" % out[-800:]}
    if "harness_error" in payload:
        return cid, {"harness_error": payload["harness_error"]}
    return cid, payload["obs"]


def _pass(python, tree, ids, jobs, timeout, deadline=0.0, cwd=None):
    results = {}
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(_run_one, python, tree, c, timeout, deadline,
                               cwd) for c in ids]
        for f in futures:
            cid, obs = f.result()
            results[cid] = obs
    return results


def _ids():
    sys.path.insert(0, PKG_PARENT)
    from harness import registry
    registry.load_all()
    return registry.ids(), registry.CASES


def _certs():
    """Build the TLS fixture once, for every case subprocess to share.

    Without this each TLS case would generate its own CA, twice over for the
    two recording passes. Correct either way -- a case is its own
    process and never sees another's material -- but eighteen RSA keygens is a
    pointless minute.
    """
    sys.path.insert(0, PKG_PARENT)
    from harness import certs
    return certs.ensure()


def cmd_list(args):
    ids, cases = _ids()
    for cid in ids:
        print("%-52s %s" % (cid, ",".join(cases[cid].tags)))
    adv = sum(1 for c in cases.values() if c.adversarial)
    print("\n%d cases, %d adversarial (%.0f%%)"
          % (len(ids), adv, 100.0 * adv / max(1, len(ids))))
    return 0


def cmd_record(args):
    ids, cases = _ids()
    # Honoured here as well as in replay: a flag that silently does nothing on
    # one subcommand is a trap.
    ids = [c for c in ids if _selected(c, None, args)]
    _certs()
    t0 = time.time()
    first = _pass(args.python, args.tree, ids, args.jobs, args.timeout)
    time.sleep(args.gap)
    second = _pass(args.python, args.tree, ids, args.jobs, args.timeout)

    kept, dropped = {}, {}
    for cid in ids:
        a, b = first[cid], second[cid]
        if "harness_error" in a or "harness_error" in b:
            dropped[cid] = {"reason": "harness_error",
                            "detail": a.get("harness_error") or b.get("harness_error")}
            continue
        if json.dumps(a, sort_keys=True) != json.dumps(b, sort_keys=True):
            dropped[cid] = {"reason": "unstable", "detail": _first_diff(a, b)}
            continue
        kept[cid] = a

    meta = {
        "tool": "harness.driver record",
        "python": subprocess.run([args.python, "-V"], capture_output=True)
                  .stdout.decode().strip()
                  or subprocess.run([args.python, "-V"], capture_output=True)
                  .stderr.decode().strip(),
        "tree": os.path.abspath(args.tree),
        "seed": "PYTHONHASHSEED=0 TZ=UTC",
        "passes": 2,
        "gap_sec": args.gap,
        "elapsed_sec": round(time.time() - t0, 1),
        "n_recorded": len(kept),
        "n_dropped": len(dropped),
        "tags": {cid: list(cases[cid].tags) for cid in kept},
    }
    with _open(args.out, "wt") as fh:
        json.dump({"meta": meta, "cases": kept, "dropped": dropped},
                  fh, sort_keys=True, indent=1)
    print("recorded %d, dropped %d, %.1fs -> %s"
          % (len(kept), len(dropped), time.time() - t0, args.out))
    for cid, why in sorted(dropped.items()):
        print("  DROPPED %-48s %s: %s" % (cid, why["reason"], str(why["detail"])[:200]))
    return 0


def _first_diff(a, b, path=""):
    if type(a) is not type(b):
        return "%s: %s vs %s" % (path, type(a).__name__, type(b).__name__)
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a:
                return "%s.%s: missing on left" % (path, k)
            if k not in b:
                return "%s.%s: missing on right" % (path, k)
            d = _first_diff(a[k], b[k], "%s.%s" % (path, k))
            if d:
                return d
        return ""
    if isinstance(a, list):
        if len(a) != len(b):
            return "%s: len %d vs %d" % (path, len(a), len(b))
        for i, (x, y) in enumerate(zip(a, b)):
            d = _first_diff(x, y, "%s[%d]" % (path, i))
            if d:
                return d
        return ""
    if a != b:
        return "%s: %r != %r" % (path, a, b)
    return ""


def apply_flips(expected, paths, own_step=None):
    """Fold the step overlays in ``paths`` (in order) into ``expected``.

    The baseline is the oracle: every case's expected observation is the one
    recorded on 3.8, and a step that changes an unrelated answer fails.
    But two of the rungs *require* an answer to change -- step 02 has to
    aggregate the per-attempt failures the retry loop currently discards, step
    07 has to stop ``return`` in a ``finally`` from swallowing a BaseException
    -- and a few cases drive a module a later step deletes outright.

    Rather than re-record the whole suite per rung (which would let an
    unintended change ride along inside the new recording and never be seen),
    each such case is named in a small per-step overlay:

    ``{"step": ..., "why": ..., "cases": {<id>: {"expect": <record>,
                                                 "reason": ...}}}``

    ``expect`` replaces the expected observation from this rung on; ``retired``
    drops the case from the scored set from this rung on, for cases whose
    subject the step removes; ``revive`` puts a retired case back, at the
    answer the recording already holds. Every entry is an explicit, reviewable
    line in a file, and every one of them has to be stated in that step's
    instruction.md -- the candidate is not shown the suite, so an expectation
    it cannot read off the instruction is an unpassable test.

    ``own_step`` names which of these overlays belongs to the rung being scored,
    and the third return value is the set of cases that overlay claims. That set
    is the rung's *own* behavioural bar, and the separation is the whole reason
    the parameter exists: the untouched predecessor tree has to pass family 1
    completely and fail family 2 completely, and a flipped case scored inside
    family 1 breaks both halves of that at once -- the
    predecessor fails six cases it "preserves" correctly, and the rung's
    headline requirement is worth 6/481 of a 0.40 weight, which is 0.5% of the
    step. An overlay entry earns membership with ``expect`` (the answer changed)
    or with an explicit ``"owned": true`` (the answer did not change, but the
    conditions it is replayed under did -- step 06 replays with ``PYTHON_GIL=0``
    and the recorded answer stands only if the shared state is guarded).

    Matching is on the overlay's own ``step`` key rather than on list
    position: the list is cumulative and a rung with no overlay of its own
    (04, 05) would otherwise inherit its predecessor's flips as its bar.

    Returns ``(expected, retired, owned)``.
    """
    expected = dict(expected)
    retired, shelved, owned = {}, {}, set()
    for path in paths:
        with _open(path) as fh:
            overlay = json.load(fh)
        mine = own_step is not None and overlay.get("step") == own_step
        for cid, entry in overlay["cases"].items():
            if cid not in expected and cid not in retired:
                raise KeyError("%s: overlay names an unrecorded case %r"
                               % (path, cid))
            if entry.get("retired"):
                retired[cid] = entry["retired"]
                if cid in expected:
                    # Keep the expectation as it stood, so that a later rung
                    # can revive the case without restating it.
                    shelved[cid] = expected.pop(cid)
                owned.discard(cid)
                continue
            if entry.get("revive"):
                # A rung may bring back a case an earlier rung retired. The
                # four freethread races are the case in point: they are not
                # preservation behaviour on a GIL-ful build (their value is a
                # property of the scheduler, not of the tree), so 01 retires
                # them and 06 -- the rung whose instruction is about thread
                # safety -- brings them back as its own bar.
                #
                # The answer they come back at is the one that was in force
                # when they were retired, not a copy of it pasted into this
                # file. A copy is a second oracle that nothing checks: amend
                # the case and the recording moves while the copy does not, and
                # the rung goes on scoring an answer the suite no longer
                # produces -- silently, because a stale expectation looks
                # exactly like a rung nobody has passed yet. Reviving by
                # reference means there is one oracle and the drift cannot
                # happen.
                if cid not in shelved:
                    raise KeyError("%s: %r is revived but nothing before this "
                                   "overlay retired it" % (path, cid))
                if "expect" in entry:
                    # The two are alternatives, and without this check
                    # `revive` would silently win.
                    # A rung bringing a case back at a *new* answer writes
                    # `expect` alone -- which un-retires it as well -- and one
                    # that wants the old answer writes `revive` alone. Both in
                    # one entry reads as "revive it, at this", which is the one
                    # thing it does not do.
                    raise KeyError("%s: %r has both `revive` and `expect`. "
                                   "`revive` restores the shelved answer and "
                                   "ignores `expect`; to bring a case back at "
                                   "a new answer use `expect` on its own."
                                   % (path, cid))
                expected[cid] = shelved.pop(cid)
                # Dropping the retirement keeps the replay report's counts
                # honest: a case cannot be both retired and replayed.
                retired.pop(cid, None)
            elif "expect" in entry:
                expected[cid] = entry["expect"]
                retired.pop(cid, None)
            elif not entry.get("owned"):
                raise KeyError("%s: entry %r has neither expect, retired, "
                               "revive nor owned, so it says nothing"
                               % (path, cid))
            if mine:
                owned.add(cid)
    return expected, retired, owned


def cmd_replay(args):
    with _open(args.recording) as fh:
        rec = json.load(fh)
    expected, retired, owned = apply_flips(
        rec["cases"], [p for p in args.flips.split(",") if p], args.own_step)
    ids = [c for c in sorted(expected) if _selected(c, expected, args)]
    _certs()
    got = _pass(args.python, args.tree, ids, args.jobs, args.timeout,
                args.deadline, args.case_cwd)

    passed, failed = [], {}
    for cid in ids:
        want = json.dumps(expected[cid], sort_keys=True)
        obs = got[cid]
        if "harness_error" in obs:
            failed[cid] = {"reason": "harness_error", "detail": obs["harness_error"][-600:]}
            continue
        if json.dumps(obs, sort_keys=True) == want:
            passed.append(cid)
        else:
            failed[cid] = {"reason": "mismatch",
                           "detail": _first_diff(expected[cid], obs)}

    # The two halves are counted here rather than by the caller because this is
    # the only place that knows which ids were actually selected: --only would
    # otherwise let a filtered run report an owned score over cases it skipped.
    own_ids = [c for c in ids if c in owned]
    inh_ids = [c for c in ids if c not in owned]
    ok = set(passed)

    def half(sel):
        hit = sum(1 for c in sel if c in ok)
        return {"total": len(sel), "passed": hit,
                "score": (float(hit) / len(sel)) if sel else None}

    report = {
        "n_total": len(ids),
        "n_passed": len(passed),
        "n_failed": len(failed),
        "score": (len(passed) / len(ids)) if ids else 0.0,
        "failed": failed,
        # Family 1 and family 2's behavioural halves. ``owned`` is null-scored
        # rather than 1.0 when empty, so that a step which claims a bar and then
        # ships an overlay naming nothing is refused by score.py instead of
        # being handed the weight for an empty set.
        "owned": sorted(owned),
        "owned_result": half(own_ids),
        "inherited_result": half(inh_ids),
        # Named in the report so that a rung which scores well because its
        # overlay retired the hard half cannot look the same as one that did
        # not.
        "retired": retired,
    }
    with open(args.report, "w") as fh:
        json.dump(report, fh, sort_keys=True, indent=1)
    print("behaviour: %d/%d passed (%.4f), owned %d/%d, %d retired by overlay"
          % (len(passed), len(ids), report["score"],
             report["owned_result"]["passed"], report["owned_result"]["total"],
             len(retired)))
    for cid, why in sorted(failed.items())[:40]:
        print("  FAIL %-48s %s: %s" % (cid, why["reason"], str(why["detail"])[:220]))
    return 0


#: Exception types a case body raises on purpose, because the tree raises them
#: as part of the behaviour being pinned. Anything else at the top level of a
#: case is treated as suspect and listed for triage.
DELIBERATE_RAISES = (
    "elasticsearch.exceptions.",     # the client's own error mapping
    "elasticsearch.helpers.",        # BulkIndexError, ScanError
    "requests.exceptions.",
    "urllib3.exceptions.",
    "builtins.ValueError",           # required-argument guards, parser rejects
    "builtins.KeyError",
    "builtins.StopIteration",
    "exceptions.ValueError",         # py2 spelling, for a 2.7 cross-check
    "exceptions.KeyError",
)


def cmd_audit(args):
    with _open(args.recording) as fh:
        rec = json.load(fh)
    cases = rec["cases"]
    raised, suspect = {}, {}
    for cid, obs in sorted(cases.items()):
        exc = (obs.get("raised") or {}).get("__exc__")
        if not exc:
            continue
        raised[cid] = exc
        if not exc.startswith(DELIBERATE_RAISES):
            suspect[cid] = "%s: %s" % (exc, (obs["raised"].get("str") or "")[:120])

    by_type = {}
    for cid, exc in raised.items():
        by_type.setdefault(exc, []).append(cid)

    print("%d cases recorded, %d raise at the top level" % (len(cases), len(raised)))
    for exc in sorted(by_type):
        print("  %-44s %d" % (exc, len(by_type[exc])))
    if suspect:
        print("\n%d SUSPECT — an exception type the tree is not being asked for:"
              % len(suspect))
        for cid, detail in sorted(suspect.items()):
            print("  %-48s %s" % (cid, detail))
    else:
        print("\nno suspect top-level raises")
    if rec.get("dropped"):
        print("\n%d dropped at record time:" % len(rec["dropped"]))
        for cid, why in sorted(rec["dropped"].items()):
            print("  %-48s %s: %s" % (cid, why["reason"], str(why["detail"])[:160]))
    return 1 if suspect else 0


def _selected(cid, expected, args):
    if args.only:
        return any(cid.startswith(p) for p in args.only.split(","))
    return True


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        p.add_argument("--python", default=sys.executable)
        p.add_argument("--jobs", type=int, default=12)
        p.add_argument("--timeout", type=int, default=120)
        p.add_argument("--only", default="")
        # Epoch seconds after which no case starts (0: none). The verifier
        # passes the scorer's deadline less what the gates need.
        p.add_argument("--deadline", type=float, default=0.0)
        # Working directory for each case (default: this process's). The
        # verifier passes the tree as it arrived.
        p.add_argument("--case-cwd", default=None, dest="case_cwd")

    p = sub.add_parser("record")
    p.add_argument("tree")
    p.add_argument("out")
    p.add_argument("--gap", type=float, default=1.5)
    common(p)
    p.set_defaults(fn=cmd_record)

    p = sub.add_parser("replay")
    p.add_argument("tree")
    p.add_argument("recording")
    p.add_argument("report")
    p.add_argument("--flips", default="",
                   help="comma-separated step overlays, applied in order")
    p.add_argument("--own-step", default=None, dest="own_step",
                   help="which overlay's cases are this rung's own bar; "
                        "matched against the overlay's 'step' key")
    common(p)
    p.set_defaults(fn=cmd_replay)

    p = sub.add_parser("audit")
    p.add_argument("recording")
    p.set_defaults(fn=cmd_audit)

    p = sub.add_parser("list")
    p.set_defaults(fn=cmd_list)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
