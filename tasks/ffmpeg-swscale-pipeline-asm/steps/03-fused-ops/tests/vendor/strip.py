#!/usr/bin/env python3
"""Strip the libswscale ladder's stages out of an FFmpeg source tree.

    strip.py <tree> [stage ...]

Removes the named stages (and, when any stage is named, every non-x86 twin of
every stage) from a pristine checkout. With no stage named it is a no-op, which
is how the fully-restored level is produced and how losslessness is checked.

Why this shape. libswscale ships each of these stages twice over: the portable
C implementation in `libswscale/*.c`, and optimized implementations in
`libswscale/x86/` (nasm and C-intrinsics), `libswscale/{aarch64,arm,ppc,
loongarch,riscv}/`, `libswscale/vulkan/`, plus a second x86 copy of the YUV to
RGB matrix filter one library over in `libavfilter/x86/colorspacedsp.asm`.
Removing one implementation leaves the others as a translation exercise, so a
stage is removed *everywhere it exists*, not in one directory.

The portable C is never touched: it is the correctness reference and the
denominator the step is scored against, so it must survive every level.

Deletion is by explicit line ranges (see ranges.json), not by pattern: a
non-greedy regex can swallow a neighbouring function and stop the tree
linking. Ranges are asserted to be complete constructs by the
one check that matters -- every level is compiled and run by the ladder build.

The dispatch entry points stay. `ff_sws_init_swscale_x86`, `rgb2rgb_init_x86`,
`ff_yuv2rgb_init_x86` and `x86/ops.c`'s `backend_x86` are all present at every
level; a level simply does not assign the fields whose kernels are absent, which
is exactly the shape upstream's own `--disable-x86asm` build has. That is why
the base reads as an ordinary configuration rather than as a mutilated tree.
"""
import json
import os
import pathlib
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent

# Ladder order. Index in this list is the level at which the stage appears:
# level k has stages[0:k]. `strip.py` takes the stages to REMOVE.
STAGES = ["in", "rgb", "ops", "hs", "out", "yuv2rgb"]

# ---------------------------------------------------------------------------
# Files that belong to exactly one stage and are deleted whole.
# ---------------------------------------------------------------------------
STAGE_FILES = {
    "in":      ["libswscale/x86/input.asm"],
    "rgb":     ["libswscale/x86/rgb_2_rgb.asm"],
    "ops":     ["libswscale/x86/ops_common.asm",
                "libswscale/x86/ops_int.asm",
                "libswscale/x86/ops_float.asm",
                "libswscale/x86/ops_include.asm",
                "libswscale/x86/uops_macros.asm.h"],
    "hs":      ["libswscale/x86/scale.asm",
                "libswscale/x86/scale_avx2.asm",
                "libswscale/x86/hscale_fast_bilinear_simd.c"],
    "out":     ["libswscale/x86/output.asm",
                "libswscale/x86/yuv2yuvX.asm",
                # Legacy MMX template, #included by x86/swscale.c under
                # HAVE_INLINE_ASM (0 in every level's configure, so it is never
                # compiled). It is a complete readable implementation of the
                # yuv2packed family -- a sibling file left as a worked
                # example -- so it goes when the stage goes.
                "libswscale/x86/swscale_template.c"],
    "yuv2rgb": ["libswscale/x86/yuv_2_rgb.asm"],
}

# ---------------------------------------------------------------------------
# Non-x86 twins. Removed whenever ANY stage is removed, because each of them
# contains implementations of the stages this ladder strips -- aarch64's
# hscale.S is a complete polyphase horizontal scaler, loongarch's
# output_lasx.c a complete yuv2packed family, ppc's swscale_vsx.c both, and
# aarch64/ops*.c plus vulkan/ops.c are second and third backends for the fused
# operation engine. Leaving them is handing over the answer in a different
# dialect. They are never built on x86_64, so removing them cannot change any
# measurement.
# ---------------------------------------------------------------------------
CROSS_ARCH_DIRS = [
    "libswscale/aarch64",
    "libswscale/arm",
    "libswscale/ppc",
    "libswscale/loongarch",
    "libswscale/riscv",
    "libswscale/vulkan",
]

