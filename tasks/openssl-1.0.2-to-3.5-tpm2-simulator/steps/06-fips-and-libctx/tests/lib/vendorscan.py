#!/usr/bin/env python3
"""Find a vendored OpenSSL inside a source tree, by what a file *is*.

The step's requirement is "no OpenSSL header and no OpenSSL static archive
remains anywhere under /app -- not unreferenced, not unused, not moved aside",
and the first version of this gate implemented it as

    find /app -name '*.h' -path '*/openssl/*'
    find /app \\( -name libcrypto.a -o -name libssl.a -o -name libeay32.lib \\)

which is a check on the *path*, not on the file.  `git mv lib/openssl lib/ossl`
passes it with the whole of 1.0.2b still in the tree, and so does renaming
libcrypto.a.  Between them those two gates are 2/6 of the migrated-ness family,
18% of the step's reward, and the instruction says "moved aside" in as many
words -- so the check has to read the bytes.

Two detections, kept separate because they answer different questions and
either alone passes for the wrong reason:

  headers   a file is an OpenSSL header if it declares OpenSSL's API (a
            HEADER_*_H / OPENSSL_*_H include guard) -- structural,
            name-independent, 71 of the baseline's 76 -- or if it carries
            OpenSSL's provenance (the SSLeay or OpenSSL Project copyright
            block) *and* is named like a header.  The name condition is only on
            the weak half: it keeps lib/OpenSSL Copyright.txt, which is
            vendored OpenSSL but is not a header, out of a count the step
            reports as a header count.  Union: 76 on the baseline, 0 across
            src/ and cmake/.

            The guard has to appear on a preprocessor line -- `#ifndef
            HEADER_EVP_H`, `#if defined(HEADER_RC4_H)` -- and not merely
            somewhere in the bytes.  A file that names one in a comment or a
            string is talking about OpenSSL's headers, which is not the same as
            being one, and a byte-anywhere match would count a .c file that
            mentioned a guard in a comment as a vendored header.

            Neither spelling is taken on its prefix alone, because the prefix
            is not the part that is OpenSSL's.  OPENSSL_*_H is the 3.x spelling
            and it collides with a perfectly ordinary coinage: a candidate's own
            compatibility shim guarded OPENSSL_COMPAT_H is not a vendored
            OpenSSL header, and calling it one costs an honest tree 1/6 of its
            migrated-ness.  That branch asks for provenance as well, which a
            genuinely vendored 3.x header carries -- it is a verbatim copy of a
            file whose licence block OpenSSL puts at the top of every header --
            and which a shim written for this task does not.

            HEADER_*_H is the 1.x spelling, and the same collision exists one
            level down: HEADER_OSSL_COMPAT_H is the same coinage in OpenSSL's
            older convention, and `#ifdef HEADER_EVP_H` in a .c file is a
            consumer asking whether a header has been included.  Provenance
            cannot be asked for here -- five of the baseline's own vendored
            headers have a guard and no copyright block -- so this branch asks
            instead that the *token* be one OpenSSL uses, against OSSL_GUARD
            below.  All 71 of the baseline's structural detections are tokens
            OpenSSL spells; HEADER_OSSL_COMPAT_H is not, and neither, as it
            happens, is HEADER_EVP_H (evp.h has guarded HEADER_ENVELOPE_H since
            SSLeay).

            OPENSSL_VERSION_NUMBER and OPENSSL_VERSION_TEXT are not matched:
            those macros are how a *consumer* asks which OpenSSL it is being
            compiled against, which is ordinary portable C that no requirement
            in this migration forbids, and a file that guards a call site with
            `#if OPENSSL_VERSION_NUMBER >= ...` is not a vendored OpenSSL
            header.  Reading a version macro
            is using OpenSSL; declaring OpenSSL's API is being OpenSSL, and the
            requirement is about the second.

            What remains is one residual, accepted knowingly: a 3.x header
            someone vendored *and* stripped the licence block out of, and then
            renamed, reads as clean.  Stripping a copyright notice out of a
            file you are keeping is a strange thing to do by accident, and the
            alternative -- demanding the guard also be a header OpenSSL ships
            -- reintroduces the hand-maintained list of filenames this scanner
            exists to avoid.

  archives  a file is an OpenSSL static archive if it is an ar archive whose
            *symbol index* defines names that only OpenSSL defines.  The index
            is part of the ar format, so this needs no binutils and no nm, and
            it is the same question P3 asks of the built ELF -- "does this
            object define OpenSSL's symbols itself".  MSVC decorates with a
            leading underscore, which is why libeay32.lib is stripped before
            matching; without that it reads 0 OpenSSL symbols out of 10005.
            Falls back to member names (cryptlib.o, s3_lib.o, ...) for an
            archive with no index at all.  7 on the baseline.

Both tests are applied to what is inside a wrapper as well as to what is on
disk.  `gzip lib/x86-64/libcrypto.a` is not an ar archive and `tar cf ossl.tar
lib/openssl` is not a header, and either one leaves the whole of 1.0.2 in the
tree for a later step to unpack -- which is the same miss `git mv lib/openssl
lib/ossl` was, one compressor further along.  gzip, xz, bzip2, zip and tar are
opened, two levels deep, and a wrapper this scanner cannot open is reported
under `unopened` rather than passed over in silence.

And through a symlink, which is that same sentence one inode type further
along again.  A link to a file is read through and its target's bytes are
classified as if they were here; a link to a directory is recorded under
`unopened`, because os.walk does not descend into one and a directory this
scanner has not read is a directory whose contents it has not checked.  A link
whose target is inside the root is passed over instead, since the real
directory is walked in its own right.

Both counts are numbers, not booleans, and both lists are printed, because a
gate that fails on a count nobody can see beside it is a gate nobody can check
from the transcript afterwards.
"""
import argparse
import bz2
import gzip
import io
import json
import lzma
import os
import re
import struct
import sys
import tarfile
import zipfile

