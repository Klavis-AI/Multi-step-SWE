"""Turns a step's evidence into its reward.

Every step scores the same two families and the same way, so the arithmetic
lives here and the differences live in each step's `step/profile.json`:

    behaviour   the recorded behaviour suite (`cvh/`), amended by the must-flip
                overlays for steps 1..k.  Fractional: a case is worth 1/N.
    gates       this step's migrated-ness assertions.  Each is 0 or 1 with an
                integer weight; there is no partial credit inside a gate,
                because "half the `attr_accessible` declarations are gone" is
                not half migrated.

Beside the gates the profile carries **guards**, which are the same predicates
over the same evidence and are not worth anything.  A guard is not a thing the
candidate earns credit for doing; it is a condition under which the gates mean
what they say -- the tree parses, the tree is still there, and no comment tells
a tool to look away.  Failing one multiplies the whole gates family by zero.

Two reasons they are not simply gates with a weight.  A weight makes them
*free points*: an untouched tree passes all three without migrating anything,
which is a floor under every tree that did nothing.  And a weight makes them
*cheap*: at 1/27 each, silencing the scanner with a `rubocop:disable` comment
costs a candidate one twenty-seventh of the gates and can buy three whole
gates, which is a trade worth making.  As guards the trade is
never worth making, and the untouched tree scores what it did: nothing.

`reward = gates * behaviour`: the gates are T1, this step's migrated-ness, and
behaviour is T2, preservation.  A tree that preserves everything and migrates
nothing scores 0, and so does a tree that migrates and breaks everything.
Partial credit stays inside each factor.  The profile also carries `weights`;
they are validated (both at least 0.30, summing to 1) and shown in the report,
but they do not enter the reward.

A gate is a predicate over one merged `evidence` object, addressed by a dotted
path.  The predicate vocabulary is deliberately small -- if a step needs
something that is not here, the honest move is to have its verifier compute a
scalar and add a gate over that, not to grow an expression language nobody
selftests.

Missing evidence is never a pass.  A path that does not resolve, a probe that
recorded an `error` instead of a value, a file the verifier failed to write:
all of them make the gate 0 and say why.  That is the whole reason this file
exists as one evaluator instead of as a shell pipeline per step.
"""

from __future__ import annotations

import argparse
import json
import re
import sys

MISSING = object()


def _step(node, segment):
    """One segment of a path, or MISSING."""
    if isinstance(node, dict):
        return node[segment] if segment in node else MISSING
    if isinstance(node, list):
        try:
            return node[int(segment)]
        except (ValueError, IndexError):
            return MISSING
    return MISSING


def resolve(evidence, path):
    """`a.b.c` into nested dicts/lists, or MISSING.

    A list index is written as a bare integer segment.  There is no wildcard:
    a gate that wants "any of" says so with a predicate, not with a path that
    can silently match nothing.
    """
    node = evidence
    for segment in path.split("."):
        node = _step(node, segment)
        if node is MISSING:
            return MISSING
    return node


def is_probe_error(node):
    """The stub `evidence.py` records for a probe that could not be asked.

    Two keys at most, because a report that happens to carry an `error` field
    among its real findings is a report and has to be scored as one.
    """
    return isinstance(node, dict) and "error" in node and len(node) <= 2


def why_missing(evidence, path):
    """What to say about a path that did not resolve.

    "no evidence at bootprobe.probes.versions.rails" is true of a mistyped
    path and of a bootprobe that never ran, and those need different work from
    whoever reads the log: one is an author's bug and the other is a candidate tree
    that does not boot.  So the prefix is walked and the first dead probe on it
    is named; if every ancestor is a live report, the path really is wrong.
    """
    node = evidence
    for segment in path.split("."):
        if is_probe_error(node):
            return "probe failed: %s" % node["error"]
        node = _step(node, segment)
        if node is MISSING:
            break
    return "no evidence at %s" % path


# --------------------------------------------------------------------------
# predicates
# --------------------------------------------------------------------------
#
# Each returns (ok, "what was seen").  The second half is what the verifier
# prints and what the author reads when a calibration disagrees, so it carries
# the observed value even when the gate passed.

def _p_equals(value, spec):
    return value == spec["to"], repr(value)


def _p_regex(value, spec):
    if not isinstance(value, str):
        return False, "not a string: %r" % (value,)
    return bool(re.search(spec["pattern"], value)), value


