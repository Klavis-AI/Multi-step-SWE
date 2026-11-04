"""Start and stop the servers a scenario runs against.

`Candidate` drives the artifact through Dendrite's published monolith interface:
`make` in the workspace produces `bin/dendrite`, which is started with
`--config`, `--http-bind-address`, `--https-bind-address`, `--tls-cert` and
`--tls-key`.  `Synapse` is the recording oracle and the live federation partner.
"""

from __future__ import annotations

import base64
import json
import os
import resource
import shutil
import signal
import socket
import stat
import pathlib
import subprocess
import sys
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path

DEFAULT_SEED = bytes(range(32))          # fixed: deterministic server signing key


def free_port(start: int) -> int:
    port = start if start < 64000 else 20000 + (start % 40000)
    while port < start + 4000:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                s.bind(("127.0.0.1", port))
                return port
            except OSError:
                port += 1
    raise RuntimeError("no free port")


def matrix_key_pem(seed: bytes = DEFAULT_SEED, key_id: str = "abcdef") -> str:
    body = base64.b64encode(seed).decode()
    lines = [body[i:i + 64] for i in range(0, len(body), 64)]
    return (
        "-----BEGIN MATRIX PRIVATE KEY-----\n"
        f"Key-ID: ed25519:{key_id}\n\n" + "\n".join(lines) + "\n"
        "-----END MATRIX PRIVATE KEY-----\n"
    )


def wait_http(url: str, timeout: float = 60.0, proc: subprocess.Popen | None = None
              ) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            return False
        try:
            with urllib.request.urlopen(url, timeout=2) as r:
                if r.status < 500:
                    return True
        except Exception:
            time.sleep(0.1)
    return False


def make_tls(dirpath: Path, server_name: str) -> tuple[Path, Path]:
    crt, key = dirpath / "server.crt", dirpath / "server.key"
    if crt.exists() and key.exists():
        return crt, key
    host = server_name.split(":")[0]
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
         "-keyout", str(key), "-out", str(crt), "-days", "3650",
         "-subj", f"/CN={host}",
         "-addext", f"subjectAltName=IP:127.0.0.1,DNS:{host},DNS:localhost"],
        check=True, capture_output=True,
    )
    return crt, key


# --------------------------------------------------------------------------- #
# What a candidate's own processes inherit
# --------------------------------------------------------------------------- #
# The verifier runs inside the grading container, so its environment is the
# grader's environment: whatever the runner injected to say which tests are
# selected, where the result is written, which job this is.  Two processes in
# this file are written by the candidate -- `make`, which runs a Makefile from
# the collected workspace, and the homeserver binary that Makefile produced --
# and neither has any business reading that.
#
# A defence attached to a list of call sites is only as complete as the list,
# and nothing checks the list.  So the environment is not scrubbed of known-bad
# names; it is built from nothing, by name, for the candidate alone.  Anything
# the grader adds later is excluded by construction rather than by being
# remembered.  The harness's own children -- the pinned reference homeserver,
# the test clients -- keep the full environment, because they are the harness.
CANDIDATE_ENV_KEEP = (
    "PATH",             # gcc, ld, sh
    "HOME",             # overridden per run below
    "LANG", "LC_ALL", "LC_CTYPE", "TZ",
    "TMPDIR",           # the compiler's scratch space
    "SSL_CERT_FILE", "SSL_CERT_DIR",
    "DENDRITE_CONFIG",  # the interface's own config-path variable
    "SOURCE_DATE_EPOCH",
)


