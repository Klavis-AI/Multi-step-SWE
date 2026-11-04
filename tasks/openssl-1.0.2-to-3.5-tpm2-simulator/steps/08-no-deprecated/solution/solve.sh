#!/usr/bin/env bash
# Step 8 golden solution -- the last 1.0.2 spellings taken out of the sources,
# and the engine's randomness moved onto the library context the engine has
# owned since step 3.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/08-no-deprecated/solution/solve.sh; edit it here and regenerate, never
# edit the copy under tasks/.
#
# Run from /app on the tree step 7 left behind, with OpenSSL 3.4.2 installed as
# the system OpenSSL.  It edits the tree in place and builds it once to prove
# the edits compile; the verifier builds it again from scratch, so nothing here
# is load-bearing except the source changes.
#
#
# WHAT THIS DOES, AND WHY EACH PIECE IS NEEDED
#
# THE FENCE.  OPENSSL_API_COMPAT=0x30400000L with OPENSSL_NO_DEPRECATED is the
# library's own way of saying "do not show me anything retired at or before
# 3.4".  Behind it this tree does not compile, at three names:
#
#   EC_POINT_get_affine_coordinates_GFp   CpriECC.c, 6 sites
#   EC_POINT_set_affine_coordinates_GFp   CpriECC.c, 1 site
#   BN_is_prime_ex + BN_prime_checks      CpriRSA.c:1940, MathFunctions.c:675
#
# The first two are renames and nothing else: 3.0 dropped the _GFp suffix when
# the GF(2^m) half of the API went away, and the signature is unchanged.  The
# third is not quite one.  BN_check_prime takes no check count -- the library
# picks the number of Miller-Rabin rounds from the candidate's size, which is
# what BN_prime_checks was a fixed, and for large primes a too-small,
# approximation of -- so the argument is dropped rather than translated.  Both
# call sites already treat a negative return as a fatal error and a 1 as
# prime, which is BN_check_prime's contract too.
#
# THE RANDOMNESS.  CpriRNG.c is the only algorithm file in this engine that
# names neither OsslLibraryContext() nor OsslPropertyQuery().  Its four draws
# go to functions that have nowhere to put them:
#
#   RAND_seed        _cpri__RngStartup       the platform's startup entropy
#   RAND_add         _cpri__StirRandom       entropy added later
#   RAND_bytes       _cpri__GenerateRandom   public randomness
#   RAND_priv_bytes  _cpri__GeneratePrivateRandom   key material
#
# So the engine loads the FIPS module into its own context, self-tests it, and
# then draws from whatever DRBG the process default has -- which, measured, is
# whichever provider was loaded FIRST (probe/step08/randorder.c; section 12.1).
# This tree loads default at CpriCryptPri.c:175 and fips at :180, so it tests
# one module and draws from the other, and no line of the source says so.
#
# TWO THINGS HERE ARE NOT A RENAME
#
#   the window     RAND_set_DRBG_type decides which implementation serves a
#                  context's DRBG chain, and it only decides anything before
#                  the chain is instantiated.  randfips.c measured it: 1
#                  before the first draw, 0 after, chain unchanged.  So the
#                  call is a one-shot, and every startup entry point in this
#                  engine is re-entered: _cpri__Startup() calls
#                  _cpri__RngStartup() on every TPM2_Startup
#                  (CryptUtil.c:1794), and _TPM_Init re-enters
#                  _cpri__InitCryptoUnits on every power-on.  The naming
#                  therefore goes where the context itself is built -- inside
#                  the s_libCtx == NULL block, which is the one piece of this
#                  engine that runs exactly once per process -- after the
#                  module is loaded and self-tested and before any algorithm
#                  unit has started, let alone drawn.  randplace.c measured
#                  that exact point returning 1 (section 12.2).  Its return
#                  value is checked, because a call that quietly did nothing
#                  is the failure this step is about.
#
#   the slot       EVP_RAND_reseed takes entropy and additional input in two
#                  different arguments.  randseed.c measured a DRBG with a
#                  live parent REFUSING caller-supplied entropy -- rc=0,
#                  reseed counter unmoved -- and accepting the same bytes as
#                  additional input.  The platform's entropy therefore goes
#                  in the additional-input slot, which is the one that mixes
#                  it in, not the one named for what the caller thinks it is
#                  doing.
#
# Both calls return a value.  This file checks both, because both have a
# failure mode that produces perfectly random numbers from the wrong place.
set -euo pipefail

APP=${1:-/app}
cd "$APP"

say()  { printf '\n== %s\n' "$*"; }
die()  { printf 'solve: %s\n' "$*" >&2; exit 1; }

