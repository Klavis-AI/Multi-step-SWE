#!/usr/bin/env python3
"""Hidden scorer.

Builds the candidate's control plane from the collected /app, starts it against a
real etcd, replays the corpus recorded from the matching upstream release, and
scores each case.  Always writes /logs/verifier/reward.json and exits 0.
"""
import glob, hashlib, json, os, re, shlex, shutil, signal, stat, subprocess, sys, time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "kcorpus"))
os.environ.setdefault("KCP_SURFACE", os.path.join(HERE, "kcorpus", "surface.json"))
import runplan  # noqa: E402
from runplan import clean_env  # noqa: E402


# The whole scorer has to finish inside the step's verifier timeout (3600 s):
# Harbor discards a step whose verifier overruns, reward.json or not, and
# aborts every later step. The build (900 s) and run (2100 s) ceilings bound
# the candidate's processes, but not the replay itself -- after the run
# ceiling kills the components every remaining `wait` op still polls for its
# full timeout, and a process the candidate detached can keep a port open and
# make every request wait out its own. So the scorer stops itself here, scores
# what it observed (an unanswered case is a failure, as for any run the
# ceiling stopped), and test.sh's outer `timeout` only backs this up.
SCORE_DEADLINE_SEC = 3180


class _ScoreDeadline(BaseException):
    """Not an Exception: the runner's per-op `except Exception` must not
    swallow it."""


def _on_deadline(signum, frame):
    raise _ScoreDeadline()


# The runner the replay builds, kept so that a replay stopped by the deadline
# can still be scored from the observations it made.
_RUNNERS = []


class _KeptRunner(runplan.runner.Runner):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        _RUNNERS.append(self)


runplan.runner.Runner = _KeptRunner

APP = "/app"
BIN = "/out/bin"          # build products live outside the collected workspace
RUN = "/run/kcp"
# The components' stdout/stderr logs. Not under RUN: that tree belongs to the
# candidate uid, and root opens these files, so they live in a directory only
# root can write (runplan reads KCP_LOG_DIR).
LOGS = "/run/kcp-logs"
COMPONENTS = ["kube-apiserver", "kube-controller-manager", "kube-scheduler"]
ALLOWED_LIBS = {
    "libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
    "libssl.so.3", "libcrypto.so.3", "libssl.so.1.1", "libcrypto.so.1.1",
    "ld-linux-x86-64.so.2", "linux-vdso.so.1", "libgcc_s.so.1",
}

# File modes are the package's business, not the launcher's: everything written
# below is read by processes running as another uid.
os.umask(0o022)

MINOR = int(os.environ.get("KCP_MINOR", "25"))
EXPECTED = os.environ.get("KCP_EXPECTED", "v1.25.16-hid.json")
EXTRA = [a for a in os.environ.get("KCP_APISERVER_ARGS", "").split() if a]


# The constants above must describe the corpus that is actually in this image.
# All nine verifier build contexts are directories named tests/ holding the same
# relative paths, and Docker's context sync treats (path, size, mtime) as
# identity, so a sibling step's file can be delivered in place of this one's.
# A mismatch is a packaging fault, not a candidate result: record it in
# precondition_failed.json and exit 3 before scoring anything.
def _abort_precondition(reason):
    try:
        os.makedirs("/logs/verifier", exist_ok=True)
        with open("/logs/verifier/precondition_failed.json", "w") as fh:
            json.dump({"precondition_failed": True, "reason": reason,
                       "kcp_minor": MINOR, "kcp_expected": EXPECTED}, fh)
    except OSError:
        pass
    sys.stdout.write("VERIFIER PRECONDITION FAILURE: %s\n"
                     "No reward.json written: this step is not scoreable.\n" % reason)
    sys.stdout.flush()
    sys.exit(3)


_EXP_DIR = os.path.join(HERE, "expected")
_HID = sorted(f for f in os.listdir(_EXP_DIR)
              if f.startswith("v1.") and f.endswith("-hid.json")) \
    if os.path.isdir(_EXP_DIR) else []
