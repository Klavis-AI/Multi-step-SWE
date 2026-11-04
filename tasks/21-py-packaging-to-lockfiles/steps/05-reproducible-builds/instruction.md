# Step 5 — Builds that produce the same bytes twice

## 1. Implement this milestone

`/app` is one workspace with one lock, three distributions, and wheels that are
honest about the interpreters they claim. What none of them is yet is
*reproducible*: build the same distribution from the same sources twice and you
get two different files, and the difference is not in the code — it is the
moment the build ran, written into the archive by the tool that made it.

That matters here for the same reason the lock does. A lock says which inputs a
build resolved; it is worth what it is worth only if those inputs produce the
same output every time. Right now they do not, and the workspace has no way to
notice.

**Two builds, the same bytes.** Build a distribution of this workspace, then
build the same distribution again from a second copy of the tree in a different
directory, with the same build front end and the same epoch. The wheel is
byte-for-byte the same file both times, and so is the archive inside the sdist's
gzip wrapper, for every distribution in the workspace. Not "the same contents" —
the same bytes, the same `sha256sum`.

**The timestamps in an artifact are the ones the build was given.** When a
build of this workspace is given a `SOURCE_DATE_EPOCH`, the sdist and the wheel
each distribution produces on the interpreter this workspace runs carry that
instant and nothing else: every member of the sdist tar, every entry of the
wheel zip. Hand the same tree a different epoch and the artifacts carry that
one instead. A timestamp that is stable because the tool writes a constant of
its own is not the same thing and does not satisfy this: the requirement is
that the specification is read, not that the clock is avoided.

**Every build requirement names an exact version.** Each `[build-system]
requires` in the tree pins every requirement it lists to one version, not to a
floor and not to nothing. A build environment resolved from the index at build
time is a build environment that changes underneath the tree, which is the same
class of problem the lock already solved for the runtime dependencies.

**And the version that is pinned is the version that runs.** The back end named
in a built wheel's `.dist-info/WHEEL` `Generator:` line reports itself at the
version its own distribution's `requires` pins. A pin that names one version
while the build quietly uses another has changed nothing; this is the half of
the requirement above that shows in an artifact rather than in a file. A
`Generator:` line that carries no version shows no version, so it does not meet
this: meson-python writes `Generator: meson` and nothing more.

**Behaviour is unchanged.** Everything the `bento` library and the `bentomaker`
command line did before, they still do, identically — installed from the built
wheels into an empty virtual environment, which resolves the requirements those
wheels declare rather than reading the lock: the same return values, the same
objects, the same messages on standard output and standard error, the same exit
statuses. `hello` keeps the interface it has — `hello.hello()` prints
its line and returns nothing, `hello.foo` is `hello.bar.foo`, and `hello.hello`
is the function the extension defines and accepts the arguments it accepts
today.

**Still true.** Everything the workspace already has holds when this milestone
is finished, and these are the parts this milestone's work most easily
disturbs: the lock at the repository root is still current with respect to the
`pyproject.toml` files it was produced from, still complete, and still resolved
for the CPython 3.11 this workspace runs; the `src/` layout, the single-sourced
version, stated outright in the file that declares the root distribution's
metadata, the console script, the console script's entry point, and the sdists
the repository root and the `hello` distribution produce, which still rebuild
into the same wheel; the `hello` extension still built by scikit-build-core or
meson-python; the vendored third-party trees stay gone and nothing in
the tree's code turns build isolation off, turns build-dependency checking off,
or re-enables setuptools' legacy distutils shim; `dev_requirements.txt`,
`dev_requirements26.txt` and `doc_requirements.txt` are still gone from the
tree and nothing installs from them; the `hello` distribution still produces
one wheel for the interpreters that have a global interpreter lock and one for
the free-threaded interpreter, the extension still declares that it does not
need the lock, and `bento` and `yaku` still carry no compiled code and say so
in their tags; and neither the repository root nor the `hello` distribution has
a `setup.py`.

**Not part of this milestone.** Stated so you do not have to guess:

- Which build backend the two pure distributions use, and which of the two
  backends made for compiled code — scikit-build-core
  (`scikit_build_core.build`) or meson-python (`mesonpy`) — the `hello`
  extension is built by; it stays on one of those two, and its wheels meet the
  `Generator:` requirement above whichever it is. Change them or keep
  them; the requirements above are about the artifacts and the declarations,
  not about who produced them. If you change one, the things listed under *Still true*
  are still true afterwards — that is the cost of changing it, not a reason not
  to.
- The gzip wrapper's own header. `.tar.gz` is the format the tools produce and
  the requirement above is about the artifacts they produce; if your front end
  writes a gzip header timestamp you do not control, that is not a failure of
  this milestone.
- Anything in the tree this milestone does not name, including the other
  example directories, which are Bento's own format and not distributions.

## 2. Read these local references

**Normative semantics:** `/app/DOCS/05-reproducible-builds.md` lists the
specifications this milestone is defined by — `SOURCE_DATE_EPOCH`, what a
reproducible build is, `[build-system]` and build isolation, version
specifiers, and the sdist and wheel formats — with the canonical URL of each.
It also records what this repository already contains and how it is driven:
which distributions are here, what each build produces, how to read a
timestamp out of a tar and out of a zip, how to find out what actually built an
artifact, and what an ELF can carry from the machine it was compiled on.

**Additional files:** `/app/uv.lock` is the lock that stays current.
`/app/pyproject.toml` and the `pyproject.toml` of each of the two workspace
members, `yaku` and `hello`, are the three `[build-system]` tables. Append what you changed to `/app/WORKSPACE_HANDOFF.md`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /opt/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`; preserve inherited source, manifests,
  documents, and handoff files.
- Do not cheat by using online solutions or hints specific to this task.
- Leave a reproducible handoff.
