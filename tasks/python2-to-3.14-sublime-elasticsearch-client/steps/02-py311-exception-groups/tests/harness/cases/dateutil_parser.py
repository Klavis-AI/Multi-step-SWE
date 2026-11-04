# -*- coding: utf-8 -*-
"""``dateutil.parser``, ``dateutil.relativedelta`` and ``dateutil.easter``.

These three modules are pure computation: no socket, no clock, no filesystem.
That makes them the cheapest high-value coverage in the tree and also the most
exposed to a 2->3 migration, because they are where the vendored copy leans
hardest on ``six``:

* ``parser.py`` imports ``string_types``, ``text_type``, ``binary_type`` and
  ``integer_types`` from ``six``, and ``StringIO`` from ``six.moves``. Its
  ``_timelex`` class defines ``__next__`` and then aliases ``next = __next__``,
  the canonical Python 2 iterator shim.
* ``relativedelta.py`` imports ``integer_types`` and calls ``__div__`` /
  ``__rdiv__`` aliases that mean nothing on 3.
* ``parser.py`` is stored as Latin-1 and its ``_timelex.wordchars`` string
  contains literal accented bytes, so how the file is decoded is observable
  through which strings tokenise.

The wall clock is not frozen (harness/env.py records why), so every case either
supplies an explicit ``default=`` or observes only the fields the input string
actually pins. ``parse("11:30")`` without a default would inherit today's date
and expire; it is never done here.
"""
import datetime

from ..registry import case

# Every case that lets parse() fill in missing fields pins them from here.
DEFAULT = datetime.datetime(2015, 6, 15, 9, 30, 45)


def _fmt(dt):
    """A datetime rendered so that tzinfo identity, not just offset, shows."""
    if dt is None:
        return None
    if isinstance(dt, datetime.datetime):
        return {"iso": dt.isoformat(),
                "tzname": dt.tzname(),
                "utcoffset": (dt.utcoffset().total_seconds()
                              if dt.utcoffset() is not None else None),
                "tzinfo_type": type(dt.tzinfo).__name__ if dt.tzinfo else None}
    return repr(dt)


def _try(fn):
    """Run one input, recording either its value or the exception it raised.

    Per input rather than per case: one bad string must not delete the
    evidence for the fifty good ones next to it.
    """
    try:
        return {"ok": fn()}
    except Exception as exc:
        return {"raised": type(exc).__name__, "str": str(exc)}


# --- parse ---------------------------------------------------------------

GOOD = [
    "2015-06-15",
    "2015-06-15T12:34:56",
    "2015-06-15T12:34:56Z",
    "2015-06-15T12:34:56.123456",
    "2015-06-15T12:34:56+09:00",
    "2015-06-15T12:34:56-0500",
    "20150615",
    "20150615T123456",
    "2015-W25-1",
    "15/06/2015",
    "06/15/2015",
    "6/15/15",
    "Jun 15 2015",
    "15 Jun 2015",
    "June 15, 2015",
    "15-Jun-2015",
    "Mon, 15 Jun 2015 12:34:56 GMT",
    "Mon Jun 15 12:34:56 2015",
    "Mon Jun 15 12:34:56 UTC 2015",
    "2015-06-15 12:34:56",
    "2015/06/15 12:34",
    "12:34",
    "12:34:56",
    "12:34:56.789",
    "12:34 PM",
    "12:34 am",
    "1:2:3",
    "Thu Sep 25 10:36:28 2003",
    "Thu Sep 25 2003",
    "10:36:28",
    "2003-09-25T10:49:41.5-03:00",
    "19970902T090000",
    "199709020900",
    "0099-01-01",
    "0099-01-01T00:00:00",
    "Sep 03",
    "Sep of 03",
    "Wed",
    "Wednesday",
    "October",
    "31-Dec-00",
    "2015-06-15 12:34:56 EST",
    "2015-06-15 12:34:56 +0900",
    "2015-06-15T12:34:56.000000Z",
]


