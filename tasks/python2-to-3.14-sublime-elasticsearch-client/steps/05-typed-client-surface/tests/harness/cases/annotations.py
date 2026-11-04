# -*- coding: utf-8 -*-
"""What the library's own annotations are at run time.

Rung 05 annotates the public surface and stringises every annotation with
``from __future__ import annotations`` (PEP 563). Rung 07 deletes that import
and takes PEP 649's deferred evaluation instead, and the two are not the same
thing: under PEP 563 an annotation is the *text* of the expression for ever,
under PEP 649 it is a lazily built object any caller can ask for and get. The
two cases here observe that difference from outside, through the modules the
suite already imports, and they observe nothing else about it -- not the
wording of any annotation, which rung 05's gates hold in full, and not
anything about a name a migration is free to invent.

``annotations.public-surface-is-objects``
    reads ``__annotations__`` off each public def and records which of them
    came back holding a ``str``. Under PEP 563 that is all of them; under
    PEP 649 it is none of them. Beside it, one boolean per module says whether
    every def that ought to carry annotations carries any, so deleting an
    annotation is not a way to stop it being a string.

``annotations.public-surface-resolves``
    asks ``typing.get_type_hints`` for the same defs, and for the classes they
    hang off. This is the half that does not follow from deleting an import:
    an annotation that names something imported only under ``if
    TYPE_CHECKING:`` is a string PEP 563 never evaluates and a lazy object
    PEP 649 evaluates on demand, and on demand it raises. A tree whose
    annotations are objects nobody can resolve has moved the failure, not
    removed it.

Both answers are per module, and every part of them that is non-empty on a
*correct* tree is a boolean rather than a name. Rung 05 does not ask for a
particular signature -- it asks for an annotated surface, which its gates and
``mypy --strict`` hold -- so recording, per public def, the key list of its
``__annotations__`` would fail a tree at rung 07 for writing ``-> None`` on
``__init__``, spelling ``**kwargs`` out as named parameters, or adding a
method, all of which satisfy rung 05 in full. The booleans and the
string/failure maps keep everything these cases are for -- stringised
annotations, unreadable ones, unresolvable ones, and annotations deleted
wholesale -- without recording whose signatures the tree has. Deleting *some*
of a def's annotations is held by the ``public-surface-annotated`` gate, which
reads the parameter list itself and is inherited at rung 07.

On the pinned tree nothing is annotated, so both cases record twelve modules
that are not fully annotated and four with nothing to judge, with no
stringised, unreadable or unresolvable names anywhere -- and rungs 01 to 04,
none of which annotates anything, produce the same answers byte for byte.
Rung 05 makes the answers non-empty for the first time and both cases are
shelved there: what rung 05 states is that the surface is *declared*, which
its five gates and ``mypy --strict`` already hold, and not what
``__annotations__`` holds at run time. Rung 07 states
exactly that, so both come back there as that rung's own bar, where every
module is fully annotated and fully resolvable.
"""
import types

from ..registry import case

#: The library modules rung 05 annotates, as import names, fixed rather than
#: discovered. A list built by walking the tree would shrink the moment a
#: candidate deleted a module and would then report the deletion as a clean
#: answer; this one cannot. Every name here imports on 3.8 against the pinned
#: tree and on 3.14 against the rung-06 reference solution.
MODULES = (
    "elasticsearch",
    "elasticsearch.client.utils",
    "elasticsearch.compat",
    "elasticsearch.connection",
    "elasticsearch.connection.base",
    "elasticsearch.connection.http_requests",
    "elasticsearch.connection.http_urllib3",
    "elasticsearch.connection.memcached",
    "elasticsearch.connection.pooling",
    "elasticsearch.connection.thrift",
    "elasticsearch.connection_pool",
    "elasticsearch.exceptions",
    "elasticsearch.helpers",
    "elasticsearch.helpers.test",
    "elasticsearch.serializer",
    "elasticsearch.transport",
)


