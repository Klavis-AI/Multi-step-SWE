#!/usr/bin/env bash
# Step 02 reference solution: move the two importable trees under src/, let the
# installed distribution be the only place the version is written down, and turn
# examples/simples/single_extension into a distribution of its own.
#
# Cumulative: this runs on the tree step 01 left behind, so pyproject.toml and
# MANIFEST.in already exist and are edited rather than rewritten.
set -euo pipefail
cd /app

# --- 1. the layout ----------------------------------------------------------
#
# A plain move. Everything inside each tree travels with it, and nothing else
# in the repository refers to them by path -- the imports are all `import
# bento...`, which is what makes the move invisible from the outside.
mkdir -p src
mv bento src/bento
mv bentomakerlib src/bentomakerlib

# setuptools derives `package_dir = {"": "src"}` from `where`, so the wheel
# carries `bento/...` and not `src/bento/...`: the package root moved, the
# package names did not.
sed -i 's|^where = \["\."\]$|where = ["src"]|' pyproject.toml

# The manifest names files by path, and the paths moved. `bento.info`,
# `bscript`, the prose files and the licence stay at the repository root.
cat > MANIFEST.in <<'EOF'
include bento.info bscript
include INSTALL LICENSE.txt PACKAGERS.txt README.rst TESTING.txt THANKS
include src/bento/__package_info.py.in
recursive-include src/bento/commands *.exe
recursive-include src/bento/parser/tests/functionals *.info
recursive-include src/bento/testing/bentos *.py *.info
EOF

# `bento.info` names files by path on nine lines, and every one of those paths
# moved. One rule, applied uniformly: repath what moved, and do not remove
# entries for files earlier steps deleted -- `setup.py` stays in
# `ExtraSourceFiles` because deleting a manifest entry is a different act from
# correcting one, and this milestone asked for neither.
#
# Only one of the nine is read by anything that scores: `Library: Modules:`,
# which `01.wheel.declared-packages` maps to a wheel member. That row strips
# exactly one leading `src/` before the lookup -- `src` is not a package and the
# instruction says no `src/` prefix appears in the wheel -- so the repathed
# manifest and the untouched one both pass, which is the point. Repathing here
# means every oracle run exercises that normalisation end to end; the
# untouched-manifest branch is exercised by the blind run, which left
# `bento.info` byte-identical and also scored 1.000. See
# `authoring/checks/declared-modules-src.py` for the row's own five cases.
#
# The other eight are read by Bento's own build, which PEP 517 has replaced.
# They are corrected because leaving a manifest pointing at a path that no
# longer exists is how the next person loses an afternoon.
sed -i 's|^ConfigPy: bento/__config_py\.py$|ConfigPy: src/bento/__config_py.py|' bento.info
sed -i 's|^MetaTemplateFiles: bento/__package_info\.py\.in$|MetaTemplateFiles: src/bento/__package_info.py.in|' bento.info
# The two `SourceDir:` lines, longest first so the anchored short one cannot
# eat the `bento/commands` line it is a prefix of.
sed -i 's|^    SourceDir: bento/commands$|    SourceDir: src/bento/commands|' bento.info
sed -i 's|^    SourceDir: bento$|    SourceDir: src/bento|' bento.info
# The `ExtraSourceFiles` globs that reach into the two trees that moved.
sed -i 's|^    bento/\*\*/tests/\*\.py,$|    src/bento/**/tests/*.py,|' bento.info
sed -i 's|^    bento/testing/bentos/\*\.py,$|    src/bento/testing/bentos/*.py,|' bento.info
sed -i 's|^    bento/testing/bentos/\*\.info,$|    src/bento/testing/bentos/*.info,|' bento.info
sed -i 's|^    bentomakerlib/\*\*/tests/\*\.py$|    src/bentomakerlib/**/tests/*.py|' bento.info
# `Library: Modules:` -- the one the scorer reads. It sits two levels in, under
# `if flag(bundle):`, so the indentation is carried rather than spelled: an
# anchored eight-space pattern matched nothing here and the guard below is what
# said so.
sed -i 's|^\( *\)bento/private/_six/six$|\1src/bento/private/_six/six|' bento.info

# Every path the manifest names is now a `src/` path. A sed that matches
# nothing is silent, which is the whole failure mode, so check rather than hope.
if grep -nE '(^|[[:space:]])(bento|bentomakerlib)/' bento.info; then
    echo "solve: bento.info still names a pre-move path -- a sed above did not match" >&2
    exit 6
fi

# --- 2. the version ---------------------------------------------------------
#
# `bento.info` states the version, `pyproject.toml` states it to the build, and
# the installed distribution's own metadata is what the package reads back. No
# Python module carries the string, and the module Bento used to generate at
# install time is no longer imported -- which is why a checkout said `nobuilt`
# and an installed copy now says what it actually is.
python3 - <<'PY'
import io

