#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
strip.py -- remove selected SIMD kernel families from a BitMagic source tree.

Usage:
    strip.py <tree> <family> [<family> ...]
    strip.py <tree> all

The tree is modified in place; running the tool twice with the same family
list is a no-op (idempotent).

Work done per family:
  1. every ``#define VECT_<NAME> ...`` of the family is deleted from all five
     backend headers (bmavx2.h, bmsse4.h, bmsse2.h, bmsse_util.h,
     bmavx512.h),
  2. the body of every ``<arch>_<kernel>`` function of the family is deleted
     from those headers, together with every private helper (function or
     object macro defined in a backend header) that nothing surviving still
     refers to,
  3. dispatch sites fall back to the portable C++ branch: every preprocessor
     conditional whose guarded region still mentions a removed name is turned
     into ``#if 0`` (``#elif 0``), so the ``#else`` / next branch is taken.

Conditionals that cannot be handled implicitly are reported on stdout as
NOTE: lines and left untouched.
"""

import os
import re
import sys

# --------------------------------------------------------------------------
# family definitions
# --------------------------------------------------------------------------

FAMILIES = {
    'bool_algebra': {
        'kernels': """and_block or_block xor_block sub_block and_digest
            and_digest_2way and_digest_3way and_digest_5way or_block_2way
            or_block_3way or_block_5way sub_digest sub_digest_2way
            sub_digest_3way sub_digest_5way and_or_digest_2way and_arr_unal
            or_arr_unal xor_arr_unal andnot_arr_2_mask xor_arr_2_mask
            block_set_digest is_digest_zero invert_block""",
        'vect': """VECT_AND_BLOCK VECT_AND_DIGEST VECT_AND_DIGEST_2WAY
            VECT_AND_DIGEST_3WAY VECT_AND_DIGEST_5WAY VECT_AND_OR_DIGEST_2WAY
            VECT_OR_BLOCK VECT_OR_BLOCK_2WAY VECT_OR_BLOCK_3WAY
            VECT_OR_BLOCK_5WAY VECT_SUB_BLOCK VECT_SUB_DIGEST
            VECT_SUB_DIGEST_2WAY VECT_SUB_DIGEST_3WAY VECT_SUB_DIGEST_5WAY
            VECT_XOR_BLOCK VECT_XOR_BLOCK_2WAY VECT_XOR_ARR_2_MASK
            VECT_ANDNOT_ARR_2_MASK VECT_BLOCK_SET_DIGEST VECT_IS_DIGEST_ZERO
            VECT_INVERT_BLOCK""",
    },
    'gap_ops': {
        'kernels': 'gap_bfind gap_find gap_test gap_sum_arr',
        'vect': 'VECT_GAP_BFIND VECT_GAP_TEST',
    },
    'bit_to_gap': {
        'kernels': 'bit_to_gap',
        'vect': 'VECT_BIT_TO_GAP',
    },
    'shift': {
        'kernels': 'shift_l1 shift_r1 shift_r1_and i32_shift',
        'vect': 'VECT_SHIFT_L1 VECT_SHIFT_R1 VECT_SHIFT_R1_AND',
    },
    'xor_similarity': {
        'kernels': 'bit_block_xor bit_block_xor_2way',
        'vect': 'VECT_BIT_BLOCK_XOR VECT_BIT_BLOCK_XOR_2WAY',
    },
    'change_count': {
        'kernels': """bit_block_calc_change bit_block_calc_change_bc
            bit_block_calc_count_change bit_block_calc_xor_change""",
        'vect': 'VECT_BLOCK_CHANGE VECT_BLOCK_CHANGE_BC VECT_BLOCK_XOR_CHANGE',
    },
    'gather': {
        'kernels': 'bit_block_gather_scatter idx_arr_block_lookup',
        'vect': 'VECT_ARR_BLOCK_LOOKUP',
    },
    'block_construct': {
        'kernels': """set_block set_block_bits set_block_bits2 set_block_bits3
            setbit_256 setbit_to256 copy_block copy_block_unalign stream_block
            stream_block_unalign is_all_zero is_all_one test_all_zero_wave
            test_all_one_wave test_all_zero_wave2 test_all_eq_wave2""",
        'vect': """VECT_SET_BLOCK VECT_SET_BLOCK_BITS VECT_COPY_BLOCK
            VECT_COPY_BLOCK_UNALIGN VECT_STREAM_BLOCK
            VECT_STREAM_BLOCK_UNALIGN VECT_IS_ZERO_BLOCK VECT_IS_ONE_BLOCK""",
    },
    'popcount': {
        'kernels': """bit_count bit_block_count bit_count_and bit_count_or
            bit_count_xor bit_count_sub bit_count_digest bit_count_op""",
        'vect': """VECT_BITCOUNT VECT_BITCOUNT_AND VECT_BITCOUNT_OR
            VECT_BITCOUNT_XOR VECT_BITCOUNT_SUB VECT_BIT_COUNT_DIGEST""",
    },
    'search': {
        'kernels': """bit_find_first bit_find_first_diff lower_bound_scan_u32
            cmpge_u16 cmpge_u32""",
        'vect': """VECT_BIT_FIND_FIRST VECT_BIT_FIND_DIFF
            VECT_LOWER_BOUND_SCAN_U32""",
    },
}

FAMILY_ORDER = ['bool_algebra', 'gap_ops', 'bit_to_gap', 'shift',
                'xor_similarity', 'change_count', 'gather',
                'block_construct', 'popcount', 'search']

BACKEND_HEADERS = ['bmavx2.h', 'bmsse4.h', 'bmsse2.h', 'bmsse_util.h',
                   'bmavx512.h']

ARCH_PREFIX = re.compile(r'^(?:avx2|avx512|sse2|sse4|sse42)_')

# macros that select a SIMD back end; a conditional testing one of them is a
# dispatch site
SIMD_OPT_MACROS = {'BMAVX2OPT', 'BMAVX512OPT', 'BMSSE2OPT', 'BMSSE42OPT',
                   'BMNEONOPT', 'BMWASMSIMDOPT', 'BMVECTOPT', 'BM64OPT',
                   'BM64_AVX2', 'BM64_AVX512', 'BM64_SSE4'}

SOURCE_EXT = ('.h', '.hpp', '.hh', '.ht', '.c', '.cc', '.cpp', '.cxx',
              '.inl', '.ipp', '.tcc')


# --------------------------------------------------------------------------
# low level text utilities
# --------------------------------------------------------------------------

def mask_comments(text):
    """Blank out comments and string/char literal bodies.  The length and all
    line breaks are preserved, so line numbering is unchanged."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                out[i] = ' '
                i += 1
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            out[i] = out[i + 1] = ' '
            i += 2
            while i < n and not (text[i] == '*' and i + 1 < n and
                                 text[i + 1] == '/'):
                if text[i] != '\n':
                    out[i] = ' '
                i += 1
            if i < n:
                out[i] = out[i + 1] = ' '
                i += 2
        elif c in '"\'':
            quote = c
            out[i] = ' '
            i += 1
            while i < n and text[i] != quote:
                if text[i] == '\\' and i + 1 < n:
                    out[i] = out[i + 1] = ' '
                    i += 2
                    continue
                if text[i] != '\n':
                    out[i] = ' '
                i += 1
            if i < n:
                out[i] = ' '
                i += 1
        else:
            i += 1
    return ''.join(out)


