# -*- coding: utf-8 -*-
"""The loopback fixture server the cases drive.

Hooks 1 and 2 of the hook list: it publishes a bind address/port and, for the
TLS cases, a certificate and key path. Both are handed to the tree through
arguments its own public API already accepts (``Elasticsearch(host, port)``,
``requests.get(url, verify=...)``, ``HTTPSConnectionPool(host, port,
ca_certs=...)``). Nothing here selects behaviour in the tree.

The server records what actually reached the socket. That recording, not the
client's own view, is the wire half of every observation.
"""
import json
import os
import ssl
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# Request headers that vary run to run or interpreter to interpreter and are
# normalised away rather than compared. Everything else is compared verbatim.
VOLATILE_REQUEST_HEADERS = frozenset({"host", "date"})


class Route(object):
    """One canned response, plus the few stateful behaviours the retry,
    redirect and connection-failure fixtures need."""

    __slots__ = ("status", "headers", "body", "delay", "close_early",
                 "chunked", "fail_times", "reason", "no_content_length")

    def __init__(self, status=200, headers=None, body=b"", delay=0.0,
                 close_early=False, chunked=False, fail_times=0, reason=None,
                 no_content_length=False):
        self.status = status
        self.headers = list(headers or [])
        self.body = body if isinstance(body, bytes) else body.encode("utf-8")
        self.delay = delay
        self.close_early = close_early
        self.chunked = chunked
        self.fail_times = fail_times
        self.reason = reason
        self.no_content_length = no_content_length


def json_route(obj, status=200, **kw):
    body = json.dumps(obj, sort_keys=True, separators=(",", ":")).encode("utf-8")
    hdrs = kw.pop("headers", [])
    return Route(status=status, body=body,
                 headers=[("content-type", "application/json")] + list(hdrs), **kw)


def gzip_route(obj, status=200, **kw):
    raw = json.dumps(obj, sort_keys=True, separators=(",", ":")).encode("utf-8")
    co = zlib.compressobj(9, zlib.DEFLATED, 16 + zlib.MAX_WBITS)
    body = co.compress(raw) + co.flush()
    hdrs = kw.pop("headers", [])
    return Route(status=status, body=body,
                 headers=[("content-type", "application/json"),
                          ("content-encoding", "gzip")] + list(hdrs), **kw)


