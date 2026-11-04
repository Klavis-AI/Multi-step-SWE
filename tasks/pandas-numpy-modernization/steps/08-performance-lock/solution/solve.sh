#!/usr/bin/env bash
# Step 8 (08-performance-lock) reference solution.
#
# Entry tree: step 7's golden -- the meson-python build, pandas 3.0.5 / numpy
# 2.3.5 / CPython 3.12 / scikit-learn 1.7.2 / matplotlib 3.10.9 / seaborn 0.13.2
# clean, corr_plot returning its figure and no plt.show() in library code. The
# stack is UNCHANGED from step 7 (setup.sh only ADDS polars / duckdb / the
# notebook kernel on top of the identical pins), so this is neither a build port
# nor a runtime version hop: the behaviour corpus is byte-for-byte what step 7
# recorded, the inherited suite is unchanged, and the whole of the step is three
# hot row-wise scans that this step vectorises WITHOUT changing a single recorded
# value. The `hot-path-row-iteration` gate (since=8, scoped to these three
# functions) counts every for-loop, comprehension and while inside them (one
# finding per construct, whatever the iterator), exempting only
# filter_collinearity's single top-level greedy `while` -- a per-drop restart,
# not a per-row loop. That gate is what makes the rewrite carry weight; the
# `pandas-row-iteration` and
# `pandas-apply-axis-1` gates (both already at 0 on entry) are the regression
# guards that stop a fake "vectorisation" that just swaps a range loop for
# df.iterrows()/df.apply(axis=1). The corpus is the arbiter: every edit below is
# proved value-identical, so the golden re-records the entry corpus exactly.
#
# The three hot paths:
#
#   filter_collinearity  (skutil/feature_selection/select.py) -- greedy
#     multicollinearity drop. The outer `while not finished` restart loop is
#     kept; its inner `for i, nm in enumerate(c.columns)` scan -- which walked
#     the columns to find the first one whose largest off-diagonal correlation
#     is at/above the threshold -- becomes one NaN-safe NumPy column reduction
#     that picks that same column (argmax over the boolean mask == first in
#     column order). The action body (which recomputes max_cor / the other
#     column via the original sort) is left verbatim, so drops / macor / corrz
#     are byte-for-byte unchanged. `col_max >= threshold` mirrors the original
#     `max_cor < threshold` continue; `n_cols > 2` mirrors the original
#     `this_col.shape[0] == 1` skip; an all-NaN column has no candidate, exactly
#     as sort_values(na_position='first')[-1] being NaN made it `continue`.
#
#   InteractionTermTransformer.transform  (skutil/preprocessing/transform.py) --
#     N-choose-2 interaction sweep. The nested `for i in range(n_features-1)` /
#     `for j in range(i+1, n_features)` pair loop becomes np.triu_indices fancy
#     indexing: both operand blocks are gathered at once and the interaction
#     function is applied in a single call. Upper-triangle order reproduces the
#     loop's column order exactly. _mul returns (a*b).to_numpy() -> a 2-D array
#     that rebuilds with a RangeIndex, matching the original per-pair from_dict
#     of 1-D arrays; a frame-returning interaction (e.g. the corpus's a-b) keeps
#     X's index, matching the original from_dict of Series. Arbitrary callables
#     are preserved because fun() is still what produces the values. The pair
#     names are built with vectorised numpy string ops (np.char.add over the
#     fancy-indexed name arrays), not a comprehension, for the same reason.
#
#   GainsStatisticalReport.as_data_frame  (skutil/metrics/_act.py) -- per-fold
#     aggregation. `_signs` is a fixed two-key class attribute, so the outer
#     `for metric in dict_keys(self._signs)` loop is unrolled to its two metrics
#     (no loop walks them); the inner `for _ in range(n_iter)` /
#     `for _fold in range(n_folds)` accumulation becomes a reshape of the flat,
#     iter-major score list to (n_iter, n_folds) and axis-1 reductions. The iid
#     weighting sum(score*n_test)/sum(n_test) and the population np.std/min/max
#     over the RAW fold scores are preserved; the fold-order summation matches
#     the original for any n_folds < 128 (NumPy's pairwise sum is sequential
#     below its block size), so the DataFrame is byte-for-byte identical.
#
# Idempotent: every edit is an anchored region/exact replace that asserts its
# before-count and refuses to run twice, so re-running from the entry tree
# reproduces this tree byte-for-byte and a second run fails loudly.
set -euo pipefail
ROOT="${1:-/app}"
cd "$ROOT"
export ROOT

