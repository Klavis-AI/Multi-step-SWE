#!/usr/bin/env bash
# Step 2's oracle: take the step-1 tree to Python 3.9, NumPy 1.21.6,
# scipy 1.7.3, pandas 1.3.5, scikit-learn 0.24.2, matplotlib 3.5.3 and
# seaborn 0.11.2.
#
# Three layers, in this order, because each one is only correct after the
# previous one has run:
#
#   1. the semantic migration -- an import that moved between scikit-learn
#      releases, a vendored `six` the library stopped shipping, a pandas
#      method that was deleted rather than renamed, a Python-2 branch a tool
#      cannot tell from a live one, and the estimator contract sklearn 0.22
#      started enforcing. None of these has a mechanical rewrite, and all of
#      them are anchored on exact bytes with an asserted site count.
#
#   2. `ruff --fix`, then `ruff --fix --unsafe-fixes`, at step 2's own
#      selection. This is where the 259 pyupgrade-shaped edits happen --
#      f-strings, `super()`, `u''` prefixes, the NumPy scalar aliases. Run
#      after layer 1 because layer 1 deletes the `from __future__` imports and
#      the `six` shims that half of those rules would otherwise rewrite in
#      place, and before layer 3 because layer 3's anchors are what ruff
#      leaves behind.
#
#   3. the residue: the two rules ruff reports and refuses to rewrite. Both
#      refusals are correct in general and wrong for every site in this tree,
#      which is why they are spelled out one by one rather than swept.
#
# The migration is not allowed to move a recorded value that the step's own
# stack does not move. Where the new libraries genuinely changed an answer --
# `value_counts` tie order, `from_dict` key order, the parameter set of four
# estimators, and thirteen more -- the change is declared in
# tests/expected/flips-step02.json with a citation, and the verifier compares
# against the flipped expectation. Nothing here papers over a difference by
# pinning an old behaviour back into place.
#
# Every layer ends in a number that has to change: the ruff finding count
# after each pass, the site count of each anchored edit, and a residual
# construct scan that is deliberately not the verifier's instrument. A layer
# that quietly did nothing fails the script instead of passing it.
set -euo pipefail

ROOT="${1:-/app}"
cd "$ROOT"

# Identical to skutil_gates.ruffgate.selection_for(2) and target_for(2). The
# oracle and the gate have to be looking at the same rule set; if they drift,
# the oracle would be clearing findings the gate does not count, or leaving
# ones it does.
SELECT='NPY001,NPY002,PD002,PD003,PD007,PD008,PD009,PD010,PD012,PD013,PD015,UP'
TARGET=py39

want_ruff='ruff 0.14.0'
have_ruff=$(ruff --version 2>/dev/null || echo "<absent>")
if [ "$have_ruff" != "$want_ruff" ]; then
    echo "solve.sh: ruff is '$have_ruff', expected '$want_ruff'" >&2
    echo "solve.sh: a different ruff fixes a different set, and the counts" >&2
    echo "solve.sh: below were measured against the pinned one" >&2
    exit 1
fi

ruff_count() {
    local out
    out=$(ruff check --isolated --no-cache --ignore-noqa \
              --no-respect-gitignore --target-version "$TARGET" \
              --select "$SELECT" --output-format json . 2>/dev/null) || true
    printf '%s' "${out:-[]}" \
        | python -c 'import json,sys; print(len(json.load(sys.stdin)))'
}

expect_count() {  # label expected
    local got
    got=$(ruff_count)
    if [ "$got" != "$2" ]; then
        echo "solve.sh: $1: ruff reports $got findings, expected $2" >&2
        exit 1
    fi
    printf '   ruff %-34s %4d finding(s)\n' "$1" "$got"
}

echo "== step 2 oracle: $ROOT"
expect_count "entry tree" 322

# --------------------------------------------------------------------- layer 1
python - "$ROOT" <<'PYEOF'
# -*- coding: utf-8 -*-
"""Step 2's semantic migration: py3.9, NumPy 1.21.6, pandas 1.3.5, sklearn 0.24.2.

Everything here is a construct that no linter can rewrite for us: an import
that moved between scikit-learn releases, a vendored ``six`` that the library
stopped shipping, a pandas method that was deleted rather than renamed, or a
Python-2 branch that a tool cannot tell from a live one. The pyupgrade-shaped
layer on top of this -- f-strings, ``super()``, ``u''`` prefixes -- is left to
``ruff``, which does it correctly and does it 300 times.

Layer 1 of steps/02-pandas1-numpy12/solution/solve.sh, which embeds this
file verbatim. Also runnable on its own:

    python evidence/item4/step02-semantic.py /path/to/tree
"""
import os
import re
import sys

ROOT = os.path.abspath(sys.argv[1])
LEDGER = []


def read(relpath):
    with open(os.path.join(ROOT, relpath), encoding="utf-8") as fh:
        return fh.read()


def write(relpath, text):
    with open(os.path.join(ROOT, relpath), "w", encoding="utf-8") as fh:
        fh.write(text)


def die(msg):
    print("  FAILED: " + msg)
    raise SystemExit(1)


# --------------------------------------------------------------- anchored edit
#
# Every edit names the bytes it expects to find and the number of places it
# must find them. An edit that matches a different number of sites than it was
# written for is a changed tree, not a lucky one, so it stops the script
# instead of quietly doing less.

def edit(relpath, old, new, count=1):
    text = read(relpath)
    n = text.count(old)
    if n != count:
        die("%s: expected %d occurrence(s) of %r, found %d"
            % (relpath, count, old[:70], n))
    write(relpath, text.replace(old, new))
    LEDGER.append("edit  %-46s %d site(s)" % (relpath, count))


def sweep(name, fn, expected, suffix=".py"):
    """A pattern-anchored edit over every file, with the same site assertion."""
    total, files = 0, 0
    for relpath in walk(suffix):
        text = read(relpath)
        new_text, n = fn(text)
        if n:
            write(relpath, new_text)
            total += n
            files += 1
    if total != expected:
        die("sweep %s: expected %d sites, touched %d" % (name, expected, total))
    LEDGER.append("sweep %-46s %3d site(s) over %2d file(s)" % (name, total, files))


