#!/usr/bin/env python3
"""Print a C/C++/asm translation unit with comments removed.

Used by check_isa.sh so a gate on code cannot fire on prose.  It is
deliberately simple -- it does not try to be a preprocessor -- but it does
respect string and character literals, so a '//' inside a string is not
mistaken for a comment.
"""
import sys


def strip(text: str) -> str:
    out = []
    i, n = 0, len(text)
    state = None  # None, 'line', 'block', 'str', 'chr'
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state is None:
            if c == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block"
                i += 2
                continue
            if c == '"':
                state = "str"
            elif c == "'":
                state = "chr"
            out.append(c)
        elif state == "line":
            if c == "\n":
                state = None
                out.append(c)
        elif state == "block":
            if c == "*" and nxt == "/":
                state = None
                i += 2
                continue
            if c == "\n":
                out.append(c)  # keep line numbering
        elif state == "str":
            out.append(c)
            if c == "\\":
                if i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
            elif c == '"':
                state = None
        elif state == "chr":
            out.append(c)
            if c == "\\":
                if i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
            elif c == "'":
                state = None
        i += 1
    return "".join(out)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: strip_comments.py <file>", file=sys.stderr)
        return 2
    with open(sys.argv[1], "r", encoding="utf-8", errors="replace") as fh:
        sys.stdout.write(strip(fh.read()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
