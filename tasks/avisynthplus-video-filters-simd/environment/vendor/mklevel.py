#!/usr/bin/env python3
"""Build one rung of the SIMD ladder from a pinned AviSynth+ checkout.

    mklevel.py <pin-tree> <out-tree> <row> [<row> ...]

A "row" is one filter family of the ladder, identified by the `*/intel/*` file
set that implements it.  The output tree is the pin with

  * every `*/intel/*` and `*/aarch64/*` source removed except the selected
    rows', and
  * every preprocessor conditional whose condition mentions `INTEL_INTRINSICS`
    evaluated: the `#ifdef` branch is kept when every row it needs is selected,
    and cut away (leaving the `#else`/`#elif` branch) otherwise.

Ownership is by *symbol*, not by file: a dispatch block living in one family's
reference file but calling another family's kernel belongs to the family that
owns the kernel, so each rung of the ladder restores exactly its own work.

Rows are cumulative and the dependency order is not free -- a row whose intel
sources call another row's kernels cannot be selected without it.  `ORDER`
below records the constraint and `--check-order` verifies a selection against
it.
"""

import fnmatch
import glob
import os
import re
import shutil
import sys

# --- the ten rows ----------------------------------------------------------
# Each entry is the set of `*/intel/*` files that constitutes the row; these
# are exactly the files the seed's step table names.
ROWS = {
    1: ["avs_core/filters/overlay/intel/masked_rowprep_sse41.*",
        "avs_core/filters/overlay/intel/masked_rowprep_avx2.*",
        "avs_core/filters/overlay/intel/masked_rowprep_avx2_impl.h",
        "avs_core/filters/overlay/intel/masked_merge_sse41_impl.hpp",
        "avs_core/filters/overlay/intel/masked_merge_avx2_impl.hpp"],
    2: ["avs_core/filters/overlay/intel/blend_common_sse.*",
        "avs_core/filters/overlay/intel/blend_common_avx2.*",
        "avs_core/filters/overlay/intel/OF_multiply_sse.*",
        "avs_core/filters/overlay/intel/OF_multiply_avx2.*"],
    3: ["avs_core/filters/intel/focus_sse.*",
        "avs_core/filters/intel/focus_avx2.*"],
    4: ["avs_core/filters/intel/layer_sse.*",
        "avs_core/filters/intel/layer_sse41.*",
        "avs_core/filters/intel/layer_avx2.*"],
    5: ["avs_core/filters/intel/turn_sse.*",
        "avs_core/filters/intel/turn_avx2.*"],
    6: ["avs_core/filters/intel/planeswap_sse.*",
        "avs_core/filters/intel/planeswap_avx2.*",
        "avs_core/filters/intel/greyscale_sse.*"],
    # The family ships an AVX-512 body; the target host does not execute
    # AVX-512, so that body is out of scope for the ladder and is not carried at
    # any level.  The family is measured on its sse41 and avx2 bodies, which is
    # also the pairing the correctness gate runs against.
    7: ["avs_core/filters/intel/getalpharect_sse41.*",
        "avs_core/filters/intel/getalpharect_avx2.*"],
    8: ["avs_core/filters/intel/merge_sse.*",
        "avs_core/filters/intel/merge_avx2.*"],
    9: ["avs_core/filters/intel/limiter_sse.*",
        "avs_core/filters/intel/text-overlay_sse.*",
        "avs_core/filters/conditional/intel/conditional_functions_sse.*"],
    10: ["avs_core/convert/intel/convert_audio_sse.*",
         "avs_core/convert/intel/convert_audio_avx2.*"],
}

# row -> rows it must not be selected without (a row's intel sources call the
# kernels of, or #include the headers of, the rows listed here)
# Row 9's only link to row 7 was an include gate it shared with rows 1 and 7;
# include-only gates are now rewritten per-include, so row 9 no longer needs
# row 7 selected -- and row 7 is not part of the ladder at all.  Its links to
# row 1 (the masked row-prep kernels text-overlay calls) and to row 3 (the SAD
# kernels its conditional functions call) are real call sites and stay.
ORDER = {1: [], 2: [1], 3: [], 4: [1, 2], 5: [], 6: [],
         7: [], 8: [2], 9: [1, 3], 10: []}

