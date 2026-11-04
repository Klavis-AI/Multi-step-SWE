# -*- coding: utf-8 -*-
"""The vendored requests 2.7.0.

``requests/compat.py`` is a hand-written 2/3 bridge and ``requests/utils.py``
reaches for ``cgi.parse_header`` to split a Content-Type header -- ``cgi`` is
gone from the standard library at 3.13, so every response whose encoding the
tree infers runs through code that has to be replaced. These cases pin what
that inference produces today, header by header.
"""
import io

from ..registry import case
from ..server import Route, gzip_route, json_route

DOC = {"ok": True, "t": u"日本語"}


def _try(fn):
    """Record one probe's value or the exception it raised.

    Per probe rather than per case: a single latent defect must not delete the
    evidence for everything recorded beside it.
    """
    try:
        return {"ok": fn()}
    except Exception as exc:
        return {"raised": type(exc).__name__, "str": str(exc)}


def ok():
    return {"*": json_route(DOC)}


# ------------------------------------------------- header parsing (cgi) ----

@case("requests.encoding-from-headers")
def _(ctx):
    # get_encoding_from_headers is the only caller of cgi.parse_header in the
    # tree. Each of these header spellings exercises a different branch of the
    # RFC 2045 parameter grammar cgi implements.
    from requests.utils import get_encoding_from_headers
    headers = [
        {},
        {"content-type": "application/json"},
        {"content-type": "text/html"},
        {"content-type": "text/plain; charset=UTF-8"},
        {"content-type": "text/plain;charset=utf-8"},
        {"content-type": 'text/plain; charset="utf-8"'},
        {"content-type": "text/plain; charset='iso-8859-1'"},
        {"content-type": "application/json; charset=Shift_JIS"},
        {"content-type": "text/plain; charset=utf-8; boundary=xyz"},
        {"content-type": "text/plain ; charset = utf-8 "},
        {"content-type": "TEXT/PLAIN; CHARSET=UTF-8"},
        {"content-type": ""},
        {"content-type": ";charset=utf-8"},
        {"content-type": "text/plain; charset=\\\"weird\\\""},
        {"content-type": "multipart/form-data; boundary=--x"},
    ]
    return [[h.get("content-type"), get_encoding_from_headers(h)]
            for h in headers]


@case("requests.encoding-from-headers-adversarial", tags=("adversarial",))
def _(ctx):
    # Malformed values a real server can still send. cgi.parse_header never
    # raises; whatever replaces it must not start raising either.
    from requests.utils import get_encoding_from_headers
    bad = ["text/plain; charset", "text/plain; =utf-8", "text/plain;;;",
           'text/plain; charset="unterminated', u"text/plain; charset=日本語",
           "text/plain; charset=utf-8; charset=latin-1",
           "text/plain; charset=", "  ", "/", "text/plain\x00; charset=utf-8"]
    out = []
    for v in bad:
        try:
            out.append([v, get_encoding_from_headers({"content-type": v})])
        except Exception as e:
            out.append([v, "%s: %s" % (type(e).__name__, e)])
    return out



# The three cases below widen the same surface, and they exist because the
# surface was mispriced. `cgi.parse_header` is fifteen lines of RFC 2045
# parameter grammar, and the replacement an agent reaches for first --
# `email.message.Message` -- disagrees with it. Which inputs it disagrees on
# depends on which `email` API is used: `get_params()[0][0]` diverges on
# `'text/plain; charset'`, `get_content_type()` diverges on six degenerate
# values because it substitutes `text/plain` for anything it cannot parse, and
# both decode RFC 2231 continuations that `cgi` hands back raw. Two recorded
# cases priced that whole family of wrong answers at one case; these price it
# by grammar branch instead, so a port that is wrong is wrong in a number.


def _encodings_for(values):
    from requests.utils import get_encoding_from_headers
    out = []
    for v in values:
        try:
            out.append([v, get_encoding_from_headers({"content-type": v})])
        except Exception as e:
            out.append([v, "%s: %s" % (type(e).__name__, e)])
    return out


