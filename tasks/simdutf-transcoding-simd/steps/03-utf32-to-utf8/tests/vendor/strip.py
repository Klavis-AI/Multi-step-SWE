#!/usr/bin/env python3
"""Remove one or more optimization families from a simdutf source tree.

Usage:  strip.py <tree> <family> [<family> ...]

A "family" is one step of the ladder.  Removing a family means:

  * its SIMD kernel files are deleted from src/{westmere,haswell,icelake}/
  * its architecture-generic templates are deleted from src/generic/
  * any shared data table it owns is deleted from src/tables/
  * every #include of a deleted file is dropped from the file that pulled it in
  * every `implementation` method the family owns, in each ISA's
    implementation.cpp, has its body replaced by the *fallback*
    implementation's body for the same method -- the ISA class keeps its method
    table and routes that family at the portable reference.  The scalar
    reference therefore survives the strip, which is what makes the base a
    supported configuration rather than a mutilated tree, and what gives the
    measurement a denominator.

The non-x86 back-ends (arm64, ppc64, rvv, lsx, lasx) are removed
unconditionally: they are never built on x86, and leaving them in place hands
over the same algorithms as readable NEON/AltiVec/RVV intrinsics.

Paths in the tables below are relative to the tree's src/ directory, so they
are also exactly the strings an #include has to name to pull the file in.

Idempotent: running it twice, or on a tree where some family is already gone,
produces the same tree.
"""

import os
import re
import sys

# --------------------------------------------------------------------------
# Families: which methods, kernel files, generic templates and tables each owns.
#
# Shared pools are assigned to the EARLIEST family that uses them, so the ladder
# restores them as soon as anything needs them.  The one real split is
# src/generic/utf16/, whose members belong to three different families and are
# therefore listed file by file rather than as a directory.
# --------------------------------------------------------------------------

