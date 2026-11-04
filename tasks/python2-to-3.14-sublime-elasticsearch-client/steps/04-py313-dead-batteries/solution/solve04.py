# -*- coding: utf-8 -*-
"""Golden solution for rung 04, as a list of byte-anchored edits.

    python3 work/solve04.py <tree>

Applied on top of rung 03's tree. Same shape as ``work/solve01.py`` through
``work/solve03.py``: every edit anchors on the exact bytes it expects, counts
them, and refuses to continue when a count is wrong or a replacement would be
a no-op (INSTRUCTIONS trap 7).

Two requirements, thirteen findings, and they are not the same kind of work:

  the ``cgi`` port    PEP 594 retired ``cgi`` in 3.13 and ``requests/utils.py``
                      imports it at module scope for one call,
                      ``cgi.parse_header``, inside
                      ``get_encoding_from_headers``. The replacement here is
                      CPython 3.12's own ``cgi.parse_header`` and
                      ``cgi._parseparam``, copied verbatim and made private to
                      the module. Verbatim on purpose: the function's answers
                      *are* the requirement, `email.message.Message` -- the
                      parser the standard library still ships -- disagrees with
                      it on RFC 2231 parameters and on a header with no type,
                      and the header cases in this rung's own bar are recorded
                      from the real thing.

  PEP 702             six definitions across three packages are documented as
                      deprecated and announce it in three different ways.
                      ``warnings.deprecated`` is new in 3.13 and replaces all
                      three. Decorating is half the work: ``get_host`` has two
                      callers inside urllib3 itself, so the other half is
                      rerouting them onto ``parse_url``, without which the
                      library starts warning about itself on every
                      absolute-URL request.

Two groups are OPTIONAL rather than required, because rung 03's instruction
leaves them free and a blind predecessor may have done either (NOTES section
9.7 and the rung-03 oracle that scored 0.941667 for exactly this reason):
the two hand-rolled ``warnings.warn(..., DeprecationWarning)`` blocks, which
nothing before this rung asks anyone to keep or to drop, and the
``self.log.warn`` call site, which an agent tidying deprecated calls at rung 01
could plausibly have renamed already. What keeps that permission bounded is
``_final_scan``: whichever predecessor the tree came from, none of the four
constructs this rung retires may survive it.
"""
from __future__ import print_function

import ast
import os
import sys


# (relpath, old, new, count): ``old`` must appear exactly ``count`` times.
EDITS = []
#: (relpath, old, new): applied only if present; 0 or 1 occurrences.
OPTIONAL = []

#: The two urllib3 copies. ``edit3`` writes every urllib3 edit to both: they
#: are byte-identical in the pinned tree and each is imported by a different
#: half of this codebase, so an edit to one and not the other is half a
#: migration -- and this rung has a case per copy that says so.
U3 = ("lib/urllib3", "lib/requests/packages/urllib3")


def edit(rel, old, new, count=1):
    EDITS.append((rel, old, new, count))


def edit3(rel, old, new, count=1):
    for root in U3:
        EDITS.append((root + "/" + rel, old, new, count))


def optional(rel, old, new):
    OPTIONAL.append((rel, old, new))


# --------------------------------------------------------------------------
# 1. `cgi` is gone: port the one function that used it.
#
# Minimal anchors, one construct each, because the enclosing lines are not
# this rung's business and a predecessor is free to have reflowed them. The
# import block in particular was rewritten at rung 01 (`import collections`
# became `import collections.abc`) and there is more than one way to have
# done that.
# --------------------------------------------------------------------------

edit("lib/requests/utils.py", b"import cgi\n", b"")