def candidate_env(extra: dict[str, str] | None = None) -> dict[str, str]:
    """The environment a candidate-written process is given.

    Built by allow-list from nothing, so that a variable the grading runner
    starts setting tomorrow is excluded without this file being edited.
    """
    env = {k: os.environ[k] for k in CANDIDATE_ENV_KEEP if k in os.environ}
    env.setdefault("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
    env.update(extra or {})
    return env


# A server under test gets a hard ceiling on both memory and wall-clock time.
# A candidate with a runaway allocation has to score a failure rather than take
# the pod down with it -- from the outside those two look identical, and only
# one of them is the candidate's fault.  The figures are the ones stated in
# every step's instruction.
MEMORY_CEILING_MB = 4096
WALL_CLOCK_CEILING_SEC = 900


# --------------------------------------------------------------------------- #
# Who a candidate's own processes run as
# --------------------------------------------------------------------------- #
# The verifier runs as root, because the grading image starts it that way.  Two
# processes here are written by the candidate -- `make` over the collected
# workspace, and the binary it produced -- and root is the wrong user for both:
# the scored transcripts are read into memory and deleted before `make` runs
# (verify.load_and_seal), but the scenario sources and the runner are still on
# disk under /tests, and a Makefile is a program: two lines in a build file
# (copy, chmod) are enough to defeat a relocate-and-drop defence.  A build does
# not read like a launcher, yet run as root it would be the most privileged one.
#
# So the candidate's processes run as an unprivileged account and the tree they
# work in is handed to that account first.  `test.sh` takes /tests away from
# everyone else in the same breath; root keeps it, and root is the harness.
CANDIDATE_USER = "mxbuild"


def candidate_uid() -> tuple[int, int] | None:
    """(uid, gid) for candidate-written processes, or None to stay as we are.

    Returns None when the verifier is not root -- there is nothing to drop --
    and when the account does not exist, which is the case in an authoring
    checkout and must not turn into a failure to build.
    """
    if os.geteuid() != 0:
        return None
    try:
        import pwd
        ent = pwd.getpwnam(CANDIDATE_USER)
    except (ImportError, KeyError):
        # Root with no candidate account is a broken grading image, not an
        # authoring checkout: refuse rather than run candidate code as root.
        raise RuntimeError(f"running as root but the candidate account "
                           f"`{CANDIDATE_USER}` does not exist: refusing to run "
                           "candidate code as root")
    return ent.pw_uid, ent.pw_gid


def give_to_candidate(path) -> None:
    """Hand a directory tree to the candidate account, if there is one."""
    ids = candidate_uid()
    if ids is None:
        return
    uid, gid = ids
    root = pathlib.Path(path)
    try:
        os.chown(root, uid, gid)
    except OSError:
        return
    for f in root.rglob("*"):
        try:
            os.chown(f, uid, gid, follow_symlinks=False)
        except OSError:
            pass


def strip_setid(path) -> list[str]:
    """Clear set-user-ID and set-group-ID bits everywhere under `path`.

    The collected workspace is restored into this container by root with its
    modes intact, so a set-user-ID root program the agent left in it would run
    as root for any account that can execute it -- the candidate account
    included.  This runs before anything of the candidate's does, so nothing
    can swap a path between the look and the change; symlinks are neither
    followed nor descended into.  Returns whatever still carries a bit
    afterwards (empty when the tree is clean), so the caller can refuse.
    """
    def walk():
        top = os.fspath(path)
        yield top
        if os.path.islink(top):
            return
        for dirpath, dirnames, filenames in os.walk(top, followlinks=False):
            for name in dirnames + filenames:
                yield os.path.join(dirpath, name)

    for p in walk():
        try:
            st = os.lstat(p)
        except OSError:
            continue
        if stat.S_ISLNK(st.st_mode) or not st.st_mode & 0o6000:
            continue
        try:
            os.chmod(p, stat.S_IMODE(st.st_mode) & ~0o6000)
        except OSError:
            pass
    left = []
    for p in walk():
        try:
            st = os.lstat(p)
        except OSError:
            continue
        if not stat.S_ISLNK(st.st_mode) and st.st_mode & 0o6000:
            left.append(p)
    return left


# prctl(PR_SET_NO_NEW_PRIVS, 1): after it, no execve -- of a set-user-ID file
# anywhere, a restored one included -- can raise the privileges of the process
# or of anything it starts.  Resolved here, in the parent, so the child only
# makes the call.
PR_SET_NO_NEW_PRIVS = 38
_LIBC = None


def _no_new_privs_fn():
    global _LIBC
    import ctypes
    if _LIBC is None:
        _LIBC = ctypes.CDLL(None, use_errno=True)
    fn = _LIBC.prctl
    fn.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong,
                   ctypes.c_ulong, ctypes.c_ulong]
    fn.restype = ctypes.c_int
    return fn


