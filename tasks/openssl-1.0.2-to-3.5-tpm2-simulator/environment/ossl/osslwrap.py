#!/usr/bin/env python3
"""Wrap and unwrap one staged OpenSSL archive.

    osslwrap.py wrap   <archive> <wrapped> <key-hex>
    osslwrap.py unwrap <wrapped> <archive> <key-hex>

/opt/ossl holds several staged releases in one directory.  Naming the
archives for their contents (blob-<first 16 hex of SHA-256>) keeps the release
out of `ls`, and that is all it does: the file is still an xz archive of a
DESTDIR, so

    tar -xJOf blob-... ./usr/local/include/openssl/opensslv.h | grep VERSION_TEXT

would read the release out of it.  A step is told which OpenSSL it targets by
its own instruction.

So each archive is also wrapped under a 32-byte key of its own, and each step's
setup.sh carries exactly one key: the key to the release that step targets.

The wrap is a XOR against a SHA-256 counter keystream:

    keystream block i = SHA256(key || i as 8 big-endian bytes)
    wrapped           = archive XOR keystream

This is not protecting a secret from an attacker who has the key -- each
step's setup.sh carries the key to its own release.  It is the minimum that
keeps the other archives unreadable without their keys, and it is small
enough to read in one sitting.  What it relies on is one key per archive,
never reused: two archives under one key would be a two-time pad.
stage_ossl.sh draws a fresh key whenever an archive's digest changes.

The blob name is still a claim the file has to satisfy, checked before
anything is unpacked -- activate.sh hashes what comes *out* of here, so a
wrong key fails the same check a substituted archive fails, at the same place,
before the prefix is touched.

Both directions are the same operation, and both live in this one file so that
the side that writes the blobs and the side that opens them cannot drift: this
file is what stage_ossl.sh runs to wrap, and it is shipped into /opt/ossl for
activate.sh to run to unwrap.  build/probe/osslwrap.sh rehearses the pair.
"""
import hashlib
import sys


def keystream(key, n):
    """n bytes of SHA256(key || counter), counter from 0, 32 bytes a block."""
    out = bytearray()
    i = 0
    while len(out) < n:
        out += hashlib.sha256(key + i.to_bytes(8, "big")).digest()
        i += 1
    return bytes(out[:n])


def xor(data, key):
    """data XOR keystream(key).  Its own inverse, which is why one function
    serves both directions."""
    if not data:
        return b""
    ks = keystream(key, len(data))
    return (int.from_bytes(data, "big")
            ^ int.from_bytes(ks, "big")).to_bytes(len(data), "big")


def main(argv):
    if len(argv) != 5 or argv[1] not in ("wrap", "unwrap"):
        sys.stderr.write(__doc__.split("\n\n")[0] + "\n\n"
                         "    osslwrap.py wrap   <archive> <wrapped> <key-hex>\n"
                         "    osslwrap.py unwrap <wrapped> <archive> <key-hex>\n")
        return 2
    _, _, src, dst, keyhex = argv
    keyhex = keyhex.strip()
    # Checked rather than assumed: a key that is short, or that is a path or a
    # file name that got here by a shell mistake, would otherwise wrap an
    # archive under something nobody can reproduce, and the failure would
    # surface much later as an archive that does not unwrap.
    if len(keyhex) != 64:
        sys.stderr.write("osslwrap.py: a key is 64 hex digits, not %d chars\n"
                         % len(keyhex))
        return 2
    try:
        key = bytes.fromhex(keyhex)
    except ValueError:
        sys.stderr.write("osslwrap.py: the key is not hex\n")
        return 2

    with open(src, "rb") as fh:
        data = fh.read()
    with open(dst, "wb") as fh:
        fh.write(xor(data, key))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
