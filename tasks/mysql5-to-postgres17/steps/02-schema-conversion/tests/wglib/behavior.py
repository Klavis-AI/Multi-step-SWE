"""The behaviour-equivalence recording: load it, replay it, compare per id.

Every expectation here was recorded, never authored.
`fixtures/behavior-baseline.json` is one pass of `collect.collect()` over the
untouched tree -- 413 responses and
two snapshots of all 24 comparable tables -- taken twice and reduced to what
both passes agreed on. Nothing in this module knows what a correct response
looks like; it knows only what the baseline did.

Two things are compared, because a migration can break either one alone. The
responses say the application still behaves the same to a client: status line,
`Location:`, and the sha256 and length of the body, with the PHP session id
substituted out and nothing else normalised. The snapshots say the data is
still there: `tables_before` is the migrated dataset untouched, `tables_after`
is what the corpus wrote into it.

Table names are matched case-insensitively, and that is an instrument decision
with a measured reason. Four of the 27 tables are mixed-case
(`lesson_XSS2_messages` and three siblings) and the application creates and
queries them with unquoted identifiers. MySQL on Linux keeps the case it was
given; PostgreSQL folds an unquoted identifier to lower case. So a *correct*
migration necessarily renames those four, while their contents are unchanged.
Comparing the names byte-for-byte would score a correct migration as a data
loss. Whether the names themselves are acceptable is the schema gate's
question, not this one's; every fold that was needed is reported.

There are two doors for a documented semantic change, and they are not the
same door.

`flips` is the weaker one: a step enumerates the ids that must *differ* from
the recording, and `apply_flips` inverts the test for exactly those. It says
nothing about what the new value is, so it is the right shape only when the new
value is not reproducible -- when what the step changes is that the response
stops being deterministic at all.

Weaker is not free, and it is paid for twice. A flipped *request* must still
keep the recording's status and redirect and differ only in its body, because
"differs from the recording" is exactly what a stack trace does: without the
split, a deployment that answered 500 to everything would pass every flip in
the step without serving a page. `collect.request_flip_scores` is that test and
`collect.REQUEST_ENVELOPE` is the part a flip may not move. The envelope split
does not reach a 200 that renders a warning where the lesson used to be, so a
step may also record `request_anchors`: the digests of the end or ends of the
body that its own golden tree and the recording *agree* on -- the page's own
chrome, which the step does not change. An anchored flip says "this part of the
page is still there and the middle of it is the server's business", which is
the whole of what a flip is for. Either end may be absent, and that is a
measurement rather than a slackening: an anchor is only ever recorded over
bytes the two trees already share, so an end they stop sharing early cannot
carry one without the anchor becoming a demand for the golden tree's own new
bytes. How *much* of an end is anchored is measured the same way -- the step
records the largest rung of `runner.ANCHOR_LADDER` that fits inside the
agreement, so an id that agrees on 915 bytes is anchored on 768 of them rather
than on none.

The ends are still only the ends. What reads the middle is the error surface:
`runner` records, for every response, which of `httpdrv.ERROR_SURFACE` its body
carries, and a flipped body may carry none. That is not an expectation about
the new value either -- both surfaces are switched off in the tree the
candidate is handed, so a body carrying one is a deployment that turned PHP's
display or the framework's handler back on.

`overrides` is the stronger one, and it is what rule 5's exception clause
describes: the affected inputs are **re-recorded from that step's golden tree**,
the new expectation is compared byte for byte like every other, and
`apply_overrides` refuses to load one that does not differ from the baseline.
Both responses and table snapshots can be overridden. For every step up to the
cutover only responses ever needed to be, because no step before one changed
what a row holds -- but a step whose whole requirement is to repair the
contents of a column does change exactly that, and the digests of the tables it
repairs move with it. Leaving those families out of such a step instead would
drop the only check on the tables it does *not* repair, which is the larger
half of the snapshot.
That refusal is the whole mechanism. An override that matched the recording
would quietly turn a scored check into a free one; an override that differs is
a check the untouched previous-step tree necessarily fails -- it reproduces the
baseline, and the baseline is no longer what is asked for -- and so does a
compatibility shim that fakes the old behaviour. Keep the set small, enumerate
it, and cite the documentation for each entry in its `why`.

An empty flip set and no override file -- steps 01 to 03 -- means every
recorded value must reproduce exactly.

This module stops at "which ids passed". Turning that into a family score, a
denominator and a weight is `behaviorgate.py`'s job, because how much of a
step's reward the recording carries is a per-step decision and belongs in that
step's plan, not in the comparison.
"""

