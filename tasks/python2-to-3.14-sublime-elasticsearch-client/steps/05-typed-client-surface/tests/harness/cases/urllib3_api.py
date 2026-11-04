# -*- coding: utf-8 -*-
"""The vendored urllib3 1.10.4 -- the tree's actual TLS and pooling stack.

``util/ssl_.py`` opens with ``from ssl import wrap_socket, CERT_NONE,
PROTOCOL_SSLv23`` and, when that fails, substitutes a hand-rolled ``SSLContext``
class that warns about missing SNI. ``ssl.wrap_socket`` is gone at 3.12 and
``PROTOCOL_SSLv23`` has been a deprecated alias since 3.10, so the whole
import-time fallback has to be rebuilt. Everything below fixes what the module
does *around* that import: the constants it resolves, the contexts it builds,
the errors it raises, and the bytes the pool puts on the wire.
"""
from ..registry import case
from ..server import Route, gzip_route, json_route, ca_pem

DOC = {"ok": True, "t": u"日本語"}


def ok():
    return {"*": json_route(DOC)}


def _pool(ctx, **kw):
    from urllib3 import HTTPConnectionPool
    return HTTPConnectionPool(ctx.fx.host, port=ctx.fx.port, **kw)


# ---------------------------------------------------------- url parsing ----

@case("urllib3.parse-url")
def _(ctx):
    from urllib3.util.url import parse_url
    urls = ["http://x:9200/p?q=1#f", "x", "//x/p", "https://u:p@x:443/",
            "http://[::1]:9200/p", u"http://日本/p", "http://x/p%20q",
            "HTTP://X/P", "http://x", ""]
    out = []
    for u in urls:
        try:
            p = parse_url(u)
            out.append([u, {"scheme": p.scheme, "auth": p.auth, "host": p.host,
                            "port": p.port, "path": p.path, "query": p.query,
                            "fragment": p.fragment, "url": p.url,
                            "netloc": p.netloc, "request_uri": p.request_uri}])
        except Exception as e:
            out.append([u, "%s: %s" % (type(e).__name__, e)])
    return out


@case("urllib3.get-host", tags=("adversarial",))
def _(ctx):
    from urllib3.util.url import get_host, split_first
    out = {"hosts": [], "split_first": []}
    for u in ("http://x:9200/p", "x:9200", "https://x/", "unknown://x/",
              "http://[::1]:80/"):
        try:
            out["hosts"].append([u, list(get_host(u))])
        except Exception as e:
            out["hosts"].append([u, "%s: %s" % (type(e).__name__, e)])
    for args in (("a://b", [":", "://"]), ("abc", ["/"]), ("", ["/"])):
        out["split_first"].append(list(split_first(*args)))
    return out


@case("urllib3.urlencode-and-make-headers")
def _(ctx):
    from urllib3.request import urlencode
    from urllib3.util.request import make_headers
    return {
        "encoded": [urlencode({"a": 1, "b": u"日本"}),
                    urlencode([("a", 1), ("a", 2)])],
        "headers": [make_headers(keep_alive=True, accept_encoding=True),
                    make_headers(accept_encoding="gzip"),
                    make_headers(accept_encoding=["gzip", "deflate"]),
                    make_headers(user_agent=u"クライアント"),
                    make_headers(basic_auth="u:p"),
                    make_headers(proxy_basic_auth="u:p"),
                    make_headers(disable_cache=True)],
    }


# The TLS knobs in ``util/ssl_.py`` are driven in ``urllib3_more.py``, where
# every case runs against both vendored copies -- that file is where step 03
# does most of its work and the copy requests imports must not be left behind.


# ---------------------------------------------------------------- pool ----

@case("urllib3.pool-urlopen", fixture=ok)
def _(ctx):
    p = _pool(ctx)
    r = p.urlopen("GET", "/", retries=False)
    return {"status": r.status, "data": r.data.decode("utf-8"),
            "reason": r.reason,
            "headers": sorted((k.lower(), v) for k, v in r.headers.items())}


@case("urllib3.pool-request-fields", fixture=ok)
def _(ctx):
    p = _pool(ctx)
    p.request("GET", "/s", fields={"q": u"日本", "n": "1"}, retries=False)
    p.request("POST", "/s", fields={"q": u"日本"}, retries=False,
              encode_multipart=False)
    return [[r["method"], r["path"], r["body"]] for r in ctx.fx.seen]


@case("urllib3.pool-reuses-connection", fixture=ok)
def _(ctx):
    p = _pool(ctx, maxsize=1)
    before = p.pool.qsize()
    for _ in range(3):
        p.urlopen("GET", "/", retries=False).read()
    return {"before": before, "after": p.pool.qsize(),
            "num_requests": p.num_requests, "num_connections":
                p.num_connections}


@case("urllib3.response-decode-gzip", fixture=lambda: {"*": gzip_route(DOC)})
def _(ctx):
    p = _pool(ctx)
    r = p.urlopen("GET", "/", retries=False,
                  headers={"accept-encoding": "gzip"})
    return {"data": r.data.decode("utf-8"),
            "content_encoding": r.headers.get("content-encoding")}


