# -*- coding: utf-8 -*-
"""Which files the ">=75% line coverage" bar is computed over, and why.

    python -m harness.relevance <tree> [cov.json] [--list CLASS]

migration_prompt.md rule 7 sets the bar over "the migration-relevant files",
and gives the reason in the same sentence: *so covered code cannot be deleted
without failing it*. That reason is what this module turns into a rule. Every
``.py`` file in the tree is assigned exactly one class:

``BEHAVIOUR``
    The migration will rewrite it, and it can be reached from outside through
    the codebase's own surface (§4.1 of NOTES-authoring.md). These are the
    files the bar is computed over -- numerator and denominator both.

``STATIC``
    The migration will rewrite it, but nothing here can drive it: it needs a
    host application, a platform or a third-party package that is not
    installable in either image. Excluded from the bar and covered by the
    step's migrated-ness gates instead (rule 11), which read the AST and do
    not need to run the code.

``RETIRED``
    A step's requirements name it for deletion. Excluded from the bar in both
    halves, deliberately: covering a deletion target would pin it in place and
    make the step that removes it unpassable (INSTRUCTIONS trap 9).

``INHERITED``
    A vendored library's own test suite. The agent gets these in its tree, so
    rule 6 keeps them out of the scored families entirely (NOTES A-5).

Rules are matched as globs against the tree-relative path and **the last
matching rule wins**, so a specific line below overrides a general one above
it. A file matching no rule is reported as ``UNCLASSIFIED`` and makes this
program exit non-zero: a bar computed over a set that quietly lost a file is
the kind of instrument INSTRUCTIONS trap 6 is about.
"""
from __future__ import print_function

import fnmatch
import json
import os
import sys

BEHAVIOUR = "BEHAVIOUR"
STATIC = "STATIC"
RETIRED = "RETIRED"
INHERITED = "INHERITED"

#: (glob, class, reason). Last match wins.
RULES = (
    # ---------------------------------------------------------- default ----
    ("*.py", BEHAVIOUR,
     "vendored library code the ladder rewrites, reachable through the "
     "codebase's own client/transport/parser surface"),

    # ------------------------------------------- the Sublime application ---
    # D-6: every one of these does `import sublime` at module scope, and the
    # Sublime Text binary is not redistributable. Gated statically.
    ("ElasticsearchClient.py", STATIC, "imports sublime at module scope (D-6)"),
    ("events.py", STATIC, "imports sublime at module scope (D-6)"),
    ("commands/*.py", STATIC, "imports sublime at module scope (D-6)"),
    ("panel/*.py", STATIC, "imports sublime at module scope (D-6)"),

    # ------------------------------------------ absent third-party hosts ---
    # The stub trick that reaches `memcached.py` below does not reach this
    # one, and the reason is a scope decision rung 01 made on purpose.
    # `perform_request` needs `RestRequest` and `Method` from `.esthrift`,
    # and `esthrift/Rest.py` opens with `from ttypes import *` -- an implicit
    # relative import, which no Python 3 resolves. Step 01's instruction
    # names that line and puts it outside the milestone, so it is still there
    # at rung 07 by design. Stubbing `thrift.*` therefore buys nothing: the
    # in-tree generated module is what fails to import, not the absent
    # package.
    ("lib/elasticsearch/connection/thrift.py", STATIC,
     "needs the `thrift` package, and `.esthrift.Rest` cannot import on any "
     "Python 3 because of an implicit relative import step 01 leaves alone"),
    ("lib/elasticsearch/connection/esthrift/*.py", STATIC,
     "thrift-compiler output; every module imports `thrift.Thrift`"),
    ("lib/elasticsearch/helpers/test.py", STATIC,
     "needs a live Elasticsearch at ELASTICSEARCH_URL, plus unittest2"),
    ("*/urllib3/contrib/*.py", STATIC,
     "needs pyOpenSSL/pyasn1/ndg-httpsclient or python-ntlm; pyopenssl.py "
     "also imports socket._fileobject, which no Python 3 has"),
    ("lib/dateutil/tzwin.py", STATIC, "Windows registry only"),
    ("*/chardet/chardetect.py", STATIC,
     "argparse entry point that imports the top-level `chardet`, a name this "
     "vendored copy does not publish"),

    # ------------------------------------- driven without their host ------
    # Both of these sit in the block above by rights -- `memcached.py` opens
    # with `import pylibmc` and `pooling.py` is abstract -- and both were
    # classified STATIC on that reading until the rung-05 review asked what
    # was actually reading them. The answer was nothing, and that was a hole,
    # not a fact about the tree: `pylibmc` is imported inside `__init__` and
    # used for exactly two methods, so a module object carrying `get` and
    # `set` reaches `perform_request` without a server, and `PoolingConnection`
    # needs only a `_make_connection` to be a complete object. The cases in
    # `cases/esconn.py` do both. Last match wins, so these two lines promote
    # them out of the block above and into the bar, where the coverage they
    # now have (94.1% and 87.5% on the baseline) has to be kept by every rung
    # that rewrites them. That is the point of rule 7's reason clause: a file
    # excluded from the bar can be deleted for free, and the str/bytes
    # boundary in `perform_request` is the single most migration-fragile
    # expression in the connection layer.
    ("lib/elasticsearch/connection/memcached.py", BEHAVIOUR,
     "`pylibmc` is imported inside `__init__` and only `get`/`set` are used, "
     "so a stub module drives `perform_request` with no server"),
    ("lib/elasticsearch/connection/pooling.py", BEHAVIOUR,
     "abstract only in that `_make_connection` is supplied by a subclass; "
     "setting it directly drives the queue, and the memcached cases drive it "
     "through a real subclass"),

    # -------------------------------------------------- deletion targets ---
    ("lib/six.py", RETIRED, "step 01 retires six"),
    ("*/packages/six.py", RETIRED, "step 01 retires six"),
    ("*/packages/ordered_dict.py", RETIRED,
     "the 2.6 OrderedDict backport; `from collections import OrderedDict` "
     "always succeeds on 2.7+, so nothing can reach it on any interpreter "
     "this task runs"),
    ("*/packages/ssl_match_hostname/*.py", RETIRED,
     "step 03 retires the vendored hostname-matching backport in favour of "
     "the ssl module's own certificate checking"),
    ("lib/urllib3/packages/__init__.py", RETIRED,
     "re-exports only six and ssl_match_hostname, both retired"),

    # ------------------------------------- the vendored suite's own tests ---
    ("lib/analytics/test/*.py", INHERITED,
     "the vendored analytics test suite; the agent has it, so rule 6 keeps it "
     "out of both scored families (NOTES A-5)"),
)

