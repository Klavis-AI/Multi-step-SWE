"""skutil.feature_selection: the filterers and droppers."""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data
from ..est import observe

STOL = dict(rtol=1e-9, atol=1e-12)


def _reg(cid, tags, fn, **kw):
    case(cid, tags=tags, **kw)(fn)


def _dropper_cases():
    from skutil.feature_selection import FeatureDropper, FeatureRetainer

    _reg("select.FeatureDropper.subset", (BEHAVIOR,),
         observe(lambda: FeatureDropper(cols=["a", "c"]),
                 lambda: data.num_frame(20, 5)))
    _reg("select.FeatureDropper.none", (BEHAVIOR,),
         observe(lambda: FeatureDropper(), lambda: data.num_frame(20, 5)))
    _reg("select.FeatureDropper.as_array", (BEHAVIOR,),
         observe(lambda: FeatureDropper(cols=["a"], as_df=False),
                 lambda: data.num_frame(10, 3)))
    _reg("select.FeatureDropper.all_cols", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FeatureDropper(cols=list("abc")),
                 lambda: data.num_frame(10, 3)))
    _reg("select.FeatureDropper.unknown_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FeatureDropper(cols=["zzz"]),
                 lambda: data.num_frame(10, 3)))
    _reg("select.FeatureDropper.duplicate_cols", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FeatureDropper(cols=["a", "a"]),
                 lambda: data.num_frame(10, 3)))
    _reg("select.FeatureDropper.on_mixed", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FeatureDropper(cols=["c"]), lambda: data.mixed_frame()))

    _reg("select.FeatureRetainer.subset", (BEHAVIOR,),
         observe(lambda: FeatureRetainer(cols=["a", "c"]),
                 lambda: data.num_frame(20, 5)))
    _reg("select.FeatureRetainer.none", (BEHAVIOR,),
         observe(lambda: FeatureRetainer(), lambda: data.num_frame(20, 5)))
    _reg("select.FeatureRetainer.reorders", (BEHAVIOR,),
         observe(lambda: FeatureRetainer(cols=["c", "a"]),
                 lambda: data.num_frame(20, 5)))
    _reg("select.FeatureRetainer.as_array", (BEHAVIOR,),
         observe(lambda: FeatureRetainer(cols=["a"], as_df=False),
                 lambda: data.num_frame(10, 3)))
    _reg("select.FeatureRetainer.unknown_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FeatureRetainer(cols=["zzz"]),
                 lambda: data.num_frame(10, 3)))
    _reg("select.FeatureRetainer.empty_list", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FeatureRetainer(cols=[]),
                 lambda: data.num_frame(10, 3)))
    _reg("select.FeatureRetainer.refit_transform", (BEHAVIOR,),
         observe(lambda: FeatureRetainer(cols=["a", "b"]),
                 lambda: data.num_frame(20, 5), call="fit_then_transform"))


_dropper_cases()


def _sparse_cases():
    from skutil.feature_selection import SparseFeatureDropper

    for thresh in (0.1, 0.25, 0.5, 0.75, 0.99, 0.0, 1.0):
        lbl = repr(thresh).replace(".", "_")
        tags = (BEHAVIOR,) if thresh in (0.25, 0.5, 0.75) \
            else (BEHAVIOR, ADVERSARIAL)
        _reg("select.SparseFeatureDropper.thresh_%s" % lbl, tags,
             observe(lambda t=thresh: SparseFeatureDropper(threshold=t),
                     lambda: data.sparse_frame()))

    _reg("select.SparseFeatureDropper.subset", (BEHAVIOR,),
         observe(lambda: SparseFeatureDropper(cols=["a", "b", "c"]),
                 lambda: data.sparse_frame()))
    _reg("select.SparseFeatureDropper.as_array", (BEHAVIOR,),
         observe(lambda: SparseFeatureDropper(as_df=False),
                 lambda: data.sparse_frame()))
    _reg("select.SparseFeatureDropper.bad_threshold", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SparseFeatureDropper(threshold=1.5),
                 lambda: data.sparse_frame()))
    _reg("select.SparseFeatureDropper.negative_threshold", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SparseFeatureDropper(threshold=-0.1),
                 lambda: data.sparse_frame()))
    _reg("select.SparseFeatureDropper.string_threshold", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SparseFeatureDropper(threshold="half"),
                 lambda: data.sparse_frame()))
    _reg("select.SparseFeatureDropper.no_nan", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SparseFeatureDropper(), lambda: data.num_frame(20, 4)))


