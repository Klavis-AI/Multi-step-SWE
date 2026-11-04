"""Bring the candidate's deployment up inside the verifier container.

The verifier is a separate image. Nothing is inherited from the agent: the
candidate's work arrives as a set of artifacts that Harbor re-materialises at
their original absolute paths, owned by root:root, with no process running.
Everything that has to be running to observe the deployment -- Apache, PHP,
MySQL, PostgreSQL -- is started here, by the scorer, from the candidate's own
files.

Four decisions shape this module.

*Ownership is repaired, not trusted.* Harbor's artifact round trip strips uid
and gid in both directions (`filter="data"` on the way out, uid/gid zeroed on
the way in), so every restored tree is root-owned whatever it was in the agent
container. PostgreSQL refuses to start on a data directory it does not own and
mysqld cannot write to one, so the first thing that happens to each restored
tree is a chown to the account that will use it. This is repair, not
permission: it restores the ownership the agent container had.

*Locations are the scorer's, contents are the candidate's.* The data and
configuration directories travel, so the server configuration is the
candidate's deliverable and is read as written -- `sql_mode`, the server
character set, the collation, `shared_buffers`. What does not travel is where
those directories live and which port they answer on, and those are supplied
on the command line, where they beat any file.

Three other groups of settings are supplied there, and none of them is a
requirement on the candidate. The scorer's own authentication lines, so it can
always read the cluster it is grading. Statement and connection logging, so
what the application actually sent is observable rather than inferred. And the
settings that name a program to run or a library to load -- pinned to their
defaults, because those travel inside the candidate's configuration directory
and would otherwise decide what the postmaster executes.

*The superuser path is guaranteed; the application's path is not.* A scorer
that cannot read the database cannot score anything, but a scorer that hands
the application a free pass has stopped measuring it. So PostgreSQL is started
with an `hba_file` that grants `postgres` trust over the socket and then
`include_if_exists`es the candidate's own file: the scorer always gets in, and
the application still has to satisfy whatever authentication the candidate
configured. MySQL has no include directive, so the scorer connects as `root`
over the socket and falls back to the application's credentials.

*Nothing the candidate wrote executes as root.* Apache's children are www-data,
mysqld is mysql, postgres is postgres -- and the two probes that run candidate
code directly (the CLI front controller, and the PHP parser over the tree) are
dropped to an unprivileged account here rather than inheriting the scorer's.
/logs/verifier stays root:root 0700 for the whole run.

Failure is reported, never raised. `bring_up()` returns a `Deployment` whose
services each carry a status and a log tail; whether a missing MySQL or a dead
PostgreSQL is a defect is the step's question, not this module's.
"""

import fcntl
import glob
import grp
import os
import pwd
import re
import shlex
import shutil
import socket
import struct
import subprocess
import termios
import threading
import time

from . import ready

# ------------------------------------------------------------- locations ---
# The deployment contract, stated once in step 1's instruction and never
# extended. Everything here is a location, a port or an account.

APP_ROOT = "/app"
APP_USER = "www-data"

MYSQL_DATADIR = "/var/lib/mysql"
MYSQL_CONFIG = "/etc/mysql/my.cnf"
MYSQL_RUNDIR = "/var/run/mysqld"
MYSQL_SOCKET = "/var/run/mysqld/mysqld.sock"
MYSQL_GENERAL_LOG = "/var/run/mysqld/general.log"
MYSQL_PLUGIN_DIR = "/opt/mysql57/plugin"
MYSQL_PORT = 3306
MYSQL_USER = "mysql"

PG_ROOT = "/var/lib/postgresql"
PG_ETC = "/etc/postgresql"
PG_RUNDIR = "/var/run/postgresql"
PG_PORT = 5432
PG_USER = "postgres"

# The role the scorer applies a *candidate's* SQL as. Never `postgres`: a file
# run by a superuser can `COPY ... FROM PROGRAM`, `lo_import` a file off the
# disk, or `SET session_replication_role` to switch the foreign keys it is
# being checked against off, and the scorer would be paying for a tree that
# never did the work. `installgate.ensure_install_role` creates it and holds
# it to NOSUPERUSER on every run, so a candidate who made a role of this name
# themselves does not inherit whatever they gave it.
PG_INSTALL_USER = "wg_install"

HTTP_HOST = "localhost"
HTTP_BASE = "http://127.0.0.1"

# The application's own shipped deployment, created by the image's init-db.sh.
# Keeping these is the default -- the candidate has to go out of their way to
# change them -- but the scorer discovers rather than assumes (see
# `app_dsn()` and `pick_database()`), so a rename costs points only where a
# step's own gate says it should.
APP_DB = "webgoatphp"
APP_DB_USER = "webgoat"
APP_DB_PASSWORD = "webgoat"
SYSTEM_DATABASES = frozenset(["template0", "template1", "postgres",
                              "information_schema", "performance_schema",
                              "mysql", "sys"])

WORKDIR = "/tmp/deploy"
NOBODY = "nobody"

# Where the postmaster's stderr lands. `pglog` reads it.
PG_LOGFILE = os.path.join(WORKDIR, "postgres.log")

# Every statement the server executes, tagged with who executed it and against
# which database, written to the file `-l` names. Forced on the command line
# because that is the one place a setting beats both postgresql.conf and
# postgresql.auto.conf; a `SET`-level override attached to a role or a database
# still wins over it, which is why `pglog.enforce` clears those before the
# corpus runs. `logging_collector=off` and `log_destination=stderr` together
# are what keep the output in the scorer's own file instead of in a directory
# the candidate chose.
#
# The `@` is a sentinel, not decoration. A logged statement can be many lines
# long, and the only thing that distinguishes "a new record starts here" from
# "this is line nine of the CREATE TABLE" is the prefix. `%m|%p|%u|%d|@` gives
# a shape no SQL line can accidentally have.
PG_LOG_LINE_PREFIX = "%m|%p|%u|%d|@"
PG_FORCED_LOGGING = [
    "logging_collector=off",
    "log_destination=stderr",
    "log_statement=all",
    "log_connections=on",
    "log_line_prefix=" + PG_LOG_LINE_PREFIX,
    "log_error_verbosity=default",
    "log_min_messages=warning",
]

