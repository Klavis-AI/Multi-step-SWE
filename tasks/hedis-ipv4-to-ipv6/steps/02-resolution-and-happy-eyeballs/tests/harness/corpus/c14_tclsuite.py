"""The project's own Tcl test suite, run against the tree under test.

This is the one scenario written by the project rather than by the corpus. It is
here for two reasons: it is the regression net the project itself trusts, and
several of its units drive the parts of the tree that carry an address --
`SLAVEOF`, `MIGRATE`, `CLIENT LIST` -- through the project's own configuration
assets. A migration that moves the servers to a family the suite's assets do not
name shows up here as a suite that no longer starts.

The suite is run from a copy of `tests/` with `src/` symlinked beside it, so the
tree under test is never written to and the run is not affected by whatever a
previous run left in `tests/tmp`.

The copy is the **pinned** `tests/`, shipped beside the expectations, and not
the one in the tree under test. A suite taken from the tree under test is a
suite the tree under test can write: emptying a test body while leaving its
name and its `[ok]` line is invisible to every observation here, because the
names and the statuses are exactly what is recorded. Running the project's own
suite as the project wrote it removes that.

What the tree's own `tests/` says is scored too -- it is the other half of "do
not weaken, filter, rename or disable the repository's own tests" -- but as
what it *lost*, not as what it changed. A tree whose suite is not byte-identical
to the pin's is not thereby a worse tree: the later steps of this migration ask
for a repository that names no IPv4 address as a place to reach, and the suite
is part of the repository, so the addresses in its assets and its helpers are
supposed to move. What is never supposed to happen is a file disappearing or a
case ceasing to be declared, and those are the two facts recorded here. They
are satisfiable at every step, which a byte comparison stops being at the step
that asks for the edit.
"""

import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import norm  # noqa: E402
import srv  # noqa: E402

# The suite manages its own ports (a test server on 11111 and instances from
# 21111 up), so it takes no block from the corpus' range.
PORTS = 0
FAMILY = "behaviour"

# Units chosen for what they drive rather than for breadth: three that touch an
# address directly (`CLIENT LIST`, `MIGRATE`, `SLAVEOF`), and four cheap ones
# that would catch a tree broken in a way that has nothing to do with addresses.
UNITS = (
    "unit/protocol",
    "unit/auth",
    "unit/introspection",
    "unit/dump",
    "integration/rdb",
    "integration/aof",
    "integration/replication",
    "integration/replication-2",
)

RESULT = re.compile(r"^\[(ok|err|skip|ignore|exception)\]:?\s*(.*)$")
PASSED = "All tests passed without errors!"

# The project's own `tests/` at the pin, kept in `tests/fixtures`.
PIN_TESTS = os.path.join(HERE, os.pardir, os.pardir, "fixtures", "pin-tests")

# The suite's own scratch directories. They ship holding a `.gitignore` and
# nothing else, and a run fills them, so they say nothing about whether the
# tree's copy of the suite is the project's.
SCRATCH = ("tmp", "sentinel/tmp", "cluster/tmp")


def _files(root):
    """{relative path: bytes} for a `tests/` tree, minus its scratch dirs."""
    out = {}
    for dirpath, dirnames, filenames in os.walk(root):
        rel = os.path.relpath(dirpath, root)
        rel = "" if rel == "." else rel
        dirnames[:] = [d for d in dirnames
                       if os.path.join(rel, d).replace(os.sep, "/")
                       not in SCRATCH]
        for name in filenames:
            path = os.path.join(dirpath, name)
            if os.path.islink(path) or not os.path.isfile(path):
                continue
            key = os.path.join(rel, name) if rel else name
            try:
                with open(path, "rb") as f:
                    out[key.replace(os.sep, "/")] = f.read()
            except OSError:
                out[key.replace(os.sep, "/")] = None
    return out


# How a test declares itself, and which part of the declaration is its identity.
# A name that interpolates -- `test "Interactive CLI: $name"`, `test "New master
# [join $addr {:}] role matches"` -- labels a case with something computed at
# run time, and what is computed is not what the case is. Collapsing the
# substitutions keeps "this case is still declared" from quietly meaning "this
# case still spells its address the old way", which is the opposite of what the
# later steps ask for. On the pin the collapse loses nothing: 858 declared names
# go in and 858 distinct canonical names come out.
TEST_DECL = re.compile(r'(?<![A-Za-z0-9_])test\s+(?:\{([^{}]*)\}|"([^"\n]*)")')
SUBSTITUTION = re.compile(r'\[[^\[\]]*\]|\$\{[^{}]*\}|\$[A-Za-z0-9_:()]+')

# A case put beyond reach by a `#` is not a case the tree still declares, and
# commenting one out is the cheap way to stop a test failing. Only a `#` that
# opens a line is taken as a comment -- the subset Tcl and a text scan agree
# on -- and on the pin the strip is a no-op in both directions: the same 858
# names come out with it as without. A block wrapped in `if {0}` is past what
# any text scan sees; it is also past what it would be worth, because the suite
# these observations score is the pin's copy and not the tree's, so a tree that
# disables one of its own cases has disabled nothing that runs here.
LINE_COMMENT = re.compile(r"^[ \t]*#.*$", re.M)


def _test_names(files):
    """The canonical names of every case declared in a {path: bytes} suite."""
    names = set()
    for path, data in files.items():
        if not path.endswith(".tcl") or not data:
            continue
        text = LINE_COMMENT.sub("", data.decode("utf-8", "replace"))
        for braced, quoted in TEST_DECL.findall(text):
            name = SUBSTITUTION.sub("<>", braced or quoted)
            names.add(re.sub(r"\s+", " ", name).strip())
    return names


