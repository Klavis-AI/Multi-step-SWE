"""Reproducibility gates: the ones that need two processes, not one.

The behavior corpus cannot ask these questions. It seeds the global RNG itself
before every case (``skutil_probe.run.run_case``), which is what makes the
balancers' sampled rows recordable at all -- and that seeding is exactly what
hides the defect this family is about. A library that reseeds the caller's RNG
at import, and then draws from it behind the caller's back, records perfectly
and is still not reproducible.

So each check here runs the library in a *fresh interpreter*, one or two of
them, and compares what comes back:

``import_preserves_rng``         seed, snapshot, ``import skutil``, snapshot,
                                 for NumPy's global stream and ``random``'s.
``module_fixtures_preserve_rng`` then call every module-level fixture the
                                 package exposes and snapshot again.
                                 ``skutil.setup_module`` reseeds both from an
                                 unseeded draw, which is why the package's own
                                 suite cannot be reproduced.
``seeded_repeatable``            two processes, same seed, same stochastic
                                 workload, same bytes.
``figures_repeatable``           two processes, one figure, same PNG.
``random_state_honored``         every entry point that draws exposes
                                 ``random_state``; an int makes two calls in
                                 one process agree; and the call leaves the
                                 process-global stream where it found it.

The last is the one that cannot be satisfied by deleting the reseed alone: a
draw routed through a caller-supplied generator is the only way to pass it, and
the only way to pass it *without* moving the global stream.

Every child gets the same frozen environment the recorder uses, for the same
reason (``skutil_probe/run.py``): BLAS threads, ``PYTHONHASHSEED``,
``SOURCE_DATE_EPOCH``, ``TZ``, ``LC_ALL``, ``MPLBACKEND``. A check that failed
because two children disagreed about the timezone would be a finding about this
file.

Python 3.7 compatible: these run in every verifier image on the ladder.
"""
from __future__ import print_function, absolute_import, division

import binascii
import json
import os
import subprocess
import sys
import tempfile


CHILD_ENV = {
    "OMP_NUM_THREADS": "1",
    "OPENBLAS_NUM_THREADS": "1",
    "MKL_NUM_THREADS": "1",
    "NUMEXPR_NUM_THREADS": "1",
    "VECLIB_MAXIMUM_THREADS": "1",
    "PYTHONHASHSEED": "0",
    "PYTHONDONTWRITEBYTECODE": "1",
    "MPLBACKEND": "Agg",
    "TZ": "UTC",
    "SOURCE_DATE_EPOCH": "1514764800",
    "LC_ALL": "C.UTF-8",
}

# Shared by the children that need data. Built from an explicit RandomState so
# the frame itself is never a source of disagreement -- only what the library
# does with it is.
PRELUDE = '''
import hashlib, json, sys
import numpy as np
import pandas as pd

def frame(n=150, seed=4):
    rs = np.random.RandomState(seed)
    counts = [int(n * 0.6), int(n * 0.28)]
    counts.append(n - sum(counts))
    y = np.concatenate([np.full(c, k, dtype=np.int64)
                        for k, c in enumerate(counts)])
    X = pd.DataFrame(rs.rand(n, 3), columns=["a", "b", "c"])
    X["target"] = y
    return X

def digest_frame(h, out):
    arr = np.asarray(out[["a", "b", "c"]], dtype=np.float64)
    h.update(np.ascontiguousarray(arr).tobytes())
    h.update(np.ascontiguousarray(
        np.asarray(out["target"]).astype(np.int64)).tobytes())

def emit(payload):
    sys.stdout.write(RESULT_MARK + json.dumps(payload, sort_keys=True) + "\\n")
'''