if len(_HID) != 1:
    _abort_precondition("expected exactly one expected/v1.*-hid.json in this image, "
                        "found %d: %s" % (len(_HID), _HID))
if _HID[0] != EXPECTED:
    _abort_precondition("KCP_EXPECTED=%s but this image holds expected/%s -- the "
                        "verifier was given another step's constants"
                        % (EXPECTED, _HID[0]))
_M = re.match(r"^v1\.(\d+)\.", _HID[0])
if not _M or int(_M.group(1)) != MINOR:
    _abort_precondition("KCP_MINOR=%d does not match expected/%s" % (MINOR, _HID[0]))
for _F in ("plan.json", "milestone.json"):
    if not os.path.isfile(os.path.join(_EXP_DIR, _F)):
        _abort_precondition("missing expected/%s" % _F)


# The reference builds in 18 s and replays the corpus in 79 s; these are 50x and
# 26x that, and their sum is 600 s inside the step's own verifier timeout.
BUILD_LIMIT_SEC = int(os.environ.get("KCP_BUILD_LIMIT_SEC", "900"))


def sh(c, timeout=None):
    try:
        return subprocess.run(c, shell=True, capture_output=True, text=True,
                              timeout=timeout)
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"")
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        return subprocess.CompletedProcess(
            c, 124, out + "\n[build exceeded %d s and was stopped]" % timeout, "")


SCORER_UID = 65534          # nobody


def _live_pids(uid):
    """Live (non-zombie) processes owned by uid, read from /proc."""
    out = []
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            with open("/proc/%s/status" % d) as fh:
                st = fh.read()
        except OSError:
            continue
        m_uid = re.search(r"^Uid:\s+(\d+)", st, re.M)
        m_st = re.search(r"^State:\s+(\S)", st, re.M)
        if m_uid and int(m_uid.group(1)) == uid and \
                not (m_st and m_st.group(1) in "ZX"):
            out.append(int(d))
    return out


def reap_candidate():
    """Kill every process of the candidate uid, looping until none is left.

    Every piece of candidate code -- the build, etcd and the three components --
    runs as SCORER_UID, and none of it has any business outliving the phase it
    was started for: a detached child of the build could otherwise rewrite the
    binaries after they were checked, and one left at the end could keep
    writing after the scorer. `kill -KILL -1` sent as that uid reaches all of
    them in one pass that new forks cannot race; pkill catches the rest.
    At most 30 s. Returns how many were still alive after the last pass (0 is
    the goal)."""
    if os.getuid() != 0:
        return 0
    end = time.time() + 30
    live = _live_pids(SCORER_UID)
    while live and time.time() < end:
        for c in (["setpriv", "--reuid", str(SCORER_UID), "--regid", str(SCORER_UID),
                   "--clear-groups", "bash", "-c", "kill -KILL -1"],
                  ["pkill", "-KILL", "-u", str(SCORER_UID)]):
            try:
                subprocess.run(c, capture_output=True, timeout=5)
            except Exception:
                pass
        time.sleep(0.2)
        live = _live_pids(SCORER_UID)
    return len(live)


def take_back_build_output():
    """After the unprivileged build: nothing of it left running, and its output
    owned by root, so what the checks below inspect is what the harness starts."""
    left = reap_candidate()
    sh("chown -R 0:0 /out && chmod -R u+rwX,go+rX,go-w,ug-s /out")
    return {"build_processes_left": left}


