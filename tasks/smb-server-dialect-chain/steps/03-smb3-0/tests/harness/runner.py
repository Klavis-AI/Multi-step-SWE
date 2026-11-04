#!/usr/bin/env python3
"""Scoring harness: starts the candidate's smbd under a scenario's smb.conf and
runs the scored groups against it through the SMB wire protocol.

Every configuration file, share tree and account database used for scoring is
created here, in the verifier's own directory tree; nothing under /app is
trusted or executed except the service itself.
"""
import json
import os
import re
import shutil
import signal
import socket
import stat
import subprocess
import sys
import threading
import time

APP = "/app"
SCRATCH = "/scratch"
# The shares live outside SCRATCH/<scenario>, which the service account owns:
# a share's parent must be root's, so nothing the server runs can swap the
# share directory for a link while root seeds it.
SHARE_ROOT = "/srv/smb-shares"
TESTS = os.path.dirname(os.path.abspath(__file__))
LOGDIR = "/logs/verifier"

# The service runs as nobody, not as the scorer's root: test.sh locks
# /logs/verifier to root 0700, and that only binds a process that is not root.
# Keep in step with SERVICE_UID in helpers.sh.
SERVICE_UID = 65534


def _live_service_pids():
    """Live (non-zombie) processes owned by SERVICE_UID, read from /proc."""
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
        if m_uid and int(m_uid.group(1)) == SERVICE_UID and \
                not (m_st and m_st.group(1) in "ZX"):
            out.append(int(d))
    return out


def reap_service_uid(limit=15.0):
    """Kill every process of SERVICE_UID, looping until none is left (bounded).

    `kill -KILL -1` sent as that uid reaches all of them in one pass that new
    forks cannot race; pkill catches the rest. Returns how many are left."""
    if os.getuid() != 0:
        return 0
    end = time.time() + limit
    live = _live_service_pids()
    while live and time.time() < end:
        for c in (["setpriv", "--reuid", str(SERVICE_UID), "--regid", str(SERVICE_UID),
                   "--clear-groups", "bash", "-c", "kill -KILL -1"],
                  ["pkill", "-KILL", "-u", str(SERVICE_UID)]):
            try:
                subprocess.run(c, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=5)
            except Exception:
                pass
        time.sleep(0.2)
        live = _live_service_pids()
    if live:
        log("WARNING: %d process(es) of uid %d survived the reap" % (len(live), SERVICE_UID))
    return len(live)

USER = "smbuser"
PASSWORD = "smbpass1!"
# NT hash of the password above, in the smbpasswd(5) layout, as Samba's own
# `smbpasswd -a` writes it.
SMBPASSWD_LINE = ("smbuser:1000:XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX:"
                  "52B9C753DCB34317A65F6A7167C8D86E:[U          ]:LCT-6AA4DA1C:")

BINARY_MAGIC = (b"\x7fELF", b"MZ", b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe",
                b"\xfe\xed\xfa\xcf", b"\xfe\xed\xfa\xce", b"\xca\xfe\xba\xbe")


DEADLINE = [None]
GROUP_DEADLINE = [None]

# What every group still to run is guaranteed, whatever the groups before it do.
# A group that cannot answer is bounded by its own guards, but those are
# heuristics: a service that hangs on some suites and fails fast on others resets
# the consecutive-timeout counter and can still consume the whole budget. This
# makes the guarantee structural instead -- no group may spend the reserve owed
# to the groups after it -- so a rung's carry-forward groups cannot be starved by
# its own new-dialect groups however the candidate misbehaves.
# A flat reserve per group cannot be right for groups that differ by two orders
# of magnitude in cost: 90 s is ample for 36 smbclient cases and not enough for
# 65 smbtorture tests across fifteen suites.
#
# So the share is proportional to the work: before each group, it may use the
# remaining budget scaled by its share of the cases still to be scored. A group
# that finishes early leaves the surplus to the rest, a group that hangs is
# bounded by its own share, and no constant has to be guessed.
MIN_GROUP_SECONDS = 30.0

# Which group is being scored, and how many of its cases the clock -- not the
# candidate -- is responsible for. A starved case still counts as a failure: a
# service too slow to be scored has not earned the credit. But the REPORT has to
# say which, because `torture-nt1-regression 0/65` on a carry-forward group
# looks exactly like a regression in what already worked, and a reader has to
# be able to tell the two apart.
CURRENT_GROUP = [None]
BUDGET_SECONDS = [None]
STARVED = {}


