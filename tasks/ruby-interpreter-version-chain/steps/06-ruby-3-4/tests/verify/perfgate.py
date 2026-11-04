"""Instruction-count gate for the 3.4 milestone.

Each benchmark program is executed under `valgrind --tool=callgrind`, which
counts the machine instructions the process executes. The budgets are CRuby
3.4.10's own counts for the same programs, measured the same way.

Each run is capped at 240 s, and every run must also be over by `end` (the
runner's PERF_END), so the gate as a whole cannot push the verifier past
Harbor's 3,600 s.  The components inside that budget are bounded at 900 s for
the rebuild, about 1,100 s for the cases (one per program, ten seconds each,
seven at a time) and 5 x 240 s here, so a candidate that is slow everywhere
still finishes before the verifier is killed.  A program that does not finish
counts as over budget.

The program under measurement is the candidate's, so it runs as a scored case
does: as the unprivileged candidate uid, in its own session, and from a scratch
copy of the benchmark (the candidate cannot read /tests).

valgrind takes options from ~/.valgrindrc, $VALGRIND_OPTS and ./.valgrindrc as
well as from its command line, and reads a dot file as long as the uid it runs
as owns it.  So nothing the candidate's uid can write is in reach of a counted
run: every process of that uid is killed before each benchmark, so nothing an
earlier run left behind is still running; the scratch directory, the run's
working directory, stays root's and unwritable; the run gets `env -i` and only
PATH, TMPDIR and an empty root-owned HOME; and --command-line-only=yes turns all
three option sources off, so the command line below is the whole of valgrind's
configuration.
"""
import hashlib
import os
import re
import resource
import shutil
import signal
import subprocess
import tempfile
import threading
import time

# valgrind's own report, on a pipe of its own (--log-fd).  The program under
# measurement shares valgrind's process and could write that pipe too, so the
# report is accepted only when it is exactly one run's: one `Command:` header
# and one `I refs:` summary, both under valgrind's pid, and a clean exit.
# Any extra line of either kind -- another image, another process, or text the
# program wrote -- makes the run unmeasured rather than trusted.
IR_RE = re.compile(rb"I\s+refs:")
SUMMARY_RE = re.compile(rb"^==(\d+)== I\s+refs:\s+([0-9,]+)\s*$", re.M)
CMD_RE = re.compile(rb"Command:")
CMD_LINE_RE = re.compile(rb"^==(\d+)== Command: ", re.M)


def _as_candidate(uid, gid):
    def _apply():
        os.setsid()
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        if uid is not None:
            os.setgid(gid)
            os.setgroups([])
            os.setuid(uid)
    return _apply


def measure(ruby, program, uid=None, gid=None, end=None):
    """Instructions executed, and a digest of what the program printed.  Coming
    in under budget is not enough: a binary that prints nothing executes almost
    no instructions, so the count alone rewards doing nothing.  The benchmark
    counts only when the answer is right AND the work was cheap."""
    limit = 240.0
    if end is not None:
        limit = min(limit, end - time.monotonic())
        if limit < 5:
            return None, None
    scratch = tempfile.mkdtemp(prefix="perf-")
    home = tempfile.mkdtemp(prefix="perfhome-")
    path = os.path.join(scratch, os.path.basename(program))
    proc = None
    rfd, wfd = os.pipe()
    chunks = []

    def _drain():
        while True:
            try:
                b = os.read(rfd, 65536)
            except OSError:
                break
            if not b:
                break
            chunks.append(b)

    reader = threading.Thread(target=_drain, daemon=True)
    try:
        shutil.copyfile(program, path)
        os.chmod(path, 0o644)
        # Both stay root's (mkdtemp made them): the candidate's uid may enter
        # the scratch directory and read the program, and may write neither.
        os.chmod(scratch, 0o711)
        os.chmod(home, 0o555)
        reader.start()
        proc = subprocess.Popen(
            ["/usr/bin/env", "-i", "PATH=/usr/bin:/bin", "HOME=" + home,
             "TMPDIR=/tmp",
             "/usr/bin/valgrind", "--command-line-only=yes", "--tool=callgrind",
             "--callgrind-out-file=/dev/null",
             "--trace-children=yes", "--log-fd=%d" % wfd, ruby, path],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=scratch,
            env={}, pass_fds=(wfd,), preexec_fn=_as_candidate(uid, gid))
        os.close(wfd)
        wfd = -1
        out, err = proc.communicate(timeout=limit)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.communicate(timeout=10)
        except (OSError, ValueError, subprocess.TimeoutExpired):
            pass
        return None, None
    except OSError:
        return None, None
    finally:
        if wfd >= 0:
            os.close(wfd)
        shutil.rmtree(scratch, ignore_errors=True)
        shutil.rmtree(home, ignore_errors=True)
        if proc is not None:
            try:
                os.killpg(proc.pid, signal.SIGKILL)   # nothing of this run outlives it
            except OSError:
                pass
        reader.join(10)
        try:
            os.close(rfd)
        except OSError:
            pass
    if reader.is_alive() or proc.returncode != 0:
        return None, None
    log = b"".join(chunks)
    sums = SUMMARY_RE.findall(log)
    cmds = CMD_LINE_RE.findall(log)
    if (len(IR_RE.findall(log)) != 1 or len(sums) != 1 or len(CMD_RE.findall(log)) != 1
            or len(cmds) != 1 or int(sums[0][0]) != proc.pid or int(cmds[0]) != proc.pid):
        return None, None
    return int(sums[0][1].replace(b",", b"")), hashlib.sha256(out).hexdigest()


def perf_score(ruby, tests_dir, uid=None, gid=None, end=None, reap=None):
    """`reap` kills every process of the candidate's uid; it runs before each
    benchmark, so no run starts with anything of an earlier one alive."""
    root = os.path.join(tests_dir, "perf")
    budgets = []
    with open(os.path.join(root, "budgets.tsv")) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").split("\t")
            name, budget = parts[0], int(parts[1])
            want = parts[2].strip() if len(parts) > 2 else None
            budgets.append((name, budget, want))
    ok = 0
    detail = []
    for name, budget, want in budgets:
        prog = os.path.join(root, "bench", name + ".rb")
        if reap is not None:
            reap()
        ir, got = measure(ruby, prog, uid, gid, end)
        if ir is None:
            detail.append("%s=nomeasure" % name)
            continue
        if want is not None and got != want:
            detail.append("%s=wrong-output" % name)
            continue
        if ir <= budget:
            ok += 1
            detail.append("%s=%d<=%d" % (name, ir, budget))
        else:
            detail.append("%s=%d>%d" % (name, ir, budget))
    if not budgets:
        return 0.0, "no budgets"
    return ok / len(budgets), "%d/%d within budget (%s)" % (ok, len(budgets), " ".join(detail))
