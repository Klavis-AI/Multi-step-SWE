# TEST_INTERFACE.md — the contract the checkpoint is read through

The checkpoint builds `/app` and then exercises the binary it produces. This
document fixes everything that crossing has to agree on: where the binary comes
from, how it is invoked, what it may assume about its environment, and what is
deliberately not looked at.

It is a fairness document. If a name, path, flag spelling, output format or
exit status matters to the checkpoint, it is written down here or in a document
named by this step's reading list. The checkpoint may combine documented
behaviour in ways this file does not enumerate; it will not require behaviour
that is written down nowhere.

Sections accumulate the same way `PROFILE.md`'s do: section *N* states what
step *N* added.

## The build

- The workspace root manifest is `/app/Cargo.toml`.
- The checkpoint runs, as an unprivileged user, from a cold target directory:

      cargo build --manifest-path /app/Cargo.toml --bin uv

  with `CARGO_TARGET_DIR` set outside `/app`, `CARGO_NET_OFFLINE=true`, and a
  `CARGO_HOME` pre-populated with every crate the lockfiles the checkpoint
  prewarms name — the releases this step spans, together with the starter tree
  the workspace was seeded from — so that the tree builds whether it is left as
  this step finds it or carried to this step's release. A build that needs a
  crate none of those lockfiles names cannot fetch it, and none of the
  behaviour in `PROFILE.md` requires one.
- The toolchain is the Rust release this step's release declares: **Rust 1.93**,
  the channel that release's own `rust-toolchain.toml` names. It is never older
  than the channel the inherited `/app/rust-toolchain.toml` names, and it
  compiles every version the inherited lockfile pins, so the tree builds whether
  it is left as this step finds it or carried to this step's release.
  `rust-toolchain.toml` in `/app` is not consulted at checkpoint time: pinning a
  different channel there changes nothing, and code that needs a channel
  *newer* than 1.93 can build here and still fail the checkpoint build.
- The binary is taken from `$CARGO_TARGET_DIR/debug/uv`. Nothing else in the
  target directory is read, and no other binary or library target in the
  workspace is invoked.
- Build warnings are out of scope. A build failure is fatal: the step fails,
  whatever else is in place.
- Deadlines are fixed and generous: the build is allowed 3600 s and the
  checkpoint observations 2700 s in total; a single observation is additionally
  bounded by the checkpoint's own per-probe timeout, 180 s unless a fixture
  declares less. A run that exceeds its deadline is treated the same as a build
  failure.
- The checkpoint build uses the dev profile with debug assertions and integer
  overflow checks disabled (`CARGO_PROFILE_DEV_DEBUG_ASSERTIONS=false`,
  `CARGO_PROFILE_DEV_OVERFLOW_CHECKS=false`), matching the release profile
  this task's ground truth was recorded from. A `debug_assert!` that fires
  only in a stock dev build — in particular one that rejects an interpreter
  whose short-named directory is a symlink, so that `sys.base_prefix` and
  `sys.base_exec_prefix` differ — is not a failure the checkpoint will see,
  and a plain `cargo build` locally may panic where the checkpoint binary runs
  clean.

## How the binary is invoked

- Each observation runs `uv` as a child process in a scratch working
  directory with its own `HOME`, its own `UV_CACHE_DIR`, and no inherited
  `VIRTUAL_ENV`. Nothing carries over between observations.
- `NO_COLOR=1`, `UV_NO_PROGRESS=1` and `COLUMNS=100` are always set, so
  nothing is coloured and anything rendered to a terminal width is rendered to
  100 columns: a diagnostic compared as one whole line does not match a run
  that wraps it. Output is additionally
  stripped of ANSI escapes, and wall-clock durations, absolute scratch paths,
  the index URL, the loopback port, the `CPython 3.x.y` version an interpreter
  reports and the `uv <version>` token are replaced with placeholders before
  anything is compared. Timings, temporary paths and
  the version string are therefore never part of a requirement.
- `UV_PYTHON_DOWNLOADS=never` is always set, and `UV_PYTHON_INSTALL_DIR` points
  at an empty scratch directory. Interpreters are never fetched from the
  network during a checkpoint.