@case("requests.encoding-from-headers-quoting", tags=("adversarial",))
def _(ctx):
    # The quoted-string half of the grammar. A parser that splits on ";" and
    # strips quotes at the end gets the first two of these wrong, and a parser
    # that requires quotes to balance gets the next two wrong.
    return _encodings_for([
        'text/html; boundary="a;b"; charset=utf-8',
        'text/html; filename="x=y;z"; charset=latin-1',
        'text/html; charset="a\\"b"',
        'text/html; charset="utf-8',
        'text/html; charset=utf-8"',
        'text/html; charset=""',
        'text/html; charset="utf\\-8"',
        'text/html; charset="  utf-8  "',
        "text/html; charset='utf-8'",
        u'text/html; charset="\u65e5\u672c\u8a9e"',
    ])


@case("requests.encoding-from-headers-degenerate", tags=("adversarial",))
def _(ctx):
    # Values that are not content types. `cgi.parse_header` returns whatever is
    # left of the first ";" verbatim, including the empty string, and
    # `get_encoding_from_headers` then asks whether "text" is a substring of
    # it -- so the answer for `'attachment; filename=...'` is None and the
    # answer for `'text'` is ISO-8859-1. Anything that normalises an
    # unparseable value to `text/plain` inverts both.
    return _encodings_for([
        "   ",
        ";",
        "; charset=utf-8",
        "text",
        "/",
        "/plain",
        "text/",
        'attachment; filename="a;b.txt"',
        "text/html;;charset=utf-8",
        "text/html; charset=utf-8;",
        "text/html;",
    ])


@case("requests.encoding-from-headers-rfc2231", tags=("adversarial",))
def _(ctx):
    # RFC 2231 extended parameters. `cgi.parse_header` has never implemented
    # them: it keeps the trailing "*" in the parameter name, so `charset*=` is
    # not `charset` and the lookup misses. `email` does implement them, decodes
    # the value into a (charset, language, text) triple, and files it under
    # `charset` -- which changes the answer on the first five of these.
    return _encodings_for([
        "text/html; charset*=UTF-8''utf-8",
        "text/html; charset*=utf-8''%E6%97%A5",
        "text/html; charset*0=utf; charset*1=-8",
        "text/html; charset*=''utf-8",
        "text/html; charset*=UTF-8'en'utf-8",
        "text/html; charset=utf-8; charset*=UTF-8''latin-1",
    ])


@case("requests.encoding-from-headers-real-world")
def _(ctx):
    # Values copied from what the servers this client actually talks to send.
    # Nothing adversarial here on purpose: this is the case that says the port
    # still works on the ordinary traffic, and it is the one a port that
    # over-corrects for the adversarial cases above will break.
    return _encodings_for([
        "application/json; charset=UTF-8",
        "application/json;charset=UTF-8",
        "application/x-ndjson; charset=UTF-8",
        "text/plain; charset=utf-8",
        "text/html; charset=iso-8859-1",
        "application/xml; charset=us-ascii; version=1.0",
        "application/octet-stream",
        "application/x-www-form-urlencoded",
        "multipart/form-data; boundary=----WebKitFormBoundaryABC123",
        "text/css",
        "text/javascript; charset=UTF-8",
        "image/png",
        "application/vnd.api+json",
        "text/event-stream",
        "application/json; q=0.9; charset=Shift_JIS",
        "text/html;charset=gb2312",
        "application/json ; charset = EUC-KR ",
        "TEXT/HTML; CHARSET=Windows-1251",
        "application/json; charset=utf-8; boundary=ignored",
        "text/xml; charset=KOI8-R",
    ])


WIRE_TYPES = [
    ("json-utf8", "application/json; charset=UTF-8"),
    ("json-bare", "application/json"),
    ("text-bare", "text/plain"),
    ("text-quoted", 'text/plain; charset="utf-8"'),
    ("text-valueless", "text/plain; charset"),
    ("text-2231", "text/plain; charset*=UTF-8''utf-8"),
    ("text-quoted-semicolon", 'text/plain; boundary="a;b"; charset=utf-8'),
    ("not-a-type", 'attachment; filename="a;b.txt"'),
    ("empty", ""),
]

WIRE_BODY = u"caf\u00e9 \u65e5\u672c\u8a9e"


def _wire_routes():
    return dict(("/" + label, Route(body=WIRE_BODY.encode("utf-8"),
                                    headers=[("content-type", ct)]))
                for label, ct in WIRE_TYPES)


@case("requests.response-encoding-through-the-wire",
      fixture=_wire_routes)
