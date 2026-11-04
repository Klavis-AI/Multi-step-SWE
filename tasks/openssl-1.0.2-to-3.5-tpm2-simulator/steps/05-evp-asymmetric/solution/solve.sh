#!/usr/bin/env bash
# Step 5 golden solution -- the public half of RSA and of ECDSA, moved onto
# EVP_PKEY objects the engine builds from parameters in its own library
# context, with the hand-rolled public-side padding deleted.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/05-evp-asymmetric/solution/solve.sh; edit it here and regenerate, never
# edit the copy under tasks/.
#
# Run from /app on the tree step 4 left behind, with OpenSSL 3.0.17 installed
# as the system OpenSSL.  It edits the tree in place and builds it once to
# prove the edits compile; the verifier builds it again from scratch, so
# nothing here is load-bearing except the source changes.
#
#
# WHAT THIS DOES, AND WHY EACH PIECE IS NEEDED
#
# Two files still hold a key the way 1.0.2 did.  CpriECC.c builds a deprecated
# EC key object on the stack -- new it, hand it a group, hand it a scalar or a
# point -- and signs with the deprecated one-shot ECDSA entry points.
# CpriRSA.c does not use libcrypto's RSA object at all: it does modular
# exponentiation on BIGNUMs and writes OAEP, PKCS#1 v1.5, PSS and the PKCS#1
# signature encoding out by hand, MGF1 and all.
#
# Neither is a key a provider can see.  A key built with the deprecated object
# API has no library context; a key that is four byte strings in a TPM struct
# is not a key at all.  Step 3 gave this engine a context of its own and loaded
# a provider into it, and step 4 moved the symmetric work onto ciphers fetched
# from it.  The asymmetric work is what is left, and until it moves there is
# nothing for step 6's FIPS module to be asked about: you cannot put a stack
# object or a hand-written mask into FIPS mode.
#
# Four pieces:
#
#   the key          EVP_PKEY_fromdata against an OSSL_PARAM list, on a
#                    context from EVP_PKEY_CTX_new_from_name(
#                    OsslLibraryContext(), ...).  Two builders, one per
#                    algorithm: RsaPublicKey() in CpriRSA.c from the modulus
#                    and the exponent, EccKeyFromData() in CpriECC.c from
#                    CpriDataEcc.h's explicit curve parameters plus a scalar,
#                    a point, or both.  A key belongs to the call it was
#                    built for and is freed before that call returns -- there
#                    is nothing to cache, because the key arrives in the
#                    caller's argument list every time.
#
#   RSA's public     _cpri__EncryptRSA() for RSAES and OAEP becomes
#   operations       EVP_PKEY_encrypt() with the padding named as a parameter;
#                    _cpri__ValidateSignatureRSA() becomes EVP_PKEY_verify().
#                    Four encoders go with them: OaepEncode,
#                    RSAES_PKSC1v1_5Encode, PssDecode and RSASSA_Decode.
#
#   ECDSA            SignEcdsa() becomes EVP_PKEY_sign() and
#                    ValidateSignatureEcdsa() becomes EVP_PKEY_verify(), over
#                    keys from EccKeyFromData().  The deprecated key object
#                    and the deprecated one-shot signing entry points leave
#                    the tree entirely.
#
#   OsslHashMd()     the one new line of plumbing.  EVP wants an EVP_MD * for
#                    "which hash is this OAEP label hashed with", and the only
#                    EVP_MD objects this engine may use are the ones step 3
#                    fetched from its own context into CpriHash.c's table.
#                    This hands them out by algorithm id.
#
#
# WHAT DOES NOT MOVE, AND WHY THAT IS THE POINT
#
# The four encoders that stay are not an oversight.  OAEP and PKCS#1 v1.5
# encryption padding is applied by whoever encrypts -- the public side -- and
# removed by whoever decrypts, which is the private side.  PSS and the PKCS#1
# signature encoding are applied by whoever signs -- the private side -- and
# removed by whoever verifies.  So each encoder this step deletes is the public
# half of a scheme, and the one beside it on the private side stays:
#
#     deleted (public)            stays (private)
#     OaepEncode                  OaepDecode
#     RSAES_PKSC1v1_5Encode       RSAES_Decode
#     PssDecode                   PssEncode
#     RSASSA_Decode               RSASSA_Encode
#
# _cpri__DecryptRSA() and _cpri__SignRSA() are not touched at all, and neither
# is RSADP.  Step 2 made the private exponentiation constant time -- BN_FLG_
# CONSTTIME and BN_mod_exp_mont_consttime, set in RSADP and in _math__ModExp --
# and it made it so on a BIGNUM the engine owns.  Handing the private key to
# EVP would hand that guarantee to a provider, which is the right answer for a
# real TPM and the wrong answer for this ladder: it would un-assert what step 2
# asserts, on the same tree, two steps later.  Step 9 is where the private key
# becomes opaque, and this step is what makes that possible without making it
# compulsory.  The same reasoning keeps the raw TPM_ALG_NULL path in
# _cpri__EncryptRSA() on RSAEP: raw is not padding, and there is no padding
# scheme to name in a parameter.
#
#
# THE BEHAVIOUR THAT HAS TO SURVIVE, WHICH IS WHERE THE WORK IS
#
# 1676 engine cases and 1048 end-to-end cases were recorded against this
# engine's answers at step 1 and step 2, and they are replayed unchanged.  Four
# of those answers are only reproducible if a guard that used to live inside a
# deleted encoder is hoisted to the entry point, with the same types:
#
#   OAEP with SHA-512 answers CRYPT_PARAMETER, not "message too long".
#         This Implementation.h does not build SHA-512, so
#         _cpri__GetDigestSize() returns 0, and OaepEncode tested `hLen <= 0`
#         BEFORE it tested the message length.  Both key sizes in the corpus
#         record -3 because of that ordering.  hLen must stay INT32 and the
#         length test must stay `dInSize > *cOutSize - 2 * hLen - 2`: the
#         mixed INT32/UINT32 arithmetic is part of the recorded answer.
#
#   RSAES answers CRYPT_PARAMETER for a message that does not fit.
#         `dInSize > *cOutSize - 11`, unsigned on both sides, exactly as
#         RSAES_PKSC1v1_5Encode had it.  RSAES_PKSC1v1_5Encode had no
#         pAssert and none is added.
#
#   RSASSA answers CRYPT_SCHEME for a digest of the wrong length.
#         RSASSA_Decode refused `derSize == 0 || (unsigned)hashSize !=
#         hInSize` before it looked at a byte.  EVP would happily build a
#         DigestInfo of whatever length it was given and report a bad
#         signature instead, which is CRYPT_FAIL and not what is recorded.
#
#   A signature numerically >= the modulus answers CRYPT_FAIL.
#         The old code got that from RSAEP: _math__ModExp returns
#         CRYPT_PARAMETER when the base is not less than the modulus, and
#         _cpri__ValidateSignatureRSA turned any non-success into CRYPT_FAIL.
#         Reading ModExp() through shows that is the ONLY non-fatal answer it
#         has other than success -- CRYPT_UNDERFLOW cannot happen when the
#         output buffer is the modulus size -- so the same answer comes from
#         `_math__uComp(sigInSize, sigIn, key->publicKey->size,
#         key->publicKey->buffer) >= 0`, with no exponentiation at all, and it
#         comes out the same for TPM_ALG_NULL, which returned CRYPT_SCHEME
#         only after the decryption succeeded.
#
# And two that are about what is NOT set:
#
#   ECDSA truncates the digest itself.
#         SignEcdsa() truncates to the order's length and
#         ValidateSignatureEcdsa() truncates to the length of the signature's
#         own r, which is not the same rule and is the one the eccverify rows
#         recorded.  Both survive only because neither EVP_PKEY_CTX is given a
#         signature md: the provider rejects a tbs that is not exactly mdsize
#         when one is set, and accepts whatever length it is handed when none
#         is.
#
#   RSAPSS accepts saltSize 0 as "whatever the signature says".
#         That is RSA_PSS_SALTLEN_AUTO.  A non-zero saltSize maps to itself,
#         which is what PssDecode's `saltSize != 0 && i != saltSize` did.
#
# Two more that are safe by inspection rather than by arrangement: the OAEP
# label OpenSSL is given is `label` plus its terminating NUL, because
# OaepEncode hashed strlen(label) + 1 bytes; and the randomised padding in
# RSAES and OAEP is unobservable, because the rsapad family round-trips
# through OaepDecode and RSAES_Decode and never records a ciphertext.
#
#
# THE ONE THING THAT IS NOT OBVIOUS ABOUT THE EC PARAMETERS
#
# The curves come out of CpriDataEcc.h as explicit parameters -- p, a, b, the
# generator, the order, the cofactor -- and not as a name, because one of them
# has no name to use.  BN_P256 is the Barreto-Naehrig curve ECDAA needs and
# libcrypto has no NID for it.  A name-based route would work for the three
# NIST curves and fail on the fourth.  Explicit parameters work for all four,
# and EC_GROUP_new_from_params() recognises the NIST curves from their
# parameters and substitutes the named group anyway, so nothing is lost by
# asking this way.
#
# The octet strings for the generator and the public point have to stay alive
# until OSSL_PARAM_BLD_to_param() runs: the builder keeps the pointer and
# copies at conversion time.  Both live at function scope in EccKeyFromData()
# for that reason and no other.

