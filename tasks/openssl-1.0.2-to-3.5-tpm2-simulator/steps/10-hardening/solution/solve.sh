#!/usr/bin/env bash
# Step 10 golden solution -- the tree stops being quiet about itself.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/10-hardening/solution/solve.sh; edit it here and regenerate, never
# edit the copy under tasks/.
#
# Run from /app on the tree step 9 left behind, with OpenSSL 3.5.4 installed as
# the system OpenSSL.  It edits the tree in place and builds it once to prove
# the edits compile; the verifier builds it again from scratch, twice, so
# nothing here is load-bearing except the source changes and the two files
# added under fuzz/.
#
#
# WHAT THIS DOES, AND WHY EACH PIECE IS NEEDED
#
# Four deliverables, in the order the instruction asks for them.
#
# 1  THE FENCE.  -DOPENSSL_API_COMPAT=0x30500000L -DOPENSSL_NO_DEPRECATED
#    -Werror on the whole tree.  Step 8 already put the API macro and
#    -Wdeprecated-declarations on the build; what -Werror adds at 3.5 is four
#    diagnostics, and all four are the same defect: a function called through
#    an implicit declaration because nothing declares it.  Measured
#    (build/logs/probe10/scope.txt, NOTES section 13.0.2): four, in four files,
#    identical at 3.4 and at 3.5, and one of them -- _cpri__AESDecryptCBC --
#    was written down as step 10's work in section 3.5 nine steps ago.
#
#    An implicit declaration is not a style point on this tree.  It makes the
#    compiler invent `int f()' for a function that returns uint32_t and takes
#    three arguments, and on x86-64 that happens to work, which is why it has
#    survived.  The fix for each is a declaration where a declaration belongs,
#    never a cast at the call site.
#
# 2  THE ENGINE'S OWN FILES, under -Wall -Wextra -Werror as well: every
#    translation unit compiled out of src/OsslCryptoEngine/.  Measured: 10 TUs,
#    6 of them failing, 39 distinct diagnostic lines.  22 of the 39 are one
#    pattern -- TPM2B_TYPE() expands to a UNION, so `= {32,{...}}' initialises
#    the union's first member with a brace-less list and wants `= {{32,{...}}}'.
#    The rest is ordinary C hygiene: two functions that fall off the end
#    without returning, a macro whose body is `(x);', two comment blocks
#    spliced together by a trailing backslash, one dead static left behind when
#    step 5 moved RSA onto EVP.  None of them changes an answer, and the
#    corpora say so.
#
# 3  THE SANITIZERS.  address,undefined over the whole engine corpus and the
#    whole e2e corpus, with -fno-sanitize-recover=all.  28 distinct sites
#    (build/logs/probe10/sanr-sites.txt plus the ASan report the first
#    collector lost, NOTES 13.0.2), and they are four defects rather than 28:
#
#      * 13 shift overflows, all `0x80 << 24' in int.  TPM_HT is a UINT8, so
#        HR_TRANSIENT and its family promote to int and overflow.  One line in
#        TPM_Types.h fixes twelve of them and one line in swap.h fixes the
#        thirteenth.
#      * 11 misaligned OSSL_HASH_STATE accesses.  CRYPTO_ALIGNMENT falls
#        through TpmBuildSwitches.h's #if chain to 4 -- the comment on that
#        line says `For 32-bit builds' -- and OSSL_HASH_STATE holds a pointer,
#        so every overlay of it on CPRI_HASH_STATE.state is under-aligned on a
#        64-bit machine.  The switch to say so is already in the file.
#      * one null memcpy, from an HMAC key of length zero.
#      * one stack-buffer-overflow READ, _math__Normalize2B's scan loop
#        testing the byte before testing the bound.  This is the only one of
#        the four that costs behaviour: it reads a byte past the buffer and
#        then decides what to do based on it.
#
#    And a fifth that only exists once the second is fixed.  Aligning the hash
#    states raises the alignment of HASH_OBJECT, which holds one, and
#    TPM2_ContextSave stores a saved object inside a context blob at an offset
#    the integrity digest's size decides -- never a multiple of 8.  The blob
#    was always laid out that way; what changes is that the type now says what
#    it needs.  Measured rather than predicted: it does not appear in
#    probe10's list, and it is the only report left on the solved tree
#    (build/logs/probe10s.prealign/san-sites.txt).
#
# 4  THE FUZZ HARNESS.  fuzz/engine.c and fuzz/corpus/engine/.  The verifier
#    owns the loop; what this ships is the entry point and the seeds.  What it
#    drives is the engine's public surface out of CryptoEngine.h -- hashing,
#    HMAC, KDFa/KDFe, MGF1, the five AES modes, and the big-number helpers --
#    with the first two bytes of the input choosing an operation and an
#    algorithm and the rest being the data.  Every algorithm id is checked
#    against the engine's own capability query before it is used, because the
#    engine's answer to an id it does not implement is FAIL(), FAIL() here is
#    a longjmp, and a longjmp out of _cpri__StartHash leaks the EVP_MD_CTX it
#    has already allocated.  Leak detection is on for this gate, so that would
#    be a finding about the harness rather than about the tree.
#
#    It earns its place on the first run.  Driving the public surface rather
#    than the corpus reaches _math__Mul(), which the corpus does not, and
#    _math__Mul() takes three BIGNUMs out of a BN_CTX without opening a frame
#    in it -- so the BN_CTX_end() at the foot pops an empty stack and the
#    process dies inside libcrypto.  That is the sixth item in part 3, and it
#    is the one item there that no amount of running the corpus under the
#    sanitizers would ever have produced.
set -euo pipefail

APP=${1:-/app}
cd "$APP"

say()  { printf '\n== %s\n' "$*"; }
die()  { printf 'solve: %s\n' "$*" >&2; exit 1; }

for f in src/OsslCryptoEngine/CpriSym.c src/OsslCryptoEngine/CpriHash.c \
         src/OsslCryptoEngine/CpriECC.c src/OsslCryptoEngine/CpriRSA.c \
         src/OsslCryptoEngine/MathFunctions.c \
         src/OsslCryptoEngine/RSAKeySieve.c \
         src/OsslCryptoEngine/OsslCryptoEngine.h \
         src/include/CpriDataEcc.c src/include/CpriHashData.c \
         src/include/CpriSym_fp.h src/include/Platform.h \
         src/include/TpmError.h src/include/swap.h \
         src/tpm/include/TPM_Types.h src/tpm/include/TpmBuildSwitches.h \
         src/tpm/include/prototypes/Import_fp.h \
         src/simulator/TcpServer.c cmake/CMakeLists.txt; do
    [ -f "$f" ] || die "$f is missing -- this is not the tree step 9 produced"
done

# Step 9 has to be here already: this step hardens the tree the ladder built,
# and a tree that never reached step 9 would be hardened into a different
# shape.  The key object is step 9's own substitution and is in no earlier
# tree.
grep -q 'EVP_SKEY_import_raw_key' src/OsslCryptoEngine/CpriSym.c \
    || die "src/OsslCryptoEngine/CpriSym.c hands its ciphers raw bytes -- step 9 has not been applied"
grep -q 'RAND_set_DRBG_type' src/OsslCryptoEngine/CpriCryptPri.c \
    || die "src/OsslCryptoEngine/CpriCryptPri.c does not name a DRBG -- step 8 has not been applied"

