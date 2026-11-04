#!/usr/bin/env python3
"""Remove one or more optimization stages from an srsRAN_Project source tree.

Usage:
    strip.py <tree> <stage> [<stage> ...]

The tree is modified in place. The operation is idempotent: stripping a stage
that is already absent is a no-op.

A "stage" is one pipelined DSP kernel of the 5G NR physical layer plus every
per-ISA body that implements it, for every architecture -- not just x86. Each
stage owns:

  * the per-ISA implementation files of that kernel (avx2 / avx512 / neon),
  * any shared support header whose *earliest* user is that stage,
  * the non-portable arms of the factory that selects the kernel's
    implementation, and
  * the corresponding entries in the CMake source lists.

The portable reference implementation of every kernel is left untouched: it is
both the correctness reference and the scoring denominator.
"""

import os
import re
import sys

# --------------------------------------------------------------------------
# Stage definitions.
#
# "files"  : paths relative to the tree root, removed when the stage is
#            stripped. A file shared by two stages is listed under the stage
#            that comes first in the ladder, so that the ladder restores it
#            before the later stage needs it.
# "symbols": implementation class names of this stage. Used to find the
#            factory arms and the preprocessor blocks that must go.
# "guards" : files whose architecture-guarded blocks are pruned, and the
#            preprocessor conditions that mark such a block.
# "cmake"  : CMakeLists.txt files whose statements naming a removed file are
#            pruned.
# --------------------------------------------------------------------------
LDPC = "lib/phy/upper/channel_coding/ldpc/"
MOD = "lib/phy/upper/channel_modulation/"
OFH = "lib/ofh/compression/"

STAGES = {
    "ldpc_encode": {
        "files": [
            LDPC + "ldpc_encoder_avx2.cpp",
            LDPC + "ldpc_encoder_avx2.h",
            LDPC + "ldpc_encoder_neon.cpp",
            LDPC + "ldpc_encoder_neon.h",
            # Shared by the encoder, the rate dematcher and the decoder; owned
            # here because this stage is the first of the three in the ladder.
            LDPC + "avx2_support.h",
            LDPC + "avx512_support.h",
            LDPC + "neon_support.h",
        ],
        "symbols": ["ldpc_encoder_avx2", "ldpc_encoder_neon"],
        "guards": [("lib/phy/upper/channel_coding/channel_coding_factories.cpp",
                    ["__x86_64__", "__aarch64__"])],
        "cmake": [LDPC + "CMakeLists.txt"],
    },
    "ldpc_rate_dematch": {
        "files": [
            LDPC + "ldpc_rate_dematcher_avx2_impl.cpp",
            LDPC + "ldpc_rate_dematcher_avx2_impl.h",
            LDPC + "ldpc_rate_dematcher_avx512_impl.cpp",
            LDPC + "ldpc_rate_dematcher_avx512_impl.h",
            LDPC + "ldpc_rate_dematcher_neon_impl.cpp",
            LDPC + "ldpc_rate_dematcher_neon_impl.h",
        ],
        "symbols": ["ldpc_rate_dematcher_avx2_impl",
                    "ldpc_rate_dematcher_avx512_impl",
                    "ldpc_rate_dematcher_neon_impl"],
        "guards": [("lib/phy/upper/channel_coding/channel_coding_factories.cpp",
                    ["__x86_64__", "__aarch64__"])],
        "cmake": [LDPC + "CMakeLists.txt"],
    },
    "ldpc_decode": {
        "files": [
            LDPC + "ldpc_decoder_avx2.cpp",
            LDPC + "ldpc_decoder_avx2.h",
            LDPC + "ldpc_decoder_avx512.cpp",
            LDPC + "ldpc_decoder_avx512.h",
            LDPC + "ldpc_decoder_neon.cpp",
            LDPC + "ldpc_decoder_neon.h",
        ],
        "symbols": ["ldpc_decoder_avx2", "ldpc_decoder_avx512", "ldpc_decoder_neon"],
        # The check-to-variable scaling helper lives in the shared support
        # headers but is used by the decoders alone: leaving it behind would
        # hand the candidate one of this kernel's four sub-kernels, written out.
        "funcs": [(LDPC + "avx2_support.h", "inline __m256i scale_epi8("),
                  (LDPC + "avx512_support.h", "inline __m512i scale_epi8("),
                  (LDPC + "neon_support.h", "inline int8x16_t scale_s8(")],
        "guards": [("lib/phy/upper/channel_coding/channel_coding_factories.cpp",
                    ["__x86_64__", "__aarch64__"])],
        "cmake": [LDPC + "CMakeLists.txt"],
    },
    "precode": {
        "files": [
            "lib/phy/generic_functions/precoding/channel_precoder_avx2.cpp",
            "lib/phy/generic_functions/precoding/channel_precoder_avx2.h",
            "lib/phy/generic_functions/precoding/channel_precoder_avx512.cpp",
            "lib/phy/generic_functions/precoding/channel_precoder_avx512.h",
            "lib/phy/generic_functions/precoding/channel_precoder_neon.cpp",
            "lib/phy/generic_functions/precoding/channel_precoder_neon.h",
        ],
        "symbols": ["channel_precoder_avx2", "channel_precoder_avx512", "channel_precoder_neon"],
        "guards": [("lib/phy/generic_functions/precoding/precoding_factories.cpp",
                    ["__x86_64__", "__ARM_NEON"])],
        "cmake": ["lib/phy/generic_functions/precoding/CMakeLists.txt"],
    },
    "demodulate": {
        # The demodulation mapper has no factory string: it is  selected  by
        # __AVX2__ inside the translation unit. The stage therefore owns the
        # vectorised bodies themselves, and the support headers they include.
        "files": [
            MOD + "avx2_helpers.h",
            MOD + "avx512_helpers.h",
            MOD + "neon_helpers.h",
        ],
        "symbols": [],
        "guards": [(MOD + "demodulation_mapper_qpsk.cpp",
                    ["__AVX2__", "__AVX512", "HAVE_AVX512", "__ARM_NEON"]),
                   (MOD + "demodulation_mapper_qam16.cpp",
                    ["__AVX2__", "__AVX512", "HAVE_AVX512", "__ARM_NEON"]),
                   (MOD + "demodulation_mapper_qam64.cpp",
                    ["__AVX2__", "__AVX512", "HAVE_AVX512", "__ARM_NEON"]),
                   (MOD + "demodulation_mapper_qam256.cpp",
                    ["__AVX2__", "__AVX512", "HAVE_AVX512", "__ARM_NEON"])],
        "cmake": [],
    },
    "ofh_bfp": {
        "files": [
            OFH + "iq_compression_bfp_avx2.cpp",
            OFH + "iq_compression_bfp_avx2.h",
            OFH + "iq_compression_bfp_avx512.cpp",
            OFH + "iq_compression_bfp_avx512.h",
            OFH + "iq_compression_bfp_neon.cpp",
            OFH + "iq_compression_bfp_neon.h",
            # Shared with the uncompressed-path stage; owned here because this
            # stage comes first in the ladder.
            OFH + "packing_utils_avx2.h",
            OFH + "packing_utils_avx512.h",
            OFH + "packing_utils_neon.h",
            OFH + "avx2_helpers.h",
            OFH + "avx512_helpers.h",
            OFH + "neon_helpers.h",
        ],
        "symbols": ["iq_compression_bfp_avx2", "iq_compression_bfp_avx512", "iq_compression_bfp_neon"],
        "guards": [(OFH + "compression_factory.cpp",
                    ["__x86_64__", "__ARM_NEON"])],
        "cmake": [OFH + "CMakeLists.txt"],
    },
    "ofh_pack": {
        "files": [
            OFH + "iq_compression_none_avx2.cpp",
            OFH + "iq_compression_none_avx2.h",
            OFH + "iq_compression_none_avx512.cpp",
            OFH + "iq_compression_none_avx512.h",
            OFH + "iq_compression_none_neon.cpp",
            OFH + "iq_compression_none_neon.h",
        ],
        "symbols": ["iq_compression_none_avx2", "iq_compression_none_avx512", "iq_compression_none_neon"],
        "guards": [(OFH + "compression_factory.cpp",
                    ["__x86_64__", "__ARM_NEON"])],
        "cmake": [OFH + "CMakeLists.txt"],
    },
}

