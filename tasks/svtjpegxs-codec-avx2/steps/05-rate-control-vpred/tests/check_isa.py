#!/usr/bin/env python3
"""Instruction-set envelope gate, scoped to this step and to the candidate's changes.

    check_isa.py <base-tree> <candidate-tree> <ceiling> <slot> [<slot> ...]

The ceiling is AVX2 / 256-bit for both sides: the reference implementations the
task is calibrated against are AVX2, and the grading host has no AVX-512.  A
candidate reaching past the ceiling would not be a peer of the reference, and on
this host AVX-512 code is not even reachable, so it must not silently be graded
as if it were.

The gate answers two separate questions, in this order.

  1. WHICH FILES DOES THIS STEP OWN?  Through the dispatch layer, as before: for
     each scoped slot, the rtcd entry names symbols, and the files under Source/
     that define those symbols -- the per-ISA implementations *and* the portable C
     reference the entry falls back to -- are this step's files.  Scoping is what
     makes the gate correct rather than merely strict: the candidate tree is
     cumulative, so at a late step the diff against the task base contains every
     earlier step's work too, and a gate that read the whole diff would charge
     late steps for early work.

  2. DID THE CANDIDATE TOUCH THEM?  A tree that changed nothing in this step's
     scope has changed no behaviour in it, so no envelope can have been violated;
     the gate says so, with the counts, and passes.  A tree that did touch its
     files is checked exactly as before -- and this is the part that keeps the
     gate honest, because the file set is resolved from BOTH trees and includes
     the portable C reference, not only the per-ISA symbols the candidate's
     dispatch happens to name.  A candidate that writes AVX-512 into a file this
     step owns, whether the dispatch names it or not, is inspected.

     The earlier version resolved its file set from the candidate's installed
     per-ISA symbols alone, so a candidate that left the dispatch on the C path
     was never inspected at all, and a no-op passed only as a side effect of that
     hole instead of as a stated rule.  Both halves of that are now explicit:
     nothing in scope is left unexamined, and a vacuous pass is printed rather
     than implied.

  3. THE DISPATCH TABLES ARE CHECKED SEPARATELY.  They name every slot, so the
     symbol-based scope above cannot include them -- they would put themselves
     in every step's file set -- and they are the one file a candidate must edit
     to install anything (the SET_* macro that points the slot at its new
     implementation).  They are therefore read on their own, and only for
     AVX-512 tokens the BASE tree's copy of the same file does not already
     contain: the gate must catch what the candidate wrote, not what the tree
     shipped with.  The same ceiling tokens and the same comment-blanking apply.
     Without this, AVX-512 written into a dispatch table -- the one file this
     step's work runs through that the scope scan cannot see -- was unimpeded.

The gate asserts it examined something.  It fails if a scoped slot resolves to no
dispatch entry, if the base tree -- which is verifier-owned and fixed -- yields no
files for the step's slots at all, and it reports how many files it read in each
branch.  It also reports how many dispatch tables it compared.
"""
import os
import re
import sys

AVX512_TOKENS = ("_mm512_", "__m512", "-mavx512")

SKIP_BASENAMES = {"encoder_dsp_rtcd.c", "decoder_dsp_rtcd.c", "common_dsp_rtcd.c"}


def read(p):
    with open(p, encoding="utf-8", errors="surrogateescape") as f:
        return f.read()


def read_bytes(p):
    with open(p, "rb") as f:
        return f.read()


def strip_comments(text):
    """Blank out C comments and literals, keeping offsets stable.

    The gate must judge code, not prose.  A first version scanned the raw text
    and failed the reference solution on the word `__m512i` inside a comment in
    its own AVX2 file -- the exact defect the build procedure records for an
    intrinsics ban that matched a comment inside assembly.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "\"'":
            q, i = c, i + 1
            while i < n and text[i] != q:
                if text[i] == "\\":
                    i += 1
                if i < n:
                    out[i] = " "
                i += 1
            i += 1
        else:
            i += 1
    return "".join(out)


def find_set_call(tree, slot):
    """Return (dispatch slot name, argument list) for `slot`, or (None, None).

    A scoped name in FUNCS is normally the dispatch slot itself, but a step may
    scope one slot as two measured cases -- step 3 measures the deadzone and the
    uniform quantizer, which are two settings of one slot.  So an exact miss
    falls back to progressively shorter prefixes, dropping one trailing
    _-separated word at a time, and the slot that resolves is what gets
    inspected.  Without this the gate reported "no dispatch entry" for an honest
    oracle and zeroed its correctness.
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


def implementation_files(tree, symbols):
    """Files under <tree>/Source that define one of `symbols`."""
    hits = set()
    src = os.path.join(tree, "Source")
    for root, _dirs, files in os.walk(src):
        for f in files:
            if not f.endswith((".c", ".h")):
                continue
            if f in SKIP_BASENAMES:
                continue
            p = os.path.join(root, f)
            text = read(p)
            for s in symbols:
                if re.search(r"\b%s\b" % re.escape(s), text):
                    hits.add(p)
                    break
    return sorted(hits)


