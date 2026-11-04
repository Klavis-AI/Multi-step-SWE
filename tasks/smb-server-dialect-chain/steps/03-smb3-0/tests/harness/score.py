#!/usr/bin/env python3
"""Generic scorer: reads a step configuration and produces this step's reward.

A step config is a JSON document:

{
  "scenarios": [
    {"name": "nt1", "port": 14451,
     "globals": {"server min protocol": "NT1", ...},
     "shares": {"tmp": {"read only": "no", "guest ok": "no"}, ...},
     "groups": [
        {"kind": "torture",    "name": "torture-nt1", "weight": 40,
         "share": "tmp", "min": "NT1", "max": "NT1", "list": "torture_nt1.json"},
        {"kind": "smbclient",  "name": "smbclient-nt1", "weight": 25,
         "min": "NT1", "max": "NT1", "expected": "smbclient_nt1.json"},
        {"kind": "probe",      "name": "wire-nt1", "weight": 35,
         "module": "cases_smb1", "expected": "wire_nt1.json",
         "min": "NT1", "max": "NT1"}
     ]}
  ]
}
"""
import importlib
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probebase  # noqa: E402
import runner  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


_LOADED = {}


def load_expected(name):
    """The answers, from memory once seal_corpus() has run."""
    if name in _LOADED:
        return _LOADED[name]
    with open(os.path.join(HERE, "expected", name)) as fh:
        doc = json.load(fh)
    _LOADED[name] = doc
    return doc


def cases_remaining(cfg, this_scn, this_group):
    """(this group's case count, cases still to score including this group).

    Counted from the shipped expectation sets, so the share a group gets is
    proportional to the work it actually has rather than to a guessed constant.
    """
    seq = [(s["name"], g) for s in cfg["scenarios"] for g in s["groups"]]
    sizes = [len(expected_cases(g)) for _, g in seq]
    try:
        i = next(k for k, (sn, g) in enumerate(seq)
                 if sn == this_scn["name"] and g["name"] == this_group["name"])
    except StopIteration:
        return 0, 0
    return sizes[i], sum(sizes[i:])


def seal_corpus(cfg):
    """Read every answer into memory, then take the answers off the filesystem.

    The service under test runs as a child of this scorer, in this container,
    with this filesystem. Until this runs, `/tests/harness/expected/*.json` is
    readable by it -- the recorded Samba answer to every case it is about to be
    asked, and the probes name their files after their cases
    (`probe-lv2e.bin`, `probe-64.bin`), so the case identity is on the wire as
    well. Together that is a complete index from "which check is this" to "what
    the answer is", and between 52% and 76% of a step's reward is carried by
    groups scored that way. A service that looked its answers up rather than
    computing them would be indistinguishable from one that implemented the
    protocol, and no provenance check can tell those apart: `/proc/<pid>/exe`
    answers "is this this image's node", not "did it compute this".

    Removing the files closes that: after this function returns there is nothing
    left to read, whoever is reading. Where the tests are mounted read-only --
    a local-debugging setup, not how the step ships -- the files cannot be
    removed, and the report says so rather than implying a protection that is
    not there.
    """
    names, modules = set(), set()
    for scn_cfg in cfg["scenarios"]:
        for g in scn_cfg["groups"]:
            names.add(g["list"] if g["kind"] == "torture" else g["expected"])
            if g.get("module"):
                modules.add(g["module"])
            if g["kind"] == "smbclient":
                modules.add("smbclient_cases")
    for name in sorted(names):
        try:
            load_expected(name)                  # into _LOADED, and stays there
        except FileNotFoundError:
            # The sealing is one-way, so a second `test.sh` in the same container
            # would find nothing to load and score every case false. That is a
            # broken verifier, not a broken candidate, and it has to say so:
            # a silent 0 is indistinguishable from an empty workspace.
            raise SystemExit(
                "verifier: expected/%s is missing before any scenario ran. The "
                "expectation set is removed once per container (seal_corpus); "
                "this container has already scored a run and cannot score "
                "another." % name)
    for mod in sorted(modules):
        importlib.import_module(mod)             # into sys.modules, and stays there

    removed, kept = [], []
    targets = [os.path.join(HERE, "expected")]
    targets += [os.path.join(HERE, "%s.py" % m) for m in sorted(modules)]
    # ...and the compiled copies. Removing a module's source while leaving
    # __pycache__/<module>.cpython-*.pyc behind removes nothing: the bytecode
    # still names every case and every field the case reads.
    targets.append(os.path.join(HERE, "__pycache__"))
    # The step configuration too: it is already parsed into `cfg`, and on disk it
    # is the map from group to answer file, weight and scenario. What the checks
    # *are* stays readable -- a conformance suite is public, and smbtorture's
    # source is on the candidate's own filesystem. What the answers are does not.
    targets.append(os.path.join(HERE, "step.json"))
    for path in targets:
        try:
            if os.path.isdir(path):
                shutil.rmtree(path)
            else:
                os.unlink(path)
            removed.append(path)
        except OSError as exc:
            kept.append("%s (%s)" % (path, exc.strerror))

    # Self-assertion: ask the filesystem, do not trust the removal above. A
    # defence that fails silently is worse than none, because it is believed.
    still = []
    for name in sorted(names):
        probe = os.path.join(HERE, "expected", name)
        try:
            with open(probe, "rb") as fh:
                fh.read(1)
            still.append(name)
        except OSError:
            pass
    note = ("corpus sealed: %d expectation file(s) and %d case module(s) removed "
            "before the service started; re-reading them from this filesystem "
            "now fails" % (len(names), len(modules)))
    if still or kept:
        note = ("CORPUS NOT SEALED: %d of %d expectation file(s) are still "
                "readable by the service (%s)" % (len(still), len(names),
                                                  "; ".join(kept) or "removal reported success"))
    runner.log("seal: " + note)
    return note


