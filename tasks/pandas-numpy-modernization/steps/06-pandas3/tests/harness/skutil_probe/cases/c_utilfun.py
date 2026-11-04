"""skutil.utils.util and skutil.utils.fixes: the free functions.

These are the densest source of legacy-NumPy contact points in the tree
(``np.bool``, ``np.int``, ``np.float``, ``np.long`` all appear in the type
predicates), so the predicates are exercised across the whole scalar edge set
rather than on a happy-path value or two.
"""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data


def _bind(fn, *args, **kwargs):
    def run():
        return fn(*args, **kwargs)
    return run


# --------------------------------------------------------------------------
# type predicates -- is_integer / is_float / is_numeric / is_iterable
#
# Each is its own case rather than one case over a list, because a single
# combined case would let one wrong answer hide inside a passing aggregate.

def _predicate_cases():
    from skutil.utils import is_integer, is_float, is_numeric, is_iterable

    for name, fn in (("is_integer", is_integer), ("is_float", is_float),
                     ("is_numeric", is_numeric)):
        for label, value in data.NUMERIC_EDGES:
            tags = (BEHAVIOR,) if label in ("zero", "one", "float_one", "none") \
                else (BEHAVIOR, ADVERSARIAL)
            case("util.%s.%s" % (name, label), tags=tags)(_bind(fn, value))

    for label, value in data.ITERABLE_EDGES:
        tags = (BEHAVIOR,) if label in ("list_empty", "tuple") \
            else (BEHAVIOR, ADVERSARIAL)
        case("util.is_iterable.%s" % label, tags=tags)(_bind(is_iterable, value))


_predicate_cases()


# --------------------------------------------------------------------------
# log / exp -- the sanitized, overflow-suppressing pair

def _logexp_cases():
    from skutil.utils import log, exp

    for name, fn in (("log", log), ("exp", exp)):
        for label, value in data.NUMERIC_EDGES:
            if label in ("complex", "str_digit", "str_word", "str_empty"):
                continue  # covered by the dedicated raise cases below
            tags = (BEHAVIOR,) if label in ("one", "float_one") \
                else (BEHAVIOR, ADVERSARIAL)
            case("util.%s.%s" % (name, label), tags=tags)(_bind(fn, value))

        # vectorized paths
        case("util.%s.vec_list" % name, tags=(BEHAVIOR,))(
            _bind(fn, [1.0, 2.0, 3.0, 0.0]))
        case("util.%s.vec_ndarray" % name, tags=(BEHAVIOR,))(
            _bind(fn, np.array([0.5, 1.5, 1e5])))
        adversarial("util.%s.vec_nested" % name)(
            _bind(fn, [[1.0, 2.0], [3.0]]))
        adversarial("util.%s.vec_empty" % name)(_bind(fn, []))
        adversarial("util.%s.vec_with_nan" % name)(
            _bind(fn, np.array([np.nan, np.inf, -np.inf, 0.0])))
        adversarial("util.%s.vec_object_dtype" % name)(
            _bind(fn, np.array([1.0, "a"], dtype=object)))
        adversarial("util.%s.raise_str" % name)(_bind(fn, "abc"))
        adversarial("util.%s.raise_none" % name)(_bind(fn, None))
        adversarial("util.%s.raise_complex" % name)(_bind(fn, 1 + 2j))


_logexp_cases()


# --------------------------------------------------------------------------
# flatten_all / flatten_all_generator

def _flatten_cases():
    from skutil.utils import flatten_all, flatten_all_generator

    for label, value in data.ITERABLE_EDGES:
        tags = (BEHAVIOR,) if label in ("list_nested", "tuple") \
            else (BEHAVIOR, ADVERSARIAL)
        case("util.flatten_all.%s" % label, tags=tags)(_bind(flatten_all, value))

    case("util.flatten_all_generator.nested", tags=(BEHAVIOR,))(
        _bind(lambda v: list(flatten_all_generator(v)),
              [[[], 3, 4], ["1", "a"], [[[1]]], 1, 2]))
    adversarial("util.flatten_all_generator.scalar")(
        _bind(lambda v: list(flatten_all_generator(v)), 7))
    adversarial("util.flatten_all.frame")(
        _bind(flatten_all, data.num_frame(4, 3)))
    adversarial("util.flatten_all.series")(
        _bind(flatten_all, pd.Series([1, 2, 3])))


_flatten_cases()


