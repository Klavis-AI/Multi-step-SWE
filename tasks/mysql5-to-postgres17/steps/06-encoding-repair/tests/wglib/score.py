"""The per-step orchestrator: bring the snapshot up, run this step's gates,
write the reward.

Every step's verifier is the same program. What differs between steps is a
`plan`, a small module shipped in that step's `tests/` that names the families
to run and how much each one weighs. Keeping the engine here and the plan
there means authoring step 7 cannot disturb the package for step 3 -- an
already-scored step's `tests/` directory does not change when a later step is
written.

The contract with Harbor is narrow and unforgiving, so this module's whole job
is to honour it no matter what happens inside:

  * exit 0, always. A non-zero exit is a verifier crash, and Harbor then skips
    every remaining step and averages only the ones that reported. Trap 3: a
    crashed step vanishes from the mean rather than scoring zero in it, which
    quietly raises the trial reward.
  * print exactly one line, the reward, for `report` in helpers.sh to clamp
    and write to reward.json.
  * write `/logs/verifier/report.json` first, and write it even when the run
    fell over, because it is the only evidence that leaves the container.

A family that throws scores zero over its declared denominator rather than
disappearing (`gate.crashed`), and it does not take the other families with
it. A family that is genuinely not part of this step is simply absent from the
plan -- that is a design decision made before the tree is looked at, which is
the only kind `gate.combine` will honour.

Plan modules declare:

    STEP      "04"                     the step number, for the report
    TITLE     "switch to PostgreSQL"   one line, for a reader
    WANT_HTTP True                     start Apache and warm the front page
    WEIGHTS   {"behavior": 3.0, ...}   per family; default 1.0
    FAMILIES  [("behavior", fn), ...]  fn(ctx) -> gate.Checks
    GUARDS    ("behavior:requests",)   families that cap rather than pay

`GUARDS` names the families that were already true before the step began --
the part of the corpus this step does not touch, the schema it must not
reshape, the shortcuts it must not take. They multiply the earned families'
mean instead of averaging into it, so a step cannot pay a candidate for
arriving; `gate.combine` carries the reasoning.

Usage (from test.sh, after harden_reward_dir):

    report "$(python3 -m wglib.score /tests/plan.py)"
"""

import json
import os
import sys
import time
import traceback

from . import deploy, gate

LOGDIR = "/logs/verifier"
REPORT = "report.json"

# The report carries every failure a family found, not the first sixty: it is
# read by a human deciding whether the step is well-formed, and a truncated
# list has hidden a systematic fault behind a handful of examples before.
DETAIL_LIMIT = 10000


# ---------------------------------------------------------------- context ---

class Context(object):
    """What the families are given: a deployment and the connections to it.

    Connections are opened on demand and cached. Opening one is allowed to
    fail -- that is the finding, and the family that asked turns it into
    failed checks.

    `deploy.connect_*` returns (connection, note), where the note says which of
    several credentials got in. The note is evidence a reader of the report
    wants, so it is kept in `notes`; the families get the bare connection.
    """

    def __init__(self, dep, roots=("/app",), tools=None):
        self.dep = dep
        self.roots = list(roots)
        self.tools = tools or os.path.dirname(os.path.abspath(__file__))
        self.shared = {}
        self.notes = {}
        self._pg = {}
        self._my = {}

    def postgres(self, database=None):
        if database not in self._pg:
            conn, note = deploy.connect_postgres(database)
            self._pg[database] = conn
            self.notes["postgres:%s" % (database or "*")] = note
        return self._pg[database]

    def mysql(self, database=None):
        if database not in self._my:
            conn, note = deploy.connect_mysql(database)
            self._my[database] = conn
            self.notes["mysql:%s" % (database or "*")] = note
        return self._my[database]

    def close(self):
        for pool in (self._pg, self._my):
            for conn in pool.values():
                try:
                    conn.close()
                except Exception:
                    pass
            pool.clear()


# ------------------------------------------------------------------- plan ---

