#!/usr/bin/env python3
"""Drive libStorage through its own surface and normalize what comes back.

One module is used for both recording (against the untouched baseline) and
replay (against a migrated tree), so an expectation can never be produced by
a different code path than the one that checks it.

A scenario is a fresh `lss` server with a fresh data root plus an ordered list
of HTTP exchanges, or a single CLI invocation. Everything volatile is
normalized identically in both directions; nothing else is touched, so key
order, whitespace and message text are all part of the expectation.
"""
import base64
import hashlib
import hmac
import json
import os
import re
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

# ---------------------------------------------------------------------------
# normalization
# ---------------------------------------------------------------------------

# Unix-second stamps the task manager writes into every task record.
_TIME_FIELDS = ("queueTime", "startTime", "completeTime")
_TIME_RE = re.compile(
    r'("(?:%s)"\s*:\s*)-?\d+' % "|".join(_TIME_FIELDS))

# The root route echoes its own base URL, which carries the ephemeral port.
_PORT_RE = re.compile(r"127\.0\.0\.1:\d+")

# `lss --version` and /help/version embed the build timestamp. `--version`
# also echoes argv[0], which is where the runner chose to put the binary.
_FORMED_RE = re.compile(r"^(Formed:\s+).*$", re.M)
_BINARY_RE = re.compile(r"^(Binary:\s+).*$", re.M)
_BUILDSTAMP_RE = re.compile(
    r'("buildTimestamp"\s*:\s*)(?:"[^"]*"|-?\d+)')

# Durations the help router reports.
_UPTIME_RE = re.compile(r'("uptime"\s*:\s*)-?\d+')

# The startup banner: a boxed block whose value column is padded to a fixed
# width, so a value of a different length shifts the closing `##`. Each line
# is rewritten to a canonical shape, and the three volatile values in it
# (start time, generated server name, session token) are replaced outright.
_BANNER_VOLATILE = {"server", "token", "formed"}
_BANNER_LINE_RE = re.compile(
    r"^##\s+(\w+):\s+(.*?)\s*##$", re.M)
_BANNER_START_RE = re.compile(
    r"^##\s+libStorage starting - .*?\s*##$", re.M)
_BANNER_NAME_RE = re.compile(r"^##\s+server:\s+(\S+)\s+##$", re.M)

# logrus emits both a formatted timestamp and a millisecond field.
_LOGRUS_TIME_RE = re.compile(r'\btime="[^"]*"')
_LOGRUS_MSEC_RE = re.compile(r"\btime=\d+")
_UUID_RE = re.compile(
    r"\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b", re.I)

# The HTTP logging middleware writes an Apache common log line per request,
# and the request dump it can be asked for carries the client's own banner.
_CLF_TS_RE = re.compile(
    r"\[\d{2}/[A-Za-z]{3}/\d{4}:\d{2}:\d{2}:\d{2} [+-]\d{4}\]")
_UA_RE = re.compile(r"(?m)^(\s*User-Agent:\s*).*$")

# The response dump iterates the recorder's header map, so the block of
# `    Key=value` lines after the response banner arrives in a random order.
_RESP_BANNER_RE = re.compile(r"(?m)^.*HTTP RESPONSE \(SERVER\).*$")
_RESP_HDR_RE = re.compile(r"(?m)\A(?:[ \t]*[A-Za-z][-\w]*=[^\n]*\n)+")


def _sort_resp_headers(text):
    out, pos = [], 0
    for m in _RESP_BANNER_RE.finditer(text):
        end = m.end() + 1                          # past the banner's newline
        block = _RESP_HDR_RE.match(text, end)
        out.append(text[pos:end])
        pos = end
        if block:
            lines = sorted(block.group(0).rstrip("\n").split("\n"))
            out.append("\n".join(lines) + "\n")
            pos = block.end()
    out.append(text[pos:])
    return "".join(out)


# `lss env` prints the process environment verbatim. GOCOVERDIR is present
# only when the tree under test was built with -cover, which is a property of
# how the suite is being run and not of the tree's behaviour.
_COVERDIR_LINE_RE = re.compile(r"(?m)^GOCOVERDIR=.*\n?")

