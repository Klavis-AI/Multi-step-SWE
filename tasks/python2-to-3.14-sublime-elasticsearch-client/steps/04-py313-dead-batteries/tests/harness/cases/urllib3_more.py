# -*- coding: utf-8 -*-
"""``urllib3`` internals -- run against *both* copies in the tree.

The repo vendors urllib3 1.10.4 twice: once as ``lib/urllib3`` (what the
Elasticsearch transport imports) and once as ``lib/requests/packages/urllib3``
(what requests imports). They are separate files with separate bugs, and a
migration has to fix both. Every case in this module is registered twice, once
per root: the ``urllib3.*`` ids drive ``lib/urllib3`` and the ``urllib3v.*``
ids drive requests' copy. Forgetting the vendored tree is the single most
likely way to half-finish this migration, and this is what makes that show up
in a number.

``urllib3_api.py`` drives the parts the Elasticsearch transport reaches
through its own public surface. This module covers what that leaves: the
certificate-verification outcomes, the TLS knobs in ``util/ssl_.py``, the
response file protocol and decoders, the two header containers, the pool
manager and connection pool, and the url/timeout/retry/connection helpers. Two
pieces are load-bearing for the ladder rather than merely for coverage:

* ``util/ssl_.py`` binds ``SSLContext``/``HAS_SNI`` at import time behind a
  ``try: from ssl import ... except ImportError: pass`` that leaves a
  hand-rolled shim in place if the import ever fails. Step 03 is where the
  stdlib moves under it, so *which* names it ended up with is recorded, not
  just what they do.
* ``response.py`` decides ``closed``/``readable``/``tell`` through ``io`` ABCs
  and hand-written ``is_fp_closed`` probing, which is where the 2/3
  file-object split lives.

Nothing here imports ``packages.ssl_match_hostname``, and that is deliberate:
step 03 deletes it, so a case naming the module would be unpassable from that
rung on (trap 9). What the hostname check *does* is pinned instead, through a
real handshake -- see the certificate-verification comment below.
"""
import importlib
import io
import os

from .. import certs, server
from ..registry import case
from ..server import Route, json_route
from . import doc_escapes

DOC = {"took": 3, "hits": {"total": 2}}

#: (import root, case-id prefix). Both trees, same case bodies.
ROOTS = (("urllib3", "urllib3."),
         ("requests.packages.urllib3", "urllib3v."))

_BODIES = []


def _reg(name, tags=(), fixture=None):
    """Collect a case body; it is registered once per root at import time."""
    def deco(fn):
        _BODIES.append((name, fn, tuple(tags), fixture))
        return fn
    return deco


def _mod(root, name):
    return importlib.import_module("%s.%s" % (root, name))


def _try(fn):
    try:
        return {"ok": fn()}
    except Exception as exc:
        return {"raised": type(exc).__name__, "str": str(exc)}


def ok():
    return {"*": json_route(DOC)}


# --- certificate verification --------------------------------------------
#
# Hostname matching is scored through a real handshake rather than by calling
# the matcher, and that is a deliberate narrowing. This vintage answers the
# question in three different places depending on the interpreter --
# ``ssl.match_hostname`` up to 3.11, the vendored ``_implementation`` from 3.12
# (when the stdlib function is removed), and ``SSLContext.check_hostname`` once
# the tree stops hand-rolling it at all -- and the three *disagree* on a
# DNS-only SAN for an IP host, on a commonName-only certificate and on partial
# wildcards. A table driven straight at the vendored module would pin answers
# that a correct migration is entitled to change, and would import by name a
# module the ladder requires deleted: unpassable from that rung on, through no
# fault of the agent (trap 9).
#
# What every implementation does agree on, and what a user of this library
# would actually notice, is the outcome: a certificate that names a different
# host is refused, one signed by an unknown CA is refused, the refusal happens
# before any request is written, and turning verification off accepts both
# while saying so. That is what is recorded.

#: (fixture certificate, does it name and chain to what the client asks for).
TLS_CERTS = (
    ("server", "valid"),          # right names, fixture CA
    ("mismatch", "wrong-name"),   # fixture CA, names wrong.example.invalid
    ("untrusted", "unknown-ca"),  # right names, issued by a CA nothing trusts
)


@_reg("tls-verification-outcomes", tags=("adversarial",))
def _(ctx, root):
    pool_mod = _mod(root, "connectionpool")
    errors = _mod(root, "exceptions")
    ca = os.path.join(certs.ensure(), "ca.pem")
    out = {}
    for label, name in TLS_CERTS:
        fx = server.Fixture({"*": json_route(DOC)},
                            tls=server.server_chain(label))
        try:
            for mode, kw in (("verified", {"cert_reqs": "CERT_REQUIRED",
                                           "ca_certs": ca}),
                             ("insecure", {"cert_reqs": "CERT_NONE"})):
                pool = pool_mod.HTTPSConnectionPool(fx.host, fx.port,
                                                    retries=False, **kw)
                key = "%s/%s" % (name, mode)
                try:
                    r = pool.urlopen("GET", "/ok")
                    out[key] = {"status": r.status,
                                "body": r.data.decode("utf-8")}
                except Exception as exc:
                    # The class name and message are both left out on purpose:
                    # the message carries the ephemeral port and OpenSSL's own
                    # wording, and the class changes when the certificate check
                    # moves from the vendored matcher to the ssl module. That
                    # the call failed, that it failed inside this library's own
                    # error hierarchy, and that nothing was written to the
                    # socket are true on every rung.
                    out[key] = {"failed": True,
                                "urllib3_error": isinstance(exc,
                                                            errors.HTTPError)}
                finally:
                    pool.close()
            # Counted after both modes: a rejected handshake must leave the
            # server with nothing, and an accepted one must leave it with the
            # request. This is the half of the observation the client cannot
            # fake.
            out["%s/requests_reaching_server" % name] = len(fx.seen)
        finally:
            fx.close()
    return out


