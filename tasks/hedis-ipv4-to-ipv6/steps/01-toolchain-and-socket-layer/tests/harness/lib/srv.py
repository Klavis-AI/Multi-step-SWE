"""Process and workspace management for the corpus.

Starts and stops the tree's own binaries through the tree's own command line
and configuration file, on fixed ports and fixed paths so that nothing about
the layout is volatile.
"""

import errno
import io
import os
import shutil
import signal
import socket
import stat
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import resp  # noqa: E402
import tracer  # noqa: E402

# Unprivileged identity used for every candidate-built binary in the verifier.
SCORE_UID = 65534
SCORE_GID = 65534


# -- creating things where the score uid can reach --------------------------
#
# Every server directory, configuration file and script this harness makes
# sits inside a scenario workspace the score uid owns, and servers built from
# the candidate tree already run as that uid, in this scenario and in the ones
# running beside it. Whatever stands at such a path may have been put there by
# one of them, so root never follows it: a directory is made with `mkdir`, never
# `makedirs`; a file is created O_CREAT|O_EXCL|O_NOFOLLOW after whatever stood
# there is removed (build.py's `_write_new`); and both are reached through an fd
# on their parent directory, opened O_NOFOLLOW. Only the last component needs
# that guard: each of these paths is one level below a workspace base, and the
# base sits in a root-owned directory (/work/w), which the score uid cannot
# change.

def _parent_fd(path):
    parent = os.path.dirname(os.path.abspath(path))
    return os.open(parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)


def own_dir(path):
    """`path` as a real directory the score uid owns, made without following."""
    pfd = _parent_fd(path)
    name = os.path.basename(path)
    try:
        made = True
        try:
            os.mkdir(name, 0o755, dir_fd=pfd)
        except FileExistsError:
            made = False
        st = os.stat(name, dir_fd=pfd, follow_symlinks=False)
        if not stat.S_ISDIR(st.st_mode):
            raise OSError(errno.ENOTDIR, "not a real directory, refusing", path)
        fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                     dir_fd=pfd)
        try:
            fst = os.fstat(fd)
            if (fst.st_dev, fst.st_ino) != (st.st_dev, st.st_ino):
                raise OSError(errno.EBUSY, "replaced while being made", path)
            if os.geteuid() == 0:
                # A directory found already standing here was made by this
                # harness and handed over, or by a server; one the score uid
                # does not own is neither, and is not handed to it.
                if not made and fst.st_uid != SCORE_UID:
                    raise OSError(errno.EPERM, "not the score uid's, refusing",
                                  path)
                os.fchown(fd, SCORE_UID, SCORE_GID)
        finally:
            os.close(fd)
    finally:
        os.close(pfd)
    return path


def open_new(path, mode=0o644, owner=True):
    """A write fd on a file root creates at `path`, following nothing.

    `owner` hands the file to the score uid, as the configuration files always
    were; without it the file stays root's, as captured output always was.
    """
    pfd = _parent_fd(path)
    name = os.path.basename(path)
    try:
        try:
            st = os.stat(name, dir_fd=pfd, follow_symlinks=False)
        except FileNotFoundError:
            st = None
        if st is not None:
            if stat.S_ISDIR(st.st_mode):
                raise OSError(errno.EISDIR, "a directory stands here", path)
            os.unlink(name, dir_fd=pfd)
        fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     mode, dir_fd=pfd)
    finally:
        os.close(pfd)
    os.fchmod(fd, mode)
    if owner and os.geteuid() == 0:
        os.fchown(fd, SCORE_UID, SCORE_GID)
    return fd


def write_new(path, data, mode=0o644, owner=True):
    """Write `data` (str or bytes) to a new file at `path`; see `open_new`."""
    fd = open_new(path, mode, owner)
    with os.fdopen(fd, "wb" if isinstance(data, bytes) else "w") as f:
        f.write(data)
    return path