def _(ctx):
    # The same grammar again, but through `requests.get(...)` rather than
    # through the helper -- `Response.encoding` is what the header parser is
    # *for*, and `.text` is the value the caller actually sees change when the
    # parser's answer moves. A port that is right in `get_encoding_from_headers`
    # and wrong about where requests calls it fails here and nowhere else.
    import requests
    out = {}
    for label, ct in WIRE_TYPES:
        r = requests.get(ctx.url("/" + label))
        out[label] = {"sent": ct, "encoding": r.encoding,
                      "apparent": r.apparent_encoding,
                      "text": r.text, "roundtrips": r.text == WIRE_BODY}
    return out


# ------------------------------------------- charset detection (chardet) ---
#
# When the header carries no charset *and* the type is not ``text/*``,
# ``get_encoding_from_headers`` returns None and ``Response.text`` falls through
# to ``apparent_encoding``, which is the vendored chardet 2.3.0. That is a
# thousand lines of table-driven state machines -- the least Python-like code in
# the tree and the most likely to be quietly broken by a codemod, because every
# probe is a class full of integer arithmetic over ``bytes`` whose py2 spelling
# indexed to a str. Each family below routes to a different prober, and the
# payloads are chosen so the detector answers with confidence rather than by
# coin-flip: the recorded confidence is part of the observation.
#
# Driven through ``requests.get(...).text``, not by calling the detector: the
# point is what the tree returns to its caller, and ``apparent_encoding`` is on
# the public surface that step 04 has to keep working when ``cgi`` disappears
# from under ``get_encoding_from_headers``.

CHARSET_TEXT = {
    "ja": u"日本語のテキストです。文字コードの判定を確認します。" * 3,
    "ko": u"한국어 텍스트입니다. 문자 인코딩 판별을 확인합니다." * 3,
    "zh-hans": u"这是一段简体中文测试文字，用来确认编码侦测。" * 3,
    "zh-hant": u"這是一段繁體中文測試文字，用來確認編碼偵測。" * 3,
    "he": (u"שלום עולם. זהו טקסט בעברית שנועד לבדוק את מנגנון זיהוי הקידוד. "
           u"אנחנו כותבים מספיק מילים כדי שהמנתח יוכל להחליט בביטחון סביר.") * 3,
    "ru": u"Привет мир, это тест кодировки текста. " * 6,
}


ESCAPE_CODECS = (("iso-2022-jp", "iso2022_jp", "ja"),
                 ("iso-2022-kr", "iso2022_kr", "ko"),
                 ("hz-gb-2312", "hz", "zh-hans"))
MULTIBYTE_CODECS = (("big5", "big5", "zh-hant"),
                    ("gb2312", "gb2312", "zh-hans"),
                    ("euc-kr", "euc_kr", "ko"),
                    ("shift-jis", "shift_jis", "ja"),
                    ("euc-jp", "euc_jp", "ja"))
SINGLEBYTE_CODECS = (("windows-1255", "cp1255", "he"),
                     ("koi8-r", "koi8_r", "ru"))


def _charset_routes(pairs):
    """One path per payload, served as ``application/octet-stream``.

    ``octet-stream`` rather than ``text/*`` on purpose: RFC 2616 makes
    ``text/*`` default to ISO-8859-1 and requests implements that, which would
    short-circuit the detector these cases exist to drive.
    """
    return dict(("/" + label,
                 Route(body=CHARSET_TEXT[key].encode(codec),
                       headers=[("content-type", "application/octet-stream")]))
                for label, codec, key in pairs)


def _detect_through_requests(ctx, pairs):
    import requests
    out = {}
    for label, codec, key in pairs:
        r = requests.get(ctx.url("/" + label))
        out[label] = {"len": len(r.content), "apparent": r.apparent_encoding,
                      "header_encoding": r.encoding,
                      "text_head": r.text[:24],
                      "roundtrips": r.text == CHARSET_TEXT[key]}
    return out


@case("requests.apparent-encoding-escape-sequences", tags=("adversarial",),
      fixture=lambda: _charset_routes(ESCAPE_CODECS))
def _(ctx):
    # The ISO-2022 / HZ family: stateful encodings that announce themselves
    # with ESC sequences, handled by escprober + escsm, the single
    # worst-covered module in the dependency set before this case existed.
    return _detect_through_requests(ctx, ESCAPE_CODECS)


