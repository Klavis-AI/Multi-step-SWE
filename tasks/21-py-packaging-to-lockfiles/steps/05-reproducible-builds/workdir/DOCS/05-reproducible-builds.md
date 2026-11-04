# Reproducible builds and pinned build environments

The normative references for this milestone, and what this repository already
says about itself. Nothing here is a recommendation about how to do the work.

## Specifications

**SOURCE_DATE_EPOCH** — the cross-ecosystem convention for the timestamp a
build must use in place of the wall clock: what the variable holds, what a build
system is required to do with it, and what "clamp" means for a file whose own
timestamp is already older.
<https://reproducible-builds.org/specs/source-date-epoch/>

**Reproducible builds** — what the term means, why a build that embeds the
moment it ran is not one, and the usual sources of difference in an archive:
timestamps, member order, file modes, uid/gid, and paths from the build
machine.
<https://reproducible-builds.org/docs/>

**PEP 518 — Specifying minimum build system requirements for Python projects**
— the `[build-system]` table, what `requires` is for, and the isolated
environment a front end creates from it.
<https://peps.python.org/pep-0518/>

**PEP 517 — A build-system independent format for source trees** — the hooks a
back end exposes, and build isolation: which environment the back end and its
own dependencies are resolved into, and when.
<https://peps.python.org/pep-0517/>

**Version specifiers** — the grammar `[build-system] requires` is written in,
including what `==` means, what `===` means, and how a specifier without an
upper bound resolves.
<https://packaging.python.org/en/latest/specifications/version-specifiers/>

**Source distribution format** — what an sdist is required to contain, the name
and layout of the single top-level directory inside it, and `PKG-INFO`.
<https://packaging.python.org/en/latest/specifications/source-distribution-format/>

**Binary distribution format** — the wheel: `RECORD`, and the `.dist-info/WHEEL`
file, whose `Generator:` line names the tool and version that produced the
archive.
<https://packaging.python.org/en/latest/specifications/binary-distribution-format/>

**setuptools: reproducible builds** — what setuptools itself does with
`SOURCE_DATE_EPOCH`, and which parts of an archive it applies it to.
<https://setuptools.pypa.io/en/latest/userguide/miscellaneous.html>

**Hatchling build configuration** — the `[tool.hatch.build]` tables: which files
a build includes, where the packages are, and how `reproducible` is controlled.
<https://hatch.pypa.io/latest/config/build/>

**uv: build isolation and the build backend** — how `uv build` resolves
`[build-system] requires`, where that resolution comes from, and `--no-build-isolation`.
<https://docs.astral.sh/uv/concepts/projects/build/>
<https://docs.astral.sh/uv/reference/cli/#uv-build>

## What this repository already contains

**Three distributions.** The repository root (`bento`) and the two workspace
members, `yaku` and `hello` (the example that started out as
`examples/simples/single_extension/`). Each has its own
`pyproject.toml` with its own `[build-system]` table, and the three tables do
not currently name the same back end as each other.

**What each build produces.** `uv build` and `python -m build` both write a
`.tar.gz` sdist and a `.whl` wheel. A wheel is a zip and a member's timestamp is
the zip entry's `date_time` field; an sdist is a gzipped tar and a member's
timestamp is the tar header's `mtime`. `python -m zipfile -l <wheel>` and
`python -m tarfile -l <sdist>` list members; `tar -tvf <sdist>` prints the
mtimes; `sha256sum` compares two archives as bytes.

**The gzip wrapper.** A `.tar.gz` carries a timestamp of its own in the gzip
header, ahead of the tar it wraps, which is why two identical tars can still
produce two different `.tar.gz` files.

**Reading what built an artifact.** The `Generator:` line in a wheel's
`.dist-info/WHEEL` names the back end and its version as the back end reported
it at build time. That is the version that actually ran, which is not
necessarily the version installed in this image: under build isolation the
front end resolves `[build-system] requires` into a fresh environment, from the
index, every time. Not every back end writes a version there: hatchling,
pdm-backend and scikit-build-core do, while meson-python writes
`Generator: meson` and nothing more.

**The extension.** `hello` compiles C. An ELF
object can embed the directory it was compiled in, the build id its linker
assigned, and the name of its build system; `readelf -n <so>` prints the notes,
`strings <so> | grep /` looks for embedded paths, and `file <so>` says whether
it is stripped.

**The workspace around it.** `uv.lock` is at the repository root and is the
workspace's one resolution of its runtime and development requirements — what
`uv sync` installs; a build resolves `[build-system] requires` on its own, as
above. `uv lock --check` reports
whether it is still current with respect to the `pyproject.toml` files it was
produced from. `uv build --all-packages` builds every member of the workspace
in one invocation; `--package <name>` selects one by the name its `[project]`
table declares.
