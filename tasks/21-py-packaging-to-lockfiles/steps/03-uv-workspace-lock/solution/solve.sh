#!/usr/bin/env bash
# Step 03 reference solution: delete the vendored third-party trees and declare
# them as dependencies, promote yaku to a workspace member, replace the three
# requirements files with dependency groups, and lock the whole workspace.
#
# Cumulative: this runs on the tree step 02 left behind, so `src/` exists,
# `pyproject.toml` declares the root distribution, and
# `examples/simples/single_extension/pyproject.toml` declares `hello`.
set -euo pipefail
cd /app

# --- 1. yaku leaves bento/private/ and becomes a distribution ---------------
#
# `bento/private/_yaku` is not vendored code: it is Bento's own build system,
# developed in this repository and published on no index. `bentomakerlib` and
# `bento.backends.yaku_backend` import it as `yaku`, so it has to stay
# importable under that name -- which is what a workspace member with a path
# source is for. It keeps the src layout step 02 established for the rest of
# this repository.
#
# `mv`, not `git mv`: /app is a bind mount and these paths can land on
# different devices, where rename(2) fails with EXDEV. `mv(1)` falls back to
# copy-and-unlink; `git mv` does not.
mkdir -p yaku/src
mv src/bento/private/_yaku/yaku yaku/src/yaku
mv src/bento/private/_yaku/README yaku/README
mv src/bento/private/_yaku/NOTES yaku/NOTES

# Its own declaration, in the same shape as the two distributions this
# repository already has. The version is yaku's own: this is the first time it
# has been a distribution, so it starts at 0.1.0 rather than inheriting bento's.
cat > yaku/pyproject.toml <<'EOF'
[build-system]
requires = ["setuptools>=77"]
build-backend = "setuptools.build_meta"

# Bento's build system. Until now it lived in bento/private/_yaku and was put
# on sys.path by hand at import time. It is developed in this repository and
# published nowhere, which is why the root distribution resolves it as a
# workspace member rather than from an index.
[project]
name = "yaku"
version = "0.1.0"
description = "Bento's build system"
readme = { file = "README", content-type = "text/plain" }
license = { text = "BSD" }
authors = [{ name = "David Cournapeau", email = "cournape@gmail.com" }]
requires-python = ">=3.11"

[tool.setuptools.packages.find]
where = ["src"]
include = ["yaku*"]
EOF

# --- 2. the vendored copies go ----------------------------------------------
#
# What is left of _yaku is its old distutils packaging (setup.py, MANIFEST.in),
# its own examples and tools, and a shell test runner -- none of which describe
# the distribution yaku/pyproject.toml now declares.
rm -rf src/bento/private/_ply src/bento/private/_simplejson \
       src/bento/private/_six src/bento/private/_yaku

# `bento/private/` itself stays: __init__.py, bytecode.py, _bytecode_2.py,
# _bytecode_3.py and version.py are this project's own code, imported by
# bento/core/meta.py, bento/commands/build_egg.py, bento/commands/build_msi.py
# and bento/tests/test_misc.py.

# --- 3. nothing rewrites sys.path any more ----------------------------------
python3 - <<'PY'
import pathlib

p = pathlib.Path("src/bento/__init__.py")
before = p.read_text()

# The whole vendoring apparatus in one span: the three imports it needed, the
# _config switch it read, and the loop. Everything below it -- the
# importlib.metadata version read step 02 introduced -- is untouched.
start = before.index("import sys\n")
end = before.index("from importlib.metadata import")
after = before[:start] + before[end:]

# The module docstring still describes the directory that is going away.
after = after.replace(
    "    - bento.private: bundled packages\n",
    "    - bento.private: small self-contained helpers used by the above\n")

p.write_text(after)

text = p.read_text()
# Assert the edit landed, in bytes, rather than trusting that it did.
assert "sys.path" not in text, "sys.path survived in bento/__init__.py"
assert "BENTO_UNBUNDLE" not in text
assert "USE_PRIVATE_MODULES" not in text
assert "bundled packages" not in text
assert text.startswith('"""'), "the docstring moved"
assert '"""\nfrom importlib.metadata import' in text, "the seam is not clean"
assert '__version__ = _read_version("bento")' in text
compile(text, "bento/__init__.py", "exec")
print("bento/__init__.py: removed %d bytes of vendoring apparatus"
      % (len(before) - len(text)))
PY

# The switch that loop read. Nothing else in the tree mentions it.
python3 - <<'PY'
import pathlib
p = pathlib.Path("src/bento/_config.py")
before = p.read_text()
after = before.replace("\nUSE_PRIVATE_MODULES = True\n", "\n")
assert after != before, "USE_PRIVATE_MODULES was not where it was expected"
p.write_text(after.rstrip("\n") + "\n")
assert "USE_PRIVATE_MODULES" not in p.read_text()
compile(p.read_text(), "bento/_config.py", "exec")
PY

