#!/usr/bin/env bash
# Step 9 golden solution -- the two places this engine still hands a cipher a
# key by passing a pointer to bytes, rewritten to hand it a key object the
# provider made.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/09-opaque-symmetric-keys/solution/solve.sh; edit it here and
# regenerate, never edit the copy under tasks/.
#
# Run from /app on the tree step 8 left behind, with OpenSSL 3.5.4 installed as
# the system OpenSSL.  It edits the tree in place and builds it once to prove
# the edits compile; the verifier builds it again from scratch, so nothing here
# is load-bearing except the source changes.
#
#
# WHAT THIS DOES, AND WHY EACH PIECE IS NEEDED
#
# Steps 3 to 8 moved every algorithm in this engine onto fetched
# implementations, a library context of its own, a named property query, a
# parameter list instead of a pile of setters, and off every name 3.4 retired.
# One thing they did not touch is how a symmetric operation receives its KEY.
# In this tree that is still what it was in 1.0.2: a `const BYTE *` handed to
# EVP_CipherInit_ex, which the library copies into the context and the
# provider copies again into its own key schedule.  The bytes exist in three
# places, and the only object in the picture is the context.
#
# 3.5 finished the other half of the provider transition and gave symmetric
# keys the same treatment asymmetric keys got in 3.0: EVP_SKEY, made by a
# provider's skeymgmt, handed to an init as an object.  Two call sites in this
# file are the whole of this engine's symmetric key handling --
# CpriSym.c:138 in SymRun() and CpriSym.c:191 in SymRunOfb() -- and they are
# what this step rewrites.
#
#   SymRun()      every mode but OFB, both directions
#   SymRunOfb()   OFB, which runs its last block separately for a reason that
#                 has nothing to do with keys and does not change here
#
# All fifteen cipher cells and both directions were measured through both
# spellings before any of this was written (build/gates/09-skey-spike.c,
# section 13 of NOTES-authoring.md): thirty of thirty comparisons are
# byte-identical.  This step changes no answer this engine gives.
#
# THREE THINGS HERE ARE NOT A RENAME
#
#   the context   EVP_SKEY_import_raw_key() takes a library context and a
#                 property query, and accepts NULL for both.  The IMPORT
#                 succeeds either way; what does not succeed is the init.
#                 Measured (section 12.11 of NOTES-authoring.md): a key
#                 object and a cipher that came from different contexts are
#                 refused at EVP_CipherInit_SKEY with `digital envelope
#                 routines::initialization error'.  Since step 3 the ciphers
#                 in this file come from OsslLibraryContext(), so naming it
#                 at the import is not a style point -- it is what makes the
#                 two halves of the init match, and a tree that passes NULL
#                 fails every symmetric operation rather than passing
#                 quietly.  The query goes with it for the reason every
#                 other fetch in this file carries one; on this release it
#                 selects nothing a NULL would not, because 3.5.4's FIPS
#                 provider registers no skeymgmt at all and both spellings
#                 resolve to the default provider's.
#
#   the IV        EVP_CipherInit_SKEY() takes the IV AND ITS LENGTH, and has
#                 no default for the length.  Measured: CBC with a NULL IV
#                 and a length of 0 returns 1 -- and encrypts from a zero
#                 register, giving a different answer from the same call with
#                 the IV.  The length is therefore taken from the cipher
#                 rather than assumed, and the IV is passed on every mode
#                 that has one.  This is the failure this step is most likely
#                 to be got wrong in a way that still builds.
#
#   the lifetime  The object belongs to the caller that made it.
#                 EVP_CipherInit_SKEY takes NO reference on it (verified in
#                 3.5.4's crypto/evp/evp_enc.c:627), so the context does not
#                 keep it alive and freeing it after the context is both
#                 necessary and safe.  Necessary matters on the FAILURE path
#                 too: FAIL() in this tree is a longjmp into the TPM's
#                 failure mode, not an exit, so a tree that frees only after
#                 the `if(!ok)` leaks one key object per failed operation and
#                 keeps running.
#
# The key is not a secret from its holder -- EVP_SKEY_get0_raw_key() hands the
# bytes back on the default provider, measured -- so nothing here is claimed
# about secrecy.  What changes is the SHAPE of the argument at the init, which
# is what lets a provider that does hold keys elsewhere serve this engine
# without the engine being rewritten again.
set -euo pipefail

