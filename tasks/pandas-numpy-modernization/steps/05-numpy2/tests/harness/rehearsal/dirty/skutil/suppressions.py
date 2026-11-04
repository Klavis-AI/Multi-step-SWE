"""Every suppression kind the counter knows, once each.

The two ``noqa`` lines are the weighting test: a bare marker is one
suppression, a marker listing two codes is two, because turning off two rules
is two evasions.
"""

import warnings

import numpy as np
import pandas as pd

TOTAL = 0  # noqa
LIMIT = 1  # noqa: E501,W503
X = 2  # type: ignore
Y = 3  # pragma: no cover
Z = 4  # nosec
W = 5  # pylint: disable=invalid-name
V = 6  # mypy: ignore-errors

A_STRING = "this mentions noqa and type: ignore inside a literal"


def quiet(df):
    warnings.simplefilter("ignore")
    warnings.filterwarnings("ignore", category=DeprecationWarning)
    np.seterr("ignore")                               # positional arm
    np.seterr(all="ignore")                           # keyword arm
    pd.set_option("mode.chained_assignment", None)    # also an AST finding
    try:
        return df.pop("x")
    except KeyError:
        pass


def near_misses(fn):
    warnings.simplefilter("error")
    warnings.filterwarnings("default", category=FutureWarning)
    np.seterr(all="raise")
    with warnings.catch_warnings():
        warnings.simplefilter("always")
        return fn()
