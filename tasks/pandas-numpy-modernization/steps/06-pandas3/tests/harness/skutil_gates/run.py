"""Command line for the migrated-ness gates.

    python -m skutil_gates.run observe --tree /app --step 3 \
        [--installed-root /opt/venv/lib/python3.9/site-packages/skutil] \
        [--recording /tmp/actual.json] --out obs.json

    python -m skutil_gates.run score --obs obs.json --manifest manifest.json \
        --out gates.json

    python -m skutil_gates.run counts --tree DIR --step N      # authoring aid
    python -m skutil_gates.run semantics                       # authoring aid

``observe`` is everything that has to happen inside the step's own container:
it scans, it runs ruff, it counts suppressions, it asks the interpreter what is
installed. ``score`` is pure arithmetic over that bundle and the step's
manifest, so a scoring bug can be reproduced on any machine from the saved
observation file alone.
"""
from __future__ import print_function, absolute_import, division

import argparse
import json
import os
import sys

from . import (census, probe, repro, ruffgate, scan, score as score_mod,
               suppress)


def _installed_roots(explicit, discover=True):
    """Where the package the interpreter would actually import lives.

    Passed explicitly by the verifier when it knows; discovered here when it
    does not, so that ``observe`` still covers the installed copy if the
    submission installed one somewhere unexpected. A develop install points
    back at the tree, in which case the root is dropped as a duplicate.

    ``discover=False`` is what this task's verifiers pass, and the reason is
    that they build the installed copy themselves, from a copy of ``/app``, so
    its ``.py`` files are the tree's ``.py`` files. Scanning both would report
    every construct twice and the entry counts in ``manifest.json`` -- measured
    on the tree alone -- would stop matching anything. The runtime probes in
    ``repro`` still read the installed package, because what they measure
    (import-time warnings, the public API) only exists once it is installed.
    """
    roots = []
    if explicit:
        roots.append(explicit)
        return roots
    if not discover:
        return roots
    try:
        import skutil
        path = os.path.dirname(os.path.abspath(skutil.__file__))
        roots.append(path)
    except Exception:
        pass
    return roots


def _tree_paths(root, limit=5000):
    """Every file the tree still has, as relative paths.

    Every gate counts occurrences of something old and rewards fewer of them,
    so deleting a file is an improvement as far as the counts can see. That is
    not hypothetical here: most of this tree's invalid escape sequences live in
    one vendored docstring parser that nothing imports, and `rm` would retire
    them as thoroughly as an `r` prefix would. A ``paths`` check in the
    manifest is what turns "the file is still there" into a number, instead of
    a sentence in the instruction that nothing measures.

    Build output is skipped for the same reason ``clean_build_products``
    removes it: it is not the candidate's tree, and whether it exists depends
    on what the agent happened to run last. A Cython-generated ``.c``/``.cpp``
    that sits next to its ``.pyx`` is build output by that same test -- any
    ``pip install`` regenerates and clobbers it, so its presence and mtime
    track the last build, not a source edit -- even though upstream committed
    it in-tree beside the source it comes from. Skip it when its ``.pyx`` is
    present, so a defensible clean of a regenerable artifact is not counted as
    a deletion; the ``.pyx`` itself stays pinned (and is load-bearing: the
    build needs it), so nothing real can be gutted through this hole.
    """
    skip = set([".git", "__pycache__", ".pytest_cache", ".ruff_cache",
                ".mypy_cache", "build", "dist", ".eggs"])
    generated = set([".c", ".cpp", ".cxx", ".cc"])
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames
                             if d not in skip and not d.endswith(".egg-info"))
        pyx_stems = set(os.path.splitext(n)[0]
                        for n in filenames if n.endswith(".pyx"))
        for name in sorted(filenames):
            stem, ext = os.path.splitext(name)
            if ext in generated and stem in pyx_stems:
                continue
            rel = os.path.relpath(os.path.join(dirpath, name), root)
            out.append(rel.replace(os.sep, "/"))
            if len(out) >= limit:
                return sorted(out)
    return sorted(out)