- Package indexes are local: a PEP 503 simple index served over loopback HTTP
  from a directory of generated distributions. There is no access to PyPI and
  no network beyond that server.
- Trailing whitespace is stripped from every line and the comparison is made on
  the trimmed text.

## Interpreters available at checkpoint time

The checkpoint never installs, downloads or discovers an interpreter from the
network. The machine has a fixed set of CPython builds under
`/opt/pythons/`, one directory per minor version, and each observation puts the
requested build's `bin/` first on `PATH`:

    /opt/pythons/cpython-3.9-linux-x86_64-gnu
    /opt/pythons/cpython-3.10-linux-x86_64-gnu
    /opt/pythons/cpython-3.11-linux-x86_64-gnu
    /opt/pythons/cpython-3.12-linux-x86_64-gnu
    /opt/pythons/cpython-3.13-linux-x86_64-gnu

Each `bin/` holds `python`, `python<X>` and `python<X.Y>`, the first two being
symlinks to the third. `python3` on `PATH` before an observation's interpreter
is prepended is the system interpreter of the checkpoint image, which may be a
different version from any of the above — do not assume it is one of them.

`UV_PYTHON_DOWNLOADS=never` is set for every observation, so a request that can
only be satisfied by downloading an interpreter must fail rather than fetch
one. Managed-interpreter directories (`UV_PYTHON_INSTALL_DIR`) start empty and
are scratch.

## What is deliberately not looked at

None of the following is in scope, and a checkpoint result never depends on it:

- internal module and type layout, crate boundaries, file names inside a crate,
  or which of the workspace's crates a given responsibility lives in;
- the wording of a diagnostic beyond the fragments named in `PROFILE.md`, or
  which stream a purely informational line goes to — except where a command's
  own output is compared as a whole, as `uv run`'s passthrough of its child's
  streams is;
- allocation behaviour, timing, parallelism, or the order independent
  operations happen in;
- anything the candidate writes as its own tests, including whether those
  tests pass;
- cargo build warnings, or the exact set of `.rlib` files produced;
- the order of entries in a resolved set where `PROFILE.md` does not fix an
  order, and the order of keys in emitted JSON where it does not fix one.

## 1. the pip-compatible core

**Surfaces reached.** `uv --version`; `uv venv`; and `uv pip` with the
second-level subcommands `compile`, `install`, `sync`, `uninstall` and
`freeze`. Nothing else in the command surface is invoked at this step.

**What is read after a run.** Beyond exit status and the two streams: the file
named by `-o`/`--output-file`; inside a created virtual environment,
`pyvenv.cfg`, the entries of `bin/` whose names begin with `python`, and
`lib/python<X.Y>/site-packages`; inside `site-packages`, the set of
`*.dist-info` directory names and the text of the `WHEEL` file within a named
one. `uv pip freeze` is also run after an observation to read back the
environment it left; its lines are sorted before comparison, so their order is
not part of any requirement, and neither is the order of a directory listing.

**What the environment provides.** Observations create environments from more
than one of the interpreters listed above — 3.9 and 3.12 both appear — and
requirement sets are compared across them, so marker evaluation must follow the
environment rather than the process. `VIRTUAL_ENV` may be set to a path
relative to the working directory rather than an absolute one.

**What the index provides.** The generated distributions exercise per-version
`requires-python`, yanked files, versions offering only a wheel, versions
offering only a source distribution, and wheels carrying several compatibility
tags at once, including tags for interpreter versions and ABIs the environment
does not match. One observation builds a local directory whose `[build-system]`
declares an empty `requires` and a `backend-path` naming a module inside the
tree; that build runs with no index configured at all and must not reach for
one.

**Non-requirements specific to this step.** The header and annotation comments
of a compiled requirement set are no part of any comparison *this* step's
observations make: where the output is compared as text both are suppressed on
the command line first, where it is compared as the set of pins it names every
comment is discarded before the comparison, and where it is not read at all
only the existence of the output file is checked. That is a statement about
this step's observations and not a general policy — a later section may
compare a compiled file as a whole with its header comments included. The wording of the diagnostics emitted when a resolution
fails, or when an uninstall finds nothing to remove, is never compared; those outcomes are read as an exit status plus the state of the
environment afterwards. No observation invokes `install`, `sync`, `uninstall` or `compile` with an
empty argument list, so the four "requires at least one source" rejections
`PROFILE.md` § 1 states are not exercised by this milestone's observations.

