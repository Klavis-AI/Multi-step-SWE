#!/usr/bin/env python3
"""Strip optimized-implementation ladder stages from an mldsa-native tree.

    python3 strip.py <tree> <stage> [<stage> ...]

Each named stage has its AVX2 implementation removed: the assembly files are
deleted, its MLD_USE_NATIVE_* toggles and mld_*_native bodies are removed from
mldsa/src/native/x86_64/meta.h, and every other file in the tree that refers to
the deleted files (the single-compilation-unit amalgamations, the ABI checker's
registry) has those references dropped.  What is left is a complete, correct
library that runs the portable C implementation for the removed stage and the
native implementation for every stage that is still present.

Idempotent: running it twice on the same tree is a no-op.

Stages, in ladder order:

    ntt          forward NTT (+ the NTT-domain layout permutation, + the
                 shared AVX2 constant block)
    intt         inverse NTT
    pointwise    pointwise Montgomery product / accumulate, caddq, chknorm
    decompose32  decompose and use_hint at gamma2 = (q-1)/32
    decompose88  decompose and use_hint at gamma2 = (q-1)/88
    rej          rejection sampling (uniform, eta2, eta4) + its byte table
    unpack       polyz unpacking at 17 and 19 bits
"""

import os
import re
import shutil
import sys

SRC_DIR = "mldsa/src/native/x86_64/src"
META_H = "mldsa/src/native/x86_64/meta.h"
ARITH_H = SRC_DIR + "/arith_native_x86_64.h"

# Files whose whole subtree is a different architecture's implementation of the
# same ladder, or is the generator the .S files are derived from, or embeds the
# compiled bytecode of every kernel.  None of them is reachable from the build
# of the library itself; all of them would hand over the answer.
FOREIGN = [
    "dev",
    "proofs",
    "integration",
    "scripts/autogen",
    "scripts/simpasm",
    "mldsa/src/native/aarch64",
    ".github",
]

