"""Build a copy of a tree so the gates have compiler output to read.

The gates need two things the behaviour corpus does not: a build that kept its
preprocessed intermediates, and a record of how that build went. Both come from
a *copy* of the tree, so nothing a gate does can change what the behaviour
scenarios are scoring, and so the candidate's own `src/` is left exactly as the
candidate left it.

The build is the project's own `make` with one flag added. `-save-temps=obj`
puts each `.i` next to its object file, which is what makes the intermediates
usable: without `=obj` every unit writes into the working directory and units
with the same base name in different directories overwrite each other.

Run directly to produce a build a scenario can then be pointed at:

    python3 build.py --src <tree> --dest <dir> [--cflags '...'] [--plain]
"""

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time

# This module imports nothing of the harness, but it is also run directly, and
# an entry point must not leave bytecode behind in `tests/`. See runner.py.
sys.dont_write_bytecode = True

BINARIES = ("redis-server", "redis-cli", "redis-sentinel", "redis-benchmark",
            "redis-check-rdb", "redis-check-aof")
REPORT = "build-report.json"
SAVE_TEMPS = "-save-temps=obj"

# The uid every candidate process runs as, the same one `srv.py` demotes the
# servers to. The build is candidate code too -- a Makefile recipe is a shell
# command the candidate wrote -- so when the verifier runs as root, the copy is
# handed to this uid and both `make` invocations run as it. Run as root, a
# recipe could start a detached process that outlives the build and rewrites
# /logs/verifier/reward.json after test.sh has written it.
SCORE_UID = 65534
SCORE_GID = 65534


def _demote():
    os.setgroups([])
    os.setgid(SCORE_GID)
    os.setuid(SCORE_UID)


def _score_pids():
    pids = []
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            with open("/proc/%s/status" % name) as f:
                fields = dict(line.split(":", 1) for line in f if ":" in line)
            # A zombie runs nothing and cannot be killed again.
            if fields.get("State", "").split()[:1] == ["Z"]:
                continue
            if int(fields["Uid"].split()[0]) == SCORE_UID:
                pids.append(int(name))
        except (OSError, ValueError, IndexError, KeyError):
            continue
    return pids


def _reap():
    """Kill whatever the score uid still has running, the way test.sh does.

    `kill(-1)` sent *as* that uid reaches every process it may signal and
    nothing of root's, and a process that keeps forking is swept in the pass
    that finds it. Done here, before this module reads the tree back or writes
    its report into it, so nothing a recipe left behind can rewrite either
    after it has been read or written.
    """
    for _ in range(10):
        if not _score_pids():
            return
        pid = os.fork()
        if pid == 0:
            try:
                _demote()
                os.kill(-1, signal.SIGKILL)
            finally:
                os._exit(0)
        os.waitpid(pid, 0)
        time.sleep(0.2)


def _take_back(dest):
    """Root owns the copy's top directory from here on, so nothing can create,
    swap or link an entry beside the report this module writes. The rest of
    the tree is taken back by freeze.py."""
    os.lchown(dest, 0, 0)
    os.chmod(dest, 0o755)


def _write_new(path, text):
    """Write `path` as a new regular file: whatever stood there is removed
    first, and the open neither follows a link nor reuses an existing file."""
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path)
    elif os.path.lexists(path):
        os.unlink(path)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                 0o644)
    with os.fdopen(fd, "w") as f:
        f.write(text)


def _hand_over(dest):
    """Give the copy to the score uid; return the preexec that demotes to it.

    Nothing is changed, and None is returned, when this is not running as root:
    a run as an ordinary user builds as that user.
    """
    if os.geteuid() != 0:
        return None
    os.lchown(dest, SCORE_UID, SCORE_GID)
    for dirpath, dirnames, filenames in os.walk(dest):
        for name in dirnames + filenames:
            os.lchown(os.path.join(dirpath, name), SCORE_UID, SCORE_GID)
    return _demote

