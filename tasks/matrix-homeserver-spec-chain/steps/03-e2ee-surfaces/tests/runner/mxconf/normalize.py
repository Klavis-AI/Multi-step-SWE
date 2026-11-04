"""Normalise Matrix HTTP transcripts so two implementations can be compared.

A Matrix homeserver mints room ids, event ids, access tokens, device ids, sync
tokens, media ids and timestamps.  None of those values can be equal across two
implementations, but their *structure and relationships* must be.  This module
replaces every volatile value with a stable symbol allocated in first-appearance
order, so that a transcript recorded against the reference server and the same
transcript replayed against a candidate compare equal exactly when the candidate
behaved the same way.

Volatile classes and how they are recognised:

  ROOM     values matching the room-id grammar          !opaque:server
  EVENT    values matching the event-id grammar         $opaque[:server]
  MXC      values matching the media-id grammar         mxc://server/opaque
  TOKEN    values of access_token / refresh_token
  DEVICE   values of device_id / devices[].device_id
  BATCH    values of next_batch / prev_batch / start / end / from / to / pos /
           next_token / next_batch_token
  SESSION  values of session (user-interactive auth, key backup)
  FILTER   values of filter_id
  KEYID    "ed25519:opaque" style key identifiers
  TS       integers under timestamp-ish keys
  IP       last_seen_ip
  OPAQUE   signatures / hashes subtrees, replaced wholesale

Everything else - user ids we chose, event types, membership, error codes,
content bodies - is compared verbatim.
"""

from __future__ import annotations

import json
import re
from typing import Any

# A server name is a DNS name / IPv4 / bracketed IPv6, optionally with a port.
_SERVER = r"(?:[A-Za-z0-9.-]+|\[[0-9A-Fa-f:.]+\])(?::[0-9]{1,5})?"
# Room ids and (pre-v3) event ids are "sigil + opaque localpart + ':' + server".
# From room version 3 an event id is "$" + unpadded base64 with no server part.
ROOM_RE = re.compile(r"^![^:\s]{1,255}:" + _SERVER + r"$")
EVENT_RE = re.compile(
    r"^\$(?:[A-Za-z0-9+/=_-]{1,255}|[^:\s]{1,255}:" + _SERVER + r")$"
)
MXC_RE = re.compile(r"^mxc://[^/]+/[A-Za-z0-9_=-]+$")
EMBEDDED_ID_RE = re.compile(
    r"![^:\s\"]{1,255}:" + _SERVER
    + r"|\$[A-Za-z0-9+/=_-]{20,255}"
    + r"|mxc://[^/\s\"]+/[A-Za-z0-9_=-]+"
)
KEYID_RE = re.compile(r"^(ed25519|curve25519|signed_curve25519):[A-Za-z0-9_+/=.-]+$")

TOKEN_KEYS = {"access_token", "refresh_token"}
DEVICE_KEYS = {"device_id"}
# Query parameters whose value is a number the scenario chose, never something
# the server minted.  A server whose pagination token happens to be the string
# "2" would otherwise teach the symboliser that every 2 is a token, and
# `?limit=2` would normalise to `?limit=<BATCH1>`.
NUMERIC_QUERY_KEYS = {
    "limit", "timeout", "width", "height", "depth", "max_depth", "score",
    "from_ms", "to_ms", "recurse", "ver",
}


BATCH_KEYS = {
    "next_batch", "prev_batch", "start", "end", "from", "to", "pos", "since",
    "next_token", "prev_token", "next_batch_token",
}
SESSION_KEYS = {"session"}
# A key backup's `version` is "an opaque string" the server chooses: the
# reference counts from "1", another server may mint a UUID, and both conform.
# It is symbolised so that the identity of a version is compared rather than
# its spelling -- in bodies, in `?version=`, and in `/room_keys/version/{v}`.
BACKUP_KEYS = {"version"}
FILTER_KEYS = {"filter_id"}
TS_KEYS = {
    "origin_server_ts", "age", "last_seen_ts", "valid_until_ts", "expires_in_ms",
    "expires_at", "ts", "updated_at", "received_ts", "last_active_ago",
    "retry_after_ms", "age_ts", "created_ts", "expires_ts",
    # POST /_matrix/media/v1/create: a wall-clock deadline for the reserved id.
    "unused_expires_at",
}
IP_KEYS = {"last_seen_ip"}
OPAQUE_SUBTREE_KEYS = {"signatures", "hashes"}
# Keys whose value is an identifier the scenario itself creates, in an order
# both implementations must share.  Symbols are allocated from these and from
# object keys that are themselves identifiers; nothing else.
ID_KEYS = {"room_id", "event_id", "content_uri", "replacement_room", "mxc",
           "avatar_url", "url", "media_id"}