@case("requests.apparent-encoding-multibyte", tags=("adversarial",),
      fixture=lambda: _charset_routes(MULTIBYTE_CODECS))
def _(ctx):
    # The multi-byte group: chardistribution's frequency tables and, for the
    # two Japanese ones, jpcntx's two-character context analysis.
    return _detect_through_requests(ctx, MULTIBYTE_CODECS)


@case("requests.apparent-encoding-single-byte", tags=("adversarial",),
      fixture=lambda: _charset_routes(SINGLEBYTE_CODECS))
def _(ctx):
    # The single-byte group. Hebrew is the interesting one: hebrewprober is
    # the only prober that decides between two *encodings of the same
    # language* (logical vs visual order) by counting final-form letters.
    return _detect_through_requests(ctx, SINGLEBYTE_CODECS)


OCTET = [("content-type", "application/octet-stream")]


@case("requests.apparent-encoding-refuses-and-gives-up", tags=("adversarial",),
      fixture=lambda: {"/empty": Route(body=b"", headers=OCTET),
                       "/ascii": Route(body=b"plain ascii, nothing to see",
                                       headers=OCTET)})
def _(ctx):
    # The two answers that are not an encoding.
    #
    # An empty body and pure ASCII both leave the detector with nothing to go
    # on, and it says so differently in each case -- that difference is load
    # bearing, because ``Response.text`` branches on ``encoding is None``.
    #
    # ``detect`` is also called directly here, with a str, for the one branch
    # the public surface cannot reach: ``Response.apparent_encoding`` always
    # hands it ``self.content``, so the guard that rejects text is only
    # reachable by name. That guard is spelled as a ``version_info < (3, 0)``
    # branch naming ``unicode``, which step 01 must rewrite, so what it does
    # afterwards is worth pinning.
    import requests
    from requests.packages import chardet
    r_empty = requests.get(ctx.url("/empty"))
    r_ascii = requests.get(ctx.url("/ascii"))
    return {"empty": {"apparent": r_empty.apparent_encoding,
                      "encoding": r_empty.encoding, "text": r_empty.text},
            "ascii": {"apparent": r_ascii.apparent_encoding,
                      "text": r_ascii.text},
            "detect_empty": chardet.detect(b""),
            "detect_str": _try(lambda: chardet.detect(u"日本語")),
            "detect_bytearray": _try(lambda: chardet.detect(bytearray(b"ab")))}


@case("requests.parse-list-header")
def _(ctx):
    from requests.utils import parse_list_header
    return [parse_list_header(v) for v in (
        'token, "quoted value", other',
        'a,b , c',
        '"a,b", c',
        r'"escaped \" quote", x',
        '',
        u'日本, "語"')]


@case("requests.parse-dict-header")
def _(ctx):
    from requests.utils import parse_dict_header
    return [parse_dict_header(v) for v in (
        'foo="bar", baz=qux',
        'nokey, a=1',
        'a="1,2", b=3',
        '',
        u'k=日本')]


@case("requests.unquote-header-value")
def _(ctx):
    from requests.utils import unquote_header_value
    return [unquote_header_value(v) for v in
            ('"quoted"', 'plain', '"a\\"b"', '""', '"')] + \
           [unquote_header_value('"C:\\\\dir\\\\file"', is_filename=True),
            unquote_header_value('"\\\\\\\\host\\\\share"', is_filename=True)]


@case("requests.parse-header-links")
def _(ctx):
    from requests.utils import parse_header_links
    return [parse_header_links(v) for v in (
        '<http://x/1>; rel="next", <http://x/9>; rel="last"',
        '<http://x/1>',
        '<http://x/1>; rel=next; title="a, b"',
        '')]


@case("requests.encodings-from-content")
def _(ctx):
    from requests.utils import get_encodings_from_content
    return [get_encodings_from_content(v) for v in (
        '<meta charset="utf-8">',
        '<meta http-equiv="Content-Type" content="text/html; charset=EUC-JP">',
        "<?xml version='1.0' encoding='Shift_JIS'?>",
        "<html><body>no hint</body></html>")]


# -------------------------------------------------------- byte plumbing ----

