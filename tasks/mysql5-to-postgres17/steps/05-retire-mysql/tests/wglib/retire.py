"""Step 5's four families: is the old server actually gone.

`sqlgate` asks whether the SQL is PostgreSQL's. `mysqlgone` asks whether the
tree still reaches for a MySQL *driver*. Neither sees the two shapes a
half-finished retirement leaves -- a class that survives its parent, and a
registry that still advertises a back-end whose implementation is gone -- and
neither sees the machine recipe that reinstalls the server on the next
provision. See NOTES-authoring.md §24.

Which family is scored and which is a guard is the whole design (§24.2, and
§24.14 for the second cut): everything a name-based sweep over the tree can do
is a guard, because it must be true and is not evidence of work. "Name-based
sweep" is the strict reading -- `sed -i -E '/mysql|mariadb/Id' <file>` is a
name-based sweep, and it satisfies every *absence* question this module can
ask, including all nineteen `wiring/gone/` ones.

    inventory       guard   25 files gone, 34 files still here
    server          guard   the two directories that travel as artifacts
    wiring-gone     guard   every literal that selects a MySQL back-end is
                            gone from the files the inventory keeps
    wiring          scored  what is left still resolves: the fallback names a
                            driver the tree implements, the harness imports
                            tests that exist, the reserved-words default lists
                            back-ends the registry still has
    wiring-intact   guard   the tree still reads, nothing was invented,
                            orphaned, or left pointing at a deleted class
    provisioning    guard   vagrant_bootstrap.sh
"""

import errno
import json
import os
import re
import shlex
import stat

from . import deploy
from . import gate

HERE = os.path.dirname(os.path.abspath(__file__))

WIRING_FIXTURE = "/tests/fixtures/wiring-step05.json"
INVENTORY_FIXTURE = "/tests/fixtures/inventory-step05.json"

MAX_SHOWN = 8

# A full pass over this tree is 905 files and takes about 4s for either
# instrument on the verifier image; 600 was a number with no measurement under
# it, and a scanner that has not answered in four minutes is wedged, not slow.
# The cost of being wrong here is one failed family, not a hung scoring run.
SCAN_TIMEOUT = 240

# As in mysqlgone: a tree with almost no PHP in it answers "gone" to
# everything. Trap 5 -- an empty /app is silent, not clean.
MIN_FILE_FRACTION = 0.5


# ------------------------------------------------------------- classmap ---

def scan(roots, tools=None):
    """Run classmap.php over `roots`, returning its JSON unchanged.

    Raises RuntimeError if the scanner could not run; the caller turns that
    into failed checks, never into a skip.
    """
    tools = tools or HERE
    p = deploy.run(
        deploy.as_user(deploy.NOBODY,
                       ["php", os.path.join(tools, "classmap.php")]
                       + list(roots)),
        timeout=SCAN_TIMEOUT)
    if p.returncode != 0:
        raise RuntimeError("classmap.php failed (rc=%d): %s"
                           % (p.returncode, deploy.diagnostics(p)))
    return json.loads(p.stdout.decode("utf-8", "replace"))


def lit_key(l):
    """What identifies a literal across a move: its text and its position.

    Not its path. Relocating `'pdo_mysql' => …` into another file is not
    removing it, and a path-keyed check would score that a pass twice over.
    """
    return (l["text"].lower(), l["context"])


def lit_counts(literals):
    out = {}
    for l in literals:
        k = lit_key(l)
        out[k] = out.get(k, 0) + 1
    return out


def lit_where(literals, k):
    return ["%s:%s" % (l["file"], l["line"])
            for l in literals if lit_key(l) == k]


def _slug(text):
    keep = "abcdefghijklmnopqrstuvwxyz0123456789"
    out = "".join(c if c in keep else "-" for c in text.lower())
    while "--" in out:
        out = out.replace("--", "-")
    return out.strip("-") or "x"


# ---------------------------------------------------------- the fixtures ---

def load(path):
    with open(path, "rb") as fh:
        return json.loads(fh.read().decode("utf-8"))


def record_wiring(roots, drop=(), tools=None):
    """AUTHORING: the wiring baseline.

    `drop` is the inventory's file list, relative to the root: those files are
    the free subset (§24.4) and are excluded from the denominator, so the
    nineteen that remain are the ones that need an edit rather than a
    deletion.
    """
    data = scan(roots, tools)
    roots = [os.path.abspath(r).rstrip("/") for r in roots]
    drop = set(drop)

    def rel(path):
        for r in roots:
            if path == r or path.startswith(r + "/"):
                return path[len(r) + 1:]
        return path

    kept = [l for l in data["literals"] if rel(l["file"]) not in drop]
    counts = lit_counts(kept)
    declared = sorted(set(d["fqn"] for d in data["declared"]))
    return {"files": data["files"],
            "unparsable": data["unparsable"],
            "literals": sorted(({"text": l["text"], "context": l["context"],
                                 "file": rel(l["file"]), "line": l["line"]}
                                for l in kept),
                               key=lambda l: (l["file"], l["line"])),
            "counts": sorted([list(k) + [n] for k, n in counts.items()]),
            # Every key the shipped tree holds *with how many of it*, dropped
            # files included. Only `counts` is scored; this is the ceiling
            # `no-new-literals` measures against, so that a literal the agent
            # has not got to yet reads as unfinished work in the `gone/`
            # checks and in the inventory guard, and not additionally as an
            # invention. A key in neither list is a MySQL name this codebase
            # never had.
            #
            # The count is the point. As a bare set of keys this said "some
            # file in the baseline tree mentioned `pdo_mysql`", which a tree
            # that had grown forty new `pdo_mysql` call sites also satisfies.
            "vocabulary": sorted([list(k) + [n] for k, n in
                                  lit_counts(data["literals"]).items()]),
            "declared": declared}


# ------------------------------------------------------------ wiringmap ---

def wiringscan(roots, tools=None):
    """Run wiringmap.php over `roots`, returning its JSON unchanged.

    Same contract as `scan`: a scanner that could not run raises, and the
    caller turns that into failed checks rather than into a skip.
    """
    tools = tools or HERE
    p = deploy.run(
        deploy.as_user(deploy.NOBODY,
                       ["php", os.path.join(tools, "wiringmap.php")]
                       + list(roots)),
        timeout=SCAN_TIMEOUT)
    if p.returncode != 0:
        raise RuntimeError("wiringmap.php failed (rc=%d): %s"
                           % (p.returncode, deploy.diagnostics(p)))
    return json.loads(p.stdout.decode("utf-8", "replace"))


def module_file(root, module):
    """The file `jf::import("<module>")` would load, or None.

    `_japp/model/j.php:44` -- a module path longer than three characters and
    starting with `jf/` is under `_japp/`, anything else under `app/`, and
    `.php` is appended. The lookup is exact and the filesystem is
    case-sensitive, so `jf/test/lib/db/adapter/mariaDB` does not find
    `mariadb.php`. That is a real defect in the tree this step starts from and
    it is the reason these checks resolve rather than match text: no amount of
    deleting MySQL lines fixes a path that points at nothing.

    Returns None for a module that would escape the tree.
    """
    if not module or not isinstance(module, str):
        return None
    rel = ("_japp/" + module[3:] if len(module) > 3 and module[:3] == "jf/"
           else "app/" + module)
    root = os.path.abspath(root).rstrip("/")
    path = os.path.normpath(os.path.join(root, rel + ".php"))
    if path != root and not path.startswith(root + os.sep):
        return None
    return path


def _keyed(array):
    """True if every item of a reported array literal has a string key."""
    return bool(array["items"]) and all(k is not None for k, _ in
                                        (tuple(i) for i in array["items"]))


