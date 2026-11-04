"""Where an address is joined to a port, split back, and compared as text.

A sibling of `cscan.py`, on the same preprocessed units and for the same
reason: the question is about what the compiler saw, and the answer has to
survive the preprocessor. It is a different question, though, and the trap is
the opposite one.

    `"%s:%d"` is not a macro. It survives into the AST intact, which is why
    this scan can be an AST scan at all -- and it also survives into any
    `printf` that formats a file name and a line number, which is why a scan
    that only counted the pattern would call `debug.c` an address defect.

So nothing here decides what a finding *means*. It reports, per source file,
how many string literals join a `%s` to an integer conversion with a colon, and
how many calls split a string on a `':'`. Which files those numbers are read in
is the gate's business, and the gate names the files rather than discovering
them, so a file that stops having a finding because it stopped existing is
still counted.

A third family is counted the same way and for the same reason: a call to
`strcmp`/`strcasecmp` whose two operands are address *text*. One address has
many spellings, so two different strings can be the same host, and a tree that
settles identity with a string comparison settles it wrong. Only where the
operands really are addresses, though -- which is why one of them has to be
*named* like an address, and why a comparison against a string literal
(`strcasecmp(s,"myself")`) is a keyword test rather than an identity test and
is not counted. That filter is a heuristic and says so here; what is not a
heuristic is the self-check underneath it.

The same two self-checks `cscan` makes are made here: every `.c` that compiles
has a `.i`, and the AST walk found as many joins -- and as many comparison
calls -- as a raw token scan of the same text did. A `.i` that failed to parse
partway yields an AST that is a prefix of the file, and a scan of a prefix
reports a tree cleaner than it is.
"""

import os
import re

import clang.cindex as ci

import cscan

# A `%s` and an integer conversion with a colon between them: the shape of an
# address joined to a port **without brackets**. Width, precision and flags are
# allowed on both sides because `"%.40s %s:%d"` is one of the sites this exists
# to find, and `hh` or `h` on the right because a port has been written `%hu`
# before.
#
# The wider modifiers -- `ll`, `l`, `j`, `z`, `t` -- are not allowed, and the
# reason is about what a port is rather than about which files happen to use
# them. A port is sixteen bits; nothing that has to be written as a long, an
# intmax_t or a size_t is one. `"Full resync from master: %s:%lld"` joins a
# replication identifier to a stream offset, and an offset is not a port, so
# the shape this scan counts is not the shape that line has. Excluding it by
# its conversion says why it is not counted; excluding it by its filename would
# only say that it is not.
#
# `"[%s]:%d"` deliberately does not match. The colon has to sit directly
# between the two conversions, and in the RFC 3986 form it does not -- so the
# count is of the shape the step removes rather than of every place an address
# meets a port. `anet.c` keeps one of these on purpose: the same expression
# chooses `"[%s]:%d"` for a host with a colon in it and `"%s:%d"` for one
# without, and the second branch is how an IPv4 address is written.
_CONV = r"%[-+ #0]*[0-9]*(?:\.[0-9]+)?"
ADDRESS_JOIN = re.compile(_CONV + r"s:" + _CONV + r"(?:hh|h)?[diuoxX]")

# The other half of the same shape: a `%s` in brackets with the port after the
# closing one. Counting these says *where* the bracketed form is written, which
# is a different question from whether the unbracketed one is gone -- a tree
# that spells the join out at every call site has the same defect in as many
# copies of itself as it has call sites.
BRACKETED_JOIN = re.compile(r"\[" + _CONV + r"s\]:" + _CONV
                            + r"(?:hh|h)?[diuoxX]")

# The library calls that find a delimiter inside a string. `strstr` is not one
# of them: it takes a string rather than a character, and the tree uses it to
# find `ip=` and `port=` in an INFO line, which is a field name and not a
# delimiter.
SPLIT_FUNCS = ("strchr", "strrchr", "index", "rindex", "strtok", "strtok_r",
               "strsep", "strpbrk", "memchr")

# The library calls that decide whether two strings are the same. The `n`
# variants are here too: a prefix comparison of two addresses is the same
# mistake made over fewer bytes.
COMPARE_FUNCS = ("strcmp", "strcasecmp", "strncmp", "strncasecmp")

# An operand is an address when something in it is *named* like one. The test
# is on the end of an identifier, so four suffixes catch `ip`, `cip`, `hostip`,
# `masterhost`, `slave_master_host`, `bindaddr`, `source_addr` and `peerid`
# alike; the denylist is the English words that end in "ip" and are not
# addresses.
ADDRESSY_SUFFIX = ("ip", "host", "hostname", "addr", "address", "peerid")
NOT_ADDRESSY = frozenset((
    "skip", "zip", "clip", "tip", "strip", "gzip", "chip", "flip", "slip",
    "trip", "grip", "drip", "whip", "ship", "equip", "lip", "rip", "dip",
))