# Structural: this file declares OpenSSL's own API.  1.0.2 and 1.1.x guard with
# HEADER_ENVELOPE_H, 3.x with OPENSSL_EVP_H; neither spelling appears anywhere
# in this repository's own 184 headers.  Include guards only -- a file that
# reads OpenSSL's version macros is a file that uses OpenSSL, which is not what
# this gate is looking for -- and only where the preprocessor would read them,
# so that naming one in a comment is not being one.
#
# Neither spelling stands alone on the token.  OPENSSL_*_H is checked against
# PROVENANCE by the caller; HEADER_*_H is checked against the tokens OpenSSL
# itself uses, in OSSL_GUARD below.
PP = rb"^[ \t]*#[ \t]*(?:ifndef|ifdef|define|if|elif)\b[^\n]*\b"
STRUCTURAL = re.compile(PP + rb"HEADER_([A-Z0-9_]+)_H\b", re.M)
STRUCTURAL_3X = re.compile(PP + rb"OPENSSL_[A-Z0-9_]+_H\b", re.M)

# Which HEADER_*_H tokens are OpenSSL's.  The prefix is distinctive but the
# token is what identifies the header, and `HEADER_<anything>_H` matched on the
# prefix alone reports two files that are not vendored OpenSSL: a shim of the
# candidate's own guarded HEADER_OSSL_COMPAT_H -- the same coinage the 3.x
# branch already allows for -- and a .c file asking `#ifdef HEADER_EVP_H`
# whether a header has been included yet.  The second is the sharper case:
# HEADER_EVP_H is not an OpenSSL guard at all (evp.h has guarded
# HEADER_ENVELOPE_H since SSLeay), so a file testing it is not even wrong about
# OpenSSL in a way this gate should read as being OpenSSL.
#
# The set is the union of the guards the vendored 1.0.2b copy defines and the
# guards the installed 1.1.0l headers define, and it is frozen rather than
# maintained: HEADER_*_H is a convention OpenSSL retired in 3.0, so the list of
# releases that can add to it is closed.  main() still unions in whatever the
# installed headers spell, so an image that installs some other 1.x release
# contributes its own guards without this list being touched.  Regenerate with
#
#   grep -hoE '^[ \t]*#[ \t]*(ifndef|define|if|elif|ifdef)\b.*\bHEADER_[A-Z0-9_]+_H\b' \
#        <tree>/lib/openssl/*.h /usr/local/include/openssl/*.h |
#     grep -oE 'HEADER_[A-Z0-9_]+_H' | sort -u
#
# Residual, accepted knowingly and symmetric with the 3.x branch's: a candidate
# who guards a file of their own with one of OpenSSL's own 80 tokens is read as
# vendoring it.  HEADER_ENVELOPE_H on a file that is not evp.h is not a coinage
# anyone reaches by accident, and it would collide with the real header.
OSSL_GUARD = frozenset((
    "AES ASN1T ASN1 ASN1_MAC ASYNC BF_LOCL BIO BLOWFISH BN BUFFER "
    "CAMELLIA CAST CMAC CMS COMP CONF_API CONF CRYPTLIB CRYPTO CT "
    "D1_SRTP DES DES_LOCL DH DSA DSO DTLS1 EBCDIC ECDH ECDSA EC ENGINE "
    "ENVELOPE ERR E_OS2 HMAC IDEA KDF KRB5_ASN LHASH MD2 MD4 MD5 MDC2 "
    "NEW_DES OBJECTS OCSP OPENSSLV OPENSSL_TYPES PEM PKCS12 PKCS7 "
    "PQUEUE RAND RC2 RC4 RC4_LOCL RC5 RIPEMD RSA SAFESTACK SEED SHA SRP "
    "SSL23 SSL2 SSL3 SSL SSL_LOCL STACK SYMHACKS TLS1 TS TXT_DB "
    "UI_COMPAT UI WHRLPOOL X509V3 X509 X509_VFY"
).split())