## 2. find-links, hashes, editables, constraints and overrides

**Surfaces reached.** `uv cache dir`, and `uv pip list`, `uv pip show` and
`uv pip check` alongside the subcommands § 1 of `TEST_INTERFACE.md` named. The
JSON payload of `uv pip list --format=json` is a new emitted format and is
compared as the whole of stdout, byte for byte after the normalisation above —
punctuation and key order included.

**New paths read after a run.** `<venv>/CACHEDIR.TAG` and `<venv>/bin/activate`
by existence only; the absence of a `setuptools` entry in a seeded
environment's `site-packages`; and the `__pycache__` entries beside an
installed module, whose names carry the target interpreter's version tag.

**What the environment provides.** Observations set variables in the child
process — among them `UV_INDEX_URL`, `UV_EXTRA_INDEX_URL` and `UV_PRERELEASE`,
and variables with names of the fixture's own choosing that a requirements file
interpolates into an include path. One observation points `--index-url` at a
loopback port with nothing listening while an extra index serves the package,
so index priority has to be settled before an index is contacted rather than
after a failure.

**What the index provides.** The same generated distributions are reachable
both as a simple index and as a flat directory listing suitable for
`--find-links`. The universe includes packages declaring extras, extras whose
requirement lists name further extras of the same package, versions offering
only a source distribution alongside lower versions offering a wheel, and the
seed packages `pip`, `setuptools` and `wheel` so that a seeded environment can
be created without the network.

**Non-requirements specific to this step.** The `columns` rendering of
`uv pip list` and everything `uv pip show` prints are never compared; those are
read as exit status alone. The annotation layout `--annotation-style` selects
is not compared, nor is the effect of `--no-strip-extras` or `--unsafe-package`
on the output text — only that the option is accepted and the run succeeds. The
contents of `CACHEDIR.TAG` and `bin/activate` are never read, and neither is the
prompt prefix a `--prompt` value writes into them.

Three rejections `PROFILE.md` § 2 states are likewise never compared: the
`--pre`/`--prerelease` conflict, the `--no-build`/`--only-binary :all:` conflict
and the `--no-index`/`--index-url` conflict. Each is rejected identically at the
previous milestone and at every milestone after this one, so no group this step
can own distinguishes a candidate that implements them — they are behaviour the
checkpoint does not measure, and a solution is neither helped nor hurt by
them.

Two further outcomes are recorded as observable but are not fixed at a single
milestone, so no group here compares them: the absolute path `uv cache dir`
writes to stdout, and the default prompt prefix an environment without
`--prompt` receives. Both change value more than once across the range this
step's groups are required to hold over, which makes them properties of the
releases rather than of this milestone. The `--seed` rule that `setuptools` and
`wheel` are installed only for interpreters older than Python 3.12 *is*
compared, on both sides of that boundary.

## 3. interpreter discovery redefined

**Surfaces reached.** No new subcommand. What is new to the contract is
`-p`/`--python` on the `uv pip` subcommands that act on an environment, the
widened grammar of an interpreter request, and the `UV_PYTHON` variable. No new
output format is emitted.

**How the choice is observed.** Which interpreter satisfied a request is read
from the filesystem, never from printed text: the name of the single
`lib/python<X.Y>` directory inside the environment that was created, and the
`site-packages` path a distribution lands in. An interpreter request that is
expected to go unsatisfied is observed as the *absence* of that directory.

**What the environment provides.** Observations put more than one of the
interpreters listed above on `PATH` at the same time and choose the order
deliberately — sometimes newest first, sometimes oldest, sometimes with two or
three unsuitable candidates ahead of the suitable one. The order of directories
in `PATH` is the search order the observation is written against. The
installation directories under `/opt/pythons` are also passed directly as the
value of an interpreter request, so their on-disk shape — an installation root
containing `bin/` — is part of what a path request must accept. Because
`UV_PYTHON_DOWNLOADS=never` is always set, a request no interpreter on the
machine satisfies has nowhere to fall back to and must not acquire one.

