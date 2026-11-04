#!/usr/bin/env bash
# Step 7 golden solution -- the two places this engine still configures an
# OpenSSL operation by calling a setter per setting, rewritten to hand the
# operation its parameters as a list.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/07-params-not-ctrls/solution/solve.sh; edit it here and regenerate,
# never edit the copy under tasks/.
#
# Run from /app on the tree step 6 left behind, with OpenSSL 3.2.5 installed as
# the system OpenSSL.  It edits the tree in place and builds it once to prove
# the edits compile; the verifier builds it again from scratch, so nothing here
# is load-bearing except the source changes.
#
#
# WHAT THIS DOES, AND WHY EACH PIECE IS NEEDED
#
# Steps 3 to 6 moved this engine onto a library context of its own, onto
# fetched implementations, and onto a named property query.  What they did not
# touch is how an operation is told what to do once it exists.  Two places
# still do that the 1.0.2 way, one setter call per setting:
#
#   CpriECC.c EccCurveInit()      EC_GROUP_new_curve_GFp() makes a group out of
#                                 p, a and b; EC_POINT_set_affine_coordinates_GFp()
#                                 places a point; EC_GROUP_set_generator()
#                                 fits that point to the group as its
#                                 generator with an order and a cofactor.
#                                 Three calls and a temporary EC_POINT to say
#                                 one thing: here is a curve.
#
#   CpriRSA.c _cpri__EncryptRSA() and _cpri__ValidateSignatureRSA() between
#                                 them make nine EVP_PKEY_CTX_set_rsa_*()
#                                 calls -- padding, OAEP digest, MGF1 digest,
#                                 OAEP label, PSS salt length, signature
#                                 digest.  Every one of them is a typed
#                                 wrapper that packs its argument into a
#                                 one-element OSSL_PARAM list and calls
#                                 EVP_PKEY_CTX_set_params().
#
# The 3.x spelling of both is a parameter list.  EC_GROUP_new_from_params()
# takes the whole curve at once, on a library context and with a property
# query, which the three-call form had nowhere to put.  EVP_PKEY_encrypt_init_ex()
# and EVP_PKEY_verify_init_ex() take the operation's settings at init, which is
# where the ctrl calls were applied anyway.
#
# THREE THINGS HERE ARE NOT A RENAME
#
#   the generator     The list has a key for the generator, and it is a point
#                     ENCODING, not a pair of coordinates.  There is no
#                     EC_POINT to build it from -- the group does not exist
#                     yet -- so it is built from the curve table directly:
#                     uncompressed, 0x04 and then x and y, each left padded to
#                     the field's own width.  That is byte for byte what
#                     EC_POINT_point2oct() would have produced from the point
#                     the old code made, and building it this way is what
#                     removes the temporary EC_POINT rather than moving it.
#
#   the curve's name  This tree builds three curves and hands all three over
#                     as fully explicit parameters.  OpenSSL recognises two of
#                     them: the group it returns for NIST P256 is named
#                     prime256v1 and for NIST P384 secp384r1, where the
#                     hand-built group was NID_undef, and for NIST P256 it is
#                     a different EC_METHOD as well.  The arithmetic is
#                     identical -- NOTES-authoring.md section 11.1.4 has
#                     64 rounds per curve of scalar multiplication, point
#                     addition and coordinate read-back with zero
#                     disagreements -- and BN P256 stays unnamed because
#                     OpenSSL has no NID for it.  Passing OSSL_PKEY_PARAM_GROUP_NAME
#                     for the two it would recognise reproduces the corpus too
#                     and is the shortcut this step does not take: the curve
#                     table is what the TPM was told to implement, and a group
#                     built from a name is a group built from OpenSSL's copy
#                     of it.
#
#   the salt length   RSA_PSS_SALTLEN_AUTO is -2 and goes into the list as the
#                     number -2.  The list also accepts the string "digest",
#                     which is a DIFFERENT sentinel: it says the salt is the
#                     digest's own length.  The two agree on every SHA1 row of
#                     the corpus, because SHA1's length is 20 and the salt is
#                     20, and part company on SHA256 and SHA384.
#
#   the label         EVP_PKEY_CTX_set0_rsa_oaep_label() takes the buffer, so
#                     the old code had to OPENSSL_memdup() the caller's label
#                     to have a buffer to give away.  A list is READ by the
#                     provider, which makes its own copy, so the list points
#                     at the caller's buffer and there is nothing to allocate
#                     and nothing to free.  The memdup goes with the set0.
#
# WHAT IS NOT DONE HERE
#
# This is not a deprecation sweep.  EC_POINT_set_affine_coordinates_GFp() is
# called twice in this file; only the call inside EccCurveInit() goes, because
# only that one was part of building a curve.  The other places an ordinary
# point and is step 8's business.  Nothing else in the tree changes, and the
# engine's answers do not change: the corpus is what says so.

