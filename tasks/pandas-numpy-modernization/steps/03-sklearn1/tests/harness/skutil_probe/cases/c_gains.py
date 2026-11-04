"""skutil.metrics._act: the actuarial gains report.

``GainsStatisticalReport`` is pure pandas arithmetic -- ``qcut`` into rank
groups, ``groupby.agg(np.sum)``, ``cumsum``, ``sort_values``, and a weighted
``np.average`` -- so every number it produces is exactly recordable. It is also
where the repo leans hardest on pandas idioms that moved: passing ``np.sum`` to
``agg``, ``DataFrame.from_dict``, and a ``qcut`` that raises on duplicate bin
edges rather than falling back.
"""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data

ATOL = dict(rtol=1e-9, atol=1e-12)


def _bind(fn, *args, **kwargs):
    def run():
        return fn(*args, **kwargs)
    return run


def _book(n=200, seed=137, ties=0):
    """Predictions, exposure, loss and premium for one fold.

    ``ties`` rounds the predictions onto a coarse lattice so that ``qcut``'s
    bin edges collide -- the error path the report exists to survive.
    """
    r = data.rs(seed)
    pred = r.gamma(2.0, 1.0, size=n)
    if ties:
        pred = np.round(pred * ties) / float(ties)
    expo = r.uniform(0.5, 1.5, size=n)
    loss = r.gamma(1.5, 2.0, size=n) * expo
    prem = expo * (1.0 + 0.3 * pred)
    return pred, expo, loss, prem


def _fit_one(report, **kw):
    pred, expo, loss, prem = _book(**kw)
    report.fit_fold(pred, expo, loss, prem)
    return report


def _report_state(report):
    return {"stats": dict((k, list(v)) for k, v in report.stats.items()),
            "sample_sizes": list(report.sample_sizes),
            "frame": report.as_data_frame()}


def _single_fold_cases():
    from skutil.metrics import GainsStatisticalReport

    def one(**kw):
        def run():
            rep = GainsStatisticalReport(**kw)
            return _report_state(_fit_one(rep))
        return run

    case("gains.report.default", tags=(BEHAVIOR,), **ATOL)(one())
    case("gains.report.score_by_gini", tags=(BEHAVIOR,), **ATOL)(
        one(score_by="gini"))
    case("gains.report.n_groups_5", tags=(BEHAVIOR,), **ATOL)(one(n_groups=5))
    case("gains.report.not_iid", tags=(BEHAVIOR,), **ATOL)(one(iid=False))

    def no_prem():
        """``prem`` defaults to a copy of ``expo``."""
        rep = GainsStatisticalReport()
        pred, expo, loss, _ = _book()
        rep.fit_fold(pred, expo, loss)
        return _report_state(rep)

    case("gains.report.prem_defaults_to_expo", tags=(BEHAVIOR,), **ATOL)(no_prem)

    def pandas_inputs():
        rep = GainsStatisticalReport()
        pred, expo, loss, prem = _book()
        rep.fit_fold(pd.Series(pred), pd.Series(expo), pd.Series(loss),
                     pd.Series(prem))
        return _report_state(rep)

    case("gains.report.series_inputs", tags=(BEHAVIOR,), **ATOL)(pandas_inputs)

    def list_inputs():
        rep = GainsStatisticalReport()
        pred, expo, loss, prem = _book(n=120, seed=139)
        rep.fit_fold(list(pred), list(expo), list(loss), list(prem))
        return _report_state(rep)

    case("gains.report.list_inputs", tags=(BEHAVIOR, ADVERSARIAL), **ATOL)(
        list_inputs)

    def multiple_folds():
        rep = GainsStatisticalReport()
        for s in (137, 139, 149):
            _fit_one(rep, seed=s)
        return _report_state(rep)

    case("gains.report.three_folds", tags=(BEHAVIOR,), **ATOL)(multiple_folds)

    def scoring(store):
        def run():
            rep = GainsStatisticalReport()
            pred, expo, loss, prem = _book()
            fn = rep.score if store else rep.score_no_store
            scr = fn(None, pred, expo=expo, loss=loss, prem=prem)
            return {"score": scr, "state": _report_state(rep)}
        return run

    # ``score`` stores, ``score_no_store`` does not -- but both append a sample
    # size, which is the asymmetry worth pinning
    case("gains.report.score_stores", tags=(BEHAVIOR,), **ATOL)(scoring(True))
    case("gains.report.score_no_store", tags=(BEHAVIOR, ADVERSARIAL), **ATOL)(
        scoring(False))

    def score_after_no_store():
        rep = GainsStatisticalReport(score_by="gini")
        pred, expo, loss, prem = _book()
        rep.fit_fold(pred, expo, loss, prem)
        return {"score": rep.score_no_store(None, pred, expo=expo, loss=loss,
                                            prem=prem),
                "state": _report_state(rep)}

    case("gains.report.score_no_store_after_fit", tags=(BEHAVIOR,), **ATOL)(
        score_after_no_store)


_single_fold_cases()


def _cv_cases():
    from skutil.metrics import GainsStatisticalReport

    def folded(n_folds, n_iter, iid=True, fits=None):
        def run():
            rep = GainsStatisticalReport(n_folds=n_folds, n_iter=n_iter, iid=iid)
            k = n_folds * n_iter if fits is None else fits
            for i in range(k):
                _fit_one(rep, seed=137 + 2 * i, n=100 + 10 * i)
            return _report_state(rep)
        return run

    case("gains.report.cv_2x3", tags=(BEHAVIOR,), **ATOL)(folded(3, 2))
    case("gains.report.cv_not_iid", tags=(BEHAVIOR,), **ATOL)(
        folded(3, 2, iid=False))
    case("gains.report.cv_single_iter", tags=(BEHAVIOR,), **ATOL)(folded(5, 1))
    adversarial("gains.report.cv_indivisible")(folded(3, 2, fits=5))
    adversarial("gains.report.cv_no_fits")(folded(3, 2, fits=0))