def _p_at_most(value, spec):
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return False, "not a number: %r" % (value,)
    return value <= spec["n"], str(value)


def _p_at_least(value, spec):
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return False, "not a number: %r" % (value,)
    return value >= spec["n"], str(value)


def _p_empty(value, spec):
    if not isinstance(value, (list, dict, str)):
        return False, "not a collection: %r" % (value,)
    return len(value) == 0, "%d entr%s: %s" % (
        len(value), "y" if len(value) == 1 else "ies",
        json.dumps(sorted(value) if isinstance(value, dict) else value)[:300])


def _p_absent_keys(value, spec):
    """None of `keys` is present in the mapping/sequence at `path`.

    The gem gates use this: `specs` is name -> version and `reachable` is a
    list, and "protected_attributes is gone" is the same question against
    either.
    """
    if not isinstance(value, (list, dict)):
        return False, "not a collection: %r" % (value,)
    present = sorted(k for k in spec["keys"] if k in value)
    return not present, ("none of %s" % spec["keys"] if not present
                         else "still present: %s" % present)


def _p_present_keys(value, spec):
    if not isinstance(value, (list, dict)):
        return False, "not a collection: %r" % (value,)
    absent = sorted(k for k in spec["keys"] if k not in value)
    return not absent, ("all of %s" % spec["keys"] if not absent
                        else "absent: %s" % absent)


def _p_is_true(value, spec):
    return value is True, repr(value)


def _p_version_at_least(value, spec):
    """Dotted numeric comparison, for "Rails is at least 4.2.0"."""
    def parts(text):
        return [int(x) for x in re.findall(r"\d+", str(text))]
    if not isinstance(value, str):
        return False, "not a string: %r" % (value,)
    return parts(value) >= parts(spec["n"]), value


PREDICATES = {
    "equals": _p_equals,
    "regex": _p_regex,
    "at_most": _p_at_most,
    "at_least": _p_at_least,
    "empty": _p_empty,
    "absent_keys": _p_absent_keys,
    "present_keys": _p_present_keys,
    "is_true": _p_is_true,
    "version_at_least": _p_version_at_least,
}

# The argument each predicate reads out of its own `check`, beside `path`.
#
# This exists because a gate that names a predicate correctly and spells its
# argument wrong is not a failed gate: `spec["to"]` raises KeyError inside
# `score`, which loses the whole report and reports 0 for a tree that may have
# earned 1.  The author finds out after the verifier has run -- twenty minutes
# of postgres, bundler, 386 behaviour cases and the suite -- and what they see
# is a traceback, not a profile error.  Checked in `validate` instead, where it
# costs nothing and names the gate.
PREDICATE_ARGS = {
    "equals": ("to",),
    "regex": ("pattern",),
    "at_most": ("n",),
    "at_least": ("n",),
    "empty": (),
    "absent_keys": ("keys",),
    "present_keys": ("keys",),
    "is_true": (),
    "version_at_least": ("n",),
}


class BadProfile(Exception):
    """The step's profile is not a scorable description of a step."""


def evaluate_gate(evidence, gate):
    """One gate -> {'id', 'ok', 'weight', 'saw', 'desc'}."""
    spec = gate["check"]
    predicate = PREDICATES.get(spec.get("kind"))
    if predicate is None:
        raise BadProfile("gate %s: unknown predicate %r"
                         % (gate["id"], spec.get("kind")))
    value = resolve(evidence, spec["path"])
    if value is MISSING:
        ok, saw = False, why_missing(evidence, spec["path"])
    elif is_probe_error(value):
        # A probe that recorded its own exception in place of its value.  This
        # has to be a failure and not a crash: one unreadable probe should cost
        # its own gate and leave the other twelve measuring.
        ok, saw = False, "probe failed: %s" % value["error"]
    else:
        ok, saw = predicate(value, spec)
    return {"id": gate["id"], "ok": bool(ok), "weight": gate.get("weight", 1),
            "saw": saw, "desc": gate.get("desc", "")}


