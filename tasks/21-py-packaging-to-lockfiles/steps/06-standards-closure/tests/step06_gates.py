#!/usr/bin/env python3
"""Step 06 gates: nothing left of distutils, and a repository that describes itself.

Emits one tab-separated `family<TAB>label<TAB>ok<TAB>detail` line per check,
the same contract the earlier steps' gate scripts have with `test.sh`. Every
row here is recorded against family `06`; the earlier steps' rows are re-run
from their own gate scripts and keep families `01`..`05`.

Nothing here builds anything and nothing here executes candidate code. The
wheels have already been built by `test.sh`, unprivileged, and are read here as
archives; the tree is read as text and as ASTs.

Eight groups, weighed by `test.sh` on the label prefix:

    distutils.  3 rows   the root distribution's own code imports `distutils`
                         only where `DISTUTILS_KEPT` allows it, the subpackage
                         that existed only to serve it is gone, and the three
                         public names that bridge to it still exist
    setuppy.    1 row    no `setup.py` anywhere in the repository
    license.    2 rows   each pure distribution's wheel declares an SPDX
                         expression at core metadata 2.4, and ships the files
                         its `License-File:` fields name
    typed.      1 row    every package every wheel installs carries `py.typed`
    pylock.     1 row    the exported lock is current with `uv.lock`
    reqtxt.     1 row    and the older export format did not land beside it
    sbom.       2 rows   the repository carries a CycloneDX document, and it
                         validates against the schema of the version it declares
    twine.      1 row    `twine check` over the built artifacts exits 0

Two design notes that are measurements rather than taste, both from
`authoring/design/06-standards-closure.md` §7:

*The CycloneDX validator is chosen from the document.* `uv export --format
cyclonedx1.5` writes a 1.5 document and `cyclonedx-py environment` writes a 1.6
one by default, and the 1.6 one is INVALID against a 1.5 validator: the tool
components `cyclonedx-py` stamps into `metadata.tools` carry
`licenses[].license.acknowledgement`, a key 1.6 introduced into a license object
1.5 declares `additionalProperties: false`. A validator pinned to 1.5 therefore
rejects a correct document from a tool the task line itself names. So
`sbom.validates` reads `specVersion` and validates against that schema, and the
only version requirement is 1.5 or later.

*`pylock.exported` never compares group selections.* `uv export --format
pylock.toml` writes the root, its runtime dependencies and the `dev` group --
six packages out of `uv.lock`'s thirty-four -- because `dev` is the default
group and `test` and `doc` are not. Requiring that exact set would score uv's
default rather than the workspace, so the row checks staleness in both
directions instead: nothing in the export that is not in the lock, and nothing
in the root's runtime closure that is not in the export.

*The import scan is scoped to the root distribution, and the scope is computed.*
`other_member_dirs` reads `[tool.uv.workspace] members` out of the root
`pyproject.toml` and keeps the entries that declare their own `[project] name`.
On the pinned tree that is `yaku/` and `examples/simples/single_extension/`, and
it removes thirteen of the tree's fifty-six `distutils` sites. `yaku`'s thirteen
are `ccompiler.new_compiler`, `unixccompiler`, `command.build_ext`,
`distutils.dist` and `sysconfig.customize_compiler` -- the compiler driver, the
one corner PEP 632 hands to `setuptools._distutils` rather than to a
standard-library successor, and the one this task's inherited `escapes` row
forbids the tree to depend on. Writing `yaku/` down here instead would be
invented surface; computing it means a candidate who wants a wider exemption has
to widen the member list, which an inherited `workspace.` row already scores.
Measured: a stray `pyproject.toml` with a `[project] name` dropped into
`src/bento/` exempts nothing, because the directory is not in the member list.

Usage:

    step06_gates.py source    <tree>
    step06_gates.py artifacts <twine-rc> <twine-output-file> <wheeldir>...
"""
import ast
import fnmatch
import json
import os
import sys

sys.path.insert(0, "/tests/lib")
import gatelib  # noqa: E402

# Step 02's, imported rather than copied, for the reason steps 03, 04 and 05
# import them: `walk` is the definition of "the tree" that every migrated-ness
# row in this task already scans, `load_toml` is the one TOML reader, and
# `canonical` is PEP 503 name comparison. A second copy here would let this
# step disagree with the steps whose tree it inherits.
sys.path.insert(0, "/tests")
import step02_gates  # noqa: E402

walk = step02_gates.walk
load_toml = step02_gates.load_toml
canonical = step02_gates.canonical

# Step 05's, for the same reason: "the distributions in this workspace" is a
# whole-repo scan of `[project] name`, and this step must not invent a second
# answer to a question step 05 already answers.
import step05_gates  # noqa: E402

distributions = step05_gates.distributions
wheel_distribution = step05_gates.wheel_distribution

# Step 03's, for `resolve_members`: "the ones the root's workspace member list
# names" is the member list step 03 already resolves, globs and all.
import step03_gates  # noqa: E402

ROWS = []

# The names `bento/conv.py` exposes for the benefit of code that still speaks
# distutils. They are the anti-rename guard: an AST gate that only counted
# imports is passed by deleting the module that imports them, and the corpus
# notices only because `api_conv` calls them by name. The row says out loud
# what the corpus says by accident.
BRIDGE_NAMES = ("pkg_to_distutils",
                "pkg_to_distutils_meta",
                "pkg_to_distutils_meta_pkg_info")

# The tree whose removal this step requires, as a path suffix rather than as a
# path: step 02 moved the package under `src/` and a later answer may move it
# again, so what is fixed is the subpackage's dotted position inside its
# package, not its position in the checkout.
#
# One entry, and it was two. `bento/compat/dist.py` was on this list until the
# reference solution was written against it, and it came off for a reason that
# is a fact about the repository and not a preference:
# `src/bentomakerlib/tests/test_bentomaker.py:35` imports `DistributionMetadata`
# from it, and that module has nineteen passing tests (NOTES section 4). Rule 6
# says the repository's own tests are untouched, so a requirement that makes
# nineteen of them uncollectable and offers no way to fix them except editing a
# test file is a requirement the candidate cannot satisfy cleanly. Nothing is
# lost by dropping it: the seven `distutils` imports inside that module are
# scored by `distutils.imports-gone` like every other site, and what the module
# has to become -- a PKG-INFO writer that no longer borrows
# `distutils.dist.DistributionMetadata` and still writes the same bytes -- is
# held to the corpus by `api_conv`, which renders `pkg_to_distutils_meta_pkg_info`
# on every one of its cases. Deleting it remains an allowed answer; it is no
# longer a required one.
#
# `bento/distutils` stays, and its own `tests/` go with it. The subpackage
# subclasses four `distutils` commands and exists for no other purpose, so there
# is nothing in it to port to; its five test modules test the subclasses, three
# of them already fail on the pinned tree, and a `tests/` directory whose
# subject has been deleted is not a test suite.
DEAD_TREE_SUFFIXES = ("bento/distutils",)

