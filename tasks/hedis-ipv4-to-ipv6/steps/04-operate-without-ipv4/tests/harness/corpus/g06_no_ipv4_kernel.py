"""A kernel with no IPv4 in it, and a repository that names no IPv4 address.

Step 4 asks two things that look like one. The first is about the running
tree: on a host where `socket(AF_INET, ...)` cannot succeed, the shipped
programs still start and still serve. The second is about the repository: none
of the files it ships names an IPv4 address as a place to reach -- not the
default configuration, not the test assets, not the packaging scripts, not the
vendored client's own Makefile. A tree can pass either one alone. One that
hard-codes `::1` everywhere passes the first while still shipping
`bind 127.0.0.1` in `tests/assets/default.conf`; one that edited every file
passes the second and still refuses to start where the family it asks for does
not exist, because the family is chosen from whether the string has a colon in
it rather than from what the resolver returned.

The instrument for the first half is `lib/no4.py`: a seccomp filter that fails
exactly `socket(AF_INET, ...)` with `EAFNOSUPPORT` and leaves every other
family alone. Its known answer is emitted here beside the findings -- including
the unfiltered control, because a box with no IPv4 configured and a working
filter produce the same four observations otherwise, and only one of them is
the question this step asks.

The instrument for the second half is a text audit, and two decisions in it are
load-bearing:

  * comments are *not* stripped. An address commented out rather than corrected
    is still an address written down, and stripping comments would make
    deleting a helper's assignment the cheapest way to pass;
  * the files are selected by what they are for, not by what they contain.
    `README.md` and the annotated prose in a manual page tell a reader about an
    address; `redis.conf` and `create-cluster` go to one. Only the second kind
    is read.

What the audit cannot see is a file deleted rather than fixed, so the file list
is the *pin's* and the number of the pin's files the tree still ships is scored
with the rest. `c14` and `g02` carry the same guard for the suite and for the
shipped configuration, in more detail than a file count: what those two score
is what the suite and the configuration *lost*.

A literal count cannot see an address *deleted* rather than fixed either.
Deletion drives a file's literal count to zero exactly as correction does, and
the instruction says in as many words that they are not the same thing. So the
per-file cell is a pair: the literal count, and whether every *destination
site* the pin's copy had is still a destination site.

A site is a place on a line, not a token in a file. The fixture records one
*site group* per quad-bearing line of the pin's copy: which tokens introduced
the address and at what distance back from it, which followed it and at what
distance forward, every token that followed it on that line, the address's own
token context, what the line began with, and whether the line was live. The
candidate keeps the file's sites when every group can be matched to a line *of
its own* on which a destination-looking token sits where the address sat. A
live group needs a live line, which is what catches a live directive commented
out; a commented-out group will take either, because uncommenting an example is
not this step's business. `textscan.line_site` and `textscan.sites_kept` carry
the design, field by field, and with it a note on the one thing it
deliberately does not catch.

Coarser rules do not ask about a place. A length rule only sees a whole line or
a whole file go, and keeping the line while throwing the destination away --
`# bind ::1`, `-myaddr` dropped from a `socket -server` call, `HOSTS="$HOSTS"`
emptied, `bind` with nothing after it -- would score `[0, true]` on all four,
the first of them paid twice over: a `tests/assets/default.conf` with no live
`bind` starts a server on the wildcard, and `suite-asset/started` and
`suite-asset/answers-v6` both go true for it. Counting the anchor token over
the whole file catches those four and makes two mistakes of its own, one in
each direction: an honest edit elsewhere in the file that touches the same
token takes the cell away from a candidate who corrected every address, and a
destination dropped while a trailing argument slid into its place still leaves
the anchor live with something after it (`sentinel monitor mymaster 6379 2`,
`r slaveof 0`). Asking per line answers both. With the pair in place, a tree
that answers by deletion and a tree that corrects every address but leaves the
listener alone each earn about half of the step, which is what a two-part
instruction should be worth.

The pair is part of the same cell rather than a cell of its own on purpose -- a
separate observation would be a regression cell worth a fraction of a percent,
where this is the progress a deletion would otherwise be paid.

IPv4 stays available to an operator who asks for it by name. That is not a
loose end, it is the step's other half: "IPv6-only" here means the operator
stops being handed IPv4 by default, not that the operator can no longer have
it. The explicit `bind 127.0.0.1` case is adversarial and is scored at every
step this gate runs in, so a tree that passes the audit by deleting IPv4
support fails here.
"""

