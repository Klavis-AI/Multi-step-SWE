# -*- coding: utf-8 -*-
"""The namespaced clients and the URL-building helpers underneath them.

``client/utils.py`` is the single most 2/3-sensitive file in the tree:
``_escape`` encodes every ``str`` path segment to utf-8 *bytes* and hands the
result to ``quote_plus``, whose second argument is a bytes literal. Every
namespaced method funnels through ``_make_path``, so the path each of these
cases puts on the wire is the observation that matters.
"""
from ..registry import case
from ..server import Route, json_route

ACK = {"acknowledged": True}


def ok():
    return {"*": json_route(ACK)}


def paths(ctx):
    return [(r["method"], r["path"]) for r in ctx.fx.seen]


def _try(fn):
    """Record one probe's value or the exception it raised.

    Per probe rather than per case: a single latent defect must not delete the
    evidence for everything recorded beside it.
    """
    try:
        return {"ok": fn()}
    except Exception as exc:
        return {"raised": type(exc).__name__, "str": str(exc)}


# ------------------------------------------------------- url building ----

@case("utils.escape-scalars")
def _(ctx):
    import datetime
    import decimal
    from elasticsearch.client.utils import _escape
    out = []
    for v in ("plain", u"日本語", u"a/b", u"a b", u"a,b", "*", 7, 7.5, True,
              False, None, ["a", "b"], ("x", u"ü"), datetime.date(2015, 6, 1),
              datetime.datetime(2015, 6, 1, 12, 30, 45),
              decimal.Decimal("1.5")):
        try:
            out.append(_escape(v))
        except Exception as e:  # per value, so one failure cannot hide the rest
            out.append("%s: %s" % (type(e).__name__, e))
    return out


@case("utils.escape-bytes-is-broken-on-py3", tags=("adversarial",))
def _(ctx):
    # compat.string_types is ``str, bytes`` on Python 3, so _escape takes the
    # "encode strings to utf-8" branch for a bytes value and calls .encode on
    # it. The AttributeError that follows is a latent defect, and pinning it is
    # deliberate: retiring the py2 compat shims must not quietly change what
    # this call does. A migration that "tidies" string_types into plain ``str``
    # silently starts returning "b'already-bytes'" here instead of raising.
    from elasticsearch.client.utils import _escape
    try:
        return {"ok": _escape(b"already-bytes")}
    except Exception as e:
        return {"raised": type(e).__name__, "str": str(e)}


@case("utils.make-path")
def _(ctx):
    from elasticsearch.client.utils import _make_path
    return [_make_path(*parts) for parts in (
        ("blog", "posts", "1"),
        (u"ブログ", u"記事", u"タイトル"),
        ("blog", None, "1"),
        ("blog", "", "1"),
        ("blog", [], "1"),
        ("blog", (), "1"),
        ("blog", b"", "1"),
        ("blog", 0, "1"),
        ("blog", False, "1"),
        (["a", "b"], "_search"),
        ("a,b", "*", "_search"),
        ("with space", "id/with/slash"),
        ("percent%sign", "plus+sign", "hash#sign", "amp&sign", "q?mark"),
    )]


@case("utils.make-path-skip-in-path-is-exact", tags=("adversarial",))
def _(ctx):
    # SKIP_IN_PATH holds None, '', b'', [] and () -- but *not* 0 or False, and
    # `p not in SKIP_IN_PATH` uses ==, so 0 == False is the interesting pair.
    from elasticsearch.client.utils import SKIP_IN_PATH, _make_path
    return {"skip": [repr(x) for x in SKIP_IN_PATH],
            "zero": _make_path("i", 0, "x"),
            "false": _make_path("i", False, "x"),
            "empty_dict": _make_path("i", {}, "x"),
            "zero_in_skip": 0 in SKIP_IN_PATH,
            "false_in_skip": False in SKIP_IN_PATH}


@case("utils.escape-return-type-splits-bytes-and-str", tags=("adversarial",))
def _(ctx):
    # _escape has two exits. Anything that looks like a string on the way out
    # -- text, a joined sequence, a bool, a date -- leaves as *bytes*, because
    # the last branch encodes to utf-8. Everything else leaves as *str* through
    # str(value). So _make_path hands quote_plus a bytes string one segment and
    # a text string the next, with a bytes literal for `safe` either way. On
    # Python 2 that distinction did not exist; on 3 it is two different code
    # paths inside urllib.parse, and a migration that normalises _escape to one
    # return type changes which of them runs for an integer document id.
    from elasticsearch.client.utils import _escape
    import datetime
    import decimal
    probes = [("text", u"日本語"), ("ascii", "a"), ("int", 7), ("float", 7.5),
              ("bool_true", True), ("bool_false", False),
              ("decimal", decimal.Decimal("1.5")),
              ("date", datetime.date(2015, 6, 1)),
              ("list", ["a", u"ü"]), ("tuple", ("a", "b")),
              ("dict", {"k": 1}), ("none", None)]
    out = {}
    for label, value in probes:
        r = _try(lambda v=value: _escape(v))
        if "ok" in r:
            r["type"] = type(r["ok"]).__name__
        out[label] = r
    return out


