#!/usr/bin/env python3
"""Score one milestone of the Ruby-interpreter task.

Builds the candidate with `make` in /app, then runs every hidden conformance
case through the `ruby` command-line interface and compares the recorded
(stdout, exit status, warned?, exception class) tuple with the one the target
CRuby release produced for that case at authoring time.
"""
import concurrent.futures as cf
import hashlib
import resource
import signal
import stat
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

# The reward line's nonce.  Taken out of the environment before anything else
# runs, so no child -- the build, a case, the perf gate -- inherits it: a
# candidate process must never hold the one secret the reward line rests on.
NONCE = os.environ.pop("RUBYCHAIN_NONCE", "-")

TESTS = os.path.dirname(os.path.abspath(__file__))
APP = "/app"
RUBY = os.path.join(APP, "ruby")
WARN_RE = re.compile(rb"(^|\n)[^\n]*warning:")
# CRuby ends an uncaught-exception report with the class name in parentheses.
MEM_LIMIT = 4 * 1024 * 1024 * 1024   # per scored program
CAND_USER = "candidate"              # the uid the candidate interpreter runs as
EXC_RE = re.compile(rb"\(([A-Z][A-Za-z0-9_:]*)\)\s*$")
CASE_TIMEOUT = 10

# Harbor discards a step whose verifier outlives [steps.verifier] timeout_sec
# (3600 s) even when a reward is already on disk, and then aborts every later
# step -- so a candidate that makes this run slow on purpose would keep the
# mean of the steps before it.  Each case is bounded, but a case that escapes
# its process group can hold the pipe for a second timeout, and 985 of those,
# seven at a time, would not fit.  So the run is bounded as a whole: a case not
# started CASE_CUTOFF seconds after this scorer began counts as failed, and the
# perf gate (step 6) must be done PERF_END seconds in.  An interpreter that
# times out on every case still finishes inside both (build 900 s +
# ceil(985 / 7) x 10 s, then 5 x 240 s of perf).
T0 = time.monotonic()
CASE_CUTOFF = 2700
PERF_END = 3200


def log(msg):
    print(msg, flush=True)


