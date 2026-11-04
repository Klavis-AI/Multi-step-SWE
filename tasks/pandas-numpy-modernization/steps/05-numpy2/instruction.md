# Move `skutil` onto NumPy 2.0, on Python 3.9

## 1. Implement this milestone

`/app` holds the `skutil` library. The toolchain in this container has moved
underneath it again: this is still Python 3.9.23 with pandas 2.2.3 and
scikit-learn 1.6.1, but **NumPy has jumped from 1.26.4 to 2.0.2**. SciPy stays
at 1.13.1, pyarrow at 17.0.0, Matplotlib at 3.8.4, seaborn at 0.13.2 and Cython
at 3.0.11. The library was made to work on the stack one release back; make it
work, cleanly and without regressions, on NumPy 2.

Unlike the pandas step before it, this move begins with the tree **not building
at all**. `numpy.distutils` — the build backend the package's build files are
written against — has been deprecated since NumPy 1.23 and no longer works on
this stack: its build entry point, `numpy.distutils.core`, imports `distutils`
modules that the `setuptools` installed here no longer ships, so the two
compiled extensions cannot be compiled and the package cannot be installed. The supported replacement build toolchain is installed for you:
**meson, ninja and meson-python** are on the stack, alongside `setuptools`
75.3.2 and `wheel` 0.45.1 (the old 59.8.0/0.37.1 pins existed only to keep
`numpy.distutils` working, which no longer matters).
`pip download` nothing to get there: every version named above is already
installed, and no other version of any of them may be introduced.

**1. The package builds, imports and runs on this stack.** Every module of
`skutil` imports, the compiled extensions — the Cython
`skutil/metrics/_kernel_fast.pyx` and the Fortran `skutil/odr/dqrsl.f` — build
from source against NumPy 2.0, and every public entry point works. This is the
property the others serve, and on this stack it is the milestone itself: the
tree as handed over does not build, and making it build again through a backend
that does not reach for `numpy.distutils` is the work.

The rest of this section says exactly what "works" means, property by property.
Where a fix has more than one correct spelling, the behaviour of the tree you
inherit (section 2) decides between them.

**2. No `numpy.distutils`, anywhere.** The build no longer imports or reaches
into `numpy.distutils` — not in a `setup.py`, not in `skutil/_build_utils`, not
in any module. A build configuration that still names it does not build under
NumPy 2, and a lazy import that only runs at build time is the same reach as one
at module scope. Bring the count to zero and keep it there; a build file you
add yourself obeys the same rule.

**3. No NumPy member 2.0 removed or deprecated, and no NumPy alias of a builtin
type.** These carry from the earlier steps and stay at zero: none of `np.float`,
`np.int`, `np.bool`, `np.object`, `np.str`, `np.long` or `np.unicode` anywhere
in the repository, and none of the members NumPy 2.0 retired: those its
expired-attributes table removed (`np.cumproduct`, `np.alltrue`, `np.NaN`,
`np.float_`, and the rest) and those it deprecated (`np.in1d`, `np.row_stack`,
`np.trapz`). Properties of the names the repository
reaches, not of how they are spelled: a name assembled at run time is the same
access as one written out. The tree you inherit should satisfy both; keep them at
zero, and code you add obeys them.

**4. No `copy=False` passed to `np.array`.** In NumPy 2 `copy=False` means
"never copy" and raises when a copy would be needed, where it used to mean "copy
only if you must". The tree should contain no such call; keep it at zero, and do not
introduce one.

**5. Importing and compiling the package raises no warning of its own.**
Compiling every Python source and importing every module of `skutil` under
`python -X dev -W error` must raise no warning whose source is a file inside
this repository.
Warnings that NumPy, pandas, scikit-learn, Matplotlib or seaborn raise about
*their own* files are not yours. A `DeprecationWarning` or `FutureWarning`
reported against a file in `/app` is yours whichever library noticed it.
Silencing is not fixing: adding a suppression — a `# noqa`/`# type: ignore`
marker, a `simplefilter`/`filterwarnings("ignore")`, a bare `except: pass`, a
config-file ignore entry — does not address a warning and is itself a
regression.

**6. Everything the carried house rules required before, they still
require.** These were required of `/app` before this milestone; read them as
requirements on what you hand back. The repository is
written for Python 3.9 (no `__future__` imports, no Python-2 builtins or version
forks, f-strings not `%`/`.format`, no redundant type calls); no file touches a
process-global random stream and every sampling entry point honours its
`random_state`; no construct that is a latent bug (no mutable/computed default
argument, no unused loop variable, no fixed-name `getattr`/`hasattr` probe, no
discarded bare expression, every `warnings.warn` names `stacklevel=`, every
`raise` inside `except` chains with `from`); no pandas spelling the house
rule retires (no `.values` read on a pandas object (`.to_numpy()`), no `.isnull()`/`.notnull()`,
no `.at[]`/`.iat[]`/`.ix[]`, no `read_table`, no `pandas.merge` function, no
`.pivot()`/`.unstack()`/`.stack()` call, no `.nunique()` compared with 1 to test
for a constant, no `inplace=True`), every group-by names
`observed=`, no chained assignment and no removed
pandas API; every public name still imports from the module that exports it
today; every Python source and every notebook code cell (its `%`/`!` lines aside) still parses, and the
notebooks keep all their cells. Nothing imports a scikit-learn module the stack no longer has
(`sklearn.externals`, `sklearn.cross_validation`, `sklearn.grid_search`,
`sklearn.datasets.samples_generator`) or reaches pyplot through `seaborn.plt`; no
ABC is imported from `collections` rather than `collections.abc`, guarded or
not, and no string literal carries an escape Python does not define; every
exported estimator still stores each named constructor argument under its own name and
round-trips through `clone()`, and every transformer still offers
`get_feature_names_out` and `set_output(transform="pandas")`. None of these is this
milestone's subject, but do not take the tree you inherit on trust: a violation
it still carries is as much a violation as one your edits introduce. The build migration
above must not break one, and code you add obeys them.

