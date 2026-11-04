"""Whole-repo text facts: the backstop, and the things no compiler sees.

`cscan` reads what the compiler produced, which is the right instrument for C
and useless for everything else the repo ships -- the default config file, the
cluster helper in Ruby, the test assets, the packaging scripts. An address
written into `redis.conf` is as much a part of "this tree is IPv4-only" as one
written into `anet.c`, and nothing compiles it.

So this module works on text, with two disciplines that separate it from a
grep:

  * C and C-like sources are scrubbed of comments and string literals before
    identifiers are counted, so a name in a comment is not a finding and a name
    spliced across a line continuation still is;
  * a dotted quad is only a finding once `ipaddress` agrees it is an IPv4
    address, so `3.1.999` and a version number are not addresses and
    `0.0.0.0` is.

The identifier half is a backstop: `cscan` is the instrument that decides
whether the code is IPv4-only, and this catches the files it cannot reach --
the ones no `.i` covers, and the headers of platforms this box does not build.

Not every scan here is read by every step. `ipv4_literals` answers a question
about how addresses are *spelled*, which a step that leaves the spelling alone
must not score: a scenario reads it when its own step asks about spelling, and
the ones that do not leave it alone.
"""

import ipaddress
import json
import os
import re

# IPv4-only spellings. Each is anchored on word boundaries, so `AF_INET6`,
# `sockaddr_in6` and `in6_addr` are not matches for the v4 forms.
IPV4_IDENTIFIERS = (
    "AF_INET", "PF_INET", "sockaddr_in", "in_addr", "in_addr_t",
    "INADDR_ANY", "INADDR_LOOPBACK", "INADDR_NONE", "INADDR_BROADCAST",
    "IPPROTO_IPIP", "s_addr", "sin_addr", "sin_port", "sin_family",
    "gethostbyname", "gethostbyaddr", "inet_addr", "inet_aton", "inet_ntoa",
    "inet_network", "inet_makeaddr", "h_addr", "h_addr_list", "hostent",
)
_IDENT_RE = {name: re.compile(r"(?<![A-Za-z0-9_])%s(?![A-Za-z0-9_])" % name)
             for name in IPV4_IDENTIFIERS}

# Ways a finding gets silenced rather than fixed. These count as findings: a
# tree that suppresses a warning has not stopped producing it.
#
# `-fcommon` is here with the `-Wno-` flags rather than with the optimisation
# settings. It does not quiet a message; it restores the pre-gcc-10 default that
# merges tentative definitions, so a symbol defined in three translation units
# links again without any of the three having been fixed. That is the same move
# as switching the diagnostic off, and it is the one the toolchain half of this
# migration is about.
SUPPRESSIONS = (
    re.compile(r"#\s*pragma\s+GCC\s+diagnostic\s+ignored"),
    re.compile(r"#\s*pragma\s+clang\s+diagnostic\s+ignored"),
    re.compile(r"(?<![A-Za-z0-9_-])-Wno-[A-Za-z0-9-]+"),
    re.compile(r"(?<![A-Za-z0-9_-])-fcommon(?![A-Za-z0-9_-])"),
    re.compile(r"#\s*if\s+0(?![0-9])"),
)

C_LIKE = (".c", ".h", ".cc", ".cpp", ".hpp")
# Text the tree ships that can configure or print an address.
TEXTY = (".conf", ".tcl", ".rb", ".sh", ".py", ".md", ".txt", ".json", ".yml",
         ".yaml", "")

_DOTTED = re.compile(r"(?<![\w.])(\d{1,3}(?:\.\d{1,3}){3})(?![\w.])")
# A token that is only a number tells a reader nothing about what the address
# beside it was for -- a port, a count, a timeout. `line_site` walks past these
# looking for the token that does.
_BARE_INT = re.compile(r"^\d+$")

# Punctuation a token can be wrapped in without changing what it names.
# `$` and braces go too, so `$HOSTS`, `${HOSTS}` and `HOSTS` are one token to
# the site comparison below, which is the whole of the respelling case: a
# candidate who writes `HOSTS="${HOSTS} localhost:$PORT"` where the pin wrote
# `HOSTS="$HOSTS localhost:$PORT"` has not thrown a destination away.
_NORM = re.compile(r"[\"'`{}\[\]()$\\,;]+")


