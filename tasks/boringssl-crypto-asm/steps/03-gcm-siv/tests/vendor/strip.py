#!/usr/bin/env python3
# Build the base tree for one milestone.
#
# BoringSSL ships a portable implementation of every primitive in this task's
# scope beside the perlasm one, and `OPENSSL_NO_ASM` is a supported build
# configuration that routes the library onto those portable paths. That makes
# the portable code the correctness reference AND the denominator, so it must
# survive every strip: nothing here deletes or edits `*_nohw.cc.inc`,
# `p256.cc.inc`, `montgomery.cc.inc`, `generic.cc.inc` or the C path in
# `crypto/cipher/e_aesgcmsiv.cc`.
#
# What a strip removes, for one stage:
#   1. the authored perlasm (`.pl`) for that primitive, for EVERY architecture
#      -- x86, x86_64, arm and aarch64. Removing only x86_64 leaves a working
#      implementation of the same algorithm one translation away.
#   2. its pre-generated assembly under `gen/` (`gen/bcm/`, `gen/crypto/`),
#      which is the same code in plaintext.
#   3. its entries in the five generated file lists AND in `build.json`, so the
#      tree stays exactly what `util/pregenerate` would reproduce from it.
#   4. the dispatch region in the header that installs it. Each region is
#      deleted as an exact source-text block, so a tree that does not match the
#      pin fails loudly here instead of building something unexpected.
#
# Idempotent: a stage already absent is a no-op. Run it on a tree where some
# stages are present and some are not -- that is how the ladder is built.
#
# Everything about how a stage is removed lives in STAGES below; nothing about
# which stages a given milestone removes lives in this file.

import argparse
import os
import sys

# --------------------------------------------------------------------------
# Stage definitions.
#
# `perlasm` lists authored `.pl` sources by their `dst` stem, which is the name
# BoringSSL's build gives the generated file, so a generated output is any file
# under gen/ whose basename is `<dst>-<platform>.<ext>`. Deriving the outputs
# that way rather than listing them keeps the two halves in step.
#
# `blocks` lists (file, first_line, last_line, first_line_text, last_line_text)
# exactly as it stands in the pinned tree. The two signature strings are
# checked before anything is deleted, so a tree that does not match the pin
# fails here rather than building something unexpected. Line numbers are the
# pinned ones; edits are applied in descending line order per file, so a later
# deletion never invalidates an earlier one.
# --------------------------------------------------------------------------