# Settings that run a program, load a library, or decide where libraries come
# from. These travel inside the candidate's own configuration directory, so
# without pinning them the candidate's postgresql.conf chooses what the
# postmaster executes. Pinned to their defaults -- nothing in this task is
# graded on archiving, on preloaded libraries, or on a TLS passphrase.
PG_FORCED_SAFETY = [
    "shared_preload_libraries=",
    "session_preload_libraries=",
    "local_preload_libraries=",
    "dynamic_library_path=$libdir",
    "archive_mode=off",
    "archive_command=",
    "restore_command=",
    "archive_cleanup_command=",
    "recovery_end_command=",
    "ssl_passphrase_command=",
]

# A resource floor, not a behaviour switch. Nothing in this task is graded on
# how many clients the cluster admits, but the scorer needs a few at once --
# the corpus replay's connection, the install account's, and whatever the
# candidate's own application pools -- and a candidate who sized a cluster for
# one application can leave a correct deployment unscoreable with
# `max_connections = 4`: the postmaster starts, and then the family that
# happens to connect last reads "sorry, too many clients already" and fails a
# whole denominator for a reason that has nothing to do with the migration.
# This is the same kind of forcing as the port and the socket directory, and
# it is a floor rather than a value: a candidate who raised the number keeps
# their number.
PG_MIN_MAX_CONNECTIONS = 50


def _pg_max_connections(datadir, conf):
    """The candidate's own `max_connections`, asked of the server's parser.

    `postgres -C` reads the configuration the way the postmaster will --
    through `include` directives and `postgresql.auto.conf`, which the regex
    in `_pg_setting` cannot follow -- and prints the value without starting
    anything. A configuration too broken to answer returns None, and the
    caller then forces the bare floor; that is the conservative direction,
    since a cluster whose configuration will not parse is not going to start
    with the candidate's number either.
    """
    pg = _pg_bin("postgres")
    argv = [pg, "-D", datadir]
    if conf:
        argv += ["-c", "config_file=%s" % conf]
    argv += ["-C", "max_connections"]
    p = run(as_user(PG_USER, argv, env={"HOME": PG_ROOT}), timeout=60)
    if p.returncode != 0:
        return None
    try:
        return int((p.stdout or b"").decode("utf-8", "replace").strip())
    except ValueError:
        return None


def _pg_forced_resources(datadir, conf):
    """`max_connections` at the floor or the candidate's own, whichever is more."""
    theirs = _pg_max_connections(datadir, conf)
    want = PG_MIN_MAX_CONNECTIONS
    if theirs is not None and theirs > want:
        want = theirs
    return ["max_connections=%d" % want]


def _optstr(settings):
    """`-c name=value ...` as one string, safe for the shell pg_ctl runs it in.

    `pg_ctl -o` is not an argv: pg_ctl pastes the string into a command line
    and hands the whole thing to a shell. An unquoted `|` in a log_line_prefix
    is therefore a pipe, and the postmaster ends up with a truncated prefix and
    its stderr going somewhere nobody reads. Every value is quoted here, once.
    """
    out = []
    for kv in settings:
        out.append("-c")
        out.append(shlex.quote(kv))
    return " ".join(out)


class Service(object):
    """What happened to one service, for the step report."""

    def __init__(self, name):
        self.name = name
        self.present = False        # its files are in the restored tree
        self.running = False        # it accepted a connection
        self.detail = ""            # one line, why
        self.log = ""               # tail of its own log, when it has one
        self.forced = []            # settings the scorer supplied on the CLI
        self.cleared = []           # role/database overrides of them, reset

    def as_dict(self):
        return {"present": self.present, "running": self.running,
                "detail": self.detail, "forced": self.forced,
                "cleared": self.cleared, "log": self.log[-4000:]}

    def __repr__(self):
        return "<%s present=%s running=%s %s>" % (
            self.name, self.present, self.running, self.detail)


# ----------------------------------------------------------------- shell ---

def run(cmd, timeout=300, **kw):
    """Run a command, capture everything, never raise."""
    try:
        return subprocess.run(cmd, capture_output=True, timeout=timeout, **kw)
    except subprocess.TimeoutExpired as e:
        return subprocess.CompletedProcess(cmd, 124, e.stdout or b"",
                                           (e.stderr or b"") + b"\ntimeout")
    except OSError as e:
        return subprocess.CompletedProcess(cmd, 127, b"", str(e).encode())


def diagnostics(p, limit=2000):
    """What a failed subprocess said, wherever it said it.

    php's CLI writes `Could not open input file: <path>` to *stdout* and exits
    1, so a caller that reports only stderr reports an empty reason for the one
    failure mode a privilege drop introduces -- the program itself unreadable
    by the account it was handed to. Prefer stderr, fall back to stdout, and
    mark which one is being quoted.
    """
    err = (p.stderr or b"").decode("utf-8", "replace").strip()
    if err:
        return err[-limit:]
    out = (p.stdout or b"").decode("utf-8", "replace").strip()
    if out:
        return "(stdout) " + out[-limit:]
    return "no output"


def _ids(name):
    """(uid, gid) of an account, or None if the image has no such account."""
    try:
        p = pwd.getpwnam(name)
    except KeyError:
        return None
    return p.pw_uid, p.pw_gid