set -euo pipefail

APP=${APP:-/app}
cd "$APP"

say()  { printf '\n== %s\n' "$*"; }
die()  { printf 'solve: %s\n' "$*" >&2; exit 1; }

for f in src/OsslCryptoEngine/CpriRSA.c src/OsslCryptoEngine/CpriECC.c \
         src/OsslCryptoEngine/CpriHash.c \
         src/OsslCryptoEngine/OsslCryptoEngine.h \
         src/include/CpriDataEcc.h cmake/CMakeLists.txt; do
    [ -f "$f" ] || die "$f is missing -- this is not the tree step 4 produced"
done

# Step 4's work has to be here already: this step reads the md objects out of
# CpriHash.c's fetch table and leaves the cipher table alone, and a tree
# without either is a tree the previous step did not run on.
grep -q 's_symServer' src/OsslCryptoEngine/CpriSym.c \
    || die "src/OsslCryptoEngine/CpriSym.c has no fetched cipher table -- step 4 has not been applied"
grep -q 'EVP_MD_fetch' src/OsslCryptoEngine/CpriHash.c \
    || die "src/OsslCryptoEngine/CpriHash.c does not fetch -- step 3 has not been applied"

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


def region(path, start, end, repl):
    """Replace start..end inclusive, once.  Used where a whole section of a
    file is being rewritten and quoting every line of it as an anchor would
    be less readable than naming its two ends."""
    global EDITS
    src = open(path).read()
    spat = r"\s+".join(re.escape(t) for t in start.split())
    epat = r"\s+".join(re.escape(t) for t in end.split())
    smatch = list(re.finditer(spat, src))
    ematch = list(re.finditer(epat, src))
    if len(smatch) != 1 or len(ematch) != 1:
        sys.exit("solve: %s: region ends matched %d/%d time(s), expected 1/1"
                 % (path, len(smatch), len(ematch)))
    if ematch[0].end() <= smatch[0].start():
        sys.exit("solve: %s: region end precedes its start" % path)
    open(path, "w").write(src[:smatch[0].start()] + repl + src[ematch[0].end():])
    EDITS += 1


# ================================================ the md objects, lent outward
#
# CpriRSA.c needs to name a hash to EVP, and the only EVP_MD objects this
# engine is allowed to name are the ones _cpri__HashStartup() fetched from
# OsslLibraryContext() into s_hashServer[].  GetHashServer() cannot be reused
# for this: it ends in FAIL(FATAL_ERROR_INTERNAL) for an algorithm the table
# does not carry, and the RSA callers need to answer CRYPT_SCHEME for exactly
# those algorithms rather than take the TPM down.  So this is the same lookup
# with the failure replaced by NULL, next to the table it reads.
patch("src/OsslCryptoEngine/CpriHash.c", [

    ("""FAIL(FATAL_ERROR_INTERNAL);
}

// B.8.3.2.  SeqPublish()""",
     """FAIL(FATAL_ERROR_INTERNAL);
}

// The md objects of the table above, by algorithm id, for the asymmetric code
// next door.  Every EVP_MD this engine uses was fetched once from its own
// library context and belongs to this file; what leaves here is a loan, so
// there is nothing for the caller to free.
//
// Unlike GetHashServer() this answers NULL for an algorithm the table does not
// carry rather than putting the TPM in failure mode.  Its callers reach it
// through _cpri__GetDigestSize(), which answers 0 for exactly the algorithms
// this Implementation.h leaves out, and they have already returned
// CRYPT_SCHEME by the time a NULL could reach EVP.
EVP_MD     *
OsslHashMd(
     TPM_ALG_ID         hashAlg           //  IN:   the   algorithm      to  look   up
)
{
     UINT32       i;

     if(hashAlg        ==     TPM_ALG_NULL)
            return     NULL;
     for(i     =  0;   i  <  HASH_SERVER_COUNT;    i++)
     {
            if(s_hashServer[i].alg              ==  hashAlg)
                   return      s_hashServer[i].md;
     }
     return      NULL;
}

// B.8.3.2.  SeqPublish()""", 1),
])

# Declared beside the other two things this header lends between the engine's
# own files.  EVP_MD and TPM_ALG_ID are both already in scope here.
patch("src/OsslCryptoEngine/OsslCryptoEngine.h", [

    ("""void               OsslSymCleanup(void);""",
     """void               OsslSymCleanup(void);
EVP_MD            *OsslHashMd(TPM_ALG_ID hashAlg);""", 1),
])

# ============================================================== CpriRSA.c ====
#
# core_names.h for the parameter names, param_build.h for the builder, rsa.h
# for the padding identifiers and the padding-specific ctx setters.  rsa.h is
# NOT pulled in for the low-level RSA object -- that object is what this step
# closes the door on, and the verifier reads the import table to prove it.
patch("src/OsslCryptoEngine/CpriRSA.c", [

    ("""#include   "OsslCryptoEngine.h"
#ifdef     TPM_ALG_RSA""",
     """#include   "OsslCryptoEngine.h"
#ifdef     TPM_ALG_RSA
#include   <openssl/core_names.h>
#include   <openssl/param_build.h>
#include   <openssl/rsa.h>""", 1),
])

# --- the four encoders that belong to the public side, deleted.
#
# Each region runs from the deleted function's own banner to the banner of the
# next function, which is re-emitted unchanged: region() replaces start..end
# inclusive, so the closing anchor has to come back out in the replacement.

region("src/OsslCryptoEngine/CpriRSA.c",
       "// B.12.1.3.5.   OaepEncode()",
       "// B.12.1.3.6.       OaepDecode()",
       """// B.12.1.3.6.       OaepDecode()""")