def note_starved(n=1):
    if CURRENT_GROUP[0] is not None:
        STARVED[CURRENT_GROUP[0]] = STARVED.get(CURRENT_GROUP[0], 0) + n


def start_budget(seconds):
    DEADLINE[0] = time.time() + seconds
    BUDGET_SECONDS[0] = seconds
    GROUP_DEADLINE[0] = None


def start_group(cases_this, cases_remaining, name=None):
    """Cap this group at its share, by case count, of what is left.

    `cases_remaining` counts this group's cases as well as those after it.
    """
    CURRENT_GROUP[0] = name
    if DEADLINE[0] is None:
        return
    left = max(0.0, DEADLINE[0] - time.time())
    share = left if cases_remaining <= 0 else left * (float(cases_this) / cases_remaining)
    GROUP_DEADLINE[0] = time.time() + max(MIN_GROUP_SECONDS, share)


def end_group():
    GROUP_DEADLINE[0] = None
    CURRENT_GROUP[0] = None


def budget_left():
    if DEADLINE[0] is None:
        return None
    left = DEADLINE[0] - time.time()
    if GROUP_DEADLINE[0] is not None:
        left = min(left, GROUP_DEADLINE[0] - time.time())
    return max(0.0, left)


def budget_exhausted():
    return DEADLINE[0] is not None and budget_left() <= 0.0


def log(msg):
    sys.stderr.write(str(msg) + "\n")
    sys.stderr.flush()


# --------------------------------------------------------------------------
# workspace preflight

# ELF e_type == ET_CORE: a crash dump written by the kernel, not a program.
ET_CORE = 4

# Crash dumps seen by the last scan_workspace(), reported but never fatal.
CORE_DUMPS = []


def elf_type(head):
    """The `e_type` of an ELF header, or None if *head* is not an ELF header.

    `e_type` is a two-byte field at offset 16 in both ELF32 and ELF64, in the
    byte order named by EI_DATA at offset 5.
    """
    if len(head) < 18 or head[:4] != b"\x7fELF":
        return None
    if head[5] == 1:            # ELFDATA2LSB
        return head[16] | (head[17] << 8)
    if head[5] == 2:            # ELFDATA2MSB
        return (head[16] << 8) | head[17]
    return None


def scan_workspace():
    """Reject compiled code and third-party runtime dependencies in /app.

    A crash dump is not compiled code and is not rejected. This chain's own
    instruction tells the candidate to drive its service with Samba's
    `smbtorture`, the image installs it, and both the candidate's shell and
    this harness run it with /app as the working directory. smbtorture
    segfaults readily against a part-built SMB server -- which is that
    server's normal state while it is being written -- and the kernel then
    drops a multi-megabyte ET_CORE image at /app/core. Reading that as "the
    candidate shipped a binary" would forfeit an entire step over a file the
    candidate never wrote and that says nothing about the deliverable.

    Skipping ET_CORE opens no hole. The kernel refuses to execve an ET_CORE
    image -- binfmt_elf accepts only ET_EXEC and ET_DYN -- and the dynamic
    loader refuses to dlopen one, so no runnable code can be smuggled in under
    that type. Everything that could actually execute is still rejected, and
    the separate checks that the listening socket is owned by this image's own
    `node` and that the service's process tree holds nothing else still
    apply.
    """
    problems = []
    cores = []
    for root, dirs, files in os.walk(APP):
        if ".git" in dirs:
            dirs.remove(".git")
        for d in list(dirs):
            if d == "node_modules":
                problems.append("third-party runtime dependency directory: %s" %
                                os.path.join(root, d))
                dirs.remove(d)
        for f in files:
            p = os.path.join(root, f)
            if f.endswith(".node"):
                problems.append("compiled native Node addon: %s -- the service "
                                "must be Node source run by this image's own "
                                "node; remove it from /app" % p)
                continue
            try:
                if os.path.getsize(p) < 4:
                    continue
                with open(p, "rb") as fh:
                    head = fh.read(18)
            except OSError:
                continue
            if elf_type(head) == ET_CORE:
                cores.append(p)
                continue
            # startswith(), not `head in BINARY_MAGIC`: the head read above is
            # longer than any magic number, and they differ in length (b"MZ" is
            # two bytes), so only a prefix match finds them.
            if head.startswith(BINARY_MAGIC):
                problems.append("compiled executable or shared object: %s -- a "
                                "built binary (ELF/PE/Mach-O), not a crash dump; "
                                "the graders build nothing and run only Node "
                                "source, so remove it from /app" % p)
    CORE_DUMPS[:] = cores
    for p in cores:
        log("workspace check: ignoring %s -- an ELF core file (a crash dump, "
            "most likely smbtorture's), not a shipped binary" % p)
    return problems


