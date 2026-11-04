#!/usr/bin/env python3
"""Remove the optimized kernels of named stages from a pinned vortex checkout.

    strip.py <tree> [stage ...]

With no stage arguments the tree is left untouched. With every stage named, the
tree is the ladder's L0: each stage's `#[target_feature]` kernels (and their
NEON counterparts) are gone and each selector is reduced to the plain tail
expression that names the portable kernel. The portable kernels themselves are
never touched -- they are the correctness reference *and* the speedup
denominator.

Idempotent: re-running on an already-stripped tree is a no-op, because every
removal is guarded by "is the thing still there".

Bodies are located by brace matching, never by regex: a non-greedy regex match
can swallow a neighbouring function and leave a tree that does not link.
"""

import re
import shutil
import sys
from pathlib import Path

# ---------------------------------------------------------------- item surgery

_ATTR = re.compile(r"\b(pub(?:\s*\([^)]*\))?|unsafe|async|const|extern(?:\s*\"[^\"]*\")?)\s*$")


def _first_brace(src: str, pos: int) -> int:
    """Index of the `{` that opens the body of the signature starting at pos.

    Tracks (), [] and <> so a `Fn(&[bool; 64]) -> u64` bound in the parameter
    list or a `where` clause cannot be mistaken for the body.
    """
    depth = 0  # () and []
    angle = 0  # <>, but `->` must not close one
    i = pos
    n = len(src)
    while i < n:
        c = src[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif c == "<":
            angle += 1
        elif c == ">" and angle > 0:
            angle -= 1
        elif c == "{" and depth == 0 and angle == 0:
            return i
        elif c == ";" and depth == 0 and angle == 0:
            raise ValueError("signature ends in `;` before a body opened")
        i += 1
    raise ValueError("no body brace found")


def _match_brace(src: str, open_idx: int) -> int:
    """Index of the `}` matching the `{` at open_idx."""
    depth = 0
    i = open_idx
    n = len(src)
    while i < n:
        c = src[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        elif c == '"':
            i += 1
            while i < n and src[i] != '"':
                i += 2 if src[i] == "\\" else 1
        elif c == "'":
            # a lifetime (`'a`) or a char literal; only a char literal can hold `{`
            if i + 2 < n and (src[i + 2] == "'" or src[i + 3] == "'"):
                i += 4 if src[i + 3] == "'" else 2
        i += 1
    raise ValueError("unbalanced braces")


_KW = r"(?:pub(?:\s*\([^)]*\))?|unsafe|async|const|extern(?:\s*\"[^\"]*\")?)"
_KW_LINE = re.compile(r"^" + _KW + r"\s*(?://.*)?$")
_KW_PREFIX = re.compile(r"^" + _KW + r"(?:\s+" + _KW + r")*\s*$")


def _line_bounds(src: str, pos: int):
    start = src.rfind("\n", 0, pos) + 1
    end = src.find("\n", pos)
    return start, len(src) if end < 0 else end


def _inside_attr(src: str, q: int) -> bool:
    """True when position `q` sits inside an attribute that is still open.

    An attribute may span lines (`#[expect(\\n a,\\n b\\n)]`); the line above the
    item is then `)]`, which matches none of the ordinary rules, and a walk-back
    that stopped there would strand half the attribute above the neighbouring item.
    """
    depth = 0
    i = q
    limit = max(0, q - 1500)
    while i > limit:
        i -= 1
        c = src[i]
        if c == "]":
            depth += 1
        elif c == "[":
            if depth == 0:
                return i > 0 and src[i - 1] == "#"
            depth -= 1
    return False


def _item_start(src: str, pos: int) -> int:
    """Walk back over attributes, doc comments and item keywords.

    Line-based, because an attribute may carry a trailing `//` comment whose
    closing character is not `]`. A character-based walk-back would stop at the
    comment and leave `#[cfg(target_arch = "aarch64")]` stranded above the
    neighbouring function.
    """
    p = pos
    while p > 0:
        if src[p - 1] != "\n":
            # `pos` sits inside a line -- the `fn` token follows `pub unsafe` on
            # it. Consume the whole line, but only if everything before the token
            # is item keywords.
            start, _end = _line_bounds(src, p - 1)
            prefix = src[start:p].strip()
            if prefix == "" or _KW_PREFIX.match(prefix):
                p = start
                continue  # now walk the attributes above this line
            break
        start, end = _line_bounds(src, p - 1)
        s = src[start:end].strip()
        if s.startswith("///") or s.startswith("//!"):
            p = start
            continue
        if s.startswith("#["):
            if "]" not in s:  # attribute continues on the previous line
                p = start
                continue
            p = start
            continue
        if _KW_LINE.match(s):
            p = start
            continue
        if _inside_attr(src, start):
            p = start
            continue
        break
    return p


def _fn_body_span(src: str, name: str):
    """(item_start, body_open, body_close) for the body of `fn name`."""
    for m in re.finditer(r"\bfn\s+" + re.escape(name) + r"\s*[(<]", src):
        open_idx = _first_brace(src, m.end() - 1)
        close_idx = _match_brace(src, open_idx)
        return _item_start(src, m.start()), open_idx, close_idx
    raise KeyError(name)


def _delete_trailing_newline(src: str, start: int, end: int) -> str:
    e = end
    while e < len(src) and src[e] in " \t\r\n":
        e += 1
    return src[:start] + src[e:]


def del_fn(path: Path, name: str) -> bool:
    src = path.read_text()
    try:
        start, _o, close = _fn_body_span(src, name)
    except KeyError:
        return False
    path.write_text(_delete_trailing_newline(src, start, close + 1))
    return True


def set_body(path: Path, name: str, body: str) -> bool:
    src = path.read_text()
    try:
        _s, open_idx, close = _fn_body_span(src, name)
    except KeyError:
        return False
    path.write_text(src[: open_idx + 1] + "\n" + body + "\n" + src[close:])


def del_item(path: Path, pattern: str) -> bool:
    """Delete the first braced item whose text matches `pattern` anywhere."""
    src = path.read_text()
    m = re.search(pattern, src)
    if m is None:
        return False
    # The pattern usually ends on the item's own `{`; only scan forward when it
    # does not (e.g. `struct Bmi2;`).
    open_idx = m.end() - 1 if src[m.end() - 1] == "{" else _first_brace(src, m.end())
    close = _match_brace(src, open_idx)
    start = _item_start(src, m.start())
    path.write_text(_delete_trailing_newline(src, start, close + 1))
    return True


def del_line_matching(path: Path, pattern: str) -> bool:
    src = path.read_text()
    out, hit = [], False
    for line in src.splitlines(keepends=True):
        if re.search(pattern, line):
            hit = True
            continue
        out.append(line)
    if hit:
        path.write_text("".join(out))
    return hit


def sub(path: Path, old: str, new: str) -> bool:
    src = path.read_text()
    if old not in src:
        return False
    path.write_text(src.replace(old, new))
    return True


# --------------------------------------------------------------------- stages

def stage_count_ones(tree: Path) -> None:
    f = tree / "vortex-buffer/src/bit/count_ones.rs"
    for n in (
        "count_ones_aligned_avx512_any_len",
        "count_ones_aligned_avx2_any_len",
        "count_ones_aligned_avx2",
        "count_ones_aligned_avx512",
    ):
        del_fn(f, n)
    set_body(f, "count_ones_aligned", "    count_ones_aligned_scalar(bytes)")
    del_line_matching(f, r"^type CountOnes = ")
    del_line_matching(f, r"^\s*use std::arch::x86_64::")
    del_line_matching(f, r"^\s*use crate::dispatch::CpuKernel;\s*$")
    del_line_matching(f, r"^\s*use vortex_error::VortexExpect;\s*$")
    # That import was guarded by a `#[cfg(target_arch = ...)]` of its own, which
    # is orphaned the moment the import goes. A dangling attribute above two
    # blank lines would mark the spot where code was removed.
    sub(f, '#[cfg(target_arch = "x86_64")]\n\n\n#[inline]\npub fn count_ones',
           "#[inline]\npub fn count_ones")


def stage_pack(tree: Path) -> None:
    f = tree / "vortex-buffer/src/bit/pack.rs"
    for n in (
        "collect_bool_words_sse2",
        "collect_bool_words_avx2",
        "collect_bool_words_avx512",
        "collect_bool_words_neon",
        "pack_bool_word_sse2",
        "pack_bool_word_avx2",
        "pack_bool_word_avx512",
        "pack_bool_word_neon",
    ):
        del_fn(f, n)
    set_body(
        f,
        "collect_bool_words_inline",
        "    collect_bool_words_with(words, len, f, pack_bool_word_swar)",
    )
    set_body(
        f,
        "collect_bool_words_multiversioned",
        "    collect_bool_words_inline(words, len, f)",
    )
    del_line_matching(f, r"^\s*use std::arch::x86_64::")
    del_line_matching(f, r"^\s*use std::arch::aarch64::")
    # Test module: the four tier-comparison tests name kernels that are gone.
    for n in (
        "sse2_matches_scalar",
        "avx2_matches_scalar",
        "avx512_matches_scalar",
        "neon_matches_scalar",
    ):
        del_fn(f, n)


def stage_select(tree: Path) -> None:
    f = tree / "vortex-buffer/src/bit/select.rs"
    for n in (
        "scan_chunks_avx512_vpopcnt",
        "select_in_chunk_vbmi2",
        "select_in_word_bmi2",
        "scan_chunks_neon",
    ):
        del_fn(f, n)
    set_body(
        f,
        "scan_chunks",
        "    scan_chunks_scalar(chunks, remaining, pos)",
    )
    set_body(f, "select_in_chunk", "    select_in_chunk_scalar(chunk, nth)")
    set_body(f, "select_in_word", "    select_in_word_scalar(word, nth)")
    del_line_matching(f, r"^type ScanChunks = ")
    del_line_matching(f, r"^type SelectInChunk = ")
    del_line_matching(f, r"^type SelectInWord = ")
    del_line_matching(f, r"^\s*use std::arch::x86_64::")
    del_line_matching(f, r"^\s*use crate::dispatch::CpuKernel;\s*$")


def stage_simd_compress(tree: Path) -> None:
    d = tree / "vortex-array/src/arrays/filter/execute/simd_compress"
    f = d / "mod.rs"
    shutil.rmtree(d / "x86.rs", ignore_errors=True)
    shutil.rmtree(d / "neon.rs", ignore_errors=True)
    for p in (d / "x86.rs", d / "neon.rs"):
        if p.exists():
            p.unlink()
    del_line_matching(f, r"^\s*mod x86;\s*$")
    del_line_matching(f, r"^\s*mod neon;\s*$")
    del_line_matching(f, r"^\s*#\[cfg\(all\(target_arch = \"x86_64\", not\(miri\)\)\)\]\s*$")
    del_line_matching(f, r"^\s*#\[cfg\(all\(target_arch = \"aarch64\", not\(miri\)\)\)\]\s*$")
    # `SLACK_BYTES` is the output allocation's slack, and the `Kernel` alias is
    # what `select_kernel` returns: both are part of the portable path and stay.
    set_body(f, "select_kernel", "    let _ = mask;\n    None")
    for n in ("compress_lut", "compress_tail", "bulk_copy"):
        del_fn(f, n)
    del_line_matching(f, r"^const MIN_LEN: ")
    del_line_matching(f, r"^\s*use std::ptr;\s*$")
    # Test module: the direct-coverage test for the AVX2 tier names kernels that
    # are gone, together with the helper it uses.
    t = d / "tests.rs"
    del_fn(t, "avx2_kernels_match_scalar")
    del_fn(t, "check_kernel")


def stage_take(tree: Path) -> None:
    d = tree / "vortex-array/src/arrays/fixed_width/take"
    shutil.rmtree(d / "avx2", ignore_errors=True)
    f = d / "mod.rs"
    del_line_matching(f, r'^#\[cfg\(any\(target_arch = "x86_64", target_arch = "x86"\)\)\]\s*$')
    del_line_matching(f, r"^\s*mod avx2;\s*$")
    del_line_matching(f, r"^\s*static HAS_AVX2: ")
    set_body(f, "take_values", "    take_values_scalar(values, indices)")
    del_line_matching(f, r"^\s*use std::sync::LazyLock;$")


def stage_bool_filter(tree: Path) -> None:
    f = tree / "vortex-array/src/arrays/bool/compute/filter.rs"
    del_fn(f, "filter_pext_bmi2")
    set_body(
        f,
        "filter_bitbuffer_by_mask",
        "    filter_pext_fallback(src, mask_buf, true_count)",
    )
    del_line_matching(f, r"^type FilterKernel = ")
    # `use std::arch::x86_64::_pext_u64;` appears twice: once inside the removed
    # BMI2 kernel (which goes with the function) and once inside the test that
    # checks the software fallback against the instruction. That second one must
    # survive, so nothing is deleted here by pattern.
    del_line_matching(f, r"^\s*use vortex_buffer::CpuKernel;\s*$")
    # The module's own cross-check test named the hardware instruction, the
    # feature gate and the argument order: it is a test *of the removed path*, so
    # it goes with the path.
    del_fn(f, "test_pext_fallback_matches_hardware")
    # `filter_inner` is the removed kernel's body with the missing hardware path
    # already plumbed in as a parameter. Drop the parameter so the shape is not
    # dictated, and neutralise the prose that names the technique.
    sub(f, "    filter_inner(src, mask_buf, true_count, pext_fallback)\n",
           "    filter_inner(src, mask_buf, true_count)\n")
    sub(f, "/// Core filter loop parameterized by the PEXT implementation.",
           "/// Core filter loop: walk the mask a word at a time and compact the selected bits.")
    sub(f, """    true_count: usize,
    pext_fn: impl Fn(u64, u64) -> u64,
) -> BitBuffer {""", """    true_count: usize,
) -> BitBuffer {""")
    sub(f, "let extracted = pext_fn(src_word, mask_word);",
           "let extracted = pext_fallback(src_word, mask_word);")
    sub(f, "let extracted = pext_fn(src_rem, remainder);",
           "let extracted = pext_fallback(src_rem, remainder);")
    sub(f, "copy source word directly, no PEXT needed.",
           "copy the source word directly.")
    sub(f, """/// Below this density threshold, use the sparse path which iterates only set
/// bits in the mask. Above it, the word-level PEXT approach is faster.""",
           """/// Below this density threshold, use the sparse path which iterates only set
/// bits in the mask. Above it, walking the mask a word at a time is faster.""")
    sub(f,
        "/// Extracted so the same logic is shared between the software and hardware paths.\n",
        "")


def stage_intersect(tree: Path) -> None:
    f = tree / "vortex-mask/src/intersect_by_rank.rs"
    for n in ("pdep_bmi2", "select_bit_position_bmi2"):
        del_fn(f, n)
    set_body(
        f,
        "intersect_bit_buffers_dispatch",
        "    intersect_bit_buffers::<Portable>(self_buffer, mask_buffer, true_count)",
    )
    set_body(
        f,
        "intersect_rank_indices_dispatch",
        "    intersect_bit_buffer_by_rank_indices::<Portable>(self_buffer, mask_indices)",
    )
    set_body(
        f,
        "intersect_mask_driven_dispatch",
        "    intersect_mask_driven::<Portable, _>(self_buffer, mask_indices, true_count)",
    )
    # The Bmi2 monomorphization target: the struct and its two trait impls.
    for pat in (
        r"struct Bmi2\s*;",
        r"impl DepositBits for Bmi2\s*\{",
        r"impl SelectBit for Bmi2\s*\{",
    ):
        del_item(f, pat)
    del_line_matching(f, r"^\s*use std::arch::x86_64::")
    del_line_matching(f, r"^\s*use vortex_buffer::CpuKernel;\s*$")
    sub(f,
        "    /// and `self_chunk == u64::MAX`. The portable path loops `popcount(mask)` times,\n"
        "    /// so an all-ones mask is genuinely expensive; BMI2 PDEP is constant-time and\n"
        "    /// the branches just add mispredict cost.\n",
        "    /// and `self_chunk == u64::MAX`. The portable path loops `popcount(mask)` times,\n"
        "    /// so an all-ones mask is genuinely expensive and the branches pay there.\n")
    sub(f, """/// Below this density threshold, use the sparse path which iterates only set
/// bits in the mask. Above it, the word-level PEXT approach is faster.""",
           """/// Below this density threshold, use the sparse path which iterates only set
/// bits in the mask. Above it, walking the mask a word at a time is faster.""")
    sub(f,
        "/// Extracted so the same logic is shared between the software and hardware paths.\n",
        "")



# ----------------------------------------------------------------- doc / bench

def set_module_doc(path: Path, new_doc: list[str]) -> bool:
    """Replace the `//!` block that follows the SPDX header."""
    lines = path.read_text().splitlines(keepends=True)
    first = next((i for i, l in enumerate(lines) if l.startswith("//!")), None)
    if first is None:
        return False
    last = first
    while last + 1 < len(lines) and lines[last + 1].startswith("//!"):
        last += 1
    body = "".join(("//! " + line + "\n") if line else "//!\n" for line in new_doc)
    path.write_text("".join(lines[:first]) + body + "".join(lines[last + 1 :]))
    return True


def set_item_doc(path: Path, anchor: str, new_doc: list[str]) -> bool:
    """Replace the `///` block immediately above the first item matching `anchor`.

    `_item_start` already walks back over the doc block, so the block is found by
    walking *forward* from there while the line is still a `///` line.
    """
    src = path.read_text()
    m = re.search(anchor, src)
    if m is None:
        return False
    block_start = _item_start(src, m.start())

    p = block_start
    while p < len(src):
        ls, le = _line_bounds(src, p)
        if not src[ls:le].strip().startswith("///"):
            break
        p = le + 1
    if p == block_start:
        return False

    anchor_start, _ = _line_bounds(src, m.start())
    prefix = src[anchor_start : m.start()]
    indent = prefix[: len(prefix) - len(prefix.lstrip())]
    body = "".join(
        (indent + "/// " + line + "\n") if line else (indent + "///\n")
        for line in new_doc
    )
    path.write_text(src[:block_start] + body + src[p:])
    return True


def trim_docs(tree: Path, stages: list[str]) -> None:
    """Reword the prose that described kernels this strip removed.

    Not cosmetic: a doc comment that names a removed tier marks where code was
    removed, and the stripped tree should read like ordinary code.
    """
    if "pack" in stages:
        f = tree / "vortex-buffer/src/bit/pack.rs"
        set_module_doc(f, [
            "Kernels for packing boolean values into bitmap words.",
            "",
            "`collect_bool` materializes each full 64-bit chunk as a `[bool; 64]` (a loop the",
            "auto-vectorizer turns into vector stores for simple predicates) and then packs the 64 bytes",
            "into a single `u64` with a byte->bit kernel.",
            "",
            "There are two tiers. The default ([`collect_bool_words_inline`]) compiles the loop once and",
            "inlines fully into the caller -- safe for arbitrary predicates. The opt-in tier",
            "([`collect_bool_words_multiversioned`]) exists for predicates small and simple enough that",
            "compiling the loop once per CPU feature level pays off.",
            "",
            "The bit-at-a-time loop lives on as [`collect_bool_word_scalar`], used for tail chunks and as",
            "the reference implementation for tests and benchmarks.",
        ])
        set_item_doc(f, r"\bfn collect_bool_words_inline\b", [
            "Body of [`collect_bool_words`](crate::bit::collect_bool_words) (and, via a one-word slice,",
            "of [`collect_bool_word`](crate::bit::collect_bool_word)): the word loop with the pack kernel",
            "this build was compiled for, so the predicate, the `[bool; 64]` materialization and the pack",
            "all inline into the caller with no `#[target_feature]` boundary.",
            "",
            "That boundary is why *runtime*-detected kernels are not used here: hiding an expensive,",
            "non-vectorizable predicate behind a non-inlinable loop copy costs far more end to end than a",
            "wider pack saves, and an indirect call per word is worse still, since an opaque call target",
            "blocks fill/pack fusion however cheap kernel *selection* is. For provably cheap predicates,",
            "use [`collect_bool_words_multiversioned`].",
        ])
        set_item_doc(f, r"\bfn collect_bool_words_multiversioned\b", [
            "Word loop for predicates known to be cheap, where compiling the loop once per CPU feature",
            "level is worth the duplication.",
            "",
            "`words` must hold at least `len.div_ceil(64)` entries and `f` is invoked with `0..len`,",
            "exactly once per index in ascending order.",
            "",
            "Panics if `words` is too short.",
        ])
        set_item_doc(f, r"\bfn collect_bool_words_with\b", [
            "Shared word loop: materialize each full 64-bit chunk as a `[bool; 64]` and pack it with",
            "`pack`; the tail chunk goes through the scalar loop.",
            "",
            "Marked `#[inline(always)]` so each caller gets its own fully-inlined copy.",
        ])
        # `bit/mod.rs` and `buf.rs` describe the compile-time tier in prose that
        # named the removed kernels and, on AVX-512, the exact instruction
        # sequence they use. Reworded to describe only what is still there.
        sub(f2 := tree / "vortex-buffer/src/bit/mod.rs",
            "a `[bool; 64]` and packed with the baseline SIMD byte\u2192bit instruction of the target; shorter",
            "a `[bool; 64]` and packed by the pack kernel this build was compiled with; shorter")
        sub(f2,
            "/// The word loop packs with the baseline SIMD kernel of the target (SSE2 on x86-64, NEON on\n"
            "/// aarch64), which inlines fully into the caller together with the predicate and the\n"
            "/// `[bool; 64]` materialization \u2014 wider kernels would sit behind a non-inlinable\n"
            "/// `#[target_feature]` boundary that deoptimizes expensive predicates. See\n",
            "/// The word loop inlines fully into the caller together with the predicate and the\n"
            "/// `[bool; 64]` materialization, so LLVM sees all three at once. See\n")
        sub(tree / "vortex-buffer/src/bit/buf.rs",
            "    /// # Code generation\n"
            "    ///\n"
            "    /// `collect_bool_words` calls `collect_bool_words_inline`, which selects a `pack_bool_word_*`\n"
            "    /// kernel at compile time and passes it to `collect_bool_words_with`. That shared word loop\n"
            "    /// materializes each full 64-value chunk as a byte-per-value `[bool; 64]`, then passes it to the\n"
            "    /// selected kernel. For simple predicates, LLVM vectorizes the loop and removes the physical\n"
            "    /// stack array. On AVX-512, it still combines the comparison masks, expands the result into 64\n"
            "    /// `0` or `1` bytes with `vpbroadcastq` and `vmovdqu8`, then recreates the mask with `vptestmb`.\n"
            "    /// [LLVM issue #219235](https://github.com/llvm/llvm-project/issues/219235) tracks replacing\n"
            "    /// that round trip with a direct `kmovq` store. The conversion is per chunk. This method does\n"
            "    /// not create a full-column byte buffer.\n",
            "    /// # Code generation\n"
            "    ///\n"
            "    /// The shared word loop materializes each full 64-value chunk as a byte-per-value\n"
            "    /// `[bool; 64]`, then passes it to the pack kernel this build was compiled with. For simple\n"
            "    /// predicates, LLVM vectorizes the loop and removes the physical stack array, so this method\n"
            "    /// does not create a full-column byte buffer.\n")
        set_item_doc(tree / "vortex-buffer/src/bit/buf.rs", r"\bfn collect_bool_multiversioned\b", [
            "Collects Boolean values with a fill-and-pack loop selected for the current CPU.",
            "",
            "This has the same callback contract as [`Self::collect_bool`], and shares its fill and pack",
            "steps; it exists as a separate entry point so a caller with a small, bounds-check-free",
            "predicate can be measured against it independently.",
            "",
            "Use this method only for a small, bounds-check-free predicate whose loop has been benchmarked.",
            "Use [`Self::collect_bool`] for general predicates.",
        ])
        # Bench entries that name the tier kernels by hand.
        b = tree / "vortex-buffer/benches/collect_bool.rs"
        for n in (
            "words_gather_sse2",
            "words_gather_avx2",
            "words_gather_avx512",
            "words_gather_neon",
        ):
            del_fn(b, n)

    if "select" in stages:
        f = tree / "vortex-buffer/src/bit/select.rs"
        set_item_doc(f, r"\bpub fn bit_select\b", [
            "Returns the position of the `nth` set bit (0-indexed) within the logical range",
            "`[offset, offset + len)` of the given byte slice.",
            "",
            "The returned position is relative to the logical start (i.e., 0-indexed from `offset`).",
            "Returns `None` if `nth` is out of bounds.",
            "",
            "Scans 64-byte chunks, narrows to a byte, then selects within the byte.",
        ])

    if "intersect" in stages:
        # The crate's dispatch primitive is shared by every stage, so its module
        # doc is only trimmed once, by whichever of the stages that name it is
        # present. It named two removed examples by hand: the `Bmi2`/`Portable`
        # type parameter (this stage) and the size gate in `count_ones_aligned`
        # (stage 1). Both become generic.
        d = tree / "vortex-buffer/src/dispatch.rs"
        sub(d,
            "//! monomorphize the loop instead, like the `Bmi2`/`Portable` type-parameter pattern in\n"
            "//! `vortex-mask::intersect_by_rank`.\n",
            "//! monomorphize the loop instead: put the decision in a type parameter so the compiler\n"
            "//! emits one specialized copy per feature level.\n")
        sub(d,
            "//! inputs are common: call the portable kernel directly below the size where SIMD pays\n"
            "//! off, so those calls stay inlinable and skip the dispatch entirely (see\n"
            "//! `count_ones_aligned`).\n",
            "//! inputs are common: call the portable kernel directly below the size where SIMD pays\n"
            "//! off, so those calls stay inlinable and skip the dispatch entirely.\n")

    if "simd_compress" in stages:
        f = tree / "vortex-array/src/arrays/filter/execute/simd_compress/mod.rs"
        set_module_doc(f, [
            "Kernels for fixed-width filtering.",
            "",
            "A kernel compacts the selected lanes of a fixed-width array into the front of the output,",
            "and is eligible only when the mask is dense enough and the array is long enough; otherwise",
            "control returns to [`buffer`](super::buffer) for scalar selection.",
        ])

    if "bool_filter" in stages:
        f = tree / "vortex-array/src/arrays/bool/compute/filter.rs"
        set_item_doc(f, r"\bunsafe fn filter_pext\b|\bfn filter_bitbuffer_by_mask\b", [
            "Extract bits from `src` where corresponding bits in `mask_buf` are set.",
            "",
            "Compacts the selected bits of each 64-bit word into the output, with a u128 accumulator to",
            "simplify overflow handling. Fast paths skip the compaction entirely for all-ones and",
            "all-zeros mask words.",
        ])

STAGES = {
    "count_ones": stage_count_ones,
    "pack": stage_pack,
    "select": stage_select,
    "simd_compress": stage_simd_compress,
    "take": stage_take,
    "bool_filter": stage_bool_filter,
    "intersect": stage_intersect,
}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    tree = Path(argv[1]).resolve()
    stages = argv[2:]
    for s in stages:
        if s not in STAGES:
            print(f"unknown stage {s!r}; known: {', '.join(STAGES)}", file=sys.stderr)
            return 2
    for s in stages:
        STAGES[s](tree)
    trim_docs(tree, stages)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
