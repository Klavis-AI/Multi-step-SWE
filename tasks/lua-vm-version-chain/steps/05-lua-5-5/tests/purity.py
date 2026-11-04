#!/usr/bin/env python3
"""Language-purity gate.

The task is to write a Lua implementation in Java. This checks that what was
built is exactly that: no C/assembly sources, no prebuilt or native binaries, no
third-party archives, and no compiled class that can reach outside the JVM.
"""
import os, re, shutil, subprocess, sys, tempfile

# Only version control is skipped. The reference manual (.html/.css) and the
# official suites (.lua/README) live under SPEC/ and tests/ and match none of the
# forbidden file types below, so scanning them costs nothing and denies the
# "hide a .jar under tests/ and launch it" bypass.
SKIP_DIRS = {".git"}
SRC_EXT = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh", ".s", ".asm", ".rs", ".go"}
BIN_EXT = {".so", ".dll", ".dylib", ".a", ".o", ".obj", ".exe", ".jar", ".war"}
MAGIC = [b"\x7fELF", b"MZ", b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xce", b"PK\x03\x04"]

BANNED_REFS = [
    "java/lang/Runtime", "java/lang/ProcessBuilder", "java/lang/Process",
    "java/lang/foreign/", "sun/misc/Unsafe", "jdk/internal/",
    "java/lang/reflect/", "java/lang/invoke/MethodHandles.", "java/net/",
    "javax/script/", "java/lang/ClassLoader", "java/lang/instrument",
    "java/nio/channels/", "java/lang/Class.forName", "java/lang/System.load",
    "java/lang/System.loadLibrary",
]
# What the launcher must not be: a wrapper around another language's runtime or
# around a Lua installed somewhere else. Paths on their own are not listed --
# a launcher is allowed to name a temporary directory for the JVM.
BANNED_WRAPPER = ["python", "perl", "node", "ruby", "gcc", "cc ", "clang",
                  "curl", "wget", "lua5", "luajit", "/usr/bin/lua", "/usr/local/bin/lua",
                  "wine", "qemu"]

# Anything a build could use to fetch or run something that is not the JDK.
BANNED_BUILD = ["curl", "wget", "http://", "https://", "ftp://", "git clone", "pip ",
                "pip3", "apt-get", "apt ", "npm ", "gcc", "g++", "clang", "cc -",
                "python", "perl", "ruby", "node ", "nc ", "scp ", "rsync", "base64 -d",
                "uudecode", "xxd -r", "openssl"]
BUILD_FILES = ("makefile", "gnumakefile")
BUILD_EXT = {".mk", ".sh", ".bash", ".py", ".pl", ".rb", ".js"}

def walk(root):
    for dirpath, dirnames, filenames in os.walk(root):
        rel = os.path.relpath(dirpath, root)
        parts = [] if rel == "." else rel.split(os.sep)
        if parts and parts[0] in SKIP_DIRS:
            dirnames[:] = []
            continue
        for f in filenames:
            yield os.path.join(dirpath, f)

def fail(msg):
    print("PURITY FAIL:", msg)
    sys.exit(1)


# The launcher must start the JVM on the classes built here, not a native
# interpreter smuggled into the workspace. phase_pre already rejects
# a native binary by its first four bytes, but an ELF with one junk byte in front
# (shipped as, say, `blob.txt`, then de-prefixed and exec'd by a tiny launcher)
# slips past that. This scans every regular file for an executable image's magic
# at ANY offset: ELF and the Mach-O thin/reverse forms. Java `.class` files
# (0xCAFEBABE) are build output and are skipped by content AND name, so they are
# never confused with a Mach-O fat binary. Reads are bounded and never follow a
# symlink or open a non-regular file (a FIFO would otherwise block root), and the
# per-file read is capped so a huge sparse file cannot stall the scan.
import stat as _stat
_EXE_MAGIC = (b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xce",
              b"\xce\xfa\xed\xfe", b"\xfa\xed\xfe\xcf", b"\xfa\xed\xfe\xce")
_CLASS_MAGIC = b"\xca\xfe\xba\xbe"
_EXE_KEEP = max(len(m) for m in _EXE_MAGIC) - 1
_EXE_CAP = 64 * 1024 * 1024


def _is_executable_image(path):
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError:
        return False
    try:
        if not _stat.S_ISREG(os.fstat(fd).st_mode):
            return False
        data = os.read(fd, 1 << 20)
        if data[:4] == _CLASS_MAGIC and path.endswith(".class"):
            return False          # a genuine Java class, this task's own output
        total, tail = len(data), b""
        while data:
            chunk = tail + data
            for m in _EXE_MAGIC:
                if m in chunk:
                    return True
            tail = chunk[-_EXE_KEEP:]
            if total >= _EXE_CAP:
                return False
            data = os.read(fd, 1 << 20)
            total += len(data)
        return False
    finally:
        os.close(fd)


def scan_blobs(root):
    for path in walk(root):
        try:
            if not _stat.S_ISREG(os.lstat(path).st_mode):
                continue
        except OSError:
            continue
        if _is_executable_image(path):
            fail("an executable binary image is present in the workspace: %s "
                 "(the implementation must be Java built here, not a native "
                 "program carried in the tree)" % os.path.relpath(path, root))

# Files this task ships into the candidate workspace (the local test runner and its
# helper). They belong to the task, not the candidate, so a candidate build that
# merely names one must not be judged for what the shipped file does. The exemption
# is content-keyed: it applies only while the file is byte-identical to what shipped,
# so a candidate who overwrites the runner with a fetch script gets it scanned like
# any other build file.
# The manifest sits beside this script inside the verifier image (/tests), which the
# candidate cannot read or write; there is no environment override to spoof.
MANIFEST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "workdir-manifest.txt")


