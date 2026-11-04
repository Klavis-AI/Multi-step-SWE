#!/usr/bin/env python3
"""Run exactly one scenario against one tree and write its observations.

Invoked as a subprocess by runner.py so that a scenario that hangs, crashes or
leaks a child cannot take the whole run with it.
"""

import argparse
import json
import os
import sys
import traceback

# Before the first harness import, for the reason set out in runner.py: a
# `.pyc` beside a source file is a second copy of it that nothing can read.
sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "lib"))
sys.path.insert(0, os.path.join(HERE, "corpus"))

import ctx as ctxmod  # noqa: E402
import norm  # noqa: E402
import registry  # noqa: E402
import srv  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", required=True)
    ap.add_argument("--tree", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--mode", choices=("record", "replay"), required=True)
    ap.add_argument("--privileged", action="store_true",
                    help="run candidate binaries as the current user")
    args = ap.parse_args()

    spec = registry.BY_NAME[args.scenario]

    # Register before importing the scenario so no module-level text escapes
    # unmasked: the workspace root and the tree root are the runner's choice,
    # and the candidate tree does not live where the baseline did.
    work = os.path.join(args.work, args.scenario)
    norm.register_path(work, "<WORK>")
    norm.register_path(args.tree, "<TREE>")

    # Before the scenario is allowed to measure anything: the block it is about
    # to bind has to be its own. Failing here is loud and leaves nothing
    # recorded, which is the point -- a stray server on one of these ports would
    # be measured instead of the tree, and the number it produced would look
    # exactly like a real one.
    busy = srv.busy_ports(spec["base_port"], spec["ports"])
    if busy:
        raise RuntimeError(
            "port block %d..%d is not free (%s already listening); "
            "a stray server would be measured instead of %s"
            % (spec["base_port"], spec["base_port"] + spec["ports"] - 1,
               ", ".join(str(p) for p in busy), args.tree))

    module = __import__(args.scenario)
    tree = srv.Tree(args.tree, unprivileged=not args.privileged)
    ws = ctxmod.srv.Workspace(work)
    c = ctxmod.Ctx(args.scenario, tree, ws, spec["base_port"], args.mode)

    status = "ok"
    error = None
    try:
        module.run(c)
    except BaseException:  # noqa: BLE001 - a scenario crash is a result, not a stop
        status = "error"
        error = traceback.format_exc()
    finally:
        try:
            c.shutdown_all()
        except Exception:
            pass

    payload = {
        "scenario": args.scenario,
        "family": spec["family"],
        "status": status,
        "error": error,
        "notes": c.notes,
        "observations": c.observations,
    }
    with open(args.out, "w") as f:
        json.dump(payload, f, sort_keys=True, indent=1)
    if status == "ok":
        ws.cleanup()
    else:
        # A crashed scenario's server logs are the only record of why; deleting
        # the workspace would throw them away. Report the failure through the
        # exit status too, so the runner cannot print `ok` over it.
        sys.stderr.write(error)
        sys.stderr.write("workspace kept for diagnosis: %s\n" % work)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