def cmd_observe(args):
    tree = os.path.abspath(args.tree)
    extra = []
    for root in _installed_roots(args.installed_root, args.discover_installed):
        root = os.path.abspath(root)
        if root == tree or root.startswith(tree + os.sep):
            continue
        extra.append(root)

    findings, files = scan.scan_tree(tree, args.step, extra_roots=extra)
    ruff = ruffgate.run(tree, args.step, ruff=args.ruff, extra_roots=extra)
    supp = suppress.scan_tree(tree, extra_roots=extra)

    obs = {
        "step": args.step,
        "tree": tree,
        "extra_roots": extra,
        "files": files,
        "ast_counts": scan.counts(findings),
        "ast_findings": findings if args.full else findings[:400],
        "ast_findings_truncated": (not args.full) and len(findings) > 400,
        "ruff": ruff,
        "suppressions": {"total": supp["total"], "by_kind": supp["by_kind"],
                         "items": supp["items"] if args.full
                         else supp["items"][:200]},
        "semantics": probe.semantics(),
        "versions": probe.installed_versions(args.dist or []),
        "absent_modules_present": probe.absent_modules(args.absent or []),
        "tree_paths": _tree_paths(tree),
        # What is *in* the files the paths list says are still there. See
        # ``census`` for why "still there" is not enough on its own.
        "census": census.census_tree(tree),
    }
    # Spawns interpreters that import the candidate's tree, so it is skipped
    # for the authoring aids that only want counts.
    if args.repro:
        obs["repro"] = repro.run(tree, python=args.python)
    else:
        obs["repro"] = {"checks": {}, "detail": {"skipped": True}}
    if args.recording and os.path.exists(args.recording):
        obs["warnings"] = probe.warning_census(args.recording)
    else:
        obs["warnings"] = {"by_category": {}, "total": 0, "examples": {},
                           "note": "no recording supplied"}

    _write(args.out, obs)
    print("observed step %d: %d ast findings, %d ruff findings, "
          "%d suppressions"
          % (args.step, len(findings), ruff.get("total", -1), supp["total"]))
    return 0


def cmd_repro(args):
    """The runtime probes on their own: `repro.run`, whose children import
    the candidate's package (from ``--python``, the candidate's interpreter),
    written on their own. The verifiers run this as a stage separate from
    ``observe --no-repro``, so the static scans are taken in a process tree no
    candidate code runs in, and ``score --repro`` joins the two."""
    result = repro.run(os.path.abspath(args.tree), python=args.python)
    _write(args.out, result)
    print("repro: %d checks, %d ok" % (
        len(result.get("checks", {})),
        sum(1 for v in result.get("checks", {}).values() if v)))
    return 0


# What a runtime-probe stage that produced nothing readable stands for: every
# named check failed, the public-API probe saw no importable package (so
# `score.importable` forces family B to 0, as when `import skutil` fails), and
# the import-warning probe could not run. Breaking the instrument must never
# read as a clean result.
REPRO_MISSING = {
    "checks": {},
    "public_api": {"error": "the runtime probe stage produced no result"},
    "self_warnings": {"total": 9999, "by_category": {"ProbeFailed": 9999},
                      "items": []},
    "detail": {"error": "the runtime probe stage produced no result"},
}