STAGES = {
    "ntt": {
        "toggles": ["MLD_USE_NATIVE_NTT_CUSTOM_ORDER", "MLD_USE_NATIVE_NTT"],
        "funcs": ["mld_poly_permute_bitrev_to_custom", "mld_ntt_native"],
        "files": ["mldsa_ntt_avx2_asm.S", "consts.h", "consts.c"],
        # the constants header defines these; the amalgamation's #undef list
        # enumerates them, and that list is a statement of the table layout
        "aliases": ["MLD_NATIVE_X86_64_SRC_ARITH_NATIVE_X86_64_H",
                    "MLD_NATIVE_X86_64_SRC_CONSTS_H", "mld_qdata",
                    "MLD_AVX2_BACKEND_DATA_OFFSET_8XQ",
                    "MLD_AVX2_BACKEND_DATA_OFFSET_8XQINV",
                    "MLD_AVX2_BACKEND_DATA_OFFSET_8XDIV",
                    "MLD_AVX2_BACKEND_DATA_OFFSET_8XDIV_QINV",
                    "MLD_AVX2_BACKEND_DATA_OFFSET_ZETAS",
                    "MLD_AVX2_BACKEND_DATA_OFFSET_ZETAS_QINV",
                    "MLD_AVX2_REJ_UNIFORM_BUFLEN",
                    "MLD_AVX2_REJ_UNIFORM_ETA2_BUFLEN",
                    "MLD_AVX2_REJ_UNIFORM_ETA4_BUFLEN"],
        "also": [],
    },
    "intt": {
        "toggles": ["MLD_USE_NATIVE_INTT"],
        "funcs": ["mld_intt_native"],
        "files": ["mldsa_intt_avx2_asm.S", "mldsa_nttunpack_avx2_asm.S"],
        # the file is mldsa_intt_*, the symbol is mld_invntt_*
        "aliases": ["invntt_avx2_asm"],
        "also": [],
    },
    "pointwise": {
        "toggles": [
            "MLD_USE_NATIVE_POINTWISE_MONTGOMERY",
            "MLD_USE_NATIVE_POLYVECL_POINTWISE_ACC_MONTGOMERY_L4",
            "MLD_USE_NATIVE_POLYVECL_POINTWISE_ACC_MONTGOMERY_L5",
            "MLD_USE_NATIVE_POLYVECL_POINTWISE_ACC_MONTGOMERY_L7",
            "MLD_USE_NATIVE_POLY_CADDQ",
            "MLD_USE_NATIVE_POLY_CHKNORM",
        ],
        "funcs": [
            "mld_poly_pointwise_montgomery_native",
            "mld_polyvecl_pointwise_acc_montgomery_l4_native",
            "mld_polyvecl_pointwise_acc_montgomery_l5_native",
            "mld_polyvecl_pointwise_acc_montgomery_l7_native",
            "mld_poly_caddq_native",
            "mld_poly_chknorm_native",
        ],
        "files": [
            "mldsa_pointwise_avx2_asm.S",
            "mldsa_pointwise_acc_l4_avx2_asm.S",
            "mldsa_pointwise_acc_l5_avx2_asm.S",
            "mldsa_pointwise_acc_l7_avx2_asm.S",
            "mldsa_poly_caddq_avx2_asm.S",
            "mldsa_poly_chknorm_avx2_asm.S",
        ],
        "also": [],
    },
    "decompose32": {
        "toggles": [
            "MLD_USE_NATIVE_POLY_DECOMPOSE_32",
            "MLD_USE_NATIVE_POLY_USE_HINT_32",
        ],
        "funcs": [
            "mld_poly_decompose_32_native",
            "mld_poly_use_hint_32_native",
        ],
        "files": [
            "mldsa_poly_decompose_32_avx2_asm.S",
            "mldsa_poly_use_hint_32_avx2_asm.S",
        ],
        "also": [],
    },
    "decompose88": {
        "toggles": [
            "MLD_USE_NATIVE_POLY_DECOMPOSE_88",
            "MLD_USE_NATIVE_POLY_USE_HINT_88",
        ],
        "funcs": [
            "mld_poly_decompose_88_native",
            "mld_poly_use_hint_88_native",
        ],
        "files": [
            "mldsa_poly_decompose_88_avx2_asm.S",
            "mldsa_poly_use_hint_88_avx2_asm.S",
        ],
        "also": [],
    },
    "rej": {
        "toggles": [
            "MLD_USE_NATIVE_REJ_UNIFORM",
            "MLD_USE_NATIVE_REJ_UNIFORM_ETA2",
            "MLD_USE_NATIVE_REJ_UNIFORM_ETA4",
        ],
        "funcs": [
            "mld_rej_uniform_native",
            "mld_rej_uniform_eta2_native",
            "mld_rej_uniform_eta4_native",
        ],
        "files": [
            "mldsa_rej_uniform_avx2_asm.S",
            "mldsa_rej_uniform_eta2_avx2_asm.S",
            "mldsa_rej_uniform_eta4_avx2_asm.S",
            "rej_uniform_table.c",
        ],
        "also": [],
    },
    "unpack": {
        "toggles": [
            "MLD_USE_NATIVE_POLYZ_UNPACK_17",
            "MLD_USE_NATIVE_POLYZ_UNPACK_19",
        ],
        "funcs": [
            "mld_polyz_unpack_17_native",
            "mld_polyz_unpack_19_native",
        ],
        "files": [
            "mldsa_polyz_unpack_17_avx2_asm.S",
            "mldsa_polyz_unpack_19_avx2_asm.S",
        ],
        "also": [],
    },
}  # type: dict

ORDER = list(STAGES)

# The AArch64 arithmetic backend implements the same ladder and is removed at
# every level, so its kernel names are removed with it -- from the ABI
# checker's registry, its per-kernel probes, and the amalgamations' include
# lists.  Listed explicitly rather than discovered on disk so the stripper
# stays idempotent on a tree where the directory is already gone.
AARCH64_ROLES = [
    "ntt_aarch64_asm", "intt_aarch64_asm", "pointwise_montgomery_aarch64_asm",
    "poly_caddq_aarch64_asm", "poly_chknorm_aarch64_asm",
    "poly_decompose_32_aarch64_asm", "poly_decompose_88_aarch64_asm",
    "poly_use_hint_32_aarch64_asm", "poly_use_hint_88_aarch64_asm",
    "polyvecl_pointwise_acc_montgomery_l4_aarch64_asm",
    "polyvecl_pointwise_acc_montgomery_l5_aarch64_asm",
    "polyvecl_pointwise_acc_montgomery_l7_aarch64_asm",
    "polyz_unpack_17_aarch64_asm", "polyz_unpack_19_aarch64_asm",
    "rej_uniform_aarch64_asm", "rej_uniform_eta2_aarch64_asm",
    "rej_uniform_eta4_aarch64_asm",
]