CLASSES = (BEHAVIOUR, STATIC, RETIRED, INHERITED)


def classify(rel):
    """Return ``(class, reason)`` for a tree-relative path, or ``(None, None)``."""
    hit = (None, None)
    for pattern, klass, reason in RULES:
        if fnmatch.fnmatch(rel, pattern):
            hit = (klass, reason)
    return hit


def tree_files(tree):
    out = []
    for dirpath, dirnames, filenames in os.walk(tree):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for name in filenames:
            if name.endswith(".py"):
                out.append(os.path.relpath(os.path.join(dirpath, name), tree))
    return sorted(out)


def report(tree, cov_path=None, show=None, stream=sys.stdout):
    rels = tree_files(tree)
    buckets = {k: [] for k in CLASSES}
    unclassified = []
    reasons = {}
    for rel in rels:
        klass, reason = classify(rel)
        if klass is None:
            unclassified.append(rel)
            continue
        buckets[klass].append(rel)
        reasons[rel] = reason

    def w(fmt, *a):
        print(fmt % a if a else fmt, file=stream)

    w("%d .py files under %s", len(rels), os.path.abspath(tree))
    for klass in CLASSES:
        w("  %-10s %4d", klass, len(buckets[klass]))
    if unclassified:
        w("\n%d UNCLASSIFIED -- the rule table does not cover the tree:",
          len(unclassified))
        for rel in unclassified:
            w("  %s", rel)

    problems = len(unclassified)
    if cov_path:
        with open(cov_path) as fh:
            cov = json.load(fh)["files"]

        # Trap 6: the bar is only meaningful if the map measured the same set.
        missing = [r for r in buckets[BEHAVIOUR] if r not in cov]
        unknown = [r for r in cov if classify(r)[0] is None]
        if missing:
            problems += len(missing)
            w("\n%d BEHAVIOUR files absent from the coverage map -- the bar "
              "below is computed over less than it claims:", len(missing))
            for rel in missing:
                w("  %s", rel)
        if unknown:
            problems += len(unknown)
            w("\n%d files in the coverage map that no rule classifies:",
              len(unknown))
            for rel in unknown:
                w("  %s", rel)

        w("")
        for klass in CLASSES:
            rows = [(rel, cov[rel]) for rel in buckets[klass] if rel in cov]
            stmts = sum(d["statements"] for _, d in rows)
            done = sum(d["covered"] for _, d in rows)
            w("%-10s %4d files in map, %6d statements, %6d covered  %5.1f%%",
              klass, len(rows), stmts, done,
              (100.0 * done / stmts) if stmts else 100.0)

        rows = [(rel, cov[rel]) for rel in buckets[BEHAVIOUR] if rel in cov]
        stmts = sum(d["statements"] for _, d in rows)
        done = sum(d["covered"] for _, d in rows)
        pct = (100.0 * done / stmts) if stmts else 100.0
        w("\nrule 7: %d/%d = %.1f%% of BEHAVIOUR statements  (bar: 75.0%%)  %s",
          done, stmts, pct, "PASS" if pct >= 75.0 else "SHORT")

        # Printed so that excluding the deletion targets cannot quietly be the
        # thing that clears the bar. If the two numbers ever straddle 75%, the
        # classification is doing the work instead of the suite.
        both = [(rel, cov[rel]) for k in (BEHAVIOUR, RETIRED)
                for rel in buckets[k] if rel in cov]
        bstmts = sum(d["statements"] for _, d in both)
        bdone = sum(d["covered"] for _, d in both)
        w("        with RETIRED counted in as well: %d/%d = %.1f%%",
          bdone, bstmts, (100.0 * bdone / bstmts) if bstmts else 100.0)

        w("\nworst BEHAVIOUR files by uncovered statements:")
        for rel, d in sorted(rows, key=lambda t: -t[1]["missed"])[:30]:
            w("  %5d missed of %5d  %5.1f%%  %s",
              d["missed"], d["statements"], d["percent"], rel)

    if show:
        w("\n--- %s ---", show)
        for rel in buckets.get(show, []):
            w("  %-62s %s", rel, reasons[rel])

    return 1 if problems else 0


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    show = None
    if "--list" in argv:
        show = argv[argv.index("--list") + 1]
    tree = args[0]
    cov = args[1] if len(args) > 1 else None
    return report(tree, cov, show=show)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
