#!/usr/bin/env python3
"""Provenance gates for the remote end: is what gets graded the C program this
workspace builds, and nothing else doing the work behind it?

Shared, byte-identical, across every step. Run as root, always with `python3 -I`.

The instruction says the remote end is written in C11 and built with cc, in a
grading container that has a C toolchain and nothing else, and forbids
downloading, copying, wrapping or invoking another implementation. Without
these checks a Makefile could fetch a toolchain and an existing layout library,
or build/cssdriver could be a script that runs one. Each subcommand closes one
way in:

  scrub DIR       before the build: delete every compiled artifact the workspace
                  arrived with (by content, whatever its name), so nothing
                  prebuilt elsewhere can be linked in;
  build ...       run the build as the unprivileged uid with outbound network
                  refused at the syscall level, and watch every process it
                  starts: each program it executes, and each file it maps
                  executable, must be a root-owned system file or a C program
                  that passes the same test as the deliverable;
  check ...       after the build: the deliverable must be a native x86-64 C
                  executable that links only the C library, carries no other
                  language's runtime and no embedded executable image, and the
                  workspace's C sources must not be a known layout engine;
  sweep           kill every process of the unprivileged uid;

and score.py runs a Monitor in "serve" mode while it grades: every process of
the unprivileged uid must be the executable `check` passed, mapping no code but
its own and the system's.

Inspecting another uid's /proc/<pid>/exe and maps needs ptrace-read access,
which root without CAP_SYS_PTRACE (the container default) lacks. The monitor
thread therefore sets its own filesystem uid to the candidate's, which the
kernel's ptrace-read check accepts, and leaves every other thread root. Each
monitor proves first, on a process of known identity, that it can see what it
claims to check; a platform that does not allow it is reported, not guessed.
"""
import argparse, ctypes, errno, os, pwd, signal, stat, struct, subprocess, sys, threading, time

# --------------------------------------------------------------------------
# What a C11 program built in this image may consist of.
# --------------------------------------------------------------------------

# The C library and its parts. Anything else a binary asks the loader for came
# from somewhere this image does not provide.
C_LIBRARY = {
    "libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
    "libresolv.so.2", "libutil.so.1", "libanl.so.1", "libmvec.so.1",
    "ld-linux-x86-64.so.2",
    # the compiler's own runtime libraries, installed with gcc in this image
    "libgcc_s.so.1", "libasan.so.8", "libubsan.so.1", "libtsan.so.2", "liblsan.so.0",
    "libgomp.so.1", "libatomic.so.1", "libquadmath.so.0", "libitm.so.1",
}
SYSTEM_LOADERS = {
    "/lib64/ld-linux-x86-64.so.2",
    "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2",
    "/usr/lib64/ld-linux-x86-64.so.2",
    "/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2",
}

