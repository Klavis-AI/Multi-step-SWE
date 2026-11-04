#!/usr/bin/env bash
# Step 2 golden solution -- the private pool, the library's ECDSA, and OpenSSL
# 1.1.1w.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/02-rng-and-key-objects/solution/solve.sh; edit it here and regenerate,
# never edit the copy under tasks/.
#
# Run from /app on the tree step 1 left behind, with OpenSSL 1.1.1w installed
# as the system OpenSSL.  It edits the tree in place and builds it once to
# prove the edits compile; the verifier builds it again from scratch, so
# nothing here is load-bearing except the source changes.
#
#
# WHAT THIS DOES, AND WHY EACH PIECE IS NEEDED
#
# Six requirements, all of them "1.1.1 publishes this now, stop hand-rolling
# it", and all of them about secrets rather than about types:
#
#   the split DRBG        RAND_priv_bytes for the two draws that become or
#                         protect key material; RAND_bytes stays where it is
#   BN_bn2binpad          replaces five open-coded zero-fill-then-convert
#                         sequences and two plain conversions, and gets the
#                         too-wide case right that BnTo2B gets wrong
#   OPENSSL_cleanse       the two signature nonces that die in engine storage
#   BN_FLG_CONSTTIME      the private-key exponentiation, and only that one
#   ECDSA_do_sign         SignEcdsa's hand-rolled k/k^-1/r/s
#   ECDSA_do_verify       ValidateSignatureEcdsa's hand-rolled u1G + u2Q
#
# Four things here are not mechanical:
#
#   * Which draws are secret is a question about the compiled tree, not about
#     the file list.  RSAKeySieve.c draws in four places and every one of them
#     is inside `#ifdef RSA_KEY_SIEVE`, which nothing defines; the key
#     generator that compiles is CpriRSA.c's, and its only draw is the
#     plaintext of the trial encrypt-decrypt.  So the secret draws in the
#     engine are exactly two: GetRandomPrivate's scalar and SignSM2's nonce.
#
#   * BnTo2B computes its zero-fill width in a UINT16 and then tests it for
#     being negative, which it cannot be.  A number too wide for the output
#     buffer underflows that subtraction and the fill loop runs off the end of
#     the buffer instead of returning FALSE.  BN_bn2binpad returns -1 for that
#     case, which is the return this function always meant to make.
#
#   * _math__ModExp is published, is scored by the `modexp` family, and is
#     shared between RSAEP (public exponent, public data) and RSADP (private
#     exponent).  Marking its exponent constant-time unconditionally would be
#     marking RSAEP's public exponent too.  So the arithmetic becomes a static
#     core that is told which kind of exponent it has, and there are two
#     entry points into it: _math__ModExp as it was, and _math__ModExpPrivate
#     for the decryption path.
#
#   * ValidateSignatureEcdsa truncates the digest to the length of *r*, not to
#     the length of the curve order -- `t = (digest->size > rIn->t.size) ?
#     rIn->t.size : digest->size`.  For a signature whose r leads with a zero
#     byte, and whose caller normalised that byte away, that is one byte fewer
#     of digest than the signer used, and the verification fails.  It is a
#     defect, four recorded cases depend on it, and behaviour is unchanged
#     means it stays: `t` is what gets handed to ECDSA_do_verify.  Every curve
#     order this tree implements is a whole number of bytes, so the library's
#     own order-length truncation never fires on top of it and the answer is
#     the answer the tree gives today.
set -euo pipefail

APP=${1:-/app}
cd "$APP"

say() { printf '\n== %s\n' "$*"; }
die() { printf '\nsolve: %s\n' "$*" >&2; exit 1; }

for f in src/OsslCryptoEngine/CpriRNG.c src/OsslCryptoEngine/CpriECC.c \
         src/OsslCryptoEngine/CpriRSA.c src/OsslCryptoEngine/CpriMisc.c \
         src/OsslCryptoEngine/MathFunctions.c \
         src/include/CpriRNG_fp.h src/include/MathFunctions_fp.h; do
    [ -f "$f" ] || die "$f is missing -- this is not the tree this solves"
done
grep -q 'openssl/crypto.h' src/OsslCryptoEngine/OsslCryptoEngine.h \
    || die "the engine header does not include <openssl/crypto.h> -- step 1 did not run"

say "source"
python3 - <<'PY'
import re
import sys

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