# The three ways a caller overrides the hostname decision, driven end to end.
#
# ``tls-verification-outcomes`` above scores the default: dial 127.0.0.1, get
# what the certificate says. It never passes ``assert_hostname`` or
# ``assert_fingerprint`` to a pool, so before these two cases the only thing in
# the suite that touched either was ``assert-fingerprint``, which calls the
# comparison function directly with four bytes that never went near a socket.
# Everything between that function and the handshake -- which name is verified,
# whether the check runs at all, and which of the two knobs wins when both
# could apply -- was unscored, and ``instruction.md`` asserts all of it under
# "stay exactly as they are".
#
# That gap is not academic. It is the single place a plausible step-03 port
# goes wrong: moving the check into the handshake means the *name* has to move
# with it (``server_hostname``), and ``assert_hostname=False`` has to become
# ``check_hostname = False`` without also becoming ``verify_mode = CERT_NONE``.
# A port that hands ``SSLContext`` the dialled host unconditionally passes
# every other TLS case here and breaks both overrides; one that turns the whole
# context off when the hostname check is waived passes those two and silently
# stops verifying the chain.


@_reg("assert-hostname-overrides", tags=("adversarial",))
def _(ctx, root):
    pool_mod = _mod(root, "connectionpool")
    errors = _mod(root, "exceptions")
    ca = os.path.join(certs.ensure(), "ca.pem")
    out = {}

    # Issued by the fixture CA, names wrong.example.invalid and 10.0.0.1, and
    # served from 127.0.0.1: chain good, name wrong, which is the only shape
    # that separates the two halves of the decision. Its DNS name is used
    # rather than its IP SAN because the three matchers this ladder passes
    # through agree about DNS names and disagree about IP literals
    # (harness/certs.py).
    fx = server.Fixture({"*": json_route(DOC)},
                        tls=server.server_chain("mismatch"))
    try:
        for label, kw in (
                # The control. Without an override this is the "wrong-name"
                # row of tls-verification-outcomes, and it must stay refused:
                # if this one starts passing, the two below prove nothing.
                ("default", {}),
                # Verify the name the caller names, not the one dialled.
                ("assert-hostname-other", {"assert_hostname":
                                           "wrong.example.invalid"}),
                # The documented off switch for the *name* check only. The
                # chain is still verified -- see untrusted-ca-still-refused.
                ("assert-hostname-false", {"assert_hostname": False}),
                # A name that matches neither the certificate nor the socket.
                # Refused, and for the name rather than for the chain.
                ("assert-hostname-nonsense", {"assert_hostname":
                                              "not.in.the.cert"}),
        ):
            pool = pool_mod.HTTPSConnectionPool(
                fx.host, fx.port, retries=False, cert_reqs="CERT_REQUIRED",
                ca_certs=ca, **kw)
            try:
                r = pool.urlopen("GET", "/ok")
                out[label] = {"status": r.status,
                              "body": r.data.decode("utf-8")}
            except Exception as exc:
                # Same narrowing as tls-verification-outcomes, for the same
                # reason: the class moves when the check moves into the
                # handshake, the message carries OpenSSL's wording.
                out[label] = {"failed": True,
                              "urllib3_error": isinstance(exc,
                                                          errors.HTTPError)}
            finally:
                pool.close()
        # Two accepted connections out of four, counted at the server: the
        # observation a client-side port cannot fake by mislabelling its own
        # exceptions.
        out["requests_reaching_server"] = len(fx.seen)
    finally:
        fx.close()

    # Waiving the hostname check must not waive the chain. Right names, issuer
    # nothing trusts: refused with assert_hostname=False, and refused with a
    # hostname named explicitly.
    fx = server.Fixture({"*": json_route(DOC)},
                        tls=server.server_chain("untrusted"))
    try:
        for label, kw in (("untrusted-ca-still-refused",
                           {"assert_hostname": False}),
                          ("untrusted-ca-named-host",
                           {"assert_hostname": "127.0.0.1"})):
            pool = pool_mod.HTTPSConnectionPool(
                fx.host, fx.port, retries=False, cert_reqs="CERT_REQUIRED",
                ca_certs=ca, **kw)
            try:
                r = pool.urlopen("GET", "/ok")
                out[label] = {"status": r.status,
                              "body": r.data.decode("utf-8")}
            except Exception as exc:
                out[label] = {"failed": True,
                              "urllib3_error": isinstance(exc,
                                                          errors.HTTPError)}
            finally:
                pool.close()
        out["untrusted/requests_reaching_server"] = len(fx.seen)
    finally:
        fx.close()
    return out


@_reg("assert-fingerprint-pins-connection", tags=("adversarial",))
def _(ctx, root):
    # The other half of the same sentence in instruction.md: "a fingerprint pin
    # is what decides the connection when one is given". The certificate here
    # is issued by a CA the client does not have and names the right host, so
    # chain verification would refuse it and a fingerprint pin has to accept it
    # anyway -- which is what "decides" means. The pin is computed from the
    # fixture's own leaf rather than written down, because certs.py mints fresh
    # material on every run (INSTRUCTIONS trap 10).
    import hashlib
    import ssl as stdlib_ssl
    pool_mod = _mod(root, "connectionpool")
    errors = _mod(root, "exceptions")
    with open(os.path.join(certs.ensure(), "untrusted.pem")) as fh:
        der = stdlib_ssl.PEM_cert_to_DER_cert(fh.read())
    good = hashlib.sha256(der).hexdigest()
    out = {}
    fx = server.Fixture({"*": json_route(DOC)},
                        tls=server.server_chain("untrusted"))
    try:
        for label, kw in (
                # The pin matches. Accepted although nothing chains.
                ("pinned", {"assert_fingerprint": good}),
                # Same connection, wrong pin: refused although the peer is
                # exactly who it was a moment ago.
                ("pinned-wrong", {"assert_fingerprint": "00" * 32}),
                # The pin outranks the hostname: a name that cannot match is
                # given alongside a pin that does.
                ("pin-outranks-hostname",
                 {"assert_fingerprint": good,
                  "assert_hostname": "not.in.the.cert"}),
        ):
            pool = pool_mod.HTTPSConnectionPool(fx.host, fx.port,
                                                retries=False, **kw)
            try:
                r = pool.urlopen("GET", "/ok")
                out[label] = {"status": r.status,
                              "body": r.data.decode("utf-8")}
            except Exception as exc:
                out[label] = {"failed": True,
                              "urllib3_error": isinstance(exc,
                                                          errors.HTTPError)}
            finally:
                pool.close()
        out["requests_reaching_server"] = len(fx.seen)
    finally:
        fx.close()
    return out


# --- TLS knobs ------------------------------------------------------------
#
# ``util/ssl_.py`` is the file the step-03 requirements bear on hardest, and it
# is the one place where the two vendored copies most obviously have to move
# together: they are byte-identical today and each is imported by a different
# half of the tree. These four run against both roots for that reason.

