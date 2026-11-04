"""Two properties of a tree that has stopped offering IPv4 as what it does.

Step 6 asks two things that look like one, the way step 4 did. The first is
about the files: no file this repository ships still names an IPv4 address as
an example of what this tree does. The second is about the running server: one
that was not told what to bind creates no IPv4 socket and sends and receives no
IPv4 packet. A tree can pass either alone -- one that edited every comment
still hands an unconfigured operator a dual-stack listener, and one that
changed the default still ships a README telling a reader to connect to
127.0.0.1 -- so both are measured here and each is worth its own cells.

### The audit, and the two exceptions in it

The audit is a text scan, and unlike step 4's it does **not** strip comments
from C sources. That is the whole point: at step 4 the addresses that mattered
were the ones something acted on, and a comment is not acted on; here the
question is what the repository says about itself, and a comment is exactly
where it says it. `textscan.ipv4_literals` scrubs comments, so this module
scans for itself.

Two classes of text look like an address and are not a finding:

  * **a prefix with a length.** `127.0.0.0/8` is not an example of somewhere to
    go, it is the definition of an address class the tree has to recognise --
    `anetAddrClass` cannot classify a loopback address without writing down
    what a loopback address is. Recognised by the `/` and a length of 0..32
    immediately after the quad, which is what a prefix is and what an example
    never has -- and which an example can be given: `10.0.0.1/8` is somewhere
    to go with a prefix length stuck on the end, and this rule excuses it.
    That is the limit of the rule and not a bug in it.
  * **an RFC section number.** `RFC 4291 section 2.5.5.2` is four numbers
    separated by dots and every octet is under 256, so it parses as an address
    and is not one. Recognised by the words in front of it -- `rfc`,
    `section`, `§` within the twelve characters before the match -- of the
    text, not of the line, so that a citation split across two lines of a C
    string literal is still a citation. A heuristic, and it is written down as
    one: it would miss a section number introduced some other way, and it
    would excuse a real address written after the word "section". Both halves
    are rehearsed against a known answer below, because
    a scanner that excused everything would report the same clean zero as a
    clean tree.

Neither can be tightened into something that is not a heuristic, so the size of
each one is bounded instead. `excused-prefix` and `excused-section` are whether
each absorbed no more than a ceiling, over the same surface and in the same
pass as `standing`, with the raw counts left in the transcript as notes. A tree
that passed the audit by appending `/8` to every address it kept, or by writing
the word "section" in front of one, reads as a clean tree on `standing` and
goes over the ceiling on one of these two. On the golden tree all nine
are in `src/anet.c` and `src/anet.h`: the IPv4 halves of the address-class
table and the one class check written inline, and the RFC 4291 citations that
explain them. Both shapes of abuse are rehearsed below beside the exceptions
themselves, written down as the limit of the rule rather than as a rule that
works.

And one that is *not* an exception, and needs none: `::ffff:127.0.0.1` is an
IPv6 address, and the scan does not match it, because the character before the
quad is a colon. The tree under test carries that form in `src/anet.c`, so this
is not a hypothetical -- a scan whose lookbehind only excluded word characters
would report two findings on the golden tree and call them IPv4.

What the audit cannot see is an address *deleted* rather than corrected, and
step 4 measured what that is worth. Four cells answer it here, each aimed at
one place where deletion would otherwise be free:

  * the class definitions above (`class-prefix-definitions`): a tree that
    passed the audit by deleting `anet.h`'s address-class table loses them;
  * the mapped-form explanation (`mapped-examples`): the same, for the comments
    that say what `::ffff:127.0.0.1` is;
  * the vendored client (`client-example-strings`): `deps/hiredis`'s README,
    test and five examples exist to show a reader how to connect, and each one
    is a single quoted address. Deleting it and correcting it both take the
    quad away; only one of them leaves an address behind.
  * the comments that state the *shape of a format* (`format-statements`): the
    `CLIENT LIST` peer id, the `ip:port` pair a replication log line quotes,
    Sentinel's `slave0:ip=` INFO field and the cluster-nodes hash table each
    explained itself with a dotted quad at the pin and explains itself with an
    IPv6 address now, and `by-file` reads the same clean zero whether the quad
    was corrected or the whole sentence went. Each is found by something other
    than the address in it -- the address is the thing under test and cannot
    also be the way the sentence is found -- and what is read beside the marker
    is whether the sentence still offers a reader an address at all, not how
    many it offers: that number is the author's choice.

The rest of the step's IPv4 examples live in text that is *already* scored
verbatim somewhere else and moves at this step: `redis-server --help` in
`c08_listeners`' `argv/help-out`, `redis-benchmark`'s usage in `c13_benchmark`,
and the shipped `redis.conf` in `d06_v6_default`'s `shipped-conf` cells and
`g06`'s conf cells. Deleting an example there changes a recorded text.

### The socket, and the packets

"Creates no IPv4 socket" is read from the kernel's own TCP tables: the
server's port, looked for in `/proc/net/tcp` and in `/proc/net/tcp6`.

Reading the server's `/proc/<pid>/fd` instead would be the more direct
question -- every socket the process holds, listening or connected -- and it
was written that way first. It does not work here, and the way it fails is the
dangerous way. The verifier demotes the server to uid 65534, and reading
another user's descriptor table needs CAP_SYS_PTRACE, which this container does
not hold (CapEff `00000000a80425fb`). Every `readlink` returns EACCES, the
inode set comes out empty, and the reader answers `[]` -- for the
IPv4-configured control exactly as for the IPv6-only default. An instrument
that answers "no IPv4 socket" for a server that has one is the shape of a
false pass, and it is invisible unless a control is scored beside the finding.
The tables are world-readable and carry the listening socket whatever user
opened it, so that is what is read. The port is the join key and the runner
gives every scenario its own ports.

The instrument's known answer is a v4-only and a v6-only listening socket
opened by this process, which must read `["v4", "v6"]` in that order; the
finding's control is a server the operator *did* configure for IPv4, which must
read `["v4"]`.

The capture is the same claim from the other side, and it is a weaker
instrument on purpose: a packet trace can only say that nothing went over IPv4
*during the window*, where the kernel's table says no such socket exists. It is
here because "no IPv4 traffic" is what an operator would check, and it is
paired with two things that make a dead capture visible -- a synthetic failure
fixture (the same capture around an IPv4-configured server, which must see
IPv4) and, inside each capture, a probe over IPv4 to a port this scenario owns
and never binds, which must appear in the finished file.

The cell that carries the claim is `capture/<case>/v4-session`, and not `seen`.
Both captures open a session over IPv4 to the server's own port, so
`seen[0]` is true in both of them -- a refused connection is still a SYN and a
RST on that port -- and `seen` is a statement about the instrument
rather than about the tree: the capture could see IPv4 on the port it was
watching. `v4-session` is whether that session was *served*, which is what "no
IPv4 was served" means and what the default case is being asked. It reads false
on this step's default and true on any tree whose unconfigured server still
answers IPv4, which is what the previous step's tree is.
"""

