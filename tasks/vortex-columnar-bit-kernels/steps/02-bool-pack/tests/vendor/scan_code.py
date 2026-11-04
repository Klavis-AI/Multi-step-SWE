#!/usr/bin/env python3
"""Emit a Rust file with everything that is not code removed.

    scan_code.py <file> [<file> ...]

Used by tests/test.sh for the capability-envelope scan, which greps the output
for an AVX-512 intrinsic or a thread spawn. The scan must read *code*, not
bytes, because a mention is not an implementation: a comment, a string literal
or a `#[cfg(test)]` item (which no build the verifier makes compiles) would
otherwise fail a candidate whose implementation is exactly what the step asks
for. The alternative -- a rule that only matches the shapes upstream happens to
use -- is what fails the oracle; this strips the non-code and keeps the check.

Removed: line and nested block comments; string literals of every Rust flavour
(`"..."`, `r"..."`, `r#"..."#` with any hash count, byte strings); char
literals; and the items attributed `#[cfg(test)]` (or `#[cfg(all(test, ...))]`,
which is test-only for the same reason -- an `any(test, ...)` is deliberately
left alone, because a feature could make it compile outside a test build).
Lifetimes are left alone. Removed spans are replaced by their own newlines, so
a finding still points at the right line and the line count of the input is
preserved.

Also removed: a whole *file* the tree declares as a test-only out-of-line
module -- `#[cfg(test)] mod tests;` in the parent, the body in `tests.rs` is
blanked entire, exactly like the inline form it mirrors. No build the verifier
measures compiles either one, so a `_mm512_` intrinsic or a thread spawn in it
is not an implementation of either; `main` reports the exemption on stderr, so
it shows up in the verifier's log instead of reading like a file that passed.

This is a lexical filter, not a parser: it is deliberately conservative in the
one ambiguous place (a `'` that opens neither a char literal nor a lifetime)
and it never removes an item unless it can find the item's own braces.
"""

import os
import re
import sys

RAW_STRING = re.compile(r"(?:b?r)(?P<hashes>#*)\"")
CHAR_LITERAL = re.compile(r"'(?:\\.|[^\\'])'")


def blank(text):
    """A span's replacement: its newlines, so line structure is preserved."""
    return "\n" * text.count("\n")


def strip_comments_and_literals(src):
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        two = src[i : i + 2]
        if two == "//":
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(blank(src[i:j]))
            i = j
            continue
        if two == "/*":
            depth, j = 1, i + 2
            while j < n and depth:
                if src.startswith("/*", j):
                    depth += 1
                    j += 2
                elif src.startswith("*/", j):
                    depth -= 1
                    j += 2
                else:
                    j += 1
            out.append(blank(src[i:j]))
            i = j
            continue
        m = RAW_STRING.match(src, i)
        if m:
            term = '"' + m.group("hashes")
            j = src.find(term, m.end())
            j = n if j < 0 else j + len(term)
            out.append(blank(src[i:j]))
            i = j
            continue
        if c == '"' or (c == "b" and src[i : i + 2] == 'b"'):
            j = i + (2 if c == "b" else 1)
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == '"':
                    j += 1
                    break
                j += 1
            out.append(blank(src[i:j]))
            i = j
            continue
        if c == "'":
            m = CHAR_LITERAL.match(src, i)
            if m:
                out.append(" ")
                i = m.end()
                continue
            # a lifetime: `&'a str`, `Foo<'a>` -- keep it
            out.append(c)
            i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def is_test_only_attr(inner):
    """True for `cfg(test)` and `cfg(all(test, ...))`, false for anything else."""
    norm = "".join(inner.split())
    if norm == "cfg(test)":
        return True
    m = re.fullmatch(r"cfg\(all\(([^()]*)\)\)", norm)
    return bool(m) and "test" in m.group(1).split(",")