# What `distutils.imports-gone` does not scan, and why each one is here:
#   other members   the distributions this workspace publishes that are not the
#                   root. Measured 2026-09-17 on the step-05 tree: `yaku` has 13
#                   sites in 2 modules -- `unixccompiler`, `ccompiler`,
#                   `command.build_ext`, `dist`, `errors` and `sysconfig`, the
#                   last of them four times. The stdlib answers `sysconfig`:
#                   `get_python_inc` and `get_config_var` have public successors.
#                   Nothing answers `ccompiler.new_compiler` or
#                   `sysconfig.customize_compiler`, which is the compiler driver
#                   PEP 632 moved to `setuptools._distutils` and gave no stdlib
#                   successor at all. Porting it means writing a compiler
#                   abstraction,
#                   which is a different project, so the milestone is scoped to
#                   the root distribution and the instruction says so in those
#                   words. The set is computed, never named: a directory is
#                   exempt when the root's `[tool.uv.workspace] members` lists
#                   it AND it carries its own `[project] name`. An answer cannot
#                   widen it without editing the member list, which step 03's
#                   `workspace` row already scores.
#   examples/       the `examples/simples` trees that are not distributions are
#                   fixtures; `setuppy.gone` is what covers their `setup.py`
#   */tests/*       a test that exercises the conversion path has to import
#                   what it converts from; the repo's own tests are untouched
#                   by rule 6 in any case
#   vendored        `_yaku` and friends under any path; step 03 removed the
#                   vendored copies and step 06 must not re-score their absence
EXEMPT_DIR_NAMES = ("tests",)
EXEMPT_PREFIXES = ("examples/",)
VENDOR_MARKERS = ("_yaku", "_ply", "_six", "_simplejson")

DISTUTILS_MODULES = ("distutils",)
DISTUTILS_DOTTED = ("setuptools._distutils",)

# The three modules that keep importing distutils, and the submodules each one
# is allowed. PEP 632 removed distutils from the interpreter; it did not remove
# it from the world, and `setuptools` still vendors it. So the milestone is not
# "the string distutils appears nowhere" -- it is "nothing bento uses to build
# or describe *itself* depends on a module the interpreter no longer ships".
# Three modules fall on the other side of that line, and they are named here
# and in the instruction rather than discovered:
#
#   convert/core.py           reads somebody else's `setup.py`. Its whole job is
#                             to execute a foreign distutils script and record
#                             what it passed to `setup()`; a converter that
#                             cannot import what it converts from converts
#                             nothing. Measured on the pristine tree: 6 sites:
#                             `distutils.core.setup`,
#                             `distutils.command.build_py.build_py`,
#                             `distutils.command.sdist.sdist`,
#                             `distutils.dist.Distribution` twice and
#                             `distutils.filelist.FileList`. The two
#                             `distutils.command` submodules are named
#                             individually rather than allowed as a package,
#                             for the reason the subset-test paragraph below
#                             gives.
#   commands/build_distutils.py
#                             *is* the Distutils build backend -- the one
#                             `UseBackends: Distutils` selects. Porting it off
#                             distutils does not migrate it, it renames it; the
#                             default backend is yaku and is already clean.
#   commands/wininst_utils.py `distutils.msvccompiler.get_build_version`, and
#                             nothing else. The other two sites in this module
#                             -- `distutils.util.get_platform` and
#                             `distutils.sysconfig.get_python_version` -- have
#                             stdlib successors in `sysconfig` and must go. This
#                             one has none, and the repository's own
#                             `test_wininst_utils.py` patches it by that dotted
#                             name, so removing it would break a test rule 6
#                             forbids touching.
#
# The allowance is a subset test, not an equality test: dropping one of these
# imports passes, adding a submodule that is not listed fails. That is what
# stops the obvious dodge of moving `configure.py`'s `INSTALL_SCHEMES` import
# into an allowlisted module instead of porting it -- and it only stops it
# because `convert/core.py`'s two `distutils.command` submodules are listed one
# by one. Written as the package prefix `distutils.command`, as this table was
# until the round-1 review of this step, the allowance covered
# `distutils.command.install` too, and moving that import here rather than
# porting it would have passed. `setuptools._distutils` is allowed nowhere,
# including here -- it is the same removed code under the name setuptools
# vendored it as.
#
# The allowance is also about imports and nothing else, which is a narrower
# claim than it reads as. `convert/core.py` may import `distutils.dist`; the
# name it takes from there is `Distribution`. Nothing stops a tree from taking
# `DistributionMetadata` from the same module and re-exporting it from
# `bento/compat/dist.py`, and every row in this file would still pass, because
# every row in this file reads imports. That dodge is scored at runtime
# instead, by `distutils.meta-ported` in `test.sh`, which asks an installed
# copy of the distribution where its PKG-INFO class was defined.
#
# `convert/utils.py` is absent on purpose. Its distutils imports live inside
# the triple-quoted probe snippets it writes to a temporary `setup.py` and runs
# in a subprocess, so an AST scan of the module sees none of them, and the gate
# must not pretend to score a string.
DISTUTILS_KEPT = {
    "src/bento/convert/core.py": ("distutils.core", "distutils.dist",
                                  "distutils.command.build_py",
                                  "distutils.command.sdist",
                                  "distutils.filelist"),
    "src/bento/commands/build_distutils.py": ("distutils.dist", "distutils.errors",
                                              "distutils.extension", "distutils.log"),
    "src/bento/commands/wininst_utils.py": ("distutils.msvccompiler",),
}


def kept(rel, name):
    """True when `rel` is allowed to import `name` by DISTUTILS_KEPT."""
    allowed = DISTUTILS_KEPT.get(rel)
    if allowed is None:
        return False
    return any(name == a or name.startswith(a + ".") for a in allowed)


def bound_import_sites(tree, needles):
    """Import-like calls whose module name arrives through a variable.

    `gatelib.dynamic_import_sites` reads the literal spelling,
    `import_module("distutils.util")`. `gatelib.opaque_import_sites` reads the
    calls assembled out of literals, `import_module(".".join(["distutils",
    "util"]))`, by looking for the needle among the string constants the call
    is built from. Between them sits the spelling neither reads, and it is the
    one a candidate reaches for first because it is the one an ordinary
    programmer writes:

        name = "distutils.util"
        mod = importlib.import_module(name)

    This resolves exactly that: a name bound once, in the same file, to a
    string constant, used as the first argument of an import-like call. One
    binding and one only -- a name assigned twice, or in a loop, or arriving as
    a function argument, is not resolved and is not claimed to be. Those are
    already reported by `opaque_import_sites` whenever the needle appears among
    the call's own constants, and where it does not, no static reading of the
    file can say what module is imported.

    This lives here rather than in `gatelib` because step 06 is the only step
    whose verdict is "nothing anywhere imports this", which is what makes the
    indirection worth resolving; the earlier steps' AST rows ask what a tree
    declares, and a declaration cannot be written through a variable.

    `gatelib._import_call_name` is the same four-name matcher the two public
    scans use, and calling it is deliberate: a second copy of that list here
    would be a second definition of which calls count as an import.
    """
    parts = tuple(needles)
    out = []
    for path in gatelib.iter_sources(tree):
        rel = gatelib.relpath(path, tree)
        mod, _error = gatelib.parse_source(path)
        if mod is None:
            continue
        bound = {}
        for node in ast.walk(mod):
            if not isinstance(node, ast.Assign):
                continue
            if not (isinstance(node.value, ast.Constant)
                    and isinstance(node.value.value, str)):
                continue
            for target in node.targets:
                if isinstance(target, ast.Name):
                    bound.setdefault(target.id, []).append(node.value.value)
        for node in ast.walk(mod):
            if gatelib._import_call_name(node) is None or not node.args:
                continue
            first = node.args[0]
            if not isinstance(first, ast.Name):
                continue
            values = bound.get(first.id) or []
            if len(values) != 1:
                continue
            name = values[0]
            if any(name == part or name.startswith(part + ".")
                   for part in parts):
                out.append((rel, node.lineno, name))
    return sorted(set(out))


