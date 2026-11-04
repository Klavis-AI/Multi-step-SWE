#!/usr/bin/env python3
"""Derive one rung of the SVT-JPEG-XS ladder.

    strip.py <tree> [stage ...]

Each named stage is REMOVED from <tree>: the per-ISA bodies of the runtime
dispatch slots that stage owns are deleted from the tree, and those slots are
rewired to `SET_ONLY_C(slot, slot_c)`.  The portable C reference for every slot
stays -- it is the correctness reference and the speed denominator, and the
result is a complete, conforming JPEG XS codec rather than a mutilated tree.

Slots whose stage is `never` (out of scope at the AVX2 ceiling for the whole
ladder: packing, dequantisation, the AVX-512-only rate-control variant and the
trivial log2 asm) are removed unconditionally, at every rung.

Idempotent, and stripping {A, B} in one call equals stripping A then B.
"""
import os
import re
import sys

LADDER = ["nlt_enc", "dwt_fwd", "quant", "gcli", "rate_ctrl", "unpack", "idwt", "nlt_dec"]

# ---------------------------------------------------------------- slot table
# rtcd slot name -> stage.  Census from Source/Lib/{Encoder,Decoder,Common}/
# Codec/*_dsp_rtcd.c at the pin.
SLOT_STAGE = {
    "image_shift": "nlt_enc",
    "linear_input_scaling_line_8bit": "nlt_enc",
    "linear_input_scaling_line_16bit": "nlt_enc",
    "linear_input_scaling_line_16bit_msb": "nlt_enc",
    "convert_packed_to_planar_rgb_8bit": "nlt_enc",
    "convert_packed_to_planar_rgb_16bit": "nlt_enc",
    "dwt_horizontal_line": "dwt_fwd",
    "transform_V1_Hx_precinct_recalc_HF_prev": "dwt_fwd",
    "transform_vertical_loop_hf_line_0": "dwt_fwd",
    "transform_vertical_loop_lf_line_0": "dwt_fwd",
    "transform_vertical_loop_lf_hf_line_0": "dwt_fwd",
    "transform_vertical_loop_lf_hf_line_x_prev": "dwt_fwd",
    "transform_vertical_loop_lf_hf_hf_line_x": "dwt_fwd",
    "transform_vertical_loop_lf_hf_hf_line_last_even": "dwt_fwd",
    "quantization": "quant",
    "gc_precinct_stage_scalar": "gcli",
    "gc_precinct_stage_scalar_loop": "gcli",
    "gc_histogram_16": "gcli",
    "gc_precinct_sigflags_max": "gcli",
    "rate_control_calc_vpred_cost_nosigf": "rate_ctrl",
    "unpack_data": "unpack",
    "idwt_horizontal_line_lf16_hf16": "idwt",
    "idwt_horizontal_line_lf32_hf16": "idwt",
    "idwt_vertical_line": "idwt",
    "idwt_vertical_line_recalc": "idwt",
    "linear_output_scaling_8bit": "nlt_dec",
    "linear_output_scaling_8bit_line": "nlt_dec",
    "linear_output_scaling_16bit": "nlt_dec",
    "linear_output_scaling_16bit_line": "nlt_dec",
    "linear_output_scaling_16bit_line_msb": "nlt_dec",
    # out of scope for the whole ladder
    "pack_data_groups": "never",
    "pack_data_single_group": "never",
    "dequant": "never",
    "inv_sign": "never",
    "rate_control_calc_vpred_cost_sigf_nosigf": "never",
    "svt_log2_32": "never",
}

RTCD_FILES = [
    "Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c",
    "Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c",
    "Source/Lib/Common/Codec/common_dsp_rtcd.c",
]

# --------------------------------------------------------------- file table
# Whole-file stages: file -> stage that owns every function in it.
FILE_STAGE = {
    "Source/Lib/Encoder/ASM_AVX2/NltEnc_avx2.c": "nlt_enc",
    "Source/Lib/Encoder/ASM_AVX2/NltEnc_avx2.h": "nlt_enc",
    "Source/Lib/Encoder/ASM_AVX2/Dwt_AVX2.c": "dwt_fwd",
    "Source/Lib/Encoder/ASM_AVX2/Dwt_AVX2.h": "dwt_fwd",
    "Source/Lib/Encoder/ASM_AVX2/Quant_avx2.c": "quant",
    "Source/Lib/Encoder/ASM_AVX2/Quant_avx2.h": "quant",
    "Source/Lib/Encoder/ASM_SSE4_1/Quant_sse4_1.c": "quant",
    "Source/Lib/Encoder/ASM_SSE4_1/Quant_sse4_1.h": "quant",
    "Source/Lib/Encoder/ASM_SSE4_1/group_coding_sse4_1.c": "gcli",
    "Source/Lib/Encoder/ASM_SSE4_1/group_coding_sse4_1.h": "gcli",
    "Source/Lib/Encoder/ASM_SSE2/ASM_SSE2.asm": "gcli",
    "Source/Lib/Encoder/ASM_AVX2/rate_control_helper_avx2.h": "rate_ctrl",
    "Source/Lib/Decoder/ASM_AVX2/Dwt53Decoder_AVX2.c": "idwt",
    "Source/Lib/Decoder/ASM_AVX2/Dwt53Decoder_AVX2.h": "idwt",
    "Source/Lib/Decoder/ASM_AVX2/UnPack_avx2.c": "unpack",
    "Source/Lib/Decoder/ASM_AVX2/UnPack_avx2.h": "unpack",
    "Source/Lib/Decoder/ASM_AVX2/unpack_common.h": "unpack",
    "Source/Lib/Decoder/ASM_SSE4_1/Dequant_SSE4.c": "never",
    "Source/Lib/Decoder/ASM_SSE4_1/Dequant_SSE4.h": "never",
    "Source/Lib/Encoder/ASM_AVX2/Pack_avx2.c": "never",
    "Source/Lib/Encoder/ASM_AVX2/Pack_avx2.h": "never",
    "Source/Lib/Encoder/ASM_AVX2/pack_group_helper.h": "never",
}

