#!/usr/bin/env bash
# Step 6 golden solution -- the FIPS module brought up inside the engine's own
# library context, self-tested before it is trusted, released when the engine
# stops, and every fetch in the engine made to say which implementation it
# wants.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/06-fips-and-libctx/solution/solve.sh; edit it here and regenerate,
# never edit the copy under tasks/.
#
# Run from /app on the tree step 5 left behind, with OpenSSL 3.1.8 installed as
# the system OpenSSL.  It edits the tree in place and builds it once to prove
# the edits compile; the verifier builds it again from scratch, so nothing here
# is load-bearing except the source changes.
#
#
# WHAT THIS DOES, AND WHY EACH PIECE IS NEEDED
#
# Step 3 gave this engine a library context of its own and loaded one provider
# into it.  Steps 3, 4 and 5 moved every digest, every cipher and every public
# key operation onto that context.  All of it fetches with a NULL property
# query, and with one provider in the context NULL is the right answer: there
# is nothing for a query to choose between.
#
# This step puts a second provider there, and three things follow.
#
#   the configuration   The FIPS module is handed its own integrity MAC as
#                       configuration data.  A library context that has never
#                       loaded a configuration cannot load the provider at
#                       all: OSSL_PROVIDER_load returns NULL with
#                       "SELF_TEST_post: missing config data" however correct
#                       the file on disk is.  So the engine calls
#                       OSSL_LIB_CTX_load_config on ITS OWN context.  Not
#                       CONF_modules_load_file, which configures the process,
#                       and not OSSL_LIB_CTX_load_config(NULL, ...), which is
#                       the same mistake spelled in this step's vocabulary:
#                       either one configures a context the engine does not
#                       fetch from, and undoes step 3.
#
#   the self test       OSSL_PROVIDER_load returning non-NULL says the module
#                       was found and its MAC checked at load.  It does not
#                       say the module's algorithm self tests passed, and the
#                       operator who has to attest to the module is asking
#                       about those.  OSSL_PROVIDER_self_test() runs them, and
#                       its RESULT is the point: a tree that calls it and
#                       throws the answer away has written a line of code, not
#                       a check.  The engine takes the TPM failure path when
#                       it is not 1, which is what FAIL() is for.
#
#   the property query  With two providers in one context, what an unqualified
#                       fetch resolves to is decided by the order they were
#                       loaded in -- the first explicit OSSL_PROVIDER_load
#                       wins, and where there was no explicit load the
#                       configuration's own activation wins.  Nothing in the
#                       source records that decision.  This golden loads
#                       "default" first, so its NULL queries would in fact
#                       have kept working; it names them anyway, and that is
#                       the point of the step.  A port whose answers depend on
#                       which of two adjacent lines it happened to write first
#                       has not migrated, it has got lucky.  The eight sites
#                       are the five call shapes the probe watches, in four
#                       files.  NOTES-authoring.md section 10.7 has the five
#                       load orders measured on this release.
#
# One accessor carries the query, for the same reason OsslLibraryContext()
# carries the context: the decision is written once, in the file that owns the
# context, and the algorithm files ask for it rather than each spelling a
# string of their own.
#
# WHAT IS NOT DONE HERE
#
# The engine is not made to compute in FIPS mode.  It cannot be: both of this
# TPM's RSA keys are 1024-bit and it builds every one of its EC curves from
# fully explicit parameters, neither of which the module does.  The
# module is present, loadable, self-tested and released; the engine's answers
# are unchanged, and the corpus is what says so.

set -euo pipefail

APP=${APP:-/app}
cd "$APP"

say()  { printf '\n== %s\n' "$*"; }
die()  { printf 'solve: %s\n' "$*" >&2; exit 1; }

for f in src/OsslCryptoEngine/CpriCryptPri.c src/OsslCryptoEngine/CpriHash.c \
         src/OsslCryptoEngine/CpriSym.c src/OsslCryptoEngine/CpriRSA.c \
         src/OsslCryptoEngine/CpriECC.c \
         src/OsslCryptoEngine/OsslCryptoEngine.h cmake/CMakeLists.txt; do
    [ -f "$f" ] || die "$f is missing -- this is not the tree step 5 produced"
done

