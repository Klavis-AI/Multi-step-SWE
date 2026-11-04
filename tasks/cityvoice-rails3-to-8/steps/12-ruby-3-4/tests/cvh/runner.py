"""Drives the behaviour suite against a running CityVoice tree.

The same module records (against the untouched baseline, in the authoring-only
recorder image) and replays (against a candidate's tree, in a step's verifier
image).  Nothing here knows which Rails version it is talking to: it starts the
process the repository's own Procfile declares, speaks HTTP to it, runs the
repository's own rake tasks, and captures mail at the SMTP sink.

Only the application's subprocesses get the frozen clock.  The harness itself
runs on the real clock, because its own timeouts have to keep working, and
because CPython under libfaketime's LD_PRELOAD fails `datetime.utcnow`.
"""

from __future__ import annotations

import argparse
import contextlib
import glob
import json
import os
import re
import shlex
import shutil
import signal
import socket
import stat
import subprocess
import sys
import time

from . import norm, scenarios
from .httpc import Client
from .sink import Sink
from .stub import Stub

# The clock is moved, not stopped.  Stopping it was tried and does not work:
# libfaketime 0.9.10 rewrites every select()/poll() timeout by the speed
# factor, so `x0` makes each call raise EINVAL.  Measured on this box, both
# with and without -DFAKE_SLEEP: `IO.select([r], nil, nil, 0.30)` -> EINVAL,
# and unicorn's master and worker loops then spin on that error instead of
# serving, taking the whole subscriptions world to HTTP 500.
#
# So the clock ticks from a fixed origin, and the two behaviours that depended
# on *which second* a value landed in are handled where they belong:
#   * the second itself -- norm.py labels any timestamp inside a day of the
#     origin `<NOW>`, so `Time.now.to_i` compares equal between runs;
#   * LocationSubscription.with_new_answers, whose `answers.created_at >=
#     last_email_sent_at` flips depending on whether the digest was delivered
#     in the same second the answers were recorded -- scenarios.py crosses a
#     second boundary explicitly so the comparison is decided by the data.
FAKE_CLOCK = "@2016-01-19 12:00:00"

# Hook values (NOTES-authoring.md section 1.3).  Every one of them is a knob
# the application already reads; none of them is a new interface.
APP_ENV = {
    "RAILS_ENV": "production",
    "RACK_ENV": "production",
    "PORT": "3000",
    "SECRET_TOKEN": "4b1e071e0c6f4774088668dd143b1192d762a23d7b14d27f90e8297d48b8dde2"
                    "73d8172454ad9ce91db20b7fcb3e544be2c3cdc28443d5a3f64f31f31b649a95",
    "APP_URL": "cityvoice.test",
    "SENDGRID_USERNAME": "cityvoice-harness",
    "SENDGRID_PASSWORD": "harness-secret",
    "MAPBOX_MAP_ID": "cityvoice.harness-map",
    "GOOGLE_ANALYTICS_ID": "",
    "LOCK_CITYVOICE": "",
    "DISABLE_DATABASE_ENVIRONMENT_CHECK": "1",
    "LANG": "en_US.UTF-8",
    "LC_ALL": "en_US.UTF-8",
    "TZ": "UTC",
}

_NOISE = re.compile(
    r"^\s*$"
    r"|warning:"
    r"|^DEPRECATION"
    r"|^Bundler"
    r"|deprecat"
    r"|^Ignoring "
    r"|^Your Gemfile lists"
    r"|^The dependency .* will be unused"
    r"|^Fetching |^Using |^Resolving "
    # A tab-indented line is a Ruby backtrace frame.  These arrive attached to
    # a line that is already noise -- protected_attributes prints "WARNING:
    # Can't mass-assign protected attributes" and then five frames -- and
    # dropping the header while keeping the frames left `lib/...rb:42` file
    # and line numbers in the goldens, which are not behaviour and which move
    # whenever anyone touches the file.  Nothing this application prints as
    # output is tab-indented.
    r"|^\t",
    re.IGNORECASE,
)
_EXC = re.compile(r"^([A-Z][A-Za-z0-9_]*(?:::[A-Z][A-Za-z0-9_]*)*(?:Error|Exception|Invalid|NotFound|Missing)[A-Za-z0-9_]*)\b")


