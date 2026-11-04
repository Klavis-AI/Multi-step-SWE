"""Canonicalization of Python/NumPy/pandas values into comparable JSON.

Applied identically when recording against the baseline and when replaying
against a candidate tree, so a comparison is exact up to the float tolerance
declared here and nowhere else.
"""
from __future__ import print_function, absolute_import, division

import hashlib
import math

import numpy as np
import pandas as pd

# An array larger than this is stored as summary statistics plus a bounded
# sample rather than element-wise: element-wise storage of a 500k-element frame
# makes the corpus unreadable without making the comparison any stronger.
FULL_ARRAY_LIMIT = 2000
SAMPLE_HEAD = 200
SAMPLE_TAIL = 200


def _is_float(x):
    return isinstance(x, (float, np.floating))


def _is_int(x):
    return isinstance(x, (int, np.integer)) and not isinstance(x, bool)


def _f(x):
    """A float in a form json can round-trip, NaN and infinities included."""
    return float(x)


def _dtype_str(dt):
    try:
        return str(dt)
    except Exception:
        return repr(dt)


def _scalar(x):
    if x is None:
        return {"t": "none"}
    if isinstance(x, (bool, np.bool_)):
        return {"t": "bool", "v": bool(x)}
    if _is_int(x):
        return {"t": "int", "v": int(x)}
    if _is_float(x):
        return {"t": "float", "v": _f(x)}
    if isinstance(x, (complex, np.complexfloating)):
        return {"t": "complex", "re": _f(x.real), "im": _f(x.imag)}
    if isinstance(x, (str,)):
        return {"t": "str", "v": x}
    if isinstance(x, bytes):
        return {"t": "bytes", "n": len(x),
                "sha256": hashlib.sha256(x).hexdigest()}
    return None


def _array_stats(a):
    """Tolerance-comparable summary of a numeric array."""
    out = {}
    try:
        flat = np.asarray(a).ravel()
    except Exception:
        return out
    if flat.size == 0:
        return out
    if flat.dtype.kind in "fc":
        finite = flat[np.isfinite(flat)]
        out["n_nan"] = int(np.count_nonzero(np.isnan(flat)))
        out["n_inf"] = int(np.count_nonzero(np.isinf(flat)))
        if finite.size:
            out["sum"] = _f(np.sum(finite))
            out["mean"] = _f(np.mean(finite))
            out["min"] = _f(np.min(finite))
            out["max"] = _f(np.max(finite))
            out["absmax"] = _f(np.max(np.abs(finite)))
    elif flat.dtype.kind in "iub":
        out["sum"] = int(np.sum(flat.astype("int64")))
        out["min"] = int(np.min(flat))
        out["max"] = int(np.max(flat))
    return out


def _array(a):
    a = np.asarray(a)
    node = {"t": "ndarray", "shape": list(a.shape), "dtype": _dtype_str(a.dtype)}
    flat = a.ravel()
    if flat.size <= FULL_ARRAY_LIMIT:
        node["v"] = [_value(x) for x in flat.tolist()] if a.dtype == object \
            else _flat_numeric(flat)
        return node
    node["stats"] = _array_stats(a)
    head = flat[:SAMPLE_HEAD]
    tail = flat[-SAMPLE_TAIL:]
    node["head"] = _flat_numeric(head) if a.dtype != object else [_value(x) for x in head.tolist()]
    node["tail"] = _flat_numeric(tail) if a.dtype != object else [_value(x) for x in tail.tolist()]
    return node


def _flat_numeric(flat):
    kind = flat.dtype.kind
    if kind in "fc":
        return [_f(x) for x in flat.tolist()]
    if kind in "iu":
        return [int(x) for x in flat.tolist()]
    if kind == "b":
        return [bool(x) for x in flat.tolist()]
    return [_value(x) for x in flat.tolist()]


def _index(ix):
    node = {"t": "index", "dtype": _dtype_str(ix.dtype), "name": _value(ix.name),
            "n": int(len(ix))}
    if len(ix) <= FULL_ARRAY_LIMIT:
        node["v"] = [_value(x) for x in list(ix)]
    else:
        node["head"] = [_value(x) for x in list(ix[:SAMPLE_HEAD])]
        node["tail"] = [_value(x) for x in list(ix[-SAMPLE_TAIL:])]
    return node


def _series(s):
    return {
        "t": "series",
        "name": _value(s.name),
        "dtype": _dtype_str(s.dtype),
        "n": int(len(s)),
        "index": _index(s.index),
        "values": _array(np.asarray(s.values)),
    }


def _frame(df):
    return {
        "t": "dataframe",
        "shape": list(df.shape),
        "columns": _index(df.columns),
        "index": _index(df.index),
        "dtypes": [_dtype_str(d) for d in df.dtypes],
        "cols": [_array(np.asarray(df[c].values)) for c in df.columns],
    }