# Files that carry several stages' functions: file -> {function: stage}.  A
# level keeps a function iff its stage survives; the file is deleted when it
# ends up with none.
FILE_PARTS = {
    "Source/Lib/Encoder/ASM_AVX2/RateControl_avx2.c": {
        "convert_packed_to_planar_rgb_8bit_avx2": "nlt_enc",
        "convert_packed_to_planar_rgb_16bit_avx2": "nlt_enc",
        "gc_precinct_stage_scalar_avx2": "gcli",
        "gc_histogram_16_avx2": "gcli",
        "rate_control_calc_vpred_cost_nosigf_avx2": "rate_ctrl",
    },
    "Source/Lib/Encoder/ASM_AVX2/RateControl_avx2.h": {
        "convert_packed_to_planar_rgb_8bit_avx2": "nlt_enc",
        "convert_packed_to_planar_rgb_16bit_avx2": "nlt_enc",
        "gc_precinct_stage_scalar_avx2": "gcli",
        "gc_histogram_16_avx2": "gcli",
        "rate_control_calc_vpred_cost_nosigf_avx2": "rate_ctrl",
    },
    "Source/Lib/Decoder/ASM_AVX2/NltDec_AVX2.c": {
        "linear_output_scaling_8bit_avx2": "nlt_dec",
        "linear_output_scaling_8bit_line_avx2": "nlt_dec",
        "linear_output_scaling_16bit_avx2": "nlt_dec",
        "linear_output_scaling_16bit_line_avx2": "nlt_dec",
        "linear_output_scaling_16bit_line_msb_avx2": "nlt_dec",
        "inv_sign_avx2": "never",
    },
    "Source/Lib/Decoder/ASM_AVX2/NltDec_AVX2.h": {
        "linear_output_scaling_8bit_avx2": "nlt_dec",
        "linear_output_scaling_8bit_line_avx2": "nlt_dec",
        "linear_output_scaling_16bit_avx2": "nlt_dec",
        "linear_output_scaling_16bit_line_avx2": "nlt_dec",
        "linear_output_scaling_16bit_line_msb_avx2": "nlt_dec",
        "inv_sign_avx2": "never",
    },
}

# Macros whose last argument is the AVX-512 symbol.  AVX-512 is out of scope on
# both sides of every step, so a surviving SET_* call passes NULL there.
# Only these carry an AVX-512 symbol as their LAST argument.  The macros whose
# last argument is the AVX2 symbol (SET_AVX2, SET_SSE2_AVX2, SET_SSSE3_AVX2,
# SET_SSE41_AVX2) keep it: nulling it would silently delete the tier this
# ladder is measured at.
AVX512_ARG_MACRO = {
    "SET_AVX2_AVX512", "SET_SSE2_AVX512", "SET_SSE41_AVX2_AVX512", "SET_AVX512",
}


# ------------------------------------------------------------------ helpers
def strip_comments(text):
    """Blank out comments and string/char literals, keeping offsets stable."""
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
                out[k] = " "
            i = j
        elif c in "\"'":
            q, i = c, i + 1
            while i < n and text[i] != q:
                if text[i] == "\\":
                    i += 1
                if i < n and out[i] != "\n":
                    out[i] = " "
                i += 1
            i += 1
        else:
            i += 1
    return "".join(out)


def top_level_defs(text):
    """Yield (start, end, name) for each top-level function DEFINITION.

    Preprocessor lines and declarations are skipped and never become a span,
    so a dropped definition never takes an #include with it.
    """
    code = strip_comments(text)
    n = len(code)
    i = 0
    span_start = None
    while i < n:
        c = code[i]
        if c == "\n":
            if span_start is not None and code[span_start] == "#":
                span_start = None
            i += 1
            continue
        if c.isspace():
            i += 1
            continue
        if span_start is None:
            span_start = i
        if code[span_start] == "#":
            while i < n and code[i] != "\n":
                i += 1
            span_start = None
            continue
        if c == ";":
            span_start = None
            i += 1
            continue
        if c == "{":
            head = code[span_start:i]
            m = re.search(r"([A-Za-z_]\w*)\s*\([^;{]*\)\s*$", head)
            depth, j = 1, i + 1
            while j < n and depth:
                if code[j] == "{":
                    depth += 1
                elif code[j] == "}":
                    depth -= 1
                j += 1
            yield (span_start, j, m.group(1) if m else "")
            span_start = None
            i = j
            continue
        i += 1