import collections
import json
import os
import shutil
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import no4  # noqa: E402
import norm  # noqa: E402
import resp  # noqa: E402
import srv  # noqa: E402
import textscan  # noqa: E402

PORTS = 8
FAMILY = "gate"

# The pin's own answer to the audit below, in `tests/fixtures` beside
# `pin-tests` and `pin-conf`.
PIN_SURFACE = os.path.join(HERE, os.pardir, os.pardir, "fixtures",
                           "pin-script-surface.json")

# Every program the project installs, in the mode that reaches no network at
# all. A binary that cannot even print its version on a host without IPv4 has
# gone wrong somewhere no server test would reach, because nothing about
# `--version` should touch a socket.
#
# The names are this tree's. Its rdb checker is `redis-check-rdb`; the 2.x
# spelling `redis-check-dump` names nothing here, and a name that is not there
# runs as rc=127 and prints an error, on every tree, for ever -- an observation
# that reads exactly the same on a tree that ships no binaries at all. That is
# what `present` below is for.
NO_CONNECTION_MODES = (
    ("redis-server", ["--version"]),
    ("redis-sentinel", ["--version"]),
    ("redis-cli", ["--help"]),
    ("redis-benchmark", ["--help"]),
    ("redis-check-rdb", []),
    ("redis-check-aof", []),
)

# How long a server that is going to come up is given. Nothing waits this out:
# `_settle` also returns as soon as the process has exited, which is how the
# cases that are *supposed* to fail end. It is set far above anything measured
# because a number tight enough to be informative is a number that expires on a
# loaded host, and then the recording says a listening tree stopped listening.
SETTLE = 60.0


def _site_group(group):
    """One of the fixture's site groups, or a raise.

    The groups are plain JSON -- dicts of lists of lists -- and `sites_kept`
    reads their fields positionally, so a malformed one would not fail, it
    would answer. Fail closed instead: raising here is caught by the loader
    below, which returns three empty containers, and three empty containers are
    what `pin-surface-files`, `pin-surface-quads` and `pin-surface-sites` make
    visible. A half-loaded fixture would be the one shape nothing reports.
    """
    if not isinstance(group, dict):
        raise TypeError("site group is not an object")
    for field in ("pre", "post", "after", "frag"):
        if not isinstance(group.get(field), list):
            raise TypeError("site group field %r is not a list" % field)
    return group


def _pin_surface(path=PIN_SURFACE):
    """(files, {file: quads}, {file: [site group]}) the pin shipped.

    Three empty ones if the fixture is unreadable, which `pin-surface-files`,
    `pin-surface-quads` and `pin-surface-sites` are there to make visible: an
    audit that lost its fixture reports the same clean zero as a clean tree.
    """
    try:
        with open(path) as f:
            doc = json.load(f)
        return (list(doc["files"]),
                {k: int(v) for k, v in doc["quads"].items()},
                {k: [_site_group(g) for g in v]
                 for k, v in doc["sites"].items()})
    except (OSError, ValueError, KeyError, TypeError, IndexError,
            AttributeError):
        # `AttributeError` is here for a fixture whose "sites" is a JSON
        # list: `.items()` on it raises, and a fixture that failed to load
        # has to read as three empty containers rather than take the
        # scenario down with it.
        return [], {}, {}


def _reachable(host, port, timeout=5.0):
    """Whether a PING gets an answer at this address."""
    try:
        with resp.Client(host, port, timeout=timeout) as c:
            return c.cmd("PING")[1] == b"PONG"
    except Exception:
        return False


def _settle(server, hosts, port, deadline=SETTLE):
    """Wait until any of `hosts` answers, and never raise if none does.

    A scenario that raised here would lose every observation after the wait, so
    a tree that came up on fewer addresses than expected would score the same
    as one that failed to build. The cases below that expect a *failure* rely
    on the same thing from the other side: the second exit condition is the
    process having gone, which is what ends those in well under a second.
    """
    end = time.time() + deadline
    while time.time() < end:
        for h in hosts:
            if _reachable(h, port, timeout=1.0):
                return True
        if server.proc is not None and server.proc.poll() is not None:
            return False
        time.sleep(0.1)
    return False


