"""Minimal RESP2/RESP3 client used by the grader to drive the candidate server."""
import socket, time


class RespError(Exception):
    pass


class Resp:
    def __init__(self, host="127.0.0.1", port=6379, unix=None, timeout=30.0):
        if unix:
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.settimeout(timeout)
            self.sock.connect(unix)
        else:
            self.sock = socket.create_connection((host, port), timeout=timeout)
            self.sock.settimeout(timeout)
        self.buf = b""

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def _fill(self):
        chunk = self.sock.recv(65536)
        if not chunk:
            raise RespError("connection closed")
        self.buf += chunk

    def _line(self):
        while b"\r\n" not in self.buf:
            self._fill()
        i = self.buf.index(b"\r\n")
        line, self.buf = self.buf[:i], self.buf[i + 2:]
        return line

    def _read(self, n):
        while len(self.buf) < n:
            self._fill()
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def read_reply(self):
        line = self._line()
        t, rest = line[:1], line[1:]
        if t == b"+":
            return rest
        if t == b"-":
            raise RespError(rest.decode("latin-1"))
        if t == b":":
            return int(rest)
        if t == b"$":
            n = int(rest)
            if n == -1:
                return None
            data = self._read(n)
            self._read(2)
            return data
        if t == b"=":
            n = int(rest)
            data = self._read(n)
            self._read(2)
            return data
        if t in (b"*", b"~", b">"):
            n = int(rest)
            if n == -1:
                return None
            return [self.read_reply() for _ in range(n)]
        if t == b"%":
            n = int(rest)
            out = []
            for _ in range(n):
                out.append(self.read_reply())
                out.append(self.read_reply())
            return out
        if t == b"_":
            return None
        if t == b"#":
            return rest == b"t"
        if t == b",":
            return float(rest)
        if t == b"(":
            return int(rest)
        raise RespError("unknown reply type %r" % t)

    def cmd(self, *args):
        out = b"*%d\r\n" % len(args)
        for a in args:
            if isinstance(a, str):
                a = a.encode("latin-1")
            elif isinstance(a, int):
                a = str(a).encode()
            out += b"$%d\r\n%s\r\n" % (len(a), a)
        self.sock.sendall(out)
        return self.read_reply()


def wait_ready(host, port, deadline, unix=None):
    """Poll until PING answers or the deadline passes."""
    while time.time() < deadline:
        try:
            r = Resp(host, port, unix=unix, timeout=2.0)
            try:
                if r.cmd("PING") in (b"PONG", "PONG"):
                    return r
            finally:
                pass
            r.close()
        except (OSError, RespError):
            time.sleep(0.1)
    return None