# Usage output groups flags into per-driver sections whose order comes from a
# map, so the sections are sorted before comparison. Their contents are not
# touched: only which section comes first is undetermined.
_FLAGS_SECTION_RE = re.compile(r"(?m)^[A-Za-z][\w ]* Flags$")


def _sort_flag_sections(text):
    marks = list(_FLAGS_SECTION_RE.finditer(text))
    if len(marks) < 2:
        return text
    bounds = [m.start() for m in marks] + [len(text)]
    # Trailing blank lines belong to whichever section happened to come last,
    # so they are dropped and one separator is put back between every pair.
    chunks = [text[bounds[i]:bounds[i + 1]].rstrip()
              for i in range(len(marks))]
    return text[:bounds[0]] + "\n\n".join(sorted(chunks)) + "\n"


def _canon_banner(text):
    match = _BANNER_NAME_RE.search(text)
    if match:
        text = text.replace(match.group(1), "{SERVERNAME}")

    def one(m):
        label, value = m.group(1), m.group(2)
        if label in _BANNER_VOLATILE:
            value = "{VOLATILE}"
        return "## %s: %s ##" % (label, value)

    text = _BANNER_LINE_RE.sub(one, text)
    return _BANNER_START_RE.sub("## libStorage starting - {VOLATILE} ##", text)

# Volatile response headers.
_DROP_HEADERS = {"date", "content-length"}
_MASK_HEADERS = {"libstorage-servername"}

# The same two, as they appear on the wire when a raw exchange records bytes.
_RAW_DROP_RE = re.compile(r"^(?:Date|Content-Length):[^\r\n]*\r?\n", re.M | re.I)
_RAW_MASK_RE = re.compile(r"^(Libstorage-Servername:\s*)[^\r\n]*", re.M | re.I)


# A socket error raised out of the TLS layer carries the file and line of the
# CPython `_ssl` module that raised it. That is a fact about the interpreter
# this harness happens to run on, not about the server being driven, and it
# moves whenever CPython is rebuilt; the alert name in front of it is the part
# that says what the server did.
_SSL_LINE_RE = re.compile(r" \(_ssl\.c:\d+\)")


def normalize_socket_error(text):
    """Drop the `_ssl.c:NNNN` suffix from an OSError's text, and nothing else."""
    return _SSL_LINE_RE.sub("", text)


def normalize_raw(text, env):
    """Normalize a literal HTTP response, headers included."""
    out = _RAW_DROP_RE.sub("", text)
    out = _RAW_MASK_RE.sub(r"\g<1>{SERVERNAME}", out)
    return normalize(out, env)


def normalize(text, env):
    """Replace every value that is not a function of the input."""
    if text is None:
        return None
    out = text
    for key, value in sorted(env.items(), key=lambda kv: -len(kv[1])):
        if value:
            out = out.replace(value, "{%s}" % key)
    out = _canon_banner(out)
    out = _sort_flag_sections(out)
    out = _TIME_RE.sub(r"\g<1>0", out)
    out = _UPTIME_RE.sub(r"\g<1>0", out)
    out = _PORT_RE.sub("127.0.0.1:{PORT}", out)
    out = _FORMED_RE.sub(r"\g<1>{FORMED}", out)
    out = _BINARY_RE.sub(r"\g<1>{BINARY}", out)
    out = _BUILDSTAMP_RE.sub(r"\g<1>{FORMED}", out)
    out = _LOGRUS_TIME_RE.sub('time="{TIME}"', out)
    out = _LOGRUS_MSEC_RE.sub("time={TIME}", out)
    out = _UUID_RE.sub("{UUID}", out)
    out = _COVERDIR_LINE_RE.sub("", out)
    out = _CLF_TS_RE.sub("[{TIME}]", out)
    out = _UA_RE.sub(r"\g<1>{UA}", out)
    out = _sort_resp_headers(out)
    return out


def normalize_headers(headers, env):
    out = {}
    for name, value in headers:
        low = name.lower()
        if low in _DROP_HEADERS:
            continue
        if low in _MASK_HEADERS:
            value = "{SERVERNAME}"
        out[low] = normalize(value, env)
    return out


