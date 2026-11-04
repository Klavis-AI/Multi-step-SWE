#!/usr/bin/env python3
"""Step 03 gates: one workspace, one lock, and nothing vendored.

Emits one tab-separated `family<TAB>label<TAB>ok<TAB>detail` line per check,
the same contract `step01_gates.py` and `step02_gates.py` have with `test.sh`.
Every row here is recorded against family `03`, because step 03 is where the
requirement first appears; the earlier steps' rows are re-run from their own
gate scripts and keep families `01` and `02`.

Nothing here executes candidate code and nothing here runs `uv`. The tree is
read, `uv.lock` is parsed as TOML, and the five rows that need a resolver, a
network or an interpreter -- `lock.up-to-date`, `lock.offline-sync`,
`groups.install`, `unvendor.own-code-kept`, `unvendor.real-dists` -- are driven
from `test.sh`, which runs them unprivileged.

Four groups, weighed by `test.sh` on the label prefix:

    lock.        5 rows   the lockfile exists, is hash-pinned, is current, can
                          build an environment with the index unreachable, and
                          has replaced the requirements files outright
    workspace.   3 rows   the members resolve, `yaku` is one of them and is
                          resolved from this repository, and step 02's `hello`
                          distribution is in the same workspace
    groups.      3 rows   `dev` and `doc` are declared, one group includes
                          another through PEP 735's mechanism, and every group
                          installs
    unvendor.    4 rows   the four bundled trees are gone, the `sys.path`
                          apparatus with them, Bento's own `private` modules
                          survive, and the dependencies arrive as distributions

Ten of the fifteen are emitted here; the other five are recorded by `test.sh`
after it has run something.
"""
import ast
import glob
import json
import os
import re
import sys

sys.path.insert(0, "/tests/lib")
import gatelib  # noqa: E402

# `canonical`, `load_toml`, `walk` and `locate_hello` are step 02's, imported
# rather than copied. `locate_hello` in particular is the definition of "the
# hello distribution" that step 02 scores against, and a second copy here would
# let this step disagree with the step whose requirement it is inheriting.
sys.path.insert(0, "/tests")
import step02_gates  # noqa: E402

canonical = step02_gates.canonical
load_toml = step02_gates.load_toml
walk = step02_gates.walk
locate_hello = step02_gates.locate_hello

# The root distribution's own name, read from the recording rather than
# written down: every step that checks a METADATA field takes its expectation
# from `meta-expected.json`, which `lib/record_meta.py` produced by
# asking the pinned tree's own metadata generators.
_ROOT_NAME = []


def root_name():
    if not _ROOT_NAME:
        with open("/tests/data/meta-expected.json") as fh:
            _ROOT_NAME.append(canonical(json.load(fh)["name"]))
    return _ROOT_NAME[0]


ROWS = []


def row(label, ok, detail=""):
    ROWS.append(("03", label, "1" if ok else "0",
                 str(detail).replace("\t", " ")[:300]))


def flush():
    for family, label, ok, detail in ROWS:
        sys.stdout.write("\t".join((family, label, ok, detail)) + "\n")


# --------------------------------------------------------------------------
# scanning the tree for text
# --------------------------------------------------------------------------

# Prose is not code, and two of the rows below are whole-tree bans on a string.
#
# The verifier itself installs `DOCS/03-locking-and-workspaces.md` into `/app`,
# and that document quotes the `sys.path` loop verbatim -- including the string
# `BENTO_UNBUNDLE` -- because a candidate cannot be asked to remove an
# apparatus nobody showed them. The step also requires an entry in
# `WORKSPACE_HANDOFF.md` describing what changed, and the natural way to write
# that entry names the thing that was removed.
#
# A ban that counted prose would therefore fail the reference solution, fail
# every candidate who documented their work, and pass the one who documented
# nothing. The bans are about what the code does, so the scan is about code.
#
# `.txt` is in the list because the instruction's exemption is "prose is not
# code" and this repository's plain-text documents -- `TESTING.txt`,
# `PACKAGERS.txt`, `LICENSE.txt` -- are prose with that suffix. The instruction
# names the same three forms, so the candidate and the scan agree on what a
# document is. `.txt.orig` is there because `walk` also sees the `.orig` files
# `bento/parser/tests/` ships.
PROSE_SUFFIXES = (".md", ".rst", ".txt", ".txt.orig")

# Prose inside a file that is read as code. The suffix list above exempts whole
# documents; it does not exempt the comment a candidate writes beside the line
# they changed, such as
#
#     # dev_requirements26.txt: "-r dev_requirements.txt", then unittest2.
#
# in `pyproject.toml`, recording what the group replaced. That is the same
# candidate the paragraph above says a ban must not punish -- the one
# who documented their work -- so the scan blanks comments and Python docstrings
# before it matches. Nothing else is blanked: a string that is an argument is
# code, so `subprocess.run(["pip", "install", "-r", "dev_requirements.txt"])` is
# still a site.
#
# `#` starts a comment in every language this repository installs anything from
# -- `tox.ini`, `.travis.yml`, `pyproject.toml`, the shell scripts, Python -- and
# a preprocessor directive in C, so the C family is left alone. Neither pattern
# in this file has anything to look for in an `#include`, and `strip_c_noise` is
# what reads C.
HASH_IS_DIRECTIVE = (".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp")