# Prose is not code. `DOCS/03-locking-and-workspaces.md` quotes the very loop
# this section removed, and the handoff written at the end names the switch it
# removed; a check that counted those would fail this script for doing its job.
PROSE=(--exclude='*.md' --exclude='*.rst' --exclude='*.txt.orig')
if grep -rn "${PROSE[@]}" 'BENTO_UNBUNDLE\|USE_PRIVATE_MODULES' . 2>/dev/null; then
    echo "a vendoring switch survived" >&2; exit 1
fi

# --- 4. bento.info stops describing trees the distribution no longer ships ---
#
# It is the authoritative statement of what this distribution contains, so the
# two conditional Library sections and the two flags that guarded them go. The
# flags existed only to make the bundling optional, and there is nothing left
# to bundle. `bento.private` stays in the package list: the modules that are
# Bento's own are still there.
python3 - <<'PY'
import pathlib, re

p = pathlib.Path("bento.info")
before = p.read_text()

HEAD = re.compile(r"^(\s*)(?:if flag\((?:bundle|bundle_yaku)\):|"
                  r"Flag: (?:bundle|bundle_yaku))$")

out, pending, skip = [], [], None
for line in before.splitlines(True):
    if not line.strip():
        # Held back rather than emitted: a blank that separated two removed
        # stanzas must go with them, and a blank that separated a removed
        # stanza from a surviving one must stay.
        pending.append(line)
        continue
    indent = len(line) - len(line.lstrip(" "))
    if skip is not None:
        if indent > skip:
            pending = []
            continue
        skip = None
    m = HEAD.match(line.rstrip("\n"))
    if m:
        skip = len(m.group(1))
        pending = []
        continue
    out.extend(pending)
    pending = []
    out.append(line)
out.extend(pending)

p.write_text("".join(out))

text = p.read_text()
for gone in ("bundle", "_ply", "_simplejson", "_six", "_yaku"):
    assert gone not in text, "%r survived in bento.info" % gone
assert "        bento.private,\n" in text, "bento.private was dropped by accident"
assert "        bentomakerlib\n\nExecutable: bentomaker\n" in text, \
    "the Library section did not close cleanly"
assert "HookFile: bscript\n\nDataFiles: windows_arch_data\n" in text, \
    "the Flag section did not close cleanly"
print("bento.info: removed %d bytes of bundling declarations"
      % (len(before) - len(text)))
PY

# --- 5. the root distribution declares what it depends on -------------------
python3 - <<'PY'
import pathlib, tomllib

p = pathlib.Path("pyproject.toml")
text = p.read_text()

# `ply` is the one version-sensitive requirement in this repository.
# bento/parser/parser.py truncates the pickled parse table to zero bytes and
# then hands the same path to ply.yacc.yacc(): ply 3.4 and earlier regenerate
# the table when the pickle will not load, and later versions raise out of it
# instead. 3.4 is the published release corresponding to the copy that was
# vendored (3.3), and it needs no change to the parser at all.
#
# `six` and `simplejson` are used through their stable public API only
# (six.PY3/moves/iteritems, simplejson.dumps/loads), so they get a floor
# rather than a pin.
#
# requires-python is >=3.11 because that is what this workspace runs on and
# what the lock is resolved for; nothing here claims to support less.
block = '''requires-python = ">=3.11"
dependencies = [
    "ply==3.4",
    "six>=1.9",
    "simplejson>=3",
    "yaku",
]

'''
anchor = "\n[project.urls]\n"
assert anchor in text
text = text.replace(anchor, "\n" + block + "[project.urls]\n", 1)

# These excludes existed for _yaku's own examples/ and tools/ directories,
# which are gone with it.
text = text.replace(
    'exclude = ["bento.private._yaku.examples*", "bento.private._yaku.tools*"]\n',
    "")

# The comment above the table counted four namespace directories, three of
# which were the vendored trees.
text = text.replace(
    "# `namespaces = true`, and not the default, because four of the directories\n"
    "# this distribution has to ship have no __init__.py of their own:\n"
    "# bento/commands/wininst (the Windows launcher stubs), and the vendored\n"
    "# bento/private/_ply, _simplejson and _yaku trees whose importable packages sit\n"
    "# one level below. Plain find_packages() drops all four, which imports cleanly\n"
    "# from a checkout and fails the moment anything runs from the installed wheel.\n",
    "# `namespaces = true`, and not the default, because bento/commands/wininst --\n"
    "# the directory of Windows launcher stubs this distribution ships -- has no\n"
    "# __init__.py of its own. Plain find_packages() drops it, which imports\n"
    "# cleanly from a checkout and fails the moment anything runs from the\n"
    "# installed wheel.\n")

text = text.rstrip("\n") + '''

# One workspace: the root distribution plus every other distribution this
# repository contains. `uv lock` resolves all three together into a single
# uv.lock at the repository root.
[tool.uv.workspace]
members = ["yaku", "examples/simples/single_extension"]

# yaku is published on no index; it is resolved from this workspace.
[tool.uv.sources]
yaku = { workspace = true }

# What dev_requirements.txt, dev_requirements26.txt and doc_requirements.txt
# said, in the mechanism that replaced them. dev_requirements26.txt included
# dev_requirements.txt by reference and added one more name; `include-group` is
# that line. `nose` and `unittest2` are dropped rather than translated: neither
# installs on this interpreter, so naming them would be a group that cannot be
# resolved. pytest runs this repository's tests now, and mock is kept because
# the tests import it by name.
[dependency-groups]
dev = ["mock>=5"]
test = [{ include-group = "dev" }, "pytest>=8", "coverage>=7"]
doc = ["sphinx>=7"]
'''
p.write_text(text)

