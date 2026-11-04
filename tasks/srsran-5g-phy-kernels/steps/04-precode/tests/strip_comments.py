#!/usr/bin/env python3
"""Print each line with its comments and string literals removed, preserving the
line count.

The envelope check judges code, not prose or data. Upstream's own AVX2 sources
end an `#else`/`#endif` with `// defined(__AVX512F__) && ...`, and a token scan
over the raw text flags those comments as AVX-512 -- it did, on a
candidate-shaped tree that had copied that style. A token inside a string
literal is not compiled either: `"#pragma omp"` names no directive and starts
no thread, so flagging it would fail a candidate for a diagnostic message. What
gets compiled is the code that is left, so that is what is judged here.

Line structure is preserved exactly (a comment's or literal's text is replaced
by nothing, its line is kept), so the line numbers a scan reports still point at
the original file.

String literals are scanned to their closing quote on the same line, with
backslash escapes honoured. A raw string (`R"(...)"`) is treated like any
other literal, which is exact for the single-line form; a multi-line raw string
is not tracked across lines, in which case its continuation lines are judged as
code -- the conservative direction, since that can only flag more, never less.
"""

import sys


def strip_line(line, in_block):
    out = []
    i = 0
    n = len(line)
    while i < n:
        if in_block:
            j = line.find("*/", i)
            if j < 0:
                return "".join(out), True
            i = j + 2
            in_block = False
            continue
        c = line[i]
        if c == "/" and line.startswith("//", i):
            break
        if c == "/" and line.startswith("/*", i):
            in_block = True
            i += 2
            continue
        if c == '"' or c == "'":
            quote = c
            i += 1
            while i < n:
                if line[i] == "\\":
                    i += 2
                    continue
                if line[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out), in_block


def main(path):
    in_block = False
    try:
        if path == "-":
            lines = sys.stdin.read().split("\n")
        else:
            lines = open(path, errors="replace").read().split("\n")
    except OSError:
        return
    for line in lines:
        code, in_block = strip_line(line, in_block)
        sys.stdout.write(code + "\n")


if __name__ == "__main__":
    main(sys.argv[1])
