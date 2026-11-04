#!/usr/bin/env python3
"""Build, initialise, start and stop the candidate server.

Everything here goes through the interface the instruction names: `make` in the
workspace, `bin/initdb -D`, `bin/postgres -D -p -h -k`, and postgresql.conf.
"""
import errno, hashlib, os, signal, pathlib, re, shutil, signal, socket, stat, subprocess, sys, tempfile, time

PSQL = "/usr/lib/postgresql/18/bin/psql"
PSQL_OLD = "/usr/lib/postgresql/9.6/bin/psql"


def log(msg):
    print(msg, flush=True)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


UPSTREAM_MARKERS = [
    b"PostgreSQL Global Development Group",
    b"src/backend/tcop/postgres.c",
    b"PG_MODULE_MAGIC",
    b"pg_config_manual.h",
]
# Strings that only an upstream PostgreSQL *build* leaves inside a binary: its
# copyright banner, the name of its catalog bootstrap data file, the extension
# ABI macro, and one of its configure-time build flags.  None of these is text
# any milestone asks a candidate's own programs to print, so a hit here is
# evidence of vendored upstream code and stays fatal.
BINARY_MARKERS = [
    b"PostgreSQL Global Development Group",
    b"postgres.bki",
    b"PG_MODULE_MAGIC",
    b"--with-system-tzdata",
]
# Documented program OUTPUT, not source fingerprints: `pg_controldata`'s field
# label and `initdb`'s own progress line (with the conventional `progname: `
# prefix).  Reproducing that text is exactly what a faithful from-scratch
# implementation of these interfaces is asked to do, so a hit is not evidence of
# anything and must never zero a milestone.  Recorded as a report note instead.
BINARY_OUTPUT_MARKERS = [
    b"pg_control version number",
    b"initdb: initializing pg_authid",
]
# a build that fetches a server instead of compiling one
FETCH_MARKERS = ["apt-get install", "apt install", "postgresql-server",
                 "postgresql-common", "ftp.postgresql.org", "get-pgdg",
                 "apt.postgresql.org", "yum install", "dnf install"]
UPSTREAM_PATHS = [
    "src/backend/tcop/postgres.c", "src/backend/main/main.c",
    "src/include/pg_config_manual.h", "src/bin/psql/startup.c",
    "src/backend/executor/execMain.c", "configure.in", "configure.ac",
    "src/include/catalog/pg_proc.dat", "src/backend/catalog/genbki.pl",
    "src/backend/parser/gram.y", "src/include/postgres.h",
    "src/include/catalog/catversion.h",
]
# Upstream's own source-tree layout.  A from-scratch server keeps its code in a
# flat tree of its own; these directories are PostgreSQL's build structure, so a
# tree that carries them is a copy of PostgreSQL's source and not an
# implementation of this milestone.
UPSTREAM_DIRS = [
    "src/backend", "src/include/catalog", "src/backend/catalog",
    "src/backend/utils/adt", "src/common", "src/bin/psql", "contrib",
]
# Catalog bootstrap material only an upstream build ships: the `.bki` the
# backend reads at initdb and the `.dat` files its catalog is generated from.
# A `**`-glob so a rename of the directory does not hide them, and the basenames
# so a renamed file is still caught by what the build must read.
UPSTREAM_DATA_GLOBS = ["*.bki", "pg_*.dat", "postgres.bki"]


def clean_products(app):
    """Remove every build product before the scan and the build.

    The server the grader tests has to be built from the source it was given.
    Deleting the outputs first is what enforces that: a workspace that ships a
    compiled server beside source that does not build it has nothing left to run
    once this has swept the tree.  Any ELF file goes, wherever it is and whatever
    it is called -- a source tree has none -- along with `bin/` and the usual
    intermediates.
    """
    app = pathlib.Path(app)
    for pat in ("*.o", "*.d", "*.a", "*.so", "*.lo", "*.obj"):
        for f in app.rglob(pat):
            try:
                f.unlink()
            except OSError:
                pass
    for f in app.rglob("*"):
        if not f.is_file() or f.is_symlink():
            continue
        try:
            with f.open("rb") as fh:
                if fh.read(4) == b"\x7fELF":
                    f.unlink()
        except OSError:
            pass
    b = app / "bin"
    if b.is_dir():
        shutil.rmtree(b, ignore_errors=True)


# Read the WHOLE file for the banner, not just its first 4096 bytes: the banner
# sits in a file's header, but a vendored tree can pad 4096 bytes of comment
# ahead of it, and a single read window is exactly that blind spot.  Bounded so
# a pathological tree cannot run the verifier out of time: at most this many
# bytes per file, and this many in total.
SCAN_FILE_CAP = 4 * 1024 * 1024
SCAN_TOTAL_CAP = 512 * 1024 * 1024


def scan_tree(app):
    """Refuse pre-built programs and vendored upstream PostgreSQL source.

    The realistic cheat here is to vendor PostgreSQL's C source, because the
    products are deleted before every build and the server the grader runs has
    to be compiled from what is in the tree.  So this looks for upstream source,
    not only upstream binaries: its copyright banner (whole file, so padding
    cannot push it past a read window), its own build-tree layout, and the
    catalog bootstrap data an upstream build consumes.  None of these is
    anything a from-scratch implementation of this interface produces.
    """
    problems = []
    app = pathlib.Path(app)
    nmark = 0
    budget = SCAN_TOTAL_CAP
    for p in app.rglob("*"):
        if not p.is_file() or p.is_symlink():
            continue
        rel = str(p.relative_to(app))
        if rel.startswith(("pgdata/", ".git/", "bin/")):
            continue
        try:
            with p.open("rb") as fh:
                head = fh.read(4096)
                rest = fh.read(max(0, min(SCAN_FILE_CAP, budget) - len(head)))
        except OSError:
            continue
        data = head + rest
        budget -= len(data)
        if head[:4] == b"\x7fELF" and p.stat().st_mode & 0o111:
            problems.append("pre-built executable in the tree: %s" % rel)
        for m in UPSTREAM_MARKERS:
            if m in data:
                nmark += 1
                break
        if budget <= 0:
            break
    for up in UPSTREAM_PATHS:
        if (app / up).exists():
            problems.append("vendored upstream PostgreSQL source: %s" % up)
    for d in UPSTREAM_DIRS:
        if (app / d).is_dir():
            problems.append("vendored upstream PostgreSQL source tree: %s/" % d)
    for pat in UPSTREAM_DATA_GLOBS:
        for f in app.rglob(pat):
            rel = str(f.relative_to(app))
            if f.is_file() and not rel.startswith(("pgdata/", ".git/", "bin/")):
                problems.append("upstream PostgreSQL catalog bootstrap data: %s" % rel)
                break
    # A vendored tree carries the banner in hundreds of files, so any
    # surviving pair is already decisive; a from-scratch tree carries it in
    # none.
    if nmark >= 2:
        problems.append("vendored upstream PostgreSQL source: %d files carry its banner" % nmark)
    return problems


