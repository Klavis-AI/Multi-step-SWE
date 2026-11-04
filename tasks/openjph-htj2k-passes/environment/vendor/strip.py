#!/usr/bin/env python3
"""Remove optimized (SIMD) implementations of named stages from an OpenJPH tree.

    strip.py <tree> <stage> [<stage> ...]

Everything named on the command line is REMOVED.  With no stage named, all six
stages are removed -- that is L0, the base.  Levels are built by naming every
stage EXCEPT the ones that level is supposed to have.

Construction is (a): the portable C++ implementation of each stage stays in the
tree and is what the library falls back to.  The stripper therefore removes only
the per-instruction-set .cpp files, the dispatch lines that install them, the
declarations of the removed symbols, and the build-system lines naming them.

Every ISA tier the grading host cannot execute is removed at EVERY level, for
every stage, not just the six: AVX-512 (the host has no AVX-512) and the two
non-x86 tiers (VSX, WASM).  Those are the copies that make a stripped stage a
translation exercise rather than an implementation problem, so they go first
and they never come back.

Idempotent: re-running on an already-stripped tree is a no-op.
"""
import os
import re
import sys

# --------------------------------------------------------------------------
# Stage definitions.  `files` are removed outright; `syms` are the symbols whose
# dispatch lines, declarations and build properties are removed.
# --------------------------------------------------------------------------
STAGES = {
    "blk_decode": {
        "tier": "AVX2",
        "files": [
            "src/core/coding/ojph_block_decoder_ssse3.cpp",
            "src/core/coding/ojph_block_decoder_avx2.cpp",
        ],
        "syms": [
            "ojph_decode_codeblock_ssse3", "ojph_decode_codeblock_avx2",
        ],
        "seam": "src/core/codestream/ojph_codeblock_fun.cpp",
        # `uvlc_tbl1_wide` and its initialiser exist only for the optimized
        # decoder: with that file gone they are dead objects whose presence
        # announces what was removed, so they go with it
        "scrub": [
            ("src/core/coding/ojph_block_common.cpp",
             ["uvlc_tbl1_wide", "uvlc_init_wide_table"]),
            ("src/core/coding/ojph_block_common.h", ["uvlc_tbl1_wide"]),
        ],
        "decl_in": ["src/core/coding/ojph_block_decoder.h",
                    "src/core/codestream/ojph_codeblock_fun.cpp",
                    "src/core/coding/ojph_block_common.h"],
        "calls": [],
    },
    "blk_encode": {
        "tier": "AVX2",
        "files": [
            "src/core/coding/ojph_block_encoder_avx2.cpp",
            "src/core/coding/ojph_block_encoder_avx2_apple.h",
            "src/core/coding/ojph_block_encoder_avx512.cpp",
        ],
        "syms": [
            "ojph_encode_codeblock_avx2", "ojph_encode_codeblock_avx512",
        ],
        # these are not dispatch assignments but side-effecting calls
        "calls": [
            "initialize_block_encoder_tables_avx2",
            "initialize_block_encoder_tables_avx512",
        ],
        "seam": "src/core/codestream/ojph_codeblock_fun.cpp",
        "decl_in": ["src/core/coding/ojph_block_encoder.h",
                    "src/core/codestream/ojph_codeblock_fun.cpp"],
    },
    "rev_wavelet": {
        "tier": "AVX2",
        "files": [
            "src/core/transform/ojph_transform_sse2.cpp",
            "src/core/transform/ojph_transform_avx2.cpp",
        ],
        "syms": [
            "sse2_rev_vert_step", "sse2_rev_horz_ana", "sse2_rev_horz_syn",
            "avx2_rev_vert_step", "avx2_rev_horz_ana", "avx2_rev_horz_syn",
        ],
        "seam": "src/core/transform/ojph_transform.cpp",
        "decl_in": ["src/core/transform/ojph_transform_local.h"],
    },
    "irv_wavelet": {
        "tier": "AVX",
        "files": [
            "src/core/transform/ojph_transform_sse.cpp",
            "src/core/transform/ojph_transform_avx.cpp",
        ],
        "syms": [
            "sse_irv_vert_step", "sse_irv_vert_times_K",
            "sse_irv_horz_ana", "sse_irv_horz_syn",
            "avx_irv_vert_step", "avx_irv_vert_times_K",
            "avx_irv_horz_ana", "avx_irv_horz_syn",
        ],
        "seam": "src/core/transform/ojph_transform.cpp",
        "decl_in": ["src/core/transform/ojph_transform_local.h"],
    },
    "colour": {
        "tier": "AVX2",
        "files": [
            "src/core/transform/ojph_colour_sse.cpp",
            "src/core/transform/ojph_colour_sse2.cpp",
            "src/core/transform/ojph_colour_avx.cpp",
            "src/core/transform/ojph_colour_avx2.cpp",
        ],
        "syms": [
            "sse_ict_forward", "sse_ict_backward",
            "sse2_rev_convert", "sse2_rev_convert_nlt_type3",
            "sse2_irv_convert_to_integer", "sse2_irv_convert_to_float",
            "sse2_irv_convert_to_integer_nlt",
            "sse2_irv_convert_to_integer_nlt_type3",
            "sse2_irv_convert_to_float_nlt",
            "sse2_irv_convert_to_float_nlt_type3",
            "sse2_rct_forward", "sse2_rct_backward",
            "avx_ict_forward", "avx_ict_backward",
            "avx2_rev_convert", "avx2_rev_convert_nlt_type3",
            "avx2_irv_convert_to_integer", "avx2_irv_convert_to_float",
            "avx2_irv_convert_to_integer_nlt",
            "avx2_irv_convert_to_integer_nlt_type3",
            "avx2_irv_convert_to_float_nlt",
            "avx2_irv_convert_to_float_nlt_type3",
            "avx2_rct_forward", "avx2_rct_backward",
        ],
        "seam": "src/core/transform/ojph_colour.cpp",
        "decl_in": ["src/core/transform/ojph_colour_local.h"],
    },
    "codestream": {
        "tier": "AVX2",
        "files": [
            "src/core/codestream/ojph_codestream_sse.cpp",
            "src/core/codestream/ojph_codestream_sse2.cpp",
            "src/core/codestream/ojph_codestream_avx.cpp",
            "src/core/codestream/ojph_codestream_avx2.cpp",
        ],
        "syms": [
            "sse_mem_clear", "avx_mem_clear",
            "sse2_find_max_val32", "sse2_find_max_val64",
            "avx2_find_max_val32", "avx2_find_max_val64",
            "sse2_rev_tx_to_cb32", "sse2_irv_tx_to_cb32", "avx2_rev_tx_to_cb32",
            "avx2_irv_tx_to_cb32",
            "sse2_rev_tx_from_cb32", "sse2_irv_tx_from_cb32",
            "avx2_rev_tx_from_cb32", "avx2_irv_tx_from_cb32",
            "sse2_rev_tx_to_cb64", "avx2_rev_tx_to_cb64",
            "sse2_rev_tx_from_cb64", "avx2_rev_tx_from_cb64",
        ],
        "seam": "src/core/codestream/ojph_codeblock_fun.cpp",
        "decl_in": ["src/core/codestream/ojph_codeblock_fun.cpp"],
    },
}

