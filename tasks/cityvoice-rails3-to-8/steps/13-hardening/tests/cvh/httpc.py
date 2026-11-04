"""A deliberately dumb HTTP client.

Redirects are never followed and cookies are never sent unless a scenario asks
for it, so every hop the application performs is a scored observation instead
of something the client hides.
"""

from __future__ import annotations

import http.client
import urllib.parse


class Response:
    __slots__ = ("status", "headers", "body", "raw")

    def __init__(self, status, headers, body, raw):
        self.status = status
        self.headers = headers  # lower-cased names
        self.body = body
        self.raw = raw


class Client:
    def __init__(self, host, port, timeout=60.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.cookies = {}

    def request(self, method, path, params=None, headers=None, body=None,
                use_cookies=False, encode="form"):
        # A browser's Accept, not "*/*".  With "*/*" Rails picks the first
        # entry of its own mime-type registry to render an extension-less
        # template -- Rails 4.0 answers /.well-known/status with
        # "application/zip" -- and which entry that is belongs to the
        # framework, not to CityVoice.  Pinning the header the way a browser
        # pins it makes format resolution the application's decision again.
        hdrs = {"Host": "%s:%d" % (self.host, self.port),
                "Accept": "text/html,application/xhtml+xml,"
                          "application/xml;q=0.9,*/*;q=0.8",
                "Accept-Language": "en-US,en;q=0.9",
                "User-Agent": "cvh/1"}
        payload = None
        if params is not None and method in ("POST", "PUT", "PATCH"):
            if encode == "form":
                payload = urllib.parse.urlencode(params, doseq=True).encode()
                hdrs["Content-Type"] = "application/x-www-form-urlencoded"
            else:
                payload = params.encode() if isinstance(params, str) else params
        elif params:
            sep = "&" if "?" in path else "?"
            path = path + sep + urllib.parse.urlencode(params, doseq=True)
        if body is not None:
            payload = body if isinstance(body, bytes) else body.encode()
        if payload is not None:
            hdrs["Content-Length"] = str(len(payload))
        if use_cookies and self.cookies:
            hdrs["Cookie"] = "; ".join("%s=%s" % kv for kv in self.cookies.items())
        if headers:
            hdrs.update(headers)

        conn = http.client.HTTPConnection(self.host, self.port,
                                          timeout=self.timeout)
        try:
            conn.request(method, path, body=payload, headers=hdrs)
            resp = conn.getresponse()
            raw = resp.read()
            out_headers = {}
            for name, value in resp.getheaders():
                name = name.lower()
                if name in out_headers:
                    out_headers[name] += ", " + value
                else:
                    out_headers[name] = value
            status = resp.status
        finally:
            conn.close()

        if use_cookies:
            for chunk in _split_set_cookie(out_headers.get("set-cookie", "")):
                if "=" in chunk:
                    k, _, v = chunk.partition("=")
                    self.cookies[k.strip()] = v.strip()

        charset = "utf-8"
        ctype = out_headers.get("content-type", "")
        if "charset=" in ctype:
            charset = ctype.split("charset=", 1)[1].split(";")[0].strip()
        try:
            text = raw.decode(charset, "replace")
        except LookupError:
            text = raw.decode("utf-8", "replace")
        return Response(status, out_headers, text, raw)


def _split_set_cookie(value):
    """Split a joined Set-Cookie header without breaking `Expires=Mon, 01 ...`."""
    parts, depth, cur = [], 0, []
    for ch in value:
        if ch == "," and depth == 0:
            parts.append("".join(cur))
            cur = []
            continue
        if ch == "=":
            depth = 1
        if ch == ";":
            depth = 0
        cur.append(ch)
    parts.append("".join(cur))
    return [p.split(";")[0] for p in parts if p.strip()]