STAGES = {
    "aes_block": {
        "perlasm": [
            "aesni-x86_64", "vpaes-x86_64",
            "aesni-x86", "vpaes-x86",
            "aesv8-armv8", "vpaes-armv8",
            "aesv8-armv7", "bsaes-armv7", "vpaes-armv7",
        ],
        # The authored file's own name, which is not always the stem its
        # generated output is given: `aesv8-armx.pl` produces `aesv8-armv8` and
        # `aesv8-armv7`. Matching the stem alone left the source in the tree.
        "perlasm_src": ["aesni-x86_64", "vpaes-x86_64", "aesni-x86", "vpaes-x86",
                        "aesv8-armx", "vpaes-armv8", "bsaes-armv7",
                        "vpaes-armv7"],
        "blocks": [
            ("crypto/fipsmodule/aes/internal.h", 54, 86,
             "#if !defined(OPENSSL_NO_ASM)", "#endif  // !NO_ASM",
             "inline int hwaes_capable() { return CRYPTO_is_AESNI_capable(); }"),
        ],
        # `crypto/test/abi_test.h` enables its ABI tests whenever assembly is on
        # at all, which is still true here. They call the assembly entry points
        # directly, so with this stage's assembly gone they would not compile.
        # Guarding them on the dispatch macro this stage defines is what a tree
        # with no such implementation looks like: the tests come back exactly
        # when the implementation does.
        "lines": [
            ("crypto/fipsmodule/aes/aes_test.cc", 279,
             "#if defined(SUPPORTS_ABI_TEST)",
             "#if defined(SUPPORTS_ABI_TEST) && defined(HWAES)"),
            ]
    },
    "aes_gcm": {
        "perlasm": [
            "aesni-gcm-x86_64", "aes-gcm-avx2-x86_64", "aes-gcm-avx512-x86_64",
            "ghash-x86_64", "ghash-ssse3-x86_64",
            "ghash-x86", "ghash-ssse3-x86",
            "aesv8-gcm-armv8", "ghash-neon-armv8", "ghashv8-armv8",
            "ghash-armv4", "ghashv8-armv7",
        ],
        "perlasm_src": ["aesni-gcm-x86_64", "aes-gcm-avx2-x86_64",
                        "aes-gcm-avx512-x86_64", "ghash-x86_64",
                        "ghash-ssse3-x86_64", "ghash-x86", "ghash-ssse3-x86",
                        "aesv8-gcm-armv8", "ghash-neon-armv8", "ghashv8-armx",
                        "ghash-armv4"],
        "blocks": [
            ("crypto/fipsmodule/aes/internal.h", 410, 522,
             "#if !defined(OPENSSL_NO_ASM)", "#endif  // OPENSSL_NO_ASM",
             "#define GHASH_ASM_X86_64"),
            # The fused-GCM key pairing is guarded on `!OPENSSL_NO_ASM` and the
            # architecture rather than on GHASH_ASM_X86_64, so dropping the
            # declarations above is not enough: the references to the AVX2 and
            # AVX512 GHASH pointers here would not compile. This is the block
            # that a tree with no GCM assembly would not have.
            ("crypto/fipsmodule/aes/gcm.cc.inc", 277, 295,
             "#if !defined(OPENSSL_NO_ASM)", "#endif",
             "gcm_key->impl = gcm_x86_aesni;"),
        ],
        "lines": [
            ("crypto/fipsmodule/aes/gcm_test.cc", 28,
             "#if defined(SUPPORTS_ABI_TEST) && !defined(OPENSSL_NO_ASM)",
             "#if defined(SUPPORTS_ABI_TEST) && defined(GHASH_ASM_X86_64)"),
            ]
    },
    "gcm_siv": {
        "perlasm": ["aes128gcmsiv-x86_64"],
        "perlasm_src": ["aes128gcmsiv-x86_64"],
        "blocks": [
            ("crypto/cipher/internal.h", 243, 395,
             "// AES-GCM-SIV Assembly.",
             "#endif  // OPENSSL_X86_64 && !OPENSSL_NO_ASM && !OPENSSL_WINDOWS",
             "#define AES_GCM_SIV_ASM"),
        ],
        "lines": [],
    },
    "bn_mont": {
        "perlasm": [
            "x86_64-mont", "x86_64-mont5", "x86-mont",
            "armv8-mont", "armv4-mont",
        ],
        "perlasm_src": ["x86_64-mont", "x86_64-mont5", "x86-mont",
                        "armv8-mont", "armv4-mont"],
        "blocks": [
            ("crypto/fipsmodule/bn/internal.h", 411, 466,
             "#if !defined(OPENSSL_NO_ASM) && defined(OPENSSL_X86_64)",
             "#endif  // !OPENSSL_NO_ASM && OPENSSL_X86_64",
             "#define OPENSSL_BN_ASM_MONT5"),
            ("crypto/fipsmodule/bn/internal.h", 338, 409,
             "#if !defined(OPENSSL_NO_ASM) &&                         \\",
             "#endif  // OPENSSL_BN_ASM_MONT",
             "bn_mul_mont_words writes"),
        ],
        "lines": [],
    },
    "p256": {
        "perlasm": [
            "p256-x86_64-asm", "p256_beeu-x86_64-asm",
            "p256-armv8-asm", "p256_beeu-armv8-asm",
        ],
        "perlasm_src": ["p256-x86_64-asm", "p256_beeu-x86_64-asm",
                        "p256-armv8-asm", "p256_beeu-armv8-asm"],
        # This stage's dispatch is a bare preprocessor condition rather than a
        # named macro, so it is edited instead of deleted. The edit drops the
        # x86_64 term from the condition and leaves aarch64, which is what a
        # tree with an aarch64 implementation and no x86_64 one reads like.
        #
        # What it does NOT do is remove the files carrying the condition.
        # p256-nistz.h, p256-nistz.cc.inc and p256-nistz_test.cc stay in the
        # stripped tree -- with the whole nistz256 API surface and the method
        # glue they name -- and are dead on x86_64 only because the edited
        # condition excludes it. They cannot be deleted instead: this
        # milestone's oracle restores their own guard lines, so a tree without
        # them cannot be patched at all (the patch finds no file to patch),
        # and a tree carrying them blanked would only be patchable through
        # context fuzz. The residual is recorded in the task README. Four files
        # carry the condition and differ in whitespace at the pin, so each is
        # edited by line.
        "lines": [
            ("crypto/fipsmodule/ec/ec.cc.inc", 104,
             "    (defined(OPENSSL_X86_64) || defined(OPENSSL_AARCH64)) && \\",
             "    (defined(OPENSSL_AARCH64)) && \\"),
            ("crypto/fipsmodule/ec/p256-nistz.h", 39,
             "    (defined(OPENSSL_X86_64) || defined(OPENSSL_AARCH64)) &&   \\",
             "    (defined(OPENSSL_AARCH64)) &&   \\"),
            ("crypto/fipsmodule/ec/p256-nistz.cc.inc", 44,
             "    (defined(OPENSSL_X86_64) || defined(OPENSSL_AARCH64)) && \\",
             "    (defined(OPENSSL_AARCH64)) && \\"),
            ("crypto/fipsmodule/ec/p256-nistz_test.cc", 42,
             "    (defined(OPENSSL_X86_64) || defined(OPENSSL_AARCH64)) && \\",
             "    (defined(OPENSSL_AARCH64)) && \\"),
        ],
        "blocks": [],
    },
}