@case("utils.escape-quotes-both-return-types", tags=("adversarial",))
def _(ctx):
    # The other half of the same seam: what quote_plus actually does when it is
    # handed each of _escape's two return types together with a *bytes* safe
    # list. The safe argument is why ',' and '*' survive unescaped, and _make_path
    # spells it b',*' -- a literal that a 2->3 cleanup is likely to touch.
    #
    # quote_plus is taken from the standard library rather than from
    # ``elasticsearch.compat``, which only re-exports it: the compat module is
    # a Python-2 shim and therefore a legitimate deletion target for a later
    # rung, and an inherited case must not import something the task may
    # require to be gone.
    from urllib.parse import quote_plus
    from elasticsearch.client.utils import _escape, _make_path
    out = {}
    for label, value in (("bytes_text", u"日本語"), ("bytes_comma", u"a,b"),
                         ("bytes_star", "*"), ("bytes_space", "a b"),
                         ("str_int", 7), ("str_float", 1.5),
                         ("str_dict", {"k": 1})):
        esc = _escape(value)
        out[label] = {"escaped_type": type(esc).__name__,
                      "default_safe": _try(lambda e=esc: quote_plus(e)),
                      "bytes_safe": _try(lambda e=esc: quote_plus(e, b",*")),
                      "str_safe": _try(lambda e=esc: quote_plus(e, ",*")),
                      # What the tree itself does with the same value, which is
                      # the assertion that has to survive the seam being
                      # rewritten however the migration chooses.
                      "through_make_path": _try(
                          lambda v=value: _make_path("i", v))}
    return out


@case("utils.escape-sequence-join-rejects", tags=("adversarial",))
def _(ctx):
    # _escape comma-joins lists and tuples before it does anything else, and
    # ','.join refuses a non-text member. Every one of these is a shape a caller
    # can reach by passing ``index=[...]``, and each produces a different
    # message on 2 and on 3. A generator is the near-miss: it is neither list
    # nor tuple, so it skips the join entirely and gets str()'d into its repr.
    from elasticsearch.client.utils import _escape, _make_path
    out = {}
    for label, value in (("ints", ["a", 1]),
                         ("none", [u"日", None]),
                         ("bytes_member", ["a", b"b"]),
                         ("nested", ["a", ["b"]]),
                         ("empty", []),
                         ("one", ["only"]),
                         ("generator", (c for c in "ab")),
                         ("set", set(["a"]))):
        out[label] = _try(lambda v=value: _escape(v))
    out["make_path_ints"] = _try(lambda: _make_path("i", ["a", 1]))
    return out


@case("utils.query-params-decorator")
def _(ctx):
    from elasticsearch.client.utils import query_params

    @query_params("routing", "timeout")
    def fn(a, params=None):
        return {"a": a, "params": params}
    return [fn(1),
            fn(1, routing=u"ルート", timeout="5s", pretty=True),
            fn(1, ignore=404, request_timeout=30),
            fn(1, params={"preset": "x"}, routing="r"),
            fn(1, filter_path=["hits.hits._id"], format="yaml"),
            fn(1, unknown_kwarg="kept") if False else "skipped"]


@case("utils.query-params-preserves-metadata")
def _(ctx):
    from elasticsearch.client import Elasticsearch
    m = Elasticsearch.search
    return {"name": m.__name__, "has_doc": bool(m.__doc__),
            "doc_head": (m.__doc__ or "").strip().splitlines()[0]}


@case("utils.namespaced-client-is-weakref", tags=("adversarial",))
def _(ctx):
    # AddonClient.infect_client stores a weakref.proxy so the namespace does
    # not keep the client alive. Nothing in the ladder should change that.
    from elasticsearch import Elasticsearch
    client = Elasticsearch()
    return {"indices_client_is_same": client.indices.client is client,
            "transport_is_same": client.indices.transport is client.transport,
            "types": sorted(type(getattr(client, n)).__name__ for n in
                            ("indices", "cluster", "cat", "nodes",
                             "snapshot"))}


# ----------------------------------------------------------- indices ----

