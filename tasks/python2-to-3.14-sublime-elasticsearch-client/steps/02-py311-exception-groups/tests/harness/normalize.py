# -*- coding: utf-8 -*-
"""Volatile-field normalisation, applied to the whole observation.

One module, used by the recorder and the replayer alike, so a change here
cannot desynchronise the two ends (rule 5).

Everything listed below varies between two runs of the *same* tree on the
*same* interpreter, or between two interpreters running the same tree, for
reasons that have nothing to do with the migration. Everything not listed is
compared verbatim, including ``python-requests/2.7.0`` and the other vendored
version strings: those are part of the observable behaviour the migration must
preserve.
"""
import re

_SUBS = [
    # --- the tree under test, first --------------------------------------
    # Paths into the tree differ between the recorder host, /app, and whatever
    # scratch directory a verifier unpacks a pristine copy into. requests
    # resolves DEFAULT_CA_BUNDLE_PATH to an absolute path inside the tree and
    # hands it to the adapter, so this reaches two recorded observations.
    #
    # **This rule must stay ahead of the /tmp rule below.** With the two the
    # other way round, a tree that happens to live under /tmp -- a verifier
    # scoring from a pristine copy, which is exactly what rule 6 asks for --
    # had its prefix eaten by the scratch-directory rule first, and the tree
    # rule then matched only the remainder, yielding
    # ``/tmp/<TMP><TREE>/lib/requests/cacert.pem`` against a recorded
    # ``<TREE>/lib/requests/cacert.pem``. Two cases failed for every candidate
    # at every step, including the oracle, for a reason no instruction
    # mentioned. Found by the mutation test of §4.6 -- ten mutants that could
    # not possibly touch requests all reported the same two failures, and the
    # control, which replayed the tree from its recorded location, could not
    # have shown it. Fixtured in :func:`selftest`.
    (re.compile(r"/[A-Za-z0-9_./-]*?/lib/(?=(?:six|requests|urllib3|"
                r"elasticsearch|dateutil|analytics))"), "<TREE>/lib/"),

    # --- run-to-run -------------------------------------------------------
    # CPython object addresses.
    (re.compile(r"0x[0-9a-fA-F]{6,16}"), "0xADDR"),
    # Sockets named in ResourceWarning text.
    (re.compile(r"fd=\d+"), "fd=<FD>"),
    (re.compile(r"laddr=\('127\.0\.0\.1', \d+\)"), "laddr=('127.0.0.1', <PORT>)"),
    (re.compile(r"raddr=\('127\.0\.0\.1', \d+\)"), "raddr=('127.0.0.1', <PORT>)"),
    # Scratch directories.
    (re.compile(r"/tmp/[A-Za-z0-9_.-]{6,}"), "/tmp/<TMP>"),
    # A bare 32-hex run: requests builds its multipart boundary from
    # uuid4().hex, which reaches the wire in both the body and the
    # Content-Type header. No fixture body contains a run of that shape.
    # HTTP Digest client nonce and its response digest. requests seeds the
    # cnonce from time.ctime() and os.urandom(8), so both it and every digest
    # computed over it are random per run. These come before the generic
    # 32-hex rule so the more specific name survives in the output. The nonce,
    # realm, uri, qop, nc and algorithm parameters around them are *not*
    # normalised: those are the part of the header the migration must keep
    # producing.
    (re.compile(r'cnonce="[0-9a-f]{16}"'), 'cnonce="<CNONCE>"'),
    (re.compile(r'response="[0-9a-f]{32,40}"'), 'response="<DIGEST>"'),
    (re.compile(r"(?<![0-9a-fA-F])[0-9a-f]{32}(?![0-9a-fA-F])"), "<HEX32>"),

    # --- wall clock -------------------------------------------------------
    # RFC 1123 (HTTP Date headers, cookie expiry).
    (re.compile(r"\b(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun), \d{2} "
                r"(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) "
                r"\d{4} \d{2}:\d{2}:\d{2} GMT"), "<HTTPDATE>"),
    # ISO 8601 as emitted by analytics and by the ES client.
    (re.compile(r"\b\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d+)?"
                r"(?:Z|[+-]\d{2}:?\d{2})"), "<ISOTIME>"),
    # Elapsed-time renderings. elasticsearch's own logger formats every call
    # as "[status:200 request:0.009s]" and its tracer as "(0.009s)"; urllib3
    # logs retry sleeps at six decimals. No fixture body contains this shape,
    # so matching it anywhere is safe and catches all three.
    (re.compile(r"(?<![\d.])\d+\.\d{3}s(?![\d])"), "<DUR>s"),
    (re.compile(r"(?<![\d.])\d+\.\d{6}s(?![\d])"), "<DUR>s"),

    # --- interpreter / platform ------------------------------------------
    # requests' default User-Agent embeds the running interpreter and kernel.
    (re.compile(r"CPython/\d+\.\d+\.\d+[^\s\"']*"), "CPython/<PYVER>"),
    (re.compile(r"Linux/[0-9][^\s\"']*"), "Linux/<KERNEL>"),
    # CPython 3.10 began spelling the *qualified* name of the callable in
    # argument-binding errors: 3.8 raises "send() takes 1 positional argument
    # but 2 were given", 3.10 raises "BaseAdapter.send() takes 1 positional
    # argument but 2 were given". Four cases record such a message, and four
    # things follow that together make this a normalisation rather than a flip:
    #
    #  * it is not behaviour of the tree. The prefix comes out of ceval, not
    #    out of anything under lib/;
    #  * no edit an agent can make restores the 3.8 spelling, so an expectation
    #    that embeds it is an *unpassable* inherited test from rung 01 up
    #    (INSTRUCTIONS trap 9) -- it cost the rung-01 oracle 4/485 before this
    #    rule existed;
    #  * flipping it instead would file those four cases under rung 01's own
    #    bar (family 2), which is meant to hold what the rung is *about*; and
    #  * the wording keeps moving above 3.10, so the same four cases would need
    #    flipping again at several of the rungs above.
    #
    # The callable's own name survives -- search() still has to be the thing
    # that rejected the keyword, and a migration that lost the method entirely
    # still fails. Only the prefix CPython prepended is dropped. Anchored on
    # the four verbs CPython uses for argument binding so that a dotted call
    # named anywhere else in a message is left alone.
    (re.compile(r"(?<![\w.])(?:[\w<>]+\.)+([\w<>]+\(\))"
                r"(?= (?:takes|missing|got|accepts)\b)"), r"\1"),

    # CPython 3.14 rewrote three families of interpreter-authored message that
    # a recorded observation can hold. None of the three is behaviour of the
    # tree, all three are unpassable by construction for every candidate at
    # rung 07 (INSTRUCTIONS trap 9), and the list is closed by measurement: the
    # 542 recorded cases replayed on 3.14 against an untouched rung-06 tree
    # failed on exactly five, and these rules are what those five differ by.
    #
    # Where the older spelling can be recovered from the newer one the rule
    # produces it, and the recording does not move. Where it cannot -- 3.14
    # having dropped a distinction rather than added a word -- the whole
    # *family* goes to a marker, including the members whose old spelling was
    # recoverable, so that one family is read one way at every rung.

    # Family 1: ZeroDivisionError out of the arithmetic operators. 3.14 spells
    # every one of them "division by zero"; 3.8 and 3.13 name the operand type
    # and the operator, and disagree with each other about two of them ("float
    # modulo" gained "by zero", "float divmod()" became "float floor division
    # by zero"). Nothing recovers the word "float" from 3.14's text, so that
    # distinction cannot be part of an expectation that spans the ladder. One
    # case records one of these, from dividing a relativedelta by zero, and
    # what it is there to observe is that the division raises at all.
    (re.compile(r"\A(?:division by zero"
                r"|integer division or modulo by zero"
                r"|integer modulo by zero"
                r"|integer division by zero"
                r"|float division by zero"
                r"|float floor division by zero"
                r"|float divmod\(\)"
                r"|float modulo(?: by zero)?"
                r"|complex division by zero)\Z"), "<ZERODIV>"),
    (re.compile(r"\A(?:0\.0 cannot be raised to a negative power"
                r"|zero to a negative power)\Z"), "<ZERONEGPOW>"),

    # Family 2: the range checks in the datetime, date and time constructors.
    # 3.14 appends the offending value ("month must be in 1..12, not 13") and
    # rebuilt two of the messages outright: "day is out of range for month"
    # became "day 30 must be in range 1..28 for month 2 in year 2015", and
    # "year 0 is out of range" became "year must be in 1..9999, not 0". Which
    # field was out of range is the part that does not depend on the
    # interpreter, so it is the part the marker keeps. One case records four of
    # these, from handing the parser strings no calendar can hold.
    (re.compile(r"\A(month|hour|minute|second|microsecond) must be in "
                r"\d+\.\.\d+(?:, not -?\d+)?\Z"), r"<RANGE:\1>"),
    (re.compile(r"\Ayear (?:-?\d+ is out of range"
                r"|must be in \d+\.\.\d+, not -?\d+)\Z"), "<RANGE:year>"),
    (re.compile(r"\Aday (?:is out of range for month"
                r"|-?\d+ must be in range \d+\.\.\d+ for month \d+ "
                r"in year -?\d+)\Z"), "<RANGE:day>"),
    (re.compile(r"\Afold must be either 0 or 1(?:, not -?\d+)?\Z"),
     "<RANGE:fold>"),

    # Family 3: the TypeError ``in`` raises for a right-hand side that is
    # neither a container nor an iterable. 3.14 says "is not a container or
    # iterable" where every earlier version says "is not iterable". The class
    # name in the message is behaviour -- two cases record it for
    # ProxyManager, and a migration that renamed or lost that class has to
    # fail -- so this family is rewritten to the older spelling rather than
    # marked, and the recording does not move.
    (re.compile(r"\Aargument of type '([^']*)' is not a container "
                r"or iterable\Z"), r"argument of type '\1' is not iterable"),

    # Family 4: the C type named in an OverflowError out of the datetime
    # constructor. 3.13 narrowed a conversion in datetimemodule.c from long to
    # int, so a year too large to hold reports "Python int too large to
    # convert to C int" where 3.8 through 3.12 said "C long". Which C type the
    # interpreter overflowed is not behaviour of the tree; that the year is
    # refused, with an OverflowError, is, and that survives. Rewritten to the
    # older spelling rather than marked because the sentence carries nothing
    # else. The safety argument is a census rather than the pattern: exactly
    # one recorded observation holds this sentence, from the parser refusing a
    # five-digit year, and the rule is anchored whole so that a message which
    # merely quotes it is left alone.
    (re.compile(r"\APython int too large to convert to C int\Z"),
     "Python int too large to convert to C long"),

    # Family 5: the AttributeError from setting an unknown attribute on a
    # class with __slots__. 3.13 appended " and no __dict__ for setting new
    # attributes"; 3.8 through 3.12, and the *reading* form on every version,
    # stop at the attribute name. The class name and the attribute name are
    # what a migration can break and both come before the suffix, so the older
    # spelling is recoverable and the recording does not move. One case
    # records such a message, for dateutil's _ttinfo.
    (re.compile(r"\A'([^']*)' object has no attribute '([^']*)'"
                r" and no __dict__ for setting new attributes\Z"),
     r"'\1' object has no attribute '\2'"),
]

