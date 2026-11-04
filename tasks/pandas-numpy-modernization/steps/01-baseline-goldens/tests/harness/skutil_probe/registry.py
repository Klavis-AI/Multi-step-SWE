"""Case registry.

A case is a zero-argument callable whose return value is recorded. Raising is a
legitimate outcome: the exception type, and the message when it came from
skutil's own code, are recorded exactly as the baseline produced them.
"""
from __future__ import print_function, absolute_import, division

import importlib
import traceback

CASES = []
_SEEN = set()

BEHAVIOR = "behavior"
ADVERSARIAL = "adversarial"
FIGURE = "figure"
INVARIANT = "invariant"


def case(cid, tags=(), rtol=None, atol=None):
    """Register a case under a stable id."""
    def deco(fn):
        if cid in _SEEN:
            raise ValueError("duplicate case id: %s" % cid)
        _SEEN.add(cid)
        CASES.append({
            "id": cid,
            "tags": sorted(set(tags)),
            "fn": fn,
            "rtol": rtol,
            "atol": atol,
        })
        return fn
    return deco


def adversarial(cid, **kw):
    tags = set(kw.pop("tags", ()))
    tags.add(ADVERSARIAL)
    return case(cid, tags=tags, **kw)


LOAD_ERRORS = []


def load_all():
    """Import every casebook module, populating CASES.

    One module at a time, each failure caught, because the cost of not catching
    it is out of all proportion to the mistake. A casebook imports the names it
    exercises at module scope -- that is what makes a case three lines instead
    of ten -- so a candidate that drops one name the baseline re-exported takes
    down the import of every casebook that mentions it. Uncaught, that is the
    whole corpus: no recording is written at all, the behaviour family reads
    0.0, and the number says nothing about the other twelve hundred cases.
    Caught, it costs exactly the cases in the modules that failed, which
    ``cmd_compare`` scores as ``missing`` because they are absent from the
    recording. The gates charge separately for the dropped name.

    The failures are not swallowed: they land in ``LOAD_ERRORS``, ``record``
    prints them and puts them in the payload, and ``compare`` copies them into
    its report. A corpus fraction that fell because a casebook would not import
    has to be diagnosable from the saved bundle alone.
    """
    del LOAD_ERRORS[:]
    from . import cases
    package = cases.__name__
    for name in cases.MODULES:
        try:
            importlib.import_module(package + "." + name)
        except Exception as exc:                      # noqa: BLE001
            LOAD_ERRORS.append({
                "module": name,
                "error": type(exc).__name__ + ": " + str(exc)[:300],
                "traceback": traceback.format_exc()[-1200:],
            })
    return CASES