@case("requests.guess-json-utf")
def _(ctx):
    import codecs
    from requests.utils import guess_json_utf
    samples = [
        b'{"a":1}',
        codecs.BOM_UTF8 + b'{"a":1}',
        codecs.BOM_UTF16_LE + u'{"a":1}'.encode("utf-16-le"),
        codecs.BOM_UTF16_BE + u'{"a":1}'.encode("utf-16-be"),
        codecs.BOM_UTF32_LE + u'{"a":1}'.encode("utf-32-le"),
        codecs.BOM_UTF32_BE + u'{"a":1}'.encode("utf-32-be"),
        u'{"a":1}'.encode("utf-16-le"),
        u'{"a":1}'.encode("utf-32-be"),
        b'', b'{', b'{"',
    ]
    return [[len(s), guess_json_utf(s)] for s in samples]


@case("requests.to-native-string", tags=("adversarial",))
def _(ctx):
    # to_native_string is a pure 2/3 shim: on 3 it decodes bytes with the given
    # encoding and leaves str alone. It is the first thing a migration deletes.
    from requests.utils import to_native_string
    out = []
    for value, enc in ((u"ascii-str", "ascii"), (b"ascii-bytes", "ascii"),
                       (u"日本語", "utf-8"), (b"\xe6\x97\xa5", "utf-8"),
                       (b"\xff", "ascii")):
        try:
            out.append(repr(to_native_string(value, enc)))
        except Exception as e:
            out.append("%s: %s" % (type(e).__name__, e))
    return out


@case("requests.super-len")
def _(ctx):
    from requests.utils import super_len
    buf = io.BytesIO(b"0123456789")
    buf.read(3)
    txt = io.StringIO(u"日本語abc")
    return [super_len(b"bytes"), super_len(u"日本語"), super_len([1, 2, 3]),
            super_len(buf), super_len(txt), super_len({"a": 1})]


@case("requests.key-val-list-roundtrip")
def _(ctx):
    from requests.utils import from_key_val_list, to_key_val_list
    out = []
    for v in ([("a", 1), ("b", 2)], {"a": 1, "b": 2}, None):
        out.append([sorted(from_key_val_list(v).items())
                    if v is not None else None,
                    to_key_val_list(v)])
    for bad in ("string", 42):
        try:
            out.append(to_key_val_list(bad))
        except Exception as e:
            out.append("%s: %s" % (type(e).__name__, e))
    return out


@case("requests.uri-requoting")
def _(ctx):
    from requests.utils import requote_uri, unquote_unreserved
    uris = ["http://x/a b", "http://x/%7Euser", "http://x/%zz",
            "http://x/a%20b%2Fc", u"http://x/日本", "http://x/?q=a+b&r=%2F",
            "http://x/#frag ment"]
    out = []
    for u in uris:
        try:
            out.append([u, requote_uri(u), unquote_unreserved(u)])
        except Exception as e:
            out.append([u, "%s: %s" % (type(e).__name__, e)])
    return out


@case("requests.url-auth-and-scheme")
def _(ctx):
    from requests.utils import (get_auth_from_url, prepend_scheme_if_needed,
                                urldefragauth)
    urls = ["http://u:p@x/path", "http://x/path", u"http://ユ:パ@x/p",
            "http://u%40d:p%3Fw@x/p", "x/path", "//x/path",
            "http://u:p@x/p#frag"]
    return [[u, get_auth_from_url(u), prepend_scheme_if_needed(u, "http"),
             urldefragauth(u)] for u in urls]


@case("requests.network-helpers")
def _(ctx):
    from requests.utils import (address_in_network, dotted_netmask,
                                is_ipv4_address, is_valid_cidr)
    return {
        "is_ipv4": [is_ipv4_address(s) for s in
                    ("127.0.0.1", "256.0.0.1", "localhost", "::1", "1.2.3")],
        "netmask": [dotted_netmask(n) for n in (8, 16, 24, 32)],
        "cidr": [is_valid_cidr(s) for s in
                 ("10.0.0.0/8", "10.0.0.0/33", "10.0.0.0", "junk/8")],
        "in_network": [address_in_network("192.168.1.5", "192.168.1.0/24"),
                       address_in_network("10.0.0.1", "192.168.1.0/24")],
    }


