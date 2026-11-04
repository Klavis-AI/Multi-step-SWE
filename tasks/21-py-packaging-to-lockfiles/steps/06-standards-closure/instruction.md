# Step 6 — Nothing left of distutils, and a repository that describes itself

## 1. Implement this milestone

`/app` is one workspace, one lock, three distributions, wheels that are honest
about the interpreters they claim, and builds that produce the same bytes
twice. Two things are still unfinished, and they fail in opposite directions.

The first is that this repository's own code still imports `distutils`. Not the
packaging of it — the library. A subpackage of `bento` subclasses
`distutils.core.Distribution` and four of its commands; a compatibility module
borrows `DistributionMetadata` from the standard library to write PKG-INFO; a
configure step reads `INSTALL_SCHEMES` to work out where files go. `distutils`
was removed from the standard library in Python 3.12. Everything here runs on
3.11, so nothing is broken yet, and that is exactly the condition this milestone
ends.

The second is that nothing this workspace builds says what it is. The licence is
a free-text word. No distribution declares that it ships type information. The
lock is a file only one tool can read, and nothing in the tree enumerates what a
build of this workspace actually depends on.

**`distutils` is gone from the code that builds and describes *this*
distribution.** That is the line, and it is a line rather than a list of
exceptions: `distutils` may no longer be how the root distribution builds,
configures or describes itself. It may stay in the places where `bento` drives
`distutils` as a third-party tool, on behalf of somebody else's project that
chose it. Three modules the root ships are on the far side of that line, and
they are the only three:

* `src/bento/convert/core.py` — it runs a foreign `setup.py` under a
  monkey-patched `distutils` in order to read back what that project declared.
  The tool being driven is the point of the module. It may import
  `distutils.core`, `distutils.dist`, `distutils.command.build_py`,
  `distutils.command.sdist` and `distutils.filelist` — those two submodules of
  `distutils.command` and no other.
* `src/bento/commands/build_distutils.py` — it *is* the `Distutils` build
  backend, the one a `bento.info` selects by name when the project being built
  asks for it. The default backend is `yaku` and never reaches this file. It
  may import `distutils.dist`, `distutils.errors`, `distutils.extension` and
  `distutils.log`.
* `src/bento/commands/wininst_utils.py` — `distutils.msvccompiler`, and nothing
  else in that module. `get_build_version` reports which MSVC built the running
  interpreter; the standard library never grew a replacement for it. The rest
  of this module's `distutils` use does have one, so the rest is not covered.

Those three allowances are a ceiling and not a quota. Giving one up is fine —
if one of those modules stops importing something it was allowed to import,
that is still a repository with `distutils` gone from its own code. Reaching
for something not on its list is not fine, in those three files or in any
other.

`setuptools._distutils` is on nobody's list, the three above included. Pointing
the same imports at the copy `setuptools` vendors is not a port; it is the same
code reached by a different name, and it swaps a removed standard-library
module for a build-time dependency at runtime.

Everywhere else, the root distribution's own code is finished with `distutils`:
it does not import `distutils` or a submodule of it at the top of a file, inside
a function, inside a `try`, or under a name bound by `importlib`.

One tree goes with it. `bento`'s own `distutils` subpackage subclasses four
`distutils` commands and exists for nothing else, so there is nothing in it left
to port to: no file remains anywhere under it, its own `tests` directory
included. Every other module that imports `distutils` today — every one that is
not among the three named above — is a module with work of its own to do, and it
keeps its path. `bento/compat/dist.py` in particular is
a port and not a deletion — `src/bentomakerlib/tests/test_bentomaker.py` imports
`DistributionMetadata` from it, that file is one of the repository's own tests,
and you are not editing those — so the module stays where it is, keeps that
name importable from that path, and stops borrowing the class from the standard
library.

Borrowing it at one remove is the same borrowing. `convert/core.py` is allowed
`distutils.dist` above, and `DistributionMetadata` lives in `distutils.dist`
beside the `Distribution` that allowance is for; taking the class from there,
or from anywhere else `distutils` or `setuptools` defines it, and re-exporting
it under this distribution's own name leaves the PKG-INFO writer exactly where
it was. After the port, `DistributionMetadata` as an installed copy of this
distribution exposes it is a class this repository defines: it is not the
standard library's, it does not inherit from it, and the methods it holds were
written here rather than copied off the standard library's class onto one of
your own. What it renders does not change — the PKG-INFO that
`pkg_to_distutils_meta_pkg_info` produces has to come out byte for byte as it
does today.