**Non-requirements specific to this step.** For a request that cannot be
satisfied, neither the exit status nor the wording of the diagnostic is
compared; the requirement is only that no virtual environment is left behind.
The order in which candidate interpreters are queried is not observed directly,
only through which one is finally chosen.

Two further limits follow from what an observation can be built out of here.
Every index this checkpoint serves is a PEP 503 HTML simple index: no JSON
representation is served or content-negotiated, so only the HTML spelling of the
renamed attribute is exercised. And because both spellings of that attribute
name the same detached metadata file, the tie-break between them is not
observable — the comparison is that resolution follows the detached metadata
when the renamed attribute advertises it. Whether a directory named by
`VIRTUAL_ENV` is itself a virtual environment is likewise not compared: the
variable is only ever observed naming an environment that exists.

## 4. the project, tool, script and python interfaces

New command surface reached at this step: `uv init`, `uv add`, `uv remove`,
`uv lock`, `uv sync`, `uv run`, `uv tree`, `uv tool` with the subcommands
`install`, `run`, `list`, `uninstall` and `dir`, and `uv python` with the
subcommands `find` and `pin`.

New files read after a run, by path relative to the scratch working
directory: `uv.lock` and `.python-version` at the project root, the
scaffold's `pyproject.toml` and `README.md` under the directory `uv init`
was given, the script file `uv add --script` edited, the project environment
under `.venv/`, and the executables under the directory named by
`UV_TOOL_BIN_DIR`.

`uv.lock` is parsed as TOML. Only its `package` array of tables is read, and
within each element only `name` and `version`; an element named `project` —
the project's own entry, not a distribution — is not compared. Nothing else in the file is
compared, so key order, layout, comments, `source` records, marker guards
and hashes are free. Where a resolution records several versions of one
package, the comparison keeps the one written last, which is why the
ordering § 4 fixes is part of the contract.

A script carrying more than one `script` block is not exercised either: no
probe supplies one, so the file PEP 723 requires an error for is out of scope.

Environment the observations rely on:

- `UV_TOOL_DIR` and `UV_TOOL_BIN_DIR` are both set to absolute directories
  inside the scratch working directory, as are `XDG_BIN_HOME`,
  `XDG_DATA_HOME` and `XDG_CONFIG_HOME`, which point at directories inside
  it as well. `uv tool dir` is compared against the value of `UV_TOOL_DIR`.
- `COLUMNS=100` is set alongside `NO_COLOR=1`, so any output that is
  rendered to a terminal width is rendered to 100 columns, and nothing is
  coloured. The whole-string comparisons here — the `uv tree` rendering and
  the `uv tool list` block — are made against that rendering.
- Project and tool fixtures declare an in-tree PEP 517 backend through
  `build-system.backend-path`, with `requires = []`. Builds must therefore
  succeed with no build requirement fetched and no network beyond the local
  index.
- The pinned-version and `requires-python` observations expect a version
  other than the one first on `PATH` to be found by the ordinary search § 3
  fixed; no interpreter is downloaded.
- Resolution fixtures pass the interpreter to resolve against as an absolute
  path to `--python`, and reach the index through `--index-url` on the
  command line or through `[tool.uv] index-url`. Their `pyproject.toml`
  files carry markers naming platforms and implementations the checkpoint
  does not run on, so nothing may be skipped for being unreachable here.

Not required at this step: the stdout of `uv python find`; any file the
`uv init` scaffold writes beyond `pyproject.toml` and `README.md`; the
layout of the tools directory and of a tool's receipt; the contents of a
generated executable; and the wording of any diagnostic, since no
requirement here reads stderr.

## 5. packaging as an opt-in, workspaces, sources, build and export

New command surface: `uv build` and `uv export`.

New paths read after a run, relative to the scratch working directory:
`dist/` and its contents, the directory named by `uv build -o` and its
contents, the file named by `uv export -o`, the project environment at the
directory `UV_PROJECT_ENVIRONMENT` names, `.venv/bin/` as well as
`.venv/lib/python*/site-packages/`, and the `src/` trees `uv init` writes
under the directory it was given. Absence is read as well as presence: after
`uv init --no-package` it is observed that no `src/` tree exists under the
named directory, and after a `uv build` given `-o` that no `dist/` was
created under the project root.

