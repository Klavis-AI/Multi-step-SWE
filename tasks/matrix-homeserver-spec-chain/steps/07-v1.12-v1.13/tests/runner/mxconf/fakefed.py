"""A homeserver identity that speaks the Server-Server API, for scenarios that
need a *forked* event DAG.

State resolution version 2 only does anything when two branches of the event DAG
assign different events to the same `(type, state_key)`.  The Client-Server API
cannot produce that: a client hands the server a content and the server picks the
`prev_events` itself, always the current forward extremities, so every event a
client sends extends a single chain.  A fork can only be authored by something
that signs its own PDUs and chooses their `prev_events` -- that is, by a
homeserver.  This module is the smallest thing that qualifies: an Ed25519
identity, an HTTPS listener that publishes its key and answers the questions a
real server asks about events it has been sent, and the four client-side calls
(`make_join`, `send_join`, `send_transaction`, plus the read APIs) needed to
become a genuine member of a room on the server under test and then push events
into it.

Everything here is the specification's own construction, built on the Python
standard library plus `cryptography` for Ed25519 and for the listener's
certificate:

* canonical JSON and unpadded base64 (`/appendices`),
* the redaction algorithm, the content hash, the reference hash and the event id
  for room versions 1 to 10 (`/server-server-api#signing-events` and the
  per-room-version `redactions` fragments),
* event signing: hash the full event, redact, sign the redaction, copy the
  signature back,
* `Authorization: X-Matrix ...` request signing
  (`/server-server-api#request-authentication`).

Correctness is not asserted here, it is *demonstrated*: `harness/scenarios/
step02_stateres.py` starts the pinned Synapse, joins a room on it with this
module and pushes events, and a Synapse that accepts an event has verified its
signature, its content hash and its authorisation -- which is exactly the
property the scenarios then rely on.

Nothing in this module is scored.  It is scenario *machinery*: what a scenario
records is what the Client-Server API of the server under test reports once the
forked DAG has been resolved.
"""

from __future__ import annotations

import base64
import datetime
import hashlib
import ipaddress
import json
import socket
import ssl
import tempfile
import threading
import time
import urllib.parse
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

from cryptography import x509
from cryptography.hazmat.primitives import hashes as _hashes
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.x509.oid import NameOID

FEDERATION = "/_matrix/federation"

# ---------------------------------------------------------------------------#
# Canonical JSON and unpadded base64 -- /appendices#canonical-json
# ---------------------------------------------------------------------------#


def canonical_json(value: Any) -> bytes:
    """The shortest UTF-8 JSON encoding with keys sorted by code point."""
    return json.dumps(
        value, ensure_ascii=False, separators=(",", ":"), sort_keys=True,
    ).encode("utf-8")


def encode_b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii").rstrip("=")


def decode_b64(text: str) -> bytes:
    return base64.b64decode(text + "=" * (-len(text) % 4))


def encode_urlsafe_b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")


# ---------------------------------------------------------------------------#
# Signing JSON -- /appendices#signing-json
# ---------------------------------------------------------------------------#


def sign_json(obj: dict, server_name: str, key_id: str,
              signing_key: Ed25519PrivateKey) -> dict:
    """Sign the canonical JSON of `obj` minus `signatures` and `unsigned`."""
    obj = dict(obj)
    signatures = dict(obj.pop("signatures", {}) or {})
    unsigned = obj.pop("unsigned", None)

    signature = signing_key.sign(canonical_json(obj))
    by_server = dict(signatures.get(server_name, {}))
    by_server[key_id] = encode_b64(signature)
    signatures[server_name] = by_server

    obj["signatures"] = signatures
    if unsigned is not None:
        obj["unsigned"] = unsigned
    return obj


# ---------------------------------------------------------------------------#
# The redaction algorithm -- /rooms/v1#redactions and /rooms/v6#redactions
# ---------------------------------------------------------------------------#

_ALLOWED_KEYS = frozenset({
    "event_id", "type", "room_id", "sender", "state_key", "content", "hashes",
    "signatures", "depth", "prev_events", "prev_state", "auth_events", "origin",
    "origin_server_ts", "membership",
})

_ALLOWED_CONTENT = {
    "m.room.member": frozenset({"membership"}),
    "m.room.create": frozenset({"creator"}),
    "m.room.join_rules": frozenset({"join_rule"}),
    "m.room.power_levels": frozenset({
        "ban", "events", "events_default", "kick", "redact", "state_default",
        "users", "users_default",
    }),
    "m.room.aliases": frozenset({"aliases"}),
    "m.room.history_visibility": frozenset({"history_visibility"}),
}

