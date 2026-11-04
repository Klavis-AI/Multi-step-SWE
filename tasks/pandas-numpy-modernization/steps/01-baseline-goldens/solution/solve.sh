#!/usr/bin/env bash
# Step 1's oracle: give every random draw in the tree a generator of its own,
# and make the two files that do not parse parse.
#
# The step's gates count twenty reads of, and writes to, a process-global random
# stream; nineteen of them are also what ruff calls NPY002. They are removed the
# way a maintainer would remove them -- by threading a `random_state` through the
# four sampling entry points and seeding the test fixtures -- and not by deleting
# the calls, because the same step scores the recorded behaviour of those very
# functions and the repository's own tests.
#
# Two properties make that possible without a single recorded value moving:
#
#   * `sklearn.utils.check_random_state(None)` returns `numpy.random.mtrand._rand`
#     -- the very singleton `np.random.permutation` and `np.random.choice` are
#     bound methods of. A default-constructed balancer therefore draws from the
#     same generator, in the same order, as it did before.
#   * A raw string literal and the literal it replaces are the same bytes, so
#     `r'\*'` changes what Python warns about and nothing else.
#
# Every edit below is an exact-byte replacement with an asserted occurrence
# count. `sed -i` with a loose pattern would "succeed" on a file it did not
# change; this script exits non-zero the moment an anchor is missing, ambiguous,
# or already applied, and the ledger it prints has to end with the same number of
# edits it started with.
set -euo pipefail

ROOT="${1:-/app}"
cd "$ROOT"

python - "$ROOT" <<'PYEOF'
from __future__ import print_function
import io
import os
import sys

ROOT = sys.argv[1]
EDITS = []
LEDGER = []


def edit(relpath, old, new, count=1):
    """One exact-byte replacement, with the number of sites it must match.

    The count is the assertion. A migration script that quietly matched zero
    sites, or matched a second site it was not thinking about, is the failure
    mode this whole file is shaped around.
    """
    EDITS.append((relpath, old, new, count))


def insert_doc(relpath, class_anchor, paragraph):
    """Add a parameter to the docstring of the class ``class_anchor`` opens.

    Anchoring on the ``class`` line and walking forward to whichever comes
    first -- the ``Examples`` header or the end of the docstring -- avoids
    quoting the docstring itself: these classes share whole paragraphs
    verbatim, some of those lines carry trailing whitespace, and an anchor that
    has to reproduce it is an anchor that breaks silently.
    """
    EDITS.append((relpath, class_anchor, paragraph, "doc"))


# --------------------------------------------------------------- skutil/__init__
# The hook draws a seed from the global generator it is about to overwrite, then
# reseeds numpy's and the stdlib's from it -- so importing a test module used to
# move the caller's stream to a place nothing recorded. Nothing in the package
# calls it: it is a leftover from the nose runner, and reproducibility now comes
# from the `random_state` arguments the rest of this script adds.
edit("skutil/__init__.py", """

def setup_module(module):
    import numpy as np
    import random

    _random_seed = int(np.random.uniform() * (2 ** 31 - 1))
    np.random.seed(_random_seed)
    random.seed(_random_seed)
""", '''

# ``setup_module`` used to live here. The nose-era hook drew a seed from the
# global generator it was about to overwrite and reseeded both numpy's and the
# stdlib's from it, so importing a test module moved the caller's stream
# somewhere nothing could predict. Reproducibility is a parameter now: see the
# ``random_state`` arguments on ``skutil.utils.shuffle_dataframe`` and on the
# balancers in ``skutil.preprocessing.balance``.
''')

# ------------------------------------------------------------ skutil/utils/fixes
# `Sized` moved to collections.abc in 3.3 and the alias warns from 3.3 on. No
# try/except fallback: the fallback branch is the same deprecated import, and
# this tree no longer runs on an interpreter that needs it.
edit("skutil/utils/fixes.py",
     "from collections import namedtuple, Sized\n",
     "from collections import namedtuple\n"
     "from collections.abc import Sized\n")