APP=${1:-/app}
cd "$APP"

say()  { printf '\n== %s\n' "$*"; }
die()  { printf 'solve: %s\n' "$*" >&2; exit 1; }

for f in src/OsslCryptoEngine/CpriSym.c \
         src/OsslCryptoEngine/CpriCryptPri.c \
         src/OsslCryptoEngine/OsslCryptoEngine.h cmake/CMakeLists.txt; do
    [ -f "$f" ] || die "$f is missing -- this is not the tree step 8 produced"
done

# Steps 3, 4 and 6 have to be here already: there is no context to import a key
# into if the engine has none of its own, no query to carry if step 6 never
# wrote one, and nothing to hand a key object to if the symmetric work still
# goes through the low level AES calls step 4 replaced.
grep -q 'OsslLibraryContext' src/OsslCryptoEngine/CpriCryptPri.c \
    || die "src/OsslCryptoEngine/CpriCryptPri.c has no library context -- step 3 has not been applied"
grep -q 'OsslPropertyQuery' src/OsslCryptoEngine/CpriCryptPri.c \
    || die "src/OsslCryptoEngine/CpriCryptPri.c has no property query -- step 6 has not been applied"
grep -q 'EVP_CIPHER_fetch' src/OsslCryptoEngine/CpriSym.c \
    || die "src/OsslCryptoEngine/CpriSym.c does not fetch its ciphers -- step 4 has not been applied"
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


# ==================================================== the key, as an object
#
# One helper, beside the one that hands back the cipher, because both call
# sites need the same object built the same way and because the two arguments
# that matter -- the context and the query -- belong in one place in this file
# rather than two, the same way SymCipher() keeps the table lookup in one
# place.  It is deliberately NOT cached: the key arrives with the operation,
# a TPM changes symmetric keys constantly, and a cache here would be a key
# schedule by another name, which is the thing step 4 took out.
patch("src/OsslCryptoEngine/CpriSym.c", [
    ("""return    s_symServer[mode][(keySizeInBits            /  64)   -   2].cipher;
        }

        // B.11.2.2.     SymRun()""",
     """return    s_symServer[mode][(keySizeInBits            /  64)   -   2].cipher;\n"""
     """}\n"""
     """\n"""
     """// B.11.2.1a.        SymKey()\n"""
     """\n"""
     """//   The     key    bytes      the    caller      handed        down,      as   the    key     object       3.5\n"""
     """//   wants       at    an   init.       Imported          into      the    engine's         own      context\n"""
     """//   with      the    engine's         own     property         query,       for     the    same      reason\n"""
     """//   every       fetch      in    this     file     carries        them.        The     import        would\n"""
     """//   accept        NULL      for     either;         the    init      would       not.       A   key     object\n"""
     """//   and     a   cipher        from      two     different            contexts            are     refused          at\n"""
     """//   EVP_CipherInit_SKEY,                     so    the    context         named       here      has      to    be\n"""
     """//   the     one     CpriSym.c:236                 fetches         the     ciphers         from.\n"""
     """//\n"""
     """//   The     caller        owns      the    bytes.        The     object        this      returns         is   the\n"""
     """//   calling         operation's                and     is    given       back      before       that      operation\n"""
     """//   returns,          on    both      paths.\n"""
     """static   EVP_SKEY               *\n"""
     """SymKey(\n"""
     """      UINT32                    keySizeInBits,           //   IN:   key    size   in   bits\n"""
     """      const  BYTE               *key                     //   IN:   key    buffer\n"""
     """      )\n"""
     """{\n"""
     """      return    EVP_SKEY_import_raw_key(OsslLibraryContext(),               SYM_SKEY_TYPE,\n"""
     """                                        (unsigned         char   *)key,\n"""
     """                                        (size_t)(keySizeInBits               /  8),\n"""
     """                                        OsslPropertyQuery());\n"""
     """}\n"""
     """\n"""
     """// B.11.2.2.     SymRun()""", 1),

    # The name the skeymgmt is asked for, beside the table of cipher names it
    # goes with.  One name covers all three key sizes: a skeymgmt is asked for
    # the algorithm, and the size comes from the length argument.
    ('''#define   SYM_KEY_SIZE_COUNT         3           //  128,   192   and   256   bits''',
     '''#define   SYM_KEY_SIZE_COUNT         3           //  128,   192   and   256   bits\n'''
     '''\n'''
     '''//   The     key     manager          this      engine's         symmetric            keys     come      from.\n'''
     '''//   One     name      for     all     three      sizes:        a   skeymgmt            is   fetched         for\n'''
     '''//   an    algorithm,              and     the    size      comes       from      the     length        handed\n'''
     '''//   to    the     import.\n'''
     '''#define   SYM_SKEY_TYPE              "AES"''', 1),
])


