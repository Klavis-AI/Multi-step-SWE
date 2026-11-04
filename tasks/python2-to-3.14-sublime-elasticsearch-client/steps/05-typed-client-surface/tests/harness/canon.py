# -*- coding: utf-8 -*-
"""Turn whatever a case returned into a JSON-safe, order-preserving structure.

Dict insertion order is preserved and compared: it is observable behaviour on
every interpreter in this ladder, and the task line names dict ordering
explicitly.
"""
import base64
import datetime
import decimal


def _bytes(raw):
    try:
        return {"__bytes__": raw.decode("utf-8")}
    except UnicodeDecodeError:
        return {"__bytes_b64__": base64.b64encode(raw).decode("ascii")}


def _qual(obj):
    cls = type(obj)
    mod = getattr(cls, "__module__", "?")
    return "%s.%s" % (mod, getattr(cls, "__qualname__", cls.__name__))


def canon(obj, depth=0):
    if depth > 12:
        return {"__truncated__": _qual(obj)}
    if obj is None or isinstance(obj, bool):
        return obj
    if isinstance(obj, int):
        return {"__int__": str(obj)} if abs(obj) > 2 ** 53 else obj
    if isinstance(obj, float):
        return {"__float__": repr(obj)}
    if isinstance(obj, decimal.Decimal):
        return {"__decimal__": str(obj)}
    if isinstance(obj, str):
        return obj
    if isinstance(obj, (bytes, bytearray)):
        return _bytes(bytes(obj))
    if isinstance(obj, memoryview):
        return _bytes(obj.tobytes())
    if isinstance(obj, dict):
        return {"__dict__": [[canon(k, depth + 1), canon(v, depth + 1)]
                             for k, v in obj.items()]}
    if isinstance(obj, tuple):
        return {"__tuple__": [canon(x, depth + 1) for x in obj]}
    if isinstance(obj, list):
        return [canon(x, depth + 1) for x in obj]
    if isinstance(obj, (set, frozenset)):
        return {"__set__": sorted(repr(x) for x in obj)}
    if isinstance(obj, datetime.datetime):
        return {"__datetime__": obj.isoformat(),
                "tz": repr(obj.tzinfo) if obj.tzinfo else None}
    if isinstance(obj, datetime.date):
        return {"__date__": obj.isoformat()}
    if isinstance(obj, datetime.time):
        return {"__time__": obj.isoformat()}
    if isinstance(obj, datetime.timedelta):
        return {"__timedelta__": [obj.days, obj.seconds, obj.microseconds]}
    if isinstance(obj, datetime.tzinfo):
        return {"__tzinfo__": str(obj)}
    if isinstance(obj, BaseException):
        return exc(obj)

    handler = _by_name(_qual(obj))
    if handler is not None:
        return handler(obj, depth)
    return {"__repr__": "%s %s" % (_qual(obj), repr(obj))}


def exc(e):
    out = {"__exc__": _qual(e), "str": str(e)}
    args = getattr(e, "args", ())
    if args:
        out["args"] = [canon(a, 8) for a in args]
    # getattr, not hasattr: TransportError.info is a property that indexes
    # self.args[2], and a two-argument TransportError makes it raise IndexError.
    # On Python 2 hasattr() swallowed every exception and returned False; on 3
    # it only swallows AttributeError, so the IndexError would escape and take
    # the whole observation with it.
    for attr in ("status_code", "error", "info", "reason", "response",
                 "errno", "code"):
        try:
            value = getattr(e, attr)
        except Exception:
            continue
        try:
            out[attr] = canon(value, 8)
        except Exception:
            out[attr] = "<uncanonicalisable>"
    sub = getattr(e, "exceptions", None)
    if sub is not None and isinstance(sub, (list, tuple)):
        out["__group__"] = [exc(x) if isinstance(x, BaseException) else canon(x, 8)
                            for x in sub]
    cause = getattr(e, "__cause__", None)
    if cause is not None:
        out["__cause__"] = exc(cause)
    return out


# ---------------------------------------------------------------------------
# Handlers for the concrete object types the interface hands back. Keyed by
# fully qualified name so this module never imports the tree.

def _resp_requests(obj, depth):
    return {"__type__": "requests.Response",
            "status_code": obj.status_code,
            "reason": obj.reason,
            "url": obj.url,
            "encoding": obj.encoding,
            "headers": [[k.lower(), v] for k, v in sorted(obj.headers.items())],
            "content": _bytes(obj.content),
            "history": [canon(h, depth + 1) for h in obj.history],
            "is_redirect": bool(obj.is_redirect),
            "cookies": sorted((c.name, c.value) for c in obj.cookies),
            "apparent_encoding": obj.apparent_encoding,
            }


def _resp_urllib3(obj, depth):
    return {"__type__": "urllib3.HTTPResponse",
            "status": obj.status,
            "reason": obj.reason,
            "headers": [[k.lower(), v] for k, v in sorted(dict(obj.headers).items())],
            "data": _bytes(obj.data),
            }


_HANDLERS = {
    "requests.models.Response": _resp_requests,
    "urllib3.response.HTTPResponse": _resp_urllib3,
    "requests.packages.urllib3.response.HTTPResponse": _resp_urllib3,
}


def _by_name(qual):
    return _HANDLERS.get(qual)