# ------------------------------------------------------------- skutil/utils/util
edit("skutil/utils/util.py",
     "from sklearn.metrics import confusion_matrix as cm\n",
     "from sklearn.metrics import confusion_matrix as cm\n"
     "from sklearn.utils import check_random_state\n")

edit("skutil/utils/util.py", '''def shuffle_dataframe(X):
    """Shuffle the rows in a data frame without replacement.
    The random state used for shuffling is controlled by
    numpy's random state.

    Parameters
    ----------

    X : pd.DataFrame, shape=(n_samples, n_features)
        The dataframe to shuffle
    """
    X, _ = validate_is_pd(X, None, False)
    return X.iloc[np.random.permutation(np.arange(X.shape[0]))]
''', '''def shuffle_dataframe(X, random_state=None):
    """Shuffle the rows in a data frame without replacement.

    Parameters
    ----------

    X : pd.DataFrame, shape=(n_samples, n_features)
        The dataframe to shuffle

    random_state : int, ``RandomState`` or None, optional (default=None)
        The generator the permutation is drawn from. None means the
        process-global generator, which is the only one this function
        could use before the parameter existed.
    """
    X, _ = validate_is_pd(X, None, False)
    random_state = check_random_state(random_state)
    return X.iloc[random_state.permutation(np.arange(X.shape[0]))]
''')

# -------------------------------------------------------- skutil/preprocessing/balance
BAL = "skutil/preprocessing/balance.py"

edit(BAL,
     "from numpy.random import choice\n"
     "from sklearn.externals import six\n"
     "from sklearn.neighbors import NearestNeighbors\n",
     "from sklearn.externals import six\n"
     "from sklearn.neighbors import NearestNeighbors\n"
     "from sklearn.utils import check_random_state\n")

edit(BAL, '''def _default_indices(length, shuffle):
    x = np.arange(length)
    return x.tolist() if not shuffle else np.random.permutation(x).tolist()
''', '''def _default_indices(length, shuffle, random_state=None):
    x = np.arange(length)
    random_state = check_random_state(random_state)
    return x.tolist() if not shuffle else random_state.permutation(x).tolist()
''')

insert_doc(BAL,
           "class _BaseBalancePartitioner(",
           "    random_state : int, ``RandomState`` or None, optional (default=None)\n"
           "        The generator the sampled rows are drawn from. None means the\n"
           "        process-global generator, which is what this class used before\n"
           "        the parameter existed.")

edit(BAL, '''    @abc.abstractmethod
    def __init__(self, X, y_name, ratio, validation_function=_validate_x_y_ratio):
        self.X = X
        self.y = y_name
        self.ratio = ratio
''', '''    @abc.abstractmethod
    def __init__(self, X, y_name, ratio, validation_function=_validate_x_y_ratio,
                 random_state=None):
        self.X = X
        self.y = y_name
        self.ratio = ratio
        self.random_state = check_random_state(random_state)
''')

edit(BAL, '''    def __init__(self, X, y_name, ratio, validation_function=_validate_x_y_ratio):
        super(_OversamplingBalancePartitioner, self).__init__(
            X, y_name, ratio, validation_function)
''', '''    def __init__(self, X, y_name, ratio, validation_function=_validate_x_y_ratio,
                 random_state=None):
        super(_OversamplingBalancePartitioner, self).__init__(
            X, y_name, ratio, validation_function, random_state)
''')

edit(BAL, '''    def __init__(self, X, y_name, ratio, validation_function=_validate_x_y_ratio):
        super(_UndersamplingBalancePartitioner, self).__init__(
            X, y_name, ratio, validation_function)
''', '''    def __init__(self, X, y_name, ratio, validation_function=_validate_x_y_ratio,
                 random_state=None):
        super(_UndersamplingBalancePartitioner, self).__init__(
            X, y_name, ratio, validation_function, random_state)
''')

# Both partitioners take the no-balancing-needed exit through the same call.
edit(BAL,
     "            return _default_indices(self.X.shape[0], shuffle)\n",
     "            return _default_indices(self.X.shape[0], shuffle,\n"
     "                                    self.random_state)\n",
     count=2)

