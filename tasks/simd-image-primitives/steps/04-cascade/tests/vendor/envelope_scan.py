#!/usr/bin/env python3
"""ISA-envelope check, scoped to what the candidate changed in ONE step.

    envelope_scan.py <base-tree> <cand-tree> <scope-file> <envelope-file> <build-dir>

Why this exists
---------------
The instruction states the capability envelope: this family is scored on
SSE4.1 through AVX2, the grading host provides no AVX-512, and an AVX-512 body
is never selected and never timed.  A stated constraint with no check is not a
constraint, so the verifier holds the candidate to it.

Scope, and why it is not the whole tree
---------------------------------------
The workspace is cumulative -- at step k the candidate's tree carries its own
implementations of steps 1..k-1 -- and the reference itself ships AVX-512 back
ends that this build does not compile.  A check that reads the tree wholesale
therefore fails a step for work the step does not own: either a later backend
the build ignores, or another family's code.  So only what THIS step owns is
inspected: the family's file-name patterns (`<scope-file>`) plus
`src/Simd/SimdLib.cpp`, the dispatch file whose branches are where a family's
work is wired in -- and, within those, only the lines the candidate added or
changed relative to the base tree it is given (`<base-tree>`).

That base is the pinned release with all eight families removed (the ladder's
L0): it is the only base level the verifier container carries, so at step k it
is not L(k-1).  In the files this step shares with earlier steps -- SimdLib.cpp
is in every step's scope -- the scanned lines therefore also include the
candidate's own earlier steps' lines.  That cannot fail a tree that passed
those steps: a line that is out of envelope there would already have failed
the step that wrote it, under the same ceiling and the same file list.

An unchanged file is reported as unchanged and not scanned.  That is the point:
a tree that changed nothing must not be failed for code its candidate never
wrote, and a reader must be able to see that the gate examined something and
why it passed.

What a violation is
-------------------
An added or changed line, in a file this step owns that the graded build
compiles, carrying AVX-512 or AMX constructs: intrinsics, vector/mask types,
register names, target pragmas or wider compiler flags.  A file whose name
carries a back-end prefix the build does not compile (`SimdAvx512bw*`,
`SimdAmxBf16*`, `SimdNeon*`, ...) is dead code on this host, exactly like the
reference's own AVX-512 files, and is reported as ignored -- and the gate
asserts that claim against the build itself rather than trusting it.

Exit status: 0 clean (or nothing in scope to check), 1 on a violation or a
broken premise, 2 on a malformed invocation.
"""
import difflib
import os
import re
import sys

# Constructs that require more than SSE4.1/AVX2.  Case-sensitive on purpose:
# the library's own enable macros spell the ISA in capitals (`SIMD_AVX512BW`
# guards in SimdLib.cpp and in the pinned headers must NOT trip this), while
# intrinsics, register names, target pragmas and compiler flags are lower case.
#
# The masked-intrinsic branch is anchored on the `_mm` prefix and a token
# boundary, not on the bare substring `_mask_`.  An unanchored `_mask_` flags
# AVX2's own masked gathers (`_mm256_mask_i32gather_ps`, an in-envelope
# intrinsic this host executes) and any ordinary identifier such as
# `block_mask_lo`, while missing AVX-512's 128/256-bit gathers, which are
# spelled `_mm*_mmask_i*gather` and get their own branch.  512-bit gathers are
# caught by `_mm512_` whatever their suffix.  Likewise AMX's intrinsics all
# begin with `_tile_` at a token start, so the boundary keeps every one of them
# (and its `-mamx-*` flag and tile type) while an in-envelope identifier such
# as `dst_tile_base` reads clean.
_WIDE = re.compile(
    r'avx512'                       # -mavx512*, target("avx512*")
    r'|_mm512_|__m512'              # AVX-512 intrinsics and vector types
    r'|__mmask'                     # AVX-512 mask types
    r'|(?<![A-Za-z0-9_])_mm(?:256|512)?_maskz?_(?!i(?:32|64)gather)'
    r'|(?<![A-Za-z0-9_])_mm(?:256|512)?_mmask_'   # AVX-512 masked gather
    r'|\bzmm[0-9]*\b'               # 512-bit register names
    r'|(?<![A-Za-z0-9_])_tile_|__tile|tmm[0-9]'   # AMX tile registers
    r'|mamx\b'                      # -mamx-* flags
    r'|march=native'                # wider-than-the-group flags
    r'|prefer-vector-width=512')    # 512 is wider than the 256-bit ceiling

