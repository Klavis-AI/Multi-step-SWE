#!/usr/bin/env bash
# Step 3 golden solution -- the engine's own library context, digests it
# fetches, and a sequence state that stopped being a picture of one.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/03-providers-and-fetch/solution/solve.sh; edit it here and regenerate,
# never edit the copy under tasks/.
#
# Run from /app on the tree step 2 left behind, with OpenSSL 3.0.17 installed
# as the system OpenSSL.  It edits the tree in place and builds it once to
# prove the edits compile; the verifier builds it again from scratch, so
# nothing here is load-bearing except the source changes.
#
#
# WHAT THIS DOES, AND WHY EACH PIECE IS NEEDED
#
# 3.0 is the release that took the algorithms away from the caller and gave
# them to providers.  Three things follow, and this step is all three:
#
#   the context           OSSL_LIB_CTX_new() at _cpri__InitCryptoUnits(), the
#                         default provider loaded into it by name, both given
#                         back at _cpri__StopCryptoUnits().  Passing NULL and
#                         letting the provider activate itself still works;
#                         it just means the engine computes with whatever the
#                         rest of the process left in the default context.
#
#   the fetch             EVP_MD_fetch() against that context, once, into a
#                         table the hash service keeps.  A fetched EVP_MD is
#                         reference counted and has to be given back, which is
#                         why the alternative -- a fetch and a free around
#                         every hash -- is the one worth avoiding.  The static
#                         EVP_sha256() pointers go.
#
#   the sequence state    and this is the part that is not bookkeeping.
#
# _cpri__StartHash(alg, sequence=TRUE, state) has always marshalled the
# digest's private bytes into the caller's buffer and freed the context, so
# that a sequence state is a *value*: the layers above relocate it, duplicate
# it and abandon it with memcpy and memset -- ContextSave, ContextLoad,
# FlushContext -- and every copy is a sequence of its own.  1.1.x let the
# engine do that, through EVP_MD_CTX_md_data() and the algorithm's
# app_datasize.
#
# At 3.0 there is nothing to marshal.  EVP_DigestInit_ex() routes even a
# static EVP_sha256() through the default provider, EVP_MD_CTX_get0_md_data()
# is NULL, EVP_MD_meth_get_app_datasize() is 0, and no OSSL_PARAM reads the
# running state out -- EVP_MD_CTX_gettable_params() on a SHA-256 context at
# 3.0.17 is empty.  The tree compiles unchanged and loses the whole sequence
# surface: hashseq 51/96, hashstate 21/135, hmac 27/99, e2ehashseq 0/40,
# e2eseqctx 3/154.
#
# The way back is to stop storing a picture and start storing a name.  The
# hash service keeps the contexts; the state holds an index into them; and
# nothing ever writes to a context that has been named -- an update copies the
# named context, updates the copy, and publishes the copy under a name of its
# own.  Value semantics come back because a name that is only ever read can be
# copied by copying it, which is exactly what memcpy does to it.
#
# The alternative that also passes the corpus is to keep the state a picture
# by going back to SHA256_Init/Update/Final, whose SHA256_CTX is a plain
# struct the engine could copy.  That is a step backwards into an interface
# 3.0 deprecated and step 8 removes, and the gate `no-low-level-digests`
# refuses it.
#
# What it costs: a context that has been named cannot be released early,
# because the engine is never told how many copies of the name exist.  They go
# back at _cpri__StopCryptoUnits(), with the fetched digests.  That is the
# price of a state the library will not show you, and it is bounded by how
# much hashing one boot of the TPM does.
set -euo pipefail

APP=${1:-/app}
cd "$APP"

say() { printf '\n== %s\n' "$*"; }
die() { printf '\nsolve: %s\n' "$*" >&2; exit 1; }

for f in src/OsslCryptoEngine/CpriHash.c src/OsslCryptoEngine/CpriCryptPri.c \
         src/OsslCryptoEngine/OsslCryptoEngine.h; do
    [ -f "$f" ] || die "$f is missing -- this is not the tree this solves"
done
grep -q 'RAND_priv_bytes' src/OsslCryptoEngine/CpriRNG.c \
    || die "the engine does not draw from the private pool -- step 2 did not run"

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