# ---------------------------------------------------------------------------
# security tokens
# ---------------------------------------------------------------------------

# A JWT carries iat/nbf/exp, so it cannot be a literal in the suite: it has to
# be minted at request time against the clock the server is reading. The token
# never reaches an expectation -- only the server's verdict on it does.
_JWT_ALGS = {
    "HS256": hashlib.sha256,
    "HS384": hashlib.sha384,
    "HS512": hashlib.sha512,
}

_JWT_RE = re.compile(r"\{\{JWT:([^}]*)\}\}")


def _b64(raw):
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


def mint_jwt(spec):
    """Build a JWT from a `k=v,k=v` spec.

    Recognized keys: alg, key, sub, iat, nbf, exp (the three time claims are
    offsets in seconds from now), drop (comma-free list of claims to omit,
    separated by `+`), and mangle (`sig` corrupts the signature, `parts`
    removes a segment, `payload` makes the payload non-JSON).
    """
    opts = {}
    for item in spec.split(","):
        if not item:
            continue
        key, _, value = item.partition("=")
        opts[key] = value

    alg = opts.get("alg", "HS256")
    key = opts.get("key", "libstorage-test-key").encode()
    now = int(time.time())
    claims = {
        "sub": opts.get("sub", "alice"),
        "iat": now + int(opts.get("iat", "-60")),
        "nbf": now + int(opts.get("nbf", "-60")),
        "exp": now + int(opts.get("exp", "3600")),
    }
    for name in filter(None, opts.get("drop", "").split("+")):
        claims.pop(name, None)

    header = _b64(json.dumps({"alg": alg, "typ": "JWT"},
                             sort_keys=True).encode())
    payload = _b64(json.dumps(claims, sort_keys=True).encode())
    if opts.get("mangle") == "payload":
        payload = _b64(b"not-json")
    signing_input = ("%s.%s" % (header, payload)).encode()
    digest = _JWT_ALGS.get(alg.upper(), hashlib.sha256)
    sig = _b64(hmac.new(key, signing_input, digest).digest())
    if opts.get("mangle") == "sig":
        sig = _b64(b"\x00" * 32)

    token = "%s.%s.%s" % (header, payload, sig)
    if opts.get("mangle") == "parts":
        token = "%s.%s" % (header, payload)
    return token


def resolve_tokens(text):
    if text is None:
        return None
    return _JWT_RE.sub(lambda m: mint_jwt(m.group(1)), text)


# ---------------------------------------------------------------------------
# server lifecycle
# ---------------------------------------------------------------------------

def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


# The usage block, and every error that quotes it, is prefixed with argv[0] and
# then padded to argv[0]'s width -- so where the runner happened to put the
# binary changes both the text and its alignment. argv[0] is therefore pinned
# to a constant and the real path is passed out of band.
ARGV0 = "lss"


def launch(binary, args, **kwargs):
    return subprocess.Popen([ARGV0] + list(args), executable=binary, **kwargs)


# `lss env` prints the whole process environment, so an inherited environment
# makes the answer a function of the container (HOSTNAME, SHLVL, PWD, the
# toolchain's own GO* settings) instead of a function of the input. The child
# therefore gets an explicit environment and nothing else.
BASE_ENV = {
    "PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
    "LANG": "C.UTF-8",
    "TERM": "dumb",
}

# The vfs executor derives its instance ID from `utils.HostName()`, which shells
# out to `hostname` and resolves it through PATH. The answer then travels the
# wire base64-encoded inside an instance-ID header, where no textual
# normalization can reach it -- so the command itself is pinned instead, by
# putting a fixed-output `hostname` at the front of the child's PATH.
STUB_HOSTNAME = "libstorage-fixture"
_STUB_SH = "#!/bin/sh\nprintf '%s\\n'\n" % STUB_HOSTNAME


def stub_bin(scratch):
    """Create the scenario's PATH front-end and return it."""
    path = os.path.join(scratch, "bin")
    os.makedirs(path, exist_ok=True)
    stub = os.path.join(path, "hostname")
    if not os.path.exists(stub):
        with open(stub, "w") as fh:
            fh.write(_STUB_SH)
        os.chmod(stub, 0o755)
    return path


