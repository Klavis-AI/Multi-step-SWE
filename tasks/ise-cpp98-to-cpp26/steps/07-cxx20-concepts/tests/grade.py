#!/usr/bin/env python3
"""Score one step: behaviour equivalence, then the migrated-ness gates.

Written by authoring/render_package.py into every step's tests/; edit
authoring/verifier/grade.py. What differs between steps is step.json, never
this file, so eleven verifiers cannot drift into eleven scoring rules.

Three families, combined as a product of two factors (2026-10-13):

  behaviour        the recorded suite replayed through the interface. Recorded
                   from the untouched baseline; unchanged by every step.
  inherited gates  the migrated-ness gates earlier steps introduced. Present so
                   that a step cannot pay for its own leap by undoing one.
  step gates       the gates this step introduces. The tree the previous step
                   finished passes everything except these, which is what keeps
                   a step from being a re-measurement of its predecessor.

  reward = T1 * T2
    T1  migrated-ness: the fraction of this step's own gates that pass.
    T2  preservation: behaviour and the inherited gates, in the proportion of
        their step.json weights, renormalised to [0, 1].
  A tree that did not do this step's work (T1 = 0) scores 0 however much of
  the behaviour it keeps, and a suppression or a rewritten tree, which fails
  every gate, scores 0 as well.

Everything the candidate produced runs unprivileged; this process writes the
score and does not run candidate code itself.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from isefix import catalog, gates, stage                       # noqa: E402
from isefix.engine import (run_session, SessionError,          # noqa: E402
                           expected_item_ids)

MAX_DIFF = 600
# Where the programs under test are executed from. Root-owned and not writable
# by the user they run as: `isedrv` lives here, and it is the one binary in the
# directory that the verifier owns and the candidate does not. Their working
# directory is somewhere else entirely -- see engine.RUNDIR.
BINDIR = "/tmp/isebin"
# Where the driver is *built*, by an unprivileged compiler run. Root copies the
# result into BINDIR afterwards, so that no step of the grading ever hands the
# unprivileged user write access to a directory it will later execute from.
DRVOUT = "/tmp/isedrvout"
LOGS = "/logs/verifier"


def log(message):
    sys.stderr.write("%s\n" % message)
    sys.stderr.flush()


def write(name, payload):
    with open(os.path.join(LOGS, name), "w") as handle:
        json.dump(payload, handle, indent=1, sort_keys=True)


def diff_note(expected, got):
    if got is None:
        return "item was not produced"
    for index, (left, right) in enumerate(zip(expected, got)):
        if left != right:
            start = max(0, index - 60)
            return ("first difference at byte %d\n  expected: %r\n"
                    "  actual:   %r"
                    % (index, expected[start:index + 60], got[start:index + 60]))
    return ("length differs: expected %d bytes, got %d bytes\n"
            "  expected tail: %r\n  actual tail:   %r"
            % (len(expected), len(got), expected[len(got):][:MAX_DIFF],
               got[len(expected):][:MAX_DIFF]))


# --------------------------------------------------------------------------


def build_driver(step, chosen):
    """Compile the characterisation driver against the candidate's library.

    It is the only part of the suite that is not black box, and it is what
    makes "the public API of ise/main is frozen" a statement a compiler checks
    rather than one a reader believes. A tree that renamed a public type fails
    here, loudly, instead of passing a suite that never called it.
    """
    env = dict(os.environ)
    env.update({
        "CXX": "clang++",
        "STD": step["driver_std"],
        "OPT": step.get("driver_opt", "O0"),
        "LIB": chosen["libise.a"],
        "OBJ": "/tmp/isework/drvobj",
    })
    shutil.rmtree(DRVOUT, ignore_errors=True)
    started = time.time()
    proc = subprocess.run(
        ["setpriv", "--reuid", "65534", "--regid", "65534", "--clear-groups",
         "sh", "/tests/driver/build.sh", "/app", DRVOUT],
        env=env, capture_output=True, text=True, timeout=900)
    name = "step%s-driver-build.log" % step["step"]
    with open(os.path.join(LOGS, name), "w") as handle:
        handle.write("std=%s opt=%s lib=%s\nrc=%d in %.1fs\n--- stdout ---\n%s"
                     "\n--- stderr ---\n%s"
                     % (env["STD"], env["OPT"], env["LIB"], proc.returncode,
                        time.time() - started, proc.stdout, proc.stderr))
    if proc.returncode != 0:
        return False
    built = os.path.join(DRVOUT, "isedrv")
    if not os.path.isfile(built):
        with open(os.path.join(LOGS, name), "a") as handle:
            handle.write("\nbuild.sh reported success but wrote no isedrv\n")
        return False
    # Copied by root into the root-owned bindir, and never built there: the
    # compiler ran as nobody, and a directory nobody can write is a directory
    # a program the candidate wrote cannot replace the driver in.
    target = os.path.join(BINDIR, "isedrv")
    shutil.copyfile(built, target)
    os.chmod(target, 0o755)
    return True


def open_for_reading(root):
    """Make `root` readable, and its directories traversable, by any user.

    The driver is compiled as nobody against the candidate's own headers, so
    `-I/app` has to be a directory nobody can look inside; the compiler's
    complaint when it is not is `cannot open file '/app/string'`, which reads
    like a missing standard library rather than a permission. What mode the
    artifact arrives with is not something this task gets to specify, so it is
    set here instead of hoped for.

    Read and traverse only: no write bit is added anywhere, so the tree the
    gates read stays one the programs under test cannot edit.
    """
    changed = 0
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d != ".git"]
        for path in [base] + [os.path.join(base, f) for f in files]:
            if os.path.islink(path):
                continue
            try:
                mode = os.stat(path).st_mode & 0o777
            except OSError:
                continue
            want = mode | 0o044
            if os.path.isdir(path) or mode & 0o100:
                want |= 0o011
            if want != mode:
                try:
                    os.chmod(path, want)
                    changed += 1
                except OSError:
                    pass
    return changed


def run_behaviour(expected, have_driver):
    index = {}
    for _session, item in catalog.all_steps():
        index[item["id"]] = (item.get("stratum", "core"), bool(item.get("adv")))

    got = {}
    timings = {}
    for session in catalog.sessions():
        if session.get("kind") == "driver" and not have_driver:
            for item in expected_item_ids(session):
                got[item] = "<driver-did-not-build>"
            continue
        started = time.time()
        try:
            got.update(run_session(session, BINDIR))
        except SessionError as exc:
            for item in expected_item_ids(session):
                got[item] = "<session-failed: %s>" % exc
        except Exception as exc:                              # noqa: BLE001
            for item in expected_item_ids(session):
                got[item] = "<session-error: %r>" % (exc,)
        timings[session["id"]] = round(time.time() - started, 1)
        log("  %-24s %6.1fs" % (session["id"], timings[session["id"]]))

    results = []
    for name in sorted(expected):
        want, have = expected[name], got.get(name)
        ok = have == want
        stratum, adv = index.get(name, ("core", False))
        entry = {"name": name, "ok": ok, "stratum": stratum,
                 "adversarial": adv}
        if not ok:
            entry["detail"] = diff_note(want, have)[:MAX_DIFF]
        results.append(entry)
    return results, timings


def fraction(items):
    if not items:
        return None
    return sum(1 for item in items if item["ok"]) / float(len(items))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-rc", type=int, required=True)
    parser.add_argument("--step", default="/tests/step.json")
    parser.add_argument("--fixtures", default="/tests/fixtures/expected.json")
    args = parser.parse_args()

    os.makedirs(LOGS, exist_ok=True)
    with open(args.step) as handle:
        step = json.load(handle)
    with open(args.fixtures) as handle:
        expected = json.load(handle)["items"]

    detail = {"step": step["step"], "name": step["name"],
              "build_rc": args.build_rc, "weights": step["weights"]}

    # Everything the suite starts runs as nobody; engine reads this.
    #
    # setdefault rather than an assignment, because helpers.sh already holds
    # the one definition of what "as the candidate" means and test.sh exports
    # it before this runs. An assignment here silently replaced it with a
    # shorter drop that kept the variables naming root's scratch directories,
    # so the two halves of the verifier disagreed about the very thing the
    # comment in helpers.sh says to keep in step. The literal below is the
    # fallback for a grade.py driven by hand, with no test.sh around it.
    os.environ.setdefault(
        "ISEFIX_LAUNCH_PREFIX",
        "setpriv --reuid 65534 --regid 65534 --clear-groups")
    detail["app_opened"] = open_for_reading("/app")

    chosen, staged = {}, []
    stage_error = None
    if args.build_rc == 0:
        chosen, staged = stage.stage("/app", BINDIR)
        # Readable and executable by the user the suite runs as, writable
        # by nobody but root. The programs write to engine.RUNDIR instead.
        os.chmod(BINDIR, 0o755)
    else:
        stage_error = "the build did not succeed (rc=%d)" % args.build_rc
    detail["staged"] = staged
    detail["stage_error"] = stage_error

    # Whatever was built gets run. A missing artefact costs the items that
    # needed it and nothing else; that all of them must exist is the
    # `artifacts` gate's business, not this family's. The driver is the one
    # exception, because it is linked against the library rather than found:
    # no libise.a, no driver, and the driver's items fail as a block.
    have_driver = False
    if stage.LIBRARY in chosen:
        have_driver = build_driver(step, chosen)
    detail["driver_built"] = have_driver

    log("=== behaviour")
    results, timings = run_behaviour(expected, have_driver)
    behaviour = fraction(results)
    write("step%s-behavior.json" % step["step"],
          {"results": results, "timings": timings,
           "passed": sum(1 for r in results if r["ok"]),
           "total": len(results)})
    log("behaviour: %d/%d" % (sum(1 for r in results if r["ok"]), len(results)))

    log("=== gates")
    build_dir = os.path.join("/app", step.get("build_dir", "build"))
    gate_results = gates.run_gates(step, "/app", build_dir,
                                   jobs=step.get("jobs", 7),
                                   build_rc=args.build_rc)
    write("step%s-gates.json" % step["step"], gate_results)
    inherited = [g for g in gate_results if g["step"] != step["step"]]
    current = [g for g in gate_results if g["step"] == step["step"]]
    for entry in gate_results:
        log("  %-8s %-34s %s" % (entry["step"], entry["name"],
                                 "ok" if entry["ok"] else "FAILED"))

    weights = dict(step["weights"])
    parts = {"behaviour": (behaviour, weights["behaviour"]),
             "inherited_gates": (fraction(inherited),
                                 weights["inherited_gates"]),
             "step_gates": (fraction(current), weights["step_gates"])}
    # T1 = this step's own gates; T2 = behaviour and inherited gates, weighted
    # in proportion to their step.json weights. A family with no members --
    # step 1 inherits no gates -- drops out of its factor, and the factor's
    # remaining weights are renormalised so that each factor stays in [0, 1].
    t1 = parts["step_gates"][0]
    if t1 is None:
        raise RuntimeError("step.json declares no gates of its own")
    live = {k: w for k, (v, w) in parts.items()
            if k != "step_gates" and v is not None}
    scale = sum(live.values())
    if scale <= 0:
        raise RuntimeError("no preservation family with a positive weight")
    t2 = sum(parts[k][0] * w for k, w in live.items()) / scale
    reward = t1 * t2

    detail["formula"] = ("step_gates * (behaviour and inherited_gates, "
                         "weighted by step.json and renormalised)")
    detail["t1_migrated"] = round(t1, 6)
    detail["t2_preserved"] = round(t2, 6)
    detail["families"] = {k: {"fraction": v, "weight": w,
                              "count": (len(results) if k == "behaviour" else
                                        len(inherited) if k == "inherited_gates"
                                        else len(current))}
                          for k, (v, w) in parts.items()}
    detail["reward"] = round(reward, 6)

    # Every check, filed under the step that introduced it, as `results<NN>`.
    #
    # This shape is what lets a reader tell a package defect from a run in
    # which this step compiled the PREVIOUS step's tree -- the empty-/app
    # transient's second face, which does not score 0 and does not lose
    # coverage, but hands the next step a stale /app and returns a plausible
    # partial score. The signature of that is precise: the step's OWN checks
    # collapse while every INHERITED check passes untouched. It is invisible
    # unless the checks are separable by the step that introduced them, which
    # is why they are written out this way rather than as one flat list.
    #
    # `results00` is the behaviour contract. It belongs to no step: it is the
    # recording of the untouched baseline that all eleven steps inherit, and
    # step numbering starts at 01, so 00 is the honest label for it.
    detail["results00"] = [{"name": r["name"], "ok": r["ok"]} for r in results]
    for entry in gate_results:
        detail.setdefault("results%s" % entry["step"], []).append(
            {"name": entry["name"], "ok": entry["ok"]})

    write("step%s-detail.json" % step["step"], detail)

    log("reward %.6f = T1 %.6f * T2 %.6f  (behaviour %s, inherited %s, "
        "step %s)" % (reward, t1, t2, behaviour, parts["inherited_gates"][0],
                      parts["step_gates"][0]))
    print("%.6f" % reward)
    return 0


if __name__ == "__main__":
    sys.exit(main())