def _listed(array):
    """True if a reported array is a plain list of string values."""
    return bool(array["items"]) and all(
        k is None and v is not None for k, v in
        (tuple(i) for i in array["items"]))


def _prefixes(arms):
    """The directories the harness imports its adapter tests from."""
    out = []
    for arm in arms:
        for path in arm["adds"]:
            head = path.rsplit("/", 1)[0] if "/" in path else ""
            if head and head not in out:
                out.append(head)
    return out


def wiring_roles(data, want):
    """Which reported shape is which. Returns (roles, complaints-by-role).

    Identified the way `lit_key` identifies a literal -- by what it is, not by
    where it sits -- so that moving `DriverManager.php` does not hide its
    registry and does not score as having removed it. The harness is the file
    with an adapter switch, the fallback is the guarded assignment to the
    variable the baseline named, and the two registries are the arrays the
    baseline named. Those names come out of `record_wiring_live`; none of them
    is typed in here.

    A role that is not uniquely identifiable is absent, not guessed: every
    check that needs it then fails with the reason, which is the fail-closed
    rule this module applies to its instruments as well.
    """
    roles = {}
    why = {}

    sw = data["switches"]
    if len(sw) == 1:
        roles["harness"] = sw[0]
    else:
        why["harness"] = ("the adapter test harness has no readable switch on"
                          " the configured adapter"
                          if not sw else
                          "%d files hold an adapter switch and only one can be"
                          " the harness: %s"
                          % (len(sw), ", ".join(s["file"]
                                                for s in sw[:MAX_SHOWN])))

    var = want["fallback"]["var"]
    fb = [a for a in data["assigns"] if a["var"] == var and a["guarded"]]
    if len(fb) == 1:
        roles["fallback"] = fb[0]
    else:
        why["fallback"] = ("nothing assigns a default back-end to $%s under a"
                           " test of $%s" % (var, var) if not fb else
                           "%d such assignments to $%s and only one can be the"
                           " default: %s"
                           % (len(fb), var,
                              ", ".join("%s:%d" % (a["file"], a["line"])
                                        for a in fb[:MAX_SHOWN])))

    for role in ("drivermap", "reserved"):
        name = want[role]["name"]
        got = [a for a in data["arrays"]
               if a["name"] == name and _keyed(a)]
        if len(got) == 1:
            roles[role] = got[0]
        else:
            why[role] = ("$%s is not a registry of back-end names any more"
                         % name if not got else
                         "%d arrays named $%s" % (len(got), name))

    if "reserved" in roles:
        lists = [a for a in data["arrays"]
                 if a["file"] == roles["reserved"]["file"] and _listed(a)]
        if len(lists) == 1:
            roles["reserved_default"] = lists[0]
        else:
            why["reserved_default"] = (
                "the reserved-words command has no default list of back-ends"
                " to check" if not lists else
                "%d plain lists of back-end names in %s"
                % (len(lists), roles["reserved"]["file"]))
    else:
        why["reserved_default"] = why["reserved"]
    return roles, why


def record_wiring_live(roots, gone=(), tools=None):
    """AUTHORING: the resolution baseline, from the tree step 05 is handed.

    Everything here is read off that tree and off the inventory's `gone` list.
    Nothing is a constant chosen by the author: which variable carries the
    Doctrine fallback, what the two Doctrine registries are called, which
    adapters the harness selects on and which of those the step retires are
    all derived, and the derivation asserts that each came out unique.
    """
    data = wiringscan(roots, tools)
    if data["unparsable"]:
        raise SelfTestError("the baseline tree does not parse: %r"
                            % data["unparsable"][:MAX_SHOWN])
    root = os.path.abspath(roots[0]).rstrip("/")
    gone = set(gone)

    def rel(path):
        return path[len(root) + 1:] if path.startswith(root + "/") else path

    def only(items, what):
        if len(items) != 1:
            raise SelfTestError("the baseline tree holds %d %s, not one"
                                % (len(items), what))
        return items[0]

    # At record time every shape is still present, so the reserved-words file
    # is the one holding both a registry and a plain list of back-end names,
    # and the driver map is the registry that is not in it. After this the
    # names are what identify them; the structure is not asked for again.
    keyed = [a for a in data["arrays"] if _keyed(a)]
    listed = [a for a in data["arrays"] if _listed(a)]
    default = only(listed, "plain lists of back-end names")
    reserved = only([a for a in keyed if a["file"] == default["file"]],
                    "registries in the file holding that list")
    drivermap = only([a for a in keyed if a["file"] != default["file"]],
                     "registries outside it")
    fallback = only([a for a in data["assigns"] if a["guarded"]],
                    "guarded assignments of a back-end name")
    switch = only(data["switches"], "adapter switches")

    # An adapter the harness selects on survives this step if its test module
    # is a file the tree has and the inventory does not delete. The import
    # directories are the harness's own, so no path is written down here.
    heads = _prefixes(switch["arms"])
    labels = [l for arm in switch["arms"] for l in arm["cases"]]
    keep, retired = [], []
    for label in labels:
        live = False
        for head in heads:
            path = module_file(root, "%s/%s" % (head, label))
            if path and os.path.isfile(path) and rel(path) not in gone:
                live = True
        (keep if live else retired).append(label)
    if not keep:
        raise SelfTestError("every adapter the harness selects on is retired;"
                            " there would be nothing left to check")

    # A keyword list the step retires: one whose class the inventory deletes.
    declared = {}
    for d in scan(roots, tools)["declared"]:
        declared.setdefault(d["fqn"], d["file"])
    dead = []
    for key, cls in (tuple(i) for i in reserved["items"]):
        where = declared.get((cls or "").lower().lstrip("\\"))
        if where is not None and rel(where) in gone:
            dead.append(key)

    return {"harness": {"file": rel(switch["file"]), "line": switch["line"],
                        "keep": keep, "retired": retired},
            "fallback": {"file": rel(fallback["file"]),
                         "line": fallback["line"], "var": fallback["var"],
                         "text": fallback["text"]},
            "drivermap": {"file": rel(drivermap["file"]),
                          "line": drivermap["line"],
                          "name": drivermap["name"]},
            "reserved": {"file": rel(reserved["file"]), "line": reserved["line"],
                         "name": reserved["name"],
                         "default_line": default["line"], "retired": dead}}


# ------------------------------------------------------------ 1. wiring ---

# The six questions this step's work answers and a line filter does not.
# Every one of them is false on the tree step 05 is handed *except*
# `fallback-present`, which is true there and is kept because the failure it
# names -- deleting the Doctrine fallback rather than repointing it -- is a
# thing candidates do, and because it is false under the line filter. §24.15
# records the measured (handed, filtered, golden) signature of each.
_LIVE = ("wiring/live/fallback-living",
         "wiring/live/harness-resolves",
         "wiring/live/harness-arms-live",
         "wiring/live/harness-sqlite-arm",
         "wiring/live/reserved-default-living")

# Resolution questions a *finished* tree answers and a line filter also
# answers, so they guard and do not pay.
_LIVE_INTACT = ("wiring/live/fallback-present",
                "wiring/drivermap-living",
                "wiring/reserved-registry-living",
                "wiring/harness-no-retired-arm")


def wiring_check_ids(want):
    ids = ["wiring/parseable", "wiring/files-scanned", "wiring/no-new-literals",
           "wiring/no-orphan-subclass"]
    ids.extend(_LIVE_INTACT)
    ids.extend(_LIVE)
    for text, context, n in want["counts"]:
        for i in range(1, n + 1):
            ids.append("wiring/gone/%s/%s#%d" % (context, _slug(text), i))
    return ids


def _fqn(text):
    return (text or "").lower().lstrip("\\")


