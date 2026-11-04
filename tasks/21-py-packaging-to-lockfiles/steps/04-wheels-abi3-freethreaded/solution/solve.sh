#!/usr/bin/env bash
# Step 04 reference solution: the `hello` extension builds against the stable
# ABI for the interpreters that have a GIL, builds again for the free-threaded
# one that does not, and tells the interpreter it does not need the lock.
#
# Cumulative: this runs on the tree step 03 left behind, so /app is one uv
# workspace with one lock, `examples/simples/single_extension` is a member of
# it, and its extension is declared to setuptools.
set -euo pipefail
cd /app

HELLO=examples/simples/single_extension

# --- 1. the extension declares its position on the GIL ----------------------
#
# Three changes to one file, and none of them changes what the module does:
#
#   * the limited API is turned on, so the compiled object depends only on the
#     subset of the C API CPython promises across feature releases -- except on
#     a free-threaded build, where that subset does not exist and Python.h
#     refuses outright. `pyconfig.h` comes first and alone because that is
#     where Py_GIL_DISABLED is defined: a test for it that runs before any
#     Python header has been read always answers "not free-threaded";
#   * the module is initialised in two phases -- PyInit__bar returns a
#     definition rather than a built module -- because a slot array is the only
#     place the next change can be written;
#   * Py_mod_gil says the module does not need the lock. Without it a
#     free-threaded interpreter turns the GIL back on when the module is
#     imported, prints a RuntimeWarning, and carries on: every artifact you can
#     inspect still looks free-threaded, and the process is not.
#
# m_size is 0 and not -1: this module keeps no state at all, and -1 means
# "state lives in a static, do not make me twice", which is the thing
# multi-phase initialisation exists to stop saying.
cat > "$HELLO/src/hellomodule.c" <<'EOF'
/* pyconfig.h on its own line, and before anything else: Py_GIL_DISABLED is
   defined there, so a test for it that runs before any Python header has been
   read always answers "not free-threaded" and turns the limited API on for the
   one build that cannot have it. */
#include <pyconfig.h>

#if !defined(Py_GIL_DISABLED) && !defined(Py_LIMITED_API)
#  define Py_LIMITED_API 0x030B0000
#endif

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

static PyModuleDef_Slot bar_slots[] = {
#ifdef Py_mod_gil
    /* This module keeps no state of its own, so it does not need the GIL.
       Without this slot a free-threaded interpreter turns the GIL back on
       when the module is imported. The guard is needed because Py_mod_gil
       arrived in 3.13 and is outside the limited API at 0x030B0000. */
    {Py_mod_gil, Py_MOD_GIL_NOT_USED},
#endif
    {0, NULL}
};