def slot_symbols(args, resolved):
    """The symbols a dispatch entry names, the portable-C fallback included.

    The C reference is deliberately NOT filtered out.  It is the file a candidate
    is most likely to edit in place, and it is exactly what the earlier version
    skipped: every symbol ending in `_c` was discarded, so a step whose dispatch
    the candidate left alone was never inspected.  A dispatch entry always names
    at least the C fallback, so this also keeps the per-slot file set non-empty
    even in a tree where nothing is installed.
    """
    return [a for a in args if a and a not in ("NULL", "0") and a != resolved]


def scoped_files(tree, slots):
    """This step's files, as <relpath> strings, resolved through `tree`'s rtcd."""
    files = set()
    for slot in slots:
        _resolved, args = find_set_call(tree, slot)
        if args is None:
            continue
        syms = slot_symbols(args, _resolved)
        for p in implementation_files(tree, syms):
            files.add(os.path.relpath(p, tree))
    return files


DISPATCH_FILES = (
    "Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c",
    "Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c",
    "Source/Lib/Common/Codec/common_dsp_rtcd.c",
)


def dispatch_violations(base, cand):
    """AVX-512 tokens the candidate put INTO a dispatch table.

    Only tokens absent from the base tree's copy of the same file count: the
    tables are verifier-shipped content that the candidate also edits, and the
    gate must not charge a candidate for what was already there.  A candidate
    only ever adds SET_* lines here, so an honest edit adds no token at all.
    """
    bad = 0
    compared = 0
    for rel in DISPATCH_FILES:
        c = os.path.join(cand, rel)
        if not os.path.isfile(c):
            continue  # a deleted dispatch table is the build's business
        compared += 1
        b = os.path.join(base, rel)
        base_text = strip_comments(read(b)) if os.path.isfile(b) else ""
        toks = [t for t in AVX512_TOKENS
                if t in strip_comments(read(c)) and t not in base_text]
        if toks:
            print("  %s uses %s" % (rel, ",".join(toks)))
            bad = 1
    print("  dispatch tables: %d compared against the base tree; %s"
          % (compared, "ceiling respected" if not bad else "ENVELOPE VIOLATED"))
    return bad


def scope_report(base, cand, slots):
    """(scope, changed) -- relpaths, candidate-side scope and what the candidate moved."""
    scope = scoped_files(cand, slots) | scoped_files(base, slots)
    changed = []
    for rel in sorted(scope):
        b, c = os.path.join(base, rel), os.path.join(cand, rel)
        bb = read_bytes(b) if os.path.isfile(b) else None
        cc = read_bytes(c) if os.path.isfile(c) else None
        if bb != cc:
            changed.append(rel)
    return scope, changed


def main():
    base, cand, ceiling, slots = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    if ceiling != "avx2":
        print("  unknown ceiling %r" % ceiling)
        return 1

    checked = 0
    missing = []
    for slot in slots:
        _resolved, args = find_set_call(cand, slot)
        if args is None:
            missing.append(slot)
            continue
        checked += 1

    # A scoped name that resolves to nothing is a scope-definition error, not a
    # candidate one -- but a gate that silently inspected nothing is worse, so
    # it is still a failure.
    if missing:
        print("  scoped names with no dispatch entry in the candidate tree: %s" % ", ".join(missing))
        return 1
    print("  dispatch entries examined: %d of %d scoped slots" % (checked, len(slots)))

    # Scope-definition error rather than candidate fault: the base tree is fixed and
    # verifier-owned, so every step's slots must resolve to files in it.  This is
    # the gate that stops the vacuous pass below from masking a broken scope.
    base_scope = scoped_files(base, slots)
    if not base_scope:
        print("  no files resolve for this step's slots in the base tree: scope is not derivable")
        return 1

    scope, changed = scope_report(base, cand, slots)
    dispatch_bad = dispatch_violations(base, cand)
    if not changed:
        print("  envelope: %d of %d scoped files unchanged by the candidate -- check skipped"
              % (len(scope), len(scope)))
        return dispatch_bad

    print("  envelope: %d of %d scoped files changed by the candidate -- checking them"
          % (len(changed), len(scope)))
    for rel in changed:
        print("    changed: %s" % rel)

    bad = 0
    inspected = 0
    for rel in sorted(scope):
        p = os.path.join(cand, rel)
        if not os.path.isfile(p):
            continue  # the candidate deleted an in-scope file; the build says so
        inspected += 1
        text = strip_comments(read(p))
        toks = [t for t in AVX512_TOKENS if t in text]
        if toks:
            print("  %s uses %s" % (rel, ",".join(toks)))
            bad = 1
    print("  inspected %d file(s) implementing this step's slots (%d added or modified); %s"
          % (inspected, len(changed), "ceiling respected" if not bad else "ENVELOPE VIOLATED"))
    return bad or dispatch_bad


if __name__ == "__main__":
    sys.exit(main())
