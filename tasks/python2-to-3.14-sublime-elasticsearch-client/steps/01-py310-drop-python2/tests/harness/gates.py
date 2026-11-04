# -*- coding: utf-8 -*-
"""Migrated-ness gates: what each rung requires, read off artifacts.

    python -m harness.gates <tree> --step 03-py312-tls-and-retired-stdlib
    python -m harness.gates <tree> --census
    python -m harness.gates --selftest

migration_prompt.md rule 11 says a migrated-ness gate reads artifacts or ASTs,
not text. Everything here parses the file -- ``ast`` for constructs, ``tokenize``
for the two things the AST throws away (string prefixes and comments),
``compile()`` under ``warnings.catch_warnings`` for the ones the compiler itself
is the authority on -- or asks the filesystem whether a path is gone. Nothing
greps.

The reason is not purity. A regex for ``unicode(`` matches the word inside a
docstring, inside ``# unicode(x) used to be needed here``, and inside the string
literal ``"unicode"``; it misses ``u = unicode; u(x)``. A gate that can be
passed by editing a comment is not a gate, and a gate that fires on a comment
makes the step unpassable. Both failure directions have a fixture in
``--selftest``.

Layout
------
``check_*``   one function per construct family, over a parsed unit.
``GATES``     one entry per requirement: id, the sentence the instruction has
              to state, the check, and the scope it applies to.
``STEPS``     ``(step, adds, drops)``. Running rung k runs the gates of rungs
              1..k minus anything dropped by then, because a rung that
              reintroduces a construct an earlier rung removed has not
              preserved the earlier rung's work.

Scope
-----
Gates apply to every ``.py`` file in the tree, including ``lib/analytics/test/``
(the vendored suite the agent inherits): a Python-2 spelling there is still one
in the shipped tree, and unlike the *scored behaviour* families, a static gate
over inherited code does not violate rule 6 -- the agent can read every file the
gate reads. Individual gates narrow that with ``scope`` where the requirement
itself is narrower, and each one says why.

Reporting
---------
Exit code is 1 if any gate has findings, 0 otherwise, and ``--json`` writes the
whole finding list. Both halves matter for INSTRUCTIONS trap 7: the gate's
verdict is a count that has to move, and the findings say exactly which lines
have to move it.
"""
from __future__ import print_function

import argparse
import ast
import builtins
import fnmatch
import gzip
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import tokenize
import warnings

# --------------------------------------------------------------- paths ----


def match(rel, pattern):
    """Segment-aware glob: ``*`` stops at ``/``, ``**`` crosses it.

    ``fnmatch`` alone will not do. Its ``*`` matches ``/`` as happily as any
    other character, so ``lib/elasticsearch/*.py`` also matches
    ``lib/elasticsearch/client/__init__.py`` -- which silently pulled the 173
    ``@query_params`` wrappers of the whole client API into a scope meant to
    hold six files, and made a gate report a number over a set nobody chose.
    Caught by measuring the scope rather than trusting it (INSTRUCTIONS trap 6);
    fixtured in :func:`selftest`.
    """
    rparts = rel.replace(os.sep, "/").split("/")
    pparts = pattern.split("/")

    def walk(i, j):
        while j < len(pparts):
            if pparts[j] == "**":
                for k in range(i, len(rparts) + 1):
                    if walk(k, j + 1):
                        return True
                return False
            if i >= len(rparts) or not fnmatch.fnmatch(rparts[i], pparts[j]):
                return False
            i += 1
            j += 1
        return i == len(rparts)

    return walk(0, 0)


def match_any(rel, patterns):
    return any(match(rel, p) for p in patterns)


# --------------------------------------------------------------- units ----


class Unit(object):
    """One parsed file: the three views the checks need."""

    __slots__ = ("rel", "path", "src", "tree", "_tokens")

    def __init__(self, rel, path, src, tree):
        self.rel = rel
        self.path = path
        self.src = src
        self.tree = tree
        self._tokens = None

    @property
    def tokens(self):
        if self._tokens is None:
            try:
                self._tokens = list(
                    tokenize.tokenize(io.BytesIO(self.src).readline))
            except (tokenize.TokenError, SyntaxError, IndentationError):
                self._tokens = []
        return self._tokens


def units(tree, scope=None):
    """Parse every ``.py`` under ``tree``, optionally narrowed to ``scope``.

    A file that will not parse is not silently skipped -- it is yielded as a
    finding by :func:`unparsable`, because "the gate found nothing" and "the
    gate could not look" must not produce the same number (trap 3).
    """
    out, broken = [], []
    for dirpath, dirnames, filenames in os.walk(tree):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for name in sorted(filenames):
            if not name.endswith(".py"):
                continue
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, tree)
            if scope and not match_any(rel, scope):
                continue
            with open(path, "rb") as fh:
                src = fh.read()
            try:
                node = ast.parse(src, filename=rel)
            except SyntaxError as exc:
                broken.append((rel, exc.lineno or 0, "does not parse: %s" % exc))
                continue
            out.append(Unit(rel, path, src, node))
    return out, broken


# -------------------------------------------------------------- checks ----
#
# Every check is ``f(unit) -> iterable[(lineno, detail)]``.

PY2_BUILTINS = frozenset((
    "basestring", "unicode", "xrange", "long", "unichr", "raw_input",
    "execfile", "reduce", "apply", "cmp", "buffer", "intern", "coerce"))

PY2_METHODS = frozenset((
    "iteritems", "itervalues", "iterkeys", "has_key", "iterlists"))

COLLECTIONS_ABCS = frozenset((
    "AsyncGenerator", "AsyncIterable", "AsyncIterator", "Awaitable",
    "ByteString", "Callable", "Collection", "Container", "Coroutine",
    "Generator", "Hashable", "ItemsView", "Iterable", "Iterator", "KeysView",
    "Mapping", "MappingView", "MutableMapping", "MutableSequence",
    "MutableSet", "Reversible", "Sequence", "Set", "Sized", "ValuesView"))

#: Standard-library modules the ladder's interpreters stop shipping, split by
#: the release that dropped them. The split is not cosmetic: ``imp`` goes at
#: **3.12**, and ``ElasticsearchClient.py`` line 3 is ``from imp import
#: reload``, so a single combined gate landing at step 04 would leave step 03
#: standing on a tree that cannot be imported by step 03's own interpreter --
#: every behaviour case would fail for a reason the step never asked about
#: (INSTRUCTIONS trap 9). Measured against §2.3b's break map, not guessed.
DEAD_312 = frozenset(("asynchat", "asyncore", "distutils", "imp", "smtpd"))

DEAD_313 = frozenset((
    "aifc", "audioop", "cgi", "cgitb", "chunk", "crypt", "imghdr", "lib2to3",
    "mailcap", "msilib", "nis", "nntplib", "ossaudiodev", "pipes", "sndhdr",
    "spwd", "sunau", "telnetlib", "uu", "xdrlib"))

#: The four functions this tree already documents as deprecated, by the name
#: they are defined under. Two of them are defined twice (once in each vendored
#: copy of urllib3), so the set names four functions and six definitions:
#:
#:   ``util/url.py:get_host``          "Deprecated. Use :func:`.parse_url`."
#:   ``filepost.py:iter_fields``       ".. deprecated:: 1.6"
#:   ``requests/utils.py``'s two       announced by a hand-rolled
#:                                     ``warnings.warn(..., DeprecationWarning)``
#:
#: A *name* set rather than a docstring scan, for the same reason
#: ``SSL_DEAD_NAMES`` is one: the instruction states these six definitions
#: verbatim, so the gate and the instruction can be read against each other,
#: and a rule inferred from prose ("any docstring that says deprecated") would
#: be a requirement the agent has to guess the boundary of.
#:
#: The name alone is not the test. ``requests/cookies.py`` defines
#: ``MockRequest.get_host`` -- the ``http.cookiejar`` request interface, which
#: has nothing to do with urllib3's and must neither be decorated nor rerouted
#: -- so both checks below are restricted to *module-level* definitions and to
#: calls that resolve to a module-level binding. That method is the clean
#: fixture for both (trap 6): a gate keyed on the bare name fires on it and
#: makes the only correct answer unpassable.
DEPRECATED_SURFACE = frozenset((
    "get_host", "iter_fields",
    "get_encodings_from_content", "get_unicode_from_response"))

#: ``ssl`` names removed or refused by the interpreters from 3.12 on.
SSL_DEAD_NAMES = frozenset((
    "wrap_socket", "match_hostname", "CertificateError",
    "PROTOCOL_SSLv2", "PROTOCOL_SSLv3", "PROTOCOL_SSLv23"))

#: ``datetime`` constructors deprecated at 3.12 and scheduled for removal.
NAIVE_UTC = frozenset(("utcnow", "utcfromtimestamp"))


def _root(name):
    return (name or "").split(".")[0]


def imports_of(unit, roots):
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                if _root(alias.name) in roots:
                    yield node.lineno, "import %s" % alias.name
        elif isinstance(node, ast.ImportFrom):
            if node.level:            # a relative import names no top package
                continue
            if _root(node.module) in roots:
                yield node.lineno, "from %s import %s" % (
                    node.module, ", ".join(a.name for a in node.names))


def check_legacy_future(unit):
    """Every ``__future__`` feature except ``annotations``.

    Not a blanket ban. The five this tree imports (``print_function``,
    ``unicode_literals``, ``absolute_import``, ``division``,
    ``with_statement``) have been mandatory since 3.0 and do nothing;
    ``annotations`` is the one that still changes behaviour, step 05 *requires*
    it, and step 07 removes it again once PEP 649 makes it redundant. A single
    gate over all six would make step 01 and step 05 contradict each other.
    """
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.ImportFrom) and node.module == "__future__":
            for alias in node.names:
                # Exactly the five item 5 names, not "everything but
                # ``annotations``": banning a feature the instruction never
                # mentions (``generator_stop``, say) would be a check with no
                # requirement behind it. Round-5 review, section 7.14.
                if alias.name in LEGACY_FUTURE:
                    yield node.lineno, "from __future__ import %s" % alias.name


def check_future_except_annotations(unit):
    """``from __future__ import annotations`` only -- everything else is gone
    by then, and this is the one PEP 649 makes redundant rather than illegal."""
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.ImportFrom) and node.module == "__future__":
            for alias in node.names:
                if alias.name == "annotations":
                    yield node.lineno, "from __future__ import annotations"


def _walk_all(body):
    """``ast.walk``, over a list of statements rather than one node."""
    for node in body:
        for found in ast.walk(node):
            yield found


def _is_type_checking(test):
    """``TYPE_CHECKING`` or ``typing.TYPE_CHECKING``, either spelling."""
    if isinstance(test, ast.Name):
        return test.id == "TYPE_CHECKING"
    return isinstance(test, ast.Attribute) and test.attr == "TYPE_CHECKING"


def check_type_checking_imports(unit):
    """An import the run time never makes, under annotations it now evaluates.

    Under PEP 563 the block was the point: the annotation was the *text* of
    the expression for ever, nothing evaluated it, and the import could be
    skipped -- which is how step 05 annotates ``connection/base.py`` with a
    ``TypedDict`` that costs the shipped plugin nothing. PEP 649 evaluates the
    annotation the moment something reads it, so the name has to be there:
    ``__annotations__``, ``typing.get_type_hints``, ``dataclasses.fields``,
    ``inspect.signature(eval_str=True)`` and every framework that reads a
    signature raise ``NameError`` instead, from a module that imported
    cleanly.

    Module level only, and only for the imports. A block inside a class body
    *declares* something rather than importing it -- ``connection/base.py``
    uses one to say what ``perform_request`` looks like without giving the
    base a method it has never had at run time -- and materialising that
    changes the class's run-time surface, which is not what this rung asks
    for. A module-level block holding anything other than an import (a
    ``TypedDict``, a PEP 695 alias) is not reported either: those bind no name
    the import system has to find, and the annotations that use them are
    caught by the same read.

    Module level means "runs when the module is imported", not "at column
    zero": a block under a ``try`` is still a block, and reading only
    ``tree.body`` is how the same check went blind at rung 04.

    Only the ``if`` body, though, never the ``else``. The else branch of
    ``if TYPE_CHECKING:`` is the branch the interpreter *does* take, so an
    import sitting there is an ordinary run-time import and binds the name
    this rung wants bound.
    """
    seen = set()
    for node in _module_level(unit.tree.body):
        if not isinstance(node, ast.If) or not _is_type_checking(node.test):
            continue
        for stmt in _walk_all(node.body):
            if stmt in seen or not isinstance(stmt, (ast.Import, ast.ImportFrom)):
                continue
            seen.add(stmt)
            if isinstance(stmt, ast.Import):
                for alias in stmt.names:
                    yield stmt.lineno, "TYPE_CHECKING-only: import %s" % alias.name
            else:
                yield stmt.lineno, "TYPE_CHECKING-only: from %s%s import %s" % (
                    "." * stmt.level, stmt.module or "",
                    ", ".join(a.name for a in stmt.names))


def check_future_annotations_missing(unit):
    """The step-05 half of the same construct: it has to be *there*.

    Only for a module that actually carries an annotation -- requiring the
    import in a file with nothing to annotate would be a requirement about
    nothing. On 3.13 this is what keeps a ``TYPE_CHECKING``-only name usable in
    a signature, which is why step 05's solution needs it at all.
    """
    annotated = False
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.AnnAssign):
            annotated = True
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            a = node.args
            if node.returns is not None or any(
                    x.annotation is not None for x in
                    list(getattr(a, "posonlyargs", [])) + list(a.args)
                    + list(a.kwonlyargs)
                    + [y for y in (a.vararg, a.kwarg) if y]):
                annotated = True
    if not annotated:
        return
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.ImportFrom) and node.module == "__future__":
            if any(alias.name == "annotations" for alias in node.names):
                return
    yield 1, "annotated module without `from __future__ import annotations`"


def check_six(unit):
    for hit in imports_of(unit, {"six"}):
        yield hit
    # ``six`` reached relatively. ``imports_of`` matches the *root* of an
    # absolute module path, which is blind to every spelling the vendored
    # libraries actually use: the root of ``from .packages.six import b`` is
    # ``packages``. Two shapes, both present in this tree -- the shim imported
    # as a name (``from .packages import six``, 10 sites) and the shim imported
    # *through* (``from .packages.six import ...``, 8 sites). Matching any
    # component of the dotted path covers both and keeps ``sixty`` alone.
    for node in ast.walk(unit.tree):
        if not isinstance(node, ast.ImportFrom) or not node.level:
            continue
        dots = "." * node.level
        if "six" in (node.module or "").split("."):
            yield node.lineno, "from %s%s import ..." % (dots, node.module)
            continue
        for alias in node.names:
            if alias.name == "six":
                yield node.lineno, "from %s%s import six" % (dots,
                                                            node.module or "")
    # The shim renamed and imported back under its own name
    # (``import _six as six``) satisfies both ``no-six`` and ``six-deleted``
    # while leaving the two-interpreter layer standing. Bind the *name*, not
    # just the module path. Round-5 review, section 7.14.
    for node in ast.walk(unit.tree):
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            for alias in node.names:
                if alias.asname == "six":
                    yield node.lineno, "import ... as six"


def check_py2_builtins(unit):
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.Name) and node.id in PY2_BUILTINS:
            yield node.lineno, node.id


def check_py2_methods(unit):
    # ``.next()`` calls are deliberately *not* here. The gate used to flag any
    # call of an attribute named ``next``, on the reasoning that the call is
    # the Python 2 iterator spelling however it is written. An AST census of
    # the pinned tree says otherwise: of six such call sites, three are inside
    # the three ``six.py`` copies the rung deletes outright, and the other
    # three are ``analytics.Consumer.next`` -- a ``Thread`` subclass with
    # neither ``__iter__`` nor ``__next__``, whose ``next()`` returns a whole
    # batch of queue items and for which ``next(consumer)`` would be simply
    # wrong. So the check had zero true positives and three false ones, and it
    # scored a rename the instruction does not ask for: item 3 says renaming
    # such a method "is fine", not that it is required. Two correct ports, one
    # of them 0.078 poorer for keeping a legal Python 3 method name.
    #
    # The five method *calls* below are a different matter -- the instruction
    # bans those outright -- and ``sweep_cases`` still refuses ``.next()`` in a
    # case body, because a case that calls one pins a spelling a legal answer
    # is allowed to delete. Found by the round-3 static review; section 7.9.
    #
    # The round-4 review then found the hole that removal left: item 3 also
    # says an iterator is advanced with ``next(x)``, and with ``.next`` gone
    # from the table nothing scored that half of the sentence. The answer is
    # not to resolve receivers -- undecidable in general -- but to score the
    # *alias* the spelling depends on. ``_iterator_next_alias`` below does
    # that, and once it is gone ``x.next()`` raises ``AttributeError``, which
    # the behaviour suite catches on any driven path. Section 7.11.
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.Attribute) and node.attr in PY2_METHODS:
            yield node.lineno, ".%s" % node.attr
    for lineno, detail in _iterator_next_alias(unit):
        yield lineno, detail


def _iterator_next_alias(unit):
    """A class that defines both ``next`` and ``__next__``.

    That pair is the Python 2 iterator shim and nothing else: ``next =
    __next__`` (or a forwarding ``def next``) so 2.7 could drive the object
    3.x drives through ``__next__``. Nothing on Python 3 reads the alias.

    Scoring the alias rather than the call site is what keeps
    ``analytics.Consumer`` safe. Its class defines ``next`` and *not*
    ``__next__``, so the pair never forms and the round-3 answer stands
    untouched: a method merely spelled ``next`` is an ordinary Python 3 name.
    The pinned tree has exactly two of these -- ``_timelex`` in
    ``dateutil/parser.py`` and ``_genitem`` in ``dateutil/rrule.py``.
    """
    for node in ast.walk(unit.tree):
        if not isinstance(node, ast.ClassDef):
            continue
        defined = {}
        for stmt in node.body:
            if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef)):
                defined.setdefault(stmt.name, stmt.lineno)
            elif isinstance(stmt, ast.Assign):
                for target in stmt.targets:
                    if isinstance(target, ast.Name):
                        defined.setdefault(target.id, stmt.lineno)
        if "next" in defined and "__next__" in defined:
            yield defined["next"], "%s defines next alongside __next__" % node.name


def check_version_branches(unit):
    # ``from sys import version_info`` is the same construct with the module
    # name spelled once at the top instead of at every use, and the pinned
    # tree's one non-``six`` version branch is written exactly that way
    # (``requests/packages/chardet/__init__.py:19``). Matching only the
    # attribute form left it free the moment ``unicode`` disappeared from the
    # line below it. Both the import and the bare name are scored; the tree
    # has no other binding of that name, so there is nothing else to hit.
    # Round-4 review, section 7.11.
    # The bare name only counts where the module actually took it from
    # ``sys``. Scoring every ``Name`` called ``version_info`` would fail a
    # legal port that happened to define its own -- a check broader than the
    # requirement, which is the same rule-4 defect as a requirement with no
    # check, pointing the other way. Round-5 review, section 7.14.
    from_sys = any(
        isinstance(n, ast.ImportFrom) and n.module == "sys"
        and any(a.name == "version_info" for a in n.names)
        for n in ast.walk(unit.tree))
    for node in ast.walk(unit.tree):
        if (isinstance(node, ast.Attribute) and node.attr == "version_info"
                and isinstance(node.value, ast.Name)
                and node.value.id == "sys"):
            yield node.lineno, "sys.version_info"
        if isinstance(node, ast.ImportFrom) and node.module == "sys":
            for alias in node.names:
                if alias.name == "version_info":
                    yield node.lineno, "from sys import version_info"
        if (from_sys and isinstance(node, ast.Name)
                and node.id == "version_info"):
            yield node.lineno, "version_info"
        # ``PY2``/``PY3`` in the spellings this tree uses. ``is_py2`` is the
        # same flag with a longer name (``requests/compat.py`` derives both
        # from ``sys.version_info``); recoding it to a constant and keeping
        # ``if is_py2:`` would leave a version flag standing.
        if isinstance(node, ast.Name) and node.id in PY2_FLAG_NAMES:
            yield node.lineno, node.id


def check_collections_abc(unit):
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.ImportFrom) and node.module == "collections":
            for alias in node.names:
                if alias.name in COLLECTIONS_ABCS:
                    yield node.lineno, "from collections import %s" % alias.name
        if (isinstance(node, ast.Attribute) and node.attr in COLLECTIONS_ABCS
                and isinstance(node.value, ast.Name)
                and node.value.id == "collections"):
            yield node.lineno, "collections.%s" % node.attr


def check_fractions_gcd(unit):
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.ImportFrom) and node.module == "fractions":
            for alias in node.names:
                if alias.name == "gcd":
                    yield node.lineno, "from fractions import gcd"
        if (isinstance(node, ast.Attribute) and node.attr == "gcd"
                and isinstance(node.value, ast.Name)
                and node.value.id == "fractions"):
            yield node.lineno, "fractions.gcd"


def check_u_prefix(unit):
    """``u'...'`` -- invisible to the AST, which keeps only the value.

    ``tokenize`` rather than a regex: it sees the prefix as part of a STRING
    token, so a ``u`` that happens to end a preceding identifier, or the letter
    inside a comment, cannot match.
    """
    for tok in unit.tokens:
        if tok.type == tokenize.STRING:
            prefix = tok.string[:tok.string.index(tok.string.lstrip("uUbBrRfF")[0])] \
                if tok.string.lstrip("uUbBrRfF") else ""
            if "u" in prefix.lower():
                yield tok.start[0], tok.string[:24]


def check_dead_312(unit):
    return imports_of(unit, DEAD_312)


def check_dead_313(unit):
    return imports_of(unit, DEAD_313)


def _deprecated_decorator(node):
    """Is ``node`` a decorator expression naming PEP 702's ``deprecated``?

    Both spellings, because both are correct: ``@warnings.deprecated("...")``
    and ``@deprecated("...")`` after ``from warnings import deprecated``. The
    decorator is normally *called* (PEP 702 requires the message), but the bare
    form is accepted here so that this gate reports "no marker at all" and the
    behaviour suite -- which reads ``__deprecated__`` and requires a non-empty
    string -- reports "marker with no message". Two different findings, one
    each, rather than both gates saying the same thing about half the tree.
    """
    if isinstance(node, ast.Call):
        node = node.func
    if isinstance(node, ast.Attribute):
        return node.attr == "deprecated"
    return isinstance(node, ast.Name) and node.id == "deprecated"


#: Statement attributes that hold a nested list of statements. ``handlers`` is
#: in the list because ``ast.excepthandler`` carries a ``body`` of its own and
#: the recursion below reaches it through the same branch as everything else.
_STMT_BODIES = ("body", "handlers", "orelse", "finalbody", "cases")


def _module_level(body):
    """Every statement in ``body`` the import of the module actually runs.

    The same recursion as :func:`_module_level_defs`, for the same reason -- a
    block under a ``try:`` is still a block that executes on import -- but it
    yields the statements themselves, because its caller
    (:func:`check_type_checking_imports`) is looking for an ``if`` rather than
    a ``def``. Recursion stops at a class or a function for the same reason
    too: what is inside those runs later, under a scope of its own, and in the
    one case on this tree that matters it is a declaration rather than an
    import.
    """
    for node in body:
        yield node
        if isinstance(node, (ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        for field in _STMT_BODIES:
            sub = getattr(node, field, None)
            if isinstance(sub, list):
                for found in _module_level(sub):
                    yield found


def _module_level_defs(body):
    """Every ``def`` in ``body`` that binds a *module-level* name.

    Module scope is wider than ``tree.body``. A ``def`` inside a top-level
    ``if``, ``try`` or ``with`` still binds a module global, and this tree is
    built out of ``try: import X`` / ``except ImportError:`` blocks, so a walk
    restricted to ``tree.body`` left a one-line dodge: indent the definition
    under ``try:`` and the gate stops seeing it, which makes the gate pass
    without the decorator. Rung 04's static review found it.

    Recursion stops at a class or a function, and that narrowing is the point:
    a name bound there is a different function with a different contract (see
    :data:`DEPRECATED_SURFACE`'s note on ``MockRequest.get_host``), so a plain
    ``ast.walk`` would reach a method that must *not* be decorated and would
    make the only correct answer unpassable.
    """
    for node in body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            yield node
            continue
        if isinstance(node, ast.ClassDef):
            continue
        for field in _STMT_BODIES:
            sub = getattr(node, field, None)
            if isinstance(sub, list):
                for found in _module_level_defs(sub):
                    yield found


def check_deprecated_surface_marked(unit):
    """A module-level definition in :data:`DEPRECATED_SURFACE` with no marker.

    Scoped by :func:`_module_level_defs` rather than by ``ast.walk``, so a
    method of the same name stays out of scope while a definition parked under
    a ``try`` or an ``if`` stays in it.
    """
    for node in _module_level_defs(unit.tree.body):
        if node.name not in DEPRECATED_SURFACE:
            continue
        if not any(_deprecated_decorator(d) for d in node.decorator_list):
            yield node.lineno, "def %s(...) carries no @warnings.deprecated" % (
                node.name,)


def _deprecation_warning_aliases(unit):
    """Local names that mean ``DeprecationWarning``.

    ``DW = DeprecationWarning`` at module scope, then
    ``warnings.warn(msg, DW)``, is the same hand-rolled announcement this gate
    is about, and a check matching only the literal name let it through --
    rung 04's static review found it. Two binding forms are resolved: an
    assignment from the bare or qualified name, and
    ``from builtins import DeprecationWarning as DW``.

    A *subclass* is deliberately not collected. The behaviour cases record the
    warning category by name, so a tree that swapped in a subclass to dodge
    this gate changes the recorded answer and fails there -- one finding in
    one place, rather than two gates and a case all saying the same thing.
    """
    names = set()
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.Assign):
            v = node.value
            src = v.attr if isinstance(v, ast.Attribute) else (
                v.id if isinstance(v, ast.Name) else None)
            if src == "DeprecationWarning" or (src is not None
                                               and src in names):
                for t in node.targets:
                    if isinstance(t, ast.Name):
                        names.add(t.id)
        elif isinstance(node, ast.ImportFrom):
            for alias in node.names:
                if alias.name == "DeprecationWarning" and alias.asname:
                    names.add(alias.asname)
    return names


def check_adhoc_deprecation_warn(unit):
    """``warnings.warn(..., DeprecationWarning)`` -- the hand-rolled spelling.

    Exactly ``DeprecationWarning`` and not "anything warning-shaped": the tree's
    other seven ``warnings.warn`` calls raise ``InsecureRequestWarning``,
    ``SecurityWarning`` and ``SNIMissingWarning``, which are this library's
    contract and are scored by behaviour cases that must keep passing.
    ``PendingDeprecationWarning`` is not banned either -- nothing in the tree
    raises one, a gate against it would be a rule with no requirement behind it,
    and a candidate that swapped the category to dodge this gate would fail the
    behaviour cases, which record the category by name.
    """
    aliases = _deprecation_warning_aliases(unit)
    for node in ast.walk(unit.tree):
        if not isinstance(node, ast.Call):
            continue
        fn = node.func
        if isinstance(fn, ast.Attribute):
            if fn.attr != "warn":
                continue
        elif isinstance(fn, ast.Name):
            if fn.id != "warn":
                continue
        else:
            continue
        args = list(node.args) + [kw.value for kw in node.keywords
                                  if kw.arg in (None, "category")]
        for a in args:
            name = a.attr if isinstance(a, ast.Attribute) else (
                a.id if isinstance(a, ast.Name) else None)
            if name == "DeprecationWarning" or name in aliases:
                yield node.lineno, "warnings.warn(..., %s)" % (
                    name if name == "DeprecationWarning"
                    else "%s = DeprecationWarning" % name)
                break


def _module_aliases(unit):
    """Local names this unit binds to a *module* with an ``import`` statement.

    ``from .util import url`` then ``url.get_host(u)`` is the same call as
    ``get_host(u)``, and a check that only looked at bare ``Name`` calls would
    let it through. Receivers that are not module aliases are left alone --
    ``self.get_host()`` in ``requests/cookies.py`` is the cookiejar interface,
    not urllib3's function, and flagging it would fail the correct answer.
    """
    names = set()
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                names.add(alias.asname or alias.name.split(".")[0])
        elif isinstance(node, ast.ImportFrom):
            for alias in node.names:
                # A ``from`` import binds either a module or a function; which
                # one is not decidable here, so both are collected. A false
                # module alias can only matter if something then calls
                # ``<that name>.get_host(...)``, which is the construct the
                # gate is about either way.
                names.add(alias.asname or alias.name)
    return names


def _renamed_surface(unit):
    """Local name -> the :data:`DEPRECATED_SURFACE` name it stands for.

    ``from .util.url import get_host as gh`` and then ``gh(u)`` is exactly the
    call this gate exists to catch, and matching on the callee's own spelling
    missed it; so does ``gh = get_host``. Rung 04's static review found both.

    Only a bare ``Name`` counts on the value side of an assignment.
    ``f = req.get_host`` is the cookiejar interface reached through a
    parameter -- the construct ``CLEAN_EXTRA``'s ``attribute-on-a-parameter``
    fixture pins as silent -- and resolving attribute values here would fire
    on it. The re-export in ``urllib3/__init__.py`` is unaffected either way:
    importing a name is still not calling it.
    """
    names = {}
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.ImportFrom):
            for alias in node.names:
                if alias.name in DEPRECATED_SURFACE and alias.asname:
                    names[alias.asname] = alias.name
        elif isinstance(node, ast.Assign):
            v = node.value
            if not isinstance(v, ast.Name):
                continue
            src = names.get(v.id, v.id if v.id in DEPRECATED_SURFACE else None)
            if src is None:
                continue
            for t in node.targets:
                if isinstance(t, ast.Name):
                    names[t.id] = src
    return {k: v for k, v in names.items() if k not in DEPRECATED_SURFACE}


