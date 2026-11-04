#!/usr/bin/env python3
"""Step scorer.

Five behavioural components, every one of them a set of *cases*:

  A  git's own t/ tests, run against the candidate binary through the
     documented GIT_TEST_INSTALLED interface, over a fixed per-assertion
     selection that carries step 1's points forward and adds this step's.
  B  recorded differential sessions with the real gits hidden (closed world).
  C  a corpus of repositories built by real git that the candidate must read.
  D  recorded differential sessions that use a pinned real git as $PEER.
  E  interop: repositories and packfiles the candidate wrote, read by three
     pinned real gits (fsck --strict, verify-pack, index-pack, clone, fetch,
     push).

Cases are then scored by ROLE rather than by component -- see roles.json.
Always writes /logs/verifier/reward.json and exits 0.
"""
import concurrent.futures
import hashlib
from fractions import Fraction
import json
import os
import re
import signal
import shutil
import stat
import subprocess
import sys
import tempfile
import time

APP = os.environ.get("GFPC_APP", "/app")
# Every process that runs candidate-supplied code -- the build most of all --
# runs as this user.  `make` is the most privileged process in a verifier and
# does not read like a launcher: as root it can rewrite the scorer and the
# expectations it is scored against.
GRADER_UID = int(os.environ.get("GFPC_GRADER_UID", "2000"))
GRADER = "grader"
PEER_BACKUP_SUFFIX = ".gfpc-hidden"
# A hidden real binary is kept as a hard link next to its tripwire; while it is
# hidden the link is root-only (mode 0700), or the candidate could run the real
# git -- or test-tool -- under its backup name.  The original mode is restored
# with the name.
BACKUP_MODES = {}
TRIPWIRE_LOG = "/tmp/tripwire.log"
FIX = "/tests/fixtures"
VER = "/tests/verify"
LOGDIR = "/logs/verifier"
SUITE = "/opt/git-suite"
PEERS = "/opt/gitpeer"
# git's own test helper: built from the pinned upstream source for the t/
# harness, and a library of git internals (ref stores, reftable, pkt-line,
# protocol v2 serve).  Only the harness may use it, so it is a tripwire for
# everything except component A.
TEST_TOOL = os.path.join(SUITE, "t", "helper", "test-tool")
GIT = os.path.join(APP, "out", "bin", "git")
# Cases are scored by ROLE, not by component: what this milestone adds
# (milestone) and what it must not break (regression).  The split is recorded in
# roles.json, produced at authoring time by running the previous milestone's
# reference tree against exactly these cases.  Step 1 has no previous milestone,
# so every one of its cases is labelled "milestone" and its reward is that
# role's score alone.
# The roles are combined as a PRODUCT (milestone x regression), not a weighted
# sum; ROLE_WEIGHTS only orders the log lines.
ROLE_WEIGHTS = {"milestone": 0.75, "regression": 0.25}
# Within a role, the four evidence families keep their own weights, so a role
# is never dominated by whichever family happens to have the most assertions.
# Renormalised over the families roles.json contains for that role.
COMPONENT_WEIGHTS = {"t": 0.40, "wl": 0.30, "corpus": 0.15, "interop": 0.15}
# The family weights are renormalised over whichever families a role actually
# contains, and that renormalisation is done in exact rational arithmetic
# (fractions.Fraction) in finalize(), never in binary floating point: when a
# role has no cases in one family, `0.15 + 0.40 + 0.30` evaluates to
# 0.8500000000000001 in floating point, the three renormalised shares would sum
# to 0.9999999999999999, and a run that passed every single case would report
# that instead of 1.0.  The weights themselves are
# used unchanged -- Fraction(w) is the exact value of the same double -- because
# the identity that has to hold is sum(w)/sum(w) == 1, which is exact over the
# rationals whatever the w are.
#
# The largest binary64 strictly below 1.0.  finalize() converts the exact reward
# to a float exactly once, and falls back to this if a genuinely imperfect run
# would otherwise round up: "1.0" in reward.json must mean "every case passed".
FLOAT_JUST_BELOW_ONE = float.fromhex("0x1.fffffffffffffp-1")

# One wall-clock budget for the whole scoring run, strictly inside the step's
# verifier timeout_sec: every component's timeout is
# clamped to what is left of it, so the sum of the component timeouts can never
# exceed it and a slow candidate can never get the verifier killed before
# reward.json is written.  A candidate that runs out of budget simply stops
# producing case results, and every case it has not answered scores 0 -- the
# denominator is roles.json, fixed at authoring time, not "whatever ran".
BUDGET_SEC = float(os.environ.get("GFPC_BUDGET_SEC", "3150"))
# A component that has not run yet keeps a share of the budget proportional to
# its own cap, so a candidate that hangs in the first one cannot consume what
# the ones behind it need and make their cases score 0 for never having been
# reached.  A constant reserve is a guess about the shape of
# the work; a share of the caps is a fact about it.
MIN_COMPONENT_FLOOR = 60.0
STARTED = time.time()
CASES = {}
DETAIL = {}
COMPONENT_DEADLINE = None
# Component result files the scorer trusts live in a root-only directory, not at
# fixed paths in world-writable /tmp where a process left behind by the
# candidate could create or rewrite them before they are read.
RESULTS = tempfile.mkdtemp(prefix="gfpc-results-")


def component_window(cap, caps_after):
    """Wall-clock deadline for the component about to run.

    It may use its own cap, but never more than its share of what is left:
    the budget remaining, split in proportion to the caps of this component and
    every component still to run.
    """
    left = STARTED + BUDGET_SEC - time.time()
    total = cap + sum(caps_after)
    share = left * (cap / total) if total else left
    return time.time() + max(MIN_COMPONENT_FLOOR, min(cap, share))


def remaining(cap):
    """How long a subprocess may run: never past this component's window."""
    end = COMPONENT_DEADLINE if COMPONENT_DEADLINE is not None else STARTED + BUDGET_SEC
    return max(1, int(min(cap, end - time.time())))

