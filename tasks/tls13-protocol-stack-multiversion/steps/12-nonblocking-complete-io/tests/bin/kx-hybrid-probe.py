#!/usr/bin/env python3
"""Ask both roles whether a named key-exchange group is actually *usable*.

`observe-clienthello.py` answers where a group sits in the list the client
offers. That is a statement about preference, and it is all you can read from
one hello. It is not a statement about support: a name in `supported_groups`
costs nothing to add, and a peer that lists a group it cannot perform is
indistinguishable from one that can until somebody insists on it.

So this probe insists on it, from both sides, using the mechanism TLS 1.3
provides for exactly that -- HelloRetryRequest (RFC 8446 4.1.4). Neither mode
performs any cryptography, and neither completes a handshake: the answer to
the question arrives in cleartext, in the second message each side sends.

  server mode   offer the server that one group and nothing else, and send no
                key share at all (RFC 8446 4.2.8 permits an empty
                `client_shares`; it is how a client asks to be told what to
                send). A server that supports the group answers with a
                HelloRetryRequest naming it. A server that does not has no
                group in common with us and answers `handshake_failure`.

  client mode   read the client's first hello; if it offers the group without
                a share, answer with a HelloRetryRequest selecting it, and
                measure the key share that comes back in the second hello.
                A group whose implementation is real produces a share of the
                size that group's definition fixes; nothing else can.

Both answers are one round trip of plaintext handshake bytes, so this file
stays where the other probes are: it decodes TLS by hand, links against no TLS
library, and shares no code with the implementation under test.

usage: kx-hybrid-probe.py server --port N --group NAME
       kx-hybrid-probe.py client --port N --group NAME
output: one `key=value` line on stdout, then exit 0
"""

import argparse
import socket
import struct
import sys

# The fixed ServerHello.random that marks a HelloRetryRequest, RFC 8446 4.1.3.
HRR_RANDOM = bytes.fromhex(
    "CF21AD74E59A6111BE1D8C021E65B891C2A211167ABB8C5E079E09E2C8A8339C")

# The same table observe-clienthello.py carries, so a step names a group the
# same way in every check it writes.
GROUPS = {
    "secp256r1": 0x0017, "secp384r1": 0x0018, "secp521r1": 0x0019,
    "x25519": 0x001D, "x448": 0x001E,
    "X25519MLKEM768": 0x11EC, "MLKEM768": 0x0201,
}

# draft-ietf-tls-ecdhe-mlkem section 4.1 fixes both the size and the order of
# the client's X25519MLKEM768 (0x11ec) key_exchange: the 1184-byte ML-KEM-768
# encapsulation key first, the 32-byte X25519 public value second, 1216 bytes
# in all. The same test observe-clienthello.py runs on the first hello, so a
# share that is well formed there is well formed here too; it is applied to
# the share a client sends *after* a HelloRetryRequest, which no other check
# reaches.
MLKEM_Q = 3329
MLKEM768_EK_BYTES = 1184
X25519MLKEM768_SHARE_BYTES = 1216


def check_x25519mlkem768(key):
    """Return "ok", or a short word naming how the share is wrong."""
    if len(key) != X25519MLKEM768_SHARE_BYTES:
        return "size=%d" % len(key)
    ek = key[:MLKEM768_EK_BYTES]
    bad = 0
    for i in range(0, 1152, 3):
        if (ek[i] | ((ek[i + 1] & 0x0f) << 8)) >= MLKEM_Q:
            bad += 1
        if ((ek[i + 1] >> 4) | (ek[i + 2] << 4)) >= MLKEM_Q:
            bad += 1
    if bad:
        return "mlkem-part-not-an-encapsulation-key:%d-of-768-coefficients-out-of-range" % bad
    return "ok"


ALERTS = {
    0: "close_notify", 10: "unexpected_message", 20: "bad_record_mac",
    40: "handshake_failure", 42: "bad_certificate", 47: "illegal_parameter",
    48: "unknown_ca", 50: "decode_error", 51: "decrypt_error",
    70: "protocol_version", 71: "insufficient_security", 80: "internal_error",
    109: "missing_extension", 110: "unsupported_extension",
    112: "unrecognized_name", 120: "no_application_protocol",
}


def u8(b):
    return struct.pack("!B", b)


def u16(b):
    return struct.pack("!H", b)


def vec(body, len_bytes):
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


SIG_ALGS = (0x0403, 0x0503, 0x0603, 0x0804, 0x0805, 0x0806,
            0x0401, 0x0501, 0x0601)


