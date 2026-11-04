#!/usr/bin/env bash
# Step 7 (07-matplotlib-figures) reference solution.
#
# Entry tree: step 6's golden -- the meson-python build, pandas 3.0.5 / numpy
# 2.3.5 / CPython 3.12 / scikit-learn 1.7.2 / matplotlib 3.9.4 / seaborn 0.13.2
# clean. This step moves matplotlib 3.9.4 -> 3.10.9 (seaborn stays 0.13.2, and
# everything else holds at the step-6 pins -- see workdir/setup.sh). The Cython
# and f2py extensions build against the same numpy 2.3, so this is a RUNTIME
# migration, not a build port. The whole of the step is the two charting
# helpers in skutil/utils/util.py, which on the entry tree draw onto matplotlib's
# implicit global "current figure" and call plt.show():
#
#   corr_plot     -- three plot_type branches. 'cor' builds an explicit
#                    (f, ax) with plt.subplots and hands ax to seaborn, but
#                    throws the figure away (returns None). 'pair' and the kde
#                    'else' branch draw and then call plt.show(), also returning
#                    None. Nothing a caller gets back names the figure that was
#                    drawn; the only handle is plt.gcf(), the global one.
#   report_grid_score_detail -- charts a scatter per numeric hyper-parameter
#                    with bare plt.scatter/plt.title/plt.ylabel/plt.xlabel onto
#                    the current axes, then a horizontal bar per object column
#                    with a stateful Series.plot(kind="barh"), plt.show() after
#                    each. The frame it returns is unaffected by the charts.
#
# Two concerns, and a property to gain:
#
#   A. plt.show() in library code. Under the Agg backend it does nothing but
#      emit a non-interactive-backend warning, and a library that renders should
#      not decide for its caller when a window opens. All four in-library
#      plt.show() calls (util.py: two in corr_plot, two in report_grid) go. The
#      `matplotlib-library-show` gate (since=7) counts them statically; the
#      behaviour corpus reaches only some of them, so the gate is what makes the
#      whole removal carry weight.
#
#   B. The pyplot state machine in library code. report_grid draws with
#      plt.scatter/plt.title/plt.ylabel/plt.xlabel -- the axes-level pyplot
#      functions that mutate whatever figure happens to be current. On the
#      matplotlib 3.10 hop this is exactly the API the release notes push off
#      the global state and onto explicit Axes. Rewire BOTH loops through one
#      explicit `detail_ax = plt.subplots()[1]` (created lazily, on the first
#      plottable column, at the same default figure size the implicit one had):
#      the scatter loop draws on it directly and the bar loop is handed the same
#      axes as `ax=detail_ax`. The entry tree let both loops fall onto the shared
#      implicit gca (one figure), so routing both through one explicit axes keeps
#      the figure that reaches the artists byte-for-byte what it was. The
#      `matplotlib-stateful-pyplot` gate (since=7) flags the
#      axes-level pyplot calls and allows the figure-management ones
#      (subplots/figure/close). corr_plot's 'cor' branch already used the
#      explicit (f, ax) form, so nothing there needs rewiring for B.
#
#   C. (the property gained) corr_plot returns the figure it drew. 'cor' returns
#      its `f`; 'pair' returns the pairplot's `.figure`; the kde branch returns
#      the PairGrid's `.figure`. This is the migration's observable: a caller can
#      now hold, save or compose the figure instead of digging it out of global
#      pyplot state. It is what the behaviour corpus records as the must-flip --
#      corr_plot's `returned` moves from None to the figure, a value the
#      untouched tree cannot produce and a compat shim cannot fake, while the
#      digest of what was drawn (fig_digest, global-state scrape) is unchanged.
#      seaborn's PairGrid/FacetGrid expose `.figure` from 0.11 on; 0.13.2 is the
#      pinned seaborn, so it is present. The n_levels kwarg on the kde map is
#      left exactly as it was -- it is accepted at seaborn 0.13.2 and this step
#      does not touch seaborn's version -- so the kde figure is unchanged too.
#
# Idempotent: every edit is an exact-match replace that asserts its before-count,
# so a second run over an already-migrated tree fails loudly rather than
# corrupting it. Re-running from the entry tree reproduces this tree byte-for-byte.
set -euo pipefail
ROOT="${1:-/app}"
cd "$ROOT"
export ROOT

echo "== step 7 (07-matplotlib-figures) solve.sh on $ROOT"

# --------------------------------------------------------------- entry guard
# Prove we are standing on step 6's golden and not some other tree: the
# meson-python build (no setup.py), a step-6 marker present (pandas-3 str dtype
# admitted in encode.py), and the step-7 target NOT yet applied (plt.show still
# in util.py).
[ -f meson.build ] && [ -f pyproject.toml ] || {
    echo "ABORT: no meson-python build present -- this is not step 6's golden"; exit 3; }