# ISA tiers removed at every level, for every stage.  Not stages: permanent
# exclusions, because the grading host cannot execute them (AVX-512) or is not
# this architecture (VSX, WASM) -- and each is a complete implementation of the
# same algorithm, which is exactly what must not be left in the tree.
ALWAYS_REMOVE_DIRS = [
    # a prebuilt WASM-SIMD build of the whole library, checked into upstream.
    # Not used by any configuration this task builds, and it is a complete
    # compiled implementation of the same six stages -- the "other
    # architecture's copy" that must not stay in the tree.
    "subprojects",
]
ALWAYS_REMOVE = [
    "src/core/coding/ojph_block_decoder_vsx.cpp",
    "src/core/coding/ojph_block_decoder_wasm.cpp",
    "src/core/coding/ojph_block_encoder_avx512.cpp",
    "src/core/transform/ojph_transform_vsx.cpp",
    "src/core/transform/ojph_transform_wasm.cpp",
    "src/core/transform/ojph_transform_avx512.cpp",
    "src/core/transform/ojph_colour_vsx.cpp",
    "src/core/transform/ojph_colour_wasm.cpp",
    "src/core/codestream/ojph_codestream_vsx.cpp",
    "src/core/codestream/ojph_codestream_wasm.cpp",
    "src/core/shared/ojph_simd_vsx.h",
]
ALWAYS_SYMS = [
    # avx512 block encoder is already listed under its stage; the rest:
    "vsx_mem_clear", "wasm_mem_clear",
    "vsx_find_max_val32", "vsx_find_max_val64",
    "wasm_find_max_val32", "wasm_find_max_val64",
    "vsx_rev_tx_to_cb32", "vsx_irv_tx_to_cb32", "wasm_rev_tx_to_cb32",
    "wasm_irv_tx_to_cb32",
    "vsx_rev_tx_from_cb32", "vsx_irv_tx_from_cb32", "wasm_rev_tx_from_cb32",
    "wasm_irv_tx_from_cb32",
    "vsx_rev_tx_to_cb64", "wasm_rev_tx_to_cb64",
    "vsx_rev_tx_from_cb64", "wasm_rev_tx_from_cb64",
    "ojph_decode_codeblock_vsx", "ojph_decode_codeblock_wasm",
    "vsx_rev_vert_step", "vsx_rev_horz_ana", "vsx_rev_horz_syn",
    "wasm_rev_vert_step", "wasm_rev_horz_ana", "wasm_rev_horz_syn",
    "vsx_irv_vert_step", "vsx_irv_vert_times_K",
    "vsx_irv_horz_ana", "vsx_irv_horz_syn",
    "wasm_irv_vert_step", "wasm_irv_vert_times_K",
    "wasm_irv_horz_ana", "wasm_irv_horz_syn",
    "avx512_rev_vert_step", "avx512_rev_horz_ana", "avx512_rev_horz_syn",
    "avx512_irv_vert_step", "avx512_irv_vert_times_K",
    "avx512_irv_horz_ana", "avx512_irv_horz_syn",
    "vsx_rev_convert", "vsx_rev_convert_nlt_type3",
    "vsx_irv_convert_to_integer", "vsx_irv_convert_to_float",
    "vsx_irv_convert_to_integer_nlt_type3", "vsx_irv_convert_to_float_nlt_type3",
    "vsx_rct_forward", "vsx_rct_backward",
    "vsx_ict_forward", "vsx_ict_backward",
    "wasm_rev_convert", "wasm_rev_convert_nlt_type3",
    "wasm_irv_convert_to_integer", "wasm_irv_convert_to_float",
    "wasm_irv_convert_to_integer_nlt_type3",
    "wasm_irv_convert_to_float_nlt_type3",
    "wasm_rct_forward", "wasm_rct_backward",
    "wasm_ict_forward", "wasm_ict_backward",
]
# arch-gated dispatch blocks that would otherwise install a tier we removed
ALWAYS_CALLS = ["initialize_block_encoder_tables_avx512"]