def drop_privileges(rundir):
    """Run the candidate's components as a user that cannot read the grader's
    own files, and prove it: the check below probes as that user and reports
    exactly what it can still reach, so a defence that silently fails is loud."""
    if os.getuid() != 0:
        return {"dropped": False, "why": "scorer is not root"}
    os.environ["KCP_DROP_UID"] = str(SCORER_UID)
    os.environ["KCP_DROP_GID"] = str(SCORER_UID)
    for root, dirs, files in os.walk(rundir):
        for n in dirs + files:
            try:
                os.chown(os.path.join(root, n), SCORER_UID, SCORER_UID)
            except OSError:
                pass
    try:
        os.chown(rundir, SCORER_UID, SCORER_UID)
    except OSError:
        pass
    # The components only ever read their own kubeconfig here, and these stay
    # root's. Root's kubectl does not read this tree at all: its parent belongs
    # to the components' uid, and a kubeconfig can name a credential plugin for
    # its client to execute, so the replay gets its own (runplan.root_client).
    kube = os.path.join(rundir, "kube")
    for root, dirs, files in os.walk(kube):
        for n in dirs + files:
            try:
                os.chown(os.path.join(root, n), 0, 0)
                os.chmod(os.path.join(root, n), 0o755 if n in dirs else 0o644)
            except OSError:
                pass
    try:
        os.chown(kube, 0, 0)
        os.chmod(kube, 0o755)
    except OSError:
        pass
    for d in ("/tests", os.path.join(HERE, "expected")):
        try:
            os.chmod(d, 0o700)
        except OSError:
            pass
    readable = []
    for target in (os.path.join(HERE, "expected", EXPECTED),
                   os.path.join(HERE, "expected", "plan.json"),
                   os.path.join(HERE, "expected", "milestone.json")):
        probe = sh("su -s /bin/sh -c 'cat %s > /dev/null 2>&1 && echo yes' nobody 2>/dev/null"
                   % target)
        if "yes" in probe.stdout:
            readable.append(os.path.basename(target))
    return {"dropped": True, "uid": SCORER_UID,
            "expectations_readable_by_component": readable}


def prepare_unprivileged_build():
    if os.getuid() != 0:
        return {"dropped": False, "why": "scorer is not root"}
    for d in ("/tests", os.path.join(HERE, "expected")):
        try:
            os.chmod(d, 0o700)
        except OSError:
            pass
    sh("chown -R %d:%d /out %s" % (SCORER_UID, SCORER_UID, APP))
    probe = sh("su -s /bin/sh -c 'cat %s > /dev/null 2>&1 && echo yes' nobody 2>/dev/null"
               % os.path.join(HERE, "expected", EXPECTED))
    return {"dropped": True, "expectations_readable_at_build": "yes" in probe.stdout}


def build_command():
    if os.getuid() == 0:
        # `env -i` plus the keep-list: `su` alone forwards the caller's environment
        return ("env -i %s su -s /bin/sh -c 'make -C %s' nobody 2>&1"
                % (" ".join("%s=%s" % (k, shlex.quote(v))
                            for k, v in sorted(clean_env().items())), APP))
    return "make -C %s 2>&1" % APP


ALLOWED_EXE = ("/out/bin/kube-apiserver", "/out/bin/kube-controller-manager",
               "/out/bin/kube-scheduler", "/usr/local/bin/etcd")


def _image_helpers():
    """The client tools this image installs for the harness itself (etcdctl and
    kubectl, SPEC/TEST_INTERFACE.md section 5). They are root-owned, the candidate
    uid cannot replace them, and neither can answer the API, so a component that
    keeps one running as a helper is not running a second implementation."""
    out = set()
    for p in ["/usr/local/bin/etcdctl"] + sorted(glob.glob("/usr/local/bin/kubectl*")):
        try:
            st = os.lstat(p)
        except OSError:
            continue
        if stat.S_ISREG(st.st_mode) and st.st_uid == 0 and not st.st_mode & 0o022:
            out.add(p)
    return frozenset(out)


IMAGE_HELPERS = _image_helpers()