_cv_cases()


def _error_path_cases():
    from skutil.metrics import GainsStatisticalReport

    def tied(behavior, **kw):
        def run():
            rep = GainsStatisticalReport(error_behavior=behavior, **kw)
            pred, expo, loss, prem = _book(n=200, ties=1)
            rep.fit_fold(pred, expo, loss, prem)
            return _report_state(rep)
        return run

    # duplicate qcut edges: warn (default) records error_score, raise propagates,
    # ignore swallows it silently
    case("gains.report.duplicate_edges_warn", tags=(BEHAVIOR, ADVERSARIAL),
         **ATOL)(tied("warn"))
    case("gains.report.duplicate_edges_ignore", tags=(BEHAVIOR, ADVERSARIAL),
         **ATOL)(tied("ignore"))
    adversarial("gains.report.duplicate_edges_raise")(tied("raise"))
    case("gains.report.duplicate_edges_error_score", tags=(BEHAVIOR, ADVERSARIAL),
         **ATOL)(tied("warn", error_score=-1.0))
    adversarial("gains.report.bad_error_behavior")(tied("explode"))

    adversarial("gains.report.bad_score_by")(
        _bind(GainsStatisticalReport, score_by="auc"))
    adversarial("gains.report.folds_without_iter")(
        _bind(GainsStatisticalReport, n_folds=3))

    def constant_pred():
        rep = GainsStatisticalReport()
        n = 100
        rep.fit_fold(np.ones(n), np.ones(n), np.arange(1.0, n + 1.0))
        return _report_state(rep)

    case("gains.report.constant_predictions", tags=(BEHAVIOR, ADVERSARIAL),
         **ATOL)(constant_pred)

    def tiny_fold():
        rep = GainsStatisticalReport(n_groups=3)
        pred, expo, loss, prem = _book(n=12, seed=151)
        rep.fit_fold(pred, expo, loss, prem)
        return _report_state(rep)

    case("gains.report.fold_smaller_than_groups", tags=(BEHAVIOR, ADVERSARIAL),
         **ATOL)(tiny_fold)

    def zero_premium():
        rep = GainsStatisticalReport()
        pred, expo, loss, _ = _book(n=120, seed=157)
        return {"state": _report_state(
            rep.fit_fold(pred, expo, loss, np.zeros(120)))}

    case("gains.report.zero_premium", tags=(BEHAVIOR, ADVERSARIAL), **ATOL)(
        zero_premium)

    def zero_loss():
        rep = GainsStatisticalReport()
        pred, expo, _, prem = _book(n=120, seed=163)
        return _report_state(rep.fit_fold(pred, expo, np.zeros(120), prem))

    case("gains.report.zero_loss", tags=(BEHAVIOR, ADVERSARIAL), **ATOL)(zero_loss)

    adversarial("gains.report.scalar_pred")(
        lambda: GainsStatisticalReport().fit_fold(1.0, np.ones(4), np.ones(4)))
    adversarial("gains.report.mismatched_lengths")(
        lambda: GainsStatisticalReport().fit_fold(np.ones(10), np.ones(4),
                                                  np.ones(4)))


_error_path_cases()


def _internal_cases():
    from skutil.metrics import GainsStatisticalReport
    from skutil.metrics._act import _as_numpy

    case("gains._as_numpy.single", tags=(BEHAVIOR,))(
        _bind(_as_numpy, np.arange(4.0)))
    case("gains._as_numpy.multiple", tags=(BEHAVIOR,))(
        _bind(_as_numpy, [1, 2], (3.0, 4.0), pd.Series([5, 6])))
    case("gains._as_numpy.copies", tags=(BEHAVIOR,))(
        lambda: _as_numpy(np.arange(3)) is not None)
    adversarial("gains._as_numpy.scalar")(_bind(_as_numpy, 3))
    adversarial("gains._as_numpy.string")(_bind(_as_numpy, "abc"))

    def compute_stats():
        rep = GainsStatisticalReport(n_groups=4)
        pred, expo, loss, prem = _book(n=80, seed=167)
        tab, agg, n_groups = rep._compute_stats(pred, expo, loss, prem)
        return {"tab": tab, "agg": agg, "n_groups": n_groups}

    case("gains._compute_stats.frame", tags=(BEHAVIOR,), **ATOL)(compute_stats)

    def metric_parts():
        rep = GainsStatisticalReport(n_groups=4)
        pred, expo, loss, prem = _book(n=80, seed=167)
        tab, agg, n_groups = rep._compute_stats(pred, expo, loss, prem)
        kw = {"pred": pred, "expo": expo, "loss": loss, "prem": prem,
              "stats": agg, "tab": tab, "n_groups": n_groups}
        return {"lift": rep._lift(**dict(kw)), "gini": rep._gini(**dict(kw))}

    case("gains._lift_and_gini.values", tags=(BEHAVIOR,), **ATOL)(metric_parts)

    case("gains.report.signs", tags=(BEHAVIOR,))(
        lambda: dict(GainsStatisticalReport._signs))
    # n_groups is assigned the literal 10 in __init__, whatever was asked for
    case("gains.report.n_groups_is_ignored", tags=(BEHAVIOR, ADVERSARIAL))(
        lambda: GainsStatisticalReport(n_groups=3).n_groups)


_internal_cases()