# Steps 3 to 5 have to be here already: this step edits the property query at
# call sites those steps created, and a tree without them is a tree the
# previous steps did not run on.
grep -q 'OSSL_PROVIDER_load' src/OsslCryptoEngine/CpriCryptPri.c \
    || die "src/OsslCryptoEngine/CpriCryptPri.c loads no provider -- step 3 has not been applied"
grep -q 'EVP_CIPHER_fetch' src/OsslCryptoEngine/CpriSym.c \
    || die "src/OsslCryptoEngine/CpriSym.c does not fetch its ciphers -- step 4 has not been applied"
grep -q 'EVP_PKEY_fromdata' src/OsslCryptoEngine/CpriRSA.c \
    || die "src/OsslCryptoEngine/CpriRSA.c builds no EVP_PKEY -- step 5 has not been applied"

# The configuration the module is described by.  Reported by the verifier and
# not gated: what is gated is that A configuration went into the engine's own
# context, returned success, and produced a provider named "fips" out of that
# same context.
FIPS_CONF=${FIPS_CONF:-/usr/local/ssl/fips.cnf}
[ -f "$FIPS_CONF" ] \
    || die "$FIPS_CONF is missing -- this image has no FIPS configuration"

say "source"
FIPS_CONF="$FIPS_CONF" python3 - <<'PY'
import os, re, sys

EDITS = 0
CONF = os.environ["FIPS_CONF"]


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


# ======================================================= the engine's own header
#
# The property query joins the library context as engine-internal plumbing.
# Neither is part of CryptoEngine.h, which is what the engine publishes; both
# are things the algorithm files have to ask CpriCryptPri.c for because
# CpriCryptPri.c is where the context is made and where the decision belongs.
patch("src/OsslCryptoEngine/OsslCryptoEngine.h", [
    ("""OSSL_LIB_CTX      *OsslLibraryContext(void);""",
     """OSSL_LIB_CTX      *OsslLibraryContext(void);\n"""
     """const  char       *OsslPropertyQuery(void);""", 1),
])


