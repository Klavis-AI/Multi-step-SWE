# Step 3 — One workspace, one lock, and no vendored copies

## 1. Implement this milestone

`/app` is now two distributions that build through PEP 517 and declare
themselves in `pyproject.toml`. What it does not have is any statement of what
it depends on. Four trees are carried inside the source, three of them
third-party code, put on `sys.path` by hand at import time; the development and
documentation requirements are three `*requirements*.txt` files from 2008; and
nothing anywhere records which versions a working install was ever made of.

Four things change. Nothing about what Bento does changes.

**The dependencies become dependencies.** The copies under `bento/private/` of
code this project did not write are removed — the `ply`, `simplejson` and `six`
trees — and the root distribution declares them as what they are: requirements,
resolved and installed like any other. The loop in `bento/__init__.py` that
inserts directories onto `sys.path` goes with them, and so does the environment
switch it reads: when this milestone is finished that module does not touch
`sys.path` at all, and neither the name `BENTO_UNBUNDLE` nor the name
`USE_PRIVATE_MODULES` is left anywhere in the tree's *code* — prose is not code,
and a document recording what you removed is fine and is expected. A document
here means a Markdown, reStructuredText or plain-text file (`.md`, `.rst`,
`.txt`), and inside any other file a comment or a Python docstring is prose as
well: a `#` line beside the change you made, or a docstring saying what a module
no longer does, records the work rather than doing it. Everything else in the
tree is read as code. Those two
names are the whole of the ban. Everywhere else this repository touches
`sys.path` it is looking for something that is not a dependency — a waf tool, a
`bscript`, a test fixture — and none of that is part of this milestone. Code this
project *did* write and happens to keep in the same directory stays and keeps
working — `bento.private.bytecode` and `bento.private.version` are Bento's own,
their importers are unchanged, and they still do what they did.

Which version of each of those distributions you require is your decision and
is part of the work. A requirement admits a range; a resolution picks a point in
it. The behaviour this repository has today was produced by the copies it
carries today, and the point you pick has to reproduce it. The point that
matters is the one an installer picks from what the built wheels declare:
installed from those wheels into an empty virtual environment, which resolves
their declared requirements from the index rather than from the lock, the
distribution behaves exactly as it does today. A range wide enough to admit a
version that changes that behaviour has to be narrowed where it is declared;
pinning the right version in the lock alone does not reach that install.

**`yaku` becomes a member of the workspace.** `bento/private/_yaku` is not a
third-party tree: it is Bento's own build system, developed here and published
nowhere, and `bentomakerlib` imports it as `yaku`. It leaves `bento/private/`
and becomes a distribution of its own inside this repository, declared in its
own `pyproject.toml`, named `yaku`. The root distribution depends on it and
resolves that dependency from this repository rather than from any index. It is
still the build system: the modules that make up the `yaku` package move with
it and arrive intact, and a distribution that claims the name while carrying
placeholders where those modules were has deleted Bento's build system rather
than packaged it. Where the directory lives is your call — it is found by the
name it declares, not by its path — but nothing named for it stays anywhere
under `bento/private/`, whichever of its two spellings you keep.

**The workspace and the lock.** `/app` becomes a single uv workspace whose
members are the distributions this repository contains — `yaku` and the `hello`
example extension, which is already a distribution of its own — locked together
with the root distribution
into one `uv.lock` at the repository root. That lockfile is the record of a
resolution: it is current with respect to the `pyproject.toml` files it was
produced from, it names an exact version and a hash for every distribution that
comes from an index, and it is complete enough that an environment can be built
from it — on the interpreter this workspace runs, CPython 3.11 — without
resolving anything again. A resolution is made for an interpreter, and the tool
that writes one will pick an interpreter for you if you do not say which; a lock
resolved for something this workspace does not run is a record of a different
project's environment.

**The requirements files become dependency groups.** `dev_requirements.txt`,
`dev_requirements26.txt` and `doc_requirements.txt` are replaced by
`[dependency-groups]` in the root `pyproject.toml` and are deleted, and nothing
left in the tree installs from them any more — `tox.ini` and `.travis.yml`
included. The same reading of prose applies here: a comment quoting the `-r`
line a group replaced is not installing anything. There is a
`dev` group and a `doc` group; the group that extended another one still extends
it, through the group mechanism rather than by repeating its contents. The
`doc` group provides Sphinx. Every group you declare installs on this
interpreter from the lock: a group naming something that cannot be installed
here is not a translation of anything, and what a group contains beyond that is
your call.

**Behaviour is unchanged.** Everything the `bento` library and the `bentomaker`
command line did before, they still do, identically: the same return values,
the same objects, the same messages on standard output and standard error, the
same exit statuses — including the cases where the tree raises or fails. The
distributions that replace the vendored copies are not required to be the same
version as the copies they replace; they *are* required to leave every one of
those behaviours exactly as it is. This is the part of the milestone that the
rest of it is in service of.

**Still true.** Both distributions keep everything they already have — the
`pyproject.toml` declarations, the isolated builds, the metadata, the packages
and data files the wheels ship, the sdists that rebuild, the `src/` layout with
nothing importable at the repository root, the single-sourced version — stated
outright in the file that declares the root distribution's metadata — that the
installed package reports, the `hello` extension that builds and runs and is
still built by one of the two backends made for compiled code (scikit-build-core,
`scikit_build_core.build`, or meson-python, `mesonpy`), no
`setup.py` at the repository root or in the `hello` distribution, nothing
importing `bento.distutils` outside that package's own modules, and no
build-escape switch anywhere. All of that still holds when this milestone is
finished.

**Not part of this milestone.** Stated so you do not have to guess:

- Which versions you require, and how tightly. Pin, cap or range as you see fit;
  the requirement is that what an installer resolves from them reproduces
  today's behaviour.
- Where the `yaku` directory goes, and what the distribution's version number
  is. It is found by the name it declares, not by its path.
- What goes in the `dev` group beyond the requirement that it installs here, and
  whether you declare groups beyond `dev` and `doc`.
- `bento.info`'s own `Flag:` sections and the `Library:` entries that name the
  vendored trees. Whether they follow this change is your call; keep the file a
  truthful description of what the distribution ships.
- Anything in the tree this milestone does not name. Nothing here asks you to
  change it — which is not the same as nothing reading it: the behaviour this
  milestone preserves runs through it, and the two name bans above are
  whole-tree.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/03-locking-and-workspaces.md` lists the
specifications this milestone is defined by — dependency groups, dependency and
version specifiers, editable installs — with the canonical URL of each, plus
uv's own documentation for locking, workspaces, sources and groups. It also
records what this repository already says about itself: where the `sys.path`
insertion is, which modules under `bento/private/` are Bento's own, and which
part of the tree is most sensitive to a dependency's version.

**Additional files:** `/app/src/bento/_config.py` holds the switch the insertion
loop reads. `/app/dev_requirements.txt`, `/app/dev_requirements26.txt`,
`/app/doc_requirements.txt` are what the groups replace; `/app/tox.ini` and
`/app/.travis.yml` are what installs from them.
`/app/bento.info` remains the authoritative statement of the root
distribution's identity. Append what you changed to
`/app/WORKSPACE_HANDOFF.md`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /opt/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`; preserve inherited source, manifests,
  documents, and handoff files.
- Do not cheat by using online solutions or hints specific to this task.
- Leave a reproducible handoff.