def _run(script, cwd, python, timeout, argv=(), tree=None):
    """Run one child; return its ``@@RESULT@@`` payload or an error dict.

    ``cwd`` is deliberately *not* the tree. The verifier installs the candidate
    and grades the installed package, so a child that ran from ``/app`` would
    import the source directory by accident and a submission whose build is
    broken would score as if it worked.
    """
    env = dict(os.environ)
    env.update(CHILD_ENV)
    env["SKUTIL_TREE"] = tree or cwd
    # The child must import the candidate's installed package, not a copy this
    # process happens to have imported already.
    env.pop("PYTHONSTARTUP", None)
    env.pop("PYTHONPATH", None)
    # A marker of this run's own, so a line the candidate's code prints without
    # reading this child's argument is not taken for the child's result.
    mark = "@@RESULT-%s@@" % binascii.hexlify(os.urandom(16)).decode("ascii")
    prelude = "RESULT_MARK = %r\n" % mark + PRELUDE
    proc = subprocess.Popen(
        [python] + list(argv) + ["-c", prelude + script], cwd=cwd, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        out, err = proc.communicate(timeout=timeout)
    except Exception:
        proc.kill()
        proc.communicate()
        return {"error": "timeout"}
    text = out.decode("utf-8", "replace")
    for line in text.splitlines():
        if line.startswith(mark):
            try:
                return json.loads(line[len(mark):])
            except ValueError:
                return {"error": "unparsable result line"}
    tail = err.decode("utf-8", "replace").strip().splitlines()[-4:]
    return {"error": "no result", "rc": proc.returncode, "stderr": tail}


# Import, then every module-level fixture the package exposes.
#
# Importing skutil does *not* reseed on the baseline: the reseed lives in
# ``skutil.setup_module``, the nose/pytest per-module fixture, which runs at the
# top of every test module rather than at import. That distinction is the whole
# finding -- the package's own suite draws its seed from an unseeded
# ``np.random.uniform()`` and then reseeds both global RNGs with it, so no run
# of it can be reproduced, while an import looks innocent. So both moments are
# snapshotted and reported separately; only the second one discriminates.
RNG_STATE = '''
import random
import pkgutil, importlib

def snap():
    s = np.random.get_state()
    return {"np": [s[0], hashlib.sha256(s[1].tobytes()).hexdigest(),
                   int(s[2]), int(s[3]), float(s[4])],
            "py": hashlib.sha256(
                repr(random.getstate()).encode("utf-8")).hexdigest()}

np.random.seed(11)
random.seed(11)
at_start = snap()

import skutil
after_import = snap()

# Call every module-level fixture in the package's own modules. A fixture that
# is gone cannot reseed anything, which is one correct way to pass this.
called = []
mods = [skutil]
for info in pkgutil.walk_packages(skutil.__path__, "skutil."):
    if ".tests" in info[1]:
        continue
    try:
        mods.append(importlib.import_module(info[1]))
    except Exception:
        continue
for mod in mods:
    for name in ("setup_module", "setup", "teardown_module", "teardown"):
        fn = getattr(mod, name, None)
        if not callable(fn):
            continue
        called.append(mod.__name__ + "." + name)
        try:
            fn(mod)
        except TypeError:
            try:
                fn()
            except Exception:
                pass
        except Exception:
            pass
after_hooks = snap()

emit({"import_ok": bool(at_start == after_import),
      "hooks_ok": bool(after_import == after_hooks),
      "hooks_called": sorted(called)})
'''

# Every warning the tree raises about itself, at compile time and at import
# time, under `-X dev`.
#
# This is the half of the "-W error -X dev is clean" property the behavior
# recording cannot see. The recorder's census counts warnings raised from
# skutil frames *while a case runs*; a DeprecationWarning for
# ``from collections import Sized``, or for an invalid ``\$`` escape, is raised
# once when the module is compiled and imported, long before any case starts.
#
# Sources are compiled explicitly rather than left to ``import``: a cached
# ``__pycache__`` entry -- which the agent's own container will certainly have
# produced -- makes the compile-time warnings disappear, and "no warnings
# because nothing was compiled" must never read as "no warnings".
IMPORT_WARNINGS = '''
import os, warnings, pkgutil, importlib

root = os.path.abspath(os.environ["SKUTIL_TREE"])
rows = []
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("always")
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames
                       if d != "__pycache__" and not d.startswith(".")]
        for name in sorted(filenames):
            if not name.endswith(".py"):
                continue
            path = os.path.join(dirpath, name)
            try:
                fh = open(path, "rb")
                try:
                    src = fh.read()
                finally:
                    fh.close()
                compile(src, path, "exec", dont_inherit=True)
            except SyntaxError:
                # Counted by the `unparsable` rule, not by this one.
                continue
            except Exception:
                continue
    installed = None
    try:
        import skutil
        installed = os.path.dirname(os.path.dirname(
            os.path.abspath(skutil.__file__)))
        for info in pkgutil.walk_packages(skutil.__path__, "skutil."):
            try:
                importlib.import_module(info[1])
            except Exception:
                pass
    except Exception as exc:
        rows.append(["ImportFailed", str(exc)[:200], "skutil"])
    # Two roots, because the two halves of this census live in two places. The
    # compile walk reads the tree at SKUTIL_TREE; the import walk loads the
    # *installed* package, whose files sit in site-packages, and a filter that
    # only knew about the tree would silently drop every import-time warning --
    # including the `from collections import Sized` DeprecationWarning that is
    # the reason this probe exists. Only skutil's own package directory is
    # accepted from site-packages, never its neighbours, and the path is
    # reported package-relative so the same site found by both walks is one row
    # rather than two.
    for w in caught:
        where = os.path.abspath(w.filename or "")
        if where.startswith(root + os.sep):
            rel = os.path.relpath(where, root)
        elif installed and where.startswith(
                os.path.join(installed, "skutil") + os.sep):
            rel = os.path.relpath(where, installed)
        else:
            continue
        rows.append([w.category.__name__,
                     str(w.message)[:160],
                     rel + ":" + str(w.lineno)])

# One warning site is one finding. The same `\\$` escape reached through two
# roots, or the same module compiled once and imported once, is one thing the
# migration has to fix -- counting it twice would make the gate's entry number
# depend on how many copies of the tree happened to be reachable.
unique = sorted(set(tuple(r) for r in rows))
by_category = {}
for cat, _msg, _where in unique:
    by_category[cat] = by_category.get(cat, 0) + 1
emit({"total": len(unique), "by_category": by_category,
      "items": [list(r) for r in unique][:60]})
'''

# The public surface, read off the installed package.
#
# Rule 1 names this as the interface, and the trajectory analysis asks whether
# a submission scored by deleting code the fixtures do not reach. The corpus
# covers most of the surface, but "most" is the gap: a name no case happens to
# call can be deleted for free, and the next step inherits a smaller library
# than the one the instruction described. So the names are counted directly,
# and the manifest says which ones a step is allowed to lose.
PUBLIC_API = '''
import importlib, inspect

MODULES = ["skutil", "skutil.base", "skutil.decomposition",
           "skutil.feature_selection", "skutil.grid_search", "skutil.metrics",
           "skutil.model_selection", "skutil.odr", "skutil.preprocessing",
           "skutil.testing", "skutil.utils"]

out = {}
for name in MODULES:
    try:
        mod = importlib.import_module(name)
    except Exception as exc:
        out[name] = {"error": type(exc).__name__ + ": " + str(exc)[:160]}
        continue
    declared = getattr(mod, "__all__", None)
    if declared is None:
        declared = [n for n in dir(mod) if not n.startswith("_")]
    # Declared and actually resolvable. An ``__all__`` entry that no longer
    # exists is a broken export, not a kept one. Submodules are dropped: a
    # package that stops re-exporting ``skutil.utils.fixes`` as an attribute
    # has reorganised itself, which every one of these steps is allowed to do;
    # a package that stops exporting ``MulticollinearityFilterer`` has lost a
    # class someone imports.
    # A set, not a list: ``skutil.utils.util.__all__`` lists 'is_integer'
    # twice, and one name listed twice is still one name.
    keep = set()
    for n in declared:
        if not hasattr(mod, n):
            continue
        if inspect.ismodule(getattr(mod, n)):
            continue
        keep.add(n)
    out[name] = {"names": sorted(keep), "declared": sorted(set(declared))}
emit(out)
'''

# Does every estimator still honour scikit-learn's constructor contract?
#
# ``BaseEstimator.get_params`` reads each ``__init__`` parameter off an
# attribute of exactly that name. Through scikit-learn 0.20 a missing attribute
# fell back to ``None``; 0.22 removed the fallback and raises AttributeError
# instead. ``clone`` is ``type(est)(**est.get_params(deep=False))`` plus an
# identity check on the copy's params, so ``Pipeline``, ``GridSearchCV`` and
# every ``cross_val_*`` inherit the requirement.
#
# The row is new at step 2 and not at step 1 for that reason: on the stack step
# 1 is scored under, storing a parameter somewhere else is a latent defect that
# nothing raises on, and charging for it would be charging for a bug the task
# never asked about. On 0.24.2 it is a crash.
#
# Two halves, because either alone is gameable. Structural -- is the parameter
# readable under its own name -- can be satisfied by a class that transforms
# the value on the way in; executed -- does ``clone`` give it back -- cannot
# run at all on a tree whose estimators will not construct. A parameter with no
# stand-in is reported as a *failure* rather than skipped, so the row cannot be
# cleared by making an estimator harder to build.
ESTIMATOR_CONTRACT = '''
import importlib
import inspect

MODULES = ["skutil", "skutil.base", "skutil.decomposition",
           "skutil.feature_selection", "skutil.grid_search", "skutil.metrics",
           "skutil.model_selection", "skutil.odr", "skutil.preprocessing",
           "skutil.testing", "skutil.utils"]

# Stand-ins for parameters with no default. Each is the cheapest object that
# lets `__init__` run to completion; nothing here is ever fit, so none of them
# has to be a valid value -- only a storable one. Keyed by parameter name
# because that is what the signature offers, and asserted exhaustive below: a
# parameter this dict does not cover makes the class report as unmeasurable
# rather than as clean, so a candidate cannot clear the row by renaming a
# parameter to something the stand-ins do not know.
STANDIN = {
    "estimator": None,
    "base_estimator": None,
    "param_grid": {},
    "param_distributions": {},
    # A real matrix, not None: ``QRDecomposition.__init__`` decomposes its
    # argument on the spot, so a placeholder that cannot be decomposed makes
    # the class report "__init__ raised" -- a verdict about the stand-in
    # rather than about the tree. Three rows by two columns, full rank, so
    # LINPACK has something to pivot.
    "X": np.asarray([[1.0, 0.0], [0.0, 1.0], [1.0, 1.0]]),
    "y": "target",
    "qr": None,
    "qraux": None,
    "pivot": None,
    "rank": None,
    "ratio": 0.2,
    "cv": 3,
    "func": None,
    "fun": None,
    "cols": None,
    "target_feature": None,
}


def exported(mod_name):
    """Every estimator class this module advertises, as (name, class).

    Read off `__all__` where there is one, so the set tracks what the package
    says it exports rather than what happens to be in its namespace. Classes
    are identified by bare name: the step is free to move `SelectivePCA` into a
    differently-named file, and a check keyed on `__module__` would read that
    reorganisation as a missing estimator.
    """
    try:
        mod = importlib.import_module(mod_name)
    except Exception as exc:                                      # noqa: BLE001
        return None, type(exc).__name__ + ": " + str(exc)[:160]
    from sklearn.base import BaseEstimator
    found = []
    declared = getattr(mod, "__all__", None)
    if declared is None:
        declared = [n for n in dir(mod) if not n.startswith("_")]
    for name in declared:
        obj = getattr(mod, name, None)
        if not inspect.isclass(obj):
            continue
        if not issubclass(obj, BaseEstimator):
            continue
        # sklearn's own base class is re-exported by skutil.base; it takes
        # *args/**kwargs and has no parameters to store.
        if obj.__module__.startswith("sklearn."):
            continue
        found.append((name, obj))
    return found, None


def parameters(cls):
    try:
        sig = inspect.signature(cls.__init__)
    except (TypeError, ValueError) as exc:
        return None, type(exc).__name__ + ": " + str(exc)[:80]
    out = []
    for pname, p in list(sig.parameters.items())[1:]:
        if p.kind in (p.VAR_POSITIONAL, p.VAR_KEYWORD):
            # `get_params` skips these too -- there is no attribute name for
            # a parameter that has no name.
            continue
        out.append((pname, p.default is not p.empty))
    return out, None


def inspect_class(name, cls):
    """One estimator's verdict: True iff it honours the contract."""
    params, err = parameters(cls)
    if params is None:
        return {"ok": False, "why": "signature unreadable: " + err}
    kwargs, missing = {}, []
    for pname, has_default in params:
        if has_default:
            continue
        if pname in STANDIN:
            kwargs[pname] = STANDIN[pname]
        else:
            missing.append(pname)
    if missing:
        # Not "clean". Unmeasurable, and reported as a failure, because the
        # alternative is a row a candidate clears by making its estimators
        # harder to construct.
        return {"ok": False,
                "why": "no stand-in for required parameter(s): "
                       + ",".join(sorted(missing))}
    try:
        est = cls(**kwargs)
    except Exception as exc:                                      # noqa: BLE001
        return {"ok": False,
                "why": "__init__ raised: " + type(exc).__name__ + ": "
                       + str(exc)[:120]}

    # Half one, structural: every parameter readable under its own name. This
    # is the half that is measurable on the stack the tree was handed over on,
    # where `get_params` still had its fallback and so did not raise.
    unstored = [p for p, _ in params if not hasattr(est, p)]
    # Reported only for parameters that are *also* unstored. A class that keeps
    # ``pivot`` as it was handed it and puts the decomposition's pivots in
    # ``pivot_`` is following scikit-learn's own convention -- constructor
    # argument under its own name, fitted result under a trailing underscore --
    # and flagging that would make the row punish the fix it asks for. The
    # combination that is wrong is the one this repository has: no ``y`` at
    # all, and a ``y_`` where it should have been.
    shadowed = [p for p in unstored
                if hasattr(est, p + "_") and not hasattr(type(est), p + "_")]

    # Half two, executed: what scikit-learn itself does with the promise. A
    # parameter can be present as an attribute and still not survive the round
    # trip -- transformed on the way in, or overwritten by a property.
    round_trip, why = True, []
    try:
        from sklearn.base import clone
        before = est.get_params(deep=False)
        after = clone(est).get_params(deep=False)
        for key in sorted(before):
            if before[key] is not after.get(key, object()):
                # Identity, not equality: this is the comparison `clone`'s own
                # sanity check uses, and an equal-but-not-identical value is
                # exactly what a constructor that copies its argument produces.
                if repr(before[key]) != repr(after.get(key)):
                    round_trip = False
                    why.append("%s: %r -> %r"
                               % (key, before[key], after.get(key)))
    except Exception as exc:                                      # noqa: BLE001
        round_trip = False
        why.append(type(exc).__name__ + ": " + str(exc)[:120])

    reasons = []
    if unstored:
        reasons.append("not stored under their own names: "
                       + ",".join(sorted(unstored)))
    if shadowed:
        reasons.append("stored with a trailing underscore: "
                       + ",".join(sorted(shadowed)))
    if not round_trip:
        reasons.append("clone() does not round-trip: " + "; ".join(why[:3]))
    return {"ok": not reasons, "why": "; ".join(reasons)}


out, seen = {}, {}
for mod_name in MODULES:
    found, err = exported(mod_name)
    if found is None:
        out["@module." + mod_name] = {"ok": False, "why": err}
        continue
    for name, cls in found:
        if name in seen:
            if seen[name] is not cls:
                out[name] = {"ok": False,
                             "why": "two different classes are exported "
                                    "under this name"}
            continue
        seen[name] = cls
        out[name] = inspect_class(name, cls)
emit(out)
'''


# Does every transformer expose scikit-learn's columnar-output API -- and does
# it tell the truth?
#
# ``set_output`` arrived in scikit-learn 1.2 as ``_SetOutputMixin.set_output``,
# guarded ``@available_if(_auto_wrap_is_configured)``. That guard is satisfied
# only when the instance carries a ``get_feature_names_out`` *and* declares
# ``transform`` among its auto-wrap keys -- which ``TransformerMixin`` sets for
# any subclass that defines ``transform``. So the one thing standing between a
# skutil transformer and a working ``set_output`` is ``get_feature_names_out``:
# without it ``getattr(est, "set_output", None)`` is ``None`` and the columnar
# API does not exist on the object. Wiring that method up is the visible work
# of the scikit-learn-1.x step, and this probe is where it is measured.
#
# Presence is not the measurement. ``_wrap_in_pandas_container`` assigns
# whatever ``get_feature_names_out`` returns straight onto the output frame --
# and swallows the exception if the call raises, falling back to the frame's own
# labels. A method that returns ``["x0", "x1", ...]``, or one that raises, would
# therefore leave a DataFrame that still *looks* labelled. So every row here is
# a real round trip on a real fit, and the labels are checked against ground
# truth the transformer cannot edit: the column labels its own default output
# already carries.
#
# Per transformer, all of the following must hold, or the row is False:
#
#   1. it constructs and fits on the fixture named for it in ``RECIPES``;
#   2. ``transform`` on the default path returns a labelled DataFrame -- those
#      labels, ``want``, are the ground truth for the rest of the row;
#   3. ``get_feature_names_out()`` returns a non-empty 1-d object array (this
#      alone kills a raising or ``NotImplemented`` stub) that equals ``want``
#      element for element and in order (this kills positional or otherwise
#      wrong labels);
#   4. ``get_feature_names_out(input_features=<fit columns>)`` agrees with the
#      no-argument call, so the method resolves names rather than ignoring the
#      argument or crashing on it;
#   5. after ``set_output(transform="pandas")``, ``transform`` returns a
#      ``pandas.DataFrame`` whose columns are exactly those names and whose
#      values are the default output's values -- a relabelling, not a different
#      computation;
#   6. after ``set_output(transform="default")``, ``transform`` returns the same
#      type, labels, dtypes and values it returned in (2). The columnar API has
#      to be *additive*: a ``set_output`` that leaks into the default path -- or
#      a ``get_feature_names_out`` grafted by rewriting what ``transform``
#      computes -- fails here even if every other half passed.
#
# The fixtures are the corpus's own builders, inlined: this child runs with the
# installed tree on ``sys.path`` and nothing else, so it cannot import the
# recording harness. ``RECIPES`` names, for each exported transformer, the
# cheapest constructor arguments and frame kind on which it actually fits. A
# transformer with no recipe is reported False with a reason rather than
# skipped, so growing the export list cannot silently shrink this gate.
#
# On every stack before 1.2 the API does not exist, so the probe emits *no*
# rows at all: the earlier steps on the ladder, scored under scikit-learn 0.20
# and 0.24, never see a ``set_output_supported.*`` check and this file stays a
# no-op there rather than inventing failures for an API the stack lacks.
SET_OUTPUT = '''
import importlib
import inspect

MODULES = ["skutil", "skutil.base", "skutil.decomposition",
           "skutil.feature_selection", "skutil.grid_search", "skutil.metrics",
           "skutil.model_selection", "skutil.odr", "skutil.preprocessing",
           "skutil.testing", "skutil.utils"]


def sklearn_minor():
    import sklearn
    parts = []
    for p in sklearn.__version__.split(".")[:2]:
        try:
            parts.append(int(p))
        except ValueError:
            parts.append(0)
    while len(parts) < 2:
        parts.append(0)
    return tuple(parts)


# Older stacks have no set_output at all: emit nothing so no check exists to
# score, then exit before importing names that only 1.2+ defines.
if sklearn_minor() < (1, 2):
    emit({})
    raise SystemExit(0)

from sklearn.base import BaseEstimator, TransformerMixin
from sklearn.preprocessing import LabelEncoder

LETTERS = list("abcdefghijklmnopqrstuvwxyz")


def _cols(p):
    return [LETTERS[i] for i in range(p)]


def _doubler(x):
    return x * 2


def fixture(kind):
    """A *fresh* frame per call: skutil mutates frames in place in several
    places, and the three transforms in one row have to see identical input.

    These are the behaviour corpus's own builders (``skutil_probe/data.py``),
    inlined because this child runs against the installed tree alone. Each is
    the frame the corpus already fits the matching transformer on, so a False
    row here means the columnar API is wrong, not that the fixture was.
    """
    if kind == "numeric":
        return pd.DataFrame(np.random.RandomState(7).randn(40, 4),
                            columns=_cols(4))
    if kind == "positive":
        a = np.abs(np.random.RandomState(13).randn(40, 4)) + 0.5
        return pd.DataFrame(a, columns=_cols(4))
    if kind == "nan":
        r = np.random.RandomState(19)
        a = r.randn(60, 5)
        a[r.rand(60, 5) < 0.2] = np.nan
        return pd.DataFrame(a, columns=_cols(5))
    if kind == "catnan":
        r = np.random.RandomState(71)
        f = pd.DataFrame({"a": r.randint(0, 3, 60).astype(float),
                          "b": r.randn(60), "c": r.randn(60)},
                         columns=["a", "b", "c"])
        f.loc[r.rand(60) < 0.25, "a"] = np.nan
        return f
    if kind == "category":
        r = np.random.RandomState(43)
        return pd.DataFrame({"a": [["red", "green", "blue"][i]
                                   for i in r.randint(0, 3, 60)],
                             "b": [["north", "south"][i]
                                   for i in r.randint(0, 2, 60)],
                             "c": r.randn(60)}, columns=["a", "b", "c"])
    if kind == "collinear":
        r = np.random.RandomState(23)
        base = r.randn(80)
        return pd.DataFrame({"a": base, "b": base.copy(),
                             "c": base * 2.0 + r.randn(80) * 1e-3,
                             "d": np.ones(80), "e": r.randn(80)},
                            columns=_cols(5))
    if kind == "combo":
        r = np.random.RandomState(29)
        a, b = r.randn(60), r.randn(60)
        return pd.DataFrame({"a": a, "b": b, "c": a + b, "d": r.randn(60)},
                            columns=_cols(4))
    if kind == "sparse":
        r = np.random.RandomState(31)
        cells = {}
        for i, frac in enumerate([0.0, 0.25, 0.5, 0.75, 1.0]):
            col = r.randn(50)
            col[:int(round(50 * frac))] = np.nan
            cells[LETTERS[i]] = col
        return pd.DataFrame(cells, columns=_cols(5))
    if kind == "nzv":
        r = np.random.RandomState(37)
        almost = np.zeros(100)
        almost[0] = 1.0
        return pd.DataFrame({"a": r.randn(100), "b": np.ones(100),
                             "c": almost, "d": r.randn(100) * 1e-9},
                            columns=_cols(4))
    raise KeyError(kind)


# What each exported transformer needs in order to be *fitted*, not merely
# constructed: (constructor kwargs, fixture kind). Arguments are chosen to walk
# the interesting branch of each get_feature_names_out rather than the easy one
# -- the decomposers keep an untouched column beside their components, the
# retainer reorders, the droppers actually drop -- and to avoid inputs whose
# behaviour a must-flip changes (YeoJohnson gets plain normal data, not the
# zero-variance column its flipped case uses).
RECIPES = {
    "BaggedCategoricalImputer": ({"cols": ["a"], "random_state": 42,
                                  "n_estimators": 3}, "catnan"),
    "BaggedImputer": ({"random_state": 42, "n_estimators": 3}, "nan"),
    "BoxCoxTransformer": ({}, "positive"),
    "FeatureDropper": ({"cols": ["a", "c"]}, "numeric"),
    "FeatureRetainer": ({"cols": ["c", "a"]}, "numeric"),
    "FunctionMapper": ({"fun": _doubler}, "numeric"),
    "InteractionTermTransformer": ({}, "numeric"),
    "LinearCombinationFilterer": ({}, "combo"),
    "MulticollinearityFilterer": ({}, "collinear"),
    "NearZeroVarianceFilterer": ({}, "nzv"),
    "OneHotCategoricalEncoder": ({}, "category"),
    "SelectiveImputer": ({}, "nan"),
    "SelectivePCA": ({"cols": ["a", "b", "c"], "n_components": 2}, "numeric"),
    "SelectiveScaler": ({}, "numeric"),
    "SelectiveTruncatedSVD": ({"cols": ["a", "b", "c"], "n_components": 2},
                              "numeric"),
    "SparseFeatureDropper": ({}, "sparse"),
    "SpatialSignTransformer": ({}, "numeric"),
    "YeoJohnsonTransformer": ({}, "numeric"),
}


def _why(exc):
    return "%s: %s" % (type(exc).__name__, str(exc)[:160])


def _labels(frame):
    return [str(c) for c in frame.columns]


def same_values(a, b):
    """Cell-for-cell equality with NaN==NaN, without assuming a dtype.

    Only the *labels* are allowed to differ between the default output and the
    pandas-container output; the numbers have to be the same numbers.
    """
    left = np.asarray(a.values, dtype=object)
    right = np.asarray(b.values, dtype=object)
    if left.shape != right.shape:
        return False
    for x, y in zip(left.ravel().tolist(), right.ravel().tolist()):
        try:
            fx, fy = float(x), float(y)
        except (TypeError, ValueError):
            if x != y:
                return False
            continue
        if fx != fy and not (fx != fx and fy != fy):
            return False
    return True


def supported(name, cls):
    """One transformer's verdict as ``{"ok": bool, "why": str}``.

    The six halves listed in the comment above this script, in order. The first
    failure wins and is reported, so ``detail["set_output"]`` says *what* is
    wrong rather than only that something is.
    """
    recipe = RECIPES.get(name)
    if recipe is None:
        return {"ok": False,
                "why": "no fit recipe for this exported transformer"}
    kwargs, kind = recipe

    # (1) construct and fit.
    try:
        est = cls(**kwargs)
    except Exception as exc:                                     # noqa: BLE001
        return {"ok": False, "why": "__init__ raised -- " + _why(exc)}
    if not callable(getattr(est, "get_feature_names_out", None)):
        return {"ok": False, "why": "no get_feature_names_out on the instance"}
    try:
        est.fit(fixture(kind))
    except Exception as exc:                                     # noqa: BLE001
        return {"ok": False, "why": "fit raised -- " + _why(exc)}

    # (2) ground truth: what the default path calls its own output columns.
    try:
        base = est.transform(fixture(kind))
    except Exception as exc:                                     # noqa: BLE001
        return {"ok": False, "why": "default transform raised -- " + _why(exc)}
    if not isinstance(base, pd.DataFrame):
        return {"ok": False,
                "why": "default transform returned %s, which carries no "
                       "column labels to check against" % type(base).__name__}
    want = _labels(base)
    if not want:
        return {"ok": False, "why": "default transform produced no columns"}

    # (3) the method names exactly those columns, in that order.
    try:
        got = est.get_feature_names_out()
    except Exception as exc:                                     # noqa: BLE001
        return {"ok": False,
                "why": "get_feature_names_out raised -- " + _why(exc)}
    if not isinstance(got, np.ndarray) or got.ndim != 1 or got.dtype != object:
        return {"ok": False,
                "why": "get_feature_names_out did not return a 1-d object "
                       "array (got %s)" % type(got).__name__}
    names = [str(c) for c in got.tolist()]
    if not names:
        return {"ok": False, "why": "get_feature_names_out returned nothing"}
    if names != want:
        return {"ok": False,
                "why": "get_feature_names_out does not name the columns "
                       "transform produces: %s != %s" % (names[:8], want[:8])}

    # (4) ...and resolves input_features instead of ignoring or rejecting it.
    try:
        passed = est.get_feature_names_out(list(fixture(kind).columns))
    except Exception as exc:                                     # noqa: BLE001
        return {"ok": False,
                "why": "get_feature_names_out(input_features) raised -- "
                       + _why(exc)}
    if [str(c) for c in np.asarray(passed).ravel().tolist()] != want:
        return {"ok": False,
                "why": "get_feature_names_out(input_features) disagrees with "
                       "the no-argument call"}

    # (5) the pandas container is the default output, relabelled.
    so = getattr(est, "set_output", None)
    if not callable(so):
        return {"ok": False,
                "why": "set_output did not resolve past its "
                       "available_if guard"}
    try:
        so(transform="pandas")
    except Exception as exc:                                     # noqa: BLE001
        return {"ok": False,
                "why": 'set_output(transform="pandas") raised -- ' + _why(exc)}
    try:
        wrapped = est.transform(fixture(kind))
    except Exception as exc:                                     # noqa: BLE001
        return {"ok": False,
                "why": "transform under pandas output raised -- " + _why(exc)}
    if not isinstance(wrapped, pd.DataFrame):
        return {"ok": False,
                "why": "transform under pandas output returned %s"
                       % type(wrapped).__name__}
    if _labels(wrapped) != names:
        return {"ok": False,
                "why": "pandas output is not labelled with the names "
                       "get_feature_names_out returned"}
    if not same_values(wrapped, base):
        return {"ok": False,
                "why": "pandas output is not the default output relabelled -- "
                       "the values changed"}

    # (6) additive: turning the container off restores (2) exactly.
    try:
        so(transform="default")
        after = est.transform(fixture(kind))
    except Exception as exc:                                     # noqa: BLE001
        return {"ok": False,
                "why": 'set_output(transform="default") path raised -- '
                       + _why(exc)}
    if type(after) is not type(base):
        return {"ok": False,
                "why": "default output type changed after set_output (%s)"
                       % type(after).__name__}
    if _labels(after) != want:
        return {"ok": False, "why": "default output labels changed after "
                                    "set_output"}
    if [str(d) for d in after.dtypes] != [str(d) for d in base.dtypes]:
        return {"ok": False, "why": "default output dtypes changed after "
                                    "set_output"}
    if not same_values(after, base):
        return {"ok": False, "why": "default output values changed after "
                                    "set_output"}
    return {"ok": True, "why": "%d columns round-tripped" % len(names)}


out, seen = {}, set()
for mod_name in MODULES:
    try:
        mod = importlib.import_module(mod_name)
    except Exception as exc:                                     # noqa: BLE001
        out["@module." + mod_name] = {"ok": False, "why": _why(exc)}
        continue
    declared = getattr(mod, "__all__", None)
    if declared is None:
        declared = [n for n in dir(mod) if not n.startswith("_")]
    for name in declared:
        obj = getattr(mod, name, None)
        if not inspect.isclass(obj):
            continue
        if not issubclass(obj, BaseEstimator):
            continue
        # Transformers only: ``set_output`` is a transformer-output API, and the
        # search-CV meta-estimators skutil exports are not transformers.
        if not issubclass(obj, TransformerMixin):
            continue
        # LabelEncoder subclasses transform y (labels), not X features; the
        # set_output/get_feature_names_out API is for X transformers only.
        # This is the one documented exclusion, and it is stated in the
        # manifest detail for this row as well as here.
        if issubclass(obj, LabelEncoder):
            continue
        if obj.__module__.startswith("sklearn."):
            continue
        if name in seen:
            continue
        seen.add(name)
        out[name] = supported(name, obj)
emit(out)
'''


# One workload over every entry point in the package that draws. The global
# seed is set by this script, which is what the baseline needs to agree with
# itself; a tree that routes its draws through a caller-supplied generator and
# defaults to the global stream agrees with itself too.
SEEDED_WORKLOAD = '''
import skutil                                          # noqa: F401
from skutil.utils import shuffle_dataframe
from skutil.preprocessing import (OversamplingClassBalancer,
                                  UndersamplingClassBalancer,
                                  SMOTEClassBalancer)

h = hashlib.sha256()
np.random.seed(2024)
shuffled = shuffle_dataframe(frame())
h.update(np.ascontiguousarray(
    np.asarray(shuffled.index).astype(np.int64)).tobytes())
digest_frame(h, shuffled)
for cls, kw in ((OversamplingClassBalancer, {}),
                (UndersamplingClassBalancer, {}),
                (SMOTEClassBalancer, {"k": 3})):
    kw = dict(kw)
    kw.update({"y": "target", "shuffle": True})
    digest_frame(h, cls(**kw).balance(frame()))
emit({"digest": h.hexdigest()})
'''

FIGURE_WORKLOAD = '''
import io
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import skutil                                          # noqa: F401
from skutil.utils import corr_plot

plt.close("all")
np.random.seed(5)
X = pd.DataFrame(np.random.RandomState(5).rand(40, 4), columns=list("abcd"))
returned = corr_plot(X)
# Step 7 turns these helpers into functions that return a figure instead of
# leaving one on the global stack. Accept either, so this check keeps measuring
# reproducibility rather than quietly becoming a check about return types.
fig = returned if hasattr(returned, "savefig") else plt.gcf()
buf = io.BytesIO()
fig.savefig(buf, format="png")
plt.close("all")
emit({"digest": hashlib.sha256(buf.getvalue()).hexdigest()})
'''

# The enumerated stochastic surface. Every name here draws, on the baseline,
# from the process-global legacy RNG.
RANDOM_STATE = '''
import inspect
import skutil                                          # noqa: F401
from skutil.utils import shuffle_dataframe
from skutil.preprocessing import (OversamplingClassBalancer,
                                  UndersamplingClassBalancer,
                                  SMOTEClassBalancer)

def accepts(fn, name="random_state"):
    """Named in the signature, not merely swallowed by ``**kwargs``.

    A ``**kwargs`` that quietly absorbs ``random_state`` and ignores it would
    pass a try/except probe and fail the caller, so only a declared parameter
    counts here. The behavioural half of the same question is `stable` and
    `isolated` below, which ``**kwargs`` cannot fake.
    """
    try:
        spec = inspect.getfullargspec(fn)
    except TypeError:
        return False
    return name in (spec.args or []) or name in (spec.kwonlyargs or [])

def state():
    s = np.random.get_state()
    return (s[0], s[1].tobytes(), s[2], s[3], float(s[4]))

def digest_of(out):
    h = hashlib.sha256()
    if hasattr(out, "columns"):
        digest_frame(h, out)
        h.update(np.ascontiguousarray(
            np.asarray(out.index).astype(np.int64)).tobytes())
    else:
        h.update(np.ascontiguousarray(np.asarray(out)).tobytes())
    return h.hexdigest()

def exercise(call):
    """Same seed twice, and the global stream where we left it."""
    np.random.seed(777)
    anchor = state()
    try:
        first = digest_of(call())
    except TypeError:
        return {"accepts": False, "stable": False, "isolated": False}
    except Exception as exc:
        return {"accepts": True, "stable": False, "isolated": False,
                "raised": type(exc).__name__ + ": " + str(exc)[:160]}
    moved_once = state()
    second = digest_of(call())
    return {"accepts": True,
            "stable": bool(first == second),
            "isolated": bool(anchor == moved_once)}

def exercise_instance(call):
    """Isolation when the caller passes a ``RandomState`` of their own.

    The other spelling the property names, and the one ``check_random_state``
    treats differently: an ``int`` becomes a fresh generator, an instance is
    handed straight back, so this is the arm where a careless implementation
    reaches for the global stream instead of the object it was given.

    Isolation only. A ``RandomState`` the caller owns advances as it is drawn
    from, so two calls sharing one legitimately differ; repeatability is the
    ``int``-seed arm above, and asking for it here would fail a correct
    implementation.
    """
    rs = np.random.RandomState(13)
    np.random.seed(777)
    anchor = state()
    try:
        call(rs)
    except TypeError:
        return {"accepts": False, "isolated": False}
    except Exception as exc:
        return {"accepts": True, "isolated": False,
                "raised": type(exc).__name__ + ": " + str(exc)[:160]}
    return {"accepts": True, "isolated": bool(anchor == state())}

def round_trip(cls, kw):
    """``get_params``/``clone`` for ``random_state``, and nothing else.

    scikit-learn's convention is not decoration: ``clone`` rebuilds an
    estimator out of ``get_params()`` alone, so a balancer that takes
    ``random_state`` and stores it under some other name comes back from
    ``clone`` unseeded, and every grid search or cross-validation over one
    quietly stops being reproducible. ``accepts`` above reads the signature;
    this reads what the object does with what the signature promised, which is
    the half that cannot be faked by declaring a parameter and dropping it.

    Only ``random_state``, and only here. These balancers have stored their
    ``y`` as ``self.y_`` since the tree was written, so ``get_params()["y"]``
    has always been ``None`` and a cloned balancer has always lost its target
    column. Step 1 is not the step that asks for that, and checking the whole
    round-trip *in this probe* would charge step 1 for a bug it inherited.

    ``ESTIMATOR_CONTRACT`` above is where the rest of the promise is checked,
    from step 2 on, across every exported estimator rather than these three --
    including the ``self.y_`` those three still have. The two probes overlap on
    purpose: this one is a carried row every step keeps, that one is a new row
    that starts at the step whose scikit-learn turns the defect into an
    exception.
    """
    try:
        from sklearn.base import clone
        est = cls(**kw)
        before = est.get_params().get("random_state", "<absent>")
        after = clone(est).get_params().get("random_state", "<absent>")
        return {"params": before == kw["random_state"],
                "clone": after == kw["random_state"]}
    except Exception as exc:
        return {"params": False, "clone": False,
                "raised": type(exc).__name__ + ": " + str(exc)[:160]}

out = {}
out["shuffle_dataframe.signature"] = accepts(shuffle_dataframe)
out["shuffle_dataframe"] = exercise(
    lambda: shuffle_dataframe(frame(), random_state=13))
out["shuffle_dataframe.instance"] = exercise_instance(
    lambda rs: shuffle_dataframe(frame(), random_state=rs))
for cls, label, kw in ((OversamplingClassBalancer, "oversampling", {}),
                       (UndersamplingClassBalancer, "undersampling", {}),
                       (SMOTEClassBalancer, "smote", {"k": 3})):
    out[label + ".signature"] = accepts(cls.__init__)
    kw = dict(kw)
    kw.update({"y": "target", "shuffle": True, "random_state": 13})
    out[label] = exercise(
        lambda cls=cls, kw=kw: cls(**kw).balance(frame()))
    out[label + ".instance"] = exercise_instance(
        lambda rs, cls=cls, kw=kw:
        cls(**dict(kw, random_state=rs)).balance(frame()))
    out[label + ".clone"] = round_trip(cls, kw)
emit(out)
'''


def run(tree="/app", python=None, timeout=900, cwd=None):
    """Every reproducibility check, as plain booleans plus their evidence."""
    python = python or sys.executable
    cwd = cwd or tempfile.gettempdir()
    detail = {}
    checks = {}

    def child(script, **kw):
        return _run(script, cwd, python, timeout, tree=tree, **kw)

    got = child(RNG_STATE)
    detail["rng_state"] = got
    checks["import_preserves_rng"] = bool(got.get("import_ok"))
    checks["module_fixtures_preserve_rng"] = bool(got.get("hooks_ok"))

    first = child(SEEDED_WORKLOAD)
    second = child(SEEDED_WORKLOAD)
    detail["seeded_repeatable"] = {"first": first, "second": second}
    checks["seeded_repeatable"] = bool(
        first.get("digest") and first.get("digest") == second.get("digest"))

    first = child(FIGURE_WORKLOAD)
    second = child(FIGURE_WORKLOAD)
    detail["figures_repeatable"] = {"first": first, "second": second}
    checks["figures_repeatable"] = bool(
        first.get("digest") and first.get("digest") == second.get("digest"))

    got = child(RANDOM_STATE)
    detail["random_state"] = got
    for name in ("shuffle_dataframe", "oversampling", "undersampling", "smote"):
        row = got.get(name) or {}
        sig = bool(got.get(name + ".signature"))
        checks["random_state_accepted." + name] = sig and bool(row.get("accepts"))
        checks["random_state_stable." + name] = bool(row.get("stable"))
        checks["random_state_isolated." + name] = bool(row.get("isolated"))
        inst = got.get(name + ".instance") or {}
        checks["random_state_isolated_instance." + name] = bool(
            inst.get("isolated"))
    # The balancers only: ``shuffle_dataframe`` is a function, and ``clone``
    # has nothing to say about one.
    for name in ("oversampling", "undersampling", "smote"):
        row = got.get(name + ".clone") or {}
        checks["random_state_round_trips." + name] = bool(
            row.get("params") and row.get("clone"))

    api = child(PUBLIC_API)
    detail["public_api"] = api

    # One boolean per exported estimator class, keyed by bare class name so a
    # step that moves a class between modules does not read as a missing one.
    # A probe that did not run at all produces no keys, and `score.observe`
    # counts a named check it never saw as failed -- which is the right answer
    # for a tree whose estimators could not be enumerated.
    got = child(ESTIMATOR_CONTRACT)
    detail["estimator_contract"] = got
    for name in sorted(got):
        if name.startswith("@"):
            checks["estimator_contract." + name] = False
            continue
        checks["estimator_contract." + name] = bool((got[name] or {}).get("ok"))

    # One boolean per exported transformer, keyed by bare class name. The probe
    # emits nothing on pre-1.2 stacks, so this loop adds no keys there and the
    # earlier steps' check sets are byte-for-byte unchanged. A transformer the
    # probe could not enumerate produces no key and `score.observe` counts a
    # named check it never saw as failed -- the right answer for a tree whose
    # transformers would not import. Each row is a `{"ok": ..., "why": ...}`
    # verdict, read the same way the estimator contract reads its own: through
    # `.get("ok")`, so a payload that is merely truthy cannot pass, and the
    # reason for a False lands in `detail` where the report can quote it.
    got = child(SET_OUTPUT)
    detail["set_output"] = got
    for name in sorted(got):
        if name.startswith("@"):
            checks["set_output_supported." + name] = False
            continue
        checks["set_output_supported." + name] = bool((got[name] or {}).get("ok"))


    got = child(IMPORT_WARNINGS, argv=("-X", "dev"))
    detail["self_warnings"] = got
    if "total" in got:
        self_warnings = {"total": got["total"],
                         "by_category": got.get("by_category", {}),
                         "items": got.get("items", [])}
    else:
        # A probe that could not run is not a tree with no warnings. Report a
        # count no limit can accept.
        self_warnings = {"total": 9999, "by_category": {"ProbeFailed": 9999},
                         "items": [], "error": got}

    return {"checks": checks, "self_warnings": self_warnings,
            "public_api": api, "detail": detail}


def main(argv=None):
    import argparse
    p = argparse.ArgumentParser(prog="skutil_gates.repro")
    p.add_argument("--tree", default="/app")
    p.add_argument("--cwd", help="where the children run; never the tree")
    p.add_argument("--out")
    args = p.parse_args(argv)
    report = run(args.tree, cwd=args.cwd)
    if args.out:
        with open(args.out, "w") as fh:
            json.dump(report, fh, indent=1, sort_keys=True)
    for name in sorted(report["checks"]):
        print("%-44s %s" % (name, "ok" if report["checks"][name] else "FAIL"))
    warn = report["self_warnings"]
    print("%-44s %d %s" % ("self_warnings", warn["total"],
                           json.dumps(warn["by_category"], sort_keys=True)))
    for item in warn["items"][:12]:
        print("    %s  %s  %s" % tuple(item))
    return 0


if __name__ == "__main__":
    sys.exit(main())
