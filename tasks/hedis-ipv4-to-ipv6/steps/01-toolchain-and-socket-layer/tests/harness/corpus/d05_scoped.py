"""What an address *is*, asked of the three places this tree is told where a peer is.

Step 5's requirement has two halves and this scenario asks both of them on
`CLUSTER MEET`, `SLAVEOF` and `SENTINEL MONITOR` -- the commands, and for
Sentinel the configuration file as well, because a refusal that only a command
handler knows about is a refusal half the tree does not have.

  * **A peer is named by a unicast address.** A multicast address names a group,
    the unspecified address names nothing, and an IPv4-compatible address
    (RFC 4291 section 2.5.5.1) was deprecated and names nothing either. Each is
    refused, with an error the caller reads, and nothing is created or changed.
  * **A mapped address is the address it embeds.** `::ffff:127.0.0.1` and
    `127.0.0.1` are one host, so the replica already following that master is
    following it under either spelling and the node already in handshake at that
    address is not met a second time.

Two controls travel with every refusal, and they are the reason a tree cannot
pass this by refusing more:

  * a **global unicast** address is accepted at each of the three places, even
    though nothing is there to answer -- being unreachable is not being
    ineligible, and this container has no global address of its own to offer;
  * **loopback and link-local are accepted too.** This repository's own cluster
    and Sentinel suites run entirely on loopback, so a classifier that refused
    "local" addresses would take the whole suite down with it.

**A refusal is scored as a verdict and not as a sentence.** What a refusal cell
holds is `[is-an-error, names-the-address, names-the-class]` -- the three things
the step instruction asks of a refusal, *"an error that says which address was
refused and why"*, and nothing else. The alternative is to pin the wording the
golden tree happens to use, and that turns every one of these cells into a
spelling test: a tree that refused every one of these addresses, for the right
reason, in its own English, would then score zero on the half of this scenario
that is step 5's whole point. The wording is free; the verdict is not.

Three addresses on each surface are generated rather than written down --
sixteen (or four) bytes of `sha256("d05/<surface>/<label>")` under the prefix
its class is defined by. Every hand-written address here is in the step
instruction and in `DOCS/05-address-classes.md`, so a tree carrying a list of
those strings would pass the tables above; it cannot carry these, and they are
pushed through the same three surfaces as the rest rather than a corner of
their own. `g07_address_classes` makes the whole of that argument, at scale and
on one surface; these rows are the part of it that is behaviour rather than
gate.

**What this does not ask.** The instruction names three more places an address
arrives: the addresses the cluster learns from gossip, and the instances
Sentinel learns from a master's `INFO` and from hello messages. Each one needs a
second process to announce an address this one would otherwise never see.
`d05_learned` asks all three, and its docstring
says how each announcement is made in this tree's own format. So a tree that
puts the check in `clusterCommand` instead of `clusterStartHandshake` passes
every cell below and fails that scenario.

The zone observations are not step 5's work and are not scored as progress --
step 3 bought them and they are composed as regression, which is what they
are. They are here because they are what the mapped-address change is most
likely to break: zone identity lives in `sin6_scope_id`, one field away from the
bytes an unwrapping touches, and `fe80::1%lo` staying different from
`fe80::1%eth0`, while staying the same address as the same zone written as a
number, is the cheapest guard against a fix that flattened more than the mapped
form. The number is resolved with `if_nametoindex` rather than written down:
`lo` is interface 1 in this container and that is a fact about the container.

Both halves of the round trip are asked, because they are two code paths.
`CLUSTER MEET` -> `CLUSTER NODES` is in memory and is the first. `SENTINEL
MONITOR` -> the file `sentinelFlushConfig` rewrites -> a restart that reads it
back is the second, and it is the one where a zone is most easily lost: the
address is turned back into text to be written and parsed again to be read,
and `createSentinelAddr` stores what the resolver returned rather than what the
operator typed.

Nothing here waits on a cluster forming or a replica linking. Every observation
is taken from the bookkeeping the command changed, immediately, which is what
makes the counts deterministic: the addresses being met and monitored are ones
nothing answers at, on purpose.
"""

import hashlib
import os
import re
import socket
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import norm  # noqa: E402
import resp  # noqa: E402
import srv  # noqa: E402

PORTS = 8
FAMILY = "behaviour"

HOST = "::1"
MASTER = "mymaster"