# Subtrees an implementation may shape differently, so they never allocate.
NO_LEARN_SUBTREES = {"unsigned", "prev_content", "redacted_because",
                     "auth_events", "prev_events", "auth_chain"}

# Arrays whose order the specification does not fix.  Keyed by the last path
# component; the full dotted path is matched with a suffix test so the same
# rule covers /sync, /messages and /rooms/{roomId}/state alike.
UNORDERED_SUFFIXES = (
    "state.events",
    "account_data.events",
    "presence.events",
    "ephemeral.events",
    "invite_state.events",
    "knock_state.events",
    "to_device.events",
    "device_lists.changed",
    "device_lists.left",
    "device_unused_fallback_key_types",
    "versions",
    "flows",
    "joined_rooms",
    "stages",
    "completed",
    "room_types",
    "allowed_room_ids",
    "unstable_room_versions",
    "servers",
    "auth_chain",
    "pdus",
    "edus",
    "auth_chain_ids",
    "pdu_ids",
    "results",
)
# Whole-response arrays that are unordered, keyed by "<METHOD> <template>".
UNORDERED_BY_OP = {
    "GET /_matrix/client/v3/rooms/{roomId}/state": "",
    "GET /_matrix/client/v3/rooms/{roomId}/members": "chunk",
    "GET /_matrix/client/v3/publicRooms": "chunk",
    "POST /_matrix/client/v3/publicRooms": "chunk",
    "GET /_matrix/client/v3/devices": "devices",
    "GET /_matrix/client/v3/joined_rooms": "joined_rooms",
    "GET /_matrix/client/v3/login": "flows",
    "GET /_matrix/client/v1/rooms/{roomId}/hierarchy": "rooms",
}


