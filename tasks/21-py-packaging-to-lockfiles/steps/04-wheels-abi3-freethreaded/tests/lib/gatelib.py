#!/usr/bin/env python3
"""Shared machinery for the migrated-ness gates.

Per-step copy; every step's copy is identical.

Three principles are built in here rather than repeated in every gate:

*Scan everything the candidate wrote.*  ``iter_sources`` walks that tree in
full: vendored trees included, no exclusion list, no checksum carve-out, no
per-file exemption. What it does not walk is the VCS database and the *derived*
directories -- build scratch, caches, installed environments. A file under one
of those is a copy of a file that is already being scanned, so counting it
counts the same file twice; and since those directories are the by-product of
the candidate running the very tools the step asks them to run, counting them
makes the score depend on whether they tidied up rather than on what they
wrote. Hiding source inside a skipped directory buys nothing: the gates that
matter ask what the distribution *ships*, and a file under ``build/`` is not
shipped.

*Read artifacts and ASTs, not text.*  ``import_sites`` parses each module and
reports import statements as syntax, so a string that mentions a module name is
not a finding and an import hidden inside a function or a ``try`` is. Wheels and
sdists are opened as archives and their metadata parsed, never grepped.

*Suppressed findings are findings.*  ``suppression_sites`` counts the comments
and switches that hide a finding from a tool without changing the property the
instruction states, and every gate that reports zero is expected to add that
count back in.
"""
import ast
import email.parser
import os
import re
import tarfile
import zipfile

# Derived directories: not part of the tree a human wrote. Every name below is
# one the toolchain this task actually names -- CPython, setuptools, pip,
# `python -m build` -- creates inside the repository, and none of them occurs
# at any depth in the pinned tree, so pruning them cannot hide a pinned source
# file. Names for tools this task does not use are not listed. The
# same set `step02_gates` uses for its own walk -- one definition, so the same
# candidate directory cannot be part of the tree for one row and not another.
SKIP_DIRS = {
    ".git",           # the VCS database; the environment image builds it
    "build",          # `python -m build` / `setup.py build` scratch
    "dist",           # the default --outdir for wheels and sdists
    "__pycache__",    # bytecode
    ".eggs",          # setuptools' download cache: installed dependencies
    "site-packages",  # an install target, wherever it happens to sit
}


def skip_dir(name):
    """Is this directory name derived rather than written?

    `*.egg-info` is matched by suffix rather than listed, because its name
    carries the distribution's own.
    """
    return name in SKIP_DIRS or name.endswith(".egg-info")


# --------------------------------------------------------------------------
# walking the repository
# --------------------------------------------------------------------------

def iter_files(root, suffixes=None, skip_names=()):
    """Every file under root, sorted, with the derived directories pruned.

    `skip_names` is the caller's own extra directory names, on top of
    `SKIP_DIRS`.

    Virtual environments are pruned by the marker that defines one rather than
    by name: a candidate is free to build one anywhere in the tree while
    working, and the packages it installs are not part of what they wrote.
    """
    extra = set(skip_names)
    for base, dirs, names in os.walk(root):
        if "pyvenv.cfg" in names:
            dirs[:] = []
            continue
        dirs[:] = sorted(d for d in dirs
                         if not skip_dir(d) and d not in extra)
        for name in sorted(names):
            if suffixes and not name.endswith(tuple(suffixes)):
                continue
            yield os.path.join(base, name)


def iter_sources(root):
    """Every Python source file under root."""
    return iter_files(root, (".py", ".pyi"))


def relpath(path, root):
    return os.path.relpath(path, root).replace(os.sep, "/")


def in_package(rel, dotted_or_path):
    """Is `rel` a file inside that package, wherever the package sits?

    A gate that excludes a package from its own scan has to name the package,
    not its address. `rel.startswith("bento/distutils/")` is an address, and
    the address is exactly the thing a later migration step is entitled to
    change -- moving the tree to a `src/` layout turns every one of those paths
    into `src/bento/distutils/...` and the exclusion silently stops matching,
    which turns a passing gate into one that reports the package against
    itself. The package's identity does not move with it, so match on the
    dotted name anywhere in the path instead.

    `dotted_or_path` may be spelled either way: `bento.distutils` and
    `bento/distutils` mean the same package here.
    """
    needle = "/" + dotted_or_path.replace("\\", "/").replace(".", "/").strip("/") + "/"
    return needle in "/" + rel.replace(os.sep, "/")