def walk(suffix):
    skip = {".git", "__pycache__", "build", "dist", ".eggs", ".ipynb_checkpoints"}
    out = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = sorted(d for d in dirnames
                             if d not in skip and not d.endswith(".egg-info"))
        for name in sorted(filenames):
            if name.endswith(suffix):
                out.append(os.path.relpath(os.path.join(dirpath, name), ROOT))
    return sorted(out)


# ============================================================ 1. sklearn.externals
#
# scikit-learn 0.23 deprecated ``sklearn.externals.six`` and 0.24 removed it,
# along with ``sklearn.externals.joblib``. joblib is a real distribution and is
# already an install requirement of scikit-learn itself; six's three uses here
# all have a Python-3 spelling that needs no dependency at all.

sweep("drop `from sklearn.externals import six`",
      lambda t: re.subn(r"^from sklearn\.externals import six\n", "", t, flags=re.M),
      9)

sweep("sklearn.externals.joblib -> joblib",
      lambda t: re.subn(r"^from sklearn\.externals\.joblib import ",
                        "from joblib import ", t, flags=re.M),
      2)

# ``six.string_types`` is ``(str,)`` on Python 3 and ``(str, unicode)`` on 2.
# Only the first spelling can occur here, and ``isinstance`` takes a bare type
# as happily as a one-tuple.
sweep("six.string_types -> str",
      lambda t: re.subn(r"\bsix\.string_types\b", "str", t),
      5)

# ``six.iteritems(d)`` is ``d.iteritems()`` on 2 and ``iter(d.items())`` on 3.
# Every call site here feeds a ``for``, which consumes a view exactly as it
# consumes an iterator.
sweep("six.iteritems(d) -> d.items()",
      lambda t: re.subn(r"\bsix\.iteritems\((\w+)\)", r"\1.items()", t),
      5)

# ``six.with_metaclass(M, *bases)`` builds a throwaway class whose only job is
# to carry the metaclass through Python 2's lack of a ``metaclass=`` keyword.
# Python 3 has the keyword.
for relpath, old, new in [
    ("skutil/base.py",
     "class BaseSkutil(six.with_metaclass(ABCMeta, BaseEstimator, SelectiveMixin)):",
     "class BaseSkutil(BaseEstimator, SelectiveMixin, metaclass=ABCMeta):"),
    ("skutil/decomposition/decompose.py",
     "class _BaseSelectiveDecomposer(six.with_metaclass(ABCMeta, BaseSkutil, TransformerMixin)):",
     "class _BaseSelectiveDecomposer(BaseSkutil, TransformerMixin, metaclass=ABCMeta):"),
    ("skutil/feature_selection/base.py",
     "class _BaseFeatureSelector(six.with_metaclass(ABCMeta, BaseSkutil, TransformerMixin)):",
     "class _BaseFeatureSelector(BaseSkutil, TransformerMixin, metaclass=ABCMeta):"),
    ("skutil/preprocessing/balance.py",
     "class _BaseBalancePartitioner(six.with_metaclass(abc.ABCMeta, object)):",
     "class _BaseBalancePartitioner(metaclass=abc.ABCMeta):"),
    ("skutil/preprocessing/balance.py",
     "class _BaseBalancer(six.with_metaclass(abc.ABCMeta, BaseSkutil, BalancerMixin)):",
     "class _BaseBalancer(BaseSkutil, BalancerMixin, metaclass=abc.ABCMeta):"),
    ("skutil/preprocessing/impute.py",
     "class _BaseImputer(six.with_metaclass(ABCMeta, BaseSkutil, TransformerMixin, ImputerMixin)):",
     "class _BaseImputer(BaseSkutil, TransformerMixin, ImputerMixin, metaclass=ABCMeta):"),
    ("skutil/utils/fixes.py",
     "class _SK17BaseSearchCV(six.with_metaclass(ABCMeta, BaseEstimator,\n"
     "                                           MetaEstimatorMixin)):",
     "class _SK17BaseSearchCV(BaseEstimator, MetaEstimatorMixin,\n"
     "                        metaclass=ABCMeta):"),
]:
    edit(relpath, old, new)


# ================================================ 2. modules scikit-learn moved
#
# ``sklearn.cross_validation`` and ``sklearn.grid_search`` were deprecated in
# 0.18 in favour of ``sklearn.model_selection`` and removed in 0.20;
# ``sklearn.metrics.scorer`` was deprecated in 0.22 and removed in 0.24. This
# tree still reaches all three -- in every case from a branch or a handler that
# is unreachable now, which is what made them survive the 0.20 install.

edit("skutil/utils/fixes.py",
     "from sklearn.metrics.scorer import check_scoring",
     "from sklearn.metrics import check_scoring")

# The 0.18 fork. The false arm imports three names out of modules that no
# longer exist, so it is not merely dead -- it would raise on the way past.
edit("skutil/utils/fixes.py", """\
# grid_search deprecation in sklearn 0.18
if sklearn.__version__ >= '0.18':
    SK18 = True
    from sklearn.model_selection import check_cv
    from sklearn.model_selection._validation import _fit_and_score
    from sklearn.model_selection import ParameterSampler, ParameterGrid
""", """\
# ``sklearn.grid_search`` and ``sklearn.cross_validation`` were removed in
# 0.20; ``model_selection`` is the only spelling any supported release has.
SK18 = True
from sklearn.model_selection import check_cv
from sklearn.model_selection._validation import _fit_and_score
from sklearn.model_selection import ParameterSampler, ParameterGrid
""")

