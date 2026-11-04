# Move `skutil` onto pandas 2.2 / pyarrow 17, on Python 3.9

## 1. Implement this milestone

`/app` holds the `skutil` library. The toolchain in this container has moved
underneath it again: this is still Python 3.9.23 with NumPy 1.26.4 and
scikit-learn 1.6.1, but **pandas has jumped from 1.5.3 to 2.2.3**, and
**pyarrow 17.0.0 now installs alongside it**. SciPy stays at 1.13.1,
Matplotlib at 3.8.4, seaborn at 0.13.2 and Cython at 3.0.11. The library was
made to work on the stack one release back; make it work, cleanly and without
regressions, on pandas 2.

The tree you inherit already imports, and its test suite collects. The work here
is narrow and entirely about pandas 2: the behaviours pandas changed between 1.5
and 2.0, the constructs pandas 2 turns from silent into warnings or errors, and
the pandas spellings a defect-focused reader now flags that the widened house
rule did not reach before. `pip download` nothing to get there: every version
named above is already installed, and no other version of any of them may be
introduced.

**1. The package builds, imports and runs on this stack.** Every module of
`skutil` imports, the compiled extensions build from source against this NumPy,
and every public entry point works on pandas 2.2.3. This is the property the
others serve, and on this stack it is mostly a claim about the warnings pandas 2
now raises where 1.5 was silent (properties 2 and 4) rather than about outright
breakage.

The rest of this section says exactly what "works" means, property by property.
Where a fix has more than one correct spelling, the behaviour of the tree you
inherit (section 2) decides between them.

**2. Every group-by names its `observed=`.** pandas deprecated the default of
`DataFrame.groupby(...)`/`Series.groupby(...)`'s `observed` argument: with a
categorical grouper, leaving `observed=` unset raises a `FutureWarning` on this
stack, so a call that leaves it unset raises a warning of the repository's own.
Every group-by in the repository names `observed=` explicitly, and
names the value that keeps each result exactly what it is today — section 2
requires that preservation, and it is what decides between the values. A group-by
you add yourself does the same.

**3. No pandas access that a defect-focused reader flags.** The house rule that
retired the individual pandas smells at the earlier steps widens here to the
whole family those smells belong to. In full, the repository spells none of
these: the `.isnull()`/`.notnull()` aliases (use `.isna()`/`.notna()`); element
access through `.at[]`, `.iat[]` or `.ix[]` (use `.loc[]`/`.iloc[]`);
`read_table` (use `read_csv`); the `pandas.merge` function (use the
`DataFrame.merge` method); an `inplace=True` argument; a `.pivot()`,
`.unstack()` or `.stack()` call (reshape with `.pivot_table()` or `.melt()`);
and a `.nunique()` compared with 1 to ask whether values are all the same.
And a defect-focused reader now flags
**`.values` reads on a Series or DataFrame**: pandas 2 wants the explicit
`.to_numpy()`, which says what it returns (a NumPy array, with a controllable
dtype and copy) where `.values` is ambiguous about type. Use the explicit
`.to_numpy()` where the receiver is a pandas object. Where the receiver's type
cannot be proven a pandas object, the access is outside the rule — leave it
exactly as it is; changing it is neither required nor wanted, and the behaviour
it returns is preserved by section 2. This is a rule about the construct, so the
code you write obeys it too.

**4. Nothing assigns through a chained pandas access, and nothing silences the
warning that would report it.** Chained assignment — `df[mask]['col'] = value`
or `df['col'][idx] = value`, whose target is itself the result of a
`__getitem__` — is unreliable on pandas 2: the write may land in a temporary
that is discarded, and pandas warns about it. Neither form may appear in the
repository; assign in one indexed step instead: `df.loc[mask, 'col'] = value`.
And the warning pandas raises about such an assignment may not be turned off: no
`pd.set_option("mode.chained_assignment", None)` (or `"warn"`/`"raise"` threaded
to defeat it), no `pd.options.mode.chained_assignment = …`. The tree you inherit
should contain no such option call; keep it that way, and introduce no chained
assignment of your own.

**5. No pandas API this version removed, and no NumPy alias of a builtin type.**
These carry unchanged from the earlier steps and stay at zero: no
`DataFrame.as_matrix()` or `.ix[]`; no `inplace=True`; the documented spelling of
each duplicated operation (`.isna()`/`.notna()`, `.loc`/`.iloc`, `.to_numpy()`);
and none of `np.float`, `np.int`, `np.bool`, `np.object`, `np.str`, `np.long` or
`np.unicode` anywhere in the repository, under any alias. Properties of the names
the repository reaches, not of how they are spelled: a name assembled at run time
is the same access as one written out.

**6. Importing and compiling the package raises no warning of its own.**
Compiling every Python source and importing every module of `skutil` under
`python -X dev -W error` must raise no warning whose source is a file inside
this repository. Warnings that pandas, NumPy, scikit-learn, Matplotlib or
seaborn raise about *their own* files are not yours; a `FutureWarning` or
`DeprecationWarning` reported against a file in `/app` is yours whichever
library noticed it. Silencing is not fixing:
adding a suppression — a `# noqa`/`# type: ignore` marker, a
`simplefilter`/`filterwarnings("ignore")`, the `mode.chained_assignment` option,
a bare `except: pass`, a config-file ignore entry — does not address a warning
and is itself a regression.

