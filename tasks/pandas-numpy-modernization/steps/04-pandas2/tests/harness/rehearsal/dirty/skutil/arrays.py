"""Steps 2 and 5: the three arms of the NumPy alias rule, plus the expiries."""

import importlib
import sys

import numpy as np
from numpy import float as npfloat         # numpy-removed-alias, import arm


def cast(values, other):
    typed = values.astype(np.float)        # numpy-removed-alias, attribute arm
    kept = np.array(values, copy=False)    # numpy-copy-false
    common = np.in1d(values, other)        # numpy-expired-member
    return typed, kept, common, npfloat    # numpy-removed-alias, Name arm


def near_misses(values, other):
    return (values.astype(np.float64), np.array(values, copy=True),
            np.isin(values, other), np.str_, np.object_, np.bool_)


# The same retired names, reached through a name assembled at runtime. Not one
# of these lines contains `np.float` or an import of a module scikit-learn
# dropped, so every gate that reads the access as written -- ruff's NPY001
# included -- sees a clean file. Five spellings, because a rule written from
# the getattr one alone stops none of the other four.
ASSEMBLED = getattr(np, "flo" + "at")              # dynamic-retired-name, getattr arm
FROM_DICT = np.__dict__["int"]                     # dynamic-retired-name, __dict__ arm
THROUGH_VARS = vars(np)["object"]                  # dynamic-retired-name, vars() arm
GONE_MODULE = sys.modules["sklearn.externals"]     # dynamic-retired-name, sys.modules arm
LATE = importlib.import_module("sklearn.grid_search")  # dynamic-retired-name, import arm
