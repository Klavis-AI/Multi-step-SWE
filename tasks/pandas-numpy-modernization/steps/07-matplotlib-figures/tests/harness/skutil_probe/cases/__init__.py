"""The casebooks.

Importing this package registers nothing. ``registry.load_all`` walks
``MODULES`` and imports them one at a time, because a casebook that cannot be
imported must cost its own cases and not the corpus -- see ``load_all`` for the
argument. This list is the only place the order is written down, and it is also
the order ``python -m skutil_probe.run list`` prints.
"""
from __future__ import print_function, absolute_import, division

MODULES = (
    "c_utilfun",
    "c_validate",
    "c_stats",
    "c_kernel",
    "c_odr",
    "c_decompose",
    "c_select",
    "c_transform",
    "c_impute",
    "c_encode",
    "c_balance",
    "c_grid",
    "c_gains",
    "c_base",
    "c_plot",
)
