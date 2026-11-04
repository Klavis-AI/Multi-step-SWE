#!/usr/bin/env bash
# Step 4 golden solution -- the symmetric half of the engine, moved onto EVP
# with ciphers it fetches by name from the library context step 3 gave it.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/04-evp-symmetric/solution/solve.sh; edit it here and regenerate, never
# edit the copy under tasks/.
#
# Run from /app on the tree step 3 left behind, with OpenSSL 3.0.17 installed
# as the system OpenSSL.  It edits the tree in place and builds it once to
# prove the edits compile; the verifier builds it again from scratch, so
# nothing here is load-bearing except the source changes.
#
#
# WHAT THIS DOES, AND WHY EACH PIECE IS NEEDED
#
# CpriSym.c is the last file in the engine that still talks to libcrypto the
# way 1.0.2 did: AES_set_encrypt_key() into an AES_KEY on the stack, then
# AES_encrypt() one block at a time, with CBC, CFB, CTR and OFB written out by
# hand around it.  That API is not deprecated at 3.0 -- it is worse than
# deprecated, it is a hole in the provider story.  A key schedule built on the
# stack is not a provider's key, so nothing a FIPS build, a hardware provider
# or a property query decides applies to it.  Step 6 is where those decisions
# start to matter; this step is what makes them possible.
#
# Three pieces:
#
#   the table        fifteen ciphers -- five modes over three key sizes --
#                    fetched by name from OsslLibraryContext() in
#                    _cpri__SymStartup(), held, and given back in
#                    OsslSymCleanup() before the context they came from goes.
#                    EVP_aes_128_cbc() and its relatives still compile; what
#                    they return is whatever the *process default* context
#                    holds, which is the thing step 3 stopped doing.
#
#   the modes        CBC, CFB, CTR, ECB and OFB come from the provider now,
#                    not from a loop in this file.  This is the part that is
#                    not bookkeeping: the interface's IV contract is not EVP's.
#
#   the shim         `typedef AES_KEY SM4_KEY;` and its four #defines go, with
#                    the #ifdef TPM_ALG_SM4 block they exist to serve.  That
#                    block is never compiled (Implementation.h:85 is
#                    `#define ALG_SM4 ALG_NO`) and it is the only remaining
#                    place in the tree that spells the low-level names.
#
#
# THE IV CONTRACT, WHICH IS WHERE THE WORK IS
#
# Every one of these entry points takes `BYTE *iv` IN/OUT, and what it leaves
# there is what the next chained call starts from.  Four of the five modes
# hand back something EVP does not keep in the same form:
#
#   CBC   the last cipher text block.  Same as EVP's register, but it is read
#         here out of the data rather than out of the context -- on decrypt
#         from a copy taken before the call, because dIn and dOut may be the
#         same buffer.
#
#   CFB   the last cipher text bytes, **zero filled** to sixteen when the data
#         ran short.  EVP's CFB register keeps the unused key stream there
#         instead.  CpriSym.c:296-302 is the loop that writes those zeros.
#
#   CTR   the counter, stepped once per block *begun* -- so a 20-byte call
#         steps it twice.  Big endian, from the low end.
#
#   OFB   the key stream register, which is not the cipher text and cannot be
#         recovered from it when the last block is short.  So the last block,
#         whole or short, is run as a block of zeros: OFB's key stream does
#         not depend on its input, so that returns the register in the clear
#         and the final XOR is done here.  Two EVP data calls, one operation.
#
#   ECB   no IV at all.
#
# Getting these wrong does not fail a gate.  It fails the corpus, which
# records the outgoing IV of every case that has one.
set -euo pipefail

APP=${APP:-/app}
cd "$APP"

say()  { printf '\n=== %s\n' "$*"; }
die()  { printf 'solve: %s\n' "$*" >&2; exit 1; }

for f in src/OsslCryptoEngine/CpriSym.c src/OsslCryptoEngine/CpriCryptPri.c \
         src/OsslCryptoEngine/OsslCryptoEngine.h; do
    [ -f "$f" ] || die "$f is missing -- this is not the tree this solves"
done
grep -q 'EVP_MD_fetch' src/OsslCryptoEngine/CpriHash.c \
    || die "the engine does not fetch its digests -- step 3 did not run"

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