FAMILIES = {
    "utf8_to_utf16_and_utf32": dict(
        methods=[
            "validate_utf8",
            "validate_utf8_with_errors",
            "convert_utf8_to_utf16le",
            "convert_utf8_to_utf16be",
            "convert_utf8_to_utf16le_with_errors",
            "convert_utf8_to_utf16be_with_errors",
            "convert_valid_utf8_to_utf16le",
            "convert_valid_utf8_to_utf16be",
            "convert_utf8_to_utf32",
            "convert_utf8_to_utf32_with_errors",
            "convert_valid_utf8_to_utf32",
            "count_utf8",
            "utf16_length_from_utf8",
            "utf32_length_from_utf8",
            # Not a scored function: detect_encodings is identical in every
            # implementation, but icelake's body is written inline against the
            # UTF-8 validation helpers this family owns, so it is routed at the
            # portable reference along with them.
            "detect_encodings",
        ],
        owned=[
            "westmere/sse_convert_utf8_to_utf16.cpp",
            "westmere/sse_convert_utf8_to_utf32.cpp",
            "haswell/avx2_convert_utf8_to_utf16.cpp",
            "haswell/avx2_convert_utf8_to_utf32.cpp",
            "icelake/icelake_from_utf8.inl.cpp",
            "icelake/icelake_from_valid_utf8.inl.cpp",
            "icelake/icelake_utf8_common.inl.cpp",
            "icelake/icelake_utf8_validation.inl.cpp",
            "icelake/icelake_convert_utf8_to_utf16.inl.cpp",
            "generic/utf8_to_utf16",
            "generic/utf8_to_utf32",
            "generic/utf8_validation",
            "generic/utf8.h",
            "generic/utf8/utf16_length_from_utf8_bytemask.h",
            "generic/buf_block_reader.h",
            "tables/utf8_to_utf16_tables.h",
            # The AVX-512 back-end's macro layer is this family's algorithm at
            # another width and is used by nothing else once the family is gone;
            # its 16-page comment is a specification of the kernel.
            "icelake/icelake_macros.inl.cpp",
        ],
        scripts=["scripts/sse_utf8_utf16_decode.py"],
        # Helpers that live in the ISA implementation.cpp preamble rather than
        # in a kernel file, and that no longer have a caller once the family is
        # routed at the reference.
        aux=["must_be_2_3_continuation"],
    ),
    "utf16_to_utf8": dict(
        methods=[
            "convert_utf16le_to_utf8",
            "convert_utf16be_to_utf8",
            "convert_utf16le_to_utf8_with_errors",
            "convert_utf16be_to_utf8_with_errors",
            "convert_valid_utf16le_to_utf8",
            "convert_valid_utf16be_to_utf8",
            "convert_utf16le_to_utf8_with_replacement",
            "convert_utf16be_to_utf8_with_replacement",
            "utf8_length_from_utf16le",
            "utf8_length_from_utf16be",
            "utf8_length_from_utf16le_with_replacement",
            "utf8_length_from_utf16be_with_replacement",
        ],
        owned=[
            "westmere/sse_convert_utf16_to_utf8.cpp",
            "westmere/internal",
            "haswell/avx2_convert_utf16_to_utf8.cpp",
            "icelake/icelake_convert_utf16_to_utf8.inl.cpp",
            "icelake/icelake_utf8_length_from_utf16.inl.cpp",
            "generic/utf16_to_utf8",
            "generic/utf16.h",
            "generic/utf16/utf8_length_from_utf16.h",
            "generic/utf16/utf8_length_from_utf16_bytemask.h",
            "tables/utf16_to_utf8_tables.h",
        ],
        scripts=[
            "scripts/sse_convert_utf16_to_utf8.py",
            # the same packing pattern for another back-end: still this
            # family's table, still a worked example of the compress-store
            "scripts/ppc64_convert_utf16_to_utf8.py",
        ],
    ),
    "utf32_to_utf8": dict(
        methods=[
            "convert_utf32_to_utf8",
            "convert_utf32_to_utf8_with_errors",
            "convert_valid_utf32_to_utf8",
            "utf8_length_from_utf32",
        ],
        owned=[
            "westmere/sse_convert_utf32_to_utf8.cpp",
            "haswell/avx2_convert_utf32_to_utf8.cpp",
            "icelake/icelake_convert_utf32_to_utf8.inl.cpp",
            "generic/utf32.h",
        ],
    ),
    "utf16_utf32": dict(
        methods=[
            "convert_utf16le_to_utf32",
            "convert_utf16be_to_utf32",
            "convert_utf16le_to_utf32_with_errors",
            "convert_utf16be_to_utf32_with_errors",
            "convert_valid_utf16le_to_utf32",
            "convert_valid_utf16be_to_utf32",
            "convert_utf32_to_utf16le",
            "convert_utf32_to_utf16be",
            "convert_utf32_to_utf16le_with_errors",
            "convert_utf32_to_utf16be_with_errors",
            "convert_valid_utf32_to_utf16le",
            "convert_valid_utf32_to_utf16be",
            "utf32_length_from_utf16le",
            "utf32_length_from_utf16be",
            "utf16_length_from_utf32",
            "change_endianness_utf16",
        ],
        owned=[
            "westmere/sse_convert_utf16_to_utf32.cpp",
            "westmere/sse_convert_utf32_to_utf16.cpp",
            "haswell/avx2_convert_utf16_to_utf32.cpp",
            "haswell/avx2_convert_utf32_to_utf16.cpp",
            "icelake/icelake_convert_utf16_to_utf32.inl.cpp",
            "icelake/icelake_convert_utf32_to_utf16.inl.cpp",
            "generic/utf16/change_endianness.h",
            "generic/utf16/utf32_length_from_utf16.h",
            "tables/utf32_to_utf16_tables.h",
        ],
        scripts=["scripts/sse_convert_utf32_to_utf16.py"],
    ),
    "latin1": dict(
        methods=[
            "convert_latin1_to_utf8",
            "convert_latin1_to_utf16le",
            "convert_latin1_to_utf16be",
            "convert_latin1_to_utf32",
            "convert_utf8_to_latin1",
            "convert_utf8_to_latin1_with_errors",
            "convert_valid_utf8_to_latin1",
            "convert_utf16le_to_latin1",
            "convert_utf16be_to_latin1",
            "convert_utf16le_to_latin1_with_errors",
            "convert_utf16be_to_latin1_with_errors",
            "convert_valid_utf16le_to_latin1",
            "convert_valid_utf16be_to_latin1",
            "convert_utf32_to_latin1",
            "convert_utf32_to_latin1_with_errors",
            "convert_valid_utf32_to_latin1",
            "latin1_length_from_utf8",
            "utf8_length_from_latin1",
        ],
        owned=[
            "westmere/sse_convert_latin1_to_utf8.cpp",
            "westmere/sse_convert_latin1_to_utf16.cpp",
            "westmere/sse_convert_latin1_to_utf32.cpp",
            "westmere/sse_convert_utf8_to_latin1.cpp",
            "westmere/sse_convert_utf16_to_latin1.cpp",
            "westmere/sse_convert_utf32_to_latin1.cpp",
            "haswell/avx2_convert_latin1_to_utf8.cpp",
            "haswell/avx2_convert_latin1_to_utf16.cpp",
            "haswell/avx2_convert_latin1_to_utf32.cpp",
            "haswell/avx2_convert_utf8_to_latin1.cpp",
            "haswell/avx2_convert_utf16_to_latin1.cpp",
            "haswell/avx2_convert_utf32_to_latin1.cpp",
            "icelake/icelake_convert_latin1_to_utf8.inl.cpp",
            "icelake/icelake_convert_latin1_to_utf16.inl.cpp",
            "icelake/icelake_convert_latin1_to_utf32.inl.cpp",
            "icelake/icelake_convert_utf8_to_latin1.inl.cpp",
            "icelake/icelake_convert_valid_utf8_to_latin1.inl.cpp",
            "icelake/icelake_convert_utf16_to_latin1.inl.cpp",
            "icelake/icelake_convert_utf32_to_latin1.inl.cpp",
            "generic/utf8_to_latin1",
        ],
    ),
    "base64": dict(
        methods=[
            "base64_to_binary",
            "base64_to_binary_details",
            "binary_to_base64",
            "binary_to_base64_with_lines",
            "binary_length_from_base64",
        ],
        owned=[
            "westmere/sse_base64.cpp",
            "haswell/avx2_base64.cpp",
            "icelake/icelake_base64.inl.cpp",
            "generic/base64.h",
            "generic/base64lengths.h",
        ],
        # scripts/base64/*.py print this family's validation tables (delta_asso,
        # check_asso, delta_values, check_values and the AVX-512 LUT) — tables
        # that exist nowhere else in the stripped tree, since
        # include/simdutf/base64_tables.h holds only the encoding alphabets.
        scripts=["scripts/base64"],
        # include/simdutf/base64_tables.h is a header the portable reference
        # itself depends on, so it cannot be deleted -- but three of the
        # compaction tables it carries are consumed by nothing once the SIMD
        # kernels are gone, and they are the decode construction's constants.
        header_tables={
            "include/simdutf/base64_tables.h": [
                "thintable_epi8",
                "pshufb_combine_table",
                "BitsSetTable256mul2",
            ]
        },
        # No fallback body exists for these: they are base-class methods the ISA
        # classes override.  Route them at the base-class definition instead.
        base_class_methods=["binary_length_from_base64"],
    ),
    "validate_misc": dict(
        methods=[
            "validate_ascii",
            "validate_ascii_with_errors",
            "validate_utf16le_as_ascii",
            "validate_utf16be_as_ascii",
            "validate_utf16le",
            "validate_utf16be",
            "validate_utf16le_with_errors",
            "validate_utf16be_with_errors",
            "to_well_formed_utf16le",
            "to_well_formed_utf16be",
            "validate_utf32",
            "validate_utf32_with_errors",
            "count_utf16le",
            "count_utf16be",
            "find",
        ],
        owned=[
            "westmere/sse_validate_utf16.cpp",
            "westmere/sse_utf16fix.cpp",
            "haswell/avx2_validate_utf16.cpp",
            "haswell/avx2_utf16fix.cpp",
            "icelake/icelake_utf16fix.cpp",
            "icelake/icelake_ascii_validation.inl.cpp",
            "icelake/icelake_utf32_validation.inl.cpp",
            "icelake/icelake_find.inl.cpp",
            "generic/validate_utf16.h",
            "generic/validate_utf32.h",
            "generic/ascii_validation.h",
            "generic/find.h",
            "generic/utf16/count_code_points_bytemask.h",
            "generic/utf16/to_well_formed.h",
        ],
    ),
}