def _resolved(root, module):
    path = module_file(root, module)
    return bool(path) and os.path.isfile(path)


def live_checks(c, root, want, data, declared):
    """The nine resolution checks, five scored and four guarding.

    `data` is `wiringscan`'s report, `declared` the set of class names the tree
    still declares. Every check fails with the reason when the shape it asks
    about cannot be found, because "there is no adapter switch any more" is not
    a pass.
    """
    roles, why = wiring_roles(data, want)
    hs = roles.get("harness")
    fb = roles.get("fallback")
    dm = roles.get("drivermap")
    rs = roles.get("reserved")
    dflt = roles.get("reserved_default")
    arms = hs["arms"] if hs else []

    # -- the Doctrine bootstrap's default adapter ---------------------------
    c.add("wiring/live/fallback-present", fb is not None,
          "" if fb else "%s. A deployment that configures no adapter still has"
          " to reach a database, so removing the default is not a way to stop"
          " it naming MySQL" % why.get("fallback", "it is gone"))

    if fb is None or dm is None:
        ok, detail = False, (why.get("fallback") or why.get("drivermap")
                             or "unreadable")
    else:
        pairs = dict((_fqn(k), v or "") for k, v in
                     (tuple(i) for i in dm["items"]))
        cls = pairs.get(fb["text"].lower())
        ok = cls is not None and _fqn(cls) in declared
        detail = ("" if ok else
                  "the default adapter is %r and %s"
                  % (fb["text"],
                     "$%s does not have it" % dm["name"] if cls is None
                     else "the driver it selects, %s, is not a class this tree"
                          " declares any more" % cls))
    c.add("wiring/live/fallback-living", ok, detail)

    # -- the adapter test harness -------------------------------------------
    adds = [m for arm in arms for m in arm["adds"]]
    missing = [m for m in adds if not _resolved(root, m)]
    ok = bool(hs) and bool(adds) and not missing
    c.add("wiring/live/harness-resolves", ok,
          "" if ok else
          why.get("harness", "") if not hs else
          "the adapter switch imports nothing" if not adds else
          "%d import(s) name a module this tree does not have: %s"
          % (len(missing), ", ".join(sorted(set(missing))[:MAX_SHOWN])))

    keep = set(l.lower() for l in want["harness"]["keep"])
    silent = [a for a in arms if not a["adds"]]
    stray = sorted(set(l for a in arms for l in a["cases"]
                       if l.lower() not in keep))
    computed = sum(a["other_cases"] for a in arms)
    ok = bool(arms) and not silent and not stray and not computed
    c.add("wiring/live/harness-arms-live", ok,
          "" if ok else
          why.get("harness", "") if not hs else
          "the switch has no arms" if not arms else
          "; ".join(filter(None, [
              "%d arm(s) select an adapter and then import nothing (line %s)"
              % (len(silent), ", ".join(str(a["line"]) for a in silent))
              if silent else "",
              "%d case label(s) name an adapter this step does not keep: %s"
              % (len(stray), ", ".join(stray)) if stray else "",
              "%d case label(s) are not plain strings and cannot be read"
              % computed if computed else ""])))

    trouble = []
    for label in want["harness"]["keep"]:
        hit = [a for a in arms
               if label.lower() in [x.lower() for x in a["cases"]]]
        if len(hit) != 1:
            trouble.append("%d arms select %r" % (len(hit), label))
            continue
        own = [m for m in hit[0]["adds"]
               if m.rsplit("/", 1)[-1].lower() == label.lower()]
        if not own:
            trouble.append("the %r arm imports %s, not that adapter's test"
                           % (label, ", ".join(hit[0]["adds"]) or "nothing"))
        elif not any(_resolved(root, m) for m in own):
            trouble.append("the %r arm imports %s and no such module exists"
                           % (label, ", ".join(own)))
    ok = bool(hs) and not trouble
    c.add("wiring/live/harness-sqlite-arm", ok,
          "" if ok else why.get("harness", "") if not hs
          else "; ".join(trouble[:MAX_SHOWN]))

    # -- the reserved-words command -----------------------------------------
    keys = set(_fqn(k) for k, _ in (tuple(i) for i in rs["items"])) if rs \
        else set()
    dead = set(t.lower() for t in want["reserved"]["retired"])
    entries = [v for _, v in (tuple(i) for i in dflt["items"])] if dflt else []
    unknown = [e for e in entries if _fqn(e) not in keys]
    left = [e for e in entries if _fqn(e) in dead]
    ok = bool(rs) and bool(entries) and not unknown and not left
    c.add("wiring/live/reserved-default-living", ok,
          "" if ok else
          why.get("reserved_default", "") if not dflt or not rs else
          "; ".join(filter(None, [
              "the default list still asks for %s, whose keyword class this"
              " step removes" % ", ".join(sorted(set(left))) if left else "",
              "the default list names %s, which $%s does not have"
              % (", ".join(sorted(set(unknown))), rs["name"])
              if unknown else ""])))

    # -- guards --------------------------------------------------------------
    for cid, role, label in (
            ("wiring/drivermap-living", "drivermap", "driver"),
            ("wiring/reserved-registry-living", "reserved", "keyword list")):
        got = roles.get(role)
        broken = ([v for _, v in (tuple(i) for i in got["items"])
                   if _fqn(v) not in declared] if got else [])
        ok = bool(got) and not broken
        c.add(cid, ok,
              "" if ok else why.get(role, "") if not got else
              "$%s still advertises %d %s(s) whose class this tree no longer"
              " declares: %s" % (got["name"], len(broken), label,
                                 ", ".join(sorted(set(broken))[:MAX_SHOWN])))

    gone_labels = set(t.lower() for t in want["harness"]["retired"])
    still = sorted(set(l for a in arms for l in a["cases"]
                       if l.lower() in gone_labels))
    ok = bool(hs) and not still
    c.add("wiring/harness-no-retired-arm", ok,
          "" if ok else why.get("harness", "") if not hs else
          "the switch still selects on %s" % ", ".join(still))


_INSTRUMENT = {}


def instrument_why(tools=None):
    """None when both PHP scanners measure what they claim, else why not.

    Memoized, because it costs two PHP processes and the answer cannot change
    within one scoring run. A family whose instrument is broken fails closed:
    reporting "no MySQL literal found" from a scanner that cannot find one is
    the failure this guards against, and it is indistinguishable from success
    unless the scanner is asked a question with a known answer first. The same
    argument covers `wiringmap.php`, where the indistinguishable-from-success
    answer is "no adapter switch here", so one verdict covers both and a
    failure of either fails every check in every wiring family.
    """
    if "why" not in _INSTRUMENT:
        try:
            selftest(tools)
        except Exception as e:
            _INSTRUMENT["why"] = ("a scorer instrument failed its own selftest"
                                  " (%s) so nothing it reported about this"
                                  " tree was believed"
                                  % " ".join(str(e).split())[:300])
        else:
            _INSTRUMENT["why"] = None
    return _INSTRUMENT["why"]