# Bytes that only another language's compiler or runtime, an embedded virtual
# machine, or an existing layout engine leaves in a binary. Case-sensitive and
# specific on purpose: each is something a C program written for this task has
# no reason to contain.
FOREIGN_MARKERS = (
    # Rust
    b"rust_begin_unwind", b"rust_eh_personality", b"__rust_alloc", b"__rust_dealloc",
    b"rustc version", b"/rustc/", b"RUST_BACKTRACE", b"called `Option::unwrap()`",
    b"called `Result::unwrap()`", b"index out of bounds: the len is",
    # Go
    b"Go build ID:", b"go.buildid", b"runtime.gopanic", b"runtime.goexit",
    # C++ (no C++ compiler exists here, so its runtime can only have been imported)
    b"__gxx_personality_v0", b"__cxa_throw", b"__cxa_begin_catch", b"GLIBCXX_",
    b"CXXABI_", b"_ZNSt", b"St9exception", b"NSt3__1", b"GNU C++", b"clang version",
    # other compiled languages
    b"swift_retain", b"_gfortran_", b"caml_startup", b"__gnat_",
    # embedded interpreters and virtual machines
    b"Py_Initialize", b"PyRun_SimpleString", b"JS_NewRuntime", b"duk_create_heap",
    b"luaL_newstate", b"JNI_CreateJavaVM", b"mono_jit_init", b"wasm_runtime_init",
    b"wasmtime_", b"m3_NewEnvironment", b"m3_ParseModule", b"v8::Isolate",
    b"\x00asm\x01\x00\x00\x00",
)
# Runtime entry points short enough to occur inside an ordinary C name
# (`paths_init` contains `hs_init`), so they only count as a whole symbol name.
WHOLE_SYMBOL_MARKERS = (b"hs_init", b"_d_run_main", b"NimMain")
# Identifiers of existing layout and browser engines. Checked in the binary and
# in the workspace's C sources and build files.
ENGINE_MARKERS = (
    b"YGNodeNew", b"YGNodeCalculateLayout", b"YGConfigNew", b"litehtml::",
    b"lxb_html_", b"lxb_css_", b"lwc_intern_string",
    b"taffy::", b"/taffy/", b"taffy-0.", b"TaffyTree", b"WebCore::", b"blink::",
    b"nsIFrame",
)
SOURCE_ONLY_MARKERS = (b"incbin",)   # .incbin pulls a binary blob into an object
SOURCE_SUFFIXES = (".c", ".h", ".i", ".inc", ".def", ".s", ".S", ".asm", ".mk")
SOURCE_NAMES = ("Makefile", "makefile", "GNUmakefile")

# Compiled artifacts a source tree has no business arriving with.
COMPILED_MAGIC = (
    b"\x7fELF", b"!<arch>\n", b"\xfe\xed\xfa\xce", b"\xfe\xed\xfa\xcf",
    b"\xce\xfa\xed\xfe", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe",
    b"\x00asm", b"BC\xc0\xde", b"\xde\xc0\x17\x0b",
)

# Where a file the candidate's uid could have written may live; nothing mapped
# or executed from here is a system file, whoever owns it.
CANDIDATE_WRITABLE = ("/work", "/app", "/tmp", "/var/tmp", "/dev/shm", "/home",
                      "/run", "/var/lock", "/proc", "/memfd:")


def clean(s):
    """ASCII-escape a candidate-chosen string: a file name or a path must not be
    able to start a line that a log reader would take for a verdict."""
    return ascii(str(s))[1:-1]


def say(msg):
    print(msg if msg.isascii() and "\n" not in msg and "\r" not in msg
          else clean(msg), flush=True)


# --------------------------------------------------------------------------
# ELF
# --------------------------------------------------------------------------

def _u(fmt, data, off):
    return struct.unpack_from("<" + fmt, data, off)[0]