def scan_binaries(app):
    """After the build, the produced programs must be the candidate's own.

    Three things are checked.  Both programs must be ELF executables produced by
    the build -- a shell script that downloads and execs somebody else's server
    is not an implementation of this milestone -- and neither may carry a marker
    that only an upstream PostgreSQL *build* leaves behind.  Both must be C
    programs written against the C library alone (c_only_problems): linking
    nothing else, and carrying no other language's runtime.

    Strings that upstream merely PRINTS are not markers.  This milestone asks
    the candidate to reproduce PostgreSQL's observable behaviour, so its own
    output text cannot be the thing that convicts it; those strings live in
    `BINARY_OUTPUT_MARKERS` and are reported by `note_binaries`, not here.
    """
    problems = []
    for rel in ("bin/postgres", "bin/initdb"):
        # Must be a REGULAR file the build produced, or a link to one in bin/
        # (a multi-call program).  `f.exists()` is true for a FIFO the candidate
        # planted here, and a root `open().read()` on a FIFO never returns --
        # the candidate's own `make` wrote whatever is at this path, so this
        # read has to be guarded: program_file() and _read_regular_bytes() never
        # follow a link out of bin/ and never block.
        f = program_file(app, rel)
        blob = _read_regular_bytes(f) if f is not None else None
        if blob is None:
            problems.append("%s is not a regular executable produced by the build"
                            % rel)
            continue
        if blob[:4] != b"\x7fELF":
            head = blob[:80].decode("latin1", "replace").replace("\n", " ")
            problems.append("%s is not an executable produced by the build "
                            "(it begins %r)" % (rel, head))
            continue
        for m in BINARY_MARKERS:
            if m in blob:
                problems.append("%s carries an upstream PostgreSQL marker: %s"
                                % (rel, m.decode("latin1")))
                break
        problems += c_only_problems(rel, blob)
    return problems


def note_binaries(app):
    """Documented upstream OUTPUT strings found in the produced programs.

    Purely informational.  `pg_control version number` is a `pg_controldata`
    field label and `initdb: initializing pg_authid` is `initdb`'s own progress
    line; a candidate that emits either is doing what the milestone asks of it.
    The note exists so the signal is still visible in the report, and so that
    nobody re-adds these to `BINARY_MARKERS` on the theory that nothing looks at
    them.
    """
    notes = []
    for rel in ("bin/postgres", "bin/initdb"):
        f = program_file(app, rel)       # regular file only; a FIFO would block
        blob = _read_regular_bytes(f) if f is not None else None
        if blob is None:
            continue
        if blob[:4] != b"\x7fELF":
            continue
        for m in BINARY_OUTPUT_MARKERS:
            if m in blob:
                notes.append("%s reproduces the documented PostgreSQL output "
                             "string %r (allowed; recorded only)"
                             % (rel, m.decode("latin1")))
    return notes


# ------------------------------------------------------------------ C only
# Both programs are C, written against the C library alone (instruction,
# "Meta requirements"; SPEC/INTERFACE.md, "The programs").  Every check above
# asks only that bin/postgres be an ELF the build produced and that it be what
# runs, and a ~30-line C launcher that dlopen()s libpython and hands the
# process to a server written in Python is both.  What follows closes that
# route four ways: what the
# two programs link, what their bytes carry, what the build left in the tree,
# and -- after the build -- what the server's user can still load or run.
#
# glibc's own libraries, gcc's support library and the dynamic loader.
ALLOWED_NEEDED = re.compile(
    r"^(libc|libm|libmvec|libpthread|libdl|librt|libutil|libresolv|libanl|libgcc_s)"
    r"\.so(\.[0-9]+)*$"
    r"|^ld-linux[-A-Za-z0-9_.]*\.so(\.[0-9]+)*$")
# The program interpreter an ELF asks for, when it asks for one: the system's.
SYSTEM_LOADER = re.compile(
    r"^/(usr/)?lib(64|32|x32)?/([A-Za-z0-9_.-]+/)?ld-linux[-A-Za-z0-9_.]*\.so(\.[0-9]+)*$")
# Bytes only another language's runtime -- or a program that loads one by
# name -- carries.  None is anything a PostgreSQL server written in C prints
# or calls; a launcher that dlopen()s libpython carries the library's name and
# the embedding API's symbol names, and a copy of the runtime carries both.
RUNTIME_MARKERS = (
    (b"libpython", "the Python runtime library's name"),
    (b"Py_Initialize", "the Python embedding API"),
    (b"PyRun_", "the Python embedding API"),
    (b"Py_BytesMain", "the Python embedding API"),
    (b"PyInit_", "a Python extension module"),
    (b"libperl", "the Perl runtime library's name"),
    (b"perl_construct", "the Perl embedding API"),
    (b"libruby", "the Ruby runtime library's name"),
    (b"ruby_setup", "the Ruby embedding API"),
    (b"libjvm", "the Java runtime library's name"),
    (b"JNI_CreateJavaVM", "the Java invocation API"),
    (b"libnode", "the Node.js runtime library's name"),
    (b"libmono", "the Mono runtime library's name"),
    (b"mono_jit_init", "the Mono embedding API"),
    (b"libcoreclr", "the .NET runtime library's name"),
    (b"coreclr_initialize", "the .NET hosting API"),
    (b"liblua", "the Lua runtime library's name"),
    (b"luaL_newstate", "the Lua embedding API"),
    (b"libtcl", "the Tcl runtime library's name"),
    (b"Tcl_CreateInterp", "the Tcl embedding API"),
    (b"libphp", "the PHP runtime library's name"),
    (b"php_embed_init", "the PHP embedding API"),
    (b"\xff Go buildinf:", "a Go runtime"),
    (b"runtime.gopanic", "a Go runtime"),
    (b"library/std/src/", "a Rust runtime"),
)
_MARKER_OVERLAP = max(len(m) for m, _ in RUNTIME_MARKERS)
# Shared objects that are another language's runtime, by name.
RUNTIME_LIBS = ("libpython", "libperl", "libruby", "libjvm", "libnode", "libmono",
                "libcoreclr", "libclrjit", "liblua", "libluajit", "libtcl", "libphp",
                "libjulia", "libguile", "libmozjs", "libv8")