ORDER = [
    "utf8_to_utf16_and_utf32",
    "utf16_to_utf8",
    "utf32_to_utf8",
    "utf16_utf32",
    "latin1",
    "base64",
    "validate_misc",
]

NONX86 = ["arm64", "ppc64", "rvv", "lsx", "lasx"]
ISA_DIRS = ["westmere", "haswell", "icelake"]

# --------------------------------------------------------------------------
# Definition parsing
# --------------------------------------------------------------------------
DEF_RE = re.compile(r"(?m)^[A-Za-z_][\w:<>,\s\*&]*\bimplementation::(\w+)\s*\(")


def split_params(text):
    """Split a parameter list on top-level commas."""
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "(<[":
            depth += 1
        elif ch in ")>]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [p.strip() for p in out if p.strip()]


def param_types(params_text):
    """Drop parameter names and default values, keep the types."""
    types = []
    for p in split_params(params_text):
        p = p.split("=")[0].strip()
        m = re.match(r"^(.*?[\s\*&])([A-Za-z_]\w+)$", p, re.S)
        if m:
            p = m.group(1)
        types.append(re.sub(r"\s+", " ", p).strip())
    return tuple(types)


def param_names(sig_text):
    """The parameter names, verbatim, from a method signature."""
    start = sig_text.find("(")
    names = []
    if start < 0:
        return names
    end = _scan(sig_text, start, "(", ")")
    if end < 0:
        end = len(sig_text)
    for p in split_params(sig_text[start + 1 : end]):
        p = p.split("=")[0].strip()
        m = re.search(r"([A-Za-z_]\w*)$", p)
        names.append(m.group(1) if m else None)
    return names


