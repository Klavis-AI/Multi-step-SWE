"""skutil.grid_search and the search machinery vendored in skutil.utils.fixes.

The estimators here are deliberately closed-form -- ``Ridge(solver='cholesky')``
solves a normal equation and ``GaussianNB`` is a moment estimate -- so a fitted
search's scores are arithmetic on the fold data and nothing else. Anything that
would drift with a scikit-learn release (fit timings, estimator ``repr``, the
iterative solvers) is left out of the recorded shape by ``est.search_summary``.
Folds are fixed, unshuffled ``KFold``/``StratifiedKFold`` over a row count that
divides evenly, so every fold is the same size and the ``iid`` weighting that
sklearn 0.20 applied and 0.24 removed lands on the same number either way.
"""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data
from ..est import search_summary, sk17_summary

GTOL = dict(rtol=1e-7, atol=1e-10)


def _bind(fn, *args, **kwargs):
    def run():
        return fn(*args, **kwargs)
    return run


def _reg_frame(n=120, p=4, seed=101):
    """A frame whose target is an exact linear function of its columns plus
    seeded noise, so a ridge fit is fully determined by the data."""
    r = data.rs(seed)
    X = pd.DataFrame(r.randn(n, p), columns=data.cols(p))
    y = pd.Series(X.values.dot(np.arange(1.0, p + 1.0)) + 0.1 * r.randn(n),
                  name="y")
    return X, y


def _clf_frame(n=120, p=4, seed=103):
    r = data.rs(seed)
    X = pd.DataFrame(r.randn(n, p), columns=data.cols(p))
    y = pd.Series((X["a"].values + X["b"].values > 0).astype(int), name="y")
    return X, y


def _search_cases():
    from skutil.grid_search import GridSearchCV, RandomizedSearchCV
    from skutil.utils.fixes import _grid_detail
    from sklearn.linear_model import Ridge
    from sklearn.naive_bayes import GaussianNB
    from sklearn.model_selection import KFold, StratifiedKFold

    RIDGE_GRID = {"alpha": [0.1, 1.0, 10.0]}
    NB_GRID = {"var_smoothing": [1e-9, 1e-6, 1e-3]}

    def grid_ridge(**kw):
        def run():
            X, y = _reg_frame()
            g = GridSearchCV(estimator=Ridge(solver="cholesky"),
                             param_grid=RIDGE_GRID,
                             cv=KFold(n_splits=3), **kw)
            g.fit(X, y)
            return search_summary(g, _grid_detail(g, 1.0))
        return run

    case("grid.GridSearchCV.ridge_frame", tags=(BEHAVIOR,), **GTOL)(grid_ridge())
    case("grid.GridSearchCV.ridge_explicit_scoring", tags=(BEHAVIOR,), **GTOL)(
        grid_ridge(scoring="neg_mean_squared_error"))

    def grid_ridge_numpy():
        X, y = _reg_frame()
        g = GridSearchCV(estimator=Ridge(solver="cholesky"),
                         param_grid=RIDGE_GRID, cv=KFold(n_splits=3))
        g.fit(X.values, y.values)
        return search_summary(g)

    case("grid.GridSearchCV.ridge_ndarray", tags=(BEHAVIOR,), **GTOL)(
        grid_ridge_numpy)

    def grid_ridge_y_frame():
        """``_as_numpy`` is what lets a one-column frame stand in for y."""
        X, y = _reg_frame()
        g = GridSearchCV(estimator=Ridge(solver="cholesky"),
                         param_grid=RIDGE_GRID, cv=KFold(n_splits=3))
        g.fit(X, y.to_frame())
        return search_summary(g)

    case("grid.GridSearchCV.y_as_frame", tags=(BEHAVIOR, ADVERSARIAL), **GTOL)(
        grid_ridge_y_frame)

    def grid_nb():
        X, y = _clf_frame()
        g = GridSearchCV(estimator=GaussianNB(), param_grid=NB_GRID,
                         cv=StratifiedKFold(n_splits=3))
        g.fit(X, y)
        return search_summary(g, _grid_detail(g, 1.96))

    case("grid.GridSearchCV.gaussian_nb", tags=(BEHAVIOR,), **GTOL)(grid_nb)

    def grid_pipeline():
        """A skutil transformer inside the pipeline is the reason this wrapper
        exists at all: it hands the next step a frame, not an ndarray."""
        from sklearn.pipeline import Pipeline
        from skutil.preprocessing import SelectiveScaler
        from skutil.feature_selection import FeatureRetainer

        X, y = _reg_frame()
        pipe = Pipeline([("retain", FeatureRetainer(cols=["a", "b", "c"])),
                         ("scale", SelectiveScaler()),
                         ("model", Ridge(solver="cholesky"))])
        g = GridSearchCV(estimator=pipe,
                         param_grid={"model__alpha": [0.1, 1.0],
                                     "retain__cols": [["a", "b"],
                                                      ["a", "b", "c"]]},
                         cv=KFold(n_splits=3))
        g.fit(X, y)
        return search_summary(g)

    case("grid.GridSearchCV.skutil_pipeline", tags=(BEHAVIOR,), **GTOL)(
        grid_pipeline)

    def random_ridge(n_iter=3):
        def run():
            X, y = _reg_frame()
            g = RandomizedSearchCV(
                estimator=Ridge(solver="cholesky"),
                param_distributions={"alpha": [0.01, 0.1, 1.0, 10.0, 100.0]},
                n_iter=n_iter, cv=KFold(n_splits=3), random_state=7)
            g.fit(X, y)
            return search_summary(g, _grid_detail(g, 1.0))
        return run

    case("grid.RandomizedSearchCV.ridge_frame", tags=(BEHAVIOR,), **GTOL)(
        random_ridge())
    case("grid.RandomizedSearchCV.exhaustive", tags=(BEHAVIOR,), **GTOL)(
        random_ridge(n_iter=5))

    def detail_sorting(**kw):
        def run():
            X, y = _reg_frame()
            g = GridSearchCV(estimator=Ridge(solver="cholesky"),
                             param_grid={"alpha": [0.1, 1.0, 10.0, 100.0]},
                             cv=KFold(n_splits=3))
            g.fit(X, y)
            df, drops = _grid_detail(g, 1.0, **kw)
            return {"columns": list(df.columns),
                    "index": list(df.index),
                    "alpha": [float(v) for v in df["alpha"].values],
                    "mean_test_score": [float(v)
                                        for v in df["mean_test_score"].values],
                    "drops": list(drops)}
        return run

    case("grid._grid_detail.sorted_asc", tags=(BEHAVIOR,), **GTOL)(
        detail_sorting())
    case("grid._grid_detail.sorted_desc", tags=(BEHAVIOR,), **GTOL)(
        detail_sorting(ascending=False))
    case("grid._grid_detail.unsorted", tags=(BEHAVIOR,), **GTOL)(
        detail_sorting(sort_results=False))
    case("grid._grid_detail.sort_by_param", tags=(BEHAVIOR, ADVERSARIAL), **GTOL)(
        detail_sorting(sort_by="alpha", ascending=False))

    def detail_unknown_column():
        X, y = _reg_frame()
        g = GridSearchCV(estimator=Ridge(solver="cholesky"),
                         param_grid=RIDGE_GRID, cv=KFold(n_splits=3)).fit(X, y)
        return _grid_detail(g, 1.0, sort_by="not_a_column")

    adversarial("grid._grid_detail.unknown_sort_column")(detail_unknown_column)
    adversarial("grid._grid_detail.unfitted")(
        _bind(_grid_detail, GridSearchCV(estimator=Ridge(),
                                         param_grid=RIDGE_GRID), 1.0))


