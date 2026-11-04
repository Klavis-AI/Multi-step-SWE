#!/usr/bin/env bash
# Step 1 golden solution -- opaque types, against OpenSSL 1.1.0l.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/01-opaque-types/solution/solve.sh; edit it here and regenerate, never
# edit the copy under tasks/.
#
# Run from /app with OpenSSL 1.1.0l installed as the system OpenSSL.  It edits
# the tree in place and builds it once to prove the edits compile; the verifier
# builds it again from scratch, so nothing here is load-bearing except the
# source changes.
#
#
# WHAT THIS DOES, AND WHY EACH PIECE IS NEEDED
#
# The engine builds against a copy of OpenSSL 1.0.2b vendored into the tree
# (lib/openssl/*.h, lib/x86-64/lib{crypto,ssl}.a) and reaches inside published
# structs in four places.  1.1.0 made all four opaque, so each one is a
# compile error against the system headers and has to come back through an
# accessor:
#
#   EVP_MD_CTX held by value       -> EVP_MD_CTX_new/_free, six sites
#   ctxt->digest->ctx_size         -> EVP_MD_meth_get_app_datasize(EVP_MD_CTX_md())
#   ctxt->md_data                  -> EVP_MD_CTX_md_data()
#   &group->field, &group->order   -> EC_GROUP_get_degree, EC_GROUP_get0_order
#
# and <openssl/ec_lcl.h>, which is an OpenSSL *internal* header that the
# vendored copy happened to ship, is not installed by any release and goes.
#
# Two things are not mechanical:
#
#   * CPRI_HASH_STATE must not end up holding a live EVP_MD_CTX * for a hash
#     *sequence*.  The layer above relocates a sequence by copying the OBJECT
#     that contains it (ContextSave/ContextLoad, Object.c's slot shuffling),
#     which is a bitwise copy of the state -- two containers would then share
#     one heap context and the second one to finish it would be finishing a
#     freed pointer.  The baseline already distinguishes the two cases with
#     `copySize`: a sequence marshals the digest's private state into the
#     union's byte array and holds no context between calls, and a non-sequence
#     hash keeps a live one.  That distinction is what has to survive; only its
#     representation changes, from an inline struct to a pointer.
#
#   * ValidateSignatureEcc builds a local EC_POINT from the public key and then
#     never uses it -- step 6 multiplies from `Qin` directly.  The construction
#     passes Qin->x twice, so the point it builds is (x, x), which is not on
#     the curve.  1.0.2's EC_POINT_set_affine_coordinates_GFp stored whatever
#     it was given, so a dead point with an impossible value cost nothing.
#     1.1.0 validates the point at construction and returns 0, which this call
#     site turns into FAIL(FATAL_ERROR_INTERNAL) -- the TPM goes to failure
#     mode on every ECDSA verification.  The typo is repaired so that the point
#     is the one the comment above it says it is.
set -euo pipefail

APP=${1:-/app}
cd "$APP"

say() { printf '\n== %s\n' "$*"; }
die() { printf '\nsolve: %s\n' "$*" >&2; exit 1; }

for f in src/OsslCryptoEngine/CpriHash.c src/OsslCryptoEngine/CpriECC.c \
         src/OsslCryptoEngine/CpriCryptPri.c \
         src/OsslCryptoEngine/OsslCryptoEngine.h \
         cmake/CMakeLists.txt cmake/OsslCryptoEngine/CMakeLists.txt; do
    [ -f "$f" ] || die "$f is missing -- this is not the tree this solves"
done

say "the vendored 1.0.2 copy"
rm -rf lib
[ -e lib ] && die "lib/ survived the delete"

# --------------------------------------------------------------------------
# Every edit below is applied by exact token sequence and asserted to have
# landed exactly as many times as expected.  The sources are whitespace
# disasters -- the TPM specification's own extraction tool wrote them -- so
# each anchor is matched with any run of whitespace where it has one, and a
# count that comes out wrong stops the script rather than leaving a half
# migrated tree that builds.
# --------------------------------------------------------------------------
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