def _uncomment(line):
    """`line` truncated at the first `#` that begins a comment.

    Quote-aware, because a `#` inside a quoted span is data -- `-r "#odd.txt"`
    is a strange argument and still an argument. Truncating rather than dropping
    the line keeps everything to the left of the hash, so a real requirement
    with a trailing comment after it is still found.
    """
    quote = None
    for i, ch in enumerate(line):
        if quote is not None:
            if ch == quote:
                quote = None
        elif ch in "'\"":
            quote = ch
        elif ch == "#":
            return line[:i]
    return line


def _blank_spans(lines, spans):
    """`lines` with each (srow, scol, erow, ecol) span overwritten by spaces.

    Overwritten rather than removed so every line number stays what it was: a
    detail line that says `pyproject.toml:74` has to point at line 74 of the
    file the reader will open.
    """
    for srow, scol, erow, ecol in spans:
        for row in range(srow, erow + 1):
            if not 1 <= row <= len(lines):
                continue
            line = lines[row - 1]
            start = scol if row == srow else 0
            end = ecol if row == erow else len(line)
            start = max(0, min(start, len(line)))
            end = max(start, min(end, len(line)))
            lines[row - 1] = line[:start] + " " * (end - start) + line[end:]
    return lines


def _python_code_only(text):
    """`text` with its comments and docstrings blanked, or None if it won't parse.

    Two passes, because the two kinds of prose are found by different tools: the
    tokenizer knows which `#` is a comment (and gets the quoting right for
    free), and only the AST knows which string literal is a docstring rather
    than an argument.
    """
    try:
        import ast
        import io
        import tokenize
        tree = ast.parse(text)
        toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except (SyntaxError, ValueError, tokenize.TokenError, IndentationError,
            MemoryError, RecursionError):
        return None
    spans = [(t.start[0], t.start[1], t.end[0], t.end[1])
             for t in toks if t.type == tokenize.COMMENT]
    holders = (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)
    for node in ast.walk(tree):
        if not isinstance(node, holders):
            continue
        body = getattr(node, "body", None) or []
        if not body:
            continue
        first = body[0]
        if not (isinstance(first, ast.Expr)
                and isinstance(first.value, ast.Constant)
                and isinstance(first.value.value, str)):
            continue
        end_row = getattr(first, "end_lineno", None)
        end_col = getattr(first, "end_col_offset", None)
        if end_row is None or end_col is None:
            continue
        spans.append((first.lineno, first.col_offset, end_row, end_col))
    lines = _blank_spans(text.split("\n"), spans)
    return "\n".join(lines)


def code_only(rel, text):
    """`text` with the prose in it blanked, keyed on what kind of file it is."""
    low = rel.lower()
    if low.endswith(HASH_IS_DIRECTIVE):
        return text
    if low.endswith(".py"):
        stripped = _python_code_only(text)
        if stripped is not None:
            return stripped
    if low.endswith((".ini", ".cfg")):
        # An INI file -- `tox.ini`, `setup.cfg` -- also opens a full-line
        # comment with `;`, and a comment there is prose like any other. The
        # line is blanked rather than dropped so line numbers stay put.
        text = "\n".join("" if line.lstrip().startswith(";") else line
                         for line in text.split("\n"))
    return "\n".join(_uncomment(line) for line in text.split("\n"))

# A file big enough to be an artifact rather than a source. Reading a 40 MB
# `.exe` looking for an identifier costs time and finds nothing; the five
# `wininst-*.exe` binaries this distribution ships are package data.
MAX_SCAN_BYTES = 2 * 1024 * 1024


def regex_sites(tree, pattern, limit=6):
    """(relpath, lineno, matched-text) for a regex, prose excluded.

    Prose means two things: a whole document, by suffix, and the comments and
    docstrings inside everything else -- see `code_only`.

    Stops at `limit` hits: the detail line has room for a handful, and the row
    says the same thing whether there are three sites or thirty.
    """
    hits = []
    for path in walk(tree):
        rel = gatelib.relpath(path, tree)
        if rel.lower().endswith(PROSE_SUFFIXES):
            continue
        try:
            if os.path.getsize(path) > MAX_SCAN_BYTES:
                continue
            with open(path, "rb") as fh:
                raw = fh.read()
        except OSError:
            continue
        text = raw.decode("utf-8", "replace")
        # The raw search is a fast reject only: blanking prose can remove a
        # match and never add one, so a pattern absent from the raw bytes is
        # absent from the code as well.
        if not pattern.search(text):
            continue
        text = code_only(rel, text)
        for lineno, line in enumerate(text.split("\n"), 1):
            match = pattern.search(line)
            if match:
                hits.append((rel, lineno, match.group(0)[:40]))
                if len(hits) >= limit:
                    return hits
    return hits