def code_view(text):
    return mask_comments(text).split('\n')


# --------------------------------------------------------------------------
# preprocessor structure
# --------------------------------------------------------------------------

_CPP_DIRECTIVE = re.compile(r'^[ \t]*#[ \t]*(\w+)(.*)$')


def iter_directives(lines):
    for i, ln in enumerate(lines):
        m = _CPP_DIRECTIVE.match(ln)
        if m:
            yield i, m.group(1), m.group(2)


def cpp_blocks(lines):
    """Conditional blocks with their branches.

    block  = {'start':, 'end':, 'branches': [...]}
    branch = {'directive_line':, 'kw': 'if'|'else'|'elif', 'cond': str|None,
              'lo':, 'hi':}
    """
    stack, blocks = [], []
    for i, kw, rest in iter_directives(lines):
        if kw in ('if', 'ifdef', 'ifndef'):
            if kw == 'ifdef':
                cond = 'defined(' + rest.strip() + ')'
            elif kw == 'ifndef':
                cond = '!defined(' + rest.strip() + ')'
            else:
                cond = rest.strip()
            blk = {'start': i, 'end': len(lines) - 1,
                   'branches': [{'directive_line': i, 'kw': 'if', 'cond': cond,
                                 'lo': i + 1, 'hi': len(lines) - 1}]}
            stack.append(blk)
            blocks.append(blk)
        elif kw in ('else', 'elif'):
            if not stack:
                continue
            blk = stack[-1]
            blk['branches'][-1]['hi'] = i - 1
            blk['branches'].append({
                'directive_line': i, 'kw': kw,
                'cond': rest.strip() if kw == 'elif' else None,
                'lo': i + 1, 'hi': len(lines) - 1})
        elif kw == 'endif':
            if not stack:
                continue
            blk = stack.pop()
            blk['branches'][-1]['hi'] = i - 1
            blk['end'] = i
    return blocks