def open_regular(path):
    """A binary file object on the regular file at `path`; OSError otherwise.

    The read side of the rule above: opened through its parent's fd, O_NOFOLLOW
    so a link is not followed, and O_NONBLOCK so a FIFO does not hold the
    scorer, then refused unless it is a regular file. It raises where `open()`
    would (a missing file is FileNotFoundError, a directory
    IsADirectoryError), so a caller that turns the exception into a result
    keeps that result. Each call opens afresh, so a caller polling a file that
    a process is appending to sees what was appended.
    """
    pfd = _parent_fd(path)
    try:
        fd = os.open(os.path.basename(path),
                     os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=pfd)
    finally:
        os.close(pfd)
    f = os.fdopen(fd, "rb")
    mode = os.fstat(fd).st_mode
    if not stat.S_ISREG(mode):
        f.close()
        if stat.S_ISDIR(mode):
            raise IsADirectoryError(errno.EISDIR, "Is a directory", path)
        raise OSError(errno.EINVAL, "not a regular file", path)
    return f


def read_regular(path):
    """The bytes of the regular file at `path`, or None (see `open_regular`)."""
    try:
        with open_regular(path) as f:
            return f.read()
    except OSError:
        return None


def read_text(path, errors=None):
    """`open(path, "r", errors=errors).read()`, through `open_regular`.

    The same locale encoding and universal newlines `open()` uses; OSError as
    `open_regular` raises it.
    """
    with io.TextIOWrapper(open_regular(path), errors=errors) as t:
        return t.read()


def _demote():
    try:
        os.setgid(SCORE_GID)
        os.setgroups([])
        os.setuid(SCORE_UID)
    except OSError:
        pass  # already unprivileged, or running as the target user
    os.environ["HOME"] = "/tmp"


def _home_only():
    os.environ["HOME"] = "/tmp"


def preexec_for(tree, launcher):
    """The preexec for `launcher + <tree binary>`.

    The tree's own, unless the launcher is a tracer that demotes the command
    itself (`tracer.launcher` as root): that tracer has to stay root, so the
    trace it writes is out of the traced process's reach.
    """
    if tree.unprivileged and tracer.demotes_itself(launcher):
        return _home_only
    return tree.preexec


class Tree:
    """A built source tree: knows where its binaries are."""

    def __init__(self, root, unprivileged=True):
        self.root = os.path.abspath(root)
        self.unprivileged = unprivileged and os.geteuid() == 0

    def bin(self, name):
        return os.path.join(self.root, "src", name)

    @property
    def preexec(self):
        return _demote if self.unprivileged else None


class Workspace:
    """A scratch directory tree with fixed, non-volatile paths."""

    def __init__(self, base):
        self.base = os.path.abspath(base)
        if os.path.isdir(self.base):
            shutil.rmtree(self.base)
        os.makedirs(self.base)
        if os.geteuid() == 0:
            os.chown(self.base, SCORE_UID, SCORE_GID)

    def sub(self, name):
        return own_dir(os.path.join(self.base, name))

    def cleanup(self):
        shutil.rmtree(self.base, ignore_errors=True)


