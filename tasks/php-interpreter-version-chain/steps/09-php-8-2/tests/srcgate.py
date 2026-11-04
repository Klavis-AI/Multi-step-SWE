#!/usr/bin/env python3
"""Refuse a submission that contains php-src source files, verbatim or edited.

Two checks.  Both fail the step; the rule they enforce is stated in the step
instruction ("the interpreter must be your own C code").

1. Verbatim files.  Every C source or header, and every other text file whatever its
   name (`cc -x c` compiles any file), under the candidate tree is hashed and
   compared against the SHA-256 sums of php-src's own sources at the release tags this
   task draws its corpus from (`php-src-hashes.txt`, first 16 hex characters -- 64 bits,
   ample to identify a file).  A single match fails the step.  Files under 256 bytes
   are not compared: the list includes php-src's empty files, so an empty header of
   the candidate's own would otherwise fail the step.

2. Edited copies.  An exact hash is defeated by any one-byte edit, so php-src with a
   comment appended to every file, or with its identifiers renamed, would pass check 1.
   Check 2 compares token SHAPE instead of bytes: comments and layout are dropped,
   every identifier becomes `I`, every literal `N`/`S`/`C`, runs of table data collapse
   to one token, and each 40-token window is hashed.  A fixed 1-in-8 sample of those
   hashes is compared with the same sample taken from php-src's engine and runtime
   (`Zend/`, `main/`, `sapi/cli/` at the eleven tags, minus the third-party code php-src
   bundles there and the files it generates), shipped in `php-src-fingerprints.bin`.
   Measured: the reference interpreter shares 22-23 of its
   ~25,000 sampled windows with php-src (0.1%, at most 12 in one file); php-src's own
   engine with every identifier renamed and a comment on every line shares 95-99.7%.
   The thresholds sit between the two with a wide margin on either side.

`srcgate.py --build-db <php-src git dir> <out>` re-records the fingerprint file from a
php-src clone; it is the recorder, and the only way that file should change.
"""
import hashlib
import os
import re
import stat
import struct
import subprocess
import sys

EXTS = (".c", ".h", ".y", ".l", ".re", ".inc", ".cpp", ".cc", ".hpp")
VERBATIM_MIN = 256     # bytes; see verbatim_check
READ_CAP = 64 * 1024 * 1024


def read_regular(path, limit=READ_CAP):
    """Up to `limit` bytes of a regular file, opened without following a symlink and
    without blocking: this runs as root over a tree the candidate built, so a FIFO or a
    device under a source name must not hang it, and a link must not lead it out of the
    tree.  None if `path` is not a readable regular file."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError:
        return None
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            return None
        chunks, size = [], 0
        while size < limit:
            b = os.read(fd, min(1 << 20, limit - size))
            if not b:
                break
            chunks.append(b)
            size += len(b)
        return b"".join(chunks)
    except OSError:
        return None
    finally:
        os.close(fd)


def candidate_source(path, name):
    """The bytes of `path` if it can hold C source: any C-family file, and any other
    regular file that is text.  The extension is not what makes a file source -- `cc -x c
    notes.txt` compiles it -- so php-src carried under another name is compared too.  A
    file with a NUL byte in its first 8 KiB is binary and is left to the payload gate."""
    data = read_regular(path)
    if data is None:
        return None
    if not name.endswith(EXTS) and b"\0" in data[:8192]:
        return None
    return data

# ------------------------------------------------------------------ token shape
K = 40                 # tokens per window
P = 8                  # keep windows whose hash is 0 mod P
MIN_DISTINCT = 8       # windows this repetitive say nothing about provenance
GLOBAL_LIMIT = 200     # sampled windows shared with php-src, whole tree
FILE_LIMIT = 40        # ... in one file, when they are also
FILE_RATIO = 0.3       # this share of that file's sampled windows

KEYWORDS = frozenset(b"""
auto break case char const continue default do double else enum extern float for goto if
inline int long register restrict return short signed sizeof static struct switch typedef
union unsigned void volatile while _Bool _Complex _Imaginary _Alignas _Alignof _Atomic
_Generic _Noreturn _Static_assert _Thread_local
alignas alignof and and_eq asm bitand bitor bool catch char8_t char16_t char32_t class
compl concept consteval constexpr constinit const_cast co_await co_return co_yield
decltype delete dynamic_cast explicit export false friend mutable namespace new noexcept
not not_eq nullptr operator or or_eq private protected public reinterpret_cast requires
static_assert static_cast template this thread_local throw true try typeid typename using
virtual wchar_t xor xor_eq
define include ifdef ifndef endif elif undef pragma error line defined
""".split())

TOKEN_RE = re.compile(rb"""
  (?P<ws>\s+)
