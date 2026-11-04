#!/usr/bin/env python3
"""One-time preparation of the pinned upstream checkout into the tree the
ladder is derived from (U).  Everything here is a property of the whole ladder
and is therefore baked into U before any level is cut, so no per-step oracle
patch ever has to carry it.

What it does
  1. copies the pinned checkout (no .git),
  2. drops the AVX-512 directories and every build reference to them -- the
     AVX-512 tier is out of scope for both sides of every step,
  3. gives every remaining ASM_* directory a guarded CMakeLists so a directory
     may legally hold only empty.c,
  4. deletes the unit-test sources that reference the removed SIMD symbols
     (they cannot link without them, and two of them ship AVX2 answers),
  5. makes the build reproducible: LTO off by default is left to the caller;
     -Werror is relaxed only for the one GCC-12 diagnostic upstream trips.

Usage: prepare.py <pinned-checkout> <dest>
"""
import os
import re
import shutil
import sys

# --- unit-test sources that reference *_avx2 / *_avx512 / *_sse4_1 / *_ASM ---
# They cannot compile or link once the ASM directories are emptied, and two of
# them (CodeDeprecated.cc, UnitTest_AVX512/CodeDeprecated-avx512.cc) contain
# complete AVX2 / AVX-512 implementations of ladder stages.
DROP_TEST_FILES = [
    "CodeDeprecated.cc",
    "CodeDeprecated.h",
    "TestDequant.cc",
    "TestDwtFrame.cc",
    "TestDwtIdwt.cc",
    "TestFormatConvert.cc",
    "TestGcStageProcess.cc",
    "TestIDWT53.cc",
    "TestInvDwtFrame.cc",
    "TestNlt.cc",
    "TestPack.cc",
    "TestQuant.cc",
    "TestReateControl.cc",
    "TestUnPack.cc",
]

AVX512_DIRS = [
    "Source/Lib/Encoder/ASM_AVX512",
    "Source/Lib/Decoder/ASM_AVX512",
]

# Every remaining ASM_* directory keeps an empty.c so `add_library(NAME OBJECT
# ${glob})` stays legal when the directory holds no real sources.
ASM_DIRS = [
    "Source/Lib/Common/ASM_SSE2",
    "Source/Lib/Common/ASM_AVX2",
    "Source/Lib/Encoder/ASM_SSE2",
    "Source/Lib/Encoder/ASM_SSE4_1",
    "Source/Lib/Encoder/ASM_AVX2",
    "Source/Lib/Decoder/ASM_SSE4_1",
    "Source/Lib/Decoder/ASM_AVX2",
]


