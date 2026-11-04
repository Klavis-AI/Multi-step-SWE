#!/usr/bin/env python3
"""Drive the Postfix 3.8.6 / Dovecot 2.3.21 interface and score fixtures."""
from __future__ import annotations

import hashlib
import json
import os
import re
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path

APP = Path(os.environ.get("MAILSTACK_APP", "/app"))
SMTP_ADDR = ("127.0.0.1", 2525)
IMAP_ADDR = ("127.0.0.1", 2143)
FORBIDDEN_BIN = (
    "postfix", "dovecot", "exim", "exim4", "opendkim", "cyrus", "cyrmaster",
    "smtpd", "opensmtpd", "courier", "courierd", "stalwart", "maddy",
    "haraka", "james", "sendmail", "opendove", "imapd",
)

# Candidate code never runs as root. The CMake configure and build, both
# servers, `postfix check`/`flush`, `ldd`, and every read or write of a tree the
# candidate owns (its Maildirs, var/sieve) run as nobody:nogroup with no
# supplementary groups. test.sh has already locked /logs/verifier and /tests
# (the answer key) to root and handed /app to this uid, and it sweeps the uid
# again before it reads this script's output. Nothing the candidate runs
# inherits this script's stdout or stderr.
CAND_UID = 65534
CAND_GID = 65534
CAND = dict(user=CAND_UID, group=CAND_GID, extra_groups=[], umask=0o022,
            start_new_session=True)

# Wall-clock bounds, all inside the 1800 s verifier budget (test.sh also wraps
# this script in `timeout 1400`). No case starts after CASE_DEADLINE_SEC; a
# case that has not finished CASE_CAP_SEC after it started fails. A case that
# never ran still counts in the denominator, so stalling a server costs the
# unfinished cases and never the step's reward.json.
T0 = time.monotonic()
CASE_DEADLINE_SEC = 1240
CASE_CAP_SEC = 120
CONFIGURE_TIMEOUT_SEC = 300
BUILD_TIMEOUT_SEC = 900


def log(msg: str) -> None:
    sys.stderr.write(msg + "\n")
    sys.stderr.flush()


class CaseTimeout(Exception):
    pass


def _alarm(signum, frame):
    raise CaseTimeout(f"no result within {CASE_CAP_SEC}s")


def bounded(fn, cap: float, *args):
    """Call fn(*args); raise CaseTimeout if it has not returned after cap s."""
    signal.signal(signal.SIGALRM, _alarm)
    signal.setitimer(signal.ITIMER_REAL, cap)
    try:
        return fn(*args)
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)


def cand_env(extra: dict | None = None) -> dict:
    env = os.environ.copy()
    env.update({"HOME": "/tmp", "TMPDIR": "/tmp", "USER": "nobody", "LOGNAME": "nobody"})
    if extra:
        env.update(extra)
    return env


def _killpg(p: subprocess.Popen) -> None:
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except OSError:
        pass
    p.wait()


def run_cand(argv: list[str], timeout: float, env: dict | None = None) -> tuple[int, str]:
    """Run candidate code as CAND_UID from /app; return (rc, last 64 KiB of output).

    Output goes to an unlinked root-created file, never to a descriptor the
    scorer reads. On timeout the whole process group is killed and rc is 124.
    """
    with tempfile.TemporaryFile() as out:
        try:
            p = subprocess.Popen(argv, cwd=str(APP), env=env or cand_env(),
                                 stdin=subprocess.DEVNULL, stdout=out,
                                 stderr=subprocess.STDOUT, **CAND)
        except OSError as e:
            return 127, str(e)
        try:
            rc = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            _killpg(p)
            rc = 124
        except BaseException:
            _killpg(p)
            raise
        size = out.seek(0, 2)
        out.seek(max(0, size - 65536))
        return rc, out.read().decode("utf-8", "replace")


def cand_py(code: str, *args: str, timeout: float = 60) -> str:
    """Run a short `python3 -I -c code` as CAND_UID; return its stdout ('' on failure)."""
    try:
        r = subprocess.run([sys.executable, "-I", "-c", code, *args], cwd="/",
                           env=cand_env(), stdin=subprocess.DEVNULL,
                           capture_output=True, timeout=timeout, **CAND)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return r.stdout.decode("utf-8", "replace") if r.returncode == 0 else ""


def _cand_pids() -> list[int]:
    pids = []
    for d in Path("/proc").iterdir():
        if not d.name.isdigit():
            continue
        try:
            st = (d / "status").read_text()
        except OSError:
            continue
        uid = re.search(r"^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", st, re.M)
        state = re.search(r"^State:\s+(\S)", st, re.M)
        if state and state.group(1) in "ZX":
            continue
        if uid and str(CAND_UID) in uid.groups():
            pids.append(int(d.name))
    return pids


def sweep_cand(rounds: int = 50) -> bool:
    """Kill every live process of CAND_UID; True once none is left."""
    for _ in range(rounds):
        pids = _cand_pids()
        if not pids:
            return True
        # kill(-1) from inside the uid reaches every process it owns at once,
        # including children forked after the scan above.
        try:
            subprocess.run(
                [sys.executable, "-I", "-c", "import os, signal; os.kill(-1, signal.SIGKILL)"],
                cwd="/", env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin"},
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, timeout=10, **CAND)
        except (OSError, subprocess.TimeoutExpired):
            pass
        for pid in pids:
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
        time.sleep(0.1)
    return not _cand_pids()


# Read a candidate-owned Maildir as the candidate, so a symlink in the tree can
# only reach what the candidate could read itself (never /tests or
# /logs/verifier).
_MAILDIR_READER = r"""
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
out, budget = [], 64 << 20
try:
    entries = list(root.rglob("*"))
except OSError:
    entries = []
for f in entries:
    try:
        if not f.is_file() or "dovecot" in f.name:
            continue
        data = f.read_bytes()[: 8 << 20]
    except OSError:
        continue
    budget -= len(data)
    if budget < 0:
        break
    out.append([str(f.relative_to(root)).replace("\\", "/"), data.decode("utf-8", "replace")])
sys.stdout.write(json.dumps(out))
"""


def maildir_files(root: Path) -> list[tuple[str, str]]:
    """(path relative to root, text) for each non-dovecot file, in rglob order."""
    try:
        return [(str(a), str(b)) for a, b in json.loads(cand_py(_MAILDIR_READER, str(root)) or "[]")]
    except (ValueError, TypeError):
        return []


def tlsrpt_reports(text: str) -> list:
    """Every RFC 8460 JSON report in one stored message.

    The report is the body of the message, or one of its MIME parts, whose
    media type is application/tlsrpt+json (step 09 instruction). Its
    Content-Transfer-Encoding is undone first, so a base64 or quoted-printable
    attachment (RFC 8460 section 5.3 sends the report as an attachment) reads
    the same as a plain body. A part that is not valid JSON is not a report.
    """
    import email
    out = []
    try:
        msg = email.message_from_string(text)
        parts = list(msg.walk())
    except Exception:
        return out
    for part in parts:
        try:
            if part.get_content_type() != "application/tlsrpt+json":
                continue
            raw = part.get_payload(decode=True)
            body = raw.decode("utf-8", "replace") if isinstance(raw, bytes) else str(part.get_payload())
            out.append(json.loads(body.strip().lstrip("\ufeff")))
        except Exception:
            continue
    return out


def tlsrpt_policy_domains(report) -> set:
    """Every value of a `policy-domain` key in a parsed report (RFC 8460 4.4).

    Walked with an explicit stack, so a deeply nested report cannot raise
    RecursionError here. Compared lower-case, without a trailing dot.
    """
    found, stack = set(), [report]
    while stack:
        node = stack.pop()
        if isinstance(node, dict):
            for k, v in node.items():
                if k == "policy-domain" and isinstance(v, str):
                    found.add(v.strip().lower().rstrip("."))
                else:
                    stack.append(v)
        elif isinstance(node, list):
            stack.extend(node)
    return found


def rcpt_domain(script) -> str:
    """The domain of the first RCPT TO in a case script, lower-case."""
    for line in script or []:
        m = re.match(r"\s*RCPT\s+TO:\s*<[^>@]*@([^>]+)>", line, re.I)
        if m:
            return m.group(1).strip().lower().rstrip(".")
    return ""


_RMTREE = "import shutil, sys\nfor p in sys.argv[1:]: shutil.rmtree(p, ignore_errors=True)\n"


def recv_smtp_reply(sock: socket.socket) -> list[str]:
    buf = b""
    sock.settimeout(8)
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        buf += chunk
        text = buf.decode("utf-8", "replace")
        lines = [ln for ln in text.splitlines() if ln]
        if not lines:
            continue
        last = lines[-1]
        if len(last) >= 4 and last[3:4] == "-":
            continue
        if len(last) >= 3 and last[:3].isdigit():
            return lines
    return [ln for ln in buf.decode("utf-8", "replace").splitlines() if ln]


def recv_smtp_replies(sock: socket.socket, want: int) -> list[str]:
    """Read exactly `want` complete replies and return their 3-digit codes.

    Reply boundaries come from the continuation rule (RFC 5321 4.2.1: `NNN-`
    continues a reply, `NNN ` ends it), so a pipelined group's replies are split
    by content and never by how TCP happened to segment the stream.
    """
    codes: list[str] = []
    buf = b""
    sock.settimeout(8)
    while len(codes) < want:
        nl = buf.find(b"\n")
        if nl < 0:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
            continue
        line = buf[:nl].decode("utf-8", "replace").rstrip("\r")
        buf = buf[nl + 1:]
        if len(line) >= 4 and line[3:4] == "-":
            continue
        if len(line) >= 3 and line[:3].isdigit():
            codes.append(line[:3])
    return codes