def parse_source(path):
    """(tree, error). A file that does not parse is reported, never skipped."""
    try:
        with open(path, "rb") as fh:
            raw = fh.read()
        return ast.parse(raw, filename=path), None
    except SyntaxError as exc:
        return None, "SyntaxError: %s" % (exc,)
    except Exception as exc:  # unreadable, undecodable, a directory symlink
        return None, "%s: %s" % (type(exc).__name__, exc)


# --------------------------------------------------------------------------
# import sites
# --------------------------------------------------------------------------

def import_sites(root):
    """Every import statement in the tree, as (relpath, lineno, dotted-name).

    One entry per imported name, so `from distutils.util import a, b` is two
    sites: merging import lines does not change the count. Relative imports
    are reported as `.`-prefixed names and never match a top-level module.
    """
    sites = []
    unparsable = []
    for path in iter_sources(root):
        rel = relpath(path, root)
        tree, error = parse_source(path)
        if tree is None:
            unparsable.append((rel, error))
            continue
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    sites.append((rel, node.lineno, alias.name))
            elif isinstance(node, ast.ImportFrom):
                prefix = "." * (node.level or 0)
                module = node.module or ""
                for alias in node.names:
                    name = "%s%s.%s" % (prefix, module, alias.name) if module \
                        else "%s%s" % (prefix, alias.name)
                    sites.append((rel, node.lineno, name))
    return sites, unparsable


def top_level(dotted):
    return dotted.split(".", 1)[0]


def sites_importing(sites, modules):
    """Sites whose imported name is, or lives under, one of `modules`."""
    wanted = set(modules)
    out = []
    for rel, lineno, name in sites:
        if name.startswith("."):
            continue
        if top_level(name) in wanted:
            out.append((rel, lineno, name))
    return out


# The four calls that are an import: the builtin, and the three `importlib`
# spellings that take a module name and return something loaded. Named once
# because every scan below is built on it and because step 06's own
# `bound_import_sites` reads it too -- a second copy of this list anywhere is a
# second definition of which calls count as an import.
IMPORT_CALL_NAMES = ("__import__", "import_module", "find_spec", "load_module")


def _import_call_name(node):
    """`__import__` / `import_module` / `find_spec` / `load_module`, or None.

    Each of the four is matched by both of its spellings: as an attribute of
    whatever module object holds it -- `importlib.import_module(...)`,
    `importlib.util.find_spec(...)` -- and as a bare name. The bare name is not
    a guess: a file that writes `import_module("x")` bound that name the only
    way it can be bound, with `from importlib import import_module`, and a
    matcher that read only the attribute spelling would be defeated by that one
    import line. `__import__` is a builtin, so it only has the bare spelling.

    The cost of the bare branch is a call to somebody's own `load_module` being
    read as an import. It is a cost worth paying here: what the callers do with
    a match is compare its argument against a needle list, so a call that
    imports nothing named there is not a finding, and a loader method that
    really does import the needle is one.
    """
    if not isinstance(node, ast.Call):
        return None
    func = node.func
    if isinstance(func, ast.Name) and func.id in IMPORT_CALL_NAMES:
        return func.id
    if isinstance(func, ast.Attribute) and func.attr in IMPORT_CALL_NAMES:
        return func.attr
    return None


def literal_import_sites(root):
    """Import-like calls with a literal name, as (rel, lineno, module, call).

    The raw fact and nothing else: the module the call names, spelled as the
    source spells it, and which of `IMPORT_CALL_NAMES` it arrived through. No
    needle list, because the two readings a caller can want out of this scan
    are different readings and a filter here could serve only one of them.
    `dynamic_import_sites` below wants the calls under a top-level package and
    reports each as the call it found; a caller holding an allowlist of module
    names -- step 06's `DISTUTILS_KEPT` is one -- has to test the *name*, and
    recovering it out of a formatted finding would be unparsing a `%r`.

    So the split is the point. One walk of the tree, one matcher, and each
    caller applies the test it actually needs to the fact rather than to a
    string built for a human to read.
    """
    out = []
    for path in iter_sources(root):
        rel = relpath(path, root)
        tree, _error = parse_source(path)
        if tree is None:
            continue
        for node in ast.walk(tree):
            call = _import_call_name(node)
            if call is None or not node.args:
                continue
            first = node.args[0]
            if isinstance(first, ast.Constant) and isinstance(first.value, str):
                out.append((rel, node.lineno, first.value, call))
    return out