def _read_regular(path, limit):
    """At most `limit` bytes of a regular file, or None. Everything read through
    this lives where candidate code can write, so the last path component is not
    followed if it is a symlink, a FIFO or device is refused rather than opened
    for a read that could block, and the read is bounded."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError:
        return None
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            return None
        chunks, left = [], limit
        while left > 0:
            b = os.read(fd, min(left, 1 << 20))
            if not b:
                break
            chunks.append(b)
            left -= len(b)
        return b"".join(chunks)
    except OSError:
        return None
    finally:
        os.close(fd)


def _tail(path, n):
    """The last n characters of a log, read without reading the whole file."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError as e:
        return "unreadable: %s" % e
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode):
            return "unreadable: not a regular file"
        os.lseek(fd, max(0, st.st_size - n), os.SEEK_SET)
        return os.read(fd, n).decode("utf-8", "replace")
    except OSError as e:
        return "unreadable: %s" % e
    finally:
        os.close(fd)


def inspect_process_tree(cl):
    """What is actually listening. A component that answers by proxying to some
    other program -- one it downloaded, or one that happens to be in the image --
    shows up here as an executable that is not the one the build produced."""
    seen, foreign = {}, []

    def descendants(pid):
        out = [pid]
        try:
            kids = open("/proc/%d/task/%d/children" % (pid, pid)).read().split()
        except OSError:
            kids = []
        for k in kids:
            out += descendants(int(k))
        return out

    for name, p in cl.p.items():
        if p.poll() is not None:
            continue
        for pid in descendants(p.pid):
            try:
                exe = os.readlink("/proc/%d/exe" % pid)
            except OSError:
                continue
            seen.setdefault(name, []).append(exe)
            if exe not in ALLOWED_EXE and exe not in IMAGE_HELPERS:
                foreign.append({"component": name, "exe": exe})
    return {"executables": {k: sorted(set(v)) for k, v in seen.items()},
            "foreign": foreign}


def _nobody_readlink_exe(pid):
    """readlink /proc/<pid>/exe, read as the candidate uid. /proc/<pid>/exe needs
    PTRACE_MODE_READ, which root lacks in this container (no CAP_SYS_PTRACE), so a
    root readlink returns nothing -- but the candidate uid owns the process and can
    read its own, so the drop is what makes this legible."""
    r = sh("setpriv --reuid %d --regid %d --clear-groups readlink /proc/%d/exe"
           % (SCORER_UID, SCORER_UID, pid))
    exe = (r.stdout or "").strip()
    return exe[:-10] if exe.endswith(" (deleted)") else exe


def _is_wrapped_image(pid):
    """Read the running image through /proc/<pid>/exe (works even if the file was
    unlinked after exec), as the candidate uid, capped so a huge binary cannot
    exhaust memory. An upstream control-plane binary is a Go build and far larger
    than a C build of this workspace, so 'over 25 MiB or carrying Go-build markers'
    is its signature. A small, Go-marker-free helper the reference might spawn is
    not flagged, so this does not fire on legitimate work."""
    cap = 25 * 1024 * 1024 + 1
    try:
        p = subprocess.run(
            ["setpriv", "--reuid", str(SCORER_UID), "--regid", str(SCORER_UID),
             "--clear-groups", "head", "-c", str(cap), "/proc/%d/exe" % pid],
            capture_output=True, timeout=60)
        blob = p.stdout or b""
    except Exception:
        return (False, False)
    big = len(blob) > 25 * 1024 * 1024
    marked = any(m in blob for m in GO_MARKERS)
    return (big, marked)


def _still_live_candidate(pid):
    """True if pid is still a live, non-zombie process of the candidate uid --
    re-read after a failed exe readlink so a process that merely exited between the
    listing and the readlink is not flagged."""
    try:
        with open("/proc/%d/status" % pid) as fh:
            st = fh.read()
    except OSError:
        return False
    m_uid = re.search(r"^Uid:\s+(\d+)", st, re.M)
    m_st = re.search(r"^State:\s+(\S)", st, re.M)
    return bool(m_uid and int(m_uid.group(1)) == SCORER_UID
                and not (m_st and m_st.group(1) in "ZX"))


