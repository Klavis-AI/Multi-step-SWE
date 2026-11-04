#!/usr/bin/env python3
"""Accept TLS connections, parse each ClientHello, print one JSON object.

This is the wire-observation layer of the grader. It links against nothing,
imports no TLS library, and performs no cryptography: it reads the first
handshake flight off a socket and decodes the bytes by hand. That keeps it
fully deterministic and makes it impossible for it to "agree" with the
implementation under test by sharing code with it.

It answers questions the BoGo suite cannot, because BoGo checks whether a
handshake succeeds and this checks what the client actually offered and in what
order -- for instance whether extension ordering is stable from one connection
to the next, where a named group sits in the key-share list, or whether an SNI
extension was sent at all for an IP-literal peer.

The connections are expected to fail: nothing here can complete a handshake, so
the peer will see a truncated connection after it sends its ClientHello. That is
intended and is not an error.

usage: observe-clienthello.py --port N --count K [--timeout S]
output: one JSON object per line on stdout, K lines, then exit 0
"""
import argparse
import json
import socket
import sys

# Extension numbers this task cares about by name. Anything else is reported by
# its numeric id, so an unexpected extension is visible rather than dropped.
EXT_NAMES = {
    0: "server_name", 1: "max_fragment_length", 5: "status_request",
    10: "supported_groups", 11: "ec_point_formats", 13: "signature_algorithms",
    14: "use_srtp", 15: "heartbeat", 16: "alpn",
    18: "signed_certificate_timestamp", 19: "client_certificate_type",
    20: "server_certificate_type", 21: "padding", 22: "encrypt_then_mac",
    23: "extended_master_secret", 35: "session_ticket", 41: "pre_shared_key",
    42: "early_data", 43: "supported_versions", 44: "cookie",
    45: "psk_key_exchange_modes", 47: "certificate_authorities",
    49: "post_handshake_auth", 50: "signature_algorithms_cert",
    51: "key_share", 0xfe0d: "encrypted_client_hello",
    0xff01: "renegotiation_info",
}

GROUP_NAMES = {
    0x0017: "secp256r1", 0x0018: "secp384r1", 0x0019: "secp521r1",
    0x001d: "x25519", 0x001e: "x448",
    0x0100: "ffdhe2048", 0x0101: "ffdhe3072", 0x0102: "ffdhe4096",
    0x0103: "ffdhe6144", 0x0104: "ffdhe8192",
    0x0200: "MLKEM512", 0x0201: "MLKEM768", 0x0202: "MLKEM1024",
    0x11eb: "secp256r1MLKEM768", 0x11ec: "X25519MLKEM768",
    # The two pre-standard Kyber code points, taken from the paragraph of
    # draft-ietf-tls-ecdhe-mlkem that obsoletes them: "X25519Kyber768Draft00
    # (25497) and SecP256r1Kyber768Draft00 (25498)". Those are decimals --
    # 25497 is 0x6399, which is also what the frozen runner calls
    # CurveX25519Kyber768. There is no 0x4588 entry: the draft registers
    # X25519MLKEM768 as "4588 (0x11EC)", and 4588 there is that one group in
    # decimal, not a second code point. A name in this table is what a check
    # matches on, and a wrong one is worse than a bare 0xNNNN.
    0x6399: "X25519Kyber768Draft00", 0x639a: "SecP256r1Kyber768Draft00",
}


# draft-ietf-tls-ecdhe-mlkem section 4.1 fixes both the size and the order of
# the client's X25519MLKEM768 (0x11ec) key_exchange: the 1184-byte ML-KEM-768
# encapsulation key first, the 32-byte X25519 public value second, 1216 bytes
# in all. The size alone does not separate the two orderings, so the ML-KEM
# half is decoded as well.
#
# FIPS 203 ByteEncode_12 packs 768 coefficients as twelve-bit little-endian
# fields across the first 1152 bytes of an encapsulation key, every one of them
# already reduced below q = 3329; the trailing 32 bytes are rho and are
# unconstrained, so they are not read. Putting the X25519 value first shifts
# the whole encoding by 32 bytes and the coefficients stop being reduced --
# measured against a real hello, 130 of the 768 come out at or above q. That is
# what makes this a check on the layout rather than on the length.
#
# No cryptography is performed and nothing is verified about the key as a key:
# this is a bounds test on a public encoding, which is all a wire observer can
# honestly do.
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


class Truncated(Exception):
    pass


