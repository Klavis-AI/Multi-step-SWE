# -*- coding: utf-8 -*-
"""How the tree announces its own deprecated API.

Four functions in this tree are documented as deprecated and announce nothing;
two announce by hand, from inside their own bodies; and one reaches for
``Logger.warn``, an alias the standard library has been deprecating since 3.3.
PEP 702's ``warnings.deprecated`` is new in 3.13 -- it does not exist on 3.12 --
and it is the decorator that replaces all three spellings at once.

What these cases observe, for each such function:

``value``
    what it returned, so that a rung cannot satisfy the announcement by
    breaking the function.
``warnings``
    every warning the call raised, as ``(category, where)`` -- ``where`` being
    ``"tree"`` when the warning names a file inside the tree and ``"caller"``
    when it names this file. That distinction is the whole of PEP 702's
    stacklevel handling: a hand-rolled ``warnings.warn`` in a function body
    blames the library, the decorator blames whoever called it. The *count* is
    observed too, which is what stops a rung adding the decorator on top of the
    hand-rolled call and collecting both.
``deprecated``
    whether ``__deprecated__`` is set on the object, which is the attribute
    PEP 702 specifies and the one a type checker reads.

The message text is **not** recorded. There is no wording these
functions must use, so recording one would be scoring a guess; what is recorded
is that a message exists, which the decorator cannot be applied without.

Two cases here observe the opposite property -- that ordinary traffic stays
silent. ``urllib3.util.url.get_host`` is deprecated *and* called from
``connectionpool.is_same_host`` and ``connection_from_url``, so a rung that
decorates it without rerouting those callers makes urllib3 warn about urllib3
on every absolute-URL request. Those cases are recorded empty and must stay
empty, at this rung and at every rung after it.
"""
import datetime
import os
import warnings

from ..registry import case
from ..server import json_route

#: The same frozen stamp analytics_api.py uses, for the same reason: a
#: ``timestamp=None`` here would read the clock, and the two-pass recording
#: drops anything that disagrees with itself.
TS = datetime.datetime(2015, 6, 1, 12, 30, 45)


def _where(ctx, filename):
    tree = os.path.abspath(ctx.tree) + os.sep
    return "tree" if os.path.abspath(filename).startswith(tree) else "caller"


def _observe(ctx, fn, call):
    """Call ``call`` and record what ``fn`` announced while it ran."""
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        try:
            value = {"ok": call()}
        except Exception as exc:
            value = {"raised": type(exc).__name__, "str": str(exc)}
    dep = getattr(fn, "__deprecated__", None)
    return {
        "value": value,
        "warnings": [{"category": w.category.__name__,
                      "where": _where(ctx, w.filename),
                      "has_message": bool(str(w.message))}
                     for w in caught],
        "deprecated": isinstance(dep, str) and bool(dep),
    }


def _silent(ctx, call):
    """Record every warning ``call`` raised. The point is that there are none."""
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        try:
            value = {"ok": call()}
        except Exception as exc:
            value = {"raised": type(exc).__name__, "str": str(exc)}
    return {"value": value,
            "warnings": sorted({"%s@%s" % (w.category.__name__,
                                           _where(ctx, w.filename))
                                for w in caught})}


# ------------------------------------------------ requests.utils, by hand ---

@case("requests.deprecated-utils-announce")
def _(ctx):
    # Both of these warn from inside their own bodies, so today the warning
    # blames `requests/utils.py` rather than the caller, and neither object
    # carries `__deprecated__`.
    import requests.utils as ru
    html = u'<meta charset="utf-8">'.encode("ascii")

    class _Resp(object):
        encoding = None
        content = html
        headers = {"content-type": "text/html"}

    return {
        "get_encodings_from_content": _observe(
            ctx, ru.get_encodings_from_content,
            lambda: ru.get_encodings_from_content(html.decode("ascii"))),
        "get_unicode_from_response": _observe(
            ctx, ru.get_unicode_from_response,
            lambda: ru.get_unicode_from_response(_Resp())),
    }


