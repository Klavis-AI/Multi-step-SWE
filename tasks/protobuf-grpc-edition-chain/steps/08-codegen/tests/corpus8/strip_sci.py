#!/usr/bin/env python3
"""Remove source_code_info (field 9) from every FileDescriptorProto in a
CodeGeneratorRequest, in place. See PROVENANCE.md for why."""
import sys

def varint(b, i):
    v = s = 0
    while True:
        c = b[i]; i += 1
        v |= (c & 0x7F) << s; s += 7
        if not c & 0x80:
            return v, i

def recs(b):
    i = 0
    while i < len(b):
        tag, i = varint(b, i); num, wt = tag >> 3, tag & 7
        if wt == 0:
            j = i; _, i = varint(b, i); yield num, wt, b[j:i]
        elif wt == 1: yield num, wt, b[i:i + 8]; i += 8
        elif wt == 5: yield num, wt, b[i:i + 4]; i += 4
        elif wt == 2:
            n, j = varint(b, i); yield num, wt, b[j:j + n]; i = j + n
        else: raise ValueError("wire type %d" % wt)

def wvar(v):
    o = bytearray()
    while True:
        c = v & 0x7F; v >>= 7
        o.append(c | (0x80 if v else 0))
        if not v: return bytes(o)

def emit(n, w, p):
    h = wvar(n << 3 | w)
    return h + wvar(len(p)) + p if w == 2 else h + p

def strip9(msg):
    return b"".join(emit(n, w, p) for n, w, p in recs(msg) if n != 9)

for path in sys.argv[1:]:
    raw = open(path, "rb").read()
    out = b"".join(emit(n, w, strip9(p) if n in (15, 17) else p)
                   for n, w, p in recs(raw))
    open(path, "wb").write(out)