def load_plan(path):
    """Import a plan module from a file path, without it being on sys.path."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("wg_plan", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load a plan from %r" % path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    for attr in ("STEP", "FAMILIES"):
        if not hasattr(mod, attr):
            raise RuntimeError("plan %r declares no %s" % (path, attr))
    return mod


# ------------------------------------------------------------------- work ---

def run_families(plan, ctx):
    """Run each family in its own guard. Returns [Checks] in plan order."""
    parts = []
    for name, fn in plan.FAMILIES:
        started = time.time()
        try:
            checks = fn(ctx)
            if checks is None:
                raise RuntimeError(
                    "family %r returned nothing; a family that does not apply"
                    " to this step belongs out of the plan, not out of the"
                    " denominator" % name)
        except Exception:
            checks = gate.crashed(name, "%s: %s"
                                  % (name, traceback.format_exc(limit=8)))
        checks.note("%.1fs" % (time.time() - started))
        parts.append(checks)
    return parts


def run_plan(plan, logdir=LOGDIR):
    """(reward, report). Raises nothing the caller has to handle."""
    started = time.time()
    report = {
        "step": getattr(plan, "STEP", "?"),
        "title": getattr(plan, "TITLE", ""),
        "reward": 0.0,
        "families": {},
        "deployment": {},
        "connections": {},
        "error": None,
        "timing": {},
    }
    dep = None
    ctx = None
    try:
        t0 = time.time()
        dep = deploy.bring_up(want_http=getattr(plan, "WANT_HTTP", True))
        report["deployment"] = dep.as_dict()
        report["timing"]["bring_up"] = round(time.time() - t0, 1)
        sys.stderr.write("deployment: %s\n" % dep.summary())

        ctx = Context(dep, roots=getattr(plan, "ROOTS", ("/app",)))
        t0 = time.time()
        parts = run_families(plan, ctx)
        report["timing"]["families"] = round(time.time() - t0, 1)

        reward, detail = gate.combine(parts, getattr(plan, "WEIGHTS", None),
                                      getattr(plan, "GUARDS", ()))
        report["reward"] = reward
        report["families"] = detail
        report["connections"] = dict(ctx.notes)
        guards = set(getattr(plan, "GUARDS", ()))
        for p in parts:
            sys.stderr.write("  %-26s %5d/%-5d  %.4f  %-6s%s\n"
                             % (p.family, p.passed, p.total, p.score,
                                "guard" if p.family in guards else gate.factor_of(p.family),
                                "  ERROR" if p.error else ""))
    except Exception:
        # A failure out here -- the deployment would not come up, the plan is
        # malformed -- is a zero, not a crash. The trace is the evidence.
        report["error"] = traceback.format_exc()
        report["reward"] = 0.0
        sys.stderr.write(report["error"])
    finally:
        if ctx is not None:
            ctx.close()
        try:
            deploy.tear_down()
        except Exception:
            sys.stderr.write("tear-down failed:\n%s" % traceback.format_exc())
        report["timing"]["total"] = round(time.time() - started, 1)
        write_report(report, logdir)
    return report["reward"], report


def write_report(report, logdir=LOGDIR):
    """Evidence first. Never raises: a report that cannot be written must not
    take the reward with it.

    Written twice, under two names. `report.json` is the one a reader looks
    for; `stepNN-report.json` carries the step number in the *file name*, which
    is what makes trap 2 detectable from outside the container. Harbor reads
    back only /logs/verifier, and when it builds no per-step verifier image
    every step from some point on runs an earlier step's tests -- scoring a
    reference that implements every milestone a clean 1.000 with full coverage
    and n_trials 1. The only trace left is whose verifier wrote the directory,
    so each one signs its work:

        ls jobs/<job>/*/*/steps/NN-*/verifier | grep -oE 'step[0-9]+' | sort -u

    must print exactly `stepNN`. Without the stamped name that check has
    nothing to read and silently passes.
    """
    try:
        if not os.path.isdir(logdir):
            os.makedirs(logdir)
        blob = gate.dumps(report).encode("utf-8")
        step = str(report.get("step") or "?")
        names = [REPORT]
        if step.isdigit():
            names.append("step%s-report.json" % step)
        for name in names:
            path = os.path.join(logdir, name)
            with open(path, "wb") as fh:
                fh.write(blob)
            # helpers.sh reopens the directory on the way out; these files are
            # read from the host by whoever collected the job, so they are
            # readable too.
            os.chmod(path, 0o644)
    except Exception:
        sys.stderr.write("could not write the report:\n%s"
                         % traceback.format_exc())


def main(argv):
    plan_path = argv[1] if len(argv) > 1 else "/tests/plan.py"
    logdir = argv[2] if len(argv) > 2 else LOGDIR
    try:
        plan = load_plan(plan_path)
        reward, _ = run_plan(plan, logdir)
    except Exception:
        # Loading the plan is the one thing that happens before run_plan can
        # write anything, so it needs its own report -- and its own tear-down,
        # because a plan module is ordinary Python and importing one can leave
        # a server behind before it throws.
        trace = traceback.format_exc()
        sys.stderr.write(trace)
        try:
            deploy.tear_down()
        except Exception:
            sys.stderr.write("tear-down failed:\n%s" % traceback.format_exc())
        write_report({"step": "?", "reward": 0.0, "error": trace}, logdir)
        reward = 0.0
    sys.stdout.write("%.6f\n" % reward)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