# --------------------------------------------------------------- 3-valued bool

class _BoolParser:
    """Tiny three valued evaluator for preprocessor #if expressions.

    Returns True / False / None (unknown).  Anything that cannot be parsed
    yields None.
    """

    def __init__(self, tokens, removed):
        self.t = tokens
        self.i = 0
        self.removed = removed

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else None

    def next(self):
        tok = self.t[self.i]
        self.i += 1
        return tok

    def parse(self):
        try:
            v = self.or_expr()
        except ValueError:
            return None
        if self.i != len(self.t):
            return None
        return v

    def or_expr(self):
        v = self.and_expr()
        while self.peek() == '||':
            self.next()
            r = self.and_expr()
            if v is True or r is True:
                v = True
            elif v is False and r is False:
                v = False
            else:
                v = None
        return v

    def and_expr(self):
        v = self.unary()
        while self.peek() == '&&':
            self.next()
            r = self.unary()
            if v is False or r is False:
                v = False
            elif v is True and r is True:
                v = True
            else:
                v = None
        return v

    def unary(self):
        if self.peek() == '!':
            self.next()
            v = self.unary()
            if v is None:
                return None
            return not v
        return self.primary()

    def primary(self):
        tok = self.peek()
        if tok is None:
            raise ValueError
        if tok == '(':
            self.next()
            v = self.or_expr()
            if self.peek() != ')':
                raise ValueError
            self.next()
            return v
        if tok == 'defined':
            self.next()
            if self.peek() == '(':
                self.next()
                name = self.next()
                if self.peek() != ')':
                    raise ValueError
                self.next()
            else:
                name = self.next()
            return False if name in self.removed else None
        self.next()
        if re.fullmatch(r'\d+', tok):
            return int(tok) != 0
        if re.fullmatch(r'[A-Za-z_]\w*', tok):
            return False if tok in self.removed else None
        raise ValueError


_TOKEN_RE = re.compile(r'\s*(defined|&&|\|\||[!()]|[A-Za-z_]\w*|\d+)')


def cond_value(cond, removed):
    """True/False/None for a preprocessor condition after the strip."""
    if cond is None:
        return None
    toks = []
    pos = 0
    while pos < len(cond):
        m = _TOKEN_RE.match(cond, pos)
        if not m:
            if cond[pos:].strip() == '':
                break
            return None
        toks.append(m.group(1))
        pos = m.end()
    if not toks:
        return None
    return _BoolParser(toks, removed).parse()


def dead_lines(lines, removed):
    """Boolean per line: inside a branch that is definitely not compiled."""
    dead = [False] * (len(lines) + 1)
    for blk in cpp_blocks(lines):
        state = None
        for br in blk['branches']:
            if br['kw'] in ('if', 'elif'):
                state = cond_value(br['cond'], removed)
            else:
                state = (None if state is None else (not state))
            if state is False:
                for k in range(max(0, br['lo']), min(len(lines), br['hi'] + 1)):
                    dead[k] = True
    return dead


# --------------------------------------------------------------------------
# function / macro discovery
# --------------------------------------------------------------------------

_QUALIFIER_ONLY = re.compile(
    r'^\s*(?:inline|BMFORCEINLINE|BM_NOINLINE|static)\s*$')
_TEMPLATE_ONLY = re.compile(r'^\s*template\s*<.*>\s*$')
_FUNC_NAME = re.compile(
    r'(?<![\w])((?:avx2|avx512|sse2|sse4|sse42)_[A-Za-z0-9_]+)\s*\(')
