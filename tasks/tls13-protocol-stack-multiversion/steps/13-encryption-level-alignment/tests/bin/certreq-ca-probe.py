#!/usr/bin/env python3
"""Read the `certificate_authorities` of a CertificateRequest off the wire.

A TLS server that asks for a client certificate may tell the client which
issuers it would accept. The example server in this workspace never asks for a
client certificate, so the only server here that can be driven into sending a
CertificateRequest is the interoperability shim -- driven as a server, with
client authentication required, exactly the way the conformance runner drives
it. The shim is launched with documented flags and what is read is the bytes
it puts on a socket.

Two directions, because the message is in the clear at one version and not at
the other:

  --mode tls12   The TLS 1.2 CertificateRequest is plaintext. This script is
                 the peer: it sends a hand-built TLS 1.2 ClientHello, reads the
                 server's flight up to ServerHelloDone, and reports the
                 `certificate_authorities` entries as hex DER. That field is
                 mandatory at TLS 1.2 (RFC 5246 7.4.4), so "no issuers" is a
                 zero-length list and never an absent field.

  --mode tls13   The TLS 1.3 CertificateRequest is encrypted, so it cannot be
                 read off the wire without completing the handshake. `openssl
                 s_client` -- the same independent implementation the rest of
                 this harness uses as its differential peer -- completes it,
                 and `-msg` makes it print every handshake message it received
                 after decryption, as hex. This script bridges the two sockets
                 and parses that hex itself rather than trusting a summary
                 line, because at TLS 1.3 `certificate_authorities` is an
                 *extension* (RFC 8446 4.2.4) and the question this probe is
                 asked includes whether the extension is there at all.

The shim is a TCP *client* even in `-server` mode: it connects out to `-port`.
That is why this script owns the listening sockets and launches the shim rather
than the other way round.

Output is one line, `result=<kind> detail=<...>`, the same shape the other
probes in this directory use:

  result=names          detail=count=<n> ids=<comma-separated hex DER>
  result=no-authorities detail=...   a TLS 1.3 CertificateRequest carrying no
                                     `certificate_authorities` extension
  result=no-certreq     detail=...   no client certificate was asked for
  result=declined       detail=exit=89   the shim refused the flags it was given
  result=failed         detail=...   the shim or the peer stopped early
  result=timeout        detail=...   nothing arrived inside the budget
"""

import argparse
import binascii
import os
import re
import select
import socket
import struct
import subprocess
import sys
import time

CONNECT_GRACE = 15.0         # seconds to wait for the shim to dial in

# A TLS 1.2 ClientHello wide enough that any rustls server on this ladder can
# pick a suite: the ECDHE suites it has always offered, plus the RSA ones as a
# floor. No TLS 1.3 here on purpose -- see the module docstring.
SUITES = (0xC02F, 0xC030, 0xC02B, 0xC02C, 0xCCA8, 0xCCA9, 0x009C, 0x009D)
SIGALGS = (0x0403, 0x0503, 0x0603, 0x0804, 0x0805, 0x0806, 0x0401, 0x0501, 0x0601)
GROUPS = (0x001D, 0x0017, 0x0018)

CERT_AUTHORITIES = 47        # RFC 8446 4.2.4


def u8(v):
    return struct.pack("!B", v)


def u16(v):
    return struct.pack("!H", v)


def vec(body, n):
    if n == 1:
        return u8(len(body)) + body
    if n == 2:
        return u16(len(body)) + body
    return struct.pack("!BH", len(body) >> 16, len(body) & 0xFFFF) + body


def ext(kind, body):
    return u16(kind) + vec(body, 2)


def client_hello_12(host):
    body = u16(0x0303) + b"\x00" * 32 + vec(b"", 1)
    body += vec(b"".join(u16(s) for s in SUITES), 2)
    body += vec(u8(0), 1)
    e = b""
    e += ext(0, vec(u8(0) + vec(host.encode(), 2), 2))          # server_name
    e += ext(10, vec(b"".join(u16(g) for g in GROUPS), 2))      # supported_groups
    e += ext(11, vec(u8(0), 1))                                 # ec_point_formats
    e += ext(13, vec(b"".join(u16(s) for s in SIGALGS), 2))     # signature_algorithms
    e += ext(23, b"")                                           # extended_master_secret
    body += vec(e, 2)
    hs = u8(1) + vec(body, 3)
    return u8(22) + u16(0x0301) + vec(hs, 2)