def supplied_unmodified(root):
    """Absolute paths of shipped workspace files still identical to what we shipped."""
    out = set()
    try:
        raw = open(MANIFEST, "rb").read().decode("utf-8")
    except OSError:
        return out          # no manifest -> exempt nothing; the gate stays as strict
    import hashlib
    for line in raw.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 2:
            continue
        rel, want = parts
        # never exempt a makefile: it is the root of the closure, not a leaf of it
        if os.path.basename(rel).lower() in BUILD_FILES:
            continue
        path = os.path.join(root, rel)
        try:
            got = hashlib.sha256(open(path, "rb").read()).hexdigest()
        except OSError:
            continue
        if got == want:
            out.add(os.path.normpath(path))
    return out


def build_closure(root):
    """The makefiles, plus every script they reach.

    The rule is about the build: a makefile, and any script the build runs.
    A helper the candidate keeps in the workspace but the build never touches
    is not a build file, so it is not scanned -- scanning it would fail a
    workspace for a note-to-self with a shebang in it.
    """
    texts = {}
    for path in walk(root):
        name = os.path.basename(path).lower()
        ext = os.path.splitext(name)[1]
        if name in BUILD_FILES or ext in BUILD_EXT:
            try:
                texts[path] = open(path, "rb").read().decode("latin-1")
            except OSError:
                pass
    for p in list(texts):
        if os.path.normpath(p) in supplied_unmodified(root):
            del texts[p]
    reached = {p for p in texts if os.path.basename(p).lower() in BUILD_FILES}
    changed = True
    while changed:
        changed = False
        blob = "\n".join(texts[p] for p in reached).lower()
        for p in texts:
            if p in reached:
                continue
            base = os.path.basename(p).lower()
            if base in blob:
                reached.add(p)
                changed = True
    return {p: texts[p] for p in reached}


def scan_build(root):
    """The build may run the JDK and nothing else: no fetching, no other toolchain."""
    for path, raw in sorted(build_closure(root).items()):
        # Drop comment lines (# ... ) so a comment mentioning a banned word does
        # not fail an honest build; recipe lines that run the word still match.
        lines = []
        for line in raw.splitlines():
            stripped = line.lstrip()
            if stripped.startswith("#"):
                continue
            lines.append(line.split(" #", 1)[0] if " #" in line else line)
        text = "\n".join(lines).lower()
        for bad in BANNED_BUILD:
            if bad in text:
                fail("build file %s uses %r; the build may run only the JDK"
                     % (os.path.relpath(path, root), bad.strip()))


def phase_pre(root):
    for path in walk(root):
        ext = os.path.splitext(path)[1].lower()
        if ext in SRC_EXT:
            fail("non-Java implementation source present: %s" % os.path.relpath(path, root))
        if ext in BIN_EXT:
            fail("prebuilt binary or archive present: %s" % os.path.relpath(path, root))
        try:
            with open(path, "rb") as fh:
                head = fh.read(4)
        except OSError:
            continue
        for m in MAGIC:
            if head.startswith(m):
                fail("compiled binary present: %s" % os.path.relpath(path, root))
    scan_blobs(root)
    scan_build(root)
    print("PURITY PRE OK")