def as_user(name, cmd, env=None):
    """Prefix a command so it runs as `name` with no supplementary groups.

    Used for everything that touches candidate code: the front-controller
    probe, the PHP parser, the database servers. setpriv rather than su so
    there is no shell between here and the program, and no login environment
    to inherit.
    """
    ids = _ids(name)
    if ids is None:
        return list(cmd)
    uid, gid = ids
    # --no-new-privs: a set-id file anywhere in the restored trees cannot
    # raise the dropped process back to root (set-id sweep, 2026-10-13).
    pre = ["setpriv", "--reuid", str(uid), "--regid", str(gid),
           "--clear-groups", "--no-new-privs",
           "env", "HOME=/tmp", "TMPDIR=/tmp"]
    for k, v in sorted((env or {}).items()):
        pre.append("%s=%s" % (k, v))
    return pre + list(cmd)


# How long a recursive chown of one candidate-controlled tree may take.
#
# `chown -R` walks whatever the candidate left behind, and the step's budget
# has to hold whatever that walk costs. At the old 600 s, four call sites
# added 2,400 s of worst case to a deployment the rest of the step budgets in
# minutes -- a tree with a few million entries under it would not have failed
# anything, it would have quietly eaten the verifier's clock and taken the
# families down with it on a timeout nobody could attribute. 120 s is far more
# than the real trees need (the application is ~1,800 files, the cluster
# ~1,300) and short enough that four of them cannot dominate. A tree that
# exceeds it is reported as what it is, and the service that needed it does
# not start.
CHOWN_TIMEOUT = 120


def chown_tree(path, user, group=None, mode=None, seconds=CHOWN_TIMEOUT):
    """chown -R, tolerantly and under a clock.

    Repairs what the artifact round trip stripped. False means there is a tree
    here and it could not be given away -- the walk did not finish inside
    `seconds`, or chown refused. Callers that need the ownership fail closed
    on that rather than starting a service that will half-work. A path that
    does not exist is nothing to repair and is not a failure: /etc/postgresql
    is absent on a cluster whose candidate keeps their configuration in the
    data directory, which is legal, and the callers that need a tree to be
    *there* check that themselves before they get here.
    """
    if not os.path.exists(path):
        return True
    owner = user if group is None else "%s:%s" % (user, group)
    p = run(["chown", "-R", owner, path], timeout=seconds)
    if p.returncode != 0:
        return False
    if mode is not None:
        run(["chmod", mode, path], timeout=seconds)
    return True


def _mkrundir(path, user, group, mode=0o755):
    ids = _ids(user)
    try:
        os.makedirs(path, exist_ok=True)
        os.chmod(path, mode)
        if ids:
            gid = grp.getgrnam(group).gr_gid if group else ids[1]
            os.chown(path, ids[0], gid)
    except OSError:
        pass


def _tail(path, limit=8000):
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError:
        return ""
    return data[-limit:].decode("utf-8", "replace")


# --------------------------------------------------------------- probes ---

def mysql_listening(sock=MYSQL_SOCKET, timeout=2.0):
    """True when mysqld answers on the socket, whatever it thinks of us.

    The handshake is read rather than a client run, because a candidate who
    sets a root password would turn "the server is up" into "access denied".
    Byte 4 of the first packet is the protocol version on a greeting and 0xff
    on an error packet; either proves a live server.
    """
    if not os.path.exists(sock):
        return False
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect(sock)
        head = s.recv(5)
    except OSError:
        return False
    finally:
        s.close()
    return len(head) >= 5 and head[4] in (10, 0xff)


def postgres_listening(port=PG_PORT, sockdir=PG_RUNDIR, timeout=5.0):
    """True when the postmaster accepts or actively rejects a connection."""
    p = run(["pg_isready", "-h", sockdir, "-p", str(port), "-t", "3"],
            timeout=timeout)
    # 0 accepting, 1 rejecting (starting up, but alive), 2 no response.
    return p.returncode in (0, 1)


# ---------------------------------------------------------------- mysql ---

def _mysql_present(datadir=MYSQL_DATADIR):
    if not os.path.isdir(datadir):
        return False
    # An initialised 5.7 datadir has the system tablespace and the mysql
    # schema; an empty directory left behind by a removal has neither.
    return (os.path.exists(os.path.join(datadir, "ibdata1"))
            and os.path.isdir(os.path.join(datadir, "mysql")))


# Options in my.cnf that name a place mysqld must be able to reach before it
# will serve. It aborts on a missing one rather than creating it, so a
# configuration that is fine in the agent's container is fatal in a container
# that lacks the directory -- which is how a correct deployment scored 0 on
# oracle-v1 (NOTES-authoring.md §19.8). `dir` entries are the directory
# itself; `file` entries are a file whose parent must exist.
MYSQL_PATH_OPTIONS = (
    ("secure_file_priv", "dir"),
    ("log_error", "file"),
    ("tmpdir", "dir"),
    ("slow_query_log_file", "file"),
    ("general_log_file", "file"),
)

# `secure_file_priv` takes two non-path values with a defined meaning: NULL
# disables the feature and the empty string removes the restriction. Neither
# is a directory, and creating one called "NULL" would change what the server
# does.
_NOT_A_PATH = {"", "null"}


def mysql_config_dirs(config=MYSQL_CONFIG):
    """[directory] that the config file requires mysqld to find, in order.

    Parsed from the file rather than from `mysqld --help`, because the point
    is to create them *before* the server runs. Only the [mysqld] and
    [server] sections count; a path under [client] is not the server's.
    """
    wanted = dict(MYSQL_PATH_OPTIONS)
    out = []
    try:
        fh = open(config, "r", encoding="utf-8", errors="replace")
    except OSError:
        return out
    section = ""
    with fh:
        for line in fh:
            bare = line.split("#", 1)[0].strip()
            if not bare:
                continue
            if bare.startswith("["):
                section = bare.strip("[]").strip().lower()
                continue
            if section not in ("mysqld", "server"):
                continue
            if "=" not in bare:
                continue
            key, value = bare.split("=", 1)
            key = key.strip().lower().replace("-", "_")
            if key not in wanted:
                continue
            value = value.strip().strip("'\"")
            if value.lower() in _NOT_A_PATH:
                continue
            place = value if wanted[key] == "dir" else os.path.dirname(value)
            if place and place not in out:
                out.append(place)
    return out