def guard_asm_cmake(path):
    """Wrap a bare `asm_compile_to_target(TARGET ${asm_files})` in a guard."""
    src = open(path, encoding="utf-8", errors="surrogateescape").read()
    if "asm_compile_to_target" not in src or "if(asm_files" in src:
        return
    src = re.sub(
        r"^(\s*)asm_compile_to_target\(([^)]*)\)\s*$",
        lambda m: ('%sif(asm_files AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${asm_files}")\n'
                   "%s    asm_compile_to_target(%s)\n%sendif()\n"
                   % (m.group(1), m.group(1), m.group(2), m.group(1))),
        src, flags=re.M)
    open(path, "w", encoding="utf-8", errors="surrogateescape").write(src)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns(".git"))

    # 2. AVX-512 out of the tree entirely.
    for d in AVX512_DIRS:
        p = os.path.join(dst, d)
        if os.path.isdir(p):
            shutil.rmtree(p)
    lib_cmake = os.path.join(dst, "Source/Lib/CMakeLists.txt")
    txt = open(lib_cmake, encoding="utf-8", errors="surrogateescape").read()
    txt = re.sub(r"^add_subdirectory\(\w*/ASM_AVX512\)\n", "", txt, flags=re.M)
    txt = re.sub(r"^\s*\$<TARGET_OBJECTS:\w*ASM_AVX512>\n", "", txt, flags=re.M)
    open(lib_cmake, "w", encoding="utf-8", errors="surrogateescape").write(txt)

    # 3. guarded ASM CMakeLists + an empty.c in every ASM directory.
    for d in ASM_DIRS:
        p = os.path.join(dst, d)
        os.makedirs(p, exist_ok=True)
        guard_asm_cmake(os.path.join(p, "CMakeLists.txt"))
        e = os.path.join(p, "empty.c")
        if not os.path.exists(e):
            open(e, "w").close()

    # 3b. Encoder/ASM_SSE2 names its one .asm file explicitly; glob it so the
    #     directory stays legal when that file is stripped at a later rung.
    enc_sse2 = os.path.join(dst, "Source/Lib/Encoder/ASM_SSE2/CMakeLists.txt")
    if os.path.exists(enc_sse2):
        txt = open(enc_sse2, encoding="utf-8", errors="surrogateescape").read()
        txt = re.sub(r"^set\(asm_files\s+ASM_SSE2\.asm\)\s*$",
                     'file(GLOB asm_files "*.asm")', txt, flags=re.M)
        open(enc_sse2, "w", encoding="utf-8", errors="surrogateescape").write(txt)

    # 4. unit tests that cannot build without the SIMD symbols.
    ut = os.path.join(dst, "tests/UnitTests")
    for f in DROP_TEST_FILES:
        p = os.path.join(ut, f)
        if os.path.exists(p):
            os.remove(p)
    shutil.rmtree(os.path.join(ut, "UnitTest_AVX512"), ignore_errors=True)
    utc = os.path.join(ut, "CMakeLists.txt")
    txt = open(utc, encoding="utf-8", errors="surrogateescape").read()
    txt = re.sub(r"^add_subdirectory\(UnitTest_AVX512\).*\n", "", txt, flags=re.M)
    txt = re.sub(r"^\s*\$<TARGET_OBJECTS:SvtJpegxsUnitTests_AVX512>\n", "", txt, flags=re.M)
    txt = re.sub(r"^add_dependencies\(SvtJpegxsUnitTests[^\n]*SvtJpegxsUnitTests_AVX512\)\n",
                 "", txt, flags=re.M)
    txt = re.sub(r"\sSvtJpegxsUnitTests_AVX512\)", ")", txt)
    open(utc, "w", encoding="utf-8", errors="surrogateescape").write(txt)

    # 4b. every remaining CMakeLists entry that named an AVX-512 or UnitTest_AVX512
    #     target, or an include directory that no longer exists.
    for root, _dirs, files in os.walk(dst):
        if "CMakeLists.txt" not in files:
            continue
        p = os.path.join(root, "CMakeLists.txt")
        txt = open(p, encoding="utf-8", errors="surrogateescape").read()
        new = re.sub(r"^[ \t]*\$<TARGET_OBJECTS:\w*(ASM_AVX512|UnitTests_AVX512)>\s*\n", "", txt, flags=re.M)
        new = re.sub(r"^[ \t]*(ENCODER|DECODER|COMMON)_ASM_AVX512\s*\n", "", new, flags=re.M)
        new = re.sub(r"^[ \t]*\$\{PROJECT_SOURCE_DIR\}/\S*(ASM_AVX512|UnitTest_AVX512)/?\s*\n", "", new, flags=re.M)
        if new != txt:
            open(p, "w", encoding="utf-8", errors="surrogateescape").write(new)

    # 5. GCC 12 -Wmaybe-uninitialized fires inside a *system* intrinsic header
    #    reached from an AVX-512 translation unit.  With AVX-512 gone the
    #    diagnostic has no home left; keep -Werror as upstream ships it.
    # 6. drop #include lines whose header no longer exists (the AVX-512 headers
    #    went with their directories).
    existing = set()
    for root, _dirs, files in os.walk(dst):
        for f in files:
            if f.endswith((".h", ".hh", ".hpp", ".inc", ".asm")):
                existing.add(f)
    for root, _dirs, files in os.walk(os.path.join(dst, "Source")):
        for f in files:
            if not f.endswith((".c", ".h")):
                continue
            p = os.path.join(root, f)
            txt = open(p, encoding="utf-8", errors="surrogateescape").read()
            new = re.sub(r'^[ \t]*#include[ \t]+"([^"]+)"[ \t]*\r?\n',
                         lambda m: m.group(0) if os.path.basename(m.group(1)) in existing else "",
                         txt, flags=re.M)
            if new != txt:
                open(p, "w", encoding="utf-8", errors="surrogateescape").write(new)

    comment_hygiene(dst)

    print("prepared", dst)


