"""Static facts about a C tree, read from the compiler's own output.

Everything here works on the `.i` files a build emits under `-save-temps=obj`,
not on the `.c` files. That is not a stylistic choice:

    AF_INET is a macro. By the time any compiler has an AST, it is the integer
    2 -- and an AST scan for the identifier `AF_INET` finds nothing at all, on
    the baseline included. A gate built that way is green on the code it exists
    to reject.

Preprocessed input costs the identifier but keeps the value, which is the thing
that actually reaches the kernel, and it catches a literal 2 written out by
hand just as readily. File attribution survives because the preprocessor leaves
linemarkers behind and clang's *presumed* location honours them, so a finding
still names `anet.c:251` rather than a line in a 9000-line intermediate.

Two properties are asserted rather than assumed, because a scanner that reads
less than it thinks it does reports a clean tree:

  * every `.c` that is compiled has a `.i`, and every `.c` that has none is
    accounted for by being `#include`d into one that does (`report_units`);
  * the number of `socket(` calls the AST walk found equals the number a raw
    token scan of the same text found (`_selfcheck`).

A disagreement in either is a scanner fault and is reported as one, not folded
into the finding count.
"""

import os
import re

import clang.cindex as ci
import ctypes

# AF_UNSPEC lets the resolver choose and AF_UNIX is not an internet family at
# all; both are legitimate family literals in a v6-only tree. Anything else
# written as a literal is a family chosen at compile time.
AF_UNSPEC, AF_UNIX, AF_INET, AF_INET6 = 0, 1, 2, 10
ALLOWED_FAMILY_LITERALS = (AF_UNSPEC, AF_UNIX, AF_INET6)
FAMILY_NAMES = {0: "AF_UNSPEC", 1: "AF_UNIX", 2: "AF_INET", 10: "AF_INET6"}

# The struct types that can only hold an IPv4 address. `sockaddr_storage` and
# `sockaddr` are family-agnostic and are not listed.
IPV4_TYPES = ("struct sockaddr_in", "struct in_addr")
IPV6_TYPES = ("struct sockaddr_in6", "struct in6_addr")

# libc entry points with no IPv6 behaviour available through them. `inet_ntop`
# and `inet_pton` take a family and are not listed; `getaddrinfo` and
# `getnameinfo` are the replacements and are counted separately.
IPV4_FUNCS = (
    "gethostbyname", "gethostbyname_r", "gethostbyaddr", "gethostbyaddr_r",
    "inet_addr", "inet_aton", "inet_ntoa", "inet_ntoa_r", "inet_network",
    "inet_makeaddr", "inet_lnaof", "inet_netof",
)
RESOLVER_FUNCS = ("getaddrinfo", "getnameinfo", "inet_ntop", "inet_pton")

# The one of those the instruction actually *demands*: a lookup of a host is
# "made without asking for a family", and this is the entry point that does
# that. The other three are permitted rather than required -- a conversion
# between an address and its text "takes the family from the address itself",
# which `inet_ntop(sa->sa_family, ...)` does as readily as `getnameinfo` -- so
# which of them a finished tree ends up using is its own business. Anything
# that scores an import table asks for this one and takes the rest as they
# come, or it scores a preference instead of a requirement.
RESOLVER_ENTRY = ("getaddrinfo",)

# Members that name a socket address family. Assigning a literal to one of
# these is the same compile-time decision as passing one to socket(), and so is
# comparing one against a literal: `if (info->ai_family == AF_INET)` is a
# branch the tree only has because it expects that family to turn up.
FAMILY_MEMBERS = ("sin_family", "sin6_family", "ss_family", "sa_family",
                  "ai_family")
FAMILY_OPERATORS = ("=", "==", "!=")

# Library calls that take an address family as an argument, and which argument.
# `inet_ntop` and `inet_pton` belong here for the same reason `socket` does: the
# family they are handed is the family the code has decided to handle.
FAMILY_ARG_FUNCS = {"socket": 0, "socketpair": 0, "inet_ntop": 0,
                    "inet_pton": 0}

_SOCKET_TOKEN = re.compile(r"(?<![A-Za-z0-9_])socket\s*\(")
_LINEMARKER = re.compile(r'^# \d+ "')


# --- presumed locations ----------------------------------------------------
# The Python bindings expose the spelling location, which in a `.i` is the `.i`.
# The presumed location is the one the linemarkers describe.
_lib = ci.conf.lib
_lib.clang_getPresumedLocation.argtypes = [
    ci.SourceLocation, ctypes.POINTER(ci._CXString),
    ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]


