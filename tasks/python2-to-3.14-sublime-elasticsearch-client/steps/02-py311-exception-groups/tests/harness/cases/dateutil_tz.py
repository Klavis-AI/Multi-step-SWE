# -*- coding: utf-8 -*-
"""``dateutil.tz`` beyond the four names the tree imports.

``dateutil_api.py`` drives what ``analytics`` reaches -- ``tzutc`` and
``tzlocal`` -- plus the neighbours that share their decorator. This module
drives the rest of the file, which is the largest single module in the
dependency set (591 statements) and the one with the most 2/3 seams per line:

* ``tzical`` parses RFC 5545 VTIMEZONE text. It branches on
  ``isinstance(fileobj, string_types)`` (six), opens the path in **text** mode,
  unfolds continuation lines by hand, and -- the part that matters for the
  ladder -- imports ``dateutil.rrule`` lazily at first construction. ``rrule``
  does ``from fractions import gcd``, removed at Python 3.9, so **every**
  ``tzical`` below is unreachable from the first rung onwards until the
  migration fixes ``rrule``. That makes this class a separation signal as much
  as a coverage target: it is a second, independent path to the same defect,
  reached without importing ``rrule`` by name.
* ``tzfile`` parses binary TZif with ``struct.unpack`` and integer slicing, and
  pickles through ``__reduce__``.
* ``_ttinfo`` uses ``__slots__`` with hand-written ``__getstate__`` /
  ``__setstate__``, which is the shape most likely to be "modernised" into a
  dataclass and change its pickle.

No case reads the current time: every observation is taken at an explicit
datetime, and the only external input is the read-only system zone database
(guarded, so a host without it records that fact instead of failing).
"""
import datetime
import io
import os

from ..registry import case

SUMMER = datetime.datetime(2015, 7, 1, 12, 0, 0)
WINTER = datetime.datetime(2015, 1, 1, 12, 0, 0)

#: 2015 US eastern transitions, in *local standard* terms: spring forward
#: 2015-03-08 02:00, fall back 2015-11-01 02:00.
AROUND_SPRING = [datetime.datetime(2015, 3, 8, 1, 59, 59),
                 datetime.datetime(2015, 3, 8, 2, 30, 0),   # does not exist
                 datetime.datetime(2015, 3, 8, 3, 0, 1)]
AROUND_FALL = [datetime.datetime(2015, 11, 1, 0, 59, 59),
               datetime.datetime(2015, 11, 1, 1, 30, 0),    # ambiguous
               datetime.datetime(2015, 11, 1, 2, 0, 1)]

ICS = u"""\
BEGIN:VCALENDAR
PRODID:-//harness//test//EN
VERSION:2.0
BEGIN:VTIMEZONE
TZID:Fixture/Eastern
TZURL:http://example.com/tz
LAST-MODIFIED:20050809T050000Z
BEGIN:DAYLIGHT
TZOFFSETFROM:-0500
TZOFFSETTO:-0400
TZNAME:EDT
DTSTART:19700308T020000
RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=2SU
END:DAYLIGHT
BEGIN:STANDARD
TZOFFSETFROM:-0400
TZOFFSETTO:-0500
TZNAME:EST
DTSTART:19701101T020000
RRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=1SU
END:STANDARD
END:VTIMEZONE
BEGIN:VTIMEZONE
TZID:Fixture/Fixed
BEGIN:STANDARD
TZOFFSETFROM:+0900
TZOFFSETTO:+0900
TZNAME:JST
DTSTART:19700101T000000
END:STANDARD
END:VTIMEZONE
END:VCALENDAR
"""

#: One VTIMEZONE, so ``get()`` with no argument resolves.
ICS_ONE = u"""\
BEGIN:VTIMEZONE
TZID:Fixture/Only
BEGIN:STANDARD
TZOFFSETFROM:+0000
TZOFFSETTO:+0130
TZNAME:ODD
DTSTART:19700101T000000
END:STANDARD
END:VTIMEZONE
"""

#: A folded property line: RFC 5545 continuations start with a space and the
#: parser splices them by hand.
ICS_FOLDED = u"""\
BEGIN:VTIMEZONE
TZID:Fixture/Fol
 ded
BEGIN:STANDARD
TZOFFSETFROM:+0000
TZOFFSETTO:+0000
TZNAME:UTC
DTSTART:19700101T000000
END:STANDARD
END:VTIMEZONE
"""


def _try(fn):
    try:
        return {"ok": fn()}
    except Exception as exc:
        return {"raised": type(exc).__name__, "str": str(exc)}


