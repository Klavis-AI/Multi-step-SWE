#!/usr/bin/env python3
"""
Grade one step of the Redis-compatible-server chain.

Everything is derived from Redis's own artefacts:
  * component suites are pristine copies of Redis's Tcl test suite at the
    version tag named in step.json, driven by their own ./runtest;
  * fixture components load RDB/AOF files produced by real redis-server
    binaries at authoring time and compare the resulting dataset against the
    dump those same binaries produced.

Each component belongs to a family (step.json `family`): T1 is this step's
own milestone, T2 the earlier releases it must keep serving. Within a family
the score is the weighted mean of its component fractions, and the reward is
T1 x T2 (T1 alone where a step has no T2 component). A T1 component may list
`inherited` cases -- the ones the previous release's real binary already
passes -- and those are scored in T2 instead (family_parts). A build failure, a server
that never answers PING, or a candidate process that is not this
implementation (ProcWatch) scores 0.
"""
import ctypes, hashlib, secrets, stat, threading
import json, os, re, shutil, signal, socket, subprocess, sys, time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from respclient import Resp, RespError, wait_ready

APP = Path("/app")
TESTS = Path(os.path.dirname(os.path.abspath(__file__)))
WORK = Path("/work")
SUITES = Path("/suites")
LOG = Path("/logs/verifier")

BANNED_SRC_EXT = {
    ".c", ".cc", ".cpp", ".cxx", ".c++", ".m", ".mm", ".go", ".rs", ".zig",
    ".d", ".swift", ".cs", ".kt", ".kts", ".scala", ".groovy", ".clj", ".nim",
    ".ml", ".hs", ".pas", ".f90", ".ada", ".vala",
}
BANNED_DIR_SKIP = {".git", "tests", "build", "logs"}


def stop_server(proc):
    """SIGTERM, then SIGKILL: never leave a server the verifier started running."""
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=20)
    except Exception:  # noqa: BLE001
        try:
            proc.kill()
            proc.wait(timeout=10)
        except Exception:  # noqa: BLE001
            pass


def say(msg):
    print(msg, flush=True)


def free_port(start=21111):
    for p in range(start, start + 4000):
        s = socket.socket()
        try:
            s.bind(("127.0.0.1", p))
            s.close()
            return p
        except OSError:
            s.close()
    raise RuntimeError("no free port")


# --------------------------------------------------------------- privilege --
# The expectations this verifier compares against are FILES: the recorded
# transcripts under /tests/corpus, the fixture manifests under /tests/fixtures
# (which name the exact dataset each RDB image should yield), and the scored
# test-name lists under /tests/selection. A server that reads them and answers
# from them implements nothing and scores everything -- and the liveness guard
# cannot see it, because it asks whether this is your JVM running your classes,
# not whether it computed the answer or looked it up.
#
# What actually protects them is that the packaged copies hold a sha256 of each
# expected reply rather than the reply (see `matches`), so there is nothing in
# those files to answer with.
#
# wave-A 2026-10-12 (reward-hacking audit F1/F3): the uid drop is now ON. As
# root, the candidate could write `ok` rows into the Tcl result shards this file
# counts, read the score nonce out of /proc/<test.sh>/environ, and open this
# process's stdout through /proc. Every candidate process -- `make`, every
# server this file starts, every server the Tcl harness starts (through the
# root-owned wrapper `_server_wrapper` puts in the suite copy) -- now runs as
# UNPRIV. The harness itself stays root and writes its result shards into a
# root-only directory with an unguessable name (`_score_dir`). The liveness
# guard reads the server's ptrace-guarded /proc entries by borrowing UNPRIV's
# filesystem credentials (`_cand_fs`), which is what broke it before: root in a
# container has no CAP_SYS_PTRACE.
UNPRIV = int(os.environ.get("HARBOR_UNPRIV_UID", "65534"))


def _drop():
    """argv prefix that runs a command as UNPRIV, with no capabilities."""
    return ["setpriv", "--reuid=%d" % UNPRIV, "--regid=%d" % UNPRIV,
            "--clear-groups", "--inh-caps=-all", "--bounding-set=-all",
            "--no-new-privs", "--", "env", "HOME=/tmp"]


def _uids(pid):
    """Real, effective and saved uid of a process. Not the fs uid: this
    verifier borrows UNPRIV's fs uid itself for its /proc reads (_cand_fs), and
    a ProcWatch sample in that window counted run.py as a process of the
    candidate uid and zeroed an honest step (wave H, redis oracle-h1 step 05).
    A candidate process has all four set to UNPRIV, so nothing is lost."""
    try:
        for line in open("/proc/%s/status" % pid):
            if line.startswith("Uid:"):
                return {int(x) for x in line.split()[1:4]}
    except (OSError, ValueError):
        pass
    return set()


def sweep_candidate():
    """SIGKILL every process of UNPRIV, until none is left. Run between phases:
    a daemon left by `make` or by one component's server must not live into
    the next."""
    if not UNPRIV:
        return 0
    n = 0
    for _ in range(50):
        pids = [int(p) for p in os.listdir("/proc")
                if p.isdigit() and UNPRIV in _uids(p)]
        if not pids:
            return n
        for p in pids:
            try:
                os.kill(p, signal.SIGKILL)
                n += 1
            except OSError:
                pass
        time.sleep(0.1)
    say("WARNING: processes of uid %d survived the sweep" % UNPRIV)
    return n


_LIBC = []


class _cand_fs:
    """Borrow UNPRIV's filesystem uid/gid for the duration of a /proc read.

    /proc/<pid>/{exe,fd/*,maps,environ} are ptrace-guarded and compare the
    reader's FS credentials with the target's; root in a container has no
    CAP_SYS_PTRACE, so it cannot read them for a process of another uid. The
    fs ids change only this thread's file access, nothing else."""
    def __enter__(self):
        if not _LIBC:
            _LIBC.append(ctypes.CDLL(None, use_errno=True))
        _LIBC[0].setfsgid(UNPRIV)
        _LIBC[0].setfsuid(UNPRIV)
        return self

    def __exit__(self, *exc):
        _LIBC[0].setfsuid(0)
        _LIBC[0].setfsgid(0)
        return False


def _proc(fn, *a):
    """fn(*a) as root; on a permission error, again with UNPRIV's fs creds."""
    try:
        return fn(*a)
    except PermissionError:
        if not UNPRIV:
            raise
        with _cand_fs():
            return fn(*a)


def _read_bytes(path):
    with open(path, "rb") as fh:
        return fh.read()


def _have_setpriv():
    return shutil.which("setpriv") is not None


# ------------------------------------------------------------ process watch ---
# Wave D (2026-10-13), final-review NEW-1. check_jvm() inspects ONE process --
# the one holding the liveness probe's socket -- ONCE. A JVM front on the socket
# with an existing native key-value server doing the work in a sibling process
# passed it, and every later server (fixtures, corpora, every harness-started
# instance) was never inspected at all. ProcWatch inspects EVERY process of the
# candidate uid, every half second, from the end of the build to the end of the
# last component, and any of these fails the step:
#   * an executable that is neither the system JVM (with its jspawnhelper) nor a
#     file of one of Debian's Essential packages (the shells and base utilities
#     a launcher or a stand-in child uses), perl-base excluded -- so nothing the
#     candidate wrote, downloaded or extracted, and no interpreter;
#   * executable code mapped from a location the candidate uid could have
#     written (a JNI/FFM library under /app or /tmp, a memfd, a deleted file);
#   * a native object whose name looks like an existing key-value server.
# Locations are judged by ownership, not by a list of directories: a file
# counts as the system's only when it and every directory above it are owned
# by root and writable by no other uid.
KV_NAMES = ("redis", "hiredis", "valkey", "keydb", "dragonfly", "memcached")
_CHAIN = {}


def _root_chain_ok(path):
    """True when `path` and every directory above it belong to root and no other
    uid can write them: a file the candidate uid can neither have written nor
    replaced."""
    hit = _CHAIN.get(path)
    if hit is not None:
        return hit
    ok, p = True, path
    try:
        while True:
            st = os.lstat(p)
            if (stat.S_ISLNK(st.st_mode) or st.st_uid != 0 or (st.st_mode & 0o002)
                    or ((st.st_mode & 0o020) and st.st_gid != 0)):
                ok = False
                break
            if p == "/":
                break
            p = os.path.dirname(p)
    except (OSError, ValueError):
        ok = False
    _CHAIN[path] = ok
    return ok


def allowed_executables():
    """The executables a candidate process may be: the system JVM, its process
    spawn helper, and the files of Debian's Essential packages minus perl-base,
    as dpkg itself records them."""
    pkgs = []
    try:
        status = open("/var/lib/dpkg/status", encoding="utf-8", errors="replace").read()
    except OSError:
        status = ""
    for para in status.split("\n\n"):
        name = ess = None
        arch = ""
        for line in para.splitlines():
            if line.startswith("Package: "):
                name = line[9:].strip()
            elif line.startswith("Essential: "):
                ess = line[11:].strip()
            elif line.startswith("Architecture: "):
                arch = line[14:].strip()
        if name and ess == "yes" and name != "perl-base":
            pkgs.append((name, arch))
    allowed = set()
    for name, arch in pkgs:
        for lst in ("/var/lib/dpkg/info/%s.list" % name,
                    "/var/lib/dpkg/info/%s:%s.list" % (name, arch)):
            try:
                lines = open(lst, encoding="utf-8", errors="replace").read().splitlines()
            except OSError:
                continue
            for f in lines:
                f = f.strip()
                if f and os.path.isfile(f) and os.access(f, os.X_OK):
                    allowed.add(os.path.realpath(f))
    java = shutil.which("java")
    if java:
        jr = os.path.realpath(java)
        allowed.add(jr)
        allowed.add(os.path.join(os.path.dirname(os.path.dirname(jr)), "lib", "jspawnhelper"))
    return {p for p in allowed if _root_chain_ok(p)}