# A second x86 implementation of the YUV/RGB colour matrix, one library over,
# not covered by the libswscale strip.
OUT_OF_TREE = [
    "libavfilter/x86/colorspacedsp.asm",
    "libavfilter/x86/colorspacedsp_init.c",
]

# ---------------------------------------------------------------------------
# Files whose per-level form is produced by deleting tagged line ranges.
# ---------------------------------------------------------------------------
RANGED_FILES = [
    "libswscale/x86/swscale.c",
    "libswscale/x86/rgb2rgb.c",
    "libswscale/x86/yuv2rgb.c",
]


def load_ranges():
    with open(HERE / "ranges.json") as fh:
        raw = json.load(fh)
    out = {}
    for path, ranges in raw.items():
        if path.startswith("_"):
            continue
        out[path] = [(r[0], int(r[1]), int(r[2])) for r in ranges]
    return out


def materialise(text, ranges, removed):
    """Delete every range whose stage is in `removed`. Bottom-up."""
    lines = text.split("\n")
    keep = [True] * (len(lines) + 1)          # 1-based
    for stage, a, b in ranges:
        if stage not in removed:
            continue
        assert 1 <= a <= b <= len(lines), "range %s %d-%d out of %d lines" % (stage, a, b, len(lines))
        for i in range(a, b + 1):
            keep[i] = False
    return "\n".join(l for i, l in enumerate(lines, 1) if keep[i])


def write_makefile(tree, present):
    """Regenerate libswscale/x86/Makefile for the set of present stages.

    Upstream's file is one flat list of object files; a level simply has fewer
    entries. Written line by line rather than from a template so that an empty
    list produces no dangling continuation backslash.
    """
    # x86/rgb2rgb.c and x86/yuv2rgb.c stay in the tree at every level: their
    # dispatch entry points are called unconditionally from files outside the
    # seam, so they keep a definition that installs nothing when the stage is
    # absent. Only their contents vary, and that is what the ranges do.
    obj = ["x86/rgb2rgb.o", "x86/swscale.o", "x86/yuv2rgb.o"]

    asm = ["x86/range_convert.o"]                       # dropped stage, always present
    for s, f in (("in", "input"), ("out", "output"),
                 ("hs", "scale"), ("hs", "scale_avx2"),
                 ("rgb", "rgb_2_rgb"), ("yuv2rgb", "yuv_2_rgb"),
                 ("out", "yuv2yuvX")):
        if s in present:
            asm.append("x86/%s.o" % f)

    def block(var, items, guard=None):
        # Continuation lines carry no `+=`: a second `+=` at the start of a
        # line is a new statement to make, not a continuation.
        lines = []
        for i, it in enumerate(items):
            prefix = ("%-32s += " % var) if i == 0 else " " * 35
            lines.append("%s%-38s \\" % (prefix, it))
        lines[-1] = lines[-1][:-2].rstrip()
        return "\n".join(lines) + "\n"

    out = []
    out.append(block("OBJS", obj))
    out.append("\n")
    out.append("OBJS-$(CONFIG_XMM_CLOBBER_TEST) += x86/w64xmmtest.o\n\n")
    out.append("EMMS_OBJS_$(HAVE_MMX_INLINE)_$(HAVE_MMX_EXTERNAL)_$(HAVE_MM_EMPTY) = x86/emms.o\n")
    out.append("# Add internal copy of ff_emms() to swscale for shared builds (if needed).\n")
    out.append("SHLIBOBJS                       += $(EMMS_OBJS__yes_)\n\n")
    out.append(block("X86ASM-OBJS", asm))
    out.append("\n")
    if "ops" not in present:
        out.append("OBJS-$(CONFIG_UNSTABLE)         += x86/ops.o\n")
    if "ops" in present:
        out.append("SKIPHEADERS                     += x86/uops_macros.asm.h\n\n")
        out.append("ifdef ARCH_X86_64\n")
        out.append("X86ASM-OBJS-$(CONFIG_UNSTABLE)  += x86/ops_common.o                     \\\n")
        out.append("                                   x86/ops_int.o                        \\\n")
        out.append("                                   x86/ops_float.o                      \\\n")
        out.append("                                   x86/ops.o\n\n")
        out.append("$(SUBDIR)x86/ops_common.o: $(SUBDIR)x86/uops_macros.gen.asm\n")
        out.append("$(SUBDIR)x86/ops_int.o: $(SUBDIR)x86/uops_macros.gen.asm\n")
        out.append("$(SUBDIR)x86/ops_float.o: $(SUBDIR)x86/uops_macros.gen.asm\n")
        out.append("$(SUBDIR)x86/uops_macros.gen.asm: $(SRC_PATH)/libswscale/x86/uops_macros.asm.h \\\n")
        out.append("                                  $(SRC_PATH)/libswscale/uops_macros.h\n")
        out.append("\t$(HOSTCC) $(HOSTCC_E) $(CPPFLAGS) $<\n")
        out.append("endif\n")
    (tree / "libswscale/x86/Makefile").write_text("".join(out))