def replace_body(path, name, body):
    """Replace one function's body, keeping its comment and parameter list.

    The two ECDSA functions are two hundred lines of arithmetic between them
    and the file they live in has had its indentation destroyed by the
    specification's own extraction tool, so there is no anchor for them that a
    human could check.  What is stable is the shape: a line that begins with
    the function's name at column zero, a parameter list, and a brace-balanced
    body.  This finds that body and swaps it, so the signature and the
    documentation above it are provably untouched.
    """
    global EDITS
    src = open(path).read()
    m = re.search(r"^%s\s*\(" % re.escape(name), src, re.M)
    if m is None:
        sys.exit("solve: %s: no definition of %s at column zero" % (path, name))
    if len(re.findall(r"^%s\s*\(" % re.escape(name), src, re.M)) != 1:
        sys.exit("solve: %s: %s is defined more than once" % (path, name))
    i = src.index("{", m.end())
    depth, j, n = 0, i, len(src)
    while j < n:
        ch = src[j]
        if ch == '"' or ch == "'":
            q, j = ch, j + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == "\\" else 1
        elif src.startswith("/*", j):
            j = src.index("*/", j) + 1
        elif src.startswith("//", j):
            j = src.index("\n", j)
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    if depth != 0:
        sys.exit("solve: %s: unbalanced braces in %s" % (path, name))
    open(path, "w").write(src[:i] + body + src[j + 1:])
    EDITS += 1


# ================================================================ the DRBG ===
#
# 1.1.1 keeps two DRBGs.  The public one is RAND_bytes, which is what the
# engine has always used; the private one is RAND_priv_bytes, which is seeded
# the same way and never used for anything that leaves the machine.  The
# engine gets an entry point for it beside the one it already publishes, so
# that a draw site says which pool it is drawing from by the name it calls.
patch("src/include/CpriRNG_fp.h", [
    ("""LIB_EXPORT    UINT16
_cpri__GenerateRandom(
   INT32                    randomSize,
   BYTE                     *buffer
   );""",
     """LIB_EXPORT    UINT16
_cpri__GenerateRandom(
   INT32                    randomSize,
   BYTE                     *buffer
   );

LIB_EXPORT    UINT16
_cpri__GeneratePrivateRandom(
   INT32                    randomSize,
   BYTE                     *buffer
   );""", 1),
])

patch("src/OsslCryptoEngine/CpriRNG.c", [
    ("""//     RAND_bytes     uses   1   for  success    and    we  use  0
   if(RAND_bytes(buffer,            randomSize)        ==  1)
          return     (UINT16)randomSize;
   else
          return     0;
}""",
     """//     RAND_bytes     uses   1   for  success    and    we  use  0
   if(RAND_bytes(buffer,            randomSize)        ==  1)
          return     (UINT16)randomSize;
   else
          return     0;
}

// B.7.3.4.0.    _cpri__GeneratePrivateRandom()

// The same as _cpri__GenerateRandom() except for where the bytes come from.
// 1.1.1 keeps a second DRBG for values that are, or that protect, key
// material; it is seeded from the same entropy source and its output is never
// handed to anything outside the TPM.  Callers pick the pool by picking the
// entry point, which is what makes a draw site say what kind of bytes it is
// asking for.
LIB_EXPORT    UINT16
_cpri__GeneratePrivateRandom(
   INT32                    randomSize,
   BYTE                     *buffer
   )
{
   //
   //     We  don't  do  negative        sizes  or  ones   that     are  too  large
   if     (randomSize    <  0   ||  randomSize      >  UINT16_MAX)
          return     0;
   //     RAND_priv_bytes   uses   1   for  success    and    we  use  0
   if(RAND_priv_bytes(buffer,            randomSize)        ==  1)
          return     (UINT16)randomSize;
   else
          return     0;
}""", 1),
])

# The two secret draws in the engine.  GetRandomPrivate's whole contract is "a
# random scalar in [1, n-1]" -- it is the ephemeral key for ECDH, the nonce for
# ECDSA and the nonce for EC Schnorr -- and SignSM2 open-codes the same thing
# rather than calling it.  Everything else the engine draws is padding or is a
# value a caller asked for, and stays on the public pool.
patch("src/OsslCryptoEngine/CpriECC.c", [
    ("_cpri__GenerateRandom(dOut->t.size, dOut->t.buffer);",
     "_cpri__GeneratePrivateRandom(dOut->t.size,     dOut->t.buffer);", 1),
    ("_cpri__GenerateRandom(k.t.size, k.t.buffer);",
     "_cpri__GeneratePrivateRandom(k.t.size,     k.t.buffer);", 1),
])