_OLD_DO_FIT = '''

    def _do_fit(n_jobs, verbose, pre_dispatch, base_estimator,
                X, y, scorer, parameter_iterable, fit_params,
                error_score, cv, **kwargs):
        groups = kwargs.pop('groups')

        # test_score, n_samples, parameters
        out = Parallel(n_jobs=n_jobs, verbose=verbose, pre_dispatch=pre_dispatch)(
            delayed(_fit_and_score)(
                clone(base_estimator), X, y, scorer,
                train, test, verbose, parameters,
                fit_params=fit_params,
                return_train_score=False,
                return_n_test_samples=True,
                return_times=False,
                return_parameters=True,
                error_score=error_score)
            for parameters in parameter_iterable
            for train, test in cv.split(X, y, groups))

        # test_score, n_samples, _, parameters
        return [(mod[0], mod[1], None, mod[2]) for mod in out]


else:
    SK18 = False
    # catch deprecation warnings
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")

        from sklearn.cross_validation import check_cv
        from sklearn.cross_validation import _fit_and_score
        from sklearn.grid_search import ParameterSampler, ParameterGrid


    def _do_fit(n_jobs, verbose, pre_dispatch, base_estimator,
                X, y, scorer, parameter_iterable, fit_params,
                error_score, cv, **kwargs):
        # test_score, n_samples, score_time, parameters
        return Parallel(n_jobs=n_jobs, verbose=verbose, pre_dispatch=pre_dispatch)(
            delayed(_fit_and_score)(
                clone(base_estimator), X, y, scorer,
                train, test, verbose, parameters,
                fit_params, return_parameters=True,
                error_score=error_score)
            for parameters in parameter_iterable
            for train, test in cv)
'''

# ``_fit_and_score`` returned a positional list through 0.23 and a dict from
# 0.24 on. The keys carry the same four values in the same order, so the tuple
# the callers unpack is rebuilt by name rather than by index.
_NEW_DO_FIT = '''

def _do_fit(n_jobs, verbose, pre_dispatch, base_estimator,
            X, y, scorer, parameter_iterable, fit_params,
            error_score, cv, **kwargs):
    groups = kwargs.pop('groups')

    # test_score, n_samples, parameters
    out = Parallel(n_jobs=n_jobs, verbose=verbose, pre_dispatch=pre_dispatch)(
        delayed(_fit_and_score)(
            clone(base_estimator), X, y, scorer,
            train, test, verbose, parameters,
            fit_params=fit_params,
            return_train_score=False,
            return_n_test_samples=True,
            return_times=False,
            return_parameters=True,
            error_score=error_score)
        for parameters in parameter_iterable
        for train, test in cv.split(X, y, groups))

    # test_score, n_samples, _, parameters
    return [(mod['test_scores'], mod['n_test_samples'], None, mod['parameters'])
            for mod in out]
'''

edit("skutil/utils/fixes.py", _OLD_DO_FIT, _NEW_DO_FIT)

# The two test modules guard the same import with a handler whose false arm
# names the removed modules.
edit("skutil/tests/test_big.py", """\
try:
    from sklearn.model_selection import KFold, train_test_split, RandomizedSearchCV

    SK18 = True
except ImportError as ie:
    from sklearn.cross_validation import KFold, train_test_split
    from sklearn.grid_search import RandomizedSearchCV

    SK18 = False
""", """\
from sklearn.model_selection import KFold, train_test_split, RandomizedSearchCV

SK18 = True
""")

edit("skutil/tests/test_big.py", """\
        if not SK18:
            custom_cv = KFold(n=y_train.shape[0], n_folds=3, shuffle=True, random_state=42)
        else:
            custom_cv = KFold(n_splits=3, shuffle=True, random_state=42)
""", """\
        custom_cv = KFold(n_splits=3, shuffle=True, random_state=42)
""")

edit("skutil/tests/test_pipe.py", """\
try:
    from sklearn.model_selection import train_test_split, KFold

    # get our train/test
    X_train, X_test, y_train, y_test = train_test_split(X, iris.target, train_size=0.75, random_state=42)
    # default CV does not shuffle, so we define our own
    custom_cv = KFold(n_splits=5, shuffle=True, random_state=42)
except ImportError as i:
    from sklearn.cross_validation import train_test_split, KFold

    # get our train/test
    X_train, X_test, y_train, y_test = train_test_split(X, iris.target, train_size=0.75, random_state=42)
    custom_cv = KFold(n=y_train.shape[0], n_folds=5, shuffle=True, random_state=42)
""", """\
from sklearn.model_selection import train_test_split, KFold

# get our train/test
X_train, X_test, y_train, y_test = train_test_split(X, iris.target, train_size=0.75, random_state=42)
# default CV does not shuffle, so we define our own
custom_cv = KFold(n_splits=5, shuffle=True, random_state=42)
""")


# ==================================================== 3. pandas removed methods
#
# ``DataFrame.as_matrix`` was deprecated in pandas 0.23 and deleted in 1.0. It
# was a wrapper over ``.values`` whose only extra was a ``columns=`` argument;
# no call site here passes one, so ``.values`` is the same object, not merely
# the same shape.
sweep("as_matrix() -> values",
      lambda t: re.subn(r"\.as_matrix\(\)", ".values", t),
      31)

# ``inplace=True`` on ``drop`` is the pandas-vet finding and the construct that
# Copy-on-Write makes unreliable in pandas 3. Rebinding is the documented
# replacement and is what the method already does internally.
edit("skutil/feature_selection/select.py", """\
            c.drop(drop_nm, axis=1, inplace=True)
            c.drop(drop_nm, axis=0, inplace=True)""", """\
            c = c.drop(drop_nm, axis=1)
            c = c.drop(drop_nm, axis=0)""")

edit("skutil/utils/fixes.py", """\
        result_df.drop([x for x in result_df.columns if x.startswith('split')], axis=1, inplace=True)
        result_df.drop(['rank_test_score', 'params'], axis=1, inplace=True)""", """\
        result_df = result_df.drop(
            [x for x in result_df.columns if x.startswith('split')], axis=1)
        result_df = result_df.drop(['rank_test_score', 'params'], axis=1)""")