def wiring_family(roots=("/app",), fixture=WIRING_FIXTURE, tools=None):
    """Scored. Every literal that still selects a MySQL back-end."""
    c = gate.Checks("migrated:wiring")
    try:
        want = load(fixture) if isinstance(fixture, str) else fixture
    except (OSError, ValueError) as e:
        return gate.crashed("migrated:wiring",
                            "no recorded wiring baseline: %s" % e, 4)

    ids = wiring_check_ids(want)
    why = instrument_why(tools)
    if why:
        c.fail_all(ids, why)
        return c
    try:
        data = scan(roots, tools)
    except Exception as e:
        return gate.crashed("migrated:wiring",
                            "cannot scan the tree: %s" % e, len(ids))

    broke = ("%d file(s) the PHP parser could not read, so no claim that a"
             " MySQL name is gone from this tree can be believed: %s"
             % (len(data["unparsable"]),
                ", ".join(u["file"] for u in data["unparsable"][:MAX_SHOWN]))
             if data["unparsable"] else "")
    c.add("wiring/parseable", not broke, broke)
    if broke:
        # A file the scanner cannot read is a file whose `pdo_mysql` it cannot
        # see, and "no literal found" from a scanner that found no file is the
        # same sentence as "clean". §24.9's fail-closed rule applies to the
        # tree, not only to the instrument.
        c.fail_all([i for i in ids if i.startswith("wiring/gone/")], broke)

    floor = int(want["files"] * MIN_FILE_FRACTION)
    enough = data["files"] >= floor
    short = ("scanned %d .php files; the tree this step starts from has %d,"
             " so anything under %d is not this application"
             % (data["files"], want["files"], floor))
    c.add("wiring/files-scanned", enough, "" if enough else short)
    if not enough:
        c.fail_all([i for i in ids
                    if i not in ("wiring/parseable", "wiring/files-scanned")],
                   short)
        return c

    # The second instrument. `classmap.php` has now said what is absent;
    # `wiringmap.php` says whether what is left resolves. Separate pass,
    # separate failure: if it cannot run, the nine resolution checks fail and
    # the nineteen absence ones -- which do not depend on it -- still stand.
    if not want.get("live"):
        c.fail_all(list(_LIVE) + list(_LIVE_INTACT),
                   "no recorded resolution baseline in the wiring fixture")
    else:
        try:
            wiring = wiringscan(roots, tools)
        except Exception as e:
            c.fail_all(list(_LIVE) + list(_LIVE_INTACT),
                       "cannot read the tree's wiring: %s"
                       % " ".join(str(e).split())[:300])
        else:
            if wiring["unparsable"]:
                c.fail_all(list(_LIVE) + list(_LIVE_INTACT),
                           "%d file(s) the PHP parser could not read: %s"
                           % (len(wiring["unparsable"]),
                              ", ".join(u["file"] for u
                                        in wiring["unparsable"][:MAX_SHOWN])))
            else:
                live_checks(c, roots[0], want["live"], wiring,
                            set(d["fqn"] for d in data["declared"]))

    have = lit_counts(data["literals"])
    baseline = {}
    for text, context, n in want["counts"]:
        baseline[(text.lower(), context)] = n

    for k in sorted(baseline):
        left = have.get(k, 0)
        for i in range(1, baseline[k] + 1):
            cid = "wiring/gone/%s/%s#%d" % (k[1], _slug(k[0]), i)
            if broke:
                continue
            gone = i > left
            c.add(cid, gone,
                  "" if gone else "%r still selects a MySQL back-end (%s) at %s"
                  % (k[0], k[1],
                     ", ".join(lit_where(data["literals"], k)[:MAX_SHOWN])))

    # A MySQL back-end name this codebase never held, anywhere. The keys the
    # baseline excludes -- the thirteen inside the inventory's dropped files --
    # are in `vocabulary` and so are not reported here: leaving one of those in
    # place is unfinished work, which the `gone/` checks and the inventory
    # guard already say, and calling it an invention as well would charge one
    # omission twice. What survives the filter is a name from nowhere.
    ceiling = {}
    for entry in want.get("vocabulary", []):
        text, context, n = entry[0], entry[1], (entry[2] if len(entry) > 2
                                                else 0)
        ceiling[(text.lower(), context)] = max(n, baseline.get(
            (text.lower(), context), 0))
    over = sorted(k for k in have
                  if have[k] > max(ceiling.get(k, 0), baseline.get(k, 0)))
    c.add("wiring/no-new-literals", not over,
          "" if not over else "%d literal(s) beyond the baseline: %s"
          % (sum(have[k] - max(ceiling.get(k, 0), baseline.get(k, 0))
                 for k in over),
             "; ".join("%r (%s) at %s"
                       % (k[0], k[1], lit_where(data["literals"], k)[0])
                       for k in over[:MAX_SHOWN])))

    # A class that survives its parent. Only parents the *baseline tree*
    # declared are considered, so `implements ArrayAccess` and
    # `extends PHPUnit_Framework_TestCase` -- measured over the golden tree,
    # 106 edges naming 24 distinct parents this tree never declared, none of
    # them a defect -- are not findings: what is being asked is whether this
    # step removed a class and left something inheriting it.
    was_declared = set(want["declared"])
    now = set(d["fqn"] for d in data["declared"])
    orphans = [i for i in data["inherits"]
               if i["parent"] in was_declared and i["parent"] not in now]
    c.add("wiring/no-orphan-subclass", not orphans,
          "" if not orphans else "%d class(es) %s a class this tree no longer"
          " declares: %s"
          % (len(orphans), orphans[0]["rel"],
             "; ".join("%s %s %s (%s:%d)"
                       % (o["child"], o["rel"], o["parent"],
                          o["file"], o["line"])
                       for o in orphans[:MAX_SHOWN])))

    c.note("%d .php files, %d watched literal(s) remaining"
           % (data["files"], len(data["literals"])))
    return c


# --------------------------------------------------------- 2. inventory ---

# The back-ends this step retires, and the parts of the tree that implement a
# back-end. A file is deleted by this step when its own path names one of the
# first and it lives under one of the second: `Driver/Mysqli/Driver.php` is an
# implementation, `script/ace/.../mode-mysql.js` is the SQL editor's syntax
# highlighting and stays, and no rule that looks only at the name tells them
# apart.
RETIRED_NAMES = ("mysql", "mysqli", "mariadb", "drizzle")
DB_LAYER = ("_japp/model/lib/db/", "_japp/test/lib/db/", "app/plugin/doctrine/",
            "install/_db/")

INVENTORY_RULE = "path-named under the db layer, v3"

# Wave R, 2026-10-20 (trajectory review g03 M-1). The instruction keeps
# Doctrine's other drivers and platforms "except a vendored file that holds SQL
# PostgreSQL 17 would not run, which is ported or not kept whichever server it
# is for" (05:39-43, 05:116-123, 06:115-122, and 04:80-84 before them). v2
# required every such file to be present, so a candidate who took that clause
# at its word -- SqlitePlatform's `PRAGMA`, SQLServerPlatform's `sp_RENAME`,
# OraclePlatform's PL/SQL trigger -- lost a guard check per file, while the
# reference kept them because `migrated:dialect` (which scores only statements
# it can see reach a driver) never flags them. `licensed` is the subset of
# `keep` that clause reaches: the presence check on those files passes whether
# the file was ported in place or not kept. Two kinds, and nothing else:
#
#   * Doctrine's per-server SQL layer for a server other than PostgreSQL: the
#     platform classes (which write that server's SQL), the schema managers
#     (which send it that server's catalogue queries) and the session-init
#     listener (which sends it a session statement). The SQL Server 2005/2008
#     and Azure platforms are SQL Server's platform and subclass it, so the
#     rule that no class outlives its parent sends them wherever it goes.
#   * A driver file for such a server whose own text holds a statement -- a
#     quoted string that opens with a DML verb (OCI8Connection's
#     `SELECT ... FROM DUAL`, SQLSrvStatement's `SELECT SCOPE_IDENTITY()`).
#
# Every other kept file still has to be there: exception classes, keyword
# lists, a driver's entry class and everything generic hold no SQL, so the
# clause does not reach them, and `keep` itself is unchanged from v2.
_SQL_LAYER = re.compile(
    r"^app/plugin/doctrine/Doctrine/DBAL/"
    r"(?:Platforms/(\w+)Platform|Schema/(\w+)SchemaManager"
    r"|Event/Listeners/(\w+)SessionInit)\.php$")
