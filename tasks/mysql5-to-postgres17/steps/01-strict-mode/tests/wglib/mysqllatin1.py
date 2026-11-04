"""MySQL's `latin1` character set, which is not ISO-8859-1.

MySQL's latin1 is CP1252 (Windows-1252), except that the five byte values
CP1252 leaves undefined are mapped to the C1 control characters at the same
code point:

    0x81 -> U+0081   0x8D -> U+008D   0x8F -> U+008F   0x90 -> U+0090   0x9D -> U+009D

27 of the 256 byte values therefore decode differently from ISO-8859-1, and 5
of them are values that Python's `cp1252` codec and `iconv -f CP1252` both
reject outright. Decoding a MySQL latin1 column with either of those shortcuts
is wrong; this module is the correct decoder.

The table is built from Python's own cp1252 codec and then patched, so there is
no hand-transcribed mapping to get wrong. `check_against_server()` proves the
result equals what the running MySQL server does, one byte at a time.
"""

# The five byte values CP1252 leaves undefined. MySQL maps each to the C1
# control character at the same code point.
_UNDEFINED_IN_CP1252 = (0x81, 0x8D, 0x8F, 0x90, 0x9D)


def _build_table():
    table = []
    for b in range(256):
        if b in _UNDEFINED_IN_CP1252:
            table.append(chr(b))
        else:
            table.append(bytes([b]).decode("cp1252"))
    return table


_TABLE = _build_table()
_REVERSE = {ch: i for i, ch in enumerate(_TABLE)}


def decode(raw):
    """MySQL latin1 bytes -> str. Total: every one of the 256 bytes maps."""
    if raw is None:
        return None
    if isinstance(raw, str):
        raw = raw.encode("latin-1", "surrogateescape")
    return u"".join(_TABLE[b] for b in raw)


def encode(text):
    """str -> MySQL latin1 bytes. Raises on characters the charset cannot hold."""
    out = bytearray()
    for ch in text:
        if ch not in _REVERSE:
            raise ValueError("U+%04X is not representable in MySQL latin1" % ord(ch))
        out.append(_REVERSE[ch])
    return bytes(out)


def check_against_server(query_one):
    """Prove this table equals the running server's own conversion.

    `query_one(sql)` must run `sql` and return the first column of the first
    row as bytes or str. Returns the list of disagreeing byte values, so an
    empty list means the table is exactly right.
    """
    bad = []
    for b in range(256):
        got = query_one("SELECT CONVERT(_latin1 0x%02x USING utf8mb4)" % b)
        if isinstance(got, (bytes, bytearray)):
            got = bytes(got).decode("utf-8")
        if got != _TABLE[b]:
            bad.append(b)
    return bad


# ISO-8859-1 and CP1252 shortcuts, kept only so tests can demonstrate that they
# are wrong rather than asserting it in prose.
def differs_from_iso8859_1():
    return [b for b in range(256) if _TABLE[b] != bytes([b]).decode("latin-1")]


def rejected_by_strict_cp1252():
    return sorted(_UNDEFINED_IN_CP1252)
