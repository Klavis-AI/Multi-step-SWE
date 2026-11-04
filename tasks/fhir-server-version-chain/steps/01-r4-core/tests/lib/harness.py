#!/usr/bin/env python3
"""Shared verifier harness: build the candidate, start it, prove the process that
owns the port is the binary that build produced, and tear it down."""
import glob, json, os, re, resource, shutil, signal, subprocess, sys, time, urllib.request

APP = "/app"
# Ceilings the grader puts on the candidate process.  Both are stated in the
# step's instruction, so neither is a hidden threshold.
ADDRESS_SPACE_LIMIT = 8 * 1024 * 1024 * 1024      # 8 GiB
REQUEST_TIMEOUT_SEC = 120
BIN = "/app/bin/blaze"
PORT = 18432
DATA = "/tmp/fhirdata"

# The uid the candidate server runs as.  `blazeuser` is created in the verifier
# image; 65534 (nobody) is the fallback when it is not.
def _candidate_ids():
    try:
        import pwd
        e = pwd.getpwnam("blazeuser")
        return e.pw_uid, e.pw_gid
    except Exception:
        return 65534, 65534


CANDIDATE_UID, CANDIDATE_GID = _candidate_ids()

# Author mode: this module is imported from the task tree rather than from the
# image's own /tests.  It cannot be switched on inside the shipped verifier --
# there the file IS /tests/lib/harness.py -- so nothing a candidate or a stray
# ENV line does can reach it.
AUTHOR_MODE = not os.path.abspath(__file__).startswith("/tests/")

# One wall-clock budget every component consults, rather than a set of inner
# caps whose sum has to be re-checked by hand each time a component is added
# (fleet advisory 17 part 6).  `[steps.verifier] timeout_sec` is the step's own
# ceiling; a verifier killed at that ceiling never writes reward.json, which
# aborts every step after it, so the budget stops 450 s short of it and every
# cap is clipped to what is left.  450 rather than 150: test.sh kills this
# scorer at 3,250 s so that the whole verifier ends inside timeout_sec - 240,
# and the budget has to run out before that kill does, with room for the last
# component's in-flight work, the server stop and the aggregation.
STEP_TIMEOUT = float(os.environ.get("FHIRCHAIN_STEP_TIMEOUT", "3600"))
_T0 = time.monotonic()


# The milestone/regression split, and the aggregation over it.  This lived in
# each step's score.py, where it drifted: rung 3 shipped rung 2's weight table
# verbatim, so its own two milestone components -- `profile` and `terminology`,
# the whole point of the rung -- were not in it and fell to an unnamed default.
# One copy, one assertion, and a control that pins both ends (fleet advisory 17
# parts 1 and 5).  Since 2026-10-13 the two roles are factors, not shares: the
# step's reward is milestone x regression (T1 x T2), so a server that keeps every
# earlier behaviour and does none of this milestone's work scores 0.
ROLE_WEIGHTS = {"milestone": "factor T1", "regression": "factor T2"}


