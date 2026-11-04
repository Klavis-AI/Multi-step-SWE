"""The peer addresses this tree learns from other machines rather than from an operator.

Step 5's instruction names the places where this repository is handed an
address that names another machine. `d05_scoped` asks the ones an operator
reaches: `CLUSTER MEET`, `SLAVEOF`, `SENTINEL MONITOR` and the
`sentinel monitor` directive. This scenario asks the other three, where the
address comes off the wire:

  * the addresses the cluster learns from gossip, when a node it has met tells
    it about nodes it has not;
  * the replicas Sentinel learns from a master's `INFO`;
  * the Sentinels it learns from hello messages on the master's
    `__sentinel__:hello` channel.

What is refused there is what `d05_scoped` refuses everywhere else: a multicast
group, the unspecified address, an IPv4-compatible address, and a mapped
address whose embedded IPv4 address is a group. On these paths there is no
caller to answer, so the instruction's "nothing is created or changed" is the
whole of what is asked. Each cell is one boolean: did the tree take this
address into its table.

**Every refusal comes with a control on the same path.** An accepted (loopback)
address goes through the same path, and the scenario waits for it before
reading anything else. For `INFO` and hello it is in the same exchange as the
refused ones and comes after them. For gossip it comes from a second sender, and
the window for the refused rows opens only once the first sender is a node the
learner knows. Without that, a tree that refused the address and a tree that
never read the message would look the same.

**Each announcement is in this tree's own format, or comes from this tree's
own code:**

  * Gossip comes from a node of the tree under test. Its node table holds the
    four addresses because its own `nodes.conf` does: a file it saved itself,
    with the host part of four rows rewritten in the bracket style the file
    already uses. A peer that refuses these addresses can only hold one through
    its file. A tree that refuses the file instead, and will not start with it,
    gossips nothing, and its refusal cells pass without being tested. That case
    is accepted because the instruction does not say what a node file naming a
    group must do. The control is a separate pair of nodes, so that case cannot
    cost a tree its control.
  * The `INFO` comes from a stand-in master. Its replica lines are copied from
    a line this tree's own master wrote about a real replica, with only the
    replica's index, address and port replaced.
  * The hello messages are published on a real master of this tree, in the
    eight-field format `sentinel.c` writes. The ports and run ids in them are
    the scenario's own, so each entry can be found again by port or run id,
    however the tree spells the address.

Ports are matched rather than addresses, for the same reason: a learned address
that a tree has rewritten into its canonical spelling is still the address it
learned.
"""

import hashlib
import os
import re
import socket
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import resp  # noqa: E402
import srv  # noqa: E402

PORTS = 22
FAMILY = "behaviour"

HOST = "::1"
MASTER = "mymaster"
LEARNER = "learner"
HELLO = "__sentinel__:hello"

# See d05_scoped.NODE_TIMEOUT: a learned handshake node stays for this long, and
# nothing here waits that long. Down-detection is kept out of the window too.
NODE_TIMEOUT = 60000

# One per class, plus the mapped spelling of a group: a mapped address is
# classified as the address it embeds.
REFUSED = (
    ("multicast", "ff02::1"),
    ("unspecified", "::"),
    ("v4compat", "::127.0.0.1"),
    ("mapped-multicast", "::ffff:224.0.0.1"),
)

# How long a path is watched after its control arrived. Only an accepting tree
# can end the wait early, by taking every refused address, so a refusing tree
# always costs the full window, and only on the gossip path.
GOSSIP_WINDOW = 15.0
DEADLINE = 20.0


def _client(port, timeout=10.0):
    try:
        return resp.Client(HOST, port, timeout=timeout)
    except Exception:
        return None


def _say(client, *args):
    if client is None:
        return ("error", b"harness: no-client")
    try:
        return client.cmd(*args)
    except Exception as e:
        return ("error", ("harness: %s" % type(e).__name__).encode())


def _text(v):
    return v.decode("utf-8", "replace") if isinstance(v, bytes) else ""


def _runid(label):
    return hashlib.sha1(("d05-learned/%s" % label).encode("ascii")).hexdigest()


def _wait(pred, deadline):
    end = time.time() + deadline
    while True:
        v = pred()
        if v or time.time() >= end:
            return v
        time.sleep(0.1)