say "source"
python3 - <<'PY'
import re, sys

EDITS = 0


def patch(path, edits):
    """Apply (literal, replacement, expected-count) triples to one file."""
    global EDITS
    src = open(path).read()
    for literal, repl, want in edits:
        # Whitespace in the anchor is "one or more of anything blank", which
        # is the only way to write an anchor for this codebase that a human
        # can read back against the file.
        pat = r"\s+".join(re.escape(t) for t in literal.split())
        src, got = re.subn(pat, lambda m, r=repl: r, src)
        if got != want:
            sys.exit("solve: %s: anchor matched %d time(s), expected %d:\n  %s"
                     % (path, got, want, " ".join(literal.split())[:110]))
        EDITS += got
    open(path, "w").write(src)


def patch_re(path, edits):
    """The same, for the four places where the anchor is a shape and not a
    string: twenty-one initialisers written the same way, sixteen scheme
    pairs, five DER tables, sixteen continued comment lines.  Writing those
    out one at a time would hide the fact that they are one edit each."""
    global EDITS
    src = open(path).read()
    for pat, repl, want in edits:
        src, got = re.subn(pat, repl, src)
        if got != want:
            sys.exit("solve: %s: pattern matched %d time(s), expected %d:\n  %s"
                     % (path, got, want, pat[:110]))
        EDITS += got
    open(path, "w").write(src)


def drop_function(path, header, name):
    """Delete a function definition and the comment block above it.  Used
    once, on a static that nothing has called since step 5 moved RSA onto
    EVP; the range is found by counting braces rather than by line number so
    that it does not quietly delete the wrong thing if the file moves."""
    global EDITS
    src = open(path).read()
    h = src.find(header)
    if h > 0:
        h = src.rfind("\n", 0, h) + 1        # back up to the start of that line
    d = src.find("\n" + name + "(", h if h >= 0 else 0)
    if h < 0 or d < 0:
        sys.exit("solve: %s: no %s to remove" % (path, name))
    i = src.index("{", d)
    depth = 0
    while True:
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    end = src.index("\n", i) + 1
    open(path, "w").write(src[:h] + src[end:])
    EDITS += 1


# ================================================== 1.  the fence: -Werror
#
# Four implicit declarations, four declarations.  Each one goes where the
# other declarations of its kind already live, which is the whole point: a
# prototype written at the call site, or a cast to silence the return type,
# would build and would leave the next caller with the same problem.

# close(2).  This compat block defines closesocket() in terms of close() and
# includes four system headers, none of which is the one that declares it.
patch("src/simulator/TcpServer.c", [
    ("""#include <sys/types.h>
#include <sys/socket.h>""",
     """#include <unistd.h>\n"""
     """#include <sys/types.h>\n"""
     """#include <sys/socket.h>""", 1),
])

# _cpri__AESDecryptCBC().  Defined and LIB_EXPORTed by CpriSym.c, called by
# CryptUtil.c, declared nowhere: the header has the other seven AES entry
# points and not this one.  The declaration is the definition's own parameter
# list, beside the encrypt half it pairs with.
patch("src/include/CpriSym_fp.h", [
    ("""LIB_EXPORT         CRYPT_RESULT
_cpri__AESEncryptCFB(""",
     """LIB_EXPORT       CRYPT_RESULT\n"""
     """_cpri__AESDecryptCBC(\n"""
     """   BYTE                            *dOut,                    //   OUT:   the      decrypted       data\n"""
     """   UINT32                             keySizeInBits,         //   IN:    key   size     in   bit\n"""
     """   BYTE                            *key,                     //   IN:    key   buffer.       The     size  of  this      buffer    in\n"""
     """                                                             //          bytes     is   (keySizeInBits         +    7)   /  8\n"""
     """   BYTE                            *iv,                      //   IN/OUT:      IV  for      decryption.        The  size       of  this\n"""
     """                                                             //          buffer       is   16   byte\n"""
     """   UINT32                             dInSize,               //   IN:    data     size\n"""
     """   BYTE                            *dIn                      //   IN:    data     buffer\n"""
     """   );\n"""
     """\n"""
     """LIB_EXPORT         CRYPT_RESULT\n"""
     """_cpri__AESEncryptCFB(""", 1),
])

# _plat__GetUnique().  Every other _plat__ entry point is declared in this
# header; this one was left out, and Startup.c calls it for the value that
# becomes VENDOR_PERMANENT's auth.  It returns uint32_t, and the implicit
# declaration the compiler invented returns int.
patch("src/include/Platform.h", [
    ("""LIB_EXPORT    int32_t
_plat__GetEntropy(""",
     """// C.8.9.1.  _plat__GetUnique()\n"""
     """\n"""
     """// This function is used to access the platform-specific unique value. Returns the number of bytes\n"""
     """\n"""
     """// copied, which may be less than bSize.\n"""
     """LIB_EXPORT    uint32_t\n"""
     """_plat__GetUnique(\n"""
     """      uint32_t               which,                   //  authorities        (0)  or  details      (1)\n"""
     """      uint32_t               bSize,                   //  size   of      the   buffer\n"""
     """      unsigned  char        *b                        //  output         buffer\n"""
     """);\n"""
     """\n"""
     """LIB_EXPORT    int32_t\n"""
     """_plat__GetEntropy(""", 1),
])

# TPM2_Import().  Import_fp.h has the In and Out structures, the response-code
# modifiers and the heading that says a prototype follows, and then stops.
# Every sibling command header ends with the declaration this one is missing,
# so the dispatcher calls the only command in the table through an implicit
# declaration.
patch("src/tpm/include/prototypes/Import_fp.h", [
    ("""// Declaration of function prototypes


#endif // _IMPORT_H_""",
     """// Declaration of function prototypes\n"""
     """TPM_RC\n"""
     """TPM2_Import(\n"""
     """    Import_In             *in,                   //  IN:   input    parameter    list\n"""
     """    Import_Out            *out                   //  OUT:   output     parameter       list\n"""
     """    );\n"""
     """\n"""
     """\n"""
     """#endif // _IMPORT_H_""", 1),
])

# ========================================== 2.  the engine under -Wall -Wextra
#
# Ten translation units come out of src/OsslCryptoEngine; six of them have
# something to say and between them they say it 39 times
# (build/logs/probe10/engine-diags.txt).  Grouped by cause the 39 are eight
# things, and the biggest group is a single initialiser shape repeated.

# -- missing braces, 22 of the 39 diagnostics, one cause.
#
# TPM2B_TYPE(name, bytes) expands to a *union* (TPMB.h:47): a {UINT16 size;
# BYTE buffer[bytes];} struct and a bare TPM2B overlaid.  So `= {32,{...}}'
# initialises the union's first member from a brace-less list, which is legal
# and which -Wmissing-braces is right about -- nothing in the initialiser says
# which member it is writing.  A brace says it.  No value moves.
#
# 56 rather than 21: the 21 are the initialisers inside the curve #ifdefs this
# build enables, and the same line written for a curve that is switched off is
# the same defect waiting for the switch.
patch_re("src/include/CpriDataEcc.c", [
    (r"(const\s+TPM2B_\d+_BYTE_VALUE\s+\w+\s*=\s*)\{(\s*\d+\s*,\s*\{[^{}]*\})\s*\}",
     r"\1{{\2}}", 56),
    # eccCurves[]: each row carries a TPMT_KDF_SCHEME and a TPMT_ECC_SCHEME,
    # both {algorithm id; union details;}.  The union's first member is a
    # one-field TPMS_SCHEME_HASH either way, so the hash id needs two braces
    # around it -- one for the union, one for the struct inside it.
    (r"\{(TPM_ALG_\w+),(TPM_ALG_\w+)\},", r"{\1,{{\2}}},", 16),
])

