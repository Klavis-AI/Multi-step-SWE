"""AST helpers that behave the same on Python 3.7 and on 3.12.

The gates run inside each step's verifier image, and those images span 3.7
(step 1) to 3.12 (steps 6-8). Three things moved in that range and every one of
them would silently change what a rule matches:

* literals are ``ast.Num``/``ast.Str``/``ast.NameConstant`` before 3.8 and
  ``ast.Constant`` after;
* ``Subscript.slice`` is wrapped in ``ast.Index`` before 3.9 and bare after;
* ``ast.Attribute`` gained ``end_col_offset`` in 3.8.

``const_value`` and ``subscript_index`` below are the only places that know it.
"""
from __future__ import print_function, absolute_import, division

import ast

_MISSING = object()


def const_value(node, default=_MISSING):
    """The literal a node carries, or ``default`` if it is not a literal.

    Raises ``ValueError`` when the node is not a literal and no default was
    given, so a rule that assumes a literal fails loudly rather than matching
    nothing.
    """
    if isinstance(node, ast.Constant):        # 3.8+
        return node.value
    if node.__class__.__name__ == "Num":      # 3.7
        return node.n
    if node.__class__.__name__ == "Str":
        return node.s
    if node.__class__.__name__ == "Bytes":
        return node.s
    if node.__class__.__name__ == "NameConstant":
        return node.value
    if default is _MISSING:
        raise ValueError("not a literal: %s" % node.__class__.__name__)
    return default


def subscript_index(node):
    """``Subscript.slice`` with 3.7/3.8's ``ast.Index`` wrapper removed."""
    sl = node.slice
    if sl.__class__.__name__ == "Index":
        return sl.value
    return sl


def dotted(node):
    """``a.b.c`` for a Name/Attribute chain, else ``None``.

    A call in the middle (``a.b().c``) breaks the chain: the result is no
    longer a module path and resolving it against the import table would be a
    guess.
    """
    parts = []
    while isinstance(node, ast.Attribute):
        parts.append(node.attr)
        node = node.value
    if not isinstance(node, ast.Name):
        return None
    parts.append(node.id)
    parts.reverse()
    return ".".join(parts)


def attr_name(node):
    """The trailing attribute of ``x.y.attr``, or ``None`` for a bare name.

    Used by the rules that match a *method* regardless of what it is called on
    -- ``as_matrix``, ``applymap``, ``iterrows`` -- where no import table can
    help because the receiver is a runtime value.
    """
    if isinstance(node, ast.Attribute):
        return node.attr
    return None


class ImportTable(object):
    """What every bare name in one module actually refers to.

    Built from every ``import``/``from ... import`` in the file, including the
    ones inside functions -- skutil imports inside functions constantly to keep
    optional dependencies optional, and a rule that only looked at module-level
    imports would miss every one of those call sites.

    Over-approximating is the right failure direction here: two different
    imports binding the same name in two different scopes is rare, and the cost
    is a finding pointing at a real construct under a slightly wrong
    fully-qualified name. Missing the construct entirely is the cost of the
    other direction.
    """

    def __init__(self):
        self.aliases = {}   # bound name -> fully-qualified prefix
        self.star = []      # modules imported with ``*``

    @classmethod
    def build(cls, tree):
        table = cls()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    bound = alias.asname or alias.name.split(".")[0]
                    target = alias.name if alias.asname else alias.name.split(".")[0]
                    table.aliases[bound] = target
                    # ``import numpy.distutils`` also binds ``numpy``; keep the
                    # full path reachable so a ``numpy.distutils.core`` use
                    # resolves even without an ``as``.
                    if not alias.asname and "." in alias.name:
                        table.aliases.setdefault(alias.name.split(".")[0],
                                                 alias.name.split(".")[0])
            elif isinstance(node, ast.ImportFrom):
                if node.level:
                    continue          # relative: not a library path
                module = node.module or ""
                for alias in node.names:
                    if alias.name == "*":
                        table.star.append(module)
                        continue
                    bound = alias.asname or alias.name
                    table.aliases[bound] = module + "." + alias.name
        return table

    def resolve(self, name):
        """``np.float`` -> ``numpy.float``; an unknown root is returned as-is."""
        if name is None:
            return None
        head, _, tail = name.partition(".")
        root = self.aliases.get(head)
        if root is None:
            return name
        return root + ("." + tail if tail else "")

    def imported_modules(self):
        """Every module path this file names, one entry per import statement."""
        return sorted(set(list(self.aliases.values()) + self.star))


VERSION_ROOTS = ("sys.version_info", "sys.version")


def version_rooted(node, table):
    """True when *node* reads the running interpreter's version.

    ``sys.version_info``, ``sys.version_info.major``, ``sys.version_info[0]``
    and ``sys.version[0]`` all answer yes; anything else answers no. The import
    table is consulted so ``from sys import version_info`` and
    ``import sys as _s`` resolve to the same thing.
    """
    while isinstance(node, ast.Subscript):
        node = node.value
    if isinstance(node, ast.Attribute) and node.attr in ("major", "minor",
                                                         "micro"):
        node = node.value
    return table.resolve(dotted(node)) in VERSION_ROOTS


def version_alias_names(tree, table):
    """Every bare name this module binds to the interpreter's version.

    ``py2-version-branch`` matches a comparison against ``sys.version_info``.
    This repository writes three of those directly and reaches the rest through
    a local bound one statement earlier -- ``VERSION_MAJOR =
    sys.version_info.major`` at ``skutil/utils/fixes.py:34``, then ``if
    VERSION_MAJOR > 2`` four lines down. A rule that reads only the direct
    spelling counts three of nine sites, and, worse, can be *satisfied* by
    hoisting the subscript onto its own line: the branch would still be there,
    still dead, and the gate would report it gone.

    Module-wide and over-approximating, for the reason ``ImportTable`` gives:
    the same name bound two different ways in two different scopes is rare, and
    a finding under a slightly wrong name costs less than no finding at all.
    """
    names = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign):
            targets = node.targets
        elif isinstance(node, ast.AnnAssign):
            targets = [node.target]
        else:
            continue
        if not version_rooted(node.value, table):
            continue
        for target in targets:
            if isinstance(target, ast.Name):
                names.add(target.id)
            elif isinstance(target, (ast.Tuple, ast.List)):
                # ``major, minor = sys.version_info[:2]``
                for elt in target.elts:
                    if isinstance(elt, ast.Name):
                        names.add(elt.id)
    return names


def call_keyword(node, name):
    """The ``ast.keyword`` for ``name=`` in a call, or ``None``."""
    for kw in getattr(node, "keywords", ()):
        if kw.arg == name:
            return kw
    return None


def func_of(node):
    """The callee expression of a Call, unwrapping nothing else."""
    return node.func if isinstance(node, ast.Call) else None


def enclosing_functions(tree):
    """Map every node to the dotted name of the function that contains it.

    Step 8 gates the *hot paths* by name, so a rule has to know whether the
    ``for`` it just found is inside ``filter_collinearity`` or inside a
    docstring example three functions away.
    """
    owner = {}

    def walk(node, prefix):
        for child in ast.iter_child_nodes(node):
            if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef,
                                  ast.ClassDef)):
                name = prefix + "." + child.name if prefix else child.name
                owner[child] = name
                walk(child, name)
            else:
                owner[child] = prefix
                walk(child, prefix)

    walk(tree, "")
    return owner