def _start(server):
    """Start a server; False, rather than an exception, if it will not come up."""
    try:
        server.start().wait_ready()
        return True
    except Exception:
        return False


# -- cluster ---------------------------------------------------------------
def _cluster_directives(port, timeout=NODE_TIMEOUT):
    return ctxmod.default_directives(port, HOST, extra=[
        "cluster-enabled yes",
        "cluster-config-file nodes.conf",
        "cluster-node-timeout %d" % timeout,
    ])


# The gossiping node's own timeout once it is restarted on the rewritten file.
# Its four peers are gone by then, so a second later it holds them as PFAIL, and
# a node gossips its PFAIL peers ahead of the rest (`clusterSendPing`'s bias).
# Every refused row then reaches the learner in the first packets instead of by
# a random draw among the table. The learner keeps NODE_TIMEOUT, so a row it
# did take stays in its table for as long as this scenario looks.
REF_TIMEOUT = 1000


def _port_of(field):
    """The port of a `CLUSTER NODES` / `nodes.conf` address field, or None."""
    host, sep, port = field.rpartition(":")
    try:
        return int(port) if sep else None
    except ValueError:
        return None


def _rows(client):
    """`CLUSTER NODES` as (port, flags) pairs, one per row."""
    r = _say(client, "CLUSTER", "NODES")
    if r[0] != "bulk" or r[1] is None:
        return []
    out = []
    for line in _text(r[1]).splitlines():
        parts = line.split()
        if len(parts) >= 3 and len(parts[0]) == 40:
            out.append((_port_of(parts[1]), parts[2], len(parts) > 8))
    return out


def _ports(client):
    return set(p for p, _f, _s in _rows(client))


def _known(client, port):
    """Is the node at `port` in the table and past its handshake?"""
    return any(p == port and "handshake" not in f for p, f, _s in _rows(client))


def _rehost(field, host):
    """`field` with its host part replaced, keeping the bracket style."""
    if field.startswith("["):
        close = field.find("]")
        if close < 0:
            return None
        return "[" + host + "]" + field[close + 1:]
    _old, sep, port = field.rpartition(":")
    return host + sep + port if sep else None


