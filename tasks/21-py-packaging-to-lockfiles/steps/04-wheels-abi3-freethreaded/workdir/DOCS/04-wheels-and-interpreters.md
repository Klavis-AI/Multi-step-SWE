# Wheels, ABIs and interpreters

The normative references for this milestone, and what this repository already
says about itself. Nothing here is a recommendation about how to do the work.

## Specifications

**Platform compatibility tags** — what the three fields of a wheel's filename
mean, how an installer decides whether a wheel is for it, and what the `abi3`
ABI tag says.
<https://packaging.python.org/en/latest/specifications/platform-compatibility-tags/>

**Binary distribution format** — the wheel file itself: the filename, the
`WHEEL` metadata file and its `Tag:` lines, `RECORD`, and what "one wheel, many
tags" means on disk.
<https://packaging.python.org/en/latest/specifications/binary-distribution-format/>

**C API Stable Application Binary Interface** — which part of CPython's C API is
promised across feature releases, how a module opts into it, and what
`Py_LIMITED_API` selects.
<https://docs.python.org/3/c-api/stable.html>

**PEP 384 — Defining a Stable ABI**, the specification the above implements.
<https://peps.python.org/pep-0384/>

**PEP 489 — Multi-phase extension module initialization** — the second way an
extension module can be initialised, what a module definition's slots are, and
what an initialisation function returns under it.
<https://peps.python.org/pep-0489/>

**Module Objects (C API)** — `PyModuleDef`, `PyModuleDef_Slot`,
`PyModuleDef_Init`, `PyModule_Create`, and the meaning of `m_size`.
<https://docs.python.org/3/c-api/module.html>

**PEP 703 — Making the Global Interpreter Lock Optional in CPython**, the
specification the free-threaded build implements.
<https://peps.python.org/pep-0703/>

**Python support for free threading** — the user-facing description of the
free-threaded build, including how an extension module declares whether it can
run without the GIL and what happens to a process when one does not.
<https://docs.python.org/3/howto/free-threading-extensions.html>

**Declaring project metadata** — `requires-python` and what a version specifier
on it means.
<https://packaging.python.org/en/latest/specifications/pyproject-toml/#requires-python>

**uv: building distributions** — `uv build`, and selecting the interpreter a
build runs on.
<https://docs.astral.sh/uv/concepts/build-backend/>
<https://docs.astral.sh/uv/reference/cli/#uv-build>

**uv: Python versions** — how uv discovers and names the interpreters available
on a machine, including free-threaded ones.
<https://docs.astral.sh/uv/concepts/python-versions/>

## What this repository already contains

**The extension.** `hello` — the example that started out as
`examples/simples/single_extension/` — is a distribution of this workspace. Its
extension module is one C file,
`src/hellomodule.c`, which defines a module named `_bar` with a single method
`hello` taking `METH_VARARGS`. `src/hellolib.c` is in the directory and is not
part of the extension's sources.

**What the package re-exports.** `hello/__init__.py` is

```python
from hello.bar import \
        foo
from hello._bar import \
        hello
```

so `hello.hello` is the function the extension defines and `hello.foo` is
`hello.bar.foo`. Both of those identities were already part of `hello`'s
interface before this milestone, and stay part of it.

**Interpreters available here.** `uv python list` reports every interpreter this
environment provides and the path of each; `uv python find <request>` resolves
one request. The free-threaded build of CPython 3.13 is among them and uv's name
for it carries a `t`. `sys._is_gil_enabled()` exists from CPython 3.13 and
answers, for the running process, whether the global interpreter lock is on.

**Building a distribution here.** `uv build` builds distributions of this
workspace and is run from the repository root: `--package <name>` selects one by
the name its `[project]` table declares, `--wheel` and `--sdist` select what to
produce, `--python <request>` selects the interpreter the build runs on — the
same request syntax `uv python find` takes — and `--out-dir` says where the
files land. `python -m build` is installed as well and builds whatever
distribution is rooted at the directory it is pointed at, on the interpreter it
is running under.

**Reading a wheel.** A wheel is a zip. `python -m zipfile -l <wheel>` lists its
members and `python -m zipfile -e` extracts them; the `WHEEL` file inside the
`.dist-info` directory carries the `Tag:` lines, and the filename carries the
same information in its last three fields. `auditwheel show <wheel>` reports
what an ELF inside a wheel actually requires of a system, which is a different
question from what the wheel's tag claims.