def _ceilings(memory_mb: int = MEMORY_CEILING_MB, drop: bool = False):
    """A `preexec_fn` that caps the child and puts it in its own process group.

    With `drop`, it also becomes the unprivileged candidate account -- after
    the limits, because setting them needs no privilege and dropping is
    irreversible -- and sets no_new_privs first, so no set-user-ID program
    it executes can hand any of that back.
    """
    ids = candidate_uid() if drop else None
    prctl = _no_new_privs_fn() if ids is not None else None

    def apply():
        os.setsid()
        soft = memory_mb * 1024 * 1024
        for limit in (resource.RLIMIT_AS, resource.RLIMIT_DATA):
            try:
                cur_soft, cur_hard = resource.getrlimit(limit)
                hard = cur_hard if cur_hard != resource.RLIM_INFINITY else soft
                resource.setrlimit(limit, (min(soft, hard), hard))
            except (ValueError, OSError):
                pass
        try:
            resource.setrlimit(resource.RLIMIT_CPU,
                               (WALL_CLOCK_CEILING_SEC, WALL_CLOCK_CEILING_SEC))
        except (ValueError, OSError):
            pass
        if ids is not None:
            uid, gid = ids
            if prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0:
                raise OSError("prctl(PR_SET_NO_NEW_PRIVS) failed: refusing to "
                              "run candidate code that a set-user-ID file "
                              "could raise")
            os.setgroups([gid])
            os.setgid(gid)
            os.setuid(uid)
    return apply


# Runtimes and servers a homeserver written in something else would appear as.
# `verify.py` checks the tree once at startup; `Candidate.foreign_child` checks
# it again at the end of every scenario.
FOREIGN_PROGRAMS = ("python", "python3", "pypy", "node", "ruby", "perl", "java",
                    "erl", "beam", "elixir", "synapse", "conduit", "conduwuit",
                    "ejabberd")

# Marks that only a foreign-language runtime, or an executable packer, leaves in
# a program file.  `ldd` sees a runtime that is *linked*; a Go program (Dendrite
# itself is one) or a Rust one (Conduit, conduwuit) carries its runtime inside
# the executable, so neither shows up there, and neither does its name -- the
# artifact is called `dendrite` whatever it is.  A C program built from this
# workspace carries none of these.  Checked on bin/dendrite after the build
# (verify.provenance), when it is first started (verify.no_trampoline), and at
# the end of every scenario on every program the candidate account is running,
# the server's own pid included, so a server that turns into something else
# after startup is caught too.
FOREIGN_RUNTIME_MARKS = (
    (b"\xff Go buildinf:", "a Go runtime"),
    (b"runtime.gopanic", "a Go runtime"),
    (b"RUST_BACKTRACE", "a Rust runtime"),
    (b"library/std/src/", "a Rust runtime"),
    (b"This file is packed with the UPX", "an executable packer"),
)
# Shipped by the image and not writable by the candidate account.
SYSTEM_PREFIXES = ("/usr/", "/lib/", "/lib32/", "/lib64/", "/libx32/", "/opt/synapse/")
_RUNTIME_SCAN: dict = {}


def open_regular(path, follow: bool = True):
    """Open `path` for reading only if it is a regular file, as a binary file
    object; None otherwise.

    Root reads files the candidate account wrote, or could have swapped since
    they were listed.  A FIFO or a device in their place would block, or never
    end, so the open does not wait (O_NONBLOCK) and anything that is not a
    regular file is refused before a byte is read.  With `follow=False` a
    symlink is refused as well."""
    flags = os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC
    if not follow:
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(path, flags)
    except OSError:
        return None
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            os.close(fd)
            return None
        return os.fdopen(fd, "rb")
    except OSError:
        os.close(fd)
        return None


def foreign_runtime(path) -> str | None:
    """The foreign runtime or packer the program file at `path` carries, if any."""
    f = open_regular(path)
    if f is None:
        return None
    try:
        st = os.fstat(f.fileno())
    except OSError:
        f.close()
        return None
    key = (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns)
    if key in _RUNTIME_SCAN:
        f.close()
        return _RUNTIME_SCAN[key]
    found = None
    overlap = max(len(m) for m, _ in FOREIGN_RUNTIME_MARKS)
    try:
        with f:
            if b"UPX!" in f.read(4096):
                found = "an executable packer"
            f.seek(0)
            tail = b""
            # No further than the size it had when it was opened: a file the
            # candidate is still appending to must not keep root reading.
            left = st.st_size
            while found is None and left > 0:
                chunk = f.read(min(1 << 20, left))
                if not chunk:
                    break
                left -= len(chunk)
                buf = tail + chunk
                for mark, what in FOREIGN_RUNTIME_MARKS:
                    if mark in buf:
                        found = what
                        break
                tail = buf[-overlap:]
    except OSError:
        return None
    _RUNTIME_SCAN[key] = found
    return found


