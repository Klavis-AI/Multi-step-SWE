#!/usr/bin/env python3
"""Remove optimized kernel implementations from an Apache Arrow source tree.

Usage:
    strip.py <tree> <stage> [<stage> ...]

<tree> is the root of an Arrow checkout, i.e. the directory that contains the
`cpp/` source directory.  Each <stage> names one group of optimized
implementations; requesting a stage deletes the translation units that hold the
optimized code and rewrites every place that registered or called into them so
that the remaining scalar reference implementations are used instead.

The tree is modified in place.  Running the tool twice with the same arguments
is a no-op the second time.

The tool is deliberately structural: every region it removes is located either
by its own preprocessor guard (matched against the exact guard condition, with
the body verified to contain expected declarations) or by an exact line/macro
spelling.  If an expected construct is missing, the tool reports the file and
line and exits non-zero instead of editing blindly.
"""

import os
import sys

STAGE_NAMES = ("bpacking", "bpacking512", "bss", "levels", "bloomflt", "bloomacero",
               "rowkernels", "swissjoin")


class StripError(Exception):
    pass


def err(msg):
    raise StripError(msg)


# --------------------------------------------------------------------------
# text helpers
# --------------------------------------------------------------------------


def _lines(text):
    return text.split("\n")


def _directive(line):
    """Return (keyword, condition) for a preprocessor directive line, else (None, None)."""
    s = line.strip()
    if not s.startswith("#"):
        return None, None
    s = s[1:].lstrip()
    for kw in ("ifdef", "ifndef", "if", "endif", "else", "elif"):
        if s.startswith(kw) and (len(s) == len(kw) or not (s[len(kw)].isalnum() or s[len(kw)] == "_")):
            return kw, s[len(kw):].strip()
    return None, None


def _pp_block_end(lines, start, what):
    depth = 0
    for i in range(start, len(lines)):
        kw, _ = _directive(lines[i])
        if kw in ("if", "ifdef", "ifndef"):
            depth += 1
        elif kw == "endif":
            depth -= 1
            if depth == 0:
                return i
    err("%s: unterminated preprocessor block opened at line %d" % (what, start + 1))


_SOURCES = {}


def source(root, rel):
    """Return the single editor for <root>/<rel>, so stages can share one file."""
    path = os.path.join(root, rel)
    if path not in _SOURCES:
        _SOURCES[path] = Source(root, rel)
    return _SOURCES[path]