class Server:
    """One redis-server (or redis-sentinel) process."""

    def __init__(self, tree, workspace, name, port, directives,
                 binary="redis-server", host="::1", extra_argv=(),
                 launcher=(), datadir=None, logfile="server.log", conf=None):
        self.tree = tree
        self.name = name
        self.port = port
        self.host = host
        # `datadir` lets a later server reuse an earlier one's directory, which
        # is how a restart-from-disk is expressed.
        self.dir = datadir if datadir else workspace.sub(name)
        self.logfile = os.path.join(self.dir, logfile)
        self.binary = binary
        self.extra_argv = list(extra_argv)
        self.launcher = list(launcher)
        self.proc = None
        self.pgid = None
        # Set by `tracer.start` when this server runs under a tracer: then
        # `self.proc` is the tracer and the server is its child.
        self.tracee = None
        self.directives = list(directives)
        # `conf` adopts a config file that already exists. Sentinel keeps its
        # whole state in that file and rewrites it in place, so a restart that
        # regenerated it from `directives` would be testing the corpus' idea of
        # the state instead of the server's.
        if conf:
            self.conf = conf
        else:
            self.conf = os.path.join(self.dir, "%s.conf" % name)
            self._write_conf()

    def _write_conf(self):
        write_new(self.conf, "\n".join(self.directives) + "\n")

    def start(self, env=None, stdout=None):
        argv = self.launcher + [self.tree.bin(self.binary), self.conf] + self.extra_argv
        e = dict(os.environ)
        if env:
            e.update(env)
        out = (os.fdopen(open_new(stdout, owner=False), "wb") if stdout
               else subprocess.DEVNULL)
        self._stdout_file = out if stdout else None
        self.proc = subprocess.Popen(
            argv, cwd=self.dir, env=e, stdout=out, stderr=subprocess.STDOUT,
            preexec_fn=preexec_for(self.tree, self.launcher),
            start_new_session=True)
        # Its own process group, so that stopping it can mean the whole tree of
        # processes it started rather than just the one this object holds.
        try:
            self.pgid = os.getpgid(self.proc.pid)
        except OSError:
            self.pgid = None
        return self

    def wait_ready(self, deadline=25.0, host=None):
        """Wait until the server answers, or until it is gone.

        Two things end the wait and the deadline is the third and least
        likely: the server answering, and the server having exited. A server
        that is going to come up comes up in well under a second, and one that
        is never going to come up -- a configuration the tree rejects, a port
        it cannot bind, a binary that dies on its own directives -- is already
        a corpse by the time the first probe returns. Waiting the full
        deadline out on a corpse costs every scenario after this one the time,
        and says only "never became ready" when the exit status is right
        there.

        The success path is unchanged: the same `resp.wait_ready` decides
        readiness, and the same `resp.Timeout` comes out of both the exited
        and the expired case, so a caller catching it catches this too.
        `self.proc` is the tracer rather than the server when `launcher` is
        set, and a tracer that exits detaches from its tracee instead of
        killing it, so an exited `self.proc` is confirmed with one more probe
        before it is believed.
        """
        h = host if host is not None else self.host
        end = time.time() + deadline
        last = None
        while True:
            left = end - time.time()
            if left <= 0:
                break
            try:
                resp.wait_ready(h, self.port, deadline=min(0.5, left))
                return self
            except resp.Timeout as e:
                last = e
            except Exception:
                self.dump_log()
                raise
            if self.proc is not None and self.proc.poll() is not None:
                try:
                    resp.wait_ready(h, self.port, deadline=0.5)
                    return self
                except resp.Timeout:
                    pass
                except Exception:
                    self.dump_log()
                    raise
                self.dump_log()
                raise resp.Timeout(
                    "server at %s:%s exited with status %s before it became "
                    "ready" % (h, self.port, self.proc.returncode))
        self.dump_log()
        raise last if last is not None else resp.Timeout(
            "server at %s:%s never became ready" % (h, self.port))

    def client(self, host=None, timeout=10.0):
        return resp.Client(host if host is not None else self.host, self.port,
                           timeout=timeout)

    def log(self):
        # The server's own directory: no link followed (`read_regular`). [r5]
        data = read_regular(self.logfile)
        return b"" if data is None else data

    def dump_log(self, stream=sys.stderr):
        stream.write("---- %s log ----\n" % self.name)
        stream.write(self.log().decode("utf-8", "backslashreplace"))
        stream.write("---- end %s ----\n" % self.name)

    def stop(self, sig=signal.SIGTERM, timeout=10.0):
        if self.proc is None:
            return None
        if self.proc.poll() is None:
            try:
                self.proc.send_signal(sig)
            except OSError:
                pass
            end = time.time() + timeout
            while time.time() < end and self.proc.poll() is None:
                time.sleep(0.02)
            if self.proc.poll() is None:
                self.proc.kill()
                self.proc.wait(timeout=5)
        rc = self.proc.returncode
        if getattr(self, "_stdout_file", None):
            try:
                self._stdout_file.close()
            except Exception:
                pass
        self.proc = None
        # A traced server is a grandchild: `self.proc` is the tracer, and a
        # tracer that dies detaches from its tracee rather than killing it. Stop
        # has to say so explicitly, or the server outlives the scenario, keeps
        # its port, and answers the next run's probes -- which is not a failure
        # anything downstream can see, because what it answers is plausible.
        if self.tracee:
            try:
                os.kill(self.tracee, signal.SIGKILL)
            except OSError:
                pass
            self.tracee = None
        # And whatever else is still in the group: a tracer that never reported
        # its child, a forked background save, a server this one started. The
        # group has only this server's descendants in it, and it is already
        # empty in the ordinary case, where this raises ESRCH and does nothing.
        if self.pgid:
            try:
                os.killpg(self.pgid, signal.SIGKILL)
            except OSError:
                pass
            self.pgid = None
        return rc

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.stop()