# g_hashData[]: HASH_INFO.der is `const BYTE der[20]' and SHA1_DER and friends
# are brace-less byte lists (Implementation.h:489), so the array member needs
# its own braces.  The TPM_ALG_NULL terminator already has them.
patch_re("src/include/CpriHashData.c", [
    (r"(\w+_DER_SIZE,\s+)(\w+_DER)\},", r"\1{\2}},", 5),
])

patch("src/OsslCryptoEngine/CpriECC.c", [
    # The twenty-second missing brace, same union, written out by hand.
    ("""TPM2B_4_BYTE_VALUE                 marshaledCounter                     =  {4,        {0}};""",
     """TPM2B_4_BYTE_VALUE                 marshaledCounter                     =  {{4,       {0}}};""", 1),

    # control reaches end of non-void function.  FAIL() does not return -- it
    # longjmps out of the crypto units -- but nothing in its declaration says
    # so, so the compiler is right that the path exists.  A return after it
    # costs nothing and is what every other function in this file does.
    ("""FAIL(FATAL_ERROR_INTERNAL);
}
static    const  ECC_CURVE_DATA              *
GetCurveData(""",
     """FAIL(FATAL_ERROR_INTERNAL);\n"""
     """   return         NULL;               //    not     reached:     FAIL()   does   not   return\n"""
     """}\n"""
     """static    const  ECC_CURVE_DATA              *\n"""
     """GetCurveData(""", 1),

    # this statement may fall through.  SM2 validation falls into `default:',
    # which overwrites retVal with CRYPT_SCHEME -- so the SM2 build has never
    # been able to validate an SM2 signature.  The break is a fix and not a
    # silencing; SM2 is off in this configuration, which is why no corpus case
    # moves.
    ("""retVal    =      ValidateSignatureSM2Dsa(rIn,             sIn,      curveId,      Qin,  digest);
#endif
            default:""",
     """retVal    =      ValidateSignatureSM2Dsa(rIn,             sIn,      curveId,      Qin,  digest);\n"""
     """                    break;\n"""
     """#endif\n"""
     """            default:""", 1),

    # variable 'bnX' set but not used.  Taken from the BN_CTX and never read;
    # the allocation check below it is on bnY, which is the last one out, so
    # dropping bnX leaves the check checking what it was checking.
    ("""BIGNUM                                      *bnX,         *bnY,       *bnR,     *bnD;""",
     """BIGNUM                                      *bnY,       *bnR,     *bnD;""", 1),
    ("""bnX     =  BN_CTX_get(context);
   bnY     =  BN_CTX_get(context);
   if(bnY         ==     NULL)""",
     """bnY     =  BN_CTX_get(context);\n"""
     """   if(bnY         ==     NULL)""", 1),

    # suggest parentheses around '&&' within '||'.  The three-conjunct half
    # binds tighter than the ||, which is what was meant; saying so in the
    # source makes the reader stop having to know that.
    ("""pAssert(                 r   !=     NULL     &&     (K     !=     NULL     ||   B     ==  NULL)         &&   (L  !=   NULL    ||     B  ==    NULL)
                  ||        (E   !=       NULL     ||     (M     ==  NULL     &&      B  !=     NULL)));""",
     """pAssert(     (r  !=   NULL    &&   (K  !=   NULL   ||   B  ==   NULL)   &&   (L  !=   NULL   ||   B  ==   NULL))\n"""
     """              ||   (E  !=   NULL   ||   (M  ==   NULL   &&   B  !=   NULL)));""", 1),

    # typedef 'TPM2B_DIGEST' locally defined but not used.  A TPM2B_TYPE()
    # inside a function body is a typedef, and this one names a type the
    # function never mentions.
    ("""BN_CTX                                  *context;
    TPM2B_TYPE(DIGEST,              MAX_DIGEST_SIZE);
    TPM2B_ECC_PARAMETER                      k;""",
     """BN_CTX                                  *context;\n"""
     """    TPM2B_ECC_PARAMETER                      k;""", 1),
])

patch("src/OsslCryptoEngine/CpriHash.c", [
    ("""FAIL(FATAL_ERROR_INTERNAL);
}

// The md objects of the table above, by algorithm id, for the asymmetric code""",
     """FAIL(FATAL_ERROR_INTERNAL);\n"""
     """     return     NULL;                 //     not    reached:    FAIL()   does   not   return\n"""
     """}\n"""
     """\n"""
     """// The md objects of the table above, by algorithm id, for the asymmetric code""", 1),
])

# multi-line comment, twice.  CpriSym.c carries the vendor's `//%' blocks --
# markers for the tool that extracts the published header from this source --
# and each block is a #define written across nine lines with a trailing
# backslash on every one of them.  A backslash at the end of a // comment
# splices the next line into it, so the block is one comment and -Wcomment says
# so.  Stripping the backslashes would break the extraction tool's input;
# wrapping each block in /* */ leaves every //% line byte-for-byte as the
# vendor wrote it and makes the continuation legal.
patch_re("src/OsslCryptoEngine/CpriSym.c", [
    (r"(?m)^(//%\s*#define\s+_cpri__AESDecrypt(?:CTR|OFB)\(.*\n(?://%.*\n)*)",
     r"/*\n\1*/\n", 2),
])

# 'static' is not at beginning of declaration.  The string is left alone; only
# the two storage words swap, which is what the diagnostic is about.
patch("src/OsslCryptoEngine/CpriRSA.c", [
    ("""const    static         char          defaultLabel[]         =""",
     """static    const         char          defaultLabel[]         =""", 1),
])

# 'RsaPrivateExponent' defined but not used.  Step 5 moved RSA key handling
# onto EVP_PKEY, and the private exponent is now computed by the provider from
# the key it is given; this static has had no caller since.  It goes with its
# comment block, because a comment describing a function that is not there is
# worse than either.
drop_function("src/OsslCryptoEngine/CpriRSA.c", "B.12.1.3.1.", "RsaPrivateExponent")

# 'noFuntion' defined but not used.  The vendor's placeholder (vendor's
# spelling) for the branch where RSA_KEY_SIEVE is off, written to stop the
# translation unit being empty.  It cannot be empty: OsslCryptoEngine.h is
# included above the #ifdef, so the unit has declarations either way.
patch("src/OsslCryptoEngine/RSAKeySieve.c", [
    ("""#else
static    void      noFuntion(
      void
      )
{
      pAssert(1);
}
#endif                             //%""",
     """#else\n"""
     """//   Nothing      here.     This     file     produces    no   code    unless\n"""
     """//   RSA_KEY_SIEVE       is    set,     and    the    placeholder      that   used    to   sit\n"""
     """//   here    to   keep      the    translation       unit    non-empty      was    never\n"""
     """//   needed:      OsslCryptoEngine.h        is   included      above     the    #ifdef.\n"""
     """#endif                             //%""", 1),
])