def _public(name):
    """Rung 05's own definition of a public def, so the two cannot drift."""
    return not name.startswith("_") or name in (
        "__init__", "__enter__", "__exit__", "__iter__", "__next__",
        "__call__")


def _unwrap(obj):
    if isinstance(obj, (staticmethod, classmethod)):
        return obj.__func__
    return obj


def _defs(mod):
    """``(qualname, function)`` for the public defs *this* module defines.

    Module scope and one class deep, which is the surface rung 05's gates
    read. A function another module defined is skipped wherever it is found
    -- it is that module's to annotate, and counting it here would record the
    same answer twice. The test has to be applied to a class's attributes and
    not only to module scope, because `typing` writes an `__init__` of its
    own into the `__dict__` of every `Protocol` subclass
    (`_no_init_or_replace_init`, a bare `(*args, **kwargs)` that carries no
    annotation and never will). Rung 05 declares protocols and a lineage is
    free to declare different ones, so counting those would make the answer
    say whose `Protocol`s they are rather than whether the tree is annotated
    -- five of them on one correct rung-07 tree and four, under other names,
    on another.
    """
    out = []
    for name in sorted(vars(mod)):
        obj = getattr(mod, name, None)
        if isinstance(obj, types.FunctionType):
            if obj.__module__ == mod.__name__ and _public(name):
                out.append((name, obj))
            continue
        if not isinstance(obj, type):
            continue
        if getattr(obj, "__module__", None) != mod.__name__:
            continue
        for attr in sorted(vars(obj)):
            fn = _unwrap(vars(obj)[attr])
            if isinstance(fn, types.FunctionType) and _public(attr) \
                    and fn.__module__ == mod.__name__:
                out.append(("%s.%s" % (name, attr), fn))
    return out


def _classes(mod):
    return [(name, obj) for name in sorted(vars(mod))
            for obj in [getattr(mod, name, None)]
            if isinstance(obj, type)
            and getattr(obj, "__module__", None) == mod.__name__
            and _public(name)]


def _import(name):
    import importlib
    return importlib.import_module(name)


def _err(exc):
    return "%s: %s" % (type(exc).__name__, exc)


@case("annotations.public-surface-is-objects")
def _(ctx):
    out = {}
    for name in MODULES:
        try:
            mod = _import(name)
        except Exception as e:              # pragma: no cover - measured clean
            out[name] = {"raised": _err(e)}
            continue
        stringised, failed, bare = [], {}, 0
        for qual, fn in _defs(mod):
            try:
                ann = dict(getattr(fn, "__annotations__", {}) or {})
            except Exception as e:
                # PEP 649 evaluates on this read, so a name that is not
                # importable at run time raises *here* and nowhere earlier.
                failed[qual] = _err(e)
                continue
            if not ann:
                bare += 1
                continue
            if any(isinstance(v, str) for v in ann.values()):
                stringised.append(qual)
        out[name] = {"fully_annotated": bare == 0,
                     "stringised": sorted(stringised),
                     "unreadable": failed}
    return out


@case("annotations.public-surface-resolves", tags=("adversarial",))
def _(ctx):
    import typing
    out = {}
    for name in MODULES:
        try:
            mod = _import(name)
        except Exception as e:              # pragma: no cover - measured clean
            out[name] = {"raised": _err(e)}
            continue
        failed, bare = {}, 0
        targets = list(_defs(mod)) + _classes(mod)
        for qual, obj in targets:
            try:
                hints = typing.get_type_hints(obj)
            except Exception as e:
                failed[qual] = _err(e)
                continue
            # A class with no annotated attributes is not a finding: rung 05
            # asks for annotated *defs*. The classes are here for the
            # exceptions `get_type_hints` raises on them, not for their hints.
            if not hints and not isinstance(obj, type):
                bare += 1
        out[name] = {"fully_hinted": bare == 0, "unresolved": failed}
    return out
