#!/usr/bin/env python3
"""Drive the running server through real clients and emit normalised observations.

This one file both records the expectations (against the pristine baseline)
and replays them (inside each step's verifier), so the normalisation applied
to a candidate is by construction the same normalisation applied to the
baseline.

Normalisation is a pure function of the observation -- the request that was
made and the bytes that came back. There is no per-case "ignore this" switch,
so a case cannot be quietly weakened one at a time.

Usage:
  drive.py run     --cases C [C...] --out OBS.jsonl [--only PREFIX] [--id ID]
  drive.py stable  --a A.jsonl --b B.jsonl --out MERGED.jsonl
  drive.py compare --expect E.jsonl --actual A.jsonl [--out REPORT.json]
"""

import argparse
import base64
import contextlib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.parse
from http.client import HTTPConnection

# --------------------------------------------------------------------------
# environment
# --------------------------------------------------------------------------

SITE = "/site"
WORK = "/wk"
KEYS = "/keys"
TMPROOT = "/tmp/drive"
HTTP_PORT = 8080
SSH_PORT = 29418
SSH_PORT2 = 29419          # the second sshd.listenAddress in gerrit.config
HOST = "127.0.0.1"

# Further instances of the same WAR over further gerrit.config files. A case
# names the one that answers it with "profile"; everything else goes to the
# primary instance above.
#
#   alt   no canonicalWebUrl, an absolute basePath, a wildcard listen address,
#         a second host key, and the durations spelled in other units.
#   ssl   a canonicalWebUrl beginning "https:", which is the only thing that
#         makes WebModule install RequireSslFilter.
ALT_SITE = "/site-alt"
ALT_HTTP_PORT = 8081
ALT_SSH_PORT = 29428
SSL_SITE = "/site-ssl"
SSL_HTTP_PORT = 8082
SSL_SSH_PORT = 29438

FIXED_DATE = "2001-02-03T04:05:06+0000"

# Additive forms only, so the same client options work against the MINA SSHD
# 0.5 baseline and against a modern SSHD.
SSH_LEGACY = [
    "-o", "KexAlgorithms=+diffie-hellman-group14-sha1",
    "-o", "HostKeyAlgorithms=+ssh-rsa",
    "-o", "PubkeyAcceptedAlgorithms=+ssh-rsa",
    "-o", "Ciphers=+aes128-cbc",
    "-o", "MACs=+hmac-sha1",
]
SSH_COMMON = [
    "-o", "StrictHostKeyChecking=no",
    "-o", "UserKnownHostsFile=/dev/null",
    "-o", "GlobalKnownHostsFile=/dev/null",
    "-o", "IdentitiesOnly=yes",
    "-o", "BatchMode=yes",
    "-o", "LogLevel=ERROR",
    "-o", "ConnectTimeout=20",
]


GITCONFIG_TEXT = (
    "[user]\n\tname = Fixture Author\n\temail = fixture@example.invalid\n"
    "[init]\n\tdefaultBranch = master\n"
    "[core]\n\tabbrev = 40\n\tautocrlf = false\n\tquotePath = true\n"
    "[gc]\n\tauto = 0\n[pack]\n\tthreads = 1\n"
    "[advice]\n\tdetachedHead = false\n"
    # Pinned to v0: the 2011 baseline speaks only v0, so recording
    # with the client's modern default would freeze "server fell
    # back" rather than the server's own behaviour, and a correct
    # library upgrade would then read as a regression.
    "[protocol]\n\tversion = 0\n"
)