# ------------------------------------- urllib3, documented but announcing ---
#
# Both vendored copies, one case each. They are byte-identical today and each
# is imported by a different half of the tree, so a rung that marks one and
# forgets the other passes exactly one of these two.

def _urllib3_surface(ctx, mod):
    get_host = mod("util.url").get_host
    iter_fields = mod("filepost").iter_fields
    return {
        "get_host": _observe(ctx, get_host,
                             lambda: get_host("http://example.com:8080/p?q")),
        "iter_fields_dict": _observe(
            ctx, iter_fields, lambda: sorted(iter_fields({"a": "1"}))),
        "iter_fields_pairs": _observe(
            ctx, iter_fields, lambda: list(iter_fields([("a", "1")]))),
    }


def _importer(root):
    def mod(name):
        import importlib
        return importlib.import_module("%s.%s" % (root, name))
    return mod


@case("urllib3.deprecated-surface-announces")
def _(ctx):
    return _urllib3_surface(ctx, _importer("urllib3"))


@case("urllib3v.deprecated-surface-announces")
def _(ctx):
    return _urllib3_surface(ctx, _importer("requests.packages.urllib3"))


# ---------------------------------------- ... and its own callers, silent ---

def _internal_traffic(ctx, root):
    import importlib
    u3 = importlib.import_module(root)
    pool_mod = importlib.import_module("%s.connectionpool" % root)
    base = ctx.url("")
    out = {}
    out["connection_from_url"] = _silent(
        ctx, lambda: type(u3.connection_from_url(base)).__name__)
    pool = pool_mod.HTTPConnectionPool(ctx.fx.host, ctx.fx.port)
    # assert_same_host defaults to True and an absolute URL is what makes
    # urlopen check it, which is the call that reaches get_host.
    out["urlopen_absolute"] = _silent(
        ctx, lambda: pool.urlopen("GET", ctx.url("/x")).status)
    out["is_same_host"] = _silent(ctx, lambda: [pool.is_same_host(ctx.url("/x")),
                                                pool.is_same_host("/x"),
                                                pool.is_same_host("http://elsewhere/x")])
    pool.close()
    return out


@case("urllib3.internal-callers-stay-silent", tags=("adversarial",),
      fixture=lambda: {"*": json_route({"ok": True})})
def _(ctx):
    return _internal_traffic(ctx, "urllib3")


@case("urllib3v.internal-callers-stay-silent", tags=("adversarial",),
      fixture=lambda: {"*": json_route({"ok": True})})
def _(ctx):
    return _internal_traffic(ctx, "requests.packages.urllib3")


@case("requests.internal-callers-stay-silent", tags=("adversarial",),
      fixture=lambda: {"*": json_route({"ok": True})})
def _(ctx):
    # The same property one layer up: a plain `requests.get` must not start
    # announcing urllib3's deprecations at its caller either.
    import requests
    return _silent(ctx, lambda: requests.get(ctx.url("/x")).json())


# --------------------------------------------------- logging's own alias ----

@case("analytics.queue-full-logging-alias", tags=("adversarial",))
def _(ctx):
    # `Client._enqueue`'s full-queue branch calls `self.log.warn`. The log
    # record it produces is identical either way -- `Logger.warn` forwards to
    # `Logger.warning` -- so `analytics.queue-full-warns` beside this one
    # cannot see the difference. What differs is the DeprecationWarning the
    # alias itself raises, and it is raised at the frame that called it, which
    # is inside the tree.
    from analytics.client import Client
    c = Client(write_key="test-key", send=False, max_queue_size=1)
    c.track(user_id="u", event="one", timestamp=TS)
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        ok, _msg = c.track(user_id="u", event="two", timestamp=TS)
    return {"second_ok": ok, "qsize": c.queue.qsize(),
            "warnings": sorted({"%s@%s" % (w.category.__name__,
                                           _where(ctx, w.filename))
                                for w in caught})}
