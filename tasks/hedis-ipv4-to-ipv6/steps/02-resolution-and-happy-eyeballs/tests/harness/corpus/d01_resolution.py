"""A name is resolved, and every address it resolves to is used.

This is the behaviour half of step 1. The static gates say no family is chosen
at compile time; this says what that buys an operator, from outside the
process, through the tree's own configuration file and the tree's own clients:

  * a configured listen address is resolved and the server listens on **every**
    address the resolution returned -- so a name with an address of each family
    yields a listener of each family;
  * every IPv6 listening socket is `IPV6_V6ONLY`, which is observable without
    any instrument at all: an IPv6 wildcard and an IPv4 wildcard can then be
    held on the same port by two different servers, and neither answers for the
    other's family;
  * an outbound connection resolves the name without naming a family and tries
    the addresses it got back, so a client reaches a server that is listening
    on any one of them.

**Nothing here records the text of an address.** How an address is spelled in
a log line, in `INFO`, in `CLIENT LIST` or in an error message is step 3's
subject and changes there; if this scenario pinned that text it would have to
be excepted again at step 3, and a scenario that gets re-recorded every time it
is inconvenient is not an expectation. What is recorded instead is what
answered, what did not, and what the tree itself reports about the link it
made. Those are the same facts at every step from here on.

The names come from `fixtures/hosts`, which the verifier appends to
`/etc/hosts` before anything runs:

    v6only.test     ::1
    v4only.test     127.0.0.1
    dual.test       ::1, 127.0.0.1
    multi.test      ::1, 127.0.0.1, 2001:db8::1, 2001:db8::2, 127.99.99.1
    legacy.test     127.0.0.1
    deep-tail.test  ::1, 127.0.0.1, 127.99.99.2, 127.99.99.3, 127.99.99.4,
                    2001:db8::3

`multi.test` is the awkward one on purpose. Its five addresses come back in the
order `::1, 127.0.0.1, 127.99.99.1, 2001:db8::1, 2001:db8::2`, and only the
first three are on this host; the documentation prefix has no route out of this
container and refuses immediately rather than hanging. Put a server on the
*third* address and the name is reachable only by a client that attempts an
address, is refused, and attempts the next -- twice. That is `srv-deep` in
section 3.

`deep-tail.test` is the second shape of the same awkwardness, and section 3b is
what asks about it. Walking an address list is not one behaviour but four: the
addresses are attempted in the order the resolver returned them, the walk stops
at the first one that connects rather than running to the end, the connection it
stops on is usable rather than merely open, and if none of them connects the
failure reported is the last address's. Two of the four are statements about an
*order* rather than about the resolver, and step 2 replaces the order with the
interleaved one of RFC 8305 section 4; the cells that move when it does are
listed in `flips.py` under step 2, and the rest of section 3b is written so that
it does not move -- the two servers it puts in `deep-tail.test` sit at the third
and fifth addresses, which interleaving reorders but does not swap. A single "did a client reach this
server" cell states the weakest of the four and leaves the rest unscored. So
section 3b puts a server further into a longer list with a dead address after
it, and asks each of the four separately: through the round trips the tree's own
client makes rather than a bare PING, through two servers at once so that
*which* of them answered says what order the list was walked in, and through the
one-word classification of the failure when nothing answered at all.
"""

import errno
import os
import socket
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import resp  # noqa: E402
import srv  # noqa: E402

PORTS = 6
FAMILY = "behaviour"

FAMILIES = {"v6": socket.AF_INET6, "v4": socket.AF_INET, "any": socket.AF_UNSPEC}

# Probes applied to every listening case: each family pinned, and then the
# resolver's own choice, which is the one an ordinary client makes.
PROBES = (("v6", "::1", "v6"),
          ("v4", "127.0.0.1", "v4"),
          ("localhost", "localhost", "any"),
          ("dual-name", "dual.test", "any"))