# Programs that are another language's interpreter, by command name.
INTERPRETER_COMM = re.compile(
    r"^(python|pypy|perl|ruby|node|nodejs|java|php|lua|luajit|tclsh|wish|guile|"
    r"julia|Rscript|dotnet|mono|erl|beam\.smp|deno|bun)[0-9.]*"
    r"(-[A-Za-z0-9_.-]*)?$")
# The image's own files, which nothing the candidate runs can write.
SYSTEM_PREFIXES = ("/usr/", "/lib/", "/lib64/", "/lib32/", "/libx32/")
# Interpreters and runtimes the verifier image carries, for its own use (the
# scorer and its clients are Python, run as root).  After the build they are
# taken away from the server's user, so neither program can start one or load
# one in-process, whatever name it constructs at run time.  Only the bits for
# "other" go: root owns them and keeps them.
FOREIGN_RUNTIME_GLOBS = (
    "/usr/bin/python*", "/usr/bin/pypy*", "/usr/bin/perl*", "/usr/bin/ruby*",
    "/usr/bin/node*", "/usr/bin/php*", "/usr/bin/lua*", "/usr/bin/tclsh*",
    "/usr/bin/java", "/usr/local/bin/python*",
    "/usr/lib/*/libpython*", "/usr/lib/*/libperl*", "/usr/lib/*/libruby*",
    "/usr/lib/*/liblua*", "/usr/lib/*/libtcl*", "/usr/lib/*/libphp*",
    "/usr/lib/python3*", "/usr/local/lib/python3*", "/usr/lib/*/perl*",
    "/usr/share/perl*", "/usr/lib/jvm",
)
PROGRAM_READ_CAP = 1024 * 1024 * 1024       # a few MB is what a real build makes
TREE_ELF_SCAN_CAP = 2 * 1024 * 1024 * 1024  # total bytes read by the tree sweep


def program_file(app, rel):
    """The regular file `rel` (bin/postgres or bin/initdb) is, or None.

    Either program may be a symbolic link to another program in `bin/`: one
    multi-call binary that looks at argv[0], the way `postmaster` was a link to
    `postgres` until 16.  What is then checked, hashed and run is the file the
    link names.  A link is followed one step, and only to a name in `bin/`
    itself: a link out of `bin/`, a link to a link, a FIFO or a directory is
    not a program the build produced.
    """
    app = pathlib.Path(app)
    bindir = pathlib.Path(os.path.normpath(str(app / "bin")))
    if os.path.islink(str(bindir)):
        return None
    f = app / rel
    try:
        st = os.lstat(str(f))
    except OSError:
        return None
    if stat.S_ISLNK(st.st_mode):
        try:
            tgt = os.readlink(str(f))
        except OSError:
            return None
        t = pathlib.Path(os.path.normpath(os.path.join(str(f.parent), tgt)))
        if t.parent != bindir:
            return None
        try:
            st = os.lstat(str(t))
        except OSError:
            return None
        f = t
    if not stat.S_ISREG(st.st_mode):
        return None
    return f


