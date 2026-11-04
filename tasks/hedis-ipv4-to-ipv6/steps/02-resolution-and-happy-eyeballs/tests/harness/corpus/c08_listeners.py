"""What the server listens on, and what it says about it.

Each case starts a server with one bind configuration and then asks, from the
outside, which families and addresses actually answer. The startup log is
recorded too, because the log is where the server states the listener it
opened.
"""

import errno
import os
import socket
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import norm  # noqa: E402
import resp  # noqa: E402
import srv  # noqa: E402

PORTS = 8
FAMILY = "behaviour"

FAMILIES = {"v4": socket.AF_INET, "v6": socket.AF_INET6}


def _connect(host, port, family=socket.AF_UNSPEC, timeout=3.0):
    """Return a short, stable word for what happened on connect()."""
    try:
        infos = socket.getaddrinfo(host, port, family, socket.SOCK_STREAM)
    except socket.gaierror as e:
        return "gaierror:%s" % errno.errorcode.get(-e.errno, e.errno)
    if not infos:
        return "no-address"
    af, st, proto, _, sa = infos[0]
    s = socket.socket(af, st, proto)
    s.settimeout(timeout)
    try:
        s.connect(sa)
    except socket.timeout:
        s.close()
        return "timeout"
    except OSError as e:
        s.close()
        return errno.errorcode.get(e.errno, str(e.errno))
    try:
        s.sendall(resp.encode_command(["PING"]))
        data = s.recv(64)
        return "pong" if data.startswith(b"+PONG") else "spoke:%r" % data[:16]
    except OSError as e:
        return "io:%s" % errno.errorcode.get(e.errno, e.errno)
    finally:
        s.close()


def _connect_unix(path, timeout=3.0):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect(path)
    except OSError as e:
        s.close()
        return errno.errorcode.get(e.errno, str(e.errno))
    try:
        s.sendall(resp.encode_command(["PING"]))
        return "pong" if s.recv(64).startswith(b"+PONG") else "odd"
    except OSError as e:
        return "io:%s" % errno.errorcode.get(e.errno, e.errno)
    finally:
        s.close()


LOG_KEYWORDS = ("bind", "listen", "socket", "address", "ready to accept",
                "port", "ipv", "tcp", "warning", "error", "fatal", "unix")

# Startup warnings about the *host* kernel, not about the tree. Whether they
# appear depends on the machine the scoring runs on, so they are dropped
# rather than compared.
HOST_WARNINGS = ("transparent huge pages", "overcommit_memory",
                 "somaxconn", "tcp backlog setting of",
                 "maximum number of open files")


def _log_lines_text(blob):
    """The listener-relevant part of a startup log."""
    out = []
    for line in norm.normalize_text(blob).splitlines():
        low = line.lower()
        if any(w in low for w in HOST_WARNINGS):
            continue
        if any(w in low for w in LOG_KEYWORDS):
            out.append(line)
    return out


def _log_lines(server):
    return _log_lines_text(server.log())