def die(msg):
    sys.stderr.write("strip.py: %s\n" % msg)
    raise SystemExit(1)


def symbol_word(sym):
    """Regex matching a whole identifier, so `avx2_x` never matches `x_avx2_x`."""
    return re.compile(r"\b" + re.escape(sym) + r"\b")


class _Node(object):
    """A block-structured fragment: `items` holds its lines and child nodes in
    the exact order they were read, so a node can be dropped as a whole."""

    __slots__ = ("kind", "items", "rendered", "kill")

    def __init__(self, kind, first=None):
        self.kind = kind            # root | pp | block | stmt | text
        self.items = []
        self.rendered = False       # this node existed with content at parse
        self.kill = False           # whole-region removal, decided up front
        if first is not None:
            for v in (first if isinstance(first, list) else [first]):
                self.items.append(("line", v))

    def add_line(self, ln):
        self.items.append(("line", ln))

    def add_node(self, node):
        self.items.append(("node", node))

    @property
    def children(self):
        return [v for k, v in self.items if k == "node"]

    def lines(self):
        return [v for k, v in self.items if k == "line"]


_PP_OPEN = re.compile(r"^#\s*(if|ifdef|ifndef)\b")
_PP_ELSE = re.compile(r"^#\s*(else|elif)\b")
_PP_END = re.compile(r"^#\s*endif\b")
# a `} else ...` continues the construct it closes, so an if/else chain is one
# node and can never be left with a dangling `else`
_ELSE_TAIL = re.compile(r"^else\b")
# only a per-tier or per-architecture guard may be removed as a whole region;
# the outer `#if !defined(OJPH_ENABLE_WASM_SIMD)` also carries the portable
# default path and must survive
_TIER_COMMENT = re.compile(r"\b(avx512|avx2|avx|ssse3|sse2|sse|vsx|wasm)\b", re.I)
_TIER_GUARD = re.compile(
    r"#\s*if(n?def)?\b[^\n]*(OJPH_DISABLE_(SSE|SSE2|SSSE3|SSE4|AVX|AVX2|"
    r"AVX512|NEON)\b|OJPH_ARCH_)")


