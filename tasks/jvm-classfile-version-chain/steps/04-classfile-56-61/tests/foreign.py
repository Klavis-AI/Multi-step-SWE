#!/usr/bin/env python3
"""Look for another Java runtime anywhere the candidate uid can write.

The launcher may not embed, link against or execute another Java virtual
machine.  The NEEDED check in test.sh sees only how the launcher is linked, and
the decoy sees only a `java` looked up on PATH; neither sees a launcher that
fetches a JDK and hands each case to it.  Whatever such a launcher runs, a stock
JVM cannot start without its class library on disk: a jimage (`lib/modules`),
an exploded tree holding the class it boots from (`jdk/internal/misc/Unsafe`,
or `sun/misc/Unsafe` before 9), or a jar holding that tree (`rt.jar`).  The
platform library this task ships is `java/**` only, so none of those can come
from it, and no build of this task's own virtual machine has a reason to make
one.

Which directories count is not named here: every directory the candidate uid
can create files in is found by walking the whole tree, and everything beneath
each one is inspected.  Files are opened without following links and without
blocking, and only regular files are read, so a FIFO or a device planted in a
writable directory cannot stall the scorer.  A tree too large to finish
inspecting is reported as such rather than passed.

Run directly, it scans once and prints `FOREIGN <path> -- <why>` per finding and
then one `SCAN ok|incomplete <n>` line.  run_corpus.py imports it and also scans
while the corpus runs.
"""
import os
import stat
import zipfile

CAND_UID = 65534
CAND_GID = 65534
# Never walked: kernel views, the device tree (its tmpfs is added back below),
# and the scorer's own root-only directories.
SKIP = ("/proc", "/sys", "/dev", "/logs", "/tests")
EXTRA = ("/dev/shm",)
MAX_ENTRIES = 2000000

MAGIC = {
    b"\xda\xda\xfe\xca": "a Java runtime image (jimage)",
    b"\xca\xfe\xda\xda": "a Java runtime image (jimage)",
    b"\xa2\xba\x0b\xf0": "a class-data-sharing archive",
    b"\xa8\xba\x0b\xf0": "a class-data-sharing archive",
}
BOOT_CLASSES = ("jdk/internal/misc/Unsafe.class", "sun/misc/Unsafe.class")
ARCHIVE_NAMES = frozenset(BOOT_CLASSES + ("java/lang/Object.class",))


def candidate_can_write(st):
    if st.st_uid == CAND_UID:
        return True
    if st.st_gid == CAND_GID and st.st_mode & 0o020:
        return True
    return bool(st.st_mode & 0o002)


def writable_roots():
    """Every directory the candidate uid can create entries in.

    Returns (roots, complete).  A writable directory is not descended into here:
    its whole subtree is inspected by scan(), writable or not.
    """
    roots, seen = [], 0
    stack = ["/"]
    for p in EXTRA:
        try:
            st = os.lstat(p)
        except OSError:
            continue
        if stat.S_ISDIR(st.st_mode):
            if candidate_can_write(st):
                roots.append(p)
            else:
                stack.append(p)
    while stack:
        d = stack.pop()
        try:
            it = os.scandir(d)
        except OSError:
            continue
        with it:
            for e in it:
                seen += 1
                if seen > MAX_ENTRIES:
                    return roots, False
                p = e.path
                if p in SKIP:
                    continue
                try:
                    if not e.is_dir(follow_symlinks=False):
                        continue
                    st = e.stat(follow_symlinks=False)
                except OSError:
                    continue
                if candidate_can_write(st):
                    roots.append(p)
                else:
                    stack.append(p)
    return roots, True


def inspect(path):
    """Why the regular file at `path` belongs to another Java runtime, or None."""
    for c in BOOT_CLASSES:
        if path.endswith("/" + c):
            return "a class a stock JVM boots from (%s)" % c
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW | os.O_NOCTTY)
    except OSError:
        return None
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode):
            return None
        head = os.pread(fd, 4, 0)
        if head in MAGIC:
            return MAGIC[head]
        if head != b"PK\x03\x04":
            return None
        try:
            with os.fdopen(os.dup(fd), "rb") as fh, zipfile.ZipFile(fh) as z:
                hit = sorted(ARCHIVE_NAMES.intersection(z.namelist()))
        except Exception:
            return None
        if hit:
            return "a class-library archive holding %s" % hit[0]
        return None
    finally:
        os.close(fd)


def scan(roots=None):
    """Returns (findings, complete, inspected)."""
    complete = True
    if roots is None:
        roots, complete = writable_roots()
    found, n = [], 0
    stack = list(roots)
    while stack:
        d = stack.pop()
        try:
            it = os.scandir(d)
        except OSError:
            continue
        with it:
            for e in it:
                n += 1
                if n > MAX_ENTRIES:
                    return found, False, n
                try:
                    if e.is_dir(follow_symlinks=False):
                        stack.append(e.path)
                        continue
                    if not e.is_file(follow_symlinks=False):
                        continue
                except OSError:
                    continue
                why = inspect(e.path)
                if why:
                    found.append((e.path, why))
    return found, complete, n


def main():
    found, complete, n = scan()
    for p, why in found[:20]:
        print("FOREIGN %s -- %s" % (p, why))
    print("SCAN %s %d" % ("ok" if complete else "incomplete", n))


if __name__ == "__main__":
    main()
