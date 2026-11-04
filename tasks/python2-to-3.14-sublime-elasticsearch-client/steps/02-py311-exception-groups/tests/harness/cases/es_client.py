# -*- coding: utf-8 -*-
"""elasticsearch.Elasticsearch driven over real HTTP against the fixture.

These are the cases that pin the *wire*: the exact request line, the
percent-encoding of non-ASCII index and document ids (``client/utils._escape``
encodes to UTF-8 bytes before ``quote_plus``, which is the tree's most
load-bearing 2/3 behaviour), the query-string ordering, and the body bytes.
"""
from ..registry import case
from ..server import Route, json_route, gzip_route


def _try(fn):
    """Record one probe's value or the exception it raised.

    Per probe rather than per case: a single latent defect must not delete the
    evidence for everything recorded beside it.
    """
    try:
        return {"ok": fn()}
    except Exception as exc:
        return {"raised": type(exc).__name__, "str": str(exc)}

HIT = {
    "took": 5,
    "timed_out": False,
    "_shards": {"total": 1, "successful": 1, "failed": 0},
    "hits": {"total": 1, "max_score": 1.0,
             "hits": [{"_index": "blog", "_type": "posts", "_id": "1",
                       "_score": 1.0, "_source": {"title": u"日本語"}}]},
}


def any_ok():
    return {"*": json_route(HIT)}


def info_fixture():
    return {"/": json_route({"status": 200, "name": "Fixture",
                             "version": {"number": "1.7.0"},
                             "tagline": "You Know, for Search"})}


def error_fixture(status, payload):
    def _f():
        return {"*": json_route(payload, status=status)}
    return _f


# ------------------------------------------------------------- happy path --

@case("es.info", fixture=info_fixture)
def _(ctx):
    return ctx.es().info()


@case("es.ping-ok", fixture=info_fixture)
def _(ctx):
    return ctx.es().ping()


@case("es.ping-500", fixture=error_fixture(500, {"error": "boom"}))
def _(ctx):
    # ping swallows TransportError and returns False; the log record it emits
    # on the way is part of the observation.
    return ctx.es().ping()


@case("es.ping-false-after-retries",
      fixture=error_fixture(503, {"error": "unavailable"}))
def _(ctx):
    # ping's handler is what stands between the transport's retry loop and the
    # caller: three attempts go out, the loop gives up, and the caller is still
    # told False rather than being handed the failure. The wire log counts the
    # attempts; the return value is the contract.
    return ctx.es(max_retries=2).ping()


@case("es.ping-false-while-info-gets-the-group", tags=("adversarial",),
      fixture=error_fixture(503, {"error": "unavailable"}))
def _(ctx):
    # Both halves of ping's contract in one observation: the same three
    # attempts run twice against the same fixture, once through the handler
    # that swallows them and once through a caller that does not. The suite
    # already has each half on its own -- ``es.ping-false-after-retries`` and
    # the transport's own retry cases -- but separately neither says that the
    # handler standing in front of the loop has to keep swallowing *after* the
    # loop's answer changes shape. One expectation holding both does.
    answered = ctx.es(max_retries=2).ping()
    raised = None
    try:
        ctx.es(max_retries=2).info()
    except Exception as exc:
        raised = exc
    return {"ping": answered, "info_raised": raised}


@case("es.search-match-all", fixture=any_ok)
def _(ctx):
    return ctx.es().search(index="blog", doc_type="posts",
                           body={"query": {"match_all": {}}})


@case("es.search-no-index", fixture=any_ok)
def _(ctx):
    return ctx.es().search(body={"query": {"match_all": {}}})


@case("es.search-unicode-index", fixture=any_ok)
def _(ctx):
    return ctx.es().search(index=u"日本語", doc_type=u"記事",
                           body={"query": {"match": {"title": u"寿司"}}})


@case("es.search-index-list", fixture=any_ok)
def _(ctx):
    return ctx.es().search(index=["a", "b", u"ü"], doc_type=("t1", "t2"),
                           body={"query": {"match_all": {}}})


@case("es.search-index-wildcard", fixture=any_ok)
def _(ctx):
    # ',' and '*' are deliberately left unquoted by _make_path.
    return ctx.es().search(index="logs-*,archive-*", body=None)


@case("es.search-params", fixture=any_ok)
def _(ctx):
    return ctx.es().search(index="blog", body={"query": {"match_all": {}}},
                           size=10, from_=5, sort="date:desc",
                           _source=["title", "body"])