def _parse(lines):
    root = _Node("root")
    stack = [root]
    pending = []

    def settle(node):
        node.rendered = bool(node.children)

    for ln in lines:
        code = ln.split("//", 1)[0]
        s = ln.strip()
        cs = code.strip()

        if pending:
            pending.append(ln)
            joined = " ".join(x.strip() for x in pending)
            if "{" in joined:
                node = _Node("block", pending)
                del pending[:]
                stack[-1].add_node(node)
                stack.append(node)
            elif joined.endswith(";"):
                node = _Node("stmt", pending)
                del pending[:]
                stack[-1].add_node(node)
            continue

        if not s or s.startswith("//"):
            stack[-1].add_line(ln)
        elif _PP_OPEN.match(s):
            node = _Node("pp", ln)
            stack[-1].add_node(node)
            stack.append(node)
        elif _PP_ELSE.match(s):
            stack[-1].add_line(ln)
        elif _PP_END.match(s):
            if stack[-1].kind == "pp":
                node = stack.pop()
                settle(node)
                node.add_line(ln)
            else:
                stack[-1].add_line(ln)
        elif cs.startswith("}"):
            tail = cs[1:].strip()
            if stack[-1].kind == "block":
                if _ELSE_TAIL.match(tail) and "{" in tail:
                    stack[-1].add_line(ln)          # `} else {` -- same chain
                    continue
                node = stack.pop()
                settle(node)
                node.add_line(ln)                   # the closer belongs to it
                continue
            if not tail:
                continue                            # stray brace, drop it
            if "{" in tail:
                node = _Node("block", ln)
                stack[-1].add_node(node)
                stack.append(node)
            else:
                stack[-1].add_line(ln)
        elif "{" in code:
            node = _Node("block", ln)
            stack[-1].add_node(node)
            stack.append(node)
        elif cs.endswith(";"):
            stack[-1].add_node(_Node("stmt", ln))
        else:
            pending.append(ln)

    if pending:
        stack[-1].add_node(_Node("stmt", pending))
    # a node is attached to its parent the moment it is opened, so unwinding
    # the stack must not attach it again
    while len(stack) > 1:
        settle(stack.pop())
    return root


def _walk(root):
    """Every node under root, parents before children."""
    order, work = [], [root]
    while work:
        node = work.pop()
        order.append(node)
        work.extend(node.children)
    return order


def _all_stmts(root):
    return [n for n in _walk(root) if n.kind == "stmt"]


def _removed_stmts(root, pats, call_pats, blocks_too=False):
    """Statement nodes carrying this stage's symbols, plus the `assert(r);`
    lines stranded by a removed `bool r = initialize_...(..);`."""
    dead_locals = set()
    for st in _all_stmts(root):
        text = " ".join(st.lines())
        if any(p.search(text) for p in call_pats):
            m = re.search(r"\b(?:bool|auto|int|ui32)\s+(\w+)\s*=", text)
            if m:
                dead_locals.add(m.group(1))
    ids = set()
    if blocks_too:
        for n in _walk(root):
            if n.kind != "block":
                continue
            head = next((t for t in n.lines() if t.strip()), "")
            if "(" in head and any(p.search(head) for p in pats):
                ids.add(id(n))
    for st in _all_stmts(root):
        text = " ".join(st.lines())
        if any(p.search(text) for p in pats) and "=" in text:
            ids.add(id(st))
        elif any(p.search(text) for p in call_pats):
            ids.add(id(st))
        else:
            m = re.match(r"^\s*(?:assert|ojph_unused|\(void\))\s*\(\s*(\w+)",
                         text)
            if m and m.group(1) in dead_locals:
                ids.add(id(st))
    return ids


