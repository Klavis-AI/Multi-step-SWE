# -*- coding: utf-8 -*-
"""``requests`` cookies, auth, adapters and sessions.

``requests_api.py`` drives the parts of requests the Elasticsearch transport
touches. This module covers the rest of the package, because a 2->3 migration
does not get to choose which half of a vendored library it breaks:

* ``cookies.py`` subclasses ``cookielib.CookieJar`` (``http.cookiejar`` on 3,
  reached through ``requests.compat``) and implements a ``MutableMapping`` by
  hand, complete with ``iterkeys``/``itervalues``/``iteritems``.
* ``auth.py`` builds the digest response out of ``hashlib`` over ``str`` that
  it encodes itself, and picks its hash function with a chain of ``if``
  branches that falls through to an unbound local for an unknown algorithm.
* ``adapters.py`` and ``sessions.py`` are where ``basestring``,
  ``urlparse`` and the pickling protocol (``__getstate__``/``__setstate__``
  over ``__attrs__``) come in.

The digest cnonce is seeded from ``os.urandom``; ``harness/normalize.py``
replaces it and the digest computed over it, and nothing else in the
Authorization header.
"""
from ..registry import case
from ..server import Route, json_route
from . import doc_escapes

REALM = 'Digest realm="es", nonce="0123456789abcdef", opaque="op4qu3", qop="auth"'


def _try(fn):
    try:
        return {"ok": fn()}
    except Exception as exc:
        return {"raised": type(exc).__name__, "str": str(exc)}


def _cookie_fields(c):
    return {k: getattr(c, k) for k in
            ("version", "name", "value", "port", "port_specified", "domain",
             "domain_specified", "domain_initial_dot", "path",
             "path_specified", "secure", "expires", "discard", "comment",
             "comment_url", "rfc2109")}


# --- cookies --------------------------------------------------------------

@case("requests.cookiejar-mapping-api")
def _(ctx):
    from requests.cookies import RequestsCookieJar
    jar = RequestsCookieJar()
    jar.set("a", "1")
    jar.set("b", "2", domain="example.com", path="/sub")
    jar.set("c", "3", domain="other.example.com", path="/")
    return {"keys": sorted(jar.keys()),
            "values": sorted(jar.values()),
            "items": sorted(jar.items()),
            # The jar's own ``iterkeys``/``itervalues``/``iteritems`` are py2
            # spellings named for removal on the first rung; calling one from
            # an inherited case would make it unpassable from there on.
            # What has to survive is the cookielib protocol
            # underneath them, which is the surprising half anyway: this class
            # presents a Mapping whose ``keys()`` are names, but iterating it
            # yields whole Cookie objects, so ``dict(jar)`` and ``sorted(jar)``
            # do not do what a Mapping would lead you to expect.
            "iter_yields": sorted(c.name for c in jar),
            "iter_item_types": sorted({type(c).__name__ for c in jar}),
            "get_dict": jar.get_dict(),
            "get_dict_domain": jar.get_dict(domain="example.com"),
            "get_scoped": jar.get("b", domain="example.com", path="/sub"),
            "get_missing_default": jar.get("zzz", default="fallback"),
            "len": len(jar),
            "contains": ["a" in jar, "zzz" in jar],
            "getitem": jar["a"],
            "list_domains": sorted(jar.list_domains()),
            "list_paths": sorted(jar.list_paths()),
            "multiple_domains": jar.multiple_domains(),
            "repr_type": type(jar).__name__,
            "is_mutable_mapping": [hasattr(jar, "setdefault"),
                                   hasattr(jar, "pop"),
                                   hasattr(jar, "popitem"),
                                   hasattr(jar, "update")]}


