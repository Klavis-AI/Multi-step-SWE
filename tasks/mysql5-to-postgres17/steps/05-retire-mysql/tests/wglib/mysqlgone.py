"""The driver gate: nothing in the tree may reach for a MySQL driver.

The dialect gate (`sqlgate`) asks whether the SQL this tree sends is
PostgreSQL SQL. That is a different question from whether the tree is still
wired to MySQL, and a tree can pass the first while failing the second
completely: `new mysqli(...)`, `mysqli_real_escape_string(...)`, a DSN of
`"mysql:dbname=..."` and `'driver' => 'pdo_mysql'` contain no SQL at all.

The instrument is `driverscan.php`, which walks each file's AST and reports
six kinds of site -- see its header for why each one is a site and why the
scan is not a grep. This module turns a scan into a score.

The denominator is the *recorded baseline*: every MySQL driver site the pinned
codebase had, one check each, and each one passes when that site is gone. It
is fixed before the candidate's tree is looked at, so a candidate who deletes
half the wiring scores half, and a candidate who deletes the whole application
does not score at all -- `files-scanned` is there because an empty `/app`
otherwise reports zero sites and looks perfect.

A step may hand `record` a `drop` list of files some other family already
accounts for, and their sites leave the denominator. Step 05 does: seven of
its eight sites are in files its inventory guard deletes outright, so counting
them here would score one deletion eight times. What is left is the site that
needs an edit. The dropped sites stay in `vocabulary`, so `no-new-sites`
still knows the codebase once had them.

Sites are matched by (kind, detail) across the whole tree, not by path. Moving
`DB_mysqli` into a differently-named file is not removing it, and a
path-keyed check would call that a pass twice over -- once because the old
path is empty and once because the new path was never in the baseline.

Everything is in scope, vendored trees included (rule 11). Doctrine's bundled
Mysqli driver is one of the seventeen; a MySQL driver that is merely never
loaded is still a MySQL driver in the tree.
"""

import json
import os
import re

from . import deploy
from . import gate

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = "/tests/fixtures/drivers-baseline.json"

KINDS = ("new", "call", "dsn", "name", "classdef", "extension")

# A scan that walked fewer files than this fraction of the baseline's did not
# scan the application. Trap 5: an empty `/app` is silent, not clean.
MIN_FILE_FRACTION = 0.5

MAX_SHOWN = 8

# A full pass over this application is 905 .php files and takes about four
# seconds on the verifier image. 600 was inherited from sqlgate.EXTRACT_TIMEOUT
# with no measurement under it; a scanner that has not answered in four minutes
# is wedged, not slow, and the cost of cutting it short is one failed family
# rather than a scoring run Harbor kills.
SCAN_TIMEOUT = 240


# ------------------------------------------------------------------- scan ---

def scan(roots, tools=None):
    """Run driverscan.php over `roots`.

    Returns its JSON unchanged: {"sites": [...], "unparsable": [...],
    "files": n}. Raises RuntimeError if the scanner itself could not run --
    the caller turns that into failed checks, never into a skip.
    """
    tools = tools or HERE
    # As in `sqlgate.run_extractor`: candidate code is parsed by an
    # unprivileged account, under a bound, so neither a hostile tree nor a
    # non-terminating one is read by root or turns into a Harbor kill.
    p = deploy.run(
        deploy.as_user(deploy.NOBODY,
                       ["php", os.path.join(tools, "driverscan.php")]
                       + list(roots)),
        timeout=SCAN_TIMEOUT)
    if p.returncode != 0:
        raise RuntimeError("driverscan.php failed (rc=%d): %s"
                           % (p.returncode, deploy.diagnostics(p)))
    return json.loads(p.stdout.decode("utf-8", "replace"))


def key(site):
    """What identifies a site across a refactor: its kind and what it names."""
    return (site["kind"], site["detail"])


def counts(sites):
    """{(kind, detail): how many}."""
    out = {}
    for s in sites:
        k = key(s)
        out[k] = out.get(k, 0) + 1
    return out


def where(sites, k):
    """Human-readable locations of the sites matching one key."""
    return ["%s:%s" % (s["file"], s["line"]) for s in sites if key(s) == k]


# ---------------------------------------------------------------- fixture ---

def load(path=FIXTURE):
    with open(path, "rb") as fh:
        return json.loads(fh.read().decode("utf-8"))


