# Move `skutil` onto pandas 3, on Python 3.12

## 1. Implement this milestone

`/app` holds the `skutil` library. The toolchain in this container has moved
underneath it again, and this is the largest such move yet: **pandas has
jumped from 2.2.3 to 3.0.5**, which pulls **NumPy from 2.0.2 to 2.3.5** and the
**interpreter from Python 3.9 to Python 3.12.11** with it, along with SciPy
(1.13.1 → 1.16.3), scikit-learn (1.6.1 → 1.7.2), Matplotlib (3.8.4 → 3.9.4),
pyarrow (17.0.0 → 21.0.0) and Cython (3.0.11 → 3.1.6); seaborn stays at 0.13.2.
The library was made to work on the stack one release back; make it work,
cleanly and without regressions, on pandas 3.

Unlike the NumPy 2 step before it, the **build survives this hop untouched**.
The meson-python backend already in the tree — `pyproject.toml` and
`meson.build`, the `numpy.distutils` `setup.py` files gone — builds the Cython
`skutil/metrics/_kernel_fast.pyx` and the Fortran `skutil/odr/dqrsl.f` against
NumPy 2.3 and Python 3.12 with no source change. This is a **runtime migration**,
like the pandas 2 step, not a build port. Everything named above is already
installed; `pip download` nothing, and introduce no other version of any of them.

**1. The package builds, imports and runs on this stack.** Every module of
`skutil` imports under Python 3.12, both compiled extensions build from source
against NumPy 2.3, and every public entry point works. The remaining properties
say exactly what "works" means here; where a fix has more than one correct
spelling, the behaviour of the tree you inherit (section 2) decides between
them.

**2. Every string column skutil detects, it still detects.** pandas 3 turns on
the PDEP-14 string dtype by default (`future.infer_string`): a column of Python
strings is no longer `object` but the new `str` `StringDtype`. skutil finds
"factor"/string columns in several places by testing against the literal
`'object'` — `df.dtypes == 'object'`, `select_dtypes(include=['object'])` — and
under pandas 3 those tests silently stop seeing string columns (`str` is not
equal to `'object'`, and the `select_dtypes` object-match now warns on its way to
being removed). A transformer that no longer sees its string columns produces a
different result, and section 2 says which result it must still produce. Make
every such site recognise the `str` dtype alongside `object`, so the columns are
found again and the deprecation is gone. (`category` is a distinct dtype from
both and is not string data; do not fold it in.)

**3. No stdlib `distutils`, anywhere.** Python 3.12 removed the top-level
`distutils` (PEP 632); `from distutils… import …` now raises `ModuleNotFoundError`
at import. A file that names a module this interpreter does not have is broken
whether or not a `try`/`except` around the import keeps it from crashing, and
whether or not anything at run time imports it to prove otherwise — build
helpers included. Bring the count to zero, and a build file you add yourself
obeys the same rule.

**4. No `inplace=` keyword passed to a pandas call.** pandas 3 makes
Copy-on-Write the only mode, and `inplace=` does not reliably avoid a copy under
it, so the repository drops the keyword as a house rule. No call in the
repository passes the keyword at all —
`inplace=False` included — and each call that passes it today keeps the result
it produces now.

**5. No expired pandas API, and no silent `fillna` downcast.** These carry as
guards and stay at zero: none of the pandas members removed by 3.0's expiry
table, and no `fillna`/`ffill`/`bfill` that leans on the object-downcast pandas 3
stops performing. The tree you inherit should satisfy both; keep them there, and code
you add obeys them.