check = p.read_text()
for want in ("[tool.uv.workspace]", "[tool.uv.sources]", "[dependency-groups]",
             "include-group", '"ply==3.4"', '"yaku"', 'requires-python'):
    assert want in check, want
assert "_yaku" not in check
parsed = tomllib.loads(check)
assert parsed["project"]["dependencies"][0] == "ply==3.4"
assert parsed["tool"]["uv"]["sources"]["yaku"] == {"workspace": True}
print("pyproject.toml: dependencies, workspace, sources and groups declared")
PY

# --- 6. the requirements files go, and so does every reference to them -------
rm -f dev_requirements.txt dev_requirements26.txt doc_requirements.txt

# tox.ini named two of them and ran nosetests over interpreters that predate
# this workspace. It names the groups that replaced them instead.
cat > tox.ini <<'EOF'
[tox]
envlist = py311,py312,py313

[testenv]
dependency_groups = test
commands = pytest src/bento src/bentomakerlib

[testenv:doc]
dependency_groups = doc
commands = sphinx-build -b html doc doc/_build/html
EOF

# .travis.yml installed the same two files by path.
cat > .travis.yml <<'EOF'
language: python
python:
   - 3.11
   - 3.12
   - 3.13
# The test group carries what dev_requirements.txt used to.
install:
   - pip install uv
   - uv sync --group test
script:
  - uv run python -m bentomakerlib.bentomaker distcheck;
  - uv run python check_examples.py
EOF

# Nothing left installs from them: no `-r` reference to any of the three names
# survives anywhere in the tree, and the files themselves are gone.
if grep -rnE "${PROSE[@]}" -- '-r[[:space:]]*[{}[:alnum:]_./\\-]*(dev_requirements26|dev_requirements|doc_requirements)\.txt' . 2>/dev/null; then
    echo "something still installs from a requirements file" >&2; exit 1
fi
if find . -name 'dev_requirements*.txt' -o -name 'doc_requirements*.txt' | grep .; then
    echo "a requirements file survived" >&2; exit 1
fi

# --- 7. lock the workspace --------------------------------------------------
#
# uv picks an interpreter for the project, and its default preference is the
# newest one it can find rather than the one on PATH. This workspace runs on
# CPython 3.11 -- the interpreter step 2's milestone named -- so the project
# pins it rather than leaving it to whatever happens to be installed.
printf '3.11\n' > .python-version
uv lock
uv lock --check

# --- 8. the handoff ---------------------------------------------------------
cat >> WORKSPACE_HANDOFF.md <<'EOF'

## Step 03 -- one workspace, one lock, no vendored copies

* `bento/private/_ply`, `_simplejson` and `_six` are gone; `ply`, `six` and
  `simplejson` are `[project.dependencies]` of the root distribution. `ply` is
  pinned to `==3.4`: `bento/parser/parser.py` truncates the pickled parse table
  before handing the path to `ply.yacc.yacc()`, and ply 3.8 and later raise out
  of the empty pickle instead of regenerating the table. 3.4 is the published
  release matching the copy that was vendored (3.3) and needs no change to the
  parser. `six` and `simplejson` get floors, not pins: only their stable public
  API is used.
* `bento/private/_yaku/yaku` moved to `yaku/src/yaku`. `yaku/pyproject.toml`
  declares it as a distribution of its own; the root distribution depends on it
  and `[tool.uv.sources]` resolves it from the workspace, since it is published
  on no index. Its old distutils packaging, examples and tools went with the
  vendored trees.
* `src/bento/__init__.py` no longer inserts anything onto `sys.path`, and
  `USE_PRIVATE_MODULES` is gone from `src/bento/_config.py`. `bento.private`
  still ships `bytecode.py`, `_bytecode_2.py`, `_bytecode_3.py` and
  `version.py`, which are Bento's own and whose importers are unchanged.
* `bento.info` no longer declares the vendored trees, and the `bundle` and
  `bundle_yaku` flags that guarded them are gone with them.
* `[tool.uv.workspace]` names `yaku` and `examples/simples/single_extension`;
  `uv.lock` at the repository root is the resolution of all three
  distributions together, with an exact version and hashes for everything that
  comes from an index.
* `dev_requirements.txt`, `dev_requirements26.txt` and `doc_requirements.txt`
  are replaced by `[dependency-groups]` -- `dev`, `test` (which includes `dev`,
  the way dev_requirements26.txt included dev_requirements.txt) and `doc` --
  and deleted. `tox.ini` and `.travis.yml` name the groups instead of the
  files.
* `.python-version` pins CPython 3.11: uv's default interpreter preference is
  the newest it can find, which is not the interpreter this workspace runs on.
EOF
