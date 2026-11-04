"""``validate_is_pd`` and the small validators every estimator opens with.

This is the busiest pandas seam in the repository: every ``fit`` in skutil
starts by handing its input through ``validate_is_pd``, which dispatches on
"is it a frame / did you name columns" and then, optionally, sweeps for
non-finite cells. It is built out of exactly the constructs this migration
moves -- ``DataFrame.from_records``, ``.copy()``, ``.apply`` over columns,
``hasattr(x, '__iter__')`` as the iterability test -- so its four branches, the
copy semantics of the frame it returns, and the dtypes it produces are all
recorded here rather than only observed through some transformer downstream.
"""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL, INVARIANT
from .. import data

VTOL = dict(rtol=1e-12, atol=1e-15)


def _bind(fn, *args, **kwargs):
    def run():
        return fn(*args, **kwargs)
    return run


def _seen(out):
    """The recordable half of a ``(frame, cols)`` return."""
    X, cols = out
    return {"frame": X,
            "columns": list(X.columns),
            "dtypes": [str(d) for d in X.dtypes],
            "index": list(X.index),
            "cols": cols,
            "cols_type": type(cols).__name__}


def _val_is_pd_cases():
    from skutil.utils import validate_is_pd

    def call(make_X, cols, **kw):
        def run():
            return _seen(validate_is_pd(make_X(), cols, **kw))
        return run

    # case 2: a frame and no cols -- the overwhelmingly common path
    case("validate.is_pd.frame_no_cols", tags=(BEHAVIOR,), **VTOL)(
        call(lambda: data.num_frame(8, 4), None))
    case("validate.is_pd.frame_no_cols_mixed", tags=(BEHAVIOR,), **VTOL)(
        call(data.mixed_frame, None))
    case("validate.is_pd.frame_no_cols_int", tags=(BEHAVIOR,), **VTOL)(
        call(lambda: data.int_frame(8, 3), None))

    # case 3: a frame and a subset
    case("validate.is_pd.frame_with_cols", tags=(BEHAVIOR,), **VTOL)(
        call(lambda: data.num_frame(8, 4), ["a", "c"]))
    case("validate.is_pd.frame_with_tuple_cols", tags=(BEHAVIOR,), **VTOL)(
        call(lambda: data.num_frame(8, 4), ("a", "c")))
    case("validate.is_pd.frame_with_index_cols", tags=(BEHAVIOR,), **VTOL)(
        call(lambda: data.num_frame(8, 4), pd.Index(["b", "d"])))
    case("validate.is_pd.frame_with_array_cols", tags=(BEHAVIOR,), **VTOL)(
        call(lambda: data.num_frame(8, 4), np.array(["b", "d"])))
    case("validate.is_pd.frame_with_string_col", tags=(BEHAVIOR,), **VTOL)(
        call(lambda: data.num_frame(8, 4), "a"))
    # an empty cols is treated as "all of them", not as "none of them"
    case("validate.is_pd.frame_with_empty_cols", tags=(BEHAVIOR, ADVERSARIAL),
         **VTOL)(call(lambda: data.num_frame(8, 4), []))
    # membership is never checked here: the KeyError is deferred to the caller
    case("validate.is_pd.frame_with_absent_col", tags=(BEHAVIOR, ADVERSARIAL),
         **VTOL)(call(lambda: data.num_frame(8, 4), ["nope"]))

    # case 4: an array and no cols -- default V1..Vn headers
    case("validate.is_pd.ndarray_no_cols", tags=(BEHAVIOR,), **VTOL)(
        call(lambda: np.arange(12.0).reshape(4, 3), None))
    case("validate.is_pd.ndarray_int_dtype", tags=(BEHAVIOR,), **VTOL)(
        call(lambda: np.arange(12).reshape(4, 3), None))
    # square on purpose: ``_def_headers`` counts a list's *rows*, so a
    # non-square list of lists asks pandas for the wrong number of columns.
    # That bug is pinned directly on ``_def_headers`` below, where the outcome
    # is skutil's own and not pandas' choice of exception.
    case("validate.is_pd.list_of_lists_no_cols", tags=(BEHAVIOR,), **VTOL)(
        call(lambda: [[1, 2], [3, 4]], None))

    # case 1: names but not a frame -- from_records with *default* headers, and
    # the caller's names handed straight back unattached to the frame
    case("validate.is_pd.ndarray_with_cols", tags=(BEHAVIOR, ADVERSARIAL),
         **VTOL)(call(lambda: np.arange(12.0).reshape(4, 3), ["a", "b", "c"]))
    case("validate.is_pd.list_of_lists_with_cols", tags=(BEHAVIOR, ADVERSARIAL),
         **VTOL)(call(lambda: [[1, 2], [3, 4]], ["x", "y"]))

    adversarial("validate.is_pd.series")(
        call(lambda: pd.Series([1.0, 2.0, 3.0]), None))
    adversarial("validate.is_pd.scalar")(call(lambda: 3, None))
    adversarial("validate.is_pd.none")(call(lambda: None, None))
    adversarial("validate.is_pd.string")(call(lambda: "abc", None))
    adversarial("validate.is_pd.list_of_tuples")(
        call(lambda: [(1, 2), (3, 4)], None))
    adversarial("validate.is_pd.flat_list")(call(lambda: [1, 2, 3], None))
    adversarial("validate.is_pd.bad_cols_scalar")(
        call(lambda: data.num_frame(4, 2), 3))