import json
import os

from . import collect
from . import dbstate

# The recording of the untouched tree, shared by every step. The flip list is
# per step and is written into that step's fixtures directory by
# authoring/sync_tests.py; a step with nothing to flip simply has no file, and
# `load_flips` returns an empty set.
FIXTURE = "/tests/fixtures/behavior-baseline.json"
FLIPS = "/tests/fixtures/behavior-flips.json"
OVERRIDES = "/tests/fixtures/behavior-overrides.json"

KINDS = ("requests", "tables_before", "tables_after")

# Two derived kinds, offered beside `tables_after` and never instead of it: the
# tables the corpus writes, and the tables it does not. The line between them
# is drawn by the recording rather than by hand -- a table whose recorded
# "after" snapshot equals its recorded "before" snapshot is one this corpus
# never touches, so reproducing its "after" is something a tree that does
# nothing at all already does. Scoring those eleven in the same earned family
# as the thirteen the corpus writes is what made `behavior:tables-after` 11/24
# true on the tree the candidate is handed. A step that wants the distinction
# asks for `tables_after_written` and `tables_after_unwritten`; a step that
# does not keeps asking for `tables_after` and sees all of them, which is what
# steps 01-03 do.
SPLIT_KINDS = ("tables_after_written", "tables_after_unwritten")

# The four fields a recorded response is compared on, and the four an override
# has to carry. `collect` owns the tuple because `request_scores` is what reads
# it; it is bound here so the override loader cannot drift from the comparison.
REQUEST_FIELDS = collect.REQUEST_FIELDS

# How many failures to name in the report, per problem group -- see
# `_problem_group`. A run that breaks everything should not produce a megabyte
# of JSON, but the budget is spent per group so that a heavy request run cannot
# push the table detail out of the report.
MAX_NAMED = 200


# The three groups `behaviorgate` looks a problem up under. Keep in step with
# the kind prefixes `collect.compare` emits.
PROBLEM_GROUPS = ("request", "before-table", "after-table")


def _problem_group(kind):
    for prefix in PROBLEM_GROUPS:
        if kind.startswith(prefix):
            return prefix
    return kind


class MissingFixture(RuntimeError):
    pass


def load(path=FIXTURE):
    if not os.path.isfile(path):
        raise MissingFixture("no recorded baseline at %s" % path)
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def load_flips(path=FLIPS):
    """{"requests": [...], "tables_before": [...], "tables_after": [...]}.

    Plus `request_anchors`, the per-id body anchors described on
    `collect.request_flip_scores`: {} when the file has none, which is every
    step before the one that first flips a request.
    """
    empty = {k: [] for k in KINDS}
    empty["request_anchors"] = {}
    if not os.path.isfile(path):
        return empty
    with open(path, "r", encoding="utf-8") as fh:
        data = json.load(fh)
    empty.update({k: list(v) for k, v in data.items() if k in KINDS})
    empty["request_anchors"] = {str(k): v for k, v
                                in (data.get("request_anchors") or {}).items()}
    unknown = sorted(set(empty["request_anchors"]) - set(empty["requests"]))
    if unknown:
        raise RuntimeError(
            "%s anchors %d id(s) it does not flip: %s. An anchor on an id that"
            " is scored normally would be a second, weaker test of the same"
            " response." % (path, len(unknown), ", ".join(unknown[:8])))
    for rid, entry in sorted(empty["request_anchors"].items()):
        for end in ("head", "tail"):
            spec = entry.get(end)
            if spec is None:
                continue
            if not (isinstance(spec, dict) and isinstance(spec.get("bytes"), int)
                    and isinstance(spec.get("sha256"), str)):
                raise RuntimeError(
                    "%s: anchor %s/%s is %r, not {\"bytes\": n, \"sha256\":"
                    " s}. An anchor names the length it was taken over, because"
                    " the record digests every end at several lengths and a"
                    " digest with no length is a digest of nothing in"
                    " particular." % (path, rid, end, spec))
    empty_anchor = sorted(k for k, v in empty["request_anchors"].items()
                          if not (v.get("head") or v.get("tail")))
    if empty_anchor:
        # An entry with neither end is not "anchored loosely", it is an
        # anchored id that nothing is compared on -- the envelope test with a
        # key beside it saying otherwise. Refuse it at load rather than score
        # it as if the anchor were there.
        raise RuntimeError(
            "%s anchors %d id(s) on neither end: %s. An anchor names the end or"
            " ends of the body the golden tree and the recording agree on, and"
            " an entry with neither is a check that compares nothing."
            % (path, len(empty_anchor), ", ".join(empty_anchor[:8])))
    return empty


