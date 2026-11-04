"""Minimal dependency-free RESP client.

This is adapter code: it drives the tree's own protocol and records whatever
came back. It never contains an expected value.
"""

import socket
import time


class RespError(Exception):
    pass


class Timeout(Exception):
    pass


def encode_command(args):
    """Encode args (bytes/str/int) as a RESP array."""
    out = [b"*%d\r\n" % len(args)]
    for a in args:
        if isinstance(a, str):
            a = a.encode("utf-8", "surrogateescape")
        elif isinstance(a, int):
            a = str(a).encode()
        elif not isinstance(a, (bytes, bytearray)):
            raise TypeError("unsupported argument type %r" % type(a))
        out.append(b"$%d\r\n" % len(a))
        out.append(bytes(a))
        out.append(b"\r\n")
    return b"".join(out)


class Reader:
    """Incremental RESP reply reader over a blocking socket."""

    def __init__(self, sock, timeout=10.0):
        self.sock = sock
        self.buf = b""
        self.timeout = timeout

    def _fill(self):
        self.sock.settimeout(self.timeout)
        try:
            chunk = self.sock.recv(65536)
        except socket.timeout:
            raise Timeout("no reply within %.1fs" % self.timeout)
        if not chunk:
            raise Timeout("connection closed by peer")
        self.buf += chunk

    def _line(self):
        while b"\r\n" not in self.buf:
            self._fill()
        line, self.buf = self.buf.split(b"\r\n", 1)
        return line

    def _exact(self, n):
        while len(self.buf) < n + 2:
            self._fill()
        data = self.buf[:n]
        self.buf = self.buf[n + 2:]
        return data

    def read(self):
        line = self._line()
        if not line:
            raise RespError("empty reply line")
        kind, rest = line[:1], line[1:]
        if kind == b"+":
            return ("status", rest)
        if kind == b"-":
            return ("error", rest)
        if kind == b":":
            return ("int", int(rest))
        if kind == b"$":
            n = int(rest)
            if n == -1:
                return ("nil", None)
            return ("bulk", self._exact(n))
        if kind == b"*":
            n = int(rest)
            if n == -1:
                return ("nilarray", None)
            return ("array", [self.read() for _ in range(n)])
        raise RespError("unknown reply type %r in %r" % (kind, line))


class Client:
    """One connection to a server, speaking RESP."""

    def __init__(self, host="::1", port=6379, unixsocket=None, timeout=10.0,
                 family=socket.AF_UNSPEC):
        self.timeout = timeout
        if unixsocket:
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.settimeout(timeout)
            self.sock.connect(unixsocket)
            self.peer = ("unix", unixsocket)
        else:
            infos = socket.getaddrinfo(host, port, family, socket.SOCK_STREAM)
            last = None
            for af, st, proto, _, sa in infos:
                try:
                    s = socket.socket(af, st, proto)
                    s.settimeout(timeout)
                    s.connect(sa)
                    self.sock = s
                    self.peer = sa
                    break
                except OSError as e:
                    last = e
                    try:
                        s.close()
                    except Exception:
                        pass
            else:
                raise last if last else OSError("no address for %r" % (host,))
        if self.sock.family != socket.AF_UNIX:
            # `TCP_NODELAY` is a TCP option and a Unix-domain socket answers
            # ENOTSUP to it, which surfaces as a connection that could not be
            # made rather than as an option that could not be set.
            self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.reader = Reader(self.sock, timeout)

    def send_raw(self, data):
        self.sock.sendall(data)

    def cmd(self, *args):
        self.sock.sendall(encode_command(args))
        return self.reader.read()

    def cmd_raw(self, data, nreplies=1):
        """Send arbitrary bytes and read nreplies replies."""
        self.sock.sendall(data)
        return [self.reader.read() for _ in range(nreplies)]

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


def wait_ready(host, port, unixsocket=None, deadline=20.0, family=socket.AF_UNSPEC):
    """Poll until the server answers PING, or raise."""
    end = time.time() + deadline
    last = None
    while time.time() < end:
        try:
            with Client(host, port, unixsocket=unixsocket, timeout=1.0,
                        family=family) as c:
                kind, val = c.cmd("PING")
                if kind == "status" and val == b"PONG":
                    return True
                if kind == "error":
                    # LOADING / MASTERDOWN etc: the server is up and talking.
                    return True
        except Exception as e:  # noqa: BLE001 - polling a starting server
            last = e
        time.sleep(0.05)
    raise Timeout("server at %s:%s never became ready (%s)" % (host, port, last))