# gcc and clang both report as `<file>:<line>:<col>: <severity>: <text> [-Wtag]`.
# `fatal error` is the one that matters most and the one that reads differently:
# it is what a missing header produces, and the build stops there, so a pattern
# that only knows `error` reports a tree that would not compile as having no
# errors at all.
DIAG = re.compile(r"^(?P<file>[^\s:][^:]*):(?P<line>\d+):(?:\d+:)?\s+"
                  r"(?P<sev>fatal error|error|warning):\s+(?P<text>.*?)"
                  r"(?:\s+\[(?P<tag>-W[A-Za-z0-9=-]+)\])?$")


def _diags(root, output):
    """Every compiler diagnostic in `output`, with paths made tree-relative.

    A diagnostic whose file is outside the tree belongs to the toolchain and is
    dropped; one inside it is kept with the path the tree knows it by, so a
    finding reads the same whatever scratch directory the build happened in.
    """
    root = os.path.normpath(os.path.abspath(root))
    out = []
    for line in output.splitlines():
        m = DIAG.match(line.strip())
        if not m:
            continue
        path = m.group("file")
        if not os.path.isabs(path):
            # make prints `Entering directory` and then relative paths; the
            # basename is enough to find the file back in the tree.
            hits = [p for p in _index(root).get(os.path.basename(path), [])]
            path = hits[0] if len(hits) == 1 else os.path.basename(path)
        else:
            path = os.path.relpath(os.path.normpath(path), root)
            if path.startswith(os.pardir):
                continue
        out.append({"file": path, "line": int(m.group("line")),
                    "severity": ("error" if m.group("sev").endswith("error")
                                 else m.group("sev")),
                    "text": m.group("text"), "tag": m.group("tag")})
    return out


_INDEX = {}


def _index(root):
    if root in _INDEX:
        return _INDEX[root]
    idx = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        for name in filenames:
            if name.endswith((".c", ".h")):
                rel = os.path.relpath(os.path.join(dirpath, name), root)
                idx.setdefault(name, []).append(rel)
    for v in idx.values():
        v.sort()
    _INDEX[root] = idx
    return idx


def savetemps_dirs(dest):
    """Directories that got a `.i` file out of this build, tree-relative.

    The build passes `-save-temps=obj` through `CFLAGS` on the `make` command
    line, so a directory that compiled with the flag has intermediates in it and
    one that dropped the flag on the way down does not. That is the only
    instrument the gates have for "a flag passed on the command line still
    reaches every compilation, in src/ and in deps/": the preprocessed text
    itself is read elsewhere, but only where it exists.

    Directories rather than files: which units a *failed* build got through
    before it stopped is a property of `make -j` scheduling, and which
    directories it reached is not.
    """
    dirs = set()
    for dirpath, dirnames, filenames in os.walk(dest):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        if any(f.endswith(".i") for f in filenames):
            rel = os.path.relpath(dirpath, dest)
            dirs.add("" if rel == "." else rel)
    return sorted(dirs)


# What a build leaves behind, and what `make distclean` therefore has to take
# away. The pinned tree ships not one file with any of these extensions and no
# binary in `src/`, so on a tree that cleans properly this list is empty.
RESIDUE_EXT = (".o", ".a", ".so", ".i", ".s", ".d", ".lo", ".gcda", ".gcno")


def residue(dest):
    """Build outputs still present after the tree was asked to clean itself.

    The build works on a copy, and a candidate hands over whatever it last
    built in. `make` on a tree whose objects are newer than its sources has
    nothing to do and says so with rc 0, which would let a tree that no longer
    compiles from clean report a clean build -- so the copy is cleaned first,
    with the project's own `distclean`, and what survives that is recorded.

    Scored rather than merely logged, because the clean is the thing this
    depends on and a candidate can break it: an empty list means the tree
    cleaned up, and the only other way to get one would be for the scan to have
    looked in the wrong place.
    """
    out = []
    for dirpath, dirnames, filenames in os.walk(dest):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        rel = os.path.relpath(dirpath, dest)
        rel = "" if rel == "." else rel
        for name in filenames:
            if os.path.splitext(name)[1] in RESIDUE_EXT:
                out.append(os.path.join(rel, name))
    out += [os.path.join("src", b) for b in BINARIES
            if os.path.exists(os.path.join(dest, "src", b))]
    return sorted(out)