# analytics' messageId and anonymousId are str(uuid4()); the ES client's
# request ids and the cookie jar's session ids are the same shape.
_UUID = re.compile(r"\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}"
                   r"-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\b")

# Deliberately no key-driven blanking: a rule keyed on the *name* "date" would
# also erase a JSON body field called "date", which is behaviour the fixtures
# exist to compare. Everything volatile is matched on its shape instead.


def port_subs(port):
    """Substitutions for the one ephemeral port the OS chose this run.

    Matching the *actual* port rather than "any 2-5 digit number after
    ``port``" is deliberate: a generic rule also erased the meaningful ports in
    ``Elasticsearch('https://…:9243/prefix')``'s normalised host dict, which is
    behaviour a migration can break. Anchoring on the real value normalises
    exactly what varies and nothing else.
    """
    if not port:
        return []
    p = str(int(port))
    return [
        (re.compile(r"127\.0\.0\.1:" + p + r"(?!\d)"), "127.0.0.1:<PORT>"),
        (re.compile(r"localhost:" + p + r"(?!\d)"), "localhost:<PORT>"),
        (re.compile(r"(?<![\d])" + p + r"(?!\d)(?=\s*[,)\]}])"), "<PORT>"),
        (re.compile(r"\bport=" + p + r"(?!\d)"), "port=<PORT>"),
    ]


