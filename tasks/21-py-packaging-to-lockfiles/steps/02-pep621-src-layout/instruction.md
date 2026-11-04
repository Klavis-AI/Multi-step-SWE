# Step 2 — Two distributions: a src layout, one version, and a real extension

## 1. Implement this milestone

`/app` now builds through PEP 517, but it is still shaped like a 2008 checkout.
The importable packages sit at the repository root, where every `python`
invocation started there picks them up instead of the installed distribution.
The version the package reports comes from a module Bento writes at install
time, so a checkout says `nobuilt`. And the C extension this repository ships
as an example is Python-2 source built by its own `setup.py`.

Three things change. Nothing about what Bento does changes.

**The layout.** The importable trees move under `src/`: `src/bento/` and
`src/bentomakerlib/`, with everything that was inside them still inside them.
Neither exists at the repository root any more, and running the interpreter
from the repository root no longer imports them — an import of `bento` there
fails, because the only `bento` that exists is the installed one. The built
wheel is unaffected by the move: it carries the same members at the same paths
inside it as before, `src` is not a package, and no `src/` prefix appears in
the wheel, in a module name, or in an import. The source distribution is the
tree rather than the installed layout, so it carries the two trees where they
now live, under `src/`.

**The version.** The distribution states its version once, and states it
outright, as its declaration already does: a string written in the file that
declares its metadata, not listed in `dynamic` to be produced at build time —
from `bento.info`, from a module, or from anything else. The package reports
that same string at run time by reading its own installed metadata — the string
it reports is whatever the installed distribution's metadata says, and follows
it if that changes. Installed into an empty virtual environment:
`bento.__version__` is the version the wheel's `METADATA` declares, and the
`bentomaker` console script prints that same string for `--version` and in its
banner. No Python module in the distribution carries the version as a literal,
and nothing in the tree imports `__package_info` — the module that used to be
generated at install time — by `import` statement or by name through
`importlib` or `__import__`. `bento.__git_revision__` keeps existing and keeps
being a string; what it says is not part of this milestone.

**The extension.** `examples/simples/single_extension` is a complete
distribution in miniature — a package, a C extension module, a C library, and
its own `bento.info` — written for Python 2 and built by `setup.py`. It becomes
a real distribution:

- It declares itself in a `pyproject.toml` of its own: the name and version its
  `bento.info` declares, stated outright rather than listed in `dynamic`, a
  `[build-system]` table whose backend is resolved from the build environment,
  and no `backend-path`. It has no `setup.py`.
  The extension moves onto one of the two build backends made for compiled
  code: that `build-backend` is scikit-build-core's (`scikit_build_core.build`)
  or meson-python's (`mesonpy`).
- Building it at its own directory, in an isolated build environment, produces
  exactly one wheel and exactly one source distribution. The wheel carries
  compiled code, so it is not `none-any`: it is tagged for this interpreter and
  this platform, and it contains the extension module `hello._bar` as an
  importable extension, built from `src/hellomodule.c` against the Python 3
  C API.
- Unpacking that source distribution somewhere else and building it produces a
  wheel with the same filename and the same set of members as the wheel built
  from the tree.
- Installing that wheel into an empty virtual environment on the interpreter
  this workspace runs, CPython 3.11, gives `import hello`, with `hello.hello()`
  printing `Hello from C` and `hello.foo()` printing `foo`, each on its own line
  and each returning `None`, and `hello.bar.foo` is that same function.
  `hello.hello` is the `hello` that the extension module `hello._bar` itself
  provides, and importing `hello._bar` gives a module loaded from compiled
  code. Calling `hello.hello` with extra positional arguments prints and
  returns the same thing; the arguments are ignored.

**Behaviour is unchanged.** Everything the `bento` library and the `bentomaker`
command line did before, they still do, identically: the same return values,
the same objects, the same messages on standard output and standard error, the
same exit statuses — including the cases where the tree raises or fails. The
one deliberate exception is the version itself: reporting the real version
where a checkout used to report `nobuilt` is what this milestone is for, and is
not a regression. Everything this milestone does not name stays as it is; this
is a packaging migration, not a bug-fixing exercise, and a behaviour change is
a regression even when it looks like an improvement.

**Still true.** The root distribution keeps everything it already has — the
`pyproject.toml` declaration, the name and version stated outright in the file
that declares its metadata, the isolated build of both artifacts, the
metadata, the packages and data files the wheel ships, the sdist that rebuilds,
no `setup.py` at the repository root, nothing anywhere in the tree importing
`bento.distutils` — by `import` statement or by name — outside that package's
own modules, and no build-escape switch anywhere in the tree. All of that still
holds when this milestone is finished.

**Not part of this milestone.** Stated so you do not have to guess:

- `bento.info`'s own path fields (`ConfigPy:`, `MetaTemplateFiles:`) and the
  `CompiledLibrary: foo` that `src/hellolib.c` implements. Whether they follow
  the move is your call.
- Where the `hello` distribution's directory sits. Keep it where it is or move
  it — it is found by the name it declares, not by its path.
- Anything in the tree this milestone does not name. Files that live inside
  `bento/` or `bentomakerlib/` move with them and are otherwise untouched;
  everything else stays where it is. Nothing here asks you to change them —
  which is not the same as nothing reading them: the behaviour this milestone
  preserves runs through them, and the two bans above are whole-tree.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/02-layout-and-extensions.md` lists the
specifications this milestone is defined by — the src-layout discussion, the
packaging specifications, `importlib.metadata`, and the Python 3 C API's module
initialisation — with the canonical URL of
each, plus the documentation of the extension-capable build backends installed
here.

**Additional files:** `/app/bento.info` remains the authoritative statement of
the root distribution's identity — what the version your declaration states
outright has to say — and `/app/examples/simples/single_extension/bento.info`
of the extension distribution's. `/app/bento/__init__.py` is where the version
comes from today and `/app/bento/__package_info.py.in` is the template it is
generated from. Append what you changed to `/app/WORKSPACE_HANDOFF.md`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /opt/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`; preserve inherited source, manifests,
  documents, and handoff files.
- Do not cheat by using online solutions or hints specific to this task.
- Leave a reproducible handoff.