def elf_problems(data, want_exec=True):
    """Why `data` is not a native x86-64 C program built here ([] if it is)."""
    probs = []
    if data[:4] != b"\x7fELF":
        return ["not an ELF file (a script or data, not a program built from C)"]
    if len(data) < 64 or data[4] != 2 or data[5] != 1:
        return ["not a 64-bit little-endian ELF file"]
    e_type, e_machine = _u("H", data, 16), _u("H", data, 18)
    if e_machine != 62:
        probs.append("ELF machine %d is not x86-64" % e_machine)
    if want_exec and e_type not in (2, 3):
        probs.append("ELF type %d is not an executable" % e_type)
    phoff, phentsize, phnum = _u("Q", data, 32), _u("H", data, 54), _u("H", data, 56)
    shoff, shentsize, shnum, shstrndx = (_u("Q", data, 40), _u("H", data, 58),
                                         _u("H", data, 60), _u("H", data, 62))
    loads, dyn = [], None
    for i in range(phnum):
        o = phoff + i * phentsize
        if o + 56 > len(data):
            probs.append("truncated program headers")
            break
        p_type, p_offset, p_vaddr, p_filesz = (_u("I", data, o), _u("Q", data, o + 8),
                                               _u("Q", data, o + 16), _u("Q", data, o + 32))
        if p_type == 3:      # PT_INTERP
            interp = data[p_offset:p_offset + p_filesz].split(b"\0")[0].decode("latin1")
            if interp not in SYSTEM_LOADERS:
                probs.append("program interpreter %r is not the system loader" % interp)
        elif p_type == 1:    # PT_LOAD
            loads.append((p_vaddr, p_offset, p_filesz))
        elif p_type == 2:    # PT_DYNAMIC
            dyn = (p_offset, p_filesz)

    def v2o(vaddr):
        for va, off, sz in loads:
            if va <= vaddr < va + sz:
                return off + (vaddr - va)
        return None

    if dyn is not None:
        needed, strtab = [], None
        for i in range(dyn[1] // 16):
            o = dyn[0] + i * 16
            if o + 16 > len(data):
                break
            tag, val = _u("q", data, o), _u("Q", data, o + 8)
            if tag == 0:
                break
            if tag == 1:
                needed.append(val)
            elif tag == 5:
                strtab = v2o(val)
        for n in needed:
            if strtab is None:
                probs.append("dynamic section without a string table")
                break
            name = data[strtab + n:strtab + n + 256].split(b"\0")[0].decode("latin1")
            if name not in C_LIBRARY:
                probs.append("links %r, which is not part of the C library" % name)

    # .comment names every compiler whose output was linked in.
    if shoff and shnum and shoff + shnum * shentsize <= len(data) and shstrndx < shnum:
        so = shoff + shstrndx * shentsize
        strs_off = _u("Q", data, so + 24)
        for i in range(shnum):
            o = shoff + i * shentsize
            name_off = _u("I", data, o)
            name = data[strs_off + name_off:strs_off + name_off + 32].split(b"\0")[0]
            if name == b".comment":
                c_off, c_size = _u("Q", data, o + 24), _u("Q", data, o + 32)
                for entry in data[c_off:c_off + c_size].split(b"\0"):
                    if entry and not entry.startswith(b"GCC: ("):
                        probs.append("built in part by %r, not by this image's C compiler"
                                     % entry[:60].decode("latin1"))

    for m in FOREIGN_MARKERS + ENGINE_MARKERS + tuple(b"\0" + w + b"\0"
                                                      for w in WHOLE_SYMBOL_MARKERS):
        if m in data:
            probs.append("contains %r, the mark of another language's runtime, an "
                         "embedded virtual machine or an existing engine"
                         % m.strip(b"\0").decode("latin1"))
    # A second executable image carried inside this one.
    start = 1
    while True:
        k = data.find(b"\x7fELF\x02\x01\x01", start)
        if k < 0:
            break
        if (k + 64 <= len(data) and _u("H", data, k + 16) in (1, 2, 3)
                and _u("H", data, k + 18) == 62 and _u("I", data, k + 20) == 1
                and _u("H", data, k + 52) == 64):
            probs.append("carries another ELF image inside it at offset %d" % k)
            break
        start = k + 1
    if b"!<arch>\n" in data[1:]:
        probs.append("carries an object archive inside it")
    return probs


def read_file(path, limit=1 << 30):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode):
            raise OSError(errno.EINVAL, "not a regular file")
        chunks, left = [], min(st.st_size, limit)
        while left > 0:
            b = os.read(fd, min(left, 1 << 22))
            if not b:
                break
            chunks.append(b)
            left -= len(b)
        return b"".join(chunks)
    finally:
        os.close(fd)


# --------------------------------------------------------------------------
# workspace scans
# --------------------------------------------------------------------------

def walk_files(top):
    for root, dirs, files in os.walk(top):
        dirs[:] = [d for d in dirs if not os.path.islink(os.path.join(root, d))]
        for name in files:
            yield os.path.join(root, name)