def bound_callee_sites(tree, needles):
    """Import-like calls where the *function* arrives through a variable.

    `bound_import_sites` above resolves the module name when it is written one
    line earlier. This resolves the other half of the same indirection, the one
    where the module name is written out in full and it is the import function
    that is bound:

        _i = importlib.import_module
        _i("distutils.dist")

    `gatelib._import_call_name` reads a call's own spelling and `_i` is not one
    of the four names it knows, so without this pass those two lines are an
    import nothing in this file reports. Three ways to bind it, each of them one
    line: the attribute (`_i = importlib.import_module`), the name the file
    already imported (`from importlib import import_module`, then `_i =
    import_module`), and the import statement's own alias (`from importlib
    import import_module as _i`).

    `bound_import_sites`' conventions, for the reasons it gives there: one
    binding and one only -- a name assigned twice has no single value and is not
    claimed to have one, and that holds however the second binding is spelled
    and whatever it binds -- and a callee `gatelib`'s matcher already reads is
    left to it, so that a file which binds `import_module` to itself produces
    one finding per line rather than two.

    Returns `(resolved, computed)`, which is the split the two public scans make
    between them. A literal first argument is the module name and comes back as
    one, so that a caller's allowlist can be tested against it; anything else
    comes back with the needle-bearing constants the call is built out of,
    because a name the AST cannot read is not a name an allowlist says anything
    about.
    """
    parts = tuple(needles)
    resolved, computed = [], []
    for path in gatelib.iter_sources(tree):
        rel = gatelib.relpath(path, tree)
        mod, _error = gatelib.parse_source(path)
        if mod is None:
            continue
        bound = {}
        # Every binding of a name is recorded here, the ones that bind something
        # other than an import function included -- those as None. What makes a
        # callee resolvable is that the file binds it once and once only, so a
        # rebinding has to be visible in this pass even when its value is an
        # ordinary function: a pass that collected only the import-flavoured
        # assignments would read `_i = importlib.import_module` followed by
        # `_i = shutil.which` as a name bound once, and would fault the call
        # below it for an import it does not perform.
        for node in ast.walk(mod):
            if isinstance(node, (ast.Import, ast.ImportFrom)):
                for alias in node.names:
                    if not alias.asname:
                        continue
                    call = (alias.name
                            if isinstance(node, ast.ImportFrom) else None)
                    bound.setdefault(alias.asname, []).append(
                        call if call in gatelib.IMPORT_CALL_NAMES else None)
            elif isinstance(node, ast.Assign):
                value = node.value
                if isinstance(value, ast.Attribute):
                    call = value.attr
                elif isinstance(value, ast.Name):
                    call = value.id
                else:
                    call = None
                for target in node.targets:
                    if isinstance(target, ast.Name):
                        bound.setdefault(target.id, []).append(
                            call if call in gatelib.IMPORT_CALL_NAMES else None)
        for node in ast.walk(mod):
            if not isinstance(node, ast.Call) or not node.args:
                continue
            func = node.func
            if not isinstance(func, ast.Name) \
                    or func.id in gatelib.IMPORT_CALL_NAMES:
                continue
            values = bound.get(func.id) or []
            if len(values) != 1 or values[0] is None:
                continue
            first = node.args[0]
            if isinstance(first, ast.Constant) and isinstance(first.value, str):
                if any(first.value == part or first.value.startswith(part + ".")
                       for part in parts):
                    resolved.append((rel, node.lineno, first.value))
                continue
            found = sorted(set(
                child.value for child in ast.walk(node)
                if isinstance(child, ast.Constant)
                and isinstance(child.value, str)
                and any(part in child.value for part in parts)))
            if found:
                computed.append((rel, node.lineno,
                                 "%s(<computed>) over %r"
                                 % (bound[func.id][0], found[:4])))
    return sorted(set(resolved)), sorted(set(computed))


def row(label, ok, detail=""):
    # Every whitespace run collapses to one space, and not just tabs the way
    # steps 01..05 spell this. Those steps build their details out of paths and
    # names they read themselves; this one quotes third-party error text --
    # `jsonschema`'s `best_match` message is several lines long -- and a
    # newline inside a detail ends the TSV record early. `rows_from_gates`
    # reads the continuation as a fresh `family/label/ok/detail`, records a row
    # under a label made of prose, and that label can land inside a
    # `group_fraction` prefix. Measured on fixture tree K, whose second SBOM
    # document fails with "'name' is a required property" plus four lines of
    # schema context: before this line the gate emitted four phantom rows.
    ROWS.append(("06", label, "1" if ok else "0",
                 " ".join(str(detail).split())[:300]))


def flush():
    for family, label, ok, detail in ROWS:
        sys.stdout.write("\t".join((family, label, ok, detail)) + "\n")


# --------------------------------------------------------------------------
# the removal
# --------------------------------------------------------------------------

def other_member_dirs(tree):
    """Directories belonging to a workspace member that is not the root.

    Two conditions, both read off recorded surface: the root's
    `[tool.uv.workspace] members` lists the directory, and the directory carries
    a `pyproject.toml` with a `[project] name`. Requiring both is what stops the
    exemption from being an evasion -- a stray `pyproject.toml` dropped into
    `src/bento/` exempts nothing unless the member list names it too, and the
    member list is what step 03's `workspace` row reads.

    The member list is resolved by step 03's own `resolve_members`, so
    `members = ["packages/*"]` or `["./packages/yaku"]` names the same
    directories here as it does to the inherited `workspace.` rows: entries are
    globs relative to the root, `exclude` is honoured, and each hit is
    normalised to the walk-relative spelling the exemption is compared against.
    Read literally, a glob or a `./` spelling exempted nothing, and a tree that
    left the member's sources alone, as the instruction says, lost the import
    row. (Wave D, 2026-10-12, final-review C2.)
    """
    data, _note = load_toml(os.path.join(tree, "pyproject.toml"))
    table = (((data or {}).get("tool") or {}).get("uv") or {}).get("workspace")
    if not isinstance(table, dict):
        return []
    resolved, _faults = step03_gates.resolve_members(tree, table)
    out = set()
    for _name, rel, _absdir in resolved:
        rel = os.path.normpath(rel).replace(os.sep, "/").strip("/")
        if not rel or rel == "." or rel == ".." or rel.startswith("../"):
            continue
        out.add(rel + "/")
    return sorted(out)


def exempt(rel, members=()):
    """True for a path `distutils.imports-gone` does not read.

    Written as one function rather than four filters so that every caller
    agrees on the exemption set: the gate, the detail string that reports how
    much was scanned, and the fixture that has to know which files it may edit.
    """
    parts = rel.split("/")
    if any(rel.startswith(m) for m in members):
        return True
    if any(rel.startswith(p) for p in EXEMPT_PREFIXES):
        return True
    if any(part in EXEMPT_DIR_NAMES for part in parts[:-1]):
        return True
    # Directory components only, `parts[:-1]`, which is what `gate_trees_gone`
    # spells out at its own marker test and what instruction line 95 says: "any
    # vendored third-party **tree**". Read over the basename too, the markers
    # exempt a file for the name somebody gave it: `_yaku` is a substring of
    # `src/bento/commands/build_yaku.py`, which is this repository's default
    # build backend and is shipped by the root distribution's own `packages`
    # list. The round-1 verification planted `from distutils.command.install
    # import INSTALL_SCHEMES` in that file, consumed it from `configure.py`, and
    # the row stayed at 1 with "0 sites".
    if any(marker in part for part in parts[:-1] for marker in VENDOR_MARKERS):
        return True
    return False