_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
_LINE_COMMENT = re.compile(r'//[^\n]*')


def read_text(path):
    with open(path, 'rb') as fh:
        raw = fh.read()
    try:
        return raw, raw.decode('utf-8')
    except UnicodeDecodeError:
        return raw, raw.decode('latin-1')


def strip_comments(text):
    """Blank out comments while keeping the line structure intact.

    A comment mentioning AVX-512 is prose, not code, and must not trip the
    scan.  Newlines inside a removed comment are preserved so line numbers
    still refer to the file as written.
    """
    text = _BLOCK_COMMENT.sub(
        lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text)
    return _LINE_COMMENT.sub(' ', text)


def added_lines(base_path, cand_text):
    """(lineno, text) for every line the candidate added or changed.

    A new file contributes every line.  For an existing file the comparison is
    a real alignment against the base tree's copy (difflib, not a line
    multiset: a multiset would report every line of a large file as "added"
    when the file's lines merely move, which would charge pre-existing
    out-of-envelope code to a candidate who edited an unrelated part of a
    shared header).  Comments are blanked on both sides first -- the new-file
    case included, so a file the candidate wrote from scratch is held to the
    same rule and a comment that mentions AVX-512 is prose, not a violation.
    """
    if not os.path.exists(base_path):
        return [(i + 1, l.rstrip())
                for i, l in enumerate(strip_comments(cand_text).split('\n'))]
    _, base_text = read_text(base_path)
    # Trailing whitespace (including a CR: the tree mixes CRLF and LF, and an
    # editor that normalises one file's line endings has not changed a line)
    # is ignored on both sides, so re-indentation or a line-ending change alone
    # cannot mark a whole file as added.
    base = [l.rstrip() for l in strip_comments(base_text).split('\n')]
    cand = [l.rstrip() for l in strip_comments(cand_text).split('\n')]
    matcher = difflib.SequenceMatcher(None, base, cand, autojunk=False)
    out = []
    for tag, _i1, _i2, j1, j2 in matcher.get_opcodes():
        if tag in ('insert', 'replace'):
            out.extend((j + 1, cand[j]) for j in range(j1, j2))
    return out


def data_lines(path):
    out = []
    with open(path, errors='replace') as fh:
        for line in fh:
            line = line.split('#', 1)[0].strip()
            if line:
                out.append(line)
    return out


def is_uncompiled(name, uncompiled):
    for prefix in uncompiled:
        if name.startswith(prefix):
            return prefix
    return None


def compiled_sources(build_dir):
    """The src/Simd sources the graded build compiled, from build.ninja."""
    ninja = os.path.join(build_dir, 'build.ninja')
    if not os.path.exists(ninja):
        return None
    seen = set()
    with open(ninja, errors='replace') as fh:
        for line in fh:
            for m in re.finditer(r'src/Simd/([A-Za-z0-9_.+-]+\.cpp)', line):
                seen.add(m.group(1))
    return seen


