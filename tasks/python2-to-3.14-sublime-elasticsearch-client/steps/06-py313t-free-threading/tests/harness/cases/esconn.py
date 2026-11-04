# -*- coding: utf-8 -*-
"""The two connection classes, driven directly rather than through the client.

``Urllib3HttpConnection`` and ``RequestsHttpConnection`` are where the tree
touches the socket, the TLS stack and the str/bytes boundary at the same time.
``perform_request`` still carries a Python-2-era guard that encodes a non-``str``
url and method to utf-8 bytes before handing them to urllib3, which is exactly
the kind of code a 2->3 migration deletes; these cases pin what it does today.

TLS observations are deliberately reduced to the tree's own exception classes.
The certificates are regenerated at verifier time (trap 10: nothing here may
expire), so no serial, fingerprint or ``_ssl.c`` line number can be part of a
scored expectation.
"""
from ..registry import case
from ..server import Route, gzip_route, json_route, ca_pem

BODY = {"acknowledged": True, "name": u"ノード"}


def ok():
    return {"*": json_route(BODY)}


def _hdrs(headers):
    """Header collections differ by class (HTTPHeaderDict vs
    CaseInsensitiveDict); compare the mapping, not the container."""
    return sorted((k.lower(), v) for k, v in headers.items())


def _conn(ctx, cls="urllib3", **kw):
    if cls == "urllib3":
        from elasticsearch.connection import Urllib3HttpConnection as C
    else:
        from elasticsearch.connection import RequestsHttpConnection as C
    kw.setdefault("host", ctx.fx.host)
    kw.setdefault("port", ctx.fx.port)
    return C(**kw)


# --------------------------------------------------------- construction ----

@case("conn.urllib3-repr")
def _(ctx):
    from elasticsearch.connection import Urllib3HttpConnection
    c = Urllib3HttpConnection(host="example.com", port=9200)
    return {"repr": repr(c), "host": c.host, "url_prefix": c.url_prefix,
            "timeout": c.timeout, "headers": c.headers}


@case("conn.urllib3-auth-tuple")
def _(ctx):
    from elasticsearch.connection import Urllib3HttpConnection
    c = Urllib3HttpConnection(http_auth=("user", u"pä ss"))
    return c.headers


@case("conn.urllib3-auth-string")
def _(ctx):
    from elasticsearch.connection import Urllib3HttpConnection
    return Urllib3HttpConnection(http_auth="user:secret").headers


@case("conn.urllib3-auth-list")
def _(ctx):
    from elasticsearch.connection import Urllib3HttpConnection
    return Urllib3HttpConnection(http_auth=["user", "secret"]).headers


@case("conn.urllib3-url-prefix-stripped")
def _(ctx):
    from elasticsearch.connection import Urllib3HttpConnection
    return [Urllib3HttpConnection(url_prefix=p).url_prefix
            for p in ("", "/", "es", "/es", "es/", "///es///")]


@case("conn.urllib3-ca-certs-without-verify-raises", tags=("adversarial",))
def _(ctx):
    from elasticsearch.connection import Urllib3HttpConnection
    return Urllib3HttpConnection(use_ssl=True, verify_certs=False,
                                 ca_certs="/nonexistent/ca.pem")


@case("conn.requests-repr")
def _(ctx):
    from elasticsearch.connection import RequestsHttpConnection
    c = RequestsHttpConnection(host="example.com", port=9200, url_prefix="es")
    return {"repr": repr(c), "base_url": c.base_url, "host": c.host,
            "verify": c.session.verify, "cert": c.session.cert,
            "auth": c.session.auth}


@case("conn.requests-auth-string-splits-once")
def _(ctx):
    from elasticsearch.connection import RequestsHttpConnection
    return [RequestsHttpConnection(http_auth=a).session.auth
            for a in ("user:secret", "user:se:cret", "useronly",
                      u"ユーザ:ぱす")]


@case("conn.requests-ca-certs-without-verify-raises", tags=("adversarial",))
def _(ctx):
    from elasticsearch.connection import RequestsHttpConnection
    return RequestsHttpConnection(use_ssl=True, verify_certs=False,
                                  ca_certs="/nonexistent/ca.pem")


# ------------------------------------------------------ perform_request ----

@case("conn.urllib3-perform-request", fixture=ok)
def _(ctx):
    c = _conn(ctx)
    status, headers, data = c.perform_request("GET", "/")
    return {"status": status, "headers": _hdrs(headers), "data": data}