def _zone(name):
    return os.path.join("/usr/share/zoneinfo", name)


def _describe(z, moments):
    return [[m.isoformat(),
             _try(lambda m=m: str(z.utcoffset(m))),
             _try(lambda m=m: str(z.dst(m))),
             _try(lambda m=m: z.tzname(m))] for m in moments]


# ------------------------------------------------------------------ tzical --

@case("dateutil.tzical-parse")
def _(ctx):
    from dateutil.tz import tzical
    c = tzical(io.StringIO(ICS))
    out = {"keys": sorted(c.keys()), "repr": repr(c),
           "get_unknown": c.get("Nope") is None,
           "get_ambiguous": _try(lambda: c.get())}
    east = c.get("Fixture/Eastern")
    fixed = c.get("Fixture/Fixed")
    out["eastern"] = {"repr": repr(east),
                      "winter": _describe(east, [WINTER]),
                      "summer": _describe(east, [SUMMER]),
                      "spring": _describe(east, AROUND_SPRING),
                      "fall": _describe(east, AROUND_FALL),
                      "applied": SUMMER.replace(tzinfo=east).isoformat()}
    out["fixed"] = {"repr": repr(fixed), "winter": _describe(fixed, [WINTER]),
                    "applied": WINTER.replace(tzinfo=fixed).isoformat()}
    return out


@case("dateutil.tzical-single-and-folded")
def _(ctx):
    from dateutil.tz import tzical
    one = tzical(io.StringIO(ICS_ONE))
    fold = tzical(io.StringIO(ICS_FOLDED))
    return {"one_keys": one.keys(),
            "one_get_default": repr(one.get()),
            "one_offset": str(one.get().utcoffset(SUMMER)),
            "one_name": one.get().tzname(SUMMER),
            # The folded TZID must come back spliced, without the leading
            # space of the continuation line.
            "folded_keys": fold.keys(),
            "folded_offset": str(fold.get().utcoffset(SUMMER))}


@case("dateutil.tzical-from-path")
def _(ctx):
    # The string branch: tzical opens the path itself, in text mode, and keeps
    # the path as its repr. Written as UTF-8 with CRLF the way RFC 5545 says.
    import tempfile
    from dateutil.tz import tzical
    fd, path = tempfile.mkstemp(suffix=".ics")
    with os.fdopen(fd, "wb") as fh:
        fh.write(ICS_ONE.replace(u"\n", u"\r\n").encode("utf-8"))
    try:
        c = tzical(path)
        named = tzical(io.open(path, "r", encoding="utf-8"))
        return {"keys": c.keys(),
                "offset": str(c.get().utcoffset(SUMMER)),
                # repr embeds the path; normalize.py collapses /tmp/<random>.
                "repr": repr(c),
                "named_fileobj_repr": repr(named),
                "named_keys": named.keys()}
    finally:
        os.unlink(path)