# AArch64 names that are not the file basenames: the file/symbol mismatches,
# and the data tables and header guards the removed backend owned.  The
# amalgamations' #undef lists name all of them, and a list of the AArch64
# backend's layered zeta tables is a statement about how it was organised.
AARCH64_ALIASES = [
    "poly_pointwise_montgomery_aarch64_asm",
    "MLD_ARITH_BACKEND_AARCH64", "MLD_NATIVE_AARCH64_META_H",
    "MLD_AARCH64_REJ_UNIFORM_ETA2_BUFLEN", "MLD_AARCH64_REJ_UNIFORM_ETA4_BUFLEN",
    "MLD_NATIVE_AARCH64_SRC_ARITH_NATIVE_AARCH64_H",
    "mld_aarch64_intt_zetas_layer123456", "mld_aarch64_intt_zetas_layer78",
    "mld_aarch64_ntt_zetas_layer123456", "mld_aarch64_ntt_zetas_layer78",
    "mld_polyz_unpack_17_indices", "mld_polyz_unpack_19_indices",
    "mld_rej_uniform_eta_table",
]

# Files whose only references to a removed kernel are inside a comment that
# does not spell the symbol as a token -- a brace-group like
# `rej_uniform_eta{2,4}_avx2_asm` is not a name any tokeniser will match.
# Listed with the substrings to drop lines on, rather than guessed at.
SUBSTR_FILES = {
    "test/bench/bench_components_mldsa.c": ["_avx2_asm", "_aarch64_asm"],
}

# The ABI checker's probe for each of those kernels, by the name the probe
# carries. Listed rather than derived: the probe is named after the *symbol*
# (`mld_invntt_avx2_asm` for the file `mldsa_intt_avx2_asm.S`, and
# `mld_poly_pointwise_montgomery_aarch64_asm` for
# `mldsa_pointwise_montgomery_aarch64_asm.S`), so deriving it from the file
# name silently leaves probes behind -- which is how the first strip shipped
# ten of them, complete with the kernel signatures they document.
CHECK_PROBES = [
    "check_ntt_avx2_asm", "check_invntt_avx2_asm", "check_nttunpack_avx2_asm",
    "check_pointwise_avx2_asm", "check_pointwise_acc_l4_avx2_asm",
    "check_pointwise_acc_l5_avx2_asm", "check_pointwise_acc_l7_avx2_asm",
    "check_poly_caddq_avx2_asm", "check_poly_chknorm_avx2_asm",
    "check_poly_decompose_32_avx2_asm", "check_poly_decompose_88_avx2_asm",
    "check_poly_use_hint_32_avx2_asm", "check_poly_use_hint_88_avx2_asm",
    "check_polyz_unpack_17_avx2_asm", "check_polyz_unpack_19_avx2_asm",
    "check_rej_uniform_avx2_asm", "check_rej_uniform_eta2_avx2_asm",
    "check_rej_uniform_eta4_avx2_asm",
    "check_ntt_aarch64_asm", "check_intt_aarch64_asm",
    "check_poly_pointwise_montgomery_aarch64_asm",
    "check_polyvecl_pointwise_acc_montgomery_l4_aarch64_asm",
    "check_polyvecl_pointwise_acc_montgomery_l5_aarch64_asm",
    "check_polyvecl_pointwise_acc_montgomery_l7_aarch64_asm",
    "check_poly_caddq_aarch64_asm", "check_poly_chknorm_aarch64_asm",
    "check_poly_decompose_32_aarch64_asm", "check_poly_decompose_88_aarch64_asm",
    "check_poly_use_hint_32_aarch64_asm", "check_poly_use_hint_88_aarch64_asm",
    "check_polyz_unpack_17_aarch64_asm", "check_polyz_unpack_19_aarch64_asm",
    "check_rej_uniform_aarch64_asm", "check_rej_uniform_eta2_aarch64_asm",
    "check_rej_uniform_eta4_aarch64_asm",
]

# Directories that no longer exist anywhere in the tree.
GONE_PATHS = ["src/native/x86_64/src/", "src/native/aarch64/", "aarch64/meta.h",
              "dev/x86_64/", "dev/aarch64_opt/", "dev/aarch64_clean/",
              "proofs/hol_light/"]