import ipaddress
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import resp  # noqa: E402
import srv  # noqa: E402
import textscan  # noqa: E402

PORTS = 5
FAMILY = "gate"

# The pin's answer to the audit below, in `tests/fixtures` beside
# `pin-script-surface.json`.
PIN_DOC_SURFACE = os.path.join(HERE, os.pardir, os.pardir, "fixtures",
                               "pin-doc-surface.json")

SETTLE = 60.0

# A dotted quad with nothing alphanumeric, dotted or *coloned* on either side of
# it. The colon in the lookbehind is what keeps `::ffff:127.0.0.1` out: it is an
# IPv6 address, written the way RFC 5952 section 5 says to write one.
QUAD = re.compile(r"(?<![0-9A-Za-z.:])((?:[0-9]{1,3}\.){3}[0-9]{1,3})(?![0-9A-Za-z.])")
# A prefix length immediately after the quad: `/0` to `/32`.
PREFIX = re.compile(r"/([0-9]{1,2})(?![0-9])")
# What a section number is introduced by, within reach of the number itself.
SECTION_HINT = re.compile(r"(section|rfc|§)", re.I)
SECTION_REACH = 12
# What a wrap puts between a citation and the number it introduces: the
# closing quote of one string literal, the indentation of the next line and
# its opening quote. Collapsed to one space before the window above is cut,
# so that wrapping a sentence does not push its citation out of reach.
WRAP_FILL = re.compile(r'["\s]+')
# `::ffff:a.b.c.d`, the form the tree has to keep explaining.
MAPPED = re.compile(r"(?<![0-9A-Za-z.:])::ffff:(?:[0-9]{1,3}\.){3}[0-9]{1,3}",
                    re.I)
# A quoted string that is an address or the name of this machine, which is what
# a client example has in it. Nothing else in these files is quoted like this.
QUOTED = re.compile(r'"([^"\s]{1,60})"')
# A run of the characters an address literal is made of, optionally bracketed
# and optionally carrying a port. Not a validator -- `_is_address` is, and this
# only has to find the tokens to hand it. The bracketed form is first so that
# `[::1]:6379` is one candidate and not two.
ADDR_CANDIDATE = re.compile(r"\[[0-9A-Fa-f:.%]{2,45}\](?::[0-9]{1,5})?"
                            r"|[0-9A-Fa-f:.%]{2,45}")
# A `:port` on the end of one, which `inet_pton` will not take.
PORT_SUFFIX = re.compile(r":[0-9]{1,5}$")