def busy_ports(base, count):
    """Which of the `count` ports from `base` something is already listening on.

    A scenario that probes a port another process happens to hold is measuring
    that process. Nothing downstream can catch it: a stray server answers like a
    healthy one, so both trees agree and what they agree about is the stray.
    The check therefore runs before the scenario does, and it is the same bind
    the tree's own `anetTcpServer` does -- `SO_REUSEADDR`, wildcard address --
    so that a port left in `TIME_WAIT` by the previous pass reads as free, which
    it is, while a listening socket reads as busy, which it also is.
    """
    busy = []
    for port in range(base, base + count):
        for family, addr in ((socket.AF_INET6, "::"), (socket.AF_INET, "0.0.0.0")):
            try:
                s = socket.socket(family, socket.SOCK_STREAM)
            except OSError:
                continue          # no such family here; the other one answers
            try:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                if family == socket.AF_INET6:
                    # Dual-stack on purpose: a v4-only stray on this port has to
                    # collide with the probe too.
                    try:
                        s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
                    except OSError:
                        pass
                s.bind((addr, port))
            except OSError:
                busy.append(port)
                break
            finally:
                s.close()
    return busy


def info_field(client, section, field):
    """One field out of INFO, or None if the server does not report it."""
    blob = client.cmd("INFO", section)[1]
    if not isinstance(blob, (bytes, bytearray)):
        return None
    for line in blob.decode("utf-8", "replace").splitlines():
        if line.startswith(field + ":"):
            return line.split(":", 1)[1].strip()
    return None


def wait_info(client, section, field, want, deadline=30.0, interval=0.05):
    """Poll INFO until `field` reads `want`. Returns whether it got there."""
    end = time.time() + deadline
    while time.time() < end:
        if info_field(client, section, field) == want:
            return True
        time.sleep(interval)
    return False


