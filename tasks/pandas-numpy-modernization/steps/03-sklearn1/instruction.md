# Move `skutil` onto scikit-learn 1.6 / pandas 1.5 / NumPy 1.26, on Python 3.9

## 1. Implement this milestone

`/app` holds the `skutil` library. The toolchain in this container has moved
underneath it again: this is now Python 3.9.23, with NumPy 1.26.4, SciPy 1.13.1,
pandas 1.5.3, scikit-learn 1.6.1, Matplotlib 3.8.4, seaborn 0.13.2 and
Cython 3.0.11. The library was written for the stack one release back and
has not moved with it: `import skutil` raises `ImportError` on the tree in front
of you, and the repository's own test suite cannot collect. Make the package
work on the stack it is now pinned to.

The pandas and NumPy *code* migration is behind you — the tree you inherit
already avoids the pandas and NumPy spellings those libraries removed, and this
step introduces no new one to chase. What changed is the toolchain around that
code: every pin above moved up, and scikit-learn in particular jumped from 0.24
to 1.6.1, dropping and re-signing a good deal of API on the way. Twelve properties must hold of the repository when you are done. `pip
download` nothing to get there: every version named above is already installed,
and no other version of any of them may be introduced.

**1. The package builds, imports and runs on this stack.** Every module of
`skutil` imports, the compiled extensions build from source against this NumPy,
and every public entry point works. This is the property the other eleven serve.

Most of what breaks import here is scikit-learn's own churn between 0.24 and
1.6.1, and it is real, publicly documented change, not a style question:

  - `sklearn.datasets.load_boston` was deprecated in 1.0 and **removed in 1.2**.
    A module that imports it at import time cannot be imported at all under this
    stack, so `import skutil` fails on the tree you are handed. Relocate the
    import: move it *inside* the one function that calls it, so the module scope
    is clean and the name is still reached where it is actually used.
    `skutil.utils.load_boston_df` keeps calling `sklearn.datasets.load_boston`
    and stays exported under that name. It will now raise — scikit-learn's own
    `ImportError`, with scikit-learn's own removal notice, raised from
    scikit-learn's frame rather than from skutil's — and that is the correct new
    behaviour for it under section 2. Do not retire the function, rename it,
    guard it with a `try`/`except` that returns something instead, or replace the
    failure with an error of skutil's own wording: each of those is a different
    observable behaviour from the one the tree has, and `load_boston_df` stays
    a public name of `skutil.utils`.
  - Several call signatures scikit-learn exposes to code like this package's
    became **keyword-only** or **renamed their parameters** across these
    releases: the internal fit-and-score and cross-validation-splitter helpers
    the package's own search implementation drives now take their scorer, split,
    verbosity and parameter arguments by keyword (and require the newer
    score-parameters argument), and the ensemble/meta constructors it wraps take
    their inner estimator as `estimator=` where they once took `base_estimator=`.
    The package's public parameter names do not change; only the calls it makes
    into scikit-learn do. Everything it computed before, it must go on computing.

The rest of this section says exactly what "works" means, property by property.
Where a fix has more than one correct spelling, the behaviour of the tree you
inherit (section 2) decides between them.

**2. Nothing imports a scikit-learn module this version does not have.**
scikit-learn removed `sklearn.externals` in 0.23 — the vendored `six` and the
vendored `joblib` are both gone from it — removed `sklearn.cross_validation` and
`sklearn.grid_search` in 0.20, whose contents moved to `sklearn.model_selection`
in 0.18, and removed `sklearn.datasets.samples_generator` in 0.24, whose
generators are re-exported from `sklearn.datasets` itself. None of these came
back at 1.6.1. No file in the repository may import from any of them.

`six` and `joblib` are both importable in this container, so importing either
directly is a legitimate way to satisfy this; so is dropping the dependency.
Neither is required of the other. Neither is a pin of this step's stack: `six`
arrives with `python-dateutil` and `joblib` with scikit-learn, and the tree you
are handed imports neither, the vendored shims having already been retired. They
are named here because this property is about what you may not import, not about
what you must.