class Reader:
    """Bounds-checked big-endian reader. Every TLS length prefix is attacker
    controlled even here, so a short or lying length must raise rather than
    silently return a short slice and produce a plausible wrong answer."""

    def __init__(self, buf):
        self.b = buf
        self.i = 0

    def take(self, n):
        if self.i + n > len(self.b):
            raise Truncated("wanted %d bytes at %d, have %d" % (n, self.i, len(self.b)))
        out = self.b[self.i:self.i + n]
        self.i += n
        return out

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        d = self.take(2)
        return (d[0] << 8) | d[1]

    def u24(self):
        d = self.take(3)
        return (d[0] << 16) | (d[1] << 8) | d[2]

    def vec(self, lenbytes):
        n = {1: self.u8, 2: self.u16, 3: self.u24}[lenbytes]()
        return self.take(n)


def recv_handshake(conn, timeout):
    """Read TLS records until one complete handshake message is buffered.

    A ClientHello may legally span several records, and a client is free to
    split it, so this reassembles rather than assuming one record.
    """
    conn.settimeout(timeout)
    records = b""
    body = b""
    while True:
        while len(records) < 5:
            chunk = conn.recv(4096)
            if not chunk:
                raise Truncated("peer closed before a complete record header")
            records += chunk
        ctype, _ver, rlen = records[0], (records[1] << 8) | records[2], (records[3] << 8) | records[4]
        if ctype != 0x16:
            raise Truncated("first record is content type %d, not handshake" % ctype)
        while len(records) < 5 + rlen:
            chunk = conn.recv(4096)
            if not chunk:
                raise Truncated("peer closed mid-record")
            records += chunk
        body += records[5:5 + rlen]
        records = records[5 + rlen:]
        if len(body) >= 4 and len(body) >= 4 + ((body[1] << 16) | (body[2] << 8) | body[3]):
            return body


def parse_client_hello(body):
    r = Reader(body)
    htype = r.u8()
    if htype != 0x01:
        raise Truncated("handshake type %d is not ClientHello" % htype)
    length = r.u24()
    r = Reader(r.take(length))

    out = {"legacy_version": "0x%04x" % r.u16()}
    r.take(32)                                   # random
    out["session_id_len"] = len(r.vec(1))
    suites = r.vec(2)
    out["cipher_suites"] = ["0x%04x" % ((suites[i] << 8) | suites[i + 1])
                            for i in range(0, len(suites) - 1, 2)]
    r.vec(1)                                     # legacy compression methods

    out["extensions"] = []
    out["extension_ids"] = []
    if r.i >= len(r.b):
        return out                               # no extension block at all
    er = Reader(r.vec(2))
    while er.i < len(er.b):
        etype = er.u16()
        edata = er.vec(2)
        out["extension_ids"].append(etype)
        out["extensions"].append(EXT_NAMES.get(etype, "ext_%d" % etype))

        if etype == 0:                           # server_name
            names = []
            try:
                sr = Reader(Reader(edata).vec(2))
                while sr.i < len(sr.b):
                    ntype = sr.u8()
                    host = sr.vec(2)
                    names.append({"type": ntype,
                                  "host": host.decode("ascii", "replace")})
            except Truncated:
                pass
            out["server_name"] = names
        elif etype == 43:                        # supported_versions
            v = Reader(edata).vec(1)
            out["supported_versions"] = ["0x%04x" % ((v[i] << 8) | v[i + 1])
                                         for i in range(0, len(v) - 1, 2)]
        elif etype == 10:                        # supported_groups
            g = Reader(edata).vec(2)
            ids = [(g[i] << 8) | g[i + 1] for i in range(0, len(g) - 1, 2)]
            out["supported_groups"] = [GROUP_NAMES.get(x, "0x%04x" % x) for x in ids]
        elif etype == 51:                        # key_share
            shares = []
            kr = Reader(Reader(edata).vec(2))
            while kr.i < len(kr.b):
                gid = kr.u16()
                key = kr.vec(2)
                entry = {"group": GROUP_NAMES.get(gid, "0x%04x" % gid),
                         "key_bytes": len(key)}
                if gid == 0x11ec:
                    # The one group on this ladder whose share has an internal
                    # structure worth reading. Everything else here is an
                    # opaque point and its length is the whole story.
                    entry["mlkem768_share"] = check_x25519mlkem768(key)
                shares.append(entry)
            out["key_shares"] = shares
        elif etype == 16:                        # alpn
            protos = []
            ar = Reader(Reader(edata).vec(2))
            while ar.i < len(ar.b):
                protos.append(ar.vec(1).decode("ascii", "replace"))
            out["alpn"] = protos
        elif etype == 45:                        # psk_key_exchange_modes
            out["psk_modes"] = list(Reader(edata).vec(1))
    return out


