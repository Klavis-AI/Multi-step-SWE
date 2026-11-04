"""Classification by value, asked with addresses no table of spellings contains.

`d05_scoped` asks step 5's question with the addresses a person would write
down: `ff02::1`, `::`, `::ffff:127.0.0.1`. Every one of them is in this
repository's own documentation, in the RFCs the step cites, and in the step
instruction, so a tree that carried a list of those strings and compared against
it would pass the whole of `d05_scoped` while having no notion of what an
address is. The step instruction says the opposite in as many words -- *"An
address this tree has never seen before is classified correctly the first time,
which a list of known spellings cannot do"* -- and this scenario is where that
sentence is scored.

Every address below is generated, from the prefix its class is *defined* by and
sixteen bytes of a hash of its own label. `2001:db8::1` is a spelling; the
global unicast addresses here are not, and neither are the multicast ones, and
no tree can have met them before. What makes them usable as a measurement is
that they are a pure function of the label -- `sha256("g07/<class>/<n>")` --
so the table is identical in every recording on every tree, and adding a class
or reordering one cannot renumber another one.

The classes are only the ones with room to generate in. `::` and `0.0.0.0` have
exactly one spelling each and are `d05_scoped`'s to score; there is nothing
generative about them.

Two halves:

  * **admission.** `SLAVEOF` is the cheapest of the three peer-admission
    surfaces -- one server, one command, and the answer is synchronous -- so it
    is the one the whole generated table is pushed through. What is emitted per
    address is the verdict word and not the message: the wording is
    `d05_scoped`'s to score, and a gate that also scored it would be scoring the
    same sentence twice.

  * **identity.** `CLUSTER MEET` counts. A generated IPv4 address and its mapped
    form are one machine, so the second MEET adds no node; two *different*
    generated addresses are two machines, so it adds one. Both directions are
    asked, because an unwrapping applied on one side of a comparison and not the
    other passes the first and fails the second.

The near-miss class is the anti-over-refusal control and it is the sharpest
thing here. `::1:2:3:4`, `::ffff:0:102:304` and `64:ff9b::102:304` all begin
with the text a deprecated IPv4-compatible address begins with, and none of
them is one: the first has a non-zero fifth group, the second is the
IPv4-translated form of RFC 6052 rather than the mapped form, and the third is
a NAT64 prefix. All three are ordinary global unicast and all three must be
accepted. `fec0::` is here for the same reason from the other side -- RFC 3879
withdrew site-local, so what is left at that prefix is global unicast, not a
class of its own and not something to refuse.

Nothing here waits on a link coming up or a cluster forming. Every address is
one nothing answers at, on purpose, and every observation is read out of the
bookkeeping the command changed, in the same call.
"""

import hashlib
import os
import socket
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import resp  # noqa: E402

PORTS = 4
FAMILY = "gate"

HOST = "::1"

# Long enough that no handshake entry the identity half creates is swept away
# between the MEET that created it and the `CLUSTER NODES` that counts it:
# `clusterCron` drops a node stuck in handshake after max(node-timeout, 1000)
# ms, and every address met below is one nothing answers at.
NODE_TIMEOUT = 60000


# -- the generator --------------------------------------------------------
def _stream(tag, n):
    """`n` deterministic bytes for `tag`.

    A pure function of the label rather than a position in a sequence, so the
    table is the same on every tree and in every process, and inserting a class
    cannot shift the addresses of the class after it.
    """
    out = b""
    i = 0
    while len(out) < n:
        out += hashlib.sha256(("g07/%s/%d" % (tag, i)).encode("ascii")).digest()
        i += 1
    return bytearray(out[:n])


def _v6(raw):
    return socket.inet_ntop(socket.AF_INET6, bytes(raw))


def _v4(raw):
    return "%d.%d.%d.%d" % (raw[0], raw[1], raw[2], raw[3])


# First octets that are ordinary global unicast in the IANA IPv4
# Special-Purpose Address Registry: no 0/8, no 10/8, no 100.64/10, no 127/8,
# no 169.254/16, no 172.16/12, no 192.168/16, no 224/4, no 240/4.
_GLOBAL4 = (1, 8, 11, 13, 17, 23, 34, 45, 52, 64, 72, 88,
            99, 104, 128, 140, 151, 160, 185, 198, 203, 217)