# Long enough that no handshake entry this scenario creates is swept away
# between the command that created it and the `CLUSTER NODES` that counts it.
# `clusterCron` drops a node stuck in handshake after max(node-timeout, 1000)
# milliseconds, and every address met below is one nothing answers at, so every
# one of them is stuck by construction. A minute is two orders of magnitude more
# than the gap between the two commands and still well inside the scenario.
NODE_TIMEOUT = 60000

# The addresses that cannot name a peer, and the class each one is. The class
# is carried rather than derived because it is what the refusal has to name:
# "why" in *an error that says which address was refused and why*.
REFUSED = (
    ("multicast-if-local", "ff02::1", "multicast"),
    ("multicast-global", "ff0e::1", "multicast"),
    ("unspecified", "::", "unspecified"),
    ("unspecified-long", "0:0:0:0:0:0:0:0", "unspecified"),
    ("v4compat", "::127.0.0.1", "v4-compatible"),
    ("v4compat-other", "::1.2.3.4", "v4-compatible"),
    ("v4-unspecified", "0.0.0.0", "unspecified"),
    ("v4-multicast", "224.0.0.1", "multicast"),
    # The mapped spellings of the two above: refusing these is the half of the
    # mapped rule that says a mapped address is classified by what it embeds.
    ("mapped-unspecified", "::ffff:0.0.0.0", "unspecified"),
    ("mapped-multicast", "::ffff:224.0.0.1", "multicast"),
)

# The addresses that can, and that a tree which over-refuses will lose.
ACCEPTED = (
    ("loopback", "::1"),
    ("loopback-long", "0:0:0:0:0:0:0:1"),
    ("loopback-v4", "127.0.0.1"),
    ("mapped-loopback", "::ffff:127.0.0.1"),
    ("link-local", "fe80::1%lo"),
    ("unique-local", "fd00::1"),
    ("global", "2001:db8::1"),
    # RFC 3879 withdrew site-local; whatever fec0::/10 is now, it is unicast.
    ("former-site-local", "fec0::1"),
)


# -- addresses no list of spellings contains ------------------------------
def _stream(label, n):
    """`n` deterministic bytes for `label`.

    A pure function of the label rather than a position in a sequence, so the
    address is the same in every recording on every tree, and adding a row to
    one surface cannot renumber another one. The same idea as
    `g07_address_classes._stream`, copied rather than imported: a corpus module
    that reached into another one for its fixtures would make two scenarios one
    failure.
    """
    out = b""
    i = 0
    while len(out) < n:
        out += hashlib.sha256(("d05/%s/%d" % (label, i)).encode("ascii")).digest()
        i += 1
    return bytearray(out[:n])


def _v6(raw):
    return socket.inet_ntop(socket.AF_INET6, bytes(raw))


def _v4(raw):
    return "%d.%d.%d.%d" % (raw[0], raw[1], raw[2], raw[3])


# One scope per surface and none of them the `2` of `ff02::`, which the
# hand-written rows already ask about: a group is a group at every scope.
_GEN_SCOPE = {"repl": 0x01, "cluster": 0x05, "sentinel": 0x0e}


def _gen_multicast(surface):
    b = _stream("%s/gen-multicast" % surface, 16)
    b[0] = 0xff
    b[1] = (b[1] & 0xf0) | _GEN_SCOPE[surface]
    return _v6(b)


def _gen_v4compat(surface):
    b = _stream("%s/gen-v4compat" % surface, 4)
    b[0] = 1 + (b[0] % 223)      # a real `::a.b.c.d`, never `::` or `::1`
    return "::" + _v4(b)


def _gen_global(surface):
    b = _stream("%s/gen-global" % surface, 16)
    b[0] = 0x20 | (b[0] & 0x1f)  # 2000::/3
    return _v6(b)


def _refused_for(surface):
    """The refusal table for one surface: the written ones, then generated.

    Appended rather than interleaved, so that every cumulative observation
    taken partway through the loop -- the `/host`, `/addrs` and `/masters`
    controls -- keeps the value it had before these rows existed, up to the
    point where they are reached.
    """
    return REFUSED + (
        ("gen-multicast", _gen_multicast(surface), "multicast"),
        ("gen-v4compat", _gen_v4compat(surface), "v4-compatible"),
    )


def _accepted_for(surface):
    return ACCEPTED + (("gen-global", _gen_global(surface)),)


_GENERATED = tuple(a for surface in sorted(_GEN_SCOPE)
                   for a in (_gen_multicast(surface), _gen_v4compat(surface),
                             _gen_global(surface)))
assert len(set(_GENERATED)) == 9, "generated addresses collide"
assert not (set(_GENERATED) &
            (set(a for _l, a, _k in REFUSED) | set(a for _l, a in ACCEPTED))), \
    "a generated address is one of the written ones"