def files_missing(pin, mine):
    """Files of the project's suite the tree under test no longer ships.

    Only files the pin has: a test added beside them is not a weakening, while
    one deleted or renamed is. A rename shows up as the old name gone.
    """
    if not pin:
        return ["<pin-tests fixture missing>"]
    return sorted(k for k in pin if k not in mine)


def tests_lost(pin, mine):
    """Cases of the project's suite the tree under test no longer declares.

    Across the whole suite rather than file by file, so a case moved from one
    file to another is not a loss, and a case deleted, renamed, or commented
    out is one wherever it used to live.
    """
    if not pin:
        return ["<pin-tests fixture missing>"]
    return sorted(_test_names(pin) - _test_names(mine))


def _tclsh():
    for cand in ("tclsh8.6", "tclsh"):
        p = shutil.which(cand)
        if p:
            return p
    return None


def _prepare(ctx):
    """A writable copy of `tests/` with the tree's `src/` beside it.

    `suite` belongs to the score uid while servers from the candidate tree run
    as that uid, so root neither copies into it nor chowns under it, where a
    link could stand in for any component. The copy is made and handed to the
    score uid inside a root-only staging directory beside the workspace (one
    filesystem), walked by fd with no link followed, and moved into place by
    one rename through an O_NOFOLLOW fd on `suite`.
    """
    work = ctx.workspace.sub("suite")
    stage = tempfile.mkdtemp(prefix=".suite-",
                             dir=os.path.dirname(ctx.workspace.base))
    try:
        staged = os.path.join(stage, "tests")
        shutil.copytree(PIN_TESTS, staged)
        if os.geteuid() == 0:
            for _root, _dirs, files, rfd in os.fwalk(staged):
                os.chown(rfd, srv.SCORE_UID, srv.SCORE_GID)
                for f in files:
                    st = os.stat(f, dir_fd=rfd, follow_symlinks=False)
                    if not stat.S_ISLNK(st.st_mode):
                        os.chown(f, srv.SCORE_UID, srv.SCORE_GID, dir_fd=rfd,
                                 follow_symlinks=False)
        wfd = os.open(work, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            os.rename(staged, "tests", dst_dir_fd=wfd)
            os.symlink(os.path.join(ctx.tree.root, "src"), "src", dir_fd=wfd)
        finally:
            os.close(wfd)
    finally:
        shutil.rmtree(stage, ignore_errors=True)
    return work


def _parse(out):
    """(ordered [(name, status)], exception lines) from a suite transcript."""
    results = []
    seen = {}
    exceptions = []
    for line in norm.normalize_text(out).splitlines():
        m = RESULT.match(line.strip())
        if not m:
            continue
        status, name = m.group(1), m.group(2).strip()
        if status == "exception":
            exceptions.append(name)
            continue
        # The same test name can legitimately run more than once in a unit
        # (once per encoding, for instance); the repeats are numbered so that
        # inserting a test does not renumber the ones after it.
        seen[name] = seen.get(name, 0) + 1
        if seen[name] > 1:
            name = "%s #%d" % (name, seen[name])
        results.append((name, status))
    return results, exceptions


def run(ctx):
    tcl = _tclsh()
    ctx.emit("tcl/interpreter-available", tcl is not None)
    # Scored whether or not the interpreter is there: these are facts about the
    # tree, not about the run, and they are what catches a suite that was made
    # to pass rather than kept passing.
    pin = _files(PIN_TESTS)
    mine = _files(os.path.join(ctx.tree.root, "tests"))
    ctx.emit("tcl/suite-files-missing", files_missing(pin, mine), adv=True)
    ctx.emit("tcl/suite-tests-lost", tests_lost(pin, mine), adv=True)
    # The instrument before the finding: an empty list of losses reads the same
    # whether the tree lost nothing or the fixture failed to load, so the size
    # of the fixture that produced it is scored beside it -- both halves, since
    # a fixture that found its files but could not read them would still have
    # the right number of files.
    ctx.emit("tcl/pin-suite-files", len(pin))
    ctx.emit("tcl/pin-suite-tests", len(_test_names(pin)))
    if not tcl:
        ctx.note("no tclsh on this image; suite not run")
        return
    work = _prepare(ctx)

    for unit in UNITS:
        label = unit.replace("/", "-")
        argv = [tcl, "tests/test_helper.tcl", "--single", unit,
                "--clients", "1"]
        try:
            p = subprocess.run(argv, cwd=work, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=600,
                               preexec_fn=ctx.tree.preexec)
            rc, out = p.returncode, p.stdout
        except subprocess.TimeoutExpired as t:
            rc, out = "timeout", (t.stdout or b"")
        text = out.decode("utf-8", "backslashreplace")
        results, exceptions = _parse(text)

        ctx.emit("tcl/%s/rc" % label, rc)
        ctx.emit("tcl/%s/all-passed" % label, PASSED in text)
        ctx.emit("tcl/%s/test-count" % label, len(results))
        for name, status in results:
            ctx.emit("tcl/%s/t/%s" % (label, name), status)
        # An exception is the suite failing to run rather than a test failing,
        # and its text names the reason -- a refused connection, for one.
        ctx.emit("tcl/%s/exceptions" % label,
                 [norm.normalize_text(e) for e in exceptions], adv=True)
        if rc != 0:
            ctx.note("%s: rc=%s\n%s" % (unit, rc, text[-3000:]))