edit(BAL,
     "            idcs = choice(minority_recs, n_samples, replace=True)\n",
     "            idcs = self.random_state.choice(minority_recs, n_samples,\n"
     "                                            replace=True)\n")

edit(BAL,
     "        out = sorted(all_indices) if not shuffle else "
     "[j for j in np.random.permutation(all_indices)]\n",
     "        out = sorted(all_indices) if not shuffle else "
     "[j for j in self.random_state.permutation(all_indices)]\n")

edit(BAL,
     "        idcs = choice(majority_recs, n_required, replace=False)\n",
     "        idcs = self.random_state.choice(majority_recs, n_required,\n"
     "                                        replace=False)\n")

edit(BAL,
     "        out = sorted(minorities) if not shuffle else "
     "[j for j in np.random.permutation(minorities)]\n",
     "        out = sorted(minorities) if not shuffle else "
     "[j for j in self.random_state.permutation(minorities)]\n")

# The balancer stores the seed unresolved, the way sklearn estimators do, so
# that `get_params`/`clone` round-trip it and two balancers built from the same
# seed balance the same frame the same way. The partitioner, which lives for one
# call, is where it becomes a generator.
BALANCER_DOC = (
    "    random_state : int, ``RandomState`` or None, optional (default=None)\n"
    "        The generator the sampled rows are drawn from, stored unresolved so\n"
    "        that ``get_params`` round-trips it. None means the process-global\n"
    "        generator, which is what these balancers used before the parameter\n"
    "        existed.")

insert_doc(BAL, "class _BaseBalancer(", BALANCER_DOC)

edit(BAL, '''    def __init__(self, y, ratio=BalancerMixin._def_ratio, shuffle=True, as_df=True):
        self.y_ = y
        self.ratio = ratio
        self.shuffle = shuffle
        self.as_df = as_df
''', '''    def __init__(self, y, ratio=BalancerMixin._def_ratio, shuffle=True,
                 as_df=True, random_state=None):
        self.y_ = y
        self.ratio = ratio
        self.shuffle = shuffle
        self.as_df = as_df
        self.random_state = random_state
''')

edit(BAL, '''def _over_under_balance(X, y, ratio, as_df, shuffle, partitioner_class):
''', '''def _over_under_balance(X, y, ratio, as_df, shuffle, partitioner_class,
                        random_state=None):
''')

edit(BAL,
     "    partitioner = partitioner_class(X, y, ratio)\n",
     "    partitioner = partitioner_class(X, y, ratio,\n"
     "                                    random_state=random_state)\n")

insert_doc(BAL, "class OversamplingClassBalancer(", BALANCER_DOC)

edit(BAL, '''    def __init__(self, y, ratio=BalancerMixin._def_ratio, shuffle=True, as_df=True):
        super(OversamplingClassBalancer, self).__init__(ratio=ratio, y=y,
                                                        shuffle=shuffle,
                                                        as_df=as_df)
''', '''    def __init__(self, y, ratio=BalancerMixin._def_ratio, shuffle=True,
                 as_df=True, random_state=None):
        super(OversamplingClassBalancer, self).__init__(ratio=ratio, y=y,
                                                        shuffle=shuffle,
                                                        as_df=as_df,
                                                        random_state=random_state)
''')

edit(BAL, '''        blnc = _over_under_balance(X=X, y=self.y_, ratio=self.ratio,
                                   shuffle=self.shuffle, as_df=self.as_df,
                                   partitioner_class=_OversamplingBalancePartitioner)
''', '''        blnc = _over_under_balance(X=X, y=self.y_, ratio=self.ratio,
                                   shuffle=self.shuffle, as_df=self.as_df,
                                   partitioner_class=_OversamplingBalancePartitioner,
                                   random_state=self.random_state)
''')

insert_doc(BAL, "class SMOTEClassBalancer(", BALANCER_DOC)

