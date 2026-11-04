#!/usr/bin/env python3
"""Remove one or more primitive families from a Simd source tree.

Usage: strip.py <tree> <family> [<family> ...]
Families: resizer blur edges cascade hog warp stats background (or 1..8).

Per family, working on <tree>/src:

  * deletes every `Simd{Isa}<Family>*.{cpp,h}` file for every ISA prefix,
    including the non-x86 back ends (Neon, Sve2, Hvx, ...) -- the same
    algorithm in another dialect is a translation exercise, not a derivation.
    `SimdBase<Family>*` is NOT deleted: it is the scalar reference, and it is
    what the stripped tree then runs.

  * removes that family's branches from the `SimdLib.cpp` dispatch ladders, in
    both spellings the library uses: the `#ifdef SIMD_<ISA>_ENABLE ...
    <Isa>::<Fn>(...) ... else #endif` block form, and the
    `SIMD_FUNC<n>(<Fn>, SIMD_<ISA>_FUNC, ...)` macro form.

  * removes that family's per-ISA autotest hooks from `src/Test/*.cpp` -- the
    `#ifdef SIMD_<ISA>_ENABLE` blocks that call `Simd::<Isa>::<Fn>`.  Without
    this the stripped tree does not compile.  The unguarded `Simd::Base::<Fn>`
    hook stays, so the framework still compares whatever is installed against
    the scalar reference.

Idempotent.
"""
import glob
import os
import re
import sys

FAMILIES = {
    'resizer':    ['Resizer*'],
    'blur':       ['GaussianBlur*', 'RecursiveBilateralFilter*', 'MedianFilter*'],
    'edges':      ['Sobel*', 'Laplace*', 'Texture*'],
    'cascade':    ['Detection*', 'Integral*'],
    'hog':        ['Hog*'],
    'warp':       ['WarpAffine*', 'ShiftBilinear*', 'Transform*'],
    'stats':      ['Statistic*', 'Conditional*', 'Histogram*',
                   'SquaredDifferenceSum*', 'AbsDifference*'],
    'background': ['Background*', 'Binarization*', 'Segmentation*', 'Fill*', 'Operation*'],
}
ORDER = ['resizer', 'blur', 'edges', 'cascade', 'hog', 'warp', 'stats', 'background']
ALIAS = {str(i + 1): n for i, n in enumerate(ORDER)}

# ISA prefixes whose .cpp files may implement a family.  Base is deliberately
# absent: the scalar reference stays in the tree.
ISA_PREFIXES = ['Sse41', 'Avx2', 'Avx512bw', 'Avx512vnni', 'AmxBf16',
                'Neon', 'Sve2', 'Hvx', 'Vmx', 'Vsx']

# ladder ISA macro suffix -> C++ namespace
ISA_NS = {'AVX512BW': 'Avx512bw', 'AVX2': 'Avx2', 'SSE41': 'Sse41',
          'AVX512VNNI': 'Avx512vnni', 'AMXBF16': 'AmxBf16',
          'NEON': 'Neon', 'SVE2': 'Sve2', 'HVX': 'Hvx'}

# C++ namespaces whose code the graded build does not compile: the same set
# tests/ENVELOPE.stepNN lists as `uncompiled` (AVX-512BW/VNNI, AMX-BF16, NEON,
# SVE2, HVX, VMX, VSX; VMX/VSX have no ISA_NS entry, so they never reach here).
# Their declarations are dead on this host -- their implementation files are not
# in the build and every reference to them sits under an undefined
# SIMD_<ISA>_ENABLE guard -- so removing them cannot break a build, which is
# what lets them go out of a header that must otherwise be left whole.
DEAD_ISA_NS = {'Avx512bw', 'Avx512vnni', 'AmxBf16', 'Neon', 'Sve2', 'Hvx'}


def defined_symbols(text):
    out = set()
    for m in re.finditer(r'\b(\w+)::(\w+)\s*\(', text):
        out.add(m.group(2))
    for m in re.finditer(r'(?m)^\s*(?:SIMD_INLINE\s+|static\s+|inline\s+|'
                         r'template\s*<[^>]*>\s*)*[\w][\w\s\*&:<>]*?\b(\w+)\s*\(', text):
        out.add(m.group(1))
    for m in re.finditer(r'(?m)^\s*(?:struct|class)\s+(\w+)', text):
        out.add(m.group(1))
    return out