def aggregate(results, weights, expected, role_of, log=None):
    """`results` maps component -> {unit id: (passed, weight)}.

    -> (final score, a printable breakdown, the per-role scores).  Within a role
    a component's score is the weighted pass rate of its units in that role; the
    role's score is the component weights renormalised over the components that
    have units in that role.
    """
    log = log or globals()["log"]
    missing_w = sorted(set(expected) - set(weights))
    if missing_w:
        raise AssertionError("components with no weight: %s" % missing_w)
    extra_w = sorted(set(weights) - set(expected))
    if extra_w:
        raise AssertionError("weights for components not expected: %s" % extra_w)
    total = sum(weights.values())
    if abs(total - 1.0) > 1e-9:
        raise AssertionError("component weights sum to %.6f, not 1" % total)

    # A component that produced no units at all must not quietly drop out of the
    # average: an empty result is a check that did not run, and a check that does
    # not run raises every other component's share of the score.  It is scored 0
    # in EVERY role -- an earlier version inserted one fake unit, which landed in
    # whichever role the unit id happened to map to and left the other role to
    # renormalise the component away.
    absent = [n for n in expected if not results.get(n)]
    for n in absent:
        log("COMPONENT %s produced no units at all -- scoring it 0 in every role "
            "rather than letting it drop out of the average" % n)

    table, role_score = {}, {}
    for role in ROLE_WEIGHTS:
        num = den = 0.0
        for comp, units in sorted(results.items()):
            got = w = 0.0
            n = 0
            for uid, (ok, uw) in units.items():
                # The role is looked up by COMPONENT and unit, not by unit alone:
                # the generated REST corpora all number their cases `gen-NNNN`, so
                # `gen-0209` exists in the DSTU2, STU3, R4, R4B and R5 sets at
                # once, and a single-keyed roles file makes one component's role
                # decide another's.
                if role_of(comp, uid) != role:
                    continue
                n += 1
                w += uw
                got += uw if ok else 0.0
            if not n:
                continue
            rate = got / w if w else 0.0
            table["%s/%s" % (role, comp)] = "%d units %.4f" % (n, rate)
            num += weights[comp] * rate
            den += weights[comp]
        for comp in absent:
            table["%s/%s" % (role, comp)] = "0 units 0.0000 (did not run)"
            den += weights[comp]
        if den == 0:
            # Every unit defaulted to the other role, which almost always means
            # the roles file does not cover this rung's unit ids.  A role with no
            # units scores 0, and the product with it, so a correct server would
            # score 0 -- loudly, not silently (this is how rung 3 first scored
            # 0.198640 with every component at or near 100%, under the old sum).
            log("ROLE %s has no units at all -- the roles file does not cover this "
                "rung's unit ids, and this role's whole weight is lost" % role)
        role_score[role] = num / den if den else 0.0
    final = role_score["milestone"] * role_score["regression"]
    return final, table, role_score


def budget_left():
    """Seconds before this verifier must be writing its reward."""
    return max(5.0, STEP_TIMEOUT - 450.0 - (time.monotonic() - _T0))


def component_deadline(n_left, reserve=0.0):
    """An absolute deadline for the next component, so that a candidate which is
    merely SLOW in an early component cannot starve the later ones.

    Fleet advisory 18 §13: a rung-1 workspace scored 0.748 at its own rung and
    0.000 at the next, with every carry-forward group zero -- not failed, never
    reached.  A component that is never reached is indistinguishable in the
    score from one that failed, and it is the carry-forward components, which a
    correct candidate should pass, that sit at the end of the run.
    """
    return time.monotonic() + max(10.0, (budget_left() - reserve) / max(1, n_left))


def past(deadline):
    return deadline is not None and time.monotonic() > deadline


def capped(timeout, share=1.0):
    """A component's own cap, clipped to the budget that is actually left.  A
    `share` below 1 keeps one probe from eating the whole remainder."""
    return max(2.0, min(float(timeout), budget_left() * share))


def workspace_usable(paths_to_read, dirs_to_write):
    """The other half of the confinement (fleet advisory 15 §5).

    A permission model that hides the answers from the candidate can hide its
    INPUTS just as easily, and that failure presents as a broken candidate
    rather than as a broken guard.  This runs as the candidate's uid and asserts
    it can execute the binary, read what the instruction tells it to read, and
    create and remove a file in each directory it is told to write.
    -> (ok, [what failed])
    """
    if os.geteuid() != 0:
        return None, []
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(r)
        bad = []
        try:
            os.setgroups([])
            os.setgid(CANDIDATE_GID)
            os.setuid(CANDIDATE_UID)
            for p in paths_to_read:
                if not os.access(p, os.R_OK):
                    bad.append("cannot read %s" % p)
            if not os.access(BIN, os.X_OK):
                bad.append("cannot execute %s" % BIN)
            for d in dirs_to_write:
                try:
                    os.makedirs(d, exist_ok=True)
                    t = os.path.join(d, ".probe")
                    open(t, "w").write("x")
                    os.unlink(t)
                except OSError as e:
                    bad.append("cannot write %s (%s)" % (d, e))
        except Exception as e:
            bad.append("probe failed: %s" % e)
        os.write(w, ("\n".join(bad)).encode())
        os._exit(0)
    os.close(w)
    out = b""
    while True:
        chunk = os.read(r, 4096)
        if not chunk:
            break
        out += chunk
    os.close(r)
    os.waitpid(pid, 0)
    bad = [x for x in out.decode().split("\n") if x]
    return (not bad), bad