edit(BAL, '''    def __init__(self, y, ratio=BalancerMixin._def_ratio, shuffle=True, k=3, as_df=True):
        super(SMOTEClassBalancer, self).__init__(ratio=ratio, y=y,
                                                 shuffle=shuffle,
                                                 as_df=as_df)
        self.k = k
''', '''    def __init__(self, y, ratio=BalancerMixin._def_ratio, shuffle=True, k=3,
                 as_df=True, random_state=None):
        super(SMOTEClassBalancer, self).__init__(ratio=ratio, y=y,
                                                 shuffle=shuffle,
                                                 as_df=as_df,
                                                 random_state=random_state)
        self.k = k
''')

# SMOTE does its own partitioning inline, so it resolves the seed once here and
# every draw in the method -- the minority sample and both shuffles -- comes off
# that one generator, in the order it always did.
edit(BAL, '''        X.index = np.arange(0, X.shape[0])
        ratio = self.ratio
''', '''        X.index = np.arange(0, X.shape[0])
        ratio = self.ratio
        random_state = check_random_state(self.random_state)
''')

edit(BAL,
     "            return X if not self.shuffle else shuffle_dataframe(X)\n",
     "            return X if not self.shuffle else shuffle_dataframe(\n"
     "                X, random_state=random_state)\n")

edit(BAL,
     "            idcs = choice(minority_recs.index, n_samples, replace=replace)\n",
     "            idcs = random_state.choice(minority_recs.index, n_samples,\n"
     "                                       replace=replace)\n")

edit(BAL,
     "        X = X if not self.shuffle else shuffle_dataframe(X)\n",
     "        X = X if not self.shuffle else shuffle_dataframe(\n"
     "            X, random_state=random_state)\n")

insert_doc(BAL, "class UndersamplingClassBalancer(", BALANCER_DOC)

edit(BAL, '''    def __init__(self, y, ratio=0.2, shuffle=True, as_df=True):
        super(UndersamplingClassBalancer, self).__init__(ratio=ratio, y=y,
                                                         shuffle=shuffle,
                                                         as_df=as_df)
''', '''    def __init__(self, y, ratio=0.2, shuffle=True, as_df=True,
                 random_state=None):
        super(UndersamplingClassBalancer, self).__init__(ratio=ratio, y=y,
                                                         shuffle=shuffle,
                                                         as_df=as_df,
                                                         random_state=random_state)
''')

edit(BAL, '''        blnc = _over_under_balance(X=X, y=self.y_, ratio=self.ratio,
                                   shuffle=self.shuffle, as_df=self.as_df,
                                   partitioner_class=_UndersamplingBalancePartitioner)
''', '''        blnc = _over_under_balance(X=X, y=self.y_, ratio=self.ratio,
                                   shuffle=self.shuffle, as_df=self.as_df,
                                   partitioner_class=_UndersamplingBalancePartitioner,
                                   random_state=self.random_state)
''')

# ------------------------------------------------------------------ test fixtures
# A test whose fixture is a fresh random frame every run is a test that fails on
# some runs and not others. Each file gets its own seeded generator; the
# distributions the tests assert on are unchanged.
IMP = "skutil/preprocessing/tests/test_impute.py"

edit(IMP,
     "from numpy.random import choice\n",
     "from numpy.random import RandomState\n")

edit(IMP, '''def _random_X(m, n, cols):
    return pd.DataFrame.from_records(
        data=np.random.rand(m, n),
        columns=cols)
''', '''def _random_X(m, n, cols, seed=42):
    return pd.DataFrame.from_records(
        data=RandomState(seed).rand(m, n),
        columns=cols)
''')

edit(IMP,
     "    Z = _random_X(200, 10, nms)\n",
     "    Z = _random_X(200, 10, nms, seed=43)\n")

edit(IMP, '''    def fill_with_nas(x):
        # make some of them NaN - only 10% tops
        ceil = int(x.shape[0] * 0.1)
        for col in nms:
            n_missing = max(1, choice(ceil))  # at least one in each
            missing_idcs = choice(range(x.shape[0]), n_missing)
''', '''    def fill_with_nas(x, seed=7):
        # make some of them NaN - only 10% tops
        prng = RandomState(seed)
        ceil = int(x.shape[0] * 0.1)
        for col in nms:
            n_missing = max(1, prng.choice(ceil))  # at least one in each
            missing_idcs = prng.choice(range(x.shape[0]), n_missing)
''')