# Ladder order. A stage's shared support header is owned by the earliest stage
# in this list that needs it.
STAGE_ORDER = ["ldpc_encode", "ldpc_rate_dematch", "ldpc_decode", "precode",
               "demodulate", "ofh_bfp", "ofh_pack"]


def strip_region(lines, conds):
    """Remove every preprocessor conditional block whose condition mentions
    one of `conds`.

    Nesting-aware: the matching #endif is found by counting, so a block that
    contains further conditionals is removed whole.
    """
    out = []
    skip_depth = 0          # >0 while inside a block being dropped
    depth = 0               # current #if nesting depth overall
    for line in lines:
        stripped = line.lstrip()
        is_if = (stripped.startswith("#if ") or stripped.startswith("#ifdef ")
                 or stripped.startswith("#ifndef"))
        if skip_depth == 0 and is_if:
            depth += 1
            if any(c in stripped for c in conds):
                skip_depth = depth
            else:
                out.append(line)
            continue
        if skip_depth == 0:
            if stripped.startswith("#endif"):
                depth = max(0, depth - 1)
            out.append(line)
            continue
        # inside a dropped block: only track nesting
        if is_if:
            depth += 1
        elif stripped.startswith("#endif"):
            if depth == skip_depth:
                skip_depth = 0
            depth = max(0, depth - 1)
    return out


def block_mentions(lines, symbols):
    return any(any(s in ln for s in symbols) for ln in lines)


def strip_factory_arms(lines, symbols, conds):
    """Drop architecture-guarded conditional blocks that name one of the
    stage's implementation symbols -- both the #include blocks and the
    dispatch arms."""
    out = []
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        st = line.lstrip()
        if (st.startswith("#if ") or st.startswith("#ifdef ") or st.startswith("#ifndef")) \
                and any(c in st for c in conds):
            # find the matching #endif
            depth = 0
            j = i
            while j < n:
                sj = lines[j].lstrip()
                if sj.startswith("#if ") or sj.startswith("#ifdef ") or sj.startswith("#ifndef"):
                    depth += 1
                elif sj.startswith("#endif"):
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            body = lines[i:j + 1]
            if block_mentions(body, symbols):
                i = j + 1
                continue
        out.append(line)
        i += 1
    return out