def run_tool(tree, name, args, timeout=60, stdin=None, env=None, cwd=None,
             launcher=(), grace=3.0):
    """Run one of the tree's own binaries and capture its output.

    A tool that outstays `timeout` is asked to leave before it is made to:
    SIGTERM, `grace` seconds, then SIGKILL. `subprocess.run(timeout=...)` goes
    straight to SIGKILL, which matters for the modes that only stop when they
    are stopped -- `--stat`, `--latency`, `--slave`. Either way the result is
    reported as "timeout", so nothing recorded depends on which signal ended
    it; what depends on it is whether the process got to run its exit handlers,
    and under a coverage build those handlers are what write the profile.
    """
    argv = list(launcher) + [tree.bin(name)] + [str(a) for a in args]
    e = dict(os.environ)
    if env:
        e.update(env)
    p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, env=e, cwd=cwd,
                         preexec_fn=preexec_for(tree, launcher),
                         start_new_session=True)
    try:
        pgid = os.getpgid(p.pid)
    except OSError:
        pgid = None
    try:
        out, _ = p.communicate(input=stdin, timeout=timeout)
        return p.returncode, out
    except subprocess.TimeoutExpired:
        pass
    p.terminate()
    try:
        out, _ = p.communicate(timeout=grace)
    except subprocess.TimeoutExpired:
        # `launcher` makes the tool a grandchild -- the tracer is what `p`
        # holds -- and a tracer that is killed detaches from its tracee instead
        # of killing it. The whole process group has to go, and it has to go
        # *before* the last `communicate()`: that call waits for end-of-file on
        # a pipe the grandchild still holds open, so killing only `p` would
        # leave this blocked for as long as the tool ran, which on a connection
        # to a blackholed address is the kernel's own SYN-retry timeout (about
        # two minutes per address) rather than the `timeout` asked for here.
        # A tool that ends on SIGTERM never reaches this branch, so the modes
        # that stop only when they are stopped keep the output they printed.
        if pgid:
            try:
                os.killpg(pgid, signal.SIGKILL)
            except OSError:
                pass
        p.kill()
        out, _ = p.communicate()
    return "timeout", (out or b"")


# `redis-benchmark` ends a run by calling `aeStop()` from the read handler that
# saw the last reply, and `freeClient()` has already taken `liveclients` to zero
# by then. `aeProcessEvents` handles time events *after* file events in the same
# iteration, so if the progress timer is due in that iteration `showThroughput`
# runs next, sees no live clients, prints this line and `exit(1)`s -- after the
# run had in fact completed. The timer is due every 250 ms, so a short run only
# loses the race when the host is loaded enough to stretch an iteration past a
# quarter of a second, and the same command then reports success or failure
# depending on what else was running at the time.
#
# It is a defect of the instrument: it is in the tree at the pin, no step of this
# migration touches `showThroughput` or the event loop, and the outcome it
# randomises is "did the benchmark reach the server", which every one of these
# runs is asking about for a different reason. A run that lost the race is
# therefore run again rather than recorded. A tree that really does drop the
# benchmark's connections loses every attempt and records the abort, which is
# the outcome that should be recorded in that case.
BENCH_ABORT = b"All clients disconnected... aborting."
BENCH_ATTEMPTS = 6


def run_benchmark(tree, args, **kw):
    """`run_tool` for redis-benchmark, retried past the progress-timer race."""
    for attempt in range(BENCH_ATTEMPTS):
        rc, out = run_tool(tree, "redis-benchmark", args, **kw)
        if BENCH_ABORT not in (out or b""):
            return rc, out
        time.sleep(0.2 * (attempt + 1))
    return rc, out


def start_server_expect_failure(tree, workspace, name, directives, timeout=15,
                                launcher=()):
    """Start a server expected to refuse to start.

    Returns (rc, stdout, logfile). Both are needed: a configuration-file error
    is reported on stdout before the log is open, while a failure to acquire a
    listening socket is reported into the log.

    `launcher` is the same argv prefix `Server` and `run_tool` take, for the
    failures that are a property of the kernel the server was started on rather
    than of the configuration it was handed.
    """
    d = workspace.sub(name)
    conf = os.path.join(d, "%s.conf" % name)
    write_new(conf, "\n".join(directives) + "\n")
    try:
        p = subprocess.run(
            list(launcher) + [tree.bin("redis-server"), conf],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=timeout, cwd=d,
            preexec_fn=preexec_for(tree, launcher))
        rc, out = p.returncode, p.stdout
    except subprocess.TimeoutExpired as t:
        rc, out = "timeout", (t.stdout or b"")
    log = read_regular(os.path.join(d, "server.log"))  # no link followed [r5]
    if log is None:
        log = b""
    return rc, out, log
