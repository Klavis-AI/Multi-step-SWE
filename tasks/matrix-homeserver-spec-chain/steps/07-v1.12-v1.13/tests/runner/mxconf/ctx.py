"""Scenario context: issue requests, build a normalised transcript, compare."""

from __future__ import annotations

import hashlib
import json
import threading
import urllib.parse
from dataclasses import dataclass, field
from typing import Any, Callable

from .httpc import Http, HttpError, Resp
from .normalize import (COMPARABLE_ERRCODES, FUTURE_FIELDS, GLOBAL_DROP,
                        CONCEALABLE_ERRCODES, CONCEALED,
                        UNCOMPARABLE_ERRCODES, Symbols, canonicalise,
                        drop_empty_optionals, strip_failure_codes,
                        drop_null_optionals, keep_paths, missing_required, project,
                        strip_paths)

# Headers whose value carries specified behaviour.  Everything else is ignored.
INTERESTING_HEADERS = (
    "content-type",
    "content-disposition",
    "cross-origin-resource-policy",
    "access-control-allow-origin",
    "retry-after",
    "www-authenticate",
    "location",
)


def _media_type(value: str) -> str:
    """content-type reduced to its media type, keeping a multipart boundary flag."""
    main = value.split(";", 1)[0].strip().lower()
    return main


def _norm_header(name: str, value: str) -> str:
    if name == "content-type":
        return _media_type(value)
    if name == "content-disposition":
        # RFC 6266 lets a filename be a bare token or a quoted string, and the
        # release's own example quotes one where the reference does not, so the
        # quotes are removed before comparing: `filename=pic.png` and
        # `filename="pic.png"` name the same file.
        parts = [p.strip() for p in value.split(";")]
        out = [parts[0].lower()]
        for p in parts[1:]:
            k, _, v = p.partition("=")
            v = v.strip()
            if len(v) >= 2 and v[0] == '"' and v[-1] == '"':
                v = v[1:-1].replace('\\"', '"')
            out.append(f"{k.strip().lower()}={v}")
        return "; ".join(out)
    if name == "retry-after":
        return "<N>"
    return value.strip()


@dataclass
class Entry:
    label: str
    method: str
    path: str
    status: int
    op: str | None = None
    status_exact: bool = True
    errcode_compared: bool = True
    body: Any = None
    headers: dict[str, str] = field(default_factory=dict)
    binary: dict[str, Any] | None = None
    missing: list[str] = field(default_factory=list)
    note: str | None = None


class ScenarioAbort(Exception):
    """Raised when a scenario cannot continue; remaining checks count as failed."""


