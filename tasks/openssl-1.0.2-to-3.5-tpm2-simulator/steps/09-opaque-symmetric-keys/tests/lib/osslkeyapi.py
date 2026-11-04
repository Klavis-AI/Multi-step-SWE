#!/usr/bin/env python3
"""How the installed headers let a caller hand a symmetric key to a cipher.

    osslkeyapi.py [--include DIR] [--api 0x30500000L] [--out FILE]

Derived, never listed -- for the same reason ossldep.py is.  Step 09 asks that
the engine stop handing raw key BYTES to a cipher context and hand it a key
OBJECT instead, and a gate written on a list of function names typed into a
verifier is a gate that is wrong the moment OpenSSL adds a spelling.  3.5.4 is
already the cautionary case: EVP_CipherPipelineEncryptInit and
EVP_CipherPipelineDecryptInit look like the migrated shape -- they are new, they
are not deprecated, they take a length -- and they take the key as
`const unsigned char *key'.  A hand-written ban list assembled by reading the
EVP_*Init family would have missed both.

So the set is read off the headers themselves:

  scoped    every function declaration whose FIRST parameter is an
            EVP_CIPHER_CTX *.  That is what "initialises a cipher context"
            means without having to guess from the name; it is also what keeps
            EVP_CIPHER_meth_set_init (a method-table setter whose ctx is inside
            a callback type) and EVP_PBE_CipherInit/_ex (whose ctx is the fifth
            parameter, and which take a PASSWORD rather than a key) out of it.

  accepted  of those, the ones that take an EVP_SKEY * -- the key object.
            On 3.5.4 that is exactly EVP_CipherInit_SKEY.

  refused   of those, the ones that START an operation by handing it key
            material as bytes: a parameter named `iv', which is what an
            initialisation takes and a data call does not, AND some other
            parameter whose pointee is `unsigned char'.  Both halves are
            needed.  Without the second, `iv' alone would be satisfied by
            EVP_CIPHER_CTX_get_original_iv; without the first, `unsigned
            char *' alone sweeps in EVP_CipherUpdate, EVP_EncryptFinal and
            every other data call, whose buffers are plaintext and ciphertext
            -- and the golden calls EVP_CipherUpdate, so a ban derived that
            way would refuse the tree this step is asking for.

  other     of those, everything else: the data calls, the accessors, and
            EVP_CIPHER_CTX_rand_key, which fills a caller's buffer rather than
            starting anything.  Reported in full so that a declaration this
            classifier could not place is visible rather than silently dropped
            into neither set.

  makers    every declaration that RETURNS an EVP_SKEY *, whatever its
            parameters.  A tree has to build a key object somehow, and there
            are several honest ways (import raw bytes, import a parameter
            list, generate, move one to another provider).  The gate that
            wants "a key object was constructed" asks for one of these rather
            than for the one the golden happens to use.

Parsed with libclang against the same umbrella TU ossldep.py builds, at the
same API level, and WITHOUT OPENSSL_NO_DEPRECATED: a spelling this step refuses
is refused whether or not some later release retires it, and defining the fence
macro here would hide the retired ones and shrink the ban.
"""
import json
import os
import sys
import tempfile

LIBCLANG = os.environ.get("LIBCLANG", "/usr/lib/llvm-14/lib/libclang.so.1")

CTX = ("EVP_CIPHER_CTX", "struct evp_cipher_ctx_st")
SKEY = ("EVP_SKEY", "struct evp_skey_st")
BYTES = ("unsigned char",)


def umbrella(include):
    """A TU that includes every public OpenSSL header."""
    d = os.path.join(include, "openssl")
    skip = ("asn1_mac.h", "opensslconf.h", "configuration.h", "macros.h")
    names = sorted(n for n in os.listdir(d)
                   if n.endswith(".h") and n not in skip)
    return "".join("#include <openssl/%s>\n" % n for n in names)


def pointee(t):
    """(pointer depth, every spelling of what it eventually points at).

    Both the written spelling and the canonical one, with the qualifiers
    dropped: a header is free to write EVP_CIPHER_CTX or the struct tag it is a
    typedef for, and `const unsigned char *' has to answer to `unsigned char'.
    """
    depth = 0
    while t.kind.name == "POINTER":
        t = t.get_pointee()
        depth += 1
    out = set()
    for s in (t.spelling, t.get_canonical().spelling):
        for q in ("const ", "volatile ", "restrict "):
            s = s.replace(q, "")
        out.add(s.strip())
    return depth, out


def census(include, api):
    import clang.cindex as C
    C.Config.set_library_file(LIBCLANG)
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "umbrella.c")
        with open(src, "w") as fh:
            fh.write(umbrella(include))
        tu = C.Index.create().parse(
            src, args=["-DOPENSSL_API_COMPAT=%s" % api, "-I" + include])
        fatal = [d.spelling for d in tu.diagnostics if d.severity >= 3]
        if fatal:
            sys.stderr.write("osslkeyapi: headers did not parse cleanly:\n  %s\n"
                             % "\n  ".join(fatal[:5]))
            sys.exit(2)
        scoped, accepted, refused, other, makers = set(), set(), set(), set(), set()
        seen = set()
        stack = [tu.cursor]
        while stack:
            n = stack.pop()
            if n.kind == C.CursorKind.FUNCTION_DECL:
                seen.add(n.spelling)
                rdepth, rnames = pointee(n.result_type)
                if rdepth == 1 and rnames.intersection(SKEY):
                    makers.add(n.spelling)
                args = list(n.get_arguments())
                if args:
                    d0, n0 = pointee(args[0].type)
                    if d0 == 1 and n0.intersection(CTX):
                        scoped.add(n.spelling)
                        takes_obj = takes_iv = takes_bytes = False
                        for a in args:
                            ad, an = pointee(a.type)
                            if ad >= 1 and an.intersection(SKEY):
                                takes_obj = True
                            if ad < 1 or not an.intersection(BYTES):
                                continue
                            if a.spelling == "iv":
                                takes_iv = True
                            else:
                                takes_bytes = True
                        takes_bytes = takes_bytes and takes_iv
                        if takes_obj:
                            accepted.add(n.spelling)
                        elif takes_bytes:
                            refused.add(n.spelling)
                        else:
                            other.add(n.spelling)
            stack.extend(n.get_children())
        return (sorted(scoped), sorted(accepted), sorted(refused),
                sorted(other), sorted(makers), len(seen))


def main(argv):
    inc = argv[argv.index("--include") + 1] if "--include" in argv \
        else "/usr/local/include"
    api = argv[argv.index("--api") + 1] if "--api" in argv else "0x30500000L"
    scoped, accepted, refused, other, makers, total = census(inc, api)
    out = {"include": inc, "api": api, "declared": total,
           "scoped": scoped, "accepted": accepted, "refused": refused,
           "other": other, "makers": makers}
    if "--out" in argv:
        with open(argv[argv.index("--out") + 1], "w") as fh:
            json.dump(out, fh, indent=1)
    sys.stderr.write("osslkeyapi: %d declaration(s) take an EVP_CIPHER_CTX * "
                     "first -- %d take a key object, %d take key bytes, %d "
                     "neither; %d declaration(s) return a key object\n"
                     % (len(scoped), len(accepted), len(refused), len(other),
                        len(makers)))
    if "--out" not in argv:
        json.dump(out, sys.stdout, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