@case("requests.cookiejar-mutation")
def _(ctx):
    from requests.cookies import (RequestsCookieJar, cookiejar_from_dict,
                                  merge_cookies, remove_cookie_by_name)
    jar = cookiejar_from_dict({"a": "1", "b": "2"})
    out = {"from_dict": sorted(jar.items())}
    out["setdefault_new"] = jar.setdefault("c", "3")
    out["setdefault_existing"] = jar.setdefault("a", "999")
    out["pop"] = jar.pop("b")
    out["pop_missing"] = _try(lambda: jar.pop("b"))
    out["pop_default"] = jar.pop("b", "gone")
    remove_cookie_by_name(jar, "c")
    out["after_remove"] = sorted(jar.items())
    jar.update(cookiejar_from_dict({"d": "4"}))
    out["after_update"] = sorted(jar.items())
    merged = merge_cookies(RequestsCookieJar(), {"e": "5"})
    out["merge_dict"] = sorted(merged.items())
    out["merge_jar"] = sorted(merge_cookies(merged, jar).items())
    out["merge_bad"] = _try(lambda: merge_cookies("notajar", {}))
    out["set_none_deletes"] = _try(lambda: (jar.set("d", None),
                                            sorted(jar.items()))[1])
    copied = jar.copy()
    out["copy"] = sorted(copied.items())
    out["copy_is_independent"] = (copied.set("z", "9"),
                                  sorted(jar.items()))[1]
    jar.clear()
    out["cleared"] = sorted(jar.items())
    out["overwrite_keeps_one"] = _try(lambda: (
        jar.set("k", "v1"), jar.set("k", "v2"), jar["k"], len(jar))[2:])
    return out


@case("requests.cookiejar-conflict", tags=("adversarial",))
def _(ctx):
    # Two cookies with the same name in different domains is the case the
    # hand-written MutableMapping cannot express: __getitem__ must raise
    # CookieConflictError rather than silently pick one.
    from requests.cookies import CookieConflictError, RequestsCookieJar
    jar = RequestsCookieJar()
    jar.set("dup", "left", domain="a.example.com", path="/")
    jar.set("dup", "right", domain="b.example.com", path="/")
    out = {"len": len(jar),
           "getitem": _try(lambda: jar["dup"]),
           "get": _try(lambda: jar.get("dup")),
           "scoped_left": jar.get("dup", domain="a.example.com"),
           "scoped_right": jar.get("dup", domain="b.example.com"),
           "get_dict_picks": _try(lambda: jar.get_dict()),
           "multiple_domains": jar.multiple_domains(),
           "is_subclass": issubclass(CookieConflictError, RuntimeError)}
    jar.set("dup", "same", domain="a.example.com", path="/deeper")
    out["path_conflict"] = _try(lambda: jar["dup"])
    return out


@case("requests.create-cookie-and-morsel")
def _(ctx):
    from requests.cookies import create_cookie, morsel_to_cookie
    try:
        from http.cookies import Morsel
    except ImportError:  # pragma: no cover - Python 2 path
        from Cookie import Morsel
    c = create_cookie("name", "value", domain="example.com", path="/x",
                      secure=True, rest={"HttpOnly": True})
    out = {"default": _cookie_fields(create_cookie("n", "v")),
           "explicit": _cookie_fields(c),
           "has_nonstandard": c.has_nonstandard_attr("HttpOnly"),
           "bad_kwarg": _try(lambda: create_cookie("n", "v", bogus=1))}

    # Max-Age is a *latent defect*, not a working path: morsel_to_cookie does
    # ``time.time() + morsel['max-age']`` and a Morsel value is always a
    # string, so every Set-Cookie carrying Max-Age raises here. Pinning the
    # TypeError keeps a migration from quietly "fixing" it -- and it is why no
    # wall-clock value can leak out of this case.
    m = Morsel()
    m.set("mname", "mvalue", "mvalue")
    m["domain"] = "example.com"
    m["path"] = "/"
    m["max-age"] = "60"
    m["version"] = "1"
    m["comment"] = "hi"
    out["from_max_age"] = _try(lambda: _cookie_fields(morsel_to_cookie(m)))
    m["max-age"] = 60
    out["from_max_age_int"] = _try(
        lambda: sorted(k for k, v in _cookie_fields(
            morsel_to_cookie(m)).items() if k != "expires"))

    # The expires branch parses with an explicit template and subtracts
    # time.timezone, so with TZ pinned to UTC the epoch it produces is fixed.
    e = Morsel()
    e.set("ename", "evalue", "evalue")
    e["expires"] = "Thu, 01-Jan-2032 00:00:00 GMT"
    out["from_expires"] = _cookie_fields(morsel_to_cookie(e))

    bad = Morsel()
    bad.set("bname", "bvalue", "bvalue")
    bad["expires"] = "Thu, 01 Jan 2032 00:00:00 GMT"   # RFC 1123 spacing
    out["rfc1123_expires_rejected"] = _try(
        lambda: _cookie_fields(morsel_to_cookie(bad)))

    bad2 = Morsel()
    bad2.set("bname", "bvalue", "bvalue")
    bad2["expires"] = "not-a-date"
    out["bad_expires"] = _try(lambda: _cookie_fields(morsel_to_cookie(bad2)))
    return out