def start_mysql(datadir=MYSQL_DATADIR, config=MYSQL_CONFIG,
                socket_path=MYSQL_SOCKET, port=MYSQL_PORT, seconds=120):
    svc = Service("mysql")
    if not _mysql_present(datadir):
        svc.detail = "no initialised datadir at %s" % datadir
        return svc
    svc.present = True

    if mysql_listening(socket_path):
        svc.running = True
        svc.detail = "already listening"
        return svc

    if not chown_tree(datadir, MYSQL_USER, MYSQL_USER):
        svc.detail = ("could not give %s to %s within %ds"
                      % (datadir, MYSQL_USER, CHOWN_TIMEOUT))
        return svc
    _mkrundir(MYSQL_RUNDIR, MYSQL_USER, MYSQL_USER)
    for place in mysql_config_dirs(config):
        # Only what is MISSING. An existing directory is left exactly as it
        # is: `tmpdir = /tmp` is a legal setting, and chmod 750 on /tmp would
        # break the container to fix nothing.
        if not os.path.isdir(place):
            _mkrundir(place, MYSQL_USER, MYSQL_USER, mode=0o750)
    os.makedirs(WORKDIR, exist_ok=True)

    cmd = []
    if os.path.isfile(config):
        # mysqld requires --defaults-file first, and silently ignores a config
        # file that is group- or world-writable -- which an artifact restore
        # can easily leave behind.
        try:
            os.chmod(config, 0o644)
        except OSError:
            pass
        cmd.append("--defaults-file=" + config)
        svc.detail = "config %s" % config
    else:
        cmd.append("--no-defaults")
        svc.detail = "no %s; compiled defaults" % config
    # Locations and ports only, and last, where they beat the file.
    #
    # The pid file goes in MYSQL_RUNDIR and not in WORKDIR, because mysqld
    # creates it *itself*, after dropping to --user=mysql. WORKDIR is made by
    # root under the default umask, so a pid file there is
    # "Can't create/write to file ... (Errcode: 13 - Permission denied)" and
    # the server never starts (measured, oracle-v2, NOTES §19.8). The log
    # files in WORKDIR are fine: root opens them and the child inherits the
    # descriptor.
    #
    # The general query log is turned on for the same reason, and is a location
    # too. It is the only reading any gate has of what the application actually
    # sent: a statement the tree assembles while it serves a request is in no
    # file, and is in here. It lands in MYSQL_RUNDIR because mysqld opens it
    # itself after dropping to --user=mysql, exactly like the pid file, and a
    # log left behind by an earlier start is removed first so that what a gate
    # reads belongs to this run. `--log-output=FILE` because the default on a
    # candidate's my.cnf could be TABLE, which writes to mysql.general_log and
    # leaves nothing on disk.
    try:
        os.unlink(MYSQL_GENERAL_LOG)
    except OSError:
        pass
    # No server plugins from the candidate's my.cnf: the plugin directory is
    # an empty root-owned one, and `--plugin-load=` given last resets any
    # plugin-load / plugin-load-add the file named (root-tool-config sweep,
    # 2026-10-13). The application needs no plugin.
    os.makedirs(MYSQL_PLUGIN_DIR, mode=0o755, exist_ok=True)
    forced = ["--user=" + MYSQL_USER, "--datadir=" + datadir,
              "--socket=" + socket_path, "--port=%d" % port,
              "--bind-address=127.0.0.1",
              "--pid-file=%s/mysqld.pid" % MYSQL_RUNDIR,
              "--general-log=1",
              "--general-log-file=" + MYSQL_GENERAL_LOG,
              "--log-output=FILE",
              "--plugin-dir=" + MYSQL_PLUGIN_DIR,
              "--plugin-load="]
    svc.forced = forced
    logpath = os.path.join(WORKDIR, "mysqld.log")

    with open(logpath, "ab") as log:
        try:
            # Started as the mysql account from the first instruction, like
            # postgres: the candidate's my.cnf is never read with root's
            # privileges, so no option in it can make root open, create or
            # load anything (root-tool-config sweep, completed 2026-10-14 with
            # the user's approval). --user stays in `forced` as a harmless
            # no-op for the case where as_user cannot resolve the account.
            subprocess.Popen(as_user(MYSQL_USER, ["mysqld"] + cmd + forced),
                             stdout=log, stderr=subprocess.STDOUT,
                             start_new_session=True)
        except OSError as e:
            svc.detail = "mysqld would not exec: %s" % e
            return svc

    deadline = time.time() + seconds
    while time.time() < deadline:
        if mysql_listening(socket_path):
            svc.running = True
            svc.detail = "listening on %s (%s)" % (socket_path, svc.detail)
            break
        time.sleep(0.5)
    else:
        svc.detail = "did not listen within %ds (%s)" % (seconds, svc.detail)
    svc.log = _tail(logpath)
    return svc


def stop_mysql(socket_path=MYSQL_SOCKET):
    # --no-defaults: root must not read option files from the restored
    # /etc/mysql. If shutdown is refused without them, the TERM below stops it.
    run(["mysqladmin", "--no-defaults", "--socket=" + socket_path, "-u", "root",
         "shutdown"], timeout=60)
    run(["pkill", "-TERM", "-x", "mysqld"], timeout=30)
    deadline = time.time() + 60
    while time.time() < deadline and mysql_listening(socket_path):
        time.sleep(0.5)


# ------------------------------------------------------------- postgres ---