class Source(object):
    """A single file under edit, with change tracking."""

    def __init__(self, root, rel):
        self.rel = rel
        self.path = os.path.join(root, rel)
        if not os.path.isfile(self.path):
            err("%s: file does not exist" % rel)
        with open(self.path, "r") as fh:
            original = fh.read()
        self.original = original
        self.text = original
        self.last_removed = None

    def save(self):
        if self.text != self.original:
            with open(self.path, "w") as fh:
                fh.write(self.text)
            return True
        return False

    # -- removal primitives -------------------------------------------------

    def drop_guarded_region(self, cond, needles, label, leading_comments=0, strict=True):
        """Remove the `#if <cond> ... #endif` region whose body contains all needles.

        Returns True if a region was removed, False if it was already absent.  With
        strict set (the default) a needle that survives outside any region with this
        guard is reported as a malformed tree; region-by-region callers that expect
        the same needle under several guards pass strict=False.
        """
        lines = _lines(self.text)
        for start in range(len(lines)):
            kw, c = _directive(lines[start])
            if kw not in ("if", "ifdef", "ifndef") or c != cond:
                continue
            end = _pp_block_end(lines, start, self.rel)
            body = "\n".join(lines[start:end + 1])
            if not all(n in body for n in needles):
                continue
            lo = start
            for _ in range(leading_comments):
                if lo == 0 or not lines[lo - 1].strip().startswith("//"):
                    err("%s:%d: expected comment line above guarded region"
                        % (self.rel, start + 1))
                lo -= 1
            hi = end
            # Keep the blank-line rhythm of the surrounding code.
            if (hi + 1 < len(lines) and lines[hi + 1].strip() == ""
                    and lo > 0 and lines[lo - 1].strip() == ""):
                hi += 1
            del lines[lo:hi + 1]
            self.text = "\n".join(lines)
            self.last_removed = lo
            return True
        if not strict or not any(n in self.text for n in needles):
            return False
        err("%s: found no `#if %s` region containing %s" % (self.rel, cond, needles))

    def drop_guarded_calls(self, cond, needle, count, label):
        """Remove every `#if <cond>` region containing `needle`; expect `count` of them.

        Returns the number of regions removed: zero for an already-stripped tree.
        """
        found = 0
        while self.drop_guarded_region(cond, [needle], label, strict=False):
            found += 1
        if found not in (0, count):
            err("%s: %s: expected %d guarded region(s) around %r, found %d"
                % (self.rel, label, count, needle, found))
        return found

    def drop_guarded_tail(self, cond, label):
        """Remove the closing fragment of a call site split across two guarded regions.

        Such a call site reads `#if <cond> if (...) { <simd>; } else { #endif <scalar>
        ... #if <cond> } #endif`: the second region is a bare `}`.  It is located by
        its exact body, after the opening fragment just removed.
        """
        if self.last_removed is None:
            err("%s: %s: closing fragment requested without an opening fragment"
                % (self.rel, label))
        lines = _lines(self.text)
        for start in range(self.last_removed, len(lines)):
            kw, c = _directive(lines[start])
            if kw not in ("if", "ifdef", "ifndef") or c != cond:
                continue
            end = _pp_block_end(lines, start, self.rel)
            body = [l.strip() for l in lines[start + 1:end] if l.strip()]
            if body != ["}"]:
                continue
            del lines[start:end + 1]
            self.text = "\n".join(lines)
            self.last_removed = start
            return True
        err("%s: %s: no `#if %s` closing fragment after line %d"
            % (self.rel, label, cond, self.last_removed + 1))

    def drop_dispatch_rows(self, label, first, last, allowed, tail=("};",), keep=()):
        """Remove a contiguous run of lines from the one containing `first` through
        the one containing `last`, inside a dispatch target table.

        `tail` lists the accepted spellings of the line that follows the run: the
        table's closing `};`, or -- when only part of the table is being removed --
        the next target row, which must survive.
        """
        lines = _lines(self.text)
        for start in range(len(lines)):
            if first not in lines[start]:
                continue
            end = None
            for i in range(start, len(lines)):
                if last in lines[i]:
                    end = i
                    break
            if end is None or end + 1 >= len(lines):
                continue
            if not lines[end + 1].strip().startswith(tail):
                continue
            if not all(l.strip().startswith("//") or any(a in l for a in allowed)
                       for l in lines[start:end + 1]):
                continue
            del lines[start:end + 1]
            self.text = "\n".join(lines)
            return True
        self.require_no_arch_targets(label, keep=keep)
        return False

    def require_no_arch_targets(self, label, keep=()):
        for macro in ("ARROW_DISPATCH_TARGET_NEON", "ARROW_DISPATCH_TARGET_SVE128",
                      "ARROW_DISPATCH_TARGET_SVE256", "ARROW_DISPATCH_TARGET_SVE512",
                      "ARROW_DISPATCH_TARGET_SSE4_2", "ARROW_DISPATCH_TARGET_AVX2",
                      "ARROW_DISPATCH_TARGET_AVX512"):
            if macro in keep:
                continue
            if macro in self.text:
                err("%s: %s: a `%s` entry is still registered"
                    % (self.rel, label, macro))

    def drop_whole_line(self, stripped_text, label):
        """Remove the line whose stripped content equals `stripped_text`."""
        lines = _lines(self.text)
        hits = [i for i, l in enumerate(lines) if l.strip() == stripped_text]
        if len(hits) > 1:
            err("%s: %s: line %r appears %d times"
                % (self.rel, label, stripped_text, len(hits)))
        if not hits:
            return False
        del lines[hits[0]]
        self.text = "\n".join(lines)
        return True

    def drop_list_entry(self, filename, label):
        """Remove a source-file entry from a multi-line cmake list() call."""
        lines = _lines(self.text)
        hits = [i for i, l in enumerate(lines)
                if l.strip() == filename or l.strip() == filename + ")"]
        if len(hits) > 1:
            err("%s: %s: list entry %r appears %d times"
                % (self.rel, label, filename, len(hits)))
        if not hits:
            return False
        i = hits[0]
        if lines[i].strip() == filename + ")":
            # The closing paren lives on this line; move it to the previous entry.
            j = i - 1
            if j < 0 or lines[j].strip() == "":
                err("%s: %s: cannot re-attach the list closing paren" % (self.rel, label))
            lines[j] = lines[j] + ")"
        del lines[i]
        self.text = "\n".join(lines)
        return True

    def drop_property_token(self, filename, label):
        """Remove one file name from a `set_source_files_properties(...)` statement."""
        lines = _lines(self.text)
        for i, l in enumerate(lines):
            if not l.strip().startswith("set_source_files_properties("):
                continue
            if filename not in l:
                continue
            head, _, names = l.partition("set_source_files_properties(")
            if "PROPERTIES" in names:
                err("%s: %s: unexpected one-line set_source_files_properties"
                    % (self.rel, label))
            tokens = names.split()
            if tokens.count(filename) != 1:
                err("%s: %s: %r not a single token on line %d"
                    % (self.rel, label, filename, i + 1))
            tokens.remove(filename)
            if tokens:
                lines[i] = head + "set_source_files_properties(" + " ".join(tokens)
                del_lo, del_hi = i, i - 1  # nothing further to delete
            else:
                # The statement has no file names left: drop it, including the
                # continuation line that carries PROPERTIES and the closing paren.
                last = None
                for k in range(i, len(lines)):
                    if lines[k].strip().endswith(")"):
                        last = k
                        break
                if last is None:
                    err("%s: %s: unterminated set_source_files_properties"
                        % (self.rel, label))
                if not any("PROPERTIES" in lines[k] for k in range(i, last + 1)):
                    err("%s: %s: malformed set_source_files_properties statement"
                        % (self.rel, label))
                del_lo, del_hi = i, last
            if del_lo <= del_hi:
                del lines[del_lo:del_hi + 1]
            self.text = "\n".join(lines)
            return True
        return False

    def drop_cmake_if_block(self, open_line, needles, label):
        """Remove a cmake `if(<open_line>) ... endif()` block containing all needles."""
        lines = _lines(self.text)
        for start, l in enumerate(lines):
            if l.strip() != open_line:
                continue
            if "(" not in open_line:
                err("%s: %s: malformed cmake condition %r" % (self.rel, label, open_line))
            depth = 0
            end = None
            for i in range(start, len(lines)):
                t = lines[i].strip()
                if t.startswith("if(") or t.startswith("if ("):
                    depth += 1
                elif t.startswith("endif(") or t == "endif":
                    depth -= 1
                    if depth == 0:
                        end = i
                        break
            if end is None:
                err("%s: %s: unterminated cmake if at line %d"
                    % (self.rel, label, start + 1))
            body = "\n".join(lines[start:end + 1])
            if not all(n in body for n in needles):
                continue
            del lines[start:end + 1]
            self.text = "\n".join(lines)
            return True
        if not any(n in self.text for n in needles):
            return False
        err("%s: %s: found no cmake block for %r" % (self.rel, label, open_line))

    def drop_cmake_elseif_clause(self, marker, needle, label):
        """Remove an `elseif(<marker>) ... ` clause, leaving the `endif()` in place."""
        lines = _lines(self.text)
        for start, l in enumerate(lines):
            if not l.strip().startswith("elseif(") or marker not in l:
                continue
            end = None
            for i in range(start + 1, len(lines)):
                if lines[i].strip().startswith("endif(") or lines[i].strip() == "endif":
                    end = i
                    break
            if end is None:
                err("%s: %s: unterminated cmake elseif at line %d"
                    % (self.rel, label, start + 1))
            if needle not in "\n".join(lines[start:end]):
                err("%s: %s: elseif clause does not mention %r"
                    % (self.rel, label, needle))
            del lines[start:end]
            self.text = "\n".join(lines)
            return True
        return False

    def require_no_dangling_continuation(self, label):
        """Assert no `PROPERTIES ...` line was orphaned by a rewritten statement."""
        lines = _lines(self.text)
        for i, line in enumerate(lines):
            if not line.strip().startswith("PROPERTIES"):
                continue
            # Walk back over the continued argument lines of the statement.
            k = i - 1
            while k >= 0:
                if "set_source_files_properties(" in lines[k]:
                    break
                stripped = lines[k].strip()
                if stripped == "" or stripped.endswith(")"):
                    err("%s:%d: %s: orphaned `%s` continuation"
                        % (self.rel, i + 1, label, line.strip()))
                k -= 1
            else:
                err("%s:%d: %s: orphaned `%s` continuation"
                    % (self.rel, i + 1, label, line.strip()))

    def require_absent(self, needle, label):
        if needle in self.text:
            err("%s: %s: %r is still present" % (self.rel, label, needle))

    def require_present(self, needle, label):
        if needle not in self.text:
            err("%s: %s: expected %r to be present" % (self.rel, label, needle))