set -euo pipefail

APP=${APP:-/app}
cd "$APP"

say()  { printf '\n== %s\n' "$*"; }
die()  { printf 'solve: %s\n' "$*" >&2; exit 1; }

for f in src/OsslCryptoEngine/CpriECC.c src/OsslCryptoEngine/CpriRSA.c \
         src/OsslCryptoEngine/CpriCryptPri.c \
         src/OsslCryptoEngine/OsslCryptoEngine.h cmake/CMakeLists.txt; do
    [ -f "$f" ] || die "$f is missing -- this is not the tree step 6 produced"
done

# Steps 3, 5 and 6 have to be here already: the new calls take the context and
# the query those steps created, and the RSA rewrite edits call sites step 5
# built.
grep -q 'OsslLibraryContext' src/OsslCryptoEngine/CpriECC.c \
    || die "src/OsslCryptoEngine/CpriECC.c does not use the engine's library context -- step 3 has not been applied"
grep -q 'EVP_PKEY_CTX_new_from_pkey' src/OsslCryptoEngine/CpriRSA.c \
    || die "src/OsslCryptoEngine/CpriRSA.c does not build an EVP_PKEY_CTX -- step 5 has not been applied"
grep -q 'OsslPropertyQuery' src/OsslCryptoEngine/CpriCryptPri.c \
    || die "src/OsslCryptoEngine/CpriCryptPri.c has no property query -- step 6 has not been applied"

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