**6. Everything the carried house rules required before, they still
require.** These were required of `/app` before this milestone; read them as
requirements on what you hand back. The build stays on
meson-python with no `numpy.distutils` anywhere; no NumPy member 2.0 removed or
deprecated and no NumPy alias of a builtin type; no `copy=False` to `np.array`; the
repository is written for the Python it now runs on (no `__future__` imports, no
Python-2 builtins or version forks, f-strings not `%`/`.format`, no redundant
type calls); no file touches a process-global random stream and every sampling
entry point honours its `random_state`; no construct that is a latent bug (no
mutable/computed default argument, no unused loop variable, no fixed-name
`getattr`/`hasattr` probe, no discarded bare expression, every `warnings.warn`
names `stacklevel=`, every `raise` inside `except` chains with `from`); no
pandas spelling the house rule retires (no `.values` read on a pandas object (`.to_numpy()`), no `.isnull()`/`.notnull()`,
no `.at[]`/`.iat[]`/`.ix[]`, no `read_table`, no `pandas.merge` function, no
`.pivot()`/`.unstack()`/`.stack()` call, no `.nunique()` compared with 1 to test
for a constant), every group-by
names `observed=`, no chained
assignment; every public name still imports from the module that exports it
today; every Python source and every notebook code cell (its `%`/`!` lines aside) still parses, and the
notebooks keep all their cells.

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

One carried rule is worth calling out because Python 3.12 is the first
interpreter here on which it applies. `zip()` gained a `strict=` keyword at
Python 3.10; a `zip()` call that means the truncating behaviour should now say
so. Make it explicit — `strict=False`, the value `zip()` already has, so no
output moves — at every `zip()` call that does not already pass one, in the
library and in the repository's own tests alike. `strict=True` changes behaviour
(it raises on a length mismatch) and is not what these calls mean.

## 2. Preserve the behaviour of the tree you inherit

The observable behaviour of `/app` changes in this step only in the ways the new
stack itself changes it, and only one of those asks anything of your code: the
string dtype pandas 3 introduces (below). The move from Python 3.9 also rewords
one standard-library error message on its own — no code acts on it — and that second,
automatic change is named after the string dtype so the moved message is not read
as a regression.

Where pandas 3 gives a different answer than pandas 2.2 did, the new answer is
the correct one. Do not reproduce the old one: no compatibility shim, no
`infer_string` opt-out, no wrapper that casts the new `str` dtype back to
`object`, no pinning of pandas's behaviour to what it used to be. A migration
that papers over pandas's own change has not migrated. The change that reaches
this tree's behaviour and is **required** to move is the string dtype:

  - **The default string dtype is `str`, not `object` (PDEP-14).** A column of
    strings now reports the `str` `StringDtype`, and anywhere the tree's
    behaviour includes the dtype of a string column, the new dtype is the correct
    one. This is pandas's own default under `future.infer_string`; reproduce it,
    and do not cast back to `object` to reproduce the old answer. Property 2
    above is the other side of the same coin: your code must *recognise* the new
    dtype so the columns are still found, and the values it then computes must be
    exactly what the tree computed before.

  - **One standard-library error message changed on the way to Python 3.12.**
    Since CPython 3.11, the `TypeError` the `re` module raises when the string
    it is asked to search is not a `str` or bytes-like object ends by naming the
    offending type (`…, got 'NoneType'`). skutil surfaces
    that message on one error path and does nothing to it; the new wording is the
    correct one, and reproducing the old text would be pinning the old
    interpreter. This needs no code change — it is the interpreter's own — and is
    documented here only so the single moved error message is not mistaken for a
    regression.

Everywhere else — every public entry point pandas 3 did *not* change — the tree
must return, for the same input, exactly what it returns today: the same values,
the same dtypes, the same column names and index, the same exception types and
messages on the error paths, the same fitted attributes. A floating-point value is the same value when it differs from today's by no
more than 1e-15 plus 1e-12 of its magnitude; every other value — integers,
strings, dtypes, labels, order, messages — must match exactly. The code in front of
you is the specification; read it. If you find yourself changing a value because
the new code is easier to write that way, that is a regression, not a migration.

A fitted estimator's observable state — its `vars()` keys ending in a single
trailing underscore — is closed and preserved: `fit` mints no new such attribute
and drops none. The runtime move onto pandas 3 changes which dtype a string
column reports, not which values or attributes the tree exposes; it may not move
a fitted attribute, a returned value, a column name or an error message beyond
the string-dtype change and the single `re` message named above.

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

**Every earlier requirement must still hold.** Properties 5 and 6, the two paragraphs
that follow property 6 (the rest of the carried rules with the suppression rule,
and the explicit `strict=` on `zip()`), and this
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