# ============================================= 4. NumPy's Python-builtin aliases
#
# ``np.bool``, ``np.int``, ``np.float`` and ``np.long`` were never NumPy types:
# NumPy bound them to the Python builtins. They were deprecated in 1.20 and
# removed in 1.24, and under 1.21 each access raises a DeprecationWarning --
# which is what makes them a step-2 problem rather than a step-5 one, because
# the tree is already required to import without warning. Each one is replaced
# by the builtin it aliased, and the ``isinstance`` tuples that listed the same
# builtin three ways collapse.

edit("skutil/feature_selection/select.py",
     "matrix[:, 1].astype(np.bool)", "matrix[:, 1].astype(bool)")

edit("skutil/odr/dqrutl.py", "dtype=np.int, order='F'", "dtype=int, order='F'", count=2)

edit("skutil/preprocessing/balance.py",
     "if not isinstance(ratio, (float, np.float)) or ratio <= 0 or ratio > 1:",
     "if not isinstance(ratio, float) or ratio <= 0 or ratio > 1:")

edit("skutil/utils/util.py",
     "mask = np.zeros_like(X, dtype=np.bool)",
     "mask = np.zeros_like(X, dtype=bool)")

# ``VERSION_MAJOR``/``long = int`` is the shim the whole file is named for. The
# only interpreter left is the one where the branch is taken.
edit("skutil/utils/fixes.py", """\
VERSION_MAJOR = sys.version_info.major
NoneType = type(None)  # Python 3 doesn't have a types.NoneType

# easier test for numericism
if VERSION_MAJOR > 2:
    long = int

""", """\
NoneType = type(None)  # Python 3 doesn't have a types.NoneType

""")

edit("skutil/utils/fixes.py", '''\
def _is_integer(x):
    """Determine whether some object ``x`` is an
    integer type (int, long, etc). This is part of the 
    ``fixes`` module, since Python 3 removes the long
    datatype, we have to check the version major.
''', '''\
def _is_integer(x):
    """Determine whether some object ``x`` is an
    integer type. ``bool`` is deliberately excluded:
    it is a subclass of ``int`` and of
    ``numbers.Integral``, and callers here ask this
    question to decide whether a value indexes.
''')

edit("skutil/utils/fixes.py", """\
    return (not isinstance(x, (bool, np.bool))) and \\
        isinstance(x, (numbers.Integral, int, np.int, np.long, long))  # no long type in python 3""",
     """\
    return (not isinstance(x, bool)) and isinstance(x, numbers.Integral)""")

# ``is_integer`` reimplemented the same predicate behind an interpreter-major
# branch and an assertion that cannot fail. Its live arm is ``_is_integer``,
# which the module already imports, so what is left is the delegation the
# unreachable tail of the function already performed.
edit("skutil/utils/util.py", '''\
    try:
        python_major_version = sys.version_info.major
        assert(python_major_version == 2 or python_major_version == 3)
        if python_major_version == 2:
            return (not isinstance(x, (bool, np.bool))) and \\
                isinstance(x, (numbers.Integral, int, long, np.int, np.long))
        elif python_major_version == 3:
            return (not isinstance(x, (bool, np.bool))) and \\
                isinstance(x, (numbers.Integral, int, np.int, np.long))
    except AssertionError:
        _, _, tb = sys.exc_info()
        traceback.print_tb(tb)  # Fixed format
        tb_info = traceback.extract_tb(tb)
        filename, line, func, text = tb_info[-1]

        print('An error occurred on line {} in statement {}'.format(line, text))
        exit(1)
    return _is_integer(x)''', '''\
    return _is_integer(x)''')

edit("skutil/utils/util.py",
     "    integer type (int, long, etc).\n",
     "    integer type.\n")

edit("skutil/utils/util.py",
     "    float type (float, np.float, etc).\n",
     "    float type.\n")

edit("skutil/utils/util.py", """\
    return isinstance(x, (float, np.float)) or \\
        (not isinstance(x, (bool, np.bool)) and isinstance(x, numbers.Real))""",
     """\
    return isinstance(x, float) or \\
        (not isinstance(x, bool) and isinstance(x, numbers.Real))""")

# ``sys`` and ``traceback`` were imported for the branch that just went.
edit("skutil/utils/util.py", "import warnings\nimport sys\nimport traceback\n",
     "import warnings\n")

# The three aliases this test passed were the Python builtins, so the three
# lines duplicated the two above them. Named NumPy scalar types are what the
# test was reaching for and what it now covers.
edit("skutil/utils/tests/test_util.py", """\
    assert is_numeric(np.long(1))
    assert is_numeric(np.int(1.0))
    assert is_numeric(np.float(1))""", """\
    assert is_numeric(np.int64(1))
    assert is_numeric(np.int32(1))
    assert is_numeric(np.float64(1))""")


# =========================================== 5. branches on the interpreter major
#
# Every one of these has a live arm and a dead one, and the dead arm names
# something Python 3 does not have.

edit("setup.py", """\
if sys.version_info[0] < 3:
    import __builtin__ as builtins
else:
    import builtins
""", """\
import builtins
""")

edit("doc/sphinxext/numpy_ext/numpydoc.py", """\
        if sys.version_info[0] < 3:
            lines[:] = unicode(doc).splitlines()
        else:
            lines[:] = str(doc).splitlines()
""", """\
        lines[:] = str(doc).splitlines()
""")

edit("doc/sphinxext/numpy_ext/numpydoc.py", """\
    if sys.version_info[0] < 3:
        app.connect(b'autodoc-process-docstring', mangle_docstrings)
        app.connect(b'autodoc-process-signature', mangle_signature)
    else:
        app.connect('autodoc-process-docstring', mangle_docstrings)
        app.connect('autodoc-process-signature', mangle_signature)
""", """\
    app.connect('autodoc-process-docstring', mangle_docstrings)
    app.connect('autodoc-process-signature', mangle_signature)
""")

edit("doc/sphinxext/numpy_ext/numpydoc.py",
     "import sys # Only needed to check Python version\n", "")