def find_cluster(root=PG_ROOT):
    """The candidate's data directory, whichever layout they used.

    `pg_createcluster` puts it at /var/lib/postgresql/<ver>/main with the
    configuration under /etc/postgresql; a bare `initdb -D` puts both in one
    directory. Both are found by looking for PG_VERSION, newest major version
    first so a leftover older cluster never wins.
    """
    seen = []
    for cand in glob.glob(os.path.join(root, "*", "*")) + glob.glob(
            os.path.join(root, "*")):
        if os.path.isfile(os.path.join(cand, "PG_VERSION")):
            seen.append(cand)

    def key(path):
        m = re.search(r"/(\d+)(?:/|$)", path)
        return (int(m.group(1)) if m else 0, path)

    seen.sort(key=key, reverse=True)
    return seen[0] if seen else None


def _pg_config_file(datadir):
    """The postgresql.conf the candidate's cluster is configured by."""
    ver = ""
    try:
        with open(os.path.join(datadir, "PG_VERSION")) as fh:
            ver = fh.read().strip()
    except OSError:
        pass
    name = os.path.basename(datadir.rstrip("/"))
    for cand in (os.path.join(PG_ETC, ver, name, "postgresql.conf"),
                 os.path.join(datadir, "postgresql.conf")):
        if ver and os.path.isfile(cand):
            return cand
        if os.path.isfile(cand):
            return cand
    return None


def _pg_setting(conf, name):
    """A scalar setting's value as written in a postgresql.conf, or None."""
    try:
        with open(conf, "r", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return None
    hit = None
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        m = re.match(r"^%s\s*=?\s*(.+)$" % re.escape(name), line)
        if m:
            hit = m.group(1).strip().strip("'\"")
    return hit


def _write_hba(datadir, conf):
    """An hba file that lets the scorer in and leaves the app's rules alone.

    PostgreSQL 16 gained include directives in pg_hba.conf, so the candidate's
    own file is pulled in below the two superuser lines rather than replaced.
    A candidate who tightens authentication still tightens it for the
    application; only the scorer's own socket connection is exempt. Two
    accounts are on it, not one: `postgres`, which the scorer reads the
    cluster with, and `PG_INSTALL_USER`, which is the unprivileged role a
    candidate's own SQL is applied as and which therefore has to be able to
    reach the server over the socket whatever the candidate's rules say.
    """
    os.makedirs(WORKDIR, exist_ok=True)
    theirs = _pg_setting(conf, "hba_file") if conf else None
    if not theirs:
        base = os.path.dirname(conf) if conf else datadir
        theirs = os.path.join(base, "pg_hba.conf")
    path = os.path.join(WORKDIR, "pg_hba.conf")
    with open(path, "w") as fh:
        fh.write("# scorer access, then the candidate's own rules\n")
        fh.write("local   all   postgres                 trust\n")
        fh.write("host    all   postgres   127.0.0.1/32  trust\n")
        fh.write("host    all   postgres   ::1/128       trust\n")
        fh.write("local   all   %-10s               trust\n" % PG_INSTALL_USER)
        fh.write("include_if_exists %s\n" % theirs)
    os.chmod(path, 0o644)
    return path, theirs


def _pg_bin(name):
    for cand in ("/usr/lib/postgresql/17/bin/" + name, name):
        if cand == name or os.path.exists(cand):
            return cand
    return name


def _pg_ctl():
    return _pg_bin("pg_ctl")


class _LogPump(object):
    """Carries the postmaster's output into the scorer's log, as root.

    `pglog` scores what this file says the application sent, so the file is a
    scorer input, and nothing the candidate controls may be able to rewrite
    it. Code the candidate controls can run as the `postgres` OS account: the
    delivered cluster is the candidate's, and anything in it that a superuser
    session executes -- a login event trigger, a function a view calls, a
    `search_path` set on the database -- can `COPY ... TO PROGRAM` or
    `COPY ... TO '<file>'`. When `pg_ctl -l` wrote the log, the file had to be
    the postgres account's, and one such statement could truncate it and
    write a clean stream in its place.

    So the postmaster writes into a pipe the scorer made, and this thread,
    in the scorer's root process, appends what arrives to a root-owned 0600
    file. A postgres-owned process can still add bytes to the pipe it
    inherited; it cannot truncate a pipe, seek in it, or open the file.
    `drain()` lets a reader wait until every byte written so far is on disk,
    which is what `pglog.mark()` needs to put a window boundary where the
    server's output actually was.
    """

    def __init__(self, path):
        self.rfd, self.wfd = os.pipe()
        self.out = open(path, "ab", buffering=0)
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._run, name="pglog-pump",
                                       daemon=True)
        self.thread.start()

    def _pending(self):
        buf = fcntl.ioctl(self.rfd, termios.FIONREAD, struct.pack("i", 0))
        return struct.unpack("i", buf)[0]

    def _run(self):
        import select
        while True:
            try:
                ready_, _w, _x = select.select([self.rfd], [], [], 0.5)
            except (OSError, ValueError):
                return
            if not ready_:
                continue
            with self.lock:
                try:
                    chunk = os.read(self.rfd, 1 << 16)
                except OSError:
                    chunk = b""
                if not chunk:
                    self.out.close()
                    os.close(self.rfd)
                    self.rfd = -1
                    return
                self.out.write(chunk)

    def drain(self, seconds=2.0):
        deadline = time.time() + seconds
        while time.time() < deadline:
            with self.lock:
                if self.rfd < 0:
                    return True
                try:
                    if self._pending() == 0:
                        return True
                except OSError:
                    return True
            time.sleep(0.005)
        return False


_PG_PUMP = [None]


def pg_log_sync(seconds=2.0):
    """Wait until the postmaster's output so far is in `PG_LOGFILE`."""
    pump = _PG_PUMP[0]
    return pump.drain(seconds) if pump is not None else True


