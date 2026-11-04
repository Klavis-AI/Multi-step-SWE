"""Walk a tree and produce one finding per matching expression.

Three kinds of artifact are read, and none of them as prose:

``.py``        parsed with ``ast``; rules run over the node graph.
``.ipynb``     the notebook JSON's code cells, concatenated in cell order with
               line magics and shell escapes blanked, then parsed as above.
``.pyx .pxd``  tokenised, not parsed -- Cython is not Python and ``ast`` will
               not take ``cdef``. Only the string-literal rule runs there.

A file that will not parse is itself a finding (``unparsable``): the step's
interpreter has to be able to read every source in the tree, and "it does not
compile" must never be mistaken for "it contains none of the constructs".
"""
from __future__ import print_function, absolute_import, division

import ast
import io
import json
import os
import tokenize

from .astutils import (ImportTable, const_value, dotted,
                       enclosing_functions, version_alias_names)
from . import rules as rules_mod

SKIP_DIRS = frozenset([".git", "__pycache__", ".ipynb_checkpoints", ".eggs",
                       ".pytest_cache", ".mypy_cache", ".ruff_cache",
                       "node_modules", ".tox", ".venv", "htmlcov"])

# Build products, not sources. They are regenerated from the sources the gates
# do read, so scanning them would double-count whatever the generator emitted.
SKIP_SUFFIXES = (".pyc", ".pyo", ".so", ".o", ".a", ".dylib", ".png", ".zip",
                 ".pptx", ".whl", ".gz", ".npy", ".npz")

PY_SUFFIXES = (".py",)
NB_SUFFIXES = (".ipynb",)
CY_SUFFIXES = (".pyx", ".pxd")


class Ctx(object):
    """Everything a rule may ask about the file it is looking at."""

    def __init__(self, path, kind, table, owner, root, version_names=()):
        self.path = path
        self.kind = kind
        self.table = table
        self.owner = owner
        self.root = root
        # Names this module bound to `sys.version_info`. Computed once per
        # file rather than per node: `py2-version-branch` has to know, at a
        # `Compare` on a bare name, whether that name is the interpreter's
        # version, and the binding is usually several statements away.
        self.version_names = frozenset(version_names)


def classify(relpath, package=("skutil",)):
    """library / tests / build / docs / notebook / other.

    ``library`` means "inside the installed package and not a test": the two
    rules that only make sense for library code (pyplot global state, row
    iteration in a hot path) key off it. Everything else is scanned by every
    rule that is not marked ``library_only``.
    """
    parts = relpath.replace(os.sep, "/").split("/")
    base = parts[-1]
    if base.endswith(".ipynb"):
        return "notebook"
    if "tests" in parts or base.startswith("test_") or base == "conftest.py":
        return "tests"
    if base == "setup.py" or "build_tools" in parts or "_build_utils" in parts:
        return "build"
    if parts[0] == "doc" or parts[0] == "docs":
        return "docs"
    if parts[0] in package:
        return "library"
    return "other"


def iter_files(root):
    """Every file under *root*, skipping caches and build products."""
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS
                             and not d.endswith(".egg-info"))
        for name in sorted(filenames):
            if name.endswith(SKIP_SUFFIXES):
                continue
            full = os.path.join(dirpath, name)
            if os.path.islink(full):
                continue
            yield full, os.path.relpath(full, root)


def _read(path):
    with io.open(path, "rb") as fh:
        raw = fh.read()
    for enc in ("utf-8", "latin-1"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return None


def notebook_source(text):
    """Code cells joined in order, with magics blanked.

    Blanked rather than dropped so that a finding's line number still points
    into a recognisable place, and because ``%matplotlib inline`` is not a
    construct any rule here is about.
    """
    try:
        nb = json.loads(text)
    except ValueError:
        return None
    lines = []
    for cell in nb.get("cells", []):
        if cell.get("cell_type") != "code":
            continue
        source = cell.get("source", [])
        # nbformat's `multiline_string`: a cell's source is stored either as a
        # list of lines or as one string, and Jupyter writes either. A string
        # is split the way nbformat itself splits it on write, so both forms
        # read as the same lines (iterating a string would read characters,
        # and blank every `%`, `!` and `?` in the code as if it were a magic).
        if isinstance(source, str):
            source = source.splitlines(True)
        for line in source:
            stripped = line.lstrip()
            if stripped.startswith(("%", "!", "?")) or stripped.endswith("?\n"):
                lines.append("pass" + ("\n" if line.endswith("\n") else ""))
            else:
                lines.append(line)
        if lines and not lines[-1].endswith("\n"):
            lines[-1] = lines[-1] + "\n"
    return "".join(lines)


# --------------------------------------------------------------------------
# the string-escape rule: token level, because it is about the literal's own
# bytes and ``ast`` has already decoded them by the time a rule could look.

_VALID_ESCAPES = set("\n\\'\"abfnrtv01234567xNuU\r")


def string_escape_findings(text, relpath):
    """Literals carrying an escape Python does not define.

    ``'[\\$,%]'`` in this tree is the example: it means a backslash followed by
    a dollar, Python warns that it did not ask for one, and 3.12 turned the
    warning into a SyntaxWarning that ``-W error`` stops the build on.
    """
    out = []
    try:
        readline = io.StringIO(text).readline
        toks = list(tokenize.generate_tokens(readline))
    except (tokenize.TokenError, IndentationError, SyntaxError):
        return out
    for tok in toks:
        if tok[0] != tokenize.STRING:
            continue
        raw = tok[1]
        prefix = raw[:len(raw) - len(raw.lstrip("rRbBuUfF"))]
        if "r" in prefix.lower():
            continue
        body = raw[len(prefix):]
        i = 0
        while i < len(body) - 1:
            if body[i] == "\\":
                nxt = body[i + 1]
                if nxt not in _VALID_ESCAPES:
                    out.append({
                        "rule": "invalid-string-escape",
                        "path": relpath,
                        "line": tok[2][0],
                        "col": tok[2][1],
                        "what": "\\" + nxt,
                    })
                i += 2
            else:
                i += 1
    return out


# --------------------------------------------------------------------------


# How deep a nest of `exec("exec(\'...\')")` is followed. Three is past any
# honest use and stops a crafted file from costing unbounded scan time.
MAX_EXEC_DEPTH = 3

_EXEC_BUILTINS = ("exec", "eval", "compile")


def _exec_sources(tree, table):
    """(source, lineno) for every string literal handed to exec/eval/compile.

    Source in a string is still source. It is not reached by ``ast.walk`` over
    the enclosing module -- the literal is one ``Constant`` node -- so without
    this a gated construct is retired from a gate's point of view by quoting
    it, which changes nothing about what the tree does when it runs.
    """
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call) or not node.args:
            continue
        if table.resolve(dotted(node.func)) not in _EXEC_BUILTINS:
            continue
        text = const_value(node.args[0], None)
        if isinstance(text, str) and text.strip():
            yield text, getattr(node, "lineno", 0)