def _blank(s):
    """`s` with every character but its newlines replaced by a space."""
    return re.sub(r"[^\n]", " ", s)


def _scrub(text, strings):
    """`text` with comments -- and optionally literal bodies -- blanked out.

    Line structure is preserved so a finding still has a line number. Line
    continuations are joined first, so an identifier split across two physical
    lines is seen whole.
    """
    text = text.replace("\\\n", "")
    out = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        two = text[i:i + 2]
        if two == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(_blank(text[i:j]))
        elif two == "//":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(_blank(text[i:j]))
        elif ch in "\"'":
            j = i + 1
            while j < n and text[j] != ch:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(text[i:j] if not strings
                       else ch + _blank(text[i + 1:j - 1]) + text[j - 1:j])
        else:
            out.append(ch)
            j = i + 1
        i = j
    return "".join(out)


def scrub_c(text):
    """`text` with comments and string/char literal bodies blanked out."""
    return _scrub(text, strings=True)


def scrub_comments(text):
    """`text` with only its comments blanked out -- literal bodies kept.

    The counterpart of `scrub_c`, for a scan whose subject is what a string
    says rather than what the code around it names.
    """
    return _scrub(text, strings=False)


# The name the identifier scan below has always called it by.
_scrub_comments = scrub_comments


def _read(path):
    with open(path, "r", errors="replace") as f:
        return f.read()


# What a build leaves behind. A `.i` is 9000 lines of somebody else's headers;
# counting its `#pragma`s as the tree's own suppressions says more about glibc
# than about the tree.
BUILD_ARTIFACTS = (".i", ".s", ".o", ".a", ".so", ".d", ".lo", ".log", ".gcno",
                   ".gcda", ".gcov")

# Build bookkeeping with no extension to filter on. `src/.make-settings` and
# `deps/.make-cflags` are the project's own record of the flags the *last* build
# was run with; they are regenerated on every build and the tree's `.gitignore`
# lists them, as `.make-*` and `.prerequisites`, among its outputs rather than
# its sources. Reading them would mean reading the scorer's own `make` arguments
# back as though the tree had written them, so a flag the verifier itself passes
# would be a finding against every tree, the untouched one included.
#
# `build-report.json` and `build.log` are the scorer's own, written into the
# build copy by `lib/build.py`; the report quotes the `make` command line the
# scorer chose, which is the same problem one layer up.
#
# `.built-from` is the same kind of thing one level out: a recording run writes
# it into a staged build directory to record which tree that build came from.
# The verifier's build copy has no such file, so reading it would be one more
# way for a recording to differ from the replay it is compared against.
GENERATED_PREFIXES = (".make-",)
GENERATED = (".prerequisites", "build-report.json", "build.log", ".built-from")

# The same problem one directory down, and the reason this is a path list rather
# than a name list: `deps/jemalloc/configure` writes `Makefile` from the
# `Makefile.in` the repository actually ships, and writes `config.status` to
# record how it was invoked. Both quote the `CFLAGS` of the build that produced
# them, so a scorer that builds with a flag finds that flag in them afterwards
# -- measured, not supposed: building the pin with `-fcommon` puts `-fcommon`
# into exactly these two files and into no other file a build adds.
#
# Matched by full path. `Makefile` on its own would also exclude `src/Makefile`
# and the top-level one, which the repository does ship and in which a hard-coded
# `-Wno-` or `-fcommon` is precisely the finding this module exists to make.
#
# `bin/jemalloc.sh` and `test/test.sh` are the same thing with a `.sh` on the
# end: `configure` writes each from the `.sh.in` beside it, substituting the
# prefix and the settings it was invoked with. They are shell, so the surface
# selector below would read them as scripts the repository ships -- on a tree
# that has been built, and not on the same tree before the build.
GENERATED_PATHS = frozenset((
    os.path.join("deps", "jemalloc", "Makefile"),
    os.path.join("deps", "jemalloc", "config.status"),
    os.path.join("deps", "jemalloc", "bin", "jemalloc.sh"),
    os.path.join("deps", "jemalloc", "test", "test.sh"),
))


def _generated(name, rel=None):
    return (name in GENERATED or name.startswith(GENERATED_PREFIXES)
            or (rel is not None and rel in GENERATED_PATHS))