# ====================================================== fixed-width output ===
#
# BN_bn2binpad(bn, to, tolen) is "write this number into exactly tolen bytes,
# right-aligned, zero-padded", and returns -1 if it does not fit.  Seven call
# sites become it: five that open-code it with a zero-fill and a BN_bn2bin into
# the middle of the buffer, and two plain conversions that have to go as well
# because the requirement is that the narrower call is not in the engine at
# all.
patch("src/OsslCryptoEngine/CpriMisc.c", [
    ("""size   =  size    -     (((UINT16)   BN_num_bits(inVal)             +   7)   /   8);
      if(size   <   0)
            return     FALSE;
      for(;size     >   0;    size--)
            *pb++   =   0;
      BN_bn2bin(inVal,           pb);
      return    TRUE;""",
     """// BN_bn2binpad() is the whole of what the four lines that used to be
      // here were reaching for: the number goes into the low-order bytes and
      // the bytes in front of it are zeroed.  It also gets right the case
      // they got wrong.  `size` is a UINT16, so the subtraction that used to
      // be here underflowed for a number too wide for the buffer and the
      // fill loop then ran off the end of it; the test for a negative size
      // could never fire.  A negative return is that case, and FALSE is what
      // this function always meant to return for it.
      if(BN_bn2binpad(inVal,          pb,     size)      <   0)
            return    FALSE;
      return    TRUE;""", 1),
])

patch("src/OsslCryptoEngine/CpriRSA.c", [
    # RsaPrivateExponent(): d into the key object, at the width of the modulus.
    ("""fill     =     key->publicKey->size                        -  BN_num_bytes(bnD);
      BN_bn2bin(bnD,                 &key->privateKey->buffer[fill]);
   memset(key->privateKey->buffer,                    0,     fill);""",
     """if(BN_bn2binpad(bnD,     key->privateKey->buffer,
                          key->publicKey->size)      <   0)
            FAIL(FATAL_ERROR_INTERNAL);""", 1),

    # _cpri__GetPublicKeyRSA(): the modulus, at a width the line above has
    # just checked is exactly right.  Padding nothing is still padding.
    ("BN_bn2bin(bnN, publicKey->buffer);",
     """if(BN_bn2binpad(bnN,     publicKey->buffer,     publicKey->size)      <   0)
                  FAIL(FATAL_ERROR_INTERNAL);""", 1),

    # _cpri__TestKeyRSA(): the recovered private exponent into the caller's 2B.
    ("""fill       =     d->size         -   BN_num_bytes(bnD);
   BN_bn2bin(bnD,                   &d->buffer[fill]);
   memset(d->buffer,                       0,  fill);""",
     """if(BN_bn2binpad(bnD,     d->buffer,     d->size)      <   0)
           FAIL(FATAL_ERROR_INTERNAL);""", 1),

    # Both `fill` variables are now unused.  Two declarations, one in each of
    # the two functions above.
    ("UINT32 fill;", "", 2),
])

patch("src/OsslCryptoEngine/MathFunctions.c", [
    # _math__Multiply(), fixed-width arm: the product at the full width of its
    # two operands.
    ("""BN_bn2bin(bnP,               &p[aSize         +  bSize        -  retVal]);
            memset(p,           0,   aSize      +   bSize        -  retVal);
            retVal         =   aSize     +   bSize;""",
     """if(BN_bn2binpad(bnP,      p,    aSize     +   bSize)      <   0)
                  FAIL(FATAL_ERROR_INTERNAL);
            retVal         =   aSize     +   bSize;""", 1),

    # _math__Multiply(), floating arm: retVal is BN_num_bytes(bnP), so this
    # pads to the number's own width and copies exactly what it used to.
    ("""BN_bn2bin(bnP,               p);
            *pSize         =   retVal;""",
     """if(BN_bn2binpad(bnP,      p,    retVal)      <   0)
                  FAIL(FATAL_ERROR_INTERNAL);
            *pSize         =   retVal;""", 1),
])

