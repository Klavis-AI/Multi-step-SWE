"""Turn the behaviour recording into a step's scored families.

`behavior.py` answers "which recorded ids reproduced". This module answers
"what is that worth to this step", and it exists because the answer is not the
obvious one.

Rule 8 requires the no-op agent to report **0.0 at every step**. Rule 5 says
the untouched previous-step tree "passes the first family completely" -- and it
does: a tree nobody has touched reproduces its own recording. Scored as a flat
fraction, the recording would therefore hand a no-op run most of a family it
did nothing to earn, and the trial reward would never be zero.

The two rules meet where `gate.py` already puts everything else: *a check that
could not be evaluated is a failure, never a skip*. What a step asks is not
"does the corpus reproduce" but "does the corpus reproduce **on the platform
this step was supposed to deliver**". On a tree that is still on the old
platform that question has no answer, so every one of its checks fails, with
the unmet precondition named in the detail. The precondition is the step's own
deliverable, measured by the step's plan and handed in here; this module never
guesses what it should be.

The second thing here is the partition. A step usually moves only a handful of
the recorded expectations from "free" to "hard": step 01's config change alters
9 of 413 responses and 2 of 52 table snapshots on an unrepaired tree, and those
11 ids are the entire observable difference between a one-line edit and the
work. Left in one flat family they are 2.4% of it. So a step may ship a
`behavior-partition.json` naming those ids, recorded by the authoring probe
that found them, and score them as their own family with their own weight. The
ids inside and outside the partition are compared against the *same* recorded
values -- a partitioned id is not a flip, it is an id whose recorded value is
hard to keep.

Flips and overrides are still `behavior.py`'s business and apply to both
subsets. They are loaded here, once, with the recording, so that every family
in a step is compared against the same expectations -- a step cannot re-record
a response for one family and not for another.

One observation serves every family: the corpus is driven once, cached on the
context, and re-read. Driving it twice would double the step's wall-clock and,
worse, score the second family against a deployment the first family had
already written to.
"""

import os
import traceback

from . import behavior, gate

PARTITION = "/tests/fixtures/behavior-partition.json"

# Where the single observation lives on the context. Any family that asks for
# it gets the same pass, including the one that failed to take it.
SHARED_KEY = "behavior:observation"

# Detail is per check and there are hundreds of them; a full expected/actual
# pair for a 40 KB response would bury the report.
DETAIL_HEAD = 200

# How the one corpus pass is taken. This is plan-level and not family-level on
# purpose: the pass is cached, so if two families asked for it differently the
# first one to ask would silently decide for both.
_RECORDING = {"strict": True, "logmark": None, "taken": False}


def configure(strict=None, logmark=None):
    """Set how the corpus pass will be taken. Called once, at plan import.

    `strict=False` is what a *scorer* wants: a deployment that stops answering
    is then graded on what it did answer, request by request, instead of the
    recorder raising and taking every behaviour family down with it. An
    authoring recording is taken with the default, because a transport error
    while recording is a broken recording (`runner.Recorder`).

    `logmark` is called at each scenario boundary and its value remembered, so
    a family that reads the server log can slice it per scenario without
    driving the corpus a second time.

    Refuses after the pass has been taken: a family that reconfigured the
    recorder would be describing a pass somebody else already took.
    """
    if _RECORDING["taken"]:
        raise RuntimeError("the corpus has already been driven; %s cannot be"
                           " reconfigured now" % __name__)
    if strict is not None:
        _RECORDING["strict"] = bool(strict)
    if logmark is not None:
        _RECORDING["logmark"] = logmark


def load_partition(path=PARTITION):
    """{"name": str, "requests": [...], "tables_before": [...], ...}.

    Absent means "no partition": `subset="inside"` is then empty and
    `subset="outside"` is everything, which is what a step with nothing
    special to weight wants.
    """
    out = {"name": "", "requests": [], "tables_before": [], "tables_after": []}
    if not os.path.isfile(path):
        return out
    import json
    with open(path, "r", encoding="utf-8") as fh:
        data = json.load(fh)
    for key in behavior.KINDS:
        if key in data:
            out[key] = list(data[key])
    out["name"] = str(data.get("name", "")) or os.path.basename(path)
    return out