def client_hello(group, sni="localhost"):
    """A hello offering exactly one group and carrying no key share for it.

    `client_shares` is empty, which RFC 8446 4.2.8 allows in terms ("this
    vector MAY be empty if the client is requesting a HelloRetryRequest").
    That is the whole point: we are asking the server to tell us which group
    it wants, which it can only do for a group it supports.
    """
    body = u16(0x0303) + b"\x00" * 32 + vec(b"\x01" * 32, 1)
    body += vec(u16(0x1301) + u16(0x1302) + u16(0x1303), 2)
    body += vec(u8(0), 1)
    e = ext(0, vec(u8(0) + vec(sni.encode(), 2), 2))
    e += ext(10, vec(u16(group), 2))
    e += ext(13, vec(b"".join(u16(s) for s in SIG_ALGS), 2))
    e += ext(43, vec(u16(0x0304), 1))
    e += ext(45, vec(u8(1), 1))
    e += ext(51, vec(b"", 2))
    body += vec(e, 2)
    return u8(22) + u16(0x0301) + vec(u8(1) + vec(body, 3), 2)


def hello_retry_request(session_id, suite, group):
    """A HelloRetryRequest selecting `group`: a ServerHello with the fixed
    random of RFC 8446 4.1.3 and a `key_share` holding only the group id."""
    body = u16(0x0303) + HRR_RANDOM + vec(session_id, 1) + u16(suite) + u8(0)
    body += vec(ext(43, u16(0x0304)) + ext(51, u16(group)), 2)
    return u8(22) + u16(0x0303) + vec(u8(2) + vec(body, 3), 2)


class Framing:
    """Record-layer reader that reassembles handshake messages.

    A handshake message may span records and a record may hold several, so
    neither boundary can be assumed. change_cipher_spec records are dropped:
    a client in compatibility mode sends one after a HelloRetryRequest and it
    carries nothing we are asking about.
    """

    def __init__(self, sock, timeout):
        self.sock = sock
        self.sock.settimeout(timeout)
        self.raw = b""
        self.hs = b""

    def _record(self):
        while len(self.raw) < 5:
            chunk = self.sock.recv(8192)
            if not chunk:
                raise EOFError("peer closed")
            self.raw += chunk
        ctype, _ver, length = struct.unpack("!BHH", self.raw[:5])
        while len(self.raw) < 5 + length:
            chunk = self.sock.recv(8192)
            if not chunk:
                raise EOFError("peer closed mid-record")
            self.raw += chunk
        payload = self.raw[5:5 + length]
        self.raw = self.raw[5 + length:]
        return ctype, payload

    def message(self):
        while True:
            if len(self.hs) >= 4:
                length = struct.unpack("!I", b"\x00" + self.hs[1:4])[0]
                if len(self.hs) >= 4 + length:
                    msg = self.hs[:4 + length]
                    self.hs = self.hs[4 + length:]
                    return msg
            ctype, payload = self._record()
            if ctype == 20:
                continue
            if ctype == 21:
                desc = ALERTS.get(payload[1], "alert-%d" % payload[1]) \
                    if len(payload) > 1 else "truncated"
                raise Refused("alert %s %s" % (
                    "fatal" if payload and payload[0] == 2 else "warning", desc))
            if ctype != 22:
                raise Refused("content-type-%d" % ctype)
            self.hs += payload


class Refused(Exception):
    pass


def _extensions(body, pos):
    """Yield (kind, value) over an extension block starting at `pos`."""
    total = struct.unpack("!H", body[pos:pos + 2])[0]
    pos += 2
    end = pos + total
    while pos + 4 <= end:
        kind, length = struct.unpack("!HH", body[pos:pos + 4])
        pos += 4
        yield kind, body[pos:pos + length]
        pos += length


def parse_client_hello(msg):
    body = msg[4:]
    pos = 2 + 32
    sid_len = body[pos]
    pos += 1
    session_id = body[pos:pos + sid_len]
    pos += sid_len
    cs_len = struct.unpack("!H", body[pos:pos + 2])[0]
    pos += 2
    suites = [struct.unpack("!H", body[pos + i:pos + i + 2])[0]
              for i in range(0, cs_len, 2)]
    pos += cs_len
    pos += 1 + body[pos]
    groups, shares = [], []
    for kind, value in _extensions(body, pos):
        if kind == 10:
            n = struct.unpack("!H", value[:2])[0]
            groups = [struct.unpack("!H", value[2 + i:4 + i])[0]
                      for i in range(0, n, 2)]
        elif kind == 51:
            n = struct.unpack("!H", value[:2])[0]
            q = 2
            while q + 4 <= 2 + n:
                gid, slen = struct.unpack("!HH", value[q:q + 4])
                # The body is kept as well as the length: the client's share
                # after a retry is decoded by the caller, and a length alone
                # cannot separate a real encapsulation key from a placeholder
                # of the right size.
                shares.append((gid, slen, value[q + 4:q + 4 + slen]))
                q += 4 + slen
    return session_id, suites, groups, shares


def parse_server_hello(msg):
    """Return (is_hello_retry_request, selected_group_or_None)."""
    body = msg[4:]
    is_hrr = body[2:34] == HRR_RANDOM
    pos = 34
    pos += 1 + body[pos]
    pos += 2 + 1
    selected = None
    for kind, value in _extensions(body, pos):
        if kind == 51:
            if len(value) == 2:
                selected = struct.unpack("!H", value)[0]
            elif len(value) >= 4:
                selected = struct.unpack("!H", value[:2])[0]
    return is_hrr, selected