@case("requests.mockrequest-and-mockresponse")
def _(ctx):
    # The two shims that let stdlib cookielib talk to a requests
    # PreparedRequest. They are pure adapter code and every method on them is
    # called by the policy engine, so they are worth pinning wholesale.
    from requests.cookies import (MockRequest, MockResponse,
                                  extract_cookies_to_jar, get_cookie_header,
                                  RequestsCookieJar)
    from requests.models import PreparedRequest, Response
    from requests.structures import CaseInsensitiveDict

    req = PreparedRequest()
    req.prepare(method="GET", url="https://example.com/a/b?q=1",
                headers={"Cookie": "seed=1"})
    mock = MockRequest(req)
    out = {"type": mock.get_type(),
           "host": mock.get_host(),
           "origin_req_host": mock.get_origin_req_host(),
           "full_url": mock.get_full_url(),
           "unverifiable": mock.is_unverifiable(),
           "has_header": [mock.has_header("Cookie"), mock.has_header("X")],
           "get_header": mock.get_header("Cookie"),
           "get_header_default": mock.get_header("X-Absent", "fallback"),
           "new_headers_empty": dict(mock.get_new_headers()),
           "unredirected": (mock.add_unredirected_header("Cookie2", "x"),
                            sorted(req.headers.items()),
                            dict(mock.get_new_headers()))[1:],
           "add_header": _try(lambda: mock.add_header("X", "1")),
           "origin_req_host_property": mock.origin_req_host,
           "host_property": mock.host,
           "unverifiable_property": mock.unverifiable}

    resp = Response()
    resp.headers = CaseInsensitiveDict()
    jar = RequestsCookieJar()

    class FakeRaw(object):
        def __init__(self, headers):
            self._original_response = self
            self.msg = self
            self._headers = headers

        def getheaders(self, name):
            return [v for k, v in self._headers if k.lower() == name.lower()]

        def get_all(self, name, default=None):
            got = self.getheaders(name)
            return got or default

        def info(self):
            return self

    resp.raw = FakeRaw([("Set-Cookie", "x=1; Path=/; Domain=example.com"),
                        ("Set-Cookie", "y=2; Path=/a")])
    extract_cookies_to_jar(jar, req, resp.raw)
    out["extracted"] = sorted(jar.items())
    out["cookie_header"] = get_cookie_header(jar, req)
    out["mockresponse_info_is_self"] = (
        MockResponse(resp.raw.info()).info() is resp.raw)
    return out


def _cookie_fixture():
    return {
        "/set": Route(status=200, body=b"{}", headers=[
            ("content-type", "application/json"),
            ("set-cookie", "sess=abc; Path=/; HttpOnly"),
            ("set-cookie", "pref=dark; Path=/; Max-Age=600"),
        ]),
        "/echo": json_route({"echo": True}),
        "/expire": Route(status=200, body=b"{}", headers=[
            ("content-type", "application/json"),
            ("set-cookie", "sess=; Path=/; Max-Age=0"),
        ]),
    }


@case("requests.cookies-over-the-wire", fixture=_cookie_fixture)
def _(ctx):
    import requests
    s = requests.Session()
    first = s.get(ctx.url("/set"))
    second = s.get(ctx.url("/echo"))
    out = {"jar": sorted(s.cookies.items()),
           "first_cookies": sorted(first.cookies.items()),
           "sent_on_second": second.request.headers.get("Cookie"),
           "per_request_cookie": s.get(
               ctx.url("/echo"), cookies={"adhoc": "1"}
           ).request.headers.get("Cookie")}
    s.get(ctx.url("/expire"))
    out["after_expiry"] = sorted(s.cookies.items())
    s.cookies.clear_session_cookies()
    out["after_clear_session"] = sorted(s.cookies.items())
    s.cookies.clear_expired_cookies()
    out["after_clear_expired"] = sorted(s.cookies.items())
    return out