def _sites_kept(root, rel, records):
    """Whether every destination site the pin's copy of `rel` had is still one.

    One site group at a time, and each of them has to find a line of the
    candidate's file of its own -- a line on which something that could be
    somewhere to go sits where the pin's address sat, introduced or followed by
    what introduced or followed the address there. Not a count over the file:
    the question is about a place on a line, and `textscan` says at length why
    counting was the wrong shape for it.
    """
    return textscan.sites_kept(os.path.join(root, rel), records)


# `2001:db8::/32` as the first four bytes of a packed address, which is what
# the prefix is. The comparison below is against these rather than against the
# characters `2001:db8:` because the characters are not the requirement: a
# candidate may write `2001:DB8::1`, `2001:0db8:0:0::1` or `2001:db8::1` and
# all three are one address, while `# see RFC 3849 (2001:db8::/32)` in a
# sentence, and `2001:db8:not-an-address`, are the characters and no address.
DOC_PREFIX = b"\x20\x01\x0d\xb8"


def _doc_prefix_address(tok):
    """Whether this token is an address inside `2001:db8::/32`.

    Parsed rather than matched, and what comes off the token first is what a
    configuration file wraps an address in: the quotes and trailing punctuation
    of a sentence, the brackets that hold an address away from a port, and the
    prefix length on an example network. Anything `inet_pton` will not read is
    not an address and is not this cell's business.
    """
    t = tok.strip("\"'`,;()<>")
    if t.startswith("["):
        t = t[1:].split("]", 1)[0]
    t = t.split("/", 1)[0]
    try:
        packed = socket.inet_pton(socket.AF_INET6, t)
    except (OSError, ValueError):
        return False
    return packed[:4] == DOC_PREFIX


def _doc_prefix(root):
    """Whether any root `.conf` file writes an example from `2001:db8::/32`.

    The instruction asks for the documentation prefix where an address is an
    example rather than a destination, and nothing else here can see it: the
    audit counts dotted quads, and every non-quad spelling counts the same to
    it. A boolean rather than a count, because a candidate who writes one
    example line where the golden wrote two has done what was asked -- the
    requirement is the prefix, not an arrangement of it.

    An address on a line, and nothing more than that. The golden writes its two
    on commented-out directives (`# bind 2001:db8::100 2001:db8:1::1`,
    `# sentinel announce-ip 2001:db8::1`), but neither the comment nor the
    directive is what was asked for: a candidate who writes the example in a
    comment of another shape has written the example. What was wrong with
    counting the characters is only that an example has to be an address.
    """
    try:
        names = os.listdir(root)
    except OSError:
        return False
    for name in sorted(names):
        if not name.endswith(".conf"):
            continue
        path = os.path.join(root, name)
        if os.path.islink(path) or not os.path.isfile(path):
            continue
        try:
            with open(path, "rb") as f:
                text = f.read().decode("utf-8", "replace")
        except OSError:
            continue
        for line in text.splitlines():
            if any(_doc_prefix_address(tok) for tok in line.split()):
                return True
    return False


def _audit(ctx):
    """The literal audit: no IPv4 address written anywhere the tree acts on."""
    root = ctx.tree.root
    files = textscan.script_surface(textscan.walk(root), root)
    per = collections.Counter(rel for rel, _l, _a
                              in textscan.ipv4_literals(root, files))
    # Over the whole surface of the tree under test, not only the pin's files:
    # this is the cell that sees an address written into a file the candidate
    # added.
    ctx.emit("gate/no4/audit/quads-total", sum(per.values()))

    # An example written from the documentation prefix is the one thing the
    # instruction asks of this text that counting quads cannot see.
    ctx.emit("gate/no4/audit/doc-prefix-examples", _doc_prefix(root))

    pin_files, pin_quads, pin_sites = _pin_surface()
    # Per file as well as in total, over the files the pin carried one in. A
    # tree that cleaned up `redis.conf` and left `tests/` alone has done part of
    # the work, and one all-or-nothing observation scores that as none of it.
    #
    # Each of these is a pair: how many IPv4 literals the file still carries,
    # and whether every destination site the pin's copy had is still a
    # destination site. The second half is what makes the first mean
    # "corrected" rather than "deleted"; it is `None` rather than `True` when
    # the fixture did not load, so a missing fixture cannot answer the question
    # by default.
    present = set(files)
    for rel in sorted(pin_quads):
        entry = pin_sites.get(rel)
        kept = None if entry is None else _sites_kept(root, rel, entry)
        ctx.emit("gate/no4/audit/by-file/%s" % rel, [per.get(rel, 0), kept])
    # A file deleted rather than corrected has a count of zero too. This is what
    # says which of the two happened.
    ctx.emit("gate/no4/audit/pin-files-present",
             sum(1 for rel in pin_files if rel in present))
    # The instrument before the finding: an audit that read nothing reports the
    # same zero as a clean tree, and a fixture that failed to load reports the
    # same zero as a tree that lost no files.
    ctx.emit("gate/no4/audit/pin-surface-files", len(pin_files))
    ctx.emit("gate/no4/audit/pin-surface-quads", sum(pin_quads.values()))
    ctx.emit("gate/no4/audit/pin-surface-sites",
             sum(len(v) for v in pin_sites.values()))


