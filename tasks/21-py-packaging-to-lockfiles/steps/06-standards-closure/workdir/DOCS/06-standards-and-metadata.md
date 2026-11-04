# Standards closure: what a distribution declares, and life after distutils

The normative references for this milestone, and what this repository already
says about itself. Nothing here is a recommendation about how to do the work.

## Specifications

**PEP 632 — Deprecate distutils module** — the removal: which Python version
dropped `distutils` from the standard library, what `setuptools` provides in
its place and under what name, and the migration advice for each of the
submodules this repository uses.
<https://peps.python.org/pep-0632/>
<https://peps.python.org/pep-0632/#migration-advice>

**PEP 639 — Improving license clarity with better package metadata** — the
`license` field as an SPDX expression, the `license-files` field, the
`License-Expression:` and `License-File:` core metadata fields, what happens to
the old free-text `License:` field, and where the named files are placed inside
a built wheel.
<https://peps.python.org/pep-0639/>

**Core metadata specification** — every field a `PKG-INFO` or a
`.dist-info/METADATA` may carry, the `Metadata-Version` each field was
introduced at, and which fields are mutually exclusive.
<https://packaging.python.org/en/latest/specifications/core-metadata/>

**SPDX license expressions** — the grammar an expression is written in:
identifiers, `AND`, `OR`, `WITH`, and the canonical spelling of an identifier.
<https://spdx.github.io/spdx-spec/v2.3/SPDX-license-expressions/>
<https://spdx.org/licenses/>

**PEP 561 — Distributing and packaging type information** — the `py.typed`
marker: what it is, which directory it goes in, and what a consumer is entitled
to conclude from its presence in an *installed* package.
<https://peps.python.org/pep-0561/>
<https://typing.python.org/en/latest/spec/distributing.html>

**PEP 751 — A file format to record Python dependencies for installation
reproducibility** — the `pylock.toml` format: the filename the specification
fixes, the `lock-version` field, the `[[packages]]` array and what each entry
carries.
<https://peps.python.org/pep-0751/>
<https://packaging.python.org/en/latest/specifications/pylock-toml/>

**CycloneDX** — the software bill of materials: the JSON document, its
`bomFormat` and `specVersion` fields, and the `components` array.
<https://cyclonedx.org/docs/1.5/json/>
<https://cyclonedx.org/specification/overview/>

**Binary distribution format** — the wheel: what `.dist-info` contains, and how
a file in the source tree comes to be inside an installed package directory.
<https://packaging.python.org/en/latest/specifications/binary-distribution-format/>

**uv: exporting a lock** — `uv export` and the formats it writes, including
`pylock.toml` and CycloneDX, and which packages an export of a workspace
contains.
<https://docs.astral.sh/uv/reference/cli/#uv-export>

**cyclonedx-python-lib and cyclonedx-py** — the library that can validate a
CycloneDX document, and the generator that can write one from an environment or
a lock.
<https://cyclonedx-python-library.readthedocs.io/>
<https://cyclonedx-bom-tool.readthedocs.io/>

**twine check** — what it reads out of a built artifact and what it rejects.
<https://twine.readthedocs.io/en/stable/#twine-check>

## What this repository already contains

**Where `distutils` is imported.** The root distribution imports it at
forty-one sites in thirteen of its own modules, in three different ways — at
module scope, inside a function, and inside a `try`. A grep for a line starting with `import`
finds neither the indented ones nor the two that are spelled across a
backslash continuation; reading each module's syntax tree finds all of them.
A handful of the modules this repository still carries are Python 2 and raise
`SyntaxError` on this interpreter, so their imports are never reached.
Grouped by what the import is *for*:

| module | what it takes from `distutils` |
|---|---|
| `bento/distutils/` (`dist.py`, `monkey_patch.py`, `commands/{build,config,install,sdist}.py`) | subclasses `distutils.dist.Distribution` and four command classes; this subpackage exists only to let a `setup.py` drive Bento |
| `bento/compat/dist.py` | `distutils.dist.DistributionMetadata`, subclassed; `distutils.util.rfc822_escape`; `distutils.versionpredicate` |
| `bento/convert/core.py`, `bento/commands/build_distutils.py` | `distutils.core.setup`, `Distribution`, `FileList`, `build_py`, `sdist`, `Extension`, `log`, `errors` — the implementation of the `convert` command and of building through distutils |
| `bento/commands/configure.py` | `distutils.command.install.INSTALL_SCHEMES` — the dictionary of installation path schemes |
| `bento/commands/script_utils.py` | `distutils.command.build_scripts.first_line_re` — one compiled regular expression |
| `bento/backends/waf_tools/interface_gen.py` | `distutils.dir_util.mkpath` — but this module is Python 2 and raises `SyntaxError` on this interpreter, so an AST walk never reaches its imports |
| `bento/commands/wininst_utils.py`, `bento/commands/msi_utils.py` | `distutils.util.get_platform` and `distutils.sysconfig.get_python_version` in the first, `distutils.sysconfig` in the second — the standard library's own `sysconfig` answers all three. Separately, `wininst_utils` calls `distutils.msvccompiler.get_build_version`, which reports the MSVC that built the running interpreter and is the one name in this table with no standard-library successor at all. Both modules are Windows-only paths, and `msi_utils` also imports a module that does not exist on this platform |

