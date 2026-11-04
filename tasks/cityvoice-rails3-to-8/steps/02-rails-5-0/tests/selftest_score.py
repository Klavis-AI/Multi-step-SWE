"""Rehearses the reward arithmetic against known answers.

Same discipline as `selftest_harness.py`: every predicate gets a control
that must pass and a mutation that must fail, and every way the scorer could
award credit for evidence it never saw is a check.  A verifier that reports a
number nobody rehearsed is the failure mode this file exists to prevent.
"""

from __future__ import annotations

import json
import os
import shutil
import sys
import tempfile

import evidence
import score

FAILURES = []
RAN = []

# A floor, not a count: the failure it exists for is this file being edited
# until it runs almost nothing and still prints "ok".  Raised whenever a
# section is added, so that deleting one is a failure here rather than a
# quieter suite.
MIN_CHECKS = 415


def check(name, got, want):
    RAN.append(name)
    if got != want:
        FAILURES.append("%s\n    got:  %r\n    want: %r" % (name, got, want))


def raises(exc, fn, *args, **kwargs):
    try:
        fn(*args, **kwargs)
    except exc:
        return True
    return False


def gate(gid, kind, path, **spec):
    spec.update({"kind": kind, "path": path})
    return {"id": gid, "desc": "d/" + gid, "check": spec}


def profile(gates, behaviour=0.55, gates_weight=0.45, guards=None):
    out = {"step": "t", "weights": {"behaviour": behaviour,
                                    "gates": gates_weight},
           "gates": gates}
    if guards is not None:
        out["guards"] = guards
    return out


def guard(gid, kind, path, **spec):
    spec.update({"kind": kind, "path": path})
    return {"id": gid, "desc": "guard/" + gid, "check": spec}


# --------------------------------------------------------------------------
# 1. path resolution
# --------------------------------------------------------------------------

EV = {"a": {"b": {"c": 7}}, "list": [{"x": 1}, {"x": 2}], "flat": 3}

check("nested path resolves", score.resolve(EV, "a.b.c"), 7)
check("list index resolves", score.resolve(EV, "list.1.x"), 2)
check("top level resolves", score.resolve(EV, "flat"), 3)
check("a missing key is MISSING", score.resolve(EV, "a.zz"), score.MISSING)
check("a missing branch is MISSING", score.resolve(EV, "zz.b.c"), score.MISSING)
check("an out-of-range index is MISSING",
      score.resolve(EV, "list.9.x"), score.MISSING)
check("a non-integer index is MISSING",
      score.resolve(EV, "list.x"), score.MISSING)
check("descending into a scalar is MISSING",
      score.resolve(EV, "flat.more"), score.MISSING)


# --------------------------------------------------------------------------
# 2. every predicate, both ways
# --------------------------------------------------------------------------

def one(ev, g):
    return score.evaluate_gate(ev, g)["ok"]


check("equals passes", one({"v": "4.2.11.3"},
                           gate("g", "equals", "v", to="4.2.11.3")), True)
check("equals fails", one({"v": "4.2.11.2"},
                          gate("g", "equals", "v", to="4.2.11.3")), False)

check("regex passes", one({"v": "4.2.11.3"},
                          gate("g", "regex", "v", pattern=r"^4\.2\.")), True)
check("regex fails", one({"v": "4.1.16"},
                         gate("g", "regex", "v", pattern=r"^4\.2\.")), False)
check("regex on a non-string fails, it does not coerce",
      one({"v": 42}, gate("g", "regex", "v", pattern=r"4")), False)

check("at_most passes at the boundary",
      one({"n": 0}, gate("g", "at_most", "n", n=0)), True)
check("at_most fails above it",
      one({"n": 1}, gate("g", "at_most", "n", n=0)), False)
check("at_most rejects a bool as a number",
      one({"n": True}, gate("g", "at_most", "n", n=1)), False)
check("at_least passes at the boundary",
      one({"n": 520}, gate("g", "at_least", "n", n=520)), True)
check("at_least fails below it",
      one({"n": 519}, gate("g", "at_least", "n", n=520)), False)

check("empty passes on []", one({"v": []}, gate("g", "empty", "v")), True)
check("empty passes on {}", one({"v": {}}, gate("g", "empty", "v")), True)
check("empty fails on a populated list",
      one({"v": ["x"]}, gate("g", "empty", "v")), False)
check("empty fails on a non-collection",
      one({"v": 0}, gate("g", "empty", "v")), False)
check("empty names what it saw",
      score.evaluate_gate({"v": ["x", "y"]}, gate("g", "empty", "v"))["saw"],
      '2 entries: ["x", "y"]')

GEMS = {"specs": {"rails": "4.2.11.3", "protected_attributes": "1.0.9"}}
check("absent_keys fails while the gem is there",
      one(GEMS, gate("g", "absent_keys", "specs",
                     keys=["protected_attributes"])), False)
check("absent_keys passes once it is gone",
      one({"specs": {"rails": "4.2.11.3"}},
          gate("g", "absent_keys", "specs", keys=["protected_attributes"])),
      True)
check("absent_keys works against a list too",
      one({"reachable": ["rails"]},
          gate("g", "absent_keys", "reachable", keys=["compass-rails"])), True)
check("absent_keys names the survivor",
      score.evaluate_gate(GEMS, gate("g", "absent_keys", "specs",
                                     keys=["protected_attributes", "rails"]))["saw"],
      "still present: ['protected_attributes', 'rails']")
check("present_keys passes",
      one(GEMS, gate("g", "present_keys", "specs", keys=["rails"])), True)
check("present_keys fails",
      one(GEMS, gate("g", "present_keys", "specs", keys=["activejob"])), False)

check("is_true passes on True",
      one({"v": True}, gate("g", "is_true", "v")), True)
check("is_true fails on False",
      one({"v": False}, gate("g", "is_true", "v")), False)
# The one that matters: a truthy non-boolean is not a pass.  `booted` is a
# boolean; a probe that recorded a string there is a broken probe.
check("is_true fails on a truthy string",
      one({"v": "yes"}, gate("g", "is_true", "v")), False)
check("is_true fails on 1", one({"v": 1}, gate("g", "is_true", "v")), False)

check("version_at_least passes above",
      one({"v": "4.2.11.3"},
          gate("g", "version_at_least", "v", n="4.2.0")), True)
check("version_at_least passes at equality",
      one({"v": "4.2.0"}, gate("g", "version_at_least", "v", n="4.2.0")), True)
check("version_at_least fails below",
      one({"v": "4.1.16"}, gate("g", "version_at_least", "v", n="4.2.0")), False)
# Lexicographic comparison would put "4.10" below "4.2"; numeric does not.
check("version_at_least is numeric, not lexicographic",
      one({"v": "4.10.0"}, gate("g", "version_at_least", "v", n="4.2.0")), True)


# --------------------------------------------------------------------------
# 3. missing and broken evidence is never a pass
# --------------------------------------------------------------------------

g_missing = score.evaluate_gate({}, gate("g", "at_most", "scan.counts.x", n=0))
check("a gate over absent evidence fails", g_missing["ok"], False)
check("and says where it looked", g_missing["saw"],
      "no evidence at scan.counts.x")

# A bootprobe readout that raised records {"error":..., "backtrace":[...]} in
# place of its value.  Without this branch, `empty` over that dict would be
# False (two keys) but `absent_keys` over it would be *True*, which is a probe
# crash scoring as a clean migration.
crashed = {"probes": {"database": {"error": "PG::ConnectionBad: nope",
                                   "backtrace": ["a", "b"]}}}
g_crashed = score.evaluate_gate(
    crashed, gate("g", "absent_keys", "probes.database", keys=["adapter"]))
check("a crashed probe fails its gate", g_crashed["ok"], False)
check("and reports the exception", g_crashed["saw"],
      "probe failed: PG::ConnectionBad: nope")
check("a real dict with an error field among others is still evaluated",
      one({"p": {"error": 0, "a": 1, "b": 2, "c": 3}},
          gate("g", "at_most", "p.error", n=0)), True)


# --------------------------------------------------------------------------
# 4. the arithmetic
# --------------------------------------------------------------------------

GATES = [gate("one", "at_most", "n", n=0),
         gate("two", "at_most", "m", n=0)]
GATES[0]["weight"] = 3
GATES[1]["weight"] = 1

full = score.score(profile(GATES), {"n": 0, "m": 0,
                                    "behaviour": {"fraction": 1.0}})
check("everything green is 1.0", full["reward"], 1.0)

none = score.score(profile(GATES), {"n": 1, "m": 1,
                                    "behaviour": {"fraction": 0.0}})
check("everything red is 0.0", none["reward"], 0.0)

split = score.score(profile(GATES), {"n": 0, "m": 1,
                                     "behaviour": {"fraction": 0.5}})
check("gate weights are respected", split["gates"]["fraction"], 0.75)
# Product scoring: reward = gates (T1) x behaviour (T2).
check("families combine as a product", split["reward"],
      round(0.75 * 0.5, 6))

# The trap this guards: a behaviour report the verifier failed to write must
# score 0, not vanish from the arithmetic and leave the gates at full weight.
gone = score.score(profile(GATES), {"n": 0, "m": 0})
check("a missing behaviour report scores 0, not 1", gone["reward"], 0.0)
check("and says so", gone["behaviour"]["missing"], True)
check("a non-numeric behaviour fraction scores 0",
      score.score(profile(GATES),
                  {"n": 0, "m": 0, "behaviour": {"fraction": "1.0"}})["reward"],
      0.0)

check("the reward is clamped into [0,1]",
      score.score(profile(GATES),
                  {"n": 0, "m": 0, "behaviour": {"fraction": 4.0}})["reward"],
      1.0)


# --------------------------------------------------------------------------
# 5. the profile itself
# --------------------------------------------------------------------------

check("a family below the 30% floor is rejected",
      raises(score.BadProfile, score.score,
             profile(GATES, behaviour=0.8, gates_weight=0.2), {}), True)
check("the other family below the floor is rejected too",
      raises(score.BadProfile, score.score,
             profile(GATES, behaviour=0.25, gates_weight=0.75), {}), True)
check("0.30 exactly is allowed",
      score.score(profile(GATES, behaviour=0.30, gates_weight=0.70),
                  {"n": 0, "m": 0, "behaviour": {"fraction": 1.0}})["reward"],
      1.0)
check("weights that do not sum to 1 are rejected",
      raises(score.BadProfile, score.score,
             profile(GATES, behaviour=0.55, gates_weight=0.55), {}), True)
check("a profile with no gates is rejected",
      raises(score.BadProfile, score.score, profile([]), {}), True)
check("duplicate gate ids are rejected",
      raises(score.BadProfile, score.score,
             profile([gate("dup", "at_most", "n", n=0),
                      gate("dup", "at_most", "m", n=0)]), {}), True)
check("an unknown predicate is rejected",
      raises(score.BadProfile, score.score,
             profile([gate("g", "vibes", "n")]), {}), True)
check("a zero-weight gate is rejected",
      raises(score.BadProfile, score.score,
             profile([dict(gate("g", "at_most", "n", n=0), weight=0)]), {}),
      True)
check("a gate with no description is rejected",
      raises(score.BadProfile, score.score,
             profile([{"id": "g", "check": {"kind": "at_most", "path": "n",
                                            "n": 0}}]), {}),
      True)


# --------------------------------------------------------------------------
# 6. what the verifier prints
# --------------------------------------------------------------------------

text = score.render(split)
check("the rendering names a failing gate", "[FAIL] two" in text, True)
check("the rendering names a passing gate", "[PASS] one" in text, True)
check("the rendering states what a failed gate wanted",
      "requires: d/two" in text, True)
check("the rendering carries the reward", "reward     0.375000" in text, True)

flipped = score.score(profile(GATES),
                      {"n": 0, "m": 0,
                       "behaviour": {"fraction": 1.0, "passed": 391,
                                     "total": 391,
                                     "flips": {"total": 3, "flipped": 3,
                                               "stale": []}}})
check("must-flip counts reach the report",
      flipped["behaviour"]["flips"]["flipped"], 3)
check("and the rendering", "must-flip 3/3" in score.render(flipped), True)

# A stale must-flip has two steps with a claim on it and the line names both:
# the step whose overlay owes the change, and the step whose answer the tree is
# giving instead.  On a case an earlier step already moved once those differ,
# and naming only the second reads as a regression in a step that is fine.
def _flips(flips):
    return score.score(profile(GATES),
                       {"n": 0, "m": 0,
                        "behaviour": {"fraction": 0.5, "passed": 195,
                                      "total": 391, "flips": flips}})


paired = _flips({"total": 3, "flipped": 1, "stale": ["w/a", "w/b"],
                 "stale_by_step": {"02-x": ["w/a"], "baseline": ["w/b"]},
                 "stale_detail": [
                     {"owed_by": "05-y", "still_on": "02-x", "ids": ["w/a"]},
                     {"owed_by": "05-y", "still_on": "baseline",
                      "ids": ["w/b"]}]})
check("the stale line names the step that owes the flip",
      "1 owed by 05-y, still on 02-x's answer" in score.render(paired), True)
check("and spells the baseline label as English",
      "1 owed by 05-y, still on the baseline answer" in score.render(paired),
      True)

# A report written before `stale_detail` existed still renders, by the older
# single-label route.
older = _flips({"total": 3, "flipped": 1, "stale": ["w/a"],
                "stale_by_step": {"02-x": ["w/a"]}})
check("an older report falls back to the single label",
      "1 still on 02-x's answer" in score.render(older), True)

# A behaviour family that scored nothing because the application never came up
# is a different finding from one that scored nothing case by case, and the
# render has to say which.  The list comes straight from the comparison report.
dead = score.score(profile(GATES),
                   {"n": 0, "m": 0,
                    "behaviour": {"fraction": 0.0, "passed": 0, "total": 391,
                                  "dead_worlds": ["imports", "ivr_happy"]}})
check("dead worlds reach the report",
      dead["behaviour"]["dead_worlds"], ["imports", "ivr_happy"])
check("the rendering names them",
      "2 world(s) never came up: imports, ivr_happy" in score.render(dead),
      True)
check("no dead worlds adds no line",
      "never came up" in score.render(flipped), False)


# --------------------------------------------------------------------------
# 8. the evidence join
# --------------------------------------------------------------------------
#
# `evidence.py` is the other half of the instrument: score.py can only fail a
# gate honestly if the join told it the truth about which probes ran.  Both
# halves are rehearsed against known answers here, and every behaviour gets a
# control and a mutation -- a report that is there, and the same report absent,
# empty, or truncated.

WORK = tempfile.mkdtemp(prefix="selftest-evidence-")


def wrote(name, text):
    path = os.path.join(WORK, name)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    return path


GOOD = wrote("good.json", json.dumps({"n": 3, "items": [1, 2, 3]}))
EMPTY = wrote("empty.json", "")
TRUNCATED = wrote("truncated.json", '{"n": 3, "item')
GONE = os.path.join(WORK, "not-written.json")

check("a readable report loads", evidence.load_report(GOOD)["n"], 3)
check("a missing report is an error stub",
      evidence.load_report(GONE), {"error": "no report at %s" % GONE})
check("an empty report is its own error",
      evidence.load_report(EMPTY), {"error": "empty report at %s" % EMPTY})
check("a truncated report is an error stub, not an exception",
      evidence.load_report(TRUNCATED)["error"].startswith(
          "unreadable report at"), True)

# The stubs are the shape score.py recognises.  This is the join between the
# two files, so it is checked as a number of keys and then end to end below.
for label, stub in (("missing", evidence.load_report(GONE)),
                    ("empty", evidence.load_report(EMPTY)),
                    ("truncated", evidence.load_report(TRUNCATED))):
    check("the %s stub is small enough for score.py to recognise" % label,
          len(stub) <= 2 and "error" in stub, True)

PRUNABLE = {"a": {"b": {"big": [1, 2, 3], "small": 1}}}
check("prune drops the node", evidence.prune(PRUNABLE, "a.b.big"), True)
check("and leaves its siblings", PRUNABLE, {"a": {"b": {"small": 1}}})
check("prune of an absent path reports that it found nothing",
      evidence.prune(PRUNABLE, "a.b.big"), False)
check("prune through a scalar reports nothing rather than raising",
      evidence.prune({"a": 1}, "a.b.c"), False)

JOINED = evidence.assemble([("good", GOOD), ("broken", GONE)],
                           prunes=["good.items", "good.nothing"],
                           meta={"step": "t"})
check("every key asked for is in the join", sorted(JOINED) ,
      ["broken", "good", "meta"])
check("a probe that did not run is present as its error",
      "error" in JOINED["broken"], True)
check("a probe that did run is unrenamed", JOINED["good"]["n"], 3)
check("the pruned list is gone", "items" in JOINED["good"], False)
check("the join records which prune matched",
      JOINED["meta"]["pruned"], {"good.items": True, "good.nothing": False})
check("the join records each source's size",
      JOINED["meta"]["sources"]["good"]["bytes"], os.path.getsize(GOOD))
check("the join records each source's error",
      JOINED["meta"]["sources"]["broken"]["error"].startswith("no report at"),
      True)
check("the caller's own metadata survives", JOINED["meta"]["step"], "t")
check("the same key twice is an error and not a silent overwrite",
      raises(SystemExit, evidence.assemble,
             [("good", GOOD), ("good", GOOD)]), True)

RENDERED = evidence.render(JOINED)
check("the summary names a failed source", "FAILED" in RENDERED, True)
check("the summary counts them", "2 reports, 1 failed: ['broken']" in RENDERED,
      True)
check("the summary says a prune matched nothing",
      "prune good.nothing matched nothing" in RENDERED, True)

# End to end: the same two gates, once over evidence whose probe ran and once
# over evidence whose probe did not.  This is the pair the whole file exists
# for -- "no report" has to cost exactly its own gate.
E2E_GATES = [gate("has_n", "at_least", "good.n", n=3),
             gate("broken_gate", "at_least", "broken.n", n=1)]
E2E = score.score(profile(E2E_GATES),
                  dict(JOINED, behaviour={"fraction": 1.0, "passed": 1,
                                          "total": 1}))
check("the gate whose probe ran is scored on its value",
      E2E["gates"]["results"][0]["ok"], True)
check("the gate whose probe did not run fails",
      E2E["gates"]["results"][1]["ok"], False)
check("and says the probe failed rather than naming a path",
      E2E["gates"]["results"][1]["saw"].startswith("probe failed: no report"),
      True)
check("while a mistyped path under a live probe still says where it looked",
      score.evaluate_gate(JOINED, gate("typo", "at_least", "good.nn", n=1))
      ["saw"], "no evidence at good.nn")
check("and a dead probe is named however deep the path went",
      score.evaluate_gate(JOINED,
                          gate("deep", "is_true", "broken.a.b.c"))["saw"]
      .startswith("probe failed: no report"), True)
check("a report with an error field among its findings is not a dead probe",
      score.is_probe_error({"error": "x", "findings": [], "n": 1}), False)
check("one dead probe costs one gate and no more", E2E["reward"],
      round(0.5 * 1.0, 6))

# The meta passthrough, and the one line of it that is not decoration.  A
# verifier image whose file modes stop phase 4 from reading its own probes
# produces a report that is arithmetic over probes that could not be asked;
# `test.sh` detects that before it happens and records it here, and the render
# has to lead with it rather than bury it under fourteen gate lines.
check("report.json carries the evidence meta", E2E["meta"]["step"], "t")
check("a clean preflight adds no line to the render",
      "PREFLIGHT" in score.render(E2E), False)
PRE = dict(E2E, meta=dict(E2E["meta"], preflight="unreadable:bootprobe "))
check("a failed preflight leads the render",
      score.render(PRE).splitlines()[0].startswith("PREFLIGHT   unreadable:"),
      True)
check("and says the gates under it are not a measurement",
      "not a measurement of the tree" in score.render(PRE), True)
check("a report with no meta at all still renders",
      "PREFLIGHT" in score.render(dict(E2E, meta=None)), False)
check("evidence with no meta at all leaves the key null",
      score.score(profile(E2E_GATES), {"behaviour": {"fraction": 0.0}})["meta"],
      None)

# --------------------------------------------------------------------------
# guards: the predicates that are worth nothing and cost everything
# --------------------------------------------------------------------------
#
# A guard exists because the three cheap wrong answers to this task's gates --
# silence the scanner, break the file it reads, delete the code it reads -- are
# all things an *untouched* tree does not do.  As weighted gates they are free
# points for doing nothing and a bargain for cheating.  So the arithmetic is
# checked from both sides here: a satisfied guard changes nothing at all, and a
# failed one takes the whole family, however many gates passed.

G_GATES = [gate("real", "is_true", "ok")]
G_GUARDS = [guard("intact", "at_least", "files", n=150)]
G_EV_OK = {"ok": True, "files": 177, "behaviour": {"fraction": 1.0}}
G_EV_BAD = {"ok": True, "files": 2, "behaviour": {"fraction": 1.0}}

check("a profile with no guards still scores",
      score.score(profile(G_GATES), G_EV_OK)["reward"], 1.0)
check("a satisfied guard changes nothing",
      score.score(profile(G_GATES, guards=G_GUARDS), G_EV_OK)["reward"], 1.0)
check("a failed guard zeroes the gates family",
      score.score(profile(G_GATES, guards=G_GUARDS), G_EV_BAD)
      ["gates"]["fraction"], 0.0)
check("a failed guard zeroes the reward, behaviour whole or not",
      score.score(profile(G_GATES, guards=G_GUARDS), G_EV_BAD)["reward"], 0.0)
check("and leaves the behaviour factor itself alone",
      score.score(profile(G_GATES, guards=G_GUARDS), G_EV_BAD)
      ["behaviour"]["fraction"], 1.0)
check("the gates that passed are still reported",
      [g["ok"] for g in score.score(profile(G_GATES, guards=G_GUARDS), G_EV_BAD)
       ["gates"]["results"]], [True])
check("the failed guard is named in the report",
      score.score(profile(G_GATES, guards=G_GUARDS), G_EV_BAD)
      ["gates"]["failed_guards"], ["intact"])
check("a guard result carries no weight to be mistaken for a gate",
      "weight" in score.score(profile(G_GATES, guards=G_GUARDS), G_EV_OK)
      ["gates"]["guards"][0], False)
check("a guard over evidence that is not there fails rather than passes",
      score.score(profile(G_GATES, guards=G_GUARDS),
                  {"ok": True, "behaviour": {"fraction": 1.0}})
      ["gates"]["failed_guards"], ["intact"])
check("the render says the family was zeroed",
      "ZEROED BY 1 GUARD(S)" in
      score.render(score.score(profile(G_GATES, guards=G_GUARDS), G_EV_BAD)),
      True)
check("the render says which guard, and what it requires",
      "guard/intact" in
      score.render(score.score(profile(G_GATES, guards=G_GUARDS), G_EV_BAD)),
      True)
check("a satisfied guard adds no line to the render",
      "GUARD" in
      score.render(score.score(profile(G_GATES, guards=G_GUARDS), G_EV_OK)),
      False)
check("a guard with a weight is a profile error",
      raises(score.BadProfile, score.score,
             profile(G_GATES, guards=[dict(G_GUARDS[0], weight=1)]), G_EV_OK),
      True)
check("a guard with no description is a profile error",
      raises(score.BadProfile, score.score,
             profile(G_GATES, guards=[{"id": "g", "check": {"kind": "is_true",
                                                            "path": "ok"}}]),
             G_EV_OK),
      True)
check("a guard sharing a gate's id is a profile error",
      raises(score.BadProfile, score.score,
             profile(G_GATES, guards=[guard("real", "is_true", "ok")]), G_EV_OK),
      True)

shutil.rmtree(WORK, ignore_errors=True)


# --------------------------------------------------------------------------
# 9. the real profiles, against golden-shaped evidence
# --------------------------------------------------------------------------
# The sections above rehearse the arithmetic on profiles written for the
# purpose.  None of them would notice a real profile that reads a path no probe
# emits, or a gate whose predicate can never be satisfied by the tree the
# solution produces -- and that is the failure that costs a whole step, because
# it shows up as a number rather than as an error.
#
# So every shipped profile is loaded and evaluated here against evidence
# written by hand to say what that step's golden tree looks like.  The fixtures
# are deliberately *not* derived from the profiles: a fixture generated from
# the thing it checks proves only that the generator ran.

HERE = os.path.dirname(os.path.abspath(__file__))


def discover():
    """Every (step number, profile, manifest) this run should rehearse.

    Two layouts.  Beside a `../profiles/` directory holding one profile per
    step, all of them are rehearsed, so a step whose profile stopped agreeing
    with the shared probes is found here rather than by its verifier twenty
    minutes in.  Inside a step's `tests/` there is exactly one profile, at
    `step/profile.json`, and the step it belongs to is the step it names.

    Finding nothing is a failure and not a skip -- a selftest that quietly
    tests nothing is the thing this file is against.
    """
    profiles = os.path.join(HERE, "..", "profiles")
    if os.path.isdir(profiles):
        found = []
        for name in sorted(os.listdir(profiles)):
            if len(name) == 7 and name.endswith(".json") and name[:2].isdigit():
                found.append((name[:2],
                              os.path.join(profiles, name),
                              os.path.join(HERE, "..", "manifests", name)))
        return found
    shipped = os.path.join(HERE, "step", "profile.json")
    if os.path.exists(shipped):
        with open(shipped) as handle:
            number = json.load(handle)["step"][:2]
        return [(number, shipped, os.path.join(HERE, "step", "pristine.json"))]
    return []


# What each step's golden tree looks like, written out rather than computed.
# Every value here was read off that step's golden tree and the reports the
# probes produce from it.  A profile with no fixture is a failure: adding step
# 03 without saying what its answer looks like would leave its gates rehearsed
# by nothing at all.
GOLDEN_BY_STEP = {
    "01": {
        "behaviour": {"fraction": 1.0, "passed": 1, "total": 1},
        "bootprobe": {
            "ruby": "2.3.8",
            "booted": True,
            "probes": {
                "versions": {"rails": "4.2.11.3"},
                # `lib/` is on `autoload_paths` and not on `eager_load_paths`
                # in step 01's answer, and that is correct there: 4.2 keeps
                # autoloading after boot, so a constant that is undefined the
                # moment `eager_load!` returns is still defined the first time
                # the application asks for it, and the application works.  Both
                # readings are recorded at every step and gated only from 02,
                # where 5.0 stops autoloading in production and the same
                # configuration becomes a 500.
                #
                # Which is why five constants are listed here and the step
                # passes anyway.  4.2 autoloading does not make this `[]`:
                # eager loading `app/` does not reference them, so at that
                # instant they genuinely do not exist.  Two of the seven
                # `lib/*.rb` files are absent from the list because an
                # initializer has already named them.  The number is measured,
                # not derived.
                "eager_load": {"ok": True, "autoload_only": ["lib"],
                               "undefined_outside_app": [
                                   "lib/app_content_renderer.rb "
                                   "(AppContentRenderer)",
                                   "lib/importer_errors.rb (ImporterErrors)",
                                   "lib/location_importer.rb "
                                   "(LocationImporter)",
                                   "lib/question_importer.rb "
                                   "(QuestionImporter)",
                                   "lib/question_presenter.rb "
                                   "(QuestionPresenter)"]},
                "secrets": {"secrets_yml_present": True,
                            "secrets_yml_path": "/app/config/secrets.yml",
                            "from_secrets_yml": True,
                            "effective_from_secrets_yml": True},
            },
        },
        "gemgraph": {
            "ruby": "2.3.8p459",
            "ruby_version_file": "2.3.8",
            "gemfile_ruby": "2.3.8",
            "ruby_declared": "2.3.8",
            "specs": {"rails": "4.2.11.3"},
        },
        "scan": {
            "pristine": "/tests/step/pristine.json",
            "missing_tests": [],
            "missing_core": [],
            "gutted_tests": [],
            "assertion_lines": 309,
            "hollow_tests": [],
            "emptied_tests": [],
            "parse_errors": [],
            # The two migration counts are read at every step, whether or
            # not this step's profile gates them.  Step 01's tree has not
            # versioned its migrations yet -- 43 of them -- and has therefore
            # versioned none of them wrongly.  Which is exactly why the count
            # of wrong versions is not a gate: see `migrations` below.
            "counts": {"attr_accessible": 0, "before_filter": 0,
                       "suppression": 0, "secret_key_base_assignment": 0,
                       "dead_engine_ref": 0,
                       "unversioned_migration": 43,
                       "misversioned_migration": 0},
            "migrations": {"total": 43, "unversioned": 43, "versions": {},
                           "shipped_total": 43, "shipped_unversioned": 43,
                           "shipped_versions": {}},
        },
        "depdrive": {"own_count": 0},
        # Two own deprecations and no gated one: factory_girl 4.9 announces its
        # own rename from inside `Bundler.require`, once per environment
        # booted, and `blame` attributes it to `config/application.rb` -- an
        # application file, which is why the gate reads the narrower list.
        "envboot": {"ruby": "2.3.8",
                    "declared": ["development", "production", "test"],
                    "expected": ["development", "test"],
                    "skipped": ["production"],
                    "checked": ["development", "test"],
                    "failed": [], "own_deprecation_count": 2,
                    "env_file_deprecations": []},
        "rspec": {"ran": True, "exit": 0, "db_exit": 0, "examples": 504,
                  "failures": 0, "pending": 0, "ok": True},
        "handoff": {"database_yml_present": False,
                    "database_yml_example_present": True,
                    "db_config_problems": []},
    },
    # Step 03 moves the framework one more minor and, with it, five things
    # that 5.1 either introduced or stopped tolerating: the declaration that
    # says which generation of defaults the application is on, the asset
    # helpers' refusal to guess at a name sprockets cannot resolve, the form
    # builder's new name, secrets kept encrypted rather than in the clear, and
    # mailers that take their arguments by keyword.  `pkdrift.pinned` is the
    # sixth and is not a 5.1 behaviour at all -- it is what the two ways of
    # building this database say after the generation's `bigserial` default
    # meets seven tables whose keys were created narrow.  It is a conjunction
    # of four things and not one: the dump and the migrations build the same
    # tables, they build them at the same width, that width is the narrow one,
    # and the framework's own default is now the wide one.  Any three of those
    # hold on a tree that did nothing.
    "03": {
        "behaviour": {"fraction": 1.0, "passed": 1, "total": 1},
        "bootprobe": {
            "ruby": "2.4.10",
            "booted": True,
            "probes": {
                "versions": {"rails": "5.1.7"},
                "framework_defaults_files": [],
                "eager_load": {"ok": True, "autoload_only": [],
                               "undefined_outside_app": []},
                # Both helpers resolve a name that is not an asset to the
                # public path -- and both are `strict`, which is the reading
                # that says the resolution came from a pipeline that would
                # have raised on a miss rather than from one that guesses.
                # `fallback` is the switch; `*_strict` is its consequence, and
                # the gates are on the consequence.
                "audio": {"helper_strict": True, "error_strict": True,
                          "fallback": False,
                          "helper_ok": True, "error_ok": True,
                          "helper_raised": None, "error_raised": None,
                          "helper_public_path": True,
                          "error_public_path": True,
                          "helper_deprecations": [], "error_deprecations": []},
                # `secrets_yml_present` stays true and its path is now the
                # `.enc`: the reading is "the file the application read", and
                # after this step that file is the encrypted one.  128 bytes
                # of key both times because the key itself did not change --
                # only where it was kept.  `plaintext_present` is the narrow
                # question (is there a readable `config/secrets.yml`);
                # `plaintext_secret_files` is the tree-wide one, and it is the
                # gated reading because the narrow one is satisfied by moving
                # the file.
                "secrets": {"effective_from_encrypted": True,
                            "read_encrypted_secrets": True,
                            "encrypted_key_file_present": True,
                            "encrypted_key_from_env": False,
                            "encrypted_present": True,
                            "encrypted_length": 128,
                            "plaintext_present": False,
                            "plaintext_secret_files": [],
                            "effective_from_secrets_yml": False,
                            "secrets_yml_path": "/app/config/secrets.yml.enc",
                            "reader_exists": True},
                # One mailer, three actions.  `feedback_input_type` keeps its
                # positional argument and is not an action the application
                # delivers -- `action_arity` is every public method, and
                # `declared_action_arity` is the two the application actually
                # mails, which is the pair `all_parameterized` is computed
                # from.
                "mailers": {"all_parameterized": True,
                            "with_supported": True,
                            "action_mailer": True,
                            "mailers": ["NotificationMailer"],
                            "declared_action_arity": {
                                "NotificationMailer#confirmation_email": 0,
                                "NotificationMailer#weekly_activity": 0}},
                # The six `belongs_to` and the one that is optional are
                # read here too, because step 02's three gates on them are
                # step 03's guards: the handed-on tree has them, and a tree
                # that made `Call#location` required again -- or made all six
                # optional to quiet something -- would be undoing a
                # predecessor's answer while passing this step's own gates.
                "models": {"application_record": "ApplicationRecord",
                           "application_record_abstract": True,
                           "belongs_to": {"Answer#call": True,
                                          "Answer#question": True,
                                          "Call#caller": True,
                                          "Call#location": False,
                                          "LocationSubscription#location": True,
                                          "LocationSubscription#subscriber": True},
                           "belongs_to_optional_count": 1,
                           "stray_base_subclasses": [],
                           "string_conditions": []},
                # Seven tables, every one of them `integer`, against a schema
                # whose declared default is `bigserial`.  This is the dump
                # side alone -- what `db:schema:load` built from the tree's own
                # `db/schema.rb` -- and it is not what the gate reads:
                # see `pkdrift` below, which compares it with the database the
                # migrations build.  `pk_pinned` stays in the evidence because
                # it is the half of that comparison a reader wants first, and
                # because a run where the two disagree is much easier to read
                # when both sides are written down.
                "database": {"pk_pinned": True,
                             "wide_default_primary_key": True,
                             "default_primary_key": "bigserial primary key",
                             "wide_id_tables": [],
                             "schema_format": "ruby",
                             "migrations_applied": 43,
                             "id_sql_types": {"answers": "integer",
                                              "callers": "integer",
                                              "calls": "integer",
                                              "location_subscriptions":
                                                  "integer",
                                              "locations": "integer",
                                              "questions": "integer",
                                              "subscribers": "integer"}},
                "framework_defaults": {"belongs_to_required_by_default": True,
                                       "not_adopted": []},
            },
        },
        "gemgraph": {
            "ruby": "2.4.10p364",
            "ruby_version_file": "2.4.10",
            "gemfile_ruby": "2.4.10",
            "ruby_declared": "2.4.10",
            "specs": {"rails": "5.1.7", "arel": "8.0.0", "erubi": "1.13.1"},
        },
        "scan": {
            "pristine": "/tests/step/pristine.json",
            "missing_tests": [],
            "missing_core": [],
            "gutted_tests": [],
            "assertion_lines": 309,
            "hollow_tests": [],
            "emptied_tests": [],
            "parse_errors": [],
            # Four counts this step reads that no earlier step did.  The
            # declaration is present once and names 5.1: `load_defaults`
            # counts every declaration and `load_defaults_5_1` counts the ones
            # whose argument really is 5.1, which is the gated reading.  The
            # complement -- "no declaration names anything else" -- could only
            # be a guard, because a tree with no declaration at all satisfies
            # it; and as a guard a near miss would cost the whole family while
            # a skip cost almost nothing.  Beside it: no switch was written out
            # one by one instead, and no mailer is still called positionally.
            # The migration counts are the unchanged pair: all 43 still say
            # 4.2, because this step did not ask for a migration to be
            # rewritten and a tree that restamped them would be claiming a
            # schema history it does not have.
            "counts": {"load_defaults": 1, "load_defaults_5_1": 1,
                       "framework_defaults_switch": 0,
                       "mailer_positional": 0,
                       "form_with": 2, "form_for": 0,
                       "suppression": 0, "unversioned_migration": 0,
                       "misversioned_migration": 0,
                       "before_filter": 0, "attr_accessible": 0,
                       "secret_token": 0, "secret_key_base_assignment": 0},
            "migrations": {"total": 43, "unversioned": 0,
                           "versions": {"4_2": 43},
                           "shipped_total": 43, "shipped_unversioned": 0,
                           "shipped_versions": {"4_2": 43}},
        },
        # The two databases, both built by the verifier's own final phase and
        # both read by the same probe: `db:schema:load` from a phase-1 copy of
        # the handed-on `db/schema.rb`, then `db:migrate` from `db/migrate/`.
        # The copy is the point.  `dump_schema_after_migration` defaults to
        # true, the behaviour recording runs `db:migrate`, and so the dump in
        # the tree by phase 4 is one this framework wrote -- a gate reading
        # that would be scoring the verifier's own repair work.
        #
        # The golden's two constructions agree table for table at the narrow
        # key on a framework that would now hand out the wide one, which is
        # every conjunct at once.  `problems` empty is the reading that says
        # so without having to be interpreted: every way of failing this puts
        # a sentence in that list.
        "pkdrift": {"ruby": "2.4.10",
                    "booted": True,
                    "dump_source": "/tmp/gates/pk-dump.json",
                    "migrated_source": "/tmp/gates/pk-migrated.json",
                    "schema_load_exit": 0,
                    "migrate_exit": 0,
                    "migrations_applied": 43,
                    "default_primary_key": "bigserial primary key",
                    "wide_default_primary_key": True,
                    "dump": {"wide_id_tables": [], "odd_id_tables": [],
                             "id_widths": {"answers": "narrow",
                                           "callers": "narrow",
                                           "calls": "narrow",
                                           "location_subscriptions": "narrow",
                                           "locations": "narrow",
                                           "questions": "narrow",
                                           "subscribers": "narrow"}},
                    "migrated": {"wide_id_tables": [], "odd_id_tables": [],
                                 "id_widths": {"answers": "narrow",
                                               "callers": "narrow",
                                               "calls": "narrow",
                                               "location_subscriptions":
                                                   "narrow",
                                               "locations": "narrow",
                                               "questions": "narrow",
                                               "subscribers": "narrow"},
                                 "id_sql_types": {"answers": "integer",
                                                  "callers": "integer",
                                                  "calls": "integer",
                                                  "location_subscriptions":
                                                      "integer",
                                                  "locations": "integer",
                                                  "questions": "integer",
                                                  "subscribers": "integer"}},
                    "tables_only_in_dump": [],
                    "tables_only_in_migrated": [],
                    "differing_tables": {},
                    # A width equal on both sides and spelled two ways is
                    # recorded here and is not a finding; the golden has none.
                    "differing_spellings": {},
                    "agree": True,
                    "both_narrow": True,
                    "pinned": True,
                    "problems": []},
        "depdrive": {"own_count": 0},
        "envboot": {"ruby": "2.4.10",
                    "declared": ["development", "production", "test"],
                    "expected": ["development", "test"],
                    "skipped": ["production"],
                    "checked": ["development", "test"],
                    "failed": [], "env_file_deprecations": []},
        "rspec": {"ran": True, "exit": 0, "db_exit": 0, "examples": 504,
                  "failures": 0, "pending": 0, "ok": True,
                  "deprecations": 1, "suite_deprecations": 0,
                  "suite_deprecation_sites": []},
        "handoff": {"database_yml_present": False,
                    "database_yml_example_present": True,
                    "db_config_problems": []},
    },
    # Step 02 hands on the same suite and the same tree; what moved is the
    # interpreter, the framework, the model base class, the migration
    # superclasses and the 5.0 defaults.  `rspec.suite_deprecations` is the
    # count blamed on files under `spec/`, which is not the same number as
    # `deprecations` -- see `selftest_rspec_parse.py`.  The *gated* reading is
    # `suite_deprecation_sites`, which is the union of that and what the
    # in-process collector recorded; the two are reported separately because
    # only one of them survives a tree that silences its deprecations.
    "02": {
        "behaviour": {"fraction": 1.0, "passed": 1, "total": 1},
        "bootprobe": {
            "ruby": "2.4.10",
            "booted": True,
            "probes": {
                "versions": {"rails": "5.0.7.2"},
                "eager_load": {"ok": True, "autoload_only": [],
                               "undefined_outside_app": []},
                "models": {"application_record": "ApplicationRecord",
                           "application_record_abstract": True,
                           "application_record_parent": "ActiveRecord::Base",
                           "stray_base_subclasses": [],
                           # Six `belongs_to` in the repository; the one the
                           # application creates before its parent is known is
                           # `Call#location`, and it is the only optional one.
                           "belongs_to": {"Answer#call": True,
                                          "Answer#question": True,
                                          "Call#caller": True,
                                          "Call#location": False,
                                          "LocationSubscription#location": True,
                                          "LocationSubscription#subscriber": True},
                           "belongs_to_optional_count": 1},
                "framework_defaults": {"belongs_to_required_by_default": True,
                                       "not_adopted": []},
            },
        },
        "gemgraph": {
            "ruby": "2.4.10p364",
            "ruby_version_file": "2.4.10",
            "gemfile_ruby": "2.4.10",
            "ruby_declared": "2.4.10",
            "specs": {"rails": "5.0.7.2"},
        },
        "scan": {
            "pristine": "/tests/step/pristine.json",
            "missing_tests": [],
            "missing_core": [],
            "gutted_tests": [],
            "assertion_lines": 309,
            "hollow_tests": [],
            "emptied_tests": [],
            "parse_errors": [],
            "counts": {"suppression": 0, "unversioned_migration": 0,
                       "misversioned_migration": 0,
                       "before_filter": 0, "attr_accessible": 0},
            # The positive reading: all 43 say 4.2, none says anything
            # else -- and the 43 that say it are the 43 the repository
            # shipped, which is the reading the gate is on.
            "migrations": {"total": 43, "unversioned": 0,
                           "versions": {"4_2": 43},
                           "shipped_total": 43, "shipped_unversioned": 0,
                           "shipped_versions": {"4_2": 43}},
        },
        "depdrive": {"own_count": 0},
        "envboot": {"ruby": "2.4.10",
                    "declared": ["development", "production", "test"],
                    "expected": ["development", "test"],
                    "skipped": ["production"],
                    "checked": ["development", "test"],
                    "failed": [], "own_deprecation_count": 2,
                    "env_file_deprecations": []},
        "rspec": {"ran": True, "exit": 0, "db_exit": 0, "examples": 504,
                  "failures": 0, "pending": 0, "ok": True,
                  "deprecations": 1, "suite_deprecations": 0,
                  "suite_deprecation_sites": []},
        "handoff": {"database_yml_present": False,
                    "database_yml_example_present": True,
                    "db_config_problems": []},
    },
    # Step 04's readings.  The inherited readings are step 03's own recording,
    # unchanged; the readings new at this step follow from `cvgate/bootprobe.rb`
    # and from what the step requires.  `cache_files` and `cache_bytes` are not
    # here: no gate reads them, and a fixture has no business pinning a number
    # that counts what a boot wrote on this box.
    "04": {
        "behaviour": {"fraction": 1.0, "passed": 1, "total": 1},
        "bootprobe": {
            "ruby": "2.4.10",
            "booted": True,
            "probes": {
                "versions": {"rails": "5.2.8.1"},
                "framework_defaults_files": [],
                "eager_load": {"ok": True, "undefined_outside_app": []},
                "audio": {"helper_strict": True, "error_strict": True},
                "mailers": {"all_parameterized": True},
                "framework_defaults": {"belongs_to_required_by_default": True,
                                       "not_adopted": []},
                "models": {"application_record": "ApplicationRecord",
                           "stray_base_subclasses": [],
                           "belongs_to": {"Call#location": False},
                           "belongs_to_optional_count": 1,
                           "string_conditions": []},
                # 5.2 is the first release that records what it was asked for:
                # `load_defaults` sets `@loaded_config_version` on its way out,
                # and nothing else does, so this is the declaration read off the
                # booted application rather than off the parsed source.
                "load_defaults": {"loaded_config_version": "5.2"},
                # The six switches `load_defaults 5.2` throws.  Five are gated
                # as a set through `not_adopted`, which is the list of the ones
                # that are NOT at their 5.2 value, so the evidence names them;
                # the sixth, `form_with_generates_ids`, is gated on its own
                # because it is the one with a visible consequence here -- both
                # `form_with` forms gain an `id` on every field.
                #
                # `hash_digest_class` is a class name and not a boolean because
                # `use_sha1_digests` has no reader: the switch assigns
                # `ActiveSupport::Digest.hash_digest_class`, and what that
                # delegates to is the only thing there is to read.
                "defaults_5_2": {"cache_versioning": True,
                                 "use_authenticated_cookie_encryption": True,
                                 "use_authenticated_message_encryption": True,
                                 "hash_digest_class": "Digest::SHA1",
                                 "default_protect_from_forgery": True,
                                 "form_with_generates_ids": True,
                                 "not_adopted": [],
                                 "adopted": 5},
                # Where the signing key comes from, asked of three stores at
                # once.  After this step the answer is the credentials file:
                # `effective_from_credentials` decrypts `credentials.yml.enc`
                # with the tree's own `master.key`, independently of the booted
                # application, and compares the value with
                # `Rails.application.secret_key_base` -- which on 5.2 is
                # `ENV || credentials || secrets`, so it is the only reader that
                # says which store is in force.
                #
                # 5.1's store is gone, and the three readings that say so are
                # all here: no `secrets.yml.enc` (`encrypted_present`), no key
                # for it (`encrypted_key_file_present`), and
                # `read_encrypted_secrets` back at its 5.2 default of `false`.
                # `false` and not `nil`: the reader still exists on 5.2, so
                # `nil` would mean "this release cannot be asked" and only
                # `false` means "it was asked and the answer is no".
                #
                # `secrets_yml_present` is `false` at a path -- the framework
                # still resolves `config/secrets.yml` and nothing is there --
                # and `from_secrets_yml` is `false` because with no file and no
                # initializer assigning it, `secrets.secret_key_base` falls
                # back to `config.secret_key_base`, which is nil.  Step 03 had
                # both of those true of the encrypted file; this is what the
                # move away from that store looks like from inside the probe.
                #
                # 128 characters twice: the key did not change at this step,
                # only the store it lives in, and `effective_key_length`
                # agreeing with `credentials_length` is the arithmetic of
                # `effective_from_credentials` shown rather than asserted.
                "secrets": {"effective_from_credentials": True,
                            "credentials_template": False,
                            "credentials_length": 128,
                            "effective_key_length": 128,
                            "credentials_file_present": True,
                            "credentials_present": True,
                            "credentials_key_file_present": True,
                            "credentials_key_from_env": False,
                            "legacy_secret_store_files": [],
                            "read_encrypted_secrets": False,
                            "encrypted_present": False,
                            "encrypted_key_file_present": False,
                            "encrypted_length": 0,
                            "effective_from_encrypted": False,
                            "plaintext_present": False,
                            "plaintext_secret_files": [],
                            "reader_exists": True,
                            "secrets_yml_present": False,
                            "secrets_yml_path": "/app/config/secrets.yml",
                            "from_secrets_yml": False,
                            "secrets_yml_length": 0,
                            "assigned_length": 0,
                            "yml_file_length": 0,
                            "effective_from_secrets_yml": False},
                # Three states, three readings.  `defined` says something
                # required the gem; `setup_called` reads
                # `Bootsnap::LoadPathCache.load_path_cache`, which is nil until
                # `Bootsnap.setup` runs, so it tells a `require "bootsnap"`
                # from a `require "bootsnap/setup"`; `cache_files` says it
                # actually wrote something.
                #
                # `setup_from_boot_file` is the conjunction the gate reads:
                # the `Cache` exists AND `config/boot.rb` is where the setup
                # was asked for.  Position is the whole point of the gem, and
                # on this stack a `Bootsnap.setup` anywhere later does not
                # merely cache nothing useful, it does not boot at all.
                #
                # `store_path` and `store_written` are asked of bootsnap --
                # `LoadPathCache::Cache`'s `Store` carries the file it writes
                # in `@store_path` -- and reported relative to the application
                # root, so the fixture asserts a property of the tree and not
                # where the scoring container mounted it.  `store_bytes`,
                # `cache_files` and `cache_bytes` are left out of this fixture
                # on purpose: they count what a boot wrote on this box's
                # bundle, and a fixture that pinned them would be a fixture
                # that fails whenever the bundle moves.
                "bootsnap": {"defined": True,
                             "version": "1.4.9",
                             "setup_called": True,
                             "load_path_cache_class":
                                 "Bootsnap::LoadPathCache::Cache",
                             "boot_file_setup": True,
                             "setup_from_boot_file": True,
                             "store_path": "tmp/cache/bootsnap-load-path-cache",
                             "store_written": True,
                             # The gate reads this one: `store_written`
                             # conjoined with the path being inside the
                             # tree.  Stated here as well as derived so
                             # that a probe that stopped deriving it
                             # fails this fixture rather than silently
                             # failing the gate.
                             "store_in_tree": True,
                             "compile_cache_dir":
                                 "tmp/cache/bootsnap-compile-cache",
                             "in_lockfile": True},
                # The policy as the booted application holds it.  Seven
                # directives, because this application loads map tiles, audio
                # and an analytics snippet from elsewhere and writes inline
                # styles from two libraries; `report_only` is `false`, which is
                # the difference between a policy that is enforced and one that
                # only complains.  Whether it reaches a response is a different
                # question and `depdrive` answers it.
                "csp": {"reader_exists": True,
                        "configured": True,
                        "policy_class": "ActionDispatch::ContentSecurityPolicy",
                        "directives": ["default-src", "font-src", "img-src",
                                       "media-src", "object-src", "script-src",
                                       "style-src"],
                        "report_only": False,
                        "nonce_generator": True,
                        "initializer_present": True},
            },
        },
        # Driving the application: no deprecation from its own code, the policy
        # on every HTML response, and every inline script carrying a nonce the
        # same response's `script-src` names.
        "depdrive": {"own_count": 0,
                     "csp": {"problems": [], "nonce_problems": []}},
        "envboot": {"failed": [], "env_file_deprecations": []},
        "handoff": {"db_config_problems": []},
        "pkdrift": {"pinned": True},
        "rspec": {"ok": True, "examples": 504, "suite_deprecation_sites": []},
        "scan": {"counts": {"unversioned_migration": 0,
                            "misversioned_migration": 0,
                            "suppression": 0,
                            "framework_defaults_switch": 0,
                            "form_with": 2,
                            "form_for": 0,
                            "mailer_positional": 0},
                 "parse_errors": [],
                 "missing_tests": [],
                 "gutted_tests": [],
                 "missing_core": [],
                 "assertion_lines": 309,
                 "hollow_tests": [],
                 "emptied_tests": []},
    },
    "05": {
        "behaviour": {"fraction": 1.0, "passed": 1, "total": 1},
        "bootprobe": {
            # The step moves two pins, so both readings move: the interpreter
            # the verifier image runs (2.7.8, and there is only one in the
            # image) and the framework that boots.
            "ruby": "2.7.8",
            "booted": True,
            "probes": {
                "versions": {"rails": "6.0.6.1"},
                "framework_defaults_files": [],
                "eager_load": {"ok": True, "undefined_outside_app": []},
                "audio": {"helper_strict": True, "error_strict": True},
                "mailers": {"all_parameterized": True},
                "framework_defaults": {"belongs_to_required_by_default": True,
                                       "not_adopted": []},
                "models": {"application_record": "ApplicationRecord",
                           "stray_base_subclasses": [],
                           "belongs_to": {"Call#location": False},
                           "belongs_to_optional_count": 1,
                           "string_conditions": []},
                "load_defaults": {"loaded_config_version": "6.0"},
                # Step 04's six switches are still at their 5.2 values, which
                # is why they are guards here and not gates: `load_defaults
                # 6.0` is the 5.2 branch plus this step's four, so a tree that
                # got here has them already and a gate for them would be a free
                # point.
                "defaults_5_2": {"form_with_generates_ids": True,
                                 "not_adopted": []},
                # The four switches `load_defaults 6.0` adds on top of that,
                # each read where the framework really keeps it rather than off
                # `config`: `default_enforce_utf8` off
                # `ActionView::Helpers::FormTagHelper` (the Action View railtie
                # deletes the config key as it assigns it, so the obvious
                # reader answers nil on a tree that has adopted it),
                # `collection_cache_versioning` off `ActiveRecord::Base`,
                # `delivery_job` off `ActionMailer::Base`, and
                # `use_cookies_with_metadata` off `config.action_dispatch`,
                # which is the one of the four with no class-level reader on
                # 6.0.  `delivery_job` is a class name and not a boolean
                # because that is what the switch assigns.
                "defaults_6_0": {"default_enforce_utf8": False,
                                 "use_cookies_with_metadata": True,
                                 "collection_cache_versioning": True,
                                 "delivery_job":
                                     "ActionMailer::MailDeliveryJob",
                                 "not_adopted": [],
                                 "adopted": 4},
                # Which autoloader is in force, asked of the booted
                # application.  `config` is what the tree wrote and
                # `zeitwerk_enabled` is what the framework did with it; they
                # are separate readings because a tree can write the setting
                # after the autoloader has already been chosen and the
                # framework will not go back for it.  `classic_available` is
                # still true on 6.0 -- the classic autoloader is deprecated
                # here, not gone -- which is exactly why the gate reads
                # `zeitwerk_enabled` and not the presence of the constant.
                "autoloader": {"config": "zeitwerk",
                               "zeitwerk_enabled": True,
                               "classic_available": True,
                               "loaders": ["rails.main", "rails.once"]},
                # And that Zeitwerk can load the whole tree: the same walk
                # `rails zeitwerk:check` performs, taken inside this boot.
                # `ok` is only ever set when Zeitwerk is the autoloader in
                # force; on a 6.0 tree running `config.autoloader = :classic`
                # the registry is empty, the walk succeeds by having nothing to
                # do, and the probe reports the classic reading instead.
                "zeitwerk_check": {"ok": True},
                "secrets": {"effective_from_credentials": True,
                            "credentials_key_file_present": True,
                            "legacy_secret_store_files": [],
                            "read_encrypted_secrets": False,
                            "plaintext_secret_files": []},
                "bootsnap": {"setup_from_boot_file": True,
                             "store_written": True},
                "csp": {"configured": True, "nonce_generator": True},
            },
        },
        # Driving the application.  `own_count` is 0 with the host pass in it:
        # the four `Host` requests are recorded and then sliced back off the
        # shared record list, so this is the same drive steps 01-04 gate.
        #
        # The host readings are the whole of what 6.0's new middleware does on
        # this tree, and they are three separate statements: the deployment
        # name is served (200), a name the application was never deployed under
        # is refused (403), and a name that merely *ends with* the deployment
        # name is refused too (403) -- which is the one a hand-written matcher
        # gets wrong.  `loopback` is 200 and is not gated: it is what keeps the
        # recording's own requests working.
        "depdrive": {"own_count": 0,
                     "csp": {"problems": [], "nonce_problems": []},
                     "hosts": {"serves_known": True, "known": 200,
                               "blocks_unknown": True, "unknown": 403,
                               "blocks_suffix": True, "suffix": 403,
                               "loopback": 200}},
        # Two readings of the same boot, and this step gates the stricter one.
        # `env_file_deprecations` is the list blamed on a file under
        # `config/environments/`; `own_deprecation_count` is every deprecation
        # the application's own code raises in either environment, which is
        # what the factory_girl rename and `update_attributes` land in.
        # Production is booted by `depdrive`, not here, and is counted by
        # `own_deprecations`.
        "envboot": {"failed": [], "env_file_deprecations": [],
                    "own_deprecation_count": 0, "checked": ["development",
                                                            "test"]},
        # The declaration, read off the three files rather than off the running
        # process: `.ruby-version`, the Gemfile's `ruby` line and the
        # lockfile's `RUBY VERSION` stanza have to agree or the reading is
        # `null`.  That is a different statement from `bootprobe.ruby`, which
        # is the interpreter that actually ran, and on this step it is the only
        # gate that still reads on a tree whose bundle will not install.
        "gemgraph": {"ruby_declared": "2.7.8"},
        "handoff": {"db_config_problems": []},
        "pkdrift": {"pinned": True},
        "rspec": {"ok": True, "examples": 504, "suite_deprecation_sites": []},
        # 310 assertion lines and not step 04's 309: this step's one permitted
        # suite edit -- the `factory_girl` support file renamed to
        # `factory_bot` and its two constants with it -- lands one more line
        # that the counter counts.  The guard is `at_least`, so the number
        # moving up is not the failure; it is written out because a number that
        # moved without anyone noticing is.
        "scan": {"counts": {"unversioned_migration": 0,
                            "misversioned_migration": 0,
                            "suppression": 0,
                            "framework_defaults_switch": 0,
                            "form_with": 2,
                            "form_for": 0,
                            "mailer_positional": 0},
                 "parse_errors": [],
                 "missing_tests": [],
                 "gutted_tests": [],
                 "missing_core": [],
                 "assertion_lines": 310,
                 "hollow_tests": [],
                 "emptied_tests": []},
    },
    # Step 05's fixture, re-measured against this step's own golden
    # and moved on where 6.1 and Ruby 3.0 move it.  Where the kept
    # calibration is present, `fixture_drift` checks every leaf below
    # against `calibration/06-golden.json`, so an invented key fails
    # here rather than rehearsing a probe that does not exist (the
    # autoloader readings are `probes.autoloader.zeitwerk_enabled` and
    # `probes.zeitwerk_check.ok`, not `zeitwerk` and `clean`).  What
    # moved from step 05:
    #   bootprobe.ruby              2.7.8    -> 3.0.7
    #   probes.versions.rails       6.0.6.1  -> 6.1.7.10
    #   probes.load_defaults        "6.0"    -> "6.1"
    #   probes.defaults_6_1         (new: the seven switches 6.1
    #                               adds, `not_adopted` empty)
    #   depdrive.queries            (new: the two preloaded pages)
    #   scan.counts.strict_loading  absent   -> 3
    #   gemgraph.ruby_declared      2.7.8    -> 3.0.7
    # `probes.defaults_6_0` stays, and stays a guard: 6.1's branch
    # is 6.0's plus seven, so a tree that reached 6.1 by replacing
    # the 6.0 line still adopts them.  Credit for it would be credit
    # for arriving.
    "06":
        {'behaviour': {'fraction': 1.0, 'passed': 1, 'total': 1},
         'bootprobe': {'booted': True,
                       'probes': {'audio': {'error_strict': True,
                                            'helper_strict': True},
                                  'autoloader': {'config': 'zeitwerk',
                                                 'zeitwerk_enabled': True},
                                  'bootsnap': {'setup_from_boot_file': True,
                                               'store_written': True},
                                  'csp': {'configured': True,
                                          'nonce_generator': True},
                                  'defaults_5_2': {'form_with_generates_ids': True,
                                                   'not_adopted': []},
                                  'defaults_6_0': {'adopted': 4,
                                                   'collection_cache_versioning': True,
                                                   'default_enforce_utf8': False,
                                                   'delivery_job': 'ActionMailer::MailDeliveryJob',
                                                   'not_adopted': [],
                                                   'use_cookies_with_metadata': True},
                                  'defaults_6_1': {'adopted': 7,
                                                   'cookies_same_site_protection': 'lax',
                                                   'form_with_generates_remote_forms': False,
                                                   'has_many_inversing': True,
                                                   'legacy_connection_handling': False,
                                                   'not_adopted': [],
                                                   'preload_links_header': True,
                                                   'urlsafe_csrf_tokens': True,
                                                   'utc_to_local_returns_utc_offset_times': True},
                                  'eager_load': {'ok': True,
                                                 'undefined_outside_app': []},
                                  'framework_defaults': {'belongs_to_required_by_default': True,
                                                         'not_adopted': []},
                                  'framework_defaults_files': [],
                                  'host_authorization': {'count': 3,
                                                         'middleware_present': True},
                                  'load_defaults': {'loaded_config_version': '6.1'},
                                  'mailers': {'all_parameterized': True},
                                  'models': {'application_record': 'ApplicationRecord',
                                             'belongs_to': {'Call#location': False},
                                             'belongs_to_optional_count': 1,
                                             'stray_base_subclasses': [],
                                             'string_conditions': []},
                                  'secrets': {'credentials_key_file_present': True,
                                              'effective_from_credentials': True,
                                              'legacy_secret_store_files': [],
                                              'plaintext_secret_files': [],
                                              'read_encrypted_secrets': False},
                                  'versions': {'rails': '6.1.7.10'},
                                  'zeitwerk_check': {'ok': True}},
                       'ruby': '3.0.7'},
         'depdrive': {'booted': True,
                      'csp': {'nonce_problems': [], 'problems': []},
                      'hosts': {'blocks_suffix': True,
                                'blocks_unknown': True,
                                'known': 200,
                                'loopback': 200,
                                'serves_known': True,
                                'suffix': 403,
                                'unknown': 403},
                      'own_count': 0,
                      'queries': {'export_csv': {'preloaded': True,
                                                 'queries': 5,
                                                 'rows': 13,
                                                 'status': 200},
                                  'voice_answers': {'preloaded': True,
                                                    'queries': 4,
                                                    'rows': 12,
                                                    'status': 200}}},
         'envboot': {'checked': ['development', 'test'],
                     'env_file_deprecations': [],
                     'failed': [],
                     'own_deprecation_count': 0},
         'gemgraph': {'ruby_declared': '3.0.7'},
         'handoff': {'db_config_problems': []},
         'pkdrift': {'pinned': True},
         'rspec': {'deprecations': 0,
                   'examples': 504,
                   'failures': 0,
                   'ok': True,
                   'ran': True,
                   'suite_deprecation_sites': [],
                   'suite_deprecations': 0},
         'scan': {'assertion_lines': 310,
                  'counts': {'form_for': 0,
                             'form_with': 2,
                             'framework_defaults_switch': 0,
                             'mailer_positional': 0,
                             'misversioned_migration': 0,
                             'strict_loading': 3,
                             'suppression': 0,
                             'unversioned_migration': 0},
                  'emptied_tests': [],
                  'gutted_tests': [],
                  'hollow_tests': [],
                  'missing_core': [],
                  'missing_tests': [],
                  'parse_errors': []}},
    # Step 07.  Two pins again, and the first release on this ladder that
    # breaks source: `to_s(:db)` stops taking a format and the `to_s_arity`
    # reading below is 7.0 saying so.  What moved from step 06:
    #   bootprobe.ruby                3.0.7    -> 3.1.7
    #   probes.versions.rails         6.1.7.10 -> 7.0.10
    #   probes.load_defaults          "6.1"    -> "7.0"
    #   probes.defaults_7_0           (new: the fourteen switches 7.0 adds)
    #   probes.defaults_5_2           `not_adopted` stops being empty --
    #                                 7.0 moves `hash_digest_class` to SHA256
    #                                 and 5.2's branch wanted SHA1, so the
    #                                 aggregate reading step 06 guarded is one
    #                                 the golden fails.  Four individual
    #                                 readings replace it.
    #   probes.autoloader.config      "zeitwerk" -> nil: 7.0 removed the
    #                                 classic autoloader and the accessor with
    #                                 it.  `zeitwerk_enabled` is unmoved.
    #   probes.encryption             (new)
    #   probes.async_queries          (new)
    #   depdrive.encryption           (new: the five round trips)
    #   depdrive.queries.*.async_queries (new)
    #   gemgraph.ruby_declared        3.0.7    -> 3.1.7
    #   scan.counts.misversioned_migration 0 -> 1: the step ships one
    #                                 `[7.0]` migration, so "none of them says
    #                                 anything but 4.2" is a reading the golden
    #                                 fails.  `scan.migrations.shipped_versions`
    #                                 replaces it, the way step 02 reads it.
    "07":
        {'behaviour': {'fraction': 1.0, 'passed': 1, 'total': 1},
         'bootprobe': {'booted': True,
                       'probes': {'async_queries': {'available': True,
                                                    'concurrency': 4,
                                                    'executor': 'global_thread_pool'},
                                  'audio': {'error_strict': True,
                                            'helper_strict': True},
                                  'autoloader': {'config': None,
                                                 'zeitwerk_enabled': True},
                                  'bootsnap': {'setup_from_boot_file': True,
                                               'store_written': True},
                                  'csp': {'configured': True,
                                          'nonce_generator': True},
                                  'defaults_5_2': {'cache_versioning': True,
                                                   'default_protect_from_forgery': True,
                                                   'form_with_generates_ids': True,
                                                   'not_adopted': ['hash_digest_class'],
                                                   'use_authenticated_cookie_encryption': True,
                                                   'use_authenticated_message_encryption': True},
                                  'defaults_6_0': {'adopted': 4,
                                                   'collection_cache_versioning': True,
                                                   'default_enforce_utf8': False,
                                                   'delivery_job': 'ActionMailer::MailDeliveryJob',
                                                   'not_adopted': [],
                                                   'use_cookies_with_metadata': True},
                                  'defaults_6_1': {'adopted': 7,
                                                   'cookies_same_site_protection': 'lax',
                                                   'form_with_generates_remote_forms': False,
                                                   'has_many_inversing': True,
                                                   'legacy_connection_handling': False,
                                                   'not_adopted': [],
                                                   'preload_links_header': True,
                                                   'urlsafe_csrf_tokens': True,
                                                   'utc_to_local_returns_utc_offset_times': True},
                                  'defaults_7_0': {'adopted': 14,
                                                   'apply_stylesheet_media_default': False,
                                                   'automatic_scope_inversing': True,
                                                   'button_to_generates_button_tag': True,
                                                   'cache_format_version': 7.0,
                                                   'deprecated_to_s_loaded': False,
                                                   'disable_to_s_conversion': True,
                                                   'hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                   'key_generator_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                   'not_adopted': [],
                                                   'partial_inserts': False,
                                                   'raise_on_open_redirects': True,
                                                   'time_with_zone_name': 'ActiveSupport::TimeWithZone',
                                                   'to_s_arity': 0,
                                                   'use_rfc4122_namespaced_uuids': True,
                                                   'verify_foreign_keys_for_fixtures': True,
                                                   'wrap_parameters_by_default': True,
                                                   'x_xss_protection': '0'},
                                  'eager_load': {'ok': True,
                                                 'undefined_outside_app': []},
                                  'encryption': {'available': True,
                                                 'config_override_keys': [],
                                                 'extend_queries': False,
                                                 'key_source': 'credentials',
                                                 'keys_present': {'deterministic_key': True,
                                                                  'key_derivation_salt': True,
                                                                  'primary_key': True},
                                                 'support_unencrypted_data': False},
                                  'framework_defaults': {'belongs_to_required_by_default': True,
                                                         'not_adopted': []},
                                  'framework_defaults_files': [],
                                  'host_authorization': {'count': 3,
                                                         'middleware_present': True},
                                  'load_defaults': {'loaded_config_version': '7.0'},
                                  'mailers': {'all_parameterized': True},
                                  'models': {'application_record': 'ApplicationRecord',
                                             'belongs_to': {'Call#location': False},
                                             'belongs_to_optional_count': 1,
                                             'stray_base_subclasses': [],
                                             'string_conditions': []},
                                  'secrets': {'credentials_key_file_present': True,
                                              'effective_from_credentials': True,
                                              'legacy_secret_store_files': [],
                                              'plaintext_secret_files': [],
                                              'read_encrypted_secrets': False},
                                  'versions': {'rails': '7.0.10'},
                                  'zeitwerk_check': {'ok': True}},
                       'ruby': '3.1.7'},
         'depdrive': {'booted': True,
                      'csp': {'nonce_problems': [], 'problems': []},
                      'encryption': {'case_variant_finds_same_row': True,
                                     'email_limit': None,
                                     'email_sql_type': 'text',
                                     'errors': {},
                                     'find_by_answers': True,
                                     'find_by_finds': True,
                                     'find_or_create_by_reuses': True,
                                     'long_address_round_trips': True,
                                     'long_round_trip_answers': True,
                                     'no_plaintext_at_rest': True,
                                     'ran': True},
                      'hosts': {'blocks_suffix': True,
                                'blocks_unknown': True,
                                'known': 200,
                                'loopback': 200,
                                'serves_known': True,
                                'suffix': 403,
                                'unknown': 403},
                      'own_count': 0,
                      'queries': {'export_csv': {'async_queries': 3,
                                                 'preloaded': True,
                                                 'queries': 5,
                                                 'rows': 13,
                                                 'status': 200},
                                  'voice_answers': {'async_queries': 0,
                                                    'preloaded': True,
                                                    'queries': 4,
                                                    'rows': 12,
                                                    'status': 200}}},
         'envboot': {'checked': ['development', 'test'],
                     'env_file_deprecations': [],
                     'failed': [],
                     'own_deprecation_count': 0},
         'gemgraph': {'ruby_declared': '3.1.7'},
         'handoff': {'db_config_problems': []},
         'pkdrift': {'pinned': True},
         'rspec': {'deprecations': 0,
                   'examples': 504,
                   'failures': 0,
                   'ok': True,
                   'ran': True,
                   'suite_deprecation_sites': [],
                   'suite_deprecations': 0},
         'scan': {'assertion_lines': 310,
                  'counts': {'form_for': 0,
                             'form_with': 2,
                             'framework_defaults_switch': 0,
                             'mailer_positional': 0,
                             'misversioned_migration': 1,
                             'strict_loading': 3,
                             'suppression': 0,
                             'unversioned_migration': 0},
                  'emptied_tests': [],
                  'gutted_tests': [],
                  'hollow_tests': [],
                  'migrations': {'shipped_versions': {'4_2': 43}},
                  'missing_core': [],
                  'missing_tests': [],
                  'parse_errors': []}},
    # Step 08's answer, read off its golden tree the same way.  What moved
    # from 07:
    #   ruby / gemgraph.ruby_declared 3.1.7 -> 3.2.11
    #   probes.versions.rails         7.0.10 -> 7.1.6
    #   probes.load_defaults          "7.0" -> "7.1"
    #   probes.defaults_7_1           (new: the eighteen 7.1 settings, all of
    #                                 them adopted, `not_adopted` empty)
    #   probes.autoload_lib           (new: lib/ on the autoload paths, still
    #                                 on the eager-load paths, and lib/assets
    #                                 and lib/tasks ignored by the main
    #                                 autoloader)
    #   depdrive.headers              (new: `seen` is the union over nine HTML
    #                                 responses and `always` the intersection,
    #                                 so the dropped header is read off one and
    #                                 the five survivors off the other)
    #   depdrive.strict_locals        (new: the ten partials that refuse an
    #                                 undeclared local and the four that accept
    #                                 one, which are the four nothing renders)
    #   probes.defaults_6_1.not_adopted  [] -> the two switches 7.1 REMOVED.
    #                                 `legacy_connection_handling` and
    #                                 `urlsafe_csrf_tokens` are gone from the
    #                                 framework at 7.1, so the probe reads nil
    #                                 for both and reports them unadopted; the
    #                                 guard is `absent_keys` over the five that
    #                                 survive rather than `empty`, because a
    #                                 setting that no longer exists is not a
    #                                 setting this application turned off.
    #   probes.defaults_7_0.not_adopted  [] -> `cache_format_version`, which
    #                                 has moved ON to 7.1 rather than
    #                                 disappeared (it is one of the eighteen
    #                                 above), and `use_rfc4122_namespaced_uuids`,
    #                                 which 7.1 removed.  Same treatment, over
    #                                 the twelve that survive.
    "08": {'behaviour': {'fraction': 1.0, 'passed': 1, 'total': 1},
         'bootprobe': {'booted': True,
                       'probes': {'async_queries': {'available': True,
                                                    'concurrency': 4,
                                                    'executor': 'global_thread_pool'},
                                  'audio': {'error_strict': True,
                                            'helper_strict': True},
                                  'autoload_lib': {'autoload_path_entries': 1,
                                                   'eager_load_path_entries': 1,
                                                   'ignored': ['lib/assets',
                                                               'lib/tasks'],
                                                   'in_autoload_paths': True,
                                                   'in_eager_load_paths': True,
                                                   'lib': '/app/lib',
                                                   'zeitwerk_version': '2.6.18'},
                                  'autoloader': {'config': None,
                                                 'zeitwerk_enabled': True},
                                  'bootsnap': {'setup_from_boot_file': True,
                                               'store_written': True},
                                  'csp': {'configured': True,
                                          'nonce_generator': True},
                                  'defaults_5_2': {'cache_versioning': True,
                                                   'default_protect_from_forgery': True,
                                                   'form_with_generates_ids': True,
                                                   'not_adopted': ['hash_digest_class'],
                                                   'use_authenticated_cookie_encryption': True,
                                                   'use_authenticated_message_encryption': True},
                                  'defaults_6_0': {'adopted': 4,
                                                   'collection_cache_versioning': True,
                                                   'default_enforce_utf8': False,
                                                   'delivery_job': 'ActionMailer::MailDeliveryJob',
                                                   'not_adopted': [],
                                                   'use_cookies_with_metadata': True},
                                  'defaults_6_1': {'adopted': 5,
                                                   'cookies_same_site_protection': 'lax',
                                                   'form_with_generates_remote_forms': False,
                                                   'has_many_inversing': True,
                                                   'legacy_connection_handling': None,
                                                   'not_adopted': ['legacy_connection_handling',
                                                                   'urlsafe_csrf_tokens'],
                                                   'preload_links_header': True,
                                                   'urlsafe_csrf_tokens': None,
                                                   'utc_to_local_returns_utc_offset_times': True},
                                  'defaults_7_0': {'adopted': 12,
                                                   'apply_stylesheet_media_default': False,
                                                   'automatic_scope_inversing': True,
                                                   'button_to_generates_button_tag': True,
                                                   'cache_format_version': 7.1,
                                                   'deprecated_to_s_loaded': False,
                                                   'disable_to_s_conversion': None,
                                                   'hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                   'key_generator_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                   'not_adopted': ['cache_format_version',
                                                                   'use_rfc4122_namespaced_uuids'],
                                                   'partial_inserts': False,
                                                   'raise_on_open_redirects': True,
                                                   'time_with_zone_name': 'ActiveSupport::TimeWithZone',
                                                   'to_s_arity': 0,
                                                   'use_rfc4122_namespaced_uuids': None,
                                                   'verify_foreign_keys_for_fixtures': True,
                                                   'wrap_parameters_by_default': True,
                                                   'x_xss_protection': '0'},
                                  'defaults_7_1': {'ac_allow_deprecated_parameters_hash_equality': False,
                                                   'add_autoload_paths_to_load_path': False,
                                                   'adopted': 18,
                                                   'ar_allow_deprecated_singular_associations_name': False,
                                                   'ar_before_committed_on_all_records': True,
                                                   'ar_commit_transaction_on_non_local_return': True,
                                                   'ar_default_column_serializer': 'nil',
                                                   'ar_generate_secure_token_on': 'initialize',
                                                   'ar_marshalling_format_version': 7.1,
                                                   'ar_raise_on_assign_to_attr_readonly': True,
                                                   'ar_run_after_transaction_callbacks_in_order_defined': True,
                                                   'as_cache_format_version': 7.1,
                                                   'as_message_serializer': 'json_allow_marshal',
                                                   'as_raise_on_invalid_cache_expiration_time': True,
                                                   'av_sanitizer_vendor': 'Rails::HTML5::Sanitizer',
                                                   'debug_exception_log_level': 'error',
                                                   'dom_testing_default_html_version': 'html5',
                                                   'enc_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                   'not_adopted': [],
                                                   'precompile_filter_parameters': True},
                                  'eager_load': {'ok': True,
                                                 'undefined_outside_app': []},
                                  'encryption': {'available': True,
                                                 'config_override_keys': ['hash_digest_class',
                                                                          'support_sha1_for_non_deterministic_encryption'],
                                                 'extend_queries': False,
                                                 'key_source': 'credentials',
                                                 'keys_present': {'deterministic_key': True,
                                                                  'key_derivation_salt': True,
                                                                  'primary_key': True},
                                                 'support_unencrypted_data': False},
                                  'framework_defaults': {'belongs_to_required_by_default': True,
                                                         'not_adopted': []},
                                  'framework_defaults_files': [],
                                  'host_authorization': {'count': 3,
                                                         'middleware_present': True},
                                  'load_defaults': {'loaded_config_version': '7.1'},
                                  'mailers': {'all_parameterized': True},
                                  'models': {'application_record': 'ApplicationRecord',
                                             'belongs_to': {'Call#location': False},
                                             'belongs_to_optional_count': 1,
                                             'stray_base_subclasses': [],
                                             'string_conditions': []},
                                  'secrets': {'credentials_key_file_present': True,
                                              'effective_from_credentials': True,
                                              'legacy_secret_store_files': [],
                                              'plaintext_secret_files': [],
                                              'read_encrypted_secrets': False},
                                  'versions': {'rails': '7.1.6'},
                                  'zeitwerk_check': {'ok': True}},
                       'ruby': '3.2.11'},
         'depdrive': {'booted': True,
                      'csp': {'nonce_problems': [], 'problems': []},
                      'encryption': {'case_variant_finds_same_row': True,
                                     'email_limit': None,
                                     'email_sql_type': 'text',
                                     'errors': {},
                                     'find_by_answers': True,
                                     'find_by_finds': True,
                                     'find_or_create_by_reuses': True,
                                     'long_address_round_trips': True,
                                     'long_round_trip_answers': True,
                                     'no_plaintext_at_rest': True,
                                     'ran': True},
                      'headers': {'always': {'Referrer-Policy': 'strict-origin-when-cross-origin',
                                             'X-Content-Type-Options': 'nosniff',
                                             'X-Frame-Options': 'SAMEORIGIN',
                                             'X-Permitted-Cross-Domain-Policies': 'none',
                                             'X-XSS-Protection': '0'},
                                  'html_responses': 8,
                                  'seen': ['Referrer-Policy',
                                           'X-Content-Type-Options',
                                           'X-Frame-Options',
                                           'X-Permitted-Cross-Domain-Policies',
                                           'X-XSS-Protection'],
                                  'sometimes': {}},
                      'hosts': {'blocks_suffix': True,
                                'blocks_unknown': True,
                                'known': 200,
                                'loopback': 200,
                                'serves_known': True,
                                'suffix': 403,
                                'unknown': 403},
                      'own_count': 0,
                      'queries': {'export_csv': {'async_queries': 3,
                                                 'preloaded': True,
                                                 'queries': 5,
                                                 'rows': 13,
                                                 'status': 200},
                                  'voice_answers': {'async_queries': 0,
                                                    'preloaded': True,
                                                    'queries': 4,
                                                    'rows': 12,
                                                    'status': 200}},
                      'strict_locals': {'accepts_unknown': ['locations/subscribe_form',
                                                            'numerical_answers/vote',
                                                            'numerical_answers/vote_meter',
                                                            'voice_answers/voice_answers'],
                                        'not_strict': ['locations/subscribe_form',
                                                       'numerical_answers/vote',
                                                       'numerical_answers/vote_meter',
                                                       'voice_answers/voice_answers'],
                                        'probed': 14,
                                        'refuses_unknown': ['application/feedback',
                                                            'application/footer',
                                                            'application/header',
                                                            'application/map',
                                                            'landing/answer',
                                                            'landing/info_block',
                                                            'locations/email_notifications',
                                                            'locations/numerical_response',
                                                            'locations/voice_message',
                                                            'voice_answers/answer'],
                                        'strict': ['application/feedback',
                                                   'application/footer',
                                                   'application/header',
                                                   'application/map',
                                                   'landing/answer',
                                                   'landing/info_block',
                                                   'locations/email_notifications',
                                                   'locations/numerical_response',
                                                   'locations/voice_message',
                                                   'voice_answers/answer']}},
         'envboot': {'checked': ['development', 'test'],
                     'env_file_deprecations': [],
                     'failed': [],
                     'own_deprecation_count': 0},
         'gemgraph': {'ruby_declared': '3.2.11',
                      'specs': {'sass': '3.4.25'}},
         'handoff': {'db_config_problems': []},
         'pkdrift': {'pinned': True},
         'rspec': {'deprecations': 0,
                   'examples': 504,
                   'failures': 0,
                   # rspec-expectations' once-per-run warning about the
                   # suite's own `.should` call sites: counted, named, and
                   # kept out of the tree's own total.  Nothing on this ladder
                   # asks anyone to rewrite those 22 lines.
                   'inherited_rspec_deprecations': 1,
                   'ok': True,
                   'own_rspec_deprecations': 0,
                   'ran': True,
                   'suite_deprecation_sites': [],
                   'suite_deprecations': 0},
         'scan': {'assertion_lines': 310,
                  'counts': {'exists_predicate': 0,
                             'form_for': 0,
                             'form_with': 2,
                             'framework_defaults_switch': 0,
                             'mailer_positional': 0,
                             'misversioned_migration': 1,
                             'strict_loading': 3,
                             'suppression': 0,
                             'unversioned_migration': 0},
                  'emptied_tests': [],
                  'gutted_tests': [],
                  'hollow_tests': [],
                  'migrations': {'shipped_versions': {'4_2': 43}},
                  'missing_core': [],
                  'missing_tests': [],
                  'parse_errors': []}}
,
    "09": {'behaviour': {'fraction': 1.0, 'passed': 1, 'total': 1},
         'bootprobe': {'booted': True,
                       'probes': {'async_queries': {'available': True,
                                                    'concurrency': 4,
                                                    'executor': 'global_thread_pool'},
                                  'audio': {'error_strict': True,
                                            'helper_strict': True},
                                  'autoload_lib': {'autoload_path_entries': 1,
                                                   'eager_load_path_entries': 1,
                                                   'ignored': ['lib/assets',
                                                               'lib/tasks'],
                                                   'in_autoload_paths': True,
                                                   'in_eager_load_paths': True,
                                                   'lib': '/app/lib',
                                                   'zeitwerk_version': '2.6.18'},
                                  'autoloader': {'config': None,
                                                 'zeitwerk_enabled': True},
                                  'bootsnap': {'setup_from_boot_file': True,
                                               'store_written': True},
                                  'csp': {'configured': True,
                                          'nonce_generator': True},
                                  'database': {'untyped_date_class': 'Date'},
                                  'defaults_5_2': {'cache_versioning': True,
                                                   'default_protect_from_forgery': True,
                                                   'form_with_generates_ids': True,
                                                   'not_adopted': ['hash_digest_class'],
                                                   'use_authenticated_cookie_encryption': True,
                                                   'use_authenticated_message_encryption': True},
                                  'defaults_6_0': {'adopted': 4,
                                                   'collection_cache_versioning': True,
                                                   'default_enforce_utf8': False,
                                                   'delivery_job': 'ActionMailer::MailDeliveryJob',
                                                   'not_adopted': [],
                                                   'use_cookies_with_metadata': True},
                                  'defaults_6_1': {'adopted': 5,
                                                   'cookies_same_site_protection': 'lax',
                                                   'form_with_generates_remote_forms': False,
                                                   'has_many_inversing': True,
                                                   'legacy_connection_handling': None,
                                                   'not_adopted': ['legacy_connection_handling',
                                                                   'urlsafe_csrf_tokens'],
                                                   'preload_links_header': True,
                                                   'urlsafe_csrf_tokens': None,
                                                   'utc_to_local_returns_utc_offset_times': True},
                                  'defaults_7_0': {'adopted': 12,
                                                   'apply_stylesheet_media_default': False,
                                                   'automatic_scope_inversing': True,
                                                   'button_to_generates_button_tag': True,
                                                   'cache_format_version': 7.1,
                                                   'deprecated_to_s_loaded': False,
                                                   'disable_to_s_conversion': None,
                                                   'hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                   'key_generator_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                   'not_adopted': ['cache_format_version',
                                                                   'use_rfc4122_namespaced_uuids'],
                                                   'partial_inserts': False,
                                                   'raise_on_open_redirects': True,
                                                   'time_with_zone_name': 'ActiveSupport::TimeWithZone',
                                                   'to_s_arity': 0,
                                                   'use_rfc4122_namespaced_uuids': None,
                                                   'verify_foreign_keys_for_fixtures': True,
                                                   'wrap_parameters_by_default': True,
                                                   'x_xss_protection': '0'},
                                  'defaults_7_1': {'add_autoload_paths_to_load_path': False,
                                                   'adopted': 15,
                                                   'ar_before_committed_on_all_records': True,
                                                   'ar_default_column_serializer': 'nil',
                                                   'ar_generate_secure_token_on': 'initialize',
                                                   'ar_marshalling_format_version': 7.1,
                                                   'ar_raise_on_assign_to_attr_readonly': True,
                                                   'ar_run_after_transaction_callbacks_in_order_defined': True,
                                                   'as_cache_format_version': 7.1,
                                                   'as_message_serializer': 'json_allow_marshal',
                                                   'as_raise_on_invalid_cache_expiration_time': True,
                                                   'av_sanitizer_vendor': 'Rails::HTML5::Sanitizer',
                                                   'debug_exception_log_level': 'error',
                                                   'dom_testing_default_html_version': 'html5',
                                                   'enc_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                   'not_adopted': ['ac_allow_deprecated_parameters_hash_equality',
                                                                   'ar_allow_deprecated_singular_associations_name',
                                                                   'ar_commit_transaction_on_non_local_return'],
                                                   'precompile_filter_parameters': True},
                                  'defaults_7_2': {'adopted': 3,
                                                   'aj_enqueue_after_transaction_commit': 'default',
                                                   'ar_postgresql_adapter_decode_dates': True,
                                                   'ar_validate_migration_timestamps': True,
                                                   'not_adopted': [],
                                                   'yjit': True,
                                                   'yjit_runtime_available': False},
                                  'eager_load': {'ok': True,
                                                 'undefined_outside_app': []},
                                  'encryption': {'available': True,
                                                 'config_override_keys': ['hash_digest_class',
                                                                          'support_sha1_for_non_deterministic_encryption'],
                                                 'extend_queries': False,
                                                 'key_source': 'credentials',
                                                 'keys_present': {'deterministic_key': True,
                                                                  'key_derivation_salt': True,
                                                                  'primary_key': True},
                                                 'support_unencrypted_data': False},
                                  'fork_hooks': {'after_fork': 'ok',
                                                 'after_fork_permanent_lease': True,
                                                 'before_fork': 'ok',
                                                 'before_fork_permanent_lease': True,
                                                 'before_fork_status': 'ok',
                                                 'checkout_flag_supported': True,
                                                 'config_file': 'config/unicorn.rb',
                                                 'hooks': ['after_fork',
                                                           'before_fork'],
                                                 'pool_connections_after': 0,
                                                 'pool_connections_before': 1,
                                                 'pool_leased_after': 0,
                                                 'pool_leased_before': 1},
                                  'framework_defaults': {'belongs_to_required_by_default': True,
                                                         'not_adopted': []},
                                  'framework_defaults_files': [],
                                  'host_authorization': {'count': 3,
                                                         'middleware_present': True},
                                  'load_defaults': {'loaded_config_version': '7.2'},
                                  'mailers': {'all_parameterized': True},
                                  'models': {'application_record': 'ApplicationRecord',
                                             'belongs_to': {'Call#location': False},
                                             'belongs_to_optional_count': 1,
                                             'stray_base_subclasses': [],
                                             'string_conditions': []},
                                  'secrets': {'credentials_key_file_present': True,
                                              'effective_from_credentials': True,
                                              'legacy_secret_store_files': [],
                                              'plaintext_secret_files': [],
                                              'read_encrypted_secrets': 'unsupported'},
                                  'versions': {'rails': '7.2.3.2'},
                                  'zeitwerk_check': {'ok': True}},
                       'ruby': '3.2.11'},
         'depdrive': {'booted': True,
                      'csp': {'nonce_problems': [], 'problems': []},
                      'encryption': {'case_variant_finds_same_row': True,
                                     'email_limit': None,
                                     'email_sql_type': 'text',
                                     'errors': {},
                                     'find_by_answers': True,
                                     'find_by_finds': True,
                                     'find_or_create_by_reuses': True,
                                     'long_address_round_trips': True,
                                     'long_round_trip_answers': True,
                                     'no_plaintext_at_rest': True,
                                     'ran': True},
                      'headers': {'always': {'Referrer-Policy': 'strict-origin-when-cross-origin',
                                             'X-Content-Type-Options': 'nosniff',
                                             'X-Frame-Options': 'SAMEORIGIN',
                                             'X-Permitted-Cross-Domain-Policies': 'none',
                                             'X-XSS-Protection': '0'},
                                  'html_responses': 8,
                                  'seen': ['Referrer-Policy',
                                           'X-Content-Type-Options',
                                           'X-Frame-Options',
                                           'X-Permitted-Cross-Domain-Policies',
                                           'X-XSS-Protection'],
                                  'sometimes': {}},
                      'hosts': {'blocks_suffix': True,
                                'blocks_unknown': True,
                                'known': 200,
                                'loopback': 200,
                                'serves_known': True,
                                'suffix': 403,
                                'unknown': 403},
                      'own_count': 0,
                      'queries': {'export_csv': {'async_queries': 3,
                                                 'preloaded': True,
                                                 'queries': 5,
                                                 'rows': 13,
                                                 'status': 200},
                                  'voice_answers': {'async_queries': 0,
                                                    'preloaded': True,
                                                    'queries': 4,
                                                    'rows': 12,
                                                    'status': 200}},
                      'strict_locals': {'accepts_unknown': ['locations/subscribe_form',
                                                            'numerical_answers/vote',
                                                            'numerical_answers/vote_meter',
                                                            'voice_answers/voice_answers'],
                                        'not_strict': ['locations/subscribe_form',
                                                       'numerical_answers/vote',
                                                       'numerical_answers/vote_meter',
                                                       'voice_answers/voice_answers'],
                                        'probed': 14,
                                        'refuses_unknown': ['application/feedback',
                                                            'application/footer',
                                                            'application/header',
                                                            'application/map',
                                                            'landing/answer',
                                                            'landing/info_block',
                                                            'locations/email_notifications',
                                                            'locations/numerical_response',
                                                            'locations/voice_message',
                                                            'voice_answers/answer'],
                                        'strict': ['application/feedback',
                                                   'application/footer',
                                                   'application/header',
                                                   'application/map',
                                                   'landing/answer',
                                                   'landing/info_block',
                                                   'locations/email_notifications',
                                                   'locations/numerical_response',
                                                   'locations/voice_message',
                                                   'voice_answers/answer']}},
         'envboot': {'checked': ['development', 'test'],
                     'env_file_deprecations': [],
                     'envs': {'test': {'exceptions_let_out': True,
                                       'exceptions_shown': False,
                                       'exceptions_shown_rescuable': False}},
                     'failed': [],
                     'own_deprecation_count': 0},
         'gemgraph': {'ruby_declared': '3.2.11',
                      'specs': {'sass': '3.4.25'}},
         'handoff': {'db_config_problems': []},
         'pkdrift': {'pinned': True},
         'rspec': {'deprecations': 0,
                   'examples': 504,
                   'failures': 0,
                   'inherited_rspec_deprecations': 1,
                   'ok': True,
                   'own_rspec_deprecations': 0,
                   'ran': True,
                   'suite_deprecation_sites': [],
                   'suite_deprecations': 0},
         'scan': {'assertion_lines': 310,
                  'counts': {'ar_base_connection': 0,
                             'exists_predicate': 0,
                             'form_for': 0,
                             'form_with': 2,
                             'framework_defaults_switch': 0,
                             'mailer_positional': 0,
                             'misversioned_migration': 1,
                             'strict_loading': 3,
                             'suppression': 0,
                             'unversioned_migration': 0},
                  'emptied_tests': [],
                  'gutted_tests': [],
                  'hollow_tests': [],
                  'migrations': {'shipped_versions': {'4_2': 43}},
                  'missing_core': [],
                  'missing_tests': [],
                  'parse_errors': []}},
    # Step 10, hand-written as step 09's fixture plus the six readings the
    # release moves and the two the step adds.  Every one of those eight was
    # measured before it was written here, not copied out of a verifier run:
    # a real `bootprobe.rb` run on the tree this step's solution produces, the
    # same tree declaring 7.2 instead, and `raketasks.rb` on the migrated tree
    # and on step 09's golden, the tree this step is handed.
    #
    #   versions.rails                 '7.2.3.2' -> '8.0.5.1'
    #   load_defaults.loaded_...       '7.2'     -> '8.0'
    #   defaults_8_0                   new probe, three readings, all adopted
    #   framework_defaults.not_...     []        -> ['to_time_preserves_timezone']
    #   defaults_7_2.aj_enqueue_...    'default' -> 'false'  (adopted 3 -> 2)
    #   secrets.read_encrypted_...     'unsupported' -> None
    #   rake                           new instrument
    #   scan.counts.annotate_legacy_api  new rule
    #
    # The two `not_adopted` lists that GREW are the release's doing and not the
    # tree's, which is why the step demotes and narrows those two readings
    # instead of asking for a value to be set by hand: 8.0 answers
    # `to_time_preserves_timezone` with a symbol where 7.2 answered `true`, and
    # railties 8.0's own `when "7.2"` branch no longer assigns
    # `enqueue_after_transaction_commit` at all.
    #
    # `rake.task_count` is deliberately NOT stated.  It is 47 on this tree and
    # 50 on the one the step is handed, and the difference is three described
    # tasks the retired gem contributed -- a fact about which gems describe
    # tasks, not a property of the migration.  `ok` is the property; the count
    # is evidence beside it and a candidate who writes the rake hook by hand
    # can land on a different number.
    "10": {'behaviour': {'fraction': 1.0, 'passed': 1, 'total': 1},
            'bootprobe': {'booted': True,
                          'probes': {'async_queries': {'available': True,
                                                       'concurrency': 4,
                                                       'executor': 'global_thread_pool'},
                                     'audio': {'error_strict': True,
                                               'helper_strict': True},
                                     'autoload_lib': {'autoload_path_entries': 1,
                                                      'eager_load_path_entries': 1,
                                                      'ignored': ['lib/assets',
                                                                  'lib/tasks'],
                                                      'in_autoload_paths': True,
                                                      'in_eager_load_paths': True,
                                                      'lib': '/app/lib',
                                                      'zeitwerk_version': '2.6.18'},
                                     'autoloader': {'config': None,
                                                    'zeitwerk_enabled': True},
                                     'bootsnap': {'setup_from_boot_file': True,
                                                  'store_written': True},
                                     'csp': {'configured': True,
                                             'nonce_generator': True},
                                     'database': {'untyped_date_class': 'Date'},
                                     'defaults_5_2': {'cache_versioning': True,
                                                      'default_protect_from_forgery': True,
                                                      'form_with_generates_ids': True,
                                                      'not_adopted': ['hash_digest_class'],
                                                      'use_authenticated_cookie_encryption': True,
                                                      'use_authenticated_message_encryption': True},
                                     'defaults_6_0': {'adopted': 4,
                                                      'collection_cache_versioning': True,
                                                      'default_enforce_utf8': False,
                                                      'delivery_job': 'ActionMailer::MailDeliveryJob',
                                                      'not_adopted': [],
                                                      'use_cookies_with_metadata': True},
                                     'defaults_6_1': {'adopted': 5,
                                                      'cookies_same_site_protection': 'lax',
                                                      'form_with_generates_remote_forms': False,
                                                      'has_many_inversing': True,
                                                      'legacy_connection_handling': None,
                                                      'not_adopted': ['legacy_connection_handling',
                                                                      'urlsafe_csrf_tokens'],
                                                      'preload_links_header': True,
                                                      'urlsafe_csrf_tokens': None,
                                                      'utc_to_local_returns_utc_offset_times': True},
                                     'defaults_7_0': {'adopted': 12,
                                                      'apply_stylesheet_media_default': False,
                                                      'automatic_scope_inversing': True,
                                                      'button_to_generates_button_tag': True,
                                                      'cache_format_version': 7.1,
                                                      'deprecated_to_s_loaded': False,
                                                      'disable_to_s_conversion': None,
                                                      'hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                      'key_generator_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                      'not_adopted': ['cache_format_version',
                                                                      'use_rfc4122_namespaced_uuids'],
                                                      'partial_inserts': False,
                                                      'raise_on_open_redirects': True,
                                                      'time_with_zone_name': 'ActiveSupport::TimeWithZone',
                                                      'to_s_arity': 0,
                                                      'use_rfc4122_namespaced_uuids': None,
                                                      'verify_foreign_keys_for_fixtures': True,
                                                      'wrap_parameters_by_default': True,
                                                      'x_xss_protection': '0'},
                                     'defaults_7_1': {'add_autoload_paths_to_load_path': False,
                                                      'adopted': 15,
                                                      'ar_before_committed_on_all_records': True,
                                                      'ar_default_column_serializer': 'nil',
                                                      'ar_generate_secure_token_on': 'initialize',
                                                      'ar_marshalling_format_version': 7.1,
                                                      'ar_raise_on_assign_to_attr_readonly': True,
                                                      'ar_run_after_transaction_callbacks_in_order_defined': True,
                                                      'as_cache_format_version': 7.1,
                                                      'as_message_serializer': 'json_allow_marshal',
                                                      'as_raise_on_invalid_cache_expiration_time': True,
                                                      'av_sanitizer_vendor': 'Rails::HTML5::Sanitizer',
                                                      'debug_exception_log_level': 'error',
                                                      'dom_testing_default_html_version': 'html5',
                                                      'enc_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                      'not_adopted': ['ac_allow_deprecated_parameters_hash_equality',
                                                                      'ar_allow_deprecated_singular_associations_name',
                                                                      'ar_commit_transaction_on_non_local_return'],
                                                      'precompile_filter_parameters': True},
                                     'defaults_7_2': {'adopted': 2,
                                                      'aj_enqueue_after_transaction_commit': 'false',
                                                      'ar_postgresql_adapter_decode_dates': True,
                                                      'ar_validate_migration_timestamps': True,
                                                      'not_adopted': ['aj_enqueue_after_transaction_commit'],
                                                      'yjit': True,
                                                      'yjit_runtime_available': False},
                                     'defaults_8_0': {'ad_strict_freshness': True,
                                                      'adopted': 3,
                                                      'as_to_time_preserves_timezone': 'zone',
                                                      'not_adopted': [],
                                                      'regexp_timeout': 1.0},
                                     'eager_load': {'ok': True,
                                                    'undefined_outside_app': []},
                                     'encryption': {'available': True,
                                                    'config_override_keys': ['hash_digest_class',
                                                                             'support_sha1_for_non_deterministic_encryption'],
                                                    'extend_queries': False,
                                                    'key_source': 'credentials',
                                                    'keys_present': {'deterministic_key': True,
                                                                     'key_derivation_salt': True,
                                                                     'primary_key': True},
                                                    'support_unencrypted_data': False},
                                     'fork_hooks': {'after_fork': 'ok',
                                                    'after_fork_permanent_lease': True,
                                                    'before_fork': 'ok',
                                                    'before_fork_permanent_lease': True,
                                                    'before_fork_status': 'ok',
                                                    'checkout_flag_supported': True,
                                                    'config_file': 'config/unicorn.rb',
                                                    'hooks': ['after_fork',
                                                              'before_fork'],
                                                    'pool_connections_after': 0,
                                                    'pool_connections_before': 1,
                                                    'pool_leased_after': 0,
                                                    'pool_leased_before': 1},
                                     'framework_defaults': {'belongs_to_required_by_default': True,
                                                            'not_adopted': ['to_time_preserves_timezone']},
                                     'framework_defaults_files': [],
                                     'host_authorization': {'count': 3,
                                                            'middleware_present': True},
                                     'load_defaults': {'loaded_config_version': '8.0'},
                                     'mailers': {'all_parameterized': True},
                                     'models': {'application_record': 'ApplicationRecord',
                                                'belongs_to': {'Call#location': False},
                                                'belongs_to_optional_count': 1,
                                                'stray_base_subclasses': [],
                                                'string_conditions': []},
                                     'secrets': {'credentials_key_file_present': True,
                                                 'effective_from_credentials': True,
                                                 'legacy_secret_store_files': [],
                                                 'plaintext_secret_files': [],
                                                 'read_encrypted_secrets': None},
                                     'versions': {'rails': '8.0.5.1'},
                                     'zeitwerk_check': {'ok': True}},
                          'ruby': '3.2.11'},
            'depdrive': {'booted': True,
                         'csp': {'nonce_problems': [], 'problems': []},
                         'encryption': {'case_variant_finds_same_row': True,
                                        'email_limit': None,
                                        'email_sql_type': 'text',
                                        'errors': {},
                                        'find_by_answers': True,
                                        'find_by_finds': True,
                                        'find_or_create_by_reuses': True,
                                        'long_address_round_trips': True,
                                        'long_round_trip_answers': True,
                                        'no_plaintext_at_rest': True,
                                        'ran': True},
                         'headers': {'always': {'Referrer-Policy': 'strict-origin-when-cross-origin',
                                                'X-Content-Type-Options': 'nosniff',
                                                'X-Frame-Options': 'SAMEORIGIN',
                                                'X-Permitted-Cross-Domain-Policies': 'none',
                                                'X-XSS-Protection': '0'},
                                     'html_responses': 8,
                                     'seen': ['Referrer-Policy',
                                              'X-Content-Type-Options',
                                              'X-Frame-Options',
                                              'X-Permitted-Cross-Domain-Policies',
                                              'X-XSS-Protection'],
                                     'sometimes': {}},
                         'hosts': {'blocks_suffix': True,
                                   'blocks_unknown': True,
                                   'known': 200,
                                   'loopback': 200,
                                   'serves_known': True,
                                   'suffix': 403,
                                   'unknown': 403},
                         'own_count': 0,
                         'queries': {'export_csv': {'async_queries': 3,
                                                    'preloaded': True,
                                                    'queries': 5,
                                                    'rows': 13,
                                                    'status': 200},
                                     'voice_answers': {'async_queries': 0,
                                                       'preloaded': True,
                                                       'queries': 4,
                                                       'rows': 12,
                                                       'status': 200}},
                         'strict_locals': {'accepts_unknown': ['locations/subscribe_form',
                                                               'numerical_answers/vote',
                                                               'numerical_answers/vote_meter',
                                                               'voice_answers/voice_answers'],
                                           'not_strict': ['locations/subscribe_form',
                                                          'numerical_answers/vote',
                                                          'numerical_answers/vote_meter',
                                                          'voice_answers/voice_answers'],
                                           'probed': 14,
                                           'refuses_unknown': ['application/feedback',
                                                               'application/footer',
                                                               'application/header',
                                                               'application/map',
                                                               'landing/answer',
                                                               'landing/info_block',
                                                               'locations/email_notifications',
                                                               'locations/numerical_response',
                                                               'locations/voice_message',
                                                               'voice_answers/answer'],
                                           'strict': ['application/feedback',
                                                      'application/footer',
                                                      'application/header',
                                                      'application/map',
                                                      'landing/answer',
                                                      'landing/info_block',
                                                      'locations/email_notifications',
                                                      'locations/numerical_response',
                                                      'locations/voice_message',
                                                      'voice_answers/answer']}},
            'envboot': {'checked': ['development', 'test'],
                        'env_file_deprecations': [],
                        'envs': {'test': {'exceptions_let_out': True,
                                          'exceptions_shown': False,
                                          'exceptions_shown_rescuable': False}},
                        'failed': [],
                        'own_deprecation_count': 0},
            'gemgraph': {'ruby_declared': '3.2.11',
                         'specs': {'annotaterb': '4.25.0', 'sass': '3.4.25'}},
            'handoff': {'db_config_problems': []},
            'pkdrift': {'pinned': True},
            'rake': {'annotate_tasks': 0,
                     'exit_status': 0,
                     'has_db_migrate': True,
                     'ok': True},
            'rspec': {'deprecations': 0,
                      'examples': 504,
                      'failures': 0,
                      'inherited_rspec_deprecations': 1,
                      'ok': True,
                      'own_rspec_deprecations': 0,
                      'ran': True,
                      'suite_deprecation_sites': [],
                      'suite_deprecations': 0},
            'scan': {'annotations': {'columns': 46,
                                     'count': 7,
                                     'digest': 'c5dd17db4d81',
                                     'lines': 81,
                                     'models': {
                                         'app/models/answer.rb':
                                             {'columns': 7, 'digest': '66e298d2f867', 'lines': 12},
                                         'app/models/call.rb':
                                             {'columns': 7, 'digest': 'ee8f1d59692c', 'lines': 12},
                                         'app/models/caller.rb':
                                             {'columns': 4, 'digest': '859ab859230f', 'lines': 9},
                                         'app/models/location.rb':
                                             {'columns': 8, 'digest': '837c4a5cfa36', 'lines': 13},
                                         'app/models/location_subscription.rb':
                                             {'columns': 10, 'digest': '597a33c539c2', 'lines': 15},
                                         'app/models/question.rb':
                                             {'columns': 6, 'digest': '4f79be7d68ca', 'lines': 11},
                                         'app/models/subscriber.rb':
                                             {'columns': 4, 'digest': '202c1f57622c', 'lines': 9},
                                     },
                                     },
                     'assertion_lines': 310,
                     'counts': {'annotate_legacy_api': 0,
                                'ar_base_connection': 0,
                                'exists_predicate': 0,
                                'form_for': 0,
                                'form_with': 2,
                                'framework_defaults_switch': 0,
                                'mailer_positional': 0,
                                'misversioned_migration': 1,
                                'strict_loading': 3,
                                'suppression': 0,
                                'unversioned_migration': 0},
                     'emptied_tests': [],
                     'gutted_tests': [],
                     'hollow_tests': [],
                     'migrations': {'shipped_versions': {'4_2': 43}},
                     'missing_core': [],
                     'missing_tests': [],
                     'parse_errors': []}},
    "11": {'behaviour': {'fraction': 1.0, 'passed': 1, 'total': 1},
             'bootprobe': {'booted': True,
                           'probes': {'async_queries': {'available': True,
                                                        'concurrency': 4,
                                                        'executor': 'global_thread_pool'},
                                      'audio': {'error_strict': True,
                                                'helper_strict': True},
                                      'autoload_lib': {'autoload_path_entries': 1,
                                                       'eager_load_path_entries': 1,
                                                       'ignored': ['lib/assets',
                                                                   'lib/tasks'],
                                                       'in_autoload_paths': True,
                                                       'in_eager_load_paths': True,
                                                       'lib': '/app/lib',
                                                       'zeitwerk_version': '2.6.18'},
                                      'autoloader': {'config': None,
                                                     'zeitwerk_enabled': True},
                                      'bootsnap': {'setup_from_boot_file': True,
                                                   'store_written': True},
                                      'csp': {'configured': True,
                                              'nonce_generator': True},
                                      'database': {'untyped_date_class': 'Date'},
                                      'defaults_5_2': {'cache_versioning': True,
                                                       'default_protect_from_forgery': True,
                                                       'form_with_generates_ids': True,
                                                       'not_adopted': ['hash_digest_class'],
                                                       'use_authenticated_cookie_encryption': True,
                                                       'use_authenticated_message_encryption': True},
                                      'defaults_6_0': {'adopted': 4,
                                                       'collection_cache_versioning': True,
                                                       'default_enforce_utf8': False,
                                                       'delivery_job': 'ActionMailer::MailDeliveryJob',
                                                       'not_adopted': [],
                                                       'use_cookies_with_metadata': True},
                                      'defaults_6_1': {'adopted': 5,
                                                       'cookies_same_site_protection': 'lax',
                                                       'form_with_generates_remote_forms': False,
                                                       'has_many_inversing': True,
                                                       'legacy_connection_handling': None,
                                                       'not_adopted': ['legacy_connection_handling',
                                                                       'urlsafe_csrf_tokens'],
                                                       'preload_links_header': True,
                                                       'urlsafe_csrf_tokens': None,
                                                       'utc_to_local_returns_utc_offset_times': True},
                                      'defaults_7_0': {'action_on_open_redirect': 'raise',
                                                       'adopted': 12,
                                                       'apply_stylesheet_media_default': False,
                                                       'automatic_scope_inversing': True,
                                                       'button_to_generates_button_tag': True,
                                                       'cache_format_version': 7.1,
                                                       'deprecated_to_s_loaded': False,
                                                       'disable_to_s_conversion': None,
                                                       'hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                       'key_generator_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                       'not_adopted': ['cache_format_version',
                                                                       'use_rfc4122_namespaced_uuids'],
                                                       'partial_inserts': False,
                                                       'raise_on_open_redirects': False,
                                                       'renamed': {'raise_on_open_redirects': 'action_on_open_redirect'},
                                                       'time_with_zone_name': 'ActiveSupport::TimeWithZone',
                                                       'to_s_arity': 0,
                                                       'use_rfc4122_namespaced_uuids': None,
                                                       'verify_foreign_keys_for_fixtures': True,
                                                       'wrap_parameters_by_default': True,
                                                       'x_xss_protection': '0'},
                                      'defaults_7_1': {'add_autoload_paths_to_load_path': False,
                                                       'adopted': 15,
                                                       'ar_before_committed_on_all_records': True,
                                                       'ar_default_column_serializer': 'nil',
                                                       'ar_generate_secure_token_on': 'initialize',
                                                       'ar_marshalling_format_version': 7.1,
                                                       'ar_raise_on_assign_to_attr_readonly': True,
                                                       'ar_run_after_transaction_callbacks_in_order_defined': True,
                                                       'as_cache_format_version': 7.1,
                                                       'as_message_serializer': 'json_allow_marshal',
                                                       'as_raise_on_invalid_cache_expiration_time': True,
                                                       'av_sanitizer_vendor': 'Rails::HTML5::Sanitizer',
                                                       'debug_exception_log_level': 'error',
                                                       'dom_testing_default_html_version': 'html5',
                                                       'enc_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                       'not_adopted': ['ac_allow_deprecated_parameters_hash_equality',
                                                                       'ar_allow_deprecated_singular_associations_name',
                                                                       'ar_commit_transaction_on_non_local_return'],
                                                       'precompile_filter_parameters': True},
                                      'defaults_7_2': {'adopted': 2,
                                                       'aj_enqueue_after_transaction_commit': 'false',
                                                       'ar_postgresql_adapter_decode_dates': True,
                                                       'ar_validate_migration_timestamps': True,
                                                       'not_adopted': ['aj_enqueue_after_transaction_commit'],
                                                       'yjit': True,
                                                       'yjit_runtime_available': False},
                                      'defaults_8_0': {'ad_strict_freshness': True,
                                                       'adopted': 3,
                                                       'as_to_time_preserves_timezone': 'withdrawn',
                                                       'not_adopted': [],
                                                       'regexp_timeout': 1.0,
                                                       'withdrawn': ['as_to_time_preserves_timezone']},
                                      'defaults_8_1': {'ac_escape_json_responses': False,
                                                       'ad_action_on_path_relative_redirect': 'raise',
                                                       'adopted': 6,
                                                       'ar_raise_on_missing_required_finder_order_columns': True,
                                                       'as_escape_js_separators_in_json': False,
                                                       'av_remove_hidden_field_autocomplete': True,
                                                       'av_render_tracker': 'ruby',
                                                       'not_adopted': []},
                                      'eager_load': {'ok': True,
                                                     'undefined_outside_app': []},
                                      'encryption': {'available': True,
                                                     'config_override_keys': ['hash_digest_class',
                                                                              'support_sha1_for_non_deterministic_encryption'],
                                                     'extend_queries': False,
                                                     'key_source': 'credentials',
                                                     'keys_present': {'deterministic_key': True,
                                                                      'key_derivation_salt': True,
                                                                      'primary_key': True},
                                                     'support_unencrypted_data': False},
                                      'fork_hooks': {'after_fork': 'ok',
                                                     'after_fork_permanent_lease': True,
                                                     'before_fork': 'ok',
                                                     'before_fork_permanent_lease': True,
                                                     'before_fork_status': 'ok',
                                                     'checkout_flag_supported': True,
                                                     'config_file': 'config/unicorn.rb',
                                                     'hooks': ['after_fork',
                                                               'before_fork'],
                                                     'pool_connections_after': 0,
                                                     'pool_connections_before': 1,
                                                     'pool_leased_after': 0,
                                                     'pool_leased_before': 1},
                                      'framework_defaults': {'belongs_to_required_by_default': True,
                                                             'not_adopted': ['to_time_preserves_timezone']},
                                      'framework_defaults_files': [],
                                      'host_authorization': {'count': 3,
                                                             'middleware_present': True},
                                      'load_defaults': {'loaded_config_version': '8.1'},
                                      'mailers': {'all_parameterized': True},
                                      'models': {'application_record': 'ApplicationRecord',
                                                 'belongs_to': {'Call#location': False},
                                                 'belongs_to_optional_count': 1,
                                                 'stray_base_subclasses': [],
                                                 'string_conditions': []},
                                      'routes': {'available': True,
                                                 'count': 19,
                                                 'drawn_by_positional_hash': False,
                                                 'high_voltage_drawer_keyword_safe': True,
                                                 'high_voltage_draws_routes': True,
                                                 'high_voltage_version': '5.0.0',
                                                 'page_endpoint': 'high_voltage/pages#show',
                                                 'page_format_segment': False,
                                                 'page_named': True,
                                                 'page_route_migrated': True,
                                                 'page_spec': '/pages/*id'},
                                      'secrets': {'credentials_key_file_present': True,
                                                  'effective_from_credentials': True,
                                                  'legacy_secret_store_files': [],
                                                  'plaintext_secret_files': [],
                                                  'read_encrypted_secrets': None},
                                      'versions': {'rails': '8.1.3.1'},
                                      'zeitwerk_check': {'ok': True}},
                           'ruby': '3.2.11'},
             'depdrive': {'booted': True,
                          'csp': {'nonce_problems': [], 'problems': []},
                          'encryption': {'case_variant_finds_same_row': True,
                                         'email_limit': None,
                                         'email_sql_type': 'text',
                                         'errors': {},
                                         'find_by_answers': True,
                                         'find_by_finds': True,
                                         'find_or_create_by_reuses': True,
                                         'long_address_round_trips': True,
                                         'long_round_trip_answers': True,
                                         'no_plaintext_at_rest': True,
                                         'ran': True},
                          'headers': {'always': {'Referrer-Policy': 'strict-origin-when-cross-origin',
                                                 'X-Content-Type-Options': 'nosniff',
                                                 'X-Frame-Options': 'SAMEORIGIN',
                                                 'X-Permitted-Cross-Domain-Policies': 'none',
                                                 'X-XSS-Protection': '0'},
                                      'html_responses': 8,
                                      'seen': ['Referrer-Policy',
                                               'X-Content-Type-Options',
                                               'X-Frame-Options',
                                               'X-Permitted-Cross-Domain-Policies',
                                               'X-XSS-Protection'],
                                      'sometimes': {}},
                          'hosts': {'blocks_suffix': True,
                                    'blocks_unknown': True,
                                    'known': 200,
                                    'loopback': 200,
                                    'serves_known': True,
                                    'suffix': 403,
                                    'unknown': 403},
                          'own_count': 0,
                          'queries': {'export_csv': {'async_queries': 3,
                                                     'preloaded': True,
                                                     'queries': 5,
                                                     'rows': 13,
                                                     'status': 200},
                                      'voice_answers': {'async_queries': 0,
                                                        'preloaded': True,
                                                        'queries': 4,
                                                        'rows': 12,
                                                        'status': 200}},
                          'strict_locals': {'accepts_unknown': ['locations/subscribe_form',
                                                                'numerical_answers/vote',
                                                                'numerical_answers/vote_meter',
                                                                'voice_answers/voice_answers'],
                                            'not_strict': ['locations/subscribe_form',
                                                           'numerical_answers/vote',
                                                           'numerical_answers/vote_meter',
                                                           'voice_answers/voice_answers'],
                                            'probed': 14,
                                            'refuses_unknown': ['application/feedback',
                                                                'application/footer',
                                                                'application/header',
                                                                'application/map',
                                                                'landing/answer',
                                                                'landing/info_block',
                                                                'locations/email_notifications',
                                                                'locations/numerical_response',
                                                                'locations/voice_message',
                                                                'voice_answers/answer'],
                                            'strict': ['application/feedback',
                                                       'application/footer',
                                                       'application/header',
                                                       'application/map',
                                                       'landing/answer',
                                                       'landing/info_block',
                                                       'locations/email_notifications',
                                                       'locations/numerical_response',
                                                       'locations/voice_message',
                                                       'voice_answers/answer']}},
             'envboot': {'checked': ['development', 'test'],
                         'env_file_deprecations': [],
                         'envs': {'test': {'exceptions_let_out': True,
                                           'exceptions_shown': False,
                                           'exceptions_shown_rescuable': False}},
                         'failed': [],
                         'own_deprecation_count': 0},
             'gemgraph': {'ruby_declared': '3.2.11',
                          'specs': {'annotaterb': '4.25.0',
                                    'high_voltage': '5.0.0',
                                    'sass': '3.4.25'}},
             'handoff': {'db_config_problems': []},
             'pkdrift': {'pinned': True},
             'rake': {'annotate_tasks': 0,
                      'exit_status': 0,
                      'has_db_migrate': True,
                      'ok': True},
             'rspec': {'deprecations': 0,
                       'examples': 504,
                       'failures': 0,
                       'inherited_rspec_deprecations': 1,
                       'ok': True,
                       'own_rspec_deprecations': 0,
                       'ran': True,
                       'suite_deprecation_sites': [],
                       'suite_deprecations': 0},
             'scan': {'annotations': {'columns': 46,
                                      'count': 7,
                                      'digest': 'c5dd17db4d81',
                                      'lines': 81,
                                      'models': {'app/models/answer.rb': {'columns': 7,
                                                                          'digest': '66e298d2f867',
                                                                          'lines': 12},
                                                 'app/models/call.rb': {'columns': 7,
                                                                        'digest': 'ee8f1d59692c',
                                                                        'lines': 12},
                                                 'app/models/caller.rb': {'columns': 4,
                                                                          'digest': '859ab859230f',
                                                                          'lines': 9},
                                                 'app/models/location.rb': {'columns': 8,
                                                                            'digest': '837c4a5cfa36',
                                                                            'lines': 13},
                                                 'app/models/location_subscription.rb': {'columns': 10,
                                                                                         'digest': '597a33c539c2',
                                                                                         'lines': 15},
                                                 'app/models/question.rb': {'columns': 6,
                                                                            'digest': '4f79be7d68ca',
                                                                            'lines': 11},
                                                 'app/models/subscriber.rb': {'columns': 4,
                                                                              'digest': '202c1f57622c',
                                                                              'lines': 9}}},
                      'assertion_lines': 310,
                      'counts': {'annotate_legacy_api': 0,
                                 'ar_base_connection': 0,
                                 'exists_predicate': 0,
                                 'form_for': 0,
                                 'form_with': 2,
                                 'framework_defaults_switch': 0,
                                 'mailer_positional': 0,
                                 'misversioned_migration': 1,
                                 'strict_loading': 3,
                                 'suppression': 0,
                                 'unversioned_migration': 0},
                      'emptied_tests': [],
                      'gutted_tests': [],
                      'hollow_tests': [],
                      'migrations': {'shipped_versions': {'4_2': 43}},
                      'missing_core': [],
                      'missing_tests': [],
                      'parse_errors': []}},
    "12": {'assets': {'assets_scanned': 21,
                         'counts': {'sprockets_directive': 13},
                         'root': '/app',
                         'total': 13},
              'behaviour': {'fraction': 1.0, 'passed': 1, 'total': 1},
              'bootprobe': {'app': '/app',
                            'boot_seconds': 8.49,
                            'booted': True,
                            'bundled_gem_list_known': True,
                            'bundled_gem_stdlib': [],
                            'bundled_gem_warnings': [],
                            'probes': {'asset_pipeline': {'assets_class': 'Sprockets::Railtie::OrderedOptions',
                                                          'coffee_loaded': True,
                                                          'config_assets': True,
                                                          'importmap_loaded': False,
                                                          'propshaft_loaded': False,
                                                          'sprockets_loaded': True,
                                                          'unknown_asset_fallback': False,
                                                          'webpacker_loaded': False},
                                       'async_queries': {'available': True,
                                                         'concurrency': 4,
                                                         'executor': 'global_thread_pool'},
                                       'audio': {'error_answer': '/audios/cv-probe-not-an-asset.mp3',
                                                 'error_class': 'TwilioSessionError',
                                                 'error_clean': True,
                                                 'error_deprecations': [],
                                                 'error_ok': True,
                                                 'error_public_path': True,
                                                 'error_raised': None,
                                                 'error_strict': True,
                                                 'fallback': False,
                                                 'helper_answer': '/audios/cv-probe-not-an-asset.mp3',
                                                 'helper_clean': True,
                                                 'helper_deprecations': [],
                                                 'helper_module': 'VoiceFeedbackHelper',
                                                 'helper_ok': True,
                                                 'helper_public_path': True,
                                                 'helper_raised': None,
                                                 'helper_strict': True,
                                                 'miss_name': 'cv-probe-not-an-asset'},
                                       'autoload_lib': {'autoload_path_entries': 1,
                                                        'eager_load_path_entries': 1,
                                                        'ignored': ['lib/assets',
                                                                    'lib/tasks'],
                                                        'in_autoload_paths': True,
                                                        'in_eager_load_paths': True,
                                                        'lib': '/app/lib',
                                                        'zeitwerk_version': '2.6.18'},
                                       'autoloader': {'classic_available': False,
                                                      'config': None,
                                                      'loaders': ['rails.main',
                                                                  'rails.once'],
                                                      'zeitwerk_enabled': True},
                                       'bootsnap': {'boot_file_setup': True,
                                                    'cache_bytes': 15053388,
                                                    'cache_files': 2455,
                                                    'compile_cache_dir': 'tmp/cache/bootsnap/compile-cache',
                                                    'defined': True,
                                                    'in_lockfile': True,
                                                    'load_path_cache_class': 'Bootsnap::LoadPathCache::Cache',
                                                    'setup_called': True,
                                                    'setup_from_boot_file': True,
                                                    'store_bytes': 268528,
                                                    'store_path': 'tmp/cache/bootsnap/load-path-cache',
                                                    'store_written': True,
                                                    'version': '1.9.3'},
                                       'csp': {'configured': True,
                                               'directives': ['default-src',
                                                              'font-src',
                                                              'img-src',
                                                              'media-src',
                                                              'object-src',
                                                              'script-src',
                                                              'style-src'],
                                               'initializer_present': True,
                                               'nonce_generator': True,
                                               'policy_class': 'ActionDispatch::ContentSecurityPolicy',
                                               'reader_exists': True,
                                               'report_only': False},
                                       'database': {'adapter': 'PostgreSQL',
                                                    'default_primary_key': 'bigserial '
                                                                           'primary '
                                                                           'key',
                                                    'id_sql_types': {'answers': 'integer',
                                                                     'callers': 'integer',
                                                                     'calls': 'integer',
                                                                     'location_subscriptions': 'integer',
                                                                     'locations': 'integer',
                                                                     'questions': 'integer',
                                                                     'subscribers': 'integer'},
                                                    'migrations_applied': 44,
                                                    'narrow_id_tables': ['answers',
                                                                         'callers',
                                                                         'calls',
                                                                         'location_subscriptions',
                                                                         'locations',
                                                                         'questions',
                                                                         'subscribers'],
                                                    'odd_id_tables': [],
                                                    'pk_pinned': True,
                                                    'schema_format': 'ruby',
                                                    'server_version': 160015,
                                                    'untyped_date_class': 'Date',
                                                    'wide_default_primary_key': True,
                                                    'wide_id_tables': []},
                                       'defaults_5_2': {'adopted': 4,
                                                        'cache_versioning': True,
                                                        'default_protect_from_forgery': True,
                                                        'form_with_generates_ids': True,
                                                        'hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                        'not_adopted': ['hash_digest_class'],
                                                        'use_authenticated_cookie_encryption': True,
                                                        'use_authenticated_message_encryption': True},
                                       'defaults_6_0': {'adopted': 4,
                                                        'collection_cache_versioning': True,
                                                        'default_enforce_utf8': False,
                                                        'delivery_job': 'ActionMailer::MailDeliveryJob',
                                                        'not_adopted': [],
                                                        'use_cookies_with_metadata': True},
                                       'defaults_6_1': {'adopted': 5,
                                                        'cookies_same_site_protection': 'lax',
                                                        'form_with_generates_remote_forms': False,
                                                        'has_many_inversing': True,
                                                        'legacy_connection_handling': None,
                                                        'not_adopted': ['legacy_connection_handling',
                                                                        'urlsafe_csrf_tokens'],
                                                        'preload_links_header': True,
                                                        'urlsafe_csrf_tokens': None,
                                                        'utc_to_local_returns_utc_offset_times': True},
                                       'defaults_7_0': {'action_on_open_redirect': 'raise',
                                                        'adopted': 12,
                                                        'apply_stylesheet_media_default': False,
                                                        'automatic_scope_inversing': True,
                                                        'button_to_generates_button_tag': True,
                                                        'cache_format_version': 7.1,
                                                        'deprecated_to_s_loaded': False,
                                                        'disable_to_s_conversion': None,
                                                        'hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                        'key_generator_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                        'not_adopted': ['cache_format_version',
                                                                        'use_rfc4122_namespaced_uuids'],
                                                        'partial_inserts': False,
                                                        'raise_on_open_redirects': False,
                                                        'renamed': {'raise_on_open_redirects': 'action_on_open_redirect'},
                                                        'time_with_zone_name': 'ActiveSupport::TimeWithZone',
                                                        'to_s_arity': 0,
                                                        'use_rfc4122_namespaced_uuids': None,
                                                        'verify_foreign_keys_for_fixtures': True,
                                                        'wrap_parameters_by_default': True,
                                                        'x_xss_protection': '0'},
                                       'defaults_7_1': {'ac_allow_deprecated_parameters_hash_equality': None,
                                                        'add_autoload_paths_to_load_path': False,
                                                        'adopted': 15,
                                                        'ar_allow_deprecated_singular_associations_name': None,
                                                        'ar_before_committed_on_all_records': True,
                                                        'ar_commit_transaction_on_non_local_return': None,
                                                        'ar_default_column_serializer': 'nil',
                                                        'ar_generate_secure_token_on': 'initialize',
                                                        'ar_marshalling_format_version': 7.1,
                                                        'ar_raise_on_assign_to_attr_readonly': True,
                                                        'ar_run_after_transaction_callbacks_in_order_defined': True,
                                                        'as_cache_format_version': 7.1,
                                                        'as_message_serializer': 'json_allow_marshal',
                                                        'as_raise_on_invalid_cache_expiration_time': True,
                                                        'av_sanitizer_vendor': 'Rails::HTML5::Sanitizer',
                                                        'debug_exception_log_level': 'error',
                                                        'dom_testing_default_html_version': 'html5',
                                                        'enc_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                        'not_adopted': ['ac_allow_deprecated_parameters_hash_equality',
                                                                        'ar_allow_deprecated_singular_associations_name',
                                                                        'ar_commit_transaction_on_non_local_return'],
                                                        'precompile_filter_parameters': True},
                                       'defaults_7_2': {'adopted': 2,
                                                        'aj_enqueue_after_transaction_commit': 'false',
                                                        'ar_postgresql_adapter_decode_dates': True,
                                                        'ar_validate_migration_timestamps': True,
                                                        'not_adopted': ['aj_enqueue_after_transaction_commit'],
                                                        'yjit': True,
                                                        'yjit_runtime_available': False},
                                       'defaults_8_0': {'ad_strict_freshness': True,
                                                        'adopted': 3,
                                                        'as_to_time_preserves_timezone': 'withdrawn',
                                                        'not_adopted': [],
                                                        'regexp_timeout': 1.0,
                                                        'withdrawn': ['as_to_time_preserves_timezone']},
                                       'defaults_8_1': {'ac_escape_json_responses': False,
                                                        'ad_action_on_path_relative_redirect': 'raise',
                                                        'adopted': 6,
                                                        'ar_raise_on_missing_required_finder_order_columns': True,
                                                        'as_escape_js_separators_in_json': False,
                                                        'av_remove_hidden_field_autocomplete': True,
                                                        'av_render_tracker': 'ruby',
                                                        'not_adopted': [],
                                                        'yjit': True,
                                                        'yjit_env': 'production'},
                                       'deprecation': {'behavior_classes': ['Proc'],
                                                       'configured': 'notify',
                                                       'deprecators': 10},
                                       'eager_load': {'autoload_only': [],
                                                      'ok': True,
                                                      'paths': ['lib'],
                                                      'seconds': 0.0,
                                                      'undefined_outside_app': []},
                                       'encryption': {'attributes': {'Subscriber#email': {'deterministic': True,
                                                                                          'downcase': True,
                                                                                          'limit': None,
                                                                                          'sql_type': 'text'}},
                                                      'available': True,
                                                      'config_override_keys': ['hash_digest_class',
                                                                               'support_sha1_for_non_deterministic_encryption'],
                                                      'credentials_keys': ['deterministic_key',
                                                                           'key_derivation_salt',
                                                                           'primary_key'],
                                                      'extend_queries': False,
                                                      'key_source': 'credentials',
                                                      'keys_present': {'deterministic_key': True,
                                                                       'key_derivation_salt': True,
                                                                       'primary_key': True},
                                                      'support_unencrypted_data': False},
                                       'fork_hooks': {'after_fork': 'ok',
                                                      'after_fork_permanent_lease': True,
                                                      'before_fork': 'ok',
                                                      'before_fork_permanent_lease': True,
                                                      'before_fork_status': 'ok',
                                                      'checkout_flag_supported': True,
                                                      'config_file': 'config/unicorn.rb',
                                                      'hooks': ['after_fork',
                                                                'before_fork'],
                                                      'pool_connections_after': 0,
                                                      'pool_connections_before': 1,
                                                      'pool_leased_after': 0,
                                                      'pool_leased_before': 1},
                                       'framework_defaults': {'adopted': 4,
                                                              'belongs_to_required_by_default': True,
                                                              'forgery_protection_origin_check': True,
                                                              'halt_callback_chains_on_return_false': False,
                                                              'not_adopted': ['to_time_preserves_timezone'],
                                                              'per_form_csrf_tokens': True,
                                                              'ssl_options': '{hsts: '
                                                                             '{subdomains: '
                                                                             'true}}',
                                                              'to_time_preserves_timezone': 'withdrawn'},
                                       'framework_defaults_files': [],
                                       'host_authorization': {'count': 3,
                                                              'hosts': ['"cityvoice.test"',
                                                                        '"localhost"',
                                                                        '"127.0.0.1"'],
                                                              'middleware_present': True,
                                                              'reader_exists': True},
                                       'load_defaults': {'loaded_config_version': '8.1',
                                                         'reader_exists': True},
                                       'loaded_gems': {'action_text-trix': '2.1.19',
                                                       'actioncable': '8.1.3.1',
                                                       'actionmailbox': '8.1.3.1',
                                                       'actionmailer': '8.1.3.1',
                                                       'actionpack': '8.1.3.1',
                                                       'actiontext': '8.1.3.1',
                                                       'actionview': '8.1.3.1',
                                                       'activejob': '8.1.3.1',
                                                       'activemodel': '8.1.3.1',
                                                       'activerecord': '8.1.3.1',
                                                       'activestorage': '8.1.3.1',
                                                       'activesupport': '8.1.3.1',
                                                       'addressable': '2.9.0',
                                                       'annotaterb': '4.25.0',
                                                       'backbone-rails': '1.1.0',
                                                       'base64': '0.3.0',
                                                       'bigdecimal': '3.1.8',
                                                       'bootsnap': '1.9.3',
                                                       'builder': '3.3.0',
                                                       'bundler': '2.6.9',
                                                       'capybara': '3.40.0',
                                                       'coderay': '1.1.3',
                                                       'coffee-rails': '5.0.0',
                                                       'coffee-script': '2.4.1',
                                                       'coffee-script-source': '1.12.2',
                                                       'concurrent-ruby': '1.3.4',
                                                       'connection_pool': '3.0.2',
                                                       'crack': '1.0.1',
                                                       'crass': '1.0.7',
                                                       'csv': '3.3.2',
                                                       'date': '3.5.1',
                                                       'diff-lcs': '1.6.2',
                                                       'dotenv': '2.8.1',
                                                       'dotenv-rails': '2.8.1',
                                                       'drb': '2.2.3',
                                                       'em-websocket': '0.5.3',
                                                       'erb': '6.0.7',
                                                       'erubi': '1.13.1',
                                                       'eventmachine': '1.2.7',
                                                       'execjs': '2.9.0',
                                                       'factory_bot': '5.2.0',
                                                       'factory_bot_rails': '5.2.0',
                                                       'ffaker': '2.11.0',
                                                       'ffi': '1.15.5',
                                                       'foreman': '0.89.1',
                                                       'formatador': '1.2.3',
                                                       'foundation-rails': '5.5.3.2',
                                                       'globalid': '1.4.0',
                                                       'google-analytics-rails': '0.0.5',
                                                       'guard': '2.19.0',
                                                       'guard-compat': '1.2.1',
                                                       'guard-livereload': '2.5.2',
                                                       'guard-teaspoon': '0.8.0',
                                                       'hashdiff': '1.2.1',
                                                       'hashie': '5.0.0',
                                                       'high_voltage': '5.0.0',
                                                       'http_parser.rb': '0.8.1',
                                                       'i18n': '1.14.8',
                                                       'io-console': '0.9.4',
                                                       'irb': '1.18.0',
                                                       'jbuilder': '2.13.0',
                                                       'jquery-rails': '4.6.1',
                                                       'json': '2.7.6',
                                                       'jwt': '2.3.0',
                                                       'kgio': '2.11.4',
                                                       'launchy': '2.5.2',
                                                       'leaflet-rails': '0.7.2',
                                                       'listen': '3.5.0',
                                                       'logger': '1.7.0',
                                                       'loofah': '2.25.2',
                                                       'lumberjack': '1.2.10',
                                                       'mail': '2.9.1',
                                                       'marcel': '1.2.1',
                                                       'matrix': '0.4.3',
                                                       'method_source': '1.1.0',
                                                       'mini_mime': '1.1.5',
                                                       'mini_portile2': '2.8.9',
                                                       'minitest': '5.15.0',
                                                       'msgpack': '1.4.2',
                                                       'multi_json': '1.15.0',
                                                       'mutex_m': '0.3.0',
                                                       'nenv': '0.3.0',
                                                       'net-imap': '0.5.15',
                                                       'net-pop': '0.1.2',
                                                       'net-protocol': '0.4.0',
                                                       'net-smtp': '0.5.1',
                                                       'newrelic_rpm': '8.16.0',
                                                       'nio4r': '2.7.5',
                                                       'nokogiri': '1.15.7',
                                                       'notiffany': '0.1.3',
                                                       'observer': '0.1.2',
                                                       'ostruct': '0.6.1',
                                                       'pathname': '0.4.0',
                                                       'pg': '1.4.6',
                                                       'pp': '0.6.4',
                                                       'prettyprint': '0.2.0',
                                                       'prism': '1.9.0',
                                                       'pry': '0.15.2',
                                                       'public_suffix': '4.0.7',
                                                       'racc': '1.8.1',
                                                       'rack': '2.2.24',
                                                       'rack-livereload': '0.5.2',
                                                       'rack-session': '1.0.2',
                                                       'rack-test': '2.2.0',
                                                       'rackup': '1.0.1',
                                                       'rails': '8.1.3.1',
                                                       'rails-controller-testing': '1.0.5',
                                                       'rails-dom-testing': '2.3.0',
                                                       'rails-html-sanitizer': '1.7.1',
                                                       'railties': '8.1.3.1',
                                                       'raindrops': '0.20.1',
                                                       'rake': '13.4.2',
                                                       'rb-fsevent': '0.11.2',
                                                       'rb-inotify': '0.10.1',
                                                       'rbs': '4.1.3',
                                                       'rdoc': '8.0.0',
                                                       'redcarpet': '3.1.1',
                                                       'regexp_parser': '2.12.0',
                                                       'reline': '0.7.0',
                                                       'rexml': '3.2.5',
                                                       'rspec-core': '3.13.6',
                                                       'rspec-expectations': '3.13.5',
                                                       'rspec-its': '1.3.1',
                                                       'rspec-mocks': '3.13.8',
                                                       'rspec-rails': '6.1.5',
                                                       'rspec-support': '3.13.7',
                                                       'sass': '3.4.25',
                                                       'sass-rails': '5.0.8',
                                                       'securerandom': '0.4.1',
                                                       'shellany': '0.0.1',
                                                       'shoulda-matchers': '4.5.1',
                                                       'soda-ruby': '0.2.12',
                                                       'sprockets': '3.7.5',
                                                       'sprockets-rails': '3.2.2',
                                                       'teaspoon': '1.4.0',
                                                       'thor': '1.2.2',
                                                       'tilt': '2.9.0',
                                                       'timecop': '0.9.11',
                                                       'timeout': '0.6.1',
                                                       'tsort': '0.2.0',
                                                       'twilio-ruby': '3.11.5',
                                                       'twitter-typeahead-rails': '0.10.2',
                                                       'tzinfo': '2.0.6',
                                                       'uglifier': '2.5.0',
                                                       'unicorn': '4.8.2',
                                                       'uri': '1.1.1',
                                                       'useragent': '0.16.11',
                                                       'vcr': '6.0.0',
                                                       'webmock': '2.3.2',
                                                       'webrick': '1.9.2',
                                                       'websocket-driver': '0.8.2',
                                                       'websocket-extensions': '0.1.5',
                                                       'will_paginate': '3.3.1',
                                                       'xpath': '3.2.0',
                                                       'zeitwerk': '2.6.18'},
                                       'mailers': {'action_arity': {'NotificationMailer#confirmation_email': 0,
                                                                    'NotificationMailer#feedback_input_type': 1,
                                                                    'NotificationMailer#weekly_activity': 0},
                                                   'action_mailer': True,
                                                   'all_parameterized': True,
                                                   'declared_action_arity': {'NotificationMailer#confirmation_email': 0,
                                                                             'NotificationMailer#weekly_activity': 0},
                                                   'mailers': ['NotificationMailer'],
                                                   'with_supported': True},
                                       'models': {'active_record': True,
                                                  'application_record': 'ApplicationRecord',
                                                  'application_record_abstract': True,
                                                  'application_record_parent': 'ActiveRecord::Base',
                                                  'belongs_to': {'Answer#call': True,
                                                                 'Answer#question': True,
                                                                 'Call#caller': True,
                                                                 'Call#location': False,
                                                                 'LocationSubscription#location': True,
                                                                 'LocationSubscription#subscriber': True},
                                                  'belongs_to_optional_count': 1,
                                                  'count': 8,
                                                  'direct_base_subclasses': ['ApplicationRecord'],
                                                  'parents': {'Answer': 'ApplicationRecord',
                                                              'ApplicationRecord': 'ActiveRecord::Base',
                                                              'Call': 'ApplicationRecord',
                                                              'Caller': 'ApplicationRecord',
                                                              'Location': 'ApplicationRecord',
                                                              'LocationSubscription': 'ApplicationRecord',
                                                              'Question': 'ApplicationRecord',
                                                              'Subscriber': 'ApplicationRecord'},
                                                  'stray_base_subclasses': [],
                                                  'string_conditions': []},
                                       'parameters': {'action_on_unpermitted': 'false',
                                                      'ancestors': ['ActionController::Parameters',
                                                                    'ActiveSupport::DeepMergeable',
                                                                    'ActiveSupport::Dependencies::RequireDependency',
                                                                    'Object',
                                                                    'ActiveSupport::ToJsonWithActiveSupportEncoder',
                                                                    'ActiveSupport::Tryable'],
                                                      'defined': True,
                                                      'is_hash': False,
                                                      'permit_all_parameters': False},
                                       'routes': {'available': True,
                                                  'count': 19,
                                                  'drawn_by_positional_hash': False,
                                                  'high_voltage_drawer_keyword_safe': True,
                                                  'high_voltage_draws_routes': True,
                                                  'high_voltage_version': '5.0.0',
                                                  'named': ['call_consent',
                                                            'call_location',
                                                            'call_message_playback',
                                                            'call_messages',
                                                            'call_question_answer',
                                                            'call_questions',
                                                            'calls',
                                                            'export',
                                                            'location',
                                                            'locations',
                                                            'numerical_answers',
                                                            'page',
                                                            'root',
                                                            'subscription',
                                                            'subscription_confirm',
                                                            'subscription_unsubscribe',
                                                            'voice_answers'],
                                                  'page_endpoint': 'high_voltage/pages#show',
                                                  'page_format_segment': False,
                                                  'page_named': True,
                                                  'page_route_migrated': True,
                                                  'page_spec': '/pages/*id',
                                                  'table': [{'name': None,
                                                             'spec': '/assets'},
                                                            {'name': 'voice_answers',
                                                             'spec': '/voice_answers(.:format)'},
                                                            {'name': 'numerical_answers',
                                                             'spec': '/numerical_answers(.:format)'},
                                                            {'name': 'export',
                                                             'spec': '/export(.:format)'},
                                                            {'name': 'subscription_confirm',
                                                             'spec': '/subscription/confirm(.:format)'},
                                                            {'name': 'subscription_unsubscribe',
                                                             'spec': '/subscription/unsubscribe(.:format)'},
                                                            {'name': 'subscription',
                                                             'spec': '/subscription(.:format)'},
                                                            {'name': None,
                                                             'spec': '/.well-known/status(.:format)'},
                                                            {'name': 'locations',
                                                             'spec': '/locations(.:format)'},
                                                            {'name': 'location',
                                                             'spec': '/locations/:id(.:format)'},
                                                            {'name': 'call_location',
                                                             'spec': '/calls/:call_id/location(.:format)'},
                                                            {'name': 'call_message_playback',
                                                             'spec': '/calls/:call_id/messages/:message_id/playback(.:format)'},
                                                            {'name': 'call_messages',
                                                             'spec': '/calls/:call_id/messages(.:format)'},
                                                            {'name': 'call_consent',
                                                             'spec': '/calls/:call_id/consent(.:format)'},
                                                            {'name': 'call_question_answer',
                                                             'spec': '/calls/:call_id/questions/:question_id/answer(.:format)'},
                                                            {'name': 'call_questions',
                                                             'spec': '/calls/:call_id/questions(.:format)'},
                                                            {'name': 'calls',
                                                             'spec': '/calls(.:format)'},
                                                            {'name': 'root',
                                                             'spec': '/'},
                                                            {'name': 'page',
                                                             'spec': '/pages/*id'}]},
                                       'secrets': {'assigned_length': 128,
                                                   'credentials_file_present': True,
                                                   'credentials_key_file_present': True,
                                                   'credentials_key_from_env': False,
                                                   'credentials_length': 128,
                                                   'credentials_present': True,
                                                   'credentials_template': False,
                                                   'effective_from_credentials': True,
                                                   'effective_from_encrypted': False,
                                                   'effective_from_secrets_yml': False,
                                                   'effective_key_length': 128,
                                                   'encrypted_key_file_present': False,
                                                   'encrypted_key_from_env': False,
                                                   'encrypted_length': 0,
                                                   'encrypted_present': False,
                                                   'from_secrets_yml': False,
                                                   'legacy_secret_store_files': [],
                                                   'plaintext_present': False,
                                                   'plaintext_secret_files': [],
                                                   'read_encrypted_secrets': None,
                                                   'reader_exists': False,
                                                   'secrets_yml_length': 0,
                                                   'secrets_yml_path': '/app/config/secrets.yml',
                                                   'secrets_yml_present': False,
                                                   'yml_file_length': 0},
                                       'versions': {'app_class': 'Automidnight::Application',
                                                    'env': 'production',
                                                    'rack': '2.2.24',
                                                    'rails': '8.1.3.1'},
                                       'zeitwerk_check': {'loaders': ['rails.main',
                                                                      'rails.once'],
                                                          'ok': True}},
                            'ruby': '3.4.10',
                            'ruby_platform': 'x86_64-linux',
                            'warnings': []},
              'depdrive': {'app': '/app',
                           'booted': True,
                           'count': 0,
                           'csp': {'directives': ['default-src',
                                                  'font-src',
                                                  'img-src',
                                                  'media-src',
                                                  'object-src',
                                                  'script-src',
                                                  'style-src'],
                                   'html_responses': 8,
                                   'inline_scripts': 12,
                                   'nonce_problems': [],
                                   'problems': [],
                                   'report_only': 0,
                                   'with_header': 8},
                           'csp_pass_deprecation_sites': [],
                           'csp_pass_deprecations': 0,
                           'csrf_token_found': True,
                           'deprecations': [],
                           'encryption': {'case_variant_finds_same_row': True,
                                          'email_limit': None,
                                          'email_sql_type': 'text',
                                          'errors': {},
                                          'find_by_answers': True,
                                          'find_by_finds': True,
                                          'find_or_create_by_reuses': True,
                                          'long_address_round_trips': True,
                                          'long_round_trip_answers': True,
                                          'long_stored_bytes': 283,
                                          'no_plaintext_at_rest': True,
                                          'ran': True,
                                          'stored_bytes': 122},
                           'encryption_pass_deprecations': 0,
                           'headers': {'always': {'Referrer-Policy': 'strict-origin-when-cross-origin',
                                                  'X-Content-Type-Options': 'nosniff',
                                                  'X-Frame-Options': 'SAMEORIGIN',
                                                  'X-Permitted-Cross-Domain-Policies': 'none',
                                                  'X-XSS-Protection': '0'},
                                       'html_responses': 8,
                                       'seen': ['Referrer-Policy',
                                                'X-Content-Type-Options',
                                                'X-Frame-Options',
                                                'X-Permitted-Cross-Domain-Policies',
                                                'X-XSS-Protection'],
                                       'sometimes': {}},
                           'hosts': {'blocks_suffix': True,
                                     'blocks_unknown': True,
                                     'known': 200,
                                     'loopback': 200,
                                     'serves_known': True,
                                     'suffix': 403,
                                     'unknown': 403},
                           'hosts_pass_deprecations': 0,
                           'own': [],
                           'own_count': 0,
                           'queries': {'export_csv': {'async_queries': 3,
                                                      'path': '/export.csv',
                                                      'preloaded': True,
                                                      'queries': 5,
                                                      'rows': 13,
                                                      'status': 200},
                                       'voice_answers': {'async_queries': 0,
                                                         'path': '/voice_answers',
                                                         'preloaded': True,
                                                         'queries': 4,
                                                         'rows': 12,
                                                         'status': 200}},
                           'queries_pass_deprecations': 0,
                           'raised': 0,
                           'ruby': '3.4.10',
                           'steps': [{'result': '', 'step': 'eager_load'},
                                     {'result': 'ok',
                                      'step': 'rake import:locations'},
                                     {'result': 'ok',
                                      'step': 'rake import:questions'},
                                     {'result': '200', 'step': 'GET /'},
                                     {'result': '406', 'step': 'GET /locations'},
                                     {'result': '200',
                                      'step': 'GET /locations.json'},
                                     {'result': '200', 'step': 'GET /locations/1'},
                                     {'result': '200',
                                      'step': 'GET /locations/1.json'},
                                     {'result': '200', 'step': 'GET /voice_answers'},
                                     {'result': '200',
                                      'step': 'GET /numerical_answers'},
                                     {'result': '200',
                                      'step': 'GET /numerical_answers.csv'},
                                     {'result': '200', 'step': 'GET /export.csv'},
                                     {'result': '500',
                                      'step': 'GET /.well-known/status'},
                                     {'result': '200', 'step': 'POST /calls'},
                                     {'result': '200',
                                      'step': 'POST /calls/:id/location'},
                                     {'result': '200',
                                      'step': 'POST /calls/:id/consent'},
                                     {'result': '200',
                                      'step': 'POST /calls/:id/questions'},
                                     {'result': '200',
                                      'step': 'POST /calls/:id/questions/0/answer'},
                                     {'result': '500', 'step': 'POST /subscription'},
                                     {'result': 'ok',
                                      'step': 'rake notifications:send'},
                                     {'result': '200',
                                      'step': 'CSP GET /pages/about'},
                                     {'result': '200',
                                      'step': 'CSP GET /pages/contact'},
                                     {'result': '200',
                                      'step': 'CSP GET /pages/messages'},
                                     {'result': '200', 'step': 'CSP GET /'},
                                     {'result': '14', 'step': 'strict locals'},
                                     {'result': '12 answers, 13 calls',
                                      'step': 'preload rows'},
                                     {'result': '200 in 4 queries (0 async)',
                                      'step': 'QUERIES GET /voice_answers'},
                                     {'result': '200 in 5 queries (3 async)',
                                      'step': 'QUERIES GET /export.csv'},
                                     {'result': 'at rest true, find_by true, case '
                                                'true, width true (283 bytes in '
                                                '"text")',
                                      'step': 'encryption round trip'},
                                     {'result': '200',
                                      'step': 'HOST cityvoice.test'},
                                     {'result': '200',
                                      'step': 'HOST 127.0.0.1:3000'},
                                     {'result': '403',
                                      'step': 'HOST evil.example.com'},
                                     {'result': '403',
                                      'step': 'HOST '
                                              'cityvoice.test.evil.example.com'}],
                           'strict_locals': {'accepts_unknown': ['locations/subscribe_form',
                                                                 'numerical_answers/vote',
                                                                 'numerical_answers/vote_meter',
                                                                 'voice_answers/voice_answers'],
                                             'not_strict': ['locations/subscribe_form',
                                                            'numerical_answers/vote',
                                                            'numerical_answers/vote_meter',
                                                            'voice_answers/voice_answers'],
                                             'partials': {'application/feedback': {'declaration': 'content:',
                                                                                   'probe_error': 'ActionView::Template::Error: '
                                                                                                  'unknown '
                                                                                                  'local: '
                                                                                                  ':cv_depdrive_strict_probe '
                                                                                                  'for '
                                                                                                  'app/views/application/_feedback.html.erb',
                                                                                   'refuses_unknown': True,
                                                                                   'strict': True},
                                                          'application/footer': {'declaration': 'content:',
                                                                                 'probe_error': 'ActionView::Template::Error: '
                                                                                                'unknown '
                                                                                                'local: '
                                                                                                ':cv_depdrive_strict_probe '
                                                                                                'for '
                                                                                                'app/views/application/_footer.html.erb',
                                                                                 'refuses_unknown': True,
                                                                                 'strict': True},
                                                          'application/header': {'declaration': 'content:',
                                                                                 'probe_error': 'ActionView::Template::Error: '
                                                                                                'unknown '
                                                                                                'local: '
                                                                                                ':cv_depdrive_strict_probe '
                                                                                                'for '
                                                                                                'app/views/application/_header.html.erb',
                                                                                 'refuses_unknown': True,
                                                                                 'strict': True},
                                                          'application/map': {'declaration': '**nil',
                                                                              'probe_error': 'ActionView::Template::Error: '
                                                                                             'no '
                                                                                             'locals '
                                                                                             'accepted '
                                                                                             'for '
                                                                                             'app/views/application/_map.html.erb',
                                                                              'refuses_unknown': True,
                                                                              'strict': True},
                                                          'landing/answer': {'declaration': 'answer:',
                                                                             'probe_error': 'ActionView::Template::Error: '
                                                                                            'unknown '
                                                                                            'local: '
                                                                                            ':cv_depdrive_strict_probe '
                                                                                            'for '
                                                                                            'app/views/landing/_answer.html.erb',
                                                                             'refuses_unknown': True,
                                                                             'strict': True},
                                                          'landing/info_block': {'declaration': 'title: '
                                                                                                'nil, '
                                                                                                'html_class: '
                                                                                                'nil',
                                                                                 'probe_error': 'ActionView::Template::Error: '
                                                                                                'unknown '
                                                                                                'local: '
                                                                                                ':cv_depdrive_strict_probe '
                                                                                                'for '
                                                                                                'app/views/landing/_info_block.html.erb',
                                                                                 'refuses_unknown': True,
                                                                                 'strict': True},
                                                          'locations/email_notifications': {'declaration': 'location:',
                                                                                            'probe_error': 'ActionView::Template::Error: '
                                                                                                           'unknown '
                                                                                                           'local: '
                                                                                                           ':cv_depdrive_strict_probe '
                                                                                                           'for '
                                                                                                           'app/views/locations/_email_notifications.html.erb',
                                                                                            'refuses_unknown': True,
                                                                                            'strict': True},
                                                          'locations/numerical_response': {'declaration': 'numerical_response:',
                                                                                           'probe_error': 'ActionView::Template::Error: '
                                                                                                          'unknown '
                                                                                                          'local: '
                                                                                                          ':cv_depdrive_strict_probe '
                                                                                                          'for '
                                                                                                          'app/views/locations/_numerical_response.html.erb',
                                                                                           'refuses_unknown': True,
                                                                                           'strict': True},
                                                          'locations/subscribe_form': {'declaration': '',
                                                                                       'probe_error': 'ActionView::Template::Error: '
                                                                                                      'undefined '
                                                                                                      'method '
                                                                                                      "'id' "
                                                                                                      'for '
                                                                                                      'nil',
                                                                                       'refuses_unknown': False,
                                                                                       'strict': False},
                                                          'locations/voice_message': {'declaration': 'answer:',
                                                                                      'probe_error': 'ActionView::Template::Error: '
                                                                                                     'unknown '
                                                                                                     'local: '
                                                                                                     ':cv_depdrive_strict_probe '
                                                                                                     'for '
                                                                                                     'app/views/locations/_voice_message.html.erb',
                                                                                      'refuses_unknown': True,
                                                                                      'strict': True},
                                                          'numerical_answers/vote': {'declaration': '',
                                                                                     'probe_error': 'ActionView::Template::Error: '
                                                                                                    'undefined '
                                                                                                    'local '
                                                                                                    'variable '
                                                                                                    'or '
                                                                                                    'method '
                                                                                                    "'response_set' "
                                                                                                    'for '
                                                                                                    'an '
                                                                                                    'instance '
                                                                                                    'of '
                                                                                                    '#<Class:0x00007f66441adb80>',
                                                                                     'refuses_unknown': False,
                                                                                     'strict': False},
                                                          'numerical_answers/vote_meter': {'declaration': '',
                                                                                           'probe_error': 'ActionView::Template::Error: '
                                                                                                          'undefined '
                                                                                                          'local '
                                                                                                          'variable '
                                                                                                          'or '
                                                                                                          'method '
                                                                                                          "'number' "
                                                                                                          'for '
                                                                                                          'an '
                                                                                                          'instance '
                                                                                                          'of '
                                                                                                          '#<Class:0x00007f66441adb80>',
                                                                                           'refuses_unknown': False,
                                                                                           'strict': False},
                                                          'voice_answers/answer': {'declaration': 'answer:',
                                                                                   'probe_error': 'ActionView::Template::Error: '
                                                                                                  'unknown '
                                                                                                  'local: '
                                                                                                  ':cv_depdrive_strict_probe '
                                                                                                  'for '
                                                                                                  'app/views/voice_answers/_answer.html.erb',
                                                                                   'refuses_unknown': True,
                                                                                   'strict': True},
                                                          'voice_answers/voice_answers': {'declaration': '',
                                                                                          'probe_error': 'ActionView::Template::Error: '
                                                                                                         'undefined '
                                                                                                         'local '
                                                                                                         'variable '
                                                                                                         'or '
                                                                                                         'method '
                                                                                                         "'answers' "
                                                                                                         'for '
                                                                                                         'an '
                                                                                                         'instance '
                                                                                                         'of '
                                                                                                         '#<Class:0x00007f66441adb80>',
                                                                                          'refuses_unknown': False,
                                                                                          'strict': False}},
                                             'probed': 14,
                                             'refuses_unknown': ['application/feedback',
                                                                 'application/footer',
                                                                 'application/header',
                                                                 'application/map',
                                                                 'landing/answer',
                                                                 'landing/info_block',
                                                                 'locations/email_notifications',
                                                                 'locations/numerical_response',
                                                                 'locations/voice_message',
                                                                 'voice_answers/answer'],
                                             'strict': ['application/feedback',
                                                        'application/footer',
                                                        'application/header',
                                                        'application/map',
                                                        'landing/answer',
                                                        'landing/info_block',
                                                        'locations/email_notifications',
                                                        'locations/numerical_response',
                                                        'locations/voice_message',
                                                        'voice_answers/answer']}},
              'envboot': {'app': '/app',
                          'bundled_gem_stdlib': [],
                          'checked': ['development', 'test'],
                          'declared': ['development', 'production', 'test'],
                          'env_file_deprecations': [],
                          'envs': {'development': {'booted': True,
                                                   'bundled_gem_stdlib': [],
                                                   'deprecation_count': 0,
                                                   'eager_load': False,
                                                   'env': 'development',
                                                   'exceptions_let_out': False,
                                                   'exceptions_shown': True,
                                                   'exceptions_shown_rescuable': True,
                                                   'middleware': ['ActionDispatch::HostAuthorization',
                                                                  'Rack::Sendfile',
                                                                  'ActionDispatch::Static',
                                                                  'Rack::LiveReload',
                                                                  'ActionDispatch::Executor',
                                                                  'Rack::Runtime',
                                                                  'Rack::MethodOverride',
                                                                  'ActionDispatch::RequestId',
                                                                  'ActionDispatch::RemoteIp',
                                                                  'Rails::Rack::Logger',
                                                                  'ActionDispatch::ShowExceptions',
                                                                  'ActionDispatch::DebugExceptions',
                                                                  'ActionDispatch::ActionableExceptions',
                                                                  'ActionDispatch::Reloader',
                                                                  'ActionDispatch::Callbacks',
                                                                  'ActiveRecord::Migration::CheckPending',
                                                                  'ActionDispatch::Cookies',
                                                                  'ActionDispatch::Session::CookieStore',
                                                                  'ActionDispatch::Flash',
                                                                  'ActionDispatch::ContentSecurityPolicy::Middleware',
                                                                  'Rack::Head',
                                                                  'Rack::ConditionalGet',
                                                                  'Rack::ETag',
                                                                  'Rack::TempfileReaper'],
                                                   'own': [],
                                                   'own_deprecation_count': 0,
                                                   'rails': '8.1.3.1',
                                                   'rails_env': 'development',
                                                   'show_exceptions': ':all'},
                                   'test': {'booted': True,
                                            'bundled_gem_stdlib': [],
                                            'deprecation_count': 0,
                                            'eager_load': False,
                                            'env': 'test',
                                            'exceptions_let_out': True,
                                            'exceptions_shown': False,
                                            'exceptions_shown_rescuable': False,
                                            'middleware': ['Rack::Sendfile',
                                                           'ActionDispatch::Static',
                                                           'ActionDispatch::Executor',
                                                           'Rack::Runtime',
                                                           'Rack::MethodOverride',
                                                           'ActionDispatch::RequestId',
                                                           'ActionDispatch::RemoteIp',
                                                           'Rails::Rack::Logger',
                                                           'ActionDispatch::ShowExceptions',
                                                           'ActionDispatch::DebugExceptions',
                                                           'ActionDispatch::ActionableExceptions',
                                                           'ActionDispatch::Callbacks',
                                                           'ActionDispatch::Cookies',
                                                           'ActionDispatch::Session::CookieStore',
                                                           'ActionDispatch::Flash',
                                                           'ActionDispatch::ContentSecurityPolicy::Middleware',
                                                           'Rack::Head',
                                                           'Rack::ConditionalGet',
                                                           'Rack::ETag',
                                                           'Rack::TempfileReaper'],
                                            'own': [],
                                            'own_deprecation_count': 0,
                                            'rails': '8.1.3.1',
                                            'rails_env': 'test',
                                            'show_exceptions': ':none'}},
                          'expected': ['development', 'test'],
                          'failed': [],
                          'own_deprecation_count': 0,
                          'ruby': '3.4.10',
                          'skipped': ['production']},
              'gemgraph': {'bundled_with': '2.6.9',
                           'bundler_agrees': True,
                           'bundler_error': None,
                           'direct': ['annotaterb',
                                      'backbone-rails',
                                      'bigdecimal',
                                      'bootsnap',
                                      'capybara',
                                      'coffee-rails',
                                      'concurrent-ruby',
                                      'csv',
                                      'dotenv-rails',
                                      'factory_bot_rails',
                                      'ffaker',
                                      'foreman',
                                      'foundation-rails',
                                      'google-analytics-rails',
                                      'guard-livereload',
                                      'guard-teaspoon',
                                      'high_voltage',
                                      'jbuilder',
                                      'jquery-rails',
                                      'json',
                                      'launchy',
                                      'leaflet-rails',
                                      'msgpack',
                                      'mutex_m',
                                      'newrelic_rpm',
                                      'observer',
                                      'ostruct',
                                      'pg',
                                      'pry',
                                      'rack-livereload',
                                      'rails',
                                      'rails-controller-testing',
                                      'redcarpet',
                                      'rspec-its',
                                      'rspec-rails',
                                      'sass',
                                      'sass-rails',
                                      'shoulda-matchers',
                                      'soda-ruby',
                                      'teaspoon',
                                      'timecop',
                                      'twilio-ruby',
                                      'twitter-typeahead-rails',
                                      'uglifier',
                                      'unicorn',
                                      'vcr',
                                      'webmock',
                                      'will_paginate'],
                           'gemfile_ruby': '3.4.10',
                           'lockfile': '/app/Gemfile.lock',
                           'orphans': [],
                           'reachable': ['action_text-trix',
                                         'actioncable',
                                         'actionmailbox',
                                         'actionmailer',
                                         'actionpack',
                                         'actiontext',
                                         'actionview',
                                         'activejob',
                                         'activemodel',
                                         'activerecord',
                                         'activestorage',
                                         'activesupport',
                                         'addressable',
                                         'annotaterb',
                                         'backbone-rails',
                                         'base64',
                                         'bigdecimal',
                                         'bootsnap',
                                         'builder',
                                         'bundler',
                                         'capybara',
                                         'coderay',
                                         'coffee-rails',
                                         'coffee-script',
                                         'coffee-script-source',
                                         'concurrent-ruby',
                                         'connection_pool',
                                         'crack',
                                         'crass',
                                         'csv',
                                         'date',
                                         'diff-lcs',
                                         'dotenv',
                                         'dotenv-rails',
                                         'drb',
                                         'em-websocket',
                                         'erb',
                                         'erubi',
                                         'eventmachine',
                                         'execjs',
                                         'factory_bot',
                                         'factory_bot_rails',
                                         'ffaker',
                                         'ffi',
                                         'foreman',
                                         'formatador',
                                         'foundation-rails',
                                         'globalid',
                                         'google-analytics-rails',
                                         'guard',
                                         'guard-compat',
                                         'guard-livereload',
                                         'guard-teaspoon',
                                         'hashdiff',
                                         'hashie',
                                         'high_voltage',
                                         'http_parser.rb',
                                         'i18n',
                                         'io-console',
                                         'irb',
                                         'jbuilder',
                                         'jquery-rails',
                                         'json',
                                         'jwt',
                                         'kgio',
                                         'launchy',
                                         'leaflet-rails',
                                         'listen',
                                         'logger',
                                         'loofah',
                                         'lumberjack',
                                         'mail',
                                         'marcel',
                                         'matrix',
                                         'method_source',
                                         'mini_mime',
                                         'mini_portile2',
                                         'minitest',
                                         'msgpack',
                                         'multi_json',
                                         'mutex_m',
                                         'nenv',
                                         'net-imap',
                                         'net-pop',
                                         'net-protocol',
                                         'net-smtp',
                                         'newrelic_rpm',
                                         'nio4r',
                                         'nokogiri',
                                         'notiffany',
                                         'observer',
                                         'ostruct',
                                         'pg',
                                         'pp',
                                         'prettyprint',
                                         'prism',
                                         'pry',
                                         'public_suffix',
                                         'racc',
                                         'rack',
                                         'rack-livereload',
                                         'rack-session',
                                         'rack-test',
                                         'rackup',
                                         'rails',
                                         'rails-controller-testing',
                                         'rails-dom-testing',
                                         'rails-html-sanitizer',
                                         'railties',
                                         'raindrops',
                                         'rake',
                                         'rb-fsevent',
                                         'rb-inotify',
                                         'rbs',
                                         'rdoc',
                                         'redcarpet',
                                         'regexp_parser',
                                         'reline',
                                         'rexml',
                                         'rspec-core',
                                         'rspec-expectations',
                                         'rspec-its',
                                         'rspec-mocks',
                                         'rspec-rails',
                                         'rspec-support',
                                         'sass',
                                         'sass-rails',
                                         'securerandom',
                                         'shellany',
                                         'shoulda-matchers',
                                         'soda-ruby',
                                         'sprockets',
                                         'sprockets-rails',
                                         'teaspoon',
                                         'thor',
                                         'tilt',
                                         'timecop',
                                         'timeout',
                                         'tsort',
                                         'twilio-ruby',
                                         'twitter-typeahead-rails',
                                         'tzinfo',
                                         'uglifier',
                                         'unicorn',
                                         'uri',
                                         'useragent',
                                         'vcr',
                                         'webmock',
                                         'webrick',
                                         'websocket-driver',
                                         'websocket-extensions',
                                         'will_paginate',
                                         'xpath',
                                         'zeitwerk'],
                           'root': '/app',
                           'ruby': '3.4.10p104',
                           'ruby_declared': '3.4.10',
                           'ruby_version_file': '3.4.10',
                           'sources': ['https://rubygems.org/'],
                           'specs': {'action_text-trix': '2.1.19',
                                     'actioncable': '8.1.3.1',
                                     'actionmailbox': '8.1.3.1',
                                     'actionmailer': '8.1.3.1',
                                     'actionpack': '8.1.3.1',
                                     'actiontext': '8.1.3.1',
                                     'actionview': '8.1.3.1',
                                     'activejob': '8.1.3.1',
                                     'activemodel': '8.1.3.1',
                                     'activerecord': '8.1.3.1',
                                     'activestorage': '8.1.3.1',
                                     'activesupport': '8.1.3.1',
                                     'addressable': '2.9.0',
                                     'annotaterb': '4.25.0',
                                     'backbone-rails': '1.1.0',
                                     'base64': '0.3.0',
                                     'bigdecimal': '3.1.8',
                                     'bootsnap': '1.9.3',
                                     'builder': '3.3.0',
                                     'capybara': '3.40.0',
                                     'coderay': '1.1.3',
                                     'coffee-rails': '5.0.0',
                                     'coffee-script': '2.4.1',
                                     'coffee-script-source': '1.12.2',
                                     'concurrent-ruby': '1.3.4',
                                     'connection_pool': '3.0.2',
                                     'crack': '1.0.1',
                                     'crass': '1.0.7',
                                     'csv': '3.3.2',
                                     'date': '3.5.1',
                                     'diff-lcs': '1.6.2',
                                     'dotenv': '2.8.1',
                                     'dotenv-rails': '2.8.1',
                                     'drb': '2.2.3',
                                     'em-websocket': '0.5.3',
                                     'erb': '6.0.7',
                                     'erubi': '1.13.1',
                                     'eventmachine': '1.2.7',
                                     'execjs': '2.9.0',
                                     'factory_bot': '5.2.0',
                                     'factory_bot_rails': '5.2.0',
                                     'ffaker': '2.11.0',
                                     'ffi': '1.15.5',
                                     'foreman': '0.89.1',
                                     'formatador': '1.2.3',
                                     'foundation-rails': '5.5.3.2',
                                     'globalid': '1.4.0',
                                     'google-analytics-rails': '0.0.5',
                                     'guard': '2.19.0',
                                     'guard-compat': '1.2.1',
                                     'guard-livereload': '2.5.2',
                                     'guard-teaspoon': '0.8.0',
                                     'hashdiff': '1.2.1',
                                     'hashie': '5.0.0',
                                     'high_voltage': '5.0.0',
                                     'http_parser.rb': '0.8.1',
                                     'i18n': '1.14.8',
                                     'io-console': '0.9.4',
                                     'irb': '1.18.0',
                                     'jbuilder': '2.13.0',
                                     'jquery-rails': '4.6.1',
                                     'json': '2.7.6',
                                     'jwt': '2.3.0',
                                     'kgio': '2.11.4',
                                     'launchy': '2.5.2',
                                     'leaflet-rails': '0.7.2',
                                     'listen': '3.5.0',
                                     'logger': '1.7.0',
                                     'loofah': '2.25.2',
                                     'lumberjack': '1.2.10',
                                     'mail': '2.9.1',
                                     'marcel': '1.2.1',
                                     'matrix': '0.4.3',
                                     'method_source': '1.1.0',
                                     'mini_mime': '1.1.5',
                                     'mini_portile2': '2.8.9',
                                     'minitest': '5.15.0',
                                     'msgpack': '1.4.2',
                                     'multi_json': '1.15.0',
                                     'mutex_m': '0.3.0',
                                     'nenv': '0.3.0',
                                     'net-imap': '0.5.15',
                                     'net-pop': '0.1.2',
                                     'net-protocol': '0.4.0',
                                     'net-smtp': '0.5.1',
                                     'newrelic_rpm': '8.16.0',
                                     'nio4r': '2.7.5',
                                     'nokogiri': '1.15.7',
                                     'notiffany': '0.1.3',
                                     'observer': '0.1.2',
                                     'ostruct': '0.6.1',
                                     'pg': '1.4.6',
                                     'pp': '0.6.4',
                                     'prettyprint': '0.2.0',
                                     'prism': '1.9.0',
                                     'pry': '0.15.2',
                                     'public_suffix': '4.0.7',
                                     'racc': '1.8.1',
                                     'rack': '2.2.24',
                                     'rack-livereload': '0.5.2',
                                     'rack-session': '1.0.2',
                                     'rack-test': '2.2.0',
                                     'rackup': '1.0.1',
                                     'rails': '8.1.3.1',
                                     'rails-controller-testing': '1.0.5',
                                     'rails-dom-testing': '2.3.0',
                                     'rails-html-sanitizer': '1.7.1',
                                     'railties': '8.1.3.1',
                                     'raindrops': '0.20.1',
                                     'rake': '13.4.2',
                                     'rb-fsevent': '0.11.2',
                                     'rb-inotify': '0.10.1',
                                     'rbs': '4.1.3',
                                     'rdoc': '8.0.0',
                                     'redcarpet': '3.1.1',
                                     'regexp_parser': '2.12.0',
                                     'reline': '0.7.0',
                                     'rexml': '3.2.5',
                                     'rspec-core': '3.13.6',
                                     'rspec-expectations': '3.13.5',
                                     'rspec-its': '1.3.1',
                                     'rspec-mocks': '3.13.8',
                                     'rspec-rails': '6.1.5',
                                     'rspec-support': '3.13.7',
                                     'sass': '3.4.25',
                                     'sass-rails': '5.0.8',
                                     'securerandom': '0.4.1',
                                     'shellany': '0.0.1',
                                     'shoulda-matchers': '4.5.1',
                                     'soda-ruby': '0.2.12',
                                     'sprockets': '3.7.5',
                                     'sprockets-rails': '3.2.2',
                                     'teaspoon': '1.4.0',
                                     'thor': '1.2.2',
                                     'tilt': '2.9.0',
                                     'timecop': '0.9.11',
                                     'timeout': '0.6.1',
                                     'tsort': '0.2.0',
                                     'twilio-ruby': '3.11.5',
                                     'twitter-typeahead-rails': '0.10.2',
                                     'tzinfo': '2.0.6',
                                     'uglifier': '2.5.0',
                                     'unicorn': '4.8.2',
                                     'uri': '1.1.1',
                                     'useragent': '0.16.11',
                                     'vcr': '6.0.0',
                                     'webmock': '2.3.2',
                                     'webrick': '1.9.2',
                                     'websocket-driver': '0.8.2',
                                     'websocket-extensions': '0.1.5',
                                     'will_paginate': '3.3.1',
                                     'xpath': '3.2.0',
                                     'zeitwerk': '2.6.18'},
                           'unresolved': ['bundler']},
              'handoff': {'database_yml_example_present': True,
                          'database_yml_present': False,
                          'db_config_problems': []},
              'meta': {'bundle_status': '0',
                       'preflight': 'ok',
                       'pruned': {'assets.findings': True,
                                  'behaviour.results': True,
                                  'gemgraph.paths': True,
                                  'scan.findings': True},
                       'sources': {'assets': {'bytes': 2623,
                                              'error': None,
                                              'path': '/tmp/gates/assets.json'},
                                   'behaviour': {'bytes': 27988,
                                                 'error': None,
                                                 'path': '/tmp/gates/behaviour.json'},
                                   'bootprobe': {'bytes': 20597,
                                                 'error': None,
                                                 'path': '/tmp/gates/bootprobe.json'},
                                   'depdrive': {'bytes': 9515,
                                                'error': None,
                                                'path': '/tmp/gates/depdrive.json'},
                                   'envboot': {'bytes': 2873,
                                               'error': None,
                                               'path': '/tmp/gates/envboot.json'},
                                   'gemgraph': {'bytes': 18533,
                                                'error': None,
                                                'path': '/tmp/gates/gemgraph.json'},
                                   'handoff': {'bytes': 95,
                                               'error': None,
                                               'path': '/tmp/gates/handoff.json'},
                                   'pkdrift': {'bytes': 2256,
                                               'error': None,
                                               'path': '/tmp/gates/pkdrift.json'},
                                   'rake': {'bytes': 1289,
                                            'error': None,
                                            'path': '/tmp/gates/rake.json'},
                                   'rspec': {'bytes': 569,
                                             'error': None,
                                             'path': '/tmp/gates/rspec.json'},
                                   'scan': {'bytes': 4560,
                                            'error': None,
                                            'path': '/tmp/gates/scan.json'}},
                       'step': '12-ruby-3-4'},
              'pkdrift': {'agree': True,
                          'booted': True,
                          'both_narrow': True,
                          'default_primary_key': 'bigserial primary key',
                          'differing_spellings': {},
                          'differing_tables': {},
                          'dump': {'id_sql_types': {'answers': 'integer',
                                                    'callers': 'integer',
                                                    'calls': 'integer',
                                                    'location_subscriptions': 'integer',
                                                    'locations': 'integer',
                                                    'questions': 'integer',
                                                    'subscribers': 'integer'},
                                   'id_widths': {'answers': 'narrow',
                                                 'callers': 'narrow',
                                                 'calls': 'narrow',
                                                 'location_subscriptions': 'narrow',
                                                 'locations': 'narrow',
                                                 'questions': 'narrow',
                                                 'subscribers': 'narrow'},
                                   'narrow_id_tables': ['answers',
                                                        'callers',
                                                        'calls',
                                                        'location_subscriptions',
                                                        'locations',
                                                        'questions',
                                                        'subscribers'],
                                   'odd_id_tables': [],
                                   'tables': ['answers',
                                              'callers',
                                              'calls',
                                              'location_subscriptions',
                                              'locations',
                                              'questions',
                                              'subscribers'],
                                   'wide_id_tables': []},
                          'dump_source': '/tmp/gates/pk-dump.json',
                          'migrate_exit': 0,
                          'migrated': {'id_sql_types': {'answers': 'integer',
                                                        'callers': 'integer',
                                                        'calls': 'integer',
                                                        'location_subscriptions': 'integer',
                                                        'locations': 'integer',
                                                        'questions': 'integer',
                                                        'subscribers': 'integer'},
                                       'id_widths': {'answers': 'narrow',
                                                     'callers': 'narrow',
                                                     'calls': 'narrow',
                                                     'location_subscriptions': 'narrow',
                                                     'locations': 'narrow',
                                                     'questions': 'narrow',
                                                     'subscribers': 'narrow'},
                                       'narrow_id_tables': ['answers',
                                                            'callers',
                                                            'calls',
                                                            'location_subscriptions',
                                                            'locations',
                                                            'questions',
                                                            'subscribers'],
                                       'odd_id_tables': [],
                                       'tables': ['answers',
                                                  'callers',
                                                  'calls',
                                                  'location_subscriptions',
                                                  'locations',
                                                  'questions',
                                                  'subscribers'],
                                       'wide_id_tables': []},
                          'migrated_source': '/tmp/gates/pk-migrated.json',
                          'migrations_applied': 44,
                          'pinned': True,
                          'problems': [],
                          'ruby': '3.4.10',
                          'schema_load_exit': 0,
                          'tables_only_in_dump': [],
                          'tables_only_in_migrated': [],
                          'wide_default_primary_key': True},
              'rake': {'annotate_tasks': 0,
                       'app': '/app',
                       'command': 'bundle exec rake -T',
                       'env': 'development',
                       'exit_status': 0,
                       'has_db_migrate': True,
                       'ok': True,
                       'ruby': '3.4.10',
                       'signalled': False,
                       'stderr_head': [],
                       'task_count': 46,
                       'tasks': ['app:template',
                                 'assets:clean[keep]',
                                 'assets:clobber',
                                 'assets:environment',
                                 'assets:precompile',
                                 'cache_digests:dependencies',
                                 'cache_digests:nested_dependencies',
                                 'db:create',
                                 'db:drop',
                                 'db:encryption:init',
                                 'db:environment:set',
                                 'db:fixtures:load',
                                 'db:migrate',
                                 'db:migrate:down',
                                 'db:migrate:redo',
                                 'db:migrate:reset',
                                 'db:migrate:status',
                                 'db:migrate:up',
                                 'db:prepare',
                                 'db:reset',
                                 'db:rollback',
                                 'db:schema:cache:clear',
                                 'db:schema:cache:dump',
                                 'db:schema:dump',
                                 'db:schema:load',
                                 'db:seed',
                                 'db:seed:replant',
                                 'db:setup',
                                 'db:version',
                                 'import:locations[path]',
                                 'import:questions[path]',
                                 'log:clear',
                                 'spec',
                                 'spec:controllers',
                                 'spec:features',
                                 'spec:helpers',
                                 'spec:lib',
                                 'spec:mailers',
                                 'spec:models',
                                 'teaspoon',
                                 'teaspoon:info',
                                 'time:zones[country_or_offset]',
                                 'tmp:clear',
                                 'tmp:create',
                                 'yarn:install',
                                 'zeitwerk:check']},
              'rspec': {'collected_deprecations': 0,
                        'collector': {'active_support': '8.1.3.1',
                                      'framework_silenced': 0,
                                      'installed': True,
                                      'lines': 3,
                                      'malformed': 0,
                                      'records': 0,
                                      'ruby': '3.4.10'},
                        'db_exit': 0,
                        'deprecations': 0,
                        'deprecations_by_file': {},
                        'examples': 504,
                        'exit': 0,
                        'failures': 0,
                        'inherited_rspec_deprecations': 1,
                        'ok': True,
                        'own_rspec_deprecation_sites': [],
                        'own_rspec_deprecations': 0,
                        'pending': 0,
                        'ran': True,
                        'rspec_deprecations': 1,
                        'rspec_hook': {'installed': True,
                                       'late': False,
                                       'rspec': '3.13.6'},
                        'suite_deprecation_sites': [],
                        'suite_deprecations': 0},
              'scan': {'annotations': {'columns': 46,
                                       'count': 7,
                                       'digest': 'c5dd17db4d81',
                                       'lines': 81,
                                       'models': {'app/models/answer.rb': {'columns': 7,
                                                                           'digest': '66e298d2f867',
                                                                           'lines': 12},
                                                  'app/models/call.rb': {'columns': 7,
                                                                         'digest': 'ee8f1d59692c',
                                                                         'lines': 12},
                                                  'app/models/caller.rb': {'columns': 4,
                                                                           'digest': '859ab859230f',
                                                                           'lines': 9},
                                                  'app/models/location.rb': {'columns': 8,
                                                                             'digest': '837c4a5cfa36',
                                                                             'lines': 13},
                                                  'app/models/location_subscription.rb': {'columns': 10,
                                                                                          'digest': '597a33c539c2',
                                                                                          'lines': 15},
                                                  'app/models/question.rb': {'columns': 6,
                                                                             'digest': '4f79be7d68ca',
                                                                             'lines': 11},
                                                  'app/models/subscriber.rb': {'columns': 4,
                                                                               'digest': '202c1f57622c',
                                                                               'lines': 9}}},
                       'assertion_lines': 310,
                       'counts': {'alias_method_chain': 0,
                                  'annotate_legacy_api': 0,
                                  'ar_base_connection': 0,
                                  'attr_accessible': 0,
                                  'before_filter': 0,
                                  'dead_engine_ref': 0,
                                  'exists_predicate': 0,
                                  'fixnum': 0,
                                  'form_for': 0,
                                  'form_with': 2,
                                  'framework_defaults_switch': 0,
                                  'load_defaults': 1,
                                  'load_defaults_5_1': 0,
                                  'mailer_positional': 0,
                                  'misversioned_migration': 1,
                                  'random_default': 0,
                                  'redirect_to_back': 0,
                                  'render_text': 0,
                                  'safe_global': 0,
                                  'scope_without_lambda': 0,
                                  'secret_key_base_assignment': 0,
                                  'secret_token': 0,
                                  'strict_loading': 3,
                                  'suppression': 0,
                                  'taint': 0,
                                  'unversioned_migration': 0,
                                  'update_attributes': 0,
                                  'uri_escape': 0},
                       'emptied_tests': [],
                       'erb_scanned': 28,
                       'files_scanned': 180,
                       'gutted_tests': [],
                       'hollow_tests': [],
                       'migrations': {'shipped_total': 43,
                                      'shipped_unversioned': 0,
                                      'shipped_versions': {'4_2': 43},
                                      'total': 44,
                                      'unversioned': 0,
                                      'versions': {'4_2': 43, '7_0': 1}},
                       'missing_core': [],
                       'missing_tests': [],
                       'not_evaluated': ['sprockets_directive'],
                       'parse_errors': [],
                       'pristine': '/tests/step/pristine.json',
                       'root': '/app',
                       'rules': ['attr_accessible',
                                 'secret_key_base_assignment',
                                 'dead_engine_ref',
                                 'scope_without_lambda',
                                 'before_filter',
                                 'unversioned_migration',
                                 'misversioned_migration',
                                 'redirect_to_back',
                                 'alias_method_chain',
                                 'render_text',
                                 'secret_token',
                                 'form_for',
                                 'form_with',
                                 'load_defaults',
                                 'load_defaults_5_1',
                                 'framework_defaults_switch',
                                 'mailer_positional',
                                 'update_attributes',
                                 'strict_loading',
                                 'ar_base_connection',
                                 'annotate_legacy_api',
                                 'sprockets_directive',
                                 'fixnum',
                                 'taint',
                                 'safe_global',
                                 'uri_escape',
                                 'exists_predicate',
                                 'random_default',
                                 'suppression'],
                       'total': 7}},
    "13": {'assets': {'assets_scanned': 21,
                         'counts': {'sprockets_directive': 13},
                         'root': '/app',
                         'total': 13},
              'audit': {'by_gem': {},
                        'counts': {'advisories': 0,
                                   'declared': 0,
                                   'gems': 0,
                                   'transitive': 0},
                        'db': {'advisories': 1245,
                               'commit': '44784c295391577f25d198a9205eae4ba73ec4da',
                               'error': None,
                               'gems': 470,
                               'path': '/opt/ruby-advisory-db',
                               'present': True},
                        'lockfile': '/app/Gemfile.lock',
                        'root': '/app',
                        'specs': 153,
                        'suppression': {'files': [], 'ignored_ids': []},
                        'unreadable': [],
                        'vulnerable_declared': [],
                        'vulnerable_gems': [],
                        'vulnerable_transitive': []},
              'behaviour': {'fraction': 1.0, 'passed': 1, 'total': 1},
              'bootprobe': {'app': '/app',
                            'boot_seconds': 5.69,
                            'booted': True,
                            'bundled_gem_list_known': True,
                            'bundled_gem_stdlib': [],
                            'bundled_gem_warnings': [],
                            'probes': {'asset_pipeline': {'assets_class': 'Sprockets::Railtie::OrderedOptions',
                                                          'coffee_loaded': True,
                                                          'config_assets': True,
                                                          'importmap_loaded': False,
                                                          'propshaft_loaded': False,
                                                          'sprockets_loaded': True,
                                                          'unknown_asset_fallback': False,
                                                          'webpacker_loaded': False},
                                       'async_queries': {'available': True,
                                                         'concurrency': 4,
                                                         'executor': 'global_thread_pool'},
                                       'audio': {'error_answer': '/audios/cv-probe-not-an-asset.mp3',
                                                 'error_class': 'TwilioSessionError',
                                                 'error_clean': True,
                                                 'error_deprecations': [],
                                                 'error_ok': True,
                                                 'error_public_path': True,
                                                 'error_raised': None,
                                                 'error_strict': True,
                                                 'fallback': False,
                                                 'helper_answer': '/audios/cv-probe-not-an-asset.mp3',
                                                 'helper_clean': True,
                                                 'helper_deprecations': [],
                                                 'helper_module': 'VoiceFeedbackHelper',
                                                 'helper_ok': True,
                                                 'helper_public_path': True,
                                                 'helper_raised': None,
                                                 'helper_strict': True,
                                                 'miss_name': 'cv-probe-not-an-asset'},
                                       'autoload_lib': {'autoload_path_entries': 1,
                                                        'eager_load_path_entries': 1,
                                                        'ignored': ['lib/assets',
                                                                    'lib/tasks'],
                                                        'in_autoload_paths': True,
                                                        'in_eager_load_paths': True,
                                                        'lib': '/app/lib',
                                                        'zeitwerk_version': '2.6.18'},
                                       'autoloader': {'classic_available': False,
                                                      'config': None,
                                                      'loaders': ['rails.main',
                                                                  'rails.once'],
                                                      'zeitwerk_enabled': True},
                                       'bootsnap': {'boot_file_setup': True,
                                                    'cache_bytes': 15181967,
                                                    'cache_files': 2486,
                                                    'compile_cache_dir': 'tmp/cache/bootsnap/compile-cache',
                                                    'defined': True,
                                                    'in_lockfile': True,
                                                    'load_path_cache_class': 'Bootsnap::LoadPathCache::Cache',
                                                    'setup_called': True,
                                                    'setup_from_boot_file': True,
                                                    'store_bytes': 269095,
                                                    'store_path': 'tmp/cache/bootsnap/load-path-cache',
                                                    'store_written': True,
                                                    'version': '1.9.3'},
                                       'csp': {'configured': True,
                                               'directives': ['default-src',
                                                              'font-src',
                                                              'img-src',
                                                              'media-src',
                                                              'object-src',
                                                              'script-src',
                                                              'style-src'],
                                               'initializer_present': True,
                                               'nonce_generator': True,
                                               'policy_class': 'ActionDispatch::ContentSecurityPolicy',
                                               'reader_exists': True,
                                               'report_only': False},
                                       'database': {'adapter': 'PostgreSQL',
                                                    'default_primary_key': 'bigserial '
                                                                           'primary '
                                                                           'key',
                                                    'id_sql_types': {'answers': 'integer',
                                                                     'callers': 'integer',
                                                                     'calls': 'integer',
                                                                     'location_subscriptions': 'integer',
                                                                     'locations': 'integer',
                                                                     'questions': 'integer',
                                                                     'subscribers': 'integer'},
                                                    'migrations_applied': 44,
                                                    'narrow_id_tables': ['answers',
                                                                         'callers',
                                                                         'calls',
                                                                         'location_subscriptions',
                                                                         'locations',
                                                                         'questions',
                                                                         'subscribers'],
                                                    'odd_id_tables': [],
                                                    'pk_pinned': True,
                                                    'schema_format': 'ruby',
                                                    'server_version': 160015,
                                                    'untyped_date_class': 'Date',
                                                    'wide_default_primary_key': True,
                                                    'wide_id_tables': []},
                                       'defaults_5_2': {'adopted': 4,
                                                        'cache_versioning': True,
                                                        'default_protect_from_forgery': True,
                                                        'form_with_generates_ids': True,
                                                        'hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                        'not_adopted': ['hash_digest_class'],
                                                        'use_authenticated_cookie_encryption': True,
                                                        'use_authenticated_message_encryption': True},
                                       'defaults_6_0': {'adopted': 4,
                                                        'collection_cache_versioning': True,
                                                        'default_enforce_utf8': False,
                                                        'delivery_job': 'ActionMailer::MailDeliveryJob',
                                                        'not_adopted': [],
                                                        'use_cookies_with_metadata': True},
                                       'defaults_6_1': {'adopted': 5,
                                                        'cookies_same_site_protection': 'lax',
                                                        'form_with_generates_remote_forms': False,
                                                        'has_many_inversing': True,
                                                        'legacy_connection_handling': None,
                                                        'not_adopted': ['legacy_connection_handling',
                                                                        'urlsafe_csrf_tokens'],
                                                        'preload_links_header': True,
                                                        'urlsafe_csrf_tokens': None,
                                                        'utc_to_local_returns_utc_offset_times': True},
                                       'defaults_7_0': {'action_on_open_redirect': 'raise',
                                                        'adopted': 12,
                                                        'apply_stylesheet_media_default': False,
                                                        'automatic_scope_inversing': True,
                                                        'button_to_generates_button_tag': True,
                                                        'cache_format_version': 7.1,
                                                        'deprecated_to_s_loaded': False,
                                                        'disable_to_s_conversion': None,
                                                        'hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                        'key_generator_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                        'not_adopted': ['cache_format_version',
                                                                        'use_rfc4122_namespaced_uuids'],
                                                        'partial_inserts': False,
                                                        'raise_on_open_redirects': False,
                                                        'renamed': {'raise_on_open_redirects': 'action_on_open_redirect'},
                                                        'time_with_zone_name': 'ActiveSupport::TimeWithZone',
                                                        'to_s_arity': 0,
                                                        'use_rfc4122_namespaced_uuids': None,
                                                        'verify_foreign_keys_for_fixtures': True,
                                                        'wrap_parameters_by_default': True,
                                                        'x_xss_protection': '0'},
                                       'defaults_7_1': {'ac_allow_deprecated_parameters_hash_equality': None,
                                                        'add_autoload_paths_to_load_path': False,
                                                        'adopted': 15,
                                                        'ar_allow_deprecated_singular_associations_name': None,
                                                        'ar_before_committed_on_all_records': True,
                                                        'ar_commit_transaction_on_non_local_return': None,
                                                        'ar_default_column_serializer': 'nil',
                                                        'ar_generate_secure_token_on': 'initialize',
                                                        'ar_marshalling_format_version': 7.1,
                                                        'ar_raise_on_assign_to_attr_readonly': True,
                                                        'ar_run_after_transaction_callbacks_in_order_defined': True,
                                                        'as_cache_format_version': 7.1,
                                                        'as_message_serializer': 'json_allow_marshal',
                                                        'as_raise_on_invalid_cache_expiration_time': True,
                                                        'av_sanitizer_vendor': 'Rails::HTML5::Sanitizer',
                                                        'debug_exception_log_level': 'error',
                                                        'dom_testing_default_html_version': 'html5',
                                                        'enc_hash_digest_class': 'OpenSSL::Digest::SHA256',
                                                        'not_adopted': ['ac_allow_deprecated_parameters_hash_equality',
                                                                        'ar_allow_deprecated_singular_associations_name',
                                                                        'ar_commit_transaction_on_non_local_return'],
                                                        'precompile_filter_parameters': True},
                                       'defaults_7_2': {'adopted': 2,
                                                        'aj_enqueue_after_transaction_commit': 'false',
                                                        'ar_postgresql_adapter_decode_dates': True,
                                                        'ar_validate_migration_timestamps': True,
                                                        'not_adopted': ['aj_enqueue_after_transaction_commit'],
                                                        'yjit': True,
                                                        'yjit_runtime_available': False},
                                       'defaults_8_0': {'ad_strict_freshness': True,
                                                        'adopted': 3,
                                                        'as_to_time_preserves_timezone': 'withdrawn',
                                                        'not_adopted': [],
                                                        'regexp_timeout': 1.0,
                                                        'withdrawn': ['as_to_time_preserves_timezone']},
                                       'defaults_8_1': {'ac_escape_json_responses': False,
                                                        'ad_action_on_path_relative_redirect': 'raise',
                                                        'adopted': 6,
                                                        'ar_raise_on_missing_required_finder_order_columns': True,
                                                        'as_escape_js_separators_in_json': False,
                                                        'av_remove_hidden_field_autocomplete': True,
                                                        'av_render_tracker': 'ruby',
                                                        'not_adopted': [],
                                                        'yjit': True,
                                                        'yjit_env': 'production'},
                                       'deprecation': {'behavior_classes': ['Proc'],
                                                       'configured': 'notify',
                                                       'deprecators': 10},
                                       'eager_load': {'autoload_only': [],
                                                      'ok': True,
                                                      'paths': ['lib'],
                                                      'seconds': 0.0,
                                                      'undefined_outside_app': []},
                                       'encryption': {'attributes': {'Subscriber#email': {'deterministic': True,
                                                                                          'downcase': True,
                                                                                          'limit': None,
                                                                                          'sql_type': 'text'}},
                                                      'available': True,
                                                      'config_override_keys': ['hash_digest_class',
                                                                               'support_sha1_for_non_deterministic_encryption'],
                                                      'credentials_keys': ['deterministic_key',
                                                                           'key_derivation_salt',
                                                                           'primary_key'],
                                                      'extend_queries': False,
                                                      'key_source': 'credentials',
                                                      'keys_present': {'deterministic_key': True,
                                                                       'key_derivation_salt': True,
                                                                       'primary_key': True},
                                                      'support_unencrypted_data': False},
                                       'fork_hooks': {'after_fork': 'ok',
                                                      'after_fork_permanent_lease': True,
                                                      'before_fork': 'ok',
                                                      'before_fork_permanent_lease': True,
                                                      'before_fork_status': 'ok',
                                                      'checkout_flag_supported': True,
                                                      'config_file': 'config/unicorn.rb',
                                                      'hooks': ['after_fork',
                                                                'before_fork'],
                                                      'pool_connections_after': 0,
                                                      'pool_connections_before': 1,
                                                      'pool_leased_after': 0,
                                                      'pool_leased_before': 1},
                                       'framework_defaults': {'adopted': 4,
                                                              'belongs_to_required_by_default': True,
                                                              'forgery_protection_origin_check': True,
                                                              'halt_callback_chains_on_return_false': False,
                                                              'not_adopted': ['to_time_preserves_timezone'],
                                                              'per_form_csrf_tokens': True,
                                                              'ssl_options': '{hsts: '
                                                                             '{subdomains: '
                                                                             'true}}',
                                                              'to_time_preserves_timezone': 'withdrawn'},
                                       'framework_defaults_files': [],
                                       'host_authorization': {'count': 3,
                                                              'hosts': ['"cityvoice.test"',
                                                                        '"localhost"',
                                                                        '"127.0.0.1"'],
                                                              'middleware_present': True,
                                                              'reader_exists': True},
                                       'load_defaults': {'loaded_config_version': '8.1',
                                                         'reader_exists': True},
                                       'loaded_gems': {'action_text-trix': '2.1.19',
                                                       'actioncable': '8.1.3.1',
                                                       'actionmailbox': '8.1.3.1',
                                                       'actionmailer': '8.1.3.1',
                                                       'actionpack': '8.1.3.1',
                                                       'actiontext': '8.1.3.1',
                                                       'actionview': '8.1.3.1',
                                                       'activejob': '8.1.3.1',
                                                       'activemodel': '8.1.3.1',
                                                       'activerecord': '8.1.3.1',
                                                       'activestorage': '8.1.3.1',
                                                       'activesupport': '8.1.3.1',
                                                       'addressable': '2.9.0',
                                                       'annotaterb': '4.25.0',
                                                       'backbone-rails': '1.1.0',
                                                       'base64': '0.3.0',
                                                       'bigdecimal': '3.1.8',
                                                       'bootsnap': '1.9.3',
                                                       'builder': '3.3.0',
                                                       'bundler': '2.6.9',
                                                       'capybara': '3.40.0',
                                                       'coderay': '1.1.3',
                                                       'coffee-rails': '5.0.0',
                                                       'coffee-script': '2.4.1',
                                                       'coffee-script-source': '1.12.2',
                                                       'concurrent-ruby': '1.3.7',
                                                       'connection_pool': '3.0.2',
                                                       'crack': '1.0.1',
                                                       'crass': '1.0.7',
                                                       'csv': '3.3.2',
                                                       'date': '3.5.1',
                                                       'diff-lcs': '1.6.2',
                                                       'dotenv': '2.8.1',
                                                       'dotenv-rails': '2.8.1',
                                                       'drb': '2.2.3',
                                                       'em-websocket': '0.5.3',
                                                       'erb': '6.0.7',
                                                       'erubi': '1.13.1',
                                                       'eventmachine': '1.2.7',
                                                       'execjs': '2.9.0',
                                                       'factory_bot': '5.2.0',
                                                       'factory_bot_rails': '5.2.0',
                                                       'ffaker': '2.11.0',
                                                       'ffi': '1.15.5',
                                                       'foreman': '0.89.1',
                                                       'formatador': '1.2.3',
                                                       'foundation-rails': '5.5.3.2',
                                                       'globalid': '1.4.0',
                                                       'google-analytics-rails': '0.0.5',
                                                       'guard': '2.19.0',
                                                       'guard-compat': '1.2.1',
                                                       'guard-livereload': '2.5.2',
                                                       'guard-teaspoon': '0.8.0',
                                                       'hashdiff': '1.2.1',
                                                       'hashie': '5.0.0',
                                                       'high_voltage': '5.0.0',
                                                       'http_parser.rb': '0.8.1',
                                                       'i18n': '1.14.8',
                                                       'io-console': '0.9.4',
                                                       'irb': '1.18.0',
                                                       'jbuilder': '2.13.0',
                                                       'jquery-rails': '4.6.1',
                                                       'json': '2.7.6',
                                                       'jwt': '3.3.0',
                                                       'kgio': '2.11.4',
                                                       'launchy': '2.5.2',
                                                       'leaflet-rails': '0.7.2',
                                                       'listen': '3.5.0',
                                                       'logger': '1.7.0',
                                                       'loofah': '2.25.2',
                                                       'lumberjack': '1.2.10',
                                                       'mail': '2.9.1',
                                                       'marcel': '1.2.1',
                                                       'matrix': '0.4.3',
                                                       'method_source': '1.1.0',
                                                       'mini_mime': '1.1.5',
                                                       'mini_portile2': '2.8.9',
                                                       'minitest': '5.15.0',
                                                       'msgpack': '1.8.2',
                                                       'multi_json': '1.15.0',
                                                       'mutex_m': '0.3.0',
                                                       'nenv': '0.3.0',
                                                       'net-imap': '0.5.15',
                                                       'net-pop': '0.1.2',
                                                       'net-protocol': '0.4.0',
                                                       'net-smtp': '0.5.1',
                                                       'newrelic_rpm': '8.16.0',
                                                       'nio4r': '2.7.5',
                                                       'nokogiri': '1.19.4',
                                                       'notiffany': '0.1.3',
                                                       'observer': '0.1.2',
                                                       'ostruct': '0.6.1',
                                                       'pathname': '0.4.0',
                                                       'pg': '1.4.6',
                                                       'pp': '0.6.4',
                                                       'prettyprint': '0.2.0',
                                                       'prism': '1.9.0',
                                                       'pry': '0.15.2',
                                                       'public_suffix': '4.0.7',
                                                       'racc': '1.8.1',
                                                       'rack': '2.2.24',
                                                       'rack-livereload': '0.5.2',
                                                       'rack-session': '1.0.2',
                                                       'rack-test': '2.2.0',
                                                       'rackup': '1.0.1',
                                                       'rails': '8.1.3.1',
                                                       'rails-controller-testing': '1.0.5',
                                                       'rails-dom-testing': '2.3.0',
                                                       'rails-html-sanitizer': '1.7.1',
                                                       'railties': '8.1.3.1',
                                                       'raindrops': '0.20.1',
                                                       'rake': '13.4.2',
                                                       'rb-fsevent': '0.11.2',
                                                       'rb-inotify': '0.10.1',
                                                       'rbs': '4.1.3',
                                                       'rdoc': '8.0.0',
                                                       'redcarpet': '3.6.1',
                                                       'regexp_parser': '2.12.0',
                                                       'reline': '0.7.0',
                                                       'rexml': '3.4.4',
                                                       'rspec-core': '3.13.6',
                                                       'rspec-expectations': '3.13.5',
                                                       'rspec-its': '1.3.1',
                                                       'rspec-mocks': '3.13.8',
                                                       'rspec-rails': '6.1.5',
                                                       'rspec-support': '3.13.7',
                                                       'sass': '3.4.25',
                                                       'sass-rails': '5.0.8',
                                                       'securerandom': '0.4.1',
                                                       'shellany': '0.0.1',
                                                       'shoulda-matchers': '4.5.1',
                                                       'soda-ruby': '0.2.12',
                                                       'sprockets': '3.7.5',
                                                       'sprockets-rails': '3.2.2',
                                                       'teaspoon': '1.4.0',
                                                       'thor': '1.2.2',
                                                       'tilt': '2.9.0',
                                                       'timecop': '0.9.11',
                                                       'timeout': '0.6.1',
                                                       'tsort': '0.2.0',
                                                       'twilio-ruby': '3.11.5',
                                                       'twitter-typeahead-rails': '0.10.2',
                                                       'tzinfo': '2.0.6',
                                                       'uglifier': '4.2.1',
                                                       'unicorn': '4.8.2',
                                                       'uri': '1.1.1',
                                                       'useragent': '0.16.11',
                                                       'vcr': '6.0.0',
                                                       'webmock': '2.3.2',
                                                       'webrick': '1.9.2',
                                                       'websocket-driver': '0.8.2',
                                                       'websocket-extensions': '0.1.5',
                                                       'will_paginate': '3.3.1',
                                                       'xpath': '3.2.0',
                                                       'zeitwerk': '2.6.18'},
                                       'mailers': {'action_arity': {'NotificationMailer#confirmation_email': 0,
                                                                    'NotificationMailer#feedback_input_type': 1,
                                                                    'NotificationMailer#weekly_activity': 0},
                                                   'action_mailer': True,
                                                   'all_parameterized': True,
                                                   'declared_action_arity': {'NotificationMailer#confirmation_email': 0,
                                                                             'NotificationMailer#weekly_activity': 0},
                                                   'mailers': ['NotificationMailer'],
                                                   'with_supported': True},
                                       'models': {'active_record': True,
                                                  'application_record': 'ApplicationRecord',
                                                  'application_record_abstract': True,
                                                  'application_record_parent': 'ActiveRecord::Base',
                                                  'belongs_to': {'Answer#call': True,
                                                                 'Answer#question': True,
                                                                 'Call#caller': True,
                                                                 'Call#location': False,
                                                                 'LocationSubscription#location': True,
                                                                 'LocationSubscription#subscriber': True},
                                                  'belongs_to_optional_count': 1,
                                                  'count': 8,
                                                  'direct_base_subclasses': ['ApplicationRecord'],
                                                  'parents': {'Answer': 'ApplicationRecord',
                                                              'ApplicationRecord': 'ActiveRecord::Base',
                                                              'Call': 'ApplicationRecord',
                                                              'Caller': 'ApplicationRecord',
                                                              'Location': 'ApplicationRecord',
                                                              'LocationSubscription': 'ApplicationRecord',
                                                              'Question': 'ApplicationRecord',
                                                              'Subscriber': 'ApplicationRecord'},
                                                  'stray_base_subclasses': [],
                                                  'string_conditions': []},
                                       'parameters': {'action_on_unpermitted': 'false',
                                                      'ancestors': ['ActionController::Parameters',
                                                                    'ActiveSupport::DeepMergeable',
                                                                    'ActiveSupport::Dependencies::RequireDependency',
                                                                    'Object',
                                                                    'ActiveSupport::ToJsonWithActiveSupportEncoder',
                                                                    'ActiveSupport::Tryable'],
                                                      'defined': True,
                                                      'is_hash': False,
                                                      'permit_all_parameters': False},
                                       'routes': {'available': True,
                                                  'count': 19,
                                                  'drawn_by_positional_hash': False,
                                                  'high_voltage_drawer_keyword_safe': True,
                                                  'high_voltage_draws_routes': True,
                                                  'high_voltage_version': '5.0.0',
                                                  'named': ['call_consent',
                                                            'call_location',
                                                            'call_message_playback',
                                                            'call_messages',
                                                            'call_question_answer',
                                                            'call_questions',
                                                            'calls',
                                                            'export',
                                                            'location',
                                                            'locations',
                                                            'numerical_answers',
                                                            'page',
                                                            'root',
                                                            'subscription',
                                                            'subscription_confirm',
                                                            'subscription_unsubscribe',
                                                            'voice_answers'],
                                                  'page_endpoint': 'high_voltage/pages#show',
                                                  'page_format_segment': False,
                                                  'page_named': True,
                                                  'page_route_migrated': True,
                                                  'page_spec': '/pages/*id',
                                                  'table': [{'name': None,
                                                             'spec': '/assets'},
                                                            {'name': 'voice_answers',
                                                             'spec': '/voice_answers(.:format)'},
                                                            {'name': 'numerical_answers',
                                                             'spec': '/numerical_answers(.:format)'},
                                                            {'name': 'export',
                                                             'spec': '/export(.:format)'},
                                                            {'name': 'subscription_confirm',
                                                             'spec': '/subscription/confirm(.:format)'},
                                                            {'name': 'subscription_unsubscribe',
                                                             'spec': '/subscription/unsubscribe(.:format)'},
                                                            {'name': 'subscription',
                                                             'spec': '/subscription(.:format)'},
                                                            {'name': None,
                                                             'spec': '/.well-known/status(.:format)'},
                                                            {'name': 'locations',
                                                             'spec': '/locations(.:format)'},
                                                            {'name': 'location',
                                                             'spec': '/locations/:id(.:format)'},
                                                            {'name': 'call_location',
                                                             'spec': '/calls/:call_id/location(.:format)'},
                                                            {'name': 'call_message_playback',
                                                             'spec': '/calls/:call_id/messages/:message_id/playback(.:format)'},
                                                            {'name': 'call_messages',
                                                             'spec': '/calls/:call_id/messages(.:format)'},
                                                            {'name': 'call_consent',
                                                             'spec': '/calls/:call_id/consent(.:format)'},
                                                            {'name': 'call_question_answer',
                                                             'spec': '/calls/:call_id/questions/:question_id/answer(.:format)'},
                                                            {'name': 'call_questions',
                                                             'spec': '/calls/:call_id/questions(.:format)'},
                                                            {'name': 'calls',
                                                             'spec': '/calls(.:format)'},
                                                            {'name': 'root',
                                                             'spec': '/'},
                                                            {'name': 'page',
                                                             'spec': '/pages/*id'}]},
                                       'secrets': {'assigned_length': 128,
                                                   'credentials_file_present': True,
                                                   'credentials_key_file_present': True,
                                                   'credentials_key_from_env': False,
                                                   'credentials_length': 128,
                                                   'credentials_present': True,
                                                   'credentials_template': False,
                                                   'effective_from_credentials': True,
                                                   'effective_from_encrypted': False,
                                                   'effective_from_secrets_yml': False,
                                                   'effective_key_length': 128,
                                                   'encrypted_key_file_present': False,
                                                   'encrypted_key_from_env': False,
                                                   'encrypted_length': 0,
                                                   'encrypted_present': False,
                                                   'from_secrets_yml': False,
                                                   'legacy_secret_store_files': [],
                                                   'plaintext_present': False,
                                                   'plaintext_secret_files': [],
                                                   'read_encrypted_secrets': None,
                                                   'reader_exists': False,
                                                   'secrets_yml_length': 0,
                                                   'secrets_yml_path': '/app/config/secrets.yml',
                                                   'secrets_yml_present': False,
                                                   'yml_file_length': 0},
                                       'versions': {'app_class': 'Automidnight::Application',
                                                    'env': 'production',
                                                    'rack': '2.2.24',
                                                    'rails': '8.1.3.1'},
                                       'zeitwerk_check': {'loaders': ['rails.main',
                                                                      'rails.once'],
                                                          'ok': True}},
                            'ruby': '3.4.10',
                            'ruby_platform': 'x86_64-linux',
                            'warnings': []},
              'depdrive': {'app': '/app',
                           'booted': True,
                           'count': 0,
                           'csp': {'directives': ['default-src',
                                                  'font-src',
                                                  'img-src',
                                                  'media-src',
                                                  'object-src',
                                                  'script-src',
                                                  'style-src'],
                                   'html_responses': 8,
                                   'inline_scripts': 12,
                                   'nonce_problems': [],
                                   'problems': [],
                                   'report_only': 0,
                                   'with_header': 8},
                           'csp_pass_deprecation_sites': [],
                           'csp_pass_deprecations': 0,
                           'csrf_token_found': True,
                           'deprecations': [],
                           'encryption': {'case_variant_finds_same_row': True,
                                          'email_limit': None,
                                          'email_sql_type': 'text',
                                          'errors': {},
                                          'find_by_answers': True,
                                          'find_by_finds': True,
                                          'find_or_create_by_reuses': True,
                                          'long_address_round_trips': True,
                                          'long_round_trip_answers': True,
                                          'long_stored_bytes': 283,
                                          'no_plaintext_at_rest': True,
                                          'ran': True,
                                          'stored_bytes': 122},
                           'encryption_pass_deprecations': 0,
                           'headers': {'always': {'Referrer-Policy': 'strict-origin-when-cross-origin',
                                                  'X-Content-Type-Options': 'nosniff',
                                                  'X-Frame-Options': 'SAMEORIGIN',
                                                  'X-Permitted-Cross-Domain-Policies': 'none',
                                                  'X-XSS-Protection': '0'},
                                       'html_responses': 8,
                                       'seen': ['Referrer-Policy',
                                                'X-Content-Type-Options',
                                                'X-Frame-Options',
                                                'X-Permitted-Cross-Domain-Policies',
                                                'X-XSS-Protection'],
                                       'sometimes': {}},
                           'hosts': {'blocks_suffix': True,
                                     'blocks_unknown': True,
                                     'known': 200,
                                     'loopback': 200,
                                     'serves_known': True,
                                     'suffix': 403,
                                     'unknown': 403},
                           'hosts_pass_deprecations': 0,
                           'own': [],
                           'own_count': 0,
                           'queries': {'export_csv': {'async_queries': 3,
                                                      'path': '/export.csv',
                                                      'preloaded': True,
                                                      'queries': 5,
                                                      'rows': 13,
                                                      'status': 200},
                                       'voice_answers': {'async_queries': 0,
                                                         'path': '/voice_answers',
                                                         'preloaded': True,
                                                         'queries': 4,
                                                         'rows': 12,
                                                         'status': 200}},
                           'queries_pass_deprecations': 0,
                           'raised': 0,
                           'ruby': '3.4.10',
                           'steps': [{'result': '', 'step': 'eager_load'},
                                     {'result': 'ok',
                                      'step': 'rake import:locations'},
                                     {'result': 'ok',
                                      'step': 'rake import:questions'},
                                     {'result': '200', 'step': 'GET /'},
                                     {'result': '406', 'step': 'GET /locations'},
                                     {'result': '200',
                                      'step': 'GET /locations.json'},
                                     {'result': '200', 'step': 'GET /locations/1'},
                                     {'result': '200',
                                      'step': 'GET /locations/1.json'},
                                     {'result': '200', 'step': 'GET /voice_answers'},
                                     {'result': '200',
                                      'step': 'GET /numerical_answers'},
                                     {'result': '200',
                                      'step': 'GET /numerical_answers.csv'},
                                     {'result': '200', 'step': 'GET /export.csv'},
                                     {'result': '500',
                                      'step': 'GET /.well-known/status'},
                                     {'result': '200', 'step': 'POST /calls'},
                                     {'result': '200',
                                      'step': 'POST /calls/:id/location'},
                                     {'result': '200',
                                      'step': 'POST /calls/:id/consent'},
                                     {'result': '200',
                                      'step': 'POST /calls/:id/questions'},
                                     {'result': '200',
                                      'step': 'POST /calls/:id/questions/0/answer'},
                                     {'result': '500', 'step': 'POST /subscription'},
                                     {'result': 'ok',
                                      'step': 'rake notifications:send'},
                                     {'result': '200',
                                      'step': 'CSP GET /pages/about'},
                                     {'result': '200',
                                      'step': 'CSP GET /pages/contact'},
                                     {'result': '200',
                                      'step': 'CSP GET /pages/messages'},
                                     {'result': '200', 'step': 'CSP GET /'},
                                     {'result': '14', 'step': 'strict locals'},
                                     {'result': '12 answers, 13 calls',
                                      'step': 'preload rows'},
                                     {'result': '200 in 4 queries (0 async)',
                                      'step': 'QUERIES GET /voice_answers'},
                                     {'result': '200 in 5 queries (3 async)',
                                      'step': 'QUERIES GET /export.csv'},
                                     {'result': 'at rest true, find_by true, case '
                                                'true, width true (283 bytes in '
                                                '"text")',
                                      'step': 'encryption round trip'},
                                     {'result': '200',
                                      'step': 'HOST cityvoice.test'},
                                     {'result': '200',
                                      'step': 'HOST 127.0.0.1:3000'},
                                     {'result': '403',
                                      'step': 'HOST evil.example.com'},
                                     {'result': '403',
                                      'step': 'HOST '
                                              'cityvoice.test.evil.example.com'}],
                           'strict_locals': {'accepts_unknown': ['locations/subscribe_form',
                                                                 'numerical_answers/vote',
                                                                 'numerical_answers/vote_meter',
                                                                 'voice_answers/voice_answers'],
                                             'not_strict': ['locations/subscribe_form',
                                                            'numerical_answers/vote',
                                                            'numerical_answers/vote_meter',
                                                            'voice_answers/voice_answers'],
                                             'partials': {'application/feedback': {'declaration': 'content:',
                                                                                   'probe_error': 'ActionView::Template::Error: '
                                                                                                  'unknown '
                                                                                                  'local: '
                                                                                                  ':cv_depdrive_strict_probe '
                                                                                                  'for '
                                                                                                  'app/views/application/_feedback.html.erb',
                                                                                   'refuses_unknown': True,
                                                                                   'strict': True},
                                                          'application/footer': {'declaration': 'content:',
                                                                                 'probe_error': 'ActionView::Template::Error: '
                                                                                                'unknown '
                                                                                                'local: '
                                                                                                ':cv_depdrive_strict_probe '
                                                                                                'for '
                                                                                                'app/views/application/_footer.html.erb',
                                                                                 'refuses_unknown': True,
                                                                                 'strict': True},
                                                          'application/header': {'declaration': 'content:',
                                                                                 'probe_error': 'ActionView::Template::Error: '
                                                                                                'unknown '
                                                                                                'local: '
                                                                                                ':cv_depdrive_strict_probe '
                                                                                                'for '
                                                                                                'app/views/application/_header.html.erb',
                                                                                 'refuses_unknown': True,
                                                                                 'strict': True},
                                                          'application/map': {'declaration': '**nil',
                                                                              'probe_error': 'ActionView::Template::Error: '
                                                                                             'no '
                                                                                             'locals '
                                                                                             'accepted '
                                                                                             'for '
                                                                                             'app/views/application/_map.html.erb',
                                                                              'refuses_unknown': True,
                                                                              'strict': True},
                                                          'landing/answer': {'declaration': 'answer:',
                                                                             'probe_error': 'ActionView::Template::Error: '
                                                                                            'unknown '
                                                                                            'local: '
                                                                                            ':cv_depdrive_strict_probe '
                                                                                            'for '
                                                                                            'app/views/landing/_answer.html.erb',
                                                                             'refuses_unknown': True,
                                                                             'strict': True},
                                                          'landing/info_block': {'declaration': 'title: '
                                                                                                'nil, '
                                                                                                'html_class: '
                                                                                                'nil',
                                                                                 'probe_error': 'ActionView::Template::Error: '
                                                                                                'unknown '
                                                                                                'local: '
                                                                                                ':cv_depdrive_strict_probe '
                                                                                                'for '
                                                                                                'app/views/landing/_info_block.html.erb',
                                                                                 'refuses_unknown': True,
                                                                                 'strict': True},
                                                          'locations/email_notifications': {'declaration': 'location:',
                                                                                            'probe_error': 'ActionView::Template::Error: '
                                                                                                           'unknown '
                                                                                                           'local: '
                                                                                                           ':cv_depdrive_strict_probe '
                                                                                                           'for '
                                                                                                           'app/views/locations/_email_notifications.html.erb',
                                                                                            'refuses_unknown': True,
                                                                                            'strict': True},
                                                          'locations/numerical_response': {'declaration': 'numerical_response:',
                                                                                           'probe_error': 'ActionView::Template::Error: '
                                                                                                          'unknown '
                                                                                                          'local: '
                                                                                                          ':cv_depdrive_strict_probe '
                                                                                                          'for '
                                                                                                          'app/views/locations/_numerical_response.html.erb',
                                                                                           'refuses_unknown': True,
                                                                                           'strict': True},
                                                          'locations/subscribe_form': {'declaration': '',
                                                                                       'probe_error': 'ActionView::Template::Error: '
                                                                                                      'undefined '
                                                                                                      'method '
                                                                                                      "'id' "
                                                                                                      'for '
                                                                                                      'nil',
                                                                                       'refuses_unknown': False,
                                                                                       'strict': False},
                                                          'locations/voice_message': {'declaration': 'answer:',
                                                                                      'probe_error': 'ActionView::Template::Error: '
                                                                                                     'unknown '
                                                                                                     'local: '
                                                                                                     ':cv_depdrive_strict_probe '
                                                                                                     'for '
                                                                                                     'app/views/locations/_voice_message.html.erb',
                                                                                      'refuses_unknown': True,
                                                                                      'strict': True},
                                                          'numerical_answers/vote': {'declaration': '',
                                                                                     'probe_error': 'ActionView::Template::Error: '
                                                                                                    'undefined '
                                                                                                    'local '
                                                                                                    'variable '
                                                                                                    'or '
                                                                                                    'method '
                                                                                                    "'response_set' "
                                                                                                    'for '
                                                                                                    'an '
                                                                                                    'instance '
                                                                                                    'of '
                                                                                                    '#<Class:0x00007fd80b527958>',
                                                                                     'refuses_unknown': False,
                                                                                     'strict': False},
                                                          'numerical_answers/vote_meter': {'declaration': '',
                                                                                           'probe_error': 'ActionView::Template::Error: '
                                                                                                          'undefined '
                                                                                                          'local '
                                                                                                          'variable '
                                                                                                          'or '
                                                                                                          'method '
                                                                                                          "'number' "
                                                                                                          'for '
                                                                                                          'an '
                                                                                                          'instance '
                                                                                                          'of '
                                                                                                          '#<Class:0x00007fd80b527958>',
                                                                                           'refuses_unknown': False,
                                                                                           'strict': False},
                                                          'voice_answers/answer': {'declaration': 'answer:',
                                                                                   'probe_error': 'ActionView::Template::Error: '
                                                                                                  'unknown '
                                                                                                  'local: '
                                                                                                  ':cv_depdrive_strict_probe '
                                                                                                  'for '
                                                                                                  'app/views/voice_answers/_answer.html.erb',
                                                                                   'refuses_unknown': True,
                                                                                   'strict': True},
                                                          'voice_answers/voice_answers': {'declaration': '',
                                                                                          'probe_error': 'ActionView::Template::Error: '
                                                                                                         'undefined '
                                                                                                         'local '
                                                                                                         'variable '
                                                                                                         'or '
                                                                                                         'method '
                                                                                                         "'answers' "
                                                                                                         'for '
                                                                                                         'an '
                                                                                                         'instance '
                                                                                                         'of '
                                                                                                         '#<Class:0x00007fd80b527958>',
                                                                                          'refuses_unknown': False,
                                                                                          'strict': False}},
                                             'probed': 14,
                                             'refuses_unknown': ['application/feedback',
                                                                 'application/footer',
                                                                 'application/header',
                                                                 'application/map',
                                                                 'landing/answer',
                                                                 'landing/info_block',
                                                                 'locations/email_notifications',
                                                                 'locations/numerical_response',
                                                                 'locations/voice_message',
                                                                 'voice_answers/answer'],
                                             'strict': ['application/feedback',
                                                        'application/footer',
                                                        'application/header',
                                                        'application/map',
                                                        'landing/answer',
                                                        'landing/info_block',
                                                        'locations/email_notifications',
                                                        'locations/numerical_response',
                                                        'locations/voice_message',
                                                        'voice_answers/answer']}},
              'envboot': {'app': '/app',
                          'bundled_gem_stdlib': [],
                          'checked': ['development', 'test'],
                          'declared': ['development', 'production', 'test'],
                          'env_file_deprecations': [],
                          'envs': {'development': {'booted': True,
                                                   'bundled_gem_stdlib': [],
                                                   'deprecation_count': 0,
                                                   'eager_load': False,
                                                   'env': 'development',
                                                   'exceptions_let_out': False,
                                                   'exceptions_shown': True,
                                                   'exceptions_shown_rescuable': True,
                                                   'middleware': ['ActionDispatch::HostAuthorization',
                                                                  'Rack::Sendfile',
                                                                  'ActionDispatch::Static',
                                                                  'Rack::LiveReload',
                                                                  'ActionDispatch::Executor',
                                                                  'Rack::Runtime',
                                                                  'Rack::MethodOverride',
                                                                  'ActionDispatch::RequestId',
                                                                  'ActionDispatch::RemoteIp',
                                                                  'Rails::Rack::Logger',
                                                                  'ActionDispatch::ShowExceptions',
                                                                  'ActionDispatch::DebugExceptions',
                                                                  'ActionDispatch::ActionableExceptions',
                                                                  'ActionDispatch::Reloader',
                                                                  'ActionDispatch::Callbacks',
                                                                  'ActiveRecord::Migration::CheckPending',
                                                                  'ActionDispatch::Cookies',
                                                                  'ActionDispatch::Session::CookieStore',
                                                                  'ActionDispatch::Flash',
                                                                  'ActionDispatch::ContentSecurityPolicy::Middleware',
                                                                  'Rack::Head',
                                                                  'Rack::ConditionalGet',
                                                                  'Rack::ETag',
                                                                  'Rack::TempfileReaper'],
                                                   'own': [],
                                                   'own_deprecation_count': 0,
                                                   'rails': '8.1.3.1',
                                                   'rails_env': 'development',
                                                   'show_exceptions': ':all'},
                                   'test': {'booted': True,
                                            'bundled_gem_stdlib': [],
                                            'deprecation_count': 0,
                                            'eager_load': False,
                                            'env': 'test',
                                            'exceptions_let_out': True,
                                            'exceptions_shown': False,
                                            'exceptions_shown_rescuable': False,
                                            'middleware': ['Rack::Sendfile',
                                                           'ActionDispatch::Static',
                                                           'ActionDispatch::Executor',
                                                           'Rack::Runtime',
                                                           'Rack::MethodOverride',
                                                           'ActionDispatch::RequestId',
                                                           'ActionDispatch::RemoteIp',
                                                           'Rails::Rack::Logger',
                                                           'ActionDispatch::ShowExceptions',
                                                           'ActionDispatch::DebugExceptions',
                                                           'ActionDispatch::ActionableExceptions',
                                                           'ActionDispatch::Callbacks',
                                                           'ActionDispatch::Cookies',
                                                           'ActionDispatch::Session::CookieStore',
                                                           'ActionDispatch::Flash',
                                                           'ActionDispatch::ContentSecurityPolicy::Middleware',
                                                           'Rack::Head',
                                                           'Rack::ConditionalGet',
                                                           'Rack::ETag',
                                                           'Rack::TempfileReaper'],
                                            'own': [],
                                            'own_deprecation_count': 0,
                                            'rails': '8.1.3.1',
                                            'rails_env': 'test',
                                            'show_exceptions': ':none'}},
                          'expected': ['development', 'test'],
                          'failed': [],
                          'own_deprecation_count': 0,
                          'ruby': '3.4.10',
                          'skipped': ['production']},
              'gemgraph': {'bundled_with': '2.6.9',
                           'bundler_agrees': True,
                           'bundler_error': None,
                           'direct': ['annotaterb',
                                      'backbone-rails',
                                      'bigdecimal',
                                      'bootsnap',
                                      'capybara',
                                      'coffee-rails',
                                      'concurrent-ruby',
                                      'csv',
                                      'dotenv-rails',
                                      'factory_bot_rails',
                                      'ffaker',
                                      'foreman',
                                      'foundation-rails',
                                      'google-analytics-rails',
                                      'guard-livereload',
                                      'guard-teaspoon',
                                      'high_voltage',
                                      'jbuilder',
                                      'jquery-rails',
                                      'json',
                                      'launchy',
                                      'leaflet-rails',
                                      'msgpack',
                                      'mutex_m',
                                      'newrelic_rpm',
                                      'observer',
                                      'ostruct',
                                      'pg',
                                      'pry',
                                      'rack-livereload',
                                      'rails',
                                      'rails-controller-testing',
                                      'redcarpet',
                                      'rspec-its',
                                      'rspec-rails',
                                      'sass',
                                      'sass-rails',
                                      'shoulda-matchers',
                                      'soda-ruby',
                                      'teaspoon',
                                      'timecop',
                                      'twilio-ruby',
                                      'twitter-typeahead-rails',
                                      'uglifier',
                                      'unicorn',
                                      'vcr',
                                      'webmock',
                                      'will_paginate'],
                           'gemfile_ruby': '3.4.10',
                           'lockfile': '/app/Gemfile.lock',
                           'orphans': [],
                           'reachable': ['action_text-trix',
                                         'actioncable',
                                         'actionmailbox',
                                         'actionmailer',
                                         'actionpack',
                                         'actiontext',
                                         'actionview',
                                         'activejob',
                                         'activemodel',
                                         'activerecord',
                                         'activestorage',
                                         'activesupport',
                                         'addressable',
                                         'annotaterb',
                                         'backbone-rails',
                                         'base64',
                                         'bigdecimal',
                                         'bootsnap',
                                         'builder',
                                         'bundler',
                                         'capybara',
                                         'coderay',
                                         'coffee-rails',
                                         'coffee-script',
                                         'coffee-script-source',
                                         'concurrent-ruby',
                                         'connection_pool',
                                         'crack',
                                         'crass',
                                         'csv',
                                         'date',
                                         'diff-lcs',
                                         'dotenv',
                                         'dotenv-rails',
                                         'drb',
                                         'em-websocket',
                                         'erb',
                                         'erubi',
                                         'eventmachine',
                                         'execjs',
                                         'factory_bot',
                                         'factory_bot_rails',
                                         'ffaker',
                                         'ffi',
                                         'foreman',
                                         'formatador',
                                         'foundation-rails',
                                         'globalid',
                                         'google-analytics-rails',
                                         'guard',
                                         'guard-compat',
                                         'guard-livereload',
                                         'guard-teaspoon',
                                         'hashdiff',
                                         'hashie',
                                         'high_voltage',
                                         'http_parser.rb',
                                         'i18n',
                                         'io-console',
                                         'irb',
                                         'jbuilder',
                                         'jquery-rails',
                                         'json',
                                         'jwt',
                                         'kgio',
                                         'launchy',
                                         'leaflet-rails',
                                         'listen',
                                         'logger',
                                         'loofah',
                                         'lumberjack',
                                         'mail',
                                         'marcel',
                                         'matrix',
                                         'method_source',
                                         'mini_mime',
                                         'mini_portile2',
                                         'minitest',
                                         'msgpack',
                                         'multi_json',
                                         'mutex_m',
                                         'nenv',
                                         'net-imap',
                                         'net-pop',
                                         'net-protocol',
                                         'net-smtp',
                                         'newrelic_rpm',
                                         'nio4r',
                                         'nokogiri',
                                         'notiffany',
                                         'observer',
                                         'ostruct',
                                         'pg',
                                         'pp',
                                         'prettyprint',
                                         'prism',
                                         'pry',
                                         'public_suffix',
                                         'racc',
                                         'rack',
                                         'rack-livereload',
                                         'rack-session',
                                         'rack-test',
                                         'rackup',
                                         'rails',
                                         'rails-controller-testing',
                                         'rails-dom-testing',
                                         'rails-html-sanitizer',
                                         'railties',
                                         'raindrops',
                                         'rake',
                                         'rb-fsevent',
                                         'rb-inotify',
                                         'rbs',
                                         'rdoc',
                                         'redcarpet',
                                         'regexp_parser',
                                         'reline',
                                         'rexml',
                                         'rspec-core',
                                         'rspec-expectations',
                                         'rspec-its',
                                         'rspec-mocks',
                                         'rspec-rails',
                                         'rspec-support',
                                         'sass',
                                         'sass-rails',
                                         'securerandom',
                                         'shellany',
                                         'shoulda-matchers',
                                         'soda-ruby',
                                         'sprockets',
                                         'sprockets-rails',
                                         'teaspoon',
                                         'thor',
                                         'tilt',
                                         'timecop',
                                         'timeout',
                                         'tsort',
                                         'twilio-ruby',
                                         'twitter-typeahead-rails',
                                         'tzinfo',
                                         'uglifier',
                                         'unicorn',
                                         'uri',
                                         'useragent',
                                         'vcr',
                                         'webmock',
                                         'webrick',
                                         'websocket-driver',
                                         'websocket-extensions',
                                         'will_paginate',
                                         'xpath',
                                         'zeitwerk'],
                           'root': '/app',
                           'ruby': '3.4.10p104',
                           'ruby_declared': '3.4.10',
                           'ruby_version_file': '3.4.10',
                           'sources': ['https://rubygems.org/'],
                           'specs': {'action_text-trix': '2.1.19',
                                     'actioncable': '8.1.3.1',
                                     'actionmailbox': '8.1.3.1',
                                     'actionmailer': '8.1.3.1',
                                     'actionpack': '8.1.3.1',
                                     'actiontext': '8.1.3.1',
                                     'actionview': '8.1.3.1',
                                     'activejob': '8.1.3.1',
                                     'activemodel': '8.1.3.1',
                                     'activerecord': '8.1.3.1',
                                     'activestorage': '8.1.3.1',
                                     'activesupport': '8.1.3.1',
                                     'addressable': '2.9.0',
                                     'annotaterb': '4.25.0',
                                     'backbone-rails': '1.1.0',
                                     'base64': '0.3.0',
                                     'bigdecimal': '3.1.8',
                                     'bootsnap': '1.9.3',
                                     'builder': '3.3.0',
                                     'capybara': '3.40.0',
                                     'coderay': '1.1.3',
                                     'coffee-rails': '5.0.0',
                                     'coffee-script': '2.4.1',
                                     'coffee-script-source': '1.12.2',
                                     'concurrent-ruby': '1.3.7',
                                     'connection_pool': '3.0.2',
                                     'crack': '1.0.1',
                                     'crass': '1.0.7',
                                     'csv': '3.3.2',
                                     'date': '3.5.1',
                                     'diff-lcs': '1.6.2',
                                     'dotenv': '2.8.1',
                                     'dotenv-rails': '2.8.1',
                                     'drb': '2.2.3',
                                     'em-websocket': '0.5.3',
                                     'erb': '6.0.7',
                                     'erubi': '1.13.1',
                                     'eventmachine': '1.2.7',
                                     'execjs': '2.9.0',
                                     'factory_bot': '5.2.0',
                                     'factory_bot_rails': '5.2.0',
                                     'ffaker': '2.11.0',
                                     'ffi': '1.15.5',
                                     'foreman': '0.89.1',
                                     'formatador': '1.2.3',
                                     'foundation-rails': '5.5.3.2',
                                     'globalid': '1.4.0',
                                     'google-analytics-rails': '0.0.5',
                                     'guard': '2.19.0',
                                     'guard-compat': '1.2.1',
                                     'guard-livereload': '2.5.2',
                                     'guard-teaspoon': '0.8.0',
                                     'hashdiff': '1.2.1',
                                     'hashie': '5.0.0',
                                     'high_voltage': '5.0.0',
                                     'http_parser.rb': '0.8.1',
                                     'i18n': '1.14.8',
                                     'io-console': '0.9.4',
                                     'irb': '1.18.0',
                                     'jbuilder': '2.13.0',
                                     'jquery-rails': '4.6.1',
                                     'json': '2.7.6',
                                     'jwt': '3.3.0',
                                     'kgio': '2.11.4',
                                     'launchy': '2.5.2',
                                     'leaflet-rails': '0.7.2',
                                     'listen': '3.5.0',
                                     'logger': '1.7.0',
                                     'loofah': '2.25.2',
                                     'lumberjack': '1.2.10',
                                     'mail': '2.9.1',
                                     'marcel': '1.2.1',
                                     'matrix': '0.4.3',
                                     'method_source': '1.1.0',
                                     'mini_mime': '1.1.5',
                                     'mini_portile2': '2.8.9',
                                     'minitest': '5.15.0',
                                     'msgpack': '1.8.2',
                                     'multi_json': '1.15.0',
                                     'mutex_m': '0.3.0',
                                     'nenv': '0.3.0',
                                     'net-imap': '0.5.15',
                                     'net-pop': '0.1.2',
                                     'net-protocol': '0.4.0',
                                     'net-smtp': '0.5.1',
                                     'newrelic_rpm': '8.16.0',
                                     'nio4r': '2.7.5',
                                     'nokogiri': '1.19.4',
                                     'notiffany': '0.1.3',
                                     'observer': '0.1.2',
                                     'ostruct': '0.6.1',
                                     'pg': '1.4.6',
                                     'pp': '0.6.4',
                                     'prettyprint': '0.2.0',
                                     'prism': '1.9.0',
                                     'pry': '0.15.2',
                                     'public_suffix': '4.0.7',
                                     'racc': '1.8.1',
                                     'rack': '2.2.24',
                                     'rack-livereload': '0.5.2',
                                     'rack-session': '1.0.2',
                                     'rack-test': '2.2.0',
                                     'rackup': '1.0.1',
                                     'rails': '8.1.3.1',
                                     'rails-controller-testing': '1.0.5',
                                     'rails-dom-testing': '2.3.0',
                                     'rails-html-sanitizer': '1.7.1',
                                     'railties': '8.1.3.1',
                                     'raindrops': '0.20.1',
                                     'rake': '13.4.2',
                                     'rb-fsevent': '0.11.2',
                                     'rb-inotify': '0.10.1',
                                     'rbs': '4.1.3',
                                     'rdoc': '8.0.0',
                                     'redcarpet': '3.6.1',
                                     'regexp_parser': '2.12.0',
                                     'reline': '0.7.0',
                                     'rexml': '3.4.4',
                                     'rspec-core': '3.13.6',
                                     'rspec-expectations': '3.13.5',
                                     'rspec-its': '1.3.1',
                                     'rspec-mocks': '3.13.8',
                                     'rspec-rails': '6.1.5',
                                     'rspec-support': '3.13.7',
                                     'sass': '3.4.25',
                                     'sass-rails': '5.0.8',
                                     'securerandom': '0.4.1',
                                     'shellany': '0.0.1',
                                     'shoulda-matchers': '4.5.1',
                                     'soda-ruby': '0.2.12',
                                     'sprockets': '3.7.5',
                                     'sprockets-rails': '3.2.2',
                                     'teaspoon': '1.4.0',
                                     'thor': '1.2.2',
                                     'tilt': '2.9.0',
                                     'timecop': '0.9.11',
                                     'timeout': '0.6.1',
                                     'tsort': '0.2.0',
                                     'twilio-ruby': '3.11.5',
                                     'twitter-typeahead-rails': '0.10.2',
                                     'tzinfo': '2.0.6',
                                     'uglifier': '4.2.1',
                                     'unicorn': '4.8.2',
                                     'uri': '1.1.1',
                                     'useragent': '0.16.11',
                                     'vcr': '6.0.0',
                                     'webmock': '2.3.2',
                                     'webrick': '1.9.2',
                                     'websocket-driver': '0.8.2',
                                     'websocket-extensions': '0.1.5',
                                     'will_paginate': '3.3.1',
                                     'xpath': '3.2.0',
                                     'zeitwerk': '2.6.18'},
                           'unresolved': ['bundler']},
              'handoff': {'database_yml_example_present': True,
                          'database_yml_present': False,
                          'db_config_problems': []},
              'logpii': {'bytes': 266420,
                         'completed_lines': 293,
                         'dir': '/logs/verifier/cvh',
                         'email': {'distinct': 0,
                                   'lines': 0,
                                   'outside_parameters': 0,
                                   'samples': []},
                         'files': ['empty-server.log',
                                   'http_edges-server.log',
                                   'imports-server.log',
                                   'ivr_adversarial-server.log',
                                   'ivr_happy-server.log',
                                   'locked-server.log',
                                   'migrate-server.log',
                                   'seeded-server.log',
                                   'status-server.log',
                                   'subscriptions-server.log'],
                         'filtered_markers': 307,
                         'lines': 2673,
                         'parameter_lines': 202,
                         'phone': {'distinct': 0,
                                   'lines': 0,
                                   'outside_parameters': 0,
                                   'samples': []},
                         'started_lines': 306},
              'meta': {'bundle_status': '0',
                       'preflight': 'ok',
                       'pruned': {'assets.findings': True,
                                  'audit.vulnerable': True,
                                  'behaviour.results': True,
                                  'gemgraph.paths': True,
                                  'logpii.per_file': True,
                                  'scan.findings': True},
                       'sources': {'assets': {'bytes': 2623,
                                              'error': None,
                                              'path': '/tmp/gates/assets.json'},
                                   'audit': {'bytes': 560,
                                             'error': None,
                                             'path': '/tmp/gates/audit.json'},
                                   'behaviour': {'bytes': 27988,
                                                 'error': None,
                                                 'path': '/tmp/gates/behaviour.json'},
                                   'bootprobe': {'bytes': 20597,
                                                 'error': None,
                                                 'path': '/tmp/gates/bootprobe.json'},
                                   'depdrive': {'bytes': 9515,
                                                'error': None,
                                                'path': '/tmp/gates/depdrive.json'},
                                   'envboot': {'bytes': 2873,
                                               'error': None,
                                               'path': '/tmp/gates/envboot.json'},
                                   'gemgraph': {'bytes': 18533,
                                                'error': None,
                                                'path': '/tmp/gates/gemgraph.json'},
                                   'handoff': {'bytes': 95,
                                               'error': None,
                                               'path': '/tmp/gates/handoff.json'},
                                   'logpii': {'bytes': 1398,
                                              'error': None,
                                              'path': '/tmp/gates/logpii.json'},
                                   'pkdrift': {'bytes': 2256,
                                               'error': None,
                                               'path': '/tmp/gates/pkdrift.json'},
                                   'rake': {'bytes': 1289,
                                            'error': None,
                                            'path': '/tmp/gates/rake.json'},
                                   'rspec': {'bytes': 569,
                                             'error': None,
                                             'path': '/tmp/gates/rspec.json'},
                                   'scan': {'bytes': 4560,
                                            'error': None,
                                            'path': '/tmp/gates/scan.json'}},
                       'step': '13-hardening'},
              'pkdrift': {'agree': True,
                          'booted': True,
                          'both_narrow': True,
                          'default_primary_key': 'bigserial primary key',
                          'differing_spellings': {},
                          'differing_tables': {},
                          'dump': {'id_sql_types': {'answers': 'integer',
                                                    'callers': 'integer',
                                                    'calls': 'integer',
                                                    'location_subscriptions': 'integer',
                                                    'locations': 'integer',
                                                    'questions': 'integer',
                                                    'subscribers': 'integer'},
                                   'id_widths': {'answers': 'narrow',
                                                 'callers': 'narrow',
                                                 'calls': 'narrow',
                                                 'location_subscriptions': 'narrow',
                                                 'locations': 'narrow',
                                                 'questions': 'narrow',
                                                 'subscribers': 'narrow'},
                                   'narrow_id_tables': ['answers',
                                                        'callers',
                                                        'calls',
                                                        'location_subscriptions',
                                                        'locations',
                                                        'questions',
                                                        'subscribers'],
                                   'odd_id_tables': [],
                                   'tables': ['answers',
                                              'callers',
                                              'calls',
                                              'location_subscriptions',
                                              'locations',
                                              'questions',
                                              'subscribers'],
                                   'wide_id_tables': []},
                          'dump_source': '/tmp/gates/pk-dump.json',
                          'migrate_exit': 0,
                          'migrated': {'id_sql_types': {'answers': 'integer',
                                                        'callers': 'integer',
                                                        'calls': 'integer',
                                                        'location_subscriptions': 'integer',
                                                        'locations': 'integer',
                                                        'questions': 'integer',
                                                        'subscribers': 'integer'},
                                       'id_widths': {'answers': 'narrow',
                                                     'callers': 'narrow',
                                                     'calls': 'narrow',
                                                     'location_subscriptions': 'narrow',
                                                     'locations': 'narrow',
                                                     'questions': 'narrow',
                                                     'subscribers': 'narrow'},
                                       'narrow_id_tables': ['answers',
                                                            'callers',
                                                            'calls',
                                                            'location_subscriptions',
                                                            'locations',
                                                            'questions',
                                                            'subscribers'],
                                       'odd_id_tables': [],
                                       'tables': ['answers',
                                                  'callers',
                                                  'calls',
                                                  'location_subscriptions',
                                                  'locations',
                                                  'questions',
                                                  'subscribers'],
                                       'wide_id_tables': []},
                          'migrated_source': '/tmp/gates/pk-migrated.json',
                          'migrations_applied': 44,
                          'pinned': True,
                          'problems': [],
                          'ruby': '3.4.10',
                          'schema_load_exit': 0,
                          'tables_only_in_dump': [],
                          'tables_only_in_migrated': [],
                          'wide_default_primary_key': True},
              'rake': {'annotate_tasks': 0,
                       'app': '/app',
                       'command': 'bundle exec rake -T',
                       'env': 'development',
                       'exit_status': 0,
                       'has_db_migrate': True,
                       'ok': True,
                       'ruby': '3.4.10',
                       'signalled': False,
                       'stderr_head': [],
                       'task_count': 46,
                       'tasks': ['app:template',
                                 'assets:clean[keep]',
                                 'assets:clobber',
                                 'assets:environment',
                                 'assets:precompile',
                                 'cache_digests:dependencies',
                                 'cache_digests:nested_dependencies',
                                 'db:create',
                                 'db:drop',
                                 'db:encryption:init',
                                 'db:environment:set',
                                 'db:fixtures:load',
                                 'db:migrate',
                                 'db:migrate:down',
                                 'db:migrate:redo',
                                 'db:migrate:reset',
                                 'db:migrate:status',
                                 'db:migrate:up',
                                 'db:prepare',
                                 'db:reset',
                                 'db:rollback',
                                 'db:schema:cache:clear',
                                 'db:schema:cache:dump',
                                 'db:schema:dump',
                                 'db:schema:load',
                                 'db:seed',
                                 'db:seed:replant',
                                 'db:setup',
                                 'db:version',
                                 'import:locations[path]',
                                 'import:questions[path]',
                                 'log:clear',
                                 'spec',
                                 'spec:controllers',
                                 'spec:features',
                                 'spec:helpers',
                                 'spec:lib',
                                 'spec:mailers',
                                 'spec:models',
                                 'teaspoon',
                                 'teaspoon:info',
                                 'time:zones[country_or_offset]',
                                 'tmp:clear',
                                 'tmp:create',
                                 'yarn:install',
                                 'zeitwerk:check']},
              'rspec': {'collected_deprecations': 0,
                        'collector': {'active_support': '8.1.3.1',
                                      'framework_silenced': 0,
                                      'installed': True,
                                      'lines': 3,
                                      'malformed': 0,
                                      'records': 0,
                                      'ruby': '3.4.10'},
                        'db_exit': 0,
                        'deprecations': 0,
                        'deprecations_by_file': {},
                        'examples': 504,
                        'exit': 0,
                        'failures': 0,
                        'inherited_rspec_deprecations': 1,
                        'ok': True,
                        'own_rspec_deprecation_sites': [],
                        'own_rspec_deprecations': 0,
                        'pending': 0,
                        'ran': True,
                        'rspec_deprecations': 1,
                        'rspec_hook': {'installed': True,
                                       'late': False,
                                       'rspec': '3.13.6'},
                        'suite_deprecation_sites': [],
                        'suite_deprecations': 0},
              'scan': {'annotations': {'columns': 46,
                                       'count': 7,
                                       'digest': 'c5dd17db4d81',
                                       'lines': 81,
                                       'models': {'app/models/answer.rb': {'columns': 7,
                                                                           'digest': '66e298d2f867',
                                                                           'lines': 12},
                                                  'app/models/call.rb': {'columns': 7,
                                                                         'digest': 'ee8f1d59692c',
                                                                         'lines': 12},
                                                  'app/models/caller.rb': {'columns': 4,
                                                                           'digest': '859ab859230f',
                                                                           'lines': 9},
                                                  'app/models/location.rb': {'columns': 8,
                                                                             'digest': '837c4a5cfa36',
                                                                             'lines': 13},
                                                  'app/models/location_subscription.rb': {'columns': 10,
                                                                                          'digest': '597a33c539c2',
                                                                                          'lines': 15},
                                                  'app/models/question.rb': {'columns': 6,
                                                                             'digest': '4f79be7d68ca',
                                                                             'lines': 11},
                                                  'app/models/subscriber.rb': {'columns': 4,
                                                                               'digest': '202c1f57622c',
                                                                               'lines': 9}}},
                       'assertion_lines': 310,
                       'counts': {'alias_method_chain': 0,
                                  'annotate_legacy_api': 0,
                                  'ar_base_connection': 0,
                                  'attr_accessible': 0,
                                  'before_filter': 0,
                                  'dead_engine_ref': 0,
                                  'exists_predicate': 0,
                                  'fixnum': 0,
                                  'form_for': 0,
                                  'form_with': 2,
                                  'framework_defaults_switch': 0,
                                  'load_defaults': 1,
                                  'load_defaults_5_1': 0,
                                  'mailer_positional': 0,
                                  'misversioned_migration': 1,
                                  'random_default': 0,
                                  'redirect_to_back': 0,
                                  'render_text': 0,
                                  'safe_global': 0,
                                  'scope_without_lambda': 0,
                                  'secret_key_base_assignment': 0,
                                  'secret_token': 0,
                                  'strict_loading': 3,
                                  'suppression': 0,
                                  'taint': 0,
                                  'unversioned_migration': 0,
                                  'update_attributes': 0,
                                  'uri_escape': 0},
                       'emptied_tests': [],
                       'erb_scanned': 28,
                       'files_scanned': 180,
                       'gutted_tests': [],
                       'hollow_tests': [],
                       'migrations': {'shipped_total': 43,
                                      'shipped_unversioned': 0,
                                      'shipped_versions': {'4_2': 43},
                                      'total': 44,
                                      'unversioned': 0,
                                      'versions': {'4_2': 43, '7_0': 1}},
                       'missing_core': [],
                       'missing_tests': [],
                       'not_evaluated': ['sprockets_directive'],
                       'parse_errors': [],
                       'pristine': '/tests/step/pristine.json',
                       'root': '/app',
                       'rules': ['attr_accessible',
                                 'secret_key_base_assignment',
                                 'dead_engine_ref',
                                 'scope_without_lambda',
                                 'before_filter',
                                 'unversioned_migration',
                                 'misversioned_migration',
                                 'redirect_to_back',
                                 'alias_method_chain',
                                 'render_text',
                                 'secret_token',
                                 'form_for',
                                 'form_with',
                                 'load_defaults',
                                 'load_defaults_5_1',
                                 'framework_defaults_switch',
                                 'mailer_positional',
                                 'update_attributes',
                                 'strict_loading',
                                 'ar_base_connection',
                                 'annotate_legacy_api',
                                 'sprockets_directive',
                                 'fixnum',
                                 'taint',
                                 'safe_global',
                                 'uri_escape',
                                 'exists_predicate',
                                 'random_default',
                                 'suppression'],
                       'total': 7}},
}

# The gates each step is expected to have, and what they are worth in total.
# Written here rather than counted off the profile: a profile that lost a gate
# to a bad edit would otherwise agree with whatever it now says.
EXPECTED_BY_STEP = {
    "01": {
        "weight": 35,
        "gates": ["before_action", "boots", "dead_engine_refs", "dead_gems",
                  "eager_load", "env_boots", "env_deprecations",
                  "from_secrets_yml", "no_secret_key_base_assignment",
                  "no_shim", "own_deprecations", "own_suite", "rails_version",
                  "ruby_declared", "ruby_runtime", "secrets_yml",
                  "strong_parameters", "suite_size"],
    },
    "03": {
        "weight": 35,
        "gates": ["asset_strict_error", "asset_strict_helper",
                  "encrypted_key_shipped", "encrypted_secrets", "form_with",
                  "load_defaults_5_1", "no_form_for",
                  "no_framework_defaults_file",
                  "no_framework_defaults_switches", "no_plaintext_secrets",
                  "no_positional_mailer_calls", "no_string_conditions",
                  "parameterized_mailer", "rails_version",
                  "read_encrypted_secrets", "schema_pins_primary_key"],
        # Eighteen more than the standing nine.  Every one of them is a
        # property step 02's answer already has -- it boots, it eager loads,
        # its suite passes, its migrations are versioned, its 5.0 defaults are
        # adopted, its one optional `belongs_to` is named -- so gating any of
        # them would be weight the handed-on tree collects for free.
        # `migrations_still_4_2` is here for the same reason: a tree whose
        # migrations still say 4.2 passes it without doing anything this step
        # asks.
        "guards": ["application_record", "belongs_to_not_all_optional",
                   "belongs_to_optional_named", "belongs_to_required", "boots",
                   "db_config_from_example", "eager_load",
                   "eager_load_outside_app", "env_boots", "env_deprecations",
                   "framework_defaults",
                   "migrations_still_4_2", "no_direct_base", "no_suppression",
                   "own_deprecations", "own_suite", "parses", "ruby_runtime",
                   "suite_deprecations", "suite_size", "tests_intact",
                   "tests_not_emptied", "tests_not_gutted",
                   "tests_not_hollow_files", "tests_not_hollowed",
                   "tree_intact", "versioned_migrations"],
    },
    "02": {
        "weight": 43,
        "gates": ["application_record", "application_record_abstract",
                  "belongs_to_not_all_optional", "belongs_to_optional_named",
                  "belongs_to_required", "boots", "eager_load",
                  "eager_load_outside_app", "env_boots",
                  "env_deprecations", "framework_defaults",
                  "migration_semantics", "no_direct_base", "own_deprecations",
                  "own_suite", "rails_version", "ruby_declared",
                  "ruby_runtime", "suite_deprecations", "suite_size",
                  "versioned_migrations"],
    },
    "04": {
        "weight": 31,
        "gates": ["bootsnap_active", "bootsnap_caches", "credentials_effective",
                  "csp_configured", "csp_nonce_generator", "csp_nonced",
                  "csp_served", "defaults_5_2_adopted",
                  "form_with_generates_ids", "load_defaults_5_2",
                  "master_key_shipped", "no_legacy_secret_store",
                  "no_read_encrypted_secrets", "rails_version"],
        # Twenty-nine more than the standing nine, which is eleven more than
        # step 03 carried: this is the second consecutive step that does not
        # move the interpreter, so everything step 03 gated is now a property
        # the handed-on tree already has, and a gate for it would be a free
        # point.  Three of step 03's gates are not here in either
        # role -- `encrypted_secrets`, `read_encrypted_secrets` and
        # `encrypted_key_shipped` asked for the 5.1 store this step removes,
        # and a ladder that required a thing and then required its removal one
        # rung later would be scoring its own shape.
        "guards": ["application_record", "asset_strict_error",
                   "asset_strict_helper", "belongs_to_not_all_optional",
                   "belongs_to_optional_named", "belongs_to_required", "boots",
                   "db_config_from_example", "eager_load",
                   "eager_load_outside_app", "env_boots", "env_deprecations",
                   "form_with", "framework_defaults", "migrations_still_4_2",
                   "no_direct_base", "no_form_for",
                   "no_framework_defaults_file",
                   "no_framework_defaults_switches", "no_plaintext_secrets",
                   "no_positional_mailer_calls", "no_string_conditions",
                   "no_suppression", "own_deprecations", "own_suite",
                   "parameterized_mailer", "parses", "ruby_runtime",
                   "schema_pins_primary_key", "suite_deprecations",
                   "suite_size", "tests_intact", "tests_not_emptied",
                   "tests_not_gutted", "tests_not_hollow_files",
                   "tests_not_hollowed", "tree_intact",
                   "versioned_migrations"],
    },
    "05": {
        "weight": 27,
        "gates": ["autoloader_zeitwerk", "boot_quiet", "defaults_6_0_adopted",
                  "hosts_refuses_suffix", "hosts_refuses_unknown",
                  "hosts_serves_deployment", "load_defaults_6_0",
                  "rails_version", "ruby_declared", "ruby_runtime",
                  "zeitwerk_clean"],
        # Forty more than the standing nine.  This step moves the interpreter
        # again, so `ruby_runtime` goes back to being a gate after two steps as
        # a guard -- it is the one reading that says the 2.7 move really took,
        # and 2.4 is not in this scorer image at all.  Everything steps 03 and
        # 04 gated is a property of the handed-on tree and is held as a guard
        # here for the usual reason: credit for it would be credit for
        # arriving.
        #
        # `bootsnap_active`, `bootsnap_caches`, `credentials_effective`,
        # `master_key_shipped`, the four CSP readings, `defaults_5_2_adopted`
        # and `form_with_generates_ids` are step 04's gates demoted; the 5.1
        # trio (`encrypted_secrets`, `read_encrypted_secrets`,
        # `encrypted_key_shipped`) is absent in either role for the reason step
        # 04 records -- this ladder does not require a thing and then require
        # its removal.
        "guards": ["application_record", "asset_strict_error",
                   "asset_strict_helper", "belongs_to_not_all_optional",
                   "belongs_to_optional_named", "belongs_to_required", "boots",
                   "bootsnap_active", "bootsnap_caches",
                   "credentials_effective", "csp_configured",
                   "csp_nonce_generator", "csp_nonced", "csp_served",
                   "db_config_from_example", "defaults_5_2_adopted",
                   "eager_load", "eager_load_outside_app", "env_boots",
                   "env_deprecations", "form_with", "form_with_generates_ids",
                   "framework_defaults", "master_key_shipped",
                   "migrations_still_4_2", "no_direct_base", "no_form_for",
                   "no_framework_defaults_file",
                   "no_framework_defaults_switches", "no_legacy_secret_store",
                   "no_plaintext_secrets", "no_positional_mailer_calls",
                   "no_read_encrypted_secrets", "no_string_conditions",
                   "no_suppression", "own_deprecations", "own_suite",
                   "parameterized_mailer", "parses",
                   "schema_pins_primary_key", "suite_deprecations",
                   "suite_size", "tests_intact", "tests_not_emptied",
                   "tests_not_gutted", "tests_not_hollow_files",
                   "tests_not_hollowed", "tree_intact",
                   "versioned_migrations"],
    },
    "06": {
        "weight": 20,
        "gates": ["defaults_6_1_adopted", "export_preloaded",
                  "load_defaults_6_1", "rails_version", "ruby_declared",
                  "ruby_runtime", "strict_loading_declared",
                  "voice_answers_preloaded"],
        # A small gate weight, and on purpose: 6.1 is a release
        # this application has little to do for.  Seven of step
        # 05's eleven gates are demoted here -- zeitwerk, its clean
        # check, the quiet boot and the three host readings are all
        # properties of the handed-on tree, and
        # `defaults_6_0_adopted` joins them.
        # `load_defaults_6_0` is dropped rather than demoted, the
        # way step 05 dropped `load_defaults_5_2`: one line cannot
        # read "6.0" and "6.1" at once, so a guard on it would be a
        # guard the golden fails.
        "guards": ["application_record", "asset_strict_error",
                   "asset_strict_helper", "autoloader_zeitwerk",
                   "belongs_to_not_all_optional", "belongs_to_optional_named",
                   "belongs_to_required", "boot_quiet", "boots",
                   "bootsnap_active", "bootsnap_caches",
                   "credentials_effective", "csp_configured",
                   "csp_nonce_generator", "csp_nonced", "csp_served",
                   "db_config_from_example", "defaults_5_2_adopted",
                   "defaults_6_0_adopted", "eager_load",
                   "eager_load_outside_app", "env_boots", "env_deprecations",
                   "form_with", "form_with_generates_ids",
                   "framework_defaults", "hosts_refuses_suffix",
                   "hosts_refuses_unknown", "hosts_serves_deployment",
                   "master_key_shipped", "migrations_still_4_2",
                   "no_direct_base", "no_form_for",
                   "no_framework_defaults_file",
                   "no_framework_defaults_switches", "no_legacy_secret_store",
                   "no_plaintext_secrets", "no_positional_mailer_calls",
                   "no_read_encrypted_secrets", "no_string_conditions",
                   "no_suppression", "own_deprecations", "own_suite",
                   "parameterized_mailer", "parses", "schema_pins_primary_key",
                   "suite_deprecations", "suite_size", "tests_intact",
                   "tests_not_emptied", "tests_not_gutted",
                   "tests_not_hollow_files", "tests_not_hollowed",
                   "tree_intact", "versioned_migrations", "zeitwerk_clean"],
    },
    "07": {
        "weight": 30,
        # Twelve gates and thirty points, ten of them the two encryption
        # requirements: five round trips through the model on the encrypted
        # column, and the key source that makes them possible.  Four of step
        # 06's eight gates are demoted here rather than dropped -- its
        # `defaults_6_1_adopted`, both preload readings and
        # `strict_loading_declared` are all properties the handed-on tree
        # already has -- and `load_defaults_6_1` is dropped the
        # way step 06 dropped `load_defaults_6_0`: one line cannot read "6.1"
        # and "7.0" at once.
        # Two guards this step could not inherit unchanged, because the
        # golden fails them.  `defaults_5_2_adopted` becomes
        # four readings of the switches 7.0 leaves alone, and
        # `migrations_still_4_2` becomes `migration_semantics`, which counts
        # the 43 shipped migrations that still say 4.2 rather than asserting
        # that nothing says anything else.
        "gates": ["defaults_7_0_adopted", "email_case_identity_kept",
                  "email_column_widened", "email_encrypted_at_rest",
                  "email_no_duplicate_row", "email_still_queryable",
                  "encryption_keys_from_credentials", "export_async",
                  "load_defaults_7_0", "rails_version", "ruby_declared",
                  "ruby_runtime"],
        "guards": ["application_record", "asset_strict_error",
                   "asset_strict_helper", "authenticated_cookie_encryption",
                   "authenticated_message_encryption", "autoloader_zeitwerk",
                   "belongs_to_not_all_optional", "belongs_to_optional_named",
                   "belongs_to_required", "boot_quiet", "boots",
                   "bootsnap_active", "bootsnap_caches", "cache_versioning_5_2",
                   "credentials_effective", "csp_configured",
                   "csp_nonce_generator", "csp_nonced", "csp_served",
                   "db_config_from_example", "default_protect_from_forgery",
                   "defaults_6_0_adopted", "defaults_6_1_adopted", "eager_load",
                   "eager_load_outside_app", "env_boots", "env_deprecations",
                   "export_preloaded", "form_with", "form_with_generates_ids",
                   "framework_defaults", "hosts_refuses_suffix",
                   "hosts_refuses_unknown", "hosts_serves_deployment",
                   "master_key_shipped", "migration_semantics",
                   "no_direct_base", "no_form_for",
                   "no_framework_defaults_file",
                   "no_framework_defaults_switches", "no_legacy_secret_store",
                   "no_plaintext_secrets", "no_positional_mailer_calls",
                   "no_read_encrypted_secrets", "no_string_conditions",
                   "no_suppression", "own_deprecations", "own_suite",
                   "parameterized_mailer", "parses", "schema_pins_primary_key",
                   "strict_loading_declared", "suite_deprecations",
                   "suite_size", "tests_intact", "tests_not_emptied",
                   "tests_not_gutted", "tests_not_hollow_files",
                   "tests_not_hollowed", "tree_intact", "versioned_migrations",
                   "voice_answers_preloaded", "zeitwerk_clean"],
    },
    "08": {
        "weight": 28,
        # Fifteen gates and twenty-eight points.  Seven of step 07's twelve are
        # demoted to guards here -- the five encryption round trips, the key
        # source and `export_async` are all properties the handed-on tree
        # already has -- `load_defaults_7_0` is dropped the way
        # step 07 dropped `load_defaults_6_1`, and `defaults_7_0_adopted`
        # becomes a guard over the twelve of its switches that still exist at
        # 7.1.  What is left is this step's own work: the two pins, the
        # declaration and its eighteen settings, the `lib` declaration in both
        # halves, the header this release drops and the five it keeps, and the
        # ten partials that refuse an undeclared local.
        #
        # Five of the fifteen close gaps between the instruction and the
        # other readings.  Four are requirements the instruction states and
        # nothing else reads --
        # the `File` predicate the interpreter removed, the sass pin that
        # cannot survive it, `lib` declared ONCE rather than twice, and the
        # fixture setting rspec deprecates through a channel
        # `ActiveSupport::Deprecation` never sees -- and the fifth,
        # `html_responses_observed`, is there because `absent_keys` over an
        # empty mapping is true, so the header gate above it could otherwise
        # be passed by a run that recorded no HTML response at all.
        #
        # `lib_declared_once` is worth 1 rather than 2: the count it reads
        # is 1 on the tree this step is handed as well as on the migrated
        # one, so it is a conjunct of `autoload_lib` rather than a reading
        # that discriminates on its own.
        "gates": ["autoload_lib", "autoload_lib_ignores",
                  "defaults_7_1_adopted", "download_options_dropped",
                  "exists_predicate_gone", "html_responses_observed",
                  "lib_declared_once", "load_defaults_7_1", "rails_version",
                  "rspec_own_deprecations", "ruby_declared", "ruby_runtime",
                  "sass_moved", "security_headers_kept",
                  "strict_locals_enforced"],
        "guards": ["application_record", "asset_strict_error",
                   "asset_strict_helper",
                   "authenticated_cookie_encryption",
                   "authenticated_message_encryption",
                   "autoloader_zeitwerk", "belongs_to_not_all_optional",
                   "belongs_to_optional_named", "belongs_to_required",
                   "boot_quiet", "boots", "bootsnap_active",
                   "bootsnap_caches", "cache_versioning_5_2",
                   "credentials_effective", "csp_configured",
                   "csp_nonce_generator", "csp_nonced", "csp_served",
                   "db_config_from_example",
                   "default_protect_from_forgery", "defaults_6_0_adopted",
                   "defaults_6_1_adopted", "defaults_7_0_adopted",
                   "eager_load", "eager_load_outside_app",
                   "email_case_identity_kept", "email_column_widened",
                   "email_encrypted_at_rest", "email_no_duplicate_row",
                   "email_still_queryable",
                   "encryption_keys_from_credentials", "env_boots",
                   "env_deprecations", "export_async", "export_preloaded",
                   "form_with", "form_with_generates_ids",
                   "framework_defaults", "hosts_refuses_suffix",
                   "hosts_refuses_unknown", "hosts_serves_deployment",
                   "master_key_shipped", "migration_semantics",
                   "no_direct_base", "no_form_for",
                   "no_framework_defaults_file",
                   "no_framework_defaults_switches",
                   "no_legacy_secret_store", "no_plaintext_secrets",
                   "no_positional_mailer_calls",
                   "no_read_encrypted_secrets", "no_string_conditions",
                   "no_suppression", "own_deprecations", "own_suite",
                   "parameterized_mailer", "parses",
                   "schema_pins_primary_key", "strict_loading_declared",
                   "suite_deprecations", "suite_size", "tests_intact",
                   "tests_not_emptied", "tests_not_gutted",
                   "tests_not_hollow_files", "tests_not_hollowed",
                   "tree_intact", "versioned_migrations",
                   "voice_answers_preloaded", "zeitwerk_clean"],
    },
    "09": {
        "weight": 20,
        # Seven gates and twenty points, a small set, and small for a
        # reason rather than by omission.  This is the first rung
        # that repairs nothing: `gem 'rails', '7.2.3.2'` and a conservative
        # relock are the whole change, and on that one line the tree installs,
        # boots all three environments and runs its 504 examples green.  So
        # what is gated is what the release turns ON and what it silently
        # turned OFF, not what a candidate had to fix to get a prompt back.
        #
        # Three of the seven are the declaration and its consequences --
        # `load_defaults 7.2`, the three settings behind it this application
        # can be held to, and the one of those three that is really behaviour,
        # taken off a live connection.  The other four are this step's
        # findings: `show_exceptions_none` (7.1 deprecated the booleans and 7.2
        # removed them without restoring an error, so `show_exceptions = false`
        # now means its opposite in the environment the suite runs in), and the
        # pair that reads this tree's only `ActiveRecord::Base.connection`
        # call, which sits in unicorn's `before_fork` and which no other
        # reading looks at: `fork_hooks_lease_free` runs the hook
        # and measures the pool it leaves behind, and `no_ar_base_connection`
        # counts the accessor over the whole tree, where nothing is run.
        #
        # Thirteen of step 08's fifteen gates are demoted to guards, which is
        # strictly harsher -- a guard carries no weight and one failure zeroes
        # the family -- because they are properties the handed-on tree already
        # has.  One of the thirteen, `defaults_7_1_adopted`, is
        # also NARROWED to the fifteen of its eighteen switches that are not
        # deprecated on 7.2; the other two of the fifteen are `rails_version`,
        # replaced rather than demoted, and `load_defaults_7_1`, dropped for
        # the reason every step from 06 on has recorded -- `loaded_config_
        # version` is one value and a correct tree here reads 7.2.
        #
        # `no_read_encrypted_secrets` is the one step-08 guard that does not
        # survive at all: railties 7.2 replaces the accessor with a pair of
        # deprecation stubs, so there is no setting left to read and nothing
        # for a guard to hold down.  The interpreter does not move -- 3.2.11
        # carries this rung and the two after it -- so `ruby_runtime` and
        # `ruby_declared` are guards, where a reading that has not changed
        # belongs.
        "gates": ["defaults_7_2_adopted", "fork_hooks_lease_free",
                  "load_defaults_7_2", "no_ar_base_connection",
                  "pg_dates_decoded", "rails_version",
                  "show_exceptions_none"],
        "guards": ["application_record", "asset_strict_error",
                   "asset_strict_helper", "authenticated_cookie_encryption",
                   "authenticated_message_encryption", "autoload_lib",
                   "autoload_lib_ignores", "autoloader_zeitwerk",
                   "belongs_to_not_all_optional",
                   "belongs_to_optional_named", "belongs_to_required",
                   "boot_quiet", "boots", "bootsnap_active",
                   "bootsnap_caches", "cache_versioning_5_2",
                   "credentials_effective", "csp_configured",
                   "csp_nonce_generator", "csp_nonced", "csp_served",
                   "db_config_from_example", "default_protect_from_forgery",
                   "defaults_6_0_adopted", "defaults_6_1_adopted",
                   "defaults_7_0_adopted", "defaults_7_1_adopted",
                   "download_options_dropped", "eager_load",
                   "eager_load_outside_app", "email_case_identity_kept",
                   "email_column_widened", "email_encrypted_at_rest",
                   "email_no_duplicate_row", "email_still_queryable",
                   "encryption_keys_from_credentials", "env_boots",
                   "env_deprecations", "exists_predicate_gone",
                   "export_async", "export_preloaded", "form_with",
                   "form_with_generates_ids", "framework_defaults",
                   "hosts_refuses_suffix", "hosts_refuses_unknown",
                   "hosts_serves_deployment", "html_responses_observed",
                   "lib_declared_once", "master_key_shipped",
                   "migration_semantics", "no_direct_base", "no_form_for",
                   "no_framework_defaults_file",
                   "no_framework_defaults_switches",
                   "no_legacy_secret_store", "no_plaintext_secrets",
                   "no_positional_mailer_calls", "no_string_conditions",
                   "no_suppression", "own_deprecations", "own_suite",
                   "parameterized_mailer", "parses",
                   "rspec_own_deprecations", "ruby_declared",
                   "ruby_runtime", "sass_moved", "schema_pins_primary_key",
                   "security_headers_kept", "strict_loading_declared",
                   "strict_locals_enforced", "suite_deprecations",
                   "suite_size", "tests_intact", "tests_not_emptied",
                   "tests_not_gutted", "tests_not_hollow_files",
                   "tests_not_hollowed", "tree_intact",
                   "versioned_migrations", "voice_answers_preloaded",
                   "zeitwerk_clean"],
    },
    "10": {
        "weight": 17,
        # Six gates and seventeen points, one fewer gate and three fewer
        # points than step 09, and the shrink is the ladder working rather
        # than the step thinning out.  Step 09's seven were the 7.2
        # declaration and four findings that the 7.2 release forced; all five
        # of those are still true of the tree handed on here, so all five are
        # guards below and none of them can be re-sold.  What is left to gate
        # is what 8.0 itself moves.
        #
        # Three gates are the release: the framework that boots (2), the
        # declaration the application makes (3), and the three settings that
        # declaration turns on, counted together because the release turns
        # them on together (4).  Three are the gem the release strands:
        # `annotate` is capped at `activerecord < 8.0` and resolves back to
        # its 2014 build rather than refusing, so the gate is that it is gone
        # (1), that the maintained fork is in its place (5), and that no call
        # site of the retired API survives (2).
        #
        # That 1/5/2 split is deliberately lopsided, and the reason is what
        # DELETION can buy.  A tree that deletes the gem, deletes the rake
        # file that configured it and leaves the comment text where it is
        # passes `annotate_gem_gone` honestly and passes
        # `annotate_legacy_api_gone` honestly -- the calls went with the
        # file -- without ever doing the step's actual work, which is putting
        # the maintained fork in the retired gem's place.  Under an even
        # split that tree collects the majority of the gem points for a
        # deletion.  Under 1/5/2 it collects 3 of the 8, and the one reading
        # deletion CANNOT produce carries 5 of them.  The other two are not
        # thereby made cheap: they are gates because the tree must arrive
        # with them true, and a tree that leaves the retired gem resolved
        # fails the family's `annotaterb_present` anyway.
        #
        # `annotate_legacy_api_gone` is the only gate on this rung that reads
        # source rather than a booted process.  Two rather than three because
        # a tree that swapped the gem but kept `Annotate.set_defaults` is a
        # tree where `rake` itself no longer loads, and `rake_tasks_load` -- a
        # guard, and so weightless, but one failing guard zeroes this whole
        # family -- catches that from the other side at full price.
        #
        # `defaults_8_0_adopted` counts all three of its readings, where step
        # 09's equivalent held one out.  That is not a tightening for its own
        # sake: all three were measured readable on both sides of the
        # declaration, so all three discriminate and none of them is a
        # reading the release withdrew.
        # The one 7.2 setting the release DID withdraw,
        # `enqueue_after_transaction_commit`, is why `defaults_7_2_adopted`
        # is demoted to a guard here and narrowed to the two that survive.
        "gates": ["annotate_gem_gone", "annotate_legacy_api_gone",
                  "annotaterb_present", "defaults_8_0_adopted",
                  "load_defaults_8_0", "rails_version"],
        "guards": ["application_record", "asset_strict_error",
                   "asset_strict_helper", "authenticated_cookie_encryption",
                   "authenticated_message_encryption", "autoload_lib",
                   "autoload_lib_ignores", "autoloader_zeitwerk",
                   "belongs_to_not_all_optional", "belongs_to_optional_named",
                   "belongs_to_required", "boot_quiet", "boots", "bootsnap_active",
                   "bootsnap_caches", "cache_versioning_5_2",
                   "credentials_effective", "csp_configured",
                   "csp_nonce_generator", "csp_nonced", "csp_served",
                   "db_config_from_example", "default_protect_from_forgery",
                   "defaults_6_0_adopted", "defaults_6_1_adopted",
                   "defaults_7_0_adopted", "defaults_7_1_adopted",
                   "defaults_7_2_adopted", "download_options_dropped",
                   "eager_load", "eager_load_outside_app",
                   "email_case_identity_kept", "email_column_widened",
                   "email_encrypted_at_rest", "email_no_duplicate_row",
                   "email_still_queryable", "encryption_keys_from_credentials",
                   "env_boots", "env_deprecations", "exists_predicate_gone",
                   "export_async", "export_preloaded", "fork_hooks_lease_free",
                   "form_with", "form_with_generates_ids", "framework_defaults",
                   "hosts_refuses_suffix", "hosts_refuses_unknown",
                   "hosts_serves_deployment", "html_responses_observed",
                   "lib_declared_once", "master_key_shipped",
                   "migration_semantics", "model_annotations",
                   "model_annotations_present", "no_ar_base_connection",
                   "no_direct_base", "no_form_for", "no_framework_defaults_file",
                   "no_framework_defaults_switches", "no_legacy_secret_store",
                   "no_plaintext_secrets", "no_positional_mailer_calls",
                   "no_string_conditions", "no_suppression", "own_deprecations",
                   "own_suite", "parameterized_mailer", "parses",
                   "pg_dates_decoded", "rake_tasks_load", "rspec_own_deprecations",
                   "ruby_declared", "ruby_runtime", "sass_moved",
                   "schema_pins_primary_key", "security_headers_kept",
                   "show_exceptions_none", "strict_loading_declared",
                   "strict_locals_enforced", "suite_deprecations", "suite_size",
                   "tests_intact", "tests_not_emptied", "tests_not_gutted",
                   "tests_not_hollow_files", "tests_not_hollowed", "tree_intact",
                   "versioned_migrations", "voice_answers_preloaded",
                   "zeitwerk_clean"],
    },
    "11": {
        "weight": 15,
        # FIVE gates and fifteen points, a small budget, which is the right
        # answer for a release that does not break this application: on the
        # pin alone the tree installs, boots all three environments and runs
        # 504/504 green.  Four of step 10's six gates are demoted to guards
        # here -- the annotate swap
        # and the 8.0 settings are all properties the handed-on tree already
        # has -- and the two version declarations are superseded rather than
        # demoted.  What is left is this step's own work: the pin, the
        # declaration and its six settings, and the routing form the release
        # stops accepting, which is worth two gates because it fails twice and
        # only one of the two failures says anything.
        "gates": ["defaults_8_1_adopted", "load_defaults_8_1",
                  "page_route_intact", "page_route_not_positional",
                  "rails_version"],
        "guards": ["annotate_gem_gone", "annotate_legacy_api_gone",
                   "annotaterb_present", "application_record",
                   "asset_strict_error", "asset_strict_helper",
                   "authenticated_cookie_encryption",
                   "authenticated_message_encryption", "autoload_lib",
                   "autoload_lib_ignores", "autoloader_zeitwerk",
                   "belongs_to_not_all_optional",
                   "belongs_to_optional_named", "belongs_to_required",
                   "boot_quiet", "boots", "bootsnap_active",
                   "bootsnap_caches", "cache_versioning_5_2",
                   "credentials_effective", "csp_configured",
                   "csp_nonce_generator", "csp_nonced", "csp_served",
                   "db_config_from_example", "default_protect_from_forgery",
                   "defaults_6_0_adopted", "defaults_6_1_adopted",
                   "defaults_7_0_adopted", "defaults_7_1_adopted",
                   "defaults_7_2_adopted", "defaults_8_0_adopted",
                   "download_options_dropped", "eager_load",
                   "eager_load_outside_app", "email_case_identity_kept",
                   "email_column_widened", "email_encrypted_at_rest",
                   "email_no_duplicate_row", "email_still_queryable",
                   "encryption_keys_from_credentials", "env_boots",
                   "env_deprecations", "exists_predicate_gone",
                   "export_async", "export_preloaded",
                   "fork_hooks_lease_free", "form_with",
                   "form_with_generates_ids", "framework_defaults",
                   "hosts_refuses_suffix", "hosts_refuses_unknown",
                   "hosts_serves_deployment", "html_responses_observed",
                   "lib_declared_once", "master_key_shipped",
                   "migration_semantics", "model_annotations",
                   "model_annotations_present", "no_ar_base_connection",
                   "no_direct_base", "no_form_for",
                   "no_framework_defaults_file",
                   "no_framework_defaults_switches",
                   "no_legacy_secret_store", "no_plaintext_secrets",
                   "no_positional_mailer_calls", "no_string_conditions",
                   "no_suppression", "own_deprecations", "own_suite",
                   "parameterized_mailer", "parses", "pg_dates_decoded",
                   "rake_tasks_load", "rspec_own_deprecations",
                   "ruby_declared", "ruby_runtime", "sass_moved",
                   "schema_pins_primary_key", "security_headers_kept",
                   "show_exceptions_none", "strict_loading_declared",
                   "strict_locals_enforced", "suite_deprecations",
                   "suite_size", "tests_intact", "tests_not_emptied",
                   "tests_not_gutted", "tests_not_hollow_files",
                   "tests_not_hollowed", "tree_intact",
                   "versioned_migrations", "voice_answers_preloaded",
                   "zeitwerk_clean"],
    },
    "12": {
        "weight": 10,
        # FIVE gates and ten points, smaller than step 11's fifteen,
        # for a rung that does not move the framework at all.  Step
        # 11's five gates are demoted to guards here except the two
        # version declarations, which are superseded rather than
        # demoted.  Four
        # points are on the interpreter -- one for running on it, three
        # for declaring it in all three places that have to agree -- and
        # six are on what the interpreter has stopped handing out.
        "gates": ['bigdecimal_current',
                  'no_stdlib_bundled_gems',
                  'no_stdlib_bundled_gems_envs',
                  'ruby_declared',
                  'ruby_runtime'],
        "guards": ['annotate_gem_gone',
                   'annotate_legacy_api_gone',
                   'annotaterb_present',
                   'application_record',
                   'asset_strict_error',
                   'asset_strict_helper',
                   'authenticated_cookie_encryption',
                   'authenticated_message_encryption',
                   'autoload_lib',
                   'autoload_lib_ignores',
                   'autoloader_zeitwerk',
                   'belongs_to_not_all_optional',
                   'belongs_to_optional_named',
                   'belongs_to_required',
                   'boot_quiet',
                   'boots',
                   'bootsnap_active',
                   'bootsnap_caches',
                   'cache_versioning_5_2',
                   'credentials_effective',
                   'csp_configured',
                   'csp_nonce_generator',
                   'csp_nonced',
                   'csp_served',
                   'db_config_from_example',
                   'default_protect_from_forgery',
                   'defaults_6_0_adopted',
                   'defaults_6_1_adopted',
                   'defaults_7_0_adopted',
                   'defaults_7_1_adopted',
                   'defaults_7_2_adopted',
                   'defaults_8_0_adopted',
                   'defaults_8_1_adopted',
                   'download_options_dropped',
                   'eager_load',
                   'eager_load_outside_app',
                   'email_case_identity_kept',
                   'email_column_widened',
                   'email_encrypted_at_rest',
                   'email_no_duplicate_row',
                   'email_still_queryable',
                   'encryption_keys_from_credentials',
                   'env_boots',
                   'env_deprecations',
                   'exists_predicate_gone',
                   'export_async',
                   'export_preloaded',
                   'fork_hooks_lease_free',
                   'form_with',
                   'form_with_generates_ids',
                   'framework_defaults',
                   'hosts_refuses_suffix',
                   'hosts_refuses_unknown',
                   'hosts_serves_deployment',
                   'html_responses_observed',
                   'lib_declared_once',
                   'load_defaults_8_1',
                   'master_key_shipped',
                   'migration_semantics',
                   'model_annotations',
                   'model_annotations_present',
                   'no_ar_base_connection',
                   'no_direct_base',
                   'no_form_for',
                   'no_framework_defaults_file',
                   'no_framework_defaults_switches',
                   'no_legacy_secret_store',
                   'no_plaintext_secrets',
                   'no_positional_mailer_calls',
                   'no_string_conditions',
                   'no_suppression',
                   'own_deprecations',
                   'own_suite',
                   'page_route_intact',
                   'page_route_not_positional',
                   'parameterized_mailer',
                   'parses',
                   'pg_dates_decoded',
                   'rails_version',
                   'rake_tasks_load',
                   'rspec_own_deprecations',
                   'sass_moved',
                   'schema_pins_primary_key',
                   'security_headers_kept',
                   'show_exceptions_none',
                   'strict_loading_declared',
                   'strict_locals_enforced',
                   'suite_deprecations',
                   'suite_size',
                   'tests_intact',
                   'tests_not_emptied',
                   'tests_not_gutted',
                   'tests_not_hollow_files',
                   'tests_not_hollowed',
                   'tree_intact',
                   'versioned_migrations',
                   'voice_answers_preloaded',
                   'zeitwerk_clean'],
    },
    "13": {
        "weight": 10,
        # FOUR gates and ten points, the shortest gate list on the
        # ladder, for the rung that asks two questions rather than
        # twenty: does anything in the lockfile carry a published
        # advisory, and does the log keep a telephone number or an
        # e-mail address.  Five points each way, split three/two inside
        # each half so that the cheap direct half of the work cannot
        # collect the expensive transitive half's points.  Step 12's
        # five gates are demoted to guards here: the tree this step
        # is handed already runs on 3.4.10 and already declares it.
        "gates": ['no_declared_vulns',
                  'no_email_in_logs',
                  'no_phone_in_logs',
                  'no_transitive_vulns'],
        "guards": ['annotate_gem_gone',
                   'annotate_legacy_api_gone',
                   'annotaterb_present',
                   'application_record',
                   'asset_strict_error',
                   'asset_strict_helper',
                   'audit_db_complete',
                   'audit_db_pinned',
                   'audit_db_present',
                   'audit_lockfile_resolved',
                   'authenticated_cookie_encryption',
                   'authenticated_message_encryption',
                   'autoload_lib',
                   'autoload_lib_ignores',
                   'autoloader_zeitwerk',
                   'belongs_to_not_all_optional',
                   'belongs_to_optional_named',
                   'belongs_to_required',
                   'bigdecimal_current',
                   'boot_quiet',
                   'boots',
                   'bootsnap_active',
                   'bootsnap_caches',
                   'cache_versioning_5_2',
                   'credentials_effective',
                   'csp_configured',
                   'csp_nonce_generator',
                   'csp_nonced',
                   'csp_served',
                   'db_config_from_example',
                   'default_protect_from_forgery',
                   'defaults_6_0_adopted',
                   'defaults_6_1_adopted',
                   'defaults_7_0_adopted',
                   'defaults_7_1_adopted',
                   'defaults_7_2_adopted',
                   'defaults_8_0_adopted',
                   'defaults_8_1_adopted',
                   'download_options_dropped',
                   'eager_load',
                   'eager_load_outside_app',
                   'email_case_identity_kept',
                   'email_column_widened',
                   'email_encrypted_at_rest',
                   'email_no_duplicate_row',
                   'email_still_queryable',
                   'encryption_keys_from_credentials',
                   'env_boots',
                   'env_deprecations',
                   'exists_predicate_gone',
                   'export_async',
                   'export_preloaded',
                   'fork_hooks_lease_free',
                   'form_with',
                   'form_with_generates_ids',
                   'framework_defaults',
                   'hosts_refuses_suffix',
                   'hosts_refuses_unknown',
                   'hosts_serves_deployment',
                   'html_responses_observed',
                   'lib_declared_once',
                   'load_defaults_8_1',
                   'log_parameters_recorded',
                   'log_requests_recorded',
                   'log_responses_recorded',
                   'master_key_shipped',
                   'migration_semantics',
                   'model_annotations',
                   'model_annotations_present',
                   'no_ar_base_connection',
                   'no_audit_suppression',
                   'no_direct_base',
                   'no_form_for',
                   'no_framework_defaults_file',
                   'no_framework_defaults_switches',
                   'no_legacy_secret_store',
                   'no_plaintext_secrets',
                   'no_positional_mailer_calls',
                   'no_stdlib_bundled_gems',
                   'no_stdlib_bundled_gems_envs',
                   'no_string_conditions',
                   'no_suppression',
                   'own_deprecations',
                   'own_suite',
                   'page_route_intact',
                   'page_route_not_positional',
                   'parameterized_mailer',
                   'parses',
                   'pg_dates_decoded',
                   'rails_version',
                   'rake_tasks_load',
                   'rspec_own_deprecations',
                   'ruby_declared',
                   'ruby_runtime',
                   'sass_moved',
                   'schema_pins_primary_key',
                   'security_headers_kept',
                   'show_exceptions_none',
                   'strict_loading_declared',
                   'strict_locals_enforced',
                   'suite_deprecations',
                   'suite_size',
                   'tests_intact',
                   'tests_not_emptied',
                   'tests_not_gutted',
                   'tests_not_hollow_files',
                   'tests_not_hollowed',
                   'tree_intact',
                   'versioned_migrations',
                   'voice_answers_preloaded',
                   'zeitwerk_clean'],
    },
}

# The guards are the same nine at every step, because what they protect --
# the repository's own tests, its own source tree, and the handed-on database
# configuration -- is the same property at every step.  A step that needed a
# different set would be a step that had decided to protect something else,
# and that is a decision to make here rather than in one profile.
GUARDS = ["db_config_from_example", "no_suppression", "parses", "tests_intact",
          "tests_not_emptied", "tests_not_gutted", "tests_not_hollow_files",
          "tests_not_hollowed", "tree_intact"]

TEST_ROOTS = ("spec/",)
CORE_ROOTS = ("app/models/", "app/controllers/", "app/helpers/", "lib/",
              "db/migrate/")


def mutated(doc, path, value):
    """`doc` with one field changed, and the original left alone."""
    out = json.loads(json.dumps(doc))
    node = out
    parts = path.split(".")
    for part in parts[:-1]:
        node = node[part]
    node[parts[-1]] = value
    return out


def breaking_value(spec, current=None):
    """A value that this check must call false.

    One per predicate, so that the sweep below can take any profile and ask of
    every gate in it: does this gate measure anything?  A gate that scores the
    golden and scores a broken reading identically is a gate that reads a field
    nothing writes, or compares against a constant that cannot fail; either way
    it is weight awarded for nothing, and it is invisible in a reward.
    """
    kind = spec["kind"]
    if kind == "is_true":
        return False
    if kind == "equals":
        return "%s/not" % (spec["to"],)
    if kind == "regex":
        return "cv-no-such-value"
    if kind == "at_most":
        return spec["n"] + 1
    if kind == "at_least":
        return spec["n"] - 1
    if kind == "empty":
        return ["something"]
    if kind == "absent_keys":
        # Merged into what is there, not substituted for it: `absent_keys` is
        # the one predicate whose path is a CONTAINER rather than a leaf, and
        # a container replaced wholesale breaks every sibling reading too.
        # The smallest edit that makes this check false is the forbidden key
        # put back beside whatever else the container held.
        forbidden = {key: 1 for key in spec["keys"]}
        if isinstance(current, dict):
            merged = dict(current)
            merged.update(forbidden)
            return merged
        return forbidden
    if kind == "present_keys":
        return {}
    if kind == "version_at_least":
        return "0.0.0"
    raise AssertionError("no breaking value for the %r predicate" % kind)


def rehearse(number, real, manifest, golden):
    """Every check that can be asked of any step's profile."""
    at = "step %s: " % number

    # Reported and returned, not asserted and carried on: every check below
    # calls `score`, which calls `validate` itself, so a profile that does not
    # validate would turn one legible failure into a traceback from the next
    # line and no report at all.
    try:
        score.validate(real)
    except score.BadProfile as exc:
        check(at + "the profile validates", str(exc), "")
        return
    check(at + "the profile validates", True, True)

    entries = real["gates"] + real.get("guards", [])

    # A gate whose path is not in the fixture is a gate nobody wrote a golden
    # value for.  That is the case this section exists to catch, so it is a
    # check in its own right rather than a KeyError somewhere below.
    check(at + "every gate and guard has a golden value here",
          sorted(e["check"]["path"] for e in entries
                 if score.resolve(golden, e["check"]["path"]) is score.MISSING),
          [])

    result = score.score(real, golden)
    check(at + "the golden passes every gate", result["gates"]["fraction"], 1.0)
    check(at + "the golden trips no guard", result["gates"]["failed_guards"],
          [])
    check(at + "the golden's behaviour fraction is 1",
          result["behaviour"]["fraction"], 1.0)
    check(at + "the golden's reward is 1", result["reward"], 1.0)

    expected = EXPECTED_BY_STEP[number]
    check(at + "the gate ids are the ones this file rehearses",
          sorted(g["id"] for g in real["gates"]), sorted(expected["gates"]))
    check(at + "the gates are worth what this file says",
          sum(g["weight"] for g in real["gates"]), expected["weight"])
    # Two statements, not one.  `GUARDS` is the standing set that protects the
    # repository's own tests and tree and is the same at every step; a step may
    # carry more, and step 03 carries eighteen more, because everything its
    # predecessor was gated on is a property the handed-on tree already has and
    # a gate for it would be a free point.  So: the standing nine
    # must all be there, and the full list has to be written down here, which
    # is what makes a guard appearing or disappearing a failure rather than a
    # silent change of what the step protects.
    check(at + "the nine standing guards are all present",
          sorted(set(GUARDS) - {g["id"] for g in real.get("guards", [])}), [])
    check(at + "the guard ids are the ones this file rehearses",
          sorted(g["id"] for g in real.get("guards", [])),
          sorted(expected.get("guards", GUARDS)))

    # The sweep.  One field at a time, because a mutation that trips two gates
    # does not say which gate is measuring it -- and two gates reading one
    # field is itself worth being told about, since one of them is redundant
    # weight.
    for gate_entry in real["gates"]:
        spec = gate_entry["check"]
        broken = mutated(golden, spec["path"],
                         breaking_value(spec, score.resolve(golden,
                                                            spec["path"])))
        outcome = score.score(real, broken)
        check(at + "breaking %s costs exactly that gate" % gate_entry["id"],
              (sorted(g["id"] for g in outcome["gates"]["results"]
                      if not g["ok"]),
               outcome["gates"]["failed_guards"]),
              ([gate_entry["id"]], []))

    for guard_entry in real.get("guards", []):
        spec = guard_entry["check"]
        broken = mutated(golden, spec["path"],
                         breaking_value(spec, score.resolve(golden,
                                                            spec["path"])))
        outcome = score.score(real, broken)
        check(at + "tripping %s costs the family" % guard_entry["id"],
              (sorted(g["id"] for g in outcome["gates"]["results"]
                      if not g["ok"]),
               outcome["gates"]["failed_guards"],
               outcome["gates"]["fraction"]),
              ([], [guard_entry["id"]], 0.0))

    # A probe that could not run must never be a pass: every gate reading it
    # goes false, and none of them reads through to a stale value.
    for probe in sorted({e["check"]["path"].split(".")[0] for e in entries}):
        broken = json.loads(json.dumps(golden))
        broken[probe] = {"error": "boom"}
        outcome = score.score(real, broken)
        depend = sorted(e["id"] for e in entries
                        if e["check"]["path"].split(".")[0] == probe)
        check(at + "a failed %s probe passes nothing that reads it" % probe,
              sorted([g["id"] for g in outcome["gates"]["results"]
                      if not g["ok"]] + outcome["gates"]["failed_guards"]),
              depend)

    # The manifest the guards are evaluated against.  The guards above are only
    # as good as the list of paths they are given, and that list is generated
    # by a script nobody runs twice.  These are the properties `scan.rb` relies
    # on and the properties that make the list a statement about this
    # repository rather than about whatever directory the generator was
    # pointed at.
    check(at + "the manifest has all three lists",
          sorted(k for k in manifest if not k.startswith("_")),
          ["core", "tests", "tests_substantive"])
    check(at + "the manifest's tests list is sorted and unique",
          (manifest["tests"] == sorted(manifest["tests"]),
           len(set(manifest["tests"])) == len(manifest["tests"])),
          (True, True))
    check(at + "the manifest's core list is sorted and unique",
          (manifest["core"] == sorted(manifest["core"]),
           len(set(manifest["core"])) == len(manifest["core"])),
          (True, True))
    check(at + "the manifest lists no absolute path",
          [p for p in manifest["tests"] + manifest["core"]
           if p.startswith("/")], [])
    check(at + "every path in the tests list is under spec/",
          [p for p in manifest["tests"] if not p.startswith(TEST_ROOTS)], [])
    check(at + "every path in the core list is under a core root",
          [p for p in manifest["core"] if not p.startswith(CORE_ROOTS)], [])
    check(at + "neither list is empty",
          (len(manifest["tests"]) > 0, len(manifest["core"]) > 0),
          (True, True))
    # `tests_substantive` is a subset of `tests` and is measured, not assumed:
    # the generator reads each baseline file and keeps the ones that had a
    # non-blank, non-comment line.  It is one shorter than `tests` on this
    # repository, because `spec/javascripts/spec_helper.js` ships as Teaspoon
    # commentary and nothing else -- and that one file is the whole reason the
    # `tests_not_emptied` guard reads this list and not `tests`.
    check(at + "the substantive list is a sorted subset of the tests list",
          (manifest["tests_substantive"] == sorted(manifest["tests_substantive"]),
           set(manifest["tests_substantive"]) <= set(manifest["tests"])),
          (True, True))
    check(at + "the substantive list is the tests list minus the one "
               "comment-only file",
          sorted(set(manifest["tests"]) - set(manifest["tests_substantive"])),
          ["spec/javascripts/spec_helper.js"])
    # The two guards quote their own counts, and a description that drifts from
    # the file is how a reviewer ends up checking the wrong number.
    descs = {g["id"]: g["desc"] for g in real["guards"]}
    check(at + "the tests_intact guard quotes the number of paths it checks",
          str(len(manifest["tests"])) in descs["tests_intact"], True)
    check(at + "the tree_intact guard quotes the number of paths it checks",
          str(len(manifest["core"])) in descs["tree_intact"], True)


# The two calibration runs, kept.  `GOLDEN_BY_STEP` above is written by hand
# and is the control the arithmetic is rehearsed against; these are the merged
# evidence documents two *real* verifier runs produced -- the step's own
# solution, and the tree that solution starts from -- copied out of
# `/logs/verifier/evidence.json` into `../calibration/`.
#
# They answer the question the hand-written fixture cannot.  A hand-written
# fixture says what the author believes the golden looks like, and the sweep
# below breaks one field at a time to show each gate reads something; neither
# can say whether a gate's *threshold* is anywhere the unmigrated tree is not
# already.  The recorded floor can: a gate like `suite_deprecations` would
# pass on a tree whose `bundle install` exits 1 if a suite that never ran
# reported zero warnings, because `at_most 0` is satisfied by zero -- points
# awarded for nothing, invisible in any number but this one.
CALIBRATION = os.path.join(HERE, "..", "calibration")


def recorded(number, which):
    """The evidence a real run of this step's verifier produced, or None."""
    path = os.path.join(CALIBRATION, "%s-%s.json" % (number, which))
    if not os.path.exists(path):
        return None
    with open(path) as handle:
        return json.load(handle)


def fixture_drift(fixture, recorded_doc, path=""):
    """Every leaf the hand-written fixture states, against what really ran.

    `behaviour` is excluded and is the one field the fixture is allowed to
    invent: it carries 1 of 1 so that a gates-family arithmetic check is not
    also a statement about 386 recorded cases.  Everything else in the fixture
    is a claim about what this step's verifier reads off its own golden, and a
    claim like that has an answer sitting in `calibration/NN-golden.json`.

    Without this the sweep is self-consistent and possibly fictional: it breaks
    one field at a time of a document the author typed, and a field the real
    probe never emits -- or emits under another name, or with another shape --
    breaks just as convincingly as a real one.  So does a fixture for a probe
    that is not in the image at all.
    """
    out = []
    if isinstance(fixture, dict):
        if not isinstance(recorded_doc, dict):
            return [(path, fixture, recorded_doc)]
        for key, value in fixture.items():
            if path == "" and key == "behaviour":
                continue
            where = path + "." + key
            if key not in recorded_doc:
                out.append((where, value, "<absent from the recording>"))
            else:
                out += fixture_drift(value, recorded_doc[key], where)
        return out
    if fixture != recorded_doc:
        out.append((path, fixture, recorded_doc))
    return out


# What the tree the candidate is handed scores on behaviour before the
# candidate has touched it.  Zero at 01 and at 02 because in both cases the
# handed-on tree cannot be made to run under the next step's verifier at all --
# the pristine 3.2 application does not boot on any of it, and step 01's answer
# declares a Ruby the 5.0 bundle will not install against -- so every world is
# dead and every case is a miss.  At 03 it is not zero and could not be: step
# 02's answer is a 5.0 application that boots, serves and answers 363 of the
# 386 cases exactly as 5.1 does, and the 23 it gets wrong are the whole of what
# this step is asking for.  The reward is the product of the two families, so
# with no gate passing that floor still scores 0.
#
# Per step, written out, because "the floor scores nothing" is a statement
# about a particular pair of trees and not a law.  Assumed rather than written
# it silently stops being checked at exactly the step where it is false.
# At 04 it is higher still and for the same reason read the other way: step
# 03's answer is a 5.1 application that already answers 385 of the 386 cases
# the way 5.2 does, because almost nothing 5.2 changes on this tree is visible
# in a recording -- the one case that moves is the migration log, which stops
# naming the adapter.
# At 05 it drops back to zero, and not because 5.2 and 6.0 answer differently.
# This step moves the interpreter as well as the framework, the scoring image
# carries one interpreter and rbenv does not fall back, and step 04's answer
# declares 2.4.10 in all three places -- so `bundle install` fails before
# anything is recorded, every world is dead and every case is a miss.  The same
# shape as 01 and 02 and for the same reason: a floor tree that cannot be made
# to run at all under the next step's verifier.
# 06 and 07 are the same shape as 05 and for the same reason at 07: the step
# moves the interpreter as well as the framework, the scoring image carries
# 3.1.7 alone, and step 06's answer declares 3.0.7 in `.ruby-version`, in the
# Gemfile and in the lockfile -- so `bundle install` fails before anything is
# recorded.
# 08 is the same shape again and the mechanism is worth naming exactly, because
# it is one step earlier than the others': step 07's answer declares 3.1.7 in
# `.ruby-version`, the 08 image carries 3.2.11 alone, and rbenv does not fall
# back -- so the floor run does not fail to RESOLVE a bundle, it fails to
# select an interpreter at all ("rbenv: version `3.1.7\' is not installed",
# recorded in the floor run's own bundle log).  Every world is dead and every
# case is a miss.
# 09 is the first floor since 04 that is not zero, and it is the highest one
# in the ladder: 1.0, every case answered.  The interpreter does not move at
# this step -- 3.2.11 carries 08, 09, 10 and 11 -- so step 08's answer selects
# an interpreter, resolves its bundle and boots, and then answers all 386
# recorded cases exactly as the 7.2 tree does, because nothing 7.2 changes on
# this application is visible in a recording.  That is measured on both sides
# and it is also why step 09 ships no overlay fragment.  It leaves the gates
# family as the whole of the separation -- so on this step "no gate passes on
# the floor" is not a nicety, it is the only thing holding the floor down.
# That is where `envboot`'s compound `exceptions_let_out` reading comes from:
# a simpler reading is a property the handed-on 7.1 tree already has, and a
# gate on it would pay the floor for it.
# 10: the interpreter still does not move, so step 09's answer selects
# 3.2.11, resolves its bundle and boots, and answers every recorded case
# except the one this step's overlay re-records: on 7.2 `db:migrate` against
# a dropped and recreated database still runs all forty-four migrations and
# prints a banner for each, and the answer step 10 is scored against is the
# silence 8.0 produces instead.  385/386 is the must-flip doing its work -- a
# floor that reads 1.0 here means the fragment stopped being applied.  The
# gates family is the rest of the separation, which is the reason
# `rake_tasks_load` is a guard and not a seventh gate: the tree this step is
# handed already exits 0 on `rake -T`, so gating it would pay the floor for a
# property it arrived with.
FLOOR_BEHAVIOUR = {"01": 0.0, "02": 0.0, "03": 363 / 386, "04": 385 / 386,
                   "05": 0.0, "06": 0.0, "07": 0.0, "08": 0.0, "09": 1.0,
                   "10": 385 / 386, "11": 1.0, "12": 0.0,
                   "13": 1.0}


def rehearse_calibration(number, real):
    at = "step %s: " % number
    kept_golden = recorded(number, "golden")
    if kept_golden is not None:
        check(at + "the hand-written golden fixture says what the recording says",
              fixture_drift(GOLDEN_BY_STEP[number], kept_golden["evidence"]),
              [])
    check(at + "the floor's behaviour is written down above",
          number in FLOOR_BEHAVIOUR, True)
    for which, want_gates, want_behaviour in (
            ("golden", 1.0, 1.0),
            ("floor", 0.0, FLOOR_BEHAVIOUR.get(number, 0.0))):
        kept = recorded(number, which)
        # Absent is a failure and not a skip.  A step whose calibration was
        # never kept is a step whose floor nobody can check later.
        check(at + "the %s calibration was kept" % which, kept is not None,
              True)
        if kept is None:
            continue
        result = score.score(real, kept["evidence"])
        check(at + "the recorded %s scores %s on the gates" % (which,
                                                               want_gates),
              result["gates"]["fraction"], want_gates)
        check(at + "the recorded %s scores %s on behaviour" % (which,
                                                               want_behaviour),
              result["behaviour"]["fraction"], want_behaviour)
        # And the number the run itself reported, so that a profile edit that
        # changes what the step is worth has to be written down here too.
        check(at + "the recorded %s still scores what it scored" % which,
              round(result["reward"], 6), kept["reward"])
    kept = recorded(number, "floor")
    if kept is not None:
        # Named, not just counted: "the floor passes no gate" is the property,
        # and when it stops holding the useful output is which gate.
        result = score.score(real, kept["evidence"])
        check(at + "no gate passes on the recorded floor",
              sorted(g["id"] for g in result["gates"]["results"] if g["ok"]),
              [])
        # The gates family is zero on the floor by the check above, and the
        # reward is the product of the two families, so this holds whenever
        # that does.  It is the property itself, stated: whatever the
        # handed-on tree already answers correctly, it must not be able to
        # collect more than half a reward for it.
        check(at + "the recorded floor stays at or under half a reward",
              result["reward"] <= 0.5, True)


STEPS = discover()
check("there is at least one profile to rehearse", len(STEPS) > 0, True)
LOADED = {}
for _number, _profile_path, _manifest_path in STEPS:
    if _number not in GOLDEN_BY_STEP:
        print("SELFTEST FAILED: step %s has a profile and no golden fixture "
              "in GOLDEN_BY_STEP" % _number)
        sys.exit(1)
    if not os.path.exists(_manifest_path):
        print("SELFTEST FAILED: step %s has no manifest at %s"
              % (_number, _manifest_path))
        sys.exit(1)
    with open(_profile_path) as _handle:
        _real = json.load(_handle)
    with open(_manifest_path) as _handle:
        _manifest = json.load(_handle)
    LOADED[_number] = (_real, _manifest)
    rehearse(_number, _real, _manifest, GOLDEN_BY_STEP[_number])
    if os.path.isdir(CALIBRATION):
        rehearse_calibration(_number, _real)


# --------------------------------------------------------------------------
# 9b. step 01's profile, read one gate at a time
# --------------------------------------------------------------------------
# The sweep above asks of every gate whether it measures anything.  It cannot
# ask whether it measures the *right* thing: that is a statement about what the
# field means, and it is written out below for the step whose fields are the
# subtlest -- the pair of secrets fields, and the pair of suite gates, where
# the weaker reading of each is exactly the reading an author reaches for
# first.  Later steps are covered by the sweep and by their calibration runs.

REAL, MANIFEST = LOADED["01"]
GOLDEN = GOLDEN_BY_STEP["01"]


def mutate(path, value):
    """Step 01's golden fixture with one field changed."""
    return mutated(GOLDEN, path, value)


def failing(evidence_doc):
    result = score.score(REAL, evidence_doc)
    return (sorted(g["id"] for g in result["gates"]["results"] if not g["ok"]),
            result["gates"]["failed_guards"])


# One field at a time, because a mutation that trips two gates does not say
# which gate is measuring it.
check("a null ruby_declared costs exactly the ruby_declared gate",
      failing(mutate("gemgraph.ruby_declared", None)),
      (["ruby_declared"], []))
check("a 2.1 agreement costs the ruby_declared gate too",
      failing(mutate("gemgraph.ruby_declared", "2.1.7"))[0],
      ["ruby_declared"])
check("an absent config/secrets.yml costs exactly the secrets_yml gate",
      failing(mutate("bootprobe.probes.secrets.secrets_yml_present", False)),
      (["secrets_yml"], []))
# The gate reads `effective_from_secrets_yml`, not `from_secrets_yml`: a tree
# whose `secrets` reader is populated but whose booted key came from somewhere
# else is the case the weaker field cannot see.
check("a key from somewhere else costs exactly the from_secrets_yml gate",
      failing(mutate("bootprobe.probes.secrets.effective_from_secrets_yml",
                     False)),
      (["from_secrets_yml"], []))
# ... and the weaker field left true is exactly what that mutation leaves
# behind: `GOLDEN` still says `from_secrets_yml: True` above it.
check("the weaker field on its own no longer moves the gate",
      failing(mutate("bootprobe.probes.secrets.from_secrets_yml", False)),
      ([], []))
check("one hand-written secret_key_base costs exactly its own gate",
      failing(mutate("scan.counts.secret_key_base_assignment", 1)),
      (["no_secret_key_base_assignment"], []))
check("one leftover engine constant costs exactly the dead_engine_refs gate",
      failing(mutate("scan.counts.dead_engine_ref", 1)),
      (["dead_engine_refs"], []))

# The suite's two gates are separate on purpose, and the mutations say why: a
# suite that is whole and failing and a suite that passes because the failures
# were deleted are different wrong answers and cost different weights.
check("one failing example costs exactly the own_suite gate",
      failing(mutate("rspec.ok", False)), (["own_suite"], []))
# `ran` is a diagnostic and not the gate: the gate reads `ok`, which the parser
# only sets when it saw a summary line, the process exited 0, the failure count
# was 0 and the example count was above 0.  Mutating `ran` on its own must
# therefore move nothing -- if it did, the gate would be reading two fields and
# one of them would be redundant.
check("the ran flag is a diagnostic and not the gate",
      failing(mutate("rspec.ran", False)), ([], []))
# The pair that says why there are two gates and not one: a suite that is green
# because the failures were deleted passes `own_suite` and must still lose
# `suite_size`.
DELETED_EXAMPLES = json.loads(json.dumps(GOLDEN))
DELETED_EXAMPLES["rspec"].update({"examples": 470, "ok": True})
check("a green suite well short of the floor still costs suite_size",
      failing(DELETED_EXAMPLES), (["suite_size"], []))
check("the floor is a floor and not an equality",
      failing(mutate("rspec.examples", 495)), ([], []))
check("one example below the floor fails",
      failing(mutate("rspec.examples", 494)), (["suite_size"], []))
# `examples: None` is what the parser writes when it found no summary line at
# all.  `at_least` over a non-number has to be False and not a TypeError.
check("an unparsed example count is not a pass",
      failing(mutate("rspec.examples", None)), (["suite_size"], []))

# The two pristine guards are guards and not gates: deleting the tests must not
# cost one point of twenty-seven, it must cost the family.
DELETED_SPEC = failing(mutate("scan.missing_tests",
                              ["spec/controllers/twilio_controller_spec.rb"]))
check("a deleted spec trips the tests_intact guard", DELETED_SPEC[1],
      ["tests_intact"])
check("a deleted spec costs no individual gate", DELETED_SPEC[0], [])
check("a deleted spec zeroes the whole gates family",
      score.score(REAL, mutate("scan.missing_tests",
                               ["spec/spec_helper.rb"]))["gates"]["fraction"],
      0.0)
GUTTED_SPEC = failing(mutate("scan.gutted_tests",
                             ["spec/models/location_spec.rb"]))
check("an emptied spec trips the tests_not_gutted guard", GUTTED_SPEC[1],
      ["tests_not_gutted"])
check("an emptied spec costs no individual gate", GUTTED_SPEC[0], [])
check("an emptied spec zeroes the whole gates family",
      score.score(REAL, mutate("scan.gutted_tests",
                               ["spec/models/location_spec.rb"]))
      ["gates"]["fraction"],
      0.0)
check("a deleted model trips the tree_intact guard",
      failing(mutate("scan.missing_core", ["app/models/location.rb"]))[1],
      ["tree_intact"])
# `tests_not_hollowed`.  The attack it answers keeps every path and every
# example declaration -- so `tests_intact` and `tests_not_gutted` both pass --
# and replaces the bodies with a tautology.  The measured figures are baseline
# 327 and golden 309, the difference being the 25 one-line
# `allow_mass_assignment_of` examples step 01 removes with the mechanism they
# were about and the `have(n).items` forms it rewrites; a suite whose examples
# are all one line is 1.
HOLLOW = failing(mutate("scan.assertion_lines", 1))
check("a hollowed suite trips the tests_not_hollowed guard", HOLLOW[1],
      ["tests_not_hollowed"])
check("a hollowed suite costs no individual gate", HOLLOW[0], [])
check("a hollowed suite zeroes the whole gates family",
      score.score(REAL, mutate("scan.assertion_lines", 1))["gates"]["fraction"],
      0.0)
check("the floor is a floor: one line under it fails",
      failing(mutate("scan.assertion_lines", 219))[1], ["tests_not_hollowed"])
check("the floor is a floor: the floor itself passes",
      failing(mutate("scan.assertion_lines", 220))[1], [])
# The measured baseline and golden are both comfortably clear of it, which is
# the property that makes the floor safe for an answer other than the
# reference solution.
check("the recorded baseline clears the floor",
      failing(mutate("scan.assertion_lines", 327))[1], [])
# `at_least` over a non-number is False, not a TypeError: a scan that could not
# read the suite is not a pass.
check("an unmeasured suite is not a pass",
      failing(mutate("scan.assertion_lines", None))[1], ["tests_not_hollowed"])

# `tests_not_hollow_files`, the per-file companion.  The count above is a
# floor over the union of the tree's distinct assertion lines, and a union has
# a shape it cannot see: a tree that moves every surviving assertion into one
# spec file and leaves the other seventy-odd declaring examples that assert
# nothing reaches the same number.  So the shape is stated separately, and a
# tree can be clear of the floor and still trip this.
FLAT = failing(mutate("scan.hollow_tests",
                      ["spec/models/call_spec.rb",
                       "spec/models/caller_spec.rb"]))
check("a spec file left asserting nothing trips tests_not_hollow_files",
      FLAT[1], ["tests_not_hollow_files"])
check("a hollowed file costs no individual gate", FLAT[0], [])
check("one hollowed file zeroes the whole gates family",
      score.score(REAL, mutate("scan.hollow_tests",
                               ["spec/models/call_spec.rb"]))["gates"]["fraction"],
      0.0)
# The two readings are independent in both directions, which is the only
# reason it is worth having both.
check("the count can be met while the distribution is not",
      failing(mutated(mutated(GOLDEN, "scan.assertion_lines", 309),
                      "scan.hollow_tests", ["spec/models/call_spec.rb"]))[1],
      ["tests_not_hollow_files"])
check("the distribution can be clean while the count is not",
      failing(mutated(mutated(GOLDEN, "scan.assertion_lines", 40),
                      "scan.hollow_tests", []))[1],
      ["tests_not_hollowed"])
# `empty` over a non-list is False: a scan that could not read the suite is
# not a pass here either.
check("an unmeasured distribution is not a pass",
      failing(mutate("scan.hollow_tests", None))[1],
      ["tests_not_hollow_files"])

# `db_config_from_example`.  `test.sh` writes `handoff.json` immediately before
# it copies `database.yml.example` over `database.yml`, because after the copy
# the handed-on state is gone.
DBCFG = failing(mutate("handoff.db_config_problems",
                       ["config/database.yml is in the handed-on tree"]))
check("a shipped database.yml trips the db_config_from_example guard",
      DBCFG[1], ["db_config_from_example"])
check("a shipped database.yml costs no individual gate", DBCFG[0], [])
check("a missing database.yml.example trips the same guard",
      failing(mutate("handoff.db_config_problems",
                     ["config/database.yml.example is not in the handed-on "
                      "tree"]))[1],
      ["db_config_from_example"])

# Every other probe answering, so that what this measures is the cost of
# *bootprobe* failing and not the cost of a short document.  `envboot` is in
# here for that reason: it boots the application too, and leaving it out made
# this check charge bootprobe for the two gates that read it.
check("a probe that failed is never a pass",
      failing({"scan": GOLDEN["scan"], "gemgraph": GOLDEN["gemgraph"],
               "depdrive": GOLDEN["depdrive"], "rspec": GOLDEN["rspec"],
               "envboot": GOLDEN["envboot"],
               "bootprobe": {"error": "boom"}})[0],
      ["boots", "eager_load", "from_secrets_yml", "rails_version",
       "ruby_runtime", "secrets_yml"])

# And the other way round: a bundle the step's image cannot resolve costs the
# environment gates and nothing else, which is what makes them a reading of the
# two environment files rather than a second reading of the bundle.
check("a failed envboot costs exactly the environment gates",
      failing({"scan": GOLDEN["scan"], "gemgraph": GOLDEN["gemgraph"],
               "depdrive": GOLDEN["depdrive"], "rspec": GOLDEN["rspec"],
               "bootprobe": GOLDEN["bootprobe"],
               "envboot": {"error": "boom"}})[0],
      ["env_boots", "env_deprecations"])

# The two of them separately, because they are one probe and two findings: a
# development environment that cannot boot and a configuration key the release
# renamed are different defects and a candidate can have exactly one of them.
check("an environment that does not boot costs env_boots alone",
      failing(mutate("envboot.failed", ["development"]))[0], ["env_boots"])
check("a deprecated key in an environment file costs env_deprecations alone",
      failing(mutate("envboot.env_file_deprecations",
                     ["config/environments/test.rb:16:in `block in "
                      "<top (required)>'"]))[0],
      ["env_deprecations"])



# --------------------------------------------------------------------------
# 9c. step 02's two pairs, read the same way
# --------------------------------------------------------------------------
# Step 02 has two places where the obvious gate and the gate that actually
# states the requirement are different fields of the same probe.  A base class
# named `ApplicationRecord` that is not abstract is still an `ApplicationRecord`
# to the first gate; a migration stamped `[5.0]` still has a version suffix to
# the first count.  Both weaker readings pass a tree that did the cheap thing,
# so both are rehearsed here against the step whose profile has them.

REAL02, _ = LOADED["02"]
GOLDEN02 = GOLDEN_BY_STEP["02"]


def mutate02(path, value):
    """Step 02's golden fixture with one field changed."""
    return mutated(GOLDEN02, path, value)


def failing02(evidence_doc):
    result = score.score(REAL02, evidence_doc)
    return sorted(g["id"] for g in result["gates"]["results"] if not g["ok"])


check("step 02: the recorded golden fails no gate",
      failing02(GOLDEN02), [])
check("a base class that is not abstract costs application_record_abstract "
      "alone",
      failing02(mutate02("bootprobe.probes.models."
                         "application_record_abstract", False)),
      ["application_record_abstract"])
check("a base class that is not there costs both model-base gates",
      failing02(mutated(mutated(GOLDEN02,
                                "bootprobe.probes.models.application_record",
                                None),
                        "bootprobe.probes.models."
                        "application_record_abstract", None)),
      ["application_record", "application_record_abstract"])
check("a migration stamped at the wrong release costs migration_semantics "
      "alone",
      failing02(mutated(mutated(GOLDEN02,
                                "scan.migrations.shipped_versions.4_2", 42),
                        "scan.migrations.shipped_versions.5_0", 1)),
      ["migration_semantics"])
check("an unversioned migration costs versioned_migrations alone",
      failing02(mutate02("scan.counts.unversioned_migration", 43)),
      ["versioned_migrations"])
check("versioning every migration at 5.0 fails the step it looks like it "
      "passes",
      failing02(mutated(mutated(GOLDEN02,
                                "scan.counts.unversioned_migration", 0),
                        "scan.migrations.shipped_versions", {"5_0": 43})),
      ["migration_semantics"])
# The reason this gate reads `migrations` and not `misversioned_migration`.
#
# The tree step 02 is handed is step 01's end state: 43 migrations, every one
# of them unversioned, and therefore not one of them versioned *wrongly*.  A
# gate spelled `misversioned_migration <= 0` is true of that tree before the
# candidate has typed anything, which is a point for nothing.  Stated as a
# fixture: on the handed-on tree this gate must FAIL.
check("the tree this step is handed does not already pass migration_semantics",
      failing02(mutated(mutated(mutated(
          GOLDEN02, "scan.counts.unversioned_migration", 43),
          "scan.counts.misversioned_migration", 0),
          "scan.migrations",
          {"total": 43, "unversioned": 43, "versions": {},
           "shipped_total": 43, "shipped_unversioned": 43,
           "shipped_versions": {}})),
      ["migration_semantics", "versioned_migrations"])
check("a suffix that is not a literal counts as neither",
      failing02(mutate02("scan.migrations.shipped_versions",
                         {"not_a_literal": 43})),
      ["migration_semantics"])
check("forty-two of forty-three is not forty-three",
      failing02(mutate02("scan.migrations.shipped_versions.4_2", 42)),
      ["migration_semantics"])
# The reason the gate reads `shipped_versions` and not `versions`, stated as
# the attack it answers: a tree that stamps the 43 files the repository
# shipped `[5.0]` -- the thing the gate is about, since the suffix picks the
# shim their bodies run under -- and drops 43 dummy `[4.2]` classes beside
# them reports 43 at 4.2 across `db/migrate/` and none among the files that
# shipped.  On the directory-wide reading that is a pass.
check("dummy migrations cannot answer for the shipped ones",
      failing02(mutated(mutated(GOLDEN02, "scan.migrations.versions",
                                {"4_2": 43, "5_0": 43}),
                        "scan.migrations.shipped_versions", {"5_0": 43})),
      ["migration_semantics"])
# ... and the count is exact, so padding past it is not a pass either.
check("more than forty-three is not forty-three",
      failing02(mutate02("scan.migrations.shipped_versions.4_2", 86)),
      ["migration_semantics"])
# A shipped migration that was deleted contributes to no bucket, so deletion
# lowers the count rather than moving it somewhere harmless.  (`tree_intact`
# also trips on the deletion; this reads the gates, and the guard has its own
# fixture in section 8.)
check("deleting a shipped migration costs the gate",
      failing02(mutated(mutated(GOLDEN02,
                                "scan.migrations.shipped_versions.4_2", 42),
                        "scan.missing_core",
                        ["db/migrate/20130101000000_create_things.rb"])),
      ["migration_semantics"])
# A payload without this reading loses the gate rather than winning it: an
# absent path is False, never a free pass.
check("a reading the scanner did not write costs the gate",
      failing02(mutate02("scan.migrations", None)),
      ["migration_semantics"])
check("a scan with no manifest reports the shipped keys absent, not zero",
      failing02(mutated(mutated(GOLDEN02, "scan.migrations.shipped_versions",
                                {}),
                        "scan.migrations.shipped_total", None)),
      ["migration_semantics"])


# --------------------------------------------------------------------------
# 9d. step 02's other three
# --------------------------------------------------------------------------
# The same shape again: a requirement the obvious reading cannot reach.
# `eager_load.ok` is the return of `eager_load!`, which succeeds on a tree that
# left `lib/` autoload-only, and the application-wide `belongs_to` default is
# on in a tree that marked every association optional.  Both readings have a
# field of their own and both are rehearsed against a tree that has the
# defect.

check("a constant left undefined after eager loading costs "
      "eager_load_outside_app alone",
      failing02(mutate02("bootprobe.probes.eager_load.undefined_outside_app",
                         ["lib/app_content_renderer.rb "
                          "(AppContentRenderer)"])),
      ["eager_load_outside_app"])
# The gate reads the end and not the means, and the two come apart in both
# directions.  Measured on this repository: a tree that keeps `lib` on
# `eager_load_paths` and moves what `lib/question_presenter.rb` defines
# reports no autoload-only path, eager-loads cleanly, and has no
# `QuestionPresenter`; the reverse -- `lib` autoload-only -- does not boot at
# all under 5.0, because an initializer names one of those constants.
check("an autoload-only path whose constants still resolve is not a failure",
      failing02(mutate02("bootprobe.probes.eager_load.autoload_only",
                         ["lib"])),
      [])
check("an unmeasured resolution is not a pass",
      failing02(mutate02("bootprobe.probes.eager_load.undefined_outside_app",
                         None)),
      ["eager_load_outside_app"])
# The pair: `eager_load!` returning is not the same fact, and a tree can fail
# one without the other in either direction.
check("a production boot that cannot eager load costs eager_load alone",
      failing02(mutate02("bootprobe.probes.eager_load.ok", False)),
      ["eager_load"])
check("a required Call#location costs belongs_to_optional_named alone",
      failing02(mutate02("bootprobe.probes.models.belongs_to.Call#location",
                         True)),
      ["belongs_to_optional_named"])
# Marking the lot optional is the evasion the count is for: the default stays
# on, `Call#location` is still optional, and both of the gates beside it pass.
check("marking every association optional costs belongs_to_not_all_optional "
      "alone",
      failing02(mutated(mutated(GOLDEN02,
                                "bootprobe.probes.models."
                                "belongs_to_optional_count", 6),
                        "bootprobe.probes.models.belongs_to",
                        {"Answer#call": False, "Answer#question": False,
                         "Call#caller": False, "Call#location": False,
                         "LocationSubscription#location": False,
                         "LocationSubscription#subscriber": False})),
      ["belongs_to_not_all_optional"])
# And the other direction, which is the cheapest wrong answer of all: switching
# the default off makes every association optional *and* leaves `Call#location`
# reading as optional, so it is the application-wide gate that has to catch it.
check("switching the default off costs the default gate and the count",
      failing02(mutated(mutated(mutated(GOLDEN02,
                                        "bootprobe.probes.framework_defaults."
                                        "belongs_to_required_by_default",
                                        False),
                                "bootprobe.probes.models."
                                "belongs_to_optional_count", 6),
                        "bootprobe.probes.models.belongs_to",
                        {"Answer#call": False, "Answer#question": False,
                         "Call#caller": False, "Call#location": False,
                         "LocationSubscription#location": False,
                         "LocationSubscription#subscriber": False})),
      ["belongs_to_not_all_optional", "belongs_to_required"])
# A model the tree never defined is not a model whose association is optional.
check("a Call#location nobody declared costs belongs_to_optional_named",
      failing02(mutated(GOLDEN02, "bootprobe.probes.models.belongs_to",
                        {"Answer#call": True, "Answer#question": True,
                         "Call#caller": True,
                         "LocationSubscription#location": True,
                         "LocationSubscription#subscriber": True})),
      ["belongs_to_optional_named"])

# The silencing attack, as the pair of readings it produces.  A tree that sets
# `config.active_support.deprecation = :silence` raises exactly as many
# deprecations and prints none of them: the log-derived count stays 0 and the
# collector's list does not.  Measured with that one setting changed in the
# three environment files, 306 logged suite deprecations become 0.  The gate
# reads the list, so the tree fails.
check("a silenced log with a full collector still costs suite_deprecations",
      failing02(mutated(mutated(GOLDEN02, "rspec.suite_deprecations", 0),
                        "rspec.suite_deprecation_sites",
                        ["spec/controllers/locations_controller_spec.rb"])),
      ["suite_deprecations"])
# ... and the log-derived count is not load-bearing on its own:
# 306 in the log with nothing in the collector's list is a tree that printed
# its findings, and the list is what the step asks about.
check("the log-derived count alone no longer decides the gate",
      failing02(mutate02("rspec.suite_deprecations", 306)), [])
# A collector that never got into the process is an instrument failure, and an
# instrument failure must not read as a clean tree.
check("no collector reading at all costs suite_deprecations",
      failing02(mutate02("rspec.suite_deprecation_sites", None)),
      ["suite_deprecations"])


# -- step 03 -----------------------------------------------------------------
#
# Shortcut scenarios, as fixtures.  Each one is a tree that does the rest of
# this step's work and then takes a shortcut on one requirement; what is
# asserted is which checks notice.  The counts the two "bypass" shapes carry
# are measured, not guessed: step 03's golden tree with
# `config/application.rb` edited into each shape and `cvgate/scan.rb` run over
# the result.

REAL03, _ = LOADED["03"]
GOLDEN03 = GOLDEN_BY_STEP["03"]


def mutate03(path, value):
    """Step 03's golden fixture with one field changed."""
    return mutated(GOLDEN03, path, value)


def failing03(evidence_doc):
    result = score.score(REAL03, evidence_doc)
    return sorted(g["id"] for g in result["gates"]["results"] if not g["ok"])


def guards_failing03(evidence_doc):
    result = score.score(REAL03, evidence_doc)
    return sorted(result["gates"].get("failed_guards", []))


def reward03(evidence_doc):
    return round(score.score(REAL03, evidence_doc)["reward"], 6)


check("step 03: the recorded golden fails no gate", failing03(GOLDEN03), [])

# Bypass A, measured: load_defaults 1, load_defaults_5_1 0,
# framework_defaults_switch 0.  Naming 5.0 and setting the one switch 5.1 adds
# by hand reaches every effect this step's gates read -- and says 5.0.
#
# The declaration is read as a match and not as a complement, and the
# difference is this scenario.  "No declaration names anything but 5.1" is
# true of the handed-on tree, which has no declaration at all, so it could
# only ever have been a guard -- and as a guard this tree, which is one token
# away from the answer, would lose the whole gates family while bypass B,
# which does not declare anything, would keep 31 of 35.  Read as a match, both
# of them are short the same three weight, which is what the difference
# between them is actually worth.
BYPASS_A = mutated(mutated(mutated(
    GOLDEN03, "scan.counts.load_defaults", 1),
    "scan.counts.load_defaults_5_1", 0),
    "scan.counts.framework_defaults_switch", 0)
check("declaring 5.0 and setting the 5.1 switch by hand costs the declaration "
      "gate",
      failing03(BYPASS_A), ["load_defaults_5_1"])
check("and trips no guard, because nothing about that tree is broken",
      guards_failing03(BYPASS_A), [])
check("which leaves it three weight short of the step",
      reward03(BYPASS_A), round(32 / 35, 6))
# The declaration read as a constant is the same shape: the reading cannot see
# what `TARGET` is, so it must not read as 5.1 -- and must not read as a
# finding either, which is the trap a complement spelling falls into.
check("a declaration whose version is a constant costs the same gate",
      (failing03(mutate03("scan.counts.load_defaults_5_1", 0)),
       guards_failing03(mutate03("scan.counts.load_defaults_5_1", 0))),
      (["load_defaults_5_1"], []))

# Bypass B, measured: load_defaults 0, load_defaults_5_1 0,
# framework_defaults_switch 7.  No declaration at all, the 5.0 assignments
# copied into `config/application.rb` and the asset fallback set by hand.  No
# guard trips -- nothing about that tree is broken either -- and it loses the
# two gates that are about the declaration.
BYPASS_B = mutated(mutated(mutated(
    GOLDEN03, "scan.counts.load_defaults", 0),
    "scan.counts.load_defaults_5_1", 0),
    "scan.counts.framework_defaults_switch", 7)
check("carrying the switches by hand instead of declaring costs both gates",
      failing03(BYPASS_B),
      ["load_defaults_5_1", "no_framework_defaults_switches"])
check("and nothing about that tree trips a guard",
      guards_failing03(BYPASS_B), [])
check("which leaves it four weight short of the step",
      reward03(BYPASS_B), round(31 / 35, 6))

# The free-point question, asked of the checks below: on the tree the
# candidate is handed -- 5.0, the generated per-switch file in place, the
# plaintext secrets file still there, four positional mailer calls -- the two
# gates FAIL and the two guards PASS.  A gate that the floor already passes
# would be weight for nothing; a guard the floor already fails would make the
# floor a guard cliff rather than a measurement.
HANDED_ON = mutated(mutated(mutated(mutated(mutated(mutated(mutated(
    GOLDEN03, "scan.counts.load_defaults", 0),
    "scan.counts.load_defaults_5_1", 0),
    "scan.counts.framework_defaults_switch", 7),
    "scan.counts.mailer_positional", 4),
    "bootprobe.probes.secrets.plaintext_secret_files", ["config/secrets.yml"]),
    "pkdrift.wide_default_primary_key", False),
    "pkdrift.pinned", False)
check("the handed-on tree passes none of the checks this round added",
      [g for g in failing03(HANDED_ON)
       if g in ("load_defaults_5_1", "no_framework_defaults_switches",
                "no_plaintext_secrets", "no_positional_mailer_calls",
                "schema_pins_primary_key")],
      ["load_defaults_5_1", "no_framework_defaults_switches",
       "no_plaintext_secrets", "no_positional_mailer_calls",
       "schema_pins_primary_key"])
check("and trips none of the guards this round added",
      guards_failing03(HANDED_ON), [])

# Each new reading, alone, so that a scenario above cannot be passing for the
# wrong reason.
# The two halves of the per-switch file, each alone.  A tree that renames the
# file, and a tree that pastes its body into `config/application.rb`, produce
# the same evidence -- no file under that name, seven assignments somewhere --
# and it is the second gate that reads it.  The first reads the name, and a
# tree that left the file where the generator put it fails that one instead.
check("the switches carried by hand cost the contents gate alone",
      failing03(mutate03("scan.counts.framework_defaults_switch", 7)),
      ["no_framework_defaults_switches"])
check("the per-switch file left where the generator put it costs the name "
      "gate alone",
      failing03(mutate03("bootprobe.probes.framework_defaults_files",
                         ["config/initializers/new_framework_defaults.rb"])),
      ["no_framework_defaults_file"])
check("a plaintext secrets file anywhere in the tree costs its gate alone",
      failing03(mutate03("bootprobe.probes.secrets.plaintext_secret_files",
                         ["config/secrets.yml.bak"])),
      ["no_plaintext_secrets"])
check("a caller nothing runs costs the static gate and not the runtime one",
      failing03(mutate03("scan.counts.mailer_positional", 2)),
      ["no_positional_mailer_calls"])
check("a mailer action that still takes an argument costs the runtime gate "
      "and not the static one",
      failing03(mutate03("bootprobe.probes.mailers.all_parameterized", False)),
      ["parameterized_mailer"])
# The schema cheat: keep every migration
# declared `[4.2]`, write `id: :bigint` on the `create_table`s, and hand-edit
# `db/schema.rb` back to the narrow key.  Nothing that reads the dump alone
# sees it -- `bootprobe`'s `pk_pinned` is true, because the dump really is
# narrow, and `misversioned_migration` is 0, because the declarations really
# are 4.2.  The database the migrations build is what sees it.
check("restamping the migrations to reach the pinned dump trips a guard",
      guards_failing03(mutate03("scan.counts.misversioned_migration", 43)),
      ["migrations_still_4_2"])
CHEAT = mutated(mutated(mutated(
    GOLDEN03, "pkdrift.differing_tables",
    {"locations": {"dump": "integer", "migrated": "bigint",
                   "dump_width": "narrow", "migrated_width": "wide"}}),
    "pkdrift.agree", False),
    "pkdrift.pinned", False)
check("a dump the migrations would not rebuild costs the schema gate alone",
      failing03(CHEAT), ["schema_pins_primary_key"])
check("and that tree, which is what the dump-only reading called pinned, "
      "trips no guard",
      (guards_failing03(CHEAT),
       score.resolve(CHEAT, "bootprobe.probes.database.pk_pinned"),
       score.resolve(CHEAT, "scan.counts.misversioned_migration")),
      ([], True, 0))
# The other ways the second database can disagree -- the two sides agree at
# the wide key, a table exists on one side only, the migration run itself
# failed -- are not scenarios here, and deliberately: `pinned` is computed
# inside the probe, so setting one of its conjuncts in a document the profile
# never recomputes would rehearse nothing.  That conjunction is rehearsed
# where it is written, against a disagreeing pair (`selftest_tools.rb`, the
# `pkdrift:` checks).  What belongs here is the other half of the same
# statement: the profile reads the conjunction and never one of its parts, so
# there is no way for a tree to collect this gate on three conditions out of
# four.
check("the profile reads the conjunction and not one of its halves",
      sorted({e["check"]["path"]
              for e in REAL03["gates"] + REAL03.get("guards", [])
              if e["check"]["path"].startswith("pkdrift.")}),
      ["pkdrift.pinned"])


# -- step 04 -----------------------------------------------------------------
#
# Three shortcuts a candidate could take at this step, as fixtures.  Each is
# a tree that does the rest of the step and then satisfies one requirement in
# name only; what is asserted is which check notices and what it costs.  The
# trees below were measured against the real probes.
#
# The fourth scenario an author reaches for -- the handed-on tree itself, to
# show it collects nothing -- is deliberately not here.  It is not a mutation
# of anything; it is a real recording, and `rehearse_calibration` above scores
# the profile against it and names any gate that passes.

REAL04, _ = LOADED["04"]
GOLDEN04 = GOLDEN_BY_STEP["04"]


def mutate04(path, value):
    """Step 04's golden fixture with one field changed."""
    return mutated(GOLDEN04, path, value)


def failing04(evidence_doc):
    result = score.score(REAL04, evidence_doc)
    return sorted(g["id"] for g in result["gates"]["results"] if not g["ok"])


def guards_failing04(evidence_doc):
    result = score.score(REAL04, evidence_doc)
    return sorted(result["gates"].get("failed_guards", []))


def reward04(evidence_doc):
    return round(score.score(REAL04, evidence_doc)["reward"], 6)


check("step 04: the hand-written golden fails no gate", failing04(GOLDEN04), [])

# The credentials that hold a template.  `bin/rails credentials:edit` with
# `secret_key_base: <%= ENV["SECRET_TOKEN"] %>` pasted in from the 5.1 file it
# replaces: the file exists, it decrypts, `Rails.application.credentials` is
# populated, and the application signs with the sentence -- measured, the tree
# boots in production on a 26-character key and nothing in the framework
# objects.  `credentials_template` is a conjunct of the reading and reported
# beside it, so that tree does not collect the gate's three weight.
TEMPLATE = mutated(mutated(mutated(mutated(
    GOLDEN04, "bootprobe.probes.secrets.effective_from_credentials", False),
    "bootprobe.probes.secrets.credentials_template", True),
    "bootprobe.probes.secrets.credentials_length", 26),
    "bootprobe.probes.secrets.effective_key_length", 26)
check("credentials holding an ERB template cost the credentials gate",
      (failing04(TEMPLATE), guards_failing04(TEMPLATE)),
      (["credentials_effective"], []))
check("which is three weight and no cliff", reward04(TEMPLATE),
      round(28 / 31, 6))

# The nonce that is not minted per request, in its two shapes.  A generator
# returning a constant satisfies `csp_nonce_generator` and fails the repeat
# fetch; a `nonce="abc"` written into the view with `'nonce-abc'` pasted into
# the initializer beside it fails both.  Everything else about both trees is
# clean -- served, enforced, `default-src 'self'`, every inline script carrying
# a nonce its own response names -- which is exactly why the repeat exists
# (`selftest_tools.rb`, the `depdrive/csp` pair on TWICE and SAME_NONCE).
SAME_NONCE04 = mutate04("depdrive.csp.nonce_problems",
                        ["/: the same nonce on two responses "
                         "-- not minted per request"])
check("a generator that returns a constant costs the nonce gate",
      (failing04(SAME_NONCE04), guards_failing04(SAME_NONCE04)),
      (["csp_nonced"], []))
HAND_NONCE = mutated(SAME_NONCE04, "bootprobe.probes.csp.nonce_generator", False)
check("a nonce written into the view costs that gate and the generator gate",
      failing04(HAND_NONCE), ["csp_nonce_generator", "csp_nonced"])
check("so the view shortcut costs more than the constant generator",
      (reward04(HAND_NONCE) < reward04(SAME_NONCE04),
       reward04(HAND_NONCE), reward04(SAME_NONCE04)),
      (True, round(28 / 31, 6), round(29 / 31, 6)))

# Bootsnap in the bundle and not where it has to be.  `setup_from_boot_file` is
# the conjunction, so a `Bootsnap.setup` an initializer makes -- which on this
# stack does not boot at all -- reads false with `setup_called` true.
LATE_SETUP = mutated(mutated(
    GOLDEN04, "bootprobe.probes.bootsnap.setup_from_boot_file", False),
    "bootprobe.probes.bootsnap.boot_file_setup", False)
check("a bootsnap set up anywhere but the boot file costs its gate",
      (failing04(LATE_SETUP), guards_failing04(LATE_SETUP),
       score.resolve(LATE_SETUP, "bootprobe.probes.bootsnap.setup_called")),
      (["bootsnap_active"], [], True))
# And the cache that is not one.  A reading that counted any file under
# `tmp/` whose path named bootsnap would be satisfied by `touch tmp/bootsnap`;
# this one stats the file bootsnap says it is writing.
TOUCHED = mutated(mutated(mutated(
    GOLDEN04, "bootprobe.probes.bootsnap.store_written", False),
    "bootprobe.probes.bootsnap.store_path", None),
    "bootprobe.probes.bootsnap.store_in_tree", False)
check("a bootsnap-looking file that bootsnap did not write costs the cache "
      "gate",
      (failing04(TOUCHED), guards_failing04(TOUCHED)),
      (["bootsnap_caches"], []))
# The other half of the same gate, and the half a path-name reading cannot
# see.
# `Bootsnap.setup(cache_dir: ENV["HOME"] + "/bs")` really does write a cache:
# `store_written` is true, `store_bytes` is non-zero, bootsnap ran.  What it
# does not do is leave the cache where the next boot of THIS tree will find it,
# which is what the page asks for, so the conjunction is what the gate reads.
# The probe derives it in `CVGate::BootProbe.store_in_tree?` and
# selftest_tools.rb rehearses that function on paths this fixture cannot
# produce; here it is priced.
OUTSIDE_TREE = mutated(mutated(
    GOLDEN04, "bootprobe.probes.bootsnap.store_path", "/root/bs/load-path-cache"),
    "bootprobe.probes.bootsnap.store_in_tree", False)
check("a bootsnap cache written outside the tree costs the cache gate",
      (failing04(OUTSIDE_TREE), guards_failing04(OUTSIDE_TREE),
       score.resolve(OUTSIDE_TREE, "bootprobe.probes.bootsnap.store_written")),
      (["bootsnap_caches"], [], True))

# The cliff the instruction warns about, priced.  `suite_deprecations` is a
# guard here and not a weighted gate, because the floor tree is on 5.1, where
# `be_success` is not deprecated, so as a gate it would be weight the
# handed-on tree collects for free.  As a guard it is all or nothing --
# eleven of the twelve sites fixed scores the gates family zero, not 29/31 --
# and the instruction says so in the same words before the work starts.
ONE_LEFT = mutate04("rspec.suite_deprecation_sites",
                    ["spec/controllers/landing_controller_spec.rb:13"])
check("one be_success left in the suite costs the whole gates family",
      (failing04(ONE_LEFT), guards_failing04(ONE_LEFT)),
      ([], ["suite_deprecations"]))
check("which leaves nothing: the gates factor is zero",
      reward04(ONE_LEFT), 0.0)


# --------------------------------------------------------------------------

if FAILURES:
    print("SELFTEST FAILED (%d of %d)" % (len(FAILURES), len(RAN)))
    for failure in FAILURES:
        print("  " + failure)
    sys.exit(1)
if len(RAN) < MIN_CHECKS:
    print("SELFTEST INCOMPLETE: %d checks ran, expected at least %d"
          % (len(RAN), MIN_CHECKS))
    sys.exit(1)
if len(set(RAN)) != len(RAN):
    dupes = sorted({n for n in RAN if RAN.count(n) > 1})
    print("SELFTEST AMBIGUOUS: duplicate check names %s" % dupes)
    sys.exit(1)
print("score selftest ok: %d checks" % len(RAN))