def _scan(text, i, opener, closer):
    """Index of the closer matching the opener at i, skipping comments and
    string/char literals -- a brace inside either is not a brace."""
    assert text[i] == opener, (i, text[i - 20 : i + 20])
    depth, j, n = 0, i, len(text)
    while j < n:
        c = text[j]
        if c == "/" and j + 1 < n and text[j + 1] == "*":
            j = text.find("*/", j + 2)
            if j < 0:
                raise SystemExit("strip.py: unterminated comment")
            j += 1
        elif c == "/" and j + 1 < n and text[j + 1] == "/":
            j = text.find("\n", j)
            if j < 0:
                break
        elif c in "\"'":
            q, j = c, j + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
        elif c == opener:
            depth += 1
        elif c == closer:
            depth -= 1
            if depth == 0:
                return j
        j += 1
    return -1


def definitions(text, path):
    """Yield (name, types, body_start, body_end, sig_start) per definition."""
    for m in DEF_RE.finditer(text):
        name = m.group(1)
        sig_start = text.rfind("\n", 0, m.start()) + 1
        paren = m.end() - 1  # the "(" the regex consumed
        i = _scan(text, paren, "(", ")")
        if i < 0:
            raise SystemExit("strip.py: unbalanced parens after %s in %s" % (name, path))
        params = text[m.end() : i]
        j = i
        while j < len(text) and text[j] != "{":
            j += 1
        k = _scan(text, j, "{", "}")
        if k < 0:
            raise SystemExit("strip.py: unbalanced braces after %s in %s" % (name, path))
        yield name, param_types(params), j, k, sig_start


def load_fallback(tree):
    """(method, param types) -> (body, parameter names) from the reference."""
    path = os.path.join(tree, "src/fallback/implementation.cpp")
    text = open(path).read()
    table = {}
    for name, types, bs, be, sig_start in definitions(text, path):
        table.setdefault((name, types), (text[bs : be + 1], param_names(text[sig_start:bs])))
    return table


def rename_params(body, old, new):
    """Rewrite the reference body's parameter names to the target's."""
    for o, n in zip(old, new):
        if o and n and o != n:
            body = re.sub(r"\b%s\b" % re.escape(o), n, body)
    return body


def strip_isa_file(path, methods, fallback, base_class_methods):
    """Replace the body of every owned method with the portable reference's."""
    text = open(path).read()
    out, cursor, replaced, missing = [], 0, 0, []
    for name, types, bs, be, sig_start in definitions(text, path):
        if name not in methods:
            continue
        key = (name, types)
        if key in fallback:
            ref_body, ref_names = fallback[key]
            body = rename_params(ref_body, ref_names, param_names(text[sig_start:bs]))
        elif name in base_class_methods:
            sig = text[sig_start:bs]
            args = ", ".join(n for n in param_names(sig) if n)
            body = "{ return simdutf::implementation::%s(%s); }" % (name, args)
        else:
            missing.append((name, types))
            continue
        out.append(text[cursor:bs])
        out.append(body)
        cursor = be + 1
        replaced += 1
    out.append(text[cursor:])
    if missing:
        raise SystemExit(
            "strip.py: no replacement body for %s in %s"
            % (sorted(set(m[0] for m in missing)), path)
        )
    open(path, "w").write(prune_empty_blocks("".join(out)))
    return replaced


