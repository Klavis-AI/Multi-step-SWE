"""One address, one spelling: every address this tree writes down is canonical.

Two halves, because the property has two halves and neither instrument can see
the other's.

The static half asks where an address is *joined to a port*. `"%s:%d"` is not a
macro -- it survives the preprocessor intact -- so this is an AST question, and
`fmtscan` answers it over the same `.i` units `gate.build_dir()` already makes
once per verifier run. What is scored is a **count per file**, not a site list:
`g02` can afford `gate.sites()` because its finding set on a finished tree is
empty and an empty list carries no line numbers, while this one's is not, and
pinning line numbers would fail an honest candidate for editing the line above.
One of the counts is deliberately not zero. `src/redis.c` holds

    "Redis server v=%s sha=%s:%d malloc=%s bits=%d build=%llx"

which joins a git sha to a dirty flag and is not an address at all, so the
finished tree still has one. A candidate who satisfies this gate by deleting
every colon-joined literal in sight breaks the version banner and fails here.

The per-file counts are compared against zero, which is a floor and a ceiling
at once. The two totals and the one file allowed to keep a join are compared
as a *ceiling* instead -- `_places()` says why: a count cannot tell "wrote the
join once, as a helper call with no literal left" apart from "never did the
work", and it reads the first as the second.

The behavioural half asks what the running server actually *prints*. A tree can
pass every static count by routing all its formatting through one helper and
still hand out `0:0:0:0:0:0:0:1`, because canonicality is a property of the
bytes the formatter chose and not of where the format string lives. So the
second half starts a server, a replica, a two-node cluster and a sentinel, and
reads back the structured places this product writes an address into a reply or
a file. Each one is checked against `inet_ntop(inet_pton(text))` -- round-trips
*and is byte-identical*, which is what "canonical" means and what "parses" does
not.

The log is deliberately not one of those places. Which words surround an
address in a log line is the tree's business, and a scan that had to recognise
them would fail a candidate for rewording a message; the sites that write those
lines are counted by the static half instead, where rewording does not matter.

Every instrument here is scored beside its finding. The scanner's faults and
blind spots, the number of strings each surface yielded, the surface list
itself, and a fixture table of hand-written spellings with the verdict the
checker gives each one are all observations. An empty defect list read off a
checker that had stopped working would otherwise look exactly like a finished
tree, and a surface that collected nothing would look like a surface with
nothing wrong in it.
"""

import os
import re
import socket
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import build  # noqa: E402
import ctx as ctxmod  # noqa: E402
import fmtscan  # noqa: E402
import gate  # noqa: E402
import srv  # noqa: E402
import textscan  # noqa: E402

PORTS = 6
FAMILY = "gate"

SLOTS = 16384
MASTER = "canonmaster"

# Deadlines. The behaviour half of this gate stands up a replica, a two-node
# cluster and a Sentinel purely so that there are live surfaces to read the
# address text off, and every wait below is a wait for one of those to finish
# arranging itself. None of them measures the tree: the tree is measured by
# what the surfaces then say. So a wait that expires is a fact about the
# machine, and it is recorded as `"timeout"` rather than as a negative answer
# -- and the deadlines are not to be shortened to make a slow run pass, since
# that would turn an unusable observation back into a plausible one.

# The cluster is two nodes that have to agree, first that they have met and
# then about the slot map, over gossip rather than over the connection the
# command arrived on.
CLUSTER_DEADLINE = 45.0
# Sentinel learns the replica from the master's own `INFO`, on its own refresh
# timer, and `known-slave` cannot exist before it does.
SENTINEL_SLAVE_DEADLINE = 30.0
# And then it rewrites its configuration file, which is where the
# `sentinel-conf/*` samples are read from. The rewrite follows the discovery,
# so this is spent after the deadline above rather than inside it.
SENTINEL_REWRITE_DEADLINE = 10.0
# The gap between polls in all three loops: short enough that the deadline is
# what ends the wait, long enough that polling is not itself the load on a node
# that is busy agreeing about slots.
POLL_INTERVAL = 0.2

# The binaries whose `.rodata` is read back. Six are built; these four are the
# ones that format an address for a human or a peer.
ARTIFACTS = ("redis-server", "redis-cli", "redis-benchmark", "redis-sentinel")