def walk_dirs(tree):
    """Every directory under `tree`, relative, with the derived ones pruned.

    `gatelib.iter_files` yields files, and an emptied directory has none. The
    pruning is the same: build scratch, VCS metadata and anything with a
    `pyvenv.cfg` in it are not part of what the candidate wrote.
    """
    for base, dirs, names in os.walk(tree):
        if "pyvenv.cfg" in names:
            dirs[:] = []
            continue
        dirs[:] = sorted(d for d in dirs if not gatelib.skip_dir(d))
        for name in dirs:
            yield gatelib.relpath(os.path.join(base, name), tree)


# --------------------------------------------------------------------------
# the lock
# --------------------------------------------------------------------------

LOCKFILE = "uv.lock"

# A source table with any of these keys is a distribution that came out of this
# repository rather than off an index: uv spells a workspace member `editable`
# or `virtual`, and a plain path dependency `directory`. Anything else --
# `registry`, `url`, `git` -- was fetched, and the lock is only a record of a
# resolution if it says exactly which bytes were fetched.
LOCAL_SOURCE_KEYS = ("editable", "virtual", "directory", "path")


def lock_packages(data):
    packages = (data or {}).get("package")
    return packages if isinstance(packages, list) else []


def is_local(package):
    source = package.get("source")
    if not isinstance(source, dict):
        return False
    return any(key in source for key in LOCAL_SOURCE_KEYS)


def lock_entry(data, name):
    for package in lock_packages(data):
        if canonical(package.get("name")) == canonical(name):
            return package
    return None


def gate_lock_exists(tree):
    """A lockfile at the repository root that parses and describes packages."""
    path = os.path.join(tree, LOCKFILE)
    data, note = load_toml(path)
    if data is None:
        row("lock.exists", False, "%s: %s" % (LOCKFILE, note))
        return None
    faults = []
    if not isinstance(data.get("version"), int):
        faults.append("no integer `version` key")
    packages = lock_packages(data)
    if not packages:
        faults.append("no [[package]] entries")
    root = root_name()
    if packages and lock_entry(data, root) is None:
        faults.append("no [[package]] named %r" % root)
    row("lock.exists", not faults,
        "%s (%s), %d package(s): %s" % (LOCKFILE, note, len(packages),
                                        "; ".join(faults) or "ok"))
    return data


def gate_lock_hashed(data):
    """Every distribution that came off an index is pinned to exact bytes.

    The exact version is asked of those distributions and not of the
    workspace's own members, because that is what the instruction asks ("an
    exact version and a hash for every distribution that comes from an
    index"): uv writes no `version` for a member whose version is `dynamic`,
    and how a member states its version is not this row's question.
    """
    if data is None:
        row("lock.hashed", False, "there is no lockfile to read")
        return
    remote, unhashed, unversioned = [], [], []
    for package in lock_packages(data):
        name = package.get("name")
        if is_local(package):
            continue
        if not isinstance(package.get("version"), str):
            unversioned.append(name)
        remote.append(name)
        sdist = package.get("sdist")
        wheels = package.get("wheels")
        have = False
        if isinstance(sdist, dict):
            have = bool(sdist.get("hash"))
            if not have:
                unhashed.append("%s (sdist)" % name)
                continue
        if isinstance(wheels, list) and wheels:
            missing = [w for w in wheels
                       if not (isinstance(w, dict) and w.get("hash"))]
            if missing:
                unhashed.append("%s (%d wheel(s))" % (name, len(missing)))
                continue
            have = True
        if not have:
            unhashed.append("%s (neither sdist nor wheels)" % name)
    faults = []
    if not remote:
        faults.append("no package in the lock comes from an index, so nothing "
                      "in it is a resolution of anything")
    if unhashed:
        faults.append("%d without a hash: %s"
                      % (len(unhashed), ", ".join(sorted(unhashed)[:5])))
    if unversioned:
        faults.append("%d without a version: %s"
                      % (len(unversioned), ", ".join(
                          sorted(str(n) for n in unversioned)[:5])))
    row("lock.hashed", not faults,
        "%d indexed package(s): %s" % (len(remote), "; ".join(faults) or "all "
                                       "carry an exact version and a hash"))


# The three files the dependency groups replace, and the shape of a line that
# installs from one. The names are matched, not the whole path: `tox.ini` spells
# it `-r{toxinidir}/dev_requirements.txt` and pip spells it `-rdev_requirements.txt`.
# The optional quote after the flag is there because a shell script that quotes
# its argument -- `pip install -r "dev_requirements.txt"` -- is installing from
# the file exactly as much as one that does not.
REQUIREMENTS_FILES = ("dev_requirements.txt", "dev_requirements26.txt",
                      "doc_requirements.txt")
INSTALLS_FROM = re.compile(
    r"(?:-r|--requirement[=\s])\s*['\"]?[^\s'\"]*"
    r"(?:dev_requirements26|dev_requirements|doc_requirements)\.txt")


