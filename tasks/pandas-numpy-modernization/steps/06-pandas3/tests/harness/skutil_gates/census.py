"""What each shipped file still *contains*, as numbers a manifest can hold.

Every other gate in this package counts occurrences of an old construct and
rewards fewer of them, so the cheapest way to satisfy all of them at once is to
remove code. ``run._tree_paths`` already answers ``rm`` -- the file has to still
be there -- but "still there" is satisfied by an empty file, and most of this
tree's findings live in places no runtime check reaches: nine of the ten invalid
escapes are in a vendored docstring parser nothing imports, and half the global
RNG sites are in the repository's own test files, which the inherited-suite
table only ever asks to *collect and pass* -- something a body of ``pass``
does.

So this module records, per file, the things the migration does not change and
a gutting does:

``def_names`` every ``def`` and ``class`` the module defines, at any depth,
             written dotted with its enclosing definitions
             (``NumpyDocString.__init__``). This is what the ``source-census``
             gate scores, because a definition is the unit a gutting removes and
             a migration never does: rewriting a call, adding an ``r`` prefix or
             fixing a ``print`` leaves every one, while a stub, a re-export shim
             or a truncation loses them. Compared as a subset, so adding a
             definition is free. Members are in here because top-level names
             alone were not enough: a file that keeps its four ``class``
             statements and drops all forty-five of their methods defines
             exactly the names it used to, and only the dotted method names
             tell the two apart. Import bindings and module-level constants are
             deliberately *not* in here -- a migration legitimately drops a dead
             import when it collapses a version branch, retires a Python-2
             builtin, or renames a global-RNG helper, and charging for those
             removals fails the honest agent for doing the step. A public
             constant that is part of the surface is ``public-api``'s to hold; a
             file emptied of everything but its imports is the line and digest
             floors' to catch (see below); neither needs this row.

``names``    the fuller binding set -- everything in ``def_names`` *plus*
             module-level assignment and import bindings, including inside a
             top-level ``if``/``try``/``for``/``with``. No gate scores it; it is
             the coverage signal the recorder uses to decide a file is real
             source worth a line floor and a retention set, so a settings module
             that is all assignments (``doc/conf.py``) still earns those two
             even though it defines nothing.

``lines``    non-blank, non-comment source lines. Compared as a floor, because
             a body can be gutted without losing a single name -- give every
             method a body of ``pass`` and the dotted names above all survive.
             Counted off the raw text rather than the parse so that a file
             changing parse-state mid-migration does not change the unit.

``digests``  a six-character hash of each distinct stripped code line. Compared
             as a *fraction still present*, because ``lines`` is a count and a
             count can be bought back: gut every body and append two hundred
             ``_pad_n = n`` and the file clears its line floor holding none of
             the code it was handed. The only way to raise this one is to put
             the handed-over lines back. Stripped before hashing so re-indenting
             is free; hashed so the manifest holds six bytes a line rather than
             the line itself.

``asserts`` how many assertions a test module makes: ``assert`` statements plus
             calls to ``assertX``/``raises``. Compared as a floor. This is the
             one number that distinguishes "the tests still pass" from "the
             tests still exist", which the outcome table in ``suite`` cannot:
             a body replaced by ``pass`` collects under its recorded node id and
             passes.

Files that do not parse are censused with a tolerant reader instead of being
skipped, because two of the files this step is *about* do not parse on entry
(``doc/conf.py`` is Python 2) and a check that only starts applying once the
agent has fixed them would not be carried. Notebooks are censused through the
scanner's own ``notebook_source``, so blanking the code cells is a loss of
names like any other.
"""
from __future__ import print_function, absolute_import, division

import ast
import hashlib
import os
import re

from . import scan as scan_mod

# `name =`, `def name`, `class name` at column 0. Only used for a file the
# interpreter will not parse, where the alternative is no census at all.
_FLAT_ASSIGN = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(?::[^=]+)?=(?!=)")
_FLAT_DEF = re.compile(r"^(?:def|class)\s+([A-Za-z_][A-Za-z0-9_]*)")

# Calls that are an assertion in the idiom this tree's tests are written in:
# bare `assert`, unittest's `assertEqual`, nose/pytest's `assert_array_equal`,
# and the `raises` context managers.
_ASSERT_CALLS = frozenset(["raises", "fail", "failUnless", "failIf"])

_BLOCK_BODIES = ("body", "orelse", "finalbody")


def _bound_by(target, out):
    """Names a single assignment target binds."""
    if isinstance(target, ast.Name):
        out.add(target.id)
    elif isinstance(target, (ast.Tuple, ast.List)):
        for elt in target.elts:
            _bound_by(elt, out)
    elif isinstance(target, ast.Starred):
        _bound_by(target.value, out)