# --------------------------------------------------------------------------
# stage definitions
# --------------------------------------------------------------------------

DELETED_FILES = {
    "bpacking": [
        "src/arrow/util/bpacking_simd_128.cc",
        "src/arrow/util/bpacking_simd_256.cc",
        "src/arrow/util/bpacking_simd_kernel_internal.h",
    ],
    "bpacking512": [
        "src/arrow/util/bpacking_simd_avx512.cc",
        "src/arrow/util/bpacking_simd512_generated_internal.h",
        "src/arrow/util/bpacking_simd_codegen.py",
    ],
    "bss": [
        "src/arrow/util/byte_stream_split_internal_avx2.cc",
    ],
    "levels": [
        "src/parquet/level_conversion_bmi2.cc",
        "src/parquet/level_conversion_bmi2_internal.h",
        "src/parquet/level_comparison_avx2.cc",
        "src/parquet/level_comparison_avx2_internal.h",
    ],
    "bloomflt": [
        "src/parquet/bloom_filter_avx2.cc",
        "src/parquet/bloom_filter_avx2_internal.h",
    ],
    "bloomacero": [
        "src/arrow/acero/bloom_filter_avx2.cc",
    ],
    "rowkernels": [
        "src/arrow/compute/row/compare_internal_avx2.cc",
        "src/arrow/compute/row/encode_internal_avx2.cc",
        "src/arrow/compute/row/row_util_avx2_internal.h",
        "src/arrow/compute/key_hash_internal_avx2.cc",
        "src/arrow/compute/key_map_internal_avx2.cc",
        "src/arrow/compute/util_avx2.cc",
    ],
    "swissjoin": [
        "src/arrow/acero/swiss_join_avx2.cc",
    ],
}