# The interface index `lo` has on this machine. `fe80::1%1` and `fe80::1%lo`
# being one address is true because `1` is what `lo` is numbered here, which is
# a fact about this container and not about IPv6 -- so it is resolved rather
# than written down. The fallback keeps the observation the shape it has always
# had on a machine where the lookup fails, and `cluster/zone/lo-index-known`
# says which of the two happened, so that a scenario asking a different
# question is visible as a different value instead of a silent one.
try:
    LO_INDEX = socket.if_nametoindex("lo")
    LO_INDEX_KNOWN = True
except Exception:
    LO_INDEX, LO_INDEX_KNOWN = 1, False

ZONE_NUMERIC = "fe80::1%%%d" % LO_INDEX


def _client(port, host=HOST, timeout=10.0):
    try:
        return resp.Client(host, port, timeout=timeout)
    except Exception:
        return None


def _say(client, *args):
    """One command's answer as a JSON-safe reply pair, or a marker.

    Never raises: an address handling change that made a server stop answering
    is a finding, and a scenario that died on it would report nothing at all.
    """
    if client is None:
        return ("error", b"harness: no-client")
    try:
        return client.cmd(*args)
    except Exception as e:
        return ("error", ("harness: %s" % type(e).__name__).encode())


def _text(reply):
    kind, val = reply
    if isinstance(val, bytes):
        return val.decode("utf-8", "replace")
    return ""


# The words that name each class. More than one per class because the class has
# more than one true name -- the unspecified address is the wildcard and is "any
# address" -- and none of them is the golden tree's sentence.
CLASS_WORDS = {
    "multicast": (r"multicast",),
    "unspecified": (r"unspecified", r"wildcard", r"any\s*address"),
    "v4-compatible": (r"ipv4[\s-]*compat", r"v4[\s-]*compat", r"compatible"),
}

# What makes a line a configuration error rather than a log line: it says so,
# or it says which line, or it says it cannot.
CONFIG_ERROR_WORDS = (r"config", r"\bline\s+\d", r"can't|cannot|unable")


def _spellings(addr):
    """The caller's spelling of this address, and this tree's own.

    Two, because step 3 settled that every address this tree writes down is
    written in one canonical form, and a refusal is a place it writes one
    down. An operator who typed `0:0:0:0:0:0:0:0` and was told `::` was told
    about the address they typed, spelled the way the rest of the tree spells
    it; a cell that called that a failure would be scoring step 3 compliance
    as a step 5 defect.

    A zone travels with the address and is not part of the 16 bytes, so it is
    taken off before the conversion and put back after it.

    The caller's own spelling comes back first, and `_names_addr` looks for it
    exactly as it always did; the canonical one is second and is matched with
    the delimiter rule below. So this can turn a cell that was false true and
    can never turn a true one false, which is what makes it safe to change a
    recorded corpus by.
    """
    out = [addr.lower()]
    base, _sep, zone = addr.partition("%")
    for fam in (socket.AF_INET6, socket.AF_INET):
        try:
            canon = socket.inet_ntop(fam, socket.inet_pton(fam, base))
        except (OSError, ValueError):
            continue
        if zone:
            canon += "%" + zone
        if canon.lower() not in out:
            out.append(canon.lower())
        break
    return out


# The characters an address literal is written out of. A candidate spelling
# with one of these on either side of it is a piece of some longer address and
# not the address itself -- `::` inside `fe80::9` above all, which is the one
# way the canonical spelling of `0:0:0:0:0:0:0:0` could be found in a sentence
# that never mentioned it.
_ADDR_CHAR = re.compile(r"[0-9a-f:.%]")


def _delimited(low, a):
    """`a` somewhere in `low` with a non-address character on either side."""
    for m in re.finditer(re.escape(a), low):
        before = low[m.start() - 1] if m.start() else " "
        after = low[m.end():m.end() + 1] or " "
        if not _ADDR_CHAR.match(before) and not _ADDR_CHAR.match(after):
            return True
    return False


# A run of the characters an address is written out of, brackets included, cut
# out of a sentence that has punctuation of its own around it.
_ADDR_RUN = re.compile(r"\[?[0-9a-f:.%]+\]?")


def _packed(text):
    """(family, bytes, zone) for a text that is an address, else None."""
    base, _sep, zone = text.strip("[]").partition("%")
    for fam in (socket.AF_INET6, socket.AF_INET):
        try:
            return fam, socket.inet_pton(fam, base), zone
        except (OSError, ValueError):
            continue
    return None