def walk(root, skip_dirs=(), skip_ext=BUILD_ARTIFACTS):
    """Every regular source file under `root`, repo-relative, sorted."""
    found = []
    for dirpath, dirnames, filenames in os.walk(root):
        rel = os.path.relpath(dirpath, root)
        rel = "" if rel == "." else rel
        dirnames[:] = sorted(d for d in dirnames
                             if os.path.join(rel, d) not in skip_dirs
                             and d not in (".git",))
        for name in sorted(filenames):
            p = os.path.join(dirpath, name)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            relpath = os.path.join(rel, name) if rel else name
            if os.path.splitext(name)[1] in skip_ext or _generated(name, relpath):
                continue
            found.append(relpath)
    return sorted(found)


def is_text(path):
    """A file worth reading as text: no NUL in its first 8 KiB."""
    try:
        with open(path, "rb") as f:
            return b"\0" not in f.read(8192)
    except OSError:
        return False


def identifier_hits(root, files):
    """(file, line, identifier) for every IPv4-only name in real code."""
    hits = set()
    for rel in files:
        path = os.path.join(root, rel)
        if os.path.splitext(rel)[1] not in C_LIKE or not is_text(path):
            continue
        text = scrub_c(_read(path))
        lines = text.splitlines()
        for name, rx in _IDENT_RE.items():
            for lineno, line in enumerate(lines, 1):
                if rx.search(line):
                    hits.add((rel, lineno, name))
    return hits


# The part of the repository that is read rather than merely read *by* someone:
# a configuration file, a test asset, a packaging script, a Makefile. An address
# written in one of these is a place the tree goes; an address written in
# `README.md` or in the annotated prose of a manual page is a place the reader
# is told about. The two are different claims and only the first is a defect, so
# the audit selects on that and nothing else.
#
# The empty string in `SCRIPT_EXT` is the extensionless script -- `runtest`,
# `utils/create-cluster/create-cluster`, `deps/jemalloc/configure` -- which is
# also where most of the prose files with no extension live, so the four names
# the project spells that way are excluded by name.
DOC_EXT = (".md", ".txt", ".html", ".css", ".xsl", ".1", ".rst", ".tpl")
DOC_NAMES = ("README", "INSTALL", "COPYING", "CONTRIBUTING", "ChangeLog",
             "BUGS", "MANIFESTO", "CONTRIBUTING.md")
SCRIPT_EXT = (".tcl", ".sh", ".rb", ".conf", ".py", ".pl", "")
SCRIPT_NAMES = ("Makefile",)


def script_surface(files, root=None):
    """The files of `files` that drive something, rather than describe it.

    Selection is by name, not by content, so it says the same thing about a
    tree that has changed the contents of every one of them.

    With one exception, and `root` is what buys it: an extensionless name is
    only a script if the file is text. `src/redis-server`, `src/redis-cli`,
    `deps/lua/src/lua` and the rest of what the link step writes have no
    extension either, and they land under exactly the names this selector is
    looking for. Counting them makes the surface eight files wider on a tree
    that has been built than on the same tree before the build -- measured, not
    supposed -- and the audit reads a candidate's tree in whatever state the
    candidate left it in. Callers that have a root to read from should pass it;
    the fixture this is compared against is built the same way.
    """
    out = []
    for rel in files:
        name = os.path.basename(rel)
        ext = os.path.splitext(name)[1].lower()
        if ext in DOC_EXT or name in DOC_NAMES:
            continue
        if ext in SCRIPT_EXT or name in SCRIPT_NAMES:
            if ext == "" and root is not None \
                    and not is_text(os.path.join(root, rel)):
                continue
            out.append(rel)
    return out


def ipv4_literals(root, files):
    """(file, line, address) for every dotted quad `ipaddress` accepts.

    Comments in C sources are scrubbed first and string literals are not: a
    usage message that offers the reader an IPv4 example is exactly the finding
    this is looking for, while `/* 7.18.1.1 Exact-width integer types */` and
    an RCS id are dotted quads that happen to parse as addresses.
    """
    hits = set()
    for rel in files:
        path = os.path.join(root, rel)
        if not is_text(path):
            continue
        text = _read(path)
        if os.path.splitext(rel)[1] in C_LIKE:
            text = _scrub_comments(text)
        for lineno, line in enumerate(text.splitlines(), 1):
            for m in _DOTTED.finditer(line):
                try:
                    ipaddress.IPv4Address(m.group(1))
                except ValueError:
                    continue
                if _ipv6_tail(line, m.start(1), m.group(1)):
                    continue
                hits.add((rel, lineno, m.group(1)))
    return hits