# The order stages are removed in. `aes_gcm` edits a region of
# `crypto/fipsmodule/aes/internal.h` that sits BELOW the region `aes_block`
# edits, and the line numbers in STAGES are the pinned ones, so the lower
# region goes first.
STRIP_ORDER = ("aes_gcm", "aes_block", "gcm_siv", "bn_mont", "p256")

# The five generated file lists, and the authored-input manifest they are
# generated from.
GEN_LISTS = [
    "gen/sources.cmake", "gen/sources.json", "gen/sources.bzl",
    "gen/sources.gni", "gen/sources.mk",
]
BUILD_JSON = "build.json"


def fail(msg):
    sys.stderr.write("strip.py: FAIL: %s\n" % msg)
    sys.exit(1)


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def write(path, text):
    # Replace rather than truncate: the work tree may be a hardlink farm, and
    # truncating would rewrite the sibling copy's inode behind its back.
    tmp = path + ".strip-tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.chmod(tmp, os.stat(path).st_mode & 0o7777)
    os.replace(tmp, path)


def gen_outputs(tree, dst):
    """Every generated assembly file whose stem is `dst`."""
    hits = []
    for sub in ("gen/bcm", "gen/crypto"):
        d = os.path.join(tree, sub)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            stem, _, ext = name.rpartition(".")
            if ext in ("S", "asm") and stem.startswith(dst + "-"):
                hits.append(sub + "/" + name)
    return hits


def drop_lines(path, needles):
    """Delete every line of `path` containing one of `needles`; return the count."""
    lines = read(path).splitlines(keepends=True)
    kept = [ln for ln in lines if not any(n in ln for n in needles)]
    dropped = len(lines) - len(kept)
    if dropped:
        write(path, "".join(kept))
    return dropped


def stage_present_files(tree, stage):
    """The stage's perlasm and generated assembly that are still in the tree."""
    spec = STAGES[stage]
    srcs = set(spec["perlasm_src"])
    hits = []
    for dst in spec["perlasm"]:
        hits.extend(gen_outputs(tree, dst))
    for root, _dirs, files in os.walk(os.path.join(tree, "crypto")):
        for name in files:
            if name.endswith(".pl") and name[:-3] in srcs:
                hits.append(os.path.relpath(os.path.join(root, name), tree))
    return sorted(hits)


