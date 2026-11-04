# -*- coding: utf-8 -*-
"""Deterministic execution environment, applied identically at recording and
at replay.

It removes wall-clock, locale and hash-ordering from the observation so that
an expectation cannot expire and cannot drift between interpreters.

The clock is *not* monkeypatched. Freezing ``time.time`` or swapping
``datetime.datetime`` for a subclass would change behaviour rather than only
observe it:

* ``urllib3/util/timeout.py`` binds ``current_time = time.time`` at import, so
  a frozen ``time.time`` makes elapsed time permanently zero and silently
  disables every timeout path the fixtures are meant to exercise.
* Replacing ``datetime.datetime`` with a subclass breaks
  ``isinstance(x, datetime.datetime)`` for any instance built before the swap —
  and ``elasticsearch/serializer.py`` does exactly that test.
* Defining our own ``utcnow`` suppresses the 3.12 ``DeprecationWarning`` that
  the step-03 gate exists to catch.

Instead the clock runs, and every time-derived value is normalised by
``normalize.py``. The recording is taken twice, at different wall-clock
instants, so any unnormalised clock reading shows up as a mismatch and the
case is dropped.
"""
import os


def pin():
    """Call before importing anything from the tree under test."""
    os.environ["TZ"] = "UTC"
    os.environ["LC_ALL"] = "C.UTF-8"
    os.environ["LANG"] = "C.UTF-8"
    os.environ["PYTHONHASHSEED"] = "0"  # advisory here; set for real by the runner
    os.environ["NO_PROXY"] = "*"
    for var in ("HTTP_PROXY", "HTTPS_PROXY", "http_proxy", "https_proxy",
                "REQUESTS_CA_BUNDLE", "CURL_CA_BUNDLE", "SSLKEYLOGFILE"):
        os.environ.pop(var, None)
    try:
        import time
        time.tzset()
    except AttributeError:  # pragma: no cover - non-POSIX
        pass
