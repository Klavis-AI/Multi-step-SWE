"""Migrated-ness gates.

Design rule 11: a gate scans the whole repository, at the artifact or AST
level, and counts its own suppressions. Text matching is used only where the
artifact really is text -- a CMakeLists line, an `#include` directive, a
`NOLINT` comment. Every claim about C++ semantics goes through the compile
database and Clang's AST, so that a rename, a macro or a reformat cannot make
a banned construct invisible.

Each gate returns (ok, failures): a bool and a list of human-readable strings
naming what it found and where. The strings reach /logs/verifier only; the
agent never sees them.

Every gate is defined so that the baseline fails it and the golden solution
passes it. A gate nothing can fail is not a gate.
"""
import bisect
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

from . import stage

MAX_REPORT = 40          # failures listed per gate; the count is always exact
TOOL_TIMEOUT = 900.0

# Where the re-derived compile database is written for the analysers to read.
#
# Directly under /tmp, and deliberately not under the scorer's workdir:
# helpers.sh gives that directory to the unprivileged user so the callgrind
# bench can write it, and a directory that user owns is one in which a
# root-owned file can be unlinked and replaced by a file of its own. That
# would hand back exactly the trust derive_db exists to take away. /tmp is
# mode 1777 with the sticky bit set, so a root-owned entry directly in it
# cannot be renamed or unlinked by anyone but root, and the database the
# analysers are pointed at is the one root wrote. The placement is the
# security property, not a tidiness choice: do not move this back under the
# workdir.
COMPDB_DIR = "/tmp/isecompdb"


# Where the verifier stamps the build graph, between the configure that writes
# it and the build that reads it. test.sh creates the directory with mktemp,
# writes compdb.json and build.ninja.sha256 into it, and exports this; see
# stamp_build_graph in helpers.sh for why the stamp is taken there and
# derive_db for what it is worth. Unset outside the verifier, where there is
# no verifier around run_gates to take a stamp -- and an unset value is refused
# rather than quietly downgraded, because "the strong evidence is missing" and
# "the weak evidence is good enough" are not the same answer. An authoring
# script that wants the old post-build derivation has to ask for it by name.
GRAPH_ENV = "ISEFIX_GRAPH_DIR"
UNSTAMPED_ENV = "ISEFIX_ALLOW_UNSTAMPED_DB"

# Where the verifier wrote the trace of the configure that produced the build.
# test.sh opens the sink as root before the configure starts and passes it to
# cmake as a descriptor; helpers.sh's open_trace_sink is where the shape of
# that is written down, and traceguard.c is what keeps the project from
# reaching the other end of it.
#
# Unset outside the verifier, and refused rather than worked around: the trace
# is a measurement of the configure that was graded, and there is no way for
# anything running after that configure to take it again. This module used to
# run its own second configure to get one, which is the hole B2 named -- a
# project only has to tell the two apart to run the retired command in the one
# that counts and not in the one that is read.
TRACE_FILE_ENV = "ISEFIX_TRACE_FILE"

# Where root digested the source tree, after the verifier's stale-output sweep
# and before the configure. Compared in run_gates against the tree the text
# gates are about to read; see snapshot_tree in helpers.sh.
SNAPSHOT_ENV = "ISEFIX_TREE_SNAPSHOT"

# A trace of a configure this big is around 1.5 MB. Ten times that is a trace
# with something else in it, and reading an unbounded file into memory to find
# out is not the way to discover that.
TRACE_LIMIT = 64 << 20

SOURCE_GLOBS = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx", ".inl",
                ".ixx", ".cppm", ".txt", ".cmake", ".sh")

# Directories that are build output or vendored, never authored by the agent.
SKIP_DIRS = {".git", "build", "log", "debug", "release", "CMakeFiles",
             "__pycache__", ".cache"}


# ---------------------------------------------------------------------------
# repository walk


def walk(src, include=None, exclude=()):
    """Every authored file under `src`, build output excluded.

    include -- iterable of suffixes or explicit relative paths; default is
               SOURCE_GLOBS. exclude -- regexes matched against the relative
               path.
    """
    include = tuple(include) if include else SOURCE_GLOBS
    exclude = [re.compile(p) for p in exclude]
    out = []
    for root, dirs, names in os.walk(src):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in sorted(names):
            path = os.path.join(root, name)
            rel = os.path.relpath(path, src)
            if any(p.search(rel) for p in exclude):
                continue
            if not (rel in include or name in include
                    or os.path.splitext(name)[1] in include):
                continue
            out.append(rel)
    return sorted(out)


def read(path):
    with open(path, "rb") as handle:
        return handle.read().decode("utf-8", "replace")


# ---------------------------------------------------------------------------
# configure-time trace


# CMake's own files under a binary directory: the compiler- and system-
# identification .cmake it writes once per binary directory, and the
# throwaway project of each try_compile. Measured on the pinned tree, those
# are the whole of what a configure executes from under the binary directory
# -- four files -- and one of them calls include_directories(''), so a scope
# filter that keeps them fails every project there is, the golden included.
#
# It is this shape and not "everything under CMakeFiles/", because a project
# can write a .cmake anywhere it can write at all, CMakeFiles/ included, and
# include() it; that route is the reason the trace exists, and a blanket
# exclusion would have handed it back a place to stand. Reachability is
# checked separately below: a file of this shape that the project's own code
# includes is the project's file and is in scope.
CMAKE_OWNED = re.compile(
    r"CMakeFiles/(?:\d+(?:\.\d+)*|CMakeScratch|CMakeTmp)(?:/|$)")

# The commands that can put another file's contents into the configure.
TRACE_PULLERS = ("include", "add_subdirectory")


def _ninja_regen_inputs(build):
    """(inputs, problem): the inputs of build.ninja's regenerate edge.

    Ninja re-runs cmake when any file the configure read has changed, so the
    generator writes that set down as the inputs of one edge. It is the only
    record of what the scored configure executed that survives in the build
    directory, and reading it costs nothing and changes nothing -- which is
    the point, since re-configuring /app/build to find out would destroy the
    build every other gate is about.
    """
    path = os.path.join(build, "build.ninja")
    try:
        with open(path, errors="replace") as handle:
            text = handle.read()
    except OSError as exc:
        return None, "%s could not be read: %r" % (path, exc)
    # Ninja wraps a long statement with `$` before the newline; the edge is
    # one statement however it happens to be wrapped.
    text = text.replace("$\n", " ")
    for line in text.splitlines():
        head, sep, rest = line.partition(": RERUN_CMAKE")
        if not sep or not head.startswith("build "):
            continue
        # The outputs come first and the rule name is in the middle: the
        # inputs are everything after it, with ninja's dependency separators
        # dropped and its escapes undone.
        return _split_ninja(rest), None
    return None, ("%s names no RERUN_CMAKE edge, so there is no record in it "
                  "of which files the scored configure read" % path)


def _split_ninja(rest):
    """The paths in one ninja statement, with ninja's escaping undone.

    Ninja escapes exactly three characters in a path -- `$$`, `$:` and `$ `
    -- and splitting on whitespace first gets the third one wrong: a listfile
    whose name has a space in it arrives as `a$ b` and comes apart into two
    names that are not files. The set is then compared against the trace and
    neither half is there, so a path with a space in it failed every traced
    gate of an honest tree. Unescape while splitting instead, which is how
    ninja reads its own files.
    """
    items, current, index = [], [], 0
    while index < len(rest):
        char = rest[index]
        if char == "$" and index + 1 < len(rest) and rest[index + 1] in " :$":
            current.append(rest[index + 1])
            index += 2
            continue
        if char in " \t":
            if current:
                items.append("".join(current))
                current = []
            index += 1
            continue
        current.append(char)
        index += 1
    if current:
        items.append("".join(current))
    return [item for item in items if item not in ("|", "||")]


_CMAKE_ROOTS = []


def _outside_cmake(path):
    """True for a file of CMake's own installation rather than the project's.

    CMake's modules live under its installation prefix and the toolchain file
    is the image's. Neither is the candidate's CMake code, on either side of
    the comparison below: not in the trace, and not in ninja's record of what
    the scored configure read.
    """
    if not _CMAKE_ROOTS:
        roots = []
        found = shutil.which("cmake")
        if found:
            roots.append(os.path.dirname(os.path.dirname(found)))
        roots += ["/usr/share/cmake", "/usr/local/share/cmake"]
        toolchain = os.environ.get("CMAKE_TOOLCHAIN_FILE")
        _CMAKE_ROOTS.append((roots, os.path.abspath(toolchain)
                             if toolchain else None))
    roots, toolchain = _CMAKE_ROOTS[0]
    if toolchain and path == toolchain:
        return True
    return any(path == root or path.startswith(root + os.sep)
               for root in roots)


def _is_listfile(path):
    """True for a name CMake executes rather than merely reads."""
    base = os.path.basename(path)
    return base == "CMakeLists.txt" or base.endswith(".cmake")


_EVAL_SUFFIX = re.compile(r":\d+:EVAL$")


def _listfile_of(path):
    """The file a record's code was written in, not the name it is filed under.

    cmake_language(EVAL) runs its code with a synthetic file name -- the
    listfile that called it, the line it was called from, and EVAL -- and adds
    one suffix per level of nesting. Measured against CMake 4.1.2 in this
    image, with a project that EVALs at top level, EVALs an EVAL, and EVALs
    from an included .cmake:

        /app/CMakeLists.txt:4:EVAL
        /app/CMakeLists.txt:5:EVAL:1:EVAL
        /app/inner.cmake:1:EVAL

    The code really ran, in the configure being scored, from a file the build
    graph really depends on; only the name it is reported under is not that
    file's name, and a scope test that asks whether the name is one of the
    build's listfiles answers no. It cost EVALVAR its gate -- 14 of 14 on the
    tree §7.10 added that fixture for -- the first time the whole sweep was
    run after cmake_trace started asking that question.

    Nothing else in a trace is named this way: DEFER'd calls, macro and
    function bodies, and included files all report the listfile they are
    written in (measured, same image, same probe).
    """
    while True:
        stripped = _EVAL_SUFFIX.sub("", path)
        if stripped == path:
            return path
        path = stripped


def _edge_inputs(build):
    """(paths, problem): the files the SCORED configure's build graph depends on.

    Ninja writes one edge whose inputs are every file the configure read, and
    that edge is the only record in the build directory of what the configure
    touched. Two things are dropped from it and nothing else is: CMake's own
    installation -- the modules and templates it reads on its own account,
    which are not the candidate's CMake code on either side of any comparison
    here -- and the binary directory's CMakeCache.txt, which is on the edge
    because the configure reads it, not because it runs it.

    What is deliberately NOT dropped is everything that fails _is_listfile.
    Round 6 found that filter here: it made "a file this build depends on"
    mean "a file whose name is CMakeLists.txt or ends in .cmake", and CMake's
    include() means no such thing -- it takes a path, and appends .cmake only
    when the argument has no extension of its own. A fragment written at
    configure time and included as gen.inc executed a retired command, landed
    on this edge, and was dropped from scope by its name (GENINC in
    check_cmake_trace.py). The edge is the question; the name is not.

    Measured on the solved tree: the edge names 116 inputs, of which 96 are
    under CMake's own installation and one is the cache. The nineteen that
    remain are the project's seventeen CMakeLists.txt and the two files CMake
    writes under CMakeFiles/<version>/ to record the compiler and the system.
    """
    inputs, problem = _ninja_regen_inputs(build)
    if inputs is None:
        return None, problem
    build = os.path.abspath(build)
    cache = os.path.join(build, "CMakeCache.txt")
    out = []
    for item in inputs:
        full = os.path.normpath(item if os.path.isabs(item)
                                else os.path.join(build, item))
        if _outside_cmake(full) or full == cache:
            continue
        out.append(full)
    return out, None


def _trace_anchors(edge, build):
    """The edge inputs that a configure of THIS build directory has to run.

    What ties a trace to a build graph has to be a file that is on the edge
    AND that nothing can leave unexecuted. CMake writes the compiler and the
    system it found into CMakeFiles/<version>/*.cmake under the binary
    directory and executes them itself; their paths name this build directory,
    so a trace of some other configure does not contain them, and no project
    can decline to run them.

    The demand used to be the whole listfile-shaped half of the edge, and that
    was wrong in the honest direction: CMAKE_CONFIGURE_DEPENDS is the
    documented way to say "re-configure when this changes" and says nothing
    about who reads the file, so a project that keeps its version in
    version.cmake and reads it with file(READ) put a listfile-shaped input on
    the edge that no configure executes. Every traced gate then failed on a
    tree that had done nothing wrong (CFGDEPCMAKE in check_cmake_trace.py). An
    included file that happens to be empty is the same story. The strength
    that demand looked like it had was already gone: with one configure and a
    sink only root can open, the trace cannot be anything but this configure's
    output, so what is left to check is that it is THIS build's -- which is
    what these anchors check.
    """
    build = os.path.abspath(build)
    out = []
    for path in edge:
        if not path.startswith(build + os.sep):
            continue
        if _is_listfile(path) and CMAKE_OWNED.match(path[len(build) + 1:]):
            out.append(path)
    return out


def _graph_moved(build):
    """A reason string if build.ninja is no longer what the stamp recorded.

    The trace describes the configure that wrote the build graph. Ninja can
    re-run CMake mid-build from its own regenerate edge, and that second
    configure is one nothing here saw -- it needs no discriminator to know it
    is the second, because the first can leave it a file to find. So a graph
    whose hash has moved since root stamped it takes the trace with it, on the
    same terms _stamped_db refuses the database: see stamp_build_graph in
    helpers.sh for where the hash comes from.
    """
    stamp = os.environ.get(GRAPH_ENV)
    if not stamp:
        return None
    digest = os.path.join(stamp, "build.ninja.sha256")
    graph = os.path.join(build, "build.ninja")
    if not os.path.exists(digest) or not os.path.exists(graph):
        return None                 # derive_db is where an absent stamp lands
    try:
        with open(digest) as handle:
            want = (handle.read().split() or [""])[0]
        with open(graph, "rb") as handle:
            have = hashlib.sha256(handle.read()).hexdigest()
    except OSError as exc:
        return "the stamped build graph could not be read: %r" % (exc,)
    if want and want != have:
        return ("build.ninja is no longer the file the traced configure "
                "wrote: sha256 %s when the verifier stamped it, %s now, so "
                "the build was driven from a configure this trace never saw"
                % (want[:16], have[:16]))
    return None