region("src/OsslCryptoEngine/CpriRSA.c",
       "// B.12.1.3.7.   PKSC1v1_5Encode()",
       "// B.12.1.3.8.   RSAES_Decode()",
       """// B.12.1.3.8.   RSAES_Decode()""")

region("src/OsslCryptoEngine/CpriRSA.c",
       "// B.12.1.3.10. PssDecode()",
       "// B.12.1.3.11. PKSC1v1_5SignEncode()",
       """// B.12.1.3.11. PKSC1v1_5SignEncode()""")

# --- RSASSA_Decode goes, and the key builder takes its place.
#
# It goes here, as the last static in the file, because both entry points below
# use it and nothing above does.
region("src/OsslCryptoEngine/CpriRSA.c",
       "// B.12.1.3.12. RSASSA_Decode()",
       "// B.12.1.4.    Externally Accessible Functions",
       """// The public key as something EVP will operate on.

// The engine is handed a modulus and an exponent and has to produce a key
// object.  There are two ways to do that and only one of them reaches a
// provider: an OSSL_PARAM list and EVP_PKEY_fromdata, against a context made
// from this engine's own library context.  The other way -- the low-level RSA
// object and its setters -- is deprecated at 3.0, has no library context, and
// produces a key that no provider, property query or FIPS module can see.

// The exponent is a TPM UINT32, zero meaning the default, and goes in as the
// four bytes UINT32_TO_BYTE_ARRAY makes of it.  The caller owns what comes
// back and frees it.

// Returns NULL if the key could not be built, which for this engine's callers
// is a fatal internal error and not a scheme they can decline.
static    EVP_PKEY      *
RsaPublicKey(
      RSA_KEY               *key                      //  IN:   the   public     key   to    build    from
      )
{
      EVP_PKEY              *pkey     =   NULL;
      EVP_PKEY_CTX          *ctx      =   NULL;
      OSSL_PARAM_BLD        *bld      =   NULL;
      OSSL_PARAM            *params   =   NULL;
      BIGNUM                *bnN      =   NULL;
      BIGNUM                *bnE      =   NULL;
      UINT32                 e;
      BYTE                   exponent[4];

      e   =   key->exponent;
      if(e     ==   0)
            e   =   RSA_DEFAULT_PUBLIC_EXPONENT;
      UINT32_TO_BYTE_ARRAY(e,        exponent);

      if(        (bld    =  OSSL_PARAM_BLD_new())      ==   NULL
              || (bnN    =  BN_bin2bn(key->publicKey->buffer,
                                      key->publicKey->size,      NULL))    ==   NULL
              || (bnE    =  BN_bin2bn(exponent,        sizeof(exponent),    NULL))    ==   NULL
              || !OSSL_PARAM_BLD_push_BN(bld,          OSSL_PKEY_PARAM_RSA_N,       bnN)
              || !OSSL_PARAM_BLD_push_BN(bld,          OSSL_PKEY_PARAM_RSA_E,       bnE)
              || (params =  OSSL_PARAM_BLD_to_param(bld))       ==   NULL
              || (ctx    =  EVP_PKEY_CTX_new_from_name(OsslLibraryContext(),
                                                       "RSA",    NULL))    ==   NULL
              || EVP_PKEY_fromdata_init(ctx)           <=   0
              || EVP_PKEY_fromdata(ctx,     &pkey,     EVP_PKEY_PUBLIC_KEY,
                                   params)             <=   0)
      {
            //  fromdata      can    leave     a   half     built     key   behind
            EVP_PKEY_free(pkey);
            pkey    =   NULL;
      }
      EVP_PKEY_CTX_free(ctx);
      OSSL_PARAM_free(params);
      OSSL_PARAM_BLD_free(bld);
      BN_free(bnN);
      BN_free(bnE);
      return    pkey;
}

// B.12.1.4.    Externally Accessible Functions""")