@case("dateutil.tzical-rejects", tags=("adversarial",))
def _(ctx):
    # Every ValueError branch in _parse_rfc and _parse_offset, by the input
    # that reaches it. A migration that reorders the parser will move one of
    # these messages onto a different input.
    from dateutil.tz import tzical

    def parse(s):
        return tzical(io.StringIO(s)).keys()

    bad = {
        "empty": u"",
        "blank-lines-only": u"\n\n\n",
        "no-vtimezone": u"BEGIN:VCALENDAR\nEND:VCALENDAR\n",
        "unknown-component": (u"BEGIN:VTIMEZONE\nTZID:X\n"
                              u"BEGIN:WEIRD\nEND:WEIRD\nEND:VTIMEZONE\n"),
        "no-tzid": (u"BEGIN:VTIMEZONE\nBEGIN:STANDARD\nTZOFFSETFROM:+0000\n"
                    u"TZOFFSETTO:+0000\nDTSTART:19700101T000000\n"
                    u"END:STANDARD\nEND:VTIMEZONE\n"),
        "no-components": u"BEGIN:VTIMEZONE\nTZID:X\nEND:VTIMEZONE\n",
        "component-not-closed": (u"BEGIN:VTIMEZONE\nTZID:X\nBEGIN:STANDARD\n"
                                 u"END:VTIMEZONE\n"),
        "no-dtstart": (u"BEGIN:VTIMEZONE\nTZID:X\nBEGIN:STANDARD\n"
                       u"TZOFFSETFROM:+0000\nTZOFFSETTO:+0000\n"
                       u"END:STANDARD\nEND:VTIMEZONE\n"),
        "no-tzoffsetfrom": (u"BEGIN:VTIMEZONE\nTZID:X\nBEGIN:STANDARD\n"
                            u"DTSTART:19700101T000000\nTZOFFSETTO:+0000\n"
                            u"END:STANDARD\nEND:VTIMEZONE\n"),
        "no-tzoffsetto": (u"BEGIN:VTIMEZONE\nTZID:X\nBEGIN:STANDARD\n"
                          u"DTSTART:19700101T000000\nTZOFFSETFROM:+0000\n"
                          u"END:STANDARD\nEND:VTIMEZONE\n"),
        "bad-component-end": (u"BEGIN:VTIMEZONE\nTZID:X\nBEGIN:STANDARD\n"
                              u"DTSTART:19700101T000000\nTZOFFSETFROM:+0000\n"
                              u"TZOFFSETTO:+0000\nEND:DAYLIGHT\n"
                              u"END:VTIMEZONE\n"),
        "unsupported-outer-property": (u"BEGIN:VTIMEZONE\nTZID:X\n"
                                       u"NOSUCHTHING:1\nEND:VTIMEZONE\n"),
        "unsupported-inner-property": (u"BEGIN:VTIMEZONE\nTZID:X\n"
                                       u"BEGIN:STANDARD\nNOSUCH:1\n"
                                       u"END:STANDARD\nEND:VTIMEZONE\n"),
        "tzid-with-parm": (u"BEGIN:VTIMEZONE\nTZID;X=1:Y\nEND:VTIMEZONE\n"),
        "tzname-with-parm": (u"BEGIN:VTIMEZONE\nTZID:X\nBEGIN:STANDARD\n"
                             u"DTSTART:19700101T000000\nTZOFFSETFROM:+0000\n"
                             u"TZOFFSETTO:+0000\nTZNAME;L=en:UTC\n"
                             u"END:STANDARD\nEND:VTIMEZONE\n"),
        "no-colon": u"BEGIN:VTIMEZONE\nTZID:X\nJUSTAWORD\nEND:VTIMEZONE\n",
        "comment-is-allowed": (u"BEGIN:VTIMEZONE\nTZID:X\nCOMMENT:hi\n"
                               u"BEGIN:STANDARD\nDTSTART:19700101T000000\n"
                               u"TZOFFSETFROM:+0000\nTZOFFSETTO:+0000\n"
                               u"COMMENT:also hi\nEND:STANDARD\n"
                               u"END:VTIMEZONE\n"),
    }
    out = {k: _try(lambda s=s: parse(s)) for k, s in sorted(bad.items())}
    # _parse_offset in isolation: the four-digit, six-digit, signed, unsigned
    # and malformed forms.
    c = tzical(io.StringIO(ICS_ONE))
    out["_parse_offset"] = {
        s: _try(lambda s=s: c._parse_offset(s))
        for s in ("+0530", "-0530", "0530", "+053045", "-053045", "  +0000  ",
                  "", "   ", "+53", "+05300", "abcd", "++0530", "+05:30")}
    return out


# ------------------------------------------------------------------- tzfile --

@case("dateutil.tzfile-transitions")
def _(ctx):
    # _find_ttinfo walks the transition list linearly and has four exits:
    # before the first transition, inside the list, past the end, and the
    # laststd=1 walk-back that dst() uses to keep utcoffset()-dst() constant.
    from dateutil.tz import tzfile
    path = _zone("America/New_York")
    if not os.path.isfile(path):
        return {"available": False}
    z = tzfile(path)
    ancient = datetime.datetime(1850, 1, 1)
    far = datetime.datetime(2400, 1, 1)
    return {"available": True, "repr": repr(z),
            # 2.4.2 has no ``hasdst`` attribute; the presence of a dst ttinfo
            # is the only way to ask.
            "has_dst_ttinfo": z._ttinfo_dst is not None,
            "before_first_transition": _describe(z, [ancient]),
            "winter": _describe(z, [WINTER]),
            "summer": _describe(z, [SUMMER]),
            "spring": _describe(z, AROUND_SPRING),
            "fall": _describe(z, AROUND_FALL),
            "past_last_transition": _describe(z, [far]),
            "n_transitions": len(z._trans_list),
            "ttinfo_std": repr(z._ttinfo_std),
            "ttinfo_dst": repr(z._ttinfo_dst),
            "ttinfo_before": repr(z._ttinfo_before),
            "ttinfo_list": [repr(t) for t in z._ttinfo_list]}