_search_cases()


def _sk17_cases():
    """The vendored sklearn-0.17 search classes: this repo still ships them and
    still tests them, whatever sklearn is installed."""
    from skutil.utils.fixes import _SK17GridSearchCV, _SK17RandomizedSearchCV
    from sklearn.linear_model import Ridge
    from sklearn.model_selection import KFold

    def sk17_grid(**kw):
        def run():
            X, y = _reg_frame(n=90, p=3)
            g = _SK17GridSearchCV(estimator=Ridge(solver="cholesky"),
                                  param_grid={"alpha": [0.1, 1.0, 10.0]},
                                  cv=KFold(n_splits=3), **kw)
            g.fit(X, y)
            return sk17_summary(g)
        return run

    case("grid._SK17GridSearchCV.iid", tags=(BEHAVIOR,), **GTOL)(sk17_grid())
    case("grid._SK17GridSearchCV.not_iid", tags=(BEHAVIOR,), **GTOL)(
        sk17_grid(iid=False))
    case("grid._SK17GridSearchCV.no_refit", tags=(BEHAVIOR,), **GTOL)(
        sk17_grid(refit=False))
    case("grid._SK17GridSearchCV.scoring", tags=(BEHAVIOR,), **GTOL)(
        sk17_grid(scoring="neg_mean_squared_error"))

    def sk17_random():
        X, y = _reg_frame(n=90, p=3)
        g = _SK17RandomizedSearchCV(
            estimator=Ridge(solver="cholesky"),
            param_distributions={"alpha": [0.01, 0.1, 1.0, 10.0]},
            n_iter=3, cv=KFold(n_splits=3), random_state=11)
        g.fit(X, y)
        return sk17_summary(g)

    case("grid._SK17RandomizedSearchCV.basic", tags=(BEHAVIOR,), **GTOL)(
        sk17_random)

    def sk17_delegated():
        """``score``/``predict``/``transform`` all route through the
        ``if_delegate_has_method`` descriptors in utils.metaestimators."""
        X, y = _reg_frame(n=90, p=3)
        g = _SK17GridSearchCV(estimator=Ridge(solver="cholesky"),
                              param_grid={"alpha": [0.1, 1.0]},
                              cv=KFold(n_splits=3)).fit(X, y)
        return {"score": g.score(X, y),
                "predict_head": list(g.predict(X)[:10]),
                "estimator_type": g._estimator_type,
                "fit_predict_head": list(
                    _SK17GridSearchCV(estimator=Ridge(solver="cholesky"),
                                      param_grid={"alpha": [0.1, 1.0]},
                                      cv=KFold(n_splits=3)
                                      ).fit_predict(X, y)[:10])}

    case("grid._SK17GridSearchCV.delegated_methods", tags=(BEHAVIOR,), **GTOL)(
        sk17_delegated)

    def sk17_scoring_warns():
        """``score`` warns when an explicit scorer supersedes the estimator's."""
        X, y = _reg_frame(n=90, p=3)
        g = _SK17GridSearchCV(estimator=Ridge(solver="cholesky"),
                              param_grid={"alpha": [0.1, 1.0]},
                              scoring="neg_mean_squared_error",
                              cv=KFold(n_splits=3)).fit(X, y)
        return g.score(X, y)

    case("grid._SK17GridSearchCV.score_warns", tags=(BEHAVIOR, ADVERSARIAL),
         **GTOL)(sk17_scoring_warns)

    def sk17_no_predict_proba():
        X, y = _reg_frame(n=90, p=3)
        g = _SK17GridSearchCV(estimator=Ridge(solver="cholesky"),
                              param_grid={"alpha": [0.1, 1.0]},
                              cv=KFold(n_splits=3)).fit(X, y)
        return g.predict_proba(X)

    adversarial("grid._SK17GridSearchCV.missing_delegate")(sk17_no_predict_proba)

    def sk17_transform_delegate():
        from skutil.decomposition import SelectivePCA
        X, _ = _reg_frame(n=90, p=3)
        g = _SK17GridSearchCV(estimator=SelectivePCA(),
                              param_grid={"n_components": [1, 2]},
                              scoring=lambda est, Xi, yi=None: float(
                                  est.score(Xi)),
                              cv=KFold(n_splits=3)).fit(X, None)
        Xt = g.transform(X)
        return {"transformed": Xt,
                "inverse": g.inverse_transform(Xt),
                "best": dict(g.best_params_)}

    case("grid._SK17GridSearchCV.transform_delegate",
         tags=(BEHAVIOR, ADVERSARIAL), **GTOL)(sk17_transform_delegate)

    def sk17_error_score():
        """``error_score`` is the only place a failing candidate is tolerated."""
        from sklearn.base import BaseEstimator, RegressorMixin

        class Brittle(BaseEstimator, RegressorMixin):
            def __init__(self, alpha=1.0):
                self.alpha = alpha

            def fit(self, X, y=None):
                if self.alpha > 1.0:
                    raise RuntimeError("no")
                self.mean_ = float(np.mean(y))
                return self

            def predict(self, X):
                return np.full(np.asarray(X).shape[0], self.mean_)

        X, y = _reg_frame(n=90, p=3)
        g = _SK17GridSearchCV(estimator=Brittle(),
                              param_grid={"alpha": [0.5, 2.0]},
                              cv=KFold(n_splits=3),
                              error_score=-99.0).fit(X, y)
        return sk17_summary(g)

    case("grid._SK17GridSearchCV.error_score", tags=(BEHAVIOR, ADVERSARIAL),
         **GTOL)(sk17_error_score)


