#!/usr/bin/env bash
#
# Step 3 golden solution: 02-pandas1-numpy12's tree -> 03-sklearn1.
#
# The entry tree is step 2's golden, written for pandas 1.3.5 / numpy 1.21.6.
# The whole stack moves up here -- pandas 1.5.3, numpy 1.26.4, scipy 1.13.1,
# matplotlib 3.8.4, seaborn 0.13.2 -- but the pandas/numpy *code* migration was
# step 2's work and does not recur; what is new is scikit-learn, 0.24 -> 1.6.1,
# and with it a fresh batch of removals and keyword-only signatures (plus one
# numpy.distutils import-time deprecation that only bites at numpy>=1.23). The
# golden that comes out the
# far end has to (a) import and behave under 1.6.1 exactly as step 1's frozen
# corpus says, minus the 156 differences step 3 declares as flips; (b) give its
# eighteen X-transformers the set_output surface 1.6.1 offers, so the new
# `set-output` gate goes green; and (c) leave the tree clean under ruff's
# flake8-bugbear (B) family, which this step turns on.
#
# Three layers, in the order the tree needs them:
#
#   Layer 1 (semantic).  Everything that changes what the code MEANS. Six
#   behaviour fixes for scikit-learn 1.6.1's API and one for numpy.distutils's
#   import-time deprecation (numpy is 1.26.4 here, and numpy.distutils warns
#   under -X dev since NumPy 1.23), then the set_output graft:
#   get_feature_names_out on eleven classes (the eighteen transformers, several
#   sharing a base) plus, in each transformer's fit, an input-name cache the
#   method reads back. Additive and corpus-neutral -- the cache is a leading-
#   underscore attribute and a method, neither of which the behaviour recorder
#   captures. This layer moves no ruff-B count: the entry tree's 36 B findings
#   are unrelated to any line it touches, and the grafted methods are written to
#   pass ruff (f-strings, not %-format), so the SELECT count is 36 in and 36 out.
#
#   Layer 2 (mechanical).  ruff's own fixers at this step's SELECT. Every one of
#   the 36 B findings that ruff can rewrite is an UNSAFE fix (callable() for a
#   __call__ hasattr, an explicit stacklevel= on a warning, an unused loop
#   variable renamed), so the safe pass is an honest no-op and the unsafe pass
#   clears 23, leaving 13.
#
#   Layer 3 (residue).  The 13 ruff will not touch on its own -- bare-name
#   statements, except-without-from, a mutable default, unused loop variables in
#   code ruff cannot prove safe to rewrite. Each is done by hand in the one form
#   that keeps behaviour identical, and the SELECT count reaches 0.
#
# Then the checks: every module compiles, the three notebooks still parse with
# every cell they held, and an independent scan (its own tokenizer, strings and
# comments blanked) confirms the five retired scikit-learn call forms are gone
# and the set_output surface is fully present.
set -euo pipefail

ROOT="${1:-/app}"
cd "$ROOT"

# ruff's SELECT and target for this step. B is flake8-bugbear (new at step 3);
# the rest are the pandas/numpy/pyupgrade families carried up the ladder.
SELECT='B,NPY001,NPY002,PD002,PD003,PD007,PD008,PD009,PD012,PD015,UP'
TARGET='py39'

want_ruff='ruff 0.14.0'
have_ruff="$(ruff --version 2>/dev/null || true)"
if [ "$have_ruff" != "$want_ruff" ]; then
    echo "solve.sh: expected '$want_ruff' but found '${have_ruff:-nothing}'" >&2
    exit 1
fi

# Count SELECT findings across the tree, exactly as the gate does: isolated
# config, no cache, noqa ignored, gitignore ignored, this step's target. ruff
# exits non-zero whenever it reports findings, so capture its output first (|| true)
# rather than piping it straight through under `set -o pipefail`.
ruff_count() {
    local out
    out=$(ruff check --isolated --no-cache --ignore-noqa \
              --no-respect-gitignore --target-version "$TARGET" \
              --select "$SELECT" --output-format json . 2>/dev/null) || true
    printf '%s' "${out:-[]}" \
        | python -c 'import json,sys; print(len(json.load(sys.stdin)))'
}

