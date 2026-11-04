# -*- coding: utf-8 -*-
"""The vendored python-dateutil 2.4.2, as the tree actually uses it.

Only ``dateutil.tz`` is reachable from this repo -- ``analytics`` imports
``tzutc``, ``tzlocal`` from it and nothing else does. That module is still a
2/3 hybrid: it imports ``string_types`` and ``PY3`` from ``six`` and wraps every
``tzname`` implementation in a ``tzname_in_python2`` decorator that encodes the
result to bytes on 2. Retiring six means rewriting that file, so what its
classes return is pinned here.

The wall clock is not frozen (see harness/env.py), so every case below either
supplies its own datetime or observes only clock-independent structure.
"""
import datetime

from ..registry import case


def _try(fn):
    """Record one probe's value or the exception it raised.

    Per probe rather than per case: a single latent defect must not delete the
    evidence for everything recorded beside it.
    """
    try:
        return {"ok": fn()}
    except Exception as exc:
        return {"raised": type(exc).__name__, "str": str(exc)}

AWARE_SUMMER = datetime.datetime(2015, 7, 1, 12, 0, 0)
AWARE_WINTER = datetime.datetime(2015, 1, 1, 12, 0, 0)


@case("dateutil.tzutc")
def _(ctx):
    from dateutil.tz import tzutc
    z = tzutc()
    return {"repr": repr(z), "str": str(z),
            "utcoffset": z.utcoffset(AWARE_SUMMER),
            "dst": z.dst(AWARE_SUMMER),
            "tzname": z.tzname(AWARE_SUMMER),
            "tzname_type": type(z.tzname(AWARE_SUMMER)).__name__,
            "eq_self": z == tzutc(),
            # A *latent defect*, pinned rather than worked around: tzutc
            # defines __eq__ and no __hash__, so Python 3 sets __hash__ to
            # None and every tzutc() is unhashable -- which means a datetime
            # carrying one cannot be a dict key either. dateutil 2.4.2 predates
            # the fix. Scoring the TypeError stops a migration from "tidying"
            # it into existence and changing what `{dt: v}` does.
            "hash": _try(lambda: hash(z) == hash(tzutc())),
            "as_dict_key": _try(
                lambda: {AWARE_SUMMER.replace(tzinfo=z): 1}),
            "applied": AWARE_SUMMER.replace(tzinfo=z).isoformat()}


@case("dateutil.tzoffset")
def _(ctx):
    from dateutil.tz import tzoffset
    z = tzoffset(u"日本標準時", 9 * 3600)
    n = tzoffset(None, -5 * 3600)
    return {"repr": repr(z), "name_unicode": z.tzname(AWARE_SUMMER),
            "utcoffset": z.utcoffset(AWARE_SUMMER),
            "dst": z.dst(AWARE_SUMMER),
            "negative_repr": repr(n),
            "negative_offset": n.utcoffset(AWARE_SUMMER),
            "applied": AWARE_SUMMER.replace(tzinfo=z).isoformat(),
            "eq": z == tzoffset(u"日本標準時", 9 * 3600),
            "ne_other_name": z == tzoffset("JST", 9 * 3600)}


@case("dateutil.tzlocal-under-pinned-tz")
def _(ctx):
    # The runner pins TZ=UTC and calls time.tzset(), so tzlocal is fully
    # determined: any drift here would be the migration changing how the
    # module reads the C library, not the environment moving.
    import os
    from dateutil.tz import tzlocal
    z = tzlocal()
    return {"TZ": os.environ.get("TZ"), "repr": repr(z),
            "utcoffset_summer": z.utcoffset(AWARE_SUMMER),
            "utcoffset_winter": z.utcoffset(AWARE_WINTER),
            "dst_summer": z.dst(AWARE_SUMMER),
            "tzname": z.tzname(AWARE_SUMMER),
            "eq_utc": z == tzlocal()}


@case("dateutil.tzstr-posix-rule")
def _(ctx):
    # A POSIX TZ string with a DST rule: the winter/summer split is computed
    # by dateutil itself, not by the platform, so it is the purest test of the
    # module's own arithmetic.
    from dateutil.tz import tzstr
    z = tzstr("EST5EDT,M3.2.0/2,M11.1.0/2")
    return {"repr": repr(z),
            "winter": [z.utcoffset(AWARE_WINTER), z.dst(AWARE_WINTER),
                       z.tzname(AWARE_WINTER)],
            "summer": [z.utcoffset(AWARE_SUMMER), z.dst(AWARE_SUMMER),
                       z.tzname(AWARE_SUMMER)],
            "eq": z == tzstr("EST5EDT,M3.2.0/2,M11.1.0/2")}


@case("dateutil.tzrange")
def _(ctx):
    from dateutil.tz import tzrange
    z = tzrange("STD", -5 * 3600, "DST", -4 * 3600)
    return {"repr": repr(z),
            "winter": [z.utcoffset(AWARE_WINTER), z.tzname(AWARE_WINTER)],
            "summer": [z.utcoffset(AWARE_SUMMER), z.tzname(AWARE_SUMMER)],
            "no_dst": repr(tzrange("STD", -5 * 3600))}


