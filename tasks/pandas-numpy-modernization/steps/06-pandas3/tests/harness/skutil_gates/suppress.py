"""Count every way the tree can be made to look clean without being clean.

Rule 11 of the authoring prompt asks for this by name, and the reason is
concrete: every gate in ``rules.py`` can be satisfied by silencing the thing it
measures instead of fixing it. ``# noqa`` turns off ruff, ``filterwarnings =
ignore`` turns off ``-W error``, ``except Exception: pass`` turns off the
exception a migration was supposed to surface, and
``pd.set_option('mode.chained_assignment', None)`` turns off the exact warning
step 4 exists to eliminate.

So the suppression count is measured on the baseline once and carried as a
ceiling: a step passes only if the tree it hands back suppresses no more than
the tree it was handed. Reducing the count is free; raising it costs the gate.

Three passes, each reading the artifact rather than the prose:

* comments, through ``tokenize`` -- so a ``# noqa`` inside a string literal is
  not counted and one at the end of a line of code is;
* runtime silencers, through ``ast``;
* tool configuration, through ``configparser`` and a TOML parser -- each
  individually listed ignore code counts one.
"""
from __future__ import print_function, absolute_import, division

import ast
import io
import os
import re
import tokenize

try:                                   # 3.11+
    import tomllib as _toml
    _TOML_LOADS = lambda s: _toml.loads(s)       # noqa: E731
except ImportError:
    try:
        import tomli as _toml
        _TOML_LOADS = lambda s: _toml.loads(s)   # noqa: E731
    except ImportError:
        _TOML_LOADS = None

# Read by each step's build-time selfcheck. A verifier image on Python < 3.11
# with no `tomli` installed still scans, but it reports one `toml-unreadable`
# finding in place of whatever pyproject.toml actually configures, so its
# suppression ceiling is not comparable with the one measured on the baseline.
# Better to fail the image build than to grade against a different number.
HAVE_TOML = _TOML_LOADS is not None

try:
    import configparser
except ImportError:                    # pragma: no cover - py2 only
    import ConfigParser as configparser

# The marker set. Each is one suppression wherever it appears; ``noqa: A,B``
# counts the codes, because turning off two rules is two suppressions.
_COMMENT_MARKERS = (
    ("noqa", re.compile(r"#\s*(?:ruff|flake8)?\s*:?\s*noqa\b(?P<codes>[:\s]+[A-Z]+[0-9]+(?:\s*,\s*[A-Z]+[0-9]+)*)?",
                        re.IGNORECASE)),
    ("type-ignore", re.compile(r"#\s*type:\s*ignore\b", re.IGNORECASE)),
    ("pragma-no-cover", re.compile(r"#\s*pragma:\s*no\s*cover\b", re.IGNORECASE)),
    ("nosec", re.compile(r"#\s*nosec\b", re.IGNORECASE)),
    ("pylint-disable", re.compile(r"#\s*pylint:\s*disable\b", re.IGNORECASE)),
    ("mypy-ignore", re.compile(r"#\s*mypy:\s*(?:ignore-errors|disable)", re.IGNORECASE)),
)

_CODE_SPLIT = re.compile(r"[A-Z]+[0-9]+", re.IGNORECASE)


def comment_suppressions(text, relpath):
    """Markers in real comments, via the tokenizer rather than a line scan."""
    out = []
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except (tokenize.TokenError, IndentationError, SyntaxError):
        return out
    for tok in toks:
        if tok[0] != tokenize.COMMENT:
            continue
        comment = tok[1]
        for kind, pattern in _COMMENT_MARKERS:
            for match in pattern.finditer(comment):
                weight = 1
                codes = match.groupdict().get("codes") if match.groupdict() else None
                if codes:
                    found = _CODE_SPLIT.findall(codes)
                    weight = max(1, len(found))
                out.append({"kind": kind, "path": relpath, "line": tok[2][0],
                            "detail": comment.strip()[:80], "weight": weight})
    return out


# Only ``ignore`` hides anything. ``error::`` and ``default::`` entries are the
# opposite -- a tree that adds ``error::DeprecationWarning`` is doing this
# step's work, not evading it -- so they must not count against the ceiling.
_IGNORE_ACTIONS = ("ignore",)


def _is_ignore_string(value):
    if not isinstance(value, str):
        return False
    return value.strip().lower().startswith("ignore")


def runtime_suppressions(text, relpath):
    """Calls that silence a diagnostic at run time."""
    out = []
    try:
        tree = ast.parse(text, filename=relpath)
    except SyntaxError:
        return out

    def add(kind, node, detail):
        out.append({"kind": kind, "path": relpath,
                    "line": getattr(node, "lineno", 0),
                    "detail": detail, "weight": 1})

    for node in ast.walk(tree):
        if isinstance(node, ast.Call):
            name = _tail(node.func)
            if name in ("simplefilter", "filterwarnings"):
                # The *action* only. Both signatures take it first, or by
                # keyword; every other parameter is a regex or a category, and
                # `filterwarnings("error", message="ignore stale rows")` is the
                # opposite of a suppression. The instruction says "whose action
                # is `ignore`", so scanning every argument would charge a step
                # for something nobody was told about.
                action = _literal(node.args[0]) if node.args else None
                for kw in node.keywords:
                    if kw.arg == "action":
                        action = _literal(kw.value)
                if _is_ignore_string(action):
                    add("warnings-" + name, node, "warnings." + name)
            elif name == "seterr":
                for arg in list(node.args) + [k.value for k in node.keywords]:
                    if _literal(arg) == "ignore":
                        add("numpy-seterr-ignore", node, "np.seterr(ignore)")
                        break
            elif name == "set_option":
                args = [_literal(a) for a in node.args]
                if "mode.chained_assignment" in args:
                    add("pandas-chained-assignment-off", node,
                        "set_option('mode.chained_assignment')")
            # ``warnings.catch_warnings()`` itself is deliberately not counted.
            # It is equally the wrapper for ``simplefilter('error')``, and
            # penalising it would charge a step for turning a warning *into* a
            # failure. The ``simplefilter``/``filterwarnings`` arm above
            # already counts the silencing half, wrapper or no wrapper.
        elif isinstance(node, ast.ExceptHandler):
            body = [s for s in node.body
                    if not isinstance(s, ast.Expr) or
                    not isinstance(getattr(s, "value", None), ast.Str)]
            if len(body) == 1 and isinstance(body[0], ast.Pass):
                add("except-pass", node, "except ...: pass")
    return out