def foreign_code(pid: int) -> str | None:
    """A foreign runtime in what process `pid` runs: its executable, or a file it
    has mapped executable, other than the image's own system files."""
    try:
        exe = os.readlink(f"/proc/{pid}/exe")
    except OSError:
        exe = ""
    if not exe.startswith(SYSTEM_PREFIXES):
        why = foreign_runtime(f"/proc/{pid}/exe")
        if why:
            return why
    try:
        maps = Path(f"/proc/{pid}/maps").read_text()
    except OSError:
        return None
    seen: set[str] = set()
    for line in maps.splitlines():
        parts = line.split(None, 5)
        if len(parts) < 6 or "x" not in parts[1]:
            continue
        path = parts[5].strip()
        if not path.startswith("/") or path.startswith(SYSTEM_PREFIXES) or path in seen:
            continue
        seen.add(path)
        why = foreign_runtime(path)
        if why:
            return why
    return None


def candidate_processes(pgid: int | None = None) -> list[tuple[int, str]]:
    """(pid, comm) of every live process the candidate account runs, plus every
    process in `pgid` -- a child that left the group with setsid() is still the
    candidate's."""
    ids = candidate_uid()
    uid = str(ids[0]) if ids else None
    out = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            fields = (entry / "stat").read_text().rsplit(")", 1)[1].split()
            if fields[0] in ("Z", "X"):
                continue
            in_group = pgid is not None and int(fields[2]) == pgid
            owner = None
            for ln in (entry / "status").read_text().splitlines():
                if ln.startswith("Uid:"):
                    owner = ln.split()[1]
                    break
            if not in_group and (uid is None or owner != uid):
                continue
            comm = (entry / "comm").read_text().strip()
        except (OSError, IndexError, ValueError):
            continue
        out.append((int(entry.name), comm))
    return out


def _kill_group(proc, grace: float = 10.0) -> None:
    """Kill the whole process group, so a server's children go with it."""
    if proc is None or proc.poll() is not None:
        return
    for sig, wait in ((signal.SIGTERM, grace), (signal.SIGKILL, 5.0)):
        try:
            os.killpg(os.getpgid(proc.pid), sig)
        except (ProcessLookupError, PermissionError):
            try:
                proc.send_signal(sig)
            except ProcessLookupError:
                return
        try:
            proc.wait(timeout=wait)
            return
        except subprocess.TimeoutExpired:
            continue


def reap_candidate(rounds: int = 50) -> int:
    """Kill every process the candidate account runs, until none is left.

    `_kill_group` reaches one server's process group; a child that called
    setsid(), or anything `make` left behind, is outside it.  So the sweep is
    by account, not by group: a child that has become the candidate account
    sends SIGKILL to every process that account may signal (kill(-1) -- one
    pass the kernel makes with forking held off, so a process cannot outrun it
    by forking), and that repeats until /proc shows none.  Returns how many are
    still running afterwards (0 unless something is badly wrong)."""
    ids = candidate_uid()
    if ids is None:
        return 0
    uid, gid = ids
    for _ in range(rounds):
        if not candidate_processes():
            return 0
        pid = os.fork()
        if pid == 0:                                   # pragma: no cover
            try:
                os.setgroups([gid])
                os.setgid(gid)
                os.setuid(uid)
                os.kill(-1, signal.SIGKILL)
            finally:
                os._exit(0)
        os.waitpid(pid, 0)
        time.sleep(0.1)
    return len(candidate_processes())


