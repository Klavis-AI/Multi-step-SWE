"""How this tree writes an address down, and whether two spellings are one address.

`d01` and `d02` say which address a connection reached and in what order the
attempts were made; `d01`'s docstring reserves this ground explicitly. This
scenario is the complement: it never asks whether something connected -- that
is settled by the time each section starts -- and only ever asks how the
address was *spelled* in what came back, and whether a second spelling of the
same address is recognised as the same one.

Two spellings, used throughout: `::1` and `0:0:0:0:0:0:0:1`. They are the same
128 bits and RFC 5952 4.2 makes the first the canonical one. Everything here is
a question about one of three relations between them:

  * **joining** -- an address and a port written into one string. RFC 3986
    3.2.2 requires the brackets, and every site that does it with a bare
    `"%s:%d"` produces a string that cannot be split back up.
  * **splitting** -- the inverse. `nodes.conf` and the `-MOVED` follower both
    read back what something else wrote, and a tree that fixes one half of a
    round trip without the other is *worse* than one that fixes neither: it
    writes a file it can no longer load. Those cells are scored on the
    regression side on purpose, so a half-fix costs rather than pays.
  * **identity** -- `CLIENT KILL ADDR`, the `SLAVEOF` short-circuit. Both
    compare with `strcmp` today, so they see two addresses where there is one.

What is *not* asked matters as much. Three surfaces here echo text the operator
supplied -- `CONFIG GET bind`, the process title's bind address, the address in
a client's connect-failure message -- and for those the right answer is to
bracket the join and leave the spelling alone. A tree that canonicalises them
is changing what a configuration file means, and it fails those cells. Two
more, `INFO`'s `slave0:ip=` and `CLUSTER SLOTS`, keep the address and the port
in separate fields, where there is no join to fix; a solution that brackets
every address in sight breaks both.

Every classification here is made by two small functions, and both are
rehearsed against a fixture table whose answers are emitted beside the
findings. They are deliberately *not* `g05`'s checker: that one asks whether a
string is canonical, this one asks which of four spellings was used, and a
scenario that shared an instrument with the gate that scores the same tree
would lose both to one bug.

No IPv4 appears anywhere in this scenario, and that is a constraint rather than
a preference. `d03` is scored from step 3 through step 6, and step 4 removes
IPv4 entirely; a cell that needed a `127.0.0.1` listener would record happily
here and become unpassable two steps later, inside a mean that would not say
which step broke it.
"""

import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import srv  # noqa: E402

PORTS = 10
FAMILY = "behaviour"

# The two spellings. `SHORT` is what RFC 5952 4.2 makes canonical; `LONG` is
# the fully expanded form of the same address, which every conforming parser
# accepts and no conforming formatter produces.
SHORT = "::1"
LONG = "0:0:0:0:0:0:0:1"
# A third spelling, used only where the question is whether a *parser* treats
# equal addresses as equal: leading zeros inside a group are legal input.
PADDED = "0:0:0:0:0:0:0:0001"
# Two spellings whose text is not an address at all, and they are here because
# the rule this step states is about the text: a host whose text contains a `:`
# is bracketed and a host whose text does not is left alone. Everything below
# that reads a bracket reads both arms, because a tree that brackets whatever
# it is handed answers the first arm correctly and has not implemented the rule.
#
# `NAME` is registered and resolves to `::1` and to nothing else under
# `AF_UNSPEC`, so the sections that use it reach the same servers the literal
# spellings do, with no IPv4 anywhere.
NAME = "v6only.test"
# `UNKNOWN_NAME` resolves to nothing, and it is used in the one place `NAME`
# cannot be: a *listening* socket. The baseline picks the family from the
# presence of a `:` in the text, asks for `AF_INET`, and glibc answers an
# `AF_INET` query for `v6only.test` with `127.0.0.1` (see
# `tests/fixtures/hosts`) -- so the baseline would bind a second listener,
# on IPv4, inside a scenario that has none. A name that resolves to nothing
# fails on every tree, for the same reason on every tree, and the tree still
# writes the join before it gives up.
UNKNOWN_NAME = "d03-unknown.invalid"
# The same two spellings of one address, at an address that is not the
# loopback. `2001:db8::/32` is the documentation prefix RFC 3849 sets aside, so
# nothing is on it and nothing can be: an instance pointed at it never reaches
# anything, which is exactly why it can be pointed at it. It is here because
# every other identity question in this file is asked at `::1`, and `::1` is
# three characters with one non-zero group in it -- a comparison that got the
# right answer by reading the first group, or by counting colons, would look
# right at the loopback and nowhere else. These two have eight groups, a
# leading zero inside one of them, and a run of zeros in the middle.
DOC_LONG = "2001:0db8:0000:0000:0000:0000:0000:0001"
DOC_SHORT = "2001:db8::1"

MASTER = "d03master"
SLOTS = 16384
SPLIT = SLOTS // 2
# `foo` hashes into the upper half of the slot space and `bar` into the lower,
# so with the split at 8192 the first is always a redirect away from node A and
# the second is always served by it. The redirect names the slot `foo` landed in
# and `cluster/local/set` is `bar` being served where it was asked, so the
# arrangement is evidence rather than an assumption.
KEY_REMOTE = "foo"
KEY_LOCAL = "bar"

# Port block. All ten are named; the arrangement is written out because
# several sections share a server and a reader cannot tell that from the calls.
P_PLAIN = 0        # boundary text, CLIENT KILL targets, the open-port tool case
P_MASTER = 1       # replication, and the master sentinel is pointed at
P_REPLICA = 2
P_NODE_A = 3       # cluster: holds the lower half of the slot space
P_NODE_B = 4       # cluster: holds the upper half
P_NODE_C = 5       # cluster: met with the long spelling, holds no slots
P_SENTINEL = 6
P_CLOSED = 7       # never bound: the tools' connect-failure messages
P_LONGBIND = 8     # a server whose *operator* wrote the long spelling
P_DOCIDENT = 9     # a throwaway whose replication state is free to wreck

