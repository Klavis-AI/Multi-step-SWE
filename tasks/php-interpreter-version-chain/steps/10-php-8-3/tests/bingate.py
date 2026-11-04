#!/usr/bin/env python3
"""Check that the graded binary really is the candidate's C sources, compiled.

Four checks, each stated in the step instruction:

 1. no embedded binary payload — outside `tests/` and `SPEC/`, every file that is not
    a C-family source and is over 256 KiB is payload, and no text file of any size or
    extension may carry a very long base64/hex blob;
 2. the executable's DWARF compilation units resolve, by `DW_AT_comp_dir` + `DW_AT_name`,
    to at least eight distinct `*.c` files that exist under the build tree — i.e. it was
    compiled, here, from the repository's own sources (this is why the build must keep
    debug information);
 3. the executable imports none of the process-spawning or dynamic-loading symbols —
    an interpreter written in C needs none of them, and a wrapper around another
    program needs at least one;
 4. the executable is a normally linked native program (it has a program interpreter
    or declares shared-library needs); a statically linked blob carries no imports to
    inspect and is refused.

Exit 0 when everything passes, 1 otherwise, printing the reason.
"""
import os
import re
import stat
import subprocess
import sys

# C-family sources are the only files exempt from the 256 KiB payload cap.  Anything
# else that large, outside tests/ and SPEC/, is treated as an embedded payload, so a
# prebuilt binary carried as text (`.txt`, `.json`, `.php`, ...) is caught too.
C_FAMILY_EXT = (".c", ".h", ".y", ".l", ".re", ".inc", ".cpp", ".cc", ".hpp", ".hh",
                ".cxx", ".hxx", ".ipp", ".tcc")
MAX_OTHER = 256 * 1024
READ_CAP = 64 * 1024 * 1024
BLOB = re.compile(rb"[A-Za-z0-9+/=]{20000,}|(?:\\x[0-9a-fA-F]{2}){8000,}|(?:0x[0-9a-fA-F]{2},\s*){8000,}")
# The same blob cut into short pieces ("...", "...", ...) is still a blob: what counts is
# how much base64-alphabet text a file carries in runs of 64 or more, not the longest run.
B64_RUN = re.compile(rb"[A-Za-z0-9+/=]{64,}")
B64_FILE_CAP = 64 * 1024
FORBIDDEN = {"execve", "execv", "execl", "execlp", "execvp", "execve@GLIBC", "system",
             "popen", "fork", "vfork", "posix_spawn", "posix_spawnp", "dlopen", "dlsym",
             "syscall", "ptrace", "clone"}


def fail(msg):
    print(msg)
    return 1