LAST_TLS_IDENTITY = ""
LAST_TLS_SHA256 = ""


def _tls_identity_from_der(der: bytes) -> str:
    r = subprocess.run(
        ["openssl", "x509", "-inform", "der", "-noout", "-subject", "-nameopt", "RFC2253"],
        input=der,
        capture_output=True,
        timeout=5,
    )
    text = (r.stdout or b"").decode("utf-8", "replace")
    for part in text.replace("subject=", "").split(","):
        part = part.strip()
        if part.startswith("CN="):
            return part[3:]
    return text.strip()


def verify_rfc6376_dkim(msg: str, pub_pem_path: Path) -> bool:
    """Verify a simple/simple RSA-SHA256 DKIM-Signature (RFC 6376)."""
    import base64
    import hashlib
    import tempfile
    if not pub_pem_path.is_file():
        return False
    parts = msg.split("\r\n\r\n", 1)
    if len(parts) != 2:
        parts = msg.split("\n\n", 1)
    if len(parts) != 2:
        return False
    head, body = parts[0], parts[1]
    body = body.replace("\r\n", "\n").replace("\n", "\r\n")
    while body.endswith("\r\n"):
        body = body[:-2]
    body += "\r\n"
    bh = base64.b64encode(hashlib.sha256(body.encode("utf-8", "surrogateescape")).digest()).decode()
    # c=simple header canonicalization (RFC 6376 section 3.4.1): each signed
    # field is hashed exactly as it appears in the message, folding included.
    fields: list[tuple[str, str]] = []  # (lower-case name, raw field, no final CRLF)
    for line in head.replace("\r\n", "\n").split("\n"):
        if line[:1] in (" ", "\t") and fields:
            fields[-1] = (fields[-1][0], fields[-1][1] + "\r\n" + line)
        elif ":" in line:
            fields.append((line.split(":", 1)[0].strip().lower(), line))
    for dkim_raw in [raw for name, raw in fields if name == "dkim-signature"]:
        tags = {}
        for piece in dkim_raw.split(":", 1)[1].split(";"):
            if "=" in piece:
                k, v = piece.split("=", 1)
                tags[k.strip()] = re.sub(r"\s+", "", v)
        if tags.get("bh") != bh:
            continue
        # h= fields, each instance taken from the bottom up (section 5.4.2); a
        # name with no instance left contributes nothing.
        to_sign = ""
        used: dict[str, int] = {}
        for hn in (tags.get("h") or "").split(":"):
            hn = hn.strip().lower()
            if not hn:
                continue
            inst = [raw for name, raw in fields if name == hn]
            k = used.get(hn, 0)
            if k < len(inst):
                to_sign += inst[len(inst) - 1 - k] + "\r\n"
                used[hn] = k + 1
        # The signature field itself, with the b= value (and the whitespace
        # around it) emptied, and without a trailing CRLF (section 3.7).
        to_sign += re.sub(r"(?<![A-Za-z0-9_])(b[ \t]*=)[^;]*", r"\1", dkim_raw)
        try:
            sig = base64.b64decode(tags.get("b") or "")
        except Exception:
            continue
        if len(sig) < 64:
            continue
        with tempfile.TemporaryDirectory() as td:
            sp, dp, pp = Path(td) / "sig", Path(td) / "data", Path(td) / "pub.pem"
            sp.write_bytes(sig)
            dp.write_bytes(to_sign.encode("utf-8", "surrogateescape"))
            pp.write_bytes(pub_pem_path.read_bytes())
            r = subprocess.run(
                ["openssl", "dgst", "-sha256", "-verify", str(pp), "-signature", str(sp), str(dp)],
                capture_output=True, text=True, timeout=5,
            )
            if r.returncode == 0 and "Verified OK" in (r.stdout + r.stderr):
                return True
    return False


def smtp_session(script: list[str], port: int | None = None) -> tuple[str, list[str], list[list[str]]]:
    import ssl
    global LAST_TLS_IDENTITY, LAST_TLS_SHA256
    LAST_TLS_IDENTITY = ""
    LAST_TLS_SHA256 = ""
    addr = (SMTP_ADDR[0], port or SMTP_ADDR[1])
    s = socket.create_connection(addr, 8)
    try:
        banner = recv_smtp_reply(s)
        banner_code = banner[-1][:3] if banner else ""
        codes = []
        replies: list[list[str]] = []
        for step in script:
            raw = step.encode("utf-8", "surrogateescape")
            if raw.upper().startswith(b"BDAT "):
                # command line then payload; payload may follow the first CRLF
                nl = raw.find(b"\n")
                if nl >= 0:
                    s.sendall(raw)
                else:
                    s.sendall(raw if raw.endswith(b"\r\n") else raw + b"\r\n")
            else:
                if not raw.endswith(b"\n"):
                    raw += b"\r\n"
                s.sendall(raw)
            r = recv_smtp_reply(s)
            replies.append(r)
            codes.append(r[-1][:3] if r else "")
            if step.upper().lstrip().startswith("STARTTLS") and codes[-1] == "220":
                ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
                ctx.check_hostname = False
                ctx.verify_mode = ssl.CERT_NONE
                s = ctx.wrap_socket(s, server_hostname="mail.example.test")
                der = s.getpeercert(binary_form=True) or b""
                LAST_TLS_IDENTITY = _tls_identity_from_der(der) if der else ""
                if der:
                    import hashlib
                    LAST_TLS_SHA256 = hashlib.sha256(der).hexdigest()
        return banner_code, codes, replies
    finally:
        try:
            s.close()
        except OSError:
            pass


def recv_until(sock: socket.socket, pred) -> str:
    buf = b""
    sock.settimeout(8)
    while True:
        try:
            chunk = sock.recv(8192)
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk
        text = buf.decode("utf-8", "replace")
        if pred(text):
            return text
    return buf.decode("utf-8", "replace")


def tag_status(text: str, tag: str) -> str:
    for line in text.splitlines():
        if line.startswith(tag + " "):
            parts = line.split()
            if len(parts) >= 2:
                return parts[1].upper()
    if "+ " in text or text.startswith("+"):
        return "+"
    return ""