class ProcWatch(threading.Thread):
    """Samples every UNPRIV process until stop(); see the block comment above."""

    def __init__(self, interval=0.5):
        super().__init__(daemon=True)
        self.interval = interval
        self.allowed = allowed_executables()
        self.java = os.path.realpath(shutil.which("java") or "/nonexistent")
        self.violations = []
        self.samples = 0
        self.errors = 0
        self.jvms = set()
        self.helpers = set()
        self._maps_at = {}
        self._halt = threading.Event()

    @staticmethod
    def _read(fn, path):
        """_proc(fn, path), retried for about 0.4 s while it is refused.

        The kernel clears a process's dumpable flag when its uids change and
        sets it again when it execs, so a process caught between setpriv's uid
        drop and its exec of the next program is briefly unreadable even with
        UNPRIV's fs credentials. One refusal is that window; a process that
        stays unreadable has made itself non-dumpable and is flagged."""
        for delay in (0.02, 0.08, 0.3, None):
            try:
                return _proc(fn, path)
            except PermissionError:
                if delay is None:
                    raise
                time.sleep(delay)

    def _flag(self, pid, exe, why):
        if len(self.violations) < 20 and not any(v["pid"] == pid for v in self.violations):
            self.violations.append({"pid": pid, "exe": exe, "why": why,
                                    "t": round(time.time(), 1)})

    def inspect(self):
        now = time.time()
        for entry in os.listdir("/proc"):
            if not entry.isdigit() or UNPRIV not in _uids(entry):
                continue
            pid = int(entry)
            try:
                exe = self._read(os.readlink, "/proc/%d/exe" % pid)
            except FileNotFoundError:
                continue                      # exited, or a zombie
            except PermissionError:
                self._flag(pid, None, "its executable cannot be read (not dumpable)")
                continue
            except OSError:
                continue
            if exe not in self.allowed:
                self._flag(pid, exe, "executable is not the system JVM or a base "
                                     "system utility")
                continue
            if exe == self.java:
                self.jvms.add(pid)
            else:
                self.helpers.add(os.path.basename(exe))
            try:
                with open("/proc/%d/stat" % pid) as fh:
                    start = fh.read().rsplit(")", 1)[1].split()[19]
            except (OSError, IndexError):
                continue
            key = (pid, start)
            if now - self._maps_at.get(key, 0.0) < 2.0:
                continue
            self._maps_at[key] = now
            try:
                maps = self._read(_read_bytes, "/proc/%d/maps" % pid).decode("utf-8", "replace")
            except FileNotFoundError:
                continue
            except PermissionError:
                self._flag(pid, exe, "its memory map cannot be read (not dumpable)")
                continue
            except OSError:
                continue
            for line in maps.splitlines():
                parts = line.split(None, 5)
                if len(parts) < 6 or not parts[5].startswith("/"):
                    continue
                path = parts[5].strip()
                base = os.path.basename(path).lower()
                native = "x" in parts[1] or ".so" in base
                if native and any(k in base for k in KV_NAMES):
                    self._flag(pid, exe, "maps a native object named like a key-value "
                                         "server: %s" % path)
                    break
                if "x" in parts[1] and not _root_chain_ok(path):
                    self._flag(pid, exe, "maps executable code from a location uid %d "
                                         "can write: %s" % (UNPRIV, path))
                    break

    def run(self):
        while not self._halt.is_set():
            try:
                self.inspect()
                self.samples += 1
            except Exception:  # noqa: BLE001
                self.errors += 1
            self._halt.wait(self.interval)

    def stop(self, report, require_jvm=True):
        self._halt.set()
        self.join(timeout=30)
        try:
            self.inspect()                    # one last look at whatever is left
            self.samples += 1
        except Exception:  # noqa: BLE001
            self.errors += 1
        report["process_watch"] = {
            "samples": self.samples, "errors": self.errors,
            "allowed_executables": len(self.allowed),
            "jvms_seen": len(self.jvms), "helpers_seen": sorted(self.helpers),
            "violations": self.violations}
        if self.violations:
            report["foreign_process_error"] = (
                "a process of the candidate uid is not this implementation: %s"
                % json.dumps(self.violations[:3]))
            return False
        if self.samples == 0 or not self.allowed or (require_jvm and not self.jvms):
            report["foreign_process_error"] = (
                "the process watch could not have caught anything (samples=%d, "
                "allowed=%d, jvms_seen=%d), so this run proves nothing"
                % (self.samples, len(self.allowed), len(self.jvms)))
            return False
        return True


def product_reward(results, report):
    """reward = T1 x T2, each the weighted mean of its components' fractions;
    T1 alone when the step declares no T2 component."""
    num = {"T1": 0.0, "T2": 0.0}
    den = {"T1": 0.0, "T2": 0.0}
    for fam, w, frac in results:
        num[fam] += w * max(0.0, min(1.0, frac))
        den[fam] += w
    t1 = num["T1"] / den["T1"] if den["T1"] > 0 else 0.0
    if den["T2"] > 0:
        t2 = num["T2"] / den["T2"]
        reward = t1 * t2
    else:
        t2 = None
        reward = t1
    report["families"] = {"T1": round(t1, 6), "T2": (None if t2 is None else round(t2, 6)),
                          "formula": "T1*T2" if t2 is not None else "T1",
                          "weights": {k: round(v, 6) for k, v in den.items()}}
    return reward


def case_universe(comp, report):
    """Every case id a component scores, read from its sealed expectation file
    (never from the run, so a component that died early keeps its weight)."""
    kind = comp["kind"]
    if kind in ("suite", "instances"):
        return set(selection_names(comp["selection"], report)[0])
    if kind == "corpus":
        return {e.get("id") for e in json.loads(expect_text("corpus", comp["corpus"]))}
    if kind == "fixtures":
        return {s["name"] for s in json.loads(expect_text("fixtures", comp["manifest"]))}
    raise ValueError("component %s: no case list for kind %r" % (comp["id"], kind))


def family_parts(comp, frac, report):
    """[(family, weight, fraction)] for one component.

    wave H 2026-10-13 (R8, product split): a T1 component's suite is mostly
    tests the previous release already passes, so a predecessor tree scored
    0.43-0.67 on T1 alone. Such a component lists those cases as `inherited`
    (measured with the previous release's real redis-server through this
    harness, see NOTES); they are scored in T2, the rest stay in T1. Each case
    keeps its own weight: the component's weight is shared out by case count.
    """
    fam, w = comp.get("family", "T1"), comp["weight"]
    inh = comp.get("inherited")
    if not inh:
        return [(fam, w, frac)]
    if fam != "T1":
        raise ValueError("component %s: only a T1 component can hand cases to T2" % comp["id"])
    rep = report.setdefault("components", {}).setdefault(comp["id"], {})
    try:
        uni = case_universe(comp, report)
    except Exception as e:  # noqa: BLE001
        # no case list means no verdicts either: the whole weight stays in T1, at 0
        rep["split_error"] = repr(e)[:300]
        return [("T1", w, 0.0)]
    declared = set(inh)
    strays = declared - uni
    old = declared & uni
    new = uni - old
    _uni, passed = CASES.get(comp["id"], (None, set()))
    w2 = w * len(old) / len(uni) if uni else 0.0
    w1 = w - w2
    f1 = len(passed & new) / len(new) if new else 0.0
    f2 = len(passed & old) / len(old) if old else 0.0
    rep["split"] = {"T1": {"cases": len(new), "passed": len(passed & new), "weight": round(w1, 6)},
                    "T2": {"cases": len(old), "passed": len(passed & old), "weight": round(w2, 6)}}
    if strays:
        # a declared case this step does not score is logged, never scored
        rep["split"]["strays"] = sorted(map(str, strays))[:40]
    parts = []
    if new:
        parts.append(("T1", w1, f1))
    if old:
        parts.append(("T2", w2, f2))
    return parts


def digest(obj):
    """A stable digest of a normalised reply or dataset."""
    return hashlib.sha256(
        json.dumps(obj, sort_keys=True, separators=(",", ":"),
                   ensure_ascii=True).encode("ascii")).hexdigest()


def matches(want, got):
    """`want` is either the value itself or {"sha256": ...} of it.

    The packaged corpora and fixture manifests carry digests, not answers: the
    server shares this filesystem with them, and a server that reads the answer
    and replies with it implements nothing. A digest cannot be replied with.
    """
    if isinstance(want, dict) and set(want) == {"sha256"}:
        try:
            return digest(got) == want["sha256"]
        except (TypeError, ValueError):
            # `got` held something json.dumps will not serialise. That is one
            # entry failing to match, not a reason to abandon the component: a
            # raise here escapes to main()'s per-component handler and costs the
            # other several hundred entries their score. The plain-value branch
            # below cannot raise, so only the digest branch needs this.
            return False
    return got == want


def open_app():
    """After `make`: the unprivileged server has to be able to read the classes
    it was just built from, and the one third-party jar."""
    for p in (APP, Path("/opt/redis-deps")):
        if p.exists():
            subprocess.run(["chmod", "-R", "a+rX", str(p)], capture_output=True)