_val_is_pd_cases()


def _finite_cases():
    from skutil.utils import validate_is_pd

    case("validate.is_pd.finite_clean", tags=(BEHAVIOR,), **VTOL)(
        lambda: _seen(validate_is_pd(data.num_frame(10, 4), None,
                                     assert_all_finite=True)))
    adversarial("validate.is_pd.finite_nan")(
        lambda: _seen(validate_is_pd(data.nan_frame(10, 4), None,
                                     assert_all_finite=True)))

    def inf_frame():
        X = data.num_frame(10, 4)
        X.iloc[2, 1] = np.inf
        return X

    adversarial("validate.is_pd.finite_inf")(
        lambda: _seen(validate_is_pd(inf_frame(), None, assert_all_finite=True)))
    adversarial("validate.is_pd.finite_neg_inf")(
        lambda: _seen(validate_is_pd(
            data.num_frame(10, 4).assign(b=-np.inf), None,
            assert_all_finite=True)))

    def nan_outside_subset():
        """Only the named columns are swept, so a NaN elsewhere is invisible."""
        X = data.num_frame(10, 4)
        X.iloc[3, 3] = np.nan
        return _seen(validate_is_pd(X, ["a", "b"], assert_all_finite=True))

    case("validate.is_pd.finite_nan_outside_subset", tags=(BEHAVIOR, ADVERSARIAL),
         **VTOL)(nan_outside_subset)

    def nan_inside_subset():
        X = data.num_frame(10, 4)
        X.iloc[3, 0] = np.nan
        return _seen(validate_is_pd(X, ["a", "b"], assert_all_finite=True))

    adversarial("validate.is_pd.finite_nan_inside_subset")(nan_inside_subset)

    # object columns never reach ``np.isfinite``: ``get_numeric`` drops them
    case("validate.is_pd.finite_object_cols", tags=(BEHAVIOR, ADVERSARIAL),
         **VTOL)(lambda: _seen(validate_is_pd(data.mixed_frame(), None,
                                              assert_all_finite=True)))
    adversarial("validate.is_pd.finite_all_object")(
        lambda: _seen(validate_is_pd(data.category_frame(), None,
                                     assert_all_finite=True)))
    case("validate.is_pd.finite_ndarray", tags=(BEHAVIOR,), **VTOL)(
        lambda: _seen(validate_is_pd(np.arange(12.0).reshape(4, 3), None,
                                     assert_all_finite=True)))
    case("validate.is_pd.finite_bool_col", tags=(BEHAVIOR, ADVERSARIAL), **VTOL)(
        lambda: _seen(validate_is_pd(
            pd.DataFrame({"a": [True, False, True], "b": [1.0, 2.0, 3.0]}),
            None, assert_all_finite=True)))


_finite_cases()