# --- auth ------------------------------------------------------------------

@case("requests.auth-basic-and-proxy")
def _(ctx):
    from requests.auth import AuthBase, HTTPBasicAuth, HTTPProxyAuth
    from requests.models import PreparedRequest

    def prepared():
        r = PreparedRequest()
        r.prepare(method="GET", url="http://example.com/")
        return r

    b = HTTPBasicAuth("user", "pass")
    p = HTTPProxyAuth("user", "pass")
    out = {"basic_header": b(prepared()).headers["Authorization"],
           "proxy_header": p(prepared()).headers["Proxy-Authorization"],
           "eq": b == HTTPBasicAuth("user", "pass"),
           "ne": b != HTTPBasicAuth("user", "other"),
           "eq_other_type": b == p,
           # AuthBase is an abstract marker: calling it raises rather than
           # passing the request through.
           "authbase_call": _try(lambda: AuthBase()(prepared()))}
    # Non-ASCII and bytes credentials: _basic_auth_str has to decide an
    # encoding, and which one it picks is observable in the base64.
    for name, creds in (("unicode", (u"usér", u"pässword")),
                        ("bytes", (b"user", b"pass")),
                        ("int", (1234, 5678)),
                        ("empty", ("", ""))):
        out[name] = _try(lambda c=creds: HTTPBasicAuth(*c)(
            prepared()).headers["Authorization"])
    return out


def _digest_fixture():
    ok = json_route({"authorized": True})
    return {
        "/protected": [Route(status=401, body=b'{"error":"auth"}',
                             headers=[("content-type", "application/json"),
                                      ("www-authenticate", REALM)]),
                       ok, ok, ok],
        "/protected2": [Route(status=401, body=b'{"error":"auth"}',
                              headers=[("content-type", "application/json"),
                                       ("www-authenticate", REALM)]),
                        ok],
    }


@case("requests.auth-digest-flow", fixture=_digest_fixture)
def _(ctx):
    import requests
    from requests.auth import HTTPDigestAuth
    s = requests.Session()
    auth = HTTPDigestAuth("user", "pass")
    r = s.get(ctx.url("/protected"), auth=auth)
    out = {"status": r.status_code,
           "history": [h.status_code for h in r.history],
           "chal": dict(auth.chal),
           "nonce_count": auth.nonce_count,
           "last_nonce": auth.last_nonce,
           "num_401_calls": auth.num_401_calls,
           "auth_header": r.request.headers.get("Authorization")}
    # A second request on the same auth object reuses the nonce and bumps nc.
    r2 = s.get(ctx.url("/protected2"), auth=auth)
    out["second_nc"] = auth.nonce_count
    out["second_header"] = r2.request.headers.get("Authorization")
    out["wire"] = [(w["method"], w["path"]) for w in ctx.fx.wire()]
    return out


@case("requests.auth-digest-algorithms")
def _(ctx):
    from requests.auth import HTTPDigestAuth
    out = {}
    for algo in (None, "MD5", "md5", "MD5-SESS", "SHA"):
        a = HTTPDigestAuth("user", "pass")
        a.chal = {"realm": "es", "nonce": "0123456789abcdef", "qop": "auth"}
        if algo is not None:
            a.chal["algorithm"] = algo
        out[str(algo)] = a.build_digest_header("GET", "http://h/a/b?q=1")
    # No qop at all, and a qop this implementation refuses.
    for qop in (None, "auth-int", "auth,auth-int"):
        a = HTTPDigestAuth("user", "pass")
        a.chal = {"realm": "es", "nonce": "0123456789abcdef"}
        if qop is not None:
            a.chal["qop"] = qop
        out["qop=%s" % qop] = a.build_digest_header("POST", "http://h/")
    out["empty_path"] = HTTPDigestAuth("u", "p")
    out["empty_path"].chal = {"realm": "r", "nonce": "n"}
    out["empty_path"] = out["empty_path"].build_digest_header("GET",
                                                              "http://h")
    return out