def check_deprecated_surface_callers(unit):
    """A call from inside the tree to one of its own deprecated functions.

    The measured trap this exists for: marking ``get_host`` without rerouting
    its callers makes urllib3 raise a DeprecationWarning about urllib3 on every
    ``connection_from_url`` and on every ``urlopen(..., assert_same_host=True)``
    -- library code scolding its own users for something they did not write.
    ``parse_url`` is the documented replacement and is not deprecated.

    Bare-name calls and module-qualified calls both count; attribute calls on
    anything that is not a module alias do not (``self.get_host()``).
    """
    aliases = _module_aliases(unit)
    renamed = _renamed_surface(unit)
    for node in ast.walk(unit.tree):
        if not isinstance(node, ast.Call):
            continue
        fn = node.func
        if isinstance(fn, ast.Name):
            if fn.id in DEPRECATED_SURFACE:
                yield node.lineno, "calls %s()" % fn.id
            elif fn.id in renamed:
                yield node.lineno, "calls %s(), a local name for %s" % (
                    fn.id, renamed[fn.id])
        elif isinstance(fn, ast.Attribute):
            if (fn.attr in DEPRECATED_SURFACE
                    and isinstance(fn.value, ast.Name)
                    and fn.value.id in aliases):
                yield node.lineno, "calls %s.%s()" % (fn.value.id, fn.attr)


_WARNINGS_MODULES = ("warnings", "_warnings")


def _names_the_warnings_module(node, aliases):
    """Is ``node`` an expression that names the ``warnings`` module?

    Either a local name bound to it, or an Attribute chain whose last
    component is the module: a dotted ``import pkg.warnings`` binds only
    ``pkg``, so the call reads ``pkg.warnings.warn(...)`` and there is no
    local name to match. Written as a predicate rather than folded into the
    set for the same reason :func:`_names_the_ssl_module` is one rung
    earlier -- the receiver, not the attribute, carries the whole test.
    """
    if isinstance(node, ast.Name):
        return node.id in aliases
    if isinstance(node, ast.Attribute):
        if node.attr not in _WARNINGS_MODULES:
            return False
        # `self.warnings.warn(...)` is an *instance attribute* that happens to
        # be spelled like the module, and the round-2 static review found it
        # silent here. A module reached through a dotted import has a package
        # at the root of the chain (`pkg.warnings`); an attribute of the
        # receiver does not name a module, so the chain is walked to its root
        # and `self` / `cls` disqualify it. A tree that really did park the
        # warnings module on an instance and call `self.warnings.warn(...)`
        # would be reported -- that spelling appears in none of the 2606
        # external files swept for this gate, and the direction of the error
        # is the one that leaves an evasion visible rather than a correct
        # call unpassable.
        root = node.value
        while isinstance(root, ast.Attribute):
            root = root.value
        if isinstance(root, ast.Name) and root.id in ("self", "cls"):
            return False
        return True
    return False


def _warnings_module_aliases(unit):
    """Local names bound to the ``warnings`` module.

    ``warnings.warn`` is not deprecated and this tree makes eleven correct
    calls to it, so the gate below has to tell it apart from ``Logger.warn``.
    An aliased import binds a different local name, which is why this is a
    set rather than the literal string -- the same reason :func:`_ssl_aliases`
    exists one rung earlier.

    Three further spellings are collected because each of them *is* the real
    module, and firing on one would fail a correct tree. ``_warnings`` is the
    C accelerator that ``warnings.warn`` is itself defined from. A dotted
    ``import pkg.warnings`` binds only its root, so nothing local names the
    module and :func:`_names_the_warnings_module` reads the receiver instead
    -- collecting the root here, as this did before rung 04's static review,
    would have let ``pkg.warn(...)`` through as well. And ``w = warnings``
    rebinds it to a plain local, which is the module-side twin of the
    category rebind :func:`_deprecation_warning_aliases` already resolves;
    the rebind is read through the same predicate, so binding a *logger* to a
    local (``w = self.log``) collects nothing and stays scored.
    """
    # Seeded with the module's own names rather than started empty, for the
    # reason :func:`_ssl_aliases` is: a name can be bound without an ``Import``
    # node above it. Swept over the 3.13 standard library, the two spellings
    # that reach a ``.warn`` call this way are ``importlib/_bootstrap.py``,
    # where the interpreter injects ``_warnings`` over a module-level
    # ``_warnings = None``, and a module that picks ``warnings`` up from a
    # star-import. Both are correct calls to the real function. Nothing is
    # lost by seeding: a *logger* named ``warnings`` is not a construct this
    # gate has to find, and ``self.log.warn`` is unaffected either way.
    names = set(_WARNINGS_MODULES)
    for node in ast.walk(unit.tree):
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            for alias in node.names:
                # Only the last component can be the module, and without an
                # ``asname`` a dotted import binds its root rather than the
                # module -- the ``_ssl_aliases`` rule one rung earlier.
                if alias.name.rsplit(".", 1)[-1] in _WARNINGS_MODULES:
                    if alias.asname:
                        names.add(alias.asname)
                    elif "." not in alias.name:
                        names.add(alias.name)
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.Assign) and _names_the_warnings_module(
                node.value, names):
            for t in node.targets:
                if isinstance(t, ast.Name):
                    names.add(t.id)
    # And the same pass in the other direction, which the round-2 static
    # review asked for: a name *rebound away* from the module is no longer the
    # module, so `warnings = self.log` followed by `warnings.warn(...)` is the
    # deprecated alias wearing the module's name. Only an Attribute or a Call
    # value discards -- an object fetched at run time -- because the seeded
    # names exist for the two spellings that bind no `Import` node, and one of
    # them is `importlib/_bootstrap.py`'s module-level `_warnings = None`
    # which the interpreter later injects over. Discarding on a Constant would
    # make that correct call fire. The limit, stated: a tree that binds the
    # real module through a call (`warnings = importlib.import_module(
    # "warnings")`) and then calls `.warn` on it is reported.
    for node in ast.walk(unit.tree):
        if not isinstance(node, ast.Assign):
            continue
        if not isinstance(node.value, (ast.Attribute, ast.Call)):
            continue
        if _names_the_warnings_module(node.value, names):
            continue
        for t in node.targets:
            if isinstance(t, ast.Name):
                names.discard(t.id)
    return names


def check_logger_warn_alias(unit):
    """``.warn(...)`` on anything that is not the ``warnings`` module.

    ``Logger.warn`` has been a deprecated alias of ``Logger.warning`` since
    3.3. It still forwards on 3.13, so nothing the caller gets back changes,
    but it raises a ``DeprecationWarning`` from inside this tree every time
    the branch is taken -- which is instruction group 4 of this rung, and the
    one group that had no static gate until rung 04's static review asked why.

    Measured on the predecessor, which is the tree it has to fire on: twelve
    ``warn`` and nine ``warning`` call sites, of which exactly one matches --
    ``self.log.warn`` in ``lib/analytics/client.py``. The eleven
    ``warnings.warn`` calls are skipped by receiver, not by name, and the
    ``.warning`` calls never reach the test. On the finished tree the same
    scan reads nine and ten and matches nothing: the rename moved one call
    across, and two of the ``warnings.warn`` calls were the hand-rolled
    announcements PEP 702 replaced. ``logging.warn(...)``, the module-level
    alias, is caught by the same rule and is equally deprecated.

    The limit, stated rather than hidden: the receiver test can only rule out
    the ``warnings`` module, so any other ``.warn(...)`` matches -- including
    APIs that are not logging at all. Swept over 2748 parsed files of the 3.13
    standard library and three tool virtualenvs this fires 167 times, on
    ``self.warn`` in coverage (73) and ``log.warn`` in various packages (77),
    ``Command.warn`` in distutils and ``js.console.warn`` in an emscripten
    backend. Telling those from ``Logger.warn`` needs types, which a per-file
    AST pass does not have. It is the right trade here because the gate runs
    over this one tree -- 285 files of a Sublime plugin and its vendored
    libraries -- and across all of them exactly one ``.warn`` receiver is not
    the ``warnings`` module, the logger this rung is about.

    Two receiver spellings the round-2 static review found silent are closed
    above: ``self.warnings.warn(...)``, an instance attribute wearing the
    module's name, and a local rebound away from the module
    (``warnings = self.log``). Both are in the fixtures. The same 2748-file
    sweep re-run after the narrowing reports **167** findings -- the identical
    number, broken down identically -- so neither narrowing bought its
    coverage with a single new over-fire.
    """
    ok = _warnings_module_aliases(unit)
    for node in ast.walk(unit.tree):
        if not isinstance(node, ast.Call):
            continue
        fn = node.func
        if not isinstance(fn, ast.Attribute) or fn.attr != "warn":
            continue
        if _names_the_warnings_module(fn.value, ok):
            continue
        recv = fn.value.id if isinstance(fn.value, ast.Name) else (
            "%s.%s" % (getattr(fn.value.value, "id", "..."), fn.value.attr)
            if isinstance(fn.value, ast.Attribute) else "<expr>")
        yield node.lineno, "%s.warn() -- a deprecated alias of .warning()" % recv


def _ssl_aliases(tree):
    """Every local name this unit binds to the ``ssl`` module.

    ``import ssl as s`` makes ``s.wrap_socket(...)`` exactly as dead as
    ``ssl.wrap_socket(...)``, and the narrowing below -- which exists so that
    the *replacement* ``context.wrap_socket`` stays silent -- would otherwise
    let the aliased spelling through. A live aliased call dies on 3.12 anyway
    and takes the TLS behaviour cases with it, but one left in a branch no
    fixture reaches would have been invisible. Rung 03's static review raised
    it; the fixtures below pin both directions.
    """
    names = {"ssl"}
    for node in ast.walk(tree):
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            for alias in node.names:
                # ``import backports.ssl as _ssl`` binds the same local name as
                # ``from backports import ssl as _ssl`` and was the one spelling
                # the round-2 review found still silent: the alias's ``name`` is
                # the dotted path, so the equality test missed it. Only the last
                # component can be the module, and only an ``asname`` binds a
                # local Name -- without one the call reads ``backports.ssl.
                # wrap_socket(...)``, which `_names_the_ssl_module` already
                # catches on the Attribute receiver.
                if alias.name.rsplit(".", 1)[-1] == "ssl" and alias.asname:
                    names.add(alias.asname)
    return names


def _names_the_ssl_module(node, aliases=("ssl",)):
    """Is ``node`` the expression ``ssl`` (or an alias of it, or ``<pkg>.ssl``)?

    ``wrap_socket`` is the one dead name that is also a *live* method on a
    live object: ``SSLContext.wrap_socket`` is the replacement for the removed
    ``ssl.wrap_socket`` function, so the correct answer to this rung contains
    ``context.wrap_socket(sock, server_hostname=...)`` and there is no other
    way to wrap a socket. A gate that fired on every ``.wrap_socket`` would
    fail the only tree that can pass it -- which is the shape INSTRUCTIONS
    trap 6 is about, and is why ``CLEAN_EXTRA`` below carries a context-method
    fixture that must stay silent.
    """
    if isinstance(node, ast.Name):
        return node.id in aliases
    if isinstance(node, ast.Attribute):
        # Attribute receivers stay at the literal spelling: an alias binds a
        # local *name*, so ``foo.s`` is not ``ssl`` however this unit imported
        # it, and widening here would invent findings.
        return node.attr == "ssl"
    return False


def check_ssl_dead_names(unit):
    aliases = _ssl_aliases(unit.tree)
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.Attribute) and node.attr in SSL_DEAD_NAMES:
            # Every other name in the set is dead on *any* receiver: nothing
            # live is called `.match_hostname`, `.CertificateError` or
            # `.PROTOCOL_SSLv23`, so re-vendoring one under a package name
            # still trips this. Only `wrap_socket` is narrowed.
            if (node.attr != "wrap_socket"
                    or _names_the_ssl_module(node.value, aliases)):
                yield node.lineno, ".%s" % node.attr
        if isinstance(node, ast.ImportFrom) and node.module == "ssl":
            for alias in node.names:
                if alias.name in SSL_DEAD_NAMES:
                    yield node.lineno, "from ssl import %s" % alias.name
        if isinstance(node, ast.Name) and node.id in SSL_DEAD_NAMES:
            yield node.lineno, node.id


#: What the vendored ``ssl_match_hostname`` backport defines. A tree that moves
#: the directory instead of deleting it satisfies a path check and still ships
#: the backport; a tree that renames the package keeps both these definitions.
REVENDORED_MATCHER = frozenset(("match_hostname", "CertificateError"))


def check_ssl_revendored(unit):
    """A *definition* of one of the backport's names, anywhere in the tree.

    ``check_ssl_dead_names`` sees references -- imports, calls, attributes,
    bare loads. It does not see ``def match_hostname(cert, hostname):``, which
    is what a relocated or renamed copy of the backport consists of, and which
    is silent under the path glob the moment the directory is not called
    ``packages/ssl_match_hostname``.
    """
    for node in ast.walk(unit.tree):
        if (isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                              ast.ClassDef))
                and node.name in REVENDORED_MATCHER):
            yield node.lineno, "def/class %s" % node.name
        # A ``def`` is not the only way to define a name. ``match_hostname =
        # lambda cert, host: ...`` and ``CertificateError = _impl.
        # CertificateError`` revendor the backport without a single
        # ``FunctionDef``, which the round-2 review named as the remaining
        # silent spelling. Any binding of one of the two names is one too many:
        # a correct 3.12 port has no ``match_hostname`` anywhere, because
        # ``SSLContext.check_hostname`` does the work inside the handshake.
        elif isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = (node.targets if isinstance(node, ast.Assign)
                       else [node.target])
            for t in targets:
                for leaf in ast.walk(t):
                    if (isinstance(leaf, ast.Name)
                            and isinstance(leaf.ctx, ast.Store)
                            and leaf.id in REVENDORED_MATCHER):
                        yield node.lineno, "assign %s" % leaf.id


def check_naive_utc(unit):
    for node in ast.walk(unit.tree):
        if isinstance(node, ast.Attribute) and node.attr in NAIVE_UTC:
            yield node.lineno, ".%s()" % node.attr


def check_finally_control_flow(unit):
    """PEP 765: ``return``/``break``/``continue`` leaving a ``finally``.

    Nested function and class bodies are not descended into: a ``return`` there
    belongs to the nested callable, not to the ``finally``.
    """
    for node in ast.walk(unit.tree):
        if not (isinstance(node, ast.Try) and node.finalbody):
            continue
        stack = list(node.finalbody)
        while stack:
            sub = stack.pop()
            if isinstance(sub, (ast.FunctionDef, ast.AsyncFunctionDef,
                                ast.ClassDef, ast.Lambda)):
                continue
            if isinstance(sub, (ast.Return, ast.Break, ast.Continue)):
                yield sub.lineno, type(sub).__name__.lower() + " in finally"
            for child in ast.iter_child_nodes(sub):
                stack.append(child)


def check_string_annotations(unit):
    """Quoted annotations: PEP 649 makes them unnecessary at 3.14.

    Only strings in type position. ``Literal["json"]`` is a value and the
    metadata after an ``Annotated``'s first argument is documentation; neither
    names a type, so neither is a quoted annotation. :func:`_ann_walk` already
    draws that line for ``annotation-names-bound`` and drawing it once means
    the two cannot disagree about what an annotation is.
    """
    carve = _carve_aliases(unit.tree)

    def scan(ann, where):
        if ann is None:
            return
        quotes = []
        for _ in _ann_walk(ann, quotes=quotes, carve=carve):
            pass
        for line, text in quotes:
            yield line, "%s: %r" % (where, text[:40])

    for node in ast.walk(unit.tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            args = node.args
            every = list(args.args) + list(args.kwonlyargs)
            every += [a for a in (args.vararg, args.kwarg) if a]
            every += list(getattr(args, "posonlyargs", []))
            for a in every:
                for hit in scan(a.annotation, "%s(%s)" % (node.name, a.arg)):
                    yield hit
            for hit in scan(node.returns, "%s()->" % node.name):
                yield hit
        elif isinstance(node, ast.AnnAssign):
            for hit in scan(node.annotation, "assignment"):
                yield hit


def _annotation_nodes(node):
    """Every annotation on one ``def`` or annotated assignment."""
    if isinstance(node, ast.AnnAssign):
        return [node.annotation]
    args = node.args
    out = [a.annotation for a in
           list(getattr(args, "posonlyargs", [])) + list(args.args)
           + list(args.kwonlyargs)]
    out += [a.annotation for a in (args.vararg, args.kwarg) if a]
    out.append(node.returns)
    return [a for a in out if a is not None]


def _carve_aliases(tree):
    """Local names that mean ``Literal`` or ``Annotated``, however spelled.

    :func:`_any_aliases`'s argument from the other side. The carve-out below is
    keyed on a name, and ``from typing import Literal as L`` renames it: with
    nothing here, ``x: L["json"]`` is read as a quoted type and a tree that did
    nothing wrong loses the guard's 0.05. ``typing.Literal[...]`` needs no
    entry -- :func:`_ann_head` returns an attribute's ``attr`` -- but
    ``L = typing.Literal`` does, so a plain rebinding is followed as well.

    Only a bare name or attribute rebinds the form. ``Coord = Literal["a"]``
    names a *type* built with ``Literal`` and is not another spelling of the
    form itself, so it is not followed: treating it as one would carve out the
    contents of ``Coord[...]``, and the point of the carve-out is that it is
    narrow.
    """
    out = {"Literal": "Literal", "Annotated": "Annotated"}
    binders = []
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom):
            for a in node.names:
                if a.name in ("Literal", "Annotated"):
                    out[a.asname or a.name] = a.name
        elif isinstance(node, ast.Assign) and len(node.targets) == 1:
            binders.append((node.targets[0], node.value))
        elif node.__class__.__name__ == "TypeAlias":           # PEP 695, 3.12+
            binders.append((node.name, node.value))
    for _ in range(len(binders) + 1):
        grew = False
        for target, value in binders:
            if not isinstance(target, ast.Name) or target.id in out:
                continue
            if not isinstance(value, (ast.Name, ast.Attribute)):
                continue
            head = _ann_head(value)
            if head in out:
                out[target.id] = out[head]
                grew = True
        if not grew:
            break
    return out


def _ann_walk(ann, line=None, quotes=None, carve=None):
    """Walk an annotation, descending into quoted ones.

    A string inside an annotation is an annotation: ``def f(x: "Any")`` says
    exactly what ``def f(x: Any)`` says. Reported at the quote's own line,
    since the re-parsed tree's line numbers are relative to the string.

    Two subscripts are not type references and are not followed into: the
    contents of a ``Literal`` are values, and everything after the first
    element of an ``Annotated`` is metadata. ``Literal["Any"]`` is the string
    ``"Any"``, not the type, and a walk that cannot tell them apart reports a
    correct annotation as a violation.

    ``quotes`` collects ``(line, text)`` for every string the walk decides *is*
    a type, and is how ``no-quoted-annotations`` gets the same carve-out rather
    than a second, coarser one of its own. The walk still descends through the
    string, so a name quoted inside a quote is read once and reported once.

    ``carve`` is :func:`_carve_aliases`' map for the file the annotation came
    from, so the two subscripts are recognised under whatever name that file
    imported them as. Without it the carve-out reads the spelling rather than
    the form.
    """
    line = getattr(ann, "lineno", 0) if line is None else line
    if isinstance(ann, ast.Constant) and isinstance(ann.value, str):
        if quotes is not None:
            quotes.append((ann.lineno, ann.value))
        try:
            inner = ast.parse(ann.value, mode="eval").body
        except SyntaxError:
            return
        for pair in _ann_walk(inner, ann.lineno, quotes, carve):
            yield pair
        return
    if isinstance(ann, ast.Subscript):
        head = _ann_head(ann)
        head = (carve or {}).get(head, head)
        if head in ("Literal", "Annotated"):
            for pair in _ann_walk(ann.value, line, quotes, carve):
                yield pair
            if head == "Annotated":
                first = ann.slice
                if isinstance(first, ast.Tuple) and first.elts:
                    for pair in _ann_walk(first.elts[0], line, quotes, carve):
                        yield pair
            return
    yield ann, line
    for child in ast.iter_child_nodes(ann):
        for pair in _ann_walk(child, line, quotes, carve):
            yield pair


def _live_children(node):
    """``ast.iter_child_nodes``, minus the branches the interpreter skips.

    An ``if TYPE_CHECKING:`` body does not run, so a name bound there is not
    bound; its ``else:`` is the branch that does run, so a name bound *there*
    is. ``if False:`` and ``if 0:`` are the same statement written to evade a
    gate that only knows the one spelling, and are pruned for the same reason.
    """
    if isinstance(node, ast.If) and (_is_type_checking(node.test)
                                     or (isinstance(node.test, ast.Constant)
                                         and not node.test.value)):
        return list(node.orelse)
    return list(ast.iter_child_nodes(node))


_SCOPES = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef, ast.Lambda,
           ast.ListComp, ast.SetComp, ast.DictComp, ast.GeneratorExp)


def _scope_walk(scope):
    """Every node reached while running one scope's own body, and no other's.

    A nested ``def``, ``class``, ``lambda`` or comprehension is yielded -- it is
    a statement in this scope, and binds its name here -- but not descended
    into, because what happens inside it happens somewhere else. The dead
    branches :func:`_live_children` prunes are not reached.

    Two assignment targets are stepped over rather than walked, so that the
    ``Name`` node a caller would read as a binding never arrives: the target of
    a bare ``x: T``, which records a type without binding anything, and the
    target of a stub assignment (:func:`_is_stub_value`), which binds a name
    that means nothing. Everything else about those statements -- the value,
    the annotation -- is still walked.
    """
    stack = [scope.body] if isinstance(scope, ast.Lambda) else list(scope.body)
    while stack:
        node = stack.pop()
        yield node
        if isinstance(node, _SCOPES):
            continue
        if isinstance(node, ast.AnnAssign):
            stack.append(node.annotation)
            if node.value is not None and not _is_stub_value(node.value):
                stack.append(node.target)
            if node.value is not None:
                stack.append(node.value)
            continue
        if isinstance(node, ast.Assign) and _is_stub_value(node.value):
            stack.append(node.value)
            continue
        stack += _live_children(node)


def _is_stub_value(value):
    """Is this assignment a name with nothing behind it?

    ``Elasticsearch = None`` and ``Elasticsearch = object`` bind the name, so a
    gate that asks only "is it bound" is satisfied by either, and a candidate
    who deletes an import and writes one of these has a tree whose annotations
    resolve to something that is not the type they name. The two-line
    ``if TYPE_CHECKING: from x import Y`` / ``else: Y = None`` idiom is exactly
    this, and is what this rung is removing rather than a way to comply with it.
    """
    if isinstance(value, ast.Constant):                  # None, ..., 0, "", True
        return True
    if isinstance(value, ast.Call):
        return _is_stub_value(value.func)
    return _ann_head(value) in ("object", "Any")


def _scope_bound(scope, stars=None):
    """The names one scope binds by running its own body.

    Lexical, not tree-wide: a ``Store`` inside a function the annotation does
    not live in is that function's local, and crediting it lets a tree buy the
    whole gate with ``def _hold(): from elasticsearch import Elasticsearch``
    while the annotations that name ``Elasticsearch`` are still unresolvable
    (review-07 round 1, item A). :func:`_bound_in_scope` composes these sets
    outward-in, which is the direction Python actually resolves them.

    A ``global`` declaration is not credited: it says where a name lives
    rather than putting one there. ``del`` is the other direction -- it takes
    a binding away, so ``from .client import Elasticsearch`` followed by ``del
    Elasticsearch`` leaves the name as absent as never importing it. The last
    line wins, which is a line-number comparison rather than real flow
    analysis: a name deleted after its last binding is not bound.

    A live ``from x import *`` binds nothing here; it is appended to ``stars``
    instead, for :func:`_star_exports` to resolve against the tree.
    """
    out = set(dir(builtins)) if isinstance(scope, ast.Module) else set()
    out.update(p.name for p in getattr(scope, "type_params", ()) or ())
    if isinstance(scope, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)):
        args = scope.args
        out.update(a.arg for a in
                   list(getattr(args, "posonlyargs", [])) + list(args.args)
                   + list(args.kwonlyargs)
                   + [a for a in (args.vararg, args.kwarg) if a])
    at, gone = {}, {}

    def bind(name, line):
        out.add(name)
        at[name] = max(at.get(name, 0), line)

    for node in _scope_walk(scope):
        if isinstance(node, ast.Import):
            for alias in node.names:
                bind(alias.asname or alias.name.split(".")[0], node.lineno)
        elif isinstance(node, ast.ImportFrom):
            for alias in node.names:
                if alias.name == "*":
                    if stars is not None:
                        stars.append(node)
                    continue
                bind(alias.asname or alias.name, node.lineno)
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                               ast.ClassDef)):
            bind(node.name, node.lineno)
        elif node.__class__.__name__ == "TypeAlias":              # PEP 695
            if isinstance(node.name, ast.Name):
                bind(node.name.id, node.lineno)
        elif isinstance(node, ast.Name):
            if isinstance(node.ctx, ast.Store):
                bind(node.id, node.lineno)
            elif isinstance(node.ctx, ast.Del):
                gone[node.id] = max(gone.get(node.id, 0), node.lineno)
    return out - set(n for n, line in gone.items() if at.get(n, -1) < line)


def _star_exports(unit, node):
    """What a live ``from ... import *`` binds, read rather than guessed.

    The gate resolves the import against the file's own position in the tree,
    parses what it finds, and takes that module's ``__all__`` if it declares
    one and its public run-time names if it does not. Four files import this
    way -- ``ElasticsearchClient.py`` from ``.commands``,
    ``elasticsearch/__init__.py`` from ``.exceptions``, and the two generated
    Thrift modules from ``ttypes`` in the py2 implicit-relative spelling -- and
    all four resolve, which is why the finished tree reports nothing here.

    A star import the gate cannot resolve binds nothing, and a name that comes
    only from one is reported. The other answer -- the module is unreadable,
    so say nothing about it -- would make this the one gate on the ladder a
    candidate can switch off by adding a line to the file it was about to fire
    on, which is the failure it was written to close in the first place.
    """
    if not unit.path.endswith(unit.rel):
        return ()
    root = unit.path[:-len(unit.rel)]
    here = [p for p in os.path.dirname(unit.rel).replace(os.sep, "/").split("/")
            if p]
    if node.level:
        bases = [here[:len(here) - node.level + 1]]
    else:
        bases = [here, []]          # implicit relative first, then the root
    parts = (node.module or "").split(".") if node.module else []
    for base in bases:
        stem = os.path.join(root, *(base + parts)) if base + parts else root
        for cand in (stem + ".py", os.path.join(stem, "__init__.py")):
            if not os.path.isfile(cand):
                continue
            try:
                sub = ast.parse(open(cand, "rb").read())
            except (SyntaxError, ValueError):
                return ()
            for stmt in sub.body:
                if (isinstance(stmt, ast.Assign)
                        and any(isinstance(t, ast.Name) and t.id == "__all__"
                                for t in stmt.targets)
                        and isinstance(stmt.value, (ast.List, ast.Tuple))):
                    named = set(e.value for e in stmt.value.elts
                                if isinstance(e, ast.Constant)
                                and isinstance(e.value, str))
                    if named:
                        return named
            return set(n for n in _scope_bound(sub) - set(dir(builtins))
                       if not n.startswith("_"))
    return ()


