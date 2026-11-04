#!/usr/bin/env python3
"""Step 02 gates: is this a src layout, with one version and a real extension?

Emits one tab-separated `family<TAB>label<TAB>ok<TAB>detail` line per check, the
same contract `step01_gates.py` has with `test.sh`. Every row here is recorded
against family `02`, because step 02 is where the requirement first appears;
step 01's rows are re-run from its own gate script, unchanged, and keep family
`01`.

Nothing here executes candidate code. The tree is read, the archives are opened
as archives, and the one subprocess runs the *pinned* tree's parser over a
*pinned* file. The parts that must run something -- the extension build, the
venv, `bentomaker` -- are driven from `test.sh`, which runs them unprivileged.

Three groups, weighed by `test.sh` on the label prefix:

    layout.    5 rows   where the importable trees live, and that moving them
                        changed neither the wheel nor the imports
    version.   6 rows   one declared source, reported by the installed
                        distribution, and following that metadata when it
                        changes
    hello.    12 rows   the example distribution: declared, ported, built,
                        tagged, installed, extension-backed, and behaving
"""
import ast
import codecs
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, "/tests/lib")
import gatelib  # noqa: E402

# The floor list, imported rather than copied. `CORE_PACKAGES` is the set of
# packages no step in this migration line is allowed to drop, chosen once in
# `step01_gates.py` and used here to ask a different question about the same
# set: not "does the wheel still ship them" but "did they all move".
sys.path.insert(0, "/tests")
import step01_gates  # noqa: E402

try:
    import tomllib
except ImportError:                                  # pragma: no cover
    tomllib = None

ROWS = []


def row(label, ok, detail=""):
    ROWS.append(("02", label, "1" if ok else "0",
                 str(detail).replace("\t", " ")[:300]))


def flush():
    for family, label, ok, detail in ROWS:
        sys.stdout.write("\t".join((family, label, ok, detail)) + "\n")


# Directories that are not part of the tree a human wrote: build scratch, VCS
# metadata, environments, caches. A `pyproject.toml` or a `.py` found under one
# of these is a copy of something else, and counting it twice would make a
# candidate's own build output change their score.
#
# One definition, in gatelib: step 01's whole-tree scans and step 02's walk have
# to agree about what is derived, or the same candidate directory is part of the
# tree for one row and not for another. The names are kept here because the call
# sites below are written in them.
SKIP_DIRS = gatelib.SKIP_DIRS
_skip = gatelib.skip_dir


def walk(root, skip_names=()):
    """Every file under `root`, sorted, with build scratch and VCS skipped.

    Virtual environments are pruned by the marker that defines one rather than
    by name: a candidate is free to build one in the tree while working, and
    the packages it installs are not part of what they wrote.
    """
    return gatelib.iter_files(root, skip_names=skip_names)


def package_roots(tree):
    """Where `bento/` and `bentomakerlib/` actually live, wherever that is.

    The step moves them, a later step may move them again, and "the Python
    modules in the distribution" means these two trees and not the whole
    checkout -- a release script at the repository root is not shipped.
    """
    roots = []
    for path in walk(tree):
        parts = gatelib.relpath(path, tree).split("/")
        if len(parts) >= 2 and parts[-1] == "__init__.py" \
                and parts[-2] in ROOT_TREES:
            here = os.path.dirname(path)
            if not any(here.startswith(r + os.sep) for r in roots):
                roots.append(here)
    return roots


def canonical(value):
    """PEP 503 name comparison, so `Hello` and `hello` are one name."""
    return re.sub(r"[-_.]+", "-", (value or "").strip()).lower()


def load_toml(path):
    """(data, note). `data` is None when there is nothing usable to read."""
    if not os.path.isfile(path):
        return None, "not found"
    try:
        with open(path, "rb") as fh:
            raw = fh.read()
    except Exception as exc:
        return None, "%s: %s" % (type(exc).__name__, exc)
    try:
        return tomllib.loads(raw.decode("utf-8")), "%d bytes" % len(raw)
    except Exception as exc:
        return None, "%s: %s" % (type(exc).__name__, exc)


# --------------------------------------------------------------------------
# the layout
# --------------------------------------------------------------------------

ROOT_TREES = ["bento", "bentomakerlib"]


