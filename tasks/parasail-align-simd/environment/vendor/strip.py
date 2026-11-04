#!/usr/bin/env python3
"""Remove vectorized implementation families from a parasail source tree.

    strip.py <tree> [<key> ...]

A *key* names one family of generated vectorized implementations:

    sw nw sg                     score-only families
    sw_stats nw_stats sg_stats   statistics-carrying families
    sw_trace nw_trace sg_trace   traceback-capable families

The `sg*` keys expand over parasail's eleven semi-global variants (sg, sg_qb,
sg_qe, sg_qx, sg_db, sg_de, sg_dx, sg_qb_de, sg_qe_db, sg_qb_db, sg_qe_de):
the variants share one generated body through src/sg_helper.h, so one key
covers all eleven.

For each removed family the stripper:

  1. deletes the generated `src/<family>_<strategy>_<isa>_<bits>_<width>.c`
     files (all strategies, all five ISAs, all four solution widths -- leaving
     AltiVec or NEON behind would turn the milestone into a transcription);
  2. drops those files from Makefile.am, CMakeLists.txt and src/meson.build;
  3. drops the family's symbols from `parasail/function_lookup.h` and from the
     generated `tests/func_verify*.h` groups;
  4. drops the `#if HAVE_<ISA>` blocks in `src/satcheck.c` that call them;
  5. rewrites the family's dispatchers in the `src/*_dispatch.c` files: every
     `#if HAVE_<ISA>` branch goes, and the fall-through is pointed at the
     family's OWN non-vectorized reference.  Upstream's generator emits
     `parasail_sw` as the fall-through for *every* sw dispatching name, stats
     and traceback included; redirecting it to `parasail_sw_stats` /
     `parasail_sw_trace` is what makes the removed family complete and correct
     rather than merely stubbed.

That last point is the construction the whole ladder rests on: the base does
not stub a name, it ALIASES it at the reference implementation, which stays in
the tree and serves as both the correctness oracle and the speedup
denominator.

The `_profile_` entry points are the single exception -- parasail ships no
non-vectorized profile implementation, so those dispatchers end in
`{ return NULL; }` and are left that way.  They are therefore outside every
milestone's scope.

Two removals are applied unconditionally, whatever the keys:

  * `contrib/ssw/` is deleted.  It is a complete third-party SIMD
    Smith-Waterman -- SSE2 intrinsics, the striped layout, traceback and CIGAR
    -- vendored inside the parasail source tree and used by parasail's own
    tests.  Left in place it makes the first milestone a copy job rather than
    an optimization; the check programs that link it go with it.
  * `tests/test_12.c` and `tests/test_64.c` call ISA-suffixed symbols directly,
    so they cannot survive the removal of any family; they are dropped too.

Idempotent: re-running on an already-stripped tree is a no-op.
"""

import os
import re
import shutil
import sys

SG_VARIANTS = [
    "sg", "sg_qb", "sg_qe", "sg_qx", "sg_db", "sg_de", "sg_dx",
    "sg_qb_de", "sg_qe_db", "sg_qb_db", "sg_qe_de",
]
PLAIN_ALGS = ["sw", "nw"]
STRATEGIES = ["striped", "scan", "diag"]
ISAS = ["sse2", "sse41", "avx2", "altivec", "neon"]
BUILD_LISTS = ["Makefile.am", "CMakeLists.txt", "src/meson.build"]

# check programs that link contrib/ssw or call ISA-suffixed symbols directly
DROPPED_PROGRAMS = ["test_openmp", "test_scatter", "test_ssw", "ssw_vs_pssw",
                    "ssw_test", "test_12", "test_64"]
DROPPED_SOURCES = ["tests/test_openmp.c", "tests/test_openmp_sais.cpp",
                   "tests/test_scatter.c", "tests/test_ssw.cpp",
                   "tests/ssw_vs_pssw.c", "tests/test_12.c", "tests/test_64.c"]
SSW_TOKENS = DROPPED_PROGRAMS + ["libssw", "contrib/ssw", "ssw_dummy",
                                 "parasail_contrib_ssw"]


def die(msg):
    raise SystemExit("strip.py: " + msg)


# ------------------------------------------------------------------- keys ---

def expand(keys):
    """Key -> set of concrete family stems ('sg' -> {'sg', 'sg_qb', ...})."""
    out = set()
    for k in keys:
        if k.endswith("_stats") or k.endswith("_trace"):
            base, detail = k.rsplit("_", 1)
            detail = "_" + detail
        else:
            base, detail = k, ""
        if base == "sg":
            out |= {v + detail for v in SG_VARIANTS}
        elif base in PLAIN_ALGS:
            out.add(base + detail)
        else:
            die("unknown key %r" % k)
    return out