# ================================================ the cipher table and the run
#
# In place of the SM4 shim, which is the other thing that has to leave this
# file.  The table goes above _cpri__SymStartup() because that is what fills
# it, and the two run helpers go above the entry points that use them.
patch("src/OsslCryptoEngine/CpriSym.c", [

    ("""// The following sets of defines are used to allow use of the SM4 algorithm identifier while waiting
// for the

// SM4 implementation code to appear.
typedef     AES_KEY  SM4_KEY;
#define     SM4_set_encrypt_key         AES_set_encrypt_key
#define     SM4_set_decrypt_key         AES_set_decrypt_key
#define     SM4_decrypt                 AES_decrypt
#define     SM4_encrypt                 AES_encrypt""",
     """// The ciphers this engine encrypts with, and where they come from.  3.0
// moved the algorithm objects behind providers: EVP_aes_128_cbc() and its
// fourteen relatives still compile, but what they hand back belongs to the
// process default context, which is the one this engine no longer takes
// anything from.  These are fetched by name, once, from the engine's own
// context, and held for as long as the engine runs.
typedef  enum
{
      SYM_MODE_CBC      =  0,
      SYM_MODE_CFB,
      SYM_MODE_CTR,
      SYM_MODE_ECB,
      SYM_MODE_OFB,
      SYM_MODE_COUNT
} SYM_MODE;

#define   SYM_KEY_SIZE_COUNT         3           //  128,   192   and   256   bits

typedef  struct
{
      const  char       *name;
      EVP_CIPHER        *cipher;
} SYM_CIPHER_ENTRY;

static   SYM_CIPHER_ENTRY       s_symServer[SYM_MODE_COUNT][SYM_KEY_SIZE_COUNT]   =
{
      { {"AES-128-CBC",  NULL},   {"AES-192-CBC",  NULL},   {"AES-256-CBC",  NULL} },
      { {"AES-128-CFB",  NULL},   {"AES-192-CFB",  NULL},   {"AES-256-CFB",  NULL} },
      { {"AES-128-CTR",  NULL},   {"AES-192-CTR",  NULL},   {"AES-256-CTR",  NULL} },
      { {"AES-128-ECB",  NULL},   {"AES-192-ECB",  NULL},   {"AES-256-ECB",  NULL} },
      { {"AES-128-OFB",  NULL},   {"AES-192-OFB",  NULL},   {"AES-256-OFB",  NULL} }
};

// B.11.2.1.     SymCipher()

// The cipher for one mode and key size.  Engine internal.  A key size this
// table does not have is the same error the key schedule used to report, so
// the callers treat a NULL the way they treated a key schedule that
// refused to build.
static   const  EVP_CIPHER      *
SymCipher(
      SYM_MODE                  mode,                    //   IN:   which     chaining      mode
      UINT32                    keySizeInBits            //   IN:   key    size   in   bits
      )
{
      if(keySizeInBits          !=  128     &&  keySizeInBits        !=  192
              &&  keySizeInBits         !=  256)
              return       NULL;
      return    s_symServer[mode][(keySizeInBits            /  64)   -   2].cipher;
}

// B.11.2.2.     SymRun()

// One symmetric operation, from the cipher the table holds to the bytes the
// caller asked for.  Every entry point below goes through here, so there is
// one place in this file where a cipher context is made, used and given back.
// Padding is off: this interface has always handed whole blocks to the cipher
// and kept the short-block rules for itself.
static   CRYPT_RESULT
SymRun(
      SYM_MODE                  mode,                    //   IN:   which     chaining      mode
      UINT32                    keySizeInBits,           //   IN:   key    size   in   bits
      const  BYTE               *key,                    //   IN:   key    buffer
      const  BYTE               *iv,                     //   IN:   starting      IV,   or  NULL
      int                       encrypt,                 //   IN:   1  to   encrypt,      0  to  decrypt
      BYTE                      *dOut,                   //   OUT:  result
      const  BYTE               *dIn,                    //   IN:   data   buffer
      INT32                     dSize                    //   IN:   data   size
      )
{
      const  EVP_CIPHER         *cipher   =  SymCipher(mode,       keySizeInBits);
      EVP_CIPHER_CTX            *ctx;
      int                       outLen    =  0;
      int                       ok;

      if(cipher         ==  NULL)
              FAIL(FATAL_ERROR_INTERNAL);

      if((ctx       =   EVP_CIPHER_CTX_new())               ==  NULL)
              FAIL(FATAL_ERROR_ALLOCATION);

      ok    =   EVP_CipherInit_ex(ctx,          cipher,     NULL,     key,    iv,   encrypt)   ==  1
                &&   EVP_CIPHER_CTX_set_padding(ctx,             0)   ==  1
                &&   EVP_CipherUpdate(ctx,          dOut,      &outLen,      dIn,   (int)dSize)  ==  1
                &&   outLen       ==  (int)dSize;

      EVP_CIPHER_CTX_free(ctx);

      if(!ok)
              FAIL(FATAL_ERROR_INTERNAL);
      return    CRYPT_SUCCESS;
}

// B.11.2.3.     SymRunOfb()

// OFB, which needs a path of its own for one reason.  This interface hands
// the caller back the key stream register rather than the cipher text, and
// when the last block is short the register cannot be recovered from the data.
// So the last block -- whole or short -- is run as a block of zeros: an OFB
// key stream does not depend on its input, so that call returns the register
// in the clear and the final XOR is done here.
static   CRYPT_RESULT
SymRunOfb(
      UINT32                    keySizeInBits,           //   IN:   key    size   in   bits
      const  BYTE               *key,                    //   IN:   key    buffer
      BYTE                      *iv,                     //   IN/OUT:     the   OFB   register
      BYTE                      *dOut,                   //   OUT:  result
      const  BYTE               *dIn,                    //   IN:   data   buffer
      INT32                     dSize                    //   IN:   data   size
      )
{
      const  EVP_CIPHER         *cipher   =  SymCipher(SYM_MODE_OFB,         keySizeInBits);
      EVP_CIPHER_CTX            *ctx;
      BYTE                      zero[16];
      BYTE                      keyStream[16];
      INT32                     head;
      int                       outLen    =  0;
      int                       ok;
      int                       i;
      int                       n;

      if(cipher         ==  NULL)
              FAIL(FATAL_ERROR_INTERNAL);

      memset(zero,          0,   sizeof(zero));

      //    Everything      but   the   final     block,     which     may   be   short,     goes
      //    through     the   cipher     as   it   stands.
      head      =   ((dSize       %   16)   ==  0)   ?   dSize   -  16   :  dSize   &   ~((INT32)15);
      n         =   (int)(dSize        -   head);

      if((ctx       =   EVP_CIPHER_CTX_new())               ==  NULL)
              FAIL(FATAL_ERROR_ALLOCATION);

      ok    =   EVP_CipherInit_ex(ctx,          cipher,     NULL,     key,    iv,   1)   ==  1
                &&   EVP_CIPHER_CTX_set_padding(ctx,             0)   ==  1;
      if(ok     &&  head    >   0)
              ok    =   EVP_CipherUpdate(ctx,          dOut,      &outLen,      dIn,   (int)head)  ==  1
                        &&   outLen       ==  (int)head;
      if(ok)
              ok    =   EVP_CipherUpdate(ctx,          keyStream,       &outLen,     zero,   16)  ==  1
                        &&   outLen       ==  16;

      EVP_CIPHER_CTX_free(ctx);

      if(!ok)
              FAIL(FATAL_ERROR_INTERNAL);

      //    The   short     tail,    and    then     the   register      the   next     call    starts
      //    from.
      for(i     =   0;  i   <   n;   i++)
              dOut[head          +  i]   =   dIn[head        +  i]   ^   keyStream[i];
      memcpy(iv,            keyStream,       16);

      return    CRYPT_SUCCESS;
}""", 1),

    ("""LIB_EXPORT    BOOL
_cpri__SymStartup(
      void
)
{
      return  TRUE;
}""",
     """LIB_EXPORT    BOOL
_cpri__SymStartup(
      void
)
{
      UINT32        mode;
      UINT32        size;

      //    Fetch     the   ciphers      once.     The    TPM   calls     this    function       from
      //    _cpri__Startup()          as   well    as   from     the   engine's        initialisation,
      //    and   the   second      call    finds     them     already      here    and    asks   the
      //    provider        for   nothing.
      for(mode          =  0;   mode    <   SYM_MODE_COUNT;             mode++)
      {
              for(size          =  0;   size    <   SYM_KEY_SIZE_COUNT;              size++)
              {
                     if(s_symServer[mode][size].cipher                  !=  NULL)
                            continue;
                     s_symServer[mode][size].cipher                  =
                            EVP_CIPHER_fetch(OsslLibraryContext(),
                                                  s_symServer[mode][size].name,               NULL);
                     if(s_symServer[mode][size].cipher                  ==  NULL)
                            FAIL(FATAL_ERROR_INTERNAL);
              }
      }
      return  TRUE;
}

// B.11.3.1a.    OsslSymCleanup()

// Give back every cipher this file fetched.  Called from
// _cpri__StopCryptoUnits(), which is the engine's own shutdown entry point,
// and before the context they were fetched from is freed.
void
OsslSymCleanup(
      void
      )
{
      UINT32        mode;
      UINT32        size;

      for(mode          =  0;   mode    <   SYM_MODE_COUNT;             mode++)
      {
              for(size          =  0;   size    <   SYM_KEY_SIZE_COUNT;              size++)
              {
                     EVP_CIPHER_free(s_symServer[mode][size].cipher);
                     s_symServer[mode][size].cipher                  =  NULL;
              }
      }
}""", 1),
])