@case("indices.create-and-delete", fixture=ok)
def _(ctx):
    es = ctx.es()
    out = [es.indices.create(index=u"ブログ",
                             body={"settings": {"number_of_shards": 1}}),
           es.indices.delete(index=u"ブログ")]
    return {"out": out, "paths": paths(ctx)}


@case("indices.exists-returns-bool", fixture=lambda: {
    "*": Route(status=200), "/missing": Route(status=404)})
def _(ctx):
    es = ctx.es()
    return {"present": es.indices.exists(index="blog"),
            "missing": es.indices.exists(index="missing"),
            "paths": paths(ctx)}


@case("indices.exists-type", fixture=lambda: {"*": Route(status=404)})
def _(ctx):
    es = ctx.es()
    return {"r": es.indices.exists_type(index=["a", "b"], doc_type="posts"),
            "paths": paths(ctx)}


@case("indices.analyze", fixture=lambda: {"*": json_route(
    {"tokens": [{"token": u"日本", "start_offset": 0, "end_offset": 2,
                 "type": "word", "position": 1}]})})
def _(ctx):
    es = ctx.es()
    r = es.indices.analyze(index="blog", body=u"日本語",
                           params={"analyzer": "kuromoji"})
    return {"r": r, "wire": ctx.fx.wire()}


@case("indices.put-and-get-mapping", fixture=lambda: {
    "*": json_route({"blog": {"mappings": {"posts": {
        "properties": {"title": {"type": "string"}}}}}})})
def _(ctx):
    es = ctx.es()
    out = [es.indices.put_mapping(doc_type="posts", index="blog",
                                  body={"posts": {"properties": {}}}),
           es.indices.get_mapping(index=["blog", u"ブログ"],
                                  doc_type="posts")]
    return {"out": out, "paths": paths(ctx)}


@case("indices.get-field-mapping", fixture=ok)
def _(ctx):
    es = ctx.es()
    es.indices.get_field_mapping(field=["title", u"本文"], index="blog",
                                 doc_type="posts")
    return paths(ctx)


@case("indices.aliases", fixture=ok)
def _(ctx):
    es = ctx.es()
    es.indices.put_alias(name=u"別名", index="blog", body={"routing": "r"})
    es.indices.get_alias(index="blog", name=u"別名")
    es.indices.exists_alias(name=u"別名", index="blog")
    es.indices.update_aliases(body={"actions": [{"add": {"index": "a",
                                                         "alias": "b"}}]})
    es.indices.delete_alias(index="blog", name=u"別名")
    return paths(ctx)


@case("indices.templates", fixture=ok)
def _(ctx):
    es = ctx.es()
    es.indices.put_template(name="t1", body={"template": "blog-*"})
    es.indices.get_template(name="t1")
    es.indices.exists_template(name="t1")
    es.indices.delete_template(name="t1")
    return paths(ctx)


@case("indices.settings", fixture=ok)
def _(ctx):
    es = ctx.es()
    es.indices.put_settings(body={"index": {"refresh_interval": "1s"}},
                            index="blog")
    es.indices.get_settings(index=["a", "b"], name="index.refresh*")
    return paths(ctx)


@case("indices.refresh-flush-optimize", fixture=ok)
def _(ctx):
    es = ctx.es()
    es.indices.refresh(index="blog")
    es.indices.flush(index=["a", "b"], params={"wait_if_ongoing": True})
    es.indices.optimize(index="blog", params={"max_num_segments": 1})
    es.indices.clear_cache(index="blog")
    es.indices.open(index="blog")
    es.indices.close(index="blog")
    return paths(ctx)


@case("indices.stats-and-status", fixture=ok)
def _(ctx):
    es = ctx.es()
    es.indices.stats(index="blog", metric=["docs", "store"])
    es.indices.status(index="blog")
    es.indices.segments(index="blog")
    es.indices.recovery(index="blog")
    return paths(ctx)


@case("indices.validate-query", fixture=lambda: {"*": json_route(
    {"valid": False, "_shards": {"total": 1, "successful": 1, "failed": 0},
     "explanations": [{"index": "blog", "valid": False,
                       "error": "ParseException"}]})})
def _(ctx):
    es = ctx.es()
    return es.indices.validate_query(index="blog", doc_type="posts",
                                     body={"query": {"bad": {}}},
                                     params={"explain": True})


@case("indices.missing-required-arg-raises", tags=("adversarial",),
      fixture=ok)
def _(ctx):
    # Every method guards its required arguments against SKIP_IN_PATH and
    # raises ValueError; an empty string must be rejected the same as None.
    es = ctx.es()
    out = []
    for arg in (None, "", [], ()):
        try:
            es.indices.create(index=arg)
            out.append("no-raise:%r" % (arg,))
        except Exception as e:
            out.append("%s: %s" % (type(e).__name__, e))
    return out


