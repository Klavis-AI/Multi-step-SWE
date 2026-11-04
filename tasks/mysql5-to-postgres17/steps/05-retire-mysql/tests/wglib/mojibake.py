"""Detecting -- and undoing -- a latin1/UTF-8 round trip that went wrong.

Mojibake here is one specific accident: UTF-8 bytes that were read as if they
were single-byte characters, and then stored as the characters that reading
produced. `Café` becomes `CafÃ©`. Undoing it is the inverse: encode the string
back to the single bytes it was misread from, and decode those as UTF-8.

The question that decides whether a detector works is *which* single-byte
codec. It is not a free choice, and getting it wrong silently misses real
damage -- this module was written after a cp1252-based prototype passed over
four genuinely broken values in the corpus because it could not encode them.

Two codecs are tried, in this order, and between them they cover both of the
misreadings this migration can produce:

  mysql-latin1  What MySQL means by `latin1`: CP1252, with the five bytes
                CP1252 leaves undefined (0x81 0x8D 0x8F 0x90 0x9D) mapped to
                the C1 controls at the same code point. This is the exact
                inverse of the decode a `latin1` column performs, so it is
                the right first guess for anything that came out of MySQL.
  latin-1       ISO-8859-1 proper, where 0x80-0x9F are all C1 controls. This
                is what a pipeline that went through `iconv -f ISO-8859-1`,
                PHP's `utf8_decode`, or a client that negotiated `latin1` by
                its real name will have produced.

Neither subsumes the other. `Ã\x85sa` (from `Åsa`) needs U+0085, which
mysql-latin1 maps from 0x85 to U+2026 and so cannot encode; the Greek,
Japanese and Russian values need U+0152, which ISO-8859-1 has no byte for.
CP1252 itself is not in the ladder: mysql-latin1 is CP1252 plus five bytes,
so anything CP1252 can encode, it can encode too.

Two properties keep the false-positive rate down without any heuristic about
what text "looks like":

  * the re-encoded bytes must be *valid* UTF-8. Real text in these scripts
    almost never is -- `São Paulo – SP` re-encodes to a 0xE3 with no
    continuation byte, and is left alone.
  * the result must differ from the input. A pure-ASCII string re-encodes and
    decodes to itself, so it can never be flagged.

`repair()` iterates, because the corpus contains values that were mangled
twice (`ÃƒÂ¸` is `ø` through the accident applied to its own output). It stops
when nothing more changes, so a repaired value is a fixed point.

Nothing here decides whether a *step* wants values repaired. That is the
encoding gate's mode, and the expectation it scores against is recorded from a
golden tree, not computed by this module at scoring time.
"""

import unicodedata

from . import mysqllatin1

# In ladder order. See the module docstring for why these two and only these.
CODECS = ("mysql-latin1", "latin-1")

# Repairing is idempotent in practice after two rounds; the cap is a guard
# against a pathological value oscillating, not a tuning knob.
MAX_ROUNDS = 6


def _encode(text, codec):
    """The single-byte bytes `text` would have been misread from, or None."""
    if codec == "mysql-latin1":
        try:
            return mysqllatin1.encode(text)
        except ValueError:
            return None
    try:
        return text.encode(codec)
    except UnicodeEncodeError:
        return None


def undo(text):
    """One round: (repaired, codec) or (None, None) if nothing applies."""
    for codec in CODECS:
        raw = _encode(text, codec)
        if raw is None:
            continue
        try:
            out = raw.decode("utf-8")
        except UnicodeDecodeError:
            continue
        if out != text:
            return out, codec
    return None, None


def repair(text, max_rounds=MAX_ROUNDS):
    """(fixed point, [codec per round]). An empty list means untouched."""
    current, used = text, []
    for _ in range(max_rounds):
        nxt, codec = undo(current)
        if nxt is None:
            break
        current = nxt
        used.append(codec)
    return current, used


def is_mojibake(text):
    return bool(undo(text)[0])


def is_nfc(text):
    return unicodedata.normalize("NFC", text) == text


def nfc(text):
    return unicodedata.normalize("NFC", text)


def survey(values):
    """Counts over an iterable of strings: what a column looks like.

    The three numbers the encoding gate compares against its recording.
    """
    total = mojibake = not_normalised = 0
    for v in values:
        total += 1
        if is_mojibake(v):
            mojibake += 1
        if not is_nfc(v):
            not_normalised += 1
    return {"values": total, "mojibake": mojibake, "not_nfc": not_normalised}


# ---------------------------------------------------------------- selftest --
# A known-answer control, built by construction rather than by observation: the
# broken side is produced by *applying* the accident to text stated here, so
# the expected repair is known without asking the detector. The clean side is
# text that must survive untouched, including the three shapes that have
# fooled a detector before -- a lone CP1252 punctuation character, a truncated
# UTF-8 sequence, and raw bytes from the undefined CP1252 range.

_CLEAN = (
    "Herbstpokal München",          # genuine CP1252-representable text
    "Premio – libro",               # U+2013, i.e. CP1252 0x96
    "©dmin",                        # a deliberate lone copyright sign
    "Truncated CafÃ",               # half a UTF-8 sequence: unfixable
    "Prize",           # bytes CP1252 leaves undefined
    "日本語",                # already-correct UTF-8
    "ﬁligree",                      # U+FB01, in no single-byte codec
    "plain ascii",
    "Ιστορία",   # Greek, already correct
)

# (original text, misreading codec, rounds) -- the broken form is generated.
_BROKEN = (
    ("Café", "mysql-latin1", 1),
    ("Åsa", "latin-1", 1),                     # needs U+0085: latin-1 only
    ("ø", "mysql-latin1", 2),                  # doubly mangled
    ("Θεοδώρα", "mysql-latin1", 1),
    ("日本語", "mysql-latin1", 1),
    ("Сообщение", "mysql-latin1", 1),
    ("Jürgen Schäfer", "mysql-latin1", 2),
)


def _mangle(text, codec, rounds):
    """Apply the accident: UTF-8 bytes read through a single-byte codec."""
    for _ in range(rounds):
        raw = text.encode("utf-8")
        if codec == "mysql-latin1":
            text = mysqllatin1.decode(raw)
        else:
            text = raw.decode(codec)
    return text


def selftest():
    """(ok, [problem, ...]). Run before the instrument is believed."""
    problems = []
    for text in _CLEAN:
        fixed, used = repair(text)
        if used:
            problems.append("clean %r was 'repaired' to %r via %s"
                            % (text, fixed, "+".join(used)))
    for text, codec, rounds in _BROKEN:
        broken = _mangle(text, codec, rounds)
        if broken == text:
            problems.append("selftest bug: mangling %r via %s changed nothing"
                            % (text, codec))
            continue
        fixed, used = repair(broken)
        if fixed != text:
            problems.append("broken %r should repair to %r, got %r via %s"
                            % (broken, text, fixed, "+".join(used) or "none"))
        elif len(used) != rounds:
            problems.append("broken %r took %d rounds, expected %d"
                            % (broken, len(used), rounds))
    # Repair must be a fixed point: applying it twice is applying it once.
    for text, codec, rounds in _BROKEN:
        once, _ = repair(_mangle(text, codec, rounds))
        twice, again = repair(once)
        if again:
            problems.append("repair of %r is not a fixed point: %r -> %r"
                            % (text, once, twice))
    return (not problems), problems