# --- _cpri__EncryptRSA(): the padding is named, not written.
#
# The size and scheme answers this function has always given are decided before
# any EVP call, because three of them used to be decided inside the encoders
# that have just been deleted and the recorded answers depend on where.  See
# the header comment: OAEP's `hLen <= 0` has to be tested before its length
# test, and the length tests have to keep their original mixed INT32/UINT32
# arithmetic.
region("src/OsslCryptoEngine/CpriRSA.c",
       "// B.12.1.4.2.      _cpri__EncryptRSA()",
       "// B.12.1.4.3.     _cpri__DecryptRSA()",
       """// B.12.1.4.2.      _cpri__EncryptRSA()

// This is the entry point for encryption using RSA. Encryption is use of the public exponent. The padding

// parameter determines what padding will be used.

// The cOutSize parameter must be at least as large as the size of the key.

// If the padding is RSA_PAD_NONE, dIn is treaded as a number. It must be lower in value than the key

// modulus.

// NOTE:            If dIn has fewer bytes than cOut, then we don't add low-order zeros to dIn to make it the size of
// the RSA key for

// the call to RSAEP. This is because the high order bytes of dIn might have a numeric value that is greater than

// the value of the key modulus. If this had low-order zeros added, it would have a numeric value larger than the

// modulus even though it started out with a lower numeric value.

// Return Value                                 Meaning

// CRYPT_SUCCESS                                encryption complete

// CRYPT_PARAMETER                              cOutSize is too small (must be the size of the modulus)

// CRYPT_SCHEME                                 padType is not a supported scheme
LIB_EXPORT    CRYPT_RESULT
_cpri__EncryptRSA(
   UINT32                    *cOutSize,                         //  OUT:     the      size     of   the  encrypted        data
   BYTE                      *cOut,                             //  OUT:     the      encrypted         data
   RSA_KEY                   *key,                              //  IN:      the  key      to   use     for   encryption
   TPM_ALG_ID                     padType,                      //  IN:      the  type      of     padding
   UINT32                         dInSize,                      //  IN:      the  amount        of   data     to   encrypt
   BYTE                      *dIn,                              //  IN:      the  data      to     encrypt
   TPM_ALG_ID                     hashAlg,                      //  IN:      in   case     this     is   needed
   const     char            *label                             //  IN:      in   case     it   is   needed
   )
{
   EVP_PKEY                  *pkey          =     NULL;
   EVP_PKEY_CTX              *ctx           =     NULL;
   EVP_MD                    *md;
   BYTE                      *oaepLabel;
   size_t                     outLen;
   size_t                     lSize;
   INT32                      hLen;
   int                        padding;
   int                        worked;

   pAssert(cOutSize               !=     NULL);

   //   All   encryption          schemes              return     the  same       size     of   data
   if(*cOutSize           <  key->publicKey->size)
         return       CRYPT_PARAMETER;
   *cOutSize       =     key->publicKey->size;

   //   Raw     is    not     padding.        There      is   no   scheme      to   name      to  a
   //   provider,         the   value       is   the    caller's         number       and    not     a
   //   message,          and    the      exponentiation            it   needs      is   the   one     this
   //   file    has     always        done.       It   stays      where      it  is.
   if(padType         ==     TPM_ALG_NULL)
   {
         //      dIn    can      have         more     bytes   than     cOut      as   long     as  the      extra   bytes
         //      are    zero
         for(;          dInSize         >     *cOutSize;       dInSize--)
         {
                    if(*dIn++           !=     0)
                        return             CRYPT_PARAMETER;

         }
         //      If     dIn  is     smaller            than    cOut,    fill      cOut     with     zeros
         if(dInSize              <  *cOutSize)
                    memset(cOut,               0,   *cOutSize        -  dInSize);

         //      Copy   the         rest       of   the    value
         memcpy(&cOut[*cOutSize-dInSize],                                   dIn,   dInSize);
         //      If     the  size          of     dIn  is    the  same       as   cOut     dIn     could     be  larger    than
         //      the    modulus.              If   it  is,     then     RSAEP()       will     catch     it.
         return      RSAEP(*cOutSize,             cOut,     key);
   }

   //   Everything          the    deleted        encoders         used      to  answer       for
   //   themselves,           answered          here     and   in   the    same     order,      because
   //   the     order      is    what     the     recorded         answers        record.
   switch     (padType)
   {
   case     TPM_ALG_RSAES:
         //      RSAES_PKSC1v1_5Encode()                    had    no   pAssert        and    this     has
         //      none.       Unsigned           on    both     sides,      as   it   was.
         if(dInSize            >   *cOutSize          -  11)
                    return             CRYPT_PARAMETER;
         padding       =   RSA_PKCS1_PADDING;
         break;
   case     TPM_ALG_OAEP:
         pAssert(cOut             !=     NULL        &&   dIn      !=   NULL);
         //      This       Implementation.h                does     not    build      every       hash
         //      the      interface           can    name.      _cpri__GetDigestSize()                     is  0
         //      for      the     ones     it   leaves       out,     and     OaepEncode()               tested
         //      that       BEFORE        it   tested      the    message         length.        SHA-512
         //      records         CRYPT_PARAMETER                   on   both     key     sizes      because
         //      of     that      ordering            and    of    the     unsigned          wrap     below.
         hLen      =   _cpri__GetDigestSize(hashAlg);
         if(hLen         <=   0)
                    return             CRYPT_PARAMETER;
         if(dInSize            >   *cOutSize          -  2   *  hLen      -   2)
                    return             CRYPT_FAIL;
         padding       =   RSA_PKCS1_OAEP_PADDING;
         break;
   default:
         return      CRYPT_SCHEME;
   }

   //   From    here     down       the     only     way     to  fail     is   a   way    this
   //   interface          has    no    code     for  --    out     of  memory,       or   a
   //   provider          that    cannot        do   RSA  --    which      is  what
   //   FATAL_ERROR_INTERNAL                      is   for.
   if(        (pkey        =   RsaPublicKey(key))            ==   NULL
           ||   (ctx     =   EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),
                                                        pkey,     NULL))     ==   NULL
           ||   EVP_PKEY_encrypt_init(ctx)                  <=   0
           ||   EVP_PKEY_CTX_set_rsa_padding(ctx,            padding)      <=   0)
           FAIL(FATAL_ERROR_INTERNAL);

   if(padType         ==     TPM_ALG_OAEP)
   {
         //      OaepEncode()               used    one     hash     for    the    label      and     the   same
         //      one     for    MGF1,      so    both     are    named      here.
         md      =   OsslHashMd(hashAlg);
         if(        md   ==   NULL
                 ||   EVP_PKEY_CTX_set_rsa_oaep_md(ctx,              md)     <=   0
                 ||   EVP_PKEY_CTX_set_rsa_mgf1_md(ctx,              md)     <=   0)
                 FAIL(FATAL_ERROR_INTERNAL);
         if(label            !=   NULL)
         {
                 //      OaepEncode()               hashed       strlen(label)             +  1   bytes    --
                 //      the      terminating             NUL     is   part      of   the    label      that   was
                 //      recorded,             so    it  is    part     of   the    label      given      here.
                 //      set0      takes     the    buffer      on   success        and    leaves      it   on
                 //      failure.
                 lSize        =   strlen(label)           +   1;
                 oaepLabel        =   OPENSSL_memdup(label,          lSize);
                 if(oaepLabel             ==   NULL)
                         FAIL(FATAL_ERROR_ALLOCATION);
                 if(EVP_PKEY_CTX_set0_rsa_oaep_label(ctx,             oaepLabel,
                                                     (int)lSize)      <=   0)
                 {
                         OPENSSL_free(oaepLabel);
                         FAIL(FATAL_ERROR_INTERNAL);
                 }
         }
   }

   //   The    ciphertext          is  always      the    size     of  the     modulus,        which     is
   //   what     *cOutSize          was    set     to  above      and    what    RSAEP()        would      have
   //   written.
   outLen      =   *cOutSize;
   worked      =   EVP_PKEY_encrypt(ctx,          cOut,      &outLen,      dIn,     dInSize);
   EVP_PKEY_CTX_free(ctx);
   EVP_PKEY_free(pkey);
   if(worked          <=   0    ||   outLen      !=   *cOutSize)
         FAIL(FATAL_ERROR_INTERNAL);
   return      CRYPT_SUCCESS;
}

// B.12.1.4.3.     _cpri__DecryptRSA()""")