# What an IPv6 address is written with in front of the dotted quad that ends
# it in RFC 4291 section 2.2's mixed notation: the `::ffff:` of
# `::ffff:127.0.0.1`.
_V6_HEAD_CHARS = frozenset("0123456789abcdefABCDEF:")


def _ipv6_tail(line, start, quad):
    """Whether the dotted quad at `start` is the last 32 bits of an IPv6 address.

    `::ffff:127.0.0.1` is an IPv6 address: the mixed notation RFC 4291 section
    2.2 allows, and the form RFC 5952 section 5 gives a mapped address, which
    the tree is asked to print. The quad inside it is not an IPv4 address of its
    own, and the artifact scan in `g08` already reads it that way. Parsed rather
    than matched: the run of hex digits and colons in front of the quad, joined
    to the quad, has to be an address `ipaddress` accepts, so `host:127.0.0.1`
    and `12:34:127.0.0.1` are still quads.
    """
    i = start
    while i > 0 and line[i - 1] in _V6_HEAD_CHARS:
        i -= 1
    head = line[i:start]
    if not head.endswith(":"):
        return False
    try:
        ipaddress.IPv6Address(head + quad)
    except ValueError:
        return False
    return True


# The pin's destination sites, and what a candidate has to keep of them.
#
# A length rule is not enough -- correcting an address never shortens a file,
# deleting the line it is on always does, and keeping the line while dropping
# the address passes it. Nor is asking that every anchor token the pin used to
# introduce an address still be used, as a token, with something after it, on
# at least as many live lines and at least as many lines in total as the pin's
# copy used it. That is a whole-file count of an incidental token, and it is
# wrong in both directions:
#
#   an honest edit somewhere else in the file loses the cell. Renaming
#   sentinel's `mymaster`, rewriting `ARGV[0] || "127.0.0.1"` as a ternary,
#   spelling `HOSTS="$HOSTS` as `HOSTS="${HOSTS}`, removing the Solaris `-h`
#   from a different recipe of the same Makefile, folding a continuation line,
#   editing prose that happens to contain the word `bind` -- each of those
#   drops a count the check reads and none of them throws a destination away;
#
#   and dropping the destination while a trailing argument slides into its
#   place keeps the anchor live with something after it, so
#   `sentinel monitor mymaster 6379 2`, `r slaveof 0`, `cluster meet $b_port`
#   and `./hiredis-test -h -p $(REDIS_PORT)` are all still paid.
#
# Both come from the same mistake: counting tokens over a file when the question
# is about a place on a line. So what is recorded is one *site group* per
# quad-bearing line of the pin, and what is asked of a candidate's file is that
# every group still have a line of its own where a destination-looking token
# sits in the place the address sat.
#
# A group says four things:
#
#   pre    the tokens that introduce the address, each with its distance back
#          from it -- `bind` at 1, `monitor` at 2, `sentinel` at 3. Any one of
#          them is enough, which is what makes renaming one of them survivable.
#   post   the tokens that follow it, each with its distance forward. These
#          carry the one pin site that has no introducing token at all
#          (`tests/cluster/tests/04-resharding.tcl:62`, where the address
#          starts a continuation line), and they are the reason folding that
#          line into the one above it is not a failure.
#   after  every token that followed the address on the pin's line. A candidate
#          token that *is* one of these, sitting where the address was, is the
#          signature of the address having been taken away and the rest of the
#          line having slid left. So is a token that is one of the introducing
#          tokens.
#   live   whether the pin's line was a live directive. A live group needs a
#          live line; a commented-out one will take either, because
#          uncommenting an example is not this step's business.
#
# Matching is one distinct candidate line per pin group, so a file that had two
# sites cannot answer both with one line. Two addresses on the *same* pin line
# are one group: the candidate that writes `# bind 2001:db8::100 2001:db8:1::1`
# and the one that writes
# `# set c [redis_cluster {localhost:6379 localhost:6380}]` have both kept the
# site, and neither preserves the pin's token arithmetic.
#
# What this does not catch, measured rather than assumed. Matching a group to
# any line of the same shape means a file that ships two lines of one shape
# still answers after one of them is emptied. Two of the twenty-four
# destruction mutants this was rehearsed against survive for exactly that
# reason, and both are real lines of the pin rather than artefacts of the test:
#
#     tests/unit/scripting.tcl:404   `r slaveof 127.0.0.1 0`
#                             :529   `r -1 slaveof [srv 0 host] [srv 0 port]`
#     tests/test_helper.tcl:58       `set ::host 127.0.0.1`
#                          :424      `set ::host $arg`
#
# Closing them means counting lines of a shape again, which is the whole-file
# count rejected above, and the trade is not worth it: the file still carries
# the shape it is audited for, the two files are still audited for their quads
# by the other half of the same cell, and a tree that empties one of a pair of
# identical sites has not made the repository name an IPv4 address anywhere.