# ---------------------------------------------------------------- the header
#
# ec_lcl.h is OpenSSL's own internal header.  No release installs it; the
# vendored copy shipped it so that the engine could see inside EC_GROUP.
# <string.h> came along in the same block and is still wanted.  <crypto.h> is
# added for OPENSSL_init_crypto below -- it arrives transitively today, but a
# file that calls it should say where it comes from.
patch("src/OsslCryptoEngine/OsslCryptoEngine.h", [
    ("""#if defined(_Win32) || defined(WIN32)
        #include <openSSL/ec_lcl.h>
        #else
        #include <openssl/ec_lcl.h>
        #include <string.h>
        #endif""",
     "#include     <openssl/crypto.h>\n#include     <string.h>", 1),
])

# ------------------------------------------------------------------ CpriHash
#
# The whole file is one question: where does a digest context live?  In 1.0.2
# it could live inside CPRI_HASH_STATE, because EVP_MD_CTX had a size.  In
# 1.1.0 it lives on OpenSSL's heap and the state holds a pointer to it -- for
# the non-sequence case only.  A sequence still marshals into the union's
# bytes, exactly as before, and holds no pointer between calls.
patch("src/OsslCryptoEngine/CpriHash.c", [
    # --- the state ---------------------------------------------------------
    # The union's other arm is a byte array of OSSL_HASH_STATE_DATA_SIZE, which
    # is far larger than a pointer, so the size of CPRI_HASH_STATE does not
    # move and neither does anything the TPM layer marshals around it.
    ("EVP_MD_CTX   context;",
     "EVP_MD_CTX  *context;", 1),

    # --- how big the digest's private state is, and where it is ------------
    # ctx_size and md_data are the two fields this file read.  1.1.0 publishes
    # both: EVP_MD_meth_get_app_datasize() is ctx_size, on the EVP_MD that
    # EVP_MD_CTX_md() returns, and EVP_MD_CTX_md_data() is md_data.
    ("pAssert(ctxt->digest->ctx_size <= OSSL_HASH_STATE_DATA_SIZE);",
     "pAssert(CtxDataSize(ctxt) <= OSSL_HASH_STATE_DATA_SIZE);", 1),
    ("memcpy(buf, (void*) ctxt->md_data, ctxt->digest->ctx_size);",
     "memcpy(buf, (void*) EVP_MD_CTX_md_data(ctxt), CtxDataSize(ctxt));", 1),
    # Once in MarshalHashState, once at the end of GetHashState.
    ("return (UINT16)ctxt->digest->ctx_size;",
     "return      (UINT16)CtxDataSize(ctxt);", 2),
    ("pAssert(ctxt->digest->ctx_size < sizeof(ALIGNED_HASH_STATE));",
     "pAssert(CtxDataSize(ctxt) < sizeof(ALIGNED_HASH_STATE));", 1),
    ("memcpy(ctxt->md_data, buf, ctxt->digest->ctx_size);",
     "memcpy(EVP_MD_CTX_md_data(ctxt), buf, CtxDataSize(ctxt));", 1),
    # GetHashState is handed a context its caller allocated.  _init is the
    # 1.0.2 spelling of "make this blank"; _reset is the 1.1.0 one, and is
    # what the deprecated macro expands to anyway.
    ("EVP_MD_CTX_init(ctxt);",
     "EVP_MD_CTX_reset(ctxt);", 1),

    # --- the accessor shim, placed just under the type it is about ----------
    ("""}  OSSL_HASH_STATE;""",
     """}  OSSL_HASH_STATE;

// How many bytes of private state the digest behind this context keeps.  This
// was ctxt->digest->ctx_size until 1.1.0 made both records opaque; it is the
// same number, read through the accessors the release publishes for it.
static    int
CtxDataSize(
     EVP_MD_CTX        *ctxt
)
{
     return    EVP_MD_meth_get_app_datasize(EVP_MD_CTX_md(ctxt));
}""", 1),

    # --- _cpri__CopyHashState ----------------------------------------------
    # Only ever called on a live (non-sequence) state -- a sequence has no
    # context to copy -- so `in` holds a pointer and the clone gets one of its
    # own.  Sharing here would be the same defect as sharing across a
    # ContextSave.
    ("""EVP_MD_CTX_init(&o->u.context);
        EVP_MD_CTX_copy_ex(&o->u.context, &i->u.context);""",
     """o->u.context  =  EVP_MD_CTX_new();
    if(o->u.context   ==  NULL)
           FAIL(FATAL_ERROR_ALLOCATION);
    EVP_MD_CTX_copy_ex(o->u.context,               i->u.context);""", 1),

    # --- the five contexts that used to be locals ---------------------------
    # _cpri__StartHash and _cpri__CompleteHash both call theirs localState.
    ("EVP_MD_CTX localState;",
     "EVP_MD_CTX         *localState       =  NULL;", 2),
    ("EVP_MD_CTX localContext;",
     "EVP_MD_CTX                   *localContext      =  NULL;", 1),
    # _cpri__HashBlock and _cpri__MGF1 both call theirs hashContext.
    ("EVP_MD_CTX hashContext;",
     "EVP_MD_CTX                     *hashContext;", 2),

    # --- _cpri__StartHash ---------------------------------------------------
    # One allocation either way.  A sequence keeps it only long enough to
    # marshal; a live hash hands it to the state and it stays there until
    # _cpri__CompleteHash finishes it.
    ("""if(sequence)
        context = &localState;
        else
        context = &state->u.context;

        hashState->hashAlg = hashAlg;

        EVP_MD_CTX_init(context);""",
     """context    =  EVP_MD_CTX_new();
   if(context     ==  NULL)
          FAIL(FATAL_ERROR_ALLOCATION);
   if(sequence)
          localState    =   context;
   else
          state->u.context   =   context;

   hashState->hashAlg             =     hashAlg;
""", 1),
    ("""//     Do  the  cleanup
        EVP_MD_CTX_cleanup(context);""",
     """//     Do  the  cleanup
                 EVP_MD_CTX_free(localState);""", 1),
    # The failure arm: hashAlg was TPM_ALG_NULL or the digest had no size, so
    # nothing keeps the context.  1.0.2 could leave an initialised struct
    # inside the state and forget about it; a pointer has to be released, and
    # the union must not be left holding a stale one.
    ("""else
        state->copySize = -1;
        }
        else
        state->copySize = 0;
        return retVal;""",
     """else
                 state->copySize             =  -1;
   }
   else
   {
          state->copySize            =  0;
          EVP_MD_CTX_free(context);
          if(!sequence)
                 state->u.context            =  NULL;
   }
   return     retVal;""", 1),

    # --- _cpri__UpdateHash --------------------------------------------------
    ("""context = &localContext;
        if((retVal = GetHashState(context, hashState->hashAlg, stateData)) <= 0)
        return;
        }
        else
        context = &state->u.context;""",
     """if((localContext     =  EVP_MD_CTX_new())    ==  NULL)
                  FAIL(FATAL_ERROR_ALLOCATION);
           context      =   localContext;
           if((retVal          =   GetHashState(context,           hashState->hashAlg,      stateData))     <=  0)
           {
                  EVP_MD_CTX_free(localContext);
                  return;
           }
    }
    else
           context      =   state->u.context;""", 1),
    # One exit instead of one arm: EVP_MD_CTX_free(NULL) is a no-op, and the
    # live context belongs to the state rather than to this call.
    ("""else
        EVP_MD_CTX_cleanup(context);
        }
        return;""",
     """    }
    EVP_MD_CTX_free(localContext);
    return;""", 1),

    # --- _cpri__CompleteHash ------------------------------------------------
    ("""context = &localState;
        if((retVal = GetHashState(context, hashState->hashAlg, stateData)) <= 0)
        goto Cleanup;
        }
        else
        context = &state->u.context;""",
     """if((localState       =  EVP_MD_CTX_new())    ==  NULL)
                  FAIL(FATAL_ERROR_ALLOCATION);
           context      =   localState;
           if((retVal          =   GetHashState(context,           hashState->hashAlg,      stateData))     <=  0)
                  goto     Cleanup;
    }
    else
           context      =  state->u.context;""", 1),
    # The state is spent either way -- copySize is 0 on success and the next
    # call returns immediately -- so this is where a live context is released.
    ("""Cleanup:
        EVP_MD_CTX_cleanup(context);
        return retVal;""",
     """Cleanup:
     if(localState             !=   NULL)
           EVP_MD_CTX_free(localState);
     else
     {
           EVP_MD_CTX_free(state->u.context);
           state->u.context            =  NULL;
     }
     return      retVal;""", 1),

    # --- _cpri__HashBlock ---------------------------------------------------
    ("""//   After       the     call     to  EVP_MD_CTX_init(),             will  need      to  call    EVP_MD_CTX_cleanup()
        EVP_MD_CTX_init(&hashContext);                              //   Initialize       the    local       hash     context""",
     """//   Allocated       here     and     released      at   the   single    exit    below
      hashContext      =  EVP_MD_CTX_new();               //   Allocate     the   local     hash     context
      if(hashContext          ==  NULL)
              FAIL(FATAL_ERROR_ALLOCATION);""", 1),

    # --- _cpri__MGF1 --------------------------------------------------------
    # The "no server" arm returned without cleaning up, which cost nothing when
    # the context was a local struct and leaks now.
    ("""EVP_MD_CTX_init(&hashContext);                                      //   Initialize              the   local    hash     context
        hashServer = GetHashServer(hashAlg);                       //  Find      the       hash  server
        if(hashServer == NULL)
        //     If     there         is   no    server,        then    there        is  no      digest
        return 0;""",
     """hashContext      =   EVP_MD_CTX_new();                       //   Allocate   the   local    hash     context
   if(hashContext            ==  NULL)
         FAIL(FATAL_ERROR_ALLOCATION);
   hashServer           =     GetHashServer(hashAlg);                       //  Find      the       hash  server
   if(hashServer                 ==  NULL)
   {
         //     If     there         is   no    server,        then    there        is  no      digest
         EVP_MD_CTX_free(hashContext);
         return         0;
   }""", 1),

    # Both functions' single exits.
    ("EVP_MD_CTX_cleanup(&hashContext);",
     "EVP_MD_CTX_free(hashContext);", 2),
])

