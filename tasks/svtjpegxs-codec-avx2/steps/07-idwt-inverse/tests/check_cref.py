#!/usr/bin/env python3
"""C-reference integrity gate, scoped to this step's dispatch table.

    check_cref.py <base-tree> <candidate-tree> <slot> [<slot> ...]

The candidate-facing instruction states the rule this gate enforces: "Do not
edit the C reference implementations; they define the correct output and are
what your speedup is measured against."  It was stated and enforced by
nothing.  It is grading-relevant because the step's per-slot ratio divides by
the CANDIDATE'S OWN portable reference, timed in the candidate's own binary:
the same translation unit as the installed implementation, so build layout
cancels between numerator and denominator.  A candidate who edits that
reference -- slowing it, or replacing it with a slower equivalent -- inflates
every ratio by the divergence, up to the harness's sabotage window.  The
reference is verifier-owned and fixed, and after this gate the candidate's
copy of it is read-only.

For each scoped slot, three statements about the C reference path:

  1. The candidate's dispatch entry still names the same C symbol as the base
     tree's entry.  A rerouted fallback is a replaced reference, however the
     symbols' files look.
  2. Every file the base tree's C symbol appears in -- the implementation and
     its header -- still exists in the candidate tree, so "removal" is the
     question asked rather than assumed away by resolution to nothing.
  3. Those files' TOKEN STREAM is unchanged.  Comments and whitespace are
     excluded, so a candidate whose only edit is a comment or a reformat has
     changed nothing the reference does, and failing them for it would be a
     false positive of exactly the kind this batch exists to remove.
     Everything else is compared token by token -- identifiers, numbers,
     string and character literals (their text included), and C punctuators
     longest-first so that `- -x` and `--x` are different token streams, as
     they are different code.  Any edit that can change what the reference
     computes or how fast it runs changes the stream and fails.

Slowing the C path by editing OTHER code it calls (a shared helper) is not
this gate's subject; that route stays bounded by the harness's C_DIVERGE
sabotage substitution.

The gate asserts it examined something: a scoped slot with no dispatch entry
in the base tree, an entry whose C argument is not a symbol, or a C symbol
that appears in no file of the base tree is reported and fails.  A scope-definition
error must not read as a pass, and neither must an empty comparison.
"""
import os
import re
import sys

SKIP_BASENAMES = {"encoder_dsp_rtcd.c", "decoder_dsp_rtcd.c", "common_dsp_rtcd.c"}