@case("requests.default-headers")
def _(ctx):
    from requests.utils import default_headers, default_user_agent
    h = default_headers()
    return {"ua": default_user_agent(), "ua_named": default_user_agent("x"),
            "headers": sorted((k.lower(), v) for k, v in h.items()),
            "type": type(h).__name__}


@case("requests.case-insensitive-dict")
def _(ctx):
    from requests.structures import CaseInsensitiveDict
    d = CaseInsensitiveDict()
    d["Content-Type"] = "a"
    d["CONTENT-TYPE"] = "b"
    d[u"Ünicode-Key"] = "c"
    return {"get": d["content-type"], "len": len(d),
            "keys": sorted(d.keys()), "lower": sorted(d.lower_items()),
            "eq": d == CaseInsensitiveDict({"content-type": "b",
                                            u"ünicode-key": "c"}),
            "repr_sorted": sorted(dict(d).items()),
            "copy_type": type(d.copy()).__name__}


# ------------------------------------------------------------- models ----

@case("requests.prepare-url")
def _(ctx):
    from requests.models import PreparedRequest
    out = []
    for url, params in (("http://x/p", None),
                        ("http://x/p", {"a": 1, "b": u"日本"}),
                        ("http://x/p?z=1", {"a": 2}),
                        ("http://x/p", [("a", 1), ("a", 2)]),
                        ("http://x/p", "raw=string"),
                        ("http://x/p", b"raw=bytes"),
                        (u"http://日本.example/p", None),
                        ("http://x/p#frag", {"a": 1})):
        p = PreparedRequest()
        try:
            p.prepare_url(url, params)
            out.append(p.url)
        except Exception as e:
            out.append("%s: %s" % (type(e).__name__, e))
    return out


@case("requests.prepare-url-invalid", tags=("adversarial",))
def _(ctx):
    from requests.models import PreparedRequest
    out = []
    for url in ("", "://x", "http://", "/relative", "hiybbprqag://x",
                u"http://[::1]:9200/p"):
        p = PreparedRequest()
        try:
            p.prepare_url(url, None)
            out.append(p.url)
        except Exception as e:
            out.append("%s: %s" % (type(e).__name__, e))
    return out


@case("requests.prepare-body-and-headers")
def _(ctx):
    from requests.models import PreparedRequest
    out = []
    for data, json_body, files in (
            ({"a": u"日本"}, None, None),
            ([("a", 1), ("a", 2)], None, None),
            (u"日本語", None, None),
            (b"\x00\x01", None, None),
            (None, None, None)):
        p = PreparedRequest()
        p.prepare_headers({"X-Custom": u"値"})
        p.prepare_body(data, files, json_body)
        out.append({"body": repr(p.body),
                    "headers": sorted((k.lower(), v)
                                      for k, v in p.headers.items())})
    return out


@case("requests.response-text-and-json", fixture=lambda: {
    "*": Route(body=u'{"t":"日本語"}'.encode("utf-8"),
               headers=[("content-type", "application/json; charset=utf-8")])})
def _(ctx):
    import requests
    r = requests.get(ctx.url("/"))
    return {"encoding": r.encoding, "text": r.text, "json": r.json(),
            "apparent": r.apparent_encoding, "ok": r.ok, "status": r.status_code,
            "content": r.content.decode("utf-8")}


@case("requests.response-no-charset-guesses", tags=("adversarial",),
      fixture=lambda: {"*": Route(body=u'{"t":"日本語"}'.encode("utf-8"),
                                  headers=[("content-type", "text/plain")])})
def _(ctx):
    # No charset on a text/* type: requests falls back to ISO-8859-1 per RFC
    # 2616, so .text mojibakes. That fallback lives next to the cgi call.
    import requests
    r = requests.get(ctx.url("/"))
    return {"encoding": r.encoding, "text": r.text,
            "json_roundtrip": r.json()}


@case("requests.iter-content-and-lines", fixture=lambda: {
    "*": Route(body=u"一\n二\n三".encode("utf-8"),
               headers=[("content-type", "text/plain; charset=utf-8")])})
def _(ctx):
    import requests
    r = requests.get(ctx.url("/"), stream=True)
    chunks = list(r.iter_content(chunk_size=4))
    r2 = requests.get(ctx.url("/"), stream=True)
    lines = list(r2.iter_lines(decode_unicode=True))
    return {"chunks": chunks, "lines": lines}