def cmake_trace(build=None):
    """(events, problem): every CMake command the project actually ran.

    Requirement 3 states its prohibitions as properties of the project as
    CMake configures it, not of how its text is spelled, and a regex over the
    files answers a different question: `cmake_language(CALL ${c} ...)` and a
    `.cmake` written at configure time are both invisible to it and both
    execute. CMake will say what it executed if asked, with every argument
    expanded, so the gates ask.

    This reads a trace; it does not take one. The configure that produced the
    build is the configure that was traced -- test.sh passes the sink to it on
    a descriptor and build_candidate.sh puts --trace-redirect on that one
    command -- and there is exactly one of them.

    It used to run a second configure of its own, into a scratch binary
    directory, on the reasoning that /app/build holds the build the text gates
    and the compile database are about and configuring into it again would
    destroy it. The reasoning was right and the conclusion was wrong. Two
    configures of the same project have to differ in something, and whatever
    they differ in is a branch the project can read:

        if(NOT DEFINED ENV{ISEFIX_TRACE_PATH})
            cmake_language(CALL ${_c} /usr/include)
        endif()

    -- the retired command runs in the configure that is graded and not in the
    one that is read, and no listfile is added, so nothing is left to
    reconcile. Measured on the package as it stood: 14 of 14 gates and a
    reward of 1.000000 on a tree breaking step 1's requirement 5, where the
    same tree without the branch scored 13 of 14. CMAKE_BINARY_DIR was a
    second such discriminator and ISEFIX_TRACE_PATH a third. There is no list
    of discriminators to close; there is only not having a second configure.

    What is left is a narrower tie: the files CMake writes under the binary
    directory's CMakeFiles/<version>/ and executes have to be in the trace, or
    this trace is not a record of the configure that wrote this build graph
    (_trace_anchors). Every failure is returned rather than raised: a
    trace that cannot be tied to the build is a gate failure with a reason in
    it, never an exception out of grade.py.
    """
    path = os.environ.get(TRACE_FILE_ENV)
    if not path:
        return None, ("no configure trace was taken. The verifier opens the "
                      "sink before the configure and passes it to cmake on a "
                      "descriptor; without one there is no record of what the "
                      "project executed, and a trace taken now would be of a "
                      "configure that is not the one that was built")
    if not os.path.exists(path):
        return None, ("the verifier meant to trace the configure and could "
                      "not: %s does not exist. See open_trace_sink in "
                      "helpers.sh -- the variable is exported even when the "
                      "sink could not be created, so that a missing trace "
                      "arrives here as a refusal rather than as silence"
                      % path)
    if build:
        moved = _graph_moved(build)
        if moved:
            return None, moved
    try:
        size = os.path.getsize(path)
        if size > TRACE_LIMIT:
            return None, ("the configure wrote a %d byte trace, over the %d "
                          "byte limit" % (size, TRACE_LIMIT))
        with open(path, "rb") as handle:
            raw = handle.read()
    except OSError as exc:
        return None, "the configure trace could not be read: %r" % (exc,)
    if not raw:
        return None, ("the configure wrote an empty trace, so nothing here "
                      "records what it executed")

    records = []
    for line in raw.decode("utf-8", "replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if isinstance(record, dict) and "cmd" in record and record.get("file"):
            records.append(record)

    # What CMake ran on its own account rather than on the project's is
    # _outside_cmake, which the edge set below uses too: the two sides of that
    # comparison have to agree about whose file a file is.
    scored = os.path.abspath(build) if build else None

    # The regenerate edge, read before any record is named, because the answer
    # to "is this a file the scored build depends on" decides two different
    # things: which records are in scope, and whether a record's name is the
    # name of a file at all (see normal).
    edge = None
    if scored:
        edge, problem = _edge_inputs(scored)
        if edge is None:
            return None, ("this trace cannot be tied to the build that was "
                          "scored, so it is not evidence about it: %s"
                          % problem)
        edge = set(edge)

    def normal(path):
        """The file a record's code was written in.

        cmake_language(EVAL)'s synthetic name is rewritten back to the
        listfile that called it -- but only when the name is not itself a file
        this build depends on. `hooks.cmake:1:EVAL` is a legal path that
        include() will run, and stripping the suffix off a real file names one
        that ran nothing (INCEVALNAME in check_cmake_trace.py).
        """
        path = os.path.normpath(path)
        if edge is not None and path in edge:
            return path
        return _listfile_of(path)

    def theirs(path):
        """True for a file CMake generated for itself under the binary dir."""
        if scored and path.startswith(scored + os.sep):
            return bool(CMAKE_OWNED.match(path[len(scored) + 1:]))
        return False

    # Reachability, to a fixpoint: a file of CMake's own shape that the
    # project include()s is the project's, and executing from a name CMake
    # also uses does not buy an exemption. CMake's four are unreachable this
    # way -- only CMake's own machinery includes them -- so they stay out.
    seen = set(normal(r["file"]) for r in records)
    pulled = set()
    for _ in range(8):
        grew = False
        for record in records:
            path = normal(record["file"])
            if _outside_cmake(path) or (theirs(path)
                                        and path not in pulled):
                continue
            if str(record["cmd"]).lower() not in TRACE_PULLERS:
                continue
            here = os.path.dirname(path)
            for arg in record.get("args") or []:
                arg = str(arg)
                for cand in (arg, os.path.join(here, arg),
                             os.path.join(here, arg, "CMakeLists.txt")):
                    cand = normal(cand)
                    if cand in seen and cand not in pulled:
                        pulled.add(cand)
                        grew = True
        if not grew:
            break

    # The comparison below used to have to rename the trace's binary
    # directory to the scored one, because they were two different
    # directories. They are the same directory now, and the set is the trace's
    # own.
    traced = seen

    executed = edge
    if scored:
        anchors = _trace_anchors(edge, scored)
        if not anchors:
            return None, ("the build graph of %s names none of the files "
                          "CMake writes under its own CMakeFiles/<version>/ "
                          "and executes, so there is nothing in it this trace "
                          "can be tied to" % scored)
        absent = sorted(path for path in anchors if path not in traced)
        if absent:
            return None, ("the configure of %s executed %d CMake file(s) of "
                          "CMake's own that this trace never saw, so the "
                          "trace is not a record of the configure that wrote "
                          "this build graph: %s"
                          % (scored, len(absent), ", ".join(absent[:8])))

    def in_scope(path):
        """Does a record from this file say anything about THIS build?

        Three conditions, and the third was measured. A configure runs more
        listfiles than the project's own: try_compile configures a whole
        second project, in-process, into the same trace, and every command it
        runs is recorded against whatever file ran it. CMake writes that inner
        project's CMakeLists.txt itself and puts `cmake_minimum_required
        (VERSION 4.1.2.0)` at the top of it -- so a project that calls
        try_compile with a binary directory of its own choosing satisfied
        cmake-minimum-328 while declaring 3.10 in the file that governs its
        build, and a project that points the inner configure at a .cmake in
        its own source tree could satisfy any `require` rule at all from code
        that compiles nothing.

        Membership in the regenerate edge is what tells the two apart, and it
        is not a heuristic about directories: a command that affects the build
        being scored has to have run from a file that build depends on, and
        CMake puts every listfile it executes for the outer project into that
        edge. The inner configure's listfiles are the inner build's
        dependencies, not this one's, and CMake chooses their names itself.

        Membership is asked of the name the record carries first and of the
        listfile that name was invented from second (normal, above), so a
        file whose own name looks synthetic is still its own file. It is not
        asked about the SHAPE of the name at all: a fragment included as
        gen.inc is a file this build depends on, and every command it ran is
        a command this build ran.
        """
        if _outside_cmake(path) or (theirs(path) and path not in pulled):
            return False
        return executed is None or path in executed

    events = []
    for record in records:
        if not in_scope(normal(record["file"])):
            continue
        events.append({"cmd": str(record["cmd"]).lower(),
                       "args": [str(a) for a in record.get("args") or []],
                       "file": record.get("file"),
                       "line": record.get("line")})
    return events, None


def _tree_rewritten(src):
    """[] or the paths that moved since root digested the tree.

    The snapshot is taken by the verifier, as root, between the stale-output
    sweep and the configure -- see snapshot_tree in helpers.sh. A single
    element is enough to fail every gate, so the reasons a snapshot could be
    missing all have to arrive here as findings rather than as an empty list:
    a snapshot that was meant to be taken and is not there is a verifier that
    cannot say what it built.

    No snapshot AND no trace is the authoring path -- run_gates driven in
    process, with no verifier around it -- and there is nothing to compare.
    """
    path = os.environ.get(SNAPSHOT_ENV)
    if not path:
        if os.environ.get(TRACE_FILE_ENV):
            return ["the verifier traced the configure but digested no tree "
                    "beforehand, so nothing here can say the sources the "
                    "gates are reading are the sources that were compiled"]
        return []
    try:
        with open(path) as handle:
            before = json.load(handle)
    except (OSError, ValueError) as exc:
        return ["the tree digest taken before the configure could not be "
                "read: %r" % (exc,)]
    after = tree_digest(src)
    return sorted(set(before) ^ set(after)
                  | set(rel for rel in set(before) & set(after)
                        if before[rel] != after[rel]))


def tree_digest(src):
    """{relative path: sha256} over every file the gates read as text."""
    out = {}
    for rel in walk(src):
        try:
            with open(os.path.join(src, rel), "rb") as handle:
                out[rel] = hashlib.sha256(handle.read()).hexdigest()
        except OSError:
            out[rel] = "unreadable"
    return out


# ---------------------------------------------------------------------------
# text-level gates


def _blank_comments(body, marker):
    """Blank `marker`-to-end-of-line, preserving every byte offset.

    Spaces rather than deletion, so a hit's offset still maps to the line it
    was found on and so a stripped comment cannot glue the line above it to
    the line below. Opt-in per gate: `#` opens a comment in CMake and opens a
    *directive* in C++, and the suppression scan is looking for `#pragma`.
    """
    return re.sub(r"%s[^\n]*" % re.escape(marker),
                  lambda m: " " * len(m.group(0)), body)


CHAR_LITERAL = re.compile(
    r"'(?:\\(?:x[0-9a-fA-F]+|[0-7]{1,3}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|.)"
    r"|[^'\\\n])'")


def _blank_block_comments(body):
    """Blank every /* */ comment, delimiters included, preserving offsets.

    `_blank_comments` handles the marker-to-end-of-line kind, which is the
    only kind most of these gates meet. This is the other kind, and it is a
    separate function rather than a second regex because a `/*` inside a
    string literal opens nothing: `writeStr("/*")` is code, and a scan that
    treats it as a comment blanks the rest of the file. So this is the same
    state machine `_blank_strings` runs, reading the file the same way the
    compiler does, and it skips literals for exactly the reason that one
    skips comments.

    Unlike a string, whose quotes are kept because a pattern may be about
    literals, a comment is blanked whole: nothing in it is code, and there is
    no pattern that wants to know a comment was once there.

    Opt-in per gate, and off by default, because a gate looking for a
    suppression is looking for it inside comments too.
    """
    out = list(body)
    index, end = 0, len(body)
    while index < end:
        char = body[index]
        if char == "/" and index + 1 < end and body[index + 1] == "/":
            while index < end and body[index] != "\n":
                index += 1
            continue
        if char == "/" and index + 1 < end and body[index + 1] == "*":
            start = index
            index += 2
            while index + 1 < end and not (body[index] == "*"
                                           and body[index + 1] == "/"):
                index += 1
            index = min(index + 2, end)
            for offset in range(start, index):
                if body[offset] != "\n":
                    out[offset] = " "
            continue
        if char == "'":
            match = CHAR_LITERAL.match(body, index)
            index = index + 1 if match is None else match.end()
            continue
        if char != '"':
            index += 1
            continue
        index += 1
        while index < end:
            char = body[index]
            if char == "\\" and index + 1 < end:
                index += 2
                continue
            if char == '"':
                index += 1
                break
            index += 1
    return "".join(out)


def _blank_strings(body):
    """Blank the inside of every string literal, preserving every byte offset.

    A text gate asks what the code says. A string literal is not code saying
    something, it is data the code prints, and the two are not interchangeable
    when the program's output is frozen: step 10 deletes the callback
    `onTcpConnected()` and keeps the log line `"onTcpConnected (...)"`, so
    `\bonTcpConnected\b` over the raw text reports a tree that did exactly
    what it was told as a tree that did not.

    Quotes are kept and only their contents blanked, so a pattern that is
    *about* literals still sees an empty one rather than nothing at all.
    Spaces rather than deletion, and newlines left alone, so a hit's offset
    still maps to the line it was found on -- the same rule `_blank_comments`
    follows.

    A state machine and not a regex, and it skips comments as it goes, for
    the reason `strip_for_preservation` gives from the other side: a `"` in a
    comment opens no string, and a scan that thinks it does blanks the rest
    of the file. Opt-in per gate, because a gate looking for a `#pragma` in a
    string is a real gate.

    Character literals are handled for the same reason and it is not
    theoretical: this tree writes `char quoteChar = '"'` three times in
    `ise_sys_utils.h` and `enum { PROP_ITEM_QUOT = '"' }` once in
    `ise_classes.h`. A scanner that knows only about `"` opens a string on
    each of those and blanks everything to the next quote -- which, measured
    before this paragraph existed, silently emptied the rest of both headers
    and would have turned two unrelated gates into false failures. A `'` that
    does not open a well-formed character literal is left alone, because C++
    also spells a digit separator that way.
    """
    out = list(body)
    index, end = 0, len(body)
    while index < end:
        char = body[index]
        if char == "/" and index + 1 < end and body[index + 1] == "/":
            while index < end and body[index] != "\n":
                index += 1
            continue
        if char == "/" and index + 1 < end and body[index + 1] == "*":
            index += 2
            while index + 1 < end and not (body[index] == "*"
                                           and body[index + 1] == "/"):
                index += 1
            index = min(index + 2, end)
            continue
        if char == "'":
            match = CHAR_LITERAL.match(body, index)
            if match is None:
                index += 1
                continue
            for offset in range(match.start() + 1, match.end() - 1):
                out[offset] = " "
            index = match.end()
            continue
        if char != '"':
            index += 1
            continue
        index += 1
        while index < end:
            char = body[index]
            if char == "\\" and index + 1 < end:
                for offset in (index, index + 1):
                    if body[offset] != "\n":
                        out[offset] = " "
                index += 2
                continue
            if char == '"':
                index += 1
                break
            if char != "\n":
                out[index] = " "
            index += 1
    return "".join(out)


def _executed_listfiles(ctx, cfg, scanned):
    """Files under the source tree the configure ran that `walk` did not read.

    `include()` takes a path, and CMake appends `.cmake` only when the
    argument names no file that exists. So `include(warnings.inc)` is a
    listfile by every measure that decides anything: it runs, its commands are
    the project's, and the flags it sets are on every translation unit. A scan
    that decides what counts as CMake code from the end of the filename reads
    none of it -- and on an honest tree that splits its CMake up the way CMake
    documents, the gate reports the work undone while the trace shows it done.
    That is the same mistake as scoping trace records by name shape, one arm
    over, and it has the same answer: what makes a file part of the project's
    CMake is that the configure executed it.

    Only for gates that read the trace, because those are the gates that are
    about the configure; a scan of the C++ sources is not widened by what
    CMake ran. Build output stays out on both counts -- `walk` skips it, and a
    fragment the configure generated into the build directory was not in the
    tree the candidate wrote. The trace half is what answers for those.
    """
    events = ctx.get("trace") if ctx and cfg.get("trace") else None
    if not events:
        return []
    src = os.path.abspath(ctx["src"])
    seen, extra = set(scanned), []
    for event in events:
        path = os.path.abspath(event["file"])
        if not path.startswith(src + os.sep):
            continue
        rel = os.path.relpath(path, src)
        if rel in seen or not os.path.isfile(path):
            continue
        if set(os.path.dirname(rel).split(os.sep)) & SKIP_DIRS:
            continue
        seen.add(rel)
        extra.append(rel)
    return sorted(extra)


def _blank_platform_branches(body):
    """The text, with every line inside a platform-only branch blanked.

    A written spelling is the only way to state a property an alias or a macro
    can erase -- `#define NULL nullptr` makes every `NULL` in the tree a
    nullptr literal to the AST, and
    `namespace boost { template<class T> using function = std::function<T>; }`
    makes every `boost::function` typedef a `std::function` one. Both are
    caught by looking at what is written; neither is caught by looking at what
    it desugars to.

    What is written inside `#ifdef ISE_WINDOWS` is frozen by requirement 13,
    and both spellings legitimately survive in there. So the scan has to skip
    those branches, and it skips them by the same reading of "inside" that
    `preserved_regions` uses -- `CPP_DIRECTIVE` and `WINDOWS_MACRO`, the
    constants, not a second copy of the idea. Lines are blanked rather than
    removed so that every line number this gate reports is still the line
    number in the file.
    """
    out, depth, blanked = [], 0, set()
    for line in body.splitlines():
        match = CPP_DIRECTIVE.match(line)
        if match is None:
            out.append("" if blanked else line)
            continue
        kind, condition = match.group(1), match.group(2)
        if kind in ("if", "ifdef", "ifndef"):
            depth += 1
            if WINDOWS_MACRO.search(condition) and kind != "ifndef":
                blanked.add(depth)
        elif kind == "elif":
            blanked.discard(depth)
            if WINDOWS_MACRO.search(condition):
                blanked.add(depth)
        elif kind == "else":
            blanked.discard(depth)
        elif kind == "endif":
            blanked.discard(depth)
            depth = max(0, depth - 1)
        out.append("")
    return "\n".join(out)


def _scan(src, cfg, ctx=None, pairs=None):
    """Returns (hits, the relative paths scanned).

    The second number is not decoration. `walk` skips build output, and the
    pinned tree keeps its only CMakeLists.txt under build/linux/ -- so a gate
    reading "no CMakeLists.txt may call aux_source_directory" scans nothing at
    all on the baseline and reports success. A gate the baseline passes for
    want of anything to read is the failure mode this module's own docstring
    forbids, so the callers can require a floor on it.

    `span` matches the whole file instead of one line at a time, and is what
    the CMake gates use. A CMake command is one command however many lines it
    is written across, and the multi-line spelling is the idiomatic one:

        target_link_libraries(echo_server
            ise
            pthread)

    Line at a time, that is three lines none of which is a violation -- the
    gate reads it as clean and the tree links libpthread by name anyway.
    Measured, before this existed, on a tree built for the purpose: all eight
    CMake gates passed it. The same tree also called `INCLUDE_DIRECTORIES`
    and `AUX_SOURCE_DIRECTORY`, which is why every pattern now spells its
    command name `(?i:...)`: CMake command names are case-insensitive, so a
    case-sensitive gate forbids one spelling of a command and permits the
    other, and the permitted one is the spelling the 2.x-era project this task
    starts from actually used.

    `pairs` is an out-parameter and the reason it is one is the hit format:
    a hit is a human-readable `path:line: text` string, and a caller that
    wanted the path back out of it would have to parse a colon out of a
    filename. Callers that ask for it get (relative path, matched text) for
    every match, which is what `min_distinct` and `min_hit_files` count. It
    stays optional so that the gates which only need a total pay nothing.
    """
    pattern = re.compile(cfg["pattern"])
    hits = []
    files = walk(src, cfg.get("include"), cfg.get("exclude", ()))
    files = files + _executed_listfiles(ctx, cfg, files)
    for rel in files:
        body = read(os.path.join(src, rel))
        if cfg.get("block_comments"):
            body = _blank_block_comments(body)
        if cfg.get("strings"):
            body = _blank_strings(body)
        if cfg.get("comment"):
            body = _blank_comments(body, cfg["comment"])
        if cfg.get("outside_platform_branches"):
            body = _blank_platform_branches(body)
        if cfg.get("span"):
            for match in pattern.finditer(body):
                hits.append("%s:%d: %s"
                            % (rel, body.count("\n", 0, match.start()) + 1,
                               " ".join(match.group(0).split())[:160]))
                if pairs is not None:
                    pairs.append((rel, match.group(0)))
        elif cfg.get("suppressions"):
            # The suppression precondition: SUPPRESSIONS, read the way the
            # compiler reads the text (see suppression_lines).
            for lineno, line in suppression_lines(body):
                hits.append("%s:%d: %s" % (rel, lineno, line.strip()[:160]))
                if pairs is not None:
                    pairs.append((rel, line))
        else:
            for lineno, line in enumerate(body.splitlines(), 1):
                match = pattern.search(line)
                if match:
                    hits.append("%s:%d: %s" % (rel, lineno, line.strip()[:160]))
                    if pairs is not None:
                        pairs.append((rel, match.group(0)))
    return hits, files


def _scope_wrong(cfg, scanned):
    """Why this gate's scan does not cover what the gate is about, or None.

    `min_files` is the weak form -- the scan read something. `require_files`
    is the strong one, and the one the CMake gates need: this repository keeps
    sixteen perfectly modern `examples/*/CMakeLists.txt` and hides everything
    objectionable in `build/linux/CMakeLists.txt`, which `walk` skips as build
    output. A gate reading "no CMakeLists.txt may call aux_source_directory"
    therefore scans sixteen innocent files on the baseline and reports success
    while the guilty one sits there unread. Naming the file the gate is really
    about turns that into the failure it should always have been.
    """
    need = cfg.get("min_files", 0)
    if len(scanned) < need:
        return ["scanned %d file(s) matching %r, need at least %d"
                % (len(scanned), cfg.get("include")
                   or "the default source globs", need)]
    missing = [p for p in cfg.get("require_files", []) if p not in scanned]
    if missing:
        return ["this gate is about %s, and the scan did not reach %s"
                % (", ".join(cfg["require_files"]), ", ".join(missing))]
    return None


def _no_project(cfg, ctx):
    """`needs_project`, and why a preservation gate has to declare it.

    Most gates here are about something the candidate has to *do*, so the
    pristine tree fails them by simply not having done it. A gate about
    something the candidate must not *undo* -- "the Windows implementation is
    still there" -- is true of the pristine tree for free, and a gate that is
    true before any work is done is 1/13 of the gate family under every score
    in the step, including the score of a run that changed nothing. Build
    item 6 wants that run at zero.

    So a preservation gate says `needs_project`, and reads: the migrated
    project still contains this. A tree CMake could not configure is not a
    migrated project; there is nothing yet for the property to be about.
    """
    if cfg.get("needs_project") and ctx.get("db") is None:
        return ["no compile_commands.json in %s: there is no configured "
                "project for this property to hold of" % ctx["build"]]
    return None


def _no_build(cfg, ctx):
    """`needs_build`, and why a gate about the built project needs it.

    A compile database is a plan. CMake writes it at generate time, from the
    targets the project declared, and it says what the compiler *would* be
    told; it is not evidence that any compiler ever ran. So a tree whose CMake
    is flag-perfect and whose sources still do not survive `-Werror`
    configures, writes a database naming every required flag, and produces no
    object files at all -- and the three gates that read that database say
    yes. Measured: thirteen of fourteen gates green, no binaries, no
    behaviour.

    Every one of those gates describes itself as being about something that
    was *compiled*, so each of them declares `needs_build` and reads: the
    build succeeded, and this is how it compiled. A gate about the built
    project has nothing to hold of until there is one.
    """
    if cfg.get("needs_build") and ctx.get("build_rc") != 0:
        return ["`cmake --build` did not succeed (rc=%r): there is no "
                "built project for this property to hold of"
                % (ctx.get("build_rc"),)]
    return None


def _no_database(ctx):
    """Why there is no compile database to read, in what detail there is."""
    problem = ctx.get("db_problem")
    if problem:
        return "no compile database for %s: %s" % (ctx["build"], problem)
    return "no compile database for %s" % ctx["build"]


def _trace_failures(cfg, ctx):
    """The `trace` half of a gate: what the configure actually did.

    The text half asks how the project is spelled and this half asks what it
    executed, and the two are unioned rather than one chosen: a gate passes
    only if both are satisfied. Every existing pattern therefore keeps
    meaning what it meant, and the routes a pattern cannot follow -- a command
    name reached through a variable, a `.cmake` written into the build
    directory and included -- are answered here instead of by widening a
    regex until it is a CMake interpreter.
    """
    block = cfg.get("trace")
    if not block:
        return []
    events = ctx.get("trace")
    if events is None:
        return ["this gate is about what the configure did, and there is no "
                "trace of it: %s" % (ctx.get("trace_problem")
                                     or "no trace was taken")]

    failures = []
    # Without a floor every `forbid` below is vacuously true of a trace that
    # captured nothing, which is the same class of bug as an empty fixture
    # file. On any tree that configures at all this number is around sixty.
    need = block["min_events"]
    if len(events) < need:
        failures.append("VERIFIER-PROBLEM: the trace captured %d command(s) "
                        "from the project's own CMake files, below the floor "
                        "of %d, so nothing it says is evidence"
                        % (len(events), need))

    forbidden = set(name.lower() for name in block.get("forbid_cmd", []))
    for event in events:
        if event["cmd"] in forbidden:
            failures.append("%s:%s: %s was executed"
                            % (event["file"], event["line"], event["cmd"]))

    for rule in block.get("forbid_arg", []):
        want, pattern = rule.get("cmd"), rule["arg"]
        for event in events:
            if want and not re.fullmatch(want, event["cmd"]):
                continue
            args = event["args"]
            if "index" in rule:
                index = rule["index"]
                tested = [args[index]] if index < len(args) else []
            else:
                tested = args
            for arg in tested:
                if re.search(pattern, arg, re.S):
                    failures.append("%s:%s: %s was executed with %r: %s"
                                    % (event["file"], event["line"],
                                       event["cmd"], arg[:120], rule["why"]))

    for rule in block.get("require", []):
        count = 0
        for event in events:
            if event["cmd"] != rule["cmd"].lower():
                continue
            if re.search(rule["args"], "\x00".join(event["args"]), re.S):
                count += 1
        if count < rule["min"]:
            failures.append("%d of the project's %s calls matched %r, need "
                            "%d: %s" % (count, rule["cmd"], rule["args"],
                                        rule["min"], rule["why"]))

    for rule in block.get("require_arg_at", []):
        for event in events:
            if event["cmd"] != rule["cmd"].lower():
                continue
            args = event["args"]
            index = rule["index"]
            if index < len(args) and re.fullmatch(rule["arg"], args[index]):
                continue
            failures.append("%s:%s: %s %s: %s"
                            % (event["file"], event["line"], event["cmd"],
                               args[:6], rule["why"]))
    return failures


def gate_text_absent(cfg, ctx):
    missing = _no_build(cfg, ctx) or _no_project(cfg, ctx)
    if missing:
        return False, missing
    hits, scanned = _scan(ctx["src"], cfg, ctx)
    wrong = _scope_wrong(cfg, scanned)
    if wrong:
        return False, wrong
    limit = cfg.get("allow", 0)
    traced = _trace_failures(cfg, ctx)
    return len(hits) <= limit and not traced, (hits + traced)[:MAX_REPORT]


def _variety_failures(cfg, pairs):
    """How many different things the matches are, not how many there are.

    `min` counts matches, and twenty copies of one line is twenty matches. For
    a gate whose claim is about a *structure* -- "the dependency order the
    headers had is carried into the module graph" -- that is the wrong
    measurement twice over: twenty repetitions of `import ise.classes;` in one
    interface unit satisfy a floor of twenty and compose nothing, and so does
    one unit that imports twenty others while the other fourteen import
    nothing. Both were written and measured; both passed a floor of twenty.

    `min_distinct` counts the distinct values a one-group pattern takes over
    the matched text, optionally keyed by the file the match was in, which is
    what turns "twenty imports" into "twenty distinct edges". `min_hit_files`
    counts the files that contain any match at all, which is what turns a star
    into a graph. Neither replaces `min`: a tree can have twenty distinct
    edges and still be required to have twenty matches.
    """
    failures = []
    for spec in cfg.get("min_distinct", []):
        pattern = re.compile(spec["pattern"])
        # Asserted rather than assumed, for the same reason the symbols gate
        # asserts it: a pattern written without a group counts nothing and
        # reports every tree as empty, and one with two counts only the first.
        if pattern.groups != 1:
            failures.append("min_distinct pattern %r has %d capture group(s), "
                            "not 1" % (spec["pattern"], pattern.groups))
            continue
        seen = set()
        for rel, text in pairs:
            for value in pattern.findall(text):
                seen.add((rel, value) if spec.get("per_file") else value)
        if len(seen) < spec["min"]:
            failures.append(
                "%d %s, need at least %d"
                % (len(seen), spec.get("what", "distinct matches of %r"
                                       % spec["pattern"]), spec["min"]))
    need = cfg.get("min_hit_files", 0)
    if need:
        files = set(rel for rel, _ in pairs)
        if len(files) < need:
            failures.append("the matches are spread over %d file(s), need at "
                            "least %d" % (len(files), need))
    return failures


def gate_text_present(cfg, ctx):
    missing = _no_build(cfg, ctx) or _no_project(cfg, ctx)
    if missing:
        return False, missing
    pairs = [] if (cfg.get("min_distinct") or cfg.get("min_hit_files")) else None
    hits, scanned = _scan(ctx["src"], cfg, ctx, pairs)
    wrong = _scope_wrong(cfg, scanned)
    if wrong:
        return False, wrong
    need = cfg.get("min", 1)
    failures = _trace_failures(cfg, ctx)
    if pairs is not None:
        failures.extend(_variety_failures(cfg, pairs))
    if len(hits) < need:
        failures.insert(0, "found %d matches for %r, need %d"
                        % (len(hits), cfg["pattern"], need))
    return not failures, failures[:MAX_REPORT]


def gate_files_exist(cfg, ctx):
    missing = [p for p in cfg["paths"]
               if not os.path.exists(os.path.join(ctx["src"], p))]
    return not missing, ["missing: %s" % p for p in missing]


def gate_files_absent(cfg, ctx):
    present = [p for p in cfg["paths"]
               if os.path.exists(os.path.join(ctx["src"], p))]
    return not present, ["still present: %s" % p for p in present]


def gate_artifacts(cfg, ctx):
    """The build produced each named artefact, exactly once by content.

    Where under the tree a target lands is the build system's business and the
    instruction says so, so this asks for names rather than paths -- the same
    search the behaviour suite uses to find what to run, asserted as its own
    requirement rather than as a precondition of something else. Two files
    with one name and different contents is a failure and not a tie-break: a
    verifier that picked one would let directory order pick the score.
    """
    found = stage.find(ctx["src"], cfg["names"])
    failures = []
    for name in cfg["names"]:
        paths = found.get(name, [])
        if not paths:
            failures.append("%s: the build produced no file with this name"
                            % name)
            continue
        digests = set(stage.digest(path) for path in paths)
        if len(digests) > 1:
            failures.append(
                "%s: %d files with this name and %d different contents: %s"
                % (name, len(paths), len(digests),
                   ", ".join(os.path.relpath(p, ctx["src"])
                             for p in sorted(paths)[:6])))
    return not failures, failures[:MAX_REPORT]


# Every suppression form the two compilers accept, counted repository-wide.
# This is deliberately not a gate. As a gate it would be one the baseline
# passes for free -- a pristine tree contains no NOLINT -- and build-process
# item 6 wants the nop run to score zero, so a gate that is true before any
# work is done is a floor under every score in the step. As a precondition it
# is also strictly stronger than a gate would have been: one suppression
# anywhere fails every gate at once, including whichever one it was written to
# silence. Design rule 11, "counts its own suppressions", read as the sentence
# it is. Every step's instruction says this in as many words.
SUPPRESSIONS = r"NOLINT|NOLINTNEXTLINE|NOLINTBEGIN|" \
               r"(#\s*pragma|_Pragma\s*\(\s*[\"'])\s*" \
               r"(clang|GCC)\s+diagnostic\s+(ignored|warning)|" \
               r"(#\s*pragma|_Pragma\s*\(\s*[\"'])\s*" \
               r"(clang|GCC)\s+system_header|" \
               r"(?<![\w.-])-Wno-|" \
               r"(?<![\w.-])-fno-sanitize|" \
               r"(?<![\w.-])-fsanitize-(ignorelist|blacklist" \
               r"|coverage-ignorelist)|" \
               r"__(a|l|ub|t|m)san_(default_options|default_suppressions" \
               r"|disable|enable|do_leak_check|ignore_object|handle_)|" \
               r"\b(ASAN|LSAN|UBSAN|TSAN|MSAN)_OPTIONS\b|" \
               r"detect_leaks\s*=\s*0|" \
               r"\bno_sanitize\w*|" \
               r"no_address_safety_analysis|" \
               r"__SANITIZE_ADDRESS__|" \
               r"__has_feature\s*\(\s*(address|leak|memory|thread)_sanitizer|" \
               r"\b(clang|GCC)\s+(system_header\b|diagnostic\s+(ignored|warning)" \
               r"\b(?![ \t]*[().,:;!?'\-]))|" \
               r"(?:^|\*/)(?:\s|/\*.*?\*/)*(?:#|%:)(?:\s|/\*.*?\*/)*\d+" \
               r"(?:\s|/\*.*?\*/)*\"[^\"]*\"[^\n]*?(?<![\w.])3(?![\w.])|" \
               r"(?:^|\*/)(?:\s|/\*.*?\*/)*(?:#|%:)(?:\s|/\*.*?\*/)*" \
               r"(?:\d[^\n]*)?\\$"

# Three things this expression learned from a review of step 1:
#
#   * `_Pragma("GCC diagnostic ignored \"-Wfoo\"")` is the same instruction as
#     `#pragma GCC diagnostic ignored`, spelled so that it survives macro
#     expansion. A scan that reads only the `#` form forbids the spelling an
#     agent would reach for by accident and permits the one it would reach for
#     on purpose.
#   * `diagnostic warning` demotes an error to a warning, which under `-Werror`
#     is exactly as effective as `ignored` and was not matched.
#   * `-Wno-(?!error)` exempted `-Wno-error`, which turns the whole of
#     step 1's requirement 12 off in one option. There is no reason to carve
#     it out;
#     the carve-out was a guess about false positives, not a measurement.
#
# The second half of the expression arrived with step 2, which is the first
# step scored on what the programs do under a sanitizer rather than only on
# what the compiler says about their source. A sanitizer is silenced from
# inside the tree in more ways than a compiler is, and each of these was
# measured against the seven trees this task has built so far -- the pin, the
# reference solution, the blind run's tree and the four calibration trees --
# before it was added: all seven answer zero.
#
#   * `-fno-sanitize=...` and `-fsanitize-ignorelist=` (and its old name
#     `-fsanitize-blacklist=`) turn an instrumentation off from a flag.
#   * `__asan_default_options`, `__lsan_default_suppressions` and friends are
#     the documented way a program overrides the runtime's options without an
#     environment variable at all -- a weak symbol the runtime calls at
#     startup. Defining one is the in-tree spelling of `detect_leaks=0`.
#   * `__lsan_disable()`, `__lsan_ignore_object()`, `__lsan_do_leak_check()`:
#     the same thing at a finer grain.
#   * `ASAN_OPTIONS` and its siblings, named anywhere a build script or a
#     listfile could export them.
#   * `no_sanitize`, in all of its spellings -- the GNU attribute, the C++11
#     `[[clang::no_sanitize]]`, and the retired `no_address_safety_analysis`.
#   * `__SANITIZE_ADDRESS__` and `__has_feature(address_sanitizer)`: a tree
#     that compiles the leaking branch out when it is being instrumented is
#     not a tree that stopped leaking.
#
# The last three alternatives arrived with the final review's G-1 (wave H,
# 2026-10-13): a project header the compiler takes for a system header has
# every diagnostic in it switched off, which under -Werror is requirement 12
# answered by changing nothing.
#
#   * `(clang|GCC) system_header` and `(clang|GCC) diagnostic ignored` without
#     the `#pragma` / `_Pragma("` in front: `#define P(x) _Pragma(#x)` and
#     `P(GCC system_header)` is the same pragma, and clang honours it (measured,
#     clang 14 and 22).
#   * A GNU line marker carrying flag 3, `# 1 "x.h" 3`, makes the rest of the
#     file a system header. Measured on clang and GCC: the marker is honoured
#     after a comment (`/**/ # 1 "x.h" 3`), spelled with the `%:` digraph, and
#     continued over a backslash-newline, so the expression accepts comments
#     where whitespace may stand, both introducers, and refuses a marker (or a
#     bare introducer) that ends in a line splice. `#line 1 "x.h" 3` is not a
#     third spelling: both compilers reject the flag as "extra tokens at end of
#     #line directive", which -Werror makes fatal. Trigraph `??=` is likewise
#     fatal under -Wtrigraphs. The pin and all eleven reference trees contain
#     no line marker of any kind (measured).
#
# The option route (`-isystem` and its relatives on the compile line) and the
# CMake route (`SYSTEM` on an include directory) are not text in a source file
# and are read where they take effect: _system_include_options and
# _cmake_system_markers below, and the `-M`/`-MM` comparison in
# _scan_dependencies, which asks the compiler itself.
#
# Wave H round 4 (H-IS4, 2026-10-14), two changes to the pragma words:
#
#   * After `diagnostic ignored|warning` the next thing on the line may not be
#     one of `( ) . , : ; ! ? ' -`. None of them can start the warning name
#     the pragma needs, so none of them is a pragma (measured, clang 14 and 22,
#     GCC 11 and 16: every one leaves the warning on). That stops prose such
#     as "the GCC diagnostic warning." or "(GCC diagnostic warning)" failing
#     every gate. It cannot go further, because a real pragma also has its
#     words followed by an identifier (`P2(GCC diagnostic ignored w)` in a
#     macro body, `$W`, `%:w`), by a raw string, by a comment, or by nothing at
#     all (`#define DI GCC diagnostic ignored`, and a macro argument continued
#     on the next line) -- all measured to silence the warning -- and an
#     identifier or a line end is all that tells "warning here" in a comment
#     apart. Telling comments from code needs a C++ lexer; an approximate one
#     is a way to hide a real pragma behind a `"//"` or a digit separator.
#     `system_header` keeps a bare word boundary: clang 14 and 22 accept the
#     pragma with any tokens after it (`P(GCC system_header extra)` builds
#     under -Werror), so no follower can be refused there.
#   * The words are also read the way the compiler reads them: line splices
#     removed first (translation phase 2), then comments and line breaks
#     between the words taken as the spaces they are (phase 3). Measured on
#     clang 22: `#pragma GCC /**/ diagnostic ignored "-W..."`, `P(GCC
#     diagnostic` + newline + `ignored "-W...")`, `P(GCC // c` + newline +
#     `diagnostic ...)`, `#pragma GCC diagnostic \` + newline + `ignored ...`
#     and `diag\` + newline + `nostic` all silence the warning, and a scan of
#     one physical line at a time saw none of them. See suppression_lines.
#
# `-Wno-`, `-fno-sanitize` and `-fsanitize-ignorelist` are anchored on a
# negative lookbehind rather than on whitespace. `(^|\s)-Wno-` is blind to
# `target_compile_options(ise PRIVATE "-Wno-unused")`, where the character
# before the dash is a quote: measured, and the reason the anchor changed.

# What it deliberately does NOT match: `#pragma warning(disable: 4355)` and
# `#pragma warn -pch`. Those are MSVC and Borland, the pinned tree already
# contains four of them inside `#ifdef ISE_COMPILER_VC`, and no compiler this
# environment runs reads either. Matching them would make the baseline -- and
# every correct solution that leaves the Windows branches alone, as the
# instruction requires -- fail every gate at once for a suppression aimed at a
# compiler that is not here.

_SUPPRESSION_LINE = re.compile(SUPPRESSIONS)

# Translation phase 2: a backslash that ends a line joins it to the next one,
# wherever it stands -- inside a word as well (`diag\` + newline + `nostic`).
# Trailing blanks before the newline are allowed for: both compilers warn
# about them, and the scan should not be the stricter of the two.
LINE_SPLICE = re.compile(r"\\[ \t]*\r?\n")

# What phase 3 makes one space between two pragma words: white space, a line
# break included (a macro argument goes on over lines), a block comment, or a
# line comment together with the line break that ends it. The line comment
# runs to its newline in one piece, so a comment full of `//` cannot be cut
# up in many ways while the expression backtracks.
_PP_SPACE = r"(?:\s|/\*(?:[^*]|\*+[^*/])*\*+/|//[^\n]*(?:\n|\Z))"

# The pragma words with that space between them, read over a whole file after
# phase 2. The follower rule is the one in SUPPRESSIONS, for the same reason.
PRAGMA_WORDS = re.compile(
    r"\b(?:clang|GCC)" + _PP_SPACE + r"+(?:system_header\b|diagnostic"
    + _PP_SPACE + r"+(?:ignored|warning)\b(?![ \t]*[().,:;!?'\-]))")


def suppression_lines(body):
    """Every line of `body` with a suppression on it, as (line number, text).

    Line numbers and text are the file's own (physical) lines. Three reads,
    all kept: each physical line against SUPPRESSIONS, as every scan here
    always did; each logical line -- splices removed -- against SUPPRESSIONS,
    so that a token cut by a splice (`no_sani\` + newline + `tize`) is whole
    again; and the logical text as one piece against PRAGMA_WORDS, whose
    words may be split by comments and line breaks. One entry per physical
    line.
    """
    physical = body.splitlines()
    found = {}
    for lineno, line in enumerate(physical, 1):
        if _SUPPRESSION_LINE.search(line):
            found.setdefault(lineno, line)
    pieces, cuts, size, last = [], [], 0, 0
    for match in LINE_SPLICE.finditer(body):
        pieces.append(body[last:match.start()])
        size += match.start() - last
        cuts.append(size)
        last = match.end()
    pieces.append(body[last:])
    text = "".join(pieces)

    def line_of(offset):
        # Newlines before `offset` in the joined text, plus the ones the
        # splices before it took away.
        return (text.count("\n", 0, offset)
                + bisect.bisect_right(cuts, offset) + 1)

    def keep(lineno):
        found.setdefault(lineno, physical[lineno - 1]
                         if 0 < lineno <= len(physical) else "")

    if cuts:
        offset = 0
        for line in text.split("\n"):
            if _SUPPRESSION_LINE.search(line):
                keep(line_of(offset))
            offset += len(line) + 1
    for match in PRAGMA_WORDS.finditer(text):
        keep(line_of(match.start()))
    return sorted(found.items())


# Where a suppression can actually be written. Narrower than SOURCE_GLOBS,
# which includes every `.txt` for the sake of `CMakeLists.txt`: a suppression
# precondition that fails every gate at once must not be armed by a handoff
# note that mentions the flag it did not use. Every file that a compiler, a
# CMake run or a build script reads is still scanned.
# Which extensions are on that list is the compiler's business and not this
# module's taste. Clang decides what a file is from how the build names it, so
# `#include "silence.inc"` silences exactly as well as `#include
# "silence.h"` and a list that stops at the spellings this project happens to
# use forbids the pragma in the files an agent would reach for by accident and
# permits it in the ones it would reach for on purpose. Measured: a
# `#pragma clang diagnostic ignored` in `ise/main/silence.inc` was read by the
# compiler and by no gate. Every extension a C++ compiler will accept as a
# header or a translation unit is therefore here.
SUPPRESSION_FILES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx", ".inl",
                     ".ixx", ".cppm", ".cmake", ".sh", "CMakeLists.txt",
                     ".inc", ".ipp", ".tcc", ".hh", ".h++", ".c++", ".cp",
                     ".def", ".ii", ".m", ".mm", ".pch")