edit(IMP,
     "    fill_with_nas(Z)\n",
     "    fill_with_nas(Z, seed=8)\n")

edit(IMP,
     "    rands = np.random.rand(X.shape[0])\n",
     "    rands = RandomState(42).rand(X.shape[0])\n")

edit(IMP,
     "    f = ['a' if choice(4) % 2 == 0 else 'b' for i in range(X.shape[0])]\n",
     "    prng = RandomState(42)\n"
     "    f = ['a' if prng.choice(4) % 2 == 0 else 'b' for i in range(X.shape[0])]\n")

TRF = "skutil/preprocessing/tests/test_transform.py"

edit(TRF,
     "from numpy.testing import (assert_array_equal, assert_array_almost_equal)\n",
     "from numpy.random import RandomState\n"
     "from numpy.testing import (assert_array_equal, assert_array_almost_equal)\n")

edit(TRF,
     "    assert_fails(BoxCoxTransformer().fit, ValueError, np.random.rand(1, 5))\n",
     "    assert_fails(BoxCoxTransformer().fit, ValueError, "
     "RandomState(42).rand(1, 5))\n")

# `'[\$,%]'` and `r'[\$,%]'` are the same four characters; only one of them
# makes Python warn about an escape it does not define.
edit(TRF,
     r"""        return i.replace('[\$,%]', '', regex=True).astype(float)""",
     r"""        return i.replace(r'[\$,%]', '', regex=True).astype(float)""")

edit(TRF, '''    x = np.random.rand(m, n)

    # make some random
    mask = np.random.rand(m, n) % 2 < 0.5
''', '''    prng = RandomState(42)
    x = prng.rand(m, n)

    # make some random
    mask = prng.rand(m, n) % 2 < 0.5
''')

BIG = "skutil/tests/test_big.py"

edit(BIG,
     "from numpy.testing import (assert_array_equal)\n",
     "from numpy.random import RandomState\n"
     "from numpy.testing import (assert_array_equal)\n")

edit(BIG, '''# generate a totally random matrix
X = np.random.rand(500, 25)  # kind of large...
''', '''RANDOM_SEED = 42
_prng = RandomState(RANDOM_SEED)

# generate a totally random matrix
X = _prng.rand(500, 25)  # kind of large...
''')

edit(BIG,
     "y = factorize(np.random.rand(X.shape[0]))\n",
     "y = factorize(_prng.rand(X.shape[0]))\n")

edit(BIG,
     "X_train, X_test, y_train, y_test = train_test_split(X, y, train_size=0.75)\n",
     "X_train, X_test, y_train, y_test = train_test_split(\n"
     "    X, y, train_size=0.75, random_state=RANDOM_SEED)\n")

# ------------------------------------------------------------------------- doc
# Two Python-2 print statements are the whole reason doc/conf.py does not parse.
edit("doc/conf.py",
     '''print "\\n\\n" + str(sys.path) + "\\n\\n"\n''',
     '''print("\\n\\n" + str(sys.path) + "\\n\\n")\n''')

edit("doc/conf.py",
     "print version, release\n",
     "print(version, release)\n")

DOCSCRAPE = "doc/sphinxext/numpy_ext/docscrape.py"

edit(DOCSCRAPE,
     r"""        if re.compile('^([\w., ]+=)?\s*[\w\.]+\(.*\)$').match(summary_str):""",
     r"""        if re.compile(r'^([\w., ]+=)?\s*[\w\.]+\(.*\)$').match(summary_str):""")

edit(DOCSCRAPE,
     r""".replace('*', '\*')""",
     r""".replace('*', r'\*')""",
     count=3)

# The notebook is edited as the JSON text it is: every cell but the four source
# lines below has to come out byte-identical, and a json round-trip would
# renumber, reindent and reorder it.
NB = "doc/examples/pipeline/skutil grid demo.ipynb"
for _call, _tail in (("pipe.predict(X_train))", '\\n",'),
                     ("search.predict(X_train))", '\\n",')):
    edit(NB,
         '"print \'Train RF accuracy: %.5f\' % accuracy_score(y_train, '
         + _call + _tail,
         '"print(\'Train RF accuracy: %.5f\' % accuracy_score(y_train, '
         + _call + ")" + _tail)