ARROW_RUNTIME_AVX2 = "defined(ARROW_HAVE_RUNTIME_AVX2)"
ARROW_RUNTIME_AVX2_BMI2 = ("defined(ARROW_HAVE_RUNTIME_AVX2) && "
                           "defined(ARROW_HAVE_RUNTIME_BMI2)")


def strip_bpacking(root, edits):
    # The 128-bit and 256-bit bit-unpacking kernels: the 128-bit TU defines both
    # unpack_sse4_2 and unpack_neon, the 256-bit one both unpack_avx2 and
    # unpack_sve256, so all four dispatch rows and both TUs go together.  The
    # AVX-512 kernel is a separate stage (bpacking512).
    src = source(root, "src/arrow/util/bpacking.cc")
    src.drop_dispatch_rows(
        "bit-unpacking dispatch table",
        first="ARROW_DISPATCH_TARGET_NEON(&bpacking::unpack_neon<Uint>)",
        last="ARROW_DISPATCH_TARGET_AVX2(&bpacking::unpack_avx2<Uint>)",
        allowed=["ARROW_DISPATCH_TARGET_", "GH-50503"],
        tail=("};", "ARROW_DISPATCH_TARGET_AVX512"),
        keep=("ARROW_DISPATCH_TARGET_AVX512",))
    src.require_no_arch_targets("bit-unpacking dispatch table",
                                keep=("ARROW_DISPATCH_TARGET_AVX512",))
    src.require_present("ARROW_DISPATCH_TARGET_NONE(&bpacking::unpack_scalar<Uint>)",
                        "bit-unpacking dispatch table")
    edits.append(src)

    cmake = source(root, "src/arrow/CMakeLists.txt")
    cmake.drop_list_entry("util/bpacking_simd_128.cc", "arrow util sources")
    cmake.drop_whole_line("append_runtime_avx2_src(ARROW_UTIL_SRCS util/bpacking_simd_256.cc)",
                          "arrow util sources")
    cmake.drop_whole_line("append_runtime_sve256_src(ARROW_UTIL_SRCS util/bpacking_simd_256.cc)",
                          "arrow util sources")
    for name in ("bpacking_simd_128", "bpacking_simd_256"):
        cmake.require_absent(name, "arrow util sources")
    edits.append(cmake)

    meson = source(root, "src/arrow/meson.build")
    meson.drop_whole_line("'util/bpacking_simd_128.cc',", "arrow meson sources")
    edits.append(meson)

    test = source(root, "src/arrow/util/bpacking_test.cc")
    for cond, needle in (
            ("defined(ARROW_HAVE_SSE4_2)", "bpacking::unpack_sse4_2<TypeParam>"),
            (ARROW_RUNTIME_AVX2, "bpacking::unpack_avx2<TypeParam>"),
            ("defined(ARROW_HAVE_NEON)", "bpacking::unpack_neon<TypeParam>"),
            ("defined(ARROW_HAVE_RUNTIME_SVE256)", "bpacking::unpack_sve256<TypeParam>")):
        test.drop_guarded_region(cond, [needle], "bit-unpacking tests")
    for needle in ("unpack_sse4_2<TypeParam>", "unpack_avx2<TypeParam>",
                   "unpack_neon<TypeParam>", "unpack_sve256<TypeParam>"):
        test.require_absent(needle, "bit-unpacking tests")
    test.require_present("TYPED_TEST(TestUnpack, UnpackScalar)", "bit-unpacking tests")
    test.require_present("TYPED_TEST(TestUnpack, Unpack)", "bit-unpacking tests")
    edits.append(test)

    bench = source(root, "src/arrow/util/bpacking_benchmark.cc")
    for cond, needle in (
            ("defined(ARROW_HAVE_SSE4_2)", "bpacking::unpack_sse4_2"),
            (ARROW_RUNTIME_AVX2, "bpacking::unpack_avx2"),
            ("defined(ARROW_HAVE_NEON)", "bpacking::unpack_neon"),
            ("defined(ARROW_HAVE_RUNTIME_SVE256)", "bpacking::unpack_sve256")):
        bench.drop_guarded_calls(cond, needle, 1, "bit-unpacking benchmark")
    for needle in ("unpack_sse4_2", "unpack_avx2", "unpack_neon", "unpack_sve256"):
        bench.require_absent(needle, "bit-unpacking benchmark")
    edits.append(bench)