def read_regular(path, limit=READ_CAP):
    """Up to `limit` bytes of a regular file, read without following a symlink and
    without blocking.  This runs as root over a tree the candidate built, so a FIFO
    or a device under a source name must not hang the gate, and a symlink must not
    be followed out of the tree.  None if it is not a readable regular file."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError:
        return None
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            return None
        chunks, size = [], 0
        while size < limit:
            b = os.read(fd, min(1 << 20, limit - size))
            if not b:
                break
            chunks.append(b)
            size += len(b)
        return b"".join(chunks)
    except OSError:
        return None
    finally:
        os.close(fd)


def check_payload(root):
    # 1. no embedded payload
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in (".git", "__pycache__")]
        rel_dir = os.path.relpath(dirpath, root)
        if rel_dir.split(os.sep)[0] in ("tests", "SPEC"):
            continue
        for f in files:
            p = os.path.join(dirpath, f)
            try:
                st = os.lstat(p)
            except OSError:
                continue
            if not stat.S_ISREG(st.st_mode):      # a symlink/FIFO/device is not a source
                if stat.S_ISLNK(st.st_mode):
                    continue
                return fail("embedded payload: %s is not a regular file"
                            % os.path.relpath(p, root))
            ext = os.path.splitext(f)[1].lower()
            is_c = ext in C_FAMILY_EXT
            if not is_c and st.st_size > MAX_OTHER:
                return fail("embedded payload: %s is %d bytes and is not a C source file"
                            % (os.path.relpath(p, root), st.st_size))
            # the blob regex runs on every file regardless of size or extension;
            # a binary (NUL in the head) is not text and cannot be a source anyway
            data = read_regular(p)
            if data is None or b"\0" in data[:8192]:
                continue
            if BLOB.search(data):
                return fail("embedded payload: %s contains a very long encoded blob"
                            % os.path.relpath(p, root))
            if sum(len(m) for m in B64_RUN.findall(data)) > B64_FILE_CAP:
                return fail("embedded payload: %s carries more than %d characters of "
                            "base64-style encoded text" % (os.path.relpath(p, root), B64_FILE_CAP))

    print("no embedded binary payload")
    return 0


def _attr_value(line):
    """The value of a `DW_AT_* : ...` readelf line, handling the
    `(indirect line string, offset: 0x..): value` form."""
    if "): " in line:
        return line.rsplit("): ", 1)[1].strip()
    return line.split(": ", 1)[1].strip() if ": " in line else ""


def compile_units(binary):
    """(comp_dir, name) for every DWARF compilation unit DIE in `binary`."""
    try:
        out = subprocess.run(["readelf", "--debug-dump=info", binary],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             timeout=240).stdout.decode("utf8", "replace")
    except Exception as exc:                                      # noqa: BLE001
        print("readelf failed: %s" % exc)
        return []
    units = []
    in_cu = False
    name = comp_dir = None
    die_re = re.compile(r"^\s*<[0-9a-f]+><[0-9a-f]+>:\s*Abbrev Number:")
    for line in out.splitlines():
        if die_re.match(line):
            # a new DIE begins; close any CU we were collecting
            if in_cu and name is not None:
                units.append((comp_dir or "", name))
            in_cu = "(DW_TAG_compile_unit)" in line
            name = comp_dir = None
            continue
        if not in_cu:
            continue
        if "DW_AT_name" in line and name is None:
            name = _attr_value(line)
        elif "DW_AT_comp_dir" in line and comp_dir is None:
            comp_dir = _attr_value(line)
    if in_cu and name is not None:
        units.append((comp_dir or "", name))
    return units


def check_binary(root, binary):
    # 4. a normally linked native executable (has PT_INTERP or declares DT_NEEDED):
    # a static blob carries no imports for check 3 to inspect, so it is refused outright.
    try:
        seg = subprocess.run(["readelf", "-lW", binary], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, timeout=120).stdout.decode("utf8", "replace")
    except Exception:                                            # noqa: BLE001
        seg = ""
    try:
        dyns = subprocess.run(["readelf", "-dW", binary], stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, timeout=120).stdout.decode("utf8", "replace")
    except Exception:                                            # noqa: BLE001
        dyns = ""
    if "INTERP" not in seg and "(NEEDED)" not in dyns:
        return fail("the graded executable is statically linked (no program interpreter "
                    "and no shared-library needs); it carries no dynamic imports to "
                    "check, which a PHP interpreter compiled normally does not do")

    # 2. the binary was compiled, here, from the repository's own sources: each CU's
    # DW_AT_comp_dir + DW_AT_name must resolve to a real .c file under the build tree.
    # Basename matching would accept a prebuilt binary whose debug names merely happen
    # to match; a full-path resolution under the tree the grader built does not.
    root_real = os.path.realpath(root)
    matched = set()
    for comp_dir, name in compile_units(binary):
        if not name.endswith(".c"):
            continue
        full = name if os.path.isabs(name) else os.path.join(comp_dir, name)
        real = os.path.realpath(full)
        try:
            if os.path.commonpath([real, root_real]) != root_real:
                continue
        except ValueError:
            continue
        if os.path.isfile(real):
            matched.add(real)
    if len(matched) < 8:
        show = ", ".join(sorted(os.path.relpath(m, root_real) for m in matched)[:8])
        return fail("the graded executable's debug information resolves to only %d of the "
                    "build tree's own C files (%s); it was not compiled here from them, or "
                    "the build dropped debug information" % (len(matched), show))
    print("compiled here from %d of the build tree's own C files" % len(matched))

    # 3. no process spawning, no dynamic loading
    try:
        dyn = subprocess.run(["nm", "-D", "--undefined-only", binary],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             timeout=120).stdout.decode("utf8", "replace")
    except Exception:                                            # noqa: BLE001
        dyn = ""
    imported = set()
    for line in dyn.splitlines():
        parts = line.split()
        if parts:
            imported.add(parts[-1].split("@")[0])
    bad = sorted(imported & FORBIDDEN)
    if bad:
        return fail("the graded executable imports %s; a PHP interpreter written in C "
                    "does not start other programs or load shared objects to evaluate "
                    "PHP code" % ", ".join(bad))
    print("imports no process-spawning or dynamic-loading symbol")
    return 0


def main():
    if sys.argv[1] == "--payload":
        return check_payload(sys.argv[2])
    if sys.argv[1] == "--binary":
        return check_binary(sys.argv[2], sys.argv[3])
    print("usage: bingate.py --payload <root> | --binary <root> <executable>")
    return 2


if __name__ == "__main__":
    sys.exit(main())