#: CPython 3.12's `cgi._parseparam` and `cgi.parse_header`, verbatim but for
#: the leading underscore and the `str()` of the docstring: the public names
#: are gone from the standard library and the tree needs the same answers, not
#: similar ones. Kept together and private to the module, which is what
#: `requests` itself did when it met this removal (requests 2.32's
#: `_parse_content_type_header`).
PARSE_HEADER = b'''def _parseparam(s):
    """``cgi._parseparam``, which 3.13 removed along with the module.

    Splits a header on semicolons, skipping any that fall inside a quoted
    string. Copied rather than reimplemented: the answers it gives for
    malformed input are the answers this library has always given.
    """
    while s[:1] == ';':
        s = s[1:]
        end = s.find(';')
        while end > 0 and (s.count('"', 0, end) - s.count('\\\\"', 0, end)) % 2:
            end = s.find(';', end + 1)
        if end < 0:
            end = len(s)
        f = s[:end]
        yield f.strip()
        s = s[end:]


def _parse_header(line):
    """``cgi.parse_header``, which 3.13 removed along with the module.

    Returns the value up to the first unquoted semicolon and a dict of the
    parameters after it, with RFC 2045 quoting undone. `email`'s parser is
    not a drop-in replacement for it: it defaults a missing type to
    `text/plain`, and it decodes RFC 2231 continuations into tuples where
    this one hands back the raw `name*` key untouched.
    """
    parts = _parseparam(';' + line)
    key = next(parts)
    pdict = {}
    for p in parts:
        i = p.find('=')
        if i >= 0:
            name = p[:i].strip().lower()
            value = p[i + 1:].strip()
            if len(value) >= 2 and value[0] == value[-1] == '"':
                value = value[1:-1]
                value = value.replace('\\\\\\\\', '\\\\').replace('\\\\"', '"')
            pdict[name] = value
    return key, pdict


'''

edit("lib/requests/utils.py",
     b"def get_encoding_from_headers(headers):\n",
     PARSE_HEADER + b"def get_encoding_from_headers(headers):\n")

edit("lib/requests/utils.py",
     b"    content_type, params = cgi.parse_header(content_type)\n",
     b"    content_type, params = _parse_header(content_type)\n")


# --------------------------------------------------------------------------
# 2. PEP 702 on the six deprecated definitions.
# --------------------------------------------------------------------------

# urllib3.util.url.get_host -- documented as deprecated, announces nothing.
edit3("util/url.py",
      b"from collections import namedtuple\n",
      b"import warnings\nfrom collections import namedtuple\n")
edit3("util/url.py",
      b"def get_host(url):\n",
      b'@warnings.deprecated("get_host() is deprecated; use parse_url() '
      b'instead.")\ndef get_host(url):\n')

# urllib3.filepost.iter_fields -- ".. deprecated:: 1.6", announces nothing.
edit3("filepost.py", b"import codecs\n", b"import codecs\nimport warnings\n")
edit3("filepost.py",
      b"def iter_fields(fields):\n",
      b"@warnings.deprecated(\n"
      b'    "iter_fields() is deprecated; use iter_field_objects() '
      b'instead.")\ndef iter_fields(fields):\n')

# requests.utils' two -- these announce, by hand, from inside their own
# bodies, which blames requests/utils.py instead of the caller. The blocks go
# (optionally: see the module docstring) and the decorator carries the same
# message, less the "(This warning should only appear once.)" clause, which
# was a statement about `warnings`' default filter and not about the function.
optional("lib/requests/utils.py",
         b"    warnings.warn((\n"
         b"        'In requests 3.0, get_encodings_from_content will be "
         b"removed. For '\n"
         b"        'more information, please see the discussion on issue "
         b"#2266. (This'\n"
         b"        ' warning should only appear once.)'),\n"
         b"        DeprecationWarning)\n"
         b"\n",
         b"")
optional("lib/requests/utils.py",
         b"    warnings.warn((\n"
         b"        'In requests 3.0, get_unicode_from_response will be "
         b"removed. For '\n"
         b"        'more information, please see the discussion on issue "
         b"#2266. (This'\n"
         b"        ' warning should only appear once.)'),\n"
         b"        DeprecationWarning)\n"
         b"\n",
         b"")
edit("lib/requests/utils.py",
     b"def get_encodings_from_content(content):\n",
     b"@warnings.deprecated(\n"
     b"    'In requests 3.0, get_encodings_from_content will be removed. "
     b"For '\n"
     b"    'more information, please see the discussion on issue #2266.')\n"
     b"def get_encodings_from_content(content):\n")