# statement with no effect.  UNREFERENCED_PARAMETER(x) expands to `(x);' and
# every call site writes its own semicolon, so the expansion is an expression
# statement that evaluates x and throws it away -- which is exactly what the
# macro is for and exactly what -Wunused-value objects to.  The cast says
# "deliberately discarded" in the way the compiler understands.
patch("src/OsslCryptoEngine/OsslCryptoEngine.h", [
    ("""#define UNREFERENCED_PARAMETER(x) (x);""",
     """#define UNREFERENCED_PARAMETER(x) ((void)(x))""", 1),
])

# statement with no effect / right-hand operand of comma expression has no
# effect.  pAssert's conditional yields 1 or 0 and every call site discards it.
# Nothing in this tree uses pAssert as an expression -- checked across all of
# src, including its one wrapper, assert2Bsize() in OsslCryptoEngine.h -- so
# the statement form is available, and it is the form that also makes
# `if(x) pAssert(y); else ...' mean what it looks like.
patch("src/include/TpmError.h", [
    ("""#     define  pAssert(a)     (!!(a)        ?  1  :  (FAIL(FATAL_ERROR_PARAMETER),                 0))""",
     """#     define  pAssert(a)     do  {  if(!(a))     FAIL(FATAL_ERROR_PARAMETER);    }  while(0)""", 1),
])

# right-hand operand of comma expression has no effect.  Each *_TO_BYTE_ARRAY
# macro ends with `, (i))' so that it can be used for its value, and nothing in
# this tree ever has -- all 26 call sites across src are bare statements,
# checked.  Dropping the tail leaves the assignments and the parentheses that
# make the macro safe in an if.
patch("src/include/swap.h", [
    (r"""((b)[0] = (BYTE)(i), i)""",
     r"""((b)[0]       =  (BYTE)(i))""", 1),
    (r"""(b)[1]        =  (BYTE)     (i),                \
                                           (i))""",
     r"""(b)[1]        =  (BYTE)     (i))""", 1),
    (r"""(b)[3]        =  (BYTE)     (i),                \
                                           (i))""",
     r"""(b)[3]        =  (BYTE)     (i))""", 1),
    (r"""(b)[7]        =  (BYTE)     (i),                \
                                           (i))""",
     r"""(b)[7]        =  (BYTE)     (i))""", 1),
])

# ============================================= 3.  the sanitizers, four causes
#
# address,undefined over the whole engine corpus and the whole e2e corpus
# reports 28 distinct sites (build/logs/probe10/sanr-sites.txt).  They are four
# defects; two of them account for 24 of the 28, and each of those two is one
# line of one header.

# -- 12 of the 13 shift overflows.  TPM_HT is a UINT8 (TPM_Types.h:422), so
# TPM_HT_TRANSIENT (0x80) promotes to int and `0x80 << 24' overflows a signed
# 32-bit integer.  The (TPM_HC) cast on the outside converts the result after
# the shift has already happened, which is exactly the shape UBSan is built to
# catch.  Casting the operand instead does the shift in the unsigned type these
# handles have always been.
patch_re("src/tpm/include/TPM_Types.h", [
    (r"\(TPM_HC\)\(\((TPM_HT_\w+)<<HR_SHIFT\)\)",
     r"(TPM_HC)(((TPM_HC)(\1))<<HR_SHIFT)", 7),
])

# -- the thirteenth, in marshal.c's UINT32 unmarshal.  BYTE_ARRAY_TO_UINT32
# shifts a BYTE left by 24 in int; BYTE_ARRAY_TO_UINT64 two lines below it
# already casts every operand, which is the same author writing the same macro
# correctly the second time.
patch("src/include/swap.h", [
    (r"""(UINT32)(     ((b)[0]  <<   24)    \
                                                      +  ((b)[1]  <<   16)    \
                                                      +  ((b)[2]  <<   8  )   \
                                                      +  (b)[3])""",
     r"""(UINT32)(     ((UINT32)(b)[0]  <<   24)    \
                                                      +  ((UINT32)(b)[1]  <<   16)    \
                                                      +  ((UINT32)(b)[2]  <<   8  )   \
                                                      +  (UINT32)(b)[3])""", 1),
])

# -- all 11 misaligned accesses.  TpmBuildSwitches.h picks CRYPTO_ALIGNMENT
# from a chain of #ifdefs and, with none of the switches set, falls through to
# `4 // For 32-bit builds'.  This is a 64-bit build, and OSSL_HASH_STATE holds
# an EVP_MD_CTX * in a union that CPRI_HASH_STATE aligns with CRYPTO_ALIGNED --
# so every hash state in the tree is a 4-byte-aligned object holding an
# 8-byte-aligned member.  The switch that says so is already in the file, three
# lines above the fall-through; it just was never turned on.
patch("src/tpm/include/TpmBuildSwitches.h", [
    ("""#define   SIMULATION
#define   FIPS_COMPLIANT""",
     """#define   SIMULATION\n"""
     """#define   FIPS_COMPLIANT\n"""
     """//   This    is   a    64-bit      build     and    OSSL_HASH_STATE      holds     an\n"""
     """//   EVP_MD_CTX     *,    so   the    hash    states      this     header      aligns    have   to\n"""
     """//   be    aligned      for    a   pointer.      Without      this,     CRYPTO_ALIGNMENT\n"""
     """//   falls     through       the    chain     below      to   4,    which     the    line    itself\n"""
     """//   says    is   for    32-bit      builds.\n"""
     """#define   CRYPTO_ALIGN_8""", 1),

    # ...and the typo three lines down, which has been harmless for as long as
    # none of the switches was set.  `#eliF' is not a directive, so it does not
    # end the group the taken branch opened: with CRYPTO_ALIGN_8 defined the
    # preprocessor walks straight on into `#define CRYPTO_ALIGNMENT 2' and
    # redefines what it has just defined.  Turning the switch on is what makes
    # this fixable rather than merely wrong.
    ("""#eliF   defined  CRYPTO_ALIGN_2""",
     """#elif   defined  CRYPTO_ALIGN_2""", 1),
])

# -- the null memcpy.  An HMAC key of length zero is legal and the corpus has
# them; memcpy(dst, key, 0) with a null key is undefined even though it copies
# nothing, and UBSan says so.  The guard is on the copy and not on the size
# assignment, because a zero-length key still has to set the size.
patch("src/OsslCryptoEngine/CpriHash.c", [
    ("""//     key     size       is     ok
             memcpy(oPadKey->buffer,                           key,     keySize);
             oPadKey->size                 =     keySize;""",
     """//     key     size       is     ok\n"""
     """             //  A   zero-length         HMAC    key    is   legal,     and    memcpy()      with   a\n"""
     """             //  null     source        is   undefined      even    when    it   copies      nothing.\n"""
     """             if(keySize            >  0)\n"""
     """                    memcpy(oPadKey->buffer,                    key,     keySize);\n"""
     """             oPadKey->size                 =     keySize;""", 1),
])

# -- the stack-buffer-overflow READ, and the only one of the four that can
# change an answer.  The scan loop reads b->buffer[from] before checking that
# `from' is still inside the buffer, so on an all-zero TPM2B it reads one byte
# past the end and then decides what to do with the result.  Swapping the two
# conjuncts is the whole fix; && is sequenced, so the read cannot happen unless
# the bound says it may.
patch("src/OsslCryptoEngine/MathFunctions.c", [
    ("""for(from    =   0;  b->buffer[from]         ==  0    &&  from   <  size;     from++);""",
     """for(from    =   0;  from   <  size    &&  b->buffer[from]         ==  0;     from++);""", 1),
])

