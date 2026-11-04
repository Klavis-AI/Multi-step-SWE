"""Combine the two scored families into the step's reward.

    reward = A * B

    A  behavior equivalence   0.85 * recorded corpus + 0.15 * inherited suite
    B  migrated-ness gates    mean(new credits) * all(carried credits)

The recorded corpus is itself a product -- (must-stay behaviours kept) *
(must-flip behaviours made), the same two-population shape B uses -- for the
reason ``corpus_fraction`` gives: a fraction over the union lets a submission
un-migrate a documented flip for the price of one case in twelve hundred.

The product is not a stylistic choice; it is the only shape that satisfies the
two things the task is required to be at once. Rule 5 says of the untouched
previous-step tree that it "passes the first family completely and must fail
this one completely", and the run contract says nop "reports 0.0 at every
step". A weighted sum cannot hold both: the nop tree is the previous step's
tree, so A = 1 and B = 0, and any sum of those with positive weights is the
behavior weight -- 0.5 under the flattest split, which is what this grader
scored before and exactly what `harbor run --agent nop` reported. Under the
product it is 0, which is what a submission that did none of the step's work
deserves and what the contract asks for.

The floor falls out of the same argument. Step k-1's finished tree fails every
one of step k's `new` checks by construction -- that is how the manifest was
built -- so B = 0 and the floor is 0, comfortably inside the "<= 0.5" the build
process wants.

Rule 5's "neither family below ~30% of its reward" is satisfied more strictly
by the product than by the 50/50 sum it replaces: a one-point drop in either
family costs the same fraction of the reward as the same drop in the other
(the elasticity is 1 in both), and zeroing either zeroes the score. Nothing
here can be reached by doing one family and ignoring the other, which is the
failure mode the rule exists to prevent. The price is that a half-done step
scores as a half-done step -- 1.0 behavior with half the gates is 0.50, where
the sum would have paid 0.75 for it -- and that is the intended reading of a
ladder whose rungs are supposed to be leaps.

The product also removes a hole worth naming, because it was found by review
rather than by argument: a tree whose modules were replaced by stubs passes
several gates trivially (no old construct survives in a file that no longer
has any code in it) while failing every behavior case. Additively that is
around a tenth of the step for deleting the repository. Multiplicatively it is
zero, and `score.importable()` makes it zero for the stated reason rather than
as a side effect.

Why A is not a straight average of its two parts: the recorded corpus is the
oracle -- 1236 cases, a third of them adversarial, each one a value the tree
produced before anybody touched it. The repository's own suite is 65 tests that
overlap it heavily and were written by the same people who wrote the bugs. It
belongs in the score, because rule 5 says so and because it catches things the
corpus does not reach (collection errors, import-time breakage in modules no
case exercises), but it is corroboration, not the measurement. 0.15 is enough
that losing the suite entirely costs more than any single adversarial case and
little enough that the suite cannot outvote the corpus.

Both families are reported whatever happens. A crashed probe is a 0 for its
family and a line in the report saying so -- never an exception that escapes
test.sh, because a verifier that exits non-zero takes the rest of the run's
steps with it (Harbor aborts the remaining steps, and the step's own reward
never reaches the mean at all).

    python -m skutil_gates.grade --behavior b.json --suite s.json \\
                                 --gates g.json --out report.json

Prints the reward on the last line as ``REWARD <float>`` so test.sh can read it
with a single ``tail -1``. Python 3.7 compatible.
"""
from __future__ import print_function, absolute_import, division

import argparse
import json
import sys

CORPUS_WEIGHT = 0.85
SUITE_WEIGHT = 0.15


def _load(path):
    if not path:
        return None
    try:
        with open(path) as fh:
            return json.load(fh)
    except Exception as exc:                      # noqa: BLE001 - reported
        return {"__unreadable__": "%s: %s" % (path, exc)}


