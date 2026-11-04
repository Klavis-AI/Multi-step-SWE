# -*- coding: utf-8 -*-
"""Measure which lines of the tree the behaviour suite actually reaches.

    python -m harness.coverage_map <tree> <out.json> [--only PREFIX]
                                  [--lines REL[,REL...]]

``--lines`` additionally records, for each named tree-relative file, the exact
line numbers the suite never reached. The aggregate percentage says how much of
a file is covered; it does not say *what* is uncovered, and at a rung whose
whole subject lives in one file (``lib/urllib3/util/ssl_.py``, for instance)
that is the question worth asking.

This is a development tool, not part of any step's verifier. It runs every
registered case in a *single* process under ``coverage``, which is exactly what
``runcase`` refuses to do: observations recorded that way are worthless because
module state leaks between cases. The line map is not -- a line executed is a
line executed regardless of what ran before it -- so the two uses are kept in
separate programs rather than sharing one runner.

Cases that raise here are still counted: the lines they reached before raising
are the lines the suite covers.
"""
from __future__ import print_function

import json
import os
import sys


def _tree_files(tree):
    root = os.path.join(tree, "lib")
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames
                       if d not in ("__pycache__", "test", "tests")]
        for name in filenames:
            if name.endswith(".py"):
                out.append(os.path.join(dirpath, name))
    return sorted(out)


def main(argv):
    tree = os.path.abspath(argv[1])
    out_path = argv[2]
    only = None
    if "--only" in argv:
        only = argv[argv.index("--only") + 1]
    want_lines = set()
    if "--lines" in argv:
        want_lines = set(argv[argv.index("--lines") + 1].split(","))

    import coverage

    sys.path.insert(0, os.path.join(tree, "lib"))
    sys.path.insert(0, tree)

    from . import env, registry, server
    env.pin()
    registry.load_all()

    cov = coverage.Coverage(data_file=None, branch=False,
                            include=[os.path.join(tree, "lib", "*")])
    ran = errored = 0
    cov.start()
    for cid in registry.ids():
        if only and not cid.startswith(only):
            continue
        c = registry.CASES[cid]
        routes, default = {}, None
        try:
            if c.fixture is not None:
                spec = c.fixture()
                if isinstance(spec, tuple):
                    routes, default = spec
                else:
                    routes = spec
            tls = server.server_chain() if c.tls else None
            fx = server.Fixture(routes, default=default, tls=tls)
        except Exception:
            errored += 1
            continue
        try:
            from .runcase import Ctx
            c.fn(Ctx(tree, fx))
            ran += 1
        except BaseException:
            errored += 1
        finally:
            fx.close()
    cov.stop()

    data = cov.get_data()
    report = {}
    for path in _tree_files(tree):
        rel = os.path.relpath(path, tree)
        try:
            analysis = cov._analyze(path)
        except Exception:
            continue
        total = len(analysis.statements)
        missed = len(analysis.missing)
        report[rel] = {"statements": total, "missed": missed,
                       "covered": total - missed,
                       "percent": (round(100.0 * (total - missed) / total, 1)
                                   if total else 100.0)}
        if rel in want_lines:
            report[rel]["missing_lines"] = sorted(analysis.missing)
    with open(out_path, "w") as fh:
        json.dump({"ran": ran, "errored": errored, "files": report}, fh,
                  indent=1, sort_keys=True)
    print("ran %d, errored %d -> %s" % (ran, errored, out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