def record(roots, drop=(), tools=None):
    """AUTHORING: the baseline, recorded from the pinned tree.

    `drop` is a list of paths relative to the root whose sites are not part of
    the denominator, because some other family already establishes that those
    files are gone. At step 05 that is the inventory's `gone` list: seven of
    the eight baseline sites live in files the inventory deletes outright, so
    scoring them here asks the same question twice and lets a tree that
    deleted the files look like it did eight things. The one site that
    survives the filter is the one that needs an edit rather than a deletion.

    The dropped sites still go into `vocabulary`, which `evaluate` reads as
    "this codebase had that". Without it a tree that has not got to the
    deletions yet would fail `no-new-sites` for sites that were always there,
    which reads as an invention rather than as unfinished work.
    """
    data = scan(roots, tools)
    roots = [os.path.abspath(r).rstrip("/") for r in roots]
    drop = set(drop)

    def rel(path):
        for r in roots:
            if path == r or path.startswith(r + "/"):
                return path[len(r) + 1:]
        return path

    sites = sorted(({"file": rel(s["file"]), "line": s["line"],
                     "kind": s["kind"], "detail": s["detail"],
                     "class": s["class"], "function": s["function"]}
                    for s in data["sites"] if rel(s["file"]) not in drop),
                   key=lambda s: (s["file"], s["line"], s["kind"],
                                  s["detail"]))
    # `evaluate` counts sites by key over the whole tree, so a key that is on
    # both sides of the filter could never reach zero and its check could
    # never pass. Refuse the recording rather than ship an unpassable check.
    shared = set(counts(sites)) & set(
        counts([s for s in data["sites"] if rel(s["file"]) in drop]))
    if shared:
        raise SelfTestError(
            "%d site key(s) appear both in a dropped file and in a kept one,"
            " so the kept one could never read as gone: %s"
            % (len(shared), ", ".join(repr(k) for k in sorted(shared))))
    return {"files": data["files"], "unparsable": data["unparsable"],
            "sites": sites,
            "vocabulary": sorted([list(k) + [n] for k, n
                                  in counts(data["sites"]).items()])}


def _slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-") or "x"


def check_ids(fixture=FIXTURE):
    """The denominator, without looking at any tree."""
    want = load(fixture) if isinstance(fixture, str) else fixture
    ids = ["driver/parseable", "driver/files-scanned", "driver/no-new-sites"]
    seen = {}
    for s in sorted(want["sites"], key=lambda s: (s["kind"], s["detail"])):
        k = key(s)
        seen[k] = seen.get(k, 0) + 1
        ids.append("driver/gone/%s/%s#%d" % (k[0], _slug(k[1]), seen[k]))
    return ids


# --------------------------------------------------------------- evaluate ---

def evaluate(roots, fixture=FIXTURE, tools=None, family="driver"):
    """The driver family for one candidate tree.

    `family` is the name it reports under, because which step this gate is a
    scored family of and which it is a guard of is the step's decision and not
    this module's -- `score.py` matches `plan.GUARDS` against the name a gate
    gives itself. The check ids are `driver/...` either way.
    """
    c = gate.Checks(family)
    try:
        want = load(fixture) if isinstance(fixture, str) else fixture
    except (OSError, ValueError) as e:
        return gate.crashed(family, "no recorded driver baseline: %s" % e, 3)

    ids = check_ids(want)
    if "why" not in _INSTRUMENT:
        try:
            selftest(tools)
        except Exception as e:
            _INSTRUMENT["why"] = ("the scorer's driver scanner failed its own"
                                  " selftest (%s) so nothing it reported about"
                                  " this tree was believed"
                                  % " ".join(str(e).split())[:300])
        else:
            _INSTRUMENT["why"] = None
    if _INSTRUMENT["why"]:
        c.fail_all(ids, _INSTRUMENT["why"])
        return c
    try:
        data = scan(roots, tools)
    except Exception as e:                      # a tree the scanner cannot read
        return gate.crashed(family, "cannot scan the tree: %s" % e, len(ids))

    c.add("driver/parseable", not data["unparsable"],
          "" if not data["unparsable"]
          else "%d file(s) the PHP parser could not read: %s"
          % (len(data["unparsable"]),
             ", ".join(u["file"] for u in data["unparsable"][:MAX_SHOWN])))

    # Trap 5, in gate form. A tree with almost no PHP in it reports almost no
    # sites, and every "is it gone" check below would pass -- an empty /app
    # scoring 0.95. Nothing has been shown to be gone; the question could not
    # be asked. An unevaluable check is a failed check, so the rest of the
    # family fails with it rather than being quietly removed.
    floor = int(want["files"] * MIN_FILE_FRACTION)
    enough = data["files"] >= floor
    short = ("scanned %d .php files; the baseline tree has %d, so anything"
             " under %d is not this application" % (data["files"],
                                                    want["files"], floor))
    c.add("driver/files-scanned", enough, "" if enough else short)
    if not enough:
        c.fail_all([i for i in ids
                    if i not in ("driver/parseable", "driver/files-scanned")],
                   short)
        return c

    have = counts(data["sites"])
    baseline = counts(want["sites"])

    # One check per baseline site. Two sites that say the same thing are two
    # checks, and removing one of them earns one of them.
    for k in sorted(baseline):
        left = have.get(k, 0)
        for i in range(1, baseline[k] + 1):
            cid = "driver/gone/%s/%s#%d" % (k[0], _slug(k[1]), i)
            gone = i > left
            c.add(cid, gone,
                  "" if gone else "%s %r still present at %s"
                  % (k[0], k[1], ", ".join(where(data["sites"], k)[:MAX_SHOWN])))

    # Anything the baseline never had. Without this, relocating the wiring
    # into a new shape would pass every check above. "Never had" is measured
    # against `vocabulary` -- every site the pinned tree held, including the
    # ones another family is responsible for -- so that a file still waiting
    # to be deleted reads as unfinished work there and not as an invention
    # here. It is a count and not a set, for the same reason `retire`'s
    # `no-new-literals` is: as bare membership this said "the pinned tree
    # called `mysqli_init` somewhere", which a tree that had grown forty new
    # calls to it also satisfies. An older fixture without the key falls back
    # to its scored sites, which is the behaviour this check had before.
    ceiling = dict((tuple(v[:2]), v[2]) for v in want.get("vocabulary", []))
    ceiling = ceiling or dict(baseline)
    new = sorted(k for k in have
                 if have[k] > max(ceiling.get(k, 0), baseline.get(k, 0)))
    c.add("driver/no-new-sites", not new,
          "" if not new else "%d MySQL driver site(s) beyond what the"
          " baseline tree held: %s"
          % (sum(have[k] for k in new),
             "; ".join("%s %r at %s"
                       % (k[0], k[1], where(data["sites"], k)[0])
                       for k in new[:MAX_SHOWN])))

    c.note("%d .php files, %d site(s) remaining of %d"
           % (data["files"], len(data["sites"]), len(want["sites"])))
    return c