def _global4(tag):
    b = _stream(tag, 4)
    b[0] = _GLOBAL4[b[0] % len(_GLOBAL4)]
    return b


def _build():
    """The whole table: [(class, expected-verdict, address-text), ...]."""
    rows = []

    def add(cls, want, text):
        rows.append((cls, want, text))

    for i in range(6):
        b = _stream("multicast-v6/%d" % i, 16)
        b[0] = 0xff
        # The scope nibble is varied on purpose: a group is a group at every
        # scope, and a check written against `ff02::` alone would say so only
        # for one of them.
        b[1] = (b[1] & 0xf0) | (0x01, 0x02, 0x05, 0x08, 0x0e)[i % 5]
        add("multicast-v6", "refused", _v6(b))

    for i in range(4):
        b = _stream("multicast-v4/%d" % i, 4)
        b[0] = 224 + (b[0] & 0x0f)
        add("multicast-v4", "refused", _v4(b))
        add("multicast-mapped", "refused", "::ffff:" + _v4(b))

    for i in range(6):
        b = _stream("v4-compatible/%d" % i, 4)
        b[0] = 1 + (b[0] % 223)
        add("v4-compatible", "refused", "::" + _v4(b))

    for i in range(4):
        b = _stream("global-v6/%d" % i, 16)
        b[0] = 0x20 | (b[0] & 0x1f)          # 2000::/3
        add("global-v6", "accepted", _v6(b))

    for i in range(3):
        b = _stream("unique-local/%d" % i, 16)
        b[0] = 0xfd                          # fc00::/7, L=1
        add("unique-local", "accepted", _v6(b))

    # The other half of the prefix. RFC 4193 defines unique-local as seven bits
    # and assigns only L=1; L=0 is unassigned, not invalid, and an address
    # there is still unique-local unicast -- which the step says in as many
    # words, `fd00::1` and `fc00::1` together. A tree whose test is "begins
    # fd" agrees with the three rows above and not with these, so they are
    # separate rows rather than a wider mask on the same ones.
    for i in range(2):
        b = _stream("unique-local-l0/%d" % i, 16)
        b[0] = 0xfc                          # fc00::/7, L=0
        add("unique-local", "accepted", _v6(b))

    for i in range(3):
        b = _stream("link-local/%d" % i, 16)
        b[0] = 0xfe
        b[1] = 0x80 | (b[1] & 0x3f)          # fe80::/10
        add("link-local", "accepted", _v6(b))

    for i in range(3):
        b = _stream("loopback-v4/%d" % i, 4)
        b[0] = 127
        add("loopback-v4", "accepted", _v4(b))

    # These are accept rows on purpose, and they are the IPv4 half of the
    # anti-over-refusal control. The step's IPv4 list classifies 169.254/16 and
    # the RFC 1918 ranges without saying either is unfit to be a peer, because
    # neither is: they are ordinary unicast, and a great many real clusters are
    # entirely inside 10/8. A tree that read "an address that cannot name
    # another machine is refused" as "refuse everything that is not global
    # unicast" passes every refusal row in this scenario and fails these.
    ll4 = []
    for i in range(2):
        b = _stream("link-local-v4/%d" % i, 4)
        b[0] = 169
        b[1] = 254                           # 169.254/16, RFC 3927
        ll4.append(_v4(b))
        add("link-local-v4", "accepted", ll4[-1])

    # Mapped, so the same address and the same verdict. A classifier that
    # unwraps `::ffff:` before it classifies gets this one for free; one that
    # matches prefixes against text has to have thought of it.
    add("link-local-v4-mapped", "accepted", "::ffff:" + ll4[0])

    b = _stream("private-v4/10", 4)
    b[0] = 10                                # 10/8
    add("private-v4", "accepted", _v4(b))

    b = _stream("private-v4/172", 4)
    b[0] = 172
    b[1] = 16 + (b[1] & 0x0f)                # 172.16/12
    add("private-v4", "accepted", _v4(b))

    b = _stream("private-v4/192", 4)
    b[0] = 192
    b[1] = 168                               # 192.168/16
    add("private-v4", "accepted", _v4(b))

    for i in range(3):
        b = _global4("global-v4/%d" % i)
        add("global-v4", "accepted", _v4(b))
        add("global-mapped", "accepted", "::ffff:" + _v4(b))

    # The near misses, each one text that begins the way a refused address
    # begins and a value that is nothing of the kind.
    b = _stream("near-miss/embedded", 16)
    for i in range(8):
        b[i] = 0
    b[8] = 0x01                              # a non-zero fifth group: ::1:x:x:x
    add("near-miss", "accepted", _v6(b))

    for i in range(2):
        q = _global4("near-miss/translated/%d" % i)
        t = bytearray(16)
        t[8] = 0xff
        t[9] = 0xff
        t[12:16] = q                         # ::ffff:0:a.b.c.d, RFC 6052
        add("near-miss", "accepted", _v6(t))

        n = bytearray(16)
        n[0:6] = bytearray((0x00, 0x64, 0xff, 0x9b, 0x00, 0x00))
        n[12:16] = q                         # 64:ff9b::/96, NAT64
        add("near-miss", "accepted", _v6(n))

    for i in range(2):
        b = _stream("former-site-local/%d" % i, 16)
        b[0] = 0xfe
        b[1] = 0xc0 | (b[1] & 0x0f)          # fec0::/10, withdrawn by RFC 3879
        add("near-miss", "accepted", _v6(b))

    return tuple(rows)