def expectations_unreachable(paths):
    """Self-asserting guard (fleet advisory 12).

    A `chmod` that silently failed -- a read-only mount, a uid that does not
    exist, a file copied in later -- leaves the expectations readable to the
    candidate and nothing says so.  This forks a child that becomes the
    candidate's uid and reports what it can still open.  -> (ok, [readable])
    """
    if os.geteuid() != 0:
        return None, []                # not root: author mode, cannot drop
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(r)
        bad = []
        try:
            os.setgroups([])
            os.setgid(CANDIDATE_GID)
            os.setuid(CANDIDATE_UID)
            for p in paths:
                try:
                    with open(p, "rb") as fh:
                        fh.read(1)
                    bad.append(p)
                except OSError:
                    pass
        except Exception as e:
            bad.append("probe failed: %s" % e)
        os.write(w, ("\n".join(bad)).encode())
        os._exit(0)
    os.close(w)
    out = b""
    while True:
        chunk = os.read(r, 4096)
        if not chunk:
            break
        out += chunk
    os.close(r)
    os.waitpid(pid, 0)
    bad = [x for x in out.decode().split("\n") if x]
    return (not bad), bad
BASE_URL = "http://127.0.0.1:%d" % PORT

# Compiled-language sources the tree may not contain.  Interpreted helper
# scripts (shell, python) are fine; a second compiled language is not.
FOREIGN_SRC = (".cpp", ".cc", ".cxx", ".c++", ".rs", ".go", ".java", ".scala",
               ".cs", ".swift", ".kt", ".kts", ".zig", ".ml", ".hs", ".f90",
               ".f95", ".pas", ".adb", ".ads", ".m", ".mm", ".nim", ".cr", ".v",
               ".d", ".jl", ".erl", ".ex", ".exs", ".clj", ".groovy")
# Nothing under /app is exempt: the whole tree that reaches the verifier is the
# candidate's own work, and a second language hidden anywhere in it fails the step.
SKIP_DIRS = ()
RUNTIME_NAMES = ("java", "python", "python3", "ruby", "node", "nodejs", "perl",
                 "php", "dotnet", "mono", "erl", "beam.smp", "beam", "deno",
                 "bun", "R", "julia", "lua", "luajit", "clojure", "scala", "go",
                 "dart", "swift", "pypy", "pypy3")
# Shared objects that betray another language runtime loaded into the process.
RUNTIME_LIBS = ("libjvm", "libpython", "libruby", "libnode", "libperl", "libphp",
                "libmono", "libcoreclr", "libdotnet", "libjulia", "liblua")


def log(msg):
    print(msg, flush=True)


def _under_skip(path):
    return any(path == d or path.startswith(d + "/") for d in SKIP_DIRS)


def scrub_prebuilt():
    """Delete every compiled artefact the candidate may have left behind, so the
    verifier grades a binary it built itself."""
    removed = 0
    for root, dirs, files in os.walk(APP):
        if _under_skip(root):
            dirs[:] = []
            continue
        for f in files:
            p = os.path.join(root, f)
            if f.endswith((".o", ".a", ".so", ".lo", ".obj", ".class", ".jar", ".pyc")):
                try:
                    os.unlink(p); removed += 1
                except OSError:
                    pass
                continue
            try:
                with open(p, "rb") as fh:
                    magic = fh.read(4)
            except OSError:
                continue
            if magic[:4] in (b"\x7fELF", b"PK\x03\x04") or magic[:2] == b"#!":
                if magic[:4] == b"\x7fELF" or (magic[:4] == b"PK\x03\x04" and f.endswith((".jar", ".zip"))):
                    try:
                        os.unlink(p); removed += 1
                    except OSError:
                        pass
    return removed


