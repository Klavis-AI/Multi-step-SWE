# -*- coding: utf-8 -*-
"""elasticsearch.transport and connection_pool.

Retry, node-failure and sniffing behaviour. ``Transport.set_connections`` calls
``map(_create_connection, hosts)`` and then ``list(zip(...))`` over the result:
on Python 2 ``map`` returned a list, on 3 it returns a one-shot iterator, so
this is one of the few places in the tree where the 2/3 difference could still
bite silently. Sniffing is driven end to end so the iterator is consumed twice.
"""
from ..registry import case
from ..server import Route, json_route


def _try(fn):
    """Record one probe's value or the exception it raised.

    Per probe rather than per case: a single latent defect must not delete the
    evidence for everything recorded beside it.
    """
    try:
        return {"ok": fn()}
    except Exception as exc:
        return {"raised": type(exc).__name__, "str": str(exc)}

SHARDS = {"total": 1, "successful": 1, "failed": 0}
OK = {"took": 1, "_shards": SHARDS, "hits": {"total": 0, "max_score": None,
                                             "hits": []}}


def ok():
    return {"*": json_route(OK)}


def flaky_503():
    # Three 503s then a 200 on the same path: exactly max_retries=3 recoveries.
    return {"*": [json_route({"error": "unavailable"}, status=503),
                  json_route({"error": "unavailable"}, status=503),
                  json_route({"error": "unavailable"}, status=503),
                  json_route(OK)]}


def always_503():
    return {"*": json_route({"error": "unavailable"}, status=503)}


def always_504():
    return {"*": json_route({"error": "gateway timeout"}, status=504)}


def slow():
    return {"*": json_route(OK, delay=1.2)}


def _sniff_route(ctx, extra_attrs=None, n=1):
    from ..server import json_route as jr
    nodes = {}
    for i in range(n):
        nodes["node%d" % i] = {
            "name": "n%d" % i,
            "http_address": "inet[/127.0.0.1:%d]" % ctx.fx.port,
            "attributes": dict(extra_attrs or {}),
        }
    return jr({"cluster_name": "fixture", "nodes": nodes})


# ------------------------------------------------------------- retries ----

@case("transport.retry-503-then-ok", fixture=flaky_503)
def _(ctx):
    return ctx.es().search(index="blog", body={"query": {"match_all": {}}})


@case("transport.retry-exhausted-503", tags=("adversarial",),
      fixture=always_503)
def _(ctx):
    return ctx.es().search(index="blog", body={"query": {"match_all": {}}})


@case("transport.retry-504", tags=("adversarial",), fixture=always_504)
def _(ctx):
    return ctx.es().info()


@case("transport.no-retry-on-400", tags=("adversarial",),
      fixture=lambda: {"*": json_route({"error": "bad"}, status=400)})
def _(ctx):
    # 400 is not in retry_on_status: exactly one request must reach the wire.
    return ctx.es().info()


@case("transport.max-retries-zero", tags=("adversarial",), fixture=always_503)
def _(ctx):
    return ctx.es(max_retries=0).info()


@case("transport.max-retries-one", tags=("adversarial",), fixture=always_503)
def _(ctx):
    return ctx.es(max_retries=1).info()


@case("transport.retry-on-status-custom", tags=("adversarial",),
      fixture=lambda: {"*": json_route({"error": "teapot"}, status=418)})
def _(ctx):
    return ctx.es(retry_on_status=(418,), max_retries=2).info()


@case("transport.request-timeout-param", tags=("adversarial",), fixture=slow)
def _(ctx):
    return ctx.es(max_retries=0).search(index="blog", body=None,
                                        request_timeout=0.25)


@case("transport.retry-on-timeout", tags=("adversarial",), fixture=slow)
def _(ctx):
    return ctx.es(max_retries=1, retry_on_timeout=True, timeout=0.25).info()


@case("transport.connection-refused", tags=("adversarial",))
def _(ctx):
    # Port 1 on loopback: nothing binds it and nothing may, so the refusal is
    # the same every run. An OS-assigned "just closed" port would leak its
    # number into the log records and make the case unstable.
    from elasticsearch import Elasticsearch
    client = Elasticsearch([{"host": "127.0.0.1", "port": 1}], max_retries=0)
    return client.info()


# --------------------------------------------- what the retry loop keeps ----
#
# The cases above record *that* an exhausted retry loop raises; these two
# record *what it was holding* when it did. The loop makes one attempt per
# retry and reports one exception, so a tree that keeps every attempt and a
# tree that keeps only the last are indistinguishable from the ``__exc__`` and
# ``str`` of the five cases above. Both of these let the failure escape: the
# recorder walks whatever comes out, including an ``exceptions`` sequence if
# the object has one, and logs every request that reached the fixture, so the
# attempts and what was kept of them are recorded side by side.