**What the other workspace members do with it.** `yaku` imports `distutils` at
thirteen sites in two modules — `yaku.sysconfig` and `yaku.tools.pyext`, the
`sysconfig.py` and `tools/pyext.py` of the `yaku` package wherever that
distribution sits. What those sites reach for is the compiler
driver: `ccompiler.new_compiler` and `ccompiler.get_default_compiler`,
`unixccompiler`, `command.build_ext`, `dist`, and
`sysconfig.customize_compiler`, with `distutils.sysconfig.get_python_inc()` and
`get_config_var(...)` around them. The second half of that list has successors —
`sysconfig` in the standard library answers `get_python_inc` and
`get_config_var` — but the driver does not. `new_compiler` and
`customize_compiler` are the one corner of `distutils` that PEP 632 hands to
`setuptools._distutils` rather than to a standard-library replacement, and
porting them means writing a compiler abstraction rather than changing an import
line. `hello` imports `distutils` nowhere. This is the reason the milestone is
scoped to the root distribution.

**Which of those have output you can see.** Three do, and they are not the three
the file sizes suggest.

* `configure.py`'s `INSTALL_SCHEMES` decides the directory `bentomaker
  configure` computes for every installation path, for a given `--prefix`. The
  values are visible in `bentomaker configure`'s output and in the `.info` files
  it writes, and they are the input every later command canonicalises against.
* `compat/dist.py` is what writes `PKG-INFO`: field order, the `Metadata-Version`
  it declares, and the RFC-822 escaping of a multi-line description are all
  `DistributionMetadata.write_pkg_file`'s, not this repository's. Two things are
  worth knowing before you open it. The class it binds on this interpreter is
  `distutils.dist.DistributionMetadata` — the `_DistributionMetadata` copy above
  the binding is a Python 2 fallback the `sys.version_info[0] < 3` test has not
  selected since the file was written, and it does not run as it stands
  (`write_pkg_info` calls `os.path.join` in a module that never imports `os`).
  And `src/bentomakerlib/tests/test_bentomaker.py` imports `DistributionMetadata`
  from this module by name, at module level.
* `convert/core.py` and `build_distutils.py` implement the `convert` command,
  whose `--help` text and error messages are part of the command line's surface.

The remaining four import things that are pure — a compiled regular expression,
a `mkpath`, and two Windows entry points this image cannot execute.

**Reading metadata out of a built artifact.** A wheel is a zip; its
`<name>-<version>.dist-info/METADATA` is an RFC-822 message.
`python -m zipfile -l <wheel>` lists the members and `python -m zipfile -e`
extracts them; `email.parser.BytesParser().parsebytes(...)` parses the message,
and `msg.get_all("License-File")` returns every occurrence of a repeated field.
The same file inside an sdist is called `PKG-INFO`.

**Checking an SPDX expression.** `packaging.licenses.canonicalize_license_expression`
is installed here; it returns the canonical spelling of a valid expression and
raises `InvalidLicenseExpression` for anything else.

**Validating a CycloneDX document offline.** `cyclonedx-python-lib` ships the
JSON schemas inside the installed package, at
`cyclonedx/schema/_res/bom-<version>.SNAPSHOT.schema.json`, and exposes
`cyclonedx.validation.json.JsonStrictValidator`. `jsonschema` is installed.
Both the schemas and the validator are on disk, so validation answers
without a network round trip.

**The workspace around it.** `uv.lock` at the repository root is the
workspace's one resolution of its runtime and development requirements — what
`uv sync` installs; `uv lock --check` reports whether it
is still current. The lock resolves the root distribution and its workspace
members together, and a member that nothing depends on is in the lock but not
in the root distribution's dependency closure. `uv export` reads that lock.