def gate_imports_gone(tree):
    """Nothing outside the documented allowance imports distutils.

    `DISTUTILS_KEPT` above names the three modules that may still import it and
    the submodules each one is allowed; every other module in the root
    distribution must be clean, by any spelling.

    Four spellings, because an AST gate that reads only `import` statements is
    defeated by one line of `importlib` and a gate that reads only module scope
    is defeated by an indent:

      * `import distutils.x` and `from distutils.x import y`, at any depth --
        `gatelib.import_sites` walks the whole AST, so a function-local or
        `try`-wrapped import is the same site as a top-level one;
      * `setuptools._distutils`, which is the same code under the name
        `setuptools` vendored it as, and is what PEP 632's own migration advice
        sends a reader to;
      * `__import__("distutils")` / `importlib.import_module("distutils.x")`
        with a literal name, by either spelling of the call -- the attribute on
        `importlib` and the bare name a `from importlib import import_module`
        binds are the same call;
      * the same call reached through a variable bound once to a string
        constant in the same file, which `bound_import_sites` resolves;
      * and the mirror of that one, where the name is written out and it is the
        import function that is bound to a variable, which
        `bound_callee_sites` resolves;
      * an import-like call whose argument is computed, which cannot be
        resolved and is reported rather than resolved.

    A file that does not parse is counted and reported, never faulted: this
    repository carries Python 2 modules that raise `SyntaxError` on this
    interpreter, and a file the interpreter cannot read imports nothing. The
    instruction says so in the same words.
    """
    members = other_member_dirs(tree)
    sites, unparsable = gatelib.import_sites(tree)
    hits = [(rel, line, name)
            for rel, line, name in gatelib.sites_importing(sites,
                                                           DISTUTILS_MODULES)
            if not exempt(rel, members) and not kept(rel, name)]
    hits += [(rel, line, name) for rel, line, name in sites
             if not exempt(rel, members)
             and any(name == d or name.startswith(d + ".")
                     for d in DISTUTILS_DOTTED)]
    # `gatelib.literal_import_sites` rather than `dynamic_import_sites`, and the
    # finding is the module name rather than the call as written. The reason is
    # the one this row is built on: the allowance is stated in module names, so
    # `kept()` tests module names, and a `dynamic_import_sites` finding is the
    # string `import_module('distutils.dist')` -- which is not equal to
    # `distutils.dist` and does not start with `distutils.dist.`, so `kept()`
    # answered False for every literal call in every keeper. A test against a
    # formatted string is not a test at all, and what it cost was a file the
    # instruction grants `distutils.dist` in as many words being faulted for
    # importing `distutils.dist`. `bound_import_sites` above already reports
    # module names for the same reason; this makes the two spellings of one
    # indirection agree rather than leaving them to differ.
    #
    # The needle test is `kept()`'s own prefix test rather than
    # `dynamic_import_sites`' top-level-package one, which is what lets both
    # needle sets be needles here: `top_level("setuptools._distutils.core")` is
    # `setuptools`, in nobody's needle set, so the vendored copy reached by a
    # plain literal call was seen by no scan in this gate at all. `kept()` is
    # applied to both sets and is right for both, for the reason the
    # `bound_callee_sites` call below gives in full: no file's allowance names
    # `setuptools._distutils`.
    hits += [(rel, line, mod)
             for rel, line, mod, _call in gatelib.literal_import_sites(tree)
             if any(mod == n or mod.startswith(n + ".")
                    for n in DISTUTILS_MODULES + DISTUTILS_DOTTED)
             and not exempt(rel, members) and not kept(rel, mod)]
    hits += [(rel, line, name)
             for rel, line, name in bound_import_sites(tree, DISTUTILS_MODULES)
             if not exempt(rel, members) and not kept(rel, name)]
    hits += [(rel, line, name)
             for rel, line, name in bound_import_sites(tree, DISTUTILS_DOTTED)
             if not exempt(rel, members)]
    # One call for both needle sets, unlike the two above, because the two
    # readings this one returns are already the two the row treats differently.
    # `kept()` is applied to the resolved half whichever needle matched, and it
    # is right for both: no file's allowance names `setuptools._distutils`, so
    # `kept` answers False for every one of those names in every file.
    callee_hits, callee_opaque = bound_callee_sites(
        tree, DISTUTILS_MODULES + DISTUTILS_DOTTED)
    hits += [(rel, line, name) for rel, line, name in callee_hits
             if not exempt(rel, members) and not kept(rel, name)]
    # The keepers are not exempt from this one. `DISTUTILS_KEPT` is a subset
    # test over names the AST can read; a computed name is a name the AST
    # cannot read, so there is nothing to test it against and "this file is
    # allowed four submodules" says nothing about it. Reported, as everywhere
    # else here, rather than resolved.
    opaque = [(rel, line, name)
              for rel, line, name in gatelib.opaque_import_sites(
                  tree, DISTUTILS_MODULES)
              if not exempt(rel, members)]
    opaque += [(rel, line, name) for rel, line, name in callee_opaque
               if not exempt(rel, members)]
    hits = sorted(set(hits))
    opaque = sorted(set(opaque))
    survivors = sorted(set(rel for rel, _, name in
                           gatelib.sites_importing(sites, DISTUTILS_MODULES)
                           if not exempt(rel, members) and kept(rel, name)))
    counts = ("%d imports scanned, %d files unparsable, %d member dirs exempt, "
              "%d of %d documented keepers still importing"
              % (len(sites), len(unparsable), len(members),
                 len(survivors), len(DISTUTILS_KEPT)))
    found = ["%s:%d %s" % h for h in hits] \
        + ["%s:%d %s (computed)" % h for h in opaque]
    detail = ("%d sites (%s)" % (len(found), counts) if not found
              else "still importing distutils: %s [%s]"
                   % (", ".join(found[:6]), counts))
    row("distutils.imports-gone", not found, detail)


def gate_trees_gone(tree):
    """The subpackage that existed only to serve distutils.

    Matched on the dotted tail of the path rather than on a fixed path, because
    step 02 moved the package under `src/` and nothing forbids a later answer
    moving it again. A candidate who empties the directory but leaves it, or who
    leaves an `__init__.py` behind, has not removed the subpackage: any file
    under the suffix counts.

    `exempt()` is deliberately not used here, and the difference is
    `bento/distutils/tests/`. The import scan skips every `tests/` directory
    because a test that exercises the conversion path has to import what it
    converts from. That reasoning does not reach this row: a `tests/` directory
    inside the dead tree is part of the dead tree, it tests four `distutils`
    command subclasses that will not exist, and a candidate who deleted the
    subpackage and kept its test package would pass a row that says the
    subpackage is gone while `src/bento/distutils/tests/test_build.py` sits in
    the checkout. The other workspace members are still skipped -- the milestone
    is the root distribution and only the root distribution -- and so is
    anything vendored, on the same footing as the import scan.
    """
    members = other_member_dirs(tree)
    left = []
    for path in walk(tree):
        rel = gatelib.relpath(path, tree)
        if any(rel == m.rstrip("/") or rel.startswith(m) for m in members):
            continue
        if any(marker in rel.split("/") for marker in VENDOR_MARKERS):
            continue
        for suffix in DEAD_TREE_SUFFIXES:
            if rel == suffix or ("/" + rel).endswith("/" + suffix) \
                    or ("/" + rel + "/").find("/" + suffix + "/") >= 0:
                left.append(rel)
    left = sorted(set(left))
    row("distutils.trees-gone", not left,
        "still in the tree: " + ", ".join(left[:8]) if left
        else "nothing anywhere under %s, its own tests included"
             % DEAD_TREE_SUFFIXES[0])


def gate_setuppy_gone(tree):
    """No file named `setup.py`, anywhere, with no exemptions.

    The instruction has no exemptions here and neither does the gate: the three
    `examples/simples` files are in scope, which is the whole content of
    `s6-setuppy-left`.
    """
    left = sorted(gatelib.relpath(p, tree) for p in walk(tree)
                  if os.path.basename(p) == "setup.py")
    row("setuppy.gone", not left,
        "still in the tree: " + ", ".join(left[:8]) if left
        else "no setup.py anywhere in the repository")