TABLE = _build()
assert len(set((c, a) for c, _w, a in TABLE)) == len(TABLE), \
    "two generated addresses share a cell name"

# The nine generated IPv4 addresses `_identity` meets, all at one port. The
# counts there are only meaningful if these are nine machines, so that is
# asserted at import rather than discovered as an off-by-one in a recording.
IDENTITY4 = (
    tuple(_v4(_global4("identity/%d" % i)) for i in range(3)) +
    tuple(_v4(_global4("identity-distinct/%d" % i)) for i in range(3)) +
    tuple(_v4(_global4("identity-distinct-mapped/%d" % i)) for i in range(3))
)
assert len(set(IDENTITY4)) == 9, "generated identity addresses collide"


# -- reading replies ------------------------------------------------------
def _client(port, timeout=10.0):
    try:
        return resp.Client(HOST, port, timeout=timeout)
    except Exception:
        return None


def _verdict(client, *args):
    """`accepted`, `refused`, or a marker -- never an exception.

    An address-handling change that made a server stop answering is a finding,
    and a scenario that died on it would report nothing at all.
    """
    if client is None:
        return "harness: no-client"
    try:
        kind, _val = client.cmd(*args)
    except Exception as e:
        return "harness: %s" % type(e).__name__
    if kind == "error":
        return "refused"
    return "accepted"


def _role(client):
    """`INFO replication`'s own word for what this server is, or a marker.

    Not the reply to `SLAVEOF NO ONE`, which is `+OK` on every tree whatever the
    server was doing: a control has to be read from something the server would
    have to change to make it wrong.
    """
    if client is None:
        return "harness: no-client"
    try:
        r = client.cmd("INFO", "replication")
    except Exception as e:
        return "harness: %s" % type(e).__name__
    if r[0] != "bulk" or r[1] is None:
        return "harness: no-info"
    for line in r[1].decode("utf-8", "replace").splitlines():
        if line.startswith("role:"):
            return line.split(":", 1)[1].strip()
    return "harness: no-role"


def _node_count(client):
    """How many rows `CLUSTER NODES` has, or -1 if it could not be read."""
    if client is None:
        return -1
    try:
        r = client.cmd("CLUSTER", "NODES")
    except Exception:
        return -1
    if r[0] != "bulk" or r[1] is None:
        return -1
    n = 0
    for line in r[1].decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) >= 3 and len(parts[0]) == 40:
            n += 1
    return n