def corpus_fraction(report, min_flips=0):
    """Corpus credit: the must-stay behaviours kept times the must-change ones made.

    The recorded corpus is two populations, not one. Most of the 1236 cases are
    values the migration must *preserve* -- the tree produced them before it was
    touched and must go on producing them. A declared subset are *flips*: the
    documented semantic changes of this step, cases whose recorded value was
    overwritten by ``skutil_probe.run compare --flips`` so that the untouched
    tree fails them and only a migrated one passes. Rule 5 calls these the
    must-flip inputs and puts them at the centre of the step.

    A single fraction over the union treats the two the same, and that is a
    hole: a submission that migrates everything and then pins one library
    change back -- restores pandas 0.23's ``from_dict`` key order, say -- fails
    only the seventeen flips that change encodes, seventeen out of 1236, and
    keeps 98.6% of the corpus for un-migrating a documented behaviour. So the
    two populations are scored as two sub-families and multiplied, the same
    shape family B uses for its own two:

        corpus = (must-stay kept) * (must-flip made)

    The flip factor is a mean, not all-or-nothing: 119 subtle changes is too
    many to make missing one cost the whole step, but failing a tenth of them
    now costs a tenth of the corpus rather than 119/1236 of it. The oracle
    passes both populations and scores 1; the pin-it-back shim scores its
    must-stay times 0.86, not 0.99.

    ``min_flips`` is trap 4 for this factor. A step that declares flips but
    whose flips file failed to load applies none, and an empty flip population
    reads as a perfect one (mean of nothing is 1) -- which would silently undo
    the split and hand the shim its 0.99 back. When the caller says how many
    flips this step must have and fewer arrived, the corpus is an error and
    scores 0, exactly as an empty denominator does. A step with no flips (step
    1) passes ``min_flips=0`` and the factor is inert.

    A missing report, an unreadable one, or one with no cases is 0. An empty
    denominator is not a pass: it is the shape of a probe that never ran, and
    reading it as 1.0 would hand a broken verifier a full behavior score.

    ``casebooks_unimported`` is the other way a fraction can be honest and
    still mislead. The casebooks import from skutil at module scope, so a
    candidate that drops a re-exported name costs itself every case in the
    modules that mention it -- scored as missing, which is right -- and the
    fraction alone does not say that is what happened. The count is carried
    into the report so it does.
    """
    if not report or "__unreadable__" in report:
        why = (report or {}).get("__unreadable__", "no behavior report")
        return 0.0, {"error": why, "total": 0, "passed": 0}
    total = int(report.get("total") or 0)
    passed = int(report.get("passed") or 0)
    if total <= 0:
        return 0.0, {"error": "behavior report has no cases", "total": 0,
                     "passed": 0}

    results = report.get("results", [])
    flip_ids = set(f["case"] for f in (report.get("flips_applied") or []))

    flip_total = flip_passed = unchanged_total = unchanged_passed = 0
    for r in results:
        if r.get("case") in flip_ids:
            flip_total += 1
            flip_passed += 1 if r.get("ok") else 0
        else:
            unchanged_total += 1
            unchanged_passed += 1 if r.get("ok") else 0

    if flip_total < int(min_flips or 0):
        return 0.0, {
            "error": "expected at least %d flips, %d applied -- flips file "
                     "missing or unreadable" % (int(min_flips), flip_total),
            "total": total, "passed": passed,
            "flips_total": flip_total, "flips_expected": int(min_flips),
        }

    unchanged_fraction = (unchanged_passed / float(unchanged_total)
                          if unchanged_total else 1.0)
    flip_fraction = flip_passed / float(flip_total) if flip_total else 1.0
    corpus = unchanged_fraction * flip_fraction

    failures = [r["case"] for r in results if not r.get("ok")]
    adversarial = [r["case"] for r in results
                   if not r.get("ok") and "adversarial" in (r.get("tags") or [])]
    return corpus, {
        "error": None,
        "total": total,
        "passed": passed,
        "failed": total - passed,
        "failed_adversarial": len(adversarial),
        "unchanged_total": unchanged_total,
        "unchanged_passed": unchanged_passed,
        "unchanged_fraction": round(unchanged_fraction, 6),
        "flips_total": flip_total,
        "flips_passed": flip_passed,
        "flips_fraction": round(flip_fraction, 6),
        "flips_expected": int(min_flips or 0),
        "flips_applied": len(flip_ids),
        "cases_dropped": len(report.get("cases_dropped") or []),
        "casebooks_unimported": [e.get("module") for e
                                 in report.get("actual_load_errors") or []],
        "first_failures": sorted(failures)[:20],
    }


def suite_fraction(report):
    if not report or "__unreadable__" in report:
        why = (report or {}).get("__unreadable__", "no suite report")
        return 0.0, {"error": why, "total": 0}
    return float(report.get("score") or 0.0), {
        "error": report.get("error"),
        "total": report.get("total", 0),
        "collected": report.get("collected", 0),
        "missing": len(report.get("missing") or []),
        "regressed": len(report.get("regressed") or []),
        "repaired": len(report.get("repaired") or []),
    }


