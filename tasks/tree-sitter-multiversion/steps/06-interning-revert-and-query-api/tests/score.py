#!/usr/bin/env python3
"""Turn driver results into `reward = gate x score`.

Reads the step's group definition and the driver's per-check results and writes
`reward_details.json`. It decides nothing about what a check means: the groups
file names which checks form the gate and which form each scored group, and
fixes their weights.

  gate   1 only if every gate check passed. Binary, earns nothing, vetoes.
  score  sum over scored groups of weight * (passed / total), in [0, 1].
         The weights sum to 1.0; that is checked here rather than trusted — a
         table that drifts to 0.9 is recorded as a `weight_error` in the
         details and printed by test.sh, so it cannot silently cap the step.

A check the driver never reached (aggregate budget exhausted) counts as failed,
and is listed separately in the details so a timeout is never mistaken for a
wrong answer.

Usage:  score.py <groups.json> <results.json> <details.json>
"""

import json
import sys


def main():
    groups_path, results_path, details_path = sys.argv[1:4]
    with open(groups_path, encoding="utf-8") as handle:
        groups = json.load(handle)
    with open(results_path, encoding="utf-8") as handle:
        results = json.load(handle)

    def outcome(check_id):
        record = results.get(check_id)
        if record is None:
            return False, "check missing from results"
        return bool(record.get("ok")), record.get("reason") or ", ".join(
            record.get("mismatched_keys", [])[:6]
        )

    details = {"step": groups["step"], "gate": 0, "score": 0.0, "reward": 0.0}

    gate_rows = []
    gate_ok = True
    for check_id in groups["gate"]["checks"]:
        passed, why = outcome(check_id)
        gate_ok = gate_ok and passed
        gate_rows.append({"check": check_id, "passed": passed, "detail": why})
    details["gate_checks"] = gate_rows
    details["gate"] = 1 if gate_ok else 0

    total_weight = 0.0
    score = 0.0
    scored_rows = []
    for group in groups["scored"]:
        rows = []
        passed_count = 0
        for check_id in group["checks"]:
            passed, why = outcome(check_id)
            passed_count += 1 if passed else 0
            rows.append({"check": check_id, "passed": passed, "detail": why})
        fraction = passed_count / float(len(group["checks"])) if group["checks"] else 0.0
        weight = float(group["weight"])
        total_weight += weight
        score += weight * fraction
        scored_rows.append(
            {
                "group": group["id"],
                "tier": group.get("tier", "unspecified"),
                "weight": weight,
                "passed": passed_count,
                "total": len(group["checks"]),
                "fraction": round(fraction, 6),
                "contribution": round(weight * fraction, 6),
                "checks": rows,
            }
        )
    details["scored_groups"] = scored_rows

    if abs(total_weight - 1.0) > 1e-6:
        details["weight_error"] = (
            "scored group weights sum to %.6f, not 1.0; the step cannot reach "
            "its full score" % total_weight
        )

    details["score"] = round(min(max(score, 0.0), 1.0), 6)
    details["reward"] = round(details["score"] if details["gate"] == 1 else 0.0, 6)
    details["budget_exhausted"] = sorted(
        cid
        for cid, record in results.items()
        if not record.get("ok") and "budget" in (record.get("reason") or "")
    )

    with open(details_path, "w", encoding="utf-8") as handle:
        json.dump(details, handle, indent=1, sort_keys=True)
    print("%d %.6f" % (details["gate"], details["score"]))


if __name__ == "__main__":
    main()
