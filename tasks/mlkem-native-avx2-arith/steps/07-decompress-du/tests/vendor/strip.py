#!/usr/bin/env python3
"""strip.py <tree> <level>

Turn a pinned mlkem-native checkout into ladder level L<level>:
steps 1..<level> are implemented, steps > <level> are absent and their
dispatch seams fall back to the C frontend.

Author-owned build tool. Never shipped into the agent image.
"""
import os
import re
import sys

X = "mlkem/src/native/x86_64"
S = X + "/src"

# step -> (files it owns, meta.h toggles it installs, meta.h seam functions)
#
# The NTT-domain primitives (forward NTT, inverse NTT, base multiplication,
# mulcache, to/from-bytes) are one atomic unit, because they share the custom
# NTT-domain coefficient order (mlkem/src/native/api.h).  Everything else
# is independently switchable, verified by building and running the
# project's own func + KAT suites on each subset.
STEPS = {
    1: (  # rejection sampling
        [f"{S}/mlkem_rej_uniform_avx2_asm.S", f"{S}/rej_uniform_table.c"],
        ["MLK_USE_NATIVE_REJ_UNIFORM"],
        ["mlk_rej_uniform_native"],
    ),
    2: (  # the NTT-domain core: one atomic unit
        [
            f"{S}/mlkem_ntt_avx2_asm.S",
            f"{S}/mlkem_nttunpack_avx2_asm.S",
            f"{S}/mlkem_intt_avx2_asm.S",
            f"{S}/mlkem_poly_mulcache_compute_avx2_asm.S",
            f"{S}/mlkem_polyvec_basemul_acc_montgomery_cached_k2_avx2_asm.S",
            f"{S}/mlkem_polyvec_basemul_acc_montgomery_cached_k3_avx2_asm.S",
            f"{S}/mlkem_polyvec_basemul_acc_montgomery_cached_k4_avx2_asm.S",
            f"{S}/mlkem_ntttobytes_avx2_asm.S",
            f"{S}/mlkem_nttfrombytes_avx2_asm.S",
            f"{S}/consts.c",
            f"{S}/consts.h",
        ],
        ["MLK_USE_NATIVE_NTT_CUSTOM_ORDER", "MLK_USE_NATIVE_NTT",
         "MLK_USE_NATIVE_INTT", "MLK_USE_NATIVE_POLY_MULCACHE_COMPUTE",
         "MLK_USE_NATIVE_POLYVEC_BASEMUL_ACC_MONTGOMERY_CACHED",
         "MLK_USE_NATIVE_POLY_TOBYTES", "MLK_USE_NATIVE_POLY_FROMBYTES"],
        ["mlk_ntt_native", "mlk_poly_permute_bitrev_to_custom",
         "mlk_intt_native", "mlk_poly_mulcache_compute_native",
         "mlk_polyvec_basemul_acc_montgomery_cached_k2_native",
         "mlk_polyvec_basemul_acc_montgomery_cached_k3_native",
         "mlk_polyvec_basemul_acc_montgomery_cached_k4_native",
         "mlk_poly_tobytes_native", "mlk_poly_frombytes_native"],
    ),
    3: (  # modular reduction and Montgomery conversion
        [f"{S}/mlkem_reduce_avx2_asm.S", f"{S}/mlkem_tomont_avx2_asm.S"],
        ["MLK_USE_NATIVE_POLY_REDUCE", "MLK_USE_NATIVE_POLY_TOMONT"],
        ["mlk_poly_reduce_native", "mlk_poly_tomont_native"],
    ),
    4: (  # compression, d = 4 and 5
        [
            f"{S}/mlkem_poly_compress_d4_avx2_asm.S",
            f"{S}/mlkem_poly_compress_d5_avx2_asm.S",
            f"{S}/compress_consts.c",
            f"{S}/compress_consts.h",
        ],
        ["MLK_USE_NATIVE_POLY_COMPRESS_D4", "MLK_USE_NATIVE_POLY_COMPRESS_D5"],
        ["mlk_poly_compress_d4_native", "mlk_poly_compress_d5_native"],
    ),
    5: (  # compression, d = 10 and 11
        [
            f"{S}/mlkem_poly_compress_d10_avx2_asm.S",
            f"{S}/mlkem_poly_compress_d11_avx2_asm.S",
        ],
        ["MLK_USE_NATIVE_POLY_COMPRESS_D10", "MLK_USE_NATIVE_POLY_COMPRESS_D11"],
        ["mlk_poly_compress_d10_native", "mlk_poly_compress_d11_native"],
    ),
    6: (  # decompression, d = 4 and 5
        [
            f"{S}/mlkem_poly_decompress_d4_avx2_asm.S",
            f"{S}/mlkem_poly_decompress_d5_avx2_asm.S",
        ],
        ["MLK_USE_NATIVE_POLY_DECOMPRESS_D4",
         "MLK_USE_NATIVE_POLY_DECOMPRESS_D5"],
        ["mlk_poly_decompress_d4_native", "mlk_poly_decompress_d5_native"],
    ),
    7: (  # decompression, d = 10 and 11
        [
            f"{S}/mlkem_poly_decompress_d10_avx2_asm.S",
            f"{S}/mlkem_poly_decompress_d11_avx2_asm.S",
        ],
        ["MLK_USE_NATIVE_POLY_DECOMPRESS_D10",
         "MLK_USE_NATIVE_POLY_DECOMPRESS_D11"],
        ["mlk_poly_decompress_d10_native", "mlk_poly_decompress_d11_native"],
    ),
}

