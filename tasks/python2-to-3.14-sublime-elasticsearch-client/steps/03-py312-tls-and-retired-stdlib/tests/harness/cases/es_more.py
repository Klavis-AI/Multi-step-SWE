# -*- coding: utf-8 -*-
"""The rest of ``elasticsearch.client.Elasticsearch``, driven over HTTP.

``es_client.py`` drives the endpoints the plugin itself calls. This module
drives the ones it does not, because the client module is one file and a
migration that breaks ``_make_path`` or ``query_params`` breaks all of them
together -- a suite that only exercises the called half would score half the
damage.

Two things these endpoints have that the plugin's own do not:

* **hand-written required-argument guards.** ``percolate``, ``count_percolate``,
  ``mlt``, ``termvectors``, ``termvector``, ``get_script`` and
  ``delete_script`` loop over their positional arguments and compare each
  against ``SKIP_IN_PATH``. The comparison is ``in``, i.e. ``==``, over
  ``(None, '', b'', [], ())`` -- and ``b'' == ''`` is True on Python 2 and
  False on Python 3. Every one of those guards is driven with each member of
  that tuple below, and the exact ``ValueError`` message is scored: this is the
  most concentrated 2/3 semantic difference left in the package.
* **path segments after the id.** ``'_percolate', 'count'`` and ``'_mlt'`` are
  appended *after* a user-supplied id, so an id that percent-encodes to
  something containing a slash moves the trailing segment. The ids below
  include one.

Every fixture returns a fixed document; nothing here reads the clock.
"""
from ..registry import case
from ..server import Route, json_route
from . import doc_escapes

DOC = {"took": 3, "_shards": {"total": 1, "successful": 1, "failed": 0},
       "hits": {"total": 1, "max_score": 1.0,
                "hits": [{"_index": "blog", "_type": "posts", "_id": "1",
                          "_score": 1.0, "_source": {"title": u"日本語"}}]}}

#: Each member of SKIP_IN_PATH, plus the two values that look like members but
#: are not (``0`` and ``False`` compare unequal to all five).
SKIPPY = [None, "", b"", [], (), 0, False]


def any_ok():
    return {"*": json_route(DOC)}


def _try(fn):
    try:
        return {"ok": fn()}
    except Exception as exc:
        return {"raised": type(exc).__name__, "str": str(exc)}


# ------------------------------------------------------------- search side --

@case("es.search-shards", fixture=lambda: {"*": json_route(
    {"nodes": {"n1": {"name": "node-1"}},
     "shards": [[{"state": "STARTED", "primary": True, "index": "blog"}]]})})
def _(ctx):
    es = ctx.es()
    return [es.search_shards(),
            es.search_shards(index="blog"),
            es.search_shards(index="blog", doc_type="posts"),
            es.search_shards(index=["a", u"ü"], doc_type="posts",
                             preference="_local", routing="r1",
                             local=True, allow_no_indices=False,
                             expand_wildcards="open")]


@case("es.search-exists-200",
      fixture=lambda: {"*": json_route({"exists": True})})
def _(ctx):
    return ctx.es().search_exists(index="blog", doc_type="posts",
                                  body={"query": {"match_all": {}}},
                                  min_score=0.5, q=u"title:寿司")


@case("es.search-exists-404", tags=("adversarial",),
      fixture=lambda: {"*": json_route({"exists": False}, status=404)})
def _(ctx):
    # search_exists has no 404-swallowing branch of its own -- unlike
    # ``exists``, which passes ignore=404. A 404 therefore has to surface as a
    # NotFoundError, and the log line the transport emits on the way is part of
    # the observation.
    return _try(lambda: ctx.es().search_exists(index="blog"))


@case("es.delete-by-query", fixture=lambda: {"*": json_route(
    {"_indices": {"blog": {"_shards": {"total": 1, "successful": 1,
                                       "failed": 0}}}})})