class Symbols:
    """First-appearance symbol table, shared by one scenario run."""

    def __init__(self) -> None:
        self._map: dict[tuple[str, str], str] = {}
        self._count: dict[str, int] = {}
        self._embedded: dict[str, str] = {}
        self.aliases: dict[str, str] = {}
        self.tokens: set[str] = set()
        self.devices: set[str] = set()
        self.batches: set[str] = set()
        self.sessions: set[str] = set()
        self.filters: set[str] = set()
        self.backups: set[str] = set()

    def sym(self, kind: str, value: str) -> str:
        key = (kind, value)
        if key not in self._map:
            n = self._count.get(kind, 0) + 1
            self._count[kind] = n
            self._map[key] = f"<{kind}{n}>"
            # Only long, opaque values are substituted inside a larger string:
            # a short id (a filter id is "0") would match everywhere.
            if len(value) >= 12:
                self._embedded[value] = self._map[key]
        return self._map[key]

    def substitute(self, text: str) -> str:
        """Symbolise ids that appear inside a larger string.

        A `/sync` filter is a JSON document in a query string and may name a
        room; a body may quote an event id.  Those have to become the same
        symbols as when they appear as values in their own right.
        """
        if len(text) > 8:
            for literal, replacement in sorted(self._embedded.items(),
                                               key=lambda kv: -len(kv[0])):
                if literal in text:
                    text = text.replace(literal, replacement)
            text = EMBEDDED_ID_RE.sub(
                lambda m: self.sym(
                    "ROOM" if m.group(0)[0] == "!" else
                    ("MXC" if m.group(0)[0] == "m" else "EVENT"),
                    m.group(0)),
                text)
        for literal, replacement in self.aliases.items():
            if literal in text:
                text = text.replace(literal, replacement)
        return text

    # ---- learning: called on responses so later requests symbolise too ---- #
    def learn(self, obj: Any, _depth: int = 0) -> None:
        """Walk a response and give every identifier its symbol now.

        Symbols are allocated in first-appearance order, and `learn` runs on
        *every* response including the unchecked ones a scenario uses for
        setup.  That matters: a room created by the second `createRoom` call
        must be `<ROOM2>` on both implementations, and it would not be if the
        first time the harness saw it were inside a `/sync` whose room map two
        servers happen to order differently.
        """
        if isinstance(obj, dict):
            for k in sorted(obj):
                v = obj[k]
                if k in OPAQUE_SUBTREE_KEYS or k in NO_LEARN_SUBTREES:
                    # Server-specific bookkeeping: allocating symbols from here
                    # would make the numbering depend on which optional fields
                    # an implementation happens to include.
                    continue
                if isinstance(k, str) and _depth == 0:
                    if ROOM_RE.match(k):
                        self.sym("ROOM", k)
                    elif EVENT_RE.match(k):
                        self.sym("EVENT", k)
                if isinstance(v, str):
                    # Values a later request will quote back: they are
                    # recognised by the key that produced them.
                    if k in TOKEN_KEYS:
                        self.tokens.add(v)
                    elif k in DEVICE_KEYS:
                        self.devices.add(v)
                    elif k in BATCH_KEYS:
                        self.batches.add(v)
                    elif k in SESSION_KEYS:
                        self.sessions.add(v)
                    elif k in FILTER_KEYS:
                        self.filters.add(v)
                    elif k in BACKUP_KEYS:
                        self.backups.add(v)
                if isinstance(v, str) and k in ID_KEYS and _depth == 0:
                    if MXC_RE.match(v):
                        # A media id also appears on its own as a path segment.
                        self.sym("MXC", v)
                        self.sym("MEDIA", v.rsplit("/", 1)[-1])
                    elif ROOM_RE.match(v):
                        self.sym("ROOM", v)
                    elif EVENT_RE.match(v):
                        self.sym("EVENT", v)
                    if k in TOKEN_KEYS:
                        self.tokens.add(v)
                    elif k in DEVICE_KEYS:
                        self.devices.add(v)
                    elif k in BATCH_KEYS:
                        self.batches.add(v)
                    elif k in SESSION_KEYS:
                        self.sessions.add(v)
                    elif k in FILTER_KEYS:
                        self.filters.add(v)
                    elif k in BACKUP_KEYS:
                        self.backups.add(v)
                self.learn(v, _depth + 1)
        elif isinstance(obj, list):
            for v in obj:
                self.learn(v, _depth + 1)

    # ---- classification --------------------------------------------------- #
    def classify(self, value: str, key: str | None) -> str | None:
        if key in TOKEN_KEYS or value in self.tokens:
            return "TOKEN"
        if key in DEVICE_KEYS or value in self.devices:
            return "DEVICE"
        if key in BATCH_KEYS or value in self.batches:
            return "BATCH"
        if key in SESSION_KEYS or value in self.sessions:
            return "SESSION"
        if key in FILTER_KEYS or value in self.filters:
            return "FILTER"
        if key in BACKUP_KEYS or value in self.backups:
            return "BACKUP"
        if key in IP_KEYS:
            return "IP"
        if ROOM_RE.match(value):
            return "ROOM"
        if EVENT_RE.match(value):
            return "EVENT"
        if MXC_RE.match(value):
            return "MXC"
        if KEYID_RE.match(value):
            return "KEYID"
        return None

    def scalar(self, value: Any, key: str | None) -> Any:
        if isinstance(value, bool) or value is None:
            return value
        if isinstance(value, int) and key in TS_KEYS:
            return "<TS>"
        if isinstance(value, float):
            return "<FLOAT>"
        if not isinstance(value, str):
            return value
        kind = self.classify(value, key)
        if kind is not None:
            return self.sym(kind, value)
        return self.substitute(value)


def _blind(obj: Any, syms: Symbols, key: str | None = None) -> Any:
    """Volatile values -> a constant, for computing order-independent sort keys."""
    if isinstance(obj, dict):
        if key in OPAQUE_SUBTREE_KEYS:
            return "<OPAQUE>"
        out = {}
        for k in sorted(obj):
            bk = "<V>" if syms.classify(k, None) else k
            for literal, replacement in syms.aliases.items():
                if isinstance(bk, str) and literal in bk:
                    bk = bk.replace(literal, replacement)
            out[str(bk)] = _blind(obj[k], syms, k)
        return dict(sorted(out.items()))
    if isinstance(obj, (list, tuple)):
        return [_blind(v, syms, key) for v in obj]
    if isinstance(obj, bool) or obj is None:
        return obj
    if isinstance(obj, int):
        return "<N>" if key in TS_KEYS else obj
    if isinstance(obj, str):
        if syms.classify(obj, key):
            return "<V>"
        for literal, replacement in syms.aliases.items():
            if literal in obj:
                obj = obj.replace(literal, replacement)
        return obj
    return obj