edit("lib/requests/utils.py",
     b"def get_unicode_from_response(r):\n",
     b"@warnings.deprecated(\n"
     b"    'In requests 3.0, get_unicode_from_response will be removed. "
     b"For '\n"
     b"    'more information, please see the discussion on issue #2266.')\n"
     b"def get_unicode_from_response(r):\n")


# --------------------------------------------------------------------------
# 3. urllib3 stops calling its own deprecated function.
#
# `is_same_host` and `connection_from_url` are the two callers, and both want
# the same three fields `get_host` returned. `parse_url` is the documented
# replacement and is already in the same module; its `scheme` is None where
# `get_host` defaulted to 'http', which is the one difference the call sites
# have to absorb.
# --------------------------------------------------------------------------

edit3("connectionpool.py",
      b"from .util.url import get_host\n",
      b"from .util.url import parse_url\n")
edit3("connectionpool.py",
      b"        # TODO: Add optional support for socket.gethostbyname "
      b"checking.\n"
      b"        scheme, host, port = get_host(url)\n",
      b"        # TODO: Add optional support for socket.gethostbyname "
      b"checking.\n"
      b"        p = parse_url(url)\n"
      b"        scheme, host, port = p.scheme or 'http', p.hostname, p.port\n")
edit3("connectionpool.py",
      b"    scheme, host, port = get_host(url)\n"
      b"    if scheme == 'https':\n",
      b"    p = parse_url(url)\n"
      b"    scheme, host, port = p.scheme or 'http', p.hostname, p.port\n"
      b"    if scheme == 'https':\n")


# --------------------------------------------------------------------------
# 4. `Logger.warn`, deprecated since 3.3 and still present at 3.14.
#
# One call site in the whole tree. The log record is identical either way --
# `warn` forwards to `warning` -- so what the rename buys is the
# DeprecationWarning the alias raises at its caller, which is inside the tree.
# --------------------------------------------------------------------------

optional("lib/analytics/client.py",
         b"self.log.warn('analytics-python queue is full')",
         b"self.log.warning('analytics-python queue is full')")


# --------------------------------------------------------------------------
# The final scan.
# --------------------------------------------------------------------------

#: Copies of `gates.DEAD_313` and `gates.DEPRECATED_SURFACE`, which this file
#: cannot import -- it ships alone, beside a tree, with no `/tests`. The gates
#: are the authority and the names here are deliberately `_` plus the gate
#: constant's own name, because that is what `build/sync.sh` matches on: it
#: refuses to install a solver whose copy of a gate constant has drifted.
_DEAD_313 = frozenset((
    "aifc", "audioop", "cgi", "cgitb", "chunk", "crypt", "imghdr", "lib2to3",
    "mailcap", "msilib", "nis", "nntplib", "ossaudiodev", "pipes", "sndhdr",
    "spwd", "sunau", "telnetlib", "uu", "xdrlib"))
_DEPRECATED_SURFACE = frozenset((
    "get_host", "iter_fields",
    "get_encodings_from_content", "get_unicode_from_response"))


def _is_deprecated_decorator(node):
    if isinstance(node, ast.Call):
        node = node.func
    if isinstance(node, ast.Attribute):
        return node.attr == "deprecated"
    return isinstance(node, ast.Name) and node.id == "deprecated"


def _module_level_defs(body):
    """Every `def` in `body` that binds a module-level name.

    A mirror of `gates._module_level_defs`, and for the same reason: a `def`
    inside a top-level `try` or `if` still binds a module global, and this
    tree is built out of `try: import X` / `except ImportError:` blocks.
    Recursion stops at a class or a function, so `MockRequest.get_host` --
    which must not be decorated -- stays out of scope.
    """
    for node in body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            yield node
            continue
        if isinstance(node, ast.ClassDef):
            continue
        for field in ("body", "handlers", "orelse", "finalbody", "cases"):
            sub = getattr(node, field, None)
            if isinstance(sub, list):
                for found in _module_level_defs(sub):
                    yield found


_WARNINGS_MODULES = ("warnings", "_warnings")