[ ! -f setup.py ] || { echo "ABORT: a setup.py is present -- not step 6's golden"; exit 3; }
[ -f skutil/utils/util.py ] || { echo "ABORT: skutil/utils/util.py missing"; exit 3; }
grep -q "include=\['object', 'str'\]" skutil/preprocessing/encode.py || {
    echo "ABORT: step-6 string-dtype edit absent -- entry is not step 6's golden"; exit 3; }
grep -q "plt.show()" skutil/utils/util.py || {
    echo "ABORT: no plt.show() in util.py -- step 7 already applied?"; exit 3; }

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

# C (+ A for corr_plot): corr_plot returns the figure on every branch; the two
# plt.show() calls in the pair / kde branches go. One contiguous replace spanning
# the tail of the 'cor' branch through the end of the kde 'else'.
patch(
    "skutil/utils/util.py",
    "                    linewidths=linewidths, cbar_kws=cbar_kws, ax=ax)\n"
    "\n"
    "    elif plot_type == 'pair':\n"
    "        sns.pairplot(X)\n"
    "        plt.show()\n"
    "\n"
    "    else:\n"
    "        g = sns.PairGrid(X)\n"
    "        g.map_diag(sns.kdeplot)\n"
    "        g.map_offdiag(sns.kdeplot, cmap=cmap, n_levels=n_levels)\n"
    "        plt.show()\n",
    "                    linewidths=linewidths, cbar_kws=cbar_kws, ax=ax)\n"
    "        return f\n"
    "\n"
    "    elif plot_type == 'pair':\n"
    "        return sns.pairplot(X).figure\n"
    "\n"
    "    else:\n"
    "        g = sns.PairGrid(X)\n"
    "        g.map_diag(sns.kdeplot)\n"
    "        g.map_offdiag(sns.kdeplot, cmap=cmap, n_levels=n_levels)\n"
    "        return g.figure\n",
)

# A + B: report_grid_score_detail -- drop both plt.show(), and draw through
# explicit Axes instead of the pyplot state machine, preserving the figure that
# reaches the artists. The entry tree reused the implicit gca across BOTH loops,
# so scatter (per numeric col) and barh (per object col) accumulate on ONE shared
# axes -> a single figure. Reproduce that exactly with one lazily-created
# `detail_ax` handed to both the scatter calls and the barh via ax=detail_ax
# (dropping figsize, which the digest ignores and which ax= supersedes).
patch(
    "skutil/utils/util.py",
    "    if charts and CAN_CHART_MPL:\n"
    "        for col in get_numeric(result_df):\n"
    "            if col in ignore_axes:\n"
    "                # don't plot these ones\n"
    "                continue\n"
    "            elif col not in valid_axes:  # skip score / std\n"
    "                ser = result_df[col]\n"
    "                color = [def_color for _ in range(ser.shape[0])]\n"
    "\n"
    "                # highlight if needed\n"
    "                if sort_results and highlight_best:\n"
    "                    color[0] = highlight_col\n"
    "\n"
    "                # build scatter plot\n"
    "                plt.scatter(ser, result_df[y_axis], color=color)\n"
    "                plt.title(col)\n"
    "                plt.ylabel(y_axis)\n"
    "\n"
    "                # if there's a '__' in the col, split it\n"
    "                x_lab = col if '__' not in col else col.split('__')[-1]\n"
    "                plt.xlabel(x_lab)\n"
    "\n"
    "                # render\n"
    "                plt.show()\n"
    "\n"
    "        for col in list(result_df.columns[(result_df.dtypes == \"object\") | (result_df.dtypes == \"str\")]):\n"
    "            cat_plot = result_df[y_axis].groupby(result_df[col], observed=False).mean()\n"
    "            cat_plot.sort_values()\n"
    "            cat_plot.plot(kind=\"barh\", xlim=(.5, None), figsize=(7, cat_plot.shape[0] / 2))\n"
    "\n"
    "            plt.show()\n",
    "    if charts and CAN_CHART_MPL:\n"
    "        detail_ax = None\n"
    "        for col in get_numeric(result_df):\n"
    "            if col in ignore_axes:\n"
    "                # don't plot these ones\n"
    "                continue\n"
    "            elif col not in valid_axes:  # skip score / std\n"
    "                ser = result_df[col]\n"
    "                color = [def_color for _ in range(ser.shape[0])]\n"
    "\n"
    "                # highlight if needed\n"
    "                if sort_results and highlight_best:\n"
    "                    color[0] = highlight_col\n"
    "\n"
    "                # build scatter plot on an explicit axes (no global pyplot state)\n"
    "                if detail_ax is None:\n"
    "                    detail_ax = plt.subplots()[1]\n"
    "                detail_ax.scatter(ser, result_df[y_axis], color=color)\n"
    "                detail_ax.set_title(col)\n"
    "                detail_ax.set_ylabel(y_axis)\n"
    "\n"
    "                # if there's a '__' in the col, split it\n"
    "                x_lab = col if '__' not in col else col.split('__')[-1]\n"
    "                detail_ax.set_xlabel(x_lab)\n"
    "\n"
    "        for col in list(result_df.columns[(result_df.dtypes == \"object\") | (result_df.dtypes == \"str\")]):\n"
    "            cat_plot = result_df[y_axis].groupby(result_df[col], observed=False).mean()\n"
    "            cat_plot.sort_values()\n"
    "            # draw the bar chart onto the SAME axes the scatter loop used, so the\n"
    "            # figure digest matches the entry tree's gca-reuse (one shared axes)\n"
    "            if detail_ax is None:\n"
    "                detail_ax = plt.subplots()[1]\n"
    "            cat_plot.plot(kind=\"barh\", xlim=(.5, None), ax=detail_ax)\n",
)
print("  all edits applied (corr_plot returns figures on 3 branches; report_grid "
      "rewired to ONE shared explicit Axes; 4 plt.show() removed)")