@case("transport.retry-group-carries-every-attempt", tags=("adversarial",),
      fixture=lambda: {"*": [json_route({"error": "unavailable"}, status=503),
                             json_route({"error": "gateway timeout"},
                                        status=504),
                             json_route({"error": "unavailable"}, status=503)]})
def _(ctx):
    # Three attempts against three *different* statuses, so the order of what
    # the loop kept is observable and not merely its length.
    return ctx.es(max_retries=2, retry_on_status=(503, 504)).info()


@case("transport.sniff-every-connection-failed", tags=("adversarial",))
def _(ctx):
    # Ports 1 and 2 on loopback: nothing binds them and nothing may, so the
    # refusal is the same every run. randomize_hosts is off because the pool's
    # order is the order the failures come back in and the default shuffles it.
    # sniff_hosts walks the pool's connections and then the seed connections --
    # the same two, twice -- and this is the only case that reaches the
    # "Unable to sniff hosts." arm: a 503 is not a ConnectionError and leaves
    # that loop by the other door (transport.sniff-unreachable records it).
    from elasticsearch import Elasticsearch
    client = Elasticsearch([{"host": "127.0.0.1", "port": 1},
                            {"host": "127.0.0.1", "port": 2}],
                           randomize_hosts=False)
    return client.transport.sniff_hosts()


@case("transport.sniff-on-start-every-connection-failed",
      tags=("adversarial",))
def _(ctx):
    # The same "no connection answered" arm as the case above, reached through
    # sniff_on_start, which runs inside Transport.__init__ -- so whatever it
    # raises comes out of the constructor and there is no client to return.
    # One host, so the walk is two attempts rather than four.
    from elasticsearch import Elasticsearch
    return Elasticsearch([{"host": "127.0.0.1", "port": 1}],
                         sniff_on_start=True)


@case("transport.sniff-recovers-when-one-connection-works",
      tags=("adversarial",), fixture=ok)
def _(ctx):
    # The contrast the two cases above need: the first connection is refused
    # and the second answers, so the loop leaves by ``break`` and nothing is
    # raised at all. A tree that collected the failures and reported them
    # whatever the outcome would fail here and nowhere else.
    from elasticsearch import Elasticsearch
    ctx.fx.routes["/_nodes/_all/clear"] = _sniff_route(ctx, n=2)
    client = Elasticsearch([{"host": "127.0.0.1", "port": 1},
                            {"host": ctx.fx.host, "port": ctx.fx.port}],
                           randomize_hosts=False)
    client.transport.sniff_hosts()
    # repr reads Transport.hosts, which sniffing does not touch, so the pool is
    # what says the sniff ran to the end and replaced the connections.
    return {"repr": repr(client),
            "pool": sorted(c.host for c in
                           client.transport.connection_pool.connections)}


@case("transport.short-response", tags=("adversarial",),
      fixture=lambda: {"*": Route(close_early=True)})
def _(ctx):
    return ctx.es(max_retries=0).info()


# ------------------------------------------------------------ sniffing ----

@case("transport.sniff-explicit", fixture=ok)
def _(ctx):
    client = ctx.es()
    ctx.fx.routes["/_nodes/_all/clear"] = _sniff_route(ctx, n=2)
    client.transport.sniff_hosts()
    return repr(client)


@case("transport.sniff-filters-master-only", fixture=ok)
def _(ctx):
    client = ctx.es()
    ctx.fx.routes["/_nodes/_all/clear"] = _sniff_route(
        ctx, extra_attrs={"data": "false", "client": "false", "master": "true"})
    return client.transport.sniff_hosts()


@case("transport.sniff-no-viable-hosts", tags=("adversarial",), fixture=ok)
def _(ctx):
    client = ctx.es()
    ctx.fx.routes["/_nodes/_all/clear"] = json_route({"nodes": {
        "n": {"http_address": "not an address", "attributes": {}}}})
    return client.transport.sniff_hosts()


@case("transport.sniff-unreachable", tags=("adversarial",),
      fixture=always_503)
def _(ctx):
    return ctx.es().transport.sniff_hosts()


@case("transport.sniff-on-start", fixture=ok)
def _(ctx):
    # sniff_on_start runs inside Transport.__init__, before the client exists,
    # so the route has to be in place first.
    from elasticsearch import Elasticsearch
    ctx.fx.routes["/_nodes/_all/clear"] = _sniff_route(ctx, n=2)
    client = Elasticsearch([{"host": ctx.fx.host, "port": ctx.fx.port}],
                           sniff_on_start=True)
    return repr(client)


@case("transport.add-connection", fixture=ok)
def _(ctx):
    client = ctx.es()
    client.transport.add_connection({"host": "127.0.0.1", "port": 9999})
    return repr(client)