def _named_by_value(low, a):
    """Is `a` one of the addresses this text wrote down, by value?

    `_delimited` asks for a non-address character on each side of the match,
    and `:` is one of the characters an address is made of. A tree that
    refuses with

        Invalid master address ::: the unspecified address names no host

    has named `::` and put the sentence's own colon straight after it, and no
    rule about the character *after* the match can tell that apart from a
    longer address. So every run of address characters in the sentence is cut
    out whole, one trailing separator is dropped, and the two are compared as
    addresses rather than as text: `fe80::9` is not `::` however it is
    punctuated, and `::` followed by a colon is. Reading more than
    `_delimited` did can only turn a cell that was false true, which is what
    makes it safe to change a recorded corpus by.
    """
    want = _packed(a)
    if want is None:
        return False
    for m in _ADDR_RUN.finditer(low):
        tok = m.group(0)
        if _packed(tok) == want:
            return True
        # One separator, not every trailing colon: `:::` is `::` beside the
        # sentence's colon, and stripping them all would leave nothing.
        if tok[-1:] in ":.,;" and _packed(tok[:-1]) == want:
            return True
    return False


def _names_addr(text, addr):
    """Is the address the caller wrote named in the answer?

    Named, not spelled back letter for letter: either the caller's spelling or
    this tree's canonical one counts, and nothing else does -- an error that
    names some other address still fails here, which is what the cell is for.
    The bracketed form counts because `[%s]:%d` is how this tree writes an
    address beside a port, and the cluster surface refuses in exactly those
    terms.
    """
    low = text.lower()
    written, *rest = _spellings(addr)
    if written in low or ("[" + written + "]") in low:
        return True
    if any(_delimited(low, a) or ("[" + a + "]") in low for a in rest):
        return True
    return _named_by_value(low, addr)


def _names_class(text, klass):
    return any(re.search(p, text, re.I) for p in CLASS_WORDS[klass])


def _refusal(reply, addr, klass):
    """[is-error, names-the-address, names-the-class].

    The three things `instruction.md` requires of a refusal -- *"an error that
    says which address was refused and why"* -- and nothing else. The
    alternative is to pin the sentence, and a pinned sentence is a spelling
    test: the tree that refuses every address here correctly, in its own
    words, would lose every cell that scored the wording, and the wording is
    the one part of this the step does not specify. `norm.reply_to_json` says
    whether it is an error and `_text` says what it said, so nothing here is a
    second normalisation of a reply.
    """
    said = _text(reply)
    return [norm.reply_to_json(reply)[0] == "error",
            _names_addr(said, addr),
            _names_class(said, klass)]


def _config_refusal(out, addr):
    """The same verdict for output a process printed rather than a reply.

    `is-error` becomes "says it is a configuration error", because that is what
    the instruction asks of this half: the line is a configuration error, the
    process says so, and it stops. Whether it stopped is
    `sentinel/config/multicast/rc` and is not this.

    Two elements and not three. `_refusal` has a third, "the sentence names the
    class", and the golden's own startup banner does not: it prints the line it
    could not take and the reason it could not take it, and stops, without the
    word `multicast` anywhere in it. Pinning that third element at `False` would
    be a cell a tree loses for printing a *better* message than the golden's --
    the same defect this file was reviewed for, one surface further down. What
    the instruction asks of the file half is that the address is named and the
    process says it is a configuration error, and that is what is scored; the
    reply half, where the golden does name the class, keeps all three.
    """
    return [any(re.search(p, out, re.I) for p in CONFIG_ERROR_WORDS),
            _names_addr(out, addr)]


def _monitor_lines(path):
    """The `sentinel monitor` lines of a config file, and only those.

    Sentinel rewrites the whole file, and most of what it writes back is its
    own bookkeeping -- `myid`, `current-epoch`, `known-replica`, a config
    epoch that moves. The monitor lines are the ones an address is in.
    """
    raw = srv.read_regular(path)  # no link followed; [] if unreadable
    if raw is None:
        return []
    text = raw.decode("utf-8", "replace")
    return sorted(l.strip() for l in text.splitlines()
                  if l.startswith("sentinel monitor "))


def _info_field(client, section, field):
    if client is None:
        return None
    try:
        r = client.cmd("INFO", section)
    except Exception:
        return None
    if r[0] != "bulk" or r[1] is None:
        return None
    for line in r[1].decode("utf-8", "replace").splitlines():
        if line.startswith(field + ":"):
            return line.split(":", 1)[1].strip()
    return None