# Not another server's: the abstract bases, PostgreSQL's own, and the generic
# listener that sends whatever statement it is configured with.
_SQL_LAYER_OWN = ("Abstract", "PostgreSql", "SQL")
_DRIVER_FILE = re.compile(r"^app/plugin/doctrine/Doctrine/DBAL/Driver/(\w+)/[^/]+\.php$")
_DRIVER_OWN = ("PDOPgSql",)
_HOLDS_STATEMENT = re.compile(
    br"""['"];?\s*(?:SELECT|INSERT|UPDATE|DELETE)\s""", re.I)


def _licensed(root, rel):
    """True when the instruction's "ported or not kept" clause reaches `rel`."""
    m = _SQL_LAYER.match(rel)
    if m:
        return next(g for g in m.groups() if g) not in _SQL_LAYER_OWN
    m = _DRIVER_FILE.match(rel)
    if m and m.group(1) not in _DRIVER_OWN:
        try:
            with open(os.path.join(root, rel), "rb") as fh:
                return bool(_HOLDS_STATEMENT.search(fh.read()))
        except OSError:
            return False
    return False

# Files this scanner will not read looking for a mention: a binary blob's bytes
# are not prose, and `phpunit.phar` happens to contain the word.
_TEXT_SNIFF = 4096


def _mentions(path):
    try:
        with open(path, "rb") as fh:
            body = fh.read()
    except OSError:
        return False
    if b"\0" in body[:_TEXT_SNIFF]:
        return False
    low = body.lower()
    return any(n.encode("ascii") in low for n in RETIRED_NAMES)


def _named(rel):
    low = rel.lower()
    return any(n in low for n in RETIRED_NAMES)


def record_inventory(roots, previous=None):
    """AUTHORING: classify every file in the handed tree. Nothing is listed
    by hand.

    Three limbs, and a file is in exactly one of them:

    `gone`   its path names a retired back-end and it sits in the database
             layer -- the adapter, the driver, the platform, the keyword list,
             the seed data.
    `keep`   it names a retired back-end somewhere *other* than the database
             layer (the SQL editor's highlighting modes); or its bytes mention
             one (the twelve Doctrine files that discuss MySQL in prose, the
             README, the Vagrantfile); or it is a neighbour of a deletion --
             in the same directory, its parent, or a sibling directory. The
             last limb is the one that matters for rule 4.3: `find | grep -i
             mysql | xargs rm -rf` takes `Driver/Mysqli/` with it, and what
             tells a retirement from a demolition is whether `Driver/OCI8/` is
             still there afterwards.
    neither  the other ~1350 files, which this step has no opinion about.

    `previous`, when given, is the fixture this replaces: every path it lists
    must come back out of the classification. A hand-added entry that the rule
    does not produce is refused rather than carried, because a curated list is
    a list nothing checks.
    """
    root = os.path.abspath(roots[0]).rstrip("/")
    files = []
    for base, _, names in os.walk(root):
        for n in names:
            files.append(os.path.relpath(os.path.join(base, n), root))
    files.sort()

    gone = [r for r in files if _named(r) and r.startswith(DB_LAYER)]
    dirs = set(os.path.dirname(g) for g in gone)
    hood = set(dirs)
    parents = set(os.path.dirname(d) for d in dirs)
    hood |= parents
    for r in files:
        d = os.path.dirname(r)
        if os.path.dirname(d) in parents:
            hood.add(d)

    keep = set(r for r in files if _named(r) and r not in gone)
    keep |= set(r for r in files
                if not _named(r) and _mentions(os.path.join(root, r)))
    keep |= set(r for r in files
                if r not in gone and os.path.dirname(r) in hood)

    out = {"rule": INVENTORY_RULE, "gone": sorted(gone), "keep": sorted(keep),
           "licensed": sorted(r for r in keep if _licensed(root, r))}
    both = set(out["gone"]) & set(out["keep"])
    if both:
        raise SelfTestError("%d path(s) classified both ways: %s"
                            % (len(both), ", ".join(sorted(both)[:MAX_SHOWN])))
    if previous:
        lost = sorted((set(previous.get("gone", [])) - set(out["gone"]))
                      | (set(previous.get("keep", [])) - set(out["keep"])))
        if lost:
            raise SelfTestError(
                "%d path(s) in the fixture this replaces are not produced by"
                " the rule and will not be carried by hand: %s"
                % (len(lost), ", ".join(lost[:MAX_SHOWN])))
    return out


def inventory_family(roots=("/app",), fixture=INVENTORY_FIXTURE):
    """Guard. The files that must be gone, and the ones that must not."""
    c = gate.Checks("migrated:inventory")
    try:
        want = load(fixture) if isinstance(fixture, str) else fixture
    except (OSError, ValueError) as e:
        return gate.crashed("migrated:inventory",
                            "no recorded inventory: %s" % e, 2)
    if want.get("rule") != INVENTORY_RULE:
        # A fixture without the recorder's stamp was edited by hand, and a
        # hand-edited inventory is one whose `keep` list can be shortened to
        # whatever the tree happens to hold.
        return gate.crashed("migrated:inventory",
                            "the recorded inventory is not %r, so it was not"
                            " produced by record_inventory" % INVENTORY_RULE, 2)
    root = roots[0]
    licensed = set(want.get("licensed", ()))
    for rel in want["gone"]:
        path = os.path.join(root, rel)
        # A directory left behind with the file removed is removed; an empty
        # `Driver/PDOMySql/` is what step 04's golden already left.
        c.add("inventory/gone/%s" % rel, not os.path.isfile(path),
              "" if not os.path.isfile(path) else "%s is still here" % rel)
    dropped = 0
    for rel in want["keep"]:
        path = os.path.join(root, rel)
        here = os.path.isfile(path)
        if rel in licensed:
            # Holds another server's SQL: ported in place or not kept, and
            # either answer passes (see `_licensed`).
            dropped += not here
            c.add("inventory/keep/%s" % rel, True)
            continue
        c.add("inventory/keep/%s" % rel, here,
              "" if here
              else "%s is not a MySQL file and is missing from the"
                   " delivered tree" % rel)
    if dropped:
        c.note("%d file(s) holding another server's SQL not kept, which the"
               " instruction allows" % dropped)
    return c


# ------------------------------------------------------------ 3. server ---

MYSQL_DIRS = ("/var/lib/mysql", "/etc/mysql")
PG_DIR = "/var/lib/postgresql"


def _nonempty(path):
    try:
        return bool(os.listdir(path))
    except OSError:
        return False


def server_family(dirs=MYSQL_DIRS):
    """Guard. The two directories the old server owns that travel here.

    Both checks and nothing else, so that a tree on which no retirement has
    happened scores this family at exactly 0 and the guard product -- and with
    it the whole reward -- is exactly 0. Rule 6 wants the nop at 0.000, and
    for this step the nop is step 04's *finished* tree, which passes the two
    inherited behaviour families for free; without a guard that bottoms out,
    that free 0.34 survives everything else multiplying against it.

    §24.5: an empty directory here means either "retired" or "the artifact
    round trip broke", and the manifest that tells them apart is written on
    the host. `postgres_family` is what stops a wholesale artifact failure
    from reading as a clean retirement; it does not close the hole for a
    failure that hits only one source, and nothing here claims it does.
    """
    c = gate.Checks("migrated:server-retired")
    for d in dirs:
        live = _nonempty(d)
        c.add("server/retired%s" % d.replace("/", "-"), not live,
              "" if not live else "%s still holds %d entr(y/ies)"
              % (d, len(os.listdir(d))))
    return c


