# -*- coding: utf-8 -*-
"""The verifier runner: one step's reward, from a pristine copy of ``/app``.

    python -m harness.score --config /tests/step.json --tree /app \\
                            --report /tmp/score.json

This is glue and nothing else. Every expectation it compares against came
from the baseline recording; every gate it runs is ``gates.py``'s. It decides
no thresholds of its own -- the weights and the check list arrive in
``step.json``, which is derived from the one table in ``gates.py``.

**Two families, four components.** The untouched previous-step tree passes
family 1 completely and fails family 2 completely. Both families are drawn
from both instruments rather than one each:

* **Family 1, preservation** -- everything this rung inherits. The recorded
  cases this rung does *not* claim (``behaviour_inherited``) plus the gates
  earlier rungs added (``inherited_gates``). A finished step k-1 tree scores
  this family 1.00 by construction: it is exactly what step k-1 was scored on.
* **Family 2, the rung's own bar** -- the recorded cases this rung's overlay
  claims (``behaviour_new``) plus the gates this rung adds (``new_gates``). A
  step k-1 tree scores this 0.00, because every member is something step k-1
  was not asked for and does not do.

**The two families multiply**: the reward is T1 x T2, T1 the rung's own bar
and T2 preservation, each the weighted mean of its own two components, less
the guard penalty. A previous-step tree, which fails the own bar completely,
therefore scores 0 however much it preserves.

The weights in ``step.json`` are per rung and sum to 1 over the four
components; because the families multiply, what they fix is the proportion
*inside* each family (0.25 / 0.05 inside preservation, for instance). Step 01
spends its family 1 entirely on behaviour (0.30 / 0.00) and its family 2
entirely on gates (0.00 / 0.70); step 02 is the mirror image (0.25 / 0.05 and
0.70 / 0.00), because its leap is a changed answer rather than a removed
spelling.

The split within each family follows from the gate table and the overlay, so a
rung cannot be handed weight for a component with no members: see the
``blocked`` path below.

Why the behaviour suite lands in both families: the recording survives every
rung, so a rung that breaks an earlier rung's answer pays for it in family 1,
while the 21 cases step 02 *must* flip are the whole substance of step 02 and
belong in family 2. Scoring those 21 inside family 1 would make the predecessor
fail family 1 and would price step 02's headline requirement at 21/495 of 0.30
-- one and a quarter percent of the step. The same arithmetic run the other way
is the reason a requirement that a rung must *preserve* is cheap here by
construction, and why anything a rung is really being asked to do has to be
reachable from a case the predecessor tree fails.

Each gate is scored at zero tolerance, pass or fail, and the component is the
fraction of gates passed: partial credit lives at gate granularity, not
inside a gate, so "I fixed all but one `unicode`" is a failed gate rather than
a 99% one.

Two things this module refuses to do quietly:

* An empty or near-empty ``/app`` is a broken run, not a zero. A tree with no
  Python in it, or with implausibly few files against the baseline's 294,
  reports ``reward 0`` *and* says so in the report's ``blocked`` field, so a
  partial score from a broken mount cannot be mistaken for a candidate that
  did badly.
* The case count is asserted against the recording. If the replay comes back
  having scored fewer cases than the recording holds, the denominator moved,
  and a mean over a shrunken denominator is exactly the failure that hides a
  crashed step.
"""
from __future__ import print_function

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PKG_PARENT = os.path.dirname(HERE)

#: The baseline has 294 ``.py`` files. A candidate may legitimately delete some
#: -- the ladder requires deleting nine of them -- but not most of them, and a
#: tree that arrives with a handful is an empty-artifact accident rather than a
#: migration. Deliberately far below any plausible real tree.
MIN_PY_FILES = 200

#: Subtracted per guard gate that fires (see :data:`gates.GUARDS`). Large
#: enough that introducing a construct the ladder has spent a step deleting is
#: never free, small enough that it cannot by itself turn a finished step into
#: a failure -- a guard measures a blemish on correct work, not the work.
GUARD_PENALTY = 0.05

#: The two factors of the reward and the step.json components each is the
#: weighted mean of. T1 first: the rung's own bar, which a step k-1 tree fails.
#: T2: preservation, which a step k-1 tree passes. See "the reward" in main.
FACTORS = (("own_bar", ("behaviour_new", "new_gates")),
           ("preservation", ("behaviour_inherited", "inherited_gates")))