# ``dict.iteritems`` is the Python-2 spelling of ``dict.items``.
sweep("dict.iteritems() -> dict.items()",
      lambda t: re.subn(r"\.iteritems\(\)", ".items()", t),
      2)


# ================================================================ 6. the notebooks
#
# ruff and the scanners read notebooks natively, and so does anyone following
# the examples. Each edit is against the raw JSON so that nothing else about
# the file -- cell ids, outputs, execution counts, key order -- moves.

for relpath, old, new, count in [
    ("doc/examples/decomposition/SelectivePCA weighting.ipynb",
     '    "from __future__ import print_function\\n",\n', "", 1),
    ("doc/examples/machine_learning_demo/ml demo.ipynb",
     '    "from __future__ import print_function, division\\n",\n', "", 1),
    ("doc/examples/pipeline/skutil grid demo.ipynb",
     '    "from __future__ import print_function\\n",\n', "", 1),
    ("doc/examples/machine_learning_demo/ml demo.ipynb",
     '"from sklearn.cross_validation import train_test_split\\n"',
     '"from sklearn.model_selection import train_test_split\\n"', 1),
    ("doc/examples/machine_learning_demo/ml demo.ipynb",
     '"from sklearn.cross_validation import KFold\\n"',
     '"from sklearn.model_selection import KFold\\n"', 1),
    ("doc/examples/machine_learning_demo/ml demo.ipynb",
     "`sklearn.cross_validation.train_test_split`",
     "`sklearn.model_selection.train_test_split`", 1),
    ("doc/examples/pipeline/skutil grid demo.ipynb",
     '"from sklearn.cross_validation import train_test_split\\n"',
     '"from sklearn.model_selection import train_test_split\\n"', 1),
    ("doc/examples/pipeline/skutil grid demo.ipynb",
     '"from sklearn.cross_validation import KFold\\n"',
     '"from sklearn.model_selection import KFold\\n"', 1),
    ("doc/examples/pipeline/skutil grid demo.ipynb",
     '"from sklearn.externals import joblib\\n"', '"import joblib\\n"', 1),
    # ``KFold`` moved from a length-and-fold-count constructor to a splitter.
    ("doc/examples/machine_learning_demo/ml demo.ipynb",
     "custom_cv = KFold(n=y_train.shape[0], n_folds=5, shuffle=True, random_state=42)",
     "custom_cv = KFold(n_splits=5, shuffle=True, random_state=42)", 3),
    ("doc/examples/pipeline/skutil grid demo.ipynb",
     "custom_cv = KFold(n=y_train.shape[0], n_folds=5, shuffle=True, random_state=42)",
     "custom_cv = KFold(n_splits=5, shuffle=True, random_state=42)", 1),
]:
    edit(relpath, old, new, count)


# ===================================== 7. what the renames left pointing nowhere
#
# Two feature probes that ask whether an object supports an API, written in
# terms of the spelling that has just gone. Both answer "no" now and fall
# through to a slower branch that returns a different type -- the failure a
# rename makes when the rename is only half of it.

edit("skutil/utils/fixes.py", """\
    elif hasattr(y, 'as_matrix'):
        return y.values""", """\
    elif isinstance(y, (pd.Series, pd.DataFrame, pd.Index)):
        return y.values""")

# ``seaborn.plt`` was seaborn re-exporting pyplot, and it stopped doing that in
# 0.9. The module already imports pyplot under the name the rest of the file
# uses.
edit("skutil/utils/util.py", "        sns.plt.show()\n", "        plt.show()\n", count=2)


# ================================= 8. the estimator contract sklearn now enforces
#
# ``BaseEstimator.get_params`` used to read each ``__init__`` parameter with a
# default -- ``getattr(self, key, None)`` -- so an estimator that never stored
# one reported it as ``None`` and nobody noticed. 0.22 dropped the default and
# reads the attribute outright, so the same estimator now raises
# ``AttributeError`` the first time anything asks it for its parameters, which
# includes ``repr``, ``clone`` and every grid search.
#
# sklearn's rule is the one the docs have always stated: every ``__init__``
# keyword is stored on the instance under its own name, unmodified.

# The three balancers take the response column as ``y`` and stored it as
# ``y_``, which is the spelling for a *fitted* attribute, not a parameter.
edit("skutil/preprocessing/balance.py", "        self.y_ = y\n",
     "        self.y = y\n")
sweep("self.y_ -> self.y",
      lambda t: re.subn(r"self\.y_\b", "self.y", t),
      6)

# ``QRDecomposition`` never stored ``X`` at all, and stored ``pivot`` only
# long enough for ``_decompose`` to overwrite it (next edit).
edit("skutil/odr/dqrutl.py", """\
    def __init__(self, X, pivot=1):
        self.job_ = 0 if not pivot else 1""", """\
    def __init__(self, X, pivot=1):
        self.X = X
        self.pivot = pivot
        self.job_ = 0 if not pivot else 1""")

# ...and it overwrote its other parameter. ``pivot`` arrives as a flag -- 1 or
# 0 -- and ``_decompose`` immediately rebinds ``self.pivot`` to the pivot
# *vector* LINPACK returns. Under scikit-learn 0.20 that made
# ``get_params()["pivot"]`` an ndarray where a flag was passed; under 0.24.2 it
# makes ``clone`` call ``QRDecomposition(X, pivot=array([0,1,2,3]))``, and
# ``0 if not pivot else 1`` on a four-element array raises ValueError. The flag
# stays under its own name and the vector moves to ``pivot_``, which is the
# spelling scikit-learn reserves for exactly this: something the constructor
# computed rather than something it was handed.
edit("skutil/odr/dqrutl.py",
     "        self.qr, self.rank, self.qraux, self.pivot = "
     "qr_decomposition(X, self.job_)\n",
     "        self.qr, self.rank, self.qraux, self.pivot_ = "
     "qr_decomposition(X, self.job_)\n")