# The nine remaining `&hashContext` are the EVP_Digest* calls in those two
# functions.  Done as one pass with a count rather than nine anchors: the
# address-of is the entire change, and nine hand-written anchors into this
# whitespace would be nine chances to write one wrong.
src = open("src/OsslCryptoEngine/CpriHash.c").read()
src, n = re.subn(r"&hashContext\b", "hashContext", src)
if n != 9:
    sys.exit("solve: CpriHash.c: %d `&hashContext` left, expected 9" % n)
open("src/OsslCryptoEngine/CpriHash.c", "w").write(src)
EDITS += n

# ------------------------------------------------------------------- CpriECC
patch("src/OsslCryptoEngine/CpriECC.c", [
    # The field's size in bytes.  EC_GROUP_get_degree is the number of bits in
    # the field, which for these GFp curves is BN_num_bits(p) -- the same
    # number BN_num_bytes(&group->field) was rounding up.
    ("BN_num_bytes(&group->field)",
     "((EC_GROUP_get_degree(group) + 7) / 8)", 2),
    # The group order is published directly, as a const BIGNUM * that the
    # group owns.  Every use here is a read.
    ("&group->order",
     "EC_GROUP_get0_order(group)", 5),

    # ValidateSignatureEcc: the public point is (x, y), not (x, x).  See the
    # note at the top of this file -- 1.0.2 stored the impossible point without
    # complaint because nothing downstream reads it, and 1.1.0 rejects it at
    # construction, which this call site escalates to failure mode.
    ("""bnQx) == NULL
        || BN_bin2bn(Qin->x.t.buffer, Qin->x.t.size, bnQy) == NULL""",
     """bnQx)      ==     NULL
           ||     BN_bin2bn(Qin->y.t.buffer,                               Qin->y.t.size,                 bnQy)      ==     NULL""",
     1),
])