@_reg("resolve-cert-reqs-and-version")
def _(ctx, root):
    import ssl
    s = _mod(root, "util.ssl_")
    return {
        "cert_reqs": [[c, int(s.resolve_cert_reqs(c))] for c in
                      (None, "CERT_NONE", "CERT_OPTIONAL", "CERT_REQUIRED",
                       "NONE", "REQUIRED", ssl.CERT_REQUIRED)],
        "ssl_version": [[v, int(s.resolve_ssl_version(v))] for v in
                        (None, "PROTOCOL_TLSv1", "TLSv1",
                         ssl.PROTOCOL_TLSv1)],
    }


@_reg("create-urllib3-context")
def _(ctx, root):
    # The context urllib3 builds for every https pool. Its options mask and
    # verify flag are the security posture the migration must keep.
    import ssl
    c = _mod(root, "util.ssl_").create_urllib3_context()
    return {"verify_mode": int(c.verify_mode),
            "no_sslv2": bool(c.options & ssl.OP_NO_SSLv2),
            "no_sslv3": bool(c.options & ssl.OP_NO_SSLv3),
            "no_compression": bool(c.options & ssl.OP_NO_COMPRESSION),
            "type": type(c).__name__}


@_reg("assert-fingerprint", tags=("adversarial",))
def _(ctx, root):
    import hashlib
    assert_fingerprint = _mod(root, "util.ssl_").assert_fingerprint
    cert = b"\x00\x01\x02\x03"
    good = hashlib.sha256(cert).hexdigest()
    out = []
    for fp in (good, ":".join(good[i:i + 2] for i in range(0, len(good), 2)),
               good.upper(), "00" * 32, "short", hashlib.md5(cert).hexdigest()):
        try:
            assert_fingerprint(cert, fp)
            out.append([len(fp), "ok"])
        except Exception as e:
            out.append([len(fp), "%s: %s" % (type(e).__name__, e)])
    return out


@_reg("ssl-module-shims-present", tags=("adversarial",))
def _(ctx, root):
    # Which names util.ssl_ actually bound at import time. On this interpreter
    # the real ssl.SSLContext wins and HAS_SNI is true; the migration must not
    # silently fall back to the hand-rolled shim, which warns and refuses SNI.
    s = _mod(root, "util.ssl_")
    return {"has_sni": bool(s.HAS_SNI),
            "sslcontext_is_stdlib": s.SSLContext.__module__ == "ssl",
            "has_create_context": hasattr(s, "create_urllib3_context"),
            # The whole string, not its first token. The instruction says "the
            # same DEFAULT_CIPHERS string", and a head-only observation lets
            # every cipher after the first move without the case noticing --
            # including the two the migration is most likely to reach for,
            # `ECDH+3DES` and `RSA+3DES`. It is a literal in this tree's own
            # source, identical in both copies and independent of the
            # interpreter, so there is nothing to make it flaky. Round-1
            # static review of rung 03.
            "default_ciphers": s.DEFAULT_CIPHERS}


@_reg("ssl-wrap-socket-bad-ca-file", tags=("adversarial",))
def _(ctx, root):
    # ``ssl_wrap_socket`` translates a ca_certs path it cannot load into this
    # library's own SSLError. That translation is two lines inside a `try`, and
    # before this case nothing in the suite reached either of them (section
    # 9.2): every other TLS case hands it a ca file that exists. A port that
    # moves the `load_verify_locations` call outside the `try`, or that lets
    # the modern `SSLContext` build the context and forgets the wrapper, leaves
    # a raw OSError coming out of a library whose documented failure type is
    # SSLError -- correct-looking on every happy path, and invisible to every
    # other case here.
    import socket
    import ssl as stdlib_ssl
    s = _mod(root, "util.ssl_")
    errors = _mod(root, "exceptions")
    d = certs.ensure()
    out = {}
    # The numeric constant and not the "CERT_REQUIRED" spelling: the pool
    # resolves the string through `resolve_cert_reqs` before it gets here, and
    # this case is about what happens to the ca_certs path, not about that
    # resolution (`resolve-cert-reqs-and-version` owns it).
    for label, ca in (("missing-file", os.path.join(d, "no-such-ca.pem")),
                      ("directory", d)):
        sock = socket.socket()
        try:
            s.ssl_wrap_socket(sock, ca_certs=ca,
                              cert_reqs=stdlib_ssl.CERT_REQUIRED)
            out[label] = {"wrapped": True}
        except Exception as exc:
            out[label] = {"raised": type(exc).__name__,
                          "urllib3_ssl_error": isinstance(exc, errors.SSLError),
                          "urllib3_error": isinstance(exc, errors.HTTPError)}
        finally:
            sock.close()
    return out


@_reg("https-client-certificate", tags=("adversarial",))
def _(ctx, root):
    # ``ssl_wrap_socket``'s certfile/keyfile branch, driven through the pool
    # rather than called directly: `VerifiedHTTPSConnection.connect` is what
    # passes `cert_file`/`key_file` down, so this is the library's own surface.
    # The fixture asks for a client certificate and accepts the connection
    # either way (`CERT_OPTIONAL`, see server.Fixture), so what separates the
    # two halves is what the *server* saw -- a client that silently stops
    # sending its certificate still gets its 200 back, and only `peer_cns`
    # notices.
    pool_mod = _mod(root, "connectionpool")
    errors = _mod(root, "exceptions")
    d = certs.ensure()
    ca = os.path.join(d, "ca.pem")
    out = {}
    fx = server.Fixture({"*": json_route(DOC)}, tls=server.server_chain(),
                        client_ca=ca)
    try:
        for label, extra in (("with-cert",
                              {"cert_file": os.path.join(d, "client.pem"),
                               "key_file": os.path.join(d, "client.key")}),
                             ("without-cert", {})):
            pool = pool_mod.HTTPSConnectionPool(
                fx.host, fx.port, retries=False, cert_reqs="CERT_REQUIRED",
                ca_certs=ca, **extra)
            try:
                r = pool.urlopen("GET", "/ok")
                out[label] = {"status": r.status,
                              "body": r.data.decode("utf-8")}
            except Exception as exc:
                out[label] = {"failed": True,
                              "urllib3_error": isinstance(exc,
                                                          errors.HTTPError)}
            finally:
                pool.close()
        out["peer_cns"] = list(fx.peer_cns)
        out["requests_reaching_server"] = len(fx.seen)
    finally:
        fx.close()
    return out