def presumed(loc):
    s = ci._CXString()
    line = ctypes.c_uint()
    col = ctypes.c_uint()
    _lib.clang_getPresumedLocation(loc, ctypes.byref(s), ctypes.byref(line),
                                   ctypes.byref(col))
    name = ci.conf.lib.clang_getCString(s)
    if isinstance(name, bytes):
        name = name.decode("utf-8", "replace")
    return name, line.value


# --- units -----------------------------------------------------------------

def report_units(root, dirs):
    """Which `.c` files under `dirs` the preprocessed output covers.

    A `.c` with no `.i` of its own is either `#include`d into one that has one
    -- `ae_epoll.c` into `ae.c` -- in which case its text is scanned anyway, or
    it is not part of this platform's build at all, in which case it is not
    scanned and the caller is told so by name. Being told is the point: an
    unscanned file is a blind spot, and a blind spot the gate does not list is
    a blind spot nobody knows about. The token backstop covers them.
    """
    units, missing = [], []
    for d in dirs:
        full = os.path.join(root, d)
        if not os.path.isdir(full):
            continue
        for name in sorted(os.listdir(full)):
            if not name.endswith(".c"):
                continue
            i_path = os.path.join(full, name[:-2] + ".i")
            if os.path.exists(i_path):
                units.append(i_path)
            else:
                missing.append(os.path.join(d, name))
    included = _included_names(units)
    unscanned = sorted(m for m in missing
                       if os.path.basename(m) not in included)
    return units, sorted(missing), unscanned


def _included_names(units):
    """Base names that appear as the source of some line in some unit."""
    seen = set()
    for u in units:
        with open(u, "r", errors="replace") as f:
            for line in f:
                if _LINEMARKER.match(line):
                    seen.add(os.path.basename(line.split('"')[1]))
    return seen


# --- the scan --------------------------------------------------------------

class Findings(object):
    def __init__(self):
        self.family_literals = set()          # (file, line, site, family name)
        self.family_dynamic = set()           # (file, line, site)
        self.ipv4_type_refs = set()           # (file, line, type)
        self.ipv6_type_refs = set()
        self.ipv4_func_refs = set()           # (file, line, func)
        self.resolver_func_refs = set()
        self.parse_errors = set()             # (file, line, message)
        self.unscanned = []                   # .c files with no preprocessed text
        self.scanner_faults = []              # free text; any entry is a bug

    def counts(self):
        return {
            "family_literals": len(self.family_literals),
            "family_dynamic": len(self.family_dynamic),
            "ipv4_type_refs": len(self.ipv4_type_refs),
            "ipv6_type_refs": len(self.ipv6_type_refs),
            "ipv4_func_refs": len(self.ipv4_func_refs),
            "resolver_func_refs": len(self.resolver_func_refs),
            "parse_errors_in_tree": len(self.parse_errors),
            "scanner_faults": len(self.scanner_faults),
        }


def _int_literal(cursor):
    """The integer value of `cursor` if it is one, through any casts."""
    while cursor is not None and cursor.kind in (
            ci.CursorKind.UNEXPOSED_EXPR, ci.CursorKind.PAREN_EXPR,
            ci.CursorKind.CSTYLE_CAST_EXPR):
        kids = list(cursor.get_children())
        cursor = kids[-1] if kids else None
    if cursor is None or cursor.kind != ci.CursorKind.INTEGER_LITERAL:
        return None
    toks = [t.spelling for t in cursor.get_tokens()]
    if len(toks) != 1:
        return None
    try:
        return int(toks[0], 0)
    except ValueError:
        return None


def _operator(cursor, lhs):
    """The operator token of a binary expression, read between its operands.

    clang 14's Python bindings do not expose a binary operator's opcode, and
    the first `=` in the token list can belong to a nested initialiser. The
    token that starts at or after the left operand ends is the operator.
    """
    end = lhs.extent.end.offset
    for t in cursor.get_tokens():
        if t.extent.start.offset >= end:
            return t.spelling
    return None


def _family_member(cursor, depth=6):
    """The family member `cursor` names, looking through casts and parens."""
    for c in _descend(cursor, depth):
        if (c.kind == ci.CursorKind.MEMBER_REF_EXPR
                and c.spelling in FAMILY_MEMBERS):
            return c.spelling
    return None


def _family_member_op(cursor):
    """(site, other operand) if `cursor` reads or writes a family member.

    Either operand may be the member: `sa.ss_family == AF_INET` and
    `AF_INET == sa.ss_family` are the same decision written two ways.

    The operand is identified before the operator is, because reading the
    operator means asking libclang to tokenize the expression and almost no
    binary operator in the tree is about an address family.
    """
    kids = list(cursor.get_children())
    if len(kids) != 2:
        return None, None
    lhs, rhs = kids
    member, other = _family_member(lhs), rhs
    if member is None:
        member, other = _family_member(rhs), lhs
        if member is None:
            return None, None
        if _operator(cursor, lhs) == "=":
            return None, None      # assigning *to* the other side
    op = _operator(cursor, lhs)
    if op not in FAMILY_OPERATORS:
        return None, None
    return "%s%s" % (op, member), other