@case("dateutil.tzfile-from-zoneinfo", tags=("adversarial",))
def _(ctx):
    # tzfile parses the binary TZif format with struct.unpack and slices the
    # buffer with integer arithmetic -- the most byte-oriented code in the
    # dependency set. The system database is read-only and stable; if it is
    # absent the case records that fact rather than failing.
    import os
    from dateutil.tz import tzfile
    path = "/usr/share/zoneinfo/Asia/Tokyo"
    if not os.path.exists(path):
        return {"available": False}
    with open(path, "rb") as fh:
        z = tzfile(fh, filename="Asia/Tokyo")
    return {"available": True, "repr": repr(z),
            "utcoffset": z.utcoffset(AWARE_SUMMER),
            "tzname": z.tzname(AWARE_SUMMER),
            "dst": z.dst(AWARE_SUMMER),
            # 2.4.2's tzfile has no ``hasdst``; whether a dst ttinfo was found
            # while parsing is the only way to ask, and it is what dst() itself
            # branches on.
            "has_dst_ttinfo": z._ttinfo_dst is not None,
            "applied": AWARE_SUMMER.replace(tzinfo=z).isoformat()}


@case("dateutil.gettz")
def _(ctx):
    from dateutil.tz import gettz, tzlocal, tzutc
    out = {}
    for name in (None, "UTC", "Asia/Tokyo", "Etc/GMT+5", "Not/AZone",
                 "EST5EDT,M3.2.0/2,M11.1.0/2"):
        z = gettz(name)
        out[str(name)] = {
            "type": type(z).__name__ if z is not None else None,
            "offset": (z.utcoffset(AWARE_SUMMER) if z is not None else None),
            "name": (z.tzname(AWARE_SUMMER) if z is not None else None)}
    out["local_is_tzlocal"] = isinstance(gettz(None), (tzlocal, tzutc))
    return out


@case("dateutil.tzname-return-type-is-str", tags=("adversarial",))
def _(ctx):
    # tzname_in_python2 encodes the name to bytes on Python 2 only. On 3 the
    # decorator is a no-op, so every tzname must already be str. A migration
    # that deletes the decorator must not change these types.
    import os
    from dateutil.tz import gettz, tzlocal, tzoffset, tzstr, tzutc
    zones = [tzutc(), tzoffset("ABC", 3600), tzlocal(),
             tzstr("EST5EDT,M3.2.0/2,M11.1.0/2")]
    if os.path.exists("/usr/share/zoneinfo/Asia/Tokyo"):
        zones.append(gettz("Asia/Tokyo"))
    return [[type(z).__name__, type(z.tzname(AWARE_SUMMER)).__name__,
             z.tzname(AWARE_SUMMER)] for z in zones]


@case("dateutil.aware-arithmetic")
def _(ctx):
    from dateutil.tz import tzoffset, tzutc
    jst = tzoffset("JST", 9 * 3600)
    a = AWARE_SUMMER.replace(tzinfo=jst)
    b = AWARE_SUMMER.replace(tzinfo=tzutc())
    return {"delta_seconds": (b - a).total_seconds(),
            "astimezone": a.astimezone(tzutc()).isoformat(),
            "lt": a < b, "eq": a == b,
            "timestamp": b.timestamp(),
            # The aware spelling deliberately: datetime.utcfromtimestamp is
            # deprecated from 3.12 and named for removal on this ladder, so a
            # case calling it would stop being passable partway up.
            "fromtimestamp_utc": datetime.datetime.fromtimestamp(
                0, tzutc()).isoformat()}


@case("dateutil.zoneinfo-bundled-tarball", tags=("adversarial",))
def _(ctx):
    # ``dateutil/zoneinfo`` reads a 2016-vintage tz database out of a gzipped
    # tarball shipped inside the package, with ``pkgutil.get_data`` and
    # ``TarFile``. Three things there are exactly the kind of 2.6-era scaffolding
    # a migration sweeps away, and all three are observable:
    #
    #   * ``_tar_open`` is rebound to a ``closing()`` wrapper if ``TarFile`` has
    #     no ``__exit__``; on anything modern the rebinding never happens, so
    #     deleting it must not change what the tarball yields;
    #   * links in the tarball are resolved by *aliasing the same tzfile object*,
    #     not by re-reading, so identity is part of the contract;
    #   * ``gettz`` memoises through a module-level one-element list used as a
    #     mutable global, and the second call must return the same object.
    #
    # The zone count is pinned too: it is a property of the bundled file, which
    # is a fixed artefact in the tree, so it cannot drift with the host's tz
    # database or the clock.
    import pickle
    from dateutil import zoneinfo
    z = zoneinfo.gettz("Asia/Tokyo")
    back = pickle.loads(pickle.dumps(z))
    return {"type": [type(z).__name__, type(z).__module__],
            "repr": repr(z),
            "utcoffset": z.utcoffset(AWARE_SUMMER),
            "tzname": z.tzname(AWARE_SUMMER),
            "memoised": zoneinfo.gettz("Asia/Tokyo") is z,
            "unknown_zone": zoneinfo.gettz("Not/AZone"),
            "link_aliases_parent": (zoneinfo.gettz("US/Eastern")
                                    is zoneinfo.gettz("America/New_York")),
            "n_zones": len(zoneinfo.ZoneInfoFile(
                zoneinfo.getzoneinfofile_stream()).zones),
            "no_stream_is_empty": zoneinfo.ZoneInfoFile(None).zones,
            # __reduce__ returns (gettz, (filename,)), so unpickling goes back
            # through the memoised loader rather than re-parsing the bytes.
            "pickle_roundtrip": [repr(back), back.utcoffset(AWARE_SUMMER),
                                 back is z],
            "all": sorted(zoneinfo.__all__)}


@case("dateutil.module-exports")
def _(ctx):
    import dateutil
    import dateutil.tz as tz
    # ``__all__`` is the module's stated export list and survives the ladder
    # unchanged. The private ``tzname_in_python2`` decorator is not probed:
    # it is dead weight on every interpreter in the ladder, so a step that
    # requires the Python 2 branches gone leaves a candidate no answer that
    # both passes this and satisfies its instruction.
    return {"version": dateutil.__version__,
            "all": sorted(tz.__all__)}