# -- and the one the corpus cannot reach, which is the fuzz harness's first
# finding rather than the sanitizers'.  _math__Mul() allocates a BN_CTX and
# takes three BIGNUMs out of it without opening a frame first: no
# BN_CTX_start(), and then BN_CTX_end() at the foot pops a stack that was never
# pushed.  In OpenSSL 3 that pop reads indexes[-1] and the process dies with a
# SEGV inside BN_CTX_end, which is what the harness gets on any call that
# reaches the body.
#
# It has never been reached.  All twelve `mul' cases in the engine corpus
# declare a product buffer that is either too small -- which returns
# CRYPT_PARAMETER before the context is allocated -- or larger than
# MAX_2B_BYTES, which trips the pAssert above; and nothing else in the tree
# calls _math__Mul at all.  So the successful path of an exported engine entry
# point has been dead since the baseline, and a harness that drives the public
# surface rather than the corpus is exactly what finds that.  The two
# neighbours, _math__Div and _math__ModExp, open the frame correctly, which is
# the same author writing the same prologue twice more and getting it right.
patch("src/OsslCryptoEngine/MathFunctions.c", [
    ("""context       =   BN_CTX_new();
      if(context           ==   NULL)
            FAIL(FATAL_ERROR_ALLOCATION);
      bnA   =  BN_CTX_get(context);""",
     """context       =   BN_CTX_new();\n"""
     """      if(context           ==   NULL)\n"""
     """            FAIL(FATAL_ERROR_ALLOCATION);\n"""
     """      //   BN_CTX_get()       returns      a  number       from    the   frame     on   top    of\n"""
     """      //   the    context,      and    BN_CTX_end()          pops    that     frame.      Without\n"""
     """      //   this    line    there     is   no   frame:     the   pop   underflows       the\n"""
     """      //   stack.       _math__Div()          and   _math__ModExp()           open    theirs.\n"""
     """      BN_CTX_start(context);\n"""
     """      bnA   =  BN_CTX_get(context);""", 1),
])

# -- the misalignment the switch above uncovers, and the reason it is here
# rather than in the list probe10 printed.  CRYPTO_ALIGN_8 raises the alignment
# of every hash state, and with it the alignment of HASH_OBJECT, which holds
# one.  TPM2_ContextSave stores a saved object inside the context blob at
# `integritySize + fingerprintSize' bytes into a TPM2B's buffer -- 34 + 8 for
# SHA256, and the buffer itself starts 2 bytes into its own union -- so the
# object in a blob is never on an 8-byte boundary, and a sequence object read
# through that pointer is a misaligned HASH_OBJECT.  The blob was always laid
# out that way; what changes here is that the type now says what it needs, so
# the access becomes a diagnosable defect instead of a silent one.
#
# The padding cannot be added to the blob, because the offset would have to be
# computed from an address, and ContextSave's address and ContextLoad's address
# are different: the two sides would disagree about where the object starts.
# So the copy goes through an aligned local, which is the idiom this file
# already uses one function up for the fingerprint, and the blob's layout is
# untouched -- same bytes, same size, same integrity digest.  The write-back is
# conditional because only EXPORT_STATE has anything to write.
patch("src/tpm/command/Context/Context_spt.c", [
    ("""HASH_OBJECT *externalFmt = (HASH_OBJECT *)exportObject;

      if(object->attributes.eventSeq)
           count     =  HASH_COUNT;
      for(;   count;    count--)
           CryptHashStateImportExport(&internalFmt->state.hashState[count - 1],
                                         externalFmt->state.hashState, direction);""",
     """HASH_OBJECT                  externalFmt;\n"""
     """\n"""
     """      //   The    object     in   a   context      blob   starts     at   an   offset      the\n"""
     """      //   integrity       digest's      own   size    decides,       so   it   is   not   on   the\n"""
     """      //   boundary        a   hash   state     needs.       Copy   it   through      an   aligned\n"""
     """      //   local     rather      than   reading      it  where       it  lies:     the   blob's\n"""
     """      //   layout     does    not   change,        and   the   access      stops    being\n"""
     """      //   undefined.\n"""
     """      MemoryCopy(&externalFmt,                  exportObject,       sizeof(externalFmt),\n"""
     """                       sizeof(externalFmt));\n"""
     """\n"""
     """      if(object->attributes.eventSeq)\n"""
     """           count     =  HASH_COUNT;\n"""
     """      for(;   count;    count--)\n"""
     """           CryptHashStateImportExport(&internalFmt->state.hashState[count    -   1],\n"""
     """                                         externalFmt.state.hashState,        direction);\n"""
     """      if(direction          ==  EXPORT_STATE)\n"""
     """           MemoryCopy(exportObject,             &externalFmt,       sizeof(externalFmt),\n"""
     """                            sizeof(externalFmt));""", 1),
])

print("solve: %d source edit(s)" % EDITS)
PY

# ====================================================== 4.  the fuzz harness
#
# fuzz/engine.c and fuzz/corpus/engine/.  The verifier compiles this against a
# loop of its own and runs it; what the tree owns is the entry point, the
# meaning of an input, and the seeds.
#
# Two properties are worth naming because neither is obvious from the file.
#
# The first is that every algorithm id and key size is checked against the
# engine's own capability query before it is used.  The engine's answer to an
# id it does not implement is FAIL(), FAIL() is a longjmp into the TPM's
# failure mode, and a longjmp out of _cpri__StartHash leaves the EVP_MD_CTX it
# has already allocated unreachable.  This gate runs with leak detection on, so
# a harness that walks into that reports a defect in itself and calls it a
# defect in the tree.
#
# The second is that six of the fourteen operations check an answer rather than
# only surviving one: the five AES modes round-trip, and the incremental hash
# completes a copied state and the original and compares the two digests.  A
# harness that only asks "did it crash" cannot tell a working cipher from one
# that returns its input, and this step is about the tree saying what is true
# of it.
say "fuzz"
python3 - <<'PY'
import os