@case("dateutil.parse-good")
def _(ctx):
    from dateutil.parser import parse
    out = {}
    for s in GOOD:
        out[s] = _try(lambda s=s: _fmt(parse(s, default=DEFAULT)))
    return out


BAD = [
    "",
    "   ",
    "not a date at all",
    "2015-13-01",
    "2015-02-30",
    "25:00",
    "12:61",
    "0-0-0",
    "1997-02-30T00:00:00",
    "Tue Feb 30 2015",
    "6/31/2015",
    u"2015-06-15T12:34:56 Z",
    "++2015",
    "2015-06-15T",
    ":::",
    "5",
    "999999999999999999999",
    "Jan Feb Mar",
]


@case("dateutil.parse-rejects", tags=("adversarial",))
def _(ctx):
    # The reject set is scored as tightly as the accept set: a migration that
    # turns a ValueError into a TypeError, or that starts accepting a string
    # this version refuses, has changed the parser's contract.
    from dateutil.parser import parse
    out = {}
    for s in BAD:
        out[repr(s)] = _try(lambda s=s: _fmt(parse(s, default=DEFAULT)))
    return out


@case("dateutil.parse-dayfirst-yearfirst")
def _(ctx):
    from dateutil.parser import parse
    out = {}
    for s in ("01/02/03", "10/11/12", "2015-06-15", "13/01/02", "01/13/02"):
        for df in (False, True):
            for yf in (False, True):
                key = "%s df=%s yf=%s" % (s, df, yf)
                out[key] = _try(lambda s=s, df=df, yf=yf: _fmt(
                    parse(s, default=DEFAULT, dayfirst=df, yearfirst=yf)))
    return out


@case("dateutil.parse-fuzzy")
def _(ctx):
    from dateutil.parser import parse
    strings = [
        "Today is 25 of September of 2003, exactly at 10:49:41 with timezone -03:00.",
        "I have a meeting on March 1, 1974",
        "On June 8th, 2020, I am going to be the first man on Mars",
        "Meet me at the AM/PM on Sunset at 3:00 AM on December 3rd, 2003",
    ]
    out = {}
    for s in strings:
        out[s] = _try(lambda s=s: _fmt(parse(s, default=DEFAULT, fuzzy=True)))
        dt, tokens = parse(s, default=DEFAULT, fuzzy_with_tokens=True)
        out[s + " |tokens"] = {"dt": _fmt(dt), "tokens": list(tokens)}
    return out


@case("dateutil.parse-ignoretz-and-tzinfos")
def _(ctx):
    from dateutil.parser import parse
    from dateutil.tz import tzoffset, tzutc
    s = "2015-06-15 12:34:56 BRST"
    out = {"ignoretz": _try(lambda: _fmt(parse(s, default=DEFAULT,
                                               ignoretz=True)))}
    out["tzinfos_dict"] = _try(lambda: _fmt(parse(
        s, default=DEFAULT, tzinfos={"BRST": -10800})))
    out["tzinfos_tzobj"] = _try(lambda: _fmt(parse(
        s, default=DEFAULT, tzinfos={"BRST": tzoffset("BRST", -10800)})))
    out["tzinfos_str"] = _try(lambda: _fmt(parse(
        s, default=DEFAULT, tzinfos={"BRST": "BRST+3BRDT,M10.3.0,M2.3.0"})))
    out["tzinfos_callable"] = _try(lambda: _fmt(parse(
        s, default=DEFAULT, tzinfos=lambda name, off: tzutc())))
    out["tzinfos_bad_type"] = _try(lambda: _fmt(parse(
        s, default=DEFAULT, tzinfos={"BRST": 3.5})))
    out["utc_named"] = _try(lambda: _fmt(parse("2015-06-15 12:34:56 UTC",
                                               default=DEFAULT)))
    return out