def gate_lock_no_requirements(tree):
    """The files are gone and nothing left in the tree installs from them.

    Two questions, because either alone is passable by doing half the work: a
    tree that deletes the files but keeps `tox.ini`'s `deps = -r...` has a
    broken test runner, and a tree that rewrites `tox.ini` but keeps the files
    still carries a second, unlocked statement of what it depends on.
    """
    survivors = sorted(gatelib.relpath(p, tree) for p in walk(tree)
                       if os.path.basename(p) in REQUIREMENTS_FILES)
    sites = regex_sites(tree, INSTALLS_FROM)
    faults = []
    if survivors:
        faults.append("still present: %s" % ", ".join(survivors[:5]))
    if sites:
        faults.append("still installed from: %s"
                      % "; ".join("%s:%d %s" % s for s in sites[:4]))
    row("lock.no-requirements-txt", not faults,
        "; ".join(faults) or "the three requirements files are gone and "
        "nothing installs from them")


# --------------------------------------------------------------------------
# the workspace
# --------------------------------------------------------------------------

def workspace_table(tree):
    data, note = load_toml(os.path.join(tree, "pyproject.toml"))
    if data is None:
        return None, None, "pyproject.toml: %s" % note
    table = ((data.get("tool") or {}).get("uv") or {}).get("workspace")
    if not isinstance(table, dict):
        return data, None, "no [tool.uv.workspace] table in pyproject.toml"
    return data, table, note


def resolve_members(tree, table):
    """([(canonical-name, reldir, absdir)], [faults]) for the declared members.

    The members are globs relative to the workspace root, which is what uv
    documents them as, so this resolves them rather than comparing strings: a
    candidate who writes `members = ["packages/*"]` has declared the same
    workspace as one who writes the directories out.
    """
    faults = []
    patterns = table.get("members")
    if not isinstance(patterns, list) or not patterns:
        return [], ["[tool.uv.workspace] members is %r" % (patterns,)]
    excluded = set()
    for pattern in (table.get("exclude") or []):
        if isinstance(pattern, str):
            excluded.update(glob.glob(pattern, root_dir=tree))
    rels = []
    for pattern in patterns:
        if not isinstance(pattern, str):
            faults.append("member pattern %r is not a string" % (pattern,))
            continue
        hits = [m for m in sorted(glob.glob(pattern, root_dir=tree))
                if os.path.isdir(os.path.join(tree, m))]
        if not hits:
            faults.append("member pattern %r matches no directory" % pattern)
            continue
        rels.extend(hits)
    members = []
    for rel in sorted(set(rels) - excluded):
        absdir = os.path.join(tree, rel)
        data, note = load_toml(os.path.join(absdir, "pyproject.toml"))
        name = ((data or {}).get("project") or {}).get("name")
        if not name:
            faults.append("member %s declares no [project] name (%s)"
                          % (rel, note))
            continue
        members.append((canonical(name), rel, absdir))
    return members, faults


# The distribution this step moves out of `bento/private/` and into the
# workspace. The instruction names it -- "declared in its own `pyproject.toml`,
# named `yaku`" -- and says nothing about where the directory goes, so the name
# is asserted and the path never is.
YAKU = "yaku"


def gate_workspace_members(tree, table, note, members, faults):
    if table is None:
        row("workspace.members", False, note)
        return
    names = [name for name, _rel, _abs in members]
    problems = list(faults)
    if YAKU not in names:
        problems.append("no member declares [project] name = %r; members "
                        "declare %r" % (YAKU, names[:6]))
    declared_root = canonical(
        ((load_toml(os.path.join(tree, "pyproject.toml"))[0] or {})
         .get("project") or {}).get("name"))
    if declared_root != root_name():
        problems.append("the workspace root declares [project] name = %r"
                        % (declared_root,))
    if len(members) + 1 < 3:
        problems.append("%d distribution(s) in the workspace, counting the "
                        "root" % (len(members) + 1))
    row("workspace.members", not problems,
        "; ".join(problems)[:290] if problems
        else "%d member(s) beside the root: %s"
             % (len(members), ", ".join("%s (%s)" % (n, r)
                                        for n, r, _a in members)[:200]))


def _source_is_local(entry, tree):
    """Is this `[tool.uv.sources]` value a pointer into this repository?

    `{ workspace = true }` is the workspace spelling; `{ path = ... }` and
    `{ editable = ... }` are the directory spellings. uv also allows a list of
    tables differing by marker, so a list is any-of. What is rejected is the
    absence of an entry, which means the dependency comes off an index.
    """
    if isinstance(entry, list):
        return any(_source_is_local(item, tree) for item in entry)
    if not isinstance(entry, dict):
        return False
    if entry.get("workspace") is True:
        return True
    for key in ("path", "editable", "directory"):
        value = entry.get(key)
        if isinstance(value, str):
            here = os.path.realpath(os.path.join(tree, value))
            root = os.path.realpath(tree)
            return here == root or here.startswith(root + os.sep)
    return False