def scan_wrapped_exes():
    """A component must BE the program the build produced from this workspace's C
    sources, not a launcher that execs a fetched or decoded upstream binary at run
    time. The build-provenance checks look at the files in /out/bin; this catches
    the run-time form: a live process of the candidate uid whose executable is not
    one of ours and is an upstream-sized / Go-marked binary. Only such a binary is
    flagged, so a legitimate helper process does not trip it, and the image's own
    client tools (IMAGE_HELPERS) are not a second implementation either.

    An unresolvable exe is a HIT, not a skip. A component we built runs a
    root-owned, world-readable binary under /out/bin, so it is dumpable and its
    /proc/<pid>/exe always resolves even as its own uid. A live candidate-uid
    process whose exe cannot be resolved exec'd a binary it cannot read -- an
    execute-only (e.g. mode 0111) or unlinked file -- which the kernel makes
    non-dumpable, so neither the same-uid readlink here nor the root readlink in
    inspect_process_tree can see it, and _is_wrapped_image (which reads the exe)
    cannot classify it either. That is exactly the shape of a launcher that
    downloaded or decoded the upstream Go control plane, exec'd it from an
    unreadable/unlinked file and proxied the API to it, so the unresolvable case
    fails closed (the reference components never trip it)."""
    bad = []
    for pid in _live_pids(SCORER_UID):
        exe = _nobody_readlink_exe(pid)
        if not exe:
            # Could not resolve the exe. Confirm the process is still a live
            # candidate (not one that exited mid-readlink), then fail closed.
            if _still_live_candidate(pid):
                bad.append({"pid": pid, "exe": None, "unresolved": True})
            continue
        if exe in ALLOWED_EXE or exe in IMAGE_HELPERS:
            continue
        big, marked = _is_wrapped_image(pid)
        if big or marked:
            bad.append({"pid": pid, "exe": exe, "big": big, "go": marked})
    return bad


def scan_stash_dirs():
    """Backstop for the file such a launcher would exec or decode: an ELF over
    25 MiB, or any ELF carrying Go-build markers, that the candidate uid wrote.
    Every file the candidate creates is owned by its uid, so walking the whole
    filesystem for files of that uid covers every directory it can write -- /tmp,
    the run tree, /app and anywhere else -- rather than a list of likely places.
    The reference writes nothing of the kind anywhere."""
    bad = []
    cap = 25 * 1024 * 1024
    for root, dirs, files in os.walk("/"):
        if root == "/":
            dirs[:] = [d for d in dirs if d not in ("proc", "sys")]
        for f in files:
            p = os.path.join(root, f)
            try:
                st = os.lstat(p)
            except OSError:
                continue
            if st.st_uid != SCORER_UID or not stat.S_ISREG(st.st_mode) \
                    or st.st_size < 1024:
                continue
            if _read_regular(p, 4) != b"\x7fELF":
                continue
            blob = _read_regular(p, cap + 1)
            if blob is None:
                continue
            if len(blob) > cap or any(m in blob for m in GO_MARKERS):
                bad.append(os.path.relpath(p, "/"))
                if len(bad) >= 50:
                    return bad
    return bad


def committed_elf():
    """Binaries under /app: a build product committed as if it were source, and
    also a file the workspace cannot carry (see SPEC/TEST_INTERFACE.md §1)."""
    bad = []
    for root, dirs, files in os.walk(APP):
        dirs[:] = [d for d in dirs if d not in (".git", "_output")]
        for f in files:
            p = os.path.join(root, f)
            try:
                if os.path.getsize(p) < 64:
                    continue
                with open(p, "rb") as fh:
                    head = fh.read(18)
                # e_type 2 (EXEC) and 3 (DYN) are programs and shared objects; an
                # intermediate object file (1, REL) is not a committed binary
                if head[:4] == b"\x7fELF" and len(head) >= 18 and \
                        int.from_bytes(head[16:18], "little") in (2, 3):
                    bad.append(os.path.relpath(p, APP))
            except OSError:
                pass
    return bad