# --- response -------------------------------------------------------------

@_reg("response-file-protocol")
def _(ctx, root):
    resp = _mod(root, "response")
    HTTPHeaderDict = _mod(root, "_collections").HTTPHeaderDict
    r = resp.HTTPResponse(body=io.BytesIO(b"abcdefghij"), status=200,
                          preload_content=False,
                          headers=HTTPHeaderDict({"content-type":
                                                  "text/plain"}))
    out = {"readable": r.readable(), "writable": r.writable(),
           "seekable": r.seekable(), "closed_before": r.closed,
           "tell_before": r.tell(),
           "first_read": r.read(4).decode("ascii"),
           "tell_after": r.tell(),
           "isatty": _try(lambda: r.isatty()),
           "flush": _try(lambda: r.flush()),
           "seek": _try(lambda: r.seek(0)),
           "truncate": _try(lambda: r.truncate()),
           "writelines": _try(lambda: r.writelines([b"x"])),
           "fileno": _try(lambda: r.fileno())}
    buf = bytearray(3)
    out["readinto"] = (r.readinto(buf), bytes(buf).decode("ascii"))
    out["rest"] = r.read().decode("ascii")
    out["tell_end"] = r.tell()
    r.close()
    out["closed_after"] = r.closed
    out["read_after_close"] = _try(lambda: r.read())
    # is_fp_closed probes isclosed(), then closed, then fp, and gives up with
    # a ValueError. None hits the give-up branch.
    out["is_fp_closed_none"] = _try(lambda: resp.is_fp_closed(None))
    out["is_fp_closed_bytesio"] = resp.is_fp_closed(io.BytesIO(b""))
    closed_io = io.BytesIO(b"")
    closed_io.close()
    out["is_fp_closed_closed"] = resp.is_fp_closed(closed_io)
    return out


@_reg("response-readline-and-iteration")
def _(ctx, root):
    HTTPResponse = _mod(root, "response").HTTPResponse
    body = b"line one\nline two\nline three\n"
    r = HTTPResponse(body=io.BytesIO(body), status=200, preload_content=False)
    out = {"readline": r.readline().decode("ascii"),
           "readlines": [x.decode("ascii") for x in r.readlines()]}
    r2 = HTTPResponse(body=io.BytesIO(body), status=200,
                      preload_content=False)
    out["iter"] = [x.decode("ascii") for x in r2]
    r3 = HTTPResponse(body=io.BytesIO(body), status=200,
                      preload_content=False)
    out["stream_amt"] = [x.decode("ascii") for x in r3.stream(8)]
    r4 = HTTPResponse(body=io.BytesIO(body), status=200, preload_content=True)
    out["preloaded_data"] = r4.data.decode("ascii")
    out["data_is_cached"] = r4.data is r4.data
    out["bytes_body"] = HTTPResponse(
        body=b"raw bytes", status=200).data.decode("ascii")
    out["empty_body"] = HTTPResponse(body=b"", status=204).data
    out["none_body"] = HTTPResponse(status=204).data
    return out


@_reg("response-decoders", tags=("adversarial",))
def _(ctx, root):
    # GzipDecoder and DeflateDecoder are hand-written state machines around
    # zlib, and DeflateDecoder in particular retries with a raw-deflate window
    # when the first feed fails. Both need to be driven with truncated and
    # corrupt input, not just clean input.
    import zlib

    resp = _mod(root, "response")
    HTTPHeaderDict = _mod(root, "_collections").HTTPHeaderDict

    raw = b"the quick brown fox" * 8
    co = zlib.compressobj(9, zlib.DEFLATED, 16 + zlib.MAX_WBITS)
    gz = co.compress(raw) + co.flush()
    zl = zlib.compress(raw)
    rawdeflate = zlib.compressobj(9, zlib.DEFLATED, -zlib.MAX_WBITS)
    rd = rawdeflate.compress(raw) + rawdeflate.flush()

    def through(body, encoding, decode=True):
        r = resp.HTTPResponse(body=io.BytesIO(body), status=200,
                              preload_content=False,
                              headers=HTTPHeaderDict({"content-encoding":
                                                      encoding}))
        return r.read(decode_content=decode)

    out = {"gzip": _try(lambda: len(through(gz, "gzip"))),
           "zlib-deflate": _try(lambda: len(through(zl, "deflate"))),
           "raw-deflate": _try(lambda: len(through(rd, "deflate"))),
           "gzip-undecoded": _try(lambda: len(through(gz, "gzip", False))),
           "gzip-truncated": _try(lambda: len(through(gz[:20], "gzip"))),
           "gzip-corrupt": _try(lambda: len(through(b"not gzip at all",
                                                    "gzip"))),
           "deflate-corrupt": _try(lambda: len(through(b"\x00\x01\x02",
                                                       "deflate"))),
           "unknown-encoding": _try(lambda: len(through(raw, "br"))),
           "identity": _try(lambda: len(through(raw, "identity"))),
           "uppercase-encoding": _try(lambda: len(through(gz, "GZIP"))),
           "content_decoders": resp.HTTPResponse.CONTENT_DECODERS,
           "redirect_statuses": resp.HTTPResponse.REDIRECT_STATUSES}
    d = resp.GzipDecoder()
    out["decoder_incremental_gzip"] = len(
        d.decompress(gz[:30]) + d.decompress(gz[30:]))
    f = resp.DeflateDecoder()
    out["decoder_incremental_deflate"] = len(
        f.decompress(zl[:20]) + f.decompress(zl[20:]))
    out["decoder_getattr_passthrough"] = hasattr(resp.GzipDecoder(), "flush")
    out["deflate_empty_first_feed"] = _try(
        lambda: resp.DeflateDecoder().decompress(b""))
    return out


@_reg("response-headers-and-redirect")
def _(ctx, root):
    HTTPResponse = _mod(root, "response").HTTPResponse
    HTTPHeaderDict = _mod(root, "_collections").HTTPHeaderDict
    hdrs = HTTPHeaderDict()
    hdrs.add("Set-Cookie", "a=1")
    hdrs.add("Set-Cookie", "b=2")
    hdrs.add("Location", "/next")
    hdrs.add("Content-Type", "application/json")
    out = {}
    for status in (200, 301, 302, 303, 307, 308, 404):
        r = HTTPResponse(body=b"", status=status, headers=hdrs,
                         reason="Because")
        out[str(status)] = {"redirect": r.get_redirect_location(),
                            "reason": r.reason}
    r = HTTPResponse(body=b"", status=302, headers=hdrs)
    out["getheaders"] = sorted(r.getheaders().items())
    out["getheader"] = r.getheader("set-cookie")
    out["getheader_default"] = r.getheader("x-absent", "dflt")
    out["headers_are_dict"] = isinstance(r.headers, HTTPHeaderDict)
    out["no_location"] = HTTPResponse(
        body=b"", status=302, headers=HTTPHeaderDict()).get_redirect_location()
    out["plain_dict_headers"] = sorted(HTTPResponse(
        body=b"", status=200, headers={"a": "1"}).headers.items())
    return out