# --------------------------------------------------------------------------- #
@dataclass
class Candidate:
    """The artifact under test, started through Dendrite's monolith interface."""

    workdir: Path                 # a pristine copy of /app, already built
    rundir: Path                  # per-scenario state (database, media, config)
    server_name: str
    http_port: int
    https_port: int
    extra_config: dict | None = None
    proc: subprocess.Popen | None = None
    log: Path | None = None
    logdir: Path | None = None    # root's own: the server's stdout and stderr

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.http_port}"

    @property
    def federation_url(self) -> str:
        return f"https://127.0.0.1:{self.https_port}"

    def config(self) -> dict:
        cfg = {
            "version": 2,
            "global": {
                "server_name": self.server_name,
                "private_key": str(self.rundir / "matrix_key.pem"),
                "key_validity_period": "168h0m0s",
                "database": {
                    "connection_string": f"file:{self.rundir / 'dendrite.db'}",
                    "max_open_conns": 10,
                    "max_idle_conns": 2,
                    "conn_max_lifetime": -1,
                },
                "disable_federation": False,
                "presence": {"enable_inbound": True, "enable_outbound": True},
                "well_known_server_name": "",
                "well_known_client_name": "",
            },
            "client_api": {
                "registration_disabled": False,
                "guests_disabled": True,
                "registration_shared_secret": "",
                "enable_registration_captcha": False,
                "rate_limiting": {"enabled": False, "threshold": 1000,
                                  "cooloff_ms": 0},
            },
            "federation_api": {
                "send_max_retries": 2,
                "disable_tls_validation": True,
                "prefer_direct_fetch": True,
                "key_perspectives": [],
            },
            "media_api": {
                "base_path": str(self.rundir / "media_store"),
                "max_file_size_bytes": 10485760,
                "dynamic_thumbnails": True,
                "thumbnail_sizes": [
                    {"width": 32, "height": 32, "method": "crop"},
                    {"width": 96, "height": 96, "method": "crop"},
                    {"width": 640, "height": 480, "method": "scale"},
                ],
            },
            "sync_api": {"search": {"enabled": False}},
            "user_api": {"bcrypt_cost": 4, "auto_join_rooms": []},
            "app_service_api": {"config_files": []},
            "mscs": {"mscs": []},
            "logging": [{"type": "std", "level": "info"}],
        }
        # dendrite-sample.yaml configures SQLite per component as well as
        # globally; both carry the same URI here.
        db = {"connection_string": f"file:{self.rundir / 'dendrite.db'}",
              "max_open_conns": 10, "max_idle_conns": 2, "conn_max_lifetime": -1}
        for comp in ("app_service_api", "federation_api", "key_server",
                     "media_api", "mscs", "relay_api", "room_server", "sync_api",
                     "user_api"):
            cfg.setdefault(comp, {})
            cfg[comp]["database"] = dict(db)
        if self.extra_config:
            _deep_update(cfg, self.extra_config)
        return cfg

    def _logdir(self) -> Path:
        return self.logdir or self.rundir.with_name(self.rundir.name + ".log")

    def prepare(self) -> None:
        # A new directory every time, made here by root and written before the
        # candidate account owns it.  Preparing one that account has already
        # owned would have root writing through whatever it left there -- a
        # symlink where the key or the config goes -- so a run directory is
        # never reused, and this refuses one that already exists.
        self.rundir.mkdir(parents=True)
        # The server runs as the candidate account, so its run directory --
        # database, media store, keys -- has to belong to that account.  Its
        # log does not live there: see start().
        self._logdir().mkdir(mode=0o700, parents=True)
        (self.rundir / "matrix_key.pem").write_text(matrix_key_pem())
        (self.rundir / "media_store").mkdir(exist_ok=True)
        make_tls(self.rundir, self.server_name)
        import yaml
        (self.rundir / "dendrite.yaml").write_text(
            yaml.safe_dump(self.config(), sort_keys=False)
        )
        give_to_candidate(self.rundir)

    def start(self, timeout: float = 45.0) -> bool:
        binary = self.workdir / "bin" / "dendrite"
        if not binary.exists():
            return False
        # Root opens the server's log and hands the descriptor to candidate
        # code as its stdout and stderr, so the path must be one no candidate
        # process can have put a symlink at: a file in root's own 0700
        # directory beside the run directory, never inside it, opened through
        # that directory's descriptor and without following a link.
        logdir = self._logdir()
        dfd = os.open(logdir, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
                      | os.O_CLOEXEC)
        try:
            st = os.fstat(dfd)
            if st.st_uid != os.geteuid() or st.st_mode & 0o077:
                raise RuntimeError(f"{logdir} is not the verifier's own directory")
            fd = os.open("dendrite.log", os.O_WRONLY | os.O_CREAT | os.O_APPEND
                         | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=dfd)
        finally:
            os.close(dfd)
        self.log = logdir / "dendrite.log"
        env = candidate_env({"HOME": str(self.rundir)})
        with os.fdopen(fd, "ab") as lf:
            self.proc = subprocess.Popen(
                [str(binary),
                 "--config", str(self.rundir / "dendrite.yaml"),
                 "--http-bind-address", f"127.0.0.1:{self.http_port}",
                 "--https-bind-address", f"127.0.0.1:{self.https_port}",
                 "--tls-cert", str(self.rundir / "server.crt"),
                 "--tls-key", str(self.rundir / "server.key")],
                cwd=str(self.workdir), stdin=subprocess.DEVNULL,
                stdout=lf, stderr=lf, env=env,
                preexec_fn=_ceilings(drop=True),
            )
        return wait_http(self.base_url + "/_matrix/client/versions", timeout, self.proc)

    def foreign_child(self) -> str | None:
        """The name of a foreign program running in this server's session.

        The provenance check looks at the process tree once, at startup. A
        server that behaved itself for that one moment and started an existing
        homeserver on its first real request would pass it, so every scenario
        looks again before it stops the server it has been talking to. The
        candidate is started with `setsid`, so its descendants share its
        process group id whatever they do with their parentage.
        """
        if self.proc is None or self.proc.poll() is not None:
            return None
        try:
            pgid = os.getpgid(self.proc.pid)
        except OSError:
            return None
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit() or int(entry.name) == self.proc.pid:
                continue
            try:
                fields = (entry / "stat").read_text().rsplit(")", 1)[1].split()
                if int(fields[2]) != pgid:          # pgid is the 5th field
                    continue
                comm = (entry / "comm").read_text().strip()
            except (OSError, IndexError, ValueError):
                continue
            base = comm.lower()
            for bad in FOREIGN_PROGRAMS:
                if base == bad or base.startswith(bad + "."):
                    return comm
        # What is running, not just what it is called: every process of the
        # candidate account and of this group, the server's own pid included.
        for pid, comm in candidate_processes(pgid):
            why = foreign_code(pid)
            if why:
                return f"{comm} ({why})"
        return None

    def stop(self, grace: float = 10.0) -> None:
        # The whole process group goes, so a server that forked workers does
        # not leave them holding its port.
        _kill_group(self.proc, grace)
        self.proc = None

    def restart(self, timeout: float = 45.0) -> bool:
        self.stop()
        return self.start(timeout)

    def tail(self, n: int = 40) -> str:
        # The last 64 KiB at most: the candidate writes this file.
        f = open_regular(self.log, follow=False) if self.log else None
        if f is None:
            return ""
        with f:
            f.seek(max(0, os.fstat(f.fileno()).st_size - 65536))
            data = f.read(65536)
        return "\n".join(data.decode(errors="replace").splitlines()[-n:])