GO_MARKERS = (b"k8s.io/kubernetes", b"go.buildid", b"runtime.gcWriteBarrier",
              b"golang.org/x/net")


def not_built_from_source():
    """A control plane that wraps a downloaded upstream binary is not a build from
    the workspace's C sources: the link set is closed, the binaries are small, and
    they carry no Go runtime markers."""
    bad = []
    cap = 25 * 1024 * 1024
    for c in COMPONENTS:
        p = os.path.join(BIN, c)
        # resolved, then read without following or blocking: the build chose
        # what this path is
        blob = _read_regular(os.path.realpath(p), cap + 1)
        if blob is None:
            bad.append(c + ": missing or not a regular file")
            continue
        if len(blob) > cap:
            bad.append("%s: over %d bytes" % (c, cap))
        for m in GO_MARKERS:
            if m in blob:
                bad.append("%s: contains %s" % (c, m.decode()))
                break
    return bad


def link_violations():
    bad = []
    for c in COMPONENTS:
        p = os.path.join(BIN, c)
        if not os.path.isfile(p):
            bad.append(c + ": missing")
            continue
        # ldd hands the file to the dynamic loader, so it runs as the
        # candidate's uid like everything else that loads candidate code
        r = sh("timeout -k 5 60 setpriv --reuid %d --regid %d --clear-groups "
               "env -i PATH=/usr/bin:/bin ldd %s"
               % (SCORER_UID, SCORER_UID, shlex.quote(p)))
        out = r.stdout + r.stderr
        if "not a dynamic executable" in out:
            bad.append(c + ": not dynamically linked")
            continue
        for line in r.stdout.splitlines():
            m = re.match(r"\s*(\S+)\s*=>", line) or re.match(r"\s*(\S+)\s*\(", line)
            if not m:
                continue
            lib = os.path.basename(m.group(1))
            if lib not in ALLOWED_LIBS:
                bad.append("%s: %s" % (c, lib))
    return bad


def fixture_fingerprint():
    """What this image actually holds, hashed. The verifier image TAG is a hash of
    the directory on disk, so it stays correct and per-step distinct even when the
    image behind it was served a sibling context's bytes out of the builder's local
    cache; only a digest taken INSIDE the running verifier can tell the two apart.
    Recorded in report.json so a run proves, from its own output, which corpus it
    graded."""
    fp = {}
    try:
        envs = sorted(f for f in os.listdir(HERE)
                      if f.startswith("release-1.") and f.endswith(".env"))
    except OSError as e:
        envs = ["unreadable: %s" % e]
    fp["release_env"] = envs[0] if len(envs) == 1 else envs
    fp["expected_dir"] = sorted(os.listdir(os.path.join(HERE, "expected"))) \
        if os.path.isdir(os.path.join(HERE, "expected")) else []
    for rel in (os.path.join("expected", EXPECTED),
                os.path.join("expected", "plan.json"),
                os.path.join("expected", "milestone.json"),
                os.path.join("kcorpus", "surface.json")):
        try:
            with open(os.path.join(HERE, rel), "rb") as fh:
                fp[rel] = hashlib.sha256(fh.read()).hexdigest()[:16]
        except OSError as e:
            fp[rel] = "unreadable: %s" % e
    return fp