def elf_needed(path: Path) -> list[str]:
    r = subprocess.run(["readelf", "-d", str(path)], capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        # ldd may execute the file's own loader, so it runs as the candidate.
        _, out = run_cand(["ldd", str(path)], timeout=60)
        return re.findall(r"libstdc\+\+|libc\+\+", out)
    return re.findall(r"Shared library: \[([^\]]+)\]", r.stdout)


def is_cxx_elf(path: Path) -> bool:
    needed = " ".join(elf_needed(path))
    return "libstdc++" in needed or "libc++" in needed


STOCK_MARKERS = (
    "Postfix built for",
    "dovecot-core",
    "/usr/lib/postfix",
    "Dovecot v2.",
    "Exim version",
    "OpenSMTPD",
    "Cyrus IMAP",
)

_SRC_TEXT: str | None = None


def app_source_text() -> str:
    """Everything the candidate authored under /app, concatenated as text.

    Build output, the shipped RFC corpus under SPEC/, runtime state under var/,
    the built binaries in sbin/ and any other ELF are skipped, so what is left
    is what the candidate wrote: sources, headers, CMake files and the etc/
    configuration it ships. Computed once.
    """
    global _SRC_TEXT
    if _SRC_TEXT is not None:
        return _SRC_TEXT
    skip = {"build", "SPEC", "sbin", "var", ".git", "__pycache__", "node_modules"}
    parts: list[str] = []
    budget = 64 * 1024 * 1024
    try:
        entries = sorted(APP.rglob("*"))
    except OSError:
        entries = []
    for p in entries:
        try:
            rel = p.relative_to(APP).parts
        except ValueError:
            continue
        if any(d in skip for d in rel[:-1]):
            continue
        if p.is_symlink() or not p.is_file():
            continue
        try:
            if p.stat().st_size > 4 * 1024 * 1024:
                continue
            data = p.read_bytes()
        except OSError:
            continue
        if data[:4] == b"\x7fELF":
            continue
        budget -= len(data)
        if budget < 0:
            break
        parts.append(data.decode("utf-8", "replace"))
    _SRC_TEXT = "\n".join(parts)
    return _SRC_TEXT


def stock_mta_fingerprint(path: Path) -> str:
    """Reason if `path` carries stock-MTA fingerprints it did not write itself.

    These instructions require behaviour "matching Postfix 3.8.6" and "matching
    Dovecot 2.3.21" down to the banners and log lines, so a stock product string
    inside the candidate binary is expected, not evidence of wrapping: a
    from-scratch implementation that logs
    `dovecot: master: Dovecot v2.3.21 starting up on ...` puts one there itself.
    This package also links one executable and installs it as BOTH sbin/postfix
    and sbin/dovecot, so any IMAP-side literal is necessarily present in the
    SMTP binary too. A bare substring scan therefore cannot separate "this IS a
    stock binary" from "this binary MENTIONS a stock banner".

    A marker only counts when it is in the binary and nowhere in the candidate's
    own sources: then it was linked in from somewhere else, which is what an
    embedded or appended stock build looks like. The load-bearing anti-wrap
    checks remain is_elf / is_cxx_elf (a stock Postfix or Dovecot is a C binary
    with no libstdc++ dependency), the listener check (stack_violation: every
    process that holds a scored port's listening socket must itself be such a
    binary under /app) and byte-identity with a system binary.
    """
    r = subprocess.run(["strings", "-n", "8", str(path)], capture_output=True, text=True,
                       timeout=120)
    return _unexplained_markers(r.stdout)


def _unexplained_markers(blob: str) -> str:
    """The stock_mta_fingerprint verdict for the `strings` output `blob`."""
    hits = [s for s in STOCK_MARKERS if s in blob]
    if not hits:
        return ""
    src = app_source_text()
    unexplained = [s for s in hits if s not in src]
    if not unexplained:
        return ""
    return "carries stock-MTA fingerprints absent from its own sources: " + ", ".join(
        repr(s) for s in unexplained
    )


def wrap_violation() -> str:
    """Return a reason if the candidate wraps a stock MTA, else ''."""
    for name in ("postfix", "dovecot"):
        p = APP / "sbin" / name
        if not p.exists():
            return f"missing {p}"
        if p.is_symlink():
            target = str(p.resolve())
            if target.startswith("/usr/") or any(b in target for b in FORBIDDEN_BIN if b != name):
                return f"{p} symlink to {target}"
        if not is_elf(p):
            txt = p.read_text(errors="replace") if p.is_file() else ""
            if re.search(
                r"/usr/(s)?bin/(postfix|dovecot|exim|sendmail|cyrus|opensmtpd)", txt
            ):
                return f"{p} wraps a system MTA"
            return f"{p} is not a C++ ELF"
        if not is_cxx_elf(p):
            return f"{p} is not linked as C++ (no libstdc++/libc++)"
        stock = stock_mta_fingerprint(p)
        if stock:
            return f"{p} {stock}"
        # identical to a system binary if present
        for sysp in (Path("/usr/sbin") / name, Path("/usr/lib/postfix/sbin") / name):
            if sysp.exists():
                try:
                    if p.read_bytes() == sysp.read_bytes():
                        return f"{p} is a byte-for-byte copy of {sysp}"
                except OSError:
                    pass
    cpp = list(APP.rglob("*.cpp")) + list(APP.rglob("*.cc")) + list(APP.rglob("*.cxx"))
    cpp = [p for p in cpp if "build" not in p.parts]
    if not cpp:
        return "no C++ sources under /app"
    return ""


def _lit_n(line: str) -> int:
    """Octet count of a literal that ends this command line, or -1."""
    m = re.search(r"\{(\d+)\+?\}\s*$", line.rstrip("\r\n"))
    return int(m.group(1)) if m else -1


def _send_literal(s: socket.socket, payload: str, n: int) -> None:
    """Send a literal's octets. When they are all that is left of the command
    (exactly n octets), end the command line with the CRLF that RFC 3501 and
    RFC 5804 require after the literal."""
    data = payload.encode("utf-8", "surrogateescape")
    s.sendall(data)
    if n >= 0 and len(data) == n:
        s.sendall(b"\r\n")


# JMAP requests authenticate as bob (RFC 8620 section 1.7: every request is
# authenticated) with HTTP Basic (RFC 7617), and use the session's primary
# mail account, else bob's account, else the first one listed.
JMAP_AUTH = "Authorization: Basic Ym9iOmJvYi1zZWNyZXQ=\r\n"


def jmap_account(sess: dict, default: str = "u1") -> str:
    accts = sess.get("accounts") or {}
    prim = (sess.get("primaryAccounts") or {}).get("urn:ietf:params:jmap:mail")
    if isinstance(prim, str) and prim and (not accts or prim in accts):
        return prim
    for aid, a in accts.items():
        if isinstance(a, dict) and str(a.get("name") or "").split("@")[0].lower() == "bob":
            return aid
    return next(iter(accts), default)


def imap_session(script: list[str]) -> tuple[list[str], str]:
    s = socket.create_connection(IMAP_ADDR, 8)
    try:
        greet = recv_until(s, lambda t: "\n" in t)
        transcript = greet
        statuses: list[str] = []
        i = 0
        while i < len(script):
            send = script[i]
            is_tagged = bool(re.match(r"^[A-Za-z0-9]+ ", send))
            if not is_tagged:
                i += 1
                continue
            tag = send.split()[0]
            s.sendall(send.encode("utf-8", "surrogateescape"))
            cmdword = send.split()[1].upper() if len(send.split()) > 1 else ""
            if cmdword == "IDLE":
                text = recv_until(s, lambda t: t.lstrip().startswith("+") or "+ " in t)
                transcript += text
                if i + 1 < len(script) and not re.match(r"^[A-Za-z0-9]+ ", script[i + 1]):
                    s.sendall(script[i + 1].encode("utf-8", "surrogateescape"))
                    i += 1
            plus_lit = bool(re.search(r"\{\d+\+\}\s*$", send.rstrip("\r\n")))
            if plus_lit:
                # RFC 2088: non-synchronizing literal — send the payload immediately.
                if i + 1 < len(script) and not re.match(r"^[A-Za-z0-9]+ ", script[i + 1]):
                    _send_literal(s, script[i + 1], _lit_n(send))
                    i += 1
            elif "{" in send and send.rstrip().endswith("}"):
                text = recv_until(
                    s,
                    lambda t: t.lstrip().startswith("+") or any(
                        ln.startswith(tag + " ") for ln in t.splitlines()),
                )
                transcript += text
                if i + 1 < len(script) and not re.match(r"^[A-Za-z0-9]+ ", script[i + 1]):
                    _send_literal(s, script[i + 1], _lit_n(send))
                    i += 1
            text = recv_until(
                s,
                lambda t, tag=tag: any(ln.startswith(tag + " ") for ln in t.splitlines()),
            )
            transcript += text
            statuses.append(tag_status(text, tag))
            i += 1
        return statuses, transcript
    finally:
        try:
            s.close()
        except OSError:
            pass


def wait_port(addr, timeout=20.0) -> bool:
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            s = socket.create_connection(addr, 0.5)
            s.close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


def build() -> bool:
    if not (APP / "CMakeLists.txt").exists():
        log("no CMakeLists.txt")
        return False
    # Runs before any candidate process exists in this container, so root
    # rmtree cannot race a swap; rmtree also refuses a symlinked build/.
    import shutil
    shutil.rmtree(APP / "build", ignore_errors=True)
    rc, out = run_cand(
        ["cmake", "-S", str(APP), "-B", str(APP / "build"), "-DCMAKE_BUILD_TYPE=Release"],
        timeout=CONFIGURE_TIMEOUT_SEC)
    if rc != 0:
        log(f"cmake configure failed rc={rc}\n" + out)
        return False
    rc, out = run_cand(
        ["cmake", "--build", str(APP / "build"), "-j", str(os.cpu_count() or 2)],
        timeout=BUILD_TIMEOUT_SEC)
    if rc != 0:
        log(f"cmake build failed rc={rc}\n" + out)
        return False
    return (APP / "sbin/postfix").exists() and (APP / "sbin/dovecot").exists()


def is_elf(path: Path) -> bool:
    try:
        with path.open("rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


# The ports the stack serves: SMTP 2525 and submission 2587 (sbin/postfix),
# IMAP 2143, ManageSieve 24190 and HTTP 8080 (sbin/dovecot). The verifier's own
# sinks listen on 2526-2529 and are not among them.
SCORED_PORTS = (2525, 2143, 2587, 24190, 8080)
# Bounds on what root reads while it resolves a socket to the processes that
# hold it. A socket table larger than this, or one descriptor table larger than
# this, is not a stack this task asks for, and a listener that cannot be
# resolved does not pass.
PROC_TABLE_CAP = 64 << 20
FD_SCAN_CAP = 1 << 16


def _read_proc(path: str, cap: int) -> bytes:
    """At most `cap` bytes of a /proc file, read as root without following a
    link in its last component and without waiting on it."""
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        parts, left = [], cap
        while left > 0:
            chunk = os.read(fd, min(left, 1 << 20))
            if not chunk:
                break
            parts.append(chunk)
            left -= len(chunk)
        return b"".join(parts)
    finally:
        os.close(fd)


def _socket_inodes(port: int, states: tuple[str, ...]) -> set[str]:
    """Inodes of the IPv4 and IPv6 TCP sockets whose local port is `port` and
    whose state is one of `states` ("0A" listening, "01" established)."""
    found: set[str] = set()
    for table in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            text = _read_proc(table, PROC_TABLE_CAP).decode("ascii", "replace")
        except OSError:
            continue
        for ln in text.splitlines()[1:]:
            parts = ln.split()
            if len(parts) < 10 or parts[3] not in states or parts[9] == "0":
                continue
            try:
                if int(parts[1].rsplit(":", 1)[1], 16) == port:
                    found.add(parts[9])
            except (IndexError, ValueError):
                continue
    return found


class _CandidateFsCreds:
    """This thread's filesystem credentials set to CAND_UID/CAND_GID, and only
    those. The container gives root no CAP_SYS_PTRACE, and without it the
    links under /proc/<pid>/fd and /proc/<pid>/exe of a process of another uid
    cannot be read; with the owner's filesystem credentials they can, provided
    the process is dumpable. The real and effective ids stay root's, and
    root's filesystem credentials are restored, and checked, on the way out."""
    _libc = None

    def __enter__(self):
        if _CandidateFsCreds._libc is None:
            import ctypes
            _CandidateFsCreds._libc = ctypes.CDLL("libc.so.6", use_errno=True)
        _CandidateFsCreds._libc.setfsgid(CAND_GID)
        _CandidateFsCreds._libc.setfsuid(CAND_UID)
        return self

    def __exit__(self, *exc):
        libc = _CandidateFsCreds._libc
        libc.setfsuid(0)
        libc.setfsgid(0)
        # setfsuid/setfsgid return the current value when handed an invalid id.
        if libc.setfsuid(-1) != 0 or libc.setfsgid(-1) != 0:
            raise RuntimeError("could not restore root's filesystem credentials")
        return False


def _socket_holders(inodes: set[str]) -> tuple[set[int], str]:
    """Every process of the candidate's uid that has a descriptor on one of
    these sockets -- not just the first one found, and through every thread's
    descriptor table -- and a reason if that cannot be established: a process
    of that uid whose descriptors cannot be read (a non-dumpable one hides
    them) could be holding the socket. Only readlink() is used on the
    descriptors, so nothing the candidate points them at is opened."""
    want = {f"socket:[{i}]" for i in inodes}
    pids: set[int] = set()
    if not want:
        return pids, ""
    with _CandidateFsCreds():
        for pid in _cand_pids():
            try:
                tables = [f"/proc/{pid}/fd"] + [
                    f"/proc/{pid}/task/{t.name}/fd"
                    for t in os.scandir(f"/proc/{pid}/task") if t.name.isdigit()]
            except (FileNotFoundError, ProcessLookupError):
                continue
            except OSError as e:
                if _gone(pid):
                    continue
                return pids, f"process {pid} cannot be inspected ({e.strerror})"
            held = False
            for table in tables:
                try:
                    with os.scandir(table) as it:
                        for n, e in enumerate(it):
                            if n >= FD_SCAN_CAP:
                                return pids, f"process {pid} holds more than {FD_SCAN_CAP} descriptors"
                            try:
                                if os.readlink(e.path) in want:
                                    held = True
                                    break
                            except FileNotFoundError:
                                continue
                except (FileNotFoundError, ProcessLookupError):
                    continue
                except OSError as e:
                    if _gone(pid):
                        break
                    return pids, f"the descriptors of process {pid} cannot be inspected ({e.strerror})"
                if held:
                    break
            if held:
                pids.add(pid)
    return pids, ""


_EXE_VERDICT: dict[tuple[int, int, int, int, int], str] = {}

# A holder that exits between being listed and being examined -- a prefork
# worker recycled mid-check -- no longer holds anything: it is skipped, not
# failed, and stack_violation re-lists the holders if none is left.
GONE = "\0gone"


def _gone(pid: int) -> bool:
    """True if `pid` has exited (no /proc entry, or a zombie)."""
    try:
        with open(f"/proc/{pid}/stat", "rb") as fh:
            st = fh.read(4096)
    except OSError:
        return True
    tail = st.rsplit(b")", 1)[-1].split()
    return not tail or tail[0] in (b"Z", b"X")


def _exe_content_verdict(fd: int) -> str:
    """The checks wrap_violation makes on sbin/*, on an open executable."""
    if os.pread(fd, 4, 0) != b"\x7fELF":
        return "is not an ELF executable"
    path = f"/dev/fd/{fd}"
    r = subprocess.run(["readelf", "-d", path], pass_fds=(fd,), capture_output=True,
                       text=True, timeout=60)
    needed = " ".join(re.findall(r"Shared library: \[([^\]]+)\]", r.stdout)) if r.returncode == 0 else ""
    if "libstdc++" not in needed and "libc++" not in needed:
        return "is not linked as C++ (no libstdc++/libc++)"
    r = subprocess.run(["strings", "-n", "8", path], pass_fds=(fd,), capture_output=True,
                       text=True, timeout=120)
    return _unexplained_markers(r.stdout)


def _exe_violation(pid: int) -> str:
    """'' if process `pid` runs an ELF from under /app that is linked as C++
    and carries no stock-server fingerprint; otherwise why not. The executable
    is opened through /proc/<pid>/exe, which is the file the process is
    running whatever has happened to its path since, and examined through that
    descriptor."""
    link = f"/proc/{pid}/exe"
    try:
        with _CandidateFsCreds():
            exe = os.readlink(link)
            fd = os.open(link, os.O_RDONLY | os.O_NONBLOCK)
    except OSError as e:
        if _gone(pid):
            return GONE
        return f"process {pid}: its executable cannot be resolved ({e.strerror})"
    try:
        st = os.fstat(fd)
        if exe.endswith(" (deleted)"):
            return f"process {pid} runs {exe}"
        app = str(APP).rstrip("/")
        if not exe.startswith(app + "/"):
            return f"process {pid} runs {exe}, which is not under {app}"
        if not stat.S_ISREG(st.st_mode):
            return f"process {pid} runs {exe}, which is not a regular file"
        key = (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns)
        if key not in _EXE_VERDICT:
            _EXE_VERDICT[key] = _exe_content_verdict(fd)
        why = _EXE_VERDICT[key]
    finally:
        os.close(fd)
    return f"process {pid} runs {exe}, which {why}" if why else ""


def stack_violation(required: tuple[int, ...], probe: bool) -> str:
    """'' if every process holding a listening socket on a scored port passes
    the rule the instruction states for the stack's binaries (an ELF built from
    the C++ under /app, linked against the C++ standard library, not a stock
    server); otherwise the first reason. A port in `required` must have a
    resolvable listener: it answered, so some process holds it; and on any port
    a listening socket whose holder cannot be found does not pass. With `probe`, a
    connection is also opened to each scored port and the processes serving it
    on the server side are held to the same rule, so a C++ acceptor that hands
    the connection to a stock server is caught as well."""
    for port in SCORED_PORTS:
        for attempt in range(3):
            inodes = _socket_inodes(port, ("0A",))
            holders, why = _socket_holders(inodes)
            if why:
                return f"port {port}: {why}"
            live = 0
            for pid in sorted(holders):
                why = _exe_violation(pid)
                if why == GONE:
                    continue
                if why:
                    return f"port {port} listener: {why}"
                live += 1
            if live or (not inodes and port not in required):
                break
            time.sleep(0.5)
        if not live:
            if inodes:
                return f"port {port}: no process holding its listening socket can be found"
            if port in required:
                return f"port {port} answered, but no socket is listening on it"
            continue
    if not probe:
        return ""
    socks = []
    try:
        for port in SCORED_PORTS:
            try:
                s = socket.create_connection(("127.0.0.1", port), 3)
            except OSError:
                continue
            socks.append(s)
            s.settimeout(3)
            try:
                s.recv(1, socket.MSG_PEEK)
            except OSError:
                pass
        for port in SCORED_PORTS:
            holders, why = _socket_holders(_socket_inodes(port, ("01",)))
            if why:
                return f"port {port}: {why}"
            for pid in sorted(holders):
                why = _exe_violation(pid)
                if why and why != GONE:
                    return f"port {port} connection served by {why}"
    finally:
        for s in socks:
            try:
                s.close()
            except OSError:
                pass
    return ""


_MKDIRS = r"""
import sys
from pathlib import Path
for d in sys.argv[1:]:
    try:
        Path(d).mkdir(parents=True, exist_ok=True)
        for sub in ("tmp", "new", "cur"):
            if str(d).endswith("Maildir"):
                Path(d, sub).mkdir(parents=True, exist_ok=True)
    except OSError:
        pass
"""


def start_servers():
    env = cand_env()
    env["SOURCE_DATE_EPOCH"] = env.get("SOURCE_DATE_EPOCH", "1700000000")
    # Created by the candidate uid, inside the tree it owns.
    cand_py(_MKDIRS, *[str(d) for d in (
        APP / "var/spool/postfix",
        APP / "var/lib/postfix",
        APP / "var/run/dovecot",
        APP / "var/log",
        APP / "var/mail/alice/Maildir",
        APP / "var/mail/bob/Maildir",
        APP / "var/mail/carol/Maildir",
        APP / "var/mail/tlsrpt/Maildir",
        APP / "var/mail/dmarc-rua/Maildir",
        APP / "var/mail/mailer-daemon/Maildir",
    )])
    conf = str(APP / "etc/postfix")
    dconf = str(APP / "etc/dovecot/dovecot.conf")
    rc, out = run_cand([str(APP / "sbin/postfix"), "-c", conf, "check"], timeout=60)
    if rc != 0:
        log(f"postfix check rc={rc}\n" + out[-2000:])
    pf = subprocess.Popen(
        [str(APP / "sbin/postfix"), "-c", conf, "start-fg"], cwd=str(APP),
        env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, **CAND)
    dv = subprocess.Popen(
        [str(APP / "sbin/dovecot"), "-F", "-c", dconf], cwd=str(APP),
        env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, **CAND)
    return pf, dv


def stop_servers(procs):
    for p in procs:
        if p and p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                p.kill()
                try:
                    p.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    pass


def fixture_digests(paths: list[Path]) -> dict[str, str]:
    """md5 of every fixture file this run actually loaded.

    A verifier image is content-addressed by its tests/ tree, but the bytes that
    reach the image travel through the container build context, whose local
    cache is keyed on (relative path, size, mode, mtime) under a shared key
    derived from the context directory's basename -- "tests" for every step. Two
    steps whose same-named fixture has the same size and mtime therefore share
    one cached copy. Recording what was graded makes that visible in the
    artifact instead of silently grading the wrong expectations.
    """
    out: dict[str, str] = {}
    for p in paths:
        if p.is_dir():
            files = sorted(p.glob("*.json"))
        elif p.exists():
            files = [p]
        else:
            files = []
        for f in files:
            try:
                out[f"{f.parent.name}/{f.name}"] = hashlib.md5(f.read_bytes()).hexdigest()
            except OSError:
                pass
    return out


# The fields that only name or describe a case; everything else is read by
# run_case. Two cases with the same key are the same check, and test.sh counts a
# step's own case together with the earlier cases when an earlier group (or the
# held-out set) already has it unchanged.
CASE_META = ("id", "provenance", "stratum", "gate")


def case_key(case: dict) -> str:
    body = {k: v for k, v in case.items() if k not in CASE_META}
    return hashlib.sha256(json.dumps(body, sort_keys=True, separators=(",", ":"))
                          .encode("utf-8")).hexdigest()


def load_cases(paths: list[Path]) -> list[tuple[str, dict]]:
    """(fixture group, case) for every case, in load order. The group is the
    fixture directory's name (m01 ... m10, heldout); test.sh scores this
    step's own group and the rest as two separate factors."""
    out = []
    for p in paths:
        if p.is_dir():
            for f in sorted(p.glob("*.json")):
                out.extend((p.name, c) for c in json.loads(f.read_text()))
        elif p.exists():
            out.extend((p.parent.name, c) for c in json.loads(p.read_text()))
    return out


_RESET_MAILDIRS = r"""
import shutil, sys
from pathlib import Path
APP = Path(sys.argv[1])
for u in ("alice", "bob", "carol"):
    root = APP / "var/mail" / u / "Maildir"
    for sub in ("tmp", "new", "cur"):
        d = root / sub
        d.mkdir(parents=True, exist_ok=True)
        for f in d.iterdir():
            try:
                f.unlink()
            except OSError:
                pass
    for leftover in root.glob("dovecot*"):
        try:
            leftover.unlink()
        except OSError:
            pass
    for child in root.iterdir():
        if child.name.startswith(".") and child.is_dir():
            shutil.rmtree(child, ignore_errors=True)
sys.stdout.write("ok")
"""


def reset_maildirs() -> None:
    # Deletes inside the candidate's tree, so it runs as the candidate: a
    # symlinked Maildir can then only lead to files the candidate could delete.
    # A failure fails the case, as the in-process version's OSError did.
    if cand_py(_RESET_MAILDIRS, str(APP)) != "ok":
        raise RuntimeError("reset_maildirs failed")


def sieve_codes(script: list[str], port: int = 24190) -> list[str]:
    s = socket.create_connection(("127.0.0.1", port), 8)
    try:
        recv_until(s, lambda t: "\nOK" in t or t.rstrip().endswith("OK") or "OK \"" in t)
        codes: list[str] = []
        i = 0
        while i < len(script):
            send = script[i]
            s.sendall(send.encode("utf-8", "surrogateescape"))
            if "{" in send and send.rstrip().endswith("}"):
                # RFC 5804 client literals are {n+}: the octets follow at once.
                if "+}" not in send:
                    recv_until(s, lambda t: t.lstrip().startswith("+") or "OK" in t or "NO" in t)
                if i + 1 < len(script) and not re.match(r"^[A-Z]", script[i + 1]):
                    _send_literal(s, script[i + 1], _lit_n(send))
                    i += 1
            text = recv_until(
                s,
                lambda t: any(ln.startswith("OK") or ln.startswith("NO") or ln.startswith("BYE")
                              for ln in t.replace("\r", "").split("\n") if ln),
            )
            st = ""  # no OK/NO/BYE line before the timeout is not a reply
            for ln in text.replace("\r", "").split("\n"):
                if ln.startswith("OK"):
                    st = "OK"
                elif ln.startswith("NO"):
                    st = "NO"
                elif ln.startswith("BYE"):
                    st = "BYE"
            codes.append(st)
            i += 1
        return codes
    finally:
        try:
            s.close()
        except OSError:
            pass


def run_case(case: dict) -> bool:
    kind = case["kind"]
    try:
        if kind == "smtp_scram":
            import base64, hashlib, hmac, ssl as _ssl
            def b64(x: bytes) -> str:
                return base64.b64encode(x).decode()
            def b64d(x: str) -> bytes:
                return base64.b64decode(x + "==")
            user = case.get("user") or "alice"
            password = case.get("password") or "alice-secret"
            addr = (SMTP_ADDR[0], int(case.get("port") or SMTP_ADDR[1]))
            s = socket.create_connection(addr, 8)
            try:
                recv_smtp_reply(s)
                s.sendall(b"EHLO c.test\r\n")
                ehlo = recv_smtp_reply(s)
                if "SCRAM-SHA-1" not in "\n".join(ehlo):
                    log(f"FAIL {case['id']} EHLO missing SCRAM-SHA-1")
                    return False
                cnonce = "rOprNGfwEbeRWgbNEkqO"
                first = f"n,,n={user},r={cnonce}"
                s.sendall(("AUTH SCRAM-SHA-1 " + b64(first.encode()) + "\r\n").encode())
                ch = recv_smtp_reply(s)
                if not ch or not ch[-1].startswith("334"):
                    log(f"FAIL {case['id']} no 334 got={ch}")
                    return False
                sf = b64d(ch[-1][4:].strip()).decode()
                # r=,s=,i=
                parts = dict(p.split("=", 1) for p in sf.split(",") if "=" in p)
                salted = hashlib.pbkdf2_hmac("sha1", password.encode(), b64d(parts["s"]), int(parts["i"]))
                client_key = hmac.new(salted, b"Client Key", hashlib.sha1).digest()
                stored = hashlib.sha1(client_key).digest()
                without_p = f"c=biws,r={parts['r']}"
                authmsg = f"n={user},r={cnonce},{sf},{without_p}"
                csig = hmac.new(stored, authmsg.encode(), hashlib.sha1).digest()
                proof = bytes(a ^ b for a, b in zip(client_key, csig))
                # This first session only establishes that EHLO advertises
                # SCRAM-SHA-1 and that AUTH is answered with a 334 challenge.
                # Its client-final message base64-encodes the proof twice and
                # its reply is not read; the exchange is repeated below with a
                # correctly encoded c=biws,r=...,p=b64(proof).
                s.sendall((b64(without_p.encode() + b",p=" + base64.b64encode(proof)) + "\r\n").encode())
            finally:
                try:
                    s.close()
                except OSError:
                    pass
            # Second session: the complete SCRAM exchange that is scored.
            s = socket.create_connection(addr, 8)
            try:
                recv_smtp_reply(s)
                s.sendall(b"EHLO c.test\r\n")
                recv_smtp_reply(s)
                first = f"n,,n={user},r={cnonce}"
                s.sendall(("AUTH SCRAM-SHA-1 " + b64(first.encode()) + "\r\n").encode())
                ch = recv_smtp_reply(s)
                if not ch or not ch[-1].startswith("334"):
                    log(f"FAIL {case['id']} no 334 got={ch}")
                    return False
                payload = ch[-1][4:].strip()
                sf = b64d(payload).decode()
                parts = dict(p.split("=", 1) for p in sf.split(",") if "=" in p)
                salted = hashlib.pbkdf2_hmac("sha1", password.encode(), b64d(parts["s"]), int(parts["i"]))
                client_key = hmac.new(salted, b"Client Key", hashlib.sha1).digest()
                stored = hashlib.sha1(client_key).digest()
                gs2bare = f"n={user},r={cnonce}"
                without_p = f"c=biws,r={parts['r']}"
                authmsg = gs2bare + "," + sf + "," + without_p
                csig = hmac.new(stored, authmsg.encode(), hashlib.sha1).digest()
                proof = bytes(a ^ b for a, b in zip(client_key, csig))
                cfin = without_p + ",p=" + b64(proof)
                s.sendall((b64(cfin.encode()) + "\r\n").encode())
                fin = recv_smtp_reply(s)
                # RFC 4954 section 4: SMTP carries no data with a successful
                # outcome, so the server-final message may come as a 334
                # challenge, answered with an empty line, before the 235.
                if fin and fin[-1].startswith("334"):
                    s.sendall(b"\r\n")
                    fin = recv_smtp_reply(s)
                ok = bool(fin and fin[-1].startswith("235"))
                if not ok:
                    log(f"FAIL {case['id']} scram final {fin}")
                s.sendall(b"QUIT\r\n")
                try:
                    recv_smtp_reply(s)
                except Exception:
                    pass
                return ok
            finally:
                try:
                    s.close()
                except OSError:
                    pass
        if kind == "smtp_dane":
            import ssl
            import tempfile
            import threading
            token = case.get("token") or "TOKEN-DANE-001"
            port = int(case.get("sink_port") or 2527)
            good = not case.get("dane_mismatch")
            got = {"data": False, "starttls": False}
            cert = str(APP / "etc/ssl/server.certificate")
            key = str(APP / "etc/ssl/server.private")
            bad_dir = None
            if not good:
                bad_dir = tempfile.TemporaryDirectory()
                cert = str(Path(bad_dir.name) / "c.pem")
                key = str(Path(bad_dir.name) / "k.pem")
                subprocess.run(
                    ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                     "-keyout", key, "-out", cert, "-days", "1",
                     "-subj", "/CN=other.example.test"],
                    check=True, capture_output=True, timeout=20,
                )
            def sink():
                ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                ctx.load_cert_chain(cert, key)
                srv = socket.socket()
                srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                srv.bind(("127.0.0.1", port))
                srv.listen(1)
                srv.settimeout(20)
                try:
                    c, _ = srv.accept()
                    c.sendall(b"220 sink\r\n")
                    buf = b""
                    wrapped = None
                    def recv_line(sock):
                        nonlocal buf
                        while b"\n" not in buf:
                            n = sock.recv(4096)
                            if not n:
                                return b""
                            buf += n
                        i = buf.find(b"\n")
                        line, buf = buf[: i + 1], buf[i + 1 :]
                        return line
                    sock = c
                    while True:
                        line = recv_line(sock)
                        if not line:
                            break
                        u = line.decode("utf-8", "replace").upper()
                        if u.startswith("EHLO") or u.startswith("HELO"):
                            # RFC 5321 4.1.1.1: the first line of the reply
                            # names the server; extension keywords follow it,
                            # so a client that parses EHLO by the RFC sees
                            # STARTTLS offered.
                            sock.sendall(b"250-sink.test\r\n250 STARTTLS\r\n")
                        elif u.startswith("STARTTLS"):
                            sock.sendall(b"220 ready\r\n")
                            wrapped = ctx.wrap_socket(sock, server_side=True)
                            sock = wrapped
                            got["starttls"] = True
                            buf = b""
                        elif u.startswith("MAIL") or u.startswith("RCPT") or u.startswith("RSET"):
                            # Match sink requires TLS so a non-DANE client cannot pass.
                            # Mismatch sink allows plaintext so a non-DANE client *would*
                            # deliver and the case fails unless the MTA aborts after TLSA.
                            if wrapped is None and not case.get("dane_mismatch"):
                                sock.sendall(b"530 Must issue STARTTLS first\r\n")
                            else:
                                sock.sendall(b"250 ok\r\n")
                        elif u.startswith("DATA"):
                            if wrapped is None and not case.get("dane_mismatch"):
                                sock.sendall(b"530 Must issue STARTTLS first\r\n")
                                continue
                            sock.sendall(b"354 go\r\n")
                            body = b""
                            while True:
                                l = recv_line(sock)
                                if not l or l == b".\r\n":
                                    break
                                body += l
                            if token.encode() in body:
                                got["data"] = True
                            sock.sendall(b"250 queued\r\n")
                        elif u.startswith("QUIT"):
                            sock.sendall(b"221 bye\r\n")
                            break
                        else:
                            sock.sendall(b"250 ok\r\n")
                    c.close()
                except Exception:
                    pass
                finally:
                    srv.close()
            t = threading.Thread(target=sink, daemon=True)
            t.start()
            time.sleep(0.2)
            smtp_session(case["script"])
            t.join(8)
            if bad_dir:
                bad_dir.cleanup()
            want = not case.get("dane_mismatch")
            if want:
                if not got["data"]:
                    log(f"FAIL {case['id']} DANE match did not deliver token starttls={got['starttls']}")
                    return False
            else:
                if got["data"]:
                    log(f"FAIL {case['id']} DANE mismatch delivered without TLSA verify")
                    return False
            if case.get("expect_tlsrpt_report"):
                # The instruction asks for a JSON report
                # (application/tlsrpt+json) that names the evaluated domain in
                # `policy-domain`, with dane-invalid on mismatch. Parse it and
                # check exactly that, on the report for this case's domain.
                root = APP / "var/mail/tlsrpt/Maildir"
                reports = []
                for _rel, text in maildir_files(root):
                    reports.extend(tlsrpt_reports(text))
                if not reports:
                    log(f"FAIL {case['id']} missing TLS-RPT report (application/tlsrpt+json JSON) in rua mailbox")
                    return False
                domain = rcpt_domain(case.get("script"))
                mine = [r for r in reports if domain and domain in tlsrpt_policy_domains(r)]
                if not mine:
                    log(f"FAIL {case['id']} TLS-RPT report has no policy-domain {domain}")
                    return False
                if case.get("dane_mismatch") and not any("dane-invalid" in json.dumps(r) for r in mine):
                    log(f"FAIL {case['id']} TLS-RPT missing dane-invalid")
                    return False
            return True
        if kind == "smtp_eai_downgrade":
            # RFC 6530 §8: 7-bit next hop (no SMTPUTF8) — rewrite UTF-8 headers
            # as RFC 2047, or bounce a UTF-8 envelope.
            import threading
            token = case.get("token") or "TOKEN-EAI-DOWN-001"
            port = int(case.get("sink_port") or 2529)
            got = {"data": False, "body": b"", "smtputf8": False}
            def sink():
                srv = socket.socket()
                srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                srv.bind(("127.0.0.1", port))
                srv.listen(1)
                srv.settimeout(20)
                try:
                    c, _ = srv.accept()
                    f = c.makefile("rwb", buffering=0)
                    c.sendall(b"220 7bit-sink\r\n")
                    while True:
                        line = f.readline()
                        if not line:
                            break
                        u = line.decode("utf-8", "replace").upper()
                        if u.startswith("EHLO") or u.startswith("HELO"):
                            # 7-bit-era peer: PIPELINING/SIZE only — no SMTPUTF8.
                            # RFC 5321 4.1.1.1: domain line first, then keywords.
                            c.sendall(b"250-7bit-sink.test\r\n250-PIPELINING\r\n250 SIZE 10240000\r\n")
                        elif u.startswith("DATA"):
                            c.sendall(b"354 go\r\n")
                            buf = b""
                            while True:
                                l = f.readline()
                                if not l or l == b".\r\n":
                                    break
                                buf += l
                            got["body"] = buf
                            if token.encode() in buf:
                                got["data"] = True
                            c.sendall(b"250 queued\r\n")
                        elif u.startswith("QUIT"):
                            c.sendall(b"221 bye\r\n")
                            break
                        elif "SMTPUTF8" in u:
                            got["smtputf8"] = True
                            c.sendall(b"250 ok\r\n")
                        else:
                            c.sendall(b"250 ok\r\n")
                    c.close()
                except Exception:
                    pass
                finally:
                    srv.close()
            # Snapshot the reverse-path Maildir before the session: reset_maildirs()
            # runs only for IMAP cases, so mail an earlier case delivered here is
            # still on disk (the m01 remote-MX bounce also lands in alice's
            # Maildir). Only a message that appears during THIS case counts.
            bounce_root = APP / "var/mail" / (case.get("maildir_user") or "alice") / "Maildir"
            def bounce_files():
                return {p for p in bounce_root.rglob("*")
                        if p.is_file() and "dovecot" not in p.name}
            before_bounce = bounce_files()
            t = threading.Thread(target=sink, daemon=True)
            t.start()
            time.sleep(0.2)
            smtp_session(case["script"])
            t.join(8)
            if case.get("expect_bounce"):
                if got["data"]:
                    log(f"FAIL {case['id']} 7-bit MX accepted UTF-8 envelope")
                    return False
                # RFC 5321 3.7: the notification goes to the reverse path. Any new
                # message there is the bounce; the wording is the implementation's
                # own (step 1 names the Postfix "Returned mail" genre), so no
                # golden body text is pinned.
                deadline = time.time() + 5
                fresh = set()
                while time.time() < deadline:
                    fresh = bounce_files() - before_bounce
                    if fresh:
                        break
                    time.sleep(0.1)
                if not fresh:
                    log(f"FAIL {case['id']} no bounce to the reverse path for UTF-8 envelope to 7-bit MX")
                    return False
                return True
            if not got["data"]:
                log(f"FAIL {case['id']} 7-bit MX did not receive downgraded DATA")
                return False
            body = got["body"]
            if b"=?UTF-8?" not in body and b"=?utf-8?" not in body:
                log(f"FAIL {case['id']} 7-bit MX DATA missing RFC 2047 encoded-word")
                return False
            # Raw UTF-8 café must not appear; Q-encoding uses =C3=A9.
            if bytes([0xC3, 0xA9]) in body:
                log(f"FAIL {case['id']} 7-bit MX DATA still has raw UTF-8")
                return False
            if token.encode() not in body:
                log(f"FAIL {case['id']} 7-bit MX DATA missing token")
                return False
            return True
        if kind == "smtp_retry":
            import threading
            sink_got = {"n": 0, "token": False, "codes": []}
            token = case.get("token") or "TOKEN-RETRY-001"
            def sink():
                srv = socket.socket()
                srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                srv.bind(("127.0.0.1", int(case.get("sink_port") or 2526)))
                srv.listen(2)
                srv.settimeout(20)
                try:
                    while sink_got["n"] < 2:
                        try:
                            c, _ = srv.accept()
                        except socket.timeout:
                            break
                        f = c.makefile("rwb", buffering=0)
                        c.sendall(b"220 sink\r\n")
                        while True:
                            line = f.readline()
                            if not line:
                                break
                            u = line.decode("utf-8", "replace").upper()
                            if u.startswith("DATA"):
                                sink_got["n"] += 1
                                if sink_got["n"] == 1:
                                    c.sendall(b"450 try again later\r\n")
                                    continue
                                c.sendall(b"354 go\r\n")
                                buf = b""
                                while True:
                                    l = f.readline()
                                    if not l or l == b".\r\n":
                                        break
                                    buf += l
                                if token.encode() in buf:
                                    sink_got["token"] = True
                                c.sendall(b"250 queued\r\n")
                            elif u.startswith("QUIT"):
                                c.sendall(b"221 bye\r\n")
                                break
                            else:
                                c.sendall(b"250 ok\r\n")
                        c.close()
                finally:
                    srv.close()
            t = threading.Thread(target=sink, daemon=True)
            t.start()
            time.sleep(0.2)
            smtp_session(case["script"])
            deferred = APP / "var/spool/postfix/deferred"
            has_def = any(p.name[0] != "." for p in deferred.glob("*")) if deferred.is_dir() else False
            if sink_got["n"] < 2:
                rc, _ = run_cand([str(APP / "sbin/postfix"), "-c", str(APP / "etc/postfix"), "flush"],
                                 timeout=10)
                if rc == 124:
                    raise subprocess.TimeoutExpired("postfix flush", 10)
                t.join(8)
            if sink_got["n"] < 1:
                log(f"FAIL {case['id']} nexthop never contacted n={sink_got['n']} deferred={has_def}")
                return False
            if sink_got["n"] < 2 and not has_def:
                log(f"FAIL {case['id']} no defer after 4xx n={sink_got['n']}")
                return False
            if sink_got["n"] >= 2:
                return True
            log(f"FAIL {case['id']} flush did not retry n={sink_got['n']}")
            return False
        if kind == "jmap_blob":
            def http_get_json(path: str) -> dict:
                s0 = socket.create_connection(("127.0.0.1", 8080), 5)
                s0.sendall(f"GET {path} HTTP/1.1\r\nHost: 127.0.0.1\r\n{JMAP_AUTH}Connection: close\r\n\r\n".encode())
                raw0 = b""
                try:
                    while True:
                        n = s0.recv(8192)
                        if not n:
                            break
                        raw0 += n
                except OSError:
                    pass
                body0 = raw0.split(b"\r\n\r\n", 1)[-1].decode("utf-8", "replace")
                try:
                    return json.loads(body0)
                except Exception:
                    return {}
            def url_path(url: str) -> str:
                u = url.replace("http://127.0.0.1:8080", "")
                if "://" in u:
                    u = "/" + u.split("/", 3)[-1]
                if not u.startswith("/"):
                    u = "/" + u
                return u.split("?")[0]
            sess = http_get_json("/.well-known/jmap")
            account_id = jmap_account(sess)
            up = (sess.get("uploadUrl") or "http://127.0.0.1:8080/jmap/upload/{accountId}").replace("{accountId}", account_id)
            payload = (case.get("blob") or "blob-bytes-001").encode()
            s = socket.create_connection(("127.0.0.1", 8080), 5)
            path = url_path(up)
            req = (
                f"POST {path} HTTP/1.1\r\nHost: 127.0.0.1\r\n{JMAP_AUTH}".encode()
                + b"Content-Type: application/octet-stream\r\nContent-Length: "
                + str(len(payload)).encode() + b"\r\nConnection: close\r\n\r\n" + payload
            )
            s.sendall(req)
            buf = b""
            try:
                while True:
                    n = s.recv(8192)
                    if not n:
                        break
                    buf += n
            except OSError:
                pass
            text = buf.decode("utf-8", "replace")
            if "201" not in text.split("\r\n", 1)[0] and "200" not in text.split("\r\n", 1)[0]:
                log(f"FAIL {case['id']} blob upload {text[:200]!r}")
                return False
            m = re.search(r'"blobId"\s*:\s*"([^"]+)"', text)
            if not m:
                log(f"FAIL {case['id']} no blobId {text[:200]!r}")
                return False
            bid = m.group(1)
            down = (sess.get("downloadUrl") or "http://127.0.0.1:8080/jmap/download/{accountId}/{blobId}/{name}")
            down = down.replace("{accountId}", account_id).replace("{blobId}", bid).replace("{name}", "x.bin")
            s2 = socket.create_connection(("127.0.0.1", 8080), 5)
            s2.sendall(
                f"GET {url_path(down)} HTTP/1.1\r\nHost: 127.0.0.1\r\n{JMAP_AUTH}Connection: close\r\n\r\n".encode()
            )
            buf2 = b""
            try:
                while True:
                    n = s2.recv(8192)
                    if not n:
                        break
                    buf2 += n
            except OSError:
                pass
            body = buf2.split(b"\r\n\r\n", 1)[-1]
            if payload not in body:
                log(f"FAIL {case['id']} blob download mismatch")
                return False
            return True
        if kind == "jmap_eventsource":
            s0 = socket.create_connection(("127.0.0.1", 8080), 5)
            s0.sendall(("GET /.well-known/jmap HTTP/1.1\r\nHost: 127.0.0.1\r\n" + JMAP_AUTH + "Connection: close\r\n\r\n").encode())
            raw0 = b""
            try:
                while True:
                    n = s0.recv(8192)
                    if not n:
                        break
                    raw0 += n
            except OSError:
                pass
            ev = "/jmap/eventsource"
            try:
                sess = json.loads(raw0.split(b"\r\n\r\n", 1)[-1].decode("utf-8", "replace"))
                u = sess.get("eventSourceUrl") or ev
                u = u.replace("{types}", "*").replace("{closeafter}", "state").replace("{ping}", "0")
                u = u.replace("http://127.0.0.1:8080", "")
                if "://" in u:
                    u = "/" + u.split("/", 3)[-1]
                if not u.startswith("/"):
                    u = "/" + u
                ev = u
            except Exception:
                pass
            s = socket.create_connection(("127.0.0.1", 8080), 5)
            s.settimeout(5)
            s.sendall(f"GET {ev} HTTP/1.1\r\nHost: 127.0.0.1\r\n{JMAP_AUTH}Accept: text/event-stream\r\nConnection: close\r\n\r\n".encode())
            buf = b""
            try:
                while True:
                    n = s.recv(8192)
                    if not n:
                        break
                    buf += n
                    if b"event:" in buf:
                        break
            except OSError:
                pass
            text = buf.decode("utf-8", "replace")
            if "text/event-stream" not in text or "event:" not in text:
                log(f"FAIL {case['id']} eventsource {text[:200]!r}")
                return False
            return True
        if kind == "sieve":
            got = sieve_codes(case["script"], int(case.get("port") or 24190))
            exp = case.get("sieve_status") or case.get("codes")
            if got != exp:
                log(f"FAIL {case['id']} sieve got={got} exp={exp}")
                return False
            return True
        if kind == "jmap_query":
            cand_py(_RMTREE, str(APP / "var/sieve"))
            if case.get("seed_smtp"):
                smtp_session(case["seed_smtp"])
            port = int(case.get("port") or 8080)
            account_id = "u1"
            try:
                s0 = socket.create_connection(("127.0.0.1", port), 5)
                s0.sendall(("GET /.well-known/jmap HTTP/1.1\r\nHost: 127.0.0.1\r\n" + JMAP_AUTH + "Connection: close\r\n\r\n").encode())
                raw0 = b""
                try:
                    while True:
                        n = s0.recv(8192)
                        if not n:
                            break
                        raw0 += n
                except OSError:
                    pass
                text0 = raw0.decode("utf-8", "replace")
                js = text0.split("\r\n\r\n", 1)[-1]
                sess = json.loads(js)
                account_id = jmap_account(sess)
            except Exception:
                pass
            s = socket.create_connection(("127.0.0.1", port), 5)
            payload = (
                b'{"using":["urn:ietf:params:jmap:core","urn:ietf:params:jmap:mail"],'
                b'"methodCalls":[["Email/query",{"accountId":"' + account_id.encode("ascii", "replace") + b'","filter":null},"0"]]}'
            )
            req = (
                b"POST /jmap HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                + JMAP_AUTH.encode()
                + b"Content-Length: " + str(len(payload)).encode() + b"\r\nConnection: close\r\n\r\n" + payload
            )
            s.sendall(req)
            buf = b""
            try:
                while True:
                    n = s.recv(8192)
                    if not n:
                        break
                    buf += n
            except OSError:
                pass
            text = buf.decode("utf-8", "replace")
            ok = "200" in text.split("\r\n", 1)[0] and "Email/query" in text and "methodResponses" in text
            if case.get("expect_jmap_ids") and ('"ids":[]' in text.replace(" ", "") or '"ids": []' in text):
                ok = False
            if not ok:
                log(f"FAIL {case['id']} jmap_query {text[:300]!r}")
            return ok
        if kind == "sts":
            s = socket.create_connection(("127.0.0.1", int(case.get("port") or 8080)), 5)
            s.sendall(b"GET /.well-known/mta-sts.txt HTTP/1.1\r\nHost: mta-sts.example.test\r\nConnection: close\r\n\r\n")
            buf = b""
            try:
                while True:
                    n = s.recv(4096)
                    if not n:
                        break
                    buf += n
            except OSError:
                pass
            text = buf.decode("utf-8", "replace")
            ok = (
                "200" in text.split("\r\n", 1)[0]
                and "version: STSv1" in text
                and "mode:" in text
                and "mx: mail.example.test" in text
            )
            if not ok:
                log(f"FAIL {case['id']} mta-sts {text[:200]!r}")
            return ok
        if kind == "jmap":
            s = socket.create_connection(("127.0.0.1", int(case.get("port") or 8080)), 5)
            s.sendall(("GET /.well-known/jmap HTTP/1.1\r\nHost: 127.0.0.1\r\n" + JMAP_AUTH + "Connection: close\r\n\r\n").encode())
            buf = b""
            try:
                while True:
                    n = s.recv(4096)
                    if not n:
                        break
                    buf += n
            except OSError:
                pass
            text = buf.decode("utf-8", "replace")
            ok = (
                "200" in text.split("\r\n", 1)[0]
                and "urn:ietf:params:jmap:core" in text
                and "urn:ietf:params:jmap:mail" in text
                and "apiUrl" in text
                and "downloadUrl" in text
                and "uploadUrl" in text
            )
            if not ok:
                log(f"FAIL {case['id']} jmap {text[:200]!r}")
            return ok
        if kind == "smtp_pipeline":
            # RFC 2920: the client may send a group of commands in one write and
            # read the replies afterwards. Grouping changes only reply timing,
            # not the replies, so the expected codes are the recorded
            # single-command ones. Each group declares how many replies it
            # expects and the reader splits them by the continuation rule, so the
            # outcome does not depend on TCP segmentation.
            addr = (SMTP_ADDR[0], int(case.get("port") or SMTP_ADDR[1]))
            s = socket.create_connection(addr, 8)
            try:
                banner = recv_smtp_replies(s, 1)
                if case.get("banner_code") and (not banner or banner[0] != case["banner_code"]):
                    log(f"FAIL {case['id']} banner got={banner} exp={case['banner_code']}")
                    return False
                got = []
                for group in case["groups"]:
                    s.sendall(group["send"].encode("utf-8", "surrogateescape"))
                    got.extend(recv_smtp_replies(s, int(group["replies"])))
            finally:
                try:
                    s.close()
                except OSError:
                    pass
            exp = case["codes"]
            if got != exp:
                log(f"FAIL {case['id']} pipelined codes got={got} exp={exp}")
                return False
            note = case.get("notes") or ""
            if "TOKEN-" in note:
                tok = note.split("token ")[-1].strip() if "token " in note else ""
                found = None
                root = APP / "var/mail" / (case.get("maildir_user") or "bob") / "Maildir"
                for rel, blob in maildir_files(root):
                    if tok in blob:
                        found = rel
                        break
                if found is None:
                    log(f"FAIL {case['id']} missing maildir token {tok}")
                    return False
                parts = Path(found).parts
                if "new" not in parts and "cur" not in parts:
                    log(f"FAIL {case['id']} pipelined token not in new/ or cur/")
                    return False
            return True
        if kind == "imap":
            reset_maildirs()
        if kind == "smtp":
            banner, got, replies = smtp_session(case["script"], case.get("port"))
            exp = case["codes"]
            if case.get("banner_code") and banner != case["banner_code"]:
                log(f"FAIL {case['id']} banner got={banner} exp={case['banner_code']}")
                return False
            if got != exp:
                log(f"FAIL {case['id']} smtp codes got={got} exp={exp}")
                return False
            ident = case.get("expect_tls_identity")
            if ident and ident not in LAST_TLS_IDENTITY:
                log(f"FAIL {case['id']} tls identity got={LAST_TLS_IDENTITY!r} exp contains {ident!r}")
                return False
            blob = "\n".join(ln for r in replies for ln in r)
            for n in case.get("expect_smtp_contains") or []:
                if n not in blob:
                    log(f"FAIL {case['id']} missing {n!r} in SMTP transcript")
                    return False
            for idx, needles in (case.get("expect_reply_contains") or {}).items():
                i = int(idx)
                blob_i = "\n".join(replies[i]) if 0 <= i < len(replies) else ""
                for n in needles:
                    if n not in blob_i:
                        log(f"FAIL {case['id']} reply {idx} missing {n!r}")
                        return False
            for idx, needles in (case.get("expect_reply_excludes") or {}).items():
                i = int(idx)
                blob_i = "\n".join(replies[i]) if 0 <= i < len(replies) else ""
                for n in needles:
                    if n in blob_i:
                        log(f"FAIL {case['id']} reply {idx} still has {n!r}")
                        return False
            note = case.get("notes") or ""
            if "TOKEN-" in note:
                tok = note.split("token ")[-1].strip() if "token " in note else ""
                if tok:
                    found = None
                    root = APP / "var/mail" / (case.get("maildir_user") or "bob") / "Maildir"
                    for rel, blob in maildir_files(root):
                        if tok in blob:
                            found = rel
                            break
                    if found is None:
                        log(f"FAIL {case['id']} missing maildir token {tok}")
                        return False
                    rel = found
                    parts = Path(rel).parts
                    if "new" not in parts and "cur" not in parts:
                        log(f"FAIL {case['id']} token not in new/ or cur/: {rel}")
                        return False
            if case.get("expect_dkim_real"):
                pub = APP / "etc/opendkim/keys/example.test/default.txt"
                ok_sig = False
                root = APP / "var/mail" / (case.get("maildir_user") or "bob") / "Maildir"
                for _rel, blob in maildir_files(root):
                    if verify_rfc6376_dkim(blob, pub):
                        ok_sig = True
                        break
                if not ok_sig:
                    log(f"FAIL {case['id']} RFC 6376 DKIM verify failed")
                    return False
            needles = case.get("expect_maildir_contains") or []
            if needles:
                blob = ""
                root = APP / "var/mail" / (case.get("maildir_user") or "bob") / "Maildir"
                for _rel, text in maildir_files(root):
                    blob += text
                for n in needles:
                    if n not in blob:
                        log(f"FAIL {case['id']} maildir missing {n!r}")
                        return False
            return True
        if kind == "imap":
            got, transcript = imap_session(case["script"])
            exp = case["tag_status"]
            if len(got) != len(exp) or any(g != e for g, e in zip(got, exp)):
                log(f"FAIL {case['id']} imap status got={got} exp={exp}")
                return False
            needle = case.get("expect_capability") or (
                ["IMAP4rev1"] if "CAPABILITY" in "".join(case.get("script") or []).upper() else []
            )
            for n in needle:
                if n not in transcript:
                    log(f"FAIL {case['id']} missing {n} in IMAP transcript")
                    return False
            for n in case.get("expect_untagged") or []:
                if n not in transcript:
                    log(f"FAIL {case['id']} missing untagged {n!r}")
                    return False
            return True
    except Exception as e:
        log(f"FAIL {case['id']} exception {e}")
        return False
    return False


def is_refusal_case(case: dict) -> bool:
    """True when the recorded session contains a refusal: an SMTP 4xx/5xx reply
    code or an IMAP tagged NO/BAD. These cases bound the score (test.sh), so a
    server that accepts every command cannot collect the accept-path cases."""
    kind = case.get("kind")
    if kind in ("smtp", "smtp_pipeline"):
        return any(str(c)[:1] in ("4", "5") for c in case.get("codes") or [])
    if kind == "imap":
        return any(str(s).upper() in ("NO", "BAD") for s in case.get("tag_status") or [])
    return False


def main(argv: list[str]) -> int:
    mode = argv[1] if len(argv) > 1 else "all"
    if mode == "sweep":
        return 0 if sweep_cand() else 1
    fixture_paths = [Path(p) for p in argv[2:]]
    results = {
        "build": False,
        "liveness": False,
        "wrap_ok": False,
        "gate_pass": 0,
        "gate_total": 0,
        "refusal_pass": 0,
        "refusal_total": 0,
        "pass": 0,
        "total": 0,
        "groups": {},
        "cases": [],
        "failed": [],
    }
    # The answer key is read into memory before any candidate code runs.
    results["fixtures"] = fixture_digests(fixture_paths)
    cases = load_cases(fixture_paths)
    cand_py(_RMTREE, str(APP / "var/sieve"))
    results["build"] = build()
    sweep_cand()
    if not results["build"]:
        json.dump(results, sys.stdout)
        print()
        return 0
    try:
        wrap = bounded(wrap_violation, 300)
    except Exception as e:
        wrap = f"wrap check did not complete: {e}"
    results["wrap_ok"] = not wrap
    if wrap:
        log("wrap: " + wrap)
    procs = start_servers()
    try:
        live = wait_port(SMTP_ADDR) and wait_port(IMAP_ADDR)
        if live and (APP / "SPEC" / "rfc6409.txt").exists():
            live = wait_port(("127.0.0.1", 2587))
        if live and (APP / "SPEC" / "rfc5804.txt").exists():
            live = wait_port(("127.0.0.1", 24190))
        if live and (APP / "SPEC" / "rfc8461.txt").exists():
            live = wait_port(("127.0.0.1", 8080))
        results["liveness"] = live
        required = tuple(p for p in SCORED_PORTS if p in (2525, 2143)
                         or (p == 2587 and (APP / "SPEC" / "rfc6409.txt").exists())
                         or (p == 24190 and (APP / "SPEC" / "rfc5804.txt").exists())
                         or (p == 8080 and (APP / "SPEC" / "rfc8461.txt").exists()))

        def listener_check(req: tuple[int, ...], probe: bool) -> bool:
            # The instruction's rule, on the processes that actually hold the
            # scored ports (any of the five that is listening, whether or not
            # its liveness was required), now and again after every case: a
            # stack that hands a port to something else later is caught then.
            try:
                why = bounded(stack_violation, 120, req, probe)
            except Exception as e:
                why = f"listener check did not complete: {e}"
            if why:
                log("wrap: " + why)
                results["wrap_ok"] = False
            return not why

        if live and not listener_check(required, True):
            live = False
            results["liveness"] = False
        if not live:
            log("servers did not listen")
        else:
            for group, case in cases:
                if time.monotonic() - T0 > CASE_DEADLINE_SEC:
                    log(f"FAIL {case['id']} not run: verifier time budget spent")
                    ok = False
                else:
                    try:
                        ok = bool(bounded(run_case, CASE_CAP_SEC, case))
                    except CaseTimeout:
                        log(f"FAIL {case['id']} no result within {CASE_CAP_SEC}s")
                        ok = False
                results["total"] += 1
                results["cases"].append([group, case_key(case), ok])
                g = results["groups"].setdefault(group, {"pass": 0, "total": 0})
                g["total"] += 1
                if ok:
                    g["pass"] += 1
                if case.get("gate"):
                    results["gate_total"] += 1
                    if ok:
                        results["gate_pass"] += 1
                if is_refusal_case(case):
                    results["refusal_total"] += 1
                    if ok:
                        results["refusal_pass"] += 1
                if ok:
                    results["pass"] += 1
                else:
                    results["failed"].append(case["id"])
                if not listener_check((), False):
                    break
            else:
                listener_check((), True)
    finally:
        stop_servers(procs)
        sweep_cand()
    json.dump(results, sys.stdout)
    print()
    return 0


if __name__ == "__main__":
    main(sys.argv)
