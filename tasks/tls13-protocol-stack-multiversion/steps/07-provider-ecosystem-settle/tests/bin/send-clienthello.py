#!/usr/bin/env python3
"""Send one hand-built TLS 1.3 ClientHello and report what the server said back.

The point of this probe is to ask a question that no TLS client library will
ask on our behalf: what does the server do with a server_name extension it
should refuse to *honour* but must not refuse to *parse*? RFC 6066 says the
"HostName" in server_name is a DNS name and "literal IPv4 and IPv6 addresses
are not permitted". A server that treats that as a decode error breaks real
clients that send one anyway (Safari did); a server that honours it is doing
name-based dispatch on an address. The correct reading is to parse the
extension and ignore that entry.

So we build the ClientHello ourselves, put whatever we like in server_name,
and look only at the *first record* the server emits. That is enough to tell
the three outcomes apart:

    handshake   the server accepted the hello and is answering (ServerHello)
    alert       the server rejected it, and we report the alert description
    none        the server said nothing / closed / never came up

We deliberately do not finish the handshake. The key share is 32 bytes of
fixed non-secret filler, so the connection is doomed after ServerHello -- but
ServerHello is the answer to the question, and stopping there keeps this probe
free of any crypto dependency.

A server that *honours* the name rather than ignoring it emits the same
ServerHello, so the first record alone cannot separate "ignored" from
"honoured". ``--sni-ack`` asks that second half directly. It sends a TLS 1.2
ClientHello instead of a TLS 1.3 one, because RFC 6066 section 3 says a server
that has used the name "SHALL include an extension of type server_name" in its
hello -- and in TLS 1.2 that acknowledgement rides in the ServerHello, in the
clear, where an observer that holds no key can read it. (In TLS 1.3 the same
acknowledgement moves into EncryptedExtensions and is unreadable here, which is
why this one probe drops to the older version.) So: a hostname the server
honours comes back acknowledged, and a name it disregards comes back exactly as
a hello with no server_name extension at all does.

The same machinery answers a third question of the same shape, asked with
``--psk-without-modes``: what does the server do with a ClientHello that
offers "pre_shared_key" and no "psk_key_exchange_modes"? RFC 8446 4.2.9 says
a client "MUST provide" the latter when it sends the former and that servers
"MUST abort the handshake" otherwise, so the first record is an alert rather
than a ServerHello. We never have to produce a real binder for this: the
missing extension is checked before anything looks at one.
"""

import argparse
import socket
import struct
import sys

ALERTS = {
    0: "close_notify", 10: "unexpected_message", 20: "bad_record_mac",
    40: "handshake_failure", 42: "bad_certificate", 43: "unsupported_certificate",
    44: "certificate_revoked", 45: "certificate_expired", 46: "certificate_unknown",
    47: "illegal_parameter", 48: "unknown_ca", 49: "access_denied",
    50: "decode_error", 51: "decrypt_error", 70: "protocol_version",
    71: "insufficient_security", 80: "internal_error", 86: "inappropriate_fallback",
    90: "user_canceled", 109: "missing_extension", 110: "unsupported_extension",
    112: "unrecognized_name", 116: "certificate_required", 120: "no_application_protocol",
}


def u8(b):
    return struct.pack("!B", b)


def u16(b):
    return struct.pack("!H", b)


def vec(body, len_bytes):
    """Length-prefixed vector, the shape TLS uses everywhere."""
    n = len(body)
    if len_bytes == 1:
        return u8(n) + body
    if len_bytes == 2:
        return u16(n) + body
    if len_bytes == 3:
        return struct.pack("!BH", n >> 16, n & 0xFFFF) + body
    raise ValueError(len_bytes)


def ext(kind, body):
    return u16(kind) + vec(body, 2)


def server_name_ext(host):
    # One ServerName entry of NameType host_name(0). We pass the bytes through
    # verbatim -- an IP literal or an illegal name is the entire point.
    entry = u8(0) + vec(host.encode(), 2)
    return ext(0, vec(entry, 2))


def pre_shared_key_ext():
    """A syntactically valid pre_shared_key with filler identity and binder.

    The identity is 32 bytes that match no ticket any server ever issued and
    the binder is 32 bytes that verify against nothing. Neither matters: this
    extension only ever travels with psk_key_exchange_modes deliberately
    absent, and the rule under test is checked on the *presence* of the two
    extensions, before any binder is opened.
    """
    identity = vec(b"\xaa" * 32, 2) + struct.pack("!I", 0)   # + obfuscated_ticket_age
    binders = vec(vec(b"\xbb" * 32, 1), 2)
    return ext(41, vec(identity, 2) + binders)