def _chunked_fixture():
    return {"/chunked": Route(status=200, body=b'{"chunked":true}' * 4,
                              chunked=True,
                              headers=[("content-type", "application/json")]),
            "/plain": json_route(DOC)}


@_reg("response-chunked-reads", fixture=_chunked_fixture)
def _(ctx, root):
    # read_chunked only works on a response that came off a real httplib
    # connection: it reaches through _original_response.fp and parses the
    # chunk framing itself. A non-chunked response must refuse rather than
    # return nothing.
    pool = _mod(root, "connectionpool").HTTPConnectionPool(ctx.fx.host,
                                                           ctx.fx.port)
    r = pool.urlopen("GET", "/chunked", preload_content=False)
    out = {"chunked_flag": r.chunked,
           "chunks": [len(c) for c in r.read_chunked()],
           "headers": sorted(r.headers.items()),
           "tell_after": r.tell()}
    r2 = pool.urlopen("GET", "/chunked", preload_content=False)
    out["chunked_amt"] = [len(c) for c in r2.read_chunked(4)]
    r3 = pool.urlopen("GET", "/plain", preload_content=False)
    out["plain_chunked_flag"] = r3.chunked
    out["plain_read_chunked"] = _try(lambda: list(r3.read_chunked()))
    r4 = pool.urlopen("GET", "/chunked")
    out["preloaded_len"] = len(r4.data)
    pool.close()
    return out


# --- _collections ---------------------------------------------------------

@_reg("header-dict-multivalue")
def _(ctx, root):
    HTTPHeaderDict = _mod(root, "_collections").HTTPHeaderDict
    d = HTTPHeaderDict()
    d.add("Set-Cookie", "a=1")
    d.add("set-cookie", "b=2")
    d["Content-Type"] = "application/json"
    out = {"getlist": d.getlist("SET-COOKIE"),
           "getlist_missing": d.getlist("nope"),
           "getitem_joined": d["set-cookie"],
           "getheaders_alias": d.getheaders("set-cookie"),
           "iget_alias": d.iget("set-cookie"),
           "getallmatchingheaders": d.getallmatchingheaders("set-cookie"),
           "items": sorted(d.items()),
           "itermerged": sorted(d.itermerged()),
           # ``iter(d)``, not ``d.iteritems()``: the py2 spellings this class
           # defines are named for removal on the first rung, and an inherited
           # case that calls one would be unpassable from there on (trap 9).
           # What must survive is that the container is iterable and that
           # iterating it yields its keys, which is what is recorded.
           "iter_protocol": sorted(iter(d)),
           "keys": sorted(d.keys()),
           "len": len(d),
           "contains_ci": ["SET-COOKIE" in d, "nope" in d],
           "repr_type": type(d).__name__}
    d2 = d.copy()
    d2.add("Set-Cookie", "c=3")
    out["copy_independent"] = [d.getlist("set-cookie"),
                               d2.getlist("set-cookie")]
    out["eq_same"] = d == d.copy()
    out["eq_other"] = d == d2
    out["eq_plain_dict"] = d == {"set-cookie": "a=1, b=2",
                                 "content-type": "application/json"}
    out["ne"] = d != d2
    d2.discard("set-cookie")
    out["after_discard"] = sorted(d2.items())
    out["discard_missing"] = _try(lambda: d2.discard("not-there"))
    d3 = HTTPHeaderDict()
    d3.extend(d)
    out["extend_from_headerdict"] = sorted(d3.items())
    d4 = HTTPHeaderDict()
    d4.extend([("a", "1"), ("a", "2")])
    d4.extend({"b": "3"})
    d4.extend(c="4")
    out["extend_forms"] = sorted(d4.items())
    out["extend_two_args"] = _try(lambda: d4.extend({}, {}))
    out["init_kwargs"] = sorted(HTTPHeaderDict(a="1", b="2").items())
    out["init_mapping"] = sorted(HTTPHeaderDict({"a": "1"}, b="2").items())
    out["pop"] = d4.pop("a")
    out["setdefault"] = d4.setdefault("z", "26")
    out["non_string_value"] = _try(lambda: HTTPHeaderDict(n=1)["n"])
    return out


@_reg("header-dict-from-httplib", tags=("adversarial",))
def _(ctx, root):
    # from_httplib is the 2/3 seam in this file: it re-reads the raw header
    # lines off a Python 2 httplib message and folds continuations by hand.
    HTTPHeaderDict = _mod(root, "_collections").HTTPHeaderDict

    class Py2Message(object):
        """httplib.HTTPMessage on Python 2: .headers is the list of raw
        header lines, continuation lines included."""

        def __init__(self, lines):
            self.headers = list(lines)

    lines = ["Set-Cookie: a=1\r\n", "Content-Type: text/plain\r\n",
             "Set-Cookie: b=2\r\n", "set-cookie: c=3\r\n",
             "X-Folded: first\r\n", "\tsecond\r\n"]
    d = HTTPHeaderDict.from_httplib(Py2Message(lines))

    class Py3Message(object):
        """email.message.Message on Python 3 has no .headers at all."""

        def items(self):
            return [("Set-Cookie", "a=1")]

        def get_all(self, name, default=None):
            return ["a=1"]

    return {"items": sorted(d.items()),
            "set_cookie_list": d.getlist("set-cookie"),
            "joined": d["set-cookie"],
            "folded": d.getlist("x-folded"),
            "len": len(d),
            "empty": sorted(HTTPHeaderDict.from_httplib(
                Py2Message([])).items()),
            # The py3 message object is the whole point: this classmethod is
            # Python-2-only and blows up on the object urllib3 actually gets
            # from http.client. Nothing in the tree calls it, which is why the
            # defect is invisible until something does.
            "py3_message": _try(
                lambda: sorted(HTTPHeaderDict.from_httplib(
                    Py3Message()).items())),
            "malformed_line": _try(
                lambda: sorted(HTTPHeaderDict.from_httplib(
                    Py2Message(["no-colon-here\r\n"])).items()))}