def _remove_symbol(lines, sym):
    """Delete everything that names `sym`: the function whose definition it is
    (brace matched from its signature), the declaration, and the doc comment
    that introduces either.  Removing a definition by hand rather than through
    the block parser, because a doc comment opening with `/**` is not a `//`
    line and a declaration with an initialiser `= { 0 };` opens and closes a
    brace on one line -- both defeat the block model."""
    rx = re.compile(r"\b" + re.escape(sym) + r"\b")
    kill = [False] * len(lines)
    n = 0
    for i, ln in enumerate(lines):
        if kill[i] or not rx.search(ln):
            continue
        code = ln.split("//", 1)[0]
        start = i
        if "(" in code and ")" in code:
            # a definition or declaration: find its terminating ';' or its body
            depth = 0
            j = i
            opened = False
            while j < len(lines):
                for ch in lines[j].split("//", 1)[0]:
                    if ch == "{":
                        depth += 1
                        opened = True
                    elif ch == "}":
                        depth -= 1
                if opened and depth <= 0:
                    break
                if not opened and ";" in lines[j].split("//", 1)[0]:
                    break
                j += 1
            end = min(j, len(lines) - 1)
        else:
            end = i
        # take the doc comment that introduces it
        k = start - 1
        while k >= 0 and lines[k].strip().startswith(("///", "//", "*", "/*")):
            start = k
            k -= 1
        for t in range(start, end + 1):
            if not kill[t]:
                kill[t] = True
                n += 1
    return [l for l, k in zip(lines, kill) if not k], n


def _prune(root, removable_ids):
    """Bottom-up: drop removable statements, then any block or #if whose whole
    content went with them."""
    for node in reversed(_walk(root)):
        keep = []
        for kind, val in node.items:
            if kind == "node":
                pass
            if kind == "node":
                if id(val) in removable_ids and val.kind in ("stmt", "block"):
                    continue
                # `rendered` means the node held child nodes when it was
                # parsed, so having none now means they all went
                if val.kind in ("block", "pp") and val.rendered \
                        and not val.children:
                    continue
                # an `#ifndef OJPH_DISABLE_<tier>` region whose every
                # assignment installs a removed symbol goes whole, including
                # the `else` branch upstream wrote beside it, which only
                # restores the default path the generic block already set
            keep.append((kind, val))
        # a removed definition takes the doc comment that introduces it
        if len(keep) < len(node.items):
            while keep and keep[-1][0] == "line" and \
                    keep[-1][1].strip().startswith(("//", "*", "/*")):
                keep.pop()
        # an `else { ... }` whose `if` was taken away is a dangling branch
        fixed = []
        for kind, val in keep:
            if kind == "node" and val.kind == "block":
                head = next((t.strip() for t in val.lines() if t.strip()), "")
                prev = fixed[-1] if fixed else None
                if _ELSE_TAIL.match(head) and not (
                        prev and prev[0] == "node"
                        and prev[1].kind == "block"):
                    continue
            fixed.append((kind, val))
        node.items = fixed


def drop_tier_comments(lines, tier_re):
    """Remove comment-only lines that signpost a tier this tree no longer has.
    A stripped tree that still announces `// AVX2-accelerated decoder` tells the
    candidate what to write as loudly as the file itself would."""
    out, n = [], 0
    for ln in lines:
        t = ln.strip()
        if t.startswith("//") and tier_re.search(t):
            n += 1
            continue
        out.append(ln)
    return out, n


def _is_structural(ln):
    t = ln.strip()
    return t in ("", "{", "}", "};") or t.startswith("#") or t.startswith("//")


def _emit(root):
    out, work = [], [("node", root)]
    while work:
        kind, val = work.pop()
        if kind == "line":
            out.append(val)
            continue
        for k, v in reversed(val.items):
            work.append((k, v))
    return out