#: Every member of SKIP_IN_PATH, plus the two values that look like members
#: and are not: ``0`` and ``False`` compare unequal to all five, so they reach
#: _make_path and become path segments.
SKIPPY = [("none", None), ("str", ""), ("bytes", b""), ("list", []),
          ("tuple", ()), ("zero", 0), ("false", False)]


@case("indices.required-arg-guards-sweep", tags=("adversarial",), fixture=ok)
def _(ctx):
    # The guards are hand-written, one per method, and they come in three
    # shapes: a single ``if x in SKIP_IN_PATH``, a loop over a tuple of
    # arguments, and a guard on ``body``. Sweeping all seven sentinels through
    # all three shapes is what makes the shapes distinguishable -- a migration
    # that consolidates them into a helper has to keep every cell of this table
    # identical, including which argument the message names and the fact that
    # 0 and False sail straight through into the URL.
    es = ctx.es()
    probes = (
        ("indices.create(index)", lambda v: es.indices.create(index=v)),
        ("indices.delete(index)", lambda v: es.indices.delete(index=v)),
        ("indices.refresh(index)", lambda v: es.indices.refresh(index=v)),
        ("indices.get_mapping(index,doc_type)",
         lambda v: es.indices.get_mapping(index="blog", doc_type=v)),
        ("indices.put_mapping(doc_type,body)",
         lambda v: es.indices.put_mapping(index="blog", doc_type=v,
                                          body={"posts": {}})),
        ("indices.put_mapping(body)",
         lambda v: es.indices.put_mapping(index="blog", doc_type="posts",
                                          body=v)),
        ("indices.put_alias(index,name)",
         lambda v: es.indices.put_alias(index="blog", name=v)),
        ("indices.put_template(name,body)",
         lambda v: es.indices.put_template(name=v, body={"template": "x"})),
        ("indices.get_field_mapping(field)",
         lambda v: es.indices.get_field_mapping(field=v, index="blog")),
        ("cluster.put_settings(body)",
         lambda v: es.cluster.put_settings(body=v)),
        ("snapshot.create_repository(repository,body)",
         lambda v: es.snapshot.create_repository(repository=v,
                                                 body={"type": "fs"})),
        ("nodes.hot_threads(node_id)",
         lambda v: es.nodes.hot_threads(node_id=v)),
    )
    out = {}
    for label, call in probes:
        row = {}
        for tag, sentinel in SKIPPY:
            before = len(ctx.fx.seen)
            r = _try(lambda c=call, s=sentinel: c(s))
            # Whether a request escaped is the part a guard is actually for;
            # a ValueError that still put bytes on the wire would be a bug.
            row[tag] = {"raised": r.get("raised"),
                        "str": r.get("str"),
                        "sent": [p for _, p in paths(ctx)[before:]]}
        out[label] = row
    return out


@case("indices.multi-index-paths-on-the-wire", tags=("adversarial",),
      fixture=ok)
def _(ctx):
    # Index arguments reach _escape as lists, tuples, comma strings and
    # wildcards, and the ',' and '*' that _make_path deliberately leaves
    # unescaped are the difference between one request against three indices
    # and one request against an index literally named "a,b,c".
    es = ctx.es()
    for index in (["a", "b", "c"], ("a", u"ブログ"), "a,b", "_all", "log-*",
                  u"ブログ", "with space", "a/b"):
        _try(lambda i=index: es.indices.refresh(index=i))
    # A list whose members are not all text cannot be joined at all, and the
    # failure happens in _escape, before any guard or any socket.
    mixed = _try(lambda: es.indices.refresh(index=["a", 1]))
    return {"paths": paths(ctx), "mixed": mixed}


@case("snapshot.names-with-reserved-characters", tags=("adversarial",),
      fixture=ok)
def _(ctx):
    # Repository and snapshot names are two consecutive path segments with a
    # fixed "_snapshot" prefix, so they exercise _make_path's escaping in the
    # middle of a path rather than at its end -- which is where a slash in a
    # name would silently change the endpoint being addressed if it were not
    # percent-encoded.
    es = ctx.es()
    for repo, snap in ((u"バックアップ", u"スナップ"),
                       ("repo/with/slash", "snap?q=1"),
                       ("repo with space", "snap+plus"),
                       ("repo%25", "snap#frag"),
                       ("repo", "_all"),
                       ("repo", "snap-*")):
        _try(lambda r=repo, s=snap: es.snapshot.get(repository=r, snapshot=s))
    _try(lambda: es.snapshot.delete(repository="repo", snapshot="s"))
    _try(lambda: es.snapshot.status(repository="repo", snapshot="s"))
    return paths(ctx)