def validate(profile):
    weights = profile.get("weights", {})
    for family in ("behaviour", "gates"):
        if family not in weights:
            raise BadProfile("profile has no %s weight" % family)
        if weights[family] < 0.30:
            raise BadProfile(
                "%s weight is %.2f; design rule 5 puts the floor at ~0.30"
                % (family, weights[family]))
    total = sum(weights.values())
    if abs(total - 1.0) > 1e-9:
        raise BadProfile("weights sum to %r, not 1.0" % total)
    ids = [g["id"] for g in profile.get("gates", [])]
    if not ids:
        raise BadProfile("profile has no gates")
    # Guards share the id namespace with gates on purpose: both are reported
    # under `gates` and both are read against the instruction, so a profile
    # that used one name for two different predicates would produce a report
    # nobody could resolve.
    ids += [g["id"] for g in profile.get("guards", [])]
    if len(set(ids)) != len(ids):
        raise BadProfile("duplicate gate ids: %s"
                         % sorted({i for i in ids if ids.count(i) > 1}))
    for entry in list(profile["gates"]) + list(profile.get("guards", [])):
        spec = entry.get("check") or {}
        kind = spec.get("kind")
        if kind not in PREDICATES:
            raise BadProfile("gate %s: unknown predicate %r"
                             % (entry.get("id"), kind))
        if not spec.get("path"):
            raise BadProfile("gate %s: no path" % entry.get("id"))
        for key in PREDICATE_ARGS[kind]:
            if key not in spec:
                raise BadProfile("gate %s: a %s check needs %r, and this one "
                                 "has %s" % (entry.get("id"), kind, key,
                                             sorted(spec)))
    for gate in profile["gates"]:
        if gate.get("weight", 1) <= 0:
            raise BadProfile("gate %s has non-positive weight" % gate["id"])
        if not gate.get("desc"):
            raise BadProfile("gate %s has no description; a gate nobody can "
                             "read is a gate nobody can check the instruction "
                             "against" % gate["id"])
    for guard in profile.get("guards", []):
        if "weight" in guard:
            raise BadProfile("guard %s has a weight; a guard is a condition on "
                             "the gates, not one of them" % guard["id"])
        if not guard.get("desc"):
            raise BadProfile("guard %s has no description" % guard["id"])


def score(profile, evidence):
    validate(profile)
    gates = [evaluate_gate(evidence, g) for g in profile["gates"]]
    weight = sum(g["weight"] for g in gates)
    gate_fraction = sum(g["weight"] for g in gates if g["ok"]) / weight

    # A guard is evaluated exactly like a gate and then thrown away as a
    # score: what it produces is a factor of 1 or 0 over the family above.
    # `weight` is stripped from the result so a reader of report.json cannot
    # mistake a guard for a gate that happened to be worth nothing.
    guards = [evaluate_gate(evidence, dict(g, weight=0))
              for g in profile.get("guards", [])]
    for guard in guards:
        guard.pop("weight", None)
    failed_guards = [g["id"] for g in guards if not g["ok"]]
    if failed_guards:
        gate_fraction = 0.0

    behaviour = resolve(evidence, "behaviour.fraction")
    behaviour_missing = behaviour is MISSING or not isinstance(
        behaviour, (int, float))
    if behaviour_missing:
        behaviour = 0.0

    weights = profile["weights"]
    # T1 x T2: the gates (zeroed by any failed guard) times behaviour.
    reward = gate_fraction * behaviour
    return {
        "step": profile.get("step"),
        "reward": round(min(1.0, max(0.0, reward)), 6),
        # `evidence.py`'s meta, carried through unread.  Nothing here is
        # scored: it is what `test.sh` knew about its own run -- which step's
        # profile it loaded, whether `bundle install` succeeded, whether the
        # image's file modes were the ones phase 4 needs -- and report.json is
        # the artifact somebody reads a month later when a number looks wrong.
        "meta": evidence.get("meta") if isinstance(evidence, dict) else None,
        "behaviour": {
            "fraction": behaviour,
            "weight": weights["behaviour"],
            "passed": resolve(evidence, "behaviour.passed"),
            "total": resolve(evidence, "behaviour.total"),
            "missing": behaviour_missing,
            "flips": resolve(evidence, "behaviour.flips"),
            "dead_worlds": resolve(evidence, "behaviour.dead_worlds"),
        },
        "gates": {
            "fraction": gate_fraction,
            "weight": weights["gates"],
            "results": gates,
            "guards": guards,
            "failed_guards": failed_guards,
        },
    }


def _whose_answer(label):
    """How a `stale_as` label reads in a sentence."""
    return "the baseline answer" if label == "baseline" else "%s's answer" % label