for f in src/OsslCryptoEngine/CpriECC.c src/OsslCryptoEngine/CpriRSA.c \
         src/OsslCryptoEngine/CpriRNG.c src/OsslCryptoEngine/MathFunctions.c \
         src/OsslCryptoEngine/CpriCryptPri.c \
         src/OsslCryptoEngine/OsslCryptoEngine.h cmake/CMakeLists.txt; do
    [ -f "$f" ] || die "$f is missing -- this is not the tree step 7 produced"
done

# Steps 3 and 6 have to be here already: the new RNG calls take the context and
# the query those steps created, and there is nothing to name a DRBG on if the
# engine has no context of its own.
grep -q 'OsslLibraryContext' src/OsslCryptoEngine/CpriCryptPri.c \
    || die "src/OsslCryptoEngine/CpriCryptPri.c has no library context -- step 3 has not been applied"
grep -q 'OsslPropertyQuery' src/OsslCryptoEngine/CpriCryptPri.c \
    || die "src/OsslCryptoEngine/CpriCryptPri.c has no property query -- step 6 has not been applied"
grep -q 'EC_GROUP_new_from_params' src/OsslCryptoEngine/CpriECC.c \
    || die "src/OsslCryptoEngine/CpriECC.c does not build groups from parameters -- step 7 has not been applied"

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


def rename(path, old, new, want):
    """A retired name for its 3.x spelling, with the site count asserted."""
    global EDITS
    src = open(path).read()
    src, got = re.subn(r"\b%s\b" % re.escape(old), new, src)
    if got != want:
        sys.exit("solve: %s: %s appears %d time(s), expected %d"
                 % (path, old, got, want))
    EDITS += got
    open(path, "w").write(src)


# ============================================================ the fence
#
# Two renames and a dropped argument.  Nothing else about these call sites
# changes: the coordinate accessors have the same signature under both names,
# and BN_check_prime returns what BN_is_prime_ex returned -- 1 prime, 0 not,
# negative error -- which is what both callers already branch on.
rename("src/OsslCryptoEngine/CpriECC.c",
       "EC_POINT_get_affine_coordinates_GFp",
       "EC_POINT_get_affine_coordinates", 6)
rename("src/OsslCryptoEngine/CpriECC.c",
       "EC_POINT_set_affine_coordinates_GFp",
       "EC_POINT_set_affine_coordinates", 1)

# BN_prime_checks was a fixed round count and is gone with the function that
# took one; BN_check_prime derives the rounds from the candidate's size, which
# is a stronger test for the key sizes this engine generates, not a weaker one.
patch("src/OsslCryptoEngine/CpriRSA.c", [
    ("""if((retVal               =   (CRYPT_RESULT)BN_is_prime_ex(bnP,
        BN_prime_checks,                    NULL,      NULL))    <   0)""",
     """if((retVal               =   (CRYPT_RESULT)BN_check_prime(bnP,\n"""
     """                                NULL,      NULL))    <   0)""", 1),
])

patch("src/OsslCryptoEngine/MathFunctions.c", [
    ("""// BN_is_prime returning -1 means that it ran into an error.
        //  It  should     only   return  0  or  1
        //
        if((isPrime     =  BN_is_prime_ex(p,        BN_prime_checks,  NULL,  NULL))  <  0)""",
     """//     BN_check_prime           returning        -1  means        that  it    ran  into  an  error.\n"""
     """   //  It  should     only   return  0  or  1\n"""
     """   //\n"""
     """   if((isPrime     =  BN_check_prime(p,        NULL,  NULL))  <  0)""", 1),
])