class BadOverride(RuntimeError):
    pass


# What an override has to carry, per kind. A response is re-recorded whole;
# a table snapshot is the pair the recorder writes for it.
# Free-text keys the recorder writes beside the kinds; not scored.
OVERRIDE_NOTES = frozenset(("note", "recorded_from"))

OVERRIDE_FIELDS = {
    "requests": REQUEST_FIELDS,
    "tables_before": ("digest", "rows"),
    "tables_after": ("digest", "rows"),
}


def load_overrides(path=OVERRIDES):
    """{kind: {id: {the kind's fields, why}}} for this step, or {}.

    Absent means "this step changes nothing the recording saw", which is the
    ordinary case and the one every step before the cutover is in. A file that
    carries only `requests` -- which is every one written before step 06 --
    loads to a single-kind mapping and behaves exactly as it did.
    """
    if not os.path.isfile(path):
        return {}
    with open(path, "r", encoding="utf-8") as fh:
        data = json.load(fh)
    out = {}
    for kind in OVERRIDE_FIELDS:
        got = data.get(kind) or {}
        if got:
            out[kind] = {str(k): v for k, v in got.items()}
    # A key that is neither a kind nor one of the recorder's own annotations
    # is a typo, and a typo here is a whole kind silently not overridden.
    unknown = sorted(set(data) - set(OVERRIDE_FIELDS) - OVERRIDE_NOTES)
    if unknown:
        raise BadOverride(
            "override file names %s, which is neither a kind that can be"
            " overridden (%s) nor an annotation (%s)"
            % (", ".join(unknown), ", ".join(sorted(OVERRIDE_FIELDS)),
               ", ".join(sorted(OVERRIDE_NOTES))))
    return out


def apply_overrides(expected, overrides):
    """(a re-recorded copy of `expected`, {kind: {id: what changed and why}}).

    Four things are refused rather than reported, because each of them would
    make a scored check weaker than it reads and none of them can be caused by
    anything the candidate does -- the file is the step's own fixture, so a
    fault in it is an authoring fault and belongs at load time.

      * an id the recording does not have. The denominator is the recording;
        an override outside it scores nothing and hides a typo.
      * a missing field. The fields that were carried would be compared
        against the new recording and the rest against the old one.
      * an override that agrees with the recording on every field. That is not
        a documented change, it is a check turned free: the untouched tree
        would pass it.
      * an empty `why`. Rule 5's exception clause is "cite the documentation";
        an override without a citation is an unexplained expectation, which is
        the thing the recording exists to avoid.

    A table snapshot is overridable on the same four terms as a response, and
    for one narrow reason: a step whose requirement is to change what a row
    holds. That is the encoding repair and nothing before it. A step that
    merely rewrites SQL has no business moving a digest, and the refusals above
    are what stops it from doing so quietly -- an override that reproduces the
    recording is rejected, so a digest can only be moved by a tree that really
    does hold different bytes, and the `why` has to say which requirement made
    it.
    """
    if not overrides:
        return expected, {}
    out = dict(expected)
    applied = {}

    reqs = overrides.get("requests") or {}
    if reqs:
        at = {r["id"]: i for i, r in enumerate(expected["requests"])}
        rows = [dict(r) for r in expected["requests"]]
        for rid in sorted(reqs):
            if rid not in at:
                raise BadOverride(
                    "override %r names a request the recording does not have"
                    % rid)
            was = rows[at[rid]]
            want = _checked(rid, "requests", was, reqs[rid])
            row = dict(was)
            row.update({f: want[f] for f in REQUEST_FIELDS})
            rows[at[rid]] = row
            applied.setdefault("requests", {})[rid] = _record(
                "requests", was, want)
        out["requests"] = rows

    for kind in ("tables_before", "tables_after"):
        want_all = overrides.get(kind) or {}
        if not want_all:
            continue
        snap = {t: dict(v) for t, v in expected[kind].items()}
        for table in sorted(want_all):
            if table not in snap:
                raise BadOverride(
                    "override %r names a table the recording's %s snapshot"
                    " does not have" % (table, kind))
            if table in dbstate.EXCLUDED_TABLES:
                # `comparable` drops these before anything is scored, so an
                # override on one is a fixture entry that can never be read.
                raise BadOverride(
                    "override %r is on a table whose contents are not"
                    " comparable state (%s); it would never be scored"
                    % (table, kind))
            was = snap[table]
            want = _checked(table, kind, was, want_all[table])
            snap[table] = {f: want[f] for f in OVERRIDE_FIELDS[kind]}
            applied.setdefault(kind, {})[table] = _record(kind, was, want)
        out[kind] = snap

    return out, applied