def strip_seam(path, syms, calls, kept_syms=()):
    """Remove this stage's dispatch assignments, table initialisation calls and
    every comment that signposts a tier this tree no longer carries.  Whole
    constructs go, not lines, so no dangling `if`/`else` is left behind."""
    if not os.path.exists(path):
        return 0
    pats = [symbol_word(s) for s in syms]
    call_pats = [symbol_word(c) for c in calls]
    tier = re.compile(r"\b(avx512|vsx|wasm)\b", re.I)

    src = open(path).read().split("\n")
    whole = "".join(src)
    before = (whole.count("{"), whole.count("}"))

    kept_isa = [symbol_word(x) for x in kept_syms]
    removed_isa = [symbol_word(x) for x in syms]

    root = _parse(src)
    # A tier guard (`#ifndef OJPH_DISABLE_AVX2`, `#if defined(OJPH_ARCH_...)`)
    # whose every assignment installs a removed symbol goes whole, including
    # the `else` branch upstream wrote beside it -- that branch only restores
    # the default the generic block above already set.  Judged on the ORIGINAL
    # text: once the removed assignments are pruned away, an `else` branch is
    # all that is left to look at, and it names no removed symbol.
    for node in _walk(root):
        if node.kind != "pp":
            continue
        head = node.lines()[0] if node.lines() else ""
        if not _TIER_GUARD.search(head):
            continue
        text = " ".join(node.lines()) + " " + " ".join(
            t for st in _all_stmts(node) for t in st.lines())
        if any(p.search(text) for p in removed_isa) and \
                not any(p.search(text) for p in kept_isa):
            node.kill = True

    gone = _removed_stmts(root, pats, call_pats)
    _prune(root, gone)
    # kill the marked regions and everything inside them
    for node in _walk(root):
        node.items = [(k, v) for k, v in node.items
                      if not (k == "node" and getattr(v, "kill", False))]
    lines = _emit(root)

    # a braceless `if (cond)` whose body was removed has nothing under it
    out, n = [], len(gone)
    for i, ln in enumerate(lines):
        if re.match(r"^\s*if\s*\(.*\)\s*$", ln):
            j = i + 1
            while j < len(lines) and not lines[j].strip():
                j += 1
            nxt = lines[j].strip() if j < len(lines) else ""
            if nxt == "" or nxt.startswith("#") or nxt.startswith("}"):
                n += 1
                continue
        out.append(ln)
    out, c = drop_tier_comments(out, tier)
    n += c
    res, blank = [], 0
    for ln in out:
        if not ln.strip():
            blank += 1
            if blank > 1:
                continue
        else:
            blank = 0
        res.append(ln)

    # the stripper removes whole balanced constructs, so the counts move; what
    # must never move is the balance between them
    after = ("".join(res).count("{"), "".join(res).count("}"))
    if before[0] != before[1] or after[0] != after[1]:
        die("%s: unbalanced braces %s -> %s; refusing to write"
            % (path, before, after))
    open(path, "w").write("\n".join(res))
    return n


def strip_decls(paths, syms, calls):
    """Remove declarations of the removed symbols, using the same block parser
    so a declaration wrapped over several lines goes as a unit."""
    pats = [symbol_word(s) for s in syms + calls]
    n = 0
    for path in paths:
        if not os.path.exists(path):
            continue
        src = open(path).read().split("\n")
        whole = "".join(src)
        before = (whole.count("{"), whole.count("}"))
        root = _parse(src)
        ids = set()
        for st in _all_stmts(root):
            text = " ".join(st.lines())
            if any(p.search(text) for p in pats) and "(" in text:
                ids.add(id(st))
        if not ids:
            continue
        _prune(root, ids)
        out = _emit(root)
        after = ("".join(out).count("{"), "".join(out).count("}"))
        if before[0] != before[1] or after[0] != after[1]:
            die("%s: unbalanced braces %s -> %s; refusing to write"
                % (path, before, after))
        out, c = drop_tier_comments(out, _TIER_COMMENT)
        n += len(ids) + c
        open(path, "w").write("\n".join(out))
    return n