# ============================================================ the randomness
#
# One constant first, beside the file it governs: how much security strength
# every draw asks for.  256 is the ceiling a CTR-DRBG over AES-256 will
# satisfy -- probe/step08 measured 256 accepted and 257 refused -- and asking
# for it is what makes the fourth argument a constraint rather than a field
# that got filled in.  The generator's name is not here; it belongs beside the
# context it is set on, which is CpriCryptPri.c, below.
# The generator's name, and the one call that decides which implementation
# serves this context's DRBG chain.  Both go here rather than in CpriRNG.c
# because both are properties of the CONTEXT, and because this is the only
# place in the engine that runs exactly once per process: every startup entry
# point below it is re-entered -- _cpri__Startup() calls _cpri__RngStartup()
# on every TPM2_Startup, and _TPM_Init re-enters _cpri__InitCryptoUnits on
# every power-on -- and RAND_set_DRBG_type is a one-shot that returns 0 to
# every call after the chain exists.
patch("src/OsslCryptoEngine/CpriCryptPri.c", [
    ('''#define    OSSL_ENGINE_FIPS_CONF    "/usr/local/ssl/fips.cnf"
        #define    OSSL_ENGINE_PROPQ        "provider=default"''',
     """#define        OSSL_ENGINE_FIPS_CONF          "/usr/local/ssl/fips.cnf"\n"""
     """#define        OSSL_ENGINE_PROPQ              "provider=default"\n"""
     """\n"""
     """//   And     the    generator            this     engine        draws      from.        Unnamed,        the\n"""
     """//   context's             DRBG      chain      resolves          to    whichever            provider          was\n"""
     """//   loaded        first        --   which       is   load      order,        not     a   decision          this\n"""
     """//   source        makes,       and     is    the    same      objection            the    query       above\n"""
     """//   answers         for    every       other       fetch.\n"""
     '''#define        OSSL_ENGINE_DRBG               "CTR-DRBG"''', 1),

    # The naming, at the end of the once-per-process block: the module is
    # loaded and has passed its own tests, the context is complete, and
    # nothing in this process has drawn a byte from it yet.  That last part is
    # the whole point -- after the first draw the same call returns 0 and
    # leaves the chain exactly as it found it, so the return value is the only
    # evidence that the call happened in time, and it is checked.
    ("""if(OSSL_PROVIDER_self_test(s_fips) != 1)
        FAIL(FATAL_ERROR_INTERNAL);
        }""",
     """if(OSSL_PROVIDER_self_test(s_fips)                                   !=  1)\n"""
     """                 FAIL(FATAL_ERROR_INTERNAL);\n"""
     """\n"""
     """          //  Last,       while       this      context         is   still      new:      name      the\n"""
     """          //  generator             it   draws       from,       with      the    same      query      every\n"""
     """          //  other       fetch       carries.          This      decides         anything           only\n"""
     """          //  before        the    chain       is   instantiated,                 and    a   chain\n"""
     """          //  instantiates                on   first       use,      so    it   is   here      and    not\n"""
     """          //  in    the    unit      that      draws.        Called        late      it   returns          0\n"""
     """          //  and     changes         nothing,          which       is   why      the    answer        is\n"""
     """          //  read.\n"""
     """          if(RAND_set_DRBG_type(s_libCtx,             OSSL_ENGINE_DRBG,\n"""
     """                                OSSL_ENGINE_PROPQ,               NULL,     NULL)     !=  1)\n"""
     """                 FAIL(FATAL_ERROR_INTERNAL);\n"""
     """   }""", 1),
])


