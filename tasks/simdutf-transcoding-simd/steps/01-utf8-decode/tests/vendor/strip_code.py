#!/usr/bin/env python3
"""Print a C/C++ source file with its comments and string/char literals removed.

    strip_code.py <file> [<file> ...]

Used by check_isa.sh.  The AVX-512 envelope is a rule about *code*: a file that
documents the envelope in a comment ("this stays on AVX2, no _mm512_ here") is
inside it, and a token in a comment or in a string literal is not a construct
that builds anything.  Stripping both before the match is what keeps the gate
from firing on an honest tree for writing about the rule it obeys.

A scanner, not a parser.  It is deliberately conservative in the one direction
that matters: it can only ever drop text, never add any, so a file it cannot
make sense of can hide a match from its caller but can never invent one.  The
caller greps the result, and check_isa.sh falls back to the raw file if this
script fails.
"""

import sys


def strip(text):
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            # line comment: keep the newline so line structure survives
            j = text.find("\n", i)
            if j < 0:
                break
            out.append("\n")
            i = j + 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            out.append(" ")
            i = n if j < 0 else j + 2
        elif c == "R" and i + 1 < n and text[i + 1] == '"':
            # raw string literal: R"delim( ... )delim"
            k = text.find("(", i + 2)
            nl = text.find("\n", i + 2)
            if k > 0 and (nl < 0 or k < nl):
                end = ")" + text[i + 2:k] + '"'
                j = text.find(end, k + 1)
                out.append('""')
                i = n if j < 0 else j + len(end)
            else:
                out.append(c)
                i += 1
        elif c == '"':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    j += 1
                    break
                j += 1
            out.append('""')
            i = j
        elif c == "'" and not (
            i > 0 and text[i - 1].isalnum() and i + 1 < n and text[i + 1].isalnum()
        ):
            # char literal.  The guard keeps a C++14 digit separator (1'000'000)
            # out of this branch: it is not a literal, and treating it as one
            # would swallow real code up to the next apostrophe.
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == "'":
                    j += 1
                    break
                j += 1
            out.append("' '")
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def main(argv):
    out = sys.stdout
    for path in argv[1:]:
        with open(path, "r", errors="replace") as f:
            out.write(strip(f.read()))


if __name__ == "__main__":
    main(sys.argv)