def gate_workspace_yaku(tree, data, lock):
    """`yaku` is required by the root and resolved from this repository."""
    project = ((data or {}).get("project") or {})
    sources = (((data or {}).get("tool") or {}).get("uv") or {}).get("sources")
    sources = sources if isinstance(sources, dict) else {}
    faults = []
    requires = [r for r in (project.get("dependencies") or [])
                if isinstance(r, str)]
    if not any(canonical(requirement_name(r)) == YAKU for r in requires):
        faults.append("[project] dependencies does not require %r: %r"
                      % (YAKU, requires[:8]))
    entry = None
    for key, value in sources.items():
        if canonical(key) == YAKU:
            entry = value
            break
    if entry is None:
        faults.append("[tool.uv.sources] has no %r entry, so it would be "
                      "resolved from an index" % YAKU)
    elif not _source_is_local(entry, tree):
        faults.append("[tool.uv.sources] %s = %r is not a pointer into this "
                      "repository" % (YAKU, entry))
    package = lock_entry(lock, YAKU) if lock is not None else None
    if package is None:
        faults.append("the lock has no package named %r" % YAKU)
    elif not is_local(package):
        faults.append("the lock resolves %s from %r" % (YAKU,
                                                        package.get("source")))
    row("workspace.yaku-path-source", not faults,
        "; ".join(faults)[:290] if faults
        else "%s is required by %s and locked as %r"
             % (YAKU, root_name(), (package or {}).get("source")))


# Where the vendored copy of yaku's importable package was, and therefore what
# the member that replaces it has to contain.
YAKU_PRISTINE_PKG = os.path.join("bento", "private", "_yaku", "yaku")


def _top_modules(root):
    """The `*.py` basenames directly inside a package directory.

    Directly, and not recursively, because yaku's own `tests/` and `tools/`
    subpackages are not what makes yaku yaku: a candidate who drops the test
    suite while porting the packaging has still moved the build system.
    """
    if not os.path.isdir(root):
        return set()
    return set(name for name in os.listdir(root) if name.endswith(".py"))


def _all_modules(root):
    """Every `*.py` basename anywhere under a package directory, to its path.

    The candidate's side of the comparison is recursive so that reorganising --
    a module pushed down into a subpackage -- is not scored as a deletion.
    Shallowest wins when a basename occurs twice, for the same reason
    `_find_package` sorts by depth: the top-level module is the one the name
    refers to.
    """
    paths = {}
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if not gatelib.skip_dir(d)]
        for name in files:
            if not name.endswith(".py"):
                continue
            full = os.path.join(base, name)
            keep = paths.get(name)
            if keep is None or full.count(os.sep) < keep.count(os.sep):
                paths[name] = full
    return paths


SUBSTANCE = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef,
             ast.Assign, ast.AnnAssign)


def _has_substance(path):
    """Does this module define anything at all at its top level?

    Not a content comparison -- the step does not freeze yaku's source and a
    candidate may rewrite any of it. The question is only whether the file is a
    module or a placeholder: an empty file, or one holding nothing but a
    docstring or a `pass`, defines no name and cannot be the module whose name
    it carries. Import statements do not count, because `import os` is what a
    placeholder that wants to look occupied would contain.

    Unparseable is False and says so, which is the same answer the row wants.
    """
    tree, error = gatelib.parse_source(path)
    if tree is None:
        return False, error
    if any(isinstance(node, SUBSTANCE) for node in tree.body):
        return True, ""
    return False, "defines nothing at its top level"


def _find_package(memberdir, package):
    """The importable `<package>/` directory inside a member, shallowest first.

    The step does not say where inside its own distribution a member puts its
    package -- `src/yaku`, `yaku/`, anywhere -- so it is found rather than
    assumed.
    """
    found = []
    for base, dirs, files in os.walk(memberdir):
        dirs[:] = [d for d in dirs if not gatelib.skip_dir(d)]
        if os.path.basename(base) == package and "__init__.py" in files:
            found.append(base)
    found.sort(key=lambda p: (p.count(os.sep), p))
    return found[0] if found else None


def gate_workspace_yaku_real(tree, pristine, members):
    """The member named `yaku` is Bento's build system, not a name.

    `workspace.yaku-path-source` asks whether something called `yaku` is
    required by the root and resolved from this repository, and every part of
    that is satisfiable by an empty distribution: nothing else in this step
    reads yaku's code statically. A member that declares the name and ships
    nothing would collect the whole `workspace` group while the build system it
    is supposed to carry was deleted. So the modules are counted, against the
    copy the pinned tree carries.

    The recording is not blind to yaku -- `bentomaker` imports
    `bento.backends.yaku_backend`, which imports `yaku.context`, so an empty
    member measurably costs `01.install.console-script`, both `02.version` rows
    and every module-level global. That is a backstop and not this row's job.

    Presence by name, not bytes: this step does not freeze yaku's source, and a
    candidate is free to move a module into a subpackage while porting the
    packaging around it. What a name cannot be is empty -- seventeen
    correctly-named zero-byte files would pass a basename count -- so each
    module the pinned tree gives a top-level
    definition to must have one too. Which definitions is not asked.
    """
    faults = []
    memberdir = None
    for name, _rel, absdir in members:
        if canonical(name) == YAKU:
            memberdir = absdir
            break
    want = _top_modules(os.path.join(pristine, YAKU_PRISTINE_PKG))
    if not want:
        faults.append("HARNESS FAULT: the pinned tree has no %s"
                      % YAKU_PRISTINE_PKG)
    elif memberdir is None:
        faults.append("no workspace member declares the name %r" % YAKU)
    else:
        pkgdir = _find_package(memberdir, YAKU)
        if pkgdir is None:
            faults.append("the %r member at %s has no importable %s/ package"
                          % (YAKU, gatelib.relpath(memberdir, tree), YAKU))
        else:
            have = _all_modules(pkgdir)
            missing = sorted(want - set(have))
            if missing:
                faults.append("%s/ is missing %d of the %d modules "
                              "%s carries: %s"
                              % (gatelib.relpath(pkgdir, tree), len(missing),
                                 len(want), YAKU_PRISTINE_PKG,
                                 ", ".join(missing[:6])))
            else:
                hollow = []
                for name in sorted(want):
                    wanted, _why = _has_substance(
                        os.path.join(pristine, YAKU_PRISTINE_PKG, name))
                    if not wanted:          # pristine `__init__.py` is empty
                        continue
                    ok, why = _has_substance(have[name])
                    if not ok:
                        hollow.append("%s (%s)" % (name, why))
                if hollow:
                    faults.append("%s/ has all %d module names but %d of them "
                                  "are placeholders: %s"
                                  % (gatelib.relpath(pkgdir, tree), len(want),
                                     len(hollow), ", ".join(hollow[:4])))
    row("workspace.yaku-real", not faults,
        "; ".join(faults)[:290] if faults
        else "the %r member carries all %d modules of %s"
             % (YAKU, len(want), YAKU_PRISTINE_PKG))