def open_dir(d):
    """Make one run directory usable by the unprivileged server."""
    try:
        subprocess.run(["chmod", "-R", "a+rwX", str(d)], capture_output=True)
    except OSError:
        pass


def clean_env():
    """The environment the candidate's server is started with.

    Every HARBOR_* variable is removed. Three of them are the answer key:
    HARBOR_ONLY_FILE names every scored test, HARBOR_RESULT_LOG names the file
    whose rows this verifier counts, and HARBOR_FILES names the units. A server
    that inherits them can write its own score.

    The Tcl adapter wraps `exec` so the servers the HARNESS starts are clean
    (§42). This is the other half, found by instrumenting the attacker rather
    than by reading the code: the servers **this file** starts -- the liveness
    probe, the corpus replay, every fixture load -- were started with
    `subprocess.Popen` and no `env=`, so they inherited the verifier's own
    environment. One of them writing the result log is enough, because the rows
    land in the directory `run_suite` globs, and after the cheap-first
    reordering the fixtures run *before* the suites.
    """
    return {k: v for k, v in os.environ.items() if not k.startswith("HARBOR_")}


def server_cmd(conf):
    """The command that starts the candidate's server, as UNPRIV. main()
    refuses to score when UNPRIV is set and setpriv is unusable, so this never
    silently falls back to root."""
    exe = [str(APP / "src" / "redis-server"), str(conf)]
    return (_drop() + exe) if UNPRIV else exe


def _server_wrapper(path, target):
    """Replace the suite copy's `src/<name>` with a root-owned launcher that
    execs the candidate's binary as UNPRIV. The Tcl harness starts every server
    through this path (`exec src/redis-server ...`, `../../../src/<name>`), and
    `exec` keeps the pid it records."""
    if path.exists() or path.is_symlink():
        path.unlink()
    if not UNPRIV:
        path.symlink_to(target)
        return
    path.write_text("#!/bin/sh\nexec %s %s \"$@\"\n" % (" ".join(_drop()), target))
    os.chmod(path, 0o755)


def _score_dir(comp):
    """Root-only, unguessably named home for one harness run's selection file
    and result shards. /work is 0711, so the candidate's uid can neither list
    it nor enter it; the root harness writes the shards, nobody else can."""
    d = WORK / ("score-%s-%s" % (comp["id"], secrets.token_hex(12)))
    d.mkdir(mode=0o700)
    os.chmod(d, 0o700)
    return d


SHARD = re.compile(r"^harbor-[A-Za-z0-9_.-]*\.tsv\.[0-9]+$")


def _shards(d, prefix):
    """The result shards the harness's own client processes wrote."""
    return [p for p in sorted(d.glob(prefix + ".*"))
            if SHARD.match(p.name) and p.is_file() and not p.is_symlink()]


def _open_scratch(dst):
    """The harness creates the servers' directories (umask 0); the tmp dirs
    that ship in the suite copy also have to accept the unprivileged server.
    The copy keeps /suites' modes, which can be 0700 (they come from the build
    context), and the server resolves its config relative to the harness's cwd
    inside it -- so the whole copy is opened for reading first. Its content is
    the public upstream suite; nothing scored lives in it any more."""
    subprocess.run(["chmod", "-R", "a+rX", str(dst)], capture_output=True)
    # 0777, not 1777: Redis rewrites its config (CONFIG REWRITE) by writing a
    # temp file beside it and renaming it over the original, and the harness
    # (root) creates that config in tests/tmp. A sticky directory refuses that
    # rename to any uid but the file's owner, so a server that rewrites the way
    # Redis does would fail those tests here (wave-B 2026-10-12, reproduced).
    for p in dst.rglob("tmp"):
        if p.is_dir() and not p.is_symlink():
            os.chmod(p, 0o777)
    # The suite was written for a harness and server that share one uid: tests
    # `exec cp tests/assets/user.acl $server_dir` and then expect the server to
    # rewrite that file (ACL SAVE), or to append to a copied AOF. Under umask 0
    # a copy takes the source's mode, so the server-input data files (RDB, AOF,
    # ACL) are made a+rw here and every copy of them is writable by UNPRIV.
    # The harness's base configs (*.conf) and all Tcl stay read-only. Measured:
    # without this, `ACL load and save` and `... with restricted channels`
    # failed for the reference at steps 10 and 11 (oracle-a1).
    for p in dst.rglob("*"):
        if (p.suffix in (".rdb", ".aof", ".acl") and p.parent.name == "assets"
                and p.is_file() and not p.is_symlink()):
            os.chmod(p, 0o666)


# ------------------------------------------------------------ expectations ----
#
# Everything that says what the right answer IS gets read into this process
# before the candidate's first instruction runs, and every later read comes from
# here rather than from the disk.
#
# `seal_expectations` below chmods those files to 0700, which stops an
# unprivileged candidate. But UNPRIV defaults to 0 -- the candidate's server is
# **root in this container** -- and root does not consult file permissions. The
# selection lists, recorded transcripts and fixture manifests are read inside
# each component, which is after the build and after the candidate's processes
# exist. That is a window in which the answer key is writable by the thing being
# graded, and a scorer reading it afterwards is grading a question the candidate
# was free to edit. Emptying the key scores 0 (`frac = ... if expected else 0.0`)
# so the cheap version of the attack is unprofitable, but rewriting a recorded
# reply to whatever the server answers is not.
#
# The suites are the same hole one directory over: `/suites` is copied per
# component, again after the candidate is running, so the Tcl bodies could be
# rewritten to pass. Those are too large to hold in memory, so what is held is a
# digest taken before the build, and the copy is checked against it at use.
#
# The principle is the one the entrypoint's function wrap uses: the authority
# lives in this process's memory, where a root candidate cannot reach it, not in
# a file it can open. (FLEET-ADVISORY-21, extended.)
EXPECT = {}
SUITE_DIGEST = {}
FIX_DIRS = {}
# component id -> (every scored case id, the ids that passed), filled in by each
# runner once it has a verdict per case; family_parts() splits on it.
CASES = {}


def _tree_digest(root):
    h = hashlib.sha256()
    for f in sorted(Path(root).rglob("*")):
        if f.is_file() or f.is_symlink():
            h.update(str(f.relative_to(root)).encode() + b"\0")
            if f.is_symlink():
                h.update(b"L" + os.readlink(f).encode() + b"\0")
            else:
                h.update(hashlib.sha256(f.read_bytes()).digest())
    return h.hexdigest()


def preload_expectations(report):
    """Read every expectation into memory and digest every suite tree."""
    for sub in ("selection", "corpus", "fixtures"):
        d = TESTS / sub
        if not d.is_dir():
            continue
        for f in sorted(d.rglob("*")):
            if f.is_file():
                EXPECT[(sub, str(f.relative_to(d)))] = f.read_bytes()
    EXPECT[("", "step.json")] = (TESTS / "step.json").read_bytes()
    d = TESTS / "fixtures"
    if d.is_dir():
        # EVERY directory under fixtures/, at every depth -- not only the
        # top-level ones.
        #
        # The manifests name a multi-part-AOF fixture one level down
        # ("aof-7.0.15/appendonlydir"), and run_fixtures looks its digest up
        # under exactly that string. Built from `iterdir()` this map held only
        # "aof-7.0.15", so FIX_DIRS.get() returned None for every directory
        # fixture, None never equals a digest, and each one failed
        # UNCONDITIONALLY with "fixture dir ... was modified after the verifier
        # started" -- a guaranteed zero on 0.050000 of step 9, 0.052381 of step
        # 10 and 0.056000 of step 11, before a single server was started.
        #
        # It read as a real tamper signal for four rounds (NOTES 106.1, 110,
        # 111, where it is explained as "the server legitimately appends to the
        # directory it was given"). It cannot be that: the digest is taken of
        # the SOURCE tree under /tests/fixtures, before the copytree and before
        # any server exists, and the graded server is pointed at its own
        # WORK/fx-<comp>-<name> copy and never at /tests. The check was not
        # firing on a write; it was failing to find its key.
        for f in sorted(d.rglob("*")):
            if f.is_dir():
                FIX_DIRS[str(f.relative_to(d))] = _tree_digest(f)
    if SUITES.is_dir():
        for t in sorted(SUITES.iterdir()):
            if t.is_dir():
                SUITE_DIGEST[t.name] = _tree_digest(t)
    report["expectations_preloaded"] = len(EXPECT)
    report["suites_digested"] = len(SUITE_DIGEST)
    # An empty preload is indistinguishable from a clean one downstream, so it
    # is said here rather than inferred from an absence later.
    if not EXPECT or (SUITES.is_dir() and not SUITE_DIGEST):
        say("WARNING: preload found %d expectation files and %d suite trees -- "
            "one of those is zero, so the integrity checks below have nothing "
            "to compare and cannot fail." % (len(EXPECT), len(SUITE_DIGEST)))


def expect_text(sub, name):
    """The expectation as it was BEFORE the candidate ran, or a hard failure."""
    b = EXPECT.get((sub, name))
    if b is None:
        raise RuntimeError("expectation %r/%r was not preloaded" % (sub, name))
    return b.decode("utf-8", "surrogateescape")