def installed_guards(incdir):
    """The HEADER_*_H tokens the installed OpenSSL's own headers spell.

    Empty when there is no such directory: the frozen set already covers every
    release that uses this spelling, so a missing install weakens nothing, and
    failing here would fail the gate on the one fact it does not need.
    """
    found = set()
    d = os.path.join(incdir, "openssl")
    try:
        names = sorted(os.listdir(d))
    except OSError:
        return found
    for fn in names:
        if not fn.endswith(".h"):
            continue
        try:
            with open(os.path.join(d, fn), "rb") as fh:
                blob = fh.read(READ)
        except OSError:
            continue
        found.update(m.group(1).decode("ascii", "replace")
                     for m in STRUCTURAL.finditer(blob))
    return found

# Provenance: this file came out of an OpenSSL release.  Weaker -- a source
# file that merely mentions OpenSSL in a comment could match -- so it counts
# only for files named like a header.
PROVENANCE = re.compile(
    rb"eay@cryptsoft\.com"
    rb"|The OpenSSL Project"
    rb"|openssl-core@openssl\.org"
    rb"|www\.openssl\.org/source/license"
)

HEADERISH = re.compile(r"\.(h|hh|hpp|hxx|H|inc|h\.in)$")

# Symbols only OpenSSL defines.  Deliberately prefixes of the public API rather
# than a list of object files: an archive built from OpenSSL sources under any
# name still defines them.
OSSL_SYM = re.compile(
    r"^(OPENSSL_|OpenSSL_|SSLeay|CRYPTO_|EVP_|BN_|BN_MONT|DSA_|DH_|RSA_|EC_|"
    r"ECDSA_|ECDH_|SHA1_|SHA224_|SHA256_|SHA384_|SHA512_|MD5_|MD4_|RIPEMD160_|"
    r"AES_|DES_|RC2_|RC4_|CAST_|IDEA_|BF_|CAMELLIA_|SEED_|WHIRLPOOL_|GOST|"
    r"ERR_|ASN1_|X509_|X509V3_|PKCS5_|PKCS7_|PKCS8_|PKCS12_|SSL_|SSLv|TLSv|"
    r"DTLS|d2i_|i2d_|PEM_|HMAC_|CMAC_|RAND_|BIO_|ENGINE_|OBJ_|NCONF_|CONF_|"
    r"UI_|OCSP_|TS_|CMS_|SRP_|EC_GROUP|EC_POINT|EC_KEY|COMP_|TXT_DB|"
    r"MODES_|CRYPTO_gcm|CRYPTO_ccm|CRYPTO_ofb|CRYPTO_cfb|CRYPTO_cbc|CRYPTO_ctr)"
)