def _deep_update(dst: dict, src: dict) -> None:
    for k, v in src.items():
        if isinstance(v, dict) and isinstance(dst.get(k), dict):
            _deep_update(dst[k], v)
        else:
            dst[k] = v


# --------------------------------------------------------------------------- #
@dataclass
class Synapse:
    """Pinned Synapse: the recording oracle, and the live federation partner."""

    python: str
    rundir: Path
    server_name: str
    port: int
    federation: bool = True
    tls_port: int = 0
    proc: subprocess.Popen | None = None
    extra: dict | None = None

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    def prepare(self) -> None:
        self.rundir.mkdir(parents=True, exist_ok=True)
        import yaml
        listeners = [{
            "port": self.port, "tls": False, "type": "http",
            "bind_addresses": ["127.0.0.1"],
            "resources": [{"names": ["client", "federation", "openid"],
                           "compress": False}],
        }]
        cfg = {
            "server_name": self.server_name,
            "pid_file": str(self.rundir / "hs.pid"),
            "public_baseurl": f"http://127.0.0.1:{self.port}/",
            "serve_server_wellknown": True,
            "listeners": listeners,
            "database": {"name": "sqlite3",
                         "args": {"database": str(self.rundir / "homeserver.db")}},
            "media_store_path": str(self.rundir / "media_store"),
            "signing_key_path": str(self.rundir / "signing.key"),
            "trusted_key_servers": [],
            "report_stats": False,
            "enable_registration": True,
            "enable_registration_without_verification": True,
            "registration_shared_secret": "mxconf_shared_secret",
            "allow_guest_access": False,
            "presence": {"enabled": True},
            "enable_media_repo": True,
            "max_upload_size": "10M",
            "dynamic_thumbnails": True,
            "url_preview_enabled": False,
            "federation_domain_whitelist": None,
            "suppress_key_server_warning": True,
            # Newer Synapse denies publication to the room directory by default,
            # which would hide a behaviour the specification defines.  Both the
            # oracle and the candidate publish, so the comparison is about the
            # specification rather than about one deployment's policy.
            "room_list_publication_rules": [{"action": "allow"}],
            # Newer Synapse defaults to MSC3916 authenticated media, which makes
            # the /_matrix/media/v3 endpoints stop serving newly uploaded
            # content.  Those endpoints are what the v1.1 release defines, so
            # the oracle serves them here; the release that moves media behind
            # authentication is a later step's subject.
            "enable_authenticated_media": False,
            # The reference refuses to federate with an IP-literal server name
            # unless the loopback range is out of its blocklist; the allowlist
            # alone does not reach the agent that rejects IP literals.  Both
            # homeservers here are 127.0.0.1:<port>.
            "ip_range_blacklist": [],
            "ip_range_whitelist": ["127.0.0.0/8", "::1/128"],
            "federation_ip_range_blacklist": [],
            "log_config": str(self.rundir / "log.config"),
        }
        for name in ("rc_message", "rc_registration", "rc_3pid_validation"):
            cfg[name] = {"per_second": 10000, "burst_count": 100000}
        cfg["rc_login"] = {k: {"per_second": 10000, "burst_count": 100000}
                           for k in ("address", "account", "failed_attempts")}
        cfg["rc_joins"] = {k: {"per_second": 10000, "burst_count": 100000}
                           for k in ("local", "remote")}
        cfg["rc_invites"] = {k: {"per_second": 10000, "burst_count": 100000}
                             for k in ("per_room", "per_user", "per_issuer")}
        cfg["rc_federation"] = {"window_size": 1000, "sleep_limit": 100000,
                                "sleep_delay": 0, "reject_limit": 100000,
                                "concurrent": 64}
        if self.tls_port:
            # The server name carries this port, so other servers reach the
            # federation API at https://<server_name>/_matrix/federation/...
            crt, key = make_tls(self.rundir, self.server_name)
            cfg["tls_certificate_path"] = str(crt)
            cfg["tls_private_key_path"] = str(key)
            cfg["federation_verify_certificates"] = False
            listeners.append({
                "port": self.tls_port, "tls": True, "type": "http",
                "bind_addresses": ["127.0.0.1"],
                "resources": [{"names": ["federation"], "compress": False}],
            })
        if self.extra:
            _deep_update(cfg, self.extra)
        (self.rundir / "homeserver.yaml").write_text(yaml.safe_dump(cfg, sort_keys=False))
        (self.rundir / "log.config").write_text(_LOGCONF.format(
            path=str(self.rundir / "homeserver.log")))
        if not (self.rundir / "signing.key").exists():
            subprocess.run(
                [self.python, "-I", "-m", "synapse.app.homeserver",
                 "--server-name", self.server_name,
                 "--config-path", str(self.rundir / "homeserver.yaml"),
                 "--generate-keys"],
                check=True, capture_output=True, cwd=str(self.rundir),
            )

    def start(self, timeout: float = 90.0) -> bool:
        out = open(self.rundir / "synapse.out", "ab")
        self.proc = subprocess.Popen(
            [self.python, "-I", "-m", "synapse.app.homeserver",
             "--config-path", str(self.rundir / "homeserver.yaml")],
            stdout=out, stderr=out, cwd=str(self.rundir),
            preexec_fn=_ceilings(6144),
        )
        return wait_http(self.base_url + "/_matrix/client/versions", timeout, self.proc)

    def restart(self, timeout: float = 120.0) -> bool:
        self.stop()
        return self.start(timeout)

    def stop(self) -> None:
        _kill_group(self.proc, 30.0)
        self.proc = None


_LOGCONF = """version: 1
formatters:
  precise:
    format: '%(asctime)s - %(name)s - %(lineno)d - %(levelname)s - %(message)s'
handlers:
  file:
    class: logging.FileHandler
    formatter: precise
    filename: {path}
loggers:
  synapse:
    level: WARNING
root:
  level: WARNING
  handlers: [file]
disable_existing_loggers: false
"""