def _checked(oid, kind, was, want):
    """One override, past the three refusals that do not need the snapshot."""
    fields = OVERRIDE_FIELDS[kind]
    missing = [f for f in fields if f not in want]
    if missing:
        raise BadOverride(
            "override %r (%s) carries no %s; a partial override compares some"
            " fields against the new recording and the rest against the old"
            " one" % (oid, kind, ", ".join(missing)))
    if not str(want.get("why", "")).strip():
        raise BadOverride(
            "override %r (%s) cites nothing. An expectation that differs from"
            " the recording has to say which documented behaviour makes it"
            " differ" % (oid, kind))
    if all(was[f] == want[f] for f in fields):
        raise BadOverride(
            "override %r (%s) reproduces the recording exactly. An override"
            " that does not differ is a scored check made free: the untouched"
            " tree passes it" % (oid, kind))
    return want


def _record(kind, was, want):
    fields = OVERRIDE_FIELDS[kind]
    return {"was": {f: was[f] for f in fields},
            "now": {f: want[f] for f in fields},
            "why": str(want["why"]).strip()}


def comparable(d):
    """Strip the tables whose contents are not comparable state.

    The behaviour families read their table lists straight out of the
    recording rather than through `datagate.split_tables`, so the exclusion
    has to be applied here too or the denominator would keep tables the live
    snapshot no longer carries. `dbstate.EXCLUDED_TABLES` is the single source
    of truth for which those are. §24.18.
    """
    return {t: v for t, v in d.items() if t not in dbstate.EXCLUDED_TABLES}


def ids(expected):
    """The recorded ids, per kind, in recording order. The denominator.

    Taken from the fixture and nothing else, so it is known before the
    deployment is looked at.
    """
    written, unwritten = split_after(expected)
    return {"requests": [r["id"] for r in expected["requests"]],
            "tables_before": sorted(comparable(expected["tables_before"])),
            "tables_after": sorted(comparable(expected["tables_after"])),
            "tables_after_written": written,
            "tables_after_unwritten": unwritten}


def split_after(expected):
    """(written, unwritten) -- the two halves of the "after" snapshot.

    Read out of the recording, so the denominator of each half is fixed before
    the deployment is looked at, exactly as `ids` is.
    """
    before = comparable(expected["tables_before"])
    after = comparable(expected["tables_after"])
    written = sorted(t for t in after if after[t] != before.get(t))
    unwritten = sorted(t for t in after if after[t] == before.get(t))
    return written, unwritten


def observe(state, base="http://127.0.0.1", host="localhost", scenarios=None,
            strict=True, logmark=None):
    """One pass over the corpus against a live deployment.

    `strict` and `logmark` belong to `collect.collect` and are passed through
    unread: the recorder decides what a transport error means and what a
    scenario boundary is worth marking, and this module only compares.
    """
    return collect.collect(state, base=base, host=host, scenarios=scenarios,
                           strict=strict, logmark=logmark)


# ------------------------------------------------------------ comparison ---

def _fold(tables):
    """{lowercased name: [(original name, value)]}.

    Two tables differing only in case cannot both exist in PostgreSQL unless
    they were quoted into existence, and if they were, one of them is not the
    one the application reads. Collisions are reported by the caller.
    """
    out = {}
    for name, value in tables.items():
        out.setdefault(name.lower(), []).append((name, value))
    return out