def child_env(home, scratch, extra=None, covdir=None):
    env = dict(BASE_ENV)
    env["PATH"] = "%s:%s" % (stub_bin(scratch), BASE_ENV["PATH"])
    env["HOME"] = scratch
    env["LIBSTORAGE_HOME"] = home
    env.update(extra or {})
    if covdir:
        env["GOCOVERDIR"] = covdir
    return env


# The server's own certificate fingerprint is an input to every known-host
# configuration, and it is a property of the tree under test rather than of the
# suite: the fixtures in `.tls` are regenerated whenever their validity runs
# out. It is therefore computed from the tree at run time and substituted, in
# the uppercase colon-separated form the tree's own `known_hosts` file uses.
_FPRINT_CACHE = {}


def server_fingerprint(repo):
    """SHA-256 of the DER form of `.tls/libstorage-server.crt`."""
    if repo in _FPRINT_CACHE:
        return _FPRINT_CACHE[repo]
    path = os.path.join(repo, ".tls", "libstorage-server.crt")
    try:
        with open(path, "rb") as fh:
            pem = fh.read()
    except (IOError, OSError):
        _FPRINT_CACHE[repo] = "00" * 32
        return _FPRINT_CACHE[repo]
    body = b"".join(pem.split(b"-----")[2].split())
    digest = hashlib.sha256(base64.b64decode(body)).hexdigest().upper()
    value = ":".join(digest[i:i + 2] for i in range(0, len(digest), 2))
    _FPRINT_CACHE[repo] = value
    return value


def render_config(text, repo, scratch, home, port, upport=None):
    """Resolve every placeholder a configuration may carry.

    A configuration may also carry a client credential, and a credential has
    claims about the clock, so it is minted here rather than written as a
    literal -- exactly as a request header is.
    """
    return (resolve_tokens(text)
            .replace("{{PORT}}", str(port))
            .replace("{{UPPORT}}", str(upport or 0))
            .replace("{{HOME}}", home)
            .replace("{{SCRATCH}}", scratch)
            .replace("{{TLS}}", os.path.join(repo, ".tls"))
            .replace("{{FPRINT}}", server_fingerprint(repo)))


