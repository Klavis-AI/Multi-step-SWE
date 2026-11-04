"""Minimal text-format reader for protoc's own --decode output.

This module never produces an expectation. It only parses the text rendering of
a FileDescriptorSet that `protoc --decode=google.protobuf.FileDescriptorSet`
emitted, so the corpus's codec *inputs* are derived mechanically from real
descriptors instead of being hand-written.
"""

import re

_TOKEN_RE = re.compile(
    r"""
      (?P<ws>\s+)
    | (?P<comment>\#[^\n]*)
    | (?P<string>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')
    | (?P<punct>[{}\[\]<>:,;])
    | (?P<bare>[^\s{}\[\]<>:,;"']+)
    """,
    re.VERBOSE,
)

_ESCAPES = {"a": "\a", "b": "\b", "f": "\f", "n": "\n", "r": "\r",
            "t": "\t", "v": "\v", "\\": "\\", "'": "'", '"': '"', "?": "?"}


def _tokenize(text):
    tokens = []
    pos, end = 0, len(text)
    while pos < end:
        m = _TOKEN_RE.match(text, pos)
        if m is None:
            raise ValueError("cannot tokenize at %d: %r" % (pos, text[pos:pos + 40]))
        pos = m.end()
        if m.lastgroup not in ("ws", "comment"):
            tokens.append((m.lastgroup, m.group()))
    return tokens


def unquote(tok):
    body, out, i, n = tok[1:-1], [], 0, len(tok) - 2
    while i < n:
        ch = body[i]
        if ch != "\\":
            out.append(ch)
            i += 1
            continue
        i += 1
        esc = body[i]
        if esc in _ESCAPES:
            out.append(_ESCAPES[esc])
            i += 1
        elif esc == "x":
            i += 1
            j = i
            while j < n and j < i + 2 and body[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(chr(int(body[i:j], 16)))
            i = j
        elif esc in "01234567":
            j = i
            while j < n and j < i + 3 and body[j] in "01234567":
                j += 1
            out.append(chr(int(body[i:j], 8)))
            i = j
        else:
            out.append(esc)
            i += 1
    return "".join(out)


class Msg(dict):
    """Parsed text-format message: field name -> list of values."""

    def get1(self, name, default=None):
        v = self.get(name)
        return v[0] if v else default

    def getall(self, name):
        return self.get(name, [])


def _parse_message(tokens, i, terminator):
    msg = Msg()
    while i < len(tokens):
        kind, tok = tokens[i]
        if kind == "punct" and tok == terminator:
            return msg, i + 1
        if kind == "punct" and tok in ",;":
            i += 1
            continue
        if kind != "bare":
            raise ValueError("expected field name, got %r" % (tok,))
        name = tok
        i += 1
        if i < len(tokens) and tokens[i] == ("punct", ":"):
            value, i = _parse_value(tokens, i + 1)
            msg.setdefault(name, []).append(value)
        elif i < len(tokens) and tokens[i][0] == "punct" and tokens[i][1] in "{<":
            close = "}" if tokens[i][1] == "{" else ">"
            sub, i = _parse_message(tokens, i + 1, close)
            msg.setdefault(name, []).append(sub)
        elif i < len(tokens) and tokens[i] == ("punct", "["):
            i += 1
            while True:
                value, i = _parse_value(tokens, i)
                msg.setdefault(name, []).append(value)
                if tokens[i] == ("punct", ","):
                    i += 1
                elif tokens[i] == ("punct", "]"):
                    i += 1
                    break
                else:
                    raise ValueError("bad list element %r" % (tokens[i],))
        else:
            raise ValueError("bad field %r" % (name,))
    if terminator is not None:
        raise ValueError("unterminated message")
    return msg, i


def _parse_value(tokens, i):
    kind, tok = tokens[i]
    if kind == "string":
        parts = []
        while i < len(tokens) and tokens[i][0] == "string":
            parts.append(unquote(tokens[i][1]))
            i += 1
        return "".join(parts), i
    if kind == "punct" and tok in "{<":
        return _parse_message(tokens, i + 1, "}" if tok == "{" else ">")
    if kind == "bare":
        return tok, i + 1
    raise ValueError("bad value token %r" % (tok,))


def parse(text):
    tokens = _tokenize(text)
    msg, i = _parse_message(tokens, 0, None)
    if i != len(tokens):
        raise ValueError("trailing tokens")
    return msg