def git_env(extra=None, key="tester"):
    # DRIVE_GITCONFIG / DRIVE_HOME: the verifier hands the clients a config file
    # and a HOME that root prepared and the servers' user cannot write. /tmp is
    # the fallback for a recording run; in a scored run a config or an
    # ~/.ssh/config the candidate's build left in /tmp would otherwise steer
    # every git and ssh client this driver starts (an insteadOf straight to the
    # fixture repositories, a ProxyCommand around the server under test).
    cfg = os.environ.get("DRIVE_GITCONFIG") or "/tmp/drive-gitconfig"
    if not os.path.exists(cfg):
        with open(cfg, "w") as f:
            f.write(GITCONFIG_TEXT)
    ssh_cmd = " ".join(["ssh"] + SSH_LEGACY + SSH_COMMON +
                       ["-i", os.path.join(KEYS, key)])
    env = dict(os.environ)
    env.update({
        "GIT_CONFIG_GLOBAL": cfg,
        "GIT_CONFIG_SYSTEM": "/dev/null",
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_TERMINAL_PROMPT": "0",
        "GIT_ASKPASS": "/bin/true",
        "GIT_AUTHOR_DATE": FIXED_DATE,
        "GIT_COMMITTER_DATE": FIXED_DATE,
        "GIT_AUTHOR_NAME": "Fixture Author",
        "GIT_COMMITTER_NAME": "Fixture Author",
        "GIT_AUTHOR_EMAIL": "fixture@example.invalid",
        "GIT_COMMITTER_EMAIL": "fixture@example.invalid",
        "GIT_SSH_COMMAND": ssh_cmd,
        "TZ": "UTC",
        "LC_ALL": "C.UTF-8",
        "LANG": "C.UTF-8",
        "HOME": os.environ.get("DRIVE_HOME") or "/tmp",
    })
    if extra:
        env.update(extra)
    return env


# --------------------------------------------------------------------------
# byte / text normalisers
# --------------------------------------------------------------------------