def _is_elf(path):
    try:
        with open(path, "rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


def build(src, dest, cflags=(), jobs=None, timeout=1800, savetemps=True,
          reap=False):
    """Copy `src` to `dest`, build it there, and write the report.

    With `savetemps` false and no `cflags`, the command is `make -jN` and
    nothing else -- no `CFLAGS=` on the command line at all, which is how the
    project's own documentation says to build it. That is a different question
    from the one the `-save-temps=obj` build asks, and neither answer implies
    the other: a tree that hard-codes its own flags builds plainly and drops
    the one the scorer passed, and a tree that only compiles because of
    something the scorer passed does the reverse.
    """
    if os.path.exists(dest):
        shutil.rmtree(dest)
    shutil.copytree(src, dest, symlinks=True)
    demote = _hand_over(dest)
    env = dict(os.environ, HOME="/tmp") if demote else None
    jobs = jobs or str(os.cpu_count() or 1)
    # The copy is cleaned before it is built, with the target the project
    # documents for it. Its own exit status is not interesting -- a tree with
    # nothing to clean and a tree whose `distclean` the candidate broke both
    # end up judged by what is left afterwards, which is what `residue` reads.
    try:
        c = subprocess.run(["make", "distclean"], cwd=dest,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, preexec_fn=demote, env=env)
        clean_out, clean_rc = c.stdout, c.returncode
    except (subprocess.TimeoutExpired, OSError) as e:
        clean_out, clean_rc = str(e).encode(), "failed"
    # `reap` only from the verifier's own build (`main`): a gate that builds
    # for itself runs beside other scenarios' servers, which are the same uid.
    if demote and reap:
        _reap()
    left = residue(dest)
    flags = ([SAVE_TEMPS] if savetemps else []) + list(cflags)
    argv = ["make", "-j%s" % jobs] + (["CFLAGS=%s" % " ".join(flags)]
                                      if flags else [])
    try:
        p = subprocess.run(argv, cwd=dest, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout,
                           preexec_fn=demote, env=env)
        rc, out = p.returncode, p.stdout
    except subprocess.TimeoutExpired as t:
        rc, out = "timeout", (t.stdout or b"")
    except OSError as e:
        rc, out = "no-make", str(e).encode()
    text = out.decode("utf-8", "replace")
    if demote and reap:
        _reap()
        _take_back(dest)

    report = {
        "rc": rc,
        "clean-rc": clean_rc,
        "residue": left,
        "command": " ".join(argv),
        "binaries": sorted(b for b in BINARIES
                           if _is_elf(os.path.join(dest, "src", b))),
        "savetemps-dirs": savetemps_dirs(dest),
        "diagnostics": _diags(dest, text),
    }
    _write_new(os.path.join(dest, REPORT),
               json.dumps(report, indent=1, sort_keys=True))
    _write_new(os.path.join(dest, "build.log"),
               "== make distclean (rc %s)\n" % clean_rc
               + clean_out.decode("utf-8", "replace")
               + "\n== %s\n" % " ".join(argv) + text)
    return report


def load_report(dest):
    with open(os.path.join(dest, REPORT)) as f:
        return json.load(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True)
    ap.add_argument("--dest", required=True)
    ap.add_argument("--cflags", default="")
    ap.add_argument("--jobs")
    ap.add_argument("--plain", action="store_true",
                    help="plain `make`: no CFLAGS on the command line at all")
    a = ap.parse_args()
    r = build(a.src, a.dest, cflags=a.cflags.split() if a.cflags else (),
              jobs=a.jobs, savetemps=not a.plain, reap=True)
    print(json.dumps({k: v for k, v in r.items() if k != "diagnostics"},
                     indent=1, sort_keys=True))
    print("diagnostics: %d (%d errors)"
          % (len(r["diagnostics"]),
             sum(1 for d in r["diagnostics"] if d["severity"] == "error")))
    return 0 if r["rc"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