def _names_warnings_module(node, aliases):
    """Is `node` an expression naming the `warnings` module?

    `gates._names_the_warnings_module`, rule for rule: a local name bound to
    the module, or an Attribute chain whose last component is it -- a dotted
    `import pkg.warnings` binds only `pkg`, so there is no local name to
    match and the receiver carries the test -- but not an attribute of the
    receiver, because `self.warnings` is an instance attribute wearing the
    module's name and the gate reports it.
    """
    if isinstance(node, ast.Name):
        return node.id in aliases
    if isinstance(node, ast.Attribute):
        if node.attr not in _WARNINGS_MODULES:
            return False
        root = node.value
        while isinstance(root, ast.Attribute):
            root = root.value
        if isinstance(root, ast.Name) and root.id in ("self", "cls"):
            return False
        return True
    return False


def _warnings_module_names(parsed):
    """Local names this module binds to the `warnings` module.

    `gates._warnings_module_aliases`, rule for rule, because arm (e) below
    would otherwise be a *superset* of `no-logger-warn-alias`: `_warnings` is
    the C accelerator, a dotted import binds its root rather than the module,
    and `w = warnings` rebinds it to a plain local. A tree spelling a correct
    `warnings.warn` any of those three ways passes the gate, so it has to
    pass this scan too -- and, since rung 04's round-2 review, a name rebound
    away from the module is dropped here as it is there.
    """
    # Seeded, not started empty: `gates._warnings_module_aliases` is, because
    # a module can be bound without an `Import` node above it, and arm (e) has
    # to stay a subset of the gate.
    names = set(_WARNINGS_MODULES)
    for node in ast.walk(parsed):
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            for alias in node.names:
                if alias.name.rsplit(".", 1)[-1] in _WARNINGS_MODULES:
                    if alias.asname:
                        names.add(alias.asname)
                    elif "." not in alias.name:
                        names.add(alias.name)
    for node in ast.walk(parsed):
        if isinstance(node, ast.Assign) and _names_warnings_module(
                node.value, names):
            for t in node.targets:
                if isinstance(t, ast.Name):
                    names.add(t.id)
    # The other direction, mirrored from the gate: a name rebound *away* from
    # the module is no longer the module. Only an Attribute or a Call value
    # discards, so that the seeded `_warnings = None` spelling survives.
    for node in ast.walk(parsed):
        if not isinstance(node, ast.Assign):
            continue
        if not isinstance(node.value, (ast.Attribute, ast.Call)):
            continue
        if _names_warnings_module(node.value, names):
            continue
        for t in node.targets:
            if isinstance(t, ast.Name):
                names.discard(t.id)
    return names