def cmd_score(args):
    with open(args.obs) as fh:
        obs = json.load(fh)
    # The runtime probes are their own observing stage too (see cmd_repro).
    # Absent, unreadable or not a bundle, they read as REPRO_MISSING.
    if getattr(args, "repro", None):
        try:
            with open(args.repro) as fh:
                got = json.load(fh)
        except (OSError, ValueError):
            got = None
        if not isinstance(got, dict) or not isinstance(got.get("checks"), dict):
            got = REPRO_MISSING
        obs["repro"] = got
    with open(args.manifest) as fh:
        manifest = json.load(fh)
    report = score_mod.score(manifest, obs)
    _write(args.out, report)
    print("gates: %.4f (new %.4f x carried %.4f)"
          % (report["score"], report["new_mean"], report["carried_mean"]))
    if not report.get("importable", True):
        print("  family B forced to 0: %s"
              % (report.get("import_detail") or {}).get("why"))
    for group in ("new", "carried"):
        for row in report[group]:
            if row["credit"] < 1.0:
                print("  %-8s %-40s observed=%s limit=%s credit=%.2f"
                      % (group, row["name"], row["observed"], row["limit"],
                         row["credit"]))
    return 0


def cmd_counts(args):
    findings, files = scan.scan_tree(os.path.abspath(args.tree), args.step)
    counts = scan.counts(findings)
    print("files: %s" % files)
    for name in sorted(counts):
        print("%-36s %d" % (name, counts[name]))
    if args.out:
        _write(args.out, {"counts": counts, "findings": findings,
                          "files": files})
    return 0


def cmd_semantics(args):
    print(json.dumps(probe.semantics(), indent=2, sort_keys=True))
    return 0


def _write(path, payload):
    """Write the bundle to `path`, or to an inherited descriptor for
    ``fd:N``. The verifiers pass ``--out fd:3`` with fd 3 opened by root on a
    root-owned file: this process writes through the descriptor it was handed,
    nothing it spawns inherits it (close_fds), and no other process of its uid
    can open that file for writing -- so a process the candidate left running
    cannot rewrite the bundle between this write and the stage's end."""
    if not path:
        return
    if path.startswith("fd:"):
        fh = os.fdopen(int(path[3:]), "w")
    else:
        fh = open(path, "w")
    with fh:
        json.dump(payload, fh, indent=1, sort_keys=True)


def main(argv=None):
    p = argparse.ArgumentParser(prog="skutil_gates.run")
    sub = p.add_subparsers(dest="cmd")

    o = sub.add_parser("observe")
    o.add_argument("--tree", required=True)
    o.add_argument("--step", type=int, required=True)
    o.add_argument("--installed-root")
    o.add_argument("--no-installed-root", dest="discover_installed",
                   action="store_false", default=True,
                   help="do not add the importable copy of the package as a "
                        "second scan root (see _installed_roots)")
    o.add_argument("--recording")
    o.add_argument("--ruff", default="ruff")
    o.add_argument("--dist", action="append")
    o.add_argument("--absent", action="append")
    o.add_argument("--full", action="store_true")
    o.add_argument("--repro", dest="repro", action="store_true", default=True)
    o.add_argument("--no-repro", dest="repro", action="store_false")
    o.add_argument("--python", help="the interpreter the runtime probes' "
                                    "children run (default: this one)")
    o.add_argument("--out", required=True)
    o.set_defaults(func=cmd_observe)

    r = sub.add_parser("repro")
    r.add_argument("--tree", required=True)
    r.add_argument("--python", help="the interpreter the probes' children "
                                    "run (default: this one)")
    r.add_argument("--out", required=True)
    r.set_defaults(func=cmd_repro)

    s = sub.add_parser("score")
    s.add_argument("--obs", required=True)
    s.add_argument("--manifest", required=True)
    s.add_argument("--repro",
                   help="the `repro` stage's output, merged into the bundle")
    s.add_argument("--out")
    s.set_defaults(func=cmd_score)

    c = sub.add_parser("counts")
    c.add_argument("--tree", required=True)
    c.add_argument("--step", type=int, required=True)
    c.add_argument("--out")
    c.set_defaults(func=cmd_counts)

    m = sub.add_parser("semantics")
    m.set_defaults(func=cmd_semantics)

    args = p.parse_args(argv)
    if not getattr(args, "func", None):
        p.print_help()
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
