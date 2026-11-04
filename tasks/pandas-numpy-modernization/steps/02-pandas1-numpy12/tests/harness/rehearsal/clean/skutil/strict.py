"""The near misses for the suppression counter specifically.

Every construct here is the *opposite* of a suppression: it turns a diagnostic
into a failure, or handles an exception instead of swallowing it. If the
counter charges this file for anything, it is charging steps for doing the work
rather than for evading it.
"""

import warnings

import numpy as np
import pandas as pd


def strict():
    warnings.simplefilter("error")
    warnings.filterwarnings("error", category=DeprecationWarning)
    np.seterr(all="raise")
    pd.set_option("mode.copy_on_write", True)


def measured(fn, *args):
    """catch_warnings is the wrapper for both halves; it is not a suppression."""
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        result = fn(*args)
    return result, [str(w.message) for w in caught]


def handled(df):
    try:
        return df.pop("x")
    except KeyError:
        raise
