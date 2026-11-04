#!/usr/bin/env python3
"""diff_scope.py <start-tree> <candidate-tree>

Correctness gate 5 (diff scope). The candidate is handed the pinned source tree
with every accelerator stage stripped. The rule the candidate-facing documents
state is: nothing in that tree changes, and what the candidate adds lives under
src/INTEL/. This script enforces exactly that.

  * a file of the starting tree that is missing from the candidate's tree, or
    whose bytes differ (a symlink's target, for a symlink), is a violation;
  * an added file is a violation when the project's build could compile it --
    a *.cpp under src/ or lib/ but not under src/INTEL/ (the CMake build globs
    *.cpp in those directories) -- and a non-scored diagnostic otherwise:
    logs, dumps and CMake caches an honest candidate may leave behind cannot
    move a measurement and must not fail one.

The reference is the *starting* tree (/start, built in tests/Dockerfile by the
same strip.py invocation the agent image uses), not /base: at step k, /base
still carries the earlier stages as upstream code, so a diff against it would
blame the candidate for files the ladder itself handed them.

Exit status: 0 clean, 1 at least one violation, 2 the check could not run.
Never exits 0 without having compared the starting tree, so a missing or
unreadable tree can not read as a pass.
"""

import hashlib
import os
import sys

MAX_PRINT = 20
# cmake/CMakeLists.txt globs *.cpp under src/ (the root package-less sources)
# and under each enabled package's directory; lib/ trees add their own.
GLOB_SOURCE_DIRS = ("src", "lib")
ALLOWED_DIR = "src/INTEL"


def digest(path):
    h = hashlib.sha1()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.digest()


def entries(root):
    """relpath -> (kind, value); kind is 'file', 'link' or 'dir'."""
    out = {}
    for dirpath, dirnames, filenames in os.walk(root, followlinks=False):
        for name in list(dirnames):
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            if os.path.islink(full):
                out[rel] = ("link", os.readlink(full))
                dirnames.remove(name)  # do not descend through a linked dir
            else:
                out[rel] = ("dir", "")
        for name in filenames:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            if os.path.islink(full):
                out[rel] = ("link", os.readlink(full))
            else:
                out[rel] = ("file", digest(full))
    return out


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    start, cand = argv[1], argv[2]
    for d in (start, cand):
        if not os.path.isdir(d):
            print("  the diff-scope gate could not run: %s is not a directory" % d)
            return 2
    try:
        a = entries(start)
        b = entries(cand)
    except OSError as exc:
        print("  the diff-scope gate could not run: %s" % exc)
        return 2
    if not a:
        print("  the diff-scope gate could not run: the starting tree %s is empty" % start)
        return 2

    violations = []
    for rel in sorted(a):
        kind, val = a[rel]
        if rel not in b:
            violations.append("REMOVED   %s" % rel)
        elif kind == "dir":
            if b[rel][0] != "dir":
                violations.append("REPLACED  %s (directory -> %s)" % (rel, b[rel][0]))
        elif b[rel][0] != kind:
            violations.append("REPLACED  %s (%s -> %s)" % (rel, kind, b[rel][0]))
        elif b[rel][1] != val:
            violations.append("MODIFIED  %s%s" % (rel, " (symlink target)" if kind == "link" else ""))

    added = sorted(rel for rel in b if rel not in a)
    in_scope = [rel for rel in added if rel.startswith(ALLOWED_DIR + "/")]
    compiled = [rel for rel in added
                if rel not in in_scope
                and b[rel][0] == "file"
                and rel.endswith(".cpp")
                and rel.split("/", 1)[0] in GLOB_SOURCE_DIRS]
    for rel in compiled:
        violations.append("ADDED     %s (compiled source outside %s/)" % (rel, ALLOWED_DIR))
    inert = [rel for rel in added if rel not in in_scope and rel not in compiled]

    print("  compared %d shipped path(s) against the tree the candidate was handed: "
          "%d changed or removed, %d addition(s) in %s/, "
          "%d compiled addition(s) outside it, %d inert addition(s)"
          % (len(a), len(violations) - len(compiled), len(in_scope), ALLOWED_DIR,
             len(compiled), len(inert)))
    if inert:
        print("  diagnostic (not scored): added outside %s/ and not compiled: %s%s"
              % (ALLOWED_DIR, " ".join(inert[:5]),
                 "" if len(inert) <= 5 else " (and %d more)" % (len(inert) - 5)))
    if violations:
        for line in violations[:MAX_PRINT]:
            print("  %s" % line)
        if len(violations) > MAX_PRINT:
            print("  ... and %d more" % (len(violations) - MAX_PRINT))
        print("  DIFF SCOPE VIOLATION: the tree may only change by files ADDED under "
              "%s/ -- every path that was in the tree the milestone handed over must "
              "still be there, unchanged." % ALLOWED_DIR)
        return 1
    print("  every shipped path is intact; additions stay inside %s/" % ALLOWED_DIR)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