# ============================================================ the curve
#
# EccCurveInit() keeps its seven BIGNUMs -- the list wants the field, the
# coefficients, the order and the cofactor as numbers, and BnFrom2B() is still
# how this file turns the curve table into numbers.  What it loses is the
# temporary EC_POINT, which existed only to carry two coordinates into
# EC_GROUP_set_generator().
patch("src/OsslCryptoEngine/CpriECC.c", [
    ("""EC_GROUP                           *group         =  NULL;
        EC_POINT                           *P   =     NULL;
        BN_CTX                             *context;""",
     """EC_GROUP                           *group         =  NULL;\n"""
     """   OSSL_PARAM_BLD                     *bld           =  NULL;\n"""
     """   OSSL_PARAM                         *params        =  NULL;\n"""
     """   BN_CTX                             *context;""", 1),

    # The generator's encoding is built in a buffer on the stack.  Every curve
    # this Implementation.h names has a field no wider than MAX_ECC_KEY_BYTES,
    # and the bound is checked below rather than assumed.
    ("""BIGNUM                             *bnH;
        int                                 ok     =  FALSE;""",
     """BIGNUM                             *bnH;\n"""
     """   BYTE                                generator[1   +  2   *  MAX_ECC_KEY_BYTES];\n"""
     """   size_t                              coordSize;\n"""
     """   size_t                              generatorSize;\n"""
     """   int                                 ok     =  FALSE;""", 1),

    ("""//   initialize               EC     group,     associate    a  generator        point    and      initialize      the   point
        //   from         the     parameter           data
        ok   =     (       (group         =   EC_GROUP_new_curve_GFp(bnP,                bnA,     bnB,     groupContext))        !=  NULL
                      &&   (P     =   EC_POINT_new(group))              !=   NULL
                      &&   EC_POINT_set_affine_coordinates_GFp(group,                             P,    bnX,     bnY,  groupContext)
                      &&   EC_GROUP_set_generator(group,                     P,    bnN,  bnH)
                   );
        Cleanup:
        if   (!ok         &&     group       !=  NULL)""",
     """//   The     three      calls      that     used     to  be   here      --   make      a  group\n"""
     """      //   from         p,    a   and     b,   place      a  point,       fit   the     point       to   the\n"""
     """      //   group        as   its    generator          --    are    one     statement          now.    The\n"""
     """      //   curve        arrives          whole,       on   this     engine's          own     context        and\n"""
     """      //   with         this      engine's          own     property         query,       neither       of\n"""
     """      //   which        the    old     form      had     anywhere         to    put.\n"""
     """      //\n"""
     """      //   The     generator            travels        as   a   point      encoding         because        that\n"""
     """      //   is    the     form      the    list     has     a  key     for:    uncompressed,             0x04\n"""
     """      //   and     then     the    two     field      elements,          each      left    padded        to   the\n"""
     """      //   field's          own     width.       It   is    built      from       the    curve      table\n"""
     """      //   rather        than      from      a  point      because        the     group      it   would       be\n"""
     """      //   a    point      of   does     not     exist      yet.\n"""
     """      coordSize          =   curveData->p->size;\n"""
     """      generatorSize          =   1   +  2   *  coordSize;\n"""
     """      if(       curveData->x->size              >  coordSize\n"""
     """            ||  curveData->y->size              >  coordSize\n"""
     """            ||  generatorSize                   >  sizeof(generator))\n"""
     """             goto      Cleanup;\n"""
     """      memset(generator,              0,   generatorSize);\n"""
     """      generator[0]           =   POINT_CONVERSION_UNCOMPRESSED;\n"""
     """      memcpy(&generator[1        +  coordSize          -  curveData->x->size],\n"""
     """             curveData->x->buffer,                curveData->x->size);\n"""
     """      memcpy(&generator[generatorSize                -  curveData->y->size],\n"""
     """             curveData->y->buffer,                curveData->y->size);\n"""
     """\n"""
     """      ok   =     (       (bld      =   OSSL_PARAM_BLD_new())          !=  NULL\n"""
     """                    &&   OSSL_PARAM_BLD_push_utf8_string(bld,\n"""
     """                                 OSSL_PKEY_PARAM_EC_FIELD_TYPE,\n"""
     """                                 SN_X9_62_prime_field,          0)\n"""
     """                    &&   OSSL_PARAM_BLD_push_BN(bld,           OSSL_PKEY_PARAM_EC_P,        bnP)\n"""
     """                    &&   OSSL_PARAM_BLD_push_BN(bld,           OSSL_PKEY_PARAM_EC_A,        bnA)\n"""
     """                    &&   OSSL_PARAM_BLD_push_BN(bld,           OSSL_PKEY_PARAM_EC_B,        bnB)\n"""
     """                    &&   OSSL_PARAM_BLD_push_octet_string(bld,\n"""
     """                                 OSSL_PKEY_PARAM_EC_GENERATOR,\n"""
     """                                 generator,         generatorSize)\n"""
     """                    &&   OSSL_PARAM_BLD_push_BN(bld,           OSSL_PKEY_PARAM_EC_ORDER,    bnN)\n"""
     """                    &&   OSSL_PARAM_BLD_push_BN(bld,           OSSL_PKEY_PARAM_EC_COFACTOR, bnH)\n"""
     """                    &&   (params        =   OSSL_PARAM_BLD_to_param(bld))       !=  NULL\n"""
     """                    &&   (group         =   EC_GROUP_new_from_params(params,\n"""
     """                                                     OsslLibraryContext(),\n"""
     """                                                     OsslPropertyQuery()))      !=  NULL\n"""
     """                 );\n"""
     """Cleanup:\n"""
     """      OSSL_PARAM_free(params);\n"""
     """      OSSL_PARAM_BLD_free(bld);\n"""
     """      if   (!ok         &&     group       !=  NULL)""", 1),

    # The temporary point went with the three calls.
    ("""if(P       !=     NULL)
                EC_POINT_free(P);
        BN_CTX_end(context);""",
     """BN_CTX_end(context);""", 1),
])


