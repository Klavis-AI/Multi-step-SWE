"""Build a router + declared-field index from a matrix-spec checkout.

The matrix-spec repository ships machine-readable definitions of every
Client-Server and Server-Server endpoint (`data/api/**.yaml`), in Swagger 2.0 up
to spec v1.6 and OpenAPI 3.1 from v1.7.  This module resolves their `$ref`s and
produces, for one spec release:

  * a router: (method, concrete path) -> operation template, and
  * for each operation and response status, the set of dotted JSON paths the
    published schema declares, which of them are `required`, and which subtrees
    are free-form (`additionalProperties`).

Nothing here encodes an expected *value*: it only records which fields the
published specification says exist.  The values come from the recorded oracle.
"""

from __future__ import annotations

import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

FREEFORM = "*"          # marker: subtree is not enumerated by the schema
MAX_DEPTH = 40


# --------------------------------------------------------------------------- #
# $ref resolution
# --------------------------------------------------------------------------- #
class RefResolver:
    def __init__(self, root: Path):
        self.root = root
        self._cache: dict[Path, Any] = {}

    def load(self, path: Path) -> Any:
        path = path.resolve()
        if path not in self._cache:
            with open(path, "r", encoding="utf-8") as fh:
                self._cache[path] = yaml.safe_load(fh)
        return self._cache[path]

    def resolve(self, node: Any, base: Path, depth: int = 0, seen: tuple = ()) -> Any:
        """Recursively inline every $ref. `base` is the file `node` came from."""
        if depth > MAX_DEPTH:
            return {}
        if isinstance(node, list):
            return [self.resolve(n, base, depth + 1, seen) for n in node]
        if not isinstance(node, dict):
            return node

        if "$ref" in node and isinstance(node["$ref"], str):
            ref = node["$ref"]
            rest = {k: v for k, v in node.items() if k != "$ref"}
            target, newbase = self._deref(ref, base)
            key = (str(newbase), ref)
            if key in seen:           # cycle: stop, treat as free-form
                return {"type": "object", "additionalProperties": True}
            resolved = self.resolve(target, newbase, depth + 1, seen + (key,))
            if isinstance(resolved, dict) and rest:
                merged = dict(resolved)
                merged.update(self.resolve(rest, base, depth + 1, seen))
                return merged
            return resolved

        return {k: self.resolve(v, base, depth + 1, seen) for k, v in node.items()}

    def _deref(self, ref: str, base: Path) -> tuple[Any, Path]:
        filepart, _, pointer = ref.partition("#")
        newbase = base if not filepart else (base.parent / filepart).resolve()
        doc = self.load(newbase) if filepart else self.load(base)
        node = doc
        if pointer:
            for tok in pointer.strip("/").split("/"):
                if not tok:
                    continue
                tok = tok.replace("~1", "/").replace("~0", "~")
                if isinstance(node, list):
                    node = node[int(tok)]
                else:
                    node = node.get(tok, {})
        return node, newbase


# --------------------------------------------------------------------------- #
# Declared-field extraction
# --------------------------------------------------------------------------- #
def _walk_schema(schema: Any, prefix: str, out: dict[str, bool], depth: int = 0) -> None:
    """Record dotted paths declared by `schema` into `out` {path: required}."""
    if depth > 25 or not isinstance(schema, dict):
        return

    for combo in ("allOf", "anyOf", "oneOf"):
        for sub in schema.get(combo, []) or []:
            _walk_schema(sub, prefix, out, depth + 1)

    typ = schema.get("type")
    if isinstance(typ, list):
        typ = next((t for t in typ if t != "null"), None)

    if typ == "array" or "items" in schema:
        items = schema.get("items")
        if items is None:
            out[prefix + "[]" if prefix else "[]"] = out.get(prefix, False)
            return
        _walk_schema(items, (prefix + "[]") if prefix else "[]", out, depth + 1)
        return

    props = schema.get("properties")
    addl = schema.get("additionalProperties")
    patt = schema.get("patternProperties")
    if not isinstance(props, dict):
        props = None
    if not isinstance(patt, dict):
        patt = None

    if props is None:
        if addl or patt or typ == "object":
            p = (prefix + "." if prefix else "") + FREEFORM
            out[p] = False
            if isinstance(addl, dict):
                _walk_schema(addl, p, out, depth + 1)
            if isinstance(patt, dict):
                for sub in patt.values():
                    _walk_schema(sub, p, out, depth + 1)
        return

    required = set(schema.get("required") or [])
    for name, sub in props.items():
        p = f"{prefix}.{name}" if prefix else name
        out[p] = name in required
        _walk_schema(sub, p, out, depth + 1)

    if addl is True or isinstance(addl, dict):
        p = (prefix + "." if prefix else "") + FREEFORM
        out[p] = False
        if isinstance(addl, dict):
            _walk_schema(addl, p, out, depth + 1)
    if isinstance(patt, dict):
        p = (prefix + "." if prefix else "") + FREEFORM
        out[p] = False
        for sub in patt.values():
            _walk_schema(sub, p, out, depth + 1)