edit("skutil/odr/dqrutl.py",
     "        #   cf[self.pivot[np.arange(k)], :] = coef\n"
     "        return coef if not k < p else coef[self.pivot[np.arange(k)], :]",
     "        #   cf[self.pivot_[np.arange(k)], :] = coef\n"
     "        return coef if not k < p else coef[self.pivot_[np.arange(k)], :]")
edit("skutil/odr/dqrutl.py",
     "    pivot : array_like, shape (n_features,)\n"
     "        The pivots, if pivot was set to 1, else None\n",
     "    pivot_ : array_like, shape (n_features,)\n"
     "        The pivots, if pivot was set to 1, else None\n")
edit("skutil/feature_selection/combos.py",
     "        pivot = decomp.pivot         # the pivot vector\n",
     "        pivot = decomp.pivot_        # the pivot vector\n")


print("== step 2 semantic migration")
for line in LEDGER:
    print("   " + line)
PYEOF

expect_count "after the semantic layer" 302

# --------------------------------------------------------------------- layer 2
# `--isolated` so no pyproject.toml in the tree can widen or narrow the
# selection, `--ignore-noqa` so a `# noqa` cannot hide a finding from the fix
# pass that the gate would still count, `--no-respect-gitignore` so the doc
# tree is not skipped. The same four flags the gate uses.
#
# Safe first, then unsafe, and never the two in one invocation: the unsafe
# pass rewrites constructs the safe pass has already simplified, and running
# it first produces a different -- still correct, but different -- tree.
#
# `ruff check --fix` exits 1 while findings remain, which is the normal case
# here, so the exit status is not the signal. `expect_count` is.
echo "   ruff fix (safe)"
ruff check --isolated --no-cache --ignore-noqa --no-respect-gitignore \
    --target-version "$TARGET" --select "$SELECT" --fix . >/dev/null 2>&1 || true
expect_count "after the safe fixes" 208

echo "   ruff fix (unsafe)"
ruff check --isolated --no-cache --ignore-noqa --no-respect-gitignore \
    --target-version "$TARGET" --select "$SELECT" --fix --unsafe-fixes . \
    >/dev/null 2>&1 || true
expect_count "after the unsafe fixes" 43

# --------------------------------------------------------------------- layer 3
python - "$ROOT" <<'PYEOF'
# -*- coding: utf-8 -*-
"""What ruff finds but will not fix, for step 2's selection.

``ruff check --fix`` and then ``--fix --unsafe-fixes`` clear 259 of the 302
findings the step-2 selection reports on the semantically-migrated tree. The 43
they leave are two rules ruff declines to rewrite at all, and it declines for a
reason in both cases:

``PD003``  ``isnull`` is an alias of ``isna``, but ruff cannot know that the
           receiver is a pandas object rather than something else that happens
           to have the method, so there is no safe automatic edit. Every site
           in this tree is a ``DataFrame``/``Series``/``pd.isnull`` call.

``UP031``  ``'%i' % x`` truncates a float; ``f'{x}'`` does not. ruff will not
           silently change that. Every argument here is an integer -- a shape,
           a length, a class count or a ``range`` index -- so the two spellings
           produce the same characters.

Layer 3 of steps/02-pandas1-numpy12/solution/solve.sh, which embeds this
file verbatim. Also runnable on its own:

    python evidence/item4/step02-residue.py /path/to/tree
"""
import os
import re
import sys

ROOT = os.path.abspath(sys.argv[1])
LEDGER = []


def read(relpath):
    with open(os.path.join(ROOT, relpath), encoding="utf-8") as fh:
        return fh.read()


def write(relpath, text):
    with open(os.path.join(ROOT, relpath), "w", encoding="utf-8") as fh:
        fh.write(text)


def die(msg):
    print("  FAILED: " + msg)
    raise SystemExit(1)


def edit(relpath, old, new, count=1):
    text = read(relpath)
    n = text.count(old)
    if n != count:
        die("%s: expected %d occurrence(s) of %r, found %d"
            % (relpath, count, old[:70], n))
    write(relpath, text.replace(old, new))
    LEDGER.append("edit  %-46s %d site(s)" % (relpath, count))


def walk(suffix):
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames
                       if d not in (".git", "build", "dist", "__pycache__",
                                    ".ipynb_checkpoints")]
        for name in sorted(filenames):
            if name.endswith(suffix):
                yield os.path.relpath(os.path.join(dirpath, name), ROOT)


def sweep(name, fn, expected, suffix=".py"):
    total, files = 0, 0
    for relpath in walk(suffix):
        text = read(relpath)
        new_text, n = fn(text)
        if n:
            write(relpath, new_text)
            total += n
            files += 1
    if total != expected:
        die("sweep %s: expected %d sites, touched %d" % (name, expected, total))
    LEDGER.append("sweep %-46s %3d site(s) over %2d file(s)" % (name, total, files))


# ------------------------------------------------------------------- PD003
# ``isnull``/``notnull`` were kept as aliases when pandas 0.21 introduced
# ``isna``/``notna``; pandas-vet wants the surviving spelling. Comments are
# rewritten too -- one in `filter_collinearity` explains the branch below it in
# terms of the call it names, and leaving the two out of step is worse than the
# churn.
sweep("isnull -> isna", lambda t: re.subn(r"\bisnull\(", "isna(", t), 22)
sweep("isnull -> isna (notebooks)",
      lambda t: re.subn(r"\bisnull\(", "isna(", t), 1, suffix=".ipynb")