echo "== step 8 (08-performance-lock) solve.sh on $ROOT"

# --------------------------------------------------------------- entry guard
# Prove we stand on step 7's golden and step 8 is not yet applied: the
# meson-python build (no setup.py), the step-7 marker (corr_plot returns the
# pairplot figure, no plt.show in util.py), and the three step-8 targets still
# in their original row-wise form.
[ -f meson.build ] && [ -f pyproject.toml ] || {
    echo "ABORT: no meson-python build present -- this is not step 7's golden"; exit 3; }
[ ! -f setup.py ] || { echo "ABORT: a setup.py is present -- not step 7's golden"; exit 3; }
grep -q "return sns.pairplot(X).figure" skutil/utils/util.py || {
    echo "ABORT: step-7 corr_plot figure-return absent -- entry is not step 7's golden"; exit 3; }
grep -q "plt.show()" skutil/utils/util.py && {
    echo "ABORT: plt.show() present in util.py -- entry is not step 7's golden"; exit 3; }
grep -q "for i, nm in enumerate(c.columns):" skutil/feature_selection/select.py || {
    echo "ABORT: filter_collinearity enumerate scan absent -- step 8 already applied?"; exit 3; }
grep -q "for i in range(n_features - 1):" skutil/preprocessing/transform.py || {
    echo "ABORT: interaction pair loop absent -- step 8 already applied?"; exit 3; }
grep -q "for _ in range(n_iter):" skutil/metrics/_act.py || {
    echo "ABORT: gains fold loop absent -- step 8 already applied?"; exit 3; }

# --------------------------------------------------------------- the migration
python3 - <<'PY'
import io

def _read(path):
    with io.open(path, encoding="utf-8") as fh:
        return fh.read()

def _write(path, s):
    with io.open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(s)

def region(path, head, tail, new, sentinel):
    """Replace the text between (exclusive) an anchored head and tail.

    head / tail must each occur exactly once and head must precede tail; the
    span in between is swapped for `new`. `sentinel` is a unique marker from the
    replacement so a second run refuses rather than corrupting the file."""
    s = _read(path)
    assert s.count(head) == 1, "%s: head anchor count %d" % (path, s.count(head))
    assert s.count(tail) == 1, "%s: tail anchor count %d" % (path, s.count(tail))
    assert sentinel not in s, "%s: already migrated (sentinel present)" % path
    hi = s.index(head) + len(head)
    ti = s.index(tail)
    assert ti > hi, "%s: tail precedes head" % path
    s = s[:hi] + new + s[ti:]
    _write(path, s)
    print("  patched %s" % path)

def patch(path, old, new, n=1):
    s = _read(path)
    c = s.count(old)
    assert c == n, "%s: expected %d occurrence(s), found %d" % (path, n, c)
    assert new not in s, "%s: replacement already present (already migrated?)" % path
    _write(path, s.replace(old, new))
    print("  patched %s" % path)