# ---------------------------------------------------------------------------
# Comment hygiene.  Four upstream comments describe *how* the removed per-ISA
# implementations work -- one of them (the histogram note in RateControl.c) sits
# in a file the step-04 instruction tells the candidate to read, and two more in
# the dispatch tables describe the removed packer and unpacker.  They are hints,
# not code, but "every usable reference must be closed" applies to hints that
# state a construction.  The scalar-path explanation is kept; only the sentences
# that describe the deleted bodies are rewritten.  Behaviour is untouched, so
# no oracle patch changes.
COMMENT_EDITS = [
    ("Source/Lib/Encoder/Codec/RateControl.c",
     " * vector versions count all sixteen bins with compares and have no such\n"
     " * dependency. */",
     " */"),
    ("Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c",
     "    /* The AVX2 directory is built with -mpopcnt as well, and RateControl_avx2.c\n"
     "     * asks for POPCNT by intrinsic, so the same rule applies one tier down.\n"
     "     * Every processor with AVX2 also has POPCNT, so this only matters where a\n"
     "     * hypervisor or emulator masks the feature bits apart. */",
     "    /* The AVX2 directory is built with -mpopcnt as well, so the same rule\n"
     "     * applies one tier down.  Every processor with AVX2 also has POPCNT, so\n"
     "     * this only matters where a hypervisor or emulator masks the feature bits\n"
     "     * apart. */"),
    ("Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c",
     "    /* pack_data_groups_avx2 packs a group's planes with a per-plane loop; PDEP does the same\n"
     "     * work in four instructions regardless of plane count (see pack_group_helper.h), but needs\n"
     "     * BMI2 and is microcoded and slow on some older CPUs, so this only overrides the AVX2 pick -\n"
     "     * never AVX-512, which already uses PDEP - and only when the host (not just the requested\n"
     "     * tier) actually has BMI2. */",
     "    /* One implementation of this stage needs BMI2 and is microcoded and slow on\n"
     "     * some older CPUs, so it only overrides the AVX2 pick -- never AVX-512,\n"
     "     * which already subsumes it -- and only when the host (not just the\n"
     "     * requested tier) actually has BMI2. */"),
    ("Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c",
     "    /* unpack_data_avx2 spreads a group's planes with a nibble-split-and-movemask sequence;\n"
     "     * PEXT does the same spread in four instructions (see unpack_common.h), but needs BMI2.\n"
     "     * It is microcoded and slow on some older CPUs, so this only overrides the AVX2 pick -\n"
     "     * never AVX-512, which already uses PEXT - and only when the host (not just the requested\n"
     "     * tier) actually has BMI2. */",
     "    /* One implementation of this stage needs BMI2 and is microcoded and slow on\n"
     "     * some older CPUs, so it only overrides the AVX2 pick -- never AVX-512,\n"
     "     * which already subsumes it -- and only when the host (not just the\n"
     "     * requested tier) actually has BMI2. */"),
]


def comment_hygiene(tree):
    for rel, old, new in COMMENT_EDITS:
        p = os.path.join(tree, rel)
        txt = open(p, encoding="utf-8", errors="surrogateescape").read()
        if old not in txt:
            print("  comment hygiene: %s already clean or text moved" % rel)
            continue
        open(p, "w", encoding="utf-8", errors="surrogateescape").write(txt.replace(old, new, 1))


if __name__ == "__main__":
    main()
