#!/usr/bin/env python3
"""The path-expression evaluator the conformance runner uses to check `_sort`."""
import re

# ---------- a tiny FHIRPath subset, enough for SearchParameter.expression ----------

def _split_top(expr, sep):
    out, depth, cur = [], 0, ""
    i = 0
    while i < len(expr):
        ch = expr[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if depth == 0 and expr.startswith(sep, i):
            out.append(cur)
            cur = ""
            i += len(sep)
            continue
        cur += ch
        i += 1
    out.append(cur)
    return out


def _steps(term):
    """Split 'a.b.where(x='y').c' into navigation steps."""
    out, depth, cur, q = [], 0, "", False
    for ch in term:
        if ch == "'":
            q = not q
        if not q:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == "." and depth == 0:
                out.append(cur)
                cur = ""
                continue
        cur += ch
    out.append(cur)
    return [s for s in out if s]


def evaluate(res, expr):
    """Return the list of JSON values the expression selects."""
    vals = []
    for term in _split_top(expr, "|"):
        term = term.strip()
        while term.startswith("(") and term.endswith(")"):
            term = term[1:-1].strip()
        cast = None
        m = re.search(r"\s+as\s+([A-Za-z]+)\s*$", term)
        if m:
            cast = m.group(1)
            term = term[: m.start()].strip()
        cur = [res]
        steps = _steps(term)
        if not steps:
            continue
        if steps[0][:1].isupper() and "(" not in steps[0]:
            if steps[0] != res.get("resourceType"):
                continue
            steps = steps[1:]
        for st in steps:
            nxt = []
            fn = re.match(r"^([A-Za-z]+)\((.*)\)$", st)
            if fn and fn.group(1) == "where":
                cond = fn.group(2)
                cm = re.match(r"^\s*([A-Za-z0-9_.]+)\s*=\s*'([^']*)'\s*$", cond)
                for c in cur:
                    if cm and isinstance(c, dict):
                        got = c
                        for seg in cm.group(1).split("."):
                            got = got.get(seg) if isinstance(got, dict) else None
                        if got == cm.group(2):
                            nxt.append(c)
                    elif not cm:
                        nxt.append(c)
                cur = nxt
                continue
            if fn:
                cur = cur if fn.group(1) in ("resolve", "first") else []
                continue
            for c in cur:
                if not isinstance(c, dict):
                    continue
                if st in c:
                    v = c[st]
                elif any(k.startswith(st) and k[len(st):][:1].isupper() for k in c):
                    k = next(k for k in c if k.startswith(st) and k[len(st):][:1].isupper())
                    if cast and k != st + cast[0].upper() + cast[1:]:
                        continue
                    v = c[k]
                else:
                    continue
                nxt.extend(v if isinstance(v, list) else [v])
            cur = nxt
        vals.extend(cur)
    return vals