def _copy_cases():
    from skutil.utils import validate_is_pd

    def copy_is_independent():
        """The returned frame is a copy: skutil transformers write into it, and
        the caller's frame must not move. Under copy-on-write that promise is
        kept by the write, not by the ``.copy()`` -- so it is pinned by doing
        the write and reading both frames back."""
        X = data.num_frame(6, 3)
        out, cols = validate_is_pd(X, None)
        out.iloc[0, 0] = -12345.0
        out["new"] = 1.0
        return {"is_same_object": out is X,
                "original_first_cell": float(X.iloc[0, 0]),
                "copy_first_cell": float(out.iloc[0, 0]),
                "original_columns": list(X.columns),
                "copy_columns": list(out.columns)}

    case("validate.is_pd.copy_is_independent", tags=(BEHAVIOR, INVARIANT),
         **VTOL)(copy_is_independent)

    def subset_copy_is_independent():
        """Case 3 copies the *whole* frame and returns the names separately, so
        the unnamed columns come along and are writable too."""
        X = data.num_frame(6, 3)
        out, cols = validate_is_pd(X, ["a"])
        out.iloc[0, 2] = -999.0
        return {"cols": cols,
                "returned_columns": list(out.columns),
                "original_last_cell": float(X.iloc[0, 2]),
                "copy_last_cell": float(out.iloc[0, 2])}

    case("validate.is_pd.subset_copy_is_independent", tags=(BEHAVIOR, INVARIANT),
         **VTOL)(subset_copy_is_independent)

    def index_is_preserved():
        X = data.num_frame(5, 2)
        X.index = ["r%d" % i for i in range(5)]
        out, _ = validate_is_pd(X, None)
        return {"index": list(out.index), "columns": list(out.columns)}

    case("validate.is_pd.index_preserved", tags=(BEHAVIOR,), **VTOL)(
        index_is_preserved)

    def from_records_drops_the_index():
        """The array branches rebuild through ``from_records``, which numbers
        the rows from zero whatever the caller had."""
        X = data.num_frame(5, 2)
        X.index = [10, 20, 30, 40, 50]
        out, _ = validate_is_pd(X.values, None)
        return {"index": list(out.index), "columns": list(out.columns)}

    case("validate.is_pd.from_records_reindexes", tags=(BEHAVIOR, ADVERSARIAL),
         **VTOL)(from_records_drops_the_index)


_copy_cases()


def _val_cols_cases():
    from skutil.utils.util import _val_cols, _def_headers

    def vc(cols):
        def run():
            out = _val_cols(cols)
            return {"out": out, "type": type(out).__name__}
        return run

    case("validate._val_cols.none", tags=(BEHAVIOR,))(vc(None))
    case("validate._val_cols.list", tags=(BEHAVIOR,))(vc(["a", "b"]))
    case("validate._val_cols.tuple", tags=(BEHAVIOR,))(vc(("a", "b")))
    case("validate._val_cols.string", tags=(BEHAVIOR,))(vc("a"))
    case("validate._val_cols.empty_list", tags=(BEHAVIOR,))(vc([]))
    case("validate._val_cols.ndarray", tags=(BEHAVIOR,))(vc(np.array(["a", "b"])))
    case("validate._val_cols.int_ndarray", tags=(BEHAVIOR,))(vc(np.array([0, 2])))
    case("validate._val_cols.index", tags=(BEHAVIOR,))(vc(pd.Index(["a", "b"])))
    case("validate._val_cols.series", tags=(BEHAVIOR,))(vc(pd.Series(["a", "b"])))
    case("validate._val_cols.set", tags=(BEHAVIOR, ADVERSARIAL))(
        vc(frozenset(["a"])))
    case("validate._val_cols.generator", tags=(BEHAVIOR, ADVERSARIAL))(
        vc(iter(["a", "b"])))
    case("validate._val_cols.dict", tags=(BEHAVIOR, ADVERSARIAL))(
        vc({"a": 1, "b": 2}))
    adversarial("validate._val_cols.int")(vc(3))
    adversarial("validate._val_cols.float")(vc(1.5))
    adversarial("validate._val_cols.bool")(vc(True))

    case("validate._def_headers.frame", tags=(BEHAVIOR,))(
        lambda: _def_headers(data.num_frame(4, 3)))
    case("validate._def_headers.ndarray", tags=(BEHAVIOR,))(
        lambda: _def_headers(np.zeros((4, 5))))
    # a list has no ``shape``, so the header count falls back to ``len(X)`` --
    # the number of rows. Three rows of two columns get three headers.
    case("validate._def_headers.list_of_lists", tags=(BEHAVIOR, ADVERSARIAL))(
        lambda: _def_headers([[1, 2], [3, 4], [5, 6]]))
    case("validate._def_headers.empty_frame", tags=(BEHAVIOR, ADVERSARIAL))(
        lambda: _def_headers(pd.DataFrame()))
    adversarial("validate._def_headers.one_dim")(
        lambda: _def_headers(np.zeros(4)))