Distribution file names inside `dist/` are compared exactly, so the wheel and
source-distribution naming rules of the inherited specifications are part of
this step's contract. The contents of those archives are not read. The file
`uv export` writes is checked for existence only; its text is not compared.

Environment the observations rely on:

- Fixtures that build supply an in-tree PEP 517 backend through
  `build-system.backend-path` with `requires = []`. Building must never
  need a build requirement from the network.
- Many runs at this step pass the global flag `--offline` — on the project
  commands, the build commands and the `uv pip` commands alike. It takes no
  value, is accepted wherever a global flag is accepted, and forbids network
  access for that run rather than changing its result: every run that passes
  it is given a fixture and an index needing no fetch, so the flag decides
  nothing. A run that would succeed without it exits `0` and produces exactly
  what the same command without the flag produces, and a run that this
  milestone's validation refuses is refused just the same, with the status
  that refusal carries. What a command does when `--offline` blocks a fetch it
  does need is not observed here.
- Workspace fixtures place members under a directory matched by a `members`
  glob; members and roots are ordinary `pyproject.toml` files, with no
  version-control metadata present anywhere in the tree.
- `uv pip freeze` is run against the project environment after a sync to
  read back what was installed, so an environment this step creates must be
  legible to the pip-compatible commands of § 1.

Not required at this step: the text of the exported requirements file,
including any header; the contents of `dist/.gitignore`; which build
backend a scaffolded project declares and what `uv init` writes into a
generated module or script beyond the paths named in § 5; what `uv init`
scaffolds when given no layout flag, neither the text of its
`pyproject.toml` nor whether a `src/` tree is absent; the outcome of
`uv export --format` given any value other than `requirements-txt`; and
stderr wording, since no requirement here reads stderr.

## 6. the breaking batch

New command surface: the global options `--allow-insecure-host` and
`--directory`, the `-` argument to `uv run`, and the group, exactness,
outdated, files, dry-run and header flags § 6 names.

`uv run -` is the only place the checkpoint writes to standard input; a run
must read the script from there and must not wait for input otherwise.

Observations reach into the created environment: a Python process started
through `uv run --no-project` prints `sys._base_executable` and reads
`pyvenv.cfg` from `sys.prefix`. The `home` line is compared as the whole
line, `home = <directory>`, so the spacing around `=` is part of the
contract, and the compared paths are the managed-interpreter root with the
scratch location replaced by a placeholder.

Environment the observations rely on:

- The managed-interpreter root holds version-only directory names, such as
  `cpython-3.12-linux-x86_64-gnu`, which are symbolic links to
  full-version directories. The checkpoint hands uv the path under the
  version-only name and expects it back unchanged, so nothing may
  canonicalise it.
- Both 3.11 and 3.12 are available; 3.8 is not, and is what the
  unsatisfiable-request observations request.
- `--directory` targets are ordinary directories created by the fixture with
  a placeholder file in them; they contain no `pyproject.toml`, so the
  `.python-version` search runs over plain directories.
- Index universes include releases whose only published version carries a
  local segment, and releases published several times under different local
  segments, some of whose distributions carry tags that do not match the
  platform under test.

Not required at this step: the wording of any diagnostic, including the one
naming a rejected `uv.toml` setting; the exit status of a `uv venv` whose
interpreter request cannot be satisfied, only the absence of the
environment; the output format of the outdated and files reports; and the
text `uv lock --dry-run` prints.

## 7. the forking universal resolver

New surfaces reached at this step: `--universal` on `uv tree` as well as on
`uv pip compile`; `--frozen` on `uv tree`, with and without `--universal`; `--fork-strategy` on
`uv pip compile`, `uv lock` and `uv sync`; the
`fork-strategy` and `conflicts` keys of `[tool.uv]` in `pyproject.toml`. No new file is
produced: `uv lock` writes `uv.lock` at the root of the project, as before.

What the observations rely on:

- Resolutions are produced on Linux x86-64, and every marker environment other than that
  one is reached only symbolically. A universal resolution is read from stdout and compared
  as exact text, so the marker written beside an entry, the order of entries, and the
  absence of a marker on an unguarded entry all matter.