# Shipped programs the C compiler never sees. `redis-trib.rb` is the cluster
# administration tool the repository ships and runs; the tcl suite is the
# project's own test driver. Both join hosts to ports in their own syntax, and
# neither appears in any `.i`.
SCRIPT_EXT = (".rb", ".tcl", ".sh", ".py")

# `"#{host}:#{port}"` and `"%s:%d" % (h, p)` -- the same shape as the C one, in
# the two spellings these files use.
SCRIPT_JOIN = re.compile(r"#\{[^}\n]+\}:#\{[^}\n]+\}"
                         r"|%s:%[sd](?![A-Za-z0-9_])")

# The receiver is captured because who is being split decides whether the split
# is address work: `source_addr.split(":")` is, `e.split(":")` inside a loop
# over an INFO line's `k:v` pairs is not. The group is optional so that a split
# with no named receiver still matches and reads as unnamed rather than as
# absent.
SCRIPT_SPLIT = re.compile(r"([A-Za-z_@][\w@\[\]:.]*)?"
                          r"(?:\.split\(\s*[\"']:[\"']\s*\)"
                          r"|\.rindex\(\s*[\"']:[\"']\s*\))")

# Is this text about an address? The C half asks the same question of every
# identifier in an expression and answers it on the *end* of the name, because
# C names an address `cip` or `slave_master_host` and the suffix is where the
# meaning is. A script interpolates `#{@info[:host]}` and `#{opts[:srchost]}`,
# where the word sits in the middle of the token, so the test here is on the
# substring and not on the suffix. The words are `fmtscan.ADDRESSY_SUFFIX` with
# the two the scripts add: a Ruby tool talks about `peer` and `node` where the
# C talks about `ip`.
#
# There is no denylist of English words ending in "ip" to go with it, and the
# reason is what this pattern is asked about: the interpolation that was
# matched, or the receiver of the split, rather than every identifier on the
# line. The exception is the sprintf join below, where the line is the only
# haystack there is, and a line about a `zip` and a port would be counted. No
# shipped script has one.
SCRIPT_ADDRESSY = re.compile(r"addr|address|host|ip|peer|node", re.I)

# A line that has already done the work is not a finding on that line. Two
# shapes say so: a bracket against the interpolation, which is the RFC 3986
# authority being written, and a test for a colon in the host, which is the
# decision that chooses whether to write one. A migrated `format_addr` is one
# line carrying both, and counting the `"#{host}:#{port}"` arm of it would be
# counting the IPv4 branch of the file's own formatter -- the reading the C
# half avoids by knowing from `gate.FOCUS_FILES` where the formatter lives,
# which for a file that is both the formatter and every caller of it is not
# available.
#
# `rindex(":")` appears here and in the split pattern above, and a line holding
# one is read as guarded. That is deliberate: ending the address at the last
# colon is the RFC 3986 parse rule for an unbracketed authority, so a file that
# reached for it has answered the question rather than begged it.
SCRIPT_GUARD = re.compile(
    r"\[#\{"                              # "[#{host}]:#{port}"
    r"|\[%s\]"                            # sprintf("[%s]:%s", host, port)
    r"|\.index\(\s*[\"']:[\"']\s*\)"    # host.index(":") ? ... : ...
    r"|\.include\?\(\s*[\"']:[\"']\s*\)"
    r"|\.count\(\s*[\"']:[\"']\s*\)"
    r"|\.rindex\(\s*[\"']:[\"']\s*\)"
    r"|=~\s*/[^/\n]*:")                   # addr =~ /:/

# Whether a script handles the RFC 3986 form at all, in each direction: does it
# *write* a bracketed authority, and does it *look for* the closing bracket
# before the port separator. Both are coarse on purpose, and both are recorded
# and neither decides anything. Zeroing the site count beside them for a file
# that answered yes twice would rest on the argument that a text scan cannot
# tell a bracket-aware file's remaining joins from the IPv4 branch of its own
# formatter. It cannot, file-wide -- but the question is per line, and
# `SCRIPT_GUARD` is where it is asked: the formatter's arms sit on a line
# that brackets or tests for a colon, and the callers' do not. A boolean over
# the whole file could be set by a string in a comment or in dead code, and a
# count that a dead string can zero is a count that measures the string.
#
# Which idiom the author reached for -- `"#{h}:#{p}"` inside a ternary, or
# `h + ":" + p.to_s` -- is still not something the step asks for, so it is
# still not something these two read. A bare `"["` is not one of their shapes
# either: `redis-trib.rb` tests for it twice already, on the `[slot-...`
# markers of a migrating slot, and a cell that read those as address work would
# start green and stay green.
SCRIPT_WRITES_BRACKET = re.compile(
    r"\[#\{[^}\n]+\}\]"                  # "[#{host}]:#{port}"
    r"|\[%s\]"                             # sprintf("[%s]:%s", host, port)
    r"|[\"']\[[\"']\s*(?:\+|,|\))")       # "[" + host, or f("[", ...)