# ------------------------------------------------------------------- UP031
for relpath, old, new, count in [
    # A printf template built out of two widths. The result is still consumed
    # as a printf template one line below, so only the outer format changes.
    ("doc/sphinxext/numpy_ext/docscrape_sphinx.py",
     "fmt = '%%%ds  %%%ds  ' % (maxlen_0, maxlen_1)",
     "fmt = f'%{maxlen_0}s  %{maxlen_1}s  '", 1),
    ("doc/sphinxext/numpy_ext/numpydoc.py",
     'new_r = "R%d" % (reference_offset[0] + int(r))',
     'new_r = f"R{reference_offset[0] + int(r)}"', 1),
    ("doc/sphinxext/numpy_ext/numpydoc.py",
     'new_r = "%s%d" % (r, reference_offset[0])',
     'new_r = f"{r}{reference_offset[0]}"', 1),
    ("skutil/decomposition/decompose.py",
     "columns=[('PC%i' % (i + 1)) for i in range(transform.shape[1])]",
     "columns=[f'PC{i + 1}' for i in range(transform.shape[1])]", 1),
    ("skutil/decomposition/decompose.py",
     "('Concept%i' % (i + 1))", "(f'Concept{i + 1}')", 1),
    ("skutil/odr/dqrutl.py",
     "'expected qraux to be of length %i' % p",
     "f'expected qraux to be of length {p}'", 1),
    ("skutil/odr/dqrutl.py",
     "'expected pivot to be of length %i' % p",
     "f'expected pivot to be of length {p}'", 1),
    ("skutil/odr/dqrutl.py",
     "'expected work to be of length %i' % p",
     "f'expected work to be of length {p}'", 1),
    ("skutil/preprocessing/balance.py",
     "'class balancing can only handle <= %i classes, but got %i' % (mc, n_classes)",
     "f'class balancing can only handle <= {mc} classes, but got {n_classes}'", 1),
    ("skutil/preprocessing/encode.py",
     "'Too many factor levels in feature. Max is %i' % unseen",
     "f'Too many factor levels in feature. Max is {unseen}'", 1),
    ("skutil/preprocessing/transform.py",
     "'n_samples should be at least two, but got %i' % m",
     "f'n_samples should be at least two, but got {m}'", 1),
    ("skutil/utils/util.py",
     "return ['V%i' % (i + 1) for i in range(m)]",
     "return [f'V{i + 1}' for i in range(m)]", 1),
    ("skutil/utils/util.py",
     "'max classes is 2, but got %i' % max_len",
     "f'max classes is 2, but got {max_len}'", 1),
    ("skutil/preprocessing/tests/test_impute.py",
     "'expected no nulls but got %i' % null_ct",
     "f'expected no nulls but got {null_ct}'", 3),
    ("skutil/preprocessing/tests/test_impute.py",
     "'expected some missing values but got %i' % null_ct",
     "f'expected some missing values but got {null_ct}'", 1),
    ("skutil/preprocessing/tests/test_transform.py",
     "'expected len == 0, but got %i' % l",
     "f'expected len == 0, but got {l}'", 1),
    ("skutil/preprocessing/tests/test_transform.py",
     "'expected len == 5, but got %i' % l",
     "f'expected len == 5, but got {l}'", 1),
    # The notebook is edited as the JSON text it is, so that every cell it does
    # not name comes back byte-identical.
    ("doc/examples/machine_learning_demo/ml demo.ipynb",
     "print('Train size: %i' % X_train.shape[0])",
     "print(f'Train size: {X_train.shape[0]}')", 1),
    ("doc/examples/machine_learning_demo/ml demo.ipynb",
     "print('Validation size: %i' % X_val.shape[0])",
     "print(f'Validation size: {X_val.shape[0]}')", 1),
    ("doc/examples/machine_learning_demo/ml demo.ipynb",
     "print('Holdout size: %i' % X_test.shape[0])",
     "print(f'Holdout size: {X_test.shape[0]}')", 1),
]:
    edit(relpath, old, new, count)


print("== step 2 residue")
for line in LEDGER:
    print("   " + line)
PYEOF

expect_count "after the residue layer" 0

# ------------------------------------------------------------------- checks
# Everything below reads the tree and asserts; nothing edits it.

python - "$ROOT" <<'PYEOF'
"""Every module in the tree compiles at the step's own syntax level."""
import os
import sys

root = sys.argv[1]
files, failures = 0, []
for dirpath, dirnames, filenames in os.walk(root):
    dirnames[:] = [d for d in dirnames
                   if d not in (".git", "build", "dist", "__pycache__",
                                ".ipynb_checkpoints")
                   and not d.endswith(".egg-info")]
    for name in sorted(filenames):
        if not name.endswith(".py"):
            continue
        path = os.path.join(dirpath, name)
        files += 1
        with open(path, encoding="utf-8") as fh:
            source = fh.read()
        try:
            compile(source, path, "exec", dont_inherit=True)
        except SyntaxError as exc:
            failures.append("%s:%s: %s" % (os.path.relpath(path, root),
                                           exc.lineno, exc.msg))
print("  python modules: %d compile, %d do not" % (files - len(failures),
                                                   len(failures)))
for line in failures:
    print("    " + line)
if failures or files != 58:
    raise SystemExit("solve.sh: expected 58 modules, all compiling")
PYEOF

python - "$ROOT" <<'PYEOF'
"""The three notebooks still hold every cell they held, and still parse.

`ast.parse`, not `compile`: a notebook's cells joined into one module put the
second cell's imports after executable statements, which only bytecode
compilation objects to. Parsing is the question the gate asks.
"""
import ast
import json
import os
import sys

EXPECTED = {
    "doc/examples/decomposition/SelectivePCA weighting.ipynb": (13, 6),
    "doc/examples/machine_learning_demo/ml demo.ipynb": (47, 24),
    "doc/examples/pipeline/skutil grid demo.ipynb": (15, 10),
}

root = sys.argv[1]
seen = {}
for dirpath, dirnames, filenames in os.walk(root):
    dirnames[:] = [d for d in dirnames
                   if d not in (".git", "build", "dist", ".ipynb_checkpoints")]
    for name in sorted(filenames):
        if not name.endswith(".ipynb"):
            continue
        path = os.path.join(dirpath, name)
        rel = os.path.relpath(path, root)
        with open(path, encoding="utf-8") as fh:
            nb = json.load(fh)
        cells = nb["cells"]
        code = [c for c in cells if c.get("cell_type") == "code"]
        lines = []
        for cell in code:
            for line in cell["source"]:
                # IPython magics are not Python. Blanked rather than dropped so
                # a line number still points somewhere.
                if line.lstrip()[:1] in ("%", "!", "?"):
                    lines.append("pass" + ("\n" if line.endswith("\n") else ""))
                else:
                    lines.append(line)
            if lines and not lines[-1].endswith("\n"):
                lines[-1] = lines[-1] + "\n"
        ast.parse("".join(lines), path)
        seen[rel] = (len(cells), len(code))
        print("  notebook %-52s %2d cells (%2d code), parses"
              % (rel, len(cells), len(code)))
