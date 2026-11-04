# -*- coding: utf-8 -*-
"""Golden solution for rung 07: annotations that are objects, and that resolve.

    python3.14 work/solve07.py <tree>

Applied on top of rung 06's tree, in place. Rung 05 wrote 776 annotations and
then stringised every one of them with ``from __future__ import annotations``;
under PEP 563 that made an annotation the *text* of its expression, for ever,
and nothing ever looked a name up. 3.14 stores annotations lazily instead
(PEP 649/749): the expression is compiled into a function on the object, and
the first caller to read ``__annotations__`` -- or ``typing.get_type_hints``,
or ``dataclasses.fields``, or ``inspect.signature(eval_str=True)``, or any
framework that reads a signature -- runs it, in module globals, at that
moment. So this rung is not "delete an import". It is: every name an
annotation is built out of has to be a name the run time actually binds.

Three rules, derived from the tree by parsing it, and two edits written out,
because they are different kinds of work. The rules are mechanical and cover
185 sites; the two edits are the places where doing the mechanical thing would
be wrong, and each is here with the reason:

  1. ``_drop_future`` deletes ``from __future__ import annotations``. 156
     files, one line each, the whole of the own surface and nothing else.

  2. ``_hoist_type_checking`` lifts each module-level ``if TYPE_CHECKING:``
     block into the module: the imports become imports the run time makes, and
     the ``Protocol`` / ``TypedDict`` classes and PEP 695 aliases declared
     inside them become objects that exist. Both halves matter. An annotation
     naming ``HttpResponse`` is as unresolvable as one naming ``Mapping`` if
     the alias was only ever declared for a checker.

     Module level only. ``connection/base.py`` has a second block *inside*
     ``class Connection`` that declares what ``perform_request`` looks like
     without giving the base a method it has never had at run time; lifting
     that one would add a method to the class's run-time surface, which is a
     change to the library rather than to how its annotations are stored.
     It stays, and ``_final_scan`` checks that it stays.

  3. ``_drop_type_checking_import`` removes ``TYPE_CHECKING`` from the
     ``typing`` import in the files where rule 2 left nothing using it. The
     name stays importable and stays imported in ``connection/base.py``,
     which still has a block.

  EDITS[0]  ``client/utils.py`` cannot import ``Elasticsearch`` the way the
            block did. ``elasticsearch/__init__.py`` imports ``.client``
            first, ``client/__init__.py`` imports ``.utils`` last, so at the
            moment ``utils`` runs, the package object exists and the name
            ``Elasticsearch`` is not on it yet: a module-level
            ``from .. import Elasticsearch`` is an ImportError, not a slow
            import. ``import elasticsearch`` binds the half-built module
            itself, and an annotation spelled ``elasticsearch.Elasticsearch``
            defers the attribute lookup to the moment something reads the
            annotation -- by which time the package is complete. This is the
            one place where the shape of the annotation changes, and it is
            the reason the rung is not a one-line sed.

  EDITS[1]  ``lib/analytics/consumer.py`` returns from a ``finally``. PEP 765
            makes that a ``SyntaxWarning`` on 3.14 and the warning is right:
            the ``return`` discards whatever was in flight, including a
            ``KeyboardInterrupt`` or a ``SystemExit`` raised by the upload.
            Dedenting it out of the block is the fix, and it changes an
            observable -- ``analytics.consumer-finally-return-swallows`` is
            recorded on 3.8 with the exception swallowed and flips here.

``_final_scan`` then re-parses and re-imports the finished tree: no future
import, no module-level ``TYPE_CHECKING`` import block, no control flow
leaving a ``finally``, the one class-body block still in place, and all
sixteen public modules imported on this interpreter with every public
annotation an object and every one of them resolvable by
``typing.get_type_hints``. The rules say what was written; only that pass says
what was left (INSTRUCTIONS trap 7), and only the last part of it says the
rung's requirement rather than its diff.
"""

import ast
import os
import sys


FUTURE = "from __future__ import annotations"

#: The public modules the run-time half of the scan imports, identical to
#: ``harness/cases/annotations.py``'s list and fixed for the same reason: a
#: list built by walking the tree would shrink the moment a module was deleted
#: and would then report the deletion as a clean answer.
MODULES = (
    "elasticsearch",
    "elasticsearch.client.utils",
    "elasticsearch.compat",
    "elasticsearch.connection",
    "elasticsearch.connection.base",
    "elasticsearch.connection.http_requests",
    "elasticsearch.connection.http_urllib3",
    "elasticsearch.connection.memcached",
    "elasticsearch.connection.pooling",
    "elasticsearch.connection.thrift",
    "elasticsearch.connection_pool",
    "elasticsearch.exceptions",
    "elasticsearch.helpers",
    "elasticsearch.helpers.test",
    "elasticsearch.serializer",
    "elasticsearch.transport",
)

