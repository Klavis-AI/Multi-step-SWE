"""The enumerated must-flip exceptions to "every expectation is the baseline's".

Expectations are recorded from the untouched baseline, with one exception: an
observation the step is *supposed* to change. Those are listed here, one line
each, with the requirement that makes the change correct -- and nowhere else.

The mechanism is deliberately dull. A step's expectation file is composed
from the baseline recording, with exactly the observations named below
replaced by the value recorded from the golden tree of the step that
introduces the change. Everything not named here stays the baseline's, so an unintended
difference between the baseline and a golden tree is a failure rather than a
silent re-baseline -- which is the whole reason the list is enumerated instead
of computed from a diff.

Composition also refuses an entry whose two values are *equal*: a flip that
does not flip is a stale line, and leaving it in would quietly widen the
exception.

Scenarios recorded wholesale from a golden tree (`flip_from` in `registry.py`)
are not listed here. Those exercise a semantic the baseline does not have at
all, so there is no baseline value to except; the registry entry is the record.
"""

# step -> {scenario: {observation: reason}}
#
# The reason is the requirement, stated the way the step's instruction states
# it, so that this file can be read against the instruction directly.
FLIPS = {
    # Step 1's list is short. Two things the step requires were already true
    # at the pin and so cannot flip: with no `bind` directive at all the
    # baseline already opens both wildcards, and it already sets `IPV6_V6ONLY`
    # on the IPv6 listener it opens. What the baseline does not do is
    # *resolve* -- it asks for one family and uses the one answer it gets, at
    # both ends -- and that is what moves here. Five observations, in the two
    # scenarios that put a name rather than a literal in front of the address
    # code.
    1: {
        "c08_listeners": {
            "listen/hostname-bind/probe-v6":
                "a configured listen address is resolved and the server listens"
                " on every address the resolution returns, so `bind localhost`"
                " answers on ::1 as well as on 127.0.0.1",
            "listen/hostname-bind/probe-localhost-any":
                "the same requirement reached through the resolver's own"
                " preference order rather than through a literal",
        },
        # The client half of the same requirement. This server listens on ::1
        # alone, so `-h localhost` reaches it only if the client uses the IPv6
        # address the resolver returned instead of the IPv4 one it happened to
        # return first.
        "c13_benchmark": {
            "bench/host-hostname/rc":
                "a client resolves its host argument and connects over the"
                " family the resolution returns, so the run reaches a server"
                " listening only on ::1 and exits 0",
            "bench/host-hostname/out":
                "the same run therefore prints a rate rather than `Connection"
                " refused`",
            "bench/host-hostname/dbsize":
                "and the server it claimed to drive really did receive the"
                " writes, which is what separates connecting from printing",
        },
    },
    # Step 2 changes *when* attempts are made, not which addresses exist, so
    # every flip it owes is in `d01_resolution` -- the scenario that asks what a
    # caller reached. The gates do not move at all: interleaving needs one
    # family named and step 2 names the one section 4 puts first, so
    # `g02_family_literals` reads the same zero it read at step 1, and the
    # syscall audit sees the same calls in a different order, which it does not
    # record. The sixteen below are three requirements.
    2: {
        "d01_resolution": {
            # (a) The order the list ends on. `deep-tail.test` comes back as
            # ::1, 127.0.0.1, 127.99.99.2, 127.99.99.3, 127.99.99.4,
            # 2001:db8::3; interleaved it is ::1, 127.0.0.1, 2001:db8::3,
            # 127.99.99.2, 127.99.99.3, 127.99.99.4. The resolver's list ends on
            # a documentation address with no route and the interleaved one ends
            # on a loopback address with nothing listening, so the word for the
            # failure moves with the order. `dual.test` is the control beside
            # it: one address per family, so interleaving cannot reorder it and
            # its cell does not move.
            "deep/no-server/tail-last-error":
                "if every attempt fails, the failure reported is that of the"
                " attempt furthest down the attempt order, which is now the"
                " interleaved order rather than the resolver's",

            # (b) The load generator races. `redis-benchmark` starts its
            # connection and lets its event loop finish it, so at step 1 it took
            # the one address it could start an attempt on and reported what
            # that address did. Six names it could not reach that way it now
            # reaches: three where the server is on the IPv4 address of a
            # dual-stack name whose IPv6 address comes first, and three where
            # the server is deeper in the list than the first attempt.
            "redis-benchmark/srv-v4/localhost/rc":
                "a caller that does not wait for its connection races too, so"
                " the load generator reaches a server on an address that is not"
                " the first the name resolves to",
            "redis-benchmark/srv-v4/localhost/reached":
                "and the server it claimed to drive really did receive the"
                " writes, which is what separates connecting from printing",
            "redis-benchmark/srv-v4/dual/rc":
                "the same requirement through a name with exactly one address"
                " of each family",
            "redis-benchmark/srv-v4/dual/reached":
                "and the same proof that the run reached the server",
            "redis-benchmark/srv-v4/multi/rc":
                "the same requirement through a five-address name",
            "redis-benchmark/srv-v4/multi/reached":
                "and the same proof that the run reached the server",
            "redis-benchmark/srv-deep/multi/rc":
                "the same requirement where the server is on the third address"
                " of the name rather than the second, so no single attempt"
                " could have found it",
            "redis-benchmark/srv-deep/multi/reached":
                "and the same proof that the run reached the server",
            "redis-benchmark/deep-multi/self/rc":
                "the same requirement at the deep name section 3b puts a server"
                " in, reached only because the attempts overlap",
            "redis-benchmark/deep-multi/self/reached":
                "and the same proof that the run reached the server",
            "redis-benchmark/deep-tail/self/rc":
                "the same requirement at the second deep list shape, whose"
                " server is on the last address of the interleaved order",
            "redis-benchmark/deep-tail/self/reached":
                "and the same proof that the run reached the server",

            # (c) The replication link races. This is step 1's second "must not
            # link" case and the step-1 reason for it -- that the link does not
            # wait, so it cannot walk a list -- is exactly what step 2 removes.
            # The master is on the last address of the interleaved order and
            # every address before it refuses at once, so the race reaches it in
            # milliseconds rather than in the 8 s the cell allows.
            "repl/v4-deep-master-tail-name/link-up":
                "a master whose name's earlier addresses do not answer must"
                " still be linked to: the link a replica opens to its master"
                " races the addresses rather than taking the first one it can"
                " start an attempt on",
            "repl/v4-deep-master-tail-name/master-slave-count":
                "the master therefore counts the replica, which is what"
                " separates a link the replica believes in from one that"
                " carries data",
            "repl/v4-deep-master-tail-name/replicated-key":
                "and the key written on the master before the link was asked"
                " for arrives on the replica",
        },
    },
    # Step 3 changes how an address is *written*, so its flips are wherever the
    # corpus already read a string that carries an address and a port. There is
    # one requirement behind almost all of them -- the join is bracketed -- and
    # it reaches this far because the baseline wrote that string in eleven
    # places and the corpus was recorded before any of them moved. Two entries
    # are not that requirement and are called out where they appear.
    #
    # The five `d03`/`g05` surfaces are not here: those scenarios are recorded
    # wholesale from this step's golden tree (`flip_from=3`), which is the
    # registry's record of the same exception.
    3: {
        # The listener's own error line names the address it could not take.
        # Four of these five are a host the operator wrote, and the fifth is a
        # scope identifier this step otherwise says nothing about: all five are
        # text with a colon in it being joined to a port, which is the whole of
        # the rule.
        "c08_listeners": {
            "listen/scoped-loopback/log":
                "where an address and a port appear as one string the host is"
                " bracketed if its text contains a colon, whatever the text"
                " turned out to denote",
            "refuse/bracketed-literal/log":
                "the same rule applied to the text the operator supplied,"
                " which is not re-spelled on the way into the join",
            "refuse/port-taken/log":
                "the same rule where the address is one the tree resolved the"
                " operator's argument to",
            "refuse/unassigned-v6/log":
                "the same rule at a documentation address no interface holds",
            "refuse/v4-mapped/log":
                "the same rule at a v4-mapped address, whose text has colons"
                " in it and is therefore bracketed like any other",
        },
        # The shipped client's connect-failure message names the host it was
        # given, joined to the port it was given.
        "c09_cli": {
            "cli/connect-refused-v6/out":
                "the connection-failure message the shipped client prints"
                " brackets the host it names",
            "cli/negative-port/out":
                "the same message where the port was rejected rather than the"
                " host, so the join still happened",
            "cli/non-numeric-port/out":
                "the same message where the port argument was not a number",
            "cli/ping-bracketed-host/out":
                "the operator's own text is not re-spelled before it is"
                " joined, so a host that already carries brackets is bracketed"
                " as the text it is",
            "cli/ping-scoped-host/out":
                "the same rule at a host carrying a scope identifier",
        },
        # A replica's log names its master.
        "c10_replication": {
            "repl/slave-log":
                "a log line that names a peer writes the address and the port"
                " as one bracketed string",
            "repl/slave-log-after-master-loss":
                "the same line after the master went away, which is where a"
                " tree that formats in two places shows it",
        },
        # The cluster is where the string is not only printed but *stored* and
        # *read back*: the redirection errors a client follows, the rows of
        # `CLUSTER NODES`, and the rows of the configuration file a node writes
        # for itself.
        "c11_cluster": {
            "cluster/ask-redirect":
                "the redirection errors a cluster node replies with name the"
                " target as one bracketed string",
            "cluster/cli-plain-out":
                "the same redirection as the shipped client prints it",
            "cluster/importing-without-asking":
                "the same rule at the redirection a node importing a slot"
                " replies with",
            "cluster/multikey-same-tag":
                "the same rule at the redirection for a multi-key command",
            "cluster/read-from-replica":
                "the same rule at the redirection a replica replies with",
            "cluster/set-bravo":
                "the same rule at a redirection for a key in another node's"
                " slot range",
            "cluster/set-delta": "the same rule, another slot",
            "cluster/set-echo": "the same rule, another node",
            "cluster/set-golf": "the same rule, another slot on that node",
            "cluster/set-{tag}one":
                "the same rule where the slot was chosen by a hash tag",
            "cluster/set-{tag}two":
                "the same rule for the second key of that tag",
            "cluster/nodes-lines":
                "the rows of `CLUSTER NODES` carry the address and the port as"
                " one bracketed string",
            "cluster/nodes-conf":
                "so do the rows of the configuration file the node writes for"
                " itself",
            "cluster/nodes-conf-replica":
                "the same file as the replica writes it",
            "cluster/slaves-of-master":
                "and the rows `CLUSTER SLAVES` replies with",
            "cluster/peer-fields":
                "the address column of the node table, read from the node that"
                " holds the lower half of the slot space",
            "cluster/peer-fields-node1":
                "the same column as the second node sees it, which is the"
                " address it learned over gossip rather than the one it was"
                " configured with",
            "cluster/peer-fields-replica":
                "the same column as the replica sees it",
            "cluster/restart-peer-fields":
                "the same column after a restart, so the rows the node wrote"
                " are rows it can read back",
            "cluster/probe-peer-fields":
                "the same column across the spellings a node can be met at,"
                " including the ones with no host and with an IPv4 host, which"
                " have no colon in them and are therefore not bracketed",
            "cluster/meet-bracketed":
                "the address `CLUSTER MEET` names back in its complaint is the"
                " operator's own text joined as the text it is, so a host that"
                " already carries brackets is bracketed again: the join rule"
                " reads the text and not what the text turned out to denote",
            "cluster/meet-scoped":
                "a zone identifier leaves a colon in the host text, so the join"
                " brackets it, and the tree acquires no opinion about scopes by"
                " doing so",
            "cluster/meet-negative-port":
                "the same diagnostic, reached by a port the handshake rejects"
                " rather than by a host it cannot use; the host is an address"
                " and is bracketed",
            # Not the writing half. This pair is the redirect *round trip*:
            # the client parses what the node wrote and reissues the command at
            # the node that issued it, which it could not do at the baseline
            # because the string it was handed had no unambiguous delimiter.
            "cluster/cli-follow-redirects-out":
                "the shipped client, in cluster mode, follows a redirection it"
                " is given to the node that issued it and reissues the command"
                " there",
            "cluster/cli-follow-redirects-rc":
                "and therefore exits 0 rather than failing to parse the target",
        },
        "c12_sentinel": {
            "sentinel/old-master-slaveof-log":
                "the log lines a demoted master writes name their peer as one"
                " bracketed string",
        },
        # The shipped load generator's connect-failure message.
        "c13_benchmark": {
            "bench/closed-port-bracketed/out":
                "the connection-failure message the shipped load generator"
                " prints brackets the host it names, and does not re-spell the"
                " operator's own text on the way in",
            "bench/host-bracketed/out":
                "the same message where the host was reachable text rather"
                " than a closed port",
            "bench/host-scoped/out":
                "the same message at a host carrying a scope identifier",
        },
        "c15_cli_modes": {
            "cli/latency-refused/shape":
                "the client's connection-failure message is the same string in"
                " its non-interactive modes",
            "cli/slave-refused/shape": "the same message in replica mode",
            "cli/stat-refused/shape": "the same message in stat mode",
        },
        "c16_sentinel_failure": {
            "failover/info/masters":
                "the address field of `INFO sentinel` carries the address and"
                " the port as one bracketed string",
            "failover/info/masters-all":
                "the same field in the reply that carries every section",
            "failover/info/masters-sentinel":
                "the same field as the second sentinel reports it",
            "failover/revived/slaveof-log":
                "the log lines a revived replica writes name their master as"
                " one bracketed string",
        },
        "c17_cluster_failover": {
            "cfail/blocked-client-redirect":
                "the redirection a client blocked across a failover is given"
                " names its target as one bracketed string",
            "cfail/moved-to-new-master":
                "the same rule at the redirection to the promoted node",
            "cfail/nodes-conf-after":
                "the rows of the configuration file the node writes after a"
                " failover carry the address and the port as one bracketed"
                " string",
            "cfail/peer-fields-after":
                "the address column of the node table after a failover",
            "cfail/forgotten-peer-fields":
                "the same column after a node was forgotten",
            "cfail/slaves-of-master":
                "the rows `CLUSTER SLAVES` replies with before the failover",
            "cfail/slaves-of-new-master":
                "and the rows it replies with for the promoted node",
        },
    },
    # Step 4 is mostly a step the older scenarios cannot see: the corpus runs
    # on a host that has IPv4, and what changes is what happens on one that does
    # not. Two things it changes *are* visible without the filter, and both come
    # from the same edit -- the compiled-in default host of the two shipped
    # tools stops being an IPv4 literal.
    4: {
        "c09_cli": {
            "cli/help/out":
                "nothing this repository ships names an IPv4 address as a place"
                " it reaches, and the client's default host is printed in its"
                " own usage text",
        },
        "c13_benchmark": {
            "bench/usage/out":
                "the same requirement in the load generator's usage text,"
                " which names its default host too",
            "bench/missing-host-argument/out":
                "the same text, reached by the other path that prints it",
            # And the consequence, which is the half that says the default was
            # really changed rather than only re-worded: this scenario's server
            # listens on ::1 alone, so a run with no `-h` at all reaches it only
            # if the default resolves there.
            "bench/host-default/rc":
                "the shipped tools reach something by default, and the default"
                " is a host this tree can still connect to, so the run exits 0"
                " instead of refusing the connection",
            "bench/host-default/out":
                "the same run therefore prints a rate rather than `Writing to"
                " socket: Connection refused`",
            "bench/host-default/dbsize":
                "and the server it claimed to drive really did receive the"
                " write, which is what separates connecting from printing",
        },
    },
    # Step 5 is a step whose subject the older scenarios reach in exactly two
    # places: the three commands that take the address of *another machine*.
    # `anetAddrIsPeer` now stands in front of `clusterStartHandshake`,
    # `slaveofCommand` and `createSentinelAddr`, so an address that cannot be
    # one machine is refused there; and `anetAddrIsEqual` now compares mapped
    # and IPv4 spellings by value, which changes what the node table holds.
    # Everything else the step does is in `d05` and `g07`, which are recorded
    # from the step-5 golden tree wholesale.
    #
    # `sentinel/peer-after-failover/log-monitor` is the same refusal read a
    # second time, out of a log rather than out of a reply.
    # `cluster/meet-v4-literal` does not move, because a mapped handshake
    # address and the IPv4 literal that was met resolve to the same node
    # either way.
    5: {
        "c11_cluster": {
            "cluster/meet-unspecified":
                "a peer is named by a unicast address and nothing else is"
                " accepted as one, so `CLUSTER MEET ::` is refused with the"
                " class in the message instead of creating a node",
            "cluster/meet-multicast":
                "the same requirement at a multicast group, which by"
                " definition is more than one interface and is never where a"
                " single node is",
            "cluster/meet-v4-compatible":
                "the same requirement at a deprecated IPv4-compatible address"
                " (RFC 4291 section 2.5.5.1), which is not the IPv4 address it"
                " resembles and is not anywhere",
            "cluster/probe-peer-fields":
                "the node table no longer holds a row for any of the three"
                " addresses above, and holds one row rather than two for the"
                " IPv4 literal and its mapped spelling, because identity is"
                " decided by value",
        },
        "c12_sentinel": {
            "sentinel/monitor-unspecified":
                "a master is reached at a unicast address, so"
                " `SENTINEL MONITOR` on the unspecified address is refused"
                " with the class in the message",
            "sentinel/monitor-unspecified-stored":
                "and therefore nothing is stored for it",
            "sentinel/monitor-unspecified-removed":
                "and `SENTINEL REMOVE` for it answers that there is no such"
                " master, rather than removing the one that was created",
            "sentinel/peer-after-failover/log-monitor":
                "the same refusal seen in the log: the `+monitor` line for the"
                " unspecified address is not written, because the monitor was"
                " never created",
        },
    },
    6: {
        "c08_listeners": {
            "listen/no-bind/probe-v4":
                "a server that was not told what to bind creates no `AF_INET`"
                " socket, so the IPv4 probe is refused where it used to be"
                " answered -- this is the default itself, seen from outside",
            "argv/help-out":
                "the server's own usage text no longer offers"
                " `--slaveof 127.0.0.1 8888` as an example of what this tree"
                " does; it still documents the option",
        },
        "c13_benchmark": {
            "bench/usage/out":
                "the benchmark's usage block no longer offers `192.168.1.1`"
                " or `127.0.0.1:6379`; every option it documented is still"
                " documented",
            "bench/missing-host-argument/out":
                "the same usage block, printed by a second failing invocation",
        },
        "d01_resolution": {
            "bind/no-bind/probe-v4":
                "the same default, read through the scenario that owns"
                " resolution: absent a `bind` directive there is no IPv4"
                " listener to reach",
        },
        "g02_family_literals": {
            "gate/backstop/conf-directives-lost":
                "the shipped `redis.conf` comes up on the new default, so its"
                " `bind` line is no longer active and the pin's `bind`"
                " directive is legitimately lost. Exactly one entry: the"
                " backstop still names every other directive a candidate"
                " switches off, and `conf-missing` still sees a deleted file",
        },
        "g06_no_ipv4_kernel": {
            "gate/no4/audit/by-file/redis.conf":
                "the same deactivated directive, seen as a destination site."
                " The pin recorded three site groups in `redis.conf` and the"
                " live single-address one -- `bind 127.0.0.1`, which step 4"
                " corrected to `bind localhost` -- has no live line of its own"
                " once the directive is commented out. The two annotated"
                " example groups are still satisfied, and the quad count"
                " beside it is still 0, so the pair goes from `[0, true]`"
                " at step 5 to `[0, false]` here: the count did not move and"
                " the site flag did",
        },
    },
}


def for_step(step):
    """{scenario: {observation: reason}} introduced at exactly `step`."""
    return FLIPS.get(step, {})


def upto(step):
    """{scenario: {observation: source_step}} in force at `step`.

    A later step may flip an observation an earlier step already flipped -- the
    address text in a log line changes at step 3 whether or not step 1 changed
    which log lines there are -- so the latest source wins.
    """
    out = {}
    for k in sorted(FLIPS):
        if k > step:
            break
        for scenario, obs in FLIPS[k].items():
            for name in obs:
                out.setdefault(scenario, {})[name] = k
    return out