@case("dateutil.tzfile-no-dst-zone")
def _(ctx):
    # A zone that has never observed DST takes the ``if not self._ttinfo_dst``
    # early return in dst() and a different branch in tzname().
    from dateutil.tz import tzfile
    out = {}
    for name in ("UTC", "Asia/Tokyo", "Etc/GMT+5"):
        path = _zone(name)
        if not os.path.isfile(path):
            out[name] = {"available": False}
            continue
        z = tzfile(path)
        out[name] = {"repr": repr(z),
                     "has_dst_ttinfo": z._ttinfo_dst is not None,
                     "moments": _describe(z, [WINTER, SUMMER])}
    return out


@case("dateutil.tzfile-equality-and-pickle")
def _(ctx):
    import pickle
    from dateutil.tz import tzfile, tzutc
    path = _zone("America/New_York")
    if not os.path.isfile(path):
        return {"available": False}
    a, b = tzfile(path), tzfile(path)
    other = tzfile(_zone("Asia/Tokyo"))
    out = {"available": True, "eq_same_file": a == b,
           "ne_same_file": a != b,
           "eq_other_zone": a == other,
           "eq_wrong_type": a == tzutc(),
           "ne_wrong_type": a != tzutc(),
           "reduce": [repr(x) for x in a.__reduce__()]}
    for proto in (0, 2, pickle.HIGHEST_PROTOCOL):
        rt = pickle.loads(pickle.dumps(a, proto))
        out["pickle_%d" % proto] = {"eq": rt == a, "repr": repr(rt),
                                    "summer": _describe(rt, [SUMMER])}
    # A tzfile built from an open file object has no path to pickle through,
    # so __reduce__ must refuse rather than emit something unloadable.
    with open(path, "rb") as fh:
        anon = tzfile(fh)
    out["anonymous_repr"] = repr(anon)
    # An explicit protocol, not the default: 3.14 raised pickle's
    # default from 4 to 5, and these bytes are recorded verbatim, so
    # the default would make the observation a fact about the
    # interpreter rather than about __reduce__. Protocols 0, 2 and 4
    # are byte-identical here from 3.8 to 3.14; only the default moved.
    out["anonymous_pickle"] = _try(lambda: pickle.dumps(anon, 2))
    return out


@case("dateutil.tzfile-rejects", tags=("adversarial",))
def _(ctx):
    # The binary parser's error paths: wrong magic, a truncated header, a
    # truncated body, and an empty file.
    from dateutil.tz import tzfile
    path = _zone("America/New_York")
    if not os.path.isfile(path):
        return {"available": False}
    with open(path, "rb") as fh:
        good = fh.read()
    cases = {"empty": b"",
             "bad-magic": b"XXXX" + good[4:],
             "header-only": good[:20],
             "truncated-body": good[:60],
             "magic-only": b"TZif",
             "text": b"this is not a tzfile at all"}
    out = {"available": True}
    for name, blob in sorted(cases.items()):
        out[name] = _try(lambda b=blob: repr(tzfile(io.BytesIO(b))))
    # A file object opened in *text* mode: a 2/3 seam, since on 2 that is the
    # same bytes and on 3 it is a decode error waiting to happen.
    out["text-mode-handle"] = _try(
        lambda: repr(tzfile(io.open(path, "r", errors="replace"))))
    out["missing-path"] = _try(lambda: tzfile("/no/such/zone"))
    return out


@case("dateutil.ttinfo-slots-and-state", tags=("adversarial",))
def _(ctx):
    import pickle
    from dateutil.tz import _ttinfo
    a = _ttinfo()
    out = {"slots": list(_ttinfo.__slots__),
           "fresh_repr": repr(a),
           "has_dict": hasattr(a, "__dict__")}
    a.offset, a.delta, a.isdst = -18000, datetime.timedelta(hours=-5), 0
    a.abbr, a.isstd, a.isgmt = "EST", 1, 0
    b = _ttinfo()
    b.__setstate__(a.__getstate__())
    out["filled_repr"] = repr(a)
    out["getstate"] = {k: repr(v) for k, v in sorted(a.__getstate__().items())}
    out["roundtrip_eq"] = a == b
    out["eq_wrong_type"] = a == object()
    out["ne_wrong_type"] = a != object()
    out["partial_setstate"] = repr(
        _ttinfo()) if not b.__setstate__({"abbr": "X"}) else None
    out["after_partial"] = repr(b)
    for proto in (0, 2, pickle.HIGHEST_PROTOCOL):
        out["pickle_%d" % proto] = repr(pickle.loads(pickle.dumps(a, proto)))
    out["unknown_attr"] = _try(lambda: setattr(a, "nope", 1))
    return out