def _nodes(client):
    """Every `CLUSTER NODES` row, as (address, flags-word) pairs."""
    if client is None:
        return []
    try:
        r = client.cmd("CLUSTER", "NODES")
    except Exception:
        return []
    if r[0] != "bulk" or r[1] is None:
        return []
    out = []
    for line in r[1].decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) >= 3 and len(parts[0]) == 40:
            out.append((parts[1], parts[2]))
    return out


def _addrs(client):
    return sorted(a for a, _ in _nodes(client))


def _masters(client):
    """`SENTINEL MASTERS` as a sorted list of (name, ip, port)."""
    if client is None:
        return []
    try:
        r = client.cmd("SENTINEL", "masters")
    except Exception:
        return []
    if r[0] != "array" or not r[1]:
        return []
    out = []
    for entry in r[1]:
        if entry[0] != "array":
            continue
        kv = {}
        fields = entry[1]
        for i in range(0, len(fields) - 1, 2):
            k = _text(fields[i])
            v = _text(fields[i + 1])
            kv[k] = v
        out.append((kv.get("name", "?"), kv.get("ip", "?"), kv.get("port", "?")))
    return sorted(out)


# -- 1. replication -------------------------------------------------------
def _replication(ctx):
    """`SLAVEOF`: one command, one server, and every answer synchronous.

    `slaveofCommand` sets `server.masterhost` in the same call that answers, so
    every observation below is read straight after the reply with nothing
    waited on. The master is real and reachable so that the accepted cases are
    accepted for the right reason; whether the link then comes *up* is step 4's
    question and is deliberately not asked here.
    """
    mp, sp = ctx.port(0), ctx.port(1)
    master = ctx.server("repl-master", mp, ctxmod.default_directives(mp, HOST))
    master.start().wait_ready()
    slave = ctx.server("repl-slave", sp, ctxmod.default_directives(sp, HOST))
    slave.start().wait_ready()
    sc = _client(sp)

    # A legal master first, so that every refusal below has something to leave
    # untouched. A refusal that is only observable as "nothing happened" cannot
    # be told from a command that was never sent.
    ctx.emit_reply("repl/anchor/reply", _say(sc, "SLAVEOF", HOST, str(mp)))
    ctx.emit("repl/anchor/host", _info_field(sc, "replication", "master_host"))

    for label, addr, klass in _refused_for("repl"):
        ctx.emit("repl/refuse/%s/reply" % label,
                 _refusal(_say(sc, "SLAVEOF", addr, str(mp)), addr, klass),
                 adv=True)
        # The state the refusal must not have touched.
        ctx.emit("repl/refuse/%s/host" % label,
                 _info_field(sc, "replication", "master_host"))

    # The controls. Each one is set and then the anchor is restored, so the
    # next one starts from the same place.
    for label, addr in _accepted_for("repl"):
        ctx.emit_reply("repl/accept/%s/reply" % label,
                       _say(sc, "SLAVEOF", addr, str(mp)))
        ctx.emit("repl/accept/%s/host" % label,
                 _info_field(sc, "replication", "master_host"))
        _say(sc, "SLAVEOF", "NO", "ONE")

    # A registered name is not judged by its spelling: the resolver says what it
    # denotes, and `localhost` is still a master.
    ctx.emit_reply("repl/accept/name/reply",
                   _say(sc, "SLAVEOF", "localhost", str(mp)))
    ctx.emit("repl/accept/name/host",
             _info_field(sc, "replication", "master_host"))

    # The mapped-identity half. Following `127.0.0.1` and then being told to
    # follow `::ffff:127.0.0.1` is being told to follow the master already
    # followed, so the second command changes nothing and says so.
    _say(sc, "SLAVEOF", "NO", "ONE")
    ctx.emit_reply("repl/mapped/first", _say(sc, "SLAVEOF", "127.0.0.1", str(mp)))
    ctx.emit("repl/mapped/first-host",
             _info_field(sc, "replication", "master_host"))
    ctx.emit_reply("repl/mapped/again",
                   _say(sc, "SLAVEOF", "::ffff:127.0.0.1", str(mp)), adv=True)
    ctx.emit("repl/mapped/again-host",
             _info_field(sc, "replication", "master_host"))
    ctx.emit_reply("repl/mapped/again-hex",
                   _say(sc, "SLAVEOF", "::ffff:7f00:1", str(mp)), adv=True)
    ctx.emit("repl/mapped/again-hex-host",
             _info_field(sc, "replication", "master_host"))
    # And the other way round, so that the answer is not an artefact of which
    # spelling arrived first.
    _say(sc, "SLAVEOF", "NO", "ONE")
    ctx.emit_reply("repl/mapped/rev-first",
                   _say(sc, "SLAVEOF", "::ffff:127.0.0.1", str(mp)))
    ctx.emit("repl/mapped/rev-first-host",
             _info_field(sc, "replication", "master_host"))
    ctx.emit_reply("repl/mapped/rev-again",
                   _say(sc, "SLAVEOF", "127.0.0.1", str(mp)), adv=True)
    ctx.emit("repl/mapped/rev-again-host",
             _info_field(sc, "replication", "master_host"))
    # A different address is still a different master, mapped or not.
    ctx.emit_reply("repl/mapped/other",
                   _say(sc, "SLAVEOF", "::ffff:127.0.0.2", str(mp)))
    ctx.emit("repl/mapped/other-host",
             _info_field(sc, "replication", "master_host"))

    _say(sc, "SLAVEOF", "NO", "ONE")
    ctx.emit("repl/end/role", _info_field(sc, "replication", "role"))
    if sc:
        sc.close()
    slave.stop()
    master.stop()