def gate_layout_tree(tree):
    """The move itself, read off the tree.

    Three separate rows rather than one, because they fail for different
    reasons and a candidate who has done two of the three should be told which
    one is missing: the trees are under `src/`, nothing importable is left at
    the root, and every package that survives all six steps came along.
    """
    src = os.path.join(tree, "src")
    misses = []
    for name in ROOT_TREES:
        if not os.path.isfile(os.path.join(src, name, "__init__.py")):
            misses.append("src/%s/__init__.py is missing" % name)
    # `src` is a directory the backend is pointed at, not a package. An
    # `__init__.py` there makes the distribution ship a package called `src`
    # and every module under it `src.bento.*`, which is the failure this row
    # exists to name.
    if os.path.isfile(os.path.join(src, "__init__.py")):
        misses.append("src/__init__.py exists, so src is itself a package")
    row("layout.src-tree", not misses,
        "; ".join(misses)[:290] if misses
        else "src/bento/ and src/bentomakerlib/ are packages, src/ is not")

    left = []
    for name in ROOT_TREES:
        if os.path.isdir(os.path.join(tree, name)):
            left.append(name + "/")
        if os.path.isfile(os.path.join(tree, name + ".py")):
            left.append(name + ".py")
    row("layout.root-clean", not left,
        "still at the repository root: %r" % (left,) if left
        else "neither bento nor bentomakerlib is at the repository root")

    absent = [pkg for pkg in step01_gates.CORE_PACKAGES
              if not os.path.isfile(os.path.join(src, pkg.replace(".", "/"),
                                                 "__init__.py"))]
    row("layout.core-packages-moved", not absent,
        "%d of %d core packages are not under src/: %r"
        % (len(absent), len(step01_gates.CORE_PACKAGES), absent[:8]) if absent
        else "all %d core packages are under src/"
             % len(step01_gates.CORE_PACKAGES))


def gate_layout_artifacts(wheel, sdist):
    """The move must not be visible in the wheel, and must be visible in the sdist.

    Both halves in one row because they are one property: the layout changed,
    and what the distribution ships did not. A wheel carrying a top-level `src`
    (the standard way to get a src layout wrong -- an `__init__.py` next to the
    trees, or a backend pointed at the repository root) is the failure the
    first half names, and an sdist that still carries `bento/` at the top means
    the tree did not actually move -- it was copied.

    `src` is tested as the *first* path component and not as any component,
    because the repository ships directories genuinely called `src` further
    down: `bento/private/_yaku/examples/.../src/` is vendored third-party
    material whose own layout is not this step's business, and a rule that
    banned it anywhere would be unpassable for a candidate who ships the
    vendored tree whole.

    The top-level inventory is the other half of "the same members at the same
    paths": whatever else moved or did not, the wheel's top-level names must
    still be the two importable trees plus whatever `.dist-info` / `.data`
    directories the wheel format itself contributes. A wheel that has quietly
    grown a third top-level name ships something the pinned one did not.
    """
    faults = []
    if not wheel:
        faults.append("no wheel was built")
    else:
        names = gatelib.wheel_names(wheel)
        bad = [n for n in names if n.split("/")[0] == "src"]
        if bad:
            faults.append("%d wheel member(s) are under a top-level src/: %r"
                          % (len(bad), bad[:4]))
        tops = set(n.split("/")[0] for n in names)
        tops = set(t for t in tops
                   if not t.endswith(".dist-info") and not t.endswith(".data"))
        extra = sorted(tops - set(ROOT_TREES))
        if extra:
            faults.append("the wheel has %d top-level name(s) that are neither "
                          "%s: %r" % (len(extra), " nor ".join(ROOT_TREES),
                                      extra[:4]))
        if "bento/__init__.py" not in names:
            faults.append("the wheel has no bento/__init__.py")
        if "bentomakerlib/__init__.py" not in names:
            faults.append("the wheel has no bentomakerlib/__init__.py")
    if not sdist:
        faults.append("no sdist was built")
    else:
        members = gatelib.sdist_names(sdist)
        prefix = sorted(members)[0].split("/", 1)[0] + "/" if members else ""
        want = [prefix + "src/%s/__init__.py" % n for n in ROOT_TREES]
        missing = [w for w in want if w not in set(members)]
        if missing:
            faults.append("the sdist does not carry %r" % (missing,))
    row("layout.artifacts-unmoved", not faults,
        "; ".join(faults)[:290] if faults
        else "the wheel ships bento/ and bentomakerlib/ and nothing else at "
             "the top level, and the sdist ships them under src/")


# --------------------------------------------------------------------------
# the version
# --------------------------------------------------------------------------