def gate_fraction(report):
    if not report or "__unreadable__" in report:
        why = (report or {}).get("__unreadable__", "no gate report")
        return 0.0, {"error": why}
    failing = []
    for group in ("new", "carried"):
        for row in report.get(group) or []:
            if row.get("credit", 0) < 1.0:
                failing.append({"group": group, "name": row.get("name"),
                                "observed": row.get("observed"),
                                "limit": row.get("limit"),
                                "credit": row.get("credit")})
    return float(report.get("score") or 0.0), {
        "error": None,
        "new_mean": report.get("new_mean"),
        "carried_mean": report.get("carried_mean"),
        "importable": report.get("importable", True),
        "import_detail": report.get("import_detail"),
        "incomplete": failing,
    }


def grade(behavior, suite, gates, min_flips=0):
    corpus, corpus_detail = corpus_fraction(behavior, min_flips=min_flips)
    inherited, suite_detail = suite_fraction(suite)
    gate, gate_detail = gate_fraction(gates)

    family_a = max(0.0, min(1.0, CORPUS_WEIGHT * corpus
                            + SUITE_WEIGHT * inherited))
    family_b = max(0.0, min(1.0, gate))
    reward = max(0.0, min(1.0, family_a * family_b))
    return {
        "reward": round(reward, 6),
        "combination": "A * B",
        "families": {
            "behavior": {
                "score": round(family_a, 6),
                "corpus": {"weight": CORPUS_WEIGHT,
                           "fraction": round(corpus, 6),
                           "detail": corpus_detail},
                "inherited_suite": {"weight": SUITE_WEIGHT,
                                    "fraction": round(inherited, 6),
                                    "detail": suite_detail},
            },
            "gates": {
                "score": round(family_b, 6),
                "detail": gate_detail,
            },
        },
    }


def main(argv=None):
    p = argparse.ArgumentParser(prog="skutil_gates.grade")
    p.add_argument("--behavior", help="probe compare report")
    p.add_argument("--suite", help="inherited suite report")
    p.add_argument("--gates", help="gate score report")
    p.add_argument("--out")
    p.add_argument("--min-flips", type=int, default=0,
                   help="fail the corpus if fewer than this many flips applied "
                        "(trap 4: an unloaded flips file must not read as a pass)")
    args = p.parse_args(argv)

    report = grade(_load(args.behavior), _load(args.suite), _load(args.gates),
                   min_flips=args.min_flips)
    if args.out:
        with open(args.out, "w") as fh:
            json.dump(report, fh, indent=1, sort_keys=True)

    fam = report["families"]
    beh = fam["behavior"]
    print("family A  behavior equivalence  %.4f  "
          "(corpus %.4f of %d, suite %.4f of %d)"
          % (beh["score"], beh["corpus"]["fraction"],
             beh["corpus"]["detail"].get("total", 0),
             beh["inherited_suite"]["fraction"],
             beh["inherited_suite"]["detail"].get("total", 0)))
    cd = beh["corpus"]["detail"]
    if cd.get("flips_total"):
        print("          corpus = must-stay %.4f (%d/%d) x must-flip %.4f (%d/%d)"
              % (cd.get("unchanged_fraction", 0.0),
                 cd.get("unchanged_passed", 0), cd.get("unchanged_total", 0),
                 cd.get("flips_fraction", 0.0),
                 cd.get("flips_passed", 0), cd.get("flips_total", 0)))
    print("family B  migrated-ness gates   %.4f  (new %s x carried %s%s)"
          % (fam["gates"]["score"],
             fam["gates"]["detail"].get("new_mean"),
             fam["gates"]["detail"].get("carried_mean"),
             "" if fam["gates"]["detail"].get("importable", True)
             else ", NOT IMPORTABLE"))
    for key in ("corpus", "inherited_suite"):
        err = beh[key]["detail"].get("error")
        if err:
            print("  !! %s: %s" % (key, err))
    unimported = beh["corpus"]["detail"].get("casebooks_unimported") or []
    if unimported:
        print("  !! %d casebook(s) would not import against this tree, so "
              "their cases are scored missing: %s"
              % (len(unimported), ", ".join(unimported)))
    err = fam["gates"]["detail"].get("error")
    if err:
        print("  !! gates: %s" % err)
    print("reward    A x B                  %.4f x %.4f"
          % (beh["score"], fam["gates"]["score"]))
    print("REWARD %.6f" % report["reward"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