# The interpreter this image provides, resolved once. A candidate's service must
# be running under THIS binary, not merely under something named "node": a
# check on the basename alone would accept an interpreter the candidate shipped
# itself, and build products survive the artifact round trip intact.
def _image_node():
    for p in ("/usr/local/bin/node", "/usr/bin/node"):
        if os.path.exists(p):
            return os.path.realpath(p)
    found = shutil.which("node")
    return os.path.realpath(found) if found else None


IMAGE_NODE = _image_node()


def listening_exe(port):
    """The resolved path of the executable owning the listening socket on *port*."""
    inodes = set()
    for path in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            with open(path) as fh:
                next(fh, None)
                for line in fh:
                    f = line.split()
                    if len(f) < 10 or f[3] != "0A":
                        continue
                    if int(f[1].split(":")[1], 16) != port:
                        continue
                    inodes.add(f[9])
        except OSError:
            pass
    if not inodes:
        return None
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        fddir = "/proc/%s/fd" % pid
        try:
            fds = os.listdir(fddir)
        except OSError:
            continue
        for fd in fds:
            try:
                target = os.readlink(os.path.join(fddir, fd))
            except OSError:
                continue
            if target.startswith("socket:[") and target[8:-1] in inodes:
                try:
                    return os.path.realpath(os.readlink("/proc/%s/exe" % pid))
                except OSError:
                    return None
    return None


def listener_is_image_node(port):
    """(ok, what) for the process listening on *port*.

    ok is True only when that process is running the interpreter this image
    installed. `None` means nothing was listening, which the caller reports
    separately.
    """
    exe = listening_exe(port)
    if exe is None:
        return None, None
    if IMAGE_NODE and os.path.realpath(exe) == IMAGE_NODE:
        return True, exe
    return False, exe


# A candidate whose service leaks should score a failure, not take the verifier
# container down with it.  V8 reserves far more address space than it ever
# touches, so the ceiling is on resident memory, watched from outside, rather
# than an RLIMIT_AS that would refuse a correct implementation.
MEMORY_CEILING_MB = int(os.environ.get("SMB_MEMORY_CEILING_MB", "4096"))


def _tree_rss_kb(pid):
    total, pids = 0, [pid]
    for _ in range(5):
        children = []
        for p in pids:
            try:
                with open("/proc/%d/status" % p) as fh:
                    for line in fh:
                        if line.startswith("VmRSS:"):
                            total += int(line.split()[1])
                            break
            except OSError:
                continue
            try:
                with open("/proc/%d/task/%d/children" % (p, p)) as fh:
                    children.extend(int(x) for x in fh.read().split())
            except OSError:
                pass
        if not children:
            break
        pids = children
    return total