# -------------------------------------------------------------------- gettz --

@case("dateutil.gettz-lookup-paths")
def _(ctx):
    from dateutil.tz import gettz
    out = {}
    for name in ("America/New_York", "Etc/GMT-9", "America/Argentina/Buenos_Aires",
                 "/usr/share/zoneinfo/Asia/Tokyo", "/no/such/file",
                 ":Asia/Tokyoo", "GMT+3", "UTC0", ""):
        z = gettz(name)
        out[repr(name)] = {"type": type(z).__name__ if z is not None else None,
                           "repr": repr(z) if z is not None else None,
                           "summer": _describe(z, [SUMMER]) if z else None}
    # A name with a space: gettz retries after replacing ' ' with '_', which is
    # how "America/New York" resolves at all.
    out["space_retry"] = repr(gettz("America/New York"))
    return out


@case("dateutil.gettz-tz-env", tags=("adversarial",))
def _(ctx):
    # gettz() with no argument reads os.environ["TZ"]; the runner pins that to
    # "UTC" (hook 3), so what comes back is fully determined. The leading-colon
    # form is the interesting one: gettz strips the *last* character, not the
    # first -- ``name = name[:-1]`` where ``name[1:]`` was meant. Pinned as a
    # latent defect so the migration cannot silently correct it.
    from dateutil.tz import gettz
    return {"TZ": os.environ.get("TZ"),
            "no_arg": repr(gettz()),
            "no_arg_summer": _describe(gettz(), [SUMMER]),
            "colon_prefix": repr(gettz(":UTCX")),
            "colon_only": repr(gettz(":")),
            "tzfiles": __import__("dateutil.tz", fromlist=["tz"]).TZFILES,
            "tzpaths": __import__("dateutil.tz", fromlist=["tz"]).TZPATHS}


# ----------------------------------------------------------- tzstr / tzrange --

@case("dateutil.tzstr-forms")
def _(ctx):
    from dateutil.tz import tzstr
    out = {}
    for s in ("EST5EDT", "EST5EDT,M3.2.0/2,M11.1.0/2", "UTC", "GMT+3",
              "JST-9", "<+04>-4", "CET-1CEST,M3.5.0,M10.5.0/3",
              "AEST-10AEDT,M10.1.0,M4.1.0/3"):
        out[s] = _try(lambda s=s: {"repr": repr(tzstr(s)),
                                   "moments": _describe(tzstr(s),
                                                        [WINTER, SUMMER])})
    return out


@case("dateutil.tzstr-rejects", tags=("adversarial",))
def _(ctx):
    from dateutil.tz import tzstr
    return {repr(s): _try(lambda s=s: repr(tzstr(s)))
            for s in ("", "   ", "5", ",,,", "EST5EDT,bogus",
                      "EST5EDT,M3.2.0/2", u"日本", None, 5)}


@case("dateutil.tzrange-isdst-boundaries", tags=("adversarial",))
def _(ctx):
    # tzrange._isdst compares the naive dt against transitions it computes for
    # dt.year with dateutil.relativedelta. Straddling the transition in both
    # directions is the only way to reach both sides of that comparison, and
    # the ambiguous and nonexistent local hours are the inputs a migration is
    # most likely to move by an hour without noticing.
    from dateutil.relativedelta import relativedelta, SU
    from dateutil.tz import tzrange
    z = tzrange("EST", -5 * 3600, "EDT", -4 * 3600,
                relativedelta(hours=+2, month=3, day=1, weekday=SU(+2)),
                relativedelta(hours=+1, month=11, day=1, weekday=SU(+1)))
    return {"repr": repr(z),
            "spring": _describe(z, AROUND_SPRING),
            "fall": _describe(z, AROUND_FALL),
            "winter": _describe(z, [WINTER]),
            "summer": _describe(z, [SUMMER]),
            "eq_same": z == tzrange("EST", -5 * 3600, "EDT", -4 * 3600,
                                    relativedelta(hours=+2, month=3, day=1,
                                                  weekday=SU(+2)),
                                    relativedelta(hours=+1, month=11, day=1,
                                                  weekday=SU(+1))),
            "eq_wrong_type": z == object(),
            "ne_wrong_type": z != object(),
            "std_only": repr(tzrange("UTC", 0)),
            "std_only_moments": _describe(tzrange("UTC", 0), [WINTER, SUMMER]),
            "default_offsets": repr(tzrange("STD"))}