@case("transport.get-host-info-callback")
def _(ctx):
    from elasticsearch.transport import get_host_info
    host = {"host": "127.0.0.1", "port": 9200}
    return [
        get_host_info({"attributes": {}}, host),
        get_host_info({"attributes": {"data": "false", "client": "false",
                                      "master": "true"}}, host),
        get_host_info({"attributes": {"data": "false", "client": "true",
                                      "master": "true"}}, host),
        get_host_info({}, host),
    ]


@case("transport.address-re")
def _(ctx):
    from elasticsearch.transport import ADDRESS_RE
    return [(s, (ADDRESS_RE.search(s).groupdict()
                 if ADDRESS_RE.search(s) else None))
            for s in ("inet[/127.0.0.1:9200]",
                      "inet[node-1/10.0.0.4:9300]",
                      "inet[/::1:9200]",
                      "10.0.0.4:9200",
                      "garbage")]


# ------------------------------------------------------ connection pool ----

@case("transport.pool-is-dummy-for-one-host", fixture=ok)
def _(ctx):
    client = ctx.es()
    pool = client.transport.connection_pool
    return {"class": type(pool).__name__,
            "conn": repr(pool.get_connection())}


@case("transport.pool-round-robin")
def _(ctx):
    # randomize_hosts defaults to True and shuffles with the *global* random
    # module, so the pool's own order is not reproducible. Turning it off is
    # what makes the round-robin sequence itself observable.
    from elasticsearch.connection import Connection
    from elasticsearch.connection_pool import ConnectionPool
    conns = [(Connection(host="h%d" % i, port=9200), {"host": "h%d" % i})
             for i in range(3)]
    pool = ConnectionPool(conns, randomize_hosts=False)
    return [repr(pool.get_connection()) for _ in range(7)]


@case("transport.pool-randomize-hosts-is-a-permutation", tags=("adversarial",))
def _(ctx):
    # With shuffling on, the only thing that can be asserted run to run is
    # that the pool still holds exactly the connections it was given and that
    # two full cycles of round robin visit each one twice.
    import collections
    from elasticsearch.connection import Connection
    from elasticsearch.connection_pool import ConnectionPool
    conns = [(Connection(host="h%d" % i, port=9200), {"host": "h%d" % i})
             for i in range(4)]
    pool = ConnectionPool(conns, randomize_hosts=True)
    seen = collections.Counter(repr(pool.get_connection()) for _ in range(8))
    return {"members": sorted(repr(c) for c in pool.connections),
            "cycle_counts": sorted(seen.values()),
            "distinct": len(seen)}


@case("transport.pool-random-selector")
def _(ctx):
    # RandomSelector picks with the global random module; only membership is
    # assertable, not which member.
    from elasticsearch.connection import Connection
    from elasticsearch.connection_pool import ConnectionPool, RandomSelector
    conns = [(Connection(host="h%d" % i, port=9200), {"host": "h%d" % i})
             for i in range(3)]
    pool = ConnectionPool(conns, selector_class=RandomSelector)
    picked = pool.get_connection()
    return picked in [c for c, _ in conns]


@case("transport.pool-mark-dead-and-resurrect", tags=("adversarial",))
def _(ctx):
    from elasticsearch.connection import Connection
    from elasticsearch.connection_pool import ConnectionPool
    conns = [(Connection(host="h%d" % i, port=9200), {"host": "h%d" % i})
             for i in range(3)]
    pool = ConnectionPool(conns, dead_timeout=0, randomize_hosts=False)
    first = conns[0][0]
    pool.mark_dead(first, now=1000.0)
    after_dead = sorted(repr(c) for c in pool.connections)
    # dead_timeout=0 means it is eligible immediately.
    pool.resurrect(force=True)
    after_resurrect = sorted(repr(c) for c in pool.connections)
    counted = dict((repr(k), v) for k, v in pool.dead_count.items())
    pool.mark_live(first)
    return {"after_dead": after_dead, "after_resurrect": after_resurrect,
            "failures": counted,
            "cleared_by_mark_live": dict(
                (repr(k), v) for k, v in pool.dead_count.items())}