**3. Public estimators satisfy scikit-learn's constructor contract.**
scikit-learn 0.24 made `BaseEstimator.get_params` read each parameter from an
attribute of the same name with no fallback, and `sklearn.base.clone` is
`type(est)(**est.get_params(deep=False))` plus an identity check on the copy, so
`Pipeline`, `GridSearchCV` and every `cross_val_*` inherit the requirement — and
1.6.1 enforces it no more gently than 0.24 did. For every estimator, transformer
and meta-estimator this package exports:

  - each argument `__init__` names in its signature is stored on the instance
    under that same name, holding the value it was passed. Where the
    constructor you inherit already resolves an argument before storing it —
    a `None` default replaced by a concrete value the argument also accepts,
    as the imputers do when handed `fill=None` — the value it stores today is
    the one that stays, and `get_params()` goes on reporting it. A `**kwargs`
    catch-all is not a named argument: `get_params()` does not read it, and
    the keywords it absorbs are not added to what `get_params()` reports;
  - nothing else in the class stores over a constructor argument with a value
    that argument would not accept back. `clone()` is
    `type(est)(**est.get_params(deep=False))`, so a store-over breaks it *only*
    when the value `get_params()` reads off the attribute is no longer one the
    constructor accepts — and only such a value takes the trailing underscore
    that marks it derived. A quantity the argument's slot could never hold breaks
    `clone()` and belongs under a `_`-suffixed name: a decomposition that
    computes a pivot *array* and stores it over its integer `pivot` flag cannot
    be rebuilt by `type(est)(pivot=<array>)`, so that array is `pivot_`, not
    `pivot`. A value the constructor would still accept, re-stored under the same
    name, does *not* break `clone()` and does *not* take the underscore: a `fit`
    that writes a re-validated column list — still a list, or `None` — back over
    `self.cols` leaves it under `cols`, exactly as the tree keeps it; and a plain
    mirror that merely re-stores an accepted argument under a `_`-suffixed name
    (a `self.y_ = y` holding the target-column string the constructor took as
    `y`) sheds the underscore back to `self.y`. A constructor argument never
    carries the trailing underscore, and no store-over of a value the constructor
    accepts adds one;
  - `get_params()` names every argument the signature names, and `clone()`
    round-trips an unfitted instance.

`__init__` doing work is not itself the problem and this package has a class that
has to: it decomposes the matrix it is handed on construction, and the
behaviour of the tree depends on that still happening there. What the contract
forbids is losing the arguments.

The package's grid-search wrappers are where this is easiest to get wrong.
`skutil.grid_search.GridSearchCV` and `RandomizedSearchCV` — re-exported from
`skutil.model_selection` — subclass scikit-learn's own, but the package also
ships an internal search implementation the test suite instantiates directly,
and that one drives the cross-validation loop itself by calling into
scikit-learn's own fit-and-score machinery, whose call signature this release
made keyword-only (property 1). "It subclasses scikit-learn, so it fits for
free" covers only the first: both must still fit and select on this stack — for
the same inputs, selecting the same parameters, leaving the same fitted state and
raising the same errors they do today. That is a property of the tree, not of its
test file: it holds on every call into either search implementation, whether or
not the repository happens to have a test that makes one.

**4. Every transformer this package exports supports scikit-learn's output
configuration.** scikit-learn 1.2 added `set_output`: a transformer that
implements `get_feature_names_out` gains a `set_output(transform="pandas")`
method that makes its `transform` return a labelled `DataFrame` — column names
and all — instead of a bare array. The transformers in this package carry the
auto-wrapping machinery scikit-learn looks for but never grew the method, so
`set_output` is inert on them. Give every transformer the surface:

  - each implements `get_feature_names_out(input_features=None)` and returns the
    names of the columns its `transform` produces, in the order it produces
    them, as a NumPy array of `object` dtype — resolving `input_features` when it
    is given and falling back to the names the transformer saw at `fit` when it
    is not;
  - with that method present, `set_output(transform="pandas")` becomes available
    and, after it is set, `transform` returns a `DataFrame` whose columns are
    exactly those names.

This is additive and must stay additive. The default output (an array, or
whatever the transformer returns today) does not change; no fitted attribute the
transformer already exposes changes; nothing about the behaviour of `fit` or
`transform` changes. You are turning on a surface scikit-learn offers,
not altering what the transformer computes.