@case("requests.raise-for-status", tags=("adversarial",), fixture=lambda: {
    "/ok": Route(status=200), "/client": Route(status=404),
    "/server": Route(status=503)})
def _(ctx):
    import requests
    out = []
    for path in ("/ok", "/client", "/server"):
        r = requests.get(ctx.url(path))
        try:
            r.raise_for_status()
            out.append([path, r.status_code, "no-raise", r.reason])
        except Exception as e:
            out.append([path, r.status_code, type(e).__name__, str(e)])
    return out


@case("requests.redirect-chain", fixture=lambda: {
    "/a": Route(status=302, headers=[("location", "/b")]),
    "/b": Route(status=301, headers=[("location", "/c")]),
    "/c": json_route(DOC)})
def _(ctx):
    import requests
    r = requests.get(ctx.url("/a"))
    return {"final": r.url.split(":")[-1].split("/", 1)[-1],
            "history": [h.status_code for h in r.history],
            "json": r.json(), "is_redirect": r.is_redirect,
            "paths": [x["path"] for x in ctx.fx.seen]}


@case("requests.cookies-round-trip", fixture=lambda: {
    "/set": Route(status=200, headers=[
        ("set-cookie", "a=1; Path=/"),
        ("set-cookie", u"b=日本; Path=/".encode("utf-8").decode("latin-1"))]),
    "*": json_route(DOC)})
def _(ctx):
    import requests
    s = requests.session()
    s.get(ctx.url("/set"))
    s.get(ctx.url("/echo"))
    sent = [h for h in ctx.fx.seen[-1]["headers"] if h[0] == "cookie"]
    from requests.utils import dict_from_cookiejar
    return {"jar": sorted(dict_from_cookiejar(s.cookies).items()),
            "sent": sent}


@case("requests.session-headers-merge", fixture=ok)
def _(ctx):
    import requests
    s = requests.session()
    s.headers["Accept"] = "application/json"
    s.headers["X-Session"] = "session"
    # Accept: None is requests' documented way to delete an inherited session
    # header for one call, and it is what this case is really about.
    s.get(ctx.url("/"), headers={"X-Call": "1", "Accept": None})
    out = {"merged": [h for h in ctx.fx.seen[0]["headers"]
                      if h[0].startswith("x-") or h[0] == "accept"]}
    # A non-ASCII header value, on the other hand, cannot leave the process:
    # http.client encodes header values as latin-1 on Python 3 and requests
    # 2.7.0 does not pre-encode them. Pinned, because a migration is free to
    # make this work and must not do so by accident.
    s.headers["X-Session"] = u"セッション"
    out["non_ascii"] = _try(lambda: s.get(ctx.url("/")))
    out["requests_after"] = len(ctx.fx.seen)
    return out


@case("requests.basic-and-digest-auth", fixture=lambda: {
    "/digest": [Route(status=401, headers=[
        ("www-authenticate",
         'Digest realm="r", nonce="deadbeef", qop="auth", opaque="o"')]),
        json_route(DOC)],
    "*": json_route(DOC)})
def _(ctx):
    import requests
    from requests.auth import HTTPBasicAuth, HTTPDigestAuth
    # requests' _basic_auth_str encodes the pair as latin-1, so a non-ASCII
    # credential raises before the request is built. Pinned: it is the same
    # defect urllib3 has one layer down, and both have to stay visible, because
    # a migration that fixes either changes who can log in.
    nonascii = _try(
        lambda: requests.get(ctx.url("/basic"), auth=HTTPBasicAuth(u"ユ", u"パ")))
    requests.get(ctx.url("/basic"), auth=HTTPBasicAuth("u", "p"))
    requests.get(ctx.url("/digest"), auth=HTTPDigestAuth("u", "p"))
    seen = []
    for rec in ctx.fx.seen:
        auth = [v for k, v in rec["headers"] if k == "authorization"]
        # The digest response embeds a random client nonce; only its shape is
        # stable, so the scheme and the fields present are what is compared.
        seen.append([rec["path"], [a.split(" ")[0] for a in auth],
                     sorted(set(p.split("=")[0].strip()
                                for a in auth if a.startswith("Digest")
                                for p in a[7:].split(",")))])
    return {"non_ascii_basic": nonascii, "exchanges": seen,
            "basic_header": sorted(
                v for rec in ctx.fx.seen for k, v in rec["headers"]
                if k == "authorization" and v.startswith("Basic"))}


