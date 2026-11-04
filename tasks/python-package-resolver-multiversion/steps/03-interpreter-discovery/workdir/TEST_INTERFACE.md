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
- The toolchain is the Rust release this step's release declares: **Rust 1.78**,
  the channel that release's own `rust-toolchain.toml` names. It is never older
  than the channel the inherited `/app/rust-toolchain.toml` names, and it
  compiles every version the inherited lockfile pins, so the tree builds whether
  it is left as this step finds it or carried to this step's release.
  `rust-toolchain.toml` in `/app` is not consulted at checkpoint time: pinning a
  different channel there changes nothing, and code that needs a channel
  *newer* than 1.78 can build here and still fail the checkpoint build.
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
- `NO_COLOR=1` and `UV_NO_PROGRESS=1` are always set. Output is additionally
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
