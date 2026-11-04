# Take `skutil`'s three hot paths off row-at-a-time iteration

## 1. Implement this milestone

`/app` holds the `skutil` library. This time the toolchain does **not** move at
all: Matplotlib stays at 3.10.9, pandas at 3.0.5, NumPy at 2.3.5, SciPy at
1.16.3, scikit-learn at 1.7.2, pyarrow at 21.0.0 and seaborn at 0.13.2, on Python
3.12.11 — character for character the stack the release before it pinned — and no
build file changes: the meson-python backend already in the tree builds the
Cython `skutil/metrics/_kernel_fast.pyx` and the Fortran `skutil/odr/dqrsl.f`
against the same NumPy 2.3 with no source change. Everything named above is
already installed; `pip download` nothing, and introduce no other version of any
of them.

What is **new in the container** is a columnar-engine and notebook toolchain:
`polars`, `duckdb`, `nbclient`, `nbformat`, `ipykernel` and `skops` are
installed alongside the stack above. Nothing forces you to import the engines —
but they are there because this milestone is chiefly a **performance
migration**: three functions walk a DataFrame one row at a time in an
interpreter loop. Take them off it. Beside that, the repository becomes
reproducible in three smaller ways: how it persists a fitted model, example
notebooks that run as tests, and a lock of the environment it runs in
(properties 6 to 8).

The three are:

  - **`filter_collinearity`** in `skutil/feature_selection/select.py`. It walks
    the correlation matrix in a `while` loop, `enumerate`-scanning the remaining
    columns each pass to find the most-correlated pair and drop one of them.
  - **`InteractionTermTransformer.transform`** in
    `skutil/preprocessing/transform.py`. It builds pairwise interaction columns
    with a nested `for i … for j …` over a column-index `range`, multiplying one
    column pair per inner step.
  - **`GainsStatisticalReport.as_data_frame`** in `skutil/metrics/_act.py`. It
    assembles the gains/lift table with a nested `for` over `range`, accumulating
    one fold's row at a time.

**1. The package builds, imports and runs on this stack.** Every module of
`skutil` imports under Python 3.12, both compiled extensions build from source
against NumPy 2.3, and every public entry point works. The remaining properties
say exactly what "works" means here; where a fix has more than one correct
spelling, the existing behaviour of the tree (section 2) decides between them.

**2. The three hot paths carry no interpreter-level row iteration.** In each of
`filter_collinearity`, `InteractionTermTransformer.transform` and
`GainsStatisticalReport.as_data_frame`, the data is not walked an element at a
time in the interpreter: no `for` loop, no comprehension, and no `while` loop over
the frame's rows, columns or matrix cells — whatever the iterator, whether it is
`range(…)`, `enumerate(…)`, `zip(…)`, a bare sequence, `.iterrows()` or
`.itertuples()`, and whether the loop does arithmetic or only builds column names.
`np.vectorize` and `np.frompyfunc` are such a loop too: they call a Python
function once per element. The one exception is a top-level convergence `while` that tests whether a
whole-array sweep has settled and drives the next sweep: that is control flow, not
row iteration, and it may stay. Compute each result with array operations
instead. The observable result of each function does not change (section 2); only
how it is computed does.

**3. How you vectorise is open, so long as the answer is identical.** You may do
the numeric work with NumPy array operations over the same frames the loops
walked, or push a hot path into `polars` or `duckdb` — both are installed for
exactly this — **where the result is identical to what the loop produced**, value
for value, dtype for dtype, order for order, and exception for exception. The
requirement is the property, not the tool: no row iteration in the three paths,
and every observable answer unchanged.

**4. No row-wise escape hatch anywhere in library code.** Three guards carry
at zero across the whole package, not just the three paths, and your rewrite
must keep them there: no `DataFrame.iterrows()` or `.itertuples()` in library
code; no `.apply(…, axis=1)` (a row-wise `apply` is a Python loop wearing a
method name); and no Python callable run once per row inside a columnar engine —
polars `map_elements`, `map_rows` or `apply_rows`, or a duckdb `create_function`
user-defined function. Vectorise; do not trade one hidden loop for another.