**5. No pandas API this version removed.** `DataFrame.as_matrix()` (deprecated in
pandas 0.23) and the `.ix[]` indexer (deprecated in 0.20) were deleted in 1.0;
nothing in the repository may call either. Use `.to_numpy()` or `.values` for the first, and
`.loc[]` or `.iloc[]` for the second, according to whether the original lookup
was by label or by position — they are not interchangeable and `.ix` chose
between them at runtime, so each call site has to be read.

**6. Nothing mutates a pandas object in place.** No call in the repository may
pass `inplace=True`. pandas' own user guide recommends against it — it returns
`None`, so it cannot be composed, and it does not reliably avoid a copy — and a
library that returns new objects is the one this package's API already presents
to its callers.

**7. Where pandas offers two spellings of one operation, the repository uses the
documented one.** `.isna()` and `.notna()`, not the `.isnull()` and `.notnull()`
aliases. `.loc[]` and `.iloc[]` for element access, not `.at[]`, `.iat[]` or
`.ix[]`. `.read_csv()` rather than `.read_table()`, and the `DataFrame.merge`
method rather than the `pandas.merge` function. This is a house rule about how
the repository is written, and it applies to the lines you write as much as to
the ones you are replacing. It is a rule about naming the same result two ways,
so it stops at operations that are genuinely the same: reshaping calls whose
results differ — `.pivot`/`.pivot_table`, `.stack`/`.melt` — are not
interchangeable and are left to your judgement here.

**8. No NumPy alias of a builtin type.** `np.float`, `np.int`, `np.bool`,
`np.object`, `np.str`, `np.long` and `np.unicode` were never NumPy types: each
was a plain alias for the builtin of the same name, and NumPy 1.20 deprecated all
of them (1.24 turned the access into an error). None may appear anywhere in the
repository, under any import alias. The replacement is the builtin — `np.float`
was `float`, not `np.float64` — except where the surrounding code is actually
asking for a NumPy scalar type, where the sized spelling (`np.float64`,
`np.int64`, `np.bool_`) is what it meant.

**9. The repository is written for the interpreter it is pinned to.** Nothing in
it may exist only to support a Python older than the one it now runs on, and no
construct may be spelled the way an older Python required when this one provides
its own. In this tree that is:

  - every `from __future__ import …` statement, in `.py` files and in the code
    cells of the notebooks alike;
  - the Python 2 builtins `unicode`, `basestring`, `xrange`, `long`, and
    `dict.iteritems`/`itervalues`/`iterkeys`;
  - branches on the interpreter's major version — whether written as
    `sys.version_info[0] == 2`, through a `VERSION_MAJOR`-style local alias, or
    as a `try`/`except ImportError` pair whose two arms are a Python 2 and a
    Python 3 import — where the Python 2 arm is now unreachable. The dead arm
    goes and the live one is inlined; a branch that still has two reachable
    outcomes stays;
  - `u''` string prefixes, `# -*- coding: utf-8 -*-` lines, `class C(object)`
    bases, `super(C, self)` calls, `IOError`/`EnvironmentError` aliases of
    `OSError`, redundant `open(path, 'r')` modes, and a `for` loop whose body is
    a single `yield`;
  - printf-style `%` interpolation and both indexed and implicit-index
    `.format(…)` calls, in favour of an f-string. (`'{}'.format(x)`
    is not a resting place either — it rewrites to an f-string too; positional
    `'{0}'` is no cleaner. Only the f-string is clean.) These reach a format expression the running code evaluates — a `'%s' % v` or `'{}'.format(v)` reached at run time. Format syntax that is merely the **text of a string** the program never evaluates — code quoted inside a bare triple-quoted block a file keeps as a note, say — is not such an expression: it is that string's own characters, and it is left exactly as written. Do not modernise the contents of such a literal, turn a triple-quoted block into `#` comment lines, or otherwise restructure or delete it — the block stays byte for byte.
  - a builtin type called on a literal that already has that type — `int(1)`,
    `float(1.0)`, `str('x')` — where the call constructs nothing the literal was
    not already; the call goes and the literal stays. Property 8 can strand one:
    `np.long(1)` becomes the builtin `int`, and `int(1)` is then this redundant
    call. Write the bare `1`, or the sized NumPy scalar (`np.int64(1)`) where
    property 8's exception applies and the line means a NumPy type.