# For an archive with no symbol index: object files only OpenSSL ships.
OSSL_MEMBER = re.compile(
    r"(^|[\\/])(cryptlib|mem_dbg|ex_data|o_init|o_time|err_all|evp_enc|"
    r"evp_lib|bn_lib|bn_mont|rsa_lib|dsa_lib|ec_lib|ec_key|ecp_smpl|"
    r"s3_lib|s3_enc|ssl_lib|ssl_sess|t1_enc|aes_core|aes_cbc|sha1dgst|"
    r"sha256|sha512|md5_dgst|asn1_lib|a_int|x509_v3|obj_dat)\.(o|obj)$"
)

READ = 128 * 1024        # a header that has not identified itself in 128 KiB
                         # is not identifying itself
CAP = 512 * 1024 * 1024  # what a wrapper is allowed to expand to before this
                         # stops reading it and says so


def classify_header(name, blob, guards=OSSL_GUARD):
    """(is_header, why) -- why is None when it is not one.

    `name` is what the file is called (for the provenance branch's header-name
    condition), `blob` its first READ bytes, `guards` the HEADER_*_H tokens that
    count as OpenSSL's own.
    """
    if b"\0" in blob[:512]:           # a binary is not a header
        return False, None
    for m in STRUCTURAL.finditer(blob):
        tok = m.group(1).decode("ascii", "replace")
        if tok in guards:
            return True, "declares-openssl-api (HEADER_%s_H)" % tok
    if STRUCTURAL_3X.search(blob) and PROVENANCE.search(blob):
        return True, "declares-openssl-api-3x"
    if HEADERISH.search(name) and PROVENANCE.search(blob):
        return True, "openssl-provenance"
    return False, None


def ar_symbols(fh):
    """Symbol index of an ar archive, or None if the bytes are not one.

    Takes a seekable binary file object, positioned anywhere.  Returns
    ([symbols], [member names]); either may be empty.  Parsing stops at the
    first malformed header rather than raising: a truncated archive is still an
    archive, and what has been read so far is still evidence.
    """
    try:
        fh.seek(0)
        if fh.read(8) != b"!<arch>\n":
            return None
        syms, members, longnames = [], [], b""
        while True:
            hdr = fh.read(60)
            if len(hdr) < 60 or hdr[58:60] != b"`\n":
                break
            name = hdr[0:16].decode("latin1").rstrip()
            try:
                size = int(hdr[48:58].decode("latin1").strip())
            except ValueError:
                break
            at = fh.tell()
            if name in ("/", "__.SYMDEF", "__.SYMDEF SORTED", "/SYM64/"):
                body = fh.read(size)
                wide = name == "/SYM64/"
                w, fmt = (8, ">Q") if wide else (4, ">I")
                if len(body) >= w:
                    n = struct.unpack(fmt, body[:w])[0]
                    tail = body[w + w * n:]
                    syms = [s.decode("latin1", "replace")
                            for s in tail.split(b"\0") if s]
            elif name.startswith("//"):
                longnames = fh.read(size)
            elif name.startswith("/") and name[1:].isdigit():
                off = int(name[1:])
                end = longnames.find(b"/", off)
                if end < 0:
                    end = longnames.find(b"\n", off)
                members.append(longnames[off:end].decode("latin1", "replace"))
            else:
                members.append(name.rstrip("/"))
            fh.seek(at + size + (size % 2))
        return syms, members
    except (OSError, ValueError):
        return None