_sparse_cases()


def _multicollinearity_cases():
    from skutil.feature_selection import MulticollinearityFilterer, filter_collinearity

    for thresh in (0.5, 0.85, 0.95, 0.999, 0.0, 1.0):
        lbl = repr(thresh).replace(".", "_")
        tags = (BEHAVIOR,) if thresh in (0.85, 0.95) else (BEHAVIOR, ADVERSARIAL)
        _reg("select.Multicollinearity.thresh_%s" % lbl, tags,
             observe(lambda t=thresh: MulticollinearityFilterer(threshold=t),
                     lambda: data.collinear_frame()), **STOL)

    for method in ("pearson", "kendall", "spearman"):
        _reg("select.Multicollinearity.method_%s" % method, (BEHAVIOR,),
             observe(lambda m=method: MulticollinearityFilterer(method=m),
                     lambda: data.collinear_frame()), **STOL)

    _reg("select.Multicollinearity.subset", (BEHAVIOR,),
         observe(lambda: MulticollinearityFilterer(cols=["a", "b", "c"]),
                 lambda: data.collinear_frame()), **STOL)
    _reg("select.Multicollinearity.as_array", (BEHAVIOR,),
         observe(lambda: MulticollinearityFilterer(as_df=False),
                 lambda: data.collinear_frame()), **STOL)
    _reg("select.Multicollinearity.independent", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: MulticollinearityFilterer(),
                 lambda: data.num_frame(50, 5)), **STOL)
    _reg("select.Multicollinearity.constant_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: MulticollinearityFilterer(),
                 lambda: data.near_zero_variance_frame()), **STOL)
    _reg("select.Multicollinearity.with_nan", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: MulticollinearityFilterer(), lambda: data.nan_frame()),
         **STOL)
    _reg("select.Multicollinearity.single_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: MulticollinearityFilterer(),
                 lambda: data.num_frame(20, 1)), **STOL)
    _reg("select.Multicollinearity.refit_transform", (BEHAVIOR,),
         observe(lambda: MulticollinearityFilterer(),
                 lambda: data.collinear_frame(), call="fit_then_transform"),
         **STOL)

    corr = data.collinear_frame().corr()
    case("select.filter_collinearity.default", tags=(BEHAVIOR,), **STOL)(
        lambda: filter_collinearity(corr.copy(), 0.85))
    adversarial("select.filter_collinearity.zero_threshold", **STOL)(
        lambda: filter_collinearity(corr.copy(), 0.0))
    adversarial("select.filter_collinearity.one_threshold", **STOL)(
        lambda: filter_collinearity(corr.copy(), 1.0))
    adversarial("select.filter_collinearity.non_square")(
        lambda: filter_collinearity(data.num_frame(6, 3), 0.85))
    adversarial("select.filter_collinearity.identity", **STOL)(
        lambda: filter_collinearity(pd.DataFrame(np.eye(4), columns=data.cols(4),
                                                 index=data.cols(4)), 0.85))


_multicollinearity_cases()