# 1) filter_collinearity: vectorise the per-pass enumerate scan.
region(
    "skutil/feature_selection/select.py",
    "    corrz = []  # the correlations\n\n",
    "\n    # return\n    out_tup = (drops, macor, corrz)\n    return out_tup\n",
    "    # Greedily drop the most-collinear feature until none remain over the\n"
    "    # threshold. The outer restart loop is unchanged; the per-pass search for\n"
    "    # the offending column is vectorised -- one NaN-safe NumPy reduction over\n"
    "    # the off-diagonal correlations picks the same column the original\n"
    "    # enumerate(c.columns) scan would have (first in column order), and the\n"
    "    # action body is left verbatim so the recorded metrics are unchanged.\n"
    "    finished = False\n"
    "    while not finished:\n"
    "\n"
    "        cols = c.columns\n"
    "        n_cols = cols.shape[0]\n"
    "\n"
    "        # off-diagonal correlation matrix (self-correlation excluded)\n"
    "        mat = c.to_numpy(dtype=float, copy=True)\n"
    "        np.fill_diagonal(mat, np.nan)\n"
    "\n"
    "        # per-column max over the OTHER features, NaN-safe. A column that is\n"
    "        # all-NaN off the diagonal has no candidate -- exactly as the original\n"
    "        # sort_values(na_position='first')[-1] being NaN made it `continue`.\n"
    "        col_all_nan = np.isnan(mat).all(axis=0)\n"
    "        col_max = np.where(\n"
    "            col_all_nan, np.nan,\n"
    "            np.where(np.isnan(mat), -np.inf, mat).max(axis=0))\n"
    "\n"
    "        # a column is over iff it has a real max >= threshold AND there is\n"
    "        # more than one other feature (n_cols > 2 <=> the original\n"
    "        # this_col.shape[0] != 1 guard). `>=` mirrors `max_cor < threshold`.\n"
    "        over = (~col_all_nan) & (col_max >= threshold) & (n_cols > 2)\n"
    "\n"
    "        if not over.any():\n"
    "            # the original scan reached the last column on the continue branch\n"
    "            # and set finished there; nothing is left to drop\n"
    "            finished = True\n"
    "        else:\n"
    "            # first offending column, in column order (argmax -> first True)\n"
    "            i = int(np.argmax(over))\n"
    "            nm = cols[i]\n"
    "\n"
    "            # recompute the stored metrics exactly as the original did, so\n"
    "            # abs_corr / the other-column selection are byte-for-byte unchanged\n"
    "            this_col = c[nm].drop(nm).sort_values(\n"
    "                na_position='first')  # gets the column, drops the index of itself, and sorts\n"
    "            this_col_nms = this_col.index.tolist()\n"
    "            this_col = np.array(this_col)\n"
    "            max_cor = this_col[-1]\n"
    "\n"
    "            # gets the current col, and drops the same row, sorts asc and gets other col\n"
    "            other_col_nm = this_col_nms[-1]\n"
    "            that_col = c[other_col_nm].drop(other_col_nm)\n"
    "\n"
    "            # get the mean absolute correlations of each\n"
    "            mn_1, mn_2 = np.nanmean(this_col), np.nanmean(that_col)\n"
    "\n"
    "            if pd.isna(mn_1):\n"
    "                drop_nm = other_col_nm\n"
    "            elif pd.isna(mn_2):\n"
    "                drop_nm = nm\n"
    "            else:\n"
    "                drop_nm = nm if mn_1 > mn_2 else other_col_nm\n"
    "\n"
    "            # drop the bad col, row\n"
    "            c = c.drop(drop_nm, axis=1)\n"
    "            c = c.drop(drop_nm, axis=0)\n"
    "\n"
    "            # add the bad col to drops\n"
    "            drops.append(drop_nm)\n"
    "            macor.append(np.maximum(mn_1, mn_2))\n"
    "            corrz.append(_MCFTuple(\n"
    "                feature_x=drop_nm,\n"
    "                feature_y=nm if not nm == drop_nm else other_col_nm,\n"
    "                abs_corr=max_cor,\n"
    "                mac=macor[-1]\n"
    "            ))\n",
    sentinel="col_all_nan = np.isnan(mat).all(axis=0)",
)