def ifdef_blocks(text):
    """(start_line, end_line, block_text) for each balanced #if*/#endif."""
    lines = text.split('\n')
    stack = []
    for i, line in enumerate(lines):
        s = line.strip()
        if re.match(r'#\s*if(def)?\b', s):
            stack.append(i)
        elif re.match(r'#\s*endif\b', s):
            if stack:
                start = stack.pop()
                yield start, i, '\n'.join(lines[start:i + 1])


def main():
    tree = sys.argv[1]
    args = sys.argv[2:]
    # --isa <Prefix,...> restricts the strip to those ISA prefixes, which is how
    # the candidate-shaped validation tree is built: a candidate may restore one
    # ISA and not another, and the tree must still compile against the restored,
    # uniformly hook-stripped test framework.
    isas = None
    if '--isa' in args:
        i = args.index('--isa')
        isas = args[i + 1].split(',')
        args = args[:i] + args[i + 2:]
    names = []
    for a in args:
        a = ALIAS.get(a, a)
        if a not in FAMILIES:
            sys.exit('strip.py: unknown family %r' % (a,))
        names.append(a)
    src_dir = os.path.join(tree, 'src', 'Simd')
    test_dir = os.path.join(tree, 'src', 'Test')

    prefixes = [p for p in ISA_PREFIXES if isas is None or p in isas]
    # ns -> stripped symbols defined for that ISA
    isa_syms = {}
    removed = 0
    for name in names:
        for prefix in prefixes:
            files = set()
            for g in FAMILIES[name]:
                files |= set(glob.glob(os.path.join(src_dir, 'Simd%s%s.cpp' % (prefix, g))))
                files |= set(glob.glob(os.path.join(src_dir, 'Simd%s%s.h' % (prefix, g))))
            syms = set()
            for f in files:
                if f.endswith('.cpp'):
                    syms |= defined_symbols(open(f, errors='replace', newline='').read())
                os.remove(f)
                removed += 1
            if syms:
                isa_syms.setdefault(prefix, set()).update(syms)
    isa_syms = {k: v for k, v in isa_syms.items() if k in ISA_NS.values()}

    # Symbols that a *remaining* file defines must not be treated as stripped:
    # a helper name such as `Copy` is shared across families, and dropping
    # another family's dispatch branch over a name collision would be silent.
    kept_syms = {}
    for prefix in ISA_PREFIXES:
        s = set()
        for f in glob.glob(os.path.join(src_dir, 'Simd%s*.cpp' % prefix)):
            s |= defined_symbols(open(f, errors='replace', newline='').read())
        kept_syms[prefix] = s
    for prefix in list(isa_syms):
        isa_syms[prefix] -= kept_syms.get(prefix, set())
        if not isa_syms[prefix]:
            del isa_syms[prefix]

    def calls_stripped(block, ns):
        syms = isa_syms.get(ns)
        if not syms:
            return False
        return any(re.search(r'\b%s::%s\b' % (ns, re.escape(s)), block) for s in syms)

    # --- dispatch ladders in SimdLib.cpp -------------------------------------
    lib = os.path.join(src_dir, 'SimdLib.cpp')
    text = open(lib, errors='replace', newline='').read()
    lines = text.split('\n')
    drop = set()
    for start, end, block in ifdef_blocks(text):
        m = re.match(r'#\s*ifdef\s+SIMD_(\w+)_ENABLE', block.split('\n')[0].strip())
        if m and m.group(1) in ISA_NS and calls_stripped(block, ISA_NS[m.group(1)]):
            drop |= set(range(start, end + 1))
    text = '\n'.join(l for i, l in enumerate(lines) if i not in drop)

    def fix_func(m):
        args = [a.strip() for a in m.group(1).split(',')]
        keep = [args[0]]
        for a in args[1:]:
            mm = re.match(r'SIMD_(\w+)_FUNC$', a)
            if mm and mm.group(1) in ISA_NS and args[0] in isa_syms.get(ISA_NS[mm.group(1)], ()):
                continue
            keep.append(a)
        return 'SIMD_FUNC%d(%s)' % (len(keep) - 1, ', '.join(keep))

    text = re.sub(r'SIMD_FUNC\d\(([^()]*)\)', fix_func, text)
    open(lib, 'w', newline='').write(text)

    # --- ISA code left in a family-private header ----------------------------
    # Not every one of a family's kernels lives in a Simd<Isa><Family>*.cpp file.
    # SimdResizerCommon.h carries the resizer's 128/256-bit bilinear column and
    # area kernels in its `namespace Sse41` / `namespace Avx2` blocks: dead code
    # in the stripped tree, never compiled, and exactly the inner loop this task
    # asks the candidate to write. If every file that still includes such a
    # header after the strip is one of the family's own Base files, the whole ISA
    # namespace goes with the family. If any includer survives from another
    # family the header is left alone and the leak is reported instead -- a
    # shared header cannot be gutted without breaking the family that shares it
    # (SimdTransform.h's shuffle primitives are also used by SynetPermute).
    #
    # A shared header is not all-or-nothing, though.  The back ends this build
    # does not compile (DEAD_ISA_NS) are dead code here, so nothing needs the
    # declarations in those namespaces and they go even out of a header that has
    # to be left whole.  What stays in such a header is the in-envelope half
    # (Sse41/Avx2), which the family's own implementations -- the tree that
    # restores the family -- are compiled against; a header declaring those is
    # the residue the procedure accepts, not a defect this pass removes.
    import glob as _glob
    stems = [g[:-1] for name in names for g in FAMILIES[name]]
    for hdr in sorted(set(_glob.glob(os.path.join(src_dir, 'Simd*.h')))):
        base = os.path.basename(hdr)
        # only a header that belongs to one of the stripped families: the rule is
        # about this family's private ISA code, not about every header that
        # happens to have an ISA namespace block.
        if not any(stem in base for stem in stems):
            continue
        users = set()
        for f in _glob.glob(os.path.join(src_dir, '*.cpp')) + _glob.glob(os.path.join(src_dir, '*.hpp')):
            if '#include "Simd/%s"' % base in open(f, errors='replace', newline='').read():
                users.add(os.path.basename(f))
        if not users:
            continue
        # which of those users were deleted by this strip?
        alive = set()
        for u in users:
            if os.path.exists(os.path.join(src_dir, u)):
                alive.add(u)
        if not alive:
            continue
        text = open(hdr, errors='replace', newline='').read()
        non_base = [u for u in alive if not u.startswith('SimdBase')]
        keep = set(isa_syms)
        if non_base:
            keep &= DEAD_ISA_NS
        if not any(re.search(r'(?m)^\s*namespace\s+%s\b' % ns, text) for ns in keep):
            if non_base:
                print('strip.py: NOTE %s still used by %s; its Sse41/Avx2 ISA code stays'
                      % (base, ','.join(sorted(non_base)[:3])))
            continue
        out, i, changed = [], 0, 0
        while i < len(text):
            m = re.compile(r'(?m)^[ \t]*namespace\s+(\w+)\s*\n?[ \t]*\{').search(text, i)
            if not m:
                out.append(text[i:])
                break
            out.append(text[i:m.start()])
            if m.group(1) not in keep:
                out.append(text[m.start():m.end()])
                i = m.end()
                continue
            j, depth = m.end() - 1, 0
            while j < len(text):
                if text[j] == '{':
                    depth += 1
                elif text[j] == '}':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            i = j + 1
            while i < len(text) and text[i] in '\r\n':
                i += 1
            changed += 1
        if changed:
            open(hdr, 'w', newline='').write(''.join(out))
            if non_base:
                print('strip.py: %s -> removed %d uncompiled-back-end namespace(s); '
                      'Sse41/Avx2 declarations kept (used by %s)'
                      % (base, changed, ','.join(sorted(non_base)[:3])))
            else:
                print('strip.py: %s -> removed %d ISA namespace(s)' % (base, changed))

    # --- per-ISA autotest hooks ----------------------------------------------
    hooks = 0
    for path in sorted(glob.glob(os.path.join(test_dir, '*.cpp'))):
        text = open(path, errors='replace', newline='').read()
        lines = text.split('\n')
        drop = set()
        for start, end, block in ifdef_blocks(text):
            m = re.match(r'#\s*ifdef\s+SIMD_(\w+)_ENABLE', block.split('\n')[0].strip())
            if m and m.group(1) in ISA_NS and calls_stripped(block, ISA_NS[m.group(1)]):
                drop |= set(range(start, end + 1))
        if drop:
            open(path, 'w', newline='').write('\n'.join(l for i, l in enumerate(lines) if i not in drop))
            hooks += 1
    print('strip.py: %s -> removed %d files, %d test files rewritten'
          % (','.join(names), removed, hooks))


if __name__ == '__main__':
    main()