# Assert the SELECT count is what this layer must leave, and say so.
expect_count() {
    local label="$1" expected="$2" got
    got="$(ruff_count)"
    if [ "$got" != "$expected" ]; then
        echo "solve.sh: ruff $label expected $expected finding(s), got $got" >&2
        exit 1
    fi
    echo "ruff $label $got finding(s)"
}

echo "== step 3 oracle: $ROOT"
expect_count "entry tree" 36

# ---------------------------------------------------------------------------
# Layer 1 -- semantic. Six behaviour fixes, then the set_output graft.
# ---------------------------------------------------------------------------
python - "$ROOT" <<'PYEOF'
import io, sys
root = sys.argv[1]
LEDGER = []

def read(rel):
    return io.open(root + "/" + rel, encoding="utf-8").read()

def write(rel, text):
    io.open(root + "/" + rel, "w", encoding="utf-8").write(text)

def die(msg):
    sys.stderr.write("solve.sh Layer 1: " + msg + "\n")
    sys.exit(1)

def edit(rel, old, new, count=1):
    """Replace `old` with `new` in `rel`, asserting it appears exactly `count`
    times. A drifted anchor is a changed tree, not a silent no-op."""
    s = read(rel)
    n = s.count(old)
    if n != count:
        die("%s: anchor matched %d time(s), need %d\n    %r" % (rel, n, count, old[:70]))
    write(rel, s.replace(old, new))
    LEDGER.append("edit %-34s x%d" % (rel, count))

# ---- the six scikit-learn 1.6.1 behaviour fixes ----------------------------

# 1. load_boston left scikit-learn in 1.2. Drop it from the top import; import
#    it lazily where it is actually called so importing skutil no longer fails.
edit("skutil/utils/util.py",
     "from sklearn.datasets import load_iris, load_breast_cancer, load_boston\n",
     "from sklearn.datasets import load_iris, load_breast_cancer\n")
edit("skutil/utils/util.py",
     "    bo = load_boston()\n",
     "    from sklearn.datasets import load_boston  # removed from scikit-learn>=1.2\n"
     "    bo = load_boston()\n")

# 2. _fit_and_score's scorer/train/test/verbose/parameters are keyword-only in
#    1.6.1, and score_params is required.
edit("skutil/utils/fixes.py",
"""            clone(base_estimator), X, y, scorer,
            train, test, verbose, parameters,
            fit_params=fit_params,
            return_train_score=False,""",
"""            clone(base_estimator), X, y,
            scorer=scorer, train=train, test=test,
            verbose=verbose, parameters=parameters,
            fit_params=fit_params, score_params=None,
            return_train_score=False,""")

# 3. check_cv takes classifier by keyword.
edit("skutil/utils/fixes.py",
     "return check_cv(cv, X, y, classifier) if not SK18 else check_cv(cv, y, classifier)",
     "return check_cv(cv, X, y, classifier) if not SK18 else check_cv(cv, y, classifier=classifier)")

# 4. BaseSearchCV.fit takes groups by keyword (both search subclasses).
edit("skutil/grid_search.py",
     "return super().fit(X, _as_numpy(y), groups)",
     "return super().fit(X, _as_numpy(y), groups=groups)", count=2)

# 5. the bagging model's base_estimator= became estimator= (the public skutil
#    parameter stays base_estimator; only the call into the sklearn model moves).
edit("skutil/preprocessing/impute.py",
     "base_estimator=self.base_estimator,",
     "estimator=self.base_estimator,")

# 6. numpy.distutils is deprecated since NumPy 1.23 and warns the moment it is
#    imported under -X dev. It is a build-time dependency only: get_blas_info()
#    configures the C extensions from setup.py and is never called at run time.
#    Two modules import it at module scope and so drag the warning into every
#    `import skutil.*` -- and because numpy emits it once per process, whichever
#    module the import walk reaches first owns it, so both have to move or the
#    warning simply relocates. Push each import down into the function that uses
#    it: the tree is warning-clean under -X dev, and the build still resolves
#    numpy.distutils when configuration() actually runs. The import lines keep
#    their exact text (only their indentation changes), so no source-shape row
#    that hashes stripped lines sees a difference.
edit("skutil/_build_utils/__init__.py",
     "from numpy.distutils.system_info import get_info\n\nDEFAULT_ROOT",
     "DEFAULT_ROOT")