@case("requests.auth-digest-unknown-algorithm", tags=("adversarial",))
def _(ctx):
    # build_digest_header assigns hash_utf8 inside two if-branches and then
    # reads it unconditionally. An algorithm it does not recognise -- SHA-256
    # is the one a real server is most likely to offer -- reaches that read
    # with the name unbound. On Python 2 and 3 alike that is an
    # UnboundLocalError, and it is a latent defect rather than a behaviour to
    # "fix": a migration that adds a fallback changes what every caller sees.
    from requests.auth import HTTPDigestAuth
    out = {}
    for algo in ("SHA-256", "SHA-512-SESS", "", "sha"):
        a = HTTPDigestAuth("user", "pass")
        a.chal = {"realm": "es", "nonce": "0123456789abcdef", "qop": "auth",
                  "algorithm": algo}
        out[repr(algo)] = _try(
            lambda a=a: a.build_digest_header("GET", "http://h/"))
    missing = HTTPDigestAuth("user", "pass")
    missing.chal = {}
    out["no_realm"] = _try(lambda: missing.build_digest_header("GET",
                                                               "http://h/"))
    return out


def _digest_401_twice():
    ok = json_route({"authorized": True})
    return {"/always401": [Route(status=401, body=b"{}",
                                 headers=[("content-type", "application/json"),
                                          ("www-authenticate", REALM)])] * 4,
            "/once": [Route(status=401, body=b"{}",
                            headers=[("content-type", "application/json"),
                                     ("www-authenticate", REALM)]), ok]}


@case("requests.auth-digest-gives-up", fixture=_digest_401_twice,
      tags=("adversarial",))
def _(ctx):
    # handle_401 retries exactly once: num_401_calls guards the recursion. A
    # server that answers 401 forever must produce two requests and a 401, not
    # a loop -- and the counter must reset for the next response.
    import requests
    from requests.auth import HTTPDigestAuth
    s = requests.Session()
    auth = HTTPDigestAuth("user", "pass")
    r = s.get(ctx.url("/always401"), auth=auth)
    out = {"status": r.status_code,
           "history_len": len(r.history),
           "requests_seen": len(ctx.fx.wire()),
           "num_401_calls": auth.num_401_calls}
    r2 = s.get(ctx.url("/once"), auth=HTTPDigestAuth("user", "pass"))
    out["fresh_auth_status"] = r2.status_code
    out["total_requests"] = len(ctx.fx.wire())
    return out


# --- adapters --------------------------------------------------------------

@case("requests.adapter-surface")
def _(ctx):
    from requests.adapters import (DEFAULT_CA_BUNDLE_PATH, DEFAULT_POOLBLOCK,
                                   DEFAULT_POOLSIZE, DEFAULT_RETRIES,
                                   BaseAdapter, HTTPAdapter)
    from requests.models import PreparedRequest
    a = HTTPAdapter()
    req = PreparedRequest()
    req.prepare(method="GET", url="http://example.com/a/b?q=1")
    out = {"defaults": [DEFAULT_POOLSIZE, DEFAULT_POOLBLOCK, DEFAULT_RETRIES],
           "ca_bundle": DEFAULT_CA_BUNDLE_PATH,
           "attrs": sorted(a.__attrs__),
           "max_retries": repr(a.max_retries),
           "poolmanager_type": type(a.poolmanager).__name__,
           "request_url_direct": a.request_url(req, {}),
           "request_url_proxied": a.request_url(
               req, {"http": "http://proxy.example.com:8080"}),
           "proxy_headers_plain": a.proxy_headers("http://p:8080"),
           "proxy_headers_auth": sorted(
               a.proxy_headers("http://u:p@proxy:8080").keys()),
           "add_headers_is_noop": (a.add_headers(req), sorted(
               req.headers.keys()))[1],
           "base_adapter_send": _try(lambda: BaseAdapter().send(req)),
           "base_adapter_close": _try(lambda: BaseAdapter().close())}
    out["pickle_roundtrip"] = _try(lambda: _pickle_attrs(a))
    return out


