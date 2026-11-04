"""An SMTP capture server standing in for smtp.sendgrid.net.

config/environment.rb points ActionMailer at smtp.sendgrid.net:587 with
`:authentication => :plain` and `:enable_starttls_auto => true`.  The sink
therefore listens on 587, answers EHLO with AUTH but *not* STARTTLS (so
`enable_starttls_auto` correctly declines to upgrade), and accepts the PLAIN
exchange the mailer performs.  Delivered messages are parsed and queued.

Written against the raw protocol rather than `smtplib`/`smtpd` so that the same
file works on the recorder's Python and on every verifier's Python without a
deprecation shim.
"""

from __future__ import annotations

import base64
import email
import email.policy
import re
import socket
import threading


class Sink:
    def __init__(self, host="0.0.0.0", port=587):
        self.host = host
        self.port = port
        self.messages = []
        self._lock = threading.Lock()
        self._sock = None
        self._thread = None
        self._stop = threading.Event()
        self.auth_seen = []

    # -- lifecycle ---------------------------------------------------------

    def start(self):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self.host, self.port))
        self._sock.listen(16)
        self._sock.settimeout(0.25)
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=5)
        if self._sock:
            self._sock.close()

    def _serve(self):
        while not self._stop.is_set():
            try:
                conn, _ = self._sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=self._session, args=(conn,),
                             daemon=True).start()

    # -- one conversation --------------------------------------------------

    def _session(self, conn):
        conn.settimeout(30)
        rfile = conn.makefile("rb")

        def say(line):
            conn.sendall(line.encode() + b"\r\n")

        try:
            say("220 smtp.sendgrid.net ESMTP cvh")
            envelope = {"from": None, "to": []}
            while True:
                raw = rfile.readline()
                if not raw:
                    break
                line = raw.decode("utf-8", "replace").rstrip("\r\n")
                verb = line.split(" ")[0].upper() if line else ""
                if verb == "EHLO":
                    say("250-smtp.sendgrid.net")
                    say("250-AUTH PLAIN LOGIN")
                    say("250-8BITMIME")
                    say("250 SIZE 35882577")
                elif verb == "HELO":
                    say("250 smtp.sendgrid.net")
                elif verb == "AUTH":
                    self._auth(line, rfile, say)
                elif verb == "MAIL":
                    envelope["from"] = _addr(line)
                    say("250 2.1.0 Ok")
                elif verb == "RCPT":
                    envelope["to"].append(_addr(line))
                    say("250 2.1.5 Ok")
                elif verb == "DATA":
                    say("354 End data with <CR><LF>.<CR><LF>")
                    body = _read_dot(rfile)
                    self._record(envelope, body)
                    envelope = {"from": None, "to": []}
                    say("250 2.0.0 Ok: queued")
                elif verb == "RSET":
                    envelope = {"from": None, "to": []}
                    say("250 2.0.0 Ok")
                elif verb == "NOOP":
                    say("250 2.0.0 Ok")
                elif verb == "QUIT":
                    say("221 2.0.0 Bye")
                    break
                elif verb == "STARTTLS":
                    say("502 5.5.1 Not implemented")
                else:
                    say("250 2.0.0 Ok")
        except (OSError, socket.timeout):
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def _auth(self, line, rfile, say):
        parts = line.split()
        mechanism = parts[1].upper() if len(parts) > 1 else ""
        if mechanism == "PLAIN":
            if len(parts) > 2:
                blob = parts[2]
            else:
                say("334 ")
                blob = rfile.readline().decode("utf-8", "replace").strip()
            try:
                decoded = base64.b64decode(blob + "=" * (-len(blob) % 4))
                fields = decoded.split(b"\0")
                self.auth_seen.append(
                    (fields[1].decode() if len(fields) > 1 else "",
                     fields[2].decode() if len(fields) > 2 else ""))
            except Exception:
                self.auth_seen.append(("<undecodable>", ""))
            say("235 2.7.0 Authentication successful")
        elif mechanism == "LOGIN":
            say("334 VXNlcm5hbWU6")
            user = rfile.readline().decode("utf-8", "replace").strip()
            say("334 UGFzc3dvcmQ6")
            secret = rfile.readline().decode("utf-8", "replace").strip()
            try:
                self.auth_seen.append((
                    base64.b64decode(user + "=" * (-len(user) % 4)).decode(),
                    base64.b64decode(secret + "=" * (-len(secret) % 4)).decode()))
            except Exception:
                self.auth_seen.append(("<undecodable>", ""))
            say("235 2.7.0 Authentication successful")
        else:
            say("504 5.5.4 Unrecognized authentication type")

    def _record(self, envelope, body):
        msg = email.message_from_bytes(body, policy=email.policy.default)
        parts = []
        if msg.is_multipart():
            for part in msg.walk():
                if part.get_content_maintype() == "multipart":
                    continue
                parts.append({
                    "content_type": part.get_content_type(),
                    "text": part.get_content(),
                })
        else:
            parts.append({"content_type": msg.get_content_type(),
                          "text": msg.get_content()})
        with self._lock:
            self.messages.append({
                "envelope_from": envelope["from"],
                "envelope_to": sorted(envelope["to"]),
                "from": str(msg.get("From") or ""),
                "to": str(msg.get("To") or ""),
                "subject": str(msg.get("Subject") or ""),
                "parts": parts,
            })

    # -- drain -------------------------------------------------------------

    def take(self):
        """Return and clear everything delivered so far."""
        with self._lock:
            out = self.messages
            self.messages = []
            return out

    def wait_for(self, count, timeout=20.0, quiet_for=1.0):
        """Wait until `count` messages have arrived and delivery has gone quiet.

        ActionMailer's delivery is synchronous in this application, but a
        message can still land a moment after the HTTP response; waiting for
        quiescence rather than for the response keeps the count deterministic.
        """
        import time
        deadline = time.time() + timeout
        last_change = time.time()
        seen = len(self.messages)
        while time.time() < deadline:
            now = len(self.messages)
            if now != seen:
                seen = now
                last_change = time.time()
            if now >= count and (time.time() - last_change) >= quiet_for:
                break
            time.sleep(0.05)
        return self.take()


def _addr(line):
    match = re.search(r"<([^>]*)>", line)
    if match:
        return match.group(1)
    parts = line.split(":", 1)
    return parts[1].strip() if len(parts) > 1 else ""


def _read_dot(rfile):
    chunks = []
    while True:
        raw = rfile.readline()
        if not raw:
            break
        if raw in (b".\r\n", b".\n"):
            break
        if raw.startswith(b".."):
            raw = raw[1:]
        chunks.append(raw)
    return b"".join(chunks)
