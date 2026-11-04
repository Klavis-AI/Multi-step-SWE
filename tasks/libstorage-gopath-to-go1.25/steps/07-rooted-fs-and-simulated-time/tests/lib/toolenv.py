"""Process plumbing shared by the scorer and the gates.

Everything the verifier runs against the candidate tree goes through `run`, so
there is one place that decides the environment, the working directory and the
timeout. The verifier's own toolchain and module cache are fixed by the image:
nothing here reads a setting out of `/app`, because a candidate could write one.
"""
import os
import signal
import subprocess
import time

# The repository under test, restored from the collected artifact. The build,
# the replay and the repository's own tests run here; score.py points this at
# GATED_ROOT before the first gate.
ROOT = "/app"

# What the gates read. test.sh copies /app and the image's
# module cache here, as root, before any candidate code runs, and leaves both
# root-owned and read-only: /app belongs to the candidate's uid while its server
# and its tests run, so a gate that read /app afterwards would be measuring
# whatever those processes left there, not the tree that was handed in.
GATED_ROOT = "/run/lsgate/app"
GATED_MODCACHE = "/run/lsgate/modcache"

# The uid every candidate process runs at, and the scorer with them
# (helpers.sh SCORE_UID).
SCORER_UID = 65534

# reap_strays only acts inside the verifier image, never on a machine where
# this file is run by hand.
IN_VERIFIER = os.path.realpath(__file__).startswith("/tests/lib/")

# Where the verifier's own tools live, built into the image.
TOOLS = "/opt/gates/bin"

# The toolchain environment. GOFLAGS is empty: a `-mod=` or `-tags=`
# the candidate wants has to be expressed in the repository, not inherited.
# GOENV=off because an *empty* variable is not a setting to `go`: it falls back
# to the user's go env file, $HOME/.config/go/env, and HOME here is /tmp, which
# candidate code running at this uid (its server, its tests) can write before
# the gates run -- `go env -w GOFLAGS=-exec=...` would then decide every later
# `go test`.
BASE_ENV = {
    "PATH": TOOLS + ":/usr/local/go/bin:/usr/local/bin:/usr/bin:/bin",
    "HOME": "/tmp",
    "GOENV": "off",
    "TMPDIR": "/tmp",
    "LANG": "C.UTF-8",
    "GOTOOLCHAIN": "local",
    "GOFLAGS": "",
    # A `go.work` in the tree would be found by every go command and its
    # `replace` lines would override the module's own version selection, which
    # the gomod-directives gate reads from go.mod alone. The module is graded
    # as go.mod declares it.
    "GOWORK": "off",
    "GOPROXY": "file:///opt/gomodmirror",
    "GOSUMDB": "off",
    "GONOSUMDB": "*",
    "GONOSUMCHECK": "1",
    "GOFLAGS_UNUSED": "",
    "GOPATH": "/tmp/gopath",
    "GOCACHE": "/tmp/gocache",
    "GOMODCACHE": "/tmp/gopath/pkg/mod",
    "GO111MODULE": "on",
    "CGO_ENABLED": "0",
}


class Output(object):
    __slots__ = ("code", "stdout", "stderr", "timedout")

    def __init__(self, code, stdout, stderr, timedout=False):
        self.code = code
        self.stdout = stdout
        self.stderr = stderr
        self.timedout = timedout


def base_env(extra=None):
    env = dict(BASE_ENV)
    env.update(extra or {})
    return env


def gate_env(scratch):
    """The toolchain environment the gates run in.

    `scratch` is a directory score.py creates after `reap_strays`, under a name
    nothing could have guessed before it existed, so the build cache, the
    analysis cache and HOME the gates use start empty and hold nothing a
    candidate process wrote. The module cache is root's read-only copy.
    `-buildvcs=false`: the gates read a root-owned copy, and VCS stamping
    would run `git` over a `.git` the candidate wrote; a gate has no use for
    the stamp.
    """
    return base_env({
        "HOME": scratch,
        "XDG_CACHE_HOME": os.path.join(scratch, "xdg"),
        "GOPATH": os.path.join(scratch, "gopath"),
        "GOCACHE": os.path.join(scratch, "gocache"),
        "GOMODCACHE": GATED_MODCACHE,
        "GOFLAGS": "-buildvcs=false",
    })


