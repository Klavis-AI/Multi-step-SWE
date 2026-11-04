#!/usr/bin/env python3
"""Generate the transcode corpus a simdutf milestone is measured on.

    make_corpus.py <outdir> [seed]

Every file is a deterministic function of the seed.  The verifier draws its own
seed and regenerates the corpus inside its image; the workspace ships a sample
drawn from the same family with a different seed, so the shape of the input is
public and which bytes are scored is not.

The distribution -- not the draw -- is what the agent is told:

    utf8      mixed-script UTF-8: ASCII runs, 2-byte Latin/Greek/Cyrillic,
              3-byte CJK, 4-byte emoji and astral planes, at a stated mix
    utf8err   the same, with invalid sequences at a stated rate
    utf16le/be, utf32le   the same text in the other encodings
    utf16bad  well-formed units carrying lone surrogates
    ascii     pure ASCII
    latin1    every byte value 0x00..0xff
    base64    RFC 4648 base64 with line breaks
    bin       uniformly random bytes
"""

import base64
import os
import random
import sys

# Per-category multiplicities.  A run of `repeat` identical code points is
# what makes the corpus look like real text to a SIMD kernel: long stretches
# of one width, then a switch.
ASCII = "the quick brown fox jumps over the lazy dog 0123456789 "
LATIN = "àéîõüÀÉÎÕÜñçßøåæ"                      # 2-byte UTF-8, all <= U+00FF
GREEK = "αβγδεζηθικλμνξοπρστυφχψω"                # 2-byte UTF-8
CYRIL = "абвгдежзийклмнопрстуфхцчшщыэюя"          # 2-byte UTF-8
CJK = "日本語中文漢字測試東京都大阪府"               # 3-byte UTF-8
EMOJI = "😀🎉🚀🌍🔥💡📦🎯🧩🛰️"                     # 4-byte UTF-8 (last is 2 cp)
ASTRAL = "\U0001D400\U0001D401\U0001D402\U0001F600\U0001F601"  # astral


def block(rng):
    """A run of one script.  ASCII dominates by byte share, as it does in the
    documents a transcoding pass actually sees; the non-ASCII runs are long
    enough to exercise the wide paths rather than to be averaged away."""
    kind = rng.random()
    if kind < 0.70:
        pool, rep = ASCII, rng.randint(120, 400)
    elif kind < 0.82:
        pool, rep = rng.choice([LATIN, GREEK, CYRIL]), rng.randint(8, 24)
    elif kind < 0.94:
        pool, rep = CJK, rng.randint(8, 24)
    else:
        pool, rep = rng.choice([EMOJI, ASTRAL]), rng.randint(4, 12)
    s = ""
    for _ in range(rep):
        s += rng.choice(pool)
    return s


def make_text(rng, target):
    out = []
    total = 0
    while total < target:
        b = block(rng)
        out.append(b)
        total += len(b.encode("utf-8"))
        if rng.random() < 0.3:
            out.append(rng.choice(" \n\t ,.;:!?—"))
    return "".join(out)


def inject_errors(rng, text, rate):
    """Break the text at a stated rate with the standard UTF-8 error shapes."""
    data = bytearray(text.encode("utf-8"))
    defects = [
        b"\x80",                    # stray continuation
        b"\xc0\x80",                # overlong NUL
        b"\xe0\x80\x80",            # overlong 3-byte
        b"\xf5\x80\x80\x80",        # > U+10FFFF
        b"\xed\xa0\x80",            # CESU-8 surrogate
        b"\xc2",                    # truncated 2-byte
        b"\xe2\x82",                # truncated 3-byte
        b"\xf0\x9f\x98",            # truncated 4-byte
    ]
    n = int(len(data) * rate / 16) + 1
    for _ in range(n):
        pos = rng.randrange(len(data))
        data[pos:pos] = rng.choice(defects)
    return bytes(data)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    out = sys.argv[1]
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260923
    rng = random.Random(seed)
    os.makedirs(out, exist_ok=True)
    target = 256 * 1024

    text = make_text(rng, target)
    utf8 = text.encode("utf-8")

    j = lambda name: os.path.join(out, name)
    with open(j("utf8.txt"), "wb") as f:
        f.write(utf8)
    with open(j("utf8err.txt"), "wb") as f:
        f.write(inject_errors(rng, text, 0.02))

    # The other encodings carry the same text, so a ratio measured on one is
    # a ratio measured on the same content.
    with open(j("utf16le.bin"), "wb") as f:
        f.write(text.encode("utf-16-le"))
    with open(j("utf16be.bin"), "wb") as f:
        f.write(text.encode("utf-16-be"))
    with open(j("utf32le.bin"), "wb") as f:
        f.write(text.encode("utf-32-le"))

    # Well-formed UTF-16 carrying lone surrogates: high halves are never
    # closed and low halves never opened, which is what utf16fix repairs.
    bad = []
    for _ in range(12000):
        r = rng.random()
        if r < 0.4:
            bad.append(rng.randrange(0xD800, 0xDC00))
        elif r < 0.8:
            bad.append(rng.randrange(0xDC00, 0xE000))
        else:
            bad.append(rng.randrange(0x20, 0xD800))
    with open(j("utf16bad.bin"), "wb") as f:
        f.write(b"".join(c.to_bytes(2, "little") for c in bad))

    # A Latin-1 document, and the same text in the other three encodings.
    # The mixed-script corpus above is useless for the latin1 directions: a
    # conversion there aborts at the first code point above U+00FF and the
    # measurement would time an error path on both sides.
    lb = bytearray()
    while len(lb) < target:
        if rng.random() < 0.55:
            lb += ASCII[: rng.randint(40, 200)].encode("ascii")
        else:
            lb += bytes(rng.randrange(0x20, 0x100) for _ in range(rng.randint(20, 90)))
        if rng.random() < 0.2:
            lb += b"\n"
    lat = bytes(lb).decode("latin-1")
    with open(j("latin1.bin"), "wb") as f:
        f.write(bytes(lb))
    with open(j("utf8latin1.txt"), "wb") as f:
        f.write(lat.encode("utf-8"))
    with open(j("utf16latin1.bin"), "wb") as f:
        f.write(lat.encode("utf-16-le"))
    with open(j("utf32latin1.bin"), "wb") as f:
        f.write(lat.encode("utf-32-le"))

    with open(j("ascii.txt"), "wb") as f:
        f.write((ASCII * 6000).encode("ascii"))

    # A 'find' workload: the needle never occurs, so the search scans the whole
    # buffer instead of returning on the first byte it looks at.
    needle_text = "".join(rng.choice(ASCII) for _ in range(300 * 1024))
    needle_text = needle_text.replace("\x01", "")
    with open(j("findneedle.txt"), "wb") as f:
        f.write(needle_text.encode("ascii"))

    raw = bytes(rng.randrange(256) for _ in range(3 * 40 * 1024))
    with open(j("bin.bin"), "wb") as f:
        f.write(raw)
    b64 = base64.b64encode(raw).decode("ascii")
    wrapped = "\n".join(b64[i : i + 76] for i in range(0, len(b64), 76))
    with open(j("base64.txt"), "wb") as f:
        f.write(wrapped.encode("ascii"))

    for n in sorted(os.listdir(out)):
        print("%-14s %8d" % (n, os.path.getsize(j(n))))


if __name__ == "__main__":
    main()