# Anything compiled, or any implementation source that is not PHP, fails the
# language gate: the task is to write the implementation in PHP.
BANNED_SUFFIXES = (
    ".c", ".cc", ".cpp", ".cxx", ".m", ".go", ".rs", ".java", ".class", ".jar",
    ".py", ".pyc", ".rb", ".pl", ".pm", ".lua", ".js", ".ts", ".cs", ".swift",
    ".o", ".a", ".so", ".dylib", ".exe", ".wasm",
)
BINARY_MAGIC = (b"\x7fELF", b"MZ", b"\xca\xfe\xba\xbe", b"\xcf\xfa\xed\xfe",
                b"\xfe\xed\xfa\xce", b"\x00asm")


def digest(status, stdout, stderr=""):
    """Expectations ship as sha256 digests, never as the expected bytes: the
    candidate is built and run inside this image, so anything readable under
    /tests is readable by it.  Matching a digest requires
    producing the output."""
    h = hashlib.sha256()
    h.update(b"%d\0" % int(status))
    h.update(stdout.encode("utf-8", "surrogateescape") + b"\0")
    h.update(stderr.encode("utf-8", "surrogateescape"))
    return h.hexdigest()


def log(msg):
    print(msg, flush=True)


def have_grader():
    try:
        import pwd
        pwd.getpwnam(GRADER)
        return os.geteuid() == 0
    except Exception:  # noqa: BLE001
        return False


def as_grader(cmd):
    """Wrap a shell command so it runs as the unprivileged grader user."""
    if not have_grader():
        return cmd
    return ("setpriv --reuid=%d --regid=%d --clear-groups --inh-caps=-all "
            "/bin/bash -c %s" % (GRADER_UID, GRADER_UID, shell_quote(cmd)))


def grader_flag():
    return "--as-user %d" % GRADER_UID if have_grader() else ""


def shell_quote(s):
    return "'" + s.replace("'", "'\\''") + "'"


def run(cmd, cwd=None, env=None, timeout=900, shell=True):
    try:
        p = subprocess.run(cmd if shell else cmd, cwd=cwd, env=env, shell=shell,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout)
        return p.returncode, p.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        return 124, "TIMEOUT\n"
    except Exception as exc:  # noqa: BLE001
        return 125, "harness error: %s\n" % exc


