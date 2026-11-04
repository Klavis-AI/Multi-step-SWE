"""Where a SQL script's statements end.

`jf\\BaseDatabase::InitializeData()` splits the file it installs on `;`, hands
the pieces to `query()` one at a time and throws away what each one returns.
The gate has to hand a candidate's file over the same way -- one statement per
round trip, carrying on past a failure -- and the way it used to do that was
to give the whole file to `psql`.

That was wrong twice over.

`psql` is not a SQL processor. It reads backslash meta-commands out of the
file before the server ever sees it, so `\\!` in a data file ran a shell,
`\\copy ... from program` ran one too, and `\\o |` ran a third. No psql flag
turns that off: `-X` and `--no-psqlrc` only skip `~/.psqlrc`. The one way to
stop psql interpreting a candidate's file is not to give it to psql.

And `;` is not a statement terminator on its own. The MySQL database this
task migrates puts 12,730 semicolons inside 3,750 string literals of the
reference data file, so a splitter that
cuts on every one of them cuts string literals in half. Splitting is lexing.

So this module lexes, to PostgreSQL 17's own rules:

* `'...'`, with `''` for an embedded quote. A backslash is an ordinary
  character -- `standard_conforming_strings` has been `on` by default since
  9.1 and PostgreSQL 17 warns when it is turned off. `E'...'` is the form
  where a backslash escapes, and it is recognised as its own token so that
  `E'a\\';b'` stays one statement while `'a\\'; b'` is two.
* `"..."`, with `""` for an embedded quote, so `CREATE TABLE "a;b"` survives.
* `$$...$$` and `$tag$...$tag$`, which is how a function body carries
  semicolons. An identifier is consumed whole first, so the `$` in `a$b$c` is
  part of a name and does not open a dollar quote, and `$1` is a parameter.
* `--` to end of line, and `/* ... */` which nests.

What it deliberately does not do is interpret anything. A line beginning `\\!`
is not a meta-command here; it is text in the middle of whatever statement it
falls in, and the server rejects it. That is the same thing jFramework would
do with it.
"""

import re