# This project's own material. Everything the bundled third-party runtimes
# carry is somebody else's description of themselves; step 1's requirement that
# no IPv4-only type or function survive anywhere already covers what matters
# about them.
SKIP_PREFIXES = ("deps/lua/", "deps/jemalloc/", "deps/linenoise/", "src/lua/")
KEEP_PREFIXES = ("src/", "tests/", "utils/", "deps/hiredis/")
# Written by `utils/mkreleasehdr.sh` during `make`, so it is in a built tree and
# not in a clean one, and this scenario is recorded from built trees. Naming it
# in `textscan.GENERATED` instead would move g06's counts at every step it is
# scored in, for a file that says nothing about addresses either way.
GENERATED_AT_BUILD = ("src/release.h",)
# Supplied by the platform, not shipped by this repository: the step image
# copies `timer.sh` into the workspace, and `WORKSPACE_HANDOFF.md` is the note
# the workspace asks whoever works here to keep accurate. Both sit at the root
# of a run tree and in neither a clean checkout nor the trees this scenario is
# recorded from, so counting them would make `surface-files` a fact about the
# harness rather than about the tree. Auditing them would be wrong for a second
# reason: a handoff note that says `bind 127.0.0.1` still means IPv4 is
# accurate about this tree, not an example of what the tree does. This
# repository's own root documents -- `README.md`, `redis.conf`,
# `sentinel.conf`, `hedis.yml` and the rest -- are audited.
PLATFORM_FILES = ("timer.sh", "WORKSPACE_HANDOFF.md")

# The vendored client's own how-to-connect files: each exists to show a reader
# an address, so each has to still have one.
CLIENT_FILES = (
    "deps/hiredis/README.md",
    "deps/hiredis/examples/example-ae.c",
    "deps/hiredis/examples/example-libev.c",
    "deps/hiredis/examples/example-libevent.c",
    "deps/hiredis/examples/example-libuv.c",
    "deps/hiredis/examples/example.c",
    "deps/hiredis/test.c",
)

# The comments that state the shape of a format this tree prints, each named by
# something that is not the address in it. Three are named by the words the
# sentence is about; `src/redis.c` is named by the identifier on the line under
# it instead, because its own opening words are descriptive prose a tree is
# free to reword, while `clusterNodesDictType` is the name of the thing the
# comment explains and cannot be reworded without breaking the build. Every one
# of them explained itself with a dotted quad at the pin and explains itself
# with an IPv6 address now, and every one of them is somewhere a tree could
# pass the audit by deleting the sentence instead of correcting it.
FORMAT_STATEMENTS = (
    ("src/networking.c", 'A Redis "Peer ID"'),
    ("src/replication.c", "Closing connection with slave"),
    ("src/sentinel.c", "slave0:ip="),
    ("src/redis.c", "clusterNodesDictType"),
)
# How far either side of the marker line the explanation is taken to run. The
# four above put the example on the marker line, just under it, or -- for the
# one marker that is a line of code rather than a line of the comment -- just
# above it; three lines is room for a tree that rewrapped the comment, and
# short enough that the next unrelated sentence is not read as part of this
# one.
STATEMENT_REACH = 3

# The scanner's known answer, rehearsed before any finding is believed. Eleven
# texts and eleven counts: two of them are the exceptions this module claims to
# make, and the last three are those same exceptions swallowing an address that
# really is somewhere to go, which is the limit of the rule and not a bug in
# it. The last two are written the way this repository writes a long sentence
# in C -- as a string literal split across two lines -- because a citation that
# wrapped is still a citation and an address after one is still excused.
# `4.9.2.1` is in the list for the same reason: a four-part version number is a
# valid dotted quad and there is nothing in the text to say it is not an
# address, so it counts. `excused-prefix` and `excused-section` are what make
# the last two visible on a real tree rather than only in a fixture.
INSTRUMENT_FIXTURE = (
    ("a destination", 'redisConnect("203.0.113.9", 6379);'),
    ("a prefix with a length", "#define LOOPBACK 2 /* ::1/128, 127.0.0.0/8 */"),
    ("an RFC section number", " * deprecated by RFC 4291 section 2.5.5.1"),
    ("a mapped address", ' * "::ffff:127.0.0.1" and "::ffff:7f00:1" are one'),
    ("an octet over 255", "# see the 1.2.3.999 release notes"),
    ("a four-part version number", "built with gcc 4.9.2.1 on this host"),
    ("two on one line", 'bind 10.0.0.1 10.0.0.2'),
    ("a destination wearing a prefix length",
     'redisConnect("10.0.0.1/8", 6379);'),
    ("a destination behind a citation",
     " * the RFC 1918 10.0.0.1 this box uses"),
    ("a section number wrapped mid-literal",
     ' * an IPv4-compatible address is deprecated by RFC 4291 section\n'
     '   "2.5.5.1 and names no host";'),
    ("a destination behind a wrapped citation",
     ' * this box is on the network RFC 1918 section\n'
     '   "10.0.0.1 names, and nothing else";'),
)