class Server(object):
    """One `lss -c <config>` process with its own data root."""

    def __init__(self, lss, repo, config_text, scratch, extra_env=None,
                 covdir=None, tls=None, upport=None, log_level="error",
                 driver_args=None):
        self.lss = lss
        self.repo = repo
        self.scratch = scratch
        self.tls = tls
        self.log_level = log_level
        self.driver_args = driver_args
        self.port = free_port()
        self.home = os.path.join(scratch, "home")
        os.makedirs(self.home, exist_ok=True)
        # Only paths belong here. The port is handled by a pattern that
        # requires the host in front of it: a bare four- or five-digit
        # substitution would also rewrite unrelated digits in timestamps.
        self.env_map = {
            "HOME_DIR": self.home,
            "SCRATCH": scratch,
            "REPO": repo,
        }
        text = render_config(config_text, repo, scratch, self.home,
                             self.port, upport)
        # A scenario that names its services on the command line instead of
        # passing `-c` reaches a different half of the CLI, and that half
        # loads the configuration from the home directory rather than from a
        # path it was handed. The file therefore moves to where that half
        # will look for it: `$LIBSTORAGE_HOME/etc/config.yml`.
        if driver_args:
            etc = os.path.join(self.home, "etc")
            os.makedirs(etc, exist_ok=True)
            self.config = os.path.join(etc, "config.yml")
        else:
            self.config = os.path.join(scratch, "config.yml")
        with open(self.config, "w") as fh:
            fh.write(text)
        self.log = os.path.join(scratch, "lss.log")
        self.extra_env = dict(extra_env or {})
        self.covdir = covdir
        self.proc = None

    def _env(self):
        return child_env(self.home, self.scratch, self.extra_env, self.covdir)

    def start(self, timeout=60.0):
        fh = open(self.log, "wb")
        # The log level is an input: at `debug` the server narrates every
        # request through the context logger, which is a large piece of
        # behaviour that no other setting reaches.
        # `-l` is only read by the half of the CLI that has no `-c`: with a
        # config file the level comes from the file, and the flag is dead.
        # That is also the only half where the level reaches the server's own
        # construction, before any configuration has been parsed.
        if self.driver_args:
            argv = ["-l", self.log_level] + list(self.driver_args)
        else:
            argv = ["-c", self.config, "-l", self.log_level]
        self.proc = launch(
            self.lss, argv,
            stdout=fh, stderr=subprocess.STDOUT, env=self._env(),
            cwd=self.scratch, start_new_session=True)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("lss exited early; log:\n%s"
                                   % open(self.log).read()[-4000:])
            try:
                s = socket.create_connection(("127.0.0.1", self.port), 0.25)
                s.close()
                return
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("lss did not bind port %d; log:\n%s"
                           % (self.port, open(self.log).read()[-4000:]))

    def stop(self):
        if self.proc is None:
            return
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

    def read_log(self, name=None):
        """A log the server wrote, normalized.

        This is where a scenario that turns request or response logging on
        gets to see what the server thought it was handling -- including the
        bytes it decided to quote. `name` selects a file the configuration
        pointed the middleware at, relative to the scratch directory; without
        one the server's own stdout is read. A configured file is preferred:
        the fallback writer is a logrus pipe, and a pipe's reader is a
        goroutine whose interleaving with the rest of the log is not ordered.
        """
        path = self.log if name is None else os.path.join(self.scratch, name)
        if not os.path.exists(path):
            return None
        with open(path, "rb") as fh:
            raw = fh.read()
        return normalize(raw.decode("utf-8", "replace"), self.env_map)

    def _wrap_tls(self, sock, override):
        """Put the connection inside TLS, per the scenario's trust settings.

        `override` lets a single exchange present different credentials than
        the scenario default, which is how the client-certificate cases are
        expressed without standing up a second server.
        """
        opts = dict(self.tls)
        opts.update(override)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ca = opts.get("ca")
        if ca:
            ctx.load_verify_locations(os.path.join(self.repo, ca))
        else:
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
        if opts.get("cert"):
            ctx.load_cert_chain(os.path.join(self.repo, opts["cert"]),
                                os.path.join(self.repo, opts["key"]))
        return ctx.wrap_socket(
            sock, server_hostname=opts.get("servername", "libstorage-server"))

    def raw_exchange(self, ex):
        """Send literal bytes and record the literal reply.

        Malformed requests cannot be expressed through an HTTP client that
        insists on producing well-formed ones, so the wire is driven directly.
        """
        payload = resolve_tokens(ex["raw"]).encode("latin-1")
        payload = payload.replace(b"\n", b"\r\n")
        payload = payload.replace(b"{{PORT}}", str(self.port).encode())
        try:
            sock = socket.create_connection(("127.0.0.1", self.port), 10)
        except OSError as err:
            return {"connect_error": str(err)}
        sock.settimeout(ex.get("timeout", 20))
        if self.tls is not None:
            try:
                sock = self._wrap_tls(sock, ex.get("tls") or {})
            except Exception as err:
                sock.close()
                return {"handshake_error": type(err).__name__}
        chunks = []
        try:
            sock.sendall(payload)
            while True:
                buf = sock.recv(65536)
                if not buf:
                    break
                chunks.append(buf)
                if sum(len(c) for c in chunks) > 1 << 20:
                    break
        except OSError as err:
            chunks.append(b"\n<socket error: %s>"
                          % normalize_socket_error(str(err)).encode())
        finally:
            sock.close()
        text = b"".join(chunks).decode("latin-1")
        return {"raw": normalize_raw(text, self.env_map)}

    def exchange(self, ex):
        """Issue one recorded request and return its normalized result.

        An exchange may ask to be held back for a moment first. The server
        prints its startup footer a second after it begins listening, not
        before, so a scenario that empties its request list faster than that
        never reaches the code that prints it; `sleep` is how such a scenario
        stays alive long enough. The wait does not affect the answer, only
        when it is asked for.
        """
        if ex.get("sleep"):
            time.sleep(ex["sleep"])
        if ex.get("kind") == "raw":
            return self.raw_exchange(ex)
        url = "http://127.0.0.1:%d%s" % (self.port, ex["path"])
        if ex.get("query"):
            url += "?" + ex["query"]
        body = resolve_tokens(ex.get("body"))
        data = (body.encode("utf-8", "surrogateescape")
                if body is not None else None)
        req = urllib.request.Request(url, data=data,
                                     method=ex.get("method", "GET"))
        for name, value in sorted((ex.get("headers") or {}).items()):
            req.add_header(name, resolve_tokens(value))
        try:
            with urllib.request.urlopen(req, timeout=ex.get("timeout", 60)) as r:
                status = r.status
                headers = list(r.headers.items())
                payload = r.read()
        except urllib.error.HTTPError as err:
            status = err.code
            headers = list(err.headers.items())
            payload = err.read()
        except urllib.error.URLError as err:
            return {"transport_error": str(err.reason)}
        except Exception as err:
            # An input that makes the server drop the connection -- a handler
            # that panics, a response it cannot finish -- is still an input
            # with an answer, and the exchanges after it are how the scenario
            # records whether the process survived. Only the exception's type
            # is kept: its message quotes a socket the runner chose.
            return {"transport_error": type(err).__name__}
        try:
            text = payload.decode("utf-8")
        except UnicodeDecodeError:
            text = "base64:" + payload.hex()
        return {
            "status": status,
            "headers": normalize_headers(headers, self.env_map),
            "body": normalize(text, self.env_map),
        }