edit("skutil/_build_utils/__init__.py",
     "def get_blas_info():\n    def atlas_not_found(blas_info_):",
     "def get_blas_info():\n"
     "    # numpy.distutils is deprecated (NumPy>=1.23); import it lazily so\n"
     "    # importing this module stays warning-free under -X dev.\n"
     "    from numpy.distutils.system_info import get_info\n\n"
     "    def atlas_not_found(blas_info_):")
edit("skutil/metrics/setup.py",
     "import numpy\nfrom numpy.distutils.misc_util import Configuration\n"
     "from skutil._build_utils import get_blas_info\n",
     "import numpy\nfrom skutil._build_utils import get_blas_info\n")
edit("skutil/metrics/setup.py",
     'def configuration(parent_package="", top_path=None):\n'
     '    config = Configuration("metrics", parent_package, top_path)',
     'def configuration(parent_package="", top_path=None):\n'
     '    # numpy.distutils is deprecated (NumPy>=1.23); import it lazily.\n'
     '    from numpy.distutils.misc_util import Configuration\n'
     '    config = Configuration("metrics", parent_package, top_path)')

# ---- the set_output graft: get_feature_names_out + input-name caches --------
#
# scikit-learn's set_output is @available_if(get_feature_names_out present).
# skutil's transformers have the auto-wrap keys but lacked the method, so
# set_output was None. Adding it -- and the leading-underscore cache each method
# reads -- turns the surface on without touching any recorded attribute.

FS = """
    def get_feature_names_out(self, input_features=None):
        check_is_fitted(self, 'drop_')
        names = list(input_features) if input_features is not None \\
            else list(self._feature_names_in)
        drop = set(self.drop_ or [])
        return np.asarray([c for c in names if c not in drop], dtype=object)
"""

DECOMP = """
    def get_feature_names_out(self, input_features=None):
        decomp = self.get_decomposition()
        k = decomp.components_.shape[0]
        prefix = 'PC' if isinstance(decomp, PCA) else 'Concept'
        comp = [f'{prefix}{i + 1}' for i in range(k)]
        names = list(input_features) if input_features is not None \\
            else list(self._feature_names_in)
        cols = self.cols if self.cols is not None else names
        keep = set(cols)
        other = [nm for nm in names if nm not in keep]
        return np.asarray(comp + other, dtype=object)
"""

PASS = """
    def get_feature_names_out(self, input_features=None):
        names = list(input_features) if input_features is not None \\
            else list(self._feature_names_in)
        return np.asarray(names, dtype=object)
"""

INTER = """
    def get_feature_names_out(self, input_features=None):
        names = list(input_features) if input_features is not None \\
            else list(self._feature_names_in)
        cols = self.cols if self.cols is not None else names
        suff = self.name_suffix
        inter = [f'{cols[i]}_{cols[j]}_{suff}'
                 for i in range(len(cols) - 1) for j in range(i + 1, len(cols))]
        base = list(cols) if self.only_return_interactions else list(names)
        return np.asarray(base + inter, dtype=object)
"""

ONEHOT = """
    def get_feature_names_out(self, input_features=None):
        check_is_fitted(self, 'trans_nms_')
        return np.asarray(self.trans_nms_, dtype=object)
"""

# FeatureRetainer overrides transform: it *selects* self.cols rather than
# dropping drop_, so its output is in cols order, and with cols=None it returns
# every column even though drop_ is then every column. Inheriting FS would name
# the exact complement of what it produces. It needs its own method.
RETAIN = """
    def get_feature_names_out(self, input_features=None):
        check_is_fitted(self, 'drop_')
        names = list(input_features) if input_features is not None \\
            else list(self._feature_names_in)
        return np.asarray(names if self.cols is None else list(self.cols),
                          dtype=object)
"""

METHODS = 0
CACHES = 0

def _class_range(lines, classname):
    """[start, end) line indices of `class classname` -- start is the class
    line, end is the first line at column 0 after it (or EOF)."""
    start = None
    for i, ln in enumerate(lines):
        if ln.startswith("class " + classname + "(") or ln.startswith("class " + classname + ":"):
            start = i
            break
    if start is None:
        die("class %s not found" % classname)
    end = len(lines)
    for i in range(start + 1, len(lines)):
        ln = lines[i]
        if ln.strip() and not ln[0].isspace():
            end = i
            break
    return start, end