# Room versions whose redaction algorithm this module knows about.  The list
# the specification gives for version 1 holds through version 5; after that it
# changes three times, each time because a room version made a content key
# load-bearing for authorisation, and a redaction that dropped it would make the
# event impossible to check:
#
# * v6 drops `m.room.aliases`' `aliases` -- the event type left the
#   specification (the `m.room.aliases` event is no longer authorised),
# * v8 keeps `m.room.join_rules`' `allow`, which is the restricted join rule's
#   list of rooms, so a join authorised by it survives redaction,
# * v9 keeps `m.room.member`'s `join_authorised_via_users_server`, which names
#   the user who authorised such a join.
KNOWN_ROOM_VERSIONS = ("1", "2", "3", "4", "5", "6", "7", "8", "9", "10")

_PRE_V6 = ("1", "2", "3", "4", "5")
_RESTRICTED = ("8", "9", "10")


def _allowed_content_keys(room_version: str, etype: str) -> frozenset:
    keys = _ALLOWED_CONTENT.get(etype)
    if keys is None:
        return frozenset()
    if etype == "m.room.aliases" and room_version not in _PRE_V6:
        return frozenset()
    if etype == "m.room.join_rules" and room_version in _RESTRICTED:
        return keys | {"allow"}
    if etype == "m.room.member" and room_version in ("9", "10"):
        return keys | {"join_authorised_via_users_server"}
    return keys


def redact(event: dict, room_version: str) -> dict:
    """The event with every key the room version does not protect stripped."""
    out = {k: v for k, v in event.items() if k in _ALLOWED_KEYS}
    content = event.get("content")
    if isinstance(content, dict):
        allowed = _allowed_content_keys(room_version, event.get("type", ""))
        out["content"] = {k: v for k, v in content.items() if k in allowed}
    else:
        out["content"] = {}
    return out


# ---------------------------------------------------------------------------#
# Hashes and event identifiers -- /server-server-api#signing-events
# ---------------------------------------------------------------------------#


def content_hash(event: dict) -> bytes:
    """SHA-256 over the complete, unredacted event minus the volatile keys."""
    ev = dict(event)
    for key in ("age_ts", "unsigned", "signatures", "hashes", "outlier",
                "destinations"):
        ev.pop(key, None)
    return hashlib.sha256(canonical_json(ev)).digest()


def reference_hash(event: dict, room_version: str) -> bytes:
    """SHA-256 over the redacted event minus `signatures`, `age_ts`, `unsigned`."""
    ev = redact(event, room_version)
    for key in ("signatures", "age_ts", "unsigned"):
        ev.pop(key, None)
    return hashlib.sha256(canonical_json(ev)).digest()


def event_id(event: dict, room_version: str) -> str:
    """The event's identifier in the given room version.

    Room versions 1 and 2 carry an `event_id` chosen by the origin server;
    version 3 uses the reference hash in standard base64; version 4 and later
    use it in URL-safe base64, so that an event id is safe in a URL path.
    """
    if room_version in ("1", "2"):
        return event["event_id"]
    digest = reference_hash(event, room_version)
    if room_version == "3":
        return "$" + encode_b64(digest)
    return "$" + encode_urlsafe_b64(digest)


def add_hashes_and_signatures(event: dict, room_version: str, server_name: str,
                              key_id: str, signing_key: Ed25519PrivateKey) -> dict:
    """Content hash, then sign the *redacted* event, then copy the signature back.

    The signature covers only the keys a redaction preserves plus `hashes`, so a
    server that receives a redacted copy can still check it; the content hash is
    what ties the redacted copy to the full one.
    """
    event["hashes"] = {"sha256": encode_b64(content_hash(event))}
    stripped = redact(event, room_version)
    stripped.pop("signatures", None)
    signed = sign_json(stripped, server_name, key_id, signing_key)
    event["signatures"] = signed["signatures"]
    return event


def event_format_version(room_version: str) -> int:
    """1: `event_id` in the event and `[id, hashes]` reference pairs.
    2: no `event_id`, plain event-id strings (room version 3 and later)."""
    return 1 if room_version in ("1", "2") else 2


# ---------------------------------------------------------------------------#
# The listener's certificate.  Generated once per process: both ends of every
# connection this module makes disable certificate validation (the candidate
# through `federation_api.disable_tls_validation`, the oracle through
# `federation_verify_certificates`), so it carries no identity -- it exists
# because the Server-Server API is defined over HTTPS.
# ---------------------------------------------------------------------------#

_CERT_LOCK = threading.Lock()
_CERT_FILES: tuple[str, str] | None = None


