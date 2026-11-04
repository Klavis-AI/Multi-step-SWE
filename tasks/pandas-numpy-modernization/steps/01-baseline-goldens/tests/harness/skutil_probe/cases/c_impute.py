"""skutil.preprocessing.impute: the selective and bagged imputers."""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data
from ..est import observe, observe_structural, frame_structure

ITOL = dict(rtol=1e-9, atol=1e-12)


def _reg(cid, tags, fn, **kw):
    case(cid, tags=tags, **kw)(fn)


def _selective_cases():
    from skutil.preprocessing import SelectiveImputer

    for fill in ("mean", "median", "mode"):
        _reg("impute.SelectiveImputer.%s" % fill, (BEHAVIOR,),
             observe(lambda f=fill: SelectiveImputer(fill=f),
                     lambda: data.nan_frame()), **ITOL)

    # ``transform`` narrows the scalar branch to ``isinstance(modes, int)``, so a
    # float fill fits through ``fit`` and then dies subscripting a float. That is
    # the baseline's behavior, not a fixture mistake -- record it as such.
    _reg("impute.SelectiveImputer.float_scalar", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveImputer(fill=-999.0),
                 lambda: data.nan_frame()))
    _reg("impute.SelectiveImputer.int_scalar", (BEHAVIOR,),
         observe(lambda: SelectiveImputer(fill=0), lambda: data.nan_frame()),
         **ITOL)
    _reg("impute.SelectiveImputer.per_column_list", (BEHAVIOR,),
         observe(lambda: SelectiveImputer(fill=["mean", "median", 0.0, "mode"]),
                 lambda: data.nan_frame()), **ITOL)
    _reg("impute.SelectiveImputer.dict_fill", (BEHAVIOR,),
         observe(lambda: SelectiveImputer(fill={"a": "mean", "c": -1}),
                 lambda: data.nan_frame()), **ITOL)
    _reg("impute.SelectiveImputer.dict_fill_resets_cols", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveImputer(cols=["a", "b"],
                                          fill={"c": "median", "d": 0}),
                 lambda: data.nan_frame()), **ITOL)
    _reg("impute.SelectiveImputer.subset", (BEHAVIOR,),
         observe(lambda: SelectiveImputer(cols=["a", "b"]),
                 lambda: data.nan_frame()), **ITOL)
    _reg("impute.SelectiveImputer.as_array", (BEHAVIOR,),
         observe(lambda: SelectiveImputer(as_df=False),
                 lambda: data.nan_frame()), **ITOL)
    _reg("impute.SelectiveImputer.no_missing", (BEHAVIOR,),
         observe(lambda: SelectiveImputer(), lambda: data.num_frame(20, 4)),
         **ITOL)
    _reg("impute.SelectiveImputer.refit_transform", (BEHAVIOR,),
         observe(lambda: SelectiveImputer(), lambda: data.nan_frame(),
                 call="fit_then_transform"), **ITOL)

    _reg("impute.SelectiveImputer.bad_fill_string", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveImputer(fill="nonsense"),
                 lambda: data.nan_frame()))
    _reg("impute.SelectiveImputer.short_fill_list", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveImputer(fill=["mean", "median"]),
                 lambda: data.nan_frame()))
    _reg("impute.SelectiveImputer.fill_list_bad_element", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveImputer(fill=["mean", "median", "nope", 0.0]),
                 lambda: data.nan_frame()))
    _reg("impute.SelectiveImputer.all_missing_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveImputer(),
                 lambda: pd.DataFrame({"a": [np.nan] * 10,
                                       "b": np.arange(10.0)})), **ITOL)
    _reg("impute.SelectiveImputer.object_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveImputer(), lambda: data.mixed_frame()))
    _reg("impute.SelectiveImputer.fill_none", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveImputer(fill=None), lambda: data.nan_frame()),
         **ITOL)
    _reg("impute.SelectiveImputer.with_inf", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveImputer(),
                 lambda: pd.DataFrame({"a": [1.0, np.inf, np.nan, 2.0],
                                       "b": [1.0, 2.0, 3.0, np.nan]})), **ITOL)


_selective_cases()