# The token-scan half of the comparison self-check.
_CALL = re.compile(r"\b(?:%s)\s*\(" % "|".join(COMPARE_FUNCS))

_COLON = ord(":")
_LINEMARKER = re.compile(r'^# \d+ "')
_MARKER_FILE = re.compile(r'^# \d+ "((?:[^"\\]|\\.)*)"')


class Findings(object):
    def __init__(self):
        self.address_joins = set()      # (file, line, literal text)
        self.bracketed_joins = set()    # (file, line, literal text)
        self.colon_splits = set()       # (file, line, function)
        self.address_compares = set()   # (file, line, function)
        self.parse_errors = set()       # (file, line, message)
        self.unscanned = []             # .c files with no preprocessed text
        self.scanner_faults = []        # free text; any entry is a bug

    def by_file(self, attr, files):
        """{file: count} over exactly `files`, present or not."""
        found = getattr(self, attr)
        out = {}
        for rel in files:
            out[rel] = sum(1 for t in found if t[0] == rel)
        return out


def _literal_text(cursor):
    """The source spelling of a literal, including any adjacent concatenation.

    `cursor.spelling` carries it on the libclang this container ships; the
    token walk is the fallback, and it is also what makes a concatenated
    literal one finding rather than none.
    """
    text = cursor.spelling or ""
    if text:
        return text
    return "".join(t.spelling for t in cursor.get_tokens())


def _is_colon(cursor):
    """Does this expression denote the character `:`?"""
    if cursor.kind == ci.CursorKind.CHARACTER_LITERAL:
        sp = (cursor.spelling or "").strip()
        if sp in ("':'", '":"'):
            return True
        # Older bindings give the literal's value rather than its spelling.
        for t in cursor.get_tokens():
            if t.spelling == "':'":
                return True
        return False
    if cursor.kind == ci.CursorKind.INTEGER_LITERAL:
        for t in cursor.get_tokens():
            try:
                return int(t.spelling, 0) == _COLON
            except ValueError:
                return False
    return False


def _addressy(cursor):
    """Is any identifier in this expression named like an address?"""
    for t in cursor.get_tokens():
        if t.kind != ci.TokenKind.IDENTIFIER:
            continue
        w = t.spelling.lower()
        if w in NOT_ADDRESSY:
            continue
        if any(w.endswith(suffix) for suffix in ADDRESSY_SUFFIX):
            return True
    return False


def _compares_addresses(args):
    """Are the two sides of this comparison both address text?

    Neither side may be a string literal -- `strcasecmp(s,"myself")` asks
    whether a token is a keyword, and `strncmp(l,"slave",5)` asks whether a
    line starts with a field name. Neither is an identity question about an
    address, and both are shapes this tree really contains.
    """
    args = args[:2]
    if len(args) < 2:
        return False
    for a in args:
        for x in cscan._descend(a, 4):
            if x.kind == ci.CursorKind.STRING_LITERAL:
                return False
    return any(_addressy(a) for a in args)


def scan_unit(path, root, f, extra_args=(), index=None):
    unit_dir = os.path.dirname(os.path.abspath(path))
    idx = index or ci.Index.create()
    tu = idx.parse(path, args=["-x", "cpp-output", "-std=c99"] + list(extra_args))

    for d in tu.diagnostics:
        if d.severity < 3:
            continue
        name, line = cscan.presumed(d.location)
        where = cscan._in_tree(root, unit_dir, name)
        if where is not None:
            f.parse_errors.add((where, line, d.spelling))

    ast_joins = 0
    ast_brackets = 0
    ast_compares = 0

    def visit(c):
        nonlocal ast_joins, ast_brackets, ast_compares
        if c.kind == ci.CursorKind.STRING_LITERAL:
            hits = len(ADDRESS_JOIN.findall(_literal_text(c)))
            ast_joins += hits
            if hits:
                name, line = cscan.presumed(c.location)
                where = cscan._in_tree(root, unit_dir, name)
                if where is not None:
                    f.address_joins.add((where, line, _literal_text(c)[:200]))
            braks = len(BRACKETED_JOIN.findall(_literal_text(c)))
            ast_brackets += braks
            if braks:
                name, line = cscan.presumed(c.location)
                where = cscan._in_tree(root, unit_dir, name)
                if where is not None:
                    f.bracketed_joins.add((where, line,
                                           _literal_text(c)[:200]))
        elif c.kind == ci.CursorKind.CALL_EXPR and c.spelling in SPLIT_FUNCS:
            if any(_is_colon(x) for a in c.get_arguments()
                   for x in cscan._descend(a, 4)):
                name, line = cscan.presumed(c.location)
                where = cscan._in_tree(root, unit_dir, name)
                if where is not None:
                    f.colon_splits.add((where, line, c.spelling))
        elif c.kind == ci.CursorKind.CALL_EXPR and c.spelling in COMPARE_FUNCS:
            # Every such call is counted for the self-check; only the ones
            # whose operands look like addresses are recorded as findings.
            ast_compares += 1
            if _compares_addresses(list(c.get_arguments())):
                name, line = cscan.presumed(c.location)
                where = cscan._in_tree(root, unit_dir, name)
                if where is not None:
                    f.address_compares.add((where, line, c.spelling))
        for k in c.get_children():
            visit(k)

    visit(tu.cursor)
    _selfcheck(path, root, unit_dir, ast_joins, ast_brackets,
               ast_compares, f)