def strip_bpacking512(root, edits):
    # The AVX-512 bit-unpacking kernel and the generator that emits its
    # generated table.
    src = source(root, "src/arrow/util/bpacking.cc")
    src.drop_whole_line("ARROW_DISPATCH_TARGET_AVX512(&bpacking::unpack_avx512<Uint>)  //",
                        "bit-unpacking dispatch table")
    src.require_absent("ARROW_DISPATCH_TARGET_AVX512", "bit-unpacking dispatch table")
    src.require_present("ARROW_DISPATCH_TARGET_NONE(&bpacking::unpack_scalar<Uint>)",
                        "bit-unpacking dispatch table")
    edits.append(src)

    cmake = source(root, "src/arrow/CMakeLists.txt")
    cmake.drop_whole_line("append_runtime_avx512_src(ARROW_UTIL_SRCS util/bpacking_simd_avx512.cc)",
                          "arrow util sources")
    for name in ("bpacking_simd_avx512", "bpacking_simd512_generated"):
        cmake.require_absent(name, "arrow util sources")
    edits.append(cmake)

    test = source(root, "src/arrow/util/bpacking_test.cc")
    test.drop_guarded_region("defined(ARROW_HAVE_RUNTIME_AVX512)",
                             ["bpacking::unpack_avx512<TypeParam>"], "bit-unpacking tests")
    test.require_absent("unpack_avx512<TypeParam>", "bit-unpacking tests")
    test.require_present("TYPED_TEST(TestUnpack, UnpackScalar)", "bit-unpacking tests")
    edits.append(test)

    bench = source(root, "src/arrow/util/bpacking_benchmark.cc")
    bench.drop_guarded_calls("defined(ARROW_HAVE_RUNTIME_AVX512)",
                             "bpacking::unpack_avx512", 1, "bit-unpacking benchmark")
    bench.require_absent("unpack_avx512", "bit-unpacking benchmark")
    edits.append(bench)


def strip_bss(root, edits):
    src = source(root, "src/arrow/util/byte_stream_split_internal.h")
    src.drop_guarded_region(
        "defined(ARROW_HAVE_NEON) || defined(ARROW_HAVE_RUNTIME_SSE4_2)",
        ["xsimd/xsimd.hpp", "ARROW_HAVE_SIMD_SPLIT"],
        "byte-stream-split header", leading_comments=2)
    src.drop_guarded_region(
        "defined(ARROW_HAVE_SIMD_SPLIT)",
        ["xsimd::batch", "ByteStreamSplitEncodeAvx2<", "zip_hi_n"],
        "byte-stream-split header")
    src.require_absent("ARROW_HAVE_SIMD_SPLIT", "byte-stream-split header")
    src.require_absent("xsimd", "byte-stream-split header")
    src.require_present("inline void ByteStreamSplitDecode(", "byte-stream-split header")
    edits.append(src)

    src = source(root, "src/arrow/util/byte_stream_split_internal.cc")
    src.drop_dispatch_rows(
        "byte-stream-split decode dispatch table",
        first="ARROW_DISPATCH_TARGET_NEON(",
        last="xsimd::avx2, kNumStreams>",
        allowed=["ARROW_DISPATCH_TARGET_", "xsimd::"])
    src.drop_dispatch_rows(
        "byte-stream-split encode dispatch table",
        first="ARROW_DISPATCH_TARGET_NEON(",
        last="ByteStreamSplitEncodeAvx2<kNumStreams>",
        allowed=["ARROW_DISPATCH_TARGET_", "xsimd::", "ByteStreamSplitEncodeAvx2"])
    src.require_no_arch_targets("byte-stream-split dispatch tables")
    src.require_present("ARROW_DISPATCH_TARGET_NONE(&ByteStreamSplitDecodeScalar<kNumStreams>)",
                        "byte-stream-split dispatch tables")
    src.require_present("ARROW_DISPATCH_TARGET_NONE(&ByteStreamSplitEncodeScalar<kNumStreams>)",
                        "byte-stream-split dispatch tables")
    src.require_present("void ByteStreamSplitDecodeSimdDispatch<", "byte-stream-split dispatch")
    src.require_present("void ByteStreamSplitEncodeSimdDispatch<", "byte-stream-split dispatch")
    edits.append(src)

    cmake = source(root, "src/arrow/CMakeLists.txt")
    cmake.drop_whole_line(
        "append_runtime_avx2_src(ARROW_UTIL_SRCS util/byte_stream_split_internal_avx2.cc)",
        "arrow util sources")
    cmake.require_absent("byte_stream_split_internal_avx2.cc", "arrow util sources")
    edits.append(cmake)

    # parquet/encoding_benchmark.cc reaches the SIMD entry points through
    # positions that no longer exist.
    bench = source(root, "src/parquet/encoding_benchmark.cc")
    for cond in ("defined(ARROW_HAVE_SSE4_2)", "defined(ARROW_HAVE_AVX2)",
                 "defined(ARROW_HAVE_NEON)"):
        bench.drop_guarded_calls(cond, "ByteStreamSplitDecodeSimd", 1,
                                 "byte-stream-split benchmark")
    bench.require_absent("xsimd::", "byte-stream-split benchmark")
    edits.append(bench)