def _tools(ctx, launcher):
    """Every shipped program's no-connection mode, under the filter."""
    for name, args in NO_CONNECTION_MODES:
        # The instrument before the finding, again: a program that is not in
        # the tree and one that is in the tree and broken both exit non-zero
        # with something on stderr, and only one of those is this step's
        # business. It scores in both directions -- a tool the tree stops
        # building is a finding as much as a name the scorer got wrong.
        ctx.emit("gate/no4/tools/%s/present" % name,
                 os.path.exists(ctx.tree.bin(name)))
        rc, out = srv.run_tool(ctx.tree, name, args, timeout=30,
                               launcher=launcher)
        ctx.emit("gate/no4/tools/%s/rc" % name, rc)
        # The text these print is scored where it is the subject -- `c09` for
        # the client's usage, `c13` for the benchmark's, `c06` for the server's
        # version. Here the question is only that the program ran at all on a
        # host without IPv4, and repeating the text would tie this gate to a
        # wording two later steps are still entitled to change.
        ctx.emit("gate/no4/tools/%s/said-something" % name, bool(out))


# Spellings of "every address of this family". They are what an operator gets
# by writing no `bind` line at all, so a `bind` line that carries one of them
# is the deletion this cell exists to refuse, written out in full: the server
# comes up on the wildcard either way. The list answers for the tokens
# `inet_pton` will not read -- `*` is not an address in any notation -- and the
# parse below answers for the ones it will, since `::`, `::0`, `[::]` and
# `0:0:0:0:0:0:0:0` are one address spelled four ways.
BIND_WILDCARDS = ("*", "::", "[::]", "0.0.0.0", "::0", "0:0:0:0:0:0:0:0")


def _bind_argument_names_somewhere(arg):
    """Whether one `bind` argument names a place rather than every place."""
    a = arg.strip().lower()
    if not a or a.startswith("#"):
        return False
    if a in BIND_WILDCARDS:
        return False
    t = a[1:].split("]", 1)[0] if a.startswith("[") else a
    try:
        if socket.inet_pton(socket.AF_INET6, t) == b"\x00" * 16:
            return False
    except (OSError, ValueError):
        pass
    return True


def _live_bind(text):
    """Whether this configuration has a live `bind` line that names somewhere.

    Not read from the audit's fixture on purpose: this is the half of the
    suite-asset cell that has to keep working when the fixture is gone, and it
    is asked of the same bytes the server below was handed.

    "With an argument" was not enough. The pair exists because commenting the
    `bind` out of `tests/assets/default.conf` starts a server on the wildcard
    and must not be a way to pass, and `bind *`, `bind ::` and `bind 0.0.0.0`
    hand the server that same wildcard while keeping a live `bind` line with
    something after it. Redis takes a list, so the line counts when any one of
    its arguments names somewhere -- `bind ::1 ::` says `::1` and is a
    destination; `bind ::` says nothing and is not.
    """
    if not text:
        return False
    for line in text.splitlines():
        words = line.split()
        if not words or words[0].lower() != "bind":
            continue
        if any(_bind_argument_names_somewhere(w) for w in words[1:]):
            return True
    return False