# The known answer for the format-statement reader, in the three states that
# matter: the sentence with its example, the sentence with the example taken
# out of it, and no sentence at all. The middle one is the state `by-file`
# cannot tell from a correction, which is the whole reason that cell exists.
STATEMENT_FIXTURE = (
    ("the sentence, with its example", 'A Redis "Peer ID"',
     '/* A Redis "Peer ID" is an address and a port joined by a colon.\n'
     ' * The address is bracketed, like in "[::1]:1234". */'),
    ("the sentence, example deleted", 'A Redis "Peer ID"',
     '/* A Redis "Peer ID" is an address and a port joined by a colon. */'),
    ("the sentence deleted", 'A Redis "Peer ID"',
     "/* Generate the peer id of a client. */"),
)


def _doc_surface(root):
    """The files this project uses to describe itself, selected by name.

    By path, not by content, so it says the same thing about a tree that has
    changed every one of them -- and so that a file a candidate *adds* under
    `src/` is audited too.
    """
    out = []
    for rel in textscan.walk(root):
        if rel.startswith(SKIP_PREFIXES) or rel in GENERATED_AT_BUILD:
            continue
        if rel in PLATFORM_FILES:
            continue
        if "/" in rel and not rel.startswith(KEEP_PREFIXES):
            continue
        if textscan.is_text(os.path.join(root, rel)):
            out.append(rel)
    return out


def _hint_window(before, line, start):
    """The text a citation could have been written in, just before `start`.

    `SECTION_REACH` characters of the text that precedes the match -- *text*,
    not *line*. A C string literal is joined across source lines and this
    repository wraps them mid-sentence, so

        "an IPv4-compatible address is deprecated by RFC 4291 section "
        "2.5.5.1 and names no host"

    is one sentence written as two lines, and reading the second one alone
    turns a section number into an address. The tail of what came before is
    carried in, and the quotes and indentation the wrap added are collapsed to
    single spaces so they do not spend the window.
    """
    return WRAP_FILL.sub(" ", before + " " + line[:start])[-SECTION_REACH:]


def _carry(before, line):
    """What the next line sees of this one: the same window, same collapse."""
    return WRAP_FILL.sub(" ", before + " " + line)[-SECTION_REACH:]


def _classify(line, before=""):
    """The examples one line leaves standing, and what each exception took.

    One pass and three answers. The first is what `_standing` has always
    returned; the other two are how many matches the prefix rule and the
    section rule each threw away, which are emitted beside the finding because
    an exception that has quietly grown to cover the tree reports the same
    clean zero as a tree with nothing left to find.

    `before` is the tail of the line above, and is what makes a citation that
    wrapped still a citation.
    """
    standing, prefix, section = [], 0, 0
    for m in QUAD.finditer(line):
        try:
            ipaddress.IPv4Address(m.group(1))
        except ValueError:
            continue                      # an octet over 255: not an address
        pm = PREFIX.match(line[m.end():])
        if pm and int(pm.group(1)) <= 32:
            prefix += 1                   # a class definition, not an example
            continue
        if SECTION_HINT.search(_hint_window(before, line, m.start())):
            section += 1                  # a section number, not an address
            continue
        standing.append(m.group(1))
    return standing, prefix, section


def _standing(line):
    """The IPv4 addresses left standing as examples on one line."""
    return _classify(line)[0]


def _standing_text(text):
    """The same, over a block of text, line by line and carrying the tail."""
    out, before = [], ""
    for line in text.splitlines():
        out.extend(_classify(line, before)[0])
        before = _carry(before, line)
    return out


def _read(root, rel):
    try:
        with open(os.path.join(root, rel), "rb") as f:
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""


def _standing_per_file(root, rels):
    per = {}
    for rel in rels:
        n = len(_standing_text(_read(root, rel)))
        if n:
            per[rel] = n
    return per


def _excused(root, rels):
    """(prefix, section) matches the two exceptions absorbed, tree-wide.

    The same scan over the same files as `standing`, counting what it threw
    away rather than what it kept.
    """
    prefix = section = 0
    for rel in rels:
        before = ""
        for line in _read(root, rel).splitlines():
            _, p, s = _classify(line, before)
            prefix += p
            section += s
            before = _carry(before, line)
    return prefix, section


def _class_prefixes(root, rels):
    """Distinct IPv4 class definitions -- quad plus length -- in the tree."""
    found = set()
    for rel in rels:
        for line in _read(root, rel).splitlines():
            for m in QUAD.finditer(line):
                pm = PREFIX.match(line[m.end():])
                if not pm or int(pm.group(1)) > 32:
                    continue
                try:
                    ipaddress.IPv4Network("%s/%s" % (m.group(1), pm.group(1)),
                                          strict=False)
                except ValueError:
                    continue
                found.add("%s/%s" % (m.group(1), pm.group(1)))
    return found