#: The class-body block rule 2 must leave alone, named so that the scan can
#: tell "correctly left" from "never found".
CLASS_BLOCK = ("lib/elasticsearch/connection/base.py", "Connection")

EDITS = (
    # The import cycle. Anchored on the text rule 2 leaves behind, so this
    # runs second and a change to rule 2's output is a loud failure here.
    ("lib/elasticsearch/client/utils.py",
     b"from .. import Elasticsearch\n"
     b"from ..transport import Transport\n",
     b"import elasticsearch\n"
     b"from ..transport import Transport\n", 1),
    ("lib/elasticsearch/client/utils.py",
     b"    def __init__(self, client: Elasticsearch):\n",
     b"    def __init__(self, client: elasticsearch.Elasticsearch):\n", 1),
    ("lib/elasticsearch/client/utils.py",
     b"    def infect_client(cls, client: Elasticsearch) -> Elasticsearch:\n",
     b"    def infect_client(\n"
     b"            cls, client: elasticsearch.Elasticsearch\n"
     b"    ) -> elasticsearch.Elasticsearch:\n", 1),
    # PEP 765.
    ("lib/analytics/consumer.py",
     b"        finally:\n"
     b"            # cleanup\n"
     b"            for item in batch:\n"
     b"                self.queue.task_done()\n"
     b"\n"
     b"            return success\n",
     b"        finally:\n"
     b"            # cleanup\n"
     b"            for item in batch:\n"
     b"                self.queue.task_done()\n"
     b"\n"
     b"        return success\n", 1),
)


def _py_files(tree):
    """Every ``.py`` file in the tree, in a stable order."""
    for dirpath, dirnames, names in os.walk(tree):
        dirnames[:] = sorted(d for d in dirnames if d != "__pycache__")
        for name in sorted(names):
            if name.endswith(".py"):
                yield os.path.join(dirpath, name)


def _is_type_checking(test):
    """``TYPE_CHECKING`` or ``typing.TYPE_CHECKING``, either spelling."""
    if isinstance(test, ast.Name):
        return test.id == "TYPE_CHECKING"
    return isinstance(test, ast.Attribute) and test.attr == "TYPE_CHECKING"


#: Statement attributes holding a nested list of statements, as in
#: ``harness/gates.py``.
_STMT_BODIES = ("body", "handlers", "orelse", "finalbody", "cases")


def _module_level(body):
    """Every statement the import of the module runs.

    Wider than ``tree.body``: a block under a ``try:`` still executes on
    import. Narrower than ``ast.walk``: what is inside a class or a function
    runs later, under a scope of its own, and the one block in this tree that
    lives in a class body is a declaration rather than an import.
    """
    for node in body:
        yield node
        if isinstance(node, (ast.ClassDef, ast.FunctionDef,
                             ast.AsyncFunctionDef)):
            continue
        for field in _STMT_BODIES:
            sub = getattr(node, field, None)
            if isinstance(sub, list):
                for found in _module_level(sub):
                    yield found


def _cut(lines, i0, i1):
    """Delete ``lines[i0:i1]``, and the blank line it would strand.

    Removing an import from between two blank lines leaves two blank lines
    where the language wanted one. The rule is deliberately narrow -- one
    blank after, and only when what comes before is a blank line or the start
    of the file -- so that it cannot eat a blank line that was separating two
    things rather than padding one.
    """
    trailing = (i1 < len(lines) and lines[i1].strip() == ""
                and (i0 == 0 or lines[i0 - 1].strip() == ""))
    del lines[i0:i1 + (1 if trailing else 0)]