@case("dateutil.parse-default-fills-only-missing")
def _(ctx):
    # The default's role is subtle and easy to break: it supplies the fields
    # the string did not mention, field by field, not as a whole.
    from dateutil.parser import parse
    out = {}
    for s in ("2015", "June", "12:34", "15", "June 2014", "1 Jan"):
        out[s] = _try(lambda s=s: _fmt(parse(s, default=DEFAULT)))
    return out


@case("dateutil.parse-bytes-and-unicode", tags=("adversarial",))
def _(ctx):
    # parser.py's _parse() tests ``isinstance(timestr, binary_type)`` and
    # decodes with the *ASCII* codec on Python 3 only. Retiring six means
    # rewriting that branch; what it accepts and what it raises is pinned.
    from dateutil.parser import parse
    out = {}
    for value in (b"2015-06-15", u"2015-06-15", bytearray(b"2015-06-15"),
                  b"2015-06-15T12:34:56+09:00", u"2015–06–15",
                  b"\xff\xfe2015"):
        out[repr(value)] = _try(lambda v=value: _fmt(parse(v,
                                                           default=DEFAULT)))
    out["non_string"] = _try(lambda: _fmt(parse(20150615, default=DEFAULT)))
    out["none"] = _try(lambda: _fmt(parse(None, default=DEFAULT)))
    return out


@case("dateutil.parse-latin1-wordchars", tags=("adversarial",))
def _(ctx):
    # _timelex.wordchars is a literal containing the Latin-1 accented range,
    # and parser.py is stored as Latin-1 with a coding cookie. If the file is
    # ever re-encoded or the literal rewritten, these tokenisations move.
    from dateutil.parser import _timelex
    out = {}
    for s in (u"März 2015", u"15 Jänner 2015", u"café 2015",
              u"ß 2015", "hello_world 12"):
        out[s] = _try(lambda s=s: list(_timelex.split(s)))
    return out


@case("dateutil.timelex-split")
def _(ctx):
    from dateutil.parser import _timelex
    out = {}
    for s in ("2015-06-15T12:34:56.789+09:00",
              "Mon, 15 Jun 2015 12:34:56 GMT",
              "12:34:56 a.m.",
              "1.5.2015",
              "3.14",
              "..",
              "a1b2c3",
              ""):
        out[s] = _try(lambda s=s: list(_timelex.split(s)))
    return out


@case("dateutil.timelex-iteration-protocol", tags=("adversarial",))
def _(ctx):
    # _timelex is an iterator written for both Pythons: it defines __next__ and
    # then a ``def next(self): return self.__next__()`` wrapper marked
    # "Python 2.x support". The iteration *protocol* is what must survive --
    # __iter__ returns self, __next__ yields tokens, get_token returns None at
    # EOF rather than raising, and exhaustion raises a bare StopIteration.
    #
    # What must NOT be pinned is whether the ``next`` wrapper is still there.
    # It is exactly the Python 2 scaffolding item 1 requires gone, and item 3
    # says removing or renaming such a method is fine. Recording
    # ``hasattr(_timelex, "next")`` or ``_timelex.next is _timelex.__next__``
    # would admit only one answer -- leave the wrapper exactly as it is -- and
    # fail both deleting it and rebinding it to __next__.
    # gates.CASE_BANNED_PROBES makes that shape a sweep failure.
    from dateutil.parser import _timelex
    lex = _timelex("2015-06-15")
    first = lex.get_token()
    rest = []
    while True:
        tok = lex.get_token()
        if tok is None:
            break
        rest.append(tok)
    fresh = _timelex("1 2")
    return {"first": first, "rest": rest,
            "iter_is_self": iter(fresh) is fresh,
            "via_iter": list(_timelex("1 2")),
            "stopiteration": _try(lambda: [next(fresh), next(fresh),
                                           next(fresh), next(fresh)])}