def norm(tok):
    """`tok` reduced to what it names: wrappers off, case folded."""
    return _NORM.sub("", tok).lower()


def dest_like(tok):
    """Whether this token could be somewhere to go.

    Not a port or a count, not an option flag, not a comment marker, and not
    pure punctuation. Deliberately generous about everything else: a variable,
    a name, a bracketed literal and a `host:port` pair are all destinations,
    and the audit's other half -- the file carries no dotted quad -- is what
    says the destination is the right one.
    """
    if tok.startswith("<") and tok.endswith(">"):
        return False                       # `<ip>`: a manual's placeholder
    t = tok.strip("\"'`{}[]()<>,;\\")
    if not t or t.startswith("#"):
        return False
    if t.startswith("-"):
        return False
    if _BARE_INT.match(t.lstrip("$")):
        return False
    return any(c.isalnum() for c in t)


def _is_addr(tok):
    return bool(_DOTTED.search(tok))


# How many lines of its own file a token may appear on and still be worth
# using as a *distant* anchor. `set`, `r`, `a` and `cluster` introduce a
# hundred lines apiece and say nothing about which of them was the site; the
# token immediately before the address always says something, however common it
# is, which is why offset 1 is exempt.
ANCHOR_MAX_LINES = 3

# And a distant anchor of one character is no anchor either: `set a [lindex
# $ids $j]` has an `a` three tokens before its last word in a file whose one
# site is `R $a cluster meet 127.0.0.1 $b_port`, and that is a coincidence, not
# a destination.
ANCHOR_MIN_CHARS = 2

# And no anchor is worth reaching for past the third token: `host` four tokens
# before the address in `host = ARGV[0] || "127.0.0.1"` matches a line of
# unrelated Ruby further down the same file.
ANCHOR_MAX_OFFSET = 3


def first_token(toks):
    """What this line starts with, with a comment marker taken off.

    A line that has been commented out still starts with the directive it
    commented out, and `# bind ...` and `#bind ...` start with the same one.
    """
    for tok in toks:
        if tok == "#":
            continue
        t = norm(tok.lstrip("#"))
        return t or None
    return None


def token_lines(lines):
    """{normalised token: how many live lines of this file use it as a token}.

    Commented lines are not counted. Half of `sentinel.conf` is prose about
    what Sentinel does, and four of the five lines that say `monitor` say it in
    a sentence rather than as the directive -- counting those would throw away
    the one anchor that survives a candidate renaming the master.
    """
    freq = {}
    for line in lines:
        if line.lstrip().startswith("#"):
            continue
        for t in set(norm(tok) for tok in line.split()):
            if t:
                freq[t] = freq.get(t, 0) + 1
    return freq


