#!/usr/bin/env bash
# Step 6 (06-pandas3) reference solution.
#
# Entry tree: step 5's golden -- the meson-python build (pyproject.toml +
# meson.build, the four setup.py deleted), pandas 2.2.3 / numpy 2.0.2 / py3.9
# clean. This step moves the stack to pandas 3.0.5, which forces numpy 2.3.5,
# CPython 3.12 and the scikit-learn/scipy/matplotlib versions that ride with
# them (see workdir/setup.sh). The build itself survives the hop untouched --
# meson-python builds the Cython _kernel_fast and the f2py dqrsl against numpy
# 2.3 with no source change -- so this is a RUNTIME migration, like step 4, not
# a build port like step 5. Seven edits, three concerns:
#
#   A. CPython 3.12 removed the top-level ``distutils`` (PEP 632). One reach
#      survives in the vestigial build helper build_tools/cythonize.py, guarded
#      by a try/except so it never crashed -- but the ``stdlib-distutils`` gate
#      (since=6) counts the import statically and the helper is not something the
#      behaviour corpus ever imports, so the corpus cannot stand in for the gate.
#      Replace the LooseVersion compare with a stdlib tuple compare.
#
#   B. pandas 3 default string dtype (PDEP-14; future.infer_string is on by
#      default in 3.0). String columns are no longer ``object`` -- they are the
#      new ``str`` StringDtype. skutil detects "factor"/"object" columns in five
#      places by comparing against the literal ``'object'`` or by
#      ``select_dtypes(include=['object'])``. Under pandas 3 the equality checks
#      silently stop matching string columns (``str != 'object'``) and
#      select_dtypes keeps matching them only through a backward-compat shim that
#      now raises Pandas4Warning. Both are behaviour regressions the behaviour
#      corpus catches (must-stay): the fix is to recognise ``str`` alongside
#      ``object`` at each site, which restores the columns AND silences the
#      deprecation. ``category`` is deliberately NOT swept in (its dtype compares
#      unequal to both 'object' and 'str', as it did on pandas 2).
#
#   C. one redundant ``inplace=False`` on a sort_values whose result is already
#      assigned -- the ``pandas-inplace-kwarg`` gate (since=6) flags any
#      ``inplace=`` keyword. Drop it; behaviour is identical.
#
#   D. CPython 3.10 gave ``zip()`` a ``strict=`` keyword; ruff's bugbear rule
#      B905 (zip-without-explicit-strict) is dormant while ruff parses at py39
#      and lights up the moment step 6 moves the target to py312. Twelve
#      pre-existing ``zip()`` calls -- ten in the library, two in the inherited
#      test suite -- have carried through steps 1-5 unflagged because the target
#      was py39; they are now B905 findings the carried ``ruff-bugbear`` gate
#      (limit 0, since step 3) will not tolerate. This is the same shape as the
#      distutils break: not new debt the step introduced, but py3.12-surfaced
#      hygiene the step is the first to have to answer for. Make the truncating
#      behaviour explicit with ``strict=False`` -- the value ``zip()`` already
#      has, so every recorded output is byte-for-byte unchanged; it is exactly
#      what ``ruff check --select B905 --fix`` writes. ``strict=True`` is NOT
#      used: it would raise on a length mismatch, a behaviour change the corpus
#      would have to re-record as a flip.
#
# Idempotent: every edit is an exact-match replace that asserts its before-count,
# so a second run over an already-migrated tree fails its assertion loudly rather
# than corrupting the tree. Re-running from the entry tree reproduces this tree
# byte-for-byte.
set -euo pipefail
ROOT="${1:-/app}"
cd "$ROOT"
export ROOT

echo "== step 6 (06-pandas3) solve.sh on $ROOT"

# --------------------------------------------------------------- entry guard
# Prove we are standing on step 5's golden and not some other tree.
[ -f meson.build ] && [ -f pyproject.toml ] || {
    echo "ABORT: no meson-python build present -- this is not step 5's golden"; exit 3; }
[ ! -f setup.py ] || { echo "ABORT: a setup.py is present -- not step 5's golden"; exit 3; }
[ -f skutil/utils/util.py ] || { echo "ABORT: skutil/utils/util.py missing"; exit 3; }

# --------------------------------------------------------------- the migration
python3 - <<'PY'
import io