**5. The charting helpers still own and return their figures.** The
correlation-plot helper returns the `Figure` (or seaborn
grid's `.figure`) it drew on every drawing branch, the grid-score-detail report
helper draws through an explicit `Axes` it created and returns its frame, no
`plt.show()` appears under `skutil/` outside the repository's own tests, no
pyplot global-state call (`plt.title`, `plt.xlabel`, `plt.gca`, …) draws or
labels in library code, and no library code mutates the global `rcParams` or the
seaborn theme. The tree you inherit should already hold all of this; keep it.

**6. No fitted model is persisted by pickling.** `pickle` — and every
serializer built on it: `joblib.dump`/`joblib.load`, `cloudpickle`, `dill`,
`shelve`, pandas' `read_pickle`/`to_pickle`, `numpy.load(…, allow_pickle=True)`
— runs code taken from the file when the file is loaded, and binds the file to
the class layout of the exact library versions that wrote it. No file in the
repository, notebook code cells included, saves or restores an object that way,
whether the call is spelled through an import alias, a `from` import or a
`getattr` fetch. Where the
repository saves a fitted estimator and loads it back — the walkthrough
notebook under `doc/examples/pipeline/` does both — it does so through
`skops.io`: `skops.io.dump` to save, and `skops.io.load` with the list of types
it is to trust to load. A skops file carries a versioned schema and loads
without executing anything it holds. Every save and every load the tree makes
today is still made, through `skops.io`; dropping one is not replacing it.

**7. The example notebooks run as tests.** Two of the notebooks under
`doc/examples/` — `decomposition/SelectivePCA weighting.ipynb` and
`pipeline/skutil grid demo.ipynb` — run top to bottom on this stack against the
installed package: every code cell, in order, from a fresh working directory,
without raising, each notebook within six minutes. Their IPython magics and
shell escapes (`%…`, `!…`) are not part of what has to run, and their saved
outputs and kernel metadata do not need to change. The pipeline notebook's save and load (property 6) are among
the cells that run, so the model it reloads has to be one it can reload. The
third, `machine_learning_demo/ml demo.ipynb`, is exempt: it is built on
`sklearn.datasets.load_boston`, which this stack no longer has and which
`skutil.utils.load_boston_df` raises for by design.

**8. The repository carries a lock of the environment it runs in.** A PEP 751
lock file, `pylock.toml` at the repository root, with `lock-version = "1.0"`
and one `[[packages]]` entry per distribution, naming it and the exact version
installed. It pins every distribution this environment was built from, at the
version installed here: the stack named above; `polars`, `duckdb`, `nbclient`,
`nbformat`, `ipykernel` and `skops`; and the tooling the tree is built and
tested with — `setuptools`, `wheel`, `Cython`, `meson-python`, `ninja` and
`pytest`. It may record the rest of what is installed as well.

**9. Everything the carried house rules required before, they still
require.** These were required of `/app` before this milestone; read them as
requirements on what you hand back. Every string column skutil
detects it still detects (the pandas-3 `str` `StringDtype` recognised alongside
`object`, `category` still excluded); the build stays on meson-python with no
`numpy.distutils` and no stdlib `distutils` anywhere; no `inplace=` passed to a
pandas call; no pandas member the 3.0 expiry removed and no `fillna`/`ffill`/
`bfill` that leans on the object-downcast pandas 3 dropped; no NumPy member
2.0 removed or deprecated, no NumPy alias of a builtin type, no `copy=False` to
`np.array`; the repository is written for the Python it now runs on (no
`__future__` imports, no Python-2 builtins or version forks, f-strings not `%`/
`.format`, no redundant type calls, every `zip()` that means truncation saying
`strict=False` — including any new `zip` your vectorisation introduces over
column pairs); no file disturbs a process-global random stream and every sampling
entry point honours its `random_state`; no construct that is a latent bug
(mutable/computed default argument, unused loop variable, fixed-name
`getattr`/`hasattr` probe, discarded bare expression, every `warnings.warn`
naming `stacklevel=`, every `raise` inside `except` chaining with `from`); no
pandas spelling the house rule retires (no `.values` read on a pandas object (`.to_numpy()`), no `.isnull()`/`.notnull()`,
no `.at[]`/`.iat[]`/`.ix[]`, no `read_table`, no `pandas.merge` function, no
`.pivot()`/`.unstack()`/`.stack()` call, no `.nunique()` compared with 1 to test
for a constant), every group-by
naming `observed=`, no chained
assignment; every public name still importable from the module that exports it
today; and every Python source and every notebook code cell (its `%`/`!` lines aside) still parsing, with
the notebooks keeping all their cells.

The rest of what was required before holds too. Nothing imports a
scikit-learn module the stack no longer has (`sklearn.externals`,
`sklearn.cross_validation`, `sklearn.grid_search`,
`sklearn.datasets.samples_generator`) or reaches pyplot through `seaborn.plt`; no
ABC is imported from `collections` rather than `collections.abc`, guarded or
not, and no string literal carries an escape Python does not define; every
exported estimator still stores each named constructor argument under its own name and
round-trips through `clone()`, and every transformer still offers
`get_feature_names_out` and `set_output(transform="pandas")`. Compiling every
Python source and importing every module of `skutil` under
`python -X dev -W error` raises no warning whose source is a file inside this
repository; warnings the libraries raise about their own files are not yours. A
name the repository reaches counts however it is spelled: one assembled at run
time (`getattr`, `vars`, `sys.modules`, `importlib`, `eval`, a string) is the
access it performs. And silencing is not fixing: the number of suppressions in
the repository may not grow — no new `# noqa` (each code counts),
`# type: ignore`, `# pragma: no cover`, `# nosec`, `# pylint: disable` or
`# mypy:` marker, no `warnings.simplefilter`/`filterwarnings` call that ignores,
no `numpy.seterr(…="ignore")`, no `mode.chained_assignment` option, no
`except …: pass` whose body is only `pass`, and no ignore entry in `setup.cfg`,
`pytest.ini`, `tox.ini`, `.coveragerc`, `.flake8`, `.pylintrc` or
`pyproject.toml`.

## 2. Preserve the behaviour of the tree you inherit

This step's whole difficulty is that **nothing observable may change**. No
library moves, so there is no new answer to adopt and no change to make: the tree
you inherit already produces every migrated answer the earlier steps arrived at, and your
job is to make the three hot paths faster while returning, for every input,
exactly what they return today — value, dtype, column name, index, order, and
exception type and message on the error paths. A floating-point value is the same value when it differs from today's by no
more than 1e-15 plus 1e-12 of its magnitude; every other value — integers,
strings, dtypes, labels, order, messages — must match exactly. A vectorised path that shifts
any of these beyond that is a regression, not a migration. The code in front of you is the
specification; read it before you rewrite it.

Three places where a naïve rewrite silently changes the answer, called out
because they are where this step succeeds or fails:

  - **`filter_collinearity` drops columns in an order the loop defines.** When two
    pairs are tied for most-correlated, which column the loop reaches first
    decides which one is dropped, and the observable output is the *set of columns
    that survive*. A vectorised sweep that breaks ties in a different order drops a
    different column and returns a different set. Reproduce the loop's drop order,
    not merely its threshold.
  - **`InteractionTermTransformer.transform` emits columns in a fixed order with
    fixed names.** The nested `i, j` walk fixes both the order of the interaction
    columns and the suffix names derived from the pair. A broadcast product must
    lay the pairs out in that same order and label them the same way.
  - **`GainsStatisticalReport.as_data_frame` reduces already-computed fold
    scores in a fixed layout.** It reads each metric's scores as a flat list laid
    out iteration-major, fold-minor, and reduces across the folds of each
    iteration; it draws nothing itself. A vectorised aggregation that reshapes
    that flat list to the wrong axes — folds-major instead of iterations-major —
    or reduces along the wrong axis produces different numbers for the same input.
    Reshape to `(n_iter, n_folds)` and reduce along the fold axis, exactly as the
    nested loop consumed it.

A fitted estimator's observable state — its `vars()` keys ending in a single
trailing underscore — is closed and preserved: `fit` mints no new such attribute
and drops none.

Three invariants the tree should already satisfy are easy to spend while doing the
work above, so they are called out:

  - `import skutil` must not disturb either global random stream, and neither must
    any module-level fixture the package exposes.
  - A seeded workload must produce identical results in two separate processes.
  - A seeded plot must produce byte-identical image output in two separate
    processes.

Every public name the package exports today must still be importable from the
module that exports it today.

Keep the code you are handed: the tree is edited, not replaced. Every file in
it stays in it. Every function, class and method a file defines stays defined in
that file, unless one of this step's own requirements is what retires it. And
every source file — a notebook's code cells included — keeps at least 60% of its
non-blank, non-comment lines, both in number and as the same lines: re-indenting
a line keeps it, while rewriting or reformatting it (re-quoting, re-wrapping)
does not.

The repository's own tests must not regress, and none may be deleted, renamed
away, emptied, or have an assertion removed. Some of them fail for reasons that
predate this step; you are free to leave those failing and free to fix them — but
a test meant to pass must pass, and rewriting a test to match code you changed,
rather than changing the code to match the test, is the failure mode this rules
out.

**Every earlier requirement must still hold.** Properties 4 and 9, the guards of
property 5 (no `plt.show()`, no pyplot global-state call, no global style write),
the paragraph that follows property 9 (the rest of the carried rules with the
suppression rule), and this
section's rules on the three invariants, the public names, keeping the code you
are handed, and the repository's tests keeping their assertions are carried
rules, and each must hold without exception on the tree you hand back: no
violation of any one of them may remain, whether your edits introduced it or
the tree you inherit already carried it. Do not assume the tree you inherit is
clean on them; check it.

## 3. Build and run

The package has two compiled extensions: a Cython extension
(`skutil/metrics/_kernel_fast.pyx`) and a Fortran extension built from
`skutil/odr/dqrsl.f` with its `skutil/odr/dqrsl.pyf` signature. Both build from
source through the meson-python backend already in the tree; `gcc`, `gfortran`,
the pinned Cython and the meson toolchain are installed.

```bash
python -m pip install --no-build-isolation .   # build and install from source (pinned build deps are already installed)
python -m pytest --pyargs skutil # the repository's own tests
```

Run the tests from outside the source directory, or `import skutil` will find the
source tree instead of the installed package. `polars` and `duckdb` are installed
if you choose an engine for a hot path, `skops` for property 6, and
`nbclient`/`ipykernel` (a `python3` kernel) if you want to run the example
notebooks while developing.

## 4. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, documents, and build files. Do not create a prose answer or an
  alternate entry point.
- Do not cheat by using online solutions or hints specific to this task.
- Do not weaken, filter, rename, skip, or delete the repository's tests.