# ==================================================== the context, and what is in it
#
# conf.h is where OSSL_LIB_CTX_load_config is declared.  provider.h is already
# included here from step 3.
patch("src/OsslCryptoEngine/CpriCryptPri.c", [
    ("""#include  "OsslCryptoEngine.h"
        #include  <openssl/provider.h>""",
     """#include  "OsslCryptoEngine.h"\n"""
     """#include  <openssl/provider.h>\n"""
     """#include  <openssl/conf.h>""", 1),

    # The two statics become three, and the two strings the engine decides on
    # are written where the context is made rather than at the call sites that
    # use them.
    ("""static     OSSL_LIB_CTX       *s_libCtx      =  NULL;
        static     OSSL_PROVIDER      *s_default     =  NULL;""",
     """static     OSSL_LIB_CTX       *s_libCtx      =  NULL;\n"""
     """static     OSSL_PROVIDER      *s_default     =  NULL;\n"""
     """static     OSSL_PROVIDER      *s_fips        =  NULL;\n"""
     """\n"""
     """// The module's configuration, and the query every fetch in this engine\n"""
     """// carries.  Two providers answer to this context now, and an unqualified\n"""
     """// fetch picks between them by load order -- which is not a decision this\n"""
     """// source records, so it is not a decision this engine makes.  Every fetch\n"""
     """// names the implementation it wants instead, and the name is written here,\n"""
     """// once, beside the context it applies to.\n"""
     """#define    OSSL_ENGINE_FIPS_CONF    \"""" + CONF + """\"\n"""
     """#define    OSSL_ENGINE_PROPQ        "provider=default\"""", 1),

    # The accessor, immediately after the one it mirrors.
    ("""OSSL_LIB_CTX      *
        OsslLibraryContext(
              void
              )
        {
              return     s_libCtx;
        }""",
     """OSSL_LIB_CTX      *\n"""
     """OsslLibraryContext(\n"""
     """      void\n"""
     """      )\n"""
     """{\n"""
     """      return     s_libCtx;\n"""
     """}\n"""
     """\n"""
     """// B.6.3.0.1.     OsslPropertyQuery()\n"""
     """\n"""
     """// The property query every fetch in this engine is made with.  Engine\n"""
     """// internal, for the same reason and in the same place as the context: it is\n"""
     """// the third argument the algorithm files pass to EVP_xxx_fetch() and to\n"""
     """// EVP_PKEY_CTX_new_from_name()/_from_pkey().\n"""
     """const  char       *\n"""
     """OsslPropertyQuery(\n"""
     """      void\n"""
     """      )\n"""
     """{\n"""
     """      return     OSSL_ENGINE_PROPQ;\n"""
     """}""", 1),

    # Startup.  Three lines added inside the block that already makes the
    # context, in the order that matters: the configuration first, because the
    # module cannot load without it; "default" next, because it is the provider
    # steps 3 to 5 built on and the engine is adding to its context rather than
    # replacing what is in it; the module last, and then its self test.
    ("""if(s_libCtx         ==  NULL)
        {
               if((s_libCtx           =  OSSL_LIB_CTX_new())                        ==  NULL)
                      FAIL(FATAL_ERROR_ALLOCATION);
               if((s_default          =  OSSL_PROVIDER_load(s_libCtx,    "default"))  ==  NULL)
                      FAIL(FATAL_ERROR_INTERNAL);
        }""",
     """if(s_libCtx         ==  NULL)\n"""
     """   {\n"""
     """          if((s_libCtx           =  OSSL_LIB_CTX_new())                        ==  NULL)\n"""
     """                 FAIL(FATAL_ERROR_ALLOCATION);\n"""
     """\n"""
     """          //  The     context         is    new,      and     what      the     algorithm            tables\n"""
     """          //  hold      predates           it.      TPM_Manufacture()                  starts        the    crypto\n"""
     """          //  units       before        any     _TPM_Init()               reaches         here      --   it   has    to,\n"""
     """          //  the     seeds       it   makes       are     hashed        --   so    the    digests         and\n"""
     """          //  ciphers         fetched         once      can     already         be    there,        and     the\n"""
     """          //  context         they      came      out     of    was     the     NULL      one,      which       is\n"""
     """          //  the     process           default        and     not     this      engine's.             The     fetch\n"""
     """          //  guards        below       would       then      keep      them      forever.          They      are\n"""
     """          //  given       back      here,       and     the     startups            at    the    foot      of    this\n"""
     """          //  function          fetch       them      again       from      the     context         just      made.\n"""
     """          OsslHashCleanup();\n"""
     """          OsslSymCleanup();\n"""
     """\n"""
     """          //  The     module      is    described        by    configuration       data   --    its\n"""
     """          //  own     integrity       MAC     among       it   --    and    a   context      that   has\n"""
     """          //  never       read     any     cannot      load     it   at   all.    Into     THIS\n"""
     """          //  context:        the    process       default        is   not    what    this    engine\n"""
     """          //  fetches       from.\n"""
     """          if(OSSL_LIB_CTX_load_config(s_libCtx,      OSSL_ENGINE_FIPS_CONF)   !=  1)\n"""
     """                 FAIL(FATAL_ERROR_INTERNAL);\n"""
     """\n"""
     """          if((s_default          =  OSSL_PROVIDER_load(s_libCtx,    "default"))  ==  NULL)\n"""
     """                 FAIL(FATAL_ERROR_INTERNAL);\n"""
     """\n"""
     """          //  The     module,      held     for   the    life    of   the    engine      so   that   it\n"""
     """          //  can     be    self     tested      here     and    given      back     at   shutdown.\n"""
     """          if((s_fips             =  OSSL_PROVIDER_load(s_libCtx,    "fips"))     ==  NULL)\n"""
     """                 FAIL(FATAL_ERROR_INTERNAL);\n"""
     """\n"""
     """          //  And     the    answer       is   the    point:      a   module      that   loaded      but\n"""
     """          //  did     not    pass     its    own    tests      is   a   module      this     engine\n"""
     """          //  must     not    start      on.\n"""
     """          if(OSSL_PROVIDER_self_test(s_fips)                                   !=  1)\n"""
     """                 FAIL(FATAL_ERROR_INTERNAL);\n"""
     """   }""", 1),

    # Shutdown.  The module goes back before the provider it was added beside,
    # and both before the context they live in.
    ("""if(s_default        !=  NULL)
        {
               OSSL_PROVIDER_unload(s_default);
               s_default    =  NULL;
        }""",
     """if(s_fips           !=  NULL)\n"""
     """   {\n"""
     """          OSSL_PROVIDER_unload(s_fips);\n"""
     """          s_fips       =  NULL;\n"""
     """   }\n"""
     """   if(s_default        !=  NULL)\n"""
     """   {\n"""
     """          OSSL_PROVIDER_unload(s_default);\n"""
     """          s_default    =  NULL;\n"""
     """   }""", 1),
])