def _gossip(ctx):
    """A node learns of four nodes it has not met, from a node that has.

    Phase one gives the gossiping node `ref` four real peers, each holding one
    slot. A node with no link and no slots is never gossiped, and these peers
    will be gone when phase two starts. `ref` saves its table, everything stops,
    and the host part of the four rows in `ref`'s own `nodes.conf` is rewritten
    to the four refused addresses. Phase two restarts `ref` on that file, so its
    table holds four slot-owning nodes at addresses this scenario chose, written
    in the tree's own format. It restarts with REF_TIMEOUT, so those four are
    the rows it gossips first. A fresh node, `learner`, then meets `ref`, plus a
    second pair (`ctl` and its live peer `peer`) whose gossip about `peer` is
    the control.
    """
    ref_p = ctx.port(14)
    hold = [ctx.port(15 + i) for i in range(len(REFUSED))]
    ctl_p, peer_p, learner_p = ctx.port(19), ctx.port(20), ctx.port(21)

    ref = ctx.server("gossip-ref", ref_p, _cluster_directives(ref_p))
    holders = [ctx.server("gossip-hold%d" % i, p, _cluster_directives(p))
               for i, p in enumerate(hold)]
    ref_up = _start(ref)
    rc = _client(ref_p)
    for i, h in enumerate(holders):
        if _start(h):
            hc = _client(hold[i])
            _say(hc, "CLUSTER", "ADDSLOTS", str(1000 + i))
            if hc:
                hc.close()
        _say(rc, "CLUSTER", "MEET", HOST, str(hold[i]))

    def _ref_holds_all():
        rows = _rows(rc)
        return all(any(p == hp and "handshake" not in f and slots
                       for p, f, slots in rows) for hp in hold)
    held = ref_up and bool(_wait(_ref_holds_all, DEADLINE))
    _say(rc, "CLUSTER", "SAVECONFIG")
    if rc:
        rc.close()
    ref.stop()
    for h in holders:
        h.stop()

    # The rewrite: four rows, host part only, in the style the file uses. The
    # directory is the score uid's, so the file is read O_NOFOLLOW and written
    # back as a new file (`srv.read_regular`, `srv.write_new`), never through
    # whatever stands at the path.
    conf = os.path.join(ref.dir, "nodes.conf")
    rewritten = 0
    raw = srv.read_regular(conf)
    try:
        if raw is None:
            raise OSError("no regular nodes.conf")
        lines = raw.decode("utf-8", "replace").splitlines()
        out = []
        for line in lines:
            parts = line.split(" ")
            if len(parts) >= 3 and len(parts[0]) == 40:
                port = _port_of(parts[1])
                if port in hold:
                    new = _rehost(parts[1], REFUSED[hold.index(port)][1])
                    if new is not None:
                        parts[1] = new
                        rewritten += 1
            out.append(" ".join(parts))
        srv.write_new(conf, "\n".join(out) + "\n")
    except OSError:
        pass
    ctx.note("gossip: ref held %s, %d row(s) rewritten" % (held, rewritten))

    # Phase two.
    ctl = ctx.server("gossip-ctl", ctl_p, _cluster_directives(ctl_p))
    peer = ctx.server("gossip-peer", peer_p, _cluster_directives(peer_p))
    _start(ctl)
    _start(peer)
    cc = _client(ctl_p)
    _say(cc, "CLUSTER", "MEET", HOST, str(peer_p))
    if cc:
        cc.close()

    again = ctx.server("gossip-ref-again", ref_p,
                       _cluster_directives(ref_p, REF_TIMEOUT), datadir=ref.dir)
    again_up = _start(again)

    learner = ctx.server("gossip-learner", learner_p,
                         _cluster_directives(learner_p))
    _start(learner)
    lc = _client(learner_p)
    _say(lc, "CLUSTER", "MEET", HOST, str(ctl_p))
    _say(lc, "CLUSTER", "MEET", HOST, str(ref_p))

    control = bool(_wait(lambda: peer_p in _ports(lc), DEADLINE))
    # The window opens once `ref` is a node `learner` knows: gossip from a
    # sender still in handshake is not read.
    if again_up:
        _wait(lambda: _known(lc, ref_p), DEADLINE)
    first = {}
    t0 = time.time()
    while True:
        seen = _ports(lc)
        for p in hold:
            if p in seen and p not in first:
                first[p] = round(time.time() - t0, 1)
        if set(hold) <= seen or time.time() - t0 >= GOSSIP_WINDOW:
            break
        time.sleep(0.1)
    ctx.note("gossip: ref restarted %s, control %s, first seen %s"
             % (again_up, control, sorted(first.values())))

    ctx.emit("cluster/gossip/control/learned", control)
    for (label, _addr), port in zip(REFUSED, hold):
        ctx.emit("cluster/gossip/refuse/%s/learned" % label, port in seen,
                 adv=True)
    if lc:
        lc.close()
    for s in (learner, again, peer, ctl):
        s.stop()


# -- Sentinel: a stand-in master's INFO --------------------------------------
_ADDR_RUN = re.compile(r"\[?[0-9A-Fa-f:.%]+\]?")


def _packed(text):
    base = text.strip("[]").partition("%")[0]
    for fam in (socket.AF_INET6, socket.AF_INET):
        try:
            return socket.inet_pton(fam, base)
        except (OSError, ValueError):
            continue
    return None


def _replica_template(line, ip_bytes, port):
    """A function writing `line` about another replica, or None.

    `line` is what this tree's master wrote about a real replica at `ip_bytes`
    and `port`. The address is found by value and the port by its digits, and
    each is replaced in place, so the field names, the separators and their
    order are whatever the tree's own master writes.
    """
    m = re.match(r"(slave)(\d+)(.*)$", line)
    if not m:
        return None
    rest = m.group(3)
    span = None
    for r in _ADDR_RUN.finditer(rest):
        tok = r.group(0)
        if tok.endswith(":") and len(tok) > 1 and _packed(tok[:-1]) == ip_bytes:
            span = (r.start(), r.end() - 1)
        elif _packed(tok) == ip_bytes:
            span = (r.start(), r.end())
        if span:
            break
    pm = None
    for r in re.finditer(r"(?<![0-9])%d(?![0-9])" % port, rest):
        if span is None or r.start() >= span[1] or r.end() <= span[0]:
            pm = r
            break
    if span is None or pm is None:
        return None
    inner = rest[span[0]:span[1]]
    bracketed = inner.startswith("[")

    def write(index, addr, new_port):
        a = "[%s]" % addr if bracketed else addr
        pieces = sorted([(span[0], span[1], a),
                         (pm.start(), pm.end(), str(new_port))], reverse=True)
        out = rest
        for s, e, t in pieces:
            out = out[:s] + t + out[e:]
        return "slave%d%s" % (index, out)
    return write