def _read_regular_bytes(path, cap=PROGRAM_READ_CAP):
    """The bytes of regular file `path`, read without following a final link
    or blocking, or None if it is anything else or larger than `cap`."""
    try:
        fd = os.open(str(path), os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError:
        return None
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_size > cap:
            return None
        os.set_blocking(fd, True)
        chunks, left = [], cap + 1
        while left > 0:
            c = os.read(fd, min(1 << 20, left))
            if not c:
                break
            chunks.append(c)
            left -= len(c)
        data = b"".join(chunks)
        return data if len(data) <= cap else None
    except OSError:
        return None
    finally:
        os.close(fd)


def elf_link_info(blob):
    """(program interpreter or None, [DT_NEEDED names]) of an ELF image, read
    from its program headers and dynamic section; None if the bytes are not an
    ELF file this can read.  Nothing is executed: `ldd` would run the binary's
    own PT_INTERP."""
    import struct
    if len(blob) < 64 or blob[:4] != b"\x7fELF":
        return None
    cls, data = blob[4], blob[5]
    if cls not in (1, 2) or data not in (1, 2):
        return None
    e = "<" if data == 1 else ">"
    try:
        if cls == 2:
            phoff = struct.unpack_from(e + "Q", blob, 0x20)[0]
            phentsize, phnum = struct.unpack_from(e + "HH", blob, 0x36)
            ph_fmt, dyn_fmt = e + "IIQQQQQQ", e + "qQ"
        else:
            phoff = struct.unpack_from(e + "I", blob, 0x1C)[0]
            phentsize, phnum = struct.unpack_from(e + "HH", blob, 0x2A)
            ph_fmt, dyn_fmt = e + "IIIIIIII", e + "iI"
        if phnum and phentsize < struct.calcsize(ph_fmt):
            return None
        loads, dyn, interp = [], None, None
        for i in range(min(phnum, 65535)):
            ph = struct.unpack_from(ph_fmt, blob, phoff + i * phentsize)
            if cls == 2:
                ptype, off, vaddr, filesz = ph[0], ph[2], ph[3], ph[5]
            else:
                ptype, off, vaddr, filesz = ph[0], ph[1], ph[2], ph[4]
            if ptype == 1:                          # PT_LOAD
                loads.append((vaddr, off, filesz))
            elif ptype == 2:                        # PT_DYNAMIC
                dyn = (off, filesz)
            elif ptype == 3:                        # PT_INTERP
                interp = blob[off:off + filesz].split(b"\0")[0].decode("latin1")
        needed_offs, strtab = [], None
        if dyn is not None:
            step = struct.calcsize(dyn_fmt)
            off, size = dyn
            for j in range(max(0, min(size, len(blob) - off)) // step):
                tag, val = struct.unpack_from(dyn_fmt, blob, off + j * step)
                if tag == 0:                        # DT_NULL
                    break
                if tag == 1:                        # DT_NEEDED
                    needed_offs.append(val)
                elif tag == 5:                      # DT_STRTAB
                    strtab = val
        needed = []
        if needed_offs:
            base = None
            for vaddr, off, filesz in loads:
                if strtab is not None and vaddr <= strtab < vaddr + filesz:
                    base = strtab - vaddr + off
                    break
            if base is None:
                return None
            for o in needed_offs:
                needed.append(blob[base + o:base + o + 4096].split(b"\0")[0]
                              .decode("latin1"))
        return interp, needed
    except struct.error:
        return None


def runtime_marker(blob):
    """What another language's runtime this byte string carries, or None."""
    for m, what in RUNTIME_MARKERS:
        if m in blob:
            return "%s (%r)" % (what, m.decode("latin1"))
    return None


_FILE_MARKER_CACHE = {}


def file_runtime_marker(path, budget=None):
    """runtime_marker() over the regular file at `path`, read in bounded
    chunks without following a final link or blocking; None if it carries
    none or is not a regular file.  `budget` is a one-element list of bytes
    left to read, shared by a caller's whole sweep."""
    try:
        fd = os.open(str(path), os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError:
        return None
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode):
            return None
        key = (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns)
        if key in _FILE_MARKER_CACHE:
            return _FILE_MARKER_CACHE[key]
        os.set_blocking(fd, True)
        found, tail, left = None, b"", st.st_size
        while found is None and left > 0:
            if budget is not None and budget[0] <= 0:
                break
            c = os.read(fd, min(1 << 20, left))
            if not c:
                break
            left -= len(c)
            if budget is not None:
                budget[0] -= len(c)
            found = runtime_marker(tail + c)
            tail = c[-_MARKER_OVERLAP:]
        _FILE_MARKER_CACHE[key] = found
        return found
    except OSError:
        return None
    finally:
        os.close(fd)


def c_only_problems(rel, blob):
    """Why the program `rel`, whose bytes are `blob`, is not a C program
    written against the C library alone; [] if it is."""
    info = elf_link_info(blob)
    if info is None:
        return ["%s is not an ELF executable whose links can be read" % rel]
    interp, needed = info
    out = []
    if interp is not None and not SYSTEM_LOADER.match(interp):
        out.append("%s asks for the program interpreter %r, not the system's "
                   "dynamic loader" % (rel, interp))
    bad = [n for n in needed if not ALLOWED_NEEDED.match(n)]
    if bad:
        out.append("%s links libraries beyond the C library: %s"
                   % (rel, ", ".join(bad)))
    why = runtime_marker(blob)
    if why:
        out.append("%s carries %s: the server has to be a C program, not a "
                   "host for another language's runtime" % (rel, why))
    return out


def tree_runtime_problems(app):
    """Every ELF file the build left anywhere in the tree -- a shared library
    in bin/, an extension module, a copied interpreter -- swept for another
    language's runtime.  The two programs are checked by scan_binaries."""
    app = pathlib.Path(app)
    skip = set()
    for rel in ("bin/postgres", "bin/initdb"):
        f = program_file(app, rel)
        if f is not None:
            skip.add(str(f))
    problems = []
    budget = [TREE_ELF_SCAN_CAP]
    for dp, dirs, files in os.walk(str(app), followlinks=False):
        rel_dp = os.path.relpath(dp, str(app))
        if rel_dp.split(os.sep)[0] in (".git", "pgdata"):
            dirs[:] = []
            continue
        for name in files:
            p = os.path.join(dp, name)
            if p in skip or os.path.islink(p):
                continue
            try:
                fd = os.open(p, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            except OSError:
                continue
            try:
                if not stat.S_ISREG(os.fstat(fd).st_mode) or os.read(fd, 4) != b"\x7fELF":
                    continue
            except OSError:
                continue
            finally:
                os.close(fd)
            why = file_runtime_marker(p, budget)
            if why:
                problems.append("%s carries %s: nothing the build leaves may be "
                                "another language's runtime"
                                % (os.path.relpath(p, str(app)), why))
                if len(problems) >= 8:
                    return problems
        if budget[0] <= 0:
            break
    return problems


def lock_foreign_runtimes():
    """Take the image's interpreters and runtimes away from the server's user.

    Run after the build (a Makefile may use the image's tools to generate C)
    and before either program starts.  Root keeps them: it owns them, and only
    the bits for "other" are cleared.  Returns the paths changed."""
    import glob
    if not server_user_available():
        return []
    done = []
    for pat in FOREIGN_RUNTIME_GLOBS:
        for p in sorted(glob.glob(pat)):
            try:
                st = os.lstat(p)
            except OSError:
                continue
            if stat.S_ISLNK(st.st_mode) or st.st_uid != 0:
                continue
            try:
                os.chmod(p, stat.S_IMODE(st.st_mode) & ~0o007)
                done.append(p)
            except OSError:
                pass
    return done


def _server_user_pids():
    """Live processes whose real or effective uid is the server's."""
    out = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open("/proc/%s/status" % entry) as fh:
                st = fh.read()
        except OSError:
            continue
        state = uid = None
        for ln in st.splitlines():
            if ln.startswith("State:"):
                state = ln.split()[1:2]
            elif ln.startswith("Uid:"):
                uid = ln.split()[1:3]
        if not uid or (state and state[0] in ("Z", "X")):
            continue
        if str(SERVER_UID) in uid:
            out.append(int(entry))
    return out


def _proc_text(pid, name):
    """/proc/<pid>/<name>, read as root or, where root may not read another
    uid's process, as the server's own uid; None if neither can."""
    path = "/proc/%d/%s" % (pid, name)
    try:
        with open(path) as fh:
            return fh.read()
    except OSError:
        pass
    if not server_user_available():
        return None
    try:
        r = subprocess.run(["setpriv", "--reuid", str(SERVER_UID), "--regid",
                            str(SERVER_GID), "--clear-groups", "cat", path],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                           timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    if r.returncode != 0:
        return None
    return r.stdout.decode("utf-8", "replace")


def foreign_runtime_running(proc):
    """Is anything running as the server's user another language's runtime?

    Every live process of that uid (the server, anything it forked, anything
    the build left behind) by command name, and the server and each of them
    by what it has mapped executable: a runtime library by name, an anonymous
    or deleted file, or any file outside the image's own directories that
    carries a runtime's bytes.  The server's own map has to be readable."""
    problems = []
    pids = set(_server_user_pids())
    main_pid = proc.pid if (proc is not None and proc.poll() is None) else None
    if main_pid is not None:
        pids.add(main_pid)
    for pid in sorted(pids):
        comm = (_proc_text(pid, "comm") or "").strip()
        if comm and INTERPRETER_COMM.match(comm):
            problems.append("`%s` (pid %d) is running as the server's user" % (comm, pid))
            continue
        maps = _proc_text(pid, "maps")
        if maps is None:
            if pid == main_pid and proc.poll() is None:
                problems.append("could not read /proc/%d/maps of the server" % pid)
            continue
        for line in maps.splitlines():
            parts = line.split(None, 5)
            if len(parts) < 6 or "x" not in parts[1]:
                continue
            path = parts[5].strip()
            if not path.startswith("/"):
                continue
            base = os.path.basename(path.replace(" (deleted)", ""))
            if base.startswith(RUNTIME_LIBS):
                problems.append("pid %d (%s) has %s mapped" % (pid, comm, path))
            elif path.startswith("/memfd:") or path.endswith("(deleted)"):
                problems.append("pid %d (%s) runs code from %s" % (pid, comm, path))
            elif not path.startswith(SYSTEM_PREFIXES):
                why = file_runtime_marker(path)
                if why:
                    problems.append("pid %d (%s) has %s mapped, which carries %s"
                                    % (pid, comm, path, why))
        if len(problems) >= 8:
            break
    return problems


def server_uid(proc):
    """The uid the server process is ACTUALLY running as.

    Everything else about the privilege drop is indirect. `probe_expectation_
    access` runs its own `setpriv` as uid 4242 and reports what that user can
    read, which is true whatever the server is running as -- so if the drop
    silently became a no-op, that field would still say "no" and nothing would
    say otherwise.  This reads the server's own `/proc/<pid>/status`, so a green
    gate asserts the property rather than implying it."""
    if proc is None or proc.poll() is not None:
        return "the server is not running"
    try:
        for line in pathlib.Path("/proc/%d/status" % proc.pid).read_text().splitlines():
            if line.startswith("Uid:"):
                real, eff = line.split()[1:3]
                if not server_user_available():
                    return ("uid %s (no unprivileged user in this image, so the "
                            "drop is a no-op by design)" % eff)
                if int(eff) != SERVER_UID or int(real) != SERVER_UID:
                    return ("NOT DROPPED: real uid %s, effective uid %s, expected "
                            "%d" % (real, eff, SERVER_UID))
                return "uid %d" % SERVER_UID
    except OSError as exc:
        return "unreadable: %s" % exc
    return "no Uid line in /proc/<pid>/status"


def running_program(proc, app):
    """What is actually running after start(), resolved through /proc.

    `bin/postgres` being the candidate's own ELF is not enough on its own: a
    program that execs another one leaves the parent's path behind.  The server
    the grader talks to has to still be the program the grader started.
    """
    want = (pathlib.Path(app) / "bin/postgres").resolve()
    link = "/proc/%d/exe" % proc.pid
    got = None
    try:
        got = pathlib.Path(link).resolve()
    except OSError:
        got = None
    if got is None and server_user_available():
        # The server runs as an unprivileged user and this container does not
        # give root ptrace; a reader of the same uid may still follow the link.
        r = subprocess.run(["setpriv", "--reuid", str(SERVER_UID), "--regid",
                            str(SERVER_GID), "--clear-groups", "readlink", "-f", link],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        if r.returncode == 0 and r.stdout.strip():
            got = pathlib.Path(r.stdout.decode().strip())
    if got is None:
        return ["could not read %s" % link]
    if got != want:
        return ["the running server is %s, not the %s that `make` produced"
                % (got, want)]
    return []


# What the candidate's build is allowed to see.  It inherited the verifier's
# whole environment before, which on a cluster means service-discovery variables
# and whatever else the pod was given -- none of it answer-bearing here, but a
# build is the candidate's code running, and what it is handed should be a
# decision rather than an accident.
BUILD_ENV_KEEP = ("PATH", "HOME", "LANG", "LC_ALL", "TZ", "TERM", "SHELL",
                  "MAKEFLAGS", "CC", "CFLAGS", "LDFLAGS", "TMPDIR")


def build_env():
    env = {k: v for k, v in os.environ.items() if k in BUILD_ENV_KEEP}
    env.setdefault("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
    env.setdefault("LC_ALL", "C")
    env.setdefault("TZ", "UTC")
    return env


def dropped_build_vars():
    """The names -- never the values -- of what the build does not get."""
    return sorted(k for k in os.environ if k not in BUILD_ENV_KEEP)


def build(app):
    """Build the candidate's server AS THE UNPRIVILEGED USER.

    `make` runs instructions the candidate wrote, which makes it the most
    privileged candidate-authored process in the verifier -- and it does not
    read like one, so it is easy to leave as root.  A root `make` can read
    `/tests/corpus/expected-r*/` and print the answers into its own source, or
    rewrite `/tests/runner/score.py` outright.  The server dropping privileges
    afterwards does not help: by then the build has already run.

    So the workspace is handed to the server user first and `make` is dropped
    the same way the server is.  HOME goes to a writable directory because the
    verifier's own HOME is not one for this uid.
    """
    clean_products(app)
    env = build_env()
    if server_user_available():
        _own_for_server(app)
        env["HOME"] = "/tmp"
    # Its own session, so anything make leaves behind -- a detached grandchild
    # waiting to overwrite the binaries after they are scanned -- can be reaped
    # by process GROUP.  This kills a group this function created, by id; it is
    # not a pattern match (fleet advisory 20).
    def _build_child():
        os.setsid()
        if server_user_available():
            _limit_child()
    proc = subprocess.Popen(["make", "-C", str(app)], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env,
                            preexec_fn=_build_child)
    try:
        stdout, _ = proc.communicate(timeout=900)
    except subprocess.TimeoutExpired:
        # Kill the whole process GROUP first, then read with a bound.  A bare
        # `proc.kill()` reaps make but not a detached grandchild it left in the
        # same session, and that grandchild can keep make's stdout pipe open, so
        # an unbounded `communicate()` here would block until the step times out.
        # Killing the group make leads (os.setsid in _build_child) closes those
        # write ends; the bounded read then returns even if one lingers.
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except OSError:
            proc.kill()
        try:
            stdout, _ = proc.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            proc.kill()
            stdout = b""
    class _R:  pass
    r = _R(); r.returncode = proc.returncode; r.stdout = stdout
    try:
        os.killpg(proc.pid, signal.SIGKILL)      # the group make itself created
    except (ProcessLookupError, PermissionError, OSError):
        pass
    out = r.stdout.decode("utf-8", "replace")
    ok = (r.returncode == 0 and (pathlib.Path(app) / "bin/postgres").exists()
          and (pathlib.Path(app) / "bin/initdb").exists())
    low = out.lower()
    fetched = [m for m in FETCH_MARKERS if m in low]
    if fetched:
        out += "\n\npgvc: the build fetched or installed PostgreSQL itself: %s\n" % fetched
        ok = False
    return ok, out[-8000:]


# Exactly the hooks the instruction publishes: where to listen, where the data
# lives, and the determinism settings any client may set.
GUCS = [
    ("listen_addresses", "127.0.0.1"),
    ("timezone", "UTC"),
    ("datestyle", "ISO, MDY"),
    ("intervalstyle", "postgres"),
    ("lc_messages", "C"),
    ("client_min_messages", "notice"),
]

# Kept identical to the file each rung ships in its corpus (corpus/build.py),
# so the table described in INTERFACE.md, the one the grader writes here and the
# one a rung's own corpus carries are the same table.
HBA_DEFAULT = """# TYPE  DATABASE        USER            ADDRESS                 METHOD
local   all             all                                     trust
host    all             postgres        0.0.0.0/0            trust
host    all             pgvc_md5        0.0.0.0/0            md5
host    all             pgvc_reject     0.0.0.0/0            reject
host    all             all             0.0.0.0/0            trust
"""


# The grader starts every server under a hard address-space ceiling.  A correct
# implementation of this milestone needs a few dozen megabytes; the ceiling is
# there so that a server which sizes an allocation from a value it read off the
# wire dies by itself instead of taking the whole verifier down with it, which
# would look exactly like a failing test rather than like the bug it is.
SERVER_AS_LIMIT = 4 * 1024 * 1024 * 1024      # 4 GiB of address space
SERVER_CORE_LIMIT = 0                          # no core dumps

# The candidate's programs run as this user, never as root.  The scored
# expectations are root-owned and unreadable to it, which is what stops a
# "server" from finding the recorded output of the case being replayed and
# emitting that instead of computing anything.  The verifier image creates the
# user; where it does not exist the server runs as whoever the verifier is and
# score.py records that in the report rather than pretending otherwise.
SERVER_UID = 4242
SERVER_GID = 4242


def binary_digests(app):
    """sha256 of the two programs, taken at scan time.

    `scan_binaries` reads these files once, just after `make`.  Comparing the
    RESOLVED PATH of /proc/<pid>/exe against /app/bin/postgres afterwards proves
    nothing: a delayed writer can replace the file at that path -- and it can,
    because the workspace is handed to uid 4242 before initdb -- and both sides
    of a path comparison stay equal.  Bind the check to the bytes instead.
    """
    out = {}
    for rel in ("bin/postgres", "bin/initdb"):
        f = program_file(app, rel)       # regular file only; a FIFO would block
        blob = _read_regular_bytes(f) if f is not None else None
        out[rel] = hashlib.sha256(blob).hexdigest() if blob is not None else None
    return out


def running_binary_matches(proc, app, digests):
    """Is the program now running the one that was scanned?

    Reads /proc/<pid>/exe as a FILE -- that descriptor follows the inode the
    process actually execed, so replacing the path afterwards does not change
    what this reads."""
    if proc is None or proc.poll() is not None:
        return "the server is not running"
    want = digests.get("bin/postgres")
    if not want:
        return "bin/postgres was not hashed at scan time"
    link = "/proc/%d/exe" % proc.pid
    try:
        got = hashlib.sha256(pathlib.Path(link).read_bytes()).hexdigest()
    except OSError:
        if not server_user_available():
            return "unreadable: %s" % link
        r = subprocess.run(["setpriv", "--reuid", str(SERVER_UID), "--regid",
                            str(SERVER_GID), "--clear-groups", "cat", link],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        if r.returncode != 0:
            return "unreadable: %s" % link
        got = hashlib.sha256(r.stdout).hexdigest()
    if got != want:
        return ("REPLACED: the running program is not the one that was scanned "
                "(scanned %s, running %s)" % (want[:16], got[:16]))
    return "ok"


def server_user_available():
    try:
        import pwd
        pwd.getpwuid(SERVER_UID)
        return os.geteuid() == 0
    except (KeyError, ImportError, AttributeError):
        return False


def _limit_child():
    import resource
    try:
        resource.setrlimit(resource.RLIMIT_AS, (SERVER_AS_LIMIT, SERVER_AS_LIMIT))
    except (ValueError, OSError):
        pass
    try:
        resource.setrlimit(resource.RLIMIT_CORE, (SERVER_CORE_LIMIT, SERVER_CORE_LIMIT))
    except (ValueError, OSError):
        pass
    if server_user_available():
        try:
            os.setgroups([SERVER_GID])
            os.setgid(SERVER_GID)
            os.setuid(SERVER_UID)
        except OSError:
            pass


def allow_server_exec(app):
    """Let the server user read and run what the build produced.

    The candidate's workspace arrives with whatever permissions the artifact
    round trip gave it, and the server no longer runs as root, so `/app` and the
    programs under it have to be traversable and executable by uid 4242 or the
    server cannot start at all, failing with
    `PermissionError: /app/bin/initdb`.
    """
    app = pathlib.Path(app)
    try:
        os.chmod(app, os.stat(app).st_mode | 0o755)
    except OSError:
        pass
    # Never chmod THROUGH a candidate-planted symlink: os.chmod follows it to its
    # target, so a link from /app to a file outside the tree would hand the
    # candidate a root chmod of that target.  /app belongs to the server user by
    # now, so a check-then-chmod on a path is a race as well: the name can turn
    # into a link between the two.  os.fwalk only descends into directories it has
    # verified are not links, and each entry is changed through a descriptor
    # opened with O_NOFOLLOW, so a link is never followed, even one swapped in
    # mid-walk.  The build's own products are regular files and directories, so
    # skipping links costs the server nothing.
    for _dp, dirs, files, dfd in os.fwalk(str(app), follow_symlinks=False):
        for name in dirs + files:
            _add_mode_nofollow(name, dfd)


def _add_mode_nofollow(name, dir_fd):
    """Add read (and, for directories and owner-executables, execute) bits for
    everyone to `name` in `dir_fd`, through its own descriptor, never via a link."""
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=dir_fd)
    except OSError:
        return                                  # a link (ELOOP), a socket, or gone
    try:
        mode = os.fstat(fd).st_mode
        if stat.S_ISDIR(mode) or mode & 0o100:  # already executable by the owner
            os.fchmod(fd, stat.S_IMODE(mode) | 0o755)
        else:
            os.fchmod(fd, stat.S_IMODE(mode) | 0o444)
    except OSError:
        pass
    finally:
        os.close(fd)


def _chown_tree(top, dir_fd=None):
    """Give `top` and everything below it to the server user, never through a link.

    The server user owns these trees and can swap any name in them for a link at
    any moment, so a check-then-chown on paths is a race.  Every chown here is
    fchownat(AT_SYMLINK_NOFOLLOW) relative to a directory descriptor, and os.fwalk
    only descends into a directory it has verified is not a link.
    """
    try:
        st = os.stat(top, dir_fd=dir_fd, follow_symlinks=False)
    except OSError:
        return
    if stat.S_ISLNK(st.st_mode):
        return
    try:
        os.chown(top, SERVER_UID, SERVER_GID, dir_fd=dir_fd, follow_symlinks=False)
    except OSError:
        pass
    if not stat.S_ISDIR(st.st_mode):
        return
    for _dp, dirs, files, dfd in os.fwalk(top, dir_fd=dir_fd, follow_symlinks=False):
        for name in dirs + files:
            try:
                os.chown(name, SERVER_UID, SERVER_GID, dir_fd=dfd, follow_symlinks=False)
            except OSError:
                pass


# The data directory and its parent belong to the server user, which runs while
# root reads and writes the configuration there.  These two never follow a link
# in the last component, never block on a FIFO, and only ever touch a regular
# file; the directory they work in is a descriptor root opened itself.
CONF_READ_CAP = 4 * 1024 * 1024        # characters; initdb's postgresql.conf is ~30 KB


def _read_regular_text(name, dir_fd=None, cap=CONF_READ_CAP):
    """The text of regular file `name` (at most `cap` characters), or None if it
    is missing or anything but a regular file."""
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=dir_fd)
    except OSError:
        return None
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            os.close(fd)
            return None
        os.set_blocking(fd, True)
    except OSError:
        os.close(fd)
        return None
    with os.fdopen(fd, "r") as fh:
        return fh.read(cap)


def _write_regular_text(name, text, dir_fd=None):
    """Make `name` a regular file holding `text`, never writing through a link.

    Anything else at that name -- a link, a FIFO, a file with other hard links --
    is unlinked (which removes the name itself, never a link's target) and the
    file is created afresh with O_EXCL.
    """
    flags = os.O_WRONLY | os.O_NOFOLLOW | os.O_NONBLOCK
    fd = None
    try:
        fd = os.open(name, flags | os.O_CREAT, 0o644, dir_fd=dir_fd)
        st = os.fstat(fd)
        if not (stat.S_ISREG(st.st_mode) and st.st_nlink == 1):
            os.close(fd)
            fd = None
    except OSError:
        if fd is not None:
            os.close(fd)
        fd = None
    if fd is None:
        try:
            os.unlink(name, dir_fd=dir_fd)
        except FileNotFoundError:
            pass
        fd = os.open(name, flags | os.O_CREAT | os.O_EXCL, 0o644, dir_fd=dir_fd)
    try:
        os.set_blocking(fd, True)
        os.ftruncate(fd, 0)
    except OSError:
        os.close(fd)
        raise
    with os.fdopen(fd, "w") as fh:
        fh.write(text)


def _own_for_server(*paths):
    """Give the server user what it must write: its data directory and socket."""
    if not server_user_available():
        return
    for p in paths:
        p = pathlib.Path(p)
        p.mkdir(parents=True, exist_ok=True)
        # os.chown follows symlinks; the candidate's server (uid 4242) owns the
        # data directory and could plant a link here, so a link would chown its
        # target.  _chown_tree never follows one, even one swapped in mid-walk --
        # initdb's real output is files and dirs -- so root only chowns what is
        # really in the tree.
        _chown_tree(str(p))


class Server:
    def __init__(self, app, datadir, port=None, sockdir=None, extra_conf=(), hba=None):
        self.app = pathlib.Path(app)
        self.datadir = pathlib.Path(datadir)
        self.port = port or free_port()
        self.sockdir = pathlib.Path(sockdir or (str(datadir) + ".sock"))
        self.proc = None
        self.extra_conf = list(extra_conf)
        self.hba = hba or HBA_DEFAULT
        # The server's log lives in a directory of root's own, made here.  The
        # data directory's parent belongs to the server user, who could otherwise
        # put a link where root opens the log for appending and reads it back.
        self.logfile = (pathlib.Path(tempfile.mkdtemp(prefix="pgvc-log-"))
                        / (self.datadir.name + ".log"))
        # The data directory's parent, opened before it is handed to the server
        # user (initdb), so later work in the data directory starts from this
        # descriptor instead of from names the server user can replace.
        self._work_fd = None

    def initdb(self):
        if self.datadir.exists():
            shutil.rmtree(self.datadir, ignore_errors=True)
        self.sockdir.mkdir(parents=True, exist_ok=True)
        self.datadir.parent.mkdir(parents=True, exist_ok=True)
        if self._work_fd is None:
            self._work_fd = os.open(str(self.datadir.parent),
                                    os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        _own_for_server(self.datadir.parent, self.sockdir)
        allow_server_exec(self.app)
        r = subprocess.run([str(self.app / "bin/initdb"), "-D", str(self.datadir),
                            "-U", "postgres", "-E", "UTF8", "--no-locale",
                            "-A", "trust", "--no-sync"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300,
                           preexec_fn=_limit_child)
        return r.returncode == 0 and (self.datadir / "PG_VERSION").exists(), \
               r.stdout.decode("utf-8", "replace")[-4000:]

    def _datadir_fd(self):
        """The data directory, opened (O_DIRECTORY|O_NOFOLLOW) relative to the
        work directory descriptor taken before the server user owned it."""
        if self._work_fd is None:
            self._work_fd = os.open(str(self.datadir.parent),
                                    os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        return os.open(self.datadir.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                       dir_fd=self._work_fd)

    def write_hba(self):
        dfd = self._datadir_fd()
        try:
            _write_regular_text("pg_hba.conf", self.hba, dir_fd=dfd)
        finally:
            os.close(dfd)

    def write_conf(self):
        self.write_hba()
        dfd = self._datadir_fd()
        try:
            lines = []
            old = _read_regular_text("postgresql.conf", dir_fd=dfd)
            if old is not None:
                if len(old) >= CONF_READ_CAP:           # keep whole lines only
                    old = old[:old.rfind("\n") + 1]
                lines = [l for l in old.splitlines()
                         if not re.match(r"\s*(port|listen_addresses|unix_socket_directories)\s*=", l)]
            lines.append("port = %d" % self.port)
            lines.append("unix_socket_directories = '%s'" % self.sockdir)
            for k, v in GUCS + self.extra_conf:
                lines.append("%s = '%s'" % (k, v))
            _write_regular_text("postgresql.conf", "\n".join(lines) + "\n", dir_fd=dfd)
        finally:
            os.close(dfd)

    def _own_datadir_for_server(self):
        """_own_for_server(datadir, sockdir), relative to the held work directory."""
        if not server_user_available():
            return
        for d in (self.datadir, self.sockdir):
            if self._work_fd is None or d.parent != self.datadir.parent:
                _own_for_server(d)
                continue
            try:
                os.mkdir(d.name, dir_fd=self._work_fd)
            except FileExistsError:
                pass
            _chown_tree(d.name, dir_fd=self._work_fd)

    def start(self, timeout=60):
        self.write_conf()
        self._own_datadir_for_server()
        self.logfile.parent.mkdir(parents=True, exist_ok=True)
        f = os.fdopen(os.open(str(self.logfile),
                              os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW,
                              0o644), "ab")
        env = dict(os.environ)
        env.update(TZ="UTC", LC_ALL="C", LANG="C")
        self.proc = subprocess.Popen(
            [str(self.app / "bin/postgres"), "-D", str(self.datadir),
             "-p", str(self.port), "-h", "127.0.0.1", "-k", str(self.sockdir)],
            stdout=f, stderr=f, env=env, cwd=str(self.app),
            preexec_fn=_limit_child)
        end = time.time() + timeout
        while time.time() < end:
            if self.proc.poll() is not None:
                return False
            if self.ping():
                return True
            time.sleep(0.3)
        return False

    def ping(self):
        try:
            r = subprocess.run([PSQL, "-X", "-q", "-t", "-A", "-h", "127.0.0.1",
                                "-p", str(self.port), "-U", "postgres", "-d", "postgres",
                                "-c", "SELECT 1"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               timeout=15, env=dict(os.environ, PGCONNECT_TIMEOUT="10"))
            return r.returncode == 0 and b"1" in r.stdout
        except Exception:
            return False

    def stop(self):
        if not self.proc:
            return
        try:
            self.proc.send_signal(signal.SIGTERM)
            self.proc.wait(timeout=20)
        except Exception:
            try:
                self.proc.kill()
                self.proc.wait(timeout=10)
            except Exception:
                pass
        self.proc = None

    def tail_log(self, n=3000):
        """The last `n` characters of the server's log: read without following a
        link or blocking, regular file only, and at most its last 4*n bytes."""
        try:
            fd = os.open(str(self.logfile), os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except OSError:
            return ""
        try:
            st = os.fstat(fd)
            if not stat.S_ISREG(st.st_mode):
                return ""
            want = 4 * n
            if st.st_size > want:
                os.lseek(fd, st.st_size - want, os.SEEK_SET)
            data = b""
            while len(data) < want:
                chunk = os.read(fd, want - len(data))
                if not chunk:
                    break
                data += chunk
            return data.decode("utf-8", "replace")[-n:]
        except OSError:
            return ""
        finally:
            os.close(fd)