def _mapped_forms(root, rels):
    return sum(len(MAPPED.findall(_read(root, rel))) for rel in rels)


def _is_address(token):
    """Whether this token is an address literal of either family.

    Lifted out of `_address_strings` unchanged, so that the two cells asking
    "is there still an address here" decide it the same way rather than each
    growing its own idea of what an address is. Brackets and a zone id come off
    first: both are how an IPv6 address is written down, not part of it.
    """
    if token.startswith("[") and "]" in token:
        token = token[1:].split("]", 1)[0]
    token = token.split("%", 1)[0]
    for fam in (socket.AF_INET6, socket.AF_INET):
        try:
            socket.inet_pton(fam, token)
        except (OSError, ValueError):
            continue
        return True
    return False


def _address_strings(text):
    """Quoted strings in this text that are an address or `localhost`.

    What a client example has where the address goes. `localhost` counts
    because a name is a perfectly good thing to hand a reader; a quoted
    hostname of any other kind does not, because then every quoted word would.
    """
    n = 0
    for m in QUOTED.finditer(text):
        t = m.group(1)
        if t == "localhost" or _is_address(t):
            n += 1
    return n


def _addr_literals(line):
    """How many address literals one line offers, port or no port.

    Unquoted, because a comment explaining a format writes the example into the
    prose -- `"[::1]:6379"` is quoted and `slave0:ip=::1,port=9999` is not, and
    both are the sentence still showing a reader an address.

    `ADDR_CANDIDATE` is a finder and not a validator: it hands over every run
    of the characters an address is made of, and `_is_address` is what decides,
    which means a four-part version number like `4.9.2.1` is accepted here for
    the same reason `INSTRUMENT_FIXTURE` says the quad scanner accepts it --
    it is a valid dotted quad and nothing in the prose says otherwise. That is
    why `_format_statement` reads this count only as "at least one": a second
    literal drifting into a window then costs nothing, and the finder does not
    have to be tightened into something that is not a heuristic.
    """
    n = 0
    for m in ADDR_CANDIDATE.finditer(line):
        t = m.group(0)
        if _is_address(t) or _is_address(PORT_SUFFIX.sub("", t)):
            n += 1
    return n


def _format_statement(text, marker, reach=STATEMENT_REACH):
    """(the sentence is still here, it still offers an address).

    Two facts rather than one, because they fail differently: a tree that
    deleted the comment reads `[false, false]`, a tree that kept the sentence
    and took its example away reads `[true, false]`, and `by-file` reads a
    clean zero for both of them and for a correction.

    The second is a boolean and not the count behind it. How many examples a
    corrected sentence carries is the author's choice -- one bracketed pair,
    or one per family -- and what the instruction asks is only that the
    sentence is still here and still shows a reader an address. Reading the
    count would fail a tree that corrected both of the pin's examples rather
    than merging them into one, which is the thing "deleting the sentence is
    not correcting it" asks for.
    """
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if marker in line:
            window = lines[max(0, i - reach):i + reach + 1]
            return True, sum(_addr_literals(w) for w in window) >= 1
    return False, False


def _format_statements(root):
    out = []
    for rel, marker in FORMAT_STATEMENTS:
        present, offers = _format_statement(_read(root, rel), marker)
        out.append([rel, present, offers])
    return out


def _pin_doc_surface(path=PIN_DOC_SURFACE):
    """(files, {file: standing}) the pin shipped, or two empty containers.

    Two empty ones if the fixture is unreadable, which `pin-files` and
    `pin-quads` are emitted to make visible: an audit that lost its fixture
    reports the same clean zero as a clean tree.
    """
    try:
        with open(path) as f:
            doc = json.load(f)
        return list(doc["files"]), {k: int(v) for k, v in doc["quads"].items()}
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return [], {}