# ============================================================ RSA encryption
#
# The scheme switch decided a padding NUMBER for a ctrl.  A list carries the
# padding as the provider's own name for it, so the switch decides that
# instead and the rest of the function reads the same.
patch("src/OsslCryptoEngine/CpriRSA.c", [
    ("""EVP_MD                    *md;
        BYTE                      *oaepLabel;
        size_t                     outLen;
        size_t                     lSize;
        INT32                      hLen;
        int                        padding;
        int                        worked;""",
     """EVP_MD                    *md;\n"""
     """   OSSL_PARAM                 params[5];\n"""
     """   size_t                     nParams        =     0;\n"""
     """   const     char            *mdName;\n"""
     """   const     char            *padName;\n"""
     """   size_t                     outLen;\n"""
     """   size_t                     lSize;\n"""
     """   INT32                      hLen;\n"""
     """   int                        worked;""", 1),

    ("""padding       =   RSA_PKCS1_PADDING;
        break;""",
     """padName       =   OSSL_PKEY_RSA_PAD_MODE_PKCSV15;\n"""
     """         break;""", 1),

    ("""padding       =   RSA_PKCS1_OAEP_PADDING;
        break;""",
     """padName       =   OSSL_PKEY_RSA_PAD_MODE_OAEP;\n"""
     """         break;""", 1),

    ("""if(        (pkey        =   RsaPublicKey(key))            ==   NULL
           ||   (ctx     =   EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),
                                                    pkey,
                                                    OsslPropertyQuery()))  ==   NULL
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
   }""",
     """if(        (pkey        =   RsaPublicKey(key))            ==   NULL\n"""
     """           ||   (ctx     =   EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),\n"""
     """                                                    pkey,\n"""
     """                                                    OsslPropertyQuery()))  ==   NULL)\n"""
     """           FAIL(FATAL_ERROR_INTERNAL);\n"""
     """\n"""
     """   //   The     padding        scheme       and     everything           it   needs      are     one\n"""
     """   //   list,       applied         at   init.       The     ctrls      were      applied         after\n"""
     """   //   init      and     in   this      order,       one     per    setting,          each      of   them\n"""
     """   //   building         a  one     element         list      of   its    own.\n"""
     """   params[nParams++]           =   OSSL_PARAM_construct_utf8_string(\n"""
     """                                           OSSL_PKEY_PARAM_PAD_MODE,\n"""
     """                                           (char    *)padName,        0);\n"""
     """   if(padType         ==     TPM_ALG_OAEP)\n"""
     """   {\n"""
     """         //      OaepEncode()               used    one     hash     for    the    label      and     the   same\n"""
     """         //      one     for    MGF1,      so    both     are    named      here.\n"""
     """         md      =   OsslHashMd(hashAlg);\n"""
     """         if(md         ==   NULL)\n"""
     """                 FAIL(FATAL_ERROR_INTERNAL);\n"""
     """         mdName          =   EVP_MD_get0_name(md);\n"""
     """         params[nParams++]             =   OSSL_PARAM_construct_utf8_string(\n"""
     """                                                   OSSL_ASYM_CIPHER_PARAM_OAEP_DIGEST,\n"""
     """                                                   (char    *)mdName,       0);\n"""
     """         params[nParams++]             =   OSSL_PARAM_construct_utf8_string(\n"""
     """                                                   OSSL_ASYM_CIPHER_PARAM_MGF1_DIGEST,\n"""
     """                                                   (char    *)mdName,       0);\n"""
     """         if(label            !=   NULL)\n"""
     """         {\n"""
     """                 //      OaepEncode()               hashed       strlen(label)             +  1   bytes    --\n"""
     """                 //      the      terminating             NUL     is   part      of   the    label      that   was\n"""
     """                 //      recorded,             so    it  is    part     of   the    label      given      here.\n"""
     """                 //      The       list      is   read      and     the    provider          copies       what    it\n"""
     """                 //      reads,        so    it  points        at    the    caller's          buffer.        set0\n"""
     """                 //      TOOK       a  buffer,        which      is   why     there      used      to  be   a\n"""
     """                 //      duplicate           here      to   give     it.\n"""
     """                 lSize        =   strlen(label)           +   1;\n"""
     """                 params[nParams++]              =   OSSL_PARAM_construct_octet_string(\n"""
     """                                                           OSSL_ASYM_CIPHER_PARAM_OAEP_LABEL,\n"""
     """                                                           (void     *)label,       lSize);\n"""
     """         }\n"""
     """   }\n"""
     """   params[nParams]             =   OSSL_PARAM_construct_end();\n"""
     """\n"""
     """   if(EVP_PKEY_encrypt_init_ex(ctx,             params)       <=   0)\n"""
     """           FAIL(FATAL_ERROR_INTERNAL);""", 1),


# ============================================================ RSA verification
#
# Same rewrite, one function down.  The signature digest is common to both
# schemes and PSS adds three more settings; all of them go into one list and
# the list is applied at init.
    ("""EVP_MD                *md;
        BYTE                  *der;
        INT32                  derSize;
        INT32                  hashSize;
        int                    worked;""",
     """EVP_MD                *md;\n"""
     """      OSSL_PARAM             params[5];\n"""
     """      size_t                 nParams      =   0;\n"""
     """      const  char           *mdName;\n"""
     """      int                    saltLen;\n"""
     """      BYTE                  *der;\n"""
     """      INT32                  derSize;\n"""
     """      INT32                  hashSize;\n"""
     """      int                    worked;""", 1),

    ("""if(        (pkey     =  RsaPublicKey(key))           ==  NULL
              || (ctx    =  EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),
                                                    pkey,
                                                    OsslPropertyQuery()))  ==   NULL
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
            FAIL(FATAL_ERROR_INTERNAL);""",
     """if(        (pkey     =  RsaPublicKey(key))           ==  NULL\n"""
     """              || (ctx    =  EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),\n"""
     """                                                    pkey,\n"""
     """                                                    OsslPropertyQuery()))  ==   NULL\n"""
     """              || md    ==  NULL)\n"""
     """              FAIL(FATAL_ERROR_INTERNAL);\n"""
     """\n"""
     """      //  The   signature        hash    is   common       to   both    schemes        and   PSS\n"""
     """      //  adds    three     more.       All   of   them    go   into    one    list      and   the\n"""
     """      //  list    is   applied       at   init,     where      the   ctrls     were     applied\n"""
     """      //  from.\n"""
     """      mdName        =  EVP_MD_get0_name(md);\n"""
     """      params[nParams++]        =  OSSL_PARAM_construct_utf8_string(\n"""
     """                                         OSSL_SIGNATURE_PARAM_DIGEST,\n"""
     """                                         (char   *)mdName,      0);\n"""
     """      if(scheme     ==  TPM_ALG_RSAPSS)\n"""
     """      {\n"""
     """            //  saltSize      0   meant     "whatever       the   signature       says"     to\n"""
     """            //  PssDecode(),      which     only    compared      a   recovered       salt\n"""
     """            //  length     when    it   had   been    told    one.     That    is\n"""
     """            //  RSA_PSS_SALTLEN_AUTO,        which     is   -2,    and   -2   is   what\n"""
     """            //  goes    in   the   list.     The   list    would     also    take    the\n"""
     """            //  string      "digest",       and   that    is   a   different       sentinel:\n"""
     """            //  it  says    the   salt    is   the   digest's       own   length,     which\n"""
     """            //  is  the   same    thing     only    when    it  happens      to   be.    A\n"""
     """            //  non-zero      saltSize      maps    to   itself.      MGF1    used    the\n"""
     """            //  signature       hash,    as   it  did.\n"""
     """            saltLen       =  (saltSize     !=  0)  ?  (int)saltSize\n"""
     """                                                   :  RSA_PSS_SALTLEN_AUTO;\n"""
     """            params[nParams++]          =  OSSL_PARAM_construct_utf8_string(\n"""
     """                                                 OSSL_SIGNATURE_PARAM_PAD_MODE,\n"""
     """                                                 (char   *)OSSL_PKEY_RSA_PAD_MODE_PSS,       0);\n"""
     """            params[nParams++]          =  OSSL_PARAM_construct_utf8_string(\n"""
     """                                                 OSSL_SIGNATURE_PARAM_MGF1_DIGEST,\n"""
     """                                                 (char   *)mdName,      0);\n"""
     """            params[nParams++]          =  OSSL_PARAM_construct_int(\n"""
     """                                                 OSSL_SIGNATURE_PARAM_PSS_SALTLEN,\n"""
     """                                                 &saltLen);\n"""
     """      }\n"""
     """      else\n"""
     """            params[nParams++]          =  OSSL_PARAM_construct_utf8_string(\n"""
     """                                                 OSSL_SIGNATURE_PARAM_PAD_MODE,\n"""
     """                                                 (char   *)OSSL_PKEY_RSA_PAD_MODE_PKCSV15,   0);\n"""
     """      params[nParams]        =  OSSL_PARAM_construct_end();\n"""
     """\n"""
     """      if(EVP_PKEY_verify_init_ex(ctx,          params)       <=  0)\n"""
     """            FAIL(FATAL_ERROR_INTERNAL);""", 1),
])

