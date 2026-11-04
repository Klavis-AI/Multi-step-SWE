# Reference: building a Python distribution to the current packaging standards

This file is a pointer, not a specification. Everything normative lives in the
documents linked below; everything specific to this repository is in the
repository. Nothing here is unique to this workspace.

## 1. The normative documents

These are the current standards for how a Python project declares its build and
its metadata. They are the definition of "correct" for this milestone.

| What it defines | Where |
| --- | --- |
| Source trees declare their build system in `pyproject.toml` | PEP 518 — <https://peps.python.org/pep-0518/> |
| A build backend interface any frontend can call, in an isolated environment | PEP 517 — <https://peps.python.org/pep-0517/> |
| The `[project]` table: the standard, declarative spelling of project metadata | PEP 621 — <https://peps.python.org/pep-0621/> |
| The consolidated, maintained versions of all three | PyPA specifications — <https://packaging.python.org/en/latest/specifications/pyproject-toml/> |
| Core metadata: the fields that end up in a wheel's `METADATA` and an sdist's `PKG-INFO` | <https://packaging.python.org/en/latest/specifications/core-metadata/> |
| Entry points, including the `console_scripts` group | <https://packaging.python.org/en/latest/specifications/entry-points/> |
| The binary distribution format (what a `.whl` is, and what lives in `.dist-info`) | <https://packaging.python.org/en/latest/specifications/binary-distribution-format/> |
| The source distribution format (what a `.tar.gz` is, and what `PKG-INFO` must carry) | <https://packaging.python.org/en/latest/specifications/source-distribution-format/> |

Backend-specific documentation, for whichever backend you declare, is that
backend's own. For example: setuptools' userguide at
<https://setuptools.pypa.io/en/latest/userguide/>, Hatchling's at
<https://hatch.pypa.io/latest/config/build/>, Flit's at
<https://flit.pypa.io/en/stable/pyproject_toml.html>, PDM's at
<https://backend.pdm-project.org/>.

## 2. What this repository already says about itself

Bento is a packaging tool, and it describes itself in its own format. The
authoritative statement of this distribution's identity is `/app/bento.info`,
read by Bento's own parser:

* `Name`, `Version`, `Summary`, `Url`, `Author`, `AuthorEmail`, `Maintainer`,
  `MaintainerEmail`, `License`, `Platforms`, `Classifiers`, `Keywords` — the
  distribution's metadata, one field per line or per indented block.
* `DescriptionFromFile:` names the file whose contents are the long
  description.
* `Executable:` blocks declare a command-line program, the module it lives in,
  and the function that is its entry point.
* `Library: Packages:` lists the importable packages, some of them behind
  `if flag(...)` conditionals.
* `DataFiles:` blocks declare non-Python files that ship with the package,
  giving a source directory and the file patterns inside it.
* `ExtraSourceFiles:` lists files that belong in a source release even though
  they are not modules.
* `HookFile:` names the script Bento runs for build hooks.

`/app/setup.py` is how the tree builds today. It reads:

```python
from bento.distutils.monkey_patch import setup
setup()
```

That is: to build Bento you must first be able to import Bento, and the
metadata is produced by running Bento's parser over `bento.info` at build time.
`/app/bootstrap.py` exists to break that circle by hand.

## 3. Things worth knowing before you start

* `python -m build` is the reference frontend; it is installed. It creates a
  fresh isolated environment from `[build-system] requires`, asks the backend
  for an sdist, then builds a wheel from that sdist. A local package cache is
  warm, so the isolated environment does not need a fast network.
* The importable surface is wider than it looks. `bento.info`'s
  `Library:` section lists the packages and modules that ship, including the
  vendored upstream copies under `bento/private/`; `bento/__init__.py` puts
  those directories on `sys.path` at import time, so code elsewhere in the tree
  imports them under their upstream names.
* Files that are not Python modules do not travel into a wheel by accident.
  Every backend has its own way of declaring package data; consult the one you
  chose.
* A source distribution is a separate question from a wheel: a tree can build a
  perfectly good wheel while producing an sdist that omits a file the build
  needs. The way to find out is to unpack the sdist somewhere else and build
  *that*.
* `git` is available and the checkout is a tagged release (`v0.2.0`).