STEP_COUNT = len(STEPS)
# Steps whose presence is required before the shared consts.h can be included.
CONSTS_STEP = 2
COMPRESS_CONSTS_STEP = 4

# Declarations in arith_native_x86_64.h that belong to each step. Dropped
# together with the files, so the header of a stripped tree names only the
# kernels that tree actually has.
# Files whose kernel symbol is not the file name with `mlkem_` rewritten.
SYMBOL_OVERRIDE = {"mlk_intt_avx2_asm": "mlk_invntt_avx2_asm"}


# Derived from STEPS, so the header cannot drift from the file lists.
def _markers(step):
    """Kernel symbol names a step owns, as they appear in arith_native_x86_64.h.

    The files are named mlkem_<kernel>_avx2_asm.S while the symbols are
    mlk_<kernel>_avx2_asm, so the leading `mlkem_` is rewritten.
    """
    ms = []
    for rel in STEPS[step][0]:
        base = os.path.basename(rel)
        if base.endswith("_avx2_asm.S"):
            sym = base[: -len(".S")]
            if sym.startswith("mlkem_"):
                sym = "mlk_" + sym[len("mlkem_"): ]
            sym = SYMBOL_OVERRIDE.get(sym, sym)
            ms.append(sym)
    return ms


HEADER_MARKERS = {s: _markers(s) for s in STEPS}
HEADER_MARKERS[1] += ["MLK_AVX2_REJ_UNIFORM_BUFLEN", "mlk_rej_uniform_table"]


def drop_header_blocks(text, markers):
    """Drop each `#define <marker>`-led declaration block."""
    lines = text.split("\n")
    starts = [i for i, l in enumerate(lines) if l.startswith("#define ")]
    end = next((i for i, l in enumerate(lines) if l.startswith("#endif")), len(lines))
    starts = [i for i in starts if i < end]
    starts.append(end)
    drop = set()
    for k in range(len(starts) - 1):
        a, b = starts[k], starts[k + 1]
        head = lines[a]
        for m in markers:
            if re.search(r"\b%s\b" % re.escape(m), head):
                drop.update(range(a, b))
                break
    return "\n".join(l for i, l in enumerate(lines) if i not in drop)


STUB = """%sstatic MLK_INLINE int %s%s
{
  /* No optimized implementation for this target is present. */
%s  return MLK_NATIVE_FUNC_FALLBACK;
}
"""


def find_block(text, name):
    """Return (start, end) of the definition of `name`, by brace matching."""
    m = re.search(r"^static MLK_INLINE [A-Za-z_0-9 ]*?\b%s\(" % re.escape(name),
                  text, re.M)
    if m is None:
        raise SystemExit("strip.py: seam %s not found" % name)
    start = m.start()
    i = text.index("{", m.end() - 1)
    depth = 0
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return start, i + 1
        i += 1
    raise SystemExit("strip.py: unbalanced braces for %s" % name)