def text(value, subs=()):
    out = _UUID.sub("<UUID>", value)
    for pattern, repl in tuple(subs) + tuple(_SUBS):
        out = pattern.sub(repl, out)
    return out


#: ``(what, raw, port, expected)``. Every row is a string this module has
#: actually been handed by a recorded observation, or the failure mode of one.
FIXTURES = (
    # The bug that motivated the ordering above: the same tree, seen from the
    # three places it legitimately lives, must normalise to the same string.
    ("tree at the recorder's own path",
     "/work/agents/m03/work/baseline/lib/requests/cacert.pem", None,
     "<TREE>/lib/requests/cacert.pem"),
    ("tree at /app, where the verifier mounts it",
     "/app/lib/requests/cacert.pem", None,
     "<TREE>/lib/requests/cacert.pem"),
    ("tree in a scratch copy, which is where a pristine-copy scorer puts it",
     "/tmp/mutdbg-nq0u__rw/m10-x/lib/requests/cacert.pem", None,
     "<TREE>/lib/requests/cacert.pem"),
    ("a scratch path that is not a tree still normalises as scratch",
     "/tmp/fixture-abc123/wire.log", None, "/tmp/<TMP>/wire.log"),
    # The rules the tree rule now runs ahead of must still fire.
    ("object address", "<HTTPConnection object at 0x7f3a9c1d2e40>", None,
     "<HTTPConnection object at 0xADDR>"),
    ("multipart boundary", "boundary=0123456789abcdef0123456789abcdef", None,
     "boundary=<HEX32>"),
    ("digest cnonce and response",
     'cnonce="0123456789abcdef", response="0123456789abcdef0123456789abcdef"',
     None, 'cnonce="<CNONCE>", response="<DIGEST>"'),
    ("http date", "Expires=Wed, 21 Oct 2015 07:28:00 GMT", None,
     "Expires=<HTTPDATE>"),
    ("iso time", "2015-10-21T07:28:00.123456+00:00", None, "<ISOTIME>"),
    ("client log duration", "[status:200 request:0.009s]", None,
     "[status:200 request:<DUR>s]"),
    ("user agent", "python-requests/2.7.0 CPython/3.8.20 Linux/6.1.0-53-cloud",
     None, "python-requests/2.7.0 CPython/<PYVER> Linux/<KERNEL>"),
    ("uuid", "messageId: 1b4e28ba-2fa1-11d2-883f-0016d3cca427", None,
     "messageId: <UUID>"),
    # port_subs anchors on the port the OS actually chose, so a *different*
    # port in the same string -- the one in a configured host -- survives.
    ("the ephemeral port goes, a configured one stays",
     "https://127.0.0.1:41234/ via https://es.example:9243/prefix", 41234,
     "https://127.0.0.1:<PORT>/ via https://es.example:9243/prefix"),

    # The qualified-callable rule, from both ends: the two spellings of the
    # same error have to meet in the middle, or the rule normalises one rung's
    # answer into something no other rung produces.
    ("3.10+ argument-binding error, with the class CPython prepends",
     "BaseAdapter.send() takes 1 positional argument but 2 were given", None,
     "send() takes 1 positional argument but 2 were given"),
    ("3.8 spelling of the same error is already the normal form",
     "send() takes 1 positional argument but 2 were given", None,
     "send() takes 1 positional argument but 2 were given"),
    ("keyword and missing-argument wordings, and a dunder callable",
     "TypeError: rrule.__init__() missing 1 required positional "
     "argument: 'freq'", None,
     "TypeError: __init__() missing 1 required positional argument: 'freq'"),
    ("a nested function's qualname keeps only its own name",
     "Session.request.<locals>.hook() got an unexpected keyword "
     "argument 'timeout'", None,
     "hook() got an unexpected keyword argument 'timeout'"),
    # The negative half. A dotted call in a message about anything other than
    # argument binding is behaviour, not wording, and has to survive.
    ("a dotted call not followed by an argument-binding verb stays whole",
     "urllib3.util.parse_url() returned None for 'http://%zz'", None,
     "urllib3.util.parse_url() returned None for 'http://%zz'"),
    ("the module prefix of a name that is not a call stays whole",
     "module 'dateutil.rrule' has no attribute 'gcd'", None,
     "module 'dateutil.rrule' has no attribute 'gcd'"),

    # The three 3.14 message families, from every spelling that has reached a
    # recorded observation, and the negatives that have to survive them.
    ("3.14 collapses every arithmetic ZeroDivisionError into one string",
     "division by zero", None, "<ZERODIV>"),
    ("3.8 and 3.13 name the operand type, and must meet 3.14 in the middle",
     "float division by zero", None, "<ZERODIV>"),
    ("integer floor division and modulo, 3.8 spelling",
     "integer division or modulo by zero", None, "<ZERODIV>"),
    ("integer modulo, 3.13 spelling", "integer modulo by zero", None,
     "<ZERODIV>"),
    ("float modulo, the 3.8 spelling and the 3.13 one",
     "float modulo", None, "<ZERODIV>"),
    ("float floor division, 3.13 spelling", "float floor division by zero",
     None, "<ZERODIV>"),
    ("float divmod, 3.8 spelling", "float divmod()", None, "<ZERODIV>"),
    ("negative power of zero, the spelling up to 3.13",
     "0.0 cannot be raised to a negative power", None, "<ZERONEGPOW>"),
    ("negative power of zero, 3.14 spelling", "zero to a negative power",
     None, "<ZERONEGPOW>"),
    ("a ZeroDivisionError the library itself raises is not the "
     "interpreter's", "Fraction(1, 0)", None, "Fraction(1, 0)"),
    ("a sentence that merely mentions dividing by zero is left alone",
     "giving up after division by zero", None,
     "giving up after division by zero"),

    ("datetime range check, the spelling up to 3.13",
     "month must be in 1..12", None, "<RANGE:month>"),
    ("datetime range check, 3.14 spelling with the value echoed",
     "month must be in 1..12, not 13", None, "<RANGE:month>"),
    ("the field is kept, so two fields do not normalise onto each other",
     "minute must be in 0..59, not 61", None, "<RANGE:minute>"),
    ("day, the spelling up to 3.13", "day is out of range for month", None,
     "<RANGE:day>"),
    ("day, 3.14 spelling, which is a different sentence entirely",
     "day 30 must be in range 1..28 for month 2 in year 2015", None,
     "<RANGE:day>"),
    ("year, the spelling up to 3.13", "year 0 is out of range", None,
     "<RANGE:year>"),
    ("year, 3.14 spelling", "year must be in 1..9999, not 10000", None,
     "<RANGE:year>"),
    ("fold, 3.14 spelling", "fold must be either 0 or 1, not 2", None,
     "<RANGE:fold>"),
    ("a message that only begins like one of them survives whole",
     "month must be in 1..12 according to the mapping", None,
     "month must be in 1..12 according to the mapping"),

    ("the `in` TypeError keeps its class name, 3.14 spelling",
     "argument of type 'ProxyManager' is not a container or iterable", None,
     "argument of type 'ProxyManager' is not iterable"),
    ("the same error up to 3.13 is already the normal form",
     "argument of type 'ProxyManager' is not iterable", None,
     "argument of type 'ProxyManager' is not iterable"),

    ("the C type in a datetime OverflowError, 3.13 spelling",
     "Python int too large to convert to C int", None,
     "Python int too large to convert to C long"),
    ("the same overflow up to 3.12 is already the normal form",
     "Python int too large to convert to C long", None,
     "Python int too large to convert to C long"),
    ("setting an attribute on a __slots__ class, 3.13 spelling",
     "'_ttinfo' object has no attribute 'nope' and no __dict__ for "
     "setting new attributes", None,
     "'_ttinfo' object has no attribute 'nope'"),
    ("reading a missing attribute says the same thing on every version",
     "'_ttinfo' object has no attribute 'nope'", None,
     "'_ttinfo' object has no attribute 'nope'"),
)