def handshake_messages(buf):
    """Reassemble handshake messages across record boundaries.

    Returns (messages, alert) where messages is a list of (type, body) and
    alert is None or the two alert bytes. A server is free to pack its flight
    into one record or into one record per message; both are read here, because
    how a flight is packed is not what this probe is asking about.
    """
    i = 0
    hs = b""
    alert = None
    while i + 5 <= len(buf):
        ct, _ver, ln = struct.unpack("!BHH", buf[i:i + 5])
        if i + 5 + ln > len(buf):
            break
        if ct == 22:
            hs += buf[i + 5:i + 5 + ln]
        elif ct == 21 and alert is None:
            alert = buf[i + 5:i + 5 + ln][:2]
        i += 5 + ln
    out = []
    j = 0
    while j + 4 <= len(hs):
        t = hs[j]
        ln = int.from_bytes(hs[j + 1:j + 4], "big")
        if j + 4 + ln > len(hs):
            break
        out.append((t, hs[j + 4:j + 4 + ln]))
        j += 4 + ln
    return out, alert


def dn_list(blob):
    """A DistinguishedName<1..2^16-1> sequence, reported as hex DER."""
    names = []
    i = 0
    while i + 2 <= len(blob):
        ln = int.from_bytes(blob[i:i + 2], "big")
        i += 2
        if i + ln > len(blob):
            break
        names.append(binascii.hexlify(blob[i:i + ln]).decode())
        i += ln
    return names


def certreq_authorities_12(body):
    """certificate_authorities out of a TLS 1.2 CertificateRequest.

    RFC 5246 7.4.4: certificate_types<1..2^8-1>,
    supported_signature_algorithms<2..2^16-2>,
    certificate_authorities<0..2^16-1>, itself a sequence of
    DistinguishedName<1..2^16-1>.
    """
    i = 0
    if i >= len(body):
        return None
    i += 1 + body[i]
    if i + 2 > len(body):
        return None
    i += 2 + int.from_bytes(body[i:i + 2], "big")
    if i + 2 > len(body):
        return None
    total = int.from_bytes(body[i:i + 2], "big")
    i += 2
    return dn_list(body[i:min(i + total, len(body))])


def certreq_extensions_13(body):
    """The extension map of a TLS 1.3 CertificateRequest.

    RFC 8446 4.3.2: certificate_request_context<0..2^8-1> then
    Extension extensions<2..2^16-1>.
    """
    if not body:
        return None
    i = 1 + body[0]
    if i + 2 > len(body):
        return None
    total = int.from_bytes(body[i:i + 2], "big")
    i += 2
    end = min(i + total, len(body))
    exts = {}
    while i + 4 <= end:
        t = int.from_bytes(body[i:i + 2], "big")
        ln = int.from_bytes(body[i + 2:i + 4], "big")
        i += 4
        exts[t] = body[i:i + ln]
        i += ln
    return exts


def bind_loopback():
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", 0))
    s.listen(1)
    return s, s.getsockname()[1]


# The BoGo protocol gained a connection prologue at rustls 0.23.1 (upstream
# BoringSSL added it so the runner can tell which shim owns a socket when it
# runs them in parallel): before any TLS byte, the shim writes the 64-bit
# little-endian value of `-shim-id` to the TCP stream. Steps 06-07 predate it,
# steps 08-13 all send it -- which is exactly where these three groups started
# failing. The probe is the runner here, so the probe has to speak the
# prologue: name the id, then drain the bytes before reading TLS. Without the
# drain the eight bytes land where a record header is expected -- content type
# 0, version 0x0000 -- and the tls12 parser reports `no-certreq` for a
# CertificateRequest that was in fact sent, while the tls13 path forwards them
# to openssl and breaks that handshake instead.
#
# But `-shim-id` cannot be passed unconditionally either. The steps 06-07 shim
# has no case for it and its argument parser ends in
# `_ => { println!("unhandled option {:?}", arg); process::exit(1) }` -- so an
# unknown flag is a hard rc=1 before the shim ever dials, not a BOGO_NACK the
# probe could report as "declined". Passing the flag everywhere just moves the
# three failures from 08-13 to 06-07.
#
# So ask the binary. Both flags are matched by the shim's argument parser
# against a string literal, and that literal is in the compiled binary's string
# table whether or not the flag is ever used. Scanning for it is a capability
# test of the exact artefact the probe is about to execute -- not a guess from
# a step number, a release string, or a path convention, all three of which are
# restatements of the ladder's shape rather than observations of the shim. A
# tree that backports either flag, or reorders the releases, still gets the
# right argv.
SHIM_ID = 1