- The served index carries a `requires-python` value per distribution, and forks are made
  from those values without downloading or building anything.
- An observation that installs a fork runs against one of the CPython builds this document
  names for 3.9 through 3.13, and reads the distributions installed into the project
  environment.
- `uv tree` output is compared as exact text, including its box-drawing characters and the
  annotation on an entry reached through an extra.

Explicit non-requirements for this step:

- The internal layout of `uv.lock` is not read. What is observed is that `uv lock` writes
  it, and that a later `--frozen` command — `uv sync --frozen` or `uv tree --frozen`, with
  or without `--universal` — reproduces the fork selection from it without resolving again;
  the key names, key order and formatting inside the file are out of scope.
- The wording of the diagnostic for an invalid `conflicts` declaration, or for a request
  that enables two conflicting items, is not read. Only the exit status, and the absence of
  a lockfile where § 7 of `PROFILE.md` says none is written, are observed.
- Which fork a resolution reaches first, and the order in which forks are computed, are not
  observed; only the resulting set of entries and the order in which they are printed.

## 8. request validation and the subprocess contract

New surfaces reached at this step: `--dry-run`, `--active` and `--no-default-groups` on
`uv sync`; `--invert` together with `--no-dev` on `uv tree`; the `-p` alias on
`uv pip compile`; `uv tool run` with an interpreter as its command; `uv init --bare`;
`uv publish`; and the `UV`, `UV_PYTHON`, `UV_NO_BINARY` and `UV_NO_BINARY_PACKAGE`
environment variables. No new output format is emitted.

What the observations rely on:

- Installed distributions are read from the environment's
  `lib/python<major>.<minor>/site-packages` directory as `*.dist-info` entries, so an
  environment built for 3.12 is read at `lib/python3.12/site-packages`. An observation of
  `uv sync --active` runs with a virtual environment already created and activated at
  `.venv` and with `UV_PROJECT_ENVIRONMENT` naming a different directory; both are read.
- A spawned process may be an ordinary script in the working directory that inspects its own
  environment and chooses its exit status, or a process that arranges to be terminated by
  `SIGTERM`. The signal is delivered to the child, not to `uv`.
- The index serves at least one package that publishes wheels and no source distribution.
- `uv tool run` is observed without network access, so an interpreter request it can satisfy
  locally must succeed there.
- `uv init` is observed by the set of paths it leaves under the created project directory.

Explicit non-requirements for this step:

- The wording of the diagnostic for an unknown extra, an unknown group, or a resolution that
  has no solution under a requested interpreter is not read; only the exit status.
- What `uv sync --dry-run` prints is not read, and neither is the content of any file
  `uv init` writes — only which paths exist afterwards.
- The absence of a preview warning from `uv publish` is observed through the one diagnostic
  line § 8 of `PROFILE.md` quotes, which is the whole of that command's error output; no
  other `uv publish` behaviour is exercised.

## 9. project versions and the standard lock file

New surfaces reached at this step: `uv version` with `--short`, `--bump` and
`--preview`; `uv self version`; `uv python dir`; `uv init --build-backend`; and the
PEP 751 output format, which `uv pip compile -o` and `uv export -o` may now write to a file
whose name PEP 751 reserves. `uv.lock` is unchanged, and the absence of a `uv.lock` or of a
`.venv` after a refused run is what is read in place of a diagnostic.

What the observations rely on:

- The user-level data directory is a directory inside the scratch area, so the paths
  `uv python dir` and `uv tool dir` print are stable and are compared as exact text.
- Managed CPython installations live under a root the observation knows, and the path
  `uv python find` prints for an unconstrained request names a build in it. That path is
  compared as exact text, and for a managed build it is the `python` entry of the build's
  `bin/` directory, not one of the `python3` and `python3.<minor>` names beside it. Nothing
  is downloaded to satisfy such a request.
- A whole file may be read back and compared as exact text, `pyproject.toml` included, so
  the formatting an edit leaves behind matters as much as the value it changes.
- An executable whose name collides with a package name may exist on `PATH`, and the index
  may serve a package that installs no executables at all.

Explicit non-requirements for this step:

