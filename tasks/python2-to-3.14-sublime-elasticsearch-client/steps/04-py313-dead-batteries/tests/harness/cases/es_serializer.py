# -*- coding: utf-8 -*-
"""elasticsearch.serializer — the str/bytes and number boundary of the client.

``JSONSerializer.dumps`` is the single place every document body passes
through, and ``compat.string_types`` (``str, bytes`` on 3, ``basestring`` on 2)
is what decides whether a value is passed through untouched or JSON-encoded.
That branch is the tree's central 2/3 seam, so it is exercised exhaustively.
"""
from ..registry import case


def _js():
    from elasticsearch.serializer import JSONSerializer
    return JSONSerializer()


def _ts():
    from elasticsearch.serializer import TextSerializer
    return TextSerializer()


def _deser(default="application/json"):
    from elasticsearch.serializer import Deserializer, DEFAULT_SERIALIZERS
    return Deserializer(DEFAULT_SERIALIZERS.copy(), default)


# --------------------------------------------------------------- dumps ----

@case("ser.dumps.dict-ascii")
def _(ctx):
    return _js().dumps({"query": {"match_all": {}}})


@case("ser.dumps.dict-order-preserved")
def _(ctx):
    # Insertion order is observable in the emitted JSON on every interpreter
    # in this ladder; the task line names dict ordering explicitly.
    return _js().dumps({"z": 1, "a": 2, "m": 3, "b": 4})


@case("ser.dumps.nonascii-values")
def _(ctx):
    return _js().dumps({"title": u"日本語", "body": u"café", "emoji": u"🍣"})


@case("ser.dumps.nonascii-keys")
def _(ctx):
    return _js().dumps({u"著者": u"松尾", u"naïve": True})


@case("ser.dumps.str-passthrough")
def _(ctx):
    # A str is returned verbatim, NOT JSON-encoded. This is the bulk-body path.
    return _js().dumps(u'{"index":{"_id":1}}\n{"a":1}\n')


@case("ser.dumps.bytes-passthrough", tags=("adversarial",))
def _(ctx):
    # bytes is in string_types on Python 3 only. On 2 it is `basestring` too,
    # so this returns the bytes unchanged on both — recorded to lock it down.
    return _js().dumps(b'{"already":"encoded"}')


@case("ser.dumps.bytes-invalid-utf8", tags=("adversarial",))
def _(ctx):
    return _js().dumps(b"\xff\xfe not utf-8")


@case("ser.dumps.date")
def _(ctx):
    import datetime
    return _js().dumps({"at": datetime.date(2016, 7, 8)})


@case("ser.dumps.datetime-naive")
def _(ctx):
    import datetime
    return _js().dumps({"at": datetime.datetime(2016, 7, 8, 12, 34, 56, 789012)})


@case("ser.dumps.datetime-aware")
def _(ctx):
    import datetime
    from dateutil.tz import tzoffset
    return _js().dumps({"at": datetime.datetime(2016, 7, 8, 12, 0, 0,
                                                tzinfo=tzoffset("JST", 9 * 3600))})


@case("ser.dumps.decimal")
def _(ctx):
    import decimal
    return _js().dumps({"score": decimal.Decimal("1.5"),
                        "exact": decimal.Decimal("0.1")})


@case("ser.dumps.big-int", tags=("adversarial",))
def _(ctx):
    # 2**63 is a Python 2 `long`; the JSON text must be identical on 3.
    return _js().dumps({"n": 2 ** 63, "neg": -(2 ** 63) - 1})


@case("ser.dumps.floats", tags=("adversarial",))
def _(ctx):
    return _js().dumps({"a": 1.0, "b": 0.1 + 0.2, "c": 1e308, "d": -0.0})


@case("ser.dumps.nan-inf", tags=("adversarial",))
def _(ctx):
    return _js().dumps({"nan": float("nan"), "inf": float("inf"),
                        "ninf": float("-inf")})


@case("ser.dumps.bool-vs-int", tags=("adversarial",))
def _(ctx):
    return _js().dumps({"t": True, "one": 1, "f": False, "zero": 0})


@case("ser.dumps.set-raises", tags=("adversarial",))
def _(ctx):
    return _js().dumps({"tags": {"a", "b"}})


@case("ser.dumps.object-raises", tags=("adversarial",))
def _(ctx):
    return _js().dumps({"o": object()})


@case("ser.dumps.self-referential", tags=("adversarial",))
def _(ctx):
    d = {}
    d["self"] = d
    return _js().dumps(d)


@case("ser.dumps.nested-deep")
def _(ctx):
    node = {"leaf": 1}
    for i in range(20):
        node = {"n%d" % i: node}
    return _js().dumps(node)