# `-include <path>` and `-imacros <path>`, in both the separated and the
# attached spelling and with either one or two leading dashes. A header forced
# into every translation unit from outside the tree is read by the compiler on
# every compile and walked by nothing under `src`, which is the other half of
# the same hole: _scan_outside_tree reads whatever these name.
FORCED_HEADER = re.compile(r"(?:^|\s)--?(?:include|imacros)[=\s]*(\S+)")


def _scan_outside_tree(src, db, scanned=()):
    """Suppressions in files the build reads from outside the repository.

    `walk` is a walk of `src`, so it can only ever see what the agent put in
    the repository. The compile database names the rest: the translation units
    themselves, wherever the build decided they live, and every header the
    build forces into them. A `#pragma clang diagnostic ignored` in a file
    written to /tmp at configure time and force-included into the library is
    honoured by Clang on every compile and is not under `src` at all, so it is
    read here and folded into the same precondition. Paths inside `src` are
    skipped only when `walk` already read them -- `scanned` is the list it
    read. A translation unit or forced header under a directory `walk` prunes
    as build output (`build/`, `debug/`, `CMakeFiles/`, ...) is still a file
    the compiler reads on every compile, and skipping every in-tree path used
    to leave exactly those unread (reward-hacking audit 2026-10-12, F1). The
    rest are reported absolute, so that the log says plainly where the file
    is.
    """
    if not db:
        return []
    pattern = re.compile(SUPPRESSIONS)
    covered = set(scanned)
    wanted, seen = [], set()
    for entry in db:
        directory = entry.get("directory") or src
        command = entry.get("command") or " ".join(entry.get("arguments", []))
        candidates = [entry["file"]] if entry.get("file") else []
        candidates += FORCED_HEADER.findall(command)
        for path in candidates:
            full = os.path.abspath(os.path.join(directory, path))
            if full in seen:
                continue
            seen.add(full)
            if (full == src or full.startswith(src + os.sep)) \
                    and os.path.relpath(full, src) in covered:
                continue
            if not os.path.isfile(full):
                continue
            wanted.append(full)
    hits = []
    for full in sorted(wanted):
        try:
            body = read(full)
        except OSError:
            continue
        for lineno, line in suppression_lines(body):
            hits.append("%s:%d: %s" % (full, lineno, line.strip()[:160]))
    return hits


# The candidate's user. helpers.sh's SCORE_UID; the two must agree.
CANDIDATE_UID = 65534

# What the compiler lists as a dependency but is not text it reads as source:
# module interfaces it loaded and the build's own objects. Everything else it
# opened is scanned whatever its name.
DEP_NOT_SOURCE = (".pcm", ".gcm", ".ifc", ".bmi", ".modmap", ".ddi", ".o",
                  ".a", ".so")