def _rewrite(text, rel, stats):
    """Rules 1, 2 and 3 over one file's source. Returns the new source."""
    tree = ast.parse(text)
    lines = text.splitlines(True)
    cuts = []          # (start_index, stop_index, replacement_lines)
    hoisted_tests = []

    for node in _module_level(tree.body):
        if (isinstance(node, ast.ImportFrom) and node.module == "__future__"
                and any(a.name == "annotations" for a in node.names)):
            if len(node.names) != 1:
                raise SystemExit("%s:%d: `from __future__` names more than "
                                 "annotations; this rule would delete the "
                                 "rest" % (rel, node.lineno))
            cuts.append((node.lineno - 1, node.end_lineno, []))
            stats["future"] += 1
        elif isinstance(node, ast.If) and _is_type_checking(node.test):
            body = lines[node.lineno:node.end_lineno]
            if not any(isinstance(s, (ast.Import, ast.ImportFrom))
                       for s in node.body):
                raise SystemExit("%s:%d: a module-level TYPE_CHECKING block "
                                 "with no import in it -- unexpected, and the "
                                 "reason for lifting it would be different"
                                 % (rel, node.lineno))
            out = []
            for line in body:
                if line.strip() and not line.startswith("    "):
                    raise SystemExit("%s:%d: block body is not indented four "
                                     "spaces" % (rel, node.lineno))
                out.append(line[4:] if line.strip() else line)
            cuts.append((node.lineno - 1, node.end_lineno, out))
            hoisted_tests.append(node.test)
            stats["blocks"] += 1
            stats["lifted"] += sum(
                1 for s in node.body
                if isinstance(s, (ast.Import, ast.ImportFrom)))

    # Rule 3, decided against the original tree: every remaining use of the
    # name is a use rule 2 is not removing.
    if hoisted_tests:
        left = [n for n in ast.walk(tree)
                if isinstance(n, ast.Name) and n.id == "TYPE_CHECKING"
                and not any(n is t for t in hoisted_tests)]
        if not left:
            for node in _module_level(tree.body):
                if not (isinstance(node, ast.ImportFrom)
                        and node.module == "typing"
                        and any(a.name == "TYPE_CHECKING"
                                for a in node.names)):
                    continue
                rest = [a for a in node.names if a.name != "TYPE_CHECKING"]
                if rest:
                    kept = ", ".join(
                        a.name if not a.asname else "%s as %s" % (a.name,
                                                                  a.asname)
                        for a in rest)
                    cuts.append((node.lineno - 1, node.end_lineno,
                                 ["from typing import %s\n" % kept]))
                else:
                    cuts.append((node.lineno - 1, node.end_lineno, []))
                stats["unimported"] += 1

    for i0, i1, repl in sorted(cuts, reverse=True):
        if repl:
            lines[i0:i1] = repl
        else:
            _cut(lines, i0, i1)
    return "".join(lines)


def _apply_rules(tree):
    stats = {"files": 0, "future": 0, "blocks": 0, "lifted": 0,
             "unimported": 0}
    for path in _py_files(tree):
        with open(path, "rb") as fh:
            raw = fh.read()
        if FUTURE.encode() not in raw and b"TYPE_CHECKING" not in raw:
            continue
        rel = os.path.relpath(path, tree)
        text = raw.decode("utf-8")
        out = _rewrite(text, rel, stats)
        if out == text:
            raise SystemExit("%s: matched the scan and changed nothing" % rel)
        with open(path, "wb") as fh:
            fh.write(out.encode("utf-8"))
        stats["files"] += 1
    return stats


def _apply_edits(tree):
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
    return len(EDITS)


def _finally_control_flow(parsed):
    """Line numbers of ``return``/``break``/``continue`` in a ``finally``."""
    out = []
    for node in ast.walk(parsed):
        if not isinstance(node, ast.Try) or not node.finalbody:
            continue
        for stmt in node.finalbody:
            for inner in ast.walk(stmt):
                if isinstance(inner, (ast.Return, ast.Break, ast.Continue)):
                    out.append(inner.lineno)
    return out


def _static_scan(tree):
    bad = []
    seen_class_block = False
    for path in _py_files(tree):
        rel = os.path.relpath(path, tree)
        with open(path, "rb") as fh:
            src = fh.read()
        try:
            parsed = ast.parse(src)
        except SyntaxError as exc:
            bad.append("%s: does not parse: %s" % (rel, exc))
            continue
        for node in _module_level(parsed.body):
            if (isinstance(node, ast.ImportFrom)
                    and node.module == "__future__"
                    and any(a.name == "annotations" for a in node.names)):
                bad.append("%s:%d: still imports annotations from __future__"
                           % (rel, node.lineno))
            if isinstance(node, ast.If) and _is_type_checking(node.test):
                for stmt in ast.walk(node):
                    if isinstance(stmt, (ast.Import, ast.ImportFrom)):
                        bad.append("%s:%d: module-level TYPE_CHECKING block "
                                   "still holds an import"
                                   % (rel, stmt.lineno))
                        break
        for lineno in _finally_control_flow(parsed):
            bad.append("%s:%d: control flow leaves a finally block"
                       % (rel, lineno))
        if rel.replace(os.sep, "/") == CLASS_BLOCK[0]:
            for node in ast.walk(parsed):
                if (isinstance(node, ast.ClassDef)
                        and node.name == CLASS_BLOCK[1]
                        and any(isinstance(s, ast.If)
                                and _is_type_checking(s.test)
                                for s in node.body)):
                    seen_class_block = True
    if not seen_class_block:
        bad.append("%s: class %s no longer declares its interface under "
                   "`if TYPE_CHECKING:` -- lifting that block gives the base "
                   "a run-time method it never had"
                   % (CLASS_BLOCK[0], CLASS_BLOCK[1]))
    return bad