@case("ser.dumps.tuple-becomes-list")
def _(ctx):
    return _js().dumps({"t": (1, 2, 3)})


@case("ser.dumps.none")
def _(ctx):
    return _js().dumps({"v": None})


@case("ser.dumps.empty-containers")
def _(ctx):
    return _js().dumps({"d": {}, "l": [], "s": ""})


@case("ser.dumps.control-chars", tags=("adversarial",))
def _(ctx):
    return _js().dumps({"v": u"a\tb\nc\r\x00d\x1fe f g"})


@case("ser.dumps.surrogate-pair", tags=("adversarial",))
def _(ctx):
    return _js().dumps({"v": u"\U0001F363"})


@case("ser.dumps.lone-surrogate", tags=("adversarial",))
def _(ctx):
    return _js().dumps({"v": u"\ud800"})


@case("ser.dumps.default-hook-direct")
def _(ctx):
    import datetime
    import decimal
    s = _js()
    return [s.default(datetime.date(2001, 2, 3)),
            s.default(datetime.datetime(2001, 2, 3, 4, 5, 6)),
            s.default(decimal.Decimal("2.25"))]


@case("ser.dumps.default-hook-raises", tags=("adversarial",))
def _(ctx):
    # Everything default() does not recognise becomes a TypeError whose message
    # interpolates repr(data) and type(data). Both halves are spelled
    # differently on 2 and 3 -- repr(b'x') and <class 'x'> against <type 'x'> --
    # so this message is one of the places the port is most visible, and it is
    # what a caller sees when a document will not serialize.
    import decimal
    s = _js()
    out = {}
    for label, value in (("object", object()),
                         ("bytes", b"x"),
                         ("set", set([1])),
                         ("complex", complex(1, 2)),
                         # A Decimal subclass is accepted, because the branch
                         # is an isinstance check; recorded beside the rejects
                         # so the boundary itself is pinned, not just one side.
                         ("decimal_subclass",
                          type("D", (decimal.Decimal,), {})("2.25")),
                         ("type", int)):
        try:
            out[label] = {"ok": s.default(value)}
        except Exception as exc:
            out[label] = {"raised": type(exc).__name__, "str": str(exc)}
    return out


# --------------------------------------------------------------- loads ----

@case("ser.loads.object")
def _(ctx):
    return _js().loads(u'{"took":5,"hits":{"total":2,"max_score":1.0}}')


@case("ser.loads.nonascii")
def _(ctx):
    return _js().loads(u'{"t":"\\u65e5\\u672c\\u8a9e","raw":"日本語"}')


@case("ser.loads.bytes-input", tags=("adversarial",))
def _(ctx):
    return _js().loads(b'{"a":1}')


@case("ser.loads.invalid", tags=("adversarial",))
def _(ctx):
    return _js().loads(u"{not json")


@case("ser.loads.empty", tags=("adversarial",))
def _(ctx):
    return _js().loads(u"")


@case("ser.loads.none-input", tags=("adversarial",))
def _(ctx):
    return _js().loads(None)


@case("ser.loads.number-precision", tags=("adversarial",))
def _(ctx):
    return _js().loads(u'{"big":12345678901234567890,"f":1.0,"e":1e-7}')


@case("ser.loads.duplicate-keys", tags=("adversarial",))
def _(ctx):
    return _js().loads(u'{"a":1,"a":2}')


# ---------------------------------------------------------- text/deser ----

@case("ser.text.str")
def _(ctx):
    return _ts().dumps(u"plain text")


@case("ser.text.bytes", tags=("adversarial",))
def _(ctx):
    return _ts().dumps(b"plain bytes")


@case("ser.text.nonstring-raises", tags=("adversarial",))
def _(ctx):
    return _ts().dumps({"a": 1})


@case("ser.text.loads-passthrough")
def _(ctx):
    return _ts().loads(u"anything at all")


@case("ser.deser.default-mimetype")
def _(ctx):
    return _deser().loads(u'{"a":1}')


@case("ser.deser.charset-split")
def _(ctx):
    return _deser().loads(u'{"a":1}', "application/json; charset=UTF-8")


@case("ser.deser.text-plain")
def _(ctx):
    return _deser().loads(u"hello", "text/plain")


@case("ser.deser.unknown-mimetype", tags=("adversarial",))
def _(ctx):
    return _deser().loads(u"x", "application/x-msgpack")


@case("ser.deser.missing-default-raises", tags=("adversarial",))
def _(ctx):
    from elasticsearch.serializer import Deserializer, DEFAULT_SERIALIZERS
    return Deserializer(DEFAULT_SERIALIZERS.copy(), "application/nope")


@case("ser.deser.empty-mimetype-uses-default")
def _(ctx):
    return _deser().loads(u'{"a":1}', "")