# --- _cpri__ValidateSignatureRSA(): the verification is the library's.
#
# The two guards the deleted decoders owned are hoisted, and the range check
# RSAEP used to make is made directly.  #ifndef RSA_KEY_SIEVE closes the region
# and comes back out with the banner.
region("src/OsslCryptoEngine/CpriRSA.c",
       "// B.12.1.4.5.     _cpri__ValidateSignatureRSA()",
       "// B.12.1.4.6.     _cpri__GenerateKeyRSA()",
       """// B.12.1.4.5.     _cpri__ValidateSignatureRSA()

// This  function   is  used   to  validate  an   RSA        signature.  If  the   signature    is  valid
// CRYPT_SUCCESS    is

// returned.   If  the  signature  is  not   valid,  CRYPT_FAIL          is  returned.     Other    return  codes
// indicate  either

// parameter problems or fatal errors.

// Return Value                            Meaning

// CRYPT_SUCCESS                           the signature checks

// CRYPT_FAIL                              the signature does not check

// CRYPT_SCHEME                            unsupported scheme or hash algorithm
LIB_EXPORT      CRYPT_RESULT
_cpri__ValidateSignatureRSA(
      RSA_KEY               *key,                     //  IN:   key   to      use
      TPM_ALG_ID                scheme,               //  IN:   the   scheme       to   use
      TPM_ALG_ID                hashAlg,              //  IN:   hash      algorithm
      UINT32                    hInSize,              //  IN:   size      of   digest   to   be  checked
      BYTE                  *hIn,                     //  IN:   digest        buffer
      UINT32                    sigInSize,            //  IN:   size      of   signature
      BYTE                  *sigIn,                   //  IN:   signature
      UINT16                    saltSize              //  IN:   salt      size     for  PSS
      )
{
      EVP_PKEY              *pkey        =   NULL;
      EVP_PKEY_CTX          *ctx         =   NULL;
      EVP_MD                *md;
      BYTE                  *der;
      INT32                  derSize;
      INT32                  hashSize;
      int                    worked;

      //  Fatal      programming    errors
      pAssert(key       !=  NULL    &&  sigIn     !=  NULL  &&  hIn       !=   NULL);

      //  Errors     that   might   be   caused       by  calling     parameters
      if(sigInSize      !=  key->publicKey->size)
            return   CRYPT_FAIL;

      //  A    signature     that   is   not   less    than    the   modulus     used    to  be
      //  rejected     here     by   RSAEP:     _math__ModExp()        answers      CRYPT_PARAMETER
      //  for    a   base    that   is   not   below     the   modulus,      that    is   its   only
      //  non-fatal      answer      other    than    success      when     the   output     buffer     is
      //  the    modulus's      own   size,     and   this    function      turned     any
      //  non-success       into    CRYPT_FAIL.      The    comparison      is   the   same
      //  comparison,      made    without      the   exponentiation,        and   it   is   made
      //  before    the   scheme      switch     because      that   is   where    it  was.
      if(_math__uComp(sigInSize,          sigIn,
                      key->publicKey->size,          key->publicKey->buffer)      >=  0)
            return   CRYPT_FAIL;

      switch    (scheme)
      {
      case  TPM_ALG_NULL:
            return   CRYPT_SCHEME;
            break;
      case  TPM_ALG_RSAPSS:
            //  PssDecode()      declined      a   hash    this    build     does    not   have
            if(_cpri__GetDigestSize(hashAlg)            ==   0)
                  return   CRYPT_SCHEME;
            break;
      case  TPM_ALG_RSASSA:
            //  RSASSA_Decode()       declined      a   hash    with    no   DER    string     and
            //  a   digest    whose     length     is   not   the   hash's     own,    before
            //  it   looked     at   a   byte.     EVP    would     build     a   DigestInfo      of
            //  whatever      length     it  was    given     and   report     a   bad
            //  signature      instead,      which    is   CRYPT_FAIL       and   not   what    was
            //  recorded.
            derSize     =  _cpri__GetHashDER(hashAlg,          (const     BYTE     **)&der);
            hashSize    =  _cpri__GetDigestSize(hashAlg);
            if(derSize      ==  0     ||  (unsigned)hashSize          !=   hInSize)
                  return   CRYPT_SCHEME;
            break;
      default:
            return   CRYPT_SCHEME;
      }

      //  The    scheme      is   supported       and   the   arguments       are   in   range,     so
      //  everything      from     here    down    is   the   library's        work     and   any
      //  failure     of   it   is   internal.
      md    =  OsslHashMd(hashAlg);
      if(        (pkey     =  RsaPublicKey(key))           ==  NULL
              || (ctx    =  EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),
                                                       pkey,    NULL))     ==  NULL
              || EVP_PKEY_verify_init(ctx)               <=  0
              || md    ==  NULL
              || EVP_PKEY_CTX_set_signature_md(ctx,          md)     <=  0)
              FAIL(FATAL_ERROR_INTERNAL);

      if(scheme     ==  TPM_ALG_RSAPSS)
      {
            //  saltSize      0   meant     "whatever       the   signature       says"     to
            //  PssDecode(),      which     only    compared      a   recovered       salt
            //  length     when    it   had   been    told    one.     That    is
            //  RSA_PSS_SALTLEN_AUTO.        A   non-zero      saltSize      maps    to
            //  itself.      MGF1    used    the   signature       hash,    as   it  did.
            if(        EVP_PKEY_CTX_set_rsa_padding(ctx,            RSA_PKCS1_PSS_PADDING)      <=  0
                    || EVP_PKEY_CTX_set_rsa_mgf1_md(ctx,            md)     <=  0
                    || EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx,
                               (saltSize     !=  0)  ?  (int)saltSize
                                                     :  RSA_PSS_SALTLEN_AUTO)       <=  0)
                    FAIL(FATAL_ERROR_INTERNAL);
      }
      else if(EVP_PKEY_CTX_set_rsa_padding(ctx,           RSA_PKCS1_PADDING)      <=  0)
            FAIL(FATAL_ERROR_INTERNAL);

      worked      =  EVP_PKEY_verify(ctx,        sigIn,     sigInSize,      hIn,    hInSize);
      EVP_PKEY_CTX_free(ctx);
      EVP_PKEY_free(pkey);
      //  1   is   a   signature      that   checks;     0   is   one    that   does    not,    and
      //  so   is   anything       else,    because     this    function      has   no   other
      //  answer     for   a   signature      it  could     not   check.
      return    (worked     ==  1)  ?  CRYPT_SUCCESS      :  CRYPT_FAIL;
}
#ifndef     RSA_KEY_SIEVE

// B.12.1.4.6.     _cpri__GenerateKeyRSA()""")

# ============================================================== CpriECC.c ====
#
# obj_mac.h is here for one string: SN_X9_62_prime_field, which is what the
# field-type parameter has to say for a prime curve.
patch("src/OsslCryptoEngine/CpriECC.c", [

    ("""#include       "OsslCryptoEngine.h"
#ifdef     TPM_ALG_ECC
#include       "CpriDataEcc.h\"""",
     """#include       "OsslCryptoEngine.h"
#ifdef     TPM_ALG_ECC
#include       <openssl/core_names.h>
#include       <openssl/param_build.h>
#include       <openssl/obj_mac.h>
#include       "CpriDataEcc.h\"""", 1),
])