def dynamic_import_sites(root, modules):
    """`__import__("x")` and `importlib.import_module("x")` with a literal name.

    An AST gate that only looked at `import` statements would be defeated by
    one line of `importlib`, so the literal-string forms are syntax too.

    A view over `literal_import_sites`, and a narrow one: the match is on the
    *top-level* package rather than on a dotted prefix, so a needle like
    `setuptools._distutils` matches nothing here; and the finding is the call as
    written, `import_module('distutils.dist')`, because what a reader of a
    failure needs to see is the spelling they used. Steps 01 and 02 call this,
    and widening the filter to a prefix test would change what those steps
    count. A caller that needs the module name, or a needle that is not a
    top-level package, calls `literal_import_sites` and writes its own test;
    `step06_gates.gate_imports_gone` is the one that does.
    """
    wanted = set(modules)
    return [(rel, lineno, "%s(%r)" % (call, mod))
            for rel, lineno, mod, call in literal_import_sites(root)
            if top_level(mod) in wanted]


def opaque_import_sites(root, needles):
    """Import-like calls whose module argument is computed, not a literal.

    `dynamic_import_sites` matches `import_module("bento.distutils")` and is
    blind to `import_module(".".join(["bento", "distutils"]))`, which is the
    same import written in two pieces. This reports the calls whose first
    argument is not a string literal and that still carry one of `needles`
    inside a string constant somewhere in that call's own subtree: the name is
    in the syntax, just not in one piece.

    It is not a text search for the module name. gatelib's contract
    is that a docstring mentioning a module is not a finding; the anchor here is
    an import-like call node, and the string constants considered are only the
    ones that call is built out of.
    """
    parts = tuple(needles)
    out = []
    for path in iter_sources(root):
        rel = relpath(path, root)
        tree, _error = parse_source(path)
        if tree is None:
            continue
        for node in ast.walk(tree):
            name = _import_call_name(node)
            if name is None or not node.args:
                continue
            first = node.args[0]
            if isinstance(first, ast.Constant) and isinstance(first.value, str):
                continue                       # dynamic_import_sites' job
            found = sorted(set(
                child.value for child in ast.walk(node)
                if isinstance(child, ast.Constant)
                and isinstance(child.value, str)
                and any(part in child.value for part in parts)))
            if found:
                out.append((rel, node.lineno,
                            "%s(<computed>) over %r" % (name, found[:4])))
    return out


# --------------------------------------------------------------------------
# suppressions
# --------------------------------------------------------------------------

# Comment-level silencers, plus the packaging-specific switches that turn a
# standards check off without changing the property it checks. Each is counted
# per site, not per file.
SUPPRESSION_PATTERNS = [
    ("noqa", re.compile(r"#\s*noqa\b", re.I)),
    ("type-ignore", re.compile(r"#\s*type:\s*ignore\b", re.I)),
    ("pylint-disable", re.compile(r"#\s*pylint:\s*disable", re.I)),
    ("flake8-noqa", re.compile(r"#\s*flake8:\s*noqa", re.I)),
    ("nosec", re.compile(r"#\s*nosec\b", re.I)),
    # The key form -- `no-build-isolation = true` in `[tool.uv]`, `uv.toml` or a
    # pip config file -- and uv's environment variable are the same switch as
    # the flag.
    ("no-build-isolation", re.compile(r"--no-isolation\b|--no-build-isolation\b|"
                                      r"PIP_NO_BUILD_ISOLATION|UV_NO_BUILD_ISOLATION|"
                                      r"no-build-isolation-package|"
                                      r"^\s*no[-_]build[-_]isolation\s*=", re.I)),
    ("skip-dependency-check", re.compile(r"--skip-dependency-check\b", re.I)),
    # `SETUPTOOLS_ENABLE_FEATURES` is not in this alternation: it is
    # setuptools' opt-in feature switch, not the legacy distutils shim, and
    # step 01's "No escapes" paragraph names build isolation, build-dependency
    # checking and the shim -- nothing else.
    ("distutils-hack", re.compile(r"SETUPTOOLS_USE_DISTUTILS|_distutils_hack",
                                  re.I)),
]

# The kinds above that name a *switch* -- a flag or an environment variable
# that changes what the tooling does -- as opposed to the five that are
# comment directives and are therefore suppressions wherever they appear.
# Only these can be demoted to a mention by sitting inside a comment.
SWITCH_KINDS = frozenset(("no-build-isolation", "skip-dependency-check",
                          "distutils-hack"))