_val_cols_cases()


def _cols_if_none_cases():
    from skutil.utils.fixes import _cols_if_none

    X = data.num_frame(4, 3)
    case("validate._cols_if_none.none", tags=(BEHAVIOR,))(
        _bind(_cols_if_none, X, None))
    case("validate._cols_if_none.given", tags=(BEHAVIOR,))(
        _bind(_cols_if_none, X, ["a"]))
    # the test is falsiness, not ``is None`` -- so an empty list means "all"
    case("validate._cols_if_none.empty", tags=(BEHAVIOR, ADVERSARIAL))(
        _bind(_cols_if_none, X, []))
    case("validate._cols_if_none.tuple", tags=(BEHAVIOR,))(
        _bind(_cols_if_none, X, ("a", "b")))
    adversarial("validate._cols_if_none.ndarray")(
        _bind(_cols_if_none, X, np.array(["a", "b"])))


_cols_if_none_cases()


def _small_validator_cases():
    from skutil.preprocessing.impute import _validate_all_numeric
    from skutil.feature_selection.select import _validate_cols
    from skutil.preprocessing.transform import _validate_rows
    from skutil.odr.dqrutl import _validate_matrix_size

    case("validate._validate_all_numeric.numeric", tags=(BEHAVIOR,))(
        _bind(_validate_all_numeric, data.num_frame(4, 3)))
    case("validate._validate_all_numeric.int", tags=(BEHAVIOR,))(
        _bind(_validate_all_numeric, data.int_frame(4, 3)))
    case("validate._validate_all_numeric.with_nan", tags=(BEHAVIOR,))(
        _bind(_validate_all_numeric, data.nan_frame(6, 3)))
    case("validate._validate_all_numeric.bool", tags=(BEHAVIOR, ADVERSARIAL))(
        _bind(_validate_all_numeric,
              pd.DataFrame({"a": [True, False], "b": [1.0, 2.0]})))
    adversarial("validate._validate_all_numeric.object")(
        _bind(_validate_all_numeric, data.mixed_frame()))
    adversarial("validate._validate_all_numeric.empty")(
        _bind(_validate_all_numeric, pd.DataFrame()))

    case("validate._validate_cols.none", tags=(BEHAVIOR,))(
        _bind(_validate_cols, None))
    case("validate._validate_cols.two", tags=(BEHAVIOR,))(
        _bind(_validate_cols, ["a", "b"]))
    adversarial("validate._validate_cols.one")(_bind(_validate_cols, ["a"]))
    adversarial("validate._validate_cols.empty")(_bind(_validate_cols, []))

    case("validate._validate_rows.ok", tags=(BEHAVIOR,))(
        _bind(_validate_rows, data.num_frame(4, 2)))
    adversarial("validate._validate_rows.one_row")(
        _bind(_validate_rows, data.num_frame(1, 2)))
    adversarial("validate._validate_rows.zero_rows")(
        _bind(_validate_rows, data.num_frame(0, 2)))

    case("validate._validate_matrix_size.ok", tags=(BEHAVIOR,))(
        _bind(_validate_matrix_size, 1000, 20))
    adversarial("validate._validate_matrix_size.too_big")(
        _bind(_validate_matrix_size, 2 ** 20, 2 ** 12))


_small_validator_cases()