def _prune_freeform(declared: dict[str, bool]) -> None:
    """Drop a free-form marker when the same object also declares real properties.

    `allOf` branches such as a bare ``{"type": "object"}`` would otherwise make
    every event body free-form and defeat the projection.
    """
    for marker in [k for k in declared if k.endswith(FREEFORM)]:
        prefix = marker[: -len(FREEFORM)]
        siblings = [
            k
            for k in declared
            if k != marker
            and k.startswith(prefix)
            and "." not in k[len(prefix):].lstrip(".")
            and not k[len(prefix):].startswith(FREEFORM)
            and k[len(prefix):] != ""
        ]
        if siblings:
            declared.pop(marker, None)
            for k in [k for k in declared if k.startswith(marker + ".")]:
                declared.pop(k, None)


@dataclass
class Operation:
    method: str
    template: str          # full path template, e.g. /_matrix/client/v3/rooms/{roomId}/state
    regex: "re.Pattern"
    source: str            # yaml file it came from
    # status (as str) -> {dotted path: required}
    responses: dict[str, dict[str, bool]] = field(default_factory=dict)
    # status (as str) -> error codes the published examples document
    errcodes: dict[str, list[str]] = field(default_factory=dict)


def _example_errcodes(resp: Any, swagger2: bool) -> list[str]:
    """Error codes the specification's own examples document for this response."""
    found: list[str] = []

    def take(v: Any) -> None:
        if isinstance(v, dict) and isinstance(v.get("errcode"), str):
            found.append(v["errcode"])

    if not isinstance(resp, dict):
        return found
    if swagger2:
        for v in (resp.get("examples") or {}).values():
            take(v)
    else:
        content = (resp.get("content") or {}).get("application/json") or {}
        take(content.get("example"))
        for ex in (content.get("examples") or {}).values():
            if isinstance(ex, dict):
                take(ex.get("value"))
    return sorted(set(found))


def _path_regex(template: str) -> "re.Pattern":
    parts = re.split(r"(\{[^}]+\})", template)
    out = []
    for p in parts:
        if p.startswith("{") and p.endswith("}"):
            out.append(r"[^/]*")
        else:
            out.append(re.escape(p))
    return re.compile("^" + "".join(out) + "$")