SCRIPT_READS_BRACKET = re.compile(
    r"\\\]"                               # /\A\[(.+)\]:([0-9]+)\z/
    r"|[\"']\]:?[\"']")                    # addr.index("]"), split("]:")

# Emitted whether or not the sweep finds anything in them, for the reason
# `gate.FOCUS_FILES` names its files rather than discovering them: a surface
# that stops reporting because it stopped existing has to read as a change.
# `redis-trib.rb` is the cluster tool the step puts in scope; `redis-copy.rb`
# is an example under `utils/` that the step leaves alone, and is here as the
# regression half of the same cell.
SCRIPT_FILES = ("src/redis-trib.rb", "utils/redis-copy.rb")

# Every structured place the running product writes an address. Named here
# rather than discovered from what the sweep happened to find, so that a
# surface which yields nothing is an observation reading zero rather than an
# observation that quietly stopped existing.
#
# A host surface is a field that holds an address on its own; an authority
# surface is a field that holds an address and a port in one string, where
# RFC 3986 bracketing is half the question.
HOST_SURFACES = (
    "cluster-slots",
    "info-replication/master-host",
    "info-replication/slave-row",
    "role/master-view",
    "role/replica-view",
    "sentinel-conf/known-slave",
    "sentinel-conf/monitor",
    "sentinel-get-master-addr",
    "sentinel-masters",
    "sentinel-slaves",
)

AUTHORITY_SURFACES = (
    "client-list/addr",
    "cluster-nodes",
    "info-sentinel/address",
    "nodes-conf",
    "proctitle",
)

# The checker's known answers. Most of them are deliberately wrong spellings:
# a fixture table that only held canonical text would read the same whether the
# checker worked or always said yes.
HOST_FIXTURES = (
    "::1",
    "0:0:0:0:0:0:0:1",
    "::0:1",
    "::0001",
    "2001:0db8::1",
    "2001:db8:0:0:0:0:0:1",
    "2001:DB8::1",
    "1:0:0:0:2:0:0:0",
    "::ffff:127.0.0.1",
    "fe80::1%eth0",
    "127.0.0.1",
    "127.000.000.1",
    "localhost",
    "",
)

AUTHORITY_FIXTURES = (
    "[::1]:6379",
    "::1:6379",
    "[0:0:0:0:0:0:0:1]:6379",
    "[::1]",
    "[::1:6379",
    "[::1]x6379",
    "127.0.0.1:6379",
    "localhost:6379",
    "",
)


# -- the checker ----------------------------------------------------------
def verdict(text):
    """How this text stands as the printed form of one address.

    `zone-id` is called out rather than failed: a scope identifier is step 5's
    subject, and this gate calling one a defect would put a later step's
    requirement into this step's score.
    """
    if not text:
        return "empty"
    if "%" in text:
        return "zone-id"
    for fam in (socket.AF_INET6, socket.AF_INET):
        try:
            packed = socket.inet_pton(fam, text)
        except (OSError, ValueError, UnicodeEncodeError):
            continue
        return ("canonical" if socket.inet_ntop(fam, packed) == text
                else "non-canonical")
    return "not-an-address"


def authority_verdict(text):
    """How this text stands as an RFC 3986 host-and-port.

    The bracket prefix is half the finding: `[::1]:6379` is an authority and
    `::1:6379` is a string whose last colon is ambiguous with the address's
    own. Whether the host inside is canonical is the other half, and the two
    are reported together so a tree that fixed one and not the other scores for
    the one it fixed.
    """
    if not text:
        return "empty"
    if text.startswith("["):
        end = text.find("]")
        if end < 0:
            return "unterminated-bracket"
        rest = text[end + 1:]
        if rest and not (rest.startswith(":") and rest[1:].isdigit()):
            return "trailing-junk"
        return "bracketed/" + verdict(text[1:end])
    if text.count(":") > 1:
        return "unbracketed-v6/" + verdict(text.rsplit(":", 1)[0])
    host = text.rsplit(":", 1)[0] if ":" in text else text
    return "bare/" + verdict(host)