def shim_supports(shim, flag):
    """True when `flag` appears in the shim binary's string table.

    The parser arm that accepts a flag compares against the flag's literal, so
    the literal is present in any build that understands it and absent in one
    that does not. Read in bounded chunks with an overlap, so a literal that
    straddles a chunk boundary is still found and a large binary is never held
    in memory whole.
    """
    needle = flag.encode()
    overlap = len(needle) - 1
    try:
        with open(shim, "rb") as fh:
            prev = b""
            while True:
                chunk = fh.read(1 << 20)
                if not chunk:
                    return False
                if needle in prev + chunk:
                    return True
                prev = chunk[-overlap:] if overlap else b""
    except OSError:
        # Unreadable: claim nothing. The caller then omits the flag, which is
        # the behaviour of the oldest shim and fails loudly rather than oddly.
        return False


def shim_argv(args, port, maxver, caps):
    argv = [args.shim, "-port", str(port), "-server",
            "-key-file", "key.pem", "-cert-file", "cert.pem",
            "-require-any-client-certificate", "-max-version", str(maxver)]
    if caps["shim_id"]:
        argv += ["-shim-id", str(SHIM_ID)]
    if args.ca_list is not None:
        argv += ["-use-client-ca-list", args.ca_list]
    # The second thing that broke these groups after step 07, and it is not the
    # same thing as the prologue. From rustls 0.23.19 the shim's client-auth
    # verifier is built eagerly from `-trust-cert`, and when that flag is absent
    # the shim substitutes the relative path
    # `../../../../../test-ca/rsa-2048/ca.cert` and `.unwrap()`s the open. The
    # probe launches the shim in its own key directory, not in the workspace, so
    # the relative path resolves to nothing and the shim panics with rc=101
    # before it dials. Point the flag at the cert already staged beside the key.
    #
    # Detected, not opt-in: the steps 08-09 shim has no `-trust-cert` case and
    # its parser exits 1 on an unrecognised argument, so passing it everywhere
    # would trade three failures at 10-13 for three at 08-09.
    if caps["trust_cert"]:
        argv += ["-trust-cert", "cert.pem"]
    return argv


def tail(stream, n=200):
    try:
        data = stream.read() or b""
    except Exception:
        data = b""
    text = data.decode("utf-8", "replace")
    return " ".join(text.split())[-n:]


def reap(proc):
    if proc.poll() is None:
        try:
            proc.kill()
        except Exception:
            pass
    try:
        proc.wait(timeout=5)
    except Exception:
        pass



def drain_shim_prologue(conn, deadline):
    """Consume the shim's 8-byte BoGo id prologue, if this shim sends one.

    Only called when the shim took `-shim-id`, so a prologue is expected. It is
    still matched exactly rather than consumed blind: the id we passed is known,
    so the eight little-endian bytes are a known prefix, and on a mismatch the
    bytes are left untouched for the TLS parser to make sense of. Peeking before
    consuming costs nothing and means a wrong guess about this shim degrades to
    the old behaviour instead of eating a record header.

    The gate matters for more than tidiness. The shim is the TLS *server* here,
    so a shim with no prologue sends nothing at all until it has had a
    ClientHello -- peeking unconditionally would block out the whole timeout on
    every pre-0.23.1 step and turn three passes into three timeouts.
    """
    want = SHIM_ID.to_bytes(8, "little")
    conn.settimeout(max(0.5, min(5.0, deadline - time.time())))
    try:
        head = conn.recv(len(want), socket.MSG_PEEK)
        if head != want:
            return None          # no prologue (or a short read of TLS): leave it
        n = 0
        while n < len(want):     # now consume exactly those bytes
            got = conn.recv(len(want) - n)
            if not got:
                return None      # closed mid-prologue; the caller reports it
            n += len(got)
    except socket.timeout:
        return None              # nothing arrived; not this function's error
    except Exception as exc:
        return ("failed", "reading the shim id prologue: %s" % exc)
    return None