@case("es.search-bool-param", fixture=any_ok)
def _(ctx):
    # _escape turns a bool into 'true'/'false', not 'True'/'False'.
    return ctx.es().search(index="blog", explain=True, version=False,
                           body={"query": {"match_all": {}}})


@case("es.search-date-param", fixture=any_ok)
def _(ctx):
    import datetime
    return ctx.es().search(index="blog", timeout=datetime.datetime(
        2016, 7, 8, 1, 2, 3), body={"query": {"match_all": {}}})


@case("es.search-slash-in-id", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    return ctx.es().get(index="blog", doc_type="posts", id="a/b?c#d e+f")


@case("es.search-unicode-id", fixture=any_ok)
def _(ctx):
    return ctx.es().get(index=u"ブログ", doc_type="posts", id=u"記事/１")


@case("es.index-doc", fixture=any_ok)
def _(ctx):
    return ctx.es().index(index="blog", doc_type="posts", id=1,
                          body={"title": u"café", "n": 2 ** 40})


@case("es.index-autogen-id", fixture=any_ok)
def _(ctx):
    return ctx.es().index(index="blog", doc_type="posts",
                          body={"title": "no id"})


@case("es.create-doc", fixture=any_ok)
def _(ctx):
    return ctx.es().create(index="blog", doc_type="posts", id="x",
                           body={"a": 1}, refresh=True)


@case("es.update-doc", fixture=any_ok)
def _(ctx):
    return ctx.es().update(index="blog", doc_type="posts", id="1",
                           body={"doc": {"title": u"新"}}, retry_on_conflict=3)


@case("es.delete-doc", fixture=any_ok)
def _(ctx):
    return ctx.es().delete(index="blog", doc_type="posts", id="1")


@case("es.exists-true", fixture=lambda: {"*": Route(status=200)})
def _(ctx):
    return ctx.es().exists(index="blog", id="1", doc_type="posts")


@case("es.exists-false", fixture=lambda: {"*": Route(status=404)})
def _(ctx):
    return ctx.es().exists(index="blog", id="1", doc_type="posts")


@case("es.get-source", fixture=any_ok)
def _(ctx):
    return ctx.es().get_source(index="blog", id="1", doc_type="posts")


@case("es.mget", fixture=any_ok)
def _(ctx):
    return ctx.es().mget(body={"docs": [{"_id": "1"}, {"_id": u"２"}]},
                         index="blog", doc_type="posts")


@case("es.count", fixture=lambda: {"*": json_route({"count": 42,
                                                    "_shards": {"total": 1}})})
def _(ctx):
    return ctx.es().count(index="blog", body={"query": {"match_all": {}}})


@case("es.scroll", fixture=any_ok)
def _(ctx):
    return ctx.es().scroll(scroll_id="c2Nhbjs2OzM0NDg1OTpz", scroll="5m")


@case("es.clear-scroll", fixture=any_ok)
def _(ctx):
    return ctx.es().clear_scroll(scroll_id="abc")


@case("es.explain", fixture=any_ok)
def _(ctx):
    return ctx.es().explain(index="blog", doc_type="posts", id="1",
                            body={"query": {"match": {"title": u"寿司"}}})


@case("es.suggest", fixture=any_ok)
def _(ctx):
    return ctx.es().suggest(body={"s": {"text": u"süshi",
                                        "term": {"field": "title"}}},
                            index="blog")


@case("es.search-template", fixture=any_ok)
def _(ctx):
    return ctx.es().search_template(index="blog",
                                    body={"template": {"file": "t"},
                                          "params": {"q": u"ü"}})


@case("es.put-script", fixture=any_ok)
def _(ctx):
    return ctx.es().put_script(lang="groovy", id="s1",
                               body={"script": "1 + 1"})


@case("es.field-stats", fixture=any_ok)
def _(ctx):
    return ctx.es().field_stats(index="blog", fields=["title", "date"])


# ----------------------------------------------------------- bulk bodies --

@case("es.bulk-list-of-actions", fixture=any_ok)
def _(ctx):
    return ctx.es().bulk(body=[
        {"index": {"_index": "blog", "_type": "posts", "_id": "1"}},
        {"title": u"日本語"},
        {"delete": {"_index": "blog", "_type": "posts", "_id": "2"}},
    ])


@case("es.bulk-string-body", fixture=any_ok)
def _(ctx):
    return ctx.es().bulk(body=u'{"index":{"_id":"1"}}\n{"t":"café"}\n')


@case("es.bulk-string-body-no-trailing-newline", tags=("adversarial",),
      fixture=any_ok)
def _(ctx):
    return ctx.es().bulk(body=u'{"index":{"_id":"1"}}\n{"t":"x"}')


@case("es.bulk-bytes-body", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    # ``compat.string_types`` is ``(str, bytes)`` on Python 3, so _bulk_body
    # takes a bytes body down the "already a string" branch and then asks it
    # ``endswith('\n')`` with a str argument. On Python 2 both halves were the
    # same type and it worked; on 3 it is a TypeError, and every rung of this
    # ladder is Python 3. Pinned rather than repaired: the fix is a one-line
    # change to a widely-called method, and which of the three plausible
    # spellings is chosen decides whether a bytes body is ever accepted.
    es = ctx.es()
    out = {"bytes": _try(
        lambda: es.bulk(body=b'{"index":{"_id":"1"}}\n{"t":"x"}\n'))}
    # The two documented spellings are the control. The str body is short a
    # trailing newline on purpose: supplying it is _bulk_body's other job.
    out["str_no_newline"] = _try(
        lambda: es.bulk(body='{"index":{"_id":"1"}}\n{"t":"x"}'))
    out["sequence"] = _try(
        lambda: es.bulk(body=[{"index": {"_id": "1"}}, {"t": u"日本語"}]))
    out["sent"] = [rec["body"] for rec in ctx.fx.wire()]
    return out


@case("es.msearch", fixture=any_ok)
def _(ctx):
    return ctx.es().msearch(body=[{"index": "blog"},
                                  {"query": {"match_all": {}}}])


@case("es.mpercolate", fixture=any_ok)
def _(ctx):
    return ctx.es().mpercolate(body=[{"percolate": {"index": "blog"}},
                                     {"doc": {"t": u"ü"}}], index="blog")


# ---------------------------------------------------------- error mapping --

@case("es.error-404", tags=("adversarial",),
      fixture=error_fixture(404, {"error": "IndexMissingException[[x] missing]",
                                  "status": 404}))
def _(ctx):
    return ctx.es().search(index="x", body={"query": {"match_all": {}}})


@case("es.error-400", tags=("adversarial",),
      fixture=error_fixture(400, {"error": {"type": "parsing_exception",
                                            "reason": "bad"}, "status": 400}))
def _(ctx):
    return ctx.es().search(index="x", body={"query": {"bogus": {}}})


@case("es.error-401", tags=("adversarial",),
      fixture=error_fixture(401, {"error": "AuthenticationException"}))
def _(ctx):
    return ctx.es().info()


@case("es.error-403", tags=("adversarial",),
      fixture=error_fixture(403, {"error": "forbidden"}))
def _(ctx):
    return ctx.es().info()


@case("es.error-409", tags=("adversarial",),
      fixture=error_fixture(409, {"error": "VersionConflictEngineException"}))
def _(ctx):
    return ctx.es().create(index="b", doc_type="p", id="1", body={"a": 1})


@case("es.error-500-non-json", tags=("adversarial",),
      fixture=lambda: {"*": Route(status=500, body=b"<html>oops</html>",
                                  headers=[("content-type", "text/html")])})
def _(ctx):
    return ctx.es().info()


@case("es.error-ignored-404", fixture=error_fixture(404, {"error": "missing"}))
def _(ctx):
    return ctx.es().search(index="x", body={"query": {"match_all": {}}},
                           ignore=404)


@case("es.error-ignored-tuple", fixture=error_fixture(400, {"error": "bad"}))
def _(ctx):
    return ctx.es().search(index="x", body=None, ignore=(400, 404))


@case("es.error-body-not-json-but-200", tags=("adversarial",),
      fixture=lambda: {"*": Route(status=200, body=b"plain text",
                                  headers=[("content-type", "text/plain")])})
def _(ctx):
    return ctx.es().info()


@case("es.error-unknown-content-type", tags=("adversarial",),
      fixture=lambda: {"*": Route(status=200, body=b"\x00\x01",
                                  headers=[("content-type",
                                            "application/x-msgpack")])})
def _(ctx):
    return ctx.es().info()


@case("es.empty-body-200", tags=("adversarial",),
      fixture=lambda: {"*": Route(status=200, body=b"")})
def _(ctx):
    return ctx.es().info()


# ------------------------------------------------------- transport wiring --

@case("es.gzip-response", fixture=lambda: {"*": gzip_route(HIT)})
def _(ctx):
    return ctx.es().search(index="blog", body={"query": {"match_all": {}}})


@case("es.chunked-response", fixture=lambda: {"*": json_route(HIT, chunked=True)})
def _(ctx):
    return ctx.es().search(index="blog", body={"query": {"match_all": {}}})


@case("es.custom-headers-connection", fixture=any_ok)
def _(ctx):
    # The repo's own connection class, built the way commands/base.py builds it.
    client = ctx.es_custom_headers({"X-Api-Key": "k", "X-Trace": u"日本"})
    return client.search(index=u"日本語", doc_type="posts",
                         body={"query": {"match_all": {}}})


@case("es.custom-headers-overwrites-basic-auth", tags=("adversarial",),
      fixture=any_ok)
def _(ctx):
    client = ctx.es_custom_headers({"authorization": "Bearer t"},
                                   http_auth=("u", "p"))
    return client.info()


@case("es.send-get-body-as-post", fixture=any_ok)
def _(ctx):
    return ctx.es(send_get_body_as="POST").search(
        index="blog", body={"query": {"match_all": {}}})


@case("es.send-get-body-as-source", fixture=any_ok)
def _(ctx):
    return ctx.es(send_get_body_as="source").search(
        index="blog", body={"query": {"match": {"t": u"ü"}}})


@case("es.send-get-body-as-get", fixture=any_ok)
def _(ctx):
    return ctx.es(send_get_body_as="GET").search(
        index="blog", body={"query": {"match_all": {}}})


@case("es.requests-connection", fixture=any_ok)
def _(ctx):
    from elasticsearch import RequestsHttpConnection
    return ctx.es(connection_class=RequestsHttpConnection).search(
        index=u"日本語", body={"query": {"match_all": {}}})


@case("es.requests-connection-error", tags=("adversarial",),
      fixture=error_fixture(503, {"error": "unavailable"}))
def _(ctx):
    from elasticsearch import RequestsHttpConnection
    return ctx.es(connection_class=RequestsHttpConnection,
                  max_retries=0).info()


@case("es.url-prefix", fixture=any_ok)
def _(ctx):
    return ctx.es(url_prefix="/es").search(index="blog", body=None)


@case("es.http-auth-tuple", fixture=any_ok)
def _(ctx):
    return ctx.es(http_auth=("user", "pa:ss")).info()


@case("es.http-auth-string", fixture=any_ok)
def _(ctx):
    return ctx.es(http_auth="user:pa:ss").info()


@case("es.http-auth-unicode", tags=("adversarial",), fixture=any_ok)
def _(ctx):
    # urllib3.make_headers builds the Basic header through six.b(), which is a
    # latin-1 encode on Python 3, so a non-ASCII credential cannot be sent at
    # all -- the client raises while constructing the connection, before any
    # socket is opened. Pinned rather than repaired: urllib3 learnt to encode
    # the same credential as UTF-8 several releases later, so a migration that
    # swapped the vendored copy out would silently start authenticating where
    # it used to fail. That is a decision to take deliberately, not a tidy-up.
    out = {"non_ascii": _try(
        lambda: ctx.es(http_auth=(u"ユーザ", u"パス")).info())}
    out["nothing_sent"] = len(ctx.fx.seen)
    # The ASCII credential is the control: same builder, same call, and the
    # header it produces has to survive byte for byte.
    out["ascii"] = _try(lambda: ctx.es(http_auth=("user", "pw")).info())
    out["authorization"] = sorted(
        v for rec in ctx.fx.seen for k, v in rec["headers"]
        if k == "authorization")
    return out


@case("es.repr-normalize-hosts")
def _(ctx):
    from elasticsearch import Elasticsearch
    return [repr(Elasticsearch(h)) for h in (
        None,
        "localhost",
        "localhost:9201",
        "https://user:pw@example.com:9243/prefix",
        "http://example.com",
        ["a:1", "b:2"],
        [{"host": "h", "port": 1}],
    )]


@case("es.normalize-hosts-scheme-mismatch", tags=("adversarial",))
def _(ctx):
    from elasticsearch import Elasticsearch
    return repr(Elasticsearch("thrift://example.com:9500"))


@case("es.bulk-body-helper-direct")
def _(ctx):
    from elasticsearch import Elasticsearch
    c = Elasticsearch()
    return [c._bulk_body([{"a": 1}, {"b": u"ü"}]),
            c._bulk_body(u"already\n"),
            c._bulk_body(u"no newline")]
