#!/usr/bin/env python3
"""Flag driver loops whose literal bound disagrees with the table they index.

A case that walks a table of inputs is usually written

    static const char *INPUTS[] = { "a", "b", "c" };
    for (int i = 0; i < 3; i++) ... INPUTS[i] ...

and the bound is a hand-written count. Add an input without touching the
bound and the last one is never run; delete one and the loop reads past the
end of the array and prints whatever the linker put next. The second is the
dangerous one: it is not a compile error, not a crash, and it produces a line
that differs between two builds of the same library -- so it lands in the
recorded expectations as a difference a correct migration can fail.

DRV_COUNT(array) is the fix. This is the check that finds the ones already
written, and the gate that keeps new ones from landing.

Exit status is 1 if anything was flagged.
"""

import re
import sys
from pathlib import Path

from audit_eval_order import mask_literals

# `static const char *NAME[] =` / `const RangeCase NAME[] =` / `static const int NAME[] =`
# and the two-dimensional form `static const int NAME[][6] =`, whose rows are
# what a loop counts -- a table of rows is the shape a case with several fields
# per input naturally takes, and it needs the check at least as much.
DECL = re.compile(
    r"\b(?:static\s+)?(?:const\s+)?[A-Za-z_][A-Za-z_0-9]*\s*\*?\s*"
    r"([A-Za-z_][A-Za-z_0-9]*)\s*\[\s*\](?:\s*\[[^\]]*\])*\s*=")

# `for (int i = 0; i < 12; i++)` -- only literal bounds are checkable.
LOOP = re.compile(
    r"\bfor\s*\(\s*(?:int|size_t|unsigned)\s+([A-Za-z_][A-Za-z_0-9]*)\s*=\s*0\s*;"
    r"\s*\1\s*<\s*([0-9]+)\s*;")


def brace_span(text, start):
    """Index just past the `}` closing the `{` at or after `start`."""
    i = text.index("{", start)
    depth = 0
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unterminated initialiser")


def element_count(initialiser):
    """Elements in a brace initialiser, counting a nested brace group as one.

    `{ {0,0}, {1,1} }` is two elements, not four. A trailing comma before the
    closing brace does not add one.
    """
    inner = initialiser[initialiser.index("{") + 1:initialiser.rindex("}")]
    depth = 0
    count = 0
    seen = False
    for ch in inner:
        if ch in "{([":
            depth += 1
        elif ch in "})]":
            depth -= 1
        elif ch == "," and depth == 0:
            count += 1
            seen = False
            continue
        if not ch.isspace():
            seen = True
    return count + (1 if seen else 0)


def statement_span(text, start):
    """Index just past the statement or block a `for (...)` header introduces."""
    i = text.index(")", start)
    # Skip to the first thing after the header.
    j = i + 1
    while j < len(text) and text[j].isspace():
        j += 1
    if j < len(text) and text[j] == "{":
        return brace_span(text, j)
    end = text.find(";", j)
    return len(text) if end < 0 else end + 1


def main(paths):
    problems = []
    for path in paths:
        source = mask_literals(Path(path).read_text())
        raw = Path(path).read_text()

        # Position-ordered, not a dict: the same table name is reused from one
        # case to the next, and a loop must be checked against the declaration
        # above it rather than against the last one in the file.
        decls = []
        for decl in DECL.finditer(source):
            try:
                end = brace_span(source, decl.end())
            except ValueError:
                continue
            decls.append((decl.start(), decl.group(1),
                          element_count(source[decl.end():end])))

        for loop in LOOP.finditer(source):
            index, bound = loop.group(1), int(loop.group(2))
            body = source[loop.start():statement_span(source, loop.start())]
            nearest = {}
            for position, name, count in decls:
                if position < loop.start():
                    nearest[name] = count
            for name, count in nearest.items():
                if not re.search(r"\b%s\s*\[\s*%s\b" % (name, index), body):
                    continue
                if count != bound:
                    line = raw.count("\n", 0, loop.start()) + 1
                    problems.append((path, line, name, count, bound))

    for path, line, name, count, bound in problems:
        print("%s:%d: loop bound %d but %s has %d elements"
              % (path, line, bound, name, count))

    print("%d flagged" % len(problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