def patch(path, old, new, n=1):
    with io.open(path, encoding="utf-8") as fh:
        s = fh.read()
    c = s.count(old)
    assert c == n, "%s: expected %d occurrence(s) of %r, found %d" % (
        path, n, old[:70], c)
    assert new not in s, "%s: replacement already present (already migrated?)" % path
    s = s.replace(old, new)
    with io.open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(s)
    print("  patched %s" % path)

# A. CPython 3.12: distutils removed (PEP 632) -> stdlib tuple version compare.
patch(
    "build_tools/cythonize.py",
    "        from Cython.Compiler.Version import version as cython_version\n"
    "        from distutils.version import LooseVersion\n"
    "        if LooseVersion(cython_version) < LooseVersion('0.21'):",
    "        from Cython.Compiler.Version import version as cython_version\n"
    "        if tuple(int(p) for p in cython_version.split('.')[:2]) < (0, 21):",
)

# C. pandas: redundant inplace= (the result is already assigned).
patch(
    "skutil/metrics/_act.py",
    "        tab = tab.sort_values(by=['pred', 'idx'], axis=0, inplace=False)",
    "        tab = tab.sort_values(by=['pred', 'idx'], axis=0)",
)

# B. pandas 3 default string dtype: recognise 'str' wherever 'object' is detected.
patch(
    "skutil/preprocessing/encode.py",
    "        obj_cols_ = X.select_dtypes(include=['object']).columns.to_numpy()",
    "        obj_cols_ = X.select_dtypes(include=['object', 'str']).columns.to_numpy()",
)
patch(
    "skutil/preprocessing/balance.py",
    "    is_factor = X.dtypes[y] == 'object'",
    "    is_factor = X.dtypes[y] == 'object' or X.dtypes[y] == 'str'",
)
patch(
    "skutil/utils/util.py",
    "    object_dtypes = dtypes[dtypes == 'object']",
    "    object_dtypes = dtypes[(dtypes == 'object') | (dtypes == 'str')]",
)
patch(
    "skutil/utils/util.py",
    "        if _isint or _dtype == 'object':",
    "        if _isint or _dtype in ('object', 'str'):",
)
patch(
    "skutil/utils/util.py",
    '        for col in list(result_df.columns[result_df.dtypes == "object"]):',
    '        for col in list(result_df.columns[(result_df.dtypes == "object") | (result_df.dtypes == "str")]):',
)

# D. py3.12 target lights up ruff B905: make zip() truncation explicit with
#    strict=False (behaviour-identical). Ten in the library, two in the suite.
patch(
    "skutil/feature_selection/select.py",
    "dict(zip(self.drop_, matrix[drop_mask, 0].tolist()))",
    "dict(zip(self.drop_, matrix[drop_mask, 0].tolist(), strict=False))",
)
patch(
    "skutil/preprocessing/balance.py",
    "dict(zip(index.values, cts.values))",
    "dict(zip(index.values, cts.values, strict=False))",
)
patch(
    "skutil/preprocessing/impute.py",
    "dict(zip(cols, X[cols].apply(lambda x: _col_mode(x))))",
    "dict(zip(cols, X[cols].apply(lambda x: _col_mode(x)), strict=False))",
)
patch(
    "skutil/preprocessing/impute.py",
    "dict(zip(cols, X[cols].apply(lambda x: np.nanmedian(x.values))))",
    "dict(zip(cols, X[cols].apply(lambda x: np.nanmedian(x.values)), strict=False))",
)
patch(
    "skutil/preprocessing/impute.py",
    "dict(zip(cols, X[cols].apply(lambda x: np.nanmean(x.values))))",
    "dict(zip(cols, X[cols].apply(lambda x: np.nanmean(x.values)), strict=False))",
)
patch(
    "skutil/preprocessing/impute.py",
    "cols, fill = zip(*fill.items())",
    "cols, fill = zip(*fill.items(), strict=False)",
)
patch(
    "skutil/preprocessing/transform.py",
    "self.shift_ = dict(zip(cols, shift))",
    "self.shift_ = dict(zip(cols, shift, strict=False))",
)
patch(
    "skutil/preprocessing/transform.py",
    "(X[i].tolist()) for i in cols)))",
    "(X[i].tolist()) for i in cols), strict=False))",
)
patch(
    "skutil/preprocessing/transform.py",
    "(X[nm]) for nm in cols)))",
    "(X[nm]) for nm in cols), strict=False))",
    n=2,
)
patch(
    "skutil/preprocessing/tests/test_transform.py",
    "for i, j in zip(X_trans.columns.tolist(), expected_names)])",
    "for i, j in zip(X_trans.columns.tolist(), expected_names, strict=False)])",
    n=2,
)
print("  all edits applied (7 dtype/build/inplace + 12 py3.12 zip-strict)")
PY

