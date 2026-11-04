#!/usr/bin/env python3
"""Give a finished build back to root before anything is scored from it.

    python3 freeze.py [--restore-from <untouched tree>] <dir> [<dir> ...]

Both builds run as the score uid (`build.py` hands the copy over), and the
copies stay that uid's afterwards. The scenarios that read them -- the source
and `.i` scans, `nm`, the build report -- run while the same uid has servers
up, so without this a server started by one scenario could rewrite what a gate
reads in another. `test.sh` calls this after the last build and after the reap,
when nothing of that uid is left running, so what the gates read is exactly
what the build left.

Every entry becomes root's and nobody else may write it: files keep their read
and execute bits (the owner's are given to group and other, so the score uid
can still read and run what it built) and lose every write bit but root's and
any set-id bit; directories become 0755. A symbolic link that resolves outside
the frozen directory would leave its target where the score uid can still reach
it, so it is replaced by a copy of what it points at when that is a regular
file, and removed otherwise.

`--restore-from` then makes every source file the gates read as text
(`textscan.walk` over the untouched tree, text files only) byte-identical to
the untouched tree's copy: the build is the candidate's `make`, and a recipe
could rewrite, replace or delete a source in the copy before a gate reads it.
What the build *added* (objects, `.i`, binaries, generated headers) is left as
built. Each file put back is listed. A path component the build turned into a
link or a file is made a directory again.
"""

import os
import shutil
import stat
import sys

sys.dont_write_bytecode = True

# A link to a file bigger than this is removed rather than copied: nothing a
# gate reads is anywhere near it, and the copy is made as root.
COPY_LIMIT = 64 << 20


def _inside(path, root):
    return path == root or path.startswith(root + os.sep)


def _settle_link(path, root, log):
    target = os.path.realpath(path)
    if _inside(target, root):
        return
    try:
        st = os.stat(path)
    except OSError:
        st = None
    os.unlink(path)
    if st is not None and stat.S_ISREG(st.st_mode) and st.st_size <= COPY_LIMIT:
        try:
            shutil.copyfile(target, path, follow_symlinks=True)
            os.chmod(path, st.st_mode & 0o755)
            log.append("copied %s (link to %s)" % (path, target))
            return
        except OSError:
            if os.path.lexists(path):
                os.unlink(path)
    log.append("removed %s (link to %s)" % (path, target))


def freeze(root):
    root = os.path.realpath(root)
    log = []
    if not os.path.isdir(root):
        return log
    os.chown(root, 0, 0)
    os.chmod(root, 0o755)
    for dirpath, dirnames, filenames in os.walk(root):
        for name in list(dirnames) + filenames:
            path = os.path.join(dirpath, name)
            if os.path.islink(path):
                _settle_link(path, root, log)
            if not os.path.lexists(path):
                continue
            os.lchown(path, 0, 0)
            if os.path.islink(path):
                continue
            mode = os.lstat(path).st_mode
            if stat.S_ISDIR(mode):
                os.chmod(path, 0o755)
            else:
                # What the score uid could read or run as the owner it can
                # still read or run as "other", and nothing else changes.
                own = (stat.S_IMODE(mode) >> 6) & 0o5
                os.chmod(path, (stat.S_IMODE(mode) & 0o755) | own | (own << 3))
    return log


def _real_dirs(root, rel_dir):
    """Make every component of `rel_dir` under `root` a real directory."""
    cur = root
    for part in [p for p in rel_dir.split(os.sep) if p]:
        cur = os.path.join(cur, part)
        if os.path.islink(cur) or (os.path.lexists(cur) and not os.path.isdir(cur)):
            os.unlink(cur)
        if not os.path.lexists(cur):
            os.mkdir(cur, 0o755)


def restore(dest, src):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import textscan
    dest = os.path.realpath(dest)
    log = []
    for rel in textscan.walk(src):
        s = os.path.join(src, rel)
        if os.path.islink(s) or not os.path.isfile(s) or not textscan.is_text(s):
            continue
        with open(s, "rb") as f:
            want = f.read()
        d = os.path.join(dest, rel)
        _real_dirs(dest, os.path.dirname(rel))
        if os.path.isfile(d) and not os.path.islink(d):
            with open(d, "rb") as f:
                if f.read() == want:
                    continue
            why = "differed"
        else:
            why = "missing" if not os.path.lexists(d) else "not a regular file"
        if os.path.isdir(d) and not os.path.islink(d):
            shutil.rmtree(d)
        elif os.path.lexists(d):
            os.unlink(d)
        mode = stat.S_IMODE(os.stat(s).st_mode) & 0o755
        own = (mode >> 6) & 0o5
        fd = os.open(d, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     mode | own | (own << 3))
        with os.fdopen(fd, "wb") as f:
            f.write(want)
        log.append("restored %s (%s in the build copy)" % (rel, why))
    return log


def main(argv):
    args = list(argv[1:])
    src = None
    if args[:1] == ["--restore-from"]:
        src = args[1]
        args = args[2:]
    for d in args:
        for line in freeze(d):
            print("freeze: " + line)
        print("freeze: %s is root's" % d)
        if src:
            lines = restore(d, src)
            for line in lines:
                print("freeze: " + line)
            print("freeze: %s: %d source file(s) put back from %s"
                  % (d, len(lines), src))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