def postgres_family(pgdir=PG_DIR):
    """Guard. The cluster this deployment actually runs on is still there.

    Separate from `server_family` on §24.9's rule -- a do-nothing run passes
    this, so it earns nothing -- and separate from it on meaning too: that
    family asks whether the retirement happened, this one asks whether the
    retirement took the live database with it. One check, so it is 0 or 1, and
    0 is the right answer for a tree that deleted `/var/lib/postgresql`.
    """
    c = gate.Checks("migrated:postgres-intact")
    ok = _nonempty(pgdir)
    c.add("server/round-trip", ok,
          "" if ok else "%s arrived empty, so either this tree deleted the"
          " live cluster or the artifact round trip did not happen; either"
          " way the retirement checks prove nothing" % pgdir)
    return c


# ------------------------------------------------------ 4. provisioning ---

BOOTSTRAP = "vagrant_bootstrap.sh"

# Read as shell *words* with comments stripped, then split on the characters
# that separate a package name from what surrounds it in a debconf selection
# (`mysql-server/root_password`) or an assignment (`mysql_password=om1234`).
# Line 23's commented-out `mysqladmin` is therefore not a finding and line
# 22's argument is. §24.6 on why this is a word-level read and not a shell
# parse.
_SPLIT = " \t\n/=:;|&()<>\"'`$*"

# (label, exact words, word prefixes). MariaDB ships a new server package for
# every release -- 10.3, 10.6, 10.11, 11.4 -- and an exact list of the two that
# existed when this was written is a list that goes stale into a pass. The
# prefix covers the series; nothing outside `mariadb-server*` and
# `mariadb-client*` is a name a working PostgreSQL deployment needs.
FORBIDDEN = [
    ("mysql-server", ("default-mysql-server",), ("mysql-server",)),
    ("mariadb-server", (), ("mariadb-server",)),
    ("mysql-client", ("default-mysql-client",),
     ("mysql-client", "mariadb-client")),
    ("php-mysql", ("php5-mysql", "php-mysql", "php5-mysqli", "php-mysqli",
                   "php7.4-mysql", "php5-mysqlnd", "php-mysqlnd"), ()),
    ("phpmyadmin", ("phpmyadmin", "php-myadmin"), ()),
    ("mysqld", ("mysqld", "mysqladmin", "mysql_secure_installation"), ()),
    ("mysql-debconf", ("root_password", "root_password_again"), ()),
]

# The `has-*` half. These say the recipe still produces a deployment that runs,
# so the spellings have to cover the PHP and Apache packaging a candidate might
# reasonably move to while doing it: refusing `php8.3-cli` because the list
# stopped at 8.1 would fail a recipe that is more current than this file.
REQUIRED = [
    ("postgresql-server", ("postgresql", "postgresql-17", "postgresql-16",
                           "postgresql-15", "postgresql-server")),
    ("php-pgsql", ("php5-pgsql", "php-pgsql", "php7.4-pgsql", "php8.1-pgsql",
                   "php8.2-pgsql", "php8.3-pgsql")),
    ("apache2", ("apache2", "apache2-bin")),
    ("php", ("php5-cli", "php-cli", "libapache2-mod-php5",
             "libapache2-mod-php", "php5-cgi", "php7.4-cli", "php8.1-cli",
             "php8.2-cli", "php8.3-cli", "libapache2-mod-php7.4",
             "libapache2-mod-php8.1", "libapache2-mod-php8.2",
             "libapache2-mod-php8.3")),
]

# The same two `has-*` labels, by package-naming pattern rather than by a list
# of the versions that existed when the list was written: `postgresql-9.3` is
# what this recipe's own php5-era Ubuntu ships, `php8.4-pgsql` what a current
# one does, and both install exactly what the instruction asks for. (Round-1
# migration review, 2026-10-05: the exact lists failed both.)
REQUIRED_PATTERNS = {
    "postgresql-server": re.compile(r"^postgresql(?:-\d+(?:\.\d+)?)?$"),
    "php-pgsql": re.compile(r"^php(?:\d+(?:\.\d+)?)?-pgsql$"),
    # Round-2 review, 2026-10-05: the same class of defect in the PHP label,
    # which round 1 left exact -- `php`, `php8.4`, `php8.4-cli` and
    # `libapache2-mod-php8.4` each install PHP and each failed `has-php`.
    "php": re.compile(r"^(?:php(?:\d+(?:\.\d+)?)?(?:-cli|-cgi|-fpm)?"
                      r"|libapache2-mod-php(?:\d+(?:\.\d+)?)?)$"),
}

# And the mirror for the one forbidden label that is a family of versioned
# names: `php7.0-mysql` and `php8.3-mysql` are MySQL's PHP client exactly as
# much as the seven spellings listed above, and passed `no-php-mysql`.
FORBIDDEN_PATTERNS = {
    "php-mysql": re.compile(r"^php(?:\d+(?:\.\d+)?)?-mysql(?:i|nd)?$"),
}

# `vagrant_bootstrap.sh` is read out of the candidate's tree, so its size and
# its type are the candidate's to choose. 1 MiB is forty times the real file.
BOOTSTRAP_MAX = 1 << 20


def read_recipe(path, limit=BOOTSTRAP_MAX):
    """The provisioning recipe's text, from a file that is a file.

    This is the one read this module makes inside the candidate's tree with no
    scanner between it and the bytes, and `open().read()` on a path somebody
    else controls is a blocking read of unbounded length: a named pipe hangs
    the scoring run and a sparse file exhausts it. Opened non-blocking, refused
    unless it is a regular file, and truncated at `limit`, which is forty times
    the size of the real one. Every failure here raises OSError, which the
    caller has already committed to scoring as "no recipe".
    """
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode):
            raise OSError(errno.EINVAL, "not a regular file", path)
        if st.st_size > limit:
            raise OSError(errno.EFBIG, "%d bytes, over the %d-byte cap"
                          % (st.st_size, limit), path)
        body = b""
        while len(body) <= limit:
            chunk = os.read(fd, 65536)
            if not chunk:
                break
            body += chunk
    finally:
        os.close(fd)
    return body[:limit].decode("utf-8", "replace")


def words(text):
    """Every shell word in the script, comments stripped, sub-split."""
    lex = shlex.shlex(text, posix=True)
    lex.whitespace_split = True
    lex.commenters = "#"
    out = set()
    try:
        for tok in lex:
            for piece in _resplit(tok):
                if piece:
                    out.add(piece.lower())
    except ValueError:
        # An unbalanced quote. Fall back to a plain split of the
        # comment-stripped text rather than reporting nothing: a script this
        # scanner cannot tokenize must not become a script with no packages
        # in it.
        stripped = "\n".join(l.split("#", 1)[0] for l in text.splitlines())
        for tok in stripped.split():
            for piece in _resplit(tok):
                if piece:
                    out.add(piece.lower())
    return out


def _resplit(tok):
    out = [tok]
    for ch in _SPLIT:
        nxt = []
        for part in out:
            nxt.extend(part.split(ch))
        out = nxt
    return out