def _alt(seq):
    return "|".join(sorted(seq, key=len, reverse=True))


# ---------------------------------------------------------------- symbols ---

def removed_symbols(tree, stems):
    """Every public parasail symbol defined by a removed family's files."""
    if not stems:
        return set()
    rx = re.compile(
        r"^parasail_(?P<alg>" + _alt(stems) + r")(?P<stats>_stats)?"
        r"(?P<table>_table|_rowcol|_trace)?"
        r"_(?P<par>" + _alt(STRATEGIES) + r")(?P<prof>_profile)?"
        r"_(?P<isa>" + _alt(ISAS) + r")_(?P<bits>\d+)_(?P<width>\d+|sat)$")
    blocked = re.compile(r"^parasail_sw(_table|_rowcol)?_blocked_(" +
                         _alt(ISAS) + r")_\d+_(\d+|sat)$")
    out = set()
    for line in open(os.path.join(tree, "parasail.h"), errors="replace"):
        for m in re.finditer(r"parasail_[A-Za-z0-9_]+", line):
            tok = m.group(0)
            mm = rx.match(tok)
            if mm:
                key = mm.group("alg") + (mm.group("stats") or "")
                if mm.group("table") == "_trace":
                    key = mm.group("alg") + "_trace"
                if key in stems:
                    out.add(tok)
            elif "sw" in stems and blocked.match(tok):
                out.add(tok)
    return out


# ------------------------------------------------------------------ files ---

def delete_family_files(tree, stems):
    if not stems:
        return []
    rx = re.compile(r"^(?P<stem>" + _alt(stems) + r")_(?P<par>" +
                    _alt(STRATEGIES) + r")_(?P<isa>" + _alt(ISAS) +
                    r")_(?P<bits>\d+)_(?P<width>\d+)$")
    src = os.path.join(tree, "src")
    removed = []
    for name in sorted(os.listdir(src)):
        if name.endswith(".c") and rx.match(name[:-2]):
            os.remove(os.path.join(src, name))
            removed.append("src/" + name)
    if "sw" in stems:
        for extra in ("src/sw_blocked_sse41_128_16.c",
                      "src/sw_blocked_sse41_128_32.c"):
            p = os.path.join(tree, extra)
            if os.path.exists(p):
                os.remove(p)
                removed.append(extra)
    return removed


def prune_build_lists(tree, sources):
    """Drop every mention of a deleted source file from the three build
    systems.  meson needs a structure-aware pass: its lists close with `])`
    attached to the last entry, so a line-level prune loses the bracket."""
    bases = {os.path.basename(f) for f in sources}
    bases.discard("")
    for rel in ("Makefile.am", "CMakeLists.txt"):
        p = os.path.join(tree, rel)
        if not os.path.exists(p):
            continue
        lines = open(p, errors="replace").readlines()
        keep = [ln for ln in lines if not any(b in ln for b in bases)]
        open(p, "w").writelines(keep)
    rewrite_meson_lists(os.path.join(tree, "src/meson.build"), bases)