- The contents of a `pylock.toml` are not read at this step; § 11 and § 12 are where an
  observation reads them, of a file uv emits and of one given to it as an input alike.
  Here PEP 751 governs them, and what is observed is only which file names are accepted
  in an input or an output position, and that a refused output leaves no file behind.
- The wording of every diagnostic this step introduces — a rejected TOML file name, an
  unknown dependency group object specifier, two conflicting options, a command that the
  requested package does not provide — is not read; only the exit status and the files that
  do or do not exist afterwards.
- The version string `uv self version` prints is not read, only that it exits cleanly and
  says nothing on stderr. The `--output-format json` form of either version command is not
  exercised.

## 10. managed interpreters on `PATH`, virtual dependencies and exit-code conventions

New command surface reached at this step: `uv python upgrade`; the options
`--bin`, `--no-bin` and `--default` on `uv python install`; `--rm` and
`--global` on `uv python pin`; `--check` on `uv lock` and on `uv sync`;
`--project` on `uv sync`; `--workspace` and `--no-workspace` on `uv add`;
`--build-backend` on `uv init`; and `--python-platform` on `uv pip compile`.

New inputs and outputs read after a run, by path relative to the scratch
working directory: `uv.toml` at the root, read as configuration by every
command; `.python-version`, whose *absence* after a run is what is compared;
`uv.lock`, likewise compared for absence; and the environment of a project
created elsewhere in the tree, e.g. `demo/.venv/lib/python3.12/site-packages`.

Environment the observations rely on:

- `UV_PYTHON_DOWNLOADS` is `never` and the managed-install directory is empty,
  so `uv python install` never gets past the download policy. Nothing is
  downloaded, and no executable is ever actually linked into the executable
  directory; only the refusal is observed.
- No interpreter matching `3.7` is reachable, and the interpreters that are
  reachable are the ones the common preamble lists.
- The path-source and workspace-member fixtures deliberately omit
  `[build-system]`, and no build requirement can be fetched, so the build that
  this step newly performs cannot succeed. Only its failure is observed.
- The local index serves a distribution whose sole wheel is tagged
  `py3-none-manylinux_2_24_x86_64` and which publishes no sdist.

Not required at this step: the wording of any diagnostic other than the two
quoted in § 10 — the `uv.toml` rejection, the `--check` out-of-date report and
the `uv version` failure are read by status and stdout only; the exit status of
a lock that fails on conflicting `editable` annotations, of which only the
absent lockfile is read; the stdout of `uv python upgrade`; registration of
installed interpreters with a platform registry; and any confirmation prompt
`uv venv` may show before replacing an existing environment, since no
observation here is interactive.

## 11. the default Python version, markers for extras and groups, and project-less roots

New command surface reached at this step: `uv auth` with the subcommands
`login`, `logout` and `token`; `--force` on `uv cache clean` and
`uv cache prune`; `--editable` and `--no-editable` on `uv export`;
`--only-group` on `uv sync`; and the iOS values of `--python-platform`.

New files read after a run, by path relative to the scratch working
directory: `pylock.toml` and `req.txt` where `uv export -o` is told to write
them, the build output directory `dist/` at the workspace root and the
absence of `child/dist/`, and `pyvenv.cfg` both at the root and under
`.venv/`. `pylock.toml` is compared as whole text, so its key order and
inline-table spelling are fixed by § 11, not free.

Environment the observations rely on:

- Every observation at this step runs against the interpreters the common
  preamble lists, with Python downloads disabled. **No 3.14 interpreter is
  present and none can be fetched.** The default-version rule is therefore
  observed through what a request resolves to and through the diagnostics
  § 10 and § 11 fix for an unsatisfiable request, never through a 3.14
  interpreter being selected; every observation that needs an interpreter
  names 3.12 or is bounded by a `requires-python` that 3.12 satisfies.
- The marker observations run with no network: a requirement carrying an
  `extras` or `dependency_groups` marker must be dropped without any index
  request, since none could succeed.
- `https://example.invalid` is used precisely because it does not resolve;
  the credential lookup must fail before any request is attempted.
- The build fixtures use uv's own build backend, so a build needs nothing
  fetched.