def gate_version_source(tree, version):
    """One declared source: not in Python code, and not the generated module.

    Both halves in one row because they are one property. A tree that still
    imports `__package_info` has a second source of the version whether or not
    that module exists, and a tree with the literal in a module has one it can
    forget to update. The pinned tree carries `0.2.0` in exactly one file,
    `bento.info`, so the literal half is measurable rather than approximate.

    The literal is looked for in the two importable trees -- what the
    distribution ships -- and not in the whole checkout: `examples/` holds
    other distributions' sources (`hello` declares version 1.0), and a release
    script at the repository root is not a module the distribution carries.
    The import half is the whole tree, because the instruction says the whole
    tree.
    """
    faults = []
    quoted = ('"%s"' % version, "'%s'" % version)
    literals = []
    roots = package_roots(tree)
    if not roots:
        faults.append("neither bento/ nor bentomakerlib/ was found anywhere")
    scanned = 0
    for root in roots:
        for path in walk(root):
            if not path.endswith((".py", ".pyi")):
                continue
            scanned += 1
            try:
                with open(path, "rb") as fh:
                    text = fh.read().decode("utf-8", "replace")
            except Exception:
                continue
            if any(q in text for q in quoted):
                literals.append(gatelib.relpath(path, tree))
    if literals:
        faults.append("%d shipped module(s) carry the literal %s: %r"
                      % (len(literals), version, literals[:4]))

    # The generated module, by every name an import can wear: the statement,
    # the literal-string dynamic forms, and an import-like call whose argument
    # is computed but whose subtree still names it.
    needle = "__package_info"
    sites, unparsable = gatelib.import_sites(tree)
    hits = [(rel, line, name) for rel, line, name in sites
            if needle in name.split(".")]
    hits += [(rel, line, name) for rel, line, name in
             gatelib.dynamic_import_sites(tree, [needle])]
    hits += list(gatelib.opaque_import_sites(tree, (needle,)))
    if hits:
        faults.append("%d site(s) import %s: %r" % (len(hits), needle, hits[:4]))
    row("version.single-source", not faults,
        "; ".join(faults)[:290] if faults
        else "none of %d shipped modules carries %s and no site imports %s "
             "(%d imports scanned, %d files unparsable)"
             % (scanned, version, needle, len(sites), len(unparsable)))


# --------------------------------------------------------------------------
# the hello distribution
# --------------------------------------------------------------------------

# Read from the *pinned* example with the *pinned* parser, for the same reason
# `step01_gates.declared_library` reads `bento.info` that way: what the example
# declares about itself is a fact about the repository, not a string this file
# is entitled to invent. The path is the pristine one, which never moves; where
# the candidate put their copy is explicitly theirs to choose, and is found by
# the name it declares.
HELLO_IDENTITY_SNIPPET = """
import json, os, sys
pristine = sys.argv[1]
sys.path[:] = [p for p in sys.path if p not in ("", ".", os.getcwd())]
sys.path.insert(0, pristine)
from bento.core import PackageDescription
pkg = PackageDescription.from_file("bento.info")
json.dump({"name": pkg.name, "version": pkg.version}, sys.stdout)
"""

PINNED_HELLO_DIR = "examples/simples/single_extension"


def hello_identity(pristine):
    """(name, version). A harness fault if the pinned tree cannot be read."""
    where = os.path.join(pristine, PINNED_HELLO_DIR)
    proc = subprocess.run(
        [sys.executable, "-P", "-c", HELLO_IDENTITY_SNIPPET, pristine],
        cwd=where, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
        env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1", PYTHONHASHSEED="0",
                 PYTHONWARNINGS="ignore"))
    if proc.returncode != 0:
        raise SystemExit("HARNESS FAULT: the pinned parser could not read "
                         "%s/bento.info (exit %d): %s"
                         % (PINNED_HELLO_DIR, proc.returncode,
                            proc.stderr.decode("utf-8", "replace")[-300:]))
    parsed = json.loads(proc.stdout.decode("utf-8", "replace"))
    return str(parsed["name"]), str(parsed["version"])


def _sole(pattern, text, what, where):
    """The one match of `pattern` in `text`, or a harness fault."""
    hits = re.findall(pattern, text)
    if len(hits) != 1:
        raise SystemExit("HARNESS FAULT: %s in %s matched %d time(s), not "
                         "exactly once" % (what, where, len(hits)))
    return hits[0]