def provisioning_family(root="/app", name=BOOTSTRAP):
    """Guard. The machine recipe no longer installs the old server.

    A guard and not a scored family, and that is a measurement rather than a
    preference: this was worth 0.24 until a fifty-line sed pass took all of it
    (NOTES §24.13). The whole requirement is one substitution table --
    `mysql-server` becomes `postgresql`, `php5-mysql` becomes `php5-pgsql` --
    plus dropping the lines that still say MySQL, and a substitution table is
    exactly what an automated migrator ships. It must still be true, so it
    guards; doing it is not evidence of anything, so it earns nothing.

    That applies to the `has-*` checks too, even though a do-nothing run fails
    them. §24.9's rule -- a check a do-nothing run passes cannot be scored --
    is a floor, not the whole test; rule 4.3's mechanical pass is the stricter
    one, and these do not survive it either.
    """
    c = gate.Checks("migrated:provisioning")
    path = os.path.join(root, name)
    ids = (["provision/present"]
           + ["provision/no-%s" % n for n, _, _ in FORBIDDEN]
           + ["provision/has-%s" % n for n, _ in REQUIRED])
    try:
        text = read_recipe(path)
    except OSError as e:
        # Deleting the recipe is not a way to retire the server: the
        # deployment still has to be installable afterwards. A FIFO or a
        # gigabyte of zeroes in its place is not a way either, and
        # `read_recipe` turns both into this same failure rather than into a
        # scorer that never returns.
        c.add("provision/present", False, "%s: %s" % (name, e))
        c.fail_all([i for i in ids if i != "provision/present"],
                   "%s is not a readable recipe (%s)" % (name, e))
        return c
    c.add("provision/present", bool(text.strip()),
          "" if text.strip() else "%s is empty" % name)

    seen = words(text)
    for label, names, prefixes in FORBIDDEN:
        fpat = FORBIDDEN_PATTERNS.get(label)
        hit = sorted(set([n for n in names if n in seen]
                         + [w for w in seen
                            if any(w.startswith(pre) for pre in prefixes)]
                         + ([w for w in seen if fpat.match(w)]
                            if fpat else [])))
        c.add("provision/no-%s" % label, not hit,
              "" if not hit else "%s still names %s" % (name, ", ".join(hit)))
    for label, names in REQUIRED:
        pattern = REQUIRED_PATTERNS.get(label)
        hit = sorted(set([n for n in names if n in seen]
                         + ([w for w in seen if pattern.match(w)]
                            if pattern else [])))
        c.add("provision/has-%s" % label, bool(hit),
              "" if hit else "%s installs no %s; the recipe has to still"
              " produce a working deployment" % (name, label))
    return c


# --------------------------------------------------------------- selftest ---

class SelfTestError(Exception):
    pass


# What `selftest/classmap_dirty.php` claims about itself, read out of its own
# text: one function per context, named for the context it must produce.
_DIRTY_LITERALS = {
    "arraykey_driver_table": ("pdo_mysql", "array-key"),
    "arrayvalue_driver_list": ("mysqli", "array-value"),
    "arg_import_path": ("jf/model/lib/db/adapter/mariadb", "arg"),
    "return_driver_name": ("mysql", "return"),
    "other_assignment": ("drizzle_pdo_mysql", "other"),
}

# The two inheritance edges, one written plainly and one through a `use` alias,
# both of which must arrive fully qualified and lowercased.
_DIRTY_EDGES = {
    ("jf\\selftest\\subclass", "jf\\selftest\\db_pdo_mysql"),
    ("jf\\selftest\\aliasedsubclass",
     "doctrine\\dbal\\driver\\pdomysql\\driver"),
}


# What `selftest/wiringmap_dirty.php` claims about itself, restated. The
# second arm is the shape a line filter leaves: case labels with no body at
# all, which has to be reported as an arm that imports nothing rather than
# dropped for having no statements.
_WIRINGMAP_ARMS = [(["mysqli", "pdo_mysql"],
                    ["jf/test/lib/db/adapter/pdo_mysql"]),
                   (["pdo_sqlite"], [])]

# (variable, literal, guarded). The unguarded one is the control: `guarded` is
# what tells a default apart from an assignment, and a scanner that marked
# everything guarded would still pass an any-assignment test.
_WIRINGMAP_ASSIGNS = set([("plain", "pdo_pgsql", False),
                          ("adapter", "pdo_mysql", True),
                          ("fallback", "mysqli", True)])

# `partialMap` is the control for the other half: one computed value, which
# must arrive as null rather than shorten the array, because a registry that
# reads two entries short reads as a registry somebody cleaned up.
_WIRINGMAP_ARRAYS = set([
    ("driverMap", "property",
     (("pdo_mysql", "Doctrine\\DBAL\\Driver\\PDOMySql\\Driver"),
      ("pdo_pgsql", "Doctrine\\DBAL\\Driver\\PDOPgSql\\Driver"))),
    ("keywordLists", "variable", ((None, "mysql"), (None, "pgsql"))),
    ("partialMap", "variable", (("pdo_mysql", None), ("pdo_pgsql", "kept")))])

# The PostgreSQL shapes `selftest/wiringmap_clean.php` does hold. The clean
# half of this pair is not "reports nothing" -- a scanner that reports nothing
# passes that -- it is "reports these three and nothing that says MySQL, from
# a file that says MySQL in a nowdoc, a docblock, a class name and a comment".
_WIRINGMAP_CLEAN_ARMS = [(["pdo_pgsql"], ["jf/test/lib/db/adapter/pdo_pgsql"])]
_WIRINGMAP_CLEAN_ASSIGNS = set([("adapter", "pdo_pgsql", True)])
_WIRINGMAP_CLEAN_ARRAYS = set([("driverMap", "property")])

_RETIRED_WORDS = ("mysql", "mariadb", "drizzle")


def _wm_texts(data):
    """Every string `wiringmap.php` reported, whatever shape it came from."""
    out = []
    for sw in data["switches"]:
        for arm in sw["arms"]:
            out.extend(arm["cases"])
            out.extend(arm["adds"])
    for a in data["assigns"]:
        out.append(a["var"])
        out.append(a["text"])
    for a in data["arrays"]:
        out.append(a["name"])
        for pair in a["items"]:
            out.extend([x for x in pair if x is not None])
    return out


def _wm_shape(data):
    arms = [(arm["cases"], arm["adds"])
            for sw in data["switches"] for arm in sw["arms"]]
    assigns = set((a["var"], a["text"], a["guarded"]) for a in data["assigns"])
    arrays = set((a["name"], a["kind"],
                  tuple((k, v) for k, v in (tuple(i) for i in a["items"])))
                 for a in data["arrays"])
    return arms, assigns, arrays


def wiringmap_selftest(tools=None):
    """Prove `wiringmap.php` against a known answer before it is believed.

    Raises SelfTestError naming everything that came out wrong.
    """
    tools = tools or HERE
    st = os.path.join(tools, "selftest")
    wrong = []

    clean = wiringscan([os.path.join(st, "wiringmap_clean.php")], tools)
    if clean["unparsable"]:
        raise SelfTestError("the wiring scanner could not read its own clean"
                            " fixture: %r" % clean["unparsable"])
    for text in _wm_texts(clean):
        low = text.lower()
        if any(w in low for w in _RETIRED_WORDS):
            wrong.append("clean fixture: %r came back out of a file where it"
                         " appears only in prose" % text)
    arms, assigns, arrays = _wm_shape(clean)
    if arms != _WIRINGMAP_CLEAN_ARMS:
        wrong.append("clean fixture: its one PostgreSQL arm reads %r" % (arms,))
    if assigns != _WIRINGMAP_CLEAN_ASSIGNS:
        wrong.append("clean fixture: its default adapter reads %r" % (assigns,))
    if set((n, k) for n, k, _ in arrays) != _WIRINGMAP_CLEAN_ARRAYS:
        wrong.append("clean fixture: its driver map reads %r" % (arrays,))

    dirty = wiringscan([os.path.join(st, "wiringmap_dirty.php")], tools)
    if dirty["unparsable"]:
        raise SelfTestError("the wiring scanner could not read its own dirty"
                            " fixture: %r" % dirty["unparsable"])
    arms, assigns, arrays = _wm_shape(dirty)
    if arms != _WIRINGMAP_ARMS:
        wrong.append("dirty fixture: its two arms read %r, not %r"
                     % (arms, _WIRINGMAP_ARMS))
    for want in sorted(_WIRINGMAP_ASSIGNS):
        if want not in assigns:
            wrong.append("dirty fixture: $%s = %r (guarded=%s) was reported as"
                         " %r" % (want[0], want[1], want[2],
                                  sorted(a for a in assigns if a[0] == want[0])
                                  or "nothing"))
    for want in sorted(_WIRINGMAP_ARRAYS, key=lambda a: a[0]):
        if want not in arrays:
            wrong.append("dirty fixture: $%s reads %r, not %r"
                         % (want[0],
                            sorted(a[2] for a in arrays if a[0] == want[0])
                            or "nothing", want[2]))

    if wrong:
        raise SelfTestError("wiringmap.php is not measuring what it claims:"
                            "\n  " + "\n  ".join(wrong))
    return {"clean_arms": len(_WIRINGMAP_CLEAN_ARMS),
            "dirty_arms": len(arms),
            "dirty_assigns": len(assigns),
            "dirty_arrays": len(arrays)}