# --- SignEcdsa(), and the EC key builder both ECDSA functions use.
region("src/OsslCryptoEngine/CpriECC.c",
       "// B.13.3.2.17. SignEcdsa()",
       "// B.13.3.2.18. EcDaa()",
       """// A point as the uncompressed octet string an OSSL_PARAM wants: 0x04, then x
// and y, each left padded to the field size.  The engine's TPM2Bs are minimal
// length and this encoding is fixed length; filling in the difference is all
// this does.
static    BOOL
EccPoint2Oct(
    BYTE                                     *out,                     //  OUT:  1 + 2 * fieldSize  bytes
    UINT32                                     fieldSize,              //  IN:   bytes  per  coordinate
    const     TPM2B                          *x,                       //  IN:   the  x  coordinate
    const     TPM2B                          *y                        //  IN:   the  y  coordinate
    )
{
    if(x->size    >   fieldSize    ||   y->size    >   fieldSize)
            return     FALSE;
    memset(out,     0,    1   +   (2   *   fieldSize));
    out[0]   =   0x04;
    memcpy(&out[1   +   fieldSize   -   x->size],     x->buffer,    x->size);
    memcpy(&out[1   +   (2   *   fieldSize)   -   y->size],     y->buffer,    y->size);
    return     TRUE;
}

// An EVP_PKEY over one of this file's curves, built from parameters.

// The curves arrive as explicit parameters and are given to EVP as explicit
// parameters, not as a curve name, because one of them has no name to use:
// BN_P256 is the curve ECDAA needs and libcrypto has no NID for it.  A
// name-based route would work for the NIST curves and fail on that one.
// EC_GROUP_new_from_params() recognises the NIST curves from their parameters
// and substitutes the named group anyway, so nothing is given up by asking
// this way for all of them.

// dIn is the private scalar and Qin the public point; either may be NULL.
// Signing has only the scalar -- SignEcdsa() is not given a public point --
// and verifying has only the point.

// The two octet strings live at function scope because the parameter builder
// keeps the pointer it is given and copies at OSSL_PARAM_BLD_to_param() time.

// Returns NULL if the key could not be built, which includes a point that is
// not on the curve: the sequence this replaces made that a fatal internal
// error and so do this function's callers.
static    EVP_PKEY      *
EccKeyFromData(
    const     ECC_CURVE_DATA                 *curveData,               //  IN:  the  curve
    const     TPM2B                          *dIn,                     //  IN:  the  private  scalar,  or  NULL
    TPMS_ECC_POINT                           *Qin                      //  IN:  the  public  point,  or  NULL
    )
{
    EVP_PKEY                              *pkey     =   NULL;
    EVP_PKEY_CTX                          *ctx      =   NULL;
    OSSL_PARAM_BLD                        *bld      =   NULL;
    OSSL_PARAM                            *params   =   NULL;
    BIGNUM                                *bnP      =   NULL;
    BIGNUM                                *bnA      =   NULL;
    BIGNUM                                *bnB      =   NULL;
    BIGNUM                                *bnN      =   NULL;
    BIGNUM                                *bnH      =   NULL;
    BIGNUM                                *bnD      =   NULL;
    BYTE                                   gen[1   +   (2   *   MAX_ECC_KEY_BYTES)];
    BYTE                                   pub[1   +   (2   *   MAX_ECC_KEY_BYTES)];
    UINT32                                 fieldSize    =   curveData->p->size;
    UINT32                                 pointSize    =   1   +   (2   *   fieldSize);
    int                                    selection    =   EVP_PKEY_PUBLIC_KEY;

    if(        fieldSize    >   MAX_ECC_KEY_BYTES
            ||   !EccPoint2Oct(gen,     fieldSize,    curveData->x,    curveData->y))
            return     NULL;

    if(        (bld    =   OSSL_PARAM_BLD_new())    ==   NULL
            ||   (bnP   =   BN_bin2bn(curveData->p->buffer,     curveData->p->size,    NULL))   ==   NULL
            ||   (bnA   =   BN_bin2bn(curveData->a->buffer,     curveData->a->size,    NULL))   ==   NULL
            ||   (bnB   =   BN_bin2bn(curveData->b->buffer,     curveData->b->size,    NULL))   ==   NULL
            ||   (bnN   =   BN_bin2bn(curveData->n->buffer,     curveData->n->size,    NULL))   ==   NULL
            ||   (bnH   =   BN_bin2bn(curveData->h->buffer,     curveData->h->size,    NULL))   ==   NULL
            ||   !OSSL_PARAM_BLD_push_utf8_string(bld,      OSSL_PKEY_PARAM_EC_ENCODING,
                                                  OSSL_PKEY_EC_ENCODING_EXPLICIT,    0)
            ||   !OSSL_PARAM_BLD_push_utf8_string(bld,      OSSL_PKEY_PARAM_EC_FIELD_TYPE,
                                                  SN_X9_62_prime_field,    0)
            ||   !OSSL_PARAM_BLD_push_BN(bld,      OSSL_PKEY_PARAM_EC_P,     bnP)
            ||   !OSSL_PARAM_BLD_push_BN(bld,      OSSL_PKEY_PARAM_EC_A,     bnA)
            ||   !OSSL_PARAM_BLD_push_BN(bld,      OSSL_PKEY_PARAM_EC_B,     bnB)
            ||   !OSSL_PARAM_BLD_push_BN(bld,      OSSL_PKEY_PARAM_EC_ORDER,     bnN)
            ||   !OSSL_PARAM_BLD_push_BN(bld,      OSSL_PKEY_PARAM_EC_COFACTOR,     bnH)
            ||   !OSSL_PARAM_BLD_push_octet_string(bld,     OSSL_PKEY_PARAM_EC_GENERATOR,
                                                   gen,     pointSize))
            goto     Done;

    if(Qin    !=   NULL)
    {
            if(        !EccPoint2Oct(pub,     fieldSize,    &Qin->x.b,    &Qin->y.b)
                    ||   !OSSL_PARAM_BLD_push_octet_string(bld,     OSSL_PKEY_PARAM_PUB_KEY,
                                                           pub,     pointSize))
                    goto     Done;
    }
    if(dIn    !=   NULL)
    {
            selection   =   EVP_PKEY_KEYPAIR;
            if(        (bnD   =   BN_bin2bn(dIn->buffer,     dIn->size,    NULL))   ==   NULL
                    ||   !OSSL_PARAM_BLD_push_BN(bld,      OSSL_PKEY_PARAM_PRIV_KEY,     bnD))
                    goto     Done;
    }

    if(        (params  =   OSSL_PARAM_BLD_to_param(bld))    ==   NULL
            ||   (ctx   =   EVP_PKEY_CTX_new_from_name(OsslLibraryContext(),
                                                       "EC",     NULL))   ==   NULL
            ||   EVP_PKEY_fromdata_init(ctx)    <=   0
            ||   EVP_PKEY_fromdata(ctx,     &pkey,    selection,    params)    <=   0)
    {
            //   fromdata  can  leave  a  half  built  key  behind
            EVP_PKEY_free(pkey);
            pkey   =   NULL;
    }
 Done:
    EVP_PKEY_CTX_free(ctx);
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    BN_free(bnP);
    BN_free(bnA);
    BN_free(bnB);
    BN_free(bnN);
    BN_free(bnH);
    BN_clear_free(bnD);
    return     pkey;
}

// B.13.3.2.17. SignEcdsa()

// This function implements the ECDSA signing algorithm. The method is described in the comments below.

// It is a fatal error if rOut, sOut, dIn, or digest are not provided.
LIB_EXPORT    CRYPT_RESULT
SignEcdsa(
   TPM2B_ECC_PARAMETER                    *rOut,                  //   OUT:   r   component    of  the      signature
   TPM2B_ECC_PARAMETER                    *sOut,                  //   OUT:   s   component    of  the      signature
   TPM_ECC_CURVE                            curveId,              //   IN:   the   curve  used     in  the  signature
                                                                  //         process
   TPM2B_ECC_PARAMETER                    *dIn,                   //   IN:   the   private    key
   TPM2B                                  *digest                 //   IN:   the   value  to   sign
   )
{
    EVP_PKEY                              *pkey    =   NULL;
    EVP_PKEY_CTX                          *ctx     =   NULL;
    ECDSA_SIG                             *sig     =   NULL;
    const     BIGNUM                      *bnR;
    const     BIGNUM                      *bnS;
    const     unsigned      char          *pder;
    BYTE                                   der[(2   *   MAX_ECC_KEY_BYTES)   +   32];
    size_t                                 derSize  =   sizeof(der);
    int                                    dLen;
    const     ECC_CURVE_DATA              *curveData    =   GetCurveData(curveId);

    pAssert(rOut    !=   NULL    &&   sOut    !=   NULL    &&   dIn    !=   NULL
            &&    digest    !=   NULL);
    //   Assume  the  size  variables  do  not  overflow, which  should  not
    //   happen  in  the  contexts  that  this  function  will  be  called.
    pAssert(curveData    !=   NULL
            &&    curveData->n->size    <=   MAX_ECC_PARAMETER_BYTES);
    assert2Bsize(dIn->t);

    //   The  digest  is  used  left  to  right  and  no  wider  than  the
    //   order.  Every  order  this  implementation  carries  is  a  whole
    //   number  of  bytes,  so  truncating  to  n->size  bytes  here  is  the
    //   same  truncation  the  deprecated  one-shot  signer  would  have  made
    //   for  itself  from  the  bit  length  of  the  order.  It  is  made
    //   here  now  because  it  has  to  be:  the  operation  below  is  given
    //   no  signature  md,  so  the  provider  signs  exactly  the  bytes  it
    //   is  handed  and  truncates  nothing.
    dLen   =   (digest->size    >   curveData->n->size)
                   ?   (int)curveData->n->size   :   (int)digest->size;

    //   The  key  is  the  caller's  scalar  over  the  caller's  curve,
    //   built  in  this  engine's  library  context  so  that  what  signs
    //   with  it  is  a  provider  this  engine  chose.  libcrypto  still
    //   draws  the  per-message  secret  k,  inverts  it,  retries  on  a
    //   zero  r  or  s  and  erases  it;  that  work  never  belonged  here.
    if(        (pkey   =   EccKeyFromData(curveData,     &dIn->b,    NULL))   ==   NULL
            ||   (ctx   =   EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),
                                                       pkey,    NULL))   ==   NULL
            ||   EVP_PKEY_sign_init(ctx)    <=   0
            ||   EVP_PKEY_sign(ctx,     der,    &derSize,
                               digest->buffer,     (size_t)dLen)    <=   0)
            FAIL(FATAL_ERROR_INTERNAL);

    //   EVP_PKEY_sign()  hands  back  the  DER  SEQUENCE.  r  and  s  are
    //   what  this  interface  returns  and  ECDSA_SIG  is  the  supported
    //   way  to  get  at  them  --  it  is  not  deprecated  at  3.0  and  is
    //   not  part  of  what  this  step  removes.
    pder   =   der;
    if((sig   =   d2i_ECDSA_SIG(NULL,     &pder,    (long)derSize))   ==   NULL)
            FAIL(FATAL_ERROR_INTERNAL);

    //   ECDSA_SIG_get0()  lends  them  out  and  ECDSA_SIG_free()  takes
    //   them  with  it.
    ECDSA_SIG_get0(sig,      &bnR,     &bnS);
    rOut->t.size   =   curveData->n->size;
    sOut->t.size   =   curveData->n->size;
    if(        BN_bn2binpad(bnR,      rOut->t.buffer,     rOut->t.size)    <   0
            ||   BN_bn2binpad(bnS,      sOut->t.buffer,     sOut->t.size)    <   0)
            FAIL(FATAL_ERROR_INTERNAL);

    ECDSA_SIG_free(sig);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return     CRYPT_SUCCESS;
}
#endif      //%
#if  defined        TPM_ALG_ECDAA           ||  defined       TPM_ALG_ECSCHNORR               //%

// B.13.3.2.18. EcDaa()""")