What this does not cover, and what you may leave alone:

* **The other distributions this workspace publishes** — the ones the root's
  workspace member list names, each with its own `[project]` table. `yaku` is
  the one that matters: it leans on `distutils`'s compiler driver —
  `ccompiler.new_compiler`, `sysconfig.customize_compiler` — which is the one
  part of `distutils` the standard library never replaced. Porting it means
  writing a compiler abstraction, and that is a different project from this
  one. Leave it as it is.
* The example directories, and files under a `tests` directory.
* Any vendored third-party tree.
* The modules this repository carries that are Python 2 and do not parse on
  this interpreter at all — a file the interpreter cannot read is a file that
  imports nothing. There are a number of them, and porting them is not asked
  for here.

**The names that the rest of the code calls keep working under those names.**
`pkg_to_distutils`, `pkg_to_distutils_meta` and `pkg_to_distutils_meta_pkg_info`
are importable after this milestone, from the same module, under exactly those
spellings. Removing a dependency by renaming what depends on it is not removing
it.

**No `setup.py` remains anywhere in this repository.** Not in a distribution,
not in an example directory, not as a shim that calls something else.

**Each pure-Python distribution declares its licence as an expression, and
ships the file.** Pure-Python means what the wheel's own filename says: the
distributions whose wheel is tagged `py3-none-any`. For each of them the built
wheel's `.dist-info/METADATA` reports core metadata 2.4 or later, carries a
`License-Expression:` that is a valid SPDX expression, and no longer carries the
free-text `License:` field — the two are mutually exclusive. It carries at least
one `License-File:`, and the file each one names is inside that wheel, under the
`.dist-info/licenses/` directory where the specification puts it. The extension
distribution is out of scope here and is covered by the marker requirement
below.

**Every distribution's wheel says it is typed.** All three of them — the two
pure ones and the extension — ship a `py.typed` marker at the root of **every**
package their own wheel installs, not merely in the source tree. The marker
covers the subpackages underneath it, so what this asks for is one file per
installed package, not one per directory. One of these wheels installs two
packages, and a consumer importing the second learns nothing from a marker in
the first; likewise a marker that is in the repository and not in the artifact
tells an installed consumer nothing.

**The lock is exported in the interchange format.** This workspace carries a
`pylock.toml`: real TOML, declaring its `lock-version`, and current with respect
to `uv.lock` in both directions. Every package it names is a package `uv.lock`
locks, at the version `uv.lock` locks it at; and every package the root
distribution's runtime closure reaches in `uv.lock` — the root distribution
itself included — is named in it. It may name
more than that closure — which groups an export includes is the exporting tool's
business and not this milestone's — but it may not name something the lock does
not have, and it may not be missing something the root needs. A package the
export sources from a directory, a VCS checkout or an archive may carry no
version, because that is what identifies it; every other package carries one.
The format's own alternative filename, `pylock.<name>.toml`, is as good as the
plain one, and if the tree holds more than one of them then each one has to be
true of this workspace in all of the above — a stale export left beside a fresh
one is the same problem under a different extension.

**The repository carries a CycloneDX document that validates.** Somewhere in
this tree, checked in rather than written into a build output directory, there
is a JSON document declaring itself CycloneDX at specification version 1.5 or
later. It validates against the CycloneDX schema of the version it itself
declares. The components it lists agree with `uv.lock` by name and by version,
and between them they account for the root distribution's runtime closure — the
root itself may be the document's subject rather than one of its own components.
Where the file lives and what it is called are yours to choose; the requirement
is that the repository carries one, not that it sits at a particular path. If
more than one such document is in the tree, all of them have to be true of this
workspace — a stale one left beside a fresh one is a file that claims to
describe this repository and does not. What is in it must be true of this
workspace and not of the machine that happened to generate it.

**The artifacts stay publishable.** `twine check` over everything a build of
this workspace produces exits 0.

