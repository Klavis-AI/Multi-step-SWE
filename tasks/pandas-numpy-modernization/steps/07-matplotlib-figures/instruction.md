# Give `skutil`'s plots their own figure, on Matplotlib 3.10

## 1. Implement this milestone

`/app` holds the `skutil` library. The toolchain in this container has moved once
more, but this time in a single place: **Matplotlib has gone from 3.9.4 to
3.10.9**. Everything the release before it pinned stands exactly where it was —
pandas 3.0.5, NumPy 2.3.5, SciPy 1.16.3, scikit-learn 1.7.2, pyarrow 21.0.0 and
seaborn 0.13.2, on Python 3.12.11 — and no build file changes: the meson-python
backend already in the tree builds the Cython `skutil/metrics/_kernel_fast.pyx`
and the Fortran `skutil/odr/dqrsl.f` against the same NumPy 2.3 with no source
change. This is a **runtime migration**, and a small one: its whole surface is
the library's two charting helpers. Everything named above is already installed;
`pip download` nothing, and introduce no other version of any of them.

Matplotlib 3.10.9 is the pinned runtime for this milestone, and the only piece
of the toolchain that moves. It is not what makes the work necessary: 3.10 takes
nothing away that this library's charting uses, and owning the figure you draw —
building one, drawing through an explicit `Figure`/`Axes`, handing it back — is
sound practice under any modern Matplotlib. The version is pinned so the
behaviour asked for here is stable, not because a release forced your hand.
skutil's charting still leans on the implicit global "current figure": it draws
on whatever figure pyplot last made current and ends by calling `plt.show()`.
Take it off that state.

**1. The package builds, imports and runs on this stack.** Every module of
`skutil` imports under Python 3.12, both compiled extensions build from source
against NumPy 2.3, and every public entry point works. The remaining properties
say exactly what "works" means here; where a fix has more than one correct
spelling, the behaviour of the tree you inherit (section 2) decides between
them.

**2. The plotting helpers build a figure and hand it back.** The library has two
charting entry points. The correlation-plot helper draws a correlation heatmap,
or a seaborn pair or kde grid depending on the plot type it is asked for, and
returns `None`: the only handle on the figure it drew is Matplotlib's global
current figure. The grid-score-detail report helper charts one scatter per
numeric hyper-parameter and one horizontal bar per categorical column, and
returns its results frame while the figures it drew are left in the same global
stack. Rework both so each figure is one the helper owns and returns: the
correlation-plot helper returns the `Figure` (or the seaborn grid's `.figure`) it
drew, on every branch that draws; and the report helper keeps returning its frame
but draws through an explicit `Axes` it created rather than the implicit current
one. A caller must be able to hold, save or compose what these helpers draw
without reaching into pyplot's global state. The image each helper produces for a
given input does not change — only where the figure comes from, and whether it is
returned (section 2).

**3. No `plt.show()` in library code.** A library never decides when a window
opens; that is the caller's call, and under a non-interactive backend it does
nothing but emit a warning. Every `plt.show()` under `skutil/`, outside the
repository's own tests, must go to zero, and code you add obeys the same rule.

**4. No pyplot global-state drawing or labelling in library code.** The pyplot
functions that act on "whichever figure was last touched" — `plt.title`,
`plt.xlabel`, `plt.ylabel`, `plt.gca`, `plt.gcf`, `plt.legend`, `plt.suptitle`,
`plt.xticks`, `plt.tight_layout` and their kin — must not appear in library code:
two helpers that each reach for the current axes end up drawing on each other's
figure. Draw on an explicit `Axes` you hold instead (`ax.set_title`,
`ax.set_xlabel`, …). Getting that handle is not the defect: `plt.subplots`,
`plt.figure` and `plt.close` create and clean up a figure and hand it back, and
using the object they return is the migrated spelling. seaborn's axes-aware
helpers draw on the `Axes` you give them, and its grid objects own the figure
they build, so a figure handle is obtainable either way; keep drawing through
seaborn wherever the tree already does.

**5. No ambient Matplotlib or seaborn global state.** This carries as a guard and
stays at zero: no library code mutates the global `rcParams` or the seaborn theme
(`matplotlib.rc`, `matplotlib.style.use`, `rcParams[...] = …`, `sns.set`,
`sns.set_theme`, …). A style write outlives the call that made it and restyles
every later figure the process draws. The tree you inherit should touch none of these;
keep it there.

**6. Everything the carried house rules required before, they still
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
`strict=False`); no file disturbs a process-global random stream and every
sampling entry point honours its `random_state`; no construct that is a latent
bug (mutable/computed default argument, unused loop variable, fixed-name
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

The observable behaviour of `/app` changes in this step in exactly one way, and
it is the change your own work makes: what the plotting helpers **return**.
Nothing else moves — Matplotlib 3.10 draws the same pixels 3.9 did for these
plots, so the image every chart renders is byte-identical to what the tree
produced before, and every non-plotting entry point returns, for the same
input, exactly what it returns today. A floating-point value is the same value when it differs from today's by no
more than 1e-15 plus 1e-12 of its magnitude; every other value — integers,
strings, dtypes, labels, order, messages — must match exactly.

The one change that is **required** to move is the return value of the
correlation-plot helper:

  - **It returns the figure it draws, where it used to return `None`.**
    Case by case, the value the helper returns moves from `None` to the
    `Figure` it drew. That figure is the correct new answer. Do not go back to
    returning `None`, and do not hand back a stand-in that merely looks like a
    figure, to reproduce the old answer — a
    migration that returns the same nothing it used to has not migrated. What
    is *drawn* does not move; only what is *returned* does.

Everywhere else — every value, dtype, column name, index, exception type and
message on the error paths, and fitted attribute — the tree must return exactly
what it returns today. The code in front of you is the specification; read it. If
you find yourself changing a drawn image because the new plumbing is easier to
write that way, that is a regression, not a migration: the report helper's two
charting loops shared one implicit current axes, so one explicit axes shared
between them keeps the figure that reaches the artists byte-for-byte what it
was.

A fitted estimator's observable state — its `vars()` keys ending in a single
trailing underscore — is closed and preserved: `fit` mints no new such attribute
and drops none.

Three invariants the tree should already satisfy are easy to spend while doing the
work above, so they are called out:

  - `import skutil` must not disturb either global random stream, and neither must
    any module-level fixture the package exposes.
  - A seeded workload must produce identical results in two separate processes.
  - A seeded plot must produce byte-identical image output in two separate
    processes — the figure you now return must draw the same bytes it drew before.

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

**Every earlier requirement must still hold.** Properties 5 and 6, the paragraph that
follows property 6 (the rest of the carried rules with the suppression rule), and this
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
source tree instead of the installed package.

## 4. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, documents, and build files. Do not create a prose answer or an
  alternate entry point.
- Do not cheat by using online solutions or hints specific to this task.
- Do not weaken, filter, rename, skip, or delete the repository's tests.