# -- 2. cluster -----------------------------------------------------------
def _cluster(ctx):
    """`CLUSTER MEET`: the bookkeeping, read the instant the command answers.

    One node, and every address met is one nothing answers at. That is the
    point: what is being measured is which addresses the node is willing to
    write into its table, not whether a cluster forms -- `c11_cluster` and
    `d04_no_ipv4` already own that, over loopback, and neither of them would
    notice a node accepting a multicast address beside the real ones.
    """
    p = ctx.port(2)
    target = ctx.port(3)          # a port nothing is listening on, ever
    node = ctx.server("cluster-x", p, ctxmod.default_directives(p, HOST, extra=[
        "cluster-enabled yes",
        "cluster-config-file nodes.conf",
        "cluster-node-timeout %d" % NODE_TIMEOUT,
    ]))
    node.start().wait_ready()
    cc = _client(p)

    ctx.emit("cluster/start/addrs", _addrs(cc))

    for label, addr, klass in _refused_for("cluster"):
        ctx.emit("cluster/refuse/%s/reply" % label,
                 _refusal(_say(cc, "CLUSTER", "MEET", addr, str(target)),
                          addr, klass), adv=True)
        ctx.emit("cluster/refuse/%s/addrs" % label, _addrs(cc))

    # Text that is not an address at all keeps the refusal it has always had,
    # and keeps its wording: `c11_cluster` records that message and this step
    # does not get to change it.
    ctx.emit_reply("cluster/refuse/not-an-address/reply",
                   _say(cc, "CLUSTER", "MEET", "localhost", str(target)))
    ctx.emit("cluster/refuse/not-an-address/addrs", _addrs(cc))

    # The zone half, unchanged since step 3 and scored here as a guard: two
    # zones are two addresses, and the numeric and named forms of one zone are
    # one address.
    ctx.emit_reply("cluster/zone/lo",
                   _say(cc, "CLUSTER", "MEET", "fe80::1%lo", str(target)))
    ctx.emit("cluster/zone/lo-addrs", _addrs(cc))
    ctx.emit("cluster/zone/lo-index-known", LO_INDEX_KNOWN)
    ctx.emit_reply("cluster/zone/numeric",
                   _say(cc, "CLUSTER", "MEET", ZONE_NUMERIC, str(target)))
    ctx.emit("cluster/zone/numeric-addrs", _addrs(cc))
    ctx.emit_reply("cluster/zone/eth0",
                   _say(cc, "CLUSTER", "MEET", "fe80::1%eth0", str(target)))
    ctx.emit("cluster/zone/eth0-addrs", _addrs(cc))

    # The mapped half: the same host, twice, must be one entry in the table.
    ctx.emit_reply("cluster/mapped/first",
                   _say(cc, "CLUSTER", "MEET", "127.0.0.1", str(target)))
    ctx.emit("cluster/mapped/first-addrs", _addrs(cc))
    ctx.emit_reply("cluster/mapped/again",
                   _say(cc, "CLUSTER", "MEET", "::ffff:127.0.0.1", str(target)),
                   adv=True)
    ctx.emit("cluster/mapped/again-addrs", _addrs(cc), adv=True)
    ctx.emit_reply("cluster/mapped/again-hex",
                   _say(cc, "CLUSTER", "MEET", "::ffff:7f00:1", str(target)),
                   adv=True)
    ctx.emit("cluster/mapped/again-hex-addrs", _addrs(cc), adv=True)

    # And the controls, last, because each one adds a row.
    for label, addr in _accepted_for("cluster"):
        ctx.emit_reply("cluster/accept/%s/reply" % label,
                       _say(cc, "CLUSTER", "MEET", addr, str(target)))
    ctx.emit("cluster/accept/addrs", _addrs(cc))

    # Which addresses reached the file at all. This is a guard and not the
    # round trip: the round trip -- write it, restart, read it back with its
    # text intact -- is `d03_address_text`'s, which already restarts a cluster
    # node and compares its table, and the Sentinel half of it is
    # `sentinel/zone/*` below. What is asked here is the narrower thing the
    # refusals above make newly worth asking: that no row a refusal declined to
    # create is in the file anyway, and that the file is still written and
    # still parses -- a classifier wired into the wrong place can refuse the
    # command and persist the address, and an empty list here would be the only
    # sign.
    raw = srv.read_regular(os.path.join(node.dir, "nodes.conf"))
    conf = "" if raw is None else raw.decode("utf-8", "replace")
    saved = sorted(l.split()[1] for l in conf.splitlines()
                   if len(l.split()) >= 2 and len(l.split()[0]) == 40)
    ctx.emit("cluster/nodes-conf/addrs", saved)

    if cc:
        cc.close()
    node.stop()


