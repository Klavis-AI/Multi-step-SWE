"""Seeded inputs.

Everything here is built from ``numpy.random.RandomState``, whose legacy bit
stream NumPy guarantees not to change; that is what lets one recording hold from
NumPy 1.14 through NumPy 2.x. Nothing in this module touches the process-global
RNG, and no builder returns a shared object: skutil mutates frames in place in
several places, so every call returns a fresh one.
"""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

LETTERS = list("abcdefghijklmnopqrstuvwxyz")


def rs(seed):
    return np.random.RandomState(seed)


def cols(p, prefix=""):
    return [prefix + LETTERS[i] if i < 26 else "%s%d" % (prefix, i) for i in range(p)]


def num_frame(n=60, p=5, seed=7, loc=0.0, scale=1.0):
    """Plain numeric frame, float64, deterministic."""
    a = rs(seed).randn(n, p) * scale + loc
    return pd.DataFrame(a, columns=cols(p))


def int_frame(n=40, p=4, seed=11, low=0, high=10):
    a = rs(seed).randint(low, high, size=(n, p))
    return pd.DataFrame(a, columns=cols(p))


def positive_frame(n=60, p=4, seed=13):
    """Strictly positive — Box-Cox's domain."""
    a = np.abs(rs(seed).randn(n, p)) + 0.5
    return pd.DataFrame(a, columns=cols(p))


def mixed_frame(n=50, seed=17):
    r = rs(seed)
    return pd.DataFrame({
        "a": r.randn(n),
        "b": r.randint(0, 5, n),
        "c": [["x", "y", "z"][i % 3] for i in range(n)],
        "d": (r.randn(n) > 0),
        "e": r.rand(n) * 1e6,
    }, columns=["a", "b", "c", "d", "e"])


def nan_frame(n=40, p=4, seed=19, frac=0.2):
    r = rs(seed)
    a = r.randn(n, p)
    mask = r.rand(n, p) < frac
    a[mask] = np.nan
    return pd.DataFrame(a, columns=cols(p))


def collinear_frame(n=80, seed=23):
    """One exact copy, one near copy, one constant, one independent."""
    r = rs(seed)
    base = r.randn(n)
    return pd.DataFrame({
        "a": base,
        "b": base.copy(),
        "c": base * 2.0 + r.randn(n) * 1e-3,
        "d": np.ones(n),
        "e": r.randn(n),
    }, columns=["a", "b", "c", "d", "e"])


def linear_combo_frame(n=60, seed=29):
    """Exact linear dependency, which is what LinearCombinationFilterer hunts."""
    r = rs(seed)
    a = r.randn(n)
    b = r.randn(n)
    return pd.DataFrame({
        "a": a,
        "b": b,
        "c": a + b,
        "d": r.randn(n),
    }, columns=["a", "b", "c", "d"])


def sparse_frame(n=50, seed=31):
    """Columns with a controlled fraction of zeros/NaN for the sparsity filter."""
    r = rs(seed)
    out = {}
    for i, frac in enumerate([0.0, 0.25, 0.5, 0.75, 1.0]):
        col = r.randn(n)
        k = int(round(n * frac))
        col[:k] = np.nan
        out[LETTERS[i]] = col
    return pd.DataFrame(out, columns=cols(5))


def near_zero_variance_frame(n=100, seed=37):
    r = rs(seed)
    almost = np.zeros(n)
    almost[0] = 1.0
    return pd.DataFrame({
        "a": r.randn(n),
        "b": np.ones(n),
        "c": almost,
        "d": r.randn(n) * 1e-9,
    }, columns=["a", "b", "c", "d"])


def class_frame(n=120, ratios=(0.8, 0.2), seed=41, tgt="target"):
    """Imbalanced labelled frame for the balancers."""
    r = rs(seed)
    counts = [int(round(n * f)) for f in ratios]
    counts[-1] = n - sum(counts[:-1])
    y = np.concatenate([np.full(c, i, dtype=int) for i, c in enumerate(counts)])
    X = pd.DataFrame(r.randn(n, 3), columns=cols(3))
    X[tgt] = y
    return X