def strip_levels(root, edits):
    src = source(root, "src/parquet/level_conversion.cc")
    src.drop_guarded_region(
        "defined(ARROW_HAVE_RUNTIME_BMI2)",
        ['level_conversion_bmi2_internal.h'],
        "level conversion")
    src.drop_guarded_region(
        "defined(ARROW_HAVE_RUNTIME_BMI2)",
        ["DefLevelsToBitmapBmi2WithRepeatedParent"],
        "level conversion")
    src.require_absent("level_conversion_bmi2_internal.h", "level conversion")
    src.require_absent("DefLevelsToBitmapBmi2WithRepeatedParent", "level conversion")
    src.require_present("standard::DefLevelsToBitmapSimd</*has_repeated_parent=*/true>",
                        "level conversion")
    edits.append(src)

    src = source(root, "src/parquet/level_comparison.cc")
    src.drop_guarded_region(
        ARROW_RUNTIME_AVX2,
        ['level_comparison_avx2_internal.h'],
        "level comparison")
    src.drop_whole_line("// defined in level_comparison_avx2.cc", "level comparison")
    src.drop_dispatch_rows(
        "level comparison dispatch table",
        first="ARROW_DISPATCH_TARGET_AVX2(&GreaterThanBitmapAvx2)",
        last="ARROW_DISPATCH_TARGET_AVX2(&GreaterThanBitmapAvx2)",
        allowed=["ARROW_DISPATCH_TARGET_AVX2(&GreaterThanBitmapAvx2)"])
    src.drop_dispatch_rows(
        "level comparison dispatch table",
        first="ARROW_DISPATCH_TARGET_AVX2(&FindMinMaxAvx2)",
        last="ARROW_DISPATCH_TARGET_AVX2(&FindMinMaxAvx2)",
        allowed=["ARROW_DISPATCH_TARGET_AVX2(&FindMinMaxAvx2)"])
    src.require_absent("level_comparison_avx2_internal.h", "level comparison")
    src.require_no_arch_targets("level comparison dispatch table")
    src.require_present("ARROW_DISPATCH_TARGET_NONE(&standard::GreaterThanBitmapImpl)",
                        "level comparison dispatch table")
    src.require_present("ARROW_DISPATCH_TARGET_NONE(&standard::FindMinMaxImpl)",
                        "level comparison dispatch table")
    edits.append(src)

    cmake = source(root, "src/parquet/CMakeLists.txt")
    cmake.drop_list_entry("level_comparison_avx2.cc", "parquet sources")
    cmake.drop_list_entry("level_conversion_bmi2.cc", "parquet sources")
    cmake.drop_property_token("level_comparison_avx2.cc", "parquet sources")
    cmake.drop_cmake_if_block("if(ARROW_HAVE_RUNTIME_BMI2)",
                              ["set_source_files_properties(level_conversion_bmi2.cc"],
                              "parquet sources")
    cmake.require_no_dangling_continuation("parquet sources")
    cmake.require_absent("level_comparison_avx2.cc", "parquet sources")
    cmake.require_absent("level_conversion_bmi2.cc", "parquet sources")
    edits.append(cmake)


def strip_bloomflt(root, edits):
    # Parquet's split-block bloom filter only: the acero (join) blocked bloom
    # filter is a separate, independently removable stage (bloomacero).
    src = source(root, "src/parquet/bloom_filter.cc")
    src.drop_guarded_region(
        "defined(ARROW_HAVE_AVX2) || defined(ARROW_HAVE_RUNTIME_AVX2)",
        ['bloom_filter_avx2_internal.h'],
        "parquet bloom filter")
    src.drop_dispatch_rows(
        "parquet bloom filter dispatch table",
        first="ARROW_DISPATCH_TARGET_AVX2(&FindHashBlockAvx2)",
        last="ARROW_DISPATCH_TARGET_AVX2(&FindHashBlockAvx2)",
        allowed=["ARROW_DISPATCH_TARGET_AVX2(&FindHashBlockAvx2)"])
    src.require_absent("bloom_filter_avx2_internal.h", "parquet bloom filter")
    src.require_no_arch_targets("parquet bloom filter dispatch table")
    src.require_present("ARROW_DISPATCH_TARGET_NONE(&FindHashBlockImpl)",
                        "parquet bloom filter dispatch table")
    edits.append(src)

    cmake = source(root, "src/parquet/CMakeLists.txt")
    cmake.drop_list_entry("bloom_filter_avx2.cc", "parquet sources")
    cmake.drop_property_token("bloom_filter_avx2.cc", "parquet sources")
    cmake.drop_cmake_elseif_clause('ARROW_SIMD_LEVEL MATCHES "^(AVX2|AVX512)$"',
                                   "bloom_filter_avx2.cc", "parquet sources")
    cmake.require_no_dangling_continuation("parquet sources")
    cmake.require_absent("bloom_filter_avx2.cc", "parquet sources")
    edits.append(cmake)

    # The parquet test that drives the probe kernel: it belongs to this stage
    # (it includes parquet/bloom_filter_avx2_internal.h, which is deleted above).
    test = source(root, "src/parquet/bloom_filter_test.cc")
    test.drop_guarded_region(
        ARROW_RUNTIME_AVX2,
        ['bloom_filter_avx2_internal.h'],
        "parquet bloom filter tests")
    test.drop_guarded_region(
        ARROW_RUNTIME_AVX2,
        ["FindHashBlockAvx2", "BloomFilterProbeKernel"],
        "parquet bloom filter tests", leading_comments=2)
    test.require_absent("bloom_filter_avx2_internal.h", "parquet bloom filter tests")
    test.require_absent("FindHashBlockAvx2", "parquet bloom filter tests")
    test.require_absent("BloomFilterProbeKernel", "parquet bloom filter tests")
    test.require_present("TEST(ConstructorTest, TestBloomFilter)",
                         "parquet bloom filter tests")
    test.require_present("}  // namespace parquet", "parquet bloom filter tests")
    edits.append(test)