def line_site(line, freq=None):
    """The site group for one quad-bearing line, or None if it carries none.

    `pre` skips the tokens that say nothing about what the address was for --
    another address, a bare number, a lone `#` -- and, at any distance past the
    first, the tokens that introduce half the file. `post` keeps the bare
    numbers, because a port or a count following the address is exactly the
    shape a candidate destroys when it drops the destination and lets the rest
    of the line slide left. Tokens that normalise to nothing -- a lone
    backslash continuation -- are on neither list, and `post` stops at offset 6
    so the record stays a description of the site rather than of the line.
    """
    toks = line.split()
    idxs = [i for i, t in enumerate(toks) if _is_addr(t)]
    if not idxs:
        return None
    freq = freq or {}
    pre, post, after, frag = {}, {}, set(), []
    for i in idxs:
        m = _DOTTED.search(toks[i])
        frag.append([norm(toks[i][:m.start()]), norm(toks[i][m.end():])])
        for j in range(i - 1, -1, -1):
            t = toks[j]
            if _is_addr(t) or _BARE_INT.match(t) or t == "#" or not norm(t):
                continue
            if i - j > 1 and (i - j > ANCHOR_MAX_OFFSET
                              or len(norm(t)) < ANCHOR_MIN_CHARS
                              or freq.get(norm(t), 1) > ANCHOR_MAX_LINES):
                continue
            pre.setdefault((norm(t), i - j), None)
        for j in range(i + 1, min(len(toks), i + 7)):
            t = toks[j]
            if not norm(t):
                continue
            after.add(norm(t))
            if _is_addr(t) or t == "#":
                continue
            post.setdefault((norm(t), j - i), None)
    head = first_token(toks)
    return {
        "live": not line.lstrip().startswith("#"),
        # What the pin's line began with, unless it began with the address
        # itself -- then there is nothing to hold the candidate to.
        "head": None if (head is None or _is_addr(toks[0])) else head,
        "pre": sorted([list(k) for k in pre], key=lambda p: (p[1], p[0])),
        "post": sorted([list(k) for k in post], key=lambda p: (p[1], p[0])),
        "after": sorted(after),
        "frag": frag,
        "slots": len(idxs),
    }


# A destination the candidate computes rather than writes is more than one
# whitespace token: `[format_addr [get_instance_attrib redis $id host] $port]`
# where the pin wrote `127.0.0.1:$port`. The bracket depth is what says where
# such an expression ends, so the trailing anchors can be looked for past it
# rather than inside it.
_OPENERS = "([{"
_CLOSERS = ")]}"


def _depth(tok):
    return (sum(tok.count(c) for c in _OPENERS)
            - sum(tok.count(c) for c in _CLOSERS))


def span_end(toks, s):
    """The last token of the destination expression that starts at `toks[s]`.

    `s` itself for every token that opens nothing -- which is every site the
    pin carries, so this changes no recorded value -- and the token that
    closes the group for one that opens one. A group left unclosed by the end
    of the line ends at the end of the line rather than swallowing nothing.
    """
    depth = _depth(toks[s])
    if depth <= 0:
        return s
    for n in range(s + 1, len(toks)):
        depth += _depth(toks[n])
        if depth <= 0:
            return n
    return len(toks) - 1


def _is_ip_literal(tok):
    """Whether `tok`, wrappers off, is an IP address literal."""
    try:
        ipaddress.ip_address(norm(tok).strip("[]"))
    except ValueError:
        return False
    return True