def foreign_sources():
    out = []
    for root, dirs, files in os.walk(APP):
        if _under_skip(root):
            dirs[:] = []
            continue
        dirs[:] = [d for d in dirs if d != ".git"]
        for f in files:
            if f.lower().endswith(FOREIGN_SRC):
                out.append(os.path.join(root, f))
    return out


# The only variables a process running CANDIDATE code is given.  Everything a
# scoring run knows lives in the environment it was started with, and `make` is a
# program the candidate wrote -- `$(shell env)` reads whatever it is handed.  A
# verifier that starts a server or a build without saying `env=` hands over the
# whole of its own environment, which is how a scored-test name reached an
# attacker in another builder's package: the attack never needed the harness's
# server, only A server, and the scorer handed it one.
KEEP_ENV = ("PATH", "HOME", "LANG", "LC_ALL", "LC_CTYPE", "TZ", "TERM", "SHELL",
            "TMPDIR", "USER", "LOGNAME")


def clean_env(extra=None):
    env = {k: os.environ[k] for k in KEEP_ENV if k in os.environ}
    env.setdefault("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
    env.setdefault("HOME", "/root")
    if extra:
        env.update(extra)
    return env


def run_bounded(cmd, timeout, mem_bytes=None, as_uid=None, **kw):
    """Run a command in its own session with a wall-clock timeout and an optional
    address-space ceiling; on timeout kill the whole process group.

    `as_uid` drops to that uid first.  It exists for the BUILD: `make` runs a
    file the candidate wrote, and as root that file can read the answer key and
    overwrite the scorer -- `/tests` is mode 700, which stops the candidate's
    server and stops nothing that runs as root.  A build is the most privileged
    process in the verifier and it does not read like one."""
    def _pre():
        os.setsid()
        if mem_bytes:
            resource.setrlimit(resource.RLIMIT_AS, (mem_bytes, mem_bytes))
        if as_uid is not None and os.geteuid() == 0:
            os.setgroups([])
            os.setgid(CANDIDATE_GID)
            os.setuid(as_uid)
    p = subprocess.Popen(cmd, preexec_fn=_pre, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True, **kw)
    try:
        out, err = p.communicate(timeout=timeout)
        return p.returncode, out, err
    except subprocess.TimeoutExpired:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except (OSError, ProcessLookupError):
            pass
        # Bounded too: a descendant that left the process group still holds
        # the pipes, and an unbounded communicate() waits for it forever.
        try:
            p.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
        return None, "", "timed out after %ds" % timeout


def build(timeout=900):
    t0 = time.time()
    # The candidate's own makefile, run as the candidate.  `chown -R` in one
    # process: the cumulative workspace is tens of thousands of files by the last
    # rung and a per-entry loop in Python is the slowest part of the step.
    if os.geteuid() == 0:
        subprocess.run(["chown", "-R", "%d:%d" % (CANDIDATE_UID, CANDIDATE_GID), APP],
                       capture_output=True)
    rc, out, err = run_bounded(["make", "-C", os.path.join(APP, "src")], timeout,
                               ADDRESS_SPACE_LIMIT, as_uid=CANDIDATE_UID,
                               env=clean_env())
    if rc is None:
        log("BUILD: timed out after %ds" % timeout)
        return False
    log("BUILD: exit %d in %.0fs" % (rc, time.time() - t0))
    log("BUILD OUTPUT (tail):\n" + (out or "")[-3000:] + (err or "")[-4000:])
    return rc == 0


def listening_pid(port):
    want = "%04X" % port
    inodes = set()
    for f in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            for line in open(f).read().splitlines()[1:]:
                parts = line.split()
                if len(parts) < 10:
                    continue
                local, state = parts[1], parts[3]
                if state != "0A":            # LISTEN
                    continue
                if local.split(":")[1].upper() == want:
                    inodes.add(parts[9])
        except OSError:
            pass
    if not inodes:
        return None
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            for fd in os.listdir("/proc/%s/fd" % pid):
                try:
                    link = os.readlink("/proc/%s/fd/%s" % (pid, fd))
                except OSError:
                    continue
                m = re.match(r"socket:\[(\d+)\]", link)
                if m and m.group(1) in inodes:
                    return int(pid)
        except OSError:
            continue
    return None


def descendants(pid):
    kids = {}
    for p in os.listdir("/proc"):
        if not p.isdigit():
            continue
        try:
            stat = open("/proc/%s/stat" % p).read()
            ppid = int(stat.rsplit(")", 1)[1].split()[1])
        except (OSError, IndexError, ValueError):
            continue
        kids.setdefault(ppid, []).append(int(p))
    out, stack = [], [pid]
    while stack:
        cur = stack.pop()
        for k in kids.get(cur, []):
            out.append(k)
            stack.append(k)
    return out


def comm(pid):
    try:
        return open("/proc/%d/comm" % pid).read().strip()
    except OSError:
        return ""


def wait_http(url, seconds=180):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=5) as r:
                if r.status < 500:
                    return True
        except Exception:
            time.sleep(1)
    return False