def check_annotation_names_bound(unit):
    """The other half of ``no-type-checking-imports``, and the half a tree
    that wants the gate quiet for nothing will reach for first.

    ``no-type-checking-imports`` fires on an import inside a module-level ``if
    TYPE_CHECKING:`` block, so there are two ways to silence it: hoist the
    import, which is the work, or delete it, which is not. Deleting it leaves
    the annotation that used the name behind, and under PEP 649 that is a
    ``NameError`` waiting for the first reader -- strictly worse than the block
    it replaced, and invisible to every other gate on this rung.

    Measured, that route is worth catching: the rung-07 golden with the
    plugin's 33 block imports deleted rather than hoisted passes all three of
    the other new gates, every one of the 541 inherited cases and all three
    owned ones, and scores 0.997619 without this gate. The client core's two
    annotation cases read ``__annotations__`` and ``get_type_hints`` over the
    sixteen public ``elasticsearch.*`` modules and would have caught it there;
    nothing reaches the plugin's eighteen blocks, and this is what does.
    NOTES section 14.6.

    A name is "bound" if something the interpreter runs in a scope the
    annotation can see binds it -- see :func:`_scope_bound` for what counts as
    binding, :func:`_bound_in_scope` for what counts as seeing it, and
    :func:`_star_exports` for the one import whose bindings are not in the file
    that makes them.
    """
    return _bound_in_scope(unit, unit.tree, set())


def _bound_in_scope(unit, scope, visible):
    """Report the unbound names in one scope's annotations, then recurse.

    ``visible`` is what the scopes around this one bind; adding what this one
    binds gives what an annotation written here can name. Bindings travel
    inward only, which is the direction Python resolves them.

    A class body's names are passed inward to its methods. That is not a
    relaxation: PEP 649 builds a method's ``__annotate__`` where the ``def``
    is, so a method annotated with a class-level name resolves on 3.14.7 --
    measured, ``class C: X = int`` with ``def m(self, a: X)`` gives
    ``{'a': <class 'int'>}``. It is passed further inward than the language
    would, to a function nested inside such a method; that direction only ever
    makes the gate quieter, and the defect it is here for -- a name bound in no
    scope at all -- is caught either way.

    A ``def``'s own annotations are checked in the scope that holds the ``def``,
    because that is where they are evaluated, plus its PEP 695 type parameters,
    which are in scope for its signature.
    """
    stars = []
    here = visible | _scope_bound(scope, stars)
    for node in stars:
        here |= set(_star_exports(unit, node))
    carve = _carve_aliases(unit.tree)
    out = []
    for node in _scope_walk(scope):
        if isinstance(node, ast.AnnAssign):
            out += _unbound(node.annotation, "assignment", here, carve)
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            seen = here | set(p.name for p in
                              getattr(node, "type_params", ()) or ())
            for ann in _annotation_nodes(node):
                out += _unbound(ann, node.name, seen, carve)
            out += _bound_in_scope(unit, node, here)
        elif isinstance(node, (ast.ClassDef, ast.Lambda)):
            out += _bound_in_scope(unit, node, here)
    return out


def _unbound(ann, where, bound, carve=None):
    """The names one annotation loads that ``bound`` does not hold."""
    return [(line, "%s: `%s` is bound by nothing that runs" % (where, found.id))
            for found, line in _ann_walk(ann, carve=carve)
            if isinstance(found, ast.Name)
            and isinstance(found.ctx, ast.Load)
            and found.id not in bound]


def _any_aliases(tree):
    """Local names that mean ``Any``, however indirectly.

    ``Any`` is a name like any other, so a tree can rename it -- ``from typing
    import Any as J``, ``J = Any``, ``J: TypeAlias = Any``, ``type J = Any`` --
    and a gate that matches the literal spelling then reports nothing about a
    file annotated entirely in ``J``. An alias that merely *contains* ``Any``
    counts too: ``Json = dict[str, Any]`` puts the escape hatch one subscript
    further down, not anywhere else.

    Iterated to a fixed point, because an alias of an alias is an alias.
    """
    names = {"Any"}
    binders = []
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom):
            for a in node.names:
                if a.name == "Any":
                    names.add(a.asname or "Any")
        elif isinstance(node, ast.Assign):
            binders.append(([t for t in node.targets], node.value))
        elif isinstance(node, ast.AnnAssign) and node.value is not None:
            binders.append(([node.target], node.value))
        elif node.__class__.__name__ == "TypeAlias":          # PEP 695, 3.12+
            binders.append(([node.name], node.value))
    for _ in range(len(binders) + 1):
        grew = False
        for targets, value in binders:
            mentions = any(isinstance(sub, ast.Name) and sub.id in names
                           or isinstance(sub, ast.Attribute)
                           and sub.attr == "Any"
                           for sub in ast.walk(value))
            if not mentions:
                continue
            for t in targets:
                if isinstance(t, ast.Name) and t.id not in names:
                    names.add(t.id)
                    grew = True
        if not grew:
            break
    return names


def check_any_annotations(unit):
    """``typing.Any`` in an annotation -- D-5 scopes this to the repo's own
    surface, so the gate that uses it carries the scope.

    Matched through renames and through quotes: see :func:`_any_aliases` for
    why the literal spelling is not enough.
    """
    aliases = _any_aliases(unit.tree)
    carve = _carve_aliases(unit.tree)
    for node in ast.walk(unit.tree):
        for ann in _any_type_positions(node):
            for sub, lineno in _ann_walk(ann, carve=carve):
                if isinstance(sub, ast.Name) and sub.id in aliases:
                    yield lineno, ("Any" if sub.id == "Any"
                                   else "%s, which is Any renamed" % sub.id)
                if isinstance(sub, ast.Attribute) and sub.attr == "Any":
                    yield lineno, "typing.Any"


def _any_type_positions(node):
    """The type expressions on ``node`` that ``no-any-in-annotations`` reads.

    An annotation, and two places that switch the checker off for a value
    exactly as an annotation of ``Any`` does but are not annotations (wave-D
    S-C2): the target of a ``typing.cast(...)``, and a type variable's bound or
    constraints, old-style ``TypeVar(...)`` or PEP 695 ``[T: ...]``.
    """
    if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                         ast.AnnAssign)):
        for ann in _annotation_nodes(node):
            yield ann
    if isinstance(node, ast.Call):
        fn = node.func
        name = fn.id if isinstance(fn, ast.Name) else \
            fn.attr if isinstance(fn, ast.Attribute) else ""
        if name == "cast" and node.args:
            yield node.args[0]
        elif name == "TypeVar":
            for arg in node.args[1:]:
                yield arg
            for kw in node.keywords:
                if kw.arg == "bound":
                    yield kw.value
    for param in getattr(node, "type_params", None) or ():
        bound = getattr(param, "bound", None)
        if bound is not None:
            yield bound


#: Dunders that count as public surface even though they start with ``_``.
#: The tree calls them; a caller that writes ``with client:`` or ``for x in
#: pool:`` is using a declared interface and cannot see the underscore.
PUBLIC_DUNDERS = ("__init__", "__enter__", "__exit__", "__iter__",
                  "__next__", "__call__")


def is_public_def(name):
    """Whether ``name`` is on the public surface this rung talks about."""
    return not name.startswith("_") or name in PUBLIC_DUNDERS


def iter_public_defs(tree):
    """``(qualname, node)`` for every public ``def`` at module or class scope.

    One walk, used by :func:`check_unannotated_public_defs` to ask whether a
    def is annotated and by :func:`check_public_surface_census` to ask whether
    it is still there, so the two cannot come to disagree about what "the
    public surface" is. Nested defs are excluded from both, for the reason the
    instruction gives: a closure is not surface anyone outside can call.
    """
    tops = [(n, "") for n in tree.body]
    for node in list(tree.body):
        if isinstance(node, ast.ClassDef):
            tops += [(n, node.name + ".") for n in node.body]
    for node, prefix in tops:
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        if is_public_def(node.name):
            yield prefix + node.name, node


def check_public_surface_census(tree):
    """Every public def the agent was handed is still there, under its name.

    The rung's other checks all read the defs that are *present*: annotated,
    spelled without `Any`, agreeing with their bodies, checking clean. None of
    them notices a def that is gone. That asymmetry is a scoring vector and
    not a hypothetical one -- the cheapest way to make
    ``public-surface-annotated`` stop reporting a hard signature is to delete
    it, and the second cheapest is to rename it with a leading underscore,
    which moves it off the public surface every one of those checks reads.
    The manifest rail blocks deleting the *file*; nothing blocked emptying it.

    So the surface is pinned by name. ``harness.surface.PUBLIC_DEFS`` is the
    382 ``<rel>::<qualname>`` pairs of the pinned tree, generated from
    ``environment/repo`` by ``build/mksurface.py`` and re-verified against it
    on every ``build/sync.sh`` run, so it cannot drift from the tree it claims
    to describe. Rungs 01-04 neither add nor remove one, measured: the pinned
    tree and the rung-04 golden have the same 382 names, which is why a census
    recorded from the pinned tree is the right thing to hold a rung-05 agent
    to.

    Adding a def is not a finding. This says what must survive, not what must
    not appear -- a migration that needs a new helper is still a migration,
    and a rung that forbade one would be telling the agent how to write code.
    """
    from . import surface
    want = surface.PUBLIC_DEFS
    have = set()
    # `units` is the same parse every other check reads, and it hands back the
    # files that will not parse separately rather than dropping them. A file
    # that does not parse has not *deleted* its defs -- it has hidden them --
    # so it is reported once, as what it is, rather than once per def it was
    # holding. "The gate found nothing" and "the gate could not look" must not
    # produce the same number (trap 3), and neither should they produce the
    # same *kind* of number.
    cache, broken = units(tree, OWN_SURFACE)
    blind = set(rel.replace(os.sep, "/") for rel, _, _ in broken)
    for unit in cache:
        for qual, _node in iter_public_defs(unit.tree):
            have.add("%s::%s" % (unit.rel.replace(os.sep, "/"), qual))
    out = [(rel, line, "%s, so the public defs it was holding cannot be "
                       "accounted for" % detail)
           for rel, line, detail in broken]
    for name in sorted(set(want) - have):
        rel, _, qual = name.partition("::")
        if rel in blind:
            continue
        out.append((rel, 0, "public def `%s` is gone from %s; the surface "
                            "this rung annotates is the one it was handed"
                            % (qual, rel)))
    return out


def check_unannotated_public_defs(unit):
    """Every public ``def`` at module or class scope carries annotations.

    ``self``/``cls`` are exempt, as is ``__init__``'s return. Nested defs are
    not public surface and are skipped.
    """
    def public(name):
        return not name.startswith("_") or name in ("__init__", "__enter__",
                                                    "__exit__", "__iter__",
                                                    "__next__", "__call__")

    tops = list(unit.tree.body)
    for node in list(tops):
        if isinstance(node, ast.ClassDef):
            tops += [n for n in node.body
                     if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))]
    for node in tops:
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        if not public(node.name):
            continue
        args = node.args
        every = (list(getattr(args, "posonlyargs", [])) + list(args.args)
                 + list(args.kwonlyargs)
                 + [a for a in (args.vararg, args.kwarg) if a])
        for i, a in enumerate(every):
            if i == 0 and a.arg in ("self", "cls"):
                continue
            if a.annotation is None:
                yield node.lineno, "%s(%s) unannotated" % (node.name, a.arg)
        if node.returns is None and node.name != "__init__":
            yield node.lineno, "%s() has no return annotation" % node.name


#: Return annotations a generator may carry. Only the outermost name is read,
#: so ``Iterator[str]``, ``collections.abc.Iterator[str]`` and a bare
#: ``Iterator`` are one statement about the body, spelled three ways.
GENERATOR_RETURNS = ("Iterator", "Iterable", "Generator", "AsyncIterator",
                     "AsyncIterable", "AsyncGenerator")


def _own_scope(node):
    """Every node inside ``node``'s own scope.

    Nested ``def``, ``lambda`` and ``class`` bodies are skipped: a ``return``
    in a closure is the closure's statement about itself, and counting it
    against the enclosing function is how a check like this starts reporting
    a function that plainly returns nothing as one that returns something.
    """
    stack = list(ast.iter_child_nodes(node))
    while stack:
        child = stack.pop()
        if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef,
                              ast.Lambda, ast.ClassDef)):
            continue
        yield child
        stack += list(ast.iter_child_nodes(child))


def _ann_head(ann):
    """The outermost name of an annotation, or ``""`` if it has none.

    ``Iterator`` for ``collections.abc.Iterator[str]``, ``None`` for ``None``,
    ``""`` for ``str | None`` -- a union has no head, which is what
    :func:`_admits_none` is for.
    """
    if isinstance(ann, ast.Constant) and isinstance(ann.value, str):
        try:
            ann = ast.parse(ann.value, mode="eval").body
        except SyntaxError:
            return ""
    if isinstance(ann, ast.Subscript):
        ann = ann.value
    if isinstance(ann, ast.Attribute):
        return ann.attr
    if isinstance(ann, ast.Name):
        return ann.id
    if isinstance(ann, ast.Constant) and ann.value is None:
        return "None"
    return ""


def _ann_heads(ann):
    """Every member head of an annotation, with unions flattened.

    ``str`` -> ``{"str"}``; ``str | None`` -> ``{"str", "None"}``;
    ``Optional[dict[str, str]]`` -> ``{"dict", "None"}``; ``Union[int, str]``
    -> ``{"int", "str"}``. A subscript is read through to its base, because
    ``dict[str, int]`` and ``dict`` are the same answer to "what kind of
    object comes back".
    """
    if isinstance(ann, ast.Constant) and isinstance(ann.value, str):
        try:
            ann = ast.parse(ann.value, mode="eval").body
        except SyntaxError:
            return set()
    if isinstance(ann, ast.BinOp) and isinstance(ann.op, ast.BitOr):
        return _ann_heads(ann.left) | _ann_heads(ann.right)
    if isinstance(ann, ast.Subscript):
        base = _ann_head(ann.value)
        inner = ann.slice
        elts = inner.elts if isinstance(inner, ast.Tuple) else [inner]
        if base == "Optional":
            return _ann_heads(inner) | set(["None"])
        if base == "Union":
            out = set()
            for e in elts:
                out |= _ann_heads(e)
            return out
        if base == "Annotated":
            return _ann_heads(elts[0]) if elts else set()
        return set([base]) if base else set()
    head = _ann_head(ann)
    return set([head]) if head else set()


#: What each spelled-out return value is allowed to be declared as. The
#: supertypes are here so that a true annotation is never a finding --
#: ``-> Sequence[str]`` on ``return []`` is correct, and ``-> float`` on
#: ``return 1`` is correct under PEP 484's numeric tower. What is *not* here
#: is ``object``: it is true of everything, which is exactly why a rung that
#: asks for types cannot accept it as one.
_LITERAL_ADMITS = {
    "str": ("str", "Text", "AnyStr", "Sequence", "Iterable", "Collection",
            "Container", "Sized", "Reversible", "Hashable"),
    "bytes": ("bytes", "AnyStr", "ByteString", "Buffer", "Sequence",
              "Iterable", "Collection", "Container", "Sized", "Reversible",
              "Hashable"),
    "int": ("int", "float", "complex", "SupportsInt", "SupportsIndex",
            "SupportsFloat", "SupportsComplex", "Hashable"),
    "bool": ("bool", "int", "float", "complex", "SupportsInt",
             "SupportsIndex", "SupportsFloat", "SupportsComplex", "Hashable"),
    "float": ("float", "complex", "SupportsFloat", "SupportsComplex",
              "Hashable"),
    "complex": ("complex", "SupportsComplex", "Hashable"),
    "list": ("list", "List", "MutableSequence", "Sequence", "Iterable",
             "Collection", "Container", "Sized", "Reversible"),
    "dict": ("dict", "Dict", "MutableMapping", "Mapping", "Iterable",
             "Collection", "Container", "Sized"),
    "set": ("set", "Set", "MutableSet", "AbstractSet", "FrozenSet",
            "frozenset", "Iterable", "Collection", "Container", "Sized"),
    "tuple": ("tuple", "Tuple", "Sequence", "Iterable", "Collection",
              "Container", "Sized", "Reversible"),
}

#: Heads this check can reason about. ``None`` is deliberately absent: an
#: `X | None` whose ``X`` is a name from another module must stay unjudged,
#: and letting the ``None`` alone count as "known" would judge it.
_KNOWN_HEADS = frozenset(
    list(_LITERAL_ADMITS) + ["object", "Any"]
    + [h for heads in _LITERAL_ADMITS.values() for h in heads])

#: Return expressions whose type the source states outright.
_DISPLAY_TYPES = (
    (ast.ListComp, "list"), (ast.DictComp, "dict"), (ast.SetComp, "set"),
    (ast.List, "list"), (ast.Dict, "dict"), (ast.Set, "set"),
    (ast.Tuple, "tuple"), (ast.JoinedStr, "str"),
)

_CONSTANT_TYPES = {bool: "bool", int: "int", float: "float",
                   complex: "complex", str: "str", bytes: "bytes"}


def _returned_type(expr):
    """The builtin type of ``expr``, when the source says it outright.

    Literals, displays, comparisons and ``not`` -- nothing that depends on a
    name, a call or an import, because this check has one module in front of
    it and no more. Everything else reads as ``None``, and reading nothing is
    never a finding: this says what a spelled-out value *is*, it does not
    guess at what an expression might evaluate to.
    """
    if isinstance(expr, ast.Constant):
        return _CONSTANT_TYPES.get(type(expr.value))
    for node_type, name in _DISPLAY_TYPES:
        if isinstance(expr, node_type):
            return name
    if isinstance(expr, ast.Compare):
        return "bool"
    if isinstance(expr, ast.UnaryOp) and isinstance(expr.op, ast.Not):
        return "bool"
    return None


def _admits_none(ann):
    """Whether a body that falls off the end is a true instance of ``ann``."""
    try:
        text = ast.unparse(ann)
    except Exception:                                   # pragma: no cover
        return True
    return "None" in text or _ann_head(ann) in ("object", "Any", "Optional")


def _annotation_names(ann, got):
    """Whether ``ann`` *names* the type ``got``, or declines to judge.

    Three answers, not two. ``True`` -- the annotation is true of ``got`` and
    says so. ``False`` -- every member is a name this module knows and none of
    them is ``got`` or a supertype of it. ``None`` -- at least one member is a
    name from somewhere else, so this has no opinion: an alias it cannot
    resolve must never become a finding.

    Subtyping is not the question. ``object`` is a true supertype of every
    ``got`` and is deliberately absent from :data:`_LITERAL_ADMITS`, because a
    rung that asks for types cannot accept the one that is true of everything
    as an answer. Same reasoning as the ban on ``Any``, and the same reasoning
    the instruction gives for it.
    """
    heads = _ann_heads(ann)
    if not heads or ((heads - set(["None"])) - _KNOWN_HEADS):
        return None
    return bool(heads & set(_LITERAL_ADMITS[got]))


def _spelled_defaults(node):
    """``(param, default)`` for every default this signature writes out."""
    args = node.args
    pos = list(args.posonlyargs) + list(args.args)
    pairs = list(zip(pos[len(pos) - len(args.defaults):], args.defaults))
    pairs += [(p, d) for p, d in zip(args.kwonlyargs, args.kw_defaults)
              if d is not None]
    return pairs


def check_parameter_defaults_agree(unit):
    """A parameter annotation has to be true of the default beside it.

    The companion to :func:`check_return_annotation_agrees`, at the other end
    of the signature, and it exists for a vector that one could not reach.

    ``Any`` is banned by name, so the same empty claim gets spelled
    ``object``: true of every value, so no checker has anything to say about
    it, and unlike ``Any`` it cannot simply be banned -- ``object`` is the
    right answer 157 times in this tree's own golden, and ``**kwargs:
    object`` is the idiomatic spelling under a strict checker. Annotating all
    776 sites ``object`` measured 0.533 with only the return rule in place:
    the same score as doing the hard 17-file third honestly, for a tree that
    wrote down nothing.

    What separates them is not the returns -- the golden's own returns are
    ``object`` 124 times out of 354, because a plugin function that hands back
    a Sublime API object has nothing else true to say. It is the
    *parameters*: 33 of the golden's 429 against 422 of 422. And a parameter
    with a default is the one place where the source has already said what the
    type is, right beside the annotation that is supposed to name it. ``def
    f(timeout: object = 10)`` is a signature contradicting itself in six
    characters.

    So: where a default is a value the source spells out, the annotation has
    to name its type. Only spelled-out defaults are read -- 64 of the golden's
    210 -- and a default of ``None`` says nothing about the non-``None`` type
    and is skipped, as is any annotation this module cannot resolve.

    That 64 is a count of the *finished* tree, and it is not the count of the
    tree the step is handed: the rung-04 golden has 57, and so does the tree
    the rung-04 blind agent produced independently. The difference is the
    seven parameters a solver names where the source had ``*args`` -- this
    module reads whatever signature is there at the end, so the number grows
    with the work. The instruction quotes 57 for that reason; quoting 64
    there would have handed the agent a figure no correct tree need match. The
    automated migrator arm passes this untouched: inferring an annotation from
    a default is exactly what ``autotyping --aggressive`` does.
    """
    for _qual, node in iter_public_defs(unit.tree):
        for param, default in _spelled_defaults(node):
            if param.arg in ("self", "cls") or param.annotation is None:
                continue
            got = _returned_type(default)
            if got is None:
                continue
            if _annotation_names(param.annotation, got) is False:
                yield param.lineno, (
                    "%s(%s: %s = %s) -- the default is a %s, and the "
                    "annotation beside it does not say so"
                    % (node.name, param.arg, ast.unparse(param.annotation),
                       ast.unparse(default), got))


def check_return_annotation_agrees(unit):
    """A return annotation has to be true of the body it sits on.

    Step 05's other two own checks do not reach this. ``typed-own-surface``
    reads presence and spelling and never reads *what* was declared;
    ``types-check-clean`` reads declarations against each other but only over
    ``lib/``, because the plugin layer provably cannot satisfy it (see
    :func:`check_types_clean`). That leaves 577 of the tree's 776 signatures
    with nothing reading them -- and under
    ``from __future__ import annotations`` no behaviour case can read them
    either, since annotations are never evaluated. A tree that mass-annotates
    the plugin layer with confident nonsense would clear the rung.

    So this check reads each return annotation against the one thing that is
    always present and always local: the function's own body.

    * A missing return annotation is a finding. That is what keeps the check
      non-vacuous on a predecessor that has annotated nothing -- the
      family-2 membership rule of section 9.6 -- and it deliberately overlaps
      ``public-surface-annotated``.
    * ``-> None`` on a body that returns a value is a finding.
    * A generator annotated as anything but an iterator type is a finding.
    * A ``return`` of a value the source spells out -- a literal, a display,
      a comparison -- under an annotation that cannot be true of it is a
      finding. This is the rule that costs ``-> object``. Annotating all 776
      signatures ``object`` is the same move as annotating them all ``Any``,
      one identifier further from the word the rung bans, and it measured
      0.533 before this rule existed: every ``object`` is true, so nothing
      that reads declarations against each other has anything to say, and
      ``typed-own-surface`` passes outright. What it is not true of is the
      value in front of it -- ``return {}`` under ``-> object`` says nothing
      a reader did not already know -- so that is what is read. Only values
      the source states outright are read, so a body whose returns are calls
      or names is judged by nothing here, and an annotation whose every
      member is a name from another module is left alone: an alias this
      check cannot resolve must not become a finding.
    * A concrete, non-optional annotation on a body that never returns a
      value is a finding.

    The last one exempts anything mentioning ``None`` and anything headed
    ``object``: a body that falls off the end is a true instance of those. A
    body whose only exit is ``raise``, and a stub body (``...``, ``pass``, a
    docstring), declare nothing about a return value and are exempt from it
    too -- an abstract method and a ``Protocol`` are declarations, not
    implementations.

    ``__init__`` is skipped: it has no return annotation to read, which is
    the same exemption :func:`check_unannotated_public_defs` grants it.
    """
    def public(name):
        return not name.startswith("_") or name in ("__enter__", "__exit__",
                                                    "__iter__", "__next__",
                                                    "__call__")

    tops = list(unit.tree.body)
    for node in list(tops):
        if isinstance(node, ast.ClassDef):
            tops += [n for n in node.body
                     if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))]
    for node in tops:
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        if not public(node.name):
            continue
        if node.returns is None:
            yield node.lineno, "%s() has no return annotation" % node.name
            continue
        own = list(_own_scope(node))
        rets = [n for n in own if isinstance(n, ast.Return)]
        valued = [r for r in rets
                  if r.value is not None
                  and not (isinstance(r.value, ast.Constant)
                           and r.value.value is None)]
        yields = any(isinstance(n, (ast.Yield, ast.YieldFrom)) for n in own)
        raises = any(isinstance(n, ast.Raise) for n in own)
        stub = all(isinstance(st, ast.Pass)
                   or (isinstance(st, ast.Expr)
                       and isinstance(st.value, ast.Constant))
                   for st in node.body)
        head = _ann_head(node.returns)
        shown = ast.unparse(node.returns)
        if yields:
            if head not in GENERATOR_RETURNS:
                yield node.lineno, ("%s() yields but is annotated `-> %s`"
                                    % (node.name, shown))
            continue
        if head == "None":
            if valued:
                yield valued[0].lineno, ("%s() is annotated `-> None` but "
                                         "returns a value" % node.name)
            continue
        for ret in valued:
            got = _returned_type(ret.value)
            if got is None or _annotation_names(node.returns, got) is not False:
                continue
            yield ret.lineno, ("%s() is annotated `-> %s` but returns a %s "
                               "here; a return annotation names what comes "
                               "back" % (node.name, shown, got))
            break
        if valued or stub or (raises and not rets) or _admits_none(node.returns):
            continue
        yield node.lineno, ("%s() is annotated `-> %s` but no path returns a "
                            "value" % (node.name, shown))


#: The ``typing`` decorators that turn the type checker off for what they wrap.
NO_TYPE_CHECK_NAMES = frozenset(("no_type_check", "no_type_check_decorator"))