def site_satisfied(toks, live, rec):
    """Whether this line has a destination where the group's address sat.

    Three things have to hold of the token in the slot, and each of them is one
    of the two ways the whole-file count above was wrong:

      it has to look like somewhere to go, so a port or a count that slid left
      into the gap is not an answer (`sentinel monitor mymaster 6379 2`,
      `r slaveof 0`);

      it must not be one of the pin line's own tokens from either side of the
      address, so the slide is not an answer even when what slid in was a name
      (`cluster meet $b_port`, `./hiredis-test -h -p $(REDIS_PORT)`);

      and it has to sit where the address sat, relative to the line's own
      tokens rather than to a count taken over the whole file, so an edit
      somewhere else in the same file cannot take the site away.

    Any one of the introducing tokens answers for its side, which is what makes
    renaming `mymaster`, or spelling `$HOSTS` as `${HOSTS}`, or writing a
    ternary where the pin wrote `||`, survivable. The trailing side is asked
    for only when the candidate's line still has something on it past the slot:
    a candidate who writes `# bind ::1` where the pin wrote
    `# bind 127.0.0.1 ::1` has kept the site and shortened the line, and a
    candidate who rewrites `[redis_cluster 127.0.0.1 6379 127.0.0.1 6380]` as
    `[redis_cluster {localhost:6379 localhost:6380}]` has kept it and moved the
    ports inside the destinations -- which is why the trailing match is by
    containment rather than by equality.

    One pin site has no introducing token at all: the address opens a
    continuation line (`tests/cluster/tests/04-resharding.tcl:62`). There the
    trailing shape is the only evidence there is, so all of the near part of it
    is required.

    The slot is a place rather than a word: a candidate may put a bracketed
    expression there instead of an address, and `span_end` is what keeps the
    trailing anchors measured from the end of that expression rather than from
    its first token. See the comment on the fragment test below for what that
    costs.
    """
    if rec["live"] and not live:
        return False
    # The same kind of line, not merely a line with the same word on it: the
    # prose two lines above `# sentinel announce-ip 1.2.3.4` also says
    # `announce-ip`, and `proc accept_test_clients {fd addr port} {` also says
    # `accept_test_clients`. Neither is the site.
    if rec["head"] and first_token(toks) != rec["head"]:
        return False
    bad = set(rec["after"]) | set(a for a, _d in rec["pre"])
    pre, post = rec["pre"], rec["post"]

    def anchored(s):
        """Whether one of the group's introducing tokens is where it was.

        True for the one pin group that has none (`04-resharding.tcl:62`, the
        address opening a continuation line), which is why that group's
        trailing shape is all that is asked of it here as well as above.
        """
        return not pre or any(s - d >= 0 and norm(toks[s - d]) == a
                              for a, d in pre)

    def satisfied(s, base):
        """The group's anchors, around a slot at `s` that ends at `base`."""
        if pre:
            if not anchored(s):
                return False
            tail = len(toks) - base - 1
            if not post:
                return True
            # A candidate may end the line where the pin carried on, but only
            # where what the pin carried on with was another destination:
            # `# bind 127.0.0.1 ::1` becoming `# bind ::1` has kept the site,
            # and `[redis_cluster 127.0.0.1 6379 ...]` becoming
            # `package provide redis_cluster 0.1` has not.
            if min(e for _a, e in post) > tail \
                    and all(dest_like(a) for a, _e in post):
                return True
            return any(base + e < len(toks) and a in norm(toks[base + e])
                       for a, e in post)
        if post:
            near = [p for p in post if p[1] <= 4]
            return bool(near) and all(base + e < len(toks)
                                      and norm(toks[base + e]) == a
                                      for a, e in near)
        return False

    def swallowed(s):
        """The group's trailing anchors, in order, anywhere past the slot.

        For the span reading only, and it exists because an expression put
        where the address was does not only push the anchors right -- it can
        take them inside itself. The pin's
        `[redis_cluster 127.0.0.1:[get_instance_attrib redis 0 port]]` has
        `redis 0 port` one, two and three tokens past the address; a candidate
        who writes `[redis_cluster [format_addr [get_instance_attrib redis 0
        host] [get_instance_attrib redis 0 port]]]` still has all three, in
        order, and no longer at any fixed distance from anything. Order and
        presence are what is left to ask, and `pre` -- which is not relaxed --
        is still what says this is the site rather than another line.
        """
        if not post:
            return True
        n = s
        for a, _e in sorted(post, key=lambda p: p[1]):
            for m in range(n + 1, len(toks)):
                if a in norm(toks[m]):
                    n = m
                    break
            else:
                return False
        return True

    # The pin's own trailing destination may stand in the slot when the
    # candidate's line ends there. `# bind 127.0.0.1 ::1` becoming
    # `# bind ::1` is the shortened line the docstring above and
    # `satisfied` both accept, and the `::1` in it is what the pin offered
    # beside the IPv4 address, not something that slid into its place --
    # but `bad` holds the pin's trailing tokens, so without this the loop
    # skipped that token before `satisfied` was ever asked. Only an address
    # literal earns it, and only as the last token on the line: a port, a
    # count or a variable that slid left (`cluster meet $b_port`) is still
    # refused. Of the pin's 34 sites, one has an address literal beside its
    # IPv4 one (`redis.conf`'s commented two-address `bind`); the golden
    # writes that line `# bind ::1` too, and kept the site only because its
    # documentation example on the line above happens to end in `::1`.
    # Without it, a tree that writes `# bind ::1` here, as the golden does,
    # and `bind ::1` -- a spelling the step-4 instruction offers by name --
    # on the live line would lose the site.
    literal_post = {a for a, _e in post if _is_ip_literal(a)}
    for s, tok in enumerate(toks):
        if not dest_like(tok):
            continue
        if norm(tok) in bad and not (s == len(toks) - 1
                                     and norm(tok) in literal_post):
            continue
        n = norm(tok)
        frag = any((not h or h in n) and (not t or t in n)
                   for h, t in rec["frag"])
        # Two readings of one slot, and either satisfies the group.
        #
        # The first is the pin's own and is asked exactly as it always was: the
        # address was a word, the fragment says what that word wrapped it in,
        # and the trailing anchors sit at their recorded distances from it.
        #
        # The second is for a candidate who put a bracketed expression where
        # the word was -- `client kill [format_addr [get_instance_attrib redis
        # 0 host] $port0]` for `client kill 127.0.0.1:$port0`. Whitespace
        # splits that into five tokens, so the trailing anchor lands four
        # places further right than it was recorded, and the fragment has no
        # word left to be about: the `:` the pin wrote moved inside the helper.
        # There the anchors are counted from the end of the expression and the
        # fragment is not asked for. Removing the address entirely is the
        # strongest form of the correction this step wants, and the first
        # reading scores it as a lost site.
        #
        # Asking both rather than replacing the first is what leaves every
        # value already recorded from the pin and from the goldens where it is.
        end = span_end(toks, s)
        if frag and satisfied(s, s):
            return True
        if end != s and (satisfied(s, end)
                         or (anchored(s) and swallowed(s))):
            return True
    return False