def read_head(path, n):
    """Up to n bytes of a regular file.  Root reads trees the candidate wrote, so
    nothing is followed through a symlink and nothing can block: a FIFO or a
    device under a source name would otherwise hang the scorer.  b"" otherwise."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError:
        return b""
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            return b""
        chunks, size = [], 0
        while size < n:
            b = os.read(fd, min(1 << 20, n - size))
            if not b:
                break
            chunks.append(b)
            size += len(b)
        return b"".join(chunks)
    except OSError:
        return b""
    finally:
        os.close(fd)


def is_elf(path):
    return read_head(path, 4) == b"\x7fELF"


def clean_build_products():
    """Remove anything a previous build left behind, so `make` really rebuilds.

    Every ELF under /app goes, not only the one called `ruby`: otherwise a
    Makefile that copies a binary the agent shipped under another name would
    satisfy the build without compiling anything the source gate counted.
    """
    removed = 0
    for root, dirs, files in os.walk(APP):
        dirs[:] = [d for d in dirs if d != ".git"]
        for fn in files:
            path = os.path.join(root, fn)
            if fn.endswith((".o", ".a", ".so", ".d")) or fn == "ruby" or is_elf(path):
                try:
                    os.remove(path); removed += 1
                except OSError:
                    pass
    log("BUILD: removed %d previously built file(s), including every ELF" % removed)


def elf_kind(path):
    """Describe an ELF from its own header, executing nothing.  None if it is
    not an ELF executable or shared object."""
    h = read_head(os.path.realpath(path), 20)
    if len(h) < 20 or h[:4] != b"\x7fELF":
        return None
    bits = {1: "32-bit", 2: "64-bit"}.get(h[4], "?-bit")
    endian = {1: "LSB", 2: "MSB"}.get(h[5], "?")
    etype = int.from_bytes(h[16:18], "little" if h[5] == 1 else "big")
    kinds = {1: "relocatable", 2: "executable", 3: "shared object/PIE", 4: "core"}
    if etype not in (2, 3):
        return None
    machine = int.from_bytes(h[18:20], "little" if h[5] == 1 else "big")
    arch = {0x3E: "x86-64", 0xB7: "aarch64", 0x28: "arm", 0xF3: "riscv"}.get(machine, "machine %d" % machine)
    return "ELF %s %s %s, %s" % (bits, endian, kinds[etype], arch)


def build():
    """`make` in /app, exactly the command the instruction names."""
    clean_build_products()
    if not os.path.exists(os.path.join(APP, "Makefile")) and \
       not os.path.exists(os.path.join(APP, "makefile")):
        log("BUILD: no Makefile in /app")
        return False
    # The build is the most privileged process in the run and nobody counts it:
    # it executes instructions the candidate wrote.  A recipe that copies the
    # expectations into the candidate's own tree defeats relocation and the uid
    # drop together, so `make` runs as the candidate too.  /app is chowned (not
    # chmod'ed) because the build must be able to write there.
    if CAND_UID is not None:
        for root, dirs, files in os.walk(APP):
            # follow_symlinks=False: a link is re-owned itself, never its target
            for d in dirs:
                try:
                    os.chown(os.path.join(root, d), CAND_UID, CAND_GID,
                             follow_symlinks=False)
                except OSError:
                    pass
            for fn in files:
                try:
                    os.chown(os.path.join(root, fn), CAND_UID, CAND_GID,
                             follow_symlinks=False)
                except OSError:
                    pass
        try:
            os.chown(APP, CAND_UID, CAND_GID)
        except OSError:
            pass

    def _build_as_candidate():
        os.setsid()
        if CAND_UID is not None:
            os.setgid(CAND_GID)
            os.setgroups([])
            os.setuid(CAND_UID)

    try:
        p = subprocess.run(["make"], cwd=APP, capture_output=True, timeout=900,
                           preexec_fn=_build_as_candidate)
    except subprocess.TimeoutExpired:
        log("BUILD: `make` timed out")
        return False
    if p.returncode != 0:
        log("BUILD: `make` failed rc=%d" % p.returncode)
        sys.stdout.write(p.stdout.decode("utf-8", "replace")[-4000:])
        sys.stdout.write(p.stderr.decode("utf-8", "replace")[-4000:])
        return False
    if not (os.path.isfile(RUBY) and os.access(RUBY, os.X_OK)):
        log("BUILD: make did not produce an executable at /app/ruby")
        return False
    # Read the header rather than shelling out: `ldd` runs the artefact's own
    # PT_INTERP, so a provenance check that looks static can execute candidate
    # code inside the scorer.  The question is not "does this start a server",
    # it is "does this command execute the candidate's bytes".
    kind = elf_kind(RUBY)
    if kind is None:
        log("BUILD: rejected, /app/ruby is not a native ELF executable")
        return False
    log("BUILD: ok, /app/ruby is %s" % kind)
    return True


# Markers of an existing Ruby implementation vendored into the tree. These are
# the exact strings the instruction names.
VENDOR_MARKERS = [
    b"Yukihiro Matsumoto",
    b"mruby",
    b"JRuby",
    b"Rubinius",
    b"TruffleRuby",
    b"vm_insnhelper",
    b"vm_core.h",
    b"ruby/internal/",
]
ARCHIVE_SUFFIXES = (".tar.gz", ".tgz", ".tar.xz", ".tar.bz2", ".zip", ".gem")
# An archive is an archive whatever it is called: gzip, xz, bzip2, zip, zstd, 7z,
# lz4, and a plain tar (its "ustar" magic sits at offset 257).
ARCHIVE_MAGIC = (b"\x1f\x8b", b"\xfd7zXZ\x00", b"PK\x03\x04", b"\x28\xb5\x2f\xfd",
                 b"7z\xbc\xaf\x27\x1c", b"\x04\x22\x4d\x18")


def is_archive(head):
    return (head.startswith(ARCHIVE_MAGIC)
            or (head[:3] == b"BZh" and head[3:4] in b"123456789" and len(head) > 3)
            or head[257:262] == b"ustar")


def source_gate(extra_roots=()):
    """Refuse a tree that vendors somebody else's Ruby implementation.

    extra_roots: further trees the shape check examines (after the build, the
    scratch directories a Makefile could unpack a fetched implementation into)."""
    total = 0
    hits = []
    archives = []
    blobs = []
    for root, dirs, files in os.walk(APP):
        # no subtree is skipped: source hidden under spec/ or gems/ would dodge
        # the line budget and the vendor scan entirely
        dirs[:] = [d for d in dirs if d != ".git"]
        for fn in files:
            if fn.endswith(ARCHIVE_SUFFIXES) or is_archive(read_head(os.path.join(root, fn), 512)):
                archives.append(os.path.relpath(os.path.join(root, fn), APP))
            if not fn.endswith((".c", ".h", ".cc", ".cpp", ".y", ".def", ".inc")):
                continue
            path = os.path.join(root, fn)
            real = os.path.realpath(path)
            # read through a link only when it stays inside /app (the compiler
            # follows it, so the budget and the scan must too)
            inside = os.path.commonpath([real, os.path.realpath(APP)]) == os.path.realpath(APP)
            data = read_head(real, 64 << 20) if inside else b""
            total += data.count(b"\n")
            # a binary smuggled in as a very long string literal is neither an
            # archive nor an ELF, so bound the literals too
            longest = max((len(x) for x in re.findall(rb'"(?:[^"\\]|\\.){200,}"', data)),
                          default=0)
            if longest > 100000:
                blobs.append((os.path.relpath(path, APP), longest))
            for m in VENDOR_MARKERS:
                if m in data:
                    hits.append((os.path.relpath(path, APP), m.decode()))
    log("SOURCE: %d lines of C/C++ under /app" % total)
    if total < 2000:
        log("SOURCE: rejected, fewer than 2000 lines of C/C++ in the tree")
        return False
    if total > 120000:
        log("SOURCE: rejected, more than 120000 lines of C in the tree")
        return False
    if hits:
        for h in hits[:10]:
            log("SOURCE: rejected, %s contains %r" % h)
        return False
    if archives:
        log("SOURCE: rejected, source archive(s) in the tree: %s" % " ".join(archives[:5]))
        return False
    if blobs:
        for b in blobs[:5]:
            log("SOURCE: rejected, %s holds a %d-character string literal" % b)
        return False
    for mk in ("Makefile", "makefile", "GNUmakefile"):
        path = os.path.join(APP, mk)
        if not os.path.lexists(path):
            continue
        text = read_head(os.path.realpath(path), 8 << 20)
        for fetch in (b"wget", b"curl", b"git clone", b"svn ", b"pip install",
                      b"gem install", b"http://", b"https://", b"ftp://",
                      b"urllib", b"scp ", b"rsync "):
            if fetch in text:
                log("SOURCE: rejected, %s fetches %r at build time" % (mk, fetch.decode()))
                return False
    # The markers above fall to a consistent rename, so the token SHAPE of the C
    # sources is compared with CRuby's and mruby's as well (vendorgate.py).
    # Bounded, and fail closed: a scan that cannot finish is a rejection, not a
    # pass.
    roots = [APP] + [r for r in extra_roots if os.path.isdir(r)]
    try:
        p = subprocess.run([sys.executable, "-I", os.path.join(TESTS, "vendorgate.py")] + roots,
                           capture_output=True, timeout=600)
    except subprocess.TimeoutExpired:
        log("SOURCE: rejected, the vendor scan did not finish within 600 s")
        return False
    for line in p.stdout.decode("utf-8", "replace").splitlines()[:12]:
        log("SOURCE: " + line)
    if p.returncode != 0:
        log("SOURCE: rejected, the tree holds an existing Ruby implementation's code")
        return False
    return True


def classify(err, status):
    warned = bool(WARN_RE.search(err))
    exc = ""
    if status != 0:
        for line in err.split(b"\n"):
            m = EXC_RE.search(line.rstrip())
            if m:
                exc = m.group(1).decode()
                break
        if not exc and (b"SyntaxError" in err or b"syntax error" in err):
            exc = "SyntaxError"
    return warned, exc


def open_up_app():
    """The candidate runs unprivileged, so the things it legitimately needs --
    its own binary and the vendored gem sources the `lib` stratum requires --
    have to be readable by that uid."""
    # /app itself may arrive mode 0700 (a bind mount from a private directory),
    # in which case the candidate cannot even traverse to its own binary
    #
    # This runs as root AFTER the candidate's build, which owned /app.  So a
    # symlink is never followed out of the tree: a link that resolves outside
    # /app is left alone (the reachability probe then reports it), and inside
    # the walk every link is skipped, so no chmod can land on /tests or on any
    # other file the candidate does not own.  Every candidate process has been
    # reaped before this runs, so nothing changes the tree underneath it.
    try:
        os.chmod(APP, 0o755)
    except OSError:
        pass
    app_real = os.path.realpath(APP)

    def inside(p):
        real = os.path.realpath(p)
        return real if os.path.commonpath([real, app_real]) == app_real else None

    def chmod_if(p, kind, mode):
        try:
            st = os.lstat(p)
        except OSError:
            return
        if kind(st.st_mode):
            try:
                os.chmod(p, mode)
            except OSError:
                pass

    for path in (RUBY, os.path.join(APP, "gems")):
        if not os.path.lexists(path):
            continue
        real = inside(path)
        if real is None:
            log("OPEN: %s resolves outside /app; left as it is" % path)
            continue
        if stat.S_ISDIR(os.lstat(real).st_mode):
            for root, dirs, files in os.walk(real):          # does not follow links
                chmod_if(root, stat.S_ISDIR, 0o755)
                for fn in files:
                    chmod_if(os.path.join(root, fn), stat.S_ISREG, 0o644)
        else:
            chmod_if(real, stat.S_ISREG, 0o755)


def _resolve_candidate():
    """The uid the candidate runs as.  None means it could not be created, which
    is reported loudly rather than silently falling back to root."""
    try:
        import pwd
        e = pwd.getpwnam(CAND_USER)
        return e.pw_uid, e.pw_gid
    except Exception:
        return None, None


CAND_UID, CAND_GID = _resolve_candidate()


def isolation_probe():
    """Assert the separation rather than assume it: become the candidate uid in
    a child and report exactly what of the scored surface it can still read.  A
    chmod that did not take, a read-only mount, a uid that does not exist -- all
    of them show up here instead of silently handing the answers over."""
    if CAND_UID is None:
        log("ISOLATION: !! user %r does not exist; the candidate would run as "
            "root and could read the expectations" % CAND_USER)
        return False
    targets = [os.path.join(TESTS, "expected.json"),
               os.path.join(TESTS, "strata.json"),
               os.path.join(TESTS, "cases")]
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:                                   # child: the candidate's view
        os.close(r)
        try:
            os.setgid(CAND_GID); os.setgroups([]); os.setuid(CAND_UID)
            seen = []
            for t in targets:
                try:
                    if os.path.isdir(t):
                        os.listdir(t); seen.append(t)
                    else:
                        with open(t, "rb") as f:
                            f.read(1)
                        seen.append(t)
                except OSError:
                    pass
            os.write(w, ("\n".join(seen)).encode())
        except Exception:
            pass
        os._exit(0)
    os.close(w)
    got = b""
    while True:
        chunk = os.read(r, 4096)
        if not chunk:
            break
        got += chunk
    os.close(r)
    os.waitpid(pid, 0)
    readable = [x for x in got.decode().split("\n") if x]
    if readable:
        log("ISOLATION: !! as uid %d the candidate can still read: %s"
            % (CAND_UID, " ".join(readable)))
        return False
    log("ISOLATION: ok, uid %d cannot read the recorded expectations" % CAND_UID)
    return True


def reachability_probe():
    """The other half, and the half that fails quietly: confirm the candidate uid
    can still reach what it legitimately needs.  A permission change that hides
    the answers from the candidate can just as easily hide its own binary or the
    vendored gems, and that shows up as a correct implementation scoring zero
    rather than as an error."""
    if CAND_UID is None:
        return False
    wants = [RUBY, os.path.join(APP, "gems", "lib")]
    probe_dir = tempfile.mkdtemp(prefix="probe-")
    os.chmod(probe_dir, 0o711)
    scratch = tempfile.mkdtemp(prefix="p-", dir=probe_dir)
    os.chown(scratch, CAND_UID, CAND_GID)
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(r)
        out = []
        try:
            os.setgid(CAND_GID); os.setgroups([]); os.setuid(CAND_UID)
            for t in wants:
                if not os.path.exists(t):
                    continue
                try:
                    if os.path.isdir(t):
                        os.listdir(t)
                    else:
                        with open(t, "rb") as f:
                            f.read(1)
                        os.stat(t)
                except OSError as e:
                    try:
                        st = os.stat(t)
                        out.append("%s (%s; mode %o owner %d:%d)"
                                   % (t, e.strerror, st.st_mode & 0o7777,
                                      st.st_uid, st.st_gid))
                    except OSError:
                        out.append("%s (%s)" % (t, e.strerror))
            try:
                with open(os.path.join(scratch, "w"), "w") as f:
                    f.write("x")
            except OSError as e:
                out.append("%s not writable (%s)" % (scratch, e.strerror))
        except Exception as e:
            out.append("probe failed: %s" % e)
        os.write(w, ("\n".join(out)).encode())
        os._exit(0)
    os.close(w)
    got = b""
    while True:
        chunk = os.read(r, 4096)
        if not chunk:
            break
        got += chunk
    os.close(r)
    os.waitpid(pid, 0)
    shutil.rmtree(probe_dir, ignore_errors=True)
    bad = [x for x in got.decode().split("\n") if x]
    if bad:
        log("REACHABILITY: !! as uid %d the candidate CANNOT reach: %s -- a "
            "correct interpreter would score zero for this reason alone"
            % (CAND_UID, "; ".join(bad)))
        return False
    log("REACHABILITY: ok, uid %d can run /app/ruby, read the vendored gems and "
        "write in its scratch directory" % CAND_UID)
    return True


def _case_limits():
    """Own process group, so a timeout kills anything the program spawned; a hard
    address-space cap, so an interpreter with a memory bug fails its case instead
    of taking the whole verifier down; and an unprivileged uid, so the candidate
    cannot read the recorded expectations it is being scored against."""
    os.setsid()
    resource.setrlimit(resource.RLIMIT_AS, (MEM_LIMIT, MEM_LIMIT))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if CAND_UID is not None:
        os.setgid(CAND_GID)
        os.setgroups([])
        os.setuid(CAND_UID)


def reap_candidate():
    """Kill every process running as the candidate uid, and keep killing until
    none is left.  Called between phases, so nothing the build or a case left
    behind is still running when the next phase is measured or the score is
    written.  kill(-1) from a child that has become the candidate reaches every
    process of that uid at once, which a fork loop cannot outrun; the /proc
    scan proves the uid is empty before this returns."""
    if CAND_UID is None:
        return
    for _ in range(20):
        pid = os.fork()
        if pid == 0:
            try:
                os.setgid(CAND_GID); os.setgroups([]); os.setuid(CAND_UID)
                os.kill(-1, signal.SIGKILL)
            except Exception:
                pass
            os._exit(0)
        os.waitpid(pid, 0)
        alive = 0
        for d in os.listdir("/proc"):
            if not d.isdigit():
                continue
            try:
                with open("/proc/%s/status" % d) as f:
                    for line in f:
                        # a zombie has already been killed and can do nothing;
                        # pid 1 here does not reap, so it would never go away
                        if line.startswith("State:") and line.split()[1:2] in (["Z"], ["X"]):
                            break
                        if line.startswith("Uid:"):
                            if str(CAND_UID) in line.split()[1:]:
                                alive += 1
                            break
            except OSError:
                pass
        if not alive:
            return
        time.sleep(0.2)
    log("REAP: !! processes of uid %d survived 20 sweeps" % CAND_UID)


def run_case(args):
    cid, switches, workdir = args
    if time.monotonic() - T0 > CASE_CUTOFF:
        return cid, ("<deadline>", -1, False, "")
    scratch = tempfile.mkdtemp(prefix="c-", dir=workdir)
    # The program is copied out of the tests tree before it is run, so the
    # candidate is never handed a path from which the expectations can be
    # reached by walking up one directory.
    path = os.path.join(scratch, cid + ".rb")
    shutil.copyfile(os.path.join(TESTS, "cases", cid + ".rb"), path)
    os.chmod(path, 0o644)
    if CAND_UID is not None:
        try:
            os.chown(scratch, CAND_UID, CAND_GID)
            os.chown(path, 0, 0)
        except OSError:
            pass
    cmd = [RUBY] + switches.split() + [path]
    proc = None
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                cwd=scratch, preexec_fn=_case_limits)
        out, err = proc.communicate(timeout=CASE_TIMEOUT)
        st = proc.returncode
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.communicate(timeout=10)
        except (OSError, ValueError, subprocess.TimeoutExpired):
            pass
        return cid, ("<timeout>", -1, False, "")
    except OSError as e:
        return cid, ("<oserror:%s>" % e, -1, False, "")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    warned, exc = classify(err, st)
    # compared against a digest, so the expectations file never holds an answer
    return cid, (hashlib.sha256(out).hexdigest(), st, warned, exc)


def emit_reward(v):
    """The only channel the score leaves by.  The entrypoint invented the nonce
    and holds it in a shell variable; a candidate process that outlives this one
    can rewrite any file, but it cannot forge this line."""
    v = max(0.0, min(1.0, round(v, 6)))
    print("REWARD %s %.6f" % (NONCE, v))
    sys.stdout.flush()


def main():
    # Modes are set by this package, not inherited: every file the verifier
    # writes for the candidate to read would otherwise be 0666 & ~umask, and a
    # machine whose umask is 0077 would score a correct interpreter at zero.
    os.umask(0o022)
    expected = json.load(open(os.path.join(TESTS, "expected.json")))
    strata = json.load(open(os.path.join(TESTS, "strata.json")))

    # Fail closed.  If the candidate can read the recorded expectations, or
    # cannot reach its own binary, the cases that follow are not a measurement
    # of anything -- report zero rather than a number that looks like a score.
    if not isolation_probe():
        log("ABORT: the scored expectations are reachable by the candidate; "
            "refusing to run the cases")
        emit_reward(0.0)
        return 0
    # judged before the build as well as after: a Makefile that deletes
    # the sources it was counted on would otherwise pass the second pass
    pre_ok = source_gate()
    built = pre_ok and build()
    # whatever the build started is gone before anything is judged or run
    reap_candidate()
    built = built and source_gate(("/tmp", "/var/tmp", "/dev/shm", "/run/lock", "/var/lock"))
    if built:
        open_up_app()
        if not reachability_probe():
            log("ABORT: the candidate cannot reach its own binary or inputs; "
                "this would score a correct interpreter at zero")
            emit_reward(0.0)
            return 0
        # Probed again now: the build ran as the candidate and everything since
        # ran as root on the tree it left, so the separation is asserted where
        # it matters, immediately before the first case.
        if not isolation_probe():
            log("ABORT: the scored expectations became reachable by the "
                "candidate after the build; refusing to run the cases")
            emit_reward(0.0)
            return 0
    results = {}
    if built:
        workdir = tempfile.mkdtemp(prefix="cases-")
        # mkdtemp is 0700 and owned by root; the candidate runs as another uid
        # and has to be able to traverse into its own scratch directory
        os.chmod(workdir, 0o711)
        jobs = [(cid, meta["switches"], workdir) for cid, meta in expected.items()]
        with cf.ThreadPoolExecutor(max_workers=int(os.environ.get("JOBS", "7"))) as ex:
            for cid, got in ex.map(run_case, jobs):
                results[cid] = got
        reap_candidate()
        shutil.rmtree(workdir, ignore_errors=True)

    passed = {}
    detail = {}
    for cid, meta in expected.items():
        want = meta["expect"]
        got = results.get(cid)
        ok = False
        if got is not None:
            # got[0] and want[0] are both sha256 of the standard output
            ok = (got[0] == want[0] and got[1] == want[1] and got[3] == want[3])
            if meta.get("check_warn"):
                ok = ok and (got[2] == want[2])
        passed[cid] = ok
        if not ok:
            detail[cid] = got

    # Product scoring: reward = T1 x T2.  T1 is this release's own milestone --
    # every stratum except `regress`, plus `perf` at step 6 -- as the weighted
    # mean of their pass fractions (strata.json weights, renormalised).  T2 is
    # `regress`, the programs earlier milestones were scored on.  A step without
    # a `regress` stratum (step 1) is T1 alone.
    t1_num = t1_den = 0.0
    t2 = None
    report = []
    for st in strata["strata"]:
        ids = [c for c in st["cases"] if c in expected]
        if not ids:
            continue
        n_ok = sum(1 for c in ids if passed.get(c))
        frac = n_ok / len(ids)
        if st["name"] == "regress":
            t2 = frac
            report.append("%-12s %4d/%-4d  T2           -> %.4f"
                          % (st["name"], n_ok, len(ids), frac))
        else:
            t1_num += st["weight"] * frac
            t1_den += st["weight"]
            report.append("%-12s %4d/%-4d  T1 weight %.2f -> %.4f"
                          % (st["name"], n_ok, len(ids), st["weight"], frac))

    extra = strata.get("extra_weight", 0.0)
    # the case strata are settled: bank that score before the slow part runs
    if extra:
        # the scorer runs under `python3 -I`, which leaves this directory off
        # sys.path; add it back explicitly, and nothing else
        if TESTS not in sys.path:
            sys.path.insert(0, TESTS)
        from perfgate import perf_score  # step 6 only
        # reap_candidate before each benchmark, so no counted run starts with
        # anything of an earlier one, or of the cases, still alive
        pscore, pmsg = (perf_score(RUBY, TESTS, CAND_UID, CAND_GID, T0 + PERF_END,
                                   reap=reap_candidate)
                        if built else (0.0, "build failed"))
        reap_candidate()
        t1_num += extra * pscore
        t1_den += extra
        report.append("%-12s %s  T1 weight %.2f -> %.4f" % ("perf", pmsg, extra, pscore))
    t1 = t1_num / t1_den if t1_den else 0.0
    total_reward = t1 * (t2 if t2 is not None else 1.0)
    report.append("T1 %.4f%s  ->  reward %.4f"
                  % (t1, "" if t2 is None else " x T2 %.4f" % t2, total_reward))

    log("")
    for line in report:
        log(line)
    log("")
    n_ok = sum(1 for v in passed.values() if v)
    log("TOTAL %d/%d cases passed, reward %.4f" % (n_ok, len(passed), total_reward))
    miss = sorted(detail)[:40]
    if miss:
        log("first failing case ids: %s" % " ".join(miss))
    emit_reward(total_reward)
    return 0


if __name__ == "__main__":
    # Every path out of here reports on stdout and exits 0.  The entrypoint is
    # the only writer of reward.json, and it only trusts a line carrying the
    # nonce it invented for this run.
    try:
        sys.exit(main())
    except SystemExit:
        raise            # a normal return through sys.exit is not a crash
    except BaseException:
        import traceback
        tb = traceback.format_exc()
        sys.stderr.write(tb)
        os.makedirs("/logs/verifier", exist_ok=True)
        try:
            with open("/logs/verifier/crash.txt", "w") as f:
                f.write(tb)
        except OSError:
            pass
        print("REWARD %s 0.000000" % NONCE)
        print("VERIFIER CRASHED -- reward 0.0 written, traceback in "
              "/logs/verifier/crash.txt")
        sys.exit(0)