Not required at this step: the exit status of `uv auth token`, of which only
stderr is read; the stdout of `uv build`, `uv cache clean` and
`uv cache prune`; the exit status of a `uv lock` that emits a `[tool.uv]`
warning; the contents of `dist/.gitignore`; and the wheel or sdist contents
beyond their filenames.

## 12. destructive and ambiguous operations become errors

New command surface reached at this step: `uv workspace` with the subcommands
`dir` and `list`, `--package` on `dir` and `--paths` on `list`; `--clear` and
`--relocatable` on `uv venv`; the environment variables `UV_VENV_CLEAR` and
`UV_NO_DEFAULT_GROUPS`; `--no-binary` and `--only-binary` alongside
`--format pylock.toml` on `uv pip compile`; and comma-separated values for
`--extra`.

New files read after a run, by path relative to the scratch working
directory: `pylock.toml` where `-o` is told to write it, compared as whole
text — its key order, its inline-table spelling and the two-line header's flag
order are contract, and § 9's non-requirement does not reach a file this step
emits; `config/uv/.python-version` and a `pylock.toml` another tool could have
written, both *inputs* rather than outputs; and, under a created environment,
`pyvenv.cfg`, `bin/activate`, `bin/activate.csh` and the interpreter link
itself — `bin/python3.12` where the environment was built from the 3.12
interpreter, `bin/python3.11` where a honoured pin sent it to 3.11. The two
activation scripts are read for whether they exist, and `bin/activate` also
for whether it spells the environment's own directory as an absolute path.
Nothing else in either script, and no part of its shell syntax, is compared.

Environment the observations rely on:

- `XDG_CONFIG_HOME` is a directory inside the scratch working directory, so a
  fixture file written at `config/uv/.python-version` is uv's global pin. No
  other global configuration is present.
- The 3.12 interpreter these observations run against is a specific patch
  release, and the `requires-python` uv records in an emitted `pylock.toml` is
  that full version. It is compared literally.
- The local index is served over loopback, so the `url` values recorded in
  `pylock.toml` carry the host, the port and the fixture's own path segment,
  and the `sha256` values are those of the fixture's own artefacts. Both are
  compared as part of the whole file. It is a PEP 503 HTML index and publishes
  neither a size nor an upload time for any artefact, so of the optional keys
  § 12 lists the emitted file carries none: `upload-time` and `size` have no
  source here, and `name` is omitted because each `url` already ends in the
  artefact's filename.
- A `pylock.toml` given as an *input* is a fixture the observation wrote, not a
  file uv produced. It is offered with no index configured, and names artefacts
  on the loopback index by URL. What is read back is the environment the run
  left: which distributions are installed, that an entry whose marker is false
  for the running platform contributes none of them, and that a recorded hash
  which does not match the artefact leaves nothing installed. § 9 fixes the
  input positions and `/app/SPECS/specs/pep-0751.rst` fixes the procedure; this
  step adds nothing to either and only checks that both still hold.
- A replacement observation needs an environment to already exist, created
  either by an earlier command in the same run or as the run's active
  environment. Both cases must be refused alike, and a refusal is read as the
  exit status *together with* the environment still being there afterwards —
  its `pyvenv.cfg` and its `bin/python3.12`.

Not required at this step: the wording of the diagnostics *this step* adds, no
observation of which reads stderr — every refusal § 12 introduces is read as
its exit status together with the state it left behind. That licence covers
this step's own refusals and no others: a handful of inherited observations,
in the gate groups for steps 08 to 11, do compare a stderr line of the
milestone that fixed it, and those milestones' wording still has to hold.
Also not required: the stdout of `uv venv` and of `uv sync`; anything in an
activation script beyond the two properties above; and any listing mode of
`uv workspace list` other than names and `--paths`.

Out of scope at this step, and asserted nowhere: reading a `pylock.toml` from
a URL rather than from a path, since no observation has a network beyond the
loopback index; `--clear` on any command other than `uv venv`; the cache
commands, whose behaviour § 2 and § 11 fix and which this step leaves
unchanged; and `uv workspace metadata`, the third subcommand this release adds
under `uv workspace`, whose output no observation reads — it is preview-gated
and warns that it is experimental, and only `dir` and `list` are required
here.