def _self_signed_cert() -> tuple[str, str]:
    global _CERT_FILES
    with _CERT_LOCK:
        if _CERT_FILES is not None:
            return _CERT_FILES
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "127.0.0.1")])
        try:
            now = datetime.datetime.now(datetime.timezone.utc)
            builder = (
                x509.CertificateBuilder()
                .subject_name(name).issuer_name(name)
                .public_key(key.public_key())
                .serial_number(x509.random_serial_number())
                .not_valid_before_utc(now - datetime.timedelta(days=1))
                .not_valid_after_utc(now + datetime.timedelta(days=3650))
            )
        except AttributeError:                                   # cryptography < 42
            now = datetime.datetime.utcnow()
            builder = (
                x509.CertificateBuilder()
                .subject_name(name).issuer_name(name)
                .public_key(key.public_key())
                .serial_number(x509.random_serial_number())
                .not_valid_before(now - datetime.timedelta(days=1))
                .not_valid_after(now + datetime.timedelta(days=3650))
            )
        cert = builder.add_extension(
            x509.SubjectAlternativeName([
                x509.DNSName("localhost"),
                x509.IPAddress(ipaddress.IPv4Address("127.0.0.1")),
            ]),
            critical=False,
        ).sign(key, _hashes.SHA256())
        tmp = tempfile.mkdtemp(prefix="mxconf-fakefed-")
        crt_path, key_path = tmp + "/fake.crt", tmp + "/fake.key"
        with open(crt_path, "wb") as fh:
            fh.write(cert.public_bytes(serialization.Encoding.PEM))
        with open(key_path, "wb") as fh:
            fh.write(key.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            ))
        _CERT_FILES = (crt_path, key_path)
        return _CERT_FILES


# ---------------------------------------------------------------------------#
# The HTTPS listener
# ---------------------------------------------------------------------------#


class _Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "mxconf-fakefed/1"

    # Silence the default stderr access log; the server keeps its own.
    def log_message(self, fmt, *args):        # noqa: A003
        pass

    def _body(self) -> Any:
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        if not raw:
            return None
        try:
            return json.loads(raw.decode("utf-8"))
        except Exception:                                        # noqa: BLE001
            return None

    def _reply(self, status: int, payload: Any) -> None:
        data = canonical_json(payload)
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _dispatch(self, method: str) -> None:
        parsed = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
        body = self._body()
        fake: FakeFederation = self.server.fake            # type: ignore[attr-defined]
        fake.seen.append((method, parsed.path))
        try:
            status, payload = fake.handle(method, parsed.path, query, body)
        except Exception as exc:                                 # noqa: BLE001
            status, payload = 500, {"errcode": "M_UNKNOWN", "error": repr(exc)}
        self._reply(status, payload)

    def do_GET(self):                                            # noqa: N802
        self._dispatch("GET")

    def do_PUT(self):                                            # noqa: N802
        self._dispatch("PUT")

    def do_POST(self):                                           # noqa: N802
        self._dispatch("POST")


class _Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


# ---------------------------------------------------------------------------#


@dataclass
class Room:
    """What this server knows about one room it has joined."""

    room_id: str
    version: str
    # (type, state_key) -> event id, the state after the last event it made.
    state: dict[tuple[str, str], str] = field(default_factory=dict)
    depth: int = 1
    # The events with no children yet, i.e. what a new event would point at.
    tips: list[str] = field(default_factory=list)


class FederationError(Exception):
    pass