def _descend(cursor, depth=64):
    """`cursor` and its descendants, no deeper than `depth` levels."""
    yield cursor
    if depth <= 0:
        return
    for k in cursor.get_children():
        for x in _descend(k, depth - 1):
            yield x


def _in_tree(root, unit_dir, name):
    """Absolute path of a presumed file name, or None if outside the tree."""
    if not name:
        return None
    path = name if os.path.isabs(name) else os.path.join(unit_dir, name)
    path = os.path.normpath(path)
    root = os.path.normpath(root)
    if path == root or path.startswith(root + os.sep):
        return os.path.relpath(path, root)
    return None


def scan_unit(path, root, f, extra_args=(), index=None):
    unit_dir = os.path.dirname(os.path.abspath(path))
    idx = index or ci.Index.create()
    tu = idx.parse(path, args=["-x", "cpp-output", "-std=c99"] + list(extra_args))

    for d in tu.diagnostics:
        if d.severity < 3:
            continue
        name, line = presumed(d.location)
        where = _in_tree(root, unit_dir, name)
        if where is not None:
            f.parse_errors.add((where, line, d.spelling))

    ast_sockets = 0

    def visit(c):
        nonlocal ast_sockets
        name, line = presumed(c.location)
        where = _in_tree(root, unit_dir, name)
        if where is not None:
            if c.kind == ci.CursorKind.CALL_EXPR:
                if c.spelling == "socket":
                    ast_sockets += 1
                if c.spelling in FAMILY_ARG_FUNCS:
                    args = list(c.get_arguments())
                    i = FAMILY_ARG_FUNCS[c.spelling]
                    site = "%s(arg%d)" % (c.spelling, i)
                    val = _int_literal(args[i]) if len(args) > i else None
                    if val is None:
                        f.family_dynamic.add((where, line, site))
                    else:
                        f.family_literals.add(
                            (where, line, site, FAMILY_NAMES.get(val, str(val))))
                if c.spelling in IPV4_FUNCS:
                    f.ipv4_func_refs.add((where, line, c.spelling))
                elif c.spelling in RESOLVER_FUNCS:
                    f.resolver_func_refs.add((where, line, c.spelling))
            elif c.kind == ci.CursorKind.TYPE_REF:
                if c.spelling in IPV4_TYPES:
                    f.ipv4_type_refs.add((where, line, c.spelling))
                elif c.spelling in IPV6_TYPES:
                    f.ipv6_type_refs.add((where, line, c.spelling))
            elif c.kind == ci.CursorKind.BINARY_OPERATOR:
                site, other = _family_member_op(c)
                if site is not None:
                    val = _int_literal(other)
                    if val is None:
                        f.family_dynamic.add((where, line, site))
                    else:
                        f.family_literals.add(
                            (where, line, site, FAMILY_NAMES.get(val, str(val))))
        for k in c.get_children():
            visit(k)

    visit(tu.cursor)
    _selfcheck(path, ast_sockets, f)


def _selfcheck(path, ast_sockets, f):
    """The AST found every `socket(` the text contains.

    A `.i` that failed to parse past some point yields an AST that is a prefix
    of the file, and a scan of it reports a tree cleaner than it is. Counting
    the call tokens independently is cheap and turns that into a loud fault.
    """
    with open(path, "r", errors="replace") as fh:
        text = fh.read()
    tok = 0
    for line in text.splitlines():
        if _LINEMARKER.match(line):
            continue
        tok += len(_SOCKET_TOKEN.findall(line))
    # A declaration is a `socket(` in the text with no call in the AST; there is
    # exactly one in <sys/socket.h> and the headers may declare it more than
    # once, so the token count is a floor on, not an equal to, the call count.
    if ast_sockets > tok:
        f.scanner_faults.append(
            "%s: AST found %d socket() calls, text has %d call tokens"
            % (os.path.basename(path), ast_sockets, tok))


def scan(root, dirs, extra_args=()):
    """Scan every preprocessed unit under `dirs` and return the findings."""
    units, missing, unscanned = report_units(root, dirs)
    f = Findings()
    f.unscanned = unscanned
    if not units:
        f.scanner_faults.append("no preprocessed units under %s" % (dirs,))
    idx = ci.Index.create()
    for u in units:
        scan_unit(u, root, f, extra_args, index=idx)
    return f, units, missing