def gate_public_names(wheels):
    """The three bridge names are still defined, together, in one module.

    Read out of the built wheel and not out of the tree, so that a module the
    candidate stopped shipping counts as gone; and searched over every module
    the wheel carries rather than at a fixed path, so that moving `conv.py`
    inside the package is not a failure. "From the same module" is the
    requirement the instruction states and is what makes this an anti-rename
    guard rather than a spell-check: three module-level `def`s of those names,
    in one file.
    """
    import ast

    wanted = set(BRIDGE_NAMES)
    found_in = []
    scanned = 0
    for wheel in wheels:
        for name in gatelib.wheel_names(wheel):
            if not name.endswith(".py"):
                continue
            scanned += 1
            try:
                mod = ast.parse(gatelib.wheel_read(wheel, name))
            except Exception:
                continue
            here = set()
            for node in mod.body:
                if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) \
                        and node.name in wanted:
                    here.add(node.name)
                elif isinstance(node, ast.Assign):
                    for target in node.targets:
                        if isinstance(target, ast.Name) and target.id in wanted:
                            here.add(target.id)
            if here == wanted:
                found_in.append("%s:%s" % (os.path.basename(wheel), name))
    row("distutils.public-names-kept", bool(found_in),
        "%s defines all of %s" % (found_in[0], ", ".join(sorted(wanted)))
        if found_in else
        "no module in %d scanned defines all of %s at module level"
        % (scanned, ", ".join(sorted(wanted))))


# --------------------------------------------------------------------------
# the description: what a built artifact says
# --------------------------------------------------------------------------

def metadata_of(wheel):
    """The wheel's `.dist-info/METADATA`, parsed. (message, error).

    `gatelib.wheel_metadata` hands back the triple `(METADATA, WHEEL, names)`;
    only the first is wanted here, and a wheel with no `.dist-info/METADATA`
    at all comes back as `None` rather than as an exception, so that case is
    turned into an error string here and not left to fault later.
    """
    try:
        message, _wheel, _names = gatelib.wheel_metadata(wheel)
    except Exception as exc:
        return None, "%s: %s" % (type(exc).__name__, exc)
    if message is None:
        return None, "no .dist-info/METADATA in the archive"
    return message, None


def metadata_version(msg):
    try:
        return tuple(int(p) for p in
                     (msg.get("Metadata-Version") or "0").split("."))
    except Exception:
        return (0,)


# The three rows below read artifacts, and on 2026-09-18 the six-step nop control
# showed what that costs when there are none: nothing was done, nothing built, the
# wheel list arrived empty, every `for wheel in ...` body ran zero times, `not
# faults` was True, and `license.expression`, `license.files` and `typed.marker` all
# scored -- 27 of this step's 163 weight, 16.6%, for an untouched tree. A gate that
# reads artifacts has to fail when handed none.
#
# The floor is a count and not a list of names, for the reason the call site's own
# comment gives: which wheels are the pure ones is read off their compatibility
# tags, so a fourth distribution a candidate adds is still held to whichever half of
# this step its tag puts it in. These two numbers only assert that the
# distributions this workspace already builds did not go missing -- the two pure
# ones step 04 scores, and those two plus the extension, which is exactly the "All
# three of them" this step's instruction names.
PURE_DISTRIBUTIONS = 2
ALL_DISTRIBUTIONS = 3


def gate_license_expression(pure_wheels):
    """PEP 639, read out of the artifact and never out of `pyproject.toml`.

    Design §4 Q3 measured `validate-pyproject` calling both the pre-639 table
    form and the 639 expression form clean, so a source-side licence row would
    pass on an unmigrated tree. What cannot be faked is what the back end put in
    METADATA: `Metadata-Version` at 2.4 or later, a `License-Expression` that
    `packaging` canonicalises, and no `License` field, which 639 makes mutually
    exclusive with the expression.
    """
    from packaging.licenses import canonicalize_license_expression

    faults = []
    seen = []
    if len(pure_wheels) < PURE_DISTRIBUTIONS:
        faults.append("%d pure distribution(s) to read, and this workspace "
                      "builds %d -- a row that reads METADATA out of a wheel "
                      "cannot pass over an empty set"
                      % (len(pure_wheels), PURE_DISTRIBUTIONS))
    for name, wheel in sorted(pure_wheels.items()):
        msg, error = metadata_of(wheel)
        if msg is None:
            faults.append("%s: %s" % (name, error))
            continue
        mv = metadata_version(msg)
        expr = msg.get("License-Expression")
        legacy = msg.get("License")
        if mv < (2, 4):
            faults.append("%s declares Metadata-Version %s, below 2.4"
                          % (name, msg.get("Metadata-Version")))
        if not expr:
            faults.append("%s carries no License-Expression" % name)
        else:
            try:
                seen.append("%s=%s" % (name,
                                       canonicalize_license_expression(expr)))
            except Exception as exc:
                faults.append("%s: %r is not an SPDX expression (%s)"
                              % (name, expr, exc))
        if legacy:
            faults.append("%s still carries the free-text License: %r"
                          % (name, legacy))
    row("license.expression", not faults,
        "; ".join(faults[:4]) if faults
        else "%d distributions, %s" % (len(pure_wheels), ", ".join(seen)))


def gate_license_files(pure_wheels):
    """`License-File:` names a file, and the file is in the wheel.

    A declaration that names a file the archive does not contain is the claim
    risk this half of the step is about: the METADATA is what an index reads and
    the wheel is what an installer unpacks, and PEP 639 puts the files under
    `.dist-info/licenses/`.

    Two spellings of the field are in circulation and both name that same
    place. `hatchling` writes the file's own path as the value and puts the
    file under `.dist-info/licenses/`; `setuptools` writes the path relative to
    the `.dist-info` directory, which for the same file is
    `licenses/LICENSE.txt`. A row that accepted only the first would be scoring
    which backend wrote the wheel, and this step does not choose the backend.
    """
    faults = []
    counted = []
    if len(pure_wheels) < PURE_DISTRIBUTIONS:
        faults.append("%d pure distribution(s) to read, and this workspace "
                      "builds %d -- a row that reads a wheel's members cannot "
                      "pass over an empty set"
                      % (len(pure_wheels), PURE_DISTRIBUTIONS))
    for name, wheel in sorted(pure_wheels.items()):
        msg, error = metadata_of(wheel)
        if msg is None:
            faults.append("%s: %s" % (name, error))
            continue
        declared = [v for v in (msg.get_all("License-File") or []) if v]
        if not declared:
            faults.append("%s carries no License-File" % name)
            continue
        members = set(gatelib.wheel_names(wheel))
        dist_info = sorted({m.split("/", 1)[0] for m in members
                            if m.split("/", 1)[0].endswith(".dist-info")})
        for value in declared:
            wanted = ["%s/licenses/%s" % (d, value) for d in dist_info] \
                + ["%s/%s" % (d, value) for d in dist_info]
            if not any(w in members for w in wanted):
                faults.append("%s declares License-File %r and the wheel has "
                              "neither %s nor %s"
                              % (name, value,
                                 wanted[0] if dist_info
                                 else "*.dist-info/licenses/%s" % value,
                                 wanted[len(dist_info)] if dist_info
                                 else "*.dist-info/%s" % value))
        counted.append("%s=%d" % (name, len(declared)))
    row("license.files", not faults,
        "; ".join(faults[:4]) if faults
        else "every declared License-File is inside its wheel (%s)"
             % ", ".join(counted))