# --------------------------------------------------------------------------
# process plumbing
# --------------------------------------------------------------------------

# Set by `--coverage DIR`.  Authoring measurement only: it is never on during
# a recording that produces goldens, because instrumenting every `require`
# changes timing, and timing is the one thing this suite spent the longest
# making irrelevant.  The coverage pass is recorded separately and its cases
# are thrown away.
COVERAGE_DIR = None
COVERAGE_HOOK = None

# The application's HOME, pinned to a container-local directory the app's uid
# can write.
#
# This is not an application input -- nothing in the repository reads `HOME`.
# Bundler and RubyGems do, and an unprivileged process whose HOME is root's
# gets "`/root` is not writable." on *stdout* from Bundler 1.17, where it lands
# in the middle of a rake task's recorded output.  Bundler 1.10, which the
# recorder's jessie baseline ships, is silent about the same condition, so
# without this the identical tree transcribes differently in the recorder and
# in a verifier for a reason that has nothing to do with the migration.  The
# environment is normalized rather than the line being added to `_NOISE`:
# a filter would also hide the next tool that complains about HOME.
HOME_DIR = "/tmp/cvhome"


def app_env(extra=None):
    env = dict(os.environ)
    env.update(APP_ENV)
    env["HOME"] = HOME_DIR
    env["LD_PRELOAD"] = "/usr/local/lib/faketime/libfaketime.so.1"
    env["FAKETIME"] = FAKE_CLOCK
    env["FAKETIME_DONT_FAKE_MONOTONIC"] = "1"
    # Timed waits keep their real deadline (trajectory review 2026-10-20,
    # C-1).  libfaketime's pthread_cond_timedwait wrapper turns its "forced
    # monotonic fix" on by itself on glibc >= 2.30, and then rebases a
    # CLOCK_MONOTONIC deadline against a *faked* monotonic reading even though
    # monotonic time is not faked above: the deadline lands years in the past
    # and every timed wait returns at once.  Ruby 2.7-3.2 parks a sleeping
    # thread on exactly such a condvar, so each one spins on the GVL and the
    # thread doing the work runs 30-60x slower (measured; 2.3/2.4 instead lose
    # `ConditionVariable#wait(m, t)`, which returned immediately).  "0" makes
    # the wrapper hand the application's own deadline through unchanged, which
    # is right precisely because monotonic time is real.  The wall clock, file
    # times and the CLOCK_REALTIME condvar path are faked exactly as before.
    env["FAKETIME_FORCE_MONOTONIC_FIX"] = "0"
    for key in ("PYTHONHOME", "PYTHONPATH"):
        env.pop(key, None)
    if COVERAGE_DIR:
        # Appended, not assigned: the recorder already sets `-W0`, and losing
        # it would put Ruby's own warnings into stdout that every golden was
        # recorded without.
        env["RUBYOPT"] = ("%s -I%s -rcvcov"
                          % (env.get("RUBYOPT", "").strip(), COVERAGE_HOOK)).strip()
        env["CVCOV_DIR"] = COVERAGE_DIR
    if extra:
        env.update(extra)
    return env


class Runner:
    """Runs application commands, optionally dropping privilege first.

    The drop uses Popen's own `user`/`group` arguments rather than setpriv:
    setpriv arrived in util-linux 2.26 and the recorder's Debian jessie base
    ships 2.25, so a wrapper binary would make the recorder and the verifiers
    drop privilege by different mechanisms.  `preexec_fn` is not used either --
    the harness runs the SMTP sink and the HTTP stub on threads, and forking a
    threaded process into arbitrary Python is the documented deadlock.
    """

    def __init__(self, app_root, unprivileged_uid=None, log_dir=None):
        self.app_root = app_root
        self.uid = unprivileged_uid
        self.log_dir = log_dir

    def _creds(self):
        if self.uid is None:
            return {}
        return {"user": self.uid, "group": self.uid, "extra_groups": []}

    def run(self, argv, timeout=900, extra_env=None):
        proc = subprocess.run(
            argv, cwd=self.app_root, env=app_env(extra_env),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout,
            **self._creds())
        return (proc.returncode,
                proc.stdout.decode("utf-8", "replace"),
                proc.stderr.decode("utf-8", "replace"))