def _pg_start_once(datadir, conf, hba, port, sockdir, logpath, seconds):
    # `ssl=off` is forced for the same reason the socket and the port are: the
    # scorer connects locally, over the unix socket and 127.0.0.1, and nothing
    # in this task is graded on TLS. It matters because `pg_createcluster` --
    # the ordinary Debian way to make a cluster, and what the reference
    # solution uses -- writes `ssl = on` with the key at
    # /etc/ssl/private/ssl-cert-snakeoil.key, which is readable only through
    # the `ssl-cert` supplementary group, and `as_user` deliberately clears
    # supplementary groups. Without this the postmaster dies with "could not
    # access private key file", the cluster never comes up, and a candidate
    # who did everything right scores zero on every PostgreSQL family.
    settings = ["listen_addresses=127.0.0.1",
                "port=%d" % port,
                "unix_socket_directories=%s" % sockdir,
                "ssl=off",
                "hba_file=%s" % hba]
    settings += PG_FORCED_LOGGING + PG_FORCED_SAFETY
    settings += _pg_forced_resources(datadir, conf)
    if conf:
        settings = ["config_file=%s" % conf] + settings
    flat = _optstr(settings)
    # No `-l`: pg_ctl would open that file as the postgres account. Without
    # it the postmaster keeps pg_ctl's stdout and stderr, which are the write
    # end of the pump's pipe (see `_LogPump`).
    cmd = as_user(PG_USER, [_pg_ctl(), "-D", datadir, "-o", flat,
                            "-w", "-t", str(seconds), "start"],
                  env={"HOME": PG_ROOT, "PGDATA": datadir})
    pump = _LogPump(logpath)
    _PG_PUMP[0] = pump
    try:
        p = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=pump.wfd,
                           stderr=pump.wfd, timeout=seconds + 30)
    except subprocess.TimeoutExpired:
        p = subprocess.CompletedProcess(cmd, 124, b"", b"timeout")
    except OSError as e:
        p = subprocess.CompletedProcess(cmd, 127, b"", str(e).encode())
    finally:
        os.close(pump.wfd)
    pump.drain()
    return p, settings


def start_postgres(port=PG_PORT, sockdir=PG_RUNDIR, seconds=120):
    svc = Service("postgres")
    datadir = find_cluster()
    if datadir is None:
        svc.detail = "no cluster under %s" % PG_ROOT
        return svc
    svc.present = True
    svc.detail = "datadir %s" % datadir

    if postgres_listening(port, sockdir):
        # The step instruction tells the candidate the cluster is brought up
        # from cold rather than inherited from the session that configured it,
        # and `pglog` depends on that being true: the logging settings are
        # forced on the postmaster's own command line, so a postmaster this
        # function did not start is not writing the log the scorer reads.
        # Nothing in a verifier image starts PostgreSQL before this, which is
        # why this is a belt and not the main path -- stop what is there and
        # start it again, once.
        svc.detail += "; a cluster was already listening, stopping it first"
        stop_postgres()
        if postgres_listening(port, sockdir):
            svc.detail += "; it would not stop"
            svc.log = _tail(PG_LOGFILE)
            return svc

    for tree, mode in ((datadir, "0700"), (PG_ETC, None)):
        if not chown_tree(tree, PG_USER, PG_USER, mode=mode):
            svc.detail += ("; could not give %s to %s within %ds"
                           % (tree, PG_USER, CHOWN_TIMEOUT))
            return svc
    _mkrundir(sockdir, PG_USER, PG_USER)
    os.makedirs(WORKDIR, exist_ok=True)

    # A cluster that was not shut down cleanly leaves a pid file behind. The
    # collect hook stops the servers before the datadir is copied, so a pid
    # file here is always stale: no process in this container can hold it.
    pidfile = os.path.join(datadir, "postmaster.pid")
    if os.path.exists(pidfile):
        try:
            os.unlink(pidfile)
            svc.detail += "; removed stale postmaster.pid"
        except OSError:
            pass

    # The scorer's file, not the server's: root-owned and 0600, and written
    # only by `_LogPump`. It used to be chowned to the postgres account so
    # that `pg_ctl -l` could open it -- which made it rewritable by anything
    # the candidate's cluster runs as that account.
    logpath = PG_LOGFILE
    if os.path.islink(logpath) or (os.path.exists(logpath)
                                   and not os.path.isfile(logpath)):
        try:
            os.unlink(logpath)
        except OSError:
            pass
    open(logpath, "a").close()
    try:
        os.chown(logpath, 0, 0)
        os.chmod(logpath, 0o600)
    except OSError:
        pass

    conf = _pg_config_file(datadir)
    hba, theirs = _write_hba(datadir, conf)
    p, forced = _pg_start_once(datadir, conf, hba, port, sockdir, logpath,
                               seconds)
    svc.forced = forced
    if p.returncode != 0 or not postgres_listening(port, sockdir):
        # No second attempt. There used to be one, with a flat `trust` hba, so
        # that a scorer locked out by the candidate's own rules could be told
        # apart from a cluster that will not start -- but a fallback that
        # grants every role passwordless local access grades a *different*
        # deployment from the one delivered, and it does so precisely when the
        # delivered one is broken. A cluster that does not come up under the
        # scorer's port, socket and hba is a failed cluster, and the families
        # that need it fail closed against it.
        note = _tail(logpath, 2000)
        svc.detail += "; pg_ctl failed (%s)" % note.strip().replace(
            "\n", " ")[-300:]
    else:
        svc.running = True
        svc.detail += "; listening on %s:%d (hba includes %s)" % (
            sockdir, port, theirs)
        svc.cleared = _pg_clear_overrides(svc)
    svc.log = _tail(logpath)
    return svc