def observation(ctx, state, base="http://127.0.0.1", host="localhost",
                scenarios=None):
    """The one corpus pass, taken at most once per verifier run.

    Returns (obs, error). Exactly one of them is None. A failure to drive the
    deployment at all is cached too, so five families do not spend five
    timeouts discovering the same dead Apache.
    """
    if SHARED_KEY not in ctx.shared:
        _RECORDING["taken"] = True
        try:
            ctx.shared[SHARED_KEY] = (
                behavior.observe(state(), base=base, host=host,
                                 scenarios=scenarios,
                                 strict=_RECORDING["strict"],
                                 logmark=_RECORDING["logmark"]), None)
        except Exception:
            ctx.shared[SHARED_KEY] = (None, traceback.format_exc(limit=8))
    return ctx.shared[SHARED_KEY]


def scores(ctx, state, fixture=behavior.FIXTURE, flips_path=behavior.FLIPS,
           overrides_path=behavior.OVERRIDES,
           base="http://127.0.0.1", host="localhost", scenarios=None):
    """({kind: {id: bool}}, detail, error) -- the comparison, once.

    The overrides are applied to the recording before anything is compared
    against it, so every family in the step sees the same expectations and an
    overridden id is scored exactly like a recorded one. A malformed override
    raises out of here rather than failing a family: it is the step's own
    fixture, and a step whose expectations do not load has not been authored
    yet.
    """
    key = SHARED_KEY + ":scored"
    if key not in ctx.shared:
        obs, error = observation(ctx, state, base=base, host=host,
                                 scenarios=scenarios)
        if error is not None:
            ctx.shared[key] = (None, None, error)
        else:
            expected = behavior.load(fixture)
            expected, applied = behavior.apply_overrides(
                expected, behavior.load_overrides(overrides_path))
            flips = behavior.load_flips(flips_path)
            per, detail = behavior.per_id(expected, obs, flips)
            detail["overrides"] = applied
            ctx.shared[key] = (per, detail, None)
    return ctx.shared[key]


def _wanted(expected_ids, part, kinds, subset):
    """The ids this family scores, in recording order. The denominator."""
    out = []
    for kind in kinds:
        inside = set(part.get(kind, ()))
        for check_id in expected_ids[kind]:
            if subset == "all" or (check_id in inside) == (subset == "inside"):
                out.append((kind, check_id))
    return out


def _detail_for(kind, check_id, detail):
    # A flipped id fails for the opposite reason an ordinary one does: it
    # reproduced the recording, or it moved in a way the flip does not allow.
    # `problems` below is a diff against the recording, so reading it for a
    # flip would report a *difference* as the failure of a check that requires
    # one -- which is how a flip failure came to be undiagnosable from the
    # report. `collect.flip_reason` says which condition broke; tables invert
    # rather than being scored, so there is only the one thing to say.
    d = (detail or {}).get(kind) or {}
    if check_id in set(d.get("flips_missed") or ()):
        return ((d.get("flip_reasons") or {}).get(check_id)
                or "flip: still matches the recording")

    problems = (detail or {}).get("problems") or []
    for p in problems:
        if p["id"] == check_id and p["kind"].startswith(
                {"requests": "request",
                 "tables_before": "before-table",
                 "tables_after": "after-table",
                 "tables_after_written": "after-table",
                 "tables_after_unwritten": "after-table"}[kind]):
            return "%s expected=%s actual=%s" % (
                p["kind"], repr(p["expected"])[:DETAIL_HEAD],
                repr(p["actual"])[:DETAIL_HEAD])
    return "did not reproduce the recording"