# Files whose INTEL_INTRINSICS blocks are infrastructure, not kernels, and are
# kept verbatim at every rung.
#
# Removing a family can remove something another family's *data*: internal.h
# carries the SSE2 compatibility shims the vectorized kernels are written
# against -- `_MM_MULLO_EPI32` and `_MM_PACKUS_EPI32` are its own replacements
# for SSE4.1 intrinsics that upstream has to simulate on SSE2 -- and it is
# included by every one of them.  The stripper's ownership rule cannot see that
# a header is used by files that are themselves being deleted, so an explicit
# list is the honest instrument.  Neither file contains any family's kernel.
KEEP_FILES = {
    "avs_core/core/internal.h",
}

# Files deleted from every rung.  They are upstream's own release notes and
# changelogs, and they describe the removed work: which kernels existed, the
# magic-division exactness they were written for, and the right-edge and
# 16-bit-with-partial-opacity bugs they had to avoid.  No code, but a verified
# construction and a pitfall list.  They are deleted rather than neutralised
# because they are documentation for a version of the tree that no longer
# exists here, and nothing references them.
DROP_FILES = [
    "distrib/Readme/readme_history.txt",
    "distrib/docs/english/source/avisynthdoc/corefilters/changelist376.rst",
    "distrib/docs/english/source/avisynthdoc/corefilters/changelist25.rst",
]

# Whole-line comments that name a removed kernel or the directory it lived in
# are dropped.  Upstream's own source is one `git clone` away (the network is
# open and cannot be closed), so these leak nothing that is not already
# reachable -- but they name exactly which functions the milestone is about and
# where they would live, and a tree-local hint is the *easier* route.  Whole-line
# only: a comment that shares a line with code is left alone.
SCRUB_RE = re.compile(r'^\s*//.*(avx2|avx512|sse2|sse41|ssse3|neon|intel/)',
                      re.IGNORECASE)


def scrub(text):
    if "//" not in text:
        return text
    out = [ln for ln in text.splitlines(keepends=True) if not SCRUB_RE.match(ln)]
    return "".join(out)


COND_RE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')
INC_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')