# -- 1. admission ---------------------------------------------------------
def _admission(ctx):
    p = ctx.port(0)
    target = ctx.port(3)          # a port nothing is listening on, ever
    s = ctx.server("classes", p, ctxmod.default_directives(p, HOST))
    s.start().wait_ready()
    c = _client(p)

    # The instrument's own answer key, in the recording. If the generator ever
    # changes, this cell moves before any verdict does, and every verdict below
    # is then reporting on a different question than the one it was calibrated
    # against. The instrument is verified before the finding.
    ctx.emit("gate/class/table", ["%s %s %s" % row for row in TABLE])
    ctx.emit("gate/class/table-digest",
             hashlib.sha256("\n".join(
                 "%s %s %s" % row for row in TABLE).encode("ascii")
             ).hexdigest()[:16])

    counts = {}
    for cls, want, addr in TABLE:
        v = _verdict(c, "SLAVEOF", addr, str(target))
        ctx.emit("gate/class/admit/%s/%s" % (cls, addr), v,
                 adv=(want == "refused"))
        counts[v] = counts.get(v, 0) + 1
        # Whatever the verdict, this server must not be left following the
        # address it was just handed: the next address is asked of a master.
        _verdict(c, "SLAVEOF", "NO", "ONE")

    ctx.emit("gate/class/admit/counts", sorted(counts.items()))
    # Read back rather than assumed. Forty-six of the loop's addresses were
    # handed to SLAVEOF and every one of them was followed by a SLAVEOF NO ONE,
    # so if the class check were ever placed *before* the `NO ONE` branch of
    # slaveofCommand() the server would be stuck following something and the
    # forty-six verdicts above would be forty-six readings of a replica. `role`
    # is the field the server would have to be wrong about for that to go
    # unnoticed; the reply to `SLAVEOF NO ONE` is `+OK` either way.
    ctx.emit("gate/class/admit/left-as-master", _role(c))

    if c:
        c.close()
    s.stop()


# -- 2. identity ----------------------------------------------------------
def _identity(ctx):
    """A mapped address and the address it embeds are one node, both ways round.

    The count is read as a *delta* per pair rather than as a running total, so
    one pair getting the wrong answer does not move the answer of every pair
    after it.
    """
    p = ctx.port(1)
    target = ctx.port(2)          # nothing listens here either
    node = ctx.server("classes-cluster", p, ctxmod.default_directives(
        p, HOST, extra=[
            "cluster-enabled yes",
            "cluster-config-file nodes.conf",
            "cluster-node-timeout %d" % NODE_TIMEOUT,
        ]))
    node.start().wait_ready()
    c = _client(p)

    ctx.emit("gate/class/identity/start-nodes", _node_count(c))

    pairs = [(a, "::ffff:" + a) for a in IDENTITY4[0:3]]

    for i, (plain, mapped) in enumerate(pairs):
        first, second = (plain, mapped) if i % 2 == 0 else (mapped, plain)
        before = _node_count(c)
        ctx.emit("gate/class/identity/%d/first-reply" % i,
                 _verdict(c, "CLUSTER", "MEET", first, str(target)))
        mid = _node_count(c)
        ctx.emit("gate/class/identity/%d/first-added" % i, mid - before)
        ctx.emit("gate/class/identity/%d/second-reply" % i,
                 _verdict(c, "CLUSTER", "MEET", second, str(target)),
                 adv=True)
        ctx.emit("gate/class/identity/%d/second-added" % i,
                 _node_count(c) - mid, adv=True)
        ctx.emit("gate/class/identity/%d/order" % i,
                 "mapped-first" if i % 2 else "plain-first")

    # The controls, and they are what stops "one node" being reached by
    # deduplicating everything. Three addresses that are three machines stay
    # three machines, and so do three *mapped* addresses -- an unwrapping that
    # threw the embedded address away rather than reading it would pass the
    # half above and collapse these to one. Same target port throughout, so
    # what is being counted is the address and nothing else, and every port
    # this scenario names is inside its own allotted block.
    before = _node_count(c)
    for a in IDENTITY4[3:6]:
        _verdict(c, "CLUSTER", "MEET", a, str(target))
    ctx.emit("gate/class/identity/distinct-added", _node_count(c) - before)

    before = _node_count(c)
    for a in IDENTITY4[6:9]:
        _verdict(c, "CLUSTER", "MEET", "::ffff:" + a, str(target))
    ctx.emit("gate/class/identity/distinct-mapped-added",
             _node_count(c) - before)

    if c:
        c.close()
    node.stop()


def run(ctx):
    _admission(ctx)
    _identity(ctx)