def gate_workspace_hello(tree, members, hellodir, hellonote, hello_name, lock):
    """Step 02's example distribution is a member of the same workspace."""
    faults = []
    if hellodir is None:
        faults.append(hellonote)
    else:
        rel = gatelib.relpath(hellodir, tree)
        member_rels = [r for _n, r, _a in members]
        if not any(os.path.realpath(a) == os.path.realpath(hellodir)
                   for _n, _r, a in members):
            faults.append("%s declares %r but is not a workspace member; "
                          "members are %r" % (rel, hello_name, member_rels[:6]))
    package = lock_entry(lock, hello_name) if lock is not None else None
    if package is None:
        faults.append("the lock has no package named %r" % hello_name)
    elif not is_local(package):
        faults.append("the lock resolves %s from %r"
                      % (hello_name, package.get("source")))
    row("workspace.hello-member", not faults,
        "; ".join(faults)[:290] if faults
        else "%s is a member and is locked as %r"
             % (hello_name, (package or {}).get("source")))


# --------------------------------------------------------------------------
# the dependency groups
# --------------------------------------------------------------------------

# PEP 735 names the table and the include mechanism; the step's instruction
# names these two groups and says the `doc` one provides Sphinx.
REQUIRED_GROUPS = ("dev", "doc")
DOC_GROUP = "doc"
DOC_PROVIDES = "sphinx"

# The name at the head of a dependency specifier, which is all these rows ask
# about: `sphinx>=7`, `sphinx [docs] >= 7 ; python_version > "3"` and `Sphinx`
# are one requirement on one distribution.
_REQUIREMENT_HEAD = re.compile(r"^\s*([A-Za-z0-9][A-Za-z0-9._-]*)")


def requirement_name(spec):
    match = _REQUIREMENT_HEAD.match(spec or "")
    return match.group(1) if match else ""


def group_table(data):
    table = (data or {}).get("dependency-groups")
    return table if isinstance(table, dict) else None


def group_closure(table, name, seen=None):
    """Every requirement name in a group, following `include-group` edges.

    Returns None on a cycle, which PEP 735 forbids and uv refuses to resolve.
    """
    seen = set() if seen is None else seen
    key = canonical(name)
    if key in seen:
        return None
    seen = seen | {key}
    names = []
    for entry in (table.get(name) or []):
        if isinstance(entry, str):
            names.append(canonical(requirement_name(entry)))
        elif isinstance(entry, dict) and "include-group" in entry:
            target = _declared_as(table, entry["include-group"])
            if target is None:
                return None
            inner = group_closure(table, target, seen)
            if inner is None:
                return None
            names.extend(inner)
    return names


def _declared_as(table, name):
    """The key of `table` that names this group, compared PEP 503-style."""
    for key in table:
        if canonical(key) == canonical(name):
            return key
    return None


def gate_groups_declared(data):
    table = group_table(data)
    if table is None:
        row("groups.declared", False,
            "no [dependency-groups] table in pyproject.toml")
        return []
    faults = []
    for wanted in REQUIRED_GROUPS:
        if _declared_as(table, wanted) is None:
            faults.append("no %r group; declared: %r"
                          % (wanted, sorted(table)[:8]))
    for name, entries in sorted(table.items()):
        if not isinstance(entries, list):
            faults.append("group %r is %r, not a list" % (name, entries))
            continue
        for entry in entries:
            if isinstance(entry, str):
                if not requirement_name(entry):
                    faults.append("group %r has an unreadable entry %r"
                                  % (name, entry))
            elif isinstance(entry, dict):
                target = entry.get("include-group")
                if set(entry) != {"include-group"} or not isinstance(target, str):
                    faults.append("group %r has a table entry %r that is not "
                                  "an include-group" % (name, entry))
                elif _declared_as(table, target) is None:
                    faults.append("group %r includes undeclared group %r"
                                  % (name, target))
            else:
                faults.append("group %r has entry %r" % (name, entry))
    doc = _declared_as(table, DOC_GROUP)
    if doc is not None:
        contents = group_closure(table, doc)
        if contents is None:
            faults.append("the %r group's include-group edges do not "
                          "terminate" % DOC_GROUP)
        elif DOC_PROVIDES not in contents:
            faults.append("the %r group does not provide %s: %r"
                          % (DOC_GROUP, DOC_PROVIDES, contents[:8]))
    row("groups.declared", not faults,
        "; ".join(faults)[:290] if faults
        else "groups %r" % (sorted(table),))
    return sorted(table)