def compare_tables(expected, actual):
    """(scores, folded, extra, collisions) for one snapshot.

    `scores` is {expected name: bool}; `folded` names the tables that matched
    only after case folding; `extra` names tables the baseline never had.
    """
    act = _fold(actual)
    scores, folded, collisions = {}, [], []
    claimed = set()
    for name, exp in expected.items():
        key = name.lower()
        hits = act.get(key, [])
        if len(hits) > 1:
            collisions.append(sorted(n for n, _ in hits))
        hit = None
        for actual_name, value in hits:
            if actual_name == name:
                hit = (actual_name, value)
                break
        if hit is None and hits:
            hit = hits[0]
            folded.append({"expected": name, "actual": hit[0]})
        if hit is None:
            scores[name] = False
            continue
        claimed.add(hit[0])
        scores[name] = (hit[1] == exp)
    extra = sorted(n for n in actual if n not in claimed
                   and n not in expected)
    return scores, folded, extra, collisions


def apply_flips(scores, flipped, flipped_scores=None):
    """A flipped id passes when it *differs* from the recording.

    `flipped_scores` is the flipped test when plain inversion is too weak to
    be a test at all: `collect.request_flip_scores` still requires the
    recording's status and redirect and only lets the body move. Tables have
    no envelope to keep, so they invert.

    Returns (scores, held, missed, not_in_baseline). A flip naming an id the
    recording does not have is a fixture bug, reported rather than ignored.
    """
    flipped = set(flipped)
    out, held, missed = {}, [], []
    for key, matched in scores.items():
        if key in flipped:
            ok = (not matched) if flipped_scores is None \
                else bool(flipped_scores.get(key))
            out[key] = ok
            (held if ok else missed).append(key)
        else:
            out[key] = matched
    return out, held, missed, sorted(flipped - set(scores))


def per_id(expected, actual, flips=None):
    """{kind: {id: bool}} plus the evidence behind the table comparison.

    The one call a step's behaviour family makes. Every recorded id appears in
    the result, so the caller's denominator is the recording's size whatever
    the deployment did.
    """
    if flips is None:
        flips = {k: [] for k in KINDS}

    scores, detail = {}, {}

    req = collect.request_scores(expected, actual)
    # Why each flipped id did not hold. The flipped test scores every id in
    # the recording, so the reasons are kept for the flipped ones only: "this
    # ordinary id reproduced the recording" is not a finding.
    why = {}
    req, held, missed, unknown = apply_flips(
        req, flips["requests"],
        collect.request_flip_scores(expected, actual,
                                    flips.get("request_anchors"), why))
    scores["requests"] = req
    detail["requests"] = {"flips_held": held, "flips_missed": missed,
                          "flips_not_in_baseline": unknown,
                          "flip_reasons": {k: why[k] for k in missed
                                           if k in why}}

    for when in ("before", "after"):
        kind = "tables_" + when
        s, folded, extra, collisions = compare_tables(comparable(expected[kind]),
                                                      actual[kind])
        s, held, missed, unknown = apply_flips(s, flips[kind])
        scores[kind] = s
        detail[kind] = {"case_folded": folded, "extra_tables": extra,
                        "case_collisions": collisions,
                        "flips_held": held, "flips_missed": missed,
                        "flips_not_in_baseline": unknown}

    # The same comparison, read twice. `apply_flips` has already run over the
    # whole "after" snapshot, so a flipped table is scored the same way
    # whichever half it lands in.
    written, unwritten = split_after(expected)
    scores["tables_after_written"] = {t: scores["tables_after"][t]
                                      for t in written}
    scores["tables_after_unwritten"] = {t: scores["tables_after"][t]
                                        for t in unwritten}
    detail["tables_after_written"] = detail["tables_after"]
    detail["tables_after_unwritten"] = detail["tables_after"]

    detail["session_ids_seen"] = actual.get("session_ids_seen")
    # One budget per group, not one shared budget. `collect.compare` emits
    # every request problem before the first table problem, so a flat
    # truncation lets a heavy request run spend the whole budget and leave the
    # tables with no named problem at all -- which is how `behaviorgate` came
    # to answer "did not reproduce the recording" for a table whose expected
    # and actual values it was holding. Naming is reporting only: nothing here
    # feeds `scores`.
    spent, named = {}, []
    for k, i, e, a in collect.compare(expected, actual):
        g = _problem_group(k)
        if spent.get(g, 0) >= MAX_NAMED:
            continue
        spent[g] = spent.get(g, 0) + 1
        named.append({"kind": k, "id": i, "expected": e, "actual": a})
    detail["problems"] = named
    return scores, detail