def _sort_key(item: Any, syms: Symbols) -> str:
    return json.dumps(_blind(item, syms), sort_keys=True, ensure_ascii=False)


def _is_unordered(path: str, op_key: str | None) -> bool:
    if op_key and op_key in UNORDERED_BY_OP and UNORDERED_BY_OP[op_key] == path:
        return True
    cleaned = path.replace("[]", "")
    for suffix in UNORDERED_SUFFIXES:
        if cleaned == suffix or cleaned.endswith("." + suffix):
            return True
    return False


def canonicalise(obj: Any, syms: Symbols, op_key: str | None = None,
                 path: str = "", key: str | None = None) -> Any:
    """Sort unordered arrays, then replace every volatile value with a symbol.

    Object *keys* are symbolised too: `/rooms/{roomId}/joined_members` is keyed
    by user id, `/sync` `rooms.join` by room id and `m.room.power_levels`
    `users` by user id, so a key carries the server name just as a value does.
    """
    if isinstance(obj, dict):
        if key in OPAQUE_SUBTREE_KEYS:
            return f"<{key.upper()}>"
        out = {}
        for k in sorted(obj):
            sk = syms.scalar(k, None)
            out[str(sk)] = canonicalise(obj[k], syms, op_key,
                                        f"{path}.{k}" if path else k, k)
        return dict(sorted(out.items()))
    if isinstance(obj, (list, tuple)):
        items = list(obj)
        if _is_unordered(path, op_key):
            items.sort(key=lambda it: _sort_key(it, syms))
        return [
            canonicalise(v, syms, op_key, f"{path}[]" if path else "[]", key)
            for v in items
        ]
    return syms.scalar(obj, key)


# --------------------------------------------------------------------------- #
# Projection onto the fields the published specification declares
# --------------------------------------------------------------------------- #
def project(obj: Any, declared: dict[str, bool] | None, path: str = "") -> Any:
    """Keep only the JSON paths the spec's OpenAPI schema declares.

    `declared` maps dotted paths to a required flag; a path component of "*"
    marks an object the schema leaves free-form, and everything below it is
    kept verbatim.  When `declared` is None (no schema for that status) the
    object is returned untouched.
    """
    if declared is None:
        return obj
    if isinstance(obj, dict):
        out = {}
        for k in sorted(obj):
            child = f"{path}.{k}" if path else k
            star = f"{path}.*" if path else "*"
            if child in declared:
                out[k] = project(obj[k], declared, child)
            elif star in declared:
                # A free-form node: if the schema says nothing about what is
                # inside it, the whole subtree is kept as it stands.  Pruning it
                # to its first level would silently stop comparing nested
                # content such as m.room.power_levels' `events` map.
                deeper = any(d.startswith(star + ".") or d.startswith(star + "[")
                             for d in declared)
                out[k] = project(obj[k], declared if deeper else None, star)
        return out
    if isinstance(obj, list):
        child = f"{path}[]"
        return [project(v, declared, child) for v in obj]
    return obj


def missing_required(obj: Any, declared: dict[str, bool] | None) -> list[str]:
    """Required paths the object does not carry (spec-schema conformance)."""
    if declared is None:
        return []
    missing: list[str] = []
    for dotted, required in sorted(declared.items()):
        if not required or "*" in dotted:
            continue
        if not _has_path(obj, dotted.split(".")):
            parent = dotted.rsplit(".", 1)[0] if "." in dotted else None
            if parent is None or _has_path(obj, parent.split(".")):
                missing.append(dotted)
    return missing


def _has_path(obj: Any, parts: list[str]) -> bool:
    if not parts:
        return obj is not None
    head, rest = parts[0], parts[1:]
    arr = head.endswith("[]")
    name = head[:-2] if arr else head
    if name == "*":
        if not isinstance(obj, dict):
            return False
        return all(_has_path(v, rest) for v in obj.values()) if obj else True
    if not isinstance(obj, dict) or name not in obj:
        return False
    node = obj[name]
    if arr:
        if not isinstance(node, list):
            return False
        return all(_has_path(v, rest) for v in node) if node else True
    return _has_path(node, rest) if rest else node is not None