class SpecIndex:
    """All operations of one matrix-spec release."""

    def __init__(self, spec_root: Path, apis=("client-server", "server-server")):
        self.spec_root = Path(spec_root)
        self.ops: list[Operation] = []
        for api in apis:
            d = self.spec_root / "data" / "api" / api
            if not d.is_dir():
                continue
            for yml in sorted(d.glob("*.yaml")):
                try:
                    self._load_file(yml)
                except Exception as exc:          # noqa: BLE001 - index is best effort
                    print(f"[specindex] skip {yml.name}: {exc}", file=sys.stderr)
        self.ops.sort(key=lambda o: (-o.template.count("/"), -len(o.template)))

    # -- loading ----------------------------------------------------------- #
    def _load_file(self, yml: Path) -> None:
        rr = RefResolver(self.spec_root)
        doc = rr.load(yml)
        if not isinstance(doc, dict) or "paths" not in doc:
            return
        swagger2 = "swagger" in doc
        base = doc.get("basePath", "") if swagger2 else ""
        if not swagger2:
            servers = doc.get("servers") or []
            if servers:
                url = servers[0].get("url", "")
                vars_ = servers[0].get("variables") or {}
                for k, v in vars_.items():
                    url = url.replace("{%s}" % k, str(v.get("default", "")))
                base = re.sub(r"^https?://[^/]+", "", url).rstrip("/")
        for path, item in (doc.get("paths") or {}).items():
            if not isinstance(item, dict):
                continue
            for method, op in item.items():
                if method.lower() not in ("get", "post", "put", "delete", "head", "options"):
                    continue
                full = (base or "") + path
                o = Operation(
                    method=method.upper(),
                    template=full,
                    regex=_path_regex(full),
                    source=yml.name,
                )
                for status, resp in (op.get("responses") or {}).items():
                    schema = None
                    if swagger2:
                        schema = resp.get("schema")
                    else:
                        content = (resp or {}).get("content") or {}
                        jsonpart = content.get("application/json") or {}
                        schema = jsonpart.get("schema")
                    declared: dict[str, bool] = {}
                    if schema is not None:
                        schema = rr.resolve(schema, yml)
                        _walk_schema(schema, "", declared)
                        _prune_freeform(declared)
                    o.responses[str(status)] = declared
                    codes = _example_errcodes(resp, swagger2)
                    if codes:
                        o.errcodes[str(status)] = codes
                self.ops.append(o)

    # -- lookup ------------------------------------------------------------ #
    def match(self, method: str, path: str) -> Operation | None:
        method = method.upper()
        path = path.split("?", 1)[0]
        for o in self.ops:
            if o.method == method and o.regex.match(path):
                return o
        if path.endswith("/") and len(path) > 1:
            stripped = path.rstrip("/")
            for o in self.ops:
                if o.method == method and o.regex.match(stripped):
                    return o
        return None

    def declared(self, method: str, path: str, status: int) -> dict[str, bool] | None:
        op = self.match(method, path)
        if op is None:
            return None
        for key in (str(status), f"{status//100}XX", "default"):
            if key in op.responses:
                return op.responses[key]
        return None

    def documented_errcodes(self, method: str, path: str, status: int) -> list[str]:
        op = self.match(method, path)
        if op is None:
            return []
        return op.errcodes.get(str(status), []) or op.errcodes.get(
            f"{status // 100}XX", []
        )


    def documented_statuses(self, method: str, path: str) -> set:
        op = self.match(method, path)
        return set(op.responses.keys()) if op else set()

    def to_json(self) -> dict:
        return {
            "operations": [
                {
                    "method": o.method,
                    "template": o.template,
                    "source": o.source,
                    "responses": {k: v for k, v in o.responses.items()},
                    "errcodes": o.errcodes,
                }
                for o in self.ops
            ]
        }


class FrozenIndex:
    """SpecIndex re-hydrated from the JSON dump (used inside the verifier)."""

    def __init__(self, data: dict):
        self.ops: list[Operation] = []
        for o in data["operations"]:
            self.ops.append(
                Operation(
                    method=o["method"],
                    template=o["template"],
                    regex=_path_regex(o["template"]),
                    source=o["source"],
                    responses=o["responses"],
                    errcodes=o.get("errcodes", {}),
                )
            )

    match = SpecIndex.match
    declared = SpecIndex.declared
    documented_statuses = SpecIndex.documented_statuses

    def documented_errcodes(self, method: str, path: str, status: int) -> list[str]:
        op = self.match(method, path)
        if op is None:
            return []
        return op.errcodes.get(str(status), []) or op.errcodes.get(
            f"{status // 100}XX", []
        )


def load_frozen(path: str | Path) -> FrozenIndex:
    with open(path, "r", encoding="utf-8") as fh:
        return FrozenIndex(json.load(fh))


if __name__ == "__main__":
    root = Path(sys.argv[1])
    out = Path(sys.argv[2])
    idx = SpecIndex(root)
    out.write_text(json.dumps(idx.to_json(), sort_keys=True), encoding="utf-8")
    print(f"{len(idx.ops)} operations -> {out}")