def category_frame(n=60, seed=43):
    r = rs(seed)
    return pd.DataFrame({
        "a": [["red", "green", "blue"][i] for i in r.randint(0, 3, n)],
        "b": [["north", "south"][i] for i in r.randint(0, 2, n)],
        "c": r.randn(n),
    }, columns=["a", "b", "c"])


def matrix(kind, seed=47):
    """Named matrices for the QR and kernel edges."""
    r = rs(seed)
    if kind == "square":
        return r.randn(6, 6)
    if kind == "tall":
        return r.randn(20, 4)
    if kind == "wide":
        return r.randn(4, 20)
    if kind == "rank_deficient":
        a = r.randn(10, 3)
        return np.column_stack([a, a[:, 0] + a[:, 1]])
    if kind == "singular":
        a = r.randn(5, 1)
        return np.repeat(a, 4, axis=1)
    if kind == "zeros":
        return np.zeros((6, 3))
    if kind == "ones":
        return np.ones((6, 3))
    if kind == "single_col":
        return r.randn(8, 1)
    if kind == "single_row":
        return r.randn(1, 5)
    if kind == "identity":
        return np.eye(5)
    if kind == "huge":
        return r.randn(6, 3) * 1e150
    if kind == "tiny":
        return r.randn(6, 3) * 1e-150
    if kind == "mixed_scale":
        m = r.randn(8, 4)
        m[:, 0] *= 1e12
        m[:, 1] *= 1e-12
        return m
    if kind == "with_nan":
        m = r.randn(6, 3)
        m[2, 1] = np.nan
        return m
    if kind == "with_inf":
        m = r.randn(6, 3)
        m[1, 0] = np.inf
        m[3, 2] = -np.inf
        return m
    if kind == "int":
        return r.randint(-5, 5, size=(6, 3))
    if kind == "empty_rows":
        return np.zeros((0, 3))
    raise KeyError(kind)


# Scalars used by the type-predicate and numeric-helper strata. Ordered, so the
# ids derived from their position are stable.
NUMERIC_EDGES = [
    ("zero", 0),
    ("neg_zero_float", -0.0),
    ("one", 1),
    ("neg_one", -1),
    ("float_one", 1.0),
    ("tiny", 1e-300),
    ("denormal", 5e-324),
    ("huge", 1e300),
    ("int32_max", 2 ** 31 - 1),
    ("int32_max_plus", 2 ** 31),
    ("int64_max", 2 ** 63 - 1),
    ("int64_max_plus", 2 ** 63),
    ("past_2_53", 2 ** 53 + 1),
    ("nan", float("nan")),
    ("posinf", float("inf")),
    ("neginf", float("-inf")),
    ("np_int8", np.int8(-128)),
    ("np_uint8", np.uint8(255)),
    ("np_int64", np.int64(-(2 ** 62))),
    ("np_float32", np.float32(1.25)),
    ("np_float64", np.float64(-2.5)),
    ("np_bool_true", np.bool_(True)),
    ("bool_true", True),
    ("bool_false", False),
    ("complex", 1 + 2j),
    ("str_digit", "1"),
    ("str_word", "a"),
    ("str_empty", ""),
    ("none", None),
]

ITERABLE_EDGES = [
    ("list_empty", []),
    ("list_nested", [[[], 3, 4], ["1", "a"], [[[1]]], 1, 2]),
    ("list_deep", [1, [2, [3, [4, [5, [6, [7, [8]]]]]]]]),
    ("tuple", (1, 2, 3)),
    ("str", "abc"),
    ("dict", {"a": 1, "b": 2}),
    ("set", {1, 2, 3}),
    ("ndarray", np.arange(5)),
    ("ndarray_2d", np.arange(6).reshape(2, 3)),
    ("scalar_int", 5),
    ("none", None),
]
