"""Wait for the deployment to answer, without writing to the database.

A readiness probe that goes through the application is not free here: the stats
plugin runs on every HTTP request and writes a row to `jf_stats` carrying a
fresh PHP session id. One `curl /about` before the snapshot therefore puts a
random value into `tables_before`, and the recorded baseline stops being
reproducible -- which is exactly what the first two-pass recording showed
(3004 rows both passes, two different digests).

So the probes are chosen to touch nothing the snapshot reads:

  Apache    GET /robots.txt. The repository's own .htaccess exempts it from the
            rewrite, so Apache serves the file and PHP never runs.
  the app   php jframework.php "/about" -- the CLI front controller, which
            routes the same controllers against the same database.
            `StatsPlugin::Insert()` opens with
            `if (jf::$RunMode->IsCLI()) return false;`, so a CLI request
            exercises PHP, the database layer and the models while writing no
            stats row. Measured: jf_stats and jf_logs are unchanged across four
            CLI requests; only jf_session grows, and jf_session is excluded from
            the comparison (ENGINE=MEMORY, random ids). jf_options was measured
            unchanged too, but that reading was luck rather than evidence -- see
            below; it is unchanged now because the clock is.

Both are needed. The static probe says the web server is listening; the CLI
probe says PHP and the database behind it work. Neither on its own does.

The CLI probe runs on the deployment's frozen clock, not the host's. Apache
gets the freeze from /etc/apache2/envvars, which reaches mod_php and nothing
else, and the difference is not cosmetic: jf\Settings::_Sweep() fires
`DELETE FROM jf_options WHERE Expiration<=?` on a one-in-ten draw inside every
load and every save, and the seeded rows are stamped 2026-04-01 -- ahead of the
frozen clock, behind any host clock that has moved on. Three probe requests
under the host's date took jf_options from 371 rows to 5, and one oracle run
scored behavior:tables 48/50 on exactly that. The image installs a wrapped
interpreter for the same reason; the probe sets the same variables itself so
that it is right even in an image that has not got one.
"""

import os
import subprocess
import time
import urllib.error
import urllib.request

DOCROOT = "/app"

# The deployment's clock, as environment/Dockerfile freezes it for Apache and
# as files/php-frozen freezes it for the interpreter.
FAKETIME_LIB = "/usr/lib/x86_64-linux-gnu/faketime/libfaketime.so.1"
FROZEN_CLOCK = (
    ("LD_PRELOAD", FAKETIME_LIB),
    ("FAKETIME", "@2026-01-01 00:00:00 x0"),
    ("FAKETIME_DONT_FAKE_MONOTONIC", "1"),
    ("TZ", "UTC"),
)


def frozen_prefix():
    """An `env` prefix that puts the *next* program on the frozen clock.

    A prefix, not `subprocess(env=...)`, and the difference is not stylistic.
    The probe is run through `setpriv`, which starts as root and drops to the
    account that serves the application before exec. libfaketime opens a POSIX
    semaphore named after the process it is loaded into, so preloading it into
    the root end of that chain leaves the unprivileged end unable to reopen it:

        libfaketime: In ft_shm_init(), sem_open failed and recreation attempts
        failed: Permission denied

    and the front controller renders nothing. Measured -- it is what took
    `deployment.apache.detail` from "front controller answered" to "front
    controller did not render" on oracle-v6. Setting the variables in a second
    `env` after the drop puts libfaketime in the one process that needs it, as
    the same uid that runs it.

    Empty when libfaketime is absent: an image without it must still be able to
    run the probe, and a dangling LD_PRELOAD would break every php call.
    """
    if not os.path.exists(FAKETIME_LIB):
        return []
    return ["env"] + ["%s=%s" % kv for kv in FROZEN_CLOCK]


class NotReady(RuntimeError):
    pass


def _http(base, host, path, timeout):
    req = urllib.request.Request(base.rstrip("/") + path)
    req.add_header("Host", host)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status, resp.read()


def apache_ready(base="http://127.0.0.1", host="localhost", timeout=2.0):
    try:
        status, _ = _http(base, host, "/robots.txt", timeout)
    except (urllib.error.URLError, OSError):
        return False
    return status == 200


def app_ready(docroot=DOCROOT, path="/about", timeout=30.0, prefix=()):
    """`prefix` drops the probe to an unprivileged account.

    Inside the verifier this is candidate code, and the scorer runs as root
    with /logs/verifier root-owned and unreadable to anyone else. The probe is
    therefore run through setpriv as the account that serves the application,
    so nothing the candidate wrote ever executes with the scorer's identity.
    During recording the prefix is empty; the recorder is not being scored.
    """
    if not os.path.isfile(os.path.join(docroot, "jframework.php")):
        return False
    try:
        cmd = list(prefix) + frozen_prefix() + ["php", "jframework.php", path]
        p = subprocess.run(cmd, cwd=docroot, capture_output=True,
                           timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        return False
    # The front controller always exits 0; what distinguishes a working
    # deployment is that the controller rendered, not that PHP started.
    return p.returncode == 0 and b"</html>" in p.stdout


def wait(base="http://127.0.0.1", host="localhost", docroot=DOCROOT,
         seconds=180, interval=1.0, prefix=()):
    """Block until both probes pass, or raise NotReady with what failed."""
    deadline = time.time() + seconds
    last = "no attempt made"
    while time.time() < deadline:
        if not apache_ready(base, host):
            last = "apache: GET /robots.txt did not answer 200"
        elif not app_ready(docroot, prefix=prefix):
            last = "app: php jframework.php /about did not render"
        else:
            return
        time.sleep(interval)
    raise NotReady("not ready after %ds -- %s" % (seconds, last))