def client_hello(host, groups=(0x001D,), versions=(0x0304,), psk_no_modes=False):
    body = b""
    body += u16(0x0303)                       # legacy_version
    body += b"\x00" * 32                      # random: fixed, this probe is not secret
    body += vec(b"\x01" * 32, 1)              # legacy_session_id (compat mode)
    body += vec(u16(0x1301) + u16(0x1302) + u16(0x1303), 2)
    body += vec(u8(0), 1)                     # legacy_compression_methods: null

    exts = b""
    if host is not None:
        exts += server_name_ext(host)
    exts += ext(10, vec(b"".join(u16(g) for g in groups), 2))     # supported_groups
    exts += ext(13, vec(b"".join(u16(s) for s in                  # signature_algorithms
                                 (0x0403, 0x0503, 0x0603, 0x0804, 0x0805,
                                  0x0806, 0x0401, 0x0501, 0x0601)), 2))
    exts += ext(43, vec(b"".join(u16(v) for v in versions), 1))   # supported_versions
    if not psk_no_modes:
        exts += ext(45, vec(u8(1), 1))                            # psk_key_exchange_modes
    # key_share: one entry per offered group, 32 bytes of filler each. x25519
    # accepts any 32 bytes as a public key, so the server gets as far as
    # ServerHello before anything can go wrong.
    shares = b"".join(u16(g) + vec(b"\x02" * 32, 2) for g in groups)
    exts += ext(51, vec(shares, 2))
    if psk_no_modes:
        # pre_shared_key is required to be the last extension in the hello
        # (RFC 8446 4.2.11), so it goes on after key_share, and its partner
        # extension 45 was skipped above.
        exts += pre_shared_key_ext()
    body += vec(exts, 2)

    hs = u8(1) + vec(body, 3)                 # handshake: client_hello
    return u8(22) + u16(0x0301) + vec(hs, 2)  # record, legacy 0x0301 outer version


def client_hello_tls12(host):
    """A TLS 1.2 ClientHello, built for one purpose: reading the ServerHello.

    It carries no `supported_versions`, so `legacy_version` decides and the
    answer is a TLS 1.2 ServerHello whose extension list is in the clear.
    The offer is ECDHE-RSA only because the fixture leaf every probe server in
    this harness is started with is RSA, and the signature_algorithms list is
    the same one `client_hello` above sends. Nothing after the ServerHello is
    read, so no key exchange ever has to complete.
    """
    body = b""
    body += u16(0x0303)                       # legacy_version = TLS 1.2
    body += b"\x00" * 32                      # random: fixed, this probe is not secret
    body += vec(b"\x01" * 32, 1)              # legacy_session_id
    body += vec(u16(0xC02F) + u16(0xC030) + u16(0xCCA8), 2)
    body += vec(u8(0), 1)                     # legacy_compression_methods: null

    exts = b""
    if host is not None:
        exts += server_name_ext(host)
    exts += ext(10, vec(u16(0x001D) + u16(0x0017), 2))            # supported_groups
    exts += ext(11, vec(u8(0), 1))                                # ec_point_formats
    exts += ext(13, vec(b"".join(u16(s) for s in                  # signature_algorithms
                                 (0x0403, 0x0503, 0x0603, 0x0804, 0x0805,
                                  0x0806, 0x0401, 0x0501, 0x0601)), 2))
    body += vec(exts, 2)

    hs = u8(1) + vec(body, 3)
    return u8(22) + u16(0x0301) + vec(hs, 2)


def flight_shape(sock, budget=8.0):
    """Read the server's whole first flight and report its record layout.

    We report content types and byte lengths, never payload: the question is
    how the implementation *packs* its handshake messages into records, which
    is a visible choice the record layer makes and which changed when rustls
    started buffering a flight before emitting it.
    """
    import time
    end = time.time() + budget
    shape = []
    buf = b""
    while time.time() < end and len(shape) < 24:
        try:
            sock.settimeout(max(0.2, end - time.time()))
            chunk = sock.recv(16384)
        except (socket.timeout, OSError):
            break
        if not chunk:
            break
        buf += chunk
        while len(buf) >= 5:
            ctype, _ver, length = struct.unpack("!BHH", buf[:5])
            if len(buf) < 5 + length:
                break
            shape.append("%d/%d" % (ctype, length))
            buf = buf[5 + length:]
    return shape