def _audit(ctx):
    root = ctx.tree.root
    rels = _doc_surface(root)
    per = _standing_per_file(root, rels)

    # The instrument before the finding.
    ctx.emit("gate/v6free/audit/instrument",
             [[label, len(_standing_text(text))]
              for label, text in INSTRUMENT_FIXTURE])
    ctx.emit("gate/v6free/audit/surface-files", len(rels))

    pin_files, pin_quads = _pin_doc_surface()
    ctx.emit("gate/v6free/audit/pin-files", len(pin_files))
    ctx.emit("gate/v6free/audit/pin-quads", sum(pin_quads.values()))
    present = set(rels)
    ctx.emit("gate/v6free/audit/pin-files-present",
             sum(1 for rel in pin_files if rel in present))

    # The finding: over the whole surface of the tree under test, so that an
    # address written into a file the candidate added is seen too.
    ctx.emit("gate/v6free/audit/standing", sum(per.values()))
    # And per file, over the files the pin carried one in, so that a tree that
    # corrected `src/` and left `deps/hiredis/` alone is paid for the half it
    # did. The files step 4 already cleared are here as well; they read zero at
    # step 5 and have to keep reading zero.
    for rel in sorted(pin_quads):
        ctx.emit("gate/v6free/audit/by-file/%s" % rel, per.get(rel, 0))

    # And how much the two exceptions above had to absorb to get that number.
    # An exception that has grown to cover the tree reads as a clean tree on
    # `standing`; this is where it has to say so.
    #
    # A ceiling, and not the number itself. What each cell is for is the one
    # failure `standing` cannot see, and that failure is an exception grown
    # large enough to swallow the tree's real addresses; how many class
    # definitions or RFC citations a correct tree writes is the author's
    # choice, and paying for a particular count would fail a tree that wrote
    # one more of either. The lower bound is elsewhere and is already a bound:
    # `class-prefix-definitions` asks for at least four distinct classes.
    #
    # Twelve because the largest count either exception reaches on any tree in
    # this corpus is five -- the four class comments in `src/anet.h` and the
    # one class check written inline in `src/anet.c` -- and because a tree that
    # wrote down the IPv4 half of every class `DOCS/05-address-classes.md`
    # names would reach seven, or ten with RFC 1918's three, and still be
    # under it, while an exception that had grown to cover the tree's own
    # addresses would be far above it. The raw counts stay in the transcript as
    # notes, where they can be read without being scored.
    prefix_excused, section_excused = _excused(root, rels)
    ctx.note("prefix exceptions absorbed: %d" % prefix_excused)
    ctx.note("section exceptions absorbed: %d" % section_excused)
    ctx.emit("gate/v6free/audit/excused-prefix", prefix_excused <= 12)
    ctx.emit("gate/v6free/audit/excused-section", section_excused <= 12)

    # Deleting is not correcting: the four places where it would otherwise be.
    ctx.emit("gate/v6free/audit/class-prefix-definitions",
             len(_class_prefixes(root, rels)) >= 4)
    ctx.emit("gate/v6free/audit/mapped-examples", _mapped_forms(root, rels) >= 1)
    ctx.emit("gate/v6free/audit/client-example-strings",
             [[rel, _address_strings(_read(root, rel))] for rel in CLIENT_FILES])
    # The instrument before this finding too, for the same reason: a reader
    # that had stopped finding the sentence would report a deleted sentence.
    fixture = []
    for label, marker, text in STATEMENT_FIXTURE:
        present, offers = _format_statement(text, marker)
        fixture.append([label, present, offers])
    ctx.emit("gate/v6free/audit/format-statements/instrument", fixture)
    ctx.emit("gate/v6free/audit/format-statements", _format_statements(root))


def _listening(path):
    """The local ports with a listening row in one of the kernel's TCP tables."""
    out = set()
    try:
        with open(path) as f:
            next(f, None)
            for line in f:
                cols = line.split()
                if len(cols) < 4 or cols[3] != "0A":     # 0A == TCP_LISTEN
                    continue
                local = cols[1]
                if ":" not in local:
                    continue
                try:
                    out.add(int(local.rsplit(":", 1)[1], 16))
                except ValueError:
                    continue
    except OSError:
        pass
    return out


def _port_families(port):
    """Which IP families hold a listening socket on `port`, as ['v4', 'v6'].

    Read by port from the kernel's tables rather than from the server's
    descriptor table; the module docstring says why the descriptor table is not
    usable here and what it answers when it fails.

    One thing this reading cannot see, and it is worth writing down: an
    AF_INET6 socket bound to `::` with `IPV6_V6ONLY` off has a row in `tcp6`
    only, and accepts IPv4 anyway. No socket-level reading distinguishes it
    from an IPv6-only listener -- the distinction is behavioural, and the
    behaviour is `gate/no4/no-bind/answers-v4` in `g06_no_ipv4_kernel`, which
    this step carries forward unchanged and which such a server would flip.
    """
    fams = []
    if port in _listening("/proc/net/tcp"):
        fams.append("v4")
    if port in _listening("/proc/net/tcp6"):
        fams.append("v6")
    return fams


def _self_families():
    """The reader's known answer: a v4-only port, then a v6-only port.

    Anything other than `['v4', 'v6']` means the reader has stopped telling the
    two tables apart, or has stopped reading one of them, and the four cells
    below it are not evidence of anything.
    """
    s4 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s6 = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    try:
        # Listening, because a socket that is not is in neither table.
        s4.bind(("127.0.0.1", 0))
        s6.bind(("::1", 0))
        s4.listen(1)
        s6.listen(1)
        return (_port_families(s4.getsockname()[1])
                + _port_families(s6.getsockname()[1]))
    finally:
        s4.close()
        s6.close()


def _reachable(host, port, timeout=5.0):
    try:
        with resp.Client(host, port, timeout=timeout) as c:
            return c.cmd("PING")[1] == b"PONG"
    except Exception:
        return False