class MemoryWatchdog(threading.Thread):
    """Kill the service's process group if its resident memory passes a ceiling."""

    def __init__(self, pid, ceiling_mb, interval=1.0):
        threading.Thread.__init__(self)
        self.daemon = True
        self.pid = pid
        self.ceiling_kb = ceiling_mb * 1024
        self.interval = interval
        self.peak_mb = 0
        self.killed = False
        self._stop = threading.Event()

    def run(self):
        while not self._stop.wait(self.interval):
            try:
                os.kill(self.pid, 0)
            except OSError:
                return
            rss = _tree_rss_kb(self.pid)
            self.peak_mb = max(self.peak_mb, rss // 1024)
            if rss > self.ceiling_kb:
                self.killed = True
                log("service exceeded the %d MiB resident-memory ceiling (%d MiB); "
                    "stopping it\n" % (self.ceiling_kb // 1024, rss // 1024))
                try:
                    os.killpg(os.getpgid(self.pid), signal.SIGKILL)
                except OSError:
                    pass
                return

    def cancel(self):
        self._stop.set()


def process_tree_ok(pid):
    """Every process under the service must be the node runtime or a shell."""
    allowed = ("node", "sh", "bash", "dash", "env")
    seen = []
    try:
        pids = [pid]
        for _ in range(4):
            children = []
            for p in pids:
                try:
                    with open("/proc/%d/task/%d/children" % (p, p)) as fh:
                        children.extend(int(x) for x in fh.read().split())
                except OSError:
                    pass
            if not children:
                break
            pids = children
            seen.extend(children)
        seen.append(pid)
        bad = []
        for p in set(seen):
            try:
                exe = os.path.basename(os.readlink("/proc/%d/exe" % p))
            except OSError:
                continue
            if exe not in allowed:
                bad.append("%d:%s" % (p, exe))
        return bad
    except Exception:
        return []


# --------------------------------------------------------------------------
# scenario plumbing


class Scenario(object):
    def __init__(self, name, port, globals_extra=None, shares=None):
        self.name = name
        self.port = port
        self.globals_extra = globals_extra or {}
        self.shares = shares or {}
        self.base = os.path.join(SCRATCH, name)
        self.proc = None
        self.watchdog = None

    def build(self):
        for _ in range(5):
            if not os.path.isdir(self.base):
                break
            shutil.rmtree(self.base, ignore_errors=True)
            time.sleep(0.1)
        shutil.rmtree(self.base, ignore_errors=True)
        for d in ("private", "lock", "state", "cache"):
            os.makedirs(os.path.join(self.base, d), 0o755)
        with open(os.path.join(self.base, "private", "smbpasswd"), "w") as fh:
            fh.write(SMBPASSWD_LINE + "\n")
        lines = ["[global]"]
        base_globals = [
            ("workgroup", "WORKGROUP"),
            ("netbios name", "SMBSRV"),
            ("server string", "smbd"),
            ("security", "user"),
            ("passdb backend", "smbpasswd:%s/private/smbpasswd" % self.base),
            ("private dir", "%s/private" % self.base),
            ("lock directory", "%s/lock" % self.base),
            ("state directory", "%s/state" % self.base),
            ("cache directory", "%s/cache" % self.base),
            ("interfaces", "127.0.0.1/8"),
            ("bind interfaces only", "yes"),
            ("smb ports", str(self.port)),
            ("log file", "%s/log.smbd" % self.base),
            ("log level", "0"),
            ("map to guest", "never"),
        ]
        for k, v in base_globals:
            if k not in self.globals_extra:
                lines.append("\t%s = %s" % (k, v))
        for k, v in self.globals_extra.items():
            lines.append("\t%s = %s" % (k, v))
        share_parent = os.path.join(SHARE_ROOT, self.name)
        shutil.rmtree(share_parent, ignore_errors=True)
        os.makedirs(share_parent, 0o755)
        os.chmod(SHARE_ROOT, 0o755)
        os.chmod(share_parent, 0o755)
        for name, params in self.shares.items():
            path = self.share_path(name)
            os.makedirs(path, 0o777)
            os.chmod(path, 0o777)
            lines.append("")
            lines.append("[%s]" % name)
            lines.append("\tpath = %s" % path)
            for k, v in params.items():
                lines.append("\t%s = %s" % (k, v))
        conf = os.path.join(self.base, "smb.conf")
        with open(conf, "w") as fh:
            fh.write("\n".join(lines) + "\n")
        self.conf = conf
        # The service runs as SERVICE_UID (see start()): the directories smb.conf
        # tells it to write are its own; the configuration and the account
        # database stay root's and are only read.
        if os.getuid() == 0:
            for d in [self.base] + [os.path.join(self.base, x)
                                    for x in ("private", "lock", "state", "cache")]:
                try:
                    os.chown(d, SERVICE_UID, SERVICE_UID)
                except OSError:
                    pass
        return conf

    def share_path(self, name):
        return os.path.join(SHARE_ROOT, self.name, name)

    def start(self, timeout=25.0):
        out = open(os.path.join(self.base, "stdout.log"), "wb")
        # Unprivileged: setpriv execs the service in place (same pid), so the
        # process-tree and listener checks see the service process itself.
        argv = [os.path.join(APP, "bin", "smbd"), "--foreground", "--no-process-group",
                "--debug-stdout", "--debuglevel=1",
                "--configfile=%s" % self.conf, "--port=%d" % self.port]
        env = None
        if os.getuid() == 0:
            argv = ["setpriv", "--reuid", str(SERVICE_UID), "--regid", str(SERVICE_UID),
                    "--clear-groups", "--"] + argv
            env = dict(os.environ, HOME="/tmp")
        try:
            self.proc = subprocess.Popen(
                argv,
                stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                cwd=APP, start_new_session=True, env=env)
        except OSError as exc:
            # a service that cannot be executed at all is a failed scenario, not
            # a crashed scorer: the other scenarios still deserve to run
            log("scenario %s: could not start %s/bin/smbd: %s" % (self.name, APP, exc))
            self.proc = None
            return False
        self.watchdog = MemoryWatchdog(self.proc.pid, MEMORY_CEILING_MB)
        self.watchdog.start()
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.proc.poll() is not None:
                return False
            s = socket.socket()
            s.settimeout(0.5)
            try:
                s.connect(("127.0.0.1", self.port))
                s.close()
                return True
            except OSError:
                time.sleep(0.25)
            finally:
                try:
                    s.close()
                except OSError:
                    pass
        return False

    def memory_note(self):
        """A line about the service's memory use, or why the run was cut short."""
        w = getattr(self, "watchdog", None)
        if w is None:
            return None
        if w.killed:
            return ("the service was stopped: it used %d MiB of resident memory, "
                    "above the %d MiB ceiling" % (w.peak_mb, MEMORY_CEILING_MB))
        return "peak resident memory %d MiB (ceiling %d MiB)" % (w.peak_mb, MEMORY_CEILING_MB)

    def stop(self):
        w = getattr(self, "watchdog", None)
        if w is not None:
            w.cancel()
        if self.proc is None:
            return
        try:
            os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
        except OSError:
            try:
                self.proc.terminate()
            except OSError:
                pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
            except OSError:
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        self.proc = None
        # A child the service detached is in no process group of ours; it must
        # not carry into the next scenario or outlive the scorer.
        reap_service_uid()


# --------------------------------------------------------------------------
# smbtorture group


SUBUNIT = re.compile(rb"^(success|failure|error|skip|xfail|uxsuccess): (.+?)(?: \[)?\s*$",
                     re.M)


def run_torture(port, share, minproto, maxproto, tests, timeout=120):
    """Run one smbtorture invocation and return {full_test_id: outcome}."""
    results = {}
    left = budget_left()
    if left is not None:
        if left <= 5:
            note_starved(len(tests) or 1)
            return {}, b"<budget exhausted>", False
        timeout = min(timeout, left)
    cmd = ["smbtorture", "//127.0.0.1/%s" % share,
           "-U", "%s%%%s" % (USER, PASSWORD), "-p", str(port),
           "--target=samba3", "--format=subunit",
           "--option=client min protocol=%s" % minproto,
           "--option=client max protocol=%s" % maxproto] + tests
    # smbtorture forks helper children that inherit the pipe, so the run gets
    # its own process group and the whole group is killed on timeout.
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL, start_new_session=True)
    timed_out = False
    try:
        out, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except OSError:
            pass
        try:
            out, _ = proc.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            out = b""
    out = out or b""
    for m in SUBUNIT.finditer(out):
        results[m.group(2).decode("utf-8", "replace").strip()] = m.group(1).decode()
    return results, out, timed_out


def score_torture(port, share, minproto, maxproto, suite_tests, per_suite_timeout=150):
    """suite_tests: {suite_name: [expected test short names]} -> per-test outcome.

    smbtorture takes individual test ids as positional arguments, so only the
    scored tests are run; asking for a whole suite would also run the benchmarks
    and the timing-sensitive tests that are deliberately not scored.
    """
    outcome = {}
    empty_runs = hung_runs = 0
    for suite, names in suite_tests.items():
        # Two ways to stop asking, and they are not the same question.
        #
        # `empty_runs` catches a service that produces no subunit output at all.
        # It does not catch the worse case: a service that accepts the
        # connection and then does not answer, because smbtorture still reports
        # the tests it attempted, so the output is not empty -- it just takes
        # the full per-suite timeout to say so. Twenty-eight suites at 180 s is
        # 5,040 s, so such a candidate spends the entire scoring budget here and
        # never reaches the scenario holding its carry-forward groups, losing
        # the compatibility credit it had actually earned to a stopwatch.
        #
        # A suite that *times out* is the signal. A service that answers and
        # fails completes its suite quickly; only a hanging one runs to the cap.
        if empty_runs >= 4 or hung_runs >= 3 or budget_exhausted():
            for n in names:
                outcome["%s.%s" % (suite, n)] = False
            continue
        res, out, timed_out = run_torture(port, share, minproto, maxproto,
                                          ["%s.%s" % (suite, n) for n in names],
                                          timeout=per_suite_timeout)
        empty_runs = 0 if res else empty_runs + 1
        hung_runs = hung_runs + 1 if timed_out else 0
        if hung_runs >= 3:
            log("torture: %d consecutive suites ran to the %ds timeout without "
                "finishing; the service is not answering this dialect, marking "
                "the rest without asking\n" % (hung_runs, per_suite_timeout))
        for n in names:
            outcome["%s.%s" % (suite, n)] = (res.get(n) == "success")
    return outcome


def read_json(path):
    with open(path) as fh:
        return json.load(fh)


def write_report(groups, reward, extra=None):
    os.makedirs(LOGDIR, exist_ok=True)
    reward_value = round(max(0.0, min(1.0, reward)), 6)
    # Binary step reward: 1 only when the scorer awarded exactly 1.0.
    reward_value = 1.0 if reward_value == 1.0 else 0.0
    with open(os.path.join(LOGDIR, "reward.json"), "w") as fh:
        json.dump({"reward": reward_value}, fh)
    report = {"reward": reward, "groups": groups}
    if CORE_DUMPS:
        # Recorded so a reader of the report can see the preflight met a crash
        # dump and deliberately did not treat it as a shipped binary.
        report["ignored_core_dumps"] = list(CORE_DUMPS)
    if extra:
        report.update(extra)
    with open(os.path.join(LOGDIR, "report.json"), "w") as fh:
        json.dump(report, fh, indent=1, sort_keys=True)
    lines = []
    for gname, g in sorted(groups.items()):
        passed = sum(1 for v in g["cases"].values() if v)
        total = len(g["cases"])
        starved = min(STARVED.get(gname, 0), total - passed)
        note = ""
        if starved:
            note = ("   <-- %d of the %d not passed were never attempted: "
                    "the scoring budget ran out, not the service"
                    % (starved, total - passed))
        lines.append("%-28s %4d/%-4d  weight %.3f%s"
                     % (gname, passed, total, g["weight"], note))
        for case, ok in sorted(g["cases"].items()):
            if not ok:
                lines.append("    FAIL %s" % case)
    lines.append("")
    if STARVED:
        lines.append("BUDGET: %d case(s) across %d group(s) were not attempted "
                     "because the %s s scoring budget ran out. They are scored as "
                     "failures -- a service too slow to be scored has not earned "
                     "the credit -- but they are NOT evidence about the service's "
                     "correctness, and a zero on a carry-forward group here means "
                     "the clock, not a regression."
                     % (sum(STARVED.values()), len(STARVED),
                        "?" if BUDGET_SECONDS[0] is None
                        else "%.0f" % BUDGET_SECONDS[0]))
        for gname in sorted(STARVED):
            lines.append("    %-28s %d not attempted" % (gname, STARVED[gname]))
        lines.append("")
    if extra and extra.get("seal"):
        lines.append(extra["seal"])
    lines.append("reward = %.6f" % reward)
    with open(os.path.join(LOGDIR, "report.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    sys.stderr.write("\n".join(lines[-40:]) + "\n")
    # The authoritative reward leaves this process on STDOUT, which nothing else
    # writes to, so that test.sh can hold it in a shell variable and write the
    # reward file itself after every candidate process is dead.
    #
    # The reward.json written above is only an ordering, not a defence: a
    # DETACHED child of the service outlives the scorer and could rewrite the
    # file after it. A shell variable in test.sh is out of reach of anything
    # the service starts.
    sys.stdout.write("HARNESS_REWARD %.6f\n" % max(0.0, min(1.0, reward)))
    sys.stdout.flush()


def _is_regression_group(name):
    """A group that re-runs an earlier dialect's behaviour (the inherited /
    regression surface), as opposed to this step's own dialect. The author marks
    these in the group name."""
    n = name.lower()
    return "regression" in n or "cross-dialect" in n


def combine(groups):
    """Reward = T1 x T2.

    T1 is this step's own dialect -- its milestone groups; T2 is the inherited
    earlier-dialect groups re-run here (those the author names '*-regression' or
    '*-cross-dialect'). Each factor is the weighted mean of its groups' pass
    fractions, renormalised to [0, 1] so the existing relative weights are kept
    within the factor, and the reward is their product. A step with no regression
    group -- the first dialect -- is T1 alone. Because it is a product, any group
    all of whose cases were forced false (a liveness, anti-wrap, listener or
    workspace failure zeroes every group in a scenario) drives its factor, and so
    the reward, to 0; the hard zeros stay hard zeros."""
    def factor(items):
        num = den = 0.0
        for g in items:
            total = len(g["cases"])
            if total == 0:
                continue
            frac = sum(1 for v in g["cases"].values() if v) / float(total)
            num += frac * g["weight"]
            den += g["weight"]
        return (num / den) if den else None
    t1 = factor([g for n, g in groups.items() if not _is_regression_group(n)])
    t2 = factor([g for n, g in groups.items() if _is_regression_group(n)])
    if t1 is None:
        return t2 or 0.0
    if t2 is None:
        return t1
    return t1 * t2


# --------------------------------------------------------------------------
# deterministic share contents


SEED = [
    ("README.txt", b"R" * 120),
    ("MixedCase.TXT", b"mixedcase\r\n"),
    ("spaces in name.txt", b"space"),
    ("longfilename-that-needs-mangling.txt", b"mangleme\r\n"),
    ("data/alpha.bin", bytes(range(256)) * 16),
    ("data/beta.txt", b"beta contents, thirty-three byte"),
    ("data/gamma/delta.txt", b"delta\r\n"),
    ("data/gamma/epsilon", b""),
    ("empty", None),
]


def seed_share(path):
    """Reset a share directory to a fixed tree so every case starts equal.

    Runs as root while the candidate's server is live. The share's parent is
    root's (SHARE_ROOT), so the share itself cannot be swapped for a link; the
    tree is rebuilt inside a root-only directory and opened to the share's
    account only once it is complete, so nothing the server runs can redirect
    a write, chmod or chown made here."""
    parent = os.path.dirname(path)
    st = os.lstat(parent)
    if not stat.S_ISDIR(st.st_mode) or st.st_uid != 0 or st.st_mode & 0o022:
        raise OSError("share parent %s is not a root-owned directory" % parent)
    for _ in range(5):
        if not os.path.isdir(path):
            break
        shutil.rmtree(path, ignore_errors=True)
        time.sleep(0.1)
    shutil.rmtree(path, ignore_errors=True)
    os.mkdir(path, 0o700)
    os.chmod(path, 0o700)
    for name, content in SEED:
        full = os.path.join(path, name)
        if content is None:
            os.makedirs(full, 0o777)
            os.chmod(full, 0o777)
            continue
        os.makedirs(os.path.dirname(full), 0o777, exist_ok=True)
        with open(full, "wb") as fh:
            fh.write(content)
        os.chmod(full, 0o666)
    for root, dirs, files in os.walk(path):
        for d in dirs:
            os.chmod(os.path.join(root, d), 0o777)
    # a fixed modification time everywhere, so nothing volatile leaks into a
    # listing that is compared byte for byte after normalisation
    for root, dirs, files in os.walk(path, topdown=False):
        for n in files + dirs:
            os.utime(os.path.join(root, n), (1600000000, 1600000000))
    # The share tree belongs to the account the clients authenticate as, so a
    # server that impersonates the authenticated user and one that does not both
    # see a writable tree.
    try:
        for root, dirs, files in os.walk(path):
            for n in files + dirs:
                os.chown(os.path.join(root, n), 1000, 1000)
        os.chown(path, 1000, 1000)
    except (OSError, AttributeError):
        pass
    # Only now, with the tree complete, is the share opened up.
    os.chmod(path, 0o777)
    os.utime(path, (1600000000, 1600000000))


# --------------------------------------------------------------------------
# smbclient parity group

DATE_RE = re.compile(r"[A-Z][a-z]{2} [A-Z][a-z]{2} +\d+ \d\d:\d\d:\d\d \d{4}")
# 8.3 short names are generated by an algorithm no specification fixes, so only
# their shape is compared.
SHORTNAME_RE = re.compile(r"\b[A-Z0-9_^$!#%&{}@'()~-]{1,8}~[A-Z0-9]{1,2}\.[A-Z0-9]{1,3}\b")
ENTRY_RE = re.compile(r"^  (.+?)\s{2,}([DAHSNRE]+)\s+(\d+)\s+<TIME>$")
# smbclient prints "inf" for the throughput when the transfer's elapsed time
# rounds to zero, which for a 2 KiB file happens on an *idle* host and not on a
# busy one. A number-only pattern therefore normalises the rate on a loaded box
# and leaves it in place on a quiet one, so the two upload cases pass or fail
# with the host's load rather than with the server's behaviour.
_SPEED = r"(?:[0-9.,]+|-?inf|-?nan)"
_UNIT = r"(?:[kKMG]?i?[Bb]/s|(?:Kilo|Mega|Giga)?[Bb]ytes/sec)"
RATE_RE = re.compile(r"\(" + _SPEED + " " + _UNIT + r"\)")
AVG_RE = re.compile(r"\(average " + _SPEED + " " + _UNIT + r"\)")
BLOCKS_RE = re.compile(r"\d+ blocks of size \d+\. \d+ blocks available")
# The volume serial number is not fixed by any specification.
SERIAL_RE = re.compile(r"serial number 0x[0-9a-fA-F]+")
SIZE_RE = re.compile(r"of size \d+ as")
TIMEDELTA_RE = re.compile(r"\b\d{10,}\b")


def normalise_smbclient(out):
    text = out.decode("utf-8", "replace")
    text = DATE_RE.sub("<TIME>", text)
    text = AVG_RE.sub("<AVG>", text)
    text = RATE_RE.sub("<RATE>", text)
    text = BLOCKS_RE.sub("<BLOCKS>", text)
    text = SERIAL_RE.sub("serial number <SERIAL>", text)
    text = TIMEDELTA_RE.sub("<NUM>", text)
    text = SHORTNAME_RE.sub("<8DOT3>", text)
    lines = []
    for line in text.splitlines():
        line = line.rstrip()
        if not line:
            continue
        if line.startswith("Domain=") or line.startswith("OS=") or line.startswith("Server="):
            continue
        if "Anonymous login successful" in line:
            continue
        if "lpcfg_do_global_parameter" in line or line.startswith("WARNING:"):
            continue
        lines.append(line)
    # Directory enumeration order is not fixed by any specification, so each
    # contiguous run of listing lines is compared as a set.
    out = []
    block = []
    for line in lines:
        if ENTRY_RE.match(line):
            block.append(line)
            continue
        if block:
            out.extend(sorted(block))
            block = []
        out.append(line)
    if block:
        out.extend(sorted(block))
    # A recursive listing visits subdirectories in enumeration order, which is
    # equally unfixed, so the per-directory sections are compared as a set too.
    head, sections, cur = [], [], None
    for line in out:
        if line.startswith("\\"):
            if cur is not None:
                sections.append(cur)
            cur = [line]
        elif cur is None:
            head.append(line)
        else:
            cur.append(line)
    if cur is not None:
        sections.append(cur)
    if sections:
        trailing = []
        if sections and sections[-1] and sections[-1][-1].strip().startswith("<BLOCKS>"):
            trailing = [sections[-1].pop()]
        sections.sort(key=lambda blk: blk[0])
        out = head + [ln for blk in sections for ln in blk] + trailing
    return "\n".join(out)


def run_smbclient(port, share, minproto, maxproto, commands, extra_args=None,
                  user=USER, password=PASSWORD, timeout=60):
    left = budget_left()
    if left is not None:
        if left <= 2:
            note_starved()
            return b"<budget exhausted>"
        timeout = min(timeout, left)
    cmd = ["smbclient", "//127.0.0.1/%s" % share, "-U", "%s%%%s" % (user, password),
           "-p", str(port),
           "--option=client min protocol=%s" % minproto,
           "--option=client max protocol=%s" % maxproto,
           ]
    if extra_args:
        cmd += list(extra_args)
    cmd += ["-c", commands]
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout)
        return p.stdout
    except subprocess.TimeoutExpired as e:
        return (e.stdout or b"") + b"\n<TIMEOUT>\n"