@case("urllib3.response-stream-and-read", fixture=lambda: {
    "*": Route(body=u"一二三四五六七八九十".encode("utf-8"),
               headers=[("content-type", "text/plain")])})
def _(ctx):
    p = _pool(ctx)
    r = p.urlopen("GET", "/", retries=False, preload_content=False)
    chunks = [c for c in r.stream(7)]
    # ``closed`` is a property in this vintage, and on Python 3 it reports the
    # underlying file object rather than the HTTPResponse: draining the stream
    # exhausts the socket wrapper without the response knowing.
    return {"chunks": [len(c) for c in chunks],
            "joined": b"".join(chunks).decode("utf-8"), "closed": r.closed,
            "fp_is_none": r._fp is None,
            "read_after_stream": r.read()}


@case("urllib3.response-chunked", fixture=lambda: {
    "*": json_route(DOC, chunked=True)})
def _(ctx):
    p = _pool(ctx)
    r = p.urlopen("GET", "/", retries=False)
    return {"status": r.status, "data": r.data.decode("utf-8"),
            "te": r.headers.get("transfer-encoding")}


@case("urllib3.header-dict")
def _(ctx):
    from urllib3._collections import HTTPHeaderDict
    d = HTTPHeaderDict()
    d.add("Set-Cookie", "a=1")
    d.add("set-cookie", "b=2")
    d["Content-Type"] = "text/plain"
    return {"getlist": d.getlist("SET-COOKIE"),
            "joined": d["set-cookie"],
            "items": sorted(d.items()),
            "len": len(d),
            "in": "CONTENT-TYPE" in d,
            "copy_items": sorted(d.copy().items())}


@case("urllib3.recent-keys-cache")
def _(ctx):
    # RecentlyUsedContainer is an OrderedDict subclass with a lock; its
    # eviction order is behaviour the pool manager depends on.
    from urllib3._collections import RecentlyUsedContainer
    evicted = []
    c = RecentlyUsedContainer(3, dispose_func=evicted.append)
    for i in range(4):
        c[i] = "v%d" % i
    c[1]  # touch
    c[4] = "v4"
    return {"keys": list(c.keys()), "evicted": evicted, "len": len(c)}


@case("urllib3.fields-and-multipart")
def _(ctx):
    from urllib3.fields import RequestField, guess_content_type
    from urllib3.filepost import encode_multipart_formdata
    body, ctype = encode_multipart_formdata(
        [("plain", "value"),
         ("uni", u"日本語"),
         ("file", (u"名前.txt", u"中身".encode("utf-8"), "text/plain"))],
        boundary="FIXEDBOUNDARY")
    f = RequestField("n", b"d", filename=u"ファイル.png")
    f.make_multipart(content_type="image/png")
    return {"ctype": ctype,
            "body": body.decode("utf-8"),
            "guessed": [guess_content_type(n) for n in
                        ("a.txt", "a.png", "a.unknownext", None)],
            "field_header": f.render_headers()}


@case("urllib3.timeout-object")
def _(ctx):
    from urllib3.util.timeout import Timeout
    t = Timeout(connect=1.5, read=2.5)
    d = Timeout.from_float(3.0)
    return {"repr": repr(t), "connect": t.connect_timeout,
            "default_repr": repr(Timeout.DEFAULT_TIMEOUT),
            "from_float": repr(d),
            "clone_repr": repr(t.clone()),
            "total": repr(Timeout(total=5, connect=1).read_timeout)}


@case("urllib3.retry-object")
def _(ctx):
    from urllib3.util.retry import Retry
    r = Retry(total=5, connect=2, read=2, backoff_factor=0.5)
    steps = []
    cur = r
    for _ in range(4):
        cur = cur.increment(method="GET", url="/x", error=None)
        steps.append([cur.total, cur.get_backoff_time(), cur.is_exhausted()])
    # 1.10.4 has no ``_is_method_retryable``; the whitelist is consulted only
    # from is_forced_retry, and only together with a status code. The pair of
    # lists is what a later urllib3 would rename, so both are recorded.
    forced = Retry(total=1, status_forcelist=(503,))
    return {"repr": repr(r), "steps": steps,
            "default_methods": sorted(Retry.DEFAULT_METHOD_WHITELIST),
            "from_int": repr(Retry.from_int(3)),
            "forced": dict((m, bool(forced.is_forced_retry(m, 503)))
                           for m in ("GET", "POST", "head", "PUT")),
            "forced_other_status": bool(forced.is_forced_retry("GET", 500)),
            "no_forcelist": bool(Retry(total=1).is_forced_retry("GET", 503))}


@case("urllib3.retry-exhausted-raises", tags=("adversarial",))
def _(ctx):
    from urllib3.exceptions import MaxRetryError
    from urllib3.util.retry import Retry
    r = Retry(total=1)
    try:
        r.increment("GET", "/x").increment("GET", "/x")
    except Exception as e:
        return {"raised": type(e).__name__,
                "is_max_retry": isinstance(e, MaxRetryError),
                "str_has_url": "/x" in str(e)}
    return {"raised": None}