@case("dateutil.parserinfo-defaults")
def _(ctx):
    from dateutil.parser import parserinfo
    info = parserinfo()
    return {"jump": sorted(info.JUMP),
            "weekdays": [list(x) for x in info.WEEKDAYS],
            "months": [list(x) for x in info.MONTHS],
            "hms": [list(x) for x in info.HMS],
            "ampm": [list(x) for x in info.AMPM],
            "utczone": sorted(info.UTCZONE),
            "pertain": sorted(info.PERTAIN),
            "tzoffset": dict(info.TZOFFSET),
            "lookups": {name: [info.jump(name), info.weekday(name),
                               info.month(name), info.hms(name),
                               info.ampm(name), info.utczone(name),
                               info.pertain(name), info.tzoffset(name)]
                        for name in ("and", "Mon", "monday", "sep", "h",
                                     "pm", "UTC", "of", "UT", "nonsense")},
            "convertyear": [info.convertyear(y)
                            for y in (0, 45, 50, 69, 70, 99, 100, 1999, 2015)],
            "validate": _validate_samples()}


def _validate_samples():
    """parserinfo.validate() mutates and approves a parse result in place; the
    only way to obtain one of those is through parser._parse()."""
    from dateutil.parser import parser, parserinfo
    out = {}
    for s in ("2015-06-15 12:34", "2015-06-15 12:34 UTC", "31-Dec-49",
              "31-Dec-50", "12:34"):
        res = parser()._parse(s)
        if isinstance(res, tuple):
            res = res[0]
        out[s] = {"repr_before": repr(res),
                  "validated": parserinfo().validate(res),
                  "repr_after": repr(res)}
    return out


@case("dateutil.parserinfo-subclass")
def _(ctx):
    # Subclassing parserinfo is the documented extension point; it is also the
    # only way to reach convertyear/validate through the public parse() call.
    from dateutil.parser import parse, parserinfo

    class Spanish(parserinfo):
        WEEKDAYS = [("Lun", "Lunes"), ("Mar", "Martes"), (u"Mié", u"Miércoles"),
                    ("Jue", "Jueves"), ("Vie", "Viernes"), (u"Sáb", u"Sábado"),
                    ("Dom", "Domingo")]
        MONTHS = [("Ene", "Enero"), ("Feb", "Febrero"), ("Mar", "Marzo"),
                  ("Abr", "Abril"), ("May", "Mayo"), ("Jun", "Junio"),
                  ("Jul", "Julio"), ("Ago", "Agosto"),
                  ("Sep", "Septiembre"), ("Oct", "Octubre"),
                  ("Nov", "Noviembre"), ("Dic", "Diciembre")]

    out = {}
    for s in (u"15 Ene 2015", u"Mié 3 Jun 2015", u"15 Dic 2015"):
        out[s] = _try(lambda s=s: _fmt(parse(s, parserinfo=Spanish(),
                                             default=DEFAULT)))

    class CenturyAt2050(parserinfo):
        def convertyear(self, year, century_specified=False):
            return parserinfo.convertyear(self, year, century_specified)

    out["dayfirst_info"] = _try(
        lambda: _fmt(parse("01/02/03", parserinfo=parserinfo(dayfirst=True),
                           default=DEFAULT)))
    out["yearfirst_info"] = _try(
        lambda: _fmt(parse("01/02/03", parserinfo=parserinfo(yearfirst=True),
                           default=DEFAULT)))
    out["subclass_century"] = _try(
        lambda: _fmt(parse("31-Dec-49", parserinfo=CenturyAt2050(),
                           default=DEFAULT)))
    return out


@case("dateutil.tzparser")
def _(ctx):
    # _tzparser turns a POSIX TZ rule into a struct of fields; tz.tzstr is its
    # only caller, so this is the one direct view of it.
    from dateutil.parser import _tzparser
    out = {}
    for rule in ("EST5EDT", "EST5EDT,M3.2.0/2,M11.1.0/2",
                 "BRST+3BRDT,M10.3.0,M2.3.0", "UTC0", "GMT+0", "nonsense",
                 "<UTC+09>-9"):
        res = _tzparser().parse(rule)
        if res is None:
            out[rule] = None
            continue
        out[rule] = {k: getattr(res, k)
                     for k in sorted(dir(res))
                     if not k.startswith("_") and
                     not callable(getattr(res, k))}
    return out