def stub_seam(text, name):
    start, end = find_block(text, name)
    block = text[start:end]
    # Recover the parameter list, verbatim, and the return/must-check prefix.
    sig = block[: block.index("{")]
    plist = sig[sig.index("(") : sig.rindex(")") + 1]
    ret_is_int = re.search(r"\bint\s+%s\(" % re.escape(name), sig) is not None
    args = []
    for part in plist[1:-1].split(","):
        part = re.sub(r"\[[^\]]*\]", " ", part).replace("*", " ").strip()
        toks = [x for x in re.split(r"\s+", part) if x]
        if not toks or toks == ["void"] or toks[-1] == "void":
            continue
        args.append(toks[-1])
    # The block starts at the `static MLK_INLINE` line, so any attribute line
    # above it -- MLK_MUST_CHECK_RETURN_VALUE -- is already in place.
    prefix = ""
    if not ret_is_int:
        # void seam: nothing to fall back to, so make it a no-op
        new = "%sstatic MLK_INLINE void %s%s\n{\n%s}\n" % (
            prefix, name, plist,
            "".join("  (void)%s;\n" % a for a in args))
    else:
        new = STUB % (prefix, name, plist,
                      "".join("  (void)%s;\n" % a for a in args))
    return text[:start] + new + text[end:]



def stub_only(tree, step):
    """Neutralise one step's dispatch in place, leaving everything else alone.

    Used by the verifier for the end-to-end non-regression gate: the candidate's
    own tree, with this step's seams falling back to the C frontend, is the
    baseline its delivered tree is compared against.
    """
    meta = os.path.join(tree, X, "meta.h")
    text = open(meta).read()
    for seam in STEPS[step][2]:
        text = stub_seam(text, seam)
    open(meta, "w").write(text)


def strip(tree, level, keep=None):
    """keep: explicit set of present steps; defaults to 1..level."""
    present = set(keep) if keep is not None else set(range(1, level + 1))
    custom_ok = 2 in present
    for step in [s for s in STEPS if s not in present]:
        for rel in STEPS[step][0]:
            p = os.path.join(tree, rel)
            if os.path.exists(p):
                os.remove(p)

    ari = os.path.join(tree, S, "arith_native_x86_64.h")
    atext = open(ari).read()
    if 2 not in present:
        atext = atext.replace('#include "consts.h"\n', "")
    if 6 not in present:
        atext = atext.replace('#include "compress_consts.h"\n', "")
    for step in [s for s in STEPS if s not in present]:
        atext = drop_header_blocks(atext, HEADER_MARKERS[step])
    open(ari, "w").write(atext)

    meta = os.path.join(tree, X, "meta.h")
    text = open(meta).read()

    # Drop the toggles of every step that is not present.
    for step in [s for s in STEPS if s not in present]:
        for tog in STEPS[step][1]:
            text = re.sub(r"^#define %s\n" % re.escape(tog), "", text, flags=re.M)
    if not custom_ok:
        text = re.sub(r"^#define MLK_USE_NATIVE_NTT_CUSTOM_ORDER\n", "", text,
                      flags=re.M)

    # Stub the seams of every step that is not present.
    for step in [s for s in STEPS if s not in present]:
        for seam in STEPS[step][2]:
            text = stub_seam(text, seam)

    # consts.h / compress_consts.h are shared headers; keep whichever are needed.
    if CONSTS_STEP not in present:
        text = text.replace('#include "src/consts.h"\n', "")
    if COMPRESS_CONSTS_STEP not in present:
        text = text.replace('#include "src/compress_consts.h"\n', "")

    open(meta, "w").write(text)


def main():
    if len(sys.argv) == 4 and sys.argv[2] == "--stub-only":
        stub_only(sys.argv[1], int(sys.argv[3]))
        return
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__)
    keep = None
    if len(sys.argv) == 4:
        keep = {int(x) for x in sys.argv[3].split(",") if x}
    strip(sys.argv[1], int(sys.argv[2]), keep)


if __name__ == "__main__":
    main()