def strip_stage(tree, stage):
    spec = STAGES[stage]
    present = stage_present_files(tree, stage)

    if not present:
        # Already absent. The headers must already be in their stripped form
        # too; a tree carrying the dispatch for a kernel that is not there is
        # not a state this script produces, so refuse to guess.
        for rel, n, expected, _repl in spec["lines"]:
            lines = read(os.path.join(tree, rel)).splitlines()
            if lines[n - 1] == expected:
                fail("%s: no files for this stage exist, but %s still carries "
                     "its dispatch at line %d" % (stage, rel, n))
        for rel, _a, _b, _sig_a, _sig_b, marker in spec["blocks"]:
            if marker in read(os.path.join(tree, rel)):
                fail("%s: no files for this stage exist, but %s still carries "
                     "its dispatch (%r)" % (stage, rel, marker))
        return []

    removed = []

    # The generated file lists and the authored-input manifest. Each list must
    # actually have carried entries for this stage, or the file we are editing
    # is not the one the build reads. Collected before the files go, since the
    # names are read back off the tree.
    gen_names = [os.path.basename(g) for g in present if not g.endswith(".pl")]

    for rel in present:
        os.remove(os.path.join(tree, rel))
    removed.append("removed %d files (%d perlasm, %d generated)"
                   % (len(present),
                      sum(1 for p in present if p.endswith(".pl")),
                      len(gen_names)))
    if not gen_names:
        fail("%s: no generated assembly found for any of %s -- the stage is "
             "half present" % (stage, ", ".join(spec["perlasm"])))
    for rel in GEN_LISTS:
        p = os.path.join(tree, rel)
        if not os.path.exists(p):
            fail("%s: generated file list %s is missing" % (stage, rel))
        n = drop_lines(p, gen_names)
        if n == 0:
            fail("%s: %s carried no entry for this stage" % (stage, rel))
        removed.append("%s: -%d entries" % (rel, n))

    pl_paths = [p for p in present if p.endswith(".pl")]
    n = drop_lines(os.path.join(tree, BUILD_JSON), pl_paths)
    if n == 0:
        fail("%s: %s carried no perlasm entry for this stage" % (stage, BUILD_JSON))
    removed.append("%s: -%d entries" % (BUILD_JSON, n))

    # The dispatch region, as exact source text.
    for rel, a, b, sig_a, sig_b, _marker in spec["blocks"]:
        lines = read(os.path.join(tree, rel)).splitlines(keepends=True)
        first, last = lines[a - 1].rstrip("\n"), lines[b - 1].rstrip("\n")
        if first != sig_a or last != sig_b:
            fail("%s: %s lines %d-%d do not match the pin\n"
                 "  got  %r\n  want %r\n  got  %r\n  want %r"
                 % (stage, rel, a, b, first, sig_a, last, sig_b))
        del lines[a - 1:b]
        write(os.path.join(tree, rel), "".join(lines))
        removed.append("%s: -%d lines" % (rel, b - a + 1))

    for rel, n, expected, repl in spec["lines"]:
        p = os.path.join(tree, rel)
        lines = read(p).splitlines(keepends=True)
        got = lines[n - 1].rstrip("\n")
        if got != expected:
            fail("%s: %s line %d is not the pinned text\n  got  %r\n  want %r"
                 % (stage, rel, n, got, expected))
        lines[n - 1] = repl + "\n"
        write(p, "".join(lines))
        removed.append("%s: dispatch edited at line %d" % (rel, n))

    return removed


def main():
    ap = argparse.ArgumentParser(description="Remove optimization stages from a "
                                             "pinned BoringSSL tree.")
    ap.add_argument("tree")
    ap.add_argument("stages", nargs="*")
    args = ap.parse_args()

    tree = os.path.abspath(args.tree)
    if not os.path.isdir(os.path.join(tree, "crypto")):
        fail("%s does not look like a BoringSSL checkout" % tree)

    unknown = sorted(set(args.stages) - set(STAGES))
    if unknown:
        fail("unknown stage(s): %s" % ", ".join(unknown))

    todo = [s for s in STRIP_ORDER if s in set(args.stages)]
    if not todo:
        fail("no stages named")

    for stage in todo:
        removed = strip_stage(tree, stage)
        if removed:
            print("[strip] %s" % stage)
            for line in removed:
                print("        " + line)
        else:
            print("[strip] %s: already absent" % stage)


if __name__ == "__main__":
    main()
