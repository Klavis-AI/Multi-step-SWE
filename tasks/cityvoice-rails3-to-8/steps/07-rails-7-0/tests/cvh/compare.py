"""Compares a recording against the golden transcript.

Two jobs, one implementation, so that the stability check the goldens are
filtered with is literally the comparison the verifiers later run:

  * `--stability a.json b.json` reports which case ids differ between two
    recordings of the same tree.  Anything it names is not a behaviour, it is
    a clock or an ordering accident, and it is dropped from the goldens.
  * `--golden g.json --observed o.json` scores a candidate.

A case scores 1 only on exact equality of its whole projection.  Partial
credit within a case would let a page that lost its form still collect most of
its points; partial credit across cases is what gives the reward its gradient.
"""

from __future__ import annotations

import argparse
import json
import sys


def load(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


class Duplicate(Exception):
    """Two cases in one recording share an id."""


class BadOverlay(Exception):
    """A must-flip overlay does not describe a flip."""


# --------------------------------------------------------------------------
# must-flip overlays
# --------------------------------------------------------------------------
#
# A step may change a recorded answer when the framework documents the change:
# re-record exactly the affected inputs from the golden tree, cite the
# documentation, and make them must-flip.  An overlay is that re-recording, as
# a file:
#
#   {"step": "01-rails-4-2",
#    "cases": {"<case id>": {"obs": {...}, "why": "...", "cite": "..."}}}
#
# Overlays compose in order, so step k's verifier is given the fragments for
# steps 1..k and the behaviour contract it scores is "the baseline, as amended
# by every documented change up to here".  Without that, an input step 1 was
# required to change would be scored against the untouched baseline again at
# step 2, and the step-local mean would hide an inherited case that this step's
# own requirements made unpassable.
#
# Two invariants are enforced at load, because both failures are silent:
#
#   * an overlay case id that is not in the golden is an error, not a new case
#     -- a typo would otherwise add a case nothing can fail;
#   * an overlay whose `obs` equals the observation it replaces is an error --
#     a "must-flip" that does not flip scores exactly like the baseline and
#     tells nobody that the documented change went unmade.

def apply_overlays(golden, overlays):
    """Returns (amended golden, {case id: overlay entry}).

    The golden is copied rather than mutated: the caller is often holding the
    same object to report what the original answer was.
    """
    gold = index(golden)
    # The answer each case had before any overlay touched it.  A case can be
    # flipped twice -- step 02 moves it, step 03 moves it again -- and then
    # "the old answer" names two different strings: the one the step before
    # this one established, and the one the recording started with.  A
    # candidate can be sitting on either, they mean different things (the step
    # was not attempted / the step was attempted from the wrong starting
    # tree), and a report that only knew `was` could only ever name one.
    origin = {case_id: case["obs"] for case_id, case in gold.items()}
    flips = {}
    for overlay in overlays:
        step = overlay.get("step", "<unnamed>")
        for case_id, entry in sorted(overlay.get("cases", {}).items()):
            if case_id not in gold:
                raise BadOverlay(
                    "overlay %s names %r, which is not a recorded case"
                    % (step, case_id))
            if "obs" not in entry:
                raise BadOverlay("overlay %s: %r has no `obs`" % (step, case_id))
            if entry["obs"] == gold[case_id]["obs"]:
                raise BadOverlay(
                    "overlay %s: %r re-records the answer it already had, so it "
                    "is not a flip" % (step, case_id))
            for field in ("why", "cite"):
                if not entry.get(field):
                    raise BadOverlay(
                        "overlay %s: %r has no `%s`; a must-flip input cites the "
                        "documented change or it is not one"
                        % (step, case_id, field))
            case = dict(gold[case_id])
            case["obs"] = entry["obs"]
            previous = flips.get(case_id)
            case["flip"] = {"step": step, "why": entry["why"],
                            "cite": entry["cite"],
                            "was": gold[case_id]["obs"],
                            # Which step put `was` there, or None when it is
                            # the recording's own answer.
                            "was_step": previous["step"] if previous else None,
                            "origin": origin[case_id]}
            gold[case_id] = case
            flips[case_id] = case["flip"]
    return {"cases": [gold[c] for c in sorted(gold)]}, flips


def index(payload):
    """Case id -> case, refusing to silently collapse a duplicate id.

    A dict comprehension over `cases` would let a scenario that reuses an id
    quietly *lose* a case: the count drops, the mean over what is left goes up,
    and nothing anywhere prints a warning.  That is the same shape as a step
    that crashes and disappears from the mean, so it is an error, not a note.
    """
    out = {}
    dupes = []
    for case in payload["cases"]:
        case_id = case["id"]
        if case_id in out:
            dupes.append(case_id)
        out[case_id] = case
    if dupes:
        raise Duplicate("duplicate case ids: %s" % sorted(set(dupes)))
    if not out:
        raise Duplicate("recording contains no cases")
    return out


# --------------------------------------------------------------------------
# difference reporting
# --------------------------------------------------------------------------

def diff_paths(expected, actual, path="", out=None, limit=12):
    """Leaf-level differences, as (json-ish path, expected, actual)."""
    if out is None:
        out = []
    if len(out) >= limit:
        return out
    if type(expected) is not type(actual) and not (
            isinstance(expected, (int, float))
            and isinstance(actual, (int, float))):
        out.append((path or ".", _short(expected), _short(actual)))
        return out
    if isinstance(expected, dict):
        for key in sorted(set(expected) | set(actual)):
            if key not in expected:
                out.append(("%s.%s" % (path, key), "<absent>",
                            _short(actual[key])))
            elif key not in actual:
                out.append(("%s.%s" % (path, key), _short(expected[key]),
                            "<absent>"))
            else:
                diff_paths(expected[key], actual[key],
                           "%s.%s" % (path, key), out, limit)
            if len(out) >= limit:
                break
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            out.append(("%s[]" % path, "len=%d" % len(expected),
                        "len=%d" % len(actual)))
        for i in range(min(len(expected), len(actual))):
            diff_paths(expected[i], actual[i], "%s[%d]" % (path, i), out, limit)
            if len(out) >= limit:
                break
    elif expected != actual:
        out.append((path or ".", _short(expected), _short(actual)))
    return out


def _short(value, width=160):
    text = value if isinstance(value, str) else json.dumps(value, sort_keys=True)
    text = text.replace("\n", "\\n")
    if len(text) > width:
        text = text[:width] + "..."
    return text


# --------------------------------------------------------------------------
# scoring
# --------------------------------------------------------------------------

def dead_worlds(observed):
    """Worlds whose application never came up in this recording.

    The runner brings the tree up once per world and records, per world,
    whether it answered (`worlds.<name>.booted`).  A world that never booted
    produced no HTTP cases at all, but it can still have produced cases from
    the surfaces that do not need the server -- and those are not evidence
    about the application, they are evidence about a tree that does not run.

    This matters because an observation can match by accident.  A recorded
    `db:migrate:status` against a database with no `schema_migrations` is
    "exited non-zero, printed no rows", and so is a `bundle exec` on a tree
    whose gems will not install: same projection, no application involved.
    Scoring that as a reproduced behaviour is how an untouched tree collects a
    case it did nothing to earn.  So the boot flag the runner already writes is
    read here, positively -- a world is evidence when it came up, not when its
    failures happen to look unfamiliar.
    """
    return {name for name, info in (observed.get("worlds") or {}).items()
            if not info.get("booted")}


def score(golden, observed, skip=(), overlays=()):
    golden, flips = apply_overlays(golden, overlays)
    gold = index(golden)
    obs = index(observed)
    skip = set(skip)
    dead = dead_worlds(observed)

    results = []
    for case_id in sorted(gold):
        if case_id in skip:
            continue
        want = gold[case_id]
        got = obs.get(case_id)
        entry = {"id": case_id}
        if got is None:
            entry.update({"ok": False, "reason": "missing", "diff": []})
        elif case_id.split("/", 1)[0] in dead:
            entry.update({"ok": False, "reason": "world-not-booted",
                          "diff": []})
        else:
            entry["ok"] = want["obs"] == got["obs"]
            if not entry["ok"]:
                entry["reason"] = "differs"
                entry["diff"] = diff_paths(want["obs"], got["obs"])
        if case_id in flips:
            # Reported separately as well as scored, because "the candidate
            # still answers the way the old framework did" and "the candidate
            # answers some third way" are different findings and the reward
            # treats them the same.  The label is attached even when the case
            # is missing or its world is dead, so that `flips.total` is the
            # size of the contract and not the size of what happened to run --
            # "must-flip 1/1" on a tree where three of the four inputs never
            # executed reads like a pass and is the opposite of one.
            flip = flips[case_id]
            entry["flip"] = flip["step"]
            # Two ways to be stale, reported as one boolean and one label.
            # `was` is what the step before this one left the case answering;
            # `origin` is what the recording found before any step ran.  They
            # are the same string for a case this is the first flip of, and
            # `stale_as` then says "baseline" for both.
            stale_as = None
            if got is not None:
                if got["obs"] == flip["was"]:
                    stale_as = flip.get("was_step") or "baseline"
                elif got["obs"] == flip.get("origin"):
                    stale_as = "baseline"
            entry["stale"] = stale_as is not None
            if stale_as:
                entry["stale_as"] = stale_as
        results.append(entry)

    passed = sum(1 for r in results if r["ok"])
    total = len(results)
    scored_flips = [r for r in results if "flip" in r]
    # The stale ids paired with both steps that have a claim on them: the step
    # whose overlay owes the flip, and the step whose answer the tree is
    # actually still giving.  Either half alone misleads.  A step-05 must-flip
    # that step 02 had already moved once appears in `stale_by_step` under
    # `02-rails-5-0` -- true, because 02's answer is the one on the screen, and
    # still not what a reader takes it to mean, because nothing about step 02
    # went wrong and the unmade change is step 05's.
    pairs = {}
    for r in scored_flips:
        if r.get("stale_as"):
            pairs.setdefault((r["flip"], r["stale_as"]), []).append(r["id"])
    stale_detail = [{"owed_by": owed, "still_on": still, "ids": sorted(ids)}
                    for (owed, still), ids in sorted(pairs.items())]
    return {
        "total": total,
        "passed": passed,
        "fraction": (passed / total) if total else 0.0,
        "results": results,
        "extra": sorted(set(obs) - set(gold)),
        "dead_worlds": sorted(dead),
        "flips": {
            "total": len(scored_flips),
            "flipped": sum(1 for r in scored_flips if r["ok"]),
            "stale": sorted(r["id"] for r in scored_flips if r.get("stale")),
            # The same ids again, grouped by *whose* answer they are still
            # giving: a step id for "this is where the case stopped moving",
            # `baseline` for "it never moved at all".
            "stale_by_step": {
                label: sorted(r["id"] for r in scored_flips
                              if r.get("stale_as") == label)
                for label in sorted({r["stale_as"] for r in scored_flips
                                     if r.get("stale_as")})
            },
            "stale_detail": stale_detail,
        },
    }


def whose_answer(label):
    """How a `stale_as` label reads in a sentence."""
    return "the baseline answer" if label == "baseline" else "%s's answer" % label


def stability(a, b):
    ai, bi = index(a), index(b)
    unstable = []
    for case_id in sorted(set(ai) | set(bi)):
        if case_id not in ai or case_id not in bi:
            unstable.append((case_id, [("<presence>", case_id in ai,
                                        case_id in bi)]))
        elif ai[case_id]["obs"] != bi[case_id]["obs"]:
            unstable.append((case_id,
                             diff_paths(ai[case_id]["obs"], bi[case_id]["obs"])))
    return unstable


# --------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(prog="cvh.compare")
    parser.add_argument("--stability", nargs=2, metavar=("A", "B"))
    parser.add_argument("--golden")
    parser.add_argument("--observed")
    parser.add_argument("--skip-file",
                        help="newline separated case ids to exclude")
    parser.add_argument("--overlay", action="append", default=[],
                        metavar="PATH",
                        help="must-flip re-recording, applied in order")
    parser.add_argument("--report", help="write the full JSON report here")
    parser.add_argument("--show", type=int, default=25,
                        help="how many failing cases to print")
    args = parser.parse_args(argv)

    if args.stability:
        a, b = (load(p) for p in args.stability)
        unstable = stability(a, b)
        for case_id, diffs in unstable:
            print("UNSTABLE %s" % case_id)
            for path, x, y in diffs[:6]:
                print("    %s\n      a: %s\n      b: %s" % (path, x, y))
        print("%d unstable of %d" % (len(unstable), len(index(a))))
        if args.report:
            with open(args.report, "w", encoding="utf-8") as handle:
                json.dump([c for c, _ in unstable], handle, indent=1)
                handle.write("\n")
        # Non-zero, so that `compare --stability … && promote-the-goldens`
        # cannot promote a pair that did not agree.
        return 1 if unstable else 0

    if not (args.golden and args.observed):
        parser.error("need --golden and --observed, or --stability")

    skip = []
    if args.skip_file:
        with open(args.skip_file, "r", encoding="utf-8") as handle:
            skip = [line.strip() for line in handle
                    if line.strip() and not line.startswith("#")]

    report = score(load(args.golden), load(args.observed), skip,
                   [load(p) for p in args.overlay])
    shown = 0
    for result in report["results"]:
        if result["ok"] or shown >= args.show:
            continue
        shown += 1
        stale = ""
        if result.get("stale_as"):
            stale = (" [STALE: %s owes this flip; the tree is still on %s]"
                     % (result.get("flip"), whose_answer(result["stale_as"])))
        print("FAIL %s (%s)%s" % (result["id"], result["reason"], stale))
        for path, want, got in result["diff"][:6]:
            print("    %s\n      want: %s\n      got:  %s" % (path, want, got))
    print("%d/%d = %.4f" % (report["passed"], report["total"],
                            report["fraction"]))
    if report["flips"]["total"]:
        detail = ""
        if report["flips"]["stale_detail"]:
            detail = " (%s)" % "; ".join(
                "%d owed by %s, still on %s"
                % (len(d["ids"]), d["owed_by"], whose_answer(d["still_on"]))
                for d in report["flips"]["stale_detail"])
        print("must-flip: %d/%d flipped, %d still on an earlier answer%s"
              % (report["flips"]["flipped"], report["flips"]["total"],
                 len(report["flips"]["stale"]), detail))
    if report["extra"]:
        print("extra cases not in golden: %d" % len(report["extra"]))
    if report["dead_worlds"]:
        print("worlds whose application never came up: %s"
              % ", ".join(report["dead_worlds"]))
    if args.report:
        with open(args.report, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=1)
            handle.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
