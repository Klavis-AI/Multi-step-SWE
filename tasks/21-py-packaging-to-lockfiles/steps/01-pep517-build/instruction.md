# Step 1 — Build bento through PEP 517

## 1. Implement this milestone

`/app` is the Bento 0.2.0 source tree. It builds itself: `setup.py` imports
`bento.distutils.monkey_patch`, so producing a distribution requires the
distribution to already be importable, and every metadata field is computed at
build time by running Bento's own parser over `bento.info`.

Replace that with a standards-based build, changing nothing about what Bento
does. When this milestone is finished, all of the following hold.

**The declaration.** `pyproject.toml` sits at the repository root, parses as
TOML, and conforms to the pyproject specification. Its `[build-system]` table
declares a non-empty `requires` list and a `build-backend`. The backend is a
third-party build backend that exists independently of this project and is
resolved from the build environment: neither `requires` nor `build-backend` may
name `bento`, the backend module may not be a file or a package sitting in this
repository, and `backend-path` — the key that points the frontend at a backend
shipped inside the tree being built — may not be used at all.

**The artifacts.** Building the project at the repository root, in an isolated
build environment, produces exactly one wheel named `bento-0.2.0-*.whl` and
exactly one source distribution named `bento-0.2.0.tar.gz`. Both pass a strict
check of their metadata, including that the long description declares a content
type and renders.

**The metadata.** The built distribution's core metadata is version 2.1 or
later and says about this distribution what `/app/bento.info` says about it:

- the name and the version;
- the summary;
- the author and the author's e-mail address, and the maintainer and the
  maintainer's e-mail address, in whichever core-metadata fields the standard
  spelling of those people puts them;
- the project's URL, as the home page or as a project URL;
- every keyword it declares;
- every classifier it declares that is not a `License ::` classifier (the
  licence is a field of its own, next line);
- the licence it declares, carried in the metadata's licence field, its licence
  expression, or a licence classifier;
- the long description: the exact contents of the file named by
  `DescriptionFromFile`;
- the console script declared by the `Executable:` block, as an entry point in
  the `console_scripts` group, pointing at the module and function it names;
- and the source distribution's `PKG-INFO` agrees with the wheel's `METADATA`
  on name, version and summary.

**The contents.** The wheel ships everything the `Library` section of
`bento.info` declares: every package listed under `Packages:` as an importable
package directory — including the vendored trees under `bento/private/` —
and the module listed under `Modules:`. Shipping less of Bento is not made
acceptable by declaring less of it: whatever `bento.info` says, the wheel
carries `bento` and its `backends`, `commands`, `compat`, `convert`, `core`,
`core.platforms`, `parser`, `pypi` and `utils` subpackages, and
`bentomakerlib`. It carries the data files `bento.info`
declares under `DataFiles: windows_arch_data` — `bento/commands/cli.exe` and
the executables under `bento/commands/wininst/` — at those paths inside the
package, byte for byte as they are in the tree as you received it. The wheel's
`.dist-info` directory carries the licence file. The source distribution carries
`bento.info`, the hook file `bscript`, and the `README.rst` and `LICENSE.txt`
the metadata refers to. Unpacking that source distribution somewhere else and
building it produces a wheel with the same filename and the same set of members
as the wheel built from the tree.

**Migrated, not renamed.** `setup.py` is gone. No module outside
`bento/distutils/` imports `bento.distutils`, by `import` statement or by name
through `importlib` or `__import__`. The distribution's metadata is declared in
a file that is read, not computed by code that is run: that file states this
distribution's name and its version outright, rather than marking either of
them to be produced at build time.

**No escapes.** The build must succeed with build isolation on and with the
backend's build dependencies actually checked, and without setuptools' legacy
distutils shim. Leaving a switch in the tree that turns off build isolation,
turns off build-dependency checking, or re-enables that shim is not allowed,
even if the build would have passed without it.

**Behaviour is unchanged.** The distribution installs and runs on the
interpreter this workspace runs, CPython 3.11, and does not declare itself
incompatible with it: if it states a required Python version at all, that
statement admits this interpreter. Installing the wheel into an empty virtual
environment gives a distribution that imports as `bento` and `bentomakerlib`
and a working `bentomaker` command. Everything the library and the command line
did before, they still do, identically: the same return values, the same
objects, the same messages on standard output and standard error, the same
exit statuses — including the
cases where the tree as you received it raises or fails. This is a packaging
migration; it is not a bug-fixing exercise, and a behaviour change is a
regression even when it looks like an improvement.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/01-pep517-packaging.md` lists the packaging
standards this milestone is defined by — PEP 517, PEP 518, PEP 621, and the
PyPA specifications for `pyproject.toml`, core metadata, entry points, and the
two distribution formats — with the canonical URL of each, plus the
backend-specific documentation for the common backends.

**Additional files:** `/app/bento.info` is the authoritative statement of this
distribution's identity, and `/app/DOCS/01-pep517-packaging.md` §2 explains how
to read it. `/app/setup.py`, `/app/bootstrap.py` and `/app/bento/distutils/`
are the build as it stands today. `/app/README.rst`, `/app/LICENSE.txt` and
`/app/bscript` are named by `bento.info`. Record what you changed in
`/app/WORKSPACE_HANDOFF.md`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /opt/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`; preserve inherited source, manifests,
  documents, and handoff files.
- Do not cheat by using online solutions or hints specific to this task.
- Leave a reproducible handoff.