def _case(ctx, name, port, directives, probes, ready_host="::1",
          extra_argv=(), adv=False):
    """Start one server, probe it from the outside, record, stop."""
    s = ctx.server(name, port, directives, host=ready_host,
                   extra_argv=extra_argv)
    s.start()
    try:
        s.wait_ready(deadline=20.0, host=ready_host)
        started = True
    except Exception as e:  # noqa: BLE001 - a refusal to start is a result
        started = False
        ctx.emit("listen/%s/start-error" % name, type(e).__name__, adv=True)
    ctx.emit("listen/%s/started" % name, started, adv=adv)
    for label, host, fam in probes:
        ctx.emit("listen/%s/probe-%s" % (name, label),
                 _connect(host, port, FAMILIES.get(fam, socket.AF_UNSPEC)),
                 adv=adv)
    if started:
        with s.client(host=ready_host) as c:
            for param in ("bind", "port", "unixsocket", "tcp-backlog"):
                ctx.emit_reply("listen/%s/config-%s" % (name, param),
                               c.cmd("CONFIG", "GET", param), adv=adv)
            blob = c.cmd("INFO", "server")[1].decode("utf-8", "replace")
            ctx.emit("listen/%s/info-tcp-port" % name,
                     [l for l in blob.splitlines() if l.startswith("tcp_port:")],
                     adv=adv)
    ctx.emit("listen/%s/log" % name, _log_lines(s), adv=True)
    s.stop()
    return s


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]
    both = [("v6", "::1", "v6"), ("v4", "127.0.0.1", "v4"),
            ("localhost-any", "localhost", "any")]

    # 1. loopback v6 only
    _case(ctx, "v6-loopback", p[0], ctxmod.default_directives(p[0], "::1"), both)

    # 2. loopback v4 only
    _case(ctx, "v4-loopback", p[1], ctxmod.default_directives(p[1], "127.0.0.1"),
          both, ready_host="127.0.0.1")

    # 3. both loopbacks
    _case(ctx, "dual-loopback", p[2],
          ctxmod.default_directives(p[2], "127.0.0.1 ::1"), both)

    # 4. wildcard v6 -- IPV6_V6ONLY decides whether v4 peers are served
    _case(ctx, "wildcard-v6", p[3], ctxmod.default_directives(p[3], "::"),
          both, adv=True)

    # 5. wildcard v4
    _case(ctx, "wildcard-v4", p[4], ctxmod.default_directives(p[4], "0.0.0.0"),
          both, ready_host="127.0.0.1", adv=True)

    # 6. no bind directive at all: the server picks for itself
    nobind = [d for d in ctxmod.default_directives(p[5])
              if not d.startswith("bind ")]
    _case(ctx, "no-bind", p[5], nobind, both, adv=True)

    # 7. a scoped loopback literal
    _case(ctx, "scoped-loopback", p[6],
          ctxmod.default_directives(p[6], "::1%lo"), both, adv=True)

    # 8. a hostname rather than a literal
    _case(ctx, "hostname-bind", p[7],
          ctxmod.default_directives(p[7], "localhost"), both,
          ready_host="localhost", adv=True)

    # -- unix socket, with and without a TCP listener -----------------------
    sock = os.path.join(ctx.workspace.base, "u.sock")
    s = ctx.server("unix-and-tcp", p[0],
                   ctxmod.default_directives(p[0], "::1",
                                             extra=["unixsocket %s" % sock]))
    s.start().wait_ready()
    ctx.emit("listen/unix-and-tcp/unix", _connect_unix(sock))
    ctx.emit("listen/unix-and-tcp/tcp", _connect("::1", p[0], socket.AF_INET6))
    with s.client() as c:
        ctx.emit_reply("listen/unix-and-tcp/config",
                       c.cmd("CONFIG", "GET", "unixsocket"))
        blob = c.cmd("CLIENT", "LIST")[1]
        ctx.emit("listen/unix-and-tcp/client-list-addrs",
                 sorted(norm.addr_fields(blob)), adv=True)
    s.stop()

    sock2 = os.path.join(ctx.workspace.base, "only.sock")
    s = ctx.server("unix-only", 0,
                   ["port 0", "unixsocket %s" % sock2, "dir .",
                    "logfile server.log", "daemonize no", 'save ""',
                    "appendonly no"])
    s.start()
    time.sleep(1.0)
    ctx.emit("listen/unix-only/unix", _connect_unix(sock2), adv=True)
    ctx.emit("listen/unix-only/tcp-on-p1",
             _connect("::1", p[1], socket.AF_INET6), adv=True)
    ctx.emit("listen/unix-only/log", _log_lines(s), adv=True)
    s.stop()

    # -- configurations the server must refuse ------------------------------
    for name, directives in (
        ("unassigned-v6", ctxmod.default_directives(p[1], "2001:db8::1")),
        ("unassigned-v4", ctxmod.default_directives(p[1], "192.0.2.1")),
        ("bad-literal", ctxmod.default_directives(p[1], "not-an-address")),
        ("bracketed-literal", ctxmod.default_directives(p[1], "[::1]")),
        ("v4-mapped", ctxmod.default_directives(p[1], "::ffff:127.0.0.1")),
        ("too-many-binds",
         ctxmod.default_directives(p[1], " ".join(["::1"] * 17))),
        ("negative-port", ctxmod.default_directives(-1, "::1")),
        ("huge-port", ctxmod.default_directives(70000, "::1")),
        ("port-with-brackets", ["bind ::1", "port [%d]" % p[1], "dir .",
                                "logfile server.log", "daemonize no"]),
    ):
        # A configuration that *does* start blocks until the timeout; that
        # timeout is itself the recorded outcome, so it is kept short.
        rc, out, log = srv.start_server_expect_failure(
            ctx.tree, ctx.workspace, "refuse-%s" % name, directives, timeout=6)
        ctx.emit("refuse/%s/rc" % name, rc, adv=True)
        ctx.emit_text("refuse/%s/out" % name, out, adv=True)
        ctx.emit("refuse/%s/log" % name, _log_lines_text(log), adv=True)

    # -- a port that is already taken ---------------------------------------
    holder = ctx.server("holder", p[2], ctxmod.default_directives(p[2], "::1"))
    holder.start().wait_ready()
    rc, out, log = srv.start_server_expect_failure(
        ctx.tree, ctx.workspace, "port-taken",
        ctxmod.default_directives(p[2], "::1"), timeout=6)
    ctx.emit("refuse/port-taken/rc", rc, adv=True)
    ctx.emit_text("refuse/port-taken/out", out, adv=True)
    ctx.emit("refuse/port-taken/log", _log_lines_text(log), adv=True)
    # The same port on the *other* family is a different socket.
    rc4, out4, log4 = srv.start_server_expect_failure(
        ctx.tree, ctx.workspace, "port-taken-other-family",
        ctxmod.default_directives(p[2], "127.0.0.1"), timeout=6)
    ctx.emit("refuse/port-taken-other-family/rc", rc4, adv=True)
    ctx.emit_text("refuse/port-taken-other-family/out", out4, adv=True)
    ctx.emit("refuse/port-taken-other-family/log", _log_lines_text(log4),
             adv=True)
    holder.stop()

    # -- configuration from argv rather than a file -------------------------
    d = ctx.workspace.sub("argv")
    rc, out = srv.run_tool(ctx.tree, "redis-server", ["--version"],
                           timeout=30, cwd=d)
    ctx.emit("argv/version-rc", rc)
    ctx.emit_text("argv/version-out", out)
    rc, out = srv.run_tool(ctx.tree, "redis-server", ["--help"], timeout=30)
    ctx.emit("argv/help-rc", rc, adv=True)
    ctx.emit_text("argv/help-out", out, adv=True)
    rc, out = srv.run_tool(ctx.tree, "redis-server",
                           ["/nonexistent/redis.conf"], timeout=30)
    ctx.emit("argv/missing-conf-rc", rc, adv=True)
    ctx.emit_text("argv/missing-conf-out", out, adv=True)

    # A bind given on the command line overrides the one in the file.
    _case(ctx, "argv-override", p[4],
          ctxmod.default_directives(p[4], "127.0.0.1"), both,
          extra_argv=["--bind", "::1"], adv=True)