# ================================================ constant-time private exp ===
#
# _math__ModExp is published and is shared: RSAEP goes through it with a public
# exponent on public data, RSADP with the private exponent.  It takes its
# exponent as bytes and has no way to tell which it has, so the decision has to
# come from the caller.  The arithmetic becomes a static core that is told, and
# there are two ways in -- the published one, unchanged in signature and in
# behaviour, and a private one for the decryption path.
patch("src/OsslCryptoEngine/MathFunctions.c", [
    ("LIB_EXPORT CRYPT_RESULT _math__ModExp(",
     """// The arithmetic behind both _math__ModExp() and _math__ModExpPrivate().
// The only thing that separates them is whether the exponent is a secret,
// which the caller knows and the arithmetic cannot work out for itself.
static        CRYPT_RESULT
ModExp(""", 1),

    ("""const       BYTE                 *n                      //  IN:   modulu
   )
{
   CRYPT_RESULT                     retVal   =    CRYPT_SUCCESS;""",
     """const       BYTE                 *n,                     //  IN:   modulu
   BOOL                              secretExponent          //  IN:   e  is  a  private  exponent
   )
{
   CRYPT_RESULT                     retVal   =    CRYPT_SUCCESS;""", 1),

    ("""BIGNUM                           *bnN;
   INT32                            i;""",
     """BIGNUM                           *bnN;""", 1),

    ("if(!(BN_mod_exp(bnC, bnM, bnE, bnN, context)))",
     """//     A    private     exponent     is   marked   as   one.    BN_mod_exp
   //     dispatches      on   the    flag:    with    it   set    the
   //     exponentiation       goes   through     the    montgomery      ladder,
   //     whose    running     time    does    not    depend    on   the   bits
   //     of   the    exponent.
   if(secretExponent)
             BN_set_flags(bnE,           BN_FLG_CONSTTIME);
   if(!(BN_mod_exp(bnC,                    bnM,    bnE,    bnN,    context)))""", 1),

    ("""i   =     cSize      -     BN_num_bytes(bnC);
   BN_bn2bin(bnC,               &c[i]);
   memset(c,            0,    i);""",
     """if(BN_bn2binpad(bnC,           c,    cSize)      <    0)
             FAIL(FATAL_ERROR_INTERNAL);""", 1),

    ("""Cleanup:
   //     Free      up     allocated       BN  values
   BN_CTX_end(context);
   BN_CTX_free(context);
   return           retVal;
}""",
     """Cleanup:
   //     Free      up     allocated       BN  values
   BN_CTX_end(context);
   BN_CTX_free(context);
   return           retVal;
}

// B.5.2.10.     _math__ModExp
// The published entry point, unchanged: it is what RSAEP uses, and RSAEP's
// exponent is public.
LIB_EXPORT        CRYPT_RESULT
_math__ModExp(
   UINT32                           cSize,                  //  IN:   size    of  the     result
   BYTE                             *c,                     //  OUT:     results      buffer
   const       UINT32               mSize,                  //  IN:   size    of  number      to  be  exponentiated
   const       BYTE                 *m,                     //  IN:   number      to  be  exponentiated
   const       UINT32               eSize,                  //  IN:   size    of  power
   const       BYTE                 *e,                     //  IN:   power
   const       UINT32               nSize,                  //  IN:   modulus     size
   const       BYTE                 *n                      //  IN:   modulu
   )
{
   return     ModExp(cSize,       c,   mSize,      m,   eSize,      e,   nSize,      n,   FALSE);
}

// B.5.2.10.1.   _math__ModExpPrivate
// The same operation on a secret exponent.  Only the RSA decryption path uses
// it, and it is the only caller that has a private exponent to hand.
LIB_EXPORT        CRYPT_RESULT
_math__ModExpPrivate(
   UINT32                           cSize,                  //  IN:   size    of  the     result
   BYTE                             *c,                     //  OUT:     results      buffer
   const       UINT32               mSize,                  //  IN:   size    of  number      to  be  exponentiated
   const       BYTE                 *m,                     //  IN:   number      to  be  exponentiated
   const       UINT32               eSize,                  //  IN:   size    of  power
   const       BYTE                 *e,                     //  IN:   the     private      exponent
   const       UINT32               nSize,                  //  IN:   modulus     size
   const       BYTE                 *n                      //  IN:   modulu
   )
{
   return     ModExp(cSize,       c,   mSize,      m,   eSize,      e,   nSize,      n,   TRUE);
}""", 1),
])