| (?P<lc>//[^\n]*)
| (?P<bc>/\*.*?(?:\*/|\Z))
| (?P<str>"(?:\\.|[^"\\\n])*"?)
| (?P<chr>'(?:\\.|[^'\\\n])*'?)
| (?P<num>\.?[0-9](?:[eEpP][+-]|[0-9A-Za-z_.'])*)
| (?P<id>[A-Za-z_$][A-Za-z0-9_$]*)
| (?P<op>->\*?|\+\+|--|<<=|>>=|<=>|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&^|]=|::|\.\.\.|\#\#|.)
""", re.S | re.X)

_LIT = (b"N", b"S", b"C")
_TABLE = frozenset(_LIT + (b",", b"{", b"}", b"-", b"+", b"(", b")"))
_M64 = (1 << 64) - 1
_BASE = 0x100000001B3
_TOKVAL = {}


def shape(data):
    """The token stream with names, literals, comments and layout abstracted away."""
    out = []
    for m in TOKEN_RE.finditer(data):
        kind = m.lastgroup
        if kind in ("ws", "lc", "bc"):
            continue
        if kind == "str":
            if out and out[-1] == b"S":       # adjacent literals concatenate
                continue
            out.append(b"S")
        elif kind == "chr":
            out.append(b"C")
        elif kind == "num":
            out.append(b"N")
        elif kind == "id":
            tok = m.group()
            out.append(tok if tok in KEYWORDS else b"I")
        else:
            out.append(m.group())
    res = []
    i, n = 0, len(out)
    while i < n:
        if out[i] in _TABLE:
            j, lits = i, 0
            while j < n and out[j] in _TABLE:
                if out[j] in _LIT:
                    lits += 1
                j += 1
            if lits >= 4 and j - i >= 8:      # a data table: one token for the run
                res.append(b"T")
                i = j
                continue
        res.append(out[i])
        i += 1
    return res


def _tokval(tok):
    v = _TOKVAL.get(tok)
    if v is None:
        v = struct.unpack("<Q", hashlib.blake2b(tok, digest_size=8).digest())[0] | 1
        _TOKVAL[tok] = v
    return v


def _mix(x):
    x ^= x >> 30
    x = (x * 0xBF58476D1CE4E5B9) & _M64
    x ^= x >> 27
    x = (x * 0x94D049BB133111EB) & _M64
    return x ^ (x >> 31)


def fingerprints(data):
    """The sampled window hashes of one file (a rolling hash over the shape)."""
    toks = shape(data)
    fps = set()
    n = len(toks)
    if n < K:
        return fps
    vals = [_tokval(t) for t in toks]
    top = pow(_BASE, K - 1, 1 << 64)
    h = 0
    for i in range(K):
        h = (h * _BASE + vals[i]) & _M64
    i = 0
    while True:
        v = _mix(h)
        if v % P == 0 and len(set(toks[i:i + K])) >= MIN_DISTINCT:
            fps.add(v)
        if i + K >= n:
            break
        h = ((h - vals[i] * top) * _BASE + vals[i + K]) & _M64
        i += 1
    return fps


def load_db(path):
    with open(path, "rb") as fh:
        raw = fh.read()
    return set(struct.unpack("<%dQ" % (len(raw) // 8), raw[: len(raw) // 8 * 8]))


def shape_check(root, dbpath):
    try:
        db = load_db(dbpath)
    except OSError as exc:
        # Fail closed: a missing reference is a broken package, and passing every tree
        # because the comparison could not run would be the silent kind of broken.
        print("fingerprint list unavailable: %s" % exc)
        return 1
    if not db:
        print("fingerprint list is empty")
        return 1
    shared = set()
    flagged = []
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in (".git", "node_modules")]
        for f in files:
            p = os.path.join(dirpath, f)
            data = candidate_source(p, f)
            if data is None:
                continue
            fps = fingerprints(data)
            hit = fps & db
            shared |= hit
            if len(hit) >= FILE_LIMIT and len(hit) >= FILE_RATIO * len(fps):
                flagged.append((os.path.relpath(p, root), len(hit), len(fps)))
    if flagged or len(shared) >= GLOBAL_LIMIT:
        print("php-src source found in an edited form (%d sampled code windows shared "
              "with php-src):" % len(shared))
        for rel, h, n in sorted(flagged, key=lambda x: -x[1])[:40]:
            print("  %s (%d of %d)" % (rel, h, n))
        return 1
    print("shape gate: ok (%d sampled code windows shared with php-src)" % len(shared))
    return 0


# ------------------------------------------------------------------ verbatim files
def verbatim_check(root, hashfile):
    known = set()
    try:
        with open(hashfile, encoding="utf8") as fh:
            for line in fh:
                line = line.strip()
                if line and not line.startswith("#"):
                    known.add(line.split()[0][:16])
    except OSError as exc:
        print("hash list unavailable: %s" % exc)
        return 1
    hits = []
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in (".git", "node_modules")]
        for f in files:
            p = os.path.join(dirpath, f)
            data = candidate_source(p, f)
            if data is None:
                continue
            # The list holds php-src's empty and one-line files too (an empty `.inc`,
            # a 17-byte `config.h`), and an empty header of the candidate's own would
            # otherwise "match" one.  Nothing that small vendors an interpreter.
            if len(data) < VERBATIM_MIN:
                continue
            h = hashlib.sha256(data).hexdigest()[:16]
            if h in known:
                hits.append(os.path.relpath(p, root))
    if hits:
        print("verbatim php-src sources found (%d):" % len(hits))
        for h in sorted(hits)[:40]:
            print("  " + h)
        return 1
    print("source gate: ok")
    return 0


# ------------------------------------------------------------------ recorder
TAGS = ("php-5.6.40 php-7.0.33 php-7.1.33 php-7.2.34 php-7.3.33 php-7.4.33 php-8.0.30 "
        "php-8.1.31 php-8.2.27 php-8.3.16 php-8.4.4").split()
DB_DIRS = ("Zend", "main", "sapi/cli")
# Code php-src bundles from elsewhere, which a candidate may legitimately carry in its
# own right (David Gay's dtoa, BSD/Apache string helpers, PostgreSQL's ps_title).
DB_SKIP = {"Zend/zend_strtod.c", "Zend/zend_strtod.h", "main/snprintf.c", "main/snprintf.h",
           "main/strlcat.c", "main/strlcpy.c", "main/explicit_bzero.c", "main/mergesort.c",
           "main/reentrancy.c", "sapi/cli/ps_title.c", "sapi/cli/ps_title.h"}
GENERATED = (b"Generated by re2c", b"Bison implementation for Yacc", b"A Bison parser")


def build_db(repo, out):
    blobs = {}
    for tag in TAGS:
        ls = subprocess.run(["git", "-C", repo, "ls-tree", "-r", tag] + list(DB_DIRS),
                            capture_output=True, text=True, check=True).stdout
        for line in ls.splitlines():
            meta, path = line.split("\t", 1)
            _mode, typ, oid = meta.split()
            if typ == "blob" and path.endswith(EXTS) and path not in DB_SKIP \
                    and "/tests/" not in path:
                blobs[oid] = path
    fps = set()
    oids = sorted(blobs)
    for i in range(0, len(oids), 500):
        chunk = oids[i:i + 500]
        p = subprocess.Popen(["git", "-C", repo, "cat-file", "--batch"],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        data, _ = p.communicate(("\n".join(chunk) + "\n").encode())
        pos = 0
        for _oid in chunk:
            nl = data.index(b"\n", pos)
            size = int(data[pos:nl].split()[2])
            body = data[nl + 1:nl + 1 + size]
            pos = nl + 1 + size + 1
            if any(g in body[:4096] for g in GENERATED):
                continue
            fps |= fingerprints(body)
    with open(out, "wb") as fh:
        for v in sorted(fps):
            fh.write(struct.pack("<Q", v))
    print("%d blobs, %d fingerprints -> %s" % (len(blobs), len(fps), out))
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--build-db":
        return build_db(sys.argv[2], sys.argv[3])
    root = sys.argv[1]
    hashfile = sys.argv[2] if len(sys.argv) > 2 else "/tests/php-src-hashes.txt"
    dbfile = sys.argv[3] if len(sys.argv) > 3 else "/tests/php-src-fingerprints.bin"
    rc = verbatim_check(root, hashfile)
    if rc:
        return rc
    return shape_check(root, dbfile)


if __name__ == "__main__":
    sys.exit(main())