def gate_typed_marker(wheels):
    """PEP 561, in every package of every wheel.

    Every distribution, not the two that get it for free: design §4 Q8 measured
    hatchling shipping a `py.typed` that is in the source tree and
    scikit-build-core not shipping it, so a candidate who adds the file to all
    three trees and rebuilds has done two thirds of the work. The row reads the
    archive.

    Every *package*, not one of them: `bento`'s wheel installs `bento` and
    `bentomakerlib`, and a consumer importing the second learns nothing from a
    marker in the first.
    """
    faults = []
    counted = []
    distributions = sorted({wheel_distribution(w) or os.path.basename(w)
                            for w in wheels})
    if len(distributions) < ALL_DISTRIBUTIONS:
        faults.append("%d distribution(s) built %d wheel(s), and this workspace "
                      "has %d -- a row that reads archive members cannot pass "
                      "over an empty set"
                      % (len(distributions), len(wheels), ALL_DISTRIBUTIONS))
    for wheel in sorted(wheels):
        members = list(gatelib.wheel_names(wheel))
        tops = sorted({m.split("/", 1)[0] for m in members
                       if "/" in m
                       and not m.split("/", 1)[0].endswith(".dist-info")
                       and not m.split("/", 1)[0].endswith(".data")})
        if not tops:
            faults.append("%s installs no package directory"
                          % os.path.basename(wheel))
            continue
        for top in tops:
            if "%s/py.typed" % top not in members:
                faults.append("%s: %s/py.typed is not in the wheel"
                              % (os.path.basename(wheel), top))
        counted.append("%s[%s]" % (os.path.basename(wheel).split("-")[0],
                                   " ".join(tops)))
    row("typed.marker", not faults,
        "; ".join(faults[:4]) if faults
        else "py.typed in every package of %s" % ", ".join(counted))


def gate_twine(rc, output):
    """`twine check` over everything the build produced, exit 0.

    A weight-1 guard and not a scored row: design §4 Q5 measured the step-05
    tree already passing, so this cannot distinguish a finished step from an
    unfinished one. It is here because the step edits the metadata `twine check`
    reads, and a guard that was green before the step and red after it is
    exactly the regression the rest of this group cannot see.
    """
    # The tail and not the head: `twine check` prints one PASSED line per
    # artifact and then says what went wrong, so the first 200 characters of a
    # six-artifact run are six PASSED lines and no diagnosis.
    row("twine.clean", rc == 0,
        "twine check exit 0" if rc == 0
        else "twine check exit %s: %s" % (rc, (output or "")[-200:]))


# --------------------------------------------------------------------------
# the description: what the repository carries
# --------------------------------------------------------------------------

def read_json(path, cap=8 * 1024 * 1024):
    try:
        if os.path.getsize(path) > cap:
            return None
        with open(path, "rb") as fh:
            return json.loads(fh.read().decode("utf-8", "replace"))
    except Exception:
        return None


def find_sboms(tree):
    """Every CycloneDX document in the tree, by content and never by name.

    CycloneDX standardises the document, not where it lives, so a row that
    required `sbom.json` at the repository root would be inventing a required
    file layout -- which rule 1 names outright. The discriminator is
    `bomFormat == "CycloneDX"`, wherever the file is and whatever it is called;
    `s6-sbom-moved` is the fixture that keeps the scan honest.
    """
    out = []
    for path in walk(tree):
        if not path.endswith(".json"):
            continue
        data = read_json(path)
        if isinstance(data, dict) and data.get("bomFormat") == "CycloneDX":
            out.append((gatelib.relpath(path, tree), data))
    return sorted(out)


def spec_version(data):
    raw = str(data.get("specVersion") or "")
    try:
        return tuple(int(p) for p in raw.split(".")), raw
    except Exception:
        return (0,), raw


def gate_sbom_present(tree):
    found = find_sboms(tree)
    good = [(rel, data) for rel, data in found
            if spec_version(data)[0] >= (1, 5)]
    if not found:
        detail = "no file in the repository declares bomFormat CycloneDX"
    elif not good:
        detail = "found %s at specVersion %s, below 1.5" % (
            found[0][0], spec_version(found[0][1])[1])
    else:
        detail = "%s, CycloneDX %s, %d components" % (
            good[0][0], spec_version(good[0][1])[1],
            len(good[0][1].get("components") or []))
    row("sbom.present", bool(good), detail)
    return good


def component_versions(data):
    """canonical component name -> the set of versions the document gives it.

    A set rather than a string because `cyclonedx-py environment` lists an
    editable workspace member twice -- once for the `.pth` and once for the
    `.dist-info` -- and two entries for one name at one version is not a
    disagreement.
    """
    out = {}
    for comp in data.get("components") or []:
        name = comp.get("name")
        if isinstance(name, str) and name:
            out.setdefault(canonical(name), set()).add(
                comp.get("version") if isinstance(comp.get("version"), str)
                else None)
    return out


def same_version(declared, locked):
    """Version equality that tolerates spelling, and nothing else.

    `1.0` and `1.0.0` are one release written two ways and a row that called
    them different would be scoring a normalisation rather than a lock. When
    either side is not a version `packaging` can read, they are compared as the
    strings they are.
    """
    if declared is None or locked is None:
        return True
    try:
        from packaging.version import Version
        return Version(declared) == Version(locked)
    except Exception:
        return str(declared) == str(locked)


def source_kind(pkg):
    """The single key of a `uv.lock` entry's `source` table, or None.

    `registry`, `editable`, `directory`, `git`, `url`: uv writes exactly one of
    them for every package it locks, and which one it is is the fact
    `gate_pylock` needs in order to tell an export that may leave a version out
    from one that simply did not write it down.

    None when there is no `source` table, or when it holds something other than
    one key. An unknown source is not evidence of anything, and the caller
    treats it as such: reading a missing one as a registry would fail a correct
    tree, which is the expensive direction to be wrong in.
    """
    source = pkg.get("source")
    if isinstance(source, dict) and len(source) == 1:
        return next(iter(source))
    return None


def versions_text(locked):
    """The versions a lock gives one name, as a fault message should show them."""
    return ", ".join(sorted(str(v) for v in locked if v)) or "no version"


def lock_packages(tree):
    """`uv.lock` read as (versions, sources, closure, roots, note).

    `versions` is every locked distribution, `roots` is the root project's own
    name -- the SBOM's subject is not one of its own components, and the root
    is the one name that legitimately appears in a lock and not in a component
    list -- and `closure` is the root plus everything reachable from it through
    `dependencies`, which is the set both of the rows below are about.

    `versions` and `sources` map a name to a *set*, because a lock may hold more
    than one entry for one name and this one does: the pinned `uv.lock` locks
    `sphinx` twice, 9.0.4 for `python_full_version < "3.12"` and 9.1.0 above it.
    Read as a single value, the entry that happened to come last won, and an
    export or an SBOM produced on the other interpreter -- telling the truth
    about what it resolved -- was failed for it. Every name the lock gives is
    a name the lock has, and the rows below compare by membership. Resolving the
    markers instead would put a second resolver inside a scorer, which is a
    larger thing to be wrong about than a set is.
    """
    lock = os.path.join(tree, "uv.lock")
    if not os.path.exists(lock):
        return {}, {}, set(), set(), "no uv.lock at the repository root"
    data, note = load_toml(lock)
    if data is None:
        return {}, {}, set(), set(), note
    versions, sources, deps = {}, {}, {}
    for pkg in data.get("package") or []:
        name = pkg.get("name")
        if not isinstance(name, str):
            continue
        versions.setdefault(canonical(name), set()).add(pkg.get("version"))
        sources.setdefault(canonical(name), set()).add(source_kind(pkg))
        deps[canonical(name)] = [canonical(d.get("name"))
                                 for d in (pkg.get("dependencies") or [])
                                 if isinstance(d.get("name"), str)]
    root_data, _note = load_toml(os.path.join(tree, "pyproject.toml"))
    root_name = ((root_data or {}).get("project") or {}).get("name")
    roots = [canonical(root_name)] if isinstance(root_name, str) and root_name \
        else []
    closure, queue = set(), list(roots)
    while queue:
        cur = queue.pop()
        if cur in closure:
            continue
        closure.add(cur)
        queue.extend(deps.get(cur, ()))
    return versions, sources, closure, set(roots), "%d packages, closure %d" % (
        len(versions), len(closure))