def place_file(src, rel_dir, name):
    """Copy `src` to APP/rel_dir/name for the candidate to read.  APP belongs to
    the candidate by the time this runs, so every step goes through a directory
    descriptor opened without following links, and the file is created fresh
    (O_EXCL | O_NOFOLLOW): nothing here writes or chmods through a link."""
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    fd = os.open(APP, flags)
    try:
        for part in rel_dir.split("/"):
            try:
                os.mkdir(part, 0o755, dir_fd=fd)
            except FileExistsError:
                pass
            nfd = os.open(part, flags, dir_fd=fd)
            os.close(fd)
            fd = nfd
            os.fchmod(fd, 0o755)
        try:
            os.unlink(name, dir_fd=fd)
        except FileNotFoundError:
            pass
        out = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                      0o644, dir_fd=fd)
        with os.fdopen(out, "wb") as f, open(src, "rb") as g:
            os.fchmod(f.fileno(), 0o644)
            f.write(g.read())
    finally:
        os.close(fd)


def scratch_dir(name):
    """A fresh directory only the scorer's uid can enter (mkdtemp: 0700), for
    files the scorer writes and then reads back while the candidate's server is
    still running.  /tmp is shared with that server."""
    import tempfile
    return tempfile.mkdtemp(prefix=name + "-")