class AppServer:
    """The `web` process the repository's own Procfile declares."""

    def __init__(self, runner, port, log_path):
        self.runner = runner
        self.port = port
        self.log_path = log_path
        self.proc = None
        self.command = None

    def _web_command(self):
        procfile = os.path.join(self.runner.app_root, "Procfile")
        with open(procfile, "r", encoding="utf-8") as handle:
            for line in handle:
                name, _, command = line.partition(":")
                if name.strip() == "web":
                    return command.strip()
        raise RuntimeError("Procfile declares no `web` process")

    def start(self, extra_env=None):
        self.command = self._web_command()
        env = {"PORT": str(self.port)}
        env.update(extra_env or {})
        log = open(self.log_path, "ab")
        log.write(b"\n==== %s ====\n" % self.command.encode())
        log.flush()
        self.proc = subprocess.Popen(
            ["bash", "-c", self.command], cwd=self.runner.app_root,
            env=app_env(env), stdout=log, stderr=log, start_new_session=True,
            **self.runner._creds())
        log.close()

    def wait_ready(self, timeout=180.0):
        client = Client("127.0.0.1", self.port, timeout=15.0)
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            if self.proc.poll() is not None:
                return False, "server exited with %s" % self.proc.returncode
            try:
                with socket.create_connection(("127.0.0.1", self.port), 1.0):
                    pass
            except OSError as exc:
                last = str(exc)
                time.sleep(0.4)
                continue
            try:
                client.request("GET", "/robots.txt")
                return True, None
            except Exception as exc:  # noqa: BLE001 - any protocol error retries
                last = str(exc)
                time.sleep(0.4)
        return False, last or "timed out"

    def stop(self):
        if self.proc is None:
            return
        if self.proc.poll() is None:
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
            except OSError:
                pass
            try:
                self.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
                except OSError:
                    pass
                self.proc.wait(timeout=10)
        self.proc = None


# --------------------------------------------------------------------------
# scenario context
# --------------------------------------------------------------------------