_SPACE = " \t\r\n\f\v"
_IDENT_START = ("abcdefghijklmnopqrstuvwxyz"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ_")
_IDENT_CONT = _IDENT_START + "0123456789$"

# The tag of a dollar-quoted string: `$$`, or `$` + an identifier + `$`. The
# identifier rule is the lexer's own (`dolqdelim` in scan.l), which is why a
# digit cannot start one and `$1` reaches this point as a parameter instead.
_DOLLAR_TAG = re.compile(
    "\\$(?:[A-Za-z_\u0080-\uffff][A-Za-z0-9_\u0080-\uffff]*)?\\$")

# String literals with a one-letter type prefix. Only `E` changes how the body
# is read; the rest are here so that the prefix and the quote are taken as one
# token rather than as an identifier that happens to abut a string.
_QUOTE_PREFIX = ("E", "e", "B", "b", "X", "x", "N", "n")


def _end_quote(text, i, escapes):
    """Index just past the `'` that closes the literal starting at `text[i]`."""
    n = len(text)
    j = i + 1
    while j < n:
        c = text[j]
        if escapes and c == "\\":
            j += 2
            continue
        if c == "'":
            if j + 1 < n and text[j + 1] == "'":
                j += 2
                continue
            return j + 1
        j += 1
    return n


def _end_dquote(text, i):
    """Index just past the `"` that closes the identifier starting at `text[i]`."""
    n = len(text)
    j = i + 1
    while j < n:
        if text[j] == '"':
            if j + 1 < n and text[j + 1] == '"':
                j += 2
                continue
            return j + 1
        j += 1
    return n


def _end_block_comment(text, i):
    """Index just past the `*/` that closes the comment at `text[i]`, nesting."""
    n = len(text)
    j = i + 2
    depth = 1
    while j < n and depth:
        if text.startswith("/*", j):
            depth += 1
            j += 2
        elif text.startswith("*/", j):
            depth -= 1
            j += 2
        else:
            j += 1
    return j


def scan(text):
    """(kind, start, end, line) for every token, in order.

    kind is "space", "comment", "semi" or "code"; line is 1-based and is the
    line the token starts on. Every character of `text` is in exactly one
    token, so the caller can slice the source back out verbatim.
    """
    n = len(text)
    i = 0
    line = 1
    high = "\u0080"
    while i < n:
        ch = text[i]
        if ch in _SPACE:
            j = i
            while j < n and text[j] in _SPACE:
                j += 1
            yield ("space", i, j, line)
        elif text.startswith("--", i):
            # To the end of the line -- and the server's idea of a line ends
            # at either character: `scan.l` spells the body of a `--` comment
            # `[^\n\r]*`. A file written with CR line endings would otherwise
            # have its first comment run to the end of the file and take every
            # statement after it along.
            nl, cr = text.find("\n", i), text.find("\r", i)
            j = min(k for k in (nl, cr, n) if k >= 0)
            yield ("comment", i, j, line)
        elif text.startswith("/*", i):
            j = _end_block_comment(text, i)
            yield ("comment", i, j, line)
        elif ch == ";":
            yield ("semi", i, i + 1, line)
            j = i + 1
        elif ch == "'":
            j = _end_quote(text, i, False)
            yield ("code", i, j, line)
        elif ch == '"':
            j = _end_dquote(text, i)
            yield ("code", i, j, line)
        elif ch == "$" and _DOLLAR_TAG.match(text, i):
            tag = _DOLLAR_TAG.match(text, i).group(0)
            k = text.find(tag, i + len(tag))
            j = n if k < 0 else k + len(tag)
            yield ("code", i, j, line)
        elif ch in _IDENT_START or ch >= high:
            j = i + 1
            while j < n and (text[j] in _IDENT_CONT or text[j] >= high):
                j += 1
            word = text[i:j]
            if j < n and text[j] == "'" and word in _QUOTE_PREFIX:
                j = _end_quote(text, j, word in ("E", "e"))
            elif word in ("U", "u") and text.startswith("&'", j):
                j = _end_quote(text, j + 1, False)
            yield ("code", i, j, line)
        else:
            yield ("code", i, i + 1, line)
            j = i + 1
        line += text.count("\n", i, j)
        i = j


def split(text):
    """[(statement, line)] -- what the server should be sent, and from where.

    A statement runs from its first code character to the `;` that ends it,
    inclusive, so comments inside it come along and comments between two
    statements do not. A run of `;` with nothing but whitespace and comments
    between them yields nothing, which is what keeps a file ending in a
    newline after its last `;` from producing a trailing empty statement --
    the thing `explode(";", $sql)` gets wrong and `InitializeData()` then
    hands to `query()` as an empty string.
    """
    out = []
    start = None
    startline = 1
    for kind, i, j, line in scan(text):
        if kind == "semi":
            if start is not None:
                out.append((text[start:j], startline))
                start = None
        elif kind == "code" and start is None:
            start, startline = i, line
    if start is not None:
        out.append((text[start:].rstrip(), startline))
    return out


# ---------------------------------------------------------------- selftest --

_CASES = [
    ("SELECT 1; SELECT 2", 2, None),
    ("SELECT 1;\n", 1, None),
    ("", 0, None),
    ("   \n -- nothing but a comment\n", 0, None),
    (";;;\n;", 0, None),
    ("INSERT INTO t VALUES ('a;b'); SELECT 2;", 2, "'a;b'"),
    ("SELECT 'it''s; here';", 1, None),
    ("SELECT '';", 1, None),
    # E'' escapes the quote, so the `;` inside stays inside.
    ("SELECT E'a\\';b';", 1, None),
    # standard_conforming_strings is on: the backslash is data and the string
    # ends at the second quote, so this is two statements, not one.
    ("SELECT 'a\\'; SELECT 2;", 2, None),
    ("SELECT $$a;b$$;", 1, None),
    ("SELECT $tag$a; $$ b$tag$;", 1, None),
    ("SELECT a$b$c FROM t;", 1, None),
    ("SELECT x FROM t WHERE id = $1;", 1, None),
    ('CREATE TABLE "a;b" (x int); SELECT 1;', 2, None),
    ('CREATE TABLE "a""b;c" (x int);', 1, None),
    ("-- c;\nSELECT 1;", 1, "SELECT 1;"),
    ("/* a; /* still; */ b; */ SELECT 1;", 1, "SELECT 1;"),
    ("SELECT U&'a;b';", 1, None),
    ("SELECT B'0101';", 1, None),
    # A meta-command is not one: it is text inside the statement that follows.
    ("\\! id\nSELECT 1;", 1, "\\! id"),
    # A trailing fragment with no terminator is still handed over, because
    # `explode(";", $sql)` produces it too and `query()` is given it.
    ("SELECT 1;\n\\! id\n", 2, "SELECT 1;"),
    # Unterminated constructs end at the end of the file rather than looping.
    ("SELECT 'abc", 1, None),
    ("SELECT $$abc", 1, None),
    ('SELECT "abc', 1, None),
    ("/* abc", 0, None),
    # A `--` comment ends at CR as well as at LF, so a file with CR line
    # endings is not one comment from its first `--` to its last byte.
    ("-- c\rSELECT 1;\rSELECT 2;", 2, "SELECT 1;"),
]


def selftest():
    for text, count, needle in _CASES:
        got = split(text)
        assert len(got) == count, (text, count, got)
        if needle is not None:
            assert needle in got[0][0], (text, needle, got[0][0])
    # Every character accounted for, in order, for a sample with one of each.
    sample = ("-- lead\nSELECT 'a;b', E'c\\'d', $$e;f$$, \"g;h\" /* i; */;\n"
              "INSERT INTO t VALUES (1);\n")
    assert "".join(sample[i:j] for _, i, j, _ in scan(sample)) == sample
    lines = [ln for _, ln in split("\n\nSELECT 1;\nSELECT 2;\n\n-- x\nSELECT 3;")]
    assert lines == [3, 4, 7], lines
    # The statement is the source, verbatim: nothing is normalised on the way
    # through, because the server's error messages have to point at the file
    # the candidate wrote.
    stmt, _ = split("SELECT\n  1 /* two */;")[0]
    assert stmt == "SELECT\n  1 /* two */;", stmt
    return len(_CASES)


if __name__ == "__main__":
    print("sqlsplit: %d cases pass" % selftest())
