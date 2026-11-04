"""Near misses for every rule in the catalogue. None of these may fire."""

import importlib
import random
import sys

import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns
import six
from collections.abc import Sized
from functools import reduce
from setuptools import setup
from sklearn.datasets import fetch_california_housing
from sklearn.metrics import check_scoring
from sklearn.model_selection import cross_val_score


def generators():
    # owned generators, not the process-global bit stream
    rng = np.random.default_rng(12345)
    legacy = np.random.RandomState(12345)
    own = random.Random(12345)
    return rng.standard_normal(3), legacy.randn(3), own.random()


def dtypes():
    # the surviving spellings, one character away from the removed ones
    return np.float64, np.int64, np.bool_, np.object_, np.str_, np.floating


def numpy_replacements(a, b):
    return np.isin(a, b), np.vstack([a, b]), np.all(a), np.cumprod(a), \
        np.trapezoid(a), np.nan, np.inf, np.array(a, copy=True), np.asarray(b)


def frames(df, fn):
    pd.set_option("display.width", 100)
    out = df.to_numpy()
    df2 = df.drop(columns=["x"])
    df3 = df.groupby("g", observed=True).sum()
    df4 = df.map(fn)
    df5 = df.apply(fn)
    df6 = df.apply(fn, axis=0)
    df7 = df.fillna(0)
    df.loc[0, "a"] = 1
    df["b"] = 2
    monotonic = df["a"].is_monotonic_increasing
    for key, value in df.items():
        del key, value
    factors = (df.dtypes == 'object') | (df.dtypes == 'str')
    text_cols = df.select_dtypes(include=['object', 'str'])
    return out, df2, df3, df4, df5, df6, df7, monotonic, factors, text_cols


def runtime_names(df, attr):
    """Fetched at runtime, but nothing any step retires.

    The two dynamic rules read through these spellings, so the file that shows
    they fire has to be paired with one that shows what they do *not* charge
    for. Three of the arms are narrow on purpose: the module-fetch ones
    (sys.modules, import_module) fire only for a module that is gone, and the
    mapping ones (__dict__, vars) only for a name that is. getattr into a
    module with nothing retired in it is the fourth. None of these lines is a
    migration away from anything, and a rule that charged for them would be
    charging for ordinary reflection.

    `attr` is a parameter rather than a literal because ruff's B009 rejects
    getattr with a constant attribute, and the selection this gate runs
    includes B.
    """
    live = sys.modules["numpy"]
    moved = importlib.import_module("sklearn.model_selection")
    surviving = np.__dict__["float64"]
    from_vars = vars(np)["isin"]
    fetched = getattr(pd, attr)
    return live, moved, surviving, from_vars, fetched, df


def strings():
    # raw strings and defined escapes only
    return r"\d+", r"[\$,%]", "\n\t\\", "a string mentioning np.float and noqa"


def text(mapping):
    return list(mapping.items()), reduce(lambda a, b: a + b, [1, 2, 3])


def platform_branch():
    if sys.maxsize > 2 ** 32:
        return "64-bit"
    return "32-bit"


def chart(df):
    """Explicit receivers throughout: no global figure stack, no rcParams."""
    dpi = mpl.rcParams["figure.dpi"]
    fig, ax = plt.subplots(figsize=(4, 3), dpi=dpi)
    with mpl.rc_context({"figure.dpi": 100}):
        sns.heatmap(df.corr(), ax=ax)
    ax.set_title("correlations")
    ax.set_xlabel("feature")
    plt.close(fig)
    return fig


def filter_collinearity(corr, threshold):
    """The hot path, vectorised: no interpreter-level iteration expression."""
    mask = np.triu(np.abs(corr.to_numpy()) > threshold, k=1)
    return corr.columns[mask.any(axis=0)].tolist()


def persist(est, path):
    """A fitted model saved and loaded through skops.io, and an array read
    without unpickling: neither is a pickle (pickle-persistence)."""
    import skops.io as sio
    sio.dump(est, path)
    back = sio.load(path, trusted=sio.get_untrusted_types(file=path))
    arr = np.load(path)
    return back, arr


def quiet():
    """Handling an exception is not suppressing a finding."""
    try:
        return fetch_california_housing()
    except OSError:
        raise


UNUSED = (Sized, six, setup, check_scoring, cross_val_score)