def load_symbol_map(path):
    """symbol -> set(rows), from row_symbol_map.tsv."""
    m = {}
    with open(path) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("symbol\t"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            sym, rows = parts[0], parts[1]
            m.setdefault(sym, set()).update(int(r) for r in rows.split(",") if r)
    return m


def row_of_intel_path(path):
    """The family that owns an intel source, or 0 for one outside the ladder.

    Row 0 is the families the ladder deliberately excludes.  It is never
    selected, so a conditional that pulls in one of those sources is cut at
    every rung -- which is what has to happen, because the file it names does
    not exist at any rung.  Without this the include resolves to no family at
    all, the block looks like family-free scaffolding, and it survives into a
    tree that cannot compile.
    """
    path = path.replace(os.sep, "/")
    for row, globs in ROWS.items():
        for g in globs:
            if glob.fnmatch.fnmatch(path, g):
                return row
    if "/intel/" in "/" + path or "/aarch64/" in "/" + path:
        return 0
    return None


def resolve_include(inc, relpath):
    """Resolve an #include target to a repo-relative path (best effort)."""
    target = inc.replace("\\", "/")
    cand = os.path.normpath(os.path.join(os.path.dirname(relpath), target))
    return cand


# Files that belong to the excluded families' portable reference code and are
# taken all the way back to their C-only form, whatever the general pass would
# have decided.  `resample.cpp` is one file with three tiers of vectorization
# interleaved -- Intel, NEON and AVX-512 -- plus blocks whose `#else` arm is
# C++ that must survive; a general rule kept getting one of the combinations
# wrong, and building a rung is the only test that catches it.
DEVECTORIZE = {
    "avs_core/filters/resample.cpp",
}


def devectorize(text):
    """Keep only the fallback arm of every INTEL/NEON conditional.

    Outermost conditional first, then rescan: taking the outermost one means no
    other block's line numbers are disturbed, and re-scanning means the result
    does not depend on how the tiers were nested.

    Only a real `#else` arm is a fallback.  An `#elif defined(NEON_INTRINSICS)`
    arm is *another* vectorized tier, not a C path -- treating it as the
    fallback is how `turn_left_rgb32_neon` ended up in a tree whose aarch64
    sources do not exist.
    """
    for _ in range(400):
        lines = text.splitlines(keepends=True)
        hit = None
        stack = []
        for idx, ln in enumerate(lines):
            m = COND_RE.match(ln)
            if not m:
                continue
            kind, rest = m.group(1), m.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                cond = rest.strip()
                blk = {"start": idx, "cond": cond, "elif_line": None,
                       "else_line": None, "endif": None}
                inner = any("INTEL_INTRINSICS" in x["cond"] or "NEON_INTRINSICS" in x["cond"]
                            for x in stack)
                if (hit is None and not inner
                        and ("INTEL_INTRINSICS" in cond or "NEON_INTRINSICS" in cond)):
                    hit = blk
                stack.append(blk)
            elif stack:
                top = stack[-1]
                if kind == "elif" and top["elif_line"] is None and top["else_line"] is None:
                    top["elif_line"] = idx
                elif kind == "else" and top["else_line"] is None:
                    top["else_line"] = idx
                elif kind == "endif":
                    b = stack.pop()
                    b["endif"] = idx
                    if b is hit:
                        alt = b["else_line"]
                        if alt is not None:
                            lines[b["start"]:b["endif"] + 1] = lines[alt + 1:b["endif"]]
                        else:
                            del lines[b["start"]:b["endif"] + 1]
                        text = "".join(lines)
                        break
            if hit is not None and stack and idx > 0 and hit["endif"] is not None:
                break
        else:
            break
        continue
    return text


def drop_foreign_tiers(text, symmap, rel):
    """Remove every NEON and AVX-512 conditional, innermost first.

    Both are always cut -- the aarch64 sources do not exist at any rung and the
    target host does not execute AVX-512 -- and removing them before the Intel
    pass is what keeps the Intel pass simple: these blocks are *nested* inside
    Intel blocks in `resample.cpp`, and a nested block is invisible to the
    outer-block renderer, which decides a whole line range at once.  Innermost
    first, so removing one never invalidates another's line numbers.

    Blocks belonging to the excluded families (row 0) go the same way and for
    the same reason: they call kernels whose sources are deleted at every rung,
    and one of them sits inside a kept Intel block in `resample.cpp`, where the
    outer renderer would otherwise emit it verbatim.
    """
    for _ in range(40):
        lines = text.splitlines(keepends=True)
        stack, targets = [], []
        for idx, ln in enumerate(lines):
            m = COND_RE.match(ln)
            if not m:
                continue
            kind, rest = m.group(1), m.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                stack.append({"start": idx, "cond": rest.strip(),
                              "elif_line": None, "else_line": None})
            elif stack:
                top = stack[-1]
                if kind == "elif" and top["elif_line"] is None and top["else_line"] is None:
                    top["elif_line"] = idx
                elif kind == "else" and top["else_line"] is None:
                    top["else_line"] = idx
                elif kind == "endif":
                    b = stack.pop()
                    b["end"] = idx
                    c = b["cond"]
                    if "AVX512" in c or "NEON_INTRINSICS" in c:
                        targets.append(b)
                    elif "INTEL_INTRINSICS" in c:
                        opens = [i for i in (b["elif_line"], b["else_line"])
                                 if i is not None]
                        first = min(opens) if opens else b["end"]
                        body = "".join(lines[b["start"] + 1:first])
                        # row 0 only: a block that also serves a selected
                        # family is a real dependency and stays for the Intel
                        # pass to decide.
                        if branch_rows(body, symmap, rel) == {0}:
                            targets.append(b)
        if not targets:
            break
        # innermost = the one that contains no other target
        clip = min(targets, key=lambda b: b["end"] - b["start"])
        opens = [i for i in (clip["elif_line"], clip["else_line"]) if i is not None]
        if opens:
            first = min(opens)
            if clip["elif_line"] is not None:
                lines[clip["elif_line"]] = re.sub(r'^\s*#\s*elif\b', '#if',
                                                  lines[clip["elif_line"]], count=1)
                del lines[clip["start"]:clip["elif_line"]]
            else:
                body = lines[clip["else_line"] + 1:clip["end"]]
                lines[clip["start"]:clip["end"] + 1] = body
        else:
            del lines[clip["start"]:clip["end"] + 1]
        text = "".join(lines)
    return text


def strip_comments(text):
    """Remove C and C++ comments before looking for symbol references.

    Ownership is decided by what a branch *calls*.  A branch that merely
    mentions a symbol in a comment or in a benchmark table is not the family
    that owns it -- and `filters/resample.cpp` keeps exactly that: an excluded
    resampler block whose body quotes a performance table naming the rotation
    kernels.  Scanning the raw text made that block look like rotation code, so
    it was restored on every rung that has rotation and the tree stopped
    compiling.
    """
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def branch_rows(text, symmap, relpath):
    """Rows referenced by a branch's text: symbols called, and the row that owns
    any intel header the branch pulls in (an include of an intel header is the
    branch's dependency just as a call to one of its kernels is)."""
    need = set()
    code = []
    for ln in text.splitlines():
        inc = INC_RE.match(ln)
        if inc:
            r = row_of_intel_path(resolve_include(inc.group(1), relpath))
            if r is not None:
                need.add(r)   # 0 is the excluded families, and it is falsy
            continue
        code.append(ln)
    src = strip_comments("\n".join(code))
    for sym, rows in symmap.items():
        if re.search(r'\b' + re.escape(sym) + r'\b', src):
            need |= rows
    return need


DECL_RE = re.compile(
    r'\b(?:const\s+|static\s+|AVS_FORCEINLINE\s+)*'
    r'(?:bool|int|int64_t|uint32_t|uint64_t|size_t|float|double|BYTE|uint8_t|'
    r'uint16_t|auto|void|int32_t)\s*\*?\s+(\w+)\s*=')

# Lines a file may carry besides one INTEL_INTRINSICS block and still be that
# block's own translation unit: blanks, comments, and the preprocessor lines a
# unit needs to include its own headers and keep a header guard.
UNIT_NOISE_RE = re.compile(
    r'^\s*(?:#\s*(?:include|pragma|define|undef|ifndef|endif)\b|//|/\*|\*)')


def selfcontained_intel_unit(lines, outer):
    """True when a file is nothing but unattributable INTEL_INTRINSICS blocks.

    The idiom for a vectorized translation unit is a conditional around the
    whole body:

        #if defined(INTEL_INTRINSICS) || defined(__AVX2__)
        ... the unit's kernels ...
        #endif

    A file a *candidate* wrote in that shape cannot be attributed by the symbol
    map: its symbols are the candidate's own and name no row of
    row_symbol_map.tsv, so the cut decides its block names no family and guts
    it -- leaving a translation unit that defines nothing while the call sites
    that reached it are still there.  The tree then no longer links, and the
    non-regression gate has no baseline to build and skips.  Such a file is
    left alone instead: the tree still builds, and the gate's own fingerprint
    check then reports honestly that the cut removed nothing.

    Deliberately narrow.  Every top-level conditional in the file must be an
    armless, unattributable INTEL_INTRINSICS block -- no NEON or AVX-512
    construct, no `#elif`/`#else` arm (where the fallback arm is the right
    thing to keep and gutting the Intel arm alone is exactly right), nothing
    the row map owns -- and every line outside those blocks must be inert
    (blank, a comment, or an include/guard/pragma).  A file with any other
    shape is not the candidate's own self-contained unit and is cut as before.
    """
    if not outer:
        return False
    covered = set()
    for blk in outer:
        if blk["force_cut"] or blk["need"]:
            return False
        if blk["elif_line"] is not None or blk["else_line"] is not None:
            return False
        if "INTEL_INTRINSICS" not in blk["cond"] or "NEON_INTRINSICS" in blk["cond"]:
            return False
        if "AVX512" in blk["cond"] or "AVX512" in blk["if_body"]:
            return False
        if "NEON_INTRINSICS" in blk["if_body"]:
            return False
        covered.update(range(blk["start"], blk["end"] + 1))
    for i, ln in enumerate(lines):
        if i in covered:
            continue
        if not ln.strip() or UNIT_NOISE_RE.match(ln):
            continue
        return False
    return True


def process(path, rel, rows, symmap, log, keep_fn=None, spare_units=False,
            scrub_output=True):
    """Rewrite one source file's INTEL_INTRINSICS conditionals in place.

    A conditional whose taken branch calls a family's kernels (or pulls in that
    family's headers) belongs to that family, and is kept only when every
    family it needs is selected.  A conditional that names no family at all is
    *scaffolding*: the AviSynth idiom is

        #ifdef INTEL_INTRINSICS
          const bool use_avx2 = (cpuFlags & CPUF_AVX2) != 0;
        #endif
        ... later ...
        #ifdef INTEL_INTRINSICS
          if (use_avx2) kernel_avx2(...); else
        #endif
          { c_body(...); }

    and cutting the first block alone leaves the second referring to a variable
    that no longer exists.  Such a block is therefore kept when a name it
    declares survives in the rewritten file -- which is exactly when something
    still uses it -- and dropped otherwise.  The check runs against the
    rewritten text, not the original, so at a rung that has neither block the
    scaffolding goes too.
    """
    if rel.replace(os.sep, "/") in KEEP_FILES:
        return 0
    with open(path, encoding="utf-8", errors="surrogateescape") as fh:
        orig = fh.read()
    text = orig
    if rel.replace(os.sep, "/") in DEVECTORIZE:
        text = devectorize(text)
    else:
        text = drop_foreign_tiers(text, symmap, rel)
    if "INTEL_INTRINSICS" not in text and "NEON_INTRINSICS" not in text:
        out = scrub(text) if scrub_output else text
        if out != orig:
            # Write even when the only change came from drop_foreign_tiers: the
            # early return used to discard it, which left `field.cpp` including
            # `intel/resample_sse.h` at every rung in a tree where that file
            # does not exist.
            with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
                fh.write(out)
            return 1
        return 0
    lines = text.splitlines(keepends=True)
    # --- pass 1: locate every conditional, keep the INTEL_INTRINSICS ones ---
    stack = []
    blocks = []
    for idx, ln in enumerate(lines):
        m = COND_RE.match(ln)
        if not m:
            continue
        kind, rest = m.group(1), m.group(2)
        if kind in ("if", "ifdef", "ifndef"):
            stack.append({"start": idx, "cond": rest.strip(), "elif_line": None,
                          "else_line": None, "elif_text": None})
        elif stack:
            top = stack[-1]
            if kind == "elif" and top["elif_line"] is None and top["else_line"] is None:
                top["elif_line"] = idx
                top["elif_text"] = ln
            elif kind == "else" and top["else_line"] is None:
                top["else_line"] = idx
            elif kind == "endif":
                blk = stack.pop()
                if ("INTEL_INTRINSICS" in blk["cond"]
                        or "NEON_INTRINSICS" in blk["cond"]):
                    blk["end"] = idx
                    blocks.append(blk)
    if not blocks:
        return 0
    blocks.sort(key=lambda b: b["start"])
    outer = []
    for b in blocks:
        if outer and b["start"] > outer[-1]["start"] and b["end"] < outer[-1]["end"]:
            continue
        outer.append(b)

    def arms(b):
        opens = [i for i in (b["elif_line"], b["else_line"]) if i is not None]
        return (min(opens) if opens else b["end"]), opens

    def include_lines(b):
        """If a block is nothing but `#include`s, return them with their owners.

        An include gate routinely pulls in several families at once -- the
        rotation family's headers and the excluded resampler's arrive in the
        same `#ifdef INTEL_INTRINSICS` block in resample.cpp.  Keeping or
        cutting it whole is wrong either way: kept, it names files that were
        deleted; cut, the family that *is* selected loses its declarations.  So
        an include-only block is rewritten to the includes whose family is
        selected, and to nothing when none is.
        """
        out = []
        stack = []
        for ln in b["if_body"].splitlines(keepends=True):
            m = COND_RE.match(ln)
            if m:
                kind, rest = m.group(1), m.group(2)
                if kind in ("if", "ifdef", "ifndef"):
                    stack.append(rest.strip())
                elif kind == "endif":
                    if stack:
                        stack.pop()
                continue
            if not ln.strip() or ln.strip().startswith("//"):
                continue
            inc = INC_RE.match(ln)
            if not inc:
                return None
            if any("AVX512" in c for c in stack):
                continue
            out.append((ln, row_of_intel_path(resolve_include(inc.group(1), rel))))
        return out

    def decide(b):
        if keep_fn is not None:
            b["keep"] = keep_fn(b["need"]) and not b["force_cut"]
        elif b["includes"] is not None:
            # an include gate is kept per-include, not as a unit
            b["keep"] = any(own is not None and own in rows
                            for _, own in b["includes"])
        else:
            b["keep"] = (bool(b["need"]) and b["need"].issubset(rows)
                         and not b["force_cut"])

    for b in outer:
        first_alt, _ = arms(b)
        b["if_body"] = "".join(lines[b["start"] + 1:first_alt])
        if "NEON_INTRINSICS" in b["cond"]:
            # The aarch64 sources are removed at every rung, so a NEON dispatch
            # site names kernels that do not exist.  It is inert on this host,
            # but it is a signature list and a dispatch skeleton for the very
            # work the milestone asks for.
            b["need"] = set()
            b["force_cut"] = True
        elif "INTEL_INTRINSICS_AVX512" in b["cond"]:
            b["need"] = set()
            b["force_cut"] = True
        else:
            b["need"] = branch_rows(b["if_body"], symmap, rel)
            b["force_cut"] = False
        b["decls"] = set(DECL_RE.findall(b["if_body"]))
        b["includes"] = include_lines(b) if not arms(b)[1] else None
        decide(b)

    def neon_alt(b):
        """True when this block's alternative arm is the NEON one.

        The AviSynth idiom is `#ifdef INTEL_INTRINSICS ... #elif defined(
        NEON_INTRINSICS) ... #else ... #endif`.  Cutting only the Intel arm
        would promote the NEON arm to a primary `#if` and leave exactly the
        dispatch skeleton the milestone is about, in another dialect; both arms
        go, and only the portable `#else` survives.
        """
        return (b["elif_text"] is not None
                and "NEON_INTRINSICS" in b["elif_text"])

    def render():
        out = []
        i = 0
        by_start = {b["start"]: b for b in outer}
        while i < len(lines):
            b = by_start.get(i)
            if b is None:
                out.append(lines[i])
                i += 1
                continue
            first_alt, opens = arms(b)
            if neon_alt(b):
                # keep or cut, the NEON arm goes: promoting it or keeping it
                # verbatim both hand the candidate a dispatch skeleton for this
                # milestone's kernels, written for another architecture.
                if b["keep"]:
                    out.extend(lines[b["start"]:b["elif_line"]])
                    out.extend(lines[b["else_line"]:] if b["else_line"] is not None
                               else lines[b["end"]:b["end"] + 1])
                elif b["else_line"] is not None:
                    out.extend(lines[b["else_line"] + 1:b["end"]])
            elif b["keep"] and b["includes"] is not None:
                kept = [ln for (ln, own) in b["includes"]
                        if own is not None and own in rows]
                if kept:
                    out.append(lines[b["start"]])
                    out.extend(kept)
                    out.append(lines[b["end"]])
            elif b["keep"]:
                out.extend(lines[b["start"]:b["end"] + 1])
            elif not opens:
                pass
            elif b["elif_line"] is not None:
                out.append(re.sub(r'^\s*#\s*elif\b', '#if',
                                  lines[b["elif_line"]], count=1))
                out.extend(lines[b["elif_line"] + 1:b["end"] + 1])
            else:
                out.extend(lines[b["else_line"] + 1:b["end"]])
            i = b["end"] + 1
        return out

    # --- pass 2: attribute scaffolding to whoever uses it -----------------
    # A block that names no family on its own (the CPU-flag booleans above, for
    # instance) belongs to the families of the blocks that *use* what it
    # declares.  Deciding this from the surviving text instead -- "keep it if a
    # name it declares still appears somewhere" -- is a false positive
    # generator: a bool local and a function parameter can share a name in a
    # file that has nothing to do with the ladder, and the block then survives
    # into a rung that cannot compile.  Ownership by *use* is exact: a block
    # with no user is dead and goes, a block whose user is kept stays.
    for _ in range(6):
        changed = False
        for b in outer:
            if b["need"] or b["force_cut"] or not b["decls"]:
                continue
            acc = set()
            for other in outer:
                if other is b or other["force_cut"]:
                    continue
                body = other["if_body"]
                if any(re.search(r'\b' + re.escape(n) + r'\b', body)
                       for n in b["decls"]):
                    acc |= other["need"]
            if acc:
                b["need"] = acc
                changed = True
        if not changed:
            break
    for b in outer:
        decide(b)

    # --- pass 3: leave a self-contained unit whole (cut_one only) --------
    # `--cut` is the one caller that rewrites a tree whose files it did not
    # author, and the one shape it must not break is the candidate's own unit.
    # Gumming it is never right: it removes work the cut cannot attribute
    # instead of work the cut was asked for, and it leaves the tree unbuildable
    # so the gate has no baseline at all.  See selfcontained_intel_unit.
    if spare_units:
        if not any(b["keep"] for b in outer) and selfcontained_intel_unit(lines, outer):
            return 0

    rendered = "".join(render())
    out = scrub(rendered) if scrub_output else rendered
    if out != orig:
        with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
            fh.write(out)
    return sum(1 for b in outer if not b["keep"])


def cut_one(tree, rowspec):
    """Remove one or more rows from an existing tree, in place.

    A ladder step may carry several families (two of the overlay families are
    only reachable together), so the cut takes a comma-separated list.

    Used only to build the end-to-end non-regression baseline: `speedup` never
    uses it, and a tree it cannot handle is reported as `cut_failed` rather than
    failed, because the guard is a guard and not the score.

    The tree is the *candidate's*, so the cut has to survive a layout it did
    not author: an implementation unit the symbol map cannot attribute (the
    candidate's own file, under a name of its own) is left whole rather than
    gutted -- see `spare_units` and `selfcontained_intel_unit`.  Gutting it
    would leave the tree unbuildable, and a gate with no baseline is worse than
    a gate that reports it has no input: the first hides a regression behind a
    build failure, the second says so.

    The comment scrub is off here (`scrub_output=False`).  It exists so a rung
    does not ship a comment naming a kernel the tree no longer has, and there
    is nobody to leak to in a baseline built inside the verifier -- while the
    caller brackets this cut with a fingerprint of the tree, and a scrub that
    deletes a candidate's `// ... avx2 ...` line would read as "the cut removed
    something", making the gate report a comparison it could not have made.
    Off, the fingerprint changes exactly when the cut removed or disabled
    something of this family, which is what the gate asks about.
    """
    cut = set(int(x) for x in rowspec.split(","))
    here = os.path.dirname(os.path.abspath(__file__))
    symmap = load_symbol_map(os.path.join(here, "row_symbol_map.tsv"))
    removed = 0
    for dirpath, dirnames, filenames in os.walk(tree):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, tree)
            if "/intel/" not in "/" + rel and "/aarch64/" not in "/" + rel:
                continue
            if not fnmatch.fnmatch(rel, "*/intel/*") and "/intel/" not in "/" + rel:
                continue
            owned = row_of_intel_path(rel)
            if owned in cut:
                os.remove(full)
                removed += 1
    log = []
    for dirpath, dirnames, filenames in os.walk(os.path.join(tree, "avs_core")):
        for fn in filenames:
            if not fn.endswith((".cpp", ".h", ".hpp")):
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, tree)
            n = process(full, rel, set(ROWS), symmap, log,
                        keep_fn=lambda need: bool(need) and not (need & cut),
                        spare_units=True, scrub_output=False)
    print(f"cut rows {sorted(cut)}: {removed} files removed")
    return 0