## 2. Preserve the behaviour of the tree you inherit

The observable behaviour of `/app` changes in exactly one way in this step: the
way NumPy itself changed it between 1.26.4 and 2.0.2.

Where NumPy 2.0.2 gives a different answer than 1.26 did, the new answer is the
correct one. Do not reproduce the old one: no compatibility shim, no re-sorting
a result back into its former order, no wrapper that restores a removed default,
no pinning of NumPy's behaviour to what it used to be. A migration that papers
over NumPy's own change has not migrated. These NumPy 2.0 changes reach this
tree's behaviour, and each is **required** to move — reproduce NumPy
2.0.2's own answer for it, never NumPy 1.26's:

  - **Scalar promotion follows NEP 50.** NumPy 2 fixes value-based promotion:
    a NumPy `float32` combined with a Python `float`, or a small-unsigned array
    combined with a Python `int` that fits, keeps the array's dtype rather than
    widening to the Python operand. The values the package computes over such
    mixes now carry the NEP 50 dtype. A corollary of the same rule: a Python
    integer that no longer fits the array's dtype is not silently widened — an
    out-of-range operand now raises `OverflowError` where 1.26 promoted it, and
    where the tree's behaviour reaches that error the raise is the correct
    answer, not a value to recover.
  - **Scalars repr with their type (NEP 51).** A NumPy scalar now prints as
    `np.float64(1.5)` where it printed as `1.5` before. Anywhere the tree's
    behaviour includes the text of a scalar, the new form is the correct one.
  - **The `numpy.bool` name.** NumPy 2.0 reintroduced `numpy.bool` as the
    canonical name of the boolean scalar type — the same object as
    `numpy.bool_`, not the Python builtin `bool` — so the text of that type
    reprs with the new name, and where the tree's behaviour includes it the
    new name is the correct one. This is what NumPy calls the type; it is not
    licence to write the alias — property 3 still forbids spelling `np.bool` in
    the repository's source.
  - **NumPy 2.0's linear-algebra results differ numerically.** Its
    linear-algebra routines return marginally different values, so where a
    result orders near-tied values by those numbers the tie can fall in
    a different order. Reproduce 2.0.2's order; do not re-sort a result back into
    its former order to hide the drift. This is a numerical change, not a
    documented tie-ordering contract.

Each of these is a genuine NumPy 1.26 → 2.0 change; reproduce the new answer,
and do not paper over it. Everywhere else — every public entry point NumPy 2.0
did *not* change — the tree must return, for the same input, exactly what it
returns today: the same values, the same dtypes, the same column names and
index, the same exception types and messages on the error paths, the same fitted
attributes. A floating-point value is the same value when it differs from today's by no
more than 1e-15 plus 1e-12 of its magnitude; every other value — integers,
strings, dtypes, labels, order, messages — must match exactly. The code in front of you is the specification; read it. If you find
yourself changing a value because the new code is easier to write that way, that
is a regression, not a migration.

A fitted estimator's observable state — its `vars()` keys ending in a single
trailing underscore — is closed and preserved: `fit` mints no new such attribute
and drops none. Moving the build onto a backend that does not use
`numpy.distutils` changes how the package is compiled, not which values or
attributes the tree exposes; it may not move a fitted attribute, a returned
dtype, a column name or an error message beyond the NumPy-2 changes named above.

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
it stays in it, with one exception: the four `setup.py` files (`setup.py`,
`skutil/setup.py`, `skutil/metrics/setup.py`, `skutil/odr/setup.py`) configure
the `numpy.distutils` build and may go, and `skutil/_build_utils/__init__.py`,
which serves only that build, stays as a file but need keep none of its
contents. In every other file, every function, class and method it defines stays
defined there, and the file keeps at least 60% of its non-blank, non-comment
lines, both in number and as the same lines: re-indenting a line keeps it, while
rewriting or reformatting it (re-quoting, re-wrapping) does not.

The repository's own tests must not regress, and none may be deleted, renamed
away, emptied, or have an assertion removed. Some of them fail for reasons that
predate this step; you are free to leave those failing and free to fix them — but
a test meant to pass must pass, and rewriting a test to match code you changed,
rather than changing the code to match the test, is the failure mode this rules
out.

**Every earlier requirement must still hold.** Properties 3 to 6 (property 5's
suppression rule included), and this
section's rules on the three invariants, the public names, keeping the code you
are handed, and the repository's tests keeping their assertions are carried
rules, and each must hold without exception on the tree you hand back: no
violation of any one of them may remain, whether your edits introduced it or
the tree you inherit already carried it. Do not assume the tree you inherit is
clean on them; check it.

## 3. Build and run

The package has two compiled extensions: a Cython extension
(`skutil/metrics/_kernel_fast.pyx`) and a Fortran extension built from
`skutil/odr/dqrsl.f` with its `skutil/odr/dqrsl.pyf` signature. Both must build
from source against NumPy 2.0. `gcc`, `gfortran` and the pinned Cython are
installed, and so is the meson-python build toolchain that replaces
`numpy.distutils`.

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
