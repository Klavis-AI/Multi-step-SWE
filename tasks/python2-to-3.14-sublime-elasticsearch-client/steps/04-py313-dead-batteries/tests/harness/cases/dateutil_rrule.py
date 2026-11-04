# -*- coding: utf-8 -*-
"""``dateutil.rrule`` -- the RFC 2445 recurrence engine.

This is the single largest module in the vendored dependency set and the one
that breaks earliest on the ladder. Its import block reads:

    from fractions import gcd
    from six import advance_iterator, integer_types
    from six.moves import _thread

``fractions.gcd`` was deprecated at 3.5 and **removed at 3.9**, so the module
is unimportable on every interpreter this task targets; ``six.moves._thread``
and ``advance_iterator`` are the other two pins. That makes ``import
dateutil.rrule`` a real separation signal for the first rung rather than a
detail, and it makes the cases below worth having: once the import is fixed,
the arithmetic underneath it -- integer division against ``gcd``, bitmask
tables, a ``__next__``/``next`` generator shim, a cache guarded by a
``_thread.allocate_lock()`` -- still has to produce the same dates it does now.

Everything here is clock-independent: every rule carries an explicit
``dtstart`` and either ``count`` or ``until``.
"""
import datetime

from ..registry import case

START = datetime.datetime(2015, 6, 15, 9, 30, 0)   # a Monday
UNTIL = datetime.datetime(2016, 6, 15, 9, 30, 0)


def _dates(it, limit=25):
    return [d.isoformat() for d in list(it)[:limit]]


def _try(fn):
    try:
        return {"ok": fn()}
    except Exception as exc:
        return {"raised": type(exc).__name__, "str": str(exc)}


@case("rrule.import-surface")
def _(ctx):
    # The names the module pulls in at import time, observed after the fact.
    # On an interpreter where fractions.gcd is gone this case is the first
    # thing to fail, which is exactly what it is for.
    import dateutil.rrule as rr
    return {"freqs": {n: getattr(rr, n) for n in
                      ("YEARLY", "MONTHLY", "WEEKLY", "DAILY", "HOURLY",
                       "MINUTELY", "SECONDLY")},
            "weekdays": [repr(w) for w in rr.weekdays],
            # ``gcd`` is not probed here, for the same reason
            # ``advance_iterator`` is not: the rung names a category, "gcd
            # comes from math", not a spelling. ``from math import gcd`` and
            # ``import math`` with ``math.gcd(...)`` at the call sites are
            # both correct answers to that, and the second leaves
            # ``dateutil.rrule.gcd`` unbound, so scoring either one makes the
            # other an unpassable inherited test. The arithmetic is still
            # covered: the recurrence cases below drive the gcd-bounded
            # MINUTELY/SECONDLY loops in ``rrule._iter``.
            # ``advance_iterator`` is one of six's shims, and a step that
            # requires six gone has no single right answer for it either --
            # rebinding it to the builtin ``next`` and dropping it outright are
            # both correct.
            "m365range": list(rr.M365RANGE),
            "m366range": list(rr.M366RANGE),
            "wdaymask_head": list(rr.WDAYMASK[:14]),
            "m365mask_len": len(rr.M365MASK),
            "m366mask_len": len(rr.M366MASK),
            "mday365_head": list(rr.MDAY365MASK[:5]),
            "nmday365_head": list(rr.NMDAY365MASK[:5]),
            "easter_lazy": rr.easter is None}


@case("rrule.basic-frequencies")
def _(ctx):
    from dateutil.rrule import (DAILY, HOURLY, MINUTELY, MONTHLY, SECONDLY,
                                WEEKLY, YEARLY, rrule)
    out = {}
    for name, freq in (("YEARLY", YEARLY), ("MONTHLY", MONTHLY),
                       ("WEEKLY", WEEKLY), ("DAILY", DAILY),
                       ("HOURLY", HOURLY), ("MINUTELY", MINUTELY),
                       ("SECONDLY", SECONDLY)):
        out[name] = _dates(rrule(freq, dtstart=START, count=6))
    return out


