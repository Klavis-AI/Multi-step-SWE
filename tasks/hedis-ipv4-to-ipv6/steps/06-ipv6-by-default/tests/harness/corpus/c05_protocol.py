"""Raw RESP framing: inline commands, malformed multibulk, pipelining.

Every input here is bytes on the wire, so the reply *and* whether the server
closed the connection are both recorded. This is the error-path half of the
protocol reader.
"""

import os
import socket
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import norm  # noqa: E402
import resp  # noqa: E402

PORTS = 1
FAMILY = "behaviour"


def _drain(host, port, raw, read_bytes=8192, first=4.0, idle=0.5):
    """Send raw bytes on a fresh connection; return (data, closed_by_peer).

    The first read waits `first` seconds — that is the server's think time, and
    it has to survive a loaded box. Later reads only wait `idle`, because by
    then the server has already started answering and anything further is
    already in flight. Some inputs here are deliberately incomplete commands
    for which the correct behaviour is to answer nothing and hold the
    connection, so a timeout is a recorded outcome, not a failure.
    """
    infos = socket.getaddrinfo(host, port, socket.AF_UNSPEC, socket.SOCK_STREAM)
    af, st, proto, _, sa = infos[0]
    s = socket.socket(af, st, proto)
    s.settimeout(first)
    s.connect(sa)
    try:
        s.sendall(raw)
    except OSError:
        pass
    out = b""
    closed = False
    try:
        while len(out) < read_bytes:
            chunk = s.recv(4096)
            if not chunk:
                closed = True
                break
            out += chunk
            s.settimeout(idle)
    except socket.timeout:
        pass
    except OSError:
        closed = True
    s.close()
    # `out` can overshoot `read_bytes` by most of a chunk, and where a chunk
    # boundary falls in a long reply is the kernel's business, not the server's.
    # Recording a fixed-length prefix keeps the observation about the reply.
    return out[:read_bytes], closed


def _probe(ctx, port, label, raw, adv=True, read_bytes=8192):
    data, closed = _drain("::1", port, raw, read_bytes=read_bytes)
    ctx.emit("proto/%s" % label,
             [norm.normalize_text(data), closed], adv=adv)