def selftest(stream=None):
    import sys as _sys
    stream = stream or _sys.stdout
    bad = []
    for what, raw, port, want in FIXTURES:
        got = text(raw, port_subs(port))
        if got != want:
            bad.append("%s\n    in   %r\n    want %r\n    got  %r"
                       % (what, raw, want, got))
    if bad:
        print("NORMALIZE SELFTEST FAILED", file=stream)
        for b in bad:
            print("  " + b, file=stream)
        return 1
    print("normalize selftest: %d rows; the tree rule runs ahead of the "
          "scratch rule, so one tree at three paths gives one string"
          % len(FIXTURES), file=stream)
    return 0


def walk(node, port=None, _subs=None):
    """Normalise a canonicalised observation (returns a copy)."""
    if _subs is None:
        _subs = port_subs(port)
    if isinstance(node, str):
        return text(node, _subs)
    # Tuples are walked as well as lists. canon() turns a tuple into
    # {"__tuple__": [...]} so it never reaches here, but the fixture server's
    # own wire log builds header pairs with a generator expression and those
    # stay tuples all the way to json.dumps, which renders them as arrays.
    # Skipping them left every request header -- including the User-Agent that
    # carries "CPython/<version>" -- unnormalised.
    if isinstance(node, (list, tuple)):
        return [walk(x, _subs=_subs) for x in node]
    if isinstance(node, dict):
        return {walk(k, _subs=_subs): walk(v, _subs=_subs)
                for k, v in node.items()}
    return node


if __name__ == "__main__":
    import sys
    sys.exit(selftest())