def check_suppressions(unit):
    """``# type: ignore`` / ``# noqa`` / ``# mypy: ignore-errors``.

    Comments are gone from the AST, so this reads COMMENT tokens -- still the
    tokenizer's view, not a text scan: a ``# noqa`` inside a string literal is
    one token of type STRING and does not match.
    """
    for tok in unit.tokens:
        if tok.type != tokenize.COMMENT:
            continue
        low = tok.string.lower()
        for marker in ("type: ignore", "noqa", "mypy:", "pragma: no cover"):
            if marker in low:
                yield tok.start[0], tok.string.strip()[:60]
                break
    # The decorator spelling of the same thing: ``typing.no_type_check``
    # switches the checker off for everything it wraps, so a body under it is
    # never read (wave-D S-C2, measured). Matched as a name, an attribute, an
    # imported name under any alias, or a string naming it -- not a text scan,
    # so a docstring that mentions it is not a finding.
    for node in ast.walk(unit.tree):
        name = None
        if isinstance(node, ast.Name):
            name = node.id
        elif isinstance(node, ast.Attribute):
            name = node.attr
        elif isinstance(node, ast.alias):
            name = node.name.rsplit(".", 1)[-1]
        elif isinstance(node, ast.Constant) and isinstance(node.value, str):
            name = node.value.strip()
        if name in NO_TYPE_CHECK_NAMES:
            yield getattr(node, "lineno", 0), "typing.%s" % name
    # Control flow spelling the same thing (wave F, measured with the step-05
    # checker under the scorer's config): a checker takes ``TYPE_CHECKING`` as
    # true and never reads the branch it rules out -- the body of ``if not
    # TYPE_CHECKING:``, the ``else``/``elif`` of ``if TYPE_CHECKING:``, the
    # other arm of ``a if TYPE_CHECKING else b``, a ``while not
    # TYPE_CHECKING:`` loop -- nor anything after an ``if TYPE_CHECKING:``
    # block that returns or raises. The interpreter runs exactly that code,
    # so a module wrapped this way passes the strict check whatever it holds.
    # The plain block that only declares hides nothing (the checker reads its
    # body, the interpreter skips it) and stays legal.
    for node in ast.walk(unit.tree):
        if not isinstance(node, (ast.If, ast.IfExp, ast.While, ast.Assert)):
            continue
        if not any(_names_checker_constant(n) for n in ast.walk(node.test)):
            continue
        if (isinstance(node, ast.If) and _names_checker_constant(node.test)
                and not node.orelse and _declares_only(node.body)):
            continue
        yield node.lineno, ("code the type checker does not read: a `%s` on "
                            "TYPE_CHECKING other than a plain `if` block of "
                            "declarations" % type(node).__name__.lower())


#: Names a static checker takes as always true: ``typing.TYPE_CHECKING``, and
#: ``MYPY``, the older spelling of the same constant that checkers still honour.
CHECKER_CONSTANTS = frozenset(("TYPE_CHECKING", "MYPY"))

#: What an ``if TYPE_CHECKING:`` block may hold: declarations, and nothing that
#: leaves the block -- after a ``return``, ``raise``, ``break``, ``continue``
#: or ``NoReturn`` call there, a checker treats the rest of the enclosing
#: block as unreachable and stops reading it.
_DECLARATIONS = (ast.Import, ast.ImportFrom, ast.ClassDef, ast.FunctionDef,
                 ast.AsyncFunctionDef, ast.Assign, ast.AnnAssign, ast.Pass)
if hasattr(ast, "TypeAlias"):
    _DECLARATIONS += (ast.TypeAlias,)


def _names_checker_constant(node):
    return ((isinstance(node, ast.Name) and node.id in CHECKER_CONSTANTS)
            or (isinstance(node, ast.Attribute)
                and node.attr in CHECKER_CONSTANTS))


def _declares_only(body):
    for stmt in body:
        if isinstance(stmt, _DECLARATIONS):
            continue
        if (isinstance(stmt, ast.Expr)
                and isinstance(stmt.value, ast.Constant)):
            continue                                # a docstring, or ``...``
        if (isinstance(stmt, ast.If) and _names_checker_constant(stmt.test)
                and not stmt.orelse and _declares_only(stmt.body)):
            continue
        return False
    return True


def check_compiles_clean(unit):
    """What the compiler itself says, with warnings promoted to findings.

    This is the only authority on invalid string escapes (a DeprecationWarning
    up to 3.11, a SyntaxWarning from 3.12) and on PEP 765, and it costs nothing
    to ask it rather than to reimplement it.
    """
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        try:
            compile(unit.src, unit.rel, "exec")
        except SyntaxError as exc:
            yield exc.lineno or 0, "SyntaxError: %s" % exc
            return
    for w in caught:
        yield getattr(w, "lineno", 0) or 0, "%s: %s" % (
            w.category.__name__, w.message)


#: Six's shim surface, plus the tree's own hand-rolled Python 2 shims. None of
#: these is named by any gate, because none of them has a single right answer:
#: at the rung that deletes six an agent may rebind ``advance_iterator`` to the
#: builtin ``next`` or drop the name outright, and both satisfy the
#: instruction. A case that probes one therefore scores a coin flip and then
#: becomes unpassable for every later rung (trap 9). Two of them were in the
#: suite and cost the step 01 oracle two cases before this list existed:
#: ``advance_iterator`` in ``rrule.import-surface`` and ``tzname_in_python2``
#: in ``dateutil.module-exports``.
#: ``HTTPResponse.from_httplib`` is deliberately absent: it reads like a shim
#: and is not one. It is vendored urllib3 1.x's own constructor, called by
#: ``connectionpool.urlopen`` on every interpreter in the ladder, and no rung
#: touches it -- banning it would have cost four live probes in
#: ``urllib3_more.py`` for nothing.
PY2_SHIMS = frozenset((
    "advance_iterator", "tzname_in_python2", "wrap_ord",
    "is_py2", "is_py3", "PY2", "PY3", "reraise", "raise_from", "exec_",
    "print_", "with_metaclass", "add_metaclass", "string_types",
    "integer_types", "binary_type", "text_type", "class_types",
    "get_unbound_function", "get_method_function", "create_bound_method",
    "int2byte", "byte2int", "indexbytes", "iterbytes", "maxint"))

#: Names in the tables below that survive the whole ladder under a Python 3
#: spelling and so are fair game in a case body: ``functools.reduce`` and
#: ``sys.intern`` are the py3 homes of two names the *tree* may no longer use
#: as builtins.
CASE_NAME_EXEMPT = frozenset(("reduce", "intern"))

#: Names and modules a *case body* may not touch, because some rung of the
#: ladder requires them gone. See :func:`sweep_cases`. Derived from the gates'
#: own tables rather than restated, so a name added to a gate cannot go on
#: being probed by a case: the sweep and the gate move together.
CASE_BANNED_NAMES = ((PY2_BUILTINS | PY2_METHODS | NAIVE_UTC | SSL_DEAD_NAMES
                      | PY2_SHIMS) - CASE_NAME_EXEMPT)

CASE_BANNED_MODULES = frozenset(("six",)) | DEAD_312 | DEAD_313

CASE_BANNED_LITERALS = ("ssl_match_hostname", "packages.six",
                        "packages.ordered_dict")

#: The five ``__future__`` features instruction item 5 names. Unconditional
#: on every Python 3, so importing them is noise; ``annotations`` is
#: deliberately absent, because step 05 requires it and step 07 drops it again.
#: The names items 1-9 retire, as they would still read if an arm were left
#: behind as text instead of deleted. A regex and not the AST checks on
#: purpose: the whole point is Python 2 code that no longer parses *as code*,
#: so there is no AST to match on. Round-5 review, section 7.14.
COMMENTED_OUT_NAMES = re.compile(
    r"\b(sys\.version_info|version_info|PY2|PY3|is_py2|is_py3|six|"
    r"basestring|unicode|xrange|long|unichr|raw_input|execfile|apply|"
    r"coerce|buffer|iteritems|iterkeys|itervalues|iterlists|has_key|"
    r"ordered_dict|absolute_import|unicode_literals|print_function)\b")


COMPOUND = (ast.If, ast.For, ast.While, ast.Try, ast.With,
            ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)


def _is_dead_block(mod):
    """True if the parsed text is a *branch*, not a one-line note.

    ``# was: six.b(s)`` parses -- as an annotated assignment -- and so would
    any number of perfectly ordinary notes an author leaves behind while
    porting. Those are prose about the change, and penalising them would be a
    check with no requirement behind it. What item 4 forbids is the arm
    itself: a compound statement, an import, or two or more statements
    together. One line is a note; a block is the branch.
    """
    if not mod.body:
        return False
    if len(mod.body) >= 2:
        return True
    st = mod.body[0]
    return isinstance(st, COMPOUND + (ast.Import, ast.ImportFrom))


def _comment_blocks(unit):
    """Runs of consecutive whole-line comments, rebuilt into the text they
    were before someone put a ``#`` in front of every line.

    Reconstructing the run rather than reading one line at a time is the
    whole instrument: ``if sys.version_info[0] == 2:`` on its own does not
    parse (it has no body), and an indented continuation line on its own does
    not parse either. Only the block does. Round-5 review, section 7.14.
    """
    lines = unit.src.decode("utf-8", "replace").splitlines()
    run = []
    for tok in unit.tokens:
        if tok.type != tokenize.COMMENT:
            continue
        row, col = tok.start
        if lines[row - 1][:col].strip():
            run = []          # trailing comment on a code line, not a block
            continue
        if run and row != run[-1][0] + 1:
            yield run
            run = []
        body = tok.string[1:]
        if body.startswith(" "):
            body = body[1:]
        run.append((row, " " * col + body))
    if run:
        yield run


def _docstring_expr_ids(tree):
    """ids of the Expr nodes that are real docstrings, so the string arm
    below cannot fire on a usage example in someone's module docstring.

    Returns the ``Expr`` ids, not the ``Constant`` ids -- the string arm walks
    ``Expr`` nodes. :func:`_docstring_constant_ids` is the other half of the
    pair and returns the ``Constant`` ids for :func:`sweep_cases`, which walks
    constants. They were one name for two return types until round 2 of the
    static review; the second definition shadowed the first at import time and
    the exemption here silently stopped firing.
    """
    out = set()
    for node in ast.walk(tree):
        body = getattr(node, "body", None)
        if not isinstance(body, list) or not body:
            continue
        if not isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef,
                                 ast.AsyncFunctionDef)):
            continue
        first = body[0]
        if isinstance(first, ast.Expr) and isinstance(
                first.value, ast.Constant) and isinstance(
                    first.value.value, str):
            out.add(id(first))
    return out


def check_commented_out_py2(unit):
    """A retired branch parked in a comment block or a free-standing string.

    Every other check here reads the AST, so commenting a branch out rather
    than deleting it satisfies all of them at once while leaving the Python 2
    code in the file. That is the gaming vector this closes, and it is the
    check behind the instruction's "deleted, not commented out".
    """
    for run in _comment_blocks(unit):
        text = textwrap.dedent("\n".join(b for _, b in run))
        if not COMMENTED_OUT_NAMES.search(text):
            continue
        try:
            mod = ast.parse(text)
        except (SyntaxError, ValueError):
            continue
        if _is_dead_block(mod):
            yield run[0][0], "commented-out branch: %s" % (
                text.strip().splitlines()[0][:60])

    docs = _docstring_expr_ids(unit.tree)
    for node in ast.walk(unit.tree):
        if not (isinstance(node, ast.Expr)
                and isinstance(node.value, ast.Constant)
                and isinstance(node.value.value, str)):
            continue
        if id(node) in docs:
            continue
        text = node.value.value
        if not COMMENTED_OUT_NAMES.search(text):
            continue
        try:
            mod = ast.parse(textwrap.dedent(text))
        except (SyntaxError, ValueError):
            continue
        if _is_dead_block(mod):
            yield node.lineno, "branch parked in a string: %s" % (
                text.strip().splitlines()[0][:60])


LEGACY_FUTURE = frozenset((
    "absolute_import", "unicode_literals", "print_function",
    "division", "with_statement",
))

#: Interpreter-version flags, in the spellings this tree uses. ``PY2``/``PY3``
#: are six's; ``is_py2``/``is_py3`` are ``requests/compat.py``'s, derived from
#: ``sys.version_info`` and read in three other modules. Item 4 bans the flag
#: "in any spelling", and a flag recoded to a constant is still a flag.
PY2_FLAG_NAMES = frozenset(("PY2", "PY3", "is_py2", "is_py3"))

#: Method spellings the ladder retires, as they appear inside a *probe* --
#: ``hasattr(x, "next")``, ``getattr(x, "iteritems", None)``. A case that asks
#: whether a name is still bound pins the spelling rather than the behaviour,
#: and the instruction explicitly lets these be renamed or removed, so the
#: probe makes a legal answer fail. Kept apart from CASE_BANNED_LITERALS
#: because these are only banned in argument position: "next" as a bare string
#: is an ordinary word and appears in `Link: rel="next"` fixtures.
CASE_BANNED_PROBES = PY2_METHODS | frozenset(("next",))


def _docstring_constant_ids(tree):
    """Node ids of every docstring *constant* in ``tree``.

    Prose gets the same exemption here that the gates' clean fixture asserts
    for them: a comment or a docstring explaining why a module is *not* used
    must not count as using it.
    """
    out = set()
    for node in ast.walk(tree):
        body = getattr(node, "body", None)
        if not isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef,
                                 ast.AsyncFunctionDef)) or not body:
            continue
        first = body[0]
        if (isinstance(first, ast.Expr) and isinstance(first.value, ast.Constant)
                and isinstance(first.value.value, str)):
            out.add(id(first.value))
    return out


def sweep_cases(case_dir, stream=sys.stdout):
    """Trap 9, as a re-runnable check rather than a one-off grep.

    A behaviour case that names a module the ladder deletes scores the
    predecessor and then becomes unpassable at the rung that removes its
    subject -- the agent fails a test for a reason its own requirements caused
    and that no instruction mentioned. This walks every case body and reports
    any reference to such a name, module or ``importlib`` path literal, with
    docstrings exempt.

    Run it after adding a case. It is cheap and it is the only thing standing
    between "the suite grew" and "the suite grew a test nobody can pass".
    """
    hits = []
    mods = sorted(f for f in os.listdir(case_dir) if f.endswith(".py"))
    for name in mods:
        with open(os.path.join(case_dir, name), "rb") as fh:
            tree = ast.parse(fh.read(), filename=name)
        docs = _docstring_constant_ids(tree)
        for node in ast.walk(tree):
            if isinstance(node, ast.Name) and node.id in CASE_BANNED_NAMES:
                hits.append((name, node.lineno, node.id))
            elif (isinstance(node, ast.Attribute)
                  and node.attr in CASE_BANNED_NAMES):
                hits.append((name, node.lineno, "." + node.attr))
            elif isinstance(node, ast.Import):
                for alias in node.names:
                    if _root(alias.name) in CASE_BANNED_MODULES:
                        hits.append((name, node.lineno,
                                     "import " + alias.name))
            elif (isinstance(node, ast.ImportFrom) and not node.level
                  and _root(node.module) in CASE_BANNED_MODULES):
                hits.append((name, node.lineno, "from " + node.module))
            elif (isinstance(node, ast.Constant)
                  and isinstance(node.value, str) and id(node) not in docs):
                for frag in CASE_BANNED_LITERALS:
                    if frag in node.value:
                        hits.append((name, node.lineno,
                                     "literal %r" % node.value[:60]))
            elif (isinstance(node, ast.Call)
                  and isinstance(node.func, ast.Attribute)
                  and node.func.attr == "next"):
                hits.append((name, node.lineno, ".next()"))
            elif (isinstance(node, ast.Call)
                  and isinstance(node.func, ast.Name)
                  and node.func.id in ("hasattr", "getattr")
                  and len(node.args) >= 2
                  and isinstance(node.args[1], ast.Constant)
                  and isinstance(node.args[1].value, str)
                  and node.args[1].value in CASE_BANNED_PROBES):
                hits.append((name, node.lineno, "%s(..., %r)"
                             % (node.func.id, node.args[1].value)))

    if hits:
        print("TRAP-9: %d reference(s) to a construct the ladder deletes"
              % len(hits), file=stream)
        for row in hits:
            print("  %s:%s  %s" % row, file=stream)
        return 1
    print("trap-9 sweep clean: %d case modules, no body names a construct or "
          "module the ladder requires deleted" % len(mods), file=stream)
    return 0


#: Tokens :func:`sweep_recording` looks for inside a recorded *value*. Same
#: tables the case sweep uses, plus ``six`` and the bare ``next`` spelling.
RECORDING_BANNED_TOKENS = (CASE_BANNED_NAMES | CASE_BANNED_PROBES
                           | frozenset(("six",)))

#: Recorded observations that legitimately contain one of those tokens, with
#: the number of string leaves it appears in. Every entry was read and
#: justified once; the count is what makes the exemption a tripwire rather
#: than a blanket -- a *new* leaf in an already-exempt case changes the number
#: and the sweep fires.
RECORDING_PIN_EXEMPT = {
    # CPython's own OverflowError text for a too-large int. Not a spelling in
    # the tree; C's `long` is still called `long`.
    ("dateutil.parse-rejects", "long"): 1,
    # struct.error's own wording ("unpack requires a buffer of 24 bytes").
    ("dateutil.tzfile-rejects", "buffer"): 3,
    # chardet's own ValueError literal, at lib/requests/packages/chardet/
    # __init__.py. A user-visible exception message, preserved deliberately:
    # the instruction puts "the exact message text of every exception raised"
    # inside the behaviour being preserved, and the oracle keeps the literal
    # byte-for-byte.
    ("requests.apparent-encoding-refuses-and-gives-up", "unicode"): 2,
    # `Link: <...>; rel="next"` -- the HTTP header word, not the method.
    ("requests.parse-header-links", "next"): 2,
    # the fixture server's /next route, in a URL, a log line and the wire log.
    ("requests.session-resolve-redirects", "next"): 4,
    ("urllib3.response-headers-and-redirect", "next"): 6,
    ("urllib3v.response-headers-and-redirect", "next"): 6,
}


def _leaves(value, path, out):
    """Every string leaf in a recorded observation, with its path.

    The recorder writes dicts that must keep their order as
    ``{"__dict__": [[k, v], ...]}`` and tuples as ``{"__tuple__": [...]}``;
    the first is unwrapped to its keys so a path reads like the case body that
    produced it, and the second falls through to the ordinary list branch.
    """
    if isinstance(value, dict):
        if list(value) == ["__dict__"]:
            for key, sub in value["__dict__"]:
                _leaves(sub, "%s.%s" % (path, key), out)
            return
        for key in value:
            _leaves(value[key], "%s.%s" % (path, key), out)
    elif isinstance(value, list):
        for i, sub in enumerate(value):
            _leaves(sub, "%s[%d]" % (path, i), out)
    elif isinstance(value, str):
        out.append((path, value))


def sweep_recording(path, stream=sys.stdout):
    """Trap 9 again, from the other side: the *recording*, not the case body.

    :func:`sweep_cases` reads case source, so it cannot see a case whose body
    is clean but whose recorded value is not -- ``sorted(dir(obj))`` naming a
    ``next`` alias, an exception message quoting ``iteritems``. The expectation
    is the value, so that value is the pin: the rung that deletes the spelling
    makes the case unpassable, and nothing in the instruction warned anyone.

    Every hit is either a real pin or a word that happens to collide, so hits
    are matched against :data:`RECORDING_PIN_EXEMPT` by count. An unlisted
    case, an unlisted token, or a listed pair whose count moved is reported.
    Re-run it after every re-record.
    """
    with gzip.open(path, "rt") as fh:
        cases = json.load(fh)["cases"]

    seen = {}
    for cid in sorted(cases):
        out = []
        _leaves(cases[cid], "", out)
        for where, text in out:
            for tok in set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", text)):
                if tok in RECORDING_BANNED_TOKENS:
                    seen.setdefault((cid, tok), []).append((where, text))

    bad = []
    for key in sorted(seen):
        want = RECORDING_PIN_EXEMPT.get(key)
        got = len(seen[key])
        if want is None:
            bad.append((key, "not exempt", got, seen[key]))
        elif want != got:
            bad.append((key, "exempt for %d leaves" % want, got, seen[key]))
    for key in sorted(RECORDING_PIN_EXEMPT):
        if key not in seen:
            bad.append((key, "exempt but absent", 0, []))

    if bad:
        print("TRAP-9 (recording): %d retired spelling(s) pinned in a recorded "
              "value" % len(bad), file=stream)
        for (cid, tok), why, got, where in bad:
            print("  %s  token %r  %s, found %d" % (cid, tok, why, got),
                  file=stream)
            for path_, text in where[:4]:
                print("      %-40s %s" % (path_ or "<root>", text[:90]),
                      file=stream)
        return 1
    print("trap-9 recording sweep clean: %d cases, %d known-benign leaf(s) in "
          "%d case(s), no retired spelling pinned in a value"
          % (len(cases), sum(RECORDING_PIN_EXEMPT.values()),
             len(RECORDING_PIN_EXEMPT)), file=stream)
    return 0



def paths_absent(globs):
    """A filesystem gate: these paths must not exist."""
    def check(tree):
        out = []
        for dirpath, dirnames, filenames in os.walk(tree):
            dirnames[:] = [d for d in dirnames if d != "__pycache__"]
            for name in filenames:
                rel = os.path.relpath(os.path.join(dirpath, name), tree)
                if match_any(rel, globs):
                    out.append((rel, 0, "still present"))
        return out
    return check


# --------------------------------------------------------------- gates ----


class Gate(object):
    def __init__(self, gid, what, check=None, tree_check=None, scope=None):
        self.gid = gid
        self.what = what
        self.check = check
        self.tree_check = tree_check
        self.scope = scope

    def run(self, tree, cache):
        # Both, where a gate carries both: ``ssl-match-hostname-deleted`` asks
        # a filesystem question ("is the directory gone") and a source question
        # ("did it come back somewhere else"), and one answer without the other
        # is the hole rung 03's static review found.
        out = []
        if self.tree_check is not None:
            out += [{"file": f, "line": n, "detail": d}
                    for f, n, d in self.tree_check(tree)]
        if self.check is None:
            return out
        for unit in cache:
            if self.scope and not match_any(unit.rel, self.scope):
                continue
            for lineno, detail in self.check(unit):
                out.append({"file": unit.rel, "line": lineno, "detail": detail})
        return out


#: D-5 scopes the typing requirements to the repo's own surface rather than the
#: whole vendored dependency set: the plugin's own code, plus the Elasticsearch
#: client core it owns the surface of. 158 files, 5,683 lines, 776 parameters
#: and returns to annotate on the baseline -- measured, and the same list the
#: step-05 instruction states verbatim.
#:
#: Two parts of ``lib/elasticsearch/`` are deliberately outside it, and the
#: instruction says so: the six ``@query_params`` API tables
#: (``client/{__init__,cat,cluster,indices,nodes,snapshot}.py``, 426 more
#: signatures that are all the same signature) and the generated Thrift package
#: ``connection/esthrift/``. Neither teaches anything the rest does not, and
#: padding a step with 426 copies of one edit is the "easy" failure mode
#: INSTRUCTIONS.md warns about, not depth.
#: The scope is split in two because the two requirements this rung states over
#: it do not cover the same files. Every one of the 158 has to *declare* its
#: types; the client core additionally has to declare types that *agree with
#: each other* under a strict checker. :func:`check_types_clean`'s docstring
#: has the measurement that forced the narrower second scope -- in one line,
#: the plugin layer cannot be strictly checked without the `Any` this rung
#: bans or the suppression comment it also bans. NOTES section 11.2.
#:
#: Split, never duplicated: ``OWN_SURFACE`` is their concatenation, so the AST
#: gates and the checker cannot come to disagree about what "the repo's own
#: surface" means, only about how much of it each one reads.
TYPED_PLUGIN = ("ElasticsearchClient.py", "events.py", "commands/*.py",
                "panel/*.py")

TYPED_LIB = ("lib/elasticsearch_connections.py", "lib/elasticsearch/*.py",
             "lib/elasticsearch/client/utils.py",
             "lib/elasticsearch/connection/*.py",
             "lib/elasticsearch/helpers/*.py")

OWN_SURFACE = TYPED_PLUGIN + TYPED_LIB


#: The checker's configuration, shipped here rather than in the tree, because
#: rule 6 says the scorer runs its tools "from its own pristine copies with its
#: own configs" -- a config file the candidate could edit is a gate the
#: candidate sets the level of.
#:
#: ``disallow_subclassing_any`` is the one strict setting that is off, and the
#: reason is D-6 rather than difficulty. Six classes in the tree inherit from
#: ``sublime_plugin.WindowCommand`` / ``TextCommand`` / ``EventListener`` or
#: from ``unittest2.TestCase``; all four live in packages that are absent from
#: both images and, in Sublime's case, from every index. The only way to clear
#: the finding is to hand-write stubs for an API that ships inside a
#: non-redistributable binary -- which is D-6's invented-driver failure with a
#: ``.pyi`` extension, and which the instruction could not state without
#: describing the stub it expects. Everything else strict implies stays on,
#: ``no_implicit_reexport`` included.
#:
#: ``ignore_missing_imports`` names exactly the roots D-5 enumerated and two
#: more. It is a list, not a wildcard: ``ignore_missing_imports`` set globally
#: would also swallow a typo'd import of a module the tree *does* have.
#:
#: ``follow_imports = skip`` draws the edge of the typed island, and it names
#: only what D-5 already put outside the scope: the four vendored packages
#: under ``lib/`` (``requests``, ``urllib3``, ``analytics``, ``dateutil``), the
#: six ``@query_params`` API tables, and the generated Thrift package. Without
#: it a checked file that calls ``requests.session()`` reports
#: ``no-untyped-call`` -- a finding about vendored code, charged to the file
#: that imports it, which the instruction does not ask anyone to fix and could
#: not ask without pulling 426 signatures back into the scope it just
#: excluded. ``elasticsearch.client.utils`` is named again, after the wildcard
#: that would have caught it, because it *is* in the scope; mypy takes the
#: most specific section, so the order in this file is not what decides it.
TYPE_CHECK_CONFIG = """\
[mypy]
strict = True
disallow_subclassing_any = False
python_version = %s
follow_imports = silent
show_error_codes = True
show_column_numbers = False
incremental = False
warn_unused_configs = False
color_output = False
error_summary = False
pretty = False

[mypy-sublime]
ignore_missing_imports = True

[mypy-sublime_plugin]
ignore_missing_imports = True

[mypy-thrift.*]
ignore_missing_imports = True

[mypy-simplejson.*]
ignore_missing_imports = True

[mypy-unittest2.*]
ignore_missing_imports = True

[mypy-pylibmc.*]
ignore_missing_imports = True

[mypy-requests]
follow_imports = skip

[mypy-requests.*]
follow_imports = skip

[mypy-urllib3]
follow_imports = skip

[mypy-urllib3.*]
follow_imports = skip

[mypy-analytics]
follow_imports = skip

[mypy-analytics.*]
follow_imports = skip

[mypy-dateutil.*]
follow_imports = skip

[mypy-elasticsearch.client]
follow_imports = skip

[mypy-elasticsearch.client.*]
follow_imports = skip

[mypy-elasticsearch.client.utils]
follow_imports = normal

[mypy-elasticsearch.connection.esthrift.*]
follow_imports = skip
"""

#: ``path:line: error: detail  [code]``, and the ``path: error: detail`` form a
#: fatal takes. Notes are not findings; they are the second line of one.
_MYPY_LINE = re.compile(r"^(?P<file>[^:]+):(?:(?P<line>\d+):)?\s*"
                        r"error:\s*(?P<detail>.*)$")


def _checker():
    """The type checker binary, or ``None``.

    The step-05 verifier image installs it in its own virtualenv at
    ``$TYPECHECK_VENV`` -- deliberately *not* in the scored interpreter's
    site-packages, so that a candidate cannot import the checker's own
    dependencies and type-check against packages its image never had.
    An authoring host points ``MYPY_BIN`` at one instead.
    """
    cand = os.environ.get("MYPY_BIN")
    if cand and os.path.exists(cand):
        return cand
    cand = os.path.join(os.environ.get("TYPECHECK_VENV", "/opt/typecheck"),
                        "bin", "mypy")
    return cand if os.path.exists(cand) else None


def _target_version():
    """The language level to check at: this verifier's interpreter, named.

    Read from ``VERIFIER_PYTHON`` (``python3.13``, ``python3.13t``,
    ``python3.14``) rather than from the interpreter running the checker. The
    two are the same build in every image this package ships, but they are
    different questions -- what the checker *runs on* is an implementation
    detail of the verifier image, what it *targets* is the rung's language
    level -- and ``3.13t`` has to reach mypy as ``3.13`` either way.
    """
    name = os.environ.get("VERIFIER_PYTHON", "")
    m = re.search(r"(\d+)\.(\d+)", name)
    if m:
        return "%s.%s" % m.groups()
    return "%d.%d" % sys.version_info[:2]