def _reap_candidate():
    """Kill every live process of the candidate's user, until there are none.

    Only in the verifier -- root, with a launch prefix to drop to -- and never
    when this module is driven by hand. Zombies are not counted: the
    container's PID 1 does not reap, so a dead process can stay listed for
    the rest of the run without being able to do anything.
    """
    if os.geteuid() != 0 or not os.environ.get("ISEFIX_LAUNCH_PREFIX"):
        return
    for _ in range(50):
        live = []
        for name in os.listdir("/proc"):
            if not name.isdigit():
                continue
            try:
                with open("/proc/%s/status" % name) as handle:
                    status = handle.read()
            except OSError:
                continue
            uids = re.search(r"^Uid:\s+(.*)$", status, re.M)
            state = re.search(r"^State:\s+(\S)", status, re.M)
            if not uids or not state or state.group(1) in "ZX":
                continue
            if str(CANDIDATE_UID) in uids.group(1).split():
                live.append(int(name))
        if not live:
            return
        for pid in live:
            try:
                os.kill(pid, 9)
            except OSError:
                pass
        time.sleep(0.05)


def authored_suppressions(src):
    """Suppressions in the files `walk` prunes, read before any build ran.

    `walk` skips `build/`, `debug/`, `release/`, `log/`, `CMakeFiles/` and
    `.cache/` because after a configure they hold the build's own output. At
    the moment snapshot_tree.py calls this -- after the stale-output sweep and
    before the configure -- nothing of the build's exists yet, so whatever is
    under those names was put there by the candidate and is read here on the
    same terms as every other authored file: the extensions in
    SUPPRESSION_FILES, the raw line, the SUPPRESSIONS expression. Without it a
    `#pragma clang diagnostic ignored` in `ise/main/build/quiet.h` was honoured
    by the compiler and read by no gate (reward-hacking audit 2026-10-12, F1).
    """
    pattern = re.compile(SUPPRESSIONS)
    hits = []
    for root, dirs, names in os.walk(src):
        dirs[:] = sorted(d for d in dirs if d not in (".git", "__pycache__"))
        rel_root = os.path.relpath(root, src)
        parts = set(rel_root.split(os.sep)) if rel_root != "." else set()
        if not parts & SKIP_DIRS:
            continue
        for name in sorted(names):
            rel = os.path.normpath(os.path.join(rel_root, name))
            if not (rel in SUPPRESSION_FILES or name in SUPPRESSION_FILES
                    or os.path.splitext(name)[1] in SUPPRESSION_FILES):
                continue
            try:
                body = read(os.path.join(root, name))
            except OSError:
                continue
            for lineno, line in suppression_lines(body):
                hits.append("%s:%d: %s" % (rel, lineno, line.strip()[:160]))
    return hits


def _snapshot_suppressions():
    """What authored_suppressions found before the configure, or a refusal.

    snapshot_tree.py writes it beside the tree digest. No snapshot at all is
    the authoring path and _tree_rewritten answers for it; a digest without
    this file beside it is a snapshot that stopped half way, and is refused
    the way a missing digest is.
    """
    path = os.environ.get(SNAPSHOT_ENV)
    if not path:
        return []
    try:
        with open(path + ".suppressions") as handle:
            hits = json.load(handle)
    except (OSError, ValueError) as exc:
        return ["the pre-configure suppression scan could not be read: %r"
                % (exc,)]
    if not isinstance(hits, list):
        return ["the pre-configure suppression scan is not a list"]
    return [str(hit) for hit in hits]


def _dep_argv(entry):
    """The entry's compile command, asking for its dependency list instead.

    The image's own clang++ in place of whatever the entry names, and without
    the arguments that name an output or load code into the compiler, so what
    runs is a preprocessor pass over the candidate's sources and nothing of the
    candidate's own. None when the entry is not a clang compile.
    """
    try:
        argv = (shlex.split(entry.get("command") or "")
                or [str(a) for a in entry.get("arguments") or []])
    except ValueError:
        return None
    if not argv or not os.path.basename(argv[0]).startswith("clang"):
        return None
    out, skip = [shutil.which("clang++") or "clang++"], False
    # A response file (CMake's module map, `@...modmap`) is expanded here and
    # only its module and language arguments are kept, so nothing else it
    # carries reaches this compiler.
    expanded = []
    for arg in argv[1:]:
        if not arg.startswith("@"):
            expanded.append(arg)
            continue
        try:
            with open(os.path.join(entry.get("directory") or "/",
                                   arg[1:])) as handle:
                words = shlex.split(handle.read(1 << 20))
        except (OSError, ValueError):
            continue
        keep_next = False
        for word in words:
            if keep_next:
                expanded.append(word)
                keep_next = False
            elif word == "-x":
                expanded.append(word)
                keep_next = True
            elif word.startswith("-fmodule-file="):
                expanded.append(word)
    for arg in expanded:
        if skip:
            skip = False
            continue
        if arg in ("-o", "-MF", "-MT", "-MQ", "-Xclang", "-Xlinker"):
            skip = True
            continue
        if arg in ("-c", "-M", "-MM", "-MD", "-MMD", "-MG", "-MP", "-E",
                   "--precompile"):
            continue
        if arg.startswith(("-MF", "-MT", "-MQ", "-fplugin", "-fpass-plugin",
                           "-fmodule-output", "-Wl,")) \
                or (arg.startswith("-o") and len(arg) > 2):
            continue
        out.append(arg)
    return out + ["-M", "-MG"]


def _parse_deps(text):
    """The file names in `-M` output: make syntax, `\\ ` for a space."""
    names = []
    for line in text.replace("\\\n", " ").splitlines():
        head, sep, rest = line.partition(": ")
        if not sep:
            continue
        for token in re.findall(r"(?:\\.|[^\s\\])+", rest):
            names.append(re.sub(r"\\(.)", r"\1", token))
    return names


def _scan_dependencies(ctx, scanned):
    """Suppressions in what the compiler actually opens, beyond `walk`.

    The other two scans read what was authored (`walk`, and
    authored_suppressions for the directories `walk` prunes) and what the
    database names (_scan_outside_tree). Neither reads a header the build
    generated and a translation unit includes. This one asks the compiler:
    every database entry is preprocessed once with `-M`, as the candidate's
    user, and every file it lists is read unless `walk` already read it, or it
    lies outside the tree and the candidate's user does not own it (the
    toolchain's and the system's headers, which are full of exactly the
    pragmas this looks for and which no candidate wrote). A unit that does not
    preprocess is skipped and counted, never fatal: one file left broken
    mid-edit must not be able to take the precondition down.
    """
    db = ctx.get("db")
    if not db:
        return []
    src = ctx["src"]
    covered = set(scanned)
    prefix = (os.environ.get("ISEFIX_LAUNCH_PREFIX") or "").split()
    items = []
    for entry in db:
        argv = _dep_argv(entry)
        if argv:
            items.append((entry.get("directory") or src, argv))

    def one(item):
        directory, argv = item
        code, out, _err = _run(prefix + argv, cwd=directory)
        if code != 0:
            return directory, None, None
        # The same pass again with -MM, which lists only what the compiler
        # did not take for a system header. Whatever -M lists and -MM does
        # not is, by the compiler's own account, a system header -- however
        # it came to be one. argv ends in ["-M", "-MG"] (_dep_argv).
        code, quiet, _err = _run(prefix + argv[:-2] + ["-MM", "-MG"],
                                 cwd=directory)
        return (directory, _parse_deps(out),
                _parse_deps(quiet) if code == 0 else None)

    found, ok, as_system, asked = set(), 0, {}, 0
    for directory, deps, user in _parallel(items, one, ctx.get("jobs", 7)):
        if deps is None:
            continue
        ok += 1
        full_deps = [os.path.normpath(os.path.join(directory, dep))
                     for dep in deps]
        found.update(full_deps)
        if user is not None:
            asked += 1
            user = set(os.path.normpath(os.path.join(directory, dep))
                       for dep in user)
            for full in full_deps:
                if full not in user:
                    as_system.setdefault(full, full_deps[0] if full_deps
                                         else directory)
    _reap_candidate()

    wanted = []
    for full in sorted(found):
        if full.endswith(DEP_NOT_SOURCE) or not os.path.isfile(full):
            continue
        if full == src or full.startswith(src + os.sep):
            if os.path.relpath(full, src) in covered:
                continue
        else:
            try:
                if os.stat(full).st_uid != CANDIDATE_UID:
                    continue
            except OSError:
                continue
        wanted.append(full)
    sys.stderr.write("dependency scan: %d of %d translation units "
                     "preprocessed, %d file(s) beyond the walk; %d asked "
                     "for their system headers, %d such header(s)\n"
                     % (ok, len(items), len(wanted), asked, len(as_system)))

    pattern = re.compile(SUPPRESSIONS)
    hits = []
    for full in wanted:
        try:
            body = read(full)
        except OSError:
            continue
        for lineno, line in suppression_lines(body):
            hits.append("%s:%d: %s" % (full, lineno, line.strip()[:160]))
    for full in sorted(as_system):
        if full.endswith(DEP_NOT_SOURCE):
            continue
        if _candidate_place(full, ctx, is_file=True):
            hits.append("%s: compiled as a system header (in %s), so nothing "
                        "in it is reported" % (full, as_system[full]))
    return hits


# ---------------------------------------------------------------------------
# Project headers presented as system headers (final review G-1, wave H)
#
# Requirement 13 at steps 1-6 and "No diagnostic is suppressed" at 7-11 forbid
# presenting a project header as the system's, and a compiler says nothing
# about a system header -- not under -Wall, not under -Werror. The pragma and
# the line marker are text in a header and SUPPRESSIONS reads them. The two
# routes here are not text in a source file:
#
#   * an option on the compile line that makes a directory a system include
#     directory, or a header found anywhere a system header. CMake writes one
#     for `target_include_directories(ise SYSTEM PUBLIC ...)`, for an
#     IMPORTED target's include directories, and for nothing else the project
#     says in words that look like "system";
#   * the CMake that asked for it.
#
# What is refused is the option pointed somewhere the candidate wrote or could
# have written: the tree, the build directory, a directory the candidate's
# user owns or that anyone may write, or an ancestor of any of those. The
# toolchain's own directories are system directories by design and stay
# allowed under every spelling -- `-isystem /usr/include/boost` included.

# Each of these puts its operand on the system include path, or decides where
# the system include path is. Measured against a header that fails -Wall
# -Wextra -Woverloaded-virtual -Werror through -I (clang 14 and 22, GCC 11):
# -isystem, -idirafter, -iwithprefix, -cxx-isystem, --include-directory-after,
# --include-with-prefix, -internal-isystem through -Xclang, -iwithsysroot
# under -isysroot, `-isystem=` under --sysroot, and the same options through
# -Wp, and -Xpreprocessor, each made it build clean. The rest are their
# documented relatives: the per-language -isystem forms, -iframework*,
# -imultilib, -resource-dir (whose include/ is a system directory),
# -ivfsoverlay (which can serve any file under any name), and the clang-cl
# spellings. `-iwithprefixbefore` and `--include-with-prefix-before` are
# listed only so that the longest-prefix match below does not read them as
# `-iwithprefix`: they add a directory where -I would, and a header found
# through one still warns (measured).
SYSTEM_DIR_OPTIONS = (
    "-isystem", "-isystem-after", "-idirafter", "--include-directory-after",
    "-iwithprefix", "--include-with-prefix", "--include-with-prefix-after",
    "-iprefix", "--include-prefix", "-cxx-isystem", "-c-isystem",
    "-objc-isystem", "-objcxx-isystem", "-internal-isystem",
    "-internal-externc-isystem", "-iwithsysroot", "-isysroot", "--sysroot",
    "-imultilib", "-iframework", "-iframeworkwithsysroot", "-resource-dir",
    "-ivfsoverlay", "-imsvc", "/external:I", "-external:I",
)
# Operands that are an include-spelling prefix rather than a directory.
SYSTEM_PREFIX_OPTIONS = ("--system-header-prefix", "-isystem-prefix")
NOT_SYSTEM_OPTIONS = ("-iwithprefixbefore", "--include-with-prefix-before",
                      "--no-system-header-prefix", "-ino-system-prefix")
_OPTIONS_LONGEST_FIRST = sorted(
    SYSTEM_DIR_OPTIONS + SYSTEM_PREFIX_OPTIONS + NOT_SYSTEM_OPTIONS,
    key=len, reverse=True)
# GCC's and clang's environment spellings of -isystem.
SYSTEM_PATH_ENV = re.compile(r"^(C|CPLUS|OBJC|OBJCPLUS)_INCLUDE_PATH=(.*)$")


def _writable_roots():
    """Directories near the top of the filesystem the candidate can write.

    An ancestor of one of these holds the candidate's files somewhere under
    it, so `-isystem /` is as much a route as `-isystem /tmp`. Two levels
    deep is where a stock image keeps them (/tmp, /var/tmp, /dev/shm,
    /run/lock); anything deeper is still caught when it is the operand itself.
    """
    def entries(path):
        try:
            if os.path.isdir(path) and not os.path.islink(path):
                return sorted(os.listdir(path))
        except OSError:
            pass
        return []

    roots = []
    for top in entries("/"):
        if top in ("proc", "sys"):
            continue
        first = os.path.join("/", top)
        for path in [first] + [os.path.join(first, name)
                               for name in entries(first)]:
            try:
                st = os.lstat(path)
            except OSError:
                continue
            if not os.path.isdir(path) or os.path.islink(path):
                continue
            if st.st_uid == CANDIDATE_UID or st.st_mode & 0o002:
                roots.append(path)
    return roots


def _candidate_place(path, ctx, base=None, is_file=False):
    """True when `path` is somewhere the candidate wrote or could have written.

    The tree and the build directory; a path under /proc, which names
    whatever the reading process makes it name; anything that does not exist
    (a virtual file system overlay serves headers that are not on disk); a
    file or directory owned by the candidate's user, or with such an owner,
    or a world-writable directory, between it and the root; and, for a
    directory operand, an ancestor of the tree, the build directory or any
    directory the candidate can write.
    """
    if not path:
        return True
    full = path if os.path.isabs(path) else os.path.join(
        base or ctx["build"], path)
    full = os.path.normpath(full)
    if full == "/proc" or full.startswith("/proc/"):
        return True
    real = os.path.realpath(full)
    roots = [os.path.realpath(ctx["src"]), os.path.realpath(ctx["build"])]
    for root in roots:
        if real == root or real.startswith(root + os.sep):
            return True
    if is_file and not os.path.exists(real):
        return True
    probe = real
    while True:
        try:
            st = os.stat(probe)
        except OSError:
            st = None
        if st is not None:
            if st.st_uid == CANDIDATE_UID:
                return True
            if os.path.isdir(probe) and st.st_mode & 0o002:
                return True
        parent = os.path.dirname(probe)
        if parent == probe:
            break
        probe = parent
    if not is_file:
        if "writable_roots" not in ctx:
            ctx["writable_roots"] = _writable_roots()
        for root in roots + ctx["writable_roots"]:
            if real == "/" or root.startswith(real.rstrip(os.sep) + os.sep):
                return True
    return False


def _expand_argv(argv, directory):
    """The arguments as the compiler reads them.

    Response files expanded (one level is all clang reads before a nested
    `@` is itself expanded, and the compile-database gates refuse a nested
    one anyway); `-Xclang X`, `-Xpreprocessor X` and `-Xarch_<a> X` reduced
    to X; `-Wp,a,b` to a and b.
    """
    out, pending = [], list(argv)
    budget = 64
    while pending:
        arg = pending.pop(0)
        if arg.startswith("@") and budget > 0:
            budget -= 1
            path = os.path.join(directory or "/", arg[1:])
            if not os.path.isfile(path):
                out.append(arg)
                continue
            try:
                with open(path, encoding="utf-8", errors="replace") as handle:
                    pending[0:0] = shlex.split(handle.read(1 << 20))
            except (OSError, ValueError):
                out.append(arg)
            continue
        if arg in ("-Xclang", "-Xpreprocessor") or arg.startswith("-Xarch_"):
            if pending:
                out.append(pending.pop(0))
            continue
        if arg.startswith("-Wp,"):
            out.extend(a for a in arg[4:].split(",") if a)
            continue
        out.append(arg)
    return out


def _system_include_options(ctx):
    """Compile-line options that make the candidate's headers system headers."""
    db = ctx.get("db")
    if not db:
        return []
    src = ctx["src"]
    tree_names = None
    hits = []
    for entry in db:
        directory = entry.get("directory") or ctx["build"]
        try:
            argv = (shlex.split(entry.get("command") or "")
                    or [str(a) for a in entry.get("arguments") or []])
        except ValueError:
            continue
        rel = os.path.relpath(entry.get("file") or "?", src)
        args = _expand_argv(argv, directory)
        sysroot, iprefix = "", ""
        for i, arg in enumerate(args):
            match = SYSTEM_PATH_ENV.match(arg)
            if match:
                for part in match.group(2).split(":"):
                    if _candidate_place(part, ctx, directory):
                        hits.append("%s: compiled with %s" % (rel, arg[:160]))
                        break
                continue
            for option in _OPTIONS_LONGEST_FIRST:
                if arg == option:
                    operand = args[i + 1] if i + 1 < len(args) else ""
                elif arg.startswith(option):
                    operand = arg[len(option):]
                    if option.startswith("--") and operand.startswith("="):
                        operand = operand[1:]
                else:
                    continue
                break
            else:
                continue
            if option in NOT_SYSTEM_OPTIONS:
                continue
            if option in SYSTEM_PREFIX_OPTIONS:
                if tree_names is None:
                    tree_names = set()
                    for name in walk(src, SUPPRESSION_FILES):
                        parts = name.split(os.sep)
                        tree_names.update("/".join(parts[k:])
                                          for k in range(len(parts)))
                if not operand or any(name.startswith(operand)
                                      for name in tree_names):
                    hits.append("%s: compiled with %s %s: the tree's own "
                                "headers become system headers"
                                % (rel, option, operand[:120]))
                continue
            if option in ("--sysroot", "-isysroot"):
                sysroot = operand
            if option in ("-iprefix", "--include-prefix"):
                iprefix = operand
            places = [operand]
            if operand.startswith("="):
                places = [operand[1:], (sysroot or "/") + "/" + operand[1:]]
            elif operand.startswith("$SYSROOT"):
                places = [operand[8:], (sysroot or "/") + "/" + operand[8:]]
            if option in ("-iwithprefix", "--include-with-prefix",
                          "--include-with-prefix-after"):
                places.append(iprefix + operand)
                if ".." in operand.split("/"):
                    places.append(ctx["src"])
            if option == "-iwithsysroot":
                places.append((sysroot or "/") + "/" + operand)
            if any(_candidate_place(place, ctx, directory) for place in places):
                hits.append("%s: compiled with %s %s: what is found there is "
                            "a system header to the compiler"
                            % (rel, option, operand[:120]))
    return list(dict.fromkeys(hits))


# CMake's ways of asking for that option. A configure trace has every
# argument expanded, so the paths are the paths CMake used; relative ones are
# relative to the directory of the listfile that ran the command.
CMAKE_INCLUDE_COMMANDS = ("target_include_directories", "include_directories")
CMAKE_KEYWORDS = {"SYSTEM", "BEFORE", "AFTER", "INTERFACE", "PUBLIC",
                  "PRIVATE"}
GENEX_HEAD = re.compile(r"^(\$<[^:<>]*:)+")