def _settle(server, hosts, port, deadline=SETTLE):
    end = time.time() + deadline
    while time.time() < end:
        for h in hosts:
            if _reachable(h, port, timeout=1.0):
                return True
        if server.proc is not None and server.proc.poll() is not None:
            return False
        time.sleep(0.1)
    return False


class Capture:
    """A packet trace taken while the block it wraps runs.

    `-p` so the interface is not put in promiscuous mode, `-U` so packets reach
    the file as they arrive rather than at exit, and `-s 128` so a trace that
    happens to catch another scenario's bulk traffic stays small: an IP header
    and a TCP header are all that is read back out of it.

    There is deliberately no port filter on the *live* capture, and a port
    filter on the *read* instead. A live filter narrow enough to be this
    scenario's own traffic cannot be proved live -- the only packets that would
    prove it are the packets being measured, so a capture that recorded nothing
    and a server that sent nothing produce the same file. Capturing all TCP and
    narrowing afterwards leaves room for a probe that is not the measurement:
    `probe_port` is a port this scenario owns and never binds, a connection to
    it over IPv4 is refused after a SYN and a RST, and those two packets are
    what `start()` waits to see arrive in the file. They are counted again out
    of the finished file as `instrument`, so the file itself carries the proof
    that it was able to record IPv4.

    That wait is not decoration. libpcap here uses TPACKET_V3, whose ring hands
    a block to userspace when the block fills or when its retire timer expires,
    and the timer is about a second. Measured in this container: a capture torn
    down 0.8s after tcpdump printed `listening on` left a 24-byte file -- a pcap
    header and nothing after it -- while the kernel had already counted 80
    packets through the socket. The first version of this class waited for that
    line and 0.3s, and every `seen` cell it recorded was `[false, false]`,
    including the IPv4 control's. `available` is therefore not "tcpdump
    started" but "tcpdump was seen to write a packet".

    All of it rests on a packet socket, which needs CAP_NET_RAW. This container
    holds it -- `CapEff` measured here is `00000000a80425fb`, whose bit 13 is
    CAP_NET_RAW -- and holds it because it is in Docker's default set, not
    because the verifier image asks for it. Nothing here would announce its
    loss: `Popen` still succeeds, tcpdump writes its refusal to stderr, and the
    file stays empty. That is what `available` and `instrument` are scored for.
    Without them a capability that went missing on some future host would read
    as a tree that put no IPv4 on the wire, and every capture cell would go
    green for the wrong reason.
    """

    def __init__(self, workdir, name, probe_port):
        self.probe_port = probe_port
        # tcpdump opens its `-w` file as root (and then hands it to its own
        # user), so the file is not put in `workdir`: that is the score uid's,
        # and a link standing at the path would be followed. A root-only
        # directory of its own instead, for the trace and tcpdump's stderr;
        # nothing scored reads either path. `workdir` is kept in the signature
        # for the callers.
        private = tempfile.mkdtemp(prefix="hedis-cap-")
        self.path = os.path.join(private, "%s.pcap" % name)
        self.log = os.path.join(private, "%s.tcpdump" % name)
        self.proc = None
        self._fh = None

    def _probe(self):
        """One refused IPv4 connection: a SYN out and a RST back."""
        try:
            c = socket.create_connection(("127.0.0.1", self.probe_port), 0.2)
            c.close()
        except OSError:
            pass

    def start(self, deadline=30.0):
        try:
            self._fh = open(self.log, "wb")
            self.proc = subprocess.Popen(
                ["tcpdump", "-i", "any", "-n", "-p", "-U", "-s", "128",
                 "-w", self.path, "tcp"],
                stdout=subprocess.DEVNULL, stderr=self._fh)
        except OSError:
            self.proc = None
            return False
        end = time.time() + deadline
        seen_line = False
        while time.time() < end:
            if self.proc.poll() is not None:
                return False
            if not seen_line:
                try:
                    with open(self.log, "rb") as f:
                        seen_line = b"listening on" in f.read()
                except OSError:
                    pass
                if not seen_line:
                    time.sleep(0.1)
                    continue
            self._probe()
            try:
                # 24 bytes is a pcap file header with no packet behind it.
                if os.path.getsize(self.path) > 24:
                    return True
            except OSError:
                pass
            time.sleep(0.1)
        return False

    def _read(self, expr):
        try:
            p = subprocess.run(["tcpdump", "-r", self.path, "-n", expr],
                               stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, timeout=60)
        except (OSError, subprocess.SubprocessError):
            return None
        return len(p.stdout.decode("utf-8", "replace").splitlines())

    def counts(self, port):
        """(ipv4, ipv6, probe) packet counts, read back out of the file.

        `probe` is the IPv4 warm-up traffic. A file with none of it in it was
        not recording, whatever the other two numbers say.
        """
        if self.proc is None:
            return None, None, None
        time.sleep(1.5)                   # let the last block reach the file
        self.proc.terminate()
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)
        self._fh.close()
        return (self._read("ip and tcp port %d" % port),
                self._read("ip6 and tcp port %d" % port),
                self._read("ip and tcp port %d" % self.probe_port))