def gfno_add(rel, classname, src):
    """Append a get_feature_names_out method to a class body."""
    global METHODS
    lines = read(rel).splitlines(keepends=True)
    start, end = _class_range(lines, classname)
    block = "".join(lines[start:end])
    if "def get_feature_names_out(" in block:
        die("%s.%s already has get_feature_names_out" % (rel, classname))
    body = src if src.endswith("\n") else src + "\n"
    # trim a trailing blank line at the insertion point so bodies stay tidy
    ins = end
    while ins > start + 1 and lines[ins - 1].strip() == "":
        ins -= 1
    lines[ins:ins] = [body]
    write(rel, "".join(lines))
    METHODS += 1
    LEDGER.append("gfno %-30s %s" % (classname, rel))

def gfno_cache(rel, classname, method="fit"):
    """Insert `self._feature_names_in = list(X.columns)` right after the
    validate_is_pd call in `classname.method`."""
    global CACHES
    lines = read(rel).splitlines(keepends=True)
    start, end = _class_range(lines, classname)
    # find the method inside the class
    msig = "    def " + method + "("
    mstart = None
    for i in range(start, end):
        if lines[i].startswith(msig):
            mstart = i
            break
    if mstart is None:
        die("%s.%s.%s not found" % (rel, classname, method))
    for i in range(mstart, end):
        if "validate_is_pd(" in lines[i]:
            indent = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
            cache = indent + "self._feature_names_in = list(X.columns)\n"
            if lines[i + 1].strip() == cache.strip():
                die("%s.%s cache already present" % (rel, classname))
            lines[i + 1:i + 1] = [cache]
            write(rel, "".join(lines))
            CACHES += 1
            LEDGER.append("cache %-29s %s.%s" % (classname, rel, method))
            return
    die("%s.%s.%s has no validate_is_pd call to anchor the cache" % (rel, classname, method))

# feature_selection/base.py -- numpy for the method, then FS on the base class.
edit("skutil/feature_selection/base.py",
     "from sklearn.utils.validation import check_is_fitted\n",
     "from sklearn.utils.validation import check_is_fitted\nimport numpy as np\n")
gfno_add("skutil/feature_selection/base.py", "_BaseFeatureSelector", FS)

# feature_selection/select.py -- one fit discarded its validated frame; keep it
# so the cache can read X.columns, then cache in all five concrete selectors.
edit("skutil/feature_selection/select.py",
     "        _, self.cols = validate_is_pd(X, self.cols)\n",
     "        X, self.cols = validate_is_pd(X, self.cols)\n")
for cls in ["SparseFeatureDropper", "FeatureDropper", "FeatureRetainer",
            "MulticollinearityFilterer", "NearZeroVarianceFilterer"]:
    gfno_cache("skutil/feature_selection/select.py", cls, "fit")
gfno_add("skutil/feature_selection/select.py", "FeatureRetainer", RETAIN)

# feature_selection/combos.py -- the linear-combination filterer works in
# fit_transform.
gfno_cache("skutil/feature_selection/combos.py", "LinearCombinationFilterer", "fit_transform")

# decomposition/decompose.py -- DECOMP on the base, cache in both decomposers.
gfno_add("skutil/decomposition/decompose.py", "_BaseSelectiveDecomposer", DECOMP)
gfno_cache("skutil/decomposition/decompose.py", "SelectivePCA", "fit")
gfno_cache("skutil/decomposition/decompose.py", "SelectiveTruncatedSVD", "fit")

# preprocessing/transform.py -- five pass-through transformers, then the
# interaction-term transformer which renames its columns.
for cls in ["FunctionMapper", "SelectiveScaler", "BoxCoxTransformer",
            "YeoJohnsonTransformer", "SpatialSignTransformer"]:
    gfno_add("skutil/preprocessing/transform.py", cls, PASS)
    gfno_cache("skutil/preprocessing/transform.py", cls, "fit")
gfno_add("skutil/preprocessing/transform.py", "InteractionTermTransformer", INTER)
gfno_cache("skutil/preprocessing/transform.py", "InteractionTermTransformer", "fit")