def cmd_scrub(args):
    removed = []
    for path in walk_files(args.dir):
        try:
            st = os.lstat(path)
            if not stat.S_ISREG(st.st_mode):
                continue
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            try:
                head = os.read(fd, 8)
            finally:
                os.close(fd)
            if any(head.startswith(m) for m in COMPILED_MAGIC):
                os.unlink(path)
                removed.append(os.path.relpath(path, args.dir))
        except OSError:
            continue
    say("SCRUB removed %d compiled artifact(s) the workspace arrived with%s"
        % (len(removed), (": " + ", ".join(clean(r) for r in removed[:12])
                          + (" ..." if len(removed) > 12 else "")) if removed else ""))
    return 0


def source_problems(tree):
    probs = []
    for path in walk_files(tree):
        name = os.path.basename(path)
        if not (name.endswith(SOURCE_SUFFIXES) or name in SOURCE_NAMES):
            continue
        try:
            data = read_file(path)
        except OSError:
            continue
        if data[:4] == b"\x7fELF":
            continue
        for m in ENGINE_MARKERS + SOURCE_ONLY_MARKERS:
            if m in data:
                probs.append("%s contains %r" % (clean(os.path.relpath(path, tree)),
                                                  m.decode("latin1")))
    return probs


def cmd_check(args):
    probs = []
    try:
        st = os.lstat(args.binary)
    except OSError as exc:
        say("PROVENANCE FAIL: %s: %s" % (clean(args.binary), exc))
        return 3
    if stat.S_ISLNK(st.st_mode) or not stat.S_ISREG(st.st_mode):
        probs.append("%s is not a regular file" % clean(args.binary))
    else:
        probs += ["deliverable: " + p for p in elf_problems(read_file(args.binary))]
    probs += ["source: " + p for p in source_problems(args.tree)]
    if probs:
        for p in probs[:20]:
            say("PROVENANCE FAIL: " + p)
        return 3
    say("PROVENANCE OK: the deliverable is a native C executable linking only the "
        "C library; no foreign runtime, embedded image or engine identifier found")
    return 0


# --------------------------------------------------------------------------
# processes
# --------------------------------------------------------------------------

_libc = ctypes.CDLL(None, use_errno=True)


def candidate_ids(user):
    pw = pwd.getpwnam(user)
    return pw.pw_uid, pw.pw_gid, pw.pw_dir


def procs_of(uid):
    """pids whose real or effective uid is `uid`."""
    out = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open("/proc/%s/status" % entry) as fh:
                for line in fh:
                    if line.startswith("Uid:"):
                        ids = line.split()[1:3]
                        if str(uid) in ids:
                            out.append(int(entry))
                        break
        except (OSError, ValueError):
            continue
    return out


def sweep(uid, gid, rounds=60):
    """Kill every process of `uid`. kill(-1) from inside that uid reaches all of
    them at once, which a pid-by-pid loop racing a forking process does not."""
    for _ in range(rounds):
        if not procs_of(uid):
            return True
        pid = os.fork()
        if pid == 0:
            try:
                os.setgroups([])
                os.setgid(gid)
                os.setuid(uid)
                os.kill(-1, signal.SIGKILL)
            finally:
                os._exit(0)
        os.waitpid(pid, 0)
        time.sleep(0.1)
    return not procs_of(uid)


def cmd_sweep(args):
    uid, gid, _ = candidate_ids(args.user)
    ok = sweep(uid, gid)
    if not ok:
        say("SWEEP processes of %s survived 60 rounds: %s" % (args.user, procs_of(uid)[:10]))
    return 0


def system_file(path):
    """A root-owned file nobody else can write, outside every candidate-writable tree."""
    if path.startswith(CANDIDATE_WRITABLE):
        return False
    try:
        st = os.stat(path)
    except OSError:
        return False
    return (stat.S_ISREG(st.st_mode) and st.st_uid == 0
            and not st.st_mode & (stat.S_IWGRP | stat.S_IWOTH))