def _pg_clear_overrides(svc):
    """Reset role- and database-level overrides of the forced logging.

    `PG_FORCED_LOGGING` is on the postmaster command line, which beats both
    configuration files and is beaten by one thing: a setting attached to a
    role or a database (`ALTER ROLE webgoat SET log_statement = 'none'`).
    This runs here rather than in the family that reads the log, because it
    has to happen before Apache is started and the application connects, and
    because this is the function that forced the settings in the first place.

    Imported inside the call: `pglog` reads what this module writes, so it
    imports this module, and a module-level import here would close the cycle.
    """
    from . import pglog
    conn = None
    try:
        conn, note = connect_postgres()
        if conn is None:
            svc.detail += "; could not clear logging overrides (%s)" % note
            return []
        cleared = pglog.enforce(conn)
        if cleared:
            svc.detail += "; reset %d logging override(s)" % len(cleared)
        return ["%s/%s: %s" % (r, d, n) for r, d, n in cleared]
    except Exception as e:
        svc.detail += ("; could not clear logging overrides (%s)"
                       % " ".join(str(e).split())[:160])
        return []
    finally:
        if conn is not None:
            try:
                conn.close()
            except Exception:
                pass


def stop_postgres(seconds=60):
    datadir = find_cluster()
    if datadir:
        run(as_user(PG_USER, [_pg_ctl(), "-D", datadir, "-m", "fast",
                              "-w", "-t", str(seconds), "stop"],
                    env={"HOME": PG_ROOT, "PGDATA": datadir}),
            timeout=seconds + 30)
    deadline = time.time() + seconds
    while time.time() < deadline and postgres_listening():
        time.sleep(0.5)


# --------------------------------------------------------------- apache ---

def start_apache(seconds=60):
    """Start Apache from the scorer's own configuration.

    /etc/apache2 and /etc/php are the only parts of the deployment that do not
    travel. The document root, the vhost and the frozen clock in envvars are
    therefore the scorer's pristine copies for every step, which is what keeps
    a recorded expectation from expiring on a wall clock and keeps the
    candidate out of the measurement apparatus.
    """
    svc = Service("apache")
    svc.present = os.path.isdir("/etc/apache2")
    if ready.apache_ready(HTTP_BASE, HTTP_HOST):
        svc.running = True
        svc.detail = "already listening"
        return svc
    _mkrundir("/var/run/apache2", "root", "root")
    _mkrundir("/var/lock/apache2", APP_USER, APP_USER)
    p = run(["apache2ctl", "-k", "start"], timeout=seconds)
    svc.detail = (p.stderr or b"").decode("utf-8", "replace").strip()[:300]
    deadline = time.time() + seconds
    while time.time() < deadline:
        if ready.apache_ready(HTTP_BASE, HTTP_HOST):
            svc.running = True
            svc.detail = "listening on 127.0.0.1:80"
            break
        time.sleep(0.5)
    else:
        if not svc.detail:
            svc.detail = "did not answer /robots.txt within %ds" % seconds
    svc.log = _tail("/var/log/apache2/error.log")
    return svc


def stop_apache():
    run(["apache2ctl", "-k", "stop"], timeout=60)


# ---------------------------------------------------------- connections ---

_DSN_RE = re.compile(
    r"""DatabaseSetting\s*\(\s*         # the application's own settings class
        (['"])(?P<adapter>.*?)\1\s*,\s*
        (['"])(?P<database>.*?)\3\s*,\s*
        (['"])(?P<user>.*?)\5\s*,\s*
        (['"])(?P<password>.*?)\7""",
    re.X | re.S)


def app_dsn(root=APP_ROOT):
    """What the deployed application is configured to connect to, or None.

    Read rather than assumed, for two reasons. The scorer needs the database
    name to read state out of, and guessing it would turn a rename into a
    crash instead of a score. And the adapter name is itself evidence: a tree
    that still says `mysqli` has not moved, whatever else is in it.
    """
    for pat in ("app/config/*.php", "app/config/**/*.php", "*.php"):
        for path in sorted(glob.glob(os.path.join(root, pat), recursive=True)):
            try:
                with open(path, "r", errors="replace") as fh:
                    m = _DSN_RE.search(fh.read())
            except OSError:
                continue
            if m:
                d = m.groupdict()
                d["file"] = path
                return d
    return None


def pick_database(available, prefer=None):
    """The deployment's database among those the server actually has.

    Preference order: what the application says it uses, then the shipped
    name, then -- if neither is there -- the non-system database with
    something in it, so a candidate who renamed the database is scored on its
    contents rather than on the name.
    """
    names = [n for n in available if n not in SYSTEM_DATABASES]
    for cand in (prefer, APP_DB):
        if cand and cand in available:
            return cand
    return names[0] if names else None


def postgres_databases(port=PG_PORT, sockdir=PG_RUNDIR):
    import psycopg2
    for host in (sockdir, "127.0.0.1"):
        try:
            conn = psycopg2.connect(host=host, port=port, user=PG_USER,
                                    dbname="postgres", connect_timeout=10)
        except Exception:
            continue
        try:
            cur = conn.cursor()
            cur.execute("SELECT datname FROM pg_database ORDER BY datname")
            return [r[0] for r in cur.fetchall()]
        finally:
            conn.close()
    return []


def connect_postgres(database=None, port=PG_PORT, sockdir=PG_RUNDIR):
    """(connection, note). The superuser socket first, the app's DSN last.

    The forced hba file guarantees the first of these, so a failure here means
    the cluster is not answering rather than that the scorer was locked out --
    which is the distinction the report has to be able to draw.
    """
    import psycopg2
    dsn = app_dsn()
    if database is None:
        database = pick_database(postgres_databases(port, sockdir),
                                 dsn["database"] if dsn else None)
    if database is None:
        raise RuntimeError("postgres has no non-system database")
    attempts = [
        (dict(host=sockdir, user=PG_USER), "postgres over the unix socket"),
        (dict(host="127.0.0.1", user=PG_USER), "postgres over 127.0.0.1"),
    ]
    if dsn:
        attempts.append((dict(host="127.0.0.1", user=dsn["user"],
                              password=dsn["password"]),
                         "the application's own credentials"))
    attempts.append((dict(host="127.0.0.1", user=APP_DB_USER,
                          password=APP_DB_PASSWORD), "webgoat/webgoat"))
    last = None
    for kw, note in attempts:
        try:
            conn = psycopg2.connect(port=port, dbname=database,
                                    connect_timeout=10, **kw)
        except Exception as e:           # psycopg2.OperationalError and kin
            last = e
            continue
        return conn, "%s as %s" % (database, note)
    raise RuntimeError("cannot connect to postgres database %r: %s"
                       % (database, last))