def checks(ctx, name, state, kinds=behavior.KINDS, subset="all",
           precondition=None, fixture=behavior.FIXTURE,
           flips_path=behavior.FLIPS, overrides_path=behavior.OVERRIDES,
           partition_path=PARTITION,
           base="http://127.0.0.1", host="localhost", scenarios=None):
    """One behaviour family.

    `kinds`      which of requests / tables_before / tables_after to score.
    `subset`     "all", or "inside"/"outside" the step's partition.
    `precondition`  (ok, detail). When it is false every check fails with that
                 detail: the step's deliverable is missing, so the recording
                 cannot be asked about it. None means unconditional.
    """
    expected_ids = behavior.ids(behavior.load(fixture))
    part = load_partition(partition_path)
    wanted = _wanted(expected_ids, part, kinds, subset)

    checks = gate.Checks(name)
    if not wanted:
        raise RuntimeError(
            "family %r would score nothing: kinds=%r subset=%r against a"
            " partition of %d/%d/%d ids. A family with an empty denominator"
            " is a plan bug, not a zero." % (
                name, kinds, subset, len(part["requests"]),
                len(part["tables_before"]), len(part["tables_after"])))
    check_ids = ["%s:%s" % (kind, cid) for kind, cid in wanted]

    if precondition is not None and not precondition[0]:
        checks.fail_all(check_ids, "precondition unmet: %s" % precondition[1])
        checks.note("%d recorded expectations not evaluated: %s"
                    % (len(check_ids), precondition[1]))
        return checks

    per, detail, error = scores(ctx, state, fixture=fixture,
                                flips_path=flips_path,
                                overrides_path=overrides_path,
                                base=base, host=host, scenarios=scenarios)
    if error is not None:
        checks.fail_all(check_ids, "the corpus could not be replayed")
        checks.error = error
        return checks

    for (kind, cid), check_id in zip(wanted, check_ids):
        ok = per[kind].get(cid, False)
        checks.add(check_id, ok, "" if ok else _detail_for(kind, cid, detail))

    for kind in kinds:
        d = detail.get(kind) or {}
        for label in ("case_folded", "extra_tables", "case_collisions",
                      "flips_missed", "flip_reasons",
                      "flips_not_in_baseline"):
            if d.get(label):
                checks.note("%s %s: %s" % (kind, label, gate.dumps(d[label])))
    # Only the re-recordings this family actually scores. `wanted` is the
    # family's own id list, so a step that overrides a response and a digest
    # reports each one on the family that reads it and on no other.
    for kind, per in sorted((detail.get("overrides") or {}).items()):
        scored = {cid for k, cid in wanted
                  if k == kind or (kind == "tables_after"
                                   and k in behavior.SPLIT_KINDS)}
        for oid in sorted(set(per) & scored):
            o = per[oid]
            checks.note("re-recorded %s %s: %s -> %s (%s)"
                        % (kind, oid, gate.dumps(o["was"]),
                           gate.dumps(o["now"]), o["why"]))
    if detail.get("session_ids_seen") is not None:
        checks.note("session ids seen: %s" % detail["session_ids_seen"])

    # What the recorder could not do, on every family that reads the pass. A
    # budget that ran out or a deployment that stopped answering is scored as
    # failed ids either way (`collect.request_scores`), but a reader deciding
    # whether the step is well-formed needs to see which of the two it was.
    obs = (ctx.shared.get(SHARED_KEY) or (None, None))[0]
    if obs:
        if obs.get("aborted"):
            checks.note("corpus aborted: %s" % obs["aborted"])
        errs = obs.get("transport_errors") or []
        if errs:
            checks.note("%d transport error(s), recorded as unanswered: %s"
                        % (len(errs),
                           "; ".join("%s %s" % (i, w) for i, w in errs[:6])))
    if part["name"]:
        checks.note("partition %s, subset %s" % (part["name"], subset))
    return checks
