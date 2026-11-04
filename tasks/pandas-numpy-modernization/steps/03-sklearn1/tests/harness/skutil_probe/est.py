"""Shared shape for the transformer/selector cases.

Every skutil transformer is a ``BaseSkutil`` + ``TransformerMixin``, so one
observation shape serves them all: the fitted state (the trailing-underscore
attributes sklearn's convention reserves for it), the transformed output, and
``get_params``. Recording all three together means a migration that keeps the
output but loses an attribute still shows up.
"""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd


def fitted_state(est):
    """The sklearn-convention fitted attributes, canonicalizable."""
    out = {}
    for k in sorted(vars(est)):
        if k.endswith("_") and not k.startswith("_"):
            out[k] = getattr(est, k)
    return out


def observe(make_est, make_X, y=None, call="fit_transform", unordered=()):
    """Build, fit and transform; return the recordable triple.

    ``make_est`` and ``make_X`` are callables so that each case gets its own
    estimator and its own frame -- several skutil transformers mutate the frame
    they are handed.

    ``unordered`` names fitted attributes whose *order* is not a contract
    because skutil built them out of a ``set``. Set iteration over strings
    follows ``hash()``, which CPython reseeds per process and rewrote outright
    between 3.10 and 3.11, so those attributes are sorted before recording.
    Membership still has to match exactly; only the order is forgiven.
    """
    def run():
        est = make_est()
        X = make_X()
        params = est.get_params()
        if call == "fit_transform":
            out = est.fit_transform(X, y)
        elif call == "fit_then_transform":
            est.fit(X, y)
            out = est.transform(make_X())
        elif call == "fit":
            est.fit(X, y)
            out = None
        else:
            raise KeyError(call)
        state = fitted_state(est)
        for key in unordered:
            if key in state:
                state[key] = sorted(state[key], key=repr)
        return {"params": params, "state": state, "out": out}
    return run


def frame_structure(out, before=None):
    """Version-independent description of a transformed frame.

    Used where the exact cell values come out of a scikit-learn ensemble: tree
    splitters are not part of the pandas/NumPy contract and their output moves
    between sklearn majors for reasons this migration is not about. The
    structure -- shape, names, dtypes, which cells stopped being missing, and
    whether the observed cells were left alone -- is.
    """
    if isinstance(out, np.ndarray):
        arr = out
        node = {"kind": "ndarray", "shape": list(arr.shape),
                "dtype": str(arr.dtype)}
    else:
        arr = np.asarray(out.select_dtypes(include=[np.number]).values,
                         dtype=np.float64)
        node = {"kind": "dataframe", "shape": list(out.shape),
                "columns": list(out.columns),
                "dtypes": [str(d) for d in out.dtypes],
                "index": list(out.index)}
    node["n_nan"] = int(np.count_nonzero(np.isnan(arr.astype(np.float64))))
    node["finite_count"] = int(np.count_nonzero(np.isfinite(arr.astype(np.float64))))
    if before is not None:
        b = np.asarray(before.select_dtypes(include=[np.number]).values,
                       dtype=np.float64)
        if b.shape == arr.shape:
            was = np.isnan(b)
            node["n_was_missing"] = int(np.count_nonzero(was))
            kept = b[~was]
            got = arr[~was]
            node["untouched_cells"] = bool(np.allclose(kept, got, equal_nan=True))
    return node


def observe_structural(make_est, make_X, y=None):
    """Fit/transform, but record the structure rather than the cell values."""
    def run():
        est = make_est()
        X = make_X()
        before = make_X()
        out = est.fit_transform(X, y)
        return {"params": est.get_params(),
                "state_keys": sorted(fitted_state(est)),
                "structure": frame_structure(out, before)}
    return run


# Constructor parameters a step is *required* to add, and which are therefore
# not part of what ``get_params`` has to keep saying.
#
# Step 1's gates ask for a ``random_state`` on every balancer: the
# ``random_state_accepted.*`` checks fail until the name is in the signature.
# ``get_params`` reads the signature, so recording its output verbatim would
# turn the step's own required change into a behaviour regression, and no
# submission could score both families at once. Every parameter the entry tree
# already had is still compared exactly -- a step that drops ``ratio`` or
# renames ``as_df`` is caught by the same dict -- and nothing is hidden, because
# whether the parameter is actually *honoured* is measured directly by
# ``skutil_gates.repro``.
PARAMS_EXEMPT = frozenset(["random_state"])