class Ctx:
    def __init__(self, runner, client, sink, stub, labeller, fixture_dir):
        self.runner = runner
        self.client = client
        self.sink = sink
        self.stub = stub
        self.labeller = labeller
        self.fixture_dir = fixture_dir
        # A rake task that takes a CSV path records that path, and the path
        # sits under the run's own log directory -- so two recordings of the
        # identical import disagree on the argument.  The directory is the
        # harness's choice; the basename is the fixture's identity and stays
        # scored.
        labeller.alias(fixture_dir.rstrip("/") + "/", "<FIXTURES>/")
        self.cases = []
        self.world = None
        self.tokens = []
        self.notes = {}
        self.adv = False

    # -- recording ---------------------------------------------------------

    @contextlib.contextmanager
    def adversarial(self):
        """Mark every case recorded inside the block as an edge input.

        The classification is declared where the input is written rather than
        recovered afterwards from its name, because a name is a label someone
        chose and the share of adversarial inputs is a bar the suite has to
        meet (migration_prompt.md rule 7).  It rides on the case, not on
        `obs`, so tagging changes no score and no stability verdict.
        """
        previous = self.adv
        self.adv = True
        try:
            yield self
        finally:
            self.adv = previous

    def _add(self, case_id, kind, obs):
        full = "%s/%s" % (self.world, case_id)
        self.cases.append({"id": full, "kind": kind, "obs": obs,
                           "adv": self.adv})
        return obs

    # -- http --------------------------------------------------------------

    def http(self, case_id, method, path, params=None, headers=None,
             want=None, cookies=False, encode="form", body=None):
        try:
            resp = self.client.request(method, path, params=params,
                                       headers=headers, use_cookies=cookies,
                                       encode=encode, body=body)
        except Exception as exc:  # noqa: BLE001
            return self._add(case_id, "http",
                             {"transport_error": type(exc).__name__})
        # Alias before projecting: a token has to carry the same label the
        # first time it is seen, wherever that is, or the label depends on
        # which surface happened to be visited first.
        self._scan_tokens(resp.body + " " + (resp.headers.get("location") or ""))
        obs = norm.project_response(resp.status, resp.headers, resp.body,
                                    self.labeller, want=want, raw=resp.raw)
        obs["request"] = {"method": method, "path": norm.norm_url(path, self.labeller)}
        self._add(case_id, "http", obs)
        obs["_raw"] = resp
        return obs

    def get(self, case_id, path, **kw):
        return self.http(case_id, "GET", path, **kw)

    def post(self, case_id, path, params=None, **kw):
        return self.http(case_id, "POST", path, params=params, **kw)

    def twilio(self, case_id, path, params=None):
        """A Twilio webhook POST: form encoded, XML expected, no cookies."""
        return self.http(case_id, "POST", path, params=params or {}, want="xml")

    # -- rake --------------------------------------------------------------

    def rake(self, case_id, task, timeout=600, project=None):
        # Split for argv, recorded whole.  `db:rollback STEP=3` is two argv
        # elements and one task as far as the recording is concerned, and
        # fixture paths are checked for whitespace where they are written.
        code, out, err = self.runner.run(
            ["bundle", "exec", "rake"] + task.split(), timeout=timeout)
        lines = [norm.scrub_db_noise(self.labeller.scrub(line.rstrip()))
                 for line in out.splitlines() if not _NOISE.search(line)]
        exc = None
        for line in err.splitlines():
            match = _EXC.match(line.strip())
            if match:
                exc = match.group(1)
                break
        obs = {
            "task": self.labeller.scrub(task),
            "exit_ok": code == 0,
            "stdout": lines if project is None else project(lines),
            "aborted": "rake aborted!" in err,
            "exception": exc,
        }
        return self._add(case_id, "rake", obs)

    def run_quiet(self, argv, timeout=900):
        """A setup command whose output is not scored (schema loads, assets)."""
        return self.runner.run(argv, timeout=timeout)

    # -- mail --------------------------------------------------------------

    def mail(self, case_id, count=0, timeout=25.0):
        messages = self.sink.wait_for(count, timeout=timeout)
        for message in messages:
            for part in message["parts"]:
                self._scan_tokens(part["text"])
        obs = {"count": len(messages), "messages": [
            {
                "from": self.labeller.scrub(m["from"]),
                "to": self.labeller.scrub(m["to"]),
                "envelope_to": [self.labeller.scrub(a) for a in m["envelope_to"]],
                "subject": self.labeller.scrub(m["subject"]),
                "parts": [
                    {"content_type": p["content_type"],
                     "body": norm.project_html(p["text"], self.labeller)
                     if "html" in p["content_type"]
                     else norm.project_text(p["text"], self.labeller)}
                    for p in m["parts"]
                ],
            }
            for m in messages
        ]}
        return self._add(case_id, "mail", obs)

    # -- bookkeeping -------------------------------------------------------

    def fact(self, case_id, **values):
        """Record a plain value as its own case.

        Used where a scenario would otherwise *branch* on what it observed.  A
        branch makes the number of cases depend on the application's answer, so
        a regression removes cases instead of failing them -- and a mean over
        the cases that remain goes up.  Recording the value turns the same
        information into a case that can disagree.
        """
        return self._add(case_id, "fact",
                         {k: self.labeller.scrub(v) if isinstance(v, str) else v
                          for k, v in sorted(values.items())})

    @staticmethod
    def tick():
        """Block until the wall clock crosses into the next second.

        The application's clock is offset from this one but runs at the same
        rate, so crossing a second here crosses one there.  Scenarios call this
        where the application compares two of its own timestamps and the
        comparison would otherwise be decided by how fast the box was.
        """
        start = int(time.time())
        while int(time.time()) == start:
            time.sleep(0.05)

    # `token=` appears in the confirm and unsubscribe URLs the application
    # builds, which is the only place a raw auth token reaches the outside.
    _TOKEN_IN_URL = re.compile(r"[?&]token=([A-Za-z0-9_\-]{16,64})(?![A-Za-z0-9_\-])")

    def _scan_tokens(self, text):
        for match in self._TOKEN_IN_URL.finditer(text or ""):
            token = match.group(1)
            if token not in self.tokens:
                self.tokens.append(token)
                self.labeller.alias(token,
                                    "<AUTHTOKEN:%d>" % (len(self.tokens) - 1))

    # -- fixtures ----------------------------------------------------------

    def fixture(self, name, text):
        # The path goes into a rake task invocation that is split on whitespace
        # for argv, so a fixture name with a space in it would arrive at rake
        # as two arguments and fail in a way that looks like the application's
        # doing.  Refuse it here instead.
        if name.split() != [name]:
            raise ValueError("fixture name %r contains whitespace" % name)
        path = os.path.join(self.fixture_dir, name)
        with _create_plain(path) as handle:
            handle.write(text)
        return path

    # -- csrf --------------------------------------------------------------

    _CSRF = re.compile(
        r'<meta[^>]+name="csrf-token"[^>]+content="([^"]*)"'
        r"|<input[^>]+name=\"authenticity_token\"[^>]+value=\"([^\"]*)\"")

    def csrf_from(self, response):
        match = self._CSRF.search(response.body)
        if not match:
            return None
        import html as html_mod
        return html_mod.unescape(match.group(1) or match.group(2))


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def _fresh_dir(path, mode):
    """Make `path` a directory this process has just created, and return it.

    The path is fixed and under /tmp, and the application's uid has run code
    (its bundle install) before this harness starts, so whatever already sits
    there may be that uid's: a link, or a directory it owns and can fill with
    links.  It is removed without following anything, and the directory is
    made anew.  `mkdir` fails if the name was taken again in between, and the
    run then records nothing rather than writing through someone else's entry.
    """
    try:
        st = os.lstat(path)
    except FileNotFoundError:
        st = None
    if st is not None:
        if stat.S_ISDIR(st.st_mode):
            shutil.rmtree(path)
        else:
            os.unlink(path)
    os.mkdir(path, 0o700)
    st = os.lstat(path)
    if not stat.S_ISDIR(st.st_mode) or st.st_uid != os.geteuid():
        raise SystemExit("%s: not a directory this harness created" % path)
    os.chmod(path, mode)
    return path