# ======================================================== the library context
#
# The engine's own, and not the one the process shares.  Nothing else in this
# binary uses libcrypto, but a TPM built as a library inside something larger
# is sharing its address space with code that can load providers, change the
# default properties, or configure the default context out from under it.  An
# engine that names its context is an engine whose algorithms are decided in
# _cpri__InitCryptoUnits().
#
# OPENSSL_init_crypto() stays.  It is what step 1 asked for, it is not
# deprecated at 3.0, and taking the library up explicitly and then making a
# context of your own are two different statements.
patch("src/OsslCryptoEngine/CpriCryptPri.c", [
    ("""#include  "OsslCryptoEngine.h"
static     void  Trap(const  char  *function,   int  line,         int  code);""",
     """#include  "OsslCryptoEngine.h"
#include  <openssl/provider.h>
static     void  Trap(const  char  *function,   int  line,         int  code);""", 1),

    ("""FAIL_FUNCTION            TpmFailFunction     =  (FAIL_FUNCTION)&Trap;""",
     """FAIL_FUNCTION            TpmFailFunction     =  (FAIL_FUNCTION)&Trap;

// The library context this engine works in and the provider it gets its
// algorithms from.  3.0 leaves both implicit unless it is told otherwise: a
// NULL context means the one the process shares, and the default provider is
// activated behind the caller's back on first use.  The engine takes neither.
// It makes a context of its own, loads the provider into it by name, and hands
// that context to every fetch, so what the engine computes with is decided
// here and not by whatever else is linked into the same process.
static     OSSL_LIB_CTX       *s_libCtx      =  NULL;
static     OSSL_PROVIDER      *s_default     =  NULL;

// B.6.3.0.     OsslLibraryContext()

// The library context every fetch in this engine is made against.  Engine
// internal: it is the argument the algorithm files pass to EVP_xxx_fetch().
OSSL_LIB_CTX      *
OsslLibraryContext(
      void
      )
{
      return     s_libCtx;
}""", 1),

    ("""if(OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_DIGESTS
                          |   OPENSSL_INIT_ADD_ALL_CIPHERS
                          |   OPENSSL_INIT_LOAD_CRYPTO_STRINGS,      NULL)     !=  1)
          FAIL(FATAL_ERROR_INTERNAL);""",
     """if(OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_DIGESTS
                          |   OPENSSL_INIT_ADD_ALL_CIPHERS
                          |   OPENSSL_INIT_LOAD_CRYPTO_STRINGS,      NULL)     !=  1)
          FAIL(FATAL_ERROR_INTERNAL);

   //  Then     the   context      this     engine      fetches      everything        from,   and   the
   //  provider        that     answers      those     fetches,      both     named      rather     than
   //  taken        by   default.
   if(s_libCtx         ==  NULL)
   {
          if((s_libCtx           =  OSSL_LIB_CTX_new())                        ==  NULL)
                 FAIL(FATAL_ERROR_ALLOCATION);
          if((s_default          =  OSSL_PROVIDER_load(s_libCtx,    "default"))  ==  NULL)
                 FAIL(FATAL_ERROR_INTERNAL);
   }""", 1),

    ("""LIB_EXPORT    void
_cpri__StopCryptoUnits(
   void
   )
{
   return;
}""",
     """LIB_EXPORT    void
_cpri__StopCryptoUnits(
   void
   )
{
   //  Everything        the   engine      asked      the   library      for   goes      back,     in   the
   //  order        it   was    taken:      the   fetched      algorithms        and   the    contexts
   //  the      hash    service       still     names,     then    the    provider,       then     the
   //  context        they     all   came      out   of.
   OsslHashCleanup();

   if(s_default        !=  NULL)
   {
          OSSL_PROVIDER_unload(s_default);
          s_default    =  NULL;
   }
   if(s_libCtx         !=  NULL)
   {
          OSSL_LIB_CTX_free(s_libCtx);
          s_libCtx     =  NULL;
   }
   return;
}""", 1),
])

# The two engine-internal declarations.  CryptoEngine.h is the engine's
# published interface and is not touched: this header is the one the engine's
# own files share and consumers never see.
patch("src/OsslCryptoEngine/OsslCryptoEngine.h", [
    ("""#include     "CryptoEngine.h" """.rstrip(),
     """#include     "CryptoEngine.h"

// Engine internal, between CpriCryptPri.c and the algorithm files that fetch
// from the library context it owns.  Neither of these is part of the engine's
// published interface: CryptoEngine.h is that, and this header is not shipped
// to consumers of the engine.
OSSL_LIB_CTX      *OsslLibraryContext(void);
void               OsslHashCleanup(void);""", 1),
])