def strip_comments(text):
    """Blank out /* ... */ and // ... so brace counting sees code only."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def remove_function(text, name):
    """Delete the definition of `name` from `text`, by brace matching."""
    m = re.search(r"^static MLD_INLINE\s[^\n]*\b" + re.escape(name) + r"\s*\(",
                  text, re.M)
    if m is None:
        return text, False
    start = m.start()
    # swallow the attribute line that precedes the definition
    prev_nl = text.rfind("\n", 0, start)
    if prev_nl > 0:
        prev_line = text[text.rfind("\n", 0, prev_nl) + 1:prev_nl].strip()
        if prev_line in ("MLD_MUST_CHECK_RETURN_VALUE",):
            start = text.rfind("\n", 0, prev_nl) + 1
    clean = strip_comments(text)
    open_brace = clean.find("{", m.end())
    if open_brace < 0:
        raise SystemExit("strip.py: no body found for " + name)
    depth = 0
    i = open_brace
    while i < len(clean):
        if clean[i] == "{":
            depth += 1
        elif clean[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    if depth != 0:
        raise SystemExit("strip.py: unbalanced body for " + name)
    end = text.find("\n", i)
    end = len(text) if end < 0 else end + 1
    # also swallow the blank line left behind
    return text[:start] + text[end:], True


def drop_md_section(text, key):
    """Remove one `### ` section from a markdown bibliography."""
    out, skipping = [], False
    for line in text.split("\n"):
        if line.startswith("### "):
            skipping = key in line
        if not skipping:
            out.append(line)
    return "\n".join(out)


def drop_yml_entry(text, key):
    """Remove one `- id: KEY` entry from the bibliography's YAML source.

    Both files are shipped, so removing the entry from only the generated
    markdown leaves the pointer in the tree -- and in whatever regenerates it.
    """
    out, skipping = [], False
    for line in text.split("\n"):
        stripped = line.strip()
        if stripped.startswith("- id:"):
            skipping = stripped[5:].strip() == key
        elif skipping and stripped and not line.startswith((" ", "-", "\t")):
            skipping = False
        if not skipping:
            out.append(line)
    return "\n".join(out)


def collapse_empty_blocks(text):
    """Drop `#if defined(...)` blocks whose body is empty after the strip.

    They mark where the removed backends were included from, which is the same
    disclosure as the lines they used to hold.
    """
    lines = text.split("\n")
    out, stack = [], []
    for line in lines:
        s = line.strip()
        if s.startswith("#if") or s.startswith("#ifdef") or s.startswith("#ifndef"):
            stack.append(len(out))
            out.append(line)
        elif s.startswith("#endif"):
            start = stack.pop() if stack else None
            if start is not None:
                body = [l for l in out[start + 1:]
                        if l.strip() and not l.strip().startswith("/*")
                        and not l.strip().startswith("*")
                        and not l.strip().startswith("//")
                        and not l.strip().endswith("*/")]
                if not body:
                    del out[start:]
                    continue
            out.append(line)
        else:
            out.append(line)
    return "\n".join(out)


def mentions(line, names):
    """True if any identifier in `line` names one of the kernels in `names`.

    Whole tokens only, so `ntt_avx2_asm` does not match `invntt_avx2_asm`.
    Each token is tried raw, as a basename, without its extension, and with the
    repository's symbol prefixes removed -- a kernel is referenced as a file
    (`mldsa_ntt_avx2_asm.S`), as a macro (`MLD_USE_NATIVE_NTT`) and as an
    out-of-line symbol (`mld_invntt_avx2_asm`), and all three have to match.
    """
    for m in re.finditer(r"[A-Za-z0-9_./]+", line):
        tok = m.group(0)
        cands = {tok, os.path.basename(tok),
                 os.path.splitext(os.path.basename(tok))[0]}
        for c in list(cands):
            for pref in ("mldsa_", "mld_", "check_"):
                if c.startswith(pref):
                    cands.add(c[len(pref):])
        if cands & names:
            return True
    return False


def drop_lines_referencing(text, names):
    """Remove whole lines that mention any of `names`."""
    return "\n".join(l for l in text.split("\n") if not mentions(l, names))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    tree, stages = sys.argv[1], [a for a in sys.argv[2:] if not a.startswith("-")]
    for s in stages:
        if s not in STAGES:
            raise SystemExit("strip.py: unknown stage %r (known: %s)"
                             % (s, " ".join(ORDER)))

    for rel in FOREIGN:
        p = os.path.join(tree, rel)
        if os.path.isdir(p):
            shutil.rmtree(p)
        elif os.path.exists(p):
            os.remove(p)

    # The forward NTT is the only stage that is correct together with the
    # inverse one: the AVX2 NTT emits the backend's custom coefficient order,
    # and the library only permutes for it when both are native.  So `ntt`
    # always carries `intt` with it.
    if "ntt" in stages and "intt" not in stages:
        stages = stages + ["intt"]

    removed_files = []
    for s in stages:
        removed_files += STAGES[s]["files"]
    removed_basenames = sorted(set(removed_files))

    # 1. the implementations themselves
    srcdir = os.path.join(tree, SRC_DIR)
    for f in removed_basenames:
        p = os.path.join(srcdir, f)
        if os.path.isfile(p):
            os.remove(p)

    # 2. the constant block and the prototype header go when the first stage
    #    that needs them goes; with a suffix strip that is level 0.
    all_removed = not any(s not in stages for s in ORDER)
    if all_removed:
        if os.path.isdir(srcdir):
            shutil.rmtree(srcdir)

    # 3. meta.h: drop this stage's toggles and the bodies that implement them
    if os.path.isfile(os.path.join(tree, META_H)):
        with open(os.path.join(tree, META_H)) as f:
            meta = f.read()
        for s in stages:
            for t in STAGES[s]["toggles"]:
                meta = re.sub(r"^#define +" + re.escape(t) + r" *\n", "",
                              meta, flags=re.M)
            for fn in STAGES[s]["funcs"]:
                meta, _ = remove_function(meta, fn)
        if all_removed:
            meta = re.sub(r'^#include "src/arith_native_x86_64\.h"\n', "",
                          meta, flags=re.M)
        meta = re.sub(r"\n{4,}", "\n\n\n", meta)
        with open(os.path.join(tree, META_H), "w") as f:
            f.write(meta)

    # 4. every other file that named the deleted files: the single-compilation-
    #    unit amalgamations, and the ABI checker's registry and per-kernel
    #    lists.  A kernel is named there both by file (`..._avx2_asm.S`) and by
    #    role (`ntt_avx2_asm`), so match on the role derived from the file name.
    roles = sorted({os.path.splitext(b)[0].replace("mldsa_", "")
                    for b in removed_basenames})
    toggles = sorted({t for s2 in ORDER if s2 in stages
                      for t in STAGES[s2]["toggles"]})
    aliases = sorted({a for s2 in ORDER if s2 in stages
                      for a in STAGES[s2].get("aliases", [])})
    names = set(removed_basenames) | set(roles) | set(toggles)
    names |= set(AARCH64_ROLES) | set(AARCH64_ALIASES) | set(aliases)
    for root, dirs, files in os.walk(tree):
        if ".git" in root.split(os.sep):
            continue
        for f in files:
            p = os.path.join(root, f)
            rel = os.path.relpath(p, tree)
            if f.startswith("check_") and f.endswith(".c"):
                continue                      # deleted below, never edited
            if not (rel.startswith("mldsa/mldsa_native")
                    or rel.startswith("test/abicheck/")
                    or rel in ("BIBLIOGRAPHY.md", "BIBLIOGRAPHY.yml",
                               "mldsa/src/native/meta.h",
                               "test/test_bounds.py")
                    or rel in SUBSTR_FILES):
                continue
            with open(p, errors="replace") as fh:
                text = fh.read()
            new = drop_lines_referencing(text, names)
            if any(g in l for l in new.split("\n") for g in GONE_PATHS):
                new = "\n".join(l for l in new.split("\n")
                                 if not any(g in l for g in GONE_PATHS))
            for pat in SUBSTR_FILES.get(rel, []):
                new = "\n".join(l for l in new.split("\n") if pat not in l)
            if rel == "BIBLIOGRAPHY.md":
                new = drop_md_section(new, "REF_AVX2")
            elif rel == "BIBLIOGRAPHY.yml":
                new = drop_yml_entry(new, "REF_AVX2")
            new = collapse_empty_blocks(new)
            if new != text:
                with open(p, "w") as fh:
                    fh.write(new)

    # 5. the ABI checker's per-kernel probes for the removed kernels. The
    #    probe for kernel K is `check_<K>_<isa>_asm.c`, so the match is exact:
    #    a substring match would take `intt` for `ntt` and leave both behind,
    #    which is how these files survived the first strip.
    for root, dirs, files in os.walk(os.path.join(tree, "test/abicheck")):
        for f in files:
            if f.endswith(".c") and f[:-2] in CHECK_PROBES:
                os.remove(os.path.join(root, f))

    print("strip.py: removed stages %s from %s" % (" ".join(stages), tree))


if __name__ == "__main__":
    main()