# -- 3. sentinel ----------------------------------------------------------
def _sentinel_directives(port, master_port, addr=HOST, extra=()):
    d = [
        "bind %s" % HOST,
        "port %d" % port,
        "dir .",
        "logfile sentinel.log",
        "daemonize no",
        "sentinel monitor %s %s %d 2" % (MASTER, addr, master_port),
        "sentinel down-after-milliseconds %s 60000" % MASTER,
    ]
    d.extend(extra)
    return d


def _sentinel(ctx):
    """`SENTINEL MONITOR`, and the same directive read out of a file.

    The command and the configuration file reach the same function by two
    different routes, and only one of them is a command handler. A tree that
    put the check in `sentinelCommand` would pass every cell in the first half
    of this and fail `sentinel/config/*` -- which is the case that matters most,
    because a Sentinel that silently dropped the line comes up monitoring
    nothing and says nothing about it.
    """
    mp, sp = ctx.port(4), ctx.port(5)
    master = ctx.server("sent-master", mp, ctxmod.default_directives(mp, HOST))
    master.start().wait_ready()
    sent = ctx.server("sentinel", sp, _sentinel_directives(sp, mp),
                      binary="redis-sentinel", logfile="sentinel.log")
    sent.start().wait_ready()
    xc = _client(sp)

    ctx.emit("sentinel/start/masters", _masters(xc))

    for i, (label, addr, klass) in enumerate(_refused_for("sentinel")):
        ctx.emit("sentinel/refuse/%s/reply" % label,
                 _refusal(_say(xc, "SENTINEL", "MONITOR", "bad%d" % i, addr,
                               str(mp), "2"), addr, klass), adv=True)
        ctx.emit("sentinel/refuse/%s/masters" % label, _masters(xc))

    for i, (label, addr) in enumerate(_accepted_for("sentinel")):
        ctx.emit_reply("sentinel/accept/%s/reply" % label,
                       _say(xc, "SENTINEL", "MONITOR", "ok%d" % i, addr,
                            str(mp), "2"))
    ctx.emit("sentinel/accept/masters", _masters(xc))

    if xc:
        xc.close()
    sent.stop()
    master.stop()

    # The same directive, in the file. A configuration error stops the process
    # and says which line; it is not a line that is quietly not there.
    cp = ctx.port(6)
    rc, out = _sentinel_config_case(ctx, "sentinel-badconf", cp, mp, "ff02::1")
    ctx.emit("sentinel/config/multicast/rc", rc, adv=True)
    ctx.emit("sentinel/config/multicast/said",
             _config_refusal(out, "ff02::1"), adv=True)

    # The control, and it is asked rather than watched. The accepting answer is
    # a process that keeps running, which has no exit status to read and a
    # stdout that keeps growing for as long as one waits -- ten seconds of a
    # Sentinel's event log is not a stable observation. `SENTINEL MASTERS` is
    # the same fact in a form that stops changing: the directive was taken, and
    # this is the address it was taken as.
    gp = ctx.port(7)
    good = ctx.server("sentinel-okconf", gp,
                      _sentinel_directives(gp, mp, addr="2001:db8::1"),
                      binary="redis-sentinel", logfile="sentinel.log")
    good.start().wait_ready()
    gc = _client(gp)
    ctx.emit("sentinel/config/global/masters", _masters(gc))
    ctx.emit_reply("sentinel/config/global/addr-by-name",
                   _say(gc, "SENTINEL", "GET-MASTER-ADDR-BY-NAME", MASTER))
    if gc:
        gc.close()
    good.stop()

    _sentinel_zone(ctx, mp)