def pinned_expectations(pristine, version):
    """(hello_c_out, hello_py_out, bannerline): what the pinned tree prints.

    `test.sh` compares what the candidate's installed code writes to stdout
    against these, so none of them may be written down here. The greeting the
    extension prints is the argument of the one `printf` in the pinned
    `hellomodule.c`; the string the pure-Python half prints is the argument of
    the one `print` statement in the pinned `hello/bar.py` -- read as text
    rather than parsed, because that file is Python 2 and this interpreter
    cannot compile it; and the banner is the first line of
    `bento.commands.core.USAGE` with the distribution's version substituted in,
    the same substitution `bentomaker` itself performs.

    Both literals are decoded as the C and Python source-level escapes they
    are, then cut at the first newline, because a caller comparing one line of
    captured stdout wants the first line and not the trailing `\n`.

    Not matching exactly once is a harness fault and not a failed row: the
    pinned tree is the verifier's own input, so a pinned file this cannot read
    means the verifier is broken, not the candidate -- the same treatment
    `hello_identity` gives an unreadable `bento.info`.
    """
    hellodir = os.path.join(pristine, PINNED_HELLO_DIR)

    where = PINNED_HELLO_DIR + "/src/hellomodule.c"
    with open(os.path.join(hellodir, "src", "hellomodule.c"), "rb") as fh:
        ctext = fh.read().decode("utf-8", "replace")
    literal = _sole(r'printf\s*\(\s*"((?:[^"\\]|\\.)*)"\s*\)', ctext,
                    'printf("...")', where)
    c_out = codecs.decode(literal, "unicode_escape").split("\n")[0]

    where = PINNED_HELLO_DIR + "/hello/bar.py"
    with open(os.path.join(hellodir, "hello", "bar.py"), "rb") as fh:
        ptext = fh.read().decode("utf-8", "replace")
    literal = _sole(r'print\s+"((?:[^"\\]|\\.)*)"', ptext, 'print "..."',
                    where)
    py_out = codecs.decode(literal, "unicode_escape").split("\n")[0]

    core = os.path.join(pristine, "bento", "commands", "core.py")
    with open(core, "rb") as fh:
        source = fh.read().decode("utf-8", "replace")
    usage = None
    for node in ast.parse(source).body:
        if not isinstance(node, ast.Assign):
            continue
        if not any(isinstance(t, ast.Name) and t.id == "USAGE"
                   for t in node.targets):
            continue
        if isinstance(node.value, ast.Constant) \
                and isinstance(node.value.value, str):
            usage = node.value.value
    if usage is None:
        raise SystemExit("HARNESS FAULT: bento/commands/core.py has no "
                         "module-level USAGE string assignment")
    # The first line only: the second carries `%(name)s`, which this mapping
    # does not have and which `bentomaker` fills in from argv.
    bannerline = usage.split("\n")[0] % {"version": version}
    return c_out, py_out, bannerline


def locate_hello(tree, name):
    """(dir, note): the one directory whose pyproject declares this name.

    By declaration, not by address. The instruction says the directory may stay
    where it is or move, and that it is found by the name it declares, so this
    reads every `pyproject.toml` in the tree except the repository root's and
    keeps the ones whose `[project] name` is this distribution's.
    """
    found = []
    for path in walk(tree):
        if os.path.basename(path) != "pyproject.toml":
            continue
        if os.path.dirname(path) == tree.rstrip("/"):
            continue                       # the root distribution's own
        data, note = load_toml(path)
        if data is None:
            continue
        declared = ((data.get("project") or {}).get("name"))
        if canonical(declared) == canonical(name):
            found.append(os.path.dirname(path))
    if len(found) == 1:
        return found[0], gatelib.relpath(found[0], tree)
    if not found:
        return None, ("no pyproject.toml outside the repository root declares "
                      "[project] name = %r" % name)
    return None, ("%d directories declare [project] name = %r: %r"
                  % (len(found), name, [gatelib.relpath(f, tree) for f in found]))


# The two PEP 517 backends built for compiled extensions, by the module name a
# `build-backend` key gives them. See gate_hello_declares.
EXTENSION_BACKENDS = ("scikit_build_core.build", "mesonpy")


def gate_hello_declares(tree, hellodir, note, name, version):
    """The declaration: this distribution, this version, a resolvable backend.

    `backend-path` is an in-tree backend, which is the one arrangement the step
    01 instruction already rules out for the root distribution and rules out
    here for the same reason.

    The backend is one of the two the task line names for C extensions --
    scikit-build-core or meson-python. The step 02 instruction says so, and
    steps 03-06 state it again among what still holds.
    """
    if hellodir is None:
        row("hello.declares", False, note)
        return
    data, tnote = load_toml(os.path.join(hellodir, "pyproject.toml"))
    project = (data or {}).get("project") or {}
    build = (data or {}).get("build-system") or {}
    faults = []
    if canonical(project.get("name")) != canonical(name):
        faults.append("[project] name is %r" % (project.get("name"),))
    declared_version = project.get("version")
    dynamic = project.get("dynamic")
    dynamic = set(dynamic) if isinstance(dynamic, list) else set()
    if "version" in dynamic:
        faults.append("version is listed in dynamic")
    elif declared_version != version:
        faults.append("[project] version is %r, the example declares %r"
                      % (declared_version, version))
    requires = build.get("requires")
    if not (isinstance(requires, list) and requires
            and all(isinstance(x, str) and x.strip() for x in requires)):
        faults.append("[build-system] requires is %r" % (requires,))
    backend = build.get("build-backend")
    if not (isinstance(backend, str) and backend.strip()):
        faults.append("[build-system] build-backend is %r" % (backend,))
    if "backend-path" in build:
        faults.append("backend-path declares an in-tree backend: %r"
                      % (build.get("backend-path"),))
    if isinstance(backend, str) and backend.strip() \
            and backend.strip() not in EXTENSION_BACKENDS:
        faults.append("[build-system] build-backend is %r, not scikit-build-core"
                      " (%s) or meson-python (%s)"
                      % (backend, EXTENSION_BACKENDS[0], EXTENSION_BACKENDS[1]))
    if os.path.exists(os.path.join(hellodir, "setup.py")):
        faults.append("setup.py is still there")
    row("hello.declares", not faults,
        "%s: %s" % (note, "; ".join(faults))[:290] if faults
        else "%s declares %s %s, backend %r, %s"
             % (note, name, version, backend, tnote))