@_reg("recently-used-container-eviction")
def _(ctx, root):
    RecentlyUsedContainer = _mod(root, "_collections").RecentlyUsedContainer
    disposed = []
    c = RecentlyUsedContainer(3, dispose_func=disposed.append)
    for i in range(3):
        c[i] = "v%d" % i
    out = {"keys_before": sorted(c.keys()), "len": len(c)}
    c[0]                         # touch 0 so 1 becomes the oldest
    c[3] = "v3"
    out["evicted"] = list(disposed)
    out["keys_after"] = sorted(c.keys())
    out["contains"] = [0 in c, 1 in c, 3 in c]
    out["get_missing"] = _try(lambda: c[1])
    del c[0]
    out["after_delete"] = (sorted(c.keys()), list(disposed))
    out["delete_missing"] = _try(lambda: c.__delitem__(99))
    c.clear()
    out["after_clear"] = (sorted(c.keys()), len(c), list(disposed))
    plain = RecentlyUsedContainer(2)
    plain["a"] = 1
    plain["a"] = 2
    out["overwrite_disposes"] = (plain["a"], len(plain))
    out["iteration_refused"] = _try(lambda: list(iter(plain)))
    return out


# --- pools ----------------------------------------------------------------

@_reg("poolmanager-pool-lifecycle", fixture=ok)
def _(ctx, root):
    pmmod = _mod(root, "poolmanager")
    pm = pmmod.PoolManager(num_pools=2)
    a = pm.connection_from_url("http://%s:%d/" % (ctx.fx.host, ctx.fx.port))
    b = pm.connection_from_url("http://%s:%d/other" % (ctx.fx.host,
                                                       ctx.fx.port))
    out = {"same_pool": a is b,
           "num_pools": len(pm.pools.keys()),
           "by_host": type(pm.connection_from_host(
               "example.com", 80, "http")).__name__,
           "https_pool": type(pm.connection_from_host(
               "example.com", 443, "https")).__name__,
           "default_port": pm.connection_from_url("http://example.com/").port,
           "port_by_scheme": dict(pmmod.port_by_scheme),
           "pool_classes": sorted(pmmod.pool_classes_by_scheme.keys()),
           "ssl_keywords": sorted(pmmod.SSL_KEYWORDS),
           "no_host": _try(lambda: pm.connection_from_url("/relative")),
           "unknown_scheme": _try(lambda: type(pm.connection_from_host(
               "example.com", None, "ftp")).__name__)}
    pm.connection_from_host("a.example.com", 80, "http")
    pm.connection_from_host("b.example.com", 80, "http")
    out["evicted_to_num_pools"] = len(pm.pools.keys())
    out["request"] = pm.urlopen("GET", ctx.url("/x")).status
    with pmmod.PoolManager(num_pools=1) as ctxmgr:
        out["context_manager"] = ctxmgr.urlopen("GET", ctx.url("/y")).status
    pm.clear()
    out["after_clear"] = len(pm.pools.keys())
    return out


@_reg("proxy-manager-shape", tags=("adversarial",))
def _(ctx, root):
    # No proxy is reachable from the runner (the environment strips every
    # proxy variable), so this pins the URL rewriting and header construction
    # ProxyManager does *before* it opens a socket.
    pmmod = _mod(root, "poolmanager")
    pm = pmmod.proxy_from_url("http://user:pass@proxy.example.com:8080/")
    out = {"proxy_host": pm.proxy.host, "proxy_port": pm.proxy.port,
           "proxy_scheme": pm.proxy.scheme,
           "headers": sorted(pm.proxy_headers.items()),
           "connection_from_http": [
               pm.connection_from_host("example.com", 80, "http").host,
               pm.connection_from_host("example.com", 80, "http").port],
           "connection_from_https": [
               pm.connection_from_host("example.com", 443, "https").host,
               pm.connection_from_host("example.com", 443, "https").port],
           "no_scheme": _try(
               lambda: pmmod.ProxyManager("proxy.example.com:8080")),
           "from_manager": _try(
               lambda: type(pmmod.ProxyManager(pm)).__name__)}
    out["set_proxy_headers"] = sorted(
        pm._set_proxy_headers("http://example.com/",
                              {"Accept": "*/*"}).items())
    return out


def _pool_fixture():
    return {"/ok": json_route(DOC),
            "/slow": Route(status=200, body=b"{}", delay=0.4,
                           headers=[("content-type", "application/json")]),
            "*": json_route(DOC)}


@_reg("connectionpool-lifecycle", fixture=_pool_fixture)
def _(ctx, root):
    cp = _mod(root, "connectionpool")
    ex = _mod(root, "exceptions")
    pool = cp.HTTPConnectionPool(ctx.fx.host, ctx.fx.port, maxsize=1,
                                 block=False)
    # The fixture binds port 0, so the number differs between the two recording
    # passes; what is worth pinning is that the pool parsed the one it was
    # given, not which one that was. Normalisation only rewrites ports inside
    # strings -- a bare int would be indistinguishable from a content-length.
    out = {"repr_scheme": pool.scheme,
           "host": pool.host, "port_is_fixture": pool.port == ctx.fx.port,
           "first": pool.urlopen("GET", "/ok").status,
           "num_connections": pool.num_connections,
           "num_requests": pool.num_requests}
    # A pool with block=True and maxsize=1 must refuse a second simultaneous
    # checkout rather than quietly growing. The timeout has to be passed to
    # _get_conn, not to the pool: this vintage reads self.block but takes the
    # queue timeout from its argument, so an omitted one blocks forever.
    blocking = cp.HTTPConnectionPool(ctx.fx.host, ctx.fx.port, maxsize=1,
                                     block=True, timeout=0.2)
    held = blocking._get_conn(timeout=0.2)
    out["empty_pool"] = _try(lambda: blocking._get_conn(timeout=0.2))
    blocking._put_conn(held)
    out["after_return"] = blocking._get_conn(timeout=0.2) is held
    blocking.close()
    out["closed_pool"] = _try(lambda: blocking.urlopen("GET", "/ok"))
    out["host_changed"] = _try(
        lambda: pool.urlopen("GET", "http://other.example.com/ok"))
    out["is_same_host"] = [pool.is_same_host("/ok"),
                           pool.is_same_host("http://%s:%d/ok"
                                             % (ctx.fx.host, ctx.fx.port)),
                           pool.is_same_host("http://elsewhere/ok")]
    out["assert_same_host_off"] = pool.urlopen(
        "GET", "http://other.example.com/ok", assert_same_host=False).status
    out["exceptions"] = {"EmptyPoolError": ex.EmptyPoolError.__name__,
                         "ClosedPoolError": ex.ClosedPoolError.__name__,
                         "HostChangedError": ex.HostChangedError.__name__}
    out["connection_from_url"] = cp.connection_from_url(
        "http://%s:%d/x" % (ctx.fx.host, ctx.fx.port)).port == ctx.fx.port
    pool.close()
    return out