def keep_parts(text, wanted, managed):
    """Drop every top-level definition of a managed function that is not wanted."""
    drop = [(s, e) for (s, e, name) in top_level_defs(text)
            if name in managed and name not in wanted]
    if not drop:
        return text
    out, last = [], 0
    for start, end in sorted(drop):
        out.append(text[last:start])
        last = end
    out.append(text[last:])
    return "".join(out)


def find_includes(text, existing):
    def repl(m):
        return m.group(0) if os.path.basename(m.group(1)) in existing else ""
    return re.sub(r'^[ \t]*#include[ \t]+"([^"]+)"[ \t]*\r?\n', repl, text, flags=re.M)


def all_headers(tree):
    names = set()
    for root, _dirs, files in os.walk(tree):
        for f in files:
            if f.endswith((".h", ".hh", ".hpp", ".inc", ".asm")):
                names.add(f)
    return names


def match_call(text, open_paren):
    depth, i = 0, open_paren
    while i < len(text):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def rewrite_rtcd(text, removed):
    out, pos = [], 0
    for m in re.finditer(r"\b(SET_\w+)\s*\(", text):
        if m.start() < pos:
            continue
        close = match_call(text, text.index("(", m.start()))
        if close < 0:
            continue
        end = text.find(";", close)
        if end < 0:
            continue
        end += 1
        stmt = text[m.start():end]
        args = [a.strip() for a in stmt[stmt.index("(") + 1:stmt.rindex(")")].split(",")]
        slot = args[0] if args else ""
        stage = SLOT_STAGE.get(slot)
        out.append(text[pos:m.start()])
        if stage is None:
            out.append(stmt)
        elif stage in removed:
            cref = args[1] if len(args) >= 2 else slot + "_c"
            out.append("SET_ONLY_C(%s, %s);" % (slot, cref))
        elif m.group(1) in AVX512_ARG_MACRO and len(args) >= 3:
            args[-1] = "NULL"
            out.append("%s(%s);" % (m.group(1), ", ".join(args)))
        else:
            out.append(stmt)
        pos = end
    out.append(text[pos:])
    text = "".join(out)

    # host-BMI2 overrides name a stage's symbol directly; drop the ones whose
    # stage is out of this level (the SET_ONLY_C above would be undone by them).
    for sym in ("pack_data_groups", "unpack_data"):
        if SLOT_STAGE[sym] in removed:
            text = re.sub(
                r"[ \t]*if \(\(flags & CPU_FLAGS_AVX2\)[^\n]*\n"
                r"[ \t]*%s = %s_avx2_bmi2;\n[ \t]*\}\n" % (sym, sym), "", text)
    return text


# -------------------------------------------------------------------- main
def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    tree, stages = sys.argv[1], sys.argv[2:]
    for s in stages:
        if s not in LADDER:
            sys.exit("strip.py: unknown stage %r (known: %s)" % (s, " ".join(LADDER)))
    removed = set(stages) | {"never"}

    for rel, stage in FILE_STAGE.items():
        if stage in removed:
            p = os.path.join(tree, rel)
            if os.path.exists(p):
                os.remove(p)

    for rel, parts in FILE_PARTS.items():
        p = os.path.join(tree, rel)
        if not os.path.exists(p):
            continue
        text = open(p, encoding="utf-8", errors="surrogateescape").read()
        wanted = {fn for fn, st in parts.items() if st not in removed}
        text = keep_parts(text, wanted, set(parts))
        if not wanted or not any(re.search(r"\b%s\b" % re.escape(fn), text) for fn in wanted):
            os.remove(p)
        else:
            open(p, "w", encoding="utf-8", errors="surrogateescape").write(text)

    for rel in RTCD_FILES:
        p = os.path.join(tree, rel)
        text = open(p, encoding="utf-8", errors="surrogateescape").read()
        new = rewrite_rtcd(text, removed)
        if new != text:
            open(p, "w", encoding="utf-8", errors="surrogateescape").write(new)

    existing = all_headers(tree)
    for root, _dirs, files in os.walk(os.path.join(tree, "Source")):
        for f in files:
            if not f.endswith((".c", ".h")):
                continue
            p = os.path.join(root, f)
            text = open(p, encoding="utf-8", errors="surrogateescape").read()
            new = find_includes(text, existing)
            if new != text:
                open(p, "w", encoding="utf-8", errors="surrogateescape").write(new)

    print("stripped %s: removed %s" % (tree, " ".join(sorted(stages)) or "(nothing)"))


if __name__ == "__main__":
    main()