def clean(root):
    # Remove only build output (.class) left from a prior build. A .jar is never
    # legitimate output for this task, so it is NOT removed here -- deleting it
    # would hide the violation from the scan below; phase_pre must catch it.
    for path in walk(root):
        if os.path.splitext(path)[1].lower() == ".class":
            os.remove(path)

def classes(root):
    out = []
    for path in walk(root):
        if path.endswith(".class"):
            out.append(path)
    return out

def phase_post(root):
    cs = classes(root)
    if not cs:
        fail("no compiled Java classes under the workspace after 'make'")
    for path in walk(root):
        ext = os.path.splitext(path)[1].lower()
        if ext in BIN_EXT:
            fail("build produced a non-Java artifact: %s" % os.path.relpath(path, root))
        if ext in SRC_EXT:
            fail("non-Java implementation source present: %s" % os.path.relpath(path, root))
    launcher = os.path.join(root, "lua")
    if not os.path.exists(launcher):
        fail("'make' did not produce ./lua")
    with open(launcher, "rb") as fh:
        head = fh.read(4)
    if any(head.startswith(m) for m in MAGIC):
        fail("./lua is a native binary")
    if os.path.getsize(launcher) > 4096:
        fail("./lua is larger than 4096 bytes, so it is not a launcher script")
    text = open(launcher, "rb").read().decode("latin-1").lower()
    if "java" not in text:
        fail("./lua does not start a JVM")
    for bad in BANNED_WRAPPER:
        if bad in text:
            fail("./lua runs %r" % bad.strip())
    pats = []
    for ref in BANNED_REFS:
        for spelling in (ref, ref.replace("/", ".")):
            tail = "" if spelling[-1] in "/." else r"(?![A-Za-z0-9_$])"
            pats.append((ref, re.compile(re.escape(spelling) + tail)))
    bad = []
    for i in range(0, len(cs), 40):
        batch = cs[i:i + 40]
        p = subprocess.run(["javap", "-p", "-c", "-constants"] + batch,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        text = p.stdout.decode("latin-1")
        for ref, rx in pats:
            if rx.search(text):
                bad.append(ref)
        if re.search(r"\bnative\s+\w[\w.$<>\[\]]*\s+\w+\(", text):
            bad.append("native method")
    if bad:
        fail("compiled classes reference forbidden APIs: %s" % ", ".join(sorted(set(bad))))
    scan_blobs(root)
    scan_build(root)
    print("PURITY POST OK (%d classes)" % len(cs))


RUN_UID = int(os.environ.get("LUA_GRADER_UID", "4242"))


def drop_privs():
    # A preexec_fn for the depends probe: it must not fail open. A drop that
    # silently did nothing would run ./lua (candidate code) as root, so verify
    # the post-condition and _exit the child if it did not land (run_corpus does
    # the same).
    os.setgroups([])
    os.setgid(RUN_UID)
    os.setuid(RUN_UID)
    if os.getuid() != RUN_UID or os.geteuid() != RUN_UID:
        os._exit(71)


def phase_depends(root):
    """
    The launcher must actually run the classes built here: with them moved away
    it has to fail. A launcher around some other interpreter would still work.
    """
    cs = classes(root)
    if not cs:
        fail("no compiled Java classes under the workspace")
    # The stash goes inside the workspace: a fresh directory (so nothing can be
    # waiting there for us) and on the same filesystem, since /app is a mount of
    # its own in some environments and a rename across devices fails.
    stash = tempfile.mkdtemp(prefix=".classstash.", dir=root)
    moved = []
    for i, c in enumerate(cs):
        dst = os.path.join(stash, "%d.class" % i)
        os.rename(c, dst)
        moved.append((dst, c))
    try:
        # ./lua is the candidate's launcher: it runs with no privileges here
        # too, so that a launcher which is really a shell script cannot use
        # this check to read the grader's files.
        p = subprocess.run([os.path.join(root, "lua"), "-e", "os.exit(0)"], cwd=root,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60,
                           preexec_fn=drop_privs if os.geteuid() == 0 else None)
        rc = p.returncode
    except Exception:
        rc = 1
    finally:
        for dst, c in moved:
            os.rename(dst, c)
        shutil.rmtree(stash, ignore_errors=True)
    if rc == 0:
        fail("./lua still runs with every compiled class removed, so it is not "
             "running the Java implementation in this workspace")
    print("PURITY DEPENDS OK")

if __name__ == "__main__":
    mode, root = sys.argv[1], sys.argv[2]
    if mode == "pre":
        clean(root)
        phase_pre(root)
    elif mode == "depends":
        phase_depends(root)
    else:
        phase_post(root)