Applied to every Python source in the repository and to the code cells of every
`.ipynb` under it — library code, tests, build tooling, documentation sources —
not only to the package.

**10. Nothing reaches a library through a re-export that library dropped.**
`seaborn.plt` was a convenience alias for `matplotlib.pyplot` that seaborn
removed in 0.8, so `sns.plt` raises `AttributeError` under the 0.13.2 installed
here and under every seaborn above it. It is not a style question: any call site
that reaches for it is broken code from the moment the pin moves. Import
`matplotlib.pyplot` and use it.

Properties 2, 8 and 10 are properties of the names the repository reaches, not of
how it spells them. A name assembled at run time is the same access as a name
written out: `getattr(np, "flo" + "at")`, `np.__dict__['int']`,
`vars(numpy)['object']`, `sys.modules['sklearn.externals']` and
`importlib.import_module('sklearn.grid_search')` each reach exactly what those
properties retire, and each counts as the access it performs. The tree you are
handed contains none of them and nothing the migration needs introduces one — the
replacement for `np.float` is the builtin `float`, named directly. Property 12
already carries this clause for the global random streams; it holds for the names
those properties retire too.

Property 1 is not on that list, and deliberately. It does not retire
`sklearn.datasets.load_boston` from the repository — it requires the opposite,
that `load_boston_df` go on reaching for it from inside the function — so there
is no dynamic spelling of it to forbid. What property 1 rules out is a
*module-scope* import of the removed name, which is a property of where the
import sits, and it is a property of what `import skutil` does rather than of
how any one line is written.

**11. No construct that is a latent bug.** Legal Python that is near-always a
mistake — the kind of pattern a defect-focused linter reads as a probable bug — is
held at zero across every Python source and notebook code cell. In this tree that
is:

  - **No mutable or computed default argument.** A default is evaluated once when
    the function is defined and shared across every call, so `def f(x=[])`,
    `def f(x={})`, and `def f(x=SomeEstimator())` all leak one object between
    calls. Default such a parameter to `None` and build the real value inside the
    function.
  - **No loop whose control variable the body never uses.** A `for i, line in
    enumerate(xs):` whose body reads only `line` has a dead `i`; rename the dead
    name to a `_`-prefixed throwaway, or drop the `enumerate`/`zip` that produced
    it. Where the value the loop bound is read *after* the loop, keep it by
    binding it to a durable name inside the body rather than leaning on the leaked
    loop variable.
  - **No probe of a fixed attribute name through `getattr`/`hasattr`.**
    `getattr(obj, "__call__", …)` and `hasattr(obj, "literal")` used to test for
    an attribute whose name is a constant are both write-arounds for a direct
    access; reach the attribute directly, and use `callable(obj)` to test for
    `__call__`.
  - **No bare expression statement whose value is discarded.** A lone name or
    attribute access on its own line — `WindowsError`, `obj._estimator_type` —
    that neither calls nor assigns does nothing at run time but raise the reader's
    doubt. Where it is a deliberate probe (a `try: Name` / `except NameError:`
    existence test), bind it to `_` so the intent is explicit.
  - **Every `warnings.warn(...)` names an explicit `stacklevel=`,** so the warning
    is reported against the caller rather than the line that raised it.
  - **Every `raise` inside an `except` block preserves the causal chain.**
    `raise NewError(...) from exc` to chain the exception being handled, or
    `raise NewError(...) from None` to drop it deliberately; a bare re-raise of a
    new exception that silently swallows the original context is not allowed.

The rule is about the class of construct, not this enumeration: a pattern of the
same kind introduced by your own edits falls under it just the same, so the code you
add must be as clean as the code you fix.