def _sentinel_zone(ctx, master_port):
    """The zone round trip that goes through a file, and through a restart.

    `CLUSTER MEET` -> `CLUSTER NODES` is the in-memory half and `_cluster` has
    it. This is the other one: `SENTINEL MONITOR` -> `sentinelFlushConfig`,
    which rewrites the whole configuration file from the address Sentinel is
    holding -> a restart that reads that file back. A zone identifier that is
    part of the address survives all three; one dropped on the way in, or
    printed without its `%lo` on the way out, is gone by the second
    `SENTINEL MASTERS` and the file says which of the two happened.

    This is a guard rather than step 5's work. Nothing about classification
    changes it -- `fe80::1%lo` is link-local unicast and was accepted before
    this step and after it -- and it is here because zone identity lives in
    `sin6_scope_id`, one field away from the bytes a mapped-address unwrapping
    rewrites.

    A second sentinel on the port the first one has finished with, rather than
    the one above: the file has to hold the monitor line this asks about and
    not the nine the acceptance controls left behind.
    """
    port = ctx.port(5)
    first = ctx.server("sentinel-zone", port,
                       _sentinel_directives(port, master_port),
                       binary="redis-sentinel", logfile="sentinel.log")
    first.start().wait_ready()
    zc = _client(port)
    ctx.emit_reply("sentinel/zone/monitor",
                   _say(zc, "SENTINEL", "MONITOR", "zoned", "fe80::1%lo",
                        str(master_port), "2"))
    ctx.emit("sentinel/zone/masters", _masters(zc))
    # Sentinel rewrites on its own after a MONITOR; asking for it makes the
    # file's state the command's state rather than a timer's.
    _say(zc, "SENTINEL", "FLUSHCONFIG")
    if zc:
        zc.close()
    first.stop()

    ctx.emit_text("sentinel/zone/conf", "\n".join(_monitor_lines(first.conf)),
                  sort=True)

    second = ctx.server("sentinel-zone-restart", port, [],
                        binary="redis-sentinel", datadir=first.dir,
                        logfile="restarted.log", conf=first.conf)
    second.start().wait_ready()
    rc = _client(port)
    ctx.emit("sentinel/zone/restart-masters", _masters(rc))
    if rc:
        rc.close()
    second.stop()


def _sentinel_config_case(ctx, name, port, master_port, addr):
    """Start a sentinel whose config names `addr`, and report `(rc, stdout)`.

    `srv.start_server_expect_failure` runs `redis-server`; Sentinel is a
    different binary with a different configuration grammar, so this is the same
    idea written for it. A process that is still running when the deadline
    passes is reported as `"running"` and stopped -- that is the *accepting*
    answer, and it has to be distinguishable from a clean exit rather than
    raising.

    `logfile` names a file rather than being empty, which is `c08_listeners`'s
    idiom and is load-bearing here: a configuration error is printed on stdout
    *before* the log is open, so the refusal still arrives, while an accepted
    configuration writes its banner and its event stream into the file and
    leaves stdout empty. Six seconds of a Sentinel's event log is not a stable
    observation; six seconds of nothing is.
    """
    d = ctx.workspace.sub(name)
    conf = os.path.join(d, "%s.conf" % name)
    # (`srv.write_new`: nothing in the score uid's directory is followed.)
    srv.write_new(conf, "\n".join([
        "bind %s" % HOST,
        "port %d" % port,
        "dir .",
        "logfile sentinel.log",
        "daemonize no",
        "sentinel monitor %s %s %d 2" % (MASTER, addr, master_port),
    ]) + "\n")
    try:
        p = subprocess.run([ctx.tree.bin("redis-sentinel"), conf],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=6, cwd=d, preexec_fn=ctx.tree.preexec)
        rc, out = p.returncode, p.stdout
    except subprocess.TimeoutExpired as t:
        rc, out = "running", (t.stdout or b"")
    return rc, out.decode("utf-8", "replace")


def run(ctx):
    _replication(ctx)
    _cluster(ctx)
    _sentinel(ctx)
