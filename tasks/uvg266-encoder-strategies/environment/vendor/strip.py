#!/usr/bin/env python3
"""Remove one or more strategy families from a uvg266 checkout.

Usage: strip.py <tree> <family> [<family> ...]

A "family" is one entry of uvg266's strategy registry: its optimized twins live
in src/strategies/{avx2,sse41,sse42,sse2,altivec}/<family>-<isa>.{c,h} and are
registered from src/strategies/strategies-<family>.c.  Removing a family means
removing every architecture's twin (not just x86's), the #include and the
registration call that pull them in, and the src/CMakeLists.txt entries that
compile them.  What remains is the generic C implementation in
src/strategies/generic/, which is the correctness reference and the denominator.

Idempotent: re-running on an already-stripped tree is a no-op.
"""
import os
import re
import sys

ISAS = ["avx2", "sse41", "sse42", "sse2", "altivec"]

# ISA-only headers owned by exactly one family; collected when that family goes.
FAMILY_HEADERS = {
    "dct": ["dct_avx2_tables.h"],
    "intra": ["intra_avx2_tables.h", "mip_data_avx2.h"],
    "picture": ["reg_sad_pow2_widths-avx2.h", "reg_sad_pow2_widths-sse41.h"],
}


def read(path):
    with open(path, "r", errors="surrogateescape") as fh:
        return fh.read()


def write(path, text):
    with open(path, "w", errors="surrogateescape") as fh:
        fh.write(text)


def rm(path):
    if os.path.exists(path):
        os.remove(path)
        return True
    return False


def strip_family(tree, family):
    removed = []

    # 1. the optimized twins, for every architecture
    for isa in ISAS:
        for ext in (".c", ".h"):
            p = os.path.join(tree, "src", "strategies", isa, family + "-" + isa + ext)
            if rm(p):
                removed.append(os.path.relpath(p, tree))

    # 2. the dispatcher: drop the includes and the registration guards
    disp = os.path.join(tree, "src", "strategies", "strategies-" + family + ".c")
    if os.path.exists(disp):
        lines = read(disp).splitlines(keepends=True)
        out = []
        i = 0
        while i < len(lines):
            line = lines[i]
            # include of one of this family's optimized twins, either
            # "strategies/<isa>/<fam>-<isa>.h" or the dct-style "<isa>/<fam>-<isa>.h"
            if re.match(r'\s*#\s*include\s+"', line) and any(
                re.search(r'%s/%s-%s\.h"' % (isa, family, isa), line) for isa in ISAS
            ):
                i += 1
                continue
            # the "#if UVG_HAVE_... \n if (flags...) { \n register \n } \n #endif" block
            m = re.match(r"\s*#\s*if\s+(UVG_HAVE_[A-Z0-9_]+)\s*$", line)
            if m:
                j = i + 1
                end = None
                while j < len(lines):
                    if re.match(r"\s*#\s*endif", lines[j]):
                        end = j
                        break
                    j += 1
                if end is not None:
                    body = "".join(lines[i + 1:end])
                    if re.search(r"uvg_strategy_register_%s_[a-z0-9]+" % re.escape(family), body):
                        i = end + 1
                        continue
            out.append(line)
            i += 1
        write(disp, "".join(out))

    # 3. the build list
    cmake = os.path.join(tree, "src", "CMakeLists.txt")
    if os.path.exists(cmake):
        text = read(cmake)
        for isa in ISAS:
            text = re.sub(
                r"^[ \t]*strategies/%s/%s-%s\.c[ \t]*\n" % (isa, family, isa),
                "",
                text,
                flags=re.M,
            )
        # drop foreach/if blocks whose source list has become empty
        text = re.sub(
            r"if\([A-Z0-9_]+\)\s*\n\s*foreach\(source\s*\n\s*\)\s*\n"
            r"(?:\s*target_sources[^\n]*\n)?(?:\s*set_property[^\n]*\n)?"
            r"\s*endforeach\(\)\s*\nendif\(\)\s*\n",
            "",
            text,
        )
        write(cmake, text)

    # 4. headers only this family used
    for name in FAMILY_HEADERS.get(family, []):
        for isa in ISAS:
            p = os.path.join(tree, "src", "strategies", isa, name)
            if os.path.exists(p) and not header_referenced(tree, name, ignore=p):
                rm(p)
                removed.append(os.path.relpath(p, tree))

    # 5. any ISA directory left empty goes with it
    for isa in ISAS:
        d = os.path.join(tree, "src", "strategies", isa)
        if os.path.isdir(d) and not os.listdir(d):
            os.rmdir(d)
            removed.append(os.path.relpath(d, tree) + "/")

    return removed


def header_referenced(tree, name, ignore=None):
    """True if any remaining source under src/ includes a header with this name."""
    for root, _dirs, files in os.walk(os.path.join(tree, "src")):
        for fn in files:
            if not fn.endswith((".c", ".h", ".cpp", ".hpp")):
                continue
            p = os.path.join(root, fn)
            if ignore is not None and os.path.samefile(p, ignore):
                continue
            if re.search(r'include\s+"[^"]*%s"' % re.escape(name), read(p)):
                return True
    return False


# A stage is one rung of the ladder; one stage may cover several registry
# families (they share an instrument and a measurement).
STAGES = {
    "transforms": ["dct"],
    "intra": ["intra"],
    "blockcost": ["picture"],
    "motion": ["ipol"],
    "quant": ["quant"],
    "sao": ["sao"],
    "alf": ["alf"],
    "depquant": ["depquant"],
    "loopfilter": ["alf", "sao"],
}


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    tree = os.path.abspath(argv[1])
    if not os.path.isdir(os.path.join(tree, "src", "strategies")):
        sys.stderr.write("not a uvg266 tree: %s\n" % tree)
        return 2
    for stage in argv[2:]:
        for family in STAGES.get(stage, [stage]):
            for rel in strip_family(tree, family):
                print("removed %s" % rel)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