def strip_bloomacero(root, edits):
    # Acero's in-memory blocked bloom filter as used by the join.  It is a
    # different algorithm from parquet's split-block filter, so it is removed by
    # its own stage: the two avx2 kernels are unrelated and neither stage
    # depends on the other.
    src = source(root, "src/arrow/acero/bloom_filter.cc")
    src.drop_guarded_calls(ARROW_RUNTIME_AVX2, "Insert_avx2(num_rows, hashes)",
                           2, "acero bloom filter")
    src.drop_guarded_calls(ARROW_RUNTIME_AVX2, "Find_avx2(num_rows, hashes,",
                           2, "acero bloom filter")
    src.require_absent("_avx2", "acero bloom filter")
    src.require_present("InsertImp(num_rows - num_processed, hashes + num_processed);",
                        "acero bloom filter")
    edits.append(src)

    cmake = source(root, "src/arrow/acero/CMakeLists.txt")
    cmake.drop_whole_line("append_runtime_avx2_src(ARROW_ACERO_SRCS bloom_filter_avx2.cc)",
                          "acero sources")
    cmake.require_absent("bloom_filter_avx2.cc", "acero sources")
    edits.append(cmake)


def strip_rowkernels(root, edits):
    # -- row compare --------------------------------------------------------
    src = source(root, "src/arrow/compute/row/compare_internal.cc")
    for needle in ("NullUpdateColumnToRow_avx2(",
                   "CompareBinaryColumnToRow_avx2(",
                   "CompareVarBinaryColumnToRow_avx2(",
                   "AndByteVectors_avx2("):
        src.drop_guarded_calls(ARROW_RUNTIME_AVX2, needle, 1, "row compare")
    src.require_absent("_avx2", "row compare")
    edits.append(src)

    # -- row encode/decode --------------------------------------------------
    src = source(root, "src/arrow/compute/row/encode_internal.cc")
    for needle in ("DecodeHelper_avx2(is_row_fixed_length, start_row",
                   "DecodeHelper_avx2(start_row, num_rows, varbinary_col_id"):
        # `if (has_avx2()) { ... } else {` ... `}` split across two guarded regions
        if src.drop_guarded_calls(ARROW_RUNTIME_AVX2, needle, 1, "row encode"):
            src.drop_guarded_tail(ARROW_RUNTIME_AVX2, "row encode")
    src.drop_guarded_calls(ARROW_RUNTIME_AVX2,
                           "DecodeHelper_avx2(is_row_fixed_length, col_width1",
                           1, "row encode")
    src.require_absent("DecodeHelper_avx2", "row encode")
    src.require_absent("DecodeImp_avx2", "row encode")
    edits.append(src)

    # -- key hash -----------------------------------------------------------
    src = source(root, "src/arrow/compute/key_hash_internal.cc")
    src.drop_guarded_calls(ARROW_RUNTIME_AVX2, "num_processed = HashVarLen_avx2(",
                           2, "key hash")
    src.drop_guarded_calls(ARROW_RUNTIME_AVX2, "num_processed = HashFixedLen_avx2(",
                           1, "key hash")
    src.require_absent("_avx2", "key hash")
    edits.append(src)

    # -- key map / swiss table ---------------------------------------------
    src = source(root, "src/arrow/compute/key_map_internal.cc")
    for needle in ("extract_group_ids_avx2(", "early_filter_imp_avx2_x32("):
        src.drop_guarded_calls(ARROW_RUNTIME_AVX2_BMI2, needle, 1, "key map")
    src.require_absent("_avx2", "key map")
    edits.append(src)

    # -- bit utilities ------------------------------------------------------
    src = source(root, "src/arrow/compute/util.cc")
    if src.drop_guarded_calls(ARROW_RUNTIME_AVX2_BMI2, "avx2::bits_filter_indexes_avx2(",
                              1, "bit utilities"):
        src.drop_guarded_tail(ARROW_RUNTIME_AVX2_BMI2, "bit utilities")
    for needle in ("avx2::bits_to_bytes_avx2(", "avx2::bytes_to_bits_avx2(",
                   "avx2::are_all_bytes_zero_avx2("):
        src.drop_guarded_calls(ARROW_RUNTIME_AVX2_BMI2, needle, 1, "bit utilities")
    src.require_absent("avx2::", "bit utilities")
    edits.append(src)

    cmake = source(root, "src/arrow/CMakeLists.txt")
    for line in (
            "append_runtime_avx2_src(ARROW_COMPUTE_LIB_SRCS compute/key_hash_internal_avx2.cc)",
            "append_runtime_avx2_bmi2_src(ARROW_COMPUTE_LIB_SRCS compute/key_map_internal_avx2.cc)",
            "append_runtime_avx2_src(ARROW_COMPUTE_LIB_SRCS compute/row/compare_internal_avx2.cc)",
            "append_runtime_avx2_src(ARROW_COMPUTE_LIB_SRCS compute/row/encode_internal_avx2.cc)",
            "append_runtime_avx2_bmi2_src(ARROW_COMPUTE_LIB_SRCS compute/util_avx2.cc)"):
        cmake.drop_whole_line(line, "arrow compute sources")
    for name in ("key_hash_internal_avx2", "key_map_internal_avx2",
                 "compare_internal_avx2", "encode_internal_avx2", "util_avx2"):
        cmake.require_absent(name, "arrow compute sources")
    edits.append(cmake)