class Ctx:
    """Drives one scenario against one homeserver and records what happened."""

    def __init__(
        self,
        base_url: str,
        spec_index,
        server_name: str,
        *,
        timeout: float = 60.0,
        extra: dict[str, Any] | None = None,
    ):
        self.http = Http(base_url, timeout=timeout)
        self.base_url = base_url
        self.spec = spec_index
        self.server_name = server_name
        self.syms = Symbols()
        self.entries: list[Entry] = []
        self.declared_checks = 0
        self.extra = extra or {}
        self._lock = threading.Lock()
        self._users: dict[str, dict[str, Any]] = {}
        self.peer_http: Http | None = None
        self.peer_name: str | None = None

    def attach_peer(self, base_url: str, server_name: str,
                    timeout: float = 60.0) -> None:
        """A second homeserver the scenario drives, so federation happens for
        real between two servers rather than being simulated."""
        self.peer_http = Http(base_url, timeout=timeout)
        self.peer_name = server_name

    # -- request ----------------------------------------------------------- #
    def req(
        self,
        method: str,
        path: str,
        *,
        token: str | None = None,
        json_body: Any = None,
        body: bytes | None = None,
        headers: dict[str, str] | None = None,
        query: dict[str, Any] | None = None,
        check: bool = True,
        label: str | None = None,
        timeout: float | None = None,
        binary: bool = False,
        note: str | None = None,
        drop: tuple[str, ...] = (),
        only: tuple[str, ...] | None = None,
        derive=None,
        peer: bool = False,
        record_headers: bool | tuple[str, ...] = True,
        learn: bool = True,
    ) -> Resp:
        """Issue one request; `check=False` makes it unscored setup.

        `learn=False` is for an unscored request whose *outcome* the release
        leaves open (a server may refuse it or accept it): its response must
        not take symbols, or a server that accepts it where the recording
        reference refused would have every later id in the scenario numbered
        one off the recording.  Every other response still binds its ids in
        first-appearance order (normalize.Symbols.learn).
        """
        if query:
            flat = []
            for k, v in query.items():
                if v is None:
                    continue
                if isinstance(v, (list, tuple)):
                    for item in v:
                        flat.append((k, str(item)))
                elif isinstance(v, bool):
                    flat.append((k, "true" if v else "false"))
                else:
                    flat.append((k, str(v)))
            path = path + ("&" if "?" in path else "?") + urllib.parse.urlencode(flat)

        hdrs = dict(headers or {})
        if binary:
            # A request for media must not tell the server that only JSON is
            # acceptable: a server that honours `Accept` would be within its
            # rights to answer 406.
            hdrs.setdefault("Accept", "*/*")
        if token:
            hdrs["Authorization"] = f"Bearer {token}"
        payload = body
        if json_body is not None:
            payload = json.dumps(json_body, ensure_ascii=False).encode("utf-8")
            hdrs.setdefault("Content-Type", "application/json")

        client = self.peer_http if peer else self.http
        if client is None:
            raise ScenarioAbort("no peer homeserver is attached")
        try:
            r = client.request(method, path, body=payload, headers=hdrs,
                               timeout=timeout)
        except HttpError as exc:
            if check:
                self._append(Entry(label=label or f"{method} {path}",
                                   method=method, path=self._sym_path(path),
                                   status=-1,
                                   note=f"transport error: {exc}"))
            raise ScenarioAbort(str(exc)) from exc

        if r.json is not None and learn:
            self.syms.learn(r.json)

        if check:
            self._record(method, path, r, label=label, binary=binary, note=note,
                         drop=drop, only=only, derive=derive, peer=peer,
                         record_headers=record_headers)
        return r

    # -- transcript -------------------------------------------------------- #
    def _sym_path(self, path: str) -> str:
        raw, sep, query = path.partition("?")
        parts = [
            self.syms.scalar(urllib.parse.unquote(seg), None) if seg else seg
            for seg in raw.split("/")
        ]
        out = "/".join(str(p) for p in parts)
        if sep:
            qs = urllib.parse.parse_qsl(query, keep_blank_values=True)
            def qval(k, v):
                # A pagination token's *identity* is not comparable: two servers
                # may mint a fresh token for a position where another repeats
                # one.  Whether the scenario followed the right chain is caught
                # by the response body, not by the token's serial number.
                from .normalize import BATCH_KEYS, NUMERIC_QUERY_KEYS
                if k in BATCH_KEYS:
                    return "<BATCH>" if v else v
                if k in NUMERIC_QUERY_KEYS:
                    return v
                return self.syms.scalar(v, k)

            out += "?" + "&".join(f"{k}={qval(k, v)}" for k, v in qs)
        return out

    def _record(self, method: str, path: str, r: Resp, *, label=None,
                binary=False, note=None, drop=(), only=None, derive=None,
                peer=False, record_headers=True) -> None:
        raw_path = path.split("?", 1)[0]
        op = self.spec.match(method, raw_path) if self.spec else None
        op_key = f"{op.method} {op.template}" if op else None
        declared = self.spec.declared(method, raw_path, r.status) if self.spec else None
        if not declared:
            # No published schema for this status: compare the body as returned
            # (minus the globally dropped, deployment-dependent fields).
            declared = None

        missing: list[str] = []
        errcode_compared = True
        if binary and not (200 <= r.status < 300):
            # An error response is JSON even on a binary endpoint.
            binary = False
        if binary and derive is not None:
            # A binary response can still be compared on decoded facts (an
            # image's dimensions) rather than on its exact bytes, which depend
            # on the encoder.
            try:
                from . import normalize as _n2
                body = canonicalise(
                    _n2.drop_excluded_types(derive(r.body), _n2.EXCLUDED_TYPES),
                    self.syms, op_key)
            except Exception as exc:                        # noqa: BLE001
                body = {"__derive_error__": f"{type(exc).__name__}: {exc}"}
            binary = False
        elif binary or r.json is None:
            # No JSON came back.  A scenario that asked for a derived body gets
            # the derivation over an empty object instead of a null, so that a
            # server answering `{}` where the reference answered nothing at all
            # is not marked down for a body the release does not describe.
            if derive is not None:
                try:
                    body = canonicalise(derive({}), self.syms, op_key)
                except Exception as exc:            # noqa: BLE001
                    body = {"__derive_error__": f"{type(exc).__name__}: {exc}"}
            else:
                body: Any = None
        elif derive is not None:
            try:
                from . import normalize as _n
                derived = strip_paths(derive(r.json), set(_n.FUTURE_FIELDS))
                derived = _n.drop_excluded_types(derived, _n.EXCLUDED_TYPES)
                body = canonicalise(derived, self.syms, op_key)
            except Exception as exc:                        # noqa: BLE001
                body = {"__derive_error__": f"{type(exc).__name__}: {exc}"}
        else:
            missing = missing_required(r.json, declared)
            projected = drop_null_optionals(project(r.json, declared), declared)
            drops = set(GLOBAL_DROP) | set(drop)
            code = projected.get("errcode") if isinstance(projected, dict) else None
            if isinstance(code, str) and r.status >= 400:
                documented = (
                    self.spec.documented_errcodes(method, raw_path, r.status)
                    if self.spec else []
                )
                if (code in UNCOMPARABLE_ERRCODES or
                        (code not in documented and code not in COMPARABLE_ERRCODES)):
                    # The specification leaves the code for this condition open,
                    # so it is not compared on either side.
                    drops.add("errcode")
                    errcode_compared = False
                elif not documented and code in CONCEALABLE_ERRCODES:
                    # An undocumented refusal that could equally be "you may not
                    # see this" or "there is no such thing": one token for both
                    # (mxconf.normalize.CONCEALED).
                    projected["errcode"] = CONCEALED
            projected = drop_empty_optionals(
                strip_failure_codes(strip_paths(projected, drops)))
            if only is not None:
                projected = keep_paths(projected, only)
            body = canonicalise(projected, self.syms, op_key)

        from . import normalize as _n
        # `record_headers` is a bool, or a tuple naming the only headers to
        # record.  The tuple form exists for a response whose *content type* is
        # the server's own choice while some other header on it is the thing
        # under test -- a thumbnail carrying `Cross-Origin-Resource-Policy`,
        # say, where comparing the whole set would pin the codec.
        wanted = (INTERESTING_HEADERS if record_headers is True
                  else tuple(record_headers or ()))
        hdrs = {
            h: _norm_header(h, r.headers[h])
            for h in wanted
            if h in r.headers and h not in _n.EXCLUDED_HEADERS
        }
        bin_info = None
        if binary:
            bin_info = {
                "sha256": hashlib.sha256(r.body).hexdigest(),
                "len": len(r.body),
            }
        if r.error:
            note = (note + "; " if note else "") + r.error

        documented = (self.spec.documented_statuses(method, raw_path)
                      if self.spec else set())
        self._append(
            Entry(
                label=label or f"{method} {op.template if op else raw_path}",
                method=method,
                path=("hs2 " if peer else "") + self._sym_path(path),
                status=r.status,
                op=op_key,
                # Exact only where the published operation documents that
                # status *and* documents no other status in the same class:
                # when a schema lists both 400 and 403 for an endpoint it has
                # not said which applies to a given refusal.
                status_exact=(
                    str(r.status) in documented
                    and sum(1 for d in documented
                            if d.isdigit() and int(d) // 100 == r.status // 100) == 1
                ),
                errcode_compared=errcode_compared,
                body=body,
                headers=hdrs,
                binary=bin_info,
                missing=missing,
                note=note,
            )
        )

    def _append(self, e: Entry) -> None:
        with self._lock:
            self.entries.append(e)

    def transcript(self) -> list[dict]:
        return [
            {
                "label": e.label,
                "method": e.method,
                "path": e.path,
                "status": e.status,
                "op": e.op,
                "status_exact": e.status_exact,
                "errcode_compared": e.errcode_compared,
                "body": e.body,
                "headers": e.headers,
                "binary": e.binary,
                "missing_required": e.missing,
            }
            for e in self.entries
        ]

    # -- helpers ----------------------------------------------------------- #
    def background(self, fn: Callable[[], Any]):
        """Run part of a scenario concurrently (long-poll /sync while sending)."""
        box: dict[str, Any] = {}

        def run():
            try:
                box["value"] = fn()
            except BaseException as exc:       # noqa: BLE001
                box["error"] = exc

        t = threading.Thread(target=run, daemon=True)
        t.start()
        return t, box

    def close(self) -> None:
        self.http.close()
        if self.peer_http is not None:
            self.peer_http.close()