def strip_cmake(tree):
    """Drop the build lines that name files this tree no longer has, and the
    GLOB/append/source_group lines for a file class that is now empty."""
    path = os.path.join(tree, "src/core/CMakeLists.txt")
    if not os.path.exists(path):
        return 0
    lines = open(path).read().split("\n")
    empty_vars = set()
    out, n = [], 0
    for ln in lines:
        m = re.search(r'set_source_files_properties\(\s*([^ \t)]+)', ln)
        if m and not os.path.exists(os.path.join(tree, "src/core", m.group(1))):
            n += 1
            continue
        g = re.search(r'file\(GLOB\s+(\w+)\s+"([^"]+)"', ln)
        if g:
            d = os.path.join(tree, "src/core", os.path.dirname(g.group(2)))
            pat = os.path.basename(g.group(2))[1:]        # strip the '*'
            if not os.path.isdir(d) or not any(
                    f.endswith(pat) for f in os.listdir(d)):
                empty_vars.add(g.group(1))
                n += 1
                continue
        out.append(ln)
    lines = out
    tier_map = [(re.compile(r"\bvsx\b|\bVSX\b"), "VSX"),
                (re.compile(r"\bwasm\b|\bWASM\b"), "WASM"),
                (re.compile(r"\bavx?512\b|\bAVX512\b"), "AVX512")]
    if empty_vars:
        kept = []
        for ln in lines:
            t = ln.strip()
            if t.startswith("#"):
                drop = False
                for rx, tag in tier_map:
                    if rx.search(t) and any(v.endswith(tag) for v in empty_vars):
                        drop = True
                if drop:
                    n += 1
                    continue
            kept.append(ln)
        lines = kept
    # A `${VAR}` whose GLOB is now empty is an undefined variable that NAMES the
    # file class it used to hold -- `list(APPEND SOURCES ${CODESTREAM_AVX2}
    # ${TRANSFORM_AVX2} ${CODING_AVX2})` still reads as "there was a
    # codestream/ and a transform/ avx2 translation unit".  Drop the empty
    # names from the line; drop the line when nothing is left.
    for _ in range(6):
        out, changed = [], False
        for ln in lines:
            vs = set(re.findall(r"\$\{(\w+)\}", ln))
            if vs and vs <= empty_vars:
                changed = True
                n += 1
                continue
            if vs & empty_vars and re.match(
                    r"^\s*(list\(|source_group\()", ln):
                new = ln
                for v in sorted(vs & empty_vars):
                    new = new.replace("${%s} " % v, "").replace(" ${%s}" % v, "")
                new = re.sub(r"\(\s+", "(", new)
                n += 1
                changed = True
                if re.search(r"\$\{\w+\}", new):
                    out.append(new)
                continue
            out.append(ln)
        lines = out
        lines, c2 = _drop_empty_blocks(lines)
        lines, c3 = _drop_comment_only_blocks(lines)
        changed = changed or c2 or c3
        n += c3
        if not changed:
            break
    open(path, "w").write("\n".join(lines))
    return n


def _cmake_statement(ln):
    t = ln.strip()
    return bool(t) and not t.startswith("#")


def _drop_comment_only_blocks(lines):
    """A CMake `if ... endif()` whose body is now nothing but comments is a
    signpost: the comments describe the very tier the body used to install."""
    changed = True
    n = 0
    while changed:
        changed = False
        out, i = [], 0
        while i < len(lines):
            if re.match(r"^\s*if\s*\(", lines[i]):
                depth, j, body = 1, i + 1, []
                while j < len(lines) and depth > 0:
                    if re.match(r"^\s*if\s*\(", lines[j]):
                        depth += 1
                    elif re.match(r"^\s*endif\s*\(", lines[j]):
                        depth -= 1
                        if depth == 0:
                            break
                    body.append(lines[j])
                    j += 1
                if j < len(lines) and depth == 0:
                    if not any(_cmake_statement(b) for b in body):
                        n += len(body) + 2
                        changed = True
                        i = j + 1
                        continue
            out.append(lines[i])
            i += 1
        lines = out
    return lines, n


def _drop_empty_blocks(lines):
    """Remove `if (...) {` / `}` pairs and #if/#endif pairs with no content."""
    changed = False
    for _ in range(20):
        out, i, did = [], 0, False
        while i < len(lines):
            ln = lines[i]
            if re.match(r"^\s*#\s*if(n?def)?\b", ln):
                depth, j, body = 1, i + 1, []
                while j < len(lines) and depth > 0:
                    if re.match(r"^\s*#\s*if(n?def)?\b", lines[j]):
                        depth += 1
                    elif re.match(r"^\s*#\s*endif", lines[j]):
                        depth -= 1
                        if depth == 0:
                            break
                    body.append(lines[j])
                    j += 1
                if j < len(lines) and depth == 0 and not [
                        b for b in body if b.strip()]:
                    did = True
                    i = j + 1
                    continue
            if re.match(r"^\s*if\s*\(", ln) and ln.rstrip().endswith("{"):
                j = i + 1
                while j < len(lines) and not lines[j].strip():
                    j += 1
                if j < len(lines) and lines[j].strip() in ("}", "endif()"):
                    k = j + (1 if lines[j].strip() == "}" else 1)
                    did = True
                    i = k
                    continue
            out.append(ln)
            i += 1
        lines = out
        changed = changed or did
        if not did:
            break
    return lines, changed


