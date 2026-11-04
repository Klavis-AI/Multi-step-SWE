"""The stub for www.southbendvoices.com.

StatusController#twilio_response_okay? POSTs to
http://www.southbendvoices.com/route_to_survey and decides the app is healthy
if the body mentions "welcome.mp3".  The host is resolved to this server by
/etc/hosts; the body it returns is switchable so both branches of
`everything_ok?` are reachable from the scenario script.
"""

from __future__ import annotations

import http.server
import threading

HEALTHY_BODY = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<Response><Play>https://s3.amazonaws.com/cityvoice/welcome.mp3</Play>'
    '<Redirect>/calls</Redirect></Response>\n'
)
UNHEALTHY_BODY = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<Response><Say>Service unavailable</Say><Hangup/></Response>\n'
)


class _Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _respond(self):
        state = self.server.state
        if state["mode"] == "reset":
            # Hang up without a status line: Net::HTTP raises, and the
            # application has no rescue, so Rails renders its 500 page.
            self.server.hits.append((self.command, self.path))
            self.close_connection = True
            try:
                self.connection.close()
            except OSError:
                pass
            return
        if state["mode"] == "error":
            self.send_response(500)
            payload = b"upstream failure\n"
        elif state["mode"] == "empty":
            # A 200 with nothing in it: `res.body` is "" rather than nil, so
            # the marker search returns nil without raising.
            self.send_response(200)
            payload = b""
        else:
            self.send_response(200)
            body = HEALTHY_BODY if state["mode"] == "healthy" else UNHEALTHY_BODY
            payload = body.encode()
        self.send_header("Content-Type", "text/xml")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
        self.server.hits.append((self.command, self.path))

    def do_GET(self):
        self._respond()

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length:
            self.rfile.read(length)
        self._respond()

    def log_message(self, fmt, *args):
        pass


class Stub:
    def __init__(self, host="0.0.0.0", port=80):
        self.host = host
        self.port = port
        self._server = None
        self._thread = None
        self.state = {"mode": "healthy"}
        self.hits = []

    def start(self):
        self._server = http.server.ThreadingHTTPServer((self.host, self.port),
                                                       _Handler)
        self._server.state = self.state
        self._server.hits = self.hits
        self._thread = threading.Thread(target=self._server.serve_forever,
                                        kwargs={"poll_interval": 0.1},
                                        daemon=True)
        self._thread.start()
        return self

    def set_mode(self, mode):
        """healthy | unhealthy | empty | error | reset"""
        assert mode in ("healthy", "unhealthy", "empty", "error", "reset"), mode
        self.state["mode"] = mode

    def stop(self):
        if self._server:
            self._server.shutdown()
            self._server.server_close()
        if self._thread:
            self._thread.join(timeout=5)