# =============================================== the eight compiled entry points
#
# One region rather than eight anchors, because every line between these two
# markers is being replaced: the eight AES bodies, and the #ifdef TPM_ALG_SM4
# block whose shim was removed above.  That block is never compiled -- ALG_SM4
# is ALG_NO at Implementation.h:85 -- and it is the only remaining place in the
# tree that spells AES_KEY, AES_set_encrypt_key, AES_encrypt or AES_decrypt.
# The two //% macro comments are kept: they document the DecryptCTR and
# DecryptOFB aliases, which are real #defines in CpriSym_fp.h.
region("src/OsslCryptoEngine/CpriSym.c",
       "// B.11.4.1. _cpri__AESEncryptCBC()",
       "#endif //% TPM_ALG_SM4",
       r"""// B.11.4.1.  _cpri__AESEncryptCBC()

// This function performs AES encryption in CBC chain mode. The input dIn buffer is encrypted into dOut.

// The input iv buffer is required to have a size equal to the block size (16 bytes). The dInSize is required to

// be a multiple of the block size.

// Return Value                                Meaning

// CRYPT_SUCCESS                               if success

// CRYPT_PARAMETER                             dInSize is not a multiple of the block size
LIB_EXPORT       CRYPT_RESULT
_cpri__AESEncryptCBC(
   BYTE                          *dOut,                     //   OUT:
   UINT32                            keySizeInBits,         //   IN:    key   size     in  bit
   BYTE                          *key,                      //   IN:    key   buffer.      The     size   of  this    buffer  in
                                                            //          bytes     is   (keySizeInBits         +   7)  /  8
   BYTE                          *iv,                       //   IN/OUT:      IV   for     decryption.
   UINT32                            dInSize,               //   IN:    data     size     (is  required       to  be  a  multiple
                                                            //          of   16   bytes)
   BYTE                          *dIn                       //   IN:    data     buffer
   )
{
   INT32                     dSize;                    //  Need     a   signed    version
   CRYPT_RESULT              result;

   pAssert(dOut              !=  NULL     &&  key      !=  NULL     &&  iv   !=   NULL     &&  dIn    !=  NULL);

   if(dInSize            ==  0)
           return        CRYPT_SUCCESS;

   pAssert(dInSize               <=   INT32_MAX);
   dSize      =  (INT32)dInSize;

   //    For     CBC,    the     data     size    must     be   an     even  multiple      of    the
   //    cipher       block      size
   if((dSize          %  16)     !=   0)
           return        CRYPT_PARAMETER;

   result     =  SymRun(SYM_MODE_CBC,           keySizeInBits,          key,   iv,   1,
                              dOut,     dIn,      dSize);
   if(result          !=  CRYPT_SUCCESS)
           return        result;

   //    The     IV  the     caller       gets     back    is   the     last  cipher      text     block,
   //    which        is  where      the  next     chained       call    has    to   start.
   memcpy(iv,            &dOut[dSize          -  16],      16);
   return        CRYPT_SUCCESS;
}

// B.11.4.2.    _cpri__AESDecryptCBC()

// This function performs AES decryption in CBC chain mode. The input dIn buffer is decrypted into dOut.

// The input iv buffer is required to have a size equal to the block size (16 bytes). The dInSize is required to

// be a multiple of the block size.

// Return Value                                Meaning

// CRYPT_SUCCESS                               if success

// CRYPT_PARAMETER                             dInSize is not a multiple of the block size
LIB_EXPORT       CRYPT_RESULT
_cpri__AESDecryptCBC(
   BYTE                            *dOut,                    //   OUT:   the      decrypted       data
   UINT32                             keySizeInBits,         //   IN:    key   size     in   bit
   BYTE                            *key,                     //   IN:    key   buffer.       The     size  of  this      buffer    in
                                                             //          bytes     is   (keySizeInBits         +    7)   /  8
   BYTE                            *iv,                      //   IN/OUT:      IV  for      decryption.        The  size       of  this
                                                             //          buffer       is   16   byte
   UINT32                             dInSize,               //   IN:    data     size
   BYTE                            *dIn                      //   IN:    data     buffer
   )
{
   INT32                     dSize;
   BYTE                      last[16];
   CRYPT_RESULT              result;

   pAssert(dOut              !=  NULL     &&  key      !=  NULL     &&  iv   !=   NULL     &&  dIn    !=  NULL);

   if(dInSize            ==  0)
           return        CRYPT_SUCCESS;

   pAssert(dInSize               <=   INT32_MAX);
   dSize      =  (INT32)dInSize;

   //    For     CBC,    the     data     size    must     be   an     even  multiple      of    the
   //    cipher       block      size
   if((dSize          %  16)     !=   0)
           return        CRYPT_PARAMETER;

   //    The     outgoing         IV  is  the     last     cipher      text     block,     and    dIn   and
   //    dOut        may     be  the     same     buffer,      so   it   is    taken     before      the
   //    cipher       runs      rather      than    after.
   memcpy(last,           &dIn[dSize         -  16],      16);

   result     =  SymRun(SYM_MODE_CBC,           keySizeInBits,          key,   iv,   0,
                              dOut,     dIn,      dSize);
   if(result          !=  CRYPT_SUCCESS)
           return        result;

   memcpy(iv,            last,     16);
   return        CRYPT_SUCCESS;
}

// B.11.4.3.      _cpri__AESEncryptCFB()

// This function performs AES encryption in CFB chain mode. The dOut buffer receives the values encrypted

// from dIn. The dInSize is not required to be a multiple of the block size.

// Return Value                                Meaning

// CRYPT_SUCCESS                               if success
LIB_EXPORT         CRYPT_RESULT
_cpri__AESEncryptCFB(
   BYTE                          *dOut,                      //   OUT:      the   encrypted
   UINT32                            keySizeInBits,          //   IN:    key    size   in   bit
   BYTE                          *key,                       //   IN:    key    buffer.       The    size   of  this    buffer  in
                                                             //          bytes      is   (keySizeInBits          +   7)  /  8
   BYTE                          *iv,                        //   IN/OUT:       IV   for     decryption.
   UINT32                            dInSize,                //   IN:    data      size
   BYTE                          *dIn                        //   IN:    data      buffer
   )
{
   INT32                     dSize;                    //  Need     a   signed    version
   int                       n;
   CRYPT_RESULT              result;

   pAssert(dOut              !=  NULL     &&  key      !=  NULL     &&  iv   !=   NULL     &&  dIn    !=  NULL);

   if(dInSize            ==  0)
           return        CRYPT_SUCCESS;

   pAssert(dInSize               <=   INT32_MAX);
   dSize      =  (INT32)dInSize;

   result     =  SymRun(SYM_MODE_CFB,           keySizeInBits,          key,   iv,   1,
                              dOut,     dIn,      dSize);
   if(result          !=  CRYPT_SUCCESS)
           return        result;

   //    The     IV  for     the     next     call    is   the     cipher      text     that     was    just
   //    produced,          and     where       that     ran     short      of  a   block     the     rest   is
   //    zero     --   this     interface         pads      the     register        out,     which      an   EVP
   //    CFB     context        does     not:    it   keeps       the     unused       key    stream      there.
   n     =  (int)(dSize            %  16);
   if(n       ==  0)
           n     =  16;
   memset(iv,            0,   16);
   memcpy(iv,            &dOut[dSize          -  n],    (size_t)n);
   return        CRYPT_SUCCESS;
}

// B.11.4.4.      _cpri__AESDecryptCFB()

// This function performs AES decryption in CFB chain mode. The dOut buffer receives the values decrypted

// from dIn. The dInSize is not required to be a multiple of the block size.

// Return Value                                Meaning

// CRYPT_SUCCESS                               if success
LIB_EXPORT         CRYPT_RESULT
_cpri__AESDecryptCFB(
   BYTE                          *dOut,                      //   OUT:      the   decrypted        data
   UINT32                            keySizeInBits,          //   IN:    key    size   in   bit
   BYTE                          *key,                       //   IN:    key    buffer.       The    size   of  this    buffer  in
                                                             //          bytes      is   (keySizeInBits          +   7)  /  8
   BYTE                          *iv,                        //   IN/OUT:       IV   for     decryption.
   UINT32                            dInSize,                //   IN:    data      size
   BYTE                          *dIn                        //   IN:    data      buffer
   )
{
   INT32                     dSize;
   BYTE                      last[16];
   int                       n;
   CRYPT_RESULT              result;

   pAssert(dOut              !=  NULL     &&  key      !=  NULL     &&  iv   !=   NULL     &&  dIn    !=  NULL);

   if(dInSize            ==  0)
           return        CRYPT_SUCCESS;

   pAssert(dInSize               <=   INT32_MAX);
   dSize      =  (INT32)dInSize;

   //    On   the     way     in  the     cipher      text     is   dIn,     and    dIn    and   dOut     may
   //    be   the     same     buffer,        so   the     bytes     the     outgoing        IV   is   made    of
   //    are     taken      before       the  cipher       runs.
   n     =  (int)(dSize            %  16);
   if(n       ==  0)
           n     =  16;
   memcpy(last,           &dIn[dSize         -  n],    (size_t)n);

   result     =  SymRun(SYM_MODE_CFB,           keySizeInBits,          key,   iv,   0,
                              dOut,     dIn,      dSize);
   if(result          !=  CRYPT_SUCCESS)
           return        result;

   memset(iv,            0,   16);
   memcpy(iv,            last,     (size_t)n);
   return        CRYPT_SUCCESS;
}

// B.11.4.5.  _cpri__AESEncryptCTR()

// This function performs AES encryption/decryption in CTR chain mode. The dIn buffer is encrypted into

// dOut. The input iv buffer is assumed to have a size equal to the AES block size (16 bytes). The iv will be

// incremented by the number of blocks (rounded up) that were encrypted.

// Return Value                                Meaning

// CRYPT_SUCCESS                               if success
LIB_EXPORT        CRYPT_RESULT
_cpri__AESEncryptCTR(
   BYTE                          *dOut,                      //   OUT:      the   encrypted        data
   UINT32                            keySizeInBits,          //   IN:    key    size   in   bit
   BYTE                          *key,                       //   IN:    key    buffer.       The    size   of  this    buffer  in
                                                             //          bytes      is   (keySizeInBits          +   7)  /  8
   BYTE                          *iv,                        //   IN/OUT:       IV   for     decryption.
   UINT32                            dInSize,                //   IN:    data      size
   BYTE                          *dIn                        //   IN:    data      buffer
   )
{
   INT32                     dSize;
   INT32                     blocks;
   int                       i;
   CRYPT_RESULT              result;

   pAssert(dOut              !=  NULL     &&  key      !=  NULL     &&  iv   !=   NULL     &&  dIn    !=  NULL);

   if(dInSize            ==  0)
           return        CRYPT_SUCCESS;

   pAssert(dInSize               <=   INT32_MAX);
   dSize      =  (INT32)dInSize;

   result     =  SymRun(SYM_MODE_CTR,           keySizeInBits,          key,   iv,   1,
                              dOut,     dIn,      dSize);
   if(result          !=  CRYPT_SUCCESS)
           return        result;

   //    The     counter        goes     back     stepped        once     for    every     block     this
   //    call     began      --   a   twenty      byte     call     steps     it   twice     --  big
   //    endian,         from     the     low     end,    which      is   what     this     interface
   //    has     always        done      and     not    what     a   cipher      context       would
   //    hand        back.
   for(blocks            =  (dSize      +  15)      /  16;    blocks     >   0;   blocks--)
           for(i      =  15;     i   >=  0;      i--)
                  if((iv[i]           +=  1)   !=   0)
                         break;

   return        CRYPT_SUCCESS;
}

// B.11.4.6.   _cpri__AESDecryptCTR()

// Counter mode decryption uses the same algorithm as encryption. The _cpri__AESDecryptCTR() function

// is implemented as a macro call to _cpri__AESEncryptCTR(). (skip)
//%  #define       _cpri__AESDecryptCTR(dOut,                         keySize,     key,     iv,   dInSize,      dIn)    \
//%                _cpri__AESEncryptCTR(                                                              \
//%                                                    ((BYTE     *)dOut),                            \
//%                                                    ((UINT32)keySize),                             \
//%                                                    ((BYTE     *)key),                             \
//%                                                    ((BYTE     *)iv),                              \
//%                                                    ((UINT32)dInSize),                             \
//%                                                    ((BYTE     *)dIn)                              \
//%                                                 )
//%

// B.11.4.7.   _cpri__AESEncryptECB()

// AES encryption in ECB mode. The data buffer is modified to contain the cipher text.

// Return Value                                Meaning

// CRYPT_SUCCESS                               if success

// CRYPT_PARAMETER                             dInSize is not a multiple of the block size
LIB_EXPORT         CRYPT_RESULT
_cpri__AESEncryptECB(
   BYTE                         *dOut,                       //   OUT:      encrypted        data
   UINT32                           keySizeInBits,           //   IN:    key    size   in   bit
   BYTE                         *key,                        //   IN:    key    buffer.       The    size   of  this    buffer  in
                                                             //          bytes      is   (keySizeInBits          +   7)  /  8
   UINT32                           dInSize,                 //   IN:    data      size
   BYTE                         *dIn                         //   IN:    clear      text    buffer
   )
{
   INT32                     dSize;

   pAssert(dOut              !=  NULL     &&  key      !=  NULL     &&  dIn      !=  NULL);

   if(dInSize            ==  0)
           return        CRYPT_SUCCESS;

   pAssert(dInSize               <=   INT32_MAX);
   dSize      =  (INT32)dInSize;

   //    For     ECB,    the     data     size    must     be   an     even  multiple      of    the
   //    cipher       block      size
   if((dSize          %  16)     !=   0)
           return        CRYPT_PARAMETER;

   //    No   IV  in     this     mode,      so   nothing        to   hand     back.
   return        SymRun(SYM_MODE_ECB,           keySizeInBits,          key,   NULL,     1,
                              dOut,     dIn,      dSize);
}

// B.11.4.8.   _cpri__AESDecryptECB()

// This function performs AES decryption using ECB (not recommended). The cipher text dIn is decrypted

// into dOut.

// Return Value                                Meaning

// CRYPT_SUCCESS                               if success

// CRYPT_PARAMETER                             dInSize is not a multiple of the block size
LIB_EXPORT       CRYPT_RESULT
_cpri__AESDecryptECB(
   BYTE                        *dOut,                        //   OUT:      the   clear     text    data
   UINT32                          keySizeInBits,            //   IN:    key    size   in   bit
   BYTE                        *key,                         //   IN:    key    buffer.       The    size   of  this    buffer  in
                                                             //          bytes      is   (keySizeInBits          +   7)  /  8
   UINT32                          dInSize,                  //   IN:    data      size
   BYTE                        *dIn                          //   IN:    cipher       text    buffer
   )
{
   INT32                     dSize;

   pAssert(dOut              !=  NULL     &&  key      !=  NULL     &&  dIn      !=  NULL);

   if(dInSize            ==  0)
           return        CRYPT_SUCCESS;

   pAssert(dInSize               <=   INT32_MAX);
   dSize      =  (INT32)dInSize;

   //    For     ECB,    the     data     size    must     be   an     even  multiple      of    the
   //    cipher       block      size
   if((dSize          %  16)     !=   0)
           return        CRYPT_PARAMETER;

   return        SymRun(SYM_MODE_ECB,           keySizeInBits,          key,   NULL,     0,
                              dOut,     dIn,      dSize);
}

// B.11.4.9.   _cpri__AESEncryptOFB()

// This function performs AES encryption/decryption in OFB chain mode. The dIn buffer is modified to

// contain the encrypted/decrypted data. The iv buffer is modified to contain the value of the OFB register

// after the last block.

// Return Value                                Meaning

// CRYPT_SUCCESS                               if success
LIB_EXPORT        CRYPT_RESULT
_cpri__AESEncryptOFB(
   BYTE                          *dOut,                      //   OUT:      the   encrypted/decrypted            data
   UINT32                            keySizeInBits,          //   IN:    key    size   in   bit
   BYTE                          *key,                       //   IN:    key    buffer.       The    size   of  this    buffer  in
                                                             //          bytes      is   (keySizeInBits          +   7)  /  8
   BYTE                          *iv,                        //   IN/OUT:       IV   for     decryption.        The   size   of  this
                                                             //          buffer       is   16   byte
   UINT32                            dInSize,                //   IN:    data      size
   BYTE                          *dIn                        //   IN:    data      buffer
   )
{
   INT32                     dSize;

   pAssert(dOut              !=  NULL     &&  key      !=  NULL     &&  iv   !=   NULL     &&  dIn    !=  NULL);

   if(dInSize            ==  0)
           return        CRYPT_SUCCESS;

   pAssert(dInSize               <=   INT32_MAX);
   dSize      =  (INT32)dInSize;

   //    OFB     hands       the  caller       back    its     register        rather      than     the
   //    cipher       text,     so   this     one    does     not    go   through       SymRun.
   return        SymRunOfb(keySizeInBits,                key,     iv,    dOut,     dIn,      dSize);
}

// B.11.4.10._cpri__AESDecryptOFB()

// OFB encryption and decryption use the same algorithms for both. The _cpri__AESDecryptOFB()
// function

// is implemented as a macro call to _cpri__AESEncrytOFB(). (skip)
//%#define        _cpri__AESDecryptOFB(dOut,keySizeInBits,                                   key,    iv,   dInSize,       dIn)      \
//%               _cpri__AESEncryptOFB                   (                                                     \
//%                                                         ((BYTE   *)dOut),                                  \
//%                                                         ((UINT32)keySizeInBits),                           \
//%                                                         ((BYTE   *)key),                                   \
//%                                                         ((BYTE   *)iv),                                    \
//%                                                         ((UINT32)dInSize),                                 \
//%                                                         ((BYTE   *)dIn)                                    \
//%                                                      )
//%
""")