# preprocessing/encode.py -- the one-hot encoder already builds its output names.
gfno_add("skutil/preprocessing/encode.py", "OneHotCategoricalEncoder", ONEHOT)

# preprocessing/impute.py -- PASS on the base, cache in the selective imputer
# and the bagged-imputer base (which fits in fit_transform).
gfno_add("skutil/preprocessing/impute.py", "_BaseImputer", PASS)
gfno_cache("skutil/preprocessing/impute.py", "SelectiveImputer", "fit")
gfno_cache("skutil/preprocessing/impute.py", "_BaseBaggedImputer", "fit_transform")

if METHODS != 11:
    die("expected 11 get_feature_names_out methods, added %d" % METHODS)
if CACHES != 16:
    die("expected 16 input-name caches, added %d" % CACHES)

sys.stderr.write("Layer 1 ledger (%d edits):\n" % len(LEDGER))
for row in LEDGER:
    sys.stderr.write("  " + row + "\n")
sys.stderr.write("  get_feature_names_out methods: %d   input-name caches: %d\n"
                 % (METHODS, CACHES))
PYEOF

expect_count "after the semantic layer" 36

# ---------------------------------------------------------------------------
# Layer 2 -- mechanical. ruff's fixers. Safe pass first (a no-op at this step:
# every fixable B finding is an unsafe fix), then the unsafe pass.
# ---------------------------------------------------------------------------
ruff check --isolated --no-cache --ignore-noqa --no-respect-gitignore \
    --target-version "$TARGET" --select "$SELECT" --fix . >/dev/null 2>&1 || true
expect_count "after the safe fixes" 36

ruff check --isolated --no-cache --ignore-noqa --no-respect-gitignore \
    --target-version "$TARGET" --select "$SELECT" --fix --unsafe-fixes . >/dev/null 2>&1 || true
expect_count "after the unsafe fixes" 13

# ---------------------------------------------------------------------------
# Layer 3 -- residue. The 13 ruff will not rewrite, each in the one form that
# keeps behaviour identical.
# ---------------------------------------------------------------------------
python - "$ROOT" <<'PYEOF'
import io, sys
root = sys.argv[1]
LEDGER = []

def read(rel):
    return io.open(root + "/" + rel, encoding="utf-8").read()

def write(rel, text):
    io.open(root + "/" + rel, "w", encoding="utf-8").write(text)

def die(msg):
    sys.stderr.write("solve.sh Layer 3: " + msg + "\n")
    sys.exit(1)

def edit(rel, old, new, count=1):
    s = read(rel)
    n = s.count(old)
    if n != count:
        die("%s: anchor matched %d time(s), need %d\n    %r" % (rel, n, count, old[:70]))
    write(rel, s.replace(old, new))
    LEDGER.append("edit %-40s x%d" % (rel, count))

# B004 -- hasattr-for-__call__ smell in a doc helper. callable() is the fix; the
# helper is used only by the (unimported) docstring machinery.
edit("doc/sphinxext/numpy_ext/docscrape.py",
     "            func = getattr(self._f, '__call__', self._f.__init__)\n",
     "            func = self._f.__call__ if callable(self._f) else self._f.__init__\n")

# B007 -- two loop variables in _strip whose value is read after the loop
# (return doc[i:len(doc) - j]). Rename the bound name to _-prefixed and capture
# the value the return needs; i and j are already initialised to 0 above, so an
# empty doc still returns the same slice.
edit("doc/sphinxext/numpy_ext/docscrape.py",
"""        for i, line in enumerate(doc):
            if line.strip():
                break

        for j, line in enumerate(doc[::-1]):
            if line.strip():
                break""",
"""        for _i, line in enumerate(doc):
            i = _i
            if line.strip():
                break

        for _j, line in enumerate(doc[::-1]):
            j = _j
            if line.strip():
                break""")

# B007 -- numpydoc loop reads lines[i], never the bound `line`.
edit("doc/sphinxext/numpy_ext/numpydoc.py",
     "        for i, line in enumerate(lines):\n",
     "        for i, _line in enumerate(lines):\n")