def host_ok(v):
    return v == "canonical"


def authority_ok(v):
    """An unbracketed authority is correct only when its host has no colon."""
    return v in ("bracketed/canonical", "bare/canonical")


# -- reading replies ------------------------------------------------------
def _s(x):
    if isinstance(x, (bytes, bytearray)):
        return x.decode("utf-8", "replace")
    return x


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


def _info(client, section):
    r = client.cmd("INFO", section)
    return _s(r[1]) or "" if r[1] is not None else ""


def _info_field(text, field):
    for line in (text or "").splitlines():
        if line.startswith(field + ":"):
            return line.split(":", 1)[1].strip()
    return None


def _kv_field(blob, key):
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


def _directives(port, extra=()):
    return ctxmod.default_directives(port, "::1", extra=extra)


def _cluster_field(client, field):
    return _info_field(_s(client.cmd("CLUSTER", "INFO")[1]) or "", field)


def _upshot(got):
    """A readiness wait's own answer: `True`, or `"timeout"` where it was not.

    `srv.wait_info` returns `False` on expiry, and `False` has the shape of an
    answer: a reader, and a diff against a recording, takes it for the tree
    having settled into the wrong state. It never means that here -- every wait
    in this file is for something the harness asked for a moment earlier, so an
    expiry says only that the harness stopped looking, which is what this
    package spells `"timeout"`.
    """
    return True if got else "timeout"