@case("conn.urllib3-perform-request-params", fixture=ok)
def _(ctx):
    # urlencode over a dict: the order the query string comes out in is the
    # dict's insertion order on every interpreter in this ladder.
    c = _conn(ctx)
    status, _, data = c.perform_request(
        "GET", "/blog/_search",
        params={"q": u"タイトル:*", "size": 10, "pretty": "true"})
    return {"status": status, "data": data, "wire_seen": len(ctx.fx.seen)}


@case("conn.urllib3-perform-request-body-bytes", fixture=ok)
def _(ctx):
    c = _conn(ctx)
    return c.perform_request("POST", "/blog/posts",
                             body=u'{"t":"日本語"}'.encode("utf-8"))


@case("conn.urllib3-perform-request-unicode-url", tags=("adversarial",),
      fixture=ok)
def _(ctx):
    # The `if not isinstance(url, str)` guard in perform_request was written
    # for Python 2, where a unicode url had to be encoded before urllib3 saw
    # it. On 3 a str url skips the branch entirely; a percent-escaped path is
    # what the client itself would produce.
    c = _conn(ctx)
    return c.perform_request("GET", u"/%E3%83%96%E3%83%AD%E3%82%B0/_search")


@case("conn.urllib3-perform-request-url-prefix", fixture=ok)
def _(ctx):
    c = _conn(ctx, url_prefix="es")
    status, _, data = c.perform_request("GET", "/_cluster/health")
    return {"status": status, "data": data,
            "paths": [r["path"] for r in ctx.fx.seen]}


@case("conn.urllib3-perform-request-timeout-kw", tags=("adversarial",),
      fixture=lambda: {"*": json_route(BODY, delay=1.2)})
def _(ctx):
    c = _conn(ctx, timeout=10)
    return c.perform_request("GET", "/", timeout=0.25)


@case("conn.urllib3-error-json", tags=("adversarial",),
      fixture=lambda: {"*": json_route({"error": "IndexMissingException[[x]]",
                                        "status": 404}, status=404)})
def _(ctx):
    return _conn(ctx).perform_request("GET", "/x/_search")


@case("conn.urllib3-error-typed-dict", tags=("adversarial",),
      fixture=lambda: {"*": json_route(
          {"error": {"type": "index_not_found_exception", "reason": "no such"},
           "status": 404}, status=404)})
def _(ctx):
    # _raise_error unwraps a dict error that carries a 'type' key and uses that
    # string as the message; everything else is kept whole in .info.
    return _conn(ctx).perform_request("GET", "/x/_search")


@case("conn.urllib3-error-non-json", tags=("adversarial",),
      fixture=lambda: {"*": Route(status=500, body=b"<html>oops</html>",
                                  headers=[("content-type", "text/html")])})
def _(ctx):
    return _conn(ctx).perform_request("GET", "/")


@case("conn.urllib3-error-ignored", fixture=lambda: {
    "*": json_route({"error": "missing", "status": 404}, status=404)})
def _(ctx):
    status, _, data = _conn(ctx).perform_request("GET", "/x/1", ignore=(404,))
    return {"status": status, "data": data}


@case("conn.urllib3-head-no-body", fixture=ok)
def _(ctx):
    return _conn(ctx).perform_request("HEAD", "/")


@case("conn.requests-perform-request", fixture=ok)
def _(ctx):
    c = _conn(ctx, cls="requests")
    status, headers, data = c.perform_request("GET", "/")
    return {"status": status, "headers": _hdrs(headers), "data": data}


@case("conn.requests-perform-request-params", fixture=ok)
def _(ctx):
    c = _conn(ctx, cls="requests")
    return c.perform_request("GET", "/blog/_search",
                             params={"q": u"タイトル:*", "size": 10})


@case("conn.requests-error-json", tags=("adversarial",),
      fixture=lambda: {"*": json_route({"error": "bad request"}, status=400)})
def _(ctx):
    return _conn(ctx, cls="requests").perform_request("POST", "/_search",
                                                      body=b"{}")


@case("conn.requests-error-ignored", fixture=lambda: {
    "*": json_route({"error": "missing"}, status=404)})
def _(ctx):
    return _conn(ctx, cls="requests").perform_request("GET", "/x/1",
                                                      ignore=(404, 409))


@case("conn.requests-gzip-response", fixture=lambda: {"*": gzip_route(BODY)})
def _(ctx):
    # requests decompresses transparently; response.text is what the tree sees.
    return _conn(ctx, cls="requests").perform_request("GET", "/")