PY

# --------------------------------------------------------------- verify (show)
# Independent of the patcher: scan code only (strings/comments blanked) so a
# plt.show in a docstring is never mistaken for a call. No in-library file may
# name a pyplot show, and every corr_plot branch must now return its figure.
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
    out = text.splitlines(True)
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
    for i, ln in enumerate(code.splitlines(), 1):
        if ".show(" in ln and ("plt" in ln or "pyplot" in ln):
            bad.append("%s:%d: pyplot show() in library code" % (path, i))

util = code_only("skutil/utils/util.py")
if util.count("return f\n") < 1:
    bad.append("util.py: corr_plot 'cor' branch does not return its figure")
if "return sns.pairplot(X).figure" not in util:
    bad.append("util.py: corr_plot 'pair' branch does not return its figure")
if "return g.figure" not in util:
    bad.append("util.py: corr_plot 'kde' branch does not return its figure")

if bad:
    print("VERIFY FAILED:")
    for b in bad:
        print("  " + b)
    sys.exit(4)
print("  verification: no in-library pyplot show(); corr_plot returns figures on all branches")
PY

# --------------------------------------------------------------- verify (state)
# Independent AST pass: no in-library call to the axes-level pyplot state machine
# (plt.scatter/plot/title/xlabel/ylabel/bar/barh/hist/...); the figure-management
# entry points (subplots/figure/close/subplot_mosaic) are allowed, because that is
# how the golden gets an explicit Axes to draw on.
python3 - <<'PY'
import ast, io, glob, sys

STATEFUL = {
    "show", "scatter", "plot", "title", "suptitle", "xlabel", "ylabel",
    "xlim", "ylim", "xticks", "yticks", "bar", "barh", "hist", "boxplot",
    "pie", "imshow", "contour", "contourf", "legend", "colorbar", "annotate",
    "text", "axhline", "axvline", "fill_between", "errorbar", "step", "stem",
}
PYPLOT_NAMES = {"plt", "pyplot"}

def pyplot_stateful_calls(tree):
    hits = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        f = node.func
        if not isinstance(f, ast.Attribute):
            continue
        base = f.value
        base_name = None
        if isinstance(base, ast.Name):
            base_name = base.id
        elif isinstance(base, ast.Attribute):
            base_name = base.attr
        if base_name in PYPLOT_NAMES and f.attr in STATEFUL:
            hits.append((getattr(node, "lineno", 0), f.attr))
    return hits

bad = []
for path in (glob.glob("skutil/**/*.py", recursive=True)
             + glob.glob("build_tools/**/*.py", recursive=True)):
    if "/tests/" in path:
        continue
    src = io.open(path, encoding="utf-8").read()
    try:
        tree = ast.parse(src, filename=path)
    except SyntaxError:
        continue
    for ln, name in pyplot_stateful_calls(tree):
        bad.append("%s:%d: pyplot.%s on global state" % (path, ln, name))

if bad:
    print("VERIFY FAILED (stateful pyplot):")
    for b in bad:
        print("  " + b)
    sys.exit(5)
print("  verification: no axes-level pyplot state-machine calls in library code")
PY

echo "== step 7 solve.sh complete"