# -------------------------------------------------------------- CpriCryptPri
# 1.1.0 still initialises libcrypto on first use, but the engine should not be
# relying on that: it is the library's decision, not the caller's, and every
# release after this one narrows what implicit initialisation does.  The crypto
# engine has exactly one entry point that means "start", so it goes there.
patch("src/OsslCryptoEngine/CpriCryptPri.c", [
    ("""TpmFailFunction = failFunction;

        _cpri__RngStartup();""",
     """TpmFailFunction        =  failFunction;

   //  Take     the    library       up    explicitly       rather     than      on   first    use:      the
   //  digests       and    ciphers       this      engine     asks     for   by   name,      and    the   error
   //  strings       the    FAIL      paths      would      otherwise      report      as   numbers.
   if(OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_DIGESTS
                          |   OPENSSL_INIT_ADD_ALL_CIPHERS
                          |   OPENSSL_INIT_LOAD_CRYPTO_STRINGS,      NULL)     !=  1)
          FAIL(FATAL_ERROR_INTERNAL);

   _cpri__RngStartup();""", 1),
])

print("solve: %d source edit(s) applied" % EDITS)
PY

say "build system"
python3 - <<'PY'
import sys

# cmake/CMakeLists.txt: ../lib was on the include path so that <openssl/*.h>
# resolved to the vendored copy.  It is gone; the system headers are on the
# compiler's default path.
p = "cmake/CMakeLists.txt"
s = open(p).read()
old = "                    ../lib)"
if s.count(old) != 1:
    sys.exit("solve: %s: expected one `../lib)` include entry" % p)