@case("rrule.interval-and-wkst")
def _(ctx):
    from dateutil.rrule import DAILY, MONTHLY, WEEKLY, YEARLY, rrule
    from dateutil.rrule import SU, MO
    out = {}
    for name, kw in (
            ("daily/3", dict(freq=DAILY, interval=3, count=6)),
            ("weekly/2", dict(freq=WEEKLY, interval=2, count=6)),
            ("monthly/4", dict(freq=MONTHLY, interval=4, count=6)),
            ("yearly/2", dict(freq=YEARLY, interval=2, count=4)),
            ("weekly wkst=SU", dict(freq=WEEKLY, interval=2, count=6,
                                    wkst=SU, byweekday=(MO,))),
            ("weekly wkst=MO", dict(freq=WEEKLY, interval=2, count=6,
                                    wkst=MO, byweekday=(MO,))),
            ("wkst int", dict(freq=WEEKLY, interval=2, count=4, wkst=6)),
    ):
        kw["dtstart"] = START
        out[name] = _dates(rrule(**kw))
    return out


@case("rrule.by-rules")
def _(ctx):
    from dateutil.rrule import DAILY, MONTHLY, WEEKLY, YEARLY, rrule
    from dateutil.rrule import FR, MO, SU, TH, TU
    out = {}
    cases = [
        ("bymonth", dict(freq=YEARLY, count=6, bymonth=(1, 7))),
        ("bymonthday", dict(freq=MONTHLY, count=6, bymonthday=(1, 15))),
        ("bymonthday-neg", dict(freq=MONTHLY, count=6, bymonthday=(-1, -2))),
        ("byyearday", dict(freq=YEARLY, count=6, byyearday=(1, 100, 200,
                                                            365, -1))),
        ("byweekno", dict(freq=YEARLY, count=6, byweekno=(1, 20, 52),
                          byweekday=MO)),
        ("byweekday", dict(freq=WEEKLY, count=6, byweekday=(MO, FR))),
        ("byweekday-nth", dict(freq=MONTHLY, count=6, byweekday=FR(+1))),
        ("byweekday-last", dict(freq=MONTHLY, count=6, byweekday=SU(-1))),
        ("byhour", dict(freq=DAILY, count=6, byhour=(8, 20))),
        ("byminute", dict(freq=DAILY, count=6, byminute=(0, 30),
                          byhour=(9,))),
        ("bysecond", dict(freq=DAILY, count=4, bysecond=(0, 30),
                          byhour=(9,), byminute=(0,))),
        ("bysetpos", dict(freq=MONTHLY, count=6, byweekday=(MO, TU, TH, FR),
                          bysetpos=-1)),
        ("byeaster", dict(freq=YEARLY, count=5, byeaster=0)),
        ("byeaster-offset", dict(freq=YEARLY, count=5, byeaster=(-2, +1))),
    ]
    for name, kw in cases:
        kw["dtstart"] = START
        out[name] = _try(lambda kw=kw: _dates(rrule(**kw)))
    return out


@case("rrule.until-and-count")
def _(ctx):
    from dateutil.rrule import MONTHLY, WEEKLY, rrule
    out = {}
    r = rrule(WEEKLY, dtstart=START, until=UNTIL)
    out["weekly_until_len"] = len(list(r))
    out["weekly_until_head"] = _dates(r, 4)
    out["weekly_until_tail"] = _dates(r)[-3:]
    out["count_len"] = rrule(MONTHLY, dtstart=START, count=5).count()
    out["until_before_start"] = list(rrule(
        WEEKLY, dtstart=START, until=datetime.datetime(2014, 1, 1)))
    out["count_zero"] = list(rrule(WEEKLY, dtstart=START, count=0))
    out["until_is_date"] = _try(lambda: len(list(rrule(
        WEEKLY, dtstart=START, until=datetime.date(2015, 8, 1)))))
    return out


@case("rrule.navigation")
def _(ctx):
    # before/after/between/count/__getitem__/__contains__ are rrulebase's whole
    # public surface, and each has a caching and a non-caching path.
    from dateutil.rrule import DAILY, rrule
    out = {}
    for cache in (False, True):
        r = rrule(DAILY, dtstart=START, count=30, cache=cache)
        probe = datetime.datetime(2015, 6, 20, 9, 30, 0)
        out[str(cache)] = {
            "count": r.count(),
            "before": r.before(probe).isoformat(),
            "before_inc": r.before(probe, inc=True).isoformat(),
            "after": r.after(probe).isoformat(),
            "after_inc": r.after(probe, inc=True).isoformat(),
            "between": _dates(r.between(
                probe, datetime.datetime(2015, 6, 25, 9, 30, 0))),
            "between_inc": _dates(r.between(
                probe, datetime.datetime(2015, 6, 25, 9, 30, 0), inc=True)),
            "getitem": r[0].isoformat(),
            "getitem_neg": r[-1].isoformat(),
            "getslice": _dates(r[2:5]),
            "contains": [probe in r,
                         datetime.datetime(2015, 6, 20, 9, 31) in r],
            "len": len(list(r)),
            "second_pass": _dates(r, 3),
        }
    return out