# C punctuators that are longer than one character, longest-first so a greedier
# one wins: `--x` must tokenise as `--` `x`, not as `-` `-` `x`.
_PUNCT = ["...", "<<=", ">>=", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
          "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "##"]
_TOKEN = re.compile(
    r"/\*.*?\*/"                    # comment: skipped
    r"|//[^\n]*"                    # comment to end of line: skipped
    r'|"(?:\\.|[^"\\])*"'           # string literal: one token
    r"|'(?:\\.|[^'\\])*'"           # character literal: one token
    r"|[A-Za-z_][A-Za-z0-9_]*"      # identifier / keyword
    r"|\d[\w.]*(?:[eEpP][+-]?\w+)?" # numeric literal (loose on suffixes)
    r"|" + "|".join(re.escape(p) for p in _PUNCT) +
    r"|.",                          # any other single character (punctuation)
    re.S,
)


def read(p):
    with open(p, encoding="utf-8", errors="surrogateescape") as f:
        return f.read()


def code_tokens(text):
    """The token stream of C source: comments and whitespace dropped.

    Literal text is kept (a changed string or character literal is a changed
    token); the tokenizer tracks C lexing for comments -- `a/*b` starts a
    comment, `a / *b` does not -- so nothing a compiler would parse as code
    can hide inside what this drops.
    """
    toks = []
    for m in _TOKEN.finditer(text):
        t = m.group(0)
        if t[0] == "/" and (t[1:2] == "*" or t[1:2] == "/"):
            continue
        if t.isspace() or t in ("\n", "\t", "\r", "\f", "\v"):
            continue
        toks.append(t)
    return toks


def find_set_call(tree, slot):
    """Return (dispatch slot name, argument list) for `slot`, or (None, None).

    A scoped name in FUNCS is normally the dispatch slot itself, but a step
    may scope one slot as two measured cases -- step 3 measures the deadzone
    and the uniform quantizer, which are two settings of one slot.  So an
    exact miss falls back to progressively shorter prefixes, dropping one
    trailing _-separated word at a time, and the slot that resolves is what
    gets inspected.  Same resolution as check_isa.py, for the same reason.
    """
    cand = [slot]
    parts = slot.split("_")
    while len(parts) > 1:
        parts = parts[:-1]
        cand.append("_".join(parts))
    for rel in ("Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c",
                "Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c",
                "Source/Lib/Common/Codec/common_dsp_rtcd.c"):
        p = os.path.join(tree, rel)
        if not os.path.exists(p):
            continue
        text = read(p)
        for name in cand:
            m = re.search(r"\bSET_\w+\s*\(\s*%s\s*,(.*?)\)\s*;" % re.escape(name), text, re.S)
            if m:
                return name, [a.strip() for a in m.group(1).split(",")]
    return None, None


def c_symbol(args):
    """The C reference a dispatch entry names, or None.

    Every SET_* macro in this tree takes the portable C implementation as its
    second parameter (`SET_ONLY_C(ptr, c)`, `SET_AVX2(ptr, c, avx2)`,
    `SET_AVX2_AVX512(ptr, c, avx2, avx512)`), so the reference is the first
    argument after the slot name.  Verified for every scoped slot of all
    eight steps against the shipped base tree, where each resolves to the
    `_c`-suffixed symbol its portable implementation is defined under.
    """
    if not args:
        return None
    sym = args[0].strip()
    if not sym or sym in ("NULL", "0") or not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", sym):
        return None
    return sym


def source_texts(tree, cache):
    """{relpath: text} for .c/.h under <tree>/Source, dispatch installers out."""
    if tree in cache:
        return cache[tree]
    texts = {}
    src = os.path.join(tree, "Source")
    for root, _dirs, files in os.walk(src):
        for f in files:
            if not f.endswith((".c", ".h")) or f in SKIP_BASENAMES:
                continue
            p = os.path.join(root, f)
            texts[os.path.relpath(p, tree)] = read(p)
    cache[tree] = texts
    return texts


def mention_files(tree, sym, cache):
    """<relpath> strings of files under <tree>/Source whose text names `sym`.

    This is the same textual resolution the envelope gate uses to find a
    slot's implementation files.  It resolves a C symbol to its definition
    site plus its declaration -- two files in this tree, verified -- without
    parsing C, and a file that only mentions a symbol has to be compared too;
    that is the conservative direction for a "do not edit" gate.
    """
    hits = []
    for rel, text in source_texts(tree, cache).items():
        if re.search(r"\b%s\b" % re.escape(sym), text):
            hits.append(rel)
    return sorted(hits)


def main():
    base, cand, slots = sys.argv[1], sys.argv[2], sys.argv[3:]
    if not slots:
        print("  no slots given: nothing to check")
        return 1

    bcache, ccache = {}, {}
    bad = 0
    checked_slots = 0
    compared = set()
    for slot in slots:
        _bname, bargs = find_set_call(base, slot)
        if bargs is None:
            print("  %s: no dispatch entry in the base tree" % slot)
            bad = 1
            continue
        c_sym = c_symbol(bargs)
        if c_sym is None:
            print("  %s: dispatch entry names no C reference (%s)" % (slot, bargs))
            bad = 1
            continue

        _cname, cargs = find_set_call(cand, slot)
        if cargs is None:
            print("  %s: no dispatch entry in the candidate tree" % slot)
            bad = 1
            continue
        cand_sym = c_symbol(cargs)
        if cand_sym != c_sym:
            print("  %s: C reference rerouted: base entry names %s, candidate entry names %s"
                  % (slot, c_sym, cand_sym))
            bad = 1
            continue
        checked_slots += 1

        files = mention_files(base, c_sym, bcache)
        if not files:
            print("  %s: %s appears in no file of the base tree -- scope is not derivable"
                  % (slot, c_sym))
            bad = 1
            continue
        for rel in files:
            compared.add(rel)
            cp = os.path.join(cand, rel)
            if not os.path.isfile(cp):
                print("  %s: reference file removed by the candidate: %s" % (slot, rel))
                bad = 1
                continue
            if code_tokens(read(os.path.join(base, rel))) != code_tokens(read(cp)):
                print("  %s: reference file edited by the candidate: %s" % (slot, rel))
                bad = 1

    print("  %d slot(s), %d reference file(s) checked against the base tree; %s"
          % (checked_slots, len(compared), "all unchanged" if not bad else "VIOLATION"))
    return bad


if __name__ == "__main__":
    sys.exit(main())
