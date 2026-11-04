# -*- coding: utf-8 -*-
"""elasticsearch.helpers — bulk, scan, reindex.

The helpers are where the tree's iterator protocol shows: ``streaming_bulk``
builds on ``compat.map`` (``itertools.imap`` on 2, the builtin on 3) and then
calls ``islice`` on it, and ``scan`` is a generator whose early ``return``
inside a loop is Python-3-only spelling. Both are exercised to exhaustion so
the migration cannot change where a chunk boundary falls.
"""
from ..registry import case
from ..server import Route, json_route


def _bulk_resp(items):
    return {"took": 3, "errors": any(
        not (200 <= list(i.values())[0].get("status", 500) < 300) for i in items),
        "items": items}


def _ok_item(op="index", _id="1", status=200):
    return {op: {"_index": "blog", "_type": "posts", "_id": _id,
                 "_version": 1, "status": status}}


def _err_item(op="index", _id="2", status=409):
    return {op: {"_index": "blog", "_type": "posts", "_id": _id,
                 "status": status,
                 "error": "VersionConflictEngineException[[blog][0]]"}}


def bulk_ok():
    return {"*/_bulk": json_route(_bulk_resp([_ok_item(_id=str(i))
                                             for i in range(1, 6)]))}


def bulk_mixed():
    return {"*/_bulk": json_route(_bulk_resp(
        [_ok_item(_id="1"), _err_item(_id="2"), _ok_item(_id="3")]))}


def bulk_503():
    return {"*/_bulk": json_route({"error": "unavailable"}, status=503)}


HITS = [{"_index": "blog", "_type": "posts", "_id": str(i),
         "_score": None, "_source": {"t": u"ドキュメント%d" % i}}
        for i in range(1, 4)]


def scan_fixture():
    shards = {"total": 2, "successful": 2, "failed": 0}
    first = json_route({"_scroll_id": "S1", "_shards": shards, "took": 1,
                        "hits": {"total": 3, "max_score": None, "hits": []}})
    page1 = json_route({"_scroll_id": "S2", "_shards": shards,
                        "hits": {"total": 3, "max_score": None, "hits": HITS}})
    page2 = json_route({"_scroll_id": "S3", "_shards": shards,
                        "hits": {"total": 3, "max_score": None, "hits": []}})
    return {"*/_search": first, "*/_search/scroll": [page1, page2]}


def scan_failed_shards():
    shards_bad = {"total": 2, "successful": 1, "failed": 1}
    first = json_route({"_scroll_id": "S1", "took": 1,
                        "_shards": {"total": 2, "successful": 2, "failed": 0},
                        "hits": {"total": 3, "max_score": None, "hits": []}})
    page1 = json_route({"_scroll_id": "S2", "_shards": shards_bad,
                        "hits": {"total": 3, "max_score": None, "hits": HITS}})
    # A terminating second page: with raise_on_error=False the generator keeps
    # scrolling after the shard failure, and a fixture that repeated page1
    # forever would hang rather than be scored.
    page2 = json_route({"_scroll_id": "S3", "_shards": shards_bad,
                        "hits": {"total": 3, "max_score": None, "hits": []}})
    return {"*/_search": first, "*/_search/scroll": [page1, page2]}


def reindex_fixture():
    routes = scan_fixture()
    routes["*/_bulk"] = json_route(_bulk_resp([_ok_item(_id=str(i))
                                              for i in range(1, 4)]))
    return routes


# ------------------------------------------------------- expand_action ----

@case("helpers.expand-action-index")
def _(ctx):
    from elasticsearch.helpers import expand_action
    return expand_action({"_index": "blog", "_type": "posts", "_id": 1,
                          "title": u"日本語"})


@case("helpers.expand-action-source-key")
def _(ctx):
    from elasticsearch.helpers import expand_action
    return expand_action({"_index": "blog", "_id": 1,
                          "_source": {"title": "t"}, "ignored": True})


@case("helpers.expand-action-delete")
def _(ctx):
    from elasticsearch.helpers import expand_action
    return expand_action({"_op_type": "delete", "_index": "blog", "_id": 7})


@case("helpers.expand-action-update")
def _(ctx):
    from elasticsearch.helpers import expand_action
    return expand_action({"_op_type": "update", "_id": 7,
                          "_retry_on_conflict": 2, "doc": {"a": 1}})


@case("helpers.expand-action-all-meta")
def _(ctx):
    from elasticsearch.helpers import expand_action
    return expand_action({
        "_index": "i", "_parent": "p", "_percolate": "*", "_routing": "r",
        "_timestamp": 1, "_ttl": "1d", "_type": "t", "_version": 3,
        "_version_type": "external", "_id": "x", "_retry_on_conflict": 1,
        "payload": u"ü"})