def _selfcheck(path, root, unit_dir, ast_joins, ast_brackets, ast_compares, f):
    """The AST found every join of either shape, and every comparison call,
    the unit's own text contains.

    The comparison count is taken over the lines this tree contributed. A
    preprocessed unit carries the declaration of `strcmp` from `string.h`, and
    on a glibc built for it, an inline definition as well -- neither is a call
    site in this tree. The line markers say which file each line came from and
    `cscan._in_tree` is the same test the findings themselves use, so the two
    halves of the check agree about what "this tree" means. Preprocessor
    directives are code to neither half.
    """
    with open(path, "r", errors="replace") as fh:
        text = fh.read()
    tok = 0
    brak = 0
    calls = 0
    here = None
    for line in text.splitlines():
        if _LINEMARKER.match(line):
            m = _MARKER_FILE.match(line)
            here = cscan._in_tree(root, unit_dir, m.group(1)) if m else None
            continue
        if line.lstrip().startswith("#"):
            # A unit preprocessed for debugging keeps the macro *definitions* it
            # saw as well as the text they expanded to -- jemalloc's units are
            # built that way, and its `assert()` carries a `%s:%d`. A definition
            # is not code: what the compiler was given is the expansion, which
            # stands on a line of its own and is counted there. Counting the
            # definition as well would ask the AST for a join that never
            # entered the program.
            continue
        tok += len(ADDRESS_JOIN.findall(line))
        brak += len(BRACKETED_JOIN.findall(line))
        if here is not None:
            calls += len(_CALL.findall(line))
    # A literal split across two source lines is one AST finding and two text
    # hits only if the pattern itself straddles the break, which it cannot: the
    # pattern has no whitespace in it. So these are equal or the scan is short.
    if ast_joins < tok:
        f.scanner_faults.append(
            "%s: AST found %d address-joins, text has %d"
            % (os.path.basename(path), ast_joins, tok))
    # The bracketed family is held to the same standard, and for a sharper
    # reason: it is the shape a solution *adds*, so a walk that stopped early
    # would under-report exactly the sites the step is judged on.
    if ast_brackets < brak:
        f.scanner_faults.append(
            "%s: AST found %d bracketed joins, text has %d"
            % (os.path.basename(path), ast_brackets, brak))
    # The AST side counts comparison calls anywhere in the unit and the text
    # side only in-tree ones, so the AST count is a superset and short is the
    # only direction that can mean a truncated walk.
    if ast_compares < calls:
        f.scanner_faults.append(
            "%s: AST found %d comparison calls, text has %d"
            % (os.path.basename(path), ast_compares, calls))


def scan(root, dirs, extra_args=()):
    """Scan every preprocessed unit under `dirs` and return the findings."""
    units, missing, unscanned = cscan.report_units(root, dirs)
    f = Findings()
    f.unscanned = unscanned
    if not units:
        f.scanner_faults.append("no preprocessed units under %s" % (dirs,))
    idx = ci.Index.create()
    for u in units:
        scan_unit(u, root, f, extra_args, index=idx)
    return f, units, missing


def strings_in(path):
    """Address-join literals found in a linked binary's data.

    The artifact half: a source tree with no such literal cannot produce a
    binary that has one, so this is the cross-check that the scan above read
    the build that was actually made rather than some other tree.
    """
    try:
        with open(path, "rb") as fh:
            blob = fh.read()
    except OSError:
        return None
    out = set()
    for run in re.findall(rb"[\x20-\x7e]{4,}", blob):
        s = run.decode("ascii")
        for m in ADDRESS_JOIN.finditer(s):
            out.add(s[max(0, m.start() - 24):m.end() + 24])
    return sorted(out)