# B007 -- _fit_and_score result loop; `parameters` is read after the loop.
edit("skutil/utils/fixes.py",
"""            for this_score, this_n_test_samples, _, parameters in \\
                    out[grid_start:grid_start + n_folds]:
                all_scores.append(this_score)""",
"""            for this_score, this_n_test_samples, _, this_parameters in \\
                    out[grid_start:grid_start + n_folds]:
                parameters = this_parameters
                all_scores.append(this_score)""")

# B008 -- StandardScaler() as a default argument is a shared mutable. Default to
# None and build it in fit; transform reads self.scaler, and the attribute has
# no trailing underscore so the corpus never sees it.
edit("skutil/preprocessing/transform.py",
     "    def __init__(self, cols=None, scaler=StandardScaler(), as_df=True):\n",
     "    def __init__(self, cols=None, scaler=None, as_df=True):\n")
edit("skutil/preprocessing/transform.py",
     "        # throws exception if the cols don't exist\n"
     "        self.scaler.fit(X[cols])\n",
     "        # throws exception if the cols don't exist\n"
     "        if self.scaler is None:\n"
     "            self.scaler = StandardScaler()\n"
     "        self.scaler.fit(X[cols])\n")

# B018 -- bare-name statements (existence probes / setup flags). Bind to `_` to
# keep the probe without the useless-expression smell; the `_` name is excluded
# from the public-API and estimator enumerations.
edit("build_tools/cythonize.py",
     "try:\n    WindowsError\nexcept NameError:\n",
     "try:\n    _ = WindowsError\nexcept NameError:\n")
edit("skutil/__init__.py",
     "    __SKUTIL_SETUP__\nexcept NameError:\n",
     "    _ = __SKUTIL_SETUP__\nexcept NameError:\n")
edit("skutil/utils/tests/test_util.py",
     "            grid1._estimator_type\n            grid1.score(df, y)\n",
     "            _ = grid1._estimator_type\n            grid1.score(df, y)\n")

# B904 -- raises inside except that must chain their cause. Build-only helper
# and the exp/log vectorizers; `from` changes neither the type nor the message
# the corpus records. The inner Cythonize raise (the fallback path, anchored on
# its unique ' sys.exit(main())' line so the identical raise in the try body is
# left alone) uses `from None`: a nonzero exit code is not the OSError's fault.
edit("build_tools/cythonize.py",
"""main())'] + flags +
                                 ["-o", gen_file, cython_file])
            if rc != 0:
                raise Exception(f'Cythonizing {cython_file} failed')""",
"""main())'] + flags +
                                 ["-o", gen_file, cython_file])
            if rc != 0:
                raise Exception(f'Cythonizing {cython_file} failed') from None""")
edit("build_tools/cythonize.py",
     "    except OSError:\n        raise OSError('Cython needs to be installed')\n",
     "    except OSError as exc:\n        raise OSError('Cython needs to be installed') from exc\n")
edit("skutil/utils/util.py",
     "    except ValueError:\n        # bail\n        raise ValueError(f\"don't know how to compute exp for type {type(x)}\")\n",
     "    except ValueError as exc:\n        # bail\n        raise ValueError(f\"don't know how to compute exp for type {type(x)}\") from exc\n")
edit("skutil/utils/util.py",
     "    except ValueError:\n        # bail\n        raise ValueError(f\"don't know how to compute log for type {type(x)}\")\n",
     "    except ValueError as exc:\n        # bail\n        raise ValueError(f\"don't know how to compute log for type {type(x)}\") from exc\n")

sys.stderr.write("Layer 3 ledger (%d edits):\n" % len(LEDGER))
for row in LEDGER:
    sys.stderr.write("  " + row + "\n")
PYEOF

expect_count "after the residue layer" 0

# ---------------------------------------------------------------------------
# Checks. Independent of ruff and the gates: prove the tree still builds, the
# notebooks still hold their cells, and the semantic surface is what we intended.
# ---------------------------------------------------------------------------
echo "== checks"

# 1. every module compiles.
python - "$ROOT" <<'PYEOF'
import compileall, sys, pathlib
root = sys.argv[1]
files = sorted(pathlib.Path(root).rglob("*.py"))
if len(files) != 58:
    sys.stderr.write("checks: expected 58 .py files, found %d\n" % len(files))
    sys.exit(1)
ok = compileall.compile_dir(root, quiet=1, force=True)
if not ok:
    sys.stderr.write("checks: a module failed to compile\n")
    sys.exit(1)