static struct PyModuleDef barmodule = {
    PyModuleDef_HEAD_INIT,
    "_bar",                      /* m_name */
    NULL,                        /* m_doc */
    0,                           /* m_size */
    HelloMethods,                /* m_methods */
    bar_slots,                   /* m_slots */
    NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit__bar(void)
{
    return PyModuleDef_Init(&barmodule);
}
EOF

# --- 2. a backend that can make the decision at build time ------------------
#
# The two wheels differ in how the extension is compiled, and which one is
# being built is only known once the interpreter is chosen. setuptools'
# `py-limited-api` is static TOML with nowhere for that condition to live now
# that `setup.py` is gone, so the declaration moves to a backend that has
# conditions: scikit-build-core, driving CMake, which is also where
# `Development.SABIModule` -- the stable-ABI half of CPython's own CMake
# support -- already exists.
cat > "$HELLO/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.15)
project(hello LANGUAGES C)

# HELLO_SABI is the lowest CPython the stable-ABI build serves, and is set by
# the build backend. It is empty on a free-threaded interpreter, where there is
# no stable ABI to link against and the module is built for that interpreter
# alone.
if(HELLO_SABI)
  find_package(Python REQUIRED COMPONENTS Interpreter Development.Module Development.SABIModule)
  python_add_library(_bar MODULE src/hellomodule.c WITH_SOABI USE_SABI ${HELLO_SABI})
else()
  find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
  python_add_library(_bar MODULE src/hellomodule.c WITH_SOABI)
endif()

install(TARGETS _bar DESTINATION hello)
EOF

cat > "$HELLO/pyproject.toml" <<'EOF'
[build-system]
requires = ["scikit-build-core"]
build-backend = "scikit_build_core.build"

[project]
name = "hello"
version = "1.0"
description = "A toy package with one C extension, from Bento's own examples"
# The floor the wheel's abi3 tag implies, stated where an installer reads it.
# A wheel tagged cp311-abi3 installs on every CPython from 3.11 up; a
# distribution claiming to support less than that is telling installers to take
# a wheel that will not import.
requires-python = ">=3.11"

[tool.scikit-build]
wheel.packages = ["hello"]
# One wheel serves 3.11, 3.12 and 3.13: the extension is compiled against the
# limited API and the wheel is tagged for the stable ABI.
wheel.py-api = "cp311"
cmake.define.HELLO_SABI = "3.11"

# ... except on a free-threaded interpreter, where there is no limited API to
# build against. Both halves of the declaration have to stand down: the tag, or
# the wheel lies about what is inside it, and the compile, or Python.h refuses.
# What comes out is an ordinary cp313t wheel for that interpreter alone.
[[tool.scikit-build.overrides]]
if.abi-flags = "t"
wheel.py-api = ""
cmake.define.HELLO_SABI = ""
EOF

# `MANIFEST.in` stays where it is. scikit-build-core does not read it -- its
# sdist is built from the source directory -- and removing a manifest is a
# different act from the one this milestone asked for. The sdist it used to
# shape still carries both C files and still rebuilds into the same wheel.

# --- 3. assert the edits landed, in bytes -----------------------------------
PYTHONPYCACHEPREFIX=/tmp/pyc python3 - <<'PY'
import pathlib, tomllib

hello = pathlib.Path("examples/simples/single_extension")

c = (hello / "src/hellomodule.c").read_text()
for want in ("#include <pyconfig.h>", "Py_LIMITED_API 0x030B0000",
             "PyModuleDef_Slot bar_slots[]", "Py_mod_gil",
             "Py_MOD_GIL_NOT_USED", "bar_slots,", "PyModuleDef_Init(&barmodule)",
             "PyInit__bar"):
    assert want in c, "%r is not in hellomodule.c" % (want,)
assert "PyModule_Create" not in c, "single-phase initialisation survived"
assert "-1,                          /* m_size */" not in c, "m_size is still -1"

cml = (hello / "CMakeLists.txt").read_text()
assert "USE_SABI ${HELLO_SABI}" in cml
assert "Development.SABIModule" in cml

data = tomllib.loads((hello / "pyproject.toml").read_text())
assert data["build-system"]["build-backend"] == "scikit_build_core.build"
assert data["project"]["requires-python"] == ">=3.11"
sk = data["tool"]["scikit-build"]
assert sk["wheel"]["py-api"] == "cp311"
assert sk["cmake"]["define"]["HELLO_SABI"] == "3.11"
over = sk["overrides"]
assert len(over) == 1 and over[0]["if"]["abi-flags"] == "t"
assert over[0]["wheel"]["py-api"] == ""
assert over[0]["cmake"]["define"]["HELLO_SABI"] == ""
assert not (hello / "setup.py").exists()
print("hello: limited API opt-in, multi-phase init, Py_mod_gil, "
      "conditional backend declaration")
PY

# --- 4. the lock follows the declaration ------------------------------------
#
# `hello` is a workspace member, so its build backend and its requires-python
# are part of what uv.lock records. A lock produced before this change is stale
# the moment it lands.
uv lock
uv lock --check

# --- 5. the two wheels, built the way the workspace builds them -------------
#
# Built into a scratch directory and thrown away: this is the check that the
# declaration above is true, not a release.
OUT=$(mktemp -d)
uv build --wheel --package hello --python 3.11  --out-dir "$OUT"
uv build --wheel --package hello --python 3.13t --out-dir "$OUT"
uv build --sdist --package hello --out-dir "$OUT"
uv build --wheel --package bento --out-dir "$OUT"
uv build --wheel --package yaku  --out-dir "$OUT"

PYTHONPYCACHEPREFIX=/tmp/pyc python3 - "$OUT" <<'PY'
import fnmatch, pathlib, sys, zipfile

out = pathlib.Path(sys.argv[1])
names = sorted(p.name for p in out.glob("*"))
print("built: %s" % ", ".join(names))

def one(prefix, suffix=".whl"):
    got = [p for p in out.glob("*") if p.name.startswith(prefix)
           and p.name.endswith(suffix)]
    assert len(got) == 1, "%s* -> %r" % (prefix, [p.name for p in got])
    return got[0]

abi3 = one("hello-1.0-cp311-abi3")
ft = one("hello-1.0-cp313-cp313t")
one("hello-1.0", ".tar.gz")
one("bento-0.2.0-py3-none-any")
one("yaku-0.1.0-py3-none-any")

for wheel, want in ((abi3, "hello/_bar.abi3.so"),
                    (ft, "hello/_bar.cpython-313t-*.so")):
    members = zipfile.ZipFile(wheel).namelist()
    assert any(fnmatch.fnmatch(m, want) for m in members), \
        "%s does not carry %s: %r" % (wheel.name, want, members)

for wheel in (one("bento-0.2.0-py3-none-any"), one("yaku-0.1.0-py3-none-any")):
    for member in zipfile.ZipFile(wheel).namelist():
        assert not member.endswith((".so", ".pyd")), \
            "%s carries compiled code: %s" % (wheel.name, member)
print("the abi3 wheel, the free-threaded wheel, the sdist and two pure wheels")
PY
rm -rf "$OUT"

# --- 6. the handoff ---------------------------------------------------------
cat >> WORKSPACE_HANDOFF.md <<'EOF'

## Step 04 -- wheels that are honest about who can install them

* `examples/simples/single_extension` builds through scikit-build-core and a
  twelve-line `CMakeLists.txt` instead of setuptools. The move was forced by
  the work rather than chosen: the extension has to be compiled one way for
  the interpreters that have a GIL and another way for the free-threaded one,
  and setuptools' `py-limited-api` is static TOML with nowhere for that
  condition to live now that `setup.py` is gone.
* `uv build --wheel --package hello --python 3.11` produces
  `hello-1.0-cp311-abi3-linux_x86_64.whl`, one file that installs, imports and
  runs on 3.11, 3.12 and 3.13. `wheel.py-api = "cp311"` tags it and
  `cmake.define.HELLO_SABI = "3.11"` makes the tag true: CMake's
  `Development.SABIModule` links the module against the stable ABI, and
  `Py_LIMITED_API 0x030B0000` in `src/hellomodule.c` restricts what it may
  call. `requires-python = ">=3.11"` is the same number where an installer
  reads it.
* `uv build --wheel --package hello --python 3.13t` produces
  `hello-1.0-cp313-cp313t-linux_x86_64.whl` from the same source and the same
  declaration. The `[[tool.scikit-build.overrides]]` block keyed on
  `if.abi-flags = "t"` stands both halves down, because a free-threaded CPython
  has no stable ABI: `Python.h` refuses to compile with `Py_LIMITED_API`
  defined, and a `cp311-abi3` tag on a free-threaded build would be a claim the
  file cannot keep.
* `src/hellomodule.c` is initialised in two phases -- `PyInit__bar` returns
  `PyModuleDef_Init(&barmodule)` -- and its slot array carries
  `{Py_mod_gil, Py_MOD_GIL_NOT_USED}`. Without that slot, importing the module
  on the free-threaded interpreter re-enables the global interpreter lock for
  the whole process: it prints a `RuntimeWarning` and keeps going, and every
  property you could read off the wheel is still true. `m_size` is 0 because
  the module keeps no state. What the module does is unchanged --
  `hello.hello()` prints `Hello from C`, takes any positional arguments and
  returns `None`.
* `bento` and `yaku` are untouched and their wheels are still
  `py3-none-any` with no compiled member.
* `MANIFEST.in` is left in place: scikit-build-core does not read it, and the
  sdist it used to shape is now built from the source directory, still carries
  both C files, and still rebuilds into the same wheel.
* `uv.lock` was regenerated: a member's build backend and `requires-python` are
  part of what the lock records.
EOF