def _tail(node):
    if isinstance(node, ast.Attribute):
        return node.attr
    if isinstance(node, ast.Name):
        return node.id
    return None


def _literal(node):
    if isinstance(node, ast.Constant):
        return node.value
    cls = node.__class__.__name__
    if cls == "Str":
        return node.s
    if cls == "NameConstant":
        return node.value
    if cls == "Num":
        return node.n
    return None


_INI_KEYS = ("filterwarnings", "ignore", "extend-ignore", "per-file-ignores",
             "exclude_lines", "disable")


def config_suppressions(text, relpath):
    """Ignore lists in setup.cfg / pytest.ini / tox.ini / .coveragerc."""
    out = []
    parser = configparser.RawConfigParser()
    try:
        parser.read_string(text)
    except Exception:
        return out
    for section in parser.sections():
        for key in parser.options(section):
            if key.lower() not in _INI_KEYS:
                continue
            raw = parser.get(section, key) or ""
            for entry in re.split(r"[\n,]", raw):
                entry = entry.strip()
                if not entry:
                    continue
                if key.lower() == "filterwarnings" and \
                        not entry.lower().startswith(_IGNORE_ACTIONS):
                    continue
                out.append({"kind": "config-" + key.lower(), "path": relpath,
                            "line": 0,
                            "detail": "[%s] %s = %s" % (section, key, entry[:40]),
                            "weight": 1})
    return out


def toml_suppressions(text, relpath):
    """The same lists in pyproject.toml.

    An unreadable pyproject is reported as a single suppression named
    ``toml-unreadable`` rather than silently counting zero: a step that cannot
    read the config cannot claim the config is clean.
    """
    if _TOML_LOADS is None:
        return [{"kind": "toml-unreadable", "path": relpath, "line": 0,
                 "detail": "no TOML parser available in this image",
                 "weight": 1}]
    try:
        doc = _TOML_LOADS(text)
    except Exception as exc:
        return [{"kind": "toml-unreadable", "path": relpath, "line": 0,
                 "detail": str(exc)[:80], "weight": 1}]

    out = []

    def walk(node, path):
        if isinstance(node, dict):
            for key, val in sorted(node.items()):
                walk(val, path + [key])
            return
        key = path[-1] if path else ""
        if key.lower() not in _INI_KEYS:
            return
        entries = node if isinstance(node, list) else [node]
        for entry in entries:
            if not isinstance(entry, str):
                continue
            if key.lower() == "filterwarnings" and \
                    not entry.lower().startswith(_IGNORE_ACTIONS):
                continue
            out.append({"kind": "toml-" + key.lower(), "path": relpath,
                        "line": 0,
                        "detail": ".".join(path) + " = " + entry[:40],
                        "weight": 1})

    walk(doc, [])
    return out


_INI_NAMES = ("setup.cfg", "pytest.ini", "tox.ini", ".coveragerc",
              ".flake8", ".pylintrc")


def scan_tree(root, extra_roots=()):
    """Every suppression under *root*, with a total that weights code lists.

    Notebook code cells count too. The instruction says "nothing below may be
    added anywhere in it, in any file", and this tree ships three ``.ipynb``;
    a ceiling that only reads ``.py`` would let a suppression be parked in a
    cell and carried free through every later step. Cells are read through the
    scanner's own ``notebook_source``, so line magics and shell escapes are
    blanked exactly as the AST gates see them, and a notebook whose JSON will
    not load is left to the ``unparsable`` gate rather than counted here.
    """
    from .scan import iter_files, notebook_source

    items = []
    for base in (root,) + tuple(extra_roots):
        if not base or not os.path.isdir(base):
            continue
        label = os.path.basename(os.path.normpath(base))
        for full, rel in iter_files(base):
            tag = rel if base == root else label + "::" + rel
            try:
                with io.open(full, encoding="utf-8") as fh:
                    text = fh.read()
            except (UnicodeDecodeError, IOError):
                continue
            name = os.path.basename(rel)
            if rel.endswith((".py", ".pyx", ".pxd")):
                items.extend(comment_suppressions(text, tag))
            if rel.endswith(".py"):
                items.extend(runtime_suppressions(text, tag))
            if rel.endswith(".ipynb"):
                cells = notebook_source(text)
                if cells is not None:
                    items.extend(comment_suppressions(cells, tag))
                    items.extend(runtime_suppressions(cells, tag))
            if name in _INI_NAMES:
                items.extend(config_suppressions(text, tag))
            if name == "pyproject.toml":
                items.extend(toml_suppressions(text, tag))

    items.sort(key=lambda i: (i["kind"], i["path"], i["line"], i["detail"]))
    total = sum(i["weight"] for i in items)
    by_kind = {}
    for i in items:
        by_kind[i["kind"]] = by_kind.get(i["kind"], 0) + i["weight"]
    return {"total": total, "by_kind": by_kind, "items": items}