patch("src/include/MathFunctions_fp.h", [
    ("""const       BYTE                 *n                      //  IN:   modulu
   );""",
     """const       BYTE                 *n                      //  IN:   modulu
   );

LIB_EXPORT        CRYPT_RESULT
_math__ModExpPrivate(
   UINT32                           cSize,                  //  IN:   size    of  the     result
   BYTE                             *c,                     //  OUT:     results      buffer
   const       UINT32               mSize,                  //  IN:   size    of  number      to  be  exponentiated
   const       BYTE                 *m,                     //  IN:   number      to  be  exponentiated
   const       UINT32               eSize,                  //  IN:   size    of  power
   const       BYTE                 *e,                     //  IN:   the     private      exponent
   const       UINT32               nSize,                  //  IN:   modulus     size
   const       BYTE                 *n                      //  IN:   modulu
   );""", 1),
])

# RSADP is the only private-exponent caller in the engine.  RSAEP, four lines
# above it in the same file, keeps the published entry point.
patch("src/OsslCryptoEngine/CpriRSA.c", [
    ("""retVal     =   _math__ModExp(dInOutSize,                    dInOut,       dInOutSize,          dInOut,
                                          key->privateKey->size,                  key->privateKey->buffer,
                                          key->publicKey->size,                  key->publicKey->buffer);""",
     """retVal     =   _math__ModExpPrivate(dInOutSize,                    dInOut,       dInOutSize,          dInOut,
                                          key->privateKey->size,                  key->privateKey->buffer,
                                          key->publicKey->size,                  key->publicKey->buffer);""", 1),
])

# ================================================== erase the nonces =========
#
# SchnorrEcc and SignSM2 each keep the per-signature secret scalar in a
# TPM2B_ECC_PARAMETER of their own and return with it still there.  A leaked
# nonce plus the signature it produced is the private key, so the storage is
# erased before it goes back to whatever runs next -- with OPENSSL_cleanse and
# not memset, because a store to a buffer that nothing reads afterwards is a
# store the compiler is allowed to delete.
patch("src/OsslCryptoEngine/CpriECC.c", [
    ("""//  If        we     have       a     value,       finish     the     signature
    if(OK)
            return             EcDaa(rOut,             sOut,     curveId,         dIn,      NULL,      &k);
    else
            return             CRYPT_NO_RESULT;""",
     """//  If        we     have       a     value,       finish     the     signature
    //  k    is    the     nonce.     EcDaa()      is    the    last     thing     that
    //  needs    it,    so    the     result     is   taken     first     and    the
    //  buffer     erased      before      this     frame      goes      away.
    {
            CRYPT_RESULT       kRetVal;

            if(OK)
                    kRetVal    =   EcDaa(rOut,     sOut,    curveId,    dIn,    NULL,    &k);
            else
                    kRetVal    =   CRYPT_NO_RESULT;
            OPENSSL_cleanse(&k,       sizeof(k));
            return     kRetVal;
    }""", 1),

    ("""BnTo2B(&rOut->b,                    bnR,       curveData->n->size);
    BnTo2B(&sOut->b,                    bnS,       curveData->n->size);""",
     """BnTo2B(&rOut->b,                    bnR,       curveData->n->size);
    BnTo2B(&sOut->b,                    bnS,       curveData->n->size);
    //  k   held    the    nonce    for    this   signature     and   is    dead
    //  from    here;    erase    it    rather     than    leave    it    in    the
    //  frame    for    the    next    caller.
    OPENSSL_cleanse(&k,        sizeof(k));""", 1),
])