def _final_scan(tree):
    """None of the four constructs this rung retires survives the edits.

    Byte anchors are exact, which makes them precise and makes them blind: an
    anchor that is missing is a loud failure, but a construct that is present
    and covered by no anchor is silent. It was silent once, at rung 03, and
    cost that oracle 0.058333 with every one of its 35 anchors matching.

    Deliberately never a *superset* of what the five gates score: a solver
    that disagreed with the verifier by finding more than it does would be
    worse than no check at all. So bare-`Name` calls only, and the
    attribute-receiver rules `no-deprecated-surface-callers` applies are not
    reproduced here -- `p.hostname` is the named replacement and is silent
    there exactly as it is here. Arms (d) and (e) are the gate's rule exactly,
    because neither has a narrowing left to get wrong: module scope is module
    scope, and `warnings.warn` is separated from `Logger.warn` by its receiver
    in both places.

    Parsed from **bytes**, not from decoded text: one module in this tree is
    not UTF-8 and `ast.parse` honours the PEP 263 declaration.
    """
    bad = []
    for dirpath, dirnames, filenames in os.walk(tree):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for fn in sorted(filenames):
            if not fn.endswith(".py"):
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, tree)
            with open(path, "rb") as fh:
                src = fh.read()
            try:
                parsed = ast.parse(src)
            except SyntaxError as exc:
                bad.append("%s: does not parse: %s" % (rel, exc))
                continue
            warnalias = _warnings_module_names(parsed)

            # (a) the retired module, imported either way.
            for node in ast.walk(parsed):
                if isinstance(node, ast.Import):
                    for alias in node.names:
                        if alias.name.split(".")[0] in _DEAD_313:
                            bad.append("%s:%d: import %s"
                                       % (rel, node.lineno, alias.name))
                elif isinstance(node, ast.ImportFrom):
                    # `node.level` guards the subset promise above: a relative
                    # import names no top package, so `gates.imports_of`
                    # skips it and `from .cgi import x` -- a sibling module,
                    # not the dead stdlib one -- has to be skipped here too.
                    if (not node.level
                            and (node.module or "").split(".")[0] in _DEAD_313):
                        bad.append("%s:%d: from %s import"
                                   % (rel, node.lineno, node.module))

                # (b) a hand-rolled DeprecationWarning anywhere.
                elif isinstance(node, ast.Call):
                    f = node.func
                    warn = ((isinstance(f, ast.Attribute) and f.attr == "warn")
                            or (isinstance(f, ast.Name) and f.id == "warn"))
                    if warn:
                        args = list(node.args) + [
                            kw.value for kw in node.keywords
                            if kw.arg in (None, "category")]
                        for a in args:
                            n = getattr(a, "attr", None) or getattr(a, "id", None)
                            if n == "DeprecationWarning":
                                bad.append("%s:%d: warns by hand"
                                           % (rel, node.lineno))
                                break
                    # (c) a bare call to one of the deprecated definitions.
                    if isinstance(f, ast.Name) and f.id in _DEPRECATED_SURFACE:
                        bad.append("%s:%d: calls %s()"
                                   % (rel, node.lineno, f.id))
                    # (e) `Logger.warn`, deprecated since 3.3. The receiver is
                    # the whole test: `warnings.warn` is a different function
                    # and is not deprecated. This is the arm that bounds the
                    # OPTIONAL `self.log.warn` rename -- a predecessor that
                    # spelled that line differently leaves the alias in place
                    # and the anchor silently does not apply.
                    if (isinstance(f, ast.Attribute) and f.attr == "warn"
                            and not _names_warnings_module(f.value,
                                                           warnalias)):
                        bad.append("%s:%d: calls .warn(), the deprecated "
                                   "alias of .warning()" % (rel, node.lineno))

            # (d) a module-scope definition of one, with no marker.
            for node in _module_level_defs(parsed.body):
                if node.name not in _DEPRECATED_SURFACE:
                    continue
                if not any(_is_deprecated_decorator(d)
                           for d in node.decorator_list):
                    bad.append("%s:%d: def %s is not marked"
                               % (rel, node.lineno, node.name))
    if bad:
        raise SystemExit(
            "solve04: %d retired construct(s) survive the edits -- this tree "
            "is not the predecessor the anchors were written against:\n  %s"
            % (len(bad), "\n  ".join(bad[:20])))
    return True


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: solve04.py <tree>")
    tree = os.path.abspath(argv[1])

    applied = 0
    for rel, old, new, count in EDITS:
        path = os.path.join(tree, rel)
        with open(path, "rb") as fh:
            src = fh.read()
        n = src.count(old)
        if n != count:
            raise SystemExit("%s: anchor appears %d times, expected %d:\n%r"
                             % (rel, n, count, old[:160]))
        out = src.replace(old, new)
        if out == src:
            raise SystemExit("%s: edit changed nothing" % rel)
        with open(path, "wb") as fh:
            fh.write(out)
        applied += 1

    n_optional = 0
    for rel, old, new in OPTIONAL:
        path = os.path.join(tree, rel)
        with open(path, "rb") as fh:
            src = fh.read()
        n = src.count(old)
        if n > 1:
            raise SystemExit("%s: optional anchor appears %d times:\n%r"
                             % (rel, n, old[:160]))
        if n:
            with open(path, "wb") as fh:
                fh.write(src.replace(old, new))
            n_optional += 1

    _final_scan(tree)
    print("solve04: %d edits (+%d of %d optional), 0 retired constructs left"
          % (applied, n_optional, len(OPTIONAL)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