def _pickle_attrs(adapter):
    import pickle
    clone = pickle.loads(pickle.dumps(adapter))
    return {"type": type(clone).__name__,
            "has_poolmanager": clone.poolmanager is not None,
            "max_retries": repr(clone.max_retries)}


@case("requests.adapter-cert-verify", tags=("adversarial",))
def _(ctx):
    # cert_verify decides between the vendored CA bundle, a caller-supplied
    # path and no verification at all, and raises IOError for a path that is
    # not there. Which branch fires is the whole of requests' TLS policy.
    from requests.adapters import HTTPAdapter
    from requests.models import PreparedRequest
    a = HTTPAdapter()
    req = PreparedRequest()
    req.prepare(method="GET", url="https://example.com/")

    def probe(verify, cert=None):
        conn = type("C", (object,), {})()
        a.cert_verify(conn, req.url, verify, cert)
        return {k: (v if not isinstance(v, str) else v)
                for k, v in sorted(vars(conn).items())}

    out = {"verify_true": probe(True),
           "verify_false": probe(False),
           "verify_missing_path": _try(lambda: probe("/nonexistent/ca.pem")),
           "cert_tuple_missing": _try(
               lambda: probe(True, ("/nope.crt", "/nope.key"))),
           "cert_str_missing": _try(lambda: probe(True, "/nope.pem"))}
    http_req = PreparedRequest()
    http_req.prepare(method="GET", url="http://example.com/")
    conn = type("C", (object,), {})()
    a.cert_verify(conn, http_req.url, True, None)
    out["plain_http_untouched"] = sorted(vars(conn).items())
    return out


@case("requests.adapter-get-connection")
def _(ctx):
    from requests.adapters import HTTPAdapter
    a = HTTPAdapter(pool_connections=2, pool_maxsize=3, pool_block=True,
                    max_retries=2)
    out = {}
    for url in ("http://example.com/a", "https://example.com/a",
                "http://example.com:8080/a"):
        conn = a.get_connection(url)
        out[url] = {"type": type(conn).__name__, "host": conn.host,
                    "port": conn.port, "maxsize": conn.pool.maxsize
                    if hasattr(conn.pool, "maxsize") else None,
                    "block": conn.block}
    proxied = a.get_connection("http://example.com/a",
                               proxies={"http": "http://proxy:8080"})
    out["proxied"] = {"type": type(proxied).__name__, "host": proxied.host,
                      "port": proxied.port}
    out["same_url_same_pool"] = (
        a.get_connection("http://example.com/a") is
        a.get_connection("http://example.com/b"))
    out["proxy_manager_keys"] = sorted(a.proxy_manager.keys())
    out["socks_unsupported"] = _try(
        lambda: a.get_connection("http://e/", {"http": "socks5://p:1080"}))
    a.close()
    out["closed"] = True
    return out


@case("requests.adapter-build-response")
def _(ctx):
    import io

    from requests.adapters import HTTPAdapter
    from requests.models import PreparedRequest
    from urllib3.response import HTTPResponse
    from urllib3._collections import HTTPHeaderDict

    req = PreparedRequest()
    req.prepare(method="GET", url="http://example.com/a")
    raw = HTTPResponse(body=io.BytesIO(b"hello"), status=201,
                       reason="Created", preload_content=False,
                       headers=HTTPHeaderDict({"content-type": "text/plain",
                                               "x-custom": "1"}))
    resp = HTTPAdapter().build_response(req, raw)
    return {"status": resp.status_code, "reason": resp.reason,
            "headers": sorted(resp.headers.items()),
            "url": resp.url, "encoding": resp.encoding,
            "content": resp.content.decode("ascii"),
            "request_is_same": resp.request is req,
            "cookies": sorted(resp.cookies.items()),
            "elapsed_is_timedelta": type(resp.elapsed).__name__,
            "connection_type": type(resp.connection).__name__}


# --- sessions --------------------------------------------------------------