# ================================================== ECDSA is the library's ===
#
# Both directions.  The signature is the library's ECDSA_SIG, read out through
# ECDSA_SIG_get0 because it is opaque at every release from 1.1.0 on, and the
# key is an EC_KEY over the group the tree already knows how to build.
replace_body("src/OsslCryptoEngine/CpriECC.c", "SignEcdsa", """{
    ECDSA_SIG                             *sig     =   NULL;
    EC_KEY                                *eccKey  =   NULL;
    EC_GROUP                              *group   =   NULL;
    BIGNUM                                *bnD     =   NULL;
    const     BIGNUM                      *bnR;
    const     BIGNUM                      *bnS;
    BN_CTX                                *context;
    int                                    dLen;
    const     ECC_CURVE_DATA              *curveData    =   GetCurveData(curveId);

    pAssert(rOut    !=   NULL    &&   sOut    !=   NULL    &&   dIn    !=   NULL
            &&    digest    !=   NULL);
    //   Assume  the  size  variables  do  not  overflow, which  should  not
    //   happen  in  the  contexts  that  this  function  will  be  called.
    pAssert(curveData    !=   NULL
            &&    curveData->n->size    <=   MAX_ECC_PARAMETER_BYTES);
    assert2Bsize(dIn->t);

    context   =   BN_CTX_new();
    if(context    ==   NULL)
            FAIL(FATAL_ERROR_ALLOCATION);
    BN_CTX_start(context);

    //   The  digest  is  used  left  to  right  and  no  wider  than  the
    //   order.  Every  order  this  implementation  carries  is  a  whole
    //   number  of  bytes,  so  truncating  to  n->size  bytes  here  is  the
    //   same  truncation  ECDSA_do_sign()  would  make  for  itself  from  the
    //   bit  length  of  the  order.
    dLen   =   (digest->size    >   curveData->n->size)
                   ?   (int)curveData->n->size   :   (int)digest->size;

    if(        (group    =   EccCurveInit(curveId,     context))    ==   NULL
            ||   (eccKey   =   EC_KEY_new())    ==   NULL
            ||   !EC_KEY_set_group(eccKey,      group)
            //   the  private  scalar  of  the  signing  key
            ||   (bnD   =   BN_bin2bn(dIn->t.buffer,     dIn->t.size,     NULL))    ==   NULL
            ||   !EC_KEY_set_private_key(eccKey,      bnD)
            //   libcrypto  draws  the  per-message  secret  k,  inverts  it,
            //   retries  on  a  zero  r  or  s,  and  erases  it
            ||   (sig   =   ECDSA_do_sign(digest->buffer,     dLen,     eccKey))    ==   NULL)
            FAIL(FATAL_ERROR_INTERNAL);

    //   r  and  s  belong  to  the  signature;  ECDSA_SIG_get0()  lends  them
    //   out  and  ECDSA_SIG_free()  takes  them  with  it.
    ECDSA_SIG_get0(sig,      &bnR,     &bnS);
    rOut->t.size   =   curveData->n->size;
    sOut->t.size   =   curveData->n->size;
    if(        BN_bn2binpad(bnR,      rOut->t.buffer,     rOut->t.size)    <   0
            ||   BN_bn2binpad(bnS,      sOut->t.buffer,     sOut->t.size)    <   0)
            FAIL(FATAL_ERROR_INTERNAL);

    ECDSA_SIG_free(sig);
    EC_KEY_free(eccKey);
    EC_GROUP_free(group);
    BN_clear_free(bnD);
    BN_CTX_end(context);
    BN_CTX_free(context);
    return     CRYPT_SUCCESS;
}""")