@case("dateutil.parse-module-level-helpers")
def _(ctx):
    from dateutil import parser
    return {"parsems": [parser._parsems(v)
                        for v in ("0", "5", "500", "123456", "1234567")],
            "parsetz": [repr(parser._parsetz(v))
                        for v in ("+0900", "-0500", "Z", "+09", "bogus")],
            "default_parser_type": type(parser.DEFAULTPARSER).__name__,
            "default_tzparser_type": type(parser.DEFAULTTZPARSER).__name__,
            # _resultbase.__repr__ reads self.__slots__, which only its
            # subclasses define -- instantiating the base class directly and
            # printing it raises. That is a latent defect in the vendored
            # copy, not something to fix here: pinning it stops a migration
            # from "tidying" the class hierarchy and changing what the two
            # subclasses print.
            "resultbase_repr": _try(lambda: repr(parser._resultbase())),
            "result_repr": repr(parser.parser._result()),
            "tzresult_repr": repr(parser._tzparser._result())}


# --- relativedelta -------------------------------------------------------

@case("dateutil.relativedelta-arithmetic")
def _(ctx):
    from dateutil.relativedelta import relativedelta
    base = datetime.datetime(2015, 1, 31, 12, 0, 0)
    out = {}
    for name, rd in [
            ("months+1", relativedelta(months=+1)),
            ("months-1", relativedelta(months=-1)),
            ("years+1", relativedelta(years=+1)),
            ("day=31", relativedelta(day=31)),
            ("month=2 day=31", relativedelta(month=2, day=31)),
            ("weeks+2", relativedelta(weeks=+2)),
            ("hours+36", relativedelta(hours=+36)),
            ("microseconds+1", relativedelta(microseconds=+1)),
            ("leapdays", relativedelta(years=+1, leapdays=+1)),
            ("yearday=100", relativedelta(yearday=100)),
            ("nlyearday=100", relativedelta(nlyearday=100)),
    ]:
        out[name] = {"repr": repr(rd), "applied": (base + rd).isoformat(),
                     "rapplied": (rd + base).isoformat(),
                     "subtracted": (base - rd).isoformat(),
                     "bool": bool(rd),
                     "fields": [rd.years, rd.months, rd.days, rd.hours,
                                rd.minutes, rd.seconds, rd.microseconds,
                                rd.leapdays, rd.year, rd.month, rd.day,
                                rd.hour, rd.minute, rd.second,
                                rd.microsecond]}
    return out


@case("dateutil.relativedelta-weekday")
def _(ctx):
    from dateutil.relativedelta import FR, MO, SU, relativedelta, weekdays
    base = datetime.datetime(2015, 6, 15, 12, 0, 0)  # a Monday
    out = {"weekdays": [repr(w) for w in weekdays]}
    for name, rd in [
            ("next MO", relativedelta(weekday=MO)),
            ("next FR", relativedelta(weekday=FR)),
            ("last FR", relativedelta(weekday=FR(-1))),
            ("2nd FR", relativedelta(weekday=FR(+2))),
            ("last SU of month", relativedelta(day=31, weekday=SU(-1))),
            ("month+1 SU(-1)", relativedelta(months=+1, day=31,
                                             weekday=SU(-1))),
    ]:
        out[name] = {"repr": repr(rd), "applied": (base + rd).isoformat()}
    # weekday defines __eq__ without __hash__: on Python 2 that left the
    # instances hashable, on 3 it makes them unhashable. The tree already runs
    # on 3, so the TypeError below is the current behaviour and must survive
    # any rewrite of the class.
    out["weekday_eq"] = [MO == MO, MO == MO(+1), MO(+1) == MO(+1),
                         MO == FR, repr(MO(+1)), repr(FR(-1))]
    out["weekday_hash"] = _try(lambda: hash(MO))
    return out