@case("helpers.expand-action-does-not-mutate", tags=("adversarial",))
def _(ctx):
    from elasticsearch.helpers import expand_action
    src = {"_index": "blog", "_id": 1, "a": 2}
    out = expand_action(src)
    return {"action": out[0], "data": out[1], "source_after": src}


# --------------------------------------------------------------- bulk ----

@case("helpers.bulk-all-ok", fixture=bulk_ok)
def _(ctx):
    from elasticsearch.helpers import bulk
    return bulk(ctx.es(), [{"_index": "blog", "_type": "posts", "_id": i,
                            "t": u"№%d" % i} for i in range(1, 6)])


@case("helpers.bulk-stats-only", fixture=bulk_mixed)
def _(ctx):
    from elasticsearch.helpers import bulk
    return bulk(ctx.es(), [{"_index": "blog", "_type": "posts", "_id": i}
                           for i in (1, 2, 3)],
                stats_only=True, raise_on_error=False)


@case("helpers.bulk-raises-on-error", tags=("adversarial",), fixture=bulk_mixed)
def _(ctx):
    from elasticsearch.helpers import bulk
    return bulk(ctx.es(), [{"_index": "blog", "_type": "posts", "_id": i}
                           for i in (1, 2, 3)])


@case("helpers.bulk-collect-errors", fixture=bulk_mixed)
def _(ctx):
    from elasticsearch.helpers import bulk
    return bulk(ctx.es(), [{"_index": "blog", "_type": "posts", "_id": i}
                           for i in (1, 2, 3)], raise_on_error=False)


@case("helpers.bulk-chunked", fixture=bulk_ok)
def _(ctx):
    # chunk_size=2 over 5 actions is three requests; the wire log pins where
    # islice cut each chunk.
    from elasticsearch.helpers import bulk
    return bulk(ctx.es(), [{"_index": "blog", "_type": "posts", "_id": i}
                           for i in range(1, 6)], chunk_size=2,
                raise_on_error=False)


@case("helpers.bulk-generator-input", fixture=bulk_ok)
def _(ctx):
    from elasticsearch.helpers import bulk

    def gen():
        for i in range(1, 6):
            yield {"_index": "blog", "_type": "posts", "_id": i}
    return bulk(ctx.es(), gen(), chunk_size=3, raise_on_error=False)


@case("helpers.bulk-empty-actions", tags=("adversarial",), fixture=bulk_ok)
def _(ctx):
    from elasticsearch.helpers import bulk
    return bulk(ctx.es(), [])


@case("helpers.bulk-transport-error-propagates", tags=("adversarial",),
      fixture=bulk_503)
def _(ctx):
    from elasticsearch.helpers import bulk
    return bulk(ctx.es(max_retries=0), [{"_index": "blog", "_id": 1}])


@case("helpers.bulk-transport-error-swallowed", tags=("adversarial",),
      fixture=bulk_503)
def _(ctx):
    from elasticsearch.helpers import bulk
    return bulk(ctx.es(max_retries=0), [{"_index": "blog", "_id": 1}],
                raise_on_exception=False, raise_on_error=False)


@case("helpers.bulk-transport-error-reraised-as-bulkindexerror",
      tags=("adversarial",), fixture=bulk_503)
def _(ctx):
    from elasticsearch.helpers import bulk
    return bulk(ctx.es(max_retries=0), [{"_index": "blog", "_id": 1}],
                raise_on_exception=False, raise_on_error=True)


@case("helpers.bulk-error-info-after-retries", fixture=bulk_503)
def _(ctx):
    # Same swallowed-failure path as the case above, but with a transport that
    # retried twice first. What streaming_bulk reports for the chunk is built
    # from the exception the handler was given -- its str, its status_code, and
    # the object itself under ``exception`` -- so this pins which attempt those
    # fields describe when there was more than one.
    from elasticsearch.helpers import bulk
    return bulk(ctx.es(max_retries=2), [{"_index": "blog", "_id": 1}],
                raise_on_exception=False, raise_on_error=False)


@case("helpers.bulk-swallows-one-chunk-and-raises-the-next",
      tags=("adversarial",), fixture=bulk_503)
def _(ctx):
    # streaming_bulk's two answers to the same transport failure, in one
    # observation: with ``raise_on_exception`` off it reports the chunk's error
    # info per action, and with it on the failure travels through to the
    # caller. The suite has both separately; only here does a single
    # expectation hold the unchanged error dict and the aggregated failure at
    # once, which is the pair the helper's own handler has to be rewritten to
    # produce -- it cannot get one right by leaving the other alone.
    from elasticsearch.helpers import bulk
    swallowed = bulk(ctx.es(max_retries=2), [{"_index": "blog", "_id": 1}],
                     raise_on_exception=False, raise_on_error=False)
    raised = None
    try:
        bulk(ctx.es(max_retries=2), [{"_index": "blog", "_id": 1}])
    except Exception as exc:
        raised = exc
    return {"swallowed": swallowed, "raised": raised}


