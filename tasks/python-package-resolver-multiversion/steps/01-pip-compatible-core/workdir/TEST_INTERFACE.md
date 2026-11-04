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
- The toolchain is the Rust release this step's release declares: **Rust 1.75**,
  the channel that release's own `rust-toolchain.toml` names. It is never older
  than the channel the inherited `/app/rust-toolchain.toml` names, and it
  compiles every version the inherited lockfile pins, so the tree builds whether
  it is left as this step finds it or carried to this step's release.
  `rust-toolchain.toml` in `/app` is not consulted at checkpoint time: pinning a
  different channel there changes nothing, and code that needs a channel
  *newer* than 1.75 can build here and still fail the checkpoint build.
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
one. Managed-interpreter directories (`UV_PYTHON_INSTALL_DIR`) are scratch, and
start empty except where a step's own section says otherwise. Where a step has
observations that instead start from a directory already holding one of the
builds listed above — so that what `uv python install` does after the download
policy can be read — that step's section is where it is written down.

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
`*.dist-info` directory names, the text of the `WHEEL` file within a named one,
and the names of the files directly inside an installed package's own
directory; and, where a run named a cache directory with `--cache-dir` or
`UV_CACHE_DIR`, whether that directory exists afterwards and whether anything
is in it — no name inside a cache is read. `uv pip freeze` is also run after an
observation to read back the environment it left; its lines are sorted before
comparison, so their order is not part of any requirement, and neither is the
order of a directory listing.

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
environment afterwards. Standard error is read for emptiness rather than for
wording: one observation requires that a successful run given `--quiet` writes
nothing to it. The effect of `--python-platform` on wheel tag matching is not
compared by any observation of this milestone, and no observation passes the
option: `PROFILE.md` § 1 documents it because a later milestone refines what
its values denote, and that milestone's section is where it becomes
observable. Nor does any observation of this milestone read a console script,
an `entry_points.txt` or a `direct_url.json` — the generated distributions
declare no entry points, and inside an installed `.dist-info` nothing but the
`WHEEL` file is read. No observation invokes `install`, `sync`, `uninstall` or `compile` with an
empty argument list, so the four "requires at least one source" rejections
`PROFILE.md` § 1 states are not exercised by this milestone's observations.
