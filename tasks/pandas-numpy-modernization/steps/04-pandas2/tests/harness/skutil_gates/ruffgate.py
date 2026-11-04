"""Run ruff over the whole repo with the scorer's own configuration.

Three flags carry the weight, and each of them closes a way of passing this
gate without doing the work:

``--isolated``     the repo's own ``pyproject.toml``/``ruff.toml`` is not read,
                   so a submission cannot narrow the rule set it is measured
                   against. The config it *does* ship is still counted, by
                   ``suppress.py``, as a suppression.
``--ignore-noqa``  ``# noqa`` does not hide a finding. Same argument: the
                   comments are counted as suppressions instead of silencing
                   the measurement.
``--no-cache``     two runs of the same tree give the same answer even when the
                   verifier reuses a layer.

Notebooks are checked too -- ruff reads ``.ipynb`` natively and reports
``cell N:row:col``. That is deliberate: rule 11 says scan everything, and two
of this repo's notebooks still contain Python-2 ``print`` statements.
"""
from __future__ import print_function, absolute_import, division

import json
import os
import subprocess

# The rule families, staged across the ladder. By the end every family in the
# task line's gates paragraph is at zero; each step owns the slice its target
# version makes meaningful, so no step is asked to fix a finding its own
# toolchain does not yet produce.
#
# The families are cumulative in the same way ``rules.by_step`` is: a step's
# selection is the union of every entry with ``since <= step``.
SELECTIONS = [
    # (since, codes, why this step)
    (1, ["NPY002"],
     "legacy numpy.random global-state draws -- step 1's reproducibility"),
    (2, ["UP", "NPY001", "PD002", "PD003", "PD007", "PD008", "PD009",
         "PD012", "PD015"],
     "Python-2 scaffolding and the pandas/NumPy accessors pandas 1.0 removed. "
     "PD010 (.pivot_table over .pivot/.unstack) and PD013 (.melt over .stack) "
     "are deliberately not here: they are pandas-vet opinions about operations "
     "that are not interchangeable, so gating them can force wrong code; the "
     "whole PD family arrives at step 4, where it is a disclosed house rule."),
    (3, ["B"],
     "bugbear: mutable estimator defaults and bare re-raises, which is what "
     "scikit-learn's own estimator checks trip over"),
    (4, ["PD"],
     "the rest of pandas-vet, including .values -> .to_numpy(), disclosed as a "
     "house rule at this step. pandas 2.2 still defaults Copy-on-Write off; "
     "this selection is about saying what a read returns, not about CoW."),
    (5, ["NPY"],
     "the rest of the NumPy rules, including the 2.0 deprecations"),
]

TARGET_VERSION = {1: "py37", 2: "py39", 3: "py39", 4: "py39", 5: "py39",
                  6: "py312", 7: "py312", 8: "py312"}


def target_for(step):
    """The syntax level ruff parses at. Named so a step's build-time selfcheck
    can assert it: the target decides whether a construct reads as an error or
    as a modernisation, and reading step 6's tree at py37 would invent findings
    the step cannot have."""
    return TARGET_VERSION[step]


def selection_for(step):
    codes = []
    for since, group, _why in SELECTIONS:
        if since <= step:
            for code in group:
                if code not in codes:
                    codes.append(code)
    return sorted(codes)


def run(root, step, ruff="ruff", extra_roots=()):
    """Every ruff finding in the step's selection, as a list of dicts.

    A ruff that will not start is not a clean tree: the caller gets an
    ``error`` key and the gate that consumes this must fail, never pass.
    """
    codes = selection_for(step)
    target = TARGET_VERSION.get(step, "py312")
    roots = [r for r in (root,) + tuple(extra_roots) if r and os.path.isdir(r)]
    cmd = [ruff, "check",
           "--isolated", "--no-cache", "--ignore-noqa",
           "--no-respect-gitignore",
           "--target-version", target,
           "--select", ",".join(codes),
           "--output-format", "json"] + roots
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        out, err = proc.communicate()
    except OSError as exc:
        return {"error": "ruff did not run: %s" % exc, "findings": [],
                "codes": codes, "target_version": target}
    try:
        raw = json.loads(out.decode("utf-8", "replace") or "[]")
    except ValueError:
        return {"error": "ruff output was not JSON: %s"
                         % err.decode("utf-8", "replace")[:400],
                "findings": [], "codes": codes, "target_version": target}

    findings = []
    for item in raw:
        loc = item.get("location") or {}
        findings.append({
            "code": item.get("code") or "invalid-syntax",
            "path": _rel(item.get("filename") or "", roots),
            "line": loc.get("row", 0),
            "col": loc.get("column", 0),
            "message": (item.get("message") or "")[:160],
        })
    findings.sort(key=lambda f: (f["code"], f["path"], f["line"], f["col"]))

    by_code = {}
    for f in findings:
        by_code[f["code"]] = by_code.get(f["code"], 0) + 1
    return {"error": None, "findings": findings, "by_code": by_code,
            "total": len(findings), "codes": codes,
            "target_version": target, "command": cmd}


def _rel(path, roots):
    for base in roots:
        base = os.path.normpath(base)
        if path.startswith(base + os.sep):
            return os.path.relpath(path, base)
    return path