print("solve: %d source edits applied" % EDITS)
PY

say "build"
B=${BUILD_DIR:-$(mktemp -d)}

cmake -S "$APP/cmake" -B "$B" -DSPEC_VERSION=116 > "$B/configure.log" 2>&1 \
    || { tail -40 "$B/configure.log" >&2; die "cmake configure failed"; }
cmake --build "$B" -j"${JOBS:-7}" > "$B/build.log" 2>&1 \
    || { tail -60 "$B/build.log" >&2; die "build failed"; }

for t in platform/libplatform.so OsslCryptoEngine/libCryptoEngine.so \
         tpm/libTPM.so Simulator; do
    [ -e "$B/$t" ] || die "$t was not built"
done

say "assert"

ENG=$B/OsslCryptoEngine/libCryptoEngine.so
undef=$(nm -D --undefined-only "$ENG" | awk '{print $NF}' | sed 's/@.*//')

# What the binary must now ask libcrypto for.  A tree with the calls in a file
# the build does not compile, or in a branch the linker dropped, fails here.
for sym in EC_GROUP_new_from_params OSSL_PARAM_BLD_new OSSL_PARAM_BLD_to_param \
           EVP_PKEY_encrypt_init_ex EVP_PKEY_verify_init_ex; do
    printf '%s\n' "$undef" | grep -qx "$sym" \
        || die "$sym is not an undefined symbol of libCryptoEngine.so"