# --- ValidateSignatureEcdsa().
#
# The range checks stay first and stay as they were: they are what answers
# CRYPT_FAIL for an r or s that is not below the order, and they run before
# anything that could be fatal.  The truncation to the length of r stays too --
# it is not the same rule as the signer's, and it is the one the recorded
# answers recorded.
region("src/OsslCryptoEngine/CpriECC.c",
       "// B.13.3.2.22. ValidateSignatureEcdsa()",
       "// B.13.3.2.23. ValidateSignatureEcSchnorr()",
       """// B.13.3.2.22. ValidateSignatureEcdsa()

// This function validates an ECDSA signature. rIn and sIn shoudl have             been      checked to make sure that

// they are not zero.

// Return Value                             Meaning

// CRYPT_SUCCESS                            signature valid

// CRYPT_FAIL                               signature not valid
static     CRYPT_RESULT
ValidateSignatureEcdsa(
    TPM2B_ECC_PARAMETER                      *rIn,                     //  IN:  r  component      of    the     signature
    TPM2B_ECC_PARAMETER                      *sIn,                     //  IN:  s  component      of    the     signature
    TPM_ECC_CURVE                              curveId,                //  IN:  the    curve   used     in   the  signature
                                                                       //       process
    TPMS_ECC_POINT                           *Qin,                     //  IN:  the    public     point     of  the   key
    TPM2B                                    *digest                   //  IN:  the    digest     that   was    signed
    )
{
    const     TPM2B                       *n;
    EVP_PKEY                              *pkey     =   NULL;
    EVP_PKEY_CTX                          *ctx      =   NULL;
    ECDSA_SIG                             *sig      =   NULL;
    BIGNUM                                *bnR      =   NULL;
    BIGNUM                                *bnS      =   NULL;
    BYTE                                   der[(2   *   MAX_ECC_KEY_BYTES)   +   32];
    unsigned      char                    *pder;
    int                                    derSize;
    CRYPT_RESULT                           retVal   =   CRYPT_FAIL;
    int                                    t;
    const     ECC_CURVE_DATA              *curveData    =   GetCurveData(curveId);

    //   The  curve  selector  should  have  been  filtered  by  the
    //   unmarshaling  process
    pAssert    (curveData    !=   NULL);
    n   =   curveData->n;

//  1.  If  r  and  s  are  not  both  integers  in  the  interval  [1, n - 1],
//      output  INVALID.
//  rIn  and  sIn  are  known  to  be  greater  than  zero  (was  checked  by
//  the  caller).
//  This  stays  first  and  stays  arithmetic:  it  is  the  only  thing  in
//  this  function  that  answers  CRYPT_FAIL  for  a  malformed  signature
//  rather  than  for  one  that  does  not  check,  and  everything  after  it
//  can  be  fatal.
    if(        _math__uComp(rIn->t.size,     rIn->t.buffer,     n->size,     n->buffer)    >=   0
            ||   _math__uComp(sIn->t.size,     sIn->t.buffer,     n->size,     n->buffer)    >=   0
        )
            return     CRYPT_FAIL;

    //   Assume  the  size  variables  do  not  overflow, which  should  not
    //   happen  in  the  contexts  that  this  function  will  be  called.
    assert2Bsize(Qin->x.t);
    assert2Bsize(rIn->t);
    assert2Bsize(sIn->t);

    //   r  and  s  become  a  DER  SEQUENCE,  which  is  what  the  provider
    //   verifies.  ECDSA_SIG_set0()  takes  ownership  of  both,  which  is
    //   why  they  are  not  freed  again  below.  The  length  is  asked  for
    //   first  so  that  the  encoding  cannot  run  off  the  buffer:  it  is
    //   sized  for  the  largest  curve  this  implementation  carries.
    if(        (sig   =   ECDSA_SIG_new())    ==   NULL
            ||   (bnR   =   BN_bin2bn(rIn->t.buffer,     rIn->t.size,     NULL))    ==   NULL
            ||   (bnS   =   BN_bin2bn(sIn->t.buffer,     sIn->t.size,     NULL))    ==   NULL
            ||   !ECDSA_SIG_set0(sig,      bnR,     bnS))
            FAIL(FATAL_ERROR_INTERNAL);
    bnR   =   NULL;                //   the  signature  owns  them  now
    bnS   =   NULL;
    pder   =   der;
    if(        (derSize   =   i2d_ECDSA_SIG(sig,     NULL))    <=   0
            ||   (unsigned)derSize    >   sizeof(der)
            ||   i2d_ECDSA_SIG(sig,     &pder)    !=   derSize)
            FAIL(FATAL_ERROR_INTERNAL);

//  2.  Use  the  selected  hash  function  to  compute  H0 = Hash(M0).
    //   This  is  an  input  parameter
//  3.  Convert  the  bit  string  H0  to  an  integer  e  as  described  in
//      Appendix  B.2.
    //   The  digest  is  truncated  to  the  length  of  r  rather  than  to
    //   the  length  of  the  order.  That  is  what  this  function  has
    //   always  done  and  what  the  answers  recorded  from  it  say;  a
    //   signature  whose  r  leads  with  a  zero  byte  is  rejected  here,
    //   and  it  is  rejected  here  still.  The  operation  below  is  given
    //   no  signature  md,  so  the  provider  takes  the  t  bytes  it  is
    //   handed  and  does  not  impose  a  length  of  its  own.
    t   =   (digest->size    >   rIn->t.size)   ?   rIn->t.size   :   digest->size;

//  4.  Compute  w = (s')^-1  mod  n.
//  5.  Compute  u1 = (e' * w)  mod  n,  and  u2 = (r' * w)  mod  n.
//  6.  Compute  R = (xR, yR) = u1G + u2Q.  If  R  is  the  point  at
//      infinity,  output  INVALID.
//  7.  Compute  v = Rx  mod  n.
//  8.  Compare  v  and  r0.  If  v = r0,  output  VALID;  otherwise,  INVALID.
    //   All  six  of  those  are  the  provider's,  reached  through  a  key
    //   built  in  this  engine's  library  context.  Building  it  is  also
    //   where  "is  Q  on  the  curve"  is  answered:  the  point  is  decoded
    //   against  the  group,  and  a  point  that  is  not  on  it  is  the
    //   same  fatal  error  it  was  before.
    if(        (pkey   =   EccKeyFromData(curveData,     NULL,    Qin))   ==   NULL
            ||   (ctx   =   EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),
                                                       pkey,    NULL))   ==   NULL
            ||   EVP_PKEY_verify_init(ctx)    <=   0)
            FAIL(FATAL_ERROR_INTERNAL);
    if(EVP_PKEY_verify(ctx,     der,    (size_t)derSize,
                       digest->buffer,     (size_t)t)   ==   1)
            retVal    =   CRYPT_SUCCESS;

   if(sig       !=   NULL)    ECDSA_SIG_free(sig);
   if(bnR       !=   NULL)    BN_free(bnR);
   if(bnS       !=   NULL)    BN_free(bnS);
   EVP_PKEY_CTX_free(ctx);
   EVP_PKEY_free(pkey);
   return       retVal;
}
#endif              //%  TPM_ALG_ECDSA
#ifdef     TPM_ALG_ECSCHNORR           //%

// B.13.3.2.23. ValidateSignatureEcSchnorr()""")