def _scoped(root, patterns):
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for name in filenames:
            if not name.endswith(".py"):
                continue
            rel = os.path.relpath(os.path.join(dirpath, name), root)
            if match_any(rel, patterns):
                out.append(rel.replace(os.sep, "/"))
    return sorted(out)


def _root_tool_env(scratch):
    """The environment for a Python tool this (root) scorer runs by its
    console script -- mypy, pyupgrade -- which, unlike the scorer, is not
    started with ``-I`` and so honours what it inherits. Its bytecode prefix
    is a directory inside ``scratch`` (a root ``mkdtemp``, 0700), never one
    the candidate's uid could have created and filled first, and no per-user
    site directory contributes to its start-up (wave-D S-C1)."""
    env = dict(os.environ)
    env["PYTHONPYCACHEPREFIX"] = os.path.join(scratch, "pyc")
    env["PYTHONNOUSERSITE"] = "1"
    return env


def _run_checker(checker, cfg, cwd, mypypath, args, strip, scratch):
    """One checker pass. Returns findings as ``(file, line, detail)``."""
    if not args:
        return []
    env = _root_tool_env(scratch)
    env["MYPYPATH"] = mypypath
    env["MYPY_CACHE_DIR"] = os.path.join(scratch, "mypy-cache")
    proc = subprocess.run([checker, "--config-file", cfg] + list(args),
                          cwd=cwd, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=900)
    text = proc.stdout.decode("utf-8", "replace")
    out = []
    for line in text.splitlines():
        m = _MYPY_LINE.match(line.strip())
        if not m:
            continue
        rel = m.group("file")
        if strip and rel.startswith(strip):
            rel = rel[len(strip):]
        out.append((rel, int(m.group("line") or 0), m.group("detail").strip()))
    # Exit 0 is "clean", exit 1 is "these findings". Anything else means the
    # checker stopped early -- "errors prevented further checking" -- and every
    # file it had not reached yet is unexamined. That has to be a finding in
    # its own right and not merely the handful of lines it managed to print
    # first: reporting nothing and exiting 0 is the answer a clean tree gets,
    # and the two must never look alike (INSTRUCTIONS trap 3).
    if proc.returncode not in (0, 1):
        out.append(("", 0, "type checker exited %d -- it stopped before "
                    "examining the whole scope, so this count is a floor, not "
                    "a total: %s"
                    % (proc.returncode, text.strip()[-400:] or "<no output>")))
    if proc.returncode == 0 and out:
        out.append(("", 0, "type checker exited 0 but reported %d error line(s)"
                    % len(out)))
    return out


def check_types_clean(tree):
    """The client core type-checks clean under strict static checking.

    A tree-level check rather than a per-unit one: a type error is a statement
    about a file *and everything it imports*, which no single-unit check can
    make. It runs on a copy so the scored tree is never written to.

    The scope is ``TYPED_LIB``, not ``OWN_SURFACE``, and the reason is measured
    rather than aesthetic. This rung also forbids `Any` inside an annotation
    and forbids suppression comments, and the plugin layer cannot satisfy all
    three at once. ``BaseCommand.run_request`` is abstract and pass-through --
    ``run`` hands it whatever Sublime passed the command -- and it has 117
    overrides, each with its own concrete parameters. Annotate the base
    ``*args: object, **kwargs: object`` and every one of those 117 becomes an
    ``[override]`` error; the only spelling a checker accepts is the
    ``(*args: Any, **kwargs: Any)`` special case, which ``no-any-in-
    annotations`` forbids, or a ``# type: ignore``, which ``no-suppressions``
    forbids. Measured on a two-class fixture before it was written down: the
    ``object`` spelling reports the override errors, the ``Any`` spelling
    reports nothing. So the plugin layer is required to *declare* its types
    and the client core is required to declare types that *agree with each
    other*. Both are stated in the instruction; neither asks for something the
    rung's other rules rule out.

    One root, therefore, not two: ``lib/`` is what Sublime puts on
    ``sys.path``, so the client core is named from ``lib/`` down and nothing
    above it is imported by name. The plugin root's synthetic ``__init__.py``
    went with the plugin pass.
    """
    checker = _checker()
    if checker is None:
        return [("", 0, "no type checker in this image (looked at $MYPY_BIN "
                 "and $TYPECHECK_VENV/bin/mypy). The gate could not look, "
                 "which is not the same answer as nothing to find.")]
    work = tempfile.mkdtemp(prefix="typecheck-")
    try:
        root = os.path.join(work, "plugin")
        shutil.copytree(tree, root, symlinks=True,
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc",
                                                      "*.pyo"))
        cfg = os.path.join(work, "mypy.ini")
        with io.open(cfg, "w", encoding="utf-8") as fh:
            fh.write(TYPE_CHECK_CONFIG % (_target_version(),))
        return sorted(set(_run_checker(
            checker, cfg, root, "lib", _scoped(root, TYPED_LIB), None, work)))
    finally:
        shutil.rmtree(work, ignore_errors=True)


# ------------------------------------------------- idioms (rung 07) ----
#
# The task line's last migrated-ness gates: ruff `UP`/`PYI`/`B`/`ASYNC` at
# zero and `pyupgrade` idempotent, over the whole tree, suppressions counted.
# Added by migration review round 2 (2026-10-05); before that no rung carried
# them. They are tree checks rather than unit checks for the same reason as
# `types-check-clean`: the instrument is a separate binary, and it is the
# binary's verdict, not a re-implementation of it, that is the definition.
#
# The instruments live in the step-07 image's checker virtualenv, beside mypy
# and for the same reason (rule 6): the candidate's interpreter must not be
# able to import them. Their configuration is entirely on the command line --
# `--isolated` so no `ruff.toml` / `pyproject.toml` the candidate writes is
# read, `--ignore-noqa` so a `# noqa` silences nothing (and
# `no-lint-suppressions` below counts the comment as well), and every file is
# passed explicitly so neither ruff's default excludes nor a `.gitignore` can
# take one out of scope. A file the tool cannot read -- it is not UTF-8, it
# does not parse -- is a finding of every one of these gates, not a file
# they skip.

def _idiom_tool(name):
    """``name`` from the checker virtualenv (or ``$<NAME>_BIN``), or ``None``."""
    cand = os.environ.get(name.upper() + "_BIN")
    if cand and os.path.exists(cand):
        return cand
    cand = os.path.join(os.environ.get("TYPECHECK_VENV", "/opt/typecheck"),
                        "bin", name)
    return cand if os.path.exists(cand) else None


def _source_files(tree):
    out = []
    for dirpath, dirnames, filenames in os.walk(tree):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for name in filenames:
            if name.endswith((".py", ".pyi")):
                out.append(os.path.relpath(os.path.join(dirpath, name), tree))
    return sorted(out)


def _ruff_target():
    return "py" + _target_version().replace(".", "")


def ruff_check(selector):
    """A tree check: ruff ``selector`` at zero over every source file."""
    def check(tree):
        ruff = _idiom_tool("ruff")
        if ruff is None:
            return [("", 0, "no ruff in this image (looked at $RUFF_BIN and "
                     "$TYPECHECK_VENV/bin/ruff). The gate could not look, "
                     "which is not the same answer as nothing to find.")]
        files = _source_files(tree)
        if not files:
            return []
        proc = subprocess.run(
            [ruff, "check", "--isolated", "--no-cache", "--ignore-noqa",
             "--no-fix", "--target-version", _ruff_target(),
             "--select", selector, "--output-format", "json", "--"] + files,
            cwd=tree, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=900)
        out = []
        try:
            rows = json.loads(proc.stdout.decode("utf-8", "replace") or "[]")
        except ValueError:
            rows = None
        for r in rows or ():
            loc = r.get("location") or {}
            rel = os.path.relpath(r.get("filename") or "", tree)
            out.append((rel, int(loc.get("row") or 0), "%s %s" % (
                r.get("code") or "", (r.get("message") or "")[:160])))
        err = proc.stderr.decode("utf-8", "replace")
        for line in err.splitlines():
            # "warning: Failed to lint x.py: stream did not contain valid
            # UTF-8" -- the file was not examined, so it is not clean.
            if "Failed to lint" in line or line.startswith("error"):
                out.append(("", 0, line.strip()[:240]))
        if rows is None or proc.returncode not in (0, 1):
            out.append(("", 0, "ruff exited %d without a readable report: %s"
                        % (proc.returncode, err.strip()[-400:] or
                           "<no output>")))
        return out
    return check


def check_modernizer_idempotent(tree):
    """``pyupgrade --py3XX-plus`` would rewrite nothing.

    Run on a throwaway copy, because the tool rewrites in place; the findings
    are the files whose bytes it changed and the files it refused to read.
    """
    tool = _idiom_tool("pyupgrade")
    if tool is None:
        return [("", 0, "no pyupgrade in this image (looked at "
                 "$PYUPGRADE_BIN and $TYPECHECK_VENV/bin/pyupgrade). The gate "
                 "could not look, which is not the same answer as nothing to "
                 "find.")]
    files = [f for f in _source_files(tree) if f.endswith(".py")]
    if not files:
        return []
    work = tempfile.mkdtemp(prefix="pyupgrade-")
    try:
        root = os.path.join(work, "tree")
        shutil.copytree(tree, root, symlinks=True,
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc",
                                                      "*.pyo"))
        before = {}
        for rel in files:
            with open(os.path.join(root, rel), "rb") as fh:
                before[rel] = fh.read()
        flag = "--py%s-plus" % _target_version().replace(".", "")
        proc = subprocess.run([tool, flag, "--"] + files, cwd=root,
                              env=_root_tool_env(work),
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=900)
        text = proc.stdout.decode("utf-8", "replace")
        out = []
        for rel in files:
            with open(os.path.join(root, rel), "rb") as fh:
                if fh.read() != before[rel]:
                    out.append((rel, 0, "a %s modernizer rewrites this file"
                                % flag))
        for line in text.splitlines():
            if "non-utf-8" in line or "not supported" in line \
                    or "Traceback" in line:
                out.append(("", 0, line.strip()[:240]))
        if proc.returncode not in (0, 1):
            out.append(("", 0, "pyupgrade exited %d: %s"
                        % (proc.returncode, text.strip()[-400:])))
        return out
    finally:
        shutil.rmtree(work, ignore_errors=True)


_NOQA = re.compile(r"\bnoqa\b", re.I)


def check_lint_suppressions(unit):
    """No comment tells a linter to look away: ``# noqa`` in any spelling,
    per line or per file (``# flake8: noqa``, ``# ruff: noqa``). Read from
    COMMENT tokens, so the word inside a string literal does not count."""
    for tok in unit.tokens:
        if tok.type == tokenize.COMMENT and _NOQA.search(tok.string):
            yield tok.start[0], tok.string.strip()[:60]

GATES = (
    Gate("no-legacy-future-imports",
         "No module imports `absolute_import`, `unicode_literals`, "
         "`print_function`, `division` or `with_statement` from `__future__`.",
         check=check_legacy_future),
    Gate("no-commented-out-py2",
         "No retired branch is left behind as a block of commented-out code "
         "or parked in a free-standing string: the dead arm is deleted.",
         check=check_commented_out_py2),
    Gate("no-six",
         "Nothing imports `six`, by any spelling, and no copy of it remains "
         "in the tree.",
         check=check_six),
    Gate("six-deleted",
         "The three vendored copies of `six.py` are deleted.",
         tree_check=paths_absent(("lib/six.py", "**/packages/six.py"))),
    Gate("no-py2-builtins",
         "No reference to a name that exists only on Python 2 "
         "(`basestring`, `unicode`, `xrange`, `long`, `unichr`, ...).",
         check=check_py2_builtins),
    Gate("no-py2-iteration",
         "No `.iteritems()` / `.itervalues()` / `.iterkeys()` / "
         "`.iterlists()` / `.has_key()`, and no class defining `next` "
         "alongside `__next__`.",
         check=check_py2_methods),
    Gate("no-version-branches",
         "No `sys.version_info` test in any spelling and no interpreter-version "
         "flag (`PY2`, `PY3`, `is_py2`, `is_py3`).",
         check=check_version_branches),
    Gate("no-collections-abc-aliases",
         "Abstract base classes are imported from `collections.abc`.",
         check=check_collections_abc),
    Gate("no-fractions-gcd",
         "`gcd` comes from `math`.",
         check=check_fractions_gcd),
    Gate("no-u-prefix",
         "No `u'...'` string prefix.",
         check=check_u_prefix),
    Gate("ordered-dict-backport-deleted",
         "The `ordered_dict` backport is deleted.",
         tree_check=paths_absent(("**/ordered_dict.py",))),

    Gate("no-dead-stdlib-312",
         "Nothing imports `imp`, `distutils`, `asynchat`, `asyncore` or "
         "`smtpd`: Python 3.12 removed all five.",
         check=check_dead_312),
    Gate("no-dead-stdlib-313",
         "Nothing imports a module Python 3.13 removed -- the nineteen PEP "
         "594 retired plus `lib2to3`; `cgi`, at module scope in "
         "`requests/utils.py`, is the only one this tree reaches for.",
         check=check_dead_313),
    Gate("deprecated-surface-marked",
         "Each of the six module-level definitions the tree documents as "
         "deprecated (`get_host` and `iter_fields` in both copies of urllib3, "
         "`get_encodings_from_content` and `get_unicode_from_response` in "
         "`requests/utils.py`) carries `@warnings.deprecated`. Methods of the "
         "same name are not in scope.",
         check=check_deprecated_surface_marked),
    Gate("no-adhoc-deprecation-warn",
         "No `warnings.warn(..., DeprecationWarning)` anywhere: a deprecation "
         "is announced with the decorator, not by hand. The library's own "
         "contract warnings are untouched.",
         check=check_adhoc_deprecation_warn),
    Gate("no-deprecated-surface-callers",
         "Nothing inside the tree calls one of those six: `parse_url` "
         "replaces `get_host`, `iter_field_objects` replaces `iter_fields`. "
         "`self.get_host()` on `requests.cookies.MockRequest` is a different "
         "function and is not flagged.",
         check=check_deprecated_surface_callers),
    Gate("no-logger-warn-alias",
         "No `.warn(...)` call on a logger: `Logger.warn` has been a "
         "deprecated alias of `Logger.warning` since 3.3 and raises a "
         "DeprecationWarning from inside this tree. `warnings.warn` is a "
         "different function, is not deprecated, and is not flagged.",
         check=check_logger_warn_alias),

    Gate("no-dead-ssl-names",
         "No reference to `ssl.wrap_socket`, and no `match_hostname`, "
         "`CertificateError` or `PROTOCOL_SSLv*` by any spelling. "
         "`SSLContext.wrap_socket` is the replacement and is not "
         "flagged.",
         check=check_ssl_dead_names),
    Gate("ssl-match-hostname-deleted",
         "The vendored `ssl_match_hostname` backport is deleted from both "
         "copies of urllib3: its directory is gone, and neither of the names "
         "it defined (`match_hostname`, `CertificateError`) is redefined "
         "anywhere else in the tree.",
         tree_check=paths_absent(("**/ssl_match_hostname/*",)),
         check=check_ssl_revendored),
    Gate("no-naive-utc",
         "No `datetime.utcnow()` or `datetime.utcfromtimestamp()`.",
         check=check_naive_utc),
    Gate("compiles-without-warnings",
         "Every module compiles with no SyntaxWarning and no "
         "DeprecationWarning from the compiler -- which is where invalid "
         "string escapes and PEP 765 show up.",
         check=check_compiles_clean),

    Gate("future-annotations-present",
         "Every module in the typed scope that carries an annotation also "
         "carries `from __future__ import annotations`.",
         check=check_future_annotations_missing, scope=OWN_SURFACE),
    Gate("no-any-in-annotations",
         "No `Any` in an annotation on the repo's own surface.",
         check=check_any_annotations, scope=OWN_SURFACE),
    Gate("no-suppressions",
         "No `# type: ignore`, `# noqa`, `# mypy:` or `# pragma: no cover`, "
         "no `typing.no_type_check`, and no code hidden from the checker "
         "behind `TYPE_CHECKING` on the repo's own surface.",
         check=check_suppressions, scope=OWN_SURFACE),
    Gate("public-surface-annotated",
         "Every public function and method on the repo's own surface is "
         "fully annotated.",
         check=check_unannotated_public_defs, scope=OWN_SURFACE),
    Gate("public-surface-census",
         "Every public function and method the repo started with is still "
         "there: annotating a surface is not the same as shrinking one.",
         tree_check=check_public_surface_census),
    Gate("annotations-match-defaults",
         "Every parameter annotation on the repo's own surface is true of the "
         "default written beside it: a default the source spells out is a "
         "type the annotation has to name.",
         check=check_parameter_defaults_agree, scope=OWN_SURFACE),
    Gate("return-annotations-match-bodies",
         "Every return annotation on the repo's own surface is true of the "
         "body it sits on: `-> None` does not return a value, a generator is "
         "annotated as one, a concrete non-optional return type has a path "
         "that returns, and a returned literal is of the type declared.",
         check=check_return_annotation_agrees, scope=OWN_SURFACE),
    Gate("types-check-clean",
         "The Elasticsearch client core (`lib/`, the scope the instruction "
         "lists) is internally consistent under strict static type checking: "
         "no declared type contradicts another, and nothing the checker needs "
         "is left to inference.",
         tree_check=check_types_clean),

    Gate("no-finally-control-flow",
         "No `return`, `break` or `continue` leaves a `finally:` block "
         "(PEP 765).",
         check=check_finally_control_flow),
    Gate("no-future-annotations",
         "No `from __future__ import annotations`: PEP 649 evaluates "
         "annotations lazily without it.",
         check=check_future_except_annotations),
    Gate("no-quoted-annotations",
         "No annotation is written as a string literal.",
         check=check_string_annotations),
    Gate("no-type-checking-imports",
         "No module-level `if TYPE_CHECKING:` import block: a name an "
         "annotation is built from has to exist at run time, because PEP 649 "
         "builds it there.",
         check=check_type_checking_imports),
    Gate("annotation-names-bound",
         "Every name an annotation is built from is bound by something the "
         "module actually runs: the imports moved out of the blocks, they "
         "were not deleted along with them.",
         check=check_annotation_names_bound),

    # One ruff selector per gate (the task line's four), every source file in
    # the tree, scored at zero. See the "idioms (rung 07)" block above.
    Gate("no-legacy-idioms",
         "No spelling survives that only an older Python needed: ruff's `UP` "
         "class at zero for the rung's target version.",
         tree_check=ruff_check("UP")),
    Gate("no-bug-prone-idioms",
         "None of the bug-prone patterns survives: ruff's `B` class at zero.",
         tree_check=ruff_check("B")),
    Gate("typing-idioms",
         "Typing constructs are spelled the current way: ruff's `PYI` class "
         "at zero.",
         tree_check=ruff_check("PYI")),
    Gate("async-idioms",
         "No blocking call or busy-wait inside a coroutine: ruff's `ASYNC` "
         "class at zero.",
         tree_check=ruff_check("ASYNC")),
    Gate("modernizer-idempotent",
         "A modernizer targeting the rung's Python version has nothing left "
         "to rewrite: `pyupgrade --py3XX-plus` changes no file.",
         tree_check=check_modernizer_idempotent),
    Gate("no-lint-suppressions",
         "No comment tells a linter to look away -- `# noqa` in any spelling, "
         "per line or per file -- anywhere in the tree.",
         check=check_lint_suppressions),
)

GATES_BY_ID = dict((g.gid, g) for g in GATES)

#: ``(step, adds, drops)``. A rung runs its own gates plus every earlier rung's
#: that no later-or-equal rung has dropped: a rung that reintroduces what an
#: earlier rung removed has not preserved the earlier rung's work, so the whole
#: set travels forward. ``drops`` exists for one case -- step 05 requires
#: ``from __future__ import annotations`` and step 07, where PEP 649 makes it
#: redundant, requires it gone -- and carrying a gate past the rung that
#: supersedes it would make that rung unpassable.
#:
#: Steps 02 and 06 add nothing here on purpose. Exception-group aggregation and
#: free-threading safety are not spellings; they are what the code *does* under
#: a retry storm and under 12 threads with the GIL off, and they are scored by
#: the behaviour suite and the parallel run. A static gate invented to give
#: those rungs a row in this table would be a gate a codemod could pass.
STEPS = (
    ("01-py310-drop-python2", (
        "no-legacy-future-imports", "no-commented-out-py2",
        "no-six", "six-deleted",
        "no-py2-builtins", "no-py2-iteration", "no-version-branches",
        "no-collections-abc-aliases", "no-fractions-gcd", "no-u-prefix",
        "ordered-dict-backport-deleted"), ()),
    ("02-py311-exception-groups", (), ()),
    ("03-py312-tls-and-retired-stdlib", (
        "no-dead-stdlib-312", "no-dead-ssl-names",
        "ssl-match-hostname-deleted", "no-naive-utc",
        "compiles-without-warnings"), ()),
    ("04-py313-dead-batteries", (
        "no-dead-stdlib-313", "deprecated-surface-marked",
        "no-adhoc-deprecation-warn", "no-deprecated-surface-callers",
        "no-logger-warn-alias"), ()),
    ("05-typed-client-surface", (
        "public-surface-annotated", "future-annotations-present",
        "no-any-in-annotations", "no-suppressions", "public-surface-census",
        "annotations-match-defaults",
        "return-annotations-match-bodies", "types-check-clean"), ()),
    ("06-py313t-free-threading", (), ()),
    ("07-py314-deferred-annotations", (
        "no-finally-control-flow", "no-future-annotations",
        "no-quoted-annotations", "no-type-checking-imports",
        "annotation-names-bound",
        "no-legacy-idioms", "no-bug-prone-idioms", "typing-idioms",
        "modernizer-idempotent", "async-idioms", "no-lint-suppressions"),
     ("future-annotations-present",)),
)
STEP_IDS = [s for s, _, _ in STEPS]


def gates_for(step_id):
    """Every gate in force at ``step_id``: its own, plus every earlier rung's
    that has not been dropped by ``step_id`` or by a rung before it."""
    if step_id not in STEP_IDS:
        raise SystemExit("unknown step %r; known: %s"
                         % (step_id, ", ".join(STEP_IDS)))
    cut = STEP_IDS.index(step_id)
    dropped = set()
    for _, _, drops in STEPS[:cut + 1]:
        dropped.update(drops)
    out = []
    for sid, adds, _ in STEPS[:cut + 1]:
        out += [GATES_BY_ID[g] for g in adds if g not in dropped]
    return out


#: Gates that are zero on the baseline **and** zero on the correct answer, so
#: a tree that has done nothing passes them. Section 2.2 of NOTES-authoring.md
#: names them and says why they exist: ``u''`` is legal again on 3.3+ and means
#: nothing, so a modernising edit can reintroduce one for free, and the obvious
#: way to satisfy step 05 on 3.13 is a quoted forward reference. Neither is a
#: construct the baseline has. They are worth checking and worth nothing.
#:
#: What none of them is, is a *prediction* about the codemods: swept with
#: tokenize over all four tool trees (`lib2to3`, `pyupgrade`, the two composed,
#: and `python-modernize`), the number of ``u''`` prefixes introduced is zero.
#: Section 7.14 has the sweep. The reason these are guards is the measured
#: arithmetic below, not a guess about what a tool would do.
#:
#: Credited, each would hand a do-nothing tree a tenth of step 01's gate family
#: and a third of step 07's -- measured, not feared: the first scored run of
#: the untouched baseline reported ``new gates 10%`` at step 01 and every one
#: of those ten points was ``no-u-prefix``. They are scored as penalties
#: instead: passing earns nothing, failing costs.
#:
#: ``no-commented-out-py2`` joined them in round 5 for the same reason, and the
#: measurement is section 7.14: the baseline carries no commented-out branch,
#: so crediting the check moved the do-nothing tree from 0.021206 to 0.091206
#: -- seven free points for a file nobody had touched. It is an anti-gaming
#: check, and an anti-gaming check belongs where gaming costs and abstaining
#: earns nothing.
#: ``compiles-without-warnings`` joined them at step 03 for the *other* half of
#: the same rule. It is not a check a do-nothing tree passes -- the pinned tree
#: has 26 invalid escapes across 9 files and fails it outright. It is a check
#: the **platform's automated migrator** passes: `pyupgrade --py312-plus` has a
#: fixer that re-prefixes exactly those literals, and measured on step 03's
#: predecessor it clears this gate and no other. Credited as one of step 03's
#: five own gates that is 0.0933 of the step handed to a tool -- the codemod
#: tree scored 0.393333 against item 4.3's ~30% cap, and with family 1 pinned
#: at 0.30 by NARROW_F1 there is no weighting that absorbs it. Both figures
#: belong to the weighting in force when they were measured, gates two thirds
#: of family 2 (0.4667 x 1/5 = 0.0933); the behaviour/gates split was flipped
#: later in the same pass, and re-measured under the current one third
#: (0.2333 x 1/5 = 0.0467) the same five-gate variant pays the codemod tree
#: 0.346667 -- smaller, still over the cap, same conclusion. So the same
#: remedy applies for the same reason: a check a tool can clear cannot be
#: allowed to pay the tool, and it is scored as a penalty instead. The codemod
#: tree then scores 0.300000 and the do-nothing tree 0.250000, which is the
#: right order and the right size -- avoiding the penalty is worth exactly what
#: running the fixer is worth. Section 9.3 has both calibrations.
#:
#: The requirement does not go away and is not softened: the gate still runs at
#: every step from 03 on, `instruction.md` still states it, the golden still
#: satisfies it, and leaving or reintroducing a SyntaxWarning still costs.
#:
#: ``async-idioms`` and ``no-lint-suppressions`` joined them in review round 2
#: (2026-10-05), with the rest of the task line's idiom gates. The first has
#: nothing of its own to find -- the tree has no coroutine for ``ASYNC`` to
#: look at -- and the step-06 tree fails it only because one vendored file is
#: not UTF-8, which re-encoding that file clears; crediting it would pay for
#: that one edit as if it were a share of the rung's bar. The
#: second is the suppression gate the task line requires beside every
#: linter-at-zero gate; the ruff gates already run with ``# noqa`` ignored, so
#: what it adds is the property itself, and deleting thirteen comment lines is
#: not a share of the rung's bar but a blemish to keep off it. Both are stated
#: in the step-07 instruction and both cost when they fire.
GUARDS = frozenset((
    "no-u-prefix", "no-quoted-annotations", "no-commented-out-py2",
    "compiles-without-warnings", "async-idioms", "no-lint-suppressions"))

#: Gates that are one requirement wearing six names, scored as a conjunction.
#:
#: Five of the six are trivially true of a tree with no annotations: a tree
#: with none has no ``Any`` in one, needs no ``__future__`` import for one,
#: suppresses no finding about one, contradicts no default with one, and has
#: not deleted anything. Scored as six equal gates, step 04's finished tree --
#: which has no annotations at all -- takes 5/6 of the family that is supposed
#: to measure step 05's leap, and rule 8's floor goes from 0.45 to 0.91.
#:
#: Grouping is not just floor arithmetic, it closes three gaming vectors, each
#: measured before the member that closes it was written. Annotating all 776
#: signatures ``Any`` passes ``public-surface-annotated`` outright; spelling
#: the same empty claim ``object`` passed the whole family and scored 0.533,
#: the same as doing the hard 17-file third honestly; deleting or
#: ``_``-prefixing a function passes by making it nobody's business. As
#: separate gates each trades most of the family for a small penalty; as a
#: conjunction each scores zero, which is what "the surface is fully
#: annotated" was always supposed to mean -- that it is annotated, that the
#: annotations say something, and that it is still the surface.
GROUPS = (
    # The description does not restate how the annotations are *stored*, even
    # though `future-annotations-present` is a member: that member is dropped
    # at rung 07, where the future import is the thing being removed, and a
    # group description that outlived its member would say something false
    # about the finished tree. Each member reports its own `what`.
    ("typed-own-surface",
     "The repo's own surface -- all of the surface it was handed -- is fully "
     "annotated, with no `Any` and no suppression.",
     ("public-surface-annotated", "future-annotations-present",
      "no-any-in-annotations", "no-suppressions", "public-surface-census",
      "annotations-match-defaults")),
)


