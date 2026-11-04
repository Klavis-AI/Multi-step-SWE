#!/usr/bin/env python3
"""Blank out comments in a source file, keeping its line structure.

    strip_comments.py [--hash] [<file>]

Reads <file> (or stdin) and writes the same text with every comment replaced
by spaces -- newlines are preserved, so line numbers and diff structure are
unchanged.  Used by check_isa.sh so the instruction-set envelope is applied to
the code a candidate wrote and not to prose they wrote *about* the envelope: a
comment saying "we deliberately do not use zmm0 here" must not fail the step,
and a wide token after a semicolon must still be found.

Comment syntax by file kind (with a file argument):
  *.S *.s *.asm            '#' to end of line, plus /* */ and //
  Makefile *.mk *.mak      '#' to end of line
  anything else            /* */ and //

In the assembly kind a '#' that starts a preprocessor directive (#include,
#define, #if, ...) is left in place: those lines are code, not commentary, and
a macro that expands to a wide mnemonic is exactly what this gate looks for.

With --hash (stdin only) '#' is treated as a comment: that is the mode for
diff lines coming from Makefiles.

Exit status is 0.  Unreadable input is reported on stderr and the input is
passed through unchanged.
"""

import sys

_DIRECTIVES = (
    "include",
    "define",
    "undef",
    "if",
    "ifdef",
    "ifndef",
    "else",
    "elif",
    "endif",
    "error",
    "warning",
    "line",
    "pragma",
    "file",
    "ident",
    "import",
    "using",
)

_C_COMMENTS = {"c", "h", "inc", "cc", "cpp", "hpp", "cxx"}
_ASM_COMMENTS = {"s", "asm"}
_HASH_COMMENTS = {"mk", "mak"}
_HASH_NAMES = {"Makefile", "makefile", "GNUmakefile"}


def _kind(path, hash_forced):
    if hash_forced:
        return "hash"
    if path is None:
        return "c"
    name = path.rsplit("/", 1)[-1]
    if name in _HASH_NAMES:
        return "hash"
    ext = name.rsplit(".", 1)[-1].lower() if "." in name else ""
    if ext in _ASM_COMMENTS:
        return "asm"
    if ext in _HASH_COMMENTS:
        return "hash"
    if ext in _C_COMMENTS:
        return "c"
    return "c"


def _is_directive(text, i):
    """True if the '#' at text[i] opens a preprocessor directive line."""
    j = i + 1
    while j < len(text) and text[j] in " \t":
        j += 1
    k = j
    while k < len(text) and (text[k].isalnum() or text[k] == "_"):
        k += 1
    return text[j:k] in _DIRECTIVES


def strip(text, kind):
    out = []
    i = 0
    n = len(text)
    state = None
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state is None:
            if c == "/" and nxt == "*":
                state = "block"
                out.append("  ")
                i += 2
                continue
            if c == "/" and nxt == "/":
                state = "line"
                out.append("  ")
                i += 2
                continue
            if c == "#":
                if kind == "hash" or (kind == "asm" and not _is_directive(text, i)):
                    state = "line"
                    out.append(" ")
                    i += 1
                    continue
            out.append(c)
            i += 1
            continue
        # inside a comment
        if c == "\n":
            if state in ("line", "hash"):
                state = None
            out.append("\n")
            i += 1
            continue
        if state == "block" and c == "*" and nxt == "/":
            state = None
            out.append("  ")
            i += 2
            continue
        out.append(" ")
        i += 1
    return "".join(out)


def main(argv):
    hash_forced = False
    path = None
    for a in argv[1:]:
        if a == "--hash":
            hash_forced = True
        elif path is None:
            path = a
        else:
            sys.stderr.write("strip_comments.py: too many arguments\n")
            return 2
    if path is None:
        text = sys.stdin.read()
    else:
        try:
            with open(path, "r", errors="replace") as f:
                text = f.read()
        except OSError as e:
            sys.stderr.write("strip_comments.py: %s\n" % e)
            return 3
    sys.stdout.write(strip(text, _kind(path, hash_forced)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