def render(report):
    lines = []
    # A failed preflight goes first and says what it means, because the reward
    # underneath it is arithmetic over probes that could not be asked and reads
    # as a hard step rather than as a broken image.
    preflight = (report.get("meta") or {}).get("preflight")
    if preflight not in (None, "ok"):
        lines.append("PREFLIGHT   %s -- verifier image defect; the gates below"
                     % preflight)
        lines.append("            are not a measurement of the tree")
    b = report["behaviour"]
    lines.append("behaviour  %s/%s = %.4f   (T2 factor)%s"
                 % (b["passed"], b["total"], b["fraction"],
                    "   NO REPORT -- scored 0" if b["missing"] else ""))
    flips = b.get("flips")
    if isinstance(flips, dict) and flips.get("total"):
        # The breakdown names both steps with a claim on an unflipped case:
        # `owed_by` is the step whose overlay asked for the change, `still_on`
        # is whose answer the tree is giving instead (a step id means the case
        # stopped moving there, `baseline` means it never moved at all).  Those
        # are different stories and the count alone tells neither -- and on a
        # case an earlier step had already moved once they are different steps,
        # so naming only the answer held reads like a regression in a step that
        # is fine.  `stale_by_step` is the single-label form and is read only
        # when `stale_detail` is absent.
        detail = ""
        pairs = flips.get("stale_detail")
        if isinstance(pairs, list) and pairs:
            detail = " (%s)" % "; ".join(
                "%s owed by %s, still on %s"
                % (len(d.get("ids") or []), d.get("owed_by"),
                   _whose_answer(d.get("still_on")))
                for d in pairs)
        else:
            by_step = flips.get("stale_by_step") or {}
            if isinstance(by_step, dict) and by_step:
                detail = " (%s)" % ", ".join(
                    "%s still on %s" % (len(ids), _whose_answer(label))
                    for label, ids in sorted(by_step.items()))
        lines.append("           must-flip %s/%s, %s still on an earlier answer%s"
                     % (flips.get("flipped"), flips.get("total"),
                        len(flips.get("stale") or []), detail))
    # Named rather than merely counted, because "0/391" and "0/391, and the
    # application never came up in any world" send the reader to different
    # places, and the second one is nearly always the true story.
    dead = b.get("dead_worlds")
    if isinstance(dead, list) and dead:
        lines.append("           %d world(s) never came up: %s"
                     % (len(dead), ", ".join(dead)))
    g = report["gates"]
    failed_guards = g.get("failed_guards") or []
    lines.append("gates      %.4f   (T1 factor)%s"
                 % (g["fraction"],
                    "   ZEROED BY %d GUARD(S)" % len(failed_guards)
                    if failed_guards else ""))
    for entry in g.get("guards") or []:
        if entry["ok"]:
            continue
        lines.append("  [GUARD] %-26s %s" % (entry["id"], entry["saw"]))
        lines.append("         requires: %s" % entry["desc"])
    for entry in g["results"]:
        lines.append("  [%s] %-28s w=%d  %s"
                     % ("PASS" if entry["ok"] else "FAIL", entry["id"],
                        entry["weight"], entry["saw"]))
        if not entry["ok"]:
            lines.append("         requires: %s" % entry["desc"])
    lines.append("reward     %.6f   (= gates x behaviour)" % report["reward"])
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(prog="score")
    parser.add_argument("--profile", required=True)
    parser.add_argument("--evidence",
                        help="merged evidence JSON written by test.sh")
    parser.add_argument("--report", help="write the scored report here")
    parser.add_argument("--lint", action="store_true",
                        help="check the profile is scorable and exit, without "
                             "reading any evidence; test.sh's preflight runs "
                             "this before the twenty minutes of work whose "
                             "result the profile would have to describe")
    args = parser.parse_args(argv)

    with open(args.profile, encoding="utf-8") as handle:
        profile = json.load(handle)
    if args.lint:
        validate(profile)
        print("profile ok: %d gates, %d guards"
              % (len(profile["gates"]), len(profile.get("guards", []))))
        return 0
    if not args.evidence:
        parser.error("--evidence is required unless --lint is given")
    with open(args.evidence, encoding="utf-8") as handle:
        evidence = json.load(handle)

    report = score(profile, evidence)
    print(render(report))
    if args.report:
        with open(args.report, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=1)
            handle.write("\n")
    # The reward goes to stdout's last line as a bare number so `test.sh` can
    # read it without parsing JSON in shell.
    print("REWARD=%.6f" % report["reward"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