def classify_archive(fh):
    got = ar_symbols(fh)
    if got is None:
        return False, None
    syms, members = got
    # MSVC decorates cdecl with a leading underscore: libeay32.lib's index
    # reads 0 OpenSSL symbols out of 10005 without this strip.
    hits = [s for s in syms if OSSL_SYM.match(s.lstrip("_"))]
    if hits:
        return True, "defines %d OpenSSL symbols (%s, ...)" % (
            len(hits), ", ".join(hits[:3]))
    named = [m for m in members if OSSL_MEMBER.search(m.replace("\\", "/"))]
    if named:
        return True, "contains OpenSSL objects (%s, ...)" % ", ".join(named[:3])
    return False, None


# The first bytes of the wrappers that are worth opening.  tar has no magic at
# offset 0 -- its "ustar" sits at 257 -- so it is detected separately.
COMPRESSED = ((b"\x1f\x8b", "gzip"), (b"\xfd7zXZ\x00", "xz"),
              (b"BZh", "bzip2"), (b"PK\x03\x04", "zip"),
              (b"\x28\xb5\x2f\xfd", "zstd"))


def wrapper_kind(head):
    """What kind of container this blob's first bytes say it is, or None."""
    for magic, kind in COMPRESSED:
        if head.startswith(magic):
            return kind
    if head[257:262] == b"ustar":
        return "tar"
    return None


SUFFIX = re.compile(r"\.(gz|xz|bz2|bzip2|zst|zstd|Z|tgz|txz|tbz2?)$")


def unwrap(name, kind, blob, unopened):
    """Yield (member-name, bytes) for what is inside one wrapper.

    One level only; the caller recurses, so `foo.tar.gz` is two.  A single-file
    compressor's member takes the outer name with the compression suffix
    stripped, so `evp.h.gz` still looks like a header to the name half of the
    provenance test.  Anything that will not open is recorded in `unopened` and
    yields nothing, because a container this scanner cannot read is a container
    whose contents it has not checked, and silence there would read as a clean
    tree.
    """
    inner = SUFFIX.sub("", name) or "(%s)" % kind
    try:
        if kind in ("gzip", "xz", "bzip2"):
            op = {"gzip": gzip.open, "xz": lzma.open, "bzip2": bz2.open}[kind]
            with op(io.BytesIO(blob), "rb") as fh:
                yield inner, fh.read(CAP)
            return
        if kind == "zstd":
            # Not in the standard library before 3.14.  Absent is recorded,
            # not assumed empty.
            try:
                from compression import zstd            # 3.14+
                yield inner, zstd.decompress(blob)
            except ImportError:
                import zstandard                        # or the pypi module
                yield inner, zstandard.ZstdDecompressor().decompress(blob)
            return
        if kind == "zip":
            with zipfile.ZipFile(io.BytesIO(blob)) as z:
                for info in z.infolist():
                    if not info.is_dir() and info.file_size <= CAP:
                        yield info.filename, z.read(info)
            return
        if kind == "tar":
            with tarfile.open(fileobj=io.BytesIO(blob), mode="r:") as t:
                for m in t:
                    if not m.isfile() or m.size > CAP:
                        continue
                    f = t.extractfile(m)
                    if f is not None:
                        yield m.name, f.read()
            return
    except Exception as exc:                # noqa: BLE001 -- any failure to
        unopened.append({"why": "%s: %s" % (kind, exc.__class__.__name__)})