class Check(object):
    """One scored unit: a single gate, or a conjunctive bundle of them.

    A bundle passes only if every member passes, and reports every member's
    findings, so the report still says *which* half of a group failed.
    """

    def __init__(self, cid, what, gates):
        self.cid = cid
        self.what = what
        self.gates = gates

    @property
    def members(self):
        return [g.gid for g in self.gates]

    def run(self, tree, cache):
        out = []
        for g in self.gates:
            for row in g.run(tree, cache):
                row = dict(row)
                row["gate"] = g.gid
                out.append(row)
        return out


def checks_for(step_id):
    """``(inherited, new, guards)`` for ``step_id``, as three lists of checks.

    The scorer needs these apart, not merged, and the reason is rule 8's floor.
    Step k-1's finished tree passes every gate step k *inherited* -- that is
    what inheriting means -- so a gates family scored over the cumulative set
    hands the predecessor most of the step. At step 05 the cumulative set is 20
    gates of which 16 are inherited: the predecessor would score 0.8 on the
    family that is supposed to measure the leap it has not made. Split, the
    inherited gates are a regression guard carrying a few percent and the new
    ones carry the step.
    """
    everything = gates_for(step_id)
    in_force = dict((g.gid, g) for g in everything)
    cut = STEP_IDS.index(step_id)
    added = set(STEPS[cut][1])

    grouped = {}
    for gid, what, members in GROUPS:
        present = [in_force[m] for m in members if m in in_force]
        if present:
            grouped[gid] = Check(gid, what, present)
    member_of = dict((m, gid) for gid, _, members in GROUPS for m in members)

    inherited, new, guards, seen = [], [], [], set()
    for g in everything:
        if g.gid in GUARDS:
            guards.append(Check(g.gid, g.what, [g]))
            continue
        gid = member_of.get(g.gid)
        check = grouped[gid] if gid else Check(g.gid, g.what, [g])
        if check.cid in seen:
            continue
        seen.add(check.cid)
        # A group is this step's own if *any* member arrived at this step.
        own = any(m in added for m in check.members)
        (new if own else inherited).append(check)
    return inherited, new, guards


def run(tree, step_id, stream=sys.stdout):
    cache, broken = units(tree)
    report = {"tree": os.path.abspath(tree), "step": step_id,
              "n_files": len(cache), "gates": [],
              "unparsable": [{"file": f, "line": n, "detail": d}
                             for f, n, d in broken]}
    total = len(broken)
    for gate in gates_for(step_id):
        found = gate.run(tree, cache)
        total += len(found)
        report["gates"].append({"id": gate.gid, "what": gate.what,
                                "n": len(found), "findings": found})
    report["n_findings"] = total

    print("%d .py files, %d gates in force at %s"
          % (len(cache), len(report["gates"]), step_id), file=stream)
    if broken:
        print("  %-34s %5d  <- the gate could not look" % ("UNPARSABLE",
                                                           len(broken)),
              file=stream)
    for g in report["gates"]:
        print("  %-34s %5d" % (g["id"], g["n"]), file=stream)
    print("total findings: %d" % total, file=stream)
    return report


def census(tree, stream=sys.stdout):
    """The whole-repo construct count -- NOTES-authoring.md section 2.2."""
    cache, broken = units(tree)
    rows = []
    for gate in GATES:
        found = gate.run(tree, cache)
        rows.append((gate.gid, len(found),
                     len(set(f["file"] for f in found))))
    print("%d .py files under %s" % (len(cache), os.path.abspath(tree)),
          file=stream)
    print("%-34s %6s %6s" % ("construct family", "hits", "files"), file=stream)
    for gid, hits, files in rows:
        print("%-34s %6d %6d" % (gid, hits, files), file=stream)
    if broken:
        print("\n%d files do not parse:" % len(broken), file=stream)
        for rel, line, detail in broken:
            print("  %s:%s %s" % (rel, line, detail), file=stream)
    return rows


# ------------------------------------------------------------ selftest ----
#
# INSTRUCTIONS trap 6: verify the instrument before the finding. Every gate
# gets two fixtures -- one file that must make it fire and one that must not.
# The clean fixture is not an empty file: it is the same construct spelled in a
# comment, a docstring and a string literal, which is exactly what a grep-based
# gate would false-positive on.

DIRTY = {
    "no-legacy-future-imports": "from __future__ import print_function\n",
    "no-commented-out-py2": ("x = 1\n"
                            "# if sys.version_info[0] == 2:\n"
                            "#     text = unicode\n"),
    "future-annotations-present": "def f(x: int) -> int:\n    return x\n",
    "no-six": "import six\nfrom .packages import six as s\n",
    "no-py2-builtins": "x = unicode('a')\ny = xrange(3)\n",
    "no-py2-iteration": "d = {}\nfor k in d.iteritems():\n    pass\n",
    "no-version-branches": "import sys\nif sys.version_info[0] == 2:\n    PY2 = 1\n",
    "no-collections-abc-aliases": "from collections import Mapping\n",
    "no-fractions-gcd": "from fractions import gcd\n",
    "no-u-prefix": "s = u'x'\n",
    "no-dead-stdlib-312": "from imp import reload\nimport distutils.spawn\n",
    "no-dead-stdlib-313": "import cgi\nimport telnetlib\n",
    "deprecated-surface-marked": ("def get_host(url):\n"
                                  "    \"\"\"Deprecated. Use parse_url.\"\"\"\n"
                                  "    return parse_url(url)\n"),
    "no-adhoc-deprecation-warn": ("import warnings\n"
                                  "warnings.warn('gone in 3.0', "
                                  "DeprecationWarning)\n"),
    "no-deprecated-surface-callers": ("from .util.url import get_host\n"
                                      "scheme, host, port = get_host(url)\n"),
    "no-logger-warn-alias": ("import logging\n"
                             "log = logging.getLogger(__name__)\n"
                             "log.warn('queue is full')\n"),
    "no-dead-ssl-names": "import ssl\nssl.wrap_socket(s)\nssl.PROTOCOL_SSLv3\n",
    "no-naive-utc": "import datetime\ndatetime.datetime.utcnow()\n",
    "compiles-without-warnings": "s = '\\d+'\n",
    "no-any-in-annotations": "from typing import Any\ndef f(x: Any) -> None:\n    pass\n",
    "no-suppressions": "x = 1  # type: ignore\n",
    "public-surface-annotated": "def f(a):\n    return a\n",
    "annotations-match-defaults":
        "def f(timeout: object = 10) -> None:\n    pass\n",
    "return-annotations-match-bodies":
        "def f(a: int) -> None:\n    return a\n",
    "no-finally-control-flow": "def f():\n    try:\n        pass\n    finally:\n        return 1\n",
    "no-future-annotations": "from __future__ import annotations\n",
    "no-quoted-annotations": "def f(x: 'int') -> 'str':\n    return ''\n",
    "no-type-checking-imports": ("from typing import TYPE_CHECKING\n\n"
                                 "if TYPE_CHECKING:\n"
                                 "    from collections.abc import Callable\n"),
    "annotation-names-bound": "def f(c: Elasticsearch) -> None:\n    pass\n",
    "no-lint-suppressions": "x = 1  # noqa: E501\n",
}

#: A gate that scores two different constructs needs two fixtures, or the
#: easier one masks the other. ``no-py2-iteration`` was the case in point:
#: its fixture held ``d.iteritems()`` *and* ``it.next()``, so when the round-3
#: fix removed ``.next`` from the table the fixture went on passing on the
#: strength of the ``iteritems`` line alone, and the selftest never noticed
#: the gate had gone blind to half of instruction item 3. Found by the round-4
#: review. Each entry here is one additional fixture that must make its gate
#: fire *on its own*. Section 7.11.

DIRTY_EXTRA = {
    # Four names behind `deprecated-surface-marked` and two decorator
    # spellings behind the thing that silences it, so each name gets its own
    # fixture (section 7.11) and the "wrong decorator" shape gets one too: a
    # definition that carries *a* decorator is the way a gate written as
    # "has decorators" instead of "has this decorator" goes quietly blind.
    "deprecated-surface-marked": [
        ("iter-fields",
         "def iter_fields(fields):\n"
         "    \"\"\".. deprecated:: 1.6\"\"\"\n"
         "    return iter(fields)\n"),
        ("get-encodings-from-content",
         "def get_encodings_from_content(content):\n"
         "    return []\n"),
        ("get-unicode-from-response",
         "def get_unicode_from_response(r):\n"
         "    return r.text\n"),
        ("some-other-decorator",
         "import functools\n\n\n"
         "@functools.lru_cache(maxsize=None)\n"
         "def get_host(url):\n"
         "    return parse_url(url)\n"),
        # Module scope is not `tree.body`. Each of these three binds a module
        # global and each was invisible while the walk stopped at the top
        # level -- a one-line dodge that made the gate pass with no decorator.
        # Rung 04 round 1.
        ("parked-under-try",
         "try:\n"
         "    def get_host(url):\n"
         "        return parse_url(url)\n"
         "except ImportError:\n"
         "    pass\n"),
        ("parked-under-if",
         "if True:\n"
         "    def iter_fields(fields):\n"
         "        return iter(fields)\n"),
        ("parked-under-with",
         "import contextlib\n\n\n"
         "with contextlib.suppress(ImportError):\n"
         "    def get_unicode_from_response(r):\n"
         "        return r.text\n"),
        ("parked-in-an-except-handler",
         "try:\n"
         "    from ._fast import get_encodings_from_content\n"
         "except ImportError:\n"
         "    def get_encodings_from_content(content):\n"
         "        return []\n"),
    ],
    # `Any` is a name, so it can be renamed, and an annotation can be a
    # string, so it can be quoted. Every row here is a file annotated
    # entirely in `Any` that the literal-spelling version of this check
    # reported nothing about. Section 11.1.
    "no-any-in-annotations": [
        ("renamed-on-import",
         "from typing import Any as J\n\n\n"
         "def f(x: J) -> None:\n    pass\n"),
        ("renamed-from-typing-extensions",
         "from typing_extensions import Any as J\n\n\n"
         "def f(x: J) -> None:\n    pass\n"),
        ("aliased-by-assignment",
         "from typing import Any\n\nJ = Any\n\n\n"
         "def f(x: J) -> None:\n    pass\n"),
        ("alias-of-an-alias",
         "from typing import Any\n\nJ = Any\nK = J\n\n\n"
         "def f(x: K) -> None:\n    pass\n"),
        ("alias-containing-any",
         "from typing import Any\n\nJson = dict[str, Any]\n\n\n"
         "def f(x: Json) -> None:\n    pass\n"),
        ("alias-declared-with-typealias",
         "from typing import Any, TypeAlias\n\nJ: TypeAlias = Any\n\n\n"
         "def f(x: J) -> None:\n    pass\n"),
        ("quoted",
         "from typing import Any\n\n\n"
         "def f(x: 'Any') -> None:\n    pass\n"),
        ("in-a-return",
         "from typing import Any\n\n\n"
         "def f(x: int) -> Any:\n    return x\n"),
        ("in-a-subscript",
         "from typing import Any\n\n\n"
         "def f(x: dict[str, Any]) -> None:\n    pass\n"),
        ("qualified",
         "import typing\n\n\n"
         "def f(x: typing.Any) -> None:\n    pass\n"),
        ("annotated-assignment",
         "from typing import Any\n\nx: Any = 1\n"),
    ],
    # Four rules, plus the three places a rule can be dodged by spelling.
    # The base fixture is the `-> None` one; each of the others is a
    # different disagreement between a declaration and the body under it, and
    # a gate that reads only the easy one is a gate that lets a tree annotate
    # 577 plugin signatures with confident nonsense. Section 11.1.
    "return-annotations-match-bodies": [
        ("no-return-annotation",
         "def f(a: int):\n    return a\n"),
        ("generator-annotated-as-a-value",
         "def f() -> list:\n    yield 1\n"),
        ("yield-from-annotated-as-none",
         "def f() -> None:\n    yield from []\n"),
        ("concrete-return-that-never-returns",
         "def f(a: int) -> str:\n    a += 1\n"),
        # The walk is `tree.body` plus class bodies, the same two levels
        # `public-surface-annotated` reads. A method-only fixture is what
        # keeps the second level in it.
        ("method-in-a-class",
         "class C:\n    def m(self) -> None:\n        return 1\n"),
        # `_ann_head` unwraps a subscript, an attribute and a string before
        # it reads a name. One fixture each, because each unwrapping is a
        # separate line that can be deleted without the others noticing.
        ("subscripted-annotation",
         "def f(a: int) -> dict[str, int]:\n    a += 1\n"),
        ("attribute-annotation",
         "import sublime\n\n\n"
         "def f(a: int) -> sublime.View:\n    a += 1\n"),
        ("quoted-annotation",
         "def f() -> 'None':\n    return 1\n"),
        ("async-def",
         "async def f() -> None:\n    return 1\n"),
        # The `object` rows. `Any` is banned by name, so the same empty claim
        # gets spelled `object`; these are what stop that being free.
        ("object-over-a-spelled-out-value",
         "def f() -> object:\n    return {}\n"),
        ("annotation-contradicts-the-literal",
         "def f() -> int:\n    return 'x'\n"),
        ("annotation-contradicts-a-comparison",
         "def f(a: int) -> str:\n    return a > 1\n"),
        ("object-over-one-of-two-returns",
         "def f(a: int) -> object:\n"
         "    if a:\n"
         "        return g(a)\n"
         "    return 'x'\n"),
        ("str-over-a-bytes-literal",
         "def f() -> str:\n    return b'x'\n"),
    ],
    "annotations-match-defaults": [
        ("object-over-an-int-default",
         "def f(timeout: object = 10) -> None:\n    pass\n"),
        ("object-over-a-str-default",
         "def f(host: object = 'localhost') -> None:\n    pass\n"),
        ("int-over-a-str-default",
         "def f(host: int = 'localhost') -> None:\n    pass\n"),
        ("keyword-only-default",
         "def f(*, force: object = False) -> None:\n    pass\n"),
        ("method-default",
         "class C:\n"
         "    def m(self, n: object = 5) -> None:\n        pass\n"),
        ("tuple-default",
         "def f(ignore: str = ()) -> None:\n    pass\n"),
    ],
    # The category can arrive positionally or as `category=`, and `warn` can be
    # imported bare. Three spellings of one call, three fixtures.
    "no-adhoc-deprecation-warn": [
        ("keyword-category",
         "import warnings\n"
         "warnings.warn('gone', category=DeprecationWarning)\n"),
        ("bare-warn-import",
         "from warnings import warn\n"
         "warn('gone', DeprecationWarning)\n"),
        ("qualified-category",
         "import warnings\n"
         "import builtins\n"
         "warnings.warn('gone', builtins.DeprecationWarning)\n"),
        # The category can arrive under another name. Rung 04 round 1.
        ("aliased-category",
         "import warnings\n\n"
         "DW = DeprecationWarning\n"
         "warnings.warn('gone', DW)\n"),
        ("aliased-category-imported",
         "import warnings\n"
         "from builtins import DeprecationWarning as DW\n"
         "warnings.warn('gone', category=DW)\n"),
        ("aliased-category-chained",
         "import warnings\n\n"
         "DW = DeprecationWarning\n"
         "_D = DW\n"
         "warnings.warn('gone', _D)\n"),
    ],
    # A bare call and a module-qualified one are the same call; `iter_fields`
    # is in the set too, so it needs its own fixture or `get_host` carries it.
    "no-deprecated-surface-callers": [
        ("module-qualified",
         "from .util import url\n"
         "scheme, host, port = url.get_host(u)\n"),
        ("iter-fields-call",
         "from .filepost import iter_fields\n"
         "for k, v in iter_fields(fields):\n"
         "    pass\n"),
        ("aliased-module",
         "import urllib3.util.url as _u\n"
         "scheme, host, port = _u.get_host(u)\n"),
        # The callee can arrive under another name, by `as` or by assignment.
        # Rung 04 round 1.
        ("renamed-import",
         "from .util.url import get_host as gh\n"
         "scheme, host, port = gh(u)\n"),
        ("rebound-name",
         "from .util.url import get_host\n\n"
         "gh = get_host\n"
         "scheme, host, port = gh(u)\n"),
    ],
    # `Logger.warn` has exactly one call site in the shipped tree and four
    # spellings a candidate could leave it in. The receiver is what separates
    # them from `warnings.warn`, so each fixture varies the receiver shape.
    "no-logger-warn-alias": [
        ("attribute-receiver",
         "class Client(object):\n"
         "    def _enqueue(self, msg):\n"
         "        self.log.warn('queue is full')\n"),
        ("logging-module-function",
         "import logging\n"
         "logging.warn('queue is full')\n"),
        ("receiver-is-a-call",
         "import logging as lg\n"
         "lg.getLogger(__name__).warn('queue is full')\n"),
        ("aliased-logging-module",
         "import logging as lg\n"
         "lg.warn('queue is full')\n"),
        # The module-rebind pass resolves `w = warnings`; it reads the value
        # through the same predicate, so binding a logger to a local collects
        # nothing and this stays scored.
        ("logger-rebound-to-a-local",
         "class Client(object):\n"
         "    def _enqueue(self, msg):\n"
         "        w = self.log\n"
         "        w.warn('queue is full')\n"),
        # Both spellings the round-2 static review found silent: a logger
        # parked on the instance under the module's name, and a local rebound
        # away from the module. Neither is a plausible accident; both are the
        # shape an evasion takes, and both now fire.
        ("instance-attribute-named-warnings",
         "class Client(object):\n"
         "    def _enqueue(self, msg):\n"
         "        self.warnings.warn('queue is full')\n"),
        ("logger-shadowing-the-module-name",
         "import warnings\n\n"
         "class Client(object):\n"
         "    def _enqueue(self, msg):\n"
         "        warnings = self.log\n"
         "        warnings.warn('queue is full')\n"),
    ],
    "no-py2-iteration": [
        ("next-alias",
         "class It(object):\n"
         "    def __next__(self):\n"
         "        return 1\n"
         "    next = __next__\n"),
        ("next-alias-def",
         "class It(object):\n"
         "    def __next__(self):\n"
         "        return 1\n"
         "    def next(self):\n"
         "        return self.__next__()\n"),
    ],
    "no-version-branches": [
        ("from-import",
         "from sys import version_info\n"
         "if version_info >= (3, 0):\n"
         "    pass\n"),
        ("is-py2-flag",
         "is_py2 = False\n"
         "if is_py2:\n"
         "    pass\n"),
    ],
    "no-commented-out-py2": [
        ("indented-block",
         "def f():\n"
         "    # if PY2:\n"
         "    #     return unicode(x)\n"
         "    return 1\n"),
        ("commented-import",
         "# from six import text_type\n"
         "x = 1\n"),
        ("string-parked",
         "def f():\n"
         "    pass\n"
         "\"\"\"\n"
         "if sys.version_info[0] == 2:\n"
         "    text = unicode\n"
         "\"\"\"\n"),
    ],
    "no-six": [
        ("renamed-shim",
         "import _six as six\n"
         "print(six.text_type)\n"),
    ],
    # Four constructs behind one id, so four fixtures: section 7.11's lesson
    # is that a gate with two constructs and one fixture goes blind to one of
    # them without the selftest noticing. The `wrap_socket` arm is the
    # narrowed one (`_names_the_ssl_module`), so it needs both of its
    # spellings -- the module attribute and the `from ssl import` -- on their
    # own, or the narrowing could go wrong in either direction unobserved.
    "no-dead-ssl-names": [
        ("ssl-wrap-socket-attr",
         "import ssl\n"
         "sock = ssl.wrap_socket(s)\n"),
        ("from-ssl-import-wrap-socket",
         "from ssl import wrap_socket\n"
         "sock = wrap_socket(s)\n"),
        ("revendored-match-hostname",
         "from ._impl import match_hostname\n"
         "match_hostname(cert, host)\n"),
        ("certificate-error-on-a-package",
         "import backports.ssl_match_hostname as b\n"
         "raise b.CertificateError('x')\n"),
        ("protocol-sslv23-attr",
         "import ssl\n"
         "x = ssl.PROTOCOL_SSLv23\n"),
        ("aliased-ssl-wrap-socket",
         "import ssl as _ssl\n"
         "sock = _ssl.wrap_socket(s)\n"),
        ("aliased-ssl-from-package",
         "from backports import ssl as _ssl\n"
         "sock = _ssl.wrap_socket(s)\n"),
        ("aliased-ssl-dotted-import",
         "import backports.ssl as _ssl\n"
         "sock = _ssl.wrap_socket(s)\n"),
    ],
    # The path half of this gate is exercised by the deletion loop in
    # :func:`selftest`; these three are the source half. A backport that is
    # *moved* rather than deleted is silent under any glob naming the old
    # directory, so each of the two names it defines must fire on its own.
    "ssl-match-hostname-deleted": [
        ("relocated-backport",
         "class CertificateError(ValueError):\n"
         "    pass\n"
         "\n\n"
         "def match_hostname(cert, hostname):\n"
         "    return True\n"),
        ("match-hostname-alone",
         "def match_hostname(cert, hostname):\n"
         "    \"\"\"The backport, renamed directory and all.\"\"\"\n"
         "    return True\n"),
        ("certificate-error-alone",
         "class CertificateError(ValueError):\n"
         "    pass\n"),
        ("match-hostname-by-lambda",
         "match_hostname = lambda cert, hostname: True\n"),
        ("certificate-error-by-assignment",
         "from ._impl import CertificateError as _CE\n"
         "CertificateError = _CE\n"),
        ("match-hostname-assigned-in-a-function",
         "def _install(impl):\n"
         "    match_hostname = impl.match_hostname\n"
         "    return match_hostname\n"),
    ],
    # Every spelling and every hiding place of the one construct rung 07
    # removes. The block is only ever written one way in this tree, which is
    # exactly why the gate has to be shown reading the others: a candidate who
    # moves it under the `try: import certifi` block at the top of
    # `connection/http_urllib3.py`, or writes `typing.TYPE_CHECKING` because
    # the module already imports `typing`, has changed nothing about what the
    # run time can resolve.
    "no-type-checking-imports": [
        ("dotted-spelling",
         "import typing\n\n"
         "if typing.TYPE_CHECKING:\n"
         "    from collections.abc import Callable\n"),
        ("plain-import",
         "from typing import TYPE_CHECKING\n\n"
         "if TYPE_CHECKING:\n"
         "    import sublime\n"),
        ("relative-import",
         "from typing import TYPE_CHECKING\n\n"
         "if TYPE_CHECKING:\n"
         "    from ..transport import Transport\n"),
        ("parked-under-a-try",
         "from typing import TYPE_CHECKING\n\n"
         "try:\n"
         "    if TYPE_CHECKING:\n"
         "        from collections.abc import Mapping\n"
         "except ImportError:\n"
         "    pass\n"),
        ("parked-under-a-version-check",
         "import sys\n"
         "from typing import TYPE_CHECKING\n\n"
         "if sys.version_info >= (3, 14):\n"
         "    if TYPE_CHECKING:\n"
         "        from collections.abc import Mapping\n"),
        ("declaration-and-import-together",
         "from typing import TYPE_CHECKING, TypedDict\n\n"
         "if TYPE_CHECKING:\n"
         "    from collections.abc import Mapping\n\n"
         "    class Opts(TypedDict, total=False):\n"
         "        timeout: float\n"),
    ],
    # Every place the name can fail to be there, and every place the
    # annotation that wants it can hide. The first is the shape the measured
    # 0.997619 arm has in all eighteen plugin files; the last two are the two
    # ways to write the binding so that it reads as present and is not.
    "annotation-names-bound": [
        ("the-import-was-deleted-with-the-block",
         "from typing import TYPE_CHECKING\n\n\n"
         "def f(c: Elasticsearch) -> None:\n"
         "    pass\n"),
        ("a-return-annotation",
         "def f() -> Elasticsearch:\n"
         "    raise NotImplementedError\n"),
        ("a-class-attribute-annotation",
         "class Client(object):\n"
         "    transport: Transport\n"),
        ("under-a-subscript",
         "def f(hosts: list[HostInfo]) -> None:\n"
         "    pass\n"),
        ("quoted-so-the-parser-does-not-see-it",
         "def f(c: \"Elasticsearch\") -> None:\n"
         "    pass\n"),
        # `if TYPE_CHECKING:` is the spelling this tree uses and the one
        # `no-type-checking-imports` reads. A block that binds the name and
        # still does not run is the same defect under another test, and a gate
        # that read only the first spelling would call this tree bound.
        ("the-block-stayed-and-so-the-name-never-binds",
         "from typing import TYPE_CHECKING\n\n"
         "if TYPE_CHECKING:\n"
         "    from .client import Elasticsearch\n\n\n"
         "def f(c: Elasticsearch) -> None:\n"
         "    pass\n"),
        ("the-import-is-parked-in-a-dead-branch",
         "if False:\n"
         "    from .client import Elasticsearch\n\n\n"
         "def f(c: Elasticsearch) -> None:\n"
         "    pass\n"),
        # A star import the gate cannot read is not an excuse, or it is the
        # line a candidate adds to the file this gate was about to fire on.
        # The resolvable direction -- the four files in this tree that really
        # do import this way -- is pinned by the two-file fixture in
        # `selftest`, which is where a sibling module can exist.
        ("a-star-import-the-gate-cannot-read",
         "from .compat import *\n\n\n"
         "def f(c: Elasticsearch) -> None:\n"
         "    pass\n"),
        # Round 1 of the static review found the gate's whole plugin half for
        # sale at four lines, because "bound" was read tree-wide: any `Store`
        # anywhere satisfied any annotation anywhere. These five are the
        # shapes that bought it. The first is the one the review wrote out --
        # an import parked in a function nobody calls, which binds a local and
        # leaves the module's annotations exactly as unresolvable as deleting
        # it did. Section 14.8.
        ("bound-only-inside-a-function-that-is-never-called",
         "def _hold():\n"
         "    from .client import Elasticsearch\n\n\n"
         "def f(c: Elasticsearch) -> None:\n"
         "    pass\n"),
        ("bound-only-in-a-class-body",
         "class _Hold(object):\n"
         "    from .client import Elasticsearch\n\n\n"
         "def f(c: Elasticsearch) -> None:\n"
         "    pass\n"),
        ("bound-to-a-stub",
         "Elasticsearch = object\n\n\n"
         "def f(c: Elasticsearch) -> None:\n"
         "    pass\n"),
        ("declared-without-being-assigned",
         "Elasticsearch: type\n\n\n"
         "def f(c: Elasticsearch) -> None:\n"
         "    pass\n"),
        ("deleted-again-after-the-import",
         "from .client import Elasticsearch\n\n"
         "del Elasticsearch\n\n\n"
         "def f(c: Elasticsearch) -> None:\n"
         "    pass\n"),
    ],
    # The quote and the string are not the same thing. Both rows are quoted
    # *types*: the second is the one a walk that stops at the subscript head
    # misses, and its neighbours in `CLEAN_EXTRA` are the strings that are
    # data. Section 14.8.
    "no-quoted-annotations": [
        ("quoted-inside-a-subscript",
         "def f(hosts: list['HostInfo']) -> None:\n"
         "    pass\n"),
        ("quoted-in-an-annotated-first-argument",
         "from typing import Annotated\n\n\n"
         "def f(c: Annotated['Elasticsearch', 'the client']) -> None:\n"
         "    pass\n"),
        # The rename cuts both ways: an `Annotated` under another name still
        # has a type in its first argument, and the carve-out must not swallow
        # the whole subscript just because it recognised the head.
        ("quoted-in-a-renamed-annotated-first-argument",
         "from typing import Annotated as Ann\n\n\n"
         "def f(c: Ann['Elasticsearch', 'the client']) -> None:\n"
         "    pass\n"),
    ],
}