def serve_library(path):
    """What the remote end may map executable from the system while it serves:
    the C library, and the pieces glibc itself loads on demand."""
    base = os.path.basename(path)
    return base in C_LIBRARY or base.startswith("libnss_") or "/gconv/" in path


class Monitor(threading.Thread):
    """Samples every process of the candidate uid until stopped.

    mode "build": each program executed and each file mapped executable must be
    a system file or a C program that elf_problems() accepts.
    mode "serve": each process must be the executable whose (st_dev, st_ino) is
    `serve_ident`, mapping executable nothing but that file and system files.
    """

    def __init__(self, uid, gid, mode, serve_ident=None, interval=0.02):
        super().__init__(daemon=True)
        self.uid, self.gid, self.mode, self.serve_ident = uid, gid, mode, serve_ident
        self.interval = interval
        self.halt = threading.Event()
        self.violations = []
        self.samples = 0
        self.inspected = set()
        self.seen_ident = 0
        self.error = None
        self.fsuid_ok = False
        self._verdicts = {}

    # -- per-thread identity ------------------------------------------------
    def _become_inspector(self):
        _libc.setfsgid(self.gid)
        _libc.setfsuid(self.uid)
        # setfsuid returns the PREVIOUS value; asking again confirms the change.
        self.fsuid_ok = (_libc.setfsuid(self.uid) == self.uid
                         and _libc.setfsgid(self.gid) == self.gid)

    def _flag(self, msg):
        if msg not in self.violations and len(self.violations) < 50:
            self.violations.append(msg)

    def _file_ok(self, path, opener):
        """Verdict for an executed or executably mapped file, cached by identity."""
        if system_file(path):
            return None
        try:
            fd = os.open(opener, os.O_RDONLY)
            try:
                st = os.fstat(fd)
                key = (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns)
                if key in self._verdicts:
                    return self._verdicts[key]
                chunks = []
                while True:
                    b = os.read(fd, 1 << 22)
                    if not b:
                        break
                    chunks.append(b)
            finally:
                os.close(fd)
        except OSError as exc:
            return "cannot be read (%s)" % exc.strerror
        probs = elf_problems(b"".join(chunks), want_exec=False)
        verdict = ("; ".join(probs[:3])) if probs else None
        self._verdicts[key] = verdict
        return verdict

    def _inspect(self, pid):
        base = "/proc/%d" % pid
        try:
            exe = os.readlink(base + "/exe")
            exe_st = os.stat(base + "/exe")
            with open(base + "/maps") as fh:
                maps = fh.read()
        except FileNotFoundError:
            return            # exited, or a zombie
        except PermissionError:
            time.sleep(0.05)
            if os.path.exists(base):
                try:
                    os.readlink(base + "/exe")
                except PermissionError:
                    self._flag("process %d cannot be inspected (it made itself "
                               "non-dumpable or changed identity)" % pid)
                except OSError:
                    pass
            return
        except OSError:
            return
        self.inspected.add(pid)
        if self.mode == "serve":
            if (exe_st.st_dev, exe_st.st_ino) == self.serve_ident:
                self.seen_ident += 1
            else:
                self._flag("process %d runs %s, not the remote end that was built "
                           "and checked" % (pid, clean(exe)))
        else:
            why = self._file_ok(exe.replace(" (deleted)", ""), base + "/exe")
            if why or exe.endswith(" (deleted)"):
                self._flag("the build executed %s: %s" % (clean(exe), why or "deleted file"))
        for line in maps.splitlines():
            parts = line.split(None, 5)
            if len(parts) < 2 or "x" not in parts[1]:
                continue
            path = parts[5] if len(parts) == 6 else ""
            if path.startswith("["):
                continue
            if not path:
                if self.mode == "serve":
                    self._flag("process %d executes anonymous memory (code that was "
                               "not loaded from a file)" % pid)
                continue
            if path.endswith(" (deleted)") or path.startswith("/memfd:"):
                self._flag("process %d executes code from %s" % (pid, clean(path)))
                continue
            if self.mode == "serve":
                try:
                    st = os.stat(path)
                    if (st.st_dev, st.st_ino) == self.serve_ident:
                        continue
                except OSError:
                    pass
                if system_file(path):
                    if not serve_library(path):
                        self._flag("process %d maps %s executable, a system library "
                                   "that is not part of the C library" % (pid, clean(path)))
                    continue
            why = self._file_ok(path, path)
            if why:
                self._flag("process %d maps %s executable: %s" % (pid, clean(path), why))

    def sample(self):
        for pid in procs_of(self.uid):
            self._inspect(pid)
        self.samples += 1

    def run(self):
        try:
            self._become_inspector()
            while not self.halt.is_set():
                self.sample()
                self.halt.wait(self.interval)
            self.sample()
        except Exception as exc:  # recorded, and treated as a failed check
            self.error = "%s: %s" % (type(exc).__name__, exc)

    def stop(self):
        self.halt.set()
        self.join(timeout=30)