def _create_plain(path):
    """Open `path` for writing as a plain file, never through a link."""
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW,
                 0o644)
    os.fchmod(fd, 0o644)
    return open(fd, "w", encoding="utf-8")


def _probe_fixture_readable(fixture_dir, args):
    """Refuse to record if the application's uid cannot read a fixture.

    The failure this guards against is silent by construction: an unreadable
    fixture makes every importer case abort identically, so the suite still
    produces a full set of cases, they still agree run to run, and the
    stability check still says zero.  A canary read costs one process.
    """
    if args.uid is None:
        return
    canary = os.path.join(fixture_dir, "readable.canary")
    with _create_plain(canary) as handle:
        handle.write("canary\n")
    probe = subprocess.run(["cat", canary], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT,
                           **Runner(args.app_root, args.uid)._creds())
    os.unlink(canary)
    if probe.returncode != 0 or probe.stdout != b"canary\n":
        raise SystemExit(
            "fixtures: uid %s cannot read %s: %s"
            % (args.uid, canary,
               probe.stdout.decode("utf-8", "replace").strip()))


def _probe_home_writable(args):
    """Refuse to run if the application's uid cannot write `HOME_DIR`.

    The same shape as the fixture canary above, for the same reason: an
    unwritable HOME does not abort anything, it adds one Bundler line to the
    stdout of every rake task, so the damage is invisible until a transcript
    is compared against one made somewhere else.
    """
    probe = subprocess.run(
        ["sh", "-c", 'printf ok > "$HOME/.canary" && cat "$HOME/.canary"'],
        env=app_env(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        **Runner("/", args.uid)._creds())
    if probe.returncode != 0 or probe.stdout != b"ok":
        raise SystemExit(
            "home: uid %s cannot write %s: %s"
            % (args.uid, HOME_DIR,
               probe.stdout.decode("utf-8", "replace").strip()))
    os.unlink(os.path.join(HOME_DIR, ".canary"))


def _reset_database(runner, log):
    code, out, err = runner.run(
        ["bundle", "exec", "rake", "db:schema:load"], timeout=600)
    log.write("\n==== db:schema:load exit=%d ====\n%s\n%s\n" % (code, out, err))
    log.flush()
    return code == 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="cvh.runner")
    parser.add_argument("--app-root", default="/app")
    parser.add_argument("--out", required=True)
    parser.add_argument("--log-dir", default="/tmp/cvh-logs")
    parser.add_argument("--port", type=int, default=3000)
    parser.add_argument("--uid", type=int, default=None,
                        help="run application commands as this uid")
    parser.add_argument("--worlds", default="",
                        help="comma separated subset of world names")
    parser.add_argument("--setup", action="store_true",
                        help="create the database and precompile assets first")
    parser.add_argument("--coverage", default=None,
                        help="collect Ruby line coverage into this directory "
                             "(authoring measurement; perturbs timing)")
    args = parser.parse_args(argv)

    if args.coverage:
        global COVERAGE_DIR, COVERAGE_HOOK
        # Neither the hook nor the dumps can live under a bind mount here.  The
        # harness mount is root-only, and the out mount inherits the authoring
        # directory's 0700 from the host, so an unprivileged ruby cannot even
        # traverse into it -- `-I` then contributes nothing to the load path and
        # `-rcvcov` fails with LoadError, which unicorn reports as "server
        # exited with 1" for every world.  That looks exactly like an
        # application fault and is not one.  A container-local directory has no
        # such inheritance; the dumps are copied out at the end of the run.
        os.makedirs(args.coverage, exist_ok=True)
        work = "/tmp/cvcov"
        COVERAGE_HOOK = os.path.join(work, "hook")
        COVERAGE_DIR = os.path.join(work, "dumps")
        os.makedirs(COVERAGE_HOOK, exist_ok=True)
        os.makedirs(COVERAGE_DIR, exist_ok=True)
        src = os.path.join(os.path.dirname(os.path.dirname(
            os.path.abspath(__file__))), "cov")
        for name in os.listdir(src):
            if not name.endswith(".rb"):
                continue
            shutil.copyfile(os.path.join(src, name),
                            os.path.join(COVERAGE_HOOK, name))
            os.chmod(os.path.join(COVERAGE_HOOK, name), 0o644)
        os.chmod(work, 0o755)
        os.chmod(COVERAGE_HOOK, 0o755)
        os.chmod(COVERAGE_DIR, 0o777)
        if not os.path.exists(os.path.join(COVERAGE_HOOK, "cvcov.rb")):
            raise SystemExit("coverage: cvcov.rb did not stage from %s" % src)
        # Prove the hook loads and writes *before* spending a whole run on it.
        probe = subprocess.run(
            ["ruby", "-e", "raise 'no Coverage' unless defined?(Coverage)"],
            env=app_env(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            **Runner(args.app_root, args.uid)._creds())
        if probe.returncode != 0:
            raise SystemExit("coverage: hook does not load as uid %s:\n%s"
                             % (args.uid,
                                probe.stdout.decode("utf-8", "replace")))
        if not glob.glob(os.path.join(COVERAGE_DIR, "cov-*.json")):
            raise SystemExit("coverage: hook loaded but wrote no dump as uid %s"
                             % args.uid)

    os.makedirs(args.log_dir, exist_ok=True)
    os.chmod(args.log_dir, 0o755)

    # Container-local, NOT under the log directory.  The log directory lives on
    # `/out`, a bind mount of the authoring workspace, which inherits that
    # directory's 0700 root ownership from the host: an unprivileged `rake`
    # cannot traverse into it however permissive every directory below it is.
    # Making the fixture directory 0755 therefore did nothing, and all 23
    # `import:*[path]` cases recorded the same Errno::EACCES abort -- one
    # constant, stable across runs, standing in for the importers' behaviour.
    # The fixtures are copied into the log directory at the end of the run, so
    # the recording still carries the inputs it was made from.
    #
    # Made fresh by this process (`_fresh_dir`): root writes every fixture
    # into it and copies every entry out of it, so it must be a directory no
    # other uid could have created or put a link in.
    fixture_dir = _fresh_dir("/tmp/cvfixtures", 0o755)
    _probe_fixture_readable(fixture_dir, args)

    os.makedirs(HOME_DIR, exist_ok=True)
    os.chmod(HOME_DIR, 0o777)
    _probe_home_writable(args)

    runner = Runner(args.app_root, args.uid, args.log_dir)
    log = open(os.path.join(args.log_dir, "runner.log"), "w", encoding="utf-8")
    setup_report = {}

    sink = Sink(port=587).start()
    stub = Stub(port=80).start()

    try:
        if args.setup:
            for name, argvv in (
                ("db_create", ["bundle", "exec", "rake", "db:create"]),
                ("schema_load", ["bundle", "exec", "rake", "db:schema:load"]),
                ("assets", ["bundle", "exec", "rake", "assets:precompile"]),
            ):
                code, out, err = runner.run(argvv, timeout=1800)
                setup_report[name] = code
                log.write("\n==== %s exit=%d ====\n%s\n%s\n" % (name, code, out, err))
                log.flush()

        wanted = [w for w in args.worlds.split(",") if w] or None
        all_cases = []
        world_report = {}

        for world in scenarios.WORLDS:
            if wanted and world.name not in wanted:
                continue
            labeller = norm.Labeller()
            # The only origins that are the harness's own doing: the address
            # the server was bound to, and the APP_URL the mailer builds
            # absolute links from.  Anything else in a URL is the app's.
            labeller.seed_hosts([
                "http://127.0.0.1:%d" % args.port,
                "http://localhost:%d" % args.port,
                "http://%s" % APP_ENV["APP_URL"],
                "https://%s" % APP_ENV["APP_URL"],
            ])
            client = Client("127.0.0.1", args.port, timeout=120.0)
            ctx = Ctx(runner, client, sink, stub, labeller, fixture_dir)
            ctx.world = world.name
            stub.set_mode("healthy")
            sink.take()

            ok = _reset_database(runner, log)
            prepare_raised = None
            if world.prepare:
                try:
                    world.prepare(ctx)
                except Exception as exc:  # noqa: BLE001
                    log.write("world %s prepare raised %r\n" % (world.name, exc))
                    import traceback
                    traceback.print_exc(file=log)
                    prepare_raised = repr(exc)
            server = AppServer(runner, args.port,
                               os.path.join(args.log_dir, "%s-server.log" % world.name))
            server.start(world.env)
            ready, why = server.wait_ready()
            world_report[world.name] = {"schema_load": ok, "booted": ready,
                                        "why": why}
            if prepare_raised:
                world_report[world.name]["prepare_raised"] = prepare_raised
            log.write("\n==== world %s booted=%s (%s) ====\n"
                      % (world.name, ready, why))
            log.flush()
            if ready:
                try:
                    world.fn(ctx)
                except Exception as exc:  # noqa: BLE001
                    log.write("world %s raised %r\n" % (world.name, exc))
                    import traceback
                    traceback.print_exc(file=log)
                    world_report[world.name]["raised"] = repr(exc)
            server.stop()
            stub.set_mode("healthy")
            for case in ctx.cases:
                case["obs"].pop("_raw", None)
            all_cases.extend(ctx.cases)
            world_report[world.name]["cases"] = len(ctx.cases)

        adversarial = sum(1 for c in all_cases if c.get("adv"))
        payload = {
            "format": 1,
            "setup": setup_report,
            "worlds": world_report,
            "adversarial": adversarial,
            "cases": all_cases,
        }
        with open(args.out, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=1, sort_keys=True)
            handle.write("\n")
        print("wrote %d cases from %d worlds to %s (%d adversarial, %.1f%%)"
              % (len(all_cases), len(world_report), args.out, adversarial,
                 100.0 * adversarial / len(all_cases) if all_cases else 0.0))
    finally:
        sink.stop()
        stub.stop()
        out_fixtures = os.path.join(args.log_dir, "fixtures")
        os.makedirs(out_fixtures, exist_ok=True)
        for name in sorted(os.listdir(fixture_dir)):
            src = os.path.join(fixture_dir, name)
            # Plain files only, and never through a link.
            if not stat.S_ISREG(os.lstat(src).st_mode):
                continue
            shutil.copyfile(src, os.path.join(out_fixtures, name),
                            follow_symlinks=False)
        if COVERAGE_DIR:
            dumps = sorted(glob.glob(os.path.join(COVERAGE_DIR, "cov-*.json")))
            for path in dumps:
                shutil.copyfile(
                    path, os.path.join(args.coverage, os.path.basename(path)))
            print("coverage: copied %d dumps to %s" % (len(dumps),
                                                       args.coverage))
            log.write("\n==== coverage: %d dumps ====\n" % len(dumps))
        log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
