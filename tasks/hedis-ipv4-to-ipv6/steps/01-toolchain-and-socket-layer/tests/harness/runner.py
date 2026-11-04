#!/usr/bin/env python3
"""Record or replay the scored corpus.

record:  runner.py record --tree <baseline> --expect <dir> [--twice]
replay:  runner.py replay --tree <candidate> --expect <dir> --step <k> \
                          --report <file.json>

Recording writes one JSON file per scenario. Replay runs the same scenario code
against the candidate tree and compares observation by observation.

Every observation in a composed expectation file carries a `role`, assigned
when the step's expectations were composed from the recordings:

* `progress` -- the observation is in a scenario recorded from this step's own
  golden tree (registry `flip_from` equal to this step: the scenarios written
  to measure it), and its value differs both from the untouched baseline's
  recording (for `g01_build`, the pin's: registry `baseline_dir`) and from the
  previous step's golden tree's; an observation absent from either recording
  counts as differing;
* `regression` -- everything else, which is what the step has to leave alone.
  That includes the inherited corpus's must-flips (`flips.py`): they are scored
  at their new values, but they do not pay.

Replay totals the two separately, and the verifier combines them. Splitting them
is what makes the step's score mean the step: a cumulative suite is mostly
regression by volume, so a single weighted fraction over all of it reads almost
the same for a tree that did the step and a tree that did the one before it.
"""

import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
import tempfile
import time

# No harness process writes bytecode. A `.pyc` is compiled beside the source it
# came from and is preferred to that source afterwards, even once the source has
# changed or gone, so one left in a composed `tests/` tree is a second copy of
# the corpus that nothing reading that tree can see. This stops them being
# written in the first place, and it has to be set before the first harness
# import, because by the time that import returns the file it would have
# cached is already written.
sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "lib"))
import category  # noqa: E402
import registry  # noqa: E402

RUN_ONE = os.path.join(HERE, "run_one.py")
DEFAULT_TIMEOUT = 900


def _run_scenario(spec, tree, work, outdir, mode, timeout, privileged):
    out = os.path.join(outdir, spec["name"] + ".json")
    argv = [sys.executable, RUN_ONE, "--scenario", spec["name"], "--tree", tree,
            "--work", work, "--out", out, "--mode", mode]
    if privileged:
        argv.append("--privileged")
    t0 = time.time()
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout)
        rc, log = p.returncode, p.stdout
    except subprocess.TimeoutExpired as e:
        rc, log = "timeout", (e.stdout or b"")
    elapsed = time.time() - t0
    if not os.path.exists(out):
        with open(out, "w") as f:
            json.dump({"scenario": spec["name"], "family": spec["family"],
                       "status": "crashed", "error": "rc=%s" % (rc,),
                       "notes": [], "observations": {}}, f)
    return spec["name"], rc, elapsed, log


def _parallel(specs, tree, work, outdir, mode, jobs, timeout, privileged):
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = {ex.submit(_run_scenario, s, tree, work, outdir, mode, timeout,
                          privileged): s for s in specs}
        for fut in concurrent.futures.as_completed(futs):
            name, rc, elapsed, log = fut.result()
            results.append((name, rc, elapsed))
            tag = "ok" if rc == 0 else "FAIL(%s)" % rc
            sys.stderr.write("[%-22s] %-10s %6.1fs\n" % (name, tag, elapsed))
            if rc != 0 and log:
                sys.stderr.write(log.decode("utf-8", "replace")[-4000:] + "\n")
    return results


def _make_work(path):
    """Create the work root so the demoted server user can reach into it.

    `tempfile.mkdtemp` makes a 0700 directory owned by the caller. Every server
    the corpus starts runs as uid 65534, so a 0700 ancestor means the server
    cannot open its own config file -- and it dies before it has a log to say
    so, which reads exactly like "never became ready".
    """
    work = path or tempfile.mkdtemp(prefix="rec-")
    os.makedirs(work, exist_ok=True)
    os.chmod(work, 0o755)
    return work