def strip_cfg_test_items(code):
    """Drop test-only attributed items, braces and all."""
    out = []
    i, n = 0, len(code)
    while i < n:
        if code.startswith("#[", i):
            depth, j = 0, i + 1
            while j < n:
                if code[j] == "[":
                    depth += 1
                elif code[j] == "]":
                    depth -= 1
                    if depth == 0:
                        j += 1
                        break
                j += 1
            if depth == 0 and is_test_only_attr(code[i + 2 : j - 1]):
                k = j
                # The attribute must be attached to an item with a body:
                # nothing but the item's own path and generics may stand
                # between it and its `{`, so a `;` or a closing brace first
                # means this is not such an item and the text stays.
                while k < n and code[k] not in "{;}":
                    k += 1
                if k < n and code[k] == "{":
                    depth, k2 = 0, k
                    while k2 < n:
                        if code[k2] == "{":
                            depth += 1
                        elif code[k2] == "}":
                            depth -= 1
                            if depth == 0:
                                break
                        k2 += 1
                    end = k2 + 1 if k2 < n else n
                    out.append(blank(code[i:end]))
                    i = end
                    continue
            out.append(code[i:j])
            i = j
            continue
        out.append(code[i])
        i += 1
    return "".join(out)


# An out-of-line module declaration, with the attributes that precede it. The
# `[^\]]*` is safe for `cfg(all(test, ...))` -- that has no `]` inside it -- and
# a `]` inside a string or comment cannot reach this: the text is stripped of
# both before this runs.
MOD_DECL = re.compile(
    r"(?<![A-Za-z0-9_])"
    r"(?P<attrs>(?:#\s*\[[^\]]*\]\s*)*)"
    r"mod\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;"
)
ATTR = re.compile(r"#\s*\[(?P<inner>[^\]]*)\]")


def declares_module(code, name, test_only):
    """True when `code` declares the out-of-line module `name`.

    With `test_only`, only a declaration carrying a test-only attribute
    (`#[cfg(test)] mod name;`) counts. Attributes and whitespace may stand
    between that attribute and the item, as in `strip_cfg_test_items`.
    """
    for m in MOD_DECL.finditer(code):
        if m.group("name") != name:
            continue
        if not test_only:
            return True
        if any(is_test_only_attr(a.group("inner")) for a in ATTR.finditer(m.group("attrs"))):
            return True
    return False


def parent_candidates(path):
    """The files that could declare `path` as an out-of-line module.

    Rust resolves `mod m;` in the module's own directory: a module whose file
    is `a/b.rs` or `a/b/mod.rs` declares its children in `a/b.rs` or `a/b/mod.rs`,
    and a module declared at a crate root's own depth is declared in the crate
    root (`lib.rs`/`main.rs`) instead.
    """
    d = os.path.dirname(os.path.abspath(path))
    up = os.path.dirname(d)
    cands = [os.path.join(up, os.path.basename(d) + ".rs"), os.path.join(d, "mod.rs")]
    if os.path.isfile(os.path.join(up, "Cargo.toml")):
        cands += [os.path.join(d, "lib.rs"), os.path.join(d, "main.rs")]
    return cands


def is_test_only_file(path, _depth=0):
    """True when the tree declares `path` as a test-only out-of-line module.

    Such a file is test-only for the same reason `strip_cfg_test_items` removes
    the inline form: no build the verifier makes compiles it, so a mention of
    an AVX-512 intrinsic or a thread spawn in it is not an implementation of
    either. The walk climbs through module parents only.
    """
    if _depth > 8:
        return False
    name = os.path.basename(path)
    if not name.endswith(".rs"):
        return False
    name = name[:-3]
    for parent in parent_candidates(path):
        try:
            code = strip_comments_and_literals(
                open(parent, encoding="utf-8", errors="replace").read()
            )
        except OSError:
            continue
        if declares_module(code, name, True):
            return True
        # A module declared by a parent that is itself test-only is test-only
        # whether or not the declaration repeats the attribute.
        if declares_module(code, name, False) and is_test_only_file(parent, _depth + 1):
            return True
    return False


def scan(src):
    return strip_cfg_test_items(strip_comments_and_literals(src))


def main(argv):
    if len(argv) < 2:
        sys.stderr.write("usage: scan_code.py <file> [<file> ...]\n")
        return 2
    for path in argv[1:]:
        try:
            src = open(path, encoding="utf-8", errors="replace").read()
        except OSError as exc:
            sys.stderr.write("scan_code.py: %s\n" % exc)
            return 1
        if is_test_only_file(path):
            sys.stderr.write(
                "scan_code.py: %s is an out-of-line #[cfg(test)] module -- "
                "not scanned\n" % path
            )
            sys.stdout.write(blank(src))
            continue
        sys.stdout.write(scan(src))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