# 2) InteractionTermTransformer.transform: vectorise the N-choose-2 pair loop.
patch(
    "skutil/preprocessing/transform.py",
    "        n_features = len(cols)\n"
    "        suff = self.name_suffix\n"
    "\n"
    "        fun = self.fun_\n"
    "        append_dict = {}\n"
    "        interaction_names = [x for x in cols]\n"
    "\n"
    "        # we can do this in N^2 or we can do it in the uglier N choose 2...\n"
    "        for i in range(n_features - 1):\n"
    "            for j in range(i + 1, n_features):\n"
    "                col_i, col_j = cols[i], cols[j]\n"
    "                new_nm = f'{col_i}_{col_j}_{suff}'\n"
    "                append_dict[new_nm] = fun(X[col_i], X[col_j])\n"
    "                interaction_names.append(new_nm)\n"
    "\n"
    "        # create DF 2:\n"
    "        df2 = pd.DataFrame.from_dict(append_dict)\n"
    "        X = pd.concat([X, df2], axis=1)\n"
    "\n"
    "        # if we only want to keep interaction names, filter now\n"
    "        X = X if not self.only_return_interactions else X[interaction_names]\n"
    "\n"
    "        # return matrix if needed\n"
    "        return X if self.as_df else X.to_numpy()\n",
    "        n_features = len(cols)\n"
    "        suff = self.name_suffix\n"
    "\n"
    "        fun = self.fun_\n"
    "        interaction_names = list(cols)\n"
    "\n"
    "        # Vectorise the N-choose-2 sweep: compute every (i < j) interaction in\n"
    "        # one fun() call over fancy-indexed operand blocks instead of a Python\n"
    "        # double loop over range(n_features). np.triu_indices gives the pairs\n"
    "        # in upper-triangle order (0,1),(0,2),...,(1,2),... -- the same column\n"
    "        # order the original loop produced.\n"
    "        ii, jj = np.triu_indices(n_features, 1)\n"
    "        # Build the pair names with vectorised string ops, not a\n"
    "        # comprehension: ca[ii] / ca[jj] are the fancy-indexed operand names\n"
    "        # and np.char.add concatenates element-wise; .tolist() yields plain\n"
    "        # str labels, identical to the old f-string per pair.\n"
    "        ca = np.asarray(cols).astype(str)\n"
    "        new_nms = np.char.add(np.char.add(np.char.add(\n"
    "            np.char.add(ca[ii], '_'), ca[jj]), '_'), str(suff)).tolist()\n"
    "        interaction_names = interaction_names + new_nms\n"
    "\n"
    "        base = X[cols]\n"
    "        left = base.iloc[:, ii].copy()\n"
    "        right = base.iloc[:, jj].copy()\n"
    "        left.columns = new_nms\n"
    "        right.columns = new_nms\n"
    "        interacted = fun(left, right)\n"
    "\n"
    "        # create DF 2: a frame-returning interaction (e.g. a - b) keeps X's\n"
    "        # index, matching the original from_dict of Series; _mul returns\n"
    "        # (a * b).to_numpy() -> a 2-D ndarray, which rebuilds with a RangeIndex\n"
    "        # exactly as the original per-pair from_dict of 1-D arrays did.\n"
    "        if isinstance(interacted, pd.DataFrame):\n"
    "            interacted.columns = new_nms\n"
    "            df2 = interacted\n"
    "        else:\n"
    "            df2 = pd.DataFrame(np.asarray(interacted), columns=new_nms)\n"
    "        X = pd.concat([X, df2], axis=1)\n"
    "\n"
    "        # if we only want to keep interaction names, filter now\n"
    "        X = X if not self.only_return_interactions else X[interaction_names]\n"
    "\n"
    "        # return matrix if needed\n"
    "        return X if self.as_df else X.to_numpy()\n",
)