def _default_template(index, addr, new_port):
    return "slave%d:ip=%s,port=%d,state=online,offset=0,lag=0" % (
        index, addr, new_port)


class _StandIn:
    """A master that answers what Sentinel asks of one, and nothing else."""

    def __init__(self, port, info):
        self.info = info.encode()
        self.sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((HOST, port))
        self.sock.listen(16)
        self.conns = []
        self.done = False
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while not self.done:
            try:
                c, _a = self.sock.accept()
            except OSError:
                return
            self.conns.append(c)
            threading.Thread(target=self._serve, args=(c,), daemon=True).start()

    @staticmethod
    def _bulk(b):
        return b"$%d\r\n%s\r\n" % (len(b), b)

    def _reply(self, args):
        cmd = args[0].upper() if args else b""
        if cmd == b"PING":
            return b"+PONG\r\n"
        if cmd == b"INFO":
            return self._bulk(self.info)
        if cmd == b"SUBSCRIBE":
            return b"".join(b"*3\r\n" + self._bulk(b"subscribe") +
                            self._bulk(ch) + b":%d\r\n" % (i + 1)
                            for i, ch in enumerate(args[1:]))
        if cmd == b"PUBLISH":
            return b":0\r\n"
        return b"+OK\r\n"

    def _serve(self, c):
        buf = b""
        try:
            while not self.done:
                data = c.recv(65536)
                if not data:
                    return
                buf += data
                while True:
                    args, buf2 = self._parse(buf)
                    if args is None:
                        break
                    buf = buf2
                    c.sendall(self._reply(args))
        except OSError:
            return

    @staticmethod
    def _parse(buf):
        """One command off the front of `buf`: (args, rest), or (None, buf)."""
        if not buf:
            return None, buf
        if buf[:1] != b"*":
            line, sep, rest = buf.partition(b"\r\n")
            return (line.split(), rest) if sep else (None, buf)
        head, sep, rest = buf.partition(b"\r\n")
        if not sep:
            return None, buf
        n = int(head[1:])
        args = []
        for _ in range(n):
            ln, sep, rest2 = rest.partition(b"\r\n")
            if not sep or ln[:1] != b"$":
                return None, buf
            size = int(ln[1:])
            if len(rest2) < size + 2:
                return None, buf
            args.append(rest2[:size])
            rest = rest2[size + 2:]
        return args, rest

    def close(self):
        self.done = True
        for c in [self.sock] + self.conns:
            try:
                c.close()
            except OSError:
                pass


def _instances(client, kind, name):
    """`SENTINEL <kind> <name>` as a list of field dicts."""
    r = _say(client, "SENTINEL", kind, name)
    if r[0] != "array" or not r[1]:
        return []
    out = []
    for entry in r[1]:
        if entry[0] != "array" or not entry[1]:
            continue
        f = entry[1]
        out.append(dict((_text(f[i][1]), _text(f[i + 1][1]))
                        for i in range(0, len(f) - 1, 2)))
    return out


def _has_port(entries, port):
    return any(e.get("port") == str(port) for e in entries)


def _has_runid(entries, runid):
    return any(e.get("runid") == runid for e in entries)