def _talk(host, port, family, rounds=8):
    """A short session with the server, so the trace has something in it."""
    try:
        with resp.Client(host, port, timeout=10.0) as c:
            for i in range(rounds):
                c.cmd("SET", "cap%d" % i, "v")
                c.cmd("GET", "cap%d" % i)
        return True
    except Exception:
        return False


def _capture_case(ctx, name, port, probe_port, directives, host, ready_hosts,
                  workdir):
    s = ctx.server("cap-%s" % name, port, directives, host=ready_hosts[0])
    s.start()
    started = _settle(s, ready_hosts, port)
    cap = Capture(workdir, name, probe_port)
    available = cap.start() if started else False
    spoke = _talk(host, port, None) if available else False
    # The same question over IPv4, to this server's own port, in both cases:
    # this is the cell that carries "no IPv4 was served". Gated on `started`
    # rather than on `available`, because whether the server answered IPv4 is a
    # fact about the server and not about tcpdump, and a missing tcpdump must
    # not make a dual-stack tree look like a clean one. When the capture is up
    # the attempt also lands in the trace, which is why `seen[0]` is true
    # in both cases and why `seen` is an instrument reading rather than the
    # finding.
    #
    # `None` and not `False` when the server never came up. `False` is the
    # clean tree's answer and `True` is the dual-stack one, so a server that is
    # not there must be neither: gating to `False` would have handed the cell
    # to a tree whose server does not start, which is the one reading that is
    # not evidence about IPv4 at all. `None` loses the cell instead.
    v4_session = _talk("127.0.0.1", port, None) if started else None
    v4, v6, probe = cap.counts(port) if available else (None, None, None)
    s.stop()
    ctx.emit("gate/v6free/capture/%s/available" % name, available)
    ctx.emit("gate/v6free/capture/%s/spoke" % name, spoke)
    ctx.emit("gate/v6free/capture/%s/v4-session" % name, v4_session)
    # The file's own proof that it was recording IPv4 while the window was open.
    ctx.emit("gate/v6free/capture/%s/instrument" % name,
             bool(probe) if probe is not None else None)
    ctx.emit("gate/v6free/capture/%s/seen" % name,
             [bool(v4) if v4 is not None else None,
              bool(v6) if v6 is not None else None])


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]
    work = ctx.workspace.sub("g08")

    _audit(ctx)

    # -- which families the server's own sockets are in --------------------
    ctx.emit("gate/v6free/sockets/instrument/self", _self_families())

    nobind = [d for d in ctxmod.default_directives(p[0])
              if not d.startswith("bind ")]
    s0 = ctx.server("sock-default", p[0], nobind)
    s0.start()
    started0 = _settle(s0, ["::1", "127.0.0.1"], p[0])
    ctx.emit("gate/v6free/sockets/default/started", started0)
    ctx.emit("gate/v6free/sockets/default/families",
             _port_families(p[0]) if s0.proc else ["<not running>"])
    s0.stop()

    # The control: a server the operator did configure for IPv4. A reader that
    # always answered `['v6']`, or always answered nothing, says so here.
    s1 = ctx.server("sock-explicit-v4", p[1],
                    ctxmod.default_directives(p[1], "127.0.0.1"),
                    host="127.0.0.1")
    s1.start()
    ctx.emit("gate/v6free/sockets/explicit-v4/started",
             _settle(s1, ["127.0.0.1"], p[1]))
    ctx.emit("gate/v6free/sockets/explicit-v4/families",
             _port_families(p[1]) if s1.proc else ["<not running>"])
    s1.stop()

    s2 = ctx.server("sock-mixed", p[2],
                    ctxmod.default_directives(p[2], "::1 127.0.0.1"))
    s2.start()
    ctx.emit("gate/v6free/sockets/mixed/started",
             _settle(s2, ["::1", "127.0.0.1"], p[2]))
    ctx.emit("gate/v6free/sockets/mixed/families",
             _port_families(p[2]) if s2.proc else ["<not running>"])
    s2.stop()

    # -- and what goes over the wire ---------------------------------------
    _capture_case(ctx, "default", p[3], p[4],
                  [d for d in ctxmod.default_directives(p[3])
                   if not d.startswith("bind ")],
                  "::1", ["::1", "127.0.0.1"], work)
    # The synthetic failure fixture for the same instrument: the one case that
    # is supposed to put IPv4 on the wire.
    _capture_case(ctx, "explicit-v4", p[1], p[4],
                  ctxmod.default_directives(p[1], "127.0.0.1"),
                  "127.0.0.1", ["127.0.0.1"], work)
