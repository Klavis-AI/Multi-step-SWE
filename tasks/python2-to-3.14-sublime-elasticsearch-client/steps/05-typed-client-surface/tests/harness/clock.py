# -*- coding: utf-8 -*-
"""Frozen clock, installed identically at recording and at replay.

Hook 3 of the hook list. Nothing here selects behaviour; it only removes
wall-clock from the observation so an expectation cannot expire
(INSTRUCTIONS trap 10).

The instant is chosen *after* urllib3's hardcoded ``RECENT_DATE``
(``lib/urllib3/connection.py:60`` == ``datetime.date(2014, 1, 1)``) so the
``SystemTimeWarning`` that fires for a clock "way off" fires in neither the
recording nor the replay.
"""
import datetime as _datetime
import time as _time

# 2021-06-15T12:34:56Z
EPOCH = 1623760496.0
ISO = "2021-06-15T12:34:56+00:00"


def install():
    """Freeze ``time`` and ``datetime`` wall-clock readings. Monotonic clocks
    are left alone so timeouts and retry backoff still terminate."""
    _time.time = lambda: EPOCH
    _time.time_ns = lambda: int(EPOCH * 1_000_000_000)

    real = _datetime.datetime
    frozen_naive_utc = real.utcfromtimestamp(EPOCH)

    class FrozenDateTime(real):
        @classmethod
        def now(cls, tz=None):
            if tz is None:
                # Recorder and replayer both run under TZ=UTC, so local == UTC.
                return cls.fromtimestamp(EPOCH)
            return cls.fromtimestamp(EPOCH, tz)

        @classmethod
        def utcnow(cls):
            return cls._from(frozen_naive_utc)

        @classmethod
        def today(cls):
            return cls.fromtimestamp(EPOCH)

        @classmethod
        def _from(cls, d):
            return cls(d.year, d.month, d.day, d.hour, d.minute, d.second,
                       d.microsecond, d.tzinfo)

    class FrozenDate(_datetime.date):
        @classmethod
        def today(cls):
            d = real.utcfromtimestamp(EPOCH).date()
            return cls(d.year, d.month, d.day)

    _datetime.datetime = FrozenDateTime
    _datetime.date = FrozenDate