patch("src/OsslCryptoEngine/CpriRNG.c", [
    ("""#include    "OsslCryptoEngine.h"
        int            s_entropyFailure;""",
     """#include    "OsslCryptoEngine.h"\n"""
     """\n"""
     """//   The     security            strength          every      draw      in    this     file     asks      for,\n"""
     """//   written         here      once      for     the     same      reason        the     property           query\n"""
     """//   is    written         once      in    CpriCryptPri.c:                    a   draw      that      states        no\n"""
     """//   requirement               is   a   requirement                this      source        did     not     make.\n"""
     """#define        OSSL_ENGINE_RNG_STRENGTH               256\n"""
     """\n"""
     """int            s_entropyFailure;""", 1),

    # The seed.  RAND_seed() put the platform's entropy into the process
    # default's chain, which is not the chain this engine draws from once the
    # draws name the context.  EVP_RAND_reseed() puts it into the primary DRBG
    # of this engine's own context -- as ADDITIONAL INPUT, because a DRBG with
    # a live parent refuses caller-supplied entropy outright (rc=0, counter
    # unmoved) and accepts the same bytes in the other slot.
    ("""//  Seed  OpenSSL       with  entropy
        RAND_seed(entropy,          entropySize);""",
     """//  Seed     this     engine's         own    generator         with    the     platform's\n"""
     """          //  entropy.       The    bytes     go   in   as   additional         input     rather\n"""
     """          //  than    as   entropy:       this    DRBG     has    a  parent       to   draw\n"""
     """          //  entropy      from    and    refuses      a  caller's       own,    which     is   a\n"""
     """          //  refusal      that     shows     up   in   a  return      value     and    nowhere\n"""
     """          //  else.\n"""
     """          if(EVP_RAND_reseed(RAND_get0_primary(OsslLibraryContext()),\n"""
     """                             0,   NULL,     0,   entropy,      entropySize)      !=  1)\n"""
     """                s_entropyFailure         =  TRUE;""", 1),

    # The same call for the same reason, on the entry point the platform uses
    # to add entropy after startup.  The caller ignores this result today, as
    # it ignored RAND_add()'s absence of one; the value is returned anyway
    # because a stir that did not stir is worth saying.
    ("""if     (entropySize      >=  0)
        {
        RAND_add((const          void  *)entropy,        (int)   entropySize,  0.0);

        }
        return     CRYPT_SUCCESS;""",
     """if     (entropySize      >  0)\n"""
     """   {\n"""
     """          //   Same     generator,            same     slot,      same      reason        as\n"""
     """          //   _cpri__RngStartup():                   the    bytes      are    additional            input      to\n"""
     """          //   this      engine's          own     chain,       not     entropy         for    a  chain      that\n"""
     """          //   has      its    own     source.\n"""
     """          if(EVP_RAND_reseed(RAND_get0_primary(OsslLibraryContext()),\n"""
     """                             0,     NULL,      0,   entropy,       (size_t)entropySize)        !=  1)\n"""
     """                return       CRYPT_FAIL;\n"""
     """   }\n"""
     """   return     CRYPT_SUCCESS;""", 1),

    # The public draw.
    ("""//     RAND_bytes     uses   1   for  success    and    we  use  0
        if(RAND_bytes(buffer,            randomSize)        ==  1)""",
     """//     RAND_bytes_ex          uses   1   for  success    and    we  use  0.    The   context\n"""
     """   //     is   this      engine's          own,     and    the    strength          is   what     these\n"""
     """   //     bytes       have      to   be   worth.\n"""
     """   if(RAND_bytes_ex(OsslLibraryContext(),               buffer,      (size_t)randomSize,\n"""
     """                    OSSL_ENGINE_RNG_STRENGTH)                 ==  1)""", 1),

    # And the private one.  The two pools stay two pools: the entry point a
    # caller picks is still what says which kind of bytes it is asking for.
    ("""//     RAND_priv_bytes   uses   1   for  success    and    we  use  0
        if(RAND_priv_bytes(buffer,            randomSize)        ==  1)""",
     """//     RAND_priv_bytes_ex          uses   1   for  success    and    we  use  0,   and\n"""
     """   //     draws       from      the    private         pool     of   the    same      context        the\n"""
     """   //     public        draw      uses.\n"""
     """   if(RAND_priv_bytes_ex(OsslLibraryContext(),               buffer,      (size_t)randomSize,\n"""
     """                         OSSL_ENGINE_RNG_STRENGTH)                 ==  1)""", 1),
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

# The fence itself, built here rather than only in the verifier, because a
# golden that does not compile behind the fence is a golden that would score
# this step's first gate 0 and say nothing about why.
say "fence"
F=$(mktemp -d)
cmake -S "$APP/cmake" -B "$F" -DSPEC_VERSION=116 \
    -DCMAKE_C_FLAGS="-DOPENSSL_API_COMPAT=0x30400000L -DOPENSSL_NO_DEPRECATED" \
    > "$F/configure.log" 2>&1 \
    || { tail -40 "$F/configure.log" >&2; die "fence configure failed"; }
cmake --build "$F" -j"${JOBS:-7}" > "$F/build.log" 2>&1 \
    || { grep -m10 ': error:' "$F/build.log" >&2; die "the tree does not build behind the fence"; }

# Building is not enough: behind the fence a retired FUNCTION is an implicit
# declaration, which C compiles.  The verifier reads the fence log for those
# names and counts only the ones its header census calls deprecated -- this
# tree implicitly declares its own _plat__GetUnique, which is a C bug that
# predates step 01 and is not this step's business.  Same reading here.
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
# the build does not compile, or in a branch the linker dropped, fails here.
for sym in RAND_bytes_ex RAND_priv_bytes_ex RAND_set_DRBG_type \
           RAND_get0_primary EVP_RAND_reseed \
           EC_POINT_get_affine_coordinates EC_POINT_set_affine_coordinates \
           BN_check_prime; do
    printf '%s\n' "$undef" | grep -qx "$sym" \
        || die "$sym is not an undefined symbol of libCryptoEngine.so"
done

# And what it must no longer ask for: the four context-free draws and the
# three names the fence retired.
for sym in RAND_bytes RAND_priv_bytes RAND_seed RAND_add \
           EC_POINT_get_affine_coordinates_GFp \
           EC_POINT_set_affine_coordinates_GFp BN_is_prime_ex; do
    if printf '%s\n' "$undef" | grep -qx "$sym"; then
        die "$sym is still an undefined symbol of libCryptoEngine.so"
    fi
done

# No deprecated name anywhere in the four objects, read the way the verifier
# reads it rather than from the list above.
if [ -x /tests/lib/ossldep.py ] || [ -f /tests/lib/ossldep.py ]; then
    python3 /tests/lib/ossldep.py --include /usr/local/include \
        --api 0x30400000L --out "$B/ossldep.json" > "$B/ossldep.log" 2>&1 \
        || die "the header census would not run"
    for o in "$B/platform/libplatform.so" "$ENG" "$B/tpm/libTPM.so" \
             "$B/Simulator"; do
        bad=$(python3 - "$B/ossldep.json" "$o" <<'PY'
import json, subprocess, sys
dep = set(json.load(open(sys.argv[1]))["deprecated"])
out = subprocess.run(["nm", "-D", "--undefined-only", sys.argv[2]],
                     capture_output=True, text=True).stdout
imps = {l.split()[-1].split("@")[0] for l in out.splitlines() if l.split()}
print(" ".join(sorted(dep.intersection(imps))))
PY
)
        [ -z "$bad" ] || die "$(basename "$o") still imports: $bad"
    done
fi

# The naming happens once, and in the window.  Checked by position rather than
# by presence, because the failure mode this step exists for is the same call
# in the wrong place: a second call, or a call after any draw, returns 0 and
# leaves the chain exactly as it found it.
n=$(grep -rho 'RAND_set_DRBG_type' src/ --include='*.c' | wc -l)
[ "$n" = 1 ] \
    || die "RAND_set_DRBG_type appears $n time(s) in the sources; it takes once"

awk '/^_cpri__InitCryptoUnits\(/          {in_fn=1}
     in_fn && /OSSL_PROVIDER_self_test/ && !tested  {tested=NR}
     in_fn && /RAND_set_DRBG_type/      && !named   {named=NR}
     in_fn && /_cpri__RngStartup\(\)/     && !started {started=NR}
     END {exit !(tested && named && started && tested < named && named < started)}' \
    src/OsslCryptoEngine/CpriCryptPri.c \
    || die "RAND_set_DRBG_type is not called in _cpri__InitCryptoUnits between the self test and the first unit startup"

# And it is inside the block that runs once per process, not on the path
# _TPM_Init re-enters on every power-on.
awk '/if\(s_libCtx *== *NULL\)/ {blk=1; depth=0}
     blk && /{/ {depth++}
     blk && /RAND_set_DRBG_type/ {inside=1}
     blk && /}/ {if (--depth == 0) blk=0}
     END {exit !inside}' \
    src/OsslCryptoEngine/CpriCryptPri.c \
    || die "the naming is outside the once-per-process context block"

grep -q 'OSSL_ENGINE_DRBG .*"CTR-DRBG"' src/OsslCryptoEngine/CpriCryptPri.c \
    || die "CpriCryptPri.c does not name the generator beside the context"

# The entropy goes in the slot that accepts it.  Both reseeds pass NULL and 0
# for the entropy arguments and the platform's buffer for the additional
# input; a tree that swapped them compiles, runs, and discards the bytes.
n=$(grep -c 'EVP_RAND_reseed(RAND_get0_primary(OsslLibraryContext())' \
        src/OsslCryptoEngine/CpriRNG.c)
[ "$n" = 2 ] \
    || die "expected 2 reseeds of this engine's primary DRBG, found $n"
n=$(grep -A1 'EVP_RAND_reseed(RAND_get0_primary(OsslLibraryContext())' \
        src/OsslCryptoEngine/CpriRNG.c | grep -c '0, *NULL, *0, *entropy')
[ "$n" = 2 ] \
    || die "the platform's entropy is not passed as additional input in both reseeds"

# Every draw names the context and a strength.
for fn in RAND_bytes_ex RAND_priv_bytes_ex; do
    grep -q "$fn(OsslLibraryContext()" src/OsslCryptoEngine/CpriRNG.c \
        || die "$fn does not draw from the engine's own library context"
done
grep -q 'OSSL_ENGINE_RNG_STRENGTH *256' src/OsslCryptoEngine/CpriRNG.c \
    || die "CpriRNG.c does not state a security strength for its draws"

# The two pools are still two pools -- step 02's property, which this step
# re-spells and does not relax.
grep -q 'RAND_priv_bytes_ex' src/OsslCryptoEngine/CpriRNG.c \
    || die "the private pool is gone"
if grep -n 'RAND_bytes_ex' src/OsslCryptoEngine/CpriRNG.c \
        | sed -n '/GeneratePrivateRandom/p' | grep -q .; then
    die "the private draw was rewritten onto the public pool"
fi

say "done"
