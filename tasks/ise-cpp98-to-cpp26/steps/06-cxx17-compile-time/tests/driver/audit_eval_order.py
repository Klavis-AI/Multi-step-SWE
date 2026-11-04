#!/usr/bin/env python3
"""Flag driver output calls whose arguments are evaluated in an unspecified order.

The order in which a compiler evaluates two arguments of the same call is
unspecified in every C++ standard this migration line touches, C++98 through
C++26 alike. So a line like

    outf("remove -> %d, now [%s]", list.remove(x), dump(list).c_str());

records the compiler, not the library: GCC evaluates right to left and prints
the list as it was *before* the removal, Clang would print it after. A
transcript with one of those in it is an expectation that a correct migration
can fail.

The rule this enforces is narrow and mechanical: within a single out()/outf()
call, at most one argument may mutate state, and if one does, no other
argument may read it. Implemented as: flag any out/outf call containing a
mutating call plus any other call that is not on the pure list.

Exit status is 1 if anything was flagged, so this can gate the build.
"""

import re
import sys
from pathlib import Path

# Names that change something the rest of the argument list could observe.
MUTATORS = {
    "add", "addStrings", "addAndGet", "allocId", "assign", "clear", "close",
    "decrement", "del", "increment", "insert", "exchange", "extract",
    "getAndAdd", "getAndSet", "loadFromFile", "loadFromStream", "move", "open",
    "pack", "pop", "push", "put", "read", "readBuffer", "remove", "resize",
    "seek", "set", "setCapacity", "setCaseSensitive", "setCommaText",
    "setData", "setDelimitedText", "setDelimiter", "setDupMode",
    "setLineBreak", "setNameValueSeparator", "setOwnsObjects", "setPointer",
    "setPosition", "setPropString", "setQuoteChar", "setSize", "setSorted",
    "setString", "setText", "setValue", "sort", "take", "unpack", "update",
    "write", "writeBuffer",
    # Free functions in ise_sys_utils that write through a reference.
    "fetchStr", "extractQuotedStr", "splitString", "splitStringToInt",
    "encodeDateTime", "decodeDateTime", "deleteDir", "deleteFile",
    "forceDirectories", "createDir", "renameFile", "saveToFile",
    "saveToStream", "find",
}

# Calls with no observable effect on anything else in the same argument list.
PURE = {
    "esc", "intToStr", "int64ToStr", "floatToStr", "boolToStr", "c_str",
    "data", "length", "size", "empty", "str", "tag", "dumpList",
    "dumpStrings", "static_cast", "reinterpret_cast", "const_cast", "tok",
    "untok", "strlen", "string", "substr", "compare", "find_first_of",
    "tmpPath", "tmpDir", "getErrorMessage",
}

CALL = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)\s*\(")


def mask_literals(source):
    """Blank out the contents of every string and character literal.

    A format string is full of things that look like calls -- "del(%d)" --
    and full of parentheses that would throw off the brace matching. Replacing
    the contents with spaces keeps every offset and line number intact.
    """
    out = list(source)
    i = 0
    n = len(source)
    while i < n:
        ch = source[i]
        if ch in "\"'":
            quote = ch
            i += 1
            while i < n:
                if source[i] == "\\":
                    out[i] = " "
                    if i + 1 < n and source[i + 1] != "\n":
                        out[i + 1] = " "
                    i += 2
                    continue
                if source[i] == quote:
                    break
                if source[i] != "\n":
                    out[i] = " "
                i += 1
        elif source.startswith("//", i):
            while i < n and source[i] != "\n":
                out[i] = " "
                i += 1
            continue
        elif source.startswith("/*", i):
            while i < n and not source.startswith("*/", i):
                if source[i] != "\n":
                    out[i] = " "
                i += 1
            continue
        i += 1
    return "".join(out)


ACCESSOR = re.compile(r"^(get|is|has|Is)[A-Z_]")


def is_pure(name):
    """A call with no observable effect on the rest of the argument list.

    Named accessors are pure by convention throughout ise -- and where one is
    not, the mutator it hides is caught instead by the `touches` rule, which
    does not consult this list.
    """
    return name in PURE or bool(ACCESSOR.match(name))


def calls_in(text):
    """Every identifier that is called in `text`, in source order."""
    return [m.group(1) for m in CALL.finditer(text)]


def output_calls(source):
    """Yield (line_number, full_text) for every out()/outf() call."""
    for match in re.finditer(r"\bout f?\(|\boutf?\s*\(", source):
        start = match.end() - 1
        depth = 0
        i = start
        while i < len(source):
            if source[i] == "(":
                depth += 1
            elif source[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        yield source.count("\n", 0, match.start()) + 1, source[match.start():i + 1]


IDENT = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)\b")

# Type names and keywords that are identifiers but never objects.
NOT_AN_OBJECT = {
    "true", "false", "NULL", "int", "long", "char", "bool", "void", "const",
    "return", "if", "else", "static_cast", "reinterpret_cast", "const_cast",
    "drv", "std", "string", "INT64", "UINT64", "size_t", "POINTER", "c_str",
}


def call_span(text, open_paren):
    """Index just past the closing parenthesis of the call opening here."""
    depth = 0
    i = open_paren
    while i < len(text):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return len(text)


def touched_objects(text, match):
    """Identifiers a mutating call could change: its receiver and its arguments.

    `list.remove(x)` touches `list`; `fetchStr(s, ',', true)` touches `s`,
    because ise's free functions take the thing they consume by reference.
    """
    end = call_span(text, match.end() - 1)
    inner = text[match.end():end - 1]
    touched = {n for n in IDENT.findall(inner) if n not in NOT_AN_OBJECT}

    # The receiver is part of the call, so it has to come out of the text that
    # is checked for a second reference -- otherwise `list.remove(x)` looks
    # like it reads `list` a second time.
    start = match.start()
    receiver = re.search(r"([A-Za-z_][A-Za-z_0-9]*)\s*(?:\.|->)\s*$", text[:start])
    if receiver:
        touched.add(receiver.group(1))
        start = receiver.start(1)
    return touched, start, end


def main(paths):
    problems = []
    for path in paths:
        source = mask_literals(Path(path).read_text())
        for line, text in output_calls(source):
            # The out/outf call itself is the first name matched.
            names = calls_in(text)[1:]
            mutating = [n for n in names if n in MUTATORS]
            if not mutating:
                continue

            reasons = set()

            # Two mutating calls in one argument list are unordered relative to
            # each other whatever they touch.
            if len(mutating) > 1:
                reasons.update(mutating)

            # One mutating call is fine only if nothing else in the same
            # argument list reads what it touched.
            for match in CALL.finditer(text):
                if match.group(1) not in MUTATORS:
                    continue
                touched, start, end = touched_objects(text, match)
                rest = text[:start] + text[end:]
                for name in IDENT.findall(rest):
                    if name in touched:
                        reasons.add("%s touches %s" % (match.group(1), name))

            # Anything impure that is neither a known mutator nor a known pure
            # call is unclassified; say so rather than passing it silently.
            unknown = [n for n in names if not is_pure(n) and n not in MUTATORS]
            if unknown and mutating:
                reasons.update("unclassified:" + n for n in unknown)

            if reasons:
                problems.append((path, line, sorted(reasons),
                                 " ".join(text.split())[:110]))

    for path, line, names, text in problems:
        print("%s:%d: %s" % (path, line, ",".join(names)))
        print("    %s" % text)

    print("%d flagged" % len(problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