def expected_cases(group):
    """The case names a group scores, however it is keyed.

    "note" is reserved for a file's own provenance line and is never a case, so
    the count is the same whether the group ran or was failed wholesale.
    """
    if group["kind"] == "torture":
        return list(load_expected(group["list"])["tests"])
    d = load_expected(group["expected"])
    return [k for k in d if k != "note"]


def run_group(scn, group, groups_out):
    kind = group["kind"]
    cases = {}
    if kind == "torture":
        spec = load_expected(group["list"])
        suites = {}
        for full in spec["tests"]:
            suite, _, name = full.rpartition(".")
            suites.setdefault(suite, []).append(name)
        outcome = runner.score_torture(scn.port, group["share"], group["min"],
                                       group["max"], suites,
                                       per_suite_timeout=group.get("timeout", 180))
        for full in spec["tests"]:
            cases[full] = bool(outcome.get(full, False))
    elif kind == "smbclient":
        expected = load_expected(group["expected"])
        import smbclient_cases
        wanted = set(expected.keys())
        # The same guard the probe groups carry, for the same reason. A service
        # that accepts the connection and then answers nothing costs one
        # smbclient timeout per case -- thirty-six of them is thirty-six minutes,
        # which is most of a step's budget spent discovering the same thing over
        # and over, and the groups after it are this rung's *regression* groups,
        # which such a candidate would otherwise pass. Once the first few cases
        # have all timed out, stop asking.
        consecutive_timeouts, unresponsive = 0, False
        for name, share, commands in smbclient_cases.CASES:
            if name not in wanted:
                continue
            if runner.budget_exhausted():
                cases[name] = False
                runner.note_starved()
                continue
            if unresponsive:
                cases[name] = False
                continue
            runner.seed_share(scn.share_path("tmp"))
            if "ro" in scn.shares:
                runner.seed_share(scn.share_path("ro"))
            out = runner.run_smbclient(scn.port, share, group["min"], group["max"],
                                       commands, extra_args=group.get("args"))
            if b"<TIMEOUT>" in out:
                consecutive_timeouts += 1
                if consecutive_timeouts >= probebase.UNRESPONSIVE_AFTER:
                    runner.log("smbclient group %s: %d consecutive cases timed out; "
                               "the service is not answering this dialect, marking "
                               "the rest without asking\n"
                               % (group["name"], consecutive_timeouts))
                    unresponsive = True
            else:
                consecutive_timeouts = 0
            got = runner.normalise_smbclient(out)
            cases[name] = (got == expected[name])
            if got != expected[name]:
                runner.log("smbclient case %s\n--- expected ---\n%s\n--- got ---\n%s\n"
                           % (name, expected[name], got))
    elif kind == "probe":
        expected = load_expected(group["expected"])
        expected.pop("note", None)   # reserved for the file's provenance line
        mod = importlib.import_module(group["module"])
        runner.seed_share(scn.share_path("tmp"))
        if "ro" in scn.shares:
            runner.seed_share(scn.share_path("ro"))
        observed = mod.run_all(host="127.0.0.1", port=scn.port,
                               user=runner.USER, password=runner.PASSWORD,
                               only=set(expected.keys()))
        for name, want in expected.items():
            cases[name] = (observed.get(name) == want)
            if observed.get(name) != want:
                runner.log("probe case %s\n  expected %r\n  got      %r"
                           % (name, want, observed.get(name)))
    else:
        raise SystemExit("unknown group kind %r" % kind)
    groups_out[group["name"]] = {"weight": float(group["weight"]), "cases": cases}