def remove_function(text, signature):
    """Remove `signature`'s definition, its leading doc comment and its body.

    Located by brace matching, never by a non-greedy regex: a regex match has
    swallowed a neighbouring function before, and the tree then stops linking."""
    lines = text.split("\n")
    start = None
    for i, ln in enumerate(lines):
        if signature in ln:
            start = i
            break
    if start is None:
        return text
    # Walk back over the contiguous comment block that documents it.
    first = start
    while first > 0:
        prev = lines[first - 1].strip()
        if prev.startswith("///") or prev.startswith("//") or prev.startswith("*") or prev.startswith("/*"):
            first -= 1
        else:
            break
    depth = 0
    end = None
    for j in range(start, len(lines)):
        depth += lines[j].count("{") - lines[j].count("}")
        if depth == 0 and "{" in "".join(lines[start:j + 1]):
            end = j
            break
    if end is None:
        return text
    return "\n".join(lines[:first] + lines[end + 1:])


def drop_includes(lines, basenames):
    """Remove `#include "x.h"` lines naming a removed file."""
    out = []
    for line in lines:
        m = re.match(r'\s*#include\s+"([^"]+)"', line)
        if m and os.path.basename(m.group(1)) in basenames:
            continue
        out.append(line)
    return out


def cmake_logical_statements(text):
    """Split a CMakeLists into logical statements, keeping the original text.

    A statement ends on a line with balanced parentheses that does not end in a
    backslash. Returns a list of (text, start_line_index)."""
    lines = text.split("\n")
    stmts = []
    buf = []
    depth = 0
    for idx, line in enumerate(lines):
        buf.append(line)
        depth += line.count("(") - line.count(")")
        if depth <= 0 and not line.rstrip().endswith("\\"):
            depth = 0
            stmts.append(("\n".join(buf), idx + 1 - len(buf)))
            buf = []
    if buf:
        stmts.append(("\n".join(buf), len(lines) - len(buf)))
    return stmts


def strip_cmake(path, basenames):
    with open(path) as f:
        text = f.read()
    out_parts = []
    for stmt, _ in cmake_logical_statements(text):
        toks = [t for t in re.findall(r"[A-Za-z0-9_./+-]+\.(?:cpp|h)", stmt)]
        hit = [t for t in toks if os.path.basename(t) in basenames]
        if not hit:
            out_parts.append(stmt)
            continue
        if stmt.lstrip().startswith("list(APPEND"):
            # Drop only the entry lines that name a removed file; the other
            # entries of the same list must survive.  The closing parenthesis
            # lives on whichever entry line happens to be last, so it has to be
            # restored when that line goes.
            kept = []
            for ln in stmt.split("\n"):
                names = re.findall(r"[A-Za-z0-9_./+-]+\.cpp", ln)
                if names and all(os.path.basename(x) in basenames for x in names):
                    continue
                kept.append(ln)
            text = "\n".join(kept)
            text += ")" * (text.count("(") - text.count(")"))
            out_parts.append(text)
            continue
        # Any other statement that names a removed file goes as a whole.
        out_parts.append("")
    with open(path, "w") as f:
        f.write("\n".join(out_parts))


def strip_stage(tree, stage):
    spec = STAGES[stage]
    removed = set()
    for rel in spec["files"]:
        p = os.path.join(tree, rel)
        if os.path.exists(p):
            os.remove(p)
        removed.add(os.path.basename(rel))
        removed.add(rel)

    for rel, signature in spec.get("funcs", []):
        p = os.path.join(tree, rel)
        if not os.path.exists(p):
            continue
        with open(p) as f:
            text = f.read()
        with open(p, "w") as f:
            f.write(remove_function(text, signature))

    for rel, conds in spec["guards"]:
        p = os.path.join(tree, rel)
        if not os.path.exists(p):
            continue
        with open(p) as f:
            lines = f.read().split("\n")
        lines = drop_includes(lines, removed)
        if spec["symbols"]:
            lines = strip_factory_arms(lines, spec["symbols"], conds)
        else:
            lines = strip_region(lines, conds)
        with open(p, "w") as f:
            f.write("\n".join(lines))

    for rel in spec["cmake"]:
        p = os.path.join(tree, rel)
        if os.path.exists(p):
            strip_cmake(p, removed)


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    tree = argv[1]
    if not os.path.isdir(os.path.join(tree, "lib")):
        sys.stderr.write("strip.py: %s does not look like an srsRAN tree\n" % tree)
        return 2
    for stage in argv[2:]:
        if stage not in STAGES:
            sys.stderr.write("strip.py: unknown stage %r (known: %s)\n"
                             % (stage, " ".join(STAGE_ORDER)))
            return 2
        strip_stage(tree, stage)
        print("stripped %s" % stage)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
