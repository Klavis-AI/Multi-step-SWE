"""An address that swallows connection attempts instead of refusing them.

Step 2 is about what a *blackholed* address costs. An address that refuses is
no test of it: a refusal comes back in microseconds, so a caller that walks its
addresses one at a time walks past it as fast as a caller that races them, and
the two are indistinguishable. The difference only appears against an address
that answers nothing at all -- there the sequential caller sits inside one
`connect()` until the kernel gives up, and the racer has the other family's
attempt running beside it a Connection Attempt Delay later.

The obvious way to produce one is a firewall rule, and this container has
neither `CAP_NET_ADMIN` nor a reachable address that is not on the host. What
it does have is the accept queue. A socket
that has been `bind()`-ed and `listen()`-ed has a port in the LISTEN state, so
nothing sends a RST; and one whose accept queue is full has nowhere to put an
arriving connection, so the kernel drops the SYN instead. `listen(0)` on this
kernel is full from the start -- measured, not assumed: `fill()` below parks
connections until a further attempt hangs and it parks none, because the first
attempt already hangs. `fill()` is a loop anyway so that a kernel which sized
the queue differently still produces a blackhole rather than a quietly open
port. Nothing about this is address-family specific: it works for `::1`, for
`127.0.0.1` and for the rest of 127/8.

Because the whole scenario reads "did this hang", the instrument is verified
before the finding: `controls()` measures a live address, an unbound one
and a darkened one in the same run with the same probe, and a scenario emits
those three answers beside its own. A blackhole that quietly stopped working
would otherwise turn every "hung" observation into "connected" and read as a
tree that had done the work.
"""

import errno
import socket

# How many connections `fill()` is willing to park before it gives up. One is
# always enough for `listen(0)` on Linux -- the queue is full at one completed
# connection -- and the loop exists so that a kernel which sized the queue
# differently produces a working blackhole rather than a silently open port.
PARK_LIMIT = 8

# What a probe waits before calling an attempt hung. A loopback connection that
# is going to complete completes in well under a millisecond, so this is three
# orders of magnitude of headroom; it is not a measurement, only the difference
# between "answered" and "did not".
PROBE_TIMEOUT = 0.5


def _family(host, v6=None):
    if v6 is None:
        v6 = ":" in host
    return socket.AF_INET6 if v6 else socket.AF_INET


def probe(host, port, timeout=PROBE_TIMEOUT, v6=None):
    """One word for what a bare TCP connection attempt to this address did.

    The same vocabulary `d01._probe` uses, and for the same reason: the `errno`
    name is the fact, and the C library's wording for it is not.
    """
    s = socket.socket(_family(host, v6), socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((host, port))
        return "connected"
    except socket.timeout:
        return "timeout"
    except OSError as e:
        return errno.errorcode.get(e.errno, str(e.errno))
    finally:
        s.close()


class Dark:
    """`host`:`port` darkened for the lifetime of the context manager.

    The listening socket and the parked connections are held by the scenario's
    own process, so they survive across every tool the scenario starts and go
    away when it exits -- including when it exits badly, which is what keeps a
    crashed scenario from leaving a black hole behind for the next one.
    """

    def __init__(self, host, port, v6=None):
        self.host = host
        self.port = port
        self.af = _family(host, v6)
        self.sock = None
        self.parked = []
        self.dark = False

    def __enter__(self):
        self.sock = socket.socket(self.af, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if self.af == socket.AF_INET6:
            # Without this the IPv6 listener would also answer for 127.0.0.1,
            # and darkening `::1` would darken an address the scenario meant to
            # leave alone -- or, worse, collide with the server it put there.
            self.sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        self.sock.bind((self.host, self.port))
        self.sock.listen(0)
        self.fill()
        return self

    def fill(self):
        """Park connections until a further attempt hangs. Idempotent."""
        for _ in range(PARK_LIMIT):
            if probe(self.host, self.port, v6=self.af == socket.AF_INET6) \
                    == "timeout":
                self.dark = True
                return True
            c = socket.socket(self.af, socket.SOCK_STREAM)
            c.settimeout(2.0)
            try:
                c.connect((self.host, self.port))
            except OSError:
                c.close()
                break
            self.parked.append(c)
        self.dark = probe(self.host, self.port,
                          v6=self.af == socket.AF_INET6) == "timeout"
        return self.dark

    def __exit__(self, *a):
        self.close()
        return False

    def close(self):
        for c in self.parked:
            try:
                c.close()
            except OSError:
                pass
        self.parked = []
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None


class Blackhole:
    """Several addresses darkened together, entered and left as one.

    Scenarios darken the head of each family at once -- that is the shape the
    step is about -- so this is the usual entry point rather than `Dark`.
    """

    def __init__(self, addrs, port):
        self.addrs = list(addrs)
        self.port = port
        self.holes = []

    def __enter__(self):
        try:
            for host in self.addrs:
                self.holes.append(Dark(host, self.port).__enter__())
        except Exception:
            self.__exit__()
            raise
        return self

    def __exit__(self, *a):
        for h in reversed(self.holes):
            h.close()
        self.holes = []
        return False

    @property
    def all_dark(self):
        return bool(self.holes) and all(h.dark for h in self.holes)

    @property
    def parked(self):
        """How many connections it took to darken these, summed.

        Zero on this kernel. It is emitted rather than asserted because a
        non-zero value is not a failure -- the blackhole still works -- and a
        recording where it changed is worth seeing.
        """
        return sum(len(h.parked) for h in self.holes)


def controls(port, host="127.99.99.9"):
    """The instrument's three known answers, measured now, on this host.

    A scenario emits these beside what it measured, so that "this attempt hung"
    is read against a probe shown in the same run to report a live address as
    live and an unbound one as refused. Everything here is the harness talking
    to itself -- no tree is involved -- so the three words are the same at every
    step, and a recording where they are not is a broken blackhole rather than a
    tree that changed.
    """
    out = {"unbound": probe(host, port, v6=False)}
    live = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    live.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        live.bind((host, port))
        live.listen(5)
        out["open"] = probe(host, port, v6=False)
    finally:
        live.close()
    with Dark(host, port, v6=False) as d:
        out["dark"] = probe(host, port, v6=False)
        out["parked"] = len(d.parked)
    return out