def main():
    cfg_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "step.json")
    with open(cfg_path) as fh:
        cfg = json.load(fh)

    runner.start_budget(float(cfg.get("budget_sec", 1500)))
    # Before anything the candidate controls is started.
    seal = seal_corpus(cfg)
    groups = {}
    problems = runner.scan_workspace()
    smbd = os.path.join(runner.APP, "bin", "smbd")
    fatal = []
    if not (os.path.isfile(smbd) and os.access(smbd, os.X_OK)):
        fatal.append("no executable at /app/bin/smbd")
    if problems:
        fatal.extend(problems)

    if fatal:
        for p in fatal:
            runner.log("workspace check: " + p)
        for scn_cfg in cfg["scenarios"]:
            for g in scn_cfg["groups"]:
                # expected_cases(), not the raw file: "note" is the file's own
                # provenance line and never a case, so a workspace that fails
                # the preflight has to report the same denominator a scored run
                # would.
                spec = {k: False for k in expected_cases(g)}
                groups[g["name"]] = {"weight": float(g["weight"]), "cases": spec}
        runner.write_report(groups, 0.0, {"fatal": fatal, "seal": seal})
        return

    for scn_cfg in cfg["scenarios"]:
        scn = runner.Scenario(scn_cfg["name"], scn_cfg["port"],
                              scn_cfg.get("globals"), scn_cfg.get("shares"))
        scn.build()
        for share in scn.shares:
            runner.seed_share(scn.share_path(share))
        started = scn.start()
        if not started:
            runner.log("scenario %s: the service did not accept a connection on port %d"
                       % (scn.name, scn.port))
            for g in scn_cfg["groups"]:
                cases = {k: False for k in expected_cases(g)}
                groups[g["name"]] = {"weight": float(g["weight"]), "cases": cases}
            scn.stop()
            continue
        bad = runner.process_tree_ok(scn.proc.pid)
        ok, exe = runner.listener_is_image_node(scn.port)
        if ok is False:
            bad = list(bad) + ["listener is not this image's node: %s" % exe]
        if bad:
            runner.log("scenario %s: unexpected executables in the service process "
                       "tree: %s" % (scn.name, ", ".join(bad)))
            for g in scn_cfg["groups"]:
                cases = {k: False for k in expected_cases(g)}
                groups[g["name"]] = {"weight": float(g["weight"]), "cases": cases}
            scn.stop()
            continue
        try:
            for g in scn_cfg["groups"]:
                # Everything after this group -- in this scenario and in every
                # scenario still to come -- is owed a reserve it cannot be
                # deprived of. The carry-forward groups are usually last.
                runner.start_group(*cases_remaining(cfg, scn_cfg, g), name=g["name"])
                try:
                    run_group(scn, g, groups)
                except Exception as e:  # a broken group must not lose the others
                    runner.log("group %s crashed: %r" % (g["name"], e))
                    import traceback
                    traceback.print_exc(file=sys.stderr)
                    cases = {k: False for k in expected_cases(g)}
                    groups[g["name"]] = {"weight": float(g["weight"]), "cases": cases}
                finally:
                    runner.end_group()
            # The same check again, now that the service has served real traffic:
            # a one-shot check at start-up would miss a service that fetches and
            # execs another server once it is running.
            bad = runner.process_tree_ok(scn.proc.pid) if scn.proc else ["gone"]
            ok, exe = runner.listener_is_image_node(scn.port)
            if ok is False:
                bad = list(bad) + ["listener is not this image's node: %s" % exe]
            if bad:
                runner.log("scenario %s: the service process tree changed while it "
                           "was being scored: %s" % (scn.name, ", ".join(bad)))
                for g in scn_cfg["groups"]:
                    groups[g["name"]] = {"weight": float(g["weight"]),
                                         "cases": {k: False for k in expected_cases(g)}}
        finally:
            note = scn.memory_note()
            if note:
                runner.log("scenario %s: %s" % (scn.name, note))
            scn.stop()

    runner.write_report(groups, runner.combine(groups), {"seal": seal})


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc(file=sys.stderr)
        os.makedirs(runner.LOGDIR, exist_ok=True)
        with open(os.path.join(runner.LOGDIR, "reward.json"), "w") as fh:
            json.dump({"reward": 0.0}, fh)
    sys.exit(0)