def _(ctx):
    es = ctx.es()
    return [es.delete_by_query(index="blog",
                               body={"query": {"match_all": {}}}),
            es.delete_by_query(index="blog", doc_type="posts",
                               body={"query": {"term": {"t": u"ü"}}},
                               consistency="one", routing="r",
                               timeout="10s", q=u"t:ü"),
            es.delete_by_query(index=["a", "b"], body=None)]


@case("es.delete-by-query-rejects", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    # The only endpoint in this module whose guard names the argument.
    es = ctx.es()
    return {repr(v): _try(lambda v=v: es.delete_by_query(index=v, body={}))
            for v in SKIPPY}


# ---------------------------------------------------------------- percolate --

@case("es.percolate", fixture=lambda: {"*": json_route(
    {"total": 2, "matches": [{"_index": "blog", "_id": "q1"}]})})
def _(ctx):
    es = ctx.es()
    return [es.percolate(index="blog", doc_type="posts",
                         body={"doc": {"title": u"寿司"}}),
            es.percolate(index="blog", doc_type="posts", id="1"),
            es.percolate(index="blog", doc_type="posts", id="1",
                         percolate_index="other", percolate_type="t",
                         version=3, routing="r", preference="_local")]


@case("es.percolate-id-with-slash", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    # '_percolate' is appended after the id, so what the id encodes to decides
    # where the trailing segment lands.
    return ctx.es().percolate(index="blog", doc_type="posts",
                              id=u"a/b?c 記事")


@case("es.percolate-rejects", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    es = ctx.es()
    out = {}
    for v in SKIPPY:
        out["index=%r" % (v,)] = _try(
            lambda v=v: es.percolate(index=v, doc_type="posts"))
        out["doc_type=%r" % (v,)] = _try(
            lambda v=v: es.percolate(index="blog", doc_type=v))
    # id is optional here: a skipped id must *not* raise, it must drop the
    # segment. That distinction is the whole point of the guard loop.
    out["id-optional"] = _try(
        lambda: es.percolate(index="blog", doc_type="posts", id=None))
    return out


@case("es.count-percolate", fixture=lambda: {"*": json_route({"total": 7})})
def _(ctx):
    es = ctx.es()
    return [es.count_percolate(index="blog", doc_type="posts",
                               body={"doc": {"title": "x"}}),
            es.count_percolate(index="blog", doc_type="posts", id="1",
                               percolate_index="pi", version_type="external",
                               ignore_unavailable=True)]


@case("es.count-percolate-rejects", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    es = ctx.es()
    return {"index=%r" % (v,): _try(
        lambda v=v: es.count_percolate(index=v, doc_type="posts"))
        for v in SKIPPY}


# ---------------------------------------------------------------------- mlt --

@case("es.mlt", fixture=any_ok)
def _(ctx):
    es = ctx.es()
    return [es.mlt(index="blog", doc_type="posts", id="1"),
            es.mlt(index="blog", doc_type="posts", id="1",
                   body={"docs": [{"_id": "2"}]},
                   mlt_fields=["title", "body"], min_term_freq=1,
                   max_query_terms=12, percent_terms_to_match=0.3,
                   boost_terms=1.5, include=True, search_size=5)]


@case("es.mlt-rejects", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    es = ctx.es()
    out = {}
    for v in SKIPPY:
        out["index=%r" % (v,)] = _try(
            lambda v=v: es.mlt(index=v, doc_type="posts", id="1"))
        out["id=%r" % (v,)] = _try(
            lambda v=v: es.mlt(index="blog", doc_type="posts", id=v))
    return out


# -------------------------------------------------------------- termvectors --

@case("es.termvectors", fixture=lambda: {"*": json_route(
    {"_index": "blog", "found": True,
     "term_vectors": {"title": {"terms": {u"寿司": {"term_freq": 1}}}}})})
def _(ctx):
    es = ctx.es()
    return [es.termvectors(index="blog", doc_type="posts", id="1"),
            es.termvectors(index="blog", doc_type="posts", id=0),
            es.termvectors(index="blog", doc_type="posts", id="1",
                           body={"fields": ["title"]},
                           field_statistics=True, offsets=False,
                           payloads=False, positions=True, term_statistics=True,
                           realtime=False, dfs=True, parent="p", routing="r")]


@case("es.termvector-singular", fixture=any_ok)
def _(ctx):
    # The 1.x deprecated spelling, kept as a separate method with a required
    # id and a *different* trailing segment ('_termvector', no 's').
    return ctx.es().termvector(index="blog", doc_type="posts", id="1",
                               body={"fields": ["title"]}, offsets=True)


@case("es.termvectors-rejects", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    es = ctx.es()
    out = {}
    for v in SKIPPY:
        out["termvectors doc_type=%r" % (v,)] = _try(
            lambda v=v: es.termvectors(index="blog", doc_type=v, id="1"))
        out["termvectors id=%r" % (v,)] = _try(
            lambda v=v: es.termvectors(index="blog", doc_type="posts", id=v))
        out["termvector id=%r" % (v,)] = _try(
            lambda v=v: es.termvector(index="blog", doc_type="posts", id=v))
    # A *latent defect*, pinned so a migration cannot quietly repair it:
    # termvectors' signature says ``id=None`` -- the whole point of the plural
    # spelling in ES 1.x is a whole-index request -- but its guard loop
    # includes id, so the documented default raises. The two methods are
    # therefore identical except for the trailing path segment. Scoring the
    # ValueError keeps that visible.
    out["default-id-raises"] = _try(
        lambda: es.termvectors(index="blog", doc_type="posts"))
    out["same_guard"] = (
        es.termvectors.__doc__ == es.termvector.__doc__)
    return out


@case("es.mtermvectors", fixture=any_ok)
def _(ctx):
    es = ctx.es()
    return [es.mtermvectors(body={"docs": [{"_id": "1"}, {"_id": u"２"}]}),
            es.mtermvectors(index="blog", body={"ids": ["1", "2"]}),
            es.mtermvectors(index="blog", doc_type="posts",
                            ids=["1", "2"], fields=["title", "body"],
                            term_statistics=True, realtime=True)]


# ------------------------------------------------------------ scripts side --

@case("es.get-script", fixture=lambda: {"*": json_route(
    {"lang": "groovy", "_id": "s1", "found": True,
     "script": "doc['n'].value * 2"})})
def _(ctx):
    es = ctx.es()
    return [es.get_script(lang="groovy", id="s1"),
            es.get_script(lang="groovy", id="s1", version=2,
                          version_type="external")]


@case("es.delete-script", fixture=lambda: {"*": json_route(
    {"found": True, "_id": "s1", "_version": 2})})
def _(ctx):
    es = ctx.es()
    return [es.delete_script(lang="groovy", id="s1"),
            es.delete_script(lang=u"groovy", id=u"スクリプト")]


@case("es.script-rejects", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    es = ctx.es()
    out = {}
    for v in SKIPPY:
        out["get lang=%r" % (v,)] = _try(
            lambda v=v: es.get_script(lang=v, id="s1"))
        out["get id=%r" % (v,)] = _try(
            lambda v=v: es.get_script(lang="groovy", id=v))
        out["delete lang=%r" % (v,)] = _try(
            lambda v=v: es.delete_script(lang=v, id="s1"))
    return out


@case("es.template-roundtrip", fixture=lambda: {"*": json_route(
    {"found": True, "_id": "t1",
     "template": {"query": {"match": {"t": "{{q}}"}}}})})
def _(ctx):
    es = ctx.es()
    return [es.put_template(id="t1", body={"template": {"query": {}}}),
            es.get_template(id="t1"),
            es.delete_template(id="t1"),
            # delete_template's id defaults to None and it has *no* guard, so
            # this one builds '/_search/template' and sends it. A migration
            # that "tidies" the missing guard into existence changes it.
            es.delete_template()]


@case("es.template-rejects", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    es = ctx.es()
    return {"get id=%r" % (v,): _try(lambda v=v: es.get_template(id=v))
            for v in SKIPPY}


# --------------------------------------------------------- param machinery --

@case("es.ignore-and-request-timeout", tags=("adversarial",),
      fixture=lambda: {"*": json_route({"error": "gone"}, status=404)})
def _(ctx):
    # query_params pulls 'ignore' and 'request_timeout' out *without* running
    # them through _escape, so they stay Python objects all the way into the
    # connection. Everything else is escaped. The list form of ignore is the
    # one that has to survive.
    es = ctx.es()
    return {"single": _try(lambda: es.get(index="blog", doc_type="posts",
                                          id="1", ignore=404)),
            "tuple": _try(lambda: es.get(index="blog", doc_type="posts",
                                         id="1", ignore=(404, 410))),
            "wrong": _try(lambda: es.get(index="blog", doc_type="posts",
                                         id="1", ignore=409)),
            "timeout": _try(lambda: es.get(index="blog", doc_type="posts",
                                           id="1", ignore=404,
                                           request_timeout=5))}


@case("es.global-params", fixture=any_ok)
def _(ctx):
    # pretty/format/filter_path are accepted by every method without being
    # listed, via GLOBAL_PARAMS.
    return ctx.es().search(index="blog", body=None, pretty=True,
                           format="yaml", filter_path=["took", "hits.total"])


@case("es.unknown-kwarg", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    # A parameter query_params does not know about is not silently dropped
    # into the query string -- it reaches the method as a keyword argument and
    # blows up there. Pinning the TypeError keeps a migration from turning a
    # typo into a silent no-op.
    return _try(lambda: ctx.es().search(index="blog", not_a_param=1))


@case("es.method-metadata", fixture=any_ok)
def _(ctx):
    # query_params uses functools.wraps, so every public method must keep its
    # own name and docstring. A migration that re-decorates by hand loses this
    # and nothing else notices.
    from elasticsearch import Elasticsearch
    es = Elasticsearch()
    names = sorted(n for n in dir(es)
                   if not n.startswith("_") and callable(getattr(es, n)))
    out = {"methods": names}
    detail = {}
    for n in names:
        fn = getattr(es, n)
        doc = getattr(fn, "__doc__", None)
        detail[n] = {"name": getattr(fn, "__name__", None),
                     "doc_first_line": (doc.strip().splitlines()[0]
                                        if doc and doc.strip() else None)}
    out["detail"] = detail
    out["namespaces"] = sorted(
        n for n in ("indices", "cluster", "nodes", "snapshot", "cat")
        if hasattr(es, n))
    return out


@case("es.repr-and-hosts", fixture=any_ok)
def _(ctx):
    # __repr__ slices the first five connections off the pool and sorts their
    # reprs; on a py2/py3 boundary the sort key and the truncation marker are
    # both easy to get wrong.
    from elasticsearch import Elasticsearch
    return {"one": repr(Elasticsearch()),
            "several": repr(Elasticsearch([{"host": "h%d" % i, "port": 9200}
                                           for i in range(3)])),
            "over_five": repr(Elasticsearch([{"host": "h%d" % i,
                                              "port": 9200}
                                             for i in range(7)])),
            "url_form": repr(Elasticsearch("https://user:pw@es.example:9243"))}


@case("es.delayed-response", fixture=lambda: {"*": Route(
    status=200, body=b'{"acknowledged":true}', delay=0.25,
    headers=[("content-type", "application/json")])})
def _(ctx):
    # A response slow enough to exercise the timing path in the logger without
    # letting a wall-clock value into the observation: normalize.py collapses
    # the "request:0.2xxs" the ES logger emits to "<DUR>s".
    return ctx.es().search_shards(index="blog")


@case("es.docstring-escapes-intact", tags=("adversarial",))
def _(ctx):
    # The 24th escaped docstring, and the only one outside the two vendored
    # HTTP stacks: ``Elasticsearch.search`` documents its ``from_`` argument as
    # ``from\_`` because a trailing underscore is a link reference in RST.
    # ``query_params`` copies the docstring through ``functools.wraps``, so
    # this reads the wrapper -- which is what a caller sees -- rather than the
    # undecorated function.
    from elasticsearch import Elasticsearch
    return {"Elasticsearch.search": doc_escapes(Elasticsearch.search)}