@case("transport.pool-all-dead-returns-one", tags=("adversarial",))
def _(ctx):
    from elasticsearch.connection import Connection
    from elasticsearch.connection_pool import ConnectionPool

    def two():
        c = [(Connection(host="h%d" % i, port=9200), {"host": "h%d" % i})
             for i in range(2)]
        return c, ConnectionPool(c, dead_timeout=3600, randomize_hosts=False)

    # A genuine Python-2-ism, pinned rather than repaired. mark_dead pushes
    # ``(deadline, connection)`` onto a PriorityQueue, and two nodes failing at
    # the same instant with the same failure count get the same deadline -- at
    # which point heapq falls through to comparing the Connection objects. On
    # Python 2 arbitrary objects had a total order and this worked; on 3 it is
    # a TypeError, and every rung of this ladder is Python 3. So the exception
    # *is* the baseline. A migration that quietly gives Connection an __lt__
    # changes what a cluster does when two nodes fail together, which is
    # exactly the moment the pool matters.
    conns, pool = two()
    out = {"simultaneous_failures": _try(
        lambda: [pool.mark_dead(c, now=1000.0) for c, _ in conns])}
    out["live_after_collision"] = [repr(c) for c in pool.connections]

    # One second apart the deadlines differ, heapq never compares the payloads,
    # and the documented behaviour is observable: with nothing live at all,
    # get_connection force-resurrects rather than raising.
    conns, pool = two()
    for i, (c, _) in enumerate(conns):
        pool.mark_dead(c, now=1000.0 + i)
    picked = pool.get_connection()
    out["picked_is_member"] = picked in [c for c, _ in conns]
    out["live"] = [repr(c) for c in pool.connections]
    out["dead_count"] = sorted(v for v in pool.dead_count.values())
    return out


@case("transport.pool-empty-raises", tags=("adversarial",))
def _(ctx):
    from elasticsearch.connection_pool import ConnectionPool
    return ConnectionPool([])


@case("transport.dummy-pool-noops")
def _(ctx):
    from elasticsearch.connection import Connection
    from elasticsearch.connection_pool import DummyConnectionPool
    c = Connection(host="h", port=9200)
    pool = DummyConnectionPool([(c, {"host": "h"})])
    pool.mark_dead(c)
    pool.mark_live(c)
    pool.resurrect()
    # The single-node pool never chains up to ConnectionPool.__init__, so it
    # has no dead queue, no dead_count and no selector at all -- the three
    # lifecycle methods are one shared no-op. Recording the absences pins that:
    # adding a super() call would give this class state it has never had.
    return {"conn": repr(pool.get_connection()),
            "connections": [repr(x) for x in pool.connections],
            "connections_is_tuple": type(pool.connections).__name__,
            "noop_is_shared": (pool.mark_dead.__func__
                               is pool.resurrect.__func__
                               is pool.mark_live.__func__),
            "absent": sorted(n for n in ("dead", "dead_count", "selector",
                                         "dead_timeout", "orig_connections")
                             if not hasattr(pool, n))}


@case("transport.dummy-pool-rejects-two", tags=("adversarial",))
def _(ctx):
    from elasticsearch.connection import Connection
    from elasticsearch.connection_pool import DummyConnectionPool
    conns = [(Connection(host="h%d" % i, port=9200), {"host": "h%d" % i})
             for i in range(2)]
    return DummyConnectionPool(conns)


@case("transport.scheme-mismatch-raises", tags=("adversarial",))
def _(ctx):
    from elasticsearch import Elasticsearch
    from elasticsearch.connection import Urllib3HttpConnection
    return Elasticsearch([{"host": "h", "port": 1, "scheme": "thrift"}],
                         connection_class=Urllib3HttpConnection)


@case("transport.custom-serializer", fixture=ok)
def _(ctx):
    from elasticsearch.serializer import JSONSerializer

    class Upper(JSONSerializer):
        mimetype = "application/x-upper"

        def dumps(self, data):
            return JSONSerializer.dumps(self, data).upper()
    return ctx.es(serializer=Upper()).search(index="blog",
                                             body={"query": {"a": "b"}})


@case("transport.deserializer-content-type-charset",
      fixture=lambda: {"*": Route(
          status=200, body=b'{"a":"\xc3\xbc"}',
          headers=[("content-type", "application/json; charset=utf-8")])})
def _(ctx):
    return ctx.es().info()


@case("transport.transport-direct", fixture=ok)
def _(ctx):
    from elasticsearch import Transport
    t = Transport([{"host": ctx.fx.host, "port": ctx.fx.port}])
    return t.perform_request("POST", "/x/_search",
                             params={"pretty": "true"},
                             body={"query": {"match": {"t": u"ü"}}})


@case("transport.transport-body-already-bytes", tags=("adversarial",),
      fixture=ok)
def _(ctx):
    from elasticsearch import Transport
    t = Transport([{"host": ctx.fx.host, "port": ctx.fx.port}])
    return t.perform_request("POST", "/x/_bulk", body=b'{"a":1}\n')


@case("transport.transport-head-with-body", tags=("adversarial",), fixture=ok)
def _(ctx):
    from elasticsearch import Transport
    t = Transport([{"host": ctx.fx.host, "port": ctx.fx.port}],
                  send_get_body_as="POST")
    return t.perform_request("HEAD", "/", body={"a": 1})
