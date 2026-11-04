"""Migrated-ness gates: what the tree may no longer contain.

The behavior corpus answers "does it still do the same thing". These answer
"was it actually migrated, or was it shimmed". They are deliberately separate
code with a separate failure mode, because a shim that restores old behavior
passes the first and must fail the second.

Everything here reads structure, never prose: Python sources through ``ast``,
Cython sources and string literals through ``tokenize``, notebooks through the
``.ipynb`` JSON, lint findings through ruff's JSON output. A construct is
counted once per *expression*, so a function with four ``inplace=True``
keywords is four findings and not one.
"""
from __future__ import print_function, absolute_import, division

__all__ = ["astutils", "rules", "scan", "suppress", "ruffgate", "score"]