def emit_summary(seen):
    """Reduce the observed hellos to the facts a shell caller can act on.

    The aggregation belongs here rather than in the caller: deciding whether
    six hellos agree is a question about parsed structure, and re-deriving it
    from JSON with line tools invites the kind of near-miss where a changed
    field name quietly turns a real difference into "no difference observed".
    """
    good = [r for r in seen if "extension_ids" in r]
    errs = [r for r in seen if "extension_ids" not in r]
    print("samples: %d" % len(good))
    print("failures: %d" % len(errs))
    for r in errs[:3]:
        print("failure_detail: %s" % r.get("error", "unknown"))
    if not good:
        return
    orders = {tuple(r["extension_ids"]) for r in good}
    print("distinct_extension_orders: %d" % len(orders))
    # The extension numbers, as an intersection and as a union over every
    # hello observed. Two lines rather than one because the two questions a
    # caller asks of them have opposite failure modes: "this client always
    # offers X" must not be satisfied by one hello out of six, and "this
    # client never offers X" must not be satisfied by five out of six. Sorted
    # numerically; order within a hello is reported by
    # distinct_extension_orders, not here.
    every = set(good[0]["extension_ids"])
    any_ = set()
    for r in good:
        every &= set(r["extension_ids"])
        any_ |= set(r["extension_ids"])
    print("extensions_every: %s" % ",".join(str(i) for i in sorted(every)))
    print("extensions_any: %s" % ",".join(str(i) for i in sorted(any_)))
    # Every distinct legacy_session_id length observed, sorted. Reported as a
    # set rather than a single sample so that a caller asserting "always
    # empty" cannot be satisfied by one lucky connection out of six.
    print("session_id_lens: %s" % ",".join(
        str(n) for n in sorted({r.get("session_id_len", -1) for r in good})))
    first = good[0]
    names = set()
    for r in good:
        for entry in r.get("server_name") or []:
            names.add(entry.get("host", ""))
    print("server_name: %s" % (",".join(sorted(names)) if names else ""))
    print("supported_groups: %s" % ",".join(first.get("supported_groups", [])))
    print("key_share_groups: %s" % ",".join(k["group"] for k in first.get("key_shares", [])))
    # The size of every share in the first hello, named by group. Reported
    # alongside key_share_groups because a caller that knows a group is present
    # still cannot tell from the name whether anything of the right shape was
    # put in it.
    print("key_share_sizes: %s" % ",".join(
        "%s=%d" % (k["group"], k["key_bytes"]) for k in first.get("key_shares", [])))
    # The X25519MLKEM768 share, aggregated over every hello observed rather
    # than read off the first, for the reason session_id_lens is a set: a
    # property of the share must not be satisfiable by one lucky connection out
    # of six. One word when all of them agree, "inconsistent:..." when they do
    # not -- which is itself a failure for any caller, since neither "always"
    # nor "never" is true of a client that varies.
    verdicts = set()
    for r in good:
        v = "absent"
        for k in r.get("key_shares", []):
            if "mlkem768_share" in k:
                v = k["mlkem768_share"]
        verdicts.add(v)
    print("mlkem768_client_share: %s" % (
        verdicts.pop() if len(verdicts) == 1
        else "inconsistent:" + ",".join(sorted(verdicts))))
    print("cipher_suites: %s" % ",".join(first.get("cipher_suites", [])))
    print("supported_versions: %s" % ",".join(first.get("supported_versions", [])))
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--count", type=int, default=1)
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--summary", action="store_true",
                    help="aggregate the observed hellos into key=value lines")
    args = ap.parse_args()

    # Listen dual-stack. "localhost" resolves to both 127.0.0.1 and ::1, and
    # which one a client picks is not ours to decide: the rustls examples moved
    # from an IPv4-only lookup to to_socket_addrs(), so from 0.22 onward they
    # take whichever address the resolver puts first -- ::1 on a stock Debian
    # and inside a default Docker network namespace. An IPv4-only listener
    # silently observes nothing there, which would read as "the client sent no
    # ClientHello" rather than "the probe was listening on the wrong family".
    srv = None
    if args.host in ("127.0.0.1", "localhost", "::", "::1") and socket.has_ipv6:
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
        srv.bind((args.host, args.port))
    srv.listen(8)
    srv.settimeout(args.timeout)
    # Tell the caller the port is open before it starts the client, so there is
    # no race between "listener is binding" and "client connects".
    sys.stderr.write("listening %s:%d\n" % (args.host, args.port))
    sys.stderr.flush()

    seen = []
    for _ in range(args.count):
        try:
            conn, _peer = srv.accept()
        except socket.timeout:
            print(json.dumps({"error": "no connection within %.1fs" % args.timeout}))
            sys.stdout.flush()
            continue
        try:
            rec = parse_client_hello(recv_handshake(conn, args.timeout))
        except (Truncated, socket.timeout, OSError, IndexError, KeyError) as exc:
            rec = {"error": "%s: %s" % (type(exc).__name__, exc)}
        finally:
            try:
                conn.close()
            except OSError:
                pass
        seen.append(rec)
        if not args.summary:
            print(json.dumps(rec, sort_keys=True))
            sys.stdout.flush()

    if args.summary:
        emit_summary(seen)

    srv.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