def _params(est):
    return dict((k, v) for k, v in est.get_params().items()
                if k not in PARAMS_EXEMPT)


def _target_name(bal):
    """The response column a balancer was built around.

    The entry tree stores the constructor's ``y`` under ``y_`` -- the spelling
    scikit-learn reserves for fitted attributes -- and a tree migrated for
    scikit-learn >= 0.22 has to expose it under ``y`` as well, because
    ``BaseEstimator.get_params`` stopped reading its parameters with a default.
    Either spelling names the same column, and this probe is not the place to
    decide which of the two a submission keeps.
    """
    for attr in ("y", "y_"):
        name = getattr(bal, attr, None)
        if name is not None:
            return name
    raise AttributeError("%s exposes neither .y nor .y_"
                         % type(bal).__name__)


def observe_balance(make_bal, make_X, invariants_only=False):
    """Balancers draw their row order from whichever generator they were given:
    on the entry tree that is the process-global RNG, and the number of draws is
    an implementation detail of whatever NumPy is installed. For those, only the
    version-independent invariants are recorded."""
    def run():
        bal = make_bal()
        X = make_X()
        out = bal.balance(X)
        if not invariants_only:
            return {"params": _params(bal), "out": out}
        return {
            "params": _params(bal),
            "shape": list(out.shape),
            "columns": list(out.columns),
            "dtypes": [str(d) for d in out.dtypes],
            "class_counts": _counts(out, _target_name(bal)),
            "index_is_unique": bool(pd.Index(out.index).is_unique),
            "values_sorted": _sorted_digest(out, _target_name(bal)),
        }
    return run


def search_summary(search, detail=None):
    """What a fitted grid search says that is not a wall-clock or a repr.

    ``cv_results_`` carries ``mean_fit_time``/``mean_score_time`` -- timings, so
    unrecordable -- and estimator ``repr`` strings, whose formatting sklearn has
    rewritten more than once. Everything else about the search is contract: the
    key set, the fold count, the parameter candidates, and, for the closed-form
    estimators these cases use, the scores themselves.
    """
    res = search.cv_results_
    keys = sorted(res)
    scored = dict((k, list(np.asarray(res[k], dtype=np.float64)))
                  for k in keys
                  if k.startswith(("mean_test", "std_test", "split",
                                   "rank_test")) and "time" not in k)
    out = {
        "cv_results_keys": keys,
        "scores": scored,
        "candidates": [dict((str(a), b) for a, b in sorted(p.items()))
                       for p in res["params"]],
        "best_params_": dict((str(a), b)
                             for a, b in sorted(search.best_params_.items())),
        "best_score_": float(search.best_score_),
        "best_index_": int(getattr(search, "best_index_", -1)),
        "n_splits_": int(getattr(search, "n_splits_", -1)),
    }
    if detail is not None:
        df, drops = detail
        out["detail_columns"] = list(df.columns)
        out["detail_shape"] = list(df.shape)
        out["detail_drops"] = list(drops)
        out["detail_scores"] = [float(v) for v in df["mean_test_score"].values]
    return out


def sk17_summary(search):
    """The vendored sklearn-0.17 search reports ``grid_scores_`` instead."""
    return {
        "grid_scores_": [{"parameters": dict((str(a), b) for a, b
                                             in sorted(g.parameters.items())),
                          "mean": float(g.mean_validation_score),
                          "cv": [float(v) for v in g.cv_validation_scores]}
                         for g in search.grid_scores_],
        "best_params_": dict((str(a), b)
                             for a, b in sorted(search.best_params_.items())),
        "best_score_": float(search.best_score_),
        "repr_first": repr(search.grid_scores_[0]),
    }


def _counts(out, tgt):
    vc = out[tgt].value_counts()
    return dict((str(k), int(v)) for k, v in vc.items())


def _sorted_digest(out, tgt):
    """Order-independent digest of the numeric payload: a shuffle permutes rows
    but must not invent or lose them."""
    num = out.select_dtypes(include=[np.number])
    vals = np.sort(np.asarray(num.values, dtype=np.float64).ravel())
    return {"n": int(vals.size),
            "sum": float(np.nansum(vals)),
            "head": [float(v) for v in vals[:25]],
            "tail": [float(v) for v in vals[-25:]]}
