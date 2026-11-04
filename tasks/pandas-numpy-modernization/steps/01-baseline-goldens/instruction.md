# Make `skutil` reproducible and warning-clean on its current stack

## 1. Implement this milestone

`/app` holds the `skutil` library. It runs on the stack already installed in
this container — Python 3.7, NumPy 1.14, SciPy 1.1, pandas 0.23,
scikit-learn 0.20, Matplotlib 2.2, seaborn 0.7 — and this step does not change
any of those versions. It makes the tree reproducible and quiet.

Four properties must hold of the repository when you are done.

**1. Nothing touches a process-global random stream.** No file in the
repository — library code, tests, build tooling, documentation sources,
notebooks — may seed or draw from a process-global random number generator.
That means the module-level functions of `numpy.random` (`numpy.random.seed`,
`numpy.random.permutation`, `numpy.random.choice`, `numpy.random.rand`, and the
rest of the legacy module-level API, however they are imported or aliased) and
the module-level functions of the standard library's `random` module
(`random.seed`, `random.shuffle`, `random.random`, …). An explicitly
constructed generator — `numpy.random.RandomState(seed)`, or whatever
`sklearn.utils.check_random_state` hands back — is not a global stream and is
the intended replacement.

**2. Every entry point that samples takes a `random_state`, honours it, and
leaves the global streams alone.** Four call sites in the public API draw
random numbers: `skutil.utils.shuffle_dataframe`, and the `balance` path of
`skutil.preprocessing.OversamplingClassBalancer`,
`skutil.preprocessing.UndersamplingClassBalancer` and
`skutil.preprocessing.SMOTEClassBalancer`. Each must

  - name `random_state` in its own signature — for the balancers, on
    `__init__`, following scikit-learn's convention so that `get_params` and
    `sklearn.base.clone` round-trip it; a `**kwargs` that silently absorbs the
    name does not satisfy this;
  - produce identical output when called twice with the same `int` seed within
    one process; and
  - when it is given an explicit `random_state` — an `int` seed or a
    `numpy.random.RandomState` — leave `numpy.random`'s global state exactly
    where it found it: a caller who seeds the global stream, then makes such a
    call, must observe the same stream position afterwards.

`random_state=None` keeps its historical meaning. `sklearn.utils.check_random_state`
— already a dependency of this package — resolves an `int` and a
`numpy.random.RandomState` to a generator of their own, and resolves `None` to
NumPy's own global `RandomState`; so an unseeded call still draws from the
global stream and still advances it, exactly as the tree does today. That is
the default, not a violation of the bullet above, and property 1 is about what
the repository's *own* files do, not about which object a caller's `None`
resolves to.

**3. Importing and compiling the package raises no warnings of its own.**
Compiling every Python source file in the repository and importing every module
of `skutil` under `python -X dev -W error` must raise no warning whose source is
a file inside this repository. On the tree you inherit it raises several, all
`DeprecationWarning`: a `collections` ABC imported from `collections` rather
than `collections.abc`, and string literals containing escape sequences Python
does not define (`'\$'`, `'\*'`, `'\w'`, …) written without an `r` prefix.
Warnings raised by NumPy, pandas, scikit-learn, Matplotlib or seaborn about
*their own* files are not yours and are out of scope.

These two are properties of the source, not only of import time: it is the
construct itself that must be gone, not merely the runtime warning on this
interpreter. In particular, importing an
ABC (`Sized`, `Iterable`, …) from `collections` rather than `collections.abc`
must not appear anywhere in the repository's source — guarded behind a `try:
from collections.abc import Sized` / `except ImportError: from collections
import Sized` compatibility shim or not. On this interpreter the first import
succeeds and the `except` branch is dead, so the shim raises no warning, yet the
deprecated import is still written in the source; the requirement is that the
source read the ABCs from `collections.abc`.

Silencing is not fixing. The number of suppressions in the repository may not
grow. The ones already in the tree may stay — removing them is
not this step's work — but nothing below may be added anywhere in it, in any
file:

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

**4. Every Python source in the repository parses on this interpreter.** Every
`.py` file in the tree, and the code cells of every `.ipynb` under it, must
compile under Python 3. Several do not today: the tree still carries Python-2
`print` statements, which are syntax errors on this interpreter. Of a notebook,
only the source of its code cells is Python; its outputs, its metadata and its
cell structure are not, and do not need to change. Nor are a code cell's
IPython magics and shell escapes — its lines that begin with `%` or `!` — which
are IPython syntax rather than Python and are not part of this property.

## 2. Preserve the behaviour of the tree you inherit

The observable behaviour of `/app` does not change in this step. Every public
entry point must return, for the same input, exactly what it returns today:
the same values, the same dtypes, the same column names and index, the same
exception types and messages on the error paths, the same fitted attributes.
A floating-point value is the same value when it differs from today's by no
more than 1e-15 plus 1e-12 of its magnitude; every other value — integers,
strings, dtypes, labels, order, messages — must match exactly. The code in front of you is the specification; read it.

Three invariants the tree already satisfies are easy to spend while doing the
work above, so they are called out:

  - `import skutil` must not disturb either global random stream, and neither
    must any module-level fixture the package exposes. Reseeding a global
    generator at import time, or from a `setup_module`-style hook, is not an
    acceptable way to make anything reproducible.
  - A seeded workload must produce identical results in two separate
    processes.
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

The repository's own tests must not regress: a test that passes on the tree you
inherit must still pass, and none may be deleted, renamed away, or have an
assertion removed. Some of them
currently fail; you are free to leave those failing, and free to fix them.

**Some rules admit no exception.** The suppression rule in property 3, and this
section's rules on the three invariants, the public names, keeping the code you
are handed, and the repository's tests keeping their assertions, must each hold
in full: no violation of any one of them is acceptable.

## 3. Build and run

The package builds with `setup.py`, which compiles a Fortran extension
(`skutil/odr/dqrsl.f`) and a Cython extension
(`skutil/metrics/_kernel_fast.pyx`); `gcc` and `gfortran` are installed.

```bash
python -m pip install --no-build-isolation .   # build and install from source, against the installed stack
python -m pytest --pyargs skutil # the repository's own tests
```

Run the tests from outside the source directory, or `import skutil` will find
the source tree instead of the installed package.

## 4. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, documents, and build files. Do not create a prose answer or an
  alternate entry point.
- Do not cheat by using online solutions or hints specific to this task.
- Do not weaken, filter, rename, skip, or delete the repository's tests.