def drop_includes(text, names):
    """Drop every #include whose quoted path is one of `names`."""
    out = []
    for line in text.splitlines(True):
        s = line.strip()
        if s.startswith("#include"):
            m = re.match(r'#include\s+"([^"]+)"', s)
            if m and m.group(1) in names:
                continue
        out.append(line)
    return "".join(out)


def drop_header_tables(tree, spec):
    """Delete named top-level array definitions from a shared header.

    Brace-matched, not regex-matched: simdutf's table literals end with the
    closing brace on the same line as the last value (`255};`), which a
    line-oriented match misses.
    """
    removed = 0
    for rel, names in spec.items():
        path = os.path.join(tree, rel)
        if not os.path.exists(path):
            continue
        pat = re.compile(
            r"(?m)^constexpr [\w:<> ]+ (" + "|".join(names) + r")\[[^\]]*\] = \{"
        )
        text = open(path).read()
        out, cursor = [], 0
        for m in pat.finditer(text):
            i, depth = m.end() - 1, 0
            while i < len(text):
                if text[i] == "{":
                    depth += 1
                elif text[i] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            j = text.find(";", i)
            start = m.start()
            while text[start - 1] == "\n":
                start -= 1
            out.append(text[cursor:start])
            cursor = j + 1
            if text[cursor] == "\n":
                cursor += 1
            removed += 1
        out.append(text[cursor:])
        open(path, "w").write("".join(out))
    return removed


IF_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b")
END_RE = re.compile(r"^\s*#\s*endif\b")


def prune_empty_blocks(text):
    """Delete #if/#endif blocks left with no content.

    Dropping an #include normally leaves the conditional and its explanatory
    comment behind -- `#if SIMDUTF_FEATURE_UTF8 && SIMDUTF_FEATURE_UTF16` /
    `// transcoding from UTF-8 to UTF-16` / `#endif`.  That is a map of which
    family used to live where, and the group comments name the deleted units.
    An empty conditional compiles to nothing, so removing it is free.
    """
    lines = text.splitlines(True)
    changed = True
    while changed:
        changed = False
        stack, spans = [], []
        for i, l in enumerate(lines):
            if IF_RE.match(l):
                stack.append(i)
            elif END_RE.match(l) and stack:
                spans.append((stack.pop(), i))
        for a, b in sorted(spans, key=lambda s: s[1] - s[0]):
            body = [x for x in lines[a + 1 : b] if x.strip() and not x.strip().startswith("//")]
            if not body:
                del lines[a : b + 1]
                changed = True
                break
    return "".join(lines)


def rm(tree, rel):
    return rm_path(os.path.join(tree, "src", rel))


def rm_path(path):
    if not os.path.exists(path):
        return 0
    if os.path.isdir(path):
        n = 0
        for root, dirs, files in os.walk(path, topdown=False):
            for f in files:
                os.remove(os.path.join(root, f))
                n += 1
            for d in dirs:
                os.rmdir(os.path.join(root, d))
        os.rmdir(path)
        return n
    os.remove(path)
    return 1


DEF_RE_ANY = re.compile(r"(?m)^[A-Za-z_][\w:<>,\s\*&]*?(\w+)\s*\(")


def all_definitions(text, path):
    """Every column-0 function definition in an ISA implementation.cpp,
    method or file-local helper alike: (name, body_start, body_end, is_method,
    start)."""
    seen, out = set(), []
    for m in DEF_RE_ANY.finditer(text):
        paren = m.end() - 1
        i = _scan(text, paren, "(", ")")
        if i < 0:
            continue
        j = i
        while j < len(text) and text[j] != "{":
            j += 1
        k = _scan(text, j, "{", "}")
        if k < 0 or j in seen:
            continue
        seen.add(j)
        head = text[m.start() : m.end()]
        out.append((m.group(1), j, k, "implementation::" in head, m.start()))
    return out


def defined_names(paths):
    """Identifiers that the given kernel/template files define."""
    names, files = set(), []
    for p in paths:
        if os.path.isdir(p):
            for root, _, fs in os.walk(p):
                files.extend(os.path.join(root, f) for f in fs)
        elif os.path.exists(p):
            files.append(p)
    for p in files:
        try:
            text = open(p).read()
        except (OSError, UnicodeDecodeError):
            continue
        for m in DEF_RE_ANY.finditer(text):
            names.add(m.group(1))
    return names


