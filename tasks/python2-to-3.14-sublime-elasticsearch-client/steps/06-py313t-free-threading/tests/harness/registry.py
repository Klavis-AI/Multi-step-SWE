# -*- coding: utf-8 -*-
"""Case registry.

A case is a function that drives the interface and returns whatever
the interface handed back. It must not import anything from the tree at module
scope — only inside the function body — so the registry can be enumerated
without the tree being importable at all.
"""
import importlib
import pkgutil

CASES = {}
_ORDER = []


class Case(object):
    __slots__ = ("cid", "fn", "tags", "fixture", "tls", "module")

    def __init__(self, cid, fn, tags, fixture, tls, module):
        self.cid = cid
        self.fn = fn
        self.tags = tuple(tags)
        self.fixture = fixture
        self.tls = tls
        self.module = module

    @property
    def adversarial(self):
        return "adversarial" in self.tags


def case(cid, tags=(), fixture=None, tls=False):
    """Register one scored input.

    ``fixture`` names a zero-argument factory returning ``{route_key: Route}``;
    it runs before the tree is imported. ``tls`` starts the fixture server on
    HTTPS with the runtime-generated certificate.
    """
    def deco(fn):
        if cid in CASES:
            raise RuntimeError("duplicate case id %r" % cid)
        c = Case(cid, fn, tags, fixture, tls, fn.__module__)
        CASES[cid] = c
        _ORDER.append(cid)
        return fn
    return deco


def load_all():
    from . import cases as cases_pkg
    for mod in pkgutil.iter_modules(cases_pkg.__path__):
        importlib.import_module("%s.%s" % (cases_pkg.__name__, mod.name))
    return CASES


def ids():
    return list(_ORDER)