def gate_groups_include(data):
    """One group still extends another, through the mechanism rather than by
    copying its contents -- which is what `dev_requirements26.txt`'s `-r` line
    was, and the reason PEP 735 has the feature at all."""
    table = group_table(data)
    if table is None:
        row("groups.include-group", False,
            "no [dependency-groups] table in pyproject.toml")
        return
    edges = []
    for name, entries in sorted(table.items()):
        for entry in (entries if isinstance(entries, list) else []):
            if isinstance(entry, dict) and isinstance(
                    entry.get("include-group"), str):
                target = _declared_as(table, entry["include-group"])
                if target is not None and canonical(target) != canonical(name):
                    edges.append("%s -> %s" % (name, target))
    row("groups.include-group", bool(edges),
        "include-group: %s" % ", ".join(edges[:6]) if edges
        else "no group includes another with {include-group = ...}")


# --------------------------------------------------------------------------
# the vendored trees
# --------------------------------------------------------------------------

# Three of the four directories `bento/__init__.py` put on `sys.path`. Matched
# as path components, never as text: the reference solution's own
# `yaku/pyproject.toml` explains where the code came from, and a comment naming
# a directory is not the directory.
REMOVED_DIRS = ("_ply", "_simplejson", "_six")

# `_yaku` is the fourth, and it is not removed -- it moves. The instruction
# says "where the directory lives is your call" and "it is found by the name it
# declares, not by its path", so a whole-tree ban on the component `_yaku`
# would score a path the step explicitly leaves free: a member at `/app/_yaku`
# does everything the milestone asks and would lose a quarter of this
# group. What the milestone does say is that yaku leaves `bento/private/`, so
# that is what is banned -- a directory with both a `private` component and a
# component that is yaku by either of its names.
#
# Both names, because banning only `_yaku` leaves a hole: rename
# `bento/private/_yaku` to `bento/private/yaku`, drop a `pyproject.toml` in it
# and declare it a member, and the whole `workspace` group would pass while the
# build system has not left `bento/private/` at all.
MOVED_DIRS = ("_yaku", "yaku")
MOVED_FROM = "private"

# The two names the apparatus switched on. Both are whole-tree bans stated in
# the instruction, and both are matched outside prose only (see PROSE_SUFFIXES).
SWITCH_NAMES = ("BENTO_UNBUNDLE", "USE_PRIVATE_MODULES")
SWITCH_PATTERN = re.compile("|".join(re.escape(n) for n in SWITCH_NAMES))


def gate_unvendor_dirs(tree):
    banned = set(REMOVED_DIRS)
    hits = []
    for rel in walk_dirs(tree):
        parts = rel.split("/")
        if any(part in banned for part in parts):
            hits.append(rel)
        elif MOVED_FROM in parts and any(part in MOVED_DIRS for part in parts):
            hits.append(rel)
    hits = sorted(hits)
    row("unvendor.dirs-gone", not hits,
        "still in the tree: %s" % ", ".join(hits[:5]) if hits
        else "none of %r is a directory in the tree and neither %r nor %r is "
             "still under %r" % (REMOVED_DIRS, MOVED_DIRS[0], MOVED_DIRS[1],
                                 MOVED_FROM))


def _bento_init(tree):
    for path in walk(tree):
        rel = gatelib.relpath(path, tree)
        if rel == "bento/__init__.py" or rel.endswith("/bento/__init__.py"):
            return path, rel
    return None, None


def _syspath_lines(path):
    """Lines in this file that touch `sys.path`, by AST rather than by text.

    A comment mentioning the attribute is not a use of it, and step 02's tree
    has several commented-out ones in `bento/parser/tests/`.
    """
    tree, error = gatelib.parse_source(path)
    if tree is None:
        return None, error
    lines = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Attribute) and node.attr == "path" \
                and isinstance(node.value, ast.Name) and node.value.id == "sys":
            lines.add(node.lineno)
    return sorted(lines), None