# 3) GainsStatisticalReport.as_data_frame: vectorise the fold aggregation.
region(
    "skutil/metrics/_act.py",
    "                raise ValueError('n_obs is not divisible by n_folds and n_iter')\n\n",
    "\n            df = pd.DataFrame.from_dict(new_stats)\n",
    "            new_stats = {}\n"
    "            # _signs is a fixed two-key class attribute ({'lift': -1,\n"
    "            # 'gini': -1}); unroll the per-metric reduction over its two keys\n"
    "            # so no loop walks them. reshape the flat, iter-major per-fold\n"
    "            # scores to (n_iter, n_folds) and reduce along the fold axis. The\n"
    "            # RAW fold scores back std/min/max (iid only rebinds the mean),\n"
    "            # exactly as the original nested loops computed them.\n"
    "            metrics = dict_keys(self._signs)\n"
    "            m0, m1 = metrics[0], metrics[1]\n"
    "            sizes = (np.asarray(self.sample_sizes).reshape(n_iter, n_folds)\n"
    "                     if self.iid else None)\n"
    "\n"
    "            scores0 = np.asarray(self.stats[m0]).reshape(n_iter, n_folds)\n"
    "            if self.iid:\n"
    "                fold0 = (scores0 * sizes).sum(axis=1) / sizes.sum(axis=1)\n"
    "            else:\n"
    "                fold0 = scores0.sum(axis=1) / float(n_folds)\n"
    "            new_stats[f'{m0}_mean'] = fold0\n"
    "            new_stats[f'{m0}_std'] = np.std(scores0, axis=1)\n"
    "            new_stats[f'{m0}_min'] = np.min(scores0, axis=1)\n"
    "            new_stats[f'{m0}_max'] = np.max(scores0, axis=1)\n"
    "\n"
    "            scores1 = np.asarray(self.stats[m1]).reshape(n_iter, n_folds)\n"
    "            if self.iid:\n"
    "                fold1 = (scores1 * sizes).sum(axis=1) / sizes.sum(axis=1)\n"
    "            else:\n"
    "                fold1 = scores1.sum(axis=1) / float(n_folds)\n"
    "            new_stats[f'{m1}_mean'] = fold1\n"
    "            new_stats[f'{m1}_std'] = np.std(scores1, axis=1)\n"
    "            new_stats[f'{m1}_min'] = np.min(scores1, axis=1)\n"
    "            new_stats[f'{m1}_max'] = np.max(scores1, axis=1)\n",
    sentinel="scores0 = np.asarray(self.stats[m0]).reshape(n_iter, n_folds)",
)

print("  all edits applied (3 hot paths vectorised; no row-wise constructs added)")
PY

# --------------------------------------------------------------- verify
# Independent AST pass mirroring the harness gate: inside each of the three
# hot-path functions there must be no For, no comprehension and no While -- with
# the single exception of filter_collinearity's top-level greedy `while` -- and
# no .iterrows()/.itertuples()/.apply(axis=1) anywhere in the three files, and
# the vectorised markers must be present. Fails loudly if an edit regressed.
python3 - <<'PY'
import ast, io, sys

TARGETS = {
    ("skutil/feature_selection/select.py", "filter_collinearity"),
    ("skutil/preprocessing/transform.py", "InteractionTermTransformer.transform"),
    ("skutil/metrics/_act.py", "GainsStatisticalReport.as_data_frame"),
}
ITER_ATTRS = {"iterrows", "itertuples"}
COMPS = (ast.ListComp, ast.SetComp, ast.GeneratorExp, ast.DictComp)
LOOPS = (ast.For, ast.While) + COMPS

def _qualnames(tree):
    names = {}
    def walk(node, prefix):
        for child in ast.iter_child_nodes(node):
            if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                nm = prefix + "." + child.name if prefix else child.name
                names[child] = nm
                walk(child, nm)
            else:
                walk(child, prefix)
    walk(tree, "")
    return names

def funcs(tree, dotted):
    # exact dotted-name match, mirroring the gate's HOT_PATHS scoping
    for node, nm in _qualnames(tree).items():
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and nm == dotted:
            yield node

def parents_of(root):
    p = {}
    for parent in ast.walk(root):
        for child in ast.iter_child_nodes(parent):
            p[id(child)] = parent
    return p

def has_loop_ancestor(node, parents, stop):
    cur = parents.get(id(node))
    while cur is not None and cur is not stop:
        if isinstance(cur, LOOPS):
            return True
        cur = parents.get(id(cur))
    return False