def check_suite_copy(tag, dst):
    """The copied suite tree against the digest taken before the build.

    A missing digest is a FAILURE, not a pass. The first version returned None
    -- "no digest, nothing to compare, carry on" -- which is a check that can
    only ever report good news: if `preload_expectations` had not run, or
    `/suites` were absent, or the tag were spelled differently, every component
    would sail through silently. The path is unreachable today (a component
    whose tag is not under /suites fails at the copytree above), but
    "unreachable" is the reasoning that let three earlier defects ship, and
    advisory 24's rule is that silence is not a result.
    """
    want = SUITE_DIGEST.get(tag)
    if want is None:
        return ("suite %s has no pre-build digest: the integrity check never "
                "ran (digested %d trees)" % (tag, len(SUITE_DIGEST)))
    got = _tree_digest(dst)
    return None if got == want else (
        "suite %s was modified after the verifier started (%s != %s)"
        % (tag, got[:16], want[:16]))


def seal_expectations(report):
    """Root-only on everything that says what the answer is; open what the
    server legitimately needs."""
    done = {}
    for d in ("corpus", "fixtures", "selection"):
        p = TESTS / d
        if p.exists():
            try:
                os.chmod(p, 0o700)
                for root, dirs, files in os.walk(p):
                    for n in dirs + files:
                        os.chmod(os.path.join(root, n), 0o700)
                done[d] = "root-only"
            except OSError as e:
                done[d] = "chmod failed: %s" % e
    # Say whether that chmod is worth anything HERE, rather than leaving
    # `expectations_sealed: root-only` in the report to be read as a defence.
    # It stops an unprivileged candidate; UNPRIV defaults to 0, and root does
    # not consult file permissions, so in the shipped configuration it stops
    # nothing. What actually protects the answer key is the preload above.
    report["expectations_sealed"] = done
    report["seal_effective"] = bool(UNPRIV) and os.geteuid() == 0 and _have_setpriv()
    if not report["seal_effective"]:
        report["seal_note"] = ("chmod 0700 is inert against a root candidate "
                               "(UNPRIV=%s); the preload is the protection" % UNPRIV)
    report["server_uid"] = (UNPRIV if UNPRIV and os.geteuid() == 0
                            and _have_setpriv() else os.geteuid())
    # self-assert: a chmod that silently did nothing is worse than none
    if UNPRIV and os.geteuid() == 0 and _have_setpriv():
        probe = []
        for d in ("corpus", "fixtures", "selection"):
            p = TESTS / d
            if not p.exists():
                continue
            r = subprocess.run(["setpriv", "--reuid=%d" % UNPRIV,
                                "--regid=%d" % UNPRIV, "--clear-groups", "--",
                                "ls", str(p)], capture_output=True)
            if r.returncode == 0:
                probe.append(d)
        report["unpriv_can_still_read"] = probe
        return not probe
    return False


# ----------------------------------------------------------------- build ----
def scan_sources():
    """Return the list of non-Java compiled-language sources found in /app."""
    bad = []
    for root, dirs, files in os.walk(APP):
        rel = Path(root).relative_to(APP)
        parts = set(rel.parts)
        if parts & BANNED_DIR_SKIP:
            dirs[:] = []
            continue
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext in BANNED_SRC_EXT:
                bad.append(str(rel / f))
    return bad


def clean_build_products():
    removed = 0
    for root, dirs, files in os.walk(APP):
        if ".git" in dirs:
            dirs.remove(".git")
        for f in files:
            if f.endswith((".class", ".jar", ".so", ".o", ".a")):
                try:
                    os.unlink(os.path.join(root, f))
                    removed += 1
                except OSError:
                    pass
    for d in ("build", "out", "target", "classes"):
        p = APP / d
        if p.is_dir():
            shutil.rmtree(p, ignore_errors=True)
    return removed


# The launchers a collected tree may carry under /app/src.
LAUNCHERS = ("redis-server", "redis-sentinel", "redis-check-aof",
             "redis-check-rdb", "redis-cli", "redis-benchmark")


def remove_launchers():
    """Delete the launchers under /app/src (the second build only, see build)."""
    removed = 0
    for name in LAUNCHERS:
        lp = APP / "src" / name
        try:
            if lp.is_symlink() or lp.is_file():
                lp.unlink()
                removed += 1
        except OSError:
            pass
    return removed


def _snapshot_app(snap):
    """Copy /app as collected, before anything touches it. cp -a copies links
    as links and special files as special files: nothing in the tree is
    followed or opened. Root-only, so the build cannot reach it."""
    shutil.rmtree(snap, ignore_errors=True)
    try:
        r = subprocess.run(["cp", "-a", "--", str(APP), str(snap)],
                           capture_output=True, timeout=300)
        os.chmod(snap, 0o700)
    except (subprocess.TimeoutExpired, OSError):
        return False
    return r.returncode == 0


def _restore_app(snap, report):
    """Put /app back exactly as collected."""
    for child in list(APP.iterdir()):
        try:
            if child.is_dir() and not child.is_symlink():
                shutil.rmtree(child, ignore_errors=True)
            else:
                child.unlink()
        except OSError:
            pass
    try:
        r = subprocess.run(["cp", "-a", "--", str(snap) + "/.", str(APP)],
                           capture_output=True, timeout=300)
        if r.returncode != 0:
            report["restore_error"] = r.stderr[-500:].decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        report["restore_error"] = "cp timed out"


def fresh_class_files(report):
    """The .class files produced by the `make` this verifier just ran."""
    floor = report.get("build_started_at", 0) - 1
    out = []
    for p in APP.rglob("*.class"):
        try:
            if p.stat().st_mtime >= floor:
                out.append(p)
        except OSError:
            pass
    return out


def build(report):
    """Build the collected tree; True when the build contract is met.

    The launchers under /app/src are left in place for the first `make`
    (wave R 2026-10-21, trajectory review g27 R1). BUILD_AND_RUN.md names what
    is deleted before `make`, and launchers were not on that list, yet they
    were deleted: a Makefile that only chmods launchers its author wrote by
    hand then fails, and two of four models lost all eleven steps that way.
    A `make` that compiles nothing is still caught by fresh_class_files().

    The deletion did have a reason, and it still holds for some Makefiles: one
    whose only file target is src/redis-server, with prerequisites that are
    also under /app/src (or none), finds the restored launcher up to date and
    never runs javac. So when the first build is rejected for any reason but a
    timeout, and the tree carried launchers, /app is restored as collected and
    built once more with the launchers deleted as well -- exactly the build
    this grader always ran. The second result stands. That build accepts
    nothing it did not accept before; the first adds only trees whose `make`
    compiles and leaves the launchers executable.
    """
    if not (APP / "Makefile").exists() and not (APP / "makefile").exists():
        report["build_error"] = "no Makefile in /app"
        return False
    bad = scan_sources()
    if bad:
        report["build_error"] = "non-Java compiled sources present: " + ", ".join(sorted(bad)[:20])
        return False
    collected = [n for n in LAUNCHERS
                 if (APP / "src" / n).is_symlink() or (APP / "src" / n).exists()]
    report["collected_launchers"] = collected
    snap = WORK / "app-as-collected"
    try:
        have_snap = bool(collected) and _snapshot_app(snap)
        report["cleaned_products"] = clean_build_products()
        report["build_attempts"] = 1
        if make_once(report):
            return True
        if not collected or report.get("build_timed_out"):
            return False
        first = {k: report.pop(k) for k in
                 ("build_error", "build_rc", "build_seconds", "build_tail",
                  "build_swept", "fresh_class_files") if k in report}
        first["build_tail"] = first.get("build_tail", "")[-1500:]
        report["first_build"] = first
        if have_snap:
            _restore_app(snap, report)
        else:
            report["restore_error"] = "no snapshot of the collected tree"
        report["build_attempts"] = 2
        report["cleaned_products_second"] = (clean_build_products()
                                             + remove_launchers())
        return make_once(report)
    finally:
        shutil.rmtree(snap, ignore_errors=True)


def make_once(report):
    """One `make -C /app` on the tree as it stands, judged against the build
    contract."""
    # The build runs as UNPRIV, so it gets /app (and must be able to read the
    # one third-party jar). chown -R -P -h never follows a link out of /app.
    open_app()
    if UNPRIV:
        subprocess.run(["chown", "-R", "-P", "-h", "%d:%d" % (UNPRIV, UNPRIV),
                        str(APP)], capture_output=True)
    t0 = time.time()
    report["build_started_at"] = t0
    try:
        # The candidate's Makefile is candidate code and runs before anything
        # else, so it gets the same clean environment as the candidate's server:
        # a recipe line that writes the result log would be the same attack one
        # step earlier. It also runs as UNPRIV, never root.
        p = subprocess.run((_drop() if UNPRIV else []) + ["make", "-C", str(APP)],
                           capture_output=True, timeout=1200, env=clean_env())
    except subprocess.TimeoutExpired:
        report["build_error"] = "make timed out after 1200s"
        report["build_timed_out"] = True
        return False
    finally:
        # nothing the build started outlives it
        report["build_swept"] = sweep_candidate()
    report["build_seconds"] = round(time.time() - t0, 1)
    report["build_rc"] = p.returncode
    tail = (p.stdout[-4000:] + b"\n" + p.stderr[-4000:]).decode("utf-8", "replace")
    report["build_tail"] = tail
    if p.returncode != 0:
        report["build_error"] = "make exited %d" % p.returncode
        return False
    needed = ["redis-server"]
    step = json.loads((TESTS / "step.json").read_text())
    if any("sentinel" in c.get("runner", "") for c in step["components"]):
        needed.append("redis-sentinel")
    for name in needed:
        srv = APP / "src" / name
        if not srv.exists():
            report["build_error"] = "make did not produce /app/src/%s" % name
            return False
        if not os.access(srv, os.X_OK):
            report["build_error"] = "/app/src/%s is not executable" % name
            return False
    # `make` can exit 0 having compiled nothing. check_jvm() already knows the
    # right words for that -- "make produced no class files under /app" -- but
    # it only reaches them after a 90s PING wait that reports a misleading
    # `liveness_error` instead. The condition is the same one; say it here,
    # where the cause is still legible, and 90s earlier.
    fresh = fresh_class_files(report)
    if not fresh:
        report["build_error"] = (
            "make produced no class files under /app: it exited %s after %ss "
            "without compiling anything" % (report.get("build_rc"),
                                            report.get("build_seconds")))
        return False
    report["fresh_class_files"] = len(fresh)
    return True


