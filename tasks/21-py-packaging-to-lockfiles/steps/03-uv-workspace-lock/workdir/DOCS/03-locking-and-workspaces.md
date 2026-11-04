# Reference: dependency locking, workspaces, and unvendoring

This file is a pointer, not a specification. Everything normative lives in the
documents linked below; everything specific to this repository is in the
repository. Nothing here is unique to this workspace.

## 1. The normative documents

| What it defines | Where |
| --- | --- |
| Dependency Groups — `[dependency-groups]`, and one group including another | PEP 735 — <https://peps.python.org/pep-0735/> |
| `[project.dependencies]` and `[project.optional-dependencies]` | <https://packaging.python.org/en/latest/specifications/pyproject-toml/> |
| Dependency specifiers: the grammar of `name >= x, < y ; marker` | <https://packaging.python.org/en/latest/specifications/dependency-specifiers/> |
| Version specifiers, and what `~=`, `==x.*` and `!=` actually match | <https://packaging.python.org/en/latest/specifications/version-specifiers/> |
| Environment markers | <https://packaging.python.org/en/latest/specifications/dependency-specifiers/#environment-markers> |
| Editable installs | PEP 660 — <https://peps.python.org/pep-0660/> |
| uv: locking, syncing, and the lockfile's own format | <https://docs.astral.sh/uv/concepts/projects/lock/> |
| uv: workspaces — several distributions, one lock | <https://docs.astral.sh/uv/concepts/projects/workspaces/> |
| uv: dependency sources, including path and workspace sources | <https://docs.astral.sh/uv/concepts/projects/dependencies/> |
| uv: dependency groups | <https://docs.astral.sh/uv/concepts/projects/dependencies/#dependency-groups> |

`uv` is installed. `pip`, `python -m build` and the backends named in the other
reference documents in this directory are still installed and still work.

## 2. What this repository already says about itself

**The vendored trees are a `sys.path` insertion, not a fork.** `src/bento/__init__.py`
puts four directories from `bento/private/` on `sys.path` before anything else
imports them:

```python
for bundled_pkg in ["_ply", "_simplejson", "_yaku", "_six"]:
    v = "BENTO_UNBUNDLE%s" % bundled_pkg.upper()
    if USE_PRIVATE_MODULES and not os.environ.get(v, False):
        m_path = op.join(op.dirname(__file__), "private", bundled_pkg)
        sys.path.insert(0, m_path)
```

`USE_PRIVATE_MODULES` comes from `src/bento/_config.py`. Every consumer imports the
*public* name — `import ply.yacc` in `src/bento/parser/parser.py`, `import six` in
`src/bento/parser/utils.py`, `import simplejson as json` in
`src/bento/compat/api.py`, `from yaku.context import ...` in
`src/bento/commands/build_yaku.py`. The private
names are referenced nowhere outside the loop above.

**Not everything under `bento/private/` is vendored.** `bytecode.py`,
`_bytecode_2.py`, `_bytecode_3.py` and `version.py` are this project's own code:
`src/bento/core/meta.py` imports `bento.private.version`, and
`src/bento/commands/build_msi.py`, `build_egg.py` and
`src/bento/tests/test_misc.py` import `bento.private.bytecode`. `_yaku/` is also this project's own — it is
Bento's build system, developed in this repository and published nowhere.

**What the project currently uses instead of a lock.**

```
dev_requirements.txt      mock, nose
dev_requirements26.txt    -rdev_requirements.txt, unittest2
doc_requirements.txt      sphinx>=1.0.0
tox.ini                   deps = -r{toxinidir}/dev_requirements.txt
```

`dev_requirements26.txt`'s first line is the mechanism `[dependency-groups]`
replaces: a requirements file whose content is "everything in that other file,
and one more thing".

**Parse tables.** `src/bento/parser/parser.py` builds the `bento.info` grammar with
ply and passes a pickle path for the generated parse table, truncating that file
immediately before handing the path over. Read that function before changing
anything about ply — it is the part of this repository most sensitive to which
version of a dependency it gets.

## 3. Things worth knowing before you start

* Names in `dev_requirements.txt` were resolvable in 2008. Some of them are not
  installable on this interpreter at all. A group that cannot be installed is
  not a translation of anything.
* A lockfile is a record of a resolution, and what it resolves depends on what
  the requirements permit. "Whatever is newest" is a decision, and a lock makes
  it permanent.
* Removing a directory from `sys.path` and installing a distribution of the same
  name is not always a no-op: the vendored copy is a snapshot of one version, and
  the public distribution has moved since. `python -c "import ply; print(ply.__file__)"`
  tells you which copy you are actually running, before and after.
* A parse-table cache is written the first time the grammar is parsed, and read
  every time after. A tool that finds an empty or stale table where it expects a
  valid one does not always say so clearly.
* An installed distribution and a checkout can both satisfy an import. When a
  project's own package and its dependencies are all resolvable from the working
  directory, `import x` succeeding proves less than it looks like it proves.
* Four CPython interpreters are installed: 3.11, 3.12, 3.13 and 3.13t. `python`
  and `python3` are 3.11 — the interpreter this workspace runs on, and the one
  everything here is expected to work on.
  Tools that manage interpreters for you do not all default to the one on
  `PATH`; check what a tool actually selected rather than what you assumed.