# ================================================================ the plumbing
#
# OsslSymCleanup() is engine internal in exactly the way OsslHashCleanup() is,
# so it is declared in the same place and called from the same function, next
# to it: everything the engine took from the library goes back before the
# context it came from does.
patch("src/OsslCryptoEngine/OsslCryptoEngine.h", [
    ("""OSSL_LIB_CTX      *OsslLibraryContext(void);
void               OsslHashCleanup(void);""",
     """OSSL_LIB_CTX      *OsslLibraryContext(void);
void               OsslHashCleanup(void);
void               OsslSymCleanup(void);""", 1),

    # The header that declares the API this step removed.  Nothing in the tree
    # needs it any more, and leaving it in would be the one line still saying
    # this engine builds key schedules of its own.
    ("""#include     <openssl/aes.h>
#include     <openssl/evp.h>""",
     """#include     <openssl/evp.h>""", 1),
])

patch("src/OsslCryptoEngine/CpriCryptPri.c", [
    ("""OsslHashCleanup();
   if(s_default        !=  NULL)""",
     """OsslHashCleanup();
   OsslSymCleanup();
   if(s_default        !=  NULL)""", 1),
])

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
for sym in EVP_CIPHER_fetch EVP_CIPHER_free EVP_CIPHER_CTX_new \
           EVP_CIPHER_CTX_free EVP_CipherInit_ex EVP_CipherUpdate; do
    printf '%s\n' "$undef" | grep -qx "$sym" \
        || die "$sym is not an undefined symbol of libCryptoEngine.so"
done
# The low-level symmetric API is gone from what the engine asks for, and the
# built-in constructors were not what replaced it.
for sym in AES_set_encrypt_key AES_set_decrypt_key AES_encrypt AES_decrypt \
           AES_cbc_encrypt AES_ecb_encrypt EVP_aes_128_cbc EVP_aes_256_cbc \
           EVP_get_cipherbyname EVP_get_cipherbynid; do
    if printf '%s\n' "$undef" | grep -qx "$sym"; then
        die "$sym is still imported by libCryptoEngine.so"
    fi
done
# And gone from the sources, which is the half an import scan cannot see.
if grep -rnE '\bAES_KEY\b|\bAES_(set_(en|de)crypt_key|encrypt|decrypt)\b' \
        src/OsslCryptoEngine/ ; then
    die "the low-level symmetric API is still written in the engine sources"
fi

say "done"
printf 'built in %s\n' "$B"