def start_server(extra_env=None, data_root=DATA, port=PORT, wait_path="/baseR4/metadata"):
    env = clean_env()
    env.update({
        "SERVER_PORT": str(port),
        "BASE_URL": "http://127.0.0.1:%d" % port,
        "INDEX_DB_DIR": os.path.join(data_root, "index"),
        "TRANSACTION_DB_DIR": os.path.join(data_root, "transaction"),
        "RESOURCE_DB_DIR": os.path.join(data_root, "resource"),
    })
    if extra_env:
        env.update(extra_env)
    # The candidate runs as an unprivileged uid, so the directories it is told
    # to write have to belong to it, and its log has to be writable.
    for d in (env["INDEX_DB_DIR"], env["TRANSACTION_DB_DIR"], env["RESOURCE_DB_DIR"]):
        os.makedirs(d, exist_ok=True)
        if os.geteuid() == 0:
            # Never through a link: the data root belongs to the candidate, and
            # on the restart it may hold whatever the server left there.
            os.chown(d, CANDIDATE_UID, CANDIDATE_GID, follow_symlinks=False)
            os.chown(os.path.dirname(d.rstrip("/")), CANDIDATE_UID, CANDIDATE_GID,
                     follow_symlinks=False)
    # The server writes through this descriptor, which it inherits, so it never
    # opens this path itself and the file does not have to belong to it.  It
    # used to be chowned to the candidate anyway, and that made the SECOND
    # start_server() of a step -- the restart inside `component_persist` --
    # raise EACCES for root: `open(..., "ab")` carries O_CREAT, /tmp is sticky
    # and world-writable, and with `fs.protected_regular` set the kernel
    # refuses an O_CREAT open of a file in such a directory whose owner is
    # neither the directory's owner nor the caller.  No capability overrides
    # that rule, so every candidate lost the whole `persist` weight.  Measured
    # in this image: root with CAP_DAC_OVERRIDE reopening the chowned file ->
    # EACCES; the same file left root-owned -> OK; the same chowned file in a
    # world-writable but NON-sticky directory -> OK.  Mode 0666 keeps the log
    # openable by the candidate's uid, which is all the chown ever bought.
    logpath = "/tmp/server-%d.log" % port
    try:
        out = open(logpath, "ab")
    except OSError:
        out = open("/tmp/server-%d-%d.log" % (port, os.getpid()), "ab")
    if os.geteuid() == 0:
        try:
            os.fchmod(out.fileno(), 0o666)
        except OSError:
            pass

    def _limits():
        os.setsid()
        resource.setrlimit(resource.RLIMIT_AS, (ADDRESS_SPACE_LIMIT, ADDRESS_SPACE_LIMIT))
        # The candidate runs in the same container as the expectation files, so
        # it drops to an unprivileged uid that cannot read them.  A server that
        # replayed recorded answers instead of computing them would otherwise
        # score most of this step without implementing anything.
        if os.geteuid() == 0:
            os.setgroups([])
            os.setgid(CANDIDATE_GID)
            os.setuid(CANDIDATE_UID)
            # Changing credentials clears the dumpable flag, which makes
            # /proc/<pid>/exe and /proc/<pid>/fd readable only with
            # CAP_SYS_PTRACE -- a capability a Kubernetes pod does not have, and
            # the liveness check needs both.  Restoring it costs nothing: it
            # governs who may inspect this process, not what this process may
            # read, so the expectations stay unreachable.
            try:
                import ctypes
                ctypes.CDLL("libc.so.6", use_errno=True).prctl(4, 1, 0, 0, 0)
            except Exception:
                pass

    proc = subprocess.Popen([BIN], cwd=APP, env=env, stdout=out, stderr=out,
                            preexec_fn=_limits)
    global SERVER_PID
    SERVER_PID = proc.pid
    # Clipped to the budget: the restart inside `component_persist` comes last,
    # and a fixed 240 s there could outlast what the step has left.
    ok = wait_http("http://127.0.0.1:%d%s" % (port, wait_path), capped(240))
    return proc, ok


def stop_server(proc):
    if proc is None:
        return
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    except (OSError, ProcessLookupError):
        pass
    try:
        proc.wait(timeout=20)
    except Exception:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except (OSError, ProcessLookupError):
            pass


def listening_uid(port):
    """The uid that owns the listening socket, from /proc/net/tcp."""
    want = "%04X" % port
    for f in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            for line in open(f).read().splitlines()[1:]:
                parts = line.split()
                if len(parts) < 8 or parts[3] != "0A":
                    continue
                if parts[1].split(":")[1].upper() == want:
                    return int(parts[7])
        except OSError:
            pass
    return None


SERVER_PID = None      # set by start_server: the process the verifier started


def proc_uid(pid):
    try:
        for line in open("/proc/%s/status" % pid):
            if line.startswith("Uid:"):
                return int(line.split()[1])
    except OSError:
        pass
    return None


def proc_cmdline(pid):
    try:
        return open("/proc/%s/cmdline" % pid, "rb").read().split(b"\x00")[0].decode()
    except OSError:
        return ""