ENGINE_C = r"""/* fuzz/engine.c -- the crypto engine, driven by an arbitrary string of bytes.
 *
 * The driver owns the loop, the corpus and the mutation schedule.  This file
 * owns one question: what does a single input mean.  Byte 0 chooses the
 * operation, byte 1 chooses the algorithm and the key size, and everything
 * from byte 2 on is the data.  Anything shorter than three bytes has nothing
 * to say and is dropped.
 *
 * The engine's answer to an argument it cannot honour is FAIL(), which in this
 * tree is a longjmp into TPM failure mode and never a return.  A harness that
 * let that happen would leak whatever had been allocated before the jump, and
 * this gate runs with leak detection on -- so it would report a defect in the
 * harness and call it a defect in the tree.  Every algorithm id and key size
 * below is therefore checked against the engine's own capability query before
 * it is used, and every output buffer is sized from the engine's own answer
 * about how much it will write.  What is under test is the data, not the API
 * contract.
 *
 * Six of the operations check an answer as well as surviving it: the five AES
 * modes and the incremental hash are their own oracle -- decrypt(encrypt(x))
 * is x, and a hash state that is copied and then completed twice gives the
 * same digest twice.  A harness that only asks "did it crash" cannot tell a
 * working cipher from one that returns its input.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CryptoEngine.h"

#define FUZZ_MAX_DATA   4096

typedef union {
    struct {
        UINT16 size;
        BYTE   buffer[FUZZ_MAX_DATA];
    } t;
    TPM2B b;
} FUZZ_2B;

/* ---------------------------------------------------------------- set-up */

static void on_engine_fail(const char *function, int line, int code)
{
    /* Unreachable by design.  If it is reached, the harness asked for
     * something the engine does not offer, and saying so is better than a
     * longjmp out of a half-finished allocation. */
    fprintf(stderr, "fuzz: engine FAIL at %s:%d code=%d\n",
            function != NULL ? function : "?", line, code);
    abort();
}

static TPM_ALG_ID g_hash[16];
static int        g_nhash;
static UINT16     g_aes[3];
static int        g_naes;
static int        g_ready;

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    static const UINT16 candidate[3] = { 128, 192, 256 };
    UINT32 i;

    (void)argc;
    (void)argv;
    if (g_ready)
        return 0;

    _cpri__InitCryptoUnits(&on_engine_fail);

    /* The engine's own list, asked for rather than written down here and
     * hoped to still be true. */
    for (i = 0; g_nhash < (int)(sizeof g_hash / sizeof g_hash[0]); i++) {
        TPM_ALG_ID a = _cpri__GetHashAlgByIndex(i);

        if (a == TPM_ALG_NULL)
            break;
        if (_cpri__GetDigestSize(a) > 0 && _cpri__GetHashBlockSize(a) > 0)
            g_hash[g_nhash++] = a;
    }
    for (i = 0; i < sizeof candidate / sizeof candidate[0]; i++)
        if (_cpri__GetSymmetricBlockSize(TPM_ALG_AES, candidate[i]) == 16)
            g_aes[g_naes++] = candidate[i];

    g_ready = 1;
    return 0;
}

/* ------------------------------------------------------------- utilities */

static void spread(BYTE *dst, size_t want, const BYTE *src, size_t have)
{
    size_t i;

    /* Key and IV material comes from the input, repeated if the input is
     * shorter than the key.  A zero-filled key would make every seed that is
     * too short test the same key. */
    for (i = 0; i < want; i++)
        dst[i] = (BYTE)(have > 0 ? src[i % have] : (BYTE)i);
}

static void load2b(FUZZ_2B *b, const BYTE *src, size_t have, size_t cap)
{
    b->t.size = (UINT16)(have < cap ? have : cap);
    if (b->t.size > 0)
        memcpy(b->t.buffer, src, b->t.size);
}

/* --------------------------------------------------------------- the ops */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    BYTE       buf[FUZZ_MAX_DATA];
    BYTE       digest[MAX_DIGEST_SIZE];
    BYTE       digest2[MAX_DIGEST_SIZE];
    TPM_ALG_ID hashAlg;
    UINT16     hashSize;
    unsigned   op, sel;
    size_t     n;

    if (size < 3)
        return 0;
    if (!g_ready)
        LLVMFuzzerInitialize(NULL, NULL);
    if (g_nhash == 0 || g_naes == 0)
        return 0;

    op  = data[0];
    sel = data[1];
    n   = size - 2;
    if (n > sizeof buf)
        n = sizeof buf;
    memcpy(buf, data + 2, n);

    hashAlg  = g_hash[sel % (unsigned)g_nhash];
    hashSize = _cpri__GetDigestSize(hashAlg);
    if (hashSize == 0 || hashSize > sizeof digest)
        return 0;

    switch (op % 14) {

    case 0: {                                   /* one-shot hash */
        _cpri__HashBlock(hashAlg, (UINT32)n, buf, hashSize, digest);
        break;
    }

    case 1: {                                   /* incremental hash, and its
                                                 * own oracle: a copied state
                                                 * completes to the same
                                                 * digest as the original */
        CPRI_HASH_STATE state;
        CPRI_HASH_STATE copy;
        size_t          off;
        size_t          chunk = (size_t)(sel >> 3) + 1;

        if (_cpri__StartHash(hashAlg, TRUE, &state) == 0)
            break;
        for (off = 0; off < n; off += chunk) {
            size_t k = n - off < chunk ? n - off : chunk;

            _cpri__UpdateHash(&state, (UINT32)k, buf + off);
        }
        _cpri__CopyHashState(&copy, &state);
        _cpri__CompleteHash(&copy, hashSize, digest);
        _cpri__CompleteHash(&state, hashSize, digest2);
        if (memcmp(digest, digest2, hashSize) != 0) {
            fprintf(stderr, "fuzz: copied hash state gave a different digest\n");
            abort();
        }
        break;
    }

    case 2: {                                   /* HMAC, including the
                                                 * zero-length key -- a legal
                                                 * key and a null pointer */
        CPRI_HASH_STATE state;
        FUZZ_2B         oPadKey;
        UINT16          keySize = (UINT16)(sel % 70);

        if (keySize > n)
            keySize = (UINT16)n;
        oPadKey.t.size = 0;
        if (_cpri__StartHMAC(hashAlg, FALSE, &state, keySize,
                             keySize > 0 ? buf : NULL, &oPadKey.b) == 0)
            break;
        _cpri__UpdateHash(&state, (UINT32)n, buf);
        _cpri__CompleteHMAC(&state, &oPadKey.b, hashSize, digest);
        break;
    }

    case 3: {                                   /* MGF1 */
        BYTE   mask[512];
        UINT32 mSize = (UINT32)(sel % sizeof mask) + 1;

        _cpri__MGF1(mSize, mask, hashAlg, (UINT32)n, buf);
        break;
    }

    case 4: {                                   /* KDFa */
        BYTE    stream[256];
        FUZZ_2B key;
        FUZZ_2B contextU;
        FUZZ_2B contextV;
        UINT32  counter = (UINT32)(sel >> 4);
        UINT32  bits    = 8u * ((UINT32)(sel % sizeof stream) + 1u);
        size_t  third   = n / 3 + 1;

        load2b(&key, buf, n < 64 ? n : 64, 64);
        load2b(&contextU, buf, third, 128);
        load2b(&contextV, buf + n / 2, n - n / 2, 128);
        _cpri__KDFa(hashAlg, &key.b, "fuzz", &contextU.b, &contextV.b,
                    bits, stream, &counter, FALSE);
        break;
    }

    case 5: {                                   /* KDFe */
        BYTE    stream[256];
        FUZZ_2B z;
        FUZZ_2B partyU;
        FUZZ_2B partyV;
        UINT32  bits = 8u * ((UINT32)(sel % sizeof stream) + 1u);

        load2b(&z, buf, n, 256);
        load2b(&partyU, buf, n / 2, 128);
        load2b(&partyV, buf + n / 2, n - n / 2, 128);
        _cpri__KDFe(hashAlg, &z.b, (sel & 1) ? "fuzz" : NULL,
                    &partyU.b, &partyV.b, bits, stream);
        break;
    }

    case 6:                                     /* AES-CBC, round trip */
    case 7:                                     /* AES-CFB, round trip */
    case 8:                                     /* AES-OFB, round trip */
    case 9:                                     /* AES-CTR, round trip */
    case 10: {                                  /* AES-ECB, round trip */
        BYTE   key[32];
        BYTE   iv[16];
        BYTE   ivIn[16];
        BYTE   cipher[FUZZ_MAX_DATA];
        BYTE   plain[FUZZ_MAX_DATA];
        UINT32 bits = g_aes[(sel >> 2) % (unsigned)g_naes];
        UINT32 len  = (UINT32)n;
        int    mode = (int)(op % 14);
        CRYPT_RESULT r1;
        CRYPT_RESULT r2;

        /* CBC and ECB take whole blocks only; the stream modes take any
         * length, which is the difference worth exercising rather than
         * rounding away. */
        if (mode == 6 || mode == 10)
            len &= ~(UINT32)15;
        if (len == 0)
            break;

        spread(key, sizeof key, buf, n);
        spread(ivIn, sizeof ivIn, buf + (n > 16 ? n - 16 : 0),
               n > 16 ? 16 : n);
        memcpy(iv, ivIn, sizeof iv);

        switch (mode) {
        case 6:
            r1 = _cpri__AESEncryptCBC(cipher, bits, key, iv, len, buf);
            memcpy(iv, ivIn, sizeof iv);
            r2 = _cpri__AESDecryptCBC(plain, bits, key, iv, len, cipher);
            break;
        case 7:
            r1 = _cpri__AESEncryptCFB(cipher, bits, key, iv, len, buf);
            memcpy(iv, ivIn, sizeof iv);
            r2 = _cpri__AESDecryptCFB(plain, bits, key, iv, len, cipher);
            break;
        case 8:
            r1 = _cpri__AESEncryptOFB(cipher, bits, key, iv, len, buf);
            memcpy(iv, ivIn, sizeof iv);
            r2 = _cpri__AESDecryptOFB(plain, bits, key, iv, len, cipher);
            break;
        case 9:
            r1 = _cpri__AESEncryptCTR(cipher, bits, key, iv, len, buf);
            memcpy(iv, ivIn, sizeof iv);
            r2 = _cpri__AESDecryptCTR(plain, bits, key, iv, len, cipher);
            break;
        default:
            r1 = _cpri__AESEncryptECB(cipher, bits, key, len, buf);
            r2 = _cpri__AESDecryptECB(plain, bits, key, len, cipher);
            break;
        }
        if (r1 != CRYPT_SUCCESS || r2 != CRYPT_SUCCESS)
            break;
        if (memcmp(plain, buf, len) != 0) {
            fprintf(stderr, "fuzz: AES mode %d did not round trip\n", mode);
            abort();
        }
        break;
    }

    case 11: {                                  /* normalise / denormalise */
        FUZZ_2B b;
        UINT16  before;

        load2b(&b, buf, n, 512);
        before = b.t.size;
        _math__Normalize2B(&b.b);
        if (b.t.size > before) {
            fprintf(stderr, "fuzz: normalise grew a number\n");
            abort();
        }
        _math__Denormalize2B(&b.b, before);
        break;
    }

    case 12: {                                  /* the small integer helpers */
        BYTE   a[64];
        BYTE   b[64];
        BYTE   c[256];
        UINT16 cSize;
        UINT32 pSize = sizeof c;
        UINT32 aSize = (UINT32)(n / 2 < sizeof a ? n / 2 : sizeof a);
        UINT32 bSize = (UINT32)(n - n / 2 < sizeof b ? n - n / 2 : sizeof b);

        if (aSize == 0 || bSize == 0)
            break;
        memcpy(a, buf, aSize);
        memcpy(b, buf + n / 2, bSize);
        _math__uComp(aSize, a, bSize, b);
        _math__Comp(aSize, a, bSize, b);
        _math__sub(aSize, a, bSize, b, &cSize, c);
        _math__Mul(aSize, a, bSize, b, &pSize, c);
        _math__Dec(aSize, a);
        break;
    }

    default: {                                  /* divide, modexp, primality */
        FUZZ_2B numerator;
        FUZZ_2B denominator;
        FUZZ_2B quotient;
        FUZZ_2B remainder;
        BYTE    modulus[64];
        BYTE    base[64];
        BYTE    exponent[8];
        BYTE    result[64];
        UINT32  prime;

        load2b(&numerator, buf, n < 128 ? n : 128, 128);
        load2b(&denominator, buf + n / 2, n - n / 2 < 64 ? n - n / 2 : 64, 64);
        if (denominator.t.size == 0)
            break;
        /* A zero denominator is FATAL_ERROR_DIVIDE_ZERO, which is the engine
         * refusing rather than the engine breaking; the bottom bit keeps the
         * fuzzer on the side of the question this harness is asking. */
        denominator.t.buffer[denominator.t.size - 1] |= 1;
        quotient.t.size = 128;
        remainder.t.size = 128;
        _math__Div(&numerator.b, &denominator.b, &quotient.b, &remainder.b);

        /* Modular exponentiation wants an odd modulus and a base below it:
         * anything else is an OpenSSL error return that this tree turns into
         * FATAL_ERROR_INTERNAL. */
        spread(modulus, sizeof modulus, buf, n);
        spread(base, sizeof base, buf + n / 2, n - n / 2);
        spread(exponent, sizeof exponent, buf, n);
        modulus[0] |= 0x80;
        modulus[sizeof modulus - 1] |= 1;
        base[0] &= 0x7f;
        _math__ModExp((UINT32)sizeof result, result,
                      (UINT32)sizeof base, base,
                      (UINT32)sizeof exponent, exponent,
                      (UINT32)sizeof modulus, modulus);

        prime = ((UINT32)buf[0] << 24) | ((UINT32)buf[1 % n] << 16)
              | ((UINT32)buf[2 % n] << 8) | (UINT32)buf[3 % n];
        _math__IsPrime(prime);
        break;
    }
    }
    return 0;
}
"""