def mysql_databases(socket_path=MYSQL_SOCKET):
    from . import collect as _collect
    for user, password in (("root", ""), (APP_DB_USER, APP_DB_PASSWORD)):
        try:
            conn = _collect.open_mysql(user=user, password=password,
                                       database="mysql",
                                       unix_socket=socket_path)
        except Exception:
            continue
        try:
            cur = conn.cursor()
            cur.execute("SHOW DATABASES")
            return [r[0].decode() if isinstance(r[0], (bytes, bytearray))
                    else r[0] for r in cur.fetchall()]
        finally:
            conn.close()
    return []


def connect_mysql(database=None, socket_path=MYSQL_SOCKET):
    """(connection, note). Read as root over the socket, or as the app.

    MySQL has no include directive for its grant tables, so unlike PostgreSQL
    there is no way to guarantee the scorer a way in that the candidate cannot
    close. The shipped state -- root over the socket, no password -- is what
    step 1's instruction says must keep working.
    """
    from . import collect as _collect
    dsn = app_dsn()
    if database is None:
        database = pick_database(mysql_databases(socket_path),
                                 dsn["database"] if dsn else None)
    if database is None:
        raise RuntimeError("mysql has no non-system database")
    attempts = [(("root", ""), "root over the unix socket")]
    if dsn:
        attempts.append(((dsn["user"], dsn["password"]),
                         "the application's own credentials"))
    attempts.append(((APP_DB_USER, APP_DB_PASSWORD), "webgoat/webgoat"))
    last = None
    for (user, password), note in attempts:
        try:
            conn = _collect.open_mysql(user=user, password=password,
                                       database=database,
                                       unix_socket=socket_path)
        except Exception as e:
            last = e
            continue
        return conn, "%s as %s" % (database, note)
    raise RuntimeError("cannot connect to mysql database %r: %s"
                       % (database, last))


# ------------------------------------------------------------ the whole ---

class Deployment(object):
    def __init__(self):
        self.app = Service("app")
        self.mysql = Service("mysql")
        self.postgres = Service("postgres")
        self.apache = Service("apache")

    @property
    def services(self):
        return [self.app, self.mysql, self.postgres, self.apache]

    def as_dict(self):
        return dict((s.name, s.as_dict()) for s in self.services)

    @property
    def http_ready(self):
        return self.apache.running

    def summary(self):
        return ", ".join(
            "%s=%s" % (s.name, "up" if s.running else
                       ("down" if s.present else "absent"))
            for s in self.services)


def prepare_app(root=APP_ROOT):
    """Give the restored tree back to the account that serves it."""
    svc = Service("app")
    svc.present = os.path.isdir(root) and bool(os.listdir(root))
    if not svc.present:
        svc.detail = "%s is empty -- the artifact did not arrive" % root
        return svc
    if not chown_tree(root, APP_USER, APP_USER):
        svc.detail = ("could not give %s to %s within %ds"
                      % (root, APP_USER, CHOWN_TIMEOUT))
        return svc
    # jFramework's Doctrine plugin writes generated proxies underneath the
    # tree on first use; the agent image makes that one directory sticky and
    # world-writable and the artifact does not carry modes reliably.
    proxy = os.path.join(root, "app", "plugin", "doctrine", "proxy_cache")
    if os.path.isdir(proxy):
        try:
            os.chmod(proxy, 0o1777)
        except OSError:
            pass
    n = sum(len(f) for _, _, f in os.walk(root))
    svc.running = True
    svc.detail = "%d files" % n
    return svc


def bring_up(want_http=True):
    """Start everything the restored tree contains, and report what came up.

    Whether an absent MySQL or a dead PostgreSQL is a defect belongs to the
    step, not here: a step that has not created the cluster yet and a step
    that was supposed to remove MySQL both run through this unchanged.

    There is no overall deadline here, because there is nothing here to bound:
    every wait below carries its own. The longest path through this function
    is CHOWN_TIMEOUT on each of the four trees that are given back to their
    owners (`/app`, the MySQL data directory, the cluster, PG_ETC), MySQL's
    120-second listen wait, `stop_postgres`'s 60, `pg_ctl -w -t 120` under a
    150-second kill, Apache's 60 twice and `ready.app_ready`'s 30 -- 960
    seconds against a verifier budget of 5,400, and 0.5 measured. This used to
    take a `seconds=180` that no caller passed and nothing read, which is a
    bound a reader would have believed.
    """
    dep = Deployment()
    dep.app = prepare_app()
    dep.mysql = start_mysql()
    dep.postgres = start_postgres()
    if want_http:
        dep.apache = start_apache()
        if dep.apache.running:
            # The front controller is the candidate's code, so it runs as the
            # unprivileged account that serves it, never as the scorer. It is
            # a warm-up signal, not a gate: an application that cannot answer
            # is a score of zero on the behaviour family, not a broken run.
            probe = ready.app_ready(prefix=as_user(APP_USER, []))
            dep.apache.detail += ("; front controller answered"
                                  if probe else
                                  "; front controller did not render")
    return dep


def tear_down():
    """Stop everything, in the order that keeps the data directories clean."""
    stop_apache()
    stop_mysql()
    stop_postgres()