@case("helpers.bulk-propagates-every-attempt", tags=("adversarial",),
      fixture=bulk_503)
def _(ctx):
    # raise_on_exception at its default: the transport's failure goes straight
    # through the helper to the caller, so whatever the loop was holding when
    # it gave up arrives here intact. Three attempts, and the wire log says so.
    from elasticsearch.helpers import bulk
    return bulk(ctx.es(max_retries=2), [{"_index": "blog", "_id": 1}])


@case("helpers.streaming-bulk-yields", fixture=bulk_mixed)
def _(ctx):
    from elasticsearch.helpers import streaming_bulk
    return list(streaming_bulk(ctx.es(),
                               [{"_index": "blog", "_id": i} for i in (1, 2, 3)],
                               raise_on_error=False))


@case("helpers.streaming-bulk-custom-expander", fixture=bulk_ok)
def _(ctx):
    from elasticsearch.helpers import streaming_bulk
    def expander(d):
        return {"index": {"_id": d["id"]}}, {"v": d["v"]}
    return list(streaming_bulk(ctx.es(), [{"id": 1, "v": u"ü"}],
                               expand_action_callback=expander,
                               raise_on_error=False))


# --------------------------------------------------------------- scan ----

@case("helpers.scan-basic", fixture=scan_fixture)
def _(ctx):
    from elasticsearch.helpers import scan
    return list(scan(ctx.es(), index="blog"))


@case("helpers.scan-with-query", fixture=scan_fixture)
def _(ctx):
    from elasticsearch.helpers import scan
    return list(scan(ctx.es(), query={"query": {"match": {"t": u"ドキュメント1"}}},
                     index="blog", doc_type="posts"))


def scan_ordered_fixture():
    # With preserve_order the initial search is a normal query whose response
    # already carries the first page, so the fixture must too.
    shards = {"total": 2, "successful": 2, "failed": 0}
    first = json_route({"_scroll_id": "S1", "_shards": shards, "took": 1,
                        "hits": {"total": 3, "max_score": 1.0, "hits": HITS}})
    page2 = json_route({"_scroll_id": "S2", "_shards": shards,
                        "hits": {"total": 3, "max_score": None, "hits": []}})
    return {"*/_search": first, "*/_search/scroll": [page2]}


@case("helpers.scan-preserve-order", fixture=scan_ordered_fixture)
def _(ctx):
    from elasticsearch.helpers import scan
    return list(scan(ctx.es(), index="blog", preserve_order=True))


@case("helpers.scan-no-scroll-id", tags=("adversarial",),
      fixture=lambda: {"*/_search": json_route(
          {"_shards": {"total": 1, "successful": 1, "failed": 0},
           "hits": {"total": 0, "max_score": None, "hits": []}})})
def _(ctx):
    from elasticsearch.helpers import scan
    return list(scan(ctx.es(), index="blog"))


@case("helpers.scan-failed-shards-raises", tags=("adversarial",),
      fixture=scan_failed_shards)
def _(ctx):
    from elasticsearch.helpers import scan
    return list(scan(ctx.es(), index="blog"))


@case("helpers.scan-failed-shards-warns", tags=("adversarial",),
      fixture=scan_failed_shards)
def _(ctx):
    from elasticsearch.helpers import scan
    return list(scan(ctx.es(), index="blog", raise_on_error=False))


@case("helpers.reindex", fixture=reindex_fixture)
def _(ctx):
    from elasticsearch.helpers import reindex
    return reindex(ctx.es(), "blog", u"ブログ-v2")


@case("helpers.reindex-with-query", fixture=reindex_fixture)
def _(ctx):
    from elasticsearch.helpers import reindex
    return reindex(ctx.es(), ["a", "b"], "target",
                   query={"query": {"term": {"x": 1}}}, chunk_size=2,
                   bulk_kwargs={"stats_only": False, "raise_on_error": False})


@case("helpers.bulk-index-alias-is-bulk")
def _(ctx):
    from elasticsearch import helpers
    return helpers.bulk_index is helpers.bulk


@case("helpers.errors-attribute", tags=("adversarial",))
def _(ctx):
    from elasticsearch.helpers import BulkIndexError, ScanError
    e = BulkIndexError("2 document(s) failed to index.", [{"index": {"a": 1}}])
    return {"str": str(e), "errors": e.errors,
            "scan_mro": [c.__name__ for c in ScanError.__mro__]}