# --------------------------------------------------------------------------
# human_bytes

def _human_bytes_cases():
    from skutil.utils import human_bytes

    for unit in ("KB", "MB", "GB", "TB"):
        case("util.human_bytes.%s" % unit, tags=(BEHAVIOR,))(
            _bind(human_bytes, 1024 ** 3 + 7, unit))
    adversarial("util.human_bytes.zero")(_bind(human_bytes, 0))
    adversarial("util.human_bytes.negative")(_bind(human_bytes, -1048576))
    adversarial("util.human_bytes.huge")(_bind(human_bytes, 2 ** 70))
    adversarial("util.human_bytes.float_input")(_bind(human_bytes, 1536.5, "KB"))
    adversarial("util.human_bytes.np_int64")(
        _bind(human_bytes, np.int64(1024 ** 2), "MB"))
    adversarial("util.human_bytes.bad_unit")(_bind(human_bytes, 1024, "PB"))
    adversarial("util.human_bytes.lowercase_unit")(_bind(human_bytes, 1024, "mb"))


_human_bytes_cases()


# --------------------------------------------------------------------------
# dict_keys / dict_values -- py2/py3 shims that a modernization should retire

def _dict_cases():
    from skutil.utils import dict_keys, dict_values

    d = {"b": 2, "a": 1, "c": 3}
    case("fixes.dict_keys.sorted", tags=(BEHAVIOR,))(
        _bind(lambda x: sorted(dict_keys(x)), d))
    case("fixes.dict_values.sorted", tags=(BEHAVIOR,))(
        _bind(lambda x: sorted(dict_values(x)), d))
    adversarial("fixes.dict_keys.empty")(_bind(dict_keys, {}))
    adversarial("fixes.dict_values.mixed")(
        _bind(lambda x: sorted(dict_values(x), key=repr),
              {"a": None, "b": np.float64(1.0), "c": "s"}))


_dict_cases()


# --------------------------------------------------------------------------
# get_numeric / is_entirely_numeric / df_memory_estimate

def _frame_kind_cases():
    from skutil.utils import get_numeric, is_entirely_numeric, df_memory_estimate

    frames = [
        ("num", data.num_frame),
        ("int", data.int_frame),
        ("mixed", data.mixed_frame),
        ("nan", data.nan_frame),
        ("category", data.category_frame),
    ]
    for label, builder in frames:
        tags = (BEHAVIOR,) if label in ("num", "int") else (BEHAVIOR, ADVERSARIAL)
        case("util.get_numeric.%s" % label, tags=tags)(
            _bind(lambda b: get_numeric(b()), builder))
        case("util.is_entirely_numeric.%s" % label, tags=tags)(
            _bind(lambda b: is_entirely_numeric(b()), builder))

    case("util.df_memory_estimate.num", tags=(BEHAVIOR,))(
        _bind(lambda: df_memory_estimate(data.num_frame(100, 8))))
    case("util.df_memory_estimate.with_index", tags=(BEHAVIOR,))(
        _bind(lambda: df_memory_estimate(data.num_frame(100, 8), index=True)))
    adversarial("util.df_memory_estimate.kb")(
        _bind(lambda: df_memory_estimate(data.int_frame(), unit="KB")))
    adversarial("util.df_memory_estimate.ndarray")(
        _bind(lambda: df_memory_estimate(np.zeros((10, 3)))))
    adversarial("util.df_memory_estimate.bad_unit")(
        _bind(lambda: df_memory_estimate(data.num_frame(4, 2), unit="PB")))
    adversarial("util.get_numeric.empty_frame")(
        _bind(lambda: get_numeric(pd.DataFrame())))
    adversarial("util.get_numeric.bool_column")(
        _bind(lambda: get_numeric(pd.DataFrame({"a": [True, False], "b": [1, 2]}))))


_frame_kind_cases()


# --------------------------------------------------------------------------
# shuffle_dataframe -- reads the process-global RNG, which the runner seeds

def _shuffle_cases():
    from skutil.utils import shuffle_dataframe

    case("util.shuffle_dataframe.num", tags=(BEHAVIOR,))(
        _bind(lambda: shuffle_dataframe(data.num_frame(30, 4))))
    adversarial("util.shuffle_dataframe.single_row")(
        _bind(lambda: shuffle_dataframe(data.num_frame(1, 3))))
    adversarial("util.shuffle_dataframe.ndarray")(
        _bind(lambda: shuffle_dataframe(np.arange(12).reshape(4, 3))))
    adversarial("util.shuffle_dataframe.series")(
        _bind(lambda: shuffle_dataframe(pd.Series([1, 2, 3]))))