class Fixture(object):
    """A running loopback server plus the log of what it saw."""

    def __init__(self, routes, default=None, tls=None, client_ca=None):
        self.routes = routes
        self.default = default or json_route({"ok": True})
        self.seen = []
        #: Common name of the client certificate presented on each accepted
        #: connection, or ``None`` where the client presented none. Kept apart
        #: from ``seen`` so that adding it moved no existing observation.
        self.peer_cns = []
        self._hits = {}
        self._lock = threading.Lock()

        fixture = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"
            server_version = "FixtureHTTP"
            sys_version = ""

            def log_message(self, *a):
                pass

            def _read_body(self):
                if self.headers.get("transfer-encoding", "").lower() == "chunked":
                    chunks = []
                    while True:
                        line = self.rfile.readline().strip()
                        n = int(line.split(b";")[0] or b"0", 16)
                        if n == 0:
                            self.rfile.readline()
                            break
                        chunks.append(self.rfile.read(n))
                        self.rfile.readline()
                    return b"".join(chunks)
                length = int(self.headers.get("content-length") or 0)
                return self.rfile.read(length) if length else b""

            def _dispatch(self):
                body = self._read_body()
                path = self.path
                with fixture._lock:
                    fixture.peer_cns.append(_peer_cn(self.connection))
                    n = fixture._hits.get((self.command, path), 0)
                    fixture._hits[(self.command, path)] = n + 1
                    fixture.seen.append({
                        "method": self.command,
                        "path": path,
                        "headers": sorted(
                            (k.lower(), v) for k, v in self.headers.items()
                            if k.lower() not in VOLATILE_REQUEST_HEADERS),
                        "body": body,
                    })
                route = fixture._route_for(self.command, path)
                if isinstance(route, list):
                    # A sequence route: the n-th hit on this (method, path)
                    # gets the n-th response, the last one repeating. Scroll
                    # and retry fixtures need the server to have a memory.
                    route = route[min(n, len(route) - 1)]
                if route.fail_times and n < route.fail_times:
                    route = fixture.routes.get("__retry_failure__") or Route(
                        status=503, body=b'{"error":"unavailable"}',
                        headers=[("content-type", "application/json"),
                                 ("retry-after", "0")])
                if route.delay:
                    time.sleep(route.delay)
                if route.close_early:
                    try:
                        self.wfile.write(b"HTTP/1.1 200 OK\r\ncontent-length: 40\r\n\r\nshort")
                        self.wfile.flush()
                    except Exception:
                        pass
                    self.close_connection = True
                    return
                self._respond(route)

            def _respond(self, route):
                body = b"" if self.command == "HEAD" else route.body
                self.send_response(route.status, route.reason)
                for k, v in route.headers:
                    self.send_header(k, v)
                if route.chunked:
                    self.send_header("transfer-encoding", "chunked")
                    self.end_headers()
                    mid = max(1, len(body) // 2)
                    for part in (body[:mid], body[mid:]):
                        self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
                    self.wfile.write(b"0\r\n\r\n")
                    return
                if not route.no_content_length:
                    self.send_header("content-length", str(len(route.body)))
                self.end_headers()
                if body:
                    self.wfile.write(body)

            do_GET = do_POST = do_PUT = do_DELETE = do_HEAD = _dispatch
            do_PATCH = do_OPTIONS = _dispatch

        class Server(ThreadingHTTPServer):
            # socketserver prints a traceback to stderr whenever a handler
            # thread dies, and a client that walks away mid-request kills one
            # every time: a rejected TLS handshake, a read timeout, a pool
            # closed with a response half-read. That traceback names the
            # *client's* ephemeral port, which is not the port normalisation
            # knows about, so it differs between the two recording passes and
            # the case is dropped as unstable -- exactly what happened to
            # `tls-verification-outcomes`. It is the fixture's own stderr, not
            # the tree's, so it is dropped here rather than normalised. What
            # the server did or did not see is still observable, through
            # `seen`/`wire()`, which is the half the client cannot fake.
            def handle_error(self, request, client_address):
                fixture.handler_errors += 1

        self.handler_errors = 0
        self._server = Server(("127.0.0.1", 0), Handler)
        self._server.daemon_threads = True
        self.scheme = "http"
        if tls:
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(tls)
            if client_ca:
                # CERT_OPTIONAL and not CERT_REQUIRED on purpose. With
                # CERT_REQUIRED and TLS 1.3 the server's refusal arrives only
                # once it has processed the client's Finished, so a client that
                # sends no certificate sometimes sees the alert while writing
                # and sometimes while reading the response -- a genuinely
                # timing-dependent answer, which the recorder would drop as
                # unstable and which would be a flake in the shipped verifier
                # if it did not. CERT_OPTIONAL asks for the certificate and
                # accepts the connection either way, so the observation becomes
                # "what did the client actually send", read off `peer_cns`:
                # deterministic, and the half the client cannot fake.
                ctx.verify_mode = ssl.CERT_OPTIONAL
                ctx.load_verify_locations(client_ca)
            self._server.socket = ctx.wrap_socket(self._server.socket,
                                                  server_side=True)
            self.scheme = "https"
        self.host = "127.0.0.1"
        self.port = self._server.server_address[1]
        self._thread = threading.Thread(target=self._server.serve_forever)
        self._thread.daemon = True
        self._thread.start()

    def _route_for(self, method, path):
        base = path.split("?")[0]
        for key in ("%s %s" % (method, path), path, base):
            if key in self.routes:
                return self.routes[key]
        # Suffix keys ("*/_search") are how an endpoint is matched independently
        # of the index prefix the client puts in front of it; prefix keys
        # ("/blog/*") match a whole subtree.
        for key, route in self.routes.items():
            if key.startswith("*") and base.endswith(key[1:]) and len(key) > 1:
                return route
        for key, route in self.routes.items():
            if key.endswith("*") and path.startswith(key[:-1]):
                return route
        return self.default

    @property
    def base(self):
        return "%s://%s:%d" % (self.scheme, self.host, self.port)

    def url(self, path=""):
        return self.base + path

    def wire(self):
        """What reached the socket, as a JSON-safe structure."""
        out = []
        for r in self.seen:
            out.append({
                "method": r["method"],
                "path": r["path"],
                "headers": [list(h) for h in r["headers"]],
                "body": _text_or_b64(r["body"]),
            })
        return out

    def close(self):
        try:
            self._server.shutdown()
        except Exception:
            pass
        try:
            self._server.server_close()
        except Exception:
            pass


def _text_or_b64(raw):
    import base64
    if not raw:
        return ""
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return {"b64": base64.b64encode(raw).decode("ascii")}


def _peer_cn(conn):
    """Common name of the peer certificate on a TLS connection, else None.

    ``getpeercert`` exists only on an ``SSLSocket`` and returns ``{}`` -- not
    ``None`` -- when the peer sent nothing under ``CERT_OPTIONAL``, so both
    have to be handled separately from "this is not a TLS connection at all".
    """
    get = getattr(conn, "getpeercert", None)
    if get is None:
        return None
    cert = get()
    if not cert:
        return None
    for rdn in cert.get("subject", ()):
        for key, value in rdn:
            if key == "commonName":
                return value
    return None


def certdir():
    from . import certs
    return certs.ensure()


def server_chain(label="server"):
    """Cert+key for the fixture to present. ``label`` selects one of the
    alternates in ``certs.EXTRA`` -- "mismatch" names a host the fixture is not
    reachable as, "untrusted" is issued by a CA nothing trusts."""
    return os.path.join(certdir(), "%s-chain.pem" % label)


def ca_pem():
    return os.path.join(certdir(), "ca.pem")