# ========================================================== the sequence state
#
# Out: the picture and the two functions that took and restored it.  In: the
# name, the table of contexts the names refer to, and the digest table the
# fetch fills.
patch("src/OsslCryptoEngine/CpriHash.c", [
    # Out goes the only reader of the private state, and in comes the name it is
    # replaced by, the table the names refer to, and the table of fetched digests.
    ("""}  OSSL_HASH_STATE;

// How many bytes of private state the digest behind this context keeps.  This
// was ctxt->digest->ctx_size until 1.1.0 made both records opaque; it is the
// same number, read through the accessors the release publishes for it.
static    int
CtxDataSize(
     EVP_MD_CTX        *ctxt
)
{
     return    EVP_MD_meth_get_app_datasize(EVP_MD_CTX_md(ctxt));
}

// Temporary aliasing of SM3 to SHA256 until SM3 is available
#define     EVP_sm3_256  EVP_sha256

// B.8.3.    Static Functions""",
     """}  OSSL_HASH_STATE;

// A sequence state used to be a picture of the digest.  1.1.x let the engine
// read the private bytes behind an EVP_MD_CTX and copy them into the caller's
// buffer, and copy them back out again into a context of the same algorithm.
// 3.0 moved those bytes into the provider: EVP_MD_CTX_get0_md_data() is NULL
// for a provided digest, EVP_MD_meth_get_app_datasize() is 0, no parameter
// reads the running state out, and there is no picture left to take.
//
// So a sequence state stops being a picture and becomes a name.  The engine
// keeps the contexts the names refer to, and never writes to a context it has
// named: an update copies the named context, updates the copy, and publishes
// the copy under a name of its own.  That is what keeps a sequence state a
// value rather than a handle.  The layers above relocate, duplicate and abandon
// these structures with memcpy and memset -- ContextSave, ContextLoad and
// FlushContext all do -- and two copies of a name that is only ever read are
// two sequences that go their own way from the moment the copy was taken.
//
// What is paid for not being able to see the state is that a named context
// cannot be released early.  The engine is never told how many copies of a name
// exist, so a context lives until the hash service is torn down.
#define     OSSL_SEQ_MAGIC                   0x53455131                //  "SEQ1"
typedef     struct   {
     UINT32              magic;                                        //  OSSL_SEQ_MAGIC
     UINT32              epoch;                                        //  which table
     UINT32              slot;                                         //  which context in it
}  OSSL_SEQ_NAME;

static    EVP_MD_CTX     **s_seqTable       =  NULL;                   //  the named contexts
static    UINT32           s_seqCount       =  0;                      //  how many are named
static    UINT32           s_seqAlloc       =  0;                      //  how many fit
static    UINT32           s_seqEpoch       =  0;                      //  bumped by teardown

// The digests this engine hashes with, fetched once from the library context
// the engine owns and held until it shuts down.  A fetched EVP_MD is reference
// counted and has to be given back, so the alternative to a cache is a fetch
// and a free around every operation; this engine asks for the same short list
// for the life of the process, so it asks once.
typedef     struct   {
     TPM_ALG_ID          alg;                                          //  the TPM's name for it
     const  char        *name;                                         //  the provider's name
     EVP_MD             *md;                                           //  what the fetch returned
}  HASH_SERVER;

static    HASH_SERVER      s_hashServer[]   =  {
#ifdef    TPM_ALG_SHA1
     {  TPM_ALG_SHA1,          "SHA1",       NULL  },
#endif
#ifdef    TPM_ALG_SHA256
     {  TPM_ALG_SHA256,        "SHA256",     NULL  },
#endif
#ifdef    TPM_ALG_SHA384
     {  TPM_ALG_SHA384,        "SHA384",     NULL  },
#endif
#ifdef    TPM_ALG_SHA512
     {  TPM_ALG_SHA512,        "SHA512",     NULL  },
#endif
#ifdef    TPM_ALG_SM3_256
     //   Temporary aliasing of SM3 to SHA256 until SM3 is available
     {  TPM_ALG_SM3_256,       "SHA256",     NULL  },
#endif
};
#define     HASH_SERVER_COUNT     (sizeof(s_hashServer) / sizeof(s_hashServer[0]))

// B.8.3.    Static Functions""", 1),

    (""")
{
     switch  (hashAlg)
     {
#ifdef    TPM_ALG_SHA1
     case   TPM_ALG_SHA1:
            return   (EVP_MD  *)EVP_sha1();
            break;
#endif
#ifdef    TPM_ALG_SHA256
     case   TPM_ALG_SHA256:
            return   (EVP_MD  *)EVP_sha256();
            break;
#endif
#ifdef    TPM_ALG_SHA384
     case   TPM_ALG_SHA384:
            return   (EVP_MD  *)EVP_sha384();
            break;
#endif
#ifdef    TPM_ALG_SHA512
     case   TPM_ALG_SHA512:
            return   (EVP_MD  *)EVP_sha512();
            break;
#endif
#ifdef    TPM_ALG_SM3_256
     case   TPM_ALG_SM3_256:
            return   (EVP_MD  *)EVP_sm3_256();
            break;
#endif
    case  TPM_ALG_NULL:
          return     NULL;
    default:
          FAIL(FATAL_ERROR_INTERNAL);
    }
}

// B.8.3.2.  MarshalHashState()

// This function copies an OpenSSL() hash context into a caller provided buffer.

// Return Value                            Meaning""",
     """)
{
     UINT32       i;

     if(hashAlg        ==     TPM_ALG_NULL)
            return     NULL;
     for(i     =  0;   i  <  HASH_SERVER_COUNT;    i++)
     {
            if(s_hashServer[i].alg              ==  hashAlg)
                   return      s_hashServer[i].md;
     }
     FAIL(FATAL_ERROR_INTERNAL);
}

// B.8.3.2.  SeqPublish()

// This function hands a context to the table of named contexts and writes the
// name it was given into a caller provided buffer.  The context belongs to the
// table from here on: nothing writes to it again, and the teardown frees it.

// Return Value                            Meaning""", 1),

    ("""static    UINT16
MarshalHashState(
    EVP_MD_CTX                  *ctxt,                 //  IN:    Context    to  marshal
    BYTE                        *buf                   //  OUT:   The  buffer    that    will   receive  the
                                                       //         context.     This  buffer    is  at  least
                                                       //         MAX_HASH_STATE_SIZE        byte
    )
{
    //    make  sure     everything         will  fit
    pAssert(CtxDataSize(ctxt) <= OSSL_HASH_STATE_DATA_SIZE);

    //    Copy  the   context         data
    memcpy(buf, (void*) EVP_MD_CTX_md_data(ctxt), CtxDataSize(ctxt));

    return      (UINT16)CtxDataSize(ctxt);
}

// B.8.3.3.  GetHashState()

// This function will unmarshal a caller provided buffer into an OpenSSL() hash context. The function
// returns

// the number of bytes copied (which may be zero).
static    UINT16
GetHashState(
    EVP_MD_CTX                  *ctxt,                 //  OUT:   The  context       structure     to  receive  the
                                                       //         result   of    unmarshaling.
    TPM_ALG_ID                  algType,               //  IN:    The  hash    algorithm  selector
    BYTE                        *buf                   //  IN:    Buffer   containing     marshaled    hash     data
    )
{
    EVP_MD                      *evpmdAlgorithm        =   NULL;

    pAssert(ctxt         !=     NULL);

    EVP_MD_CTX_reset(ctxt);

    evpmdAlgorithm           =  GetHashServer(algType);
    if(evpmdAlgorithm           ==    NULL)
          return     0;

    //    This  also     allocates      the   ctxt->md_data
    if((EVP_DigestInit_ex(ctxt,                   evpmdAlgorithm,      NULL))    !=  1)
          FAIL(FATAL_ERROR_INTERNAL);

    pAssert(CtxDataSize(ctxt) < sizeof(ALIGNED_HASH_STATE));
    memcpy(EVP_MD_CTX_md_data(ctxt), buf, CtxDataSize(ctxt));
     return      (UINT16)CtxDataSize(ctxt);
}""",
     """static    UINT16
SeqPublish(
    EVP_MD_CTX                  *ctxt,                 //  IN:    Context    to  publish
    BYTE                        *buf                   //  OUT:   The  buffer    that    will   receive  the
                                                       //         name.     This  buffer    is  at  least
                                                       //         MAX_HASH_STATE_SIZE        byte
    )
{
    OSSL_SEQ_NAME                name;

    //    make  sure     everything         will  fit
    pAssert(sizeof(name) <= OSSL_HASH_STATE_DATA_SIZE);

    if(s_seqCount        ==  s_seqAlloc)
    {
           UINT32            grown    =  (s_seqAlloc  ==  0)  ?  64  :  (s_seqAlloc  *  2);
           EVP_MD_CTX      **bigger   =  OPENSSL_realloc(s_seqTable,
                                                         grown  *  sizeof(*s_seqTable));
           if(bigger         ==  NULL)
                  FAIL(FATAL_ERROR_ALLOCATION);
           s_seqTable        =  bigger;
           s_seqAlloc        =  grown;
    }
    s_seqTable[s_seqCount]      =  ctxt;

    name.magic   =  OSSL_SEQ_MAGIC;
    name.epoch   =  s_seqEpoch;
    name.slot    =  s_seqCount++;
    memcpy(buf, &name, sizeof(name));

    return      (UINT16)sizeof(name);
}

// B.8.3.3.  SeqContext()

// This function reads a name out of a caller provided buffer and returns the
// context it refers to.  The context is the caller's to read and not to write:
// a caller that wants to take the sequence further copies it first.

// Return Value                            Meaning

// NULL                                    the buffer holds no name this service issued
static    EVP_MD_CTX      *
SeqContext(
    BYTE                        *buf                   //  IN:    Buffer   holding      a   published     name
    )
{
    OSSL_SEQ_NAME                name;

    memcpy(&name, buf, sizeof(name));
    if(       name.magic         !=  OSSL_SEQ_MAGIC
           || name.epoch         !=  s_seqEpoch
           || name.slot          >=  s_seqCount)
           return     NULL;
    return      s_seqTable[name.slot];
}

// B.8.3.3a. SeqTableFree()

// This function gives back every context the table names and the table itself.
// The epoch moves on, so a name written into a state that outlived the teardown
// no longer refers to anything.
static    void
SeqTableFree(
    void
    )
{
    UINT32                       i;

    for(i     =  0;   i  <  s_seqCount;      i++)
           EVP_MD_CTX_free(s_seqTable[i]);
    OPENSSL_free(s_seqTable);
    s_seqTable        =  NULL;
    s_seqCount        =  0;
    s_seqAlloc        =  0;
    s_seqEpoch++;
}""", 1),

    # The fetch happens once, in the hash service's own startup, and what it
    # fetched is given back in a teardown entry point of its own.
    ("""     NUMBYTES                    osslStateSize             =     sizeof(OSSL_HASH_STATE);
//       int                           dataSize     =   sizeof(osslState->u.data);
     pAssert(cpriStateSize                  >=  osslStateSize);

     return       TRUE;
}""",
     """NUMBYTES                    osslStateSize             =     sizeof(OSSL_HASH_STATE);
     UINT32                      i;
//       int                           dataSize     =   sizeof(osslState->u.data);
     pAssert(cpriStateSize                  >=  osslStateSize);

     //   Fetch     the   digests     once.     The     TPM    calls     this     function        from
     //   _cpri__Startup()          as   well    as    from    the    engine's      initialisation,      and
     //   the   second       call    finds   them     already      here  and     asks    the    provider       for
     //   nothing.
     for(i     =  0;   i  <  HASH_SERVER_COUNT;    i++)
     {
            if(s_hashServer[i].md               !=  NULL)
                   continue;
            s_hashServer[i].md     =  EVP_MD_fetch(OsslLibraryContext(),
                                                   s_hashServer[i].name,     NULL);
            if(s_hashServer[i].md               ==  NULL)
                   FAIL(FATAL_ERROR_INTERNAL);
     }

     return       TRUE;
}

// B.8.4.1a.      OsslHashCleanup()

// Give back everything the hash service holds: the contexts it published names
// for, and the digests it fetched.  Called from _cpri__StopCryptoUnits(), which
// is the engine's own shutdown entry point.
void
OsslHashCleanup(
     void
     )
{
     UINT32       i;

     SeqTableFree();
     for(i     =  0;   i  <  HASH_SERVER_COUNT;    i++)
     {
            EVP_MD_free(s_hashServer[i].md);
            s_hashServer[i].md     =  NULL;
     }
}""", 1),

    # Relocating a sequence state copies the name; relocating a one-shot state
    # still duplicates the context behind it.
    ("""    pAssert(sizeof(i)     <=  sizeof(in->state));

    o->u.context  =  EVP_MD_CTX_new();
    if(o->u.context   ==  NULL)
           FAIL(FATAL_ERROR_ALLOCATION);
    EVP_MD_CTX_copy_ex(o->u.context,               i->u.context);
    o->copySize   =     i->copySize;
    out->hashAlg     =  in->hashAlg;""",
     """pAssert(sizeof(i)     <=  sizeof(in->state));

    if(i->copySize        >   0)
    {
           //  A   sequence     state   is  a   name,     and  a   name   is  copied     by   copying
           //  it.    The   context     the   name    refers    to   is    never    written     to,   so
           //  the    two   states     are    independent        of   each    other     from    here     on.
           memcpy(o->u.data,               i->u.data,        sizeof(OSSL_SEQ_NAME));
    }
    else if(i->copySize   <   0)
    {
           o->u.context  =  EVP_MD_CTX_new();
           if(o->u.context   ==  NULL)
                  FAIL(FATAL_ERROR_ALLOCATION);
           EVP_MD_CTX_copy_ex(o->u.context,               i->u.context);
    }
    o->copySize   =     i->copySize;
    out->hashAlg     =  in->hashAlg;""", 1),

    # _cpri__StartHash: a published context is not the caller's to free.
    ("""    CPRI_HASH_STATE           *hashState            //   OUT:  the   state      of  hash     stack.
    )
{
    EVP_MD_CTX         *localState       =  NULL;
   OSSL_HASH_STATE             *state     =     (OSSL_HASH_STATE           *)&hashState->state;
   BYTE                        *stateData         =     state->u.data;
   EVP_MD_CTX                  *context;""",
     """CPRI_HASH_STATE           *hashState            //   OUT:  the   state      of  hash     stack.
    )
{
   OSSL_HASH_STATE             *state     =     (OSSL_HASH_STATE           *)&hashState->state;
   BYTE                        *stateData         =     state->u.data;
   EVP_MD_CTX                  *context;""", 1),

    ("""          FAIL(FATAL_ERROR_ALLOCATION);
   if(sequence)
          localState    =   context;
   else
          state->u.context   =   context;""",
     """FAIL(FATAL_ERROR_ALLOCATION);
   if(!sequence)
          state->u.context   =   context;""", 1),

    ("""   if(EVP_DigestInit_ex(context,                        evpmdAlgorithm,         NULL)   !=  1)
          FAIL(FATAL_ERROR_INTERNAL);
   retVal     =     (CRYPT_RESULT)EVP_MD_CTX_size(context);

Cleanup:""",
     """if(EVP_DigestInit_ex(context,                        evpmdAlgorithm,         NULL)   !=  1)
          FAIL(FATAL_ERROR_INTERNAL);
   retVal     =     (CRYPT_RESULT)EVP_MD_CTX_get_size(context);

Cleanup:""", 1),

    ("""          {
                 if((state->copySize                 =  MarshalHashState(context,           stateData))         ==  0)
                 {
                        //  If   MarshalHashState              returns     a    negative    number,     it  is  an  error
                        //  code     and     not     a  hash   size   so   copy  the    error   code    to  be  the     return
                        //  from     this       function       and  set    the  actual     stateSize    to  zero.
                        retVal    =     state->copySize;
                        state->copySize              =  0;
                 }
                 //     Do  the  cleanup
                 EVP_MD_CTX_free(localState);
          }""",
     """{
                 //  The  table  takes  the  context  and  the  state  takes  the  name.
                 //  Nothing  is  freed  here:  a  published  context  is  the  hash
                 //  service's  until  it  shuts  down.
                 state->copySize              =  SeqPublish(context,          stateData);
          }""", 1),

    # _cpri__UpdateHash: read the named context, update a copy, publish the copy.
    ("""   BYTE                        *stateData         =     state->u.data;
   EVP_MD_CTX                  *context;
   CRYPT_RESULT                  retVal      =  CRYPT_SUCCESS;

    //   If    there    is     no  context,        return""",
     """BYTE                        *stateData         =     state->u.data;
   EVP_MD_CTX                  *context;
   EVP_MD_CTX                  *named;

    //   If    there    is     no  context,        return""", 1),

    ("""           context      =   localContext;
           if((retVal          =   GetHashState(context,           hashState->hashAlg,      stateData))     <=  0)
           {""",
     """context      =   localContext;
           //  The   named   context   is   read   and   not   written:   these   bytes   go
           //  into   a   copy   of   it,   and   the   copy   gets   a   name   of   its   own.
           if(      (named      =   SeqContext(stateData))       ==  NULL
                 || EVP_MD_CTX_copy_ex(context,           named)      !=  1)
           {""", 1),

    ("""           FAIL(FATAL_ERROR_INTERNAL);
    else     if(      state->copySize              >   0
                      &&    (retVal=        MarshalHashState(context,           stateData))     >=  0)
    {
           //  retVal       is     the  size       of  the  marshaled    data.  Make  sure  that    it  is  consistent
           //  by  ensuring        that        we  didn't     get  more  than   allowed
           if(retVal        <     state->copySize)
                  FAIL(FATAL_ERROR_INTERNAL);
               }
    EVP_MD_CTX_free(localContext);""",
     """FAIL(FATAL_ERROR_INTERNAL);
    else     if(state->copySize              >   0)
    {
           //  Publishing        hands       the  copy     to   the  table,    so   there    is  nothing
           //  left     here      to   free.
           state->copySize              =  SeqPublish(context,          stateData);
           localContext                 =  NULL;
    }
    EVP_MD_CTX_free(localContext);""", 1),

    # _cpri__CompleteHash: finishing reads the named context and leaves it whole.
    ("""    EVP_MD_CTX                    *context;
    UINT16                         retVal;""",
     """EVP_MD_CTX                    *context;
    EVP_MD_CTX                    *named;
    UINT16                         retVal;""", 1),

    ("""           context      =   localState;
           if((retVal          =   GetHashState(context,           hashState->hashAlg,      stateData))     <=  0)
                  goto     Cleanup;
    }
    else
           context      =  state->u.context;

     hLen     =  EVP_MD_CTX_size(context);
     if((unsigned)hLen                >  dOutSize)""",
     """context      =   localState;
           //  Finishing        reads     the   named    context      and    leaves     it   as   it   was:    a
           //  copy     of  a   state     that     has    been    completed       can   still     be    taken
           //  further,      which     is   what    a   context      blob    loaded     twice     does.
           if(      (named      =   SeqContext(stateData))       ==  NULL
                 || EVP_MD_CTX_copy_ex(context,           named)      !=  1)
           {
                  retVal       =  0;
                  goto     Cleanup;
           }
    }
    else
           context      =  state->u.context;

     hLen     =  EVP_MD_CTX_get_size(context);
     if((unsigned)hLen                >  dOutSize)""", 1),

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
# not land is not mistaken for a solve that worked.  See step 2's script for why
# the `nm` output is collected into a variable before anything greps it.
E="$B/OsslCryptoEngine/libCryptoEngine.so"
undef=$(nm -D --undefined-only "$E" | awk '{print $NF}' | sed 's/@.*//')
for sym in OSSL_LIB_CTX_new OSSL_LIB_CTX_free OSSL_PROVIDER_load \
           OSSL_PROVIDER_unload EVP_MD_fetch EVP_MD_free EVP_MD_CTX_copy_ex; do
    printf '%s\n' "$undef" | grep -qx "$sym" \
        || die "$sym is not an undefined symbol of libCryptoEngine.so"
done
# The engine no longer names a built-in digest, no longer reads a context's
# private state, and did not reach for the low-level digest API to get value
# semantics back.
for sym in EVP_sha1 EVP_sha256 EVP_sha384 EVP_sha512 EVP_get_digestbyname \
           EVP_MD_CTX_md EVP_MD_CTX_get0_md_data EVP_MD_meth_get_app_datasize \
           SHA1_Init SHA256_Init SHA512_Init; do
    if printf '%s\n' "$undef" | grep -qx "$sym"; then
        die "$sym is still imported by libCryptoEngine.so"
    fi
done

say "done"
printf 'built in %s\n' "$B"