#: Rung 05's definition of the public surface, which this scan has to agree
#: with or it checks a different set of objects than the rung was written
#: about. A solver ships beside a tree with no ``/tests`` to import, so it
#: carries its own copy -- and the copy is named ``_`` plus the gate
#: constant's own name, which is the convention ``build/sync.sh`` pairs them
#: up by and refuses to install a solver that disagrees.
_PUBLIC_DUNDERS = ("__init__", "__enter__", "__exit__", "__iter__",
                   "__next__", "__call__")


def _public(name):
    return not name.startswith("_") or name in _PUBLIC_DUNDERS


def _runtime_scan(tree):
    """Import the sixteen public modules and read what rung 05 declared.

    The static half above is about the source. This half is the requirement:
    an annotation is an object, and something can resolve it. A tree whose
    annotations are objects nobody can resolve has moved the failure rather
    than removed it, and the only way to find that out is to ask.
    """
    import typing

    bad = []
    sys.dont_write_bytecode = True
    for entry in (os.path.join(tree, "lib"), tree):
        if entry not in sys.path:
            sys.path.insert(0, entry)
    for name in MODULES:
        try:
            mod = __import__(name, {}, {}, ["*"])
        except Exception as exc:
            bad.append("%s: does not import: %s: %s"
                       % (name, type(exc).__name__, exc))
            continue
        objs = []
        for attr in dir(mod):
            if not _public(attr):
                continue
            obj = getattr(mod, attr, None)
            if getattr(obj, "__module__", None) != name:
                continue
            if isinstance(obj, type):
                objs.append(("%s.%s" % (name, attr), obj))
                for sub in vars(obj).values():
                    if (callable(sub)
                            and getattr(sub, "__module__", None) == name
                            and _public(getattr(sub, "__name__", "_"))):
                        objs.append(("%s.%s.%s" % (name, attr,
                                                   sub.__name__), sub))
            elif callable(obj):
                objs.append(("%s.%s" % (name, attr), obj))
        for label, obj in objs:
            try:
                raw = dict(getattr(obj, "__annotations__", {}) or {})
            except Exception as exc:
                bad.append("%s: __annotations__ raises: %s: %s"
                           % (label, type(exc).__name__, exc))
                continue
            for key, value in raw.items():
                if isinstance(value, str):
                    bad.append("%s: annotation %s is still a string (%r)"
                               % (label, key, value))
            if not raw:
                continue
            try:
                typing.get_type_hints(obj)
            except Exception as exc:
                bad.append("%s: get_type_hints raises: %s: %s"
                           % (label, type(exc).__name__, exc))
    return bad


def _final_scan(tree):
    bad = _static_scan(tree) + _runtime_scan(tree)
    if bad:
        raise SystemExit("solve07: final scan failed:\n  "
                         + "\n  ".join(bad))


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: solve07.py <tree>")
    if sys.version_info < (3, 14):
        raise SystemExit("solve07.py checks its own work by reading "
                         "annotations the way 3.14 stores them; run it on "
                         "3.14, not %d.%d" % sys.version_info[:2])
    tree = os.path.abspath(argv[1])

    stats = _apply_rules(tree)
    edits = _apply_edits(tree)
    _final_scan(tree)
    print("solve07: %d files, %d future imports deleted, %d TYPE_CHECKING "
          "blocks lifted (%d imports), %d now-unused imports dropped, "
          "%d edits; %d modules import with every public annotation an "
          "object that resolves"
          % (stats["files"], stats["future"], stats["blocks"],
             stats["lifted"], stats["unimported"], edits, len(MODULES)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