# Deadlines. Every wait in this file is a wait for something the harness itself
# set in motion -- a `MEET` it sent, a `SLAVEOF` it issued, a file it asked
# Sentinel to rewrite -- and never a measurement of the tree. The numbers are
# how long a loaded machine may take to finish a thing it has already been told
# to do, so the one thing that must not happen to them is being shortened to
# make a slow run pass: a wait that expires records `"timeout"` here, which is
# a value no tree produces and no recording matches, and a shortened deadline
# turns that into a value some tree does produce.

# `CLUSTER MEET` is gossip. Node C is learned from node B rather than from the
# client that sent the command, and the slot assignment after it is agreed
# between all three; this is the longest wait in the file because it is the
# only one waiting on a protocol between processes rather than on one process
# noticing one thing.
CLUSTER_DEADLINE = 45.0
# Sentinel learns the replica from the master's own `INFO`, on a refresh timer
# of its own. Thirty seconds covers several of those refreshes, including the
# first one, which it does not hurry.
SENTINEL_SLAVE_DEADLINE = 30.0
# Sentinel rewrites its configuration file when it learns something, and that
# rewrite is what the `sentinel/conf/*` cells read. It can only happen after
# the discovery above, so this deadline is spent on top of that one rather than
# instead of it.
SENTINEL_REWRITE_DEADLINE = 15.0
# The gap between polls, shared by all three loops. Two tenths of a second is
# short enough that the deadline decides the wait rather than the rounding, and
# long enough that the poll is not itself the load: at the longest deadline
# that is at most 225 `CLUSTER INFO` calls against a node that is busy agreeing
# about slots.
POLL_INTERVAL = 0.2
# How long one of the tree's own tools may run before it is stopped. Everything
# run here is given a single command or `-n 1` and answers in milliseconds; the
# allowance is for the one thing that can legitimately take seconds, which is a
# name that has to fail to resolve. `srv.run_tool` already reports a tool it
# had to stop as `"timeout"` rather than as an exit status, so an expiry here
# arrives in the cell saying what it was.
TOOL_DEADLINE = 30


# -- the two instruments --------------------------------------------------
def shape(text, port):
    """Which spelling of `<addr>:<port>` a piece of free text used.

    The port is required rather than inferred. A log line or a tool's error
    message is prose with an address in it, and the only thing that reliably
    marks where the address ends is the port that was asked for.
    """
    if text is None:
        return "no-text"
    for label, form in (("bracketed", "[%s]:%d" % (SHORT, port)),
                        ("bracketed-long", "[%s]:%d" % (LONG, port)),
                        ("unbracketed", "%s:%d" % (SHORT, port)),
                        ("unbracketed-long", "%s:%d" % (LONG, port))):
        # Not `in`: the port must end where the text says it ends. A cluster
        # log line naming the bus port 17403 contains the digits of 7403, and
        # a substring test would read it as a line about the client port.
        if re.search(re.escape(form) + r"(?!\d)", text):
            return label
    # An address with no port beside it answers a different question, so it is
    # reported as its own outcome rather than folded into "absent": a tree that
    # stopped printing the port is not a tree that started bracketing.
    if SHORT in text or LONG in text:
        return "address-without-port"
    return "absent"


def name_shape(text, port, host=NAME):
    """`shape`, asked about a host this instrument is not allowed to recognise.

    The not-bracketed arm has to be read by something that knows nothing about
    its host beyond the characters it was handed, which is why this is not a
    third and fourth row in `shape`'s table: `shape` is allowed to know the two
    spellings of `::1` and to report which one it found, and a reader of a name
    has nothing to compare against and no way to tell a name from an address.
    """
    if text is None:
        return "no-text"
    for label, form in (("bracketed", "[%s]:%d" % (host, port)),
                        ("unbracketed", "%s:%d" % (host, port))):
        if re.search(re.escape(form) + r"(?!\d)", text):
            return label
    if host in text:
        return "address-without-port"
    return "absent"


def authority(token):
    """Which spelling one already-isolated `host:port` token used.

    Distinct from `shape` because the input is a protocol field rather than
    prose: there is no surrounding text, and the host may be anything at all,
    so the host is returned as itself rather than matched against `::1`.
    """
    if token is None:
        return "no-text"
    if not token:
        return "empty"
    if token.startswith("["):
        end = token.find("]")
        if end < 0:
            return "unterminated-bracket"
        rest = token[end + 1:]
        if rest and not (rest.startswith(":") and rest[1:].isdigit()):
            return "trailing-junk"
        return "bracketed/" + _host(token[1:end])
    if token.count(":") > 1:
        # More than one colon and no brackets: whatever split this produced,
        # the result is ambiguous, and which half a reader takes for the port
        # is exactly the defect. Report the host a last-colon reader would get.
        return "unbracketed-v6/" + _host(token.rsplit(":", 1)[0])
    return "bare/" + _host(token.rsplit(":", 1)[0] if ":" in token else token)


def _host(h):
    if h == SHORT:
        return "short"
    if h == LONG:
        return "long"
    if h == PADDED:
        return "padded"
    return "other"


# Hand-written spellings with the answer each instrument should give, most of
# them wrong on purpose. The table is emitted, not asserted: an instrument that
# stopped working would otherwise report a finished tree and a broken one
# identically, and the recording is where that shows up.
SHAPE_FIXTURES = (
    ("bound to [::1]:7471 ok", 7471),
    ("bound to ::1:7471 ok", 7471),
    ("bound to [0:0:0:0:0:0:0:1]:7471 ok", 7471),
    ("bound to 0:0:0:0:0:0:0:1:7471 ok", 7471),
    ("bound to ::1:7471 ok", 7472),          # right address, wrong port
    ("Connecting to MASTER ::1", 7471),      # no port beside it
    ("nothing to see here", 7471),
    ("[::1]:74710 is a different port", 7471),   # prefix of a longer number
    ("connecting to ::1:17403", 7403),           # a cluster bus port, not 7403
    ("bus ::1:17403 client ::1:7403", 7403),     # both, and only one counts
    ("", 7471),
    (None, 7471),
)

NAME_SHAPE_FIXTURES = (
    ("connecting to v6only.test:7471 now", 7471),
    ("connecting to [v6only.test]:7471 now", 7471),
    ("connecting to v6only.test:74710 now", 7471),   # a longer port number
    ("MASTER v6only.test", 7471),                    # no port beside it
    ("connecting to other.test:7471 now", 7471),     # a different name
    ("nothing to see here", 7471),
    ("", 7471),
    (None, 7471),
)