@case("conn.requests-chunked-response", fixture=lambda: {
    "*": json_route(BODY, chunked=True)})
def _(ctx):
    return _conn(ctx, cls="requests").perform_request("GET", "/")


# ------------------------------------------------------------------ TLS ----

@case("conn.tls-verified-roundtrip", fixture=ok, tls=True)
def _(ctx):
    c = _conn(ctx, use_ssl=True, verify_certs=True, ca_certs=ca_pem())
    status, _, data = c.perform_request("GET", "/")
    # ``host`` is recorded because it is wrong and must stay wrong:
    # ``transport_schema`` is a class attribute fixed at 'http', and the urllib3
    # connection never overrides it for TLS, so a verified https connection
    # describes itself as http:// -- in its repr and in every log line it
    # writes. A migration tidying that up would rewrite the transport's log
    # output, which is scored.
    return {"status": status, "data": data, "host": c.host}


@case("conn.tls-untrusted-ca-rejected", tags=("adversarial",), fixture=ok,
      tls=True)
def _(ctx):
    # No ca_certs: the throwaway CA is not in any trust store, so verification
    # must fail. Only the tree's own exception class is observed -- the
    # underlying OpenSSL message carries a source line number that moves
    # between interpreters.
    import elasticsearch
    c = _conn(ctx, use_ssl=True, verify_certs=True, ca_certs=None)
    try:
        c.perform_request("GET", "/")
    except Exception as e:
        return {"raised": type(e).__name__,
                "is_tree_error": isinstance(e, elasticsearch.ConnectionError),
                "is_ssl_error": isinstance(e, elasticsearch.SSLError)}
    return {"raised": None}


@case("conn.tls-unverified-warns", fixture=ok, tls=True)
def _(ctx):
    # verify_certs=False is the tree's documented escape hatch and it warns.
    # That UserWarning is part of the library's contract and is scored.
    c = _conn(ctx, use_ssl=True, verify_certs=False)
    status, _, data = c.perform_request("GET", "/")
    return {"status": status, "data": data}


@case("conn.tls-client-through-elasticsearch", fixture=ok, tls=True)
def _(ctx):
    return ctx.es().info()


@case("conn.tls-requests-verified-roundtrip", fixture=ok, tls=True)
def _(ctx):
    c = _conn(ctx, cls="requests", use_ssl=True, verify_certs=True,
              ca_certs=ca_pem())
    status, _, data = c.perform_request("GET", "/")
    return {"status": status, "data": data, "base_url_scheme":
            c.base_url.split(":")[0]}


@case("conn.tls-requests-untrusted-ca-rejected", tags=("adversarial",),
      fixture=ok, tls=True)
def _(ctx):
    import elasticsearch
    c = _conn(ctx, cls="requests", use_ssl=True, verify_certs=True)
    try:
        c.perform_request("GET", "/")
    except Exception as e:
        return {"raised": type(e).__name__,
                "is_tree_error": isinstance(e, elasticsearch.ConnectionError),
                "is_ssl_error": isinstance(e, elasticsearch.SSLError)}
    return {"raised": None}


@case("conn.tls-plain-client-against-tls-server", tags=("adversarial",),
      fixture=ok, tls=True)
def _(ctx):
    # http to an https listener: the handshake bytes come back as garbage and
    # the tree must turn that into its own ConnectionError, not leak urllib3's.
    import elasticsearch
    c = _conn(ctx)
    try:
        c.perform_request("GET", "/")
    except Exception as e:
        return {"raised": type(e).__name__,
                "is_tree_error": isinstance(e, elasticsearch.ConnectionError)}
    return {"raised": None}


# --- PoolingConnection ------------------------------------------------------
#
# The base class of `MemcachedConnection` and `ThriftConnection`, and the only
# part of either that imports on a bare interpreter: `pooling.py` needs
# `queue` and `.base.Connection` and nothing else. Both subclasses need a
# package neither image carries, so until now nothing scored reached this file
# at all -- and step 05 rewrites its `__init__`, replacing `*args` with named
# parameters so the signature can be annotated. `work/astdiff05.py` caught a
# first attempt at that rewrite narrowing the signature; these cases are what
# make the same mistake cost reward rather than go unseen.


def _pooling(*args, **kwargs):
    from elasticsearch.connection.pooling import PoolingConnection
    c = PoolingConnection(*args, **kwargs)
    c._make_connection = lambda: "made"
    return c