@_reg("request-methods-encoding", fixture=ok)
def _(ctx, root):
    # RequestMethods decides between a query string and an encoded body from
    # the method name alone; the set it treats as body-carrying is part of the
    # public contract.
    req = _mod(root, "request")
    pool = _mod(root, "connectionpool").HTTPConnectionPool(ctx.fx.host,
                                                           ctx.fx.port)
    out = {"encode_url_methods": sorted(req.RequestMethods._encode_url_methods),
           "urlopen_not_implemented": _try(
               lambda: req.RequestMethods().urlopen("GET", "/x"))}
    for method in ("GET", "HEAD", "DELETE", "POST", "PUT"):
        pool.request(method, "/x", fields={"a": "1", "b": u"é"})
    out["wire"] = [(w["method"], w["path"], w["body"][:80])
                   for w in ctx.fx.wire()]
    ctx.fx.seen[:] = []
    pool.request_encode_body("POST", "/multi",
                             fields={"f": ("n.txt", b"data", "text/plain")},
                             multipart_boundary="FIXEDBOUNDARY")
    pool.request_encode_body("POST", "/urlencoded", fields={"a": "1"},
                             encode_multipart=False)
    pool.request("POST", "/explicitbody", body=b"raw",
                 headers={"content-type": "application/json"})
    out["body_wire"] = ctx.fx.wire()
    pool.close()
    return out


# --- util -----------------------------------------------------------------

@_reg("url-namedtuple")
def _(ctx, root):
    url = _mod(root, "util.url")
    out = {}
    for args in (("http", None, "example.com", 80, "/a", "b=1", "frag"),
                 ("https", "user:pass", "example.com", None, None, None,
                  None),
                 (None, None, "example.com", None, "/", None, None),
                 (None, None, None, None, "/only/path", None, None)):
        u = url.Url(*args)
        out[repr(args)] = {"str": str(u), "url": u.url, "netloc": u.netloc,
                           "hostname": u.hostname, "host": u.host,
                           "request_uri": u.request_uri,
                           "fields": list(u._fields)}
    out["split_first"] = [list(url.split_first(s, d)) for s, d in
                          (("foo/bar?baz", "?/="), ("foo/bar?baz", "123"),
                           ("", "?/="), ("a=b", "="))]
    return out


@_reg("request-and-timeout-helpers")
def _(ctx, root):
    make_headers = _mod(root, "util.request").make_headers
    is_fp_closed = _mod(root, "util.response").is_fp_closed
    timeout = _mod(root, "util.timeout")
    out = {"make_headers": [
        sorted(make_headers(keep_alive=True).items()),
        sorted(make_headers(accept_encoding=True).items()),
        sorted(make_headers(accept_encoding="gzip").items()),
        sorted(make_headers(accept_encoding=["gzip", "deflate"]).items()),
        sorted(make_headers(user_agent="es/1.0").items()),
        sorted(make_headers(basic_auth="user:pass").items()),
        sorted(make_headers(proxy_basic_auth="user:pass").items()),
        sorted(make_headers(disable_cache=True).items()),
    ]}
    Timeout = timeout.Timeout
    out["default_timeout_repr"] = repr(Timeout())
    out["from_float"] = repr(Timeout.from_float(2.5))
    out["clone"] = repr(Timeout(connect=1, read=2).clone())
    t = Timeout(connect=1, read=2, total=3)
    out["connect_timeout"] = t.connect_timeout
    t.start_connect()
    out["start_twice"] = _try(lambda: t.start_connect())
    out["read_timeout_is_number"] = isinstance(t.read_timeout, float)
    out["bad_value"] = _try(lambda: Timeout(connect="soon"))
    out["negative"] = _try(lambda: Timeout(connect=-1))
    out["bool_value"] = _try(lambda: repr(Timeout(read=True)))
    out["get_connect_duration_before_start"] = _try(
        lambda: Timeout().get_connect_duration())
    out["current_time_is_number"] = isinstance(timeout.current_time(), float)
    out["is_fp_closed_no_attrs"] = _try(lambda: is_fp_closed(object()))
    return out


@_reg("retry-semantics")
def _(ctx, root):
    ex = _mod(root, "exceptions")
    Retry = _mod(root, "util.retry").Retry
    r = Retry(total=3, connect=2, read=1, redirect=0, backoff_factor=0.1)
    out = {"repr": repr(r),
           "default_methods": sorted(Retry.DEFAULT_METHOD_WHITELIST),
           "backoff_max": Retry.BACKOFF_MAX,
           "is_exhausted": r.is_exhausted(),
           "backoff_first": r.get_backoff_time()}
    # This vintage counts with a private _observed_errors int rather than the
    # history tuple later releases grew; the backoff schedule is computed from
    # it, so it is the field worth pinning.
    steps = []
    cur = r
    for _ in range(3):
        cur = cur.increment(method="GET", url="/x")
        steps.append((cur.total, cur.connect, cur.read, cur.redirect,
                      cur._observed_errors, round(cur.get_backoff_time(), 6)))
    out["increments"] = steps
    out["has_history"] = hasattr(r, "history")
    out["exhausted"] = _try(lambda: cur.increment(method="GET", url="/x"))
    out["from_int"] = repr(Retry.from_int(4))
    out["from_retry"] = repr(Retry.from_int(r))
    out["from_none"] = repr(Retry.from_int(None, default=Retry(2)))
    out["is_forced_retry"] = [
        Retry(status_forcelist=[503]).is_forced_retry("GET", 503),
        Retry(status_forcelist=[503]).is_forced_retry("GET", 500),
        Retry(status_forcelist=[503]).is_forced_retry("POST", 503)]
    out["redirect_false"] = repr(Retry(redirect=False))
    out["read_error_on_post"] = _try(
        lambda: Retry(read=0).increment(
            method="POST", url="/x",
            error=ex.ReadTimeoutError(None, "/x", "boom")))
    out["connect_error"] = repr(Retry(connect=1).increment(
        method="GET", url="/x", error=ex.ConnectTimeoutError("slow")))
    out["maxretry_is_requesterror"] = issubclass(ex.MaxRetryError,
                                                 ex.RequestError)
    out["new_preserves"] = repr(r.new(total=9))
    return out