def _listening_pids(port):
    """Every pid holding a listening socket on `port`, via /proc."""
    inodes = set()
    for f in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            lines = open(f).read().splitlines()[1:]
        except OSError:
            continue
        for ln in lines:
            p = ln.split()
            if len(p) < 10:
                continue
            local = p[1]
            st = p[3]
            if st != "0A":            # TCP_LISTEN
                continue
            try:
                lport = int(local.split(":")[1], 16)
            except (IndexError, ValueError):
                continue
            if lport == port:
                inodes.add(p[9])
    if not inodes:
        return set()
    pids = set()
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        fddir = "/proc/%s/fd" % entry
        try:
            for fd in _proc(os.listdir, fddir):
                try:
                    tgt = _proc(os.readlink, os.path.join(fddir, fd))
                except OSError:
                    continue
                if tgt.startswith("socket:[") and tgt[8:-1] in inodes:
                    pids.add(int(entry))
        except OSError:
            continue
    return pids


def env_leak(pid):
    """Which HARBOR_* variables the process at `pid` can actually read.

    `clean_env()` and the adapter's `exec` wrapper are constructions: they cover
    the launchers I know about. This is the measurement, taken from the process
    that is actually serving, so a green oracle is a standing assertion of the
    property however the server got started -- including from a launcher added
    next week. nfs's probe, and it composes with the construction rather than
    duplicating it.
    """
    try:
        raw = _proc(_read_bytes, "/proc/%d/environ" % pid)
    except OSError:
        return None
    return sorted(v.split(b"=", 1)[0].decode("latin-1")
                  for v in raw.split(b"\x00")
                  if v.startswith(b"HARBOR_"))


def real_uid(pid):
    """The uid the process is ACTUALLY running as, from the kernel.

    postgres broke its privilege drop deliberately and every gate stayed green,
    because its access probe ran `setpriv` as the target uid -- which is true
    whatever the server runs as. The report has always carried a `server_uid`,
    and that field records what this verifier *intended*. This one records what
    happened.
    """
    try:
        for line in open("/proc/%d/status" % pid):
            if line.startswith("Uid:"):
                return int(line.split()[1])
    except (OSError, ValueError, IndexError):
        pass
    return None


def own_server_ok(proc, port, report, ctx):
    """The process answering on `port` must be the one we just started.

    free_port() hands out a port nothing was bound to, so a mismatch here means
    a server from an earlier component outlived its stop_server() and is now
    answering for a different dataset -- a result that would look like a pass
    or a fail for reasons that have nothing to do with the candidate. Assert on
    OS facts (who holds the listening socket) rather than on anything the
    server reports about itself.
    """
    pids = _listening_pids(port)
    ok = (not pids) or (pids == {proc.pid})
    report.setdefault("own_server", {})[ctx] = {
        "port": port, "spawned": proc.pid, "listening": sorted(pids), "ok": ok}
    return ok


# Which component kinds are cheap enough that they can never be the reason a
# later one is starved. `corpus` replays a recorded transcript against one
# server and `fixtures` loads an RDB or AOF and reads the dataset back: seconds
# each. `suite` and `instances` both drive redis's Tcl harness -- `instances` is
# the sentinel and cluster harnesses, which start twenty servers and are the
# most expensive thing in the package. Classifying by "not a suite" put
# `instances` in the cheap group and made step 3 WORSE, which the before/after
# measurement caught: 0.283743 -> 0.137240, C rescued and B lost.
CHEAP = ("corpus", "fixtures")


def component_order(components):
    """Cheap components first, then the suites in their declared order.

    Components are independent -- each starts its own servers in its own
    directory -- so the order is free to choose, and first-come first-served is
    the wrong choice. The corpora and fixtures take seconds; the suites take
    minutes and can take their whole cap when a candidate HANGS rather than
    fails. Summed, the per-component caps exceed this verifier's deadline at
    nine of the eleven steps, so in declared order a server that hangs in the
    first suite silently forfeits the carry-forward fixtures -- the components
    that measure whether earlier rungs still work, which is the whole claim of a
    chain. Observed once: C and Cp came back `skipped` on a diagnostic run.

    Ordering cheap-first makes that impossible: the fixtures and transcripts are
    always reached, and the suites can only starve each other.
    """
    return ([c for c in components if c["kind"] in CHEAP]
            + [c for c in components if c["kind"] not in CHEAP])