@case("rrule.cache-second-iteration-matches-first", tags=("adversarial",))
def _(ctx):
    # The cache path holds a _thread.allocate_lock() from six.moves and a
    # generator it advances with advance_iterator. If a migration rewrites
    # either, the cached and uncached sequences must stay identical -- a cache
    # that silently truncates is the classic failure and shows up only on the
    # second pass.
    from dateutil.rrule import MONTHLY, rrule
    from dateutil.rrule import FR
    kw = dict(freq=MONTHLY, dtstart=START, count=14, byweekday=FR(+2))
    cached = rrule(cache=True, **kw)
    first = _dates(cached)
    second = _dates(cached)
    third = [d.isoformat() for d in cached]
    plain = _dates(rrule(cache=False, **kw))
    return {"first": first, "identical_second": first == second,
            "identical_third": first == third,
            "matches_uncached": first == plain,
            "partial_then_full": (_dates(rrule(cache=True, **kw), 3),
                                  len(list(rrule(cache=True, **kw)))),
            "count_then_iter": (rrule(cache=True, **kw).count(),
                                len(_dates(rrule(cache=True, **kw), 99)))}


@case("rrule.rruleset")
def _(ctx):
    from dateutil.rrule import DAILY, WEEKLY, rrule, rruleset
    rs = rruleset()
    rs.rrule(rrule(WEEKLY, dtstart=START, count=6))
    rs.rdate(datetime.datetime(2015, 6, 17, 9, 30, 0))
    rs.exrule(rrule(DAILY, dtstart=datetime.datetime(2015, 6, 29, 9, 30, 0),
                    count=1))
    rs.exdate(datetime.datetime(2015, 6, 22, 9, 30, 0))
    probe = datetime.datetime(2015, 6, 20)
    return {"dates": _dates(rs),
            "count": rs.count(),
            "before": rs.before(probe).isoformat(),
            "after": rs.after(probe).isoformat(),
            "between": _dates(rs.between(START, UNTIL)),
            "cached": _dates(rruleset(cache=True))}


@case("rrule.rrulestr")
def _(ctx):
    from dateutil.rrule import rrulestr
    out = {}
    specs = [
        "FREQ=DAILY;COUNT=5",
        "FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,FR;COUNT=6",
        "FREQ=MONTHLY;BYDAY=-1SU;COUNT=4",
        "FREQ=YEARLY;BYMONTH=6;BYMONTHDAY=15;COUNT=3",
        "FREQ=DAILY;UNTIL=20150701T093000Z",
        "RRULE:FREQ=DAILY;COUNT=3",
        "DTSTART:20150615T093000\nRRULE:FREQ=DAILY;COUNT=3",
        ("DTSTART:20150615T093000\nRRULE:FREQ=WEEKLY;COUNT=4\n"
         "EXDATE:20150622T093000"),
        ("DTSTART:20150615T093000\nRRULE:FREQ=WEEKLY;COUNT=4\n"
         "RDATE:20150616T093000"),
        "FREQ=DAILY;COUNT=3;WKST=SU",
        "FREQ=MINUTELY;INTERVAL=90;COUNT=4",
    ]
    for s in specs:
        out[s] = _try(lambda s=s: _dates(rrulestr(s, dtstart=START)))
    out["forceset"] = _try(lambda: _dates(
        rrulestr("FREQ=DAILY;COUNT=3", dtstart=START, forceset=True)))
    out["compatible"] = _try(lambda: _dates(
        rrulestr("FREQ=DAILY;COUNT=3", dtstart=START, compatible=True)))
    out["unfold"] = _try(lambda: _dates(rrulestr(
        "DTSTART:20150615T093000\nRRULE:FREQ=DAILY;\n COUNT=3",
        unfold=True)))
    out["cache"] = _try(lambda: _dates(
        rrulestr("FREQ=DAILY;COUNT=3", dtstart=START, cache=True)))
    return out