_shuffle_cases()


# --------------------------------------------------------------------------
# bundled dataset loaders

def _loader_cases():
    from skutil.utils import load_iris_df, load_breast_cancer_df, load_boston_df

    case("util.load_iris_df.default", tags=(BEHAVIOR,))(
        _bind(lambda: load_iris_df().head(12)))
    case("util.load_iris_df.no_tgt", tags=(BEHAVIOR,))(
        _bind(lambda: load_iris_df(include_tgt=False).describe()))
    adversarial("util.load_iris_df.renamed_tgt")(
        _bind(lambda: list(load_iris_df(tgt_name="y").columns)))
    adversarial("util.load_iris_df.shuffled")(
        _bind(lambda: load_iris_df(shuffle=True).index.tolist()))
    case("util.load_breast_cancer_df.shape", tags=(BEHAVIOR,))(
        _bind(lambda: (load_breast_cancer_df().shape,
                       list(load_breast_cancer_df().columns))))
    case("util.load_boston_df.shape", tags=(BEHAVIOR,))(
        _bind(lambda: (load_boston_df().shape,
                       list(load_boston_df().columns))))
    adversarial("util.load_boston_df.head")(
        _bind(lambda: load_boston_df(include_tgt=True).head(5)))


_loader_cases()


# --------------------------------------------------------------------------
# report_confusion_matrix

def _confusion_cases():
    from skutil.utils import report_confusion_matrix

    r = data.rs(101)
    act = (r.rand(200) > 0.5).astype(int)
    prd = np.where(r.rand(200) > 0.2, act, 1 - act)

    case("util.report_confusion_matrix.balanced", tags=(BEHAVIOR,))(
        _bind(report_confusion_matrix, act, prd))
    case("util.report_confusion_matrix.no_metrics", tags=(BEHAVIOR,))(
        _bind(report_confusion_matrix, act, prd, False))
    adversarial("util.report_confusion_matrix.perfect")(
        _bind(report_confusion_matrix, act, act.copy()))
    adversarial("util.report_confusion_matrix.all_one_class")(
        _bind(report_confusion_matrix, np.ones(20, dtype=int),
              np.ones(20, dtype=int)))
    adversarial("util.report_confusion_matrix.inverted")(
        _bind(report_confusion_matrix, act, 1 - act))
    adversarial("util.report_confusion_matrix.three_classes")(
        _bind(report_confusion_matrix, np.array([0, 1, 2, 0]),
              np.array([0, 1, 2, 1])))
    adversarial("util.report_confusion_matrix.string_labels")(
        _bind(report_confusion_matrix, np.array(["a", "b", "a", "b"]),
              np.array(["a", "a", "b", "b"])))
    adversarial("util.report_confusion_matrix.bool_labels")(
        _bind(report_confusion_matrix, np.array([True, False, True, True]),
              np.array([True, True, False, True])))


_confusion_cases()


# --------------------------------------------------------------------------
# testing helpers -- part of the public surface, and assert-based

def _testing_cases():
    from skutil.testing import assert_fails, assert_elements_almost_equal

    def boom():
        raise ValueError("boom")

    def type_boom():
        raise TypeError("nope")

    case("testing.assert_fails.expected", tags=(BEHAVIOR,))(
        _bind(lambda: assert_fails(boom, ValueError)))
    adversarial("testing.assert_fails.wrong_type")(
        _bind(lambda: assert_fails(type_boom, ValueError)))
    adversarial("testing.assert_fails.no_raise")(
        _bind(lambda: assert_fails(lambda: 1, ValueError)))
    case("testing.assert_elements_almost_equal.close", tags=(BEHAVIOR,))(
        _bind(lambda: assert_elements_almost_equal(1.0, 1.0 + 1e-9)))
    adversarial("testing.assert_elements_almost_equal.far")(
        _bind(lambda: assert_elements_almost_equal(1.0, 1.1)))
    adversarial("testing.assert_elements_almost_equal.strings")(
        _bind(lambda: assert_elements_almost_equal("a", "a")))
    adversarial("testing.assert_elements_almost_equal.nan")(
        _bind(lambda: assert_elements_almost_equal(np.nan, np.nan)))


_testing_cases()