def blob(data: bytes) -> dict:
    """Byte-exact record of a payload, with a readable rendering alongside."""
    r = {"len": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    try:
        s = data.decode("utf-8")
    except UnicodeDecodeError:
        r["b64"] = base64.b64encode(data).decode("ascii")
        return r
    if any(ord(c) < 32 and c not in "\n\t\r" for c in s):
        r["b64"] = base64.b64encode(data).decode("ascii")
    else:
        r["text"] = s
    return r


def is_pktline(data: bytes) -> bool:
    if len(data) < 4:
        return False
    head = data[:4]
    if not all(c in b"0123456789abcdefABCDEF" for c in head):
        return False
    n = int(head, 16)
    if n == 0:
        return True
    return 4 <= n <= len(data)


def pktline_norm(data: bytes) -> list:
    """Pkt-line stream reduced to the part the application controls.

    Everything from the first NUL of a payload is the capability list, which is
    a property of the git library version rather than of this application, so it
    is dropped. `<sha> <refname>` survives, in order, as do flush packets and
    band markers.
    """
    out = []
    i = 0
    while i < len(data):
        if i + 4 > len(data):
            out.append("TRAILING")
            break
        head = data[i:i + 4]
        if not all(c in b"0123456789abcdefABCDEF" for c in head):
            # Raw packfile bytes follow the negotiation. Their exact content is
            # a property of the git library's delta choices, so only their
            # presence is recorded; pack correctness is checked by the clone
            # cases, which compare the resulting working tree.
            out.append("NON-PKT")
            break
        n = int(head, 16)
        if n == 0:
            out.append("0000")
            i += 4
            continue
        if n < 4 or i + n > len(data):
            out.append("SHORT-PKT-%04x" % n)
            break
        payload = data[i + 4:i + n]
        i += n
        if b"\x00" in payload:
            payload = payload.split(b"\x00", 1)[0]
        s = payload.decode("utf-8", "surrogateescape")
        out.append(s.rstrip("\n").rstrip())
    return out


def scrub(s: str) -> str:
    """Remove coordinates that are ours, not the application's."""
    s = s.replace(TMPROOT, "<TMP>")
    s = s.replace(SITE, "<SITE>")
    s = s.replace(WORK, "<WORK>")
    s = re.sub(r"\b%d\b" % HTTP_PORT, "<HTTP>", s)
    s = re.sub(r"\b%d\b" % SSH_PORT, "<SSH>", s)
    s = re.sub(r"\b%d\b" % SSH_PORT2, "<SSH2>", s)
    return s


# The connection table is recognised by its shape -- a heading followed by a
# rule -- rather than by the exact heading text, so that a change to the heading
# is reported as a changed heading instead of falling out of normalisation and
# dragging the volatile clock columns back into the comparison with it.
CONN_RULE = re.compile(r"^-{40,}$")
CONN_ROW = re.compile(r"^[0-9a-f]{8} \d\d:\d\d:\d\d \d\d:\d\d:\d\d")


def text_norm(data: bytes) -> list:
    """Decode, trim, and collapse the one table whose columns are clock values."""
    s = data.decode("utf-8", "surrogateescape")
    lines = [ln.rstrip() for ln in s.split("\n")]
    while lines and lines[-1] == "":
        lines.pop()
    lines = [scrub(ln) for ln in lines]
    if len(lines) >= 2 and CONN_RULE.match(lines[1]):
        head = lines[:2]
        tail = ["--"] if lines and lines[-1] == "--" else []
        body = lines[2:len(lines) - len(tail)]
        rows = sorted({CONN_ROW.sub("<sid> <start> <idle>", r) for r in body})
        return head + rows + tail
    return lines


# stderr from a git client is mostly transfer progress, whose wording is a
# property of the git library rather than of this application.
GIT_ERR_KEEP = ("fatal:", "error:", "remote:", "ERROR:", "warning:")
GIT_ERR_DROP = (
    "Counting objects", "Compressing objects", "Receiving objects",
    "Resolving deltas", "Writing objects", "Enumerating objects",
    "Finding sources", "Getting sizes", "Unpacking objects",
    "Total ", "remote: Total ", "KiB/s", "MiB/s", "bytes/s",
    "Checking out files", "Updating files", "Checking connectivity",
    "Finding commits", "Preparing", "Searching reuse", "Building bitmaps",
)

# The same thing again, for progress meters this list does not name. A meter
# emits a line per tick of a wall-clock timer, so which of its lines survive is
# a property of how busy the host was, not of the application: the 11-ref push
# has been seen to print one line and to print eleven. Matching the shape --
# "<label>: <n>% (<done>/<total>)" -- rather than the label keeps a server-side
# meter out of the contract without having to know in advance which library
# emits one or what it calls itself. Applied identically when recording and
# when replaying, and a no-op on the baseline, whose Git library sends no
# server-side progress at all.
PROGRESS_METER = re.compile(r": +\d+% \(\d+/\d+\)")


def git_err_norm(data: bytes) -> list:
    s = data.decode("utf-8", "surrogateescape")
    segs = []
    for ln in s.replace("\r", "\n").split("\n"):
        ln = ln.rstrip()
        if not ln:
            continue
        if any(d in ln for d in GIT_ERR_DROP) or PROGRESS_METER.search(ln):
            continue
        if not ln.startswith(GIT_ERR_KEEP):
            continue
        segs.append(scrub(ln))
    return segs


def git_out_norm(data: bytes) -> list:
    s = data.decode("utf-8", "surrogateescape")
    lines = []
    for ln in s.replace("\r", "\n").split("\n"):
        ln = ln.rstrip()
        if not ln:
            continue
        if any(d in ln for d in GIT_ERR_DROP) or PROGRESS_METER.search(ln):
            continue
        lines.append(scrub(ln))
    return lines


# --------------------------------------------------------------------------
# site helpers
# --------------------------------------------------------------------------

def g(args, cwd=None, env=None, stdin_data=None, timeout=300):
    return subprocess.run(
        ["git"] + args, cwd=cwd, env=env or git_env(),
        input=stdin_data, stdin=None if stdin_data is not None else subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout,
    )


def resolve_sha(spec: str) -> str:
    repo, ref = spec.split(":", 1)
    p = g(["--git-dir", os.path.join(SITE, "repos", repo), "rev-parse", ref])
    return p.stdout.decode().strip()


SUBST = re.compile(r"\{(sha|SHA):([^}]+)\}")


def subst(s: str) -> str:
    if not isinstance(s, str):
        return s
    s = SUBST.sub(lambda m: resolve_sha(m.group(2)), s)
    return (s.replace("{HTTP}", str(HTTP_PORT))
             .replace("{SSH}", str(SSH_PORT))
             .replace("{HOST}", HOST)
             .replace("{WORK}", WORK)
             .replace("{SITE}", SITE))


def reset_repos(names):
    """Restore repositories from their pristine templates.

    A name with no template is a fixture that does not exist on the server
    (the point of the case), so there is nothing to restore.
    """
    for n in names:
        src = os.path.join(SITE, "templates", n)
        dst = os.path.join(SITE, "repos", n)
        if not os.path.isdir(src):
            continue
        shutil.rmtree(dst, ignore_errors=True)
        shutil.copytree(src, dst)


def repo_state(name) -> dict:
    d = os.path.join(SITE, "repos", name)
    refs = g(["--git-dir", d, "for-each-ref",
              "--format=%(objectname) %(refname)"])
    cnt = g(["--git-dir", d, "rev-list", "--count", "--all"])
    fsck = g(["--git-dir", d, "fsck", "--no-progress", "--no-dangling"])
    return {
        "refs": sorted(
            ln for ln in refs.stdout.decode("utf-8", "surrogateescape").split("\n") if ln
        ),
        "commits": cnt.stdout.decode().strip(),
        "fsck_ok": fsck.returncode == 0,
    }


def tree_fp(path) -> dict:
    """Fingerprint a checked-out tree: HEAD, refs, and every working file."""
    if not os.path.isdir(path):
        return {"missing": True}
    out = {}
    h = g(["-C", path, "rev-parse", "HEAD"])
    out["head"] = h.stdout.decode().strip() if h.returncode == 0 else "<none>"
    r = g(["-C", path, "for-each-ref", "--format=%(objectname) %(refname)"])
    out["refs"] = sorted(
        ln for ln in r.stdout.decode("utf-8", "surrogateescape").split("\n") if ln
    )
    files = []
    for root, dirs, names in os.walk(path):
        dirs[:] = sorted(d for d in dirs if d != ".git")
        for n in sorted(names):
            fp = os.path.join(root, n)
            rel = os.path.relpath(fp, path)
            st = os.lstat(fp)
            if os.path.islink(fp):
                files.append("%s l %s" % (rel, os.readlink(fp)))
                continue
            with open(fp, "rb") as fh:
                d = fh.read()
            mode = "x" if st.st_mode & 0o111 else "-"
            files.append("%s %s %d %s" % (rel, mode, len(d),
                                          hashlib.sha256(d).hexdigest()))
    out["files"] = sorted(files)
    return out


# --------------------------------------------------------------------------
# case runners
# --------------------------------------------------------------------------

HDR_KEEP = {
    "cache-control", "expires", "pragma", "location", "www-authenticate",
    "content-disposition", "x-frame-options", "content-encoding",
}
# `allow` is not in that set. It is written by HttpServlet.doOptions in the
# *container's* servlet-api jar, and the two handlers disagree about it inside
# one Jetty: `GET, HEAD, TRACE, OPTIONS` from the application's servlet against
# `GET,HEAD,POST,OPTIONS` from the DefaultServlet -- not even the same
# separator. Pinning that would fail a correct migration at step 5, where the
# jar becomes jakarta.servlet-api. The two OPTIONS cases keep their status and
# body; what the `notFound` binding is actually measured by is
# http.servlet.method.post, whose 405-against-404 is a status and survives the
# change of container.


def pkt_encode(items) -> bytes:
    out = b""
    for it in items:
        if it == "0000":
            out += b"0000"
            continue
        p = (subst(it) + "\n").encode("utf-8")
        out += b"%04x" % (len(p) + 4) + p
    return out


def run_http(case) -> dict:
    method = case.get("method", "GET")
    path = subst(case["path"])
    headers = {k: subst(v) for k, v in (case.get("headers") or {}).items()}
    body = None
    if "body_pkt" in case:
        body = pkt_encode(case["body_pkt"])
    elif "body_b64" in case:
        body = base64.b64decode(case["body_b64"])
    elif "body_text" in case:
        body = subst(case["body_text"]).encode("utf-8")

    # `Host` is the one header a case is allowed to take over from the client.
    # http.client adds its own unless told not to, and the result of letting it
    # is two Host headers on the wire. RFC 7230 makes that a 400; Tomcat 9
    # returns one and Jetty 9.4 silently picks a winner, so ten cases here
    # would record Jetty's tolerance of a malformed request rather than the
    # application's host handling.
    host_hdr = next((v for k, v in headers.items() if k.lower() == "host"), None)
    conn = HTTPConnection(HOST, HTTP_PORT, timeout=case.get("timeout", 120))
    try:
        conn.putrequest(method, path, skip_host=host_hdr is not None,
                        skip_accept_encoding=True)
        for k, v in headers.items():
            conn.putheader(k, v)
        if body is not None:
            conn.putheader("Content-Length", str(len(body)))
        conn.endheaders(body if body is not None else None)
        resp = conn.getresponse()
        status = resp.status
        raw = resp.getheaders()
        data = resp.read()
    except Exception as e:  # a connection-level outcome is an outcome
        return {"status": "TRANSPORT-ERROR", "error": type(e).__name__}
    finally:
        conn.close()

    hs = {}
    ct = ""
    for k, v in raw:
        lk = k.lower()
        if lk == "content-type":
            ct = v.strip().lower()
        if lk in HDR_KEEP:
            hs.setdefault(lk, []).append(v.strip())
        elif lk == "content-type" and v.strip().lower().startswith("application/x-git-"):
            # The one content type this application sets itself; every other
            # one is decided by the servlet container's mime mapping.
            hs.setdefault(lk, []).append(v.strip())
    if "location" in hs:
        # Resolved against the request URI before it is compared. A servlet that
        # passes sendRedirect a relative location leaves absolutising it to the
        # container -- Jetty 9.4 rewrites it, Tomcat 9 sends it as written -- so
        # the raw header records which container ran and the resolved one
        # records what the application asked for. Every Location the baseline
        # produces is already absolute, so this is an identity on the recording
        # and a correction on everything compared against it.
        base = "http://%s%s" % (host_hdr or "%s:%d" % (HOST, HTTP_PORT), path)
        hs["location"] = [scrub(urllib.parse.urljoin(base, x)) for x in hs["location"]]

    if isinstance(status, int) and status >= 400:
        # Rule: an error response is written by the container's error handler,
        # not by the application -- the body and the headers both. Measured on
        # this codebase: every >=400 observation carries at most
        # `cache-control` and `pragma`, and the
        # cache-control is a Jetty literal that has *replaced* the one JGit's
        # NoCacheFilter set (which is what the 2xx observations record). Keeping
        # it would pin 102 cases to the container the war happens to run in at
        # steps 1-3 and fail a correct migration at step 4 (measured: step 04
        # swaps jetty-runner for embedded Tomcat and reproduces all 428).
        return {"status": status}

    obs = {"status": status,
           "headers": {k: sorted(v) for k, v in sorted(hs.items())}}
    if ct.startswith("application/x-git-") or is_pktline(data):
        obs["body"] = {"pkt": pktline_norm(data)}
    else:
        obs["body"] = blob(data)
    return obs


def run_ssh(case) -> dict:
    user = case.get("user", "tester")
    key = os.path.join(KEYS, case.get("key", "tester"))
    port = SSH_PORT2 if case.get("port2") else SSH_PORT
    # "keys" offers several identities in order, which is how a client walks
    # into the server's maximum-authentication-attempts limit.
    ids = [os.path.join(KEYS, k) for k in case.get("keys", [])] or [key]
    # A case that pins one algorithm list spells it here rather than in
    # SSH_LEGACY, and it goes first: ssh keeps the first value it is given
    # for an option, so a pinned list overrides the additive form below
    # instead of being overridden by it.
    argv = ["ssh"]
    for o in case.get("ssh_o", []):
        argv += ["-o", o]
    argv += SSH_LEGACY + SSH_COMMON + ["-p", str(port)]
    for i in ids:
        argv += ["-i", i]
    argv.append("%s@%s" % (user, HOST))
    cmd = case.get("argv")
    if cmd is not None:
        # "--" so the local ssh never reinterprets a command string that starts
        # with a dash as its own option -- without it those cases record the ssh
        # client's usage text instead of anything the server said.
        argv.append("--")
        argv.append(subst(cmd) if isinstance(cmd, str) else " ".join(subst(a) for a in cmd))
    stdin_data = None
    if "stdin_pkt" in case:
        stdin_data = pkt_encode(case["stdin_pkt"])
    elif "stdin_b64" in case:
        stdin_data = base64.b64decode(case["stdin_b64"])
    if "stdin_tail_b64" in case:
        # Raw bytes after the pkt-lines: the pack a push is supposed to carry.
        # Kept separate from stdin_b64 so a case can spell its ref update as
        # readable pkt-lines and still hand the server a specific pack.
        stdin_data = (stdin_data or b"") + base64.b64decode(case["stdin_tail_b64"])
    if case.get("settle"):
        time.sleep(case["settle"])
    try:
        p = subprocess.run(
            argv, env=git_env(),
            input=stdin_data if stdin_data is not None else b"",
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=case.get("timeout", 120),
        )
    except subprocess.TimeoutExpired:
        return {"exit": "TIMEOUT"}
    out = p.stdout
    obs = {"exit": p.returncode, "stderr": text_norm(p.stderr)}
    obs["stdout"] = {"pkt": pktline_norm(out)} if is_pktline(out) else text_norm(out)
    if case.get("state"):
        obs["state"] = {n: repo_state(n) for n in case["state"]}
    return obs


def run_gitcase(case, tmp) -> dict:
    if case.get("seed"):
        shutil.copytree(os.path.join(WORK, case["seed"]),
                        os.path.join(tmp, "src"), symlinks=True)
    env = git_env(case.get("env"), case.get("key", "tester"))
    steps = []
    for cmd in case["cmds"]:
        argv = [subst(a) for a in cmd["argv"]]
        cwd = os.path.join(tmp, cmd.get("cwd", "."))
        os.makedirs(cwd, exist_ok=True)
        try:
            p = subprocess.run(
                ["git"] + argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=cmd.get("timeout", 300),
            )
            rc, so, se = p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired:
            rc, so, se = "TIMEOUT", b"", b""
        steps.append({"argv": cmd["argv"], "exit": rc,
                      "stdout": git_out_norm(so), "stderr": git_err_norm(se)})
        if cmd.get("stop_on_fail") and rc != 0:
            break
    obs = {"steps": steps}
    if case.get("state"):
        obs["state"] = {n: repo_state(n) for n in case["state"]}
    if case.get("tree"):
        obs["tree"] = {d: tree_fp(os.path.join(tmp, d)) for d in case["tree"]}
    return obs


RUNNERS = {"http": run_http, "ssh": run_ssh}


@contextlib.contextmanager
def profile(name):
    """Point the coordinates at one instance for the duration of a case."""
    global SITE, HTTP_PORT, SSH_PORT, SSH_PORT2
    # Each secondary profile also has a second listen address one above its
    # first, so "port2" and the port scrubbing mean the same thing everywhere.
    table = {
        "alt": (ALT_SITE, ALT_HTTP_PORT, ALT_SSH_PORT, ALT_SSH_PORT + 1),
        "ssl": (SSL_SITE, SSL_HTTP_PORT, SSL_SSH_PORT, SSL_SSH_PORT + 1),
    }
    if name not in table:
        yield
        return
    saved = (SITE, HTTP_PORT, SSH_PORT, SSH_PORT2)
    SITE, HTTP_PORT, SSH_PORT, SSH_PORT2 = table[name]
    try:
        yield
    finally:
        SITE, HTTP_PORT, SSH_PORT, SSH_PORT2 = saved


def run_case(case) -> dict:
    with profile(case.get("profile")):
        return _run_case(case)


def _run_case(case) -> dict:
    kind = case["kind"]
    # A mutating case starts from a pristine repository and leaves the server
    # exactly as it found it, so the suite has no order dependence. Any kind can
    # mutate: a push arrives over git, over ssh, or over http alike.
    if case.get("reset"):
        reset_repos(case["reset"])
    try:
        if kind != "git":
            return RUNNERS[kind](case)
        tmp = os.path.join(TMPROOT, re.sub(r"[^A-Za-z0-9._-]", "_", case["id"]))
        shutil.rmtree(tmp, ignore_errors=True)
        os.makedirs(tmp)
        try:
            return run_gitcase(case, tmp)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    finally:
        if case.get("reset"):
            reset_repos(case["reset"])


# --------------------------------------------------------------------------
# entry points
# --------------------------------------------------------------------------

def load_cases(paths):
    cases, seen = [], set()
    files = []
    for p in paths:
        if os.path.isdir(p):
            files += [os.path.join(p, f) for f in sorted(os.listdir(p))
                      if f.endswith(".jsonl")]
        else:
            files.append(p)
    for f in files:
        with open(f, encoding="utf-8") as fh:
            for lineno, line in enumerate(fh, 1):
                line = line.strip()
                if not line or line.startswith("//"):
                    continue
                c = json.loads(line)
                if c["id"] in seen:
                    raise SystemExit("duplicate case id %s (%s:%d)" % (c["id"], f, lineno))
                seen.add(c["id"])
                cases.append(c)
    return cases


def dumps(o):
    return json.dumps(o, sort_keys=True, ensure_ascii=True, separators=(",", ":"))


def cmd_run(a):
    cases = load_cases(a.cases)
    if a.only:
        cases = [c for c in cases if c["id"].startswith(tuple(a.only))]
    if a.id:
        cases = [c for c in cases if c["id"] in set(a.id)]
    os.makedirs(TMPROOT, exist_ok=True)
    n_err = 0
    with open(a.out, "w", encoding="utf-8") as out:
        for i, c in enumerate(cases, 1):
            t0 = time.time()
            try:
                obs = run_case(c)
            except Exception as e:
                obs = {"DRIVER-ERROR": "%s: %s" % (type(e).__name__, e)}
                n_err += 1
            out.write(dumps({"id": c["id"], "kind": c["kind"], "obs": obs}) + "\n")
            out.flush()
            if a.verbose:
                print("%4d/%d %-52s %5.1fs" % (i, len(cases), c["id"], time.time() - t0),
                      file=sys.stderr)
    print("ran %d cases, %d driver errors -> %s" % (len(cases), n_err, a.out),
          file=sys.stderr)
    return 0


# Stands in for a key one side does not have, so an added or removed field
# compares as a difference. A NUL keeps it out of reach of any real value.
MISSING = "\u0000absent"


def read_obs(path):
    d = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line:
                r = json.loads(line)
                d[r["id"]] = r
    return d


def cmd_stable(a):
    A, B = read_obs(a.a), read_obs(a.b)
    keep, drop = [], []
    for k in sorted(A):
        if k in B and dumps(A[k]["obs"]) == dumps(B[k]["obs"]):
            keep.append(A[k])
        else:
            drop.append(k)
    with open(a.out, "w", encoding="utf-8") as out:
        for r in keep:
            out.write(dumps(r) + "\n")
    print("stable %d / %d" % (len(keep), len(A)), file=sys.stderr)
    if drop:
        print("UNSTABLE: " + " ".join(drop), file=sys.stderr)
    return 0


def apply_must_flip(E, path):
    """Overlay this step's enumerated semantic changes onto the baseline.

    expected.jsonl is one recording, shared by all ten steps. A step that
    upgrades a library the recording went through declares here -- and only
    here -- the observations that library is documented to change. Everything
    not listed must still match the recording byte for byte.

    Each record pins the *new* observation whole, so a compatibility shim that
    restores the old behaviour fails the overlay exactly as the un-migrated
    tree does. There is no value that satisfies both.

    The file is cumulative: step k ships step k-1's records plus its own, and
    records are applied in file order, so an observation that moves twice is
    written as two records whose baselines chain.

    Refuses rather than repairs. A record whose `was` no longer matches what
    is currently in hand means the overlay and the recording have drifted
    apart, and scoring against either one alone would score the wrong thing.
    """
    if not path or not os.path.exists(path):
        return []
    flips = []
    with open(path, encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            r = json.loads(line)
            where = "%s:%d" % (path, n)
            cid = r["id"]
            if cid not in E:
                raise SystemExit("%s: no such observation %s" % (where, cid))
            was, now = r["was"], r["now"]
            if dumps(E[cid]["obs"]) != dumps(was):
                raise SystemExit("%s: %s is not at its recorded baseline"
                                 % (where, cid))
            if dumps(was) == dumps(now):
                raise SystemExit("%s: %s flips to itself" % (where, cid))
            # The record names the fields it moves. Checking that against the
            # values catches a hand-edit that changed more than its author
            # meant to, which is the only way a wrong `now` gets in here.
            moved = sorted(k for k in set(was) | set(now)
                           if dumps(was.get(k, MISSING)) != dumps(now.get(k, MISSING)))
            if moved != sorted(r.get("fields", moved)):
                raise SystemExit("%s: %s moves %s, not %s"
                                 % (where, cid, moved, r.get("fields")))
            E[cid]["obs"] = now
            flips.append(r)
    return flips


def cmd_compare(a):
    E, A = read_obs(a.expect), read_obs(a.actual)
    flips = apply_must_flip(E, getattr(a, "must_flip", None))
    results, npass = [], 0
    for k in sorted(E):
        exp = dumps(E[k]["obs"])
        if k not in A:
            results.append({"id": k, "ok": False, "why": "missing"})
            continue
        act = dumps(A[k]["obs"])
        ok = exp == act
        npass += ok
        r = {"id": k, "ok": ok}
        if not ok:
            r["expected"] = E[k]["obs"]
            r["actual"] = A[k]["obs"]
        results.append(r)
    rep = {"total": len(E), "passed": npass,
           "score": (npass / len(E)) if E else 0.0,
           "must_flipped": len(flips),
           "must_flip_causes": sorted({c for f in flips for c in f.get("cause", [])}),
           "cases": results}
    if a.out:
        with open(a.out, "w", encoding="utf-8") as fh:
            json.dump(rep, fh, indent=1, sort_keys=True)
    print("%d/%d" % (npass, len(E)))
    return 0


def main():
    global SITE, WORK, KEYS, HTTP_PORT, SSH_PORT, HOST, TMPROOT
    global ALT_SITE, SSL_SITE
    p = argparse.ArgumentParser()
    p.add_argument("--site", default=SITE)
    p.add_argument("--alt-site", default=ALT_SITE)
    p.add_argument("--ssl-site", default=SSL_SITE)
    p.add_argument("--work", default=WORK)
    p.add_argument("--keys", default=KEYS)
    p.add_argument("--tmp", default=TMPROOT)
    p.add_argument("--host", default=HOST)
    p.add_argument("--http-port", type=int, default=HTTP_PORT)
    p.add_argument("--ssh-port", type=int, default=SSH_PORT)
    sub = p.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("run")
    r.add_argument("--cases", nargs="+", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--only", nargs="*")
    r.add_argument("--id", nargs="*")
    r.add_argument("--verbose", action="store_true")
    r.set_defaults(fn=cmd_run)

    s = sub.add_parser("stable")
    s.add_argument("--a", required=True)
    s.add_argument("--b", required=True)
    s.add_argument("--out", required=True)
    s.set_defaults(fn=cmd_stable)

    c = sub.add_parser("compare")
    c.add_argument("--expect", required=True)
    c.add_argument("--must-flip", default=None)
    c.add_argument("--actual", required=True)
    c.add_argument("--out")
    c.set_defaults(fn=cmd_compare)

    a = p.parse_args()
    SITE, WORK, KEYS, TMPROOT = a.site, a.work, a.keys, a.tmp
    HOST, HTTP_PORT, SSH_PORT = a.host, a.http_port, a.ssh_port
    ALT_SITE, SSL_SITE = a.alt_site, a.ssl_site
    sys.exit(a.fn(a))


if __name__ == "__main__":
    main()