def _conf_from_tree(ctx, rel, port, name):
    """The tree's own configuration file, with only where-to-run appended.

    Nothing in the file is edited: redis takes the last occurrence of a
    directive, so appending `port`, `dir` and `logfile` moves the server into
    this scenario's workspace and leaves every other line -- `bind` above all --
    saying exactly what the tree ships it saying. Whether that line still names
    an IPv4 address is the question; rewriting it here would answer it.
    """
    d = ctx.workspace.sub(name)
    src = os.path.join(ctx.tree.root, rel)
    try:
        with open(src, "rb") as f:
            base = f.read().decode("utf-8", "replace")
    except OSError:
        return None, d, None
    conf = os.path.join(d, "%s.conf" % name)
    # (`srv.write_new`: nothing in the score uid's directory is followed.)
    srv.write_new(conf, base.rstrip("\n")
                  + "\n\n# appended by the scorer: where to run, nothing else\n"
                  + "port %d\ndir .\nlogfile server.log\ndaemonize no\n" % port)
    # The text as well as the file: what the tree's own copy says is the
    # question, and the caller asks it of these bytes rather than reading the
    # tree a second time.
    return conf, d, base


# The suite's own client, asked to reach a server the way the suite reaches one
# when nobody told it where: `proc redis {{server ...} {port 6379} {defer 0}}`
# in `tests/support/redis.tcl`. That default is the address every Tcl test
# client uses unless a test passes one, and no other cell runs it -- `c14`
# runs the *pin's* suite, deliberately, so the tree's corrected helper is
# never the thing executing. `info default` reads the default out of the proc
# rather than out of the file's text, so a tree that moved it somewhere else and
# a tree that corrected it in place are both asked the same question.
PROBE_TCL = """\
source tests/support/redis.tcl
info default redis server h
set r [redis $h %d]
$r set suiteasset yes
puts [$r get suiteasset]
"""


def _tclsh():
    for cand in ("tclsh8.6", "tclsh"):
        p = shutil.which(cand)
        if p:
            return p
    return None


def _tcl_default_host(ctx, port, work, launcher):
    """What the suite's own client does with its own default host, under the filter.

    `yes` when it reached the server and round-tripped a key. On any failure the
    interpreter's own message, cut to its first line and 120 characters: the pin
    answers `couldn't open socket: host is unreachable` here, because its
    default is `127.0.0.1` and the filter will not create the socket. A fixed
    sentinel would have been the same string for ever and would have matched a
    broken probe as readily as a broken tree.
    """
    tcl = _tclsh()
    if tcl is None:
        return "<no tclsh>"
    script = os.path.join(work, "default-host.tcl")
    srv.write_new(script, PROBE_TCL % port)
    # Run from the tree, and source a relative path, so that nothing absolute
    # can reach the emitted value: Tcl puts the name it could not read into the
    # first line of its own error, and an absolute one would be a different
    # string in the recorder and in the verifier. The script's own path appears
    # further down the traceback, which is not kept.
    argv = list(launcher) + [tcl, script]
    try:
        p = subprocess.run(argv, cwd=ctx.tree.root, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=60,
                           preexec_fn=ctx.tree.preexec)
        rc, out = p.returncode, p.stdout
    except subprocess.TimeoutExpired as t:
        rc, out = "timeout", (t.stdout or b"")
    except OSError as e:
        return ("<could not run tclsh: %s>" % e.__class__.__name__)[:120]
    lines = [l.strip() for l in norm.normalize_text(out).splitlines()
             if l.strip()]
    if not lines:
        return "<said nothing>"
    return lines[-1] if rc == 0 else lines[0][:120]