@case("requests.session-mount-and-adapters")
def _(ctx):
    import requests
    from requests.adapters import HTTPAdapter

    class Marker(HTTPAdapter):
        pass

    s = requests.Session()
    s.mount("http://special.example.com", Marker())
    s.mount("http://special.example.com/deep", Marker())
    return {"adapters": list(s.adapters.keys()),
            "get_http": type(s.get_adapter("http://x/")).__name__,
            "get_special": type(
                s.get_adapter("http://special.example.com/a")).__name__,
            "longest_prefix_wins": list(s.adapters.keys())[0],
            "no_adapter": _try(lambda: s.get_adapter("ftp://x/")),
            "session_attrs": sorted(s.__attrs__),
            "default_headers": sorted(s.headers.keys())}


@case("requests.session-prepare-request-merges")
def _(ctx):
    import requests
    s = requests.Session()
    s.headers["X-Session"] = "yes"
    s.headers["X-Both"] = "session"
    s.params = {"sp": "1"}
    s.auth = ("u", "p")
    s.cookies.set("sc", "1")
    req = requests.Request("POST", "http://example.com/a?rq=1",
                           headers={"X-Both": "request",
                                    "X-Request": "yes",
                                    "X-Dropped": None},
                           params={"rp": "2"},
                           data={"k": "v"},
                           cookies={"rc": "2"})
    p = s.prepare_request(req)
    return {"url": p.url,
            "headers": sorted((k, v) for k, v in p.headers.items()),
            "body": p.body,
            "method": p.method,
            "has_auth": "Authorization" in p.headers,
            "path_url": p.path_url}


@case("requests.session-merge-environment-settings")
def _(ctx):
    # The runner strips every proxy and CA environment variable, so this
    # observes the merge logic itself rather than the box it runs on.
    import os
    import requests
    s = requests.Session()
    s.trust_env = True
    s.proxies = {"http": "http://session-proxy:1"}
    s.verify = "/session/ca.pem"
    merged = s.merge_environment_settings(
        "http://example.com/", {"https": "http://call-proxy:2"},
        None, None, None)
    s.trust_env = False
    untrusted = s.merge_environment_settings(
        "http://example.com/", {}, True, True, None)
    return {"env_http_proxy": os.environ.get("HTTP_PROXY"),
            "no_proxy": os.environ.get("NO_PROXY"),
            "merged": {k: v for k, v in merged.items()},
            "untrusted": {k: v for k, v in untrusted.items()},
            "stream_override": s.merge_environment_settings(
                "http://e/", {}, True, False, None)["stream"]}


def _redirect_fixture():
    return {
        "/start": Route(status=303, body=b"", headers=[
            ("location", "/next"), ("set-cookie", "hop=1; Path=/")]),
        "/next": Route(status=307, body=b"", headers=[("location", "/final")]),
        "/final": json_route({"arrived": True}),
        "/loop": Route(status=302, body=b"", headers=[("location", "/loop")]),
        "/relative": Route(status=302, body=b"",
                           headers=[("location", "final")]),
        "/stripauth": Route(status=302, body=b"",
                            headers=[("location", "http://127.0.0.1:1/x")]),
    }


@case("requests.session-resolve-redirects", fixture=_redirect_fixture)
def _(ctx):
    import requests
    s = requests.Session()
    r = s.post(ctx.url("/start"), data={"k": "v"})
    out = {"final_url": r.url, "status": r.status_code,
           "history": [(h.status_code, h.url, h.request.method)
                       for h in r.history],
           "final_method": r.request.method,
           "final_body": r.request.body,
           "cookies_carried": r.request.headers.get("Cookie"),
           "relative": s.get(ctx.url("/relative")).url,
           "loop": _try(lambda: s.get(ctx.url("/loop")).url),
           "no_follow": s.get(ctx.url("/start"),
                              allow_redirects=False).status_code,
           "wire": [(w["method"], w["path"]) for w in ctx.fx.wire()]}
    return out


@case("requests.session-rebuild-auth-strips-on-host-change",
      tags=("adversarial",))
