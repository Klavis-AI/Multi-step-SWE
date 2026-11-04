"""Minimal HTTP/1.1 client for driving a Matrix homeserver over loopback."""

from __future__ import annotations

import http.client
import json as _json
import socket
import ssl
import threading
import urllib.parse
from dataclasses import dataclass, field
from typing import Any


@dataclass
class Resp:
    status: int
    headers: dict[str, str]
    body: bytes
    json: Any = None
    error: str | None = None
    elapsed_ms: int = 0

    @property
    def ok(self) -> bool:
        return 200 <= self.status < 300


class HttpError(Exception):
    pass


class Http:
    """One connection pool per (scheme, host, port), safe for threaded use."""

    def __init__(self, base_url: str, timeout: float = 60.0, verify_tls: bool = False):
        u = urllib.parse.urlsplit(base_url)
        self.scheme = u.scheme or "http"
        self.host = u.hostname or "127.0.0.1"
        self.port = u.port or (443 if self.scheme == "https" else 80)
        self.timeout = timeout
        self.verify_tls = verify_tls
        self._local = threading.local()

    def _conn(self, fresh: bool = False):
        conn = getattr(self._local, "conn", None)
        if conn is not None and not fresh:
            return conn
        if conn is not None:
            try:
                conn.close()
            except Exception:
                pass
        if self.scheme == "https":
            ctx = ssl.create_default_context()
            if not self.verify_tls:
                ctx.check_hostname = False
                ctx.verify_mode = ssl.CERT_NONE
            conn = http.client.HTTPSConnection(
                self.host, self.port, timeout=self.timeout, context=ctx
            )
        else:
            conn = http.client.HTTPConnection(self.host, self.port, timeout=self.timeout)
        self._local.conn = conn
        return conn

    def request(
        self,
        method: str,
        path: str,
        *,
        body: bytes | None = None,
        headers: dict[str, str] | None = None,
        timeout: float | None = None,
    ) -> Resp:
        hdrs = {"Accept": "application/json", "User-Agent": "mxconf/1"}
        if headers:
            hdrs.update(headers)
        if body is not None and "Content-Type" not in hdrs:
            hdrs["Content-Type"] = "application/json"
        if body is None and method in ("POST", "PUT"):
            body = b""
            hdrs.setdefault("Content-Type", "application/json")

        last_exc: Exception | None = None
        for attempt in (0, 1):
            conn = self._conn(fresh=attempt == 1)
            if timeout is not None:
                conn.timeout = timeout
                if getattr(conn, "sock", None) is not None:
                    try:
                        conn.sock.settimeout(timeout)
                    except OSError:
                        pass
            try:
                conn.request(method, path, body=body, headers=hdrs)
                r = conn.getresponse()
                data = r.read()
                hd = {k.lower(): v for k, v in r.getheaders()}
                out = Resp(status=r.status, headers=hd, body=data)
                ct = hd.get("content-type", "")
                if "json" in ct and data:
                    try:
                        out.json = _json.loads(data.decode("utf-8"))
                    except Exception as exc:      # noqa: BLE001
                        out.error = f"invalid JSON body: {exc}"
                if timeout is not None:
                    conn.timeout = self.timeout
                return out
            except (http.client.HTTPException, socket.error, OSError) as exc:
                last_exc = exc
                try:
                    conn.close()
                except Exception:
                    pass
                self._local.conn = None
        raise HttpError(f"{method} {path}: {last_exc}")

    def close(self) -> None:
        conn = getattr(self._local, "conn", None)
        if conn is not None:
            try:
                conn.close()
            except Exception:
                pass
            self._local.conn = None