AUTHORITY_FIXTURES = (
    "[::1]:60000",
    "::1:60000",
    "[0:0:0:0:0:0:0:1]:60000",
    "0:0:0:0:0:0:0:1:60000",
    "[0:0:0:0:0:0:0:0001]:60000",
    "[::1:60000",
    "[::1]:60000trailing",
    "[::1]",
    "example.test:60000",
    "",
)


# -- small readers --------------------------------------------------------
def _s(x):
    if isinstance(x, (bytes, bytearray)):
        return x.decode("utf-8", "replace")
    return x


def _start(s):
    """Start a server, reporting rather than raising if it will not come up.

    The baseline runs this scenario too, and a section whose server never
    started has to record that and go on: an exception here would lose every
    later section's observation to the first one that failed.
    """
    try:
        s.start().wait_ready()
        return True
    except Exception:
        return False


def _info(client, section):
    r = client.cmd("INFO", section)
    return _s(r[1]) or "" if r[1] is not None else ""


def _field(text, name):
    for line in (text or "").splitlines():
        if line.startswith(name + ":"):
            return line.split(":", 1)[1].strip()
    return None


def _kv(blob, key):
    """`key=value` out of a comma-separated INFO value."""
    for part in (blob or "").split(","):
        if part.startswith(key + "="):
            return part[len(key) + 1:]
    return None


