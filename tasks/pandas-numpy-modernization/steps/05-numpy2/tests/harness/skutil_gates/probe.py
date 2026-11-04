"""Gates that need a running interpreter rather than a parsed file.

Four of them, and each answers a question no scan of the source can:

``installed_versions``  what is actually importable in this container. Rule 6
                        says a toolchain the step has retired is installed in
                        neither image, and the only way to know is to ask the
                        interpreter that will run the tests.
``absent_modules``      the same claim from the other side: a retired package
                        must not import at all.
``semantics``           NEP 50 promotion and the pandas Copy-on-Write /
                        string-dtype defaults, read straight off the libraries.
                        A step that claims NumPy 2 but promotes like NumPy 1
                        has not moved, whatever its pins say.
``warning_census``      counted from the behavior recording rather than by
                        re-running anything: the recorder already stores, per
                        case, the warning categories raised from skutil's own
                        frames. "Copy-on-Write default with zero warnings" is
                        that census coming back empty.
"""
from __future__ import print_function, absolute_import, division

import json
import sys


def installed_versions(names):
    """version string per distribution name, or None when not installed."""
    out = {}
    for name in names:
        out[name] = _version(name)
    return out


def _version(name):
    try:
        import importlib.metadata as md            # 3.8+
    except ImportError:
        md = None
    if md is not None:
        try:
            return md.version(name)
        except Exception:
            pass
    try:
        import pkg_resources
        return pkg_resources.get_distribution(name).version
    except Exception:
        pass
    # A distribution can be absent from the metadata and still importable, and
    # for this gate importable is what matters.
    try:
        mod = __import__(name.replace("-", "_"))
        return getattr(mod, "__version__", "present")
    except Exception:
        return None


def absent_modules(names):
    """Which of *names* import here. Empty is the passing answer."""
    present = []
    for name in names:
        try:
            __import__(name)
        except ImportError:
            continue
        except Exception:
            # It imported far enough to fail for some other reason, which
            # still means it is installed.
            present.append(name)
            continue
        present.append(name)
    return present


def semantics():
    """Interpreter-level facts about promotion and pandas defaults.

    Everything here is a one-liner against the library's own API, recorded as
    a plain value so the expected table can be written down and diffed. No
    version strings: a vendored shim that restores NumPy 1 promotion would
    keep the version string and change these.
    """
    out = {"python": "%d.%d" % sys.version_info[:2]}
    try:
        import numpy as np
        out["numpy_version"] = np.__version__
        # NEP 50: under the old rules a python float could not upcast a
        # float32 array, and a python int could not upcast a uint8 one. Under
        # NEP 50 the python scalar is weak and the array's dtype wins, which
        # flips the second of these and not the first.
        out["nep50_f32_plus_pyfloat"] = str((np.float32(1.0) + 1.0).dtype)
        out["nep50_u8_plus_200"] = str(
            _safe(lambda: (np.uint8(1) + 200).dtype))
        out["nep50_u8_array_plus_200"] = str(
            _safe(lambda: (np.zeros(2, dtype=np.uint8) + 200).dtype))
        out["nep50_i64_plus_f32"] = str(
            (np.int64(1) + np.float32(1.0)).dtype)
        out["scalar_repr"] = repr(np.float64(1.5))
        # Which members of the 2.0 expiry list this NumPy still answers to.
        # Empty is NumPy 2; non-empty pins which NumPy 1 it really is,
        # independently of __version__.
        out["surviving_expired_names"] = sorted(
            n for n in ("float_", "complex_", "unicode_", "in1d", "row_stack",
                        "alltrue", "sometrue", "cumproduct", "product",
                        "round_", "trapz", "NaN", "Inf", "infty", "mat",
                        "asfarray", "msort", "issctype")
            if hasattr(np, n))
    except Exception as exc:
        out["numpy_error"] = str(exc)

    try:
        import pandas as pd
        out["pandas_version"] = pd.__version__
        out["copy_on_write"] = _safe(
            lambda: bool(pd.options.mode.copy_on_write))
        out["default_string_dtype"] = str(
            _safe(lambda: pd.Series(["a", "b"]).dtype))
        out["default_int_dtype"] = str(
            _safe(lambda: pd.Series([1, 2]).dtype))
        frame = pd.DataFrame({"a": [1, 2, 3]})
        view = frame["a"]
        frame.loc[0, "a"] = 99
        out["view_tracks_parent"] = bool(view.iloc[0] == 99)
    except Exception as exc:
        out["pandas_error"] = str(exc)

    try:
        import sklearn
        out["sklearn_version"] = sklearn.__version__
    except Exception:
        pass
    return out


def _safe(fn):
    try:
        return fn()
    except Exception as exc:
        return "raised:" + type(exc).__name__


def warning_census(recording_path, categories=None):
    """Warning categories raised from skutil frames, per the recording.

    The recorder filters ``warnings.catch_warnings(record=True)`` down to
    warnings whose filename is inside the package, so a DeprecationWarning
    pandas raises about its own internals never lands here and a
    DeprecationWarning skutil raises about its own code always does.
    """
    with open(recording_path) as fh:
        payload = json.load(fh)
    census = {}
    where = {}
    for rec in payload.get("records", []):
        for name in rec.get("warnings", []):
            if categories and name not in categories:
                continue
            census[name] = census.get(name, 0) + 1
            where.setdefault(name, []).append(rec["id"])
    return {"by_category": census,
            "total": sum(census.values()),
            "examples": dict((k, sorted(v)[:6]) for k, v in where.items())}