def _sentinel(ctx):
    mp, rp, sp, fp = ctx.port(0), ctx.port(1), ctx.port(2), ctx.port(3)
    hello_ports = [ctx.port(4 + i) for i in range(len(REFUSED))]
    hello_ctl = ctx.port(8)
    info_ports = [ctx.port(9 + i) for i in range(len(REFUSED))]
    info_ctl = ctx.port(13)

    master = ctx.server("master", mp, ctxmod.default_directives(mp, HOST))
    replica = ctx.server("replica", rp, ctxmod.default_directives(rp, HOST))
    _start(master)
    _start(replica)
    mc = _client(mp)
    rc = _client(rp)
    _say(rc, "SLAVEOF", HOST, str(mp))

    # A replica line in this tree's own words, for the stand-in to repeat.
    def _line():
        r = _say(mc, "INFO", "replication")
        if r[0] != "bulk" or r[1] is None:
            return None
        for l in _text(r[1]).splitlines():
            if re.match(r"slave\d+[^0-9]", l):
                return l.strip()
        return None
    line = _wait(_line, DEADLINE)
    write = _replica_template(line, _packed(HOST), rp) if line else None
    ctx.note("info template: %s" % ("tree's own" if write else "default"))
    if write is None:
        write = _default_template

    sent = ctx.server("sentinel", sp, [
        "bind %s" % HOST,
        "port %d" % sp,
        "dir .",
        "logfile sentinel.log",
        "daemonize no",
        "sentinel monitor %s %s %d 2" % (MASTER, HOST, mp),
        "sentinel down-after-milliseconds %s %d" % (MASTER, NODE_TIMEOUT),
    ], binary="redis-sentinel", logfile="sentinel.log")
    _start(sent)
    xc = _client(sp)

    # -- hello messages: the refused ones first, then the control, all on one
    # channel, so a Sentinel that has read the control has read the rest.
    def _subscribed():
        r = _say(mc, "PUBSUB", "NUMSUB", HELLO)
        try:
            return r[0] == "array" and int(r[1][1][1]) >= 1
        except Exception:
            return False
    _wait(_subscribed, DEADLINE)
    addr = _say(xc, "SENTINEL", "GET-MASTER-ADDR-BY-NAME", MASTER)
    try:
        mip, mport = _text(addr[1][0][1]), _text(addr[1][1][1])
    except Exception:
        mip, mport = HOST, str(mp)
    for (label, a), port in zip(REFUSED, hello_ports):
        _say(mc, "PUBLISH", HELLO, "%s,%d,%s,0,%s,%s,%s,0"
             % (a, port, _runid("hello/" + label), MASTER, mip, mport))
    _say(mc, "PUBLISH", HELLO, "%s,%d,%s,0,%s,%s,%s,0"
         % (HOST, hello_ctl, _runid("hello/control"), MASTER, mip, mport))
    control = bool(_wait(lambda: _has_runid(
        _instances(xc, "SENTINELS", MASTER), _runid("hello/control")), DEADLINE))
    seen = _instances(xc, "SENTINELS", MASTER)
    ctx.emit("sentinel/hello/control/learned", control)
    for (label, _a), port in zip(REFUSED, hello_ports):
        ctx.emit("sentinel/hello/refuse/%s/learned" % label,
                 _has_runid(seen, _runid("hello/" + label))
                 or _has_port(seen, port), adv=True)

    # -- a master's INFO: one reply, the refused replicas first, the control
    # last, so a Sentinel that has taken the control has read the rest.
    lines = [write(i, a, port)
             for i, ((_l, a), port) in enumerate(zip(REFUSED, info_ports))]
    lines.append(write(len(REFUSED), HOST, info_ctl))
    info = "\r\n".join([
        "# Server", "redis_version:3.1.999",
        "run_id:%s" % _runid("info/master"), "tcp_port:%d" % fp, "",
        "# Replication", "role:master",
        "connected_slaves:%d" % len(lines)] + lines +
        ["master_repl_offset:0", ""])
    standin = None
    try:
        standin = _StandIn(fp, info)
    except OSError as e:
        ctx.note("stand-in master did not start: %s" % e)
    _say(xc, "SENTINEL", "MONITOR", LEARNER, HOST, str(fp), "2")
    _say(xc, "SENTINEL", "SET", LEARNER, "down-after-milliseconds",
         str(NODE_TIMEOUT))
    control = bool(_wait(lambda: _has_port(
        _instances(xc, "SLAVES", LEARNER), info_ctl), DEADLINE))
    seen = _instances(xc, "SLAVES", LEARNER)
    ctx.emit("sentinel/info/control/learned", control)
    for (label, _a), port in zip(REFUSED, info_ports):
        ctx.emit("sentinel/info/refuse/%s/learned" % label,
                 _has_port(seen, port), adv=True)

    for c in (xc, mc, rc):
        if c:
            c.close()
    sent.stop()
    if standin:
        standin.close()
    replica.stop()
    master.stop()


def run(ctx):
    _sentinel(ctx)
    _gossip(ctx)
