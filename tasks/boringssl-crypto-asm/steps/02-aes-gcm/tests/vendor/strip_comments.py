#!/usr/bin/env python3
"""Remove comments from a source file for the capability-envelope scan.

Usage: strip_comments.py <path>   ->  the file's code, comments removed, on stdout

Why this is not a `sed` one-liner
---------------------------------
It was one, and it did not work. The check needs to look at a candidate's code
for AVX-512 / VAES / VPCLMULQDQ constructs while not charging them for *mentioning*
those words in a comment -- the procedure records that exact mistake twice, and
it had already recurred once here on an assembly comment. The shipped version
was:

    sed -e 's://.*::' -e 's:^[[:space:]]*#.*::' -e 's:;.*::' "$f" \
        | sed -e ':a' -e 'N' -e '$!ba' -e 's:/\\*[^*]*\\*+[^/]*\\*/::g'

Two independent defects, both measured on this host rather than reasoned about:

  1. `\\*+` is not "one or more asterisks" in BRE. `\\*` is a *literal* asterisk
     and `+` is then a literal plus, so the pattern requires the text to contain
     `*+` and matches no comment delimiter ever written. Correct BRE would be
     `\\*\\+`. (Checked: `s:\\*+:'X':` leaves `A *** B` alone; `s:\\*\\+:X:` gives
     `A X B`.)

  2. GNU sed's `N` exits *without running the rest of the script* when there is
     no next line, so on a single-line file the substitution never executed at
     all. (Checked: the same pipeline strips a two-line file's block comment and
     leaves a one-line file's untouched.)

The combined effect was that every C/C++ block comment reached the scan intact,
so a candidate who wrote `/* AVX-512 would need zmm, absent here */` was charged
for a capability they had explicitly declined to use.

What this does instead
----------------------
A single pass with an explicit state machine, because the interesting cases are
not regular:

  * `/* ... */` and `// ...` are removed everywhere. Both forms are comments in
    C, C++ and the perlasm prose; a multi-line block comment is handled properly.
  * `# ...` and `; ...` are removed ONLY in .S/.asm/.pl, where they are the
    assembler and perlasm comment forms. In C and C++ a `#` begins a
    preprocessor directive and a `;` ends a statement, so removing them there
    deletes real code -- the shipped version stripped `;` from C files, which
    truncated every C line at its first semicolon and could hide a real
    out-of-envelope instruction sitting after one. That is a false negative in
    the same gate, so it is fixed here too.
  * String and character literals are PRESERVED, deliberately. Inline assembly
    is written as a string -- `asm("vmovdqu64 %zmm0, (%rax)")` -- so a stripper
    that removed string contents would blind the gate to the most likely way a
    candidate would actually smuggle AVX-512 into a C file. The cost is that a
    mnemonic inside a *human-readable* string is also reported; that is the
    direction this check should err in.
"""

import sys


def strip(path, text):
    """Return `text` with comments removed, string literals kept."""
    asm = path.endswith((".S", ".asm", ".pl"))
    out = []
    i = 0
    n = len(text)
    quote = None  # the open string/char literal delimiter, or None

    while i < n:
        c = text[i]

        if quote is not None:
            out.append(c)
            if c == "\\" and i + 1 < n:
                # An escape consumes the next character, so a backslash-quote
                # inside a string does not close it.
                out.append(text[i + 1])
                i += 2
                continue
            if c == quote:
                quote = None
            i += 1
            continue

        two = text[i:i + 2]

        if two == "/*":
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue

        if two == "//":
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue

        if asm and c in "#;":
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue

        if c in "\"'":
            quote = c

        out.append(c)
        i += 1

    return "".join(out)


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: strip_comments.py <path>\n")
        return 2
    path = sys.argv[1]
    with open(path, "r", errors="replace") as f:
        text = f.read()
    sys.stdout.write(strip(path, text))
    return 0


if __name__ == "__main__":
    sys.exit(main())