if seen != EXPECTED:
    for rel in sorted(set(seen) | set(EXPECTED)):
        if seen.get(rel) != EXPECTED.get(rel):
            print("    %s: %s, expected %s"
                  % (rel, seen.get(rel), EXPECTED.get(rel)))
    raise SystemExit("solve.sh: a notebook gained, lost or dropped cells")
PYEOF

python - "$ROOT" <<'PYEOF'
"""What is left of the ten construct families step 2 retires.

Deliberately not the verifier's instrument. `skutil_gates` answers this
question with staged AST rules and with ruff; re-running either of those here
would only prove that the grader agrees with itself, and the gate's answer is
what the oracle run is for. This asks the same question a different way: each
file is tokenized and every string literal and comment is blanked in place --
so the docstrings and doctests that *talk about* `six` or `as_matrix` are not
counted -- and then the names are looked for in what is left.

The count beside each family is what the entry tree (step 1's golden) has. All
ten must reach zero; one that does not is a layer that quietly did less than it
was written to do.

Final check of steps/02-pandas1-numpy12/solution/solve.sh, which embeds this
file verbatim. Also runnable on its own:

    python evidence/item4/step02-scan.py /path/to/tree
"""
import io
import json
import os
import re
import sys
import tokenize

# (family, entry-tree count, pattern)
#
# Every pattern is wider than the rule that motivated it. `numpy scalar
# aliases` is NPY001, but it also catches `np.long`, which NPY001 does not;
# `interpreter version branches` catches the local alias in skutil/utils/fixes.py
# that a `sys.version_info` matcher walks straight past. A scan that could only
# see what the gate sees would not be an independent check.
PATTERNS = [
    ("vendored six", 26, r"\bsix\b"),
    ("__future__ imports", 32, r"from\s+__future__\s+import"),
    ("py2 builtins", 9,
     r"\b(?:xrange|basestring|unicode|long)\s*\(|\.has_key\s*\("
     r"|\b(?:iteritems|iterkeys|itervalues)\s*\("),
    ("interpreter version branches", 6,
     r"\bsys\.version_info\b|\bVERSION_MAJOR\b"),
    ("sklearn.externals", 12,
     r"\bsklearn\.externals\b|from\s+sklearn\s+import\s+externals"),
    ("pandas .as_matrix/.ix", 31, r"\.as_matrix\s*\(|\.ix\s*\["),
    ("pandas isnull/notnull", 21, r"\b(?:isnull|notnull)\s*\("),
    ("pandas inplace=True", 4, r"\binplace\s*=\s*True\b"),
    ("numpy scalar aliases", 16,
     r"\bnp\.(?:float|int|bool|object|str|unicode|long)\b"),
    ("seaborn.plt", 2, r"\bsns\.plt\b"),
]


def code_only(source):
    """The source with every string literal and comment blanked in place.

    Blanked rather than dropped so a hit still reports the line it is on. A
    file that does not tokenize is returned whole -- it will be reported by the
    compile check above this one, and swallowing it here would hide it twice.
    """
    lines = source.split("\n")
    try:
        tokens = list(tokenize.generate_tokens(io.StringIO(source).readline))
    except (tokenize.TokenError, IndentationError, SyntaxError):
        return source
    for kind, _text, start, end, _line in tokens:
        if kind not in (tokenize.STRING, tokenize.COMMENT):
            continue
        (srow, scol), (erow, ecol) = start, end
        if srow == erow:
            line = lines[srow - 1]
            lines[srow - 1] = line[:scol] + " " * (ecol - scol) + line[ecol:]
        else:
            lines[srow - 1] = lines[srow - 1][:scol]
            for row in range(srow, erow - 1):
                lines[row] = ""
            lines[erow - 1] = " " * ecol + lines[erow - 1][ecol:]
    return "\n".join(lines)


def notebook_code(text):
    out = []
    for cell in json.loads(text).get("cells", []):
        if cell.get("cell_type") != "code":
            continue
        for line in cell.get("source", []):
            out.append("pass\n" if line.lstrip()[:1] in ("%", "!", "?")
                       else line)
        if out and not out[-1].endswith("\n"):
            out[-1] = out[-1] + "\n"
    return "".join(out)


root = sys.argv[1]
hits = dict((name, []) for name, _entry, _pat in PATTERNS)
for dirpath, dirnames, filenames in os.walk(root):
    dirnames[:] = [d for d in dirnames
                   if d not in (".git", "build", "dist", "__pycache__",
                                ".ipynb_checkpoints")
                   and not d.endswith(".egg-info")]
    for name in sorted(filenames):
        if not (name.endswith(".py") or name.endswith(".ipynb")):
            continue
        path = os.path.join(dirpath, name)
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        if name.endswith(".ipynb"):
            text = notebook_code(text)
        for i, line in enumerate(code_only(text).split("\n"), 1):
            for family, _entry, pattern in PATTERNS:
                if re.search(pattern, line):
                    hits[family].append("%s:%d: %s"
                                        % (os.path.relpath(path, root), i,
                                           line.strip()[:88]))

survivors = 0
for family, entry, _pattern in PATTERNS:
    found = hits[family]
    survivors += len(found)
    print("  %-30s %3d left (the entry tree had %3d)"
          % (family, len(found), entry))
    for line in found:
        print("      " + line)
if survivors:
    raise SystemExit("solve.sh: %d step-2 construct(s) survived" % survivors)
PYEOF

echo "--- done"