os.makedirs("fuzz/corpus/engine", exist_ok=True)
open("fuzz/engine.c", "w").write(ENGINE_C)


def blob(n, tag):
    """n bytes that do not repeat and are not the same in any two seeds."""
    return bytes(((i * 37 + tag * 101 + 11) & 0xFF) for i in range(n))


# One seed per operation, plus a second reading of the two that have a branch
# worth naming: a hash short enough to fit one block and one long enough not
# to, and an HMAC key of length zero -- the null memcpy part 3 fixed, which is
# reached only when the key is both empty and NULL.
SEEDS = [
    ("hash-short",     0,  0, blob(3, 1)),
    ("hash-long",      0,  2, blob(200, 2)),
    ("hash-chunked",   1,  9, blob(130, 3)),
    ("hmac-keyed",     2,  3, blob(70, 4)),
    ("hmac-empty-key", 2, 70, blob(48, 5)),
    ("mgf1",           3,  5, blob(32, 6)),
    ("kdfa",           4,  4, blob(96, 7)),
    ("kdfe",           5,  7, blob(96, 8)),
    ("aes-cbc-128",    6,  0, blob(48, 9)),
    ("aes-cfb-192",    7,  4, blob(37, 10)),
    ("aes-ofb-256",    8,  8, blob(41, 11)),
    ("aes-ctr-128",    9,  1, blob(64, 12)),
    ("aes-ecb-192",   10,  5, blob(32, 13)),
    ("normalize",     11,  2, blob(40, 14)),
    ("int-helpers",   12,  3, blob(64, 15)),
    ("div-modexp",    13,  6, blob(80, 16)),
]
for name, op, sel, payload in SEEDS:
    open(os.path.join("fuzz/corpus/engine", name), "wb").write(
        bytes([op, sel]) + payload)