edit(NB,
     '"print \'Test RF accuracy: %.5f\'  % accuracy_score(y_test,  '
     'pipe.predict(X_test))"',
     '"print(\'Test RF accuracy: %.5f\'  % accuracy_score(y_test,  '
     'pipe.predict(X_test)))"')
edit(NB,
     '"print \'Test RF accuracy: %.5f\' % accuracy_score(y_test, '
     'search.predict(X_test))"',
     '"print(\'Test RF accuracy: %.5f\' % accuracy_score(y_test, '
     'search.predict(X_test)))"')


# ---------------------------------------------------------------------- apply
def read(path):
    fh = io.open(path, encoding="utf-8")
    try:
        return fh.read()
    finally:
        fh.close()


def write(path, text):
    fh = io.open(path, "w", encoding="utf-8")
    try:
        fh.write(text)
    finally:
        fh.close()


def apply_doc(text, anchor, paragraph):
    at = text.find(anchor)
    if at < 0:
        raise AssertionError("class anchor not found: %r" % anchor[:60])
    if text.find(anchor, at + 1) >= 0:
        raise AssertionError("class anchor is ambiguous: %r" % anchor[:60])
    stops = [text.find(m, at) for m in ('\n    Examples\n', '\n    """\n')]
    stops = [i for i in stops if i >= 0]
    if not stops:
        raise AssertionError("no docstring below: %r" % anchor[:60])
    cut = min(stops)
    head = text[:cut].rstrip()
    if text.startswith('\n    """', cut):
        # numpydoc runs the last parameter straight into the closing quotes
        return head + "\n\n" + paragraph + text[cut:]
    return head + "\n\n" + paragraph + "\n" + text[cut:]


cache = {}
failures = []
for relpath, old, new, count in EDITS:
    path = os.path.join(ROOT, relpath)
    if relpath not in cache:
        cache[relpath] = read(path)
    text = cache[relpath]
    before = len(text)
    try:
        if count == "doc":
            text = apply_doc(text, old, new)
            got = 1
        else:
            got = text.count(old)
            if got != count:
                raise AssertionError(
                    "expected %d occurrence(s), found %d" % (count, got))
            text = text.replace(old, new)
    except AssertionError as exc:
        failures.append("%s: %s | anchor=%r" % (relpath, exc, old[:70]))
        continue
    cache[relpath] = text
    LEDGER.append((relpath, got, len(text) - before))

for relpath, text in sorted(cache.items()):
    write(os.path.join(ROOT, relpath), text)

width = max(len(r) for r, _, _ in LEDGER) if LEDGER else 10
for relpath, got, delta in LEDGER:
    print("  edit  %-*s  sites=%d  bytes=%+d" % (width, relpath, got, delta))
print("  %d/%d edits applied across %d files"
      % (len(LEDGER), len(EDITS), len(cache)))

if failures:
    for line in failures:
        sys.stderr.write("  FAILED  " + line + "\n")
    raise SystemExit("solve.sh: %d edit(s) did not land" % len(failures))
PYEOF

# The assertions above are about anchors; these are about the result. Each one
# is a number that cannot stay where it was if the edits are real.
echo "--- verifying"

python - "$ROOT" <<'PYEOF'
from __future__ import print_function
import io
import os
import sys

root = sys.argv[1]
bad = []
n = 0
for dirpath, dirnames, filenames in os.walk(root):
    dirnames[:] = [d for d in dirnames
                   if d not in (".git", "build", "dist", "__pycache__")]
    for name in filenames:
        if not name.endswith(".py"):
            continue
        path = os.path.join(dirpath, name)
        fh = io.open(path, encoding="utf-8")
        try:
            src = fh.read()
        finally:
            fh.close()
        n += 1
        try:
            compile(src, path, "exec")
        except SyntaxError as exc:
            bad.append("%s: %s" % (path, exc))
print("  %d python files compile, %d do not" % (n - len(bad), len(bad)))
for line in bad:
    print("    " + line)