def _(ctx):
    # Carrying an Authorization header across a cross-host redirect is a
    # credential leak; rebuild_auth exists to stop it. The check compares
    # netlocs pulled apart with urlparse, which is exactly the kind of code a
    # 2->3 rewrite touches.
    import requests
    from requests.models import PreparedRequest, Response
    s = requests.Session()

    def rebuild(from_url, to_url):
        prep = PreparedRequest()
        prep.prepare(method="GET", url=to_url,
                     headers={"Authorization": "Basic dTpw"})
        resp = Response()
        resp.request = PreparedRequest()
        resp.request.prepare(method="GET", url=from_url,
                             headers={"Authorization": "Basic dTpw"})
        resp.url = from_url
        s.rebuild_auth(prep, resp)
        return prep.headers.get("Authorization")

    return {"same_host": rebuild("http://a.example.com/1",
                                 "http://a.example.com/2"),
            "other_host": rebuild("http://a.example.com/1",
                                  "http://b.example.com/2"),
            "http_to_https": rebuild("http://a.example.com/1",
                                     "https://a.example.com/2"),
            "https_to_http": rebuild("https://a.example.com/1",
                                     "http://a.example.com/2"),
            "port_change": rebuild("http://a.example.com/1",
                                   "http://a.example.com:8080/2"),
            "rebuild_proxies": sorted(s.rebuild_proxies(
                _prep("http://a.example.com/1"), {}).items())}


def _prep(url, method="GET", **kw):
    from requests.models import PreparedRequest
    p = PreparedRequest()
    p.prepare(method=method, url=url, **kw)
    return p


def _hook_fixture():
    return {"/h": json_route({"hooked": True})}


@case("requests.session-hooks-and-pickle", fixture=_hook_fixture)
def _(ctx):
    import pickle
    import requests
    from requests.hooks import HOOKS, default_hooks, dispatch_hook
    seen = []

    def hook(r, *args, **kwargs):
        seen.append((r.status_code, sorted(kwargs.keys())))
        return r

    s = requests.Session()
    s.hooks["response"].append(hook)
    r = s.get(ctx.url("/h"))
    out = {"hooks_registered": HOOKS,
           "default_hooks": {k: list(v) for k, v in default_hooks().items()},
           "seen": seen,
           "status": r.status_code,
           "dispatch_none": dispatch_hook("response", {}, "payload"),
           "dispatch_single": dispatch_hook(
               "response", {"response": lambda x: x.upper()}, "payload"),
           "dispatch_returns_none": dispatch_hook(
               "response", {"response": lambda x: None}, "payload")}
    fresh = requests.Session()
    clone = pickle.loads(pickle.dumps(fresh))
    out["pickled_session"] = {"headers": sorted(clone.headers.keys()),
                              "adapters": list(clone.adapters.keys()),
                              "hooks": {k: list(v) for k, v in
                                        clone.hooks.items()}}
    return out


@case("requests.session-context-manager-closes", fixture=_hook_fixture)
def _(ctx):
    import requests
    closed = []

    class Watcher(requests.adapters.HTTPAdapter):
        def close(self):
            closed.append(True)
            requests.adapters.HTTPAdapter.close(self)

    with requests.Session() as s:
        s.mount("http://", Watcher())
        status = s.get(ctx.url("/h")).status_code
    return {"status": status, "closed": len(closed),
            "module_api_returns_new_session": requests.get(
                ctx.url("/h")).status_code}


@case("requests.docstring-escapes-intact", tags=("adversarial",))
def _(ctx):
    # 15 of the 24 escaped docstrings in the tree, reached through three
    # modules of one package: the property is per-literal, not per-interface,
    # and ``requests.api`` is where two thirds of them live. ``:param
    # \*\*kwargs:`` is a Sphinx escape -- ``**kwargs`` would render as bold
    # -- so the backslashes are load-bearing prose, not debris. Step 03 has to
    # stop them warning without changing what they evaluate to; only the
    # escape census is recorded, never the text (see ``cases.doc_escapes``).
    from requests import api, models, sessions
    out = {}
    for name in ("get", "options", "head", "post", "put", "patch", "delete"):
        out["api." + name] = doc_escapes(getattr(api, name))
        out["Session." + name] = doc_escapes(getattr(sessions.Session, name))
    out["Response.json"] = doc_escapes(models.Response.json)
    return out