print("compile: %d modules OK" % len(files))
PYEOF

# 2. the three notebooks parse and still hold every cell.
python - "$ROOT" <<'PYEOF'
import json, sys
root = sys.argv[1]
EXPECTED = {
    "doc/examples/decomposition/SelectivePCA weighting.ipynb": (13, 6),
    "doc/examples/machine_learning_demo/ml demo.ipynb": (47, 24),
    "doc/examples/pipeline/skutil grid demo.ipynb": (15, 10),
}
for rel, (ncells, ncode) in EXPECTED.items():
    nb = json.load(open(root + "/" + rel, encoding="utf-8"))
    cells = nb["cells"]
    code = [c for c in cells if c["cell_type"] == "code"]
    if len(cells) != ncells or len(code) != ncode:
        sys.stderr.write("checks: %s has %d cells / %d code, expected %d / %d\n"
                         % (rel, len(cells), len(code), ncells, ncode))
        sys.exit(1)
print("notebooks: 3 parse with expected cell counts")
PYEOF

# 3. independent semantic scan -- retired call forms gone, set_output present.
python - "$ROOT" <<'PYEOF'
import io, re, sys, pathlib, tokenize
root = sys.argv[1]

def code_only(text):
    """Blank every string and comment in place, so a docstring that names one
    of these forms is not mistaken for the code doing it."""
    out = []
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except tokenize.TokenError:
        return text
    prev_end = (1, 0)
    lines = text.splitlines(keepends=True)
    result = list(text)
    # simplest robust approach: rebuild line by line, blanking STRING/COMMENT
    # token spans with spaces (newlines preserved).
    buf = list(text)
    idx = {}
    # map (row) -> start offset in text
    offs = [0]
    for ln in lines:
        offs.append(offs[-1] + len(ln))
    def pos(row, col):
        return offs[row - 1] + col
    for tok in toks:
        if tok.type in (tokenize.STRING, tokenize.COMMENT):
            s = pos(tok.start[0], tok.start[1])
            e = pos(tok.end[0], tok.end[1])
            for k in range(s, e):
                if buf[k] != "\n":
                    buf[k] = " "
    return "".join(buf)

# (label, must-equal, regex) -- scikit-learn forms the golden must not use.
RETIRED = [
    ("load_boston at module scope", 0, r"^from sklearn\.datasets import .*\bload_boston\b"),
    ("groups passed positionally to super().fit", 0, r"super\(\)\.fit\(X, _as_numpy\(y\), groups\)"),
    ("classifier passed positionally to check_cv", 0, r"check_cv\(cv, y, classifier\)"),
    ("base_estimator= into the bagging model", 0, r"base_estimator=self\.base_estimator"),
    ("_fit_and_score called positionally", 0, r"clone\(base_estimator\), X, y, scorer,"),
]
# (label, must-equal, regex) -- the set_output surface Layer 1 grafts on.
ADDED = [
    ("get_feature_names_out definitions", 11, r"\bdef get_feature_names_out\("),
    ("input-name caches stored in fit", 16, r"self\._feature_names_in = list\(X\.columns\)"),
]

def scan(patterns):
    counts = {label: 0 for label, _, _ in patterns}
    for p in sorted(pathlib.Path(root).rglob("*.py")):
        code = code_only(io.open(p, encoding="utf-8").read())
        for line in code.splitlines():
            for label, _, rx in patterns:
                if re.search(rx, line):
                    counts[label] += 1
    return counts

bad = False
rc = scan(RETIRED)
for label, want, _ in RETIRED:
    got = rc[label]
    tag = "ok" if got == want else "BAD"
    if got != want:
        bad = True
    print("  retired  %-3s %-42s %d (want %d)" % (tag, label, got, want))
ac = scan(ADDED)
for label, want, _ in ADDED:
    got = ac[label]
    tag = "ok" if got == want else "BAD"
    if got != want:
        bad = True
    print("  added    %-3s %-42s %d (want %d)" % (tag, label, got, want))
if bad:
    sys.stderr.write("checks: semantic scan mismatch\n")
    sys.exit(1)
print("semantic scan: retired forms gone, set_output surface complete")
PYEOF

echo "--- step 3 golden produced"