if bad:
    raise SystemExit("solve.sh: the tree does not parse")
PYEOF

python - "$ROOT" <<'PYEOF'
from __future__ import print_function
import ast
import io
import json
import os
import sys

root = sys.argv[1]
# The notebook has to still be a notebook, and still hold every cell it held.
path = os.path.join(root, "doc/examples/pipeline/skutil grid demo.ipynb")
fh = io.open(path, encoding="utf-8")
try:
    nb = json.load(fh)
finally:
    fh.close()
cells = nb["cells"]
code = [c for c in cells if c.get("cell_type") == "code"]
lines = []
for cell in code:
    for line in cell["source"]:
        # IPython magics are not Python. Blanked, not dropped, so a line number
        # still points somewhere.
        if line.lstrip()[:1] in ("%", "!", "?"):
            lines.append("pass" + ("\n" if line.endswith("\n") else ""))
        else:
            lines.append(line)
    if lines and not lines[-1].endswith("\n"):
        lines[-1] = lines[-1] + "\n"
# ``ast.parse``, not ``compile``: a joined notebook puts the second cell's
# ``from __future__`` in the middle of a module, which only bytecode
# compilation objects to. Parsing is the question the gate actually asks.
ast.parse("".join(lines), path)
print("  notebook: %d cells (%d code, %d lines), parses"
      % (len(cells), len(code), len(lines)))
PYEOF

python - "$ROOT" <<'PYEOF'
from __future__ import print_function
import io
import json
import os
import re
import sys
import tokenize

root = sys.argv[1]

# The number that has to change. Twenty sites on the entry tree read from or
# wrote to a process-global random stream; after the edits there must be none.
#
# This is deliberately not the verifier's AST rules. Re-running the grader's own
# instrument here would only prove that the instrument agrees with itself, and
# the gate's answer is what the oracle run is for. What this does instead is
# tokenize each file and blank out every string and comment -- so the six
# docstrings and doctests that *talk about* ``np.random`` are not counted -- and
# then look for the names in what is left.
PAT = re.compile(r"\bnp\.random\.(?!RandomState\b)"
                 r"|\bnumpy\.random\.(?!RandomState\b)"
                 r"|from\s+numpy\.random\s+import\s+(?!RandomState\b)"
                 r"|\brandom\.(?:seed|shuffle|random|randint|choice|uniform)\b")


def code_only(src):
    """The source with every string literal and comment blanked in place.

    Blanked rather than dropped so a hit still reports the line it is on.
    """
    lines = src.split("\n")
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(src).readline))
    except (tokenize.TokenError, IndentationError, SyntaxError):
        return src
    for kind, _text, start, end, _line in toks:
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
    nb = json.loads(text)
    out = []
    for cell in nb.get("cells", []):
        if cell.get("cell_type") != "code":
            continue
        for line in cell.get("source", []):
            if line.lstrip()[:1] in ("%", "!", "?"):
                out.append("pass" + ("\n" if line.endswith("\n") else ""))
            else:
                out.append(line)
        if out and not out[-1].endswith("\n"):
            out[-1] = out[-1] + "\n"
    return "".join(out)


hits = []
for dirpath, dirnames, filenames in os.walk(root):
    dirnames[:] = [d for d in dirnames
                   if d not in (".git", "build", "dist", "__pycache__")]
    for name in filenames:
        if not (name.endswith(".py") or name.endswith(".ipynb")):
            continue
        path = os.path.join(dirpath, name)
        fh = io.open(path, encoding="utf-8")
        try:
            text = fh.read()
        finally:
            fh.close()
        if name.endswith(".ipynb"):
            text = notebook_code(text)
        for i, line in enumerate(code_only(text).split("\n"), 1):
            if PAT.search(line):
                hits.append("%s:%d: %s" % (os.path.relpath(path, root),
                                           i, line.strip()[:90]))
print("  global-generator uses remaining: %d (the entry tree had 20)" % len(hits))
for line in hits:
    print("    " + line)
if hits:
    raise SystemExit("solve.sh: a global draw survived")
PYEOF

echo "--- done"