#: The other half of a narrowed check: a construct that looks like the banned
#: one and must NOT fire. Both entries are narrowings made in round 5 --
#: without these, widening either check back to its old form would pass the
#: selftest silently. Section 7.14.
CLEAN_EXTRA = {
    # `requests/cookies.py` defines `MockRequest.get_host` -- the
    # `http.cookiejar` request interface, nothing to do with urllib3's
    # function. Decorating it would be wrong and rerouting it impossible, so
    # both of this rung's `get_host` gates have to see past the name. These
    # are the fixtures that say so, and a gate written with `ast.walk` instead
    # of `unit.tree.body` fails the first one.
    "deprecated-surface-marked": [
        ("method-of-the-same-name",
         "class MockRequest(object):\n"
         "    def get_host(self):\n"
         "        return urlparse(self._r.url).netloc\n"),
        ("correctly-marked",
         "import warnings\n\n\n"
         "@warnings.deprecated('Use parse_url() instead.')\n"
         "def get_host(url):\n"
         "    return parse_url(url)\n"),
        ("correctly-marked-bare-import",
         "from warnings import deprecated\n\n\n"
         "@deprecated('Use iter_field_objects() instead.')\n"
         "def iter_fields(fields):\n"
         "    return iter(fields)\n"),
        ("prose-only",
         "\"\"\"get_host and iter_fields are deprecated; this module has"
         " neither.\"\"\"\n"
         "# def get_host(url): ...\n"
         "x = 1\n"),
        # The other half of the round-1 widening. Recursion now reaches a
        # `def` under a `try`, so it has to keep stopping at a `class` under
        # one -- otherwise widening the walk re-broke `MockRequest.get_host`
        # and the first fixture above would no longer be enough to say so.
        ("method-of-the-same-name-under-a-try",
         "try:\n"
         "    class MockRequest(object):\n"
         "        def get_host(self):\n"
         "            return urlparse(self._r.url).netloc\n"
         "except ImportError:\n"
         "    MockRequest = None\n"),
        ("correctly-marked-under-a-try",
         "import warnings\n\n"
         "try:\n"
         "    @warnings.deprecated('Use parse_url() instead.')\n"
         "    def get_host(url):\n"
         "        return parse_url(url)\n"
         "except ImportError:\n"
         "    pass\n"),
        ("nested-def-of-the-same-name",
         "def _install():\n"
         "    def get_host(url):\n"
         "        return parse_url(url)\n"
         "    return get_host\n"),
    ],
    # The other seven `warnings.warn` calls in this tree are the library's own
    # contract and are scored by behaviour cases that must keep passing. A gate
    # that fired on any of them would fail the correct answer.
    "no-adhoc-deprecation-warn": [
        ("contract-warning",
         "import warnings\n"
         "warnings.warn('Unverified HTTPS request', InsecureRequestWarning)\n"),
        ("the-decorator-itself",
         "import warnings\n\n\n"
         "@warnings.deprecated('Use parse_url() instead.')\n"
         "def get_host(url):\n"
         "    return parse_url(url)\n"),
        ("a-filter-not-a-warn",
         "import warnings\n"
         "warnings.simplefilter('ignore', DeprecationWarning)\n"
         "warnings.filterwarnings('once', category=DeprecationWarning)\n"),
        ("prose-about-it",
         "\"\"\"This used to warnings.warn(..., DeprecationWarning).\"\"\"\n"
         "# warnings.warn('gone', DeprecationWarning)\n"
         "x = 1\n"),
        # The alias resolution added in round 1 resolves *DeprecationWarning*
        # and nothing else. The tree's three contract warnings are routinely
        # bound to a local name in the modules that raise them, and a check
        # that resolved every alias would fire on the correct answer.
        ("alias-of-a-contract-warning",
         "import warnings\n\n"
         "IRW = InsecureRequestWarning\n"
         "warnings.warn('Unverified HTTPS request', IRW)\n"),
        # A subclass is not an alias and is deliberately out of scope here.
        # `requests.deprecated-utils-announce` records the category by name
        # ("DeprecationWarning"), so a tree that swapped in a subclass to dodge
        # this gate changes the recorded answer and fails the case -- one
        # finding in one place, rather than a gate and a case saying the same
        # thing about half the tree. This fixture pins that boundary.
        ("subclass-is-scored-by-the-case-not-the-gate",
         "import warnings\n\n\n"
         "class _DW(DeprecationWarning):\n"
         "    pass\n\n\n"
         "warnings.warn('gone', _DW)\n"),
    ],
    "no-deprecated-surface-callers": [
        ("cookiejar-interface",
         "class MockRequest(object):\n"
         "    def get_host(self):\n"
         "        return urlparse(self._r.url).netloc\n"
         "\n"
         "    def get_origin_req_host(self):\n"
         "        return self.get_host()\n"),
        ("attribute-on-a-parameter",
         "def origin(req):\n"
         "    return req.get_host()\n"),
        # The public re-export has to survive: `urllib3.get_host` is still part
        # of the package's API and the behaviour suite calls it from outside
        # the tree. Importing a deprecated name is not calling it.
        ("re-export",
         "from .util.url import get_host, parse_url\n"
         "__all__ = ['get_host', 'parse_url']\n"),
        ("the-replacement",
         "from .util.url import parse_url\n"
         "p = parse_url(url)\n"
         "scheme, host, port = p.scheme or 'http', p.hostname, p.port\n"),
        ("the-definition-itself",
         "import warnings\n\n\n"
         "@warnings.deprecated('Use parse_url() instead.')\n"
         "def get_host(url):\n"
         "    p = parse_url(url)\n"
         "    return p.scheme or 'http', p.hostname, p.port\n"),
        # The round-1 rename resolution must not turn the re-export into a
        # call: `from .util.url import get_host as get_host` and an `__all__`
        # entry are still an import.
        ("renamed-re-export",
         "from .util.url import get_host as gh\n"
         "__all__ = ['gh']\n"),
        # Only a bare Name counts on the value side, so the cookiejar
        # interface reached through a parameter stays silent even when it is
        # bound to a local first.
        ("parameter-attribute-bound-to-a-local",
         "def origin(req):\n"
         "    f = req.get_host\n"
         "    return f()\n"),
    ],
    "no-logger-warn-alias": [
        # `warnings.warn` is a different function, is not deprecated, and this
        # tree makes nine correct calls to it. The receiver is the test, so
        # the aliased import has to be recognised too.
        ("warnings-warn-is-not-a-logger",
         "import warnings\n"
         "warnings.warn('Unverified HTTPS request', InsecureRequestWarning)\n"),
        ("aliased-warnings-module",
         "import warnings as w\n"
         "w.warn('gone', UserWarning)\n"),
        ("the-replacement",
         "import logging\n\n"
         "log = logging.getLogger('segment')\n"
         "log.warning('analytics-python queue is full')\n"),
        ("prose-about-it",
         "\"\"\"This used to call self.log.warn() for a full queue.\"\"\"\n"
         "# self.log.warn('analytics-python queue is full')\n"
         "x = 1\n"),
        # Three more spellings of the same correct call, each of which this
        # gate fired on when it was first written. `_warnings` is the C
        # accelerator `warnings.warn` is defined from; a dotted import binds
        # only its root, so the receiver is an Attribute chain and there is no
        # local name to match; and a module rebound to a local is still the
        # module. A tree doing any of these is correct and must stay silent.
        ("c-accelerator-is-the-warnings-module",
         "import _warnings\n"
         "_warnings.warn('gone', DeprecationWarning)\n"),
        ("dotted-warnings-import-reads-through-the-receiver",
         "import pkg.warnings\n"
         "pkg.warnings.warn('gone', UserWarning)\n"),
        ("warnings-module-rebound-to-a-local",
         "import warnings\n"
         "w = warnings\n"
         "w.warn('gone', UserWarning)\n"),
        # No `import` names the module in either of these, which is why the
        # alias set is seeded rather than started empty. Both were found by
        # sweeping the 3.13 standard library for this gate over-firing:
        # `importlib/_bootstrap.py` has the interpreter inject `_warnings`
        # over a module-level `_warnings = None`, and a module that takes
        # `warnings` from a star-import binds no `Import` node at all.
        ("injected-global-never-imported",
         "_warnings = None\n"
         "def load_module(self):\n"
         "    _warnings.warn('deprecated', DeprecationWarning)\n"),
        ("star-imported-warnings-module",
         "from .compat import *\n"
         "warnings.warn('More than one string argument', UserWarning)\n"),
    ],
    "no-version-branches": [
        ("own-version-info",
         "def version_info():\n"
         "    return (1, 2)\n"
         "print(version_info())\n"),
    ],
    "no-commented-out-py2": [
        ("prose-about-py2",
         "# six is gone from this module\n"
         "# the unicode handling moved into the codec layer\n"
         "x = 1\n"),
        ("note-was",      "# was: six.b(s)\nx = 1\n"),
        ("note-replaced", "# replaced: six.text_type\nx = 1\n"),
        ("note-arrow",    "# six.b(s) -> s.encode('latin-1')\nx = 1\n"),
        ("single-dead-line", "# text = unicode\nx = 1\n"),
        ("trailing-comment", "x = 1  # unicode here means str\n"),
        ("real-docstring-with-example",
         "\"\"\"Example:\n\n"
         "    for k, v in d.iteritems():\n"
         "        print(k)\n"
         "\"\"\"\n"
         "x = 1\n"),
        # The two that reach the exemption rather than stopping at the parse.
        # `real-docstring-with-example` above is indented under a prose line,
        # so `ast.parse` refuses it and the string arm never gets as far as
        # asking whether it is a docstring -- it passed while the exemption was
        # dead. These two parse cleanly, so only the docstring test keeps them
        # quiet, and they fail the moment `_docstring_expr_ids` stops being the
        # function `check_commented_out_py2` calls.
        ("module-docstring-quotes-a-branch",
         "\"\"\"if PY2:\n"
         "    text = unicode\n"
         "\"\"\"\n"
         "x = 1\n"),
        ("function-docstring-quotes-a-branch",
         "def f():\n"
         "    \"\"\"if PY2:\n"
         "    text = unicode\n"
         "    \"\"\"\n"
         "    return 1\n"),
    ],
    "no-legacy-future-imports": [
        ("non-legacy-future",
         "from __future__ import generator_stop\n"),
    ],
    # The correct answer to rung 03 contains this line. If the gate ever fires
    # on it, the rung's own golden scores zero on its own gate family, which
    # is the failure INSTRUCTIONS trap 6 exists to catch before the finding.
    "no-dead-ssl-names": [
        ("sslcontext-wrap-socket",
         "import ssl\n"
         "ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)\n"
         "ctx.check_hostname = False\n"
         "sock = ctx.wrap_socket(s, server_hostname='example.invalid')\n"),
        # The alias widening must reach the module and stop there: a context
        # built through an alias is still a context, and an unrelated module
        # imported under a short name is not `ssl` however short the name is.
        ("aliased-sslcontext-wrap-socket",
         "import ssl as _ssl\n"
         "ctx = _ssl.SSLContext(_ssl.PROTOCOL_TLS_CLIENT)\n"
         "sock = ctx.wrap_socket(s, server_hostname='example.invalid')\n"),
        # The alias must be the *receiver* for this to measure anything: the
        # earlier version of this fixture put `_ssl` in an argument and stayed
        # silent whatever `_ssl_aliases` had collected. Here `_ssl` is bound to
        # `socket`, so a widening that registered every asname would report
        # `.wrap_socket` on a module that is not `ssl`.
        ("alias-of-another-module",
         "import socket as _ssl\n"
         "sock = _ssl.wrap_socket(s)\n"),
        # The dotted widening has to read the *last* component and nothing
        # else. `backports.tls` is not the ssl module however it is aliased,
        # and `ssl_helpers` is not it either -- a widening spelled
        # ``"ssl" in alias.name`` passes the dirty fixtures above and invents
        # a finding on both of these.
        ("dotted-alias-of-another-module",
         "import backports.tls as _ssl\n"
         "sock = _ssl.wrap_socket(s)\n"),
        ("alias-of-a-module-named-after-ssl",
         "import ssl_helpers as _sh\n"
         "sock = _sh.wrap_socket(s)\n"),
    ],
    # The assignment widening must bind a *name*. `ctx.match_hostname = None`
    # stores through an attribute and revendors nothing, and setting
    # `check_hostname` is what the correct answer to this rung does -- a
    # widening that looked at `Attribute.attr`, or at the receiver Name, would
    # fail the only tree that can pass the gate (trap 6).
    "ssl-match-hostname-deleted": [
        ("attribute-named-match-hostname",
         "ctx.match_hostname = None\n"),
        ("correct-port-sets-check-hostname",
         "import ssl\n"
         "ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)\n"
         "ctx.check_hostname = True\n"
         "ctx.verify_mode = ssl.CERT_REQUIRED\n"),
        ("prose-and-the-replacement",
         "\"\"\"The match_hostname backport is gone; CertificateError with it.\"\"\"\n"
         "# match_hostname(cert, host) used to be called from here\n"
         "import ssl\n"
         "ctx = ssl.create_default_context()\n"
         "ctx.check_hostname = True\n"),
        ("similarly-named-definitions",
         "def match_hostname_policy(cfg):\n"
         "    return cfg\n"
         "\n\n"
         "class CertificateErrorHandler(object):\n"
         "    pass\n"),
    ],
    # The other side of the widening above. Following renames means reading
    # assignments, and reading quoted annotations means parsing strings;
    # both are ways to start reporting a correct file, and each row is one.
    "no-any-in-annotations": [
        ("any-imported-but-never-annotated-with",
         "from typing import Any\n\nCASTS = [Any]\n"),
        ("an-alias-that-does-not-mention-any",
         "from typing import Any\n\nJson = dict[str, str]\n\n\n"
         "def f(x: Json) -> None:\n    pass\n"),
        # Substring, not name: `Anything` is not `Any`.
        ("a-longer-name",
         "from .types import Anything\n\n\n"
         "def f(x: Anything) -> None:\n    pass\n"),
        ("a-parameter-named-any",
         "def f(any: int) -> None:\n    pass\n"),
        # A string inside an annotation is usually a type. Inside `Literal`
        # it is a value, and inside `Annotated` past the first element it is
        # documentation.
        ("the-string-any-inside-a-literal",
         "from typing import Literal\n\n\n"
         "def f(x: Literal['Any']) -> None:\n    pass\n"),
        ("the-string-any-inside-an-annotated",
         "from typing import Annotated\n\n\n"
         "def f(x: Annotated[int, 'Any']) -> None:\n    pass\n"),
        ("any-named-only-in-prose",
         "\"\"\"Accepts Any value at all.\"\"\"\n\n\n"
         "def f(x: int) -> None:\n"
         "    \"\"\"Takes Any int -- prose, not an annotation.\"\"\"\n"),
    ],
    # Everything `return-annotations-match-bodies` deliberately does not
    # count. Each row is a shape that a coarser reading of the same rule
    # reports, and every one of them appears in the rung-05 golden -- so
    # without these, tightening the gate by one line would be a gate that
    # fires on the tree it exists to accept.
    "return-annotations-match-bodies": [
        # A `return` in a closure is the closure's statement about itself.
        ("return-inside-a-nested-def",
         "def f() -> None:\n"
         "    def inner():\n"
         "        return 1\n"
         "    inner()\n"),
        ("return-inside-a-lambda",
         "def f() -> None:\n"
         "    g = lambda: 1\n"
         "    g()\n"),
        ("return-inside-a-nested-class",
         "def f() -> None:\n"
         "    class C:\n"
         "        def m(self):\n"
         "            return 1\n"
         "    C()\n"),
        # `return None` under `-> None` is agreement, not a valued return.
        ("explicit-return-none",
         "def f() -> None:\n    return None\n"),
        ("bare-return",
         "def f() -> None:\n    return\n"),
        # A body that falls off the end is a true instance of anything that
        # admits None, and of `object`.
        ("optional-falls-off-the-end",
         "def f(a: int) -> str | None:\n    a += 1\n"),
        ("optional-spelled-old-style",
         "from typing import Optional\n\n\n"
         "def f(a: int) -> Optional[str]:\n    a += 1\n"),
        ("object-falls-off-the-end",
         "def f(a: int) -> object:\n    a += 1\n"),
        # A declaration is not an implementation.
        ("abstract-body",
         "def f() -> str:\n    ...\n"),
        ("docstring-only-body",
         "def f() -> str:\n    \"\"\"Return the thing.\"\"\"\n"),
        ("pass-body",
         "def f() -> str:\n    pass\n"),
        ("raise-only-body",
         "def f() -> str:\n    raise NotImplementedError\n"),
        # The three spellings of a generator return, and the one shape where
        # a `raise` and a `return` coexist.
        ("generator-annotated-as-an-iterator",
         "from collections.abc import Iterator\n\n\n"
         "def f() -> Iterator[int]:\n    yield 1\n"),
        ("generator-annotated-qualified",
         "import collections.abc\n\n\n"
         "def f() -> collections.abc.Generator:\n    yield 1\n"),
        ("generator-with-a-bare-return",
         "from collections.abc import Iterator\n\n\n"
         "def f(a: int) -> Iterator[int]:\n"
         "    if a:\n"
         "        return\n"
         "    yield 1\n"),
        ("raises-on-one-path-returns-on-another",
         "def f(a: int) -> str:\n"
         "    if a:\n"
         "        raise ValueError(a)\n"
         "    return 'x'\n"),
        # Private defs and `__init__` are outside the scope, exactly as they
        # are for `public-surface-annotated`.
        ("private-def",
         "def _f():\n    return 1\n"),
        ("init-without-a-return-annotation",
         "class C:\n"
         "    def __init__(self, a: int):\n"
         "        self.a = a\n"),
        # The near-misses of the `object` rows above. Each is a *true*
        # annotation that a coarser version of the rule would have called a
        # lie, and between them they are why it reads only what the source
        # spells out, and only against names it can resolve.
        ("object-over-a-value-the-source-does-not-spell",
         "def f(a: int) -> object:\n    return g(a)\n"),
        ("a-supertype-of-the-literal",
         "from collections.abc import Sequence\n\n\n"
         "def f() -> Sequence[str]:\n    return []\n"),
        ("a-mapping-over-a-dict-display",
         "from collections.abc import Mapping\n\n\n"
         "def f() -> Mapping[str, str]:\n    return {}\n"),
        ("the-numeric-tower",
         "def f() -> float:\n    return 1\n"),
        ("an-alias-this-module-cannot-resolve",
         "from .base import Payload\n\n\n"
         "def f() -> Payload | None:\n    return 'x'\n"),
        ("a-tuple-alias-over-a-tuple-display",
         "from .base import HttpResponse\n\n\n"
         "def f() -> HttpResponse:\n    return (200, {}, 'x')\n"),
        ("an-f-string-under-str",
         "def f(a: int) -> str:\n    return f'{a}'\n"),
    ],
    "annotations-match-defaults": [
        ("the-annotation-names-the-default",
         "def f(timeout: float = 10, host: str = 'localhost') -> None:\n"
         "    pass\n"),
        ("a-none-default-says-nothing",
         "def f(params: object = None) -> None:\n    pass\n"),
        ("an-optional-over-a-none-default",
         "def f(body: str | None = None) -> None:\n    pass\n"),
        ("a-supertype-of-the-default",
         "from collections.abc import Container\n\n\n"
         "def f(ignore: Container[int] = ()) -> None:\n    pass\n"),
        ("the-numeric-tower-again",
         "def f(timeout: float = 10) -> None:\n    pass\n"),
        ("a-default-the-source-does-not-spell",
         "import sublime\n\n\n"
         "def f(view: object = sublime.active_window()) -> None:\n"
         "    pass\n"),
        ("an-alias-over-a-spelled-out-default",
         "from .base import Timeout\n\n\n"
         "def f(timeout: Timeout = 10) -> None:\n    pass\n"),
        ("an-unannotated-parameter-is-another-gate's-finding",
         "def f(timeout=10) -> None:\n    pass\n"),
        ("kwargs-object-has-no-default",
         "def f(**kwargs: object) -> None:\n    pass\n"),
        ("a-private-def-is-not-public-surface",
         "def _f(timeout: object = 10) -> None:\n    pass\n"),
    ],
    # The four narrowings in `check_type_checking_imports`, one fixture each.
    # Widen the gate back to "any `if TYPE_CHECKING:` anywhere" and the first
    # of these fails, which is the point: rung 05 put a block in the body of
    # `class Connection` to say what `perform_request` looks like, and lifting
    # *that* one out gives the base class a method it has never had at run
    # time -- a change to the library's surface that no case on this ladder
    # asks for and one inherited case would see.
    "no-type-checking-imports": [
        ("a-declaration-in-a-class-body",
         "from typing import TYPE_CHECKING\n\n\n"
         "class Connection(object):\n"
         "    if TYPE_CHECKING:\n"
         "        def perform_request(self, method: str, url: str) -> object:\n"
         "            ...\n"),
        ("a-block-that-imports-nothing",
         "from typing import TYPE_CHECKING, TypedDict\n\n"
         "if TYPE_CHECKING:\n"
         "    type HttpAuth = str | tuple[str, ...] | None\n\n"
         "    class ConnectionOptions(TypedDict, total=False):\n"
         "        timeout: float\n"),
        ("the-else-branch-is-the-run-time-branch",
         "from typing import TYPE_CHECKING\n\n"
         "if TYPE_CHECKING:\n"
         "    pass\n"
         "else:\n"
         "    from collections.abc import Callable\n"),
        ("an-import-guarded-by-something-else",
         "import sys\n\n"
         "if sys.version_info >= (3, 14):\n"
         "    from collections.abc import Callable\n"),
        # The name on its own is not the construct. `TYPE_CHECKING` stays
        # importable at rung 07 -- it is how a module still marked up for mypy
        # spells a branch mypy alone takes -- so reading the import rather
        # than the `if` would fire on a module that has already done the work.
        ("importing-the-flag-without-using-it",
         "from typing import TYPE_CHECKING, TypedDict\n\n\n"
         "class Opts(TypedDict, total=False):\n"
         "    timeout: float\n"),
    ],
    # The six ways a name is legitimately there. Every one of them is in the
    # finished tree, and a gate that got any of them wrong would report the
    # golden: the `else:` branch is how a module keeps a run-time import next
    # to a checker-only one, `Literal` holds strings that are values rather
    # than names, PEP 695 binds its type parameters in the signature itself,
    # and `from x import *` binds what the source does not say it binds.
    "annotation-names-bound": [
        ("a-builtin",
         "def f(x: int, *rest: str) -> bool:\n"
         "    return True\n"),
        ("the-else-branch-is-the-run-time-branch",
         "from typing import TYPE_CHECKING\n\n"
         "if TYPE_CHECKING:\n"
         "    pass\n"
         "else:\n"
         "    from collections.abc import Callable\n\n\n"
         "def f(cb: Callable[[int], None]) -> None:\n"
         "    pass\n"),
        ("a-literal-holds-values-not-names",
         "from typing import Literal\n\n\n"
         "def f(how: Literal[\"Elasticsearch\", \"raw\"]) -> None:\n"
         "    pass\n"),
        ("a-pep-695-type-parameter",
         "def first[T](items: list[T]) -> T:\n"
         "    return items[0]\n"),
        ("a-pep-695-alias-declared-at-module-level",
         "type HostInfo = tuple[str, int]\n\n\n"
         "def f(host: HostInfo) -> None:\n"
         "    pass\n"),
        # The scope rules the review's exploit forced in (section 14.8) cut
        # both ways: a binding that really is visible has to stay visible, or
        # the gate reports a correct tree. One row per direction the new walk
        # composes -- inward from the module, inward from a class body, and a
        # parameter naming a type bound in the function that holds the `def`.
        ("bound-in-the-module-and-used-in-a-method",
         "from .client import Elasticsearch\n\n\n"
         "class Cmd(object):\n"
         "    def run(self, c: Elasticsearch) -> None:\n"
         "        pass\n"),
        ("bound-in-the-class-body-and-used-in-its-own-method",
         "class Cmd(object):\n"
         "    from .client import Elasticsearch\n\n"
         "    def run(self, c: Elasticsearch) -> None:\n"
         "        pass\n"),
        ("bound-in-the-function-that-holds-the-def",
         "def outer() -> None:\n"
         "    from .client import Elasticsearch\n\n"
         "    def inner(c: Elasticsearch) -> None:\n"
         "        pass\n"),
    ],
    # `Literal["json"]` is a value and an `Annotated`'s tail is documentation.
    # Neither names a type, so neither is a quoted annotation -- and a guard
    # that cannot tell them from one costs a correct tree 0.05. The carve-out
    # is `_ann_walk`'s, shared with `annotation-names-bound`, and these are
    # what stop it being quietly widened back. Section 14.8.
    "no-quoted-annotations": [
        ("a-literal-holds-values-not-names",
         "from typing import Literal\n\n\n"
         "def f(how: Literal[\"json\", \"raw\"]) -> None:\n"
         "    pass\n"),
        ("an-annotated-tail-is-metadata",
         "from typing import Annotated\n\n\n"
         "def f(timeout: Annotated[float, \"seconds\"]) -> None:\n"
         "    pass\n"),
        # Both forms under another name. A carve-out keyed on the spelling
        # charges these 0.05 for importing `typing` the way `typing` says it
        # may be imported; `_carve_aliases` is what stops that, and these are
        # the rows that fail if it is removed. Round 2, section 14.10.
        ("a-renamed-literal-is-still-a-literal",
         "from typing import Literal as L\n\n\n"
         "def f(how: L[\"json\", \"raw\"]) -> None:\n"
         "    pass\n"),
        ("a-rebound-annotated-is-still-an-annotated",
         "import typing\n\n"
         "Ann = typing.Annotated\n\n\n"
         "def f(timeout: Ann[float, \"seconds\"]) -> None:\n"
         "    pass\n"),
    ],
}


#: The same constructs, everywhere a parser can see them but the language
#: cannot execute them -- prose, comments and string data. This is the fixture
#: a grep-based gate fails: every banned spelling is in it.
#:
#: Two fixtures rather than one, because two of the gates are deliberately
#: opposed: step 05 *requires* ``from __future__ import annotations`` and step
#: 07 forbids it, so no single file can leave both silent. ``CLEAN_05`` is what
#: a passing step-05 tree looks like and ``CLEAN_07`` what a passing step-07
#: one does; each is the other's dirty fixture, which is exactly the property
#: the ladder depends on and the cheapest possible way to pin it.
#: The path matcher, both directions. Row three is the one that was wrong:
#: with plain ``fnmatch`` a single ``*`` swallowed the separator and a
#: six-file scope silently became a 168-file one. Rows six and seven are the
#: same defect on the other side -- a single ``*`` is one segment, so neither
#: vendored ``six.py`` matches ``*/packages/six.py`` (they live three and five
#: levels down), which is why ``six-deleted`` is written with ``**`` and why
#: writing it the other way would have been a gate that passed on a tree still
#: carrying both copies.
MATCH_FIXTURES = (
    ("events.py", "events.py", True),
    ("commands/base.py", "commands/*.py", True),
    ("lib/elasticsearch/client/__init__.py", "lib/elasticsearch/*.py", False),
    ("lib/elasticsearch/client/utils.py",
     "lib/elasticsearch/client/utils.py", True),
    ("lib/elasticsearch/connection/esthrift/Rest.py",
     "lib/elasticsearch/**/*.py", True),
    ("lib/urllib3/packages/six.py", "*/packages/six.py", False),
    ("lib/urllib3/packages/six.py", "**/packages/six.py", True),
    ("lib/requests/packages/urllib3/packages/six.py",
     "**/packages/six.py", True),
    ("lib/six.py", "lib/six.py", True),
    ("lib/sixpack.py", "lib/six.py", False),
)

_CLEAN_BODY = u'''\
"""A docstring naming unicode, xrange, six, cgi, ssl.wrap_socket,
from __future__ import annotations, .iteritems(), u'x', datetime.utcnow(),
get_host(url), iter_fields(fields), warnings.warn(msg, DeprecationWarning)
and # type: ignore -- all of it prose."""
# import six, import cgi, from fractions import gcd, sys.version_info
# return in finally, u'quoted', Any, def get_host(url), warn(DeprecationWarning)
NAMES = ["unicode", "xrange", "six", "cgi", "iteritems", "utcnow",
         "from __future__ import annotations", "# noqa", "Any"]


def f(a: int) -> int:
    """A return in the ``try`` of a ``try/finally``, which is not the thing.

    PEP 765 is about a ``return`` in the ``finally`` *body*, which swallows
    whatever was in flight. This one is the ordinary pairing, it means what it
    says, and ``no-finally-control-flow`` has to read which block the
    statement is in rather than which two keywords are in the function.
    """
    try:
        return a
    finally:
        pass


class Consumer(object):
    """``analytics.Consumer`` in miniature: a ``next`` that is not an iterator.

    The shape that made ``no-py2-iteration`` fire on a correct port. Neither
    the definition nor the call is a Python 2 spelling -- this class has no
    ``__iter__`` and no ``__next__``, and ``next(self)`` would raise -- so
    both must stay silent. Section 7.9.
    """

    def upload(self) -> list:
        return self.next()

    def next(self) -> list:
        """Return the next batch of items to upload."""
        return []
'''