def validate_document(data):
    """Schema errors for one document, against the version it declares.

    Design §7.2a: 1.5 already admits both shapes of `metadata.tools` -- one
    `oneOf` branch titled *Creation Tools* and one *Creation Tools (legacy)* --
    so the shape change is not what fails. What fails is
    `licenses[].license.acknowledgement` inside the tool components
    `cyclonedx-py` stamps: a 1.6 key in an object 1.5 declares
    `additionalProperties: false`. `jsonschema` reports it at the nearest
    enclosing `oneOf` and so blames `metadata.tools`. Validating everything
    against a fixed 1.5 schema would reject a correct document from a tool this
    step's own reference names, so the schema is selected from `specVersion`
    and the only version requirement is the one the instruction states.
    """
    try:
        from cyclonedx.schema import SchemaVersion
        from cyclonedx.validation.json import JsonStrictValidator
        raw = str(data.get("specVersion"))
        errors = JsonStrictValidator(
            SchemaVersion.from_version(raw)).validate_str(json.dumps(data))
        if errors is not None:
            return "schema %s: %s" % (raw, str(errors)[:160])
    except Exception as exc:
        return "validator: %s: %s" % (type(exc).__name__, exc)
    return None


def gate_sbom_validates(tree, good):
    """Every document in the tree: schema, then agreement with the lock.

    Every one and not the first: a candidate who regenerates the document under
    a new name and leaves the old one behind has left a file that claims to
    describe this repository and does not, which is the whole failure mode the
    group is about. The instruction says "every such document" in the same
    words.

    Nothing here reads `metadata.timestamp`, `metadata.tools[*].version` or
    `serialNumber`: design §7.1 found the first at nanosecond wall-clock
    precision inside the document `uv` writes, and trap 10 says an expectation
    that validates on the clock expires. `s6-sbom-reclocked` regenerates the
    document an hour later with a new serial number and must move nothing.
    """
    if not good:
        row("sbom.validates", False, "no CycloneDX document to validate")
        return
    versions, _sources, closure, roots, note = lock_packages(tree)
    faults = []
    for rel, data in good:
        error = validate_document(data)
        if error:
            faults.append("%s: %s" % (rel, error))
        declared = component_versions(data)
        subject = (data.get("metadata") or {}).get("component") or {}
        subject_name = canonical(subject.get("name") or "")

        unknown = sorted(set(declared) - set(versions))
        if unknown:
            faults.append("%s: components not in uv.lock: %s"
                          % (rel, ", ".join(unknown[:6])))
        for name in sorted(set(declared) & set(versions)):
            seen = declared[name]
            # The versions the lock actually gives this name, the ones it does
            # not having been dropped. `same_version` opens by answering True
            # when either side is None -- deliberately, so that a component the
            # lock has no version for is not failed for one -- and folded into
            # an `any()` over a set, one None in the set makes the whole
            # comparison vacuous: a name `uv.lock` holds twice, once with a
            # version and once without, would excuse any version at all. The
            # excuse would be a fact about the *other* entry, which is not a
            # measurement of anything this document claims. Both comparisons in
            # this file read the filtered set for that reason; `gate_pylock`
            # below points back here.
            #
            # Filtering alone is not enough on this row, and that is the second
            # half of `known`'s job: where a name's only lock entry carries no
            # version, the filtered set is empty and an unguarded `any()` over
            # it would call every declared version wrong -- turning the
            # deliberate tolerance into its opposite. So `known` guards the
            # comparison as well as supplying it, which is what the
            # `any(versions[name])` in `gate_pylock`'s condition already does
            # there. `versions_text` drops the falsy ones too, so no fault
            # message moves.
            known = set(v for v in versions[name] if v)
            # A component with no `version` at all is not a component that
            # agrees with the lock, and `same_version` would call it one:
            # `None` on either side means "not a version I can read", which is
            # the right answer for a spelling it cannot parse and the wrong one
            # for a field that is not there. A document that lists every name
            # and no version would otherwise validate against the schema --
            # `version` is optional in CycloneDX -- and pass this row while
            # saying nothing about what is installed.
            if known and not any(v for v in seen):
                faults.append("%s: %s carries no version, uv.lock has %s"
                              % (rel, name, versions_text(versions[name])))
                continue
            # Any of the versions the lock gives this name, not the one that
            # came last: a name locked twice under two marker sets is locked at
            # both, and a document generated on either interpreter is right.
            bad = sorted(v for v in seen
                         if known and v is not None
                         and not any(same_version(v, locked)
                                     for locked in known))
            if bad:
                faults.append("%s: %s at %s, uv.lock has %s"
                              % (rel, name, ", ".join(map(str, bad)),
                                 versions_text(versions[name])))
        # The root is the document's subject, not one of its own components:
        # `uv export --format cyclonedx1.5` puts it under `metadata.component`
        # and lists the five others, while `cyclonedx-py environment` lists it
        # alongside them. Both are correct documents, so the root is allowed
        # in either place.
        missing = sorted(closure - set(declared) - roots - {subject_name})
        if missing:
            faults.append("%s: the root's runtime closure is not covered: %s"
                          % (rel, ", ".join(missing[:6])))

    row("sbom.validates", not faults,
        "; ".join(faults[:3]) if faults
        else "%d document(s) validate and agree with uv.lock: %s (%s)"
             % (len(good), ", ".join("%s@%s" % (rel, data.get("specVersion"))
                                     for rel, data in good[:3]), note))