def strip_swissjoin(root, edits):
    src = source(root, "src/arrow/acero/swiss_join.cc")
    # The first region also declares `use_avx2`, which the other four test.
    src.drop_guarded_calls("ARROW_HAVE_RUNTIME_AVX2", "num_rows_to_preprocess",
                           1, "swiss join")
    for needle in ("num_rows_processed = DecodeFixedLength_avx2(",
                   "num_rows_processed = DecodeOffsets_avx2(",
                   "num_rows_processed = DecodeVarLength_avx2(",
                   "DecodeNulls_avx2(output, num_rows_before, column_id,"
                   " num_rows_to_append, row_ids);"):
        src.drop_guarded_calls("ARROW_HAVE_RUNTIME_AVX2", needle, 1, "swiss join")
    src.require_absent("_avx2", "swiss join")
    src.require_absent("use_avx2", "swiss join")
    src.require_present("DecodeFixedLength(output, num_rows_before + num_rows_processed",
                        "swiss join")
    edits.append(src)

    cmake = source(root, "src/arrow/acero/CMakeLists.txt")
    cmake.drop_whole_line("append_runtime_avx2_src(ARROW_ACERO_SRCS swiss_join_avx2.cc)",
                          "acero sources")
    cmake.require_absent("swiss_join_avx2.cc", "acero sources")
    edits.append(cmake)


STRIPPERS = {
    "bpacking": strip_bpacking,
    "bpacking512": strip_bpacking512,
    "bss": strip_bss,
    "levels": strip_levels,
    "bloomflt": strip_bloomflt,
    "bloomacero": strip_bloomacero,
    "rowkernels": strip_rowkernels,
    "swissjoin": strip_swissjoin,
}


# --------------------------------------------------------------------------
# driver
# --------------------------------------------------------------------------


def find_source_root(tree):
    if os.path.isdir(os.path.join(tree, "cpp", "src", "arrow")):
        return os.path.join(tree, "cpp")
    if os.path.isdir(os.path.join(tree, "src", "arrow")):
        return tree
    err("%s: does not look like an Arrow source tree (no cpp/src/arrow)" % tree)


def check_no_dangling_references(root, names):
    """Assert that nothing left in the tree still names a removed translation unit.

    Two directories can hold sources with the same name (parquet/ and
    arrow/acero/ both have a bloom_filter_avx2.cc, and each is removed by a
    different stage), so a bare-name mention only counts as dangling when the
    referring file cannot resolve it to a file that still exists beside it.
    """
    wanted = set(names)
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        survivors = set(filenames)
        for name in filenames:
            path = os.path.join(dirpath, name)
            try:
                if os.path.getsize(path) > 4 * 1024 * 1024:
                    continue
                with open(path, "r", errors="ignore") as fh:
                    text = fh.read()
            except OSError:
                continue
            for needle in wanted:
                if needle in text and needle not in survivors:
                    err("%s: still references the removed file %s"
                        % (os.path.relpath(path, root), needle))


def run(tree, stages):
    root = find_source_root(tree)
    _SOURCES.clear()
    edits = []
    for stage in stages:
        STRIPPERS[stage](root, edits)

    removed = []
    for rel in sorted({rel for stage in stages for rel in DELETED_FILES[stage]}):
        path = os.path.join(root, rel)
        if os.path.isfile(path):
            os.remove(path)
            removed.append(rel)

    rewritten = []
    for edit in _SOURCES.values():
        if edit.save():
            rewritten.append(edit.rel)
    _SOURCES.clear()

    check_no_dangling_references(
        root, [os.path.basename(rel) for stage in stages for rel in DELETED_FILES[stage]])
    return removed, rewritten


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    tree = argv[1]
    stages = argv[2:]
    for stage in stages:
        if stage not in STRIPPERS:
            sys.stderr.write("strip.py: unknown stage %r (known: %s)\n"
                             % (stage, ", ".join(STAGE_NAMES)))
            return 2
    try:
        removed, rewritten = run(tree, stages)
    except StripError as exc:
        sys.stderr.write("strip.py: %s\n" % exc)
        return 1
    if not stages:
        return 0
    print("removed %d file(s)" % len(removed))
    for rel in removed:
        print("  %s" % rel)
    print("rewrote %d file(s)" % len(rewritten))
    for rel in rewritten:
        print("  %s" % rel)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