def _write_tripwire(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as fh:
        fh.write("#!/bin/sh\n"
                 "echo \"TRIPWIRE $0 $*\" >> %s\n"
                 "echo \"fatal: this is not a git binary\" >&2\n"
                 "exit 127\n" % TRIPWIRE_LOG)
    os.chmod(path, 0o755)


def hide_peers():
    """Replace the real gits with tripwires while the candidate runs.

    The binaries are hard-linked aside first (same filesystem, so this is
    instant) and moved back afterwards; renaming the directory itself is not
    possible across the image's overlay layers.
    """
    if not os.path.isdir(PEERS):
        return
    for ver in sorted(os.listdir(PEERS)):
        for sub in ("bin", os.path.join("libexec", "git-core")):
            d = os.path.join(PEERS, ver, sub)
            if not os.path.isdir(d):
                continue
            for name in sorted(os.listdir(d)):
                if name.endswith(PEER_BACKUP_SUFFIX):
                    continue
                p = os.path.join(d, name)
                if not os.path.isfile(p):
                    continue
                bak = p + PEER_BACKUP_SUFFIX
                try:
                    if not os.path.exists(bak):
                        BACKUP_MODES.setdefault(bak, os.stat(p).st_mode & 0o7777)
                        os.link(p, bak)
                    os.unlink(p)
                    _write_tripwire(p)
                except OSError as exc:
                    log("== warning: could not hide %s: %s" % (p, exc))
    lock_backups()
    for p in ("/usr/local/bin/git", "/usr/bin/git", "/usr/local/bin/git-upload-pack",
              "/usr/local/bin/git-receive-pack"):
        if not os.path.exists(p):
            _write_tripwire(p)


def lock_backups():
    """Every hidden real binary's backup link is root-only (see BACKUP_MODES).
    Called after every hide and after anything that re-opens permissions under
    the suite or the peers (prepare_grader_space's chmod -R a+rX)."""
    dirs = [os.path.dirname(TEST_TOOL)]
    if os.path.isdir(PEERS):
        for ver in sorted(os.listdir(PEERS)):
            for sub in ("bin", os.path.join("libexec", "git-core")):
                dirs.append(os.path.join(PEERS, ver, sub))
    for d in dirs:
        try:
            names = os.listdir(d)
        except OSError:
            continue
        for name in names:
            if not name.endswith(PEER_BACKUP_SUFFIX):
                continue
            bak = os.path.join(d, name)
            try:
                st = os.lstat(bak)
                if stat.S_ISREG(st.st_mode):
                    BACKUP_MODES.setdefault(bak, st.st_mode & 0o7777)
                    os.chmod(bak, 0o700)
            except OSError as exc:
                log("== warning: could not lock %s: %s" % (bak, exc))


def hide_test_tool():
    """Replace test-tool with a tripwire outside component A."""
    if not os.path.isfile(TEST_TOOL) and not os.path.isfile(TEST_TOOL + PEER_BACKUP_SUFFIX):
        return
    bak = TEST_TOOL + PEER_BACKUP_SUFFIX
    try:
        if not os.path.exists(bak):
            BACKUP_MODES.setdefault(bak, os.stat(TEST_TOOL).st_mode & 0o7777)
            os.link(TEST_TOOL, bak)
        if os.path.exists(TEST_TOOL):
            os.unlink(TEST_TOOL)
        _write_tripwire(TEST_TOOL)
    except OSError as exc:
        log("== warning: could not hide %s: %s" % (TEST_TOOL, exc))
    lock_backups()


def restore_test_tool():
    bak = TEST_TOOL + PEER_BACKUP_SUFFIX
    if not os.path.exists(bak):
        return
    try:
        if os.path.exists(TEST_TOOL):
            os.unlink(TEST_TOOL)
        os.chmod(bak, BACKUP_MODES.get(bak, 0o755))
        os.rename(bak, TEST_TOOL)
    except OSError as exc:
        log("== warning: could not restore %s: %s" % (TEST_TOOL, exc))


def restore_peers():
    if not os.path.isdir(PEERS):
        return
    for ver in sorted(os.listdir(PEERS)):
        for sub in ("bin", os.path.join("libexec", "git-core")):
            d = os.path.join(PEERS, ver, sub)
            if not os.path.isdir(d):
                continue
            for name in sorted(os.listdir(d)):
                if not name.endswith(PEER_BACKUP_SUFFIX):
                    continue
                bak = os.path.join(d, name)
                real = bak[: -len(PEER_BACKUP_SUFFIX)]
                try:
                    if os.path.exists(real):
                        os.unlink(real)
                    os.chmod(bak, BACKUP_MODES.get(bak, 0o755))
                    os.rename(bak, real)
                except OSError as exc:
                    log("== warning: could not restore %s: %s" % (real, exc))
    for p in ("/usr/local/bin/git", "/usr/bin/git", "/usr/local/bin/git-upload-pack",
              "/usr/local/bin/git-receive-pack"):
        try:
            if os.path.isfile(p) and open(p).read(9) == "#!/bin/sh":
                os.unlink(p)
        except OSError:
            pass


def tripwire_hits():
    try:
        with open(TRIPWIRE_LOG) as fh:
            return [l.rstrip() for l in fh if l.strip()]
    except OSError:
        return []


def verify_privilege_posture():
    """A privilege drop that silently becomes a no-op is worse than none: it
    reads as protection in the report while candidate code runs as root.
    If this image was built to drop privileges, the grader user MUST exist and
    MUST be denied the reward directory, or the run refuses.  The observed
    ownership and modes are printed."""
    required = os.path.exists("/tests/.grader-required")
    st = None
    try:
        st = os.stat(LOGDIR)
    except OSError:
        pass
    if st is not None:
        import pwd, grp
        try: ow = pwd.getpwuid(st.st_uid).pw_name
        except Exception: ow = str(st.st_uid)
        try: gr = grp.getgrgid(st.st_gid).gr_name
        except Exception: gr = str(st.st_gid)
        log("== reward dir %s mode %o owner %s:%s" % (LOGDIR, st.st_mode & 0o7777, ow, gr))
    if not required:
        log("== note: image not built for a privilege drop (no /tests/.grader-required)")
        return True
    if not have_grader():
        log("== FATAL: image requires the grader user but it is absent -- "
            "a re-provisioned container has made the drop a no-op; refusing to run")
        DETAIL["privilege_posture"] = "grader-missing"
        return False
    # The harness may mount /logs/verifier world-writable (GKE mounts it 0777).
    # The verifier runs as root, so lock the directory down -- root-owned, no
    # group/other access -- BEFORE any candidate code runs, converting a
    # permissive mount into one the dropped candidate cannot write.  Hardening
    # rather than refusing keeps grading working on such mounts.
    try:
        os.chown(LOGDIR, 0, 0)
    except OSError:
        pass
    try:
        os.chmod(LOGDIR, 0o755)
    except OSError:
        pass
    probe = LOGDIR + "/.drop-write-probe"
    rc, out = run(as_grader("touch %s 2>/dev/null && echo WRITABLE || echo denied" % probe), timeout=60)
    run("rm -f %s" % probe, timeout=30)
    st2 = os.stat(LOGDIR)
    log("== reward dir hardened to mode %o owner uid %d" % (st2.st_mode & 0o7777, st2.st_uid))
    if "WRITABLE" in out:
        log("== FATAL: grader uid still CAN write %s after hardening; refusing" % LOGDIR)
        DETAIL["privilege_posture"] = "reward-dir-writable-by-grader"
        return False
    log("== grader uid %d denied write to %s: privilege drop is live" % (GRADER_UID, LOGDIR))
    DETAIL["privilege_posture"] = "ok"
    return True


def tests_fingerprint():
    """A hash over everything the grader ships, so a candidate that manages to
    rewrite any of it is caught even if the privilege drop is unavailable."""
    h = hashlib.sha256()
    for root, dirs, files in os.walk("/tests"):
        dirs[:] = sorted(d for d in dirs if d != "__pycache__")
        for name in sorted(files):
            if name.endswith((".pyc", ".pyo")):
                continue
            p = os.path.join(root, name)
            h.update(p.encode() + b"\0")
            try:
                with open(p, "rb") as fh:
                    while True:
                        blob = fh.read(1 << 20)
                        if not blob:
                            break
                        h.update(blob)
            except OSError:
                h.update(b"<unreadable>")
            h.update(b"\0")
    return h.hexdigest()


def prepare_grader_space():
    """The candidate's tree and every scratch directory it writes belong to the
    unprivileged user; /tests stays root-only and unreadable to it."""
    if not have_grader():
        log("== note: no unprivileged grader user; candidate code runs as root")
        return
    run("chown -R %s %s" % (GRADER, APP), timeout=300)
    for d in ("/tmp/tsuite", "/tmp/wl", "/tmp/cor", "/tmp/interop-repos", "/tmp/graderhome"):
        os.makedirs(d, exist_ok=True)
        run("chown -R %s %s" % (GRADER, d), timeout=120)
    run("chmod 1777 /tmp", timeout=60)
    # the suite and the pinned peers are read-only inputs; the unprivileged
    # user has to be able to read and execute them
    run("chmod -R a+rX %s %s" % (SUITE, PEERS), timeout=600)
    lock_backups()
    # a scenario that stands Apache up now does so as the grader, so the
    # runtime directories httpd writes have to belong to it
    for d in ("/var/log/apache2", "/var/run/apache2", "/var/lock/apache2",
              "/var/cache/apache2", "/etc/apache2/sites-available",
              "/etc/apache2/sites-enabled", "/etc/apache2/ports.conf"):
        run("test -e %s && chown -R %s %s" % (d, GRADER, d), timeout=120)


def reap_candidate():
    """Kill any process still running as the unprivileged grader before the
    reward is scored and written, so a candidate build or probe cannot leave a
    detached process behind to touch results after the scorer is done.  This
    runs inside the verifier container and targets only the grader uid; the
    root scorer itself is never a target."""
    if not have_grader():
        return
    run("pkill -u %d 2>/dev/null; sleep 0.3; pkill -9 -u %d 2>/dev/null || true"
        % (GRADER_UID, GRADER_UID), timeout=30)


def build_candidate():
    log("== build: make in /app")
    # the candidate is always rebuilt from its own sources
    shutil.rmtree(os.path.join(APP, "out"), ignore_errors=True)
    prepare_grader_space()
    rc, out = run(as_grader("cd %s && make" % APP), cwd=APP, timeout=1200)
    tail = "\n".join(out.splitlines()[-25:])
    log(tail)
    if not os.path.isfile(GIT) or not os.access(GIT, os.X_OK):
        log("== build FAILED: %s missing or not executable (make exit %d)" % (GIT, rc))
        return False
    with open(GIT, "rb") as fh:
        head = fh.read(4096)
    for magic in BINARY_MAGIC:
        if head.startswith(magic):
            log("== build FAILED: %s is a compiled binary, not a PHP program" % GIT)
            return False
    if not head.startswith(b"#!"):
        log("== build FAILED: %s is not an executable script" % GIT)
        return False
    interp = head.split(b"\n", 1)[0]
    if b"php" not in head[:4096] and b"php" not in interp:
        log("== build FAILED: %s does not run PHP" % GIT)
        return False
    # The candidate's own binary is candidate-controlled code, so it runs as the
    # unprivileged grader like every other candidate invocation -- never as root.
    # A root run here would void the reward-dir privilege drop (a process it spawns
    # could rewrite /logs/verifier or /tests).
    rc, out = run(as_grader("%s version" % GIT), timeout=60)
    log("== %s version -> rc=%d %s" % (GIT, rc, out.strip()[:120]))
    if rc != 0:
        return False
    rc, _ = run(as_grader("%s --exec-path" % GIT), timeout=60)
    return rc == 0


def _regular(path):
    """Root reads only regular files from the candidate's tree: a symlink is
    never followed, and a FIFO or device node would block or read elsewhere."""
    try:
        return stat.S_ISREG(os.lstat(path).st_mode)
    except OSError:
        return False


def _non_text_files():
    """The workspace must stay text: it is transferred as an uncompressed tar
    over a stream that is decoded as UTF-8, so binary files do not survive."""
    bad = []
    for root, dirs, files in os.walk(APP):
        dirs[:] = [d for d in dirs if d != "node_modules"]
        for name in files:
            path = os.path.join(root, name)
            if not _regular(path):
                continue
            try:
                with open(path, "rb") as fh:
                    blob = fh.read(1 << 16)
                blob.decode("utf-8")
            except UnicodeDecodeError:
                bad.append("binary file in the workspace: %s" % path)
            except OSError:
                continue
            if len(bad) > 5:
                break
    return bad


FOREIGN_EXEC = re.compile(
    rb"(/opt/gitpeer|/usr/local/bin/git|/usr/bin/git\b|/opt/git-suite/[^\s\"']*git\b"
    rb"|/opt/git-suite/t/helper|\btest-tool\b)")
# The candidate is built and run inside the verifier image, so the grader's own
# directories are on its filesystem.  Expectations ship as digests, which makes
# reading them useless, but code that goes looking is still disqualifying.
VERIFIER_PATHS = re.compile(rb"(?<![\w.)}\]])(/tests/|/tests\b|/logs/verifier\b)")


def gate_sources():
    """No compiled artefacts, no non-PHP implementation sources, no calls out
    to a real git.  The build output is scanned too: copying a git binary into
    out/bin is the obvious way to fake this task."""
    problems = []
    php_files = 0
    for root, dirs, files in os.walk(APP):
        dirs[:] = [d for d in dirs if d not in (".git", "node_modules")]
        for name in files:
            path = os.path.join(root, name)
            if not _regular(path):
                continue
            low = name.lower()
            if low.endswith(".php"):
                php_files += 1
            if low.endswith(BANNED_SUFFIXES):
                problems.append("non-PHP source or compiled object: %s" % path)
                continue
            try:
                with open(path, "rb") as fh:
                    blob = fh.read(1 << 20)
            except OSError:
                continue
            for magic in BINARY_MAGIC:
                if blob.startswith(magic):
                    problems.append("compiled binary in the repository: %s" % path)
                    break
            else:
                # only code is scanned for calls out to a real git; prose may
                # mention the reference builds
                is_code = (low.endswith((".php", ".sh", ".mk", ".inc"))
                           or name in ("Makefile", "makefile", "GNUmakefile")
                           or blob.startswith(b"#!"))
                if is_code:
                    hit = FOREIGN_EXEC.search(blob)
                    if hit is not None:
                        problems.append("reference to a real git binary (%s) in %s"
                                        % (hit.group(0).decode("utf-8", "replace"), path))
                    hit = VERIFIER_PATHS.search(blob)
                    if hit is not None:
                        problems.append("reference to grader-owned paths (%s) in %s"
                                        % (hit.group(0).decode("utf-8", "replace"), path))
    if php_files == 0:
        problems.append("no PHP sources in the repository")
    # Binary files in the workspace are a transport hazard, not a correctness
    # failure: report them, but never fail an otherwise correct implementation
    # for them.
    for warning in _non_text_files():
        log("== note: %s" % warning)
    for p in problems[:20]:
        log("== gate: %s" % p)
    return not problems


_TAP_RESULT = re.compile(r"^(not )?ok (\d+)(.*)$")
_TAP_PLAN = re.compile(r"^1\.\.(\d+)")
_TAP_SUMMARY = re.compile(r"^# (?:passed all|failed (\d+) among) (?:remaining )?(\d+) test\(s\)\s*$")
_TAP_SUMMARY_ANYWHERE = re.compile(r"(?:passed all|failed \d+ among) (?:remaining )?\d+ test\(s\)")
_TAP_GLUED = re.compile(r"^#\s*(?:not )?ok \d+\b")
_TAP_FATAL = re.compile(r"FATAL: Unexpected exit")


def parse_tap(text, rc=None):
    """A skipped assertion is not a passed one.

    test-lib reports an unmet prerequisite as `ok N # skip ...`, which would
    otherwise be free credit for any binary that merely lets the file finish
    (the selection keeps skips out, and this keeps them out at scoring time
    too).

    The candidate runs under the same uid as the test script and can write into
    its output, so the stream is only trusted when it is the one test-lib
    itself would have produced:
      * every assertion number is reported exactly once -- an injected `ok N`
        next to test-lib's own line for N is credited to nobody;
      * the harness exited by itself (status 0 or 1, never a signal or a
        timeout) -- killing it and forging the rest is void;
      * a script that reached test_done printed exactly one plan `1..N` and
        exactly one test-lib summary, which agree with the lines: N numbers
        1..N, and the summary's failure count equals the `not ok` lines that
        are not known breakages -- a test-lib line glued onto an injected
        prefix (so it no longer parses) is a mismatch;
      * the plan is the last line: test_done prints it and exits, so anything
        after it (test-lib's own `error:` or `FATAL: Unexpected exit` line
        after a forged plan, when the script was made to exit through `error`
        or a trapped TERM/INT/HUP) means the stream was not test-lib's;
      * a script that test-lib stopped early through its own `error`/`BUG`
        (status 1, e.g. `test_grep` on a file the binary under test never
        wrote) has no plan and no summary: it is credited for the numbers it
        reported, provided they run 1..K consecutively, each once, and
        test-lib's `error:` line follows the last of them;
      * no comment line carries a result or a summary, and no
        `FATAL: Unexpected exit` line appears anywhere.
    Any violation credits nothing in that file.
    """
    ok, notok, seen = set(), set(), {}
    plans, summaries, failing, non_todo, bad = [], [], 0, 0, []
    lines = text.splitlines()
    last_result, error_after = -1, False
    for i, line in enumerate(lines):
        if _TAP_FATAL.search(line):
            bad.append("test-lib reported an unexpected exit")
        if line.startswith("error: ") and last_result < i:
            error_after = True
        m = _TAP_RESULT.match(line)
        if m:
            last_result, error_after = i, False
            n = int(m.group(2))
            seen[n] = seen.get(n, 0) + 1
            rest = m.group(3)
            if not re.match(r"\s*(?:-.*)?#\s*TODO", rest):
                non_todo += 1
            if m.group(1):
                notok.add(n)
                if not re.match(r"\s*(?:-.*)?#\s*TODO", rest):
                    failing += 1
            elif not re.match(r"\s*#\s*skip", rest, re.I):
                ok.add(n)
            continue
        m = _TAP_PLAN.match(line)
        if m:
            plans.append(int(m.group(1)))
            continue
        m = _TAP_SUMMARY.match(line)
        if m:
            summaries.append((int(m.group(1) or 0), int(m.group(2))))
            continue
        if _TAP_GLUED.match(line) or _TAP_SUMMARY_ANYWHERE.search(line):
            bad.append("result or summary inside another line: %r" % line[:80])
    dup = {n for n, c in seen.items() if c > 1}
    if rc is not None and rc not in (0, 1):
        bad.append("the test script did not exit by itself (status %s)" % rc)
    if not plans:
        # stopped early by test-lib's own error()/BUG(): no plan, no summary
        if rc != 1 or not error_after:
            bad.append("no plan line, and no test-lib error after the last result")
        elif summaries:
            bad.append("a test-lib summary without a plan")
        elif seen and set(seen) != set(range(1, max(seen) + 1)):
            bad.append("numbers reported are not 1..%d" % max(seen))
    elif len(plans) != 1:
        bad.append("%d plan lines" % len(plans))
    elif not _TAP_PLAN.match(([l for l in lines if l.strip()] or [""])[-1]):
        bad.append("output continues after the plan line")
    elif plans[0] and set(seen) != set(range(1, plans[0] + 1)):
        bad.append("numbers reported do not match the plan 1..%d" % plans[0])
    # test-lib prints exactly one summary, except when every assertion was a
    # known breakage (then it prints none).
    if len(summaries) > 1:
        bad.append("%d test-lib summary lines" % len(summaries))
    elif summaries and summaries[0][0] != failing:
        bad.append("summary says %d failed, the stream shows %d" % (summaries[0][0], failing))
    elif not summaries and plans and plans[0] and non_todo:
        bad.append("no test-lib summary")
    if bad:
        return set(), notok | ok, bad
    return ok - dup, notok | dup, []


def component_a(cases):
    """git's own tests, over the recorded per-assertion selection."""
    selpath = os.path.join(FIX, "selection.json")
    if not os.path.exists(selpath):
        log("   no selection.json: 0/0 test points")
        return
    with open(selpath) as fh:
        selection = json.load(fh)
    deadline = COMPONENT_DEADLINE if COMPONENT_DEADLINE is not None else STARTED + BUDGET_SEC
    t_start = time.time()

    def one(item):
        tfile, wanted = item
        if time.time() > deadline:
            return tfile, set(), wanted, 0.0
        t0 = time.time()
        outdir = "/tmp/tsuite/%s" % tfile
        os.makedirs(outdir, exist_ok=True)
        # test-lib writes its trash directory here, as the unprivileged user
        if have_grader():
            try:
                os.chown(outdir, GRADER_UID, GRADER_UID)
            except OSError:
                pass
        env = dict(os.environ)
        env.update({
            "GIT_TEST_INSTALLED": os.path.dirname(GIT),
            "TEST_NO_MALLOC_CHECK": "1",
            "HOME": outdir,
            "TEST_OUTPUT_DIRECTORY": outdir,
            "LC_ALL": "C", "LANG": "C", "TZ": "UTC",
            "GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME": "master",
        })
        # The whole file is run, and only the recorded points are scored.
        # test-lib's --run= would be faster, but it skips the very tests that
        # set up the ones being scored, so a point whose fixture lives in an
        # earlier assertion could never be selected at all.
        # Per-file ceiling. 900s is the shipped grading value; GFPC_A_FILE_CAP
        # only ever raises it, and only when explicitly set (role generation on
        # a contended box, where t0008-ignores.sh alone can need well over
        # 900s).  It cannot be set by a candidate: in separate environment_mode
        # the verifier's environment is the harness's, not the agent's.
        file_cap = int(os.environ.get("GFPC_A_FILE_CAP", "900"))
        rc, out = run(as_grader("cd %s && ./%s --no-chain-lint"
                                % (os.path.join(SUITE, "t"), tfile)), env=env,
                      timeout=max(30, min(file_cap, int(deadline - time.time()))))
        ok, _, bad = parse_tap(out, rc)
        for why in bad[:3]:
            log("   %s: TAP stream not trusted, no point credited: %s" % (tfile, why))
        return tfile, ok, wanted, time.time() - t0

    with concurrent.futures.ThreadPoolExecutor(max_workers=7) as pool:
        total_hit = total_pts = 0
        for tfile, ok, wanted, secs in pool.map(one, sorted(selection.items())):
            hit = sum(1 for n in wanted if n in ok)
            total_hit += hit
            total_pts += len(wanted)
            log("   %-44s %3d/%-3d %5.0fs" % (tfile, hit, len(wanted), secs))
            for n in wanted:
                cases["t/%s#%d" % (tfile, n)] = n in ok
        log("   test points: %d/%d in %.0fs" % (total_hit, total_pts, time.time() - t_start))


def component_workload(cases, peer_phase):
    """Recorded differential sessions.  peer_phase selects the half that needs a
    real git as $PEER (run with the peers restored) from the closed-world half."""
    scen = os.path.join(FIX, "scenarios.json")
    with open(scen) as fh:
        scenarios = json.load(fh)
    names = [s["name"] for s in scenarios if bool(s.get("peer")) == peer_phase]
    if not names:
        log("   %s scenarios: 0/0 probes (none recorded for this phase)"
            % ("peer" if peer_phase else "closed"))
        return
    out = os.path.join(RESULTS, "workload-%s.json" % ("peer" if peer_phase else "closed"))
    rc, txt = run("python3 %s/workload.py --git %s --peers %s --scenarios %s --only %s "
                  "--out %s %s" % (VER, GIT, PEERS, scen, ",".join(names), out, grader_flag()),
                  timeout=remaining(1200))
    with open(os.path.join(FIX, "expect.json")) as fh:
        expect = {e["name"]: e for e in json.load(fh)}
    got = {}
    if os.path.exists(out):
        with open(out) as fh:
            got = {g["name"]: g for g in json.load(fh)}
    else:
        log("   workload runner failed: %s" % txt.strip()[-300:])
    hit = 0
    total = 0
    for name in names:
        e = expect.get(name)
        g = got.get(name)
        if e is None:
            continue
        for i, pe in enumerate(e["probes"]):
            total += 1
            pg = g["probes"][i] if (g and i < len(g["probes"])) else None
            try:
                ok = (pg is not None
                      and digest(pg["status"], pg["stdout"], pg["stderr"]) == pe["digest"])
            except Exception as exc:  # noqa: BLE001
                # one malformed reply costs one case, not the component
                log("   probe %s#%d could not be compared: %s" % (name, i, exc))
                ok = False
            cases["wl/%s#%d" % (name, i)] = ok
            if ok:
                hit += 1
    log("   %s scenarios: %d/%d probes" % ("peer" if peer_phase else "closed", hit, total))


def component_corpus(cases):
    out = os.path.join(RESULTS, "corpus-got.json")
    # --defs describes how the corpus was BUILT; replaying it needs only the
    # recorded expectations, so the definitions stay in the package as
    # provenance and are not passed here.
    cmd = ("python3 %s/corpus.py --mode check --git %s --corpus %s --peers %s --out %s"
           % (VER, GIT, os.path.join(FIX, "corpus"), PEERS, out))
    rc, txt = run(cmd + " " + grader_flag(), timeout=remaining(900))
    if not os.path.exists(out):
        log("   corpus runner failed: %s" % txt.strip()[-300:])
        with open(os.path.join(FIX, "corpus", "expect.json")) as fh:
            expect = json.load(fh)
        for repo, probes in expect.items():
            for i in range(len(probes)):
                cases["corpus/%s#%d" % (repo, i)] = False
        return
    with open(out) as fh:
        res = json.load(fh)
    seen = {}
    for r in res:
        i = seen.get(r["repo"], 0)
        seen[r["repo"]] = i + 1
        cases["corpus/%s#%d" % (r["repo"], i)] = bool(r["ok"])
    log("   corpus probes: %d/%d" % (sum(1 for r in res if r["ok"]), len(res)))


def component_interop(cases):
    """Build with the candidate (peers hidden), then let the pinned real gits
    read what it wrote: fsck --strict, verify-pack, index-pack, clone, fetch
    and push over the candidate's own repositories and packfiles."""
    out = os.path.join(RESULTS, "interop-got.json")
    spec = os.path.join(FIX, "interop.json")
    expectf = os.path.join(FIX, "interop-expect.json")
    repos = "/tmp/interop-repos"
    shutil.rmtree(repos, ignore_errors=True)
    hide_peers()
    rc, txt = run("python3 %s/crossverify.py --phase build --git %s --peers %s --spec %s "
                  "--repos %s %s" % (VER, GIT, PEERS, spec, repos, grader_flag()),
                  timeout=remaining(900))
    if rc != 0:
        log("   interop build phase: %s" % txt.strip()[-300:])
    restore_peers()
    rc, txt = run("python3 %s/crossverify.py --phase probe --git %s --peers %s --spec %s "
                  "--repos %s --expect %s --out %s %s"
                  % (VER, GIT, PEERS, spec, repos, expectf, out, grader_flag()),
                  timeout=remaining(900))
    with open(expectf) as fh:
        expect = [e for e in json.load(fh) if e["cmd"] != "<build>"]
    got = {}
    if os.path.exists(out):
        with open(out) as fh:
            for r in json.load(fh):
                got[(r["scenario"], r["peer"], r["cmd"])] = bool(r["ok"])
    else:
        log("   interop runner failed: %s" % txt.strip()[-400:])
    hit = 0
    for i, e in enumerate(expect):
        ok = got.get((e["scenario"], e["peer"], e["cmd"]), False)
        cases["interop/%s@%s#%d" % (e["scenario"], e["peer"], i)] = ok
        hit += 1 if ok else 0
    log("   interop checks: %d/%d" % (hit, len(expect)))


def apply_requirements():
    """A closed-world probe hears only what the candidate says about a structure
    it wrote: `verify` printing OK, or a file merely existing, is what a stub
    that writes a placeholder and exits 0 also produces.  A step
    may therefore name, in fixtures/requires.json, real-git cases that must
    pass for a candidate-side case to count: the candidate's own report on a
    commit-graph, multi-pack-index or bitmap is credited only if a pinned real
    git accepted the structure the same writer produced.  The file is the
    package's own, and every case it names was checked against roles.json when
    it was written.  Absent file: nothing changes."""
    path = os.path.join(FIX, "requires.json")
    if not os.path.exists(path):
        return
    with open(path) as fh:
        req = json.load(fh)
    voided = []
    for case_id in sorted(req):
        needs = req[case_id]
        if CASES.get(case_id) and not all(CASES.get(n, False) for n in needs):
            CASES[case_id] = False
            voided.append(case_id)
    if voided:
        log("== %d candidate-side case(s) not credited: real git did not accept the "
            "structure they report on: %s" % (len(voided), ", ".join(voided[:20])))
    DETAIL["requires_voided"] = voided


def finalize(reason="complete"):
    """Compute and write the reward from whatever has been scored so far.

    Reachable from the alarm handler as well as the normal path, so the step
    always produces a reward.json even if the run is cut short.

    The denominator is roles.json -- fixed at authoring time -- never "whatever
    ran": a component that produced nothing scores 0 for its cases instead of
    disappearing from the denominator.  Within each
    role the four evidence families are weighted, so a role cannot be dominated
    by whichever family has the most assertions.
    """
    apply_requirements()
    cases_out = os.environ.get("GFPC_CASES_OUT")
    if cases_out:
        with open(cases_out, "w") as fh:
            json.dump(CASES, fh, indent=1, sort_keys=True)
        log("== wrote %d case results to %s" % (len(CASES), cases_out))
        return 0.0
    with open(os.path.join(FIX, "roles.json")) as fh:
        roles = json.load(fh)
    # tally[role][family] = [passed, total]
    tally = {}
    missing = 0
    for case_id, role in roles.items():
        family = case_id.split("/", 1)[0]
        bucket = tally.setdefault(role, {}).setdefault(family, [0, 0])
        bucket[1] += 1
        if case_id not in CASES:
            missing += 1
        elif CASES[case_id]:
            bucket[0] += 1
    unlabelled = [c for c in CASES if c not in roles]
    if unlabelled:
        log("== note: %d of %d reported cases had no recorded role (ignored)"
            % (len(unlabelled), len(CASES)))
    if missing:
        log("== note: %d of %d expected cases were never reported (scored 0)"
            % (missing, len(roles)))

    # Renormalisation in exact rational arithmetic -- see FLOAT_JUST_BELOW_ONE.
    # Every quantity below (the weights, each family's passed/total, and both
    # divisions) is a Fraction, so a step in which every case passes sums to
    # exactly 1 and a step that misses even one case is strictly below it.
    # Product scoring: reward = milestone x
    # regression, each role's score being its family-weighted fraction of
    # passing cases.  A tree that keeps only the previous milestone's behaviour
    # scores 0; step 1 (milestone only) is that role's score alone.
    reward = Fraction(1)
    roles_scored = 0
    detail_roles = {}
    for role in sorted(ROLE_WEIGHTS, key=lambda r: -ROLE_WEIGHTS[r]):
        families = tally.get(role)
        if not families:
            log("== %-10s no cases recorded for this role" % role)
            continue
        fam_weight_total = sum((Fraction(COMPONENT_WEIGHTS.get(f, 0.0)) for f in families),
                               Fraction(0))
        role_score = Fraction(0)
        parts = []
        for family in sorted(families):
            got, tot = families[family]
            frac = Fraction(got, tot) if tot else Fraction(0)
            share = (Fraction(COMPONENT_WEIGHTS.get(family, 0.0)) / fam_weight_total
                     if fam_weight_total else Fraction(0))
            role_score += share * frac
            parts.append("%s %d/%d (%.2f)" % (family, got, tot, float(share)))
        reward *= role_score
        roles_scored += 1
        log("== %-10s %.4f  (factor)   %s"
            % (role, float(role_score), "  ".join(parts)))
        detail_roles[role] = {"score": round(float(role_score), 6),
                              "factor": True,
                              "families": {f: {"passed": v[0], "total": v[1]}
                                           for f, v in families.items()}}
    if roles_scored == 0:
        reward = Fraction(0)
    log("== reward = %s" % " x ".join(r for r in sorted(detail_roles, key=lambda r: -ROLE_WEIGHTS.get(r, 0))))

    # One conversion to float, from the exact value, and it may not invent a
    # perfect score.  float() rounds to nearest, so a shortfall below half an
    # ULP would land on 1.0; nothing in the current corpora is remotely that
    # small (one failed case costs at least ~2e-5 here, four orders above an
    # ULP), but the property has to hold by construction rather than by the
    # corpus sizes happening to be small.  This only ever moves a value DOWN.
    exact_reward = reward
    reward = float(exact_reward)
    if exact_reward < 1 and reward >= 1.0:
        log("== note: exact reward %s rounds up on conversion; reporting %r instead"
            % (exact_reward, FLOAT_JUST_BELOW_ONE))
        reward = FLOAT_JUST_BELOW_ONE
    reward = max(0.0, min(1.0, reward))

    DETAIL["roles"] = detail_roles
    DETAIL["cases_reported"] = len(CASES)
    DETAIL["cases_expected"] = len(roles)
    DETAIL["cases_missing"] = missing
    # Every failing case, not a prefix: a sorted prefix would hide every `wl/`
    # failure whenever many cases fail.
    DETAIL["cases_failed"] = sorted([c for c, ok in CASES.items() if not ok])
    DETAIL["reward"] = reward
    DETAIL["finish"] = reason
    DETAIL["elapsed_sec"] = round(time.time() - STARTED, 1)
    DETAIL["budget_sec"] = BUDGET_SEC
    write_detail()
    log("== reward %.6f in %.0fs of %.0fs budget (%s)"
        % (reward, DETAIL["elapsed_sec"], BUDGET_SEC, reason))
    return reward


def write_detail():
    """Provenance lives in a sibling file, never in reward.json: Harbor reads
    reward.json as dict[str, float] and averages every key it finds across the
    steps, so a second key there becomes a second reward."""
    try:
        with open(os.path.join(LOGDIR, "detail.json"), "w") as fh:
            json.dump(DETAIL, fh, indent=1, default=str)
    except OSError as exc:
        log("== warning: could not write detail.json: %s" % exc)


def _out_of_time(signum, frame):  # noqa: ARG001
    log("== budget of %.0fs exhausted: scoring what has been collected" % BUDGET_SEC)
    try:
        restore_peers()
    except Exception:  # noqa: BLE001
        pass
    r = finalize("budget exhausted")
    with open(os.path.join(LOGDIR, "reward.json"), "w") as fh:
        json.dump({"reward": max(0.0, min(1.0, float(r)))}, fh)
    print("REWARD=%.6f" % r, flush=True)
    os._exit(0)


def main():
    # Every file this grader creates for the candidate to read inherits the
    # umask it was started with, and tempfile.mkdtemp is 0700 regardless: pin
    # it so a run under a different umask scores the same.
    os.umask(0o022)
    os.makedirs(LOGDIR, exist_ok=True)
    DETAIL["tests_fingerprint"] = tests_fingerprint()
    posture_ok = verify_privilege_posture()
    DETAIL["started_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    DETAIL["finish"] = "started"
    DETAIL["budget_sec"] = BUDGET_SEC
    write_detail()
    signal.signal(signal.SIGALRM, _out_of_time)
    signal.alarm(int(BUDGET_SEC))
    try:
        os.unlink(TRIPWIRE_LOG)
    except OSError:
        pass
    hide_peers()
    hide_test_tool()
    built = build_candidate()
    gate_ok = gate_sources()
    restore_peers()
    DETAIL["build"] = built
    DETAIL["language_gate"] = gate_ok
    if built and gate_ok and posture_ok:
        # Every component is named up front and every one of them reports, so a
        # component that produced nothing is visible instead of silently
        # shrinking the denominator.
        plan = [
            ("A: git's own t/ tests against %s" % GIT, lambda: component_a(CASES), True,
             int(os.environ.get("GFPC_A_CAP", "1800"))),
            ("B: recorded sessions (closed world)", lambda: component_workload(CASES, False), True, 600),
            ("C: repositories built by real git", lambda: component_corpus(CASES), True, 500),
            ("D: interop sessions against the pinned real gits",
             lambda: component_workload(CASES, True), False, 500),
            ("E: real git reading repositories the candidate built",
             lambda: component_interop(CASES), False, 700),
        ]
        timings = {}
        global COMPONENT_DEADLINE
        for n, (title, fn, peers_hidden, cap) in enumerate(plan):
            COMPONENT_DEADLINE = component_window(cap, [p[3] for p in plan[n + 1:]])
            log("== %s  [window %ds]" % (title, max(0, int(COMPONENT_DEADLINE - time.time()))))
            if peers_hidden:
                hide_peers()
            else:
                restore_peers()
            # test-tool is the t/ harness's; any other phase that runs it is
            # the candidate delegating to git's own code.
            if n == 0:
                restore_test_tool()
            else:
                hide_test_tool()
            t0 = time.time()
            try:
                fn()
            except Exception as exc:  # noqa: BLE001
                # a component that dies costs its own cases, never the run
                log("   component failed: %s" % exc)
            timings[title.split(":")[0]] = round(time.time() - t0, 1)
            restore_peers()
        DETAIL["component_seconds"] = timings
        log("== component seconds: %s  (budget %.0fs)"
            % (", ".join("%s %.0f" % (k, v) for k, v in sorted(timings.items())), BUDGET_SEC))
    else:
        log("== build, language gate, or privilege posture failed: every case scores 0")

    restore_peers()
    reap_candidate()
    restore_test_tool()
    hits = tripwire_hits()
    if hits:
        log("== anti-wrap tripwire: the candidate tried to run a real git")
        for h in hits[:10]:
            log("   %s" % h)
        DETAIL["tripwire"] = hits[:50]
        for k in CASES:
            CASES[k] = False
    after = tests_fingerprint()
    same = (after == DETAIL.get("tests_fingerprint"))
    # the grader's own files must be unchanged after every candidate process
    # has run; report the comparison either way
    log("== is the scorer still the package's? %s" % ("yes" if same else "NO"))
    if not same:
        log("== the grader's own files changed during the run: scoring 0")
        DETAIL["tampered"] = True
        for k in CASES:
            CASES[k] = False
    signal.alarm(0)
    return finalize()


if __name__ == "__main__":
    try:
        r = main()
    except Exception as exc:  # noqa: BLE001
        log("harness error: %s" % exc)
        try:
            r = finalize("harness error")
        except Exception:  # noqa: BLE001
            r = 0.0
    # reward.json is always written here, even after an exception, so a zero
    # this step reports comes with its reason in this log rather than being
    # Harbor's default.
    r = max(0.0, min(1.0, float(r)))
    try:
        os.makedirs(LOGDIR, exist_ok=True)
    except OSError:
        pass
    with open(os.path.join(LOGDIR, "reward.json"), "w") as fh:
        json.dump({"reward": r}, fh)
    print("REWARD=%.6f" % r, flush=True)
    sys.exit(0)