_ANY_DEFINE = re.compile(
    r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)')


def _commentish(raw_line):
    s = raw_line.strip()
    return (s == '' or s.startswith('//') or s.startswith('*')
            or s.startswith('/*') or s.endswith('*/'))


def _find_end(code, start_line, from_col):
    depth, seen_brace = 0, False
    i, col = start_line, from_col
    while i < len(code):
        line = code[i]
        while col < len(line):
            ch = line[col]
            if ch == '{':
                depth += 1
                seen_brace = True
            elif ch == '}':
                depth -= 1
                if seen_brace and depth == 0:
                    return i
            elif ch == ';' and not seen_brace and depth == 0:
                return i
            col += 1
        i += 1
        col = 0
    return None


def find_functions(code):
    """Locate every <arch>_<name>( function definition / declaration.

    ``code`` is the comment-masked view (list of lines).  Returns dicts with
    name / lo / hi (0-based inclusive line range of the whole definition,
    including a leading 'inline' keyword and doc comment)."""
    raw = code
    funcs = []
    in_define = False
    i = 0
    while i < len(code):
        cl = code[i]
        stripped = cl.lstrip()
        if stripped.startswith('#'):
            in_define = stripped.rstrip().endswith('\\')
            i += 1
            continue
        if in_define:
            in_define = cl.rstrip().endswith('\\')
            i += 1
            continue
        if not cl or cl[0] in ' \t':
            i += 1
            continue
        m = _FUNC_NAME.search(cl)
        if not m:
            i += 1
            continue
        pre = cl[:m.start()]
        if re.search(r'[=;(){}\[\],]', pre) or '+' in pre or '-' in pre:
            i += 1
            continue
        end = _find_end(code, i, m.end() - 1)
        if end is None:
            i += 1
            continue
        lo = i
        while lo - 1 >= 0 and (_QUALIFIER_ONLY.match(code[lo - 1]) or
                               _TEMPLATE_ONLY.match(code[lo - 1]) or
                               _commentish(raw[lo - 1])):
            if (code[lo - 1].strip() == '' and lo - 2 >= 0 and
                    not _commentish(raw[lo - 2]) and
                    not _QUALIFIER_ONLY.match(code[lo - 2]) and
                    not _TEMPLATE_ONLY.match(code[lo - 2])):
                break
            lo -= 1
        funcs.append({'name': m.group(1), 'lo': lo, 'hi': end})
        i = end + 1
    return funcs


def find_defines(text):
    """All #define directives with the line range of the complete macro."""
    raw = text.split('\n')
    res = []
    i = 0
    while i < len(raw):
        m = _ANY_DEFINE.match(raw[i])
        if not m:
            i += 1
            continue
        lo = i
        while i < len(raw) and raw[i].rstrip().endswith('\\'):
            i += 1
        res.append({'name': m.group(1), 'lo': lo, 'hi': i})
        i += 1
    return res


def apply_deletions(text, ranges):
    if not ranges:
        return text
    raw = text.split('\n')
    drop = set()
    for lo, hi in ranges:
        for k in range(max(0, lo), min(len(raw) - 1, hi) + 1):
            drop.add(k)
    return '\n'.join(l for k, l in enumerate(raw) if k not in drop)


# --------------------------------------------------------------------------
# identifier scanning
# --------------------------------------------------------------------------

_IDENT = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')


def live_identifiers(lines, dead):
    out = []
    for k, ln in enumerate(lines):
        if ln and not dead[k]:
            out.append(ln)
    return set(_IDENT.findall('\n'.join(out)))


def _is_include_guard(blk):
    """A conditional that wraps a whole header (its condition names the
    ``*__INCLUDED__`` guard symbol)."""
    names = set(_IDENT.findall(
        ' '.join((b['cond'] or '') for b in blk['branches'])))
    return any(n.endswith('__INCLUDED__') for n in names)


# --------------------------------------------------------------------------
# the stripper
# --------------------------------------------------------------------------