def main():
    if len(sys.argv) != 6:
        sys.exit('usage: envelope_scan.py <base> <cand> <scope-file> '
                 '<envelope-file> <build-dir>')
    base, cand, scope_file, env_file, build_dir = sys.argv[1:]
    for path in (scope_file, env_file):
        if not os.path.exists(path):
            sys.exit('envelope_scan.py: missing data file %s' % path)

    ceiling, uncompiled = 'sse41,avx2', []
    for line in data_lines(env_file):
        key, _, val = line.partition('=')
        key, val = key.strip(), val.strip()
        if key == 'ceiling':
            ceiling = val
        elif key == 'uncompiled':
            uncompiled = val.split()
    patterns = data_lines(scope_file)
    label = 'SSE4.1/AVX2' if ceiling == 'sse41,avx2' else ceiling

    # The gate asserts its own premise instead of assuming it: the data file
    # claims certain back ends are not compiled, and the graded build is asked
    # whether that is true.  If a build ever compiled one of them, every
    # "ignored" line below would be a lie, so it is a violation.
    compiled = compiled_sources(build_dir)
    if compiled is None:
        print('  envelope: no build.ninja under %s; the compiled-source '
              'assertion is skipped' % build_dir)
    else:
        leaks = sorted(n for n in compiled if is_uncompiled(n, uncompiled))
        if leaks:
            print('  envelope: GATE PREMISE BROKEN — the graded build compiled '
                  'back ends the envelope data says it does not: %s'
                  % ', '.join(leaks[:5]))
            return 1
        print('  envelope: the graded build lists %d src/Simd source file(s), '
              'none from the uncompiled back ends (%s)'
              % (len(compiled), ' '.join(uncompiled)))

    src = os.path.join(cand, 'src', 'Simd')
    if not os.path.isdir(src):
        print('  envelope: %s is not a directory' % src)
        return 2

    scoped, dead = [], []
    for pat in patterns:
        rx = re.compile('^' + re.escape(pat).replace(r'\*', '.*') + '$')
        for name in sorted(os.listdir(src)):
            if not os.path.isfile(os.path.join(src, name)) or not rx.match(name):
                continue
            prefix = is_uncompiled(name, uncompiled)
            if prefix:
                dead.append(name)
            else:
                scoped.append(name)

    # The dispatch file is where a family's branches live, so this step owns it
    # too.  Like everything else it is scanned only for what changed, and its
    # AVX-512 guards (`#ifdef SIMD_AVX512BW_ENABLE`) are capitals, not tokens.
    if 'SimdLib.cpp' in os.listdir(src) and 'SimdLib.cpp' not in scoped \
       and not is_uncompiled('SimdLib.cpp', uncompiled):
        scoped.append('SimdLib.cpp')

    if not scoped:
        print('  envelope: no compilable file of this step is present in the '
              'candidate tree — envelope check skipped')
        return 0

    changed, unchanged = [], []
    for name in sorted(scoped):
        cand_raw, cand_text = read_text(os.path.join(src, name))
        base_path = os.path.join(base, 'src', 'Simd', name)
        if os.path.exists(base_path) and read_text(base_path)[0] == cand_raw:
            unchanged.append(name)
        else:
            changed.append((name, base_path, cand_text))

    line = '  envelope: %d of %d scoped files unchanged by the candidate' \
        % (len(unchanged), len(scoped))
    if dead:
        line += ' (%d uncompiled back-end file(s) ignored: %s)' \
            % (len(dead), ', '.join(dead[:4]))
    print(line)
    if not changed:
        print('  envelope: every file this step owns is unchanged — envelope '
              'check skipped, ceiling %s' % label)
        return 0

    print('  envelope: %d changed file(s) checked against the %s ceiling'
          % (len(changed), label))
    total_added, violations = 0, 0
    for name, base_path, cand_text in changed:
        for lineno, line in added_lines(base_path, cand_text):
            total_added += 1
            if _WIDE.search(line):
                print('  envelope: OUT OF ENVELOPE %s:%d: %s'
                      % (name, lineno, line.strip()[:120]))
                violations += 1
    if violations:
        print('  envelope: %d out-of-envelope construct(s) in this step\'s own '
              'files; the envelope is %s' % (violations, label))
        return 1
    print('  envelope: clean — %d added line(s) across %d changed file(s) '
          'carry nothing wider than %s' % (total_added, len(changed), label))
    return 0


if __name__ == '__main__':
    sys.exit(main())