# ---------------------------------------------------------------------------
# scenario execution
# ---------------------------------------------------------------------------

def _seed(scratch, seed, repo=None):
    """Write a scenario's pre-existing files, relative to its scratch root.

    A seed may carry the same placeholders a configuration does, minus the
    ports: a known_hosts file has to name the fingerprint of the certificate
    the tree under test actually ships.
    """
    for rel, text in sorted((seed or {}).items()):
        dst = os.path.join(scratch, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if repo is not None:
            text = (text
                    .replace("{{SCRATCH}}", scratch)
                    .replace("{{TLS}}", os.path.join(repo, ".tls"))
                    .replace("{{FPRINT}}", server_fingerprint(repo)))
        # surrogateescape so a seed can hold bytes that are not UTF-8.
        with open(dst, "w", encoding="utf-8", errors="surrogateescape") as fh:
            fh.write(text)


def run_http_scenario(scn, configs, lss, repo, covdir=None):
    """Return {exchange id: normalized result} for one HTTP scenario.

    A scenario that names an `upstream` config gets two servers: the upstream
    one is brought up first and its port is offered to the server under test
    as `{{UPPORT}}`. That is how the project's own client -- which is a storage
    driver like any other -- is reached from the wire: a server configured with
    `driver: libstorage` answers requests by making them again against the
    upstream, so the exchanges below travel through the client stack.
    """
    scratch = tempfile.mkdtemp(prefix="lsfx-")
    results = {}
    srv = up = None
    try:
        if scn.get("upstream"):
            up_scratch = os.path.join(scratch, "upstream")
            os.makedirs(up_scratch)
            up = Server(lss, repo, configs[scn["upstream"]], up_scratch,
                        scn.get("upstream_env"), covdir,
                        scn.get("upstream_tls"),
                        log_level=scn.get("upstream_log_level", "error"))
            _seed(up_scratch, scn.get("upstream_seed"), repo)
            up.start()
        srv = Server(lss, repo, configs[scn["config"]], scratch,
                     scn.get("env"), covdir, scn.get("tls"),
                     up.port if up else None,
                     log_level=scn.get("log_level", "error"),
                     driver_args=scn.get("driver_args"))
        _seed(scratch, scn.get("seed"), repo)
        srv.start()
        for ex in scn["exchanges"]:
            if ex.get("kind") == "stop_upstream":
                # The client's failure paths are only reachable when the
                # server it was configured against goes away, so a scenario
                # may take the upstream down partway through.
                if up is not None:
                    up.stop()
                    up = None
                results[ex["id"]] = {"upstream": "stopped"}
                continue
            results[ex["id"]] = srv.exchange(ex)
        if scn.get("capture_log"):
            results["__serverlog__"] = {
                "log": srv.read_log(scn.get("log_file"))}
    except Exception as err:                      # a start failure is a result
        results.setdefault("__scenario__",
                           {"error": "%s: %s" % (type(err).__name__, err)})
    finally:
        for server in (srv, up):
            if server is not None:
                server.stop()
        shutil.rmtree(scratch, ignore_errors=True)
    return results


def run_cli_scenario(scn, configs, lss, repo, covdir=None):
    """Return {case id: normalized result} for one CLI scenario.

    A CLI scenario may also name an `upstream` config. A proxy configuration
    that the client refuses -- a certificate no known host matches, a trust
    file that is not there -- fails while `lss` is starting, so the answer is
    an exit code and a message rather than a response. Those are exactly the
    inputs an HTTP scenario cannot hold, because there is never a server to
    talk to.
    """
    scratch = tempfile.mkdtemp(prefix="lscli-")
    results = {}
    up = None
    try:
        home = os.path.join(scratch, "home")
        os.makedirs(home)
        env_map = {"HOME_DIR": home, "SCRATCH": scratch, "REPO": repo}
        if scn.get("upstream"):
            up_scratch = os.path.join(scratch, "upstream")
            os.makedirs(up_scratch)
            up = Server(lss, repo, configs[scn["upstream"]], up_scratch,
                        scn.get("upstream_env"), covdir,
                        scn.get("upstream_tls"),
                        log_level=scn.get("upstream_log_level", "error"))
            _seed(up_scratch, scn.get("upstream_seed"), repo)
            up.start()
        _seed(scratch, scn.get("seed"), repo)
        env = child_env(home, scratch, scn.get("env"), covdir)
        for case in scn["cases"]:
            argv = []
            env_map.pop("CONFIG", None)
            cfg = case.get("config")
            if cfg is not None:
                text = render_config(configs[cfg], repo, scratch, home, 0,
                                     up.port if up else None)
                path = os.path.join(scratch, "%s.yml" % case["id"])
                with open(path, "w") as fh:
                    fh.write(text)
                argv += ["-c", path]
                env_map["CONFIG"] = path
            argv += case.get("args", [])
            proc = launch(lss, argv, env=env, cwd=scratch,
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE,
                          start_new_session=True)
            try:
                out, err = proc.communicate(
                    timeout=case.get("timeout", 60))
                code = proc.returncode
            except subprocess.TimeoutExpired:
                # A config the server accepts leaves `lss` running. That is
                # itself the answer for this input; the streams so far are
                # not, since they depend on how far it got before the clock.
                try:
                    os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                except OSError:
                    pass
                proc.communicate()
                results[case["id"]] = {"exit": "still-running"}
                continue
            results[case["id"]] = {
                "exit": code,
                "stdout": normalize(out.decode("utf-8", "replace"), env_map),
                "stderr": normalize(err.decode("utf-8", "replace"), env_map),
            }
    except Exception as err:                      # a start failure is a result
        results.setdefault("__scenario__",
                           {"error": "%s: %s" % (type(err).__name__, err)})
    finally:
        if up is not None:
            up.stop()
        shutil.rmtree(scratch, ignore_errors=True)
    return results


def run_scenario(scn, configs, lss, repo, covdir=None):
    if scn.get("kind") == "cli":
        return run_cli_scenario(scn, configs, lss, repo, covdir)
    return run_http_scenario(scn, configs, lss, repo, covdir)


def load_suite(path):
    with open(path) as fh:
        suite = json.load(fh)
    return suite["configs"], suite["scenarios"]


def scored_ids(scenarios):
    """Every scored input, as `<scenario>/<case>` pairs."""
    out = []
    for scn in scenarios:
        items = scn.get("exchanges") or scn.get("cases") or []
        for item in items:
            out.append((scn["id"], item["id"]))
    return out