def run(ctx):
    work = ctx.workspace.sub("no4")
    launcher = list(no4.launcher(work))

    # -- the instrument, before anything it is used to find -------------------
    known = no4.check(work)
    for key, value in sorted(known.items()):
        ctx.emit("gate/no4/instrument/%s" % key, value)

    # -- the repository half --------------------------------------------------
    _audit(ctx)

    # Everything below this line is measured through the filter, and
    # `no4.launcher` returns an empty launcher when it could not build one --
    # so a host with no compiler runs the whole rest of this scenario
    # unfiltered, where a server that still binds IPv4 comes up exactly like a
    # server that no longer needs to. Those cells would then read as a finished
    # tree, and read that way in a recording too.
    #
    # Reporting it in a cell would not help. `runner.py`'s `cmd_replay` scores
    # the keys of the *expectation* file against the replay, so an instrument
    # cell only ever says whether the two runs agreed about the instrument: a
    # candidate measured without a filter against a recording made without one
    # matches, and collects the filtered cells as well. The raise is the
    # signal. `run_one.py`'s `main` catches `BaseException`, writes
    # `status: "error"` with the traceback into the scenario's output, keeps
    # the workspace and exits non-zero, which is what an operator sees.
    if (known.get("launcher-available") is not True
            or known.get("filtered/AF_INET") != "EAFNOSUPPORT"
            or known.get("unfiltered/AF_INET") != "ok"):
        raise RuntimeError(
            "no-ipv4 filter is not the known answer: %r" % (known,))

    # -- every program, in the mode that reaches nothing ----------------------
    _tools(ctx, launcher)

    # -- IPv4 on request, unfiltered: this must keep working ------------------
    # Adversarial, and scored at every step this gate runs in. It is what stands
    # between the audit above and a tree that passed it by removing IPv4.
    p0 = ctx.port(0)
    s0 = ctx.server("bind-v4-plain", p0, ctxmod.default_directives(p0, "127.0.0.1"),
                    host="127.0.0.1")
    s0.start()
    ctx.emit("gate/no4/v4-on-request/started", _settle(s0, ["127.0.0.1"], p0),
             adv=True)
    ctx.emit("gate/no4/v4-on-request/answers-v4", _reachable("127.0.0.1", p0),
             adv=True)
    s0.stop()

    # -- the same configuration under the filter ------------------------------
    # An operator who asks for an address the kernel cannot provide is told so.
    # The adversarial half is the second cell: a server that answered on `::1`
    # here would have quietly served an address nobody asked it to.
    # Started rather than run to completion, so that the two adversarial cells
    # below are asked of a process that is still there to answer them. A server
    # run under `start_server_expect_failure` and then probed would be probed
    # after it had been killed, and a tree that listened on `::1` all along
    # would read exactly like one that refused.
    p1 = ctx.port(1)
    s1 = ctx.server("bind-v4-filtered", p1,
                    ctxmod.default_directives(p1, "127.0.0.1"),
                    host="127.0.0.1", launcher=launcher)
    s1.start()
    ctx.emit("gate/no4/v4-filtered/started",
             _settle(s1, ["127.0.0.1", "::1"], p1))
    ctx.emit("gate/no4/v4-filtered/answers-v6", _reachable("::1", p1, 1.0),
             adv=True)
    ctx.emit("gate/no4/v4-filtered/answers-v4",
             _reachable("127.0.0.1", p1, 1.0), adv=True)
    # Not the exit status: a server this scenario had to stop exits non-zero
    # too, so the status says nothing the previous three cells have not. That
    # it was already gone is the fact.
    ctx.emit("gate/no4/v4-filtered/exited-on-its-own",
             s1.proc is not None and s1.proc.poll() is not None)
    ctx.emit("gate/no4/v4-filtered/said-something", bool(s1.log()))
    # A sibling rather than a replacement: `said-something` is satisfied by any
    # output at all, including a banner and nothing else. A server that refused
    # this configuration had the address in hand when it refused, so the
    # address should be somewhere in what it wrote. Adversarial, because a tree
    # that stopped saying which address it could not have is worse to operate
    # than the baseline was. Substring and nothing more: the wording is the
    # project's to choose, and a cell that pinned the sentence would fail a
    # tree that improved it.
    ctx.emit("gate/no4/v4-filtered/log-names-the-address",
             b"127.0.0.1" in s1.log(), adv=True)
    s1.stop()

    # -- an IPv6 literal under the filter -------------------------------------
    p2 = ctx.port(2)
    s2 = ctx.server("bind-v6-filtered", p2, ctxmod.default_directives(p2, "::1"),
                    host="::1", launcher=launcher)
    s2.start()
    ctx.emit("gate/no4/v6-filtered/started", _settle(s2, ["::1"], p2))
    with_reply = None
    try:
        with s2.client() as c:
            c.cmd("SET", "k", "v")
            with_reply = c.cmd("GET", "k")[1]
    except Exception:
        pass
    ctx.emit("gate/no4/v6-filtered/round-trip", with_reply == b"v")
    s2.stop()

    # -- the configuration the project's own suite uses -----------------------
    # `tests/assets/default.conf` is what `tests/support/server.tcl` hands every
    # server the suite starts, so a tree whose own tests cannot run on a host
    # without IPv4 has not finished this step however clean its `src/` is. The
    # file is taken from the tree and only where-to-run is appended.
    # `started` is a pair, and the second half is why. A `default.conf` whose
    # `bind` line has been commented out starts perfectly well under the
    # filter, on the wildcard -- which is a different configuration from the
    # one the project ships, reached by deleting the project's answer rather
    # than by correcting it. The audit above has a rule for exactly this
    # (`sites`), and pairing the cell means the blind spot is not paid twice if
    # that rule is ever wrong. The pair is read off the bytes `_conf_from_tree`
    # already took from the tree: not the tree again, and deliberately not the
    # audit fixture, so this cell still asks its question on a host where the
    # fixture failed to load.
    p3 = ctx.port(3)
    conf, d, asset = _conf_from_tree(
        ctx, os.path.join("tests", "assets", "default.conf"), p3, "suite-asset")
    ctx.emit("gate/no4/suite-asset/present", conf is not None)
    if conf is not None:
        s3 = ctx.server("suite-asset", p3, (), host="::1", launcher=launcher,
                        datadir=d, conf=conf)
        s3.start()
        ctx.emit("gate/no4/suite-asset/started",
                 [_settle(s3, ["::1"], p3), _live_bind(asset)])
        ctx.emit("gate/no4/suite-asset/answers-v6", _reachable("::1", p3))
        ctx.emit("gate/no4/suite-asset/tcl-default-host",
                 _tcl_default_host(ctx, p3, d, launcher))
        s3.stop()
    else:
        ctx.emit("gate/no4/suite-asset/started", [False, False])
        ctx.emit("gate/no4/suite-asset/answers-v6", False)
        ctx.emit("gate/no4/suite-asset/tcl-default-host",
                 _tcl_default_host(ctx, p3, d, launcher))

    # -- no `bind` directive at all -------------------------------------------
    # The configuration an operator gets by writing none: the wildcard, in every
    # family the host has. It is the case with no address in it anywhere, so a
    # tree that passed this step by rewriting the addresses in its own files
    # does not pass here.
    #
    # What the recordings say about it is worth reading in full, because it is
    # not the shape the rest of this scenario has. The 2016 baseline PASSES
    # this case -- `started`, `answers-v6` and `round-trip` are all true
    # against the pin -- and steps 1, 2 and 3 of this migration all record
    # false. The wildcard listener was already family-agnostic; it was the
    # migration's own earlier work that broke it, and step 4 is where it comes
    # back. They move at this step and still match the pin, so the role rule
    # calls them regression rather than progress, which is the right answer for
    # the reason it is the right answer everywhere: a tree that did nothing at
    # all would match them too.
    #
    # Only the server runs under the filter (`launcher=launcher` below); the
    # probes do not. So `answers-v4` really can open an IPv4 socket if the
    # server left one listening, and a tree that reached a wildcard v6 socket
    # by dropping IPV6_V6ONLY answers there and is caught. Filtering the probe
    # too would have made that cell pass for the reason that nothing could have
    # connected either way.
    p4 = ctx.port(4)
    d4 = ["port %d" % p4, "dir .", "logfile server.log", "daemonize no",
          'save ""', "appendonly no"]
    s4 = ctx.server("no-bind-filtered", p4, d4, host="::1", launcher=launcher)
    s4.start()
    ctx.emit("gate/no4/no-bind/started", _settle(s4, ["::1"], p4))
    ctx.emit("gate/no4/no-bind/answers-v6", _reachable("::1", p4))
    ctx.emit("gate/no4/no-bind/answers-v4", _reachable("127.0.0.1", p4, 1.0),
             adv=True)
    round_trip = None
    try:
        with s4.client() as c:
            c.cmd("SET", "wildcard", "yes")
            round_trip = c.cmd("GET", "wildcard")[1]
    except Exception:
        pass
    ctx.emit("gate/no4/no-bind/round-trip", round_trip == b"yes")
    s4.stop()

    ctx.note("launcher: %s" % (no4.ensure(work)[1],))