def selected_files(pin, rows):
    """Repo-relative paths of the intel sources the selected rows own.

    Resolved against the *pin*, not against the process's working directory: a
    glob that silently matched nothing deleted every intel source, so a rung
    that should have carried a family carried none and still built.
    """
    keep = []
    for r in rows:
        for g in ROWS[r]:
            hits = glob.glob(os.path.join(pin, g))
            if not hits:
                raise SystemExit(f"mklevel: row {r} glob matched nothing: {g}")
            keep.extend(os.path.relpath(h, pin) for h in hits)
    return keep


DECL_LINE_RE = re.compile(
    r'^\s*(?:static\s+|inline\s+|AVS_FORCEINLINE\s+)*'
    r'(?:const\s+)?[A-Za-z_][\w:<>,*&\s]*?\s+\**(\w+)\s*\([^;{)]*\)\s*;\s*$')
TEMPLATE_LINE_RE = re.compile(r'^\s*template\s*<.*>\s*$')


def drop_dead_declarations(tree, symmap):
    """Delete declarations of a row's kernels from a tree that no longer has them.

    A rung keeps the portable reference files, and a reference file can carry a
    declaration of a kernel whose body lived in the intel source the rung just
    removed: `avs_core/filters/focus.h` declares the focus family's SAD kernels,
    and step 02's instruction points the candidate at that header.  The
    declaration is not an algorithm, but it does name the removed kernel and
    its exact signature, which is the ladder's own work handed back as a hint.

    Only a declaration that nothing in the tree can use is dropped: the symbol
    must have no occurrence anywhere in the tree outside a declaration line --
    no definition, no call, no address.  A declaration like that is dead text,
    and deleting it changes no compiled byte, because a function that is never
    defined and never referenced emits nothing.  Anything a surviving file
    still names is left exactly where it was.
    """
    files = []
    for dirpath, dirnames, filenames in os.walk(tree):
        for fn in filenames:
            if fn.endswith((".cpp", ".h", ".hpp")):
                files.append(os.path.join(dirpath, fn))
    texts = {}
    for full in files:
        with open(full, encoding="utf-8", errors="surrogateescape") as fh:
            texts[full] = fh.read()

    # file -> symbol -> the line indices its declaration occupies.  A
    # `template<...>` line directly above a declaration belongs to it.
    pending = {}
    for full, text in texts.items():
        lines = text.splitlines(keepends=True)
        for idx, ln in enumerate(lines):
            m = DECL_LINE_RE.match(ln)
            if not m or m.group(1) not in symmap:
                continue
            idxs = pending.setdefault(full, {}).setdefault(m.group(1), set())
            idxs.add(idx)
            if idx and TEMPLATE_LINE_RE.match(lines[idx - 1]):
                idxs.add(idx - 1)

    dropped = []
    changed = {}
    for full, per_symbol in sorted(pending.items()):
        gone = set()
        for sym, idxs in sorted(per_symbol.items()):
            ref = re.compile(r'\b' + re.escape(sym) + r'\b')
            used = False
            for other, text in texts.items():
                for j, ln in enumerate(text.splitlines()):
                    if other == full and j in idxs:
                        continue          # the declaration itself
                    if not ref.search(ln):
                        continue
                    if DECL_LINE_RE.match(ln) or TEMPLATE_LINE_RE.match(ln):
                        continue          # another declaration -- not a use
                    used = True
                    break
                if used:
                    break
            if not used:
                gone |= idxs
                dropped.append((os.path.relpath(full, tree),
                                sorted(i + 1 for i in idxs), sym))
        if gone:
            lines = texts[full].splitlines(keepends=True)
            changed[full] = "".join(ln for i, ln in enumerate(lines) if i not in gone)

    # Only the files whose declarations went are written back.  A file whose
    # bytes did not change keeps its own bytes and its own mtime, so the pass
    # is invisible on every tree where it has nothing to do.
    for full, text in changed.items():
        with open(full, "w", encoding="utf-8", errors="surrogateescape") as fh:
            fh.write(text)
    for rel, linenos, sym in dropped:
        print(f"declaration dropped: {rel}:{','.join(str(n) for n in linenos)} {sym}")
    return len(dropped)