print("solve: 1 harness, %d seed(s)" % len(SEEDS))
PY

# ======================================================== the tree still builds
say "build"
B=${BUILD_DIR:-$(mktemp -d)}
cmake -S "$APP/cmake" -B "$B" -DSPEC_VERSION=116 > "$B/configure.log" 2>&1 \
    || { tail -40 "$B/configure.log" >&2; die "cmake configure failed"; }
cmake --build "$B" -j"${JOBS:-7}" > "$B/build.log" 2>&1 \
    || { grep -m10 ': error:' "$B/build.log" >&2; die "build failed"; }
for t in platform/libplatform.so OsslCryptoEngine/libCryptoEngine.so \
         tpm/libTPM.so Simulator; do
    [ -e "$B/$t" ] || die "$t was not built"
done

# The fence itself, which is gate 1: the API raised to 3.5 with the deprecated
# surface switched off, -Werror on the whole tree, and -Wall -Wextra -Werror on
# the engine's own translation units.  One configure, with the compile database
# generated by the same generate that is built -- step 8's section 12.12
# argument, unchanged.
say "fence"
F=$(mktemp -d)
cmake -S "$APP/cmake" -B "$F" -DSPEC_VERSION=116 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_C_FLAGS="-DOPENSSL_API_COMPAT=0x30500000L -DOPENSSL_NO_DEPRECATED -Werror" \
    > "$F/configure.log" 2>&1 \
    || { tail -40 "$F/configure.log" >&2; die "fence configure failed"; }
cmake --build "$F" -j"${JOBS:-7}" > "$F/build.log" 2>&1 \
    || { grep -m10 ': error:' "$F/build.log" >&2
         die "the tree does not build behind the fence"; }
for t in platform/libplatform.so OsslCryptoEngine/libCryptoEngine.so \
         tpm/libTPM.so Simulator; do
    [ -e "$F/$t" ] || die "$t was not built behind the fence"
done

# The engine's ten units again, off that database, with the wider flag set.
# The database is the only honest source for how a unit was compiled: reading
# CMakeLists.txt would say what the tree asks for and not what it got.
PYTHONPYCACHEPREFIX=/tmp/pyc python3 - "$F/compile_commands.json" <<'PY' \
    || die "the engine does not build under -Wall -Wextra -Werror"
import json, os, shlex, subprocess, sys

db = json.load(open(sys.argv[1]))
eng = "/app/src/OsslCryptoEngine/"
sel = [e for e in db
       if os.path.realpath(os.path.join(e.get("directory", ""),
                                        e["file"])).startswith(eng)]
if len(sel) != 10:
    sys.exit("solve: expected 10 engine units in the database, found %d"
             % len(sel))
bad = []
for e in sel:
    argv = e.get("arguments") or shlex.split(e["command"])
    out, i = [], 0
    while i < len(argv):                      # drop the -o, keep everything else
        if argv[i] == "-o":
            i += 2
            continue
        if argv[i].startswith("-o") and len(argv[i]) > 2:
            i += 1
            continue
        out.append(argv[i])
        i += 1
    r = subprocess.run(out + ["-o", "/dev/null", "-Wall", "-Wextra", "-Werror"],
                       cwd=e["directory"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, check=False)
    if r.returncode != 0:
        bad.append(os.path.basename(e["file"]))
        print(r.stdout[:2000], file=sys.stderr)
if bad:
    sys.exit("solve: %s" % ", ".join(bad))
print("solve: 10 engine unit(s) clean under -Wall -Wextra -Werror")
PY

say "assert"

# Trap 7: a failed edit prints what a successful edit prints.  The first
# reading is in the substitutions themselves -- each one carries the number of
# places it expects to match and stops the script when the tree disagrees --
# and the second is the two builds above, which are what every fix in parts 1
# and 2 exists to make possible.  What neither covers is part 3, whose fixes
# compile identically whether or not they are right, and part 4, which the
# build does not touch at all.  Those are the ones read back here.
have() { grep -Eq "$2" "$1" || die "$3"; }
gone() { if grep -Eq "$2" "$1"; then die "$3"; fi; }

n=$(grep -c '(TPM_HC)(((TPM_HC)' src/tpm/include/TPM_Types.h || true)
[ "$n" = 7 ] || die "expected 7 handle-range casts, found $n"
have src/include/swap.h '\(\(UINT32\)\(b\)\[0\]' \
     "BYTE_ARRAY_TO_UINT32 still shifts a BYTE in int"
have src/tpm/include/TpmBuildSwitches.h '^#define[[:space:]]+CRYPTO_ALIGN_8' \
     "the alignment switch is not set"
gone src/tpm/include/TpmBuildSwitches.h '#eliF' \
     "the #eliF typo is still there, so CRYPTO_ALIGNMENT is redefined"
have src/OsslCryptoEngine/CpriHash.c 'if\(keySize[[:space:]]+>[[:space:]]+0\)' \
     "the zero-length HMAC key still reaches memcpy"
have src/OsslCryptoEngine/MathFunctions.c \
     'from[[:space:]]+<[[:space:]]+size[[:space:]]+&&[[:space:]]+b->buffer\[from\]' \
     "the normalise loop still reads before it checks the bound"
have src/tpm/command/Context/Context_spt.c \
     'HASH_OBJECT[[:space:]]+externalFmt;' \
     "the sequence state is still read through the blob's own pointer"
n=$(grep -c 'BN_CTX_start(context);' src/OsslCryptoEngine/MathFunctions.c || true)
[ "$n" = 3 ] \
    || die "expected 3 BN_CTX frames opened in MathFunctions.c, found $n"

# The two functions that were removed, and the two things that would have been
# left behind if the removal had gone wrong: a comment block describing a
# function that is not there, and a call to one.
gone src/OsslCryptoEngine/CpriRSA.c 'RsaPrivateExponent' \
     "RsaPrivateExponent is still in CpriRSA.c"
gone src/OsslCryptoEngine/RSAKeySieve.c 'noFuntion' \
     "the noFuntion placeholder is still in RSAKeySieve.c"

# Part 4, read as the gate reads it: a harness with the standard entry point,
# and a corpus that is a corpus.
[ -f fuzz/engine.c ] || die "fuzz/engine.c was not written"
have fuzz/engine.c 'int[[:space:]]+LLVMFuzzerTestOneInput\(' \
     "fuzz/engine.c does not define the entry point the driver calls"
seeds=$(find fuzz/corpus/engine -maxdepth 1 -type f | wc -l)
distinct=$(find fuzz/corpus/engine -maxdepth 1 -type f -exec md5sum {} + |
           awk '{print $1}' | sort -u | wc -l)
empty=$(find fuzz/corpus/engine -maxdepth 1 -type f -empty | wc -l)
[ "$seeds" -ge 8 ] || die "the seed corpus has $seeds file(s), wanted at least 8"
[ "$distinct" = "$seeds" ] \
    || die "$seeds seed(s) but only $distinct distinct: two seeds are the same file"
[ "$empty" = 0 ] || die "$empty seed(s) are empty"
say "fuzz corpus: $seeds seed(s), $distinct distinct, $empty empty"

say "done"