def _cmake_system_markers(ctx):
    """CMake commands that mark the tree's own directories as system ones."""
    events = ctx.get("trace")
    if not events:
        return []
    hits = []
    for event in events:
        cmd = str(event.get("cmd", "")).lower()
        args = [str(a) for a in event.get("args") or []]
        where = "%s:%s" % (event.get("file", "?"), event.get("line", "?"))
        base = os.path.dirname(str(event.get("file") or ctx["src"]))
        if cmd in CMAKE_INCLUDE_COMMANDS and "SYSTEM" in args:
            paths = args[1:] if cmd == "target_include_directories" else args
            for value in paths:
                if value in CMAKE_KEYWORDS:
                    continue
                for part in value.split(";"):
                    part = GENEX_HEAD.sub("", part).rstrip(">")
                    if part and _candidate_place(part, ctx, base):
                        hits.append("%s: %s(... SYSTEM ... %s)"
                                    % (where, cmd, part[:120]))
                        break
        elif cmd == "add_subdirectory" and "SYSTEM" in args[1:]:
            if args and _candidate_place(args[0], ctx, base):
                hits.append("%s: add_subdirectory(%s ... SYSTEM)"
                            % (where, args[0][:120]))
        elif cmd in ("set_target_properties", "set_property",
                     "set_directory_properties"):
            for k, value in enumerate(args[:-1]):
                if value == "SYSTEM" and args[k + 1].upper() not in (
                        "", "0", "OFF", "NO", "FALSE", "N", "IGNORE",
                        "NOTFOUND") and not args[k + 1].upper().endswith(
                            "-NOTFOUND"):
                    hits.append("%s: %s(... SYSTEM %s)"
                                % (where, cmd, args[k + 1][:40]))
                if value == "INTERFACE_SYSTEM_INCLUDE_DIRECTORIES":
                    for part in ";".join(args[k + 1:]).split(";"):
                        part = GENEX_HEAD.sub("", part).rstrip(">")
                        if part.startswith("/") and _candidate_place(
                                part, ctx, base):
                            hits.append("%s: %s(... %s %s)"
                                        % (where, cmd, value, part[:120]))
                            break
    return list(dict.fromkeys(hits))


# ---------------------------------------------------------------------------
# compile database


def _generator_db(build):
    """The database as the graph on disk describes it, read back now."""
    code, out, err = _run(["ninja", "-C", build, "-t", "compdb"])
    if code != 0:
        return None, "generator", (
            "`ninja -t compdb` in %s exited %d: %s"
            % (build, code, (err or out or "").strip()[:400]))
    try:
        return json.loads(out), "generator", None
    except ValueError as exc:
        return None, "generator", (
            "`ninja -t compdb` in %s did not print JSON (%s)" % (build, exc))


def _scrubbed_env():
    """os.environ minus everything this harness told itself.

    ninja reads a manifest the tree being scored wrote, and a manifest names
    commands. Nothing under ISEFIX_ is meant for a child of that tree --
    ISEFIX_GRAPH_DIR is the stamp's own path, and handing that to anything the
    graph can start is handing over the answer key.
    """
    return dict((key, value) for key, value in os.environ.items()
                if not key.startswith("ISEFIX_"))


def _stamped_db(build, stamp):
    """The database root took between the configure and the build.

    Two files, both written by root into a directory the candidate cannot
    name: the database itself, and the sha256 of the build.ninja it came from.
    The hash is what makes the database still be about this build. Ninja
    reloads its graph mid-build when its own regenerate edge fires, and that
    edge runs CMake again -- a second configure, reached from the first, could
    write a graph the stamp never saw and compile from it. The regenerate edge
    has build.ninja as its output, so any such reload moves the file's hash,
    and a graph whose hash has moved is refused rather than read. Nothing
    legitimate moves it: build_candidate.sh deletes the caches and configures
    from clean, so the graph is newer than every listfile it depends on.
    """
    path = os.path.join(stamp, "compdb.json")
    digest = os.path.join(stamp, "build.ninja.sha256")
    if not os.path.exists(path) or not os.path.exists(digest):
        return None, "stamp", (
            "the verifier stamped no build graph between the configure and "
            "the build of %s, so there is no record of what the compiler was "
            "told that the build itself could not have written afterwards"
            % build)
    with open(digest) as handle:
        stamped = handle.read().split()
    want = stamped[0] if stamped else ""
    with open(os.path.join(build, "build.ninja"), "rb") as handle:
        have = hashlib.sha256(handle.read()).hexdigest()
    if not want or want != have:
        return None, "stamp", (
            "build.ninja in %s is no longer the file the configure wrote: "
            "sha256 %s when the verifier stamped it, %s now. A build that "
            "reloads its own graph has run a second configure this verifier "
            "never saw, and nothing here can say which of the two the "
            "compiler was driven from" % (build, want[:16] or "<none>",
                                          have[:16]))
    try:
        with open(path) as handle:
            return json.load(handle), "stamp", None
    except ValueError as exc:
        return None, "stamp", (
            "the build graph stamped for %s is not JSON (%s)" % (build, exc))


def _is_scan_edge(entry):
    """A C++20 module-dependency scan edge, not a compile. See derive_db."""
    return ("-format=p1689" in (entry.get("command") or "")
            and (entry.get("output") or "").endswith(".ddi"))


def derive_db(build):
    """(entries, source, problem).

    Never the copy under the build directory: that file is writable by the
    tree being scored, and a POST_BUILD step that rewrites it with the flags
    the step asks for -- while the compiler ran with -w -- is the first half
    of the cheat this exists to refuse. `ninja -t compdb` reads build.ninja,
    which is the file the build was actually driven from, and reports the
    command each edge runs. Of its entries, the ones carrying ` -c ` are the
    translation units; their flags are CMake's own, down to the depfile
    arguments ninja's copy also carries.

    The second half is that build.ninja is writable too, and `ninja -n` was
    the whole of the answer to that: ninja records every edge's command in
    .ninja_log and compares it with the graph, so a graph whose commands
    changed since the build has work to do, and a tree built from the graph it
    ships prints "no work to do" in well under a second. The comparison is a
    hash; the hash is MurmurHash64A over the evaluated command with ninja's
    fixed seed; and twenty lines of Python re-hash the entries a rewrite
    touched. Measured on a tree whose every translation unit was compiled with
    -w: `ninja -n` up to date, database clean, 14 of 14 gates, reward
    1.000000 (authoring/check_cmake_trace.py's FORGEDGRAPH).

    So when the verifier has stamped a database, that is the one read here.
    test.sh takes it as root between the configure that wrote the graph and
    the build that reads it, with no candidate process alive in between. What
    it holds is what ninja went on to execute, because ninja loads its graph
    at startup: rewriting the file afterwards changes what a later reader is
    told, not what the compiler was told. The `ninja -n` check stays in front
    of it -- a graph that no longer matches .ninja_log is a tree nothing here
    can describe, stamp or no stamp -- and _stamped_db adds the one check the
    stamp itself needs.

    Without the stamp there is no database. The old, weaker derivation is
    still reachable for an authoring script that drives run_gates directly and
    has no verifier around it to take a stamp, but only by setting
    ISEFIX_ALLOW_UNSTAMPED_DB=1 on purpose: an absent stamp inside the verifier
    is a refusal, not a downgrade. A gate that answers from weaker evidence
    when its strong evidence is missing is a gate whose strength is optional,
    and anything that can remove the stamp -- a mktemp that failed, a test.sh
    that forgot the call -- would then be removing the check. `source` says
    which of the two answered.
    """
    if not os.path.exists(os.path.join(build, "build.ninja")):
        return None, None, ("no build.ninja in %s: nothing here records what "
                            "the compiler was told" % build)
    # The stamp is read before anything is run against the candidate's graph,
    # and that order is load-bearing. `ninja -n` below loads a manifest the
    # tree wrote, and a manifest can carry a regenerate edge that runs the
    # tree's own CMake -- as root, from here. Ninja does not execute that edge
    # under -n: it prints "Re-running CMake..." and runs nothing, measured
    # against a project that counts its own configures (check_stamp.py's
    # DRYRUN case, ninja 1.12.1). Reading first means the gates do not depend
    # on that measurement staying true of the next ninja.
    stamp = os.environ.get(GRAPH_ENV)
    if stamp:
        raw, source, problem = _stamped_db(build, stamp)
    elif os.environ.get(UNSTAMPED_ENV) == "1":
        raw, source, problem = _generator_db(build)
    else:
        return None, "stamp", (
            "no build graph was stamped for %s. The verifier takes that stamp "
            "between the configure and the build, where the tree cannot reach "
            "it; deriving a database from the finished build instead is the "
            "weaker answer the stamp exists to replace, and it is not used as "
            "a fallback" % build)
    if problem:
        return None, source, problem
    code, out, err = _run(["ninja", "-C", build, "-n"], env=_scrubbed_env())
    text = ((out or "") + (err or "")).strip()
    if code != 0 or "no work to do" not in text:
        return None, source, (
            "`ninja -n` in %s exited %d and did not report an up-to-date "
            "build, so the build graph no longer matches what was built and "
            "nothing in it is evidence of how the compiler ran: %s"
            % (build, code, text[:400]))
    # ` -c ` finds the translation units -- and, from C++20 on, finds each of
    # them twice. CMake drives a C++20 target through two edges per source:
    # the compile, and a module-dependency scan that runs clang-scan-deps over
    # that same compile command and writes a .ddi. The scan edge carries ` -c `
    # because the command it is scanning is the compile command, so it arrives
    # here looking like a second translation unit. It is not one, and it is
    # worse than a duplicate: clang-query handed `-format=p1689` refuses the
    # whole argument list, then parses the file again with no arguments at all
    # -- no include path, no -std -- and answers the gates from that. Measured
    # on cal07-gold: 41 compiles, 41 scans, and 22 sites of "allocations
    # nothing is holding" in examples/ that exist only in the flagless parse,
    # where the matcher's std::unique_ptr exclusions have no types to match.
    #
    # Both marks have to be present to drop an entry -- the p1689 format
    # argument and a .ddi output -- because the purpose is to lose the scans,
    # not to hand a tree a spelling that hides a compile from the gates. What
    # ninja knows as a .ddi output is not an object file, and a source whose
    # object no edge produces does not reach the link.
    entries = [e for e in raw
               if " -c " in (e.get("command") or "")
               and not _is_scan_edge(e)]

    # Written down where the analysers can be pointed at it, because
    # clang-tidy and clang-query take a directory and read the
    # compile_commands.json in it -- and the one in the build directory is the
    # file this function exists not to read. Root writes it, into COMPDB_DIR,
    # whose placement is what keeps the candidate's user from replacing it.
    dest = COMPDB_DIR
    os.makedirs(dest, exist_ok=True)
    os.chmod(dest, 0o755)
    path = os.path.join(dest, "compile_commands.json")
    with open(path, "w") as handle:
        json.dump(entries, handle, indent=1)
    os.chmod(path, 0o644)
    return entries, source, None


def db_entries(ctx, cfg=None):
    db = ctx.get("db")
    if db is None:
        return []
    only = re.compile(cfg["only"]) if cfg and cfg.get("only") else None
    skip = re.compile(cfg["skip"]) if cfg and cfg.get("skip") else None
    out = []
    for entry in db:
        rel = os.path.relpath(entry["file"], ctx["src"]) \
            if entry["file"].startswith(ctx["src"]) else entry["file"]
        if only and not only.search(rel):
            continue
        if skip and skip.search(rel):
            continue
        out.append((rel, entry))
    return out


def gate_compile_db(cfg, ctx):
    """Every matching translation unit was compiled, and compiled as stated.

    `min_entries` is a floor and `cover` is the real check. The floor alone
    measures "at least N things were built", which is not what an instruction
    saying *every* translation unit under a directory means: the pinned tree
    has 22 sources under examples/ and 15 programs, so a floor of 14 was
    satisfied by a tree that never compiled one of them -- and since that one
    was also absent from every behaviour session, deleting its directory
    scored full marks. `cover` enumerates the sources still present in the
    candidate's own tree and requires a database entry for each, so the set
    being checked is the set the candidate shipped and deleting a source
    cannot shrink the requirement. The floor stays underneath it: without it,
    deleting every source would make `cover` vacuously true.

    Each entry's object file is required to be on disk as well. The database
    is a plan, and an entry naming an object that was never written is a
    translation unit that was planned and not compiled -- which is the same
    "compiled as" this gate's description claims, asserted of the one artefact
    that only a compiler run can produce. It is doing more work than it looks
    like since derive_db started reading the plan root stamped before the
    build rather than one re-derived after it: the plan is now certainly the
    one ninja was handed, and the object on disk is what says ninja ran it.

    `compiler` is the same assertion made of argv[0]. Every `require` below is
    a substring of a command line, and a command line is a list whose first
    element decides who reads the rest of it: with

        set(CMAKE_CXX_COMPILER_LAUNCHER /opt/strip-werror)

    CMake prefixes every compile with that script, ninja's copy of the command
    still contains -Werror -- so every `require` is satisfied -- the object
    file is written, `ninja -n` reports the build up to date, and the compiler
    was run without it. The flags a wrapper passes on are its own business, so
    the wrapper is refused instead: argv[0] of each entry is required to be
    the toolchain's own compiler, which is what every instruction that
    inherits this gate asks for in the first place: every source compiled by
    the toolchain's own compiler, run directly, with the options its target
    carries.

    `forbid_arg` is the rest of that argument. Every `require` and `forbid`
    here reads the command as text, and clang has more than one way to be
    handed an option the text does not contain:

        target_compile_options(ise_warnings INTERFACE "@${CMAKE_SOURCE_DIR}/q.rsp")

    puts -w in q.rsp, and the database still says -Wall -Wextra -Werror and
    nothing else. `--config=q.cfg` and `--config q.cfg` are the same route
    through clang's configuration files, and `--no-warnings` is -w spelled so
    that a pattern written for the short form does not see it. All of them
    were measured: a tree carrying the offending function this gate's -Werror
    exists to catch built clean and scored 1.000000 through the first two.

    They are refused by argument and not by substring because a substring
    test reads a quoted command and `"@/app/q r.rsp"` is quoted: the entry
    is split the way a shell splits it -- the same split argv[0] already goes
    through -- and each argument is matched whole.

    What is refused is the carrier rather than its contents, which is the
    narrower claim on purpose: what a response file holds is not knowable
    from the database. Requirement 9 asks that every source be compiled with
    the options its target carries, and an option read out of a file the
    command names is not one of them.
    """
    missing = _no_build(cfg, ctx)
    if missing:
        return False, missing
    if ctx.get("db") is None:
        return False, [_no_database(ctx)]
    entries = db_entries(ctx, cfg)
    if len(entries) < cfg.get("min_entries", 1):
        return False, ["compile database has %d matching entries, need %d"
                       % (len(entries), cfg.get("min_entries", 1))]
    failures = []
    if cfg.get("cover"):
        pattern = re.compile(cfg["cover"])
        present = [rel for rel in walk(ctx["src"], cfg.get("cover_include",
                                                           (".cpp",)))
                   if pattern.search(rel)]
        compiled = set(rel for rel, _ in entries)
        for rel in present:
            if rel not in compiled:
                failures.append("%s: in the tree, never compiled" % rel)
    compiler = re.compile(cfg["compiler"]) if cfg.get("compiler") else None
    for rel, entry in entries:
        command = entry.get("command") or " ".join(entry.get("arguments", []))
        output = entry.get("output")
        try:
            argv = shlex.split(command)
        except ValueError as exc:
            argv = []
            failures.append("%s: the database entry's command does not "
                            "parse as a command line (%s)" % (rel, exc))
        if compiler and argv and not compiler.fullmatch(argv[0]):
            failures.append("%s: compiled by %r, not by the toolchain's "
                            "compiler" % (rel, argv[0][:120]))
        # `arg_file` is the one hole in the response-file refusal above, and
        # it exists because C++20 modules close it from the other side. CMake
        # hands clang the module map -- `-x c++-module`, `-fmodule-output=`,
        # one `-fmodule-file=` per import -- in a generated response file, and
        # there is no spelling of a modules build that does without it. So the
        # carrier is still refused unless it is that file, and when it is that
        # file it is opened and its contents held to the same rules the
        # command line is: no nested response file, no forbidden argument, no
        # forbidden text. What the exception admits is a file this gate has
        # read, which is the property the blanket refusal was standing in for.
        opened = []
        allow = re.compile(cfg["arg_file"]) if cfg.get("arg_file") else None
        for arg in argv:
            forbidden = [p for p in cfg.get("forbid_arg", [])
                         if re.fullmatch(p, arg)]
            if not forbidden:
                continue
            if not (allow and allow.fullmatch(arg)):
                failures.append("%s: compiled with %r, and what that "
                                "carries is not in this database"
                                % (rel, arg[:120]))
                continue
            base = entry.get("directory") or ctx["build"]
            path = arg[1:]
            full = path if os.path.isabs(path) else os.path.join(base, path)
            root = os.path.realpath(ctx["build"])
            real = os.path.realpath(full)
            if not (real == root or real.startswith(root + os.sep)):
                failures.append("%s: %r names a file outside the build "
                                "directory" % (rel, arg[:120]))
                continue
            try:
                with open(real, encoding="utf-8", errors="replace") as handle:
                    body = handle.read()
            except OSError as exc:
                failures.append("%s: %r could not be read (%s)"
                                % (rel, arg[:120], exc))
                continue
            try:
                inner = shlex.split(body)
            except ValueError as exc:
                failures.append("%s: %r does not parse as arguments (%s)"
                                % (rel, arg[:120], exc))
                continue
            for token in inner:
                if token.startswith("@"):
                    failures.append("%s: %r names another response file, %r"
                                    % (rel, arg[:120], token[:120]))
                for pattern in cfg.get("forbid_arg", []):
                    if re.fullmatch(pattern, token):
                        failures.append("%s: %r carries %r"
                                        % (rel, arg[:120], token[:120]))
            for pattern in cfg.get("forbid", []):
                if re.search(pattern, body):
                    failures.append("%s: %r contains %r"
                                    % (rel, arg[:120], pattern))
            opened.append(body)
        if not output:
            failures.append("%s: the database entry names no object file" % rel)
        else:
            full = output if os.path.isabs(output) else os.path.join(
                entry.get("directory") or ctx["build"], output)
            if not os.path.exists(full):
                failures.append("%s: planned into %s and no such object file "
                                "was built" % (rel, output))
        for pattern in cfg.get("require", []):
            if not re.search(pattern, command):
                failures.append("%s: command lacks %r" % (rel, pattern))
        for pattern in cfg.get("forbid", []):
            if re.search(pattern, command):
                failures.append("%s: command contains %r" % (rel, pattern))
        # `require_expanded` reads the command together with every response
        # file this gate opened. A modules build states the thing that makes
        # it a modules build -- `-x c++-module` for an interface unit, a
        # `-fmodule-file=` per import for a consumer -- only in the module
        # map, so a requirement about it has to be asked of the expanded
        # text. Asked of `command` alone it would answer "no" on a perfectly
        # good modules build, and asked of the raw file contents alone it
        # would miss a tree that passes the same flag directly.
        if cfg.get("require_expanded"):
            expanded = command + "\n" + "\n".join(opened)
            for pattern in cfg["require_expanded"]:
                if not re.search(pattern, expanded):
                    failures.append("%s: neither the command nor its module "
                                    "map has %r" % (rel, pattern))
    return not failures, failures[:MAX_REPORT]


# ---------------------------------------------------------------------------
# AST-level gates


def _run(argv, cwd=None, env=None):
    try:
        proc = subprocess.run(argv, cwd=cwd, capture_output=True, text=True,
                              timeout=TOOL_TIMEOUT, env=env)
    except subprocess.TimeoutExpired:
        return 124, "", "timed out after %.0fs" % TOOL_TIMEOUT
    return proc.returncode, proc.stdout, proc.stderr


def _parallel(items, fn, jobs):
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        return list(pool.map(fn, items))


TIDY_DIAG = re.compile(r"^(?P<file>[^\s:]+):(?P<line>\d+):(?P<col>\d+): "
                       r"(warning|error): (?P<msg>.*)$", re.M)