def _probe(host, port, family=socket.AF_UNSPEC, timeout=4.0):
    """One word for what a connection attempt to this address did.

    Deliberately not the error *text*: `strerror` wording is the C library's
    and an address embedded in a message is step 3's business. The `errno`
    name is the fact.
    """
    try:
        infos = socket.getaddrinfo(host, port, family, socket.SOCK_STREAM)
    except socket.gaierror:
        return "no-address"
    if not infos:
        return "no-address"
    af, st, proto, _, sa = infos[0]
    s = socket.socket(af, st, proto)
    s.settimeout(timeout)
    try:
        try:
            s.connect(sa)
        except socket.timeout:
            return "timeout"
        except OSError as e:
            return errno.errorcode.get(e.errno, str(e.errno))
        try:
            s.sendall(resp.encode_command(["PING"]))
            return "pong" if s.recv(64).startswith(b"+PONG") else "odd"
        except OSError as e:
            return "io:%s" % errno.errorcode.get(e.errno, e.errno)
    finally:
        s.close()


# A server that binds a loopback address is answering in well under a second.
# The deadline is generous by an order of magnitude and no more than that,
# because on a tree that has not done the work most of these never come up at
# all, and a deadline is then simply how long the scenario takes.
READY = 10.0


def _start(s, ready=None, deadline=READY):
    """Start a server and say whether it came up, never raising if it did not.

    A candidate that brings up fewer listeners than it should has failed those
    observations and no others. Raising here would instead lose every
    observation after this point, which would score a tree that missed one bind
    form the same as one that did not build.
    """
    s.start()
    try:
        s.wait_ready(deadline=deadline, host=ready)
        return True
    except Exception:  # noqa: BLE001 - a refusal to start is a result
        return False


def _listen_case(ctx, label, port, bindaddr, ready, adv=False):
    """Start a server on one `bind` line and ask, from outside, who answers."""
    s = ctx.server(label, port, ctxmod.default_directives(port, host=bindaddr),
                   host=ready)
    started = _start(s, ready)
    ctx.emit("bind/%s/started" % label, started, adv=adv)
    for name, host, fam in PROBES:
        ctx.emit("bind/%s/probe-%s" % (label, name),
                 _probe(host, port, FAMILIES[fam]), adv=adv)
    if started:
        with s.client(host=ready) as c:
            ctx.emit_reply("bind/%s/config" % label, c.cmd("CONFIG", "GET", "bind"))
            ctx.emit("bind/%s/tcp-port" % label,
                     srv.info_field(c, "server", "tcp_port"))
    s.stop()


def _client_case(ctx, tool, label, host, port, adv=False):
    """Run one of the tree's own clients at a name and record whether it got there.

    Only the exit status and a one-word outcome are recorded, for the reason in
    the module docstring: every one of these tools prints the address it tried,
    and that text is step 3's subject.
    """
    if tool == "redis-cli":
        args = ["-h", host, "-p", port, "ping"]
        marker = b"PONG"
        rc, out = srv.run_tool(ctx.tree, tool, args, timeout=30)
    else:
        args = ["-h", host, "-p", port, "-n", 4, "-c", 1, "-t", "ping", "-q"]
        marker = b"PING_INLINE"
        rc, out = srv.run_benchmark(ctx.tree, args, timeout=30)
    ctx.emit("%s/%s/rc" % (tool, label), rc, adv=adv)
    ctx.emit("%s/%s/reached" % (tool, label), marker in (out or b""), adv=adv)


# What a tool that never got a connection records in place of a reply. What it
# prints instead names the host it was given and quotes the C library's wording
# for the failure, and neither of those belongs in an expectation; the question
# this answers is whether there was a reply at all.
NO_REPLY = "<no-reply>"


def _cli_reply(ctx, label, host, port, cmd, stdin=None, sort=False, adv=True):
    """Run one redis-cli command at a name and record what came back.

    `_client_case` records that a connection happened; this records that the
    connection was *usable*, which is the other half of the same requirement. A
    caller that walks an address list but leaves the refusals it walked past
    where it can read them back does connect -- and then reports the first
    address's refusal and never sends the command. From outside, the two are
    told apart by asking for a reply rather than for an exit status.
    """
    rc, out = srv.run_tool(ctx.tree, "redis-cli",
                           ["-h", host, "-p", port] + [str(a) for a in cmd],
                           timeout=30, stdin=stdin)
    if rc == 0:
        ctx.emit_text(label, out, adv=adv, sort=sort)
    else:
        ctx.emit(label, NO_REPLY, adv=adv)