#: The idiom instruments' fixtures, ``(gate, dirty, clean)``: one construct
#: each selector names and its current spelling. The selftest adds a
#: non-UTF-8 file, a ``# noqa`` and a tree-local linter config to each dirty
#: case, all of which must still fire.
IDIOM_FIXTURES = (
    ("no-legacy-idioms", "class A(object):\n    pass\n",
     "class A:\n    pass\n"),
    ("no-bug-prone-idioms", "def f(x=[]):\n    return x\n",
     "def f(x=None):\n    return x\n"),
    ("typing-idioms",
     "from collections import namedtuple\n\nP = namedtuple('P', 'a b')\n",
     "from typing import NamedTuple\n\n\nclass P(NamedTuple):\n"
     "    a: int\n    b: int\n"),
    ("async-idioms",
     "import time\n\n\nasync def f():\n    time.sleep(1)\n",
     "import asyncio\n\n\nasync def f():\n    await asyncio.sleep(1)\n"),
    ("modernizer-idempotent", "s = set([1, 2])\n", "s = {1, 2}\n"),
)
IDIOM_TOOL_GATES = frozenset(g for g, _, _ in IDIOM_FIXTURES)

#: One reference of every kind :func:`sweep_cases` claims to see: a banned
#: import root, a ``from`` import, a bare name, an attribute, a ``.next()``
#: call, an ``importlib``-style path literal, a retired stdlib module and a
#: Python 2 shim under both spellings.
SWEEP_DIRTY = u'''\
"""A docstring may name ssl_match_hostname and six freely: it is prose."""
import six
import telnetlib
from distutils.version import LooseVersion


def case_a(c):
    # a comment naming unicode is prose too
    for k, v in c.iteritems():
        pass
    return unicode(k), xrange(3), six.b("x")


def case_b(c):
    it = iter([1])
    m = __import__("urllib3.packages.ssl_match_hostname")
    return it.next(), m, c.utcnow()


def case_d(c):
    import dateutil.rrule as rr
    return callable(rr.advance_iterator), string_types, telnetlib
'''

#: The same constructs, named only where the sweep must not look.
SWEEP_CLEAN = u'''\
"""Names six, cgi, imp, distutils and ssl_match_hostname -- all in prose."""


def case_c(c):
    # unicode/xrange/iteritems named only in a comment
    return str(c), range(3), list(c.items())
'''

SWEEP_EXPECT = ("import six", "from distutils.version", ".iteritems",
                "ssl_match_hostname", ".next()", "unicode", "xrange",
                ".utcnow", "import telnetlib", ".advance_iterator",
                "string_types")

#: ``types-check-clean`` is the one gate on this ladder whose instrument is a
#: separate binary, and the only one the generic ``DIRTY`` loop cannot reach:
#: that loop writes ``events.py``, which is in ``TYPED_PLUGIN`` and so outside
#: this gate's ``TYPED_LIB`` scope. It gets its own fixtures, at a path inside
#: the scope, in three directions rather than two -- because three different
#: wrong answers are available to it. A real type error has to be reported; a
#: file that checks clean must not be; and an image with no checker in it has
#: to *say so*, since "I found nothing" and "I could not look" are the same
#: empty list and the first is what a correct tree earns (INSTRUCTIONS trap 3,
#: and trap 6: verify the instrument before the finding).
TYPECHECK_REL = "lib/elasticsearch/widen.py"

TYPECHECK_DIRTY = u'''\
from __future__ import annotations


def widen(n: int) -> str:
    """Return the int, which is not a str: one unambiguous [return-value]."""
    return n
'''

TYPECHECK_CLEAN = u'''\
from __future__ import annotations


def widen(n: int) -> str:
    """The same function, spelled so that the declaration is true."""
    return str(n)
'''

CLEAN_05 = (u"# -*- coding: utf-8 -*-\nfrom __future__ import annotations\n"
            + _CLEAN_BODY)
CLEAN_07 = u"# -*- coding: utf-8 -*-\n" + _CLEAN_BODY


#: The recording :func:`selftest` sweeps. Authoring-side only -- the verifier
#: image carries its copy at a different path and never runs the selftest.
RECORDING_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), os.pardir, os.pardir,
    "work", "recording", "baseline.json.gz")


def _dirty_public_attrs(cases):
    """A ``dir()``-derived list acquires the ``next`` alias it must not pin."""
    for row in cases["rrule.no-str-serialisation"]["result"]["__dict__"]:
        if row[0] == "public_attrs":
            row[1] = sorted(row[1] + ["next"])


def _dirty_extra_leaf(cases):
    """An exempt case grows an eighth benign-looking leaf: count must move."""
    cases["requests.parse-header-links"]["result"].append([{"rel": "next"}])


def _dirty_stderr(cases):
    """A retired method name reaches an expectation through stderr."""
    cases["rrule.no-str-serialisation"]["stderr"] = "AttributeError: has_key"


def _dirty_dropped(cases):
    """An exemption outlives the case it was written for."""
    del cases["requests.parse-header-links"]


#: Four ways a retired spelling reaches a recorded value, one per mechanism:
#: an attribute enumeration, a new leaf inside an already-exempt case, a
#: stream, and an exemption left behind by a deleted case.
RECORDING_DIRTY = (("public-attrs", _dirty_public_attrs),
                   ("extra-leaf", _dirty_extra_leaf),
                   ("stderr", _dirty_stderr),
                   ("dropped-case", _dirty_dropped))


def _check_no_shadowed_definitions(failures):
    """Every harness module, scanned for a definition that another one hides.

    The independent fix round on rung 03 found ``_docstring_ids`` defined
    twice at module level in this file: Python bound the name to the second
    one, the first was dead, and the docstring exemption it implemented had
    never run. Nothing failed -- a gate simply reported a true docstring as a
    parked Python 2 branch, and a *correct* tree paid `GUARD_PENALTY` for it.
    The defect was found by reading, so the reading is now a measurement:
    module-level rebinding and duplicate string keys in a dict literal are the
    two spellings of it, and both are mechanical. Fixture tables are dicts
    keyed by gate id, which is exactly where the second spelling hides (it did
    hide there -- ``CLEAN_EXTRA`` carried ``ssl-match-hostname-deleted``
    twice, and the round-2 near-miss fixtures added under the first key were
    silently discarded until this check caught them).

    The module-level walk covers `def`, `class`, `import`, `from ... import`
    and both assignment forms -- every spelling that binds a name in
    ``tree.body``. It deliberately skips module-level `for` targets, which are
    reused between loops on purpose, and the walrus, which cannot appear at
    statement level. Round-3 static review of rung 03 named those two, the
    import forms, and the `GATES_BY_ID` collision below; the import forms and
    the collision are closed here.

    Returns the number of modules scanned, so the selftest line carries it.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    mods = []
    for dirpath, dirnames, filenames in os.walk(here):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for fn in sorted(filenames):
            if fn.endswith(".py"):
                mods.append(os.path.join(dirpath, fn))
    for path in sorted(mods):
        with io.open(path, encoding="utf-8") as fh:
            src = fh.read()
        try:
            tree = ast.parse(src)
        except SyntaxError as exc:
            failures.append("%s does not parse: %s" % (path, exc))
            continue
        rel = os.path.relpath(path, here)
        bound = {}
        for node in tree.body:
            names = []
            if isinstance(node, ast.Assign):
                names = [t.id for t in node.targets if isinstance(t, ast.Name)]
            elif isinstance(node, ast.AnnAssign):
                if isinstance(node.target, ast.Name):
                    names = [node.target.id]
            elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                                   ast.ClassDef)):
                names = [node.name]
            elif isinstance(node, ast.Import):
                # ``import a.b.c`` binds ``a``; ``import a.b as x`` binds ``x``.
                names = [a.asname or a.name.split(".")[0] for a in node.names]
            elif isinstance(node, ast.ImportFrom):
                names = [a.asname or a.name for a in node.names
                         if a.name != "*"]
            for name in names:
                # A decorated registration re-uses `_` on purpose: the case
                # modules bind every case body to it and the decorator keeps
                # the function. Nothing reads the name back.
                if name == "_":
                    continue
                if name in bound:
                    failures.append(
                        "%s: %s defined at line %d is shadowed by the "
                        "definition at line %d"
                        % (rel, name, bound[name], node.lineno))
                bound[name] = node.lineno
        for node in ast.walk(tree):
            if not isinstance(node, ast.Dict):
                continue
            seen = {}
            for key in node.keys:
                if not (isinstance(key, ast.Constant)
                        and isinstance(key.value, str)):
                    continue
                if key.value in seen:
                    failures.append(
                        "%s: dict literal at line %d repeats the key %r "
                        "(line %d then line %d); the later value wins and the "
                        "earlier one is dead"
                        % (rel, node.lineno, key.value, seen[key.value],
                           key.lineno))
                seen[key.value] = key.lineno

    # The third spelling, and the one no AST walk over this file can see:
    # ``GATES_BY_ID`` is built by ``dict((g.gid, g) for g in GATES)``, a call
    # rather than a literal, so two gates sharing an id collide there with the
    # same last-one-wins silence -- and one whole gate would never run while
    # every count in the selftest line stayed right. Counted off the objects
    # instead of parsed. Round-3 static review of rung 03.
    gids = [g.gid for g in GATES]
    if len(set(gids)) != len(gids):
        dupes = sorted(set(g for g in gids if gids.count(g) > 1))
        failures.append(
            "GATES has %d entries but only %d distinct ids; GATES_BY_ID "
            "would silently drop one of each repeated pair: %s"
            % (len(gids), len(set(gids)), ", ".join(dupes)))
    unknown = sorted(g for g in GUARDS if g not in GATES_BY_ID)
    if unknown:
        failures.append("GUARDS names a gate that does not exist: %s"
                        % ", ".join(unknown))
    return len(mods)


def selftest(stream=sys.stdout):
    import shutil
    import tempfile

    root = tempfile.mkdtemp(prefix="gateselftest.")
    failures = []
    try:
        for gid, body in sorted(DIRTY.items()):
            d = os.path.join(root, gid)
            os.makedirs(d)
            # Written under a name the scoped gates accept, so scope cannot be
            # the reason a gate stays silent.
            with open(os.path.join(d, "events.py"), "w") as fh:
                fh.write(body)
            cache, broken = units(d)
            gate = GATES_BY_ID[gid]
            found = gate.run(d, cache) if not broken else [{"file": "?",
                                                            "line": 0,
                                                            "detail": "unparsable"}]
            if not found:
                failures.append("%s did not fire on its own fixture" % gid)

        # One fixture per scored construct, so the easy half of a two-part
        # gate cannot carry the hard half. See DIRTY_EXTRA.
        for gid, variants in sorted(DIRTY_EXTRA.items()):
            for label, body in variants:
                d = os.path.join(root, "%s__%s" % (gid, label))
                os.makedirs(d)
                with open(os.path.join(d, "events.py"), "w") as fh:
                    fh.write(body)
                cache, broken = units(d)
                if broken:
                    failures.append("%s/%s does not parse" % (gid, label))
                    continue
                if not GATES_BY_ID[gid].run(d, cache):
                    failures.append("%s did not fire on its %s fixture"
                                    % (gid, label))

        # The negative half of each narrowed check. A construct that merely
        # resembles the banned one must stay silent, or the narrowing is not
        # actually in force.
        for gid, variants in sorted(CLEAN_EXTRA.items()):
            for label, body in variants:
                d = os.path.join(root, "clean__%s__%s" % (gid, label))
                os.makedirs(d)
                with open(os.path.join(d, "events.py"), "w") as fh:
                    fh.write(body)
                cache, broken = units(d)
                if broken:
                    failures.append("clean %s/%s does not parse" % (gid, label))
                    continue
                hits = GATES_BY_ID[gid].run(d, cache)
                if hits:
                    failures.append("%s fired on its clean %s fixture: %r"
                                    % (gid, label, hits))

        # `annotation-names-bound` is the one gate whose answer depends on a
        # file other than the one it is reading, so it is the one gate the
        # single-file fixtures above cannot pin in both directions. Four files
        # in this tree bind annotation names through `from x import *`, and a
        # gate that could not follow that would report the finished tree; a
        # gate that treated *any* star import as a reason to stay quiet would
        # be switched off by one added line. Both directions, two files each.
        for label, sibling, fires in (
                ("a-sibling-that-exports-the-name",
                 "class Elasticsearch(object):\n    pass\n", False),
                ("a-sibling-whose-__all__-withholds-it",
                 "__all__ = ['ConnectionError']\n\n\n"
                 "class Elasticsearch(object):\n    pass\n", True),
                ("a-sibling-that-does-not-have-it",
                 "class Transport(object):\n    pass\n", True)):
            d = os.path.join(root, "star__%s" % label)
            os.makedirs(d)
            with open(os.path.join(d, "events.py"), "w") as fh:
                fh.write("from .compat import *\n\n\n"
                         "def f(c: Elasticsearch) -> None:\n    pass\n")
            with open(os.path.join(d, "compat.py"), "w") as fh:
                fh.write(sibling)
            cache, broken = units(d)
            if broken:
                failures.append("star fixture %s does not parse" % label)
                continue
            hits = GATES_BY_ID["annotation-names-bound"].run(d, cache)
            if bool(hits) != fires:
                failures.append(
                    "annotation-names-bound %s on star fixture %s"
                    % ("fired" if hits else "stayed silent", label))

        # The two opposed gates, each exempted from the other's clean fixture
        # and asserted to fire on it.
        for tag, body, exempt in (("05", CLEAN_05, "no-future-annotations"),
                                  ("07", CLEAN_07, "future-annotations-present")):
            clean = os.path.join(root, "_clean" + tag)
            os.makedirs(clean)
            with open(os.path.join(clean, "events.py"), "w") as fh:
                fh.write(body)
            cache, broken = units(clean)
            if broken:
                failures.append("clean fixture %s does not parse: %r"
                                % (tag, broken))
            for gate in GATES:
                # A census over 158 files has nothing true to say about a
                # one-file fixture; it gets the synthesised tree below instead.
                if gate.gid == "public-surface-census":
                    continue
                # The idiom instruments judge a whole tree against a target
                # version, and these fixtures carry a coding cookie and an
                # `(object)` base on purpose; they get their own pairs below.
                if gate.gid in IDIOM_TOOL_GATES:
                    continue
                found = gate.run(clean, cache)
                if gate.gid == exempt:
                    if not found:
                        failures.append(
                            "%s did not fire on CLEAN_%s, so the step-05 and "
                            "step-07 requirements are not actually opposed"
                            % (exempt, tag))
                elif found:
                    failures.append("%s fired on clean fixture %s: %s"
                                    % (gate.gid, tag, found[:3]))

        # The trap-9 sweep, both directions. SWEEP_DIRTY plants one reference
        # of every kind the sweep knows how to see; SWEEP_CLEAN names the same
        # constructs in a docstring and a comment and must stay silent, because
        # a sweep that fires on prose is a sweep whose output nobody reads.
        sw = os.path.join(root, "_sweep")
        os.makedirs(sw)
        for name, body in (("dirty.py", SWEEP_DIRTY),
                           ("clean.py", SWEEP_CLEAN)):
            with open(os.path.join(sw, name), "w") as fh:
                fh.write(body)
        buf = io.StringIO()
        if sweep_cases(sw, stream=buf) != 1:
            failures.append("sweep_cases passed its dirty fixture")
        seen = buf.getvalue()
        for want in SWEEP_EXPECT:
            if want not in seen:
                failures.append("sweep_cases missed %s" % want)
        if "clean.py" in seen:
            failures.append("sweep_cases fired on prose: %s" % seen)
        os.remove(os.path.join(sw, "dirty.py"))
        buf = io.StringIO()
        if sweep_cases(sw, stream=buf) != 0:
            failures.append("sweep_cases fired on its clean fixture: %s"
                            % buf.getvalue())

        # The recording sweep, both directions, on the shipped recording
        # rather than a toy: the pin it exists to catch is a *value*, and a
        # fixture built by hand could not have the shape a real observation
        # has. Each dirty variant is one way a retired spelling reaches an
        # expectation; the sweep has to see all four or it is decoration.
        rp = RECORDING_PATH
        if os.path.exists(rp):
            with gzip.open(rp, "rt") as fh:
                base = json.load(fh)["cases"]
            buf = io.StringIO()
            if sweep_recording(rp, stream=buf) != 0:
                failures.append("sweep_recording fired on the shipped "
                                "recording: %s" % buf.getvalue())
            for label, mutate in RECORDING_DIRTY:
                cases = json.loads(json.dumps(base))
                mutate(cases)
                tmp = os.path.join(root, "rec-%s.json.gz" % label)
                with gzip.open(tmp, "wt") as fh:
                    json.dump({"cases": cases}, fh)
                buf = io.StringIO()
                if sweep_recording(tmp, stream=buf) != 1:
                    failures.append("sweep_recording missed %s" % label)
        else:
            failures.append("no recording at %s to sweep" % rp)

        # The idiom instruments: each fires on its dirty fixture, stays silent
        # on its clean one, fires on a file it cannot read, is not silenced by
        # a `# noqa` or by a linter config the tree carries, and reports its
        # own absence rather than a clean answer. See IDIOM_FIXTURES.
        for gid, dirty, clean in IDIOM_FIXTURES:
            gate = GATES_BY_ID[gid]
            for label, body, want in (("dirty", dirty, True),
                                      ("clean", clean, False),
                                      ("latin-1", None, True),
                                      ("noqa", dirty.replace(
                                          "\n", "  # noqa\n", 1), True),
                                      ("config", dirty, True)):
                d = os.path.join(root, "_idiom-%s-%s" % (gid, label))
                os.makedirs(d)
                path = os.path.join(d, "events.py")
                if body is None:
                    with open(path, "wb") as fh:
                        fh.write(b"# -*- coding: iso-8859-1 -*-\n"
                                 b"s = '\xdf'\n")
                else:
                    with open(path, "w") as fh:
                        fh.write(body)
                if label == "config":
                    for cfg in ("ruff.toml", ".ruff.toml"):
                        with open(os.path.join(d, cfg), "w") as fh:
                            fh.write("[lint]\nignore = [\"UP\", \"B\", "
                                     "\"PYI\", \"ASYNC\"]\n")
                    with open(os.path.join(d, ".gitignore"), "w") as fh:
                        fh.write("*.py\n")
                hits = gate.run(d, [])
                if want and not hits:
                    failures.append("%s did not fire on its %s fixture"
                                    % (gid, label))
                if not want and hits:
                    failures.append("%s fired on its clean fixture: %r"
                                    % (gid, hits[:3]))
            keep = os.environ.pop("TYPECHECK_VENV", None)
            os.environ["TYPECHECK_VENV"] = os.path.join(root, "no-such-venv")
            saved = dict((k, os.environ.pop(k)) for k in
                         ("RUFF_BIN", "PYUPGRADE_BIN") if k in os.environ)
            try:
                blind = gate.run(os.path.join(root, "_idiom-%s-clean" % gid),
                                 [])
            finally:
                os.environ.pop("TYPECHECK_VENV", None)
                if keep is not None:
                    os.environ["TYPECHECK_VENV"] = keep
                os.environ.update(saved)
            if not blind:
                failures.append("%s returned the clean answer from an image "
                                "with no instrument in it" % gid)

        # types-check-clean, all three directions. See TYPECHECK_REL.
        tcg = GATES_BY_ID["types-check-clean"]
        for label, body, want in (("dirty", TYPECHECK_DIRTY, True),
                                  ("clean", TYPECHECK_CLEAN, False)):
            d = os.path.join(root, "_typecheck-" + label)
            os.makedirs(os.path.join(d, os.path.dirname(TYPECHECK_REL)))
            with open(os.path.join(d, TYPECHECK_REL), "w") as fh:
                fh.write(body)
            hits = tcg.run(d, [])
            if want and not hits:
                failures.append("types-check-clean did not fire on its dirty "
                                "fixture")
            if not want and hits:
                failures.append("types-check-clean fired on its clean "
                                "fixture: %r" % (hits[:3],))
            if not want:
                # Same tree, no checker reachable. The gate has to report the
                # absence rather than return the clean answer it just gave.
                keep = {k: os.environ.pop(k, None)
                        for k in ("MYPY_BIN", "TYPECHECK_VENV")}
                os.environ["MYPY_BIN"] = os.path.join(d, "no-such-mypy")
                os.environ["TYPECHECK_VENV"] = os.path.join(d, "no-such-venv")
                try:
                    blind = tcg.run(d, [])
                finally:
                    for k, v in keep.items():
                        if v is None:
                            os.environ.pop(k, None)
                        else:
                            os.environ[k] = v
                if not blind:
                    failures.append("types-check-clean returned the clean "
                                    "answer from an image with no checker "
                                    "in it")

        # The census, against a tree synthesised from the census itself. This
        # is the only fixture in here that is generated rather than written
        # out, and it has to be: the thing under test is a 382-name list, and
        # a hand-written stand-in would be testing the stand-in. Building the
        # tree through `iter_public_defs`' own output and reading it back also
        # closes the loop that `build/mksurface.py` opens -- a qualname this
        # walk emits but cannot re-find would make the gate unpassable on a
        # tree that had deleted nothing, and would show up here rather than in
        # a rung-05 run.
        from . import surface
        cens = os.path.join(root, "census")
        by_file = {}
        for name in surface.PUBLIC_DEFS:
            rel, _, qual = name.partition("::")
            cls, _, leaf = qual.rpartition(".")
            by_file.setdefault(rel, {}).setdefault(cls, []).append(leaf)
        for rel, classes in by_file.items():
            path = os.path.join(cens, rel)
            if os.path.dirname(path):
                try:
                    os.makedirs(os.path.dirname(path))
                except OSError:
                    pass
            with open(path, "w") as fh:
                for cls, leaves in sorted(classes.items()):
                    if cls:
                        fh.write("class %s(object):\n" % cls)
                        for leaf in sorted(leaves):
                            fh.write("    def %s(self):\n        pass\n"
                                     % leaf)
                    else:
                        for leaf in sorted(leaves):
                            fh.write("def %s():\n    pass\n" % leaf)
        hits = GATES_BY_ID["public-surface-census"].run(cens, [])
        if hits:
            failures.append("public-surface-census fired on a tree built "
                            "from its own census: %s" % hits[:3])
        # ... now take one public def away three ways: delete it, hide it
        # behind an underscore, and make its file unparsable.
        victim = sorted(surface.PUBLIC_DEFS)[0]
        vrel, _, vqual = victim.partition("::")
        vleaf = vqual.rpartition(".")[2]
        vpath = os.path.join(cens, vrel)
        with open(vpath) as fh:
            body = fh.read()
        for label, mutated in (
                ("deleted", body.replace("def %s(" % vleaf, "def gone%s(" % vleaf)),
                ("underscored", body.replace("def %s(" % vleaf, "def _%s(" % vleaf)),
                ("unparsable", body + "\ndef x(:\n")):
            if mutated == body:
                failures.append("census fixture: %s anchor missed in %s"
                                % (label, vrel))
                continue
            with open(vpath, "w") as fh:
                fh.write(mutated)
            if not GATES_BY_ID["public-surface-census"].run(cens, []):
                failures.append("public-surface-census did not fire with "
                                "`%s` %s" % (vqual, label))
        with open(vpath, "w") as fh:
            fh.write(body)

        # A deletion gate has no dirty *file*; it fires on a path existing.
        # The second ordered_dict row sits outside ``packages/``: the glob was
        # ``**/packages/ordered_dict.py`` until round 5, so without this row a
        # backport moved one directory up would survive the gate. Likewise the
        # second six row, for ``lib/`` copies that are not ``lib/six.py``.
        # Section 7.14.
        for gid, rel in (("six-deleted", "lib/six.py"),
                         ("six-deleted", "lib/requests/packages/six.py"),
                         ("ordered-dict-backport-deleted",
                          "lib/urllib3/packages/ordered_dict.py"),
                         ("ordered-dict-backport-deleted",
                          "lib/elasticsearch/ordered_dict.py"),
                         ("ssl-match-hostname-deleted",
                          "lib/urllib3/packages/ssl_match_hostname/"
                          "_implementation.py")):
            d = os.path.join(root, "del-%s-%s"
                             % (gid, rel.replace("/", "_")))
            os.makedirs(os.path.join(d, os.path.dirname(rel)))
            with open(os.path.join(d, rel), "w") as fh:
                fh.write("x = 1\n")
            if not GATES_BY_ID[gid].run(d, []):
                failures.append("%s did not fire on %s" % (gid, rel))
            os.remove(os.path.join(d, rel))
            if GATES_BY_ID[gid].run(d, []):
                failures.append("%s fired with %s absent" % (gid, rel))
    finally:
        shutil.rmtree(root, ignore_errors=True)

    for rel, pattern, want in MATCH_FIXTURES:
        if match(rel, pattern) is not want:
            failures.append("match(%r, %r) is not %r" % (rel, pattern, want))

    covered = set(DIRTY) | {"six-deleted", "ordered-dict-backport-deleted",
                            "ssl-match-hostname-deleted", "types-check-clean",
                            "public-surface-census"} | IDIOM_TOOL_GATES
    for gate in GATES:
        if gate.gid not in covered:
            failures.append("%s has no fixture" % gate.gid)

    n_shadow = _check_no_shadowed_definitions(failures)

    if failures:
        print("SELFTEST FAILED", file=stream)
        for f in failures:
            print("  " + f, file=stream)
        return 1
    from . import surface
    print("selftest: %d gates; each fires on its own fixture and on each of "
          "%d extra per-construct fixtures, none fires on either clean "
          "fixture nor on any of %d near-miss fixtures, the step-05/07 pair "
          "fires on each other's, the census clears a tree synthesised from "
          "its own %d names and fires on each of three ways to take one "
          "away, `annotation-names-bound` follows a star import into the "
          "sibling that answers it and fires on each of three siblings that "
          "do not, the trap-9 "
          "sweep sees %d planted references and no prose, the recording "
          "sweep clears the shipped recording and catches all %d planted "
          "pins, the path matcher agrees on %d rows, and no name or dict key "
          "is defined twice in any of the %d harness modules"
          % (len(GATES), sum(len(v) for v in DIRTY_EXTRA.values()),
             sum(len(v) for v in CLEAN_EXTRA.values()),
             len(surface.PUBLIC_DEFS),
             len(SWEEP_EXPECT), len(RECORDING_DIRTY),
             len(MATCH_FIXTURES), n_shadow), file=stream)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("tree", nargs="?")
    ap.add_argument("--step")
    ap.add_argument("--census", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--sweep-cases", action="store_true",
                    help="trap 9: no case body may name a construct or module "
                         "some rung of the ladder requires deleted")
    ap.add_argument("--sweep-recording", metavar="RECORDING",
                    help="trap 9, from the recording side: no expectation may "
                         "pin a spelling some rung of the ladder deletes")
    ap.add_argument("--json")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if args.sweep_cases:
        return sweep_cases(args.tree or os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "cases"))
    if args.sweep_recording:
        return sweep_recording(args.sweep_recording)
    if not args.tree:
        ap.error("a tree is required unless --selftest")
    if args.census:
        census(args.tree)
        return 0
    if not args.step:
        ap.error("--step is required; known: %s" % ", ".join(STEP_IDS))
    report = run(args.tree, args.step)
    if args.json:
        with open(args.json, "w") as fh:
            json.dump(report, fh, indent=1, sort_keys=True)
    return 1 if report["n_findings"] else 0


if __name__ == "__main__":
    sys.exit(main())