@case("conn.pooling-binds-four-positionally")
def _(ctx):
    # `*args` went straight to `Connection.__init__`, so all four of that
    # signature's parameters bind positionally through the subclass. A rewrite
    # that names fewer of them, or names them in another order, changes what
    # this call means.
    c = _pooling("es.example", 9201, "api", 33)
    return {"host": c.host, "url_prefix": c.url_prefix, "timeout": c.timeout,
            "repr": repr(c)}


@case("conn.pooling-binds-four-by-keyword")
def _(ctx):
    c = _pooling(port=9202, host="kw.example", timeout=2.5, url_prefix="/v1/")
    return {"host": c.host, "url_prefix": c.url_prefix, "timeout": c.timeout}


@case("conn.pooling-defaults")
def _(ctx):
    c = _pooling()
    return {"host": c.host, "url_prefix": c.url_prefix, "timeout": c.timeout,
            "schema": c.transport_schema}


@case("conn.pooling-queue-round-trip")
def _(ctx):
    # Released connections come back before `_make_connection` is consulted,
    # and the queue is FIFO. The attribute is the base class's own state.
    import queue
    c = _pooling("h", 9200)
    first = c._get_connection()
    c._release_connection("a")
    c._release_connection("b")
    return {"empty_pool_makes_one": first,
            "then": [c._get_connection(), c._get_connection()],
            "and_makes_again": c._get_connection(),
            "free_is_a_queue": isinstance(c._free_connections, queue.Queue)}


@case("conn.pooling-passes-extra-keywords-through", tags=("adversarial",))
def _(ctx):
    # `**kwargs` reached `Connection.__init__`, which accepts and ignores
    # anything it does not name. A rewrite that drops `**kwargs`, or that
    # forwards it positionally, turns this from accepted into TypeError.
    try:
        c = _pooling("h", 9200, use_ssl=True, verify_certs=False, nonsense=1)
    except TypeError as e:
        return {"raised": "TypeError", "message": str(e)}
    return {"raised": None, "host": c.host,
            "kept_nothing": [n for n in ("use_ssl", "verify_certs", "nonsense")
                             if hasattr(c, n)]}


@case("conn.pooling-rejects-a-fifth-positional", tags=("adversarial",))
def _(ctx):
    # The counterpart: four is what the signature took, and a fifth was a
    # TypeError then and must be one now. Widening the signature is as much a
    # change as narrowing it.
    try:
        _pooling("h", 9200, "", 10, "extra")
    except TypeError as e:
        return {"raised": "TypeError", "has_message": bool(str(e))}
    return {"raised": None}


# ------------------------------------------------- the memcached client ----
#
# `memcached.py` is classified STATIC because `__init__` imports `pylibmc`,
# which is in neither image. But `perform_request` -- the whole of the file's
# migration risk -- needs no server and no package: what it uses from below is
# `get` and `set`, and what it does above them is the str/bytes boundary this
# migration is most likely to get wrong. Step 05's instruction says in as many
# words that `if raw:` and `isinstance(raw, bytes)` are different tests and
# that the first one is the contract; nothing enforced that until these cases.
#
# The stub goes into `sys.modules` for the life of one call and comes out
# again, so no case can see another's. It is not a mock framework and not a
# memcached: it is the two methods the file calls, recording what it was asked
# for, because the key the tree builds is the only place a `url_prefix` or a
# `params` change shows up as a number.


class _StubMemcacheClient(object):
    def __init__(self, values=None, set_result=True):
        self.values = values or {}
        self.set_result = set_result
        self.calls = []

    def get(self, key):
        self.calls.append(("get", key))
        return self.values.get(key)

    def set(self, key, value):
        self.calls.append(("set", key, value))
        return self.set_result


def _memcached(values=None, set_result=True, **kw):
    """`(connection, stub)`, with `pylibmc` satisfied for the construction."""
    import sys
    import types
    client = _StubMemcacheClient(values, set_result)
    mod = types.ModuleType("pylibmc")
    mod.Client = lambda servers, behaviors=None: client
    had = sys.modules.get("pylibmc")
    sys.modules["pylibmc"] = mod
    try:
        from elasticsearch.connection.memcached import MemcachedConnection
        kw.setdefault("host", "mc.example")
        kw.setdefault("port", 11211)
        return MemcachedConnection(**kw), client
    finally:
        if had is None:
            sys.modules.pop("pylibmc", None)
        else:
            sys.modules["pylibmc"] = had


