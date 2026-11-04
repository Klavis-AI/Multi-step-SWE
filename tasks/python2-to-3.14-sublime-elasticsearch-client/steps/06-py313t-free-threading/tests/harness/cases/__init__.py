# -*- coding: utf-8 -*-
"""Scored inputs, one module per interface area of the tree.

Every module here registers cases with ``@registry.case`` and imports the tree
**inside** the case body only. A case returns whatever the interface handed
back; the runner records the return value, the exception, the bytes that
reached the socket, and the log records the tree emitted.
"""
import re

#: Backslash-plus-one-character runs, counted in an *evaluated* docstring.
#:
#: The pinned tree has 26 invalid escape sequences and 24 of them are
#: docstrings, spread over four packages; step 03 is where they stop being a
#: DeprecationWarning and become a SyntaxWarning, and its instruction says that
#: fixing one must not change what the literal evaluates to. Both correct fixes
#: -- an ``r`` prefix, or doubling the backslash -- leave this census
#: untouched; deleting the backslash, which is what the surrounding prose
#: tempts (``:param \*\*kwargs:`` reads better as ``**kwargs``), moves it.
#:
#: Only the escape runs are recorded, never the docstring text. A later rung
#: may reword a docstring for a good reason, and a case that pinned whole
#: paragraphs would turn that into a failure it could not avoid.
_ESCAPE_RUN = re.compile(r"\\.", re.S)


def doc_escapes(obj):
    """The escape census of ``obj.__doc__``, or ``None`` if it has none."""
    doc = getattr(obj, "__doc__", None)
    if doc is None:
        return None
    runs = {}
    for m in _ESCAPE_RUN.finditer(doc):
        runs[m.group(0)] = runs.get(m.group(0), 0) + 1
    return {"runs": sorted(runs.items()), "total": sum(runs.values())}