def gate_tidy(cfg, ctx):
    """clang-tidy at zero over a pinned check list.

    The list is pinned in the step's config, never `*`: an open-ended check
    set turns a compiler upgrade into a task failure. --warnings-as-errors is
    not used for the exit code -- the diagnostics are counted directly, so a
    check that only ever warns still fails the gate.
    """
    if ctx.get("db") is None:
        return False, [_no_database(ctx)]
    entries = db_entries(ctx, cfg)
    if not entries:
        return False, ["no translation units matched %r" % cfg.get("only")]
    checks = "-*," + ",".join(cfg["checks"])
    extra = cfg.get("extra_args", [])
    # At the candidate's uid, never root: -p names /tmp/isecompdb, whose
    # entries carry the candidate's own @<name>.modmap response files, and
    # clang expands an @file and honours a -fplugin= inside it. The database
    # is root's and read-only to this uid; the AST is the same AST.
    prefix = (os.environ.get("ISEFIX_LAUNCH_PREFIX") or "").split()

    def one(item):
        rel, entry = item
        argv = ["clang-tidy", "-p", ctx.get("db_dir") or ctx["build"],
                "--quiet",
                "--checks=" + checks, "--header-filter=" + cfg.get(
                    "header_filter", r"^" + re.escape(ctx["src"]) + r"/ise/")]
        for arg in extra:
            argv.append("--extra-arg=" + arg)
        argv.append(entry["file"])
        code, out, err = _run(prefix + argv, cwd=ctx["build"])
        found = []
        for match in TIDY_DIAG.finditer(out):
            name = os.path.relpath(match.group("file"), ctx["src"]) \
                if match.group("file").startswith(ctx["src"]) \
                else match.group("file")
            found.append("%s:%s: %s" % (name, match.group("line"),
                                        match.group("msg")[:140]))
        if code == 124:
            found.append("%s: clang-tidy %s" % (rel, err))
        return found

    failures = []
    for found in _parallel(entries, one, ctx["jobs"]):
        failures.extend(found)
    _reap_candidate()
    # The same header is analysed once per including TU.
    failures = sorted(set(failures))
    return not failures, failures[:MAX_REPORT]


QUERY_COUNT = re.compile(r"^(\d+) match(?:es)?\.$", re.M)
# `set output diag` prints one `note:` per binding per match. With
# `set bind-root true` and a matcher that binds nothing itself, that is exactly
# one line per match, carrying the file, line and column the match is at.
QUERY_SITE = re.compile(r'^(.+?):(\d+):(\d+): note: "[^"]*" binds here$')
# Everything clang-query says when it could not do what it was asked. It says
# it on stdout, not stderr, and it stops reading the rest of the script.
QUERY_BROKEN = re.compile(r"Matcher not found|Error parsing|Error building "
                          r"matcher|Invalid token|not a valid top-level")


def astq_matchers(cfg, ctx):
    """The gate's matchers, normalised.

    A matcher is `{"name": ..., "query": ..., "max": n}` or `{"min": n}` or
    both. A bare string is `{"query": s, "max": 0}`, which is what a gate that
    only forbids wants to write.

    `min` is what makes this gate answerable. A clang-query matcher that does
    not compile prints its complaint and is counted as nothing, and a gate
    that reads "nothing" as "clean" reports the tree is migrated because its
    own instrument broke -- the shape that put a `cxxDeleteExpr(isArrayForm())`
    of 0 into these notes before anything was built on it. Every gate here
    therefore carries at least one matcher with a `min`, asking for something
    the tree is known to contain, and fails as broken rather than as clean
    when the answer comes back empty.

    `@@SRC@@` expands to the tree being scored. A file filter with `/app`
    written into it matches nothing when this module is run by hand against a
    checkout somewhere else, which is the same silent zero in another costume.

    `role` says which kind of `min` it is. A `control` -- the default, and
    every matcher that existed before step 3 -- asks for something the tree is
    known to contain and is short only when the instrument broke. A `require`
    is the requirement itself: the step asks for sixty of something and the
    candidate wrote forty. The two want different sentences, and the
    difference matters more than it sounds, because a candidate told that a
    gate is broken goes to read the verifier.
    """
    out = []
    for index, item in enumerate(cfg["matchers"]):
        if isinstance(item, str):
            item = {"query": item}
        entry = {"name": item.get("name", "matcher #%d" % index),
                 "query": item["query"].replace("@@SRC@@", ctx["src"]),
                 "min": int(item.get("min", 0)),
                 "role": item.get("role", "control"),
                 "max": (int(item["max"]) if "max" in item
                         else (None if "min" in item else 0))}
        out.append(entry)
    return out


def _astq_blocks(out):
    """clang-query's stdout, as one (count, sites) block per `match` command.

    `set bind-root true` plus `set output diag` prints one `note: "..." binds
    here` per match and then `N matches.`, in the order the commands were
    given -- so the blocks line up with the matchers positionally, and that
    is the only thing that makes slicing one run's output back out to several
    gates sound.
    """
    blocks, pending = [], []
    for line in out.splitlines():
        site = QUERY_SITE.match(line)
        if site is not None:
            pending.append((site.group(1), int(site.group(2)),
                            int(site.group(3))))
            continue
        count = QUERY_COUNT.match(line)
        if count is not None:
            blocks.append((int(count.group(1)), pending))
            pending = []
    return blocks


def astq_prescan(config, ctx):
    """One clang-query per translation unit for every astq gate at once.

    WHY THIS EXISTS. `clang-query -p db -c "match ..." tu.cpp` parses the
    whole translation unit and then runs the matchers, and parsing is
    essentially all of the cost. Asking each gate separately therefore parses
    every translation unit once per gate: step 6 has 23 astq gates over 41
    units, which is 943 parses to answer questions that 41 parses can answer.
    Measured on this box, that is the difference between a grading phase of
    ~22 minutes and one of ~2. It is also why step 6's first calibration run
    was killed by its own 1500 s grade budget with the gates half-done --
    rule 10 asks for scoring to fit in 30 minutes on 7 vCPU and names
    sharding, not a bigger budget, as the way to get there.

    WHAT IT DOES NOT CHANGE. Nothing about any gate's verdict. The matchers
    are the same matchers, in the same order, run against the same units with
    the same --extra-arg; `_astq_blocks` returns one block per `match`
    command positionally, so each gate's slice is byte-for-byte the answer it
    would have got on its own. gate_astq revalidates every slice it takes
    from here, and any unit this function could not answer for is simply
    absent from the cache -- the gate then runs its own clang-query for that
    unit and reports whatever went wrong in its own words. That fallback is
    the point: one matcher that this toolchain does not register makes
    clang-query abandon the rest of the script, and in a batch that would
    otherwise turn one broken gate into twenty broken ones.

    Gates are grouped by the things that change the command line or the unit
    list -- extra_args, `only`, `skip` -- so that a gate asking a narrower
    question still gets exactly the units it asked for.
    """
    if ctx.get("db") is None:
        return
    gates = [cfg for cfg in config["gates"] if cfg.get("kind") == "astq"]
    if len(gates) < 2:
        return
    groups = {}
    for cfg in gates:
        key = (tuple(cfg.get("extra_args", [])), cfg.get("only"),
               cfg.get("skip"))
        groups.setdefault(key, []).append(cfg)

    cache = ctx.setdefault("astq_cache", {})
    prefix = (os.environ.get("ISEFIX_LAUNCH_PREFIX") or "").split()
    for (extra, _only, _skip), members in groups.items():
        layout, batch = [], []
        for cfg in members:
            try:
                got = astq_matchers(cfg, ctx)
            except Exception:                             # noqa: BLE001
                # A gate whose matchers do not even normalise is the gate's
                # own problem to report; leave it out and let it run alone.
                continue
            layout.append((cfg["id"], len(got)))
            batch.extend(got)
        if len(layout) < 2 or not batch:
            continue
        entries = db_entries(ctx, members[0])
        if not entries:
            continue

        argv_head = ["clang-query", "-p", ctx.get("db_dir") or ctx["build"]]
        for arg in extra:
            argv_head.append("--extra-arg=" + arg)
        argv_head += ["-c", "set bind-root true", "-c", "set output diag"]
        for matcher in batch:
            argv_head += ["-c", "match " + matcher["query"]]

        def one(item, _argv=argv_head, _n=len(batch), _p=prefix):
            rel, entry = item
            code, out, _err = _run(_p + _argv + [entry["file"]],
                                   cwd=ctx["build"])
            if code != 0:
                return rel, None
            blocks = _astq_blocks(out)
            return rel, (blocks if len(blocks) == _n else None)

        for rel, blocks in _parallel(entries, one, ctx["jobs"]):
            if blocks is None:
                continue
            at = 0
            for gate_id, count in layout:
                cache[(gate_id, rel)] = blocks[at:at + count]
                at += count
    _reap_candidate()


def gate_astq(cfg, ctx):
    """clang-query matchers, counted over the whole compile database.

    Used for the constructs that have no clang-tidy check and cannot be seen
    in text: an owning `new` whose result is stored in a raw pointer, a
    `delete` anywhere, a call to the C allocator that a `new` was moved into.
    The task line asks for exactly this -- "checked by clang-query over the
    compile database rather than grep" -- and the difference is not pedantry:
    a grep of this tree for the C allocator answers twenty-one and the matcher
    answers sixteen, because five of the twenty-one are a member function that
    shares the name and two lines inside an `#if` this toolchain does not take.

    Counting is per site, not per match. A header is parsed once per
    translation unit that includes it and a template body once per
    instantiation, so the same `new` can be reported forty times; the gate
    deduplicates on (file, line, column) so that its bounds are counts of
    things in the tree rather than counts of times the compiler looked at
    them.
    """
    # A role this module does not know is a config that means something other
    # than what it says, and the failure mode is quiet: an unrecognised role
    # falling back to `control` would report a candidate's unfinished work as a
    # broken instrument, which is the exact confusion `role` exists to end.
    # Answered before the database, because a role spelled wrong is wrong on a
    # tree that never built, and the author who wrote it is the only person who
    # can fix it -- there is no reason to make them find a building tree first.
    matchers = astq_matchers(cfg, ctx)
    unknown = ["VERIFIER-PROBLEM: %s declares role %r, which is neither "
               "'control' nor 'require'" % (matcher["name"], matcher["role"])
               for matcher in matchers
               if matcher["role"] not in ("control", "require")]
    if unknown:
        return False, unknown
    if ctx.get("db") is None:
        return False, [_no_database(ctx)]
    entries = db_entries(ctx, cfg)
    if not entries:
        return False, ["no translation units matched %r" % cfg.get("only")]

    cache = ctx.get("astq_cache") or {}

    def one(item):
        rel, entry = item
        # The prescan has usually already asked this translation unit every
        # astq gate's matchers in one parse. A hit here is not an
        # optimisation with a different meaning: the blocks are the same
        # blocks, sliced out of the same clang-query run, and they are
        # validated below by the same code that validates a fresh answer.
        # `cfg.get`, not `cfg["id"]`: gate_astq is also called by
        # authoring/probe/verify_step_astq.py with a synthetic config
        # that has matchers and no id. A missing id is a cache miss,
        # which is exactly right -- the prescan only ever files real
        # gate ids, so such a call runs its own query as it always did.
        blocks = cache.get((cfg.get("id"), rel))
        if blocks is None:
            prefix = (os.environ.get("ISEFIX_LAUNCH_PREFIX") or "").split()
            argv = ["clang-query", "-p", ctx.get("db_dir") or ctx["build"]]
            for arg in cfg.get("extra_args", []):
                argv.append("--extra-arg=" + arg)
            argv += ["-c", "set bind-root true", "-c", "set output diag"]
            for matcher in matchers:
                argv += ["-c", "match " + matcher["query"]]
            argv.append(entry["file"])
            code, out, err = _run(prefix + argv, cwd=ctx["build"])
            if code == 124:
                return None, ["%s: clang-query %s" % (rel, err)]
            blocks = _astq_blocks(out)
            # clang-query answers a matcher it could not build by printing the
            # complaint and abandoning the rest of the script, so a short
            # answer and a non-zero status are the same event seen twice.
            # Either one fails the gate as broken; neither is allowed to read
            # as zero.
            if code != 0 or len(blocks) != len(matchers):
                said = [l for l in (out + "\n" + (err or "")).splitlines()
                        if QUERY_BROKEN.search(l)]
                return None, ["%s: clang-query answered %d of %d matchers "
                              "(rc=%d)" % (rel, len(blocks), len(matchers),
                                           code)] + said[:3]
        for matcher, (count, sites) in zip(matchers, blocks):
            if count != len(sites):
                return None, ["%s: %s reported %d match(es) but %d "
                              "location(s); a matcher here must bind nothing "
                              "but the root" % (rel, matcher["name"], count,
                                                len(sites))]
        return blocks, []

    found = [set() for _ in matchers]
    broken = []
    for blocks, problem in _parallel(entries, one, ctx["jobs"]):
        if blocks is None:
            broken.extend(problem)
            continue
        for index, (_count, sites) in enumerate(blocks):
            found[index].update(sites)
    if broken:
        return False, ["the query did not run everywhere it was asked to; "
                       "this gate is broken, not passed"] + broken[:MAX_REPORT]

    # Every matcher's verdict first, then the locations. A gate with four
    # matchers and sixty-five sites under the first of them would otherwise
    # spend its whole report on that one list and never say what the other
    # three answered.
    verdicts, listings = [], []
    for matcher, sites in zip(matchers, found):
        named = sorted((os.path.relpath(f, ctx["src"])
                        if f.startswith(ctx["src"]) else f, line, col)
                       for f, line, col in sites)
        if matcher["min"] and len(sites) < matcher["min"]:
            if matcher["role"] == "require":
                verdicts.append("%s found %d site(s), fewer than the %d this "
                                "step asks for"
                                % (matcher["name"], len(sites),
                                   matcher["min"]))
            else:
                verdicts.append(
                    "%s found %d site(s), fewer than the %d it is here to "
                    "prove the query can still see; this gate is broken, not "
                    "passed"
                    % (matcher["name"], len(sites), matcher["min"]))
        if matcher["max"] is not None and len(sites) > matcher["max"]:
            verdicts.append("%s: %d site(s), at most %d allowed"
                            % (matcher["name"], len(sites), matcher["max"]))
            listings.append("%s:" % matcher["name"])
            listings.extend("  %s:%d:%d" % site for site in named[:MAX_REPORT])
    if not verdicts:
        return True, []
    return False, verdicts + listings[:max(0, MAX_REPORT - len(verdicts))]


# ---------------------------------------------------------------------------
# binary-level gates


def binaries(ctx, cfg):
    roots = cfg.get("binaries")
    if roots:
        return [os.path.join(ctx["build"], p) for p in roots]
    out = []
    for root, dirs, names in os.walk(ctx["build"]):
        dirs[:] = [d for d in dirs if d != "CMakeFiles"]
        for name in names:
            path = os.path.join(root, name)
            if name.endswith((".a", ".so")) or (
                    os.access(path, os.X_OK) and os.path.isfile(path)
                    and not os.path.splitext(name)[1]):
                out.append(path)
    return sorted(out)


def symbols(path):
    """Every symbol in an artefact, defined and undefined, demangled.

    Undefined ones matter as much as defined: a dependency reached through a
    shared library shows up only as an undefined reference.
    """
    code, out, _err = _run(["llvm-nm", "--demangle", "--no-sort", path])
    if code != 0:
        _code, out, _err = _run(["llvm-nm", "--demangle", path])
    return out.splitlines()


def gate_symbols(cfg, ctx):
    """Symbols that must not (or must) appear in the built artefacts.

    The link is the last place a dependency can hide: a `boost::bind` reached
    through a typedef, a macro or a template alias still leaves its mangled
    name in the object file.

    Three lists, and the third is the reason this reads longer than it looks.
    `forbid` is the requirement. `require` is a second requirement, for a
    construct the step asks the candidate to adopt. `control` is neither: it
    names something *the tree already contains and will still contain after
    the step*, so that a run finding nothing has to distinguish "the tree is
    clean" from "nm printed nothing and every pattern therefore matched
    nothing". Without it this gate passes an empty build directory, an
    archive llvm-nm could not read, and a `binaries` list whose paths moved --
    all of which happened here while step 1 was being built, and all of which
    look exactly like success from inside the loop.

    `min_distinct` is a fourth list and a different kind of question. `require`
    asks whether a construct appears at all, and one line in one artefact
    answers it -- which makes "the step adopted this vocabulary" and "the tree
    contains one instantiation of it" the same measurement. Each entry here
    names a pattern with exactly one capture group, and counts the *distinct*
    values that group takes across every artefact: how many different types
    are held this way, not how many symbols mention it. A tree that mints one
    handle to satisfy a presence check and owns everything else some other way
    fails this and passes that.
    """
    found = binaries(ctx, cfg)
    if not found:
        return False, ["no binaries under %s" % ctx["build"]]
    forbid = [re.compile(p) for p in cfg.get("forbid", [])]
    require = [re.compile(p) for p in cfg.get("require", [])]
    control = [re.compile(p) for p in cfg.get("control", [])]
    counted = []
    for spec in cfg.get("min_distinct", []):
        pattern = re.compile(spec["pattern"])
        # One group, asserted rather than assumed: a pattern written without
        # it would count nothing and report every tree as empty, and a pattern
        # with two would silently count only the first.
        if pattern.groups != 1:
            return False, ["min_distinct pattern %r has %d capture group(s), "
                           "not 1" % (spec["pattern"], pattern.groups)]
        counted.append((spec, pattern, set()))
    listings = []
    seen = set()
    lines = 0
    for path in found:
        rel = os.path.relpath(path, ctx["build"])
        for line in symbols(path):
            lines += 1
            for pattern in forbid:
                if pattern.search(line):
                    listings.append("%s: %s" % (rel, line.strip()[:160]))
            for pattern in require + control:
                if pattern.search(line):
                    seen.add(pattern.pattern)
            for spec, pattern, names in counted:
                names.update(pattern.findall(line))
    verdicts = []
    if listings:
        verdicts.append("%d forbidden symbol reference(s) in %d artefact(s)"
                        % (len(listings), len(found)))
    for pattern in require:
        if pattern.pattern not in seen:
            verdicts.append("no symbol matching %r in any of the %d artefact(s)"
                            % (pattern.pattern, len(found)))
    for spec, pattern, names in counted:
        if len(names) < spec["min"]:
            verdicts.append(
                "%s: %d, at least %d required%s"
                % (spec.get("what", "distinct matches of %r" % spec["pattern"]),
                   len(names), spec["min"],
                   (" (found: %s)" % ", ".join(sorted(names)[:12])) if names
                   else ""))
    for pattern in control:
        if pattern.pattern not in seen:
            verdicts.append(
                "the control %r matched nothing across %d artefact(s) and %d "
                "symbol(s); this tree is expected to contain it whether or "
                "not the step was done, so nothing here was measured and this "
                "gate is broken, not passed"
                % (pattern.pattern, len(found), lines))
    if not verdicts:
        return True, []
    return False, verdicts + listings[:max(0, MAX_REPORT - len(verdicts))]


# ---------------------------------------------------------------------------
# runtime gates
#
# Everything above reads the tree or the artefacts. This one runs the
# programs, a second time, out of a second build of the same sources made with
# the compiler's own instrumentation switched on, and reads what that
# instrumentation says. It is here rather than in the behaviour family because
# it is not a behaviour: the recorded suite answers "does this tree still do
# what the old one did", and two trees that agree on every byte of every
# transcript can still differ on whether one of them let go of what it
# allocated.
#
# The second build is not made here. It is a phase of test.sh, run before the
# tree is digested and under the same unprivileged user as the graded build,
# because a configure is the project's own code running and run_gates decides
# once, at entry, whether the tree still matches what root digested. A gate
# that configured the candidate's CMake would be candidate code running
# *behind* that decision, which is the window the digest exists to close.

SAN_ERROR = re.compile(r"ERROR: \w*Sanitizer|DEADLYSIGNAL")
SAN_WARN = re.compile(r"(?=.*WARNING)(?=.*Sanitizer)")
# A memory error and a leak arrive as the same shape of line and are not the
# same finding, so the leak report is excluded here and counted below by its
# own summary:
#
#   ==243==ERROR: LeakSanitizer: detected memory leaks
#   SUMMARY: AddressSanitizer: 112 byte(s) leaked in 1 allocation(s).
#
# Reading the first of those as a memory error of kind "detected" was what the
# first draft did, and it turned a tree that leaks into a tree that leaks and
# also has an unnamed error in it.
SAN_KIND = re.compile(r"ERROR: (?!LeakSanitizer)\w*Sanitizer: ([\w-]+)")
SAN_LEAK = re.compile(r"SUMMARY: \w*Sanitizer: (\d+) byte\(s\) leaked in "
                      r"(\d+) allocation")