def drop_dead_helpers(tree, dead):
    """Delete file-local helpers in the ISA implementation.cpp files whose
    bodies now reference a symbol that no longer exists, to a fixpoint -- a
    helper can be reachable only through another helper."""
    removed = 0
    for isa in ISA_DIRS:
        path = os.path.join(tree, "src", isa, "implementation.cpp")
        if not os.path.exists(path):
            continue
        changed = True
        while changed:
            changed = False
            text = open(path).read()
            keep, cursor = [], 0
            for name, bs, be, is_method, start in all_definitions(text, path):
                if is_method:
                    continue
                body = text[bs : be + 1]
                if name not in dead and not any(
                    re.search(r"\b%s\b" % re.escape(d), body) for d in dead
                ):
                    continue
                # also swallow the `template <...>` line the helper may need
                head = text.rfind("\n", 0, start) + 1
                lead = text.rfind("\n", 0, max(0, head - 1)) + 1
                if re.match(r"\s*template\s*<", text[lead:head]):
                    start = lead
                # ... and the blank line before it
                while start > 0 and text[start - 1] == "\n" and text[start - 2 : start - 1] == "\n":
                    start -= 1
                keep.append(text[cursor:start])
                cursor = be + 1
                dead.add(name)
                removed += 1
                changed = True
            if not changed:
                break
            keep.append(text[cursor:])
            open(path, "w").write(prune_empty_blocks("".join(keep)))
    return removed


def strip_nonx86(tree):
    """Remove the back-ends this host can never run, and their headers."""
    removed = 0
    for isa in NONX86:
        for rel in (isa, "simdutf/" + isa, "simdutf/" + isa + ".h"):
            removed += rm(tree, rel)
    # src/simdutf.cpp names them directly
    p = os.path.join(tree, "src/simdutf.cpp")
    text = open(p).read()
    names = ["simdutf/%s.h" % i for i in NONX86] + ["%s/implementation.cpp" % i for i in NONX86]
    open(p, "w").write(prune_empty_blocks(drop_includes(text, names)))
    return removed


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    tree, families = argv[1], argv[2:]
    for f in families:
        if f not in FAMILIES:
            raise SystemExit("strip.py: unknown family %r" % f)

    fallback = load_fallback(tree)
    if not fallback:
        raise SystemExit("strip.py: could not read the fallback implementation")

    total = strip_nonx86(tree)

    owned = []
    methods = set()
    base_class_methods = set()
    aux = set()
    scripts = []
    header_tables = {}
    for fam in families:
        spec = FAMILIES[fam]
        owned.extend(spec["owned"])
        methods.update(spec["methods"])
        base_class_methods.update(spec.get("base_class_methods", []))
        aux.update(spec.get("aux", []))
        scripts.extend(spec.get("scripts", []))
        header_tables.update(spec.get("header_tables", {}))

    # 1. methods -> portable reference, in every ISA implementation
    for isa in ISA_DIRS:
        p = os.path.join(tree, "src", isa, "implementation.cpp")
        if os.path.exists(p):
            strip_isa_file(p, methods, fallback, base_class_methods)

    # 2. file-local helpers with no caller left: those that reference a symbol
    #    about to vanish, and those the family names explicitly
    dead = defined_names([os.path.join(tree, "src", rel) for rel in owned])
    dead |= aux
    if dead:
        total += drop_dead_helpers(tree, dead)

    for rel in scripts:
        total += rm_path(os.path.join(tree, rel))

    if header_tables:
        total += drop_header_tables(tree, header_tables)

    # 3. delete the owned files and directories
    for rel in owned:
        total += rm(tree, rel)

    # 4. drop every #include of a deleted file
    targets = set(owned)
    for rel in owned:
        if rel.endswith(".cpp") or rel.endswith(".h"):
            continue
        # a directory: everything under it is gone at its include path too
        targets.add(rel + "/")
    for src in [os.path.join(tree, "src/simdutf.cpp")] + [
        os.path.join(tree, "src", isa, "implementation.cpp") for isa in ISA_DIRS
    ]:
        if not os.path.exists(src):
            continue
        text = open(src).read()
        keep = []
        for line in text.splitlines(True):
            s = line.strip()
            m = re.match(r'#include\s+"([^"]+)"', s)
            if m and (
                m.group(1) in targets
                or any(m.group(1).startswith(t) for t in targets if t.endswith("/"))
            ):
                continue
            keep.append(line)
        open(src, "w").write(prune_empty_blocks("".join(keep)))

    print(
        "strip.py: removed [%s] -> %d files/dirs gone from %s"
        % (",".join(families), total, tree)
    )


if __name__ == "__main__":
    main(sys.argv)