def _wait_cluster(client, field, want, deadline=CLUSTER_DEADLINE):
    """`True`, or `"timeout"` if the field never read `want` in time.

    Never `False`, for the reason `_upshot` gives: this loop cannot observe a
    negative. It sees the field reach the value or it sees the clock run out.
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


# -- collecting what a running tree writes --------------------------------
class Sweep(object):
    """Address text, filed by the surface that produced it."""

    def __init__(self):
        self.hosts = dict((s, []) for s in HOST_SURFACES)
        self.authorities = dict((s, []) for s in AUTHORITY_SURFACES)

    def host(self, surface, *texts):
        self.hosts[surface].extend(t for t in texts if t is not None)

    def authority(self, surface, *texts):
        self.authorities[surface].extend(t for t in texts if t is not None)


def _sweep_plain(sw, server, client):
    for line in (_s(client.cmd("CLIENT", "LIST")[1]) or "").splitlines():
        for field in line.split():
            if field.startswith("addr="):
                sw.authority("client-list/addr", field[5:])
    title = _read("/proc/%d/cmdline" % server.proc.pid).replace("\0", " ")
    for tok in title.split():
        if ":" in tok and not tok.endswith(".conf"):
            sw.authority("proctitle", tok)


def _sweep_replication(sw, master_c, slave_c):
    rep = _info(slave_c, "replication")
    sw.host("info-replication/master-host", _info_field(rep, "master_host"))
    sw.host("info-replication/slave-row",
            _kv_field(_info_field(_info(master_c, "replication"), "slave0"),
                      "ip"))
    role = slave_c.cmd("ROLE")
    if role[0] == "array" and role[1] and len(role[1]) > 1:
        sw.host("role/replica-view", _s(role[1][1][1]))
    mrole = master_c.cmd("ROLE")
    if mrole[0] == "array" and mrole[1] and len(mrole[1]) > 2:
        for row in (mrole[1][2][1] or []):
            if row[0] == "array" and row[1]:
                sw.host("role/master-view", _s(row[1][0][1]))


def _sweep_cluster(sw, nodes, clients):
    sw.authority("cluster-nodes",
                 *_nodes_addrs(_s(clients[0].cmd("CLUSTER", "NODES")[1]) or ""))
    slots = clients[0].cmd("CLUSTER", "SLOTS")
    if slots[0] == "array" and slots[1]:
        for rng in slots[1]:
            for entry in (rng[1] or [])[2:]:
                if entry[0] == "array" and entry[1]:
                    sw.host("cluster-slots", _s(entry[1][0][1]))
    for n in nodes:
        sw.authority("nodes-conf",
                     *_nodes_addrs(_read_own(os.path.join(n.dir, "nodes.conf"))))


def _sweep_sentinel(sw, sentinel, sc):
    masters = sc.cmd("SENTINEL", "masters")
    if masters[0] == "array" and masters[1]:
        for inst in masters[1]:
            sw.host("sentinel-masters", _kv_reply(inst).get("ip"))
    addr = sc.cmd("SENTINEL", "get-master-addr-by-name", MASTER)
    if addr[0] == "array" and addr[1]:
        sw.host("sentinel-get-master-addr", _s(addr[1][0][1]))
    slaves = sc.cmd("SENTINEL", "slaves", MASTER)
    if slaves[0] == "array" and slaves[1]:
        for inst in slaves[1]:
            sw.host("sentinel-slaves", _kv_reply(inst).get("ip"))
    sw.authority("info-sentinel/address",
                 _kv_field(_info_field(_info(sc, "sentinel"), "master0"),
                           "address"))
    for raw in _read_own(sentinel.conf).splitlines():
        parts = raw.split()
        if len(parts) >= 4 and parts[0] == "sentinel":
            if parts[1] == "monitor":
                sw.host("sentinel-conf/monitor", parts[3])
            elif parts[1] == "known-slave":
                sw.host("sentinel-conf/known-slave", parts[3])


# -- the scenario ---------------------------------------------------------
# Where this tree's address helpers already live, and so the one focus file
# whose join count the step does not drive to zero. Naming it here does *not*
# pin the join there: a solution that moves the helpers to a file of its own
# leaves this one at zero, which `_places()` reads as "at most one place" just
# as it reads the golden's two literals, and `focus-join-places` and
# `bracketed-join-places` are what would catch the move if it left two places
# behind instead of one.
JOIN_HOME = "src/anet.c"


def _places(n, one=3):
    """How many places join an address to a port, as a ceiling rather than a
    count.

    A count is the wrong question. A tree can write the join once, in
    `anetFormatAddr()`, and spell it as `anetFormatHost()` followed by a
    separate `":%d"` -- so it holds no literal that joins a `%s` to a `%d` at
    all, and every cell that compared a count against the golden's would read
    it as *worse* than the golden, which has the same single place spelled as
    a ternary of two literals on one line. What this gate carries is "written
    in one place and called"; a solution that writes it in one place and
    leaves no literal behind has met that more thoroughly than the golden, not
    less.

    `one` is where the ceiling sits. The default 3 is the focus-set total: one
    place in either spelling, since a ternary is two literals, plus the
    `src/redis.c` version banner, which joins a git sha to a dirty flag and was
    never an address. `one=2` is one file's literals and `one=1` a count of
    files. Above the ceiling the answer is the same word whatever the number,
    because "how many copies" is not something this step grades.

    A ceiling on the total would be a hole if the per-file counts did not
    exist -- two leftovers in two files also sum to 2. They do exist, each is
    compared against zero, and `JOIN_HOME` is the only file exempt.
    """
    return "at-most-one-place" if n <= one else "copies"


def _static(ctx):
    """The AST half: what the compiler saw, counted per file and in total.

    Three join sites survive a finished tree, in two files, and none of them is
    an exception to the rule. `anet.c` holds two, which are the two arms of the
    one expression that chooses between them: `"[%s]:%d"` for a host whose text
    has a colon in it and `"%s:%d"` for one whose text does not, the second
    being how an IPv4 address and a registered name are written. `redis.c`
    holds one, the version banner, which puts a colon between a source revision
    and a build number and has never been an address. Any leftover beyond those
    three is a finding and not a known exception: the join patterns stop at
    `h`, so nothing is counted merely for writing a number too wide to be a
    port after a colon.
    """
    d = gate.build_dir(ctx)
    f, units, _missing = fmtscan.scan(d, gate.scan_dirs(d))
    ctx.emit("gate/fmt/units-scanned", len(units) > 0)
    ctx.emit("gate/fmt/scanner-faults", sorted(f.scanner_faults))
    ctx.emit("gate/fmt/unscanned-sources", sorted(f.unscanned))
    ctx.emit("gate/fmt/parse-errors",
             gate.sites((a, c) for a, _, c in f.parse_errors))

    # A join site, in **either** spelling. Counting only the unbracketed shape
    # would ask whether the literal changed, and the requirement is that the
    # site is gone: a tree that rewrites `"%s:%d"` to `"[%s]:%d"` in place has
    # moved no work into one place, it has copied the new rule to every site it
    # already had -- and the count of the old shape drops to zero and says the
    # file is clean. So the two families are added, and what the cell reports
    # is how many literals in this file join an address to a port at all.
    joins = f.by_file("address_joins", gate.FOCUS_FILES)
    brackets = f.by_file("bracketed_joins", gate.FOCUS_FILES)
    sites = dict((rel, joins[rel] + brackets[rel]) for rel in gate.FOCUS_FILES)
    splits = f.by_file("colon_splits", gate.FOCUS_FILES)
    for rel in gate.FOCUS_FILES:
        # Every focus file but one has to reach zero, and a count compared
        # against zero is a ceiling and a floor at once, so those stay counts.
        # `JOIN_HOME` is the exception and is scored as a ceiling instead.
        if rel == JOIN_HOME:
            ctx.emit("gate/fmt/by-file/%s/join-places" % rel,
                     _places(sites[rel], one=2))
        else:
            ctx.emit("gate/fmt/by-file/%s/join-sites" % rel, sites[rel])
        ctx.emit("gate/fmt/by-file/%s/colon-splits" % rel, splits[rel])
    # The total over the focus set as well: a candidate who moved a defect out
    # of `cluster.c` into a new file of their own leaves every per-file count
    # right and this one wrong. Bucketed, for the reason `_places()` gives.
    ctx.emit("gate/fmt/focus-join-places", _places(sum(sites.values())))
    ctx.emit("gate/fmt/focus-colon-splits", sum(splits.values()))
    # And how many files write the bracketed form. The counts above say how
    # many join sites remain; this says how spread out the ones that remain
    # are, which is the difference between a rule stated once and a rule
    # copied. What is scored is how many files carry one and not which, so a
    # solution is free to put the join wherever it belongs as long as it puts
    # it in one place -- including somewhere the focus list never named, which
    # is why this one count is over every file the scan read rather than over
    # the focus set. Confining it there would have let a second copy of the
    # rule sit in a new file and read as one. Bucketed for the same reason the
    # others are, and with the boundary a file count wants: one file, or none.
    ctx.emit("gate/fmt/bracketed-join-places",
             _places(len(set(a for a, _l, _t in f.bracketed_joins)), one=1))
    # The identity half. Most of these sites have no behaviour a scenario can
    # reach in the time it has -- Sentinel only reconfigures a slave whose
    # master address it disagrees with after a failover timeout has passed --
    # so this is the only place several of them are scored at all, and the
    # reason the step is named for identity as well as for text.
    compares = f.by_file("address_compares", gate.COMPARE_FILES)
    for rel in gate.COMPARE_FILES:
        ctx.emit("gate/fmt/by-file/%s/address-compares" % rel, compares[rel])
    ctx.emit("gate/fmt/focus-address-compares", sum(compares.values()))
    # The artifact half is an instrument check rather than a finding. A source
    # tree with no such literal cannot produce a binary that has one, so every
    # conversion pair in a binary's `.rodata` has to be one the scan also saw
    # in a source literal; a shape that is in the binary and not in the sources
    # means the scan missed units. What is *not* scored is the count, or the
    # strings themselves: those are the wording of this tree's messages, and a
    # candidate who reworded one while migrating it correctly would fail an
    # observation that is not about the migration.
    src_shapes = set()
    for _a, _l, text in f.address_joins:
        src_shapes.update(_join_shapes(text))
    for _a, _l, text in f.bracketed_joins:
        src_shapes.update(_join_shapes(text))
    for b in ARTIFACTS:
        found = fmtscan.strings_in(os.path.join(d, "src", b))
        ctx.emit("gate/artifact/%s/readable" % b, found is not None)
        shapes = set()
        for s in found or ():
            shapes.update(_join_shapes(s))
        ctx.emit("gate/artifact/%s/shapes-not-in-sources" % b,
                 sorted(shapes - src_shapes) if found is not None else None)
    return d, f, units


def _join_shapes(text):
    """Every address/port join shape in `text`, bracketed or not.

    A list rather than a set, because two callers want the count and one wants
    the shapes; and both families, because the question everywhere this is used
    is whether a join is *there*, not which of the two ways it is written.
    """
    return (fmtscan.ADDRESS_JOIN.findall(text)
            + fmtscan.BRACKETED_JOIN.findall(text))


def _script_row(path, rel):
    """`[file, writes-bracket, reads-bracket, unguarded]`, or None if absent.

    `unguarded` is the count of address sites this file still joins or splits
    without deciding anything about brackets first, counted a line at a time. A
    line that brackets its interpolation or tests its host for a colon has made
    the decision and is not a site; of the rest, a join counts when the text
    that matched is named like an address and a split counts when its receiver
    is. The two booleans are recorded beside it and do not gate it -- see
    `SCRIPT_WRITES_BRACKET`.

    A join written with `sprintf` carries no names of its own: `"%s:%d"` is the
    same three characters whatever it is joining. For those the whole line is
    the haystack, which is the loosest reading here and the only one available.
    """
    if not os.path.exists(path):
        return None
    text = _read(path)
    writes = bool(SCRIPT_WRITES_BRACKET.search(text))
    reads = bool(SCRIPT_READS_BRACKET.search(text))
    unguarded = 0
    for line in text.splitlines():
        if SCRIPT_GUARD.search(line):
            continue
        for m in SCRIPT_JOIN.finditer(line):
            hay = m.group(0) if "#{" in m.group(0) else line
            if SCRIPT_ADDRESSY.search(hay):
                unguarded += 1
        for m in SCRIPT_SPLIT.finditer(line):
            if SCRIPT_ADDRESSY.search(m.group(1) or ""):
                unguarded += 1
    return [rel, writes, reads, unguarded]


def _backstop(ctx, d):
    """What the compiler never saw: parked C, and the shipped scripts.

    A `.c` with no `.i` of its own is a blind spot of the AST half -- either a
    file this platform does not build, or one `#include`d into a unit that does
    have one, in which case counting it here reports the same finding twice.
    Twice is the safe direction and once-not-at-all is not, so the rule is the
    simple one and `ae_epoll.c` holding nothing to report is why it costs
    nothing. Headers are left out: they have no `.i` either, and they are read
    by the AST half through the units that include them.
    """
    root = ctx.tree.root
    files = textscan.walk(root)
    parked, scripts = [], []
    for rel in files:
        path = os.path.join(root, rel)
        if not textscan.is_text(path):
            continue
        if rel.endswith(".c"):
            if os.path.exists(os.path.join(d, rel[:-2] + ".i")):
                continue
            n = len(_join_shapes(textscan.scrub_comments(_read(path))))
            if n:
                parked.append([rel, n])
        elif rel.endswith(SCRIPT_EXT):
            row = _script_row(path, rel)
            if row is not None and row[3]:
                scripts.append(row)
    for rel in SCRIPT_FILES:
        if any(r[0] == rel for r in scripts):
            continue
        scripts.append(_script_row(os.path.join(root, rel), rel)
                       or [rel, None, None, None])
    ctx.emit("gate/backstop/parked-join-sites", sorted(parked))
    ctx.emit("gate/backstop/script-address-joins", sorted(scripts))
    ctx.emit("gate/backstop/files-walked", len(files) > 0)

    # A silenced finding is still a finding, and that includes this gate's
    # own: the cheapest way to make a per-file count fall is to park the line
    # behind `#if 0` rather than fix it. Counted per focus file rather than as a set difference, because the
    # file that already has one is the cheapest place to hide the second.
    counts = {}
    for rel, _line, kind in textscan.suppressions(root, gate.FOCUS_FILES):
        counts[(rel, kind)] = counts.get((rel, kind), 0) + 1
    ctx.emit("gate/backstop/focus-suppressions",
             sorted([a, c, n] for (a, c), n in counts.items()))


def _behaviour(ctx, p):
    ctx.emit("gate/canon/self-check/host-fixtures",
             [[t, verdict(t)] for t in HOST_FIXTURES])
    ctx.emit("gate/canon/self-check/authority-fixtures",
             [[t, authority_verdict(t)] for t in AUTHORITY_FIXTURES])

    sw = Sweep()

    master = ctx.server("canon-master", p[0], _directives(p[0]))
    master.start().wait_ready()
    slave = ctx.server("canon-slave", p[1],
                       _directives(p[1], extra=["slaveof ::1 %d" % p[0]]))
    slave.start().wait_ready()
    mc, sl = master.client(), slave.client()
    ctx.emit("gate/canon/setup/link-up",
             _upshot(srv.wait_info(sl, "replication", "master_link_status",
                                   "up")))
    _sweep_plain(sw, master, mc)
    _sweep_replication(sw, mc, sl)

    nodes, ncs = [], []
    for i in (2, 3):
        n = ctx.server("canon-node%d" % i, p[i],
                       _directives(p[i], extra=[
                           "cluster-enabled yes",
                           "cluster-config-file nodes.conf",
                           "cluster-node-timeout 3000"]))
        n.start().wait_ready()
        nodes.append(n)
        ncs.append(n.client())
    ncs[0].cmd("CLUSTER", "MEET", "::1", str(p[3]))
    ctx.emit("gate/canon/setup/cluster-met",
             _wait_cluster(ncs[0], "cluster_known_nodes", "2"))
    half = SLOTS // 2
    ncs[0].cmd("CLUSTER", "ADDSLOTS", *[str(s) for s in range(0, half)])
    ncs[1].cmd("CLUSTER", "ADDSLOTS", *[str(s) for s in range(half, SLOTS)])
    ctx.emit("gate/canon/setup/cluster-ok",
             _wait_cluster(ncs[0], "cluster_state", "ok"))
    _sweep_cluster(sw, nodes, ncs)

    sentinel = ctx.server("canon-sentinel", p[4], [
        "bind ::1",
        "port %d" % p[4],
        "dir .",
        "logfile sentinel.log",
        "daemonize no",
        "sentinel monitor %s ::1 %d 1" % (MASTER, p[0]),
        "sentinel down-after-milliseconds %s 2000" % MASTER,
        "sentinel failover-timeout %s 10000" % MASTER,
    ], binary="redis-sentinel", logfile="sentinel.log")
    sentinel.start().wait_ready()
    sc = sentinel.client()
    # The replica is learned from the master's own INFO, on sentinel's refresh
    # timer. Waiting for it is what makes `known-slave` exist to be read, and
    # the wait is reported so that a tree which never discovers it fails here
    # rather than leaving an empty surface that reads as clean.
    end = time.time() + SENTINEL_SLAVE_DEADLINE
    seen = False
    while time.time() < end and not seen:
        seen = _kv_reply(sc.cmd("SENTINEL", "master", MASTER)).get(
            "num-slaves") == "1"
        if not seen:
            time.sleep(POLL_INTERVAL)
    ctx.emit("gate/canon/setup/sentinel-saw-slave", _upshot(seen))
    # Sentinel rewrites its own configuration when it learns something. The
    # rewrite is what is being read, so it is waited for by its content -- and
    # by the same argument as the wait above it, the waiting is reported. An
    # expiry here removes the `sentinel-conf/known-slave` sample rather than
    # spoiling it, which is worse than it sounds: `_verdicts` would then emit
    # an empty verdict set and a count of zero for that surface, and an empty
    # set of verdicts is indistinguishable from a surface with nothing wrong
    # with it. This cell is where that shows up instead.
    end = time.time() + SENTINEL_REWRITE_DEADLINE
    rewritten = False
    while time.time() < end:
        rewritten = "known-slave" in _read_own(sentinel.conf)
        if rewritten:
            break
        time.sleep(POLL_INTERVAL)
    ctx.emit("gate/canon/setup/sentinel-conf-rewritten", _upshot(rewritten))
    _sweep_sentinel(sw, sentinel, sc)
    return sw


def _verdicts(ctx, sw):
    defects = []
    for surface in HOST_SURFACES:
        got = sw.hosts[surface]
        vs = [verdict(t) for t in got]
        ctx.emit("gate/canon/host/%s" % surface, sorted(set(vs)))
        ctx.emit("gate/canon/count/host/%s" % surface, len(got))
        defects += [[surface, t, v] for t, v in zip(got, vs) if not host_ok(v)]
    for surface in AUTHORITY_SURFACES:
        got = sw.authorities[surface]
        vs = [authority_verdict(t) for t in got]
        ctx.emit("gate/canon/authority/%s" % surface, sorted(set(vs)))
        ctx.emit("gate/canon/count/authority/%s" % surface, len(got))
        defects += [[surface, t, v] for t, v in zip(got, vs)
                    if not authority_ok(v)]
    ctx.emit("gate/canon/defects", sorted(defects))


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]
    d, f, units = _static(ctx)
    _backstop(ctx, d)
    sw = _behaviour(ctx, p)
    _verdicts(ctx, sw)
    ctx.shutdown_all()
    ctx.note("preprocessed units: %d; build rc %s"
             % (len(units), build.load_report(d)["rc"]))