def _top_level(body, out):
    """Every name *body* binds, descending through block statements only.

    A name bound inside a top-level ``if`` is still a module-level name --
    ``doc/conf.py`` binds half of its settings that way -- but a name bound
    inside a function is that function's business, so ``FunctionDef`` and
    ``ClassDef`` contribute their own name and are not walked into.
    """
    for node in body:
        if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
            out.add(node.name)
        elif hasattr(ast, "AsyncFunctionDef") and isinstance(
                node, ast.AsyncFunctionDef):
            out.add(node.name)
        elif isinstance(node, ast.Assign):
            for target in node.targets:
                _bound_by(target, out)
        elif isinstance(node, ast.AugAssign):
            _bound_by(node.target, out)
        elif hasattr(ast, "AnnAssign") and isinstance(node, ast.AnnAssign):
            _bound_by(node.target, out)
        elif isinstance(node, (ast.Import, ast.ImportFrom)):
            for alias in node.names:
                if alias.name == "*":
                    continue
                out.add(alias.asname or alias.name.split(".")[0])
        else:
            if isinstance(node, ast.For):
                _bound_by(node.target, out)
            elif isinstance(node, ast.With):
                for item in getattr(node, "items", []):
                    if item.optional_vars is not None:
                        _bound_by(item.optional_vars, out)
            for field in _BLOCK_BODIES:
                inner = getattr(node, field, None)
                if isinstance(inner, list):
                    _top_level(inner, out)
            for handler in getattr(node, "handlers", []) or []:
                _top_level(handler.body, out)


def _qualified_defs(body, prefix, out):
    """Every ``def``/``class`` at any depth, dotted with its enclosing defs.

    Unlike :func:`_top_level` this *does* descend into functions and classes,
    because the point is the shape of the file rather than its public surface.
    """
    for node in body:
        is_def = isinstance(node, (ast.FunctionDef, ast.ClassDef)) or (
            hasattr(ast, "AsyncFunctionDef")
            and isinstance(node, ast.AsyncFunctionDef))
        if is_def:
            qualified = prefix + node.name
            out.add(qualified)
            _qualified_defs(node.body, qualified + ".", out)
            continue
        for field in _BLOCK_BODIES:
            inner = getattr(node, field, None)
            if isinstance(inner, list):
                _qualified_defs(inner, prefix, out)
        for handler in getattr(node, "handlers", []) or []:
            _qualified_defs(handler.body, prefix, out)


# Six hex characters a line. Collisions cost an attacker nothing and an
# honest submission nothing: a collision can only make a line look kept.
DIGEST_CHARS = 6


def _line_digests(text):
    """A short digest of each distinct code line, for the retention floor.

    Hashed rather than stored verbatim so the manifest holds six bytes a line
    instead of the line, and set-valued because a file that repeats a line has
    not kept more of itself than one that does not. Stripped before hashing,
    so re-indenting a block is not a loss.
    """
    out = set()
    for line in text.splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            digest = hashlib.sha1(stripped.encode("utf-8")).hexdigest()
            out.add(digest[:DIGEST_CHARS])
    return sorted(out)


def _code_lines(text):
    """Source lines that carry code, counted without parsing."""
    n = 0
    for line in text.splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            n += 1
    return n


def _assert_count(tree):
    n = 0
    for node in ast.walk(tree):
        if isinstance(node, ast.Assert):
            n += 1
        elif isinstance(node, ast.Call):
            func = node.func
            name = (func.attr if isinstance(func, ast.Attribute)
                    else func.id if isinstance(func, ast.Name) else None)
            if name and (name.startswith("assert") or name in _ASSERT_CALLS):
                n += 1
    return n


def _flat_names(text):
    out = set()
    for line in text.splitlines():
        m = _FLAT_DEF.match(line) or _FLAT_ASSIGN.match(line)
        if m:
            out.add(m.group(1))
    return out


def _flat_defs(text):
    """``def``/``class`` names from a file the interpreter will not parse.

    The unparseable counterpart of :func:`_qualified_defs`: only the ``def`` and
    ``class`` left-hand sides, so a file mid-migration is censused for
    definitions in the same subset-safe direction as :func:`_flat_names`.
    """
    out = set()
    for line in text.splitlines():
        m = _FLAT_DEF.match(line)
        if m:
            out.add(m.group(1))
    return out


def census_source(text, is_test):
    """``{"names", "def_names", "asserts", "lines", "digests"}`` for one file.

    ``def_names`` is the definition set the ``source-census`` gate scores;
    ``names`` is the fuller binding set the recorder uses only to decide a file
    is real source worth a line floor. See the module docstring for why the two
    differ.
    """
    try:
        tree = ast.parse(text)
    except SyntaxError:
        # Python 2, or an agent mid-edit. A flat read of the left-hand sides is
        # a subset of what a parse would find, which is the direction that
        # keeps this comparison from ever failing a tree that only got better.
        return {"names": sorted(_flat_names(text)),
                "def_names": sorted(_flat_defs(text)), "asserts": 0,
                "lines": _code_lines(text),
                "digests": _line_digests(text), "parsed": False}
    names = set()
    _top_level(tree.body, names)
    _qualified_defs(tree.body, "", names)
    defs = set()
    _qualified_defs(tree.body, "", defs)
    return {"names": sorted(names),
            "def_names": sorted(defs),
            "asserts": _assert_count(tree) if is_test else 0,
            "lines": _code_lines(text),
            "digests": _line_digests(text),
            "parsed": True}


def census_tree(root):
    """Every ``.py`` and ``.ipynb`` under *root*, censused."""
    out = {}
    for full, rel in scan_mod.iter_files(root):
        rel = rel.replace(os.sep, "/")
        if rel.endswith(scan_mod.PY_SUFFIXES):
            text = scan_mod._read(full)
        elif rel.endswith(scan_mod.NB_SUFFIXES):
            raw = scan_mod._read(full)
            text = scan_mod.notebook_source(raw) if raw is not None else None
        else:
            continue
        if text is None:
            continue
        out[rel] = census_source(text, scan_mod.classify(rel) == "tests")
    return out