def gate_pylock(tree):
    """PEP 751, compared as parsed TOML and never as bytes.

    The file's own header embeds the exact command that produced it, `-o` path
    included, so a byte comparison would score the invocation. And the export is
    the root's closure rather than an inventory of the workspace -- `hello` is a
    member nothing depends on and `uv` leaves it out -- so the row is staleness
    in both directions rather than set equality with any one group selection.

    Every `pylock.*.toml` in the tree, not the first one sorted, for the reason
    `sbom.validates` gives in the same words: a candidate who re-exports under
    a second name and leaves the first behind has left a file that claims to
    lock this repository and does not. PEP 751 gives both spellings the same
    standing, so the row cannot pick one and call the other a draft.
    """
    found = [p for p in walk(tree)
             if os.path.basename(p) == "pylock.toml"
             or (os.path.basename(p).startswith("pylock.")
                 and p.endswith(".toml"))]
    if not found:
        row("pylock.exported", False,
            "no pylock.toml anywhere in the repository")
        return
    versions, sources, closure, _roots, lnote = lock_packages(tree)
    faults = []
    summaries = []
    for path in sorted(found):
        rel = gatelib.relpath(path, tree)
        data, note = load_toml(path)
        if data is None:
            faults.append("%s: %s" % (rel, note))
            continue
        if not data.get("lock-version"):
            faults.append("%s: no lock-version" % rel)
        exported = {}
        # PEP 751 makes `version` optional, and for exactly one reason: a
        # package installed from a directory, a VCS checkout or an archive is
        # identified by that source and may have no released version to name.
        # `uv export` writes the two workspace members that way. Every other
        # entry has a version in the lock and must carry it here; without this
        # split the row would either fail the export `uv` actually produces or
        # accept a file that names packages and pins nothing.
        #
        # And the excuse is checked against the lock rather than taken from the
        # export, because it is a fact about the package and the export is the
        # document under suspicion: `name = "six"` with `directory = { path =
        # "." }` and no version is three lines of TOML that pin nothing at all.
        # `excused` collects the entries that claim it, and the claim is a fault
        # when `uv.lock` sources that name from a registry.
        unversioned, excused = set(), set()
        for pkg in data.get("packages") or []:
            name = pkg.get("name")
            if isinstance(name, str) and name:
                cname = canonical(name)
                exported[cname] = pkg.get("version")
                if not pkg.get("version"):
                    if any(pkg.get(k)
                           for k in ("directory", "vcs", "archive")):
                        excused.add(cname)
                    else:
                        unversioned.add(cname)
        if not exported:
            faults.append("%s: no [[packages]] entries" % rel)
        for name, version in sorted(exported.items()):
            if name not in versions:
                faults.append("%s: %s is exported and is not in uv.lock"
                              % (rel, name))
            elif name in unversioned:
                faults.append("%s: %s is exported with no version and no "
                              "directory, vcs or archive source; uv.lock has %s"
                              % (rel, name, versions_text(versions[name])))
            # Registry versus everything else, and never a comparison of the two
            # files' key names: the reference export writes `directory = { path
            # = "." }` for the root and `uv.lock` spells the same fact `source =
            # { editable = "." }`, so the two vocabularies agree only on which
            # side of the registry line a package falls. A name whose source the
            # lock does not give -- or gives as something other than a registry
            # in even one of its entries -- is not a fault: real locks always
            # carry `source`, and reading a missing one as a registry would fail
            # a correct tree.
            elif name in excused and sources.get(name) == {"registry"}:
                faults.append("%s: %s is exported with no version and a "
                              "directory, vcs or archive source; uv.lock "
                              "sources it from a registry at %s"
                              % (rel, name, versions_text(versions[name])))
            # `same_version`, the same tolerance the SBOM half gives: `3.4` and
            # `3.4.0` are one release written two ways, and against a name the
            # lock holds twice it is enough to agree with either entry. Either
            # entry the lock gives a version for: the filter is the one
            # `gate_sbom_validates` explains above, and `any(versions[name])`
            # standing in front of it is why nothing here can be talked out of
            # the comparison by an entry that carries no version at all.
            elif version and any(versions[name]) and not any(
                    same_version(version, locked)
                    for locked in set(v for v in versions[name] if v)):
                faults.append("%s: %s exported at %s, uv.lock has %s"
                              % (rel, name, version,
                                 versions_text(versions[name])))
        for name in sorted(closure - set(exported)):
            faults.append("%s: %s is in the root's runtime closure and is not "
                          "exported" % (rel, name))
        summaries.append("%s (lock-version %s, %d packages)"
                         % (rel, data.get("lock-version"), len(exported)))
    row("pylock.exported", not faults,
        "; ".join(faults[:4]) if faults
        else "%d file(s) current with uv.lock: %s (%s)"
             % (len(found), ", ".join(summaries[:3]), lnote))


def gate_reqtxt_root(tree):
    """No `*requirements*.txt` at the repository root.

    The instruction's sentence has two halves and this row is both of them: the
    files an earlier milestone turned into dependency groups do not come back,
    and the export this milestone asks for does not arrive in the older format
    beside the one it asks for. `uv export` writes `requirements.txt` at the
    working directory by default, so a candidate who exports twice -- once in
    the format the instruction names and once in the format their fingers know
    -- lands one here without meaning to.

    Scoped to the root because that is what the instruction says. The three
    files the earlier milestone removed lived there, `uv export`'s default
    output lands there, and a `doc/requirements.txt` that some documentation
    build reads is not what either half of the sentence is about. Widening the
    scan to the whole tree would be scoring a requirement nobody stated.

    The match is on the instruction's own glob, case-folded: a repository that
    is checked out on a case-insensitive filesystem has no way to tell
    `Requirements.txt` from `requirements.txt`, and neither should this.
    """
    hits = sorted(name for name in os.listdir(tree)
                  if os.path.isfile(os.path.join(tree, name))
                  and fnmatch.fnmatch(name.lower(), "*requirements*.txt"))
    row("reqtxt.root-clean", not hits,
        "at the repository root: " + ", ".join(hits[:8]) if hits
        else "no *requirements*.txt at the repository root")


# --------------------------------------------------------------------------

SOURCE_LABELS = ["distutils.imports-gone", "distutils.trees-gone",
                 "setuppy.gone", "pylock.exported", "reqtxt.root-clean",
                 "sbom.present", "sbom.validates"]
ARTIFACT_LABELS = ["distutils.public-names-kept", "license.expression",
                   "license.files", "typed.marker", "twine.clean"]


def check_emitted(mode, want):
    got = [label for _family, label, _ok, _detail in ROWS]
    if sorted(got) != sorted(want):
        sys.stderr.write(
            "GATE FAULT (%s): emitted %d rows, expected %d; missing %r; "
            "unexpected %r\n"
            % (mode, len(got), len(want), sorted(set(want) - set(got)),
               sorted(set(got) - set(want))))
        raise SystemExit(4)


def main(argv):
    mode = argv[1]

    if mode == "source":
        tree = argv[2]
        # Scored by nothing; stderr, like every other handoff in this suite.
        # A run's log should be able to say what was in scope for the import
        # scan rather than leave it to be inferred from a number.
        for member in other_member_dirs(tree):
            sys.stderr.write("EXEMPT %s\n" % member)
        gate_imports_gone(tree)
        gate_trees_gone(tree)
        gate_setuppy_gone(tree)
        gate_pylock(tree)
        gate_reqtxt_root(tree)
        gate_sbom_validates(tree, gate_sbom_present(tree))
        check_emitted(mode, SOURCE_LABELS)
        flush()
        return 0

    if mode == "artifacts":
        # The twine log arrives as a path rather than as a string: it is
        # third-party output of unbounded length and shape, and `test.sh`
        # putting it on a command line would be a quoting accident waiting to
        # happen. Unreadable is reported, never raised -- rule 8 says this
        # script's caller always writes a reward.
        twine_rc = int(argv[2])
        try:
            with open(argv[3], "rb") as handle:
                twine_out = handle.read().decode("utf-8", "replace")
        except Exception as exc:
            twine_out = "the twine log at %s could not be read: %s" % (
                argv[3], exc)
        wheels = []
        for directory in argv[4:]:
            if not os.path.isdir(directory):
                continue
            wheels += [os.path.join(directory, n)
                       for n in sorted(os.listdir(directory))
                       if n.endswith(".whl")]
        # Which wheels are the pure ones is read off the wheels themselves --
        # the compatibility tag in the filename -- and not from a list here.
        # Step 04 already scores `bento` and `yaku` as the two distributions
        # with no compiled code in them, so a fourth distribution a candidate
        # adds is held to whichever half of this step its own tag puts it in.
        pure = {}
        for wheel in wheels:
            name = wheel_distribution(wheel)
            if name and "-none-any" in os.path.basename(wheel):
                pure[name] = wheel
        gate_public_names(wheels)
        gate_license_expression(pure)
        gate_license_files(pure)
        gate_typed_marker(wheels)
        gate_twine(twine_rc, twine_out)
        check_emitted(mode, ARTIFACT_LABELS)
        flush()
        return 0

    raise SystemExit("unknown mode %r" % mode)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