open(p, "w").write(s.replace(old, "                    ../src/OsslCryptoEngine)"))

# cmake/OsslCryptoEngine/CMakeLists.txt: the vendored archives were named by
# path for four platform cases.  All four go; the engine links the system
# libcrypto that FindOpenSSL locates, and nothing here mentions a file in the
# repository any more.
p = "cmake/OsslCryptoEngine/CMakeLists.txt"
s = open(p).read()
start = 'file(GLOB HEADER_FILES "../../lib/openssl/*.h")'
end = "if(WIN32)\n\ttarget_link_libraries(CryptoEngine"
if s.count(start) != 1 or s.count(end) != 1:
    sys.exit("solve: %s: does not look like the file this edits" % p)
i, j = s.index(start), s.index(end)
s = s[:i] + '''file(GLOB HEADER_FILES2 "../../src/include/*.h")

# The system OpenSSL, wherever this machine keeps it.  Nothing in the
# repository is an OpenSSL any more.
find_package(OpenSSL REQUIRED)

add_library(CryptoEngine SHARED ${SOURCE_FILES} ${HEADER_FILES2})
add_dependencies(CryptoEngine platform)

target_link_libraries(CryptoEngine OpenSSL::Crypto)

''' + s[j:]
open(p, "w").write(s)
print("solve: build system points at the system OpenSSL")
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

# The one fact that says the vendored copy is really gone: the engine asks the
# loader for a libcrypto and defines none of it itself.
# The read is into a variable rather than down a pipe into `grep -q`: grep
# exits at its first match, the producer takes SIGPIPE on the rest of its
# output, and `set -o pipefail` turns that into a failed pipeline whenever the
# producer had not already finished writing.  It is a race on output size.
dyn=$(readelf -d "$B/OsslCryptoEngine/libCryptoEngine.so")
printf '%s\n' "$dyn" | grep -q 'NEEDED.*libcrypto' \
    || die "libCryptoEngine.so does not link the system libcrypto"

say "done"
printf 'built in %s\n' "$B"