@case("rrule.rrulestr-rejects", tags=("adversarial",))
def _(ctx):
    from dateutil.rrule import rrulestr
    out = {}
    for s in ("", "FREQ=NOPE;COUNT=3", "FREQ=DAILY;COUNT=notanumber",
              "FREQ=DAILY;BYDAY=XX", "COUNT=3", "NOTAKEY:1",
              "FREQ=DAILY;UNTIL=nonsense", "FREQ=DAILY;BYSETPOS=0",
              "DTSTART:notadate\nRRULE:FREQ=DAILY;COUNT=2",
              "FREQ=DAILY;COUNT=3;UNTIL=20150701T093000Z"):
        out[repr(s)] = _try(lambda s=s: _dates(rrulestr(s, dtstart=START)))
    return out


@case("rrule.constructor-rejects", tags=("adversarial",))
def _(ctx):
    from dateutil.rrule import DAILY, MONTHLY, YEARLY, rrule
    out = {}
    out["bad_freq"] = _try(lambda: _dates(rrule(99, dtstart=START, count=2)))
    out["interval_zero"] = _try(lambda: _dates(
        rrule(DAILY, dtstart=START, interval=0, count=2)))
    out["bymonthday_zero"] = _try(lambda: _dates(
        rrule(MONTHLY, dtstart=START, bymonthday=0, count=2)))
    out["bysetpos_without_by"] = _try(lambda: _dates(
        rrule(DAILY, dtstart=START, bysetpos=1, count=2)))
    out["byweekday_str"] = _try(lambda: _dates(
        rrule(DAILY, dtstart=START, byweekday="MO", count=2)))
    out["byweekday_int"] = _try(lambda: _dates(
        rrule(DAILY, dtstart=START, byweekday=0, count=2)))
    out["nth_weekday_in_weekly"] = _try(lambda: _dates(
        rrule(YEARLY, dtstart=START, byweekday=(1, 2), count=3)))
    out["dtstart_is_date"] = _try(lambda: _dates(
        rrule(DAILY, dtstart=datetime.date(2015, 6, 15), count=2)))
    out["dtstart_none_is_now"] = _try(
        lambda: len(list(rrule(DAILY, count=2))) == 2)
    out["microsecond_dropped"] = _try(lambda: _dates(rrule(
        DAILY, dtstart=datetime.datetime(2015, 6, 15, 9, 30, 0, 123456),
        count=2)))
    return out


@case("rrule.tz-aware-dtstart")
def _(ctx):
    from dateutil.rrule import DAILY, rrule
    from dateutil.tz import tzoffset, tzutc
    aware = START.replace(tzinfo=tzutc())
    jst = START.replace(tzinfo=tzoffset("JST", 9 * 3600))
    return {"utc": _dates(rrule(DAILY, dtstart=aware, count=3)),
            "jst": _dates(rrule(DAILY, dtstart=jst, count=3)),
            "mixed_until": _try(lambda: _dates(rrule(
                DAILY, dtstart=aware, until=UNTIL)))}


@case("rrule.no-str-serialisation", tags=("adversarial",))
def _(ctx):
    # This vintage has no __str__ on rrule: 2.4.2 predates the RFC-string
    # serialiser, so str(r) is the default object repr and round-tripping a
    # rule through rrulestr() is impossible. Pinning the absence stops a
    # migration from pulling in a newer upstream file and silently changing
    # what every caller of str() on one of these objects gets.
    from dateutil.rrule import WEEKLY, rrule, rrulebase, rruleset, rrulestr
    from dateutil.rrule import MO, FR
    r = rrule(WEEKLY, dtstart=START, count=5, byweekday=(MO, FR), interval=2)
    return {"has_str": "__str__" in vars(rrule),
            "has_str_on_base": "__str__" in vars(rrulebase),
            "str_is_object_default": str(r) == object.__repr__(r),
            "set_has_str": "__str__" in vars(rruleset),
            "roundtrip": _try(lambda: _dates(rrulestr(str(r)))),
            "public_attrs": sorted(n for n in dir(r) if not n.startswith("_"))}