@_reg("connection-helpers", tags=("adversarial",))
def _(ctx, root):
    import socket

    conn = _mod(root, "util.connection")
    out = {}
    # A socket that is live, one whose peer has gone away, and a connection
    # object with no socket at all: the three states is_connection_dropped has
    # to tell apart with a select() poll.
    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    addr = srv.getsockname()
    live = conn.create_connection(addr, timeout=2)
    peer, _ = srv.accept()
    out["live"] = conn.is_connection_dropped(_Wrap(live))
    peer.close()
    out["peer_closed"] = conn.is_connection_dropped(_Wrap(live))
    live.close()
    out["no_sock"] = conn.is_connection_dropped(_Wrap(None))
    srv.close()
    out["refused"] = _try(
        lambda: conn.create_connection(("127.0.0.1", 1), timeout=1))
    out["bad_host"] = _try(
        lambda: conn.create_connection(("no-such-host.invalid", 80),
                                       timeout=2))
    out["source_address"] = _try(
        lambda: conn.create_connection(("127.0.0.1", 1), timeout=1,
                                       source_address=("127.0.0.1", 0)))
    return out


class _Wrap(object):
    """is_connection_dropped takes a connection object, not a socket."""

    def __init__(self, sock):
        self.sock = sock


@_reg("exceptions-carry-their-context")
def _(ctx, root):
    ex = _mod(root, "exceptions")
    out = {}
    pool = type("P", (object,), {"__str__": lambda s: "pool"})()
    for name, exc in (
            ("MaxRetryError", ex.MaxRetryError(pool, "/u", "reason")),
            ("HostChangedError", ex.HostChangedError(pool, "/u", 3)),
            ("TimeoutStateError", ex.TimeoutStateError("bad state")),
            ("ReadTimeoutError", ex.ReadTimeoutError(pool, "/u", "slow")),
            ("LocationParseError", ex.LocationParseError("http://[")),
            ("ResponseError", ex.ResponseError("too many")),
            ("ProxyError", ex.ProxyError("bad proxy")),
            ("ResponseNotChunked", ex.ResponseNotChunked("not chunked")),
            ("ProtocolError", ex.ProtocolError("aborted")),
            ("DecodeError", ex.DecodeError("bad gzip")),
            ("SSLError", ex.SSLError("handshake")),
            ("EmptyPoolError", ex.EmptyPoolError(pool, "empty")),
            ("ClosedPoolError", ex.ClosedPoolError(pool, "closed")),
            ("ConnectTimeoutError", ex.ConnectTimeoutError("too slow")),
            ("LocationValueError", ex.LocationValueError("no host")),
    ):
        out[name] = {"str": str(exc),
                     "args": [str(a) for a in exc.args],
                     "mro": [c.__name__ for c in type(exc).__mro__
                             if c is not object]}
    out["aliases"] = {"ConnectionError_is_ProtocolError":
                      ex.ConnectionError is ex.ProtocolError,
                      "RequestError_is_base":
                      issubclass(ex.MaxRetryError, ex.RequestError)}
    out["response_error_templates"] = sorted(
        (n, str(getattr(ex.ResponseError, n)))
        for n in dir(ex.ResponseError) if n.isupper())
    out["warning_categories"] = sorted(
        n for n in dir(ex)
        if n.endswith("Warning") and isinstance(getattr(ex, n), type))
    return out



@_reg("warning-hierarchy", tags=("adversarial",))
def _(ctx, root):
    # ``exceptions-carry-their-context`` pins *which* warning classes the
    # module defines; this pins what they are. Step 03 rebuilds the import-time
    # block in ``util/ssl_.py`` that raises three of them, and the instruction
    # states that SecurityWarning keeps its name and its place in the
    # hierarchy -- for both copies, like everything else in that step. Warning
    # category is not decoration: ``warnings.simplefilter("error",
    # SecurityWarning)`` and every ``catch_warnings`` block in a caller's test
    # suite dispatches on exactly these mro edges, so a class reparented onto
    # ``Warning`` (or onto ``DeprecationWarning``, which is filtered out by
    # default) stops reaching the code that was listening for it.
    ex = _mod(root, "exceptions")
    out = {}
    for name in sorted(n for n in dir(ex)
                       if n.endswith("Warning")
                       and isinstance(getattr(ex, n), type)):
        cls = getattr(ex, name)
        out[name] = {
            "mro": [c.__name__ for c in cls.__mro__ if c is not object],
            "is_warning": issubclass(cls, Warning),
            "is_user_warning": issubclass(cls, UserWarning),
            "is_deprecation_warning": issubclass(cls, DeprecationWarning),
            "under_http_warning": issubclass(cls, ex.HTTPWarning)}
    out["HTTPWarning_base"] = [c.__name__ for c in ex.HTTPWarning.__mro__
                               if c is not object]
    return out


@_reg("docstring-escapes-intact", tags=("adversarial",))
def _(ctx, root):
    # Four of the 24 sites per copy. ``connection_from_url`` is a module-level
    # function, the other three are a class and a method, so the lookup goes
    # through three different kinds of attribute.
    cp = _mod(root, "connectionpool")
    pm = _mod(root, "poolmanager")
    pool = cp.HTTPConnectionPool
    return {"HTTPConnectionPool": doc_escapes(pool),
            "HTTPConnectionPool.urlopen": doc_escapes(pool.urlopen),
            "connection_from_url": doc_escapes(cp.connection_from_url),
            "PoolManager": doc_escapes(pm.PoolManager)}


# --- registration ---------------------------------------------------------

def _bind(fn, root):
    def run(ctx):
        return fn(ctx, root)
    run.__module__ = fn.__module__
    run.__name__ = fn.__name__
    return run


for _root, _prefix in ROOTS:
    for _name, _fn, _tags, _fixture in _BODIES:
        case(_prefix + _name, tags=_tags, fixture=_fixture)(_bind(_fn, _root))