def selftest(tools=None):
    """Prove `classmap.php` against a known answer before it is believed.

    `selftest/classmap_clean.php` is a PostgreSQL-wired file that says "mysql",
    "mysqli", "pdo_mysql" and "mariadb" in comments, a docblock, a class name,
    a file path and an English sentence, and must produce no watched literal at
    all. That is the whole reason this instrument exists rather than a grep:
    the twelve Doctrine files this step requires *untouched* mention MySQL
    exactly this way, and a scanner that reported them would score a correct
    tree as unfinished.

    `selftest/classmap_dirty.php` is one watched literal per context and two
    inheritance edges, one of them written through a `use` alias so that the
    name resolver is exercised and not merely present.

    Raises SelfTestError naming everything that came out wrong.
    """
    tools = tools or HERE
    st = os.path.join(tools, "selftest")
    wrong = []

    clean = scan([os.path.join(st, "classmap_clean.php")], tools)
    if clean["unparsable"]:
        raise SelfTestError("the scanner could not read its own clean fixture:"
                            " %r" % clean["unparsable"])
    for l in clean["literals"]:
        wrong.append("clean fixture: %r reported as a %s literal at line %d"
                     % (l["text"], l["context"], l["line"]))
    if not any(d["fqn"] == "jf\\selftest\\mysqlireplacement"
               for d in clean["declared"]):
        wrong.append("clean fixture: the class it declares was not seen, so"
                     " the absence of literals above proves nothing")

    dirty = scan([os.path.join(st, "classmap_dirty.php")], tools)
    if dirty["unparsable"]:
        raise SelfTestError("the scanner could not read its own dirty fixture:"
                            " %r" % dirty["unparsable"])

    # Keyed by the enclosing declaration's line is not available, so key by the
    # fixture's own literal text: each is unique to one function there.
    got = {}
    for l in dirty["literals"]:
        got.setdefault((l["text"].lower(), l["context"]), 0)
        got[(l["text"].lower(), l["context"])] += 1
    for fn, want in sorted(_DIRTY_LITERALS.items()):
        if want not in got:
            near = sorted(c for (t, c) in got if t == want[0])
            wrong.append("dirty fixture: %s() holds %r as a %s literal and the"
                         " scanner reported it as %s"
                         % (fn, want[0], want[1],
                            ", ".join(near) or "nothing"))

    edges = set((e["child"], e["parent"]) for e in dirty["inherits"])
    for child, parent in sorted(_DIRTY_EDGES):
        if (child, parent) not in edges:
            wrong.append("dirty fixture: %s extends %s and the scanner"
                         " reported %s" % (child, parent,
                                           ", ".join(sorted(
                                               p for c, p in edges
                                               if c == child)) or "nothing"))

    if wrong:
        raise SelfTestError("classmap.php is not measuring what it claims:\n  "
                            + "\n  ".join(wrong))
    out = {"clean_literals": 0,
           "dirty_literals": len(dirty["literals"]),
           "dirty_edges": len(dirty["inherits"])}
    # Both instruments, one verdict: `wiring_family` reads both and neither
    # half of what it reports is believable if either scanner is wrong.
    out.update(wiringmap_selftest(tools))
    return out


# --------------------------------------------- earned / gone / intact split --

# Every check in `wiring_family` that a tree with *no step-5 work done*
# already passes, plus the ones a `sed -i -E '/mysql|mariadb/Id'` over the four
# wiring files passes. They are real requirements -- a scanner that cannot read
# the tree, a class left without its parent, a driver map advertising a class
# that is gone are all failures -- but passing them is not evidence of work,
# and §24.2's rule is that anything a do-nothing run satisfies is a guard.
# §24.14 sharpened "do-nothing run" to "mechanical pass", which is what moved
# the nineteen `wiring/gone/` checks out of the scored family: a line filter
# passes every one of them. §24.16 applied the original, weaker form of the
# rule to `wiring/live/fallback-present`, which the rule-6 floor run -- the
# handed step-04 tree, unedited -- passed outright.
_WIRING_INTACT = ("wiring/parseable", "wiring/files-scanned",
                  "wiring/no-new-literals",
                  "wiring/no-orphan-subclass") + _LIVE_INTACT


def _role_of(cid):
    """Which of the three families a check id belongs to. Total by
    construction: a check added to `wiring_family` and forgotten here lands in
    the guard, never in the scored family."""
    if cid in _LIVE:
        return "earned"
    if cid.startswith("wiring/gone/"):
        return "gone"
    return "intact"


# The three are scored from one evaluation, not three: `wiring_family` runs two
# PHP passes over 905 files, and running them again would also let the parts
# disagree if anything under them were not deterministic.
_CACHE = {}


def _wiring_once(roots, fixture, tools):
    key = (tuple(roots), fixture if isinstance(fixture, str) else None, tools)
    if key not in _CACHE:
        _CACHE[key] = wiring_family(roots, fixture, tools)
    return _CACHE[key]


def _split(c, family, role, denom=1):
    """One third of `c`: the checks `_role_of` assigns to `role`.

    A `wiring_family` that crashed before it added any check has nothing to
    split, and handing back an empty family would drop it out of `combine`
    entirely -- which for a guard means the cap silently stops applying. So a
    crash is re-raised into each part with its own denominator.
    """
    if c.error:
        return gate.crashed(family, c.error, denom)
    out = gate.Checks(family)
    for note in c.notes:
        out.note(note)
    for cid, ok, detail in c.results:
        if _role_of(cid) == role:
            out.add(cid, ok, detail)
    return out


def wiring_earned(roots=("/app",), fixture=WIRING_FIXTURE, tools=None):
    """Scored. What is left of the wiring still resolves.

    Six checks and not one of them is satisfiable by deleting a line: see
    `_LIVE` and NOTES §24.14/§24.15.
    """
    return _split(_wiring_once(roots, fixture, tools),
                  "migrated:wiring", "earned", len(_LIVE))


def wiring_gone(roots=("/app",), fixture=WIRING_FIXTURE, tools=None):
    """Guard. Every literal that selects a MySQL back-end is gone.

    This has to be true and it is not evidence of work: `sed -i -E
    '/mysql|mariadb/Id'` over the four files that hold them passes all
    nineteen. It was the scored family until §24.14; it is a cap now.
    """
    return _split(_wiring_once(roots, fixture, tools),
                  "migrated:wiring-gone", "gone")


def wiring_intact(roots=("/app",), fixture=WIRING_FIXTURE, tools=None):
    """Guard. The tree still reads, and nothing was invented or orphaned."""
    return _split(_wiring_once(roots, fixture, tools),
                  "migrated:wiring-intact", "intact", len(_WIRING_INTACT))