# --------------------------------------------------------------- selftest ---

class SelfTestError(Exception):
    pass


# Memoized: one PHP process, and the answer cannot change within a run. A
# scanner that cannot find a driver reports a clean tree, which is exactly
# what a finished migration reports -- so it is asked a question with a known
# answer before any of its answers are believed. See `evaluate`.
_INSTRUMENT = {}


def selftest(tools=None):
    """Prove the scanner against a known answer before it is believed.

    `selftest/drivers_clean.php` is a PostgreSQL-wired file that mentions
    MySQL nine ways -- class names, method names, comments, a table name, a
    thrown message -- and must produce no site at all. Removing the word is
    not the requirement; removing the driver is.

    `selftest/drivers_dirty.php` is one site per method, and each method's
    name begins with the kind that method must produce. The expectation is
    therefore the fixture's own text: adding a case extends this check, and a
    case that stops being detected fails it by name. Class declarations carry
    the `classdef` kind, which cannot live inside a method.

    Raises SelfTestError naming everything that came out wrong.
    """
    tools = tools or HERE
    st = os.path.join(tools, "selftest")
    wrong = []

    clean = scan([os.path.join(st, "drivers_clean.php")], tools)
    if clean["unparsable"]:
        raise SelfTestError("the scanner could not read its own clean fixture:"
                            " %r" % clean["unparsable"])
    for s in clean["sites"]:
        wrong.append("clean fixture: %s %r reported at %s:%d (%s)"
                     % (s["kind"], s["detail"], os.path.basename(s["file"]),
                        s["line"], s["function"] or s["class"]))

    dirty = scan([os.path.join(st, "drivers_dirty.php")], tools)
    if dirty["unparsable"]:
        raise SelfTestError("the scanner could not read its own dirty fixture:"
                            " %r" % dirty["unparsable"])

    # What the fixture claims, read out of its own method and class names.
    by_method = {}
    for s in dirty["sites"]:
        if s["function"]:
            by_method.setdefault(s["function"], set()).add(s["kind"])
    declared = _declared_methods(os.path.join(st, "drivers_dirty.php"))

    per_kind = {}
    for name in declared:
        kind = name.split("_", 1)[0]
        if kind not in KINDS:
            continue
        per_kind[kind] = per_kind.get(kind, 0) + 1
        got = by_method.get(name, set())
        if kind not in got:
            wrong.append("dirty fixture: %s() is a %s site and the scanner"
                         " reported %s" % (name, kind,
                                           ", ".join(sorted(got)) or "nothing"))
        for extra in sorted(got - {kind}):
            wrong.append("dirty fixture: %s() also reported a %s site"
                         % (name, extra))

    classdefs = set(s["detail"] for s in dirty["sites"]
                    if s["kind"] == "classdef")
    for want in ("DB_mysqli", "DB_pdo_mysql"):
        if want not in classdefs:
            wrong.append("dirty fixture: class %s was not reported as a"
                         " classdef" % want)
    per_kind["classdef"] = len(classdefs)

    # A fixture that has been emptied out still passes every check above,
    # because there is nothing left to get wrong. These floors are what stops
    # that: each is the number of cases the fixture was written with.
    floor = {"new": 2, "call": 2, "dsn": 3, "name": 3, "extension": 1,
             "classdef": 2}
    for kind, least in sorted(floor.items()):
        if per_kind.get(kind, 0) < least:
            wrong.append("dirty fixture has %d %s case(s), expected at least"
                         " %d" % (per_kind.get(kind, 0), kind, least))

    if wrong:
        raise SelfTestError("; ".join(wrong))
    return {"clean_sites": len(clean["sites"]),
            "dirty_sites": len(dirty["sites"]), "cases": per_kind}


_METHOD = re.compile(r"^\s*(?:public|protected|private)?\s*function\s+"
                     r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", re.M)


def _declared_methods(path):
    """The method names the dirty fixture declares, in source order."""
    with open(path, "rb") as fh:
        return _METHOD.findall(fh.read().decode("utf-8"))