def _value(x, _depth=0):
    if _depth > 8:
        return {"t": "depth-limit"}
    s = _scalar(x)
    if s is not None:
        return s
    if isinstance(x, np.dtype):
        return {"t": "dtype", "v": _dtype_str(x)}
    if isinstance(x, np.ndarray):
        return _array(x)
    if isinstance(x, pd.DataFrame):
        return _frame(x)
    if isinstance(x, pd.Series):
        return _series(x)
    if isinstance(x, pd.Index):
        return _index(x)
    if isinstance(x, dict):
        items = [(repr(k), k, v) for k, v in x.items()]
        items.sort(key=lambda kv: kv[0])
        return {"t": "dict",
                "v": [[_value(k, _depth + 1), _value(v, _depth + 1)]
                      for _, k, v in items]}
    if isinstance(x, (set, frozenset)):
        vals = sorted(x, key=repr)
        return {"t": "set", "v": [_value(v, _depth + 1) for v in vals]}
    if isinstance(x, tuple):
        node = {"t": "tuple", "v": [_value(v, _depth + 1) for v in x]}
        fields = getattr(x, "_fields", None)
        if fields:
            node["fields"] = list(fields)
        return node
    if isinstance(x, list):
        return {"t": "list", "v": [_value(v, _depth + 1) for v in x]}
    if isinstance(x, slice):
        return {"t": "slice", "v": [_value(x.start), _value(x.stop), _value(x.step)]}
    if isinstance(x, type):
        return {"t": "type", "v": x.__name__}
    if hasattr(x, "get_params") and hasattr(x, "fit"):
        try:
            params = x.get_params(deep=False)
        except Exception:
            params = {}
        return {"t": "estimator", "cls": type(x).__name__,
                "params": _value(params, _depth + 1)}
    if isinstance(x, BaseException):
        return {"t": "exception", "cls": type(x).__name__, "msg": str(x)}
    if hasattr(x, "__iter__") and not hasattr(x, "__len__"):
        # generators and other one-shot iterables
        try:
            return {"t": "iter", "v": [_value(v, _depth + 1) for v in list(x)[:500]]}
        except Exception:
            pass
    return {"t": "opaque", "cls": type(x).__name__}


def canon(x):
    """Public entry point."""
    return _value(x)


# --------------------------------------------------------------------------
# comparison


class Mismatch(object):
    def __init__(self, path, expected, actual, why):
        self.path = path
        self.expected = expected
        self.actual = actual
        self.why = why

    def __repr__(self):
        return "%s: %s (expected %r, got %r)" % (self.path, self.why,
                                                 self.expected, self.actual)

    def as_dict(self):
        return {"path": self.path, "why": self.why,
                "expected": _trunc(self.expected), "actual": _trunc(self.actual)}


def _trunc(v, n=160):
    s = repr(v)
    return s if len(s) <= n else s[:n] + "..."


def floats_equal(a, b, rtol, atol):
    if a is None or b is None:
        return a is b
    try:
        af, bf = float(a), float(b)
    except (TypeError, ValueError):
        return False
    if math.isnan(af) and math.isnan(bf):
        return True
    if math.isnan(af) or math.isnan(bf):
        return False
    if math.isinf(af) or math.isinf(bf):
        return af == bf
    return abs(af - bf) <= atol + rtol * abs(bf)


def compare(expected, actual, rtol=1e-7, atol=1e-10, path="$", out=None):
    """Collect every mismatch between two canonicalized values."""
    if out is None:
        out = []
    if type(expected) is not type(actual) and not (
            isinstance(expected, (int, float)) and isinstance(actual, (int, float))):
        out.append(Mismatch(path, type(expected).__name__, type(actual).__name__,
                            "kind"))
        return out
    if isinstance(expected, dict):
        ek, ak = set(expected), set(actual)
        if ek != ak:
            out.append(Mismatch(path, sorted(ek), sorted(ak), "keys"))
            return out
        for k in sorted(ek):
            compare(expected[k], actual[k], rtol, atol, "%s.%s" % (path, k), out)
        return out
    if isinstance(expected, list):
        if len(expected) != len(actual):
            out.append(Mismatch(path, len(expected), len(actual), "length"))
            return out
        for i, (e, a) in enumerate(zip(expected, actual)):
            compare(e, a, rtol, atol, "%s[%d]" % (path, i), out)
        return out
    if isinstance(expected, bool) or isinstance(actual, bool):
        if bool(expected) != bool(actual):
            out.append(Mismatch(path, expected, actual, "bool"))
        return out
    if isinstance(expected, float) or isinstance(actual, float):
        if not floats_equal(actual, expected, rtol, atol):
            out.append(Mismatch(path, expected, actual, "float"))
        return out
    if expected != actual:
        out.append(Mismatch(path, expected, actual, "value"))
    return out