**12. Everything already required of the tree you inherit, it still guarantees.**
These were required of `/app` before this milestone. Read them as requirements
on what you hand back: where the tree you inherit still
falls short of one, or this stack exposes a lapse the old one hid, closing it is
part of this milestone's work:

  - **No file touches a process-global random stream.** Not the module-level
    functions of `numpy.random` (`seed`, `permutation`, `choice`, `rand`, …,
    however imported or aliased), and not those of the standard library's
    `random`. An explicitly constructed generator — `numpy.random.RandomState`,
    or whatever `sklearn.utils.check_random_state` returns — is not a global
    stream and is the intended way to draw. Nor may a retired name be reached
    indirectly, through `getattr`, `sys.modules`, `eval`, or a string.
  - **Every entry point that samples takes a `random_state`, honours it, and
    leaves the global streams alone.** Four do: `skutil.utils.shuffle_dataframe`,
    and the `balance` path of `skutil.preprocessing.OversamplingClassBalancer`,
    `UndersamplingClassBalancer` and `SMOTEClassBalancer`. Each names
    `random_state` in its own signature — on `__init__` for the balancers, per
    property 3 — produces identical output when called twice with the same `int`
    seed in one process, and, when handed an explicit `int` or `RandomState`,
    leaves `numpy.random`'s global state exactly where it found it.
    `random_state=None` keeps its historical meaning: `check_random_state`
    resolves it to NumPy's own global `RandomState`, so an unseeded call still
    draws from the global stream and still advances it.
  - **Importing and compiling the package raises no warning of its own.**
    Compiling every Python source in the repository and importing every module of
    `skutil` under `python -X dev -W error` must raise no warning whose source is
    a file inside this repository. Warnings that NumPy, pandas, scikit-learn,
    Matplotlib or seaborn raise about *their own* files are not yours. This is a
    harder claim on this stack than on the one before it: the newer libraries warn
    about lines that were silent under the older ones — a build-time helper that
    imports a module NumPy now deprecates drags that warning into every `import`
    that reaches it — and a `DeprecationWarning` reported against a file in `/app`
    is yours whichever library noticed it.
  - **Every Python source in the repository parses on this interpreter**, and so
    do the code cells of every `.ipynb` under it. Of a notebook, only the source
    of its code cells is Python; its outputs, metadata and cell structure are not,
    and do not need to change — but its cells must all still be there. A code
    cell's IPython magics and shell escapes (lines that begin with `%` or `!`)
    are IPython syntax rather than Python, and are not part of this.

Silencing is not fixing, and it is a property of its own, separate from the
properties it would hide: the number of suppressions in the repository may not
grow. The ones already in the tree may stay — removing them is not this step's
work — but nothing below may be added anywhere in it, in any file:

  - the comment markers `# noqa` (each code in a `# noqa: E501,F401` counts
    separately), `# type: ignore`, `# pragma: no cover`, `# nosec`,
    `# pylint: disable`, and `# mypy: ignore-errors` or `# mypy: disable`;
  - a `warnings.simplefilter` or `warnings.filterwarnings` call whose action is
    `"ignore"`, a `numpy.seterr(…="ignore")`, a
    `pandas.set_option("mode.chained_assignment", …)`, or an `except …: pass`
    whose body is nothing but `pass`;
  - an `ignore`, `extend-ignore`, `per-file-ignores`, `filterwarnings`,
    `exclude_lines` or `disable` entry in `setup.cfg`, `pytest.ini`, `tox.ini`,
    `.coveragerc`, `.flake8`, `.pylintrc` or `pyproject.toml`.

`warnings.catch_warnings()` is not a suppression and is not counted; what is
counted is the `simplefilter`/`filterwarnings` inside it, and only when it
ignores.

## 2. Preserve the behaviour of the tree you inherit

The observable behaviour of `/app` changes in exactly one way in this step: the
way the pinned libraries themselves changed it.

Where NumPy 1.26.4, SciPy 1.13.1, pandas 1.5.3, scikit-learn 1.6.1,
Matplotlib 3.8.4 or seaborn 0.13.2 documents a different answer than the versions
this code last ran against, the new answer is the correct one. Do not reproduce
the old one: no compatibility shim, no re-sorting a result back into its former
order, no wrapper that restores a removed default, no pinning of a library's
behaviour to what it used to be. A migration that papers over a library's own
change has not migrated.

Everywhere else, every public entry point must return, for the same input,
exactly what it returns today: the same values, the same dtypes, the same column
names and index, the same exception types and messages on the error paths, the
same fitted attributes. A floating-point value is the same value when it differs from today's by no
more than 1e-15 plus 1e-12 of its magnitude; every other value — integers,
strings, dtypes, labels, order, messages — must match exactly. The code in front of you is the specification; read it.
If you find yourself changing a value because the new code is easier to write
that way, that is a regression, not a migration.