def run(ctx):
    port = ctx.port(0)
    s = ctx.server("protocol", port, ctxmod.default_directives(port))
    s.start().wait_ready()

    with s.client() as c:
        c.cmd("SELECT", 0)
        c.cmd("FLUSHALL")
        c.cmd("SET", "pk", "pv")

    # -- inline commands ---------------------------------------------------
    _probe(ctx, port, "inline-ping", b"PING\r\n", adv=False)
    _probe(ctx, port, "inline-lf-only", b"PING\n")
    _probe(ctx, port, "inline-leading-space", b"   PING\r\n")
    _probe(ctx, port, "inline-trailing-space", b"PING   \r\n")
    _probe(ctx, port, "inline-multi-space", b"GET    pk\r\n")
    _probe(ctx, port, "inline-empty-line", b"\r\nPING\r\n")
    _probe(ctx, port, "inline-only-spaces", b"    \r\nPING\r\n")
    _probe(ctx, port, "inline-quoted", b'SET "q k" "q v"\r\nGET "q k"\r\n')
    _probe(ctx, port, "inline-single-quoted", b"SET 'sq' 'sv'\r\nGET 'sq'\r\n")
    _probe(ctx, port, "inline-unbalanced-quote", b'GET "unterminated\r\n')
    _probe(ctx, port, "inline-escape-hex", b'SET eh "\\x41\\x42"\r\nGET eh\r\n')
    _probe(ctx, port, "inline-escape-nl", b'SET en "a\\nb"\r\nSTRLEN en\r\n')
    _probe(ctx, port, "inline-quote-not-at-end", b'GET "pk"x\r\n')
    # A long inline command is fine as long as the newline arrives: the reader
    # only gives up once the buffer passes its inline ceiling with no line in
    # it. Both sides of that boundary are probed.
    _probe(ctx, port, "inline-long-argument", b"PING " + b"A" * 70000 + b"\r\n")
    _probe(ctx, port, "inline-too-long-no-newline", b"PING " + b"A" * 70000)
    _probe(ctx, port, "inline-unknown", b"NOPE\r\n")
    _probe(ctx, port, "inline-then-multibulk",
           b"PING\r\n*1\r\n$4\r\nPING\r\n")

    # -- multibulk framing -------------------------------------------------
    _probe(ctx, port, "multibulk-ok", b"*1\r\n$4\r\nPING\r\n", adv=False)
    _probe(ctx, port, "multibulk-zero", b"*0\r\n*1\r\n$4\r\nPING\r\n")
    _probe(ctx, port, "multibulk-negative", b"*-1\r\n*1\r\n$4\r\nPING\r\n")
    _probe(ctx, port, "multibulk-not-a-number", b"*x\r\n")
    _probe(ctx, port, "multibulk-huge", b"*20000000\r\n")
    _probe(ctx, port, "multibulk-at-limit", b"*1048576\r\n")
    _probe(ctx, port, "multibulk-past-limit", b"*1048577\r\n")
    _probe(ctx, port, "multibulk-missing-dollar", b"*1\r\nPING\r\n")
    _probe(ctx, port, "multibulk-bulk-not-number", b"*1\r\n$x\r\n")
    _probe(ctx, port, "multibulk-bulk-negative", b"*1\r\n$-1\r\n")
    # 512MB+1 is rejected; the at-limit value is deliberately *not* probed,
    # since accepting it makes the server reserve half a gigabyte.
    _probe(ctx, port, "multibulk-bulk-huge", b"*1\r\n$536870913\r\n")
    _probe(ctx, port, "multibulk-short-payload", b"*1\r\n$10\r\nPING\r\n")
    _probe(ctx, port, "multibulk-empty-arg",
           b"*2\r\n$3\r\nGET\r\n$0\r\n\r\n")
    _probe(ctx, port, "multibulk-nul-in-arg",
           b"*3\r\n$3\r\nSET\r\n$3\r\na\x00b\r\n$1\r\nv\r\n")
    _probe(ctx, port, "multibulk-wrong-terminator",
           b"*1\r\n$4\r\nPINGXX")
    _probe(ctx, port, "multibulk-split-across-writes",
           b"*2\r\n$3\r\nGET\r\n$2\r\npk\r\n")
    _probe(ctx, port, "multibulk-truncated-then-close", b"*2\r\n$3\r\nGET\r\n")
    _probe(ctx, port, "multibulk-nested-star",
           b"*1\r\n$1\r\n*\r\n")

    # -- pipelining --------------------------------------------------------
    pipeline = b"".join(resp.encode_command(["SET", "p%d" % i, str(i)])
                        for i in range(50))
    pipeline += resp.encode_command(["DBSIZE"])
    _probe(ctx, port, "pipeline-51", pipeline, adv=False, read_bytes=65536)
    _probe(ctx, port, "pipeline-with-error-midway",
           resp.encode_command(["PING"])
           + b"*1\r\n$1\r\nx\r\n"
           + resp.encode_command(["PING"]))

    # -- oversized but legal argument --------------------------------------
    big = b"z" * (1 << 20)
    _probe(ctx, port,
           "bulk-1mb", resp.encode_command([b"SET", b"bigk", big])
           + resp.encode_command(["STRLEN", "bigk"]), adv=False)

    with s.client() as c:
        ctx.emit_reply("proto/final-dbsize", c.cmd("DBSIZE"))
        ctx.emit_reply("proto/final-get-pk", c.cmd("GET", "pk"))
        ctx.emit_reply("proto/quoted-key", c.cmd("GET", "q k"), adv=True)
        ctx.emit_reply("proto/escape-hex-value", c.cmd("GET", "eh"), adv=True)
    s.stop()