def identity_without_ptrace(pid, port):
    """Identify the listener from what stays world-readable when a process has
    changed credentials: `cmdline`, `comm` and the `Uid` line of `status`.

    Three things together: the socket belongs to the candidate's uid; the
    process the verifier started is running that uid with argv[0] equal to the
    binary the build produced; and NO process owned by that uid is a language
    runtime -- which is what the /proc/<pid>/maps scan was for.
    """
    want = os.path.realpath(BIN)
    argv0 = proc_cmdline(pid)
    if os.path.realpath(argv0 or "") != want:
        log("IDENTITY: the listener's argv[0] is %r, not %s" % (argv0, want))
        return False
    with open(want, "rb") as fh:
        if fh.read(4) != b"\x7fELF":
            log("IDENTITY: %s is not an ELF binary" % want)
            return False
    uid = proc_uid(pid)
    if os.geteuid() == 0 and uid != CANDIDATE_UID:
        log("IDENTITY: the listener runs as uid %s, not the candidate's %s"
            % (uid, CANDIDATE_UID))
        return False
    foreign = []
    for p in os.listdir("/proc"):
        if not p.isdigit() or proc_uid(p) != uid:
            continue
        c = comm(int(p))
        if c in RUNTIME_NAMES:
            foreign.append("%s (pid %s)" % (c, p))
    if foreign:
        log("IDENTITY: a language runtime is running as the candidate's uid: %s"
            % ", ".join(foreign[:4]))
        return False
    log("IDENTITY: /proc is behind PTRACE_MODE_READ for the confined candidate; "
        "identified by argv[0]=%s, uid=%s, and no runtime owned by that uid"
        % (want, uid))
    return True


def process_identity_ok(port=PORT):
    """The process that owns the listening socket must be the ELF the build
    produced, with no other language runtime anywhere in its process tree."""
    if AUTHOR_MODE:
        return True          # the server was started outside this harness
    pid = listening_pid(port)
    if pid is None and SERVER_PID is not None:
        # The candidate runs as an unprivileged uid, and a process that has
        # changed credentials can be non-dumpable, which makes the inode scan
        # over /proc/<pid>/fd unreadable without CAP_SYS_PTRACE -- a capability
        # a Kubernetes pod does not have.  The socket's owning uid still
        # identifies it: nothing else in this container runs as that uid.
        luid = listening_uid(port)
        if luid is not None and (os.geteuid() != 0 or luid == CANDIDATE_UID):
            pid = SERVER_PID
            log("IDENTITY: socket owned by uid %s; identifying the process the "
                "verifier started (pid %d)" % (luid, pid))
        else:
            log("IDENTITY: the listening socket is owned by uid %s, not the "
                "candidate's %s" % (luid, CANDIDATE_UID))
            return False
    if pid is None:
        log("IDENTITY: no process owns the listening socket")
        return False
    try:
        exe = os.path.realpath("/proc/%d/exe" % pid)
        if not os.path.exists(exe):
            raise OSError("unreadable")
    except OSError:
        # `execve` resets the dumpable flag, so a process that dropped
        # privileges has /proc/<pid>/exe, /fd and /maps behind PTRACE_MODE_READ
        # and a pod without CAP_SYS_PTRACE cannot read them -- not even as root.
        # What is still world-readable identifies it: its argv, its command
        # name, and the uid of every process in the container.
        return identity_without_ptrace(pid, port)
    want = os.path.realpath(BIN)
    if exe != want:
        log("IDENTITY: listener runs %s, not %s" % (exe, want))
        return False
    with open(exe, "rb") as fh:
        if fh.read(4) != b"\x7fELF":
            log("IDENTITY: %s is not an ELF binary" % exe)
            return False
    for kid in [pid] + descendants(pid):
        c = comm(kid)
        if c in RUNTIME_NAMES:
            log("IDENTITY: %s (pid %d) in the server's process tree" % (c, kid))
            return False
        try:
            maps = open("/proc/%d/maps" % kid).read()
        except OSError:
            maps = ""          # confined candidate: the uid scan above covers it
        for lib in RUNTIME_LIBS:
            if lib in maps:
                log("IDENTITY: %s is mapped into pid %d" % (lib, kid))
                return False
    log("IDENTITY: listener pid %d is %s" % (pid, exe))
    return True