def _mc_result(conn, client, *args, **kwargs):
    try:
        status, headers, data = conn.perform_request(*args, **kwargs)
    except Exception as e:
        return {"raised": type(e).__name__, "str": str(e),
                "calls": client.calls}
    return {"raised": None, "status": status, "headers": headers,
            "data": repr(data), "data_type": type(data).__name__,
            "calls": client.calls}


@case("conn.memcached-get-decodes-utf8")
def _(ctx):
    conn, client = _memcached({"/i": u'{"name": "ノード"}'.encode("utf-8")})
    return _mc_result(conn, client, "GET", "/i")


@case("conn.memcached-empty-value-is-not-decoded", tags=("adversarial",))
def _(ctx):
    # `if raw:` is false for b'', so the empty value is handed back exactly as
    # the client returned it -- still bytes, never decoded. An `isinstance`
    # test in its place would return u'' here, and every other case in this
    # block would still pass.
    conn, client = _memcached({"/i": b""})
    return _mc_result(conn, client, "GET", "/i")


@case("conn.memcached-miss-returns-none", tags=("adversarial",))
def _(ctx):
    conn, client = _memcached({})
    return _mc_result(conn, client, "GET", "/absent")


@case("conn.memcached-put-maps-to-set")
def _(ctx):
    conn, client = _memcached()
    return _mc_result(conn, client, "PUT", "/i/t/1", body=b'{"a": 1}')


@case("conn.memcached-a-refused-set-still-reports-200",
      tags=("adversarial",))
def _(ctx):
    # `if not json.dumps(mc.set(...))` -- `json.dumps(False)` is the string
    # 'false', which is truthy, so the 500 branch is unreachable. That is the
    # tree's behaviour today and a migration does not get to fix it here.
    conn, client = _memcached(set_result=False)
    return _mc_result(conn, client, "POST", "/i/t", body=b"{}")


@case("conn.memcached-delete-reads-rather-than-deletes",
      tags=("adversarial",))
def _(ctx):
    # 'DELETE' maps to 'delete', which is not 'set', so the else-branch runs
    # and the request is a `get`. Pinned because it looks exactly like the
    # kind of thing a port tidies up.
    conn, client = _memcached({"/i/t/1": b"gone"})
    return _mc_result(conn, client, "DELETE", "/i/t/1")


@case("conn.memcached-url-prefix-and-params-build-the-key")
def _(ctx):
    conn, client = _memcached({"/pfx/i/_search?q=a%3Ab": b"[]"},
                              url_prefix="/pfx")
    return _mc_result(conn, client, "GET", "/i/_search",
                      params={"q": "a:b"})


@case("conn.memcached-error-status-raises", tags=("adversarial",))
def _(ctx):
    conn, client = _memcached(
        {"/i/t/9": b'{"status": 404, "error": "IndexMissingException[[i]]"}'})
    return _mc_result(conn, client, "GET", "/i/t/9")


@case("conn.memcached-error-without-status-raises", tags=("adversarial",))
def _(ctx):
    conn, client = _memcached({"/i": b'{"error": "broken"}'})
    return _mc_result(conn, client, "GET", "/i")


@case("conn.memcached-ignored-status-is-returned", tags=("adversarial",))
def _(ctx):
    conn, client = _memcached({"/i": b'{"status": 404, "error": "nope"}'})
    return _mc_result(conn, client, "HEAD", "/i", ignore=(404,))


@case("conn.memcached-a-raising-client-becomes-connectionerror",
      tags=("adversarial",))
def _(ctx):
    class _Boom(_StubMemcacheClient):
        def get(self, key):
            self.calls.append(("get", key))
            raise RuntimeError("socket went away")

    import sys
    import types
    client = _Boom()
    mod = types.ModuleType("pylibmc")
    mod.Client = lambda servers, behaviors=None: client
    had = sys.modules.get("pylibmc")
    sys.modules["pylibmc"] = mod
    try:
        from elasticsearch.connection.memcached import MemcachedConnection
        conn = MemcachedConnection(host="mc.example", port=11211)
    finally:
        if had is None:
            sys.modules.pop("pylibmc", None)
        else:
            sys.modules["pylibmc"] = had
    out = _mc_result(conn, client, "GET", "/i")
    # The `finally` released it, so the next request gets the same object back
    # rather than building a second one.
    out["released"] = conn._get_connection() is client
    return out