def main(argv):
    if len(argv) == 4 and argv[1] == "--cut":
        return cut_one(argv[3], argv[2])  # (tree, rowspec)
    if len(argv) < 3:
        print(__doc__)
        return 2
    pin, out = argv[1], argv[2]
    rows = sorted(int(a) for a in argv[3:])
    here = os.path.dirname(os.path.abspath(__file__))
    symmap = load_symbol_map(os.path.join(here, "row_symbol_map.tsv"))

    for r in rows:
        missing = [d for d in ORDER[r] if d not in rows]
        if missing:
            print(f"ERROR: row {r} requires rows {missing}", file=sys.stderr)
            return 3

    if os.path.exists(out):
        shutil.rmtree(out)
    shutil.copytree(pin, out, symlinks=True, ignore=shutil.ignore_patterns(".git"))

    root = os.path.join(out, "")
    keep = selected_files(pin, rows)
    dropped = 0
    for rel in DROP_FILES:
        full = os.path.join(out, rel)
        if os.path.exists(full):
            os.remove(full)
            dropped += 1

    removed = []
    for dirpath, dirnames, filenames in os.walk(out):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, out)
            if "/intel/" not in "/" + rel and "/aarch64/" not in "/" + rel:
                continue
            if rel in keep:
                continue
            removed.append(rel)
    for rel in removed:
        os.remove(os.path.join(out, rel))
    for dirpath, dirnames, filenames in os.walk(out, topdown=False):
        if not os.listdir(dirpath):
            os.rmdir(dirpath)

    log = []
    edited_files = 0
    edited_blocks = 0
    for dirpath, dirnames, filenames in os.walk(os.path.join(out, "avs_core")):
        for fn in filenames:
            if not fn.endswith((".cpp", ".h", ".hpp")):
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, out)
            n = process(full, rel, set(rows), symmap, log)
            if n:
                edited_files += 1
                edited_blocks += n
    # After the conditionals, the dead declarations they left behind: a
    # removed kernel the reference file still names is a hint back to the work
    # this rung is about.  Before the timestamp pass below, so the files it
    # edits are stamped with the rest of the tree.
    dead = drop_dead_declarations(out, symmap)
    # One timestamp for the whole tree.  The stripper rewrites the files it
    # edits and leaves the rest alone, so without this the removal sites are
    # exactly the files whose mtime is newer than the extraction -- a candidate
    # could enumerate the whole answer set with one `find -newer`.
    stamp = 1700000000
    for dirpath, dirnames, filenames in os.walk(out):
        for fn in filenames:
            try:
                os.utime(os.path.join(dirpath, fn), (stamp, stamp))
            except OSError:
                pass
    for dirpath, dirnames, filenames in os.walk(out, topdown=False):
        try:
            os.utime(dirpath, (stamp, stamp))
        except OSError:
            pass

    print(f"rows={rows} files_removed={len(removed)} files_dropped={dropped} "
          f"files_edited={edited_files} blocks_cut={edited_blocks} "
          f"declarations_dropped={dead}")
    print("blocks not kept because another row was not selected:")
    for ln in log:
        print(ln)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