def _bagged_cases():
    """Bagged imputation runs a scikit-learn ensemble, so only the structure is
    recorded -- see ``est.frame_structure``."""
    from skutil.preprocessing import BaggedImputer, BaggedCategoricalImputer
    from sklearn.tree import DecisionTreeRegressor, DecisionTreeClassifier

    _reg("impute.BaggedImputer.default", (BEHAVIOR,),
         observe_structural(lambda: BaggedImputer(random_state=42),
                            lambda: data.nan_frame(60, 5)))
    _reg("impute.BaggedImputer.explicit_estimator", (BEHAVIOR,),
         observe_structural(
             lambda: BaggedImputer(random_state=42, n_estimators=4,
                                   base_estimator=DecisionTreeRegressor(
                                       max_depth=3, random_state=7)),
             lambda: data.nan_frame(60, 5)))
    _reg("impute.BaggedImputer.subset", (BEHAVIOR,),
         observe_structural(lambda: BaggedImputer(cols=["a", "b", "c"],
                                                  random_state=42),
                            lambda: data.nan_frame(60, 5)))
    _reg("impute.BaggedImputer.as_array", (BEHAVIOR,),
         observe_structural(lambda: BaggedImputer(as_df=False, random_state=42),
                            lambda: data.nan_frame(60, 5)))
    _reg("impute.BaggedImputer.no_missing", (BEHAVIOR,),
         observe_structural(lambda: BaggedImputer(random_state=42),
                            lambda: data.num_frame(40, 4)))
    _reg("impute.BaggedImputer.fill_override", (BEHAVIOR, ADVERSARIAL),
         observe_structural(lambda: BaggedImputer(random_state=42, fill=-1.0),
                            lambda: data.nan_frame(60, 5)))
    _reg("impute.BaggedImputer.too_few_columns", (BEHAVIOR, ADVERSARIAL),
         observe_structural(lambda: BaggedImputer(random_state=42),
                            lambda: data.nan_frame(30, 1)))
    _reg("impute.BaggedImputer.all_missing_col", (BEHAVIOR, ADVERSARIAL),
         observe_structural(
             lambda: BaggedImputer(random_state=42),
             lambda: pd.DataFrame({"a": [np.nan] * 30,
                                   "b": data.rs(1).randn(30),
                                   "c": data.rs(2).randn(30)})))
    _reg("impute.BaggedImputer.object_col", (BEHAVIOR, ADVERSARIAL),
         observe_structural(lambda: BaggedImputer(random_state=42),
                            lambda: data.mixed_frame()))
    _reg("impute.BaggedImputer.heavy_missing", (BEHAVIOR, ADVERSARIAL),
         observe_structural(lambda: BaggedImputer(random_state=42),
                            lambda: data.nan_frame(60, 5, frac=0.8)))

    def cat_frame():
        r = data.rs(71)
        df = pd.DataFrame({"a": r.randint(0, 3, 60).astype(float),
                           "b": r.randn(60),
                           "c": r.randn(60)})
        df.loc[r.rand(60) < 0.25, "a"] = np.nan
        return df

    def _bagged_refit_transform():
        """``transform`` re-predicts with the stored models, so it exercises a
        different path from ``fit_transform``."""
        imp = BaggedImputer(random_state=42).fit(data.nan_frame(60, 5))
        fresh = data.nan_frame(60, 5, frac=0.1)
        return {"state_keys": sorted(imp.models_),
                "structure": frame_structure(imp.transform(fresh), fresh)}

    def _cat_refit_transform():
        imp = BaggedCategoricalImputer(cols=["a"], random_state=42).fit(cat_frame())
        fresh = cat_frame()
        return {"state_keys": sorted(imp.models_),
                "structure": frame_structure(imp.transform(fresh), fresh)}

    _reg("impute.BaggedImputer.refit_transform", (BEHAVIOR,),
         _bagged_refit_transform)

    # ``cols`` is the set of columns to IMPUTE, and the classifier is fit against
    # each of them in turn -- so a categorical imputer must be pointed at the
    # categorical column, or it tries to classify a continuous one.
    _reg("impute.BaggedCategoricalImputer.default", (BEHAVIOR,),
         observe_structural(lambda: BaggedCategoricalImputer(cols=["a"],
                                                             random_state=42),
                            cat_frame))
    _reg("impute.BaggedCategoricalImputer.explicit_estimator", (BEHAVIOR,),
         observe_structural(
             lambda: BaggedCategoricalImputer(
                 cols=["a"], random_state=42, n_estimators=4,
                 base_estimator=DecisionTreeClassifier(max_depth=3,
                                                       random_state=7)),
             cat_frame))
    _reg("impute.BaggedCategoricalImputer.as_array", (BEHAVIOR,),
         observe_structural(lambda: BaggedCategoricalImputer(
             cols=["a"], as_df=False, random_state=42), cat_frame))
    _reg("impute.BaggedCategoricalImputer.refit_transform", (BEHAVIOR,),
         _cat_refit_transform)
    _reg("impute.BaggedCategoricalImputer.regressor_estimator",
         (BEHAVIOR, ADVERSARIAL),
         observe_structural(
             lambda: BaggedCategoricalImputer(
                 cols=["a"], random_state=42,
                 base_estimator=DecisionTreeRegressor(random_state=7)),
             cat_frame))
    _reg("impute.BaggedCategoricalImputer.continuous_target",
         (BEHAVIOR, ADVERSARIAL),
         observe_structural(lambda: BaggedCategoricalImputer(random_state=42),
                            lambda: data.nan_frame(60, 4)))


_bagged_cases()


def _helper_cases():
    from skutil.preprocessing.impute import (_validate_all_numeric, _col_mode,
                                             _val_values)

    case("impute._validate_all_numeric.numeric", tags=(BEHAVIOR,))(
        lambda: _validate_all_numeric(data.num_frame(6, 3)))
    adversarial("impute._validate_all_numeric.mixed")(
        lambda: _validate_all_numeric(data.mixed_frame()))
    case("impute._col_mode.unimodal", tags=(BEHAVIOR,))(
        lambda: _col_mode(pd.Series([1.0, 1.0, 2.0, np.nan])))
    adversarial("impute._col_mode.bimodal")(
        lambda: _col_mode(pd.Series([1.0, 1.0, 2.0, 2.0])))
    adversarial("impute._col_mode.all_nan")(
        lambda: _col_mode(pd.Series([np.nan, np.nan])))
    adversarial("impute._col_mode.strings")(
        lambda: _col_mode(pd.Series(["a", "b", "b"])))
    case("impute._val_values.strings", tags=(BEHAVIOR,))(
        lambda: _val_values(["mean", "median", "mode"]))
    case("impute._val_values.numbers", tags=(BEHAVIOR,))(
        lambda: _val_values([1, 2.5, np.float64(3)]))
    adversarial("impute._val_values.bad")(lambda: _val_values(["mean", "nope"]))
    adversarial("impute._val_values.none_element")(
        lambda: _val_values(["mean", None]))


_helper_cases()