done

# And what it must no longer ask for.  The two EC calls are the curve built by
# hand; the six RSA setters are the ctrl route.  EVP_PKEY_CTX_ctrl and
# EVP_PKEY_CTX_ctrl_str are the generic form of the same thing and this tree
# has never called either -- they are listed so that a rewrite which only
# deletes the typed names does not pass.
for sym in EC_GROUP_new_curve_GFp EC_GROUP_set_generator \
           EVP_PKEY_CTX_set_rsa_padding EVP_PKEY_CTX_set_rsa_oaep_md \
           EVP_PKEY_CTX_set_rsa_mgf1_md EVP_PKEY_CTX_set0_rsa_oaep_label \
           EVP_PKEY_CTX_set_rsa_pss_saltlen EVP_PKEY_CTX_set_signature_md \
           EVP_PKEY_CTX_ctrl EVP_PKEY_CTX_ctrl_str; do
    if printf '%s\n' "$undef" | grep -qx "$sym"; then
        die "$sym is still an undefined symbol of libCryptoEngine.so"
    fi
done

# The salt length goes in as a number.  The digest sentinel is the other
# spelling and it means something else; a tree that reaches for it has renamed
# the call and changed what it says.  Grepped for the macro rather than for the
# string, because the string appears in the comment that explains why it is not
# used.
if grep -n 'OSSL_PKEY_RSA_PSS_SALT_LEN_DIGEST' src/OsslCryptoEngine/CpriRSA.c ; then
    die "CpriRSA.c spells the PSS salt length as the digest sentinel"