def main():
    if len(sys.argv) < 2:
        die("usage: strip.py <tree> [stage ...]")
    tree = sys.argv[1]
    named = sys.argv[2:]
    for s in named:
        if s not in STAGES:
            die("unknown stage %r" % s)

    removed_files = 0
    for rel in ALWAYS_REMOVE_DIRS:
        p = os.path.join(tree, rel)
        if os.path.isdir(p):
            import shutil
            shutil.rmtree(p)
            removed_files += 1
    for rel in ALWAYS_REMOVE:
        p = os.path.join(tree, rel)
        if os.path.exists(p):
            os.remove(p)
            removed_files += 1

    seams = {}
    for name, st in STAGES.items():
        if name not in named:
            continue
        for rel in st["files"]:
            p = os.path.join(tree, rel)
            if os.path.exists(p):
                os.remove(p)
                removed_files += 1
        seams.setdefault(st["seam"], [set(), set()])
        seams[st["seam"]][0].update(st["syms"])
        seams[st["seam"]][1].update(st.get("calls", []))

    # every level loses the always-removed symbols from the same seams
    seam_of = {st["seam"] for st in STAGES.values()}
    for s in seam_of:
        seams.setdefault(s, [set(), set()])[0].update(ALWAYS_SYMS)
        seams[s][1].update(ALWAYS_CALLS)
    # the avx512 encoder symbols are removed because their file always is
    seams[STAGES["blk_encode"]["seam"]][0].add("ojph_encode_codeblock_avx512")

    all_isa = set(ALWAYS_SYMS)
    for st in STAGES.values():
        all_isa.update(st["syms"])
    all_isa.add("ojph_encode_codeblock_avx512")

    # an empty directory cannot come from a checkout: one that is empty now is
    # a signpost for a file that was here
    for root, dirs, files in os.walk(tree, topdown=False):
        if "/.git" in root or root.endswith("/.git"):
            continue
        if not os.listdir(root):
            try:
                os.rmdir(root)
            except OSError:
                pass

    n_scrub = 0
    for name, st in STAGES.items():
        if name not in named:
            continue
        for rel, syms in st.get("scrub", []):
            path = os.path.join(tree, rel)
            if not os.path.exists(path):
                continue
            lines = open(path).read().split("\n")
            whole = "".join(lines)
            before = (whole.count("{"), whole.count("}"))
            for sym in syms:
                lines, k = _remove_symbol(lines, sym)
                n_scrub += k
            whole2 = "".join(lines)
            if before[0] != before[1] or whole2.count("{") != whole2.count("}"):
                die("%s: unbalanced braces while scrubbing %s" % (path, syms))
            open(path, "w").write("\n".join(lines))

    n_lines = 0
    for seam, (syms, calls) in seams.items():
        kept = all_isa - syms
        n_lines += strip_seam(os.path.join(tree, seam), syms, calls,
                              sorted(kept))

    decl_files = set()
    for name, st in STAGES.items():
        decl_files.update(st["decl_in"])
    decl_files.update([
        "src/core/coding/ojph_block_decoder.h",
        "src/core/coding/ojph_block_encoder.h",
        "src/core/transform/ojph_transform_local.h",
        "src/core/transform/ojph_colour_local.h",
    ])
    all_removed = set(ALWAYS_SYMS)
    for name, st in STAGES.items():
        if name in named:
            all_removed.update(st["syms"])
    all_removed.add("ojph_encode_codeblock_avx512")
    all_calls = set(ALWAYS_CALLS)
    for name, st in STAGES.items():
        if name in named:
            all_calls.update(st.get("calls", []))
    n_decls = strip_decls([os.path.join(tree, f) for f in sorted(decl_files)],
                          sorted(all_removed), sorted(all_calls))
    n_cmake = strip_cmake(tree)

    print("strip.py: %d files removed, %d seam lines, %d declarations, "
          "%d cmake lines, %d scrub" % (removed_files, n_lines, n_decls,
                                        n_cmake, n_scrub))


if __name__ == "__main__":
    main()