def check_jvm(report):
    """Start the server once and confirm the process that actually holds the
    listening socket is a JVM running classes this build produced."""
    port = free_port()
    d = WORK / "jvmcheck"
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True, exist_ok=True)
    conf = d / "redis.conf"
    conf.write_text("port %d\nbind 127.0.0.1\ndir %s\nlogfile %s\nsave ''\nappendonly no\n"
                    % (port, d, d / "log"))
    open_dir(d)
    proc = subprocess.Popen(server_cmd(conf), env=clean_env(),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        r = wait_ready("127.0.0.1", port, time.time() + 90)
        if r is None:
            report["liveness_error"] = "server did not answer PING within 90s"
            try:
                report["server_log"] = (d / "log").read_text()[-3000:]
            except OSError:
                pass
            return False
        r.close()
        pids = _listening_pids(port)
        report["listening_pids"] = sorted(pids)
        if pids and pids != {proc.pid}:
            report["liveness_error"] = (
                "the listening socket on port %d is held by %s, not by the "
                "redis-server this verifier started (pid %d)"
                % (port, sorted(pids), proc.pid))
            return False
        pids = pids or {proc.pid}
        exes, cmds = [], []
        for pid in pids:
            try:
                exes.append(os.path.realpath(_proc(os.readlink, "/proc/%d/exe" % pid)))
                cmds.append(open("/proc/%d/cmdline" % pid, "rb").read()
                            .decode("utf-8", "replace").replace("\x00", " "))
            except OSError:
                pass
        report["server_exe"] = exes
        report["server_cmdline"] = cmds
        leaked = {}
        for pid in pids:
            got = env_leak(pid)
            if got:
                leaked[pid] = got
        report["server_env_leak"] = leaked
        report["server_uid_observed"] = {p: real_uid(p) for p in pids}
        if UNPRIV and any(u != UNPRIV for u in report["server_uid_observed"].values()):
            report["liveness_error"] = (
                "the process holding the listening socket runs as uid %r, not as "
                "the unprivileged uid %d" % (report["server_uid_observed"], UNPRIV))
            return False
        if leaked:
            report["liveness_error"] = (
                "the candidate's server can read %s -- those name every scored "
                "test and the file this verifier counts, so any score from this "
                "run is untrustworthy"
                % sorted({k for v in leaked.values() for k in v}))
            return False
        if not exes:
            report["liveness_error"] = "could not identify the listening process"
            return False
        for exe in exes:
            if os.path.basename(exe) != "java":
                report["liveness_error"] = ("the process holding the listening socket is %r, "
                                            "not a JVM" % exe)
                return False
        # the JVM must be running classes this build produced, not a shipped jar
        fresh = fresh_class_files(report)
        if not fresh:
            report["liveness_error"] = "make produced no class files under /app"
            return False
        report["fresh_class_files"] = len(fresh)
        if not any("/app" in c for c in cmds):
            report["liveness_error"] = ("the JVM does not load anything from /app: %r" % cmds)
            return False
        # Nothing that serves the protocol may be a C redis loaded into this
        # process: a shared object dropped anywhere and opened through JNI or a
        # panama binding would answer RESP without being this implementation.
        suspicious = []
        for pid in pids:
            try:
                maps = _proc(_read_bytes, "/proc/%d/maps" % pid).decode("utf-8", "replace")
            except OSError:
                continue
            for line in maps.splitlines():
                path = line.split(" ", 5)[-1].strip()
                if not path.startswith("/"):
                    continue
                base = os.path.basename(path).lower()
                if any(k in base for k in ("redis", "hiredis", "valkey", "keydb",
                                           "dragonfly", "memcached")):
                    suspicious.append(path)
        if suspicious:
            report["liveness_error"] = ("the serving process has a redis-like "
                                        "native library mapped: %r" % sorted(set(suspicious))[:5])
            return False
        report["mapped_libraries_checked"] = len(pids)
        return True
    finally:
        stop_server(proc)
        sweep_candidate()


# ----------------------------------------------------------------- suites ---# ----------------------------------------------------------------- suites ---
def selection_names(sel, report):
    """(names, error) for a scored-test selection list.

    Same hazard as the fixture manifests: these are per-step files that share a
    relative path across steps, and docker's build-context sync treats
    (path, size, mtime) as identity while tar rounds mtime to the second and a
    git checkout flattens it entirely -- so SIZE is the only thing that keeps
    two siblings apart. `selection/B0.txt` was 6582 bytes at steps 06 and 10
    with different contents, and 6584 at steps 09 and 11, which would have
    graded a rung against another rung's scored set without a trace. Each file
    now opens with `# step: <step>` and is padded so its size mod 16 is its own
    step ordinal, which no sibling can match; this reads the header so a
    recurrence is loud rather than plausible.
    """
    text = expect_text("selection", sel)
    owner = None
    for ln in text.splitlines():
        t = ln.strip()
        if t.startswith("# step:"):
            owner = t.split(":", 1)[1].strip()
            break
    names = [l.strip() for l in text.splitlines()
             if l.strip() and not l.strip().startswith("#")]
    if owner is not None and owner != report.get("step"):
        return names, ("selection %s carries step %r, but this is step %r -- a "
                       "sibling step's selection list was delivered into this "
                       "image" % (sel, owner, report.get("step")))
    report.setdefault("selections_used", {})[sel] = owner
    return names, None


def run_suite(comp, report):
    tag = comp["suite"]
    src = SUITES / tag
    dst = WORK / ("suite-" + comp["id"] + "-" + tag)
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst, symlinks=True)
    bad = check_suite_copy(tag, dst)
    if bad:
        report.setdefault("components", {})[comp["id"]] = {
            "kind": "suite", "suite": tag, "expected": 0, "passed": 0,
            "fraction": 0.0, "error": bad}
        return 0.0
    (dst / "src").mkdir(exist_ok=True)
    _server_wrapper(dst / "src" / "redis-server", APP / "src" / "redis-server")
    _open_scratch(dst)

    names, selerr = selection_names(comp["selection"], report)
    if selerr:
        report.setdefault("components", {})[comp["id"]] = {
            "kind": "suite", "suite": tag, "expected": 0, "passed": 0,
            "fraction": 0.0, "error": selerr}
        say("SUITE %s: %s" % (comp["id"], selerr))
        return 0.0
    expected = set(names)

    files = comp.get("files")
    if not files:
        raise RuntimeError("component %s has no 'files': the suite would run "
                           "nothing and score 0 silently" % comp["id"])
    sdir = _score_dir(comp)
    reslog = sdir / "harbor-results.tsv"
    # The Tcl harness needs the scored names as a FILE, and it gets one written
    # from the preloaded copy rather than a path into /tests. Two birds: the
    # `sel` binding this replaced no longer exists (preloading removed it, and
    # leaving this line pointing at it crashed every suite component with
    # NameError), and the file the harness reads is now derived from memory the
    # candidate cannot reach instead of from a directory it can write.
    onlyf = sdir / "harbor-only.txt"
    onlyf.write_text("\n".join(names) + "\n")
    env = dict(os.environ)
    env["HARBOR_ONLY_FILE"] = str(onlyf)
    env["HARBOR_RESULT_LOG"] = str(reslog)
    env["HARBOR_FILES"] = " ".join(files)
    env["TERM"] = "dumb"
    if "--tls" in comp.get("extra_args", []):
        gen = dst / "utils" / "gen-test-certs.sh"
        if gen.exists():
            subprocess.run(["bash", str(gen)], cwd=str(dst), capture_output=True,
                           timeout=max(30, min(300, int(DEADLINE - time.time()))))
            # openssl writes the keys 0600; the server reading them is UNPRIV
            subprocess.run(["chmod", "-R", "a+rX", str(dst / "tests" / "tls")],
                           capture_output=True)
    args = ["./runtest", "--clients", str(comp.get("clients", 16)),
            "--timeout", str(comp.get("test_timeout", 300))]
    args += comp.get("extra_args", [])
    t0 = time.time()
    try:
        budget = max(60, min(comp.get("wall_timeout", 1500), int(DEADLINE - time.time())))
        # umask 0: the harness (root) creates each server's directory, and the
        # server that has to write in it is UNPRIV.
        p = subprocess.run(args, cwd=str(dst), env=env, capture_output=True,
                           timeout=budget, umask=0)
        rc = p.returncode
        out = (p.stdout[-20000:] + b"\n" + p.stderr[-8000:]).decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e:
        rc = -1
        out = "TIMEOUT after %ss\n" % comp.get("wall_timeout", 1500)
        out += (e.stdout or b"")[-20000:].decode("utf-8", "replace")
    sweep_candidate()
    passed = set()
    for shard in _shards(sdir, "harbor-results.tsv"):
        for line in shard.read_text(errors="replace").splitlines():
            f = line.split("\t")
            if len(f) >= 2 and f[0] == "ok":
                # redis has test names with trailing whitespace ("ZUNIONSTORE/
                # ZINTERSTORE/ZDIFFSTORE error if using WITHSCORES "). A line of
                # a selection file cannot carry that, so both sides are stripped
                # -- otherwise such a test is in `expected` and can never match.
                passed.add(f[1].strip())
    got = passed & expected
    frac = len(got) / len(expected) if expected else 0.0
    CASES[comp["id"]] = (set(expected), set(got))
    try:
        (LOG / ("results-%s.tsv" % comp["id"])).write_text(
            "".join("%s\t%s\n" % ("ok" if n in passed else "err", n)
                    for n in sorted(expected)))
    except OSError:
        pass
    report.setdefault("components", {})[comp["id"]] = {
        "kind": "suite", "suite": tag, "expected": len(expected),
        "passed": len(got), "fraction": round(frac, 6),
        "seconds": round(time.time() - t0, 1), "runtest_rc": rc,
        "missing": sorted(expected - got)[:80],
        "tail": out[-6000:],
    }
    return frac


def run_instances(comp, report):
    """Redis's multi-instance harness: ./runtest-cluster and ./runtest-sentinel."""
    tag = comp["suite"]
    src = SUITES / tag
    dst = WORK / ("inst-" + comp["id"] + "-" + tag)
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst, symlinks=True)
    bad = check_suite_copy(tag, dst)
    if bad:
        report.setdefault("components", {})[comp["id"]] = {
            "kind": "instances", "suite": tag, "expected": 0, "passed": 0,
            "fraction": 0.0, "error": bad}
        return 0.0
    (dst / "src").mkdir(exist_ok=True)
    for name in ("redis-server", "redis-sentinel"):
        link = dst / "src" / name
        if link.exists() or link.is_symlink():
            link.unlink()
        target = APP / "src" / name
        if target.exists():
            _server_wrapper(link, target)
    _open_scratch(dst)
    # The multi-instance suites run many units and reuse test names across them
    # ("Cluster should eventually be up again" appears in 25 different units), so
    # a bare name is not a scored unit: it would count as passed the moment any
    # one unit passed it. Every line here is "<unit>::<name>"; the adapter still
    # filters by name, so a name-only copy goes to HARBOR_ONLY_FILE.
    _names, selerr = selection_names(comp["selection"], report)
    if selerr:
        report.setdefault("components", {})[comp["id"]] = {
            "kind": "instances", "suite": tag, "expected": 0, "passed": 0,
            "fraction": 0.0, "error": selerr}
        say("INSTANCES %s: %s" % (comp["id"], selerr))
        return 0.0
    expected = set(_names)
    names_only = sorted({e.split("::", 1)[1] if "::" in e else e for e in expected})
    sdir = _score_dir(comp)
    onlyf = sdir / "harbor-only.txt"
    onlyf.write_text("\n".join(names_only) + "\n")
    reslog = sdir / ("harbor-%s.tsv" % comp["id"])
    env = dict(os.environ)
    env["HARBOR_ONLY_FILE"] = str(onlyf)
    env["HARBOR_RESULT_LOG"] = str(reslog)
    env["TERM"] = "dumb"
    runner = "./" + comp["runner"]
    t0 = time.time()
    try:
        budget = max(60, min(comp.get("wall_timeout", 1500), int(DEADLINE - time.time())))
        p = subprocess.run([runner], cwd=str(dst), env=env, capture_output=True,
                           timeout=budget, umask=0)
        rc, out = p.returncode, (p.stdout[-20000:] + b"\n" + p.stderr[-8000:]).decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e:
        rc, out = -1, "TIMEOUT\n" + (e.stdout or b"")[-20000:].decode("utf-8", "replace")
    sweep_candidate()
    passed = set()
    for shard in _shards(sdir, reslog.name):
        for line in shard.read_text(errors="replace").splitlines():
            f = line.split("\t")
            if len(f) >= 2 and f[0] == "ok":
                unit = f[2].strip() if len(f) > 2 else "?"
                passed.add(unit + "::" + f[1].strip())
    got = passed & expected
    frac = len(got) / len(expected) if expected else 0.0
    CASES[comp["id"]] = (set(expected), set(got))
    try:
        (LOG / ("results-%s.tsv" % comp["id"])).write_text(
            "".join("%s\t%s\n" % ("ok" if n in passed else "err", n)
                    for n in sorted(expected)))
    except OSError:
        pass
    report.setdefault("components", {})[comp["id"]] = {
        "kind": "instances", "suite": tag, "runner": comp["runner"],
        "expected": len(expected), "passed": len(got), "fraction": round(frac, 6),
        "seconds": round(time.time() - t0, 1), "rc": rc,
        "missing": sorted(expected - got)[:60], "tail": out[-6000:],
    }
    return frac