fi
grep -q 'RSA_PSS_SALTLEN_AUTO' src/OsslCryptoEngine/CpriRSA.c \
    || die "CpriRSA.c no longer names RSA_PSS_SALTLEN_AUTO"
grep -B1 'OSSL_SIGNATURE_PARAM_PSS_SALTLEN' src/OsslCryptoEngine/CpriRSA.c \
    | grep -q 'OSSL_PARAM_construct_int' \
    || die "the PSS salt length does not go into the list as an integer"

# The curve goes in as its parameters, not as a name OpenSSL happens to have.
if grep -n 'OSSL_PKEY_PARAM_GROUP_NAME' src/OsslCryptoEngine/CpriECC.c ; then
    die "CpriECC.c builds its curves from a group name instead of the curve table"
fi
for key in OSSL_PKEY_PARAM_EC_P OSSL_PKEY_PARAM_EC_A OSSL_PKEY_PARAM_EC_B \
           OSSL_PKEY_PARAM_EC_GENERATOR OSSL_PKEY_PARAM_EC_ORDER \
           OSSL_PKEY_PARAM_EC_COFACTOR OSSL_PKEY_PARAM_EC_FIELD_TYPE; do
    grep -q "$key" src/OsslCryptoEngine/CpriECC.c \
        || die "CpriECC.c does not put $key in the curve's parameter list"
done

# The label is the caller's buffer.  The duplicate went with the set0 that
# needed it; a tree that kept it is allocating something nothing will free.
if grep -n 'OPENSSL_memdup' src/OsslCryptoEngine/CpriRSA.c ; then
    die "CpriRSA.c still duplicates the OAEP label"
fi

# Both new curve calls carry this engine's context and query, like every other
# call in the file that step 6 touched.
grep -q 'EC_GROUP_new_from_params(params,' src/OsslCryptoEngine/CpriECC.c \
    || die "EC_GROUP_new_from_params is not called with a parameter list"
grep -A2 'EC_GROUP_new_from_params(params,' src/OsslCryptoEngine/CpriECC.c \
    | grep -q 'OsslLibraryContext()' \
    || die "EC_GROUP_new_from_params does not use the engine's library context"
grep -A2 'EC_GROUP_new_from_params(params,' src/OsslCryptoEngine/CpriECC.c \
    | grep -q 'OsslPropertyQuery()' \
    || die "EC_GROUP_new_from_params does not use the engine's property query"

# The other affine setter stays: it places ordinary points and is not part of
# building a curve.
n=$(grep -c 'EC_POINT_set_affine_coordinates_GFp' src/OsslCryptoEngine/CpriECC.c)
[ "$n" = 1 ] \
    || die "expected exactly 1 remaining EC_POINT_set_affine_coordinates_GFp in CpriECC.c, found $n"

say "done"