def cmd_record(args):
    os.makedirs(args.expect, exist_ok=True)
    specs = registry.SCENARIOS
    if args.only:
        wanted = set(args.only.split(","))
        specs = [s for s in specs if s["name"] in wanted]
    work = _make_work(args.work)

    first = os.path.join(work, "pass1")
    os.makedirs(first, exist_ok=True)
    _parallel(specs, args.tree, os.path.join(work, "w1"), first, "record",
              args.jobs, args.timeout, args.privileged)

    if args.twice:
        second = os.path.join(work, "pass2")
        os.makedirs(second, exist_ok=True)
        _parallel(specs, args.tree, os.path.join(work, "w2"), second, "record",
                  args.jobs, args.timeout, args.privileged)
        unstable = {}
        for s in specs:
            f1 = json.load(open(os.path.join(first, s["name"] + ".json")))
            f2 = json.load(open(os.path.join(second, s["name"] + ".json")))
            o1, o2 = f1["observations"], f2["observations"]
            bad = sorted(k for k in set(o1) | set(o2)
                         if o1.get(k, {}).get("v") != o2.get(k, {}).get("v"))
            if bad:
                unstable[s["name"]] = bad
        with open(os.path.join(args.expect, "_unstable.json"), "w") as f:
            json.dump(unstable, f, indent=1, sort_keys=True)
        total_bad = sum(len(v) for v in unstable.values())
        sys.stderr.write("unstable observations across two passes: %d\n" % total_bad)
        for k, v in unstable.items():
            sys.stderr.write("  %s: %s\n" % (k, ", ".join(v[:12])))

    # A scenario that crashed recorded whatever it got to before the crash.
    # Publishing that as an expectation would bake a partial suite in and call
    # it green, so a failed recording replaces nothing and fails the command.
    broken = []
    for s in specs:
        src = os.path.join(first, s["name"] + ".json")
        if json.load(open(src))["status"] != "ok":
            broken.append(s["name"])
            continue
        dst = os.path.join(args.expect, s["name"] + ".json")
        with open(src) as fi, open(dst, "w") as fo:
            fo.write(fi.read())
    _summarise(args.expect, specs)
    if broken:
        sys.stderr.write("\nNOT RECORDED (scenario errored): %s\n"
                         % ", ".join(broken))
        return 1
    return 0


def _summarise(expect, specs):
    tot = adv = 0
    per = {}
    for s in specs:
        p = os.path.join(expect, s["name"] + ".json")
        if not os.path.exists(p):
            continue
        d = json.load(open(p))
        obs = d["observations"]
        n = len(obs)
        na = sum(1 for o in obs.values() if o.get("adv"))
        per[s["name"]] = (n, na, d["status"])
        tot += n
        adv += na
    sys.stderr.write("\n%-24s %7s %7s  %s\n" % ("scenario", "inputs", "adv", "status"))
    for k in sorted(per):
        n, na, st = per[k]
        sys.stderr.write("%-24s %7d %7d  %s\n" % (k, n, na, st))
    sys.stderr.write("%-24s %7d %7d  (%.1f%% adversarial)\n"
                     % ("TOTAL", tot, adv, 100.0 * adv / tot if tot else 0.0))


