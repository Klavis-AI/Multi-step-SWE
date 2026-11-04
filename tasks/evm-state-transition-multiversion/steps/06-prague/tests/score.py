#!/usr/bin/env python3
"""Score one step under `reward = gate * score`.

    gate   in {0,1}   every gate group passes completely, or the step is zero
    score  in [0,1]   weighted pass fraction over the SCORED groups only
    reward = gate * score

The tiering is not a comment and not an argument: it is read from
`/tests/groups.json`, which ships inside the verifier image and is the manifest
this scorer grades against.

Gate groups earn nothing. They only veto. Because of that, a step that reports
0 must say *which* gate group vetoed it and name the cases that failed --
otherwise a vetoed step is indistinguishable from a candidate that did nothing,
and the two need completely different fixes.

Some gate groups are assembled from cases that physically live in a scored
group's corpus or probe script. Those are declared in the manifest as

    {"name": "...", "source": "reassigned",
     "cases": [{"from": "<source group>", "name": "<case>"}, ...]}

and this scorer moves them: the case is counted in the gate group, and both the
numerator and the denominator of the source group shrink by one. A reassigned
case with no verdict in the per-case log counts as FAILED, never dropped --
missing evidence must not be able to shrink a denominator.
"""
import argparse, json, os, sys

KILLED_NOTE = "no verdict in the case log"


def load_cases_log(path):
    """(group, case) -> True/False. Lines: CASE\tgroup\tcase\tPASS|FAIL[\t...]."""
    verdicts = {}
    if not path or not os.path.exists(path):
        return verdicts
    with open(path, errors="replace") as fh:
        for line in fh:
            f = line.rstrip("\n").split("\t")
            if len(f) >= 4 and f[0] == "CASE":
                verdicts[(f[1], f[2])] = (f[3] == "PASS")
    return verdicts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--groups-json", required=True)
    ap.add_argument("--cases-log", default="")
    ap.add_argument("--out", required=True)
    ap.add_argument("--status", default="complete")
    ap.add_argument("--row", action="append", default=[],
                    help="<group>|<passed>|<cases> as the group itself reported")
    ap.add_argument("--gate-fail", default="",
                    help="<gate group>|<why> -- short-circuit, used by the build bail")
    args = ap.parse_args()

    man = json.load(open(args.groups_json))
    verdicts = load_cases_log(args.cases_log)

    reported = {}
    for r in args.row:
        name, passed, cases = r.split("|")
        reported[name] = [int(passed), int(cases)]

    notes = []
    forced_group, forced_why = "", ""
    if args.gate_fail:
        forced_group, _, forced_why = args.gate_fail.partition("|")
    short_circuit = bool(forced_group)

    gate_rows = []
    for g in man["gate"]:
        name, src = g["name"], g["source"]
        row = {"group": name, "kind": "gate", "source": src,
               "why_this_group_is_a_gate": g.get("why", ""), "failing_cases": []}

        if src == "build":
            ok = (forced_group != name)
            row.update(cases=1, cases_passed=1 if ok else 0, passed=ok)
            if not ok:
                row["failing_cases"] = [forced_why or "build or liveness check failed"]
            gate_rows.append(row)
            continue

        # Nothing downstream of a short-circuit ever ran, so grading it would
        # invent a verdict. Say so instead: one named veto is actionable, six
        # unreached groups reported as failures are not.
        if short_circuit:
            row.update(cases=0, cases_passed=0, passed=False, status="not reached")
            gate_rows.append(row)
            continue

        if src == "reassigned":
            passed = total = 0
            for c in g["cases"]:
                total += 1
                ok = verdicts.get((c["from"], c["name"]))
                if ok:
                    passed += 1
                else:
                    row["failing_cases"].append(
                        "%s/%s%s" % (c["from"], c["name"],
                                     "" if ok is False else " (%s)" % KILLED_NOTE))
                # graded here, so it leaves its source group's numerator and
                # denominator both -- the case must not be counted twice, and
                # must not be able to pay a scored group.
                if c["from"] in reported:
                    reported[c["from"]][1] -= 1
                    if ok:
                        reported[c["from"]][0] -= 1
                else:
                    notes.append("reassigned case names a source group that did not "
                                 "report: %s" % c["from"])
            row.update(cases=total, cases_passed=passed,
                       passed=(total > 0 and passed == total))
            row["failing_cases"] = row["failing_cases"][:12]
            gate_rows.append(row)
            continue

        passed, total = reported.get(name, [0, 0])
        row["failing_cases"] = [c for (grp, c), ok in sorted(verdicts.items())
                                if grp == name and not ok][:12]
        row.update(cases=total, cases_passed=passed,
                   passed=(total > 0 and passed == total))
        gate_rows.append(row)

    if forced_group and not any(r["group"] == forced_group for r in gate_rows):
        gate_rows.append({"group": forced_group, "kind": "gate", "source": "build",
                          "cases": 1, "cases_passed": 0, "passed": False,
                          "why_this_group_is_a_gate": "", "failing_cases": [forced_why]})

    score = 0.0
    scored_rows = []
    for g in man["scored"]:
        name = g["name"]
        if name not in reported:
            # A scored group the verifier never reported is a manifest/test.sh
            # name mismatch, not a zero. It contributes zero either way, but it
            # must not do so silently.
            notes.append("scored group %s is in the manifest but the verifier "
                         "reported no row for it" % name)
        passed, total = reported.get(name, [0, 0])
        if passed < 0 or total < 0:
            notes.append("group %s reported fewer cases than were reassigned out of "
                         "it (%d/%d); treated as zero" % (name, passed, total))
        passed, total = max(0, passed), max(0, total)
        if passed > total:
            notes.append("group %s reported %d passes out of %d cases" % (name, passed, total))
            passed = total
        rate = (passed / total) if total else 0.0
        contribution = g["weight"] * rate
        score += contribution
        scored_rows.append({"group": name, "kind": "scored", "weight": g["weight"],
                            "cases": total, "cases_passed": passed,
                            "rate": round(rate, 6),
                            "contribution": round(contribution, 6)})

    weight_sum = round(sum(g["weight"] for g in man["scored"]), 6)
    if weight_sum != 1.0:
        notes.append("scored weights sum to %s, not 1.0" % weight_sum)

    gate_ok = all(r["passed"] for r in gate_rows)
    gate = 1 if gate_ok else 0
    score = round(score, 6)
    reward = round(gate * score, 6)

    out = {
        "step": man["step"],
        "status": args.status,
        "model": "reward = gate * score",
        "gate": {
            "passed": gate_ok,
            "value": gate,
            "groups": gate_rows,
            "failures": [
                {"group": r["group"], "cases": r["cases"],
                 "cases_passed": r["cases_passed"],
                 "why_this_group_is_a_gate": r["why_this_group_is_a_gate"],
                 "status": r.get("status", "ran"),
                 "failing_cases": r["failing_cases"]}
                for r in gate_rows if not r["passed"]
            ],
        },
        "score": score,
        "scored_groups": scored_rows,
        "reward": reward,
        "notes": notes,
    }
    if not gate_ok:
        vetoed = [r["group"] for r in gate_rows
                  if not r["passed"] and r.get("status") != "not reached"]
        out["zero_reason"] = (
            "gate failed: " + ", ".join(vetoed) +
            ("; the rest of the gate was not reached" if short_circuit else "") +
            " -- score over the scored groups was %.6f, but the gate is a veto" % score)
    with open(args.out, "w") as fh:
        json.dump(out, fh, indent=2, sort_keys=True)
    print("%.6f" % reward)
    return 0


if __name__ == "__main__":
    sys.exit(main())
