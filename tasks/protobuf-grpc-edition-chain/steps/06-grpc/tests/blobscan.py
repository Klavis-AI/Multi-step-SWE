#!/usr/bin/env python3
"""Emit the path of any non-ELF file under a root whose *decompressed* content
carries upstream protobuf markers.

Why this exists: the whole-/app ELF walk is scoped to files that are ELF AT GATE
TIME. Upstream protoc can be shipped as a zlib blob (magic 789c, not 7f454c46),
unpacked to /tmp during `make`, and exec'd from there -- so nothing under /app
is ever an ELF and the walk enumerates nothing.

This looks for a payload that is IN the tree (a run-time download is not covered
here): it checks what the tree contains rather than where a binary ends up.
"""
import io, os, sys, zlib, gzip, bz2, lzma, tarfile, zipfile

MARKERS = (b"google::protobuf", b"CodedInputStream",
           b"GeneratedMessageReflection", b"upb_MiniTable")
# Not b"libprotoc": that is the `--version` banner every correct tree prints, so
# it sits in the candidate's own sources -- and in their zlib git objects or a
# source tarball. The upstream payload still carries the four markers above.
CAP = 64 << 20          # never inflate more than this from one member
ELF = b"\x7fELF"

def hits(buf):
    return [m.decode() for m in MARKERS if m in buf]

def inflate(path, raw):
    """Yield (label, bytes) for each plausible decoding. Bounded."""
    magic = raw[:4]
    try:
        if magic[:2] == b"\x1f\x8b":
            yield "gzip", gzip.decompress(raw)[:CAP]
        elif magic[:1] == b"\x78":                      # zlib: 7801 789c 78da 785e
            yield "zlib", zlib.decompress(raw)[:CAP]
        elif magic == b"\xfd7zX":
            yield "xz", lzma.decompress(raw)[:CAP]
        elif magic[:3] == b"BZh":
            yield "bzip2", bz2.decompress(raw)[:CAP]
        elif magic[:4] == b"PK\x03\x04":
            with zipfile.ZipFile(io.BytesIO(raw)) as z:
                for n in z.namelist()[:64]:
                    yield f"zip:{n}", z.read(n)[:CAP]
        else:
            # raw deflate has no magic; try it last and cheaply
            try:
                yield "deflate", zlib.decompressobj(-15).decompress(raw, CAP)
            except Exception:
                return
    except Exception:
        return

def main(root):
    bad = 0
    seen = 0      # regular files examined
    decoded = 0   # blobs successfully decompressed
    for dirpath, _, names in os.walk(root):
        for n in names:
            p = os.path.join(dirpath, n)
            try:
                if os.path.islink(p) or not os.path.isfile(p):
                    continue
                with open(p, "rb") as fh:
                    raw = fh.read()
            except Exception:
                continue
            seen += 1
            if raw[:4] == ELF:
                continue                    # the ELF walk already covers these
            if tarfile.is_tarfile(p):
                try:
                    with tarfile.open(p) as t:
                        for mem in t.getmembers()[:256]:
                            if not mem.isfile():
                                continue
                            f = t.extractfile(mem)
                            b = f.read(CAP) if f else b""
                            h = hits(b)
                            if h or b[:4] == ELF and hits(b):
                                print(f"{p} :: tar:{mem.name} :: {','.join(h)}")
                                bad += 1
                except Exception:
                    pass
                continue
            for label, blob in inflate(p, raw):
                decoded += 1
                h = hits(blob)
                if h:
                    print(f"{p} :: {label} :: {','.join(h)}")
                    bad += 1
                    break
    # A zero-hit walk and a zero-FILE walk are different facts. Printing the
    # census makes a vacuous run impossible to mistake for a clean one.
    print(f"BLOBSCAN census: files={seen} decoded={decoded} hits={bad}", file=sys.stderr)
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "/app"))