# =============================================== the eight queries, named
#
# Five call shapes, four files.  Every one of them was NULL, and every one of
# them is now the engine's own answer.  The counts are written out per file so
# that an anchor that stops matching -- because a later step moved one of these
# calls -- fails here rather than silently leaving a NULL behind.
patch("src/OsslCryptoEngine/CpriHash.c", [
    ("""s_hashServer[i].md     =  EVP_MD_fetch(OsslLibraryContext(),
                                                       s_hashServer[i].name,     NULL);""",
     """s_hashServer[i].md     =  EVP_MD_fetch(OsslLibraryContext(),\n"""
     """                                                   s_hashServer[i].name,\n"""
     """                                                   OsslPropertyQuery());""", 1),
])

patch("src/OsslCryptoEngine/CpriSym.c", [
    ("""EVP_CIPHER_fetch(OsslLibraryContext(),
                                                  s_symServer[mode][size].name,               NULL);""",
     """EVP_CIPHER_fetch(OsslLibraryContext(),\n"""
     """                                  s_symServer[mode][size].name,\n"""
     """                                  OsslPropertyQuery());""", 1),
])

patch("src/OsslCryptoEngine/CpriRSA.c", [
    ("""(ctx    =  EVP_PKEY_CTX_new_from_name(OsslLibraryContext(),
                                                       "RSA",    NULL))    ==   NULL""",
     """(ctx    =  EVP_PKEY_CTX_new_from_name(OsslLibraryContext(),\n"""
     """                                                   "RSA",\n"""
     """                                                   OsslPropertyQuery()))  ==   NULL""", 1),
    # Both of CpriRSA.c's _from_pkey sites at once.  patch() normalises the
    # whitespace in its anchors, so the two spellings of this call are one
    # anchor and asking for them separately would match twice each.
    ("""EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),
                                                        pkey,     NULL))     ==   NULL""",
     """EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),\n"""
     """                                                    pkey,\n"""
     """                                                    OsslPropertyQuery()))  ==   NULL""", 2),
])