def strip(tree, remove):
    tree = pathlib.Path(tree)
    assert tree.is_dir(), tree
    ranges = load_ranges()
    present = set(STAGES) - set(remove)
    for st in remove:
        assert st in STAGES, "unknown stage %r" % st

    # The non-x86 twins and the out-of-tree copy go at EVERY level, including
    # the fully restored one: they implement stages this ladder strips but are
    # not themselves a step, so leaving them in the base would hand over a
    # translation exercise and leaving them in an oracle patch would write them
    # back into the candidate's tree.
    for rel in CROSS_ARCH_DIRS:
        p = tree / rel
        if not p.is_dir():
            continue
        # The directory's Makefile is `include`d from libswscale/Makefile
        # (the vulkan one unconditionally, under CONFIG_UNSTABLE), so it has
        # to survive even though nothing it names does.
        shutil.rmtree(p)
        p.mkdir(parents=True, exist_ok=True)
        # The Makefile survives because it is `include`d from a build file that
        # does not know this architecture is empty, but its contents name every
        # kernel that was removed -- a build-system reference to a deleted file,
        # which is a leak source in its own right. Blank it.
        (p / "Makefile").write_text(
            "# Nothing is built for this architecture in this configuration.\n")
    for rel in OUT_OF_TREE:
        p = tree / rel
        if p.exists():
            p.unlink()
    # The build-system lines that name those files are signposts even when the
    # configuration they are gated on is off, so they go too.
    av = tree / "libavfilter/x86/Makefile"
    if av.exists():
        kept = [l for l in av.read_text().split("\n")
                if "colorspacedsp" not in l and "CONFIG_COLORSPACE_FILTER" not in l]
        av.write_text("\n".join(kept))

    if not remove:
        return

    for st in remove:
        for rel in STAGE_FILES[st]:
            p = tree / rel
            if p.exists():
                p.unlink()
    for rel in RANGED_FILES:
        p = tree / rel
        text = p.read_text()
        p.write_text(materialise(text, ranges.get(rel, []), set(remove)))

    # The fused-operation backend is registered from `libswscale/ops.c`, which
    # is not part of the seam, so the symbol has to resolve at every level. The
    # stub declines every operation list; the generic backend then serves them,
    # which is the same path the reference takes on a CPU with no usable SIMD.
    if "ops" not in present:
        shutil.copyfile(HERE / "stubs/x86_ops.c", tree / "libswscale/x86/ops.c")

    write_makefile(tree, present)


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    strip(argv[1], argv[2:])
    print("stripped: %s" % (", ".join(argv[2:]) or "(nothing)"))


if __name__ == "__main__":
    main(sys.argv)
