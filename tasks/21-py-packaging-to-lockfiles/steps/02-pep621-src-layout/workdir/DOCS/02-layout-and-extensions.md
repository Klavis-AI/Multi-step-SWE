# Reference: source layout, single-sourced versions, and building C extensions

This file is a pointer, not a specification. Everything normative lives in the
documents linked below; everything specific to this repository is in the
repository. Nothing here is unique to this workspace.

## 1. The normative documents

| What it defines | Where |
| --- | --- |
| `src` layout versus flat layout, and what each one means for imports | <https://packaging.python.org/en/latest/discussions/src-layout-vs-flat-layout/> |
| Single-sourcing a package version | <https://packaging.python.org/en/latest/discussions/single-source-version/> |
| Reading an installed distribution's own metadata at run time | `importlib.metadata` — <https://docs.python.org/3.11/library/importlib.metadata.html> |
| The `[project]` table, and the rest of the packaging specifications | <https://packaging.python.org/en/latest/specifications/pyproject-toml/> |
| Platform compatibility tags: what a wheel filename's last three fields mean | <https://packaging.python.org/en/latest/specifications/platform-compatibility-tags/> |
| Extending Python with C: module objects, method tables, and initialisation | <https://docs.python.org/3.11/extending/extending.html> |
| Multi-phase initialisation (`PyModuleDef_Slot`, `PyModuleDef_Init`) | PEP 489 — <https://peps.python.org/pep-0489/> |
| What changed in the C API between Python 2 and Python 3 | <https://docs.python.org/3.11/c-api/module.html> and <https://docs.python.org/3.11/howto/cporting.html> |

Backend-specific documentation, for whichever backend you declare, is that
backend's own. The two backends made for compiled extensions are installed
here: scikit-build-core at <https://scikit-build-core.readthedocs.io/> and
meson-python at <https://mesonbuild.com/meson-python/>. setuptools, which the
root distribution may keep using, documents its own extension support at
<https://setuptools.pypa.io/en/latest/userguide/ext_modules.html>. A C
compiler, `make`, `cmake`, `ninja` and `meson` are all on `PATH`.

## 2. What this repository already says about itself

**The version.** `bento.info` declares `Version:`, and `MetaTemplateFiles:`
names `bento/__package_info.py.in` — a template Bento's own build substitutes
into `bento/__package_info.py` at install time. `bento/__init__.py` then tries
to import that generated module and falls back to the string `nobuilt` when it
is not there, which on this interpreter is always:

```python
try:
    import __package_info
except ImportError:
    __version__ = 'nobuilt'
    __git_revision__ = 'nobuilt'
else:
    from __package_info import VERSION as __version__, GIT_REVISION as __git_revision__
```

`bentomakerlib/bentomaker.py` prints `bento.__version__` for `--version`, and
`bentomakerlib/help.py` and `bento/commands/core.py` put the same string in the
banner.

**The extension example.** `examples/simples/single_extension/` contains:

* `bento.info` — `Name: hello`, `Version: 1.0`, one package `hello`, one
  extension `hello._bar` from `src/hellomodule.c`, and one compiled library
  `foo` from `src/hellolib.c`.
* `hello/__init__.py` — re-exports `foo` from `hello.bar` and `hello` from the
  extension `hello._bar`.
* `hello/bar.py` — Python 2 source. `print "foo"` is a syntax error on any
  Python 3.
* `src/hellomodule.c` — the extension. `Py_InitModule` and an `init_bar`
  initialisation function are the Python 2 C API; neither exists in Python 3.
* `src/hellolib.c` — the compiled library, a single empty `foo()`.
* `setup.py` and `toytodist.py` — two hand-written distutils drivers.

## 3. Things worth knowing before you start

* A src layout is not a `[tool.<backend>]` key; it is where the files are. Every
  backend has its own way of being told where the package root is, and the
  wheel it produces must not change because of it.
* `python -c 'import bento'` run in a directory that contains `bento/` imports
  the checkout, not the installed distribution. That is the behaviour the move
  removes, and it is worth checking after the move rather than assuming.
* An installed distribution can read its own metadata; a checkout has no
  metadata to read. Whatever you do about the version has to work when the
  distribution is installed, and must not put the version back into a Python
  source file.
* A wheel containing compiled code cannot be `none-any`. If a build produces a
  `py3-none-any` wheel for a project with an extension, the extension was not
  built, and installing the wheel will fail to import it.
* A source distribution is a separate question from a wheel, and more so for
  compiled projects: a wheel builds fine from a tree whose sdist omits the `.c`
  files. Unpack the sdist somewhere else and build *that*.