replace_body("src/OsslCryptoEngine/CpriECC.c", "ValidateSignatureEcdsa", """{
    const     TPM2B                       *n;
    BN_CTX                                *context;
    EC_GROUP                              *group    =   NULL;
    EC_POINT                              *pQ       =   NULL;
    EC_KEY                                *eccKey   =   NULL;
    ECDSA_SIG                             *sig      =   NULL;
    BIGNUM                                *bnQx;
    BIGNUM                                *bnQy;
    BIGNUM                                *bnR      =   NULL;
    BIGNUM                                *bnS      =   NULL;
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
    if(        _math__uComp(rIn->t.size,     rIn->t.buffer,     n->size,     n->buffer)    >=   0
            ||   _math__uComp(sIn->t.size,     sIn->t.buffer,     n->size,     n->buffer)    >=   0
        )
            return     CRYPT_FAIL;

    context   =   BN_CTX_new();
    if(context    ==   NULL)
            FAIL(FATAL_ERROR_ALLOCATION);
    BN_CTX_start(context);
    bnQx   =   BN_CTX_get(context);
    bnQy   =   BN_CTX_get(context);

    //   Assume  the  size  variables  do  not  overflow, which  should  not
    //   happen  in  the  contexts  that  this  function  will  be  called.
    assert2Bsize(Qin->x.t);
    assert2Bsize(rIn->t);
    assert2Bsize(sIn->t);

    //   BN_CTX_get()  is  sticky  so  only  need  to  check  the  last  value
    //   to  know  that  all  worked.  r  and  s  are  not  the  context's:
    //   ECDSA_SIG_set0()  takes  ownership  of  them.
    if(        bnQy    ==   NULL
            //   initialize  the  group  parameters
            ||   (group    =   EccCurveInit(curveId,     context))    ==   NULL
            //   allocate  a  local  point
            ||   (pQ   =   EC_POINT_new(group))    ==   NULL
            //   use  the  public  key  values  (QxIn  and  QyIn)  to
            //   initialize  Q.  1.1.0  and  later  check  here  that  the
            //   point  is  on  the  curve.
            ||   BN_bin2bn(Qin->x.t.buffer,     Qin->x.t.size,     bnQx)    ==   NULL
            ||   BN_bin2bn(Qin->y.t.buffer,     Qin->y.t.size,     bnQy)    ==   NULL
            ||   !EC_POINT_set_affine_coordinates_GFp(group,     pQ,     bnQx,     bnQy,     context)
            ||   (eccKey   =   EC_KEY_new())    ==   NULL
            ||   !EC_KEY_set_group(eccKey,      group)
            ||   !EC_KEY_set_public_key(eccKey,      pQ)
            //   convert  the  signature  values
            ||   (sig   =   ECDSA_SIG_new())    ==   NULL
            ||   (bnR   =   BN_bin2bn(rIn->t.buffer,     rIn->t.size,     NULL))    ==   NULL
            ||   (bnS   =   BN_bin2bn(sIn->t.buffer,     sIn->t.size,     NULL))    ==   NULL
            ||   !ECDSA_SIG_set0(sig,      bnR,     bnS))
            FAIL(FATAL_ERROR_INTERNAL);
    bnR   =   NULL;                //   the  signature  owns  them  now
    bnS   =   NULL;

//  2.  Use  the  selected  hash  function  to  compute  H0 = Hash(M0).
    //   This  is  an  input  parameter
//  3.  Convert  the  bit  string  H0  to  an  integer  e  as  described  in
//      Appendix  B.2.
    //   The  digest  is  truncated  to  the  length  of  r  rather  than  to
    //   the  length  of  the  order.  That  is  what  this  function  has
    //   always  done  and  what  the  answers  recorded  from  it  say;  a
    //   signature  whose  r  leads  with  a  zero  byte  is  rejected  here,
    //   and  it  is  rejected  here  still.
    t   =   (digest->size    >   rIn->t.size)   ?   rIn->t.size   :   digest->size;

//  4.  Compute  w = (s')^-1  mod  n.
//  5.  Compute  u1 = (e' * w)  mod  n,  and  u2 = (r' * w)  mod  n.
//  6.  Compute  R = (xR, yR) = u1G + u2Q.  If  R  is  the  point  at
//      infinity,  output  INVALID.
//  7.  Compute  v = Rx  mod  n.
//  8.  Compare  v  and  r0.  If  v = r0,  output  VALID;  otherwise,  INVALID.
    //   All  six  of  those  are  ECDSA_do_verify(),  which  returns  1  only
    //   for  a  signature  it  has  checked  through  to  step  8.
    if(ECDSA_do_verify(digest->buffer,      t,    sig,     eccKey)    ==   1)
            retVal    =   CRYPT_SUCCESS;

   if(sig       !=   NULL)    ECDSA_SIG_free(sig);
   if(bnR       !=   NULL)    BN_free(bnR);
   if(bnS       !=   NULL)    BN_free(bnS);
   if(eccKey    !=   NULL)    EC_KEY_free(eccKey);
   if(pQ        !=   NULL)    EC_POINT_free(pQ);
   if(group          !=  NULL)    EC_GROUP_free(group);
   BN_CTX_end(context);
   BN_CTX_free(context);
   return       retVal;
}""")

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
# One `nm` into a variable rather than one per symbol down a pipe: `grep -q`
# exits at the first match, `nm` takes SIGPIPE for the rest of its output, and
# under `set -o pipefail` that is a failed pipeline whenever nm happens not to
# have finished writing first.  It is a race, so it fails on a different symbol
# each run.
undef=$(nm -D --undefined-only "$E" | awk '{print $NF}' | sed 's/@.*//')
for sym in RAND_priv_bytes BN_bn2binpad OPENSSL_cleanse BN_set_flags \
           ECDSA_do_sign ECDSA_do_verify ECDSA_SIG_get0; do
    printf '%s\n' "$undef" | grep -qx "$sym" \
        || die "$sym is not an undefined symbol of libCryptoEngine.so"
done
if printf '%s\n' "$undef" | grep -qx BN_bn2bin; then
    die "BN_bn2bin is still imported by libCryptoEngine.so"
fi

say "done"
printf 'built in %s\n' "$B"
