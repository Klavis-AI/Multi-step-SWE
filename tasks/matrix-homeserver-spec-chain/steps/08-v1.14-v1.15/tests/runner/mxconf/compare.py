"""Compare a replayed transcript against the recorded reference transcript.

Two relaxations keep the comparison a test of the *specification* rather than a
test of the reference implementation:

* **Extra optional properties are allowed.**  Where the published schema declares
  a property and marks it optional, a candidate that returns it while the
  reference did not is not wrong.  Entries of a free-form map (rooms keyed by
  room id, users keyed by user id) and elements of an array are *not* optional
  properties, so extras there are still differences.
* **An undocumented status code is compared by class.**  Where the published
  operation does not document the status the reference returned, only the
  2xx/4xx/5xx class is compared, because the specification left the choice open.
"""

from __future__ import annotations

import json
from typing import Any

from .normalize import (drop_empty_optionals, drop_zero_key_counts,
                        strip_failure_codes)


def _prune_extras(exp: Any, got: Any, declared: dict | None, path: str = "") -> Any:
    if declared is None:
        return got
    if isinstance(exp, dict) and isinstance(got, dict):
        out = {}
        for k, v in got.items():
            child = f"{path}.{k}" if path else k
            star = f"{path}.*" if path else "*"
            if k in exp:
                key = child if child in declared else (star if star in declared else child)
                out[k] = _prune_extras(exp[k], v, declared, key)
            elif child in declared and not declared.get(child, False):
                continue                      # declared optional property, not returned
                                              # by the reference: allowed either way
            else:
                out[k] = v                    # map entry / required / undeclared
        return out
    if isinstance(exp, list) and isinstance(got, list):
        n = min(len(exp), len(got))
        return ([_prune_extras(exp[i], got[i], declared, path + "[]") for i in range(n)]
                + list(got[n:]))
    return got


def _diff(a: Any, b: Any, path: str = "", out: list[str] | None = None,
          limit: int = 12) -> list[str]:
    if out is None:
        out = []
    if len(out) >= limit:
        return out
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            p = f"{path}.{k}" if path else k
            if k not in a:
                out.append(f"unexpected {p}={json.dumps(b[k])[:100]}")
            elif k not in b:
                out.append(f"missing {p}={json.dumps(a[k])[:100]}")
            else:
                _diff(a[k], b[k], p, out, limit)
            if len(out) >= limit:
                break
        return out
    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append(f"{path or 'body'}: expected {len(a)} items, got {len(b)}")
            return out
        for i, (x, y) in enumerate(zip(a, b)):
            _diff(x, y, f"{path}[{i}]", out, limit)
            if len(out) >= limit:
                break
        return out
    if a != b:
        out.append(f"{path or 'body'}: expected {json.dumps(a)[:100]}, "
                   f"got {json.dumps(b)[:100]}")
    return out


def compare_entry(exp: dict, got: dict, spec=None) -> tuple[bool, list[str]]:
    problems: list[str] = []
    if exp["method"] != got["method"] or exp["path"] != got["path"]:
        problems.append(
            f"request diverged: expected {exp['method']} {exp['path']}, "
            f"got {got['method']} {got['path']}"
        )
        return False, problems

    if exp.get("status_exact", True):
        if exp["status"] != got["status"]:
            problems.append(f"status: expected {exp['status']}, got {got['status']}")
    else:
        if exp["status"] // 100 != got["status"] // 100:
            problems.append(
                f"status: expected a {exp['status'] // 100}xx "
                f"(the reference returned {exp['status']}), got {got['status']}"
            )

    for h, v in (exp.get("headers") or {}).items():
        gv = (got.get("headers") or {}).get(h)
        if gv != v:
            problems.append(f"header {h}: expected {v!r}, got {gv!r}")

    if exp.get("binary") is not None:
        gb = got.get("binary") or {}
        if gb.get("sha256") != exp["binary"]["sha256"]:
            problems.append(
                f"body bytes differ (got {gb.get('len')} bytes, "
                f"expected {exp['binary']['len']})"
            )
    else:
        declared = None
        if spec is not None and exp.get("op"):
            method, _, template = exp["op"].partition(" ")
            probe = template.replace("{", "").replace("}", "")
            declared = spec.declared(method, probe, exp["status"])
        body = _prune_extras(exp.get("body"), got.get("body"), declared)
        if not exp.get("errcode_compared", True) and isinstance(body, dict):
            body = {k: v for k, v in body.items() if k != "errcode"}
        problems.extend(_diff(exp.get("body"), body))

    extra_missing = sorted(
        set(got.get("missing_required") or []) - set(exp.get("missing_required") or [])
    )
    if extra_missing:
        problems.append("missing fields the schema requires: "
                        + ",".join(extra_missing[:6]))
    if got.get("note"):
        problems.append(str(got["note"])[:160])
    return (not problems), problems


def compare_transcripts(expected: list[dict], got: list[dict], spec=None) -> dict:
    # These normalisations are applied to the recording as well as to the
    # response, so a tolerance that is symmetric by construction does not need
    # the expectation to be re-recorded.
    def tolerate(entry):
        body = entry.get("body")
        if not isinstance(body, dict):
            return entry
        return dict(entry,
                    body=drop_zero_key_counts(
                        drop_empty_optionals(strip_failure_codes(body))))

    expected = [tolerate(e) for e in expected]
    got = [tolerate(g) for g in got]
    n = len(expected)
    passed = 0
    details = []
    for i, exp in enumerate(expected):
        if i < len(got):
            ok, problems = compare_entry(exp, got[i], spec)
        else:
            ok, problems = False, ["no response recorded (the scenario stopped earlier)"]
        if ok:
            passed += 1
        else:
            details.append({"i": i, "label": exp["label"], "problems": problems})
    return {
        "checks": n,
        "passed": passed,
        "score": (passed / n) if n else 0.0,
        "failures": details[:20],
    }