def rewrite_meson_lists(path, dead):
    """Rewrite every `files([...])` list in one canonical shape, dropping
    entries whose file was deleted and collapsing emptied lists to `[]`."""
    if not os.path.exists(path):
        return
    lines = open(path, errors="replace").readlines()
    out = []
    i = 0
    while i < len(lines):
        m = re.match(r"^(\s*\S[^\n]*?files\(\[)(.*)$", lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue
        head, rest = m.group(1), m.group(2)
        body = [rest]
        i += 1
        depth = 1 + rest.count("[") - rest.count("]")
        while i < len(lines) and depth > 0:
            body.append(lines[i])
            depth += lines[i].count("[") - lines[i].count("]")
            i += 1
        files = re.findall(r"'([^']+)'", "".join(body))
        keep = [f for f in files if os.path.basename(f) not in dead]
        if not keep:
            out.append(head + "])\n")
            continue
        out.append(head + "\n")
        for k, f in enumerate(keep):
            out.append("  '%s'%s\n" % (f, "])" if k == len(keep) - 1 else ","))
    open(path, "w").writelines(out)


def prune_symbol_lines(path, syms):
    if not os.path.exists(path):
        return
    lines = open(path, errors="replace").readlines()
    open(path, "w").writelines(
        [ln for ln in lines if not any(s in ln for s in syms)])


# ------------------------------------------------------------- dispatchers ---

RET_DECL = re.compile(r"^\s*[A-Za-z_][A-Za-z0-9_ ]*\*?\s*(\w+_dispatcher)\s*\(\s*$")
ASSIGN = re.compile(r"^(\s*)(\w+)\s*=\s*(parasail_\w+)\s*;\s*$")
HAVE_IF = re.compile(r"^\s*#\s*if\s+HAVE_(SSE2|SSE41|AVX2|ALTIVEC|NEON)\s*$")
ANY_IF = re.compile(r"^\s*#\s*if")
ENDIF = re.compile(r"^\s*#\s*endif\b")
VEC_TAIL = re.compile(r"^(\w+?)_(striped|scan|diag)(_profile)?_(\d+)$")


def dispatcher_family(name):
    """parasail_sw_stats_striped_8_dispatcher -> sw_stats_striped_8"""
    bare = name[len("parasail_"):] if name.startswith("parasail_") else name
    return bare[: -len("_dispatcher")]


def reference_for(family):
    """sw_table_scan_8 -> sw_table_scan, the matching non-vectorized reference.
    None for the _profile_ dispatchers: parasail ships no profile reference."""
    m = VEC_TAIL.match(family)
    if not m or m.group(3):
        return None
    return m.group(1)


def rewrite_dispatchers(path, stems):
    """Strip the ISA branches of the removed families' dispatchers and point
    their fall-through at the matching non-vectorized reference."""
    if not os.path.exists(path) or not stems:
        return
    lines = open(path, errors="replace").readlines()
    out = []
    i = 0
    while i < len(lines):
        m = RET_DECL.match(lines[i])
        family = dispatcher_family(m.group(1)) if m else None
        want = m is not None and any(family == s or family.startswith(s + "_")
                                     for s in stems)
        if not want:
            out.append(lines[i])
            i += 1
            continue
        ref = reference_for(family)
        out.append(lines[i])                      # return type + name
        i += 1
        while i < len(lines) and lines[i].strip() != "{":
            out.append(lines[i])
            i += 1
        if i < len(lines):
            out.append(lines[i])                  # opening brace
            i += 1
        depth = 1
        while i < len(lines) and depth > 0:
            line = lines[i]
            depth += line.count("{") - line.count("}")
            if HAVE_IF.match(line):               # drop the whole block
                nest = 1
                j = i + 1
                while j < len(lines) and nest > 0:
                    if ANY_IF.match(lines[j]):
                        nest += 1
                    elif ENDIF.match(lines[j]):
                        nest -= 1
                    j += 1
                i = j
                continue
            am = ASSIGN.match(line)
            if am and ref is not None:
                out.append("%s%s = parasail_%s;\n"
                       % (am.group(1), am.group(2), ref))
                i += 1
                continue
            out.append(line)
            i += 1
    open(path, "w").writelines(out)


def strip_satcheck(path, syms):
    if not os.path.exists(path) or not syms:
        return
    lines = open(path, errors="replace").readlines()
    out = []
    i = 0
    while i < len(lines):
        if HAVE_IF.match(lines[i]):
            nest = 1
            j = i + 1
            while j < len(lines) and nest > 0:
                if ANY_IF.match(lines[j]):
                    nest += 1
                elif ENDIF.match(lines[j]):
                    nest -= 1
                j += 1
            if any(s in "".join(lines[i:j]) for s in syms):
                i = j
                continue
        out.append(lines[i])
        i += 1
    open(path, "w").writelines(out)


# ------------------------------------------------------------------- ssw ----

def _meson_drop_entries(path, names):
    """Remove `['<name>', files([...]), [deps]],` entries from a meson list."""
    if not os.path.exists(path):
        return
    lines = open(path, errors="replace").readlines()
    out = []
    i = 0
    while i < len(lines):
        m = re.match(r"^\s*\['(" + _alt(names) + r")',\s*$", lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue
        depth = 0
        while i < len(lines):
            depth += lines[i].count("[") - lines[i].count("]")
            i += 1
            if depth <= 0:
                break
    open(path, "w").writelines(out)


def _cmake_drop_programs(path, names):
    """Remove ADD_EXECUTABLE(<name> ..) .. TARGET_LINK_LIBRARIES(<name> ..)."""
    if not os.path.exists(path):
        return
    lines = open(path, errors="replace").readlines()
    out = []
    i = 0
    while i < len(lines):
        m = re.match(r"^\s*ADD_EXECUTABLE\(\s*(" + _alt(names) + r")\b",
                     lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue
        name = m.group(1)
        i += 1
        while i < len(lines):
            if re.match(r"^\s*TARGET_LINK_LIBRARIES\(\s*" + name + r"\b",
                        lines[i]):
                i += 1
                break
            if re.match(r"^\s*(ADD_|ENABLE_TESTING|SET\(|INCLUDE)", lines[i]):
                break
            i += 1
    open(path, "w").writelines(out)


def _meson_drop_ssw_dep(path):
    """Replace the contrib/ssw meson dependency with an empty one, keeping the
    variable name so the rest of the tree still configures."""
    lines = open(path, errors="replace").readlines()
    out = []
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped.startswith("parasail_contrib_ssw_incdir"):
            i += 1
            continue
        if re.match(r"^parasail_contrib_ssw\s*=\s*declare_dependency\($",
                    stripped):
            depth = 0
            while i < len(lines):
                depth += lines[i].count("(") - lines[i].count(")")
                i += 1
                if depth <= 0:
                    break
            out.append("parasail_contrib_ssw = declare_dependency()\n")
            continue
        out.append(lines[i])
        i += 1
    open(path, "w").writelines(out)


def drop_generator(tree):
    """Remove util/ -- the code generator and its templates.

    `util/templates/<family>_<strategy>.c` is the authored body of every kernel
    this ladder asks for: `util/codegen.py` expands one template into every
    ISA x width x strategy variant of a family, byte-for-byte the files the
    task removed.  Leaving it in the tree makes every milestone a regeneration
    exercise rather than an optimization -- the same finding as leaving another
    architecture's implementation behind.  Nothing under util/ is needed to
    build or to run the library or its tests; Makefile.am lists it only in
    EXTRA_DIST, so those lines go too.
    """
    removed = []
    p = os.path.join(tree, "util")
    if os.path.isdir(p):
        shutil.rmtree(p)
        removed.append("util/")
    for rel in ("Makefile.am", "CMakeLists.txt", "src/meson.build"):
        q = os.path.join(tree, rel)
        if not os.path.exists(q):
            continue
        lines = open(q, errors="replace").readlines()
        keep = [ln for ln in lines if "util/" not in ln]
        if len(keep) != len(lines):
            open(q, "w").writelines(keep)
    return removed


def drop_contrib(tree):
    """Delete contrib/ssw, its test drivers, and every build entry for them."""
    removed = []
    p = os.path.join(tree, "contrib/ssw")
    if os.path.isdir(p):
        shutil.rmtree(p)
        removed.append("contrib/ssw/")
    for rel in DROPPED_SOURCES:
        q = os.path.join(tree, rel)
        if os.path.exists(q):
            os.remove(q)
            removed.append(rel)

    for rel in ("Makefile.am", "CMakeLists.txt"):
        q = os.path.join(tree, rel)
        if not os.path.exists(q):
            continue
        lines = open(q, errors="replace").readlines()
        keep = [ln for ln in lines
                if not any(re.search(r"\b" + re.escape(t) + r"\b", ln)
                           for t in SSW_TOKENS)]
        open(q, "w").writelines(keep)

    _cmake_drop_programs(os.path.join(tree, "CMakeLists.txt"), DROPPED_PROGRAMS)

    # CMake: replace the IF(SSE2_FOUND) ssw object library with the dummy
    q = os.path.join(tree, "CMakeLists.txt")
    if os.path.exists(q):
        txt = open(q, errors="replace").read()
        txt = re.sub(
            r"IF\( SSE2_FOUND \)\s*\n(\s*).*?ADD_LIBRARY\( ssw .*?\n.*?ENDIF\( \)",
            "ADD_LIBRARY( ssw OBJECT cmake/ssw_dummy.c )", txt, flags=re.S)
        open(q, "w").write(txt)

    # meson: drop the contrib ssw dependency and its consumers
    q = os.path.join(tree, "contrib/meson.build")
    if os.path.exists(q):
        _meson_drop_ssw_dep(q)
    _meson_drop_entries(os.path.join(tree, "tests/meson.build"),
                        DROPPED_PROGRAMS)
    return removed


# ------------------------------------------------------------------ main ----

def main():
    if len(sys.argv) < 2:
        die(__doc__)
    tree = sys.argv[1]
    keys = sys.argv[2:]
    stems = expand(keys)
    syms = removed_symbols(tree, stems)
    deleted = delete_family_files(tree, stems) + drop_contrib(tree) + drop_generator(tree)
    prune_build_lists(tree, deleted)
    if syms:
        prune_symbol_lines(os.path.join(tree, "parasail/function_lookup.h"), syms)
        for h in ("tests/func_verify.h", "tests/func_verify_tables.h",
                  "tests/func_verify_rowcols.h", "tests/func_verify_traces.h"):
            prune_symbol_lines(os.path.join(tree, h), syms)
        strip_satcheck(os.path.join(tree, "src/satcheck.c"), syms)
        for name in sorted(os.listdir(os.path.join(tree, "src"))):
            if name.endswith("_dispatch.c"):
                rewrite_dispatchers(os.path.join(tree, "src", name), stems)
    print("strip.py: families=%s  files_removed=%d  symbols_removed=%d"
          % (",".join(sorted(stems)) or "(none)", len(deleted), len(syms)))


if __name__ == "__main__":
    main()