def _status(pid):
    """/proc/<pid>/status as a dict, or None once the process is gone."""
    try:
        with open("/proc/%s/status" % pid) as fh:
            text = fh.read(8192)
    except (IOError, OSError):
        return None
    fields = {}
    for line in text.splitlines():
        key, _, value = line.partition(":")
        fields[key] = value.strip()
    return fields


def _runs_as(fields, uid):
    """Whether any of a process's real, effective or saved uids is `uid`."""
    try:
        ids = [int(x) for x in fields.get("Uid", "").split()[:3]]
    except ValueError:
        return False
    return uid in ids


def _ancestors(pid):
    chain = []
    while pid > 1 and len(chain) < 64:
        fields = _status(pid)
        try:
            pid = int((fields or {}).get("PPid", "0"))
        except ValueError:
            break
        if pid <= 0 or pid in chain:
            break
        chain.append(pid)
    return chain


def _strays(uid, spare):
    """Live processes at `uid`, other than `spare`. Zombies have exited."""
    found = []
    for name in os.listdir("/proc"):
        if not name.isdigit() or int(name) in spare:
            continue
        fields = _status(name)
        if fields is None or not _runs_as(fields, uid):
            continue
        if fields.get("State", "")[:1] in ("Z", "X"):
            continue
        found.append(int(name))
    return found


def reap_strays():
    """Kill every other process at the scorer's uid and wait until none is left.

    The scorer, the candidate's server and the candidate's tests share one uid,
    so anything the candidate's code leaves running -- a `setsid` daemon its
    tests started, a server that ignored its stop -- could otherwise kill or
    stop a tool the gates run, drain its output from /proc/<pid>/fd, or edit a
    cache it reads. score.py calls this after the replay and the
    repository's own tests, before the first gate, and after every gate that
    runs candidate code.

    `kill(-1, SIGKILL)` reaches every process the scorer may signal, which is
    every process at its uid in this container, except the scorer itself; the
    kernel walks the process list under its lock, so a process that forks to
    move away cannot slip past it the way it can slip past a scan of /proc.
    test.sh keeps the deadline that wraps this process at root, so no ancestor
    is in that set; if one were, the scan below kills by pid and spares it.
    """
    uid = os.getuid()
    if not IN_VERIFIER or uid != SCORER_UID or os.geteuid() != uid:
        return {"skipped": True}
    me = os.getpid()
    ancestors = _ancestors(me)
    spare = set([me] + ancestors)
    broadcast = not any(_runs_as(_status(p) or {}, uid) for p in ancestors)
    found = _strays(uid, spare)
    left = found
    rounds = 0
    while rounds < 200:
        rounds += 1
        if broadcast:
            try:
                os.kill(-1, signal.SIGKILL)
            except OSError:
                pass
        else:
            for pid in left:
                try:
                    os.kill(pid, signal.SIGKILL)
                except OSError:
                    pass
        time.sleep(0.05)
        left = _strays(uid, spare)
        if not left:
            break
    return {"found": len(found), "left": len(left), "rounds": rounds,
            "broadcast": broadcast, "left_pids": left[:10]}


def run(argv, env=None, cwd=None, timeout=900, stdin=None):
    """Run a command and return its output; a timeout is a result, not a crash."""
    try:
        proc = subprocess.Popen(
            argv, cwd=cwd or ROOT, env=env or base_env(),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            stdin=subprocess.PIPE, start_new_session=True)
    except OSError as err:
        return Output(127, "", str(err))
    try:
        out, err = proc.communicate(stdin, timeout=timeout)
        return Output(proc.returncode, _text(out), _text(err))
    except subprocess.TimeoutExpired:
        _kill(proc)
        out, err = proc.communicate()
        return Output(124, _text(out), _text(err), timedout=True)


def _kill(proc):
    try:
        os.killpg(os.getpgid(proc.pid), 9)
    except OSError:
        pass


def _text(raw):
    if raw is None:
        return ""
    return raw.decode("utf-8", "replace")