A fitted estimator's observable state — its `vars()` keys that end in a single
trailing underscore, read back as its fitted attributes — is part of what this
preserves, and the set of them is closed: `fit` may not mint a new such attribute
nor drop one, *except* the two renames property 3 requires — a value the
constructor would reject moved onto a `_`-suffixed name (a computed `pivot` array
to `pivot_`), and an underscore a constructor-acceptable value never earned
removed (a `self.y_` that only mirrors the `y` argument dropped to `self.y`).
Those two are the only sanctioned changes to the set. The output-configuration
surface property 4 adds does not touch it: `get_feature_names_out` and the
name it reads back are a method and a leading-underscore attribute, and neither is
a fitted attribute — a single trailing underscore is what marks those, and adding
a leading-underscore cache or a public method mints none. Promoting a value the
constructor still accepts — a `self.cols` that `fit` re-validates and re-stores —
to a new `cols_` still adds observable state the tree does not have; it is a
regression under this section, not a step property 3 asks for.

An *unfitted* estimator's `get_params()` is preserved on the same terms, with one
carve-out — and it is the only behaviour change anywhere in this tree that no
library made. Property 11 forbids a constructor default that is a constructed
object, because one instance is built when the `def` is executed and then shared
by every caller who leaves the argument alone; property 11 holds it at zero, so
fixing it is required, not optional, and property 11 fixes the shape of the fix
too: the parameter defaults to `None` and the real value is built
inside the function. The visible consequence is that the unfitted `get_params()`
reports `None` for that one argument where it used to report the shared instance,
and the nested `<arg>__*` entries that instance contributed are absent until the
estimator is fitted. `SelectiveScaler(scaler=StandardScaler())` is the case in
this tree. **That change is required and expected, and this section does not
count it as a regression.** It is also all that is carved out: the same default
object must still be built, with the same arguments, at the point in `fit` where
it is first needed, so that every fitted attribute, every transformed value and
every error path is exactly what it is today; no other `get_params()` entry may
move; and the fitted `get_params()` still reports the constructed object. A
`None` that stays `None`, or a different default object, is a regression under
this section like any other.

Three invariants the tree should already satisfy are easy to spend while doing the
work above, so they are called out:

  - `import skutil` must not disturb either global random stream, and neither must
    any module-level fixture the package exposes. Reseeding a global generator at
    import time, or from a `setup_module`-style hook, is not an acceptable way to
    make anything reproducible.
  - A seeded workload must produce identical results in two separate processes.
  - A seeded plot must produce byte-identical image output in two separate
    processes. Matplotlib's default style, font resolution and layout all changed
    between the version this code was written against and the one installed here,
    so the *image* may differ from the one an older Matplotlib drew; what may not
    differ is two runs of this one.

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
away, emptied, or have an assertion removed. They do not run on the tree you
inherit, because the package does not import; the tree is nonetheless the
specification of what they are meant to do. Some of them fail for reasons that
predate this step. You are free to leave those failing and free to fix them — but
a test that is meant to pass must pass, and rewriting a test to match code you
changed, rather than changing the code to match the test, is the failure mode
this rules out.

One node fails for a reason that *arrives with* this step, and it is not a repair
target. `skutil/preprocessing/tests/test_transform.py::test_yeo_johnson` exercises
`YeoJohnsonTransformer` on a zero-variance feature. That transformer fits its
lambda through skutil's own call to `scipy.optimize.brent(..., brack=(-2, 2))`,
and on constant data every evaluation of the objective is NaN, so the bracket
search has nothing to descend. SciPy 1.1 let it wander off and return a garbage
lambda; from SciPy 1.11 the failed downhill search raises `BracketError`, and the
pin here is 1.13.1. The node therefore fails, and it is *supposed* to: this is
the pinned library documenting a different answer, which section 2's first rule
says is the correct one. Making it pass means suppressing or routing around
SciPy's own new error, which is exactly the old answer section 2 forbids
reproducing: on constant data, fitting `YeoJohnsonTransformer` now raises that
error, originating outside skutil, and a tree where it hands back a transformed
frame again has regressed, whatever the test file reports. Do not "fix" it.

**Every earlier requirement must still hold.** Properties 2, 3, 5 to 10 and 12, the
clause after property 10 on names assembled at run time, the suppression rule
after property 12, and this
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