def monitor_selftest(uid, gid, user):
    """Positive arm: the monitor must be able to read a process of the candidate
    uid whose program is known. False means this platform does not allow it."""
    probe = subprocess.Popen(["runuser", "-u", user, "--", "sleep", "5"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(0.5)
        known = os.stat("/usr/bin/sleep")
        m = Monitor(uid, gid, "serve", serve_ident=(known.st_dev, known.st_ino))
        t = threading.Thread(target=lambda: (m._become_inspector(), m.sample()))
        t.start()
        t.join(timeout=20)
        return m.fsuid_ok and m.seen_ident > 0 and not m.violations
    finally:
        probe.kill()
        probe.wait()
        sweep(uid, gid)


# --------------------------------------------------------------------------
# the build
# --------------------------------------------------------------------------

def no_network_filter():
    """A seccomp program refusing socket(AF_INET/AF_INET6), io_uring and the
    other syscall ABIs. Building a C program needs none of them."""
    LD, JEQ, JGE, RET = 0x20, 0x15, 0x35, 0x06
    ALLOW, DENY = 0x7FFF0000, 0x00050000 | errno.EACCES
    prog = [
        (LD, 0, 0, 4),                    # 0  A = arch
        (JEQ, 0, 9, 0xC000003E),          # 1  x86_64 ? next : deny
        (LD, 0, 0, 0),                    # 2  A = nr
        (JGE, 7, 0, 0x40000000),          # 3  x32 ABI -> deny
        (JEQ, 0, 4, 41),                  # 4  socket ? next : 9
        (LD, 0, 0, 16),                   # 5  A = domain
        (JEQ, 4, 0, 2),                   # 6  AF_INET -> deny
        (JEQ, 3, 0, 10),                  # 7  AF_INET6 -> deny
        (RET, 0, 0, ALLOW),               # 8
        (JEQ, 1, 0, 425),                 # 9  io_uring_setup -> deny
        (RET, 0, 0, ALLOW),               # 10
        (RET, 0, 0, DENY),                # 11
    ]
    buf = b"".join(struct.pack("<HBBI", *ins) for ins in prog)
    return buf, len(prog)


_FILTER = no_network_filter()


def install_no_network():
    """Called in the child before exec. Returns silently on a platform that
    refuses seccomp; the parent learns which from the marker it then checks."""
    buf, n = _FILTER
    fbuf = ctypes.create_string_buffer(buf, len(buf))

    class SockFprog(ctypes.Structure):
        _fields_ = [("len", ctypes.c_ushort), ("filter", ctypes.c_void_p)]

    prog = SockFprog(n, ctypes.cast(fbuf, ctypes.c_void_p))
    PR_SET_NO_NEW_PRIVS, PR_SET_SECCOMP, SECCOMP_MODE_FILTER = 38, 22, 2
    if _libc.prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0:
        return False
    return _libc.prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, ctypes.byref(prog), 0, 0) == 0


def network_refused_probe(uid, gid):
    """Positive arm for the filter: a child that installs it and drops to the
    candidate uid must get EACCES from socket(AF_INET)."""
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(r)
        code = b"?"
        try:
            ok = install_no_network()
            os.setgroups([])
            os.setgid(gid)
            os.setuid(uid)
            if not ok:
                code = b"n"
            else:
                import socket
                try:
                    socket.socket(socket.AF_INET, socket.SOCK_STREAM).close()
                    code = b"o"
                except OSError:
                    code = b"r"
        finally:
            os.write(w, code)
            os._exit(0)
    os.close(w)
    got = os.read(r, 1)
    os.close(r)
    os.waitpid(pid, 0)
    return got == b"r"


def cmd_build(args):
    uid, gid, home = candidate_ids(args.user)
    sweep(uid, gid)
    netoff = network_refused_probe(uid, gid)
    say("BUILD outbound network for the build: %s"
        % ("refused (socket(AF_INET/AF_INET6) -> EACCES)" if netoff else
           "NOT refused: this platform did not accept the seccomp filter"))
    watch = monitor_selftest(uid, gid, args.user)
    say("BUILD process inspection: %s" % ("available" if watch else
        "UNAVAILABLE on this platform; the build is not watched"))

    def child():
        install_no_network()
        os.setgroups([])
        os.setgid(gid)
        os.setuid(uid)

    env = dict(os.environ, HOME=home, USER=args.user, LOGNAME=args.user)
    log = open(args.log, "wb")
    started = time.time()
    try:
        proc = subprocess.Popen(args.cmd, cwd="/", env=env, stdin=subprocess.DEVNULL,
                                stdout=log, stderr=log, preexec_fn=child,
                                start_new_session=True)
    except OSError as exc:
        say("BUILD could not start %s: %s" % (clean(" ".join(args.cmd)), exc))
        return 1
    mon = Monitor(uid, gid, "build") if watch else None
    if mon:
        mon.start()
    try:
        rc = proc.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except OSError:
            pass
        proc.wait()
        rc = 124
    if mon:
        mon.stop()
    sweep(uid, gid)
    log.close()
    say("BUILD rc=%d in %.1fs%s" % (rc, time.time() - started,
                                     " (TIMED OUT after %ds)" % args.timeout if rc == 124 else ""))
    if mon:
        say("BUILD watched %d sample(s), %d process(es)" % (mon.samples, len(mon.inspected)))
        problems = list(mon.violations)
        if mon.error:
            problems.append("the process monitor failed: %s" % mon.error)
        if not mon.fsuid_ok:
            problems.append("the process monitor could not take the candidate's identity")
        if problems:
            for v in problems[:20]:
                say("PROVENANCE FAIL: " + v)
            return 3
    return 0 if rc == 0 else 1


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd_name", required=True)
    s = sub.add_parser("scrub"); s.add_argument("dir"); s.set_defaults(fn=cmd_scrub)
    c = sub.add_parser("check"); c.add_argument("--binary", required=True)
    c.add_argument("--tree", required=True); c.set_defaults(fn=cmd_check)
    w = sub.add_parser("sweep"); w.add_argument("--user", default="grader")
    w.set_defaults(fn=cmd_sweep)
    b = sub.add_parser("build"); b.add_argument("--user", default="grader")
    b.add_argument("--timeout", type=int, required=True)
    b.add_argument("--log", required=True); b.add_argument("cmd", nargs="+")
    b.set_defaults(fn=cmd_build)
    args = ap.parse_args()
    sys.exit(args.fn(args))


if __name__ == "__main__":
    main()