SAN_LEAK_SAID = re.compile(r"ERROR: LeakSanitizer: detected memory leaks")
# The instrumented runtime is linked into every program it instruments, so its
# own entry point is in the symbol table of anything actually built with it.
SAN_PRESENT = re.compile(r"\b__asan_init\b")
# What build_candidate.sh's sanitize phase puts in CMAKE_CXX_FLAGS_DEBUG. The
# driver has to be built with the same ones: a program that calls into an
# instrumented library from uninstrumented code reports the library's
# allocations against a shadow the caller never updated.
SAN_DRIVER_FLAGS = ("-fsanitize=address -fsanitize-recover=address "
                    "-fno-omit-frame-pointer -g")


def _san_reports(logdir, seen):
    """New sanitizer report files since the last call. {name: text}."""
    out = {}
    try:
        names = sorted(os.listdir(logdir))
    except OSError:
        return out
    for name in names:
        if name in seen:
            continue
        seen.add(name)
        try:
            with open(os.path.join(logdir, name), errors="replace") as handle:
                text = handle.read().strip()
        except OSError:
            continue
        if text:
            out["report:" + name] = text
    return out


def _sanitized_driver(cfg, bindir, library):
    """Compile the characterisation driver against the instrumented library.

    The driver is the only program in the suite that calls the containers this
    step rewrites -- `ObjectList` extraction, `setOwnsObjects`, the order a
    list destroys what it owns -- directly. The fifteen example programs link
    the same library and never reach them. Leaving the driver out of this gate
    therefore leaves the one hole a `release()` opens: a handle that owns an
    object right up to the line that hands the raw pointer to something which
    never frees it is a leak no gate above can see, in code no example runs.

    So it is built a second time, with the instrumentation, against the
    library the instrumented build produced -- not the graded one. The flags
    are build_candidate.sh's `SANFLAGS`; they are repeated rather than
    imported because that file is shell and this one is not, and the check
    below is what keeps the two honest: a driver the instrumentation is not
    actually in is a broken instrument, not a clean tree.

    The compile's own output is appended to `driver_log` when the gate names
    one. On a passing run nothing here reaches the reward -- the gate returns
    an empty list and says nothing -- so a reader of a shipped run has no
    evidence the second compile happened at all, and "the driver was built
    against the instrumented library" is exactly the claim this gate rests on.
    Best-effort: a log that cannot be written is not a finding about the tree,
    so an OSError here is swallowed rather than reported.
    """
    def note(text):
        path = cfg.get("driver_log")
        if not path:
            return
        try:
            with open(path, "a", errors="replace") as handle:
                handle.write(text)
        except OSError:
            pass

    out = "/tmp/isesandrvout"
    shutil.rmtree(out, ignore_errors=True)
    env = dict(os.environ)
    env.update({"CXX": "clang++",
                "STD": cfg.get("driver_std", "gnu++98"),
                "OPT": cfg.get("driver_opt", "O1"),
                "SAN": SAN_DRIVER_FLAGS,
                "LIB": library,
                "OBJ": "/tmp/isesan/drvobj"})
    prefix = (os.environ.get("ISEFIX_LAUNCH_PREFIX") or "").split()
    try:
        proc = subprocess.run(
            prefix + ["sh", "/tests/driver/build.sh", "/app", out],
            env=env, capture_output=True, text=True, timeout=900)
    except subprocess.TimeoutExpired:
        note("=== instrumented driver: no result in 900s\n")
        return ["the instrumented driver did not finish compiling in 900s"]
    note("=== instrumented driver: std=%s opt=%s san=%s lib=%s rc=%d\n%s%s"
         % (env["STD"], env["OPT"], env["SAN"], library, proc.returncode,
            proc.stdout or "", proc.stderr or ""))
    built = os.path.join(out, "isedrv")
    if proc.returncode != 0 or not os.path.isfile(built):
        tail = ((proc.stderr or proc.stdout or "").strip().splitlines() or
                ["no output"])[-6:]
        return ["the characterisation driver did not compile against the "
                "instrumented library (rc=%d), so the sessions that exercise "
                "the containers this step rewrites cannot run"
                % proc.returncode] + ["  | " + line[:160] for line in tail]
    if not any(SAN_PRESENT.search(line) for line in symbols(built)):
        return ["the driver built for this gate carries no instrumentation, "
                "so running it measures nothing and this gate is broken, not "
                "passed"]
    # Copied in by root, as grade.py does: the directory the suite launches
    # from stays one the programs under test cannot write.
    target = os.path.join(bindir, "isedrv")
    shutil.copyfile(built, target)
    os.chmod(target, 0o755)
    note("=== instrumented driver: %d byte(s), instrumentation present, "
         "staged at %s\n" % (os.path.getsize(target), target))
    return []


def gate_sanitize(cfg, ctx):
    """Every program, over the whole suite, under the compiler's own
    allocation checking.

    What the step asks for is a property of the sources -- nothing the
    programs allocate outlives the last pointer to it, and nothing is read or
    written through a pointer that no longer owns anything. Neither is visible
    in the text of the tree or in its symbol table: the tree that leaks and
    the tree that does not are the same tree until something runs it. So this
    runs it, once per recorded session, and reads what the instrumented
    runtime reports.

    Three things could make that report empty without the property holding,
    and each is checked rather than assumed:

      * the second build produced nothing, or produced fewer programs than the
        suite runs. Every artefact is required, by name, before anything runs;
      * the second build produced programs the instrumentation is not actually
        in, because the project pushed its own options in after the harness's
        or configured its own flags variables. The runtime's entry point is
        looked for in each program's symbol table, and its absence is a broken
        instrument, not a clean tree;
      * a session did not run. The count is compared against the count that
        was selected, because a suite that ran none of its sessions reports
        exactly what a clean suite reports.

    The report files are the primary evidence; the transcripts and the
    sessions' own stderr are read too, so that a report the runtime wrote to a
    descriptor instead of to its log directory is not lost.
    """
    from . import catalog, engine                         # noqa: PLC0415

    build = cfg.get("build", "/tmp/isesan/build")
    bindir = cfg.get("bindir", "/tmp/isesanbin")
    logdir = cfg.get("logdir", "/tmp/isesan/log")
    max_bytes = int(cfg.get("max_leaked_bytes", 0))
    skip_kinds = set(cfg.get("skip_kinds", ["driver"]))

    def build_tail():
        path = cfg.get("build_log")
        if not path or not os.path.exists(path):
            return []
        try:
            with open(path, errors="replace") as handle:
                lines = handle.read().splitlines()
        except OSError:
            return []
        return ["  | " + line[:160] for line in lines[-12:]]

    if ctx.get("build_rc"):
        return False, ["the graded build did not succeed, so there is nothing "
                       "to run under instrumentation"]
    if not os.path.isdir(build):
        return False, ["no instrumented build in %s: the verifier configures "
                       "and builds the tree a second time with the "
                       "instrumentation switched on, and that build produced "
                       "no directory" % build] + build_tail()

    shutil.rmtree(bindir, ignore_errors=True)
    chosen, report = stage.stage(build, bindir)
    os.chmod(bindir, 0o755)
    missing = [name for name in stage.WANTED if name not in chosen]
    if missing:
        return False, ["the instrumented build did not produce %d of the %d "
                       "artefact(s) the suite runs: %s"
                       % (len(missing), len(stage.WANTED),
                          ", ".join(missing))] + build_tail()

    uninstrumented = [name for name in sorted(chosen)
                      if name != stage.LIBRARY
                      and not any(SAN_PRESENT.search(line) for line
                                  in symbols(os.path.join(bindir, name)))]
    if uninstrumented:
        return False, ["%d of the %d program(s) in the second build carry no "
                       "instrumentation, so running them measures nothing and "
                       "this gate is broken, not passed: %s"
                       % (len(uninstrumented), len(chosen) - 1,
                          ", ".join(uninstrumented[:8]))] + build_tail()

    selected = [session for session in catalog.sessions()
                if session.get("kind", "server") not in skip_kinds]
    if not selected:
        return False, ["no session survived skip_kinds=%r" % sorted(skip_kinds)]
    if any(session.get("kind") == "driver" for session in selected):
        broken = _sanitized_driver(cfg, bindir,
                                   os.path.join(bindir, stage.LIBRARY))
        if broken:
            return False, broken

    shutil.rmtree(logdir, ignore_errors=True)
    os.makedirs(logdir)
    # The programs run as the unprivileged user; the directory they write
    # their reports into has to be one that user can write.
    os.chmod(logdir, 0o777)
    options = ("log_path=%s/report:halt_on_error=0:detect_leaks=1"
               ":abort_on_error=0:detect_odr_violation=0"
               ":allocator_may_return_null=1" % logdir)
    if cfg.get("asan_options"):
        options += ":" + cfg["asan_options"]

    dropped = []
    before_sink = engine.STDERR_SINK
    before_options = os.environ.get("ASAN_OPTIONS")
    os.environ["ASAN_OPTIONS"] = options
    engine.STDERR_SINK = lambda tag, text: dropped.append((tag, text))
    seen_reports = set(os.listdir(logdir))
    rows, ran = [], 0
    try:
        for session in selected:
            del dropped[:]
            error, transcripts = None, {}
            started = time.time()
            try:
                transcripts = engine.run_session(session, bindir)
                ran += 1
            except engine.SessionError as exc:
                error = str(exc)
            sources = _san_reports(logdir, seen_reports)
            for item, text in sorted((transcripts or {}).items()):
                if SAN_ERROR.search(text) or SAN_WARN.search(text):
                    sources["transcript:" + item] = text
            for index, (tag, text) in enumerate(dropped):
                if SAN_ERROR.search(text) or SAN_WARN.search(text):
                    sources["stderr:%s:%d" % (tag, index)] = text
            joined = "\n".join(sources.values())
            rows.append({
                "session": session["id"], "error": error,
                "seconds": round(time.time() - started, 1),
                "kinds": sorted(set(SAN_KIND.findall(joined))),
                "leaks": sorted(set(SAN_LEAK.findall(joined))),
                # Said it leaks, did not say how much. The byte count is what
                # the bound below is expressed in, so a report this cannot
                # read has to be a finding rather than a zero.
                "unparsed": bool(SAN_LEAK_SAID.search(joined)
                                 and not SAN_LEAK.search(joined)),
                "reported": sorted(sources)})
    finally:
        engine.STDERR_SINK = before_sink
        if before_options is None:
            os.environ.pop("ASAN_OPTIONS", None)
        else:
            os.environ["ASAN_OPTIONS"] = before_options

    verdicts, listings = [], []
    if ran != len(selected):
        verdicts.append(
            "%d of %d session(s) ran; a suite that did not run reports the "
            "same silence as a suite with nothing to report, so this gate is "
            "broken, not passed" % (ran, len(selected)))
    leaked = sum(int(count) for row in rows for count, _n in row["leaks"])
    errored = [row for row in rows if row["kinds"]]
    if errored:
        verdicts.append("%d of %d session(s) reported a memory error: %s"
                        % (len(errored), len(selected),
                           ", ".join(sorted(set(kind for row in errored
                                                 for kind in row["kinds"]
                                                 if kind != "detected")))))
    unparsed = [row["session"] for row in rows if row["unparsed"]]
    if unparsed:
        verdicts.append(
            "%d session(s) reported a leak the verifier could not read a byte "
            "count out of, so the bound below was applied to a number that is "
            "not the leak: %s" % (len(unparsed), ", ".join(unparsed[:6])))
    if leaked > max_bytes:
        verdicts.append("%d byte(s) still allocated and unreachable at exit "
                        "across %d session(s); at most %d allowed"
                        % (leaked, sum(1 for row in rows if row["leaks"]),
                           max_bytes))
    for row in rows:
        if row["error"]:
            listings.append("%s: did not run: %s"
                            % (row["session"], row["error"][:160]))
        elif row["kinds"] or row["leaks"] or row["unparsed"]:
            listings.append("%s: %s%s" % (
                row["session"], ",".join(row["kinds"]) or "-",
                "  " + ";".join("%s bytes in %s allocation(s)" % leak
                                for leak in row["leaks"])
                if row["leaks"] else ""))
    if listings and not verdicts:
        verdicts.append("%d session(s) reported something" % len(listings))
    if not verdicts:
        return True, []
    return False, verdicts + listings[:max(0, MAX_REPORT - len(verdicts))]


# ---------------------------------------------------------------------------
# ABI
#
# abidw refuses a static archive outright -- it asserts and dumps core -- so
# the archive is first collapsed into one relocatable object with `ld -r
# --whole-archive`. That also makes the reading independent of how many
# translation units the project has and what the build system called their
# object files, which matters from step 9 on, where the unit structure changes
# completely.


def abi_xml(library, workdir, name="candidate"):
    """The abixml of a static archive. Returns (path, error-or-None)."""
    os.makedirs(workdir, exist_ok=True)
    merged = os.path.join(workdir, name + ".o")
    code, _out, err = _run(["ld", "-r", "--whole-archive", library,
                            "-o", merged])
    if code != 0:
        return None, "ld -r on %s: %s" % (library, (err or "").strip()[:400])
    xml = os.path.join(workdir, name + ".abi")
    code, _out, err = _run(["abidw", "--no-show-locs", "--out-file", xml,
                            merged])
    if code != 0 or not os.path.exists(xml):
        return None, "abidw on %s: %s" % (merged, (err or "").strip()[:400])
    return xml, None


# abidiff's exit status is a bit set: 1 error, 2 usage error, 4 ABI change,
# 8 incompatible ABI change.
ABIDIFF_TROUBLE = 1 | 2
ABIDIFF_CHANGE = 4 | 8


def _abidiff(reference, candidate, suppressions, extra):
    argv = ["abidiff"] + list(extra)
    if suppressions:
        argv += ["--suppressions", suppressions]
    argv += [reference, candidate]
    code, out, err = _run(argv)
    return code, (out or "") + (err or "")


def gate_abidiff(cfg, ctx):
    """The declared public ABI of the library must match the recorded one.

    The reference is recorded from this step's golden solution and the
    suppression file narrows the comparison to the entities the instruction
    names as frozen; everything else the agent is free to change, and
    `--no-added-syms` lets it add.

    The gate then checks itself. A `[suppress_type]` whose `name_not_regexp`
    is anchored with `^` does not keep the types it names -- it silently
    suppresses every type there is, and abidiff reports a clean tree with
    "4 filtered out" in the summary. Measured on this box, twice, before the
    gate was written. So a shipped fixture that is known to be ABI-broken is
    diffed the same way, and a clean answer for *that* means the comparison
    was switched off rather than that the candidate is sound. That is a
    verifier problem, not a candidate failure, and it is reported as one.
    """
    found = _find_named(ctx["src"], cfg.get("library", "libise.a"))
    if not found:
        return False, ["no %s under %s"
                       % (cfg.get("library", "libise.a"), ctx["src"])]

    workdir = os.path.join(ctx.get("workdir", "/tmp"), "abi")
    candidate, error = abi_xml(found[0], workdir)
    if error:
        return False, [error]

    reference = os.path.join("/tests", cfg["reference"])
    suppressions = (os.path.join("/tests", cfg["suppressions"])
                    if cfg.get("suppressions") else None)
    extra = cfg.get("args", ["--no-added-syms"])
    if not os.path.exists(reference):
        return False, ["VERIFIER-PROBLEM: no ABI reference at %s" % reference]

    code, report = _abidiff(reference, candidate, suppressions, extra)
    if code & ABIDIFF_TROUBLE:
        return False, ["VERIFIER-PROBLEM: abidiff exited %d" % code,
                       report[:MAX_REPORT * 40]]

    # The control, always, whichever way the comparison came out.
    broken = os.path.join("/tests", cfg["broken_reference"])
    if not os.path.exists(broken):
        return False, ["VERIFIER-PROBLEM: no broken-ABI fixture at %s" % broken]
    control, control_report = _abidiff(reference, broken, suppressions, extra)
    if not control & ABIDIFF_CHANGE:
        return False, [
            "VERIFIER-PROBLEM: the known-broken ABI fixture compared clean "
            "(abidiff exited %d), so this comparison is not comparing "
            "anything." % control,
            control_report[:MAX_REPORT * 20]]

    if code & ABIDIFF_CHANGE:
        return False, report.strip().splitlines()[:MAX_REPORT]
    return True, []


def _find_named(root, name):
    out = []
    for base, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d not in {".git", "__pycache__"}]
        if name in names:
            out.append(os.path.join(base, name))
    return sorted(out)


# ---------------------------------------------------------------------------
# instruction count
#
# "Instruction counts within a calibrated band of the C++98 baseline", where
# the baseline is the step 1 golden -- the same sources on the same toolchain,
# which is the only comparison in which a difference means the migration and
# not the compiler.

CALLGRIND_SUMMARY = re.compile(r"^(?:summary|totals):\s+([\d ]+)", re.M)


def gate_callgrind(cfg, ctx):
    """Run the bench workload under callgrind; the count must be in band.

    The workload is the driver's bench mode, which touches no clock, no files,
    no sockets and no threads, so a difference in the count is a difference in
    the code that was migrated.

    A run that died on start-up also produces a small count, which would read
    as a spectacular optimisation. So the floor is checked first, and the
    bench's own output line has to be there.
    """
    exe = os.path.join(cfg.get("bindir", "/tmp/isebin"), "isedrv")
    if not os.path.exists(exe):
        return False, ["the characterisation driver was not built"]

    # Everything this writes has to be writable by whatever user
    # ISEFIX_LAUNCH_PREFIX drops to, which is not the one that created it --
    # and the bindir around `isedrv` must stay read-only to that user, so the
    # bench runs somewhere else. `isedrv --nodaemon` boots the framework, which
    # opens a log file at `getAppSubPath("log")` -- next to the *executable*,
    # not next to the working directory -- so that one directory inside the
    # bindir is opened up as well. Same split as engine._prepare_dirs.
    workdir = os.path.join(ctx.get("workdir", "/tmp"), "callgrind")
    shutil.rmtree(workdir, ignore_errors=True)
    os.makedirs(workdir)
    os.chmod(workdir, 0o777)
    logdir = os.path.join(os.path.dirname(exe), "log")
    shutil.rmtree(logdir, ignore_errors=True)
    os.makedirs(logdir)
    os.chmod(logdir, 0o777)
    out_file = os.path.join(workdir, "callgrind.out")
    env = dict(os.environ)
    env["ISEDRV_BENCH"] = "1"
    argv = ["valgrind", "--tool=callgrind", "--callgrind-out-file=" + out_file,
            exe, "--nodaemon"]
    prefix = os.environ.get("ISEFIX_LAUNCH_PREFIX", "").split()
    try:
        proc = subprocess.run(prefix + argv, cwd=workdir, env=env,
                              capture_output=True, text=True,
                              timeout=cfg.get("timeout", 900))
    except subprocess.TimeoutExpired:
        return False, ["the bench did not finish under callgrind in %ds"
                       % cfg.get("timeout", 900)]

    if "bench passes=" not in proc.stdout:
        return False, ["the bench printed no result line",
                       proc.stdout[-800:], proc.stderr[-800:]]
    if not os.path.exists(out_file):
        return False, ["callgrind wrote no profile", proc.stderr[-800:]]

    with open(out_file) as handle:
        text = handle.read()
    match = CALLGRIND_SUMMARY.search(text)
    if not match:
        return False, ["callgrind's profile has no summary line"]
    count = sum(int(n) for n in match.group(1).split())

    floor = cfg.get("floor", 0)
    if count < floor:
        return False, ["%d instructions is below the floor of %d: the bench "
                       "cannot have run" % (count, floor)]

    reference = cfg["reference"]
    band = cfg.get("band", 0.25)
    low, high = reference * (1.0 - band), reference * (1.0 + band)
    if not low <= count <= high:
        return False, ["%d instructions, outside the %.0f%% band "
                       "[%d, %d] around the recorded %d (%.2fx)"
                       % (count, band * 100, low, high, reference,
                          float(count) / reference)]
    return True, []


# ---------------------------------------------------------------------------
# platform preservation
#
# The instruction requires the Windows implementation to stay whole, and this
# environment never compiles a line of it: no warning, no link error and no
# behaviour item can tell a port that was carried through the migration from
# one that was replaced by a list of the functions it used to call. Counting
# identifiers cannot tell them apart either -- eight of them in eight comments
# is eight identifiers. So the regions themselves are recorded from the tree
# the agent is given, by authoring/record_platform_regions.py, which imports
# the two functions below so that what is recorded and what is checked can
# never be two different ideas.