bad = []
for path, fname in TARGETS:
    src = io.open(path, encoding="utf-8").read()
    tree = ast.parse(src, filename=path)
    found = False
    for fn in funcs(tree, fname):
        found = True
        parents = parents_of(fn)
        for node in ast.walk(fn):
            if isinstance(node, ast.For):
                bad.append("%s:%s:%d: for-loop in hot path" % (path, fname, node.lineno))
            elif isinstance(node, COMPS):
                bad.append("%s:%s:%d: comprehension in hot path" % (path, fname, node.lineno))
            elif isinstance(node, ast.While):
                # filter_collinearity's single top-level greedy while is allowed;
                # a nested while, or any while in the other two paths, is a row loop.
                if fname == "filter_collinearity" and not has_loop_ancestor(node, parents, fn):
                    continue
                bad.append("%s:%s:%d: while-loop in hot path" % (path, fname, node.lineno))
    if not found:
        bad.append("%s: function %s not found" % (path, fname))
    # no apply(axis=1) / iterrows / itertuples anywhere in the file
    for node in ast.walk(tree):
        if isinstance(node, ast.Attribute) and node.attr in ITER_ATTRS:
            bad.append("%s:%d: .%s present" % (path, node.lineno, node.attr))
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) \
                and node.func.attr in {"apply", "applymap", "map"}:
            for kw in node.keywords:
                if kw.arg == "axis" and isinstance(kw.value, ast.Constant) and kw.value.value == 1:
                    bad.append("%s:%d: .%s(axis=1) present" % (path, node.lineno, node.func.attr))

markers = {
    "skutil/feature_selection/select.py": "np.fill_diagonal(mat, np.nan)",
    "skutil/preprocessing/transform.py": "np.triu_indices(n_features, 1)",
    "skutil/metrics/_act.py": ".reshape(n_iter, n_folds)",
}
for path, m in markers.items():
    if m not in io.open(path, encoding="utf-8").read():
        bad.append("%s: vectorised marker %r absent" % (path, m))

if bad:
    print("VERIFY FAILED:")
    for b in bad:
        print("  " + b)
    sys.exit(4)
print("  verification: 3 hot paths free of for/comprehension/while (bar the greedy restart); no iterrows/apply(axis=1); markers present")
PY

# --------------------------------------------------------------- persistence
# The pipeline walkthrough notebook saves its fitted search with joblib and
# loads it back the same way: a pickle, which runs code when it is loaded and
# is bound to the class layout of the library versions that wrote it. skops.io
# writes a file with a versioned schema that loads without executing anything
# and builds only the types it is told to trust. Edited as text on the raw
# notebook JSON so that nothing else in the file -- outputs, metadata, layout --
# moves; every replacement asserts it matched exactly once.
python3 - <<'PY'
import io

NB = "doc/examples/pipeline/skutil grid demo.ipynb"
s = io.open(NB, encoding="utf-8").read()

def swap(old, new):
    global s
    n = s.count(old)
    assert n == 1, "%s: %d occurrence(s) of %r" % (NB, n, old[:60])
    s = s.replace(old, new)

swap('    "import joblib\\n",\n',
     '    "import skops.io as sio\\n",\n')
swap('    "# write the model\\n",\n'
     '    "joblib.dump(search, \'final_model.pkl\', compress=3)"',
     '    "# write the model the search selected -- the fitted pipeline its\\n",\n'
     '    "# predict() delegates to. skops\' format carries its own schema\\n",\n'
     '    "# version, and a file in it loads without running any code it holds\\n",\n'
     '    "sio.dump(search.best_estimator_, \'final_model.skops\')"')
swap('    "final_model = joblib.load(\'final_model.pkl\')\\n",\n',
     '    "# skops builds only the types it has been told to trust: these are the\\n",\n'
     '    "# ones this file was written with (read the list before trusting a file\\n",\n'
     '    "# you did not write yourself)\\n",\n'
     '    "untrusted = sio.get_untrusted_types(file=\'final_model.skops\')\\n",\n'
     '    "final_model = sio.load(\'final_model.skops\', trusted=untrusted)\\n",\n')