@case("rrule.iterinfo-rebuilds-per-year")
def _(ctx):
    # _iterinfo caches a year's worth of bitmasks and rebuilds them when the
    # rule crosses into the next year. Rules that span a leap boundary and
    # rules keyed on ISO week numbers are the two ways to force the rebuild,
    # and both are pure integer table arithmetic -- the code most exposed to a
    # careless int/float or range/xrange edit.
    from dateutil.rrule import MONTHLY, WEEKLY, YEARLY, rrule
    from dateutil.rrule import MO, SU, TH
    out = {}
    out["weekno-across-years"] = _dates(rrule(
        YEARLY, dtstart=datetime.datetime(2014, 12, 20, 9, 0),
        byweekno=(1, 52, 53), byweekday=MO, count=12))
    out["yearday-across-leap"] = _dates(rrule(
        YEARLY, dtstart=datetime.datetime(2015, 12, 20, 9, 0),
        byyearday=(-1, 1, 60, 366), count=12))
    out["monthday-neg-across-years"] = _dates(rrule(
        MONTHLY, dtstart=datetime.datetime(2015, 11, 1, 9, 0),
        bymonthday=(-1, -30), count=10))
    out["nth-weekday-across-years"] = _dates(rrule(
        MONTHLY, dtstart=datetime.datetime(2015, 11, 1, 9, 0),
        byweekday=(TH(+4), SU(-1)), count=10))
    out["weekly-across-years"] = _dates(rrule(
        WEEKLY, dtstart=datetime.datetime(2015, 12, 20, 9, 0),
        interval=3, count=10))
    out["feb29-only"] = _dates(rrule(
        YEARLY, dtstart=datetime.datetime(2015, 1, 1, 9, 0),
        bymonth=2, bymonthday=29, count=4))
    return out


@case("rrule.long-horizon-arithmetic")
def _(ctx):
    # The gcd-driven fast path in _iter only engages for intervals that do not
    # divide the period evenly; these four rules walk it, and a 90-minute rule
    # crossing a DST-free year is the cheapest way to exercise the integer
    # arithmetic at scale without a slow test.
    from dateutil.rrule import HOURLY, MINUTELY, SECONDLY, YEARLY, rrule
    out = {}
    out["minutely-7"] = _dates(rrule(MINUTELY, dtstart=START, interval=7,
                                     count=10))
    out["hourly-5"] = _dates(rrule(HOURLY, dtstart=START, interval=5,
                                   count=10))
    out["secondly-11"] = _dates(rrule(SECONDLY, dtstart=START, interval=11,
                                      count=8))
    out["minutely-90-byhour"] = _dates(rrule(MINUTELY, dtstart=START,
                                             interval=90, byhour=(9, 12),
                                             count=8))
    out["yearly-leap"] = _dates(rrule(YEARLY, dtstart=datetime.datetime(
        2012, 2, 29, 12, 0), count=4))
    out["yearly-366"] = _dates(rrule(YEARLY, dtstart=START, byyearday=366,
                                     count=3))
    return out


@case("rrule.generator-protocol", tags=("adversarial",))
def _(ctx):
    # rrulebase.__iter__ hands back either the cached list's iterator or the
    # live generator, and _iter_cached advances it with six's
    # advance_iterator. Two interleaved iterators over the same cached rule
    # must not steal each other's items -- the failure mode a hand-rolled
    # next() shim produces.
    from dateutil.rrule import DAILY, rrule
    r = rrule(DAILY, dtstart=START, count=6, cache=True)
    a = iter(r)
    b = iter(r)
    interleaved = [next(a).day, next(b).day, next(a).day, next(b).day]
    plain = rrule(DAILY, dtstart=START, count=6, cache=False)
    pa = iter(plain)
    pb = iter(plain)
    plain_interleaved = [next(pa).day, next(pb).day, next(pa).day,
                         next(pb).day]
    exhausted = iter(rrule(DAILY, dtstart=START, count=1))
    next(exhausted)
    return {"cached_interleaved": interleaved,
            "plain_interleaved": plain_interleaved,
            "stopiteration": _try(lambda: next(exhausted)),
            "iter_is_not_self": iter(r) is not r,
            "list_twice_equal": _dates(r) == _dates(r)}