def _blank(chunk):
    """`chunk` with everything but its newlines turned into spaces."""
    return "".join(c if c == "\n" else " " for c in chunk)


def strip_c_noise(text):
    """`text` with comments and string/character literals blanked out.

    A small character scanner and not a regex, because the states this has to
    keep apart -- code, `/* */`, `//`, a literal -- are exactly what a single
    pattern gets wrong: a `*/` inside a string literal ends no comment, and a
    `"` inside a comment opens no literal, and each pattern written on its own
    believes the opposite.

    Approximate. It does not implement trigraphs, it does not
    honour a line continuation that carries a `//` comment onto the next line,
    and it treats `\'` in a character literal only as far as the backslash
    escape. The one thing it has to get right is that an identifier written
    inside a comment or inside a literal is not code, so `hello.c-ported`
    cannot be satisfied by a name that appears only in prose -- nor failed by a
    Python 2 spelling mentioned in a comment explaining what was removed.

    Offsets and line numbers are preserved, so a fault found in the stripped
    text still points at the real file.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append(_blank(text[i:end]))
            i = end
        elif ch == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(_blank(text[i:end]))
            i = end
        elif ch == '"' or ch == "'":
            j = i + 1
            while j < n and text[j] != ch:
                j += 2 if text[j] == "\\" else 1
            end = min(j + 1, n)
            out.append(_blank(text[i:end]))
            i = end
        else:
            out.append(ch)
            i += 1
    return "".join(out)


# --------------------------------------------------------------------------
# which branches of the C one build compiles
# --------------------------------------------------------------------------
#
# A row that searches C as text sees every branch of every `#if` chain at once,
# and a file that serves more than one build legitimately carries code another
# build never compiles. If `hello.c-ported` and `04.gil.declares` asked "is
# this spelling anywhere in the file", a Python 2 spelling kept behind
# `#if PY_MAJOR_VERSION < 3`, or a single-phase `PyModule_Create` kept in the
# `#else` of `#ifdef Py_GIL_DISABLED` for the limited-API build, would fail a
# row about a build that never sees it. `active_c_text` blanks the branches a
# stated build configuration does not compile, and leaves everything it cannot
# decide in place -- a condition on a macro the configuration does not name
# keeps all of its branches -- so it only ever removes code that build
# provably does not compile.

PP_UNDEF = "undefined"        # known not to be defined
PP_DEFINED = "defined"        # defined, to a value that is not an integer here

_PP_TOKEN = re.compile(
    r"\s*(?:(0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]*|([A-Za-z_]\w*)|"
    r"(&&|\|\||==|!=|<=|>=|<<|>>|[-+*/%<>!()~&|^]))")


class _PPUnknown(Exception):
    """The expression is outside what this evaluator reads."""


def _pp_tokens(expr):
    out, pos = [], 0
    while pos < len(expr):
        if not expr[pos:].strip():
            break
        match = _PP_TOKEN.match(expr, pos)
        if not match:
            raise _PPUnknown(expr[pos:pos + 20])
        if match.group(1) is not None:
            literal = match.group(1)
            try:
                value = int(literal, 16) if literal[:2] in ("0x", "0X") else \
                    int(literal, 8) if len(literal) > 1 and literal[0] == "0" \
                    else int(literal)
            except ValueError:
                raise _PPUnknown(literal)
            out.append(("num", value))
        elif match.group(2) is not None:
            out.append(("name", match.group(2)))
        else:
            out.append(("op", match.group(3)))
        pos = match.end()
    return out


def _pp_defined(name, config):
    """1, 0, or None when `config` does not say."""
    if name not in config:
        return None
    return 0 if config[name] == PP_UNDEF else 1


def _pp_eval(expr, config):
    """The value of an `#if` expression under `config`: an int, or None.

    Three-valued: a macro `config` does not name is unknown rather than 0, and
    unknown propagates -- except through `&&` and `||`, where a known operand
    can decide the result on its own, as it does for the preprocessor. Anything
    outside integer literals, `defined`, macro names and the C operators --
    a function-like macro call, `__has_include`, a ternary -- is unknown.
    """
    try:
        tokens = _pp_tokens(expr)
    except _PPUnknown:
        return None
    pos = [0]

    def peek():
        return tokens[pos[0]] if pos[0] < len(tokens) else (None, None)

    def take(op=None):
        kind, value = peek()
        if kind is None or (op is not None and (kind, value) != ("op", op)):
            raise _PPUnknown(op)
        pos[0] += 1
        return kind, value

    def binary(a, b, fn):
        # The preprocessor computes in intmax_t; a value past 64 bits is
        # outside what it would compute the same way, so it is unknown.
        if a is None or b is None:
            return None
        try:
            value = int(fn(a, b))
        except (ZeroDivisionError, ValueError, OverflowError):
            return None
        return value if -(1 << 64) < value < (1 << 64) else None

    def primary():
        kind, value = take()
        if kind == "num":
            return value
        if kind == "op" and value == "(":
            inner = lor()
            take(")")
            return inner
        if kind == "name" and value == "defined":
            if peek() == ("op", "("):
                take("(")
                kind, name = take()
                take(")")
            else:
                kind, name = take()
            if kind != "name":
                raise _PPUnknown(name)
            return _pp_defined(name, config)
        if kind == "name":
            if peek() == ("op", "("):
                raise _PPUnknown(value)       # a function-like macro call
            if value not in config:
                return None
            have = config[value]
            if have == PP_UNDEF:
                return 0                      # an undefined name reads as 0
            return have if isinstance(have, int) else None
        raise _PPUnknown(value)

    def unary():
        kind, value = peek()
        if kind == "op" and value in ("!", "-", "+", "~"):
            take()
            inner = unary()
            if inner is None:
                return None
            return {"!": lambda v: int(not v), "-": lambda v: -v,
                    "+": lambda v: v, "~": lambda v: ~v}[value](inner)
        return primary()

    def chain(sub, ops):
        def level():
            left = sub()
            while peek()[0] == "op" and peek()[1] in ops:
                _kind, op = take()
                left = binary(left, sub(), ops[op])
            return left
        return level

    def c_div(a, b):
        quotient = abs(a) // abs(b)           # ZeroDivisionError -> unknown
        return quotient if (a < 0) == (b < 0) else -quotient

    mul = chain(unary, {"*": lambda a, b: a * b, "/": c_div,
                        "%": lambda a, b: a - b * c_div(a, b)})
    add = chain(mul, {"+": lambda a, b: a + b, "-": lambda a, b: a - b})
    shift = chain(add, {"<<": lambda a, b: a << b if 0 <= b < 64 else 1 // 0,
                        ">>": lambda a, b: a >> b if 0 <= b < 64 else 1 // 0})
    rel = chain(shift, {"<": lambda a, b: a < b, ">": lambda a, b: a > b,
                        "<=": lambda a, b: a <= b, ">=": lambda a, b: a >= b})
    eq = chain(rel, {"==": lambda a, b: a == b, "!=": lambda a, b: a != b})
    band = chain(eq, {"&": lambda a, b: a & b})
    bxor = chain(band, {"^": lambda a, b: a ^ b})
    bor = chain(bxor, {"|": lambda a, b: a | b})

    def land():
        left = bor()
        while peek() == ("op", "&&"):
            take()
            right = bor()
            if left == 0 or right == 0:
                left = 0
            elif left is None or right is None:
                left = None
            else:
                left = 1
        return left

    def lor():
        left = land()
        while peek() == ("op", "||"):
            take()
            right = land()
            if (left is not None and left != 0) or \
                    (right is not None and right != 0):
                left = 1
            elif left is None or right is None:
                left = None
            else:
                left = 0
        return left

    try:
        value = lor()
        if pos[0] != len(tokens):
            return None
        return value
    except (_PPUnknown, RecursionError):
        return None


def active_c_text(text, config):
    """(text, blanked): `text` with the branches `config`'s build skips blanked.

    `text` has already been through `strip_c_noise`. `config` maps a macro name
    to an int (defined to that value), `PP_DEFINED` (defined, value not an
    integer) or `PP_UNDEF` (known undefined); a name it does not mention is
    unknown. `#define` and `#undef` in compiled code update it as the file goes,
    so a macro the file defines from a decided condition is decided too; one
    defined or undefined in an undecided branch becomes unknown.

    Every `#if`/`#ifdef`/`#ifndef` chain is followed with three states per
    branch -- compiled, skipped, undecided. Only skipped branches are blanked;
    an undecided branch, and every branch after it that could still be the one
    taken, stays in the text. Lines keep their length and their newlines, so an
    offset into the result is an offset into the file. `blanked` is the number
    of lines removed, for the detail string.
    """
    config = dict(config)
    lines = text.split("\n")
    out = list(lines)
    frames = []                   # [state, taken]: state "on", "off" or "?"
    blanked = 0

    def off(stack):
        return any(state == "off" for state, _taken in stack)

    def unsure(stack):
        return any(state == "?" for state, _taken in stack)

    def branch(value):
        if value is None:
            return ["?", None]
        return ["on", True] if value else ["off", False]

    i = 0
    while i < len(lines):
        last = i
        logical = lines[i]
        while logical.rstrip().endswith("\\") and last + 1 < len(lines):
            last += 1
            logical = logical.rstrip()[:-1] + " " + lines[last]
        directive = re.match(r"\s*#\s*([A-Za-z_]\w*)(.*)$", logical, re.S)
        word = directive.group(1) if directive else None
        rest = directive.group(2).strip() if directive else ""
        # A chain's own lines belong to the code around the chain.
        skip = off(frames[:-1] if word in ("elif", "else", "endif") and frames
                   else frames)
        if word in ("if", "ifdef", "ifndef"):
            if off(frames):
                frames.append(["off", True])          # a chain inside dead code
            elif word == "if":
                frames.append(branch(_pp_eval(rest, config)))
            else:
                name = re.match(r"([A-Za-z_]\w*)", rest)
                value = _pp_defined(name.group(1), config) if name else None
                if word == "ifndef" and value is not None:
                    value = 1 - value
                frames.append(branch(value))
        elif word == "elif" and frames:
            frame = frames[-1]
            if frame[1] is True:
                frame[0] = "off"
            else:
                value = _pp_eval(rest, config)
                if frame[1] is False:
                    frame[:] = branch(value)
                elif value is not None and not value:
                    frame[0] = "off"
                else:
                    frame[0] = "?"
                    frame[1] = True if value else None
        elif word == "else" and frames:
            frame = frames[-1]
            frame[0] = {True: "off", False: "on"}.get(frame[1], "?")
            frame[1] = True
        elif word == "endif" and frames:
            frames.pop()
        elif word in ("define", "undef") and not off(frames):
            name = re.match(r"([A-Za-z_]\w*)(\()?(.*)$", rest, re.S)
            if name:
                if unsure(frames):
                    config.pop(name.group(1), None)
                elif word == "undef":
                    config[name.group(1)] = PP_UNDEF
                elif name.group(2):
                    config[name.group(1)] = PP_DEFINED
                else:
                    value = _pp_eval(name.group(3), config) \
                        if name.group(3).strip() else None
                    config[name.group(1)] = PP_DEFINED if value is None else value
        if skip:
            for k in range(i, last + 1):
                out[k] = " " * len(lines[k])
            blanked += last + 1 - i
        i = last + 1
    return "\n".join(out), blanked


# Every interpreter `hello` is built for here is a Python 3, and that is the
# only thing about them this row's question needs: a Python 2 spelling behind
# `#if PY_MAJOR_VERSION < 3` is compiled by none of them.
PYTHON3_BUILD = {"PY_MAJOR_VERSION": 3}


def gate_hello_c(hellodir):
    """The C, ported rather than replaced.

    `PyInit__bar` is the Python 3 entry point for a module named `_bar` under
    either initialisation protocol, so requiring it does not choose between
    single-phase and PEP 489 multi-phase. `Py_InitModule` and `init_bar` are
    the two Python 2 spellings the file arrives with; a file that still
    compiles either did not compile against this interpreter's C API.

    All three searches run over `strip_c_noise`'d text, so what is asserted is
    about the code and not about the comments around it, and over the branches
    a Python 3 build compiles (`active_c_text`, `PYTHON3_BUILD`), so a Python 2
    spelling kept behind `#if PY_MAJOR_VERSION < 3` is not one the build uses.
    """
    if hellodir is None:
        row("hello.c-ported", False, "the hello distribution was not found")
        return
    path = os.path.join(hellodir, "src", "hellomodule.c")
    if not os.path.isfile(path):
        row("hello.c-ported", False, "src/hellomodule.c is not there")
        return
    with open(path, "rb") as fh:
        text = strip_c_noise(fh.read().decode("utf-8", "replace"))
    text, _blanked = active_c_text(text, PYTHON3_BUILD)
    faults = []
    if not re.search(r"\bPyInit__bar\b", text):
        faults.append("no PyInit__bar entry point")
    for dead in ("Py_InitModule", "init_bar"):
        if re.search(r"\b%s\b" % dead, text):
            faults.append("still calls the Python 2 API %s" % dead)
    row("hello.c-ported", not faults,
        "; ".join(faults)[:290] if faults
        else "src/hellomodule.c defines PyInit__bar and uses no Python 2 C API")


# An importable extension, by the suffixes this interpreter would import. Read
# from the running interpreter rather than written down, so the row states
# "importable here" rather than a guess about a filename.
def extension_suffixes():
    import importlib.machinery
    return tuple(importlib.machinery.EXTENSION_SUFFIXES)


def gate_hello_dist(distdir, sdist_distdir, name, version):
    wheels = sorted(f for f in os.listdir(distdir) if f.endswith(".whl")) \
        if os.path.isdir(distdir) else []
    sdists = sorted(f for f in os.listdir(distdir) if f.endswith(".tar.gz")) \
        if os.path.isdir(distdir) else []
    ok_name = (len(wheels) == 1 and len(sdists) == 1
               and wheels[0].startswith("%s-%s-" % (name, version))
               and sdists[0] == "%s-%s.tar.gz" % (name, version))
    row("hello.wheel-name", ok_name,
        "want %s-%s-*.whl and %s-%s.tar.gz, got %r and %r"
        % (name, version, name, version, wheels, sdists))

    wheel = os.path.join(distdir, wheels[0]) if len(wheels) == 1 else None

    # "The wheel carries compiled code, so it is not none-any." Both fields, not
    # just the platform: a wheel tagged `py3-none-linux_x86_64` claims a
    # platform and a stable-ABI-free pure module in the same breath, and a
    # wheel tagged `cp311-cp311-any` is not a thing an installer can resolve.
    tags = gatelib.wheel_tags(wheel) if wheel else []
    ok_tags = bool(tags) and all(abi != "none" and plat != "any"
                                 for _py, abi, plat in tags)
    row("hello.wheel-tagged", ok_tags,
        "tags %r" % (tags,) if tags else "no wheel to read tags from")

    members = gatelib.wheel_names(wheel) if wheel else []
    suffixes = extension_suffixes()
    ext = [m for m in members
           if m.startswith("hello/_bar") and m.endswith(suffixes)]
    row("hello.extension-member", len(ext) == 1,
        "%r among %d wheel members (importable suffixes %r)"
        % (ext, len(members), list(suffixes)))

    sdist_wheels = sorted(f for f in os.listdir(sdist_distdir)
                          if f.endswith(".whl")) \
        if os.path.isdir(sdist_distdir) else []
    if not wheel or len(sdist_wheels) != 1:
        row("hello.sdist-rebuilds", False,
            "tree wheel %r, wheels rebuilt from the sdist %r"
            % (os.path.basename(wheel or ""), sdist_wheels))
        return
    rebuilt = os.path.join(sdist_distdir, sdist_wheels[0])
    same_name = os.path.basename(wheel) == os.path.basename(rebuilt)
    a = set(n for n in members if not n.endswith("/RECORD"))
    b = set(n for n in gatelib.wheel_names(rebuilt) if not n.endswith("/RECORD"))
    row("hello.sdist-rebuilds", same_name and a == b,
        "name equal=%s; only in tree wheel %r; only in sdist wheel %r"
        % (same_name, sorted(a - b)[:6], sorted(b - a)[:6]))


# --------------------------------------------------------------------------
# entry points
# --------------------------------------------------------------------------

STATIC_LABELS = ["layout.src-tree", "layout.root-clean",
                 "layout.core-packages-moved", "version.single-source",
                 "hello.declares", "hello.c-ported"]
ARTIFACT_LABELS = ["layout.artifacts-unmoved"]
HELLO_DIST_LABELS = ["hello.wheel-name", "hello.wheel-tagged",
                     "hello.extension-member", "hello.sdist-rebuilds"]


def check_emitted(mode, want):
    got = [label for _family, label, _ok, _detail in ROWS]
    if sorted(got) != sorted(want):
        sys.stderr.write(
            "GATE FAULT (%s): emitted %d rows, expected %d; missing %r; "
            "unexpected %r\n"
            % (mode, len(got), len(want), sorted(set(want) - set(got)),
               sorted(set(got) - set(want))))
        raise SystemExit(4)


def main(argv):
    mode = argv[1]

    if mode == "static":
        with open("/tests/data/meta-expected.json") as fh:
            expected = json.load(fh)
        # Everything readable off the tree, run before any build so that a
        # candidate's own build scratch cannot be scored as their source.
        tree, pristine = argv[2], argv[3]
        name, version = hello_identity(pristine)
        c_out, py_out, bannerline = pinned_expectations(pristine,
                                                        expected["version"])
        gate_layout_tree(tree)
        gate_version_source(tree, expected["version"])
        hellodir, note = locate_hello(tree, name)
        gate_hello_declares(tree, hellodir, note, name, version)
        gate_hello_c(hellodir)
        check_emitted(mode, STATIC_LABELS)
        flush()
        # test.sh needs the directory and the identity; stderr keeps them out
        # of the row stream.
        sys.stderr.write("HELLODIR %s\n" % (hellodir or ""))
        sys.stderr.write("HELLO %s %s\n" % (name, version))
        sys.stderr.write("HELLO_C_OUT %s\n" % c_out)
        sys.stderr.write("HELLO_PY_OUT %s\n" % py_out)
        sys.stderr.write("BANNERLINE %s\n" % bannerline)
        return 0

    if mode == "artifacts":
        wheel, sdist = (argv[2] or None), (argv[3] or None)
        gate_layout_artifacts(wheel, sdist)
        check_emitted(mode, ARTIFACT_LABELS)
        flush()
        return 0

    if mode == "hellodist":
        distdir, sdist_distdir = argv[2], argv[3]
        name, version = argv[4], argv[5]
        gate_hello_dist(distdir, sdist_distdir, name, version)
        check_emitted(mode, HELLO_DIST_LABELS)
        flush()
        return 0

    raise SystemExit("unknown mode %r" % mode)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