# --------------------------------------------------------------------------- #
# Error codes and config-dependent fields
# --------------------------------------------------------------------------- #
# `M_UNKNOWN` is the catch-all "an unknown error has occurred", so it never
# pins a behaviour even where an endpoint's published example happens to use it.
UNCOMPARABLE_ERRCODES = {"M_UNKNOWN"}

# "You may not see this" and "there is no such thing" are the same refusal when
# the release does not document the condition.  A homeserver that conceals the
# existence of a room from a caller who cannot see it, and one that says plainly
# that the caller is not allowed, are both inside every release these steps
# target -- the difference is a privacy posture, not a behaviour the
# specification fixes -- so where the status is one the published operation does
# not document, these two codes are recorded as one token on both sides.  Any
# other code still differs from both.
CONCEALABLE_ERRCODES = {"M_FORBIDDEN", "M_NOT_FOUND"}
CONCEALED = "M_FORBIDDEN|M_NOT_FOUND"


# Optional properties whose empty value says exactly what their absence says.
# Two conforming servers differ here -- the recording reference sends
# `"completed": []` and `"params": {}` on a user-interactive-authentication
# challenge, a second independent homeserver omits both -- and the release
# marks them optional, so neither reading may be pinned.
# Optional properties whose stated value says exactly what leaving them out
# says.  `soft_logout` "defaults to false"; `is_guest` likewise; an empty
# `completed` list is an authentication with nothing completed; `params` with
# nothing in it is no parameters.  A server that spells the default out and one
# that leaves it to the default have not answered differently.
EMPTY_IS_ABSENT = {"completed": [], "params": {}, "flows": None,
                   "soft_logout": False, "is_guest": False}


# Maps of "how many keys are left, by algorithm".  "If an algorithm is not
# listed, the count for that algorithm is zero", so a server that lists a zero
# and one that leaves it out have said the same thing -- and which algorithms a
# server chooses to name is not a difference the release fixes.  The zeros are
# dropped from both sides before comparing.
ZERO_COUNT_MAPS = ("one_time_key_counts", "device_one_time_keys_count")


def drop_zero_key_counts(body):
    if not isinstance(body, dict):
        return body
    out = dict(body)
    for k in ZERO_COUNT_MAPS:
        v = out.get(k)
        if isinstance(v, dict):
            out[k] = {a: n for a, n in v.items() if n != 0}
    return out


def drop_empty_optionals(body):
    """Remove optional properties whose value carries no information."""
    if not isinstance(body, dict):
        return body
    out = {}
    for k, v in body.items():
        want = EMPTY_IS_ABSENT.get(k, "\0missing")
        if want != "\0missing" and want is not None and v == want:
            continue
        out[k] = v
    return out


def strip_failure_codes(body):
    """Drop the error codes inside a per-item `failures` map.

    Endpoints that report per-user or per-device trouble in a `failures` object
    -- `/keys/query`, `/keys/claim`, `/keys/signatures/upload` -- describe it
    only as "a map of errors", and no published schema says which code belongs
    to which condition.  The *presence* of a failure for an item is the part the
    release fixes, so that is what stays.
    """
    if not isinstance(body, dict):
        return body
    failures = body.get("failures")
    if not isinstance(failures, dict):
        return body
    def clean(value):
        if isinstance(value, dict):
            return {k: clean(v) for k, v in value.items()
                    if k not in ("errcode", "error")}
        return value
    out = dict(body)
    out["failures"] = {k: clean(v) for k, v in failures.items()}
    return out