patch("src/OsslCryptoEngine/CpriECC.c", [
    ("""(ctx   =   EVP_PKEY_CTX_new_from_name(OsslLibraryContext(),
                                                       "EC",     NULL))   ==   NULL""",
     """(ctx   =   EVP_PKEY_CTX_new_from_name(OsslLibraryContext(),\n"""
     """                                                   "EC",\n"""
     """                                                   OsslPropertyQuery()))  ==   NULL""", 1),
    ("""(ctx   =   EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),
                                                       pkey,    NULL))   ==   NULL""",
     """(ctx   =   EVP_PKEY_CTX_new_from_pkey(OsslLibraryContext(),\n"""
     """                                                   pkey,\n"""
     """                                                   OsslPropertyQuery()))  ==   NULL""", 2),
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

# The four symbols that make this step's claims checkable in the binary rather
# than only in the source.  A tree that has the calls in a file the build does
# not compile, or inside a branch the linker dropped, fails here.
undef=$(nm -D --undefined-only "$ENG" | awk '{print $NF}' | sed 's/@.*//')
for sym in OSSL_LIB_CTX_new OSSL_LIB_CTX_load_config OSSL_LIB_CTX_free \
           OSSL_PROVIDER_load OSSL_PROVIDER_self_test OSSL_PROVIDER_unload; do
    printf '%s\n' "$undef" | grep -qx "$sym" \
        || die "$sym is not an undefined symbol of libCryptoEngine.so"
done

# Every one of the eight queries is named.  Counted rather than grepped for
# once, because "some of them" is the failure this assertion exists to catch.
n=$(grep -c 'OsslPropertyQuery()' src/OsslCryptoEngine/CpriHash.c \
                                  src/OsslCryptoEngine/CpriSym.c \
                                  src/OsslCryptoEngine/CpriRSA.c \
                                  src/OsslCryptoEngine/CpriECC.c \
      | awk -F: '{s += $2} END {print s}')
[ "$n" = 8 ] || die "expected 8 named property queries in the algorithm files, found $n"

# And none is left NULL.  Parsed rather than grepped: the last top-level
# argument of every one of the five call shapes, in every file under
# src/OsslCryptoEngine/, has to be something other than NULL.  A regular
# expression cannot find the end of an argument list that spans four lines and
# contains its own parentheses, and a check that quietly matches nothing is
# worse than no check.
python3 - <<'PQ' || die "a fetch or key-context call still passes NULL as its property query"
import glob, sys

NAMES = ("EVP_MD_fetch", "EVP_CIPHER_fetch", "EVP_Q_digest",
         "EVP_PKEY_CTX_new_from_name", "EVP_PKEY_CTX_new_from_pkey")
bad = 0
seen = 0
def uncomment(text):
    """Blank out comments, preserving every offset so line numbers stay true.
    Without this the scanner finds these names in the prose beside the code."""
    out = list(text)
    i, n = 0, len(text)
    while i < n - 1:
        two = text[i:i + 2]
        if two == "//":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif two == "/*":
            while i < n and text[i:i + 2] != "*/":
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            out[i:i + 2] = "  "
            i += 2
        else:
            i += 1
    return "".join(out)


for path in sorted(glob.glob("src/OsslCryptoEngine/*.c")):
    src = uncomment(open(path).read())
    for name in NAMES:
        i = 0
        while True:
            i = src.find(name + "(", i)
            if i < 0:
                break
            j = i + len(name)
            depth, start, args = 0, 0, []
            for k in range(j, len(src)):
                c = src[k]
                if c == "(":
                    depth += 1
                    if depth == 1:
                        start = k + 1
                elif c == ")":
                    depth -= 1
                    if depth == 0:
                        args.append(src[start:k])
                        break
                elif c == "," and depth == 1:
                    args.append(src[start:k])
                    start = k + 1
            i = j
            if not args:
                continue
            seen += 1
            last = " ".join(args[-1].split())
            if last == "NULL":
                bad += 1
                print("solve: %s:%d: %s still passes NULL as its property query"
                      % (path, src.count(chr(10), 0, i) + 1, name),
                      file=sys.stderr)
print("solve: %d call site(s) checked, %d still NULL" % (seen, bad))
sys.exit(1 if bad else 0)
PQ

# The configuration goes into the engine's context, not the process default.
grep -q 'OSSL_LIB_CTX_load_config(s_libCtx' src/OsslCryptoEngine/CpriCryptPri.c \
    || die "OSSL_LIB_CTX_load_config is not called on the engine's own context"
if grep -rn 'CONF_modules_load_file' src/OsslCryptoEngine/ ; then
    die "the tree configures the process instead of its own library context"
fi

# The self test's result is used.  A bare call would compile and would mean
# nothing, which is the whole reason the verifier runs the probe twice.
grep -q 'if(OSSL_PROVIDER_self_test(s_fips)' src/OsslCryptoEngine/CpriCryptPri.c \
    || die "OSSL_PROVIDER_self_test's result is not checked"

# And nothing the engine fetched before this context existed is still held.
# The two tables are the only things in the engine that outlive the call that
# filled them, and TPM_Manufacture() fills them before any _TPM_Init() makes a
# context, so a context made without dropping them is a context the engine does
# not in fact fetch from -- true in the source and false at run time.  Asserted
# on position, because both names also appear in _cpri__StopCryptoUnits() and
# what matters is that they appear in the function that makes the context.
python3 - <<'DROP' || die "the new library context does not drop the tables fetched before it"
import sys
src = open("src/OsslCryptoEngine/CpriCryptPri.c").read()
i = src.index("_cpri__InitCryptoUnits(")
j = src.index("_cpri__StopCryptoUnits(")
body = src[i:j]
k = body.index("OSSL_LIB_CTX_new()")
after = body[k:]
for name in ("OsslHashCleanup();", "OsslSymCleanup();"):
    if name not in after:
        print("solve: %s is not called after the context is made" % name,
              file=sys.stderr)
        sys.exit(1)
# ... and before the startups that refill them, or the drop undoes the refill.
for startup in ("_cpri__HashStartup();", "_cpri__SymStartup();"):
    if after.index(startup) < after.index("OsslSymCleanup();"):
        print("solve: %s runs before the tables are dropped" % startup,
              file=sys.stderr)
        sys.exit(1)
print("solve: the new context drops both algorithm tables before refilling them")
DROP

say "done"