path = "src/bento/__init__.py"
with io.open(path, encoding="utf-8") as fh:
    text = fh.read()

old = """try:
    import __package_info
except ImportError:
    __version__ = 'nobuilt'
    __git_revision__ = 'nobuilt'
else:
    from __package_info import VERSION as __version__, GIT_REVISION as __git_revision__
"""

new = '''from importlib.metadata import PackageNotFoundError, version as _read_version

try:
    __version__ = _read_version("bento")
except PackageNotFoundError:
    # Imported from a source checkout rather than from an installed
    # distribution: there is no metadata to read, and nothing to guess from.
    __version__ = "unknown"

# Kept because bentomaker's --full-version reads it. The build records no
# revision, so it is empty rather than a value invented here.
__git_revision__ = ""
'''

assert old in text, "the generated-module fallback is not where it was"
with io.open(path, "w", encoding="utf-8") as fh:
    fh.write(text.replace(old, new))
PY

# --- 3. the example extension, as a distribution ----------------------------
cd examples/simples/single_extension

# Its own declaration. Name and version are what its bento.info declares; the
# extension is declared to a backend made for compiled code -- scikit-build-core,
# driving CMake -- rather than to a setup.py.
cat > pyproject.toml <<'EOF'
[build-system]
requires = ["scikit-build-core"]
build-backend = "scikit_build_core.build"

[project]
name = "hello"
version = "1.0"
description = "A toy package with one C extension, from Bento's own examples"

[tool.scikit-build]
wheel.packages = ["hello"]
EOF

# The extension, as CMake sees it: one module, built against this interpreter's
# development headers and installed inside the `hello` package.
cat > CMakeLists.txt <<'EOF'
cmake_minimum_required(VERSION 3.15)
project(hello LANGUAGES C)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
python_add_library(_bar MODULE src/hellomodule.c WITH_SOABI)

install(TARGETS _bar DESTINATION hello)
EOF

# scikit-build-core builds its sdist from the source directory and does not
# read MANIFEST.in; the file is kept as the statement of what the source
# release carries (both C files included), and later steps leave it in place.
cat > MANIFEST.in <<'EOF'
include bento.info bscript
include src/*.c
EOF

# The Python 3 C API. Same function, same message, same return value: a module
# definition and PyInit__bar replace Py_InitModule and init_bar, which have not
# existed since Python 2.
cat > src/hellomodule.c <<'EOF'
#include <Python.h>
#include <stdio.h>

static PyObject*
hello(PyObject *self, PyObject *args)
{
    printf("Hello from C\n");
    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef HelloMethods[] = {
    {"hello",  hello, METH_VARARGS, "Print a hello world."},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef barmodule = {
    PyModuleDef_HEAD_INIT,
    "_bar",                      /* m_name */
    NULL,                        /* m_doc */
    -1,                          /* m_size */
    HelloMethods,                /* m_methods */
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit__bar(void)
{
    return PyModule_Create(&barmodule);
}
EOF

# `print "foo"` is a syntax error on every Python 3.
cat > hello/bar.py <<'EOF'
def foo():
    print("foo")
EOF

# The distutils driver the extension used to be built by, and the toy converter
# beside it, are what the pyproject.toml replaces.
rm -f setup.py

cd /app

# --- 4. the handoff ---------------------------------------------------------
cat >> WORKSPACE_HANDOFF.md <<'EOF'

## Step 02 -- src layout, single-sourced version, the example as a distribution

* `bento/` and `bentomakerlib/` moved to `src/bento/` and `src/bentomakerlib/`.
  `[tool.setuptools.packages.find] where` follows them; the wheel is unchanged,
  and the repository root no longer shadows the installed distribution.
  `MANIFEST.in` and `bento.info`'s `ConfigPy:`/`MetaTemplateFiles:` paths were
  repointed at the new locations.
* `src/bento/__init__.py` reads `importlib.metadata.version("bento")` instead of
  importing the `__package_info` module Bento used to generate at install time.
  The version is declared once, in `bento.info` and `pyproject.toml`; no Python
  module carries it. `__git_revision__` is still a string, now empty.
* `examples/simples/single_extension/` is a distribution: its own
  `pyproject.toml` (name `hello`, version `1.0`, scikit-build-core backend),
  a `CMakeLists.txt` declaring the `_bar` extension, `MANIFEST.in`,
  `src/hellomodule.c` ported to the Python 3 C API (`PyInit__bar`), `hello/bar.py`
  ported to `print()`, and no `setup.py`.
EOF