def gate_unvendor_syspath(tree):
    """The loop is gone from the package's own `__init__`, and so are the two
    names it switched on.

    Scoped to the one module the instruction names and to the two identifiers.
    This repository manipulates `sys.path` in half a dozen other places for
    reasons that have nothing to do with vendoring -- `backends/waf_backend.py`
    loads waf tools that way, `commands/hooks.py` loads the candidate's own
    `bscript` -- and a whole-tree ban on the attribute would ask candidates to
    break behaviour the previous step froze.
    """
    faults = []
    path, rel = _bento_init(tree)
    if path is None:
        faults.append("the bento package has no __init__.py")
    else:
        lines, error = _syspath_lines(path)
        if lines is None:
            faults.append("%s does not parse: %s" % (rel, error))
        elif lines:
            faults.append("%s still touches sys.path at line(s) %r"
                          % (rel, lines[:6]))
    sites = regex_sites(tree, SWITCH_PATTERN)
    if sites:
        faults.append("%s" % "; ".join("%s:%d %s" % s for s in sites[:4]))
    row("unvendor.syspath-hack-gone", not faults,
        "; ".join(faults)[:290] if faults
        else "%s does not touch sys.path and neither %s nor %s is left in the "
             "tree's code" % (rel, SWITCH_NAMES[0], SWITCH_NAMES[1]))


# --------------------------------------------------------------------------
# entry points
# --------------------------------------------------------------------------

STATIC_LABELS = ["lock.exists", "lock.hashed", "lock.no-requirements-txt",
                 "workspace.members", "workspace.yaku-path-source",
                 "workspace.yaku-real", "workspace.hello-member",
                 "groups.declared",
                 "groups.include-group", "unvendor.dirs-gone",
                 "unvendor.syspath-hack-gone"]


def check_emitted(mode, want):
    got = [label for _family, label, _ok, _detail in ROWS]
    if sorted(got) != sorted(want):
        sys.stderr.write(
            "GATE FAULT (%s): emitted %d rows, expected %d; missing %r; "
            "unexpected %r\n"
            % (mode, len(got), len(want), sorted(set(want) - set(got)),
               sorted(set(got) - set(want))))
        raise SystemExit(4)


def local_dependency_dirs(wheel, tree):
    """The workspace members the root distribution's wheel cannot install without.

    `[tool.uv.sources]` is uv's own metadata and does not travel into a wheel:
    a bento wheel whose METADATA says `Requires-Dist: yaku` asks pip for a
    distribution that exists on no index, and `pip install bento-*.whl` fails
    outright. The behaviour venv therefore gets the member wheels alongside the
    root one, and this is the list -- closed over the members' own
    dependencies, so a member that depends on a member is covered too.

    Steps 01 and 02 declare no workspace, so this is empty for them and the
    argument vector to `venv_install` is the one they have always had.
    """
    _data, table, _note = workspace_table(tree)
    if table is None:
        return []
    members, _faults = resolve_members(tree, table)
    if not members:
        return []
    by_name = dict((name, absdir) for name, _rel, absdir in members)
    try:
        metadata, _wheel, _names = gatelib.wheel_metadata(wheel)
    except Exception:
        return []
    wanted = [canonical(requirement_name(r))
              for r in (metadata.get_all("Requires-Dist") or [])]
    picked, queue = [], [n for n in wanted if n in by_name]
    while queue:
        name = queue.pop(0)
        if name in picked:
            continue
        picked.append(name)
        data, _note = load_toml(os.path.join(by_name[name], "pyproject.toml"))
        for spec in ((data or {}).get("project") or {}).get("dependencies") or []:
            if isinstance(spec, str):
                inner = canonical(requirement_name(spec))
                if inner in by_name and inner not in picked:
                    queue.append(inner)
    return [by_name[name] for name in picked]


def main(argv):
    mode = argv[1]

    if mode == "static":
        tree, pristine = argv[2], argv[3]
        # The example's identity comes from the pinned tree read by the pinned
        # parser, exactly as step 02 derives it, so "the hello distribution"
        # means the same thing to both steps.
        hello_name, _hello_version = step02_gates.hello_identity(pristine)
        lock = gate_lock_exists(tree)
        gate_lock_hashed(lock)
        gate_lock_no_requirements(tree)
        data, table, note = workspace_table(tree)
        members, faults = ([], []) if table is None \
            else resolve_members(tree, table)
        gate_workspace_members(tree, table, note, members, faults)
        gate_workspace_yaku(tree, data, lock)
        gate_workspace_yaku_real(tree, pristine, members)
        hellodir, hellonote = locate_hello(tree, hello_name)
        gate_workspace_hello(tree, members, hellodir, hellonote, hello_name,
                             lock)
        groups = gate_groups_declared(data)
        gate_groups_include(data)
        gate_unvendor_dirs(tree)
        gate_unvendor_syspath(tree)
        check_emitted(mode, STATIC_LABELS)
        flush()
        # What `test.sh` needs and cannot read for itself, on stderr so it
        # stays out of the row stream.
        for name in groups:
            sys.stderr.write("GROUP %s\n" % name)
        for name, rel, _absdir in members:
            sys.stderr.write("MEMBER %s %s\n" % (name, rel))
        return 0

    if mode == "localdeps":
        wheel, tree = argv[2], argv[3]
        if wheel and os.path.isfile(wheel):
            for path in local_dependency_dirs(wheel, tree):
                sys.stdout.write("%s\n" % path)
        return 0

    raise SystemExit("unknown mode %r" % mode)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