_sk17_cases()


def _fix_helper_cases():
    from skutil.utils.fixes import (_validate_X, _validate_y, _check_param_grid,
                                    _as_numpy, _indexable, _cv_len, _set_cv,
                                    _get_groups, _CVScoreTuple)
    from sklearn.model_selection import KFold

    X = data.num_frame(12, 3)
    y = pd.Series(np.arange(12.0), name="y")

    case("grid._validate_X.frame", tags=(BEHAVIOR,))(_bind(_validate_X, X))
    case("grid._validate_X.ndarray", tags=(BEHAVIOR,))(
        _bind(_validate_X, X.values))
    adversarial("grid._validate_X.list")(_bind(_validate_X, [[1, 2], [3, 4]]))

    case("grid._validate_y.none", tags=(BEHAVIOR,))(_bind(_validate_y, None))
    case("grid._validate_y.series", tags=(BEHAVIOR,))(_bind(_validate_y, y))
    case("grid._validate_y.one_col_frame", tags=(BEHAVIOR,))(
        _bind(_validate_y, y.to_frame()))
    case("grid._validate_y.ndarray", tags=(BEHAVIOR,))(
        _bind(_validate_y, np.arange(5)))
    case("grid._validate_y.list", tags=(BEHAVIOR,))(
        _bind(_validate_y, [1, 2, 3]))
    adversarial("grid._validate_y.wide_frame")(_bind(_validate_y, X))
    adversarial("grid._validate_y.scalar")(_bind(_validate_y, 3))
    adversarial("grid._validate_y.string")(_bind(_validate_y, "abc"))

    case("grid._as_numpy.none", tags=(BEHAVIOR,))(_bind(_as_numpy, None))
    case("grid._as_numpy.ndarray", tags=(BEHAVIOR,))(
        _bind(_as_numpy, np.arange(4.0)))
    case("grid._as_numpy.series", tags=(BEHAVIOR,))(_bind(_as_numpy, y))
    case("grid._as_numpy.tuple", tags=(BEHAVIOR,))(_bind(_as_numpy, (1, 2, 3)))
    case("grid._as_numpy.generator", tags=(BEHAVIOR, ADVERSARIAL))(
        lambda: _as_numpy(i for i in range(4)))
    adversarial("grid._as_numpy.string")(_bind(_as_numpy, "blah"))
    adversarial("grid._as_numpy.scalar")(_bind(_as_numpy, 4))
    adversarial("grid._as_numpy.set")(_bind(_as_numpy, set([3, 1, 2])))

    case("grid._indexable.frame_series", tags=(BEHAVIOR,))(
        _bind(_indexable, X, y))
    case("grid._indexable.unsupervised", tags=(BEHAVIOR,))(
        _bind(_indexable, X, None))
    adversarial("grid._indexable.mismatched_length")(
        _bind(_indexable, X, pd.Series(np.arange(3.0))))

    case("grid._cv_len.kfold", tags=(BEHAVIOR,))(
        _bind(_cv_len, KFold(n_splits=4), X.values, y.values))
    case("grid._set_cv.int_regressor", tags=(BEHAVIOR,))(
        lambda: type(_set_cv(3, X.values, y.values, False)).__name__)
    case("grid._set_cv.int_classifier", tags=(BEHAVIOR,))(
        lambda: type(_set_cv(3, X.values, np.arange(12) % 2, True)).__name__)
    case("grid._set_cv.passthrough", tags=(BEHAVIOR,))(
        lambda: type(_set_cv(KFold(n_splits=5), X.values, y.values,
                             False)).__name__)
    case("grid._get_groups.frame", tags=(BEHAVIOR,))(_bind(_get_groups, X, y))

    for label, pg in (("dict", {"a": [1, 2]}),
                      ("list_of_dicts", [{"a": [1]}, {"b": (2, 3)}]),
                      ("ndarray", {"a": np.array([1, 2])})):
        case("grid._check_param_grid.%s" % label, tags=(BEHAVIOR,))(
            _bind(_check_param_grid, pg))
    adversarial("grid._check_param_grid.scalar_value")(
        _bind(_check_param_grid, {"a": 1}))
    adversarial("grid._check_param_grid.empty_value")(
        _bind(_check_param_grid, {"a": []}))
    adversarial("grid._check_param_grid.two_dim")(
        _bind(_check_param_grid, {"a": np.zeros((2, 2))}))
    adversarial("grid._check_param_grid.string_value")(
        _bind(_check_param_grid, {"a": "ab"}))

    tup = _CVScoreTuple({"alpha": 1.0}, 0.5, np.array([0.4, 0.5, 0.6]))
    case("grid._CVScoreTuple.repr", tags=(BEHAVIOR,))(lambda: repr(tup))
    case("grid._CVScoreTuple.fields", tags=(BEHAVIOR,))(
        lambda: {"parameters": tup.parameters,
                 "mean": tup.mean_validation_score,
                 "cv": list(tup.cv_validation_scores),
                 "slots": list(_CVScoreTuple.__slots__)})


_fix_helper_cases()


def _model_selection_alias():
    """``skutil.model_selection`` exists only to re-export the search classes."""
    import skutil.model_selection as msel
    import skutil.grid_search as gs
    import skutil.linear_model as lm

    case("grid.model_selection.reexports", tags=(BEHAVIOR,))(
        lambda: {"all": sorted(msel.__all__),
                 "same_grid": msel.GridSearchCV is gs.GridSearchCV,
                 "same_random": msel.RandomizedSearchCV is gs.RandomizedSearchCV})
    case("grid.linear_model.placeholder", tags=(BEHAVIOR,))(
        lambda: sorted(lm.__all__))
    case("grid.grid_search.exports", tags=(BEHAVIOR,))(
        lambda: sorted(gs.__all__))


_model_selection_alias()