def _cli_info_field(ctx, label, host, port, section, field, adv=True):
    """One `INFO` field, fetched by the tree's own client through a name."""
    rc, out = srv.run_tool(ctx.tree, "redis-cli",
                           ["-h", host, "-p", port, "INFO", section],
                           timeout=30)
    value = NO_REPLY
    if rc == 0:
        for line in (out or b"").decode("utf-8", "replace").splitlines():
            if line.startswith(field + ":"):
                value = line.split(":", 1)[1].strip()
                break
    ctx.emit(label, value, adv=adv)


def _cli_failure(ctx, label, host, port, adv=True):
    """Which *kind* of failure a client reports when a name reaches nothing.

    One word, for the reason `_probe` gives: the wording is the C library's and
    the host in the message is the operator's, so neither is recorded. Which
    address's failure came back is the fact, and on a name whose addresses fail
    in two different ways that is enough to say how far down the list the caller
    got before it gave up.
    """
    rc, out = srv.run_tool(ctx.tree, "redis-cli",
                           ["-h", host, "-p", port, "PING"], timeout=30)
    text = (out or b"").decode("utf-8", "replace").lower()
    # The exit status decides first. A tool that had to be killed has whatever
    # it had printed so far in `out`, and reading a word out of that would
    # report the address it was on when the clock ran out as though it were the
    # one it stopped on.
    if rc == 0:
        word = "connected"
    elif rc == "timeout":
        word = "timeout"
    elif "unreachable" in text:
        word = "unreachable"
    elif "refused" in text:
        word = "refused"
    else:
        word = "other"
    ctx.emit(label, word, adv=adv)


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]

    # -- 1. one bind line, one resolution, every address it returned ---------
    # `localhost` and `dual.test` are the flip: two addresses in, two listeners
    # out. The single-family names are the control -- resolving a name must not
    # turn into listening on a family the name does not have.
    _listen_case(ctx, "name-localhost", p[0], "localhost", "::1", adv=True)
    _listen_case(ctx, "name-dual", p[0], "dual.test", "::1", adv=True)
    _listen_case(ctx, "name-v6only", p[0], "v6only.test", "::1")
    _listen_case(ctx, "name-v4only", p[0], "v4only.test", "127.0.0.1")
    _listen_case(ctx, "name-legacy", p[0], "legacy.test", "127.0.0.1")
    # Two names on one line: the addresses union, one listener each.
    _listen_case(ctx, "name-two", p[0], "v6only.test v4only.test", "::1",
                 adv=True)
    # A name beside a literal the name already resolves to. The entries add up,
    # so the address is asked for twice, and the second bind of it fails --
    # which is a startup failure, exactly as writing the literal twice is on
    # the baseline. Adversarial because the tempting reading of "every address
    # the resolution returned" is a set union that quietly swallows the
    # duplicate, and an operator who wrote the same address twice would then
    # never be told.
    _listen_case(ctx, "name-and-literal", p[0], "dual.test 127.0.0.1", "::1",
                 adv=True)
    # The literal controls: unchanged from the baseline, and the reason this
    # scenario can tell "resolves names" from "always listens on both".
    _listen_case(ctx, "literal-v6", p[0], "::1", "::1")
    _listen_case(ctx, "literal-v4", p[0], "127.0.0.1", "127.0.0.1")
    _listen_case(ctx, "literal-both", p[0], "::1 127.0.0.1", "::1")

    # No bind directive at all: the server chooses, and it has to choose both
    # wildcards rather than one of them.
    nobind = [d for d in ctxmod.default_directives(p[0])
              if not d.startswith("bind ")]
    s = ctx.server("no-bind", p[0], nobind, host="::1")
    ctx.emit("bind/no-bind/started", _start(s, "::1"), adv=True)
    for name, host, fam in PROBES:
        ctx.emit("bind/no-bind/probe-%s" % name,
                 _probe(host, p[0], FAMILIES[fam]), adv=True)
    s.stop()

    # A name two of whose five addresses are not on this host. "Every address
    # the resolution returned" means the ones that cannot be bound are fatal,
    # exactly as an unassigned literal already is -- so this is an exit status,
    # not a deadline: the server has to refuse to start rather than come up on
    # the three addresses it could manage. The baseline, which resolves the name
    # for IPv4 alone and binds the one address it gets, starts.
    rc, out, log = srv.start_server_expect_failure(
        ctx.tree, ctx.workspace, "name-multi",
        ctxmod.default_directives(p[0], "multi.test"), timeout=8)
    ctx.emit("bind/name-multi/rc", rc, adv=True)
    ctx.emit("bind/name-multi/probe-v6", _probe("::1", p[0], socket.AF_INET6),
             adv=True)
    ctx.emit("bind/name-multi/probe-v4",
             _probe("127.0.0.1", p[0], socket.AF_INET), adv=True)

    # -- 2. IPV6_V6ONLY, observed without an instrument ----------------------
    # Two servers, one IPv6 wildcard and one IPv4 wildcard, on the same port.
    # Both can only start if the IPv6 socket declined to cover IPv4; and then
    # each must answer for its own family and neither for the other's. A tree
    # whose IPv6 wildcard is dual-stack fails at the second server's bind.
    v6w = ctx.server("wildcard-v6", p[1], ctxmod.default_directives(p[1], "::"),
                     host="::1")
    ctx.emit("v6only/wildcard-v6/started", _start(v6w, "::1"))
    v4w = ctx.server("wildcard-v4", p[1],
                     ctxmod.default_directives(p[1], "0.0.0.0"),
                     host="127.0.0.1")
    ctx.emit("v6only/wildcard-v4/started", _start(v4w, "127.0.0.1"), adv=True)
    # Which server answered is told apart by the run id, not by the address:
    # the two are listening on one port and only their identity distinguishes
    # them. `DBSIZE` after a write to one of them is the cheapest form of it.
    try:
        with resp.Client("::1", p[1], timeout=5.0) as c6:
            c6.cmd("SET", "which", "v6")
            ctx.emit_reply("v6only/answered-v6", c6.cmd("GET", "which"))
        with resp.Client("127.0.0.1", p[1], timeout=5.0) as c4:
            ctx.emit_reply("v6only/answered-v4-isolated", c4.cmd("GET", "which"),
                           adv=True)
            c4.cmd("SET", "which", "v4")
        with resp.Client("::1", p[1], timeout=5.0) as c6:
            ctx.emit_reply("v6only/v6-unchanged", c6.cmd("GET", "which"), adv=True)
    except Exception as e:  # noqa: BLE001
        ctx.emit("v6only/answered-v6", "error:%s" % type(e).__name__)
        ctx.emit("v6only/answered-v4-isolated", "error", adv=True)
        ctx.emit("v6only/v6-unchanged", "error", adv=True)
    v4w.stop()
    v6w.stop()

    # The same fact at a loopback address rather than a wildcard.
    l6 = ctx.server("loop-v6", p[2], ctxmod.default_directives(p[2], "::1"))
    ctx.emit("v6only/loopback-v6-started", _start(l6, "::1"))
    l4 = ctx.server("loop-v4", p[2],
                    ctxmod.default_directives(p[2], "127.0.0.1"),
                    host="127.0.0.1")
    ctx.emit("v6only/loopback-pair-started", _start(l4, "127.0.0.1"), adv=True)
    l4.stop()
    l6.stop()

    # -- 3. outbound: resolve without a family, try what came back -----------
    # One server per bind form, every client name against each. The cell that
    # matters most is a v6-only server reached by a dual-stack name: the
    # address the baseline picks is the one that is not listening.
    hosts = (("dual", "dual.test"),
             ("localhost", "localhost"),
             ("v6only", "v6only.test"),
             ("v4only", "v4only.test"),
             ("multi", "multi.test"),
             ("literal-v6", "::1"),
             ("literal-v4", "127.0.0.1"))
    # `srv-deep` is the one that cannot be reached by luck. `multi.test`
    # resolves to ::1, 127.0.0.1, 127.99.99.1 and two addresses with no route,
    # in that order, and the server is on the *third* of them: a client reaches
    # it only by attempting an address, being refused, and attempting the next
    # one, twice over. A client that takes the resolver's first answer and
    # stops fails it whichever family it prefers -- which is the difference
    # between "ask without naming a family" and "try every address until one
    # connects". Everything reachable at the head of a list is reachable at the
    # baseline too, through the one IPv4 answer it asks for, so the rest of the
    # cells here are what this step must not break rather than what it is for.
    # The three host labels are the walk, a name with none of the server's
    # addresses, and the literal that is the refusal it walks past.
    #
    # This says only that the walk happened. Section 3b is where the walk is
    # asked about: in what order, how far, whether what it ended on works, and
    # what is reported when it ends on nothing.
    for bind_label, bindaddr, ready, only in (
            ("srv-v6", "::1", "::1", None),
            ("srv-v4", "127.0.0.1", "127.0.0.1", None),
            ("srv-dual", "dual.test", "::1", None),
            ("srv-deep", "127.99.99.1", "127.99.99.1",
             ("multi", "dual", "literal-v4"))):
        s = ctx.server("client-%s" % bind_label, p[3],
                       ctxmod.default_directives(p[3], host=bindaddr),
                       host=ready)
        ctx.emit("%s/server-up" % bind_label, _start(s, ready))
        for hlabel, host in hosts:
            if only is not None and hlabel not in only:
                continue
            # A name whose only address is of the family the server is not on
            # must still fail. Those cells are what keep "try every address"
            # from collapsing into "try every address of every name".
            adv = (only is not None
                   or hlabel in ("dual", "localhost", "multi", "v4only",
                                 "v6only"))
            _client_case(ctx, "redis-cli", "%s/%s" % (bind_label, hlabel),
                         host, p[3], adv=adv)
            _client_case(ctx, "redis-benchmark", "%s/%s" % (bind_label, hlabel),
                         host, p[3], adv=adv)
        s.stop()

    # -- 3b. how the list is walked: order, stopping, usability, last error ---
    # Section 3 asks whether a deep name is reached at all. That is the weakest
    # statement of the requirement rather than the whole of it: there are four
    # ways to walk an address list wrongly and only one of them is "did not
    # walk". The addresses have to be attempted in the order the resolver
    # returned them; the walk has to stop at the first one that connects rather
    # than run to the end of the list; the connection it stopped on has to be
    # usable rather than merely open, because a caller that keeps the refusals
    # it walked past reports one of them back and the walk is then invisible
    # from outside; and when none of them connects, the failure reported has to
    # be the last address's. A tree can do the first three and not the fourth.
    #
    # `deep-tail.test` is the second list shape, beside `multi.test`: six
    # addresses rather than five, two of them able to carry a server rather than
    # one and at different depths in the same list, the deeper of the two
    # reached only by crossing four refusals, and a last address that fails in a
    # different way from every other address in it.

    # Nothing is listening on this port yet, so this is the fourth question. A
    # client that stops at the head of `deep-tail.test` reports the refusal of a
    # loopback address; one that walked to the end of it reports the unreachable
    # network of the documentation address that is last. `dual.test` is the
    # control: every one of its addresses refuses, so the word is the same
    # however far the caller got, and a tree cannot pass the cell above by
    # saying "unreachable" whenever it fails to connect.
    #
    # "Last" is last in the order the attempts were made, and step 2 changes
    # that order: interleaved, `deep-tail.test` ends on 127.99.99.4 rather than
    # on 2001:db8::3, so this cell reads "refused" from step 2 on (FLIPS[2]).
    # The `dual.test` control is immune to the change by construction -- one
    # address per family cannot be reordered by interleaving them -- which is
    # what keeps it a control across both steps rather than only the first.
    _cli_failure(ctx, "deep/no-server/tail-last-error", "deep-tail.test", p[3])
    _cli_failure(ctx, "deep/no-server/dual-last-error", "dual.test", p[3])

    for dlabel, dname, bindaddr, miss in (
            ("multi", "multi.test", "127.99.99.1", "deep-tail.test"),
            ("tail", "deep-tail.test", "127.99.99.4", "multi.test")):
        s = ctx.server("deep-%s" % dlabel, p[3],
                       ctxmod.default_directives(p[3], host=bindaddr),
                       host=bindaddr)
        ctx.emit("deep/%s/server-up" % dlabel, _start(s, bindaddr))
        # The round trips the tree's own client makes, rather than one PING.
        # Each of these reaches the server only through a completed walk, and
        # each of them needs the connection the walk ended on to be in working
        # order rather than merely open: a write, the read back, the count, a
        # field the server reports about itself, the mode that takes the value
        # from standard input, and the cursor loop.
        _cli_reply(ctx, "deep/%s/set" % dlabel, dname, p[3],
                   ("SET", "walked", dlabel))
        _cli_reply(ctx, "deep/%s/get" % dlabel, dname, p[3], ("GET", "walked"))
        _cli_reply(ctx, "deep/%s/dbsize" % dlabel, dname, p[3], ("DBSIZE",))
        _cli_info_field(ctx, "deep/%s/info-role" % dlabel, dname, p[3],
                        "replication", "role")
        _cli_reply(ctx, "deep/%s/x-set" % dlabel, dname, p[3],
                   ("-x", "SET", "piped"), stdin=b"through-stdin")
        _cli_reply(ctx, "deep/%s/x-get" % dlabel, dname, p[3], ("GET", "piped"))
        _cli_reply(ctx, "deep/%s/scan" % dlabel, dname, p[3], ("--scan",),
                   sort=True)
        # The matching negatives, which are what keep "try every address" from
        # becoming "try every address of every name": the *other* deep name,
        # whose list is the same shape and holds none of this server's
        # addresses, and a name whose only address is of the family this server
        # is not on. Both have to fail here exactly as they do at the baseline.
        for nlabel, nhost in (("miss", miss), ("v6only", "v6only.test")):
            _client_case(ctx, "redis-cli", "deep-%s/%s" % (dlabel, nlabel),
                         nhost, p[3], adv=True)
        # And the caller that does not wait, at the name that needs a walk: it
        # takes the first address it can start a connection on and reports a
        # failure, here as at the baseline. Step 2 is what changes that -- a
        # caller that does not wait races the addresses rather than picking one
        # of them -- so this cell reports success from step 2 on (FLIPS[2]).
        # Until then it is the cell that says a walk was given to the callers
        # that wait and to no others.
        _client_case(ctx, "redis-benchmark", "deep-%s/self" % dlabel, dname,
                     p[3], adv=True)
        s.stop()

    # Which address of a list a walk stops at. Two servers on one port, on the
    # third and the fifth address of `deep-tail.test`, each holding its own name
    # for itself: a caller that attempts the addresses in the resolver's order
    # and stops at the first one that connects reaches the earlier server, and a
    # caller that runs the list to the end reaches the later one. Then the
    # earlier one goes away and the same question is asked again, so that the
    # walk has to carry on past an address that was answering a moment ago.
    low = ctx.server("deep-low", p[3],
                     ctxmod.default_directives(p[3], host="127.99.99.2"),
                     host="127.99.99.2")
    ctx.emit("deep/order/low-up", _start(low, "127.99.99.2"))
    high = ctx.server("deep-high", p[3],
                      ctxmod.default_directives(p[3], host="127.99.99.4"),
                      host="127.99.99.4")
    ctx.emit("deep/order/high-up", _start(high, "127.99.99.4"))
    try:
        with low.client(host="127.99.99.2") as c:
            c.cmd("SET", "where", "low")
        with high.client(host="127.99.99.4") as c:
            c.cmd("SET", "where", "high")
    except Exception as e:  # noqa: BLE001 - the cells below record the result
        ctx.note("deep/order: %s" % type(e).__name__)
    _cli_reply(ctx, "deep/order/both-up", "deep-tail.test", p[3],
               ("GET", "where"))
    low.stop()
    _cli_reply(ctx, "deep/order/low-gone", "deep-tail.test", p[3],
               ("GET", "where"))
    high.stop()

    # -- 4. the replication link, pointed at a name --------------------------
    for label, master_bind, ready, slaveof in (
            ("v6-master-dual-name", "::1", "::1", "dual.test"),
            ("v6-master-v6-name", "::1", "::1", "v6only.test"),
            ("v6-master-multi-name", "::1", "::1", "multi.test"),
            ("v4-master-v4-name", "127.0.0.1", "127.0.0.1", "v4only.test"),
            # The one that must *not* link: a v4-only name for a v6-only
            # master. Without it, a tree that connects to loopback whatever it
            # was asked for would score the four above.
            ("v6-master-v4-name", "::1", "::1", "v4only.test"),
            # The second one that must not link, for the opposite reason. The
            # master is on the *fifth* address of `deep-tail.test`, which every
            # caller that waits reaches; this link does not wait, so it cannot
            # walk a list. It takes the first address it can start a connection
            # on -- `::1`, which has nothing listening on it -- and reports the
            # failure the way it does today. Which callers wait is not changed
            # by step 1, and this is the cell that says so: a tree that gave the
            # replication link a walk would link here, and a tree that did step
            # 1 correctly does not.
            #
            # Step 2 is the step that does change it, and changes it here rather
            # than by giving the link a walk: the attempts overlap, so a link
            # that still does not wait for any one of them reaches a master no
            # single attempt could have found. The three cells this moves are in
            # FLIPS[2]. The `v6-master-v4-name` case above is what stops that
            # from collapsing into "link to anything": its name has no address
            # the master is on, and a race over those addresses still fails.
            ("v4-deep-master-tail-name", "127.99.99.4", "127.99.99.4",
             "deep-tail.test")):
        m = ctx.server("m-%s" % label, p[4],
                       ctxmod.default_directives(p[4], host=master_bind),
                       host=ready)
        ctx.emit("repl/%s/master-up" % label, _start(m, ready))
        r = ctx.server("r-%s" % label, p[5],
                       ctxmod.default_directives(p[5], host="::1"))
        ctx.emit("repl/%s/replica-up" % label, _start(r, "::1"))
        try:
            with m.client(host=ready) as mc:
                mc.cmd("SET", "replicated", label)
            with r.client() as rc:
                ctx.emit_reply("repl/%s/slaveof" % label,
                               rc.cmd("SLAVEOF", slaveof, str(p[4])))
                # A loopback replication link is up inside a second. The one
                # case here that must never link is the reason this is 8s and
                # not 20: for that case the deadline is not a timeout, it is
                # the runtime.
                up = srv.wait_info(rc, "replication", "master_link_status",
                                   "up", deadline=8.0)
                ctx.emit("repl/%s/link-up" % label, up, adv=True)
                ctx.emit("repl/%s/role" % label,
                         srv.info_field(rc, "replication", "role"))
                # The host as the operator wrote it, which is a configured
                # value rather than a rendered address: stable at every step.
                ctx.emit("repl/%s/master-host" % label,
                         srv.info_field(rc, "replication", "master_host"))
                ctx.emit("repl/%s/master-port" % label,
                         srv.info_field(rc, "replication", "master_port"))
                ctx.emit_reply("repl/%s/replicated-key" % label,
                               rc.cmd("GET", "replicated"), adv=True)
            with m.client(host=ready) as mc:
                ctx.emit("repl/%s/master-slave-count" % label,
                         srv.info_field(mc, "replication", "connected_slaves"),
                         adv=True)
        except Exception as e:  # noqa: BLE001
            # Whichever of this case's observations were not reached are
            # misses; the cases after it are still asked.
            ctx.note("repl/%s: %s" % (label, type(e).__name__))
        r.stop()
        m.stop()

    # -- 5. transports with no address in them, which must be untouched ------
    sock = os.path.join(ctx.workspace.base, "d01.sock")
    u = ctx.server("unix", p[0],
                   ctxmod.default_directives(p[0], "::1",
                                             extra=["unixsocket %s" % sock]))
    ctx.emit("unix/started", _start(u, "::1"))
    try:
        with resp.Client(unixsocket=sock, timeout=5.0) as uc:
            ctx.emit_reply("unix/ping", uc.cmd("PING"))
            ctx.emit_reply("unix/set-get", uc.cmd("SET", "u", "1"))
    except OSError as e:
        ctx.emit("unix/ping", "error:%s" % type(e).__name__)
        ctx.emit("unix/set-get", "error:%s" % type(e).__name__)
    rc, out = srv.run_tool(ctx.tree, "redis-cli", ["-s", sock, "ping"],
                           timeout=30)
    ctx.emit("unix/cli-rc", rc)
    ctx.emit("unix/cli-reached", b"PONG" in (out or b""))
    rc, out = srv.run_benchmark(
        ctx.tree, ["-s", sock, "-n", 4, "-c", 1, "-t", "ping", "-q"],
        timeout=30)
    ctx.emit("unix/bench-rc", rc)
    ctx.emit("unix/bench-reached", b"PING_INLINE" in (out or b""))
    u.stop()