def say(**kw):
    print(" ".join("%s=%s" % (k, v) for k, v in kw.items()))
    sys.stdout.flush()


def run_server_mode(args, group):
    try:
        sock = socket.create_connection((args.host, args.port),
                                        timeout=args.timeout)
    except OSError as exc:
        say(result="none", detail="connect-failed:%s" % exc.__class__.__name__)
        return 0
    try:
        sock.sendall(client_hello(group))
        framing = Framing(sock, args.timeout)
        msg = framing.message()
        if not msg or msg[0] != 2:
            say(result="unexpected", detail="handshake-type-%d" % msg[0])
            return 0
        is_hrr, selected = parse_server_hello(msg)
        say(result="hello-retry" if is_hrr else "server-hello",
            group="0x%04x" % selected if selected is not None else "none")
    except Refused as exc:
        say(result="refused", detail=str(exc).replace(" ", "-"))
    except EOFError:
        say(result="none", detail="closed-without-answering")
    except socket.timeout:
        say(result="none", detail="no-answer-within-%.0fs" % args.timeout)
    except (OSError, IndexError, struct.error) as exc:
        say(result="error", detail="%s" % type(exc).__name__)
    finally:
        try:
            sock.close()
        except OSError:
            pass
    return 0


def _listen(args):
    """Dual-stack where we can, for the reason observe-clienthello.py gives:
    `localhost` resolves to both families and the examples take whichever the
    resolver puts first, so an IPv4-only listener silently sees nothing."""
    srv = None
    if socket.has_ipv6:
        try:
            srv = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
            srv.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            srv.bind(("::", args.port))
        except OSError:
            if srv is not None:
                srv.close()
            srv = None
    if srv is None:
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("0.0.0.0", args.port))
    srv.listen(4)
    srv.settimeout(args.timeout)
    sys.stderr.write("listening %d\n" % args.port)
    sys.stderr.flush()
    return srv


def run_client_mode(args, group):
    srv = _listen(args)
    try:
        conn, _peer = srv.accept()
    except socket.timeout:
        say(result="none", detail="no-connection-within-%.0fs" % args.timeout)
        srv.close()
        return 0
    try:
        framing = Framing(conn, args.timeout)
        session_id, suites, groups, shares = parse_client_hello(framing.message())
        if group not in groups:
            say(result="not-offered",
                groups=",".join("0x%04x" % g for g in groups) or "none")
            return 0
        if group in [g for g, _l, _b in shares]:
            # The client already spent a share on it, so asking for a retry
            # would be the illegal request of RFC 8446 4.2.8 and a correct
            # client would refuse. Report the fact rather than provoke it.
            say(result="already-shared",
                share_len=[l for g, l, _b in shares if g == group][0])
            return 0
        suite = 0x1301 if 0x1301 in suites else (suites[0] if suites else 0x1301)
        conn.sendall(hello_retry_request(session_id, suite, group))
        _sid, _cs, _groups2, shares2 = parse_client_hello(framing.message())
        mine = [(l, b) for g, l, b in shares2 if g == group]
        facts = {
            "result": "retried",
            "share_len": mine[0][0] if mine else "absent",
            "shares": ",".join("0x%04x/%d" % (g, l) for g, l, _b in shares2) or "none",
        }
        if mine and group == GROUPS["X25519MLKEM768"]:
            # Report the layout verdict when it can be computed. Any error
            # omits the fact rather than failing the probe: the caller treats
            # a missing verdict as "not checked" and falls back to the length.
            try:
                facts["share_ok"] = check_x25519mlkem768(mine[0][1])
            except Exception:
                pass
        say(**facts)
    except Refused as exc:
        say(result="refused", detail=str(exc).replace(" ", "-"))
    except EOFError:
        say(result="aborted", detail="closed-without-a-second-hello")
    except socket.timeout:
        say(result="aborted", detail="no-second-hello-within-%.0fs" % args.timeout)
    except (OSError, IndexError, struct.error) as exc:
        say(result="error", detail="%s" % type(exc).__name__)
    finally:
        try:
            conn.close()
        except OSError:
            pass
        srv.close()
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=("server", "client"))
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--group", default="X25519MLKEM768",
                    help="named group, by name or as 0xNNNN")
    ap.add_argument("--timeout", type=float, default=25.0)
    args = ap.parse_args()
    try:
        group = GROUPS[args.group] if args.group in GROUPS else int(args.group, 0)
    except ValueError:
        say(result="error", detail="unknown-group:%s" % args.group)
        return 0
    if args.mode == "server":
        return run_server_mode(args, group)
    return run_client_mode(args, group)


if __name__ == "__main__":
    sys.exit(main())