@case("requests.gzip-transparent-decode", fixture=lambda: {
    "*": gzip_route(DOC)})
def _(ctx):
    import requests
    r = requests.get(ctx.url("/"))
    return {"json": r.json(), "encoding_header": r.headers.get(
        "content-encoding"), "len": len(r.content)}


@case("requests.post-json-body", fixture=ok)
def _(ctx):
    import requests
    requests.post(ctx.url("/x"), json={"t": u"日本語", "n": 1})
    rec = ctx.fx.seen[0]
    return {"body": rec["body"],
            "ctype": [v for k, v in rec["headers"] if k == "content-type"]}


@case("requests.post-files-multipart", fixture=ok)
def _(ctx):
    # The multipart boundary is random, so only the parts' shape is scored.
    import re
    import requests
    requests.post(ctx.url("/x"),
                  files={"f": ("名前.txt", u"中身".encode("utf-8"),
                               "text/plain")},
                  data={"k": u"値"})
    # ``seen`` holds the bytes that reached the socket, not text: how a
    # non-ASCII filename survives the trip is the whole question here, so the
    # decode is done explicitly and its success recorded rather than assumed.
    raw = ctx.fx.seen[0]["body"]
    try:
        body, decoded = raw.decode("utf-8"), "utf-8"
    except UnicodeDecodeError:
        body, decoded = raw.decode("latin-1"), "latin-1"
    return {"dispositions": sorted(re.findall(
        r'Content-Disposition: ([^\r\n]+)', body)),
        "body_decoded_as": decoded,
        "has_payload": u"中身" in body,
        "ctype_prefix": [v.split(";")[0] for k, v in
                         ctx.fx.seen[0]["headers"] if k == "content-type"]}


@case("requests.connection-error", tags=("adversarial",))
def _(ctx):
    import requests
    try:
        requests.get("http://127.0.0.1:1/", timeout=2)
    except Exception as e:
        return {"raised": type(e).__name__,
                "is_connection_error": isinstance(
                    e, requests.exceptions.ConnectionError)}
    return {"raised": None}


@case("requests.timeout", tags=("adversarial",), fixture=lambda: {
    "*": json_route(DOC, delay=1.2)})
def _(ctx):
    import requests
    try:
        requests.get(ctx.url("/"), timeout=0.25)
    except Exception as e:
        return {"raised": type(e).__name__,
                "is_timeout": isinstance(e, requests.exceptions.Timeout)}
    return {"raised": None}


@case("requests.status-codes-lookup")
def _(ctx):
    from requests.status_codes import codes
    return {"ok": codes.ok, "not_found": codes.not_found,
            "teapot": codes.im_a_teapot, "moved": codes.moved_permanently,
            "unavailable": codes.service_unavailable}


@case("requests.exception-hierarchy")
def _(ctx):
    import requests.exceptions as ex
    names = ["RequestException", "ConnectionError", "HTTPError", "Timeout",
             "ConnectTimeout", "ReadTimeout", "TooManyRedirects",
             "MissingSchema", "InvalidURL", "ChunkedEncodingError",
             "ContentDecodingError", "StreamConsumedError"]
    return {n: [c.__name__ for c in getattr(ex, n).__mro__[:5]] for n in names}


@case("requests.proxy-bypass-from-env", tags=("adversarial",))
def _(ctx):
    # should_bypass_proxies reads the environment directly, so the case sets it
    # explicitly rather than inheriting whatever the runner happens to have.
    import os
    from requests.utils import should_bypass_proxies
    saved = {k: os.environ.get(k) for k in ("no_proxy", "NO_PROXY")}
    try:
        os.environ.pop("NO_PROXY", None)
        os.environ["no_proxy"] = "localhost,127.0.0.1,.internal"
        return [[u, should_bypass_proxies(u)] for u in (
            "http://localhost/x", "http://127.0.0.1:9200/x",
            "http://a.internal/x", "http://example.com/x")]
    finally:
        os.environ.pop("no_proxy", None)
        for k, v in saved.items():
            if v is not None:
                os.environ[k] = v