def scan_source(text, relpath, kind, active, root, inherit=None, depth=0):
    """Run every rule in *active* over one parsed Python source."""
    findings = []
    try:
        tree = ast.parse(text, filename=relpath)
    except SyntaxError as exc:
        findings.append({"rule": "unparsable", "path": relpath,
                         "line": getattr(exc, "lineno", 0) or 0, "col": 0,
                         "what": str(exc.msg)})
        return findings

    table = ImportTable.build(tree)
    if inherit is not None:
        # An exec'd fragment runs in the enclosing module's namespace, so
        # `exec("np.random.seed(0)")` in a file that imported numpy as np is a
        # numpy.random.seed. The fragment's own imports win where both bind.
        for bound, target in inherit.aliases.items():
            table.aliases.setdefault(bound, target)

    ctx = Ctx(relpath, kind, table, enclosing_functions(tree), root,
              version_alias_names(tree, table))

    wanted = {}
    for rule in active:
        if not rule.applies_to(ctx):
            continue
        for node_type in rule.node_types:
            wanted.setdefault(node_type, []).append(rule)

    for node in ast.walk(tree):
        for node_type, rule_list in wanted.items():
            if not isinstance(node, node_type):
                continue
            for rule in rule_list:
                if not rule.owner_ok(ctx, node):
                    continue
                what = rule.match(node, ctx)
                if what is None:
                    continue
                findings.append({
                    "rule": rule.name,
                    "path": relpath,
                    "line": getattr(node, "lineno", 0),
                    "col": getattr(node, "col_offset", 0),
                    "what": what,
                })

    if depth < MAX_EXEC_DEPTH:
        for inner, lineno in _exec_sources(tree, table):
            findings.extend(scan_source(
                inner, "%s<exec:%d>" % (relpath, lineno), kind, active, root,
                inherit=table, depth=depth + 1))
    return findings


def scan_tree(root, step, extra_roots=()):
    """Every finding in force at *step*, over *root* and any extra roots.

    ``extra_roots`` is how the installed package gets scanned alongside the
    working tree. A submission that edits ``/app`` but installs something else
    would otherwise pass a gate the running code fails.
    """
    active = rules_mod.by_step(step)
    findings = []
    files = {"scanned": 0, "python": 0, "notebook": 0, "cython": 0}

    for base in (root,) + tuple(extra_roots):
        if not base or not os.path.isdir(base):
            continue
        label = os.path.basename(os.path.normpath(base))
        for full, rel in iter_files(base):
            text = _read(full)
            if text is None:
                continue
            files["scanned"] += 1
            tag = rel if base == root else label + "::" + rel
            if rel.endswith(PY_SUFFIXES):
                files["python"] += 1
                kind = classify(rel)
                findings.extend(scan_source(text, tag, kind, active, base))
                findings.extend(string_escape_findings(text, tag))
            elif rel.endswith(NB_SUFFIXES):
                files["notebook"] += 1
                src = notebook_source(text)
                if src is None:
                    findings.append({"rule": "unparsable", "path": tag,
                                     "line": 0, "col": 0,
                                     "what": "not valid notebook JSON"})
                    continue
                findings.extend(scan_source(src, tag, "notebook", active, base))
                findings.extend(string_escape_findings(src, tag))
            elif rel.endswith(CY_SUFFIXES):
                files["cython"] += 1
                findings.extend(string_escape_findings(text, tag))

    # A deterministic order, so two scans of the same tree produce byte-equal
    # reports and a diff of two reports is a diff of the tree.
    findings.sort(key=lambda f: (f["rule"], f["path"], f["line"], f["col"],
                                 f["what"]))
    return findings, files


def counts(findings):
    out = {}
    for f in findings:
        out[f["rule"]] = out.get(f["rule"], 0) + 1
    return out
