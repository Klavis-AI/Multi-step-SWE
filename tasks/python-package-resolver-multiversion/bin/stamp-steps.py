#!/usr/bin/env python3
"""Give every step's tests/ tree its own mtime, and prove contents are untouched.

Why this exists
---------------
Each step's verifier image is built with that step's ``tests/`` as its Docker
build context.  The builder keys its content checksum for a local build context
on **(relative path, size, mtime-to-the-second)** and never reads the bytes.
The absolute context path is not part of the key, so per-step -- and per-task --
directories separate nothing.  A generator that writes every step's ``tests/``
in one pass gives them all one mtime; near-clone trees then share filenames and
sizes too, and every step after the first is built with the *first* step's tests
inside it.  The image is still tagged correctly and the run still completes; it
is simply the wrong milestone's score.

Measured against the real builder, five steps with identical sizes and mtimes:

    nothing ............................ steps 02-05 got step 01's tests/
    distinct parent layer before COPY .. steps 02-05 got step 01's tests/
    uniquely-named marker file per step . steps 02-05 got step 01's tests/
    distinct mtime per step ............ every step got its own tests/

So this is the only thing that works.  Note the two failures: the stale checksum
is cached *per file*, so a new file is read fresh while ``test.sh`` beside it is
still served from the previous step, and no Dockerfile instruction can reach the
problem at all.  Content is not a lever -- the colliding trees already differed
in content.

Because the key is a *relative* path, two different tasks collide as readily as
two steps of one task: every task has a ``test.sh``.  Timestamps are therefore
derived from the task name as well as the step index, so stamping one task can
never put it on top of another.

Usage
-----
    bin/stamp-steps.py PATH...            stamp, printing a content witness
    bin/stamp-steps.py --verify PATH...   check only; non-zero if unsafe

PATH may be a task directory (one containing ``steps/``) or any directory above
one, in which case every task beneath it is included.  Pass the whole corpus to
--verify to check across tasks as well as within them.
"""

import hashlib
import os
import sys
from collections import defaultdict
from pathlib import Path

# Arbitrary fixed point, so re-stamping is idempotent and two authors stamping
# the same task agree.  Tasks are a minute apart and steps a second apart: the
# cache key compares whole seconds, so one second is a real separation, and a
# minute leaves room for more steps than any task has.  The resulting times stay
# well inside the 32-bit epoch.
BASE = 1700000000
TASK_STRIDE = 60


def task_slot(name: str) -> int:
    """Stable per-task offset. Hashed, not sequential, so adding or renaming a
    task never shifts another task's timestamps."""
    digest = hashlib.blake2b(name.encode(), digest_size=8).digest()
    return int.from_bytes(digest, "big") % 1_000_000


def content_hash(tree: Path) -> str:
    """Digest of relative paths and file bytes only -- no timestamps -- so a
    stamping run can be shown to have changed nothing but metadata."""
    digest = hashlib.sha256()
    for rel in sorted(p.relative_to(tree).as_posix() for p in tree.rglob("*") if p.is_file()):
        digest.update(rel.encode())
        digest.update(b"\0")
        digest.update((tree / rel).read_bytes())
    return digest.hexdigest()[:16]


def find_tasks(root: Path) -> list[Path]:
    if (root / "steps").is_dir():
        return [root]
    return sorted({p.parent for p in root.glob("*/steps")} | {p.parent for p in root.glob("*/*/steps")})


def step_trees(task: Path) -> list[Path]:
    return sorted(d / "tests" for d in (task / "steps").iterdir() if (d / "tests").is_dir())


def stamp(task: Path) -> int:
    slot = task_slot(task.name)
    for index, tree in enumerate(step_trees(task)):
        before = content_hash(tree)
        when = BASE + slot * TASK_STRIDE + index
        for path in [tree, *tree.rglob("*")]:
            os.utime(path, (when, when), follow_symlinks=False)
        after = content_hash(tree)
        if before != after:
            print(f"BUG  {task.name}/{tree.parent.name}: content changed ({before} -> {after})", file=sys.stderr)
            return 1
        print(f"stamped  {task.name}/{tree.parent.name:<34} mtime {when}  content {after} (unchanged)")
    return 0


def verify(tasks: list[Path]) -> int:
    """The real predicate: no two step trees may share a (relative path, size,
    mtime) for files whose bytes differ.  That -- not tidy timestamps -- is what
    decides whether one step can be built with another's tests."""
    keys = defaultdict(list)
    for task in tasks:
        for tree in step_trees(task):
            for path in tree.rglob("*"):
                if not path.is_file():
                    continue
                stat = path.stat()
                key = (path.relative_to(tree).as_posix(), stat.st_size, int(stat.st_mtime))
                keys[key].append((task.name, tree.parent.name, path))

    collisions = []
    for key, entries in sorted(keys.items()):
        if len(entries) < 2:
            continue
        if len({hashlib.sha256(p.read_bytes()).hexdigest() for _, _, p in entries}) < 2:
            continue  # same bytes: the shared cache entry is the correct one
        collisions.append((key, entries))

    trees = sum(len(step_trees(t)) for t in tasks)
    if not collisions:
        print(f"ok  {len(tasks)} task(s), {trees} step tree(s), no colliding build-context keys")
        return 0

    for (rel, size, mtime), entries in collisions:
        print(f"COLLISION  {rel}  size={size} mtime={mtime}", file=sys.stderr)
        for task_name, step_name, _ in entries:
            print(f"           {task_name}/{step_name}", file=sys.stderr)
    print(
        f"\n{len(collisions)} colliding key(s). Steps sharing one are graded by each other's\n"
        f"verifier, at an ordinary-looking score. Run bin/stamp-steps.py on the\n"
        f"affected task(s), then re-run the Oracle.",
        file=sys.stderr,
    )
    return 1


def main() -> int:
    args = sys.argv[1:]
    verify_only = "--verify" in args
    paths = [Path(a) for a in args if a != "--verify"] or [Path(".")]

    tasks: list[Path] = []
    for path in paths:
        if not path.is_dir():
            print(f"not a directory: {path}", file=sys.stderr)
            return 2
        tasks.extend(find_tasks(path))
    tasks = sorted(set(tasks))
    if not tasks:
        print(f"no task directories (containing steps/) found under: {', '.join(map(str, paths))}", file=sys.stderr)
        return 2

    if verify_only:
        return verify(tasks)
    return max((stamp(t) for t in tasks), default=0)


if __name__ == "__main__":
    sys.exit(main())