def main():
    signal.signal(signal.SIGALRM, _on_deadline)
    signal.alarm(SCORE_DEADLINE_SEC)
    os.makedirs("/logs/verifier", exist_ok=True)
    report = {"minor": MINOR, "expected": EXPECTED,
              "corpus_fingerprint": fixture_fingerprint()}
    reward = 0.0
    cl = None
    try:
        report["committed_elf"] = committed_elf()[:20]
        shutil.rmtree("/out", ignore_errors=True)
        os.makedirs(BIN, exist_ok=True)
        # The build runs unprivileged too, so a Makefile cannot copy the grader's
        # files somewhere the components can read them later.
        report["unprivileged_build"] = prepare_unprivileged_build()
        b = sh(build_command(), timeout=BUILD_LIMIT_SEC)
        report["build_handover"] = take_back_build_output()
        built = all(os.access(os.path.join(BIN, c), os.X_OK) for c in COMPONENTS)
        report["build"] = {"ok": built, "rc": b.returncode, "tail": b.stdout[-3000:]}
        if not built:
            raise SystemExit(0)
        report["link_violations"] = link_violations()
        report["provenance_violations"] = not_built_from_source()
        shutil.rmtree(RUN, ignore_errors=True)
        os.makedirs(RUN, exist_ok=True)
        shutil.rmtree(LOGS, ignore_errors=True)
        os.makedirs(LOGS, mode=0o755, exist_ok=True)
        os.chmod(LOGS, 0o755)
        os.environ["KCP_LOG_DIR"] = LOGS
        cl = runplan.Cluster(RUN, BIN, 22379, 6443, EXTRA)
        cl.certs()
        # everything the components are told to write is theirs; everything the
        # grader owns is not theirs to read
        report["unprivileged"] = drop_privileges(RUN)
        if not cl.start_etcd("/usr/local/bin/etcd", "/usr/local/bin/etcdctl"):
            report["error"] = "etcd failed to start"
            try:
                report["etcd_log"] = _tail(LOGS + "/etcd.log", 4000)
            except OSError as e:
                report["etcd_log"] = "unreadable: %s" % e
            # who the store had to be writable by, and who owns it
            report["etcd_dir"] = sh("ls -lnd %s %s/etcd 2>&1; id; "
                                    "df -h %s 2>&1 | tail -2" % (RUN, RUN, RUN)).stdout[-1500:]
            raise SystemExit(0)
        if not cl.start_apiserver():
            report["error"] = "kube-apiserver did not become healthy"
            try:
                report["apiserver_log"] = _tail(LOGS + "/apiserver.log", 4000)
            except OSError:
                pass
            raise SystemExit(0)
        cl.start_controllers()
        report["process_tree"] = inspect_process_tree(cl)
        # kubectl runs as root, so it reads kubeconfigs root wrote into a
        # directory only root can enter, with a HOME in there and a fixed PATH
        # (runplan.root_client) -- never the run tree the components' uid owns.
        kdir, kenv = runplan.root_client(cl.sport)
        kst = os.lstat(os.path.dirname(kdir))
        report["kubectl_client"] = {"dir": os.path.dirname(kdir), "uid": kst.st_uid,
                                    "mode": oct(stat.S_IMODE(kst.st_mode)),
                                    "env": sorted(kenv)}
        t0 = time.time()
        try:
            doc, obs = runplan.replay(cl, os.path.join(HERE, "expected", "plan.json"),
                                      "/usr/local/bin/kubectl", "/usr/local/bin/etcdctl",
                                      kubeconfig_dir=kdir, kubectl_env=kenv)
        except _ScoreDeadline:
            # scored from what was observed; cases_attempted below says how far
            # it got
            report["scorer_deadline"] = SCORE_DEADLINE_SEC
            doc = json.load(open(os.path.join(HERE, "expected", "plan.json")))
            obs = dict(_RUNNERS[-1].obs) if _RUNNERS else {}
            if _RUNNERS:
                _RUNNERS[-1].stop_reaper()
        # what is left is the comparison, which is bounded and fast
        signal.alarm(0)
        report["corpus_seconds"] = round(time.time() - t0, 1)
        if cl.killed:
            report["ceiling_kills"] = cl.killed
        expected = json.load(open(os.path.join(HERE, "expected", EXPECTED)))["obs"]
        per, fails = runplan.score(doc, obs, expected)
        report["strata"] = {k: list(v) for k, v in per.items()}
        report["failures"] = fails
        detail = {}
        for cid in fails[:50]:
            detail[cid] = {"expected": expected.get(cid), "observed": obs.get(cid)}
        report["failure_detail"] = detail
        total = sum(v[1] for v in per.values())
        passed = sum(v[0] for v in per.values())
        # The score combines this release's own behaviour with everything the
        # chain already had to do: see SPEC/TEST_INTERFACE.md section 7.
        scored = {cid for ids in doc["strata"].values() for cid in ids}
        # Without this file the split below silently becomes a plain case rate,
        # which scores a broken package *higher*. Refuse instead.
        try:
            milestone = set(json.load(open(os.path.join(HERE, "expected", "milestone.json"))))
        except Exception as e:
            report["error"] = "milestone.json is unusable (%s: %s); this step " \
                              "cannot be scored" % (type(e).__name__, e)
            raise SystemExit(0)
        milestone &= scored
        regression = scored - milestone
        failed = set(fails)
        m_ok, r_ok = len(milestone - failed), len(regression - failed)
        m_rate = m_ok / len(milestone) if milestone else None
        r_rate = r_ok / len(regression) if regression else None
        report["milestone"] = {"passed": m_ok, "total": len(milestone)}
        report["regression"] = {"passed": r_ok, "total": len(regression)}
        if m_rate is None:
            conformance = r_rate or 0.0
        elif r_rate is None:
            conformance = m_rate
        else:
            # Reward = T1 x T2: this release's own milestone cases (T1 = m_rate)
            # times the inherited surface the chain already had to serve
            # (T2 = r_rate). A tree that does this release's work but regresses
            # the inherited surface, or a predecessor tree that passes the
            # inherited surface but not the new cases, scores the product, with
            # no floor.
            conformance = m_rate * r_rate
        report["case_rate"] = passed / total if total else 0.0
        # a run stopped by the wall-clock ceiling shows up here as attempted <
        # total, rather than as a pile of failures with no explanation
        report["cases_attempted"] = len([c for c in scored if c in obs])
        # The sandbox has to have actually happened. When the scorer runs as
        # root it drops the components to `nobody`; if a re-provisioned container
        # lacked that user the drop could become a no-op and every other check
        # would still pass. So make the property gate the score rather than only
        # report it: a run where the components could read the grader's own
        # expectations is a compromised run, not a passing one.
        u = report.get("unprivileged") or {}
        sandbox_ok = (u.get("dropped") is not True) or (
            u.get("uid") == SCORER_UID and not u.get("expectations_readable_by_component"))
        report["sandbox_ok"] = bool(sandbox_ok)
        # Run-time anti-wrap: the served component must be the program built here,
        # not a launcher that exec'd a fetched/decoded upstream binary. The
        # components are still up at this point, so read what is actually running.
        report["wrapped_exes"] = scan_wrapped_exes()
        report["stash_binaries"] = scan_stash_dirs()[:50]
        gate = 0.0 if (report["committed_elf"] or report["link_violations"] or
                       report["provenance_violations"] or report["wrapped_exes"] or
                       report["stash_binaries"] or not sandbox_ok) else 1.0
        report["conformance"] = conformance
        report["build_provenance_gate"] = gate
        reward = conformance * gate
    except SystemExit:
        pass
    except _ScoreDeadline:
        report["error"] = "scorer deadline (%d s) reached before the replay" \
                          % SCORE_DEADLINE_SEC
    except Exception as e:
        report["error"] = "%s: %s" % (type(e).__name__, e)
        report["traceback"] = traceback.format_exc()[-4000:]
        print("scorer failed: %s" % report["traceback"])
    finally:
        signal.alarm(0)
        if cl:
            cl.stop()
        # nothing of the candidate's outlives the scorer
        report["processes_left_at_report"] = reap_candidate()
        with open("/logs/verifier/report.json", "w") as f:
            json.dump(report, f, indent=1, sort_keys=True)
        with open("/logs/verifier/reward.json", "w") as f:
            json.dump({"reward": max(0.0, min(1.0, round(reward, 6)))}, f)
        print("reward=%.6f" % reward)
    sys.exit(0)


main()
