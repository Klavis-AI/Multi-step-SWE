# Step 4 — Wheels that are honest about who can install them

## 1. Implement this milestone

`/app` is one workspace with one lock, and every distribution in it builds
through PEP 517. One of them contains compiled code: the `hello` example, whose
`_bar` extension is a C file that has been built the same way since it was
written — against whatever CPython happened to be running, for that CPython and
no other.

That has two consequences the tree does not currently admit to. A wheel built
for 3.11 is tagged for 3.11 and will not install anywhere else, so the
distribution needs one wheel per interpreter it supports even though the code
inside them is identical. And there is now an interpreter it cannot support at
all by accident: CPython 3.13's free-threaded build, where the global
interpreter lock is off and an extension that has not said it is ready will
turn it back on for the whole process.

Two artifacts come out of this milestone, and both have to be true rather than
merely labelled.

**One wheel for the interpreters that have a GIL.** Building the `hello`
distribution on CPython 3.11 produces a single wheel that installs, imports and
runs on 3.11, on 3.12 and on 3.13 — one file, three interpreters, and the
extension inside it built against the subset of the C API that CPython promises
not to break. One built object inside the wheel, not one per interpreter: a
single file that serves three of them carries a single compiled extension, and
that is the saving the subset buys. The wheel's ABI tag says so; an installer
on 3.12 reads that tag and accepts the wheel, which is the whole point, and the
tag is a claim the wheel has to be able to keep. The version floor the
distribution declares and the floor its tag implies are the same number.

**And one for the interpreter that has not.** Building the same distribution on
the free-threaded CPython 3.13 this environment provides produces a second wheel,
tagged for that interpreter, which installs and runs there. It is a different
build, not the first one relabelled: the stable ABI does not exist in a
free-threaded CPython and the C API's own headers refuse to compile against it,
so whatever makes the first wheel possible has to stand down for the second.
Both wheels come out of the one source file and the one declaration; a second
copy of the extension maintained in parallel is not an answer.

**The extension is ready for a free-threaded interpreter, and says so.** An
extension module that does not declare its position is
assumed to need the lock, and importing it re-enables the GIL — silently, as far
as anything you can see from the outside, and with every free-threading property
you might check from the artifact still true. After this milestone, importing
`hello._bar` on the free-threaded interpreter leaves that interpreter
free-threaded. The module keeps no per-module state today, so the work is in how
it declares itself to the interpreter, not in locking anything.

**The pure distributions stay pure.** `bento` and `yaku` contain no compiled
code and their wheels say so. Nothing here asks them to acquire an interpreter
or a platform in their tags, and a wheel that claims one it does not need is as
wrong as a wheel that omits one it does.

**Behaviour is unchanged.** Everything the `bento` library and the `bentomaker`
command line did before, they still do, identically — installed from the built
wheels into an empty virtual environment, which resolves the requirements those
wheels declare rather than reading the lock: the same return values, the same
objects, the same messages on standard output and standard error, the same exit
statuses. `hello` keeps the interface it has — `hello.hello()` prints its
line and returns nothing, `hello.foo` is `hello.bar.foo`, and `hello.hello` is
the function the extension defines and accepts the arguments it accepts today.

**Still true.** Everything the workspace already has holds when this milestone
is finished, and these are the parts this milestone's work most easily
disturbs: the lock at the repository root is still current with respect to the
`pyproject.toml` files it was produced from, still complete, and still resolved
for the CPython 3.11 this workspace runs; the vendored third-party trees stay
gone and nothing in the tree's code turns build isolation off, turns
build-dependency checking off, or re-enables setuptools' legacy distutils shim;
`dev_requirements.txt`, `dev_requirements26.txt` and `doc_requirements.txt` are
still gone from the tree and nothing installs from them; the `src/` layout, the
single-sourced version, stated outright in the file that declares the root
distribution's metadata, the console script; the `hello` extension built by
scikit-build-core or meson-python; each distribution still builds at
its own directory as well as through the workspace, and the sdists the
repository root and the `hello` distribution produce still rebuild into the
same wheel; and neither the repository root nor the `hello` distribution has a
`setup.py`, including as a way to make a build decision that a declaration
could not express.

**Not part of this milestone.** Stated so you do not have to guess:

- Which build backend the two pure distributions use, and which of the two
  backends made for compiled code the `hello` extension is built by. Change
  them or keep them; the requirements above are about the wheels and the
  extension, not about who produced them. What does not change is that the
  extension stays on one of those two — scikit-build-core
  (`scikit_build_core.build`) or meson-python (`mesonpy`).
- The lowest CPython the `hello` distribution supports, so long as the number it
  declares and the number its ABI tag implies are the same one.
- Whether the wheels carry a `manylinux` platform tag. Auditing and retagging an
  ELF for redistribution is a release-time act and is not asked for here.
- What the extension's C looks like beyond the two properties above — it prints
  what it prints and it leaves the GIL off. The one thing past those two that
  is not free is that the declaration saying so has to be one the interpreter
  will accept when it imports the module; beyond that, the file is yours.
- Anything in the tree this milestone does not name, including the other example
  directories, which are Bento's own format and not distributions.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/04-wheels-and-interpreters.md` lists the
specifications this milestone is defined by — the platform compatibility tags,
the stable ABI and limited API, multi-phase extension module initialisation, and
free-threaded CPython — with the canonical URL of each. It also records what
this repository already contains and how it is driven: where the extension's
source is, what the `hello` package re-exports from it, which interpreters this
environment provides, how a distribution of this workspace is built and for
which interpreter, and how to read a wheel once you have one.

**Additional files:** the `hello` workspace member — the distribution that
declares `[project] name = "hello"`, which started out as
`examples/simples/single_extension/` — is the distribution this milestone is
about. `/app/uv.lock` is the lock that stays
current. Append what you changed to `/app/WORKSPACE_HANDOFF.md`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /opt/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`; preserve inherited source, manifests,
  documents, and handoff files.
- Do not cheat by using online solutions or hints specific to this task.
- Leave a reproducible handoff.