#: The libraries the plugin itself loads from ``lib/`` -- the library half of
#: ``module_load_order`` in ``ElasticsearchClient.py``, minus ``six``, which
#: rung 01 deletes. Every instruction names an interpreter "the tree must
#: import and run under"; these are what "the tree imports" means for a
#: library whose plugin modules need ``sublime`` and cannot be imported here.
#:
#: A precondition and not a gate: a tree whose libraries do not import under
#: the rung's interpreter has not reached the rung, and it scores 0. Without
#: it the untouched baseline -- the no-op tree -- would keep a few dozen
#: ``dateutil`` cases alive on every interpreter (``urllib3`` dies at ``from
#: collections import Mapping`` on 3.10+, ``dateutil`` does not reach it) and
#: score above 0 at step 01, where a no-op tree must score exactly 0.
LIVENESS_IMPORTS = ("dateutil", "requests", "urllib3", "analytics",
                    "elasticsearch", "elasticsearch_connections")

_IMPORT_PROBE = r"""
import importlib, os, sys
tree = sys.argv[1]
sys.path[:0] = [tree, os.path.join(tree, "lib")]
bad = []
for name in sys.argv[2:]:
    try:
        importlib.import_module(name)
    except BaseException as exc:
        bad.append("%s (%s: %s)" % (name, type(exc).__name__, str(exc)[:200]))
print("; ".join(bad))
sys.exit(1 if bad else 0)
"""


#: The identity every process that runs candidate code drops to. The scorer
#: itself runs as root: it holds the recording, the gate tree and the REWARD
#: line, so nothing the candidate runs may share its uid. A scorer running as
#: uid 65534 would put the line root turns into reward.json in a process the
#: candidate's own uid could reach through /proc, and the gate tree in a
#: directory a case could rewrite after its behaviour was banked.
SCORE_UID = 65534
SCORE_GID = 65534

#: Seconds of the scorer's deadline kept back for the gate family. The replay
#: is the only phase whose length the candidate controls case by case, so it
#: is the phase that is cut short; the gates are static reads plus the pinned
#: checker and linters over a ~300-file tree, a small fraction of this.
GATE_RESERVE = 420


def unprivileged(argv):
    """``argv`` run as :data:`SCORE_UID` when this process is root.

    Unchanged when it is not, so the recorder can run as an ordinary
    user."""
    if os.geteuid() != 0:
        return list(argv)
    return (["setpriv", "--reuid", str(SCORE_UID), "--regid", str(SCORE_GID),
             "--clear-groups", "env", "HOME=/tmp", "TMPDIR=/tmp"]
            + list(argv))


def reap():
    """Kill every process of the candidate's uid, until none is left.

    A case or the import probe can leave a detached child behind. Nothing it
    can reach is scored -- the gate tree is root's own copy -- but a
    straggler still competes for the CPU the next phase is timed on, so each
    phase starts with none."""
    if os.geteuid() != 0:
        return
    for _ in range(50):
        subprocess.call(["pkill", "-KILL", "-u", str(SCORE_UID)],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if subprocess.call(["pgrep", "-u", str(SCORE_UID)],
                           stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL) != 0:
            return
        time.sleep(0.1)


def give_to_scorer_uid(path):
    """Hand a copy to :data:`SCORE_UID` without following a link in it."""
    if os.geteuid() != 0:
        return
    os.lchown(path, SCORE_UID, SCORE_GID)
    for dirpath, dirnames, filenames in os.walk(path):
        for name in dirnames + filenames:
            os.lchown(os.path.join(dirpath, name), SCORE_UID, SCORE_GID)


def pristine(tree, dest):
    """Copy ``tree`` to ``dest``, dropping bytecode. Returns the file count.

    The scorer works from its own copy. Two reasons it matters here beyond
    hygiene: a case body that writes into the tree cannot then be seen by the
    next case, and ``__pycache__`` written by the candidate's interpreter
    cannot be read by the verifier's. The second is not hypothetical -- a
    3.10 ``.pyc`` in a tree replayed on 3.12 is ignored, but one written by a
    *newer* interpreter than the one replaying is a magic-number error at
    import, which reads as a broken migration.
    """
    if os.path.exists(dest):
        shutil.rmtree(dest)
    shutil.copytree(tree, dest, symlinks=True,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc",
                                                  "*.pyo"))
    n = 0
    for dirpath, dirnames, filenames in os.walk(dest):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        n += sum(1 for f in filenames if f.endswith(".py"))
    return n