def classify_blob(name, blob, unopened, guards=OSSL_GUARD, depth=0):
    """(kind, why) for bytes: "header", "archive", or (None, None).

    The same two tests as on disk, applied through wrappers.  Depth 2 covers
    foo.tar.gz and stops short of a decompression bomb's later floors.
    """
    ok, why = classify_header(name, blob[:READ], guards)
    if ok:
        return "header", why
    ok, why = classify_archive(io.BytesIO(blob))
    if ok:
        return "archive", why
    kind = wrapper_kind(blob[:512])
    if kind is None or depth >= 2:
        return None, None
    for member, inner in unwrap(name, kind, blob, unopened):
        what, why = classify_blob(member, inner, unopened, guards, depth + 1)
        if what:
            return what, "%s inside %s %s" % (why, kind, member)
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="/app")
    ap.add_argument("--out", help="write the full per-file detail here")
    ap.add_argument("--quiet", action="store_true")
    # Where the installed OpenSSL's own include guards are read from, to be
    # unioned with the frozen set.  The default is the prefix every step image
    # installs its OpenSSL into; it is an option so the gate's own rehearsal can
    # point it at a fixture.
    ap.add_argument("--ossl-include", default="/usr/local/include")
    args = ap.parse_args()

    guards = OSSL_GUARD | installed_guards(args.ossl_include)
    root = os.path.realpath(args.root)

    headers, archives, unopened, scanned = [], [], [], 0
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in (".git", ".hg")]

        # A symlinked directory is a container this scanner declines to open,
        # and it is treated as one.  os.walk does not descend into it, so
        # `ln -s /tmp/kept/openssl lib/openssl` would otherwise leave the whole
        # of 1.0.2b one inode type beyond "not moved aside" -- the same miss
        # `git mv lib/openssl lib/ossl` and `gzip libcrypto.a` were, and the
        # instruction's sentence covers all three.  A link whose target is
        # inside the root is walked in its own right, so it is passed over
        # rather than counted twice.
        for d in list(dirnames):
            q = os.path.join(dirpath, d)
            if not os.path.islink(q):
                continue
            dirnames.remove(d)
            tgt = os.path.realpath(q)
            if tgt == root or tgt.startswith(root + os.sep):
                continue
            unopened.append({"path": q,
                             "why": "symlink to a directory outside the tree"})

        for fn in filenames:
            p = os.path.join(dirpath, fn)
            # Symlinks to files are read through, so the bytes at the other end
            # are classified as if they were here -- which is what "remains
            # anywhere under /app" means about them.  isfile() follows the link
            # and is still false for a dangling one, a fifo or a device, none of
            # which has an OpenSSL header at the end of it.
            if not os.path.isfile(p):
                continue
            scanned += 1
            # The file itself is read through the file, not through memory: the
            # ar index needs seeks and libeay32.lib is 20 MB.  Only a wrapper,
            # which has to be expanded anyway, is read whole.
            try:
                with open(p, "rb") as fh:
                    head = fh.read(READ)
                    ok, why = classify_header(p, head, guards)
                    if ok:
                        headers.append({"path": p, "why": why})
                        continue
                    ok, why = classify_archive(fh)
                    if ok:
                        archives.append({"path": p, "why": why})
                        continue
                    if wrapper_kind(head[:512]) is None:
                        continue
                    fh.seek(0)
                    blob = fh.read(CAP)
            except OSError:
                continue
            mine = []
            what, why = classify_blob(p, blob, mine, guards)
            if what == "header":
                headers.append({"path": p, "why": why})
            elif what == "archive":
                archives.append({"path": p, "why": why})
            for u in mine:
                unopened.append(dict(u, path=p))

    out = {
        "scanned": scanned,
        "headers": len(headers),
        "archives": len(archives),
        "unopened": len(unopened),
        "header_files": [h["path"] for h in headers],
        "archive_files": [a["path"] for a in archives],
        "unopened_files": [u["path"] for u in unopened],
    }
    print(json.dumps(out))

    if args.out:
        with open(args.out, "w") as fh:
            json.dump({"scanned": scanned, "headers": headers,
                       "archives": archives, "unopened": unopened}, fh, indent=1)
    if not args.quiet:
        sys.stderr.write("vendorscan: %d files under %s -- %d OpenSSL header(s),"
                         " %d OpenSSL archive(s), %d unreadable container(s)\n"
                         % (scanned, args.root, len(headers), len(archives),
                            len(unopened)))
        for u in unopened[:6]:
            sys.stderr.write("  unopened %s  (%s)\n" % (u["path"], u["why"]))
        for h in headers[:12]:
            sys.stderr.write("  header  %s  (%s)\n" % (h["path"], h["why"]))
        if len(headers) > 12:
            sys.stderr.write("  header  ... and %d more\n" % (len(headers) - 12))
        for a in archives[:12]:
            sys.stderr.write("  archive %s  (%s)\n" % (a["path"], a["why"]))


if __name__ == "__main__":
    main()
