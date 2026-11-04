#!/usr/bin/env python3
"""Hash the *code lines* of the files named on the command line, one sha1 per
line, in file order. `-` reads stdin instead of a file, and then `--lang PATH`
says which file those lines came from, because the comment syntax depends on the
extension: the verifier pipes a diff of one file through stdin, and hashing that
stream under the wrong language would drop every `#include` from the candidate's
side while upstream's side kept them.

`--index ROOT LIST OUT` writes the other side of the diagnostic: every code line
of every file LIST names under ROOT, as sorted "<path> <sha1>" lines.

This is the measuring instrument for the non-scored similarity diagnostic. The
verifier image hashes upstream's copy of each file this milestone's reference
restores, at image-build time, from the pinned checkout; the verifier hashes the
lines a candidate added, at grading time. Both sides run this script, so the two
cannot drift apart the way two hand-written normalisers would.

A line counts as code when, after stripping leading and trailing whitespace and
collapsing internal whitespace runs to one space, it is at least four characters
long, contains at least one alphanumeric or underscore, and is not a comment.

Comment syntax comes from the file's own extension, because `#` opens a comment
in .S/.asm/.pl and a preprocessor directive in .c/.cc/.h:

    //  /*  */      comment in every file
    *               comment in the C family only (block-comment continuation)
    #               comment everywhere EXCEPT the C family
    ;               comment in .asm only (nasm), a separator in .S and perl

The thresholds are deliberately blunt, and they cut both ways on purpose. This is
a *verbatim* measure, so nothing here rescues a reformatted or renamed copy: it
reads near zero. And the lines it refuses to count -- lone braces, `#endif`, bare
punctuation, three-letter mnemonics -- are the ones every implementation of the
same algorithm emits, so counting them would raise every candidate's floor
without saying anything about where the code came from.
"""

import hashlib
import os
import re
import sys

C_FAMILY = {".c", ".cc", ".cxx", ".h", ".hh", ".hpp", ".inc", ".cpp"}

_WS = re.compile(r"\s+")


def is_comment(s, ext):
    if s.startswith("//") or s.startswith("/*") or s.startswith("*/"):
        return True
    if s.startswith("*") and ext in C_FAMILY:
        return True
    if s.startswith("#") and ext not in C_FAMILY:
        return True
    if s.startswith(";") and ext == ".asm":
        return True
    return False


def code_lines(stream, ext):
    for raw in stream:
        s = _WS.sub(" ", raw.strip())
        if len(s) < 4:
            continue
        if not any(c.isalnum() or c == "_" for c in s):
            continue
        if is_comment(s, ext):
            continue
        yield s


def write_index(root, listfile, out):
    """`--index ROOT LIST OUT`: the upstream side of the diagnostic.

    Writes "<path> <sha1>" for every code line of every file named in LIST,
    sorted and deduplicated. A listed file that is not in ROOT is an error, not
    a skip: REF_FILES names the files the pinned checkout must have, so a
    missing one means the wrong tree is being indexed, and a silently smaller
    index reads as "the candidate copied nothing" for every candidate forever.
    """
    paths = [l.strip() for l in open(listfile) if l.strip()]
    missing = [p for p in paths if not os.path.isfile(os.path.join(root, p))]
    if missing:
        sys.stderr.write("--index: %d of %d listed files are not in %s (first: %s)\n"
                         % (len(missing), len(paths), root, missing[0]))
        return 1
    lines = set()
    for p in paths:
        with open(os.path.join(root, p), "r", encoding="utf-8", errors="replace") as f:
            for s in code_lines(f, os.path.splitext(p)[1].lower()):
                lines.add("%s %s" % (p, hashlib.sha1(s.encode("utf-8")).hexdigest()))
    with open(out, "w") as f:
        for l in sorted(lines):
            f.write(l + "\n")
    sys.stderr.write("indexed %d code lines over %d files\n" % (len(lines), len(paths)))
    return 0


def main(argv):
    lang = ""
    rest = []
    i = 0
    while i < len(argv):
        if argv[i] == "--lang":
            i += 1
            if i >= len(argv):
                sys.stderr.write("--lang needs a path\n")
                return 2
            lang = argv[i]
        else:
            rest.append(argv[i])
        i += 1
    if rest and rest[0] == "--index":
        if len(rest) != 4:
            sys.stderr.write("usage: code_lines.py --index ROOT LIST OUT\n")
            return 2
        return write_index(rest[1], rest[2], rest[3])
    if not rest:
        sys.stderr.write("usage: code_lines.py [--lang PATH] FILE... | -\n"
                         "       code_lines.py --index ROOT LIST OUT\n")
        return 2
    out = sys.stdout
    for path in rest:
        if path == "-":
            for s in code_lines(sys.stdin, os.path.splitext(lang)[1].lower()):
                out.write(hashlib.sha1(s.encode("utf-8")).hexdigest() + "\n")
            continue
        ext = os.path.splitext(path)[1].lower()
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for s in code_lines(f, ext):
                out.write(hashlib.sha1(s.encode("utf-8")).hexdigest() + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