**7. Everything the carried house rules required before, they still
require.** These were required of `/app` before this milestone; read them as
requirements on what you hand back. The repository is
written for Python 3.9 (no `__future__` imports, no Python-2 builtins or version
forks, f-strings not `%`/`.format`, no redundant type calls); no file touches a
process-global random stream and every sampling entry point honours its
`random_state`; no construct that is a latent bug (no mutable/computed default
argument, no unused loop variable, no fixed-name `getattr`/`hasattr` probe, no
discarded bare expression, every `warnings.warn` names `stacklevel=`, every
`raise` inside `except` chains with `from`); every public name still imports from
the module that exports it today; every Python source and every notebook code
cell (its `%`/`!` lines aside) still parses, and the notebooks keep all their cells. Nothing imports a scikit-learn module the stack no longer has
(`sklearn.externals`, `sklearn.cross_validation`, `sklearn.grid_search`,
`sklearn.datasets.samples_generator`) or reaches pyplot through `seaborn.plt`; no
ABC is imported from `collections` rather than `collections.abc`, guarded or
not, and no string literal carries an escape Python does not define; every
exported estimator still stores each named constructor argument under its own name and
round-trips through `clone()`, and every transformer still offers
`get_feature_names_out` and `set_output(transform="pandas")`. None of these is this milestone's subject, but do not take the tree you inherit on
trust: a violation it still carries is as much a violation as one your edits introduce.
The pandas edits above must not break one, and code you add obeys them.

## 2. Preserve the behaviour of the tree you inherit

The observable behaviour of `/app` changes in exactly one way in this step: the
way pandas itself changed it between 1.5.3 and 2.2.3.

Where pandas 2.2.3 documents a different answer than 1.5 did, the new answer is
the correct one. Do not reproduce the old one: no compatibility shim, no
re-sorting a result back into its former order, no wrapper that restores a
removed default, no pinning of pandas' behaviour to what it used to be. A
migration that papers over pandas' own change has not migrated. Three such
changes reach this tree's behaviour, and each is **required** to move —
reproduce pandas 2.2.3's own answer for it, never pandas 1.5's:

  - **Reductions that summed or averaged integers now return a float.** A pandas
    reduction such as `Series.skew()` / `Series.kurt()` (and the moment
    statistics the package's `pd_stats` computes over integer and boolean
    columns) returns a Python/NumPy `float` under pandas 2 where pandas 0.x
    returned an `int` for the same all-integer input. The dtype of the result
    moves from integer to floating point; the value is otherwise the same number.
    This surfaces wherever the package tabulates per-column statistics.
  - **`DataFrame.from_dict` no longer sorts its keys.** A frame built from a dict
    of column-name → values preserves insertion order. The package's `pd_stats`
    assembles its result with `from_dict`; its single-column reports, which older
    pandas alphabetised, now come out in construction order. (Multi-column
    reports already reflected construction order on the previous stack.) The set
    of columns and their values is unchanged; only the order of the single-column
    reports moves.
  - **`Series.value_counts()` keeps the Series' name.** The index of the counts
    now carries the name of the Series it counted, where older pandas left it
    unnamed. The package's class-ratio validation in `preprocessing/balance.py`
    returns a value built from `value_counts()`, so the name now travels with it.
    The counts and their order are unchanged; the result simply carries the name
    it should.

Each of these is a genuine pandas 1.5 → 2.2 change; reproduce the new answer, and
do not paper over it. Everywhere else — every public entry point pandas 2 did
*not* change — the tree must return, for the same input,
exactly what it returns today: the same values, the same dtypes, the same column
names and index, the same exception types and messages on the error paths, the
same fitted attributes. A floating-point value is the same value when it differs from today's by no
more than 1e-15 plus 1e-12 of its magnitude; every other value — integers,
strings, dtypes, labels, order, messages — must match exactly. The code in front of you is the specification; read it.
If you find yourself changing a value because the new code is easier to write
that way, that is a regression, not a migration.

A fitted estimator's observable state — its `vars()` keys ending in a single
trailing underscore — is closed and preserved: `fit` mints no new such attribute
and drops none. Adding `.to_numpy()` where a `.values` read stood, naming a
`groupby`'s `observed=`, or rewriting a chained assignment into a single `.loc`
step changes how a value is computed, not which values or attributes the tree
exposes; none of them may move a fitted attribute, a returned dtype, a column
name or an error message beyond the three pandas-2 changes named above.

Three invariants the tree should already satisfy are easy to spend while doing the
work above, so they are called out:

  - `import skutil` must not disturb either global random stream, and neither must
    any module-level fixture the package exposes.
  - A seeded workload must produce identical results in two separate processes.
  - A seeded plot must produce byte-identical image output in two separate
    processes. The `.values`→`.to_numpy()` rewrites feed the same arrays into the
    same plotting calls; two runs of this tree must still agree bit for bit.

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

**Every earlier requirement must still hold.** Properties 4 to 7 (property 6's
suppression rule included), and this
section's rules on the three invariants, the public names, keeping the code you
are handed, and the repository's tests keeping their assertions are carried
rules, and each must hold without exception on the tree you hand back: no
violation of any one of them may remain, whether your edits introduced it or
the tree you inherit already carried it. Do not assume the tree you inherit is
clean on them; check it.

## 3. Build and run

The package builds with `setup.py`, which compiles a Fortran extension
(`skutil/odr/dqrsl.f`) and a Cython extension (`skutil/metrics/_kernel_fast.pyx`);
`gcc` and `gfortran` are installed, and so is the pinned Cython.

```bash
python -m pip install --no-build-isolation .   # build and install from source, against the installed stack
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