@case("dateutil.relativedelta-combination")
def _(ctx):
    from dateutil.relativedelta import relativedelta
    a = relativedelta(years=+1, months=+2, days=+3, hours=+4)
    b = relativedelta(months=+10, days=+30, minutes=+90)
    # Every operator is probed through _try: this vintage of relativedelta
    # implements a Python 2 operator set (__div__ with no __truediv__, no
    # __abs__, no timedelta interop), so several of these are TypeErrors --
    # and which ones raise is exactly what a migration must not change.
    return {"a": repr(a), "b": repr(b),
            "a+b": repr(a + b), "a-b": repr(a - b), "-a": repr(-a),
            "a*2": _try(lambda: repr(a * 2)),
            "a*0.5": _try(lambda: repr(a * 0.5)),
            "a/2": _try(lambda: repr(a / 2)),
            "a//2": _try(lambda: repr(a // 2)),
            "a==a": a == relativedelta(years=+1, months=+2, days=+3,
                                       hours=+4),
            "a!=b": a != b,
            "hash_eq": _try(lambda: hash(a) == hash(a)),
            "plus_timedelta": _try(lambda: repr(a + datetime.timedelta(
                days=1))),
            "abs": _try(lambda: repr(abs(-a))),
            "weeks_property": _try(lambda: [a.weeks, b.weeks]),
            "has_div_alias": [hasattr(relativedelta, "__div__"),
                              hasattr(relativedelta, "__truediv__"),
                              hasattr(relativedelta, "__rdiv__"),
                              hasattr(relativedelta, "__nonzero__"),
                              hasattr(relativedelta, "__bool__")],
            "set_weeks": repr(relativedelta(days=+17)),
            }


@case("dateutil.relativedelta-integer-carry", tags=("adversarial",))
def _(ctx):
    """``_fix()``'s five carry branches.

    ``_fix`` runs from ``__init__``, so every one of these is reached by
    ordinary construction. Each branch is guarded -- ``abs(self.microseconds) >
    999999``, ``abs(self.seconds) > 59``, ``> 59``, ``> 23``, ``abs(months) >
    11`` -- and inside each, a sign is taken with ``x//abs(x)`` and a
    ``divmod`` carries the overflow up. Five ``//`` on five lines that look
    alike, in a module whose Python 2 division aliases a migration has to
    rewrite anyway.

    Turning one of them into ``/`` does not raise: ``divmod`` propagates the
    float, the field lands as ``microseconds=500001.0``, and the object still
    reprs *identically* -- the repr formats with ``%+d`` -- still applies to a
    datetime, and still compares equal to its integer twin. It shows up only
    in the recorded type. Hence the explicit type row; canon tags int and
    float differently, so an int recorded and a float replayed is a mismatch.

    No other case reaches these lines: every other dateutil case stays inside
    the guards -- none builds a relativedelta with more than 999999
    microseconds. Over-range integers are what reaches these lines.
    Fractional inputs do not; ``relativedelta(days=1.5)`` stores 1.5 days and
    leaves microseconds at 0.

    There is no ``normalized()`` here: dateutil 2.4.2 has no such method, and
    calling it would abort the body with an ``AttributeError`` before any of
    the rows below were recorded. ``driver audit`` reports that shape --
    ``builtins.AttributeError`` is not in ``DELIBERATE_RAISES``.
    """
    from dateutil.relativedelta import relativedelta

    def observe(rd):
        return {
            "repr": repr(rd),
            "fields": [rd.years, rd.months, rd.days, rd.hours, rd.minutes,
                       rd.seconds, rd.microseconds],
            "types": [type(v).__name__ for v in
                      (rd.years, rd.months, rd.days, rd.hours, rd.minutes,
                       rd.seconds, rd.microseconds)],
            "applied": (DEFAULT + rd).isoformat(),
        }

    out = {}
    # One row per guard, each side of zero, and each just over the boundary as
    # well as far past it.
    for name, rd in [
            ("us just over", relativedelta(microseconds=1000000)),
            ("us over", relativedelta(microseconds=2500001)),
            ("us negative", relativedelta(microseconds=-2500001)),
            ("us at guard", relativedelta(microseconds=999999)),
            ("seconds over", relativedelta(seconds=3725)),
            ("seconds negative", relativedelta(seconds=-3725)),
            ("seconds at guard", relativedelta(seconds=59)),
            ("minutes over", relativedelta(minutes=1501)),
            ("minutes negative", relativedelta(minutes=-1501)),
            ("hours over", relativedelta(hours=100)),
            ("hours negative", relativedelta(hours=-100)),
            ("months over", relativedelta(months=37)),
            ("months negative", relativedelta(months=-37)),
            ("months at guard", relativedelta(months=11)),
            ("every branch at once",
             relativedelta(months=37, hours=100, minutes=1501, seconds=3725,
                           microseconds=2500001)),
            ("every branch negative",
             relativedelta(months=-37, hours=-100, minutes=-1501,
                           seconds=-3725, microseconds=-2500001)),
    ]:
        out[name] = observe(rd)

    # The method does not exist in this vintage. Recording *that* is worth a
    # row: a migration that "helpfully" adds it has changed the public surface.
    out["has_normalized"] = hasattr(relativedelta, "normalized")
    return out


@case("dateutil.relativedelta-from-two-dates")
def _(ctx):
    from dateutil.relativedelta import relativedelta
    pairs = [
        (datetime.datetime(2015, 6, 15), datetime.datetime(2014, 1, 1)),
        (datetime.datetime(2014, 1, 1), datetime.datetime(2015, 6, 15)),
        (datetime.date(2016, 3, 1), datetime.date(2016, 2, 29)),
        (datetime.datetime(2015, 6, 15, 12, 34, 56, 789),
         datetime.datetime(2015, 6, 15, 0, 0, 0, 0)),
    ]
    out = {}
    for hi, lo in pairs:
        rd = relativedelta(hi, lo)
        out["%s - %s" % (hi, lo)] = {"repr": repr(rd),
                                     "roundtrip": str(lo + rd)}
    return out


@case("dateutil.relativedelta-rejects", tags=("adversarial",))
def _(ctx):
    from dateutil.relativedelta import relativedelta
    out = {}
    out["month=13"] = _try(lambda: repr(relativedelta(month=13)))
    out["day=32"] = _try(lambda: repr(relativedelta(day=32)))
    out["hour=25"] = _try(lambda: repr(relativedelta(hour=25)))
    out["weekday=9"] = _try(lambda: repr(relativedelta(weekday=9)))
    out["float_months"] = _try(lambda: repr(relativedelta(months=1.5)))
    out["float_years"] = _try(lambda: repr(relativedelta(years=1.5)))
    # integer_types comes from six; a bool is an int, and that has always been
    # accepted here. Pinning it stops a "tidy up the isinstance" edit from
    # changing it.
    out["bool_months"] = _try(lambda: repr(relativedelta(months=True)))
    out["add_str"] = _try(lambda: repr(relativedelta(days=1) + "x"))
    out["div_zero"] = _try(lambda: repr(relativedelta(days=1) / 0))
    out["radd_date"] = _try(lambda: str(datetime.date(2015, 6, 15) +
                                        relativedelta(months=+1)))
    out["rsub_date"] = _try(lambda: str(datetime.date(2015, 6, 15) -
                                        relativedelta(months=+1)))
    return out


# --- easter ---------------------------------------------------------------

@case("dateutil.easter")
def _(ctx):
    from dateutil.easter import EASTER_JULIAN, EASTER_ORTHODOX, EASTER_WESTERN
    from dateutil.easter import easter
    years = [1900, 1961, 1975, 1999, 2000, 2015, 2016, 2038, 2099]
    out = {}
    for method in (EASTER_JULIAN, EASTER_ORTHODOX, EASTER_WESTERN):
        out[str(method)] = [str(easter(y, method)) for y in years]
    out["bad_method"] = _try(lambda: str(easter(2015, 9)))
    return out