@case("urllib3.pool-retries-503", fixture=lambda: {
    "*": [json_route({"e": 1}, status=503), json_route({"e": 1}, status=503),
          json_route(DOC)]})
def _(ctx):
    from urllib3.util.retry import Retry
    p = _pool(ctx)
    r = p.urlopen("GET", "/", retries=Retry(total=3, status_forcelist=[503],
                                            backoff_factor=0))
    return {"status": r.status, "data": r.data.decode("utf-8"),
            "requests": len(ctx.fx.seen)}


@case("urllib3.pool-connect-refused", tags=("adversarial",))
def _(ctx):
    from urllib3 import HTTPConnectionPool
    from urllib3.exceptions import MaxRetryError
    p = HTTPConnectionPool("127.0.0.1", port=1, retries=False)
    try:
        p.urlopen("GET", "/")
    except Exception as e:
        return {"raised": type(e).__name__,
                "is_max_retry": isinstance(e, MaxRetryError)}
    return {"raised": None}


@case("urllib3.pool-read-timeout", tags=("adversarial",), fixture=lambda: {
    "*": json_route(DOC, delay=1.2)})
def _(ctx):
    from urllib3.exceptions import ReadTimeoutError
    from urllib3.util.timeout import Timeout
    p = _pool(ctx, timeout=Timeout(connect=2, read=0.25))
    try:
        p.urlopen("GET", "/", retries=False)
    except Exception as e:
        return {"raised": type(e).__name__,
                "is_read_timeout": isinstance(e, ReadTimeoutError)}
    return {"raised": None}


@case("urllib3.poolmanager-routes-by-host", fixture=ok)
def _(ctx):
    from urllib3 import PoolManager
    pm = PoolManager(num_pools=2)
    pm.urlopen("GET", ctx.url("/a"), retries=False, redirect=False).read()
    pm.urlopen("GET", ctx.url("/b"), retries=False, redirect=False).read()
    pm.connection_from_host("other.invalid", 80, "http")
    return {"pool_keys": sorted(str(k) for k in pm.pools.keys()),
            "paths": [r["path"] for r in ctx.fx.seen]}


@case("urllib3.poolmanager-follows-redirect", fixture=lambda: {
    "/a": Route(status=303, headers=[("location", "/b")]),
    "/b": json_route(DOC)})
def _(ctx):
    from urllib3 import PoolManager
    pm = PoolManager()
    r = pm.urlopen("POST", ctx.url("/a"), body=b"x", retries=3)
    return {"status": r.status, "data": r.data.decode("utf-8"),
            "methods": [x["method"] for x in ctx.fx.seen]}


@case("urllib3.https-verified", fixture=ok, tls=True)
def _(ctx):
    from urllib3 import HTTPSConnectionPool
    p = HTTPSConnectionPool(ctx.fx.host, port=ctx.fx.port,
                            cert_reqs="CERT_REQUIRED", ca_certs=ca_pem())
    r = p.urlopen("GET", "/", retries=False)
    return {"status": r.status, "data": r.data.decode("utf-8")}


@case("urllib3.https-untrusted-rejected", tags=("adversarial",), fixture=ok,
      tls=True)
def _(ctx):
    from urllib3 import HTTPSConnectionPool
    from urllib3.exceptions import SSLError
    p = HTTPSConnectionPool(ctx.fx.host, port=ctx.fx.port,
                            cert_reqs="CERT_REQUIRED")
    try:
        p.urlopen("GET", "/", retries=False)
    except Exception as e:
        return {"raised": type(e).__name__,
                "is_ssl_error": isinstance(e, SSLError)}
    return {"raised": None}


@case("urllib3.https-cert-none-warns", fixture=ok, tls=True)
def _(ctx):
    # cert_reqs=CERT_NONE emits InsecureRequestWarning, which is part of
    # urllib3's own contract and is therefore scored.
    from urllib3 import HTTPSConnectionPool
    p = HTTPSConnectionPool(ctx.fx.host, port=ctx.fx.port,
                            cert_reqs="CERT_NONE")
    r = p.urlopen("GET", "/", retries=False)
    return {"status": r.status, "data": r.data.decode("utf-8")}


@case("urllib3.exception-hierarchy")
def _(ctx):
    import urllib3.exceptions as ex
    names = ["HTTPError", "PoolError", "RequestError", "MaxRetryError",
             "HostChangedError", "TimeoutStateError", "TimeoutError",
             "ReadTimeoutError", "ConnectTimeoutError", "ProtocolError",
             "SSLError", "ProxyError", "DecodeError", "LocationValueError",
             "ResponseError", "SecurityWarning", "InsecureRequestWarning"]
    return {n: [c.__name__ for c in getattr(ex, n).__mro__[:4]] for n in names}
