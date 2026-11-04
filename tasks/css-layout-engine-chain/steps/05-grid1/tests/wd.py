"""Minimal W3C WebDriver (Level 1) client over plain HTTP.

Used two ways, with identical code on both sides:
  * against ChromeDriver, to record the expectations;
  * inside the verifier against the candidate's remote end, to score.
"""
import json, time, urllib.error, urllib.request

WEB_ELEMENT_ID = "element-6066-11e4-a52e-4f735466cecf"

class WDError(Exception):
    def __init__(self, status, error, message):
        super().__init__(f"{status} {error}: {message}")
        self.http_status = status; self.error = error; self.message = message

class WebDriver:
    def __init__(self, base_url, timeout=30.0):
        self.base = base_url.rstrip("/"); self.timeout = timeout; self.session_id = None

    def _req(self, method, path, body=None):
        url = self.base + path; data = None; headers = {"Accept": "application/json"}
        if body is not None:
            data = json.dumps(body).encode()
            headers["Content-Type"] = "application/json; charset=utf-8"
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                raw = r.read(); status = r.status
        except urllib.error.HTTPError as e:
            raw = e.read(); status = e.code
        except urllib.error.URLError as e:
            raise WDError(0, "connection", str(e))
        try:
            payload = json.loads(raw.decode("utf-8", "replace"))
        except Exception:
            raise WDError(status, "invalid json", raw[:400].decode("utf-8", "replace"))
        if status >= 400:
            v = payload.get("value", {}) if isinstance(payload, dict) else {}
            raise WDError(status, v.get("error", "?"), str(v.get("message", ""))[:400])
        if not isinstance(payload, dict) or "value" not in payload:
            raise WDError(status, "invalid response", str(payload)[:400])
        return payload["value"]

    def wait_ready(self, deadline_s=30.0):
        end = time.time() + deadline_s; last = None
        while time.time() < end:
            try:
                v = self._req("GET", "/status")
                if isinstance(v, dict) and v.get("ready", True): return v
            except WDError as e: last = e
            time.sleep(0.05)
        raise WDError(0, "not ready", f"/status never became ready ({last})")

    def new_session(self, caps=None):
        v = self._req("POST", "/session", {"capabilities": {"alwaysMatch": caps or {}}})
        self.session_id = v["sessionId"]; return v

    def delete_session(self):
        if self.session_id:
            try: self._req("DELETE", f"/session/{self.session_id}")
            except WDError: pass
            self.session_id = None

    def _s(self, path): return f"/session/{self.session_id}{path}"
    def set_window_rect(self, width, height, x=0, y=0):
        return self._req("POST", self._s("/window/rect"), {"width": width, "height": height, "x": x, "y": y})
    def navigate(self, url): return self._req("POST", self._s("/url"), {"url": url})
    def find_elements(self, selector, from_element=None):
        body = {"using": "css selector", "value": selector}
        if from_element is None: v = self._req("POST", self._s("/elements"), body)
        else: v = self._req("POST", self._s(f"/element/{from_element}/elements"), body)
        return [e[WEB_ELEMENT_ID] for e in v]
    def element_rect(self, eid): return self._req("GET", self._s(f"/element/{eid}/rect"))
    def element_css(self, eid, prop): return self._req("GET", self._s(f"/element/{eid}/css/{prop}"))
    def __enter__(self): return self
    def __exit__(self, *a): self.delete_session()