print("solve: %d source edits applied" % EDITS)
PY

say "build"
B=${BUILD_DIR:-$(mktemp -d)}
rm -rf "$B"; mkdir -p "$B"
cmake -S "$APP/cmake" -B "$B" -DSPEC_VERSION=116 > "$B/configure.log" 2>&1 \
    || { tail -30 "$B/configure.log"; die "cmake configure failed"; }
cmake --build "$B" -j"${JOBS:-7}" > "$B/build.log" 2>&1 \
    || { grep -E 'error:' "$B/build.log" | head -30; die "build failed"; }

for t in platform/libplatform.so OsslCryptoEngine/libCryptoEngine.so \
         tpm/libTPM.so Simulator; do
    [ -e "$B/$t" ] || die "$t was not built"
done

# The facts the gates read, asserted here so that a solve that builds but did
# not land is not mistaken for a solve that worked.
E="$B/OsslCryptoEngine/libCryptoEngine.so"
undef=$(nm -D --undefined-only "$E" | awk '{print $NF}' | sed 's/@.*//')
for sym in EVP_PKEY_fromdata EVP_PKEY_fromdata_init \
           EVP_PKEY_CTX_new_from_name EVP_PKEY_CTX_new_from_pkey \
           EVP_PKEY_CTX_free EVP_PKEY_free \
           EVP_PKEY_sign EVP_PKEY_verify EVP_PKEY_encrypt \
           OSSL_PARAM_BLD_new OSSL_PARAM_BLD_to_param OSSL_PARAM_BLD_free \
           OSSL_PARAM_free d2i_ECDSA_SIG i2d_ECDSA_SIG; do
    printf '%s\n' "$undef" | grep -qx "$sym" \
        || die "$sym is not an undefined symbol of libCryptoEngine.so"
done

# The two APIs this step closes the door on.  Same lists the verifier holds:
# <openssl/ec.h>'s deprecated key object with the one-shot signing entry
# points, and <openssl/rsa.h>'s low-level API -- which this engine never used,
# and which is named here because "RSA_public_encrypt with the library's own
# OAEP" is the wrong answer that looks most like the right one.
EC_DEPRECATED=(EC_KEY_new EC_KEY_new_by_curve_name EC_KEY_free EC_KEY_dup
               EC_KEY_set_group EC_KEY_get0_group EC_KEY_set_conv_form
               EC_KEY_set_public_key EC_KEY_get0_public_key
               EC_KEY_set_private_key EC_KEY_get0_private_key
               EC_KEY_set_public_key_affine_coordinates
               EC_KEY_generate_key EC_KEY_check_key EC_KEY_set_asn1_flag
               EC_KEY_precompute_mult
               ECDSA_do_sign ECDSA_do_sign_ex ECDSA_do_verify
               ECDSA_sign ECDSA_sign_ex ECDSA_sign_setup ECDSA_verify
               ECDSA_size)
RSA_DEPRECATED=(RSA_new RSA_free RSA_set0_key RSA_set0_factors RSA_get0_key
                RSA_public_encrypt RSA_private_decrypt
                RSA_public_decrypt RSA_private_encrypt
                RSA_sign RSA_verify RSA_size RSA_generate_key_ex
                RSA_padding_add_PKCS1_OAEP RSA_padding_check_PKCS1_OAEP
                RSA_padding_add_PKCS1_OAEP_mgf1
                RSA_padding_check_PKCS1_OAEP_mgf1
                RSA_padding_add_PKCS1_type_1 RSA_padding_check_PKCS1_type_1
                RSA_padding_add_PKCS1_type_2 RSA_padding_check_PKCS1_type_2
                RSA_padding_add_PKCS1_PSS RSA_verify_PKCS1_PSS
                RSA_padding_add_PKCS1_PSS_mgf1 RSA_verify_PKCS1_PSS_mgf1)
for sym in "${EC_DEPRECATED[@]}" "${RSA_DEPRECATED[@]}"; do
    if printf '%s\n' "$undef" | grep -qx "$sym"; then
        die "$sym is still imported by libCryptoEngine.so"
    fi
done

# And gone from the sources, which is the half an import scan cannot see -- a
# name reached through the loader as a string is neither a call nor a
# relocation.  The type is asked for here too: it is the stack object the
# deleted sequence was built around, and a tree that still declares one has
# kept the API whatever else it did.
ban=$(printf '%s|' "${EC_DEPRECATED[@]}" "${RSA_DEPRECATED[@]}")
if grep -rnE "\\b(${ban}EC_KEY)\\b" src/OsslCryptoEngine/ ; then
    die "the deprecated EC key and low-level RSA APIs are still written in the engine sources"
fi

# The four encoders that had to go, and the four that had to stay.  A tree that
# deleted all eight took the private side's padding with it, which is a
# different migration from this one.
# A definition in this file is the function's name at the start of a line, on
# the line after its return type; a call to it never is.
for fn in OaepEncode RSAES_PKSC1v1_5Encode PssDecode RSASSA_Decode; do
    if grep -qE "^[[:space:]]*$fn[[:space:]]*\(" src/OsslCryptoEngine/CpriRSA.c; then
        die "$fn is still defined in CpriRSA.c"
    fi
done
for fn in OaepDecode RSAES_Decode PssEncode RSASSA_Encode; do
    grep -qE "^[[:space:]]*$fn[[:space:]]*\(" src/OsslCryptoEngine/CpriRSA.c \
        || die "$fn is no longer defined in CpriRSA.c -- the private side does not move at this step"
done

# The private half is where step 2 left it.
for fn in _cpri__DecryptRSA _cpri__SignRSA; do
    awk -v f="$fn" '
        $0 ~ "^"f"\\(" { inf = 1 }
        inf && /RSADP\(/ { found = 1 }
        inf && /^}/ { inf = 0 }
        END { exit(found ? 0 : 1) }' src/OsslCryptoEngine/CpriRSA.c \
        || die "$fn no longer calls RSADP -- the private exponentiation must stay the engine's own"
done

say "done"
printf 'built in %s\n' "$B"
