"""Find what the build produced and put it where the suite runs it.

The task never tells the agent which directory to build into. `cmake -S . -B
build` is the documented way to drive CMake and where a target then lands is
the project's own business, so the verifier locates the artefacts by the one
thing the instruction does fix -- their names -- and copies them into a single
directory the harness runs from.

Copies, not symlinks: ise derives its log directory from the directory holding
its own executable image, and `checkMultiInstance` takes an flock on that image.
A symlink would put the log beside the build output and would make the second
instance of `echo_server` flock a different inode from the first.

Two files with the same name and different contents are an ambiguity the
verifier refuses to resolve: picking one would decide the score by directory
order.
"""
import hashlib
import os
import shutil

from .normalize import EXE_NAMES

LIBRARY = "libise.a"
WANTED = tuple(EXE_NAMES) + (LIBRARY,)

# Never descend into these when looking for build output.
SKIP_DIRS = {".git", "CMakeFiles", "__pycache__", ".cache", "log"}


class StageError(Exception):
    pass


def digest(path):
    hasher = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def find(src, names=WANTED):
    """name -> sorted list of paths under `src` carrying that name."""
    want = set(names)
    found = {}
    for root, dirs, files in os.walk(src):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for name in sorted(files):
            if name not in want:
                continue
            path = os.path.join(root, name)
            if not os.path.isfile(path) or os.path.islink(path):
                continue
            if name != LIBRARY and not os.access(path, os.X_OK):
                # An unbuilt `examples/timer/timer.cpp` is not a hit, but a
                # stray non-executable file with an executable's name is worth
                # not silently treating as one either.
                continue
            found.setdefault(name, []).append(path)
    return found


def stage(src, dest, names=WANTED):
    """Copy every wanted artefact that the build actually produced into `dest`.

    Returns (chosen, report): the name -> source path actually used, and a list
    of human-readable lines for the build log, including one for every artefact
    that was missing or ambiguous.

    Missing artefacts are reported and skipped, not raised. Refusing to stage
    anything unless all sixteen are present makes the behaviour family stop
    measuring behaviour: a tree that builds fifteen of them and one that builds
    none score the same zero, and every recorded item that has nothing to do
    with the sixteenth is never even run. Measured on the near-miss tree,
    against the 422-item suite of the day: one unbuilt example took the whole
    family from 422/422 to 1/422 and the reward from 0.567 to 0.251, which says
    nothing about the eight style gates the near-miss exists to be wrong
    about. That the build must produce all of
    them is requirement 14 and is scored where requirements are scored, by the
    `artifacts` gate; here the only job is to run what there is.
    """
    found = find(src, names)
    chosen, report, problems = {}, [], []
    for name in names:
        paths = found.get(name, [])
        if not paths:
            problems.append("%s: the build produced no file with this name"
                            % name)
            continue
        digests = sorted(set(digest(p) for p in paths))
        if len(digests) > 1:
            problems.append(
                "%s: the build produced %d files with this name and %d "
                "different contents: %s"
                % (name, len(paths), len(digests),
                   ", ".join(os.path.relpath(p, src) for p in paths[:6])))
            continue
        chosen[name] = paths[0]
        report.append("%-20s %s%s" % (
            name, os.path.relpath(paths[0], src),
            "" if len(paths) == 1 else
            "  (%d identical copies)" % len(paths)))

    report.extend(problems)

    os.makedirs(dest, exist_ok=True)
    for name, path in chosen.items():
        target = os.path.join(dest, name)
        shutil.copyfile(path, target)
        # An explicit mode rather than the source's. The source was produced by
        # the candidate's build under the candidate's umask, and whether the
        # user the suite runs as can execute what it produced is not a thing
        # the candidate gets to decide by accident.
        os.chmod(target, 0o644 if name == LIBRARY else 0o755)
    return chosen, report