swap('    "!rm final_model.pkl"',
     '    "!rm final_model.skops"')
io.open(NB, "w", encoding="utf-8", newline="\n").write(s)
print("  patched %s (joblib -> skops.io)" % NB)
PY

# --------------------------------------------------------------- lock
# The environment this tree is built, tested and run in, written down where the
# repository can carry it: a PEP 751 lock, `pylock.toml` at the repository
# root, one [[packages]] entry per distribution installed in /opt/venv at the
# version installed. Read off the environment itself rather than typed, so it
# cannot disagree with what is there.
python3 - <<'PY'
import importlib.metadata as md
import io
import re

found = {}
for dist in md.distributions():
    name = dist.metadata["Name"]
    if not name:
        continue
    found[re.sub(r"[-_.]+", "-", name).lower()] = (name, dist.version)

out = [
    "# The environment this repository is built, tested and run in: every",
    "# distribution installed in it, each at the version installed (PEP 751).",
    'lock-version = "1.0"',
    'created-by = "importlib.metadata"',
    'requires-python = ">=3.12"',
    "",
]
for key in sorted(found):
    name, version = found[key]
    out += ["[[packages]]",
            'name = "%s"' % key,
            'version = "%s"' % version,
            'index = "https://pypi.org/simple"',
            ""]
io.open("pylock.toml", "w", encoding="utf-8", newline="\n").write("\n".join(out))
print("  wrote pylock.toml (%d distributions)" % len(found))
PY

# --------------------------------------------------------------- verify (2)
python3 - <<'PY'
import ast, io, json, re, sys, tomllib

bad = []
nb = json.load(io.open("doc/examples/pipeline/skutil grid demo.ipynb", encoding="utf-8"))
src = "".join("".join(c["source"]) + "\n" for c in nb["cells"] if c["cell_type"] == "code")
src = "\n".join("pass" if l.lstrip().startswith(("%", "!")) else l for l in src.split("\n"))
tree = ast.parse(src)
names = set()
for node in ast.walk(tree):
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) \
            and isinstance(node.func.value, ast.Name):
        names.add(node.func.value.id + "." + node.func.attr)
if "joblib" in src:
    bad.append("the notebook still names joblib")
for want in ("sio.dump", "sio.load"):
    if want not in names:
        bad.append("the notebook makes no %s call" % want)

lock = tomllib.loads(io.open("pylock.toml", encoding="utf-8").read())
pins = dict((re.sub(r"[-_.]+", "-", p["name"]).lower(), p.get("version"))
            for p in lock["packages"])
if lock.get("lock-version") != "1.0":
    bad.append("pylock.toml lock-version is %r" % lock.get("lock-version"))
STACK = {"numpy": "2.3.5", "scipy": "1.16.3", "pandas": "3.0.5",
         "scikit-learn": "1.7.2", "matplotlib": "3.10.9", "seaborn": "0.13.2",
         "pyarrow": "21.0.0", "cython": "3.1.6", "meson-python": "0.18.0",
         "ninja": "1.13.0", "pytest": "8.4.2", "setuptools": "80.9.0",
         "wheel": "0.45.1", "polars": "1.35.1", "duckdb": "1.4.4",
         "nbclient": "0.10.2", "nbformat": "5.10.4", "ipykernel": "6.30.1",
         "skops": "0.13.0"}
for name, version in sorted(STACK.items()):
    if pins.get(name) != version:
        bad.append("pylock.toml pins %s at %r, the environment has %s"
                   % (name, pins.get(name), version))
if bad:
    print("VERIFY FAILED:")
    for b in bad:
        print("  " + b)
    sys.exit(5)
print("  verification: notebook persists through skops.io; pylock.toml pins the %d-distribution stack" % len(STACK))
PY

echo "== step 8 solve.sh complete"