def read_exactly(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def first_record(sock):
    head = read_exactly(sock, 5)
    if head is None:
        return ("none", "server closed without sending a record")
    ctype, _ver, length = struct.unpack("!BHH", head)
    payload = read_exactly(sock, length) or b""
    if ctype == 21:
        if len(payload) >= 2:
            level = "fatal" if payload[0] == 2 else "warning"
            desc = ALERTS.get(payload[1], "alert-%d" % payload[1])
            return ("alert", "%s %s" % (level, desc))
        return ("alert", "truncated alert")
    if ctype == 22:
        if payload and payload[0] == 2:
            return ("handshake", "server_hello")
        return ("handshake", "handshake type %d" % (payload[0] if payload else -1))
    return ("other", "content type %d" % ctype)


def server_hello(sock):
    """Read records until the first handshake message is complete.

    Returns ("server_hello", <body bytes>) for a ServerHello, and otherwise the
    same ("alert"|"none"|"other"|"handshake", detail) shapes `first_record`
    returns. A ServerHello is small enough to arrive in one record in practice;
    the loop is here so that a server which fragments it is read correctly
    rather than reported as malformed.
    """
    hs = b""
    while True:
        head = read_exactly(sock, 5)
        if head is None:
            return ("none", "server closed without sending a record")
        ctype, _ver, length = struct.unpack("!BHH", head)
        payload = read_exactly(sock, length) or b""
        if ctype == 21:
            if len(payload) >= 2:
                level = "fatal" if payload[0] == 2 else "warning"
                desc = ALERTS.get(payload[1], "alert-%d" % payload[1])
                return ("alert", "%s %s" % (level, desc))
            return ("alert", "truncated alert")
        if ctype != 22:
            return ("other", "content type %d" % ctype)
        hs += payload
        if len(hs) >= 1 and hs[0] != 2:
            return ("handshake", "handshake type %d" % hs[0])
        if len(hs) < 4:
            continue
        n = struct.unpack("!I", b"\x00" + hs[1:4])[0]
        if len(hs) >= 4 + n:
            return ("server_hello", hs[4:4 + n])


def server_hello_extensions(body):
    """The extension type codes a ServerHello body carries, or None if the
    body does not parse. An empty list and an absent extension block are the
    same answer and are both reported as []."""
    try:
        off = 2 + 32                          # legacy_version + random
        off += 1 + body[off]                  # legacy_session_id
        off += 2 + 1                          # cipher_suite + compression_method
        if off >= len(body):
            return []
        total = struct.unpack("!H", body[off:off + 2])[0]
        off += 2
        end = off + total
        out = []
        while off + 4 <= end:
            kind, n = struct.unpack("!HH", body[off:off + 4])
            out.append(kind)
            off += 4 + n
        return out
    except (IndexError, struct.error):
        return None


def framings(sni):
    """Return {mode: [record, ...]} -- the byte stream to put on the wire.

    Everything here is a *framing* question, not a content question: the
    ClientHello inside is the same well-formed message in every case. What
    varies is how it is cut into records and what shares a record with it.
    A TLS implementation is required to treat the record layer and the
    handshake layer as independent -- a handshake message may span records and
    a record may hold several messages -- with one exception that matters for
    security: a handshake message must not be read across a change of
    encryption level, and TLS 1.3 forbids other handshake messages sharing the
    record that ends a flight.
    """
    hello = client_hello(sni)
    body = hello[5:]                       # the handshake bytes, unframed
    rec = lambda ctype, payload: u8(ctype) + u16(0x0303) + vec(payload, 2)

    # A second handshake message riding in the same record as the hello. This
    # is the server-side shape of GHSA-2mjx-qc3c-rqvc: the peer must refuse the
    # record outright rather than queue the trailing message for a later,
    # differently-encrypted epoch.
    trailing = u8(20) + vec(b"\x00" * 32, 3)          # Finished, 32 zero bytes

    half = len(body) // 2
    return {
        "normal":       [hello],
        "packed":       [rec(22, body + trailing)],
        # The same trailing-message attack as `packed`, but the record
        # layer *reassembles* the ClientHello before the trailing message
        # is visible: the first record carries a partial hello, the second
        # completes it and appends the trailing Finished. This reaches the
        # alignment check through the deframer's coalesce path -- a partial
        # span completed by a later record that also holds a whole message
        # -- which is the "messages already received but not yet processed"
        # case GHSA-2mjx-qc3c-rqvc names. A release that only refuses the
        # single-record `packed` shape still answers this one.
        "packed-split": [rec(22, body[:half]), rec(22, body[half:] + trailing)],
        "fragmented":   [rec(22, body[:half]), rec(22, body[half:])],
        "byte-split":   [rec(22, body[:1]), rec(22, body[1:])],
        "empty-first":  [rec(22, b""), hello],
        # A zero-length handshake record *inside* a partially-received message.
        # Legal framing carries no information, so an implementation must
        # neither choke on it nor let it reset the message it is assembling.
        "empty-mid":    [rec(22, body[:half]), rec(22, b""), rec(22, body[half:])],
        # A handshake header claiming a message far larger than any permitted
        # one, with only a token of it delivered. The question is whether the
        # implementation rejects on the declared size or waits for bytes that
        # will never come.
        "huge-hs":      [rec(22, u8(1) + struct.pack("!BH", 0xFF, 0xFFF0) + body[4:64])],
        # A close_notify arriving before, or immediately after, the hello --
        # i.e. while the handshake is still in flight. The peer is entitled to
        # stop talking at any point; what it must not do is leave us waiting.
        #
        # No step scores either of these. They target step 09's change (a
        # handshake-time close_notify no longer leaves `complete_io()`
        # spinning) and cannot reach it, because the mio examples these probes
        # drive never call `complete_io()`. Against the 0.23.0 and 0.23.5
        # trees `close-only` gets no reply from either and `hello-close` gets
        # a ServerHello from both; step 09's TEST_INTERFACE.md section 6 says
        # the same thing to the candidate.
        "close-only":   [rec(21, u8(1) + u8(0))],
        "hello-close":  [hello, rec(21, u8(1) + u8(0))],
        "junk":         [b"\xff" * 5],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--sni", default=None,
                    help="bytes to place in server_name; omit for no extension")
    ap.add_argument("--mode", default="normal",
                    help="record framing: " + ", ".join(sorted(framings(None))))
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--flight", action="store_true",
                    help="report the record layout of the server's whole first flight")
    ap.add_argument("--sni-ack", action="store_true",
                    help="send a TLS 1.2 hello and report whether the "
                         "ServerHello acknowledges server_name (RFC 6066 "
                         "section 3); ignores --mode")
    ap.add_argument("--psk-without-modes", action="store_true",
                    help="offer pre_shared_key with no psk_key_exchange_modes "
                         "(RFC 8446 4.2.9); sends one unframed record and "
                         "ignores --mode")
    args = ap.parse_args()

    if args.sni_ack:
        records = [client_hello_tls12(args.sni)]
    elif args.psk_without_modes:
        records = [client_hello(args.sni, psk_no_modes=True)]
    else:
        try:
            records = framings(args.sni)[args.mode]
        except KeyError:
            print("result=error detail=unknown-mode:%s" % args.mode)
            return 0

    try:
        sock = socket.create_connection((args.host, args.port), timeout=args.timeout)
    except OSError as exc:
        print("result=none detail=connect-failed:%s" % exc.__class__.__name__)
        return 0
    sock.settimeout(args.timeout)
    try:
        for record in records:
            sock.sendall(record)
        if args.flight:
            shape = flight_shape(sock, min(args.timeout, 8.0))
            print("result=flight detail=%s" % (",".join(shape) if shape else "empty"))
            return 0
        if args.sni_ack:
            kind, detail = server_hello(sock)
            if kind == "server_hello":
                exts = server_hello_extensions(detail)
                if exts is None:
                    kind, detail = "other", "unparseable server_hello"
                else:
                    kind = "sni-ack"
                    detail = "%s exts=%s" % (
                        "acknowledged" if 0 in exts else "not-acknowledged",
                        ",".join(str(e) for e in exts) if exts else "none")
        else:
            kind, detail = first_record(sock)
    except socket.timeout:
        kind, detail = "none", "no reply within %.0fs" % args.timeout
    except ConnectionResetError:
        # A close that arrives as an RST rather than a FIN is still a close --
        # the peer refused what it was sent and stopped talking. Linux resets
        # when a socket is closed with unread data still queued, which is a
        # server's own choice of how much it drains before refusing; the
        # contract accepts a close, so the probe must not report this as an
        # I/O error. `checks-lib.sh`'s refuse arm matches "server closed*".
        kind, detail = "none", "server closed (connection reset)"
    except OSError as exc:
        kind, detail = "none", "io error: %s" % exc.__class__.__name__
    finally:
        try:
            sock.close()
        except OSError:
            pass
    print("result=%s detail=%s" % (kind, detail))
    return 0


if __name__ == "__main__":
    sys.exit(main())
