"""Step 2: Python-2 scaffolding and the scikit-learn modules that moved."""

from __future__ import print_function      # py2-future-import

import sys

from sklearn.externals import joblib                  # sklearn-externals
from sklearn.metrics.scorer import check_scoring      # sklearn-moved-module

# The second spelling of the same construct, and the reason this fixture has
# both. Six of the nine sites in the real tree reach the interpreter version
# through a module-level local like this one rather than reading
# `sys.version_info` at the branch, and a rule that only matched the direct
# form could be *cleared* by hoisting the subscript up here -- leaving the dead
# branch exactly where it was. Two findings: the binding and the comparison.
VERSION_MAJOR = sys.version_info.major     # py2-version-branch, the binding


def convert(mapping):
    if VERSION_MAJOR < 3:                  # py2-version-branch, through the local
        return [long(v) for v in mapping.values()]
    if sys.version_info[0] == 2:           # py2-version-branch, direct
        items = mapping.iteritems()        # py2-builtin, attribute arm
    else:
        items = mapping.items()
    return [long(v) for _k, v in items]    # py2-builtin, Name arm


UNUSED = (joblib, check_scoring)