def cmd_replay(args):
    specs = registry.for_step(args.step)
    if args.only:
        wanted = set(args.only.split(","))
        specs = [s for s in specs if s["name"] in wanted]
    work = _make_work(args.work)
    outdir = os.path.join(work, "out")
    os.makedirs(outdir, exist_ok=True)
    timings = _parallel(specs, args.tree, os.path.join(work, "w"), outdir,
                        "replay", args.jobs, args.timeout, args.privileged)

    report = {"step": args.step, "scenarios": {}, "timings":
              {n: round(t, 2) for n, _, t in timings}}
    fam_tot = {"behaviour": 0.0, "gate": 0.0}
    fam_got = {"behaviour": 0.0, "gate": 0.0}
    # Per family and per role: {family: {role: [total_weight, matched_weight]}}.
    split = {f: {r: [0.0, 0.0] for r in ("progress", "regression")}
             for f in fam_tot}
    # A scenario this step scores but has no expectation file for would
    # otherwise contribute nothing to either the numerator or the denominator,
    # so a missing recording would read as a smaller suite that still scored
    # 1.0. It is a packaging error and it stops the replay.
    missing = []
    for s in specs:
        p = os.path.join(args.expect, s["name"] + ".json")
        if not os.path.exists(p):
            missing.append(s["name"] + " (no file)")
        elif not json.load(open(p))["observations"]:
            missing.append(s["name"] + " (no observations)")
    if missing:
        sys.stderr.write("no expectations for step %d scenario(s): %s\n"
                         % (args.step, ", ".join(missing)))
        return 2

    for s in specs:
        name = s["name"]
        exp_path = os.path.join(args.expect, name + ".json")
        got_path = os.path.join(outdir, name + ".json")
        exp = json.load(open(exp_path))
        got = json.load(open(got_path))
        eobs, gobs = exp["observations"], got["observations"]
        # A cell `category.py` takes out of the comparison counts nowhere:
        # not in the total, not as a miss.
        dropped = sorted(k for k in eobs
                         if category.rule(name, k) is category.DROP)
        eobs = dict((k, o) for k, o in eobs.items() if k not in dropped)
        total = sum(o["w"] for o in eobs.values())
        ok = 0.0
        misses = []
        prog_missed = []
        fam = s["family"]
        for k, o in sorted(eobs.items()):
            g = gobs.get(k)
            # Most cells are compared exactly. The few `category.py` lists are
            # compared through the reading the instruction names, applied to
            # the recorded value and the candidate's alike.
            reading = category.rule(name, k)
            if g is None:
                hit = False
            elif reading is None:
                hit = g["v"] == o["v"]
            else:
                hit = category.same(reading, g["v"], o["v"])
            # An expectation with no role was never classified when the step
            # was composed. Scoring it as progress would hand out this
            # step's credit for something no one classified, so it counts as
            # regression -- the conservative side, since regression only ever
            # scales the score down.
            role = o.get("role", "regression")
            if role not in split[fam]:
                role = "regression"
            split[fam][role][0] += o["w"]
            if hit:
                ok += o["w"]
                split[fam][role][1] += o["w"]
            else:
                misses.append(k)
                if role == "progress":
                    prog_missed.append(k)
        fam_tot[fam] += total
        fam_got[fam] += ok
        report["scenarios"][name] = {
            "family": fam,
            "status": got["status"],
            "inputs": len(eobs),
            "matched": len(eobs) - len(misses),
            "weight_total": total,
            "weight_ok": ok,
            "misses": misses[:200],
            "miss_count": len(misses),
            "progress_missed": prog_missed[:200],
            "progress_miss_count": len(prog_missed),
            "category_dropped": dropped,
            "error": got.get("error"),
        }

    def _frac(pair):
        return {"weight_total": pair[0], "weight_ok": pair[1],
                "fraction": (pair[1] / pair[0]) if pair[0] else None}

    report["family_totals"] = {
        f: dict(weight_total=fam_tot[f], weight_ok=fam_got[f],
                fraction=(fam_got[f] / fam_tot[f]) if fam_tot[f] else None,
                progress=_frac(split[f]["progress"]),
                regression=_frac(split[f]["regression"]))
        for f in fam_tot}
    reg = [sum(split[f]["regression"][i] for f in split) for i in (0, 1)]
    report["regression_total"] = _frac(reg)
    with open(args.report, "w") as f:
        json.dump(report, f, indent=1, sort_keys=True)
    for f in ("behaviour", "gate"):
        if fam_tot[f]:
            p = report["family_totals"][f]["progress"]
            sys.stderr.write("%s: overall %.4f, progress %s (%g/%g weight)\n"
                             % (f, fam_got[f] / fam_tot[f],
                                "n/a" if p["fraction"] is None
                                else "%.4f" % p["fraction"],
                                p["weight_ok"], p["weight_total"]))
    sys.stderr.write("regression: %s (%g/%g weight)\n"
                     % ("n/a" if report["regression_total"]["fraction"] is None
                        else "%.4f" % report["regression_total"]["fraction"],
                        reg[1], reg[0]))
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("record")
    r.add_argument("--tree", required=True)
    r.add_argument("--expect", required=True)
    r.add_argument("--work")
    r.add_argument("--jobs", type=int, default=6)
    r.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    r.add_argument("--twice", action="store_true")
    r.add_argument("--only")
    r.add_argument("--privileged", action="store_true")
    r.set_defaults(func=cmd_record)

    p = sub.add_parser("replay")
    p.add_argument("--tree", required=True)
    p.add_argument("--expect", required=True)
    p.add_argument("--step", type=int, required=True)
    p.add_argument("--report", required=True)
    p.add_argument("--work")
    p.add_argument("--jobs", type=int, default=6)
    p.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    p.add_argument("--only")
    p.add_argument("--privileged", action="store_true")
    p.set_defaults(func=cmd_replay)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