# ----------------------------------------------------------- cluster ----

@case("cluster.health", fixture=lambda: {"*": json_route(
    {"cluster_name": "fixture", "status": "yellow", "number_of_nodes": 1,
     "active_shards": 5, "timed_out": False})})
def _(ctx):
    es = ctx.es()
    r = es.cluster.health(index="blog", params={"wait_for_status": "yellow",
                                                "timeout": "1s"})
    return {"r": r, "paths": paths(ctx)}


@case("cluster.state-and-stats", fixture=ok)
def _(ctx):
    es = ctx.es()
    es.cluster.state(metric="metadata", index="blog")
    es.cluster.stats(node_id="n1")
    es.cluster.pending_tasks()
    es.cluster.get_settings()
    es.cluster.put_settings(body={"transient": {"x": 1}})
    return paths(ctx)


@case("cluster.reroute", fixture=ok)
def _(ctx):
    es = ctx.es()
    es.cluster.reroute(body={"commands": [{"move": {"index": "blog"}}]},
                       params={"dry_run": True})
    return paths(ctx)


# --------------------------------------------------------------- cat ----

@case("cat.indices-text", fixture=lambda: {"*": Route(
    body=u"yellow open ブログ 5 1 0 0 575b 575b\n".encode("utf-8"),
    headers=[("content-type", "text/plain; charset=UTF-8")])})
def _(ctx):
    # cat returns text/plain, so the Deserializer's mimetype split and the
    # TextSerializer are both on this path.
    es = ctx.es()
    return {"r": es.cat.indices(index=u"ブログ", params={"v": True}),
            "paths": paths(ctx)}


@case("cat.help-and-health", fixture=lambda: {"*": Route(
    body=b"=^.^=\n/_cat/allocation\n",
    headers=[("content-type", "text/plain")])})
def _(ctx):
    es = ctx.es()
    return {"help": es.cat.help(), "health": es.cat.health(params={"ts": 0}),
            "paths": paths(ctx)}


@case("cat.json-format", fixture=lambda: {"*": json_route(
    [{"index": u"ブログ", "health": "yellow"}])})
def _(ctx):
    es = ctx.es()
    return es.cat.indices(format="json")


@case("cat.all-endpoints-build-paths", fixture=lambda: {"*": Route(
    body=b"ok\n", headers=[("content-type", "text/plain")])})
def _(ctx):
    es = ctx.es()
    es.cat.aliases(name="a")
    es.cat.allocation(node_id="n1")
    es.cat.count(index="blog")
    es.cat.master()
    es.cat.nodes()
    es.cat.recovery(index="blog")
    es.cat.shards(index="blog")
    es.cat.segments(index="blog")
    es.cat.pending_tasks()
    es.cat.thread_pool()
    es.cat.fielddata(fields=["a", "b"])
    es.cat.plugins()
    return paths(ctx)


# ------------------------------------------------------------- nodes ----

@case("nodes.info-stats-hot-threads", fixture=lambda: {
    "*/hot_threads": Route(body=b"::: {n1}\n   0.0% cpu usage\n",
                           headers=[("content-type", "text/plain")]),
    "*": json_route({"cluster_name": "fixture", "nodes": {}})})
def _(ctx):
    es = ctx.es()
    out = [es.nodes.info(node_id=["n1", "n2"], metric="os"),
           es.nodes.stats(node_id="n1", metric="jvm", index_metric="docs"),
           es.nodes.hot_threads(node_id="n1", params={"threads": 1}),
           es.nodes.shutdown(node_id="n1")]
    return {"out": out, "paths": paths(ctx)}


# ---------------------------------------------------------- snapshot ----

@case("snapshot.lifecycle", fixture=ok)
def _(ctx):
    es = ctx.es()
    es.snapshot.create_repository(repository="repo", body={"type": "fs"})
    es.snapshot.get_repository(repository=["repo", u"倉庫"])
    es.snapshot.create(repository="repo", snapshot=u"スナップ",
                       body={"indices": "blog"})
    es.snapshot.status(repository="repo", snapshot=u"スナップ")
    es.snapshot.get(repository="repo", snapshot="_all")
    es.snapshot.restore(repository="repo", snapshot=u"スナップ", body={})
    es.snapshot.delete(repository="repo", snapshot=u"スナップ")
    es.snapshot.delete_repository(repository="repo")
    es.snapshot.verify_repository(repository="repo")
    return paths(ctx)