class FakeFederation:
    """An Ed25519 identity plus an HTTPS listener: a homeserver, minus the rooms.

    `server_name` is `127.0.0.1:<port>`, an IP literal with an explicit port, so
    the server-name resolution rules send every other server straight at the
    listener with no `.well-known` lookup and no SRV record.
    """

    KEY_ID = "ed25519:fake1"
    # A fixed seed: two recordings of the same scenario must be byte-identical,
    # and a fresh key would change every signature (though not, in fact, any
    # recorded value -- signatures never reach a transcript).
    SEED = bytes(range(32, 64))

    def __init__(self, port: int, *, verify_tls: bool = False,
                 timeout: float = 60.0):
        self.port = port
        self.server_name = f"127.0.0.1:{port}"
        self.signing_key = Ed25519PrivateKey.from_private_bytes(self.SEED)
        self.public_key = self.signing_key.public_key().public_bytes(
            serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        self.verify_tls = verify_tls
        self.timeout = timeout

        self.events: dict[str, dict] = {}          # event id -> PDU
        self.state_after: dict[str, dict[tuple[str, str], str]] = {}
        self.rooms: dict[str, Room] = {}
        self.seen: list[tuple[str, str]] = []      # what was asked of us
        self.received: list[dict] = []             # PDUs pushed at us
        self.received_edus: list[dict] = []        # EDUs pushed at us
        # What this server answers when asked for its users' keys.  A scenario
        # fills it in before the server under test is made to ask.
        self.device_keys: dict[str, dict] = {}     # user -> device -> DeviceKeys
        self.cross_signing: dict[str, dict] = {}   # user -> {master_key: ...}
        self.one_time_keys: dict[str, dict] = {}   # user -> device -> {alg:key}
        self.device_stream_id = 1                  # bumped when a scenario moves a device list
        self._txn = 0
        self._lock = threading.Lock()
        self._httpd: _Server | None = None
        self._thread: threading.Thread | None = None

    # -- identity ---------------------------------------------------------- #
    def user_id(self, localpart: str) -> str:
        return f"@{localpart}:{self.server_name}"

    def key_document(self) -> dict:
        doc = {
            "server_name": self.server_name,
            "verify_keys": {self.KEY_ID: {"key": encode_b64(self.public_key)}},
            "old_verify_keys": {},
            # A day is well inside the seven days a server is allowed to trust,
            # and long past the end of any scenario.
            "valid_until_ts": int(time.time() * 1000) + 24 * 3600 * 1000,
        }
        return sign_json(doc, self.server_name, self.KEY_ID, self.signing_key)

    # -- listener ---------------------------------------------------------- #
    def start(self) -> None:
        crt, key = _self_signed_cert()
        httpd = _Server(("127.0.0.1", self.port), _Handler)
        httpd.fake = self                                # type: ignore[attr-defined]
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(crt, key)
        httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
        self._httpd = httpd
        self._thread = threading.Thread(target=httpd.serve_forever,
                                        kwargs={"poll_interval": 0.05},
                                        daemon=True)
        self._thread.start()

    def stop(self) -> None:
        if self._httpd is not None:
            try:
                self._httpd.shutdown()
                self._httpd.server_close()
            except Exception:                                    # noqa: BLE001
                pass
            self._httpd = None

    def __enter__(self) -> "FakeFederation":
        self.start()
        return self

    def __exit__(self, *exc) -> None:
        self.stop()

    # -- serving ----------------------------------------------------------- #
    def handle(self, method: str, path: str, query: dict, body: Any):
        """Answer the questions a real homeserver asks about what we pushed.

        A server that receives a PDU whose `prev_events` or `auth_events` it does
        not have will come back and ask for them; one that is told about a fork
        may ask for the state at a point in it.  Answering is not optional --
        a server that cannot resolve an event's ancestry drops the event.
        """
        if path in (f"{FEDERATION}/v1/version",):
            return 200, {"server": {"name": "mxconf-fakefed", "version": "1"}}
        if path == "/_matrix/key/v2/server" or path.startswith("/_matrix/key/v2/server/"):
            return 200, self.key_document()

        if path.startswith(f"{FEDERATION}/v1/send/"):
            # Everything the room's other server sends us.  We keep it so a
            # scenario can see what happened, and acknowledge every PDU: this
            # server has no state of its own to corrupt.
            pdus = (body or {}).get("pdus") or []
            edus = (body or {}).get("edus") or []
            with self._lock:
                for pdu in pdus:
                    self.received.append(pdu)
                for edu in edus:
                    self.received_edus.append(edu)
            return 200, {"pdus": {self._id_of(p): {} for p in pdus}}

        if path.startswith(f"{FEDERATION}/v1/event/"):
            eid = urllib.parse.unquote(path.rsplit("/", 1)[-1])
            ev = self.events.get(eid)
            if ev is None:
                return 404, {"errcode": "M_NOT_FOUND", "error": "unknown event"}
            return 200, self._transaction([ev])

        if path.startswith(f"{FEDERATION}/v1/event_auth/"):
            rest = path[len(f"{FEDERATION}/v1/event_auth/"):]
            _room, _, eid = rest.partition("/")
            eid = urllib.parse.unquote(eid)
            ev = self.events.get(eid)
            if ev is None:
                return 404, {"errcode": "M_NOT_FOUND", "error": "unknown event"}
            return 200, {"auth_chain": self._auth_chain([eid])}

        if path.startswith(f"{FEDERATION}/v1/state_ids/"):
            state = self._state_for(query)
            if state is None:
                return 404, {"errcode": "M_NOT_FOUND", "error": "unknown event"}
            ids = sorted(set(state.values()))
            return 200, {"pdu_ids": ids, "auth_chain_ids": self._auth_chain_ids(ids)}

        if path.startswith(f"{FEDERATION}/v1/state/"):
            state = self._state_for(query)
            if state is None:
                return 404, {"errcode": "M_NOT_FOUND", "error": "unknown event"}
            ids = sorted(set(state.values()))
            return 200, {
                "pdus": [self.events[i] for i in ids if i in self.events],
                "auth_chain": self._auth_chain(ids),
            }

        if path.startswith(f"{FEDERATION}/v1/backfill/"):
            starts = query.get("v") or []
            limit = int((query.get("limit") or ["10"])[0])
            return 200, self._transaction(self._walk_back(starts, limit))

        if path.startswith(f"{FEDERATION}/v1/get_missing_events/"):
            body = body or {}
            latest = body.get("latest_events") or []
            earliest = set(body.get("earliest_events") or [])
            limit = int(body.get("limit") or 10)
            return 200, {"events": self._walk_back(latest, limit, stop=earliest,
                                                   exclude_starts=True)}

        if path.startswith(f"{FEDERATION}/v1/query/profile"):
            user = (query.get("user_id") or [""])[0]
            return 200, {"displayname": user.split(":")[0].lstrip("@")}

        if path == f"{FEDERATION}/v1/user/keys/query":
            # "Returns the current devices and identity keys for the given
            # users."  Only the users this server is asked about that it knows.
            wanted = (body or {}).get("device_keys") or {}
            out: dict[str, dict] = {}
            for user, devices in wanted.items():
                have = self.device_keys.get(user)
                if not have:
                    continue
                picked = {d: k for d, k in have.items()
                          if not devices or d in devices}
                if picked:
                    out[user] = picked
            answer: dict[str, Any] = {"device_keys": out}
            for field in ("master_key", "self_signing_key"):
                got = {u: keys[field]
                       for u, keys in self.cross_signing.items()
                       if u in wanted and field in keys}
                if got:
                    answer[field + "s"] = got
            return 200, answer

        if path == f"{FEDERATION}/v1/user/keys/claim":
            # "Claims one-time keys for use in pre-key messages."  A claimed key
            # is handed out once.
            wanted = (body or {}).get("one_time_keys") or {}
            out: dict[str, dict] = {}
            with self._lock:
                for user, devices in wanted.items():
                    for device, alg in (devices or {}).items():
                        pool = (self.one_time_keys.get(user) or {}).get(device)
                        if not pool:
                            continue
                        for key_id in sorted(pool):
                            if key_id.split(":", 1)[0] != alg:
                                continue
                            out.setdefault(user, {}).setdefault(device, {})[
                                key_id] = pool.pop(key_id)
                            break
            return 200, {"one_time_keys": out}

        if path.startswith(f"{FEDERATION}/v1/user/devices/"):
            # "Gets information on all of the user's devices."  This is the
            # endpoint a receiving server actually uses to (re)build a remote
            # user's device list -- a device-list update whose stream ids it
            # cannot follow sends it here rather than to /user/keys/query -- so
            # it answers from the same table.
            user = urllib.parse.unquote(path.rsplit("/", 1)[-1])
            devices = []
            for device_id, keys in sorted((self.device_keys.get(user)
                                           or {}).items()):
                devices.append({"device_id": device_id, "keys": keys,
                                "device_display_name": f"{device_id} of {user}"})
            answer: dict[str, Any] = {"user_id": user,
                                      "stream_id": self.device_stream_id,
                                      "devices": devices}
            for field in ("master_key", "self_signing_key"):
                got = (self.cross_signing.get(user) or {}).get(field)
                if got:
                    answer[field] = got
            return 200, answer

        return 404, {"errcode": "M_UNRECOGNIZED", "error": f"no route for {path}"}

    def _id_of(self, pdu: dict) -> str:
        room = self.rooms.get(pdu.get("room_id", ""))
        version = room.version if room else "9"
        try:
            return event_id(pdu, version)
        except Exception:                                        # noqa: BLE001
            return pdu.get("event_id", "$unknown")

    def _transaction(self, pdus: list[dict]) -> dict:
        return {"origin": self.server_name,
                "origin_server_ts": int(time.time() * 1000),
                "pdus": pdus}

    def _state_for(self, query: dict):
        eid = (query.get("event_id") or [""])[0]
        return self.state_after.get(eid)

    def _auth_chain_ids(self, event_ids: list[str]) -> list[str]:
        seen: set[str] = set()
        stack = list(event_ids)
        while stack:
            eid = stack.pop()
            ev = self.events.get(eid)
            if ev is None:
                continue
            for parent in _ref_ids(ev.get("auth_events") or []):
                if parent not in seen:
                    seen.add(parent)
                    stack.append(parent)
        return sorted(seen)

    def _auth_chain(self, event_ids: list[str]) -> list[dict]:
        return [self.events[i] for i in self._auth_chain_ids(event_ids)
                if i in self.events]

    def _walk_back(self, starts: list[str], limit: int, *, stop=(),
                   exclude_starts: bool = False) -> list[dict]:
        """Events reachable backwards from `starts` through `prev_events`."""
        out: list[dict] = []
        seen: set[str] = set()
        frontier = [s for s in starts if s in self.events]
        while frontier and len(out) < max(limit, 1):
            eid = frontier.pop(0)
            if eid in seen or eid in stop:
                continue
            seen.add(eid)
            ev = self.events.get(eid)
            if ev is None:
                continue
            if not (exclude_starts and eid in starts):
                out.append(ev)
            frontier.extend(_ref_ids(ev.get("prev_events") or []))
        return out

    # -- talking to another server ----------------------------------------- #
    def x_matrix_auth(self, method: str, uri: str, destination: str,
                      content: Any = None) -> str:
        """The `Authorization: X-Matrix ...` header value for one request.

        `uri` is the full path starting `/_matrix/`, including the query string,
        and with neither scheme nor the destination's host, exactly as
        /server-server-api#request-authentication defines it.
        """
        request: dict[str, Any] = {
            "method": method,
            "uri": uri,
            "origin": self.server_name,
            "destination": destination,
        }
        if content is not None:
            request["content"] = content
        signed = sign_json(request, self.server_name, self.KEY_ID,
                           self.signing_key)
        sig = signed["signatures"][self.server_name][self.KEY_ID]
        return (f'X-Matrix origin="{self.server_name}",'
                f'destination="{destination}",'
                f'key="{self.KEY_ID}",sig="{sig}"')

    def request(self, base_url: str, destination: str, method: str, uri: str,
                content: Any = None, *, timeout: float | None = None,
                raw: bool = False):
        """One authenticated Server-Server API call.  Returns (status, body).

        With `raw`, returns `(status, headers, bytes)` instead: the media
        endpoints answer `multipart/mixed`, which is not JSON and whose parts
        are what a scenario needs to look at.
        """
        import http.client

        parsed = urllib.parse.urlsplit(base_url)
        host = parsed.hostname or "127.0.0.1"
        port = parsed.port or (443 if parsed.scheme == "https" else 80)
        if parsed.scheme == "https":
            ctx = ssl.create_default_context()
            if not self.verify_tls:
                ctx.check_hostname = False
                ctx.verify_mode = ssl.CERT_NONE
            conn = http.client.HTTPSConnection(
                host, port, timeout=timeout or self.timeout, context=ctx)
        else:
            conn = http.client.HTTPConnection(
                host, port, timeout=timeout or self.timeout)
        payload = canonical_json(content) if content is not None else None
        headers = {
            "Authorization": self.x_matrix_auth(method, uri, destination, content),
            "Content-Type": "application/json",
            "Accept": "application/json",
        }
        try:
            if raw:
                headers["Accept"] = "*/*"
            conn.request(method, uri, body=payload, headers=headers)
            resp = conn.getresponse()
            body_bytes = resp.read()
            if raw:
                return resp.status, dict(resp.getheaders()), body_bytes
            try:
                parsed_body = (json.loads(body_bytes.decode("utf-8"))
                               if body_bytes else None)
            except Exception:                                    # noqa: BLE001
                parsed_body = {"__raw__":
                               body_bytes[:400].decode("utf-8", "replace")}
            return resp.status, parsed_body
        finally:
            try:
                conn.close()
            except Exception:                                    # noqa: BLE001
                pass

    # -- joining ----------------------------------------------------------- #
    def make_join(self, base_url: str, destination: str, room_id: str,
                  user_id: str, versions=KNOWN_ROOM_VERSIONS):
        uri = (f"{FEDERATION}/v1/make_join/{_esc(room_id)}/{_esc(user_id)}?"
               + urllib.parse.urlencode([("ver", v) for v in versions]))
        status, body = self.request(base_url, destination, "GET", uri)
        if status != 200 or not isinstance(body, dict) or "event" not in body:
            raise FederationError(f"make_join {status}: {body!r}")
        return body.get("room_version", "1"), body["event"]

    def send_join(self, base_url: str, destination: str, room_id: str,
                  user_id: str, *, displayname: str | None = None,
                  versions=KNOWN_ROOM_VERSIONS, wire: str | None = None) -> dict:
        """Run the remote join handshake, and become a real member of the room.

        The template from `make_join` is completed with this server's `origin`,
        `origin_server_ts` and (in room versions 1 and 2) `event_id`, hashed,
        signed, and submitted to `send_join`.  The response carries the room's
        state, which is what lets this server build events on top of it.
        """
        version, template = self.make_join(base_url, destination, room_id,
                                           user_id, versions)
        event = dict(template)
        event.setdefault("content", {})
        event["content"] = dict(event["content"])
        event["content"]["membership"] = "join"
        if displayname is not None:
            event["content"]["displayname"] = displayname
        event["type"] = "m.room.member"
        event["state_key"] = user_id
        event["sender"] = user_id
        event["room_id"] = room_id
        event["origin"] = self.server_name
        event["origin_server_ts"] = int(time.time() * 1000)
        event.pop("unsigned", None)
        event.pop("hashes", None)
        event.pop("signatures", None)
        if event_format_version(version) == 1:
            event["event_id"] = self._fresh_event_id()
        else:
            event.pop("event_id", None)
        add_hashes_and_signatures(event, version, self.server_name, self.KEY_ID,
                                  self.signing_key)
        eid = event_id(event, version)

        if wire == "v1":
            # The older wire, which answers `[200, {...}]` rather than the body
            # on its own.  Asked for explicitly so that a scenario can check the
            # endpoint that is still specified rather than only the one a modern
            # partner happens to prefer.
            uri = f"{FEDERATION}/v1/send_join/{_esc(room_id)}/{_esc(eid)}"
            status, body = self.request(base_url, destination, "PUT", uri, event)
            self.last_v1_envelope = body
            if isinstance(body, list) and len(body) == 2:
                body = body[1]
            if status != 200 or not isinstance(body, dict):
                raise FederationError(f"send_join v1 {status}: {str(body)[:400]}")
            return self._absorb_join(room_id, version, event, eid, user_id, body)

        uri = f"{FEDERATION}/v2/send_join/{_esc(room_id)}/{_esc(eid)}"
        status, body = self.request(base_url, destination, "PUT", uri, event)
        unrecognised = (isinstance(body, dict)
                        and body.get("errcode") == "M_UNRECOGNIZED")
        if status == 404 or (status == 400 and unrecognised):
            # A server that does not publish the version 2 endpoint still has
            # version 1, which answers with `[200, {...}]` rather than the body
            # on its own.
            uri = f"{FEDERATION}/v1/send_join/{_esc(room_id)}/{_esc(eid)}"
            status, body = self.request(base_url, destination, "PUT", uri, event)
            if isinstance(body, list) and len(body) == 2:
                body = body[1]
        if status != 200 or not isinstance(body, dict):
            raise FederationError(f"send_join {status}: {str(body)[:400]}")
        return self._absorb_join(room_id, version, event, eid, user_id, body)

    def _absorb_join(self, room_id: str, version: str, event: dict, eid: str,
                     user_id: str, body: dict) -> dict:
        """Take the room's state out of a send_join response and stand in it."""
        room = self.rooms.get(room_id)
        if room is None:
            room = Room(room_id=room_id, version=version)
            self.rooms[room_id] = room
        state: dict[tuple[str, str], str] = {}
        for pdu in (body.get("auth_chain") or []) + (body.get("state") or []):
            self._remember(pdu, version)
        for pdu in body.get("state") or []:
            if "state_key" in pdu:
                state[(pdu["type"], pdu["state_key"])] = event_id(pdu, version)
        # Our own membership event is part of the state after the join whether
        # or not the resident server echoed it back inside `state`.
        self._remember(event, version)
        state[("m.room.member", user_id)] = eid
        room.state = state
        room.depth = max(int(event.get("depth") or 1),
                         max((int(self.events[i].get("depth") or 1)
                              for i in state.values() if i in self.events),
                             default=1))
        room.tips = [eid]
        self.state_after[eid] = dict(state)
        return {"event_id": eid, "event": event, "room_version": version,
                "state": body.get("state") or [],
                "auth_chain": body.get("auth_chain") or []}

    def _fresh_event_id(self) -> str:
        with self._lock:
            self._txn += 1
            return f"${self._txn}-{int(time.time() * 1000)}:{self.server_name}"

    def _remember(self, pdu: dict, version: str) -> None:
        try:
            self.events[event_id(pdu, version)] = pdu
        except Exception:                                        # noqa: BLE001
            pass

    # -- building and pushing events --------------------------------------- #
    def build_event(self, room_id: str, *, etype: str, sender: str,
                    content: dict, state_key: str | None = None,
                    prev_events: list[str] | None = None,
                    auth_events: list[str] | None = None,
                    depth: int | None = None,
                    origin_server_ts: int | None = None,
                    sign: bool = True) -> tuple[str, dict]:
        """Assemble, hash and sign one PDU.  Returns (event id, event)."""
        room = self.rooms[room_id]
        version = room.version
        prevs = list(prev_events if prev_events is not None else room.tips)
        auths = list(auth_events if auth_events is not None
                     else self.auth_events_for(room_id, etype, sender, state_key,
                                               content, room.state))
        if depth is None:
            depth = 1 + max((int(self.events[p].get("depth") or 1)
                             for p in prevs if p in self.events), default=room.depth)
        event: dict[str, Any] = {
            "room_id": room_id,
            "sender": sender,
            "type": etype,
            "content": content,
            "depth": depth,
            "origin": self.server_name,
            "origin_server_ts": (origin_server_ts if origin_server_ts is not None
                                 else int(time.time() * 1000)),
            "prev_events": _refs(prevs, version, self.events),
            "auth_events": _refs(auths, version, self.events),
        }
        if state_key is not None:
            event["state_key"] = state_key
        if event_format_version(version) == 1:
            event["event_id"] = self._fresh_event_id()
        if sign:
            add_hashes_and_signatures(event, version, self.server_name,
                                      self.KEY_ID, self.signing_key)
        eid = event_id(event, version)
        self.events[eid] = event
        return eid, event

    def auth_events_for(self, room_id: str, etype: str, sender: str,
                        state_key: str | None, content: dict,
                        state: dict[tuple[str, str], str]) -> list[str]:
        """The `auth_events` an event of this shape takes from a room state.

        The set of keys is the one the authorisation rules consult: the create
        event, the power levels, the sender's membership, and -- for a membership
        event -- the join rules and the target's membership.  A receiving server
        checks that the event's `auth_events` are exactly drawn from this set, so
        anything else here would have the event refused.
        """
        keys = [("m.room.create", ""), ("m.room.power_levels", ""),
                ("m.room.member", sender)]
        if etype == "m.room.member":
            membership = (content or {}).get("membership")
            if membership in ("join", "invite", "knock"):
                keys.append(("m.room.join_rules", ""))
            if state_key is not None:
                keys.append(("m.room.member", state_key))
            if membership == "invite":
                tpi = (content or {}).get("third_party_invite") or {}
                token = ((tpi.get("signed") or {}).get("token"))
                if token:
                    keys.append(("m.room.third_party_invite", token))
            authorised_via = (content or {}).get("join_authorised_via_users_server")
            if membership == "join" and authorised_via:
                keys.append(("m.room.member", authorised_via))
        out, seen = [], set()
        for key in keys:
            eid = state.get(key)
            if eid and eid not in seen:
                seen.add(eid)
                out.append(eid)
        return out

    def send_transaction_raw(self, base_url: str, destination: str,
                             pdus: list[dict], edus: list[dict] | None = None,
                             *, txn_id: str | None = None):
        """Push PDUs at another server and return `(status, body)` as they came.

        `send_transaction` is the convenience form: it raises on anything but a
        200 and hands back only the per-PDU map.  A scenario that is *about* the
        transaction endpoint -- replaying a transaction id, or watching a
        request be refused -- needs the status and the whole body, and needs to
        choose the transaction id itself.
        """
        if txn_id is None:
            with self._lock:
                self._txn += 1
                txn_id = f"mxconf{self._txn}-{int(time.time() * 1000)}"
        body: dict[str, Any] = {
            "origin": self.server_name,
            "origin_server_ts": int(time.time() * 1000),
            "pdus": pdus,
        }
        if edus:
            body["edus"] = edus
        uri = f"{FEDERATION}/v1/send/{_esc(txn_id)}"
        return self.request(base_url, destination, "PUT", uri, body)

    def send_transaction(self, base_url: str, destination: str,
                         pdus: list[dict], edus: list[dict] | None = None):
        """Push PDUs at another server.  Returns its per-PDU result map."""
        with self._lock:
            self._txn += 1
            txn_id = f"mxconf{self._txn}-{int(time.time() * 1000)}"
        body: dict[str, Any] = {
            "origin": self.server_name,
            "origin_server_ts": int(time.time() * 1000),
            "pdus": pdus,
        }
        if edus:
            body["edus"] = edus
        uri = f"{FEDERATION}/v1/send/{_esc(txn_id)}"
        status, resp = self.request(base_url, destination, "PUT", uri, body)
        if status != 200:
            raise FederationError(f"send transaction {status}: {str(resp)[:400]}")
        return (resp or {}).get("pdus") or {}


# ---------------------------------------------------------------------------#


def _esc(segment: str) -> str:
    return urllib.parse.quote(segment, safe="")


def _ref_ids(refs) -> list[str]:
    """Event ids out of either reference format."""
    out = []
    for ref in refs or []:
        if isinstance(ref, str):
            out.append(ref)
        elif isinstance(ref, (list, tuple)) and ref:
            out.append(ref[0])
    return out


def _refs(ids: list[str], room_version: str, store: dict[str, dict]):
    """Event references in the room version's format.

    Room versions 1 and 2 carry `[event_id, {"sha256": ...}]` pairs; version 3
    dropped the hash, because from that version the event id *is* the hash.
    """
    if event_format_version(room_version) == 2:
        return list(ids)
    out = []
    for eid in ids:
        ev = store.get(eid)
        digest = (ev or {}).get("hashes", {}).get("sha256", "")
        out.append([eid, {"sha256": digest} if digest else {}])
    return out


def pick_port(preferred: int, span: int = 400) -> int:
    """A free TCP port at or after `preferred`, on 127.0.0.1."""
    for port in range(preferred, preferred + span):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                sock.bind(("127.0.0.1", port))
            except OSError:
                continue
        return port
    raise FederationError(f"no free port near {preferred}")


def federation_url(server) -> str:
    """`https://<server name>`, where a homeserver's federation API lives."""
    url = getattr(server, "federation_url", None)
    if url:
        return url
    return "https://" + server.server_name


def port_hint(server) -> int:
    for attr in ("https_port", "tls_port", "http_port", "port"):
        value = getattr(server, attr, 0)
        if value:
            return int(value)
    return 20000