def run_corpus(comp, report):
    """Replay a recorded command/reply corpus against the candidate.

    Every expected reply was recorded from a real Redis 8.0 server with the
    bundled modules loaded (see the corpus PROVENANCE). The replay uses exactly
    the RESP decoding, JSON encoding and normalisers the recorder used, so what
    is compared is what the reference actually said.
    """
    import modcorpus_lib as MC
    corpus = json.loads(expect_text("corpus", comp["corpus"]))
    if not corpus:
        report.setdefault("components", {})[comp["id"]] = {
            "kind": "corpus", "expected": 0, "passed": 0, "fraction": 0.0,
            "error": "empty corpus"}
        return 0.0
    d = WORK / ("corpus-" + comp["id"])
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True, exist_ok=True)
    port = free_port()
    conf = d / "redis.conf"
    lines = ["port %d" % port, "bind 127.0.0.1", "dir %s" % d,
             "logfile %s" % (d / "log"), "save ''"]
    lines += comp.get("config", [])
    conf.write_text("\n".join(lines) + "\n")
    open_dir(d)
    proc = subprocess.Popen(server_cmd(conf), env=clean_env(),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ok, details, groups = 0, [], {}
    ok_ids = set()
    t0 = time.time()
    try:
        if wait_ready("127.0.0.1", port, time.time() + 90) is None:
            report.setdefault("components", {})[comp["id"]] = {
                "kind": "corpus", "expected": len(corpus), "passed": 0,
                "fraction": 0.0, "error": "server did not start"}
            return 0.0
        if not own_server_ok(proc, port, report, "corpus:" + comp["id"]):
            report.setdefault("components", {})[comp["id"]] = {
                "kind": "corpus", "expected": len(corpus), "passed": 0,
                "fraction": 0.0,
                "error": "another process answered on the corpus port"}
            return 0.0
        r = MC.Resp(host="127.0.0.1", port=port)
        if comp.get("cluster"):
            # A cluster-enabled node owns no slots when it starts and answers
            # every keyed command with -CLUSTERDOWN, so the transcript would
            # measure "is the cluster up" and nothing else. One node owning all
            # 16384 slots is a legal cluster and is what this transcript was
            # recorded against; the same two commands ran before the recording.
            r.cmd("CLUSTER", "SET-CONFIG-EPOCH", "1")
            r.cmd("CLUSTER", "ADDSLOTS", *[str(i) for i in range(16384)])
            ready = False
            for _ in range(60):
                try:
                    if "cluster_state:ok" in (r.cmd("CLUSTER", "INFO")[1] or ""):
                        ready = True
                        break
                except Exception:                                   # noqa: BLE001
                    pass
                time.sleep(0.5)
            if not ready:
                report.setdefault("components", {})[comp["id"]] = {
                    "kind": "corpus", "expected": len(corpus), "passed": 0,
                    "fraction": 0.0,
                    "error": "the node never reached cluster_state:ok after "
                             "being given all 16384 slots"}
                return 0.0
        dumps = {}
        for entry in corpus:
            if time.time() > DEADLINE:
                break
            cmd = entry["cmd"]
            try:
                reply = r.cmd(*MC.substitute(cmd, dumps))
            except Exception as e:  # noqa: BLE001
                reply = ("error", "CLIENT-EXCEPTION %s" % e)
                try:
                    r = MC.Resp(host="127.0.0.1", port=port)
                except Exception:  # noqa: BLE001
                    break
            if cmd and cmd[0].upper() == "DUMP" and reply[0] == "bulk":
                dumps[cmd[1]] = reply[1]
            got = MC.normalize(MC.enc(reply), entry["normalize"])
            g = groups.setdefault(entry["group"], [0, 0])
            g[1] += 1
            if matches(entry["reply"], got):
                ok += 1
                g[0] += 1
                ok_ids.add(entry.get("id"))
            elif len(details) < 60:
                # no "want": the packaged corpus holds a digest, and printing the
                # expected value into the report would put it back on this
                # filesystem. Diagnose against the authoring copy.
                details.append({"id": entry.get("id"), "group": entry.get("group"),
                                "cmd": cmd, "got": MC.render(got)[:300]})
        r.close()
    finally:
        stop_server(proc)
        sweep_candidate()
    frac = ok / len(corpus)
    CASES[comp["id"]] = ({e.get("id") for e in corpus}, ok_ids)
    report.setdefault("components", {})[comp["id"]] = {
        "kind": "corpus", "expected": len(corpus), "passed": ok,
        "fraction": round(frac, 6), "seconds": round(time.time() - t0, 1),
        "by_group": groups, "mismatches": details}
    return frac


# --------------------------------------------------------------- fixtures ---
def dump_dataset(r, dbs):
    """Deterministic dump of the whole dataset through the public command surface."""
    out = {}
    for db in dbs:
        r.cmd("SELECT", db)
        keys = sorted(r.cmd("KEYS", "*") or [], key=lambda x: x)
        ent = []
        for k in keys:
            t = r.cmd("TYPE", k)
            t = t.decode() if isinstance(t, bytes) else t
            ttl = r.cmd("PTTL", k)
            item = {"k": k.decode("latin-1"), "t": t, "ttl": "set" if ttl and ttl > 0 else str(ttl)}
            if t == "string":
                item["v"] = (r.cmd("GET", k) or b"").decode("latin-1")
            elif t == "list":
                item["v"] = [x.decode("latin-1") for x in r.cmd("LRANGE", k, 0, -1)]
            elif t == "set":
                item["v"] = sorted(x.decode("latin-1") for x in r.cmd("SMEMBERS", k))
            elif t == "zset":
                raw = r.cmd("ZRANGE", k, 0, -1, "WITHSCORES")
                item["v"] = [x.decode("latin-1") for x in raw]
            elif t == "hash":
                raw = r.cmd("HGETALL", k)
                pairs = sorted(zip(raw[0::2], raw[1::2]))
                item["v"] = [[a.decode("latin-1"), b.decode("latin-1")] for a, b in pairs]
            elif t == "stream":
                raw = r.cmd("XRANGE", k, "-", "+")
                item["v"] = _flatten(raw)
            else:
                item["v"] = "?"
            ent.append(item)
        if ent:
            out[str(db)] = ent
    return out


def _flatten(x):
    if isinstance(x, bytes):
        return x.decode("latin-1")
    if isinstance(x, list):
        return [_flatten(i) for i in x]
    return x


def run_fixtures(comp, report):
    specs = json.loads(expect_text("fixtures", comp["manifest"]))
    # Every spec names the step that owns it, and this refuses to grade one
    # that belongs to a sibling.
    #
    # These manifests are per-step files that used to share a name AND a size
    # across six steps (fixtures/manifest-own.json, 605 bytes, six distinct
    # contents). Docker's build-context sync treats (path, size, mtime) as
    # identity, and tar's default format rounds mtime to the second, so a
    # sibling step's manifest could be -- and was -- delivered into this image:
    # in the 2026-09-18 wave steps 04, 05 and 06 of one run loaded step 07's
    # manifest and failed with "fixture rdb-6.0.20.rdb was not preloaded",
    # while another run's steps 05 and 06 loaded step 04's manifest, found the
    # 3.2.13 fixtures present as carry-forward, and were awarded full credit for
    # loading an RDB three releases older than the rung under test. The file
    # names are unique per step now, which is what actually prevents it; this
    # check makes any recurrence loud instead of a plausible number.
    owner = report.get("step")
    wrong = sorted({s.get("step") for s in specs if s.get("step") != owner})
    if wrong:
        report.setdefault("components", {})[comp["id"]] = {
            "kind": "fixtures", "suite": None, "expected": len(specs),
            "passed": 0, "fraction": 0.0,
            "error": "manifest %s carries step %r, but this is step %r -- a "
                     "sibling step's manifest was delivered into this image"
                     % (comp["manifest"], wrong, owner)}
        say("FIXTURES %s: %s does not belong to this step (%r) -- scoring 0 "
            "rather than grading the wrong release's fixtures"
            % (comp["id"], comp["manifest"], wrong))
        return 0.0
    report.setdefault("manifests_used", {})[comp["id"]] = comp["manifest"]
    t0 = time.time()
    ok = 0
    ok_names = set()
    details = []
    for spec in specs:
        name = spec["name"]
        if out_of_time():
            # the denominator stays len(specs): an unreached fixture is a miss
            details.append({"name": name, "ok": False,
                            "error": "verifier budget exhausted"})
            continue
        d = WORK / ("fx-" + comp["id"] + "-" + name)
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True, exist_ok=True)
        names = spec.get("copy_as") or [Path(f).name for f in spec["files"]]
        tampered = None
        # `as_` rather than `name`: the fixture's own name is needed below for
        # the report and the liveness label, and shadowing it here labelled every
        # fixture with the last file copied into its directory.
        for f, as_ in zip(spec["files"], names):
            srcp = TESTS / "fixtures" / f
            if srcp.is_dir():
                bad = (None if FIX_DIRS.get(f) == _tree_digest(srcp) else
                       "fixture dir %s was modified after the verifier started" % f)
                if bad:
                    tampered = bad
                    break
                shutil.copytree(srcp, d / as_)
            else:
                # written from the copy taken before the candidate ran
                blob = EXPECT.get(("fixtures", f))
                if blob is None:
                    tampered = "fixture %s was not preloaded" % f
                    break
                (d / as_).write_bytes(blob)
        if tampered:
            # Do not start a server on a half-populated directory: that scores
            # the candidate on a fixture nobody assembled.
            details.append({"name": name, "ok": False, "error": tampered})
            continue
        port = free_port()
        conf = d / "redis.conf"
        lines = ["port %d" % port, "bind 127.0.0.1", "dir %s" % d,
                 "logfile %s" % (d / "log"), "save ''"]
        lines += spec.get("config", [])
        conf.write_text("\n".join(lines) + "\n")
        open_dir(d)
        proc = subprocess.Popen(server_cmd(conf), env=clean_env(),
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        entry = {"name": name}
        try:
            r = wait_ready("127.0.0.1", port, time.time() + 90)
            if r is None:
                entry["error"] = "server did not start"
                try:
                    entry["log"] = (d / "log").read_text()[-1500:]
                except OSError:
                    pass
            elif not own_server_ok(proc, port, report, "fixture:" + name):
                entry["error"] = "another process answered on the fixture port"
                r.close()
            else:
                got = dump_dataset(r, spec.get("dbs", [0]))
                r.close()
                want = spec["expect"]
                if matches(want, got):
                    ok += 1
                    ok_names.add(name)
                    entry["ok"] = True
                else:
                    entry["ok"] = False
                    entry["diff"] = (first_diff(want, got)
                                     if not (isinstance(want, dict)
                                             and set(want) == {"sha256"})
                                     else "dataset digest mismatch")
        except Exception as e:  # noqa: BLE001
            entry["error"] = repr(e)
        finally:
            try:
                proc.send_signal(signal.SIGTERM)
                proc.wait(timeout=20)
            except Exception:
                proc.kill()
            sweep_candidate()
        details.append(entry)
    frac = ok / len(specs) if specs else 0.0
    CASES[comp["id"]] = ({s["name"] for s in specs}, ok_names)
    report.setdefault("components", {})[comp["id"]] = {
        "kind": "fixtures", "expected": len(specs), "passed": ok,
        "fraction": round(frac, 6), "seconds": round(time.time() - t0, 1),
        "details": details,
    }
    return frac


def first_diff(want, got):
    for db in sorted(set(want) | set(got)):
        w = {e["k"]: e for e in want.get(db, [])}
        g = {e["k"]: e for e in got.get(db, [])}
        for k in sorted(set(w) | set(g)):
            if w.get(k) != g.get(k):
                return {"db": db, "key": k, "want": w.get(k), "got": g.get(k)}
    return {"note": "structures differ"}


# ------------------------------------------------------------------ main ----
# The step's verifier timeout is 3600 s, and Harbor DISCARDS a verifier that
# outlives it (and aborts every later step). No new component starts after this
# budget; the last one gets at most its 60 s floor plus teardown, and test.sh
# kills this process at 3330 s as the backstop (wave-A 2026-10-12: was 3300,
# which let the floor and teardown run past 3360).
DEADLINE = time.time() + float(os.environ.get("HARBOR_VERIFIER_BUDGET", "3150"))


def out_of_time():
    return time.time() > DEADLINE


def main():
    LOG.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    # traversable, not listable, by the candidate's uid (see _score_dir)
    os.chmod(WORK, 0o711)
    report = {}
    preload_expectations(report)
    step = json.loads(expect_text("", "step.json"))
    report["step"] = step["step"]
    reward = 0.0
    watch = None
    try:
        if UNPRIV and not (os.geteuid() == 0 and _have_setpriv()):
            # never fall back to running the candidate as root
            raise RuntimeError("cannot drop to uid %d (euid %d, setpriv %s)"
                               % (UNPRIV, os.geteuid(), _have_setpriv()))
        sealed = seal_expectations(report)
        if UNPRIV and not sealed:
            say("NOTE: the expectation files are still readable by the "
                "unprivileged uid (%r)" % report.get("unpriv_can_still_read"))
        built = build(report) and (open_app() or True)
        if built:
            watch = ProcWatch()
            watch.start()
        live = built and check_jvm(report)
        if live:
            # Cheap components first, then the suites in their declared order.
            #
            # Components are independent -- each starts its own servers in its
            # own directory -- so the order is free to choose, and first-come
            # first-served is the wrong choice. The corpora and fixtures take
            # seconds; the suites take minutes and can take their whole cap when
            # a candidate HANGS rather than fails. Summed, the per-component
            # caps exceed the verifier's deadline at nine of the eleven steps
            # (12000s against 3300 at step 11), so in declared order a server
            # that hangs in the first suite silently forfeits the carry-forward
            # fixtures -- the components that measure whether earlier rungs
            # still work, which is the whole claim of a chain. Observed once
            # already: C and Cp came back `skipped` on a diagnostic run.
            #
            # Ordering cheap-first makes that impossible: the fixtures and
            # transcripts are always reached, and the suites can now only starve
            # each other -- which the reserve below bounds.
            ordered = component_order(step["components"])
            # BOTH harness kinds are counted here. `instances` -- the cluster
            # and sentinel harnesses, twenty servers each -- was outside this
            # accounting, so it was neither clamped by the reserve nor counted
            # in anyone else's. At step 2 (S, wall 1500) and step 3 (A, wall
            # 1800) an `instances` component runs before a suite, so either
            # could take the whole remaining budget and drive the suite after
            # it to the 60 s floor in run_suite(), scoring the candidate on the
            # clock rather than on its work.
            HARNESS = ("suite", "instances")
            harness_left = sum(1 for c in ordered if c["kind"] in HARNESS)
            results = []
            for comp in ordered:
                w = comp["weight"]
                fam = comp.get("family", "T1")
                if fam not in ("T1", "T2"):
                    raise ValueError("component %s declares family %r" % (comp["id"], fam))
                if comp["kind"] in HARNESS:
                    harness_left -= 1
                    # leave the harness components after this one a floor of their own
                    RESERVE = 240
                    comp = dict(comp, wall_timeout=max(
                        60, min(comp.get("wall_timeout", 1500),
                                int(DEADLINE - time.time()) - RESERVE * harness_left)))
                if out_of_time():
                    # A skipped component contributes 0 and the reward simply
                    # comes out lower -- which is a measurement of the clock,
                    # not of the candidate, and looks exactly like one of the
                    # candidate. Record it AND say it, so a reader of the log
                    # does not have to notice an absence.
                    report.setdefault("components", {}).setdefault(comp["id"], {})[
                        "skipped"] = "verifier budget exhausted"
                    report["budget_exhausted"] = True
                    say("SKIPPED component %s (weight %.2f): the verifier budget "
                        "ran out before it started. Its weight is lost from this "
                        "step's reward, and that loss is about the budget, not "
                        "about the candidate." % (comp["id"], w))
                    results.extend(family_parts(comp, 0.0, report))
                    continue
                # one broken component must never cost the others their score
                try:
                    if comp["kind"] == "suite":
                        frac = run_suite(comp, report)
                    elif comp["kind"] == "corpus":
                        frac = run_corpus(comp, report)
                    elif comp["kind"] == "instances":
                        frac = run_instances(comp, report)
                    elif comp["kind"] == "fixtures":
                        frac = run_fixtures(comp, report)
                    else:
                        frac = 0.0
                except Exception:  # noqa: BLE001
                    import traceback
                    frac = 0.0
                    CASES.pop(comp["id"], None)   # a component that throws scores 0, both parts
                    report.setdefault("components", {}).setdefault(comp["id"], {})[
                        "exception"] = traceback.format_exc()[-2000:]
                results.extend(family_parts(comp, frac, report))
            reward = product_reward(results, report)
        else:
            say("build/liveness failed: " + json.dumps(
                {k: v for k, v in report.items() if k.endswith("error")}))
        if watch is not None and not watch.stop(report, require_jvm=live):
            reward = 0.0
            say("process watch: " + report["foreign_process_error"])
        watch = None
    except Exception as e:  # noqa: BLE001
        import traceback
        report["verifier_exception"] = traceback.format_exc()
        reward = 0.0
    if watch is not None:
        watch.stop(report)
    report["final_swept"] = sweep_candidate()
    reward = max(0.0, min(1.0, reward))
    report["reward"] = round(reward, 6)
    (LOG / "report.json").write_text(json.dumps(report, indent=1, default=str))
    # reward.json holds the "reward" key and nothing else: Harbor validates the
    # whole file as dict[str, float | int], so a string companion key fails
    # validation and a numeric one is averaged across steps as a second reward.
    # The provenance of this number goes in its own file, which test.sh checks
    # on exit -- a zero from a dead verifier and a zero from a candidate that
    # passed nothing are the same number and different facts.
    # The reward leaves this process on STDOUT, carrying the nonce, and
    # `test.sh` writes the file -- last, after sweeping the candidate's
    # processes. A file this function writes can be overwritten by anything
    # still running in the container, and the candidate is a SERVER: a detached
    # child that loops `echo '{"reward": 1.0}' > reward.json` outlives the
    # scorer and writes after it. smb demonstrated exactly that against a real
    # verifier and got 1.0 for a service implementing nothing.
    #
    # The file is still written here so a killed test.sh leaves something
    # parseable, but it is not the authority: stdout is.
    say("HARBOR_REWARD %s %.6f"
        % (os.environ.get("HARBOR_SCORE_NONCE", "none"), round(reward, 6)))
    (LOG / "reward.json").write_text(json.dumps({"reward": round(reward, 6)}))
    # Echo back the nonce test.sh invented, not a constant: a constant is
    # forgeable by anything that can write /logs/verifier, and the candidate's
    # build can (matrix's finding, NOTES 58). The candidate never sees this
    # value -- clean_env() strips every HARBOR_* variable from the build and
    # from every server this verifier starts.
    (LOG / "scored_by").write_text(os.environ.get("HARBOR_SCORE_NONCE", "run.py"))
    say(json.dumps({k: v for k, v in report.items()
                    if k in ("step", "reward", "families", "build_error", "liveness_error",
                             "foreign_process_error")}))
    for cid, c in (report.get("components") or {}).items():
        say("  component %s: %s/%s = %.3f" % (cid, c.get("passed"), c.get("expected"),
                                              c.get("fraction", 0)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