**Every project table is a valid one.** This milestone edits all three
`pyproject.toml` files, and each of them — the repository root's and every
workspace member's — conforms to the pyproject specification afterwards, as the
root's already does.

**Behaviour is unchanged.** Everything the `bento` library and the `bentomaker`
command line did before, they still do, identically — installed from the built
wheels into an empty virtual environment, which resolves the requirements those
wheels declare rather than reading the lock: the same return values, the same
objects, the same messages on standard output and standard error, the same exit
statuses. This includes text this repository *generates* rather than
stores — the PKG-INFO a conversion writes, field order and blank lines included;
the help a command prints for each of its subcommands; and the installation
paths a configure step computes for a given prefix. Code that produced those by
asking `distutils` has to produce the same bytes without asking it. `hello` keeps
the interface it has — `hello.hello()` prints its line and returns nothing,
`hello.foo` is `hello.bar.foo`, and `hello.hello` is the function the extension
defines and accepts the arguments it accepts today.

**Still true.** Everything the workspace already has holds when this milestone
is finished. That is a single requirement, not a list of separate ones: the
tree you hand back satisfies all of it at once, and the items below are named
only so that you do not have to rediscover them. The lock at the repository
root is still current with respect to the `pyproject.toml` files it was produced
from — this milestone edits one of them — still complete, and still resolved for
the CPython 3.11 this workspace runs; every `[build-system] requires` in the
tree still pins every requirement it lists to one exact version, so a build
requirement you add is a build requirement you pin; the back end named in each
built wheel's `.dist-info/WHEEL` `Generator:` line still reports itself at the
version its own distribution's `requires` pins, which a `Generator:` line with
no version, such as meson-python's `Generator: meson`, does not; builds of every
distribution still produce the same bytes twice and still carry the epoch they
are given; the
`src/` layout, the single-sourced version, stated outright in the file that
declares the root distribution's metadata, the console script and its entry
point; the `hello` extension still built by scikit-build-core or meson-python;
the sdists that rebuild into the same wheel; the vendored third-party
trees stay gone and none of the three build-escape switch names —
`--no-build-isolation`, `--skip-dependency-check`, and the `distutils` hack that
`setuptools` installs — comes back into the tree's code; no `*requirements*.txt`
at the repository root, which is also where an export of the lock in that older
format must not land; and the `hello` distribution still produces one wheel for
the interpreters that have a global interpreter lock and one for the
free-threaded interpreter, with the extension still declaring that it does not
need the lock, and `bento` and `yaku` still carrying no compiled code and saying
so in their tags.

**Not part of this milestone.** Stated so you do not have to guess:

- Which build backend the two pure distributions use, which of the two
  backends made for compiled code — scikit-build-core
  (`scikit_build_core.build`) or meson-python (`mesonpy`) — the `hello`
  extension stays on, and which tool writes the SBOM or the exported lock. Any
  of them, as long as what lands in the tree satisfies the requirements above.
- Whether the type information is *correct*. `py.typed` is a marker with a
  defined meaning; the annotations themselves are not part of this milestone,
  and you are not asked to add any.
- Windows-only code paths, which this image cannot run either way — they are
  code this repository ships, so the first requirement above covers them, and
  nothing else here does.
- Anything in the tree this milestone does not name, including the other example
  directories, which are Bento's own format and not distributions.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/06-standards-and-metadata.md` lists the
specifications this milestone is defined by — the licence expression and
licence-file fields, the typing marker, the exported lock format, core metadata
2.4, and the CycloneDX document — with the canonical URL of each. It also
records what this repository already contains and how it is driven: which
modules import `distutils` today and what each one uses it for, which of those
imports has observable output and which does not, how to read a field out of a
built wheel's metadata, and where the CycloneDX schemas live on this machine.

**Additional files:** `/app/uv.lock` is the lock that stays current.
`/app/pyproject.toml` and the `pyproject.toml` of each of the two workspace
members, `yaku` and `hello`, are the three project tables. Append what you changed to `/app/WORKSPACE_HANDOFF.md`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /opt/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`; preserve inherited source, manifests,
  documents, and handoff files.
- Do not cheat by using online solutions or hints specific to this task.
- Leave a reproducible handoff.