def sites_kept(path, records):
    """Whether every site group of `records` still has a line of its own.

    True for a file the pin recorded no site in -- a file with nothing to keep
    has not lost anything -- and False for a file that cannot be read, so a
    deleted file fails the check rather than passing it by having no lines to
    match against.
    """
    if not records:
        return True
    try:
        with open(path, errors="replace") as fh:
            lines = fh.read().splitlines()
    except OSError:
        return False
    rows = [(l.split(), not l.lstrip().startswith("#")) for l in lines]
    # One distinct line per group: augmenting-path matching, which is small
    # enough to be obvious -- no pin file carries more than five groups.
    cand = [[n for n, (toks, live) in enumerate(rows)
             if site_satisfied(toks, live, rec)] for rec in records]
    taken = {}

    def assign(k, seen):
        for n in cand[k]:
            if n in seen:
                continue
            seen.add(n)
            if n not in taken or assign(taken[n], seen):
                taken[n] = k
                return True
        return False

    return all(assign(k, set()) for k in range(len(records)))


# The pinned tree's own suppression sites, stated as a fixture rather than
# rediscovered, so that `g02` can score what a candidate *added*. The fixture
# is in `tests/fixtures`.
PIN_SUPPRESSIONS = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, os.pardir, "fixtures",
                                "pin-suppressions.json")


def pin_suppressions(path=PIN_SUPPRESSIONS):
    """{(file, text): count} the pinned tree already had, or an empty dict.

    Counted rather than merely listed. What `g02` scores is the difference
    between this and the candidate's, and a set difference cannot see a second
    `#if 0` added to a file that already had one -- which is the cheapest place
    to hide one, since the file is already on the list.

    An empty result is not silently benign -- it would make every one of the
    pin's own sites read as a candidate's addition -- so callers score its size
    alongside the findings.
    """
    out = {}
    try:
        with open(path) as f:
            for entry in json.load(f)["sites"]:
                # An entry without a count is read as one occurrence, which
                # keeps this function total rather than letting a short entry
                # raise.
                rel, text = entry[0], entry[1]
                n = entry[2] if len(entry) > 2 else 1
                out[(rel, text)] = int(n)
    except (OSError, ValueError, KeyError, TypeError, IndexError):
        return {}
    return out


def suppressions(root, files):
    """(file, line, text) for every place a finding could be silenced."""
    hits = set()
    for rel in files:
        # A document is not a build. `-fcommon` named in a sentence about the
        # toolchain silences nothing, and the handoff a candidate is asked to
        # keep accurate is exactly where that sentence gets written ("`-fcommon`
        # is no longer the default, so ..." would otherwise score as an added
        # suppression at every step). No pin site is in a `.md`.
        if rel.endswith(".md"):
            continue
        path = os.path.join(root, rel)
        if not is_text(path):
            continue
        for lineno, line in enumerate(_read(path).splitlines(), 1):
            for rx in SUPPRESSIONS:
                m = rx.search(line)
                if m:
                    hits.add((rel, lineno, m.group(0)))
    return hits