# Kept for the degenerate case: a region that normalises to fewer characters
# than this is comments and layout with no code in it. Distinctiveness, not
# length, is what decides whether a region is recorded -- see windows_regions.
MIN_REGION = 8

CPP_DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")
WINDOWS_MACRO = re.compile(r"\b(ISE_WINDOWS|ISE_COMPILER_VC)\b")

# Every macro that guards a platform branch in this tree, not just the Windows
# ones. `portable_body` needs all of them: what it is asked to produce is the
# code that compiles the same way everywhere, and a line in the ISE_LINUX
# branch is no more portable than one in the ISE_WINDOWS branch.
PLATFORM_MACRO = re.compile(r"\b(ISE_WINDOWS|ISE_COMPILER_VC|ISE_COMPILER_BCB"
                            r"|ISE_LINUX|ISE_COMPILER_GCC)\b")


def strip_for_preservation(body):
    """Comments gone, every whitespace run one space, stripped.

    What the preservation requirement protects is the code, not its layout,
    so the comparison is made on a form that survives reindentation and loses
    a comment entirely: an implementation replaced by eight comments naming
    the functions it used to call normalises to nothing. A state machine rather
    than a regex because `//` inside a string literal opens no comment, and a
    scan that thinks it does deletes the rest of a line of real code.
    """
    out = []
    index, end = 0, len(body)
    while index < end:
        char = body[index]
        if char in "\"'":
            quote = char
            out.append(char)
            index += 1
            while index < end:
                char = body[index]
                out.append(char)
                index += 1
                if char == "\\" and index < end:
                    out.append(body[index])
                    index += 1
                elif char == quote:
                    break
            continue
        if char == "/" and index + 1 < end:
            if body[index + 1] == "/":
                while index < end and body[index] != "\n":
                    index += 1
                continue
            if body[index + 1] == "*":
                index += 2
                while index + 1 < end and not (body[index] == "*"
                                               and body[index + 1] == "/"):
                    index += 1
                index = min(index + 2, end)
                # A space, not nothing: a comment between two tokens separates
                # them, and deleting it would glue `int/*n*/x` into `intx`.
                out.append(" ")
                continue
        out.append(char)
        index += 1
    return " ".join("".join(out).split())


def portable_body(body):
    """`body` with every platform-guarded construct removed, directives and all.

    What is left is the code that compiles the same way on every platform,
    which is also the code a candidate is free to restructure. Whole
    constructs go, not just the branches whose condition names a platform
    macro: the `#else` of a `#ifdef ISE_WINDOWS` is the Linux implementation,
    and calling it portable would be calling every platform-specific line in
    this tree portable.
    """
    depth, platform, out = 0, [], []
    for line in body.splitlines():
        match = CPP_DIRECTIVE.match(line)
        if match is None:
            if not platform:
                out.append(line)
            continue
        kind, condition = match.group(1), match.group(2)
        if kind in ("if", "ifdef", "ifndef"):
            depth += 1
            if PLATFORM_MACRO.search(condition):
                platform.append(depth)
            elif not platform:
                out.append(line)
            continue
        if kind == "endif":
            if platform and platform[-1] == depth:
                platform.pop()
            elif not platform:
                out.append(line)
            depth = max(0, depth - 1)
            continue
        # `#else` and `#elif` end a branch, not the construct. Once a
        # construct is known to be platform-specific, every branch of it is,
        # so nothing more is emitted from it until the `#endif` -- except that
        # an `#elif ISE_WINDOWS` can be the line that first makes a construct
        # platform-specific.
        if (kind == "elif" and PLATFORM_MACRO.search(condition)
                and not (platform and platform[-1] == depth)):
            platform.append(depth)
        elif not platform:
            out.append(line)
    return "\n".join(out)


def anchored_count(text, normalised, portable, floor=None):
    """How many times `text` has to still occur, or 0 if it proves nothing.

    `normalised` is the whole file, stripped; `portable` is the same file with
    every platform construct removed, stripped. Two ways a text earns no
    check at all: it is shorter than the degenerate floor, meaning it is
    layout and comments with no code in it, or it occurs in `portable`,
    meaning its count is not a fact about the platform port -- a candidate
    refactoring ordinary code would move it, and a platform-preservation gate
    that fires on that is a gate that has to be explained away.

    Everything else is recorded with the number of times it occurs, and that
    number is the point. `int getPriority() const { return
    threadImpl_.getPriority(); }` is written once in this tree's ISE_WINDOWS
    branch and once, character for character, in its ISE_LINUX branch. A check
    that the text is present is satisfied by the Linux copy however the
    Windows one was mangled; a check that it is present twice is not.
    """
    if floor is None:
        floor = MIN_REGION
    if not text or len(text) < floor:
        return 0
    if text in portable:
        return 0
    return normalised.count(text)


def expand(counts):
    """An ordered (text, count) list flattened to one entry per occurrence.

    The fixture is a list of strings and stays one: a region that has to occur
    three times is written down three times, which is a thing a reader of the
    JSON can see without being told the format changed.
    """
    out = []
    for text, count in counts:
        out.extend([text] * count)
    return out


def windows_region_counts(body):
    """Every recordable Windows-only region of `body`, with its occurrence count.

    Ordered by where each region was first found, so a fixture rebuilt from an
    unchanged tree is byte-identical to the one it replaces.
    """
    normalised = strip_for_preservation(body)
    portable = strip_for_preservation(portable_body(body))
    counts, seen = [], {}
    for text in _windows_region_texts(body):
        if text in seen:
            continue
        seen[text] = True
        count = anchored_count(text, normalised, portable)
        if count:
            counts.append((text, count))
    return counts


def windows_regions(body):
    """Every recordable Windows-only region, one entry per occurrence."""
    return expand(windows_region_counts(body))


def _windows_region_texts(body):
    """Every preprocessor region guarded by a Windows-only macro.

    Nesting-aware: returns the normalised text of each region opened by an
    `#if`/`#ifdef`/`#ifndef`/`#elif` whose condition names ISE_WINDOWS or
    ISE_COMPILER_VC, up to the matching `#else`, `#elif` or `#endif` at the
    same depth. Depth is counted rather than assumed because these files nest
    -- a `#ifdef ISE_WINDOWS` around a `#ifdef ISE_COMPILER_VC` around a
    pragma -- and a scan that stopped at the first `#endif` would record a
    region that ends in the middle of itself and never match again.

    Raw: every region, at every length, in the order they close. What is
    worth recording out of them is `windows_region_counts`' decision -- length
    is not the test and never should have been. What made short regions
    useless was that `#endif`, a lone brace or a bare `#include <windows.h>`
    occurs all over a file, so `region not in body` finds one wherever it
    still is and proves nothing about the place it was recorded from. A
    twenty-four character member declaration that occurs nowhere else is as
    anchored as a five-hundred character function body, and on this tree the
    difference is the whole lifetime of `OsEventLoop::iocpObject_` -- the
    declaration, the `new`, the `delete` and both uses, every one of them under
    the old floor of MIN_REGION characters.
    """
    depth = 0
    open_at = {}
    out = []

    def close(level):
        collected = open_at.pop(level, None)
        if collected is None:
            return
        out.append(strip_for_preservation("\n".join(collected)))

    for line in body.splitlines():
        match = CPP_DIRECTIVE.match(line)
        if match is None:
            for collected in open_at.values():
                collected.append(line)
            continue
        kind, condition = match.group(1), match.group(2)
        if kind in ("if", "ifdef", "ifndef"):
            for collected in open_at.values():
                collected.append(line)
            depth += 1
            if WINDOWS_MACRO.search(condition):
                open_at[depth] = []
            continue
        # `#elif`, `#else` and `#endif` all end a region open at this depth,
        # and none of them belongs to the region they end.
        close(depth)
        for collected in open_at.values():
            collected.append(line)
        if kind == "elif" and WINDOWS_MACRO.search(condition):
            open_at[depth] = []
        elif kind == "endif":
            depth = max(0, depth - 1)
    for level in sorted(open_at):
        close(level)
    return out


def text_half(cfg):
    """The keys of a `preserved_regions` config that the text half is about.

    Handed over rather than passing the whole config, because `min_files`
    means two different things in the two halves: how many files the reference
    has to have recorded, and -- to `_scope_wrong` -- how many files the text
    scan has to have read. ise_iocp.cpp is one file. A reference floor read as
    a scan floor fails the golden, which is how this was found.
    """
    return dict((key, cfg[key]) for key in
                ("include", "exclude", "require_files", "pattern", "min",
                 "span", "comment", "strings") if key in cfg)


def divergence(region, body):
    """How much of `region` the candidate's file still has, in characters.

    `region` is not in `body` -- that is the only reason this gets called --
    but a prefix of it very often is, and the character just past the longest
    matching prefix is where the recording and the tree part company. Reported
    from the start instead, a long region names whatever happens to be at its
    top: step 2's blind run was told it had lost
    `class WinTcpConnection : public TcpConnection {...`, which is
    byte-identical in both trees, when what had actually changed was
    `WinTcpEventLoop::getIocpObject()` sixty lines further down the same
    `#ifdef`.

    Binary search is exact here rather than approximate: if a prefix of length
    n is a substring of `body` then so is every shorter prefix, so the
    property is monotone in n and the largest n with it is the answer.
    """
    lo, hi = 0, len(region)
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if region[:mid] in body:
            lo = mid
        else:
            hi = mid - 1
    return lo


def region_lost(rel, region, body, before=45, after=70):
    """The message for a region that is no longer in the candidate's file.

    A region short enough to print whole is printed whole: `delete
    iocpObject_;` is its own best description, and saying that the first seven
    characters of it still match somewhere is true and useless. The pointer
    earns its keep on the regions that cannot be printed, which is what the
    blind run was actually confused by -- a thousand characters of class
    definition quoted from a top that had not changed.
    """
    if len(region) <= before + after:
        return ("%s: a Windows-only region of %d characters is gone: %s"
                % (rel, len(region), region))
    at = divergence(region, body)
    if at == 0:
        return ("%s: a Windows-only region of %d characters is gone, from its "
                "first character: %s..." % (rel, len(region), region[:90]))
    head = "..." if at > before else ""
    tail = "..." if at + after < len(region) else ""
    return ("%s: a Windows-only region of %d characters is gone; the first %d "
            "still match and then it diverges: %s%s%s"
            % (rel, len(region), at, head,
               region[max(0, at - before):at + after], tail))


def region_occurrences(region, body, slack=None):
    """How many times `region` still stands in `body`, allowing added tokens.

    `strip_for_preservation` flattens a region to one space-separated run, so
    "unchanged" was `region in body` -- and that reads an insertion as a
    deletion. Requirement 13 says the Windows arms stay whole and says, in the
    same breath, that where a change to shared code forces a matching change
    in one of those branches the agent should make it: "that is maintaining
    the port". A tree that does exactly that fails the substring test. The
    blind run produced the case before this existed: one `#include <string.h>`
    added to `ise_socket.h`'s `ISE_LINUX` arm and mirrored into the
    `ISE_WINDOWS` arm three lines up, and the gate reported
    `#include <stdio.h> #include <stdlib.h> #include <winsock2.h>` as "gone"
    when every character of it was still there, with two tokens between.

    So the test is: every token of the recorded region, in order, with nothing
    between them but tokens that were not there when it was recorded. A
    deleted token is a miss and an edited one is a miss -- which is what keeps
    the teeth: the same run failed `formatStringV`'s `#ifdef ISE_COMPILER_VC`
    arm, deleted outright, and it still fails it here.

    Insertions are bounded, or "in order, somewhere in the file" would pass
    for "still here": at most `slack` added tokens across the whole region,
    one eighth of its length and never fewer than four. On the include case
    that is four against two actually inserted; on a region that lost its
    last distinctive token, no budget helps, because the token is not in the
    file at all.

    Only ever called when the exact count already came up short, and the
    caller takes the larger of the two, so this can turn a failing region into
    a passing one and never the other way about.
    """
    rtok = region.split(" ")
    btok = body.split(" ")
    if not rtok or not rtok[0]:
        return 0
    if slack is None:
        slack = max(4, len(rtok) // 8)
    count, index, total, length = 0, 0, len(btok), len(rtok)
    while index < total:
        if btok[index] != rtok[0]:
            index += 1
            continue
        want, at, skipped = 1, index + 1, 0
        while want < length and at < total:
            if btok[at] == rtok[want]:
                want += 1
            else:
                skipped += 1
                if skipped > slack:
                    break
            at += 1
        if want == length:
            count += 1
            index = at          # occurrences do not overlap
        else:
            index += 1
    return count


def gate_preserved_regions(cfg, ctx):
    """The recorded platform regions are all still in the tree, unchanged.

    Text-level, because there is no other level available: nothing here
    compiles these regions, so they have no artefact, no symbol and no
    behaviour. What makes it more than a word count is that the comparison is
    against the regions as they were recorded from the tree the agent was
    given, in a form that ignores layout and ignores comments entirely.

    The identifier count this gate replaces still runs, as the text_present
    half. It is the cheaper statement of the same requirement and it is about
    a file the regions do not cover well -- ise_iocp.cpp is Windows from its
    first line to its last, guarded once around the whole body -- so the two
    halves are unioned rather than one chosen.
    """
    missing = _no_build(cfg, ctx) or _no_project(cfg, ctx)
    if missing:
        return False, missing

    reference = os.path.join("/tests", cfg["reference"])
    if not os.path.exists(reference):
        return False, ["VERIFIER-PROBLEM: no preservation reference at %s"
                       % reference]
    with open(reference) as handle:
        recorded = json.load(handle).get("files", {})
    regions = sum(len(v) for v in recorded.values())
    need_files, need_regions = cfg.get("min_files", 1), cfg.get("min_regions", 1)
    if len(recorded) < need_files or regions < need_regions:
        # A reference that recorded nothing is a gate that passes everything,
        # and it would pass it quietly. The floors are what make a recorder
        # that walked the wrong tree a verifier problem rather than a green
        # column.
        return False, ["VERIFIER-PROBLEM: %s records %d file(s) and %d "
                       "region(s), below the floor of %d and %d"
                       % (reference, len(recorded), regions, need_files,
                          need_regions)]

    failures = []
    for rel in sorted(recorded):
        path = os.path.join(ctx["src"], rel)
        if not os.path.exists(path):
            failures.append("%s: the platform implementation was recorded here "
                            "and the file is gone" % rel)
            continue
        body = strip_for_preservation(read(path))
        # The fixture writes a region down once per occurrence, so counting
        # the list is how many the candidate has to have. For the region that
        # occurs once -- all but four of them -- this is `region not in body`
        # with a better message.
        wanted, need = [], {}
        for region in recorded[rel]:
            if region not in need:
                wanted.append(region)
                need[region] = 0
            need[region] += 1
        for region in wanted:
            have = body.count(region)
            if have < need[region]:
                # An insertion is not a deletion. See region_occurrences.
                have = max(have, region_occurrences(region, body))
            if have >= need[region]:
                continue
            if need[region] == 1:
                failures.append(region_lost(rel, region, body))
            else:
                failures.append(
                    "%s: a Windows-only region of %d characters occurs %d "
                    "times in this file and %d of them %s gone: %s%s"
                    % (rel, len(region), need[region], need[region] - have,
                       "is" if need[region] - have == 1 else "are",
                       region[:90], "..." if len(region) > 90 else ""))
    ok, text_failures = gate_text_present(text_half(cfg), ctx)
    if not ok:
        failures.extend(text_failures)
    return not failures, failures[:MAX_REPORT]


# ---------------------------------------------------------------------------

KINDS = {
    "text_absent": gate_text_absent,
    "text_present": gate_text_present,
    "files_exist": gate_files_exist,
    "files_absent": gate_files_absent,
    "artifacts": gate_artifacts,
    "compile_db": gate_compile_db,
    "tidy": gate_tidy,
    "astq": gate_astq,
    "symbols": gate_symbols,
    "abidiff": gate_abidiff,
    "callgrind": gate_callgrind,
    "preserved_regions": gate_preserved_regions,
    "sanitize": gate_sanitize,
}


def run_gates(config, src, build, jobs=7, workdir="/tmp/isework",
              build_rc=None):
    entries, source, problem = derive_db(build)
    ctx = {"src": os.path.abspath(src), "build": os.path.abspath(build),
           "jobs": jobs, "db": entries, "db_source": source,
           "db_problem": problem, "workdir": workdir, "build_rc": build_rc,
           # Where an analyser is pointed when it wants a compile database:
           # the re-derived copy when there is one, and the build directory
           # only when there is nothing to point at anyway.
           "db_dir": (COMPDB_DIR if entries is not None
                      else os.path.abspath(build))}

    # The trace is the configure that produced this build, read back from
    # where root put it. Once, here, before the first gate, and only when some
    # gate asks for it; every gate that has a `trace` block reads the same
    # events out of ctx.
    if any("trace" in cfg for cfg in config["gates"]):
        ctx["trace"], ctx["trace_problem"] = cmake_trace(ctx["build"])
    else:
        ctx["trace"], ctx["trace_problem"] = None, None

    # A tree that rewrites itself between the compiler and the gates shows the
    # scorer one set of sources and the compiler another, and everything
    # between the two is candidate code running as the user that owns /app:
    # the project's own CMake during the configure, its custom commands during
    # the build. Root digests the tree once, after the verifier's sweep and
    # before the configure, and the comparison is here. Any difference fails
    # everything -- the same answer, and for the same reason, as a
    # suppression.
    #
    # This used to bracket a configure run from inside this function, which
    # covered a narrower window than the one the attack lives in and does not
    # exist any more. A verifier that took a trace but no snapshot is a
    # verifier missing half of what it was told to take, and is refused rather
    # than scored.
    #
    # Nothing of the candidate's may be running while any of this is read:
    # the behaviour suite has just run its programs as that user. Reaped here
    # and again after every gate, because several gates run candidate code.
    _reap_candidate()
    rewritten = _tree_rewritten(ctx["src"])

    # The suppression precondition reads every place a suppression can take
    # effect: the authored tree (`walk`), the authored files under the names
    # `walk` prunes as build output (read before the configure), the
    # database's own units and forced headers wherever they are, and every
    # file the compiler opens for them. Each of the last three closes a route
    # the first one alone left open (reward-hacking audit 2026-10-12, F1).
    suppressed, scanned = _scan(ctx["src"], {"pattern": SUPPRESSIONS,
                                             "include": SUPPRESSION_FILES,
                                             "suppressions": True})
    suppressed += _snapshot_suppressions()
    suppressed += _scan_outside_tree(ctx["src"], entries, scanned)
    suppressed += _scan_dependencies(ctx, scanned)
    suppressed += _system_include_options(ctx)
    suppressed += _cmake_system_markers(ctx)
    suppressed = list(dict.fromkeys(suppressed))
    # One parse per translation unit for all of the astq gates, before any of
    # them runs. Skipped when nothing is going to be scored anyway, so that a
    # suppressed or rewritten tree still fails in milliseconds.
    if not suppressed and not rewritten:
        astq_prescan(config, ctx)
    results = []
    for cfg in config["gates"]:
        if suppressed:
            results.append({"name": cfg["id"], "step": cfg["step"], "ok": False,
                            "desc": cfg.get("desc", ""),
                            "detail": ["%d suppression(s) in the repository; "
                                       "every gate fails" % len(suppressed)]
                                      + suppressed[:MAX_REPORT]})
            continue
        if rewritten:
            results.append({"name": cfg["id"], "step": cfg["step"], "ok": False,
                            "desc": cfg.get("desc", ""),
                            "detail": ["the tree is not the tree that was "
                                       "built; every gate fails"]
                                      + rewritten[:MAX_REPORT]})
            continue
        fn = KINDS.get(cfg["kind"])
        if fn is None:
            results.append({"name": cfg["id"], "step": cfg["step"], "ok": False,
                            "desc": cfg.get("desc", ""),
                            "detail": ["unknown gate kind %r" % cfg["kind"]]})
            continue
        try:
            ok, failures = fn(cfg, ctx)
        except Exception as exc:                          # noqa: BLE001
            ok, failures = False, ["gate raised %r" % (exc,)]
        _reap_candidate()
        results.append({"name": cfg["id"], "step": cfg["step"], "ok": bool(ok),
                        "desc": cfg.get("desc", ""),
                        "detail": [] if ok else failures})
    return results