# Codes whose meaning the specification fixes unambiguously for the situation
# that produces them ("Common error codes" and "Other error codes" in the
# Client-Server API).  An `errcode` outside this set, and not documented by the
# endpoint's own published example, is not compared: the specification leaves
# the choice to the implementation there, so pinning Synapse's choice would be
# testing Synapse rather than the specification.
COMPARABLE_ERRCODES = {
    "M_FORBIDDEN",
    "M_UNKNOWN_TOKEN",
    "M_MISSING_TOKEN",
    "M_USER_LOCKED",
    "M_USER_SUSPENDED",
    "M_NOT_JSON",
    "M_NOT_FOUND",
    "M_LIMIT_EXCEEDED",
    "M_UNRECOGNIZED",
    "M_USER_IN_USE",
    "M_INVALID_USERNAME",
    "M_ROOM_IN_USE",
    "M_BAD_ALIAS",
    "M_THREEPID_IN_USE",
    "M_THREEPID_NOT_FOUND",
    "M_THREEPID_DENIED",
    "M_TOO_LARGE",
    "M_EXCLUSIVE",
    "M_USER_DEACTIVATED",
    "M_GUEST_ACCESS_FORBIDDEN",
    "M_UNSUPPORTED_ROOM_VERSION",
    "M_INCOMPATIBLE_ROOM_VERSION",
    "M_WRONG_ROOM_KEYS_VERSION",
    "M_UNABLE_TO_AUTHORISE_JOIN",
    "M_UNABLE_TO_GRANT_JOIN",
    "M_CANNOT_OVERWRITE_MEDIA",
    "M_URL_NOT_SET",
    "M_UNAUTHORIZED",
}

# Fields every response may carry whose value depends on deployment
# configuration or on an implementation's bookkeeping rather than on the
# specification, so they are never compared.
# Content keys that pinned Synapse emits but the spec release this step targets
# does not define yet (it implements later proposals).  Set per step by the
# record/verify entry points; a later step removes the entry and starts
# requiring the field.
FUTURE_FIELDS: set[str] = set()

GLOBAL_DROP = (
    "error",             # human-readable text; only `errcode` is specified
    "unstable_features", # per-implementation unstable feature flags
    "well_known",        # depends on the deployment's delegation config
    "home_server",       # deprecated in the Client-Server API
    "last_seen_ip",      # optional device bookkeeping
    "last_seen_ts",
    "retry_after_ms",
)


def _excluded(child: str, key: str, patterns) -> bool:
    for pat in patterns:
        if "." in pat:
            if child == pat or child.endswith("." + pat):
                return True
        elif key == pat:
            return True
    return False


def strip_paths(obj, paths, prefix=""):
    """Remove a set of dotted paths from a response body (recursively)."""
    if isinstance(obj, dict):
        out = {}
        for k, v in obj.items():
            child = f"{prefix}.{k}" if prefix else k
            if (child in paths or k in paths
                    or _excluded(child, k, paths) or _excluded(child, k, FUTURE_FIELDS)):
                continue
            out[k] = strip_paths(v, paths, child)
        return out
    if isinstance(obj, list):
        return [strip_paths(v, paths, f"{prefix}[]") for v in obj]
    return obj


def keep_paths(obj, paths):
    """Keep only the named top-level-ish dotted paths."""
    out = {}
    for dotted in paths:
        parts = dotted.split(".")
        src, dst = obj, out
        okay = True
        for i, part in enumerate(parts):
            if not isinstance(src, dict) or part not in src:
                okay = False
                break
            if i == len(parts) - 1:
                dst[part] = src[part]
            else:
                dst = dst.setdefault(part, {})
                src = src[part]
        if not okay:
            out.setdefault("__absent__", []).append(dotted)
    return out


def drop_null_optionals(obj, declared, path=""):
    """An optional field explicitly set to null is equivalent to omitting it."""
    if declared is None:
        return obj
    if isinstance(obj, dict):
        out = {}
        for k, v in obj.items():
            child = f"{path}.{k}" if path else k
            star = f"{path}.*" if path else "*"
            key = child if child in declared else (star if star in declared else None)
            if v is None and key is not None and not declared.get(key, False):
                continue
            out[k] = drop_null_optionals(v, declared, key or child)
        return out
    if isinstance(obj, list):
        return [drop_null_optionals(v, declared, f"{path}[]") for v in obj]
    return obj


# Response headers this step does not compare, for the same reasons as
# FUTURE_FIELDS: the pinned reference implements a later proposal.
EXCLUDED_HEADERS: set[str] = set()

# Event types a derived list must not carry at this step (see mxconf/steps.py).
EXCLUDED_TYPES: set[str] = set()


def drop_excluded_types(obj, types):
    """Remove excluded event types from a derived list of types or tuples."""
    if not types:
        return obj
    if isinstance(obj, dict):
        return {k: drop_excluded_types(v, types) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        out = []
        for v in obj:
            if isinstance(v, str) and v in types:
                continue
            if isinstance(v, (list, tuple)) and v and v[0] in types:
                continue
            out.append(drop_excluded_types(v, types))
        return out
    return obj