def _read(path):
    try:
        with open(path, "r", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def _read_own(path):
    """`_read` for a file in a server's own directory, which the score uid owns:
    the same text and the same "" on failure, through `srv.read_text`, so no
    link is followed and a FIFO does not block."""
    try:
        return srv.read_text(path, errors="replace")
    except OSError:
        return ""


def _log_shapes(server, needle, port, how=shape, only=None):
    """The spellings used by *every* log line that mentions `needle`.

    A set rather than a first match. A log accumulates, and by the end of a
    section the same message has often been written with both spellings -- once
    before an address was re-stated and once after. Asking which came first
    reads whichever the instrument happened to test for; asking which occurred
    at all reads what the tree wrote.

    `how` picks the instrument, and `only` narrows the lines to the ones that
    also mention some text -- which is how the two arms of the join rule are
    read off one accumulating log without either arm seeing the other's lines
    and reporting them as `absent`.
    """
    text = server.log().decode("utf-8", "replace")
    lines = [l for l in text.splitlines()
             if needle in l and (only is None or only in l)]
    return sorted(set(how(l, port) for l in lines))


def _masters_set(server):
    """How many times this instance was told to replicate from somewhere.

    `slaveofCommand` writes the line once per call that got past the check for
    the master it is already attached to, so the count is of the calls that
    changed something rather than of the calls that were made.
    """
    text = server.log().decode("utf-8", "replace")
    return sum(1 for l in text.splitlines()
               if "SLAVE OF" in l and "enabled (user request)" in l)


def _directives(port, host=SHORT, extra=()):
    return ctxmod.default_directives(port, host, extra=extra)


def _cluster_directives(port):
    return _directives(port, extra=[
        "cluster-enabled yes",
        "cluster-config-file nodes.conf",
        # Long enough that stopping a node for a restart does not also start a
        # failure-detection story in the middle of a formatting measurement.
        "cluster-node-timeout 15000",
    ])


def _upshot(got):
    """A readiness wait's own answer: `True`, or `"timeout"` where it was not.

    `srv.wait_info` returns `False` when its deadline passes, and `False` is
    the shape of an answer -- a reader, and a diff against a recording, takes
    it for the tree having settled into the wrong state. It never means that
    here. Every call wrapped in this is waiting for a state the harness asked
    for a moment earlier, so the only thing an expiry establishes is that the
    harness stopped looking, and `"timeout"` is what this package says when an
    observation is unusable rather than negative.
    """
    return True if got else "timeout"


def _cluster_field(client, field):
    return _field(_s(client.cmd("CLUSTER", "INFO")[1]) or "", field)


def _wait_cluster(client, field, want, deadline=CLUSTER_DEADLINE):
    """`True`, or `"timeout"` if the field never read `want` in time.

    Never `False`: this loop has no way to observe a negative answer. It sees
    the field reach the value or it sees the clock run out, and those are
    different facts about different things -- one about the cluster, one about
    the machine. Recording the second as `False` files a fact about the harness
    under a cell that reads as a fact about the tree.
    """
    end = time.time() + deadline
    while time.time() < end:
        if _cluster_field(client, field) == want:
            return True
        time.sleep(POLL_INTERVAL)
    return "timeout"


def _nodes_addrs(text):
    """Field 1 of every `CLUSTER NODES` row -- the address, as written."""
    out = []
    for line in (text or "").splitlines():
        parts = line.split()
        if len(parts) >= 2 and len(parts[0]) == 40:
            out.append(parts[1])
    return out


# -- 0. the instruments, scored beside their findings ---------------------
def _instrument(ctx, p):
    ctx.emit("instrument/shape-fixtures",
             [[t, n, shape(t, n)] for t, n in SHAPE_FIXTURES])
    ctx.emit("instrument/name-shape-fixtures",
             [[t, n, name_shape(t, n)] for t, n in NAME_SHAPE_FIXTURES])
    ctx.emit("instrument/authority-fixtures",
             [[t, authority(t)] for t in AUTHORITY_FIXTURES])
    # The closed-port section measures a refusal. A port something else holds
    # answers instead, and the message recorded would then be a success.
    ctx.emit("instrument/closed-port-free",
             not srv.busy_ports(p[P_CLOSED], 1))


# -- 1. the boundary: what a plain server writes about itself -------------
def _boundary(ctx, p, ws):
    plain = ctx.server("d03-plain", p[P_PLAIN], _directives(p[P_PLAIN]))
    up = _start(plain)
    ctx.emit("boundary/server-up", up)
    if up:
        c = plain.client()
        tokens = []
        for line in (_s(c.cmd("CLIENT", "LIST")[1]) or "").splitlines():
            for f in line.split():
                if f.startswith("addr="):
                    tokens.append(f[5:])
        # `anetFormatAddr` already brackets, so this is a regression cell: the
        # question it answers is not "did the tree learn to bracket" but "did a
        # solution that taught the rest of the tree to bracket break the one
        # place that already did".
        ctx.emit("boundary/client-list/shapes",
                 sorted(set(authority(t) for t in tokens)))
        ctx.emit("boundary/client-list/count", len(tokens))
        title = _read("/proc/%d/cmdline" % plain.proc.pid).replace("\0", " ")
        ctx.emit("boundary/proctitle/shape", shape(title, p[P_PLAIN]))
        # `redis.c`'s "Creating Server TCP listening socket %s:%d" -- the only
        # startup line that names the address, reached by asking for a port
        # that is already taken. Both halves are read: a configuration error
        # goes to stdout before the log file is open, a bind failure into it.
        rc, out, log = srv.start_server_expect_failure(
            ctx.tree, ws, "d03-clash", _directives(p[P_PLAIN]))
        ctx.emit("boundary/listen-error/rc", rc, adv=True)
        # The same line, for a host whose text has no colon in it. This one
        # never reaches the bind -- the name resolves to nothing -- but the
        # join is written before the failure is reported, which is all this
        # cell reads. The two arms are one cell because they are one rule.
        nrc, nout, nlog = srv.start_server_expect_failure(
            ctx.tree, ws, "d03-clash-name",
            _directives(p[P_PLAIN], host=UNKNOWN_NAME))
        ctx.emit("boundary/listen-error/name-rc", nrc, adv=True)
        ctx.emit("boundary/listen-error/join-rule",
                 [shape(_s(out) + _s(log), p[P_PLAIN]),
                  name_shape(_s(nout) + _s(nlog), p[P_PLAIN],
                             host=UNKNOWN_NAME)], adv=True)
    else:
        ctx.emit("boundary/client-list/shapes", [])
        ctx.emit("boundary/client-list/count", 0)
        ctx.emit("boundary/proctitle/shape", "no-server")
        ctx.emit("boundary/listen-error/rc", None)
        ctx.emit("boundary/listen-error/name-rc", None)
        ctx.emit("boundary/listen-error/join-rule", ["no-server", "no-server"])

    # The operator wrote the long spelling. `CONFIG GET bind` has to give it
    # back unchanged -- it is a reading of the configuration, not of the tree
    # -- while the process title, which *joins* that text to a port, has to
    # bracket it and still not re-spell it. Its own server, so it is asked even
    # when the one above never started.
    longbind = ctx.server("d03-longbind", p[P_LONGBIND],
                          _directives(p[P_LONGBIND], host=LONG))
    lup = _start(longbind)
    ctx.emit("boundary/long-bind/server-up", lup)
    if lup:
        lc = longbind.client()
        ctx.emit_reply("boundary/long-bind/config-get-bind",
                       lc.cmd("CONFIG", "GET", "bind"), adv=True)
        ltitle = _read("/proc/%d/cmdline" % longbind.proc.pid).replace("\0", " ")
        ctx.emit("boundary/long-bind/proctitle-shape",
                 shape(ltitle, p[P_LONGBIND]), adv=True)
    else:
        ctx.emit("boundary/long-bind/config-get-bind", None)
        ctx.emit("boundary/long-bind/proctitle-shape", "no-server")
    return plain if up else None


# -- 2. identity: is a second spelling the same client? -------------------
def _kill_case(ctx, label, server, respell, adv=True):
    """Open a client, name it, and ask the server to kill it by one spelling.

    A fresh target per case. `CLIENT KILL` reports how many it killed, so a
    case that succeeds removes the client the next case would have used, and
    sharing one target would make the order of the cases part of the answer.
    """
    target = server.client()
    target.cmd("CLIENT", "SETNAME", "d03-target")
    ctl = server.client()
    addr = None
    for line in (_s(ctl.cmd("CLIENT", "LIST")[1]) or "").splitlines():
        fields = dict(f.split("=", 1) for f in line.split() if "=" in f)
        if fields.get("name") == "d03-target":
            addr = fields.get("addr")
    if addr is None:
        ctx.emit("identity/kill/%s/killed" % label, "no-target", adv=adv)
        return
    spelling = respell(addr)
    ctx.emit("identity/kill/%s/spelling-built" % label, spelling is not None)
    if spelling is None:
        ctx.emit("identity/kill/%s/killed" % label, "unparsed", adv=adv)
    else:
        r = ctl.cmd("CLIENT", "KILL", "ADDR", spelling)
        ctx.emit("identity/kill/%s/killed" % label,
                 r[1] if r[0] == "int" else _s(r[1]), adv=adv)
    for cl in (target, ctl):
        try:
            cl.close()
        except Exception:
            pass


_ADDR = re.compile(r"^\[(?P<host>[^\]]*)\]:(?P<port>\d+)$")


def _respell(host):
    """Rewrite `[<any>]:<port>` to `[<host>]:<port>`, keeping the port."""
    def f(addr):
        m = _ADDR.match(addr or "")
        if not m:
            return None
        return "[%s]:%s" % (host, m.group("port"))
    return f


def _identity(ctx, plain):
    if plain is None:
        for label in ("canonical", "long", "padded", "zerorun",
                      "compressed-padded", "unbracketed", "other",
                      "other-long"):
            ctx.emit("identity/kill/%s/spelling-built" % label, False)
            ctx.emit("identity/kill/%s/killed" % label, "no-server")
        return
    # The control: the spelling `CLIENT LIST` itself produced. It works on
    # every tree, and a solution that broke it would have made `CLIENT KILL`
    # unusable in the course of making it more usable.
    _kill_case(ctx, "canonical", plain, lambda a: a, adv=False)
    _kill_case(ctx, "long", plain, _respell(LONG))
    _kill_case(ctx, "padded", plain, _respell(PADDED))
    # Three more forms RFC 4291 2.2 allows for the same address, each on its
    # own axis rather than three of a kind: `0::1` puts the `::` somewhere
    # else, `::0001` pads a group inside a compressed form, and the last is
    # not bracketed at all -- which makes it the parse rule rather than the
    # comparison, the case RFC 3986 3.2.2 settles by ending the address at the
    # *last* colon.
    _kill_case(ctx, "zerorun", plain, _respell("0::1"))
    _kill_case(ctx, "compressed-padded", plain, _respell("::0001"))
    _kill_case(ctx, "unbracketed", plain,
               lambda a: "%s:%s" % (SHORT, (a or "").rsplit(":", 1)[-1])
               if _ADDR.match(a or "") else None)
    # The other side of the question, twice. A different address must still not
    # match, and both negatives differ from the target in the *address* only --
    # the port each is built with is the target's own. A negative that differed
    # by port as well would be refused by a tree that answers "the same" to
    # every pair it is shown, which is the one wrong comparison this section
    # exists to catch.
    _kill_case(ctx, "other", plain, _respell("::2"))
    # The same wrong address written out in full. A comparison that reads only
    # the leading group, or a fixed prefix of the text, matches this spelling
    # against `::1` and kills a client nobody named.
    _kill_case(ctx, "other-long", plain, _respell("0:0:0:0:0:0:0:2"))


# -- 3. replication: what the replica says its master is ------------------
def _sync_count(client):
    stats = _info(client, "stats")
    total = 0
    for f in ("sync_full", "sync_partial_ok"):
        v = _field(stats, f)
        total += int(v) if v and v.isdigit() else 0
    return total


def _replication(ctx, p):
    master = ctx.server("d03-master", p[P_MASTER], _directives(p[P_MASTER]))
    mup = _start(master)
    ctx.emit("repl/master-up", mup)
    replica = ctx.server("d03-replica", p[P_REPLICA], _directives(p[P_REPLICA]))
    rup = _start(replica)
    ctx.emit("repl/replica-up", rup)
    if not (mup and rup):
        for name, v in (("long-slaveof/reply", None),
                        ("long-slaveof/link-up", False),
                        ("master-sees-slave", False),
                        ("master-host", None),
                        ("role/replica-host", None),
                        ("role/master-slave-ip", None),
                        ("info/slave-row-ip", None),
                        ("log/replica-connecting-shapes", []),
                        ("log/master-sync-shapes", []),
                        ("identity/respell-reply", None),
                        ("identity/resyncs-on-respell", None),
                        ("identity/master-host-after", None),
                        ("identity/link-up-after", False),
                        ("identity/doc/reply", None),
                        ("identity/doc/master-host-after", None),
                        ("identity/doc/masters-set", None)):
            ctx.emit("repl/" + name, v)
        return None, None

    mc, rc_ = master.client(), replica.client()
    # The operator's spelling goes in; what comes back out of INFO, ROLE and
    # the log is the subject of the section.
    ctx.emit_reply("repl/long-slaveof/reply",
                   rc_.cmd("SLAVEOF", LONG, str(p[P_MASTER])), adv=True)
    linked = _upshot(srv.wait_info(rc_, "replication", "master_link_status",
                                   "up"))
    ctx.emit("repl/long-slaveof/link-up", linked)
    # The replica calls the link up when it has loaded the snapshot; the master
    # lists the slave when it has finished sending one. Both orders happen, so
    # the master's side is waited for separately -- otherwise `slave0:` and the
    # master's `ROLE` are read in a window where the right answer is "not yet".
    ctx.emit("repl/master-sees-slave",
             _upshot(srv.wait_info(mc, "replication", "connected_slaves",
                                   "1")))

    # `master_host` echoes the argument `SLAVEOF` was given, and that argument
    # may be a name rather than a literal -- `d01` replicates through one. So
    # this is a cell that must *not* move: the text belongs to whoever typed
    # it, and the step's rule is to bracket the join, not to re-spell the host.
    ctx.emit("repl/master-host",
             _field(_info(rc_, "replication"), "master_host"), adv=True)
    role = rc_.cmd("ROLE")
    ctx.emit("repl/role/replica-host",
             _s(role[1][1][1]) if role[0] == "array" and len(role[1] or []) > 1
             else None, adv=True)
    # Two more of the same kind, from the other direction: both take the
    # address from `getpeername` and keep it in a field of its own, so there is
    # no join here to bracket and nothing for a canonicaliser to improve.
    mrole = mc.cmd("ROLE")
    ips = []
    if mrole[0] == "array" and len(mrole[1] or []) > 2:
        for row in (mrole[1][2][1] or []):
            if row[0] == "array" and row[1]:
                ips.append(_s(row[1][0][1]))
    ctx.emit("repl/role/master-slave-ip", sorted(set(ips)))
    ctx.emit("repl/info/slave-row-ip",
             _kv(_field(_info(mc, "replication"), "slave0"), "ip"))

    # Held rather than emitted: the other arm of the same rule is measured at
    # the end of this section, and the two are one cell.
    lit_connecting = _log_shapes(replica, "Connecting to MASTER", p[P_MASTER])
    # The master's side of the same event already goes through
    # `anetFormatAddr`, so it is bracketed on every tree here.
    ctx.emit("repl/log/master-sync-shapes",
             _log_shapes(master, "Synchronization with slave", p[P_REPLICA]))

    # Identity. The replica is already replicating from this address; saying so
    # again in the canonical spelling is the same instruction, and a tree that
    # compares the two as text drops the link and syncs again to reach the
    # server it was already talking to. Three cells read the one event: the
    # reply (`+OK` against `+OK Already connected to specified master`), the
    # number of synchronisations the master was put through, and which of the
    # two spellings `master_host` ends up holding -- which on a tree that
    # short-circuits is still the operator's first one.
    before = _sync_count(mc)
    ctx.emit_reply("repl/identity/respell-reply",
                   rc_.cmd("SLAVEOF", SHORT, str(p[P_MASTER])), adv=True)
    after_up = _upshot(srv.wait_info(rc_, "replication",
                                     "master_link_status", "up"))
    ctx.emit("repl/identity/link-up-after", after_up)
    ctx.emit("repl/identity/resyncs-on-respell",
             _sync_count(mc) - before, adv=True)
    ctx.emit("repl/identity/master-host-after",
             _field(_info(rc_, "replication"), "master_host"))

    # The same question away from the loopback, on an instance of its own. The
    # reply comes out of `server.masterhost` without a packet being sent, so
    # the address this names needs no route and no interface -- and being a
    # documentation address it can have neither. A tree that compares the two
    # spellings as text sets a new master, which is the third cell: the
    # "SLAVE OF ... enabled (user request)" line is written once per time the
    # command actually reset replication, so the count is one on a tree that
    # recognised the second spelling and two on a tree that did not.
    #
    # Its own instance because this leaves a server pointed at an address that
    # answers nothing, and the master and replica above are read again by the
    # sentinel section. `SLAVEOF NO ONE` afterwards all the same: an instance
    # left retrying an unreachable master writes a line a second every second
    # the rest of the scenario runs.
    doc = ctx.server("d03-docident", p[P_DOCIDENT], _directives(p[P_DOCIDENT]))
    if _start(doc):
        dc = doc.client()
        dc.cmd("SLAVEOF", DOC_LONG, str(p[P_CLOSED]))
        ctx.emit_reply("repl/identity/doc/reply",
                       dc.cmd("SLAVEOF", DOC_SHORT, str(p[P_CLOSED])),
                       adv=True)
        ctx.emit("repl/identity/doc/master-host-after",
                 _field(_info(dc, "replication"), "master_host"), adv=True)
        ctx.emit("repl/identity/doc/masters-set", _masters_set(doc), adv=True)
        dc.cmd("SLAVEOF", "NO", "ONE")
    else:
        ctx.emit("repl/identity/doc/reply", None)
        ctx.emit("repl/identity/doc/master-host-after", None)
        ctx.emit("repl/identity/doc/masters-set", None)

    # The join rule's other arm, on the surface that writes it most often. The
    # name reaches the same master over the same address; its text has no colon
    # in it, so the join is left alone. Read together with the literal arm
    # above: a tree that brackets whatever it is handed answers the first and
    # fails the second. The two cells beside it are the reasons this is a fair
    # question to ask -- the name did reach the master, and the master a
    # replica reports is still the operator's own text.
    ctx.emit_reply("repl/name/slaveof-reply",
                   rc_.cmd("SLAVEOF", NAME, str(p[P_MASTER])), adv=True)
    ctx.emit("repl/name/link-up",
             _upshot(srv.wait_info(rc_, "replication", "master_link_status",
                                   "up")))
    ctx.emit("repl/name/master-host",
             _field(_info(rc_, "replication"), "master_host"))
    ctx.emit("repl/log/replica-connecting/join-rule",
             [lit_connecting,
              _log_shapes(replica, "Connecting to MASTER", p[P_MASTER],
                          how=name_shape, only=NAME)])
    return master, replica


# -- 4. cluster: the one surface that reads back what it wrote ------------
def _cluster(ctx, p):
    nodes, clients, up = [], [], []
    for label, idx in (("a", P_NODE_A), ("b", P_NODE_B), ("c", P_NODE_C)):
        n = ctx.server("d03-node-%s" % label, p[idx], _cluster_directives(p[idx]))
        ok = _start(n)
        up.append(ok)
        nodes.append(n)
        clients.append(n.client() if ok else None)
    ctx.emit("cluster/nodes-up", up)
    if not all(up):
        for name, v in (("meet/canonical", None), ("meet/long", None),
                        ("known-nodes", None), ("state-ok", False),
                        ("nodes-addrs", []), ("nodes-conf-addrs", []),
                        ("slots-ips", []), ("local/set", None),
                        ("moved/slot", None), ("moved/shape", "no-cluster"),
                        ("cli-c/rc", None), ("cli-c/reached", False),
                        ("restart/up", False),
                        ("restart/rejoined", False), ("restart/state-ok", False),
                        ("restart/nodes-addrs", [])):
            ctx.emit("cluster/" + name, v)
        return

    a, b, c = clients
    # B joins by the canonical spelling and C by the long one. `CLUSTER MEET`
    # already resolves its argument before storing it, so both are regression
    # cells -- and the second is the one that says so.
    ctx.emit_reply("cluster/meet/canonical",
                   a.cmd("CLUSTER", "MEET", SHORT, str(p[P_NODE_B])), adv=True)
    ctx.emit_reply("cluster/meet/long",
                   a.cmd("CLUSTER", "MEET", LONG, str(p[P_NODE_C])), adv=True)
    met = _wait_cluster(a, "cluster_known_nodes", "3")
    ctx.emit("cluster/known-nodes", _cluster_field(a, "cluster_known_nodes"))
    a.cmd("CLUSTER", "ADDSLOTS", *[str(s) for s in range(0, SPLIT)])
    b.cmd("CLUSTER", "ADDSLOTS", *[str(s) for s in range(SPLIT, SLOTS)])
    # Not `met and ...`: `"timeout"` is true, so an `and` would step past a
    # cluster that never formed and report the second wait's answer as if the
    # first had succeeded. A first wait that expired is the answer.
    ok = _wait_cluster(a, "cluster_state", "ok") if met is True else met
    ctx.emit("cluster/state-ok", ok)

    # A is the only node waited on above, and A is not the node the rest of this
    # section asks. `redis-cli -c` follows A's MOVED to B, and B answers
    # CLUSTERDOWN until its own view of the slot map is complete -- so
    # `cli-c/reached` was reading B's clock, not the tree's address handling.
    # Measured: it missed once in a whole-corpus replay at `--jobs 4` (the
    # verifier's own concurrency) and never in eight replays of this scenario
    # alone, which is the signature of a race and not of a defect. Nothing is
    # emitted here: the settle is the harness getting out of the tree's way.
    # Guarded on `ok is True` so a cluster that never formed is not waited on
    # twice more -- `ok` is `"timeout"` there, and `"timeout" is True` is False.
    if ok is True:
        for peer in (b, c):
            _wait_cluster(peer, "cluster_state", "ok")

    ctx.emit("cluster/nodes-addrs",
             sorted(set(_nodes_addrs(_s(a.cmd("CLUSTER", "NODES")[1]) or ""))))
    conf = []
    for n in nodes:
        conf += _nodes_addrs(_read_own(os.path.join(n.dir, "nodes.conf")))
    ctx.emit("cluster/nodes-conf-addrs", sorted(set(conf)))
    slots_ips = []
    sl = a.cmd("CLUSTER", "SLOTS")
    if sl[0] == "array" and sl[1]:
        for rng in sl[1]:
            for entry in (rng[1] or [])[2:]:
                if entry[0] == "array" and entry[1]:
                    slots_ips.append(_s(entry[1][0][1]))
    # Address and port in separate fields: nothing to bracket, and a solution
    # that bracketed it anyway would hand every cluster client a host it cannot
    # resolve.
    ctx.emit("cluster/slots-ips", sorted(set(slots_ips)))

    ctx.emit_reply("cluster/local/set", a.cmd("SET", KEY_LOCAL, "v"))
    moved = a.cmd("GET", KEY_REMOTE)
    err = _s(moved[1]) if moved[0] == "error" else None
    ctx.emit("cluster/moved/slot",
             int(err.split()[1]) if err and len(err.split()) > 2
             and err.split()[1].isdigit() else None)
    ctx.emit("cluster/moved/shape", shape(err, p[P_NODE_B]), adv=True)

    # The inverse of the same string: `redis-cli -c` has to take the address
    # apart again. This is the cell a solution that only fixes formatters loses.
    rc, out = srv.run_tool(ctx.tree, "redis-cli",
                           ["-c", "-h", SHORT, "-p", str(p[P_NODE_A]),
                            "SET", KEY_REMOTE, "v"], timeout=TOOL_DEADLINE)
    ctx.emit("cluster/cli-c/rc", rc, adv=True)
    ctx.emit("cluster/cli-c/reached", b"OK" in (out or b""), adv=True)

    # And the other inverse: a node reading its own `nodes.conf` back. A tree
    # that brackets the writer and leaves `strrchr(':')` in the reader cannot
    # rejoin the cluster it just wrote down.
    a_node = nodes[0]
    a_node.stop()
    restarted = ctx.server("d03-node-a2", p[P_NODE_A], [],
                           datadir=a_node.dir, conf=a_node.conf)
    rup = _start(restarted)
    ctx.emit("cluster/restart/up", rup)
    if rup:
        ra = restarted.client()
        ctx.emit("cluster/restart/rejoined",
                 _wait_cluster(ra, "cluster_known_nodes", "3"))
        ctx.emit("cluster/restart/state-ok",
                 _wait_cluster(ra, "cluster_state", "ok"))
        ctx.emit("cluster/restart/nodes-addrs",
                 sorted(set(_nodes_addrs(
                     _s(ra.cmd("CLUSTER", "NODES")[1]) or ""))))
    else:
        ctx.emit("cluster/restart/rejoined", False)
        ctx.emit("cluster/restart/state-ok", False)
        ctx.emit("cluster/restart/nodes-addrs", [])


# -- 5. sentinel ----------------------------------------------------------
def _sentinel_directives(port, master_port, spelling):
    return [
        "bind %s" % SHORT,
        "port %d" % port,
        "dir .",
        "logfile sentinel.log",
        "daemonize no",
        "sentinel monitor %s %s %d 1" % (MASTER, spelling, master_port),
        "sentinel down-after-milliseconds %s 5000" % MASTER,
        # Far beyond anything this section waits for, and that is the point.
        # Sentinel decides a replica needs its configuration fixed by comparing
        # the master the replica *reports* with the master Sentinel knows, and
        # the replica reports the operator's spelling while Sentinel holds the
        # resolved one. On a tree that compares those as text the two differ,
        # and once this timeout has elapsed Sentinel would send `SLAVEOF` to
        # correct a replica that is already correct. That is a real property of
        # the step and it belongs to `g05`'s static count, not here: reaching it
        # would buy one cell by putting a wall clock inside a scenario, which is
        # how a wrong green gets in.
        "sentinel failover-timeout %s 300000" % MASTER,
    ]


def _sentinel(ctx, p, master):
    if master is None:
        for name, v in (("up", False), ("masters-ip", None),
                        ("get-master-addr-host", None), ("saw-slave", False),
                        ("slaves-ip", []), ("info-address-shape", "no-sentinel"),
                        ("conf/monitor-host", None),
                        ("conf/known-slave-host", None),
                        ("restart/up", False), ("restart/master-known", False)):
            ctx.emit("sentinel/" + name, v)
        return
    s = ctx.server("d03-sentinel", p[P_SENTINEL],
                   _sentinel_directives(p[P_SENTINEL], p[P_MASTER], LONG),
                   binary="redis-sentinel", logfile="sentinel.log")
    up = _start(s)
    ctx.emit("sentinel/up", up)
    if not up:
        for name, v in (("masters-ip", None), ("get-master-addr-host", None),
                        ("saw-slave", False), ("slaves-ip", []),
                        ("info-address-shape", "no-sentinel"),
                        ("conf/monitor-host", None),
                        ("conf/known-slave-host", None),
                        ("restart/up", False), ("restart/master-known", False)):
            ctx.emit("sentinel/" + name, v)
        return

    sc = s.client()
    # Sentinel resolves an address before it stores one, so the long spelling
    # the configuration file used never survives to any of these -- which makes
    # every one of them a cell that says a solution did not start re-spelling
    # things it had already got right.
    end = time.time() + SENTINEL_SLAVE_DEADLINE
    seen = False
    while time.time() < end and not seen:
        seen = _kv_reply(sc.cmd("SENTINEL", "master", MASTER)).get(
            "num-slaves") == "1"
        if not seen:
            time.sleep(POLL_INTERVAL)
    ctx.emit("sentinel/saw-slave", _upshot(seen))

    ms = sc.cmd("SENTINEL", "masters")
    ctx.emit("sentinel/masters-ip",
             sorted(set(_kv_reply(i).get("ip") or "" for i in (ms[1] or [])))
             if ms[0] == "array" else None)
    addr = sc.cmd("SENTINEL", "get-master-addr-by-name", MASTER)
    ctx.emit("sentinel/get-master-addr-host",
             _s(addr[1][0][1]) if addr[0] == "array" and addr[1] else None)
    sv = sc.cmd("SENTINEL", "slaves", MASTER)
    ctx.emit("sentinel/slaves-ip",
             sorted(set(_kv_reply(i).get("ip") or "" for i in (sv[1] or [])))
             if sv[0] == "array" else [])
    # The one join in Sentinel's own output.
    ctx.emit("sentinel/info-address-shape",
             shape(_kv(_field(_info(sc, "sentinel"), "master0"), "address"),
                   p[P_MASTER]), adv=True)

    # Sentinel keeps its whole state in the file it was given and rewrites it
    # when it learns something. Wait for the rewrite by its content.
    end = time.time() + SENTINEL_REWRITE_DEADLINE
    rewritten = False
    while time.time() < end:
        rewritten = "known-slave" in _read_own(s.conf)
        if rewritten:
            break
        time.sleep(POLL_INTERVAL)
    rows = {}
    for raw in _read_own(s.conf).splitlines():
        parts = raw.split()
        if len(parts) >= 4 and parts[0] == "sentinel":
            rows.setdefault(parts[1], parts[3])
    ctx.emit("sentinel/conf/monitor-host", rows.get("monitor"))
    # `None` here would be the same value the branches above emit for "there is
    # no Sentinel at all", and a row that was never written is not a row that
    # was written wrong. The `monitor` row above needs no such care: it is in
    # the file this server was started with, so it is there whether or not the
    # rewrite ever ran.
    ctx.emit("sentinel/conf/known-slave-host",
             rows.get("known-slave") if rewritten else "timeout")

    # And the round trip: the file it just wrote is the file it reads at the
    # next start. `createSentinelAddr` resolves what it finds there, so a
    # bracketed host in that row is one Sentinel cannot resolve and it refuses
    # to start at all.
    s.stop()
    again = ctx.server("d03-sentinel2", p[P_SENTINEL], [],
                       binary="redis-sentinel", datadir=s.dir, conf=s.conf,
                       logfile="sentinel.log")
    rup = _start(again)
    ctx.emit("sentinel/restart/up", rup)
    known = False
    if rup:
        r = again.client().cmd("SENTINEL", "get-master-addr-by-name", MASTER)
        known = r[0] == "array" and bool(r[1])
    ctx.emit("sentinel/restart/master-known", known)


def _kv_reply(reply):
    """A sentinel flat-map reply (k1, v1, k2, v2, ...) as a dict of text."""
    if reply[0] != "array" or not reply[1]:
        return {}
    items = reply[1]
    out = {}
    for i in range(0, len(items) - 1, 2):
        key = _s(items[i][1])
        if isinstance(key, str):
            out[key] = _s(items[i + 1][1])
    return out


# -- 6. the tools' own messages -------------------------------------------
def _tools(ctx, p, plain):
    closed = p[P_CLOSED]
    # Both arms of the join rule at one surface, and the canonical spelling
    # beside the long one so that the bracket and the re-spelling stay two
    # separate questions. One cell: a tree that brackets the name too has not
    # applied the rule, and a cell per arm would call that two thirds right.
    arms = []
    for label, host, how in (("short", SHORT, shape),
                             ("long", LONG, shape),
                             ("name", NAME, name_shape)):
        rc, out = srv.run_tool(ctx.tree, "redis-cli",
                               ["-h", host, "-p", str(closed), "PING"],
                               timeout=TOOL_DEADLINE)
        ctx.emit("tools/cli/closed-%s/rc" % label, rc, adv=True)
        arms.append([label, how(_s(out), closed)])
    ctx.emit("tools/cli/closed/join-rule", arms, adv=True)
    rc, out = srv.run_tool(ctx.tree, "redis-benchmark",
                           ["-h", SHORT, "-p", str(closed), "-n", "1",
                            "-c", "1", "-t", "ping", "-q"],
                           timeout=TOOL_DEADLINE)
    ctx.emit("tools/benchmark/closed/rc", rc, adv=True)
    # The benchmark refuses a closed port asynchronously, from the write that
    # follows the connect, and that path names no address at all. The cell is
    # here to say so: it reads "absent" and a tree that starts printing an
    # address there has changed which message an operator gets.
    ctx.emit("tools/benchmark/closed/shape", shape(_s(out), closed), adv=True)

    # Which is why the join rule has to be asked of this tool somewhere else.
    # The site that formats the address is the one reached when the connection
    # fails before it is attempted -- when the host does not resolve -- so both
    # arms are hosts that do not: one whose text has no colon in it, and one
    # whose text arrives already bracketed. One cell across the two, for the
    # reason the client's is one cell: a tree that brackets both has not
    # applied the rule and a cell per arm would call it half right.
    barms = []
    for label, host in (("name", UNKNOWN_NAME), ("bracketed", "[%s]" % SHORT)):
        rc, out = srv.run_tool(ctx.tree, "redis-benchmark",
                               ["-h", host, "-p", str(closed), "-n", "1",
                                "-c", "1", "-t", "ping", "-q"],
                               timeout=TOOL_DEADLINE)
        ctx.emit("tools/benchmark/unresolved-%s/rc" % label, rc, adv=True)
        barms.append([label, name_shape(_s(out), closed, host=host)])
    ctx.emit("tools/benchmark/unresolved/join-rule", barms, adv=True)

    # The same tool, the same long spelling, against a server that is there:
    # the spelling is the operator's and has to reach the address it names.
    if plain is None:
        ctx.emit("tools/cli/long-host-open/rc", None)
        ctx.emit("tools/cli/long-host-open/reached", False)
        return
    rc, out = srv.run_tool(ctx.tree, "redis-cli",
                           ["-h", LONG, "-p", str(p[P_PLAIN]), "PING"],
                           timeout=TOOL_DEADLINE)
    ctx.emit("tools/cli/long-host-open/rc", rc)
    ctx.emit("tools/cli/long-host-open/reached", b"PONG" in (out or b""))


# -- the scenario ---------------------------------------------------------
def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]
    ws = ctx.workspace
    _instrument(ctx, p)
    plain = _boundary(ctx, p, ws)
    _identity(ctx, plain)
    master, _replica = _replication(ctx, p)
    _cluster(ctx, p)
    _sentinel(ctx, p, master)
    _tools(ctx, p, plain)
    ctx.shutdown_all()
    ctx.note("address text and identity: %d observations across six surfaces"
             % len(ctx.observations))