# --------------------------------------------------------------- verification
# Independent of the patcher above: scan code only (strings/comments blanked)
# so a literal 'object' in a docstring is never mistaken for a dtype check.
python3 - <<'PY'
import io, tokenize, glob, sys

def code_only(path):
    """Return source with string and comment tokens blanked to spaces."""
    with io.open(path, "rb") as fh:
        try:
            toks = list(tokenize.tokenize(fh.readline))
        except tokenize.TokenError:
            with io.open(path, encoding="utf-8") as t:
                return t.read()
    with io.open(path, encoding="utf-8") as t:
        text = t.read()
    lines = text.splitlines(True)
    out = list(lines)
    for tok in toks:
        if tok.type in (tokenize.STRING, tokenize.COMMENT):
            (sr, sc), (er, ec) = tok.start, tok.end
            if sr == er:
                ln = out[sr - 1]
                out[sr - 1] = ln[:sc] + (" " * (ec - sc)) + ln[ec:]
            else:
                out[sr - 1] = out[sr - 1][:sc] + "\n"
                for r in range(sr, er - 1):
                    out[r] = "\n"
                if er - 1 < len(out):
                    out[er - 1] = out[er - 1][ec:]
    return "".join(out)

bad = []
for path in glob.glob("skutil/**/*.py", recursive=True) + glob.glob("build_tools/**/*.py", recursive=True):
    if "/tests/" in path:            # the inherited suite is the candidate's own; not touched here
        continue
    code = code_only(path)
    if "distutils" in code:
        bad.append("%s: distutils still imported" % path)
    if "inplace" in code and "=" in code:
        for i, ln in enumerate(code.splitlines(), 1):
            if "inplace" in ln and "=" in ln.split("inplace", 1)[1][:2]:
                bad.append("%s:%d: inplace= keyword remains" % (path, i))
    # every ==/select object check must also admit str
    for i, ln in enumerate(code.splitlines(), 1):
        low = ln
        if ("== 'object'" in low or '== "object"' in low) and (
                "'str'" not in low and '"str"' not in low):
            bad.append("%s:%d: object equality without str" % (path, i))
        if "select_dtypes" in low and "object" in low and (
                "'str'" not in low and '"str"' not in low):
            bad.append("%s:%d: select_dtypes(object) without str" % (path, i))

if bad:
    print("VERIFY FAILED:")
    for b in bad:
        print("  " + b)
    sys.exit(4)
print("  verification: distutils gone, no inplace= keyword, all object checks admit str")
PY

# --------------------------------------------------------------- verify (zip)
# Independent AST pass: every zip() with two or more iterables (a starred arg
# counts) must now name strict= explicitly, or ruff B905 fires under py312.
python3 - <<'PY'
import ast, io, glob, sys

def zip_calls_missing_strict(tree):
    hits = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        f = node.func
        if not (isinstance(f, ast.Name) and f.id == "zip"):
            continue
        multi = len(node.args) >= 2 or any(
            isinstance(a, ast.Starred) for a in node.args)
        if not multi:
            continue
        if not any(kw.arg == "strict" for kw in node.keywords):
            hits.append(getattr(node, "lineno", 0))
    return hits

bad = []
for path in (glob.glob("skutil/**/*.py", recursive=True)
             + glob.glob("build_tools/**/*.py", recursive=True)):
    src = io.open(path, encoding="utf-8").read()
    try:
        tree = ast.parse(src, filename=path)
    except SyntaxError:
        continue
    for ln in zip_calls_missing_strict(tree):
        bad.append("%s:%d: zip() without strict=" % (path, ln))

if bad:
    print("VERIFY FAILED (B905):")
    for b in bad:
        print("  " + b)
    sys.exit(5)
print("  verification: every multi-iterable zip() names strict= (B905 clean)")
PY

echo "== step 6 solve.sh complete"