class Stripper:
    def __init__(self, tree, families, verbose=True):
        self.tree = os.path.abspath(tree)
        self.src = os.path.join(self.tree, 'src')
        self.families = families
        self.verbose = verbose
        self.notes = []
        self.removed_vect = set()
        self.removed_kernels = set()
        self.removed_functions = set()
        self.removed_helpers = []
        self.rewritten_sites = 0
        for fam in families:
            spec = FAMILIES[fam]
            self.removed_vect |= set(spec['vect'].split())
            self.removed_kernels |= set(spec['kernels'].split())
        for v in self.removed_vect:      # kernels reachable only via VECT name
            self.removed_kernels.add(v[len('VECT_'):].lower())

    def note(self, msg):
        self.notes.append(msg)

    def all_sources(self):
        out = []
        for root, dirs, files in os.walk(self.tree):
            dirs[:] = [d for d in dirs
                       if d not in ('.git', 'build', '.svn', 'CMakeFiles')]
            for f in files:
                if f.endswith(SOURCE_EXT):
                    out.append(os.path.join(root, f))
        return sorted(out)

    # -- shared analysis --------------------------------------------------
    def weak_lines(self, lines, removed):
        """Lines that live inside a conditional branch whose condition tests a
        SIMD option macro (a dispatch site), i.e. code that would go away if
        the SIMD back end is removed."""
        weak = [False] * (len(lines) + 1)
        for blk in cpp_blocks(lines):
            for br in blk['branches']:
                names = set(_IDENT.findall(br['cond'] or ''))
                if not (names & SIMD_OPT_MACROS):
                    continue
                for k in range(max(0, br['lo']),
                               min(len(lines), br['hi'] + 1)):
                    weak[k] = True
        return weak

    def reference_counts(self, removed, strong_only):
        """(occurrences, own-occurrences) for every identifier.

        ``own`` counts the occurrences that sit inside the definition of that
        very name (so a definition is not mistaken for a use).  With
        ``strong_only`` code inside SIMD dispatch regions is ignored."""
        occ, own = {}, {}
        defs = {}
        per_file_lines = {}
        for path, text in self.files.items():
            lines = code_view(text)
            per_file_lines[path] = lines
            dead = dead_lines(lines, removed)
            weak = self.weak_lines(lines, removed)
            # strong mode additionally drops SIMD dispatch regions
            keep = [ln for k, ln in enumerate(lines)
                    if ln and not dead[k] and not (strong_only and weak[k])]
            for n in _IDENT.findall('\n'.join(keep)):
                occ[n] = occ.get(n, 0) + 1
        # definition ranges of everything that can be a helper
        for h in BACKEND_HEADERS:
            p = os.path.join(self.src, h)
            if p not in self.files:
                continue
            lines = per_file_lines[p]
            for fn in find_functions(lines):
                defs.setdefault(fn['name'], []).append((p, fn['lo'], fn['hi']))
            for df in find_defines(self.files[p]):
                defs.setdefault(df['name'], []).append((p, df['lo'], df['hi']))

        def occ_in(name, lo, hi, path):
            lines = per_file_lines[path]
            pat = re.compile(r'(?<![\w])%s(?![\w])' % re.escape(name))
            dead = dead_lines(lines, removed)
            weak = self.weak_lines(lines, removed)
            tot = 0
            for k in range(max(0, lo), min(len(lines), hi + 1)):
                if not dead[k] and not (strong_only and weak[k]):
                    tot += len(pat.findall(lines[k]))
            return tot

        for name, ranges in defs.items():
            own[name] = sum(occ_in(name, lo, hi, p) for (p, lo, hi) in ranges)
        return occ, own, per_file_lines

    def backend_uncond_macros(self):
        """Object/function macros of the 5 back-end headers that are defined
        outside any conditional - the algorithm surface of a back end."""
        out = set()
        for h in BACKEND_HEADERS:
            p = os.path.join(self.src, h)
            if p not in self.files:
                continue
            text = self.files[p]
            lines = code_view(text)
            guarded = set()
            for blk in cpp_blocks(lines):
                if _is_include_guard(blk):
                    continue        # the include guard wraps the whole file
                for br in blk['branches']:
                    for k in range(max(0, br['lo']),
                                   min(len(lines), br['hi'] + 1)):
                        guarded.add(k)
            for df in find_defines(text):
                n = df['name']
                if (n.startswith('_') or n.startswith('VECT_')
                        or n.endswith('__INCLUDED__')):
                    continue
                if df['lo'] in guarded:
                    continue
                out.add(n)
        return out

    def pending_helpers(self, removed):
        """Back-end macros that nothing outside a SIMD dispatch region refers
        to - i.e. helpers that only survive because a dispatch site in a
        compiled header still mentions them."""
        macros = self.backend_uncond_macros()
        if not macros:
            return set()
        occ, own, _ = self.reference_counts(removed, strong_only=True)
        return {n for n in macros if occ.get(n, 0) - own.get(n, 0) <= 0}

    # -- main ------------------------------------------------------------
    def run(self):
        self.files = {}
        for p in self.all_sources():
            with open(p, 'r', encoding='utf-8',
                      errors='surrogateescape') as fh:
                self.files[p] = fh.read()

        for h in BACKEND_HEADERS:
            p = os.path.join(self.src, h)
            if p in self.files:
                self.strip_backend(p)

        for _ in range(20):
            changed = self.rewrite_dispatch_sites()
            helpers = self.remove_dead_helpers()
            if not changed and not helpers:
                break

        for p, t in self.files.items():
            with open(p, 'w', encoding='utf-8',
                      errors='surrogateescape') as fh:
                fh.write(t)

    # -- steps 1+2 : backend headers -------------------------------------
    def strip_backend(self, path):
        text = self.files[path]
        ranges = []
        for df in find_defines(text):
            if df['name'].startswith('VECT_') and df['name'] in self.removed_vect:
                ranges.append((df['lo'], df['hi']))
        for fn in find_functions(code_view(text)):
            suffix = ARCH_PREFIX.sub('', fn['name'])
            if suffix in self.removed_kernels:
                ranges.append((fn['lo'], fn['hi']))
                self.removed_functions.add(fn['name'])
        self.files[path] = apply_deletions(text, ranges)
        if self.verbose:
            print('  %-14s %5d -> %5d lines  (%d blocks deleted)' % (
                os.path.basename(path), text.count('\n') + 1,
                self.files[path].count('\n') + 1, len(ranges)))

    # -- step 3 : dispatch sites -----------------------------------------
    def removed_names(self):
        return (self.removed_vect | self.removed_functions |
                set(self.removed_helpers))

    def rewrite_dispatch_sites(self):
        removed = self.removed_names()
        pending = self.pending_helpers(removed)
        if pending:
            self.notes.append('PENDING-HELPERS %s' % ' '.join(sorted(pending)))
        surviving_vect = set()
        for fam, spec in FAMILIES.items():
            if fam not in self.families:
                surviving_vect |= set(spec['vect'].split())
        changed = False
        for path in list(self.files.keys()):
            text = self.files[path]
            lines = code_view(text)
            raw = text.split('\n')
            dead = dead_lines(lines, removed)
            weak = self.weak_lines(lines, removed)
            blocks = cpp_blocks(lines)
            rewrites = {}
            for blk in blocks:
                if _is_include_guard(blk):
                    continue                       # include guard, ignore
                fams = set()
                for br in blk['branches']:
                    names = set()
                    for k in range(max(0, br['lo']),
                                   min(len(lines), br['hi'] + 1)):
                        if not dead[k]:
                            names.update(_IDENT.findall(lines[k]))
                    hit = names & removed
                    cond_names = set(_IDENT.findall(br['cond'] or ''))
                    if (not hit) and (cond_names & SIMD_OPT_MACROS):
                        # a SIMD dispatch site that only mentions a back-end
                        # macro which would be an orphan without it
                        hit = names & pending
                    if not hit:
                        continue
                    for n in hit:
                        fams |= self.name_families(n)
                    cnames = set(_IDENT.findall(br['cond'] or ''))
                    if (cnames & self.removed_vect) and \
                       (cnames & surviving_vect):
                        self.note('MIXED-CONDITION %s:%d  %s' % (
                            os.path.relpath(path, self.tree),
                            br['directive_line'] + 1,
                            (br['cond'] or '').strip()))
                    if br['kw'] == 'else':
                        self.note('ELSE-BRANCH-USES-REMOVED %s:%d' % (
                            os.path.relpath(path, self.tree),
                            br['directive_line'] + 1))
                        continue
                    if cond_value(br['cond'], removed) is True:
                        self.note('COND-STAYS-TRUE %s:%d  %s' % (
                            os.path.relpath(path, self.tree),
                            br['directive_line'] + 1,
                            (br['cond'] or '').strip()))
                    old = raw[br['directive_line']]
                    if re.match(r'^\s*#\s*(if|elif)\s+0\s*$', old):
                        continue                   # already neutralised
                    indent = re.match(r'^[ \t]*', old).group(0)
                    rewrites[br['directive_line']] = \
                        '%s#%s 0' % (indent, br['kw'])
                    changed = True
                if len(fams) > 1 and self.verbose:
                    self.note('MULTI-FAMILY-REGION %s:%d families=%s' % (
                        os.path.relpath(path, self.tree),
                        blk['start'] + 1, ','.join(sorted(fams))))
            if rewrites:
                self.rewritten_sites += len(rewrites)
                for idx, nl in rewrites.items():
                    raw[idx] = nl
                self.files[path] = '\n'.join(raw)
        return changed

    def name_families(self, name):
        fams = set()
        suffix = ARCH_PREFIX.sub('', name)
        for fam, spec in FAMILIES.items():
            if name in spec['vect'].split():
                fams.add(fam)
                continue
            if suffix in spec['kernels'].split():
                fams.add(fam)
                continue
            for v in spec['vect'].split():
                if suffix == v[len('VECT_'):].lower():
                    fams.add(fam)
        return fams

    # -- orphan helper cleanup -------------------------------------------
    def remove_dead_helpers(self):
        removed = self.removed_names()
        # NOTE: strong_only must be False here.  A helper whose *only* caller
        # sits in a SIMD dispatch region is dead only once that region has
        # actually been neutralised (which the outer loop does first, and the
        # region then counts as dead).  Counting weak lines as live keeps the
        # kernels of the families we are *not* stripping.
        occ, own, per_file_lines = self.reference_counts(removed,
                                                         strong_only=False)

        def referenced(name):
            if name.startswith('_'):     # compiler intrinsic / shim, leave it
                return True
            return occ.get(name, 0) - own.get(name, 0) > 0

        dropped = []
        for h in BACKEND_HEADERS:
            p = os.path.join(self.src, h)
            if p not in self.files:
                continue
            text = self.files[p]
            lines = per_file_lines[p]
            ranges = []
            for fn in find_functions(lines):
                if fn['name'] in self.removed_functions:
                    continue
                if referenced(fn['name']):
                    continue
                ranges.append((fn['lo'], fn['hi']))
                dropped.append(fn['name'])
            for df in find_defines(text):
                name = df['name']
                if name.startswith('VECT_') or name.endswith('__INCLUDED__'):
                    continue
                if referenced(name):
                    continue
                ranges.append((df['lo'], df['hi']))
                dropped.append('#define ' + name)
            if ranges:
                self.files[p] = apply_deletions(text, ranges)
        if dropped:
            self.removed_helpers.extend(dropped)
        return bool(dropped)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main(argv):
    if len(argv) < 3:
        sys.stderr.write('usage: strip.py <tree> <family> [<family> ...]\n'
                         'families: %s\n' % ' '.join(FAMILY_ORDER))
        return 2
    tree, families = argv[1], argv[2:]
    if families == ['all']:
        families = list(FAMILY_ORDER)
    bad = [f for f in families if f not in FAMILIES]
    if bad:
        sys.stderr.write('unknown family: %s\nknown: %s\n' %
                         (', '.join(bad), ' '.join(FAMILY_ORDER)))
        return 2
    if not os.path.isdir(os.path.join(tree, 'src')):
        sys.stderr.write('%s: no src/ directory - not a BitMagic tree\n' % tree)
        return 2
    st = Stripper(tree, sorted(set(families)))
    print('strip.py %s : %s' % (tree, ' '.join(st.families)))
    st.run()
    print('  VECT macros deleted : %d' % len(st.removed_vect))
    print('  kernels deleted     : %d' % len(st.removed_functions))
    print('  dispatch sites set to #if 0 : %d' % st.rewritten_sites)
    if st.removed_helpers:
        uniq = sorted(set(st.removed_helpers))
        print('  orphan helpers deleted : %d  %s' %
              (len(uniq), ' '.join(uniq)))
    for n in sorted(set(st.notes)):
        print('  NOTE: ' + n)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