def _nzv_cases():
    from skutil.feature_selection import NearZeroVarianceFilterer

    for thresh in (1e-6, 1e-2, 1.0, 0.0):
        lbl = ("%g" % thresh).replace(".", "_").replace("-", "neg")
        tags = (BEHAVIOR,) if thresh == 1e-6 else (BEHAVIOR, ADVERSARIAL)
        _reg("select.NearZeroVariance.variance_%s" % lbl, tags,
             observe(lambda t=thresh: NearZeroVarianceFilterer(threshold=t),
                     lambda: data.near_zero_variance_frame()), **STOL)

    for thresh in (10.0, 1.5, 100.0):
        lbl = ("%g" % thresh).replace(".", "_")
        _reg("select.NearZeroVariance.ratio_%s" % lbl, (BEHAVIOR,),
             observe(lambda t=thresh: NearZeroVarianceFilterer(
                 threshold=t, strategy="ratio"),
                 lambda: data.int_frame(60, 4, low=0, high=3)), **STOL)

    _reg("select.NearZeroVariance.bad_strategy", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: NearZeroVarianceFilterer(strategy="nope"),
                 lambda: data.near_zero_variance_frame()))
    _reg("select.NearZeroVariance.ratio_below_one", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: NearZeroVarianceFilterer(threshold=0.5, strategy="ratio"),
                 lambda: data.int_frame()))
    _reg("select.NearZeroVariance.subset", (BEHAVIOR,),
         observe(lambda: NearZeroVarianceFilterer(cols=["a", "b"]),
                 lambda: data.near_zero_variance_frame()), **STOL)
    _reg("select.NearZeroVariance.as_array", (BEHAVIOR,),
         observe(lambda: NearZeroVarianceFilterer(as_df=False),
                 lambda: data.near_zero_variance_frame()), **STOL)
    _reg("select.NearZeroVariance.with_nan", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: NearZeroVarianceFilterer(), lambda: data.nan_frame()),
         **STOL)
    _reg("select.NearZeroVariance.all_constant", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: NearZeroVarianceFilterer(),
                 lambda: pd.DataFrame(np.ones((20, 3)), columns=data.cols(3))),
         **STOL)
    _reg("select.NearZeroVariance.refit_transform", (BEHAVIOR,),
         observe(lambda: NearZeroVarianceFilterer(),
                 lambda: data.near_zero_variance_frame(),
                 call="fit_then_transform"), **STOL)

    from skutil.feature_selection.select import _near_zero_variance_ratio
    case("select._near_zero_variance_ratio.balanced", tags=(BEHAVIOR,), **STOL)(
        lambda: _near_zero_variance_ratio(pd.Series([1, 1, 2, 2, 3]), 10.0))
    adversarial("select._near_zero_variance_ratio.degenerate", **STOL)(
        lambda: _near_zero_variance_ratio(pd.Series([1] * 20), 10.0))
    adversarial("select._near_zero_variance_ratio.two_levels", **STOL)(
        lambda: _near_zero_variance_ratio(pd.Series([1] * 19 + [2]), 10.0))

    from skutil.feature_selection.select import _validate_cols
    case("select._validate_cols.enough", tags=(BEHAVIOR,))(
        lambda: _validate_cols(["a", "b"]))
    adversarial("select._validate_cols.too_few")(
        lambda: _validate_cols(["a"]))
    adversarial("select._validate_cols.none")(lambda: _validate_cols(None))


_nzv_cases()


def _linear_combination_cases():
    from skutil.feature_selection import LinearCombinationFilterer

    # ``drop_`` is ``[p for p in set(drops)]``: which columns it holds is the
    # contract, the order they come out of the set is not.
    UNORD = ("drop_",)

    _reg("select.LinearCombination.exact", (BEHAVIOR,),
         observe(lambda: LinearCombinationFilterer(),
                 lambda: data.linear_combo_frame(), unordered=UNORD), **STOL)
    _reg("select.LinearCombination.subset", (BEHAVIOR,),
         observe(lambda: LinearCombinationFilterer(cols=["a", "b", "c"]),
                 lambda: data.linear_combo_frame(), unordered=UNORD), **STOL)
    _reg("select.LinearCombination.as_array", (BEHAVIOR,),
         observe(lambda: LinearCombinationFilterer(as_df=False),
                 lambda: data.linear_combo_frame(), unordered=UNORD), **STOL)
    _reg("select.LinearCombination.independent", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: LinearCombinationFilterer(),
                 lambda: data.num_frame(40, 4), unordered=UNORD), **STOL)
    _reg("select.LinearCombination.duplicated_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: LinearCombinationFilterer(),
                 lambda: data.collinear_frame(), unordered=UNORD), **STOL)
    _reg("select.LinearCombination.single_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: LinearCombinationFilterer(),
                 lambda: data.num_frame(20, 1), unordered=UNORD), **STOL)
    _reg("select.LinearCombination.all_zero", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: LinearCombinationFilterer(),
                 lambda: pd.DataFrame(np.zeros((20, 3)), columns=data.cols(3)),
                 unordered=UNORD), **STOL)
    _reg("select.LinearCombination.wide", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: LinearCombinationFilterer(),
                 lambda: data.num_frame(4, 10), unordered=UNORD), **STOL)
    _reg("select.LinearCombination.refit_transform", (BEHAVIOR,),
         observe(lambda: LinearCombinationFilterer(),
                 lambda: data.linear_combo_frame(), call="fit_then_transform",
                 unordered=UNORD), **STOL)

    from skutil.feature_selection.combos import _enum_lc
    from skutil.odr import QRDecomposition
    case("select._enum_lc.dependent", tags=(BEHAVIOR,))(
        lambda: _enum_lc(QRDecomposition(data.linear_combo_frame().values)))
    adversarial("select._enum_lc.independent")(
        lambda: _enum_lc(QRDecomposition(data.num_frame(40, 4).values)))


_linear_combination_cases()