# Not counted: `--skip-existing` and `--disable-progress-bar`. Both are
# `twine upload` flags -- they choose what to do about an already-published file
# and whether to draw a progress bar -- and neither turns off a standards check
# or is named by any instruction.


def comment_start(line):
    """Where this line's comment begins, or -1.

    Every file type `suppression_sites` scans uses `#`. A `#` that has no quote
    before it on the line really does open a comment rather than sitting inside
    a string, which is as much lexing as is wanted here: the consequence of
    saying yes is only that a match is recorded rather than penalised, and the
    one way to exploit it -- put the flag after an unquoted `#` -- puts it in a
    comment, where it is not a switch.
    """
    hash_at = line.find("#")
    if hash_at < 0:
        return -1
    head = line[:hash_at]
    if "'" in head or '"' in head:
        return -1
    return hash_at


def suppression_sites(root):
    """(kind, relpath, lineno, text) for every silencing site in the tree.

    A match that begins inside a comment is reported with its kind prefixed
    `comment:`, so that a caller selecting on the bare kinds files it as a
    mention rather than as a switch. Step 01's "No escapes" paragraph says
    *leaving a switch in the tree*; a line that names `--no-build-isolation` in
    order to say not to use it is the opposite of one.
    """
    out = []
    for path in iter_files(root):
        rel = relpath(path, root)
        if not rel.endswith((".py", ".pyi", ".toml", ".cfg", ".ini", ".txt",
                             ".sh", ".yaml", ".yml", ".in")):
            continue
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                lines = fh.read().split("\n")
        except Exception:
            continue
        for number, line in enumerate(lines, 1):
            hash_at = comment_start(line)
            for kind, pattern in SUPPRESSION_PATTERNS:
                # `finditer`, not `search`: a line carrying both a live switch
                # and a trailing comment is judged on the live one. Still one
                # tuple per (kind, line).
                starts = [m.start() for m in pattern.finditer(line)]
                if not starts:
                    continue
                demote = (kind in SWITCH_KINDS
                          and hash_at >= 0 and min(starts) > hash_at)
                out.append(("comment:" + kind if demote else kind,
                            rel, number, line.strip()[:160]))
    return out


# --------------------------------------------------------------------------
# artifacts
# --------------------------------------------------------------------------

def wheel_names(path):
    with zipfile.ZipFile(path) as zf:
        return sorted(zf.namelist())


def wheel_read(path, member):
    with zipfile.ZipFile(path) as zf:
        return zf.read(member)


def wheel_metadata(path):
    """The wheel's core metadata, parsed, plus its WHEEL file as a dict."""
    names = wheel_names(path)
    meta_name = next((n for n in names if n.endswith(".dist-info/METADATA")), None)
    wheel_name = next((n for n in names if n.endswith(".dist-info/WHEEL")), None)
    parser = email.parser.Parser()
    metadata = parser.parsestr(wheel_read(path, meta_name).decode("utf-8")) \
        if meta_name else None
    wheel = {}
    if wheel_name:
        for line in wheel_read(path, wheel_name).decode("utf-8").split("\n"):
            if ":" in line:
                key, _, value = line.partition(":")
                wheel.setdefault(key.strip(), []).append(value.strip())
    return metadata, wheel, names


def wheel_tags(path):
    """The tag triples from the filename, per the binary-distribution spec."""
    base = os.path.basename(path)
    if not base.endswith(".whl"):
        return []
    parts = base[:-4].split("-")
    if len(parts) < 5:
        return []
    pythons, abis, platforms = parts[-3], parts[-2], parts[-1]
    return [(py, abi, plat)
            for py in pythons.split(".")
            for abi in abis.split(".")
            for plat in platforms.split(".")]


def sdist_names(path):
    with tarfile.open(path, "r:*") as tf:
        return sorted(m.name for m in tf.getmembers() if m.isfile())


def sdist_read(path, member):
    with tarfile.open(path, "r:*") as tf:
        handle = tf.extractfile(member)
        return handle.read() if handle else b""


def sdist_metadata(path):
    names = sdist_names(path)
    pkg_info = next((n for n in names if n.count("/") == 1
                     and n.endswith("/PKG-INFO")), None)
    if pkg_info is None:
        return None, names
    return email.parser.Parser().parsestr(
        sdist_read(path, pkg_info).decode("utf-8")), names