def importable(python, tree, extra_env=None, timeout=120):
    """``""`` if every :data:`LIVENESS_IMPORTS` module imports from ``tree``,
    else a one-line account of what did not."""
    env = dict(os.environ)
    env.pop("PYTHONPATH", None)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env.update(extra_env or {})
    try:
        p = subprocess.run(unprivileged(
                               [python, "-W", "ignore", "-c", _IMPORT_PROBE,
                                tree] + list(LIVENESS_IMPORTS)),
                           cwd=tree, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "import did not finish in %ss" % timeout
    if p.returncode == 0:
        return ""
    text = p.stdout.decode("utf-8", "replace").strip()
    return (text.splitlines()[-1] if text else
            "import probe exited %d" % p.returncode)[:1200]


#: How the root replay driver is started: ``harness.driver`` run as
#: ``__main__`` from the scorer's own /tests, under ``-I``.
_DRIVER_BOOT = ("import runpy, sys; sys.path.insert(0, %r); "
                "runpy.run_module('harness.driver', run_name='__main__', "
                "alter_sys=True)" % (PKG_PARENT,))


def behaviour(python, tree, recording, flips, jobs, timeout, report_path,
              own_step=None, extra_env=None, deadline=None, case_cwd=None):
    """Replay the recorded suite. Returns the replay report dict.

    ``extra_env`` is step.json's ``env`` block, applied to every case
    subprocess. One knob, set in one file: step 06 is the reason it exists --
    the free-threaded rung replays the whole suite with ``PYTHON_GIL=0``, and
    threading that through as a per-case directive would have put a second
    scheduling model in the driver for one rung's benefit.
    """
    env = dict(os.environ)
    env["PYTHONPATH"] = PKG_PARENT + os.pathsep + env.get("PYTHONPATH", "")
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env.update(extra_env or {})
    # The driver runs as root and reads the recording, so it starts the way
    # test.sh starts this scorer: isolated (``-I``: no cwd, no user site and no
    # PYTHON* variable on its own start-up -- in particular no inherited
    # PYTHONPYCACHEPREFIX, whose directory a case's uid could have filled with
    # bytecode for this very module), with /tests put on the path
    # explicitly and cwd ``/``. ``env`` still reaches the cases it starts:
    # isolation governs the driver's own interpreter, not ``os.environ``.
    cmd = [python, "-I", "-B", "-c", _DRIVER_BOOT, "replay", tree, recording,
           report_path, "--python", python, "--jobs", str(jobs),
           "--timeout", str(timeout)]
    if own_step:
        cmd += ["--own-step", own_step]
    if flips:
        cmd += ["--flips", ",".join(flips)]
    if deadline:
        cmd += ["--deadline", "%.0f" % deadline]
    if case_cwd:
        cmd += ["--case-cwd", case_cwd]
    # The driver stops starting cases at ``deadline`` and caps each one at
    # what is left, so it returns within one case timeout of it; past that it
    # is killed and the replay scores as not having produced a report.
    wait = (max(60.0, deadline - time.time() + timeout + 60)
            if deadline else None)
    try:
        p = subprocess.run(cmd, env=env, cwd="/", stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=wait)
        out = p.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as exc:
        out = (exc.output or b"").decode("utf-8", "replace") \
            + "\n[driver killed at the scorer's deadline]"
    if not os.path.exists(report_path):
        return {"n_total": 0, "n_passed": 0, "score": 0.0,
                "crashed": out[-4000:]}
    with open(report_path) as fh:
        rep = json.load(fh)
    rep["stdout_tail"] = out[-2000:]
    return rep


def gate_family(tree, checks, cache):
    """Score a list of checks at zero tolerance. Returns ``(fraction, rows)``."""
    rows, passed = [], 0
    for c in checks:
        found = c.run(tree, cache)
        ok = not found
        passed += 1 if ok else 0
        rows.append({"id": c.cid, "what": c.what, "members": c.members,
                     "n": len(found), "passed": ok,
                     # Enough to diagnose, bounded so a tree that fails a gate
                     # 3,000 times cannot produce a 3,000-row report.
                     "sample": found[:12]})
    return (float(passed) / len(checks) if checks else None), rows


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", required=True)
    ap.add_argument("--tree", default="/app")
    ap.add_argument("--report", default="/tmp/score.json")
    ap.add_argument("--scratch", default="/tmp/pristine")
    # Root's own copy of the tree, which every gate reads. Its parent must be
    # a directory the candidate's uid cannot enter (test.sh: 0700 root).
    ap.add_argument("--gate-tree", default=None)
    # Epoch seconds by which this process must have printed REWARD (test.sh
    # kills it shortly after). 0 means no deadline.
    ap.add_argument("--deadline", type=float, default=0.0)
    args = ap.parse_args(argv)

    t0 = time.time()
    with open(args.config) as fh:
        cfg = json.load(fh)
    here = os.path.dirname(os.path.abspath(args.config))
    step_id = cfg["step"]
    weights = cfg["weights"]
    recording = os.path.join(here, cfg["recording"])
    flips = [os.path.join(here, f) for f in cfg.get("flips", ())]

    out = {"step": step_id, "weights": weights, "blocked": None,
           "python": sys.version.split()[0], "reward": 0.0}

    # ---- the tree arrives, or it does not -------------------------------
    # Two copies of the tree, both taken before any candidate code runs. The
    # replay copy goes to the candidate's uid, so the tree behaves exactly as
    # it was recorded. The gate copy stays root's: the manifest rail and every gate
    # read it, and a process a case left behind cannot rewrite it after the
    # behaviour it produced has been banked.
    gate_tree = args.gate_tree or args.scratch
    if not os.path.isdir(args.tree):
        out["blocked"] = "no %s at all" % args.tree
    else:
        n_py = pristine(args.tree, gate_tree)
        if gate_tree != args.scratch:
            pristine(args.tree, args.scratch)
            give_to_scorer_uid(args.scratch)
        out["n_py_files"] = n_py
        if n_py < MIN_PY_FILES:
            out["blocked"] = ("%s holds %d .py files; the baseline has 294 and "
                              "the ladder deletes at most nine. This is an "
                              "empty or partial artifact, not a migration."
                              % (args.tree, n_py))
    if out["blocked"]:
        _emit(out, args.report, t0)
        return 0

    # ---- the tree still holds the files it started with ------------------
    # ``recording/manifest.json`` is every regular file of the pinned tree;
    # ``deletable`` is the files the rungs up to this one require to be gone,
    # accumulated over the rungs. Everything else has
    # to still be there. What this stops is answering "port this file" with
    # "delete it": the plugin's own modules import ``sublime`` and so are never
    # driven by a case, and no gate finds anything in them, so without this rail
    # removing them costs nothing and the MIN_PY_FILES floor leaves ninety-odd
    # files' worth of room to do it in.
    #
    # A rail and not a scored gate, for the same reason as gates.GUARDS in
    # reverse: the do-nothing predecessor tree passes this trivially -- it has
    # deleted nothing -- so scoring it would hand a tree that did no work a
    # share of the rung's weight. It sets ``blocked`` instead, which is
    # reward 0 and a report that says why, and it runs before the replay so a
    # gutted tree is rejected in a second rather than after the full suite.
    if cfg.get("manifest"):
        with open(os.path.join(here, cfg["manifest"])) as fh:
            manifest = json.load(fh)
        deletable = set(cfg.get("deletable") or ())
        missing = [p for p in manifest["paths"] if p not in deletable
                   and not os.path.exists(os.path.join(gate_tree, p))]
        out["n_missing_files"] = len(missing)
        if missing:
            out["blocked"] = ("%d file(s) the tree started with are gone and "
                              "are not among the %d this step requires deleted:"
                              " %s. Deleting a file is not a port of it."
                              % (len(missing), len(deletable),
                                 ", ".join(sorted(missing)[:8])))
            _emit(out, args.report, t0)
            return 0

    # ---- the tree imports under this rung's interpreter ------------------
    # Run on the pristine copy, on the interpreter that replays the cases and
    # under the same step.json ``env`` (PYTHON_GIL=0 at step 06), in its own
    # process so a hang or a crash cannot take the scorer with it.
    why = importable(sys.executable, args.scratch, cfg.get("env"))
    reap()
    if why:
        out["blocked"] = ("the libraries the plugin loads from lib/ do not "
                          "all import under python %s: %s"
                          % (out["python"], why))
        _emit(out, args.report, t0)
        return 0

    # ---- family 1: recorded behaviour ------------------------------------
    rep = behaviour(sys.executable, args.scratch, recording, flips,
                    cfg.get("jobs", 8), cfg.get("timeout", 120),
                    args.report + ".replay", step_id, cfg.get("env"),
                    deadline=(args.deadline - GATE_RESERVE
                              if args.deadline else None),
                    case_cwd=args.tree)
    reap()
    with _open_recording(recording) as fh:
        n_recorded = len(json.load(fh)["cases"])
    # The denominator is asserted, not assumed. Overlays may *retire* a
    # case, and only by that many.
    n_retired = len(rep.get("retired") or {})
    expected_total = n_recorded - n_retired
    own = rep.get("owned_result") or {}
    inh = rep.get("inherited_result") or {}
    out["behaviour"] = {"passed": rep.get("n_passed", 0),
                        "total": rep.get("n_total", 0),
                        "recorded": n_recorded, "retired": n_retired,
                        "score": rep.get("score", 0.0),
                        "owned": own, "inherited": inh,
                        "owned_ids": rep.get("owned") or [],
                        "failed_sample": sorted(rep.get("failed", {}))[:40]}
    if rep.get("crashed"):
        out["blocked"] = "the replay did not produce a report"
        out["behaviour"]["crashed"] = rep["crashed"]
        _emit(out, args.report, t0)
        return 0
    if rep.get("n_total") != expected_total:
        out["blocked"] = ("replay scored %s cases; the recording holds %d and "
                          "the overlays retire %d, so %d were expected. A mean "
                          "over a denominator that moved is not a score."
                          % (rep.get("n_total"), n_recorded, n_retired,
                             expected_total))
        _emit(out, args.report, t0)
        return 0
    # The same assertion one level down: the two halves have to partition the
    # scored set. If they do not, a case has been counted twice or not at all
    # and both component scores are means over the wrong denominator.
    if own.get("total", 0) + inh.get("total", 0) != expected_total:
        out["blocked"] = ("the owned (%s) and inherited (%s) halves do not "
                          "partition the %d scored cases"
                          % (own.get("total"), inh.get("total"),
                             expected_total))
        _emit(out, args.report, t0)
        return 0
    bi, bn = inh.get("score"), own.get("score")

    # ---- family 2: migrated-ness gates -----------------------------------
    sys.path.insert(0, PKG_PARENT)
    from harness import gates
    inherited, new, guards = gates.checks_for(step_id)
    cache, broken = gates.units(gate_tree)
    out["unparsable"] = [{"file": f, "line": n, "detail": d}
                         for f, n, d in broken][:20]
    out["n_unparsable"] = len(broken)
    r, r_rows = gate_family(gate_tree, inherited, cache)
    n, n_rows = gate_family(gate_tree, new, cache)
    _, guard_rows = gate_family(gate_tree, guards, cache)
    out["gates"] = {"inherited": r_rows, "new": n_rows, "guards": guard_rows,
                    "inherited_score": r, "new_score": n}

    # A file the gates cannot parse is a file no gate can clear, so it fails
    # every family that reads source rather than silently scoring as clean.
    if broken:
        r = 0.0 if r is not None else None
        n = 0.0 if n is not None else None
        out["gates"]["note"] = ("%d files do not parse; every source gate is "
                                "scored 0, because a gate that cannot look at "
                                "a file has not cleared it" % len(broken))

    # ---- the reward -------------------------------------------------------
    # The two families multiply:
    #
    #     reward = T1 x T2 - GUARD_PENALTY x guards tripped, clamped to [0, 1]
    #
    # T1 is family 2, the rung's own bar (``behaviour_new`` + ``new_gates``):
    # the evidence that this step's migration was done. T2 is family 1,
    # preservation (``behaviour_inherited`` + ``inherited_gates``). Each factor
    # is the weighted mean of its own components by the step.json weights,
    # renormalised over that factor, so the relative weights inside a family
    # are unchanged. A step k-1 tree keeps T2 and scores 0 in T1, so it scores
    # 0; a tree that does this rung but breaks what earlier rungs did loses in
    # proportion.
    #
    # A component with no members contributes nothing and its weight is not
    # silently awarded: step.json's weights sum to 1 over the components this
    # step actually has, and anything else is blocked below.
    parts = {"behaviour_inherited": (weights.get("behaviour_inherited", 0.0), bi),
             "inherited_gates": (weights.get("inherited_gates", 0.0), r),
             "behaviour_new": (weights.get("behaviour_new", 0.0), bn),
             "new_gates": (weights.get("new_gates", 0.0), n)}
    used = 0.0
    factors = {}
    for fname, members in FACTORS:
        num, den = 0.0, 0.0
        for name in members:
            w, v = parts[name]
            if v is None:
                if w:
                    out["blocked"] = ("%s carries weight %.2f and has no "
                                      "members" % (name, w))
                continue
            num += w * v
            den += w
        used += den
        factors[fname] = {"weight": round(den, 6),
                          "score": (num / den) if den else None}
    t1 = factors["own_bar"]["score"]
    t2 = factors["preservation"]["score"]
    out["families"] = factors
    out["components"] = dict((k, v[1]) for k, v in parts.items())
    out["weight_used"] = round(used, 6)
    if t1 is None:
        # Without its own bar the product would pay a tree for preservation
        # alone, which is exactly what a do-nothing tree has.
        out["blocked"] = ("this step has no own-bar component, so the product "
                          "has no factor that says the step was taken")
        reward = 0.0
    else:
        # A step with no preservation component is its own bar alone.
        reward = t1 * (t2 if t2 is not None else 1.0)
    out["t1_own_bar"] = t1
    out["t2_preservation"] = t2
    if abs(used - 1.0) > 1e-6:
        out["blocked"] = ("weights over the families present sum to %.4f, not "
                          "1.0; the reward would be scaled by an accident"
                          % used)
        reward = 0.0

    # Guards earn nothing and cost when they fire. See gates.GUARDS: a check
    # that a do-nothing tree passes cannot be allowed to pay a do-nothing tree,
    # but a tree that introduces `u''` while modernising has still regressed.
    tripped = [g["id"] for g in guard_rows if not g["passed"]]
    out["guards_tripped"] = tripped
    out["penalty"] = round(GUARD_PENALTY * len(tripped), 6)
    reward -= out["penalty"]

    out["reward"] = max(0.0, min(1.0, reward))
    _emit(out, args.report, t0)
    return 0


def _open_recording(path):
    if path.endswith(".gz"):
        import gzip
        return gzip.open(path, "rt")
    return open(path)


def _emit(out, report_path, t0):
    out["elapsed_sec"] = round(time.time() - t0, 1)
    with open(report_path, "w") as fh:
        json.dump(out, fh, sort_keys=True, indent=1)
    if out["blocked"]:
        print("BLOCKED %s" % out["blocked"])
    b = out.get("behaviour") or {}
    g = out.get("gates") or {}
    own, inh = b.get("owned") or {}, b.get("inherited") or {}
    # Both families on one line, each as "behaviour + gates", because the two
    # numbers that matter when calibrating are "did the predecessor keep
    # family 1" and "did it score anything in family 2".
    print("step %s on python %s: preserve %s/%s cases + %s gates | own bar "
          "%s/%s cases + %s gates | guards %s | %.1fs"
          % (out["step"], out["python"],
             inh.get("passed"), inh.get("total"), _pct(g.get("inherited_score")),
             own.get("passed"), own.get("total"), _pct(g.get("new_score")),
             ",".join(out.get("guards_tripped") or ()) or "none",
             out["elapsed_sec"]))
    print("reward = T1 own bar %s x T2 preservation %s - guard penalty %s"
          % (_num(out.get("t1_own_bar")), _num(out.get("t2_preservation")),
             _num(out.get("penalty"))))
    # Last line, parsed by test.sh. Printed by this scorer, which runs as root
    # with no candidate process holding its stdout, and read by root, which is
    # the only thing that writes reward.json.
    print("REWARD %.6f" % out["reward"])


def _pct(v):
    return "n/a" if v is None else "%.0f%%" % (100.0 * v)


def _num(v):
    return "n/a" if v is None else "%.6f" % v


if __name__ == "__main__":
    sys.exit(main())
