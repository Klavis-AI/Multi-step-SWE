"""skutil.utils.util: the descriptive-statistics report and its helpers.

``pd_stats`` sits on top of ``DataFrame.describe``, ``value_counts``,
``skew`` and ``kurtosis``, and assembles the result with
``DataFrame.from_dict`` -- a dense concentration of the pandas surface this
migration moves, so it gets a wide sweep of frame shapes.
"""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data

TOL = dict(rtol=1e-9, atol=1e-12)


def _bind(fn, *args, **kwargs):
    def run():
        return fn(*args, **kwargs)
    return run


def _stats_cases():
    from skutil.utils import pd_stats
    from skutil.utils.util import _is_int

    frames = [
        ("numeric", lambda: data.num_frame(80, 5)),
        ("integer", lambda: data.int_frame(80, 4)),
        ("mixed", lambda: data.mixed_frame()),
        ("with_nan", lambda: data.nan_frame(80, 4)),
        ("categorical", lambda: data.category_frame(60)),
        ("collinear", lambda: data.collinear_frame()),
        ("near_zero_variance", lambda: data.near_zero_variance_frame()),
        ("class", lambda: data.class_frame()),
    ]
    for name, mk in frames:
        tags = (BEHAVIOR,) if name in ("numeric", "integer", "mixed") \
            else (BEHAVIOR, ADVERSARIAL)
        case("stats.pd_stats.%s" % name, tags=tags, **TOL)(
            lambda m=mk: pd_stats(m()))

    for ct in ("all", "numeric", "object"):
        case("stats.pd_stats.col_type_%s" % ct, tags=(BEHAVIOR,), **TOL)(
            _bind(lambda t: pd_stats(data.mixed_frame(), col_type=t), ct))

    adversarial("stats.pd_stats.bad_col_type")(
        _bind(pd_stats, data.mixed_frame(), col_type="numerical"))
    case("stats.pd_stats.custom_na_str", tags=(BEHAVIOR,), **TOL)(
        _bind(lambda: pd_stats(data.mixed_frame(), na_str="n/a")))
    case("stats.pd_stats.skew_thresholds", tags=(BEHAVIOR,), **TOL)(
        _bind(lambda: pd_stats(data.num_frame(120, 4), hi_skew_thresh=0.05,
                               mod_skew_thresh=0.01)))

    def skewed_frame():
        r = data.rs(9)
        return pd.DataFrame({"sym": r.normal(size=200),
                             "right": r.exponential(size=200),
                             "left": -r.exponential(size=200)})

    case("stats.pd_stats.skew_ratings", tags=(BEHAVIOR,), **TOL)(
        lambda: pd_stats(skewed_frame()))

    # Every column single-valued: the min class and the max class coincide, so
    # the ratio branch falls through to the na_str.
    adversarial("stats.pd_stats.single_class_cols")(
        lambda: pd_stats(pd.DataFrame({"a": [1] * 20, "b": ["z"] * 20})))
    adversarial("stats.pd_stats.all_nan_col")(
        lambda: pd_stats(pd.DataFrame({"a": [np.nan] * 20,
                                       "b": np.arange(20.0)})))
    adversarial("stats.pd_stats.single_row")(
        lambda: pd_stats(data.num_frame(1, 3)))
    adversarial("stats.pd_stats.bool_col")(
        lambda: pd_stats(pd.DataFrame({"a": [True, False, True, True],
                                       "b": [1.0, 2.0, 3.0, 4.0]})))
    adversarial("stats.pd_stats.float_that_is_integral")(
        lambda: pd_stats(pd.DataFrame({"a": np.arange(20.0),
                                       "b": data.rs(3).randn(20)})))
    adversarial("stats.pd_stats.not_a_frame")(_bind(pd_stats, [1, 2, 3]))

    # _is_int is the branch that decides "float, but really an int"
    case("stats._is_int.float_integral", tags=(BEHAVIOR,))(
        _bind(_is_int, pd.Series([1.0, 2.0, 3.0]), "float64"))
    case("stats._is_int.float_fractional", tags=(BEHAVIOR,))(
        _bind(_is_int, pd.Series([1.0, 2.5]), "float64"))
    case("stats._is_int.int", tags=(BEHAVIOR,))(
        _bind(_is_int, pd.Series([1, 2, 3]), "int64"))
    adversarial("stats._is_int.object")(
        _bind(_is_int, pd.Series(["a", "b"]), "object"))
    adversarial("stats._is_int.with_nan")(
        _bind(_is_int, pd.Series([1.0, np.nan]), "float64"))
    adversarial("stats._is_int.bool")(
        _bind(_is_int, pd.Series([True, False]), "bool"))
    adversarial("stats._is_int.empty")(
        _bind(_is_int, pd.Series([], dtype="float64"), "float64"))


_stats_cases()


def _memory_cases():
    from skutil.utils import df_memory_estimate

    for unit in ("KB", "MB", "GB", "TB"):
        case("stats.df_memory_estimate.%s" % unit, tags=(BEHAVIOR,))(
            _bind(df_memory_estimate, data.num_frame(50, 4), unit))

    case("stats.df_memory_estimate.with_index", tags=(BEHAVIOR,))(
        _bind(df_memory_estimate, data.num_frame(50, 4), "KB", True))
    adversarial("stats.df_memory_estimate.object_cols")(
        _bind(df_memory_estimate, data.mixed_frame(), "KB"))
    adversarial("stats.df_memory_estimate.bad_unit")(
        _bind(df_memory_estimate, data.num_frame(10, 2), "PB"))
    adversarial("stats.df_memory_estimate.not_a_frame")(
        _bind(df_memory_estimate, np.arange(10.0)))


_memory_cases()