# ============================================ the two inits, and the two frees
patch("src/OsslCryptoEngine/CpriSym.c", [
    # SymRun().  The declaration block, the two new steps around the context,
    # and the free that pairs with the import.
    ("""EVP_CIPHER_CTX            *ctx;
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

        EVP_CIPHER_CTX_free(ctx);""",
     """EVP_CIPHER_CTX            *ctx;\n"""
     """      EVP_SKEY                  *skey;\n"""
     """      size_t                    ivLen     =  0;\n"""
     """      int                       outLen    =  0;\n"""
     """      int                       ok;\n"""
     """\n"""
     """      if(cipher         ==  NULL)\n"""
     """              FAIL(FATAL_ERROR_INTERNAL);\n"""
     """\n"""
     """      //    The   key    object,       before        the    context         that      will      take      it.\n"""
     """      //    A   key    this      provider          will     not     hold      is    the    same      refusal\n"""
     """      //    the   key      schedule           used      to    report,         and     the     caller        sees\n"""
     """      //    it    the    same       way.\n"""
     """      if((skey      =   SymKey(keySizeInBits,                key))     ==  NULL)\n"""
     """              FAIL(FATAL_ERROR_INTERNAL);\n"""
     """\n"""
     """      if((ctx       =   EVP_CIPHER_CTX_new())               ==  NULL)\n"""
     """      {\n"""
     """              EVP_SKEY_free(skey);\n"""
     """              FAIL(FATAL_ERROR_ALLOCATION);\n"""
     """      }\n"""
     """\n"""
     """      //    The   IV    and     ITS     LENGTH.         This      entry       point      has      no\n"""
     """      //    default       for     the     length,         and     an    init      that      leaves        it   at\n"""
     """      //    zero      returns         1   and     then      runs      from      a   zero      register\n"""
     """      //    instead         of    the     one     the     caller        gave.       So    the     length        is\n"""
     """      //    the   cipher's           own,      and     a   mode      without           an    IV    passes\n"""
     """      //    neither.\n"""
     """      if(iv     !=  NULL)\n"""
     """              ivLen     =   (size_t)EVP_CIPHER_get_iv_length(cipher);\n"""
     """\n"""
     """      ok    =   EVP_CipherInit_SKEY(ctx,             cipher,       skey,      iv,     ivLen,\n"""
     """                                    encrypt,          NULL)      ==  1\n"""
     """                &&   EVP_CIPHER_CTX_set_padding(ctx,             0)   ==  1\n"""
     """                &&   EVP_CipherUpdate(ctx,          dOut,      &outLen,      dIn,   (int)dSize)  ==  1\n"""
     """                &&   outLen       ==  (int)dSize;\n"""
     """\n"""
     """      //    Both      go   back      here,       before        the     branch,         because         FAIL()\n"""
     """      //    in    this      tree      is   a   longjmp          into      the     TPM's       failure\n"""
     """      //    mode      and     not     an    exit:      a   free      written         after       the    test\n"""
     """      //    is    a   free      that      does      not     happen         on    the     path      this      step\n"""
     """      //    is    about.        The     context         first,        then      the     key:      the    init\n"""
     """      //    takes       no    reference             on    the     key     object,         so    the     context\n"""
     """      //    must      be    done      with      it   before        it   is   released.\n"""
     """      EVP_CIPHER_CTX_free(ctx);\n"""
     """      EVP_SKEY_free(skey);""", 1),

    # SymRunOfb().  Same three changes; the OFB register is always present, so
    # the length is unconditional here.
    ("""EVP_CIPHER_CTX            *ctx;
        BYTE                      zero[16];""",
     """EVP_CIPHER_CTX            *ctx;\n"""
     """      EVP_SKEY                  *skey;\n"""
     """      BYTE                      zero[16];""", 1),

    ("""if((ctx       =   EVP_CIPHER_CTX_new())               ==  NULL)
                FAIL(FATAL_ERROR_ALLOCATION);

        ok    =   EVP_CipherInit_ex(ctx,          cipher,     NULL,     key,    iv,   1)   ==  1
                  &&   EVP_CIPHER_CTX_set_padding(ctx,             0)   ==  1;""",
     """if((skey      =   SymKey(keySizeInBits,                key))     ==  NULL)\n"""
     """              FAIL(FATAL_ERROR_INTERNAL);\n"""
     """\n"""
     """      if((ctx       =   EVP_CIPHER_CTX_new())               ==  NULL)\n"""
     """      {\n"""
     """              EVP_SKEY_free(skey);\n"""
     """              FAIL(FATAL_ERROR_ALLOCATION);\n"""
     """      }\n"""
     """\n"""
     """      //    The   OFB     register         is   this      function's             whole      subject          and\n"""
     """      //    is    never       absent,         so    the     length        is    the    cipher's           own\n"""
     """      //    with      no    branch.\n"""
     """      ok    =   EVP_CipherInit_SKEY(ctx,             cipher,       skey,      iv,\n"""
     """                                    (size_t)EVP_CIPHER_get_iv_length(cipher),\n"""
     """                                    1,   NULL)      ==  1\n"""
     """                &&   EVP_CIPHER_CTX_set_padding(ctx,             0)   ==  1;""", 1),

    ("""EVP_CIPHER_CTX_free(ctx);

        if(!ok)
                FAIL(FATAL_ERROR_INTERNAL);

        //    The   short     tail,    and    then     the   register      the   next     call    starts
        //    from.""",
     """EVP_CIPHER_CTX_free(ctx);\n"""
     """      EVP_SKEY_free(skey);\n"""
     """\n"""
     """      if(!ok)\n"""
     """              FAIL(FATAL_ERROR_INTERNAL);\n"""
     """\n"""
     """      //    The   short     tail,    and    then     the   register      the   next     call    starts\n"""
     """      //    from.""", 1),
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

# Step 8's fence is still up -- this step adds code behind it, and a new call
# spelled with a name 3.4 retired would be an implicit declaration rather than
# an error.  Same reading as step 8's solve: only names the header census
# calls deprecated count, and the four this tree implicitly declares on its own
# are not among them.
say "fence"
F=$(mktemp -d)
cmake -S "$APP/cmake" -B "$F" -DSPEC_VERSION=116 \
    -DCMAKE_C_FLAGS="-DOPENSSL_API_COMPAT=0x30400000L -DOPENSSL_NO_DEPRECATED" \
    > "$F/configure.log" 2>&1 \
    || { tail -40 "$F/configure.log" >&2; die "fence configure failed"; }
cmake --build "$F" -j"${JOBS:-7}" > "$F/build.log" 2>&1 \
    || { grep -m10 ': error:' "$F/build.log" >&2; die "the tree does not build behind the fence"; }
bad=$(grep -oE "implicit declaration of function .[A-Za-z_][A-Za-z0-9_]*" \
          "$F/build.log" | sed "s/.*[^A-Za-z0-9_]//" | sort -u |
      grep -E '^(EC_|BN_|RAND_|EVP_|RSA_|DH_|DSA_|HMAC_|ENGINE_|ERR_|SHA|X509)' \
      || true)
[ -z "$bad" ] \
    || die "still implicitly declared behind the fence: $(echo $bad | tr '\n' ' ')"

say "assert"

ENG=$B/OsslCryptoEngine/libCryptoEngine.so
undef=$(nm -D --undefined-only "$ENG" | awk '{print $NF}' | sed 's/@.*//')

# What the binary must now ask libcrypto for.  A tree with the calls in a file
# the build does not compile, or behind a branch the linker dropped, fails
# here rather than at a gate.
for sym in EVP_SKEY_import_raw_key EVP_SKEY_free EVP_CipherInit_SKEY \
           EVP_CIPHER_get_iv_length; do
    printf '%s\n' "$undef" | grep -qx "$sym" \
        || die "$sym is not an undefined symbol of libCryptoEngine.so"
done

# And what it must no longer ask for.  These two were the only cipher inits in
# the whole tree, both in CpriSym.c, so after this step nothing in the engine
# starts a symmetric operation from a pointer to key bytes.
for sym in EVP_CipherInit_ex EVP_CipherInit_ex2 EVP_CipherInit \
           EVP_EncryptInit_ex EVP_DecryptInit_ex; do
    if printf '%s\n' "$undef" | grep -qx "$sym"; then
        die "$sym is still an undefined symbol of libCryptoEngine.so"
    fi
done

# The import names both.  Checked on the source rather than the binary because
# neither argument is visible in a symbol table.  This is not a gate -- a tree
# that passes NULL here is refused at the init and fails every symmetric
# operation, so the behaviour corpus reaches it first -- but a golden that got
# it wrong would fail that corpus with a message about envelope routines, and
# saying so here is cheaper than reading that back out of a verifier log.
grep -q 'EVP_SKEY_import_raw_key(OsslLibraryContext()' \
    src/OsslCryptoEngine/CpriSym.c \
    || die "the key import does not name this engine's library context"
grep -A3 'EVP_SKEY_import_raw_key(OsslLibraryContext()' \
    src/OsslCryptoEngine/CpriSym.c | grep -q 'OsslPropertyQuery()' \
    || die "the key import does not carry this engine's property query"

# One import, one free per call site, on both paths.  Counted rather than
# inspected: the shape that leaks is the shape that frees once, after the test.
n=$(grep -c 'SymKey(keySizeInBits' src/OsslCryptoEngine/CpriSym.c)
[ "$n" = 2 ] \
    || die "expected the key import at 2 call sites, found $n"
n=$(grep -c 'EVP_SKEY_free' src/OsslCryptoEngine/CpriSym.c)
[ "$n" = 4 ] \
    || die "expected 4 EVP_SKEY_free calls (two per call site: the allocation failure and the return), found $n"

# The free is between the init and the branch, in both functions.  This is the
# one property the behaviour corpus cannot see: FAIL() is a longjmp into the
# TPM's failure mode, so a free written after `if(!ok)` runs on the successful
# path and never on the failing one, and every test still passes.
awk '/EVP_CipherInit_SKEY\(/  {inop=1; freed=0}
     /EVP_SKEY_free\(skey\);/ {if (inop) freed=1}
     /if\(!ok\)/              {n++; if (!freed) bad=1; inop=0}
     END {exit !(n == 2 && !bad)}' \
    src/OsslCryptoEngine/CpriSym.c \
    || die "a key object is released after the failure branch rather than before it"

# The IV length is taken from the cipher, not assumed.  A literal 16 would be
# right for every cipher this table holds and wrong the moment the table grows,
# and a 0 would be accepted by the library and silently change the answer.
grep -q 'EVP_CIPHER_get_iv_length(cipher)' src/OsslCryptoEngine/CpriSym.c \
    || die "the IV length is not taken from the cipher"
n=$(grep -c 'EVP_CIPHER_get_iv_length(cipher)' src/OsslCryptoEngine/CpriSym.c)
[ "$n" = 2 ] \
    || die "expected the IV length at both call sites, found $n"

# Step 4's property, which this step re-spells and does not relax: the ciphers
# are still fetched by name from this engine's context.
grep -q 'EVP_CIPHER_fetch(OsslLibraryContext()' src/OsslCryptoEngine/CpriSym.c \
    || die "the cipher fetch no longer names this engine's library context"

say "done"