def accept_shim(listener, proc, deadline, caps):
    """Accept the shim's connection, or say why it never came.

    A shim that does not implement one of the flags it was given exits with the
    runner's `unimplemented` status before it dials, so waiting the whole
    budget on accept() would turn a one-second answer into a timeout. The exit
    status is checked while waiting and reported as what it is.
    """
    listener.settimeout(0.5)
    while time.time() < deadline:
        try:
            conn = listener.accept()[0]
        except socket.timeout:
            conn = None
        except Exception as exc:
            return None, ("failed", "accept: %s" % exc)
        if conn is not None:
            err = drain_shim_prologue(conn, deadline) if caps["shim_id"] else None
            if err:
                conn.close()
                return None, err
            return conn, None
        rc = proc.poll()
        if rc is not None:
            if rc == 89:
                return None, ("declined", "exit=89 the shim refused these flags "
                                          "as unimplemented")
            return None, ("failed", "shim exited rc=%d before connecting: %s"
                          % (rc, tail(proc.stderr)))
    return None, ("timeout", "the shim did not connect within %.0fs" % CONNECT_GRACE)


def mode_tls12(args):
    listener, port = bind_loopback()
    proc = subprocess.Popen(shim_argv(args, port, 771, args.caps), cwd=args.keydir,
                            stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        conn, err = accept_shim(listener, proc, time.time() + CONNECT_GRACE,
                                args.caps)
        if err:
            return err
        buf = b""
        with conn:
            conn.settimeout(args.timeout)
            conn.sendall(client_hello_12(args.sni))
            deadline = time.time() + args.timeout
            while time.time() < deadline and len(buf) < 262144:
                try:
                    chunk = conn.recv(65536)
                except socket.timeout:
                    break
                except Exception:
                    break
                if not chunk:
                    break
                buf += chunk
                msgs, alert = handshake_messages(buf)
                if alert is not None or any(t == 14 for t, _ in msgs):
                    break
        msgs, alert = handshake_messages(buf)
        if not msgs and alert is not None:
            return ("failed", "the server answered the hello with alert %s"
                    % binascii.hexlify(alert).decode())
        if not msgs:
            return ("timeout", "no handshake message arrived")
        for t, body in msgs:
            if t == 13:
                names = certreq_authorities_12(body)
                if names is None:
                    return ("failed", "unparseable CertificateRequest")
                return ("names", "count=%d ids=%s" % (len(names), ",".join(names)))
        return ("no-certreq", "the server sent no CertificateRequest; "
                              "types seen: %s" % ",".join(str(t) for t, _ in msgs))
    finally:
        reap(proc)
        listener.close()


# `openssl s_client -msg` frames each message it *received* with a `<<< ` line
# and then indents the bytes. Anything not indented hex ends the message.
MSG_HEAD = re.compile(r"^<<<[^\n]*,\s*Handshake\s")
HEXLINE = re.compile(r"^[ \t]{2,}((?:[0-9a-fA-F]{2}[ \t]*)+)$")


def received_handshakes(text):
    runs = []
    cur = None
    for line in text.splitlines():
        if MSG_HEAD.match(line):
            if cur is not None:
                runs.append(cur)
            cur = []
            continue
        if line.startswith(">>>") or line.startswith("<<<"):
            if cur is not None:
                runs.append(cur)
                cur = None
            continue
        if cur is not None:
            m = HEXLINE.match(line.rstrip())
            if m:
                cur.append(re.sub(r"[ \t]", "", m.group(1)))
            else:
                runs.append(cur)
                cur = None
    if cur is not None:
        runs.append(cur)
    msgs = []
    for parts in runs:
        try:
            raw = binascii.unhexlify("".join(parts))
        except Exception:
            continue
        if len(raw) >= 4:
            ln = int.from_bytes(raw[1:4], "big")
            msgs.append((raw[0], raw[4:4 + ln]))
    return msgs


def parse_peer_output(text):
    for t, body in received_handshakes(text):
        if t != 13:
            continue
        exts = certreq_extensions_13(body)
        if exts is None:
            return ("failed", "unparseable CertificateRequest")
        blob = exts.get(CERT_AUTHORITIES)
        if blob is None:
            return ("no-authorities", "the CertificateRequest carried no "
                                      "certificate_authorities extension; "
                                      "extensions present: %s"
                    % ",".join(str(k) for k in sorted(exts)))
        if len(blob) < 2:
            return ("failed", "truncated certificate_authorities extension")
        total = int.from_bytes(blob[:2], "big")
        names = dn_list(blob[2:2 + total])
        return ("names", "count=%d ids=%s" % (len(names), ",".join(names)))
    if "SSL handshake has read" in text or "New, TLSv1.3" in text:
        return ("no-certreq", "the handshake completed and no client "
                              "certificate was requested")
    return ("failed", "the differential peer did not complete a handshake: %s"
            % " ".join(text.split())[-200:])


def mode_tls13(args):
    ls_shim, p_shim = bind_loopback()
    ls_peer, p_peer = bind_loopback()
    proc = subprocess.Popen(shim_argv(args, p_shim, 772, args.caps), cwd=args.keydir,
                            stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    peer = None
    try:
        # openssl is started only once the shim has dialled in, so that a shim
        # which declines the flags costs one exit status and not a second
        # process that has nothing to talk to.
        conn, err = accept_shim(ls_shim, proc, time.time() + CONNECT_GRACE,
                                args.caps)
        if err:
            return err
        cert = os.path.join(args.keydir, "cert.pem")
        key = os.path.join(args.keydir, "key.pem")
        peer = subprocess.Popen(
            ["openssl", "s_client", "-connect", "127.0.0.1:%d" % p_peer,
             "-tls1_3", "-cert", cert, "-key", key, "-CAfile", cert,
             "-servername", args.sni, "-msg"],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)
        ls_peer.settimeout(args.timeout)
        try:
            other = ls_peer.accept()[0]
        except Exception as exc:
            return ("failed", "the differential peer did not connect: %s" % exc)
        with conn, other:
            conn.setblocking(False)
            other.setblocking(False)
            deadline = time.time() + args.timeout
            while time.time() < deadline:
                ready, _, _ = select.select([conn, other], [], [], 0.5)
                broken = False
                for src in ready:
                    dst = other if src is conn else conn
                    try:
                        data = src.recv(65536)
                    except Exception:
                        data = b""
                    if not data:
                        broken = True
                        break
                    try:
                        dst.sendall(data)
                    except Exception:
                        broken = True
                        break
                if broken or peer.poll() is not None:
                    break
        try:
            out = peer.communicate(timeout=10)[0] or b""
        except Exception:
            reap(peer)
            out = b""
        return parse_peer_output(out.decode("utf-8", "replace"))
    finally:
        reap(proc)
        if peer is not None:
            reap(peer)
        ls_shim.close()
        ls_peer.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shim", required=True)
    ap.add_argument("--keydir", required=True)
    ap.add_argument("--mode", choices=("tls12", "tls13"), default="tls12")
    ap.add_argument("--ca-list", default=None,
                    help="value for the shim's -use-client-ca-list; omitted "
                         "entirely when not given")
    ap.add_argument("--sni", default="localhost")
    ap.add_argument("--timeout", type=float, default=30.0)
    args = ap.parse_args()
    if not os.access(args.shim, os.X_OK):
        print("result=failed detail=no shim at %s" % args.shim)
        return 0
    # Which argv this shim understands is read off the shim, once, here -- so
    # every step in the ladder runs this same probe with no per-step argument
    # and no step number anywhere in the file.
    args.caps = {"shim_id": shim_supports(args.shim, "-shim-id"),
                 "trust_cert": shim_supports(args.shim, "-trust-cert")}
    try:
        kind, detail = (mode_tls12 if args.mode == "tls12" else mode_tls13)(args)
    except Exception as exc:                                 # never a traceback
        kind, detail = "failed", "probe error: %s" % exc
    print("result=%s detail=%s" % (kind, " ".join(str(detail).split())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
