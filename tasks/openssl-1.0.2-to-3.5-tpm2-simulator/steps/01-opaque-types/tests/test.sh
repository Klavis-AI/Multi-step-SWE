#!/usr/bin/env bash
# PRODUCT SCORING (2026-10-13): the reward is now T1 x T2 -- T1 = this
# step's own gates (`migrated`), T2 = behaviour and the retained gates of
# earlier steps at their old relative weights -- see `Product scoring` at
# the reward.  Any weight or floor arithmetic stated below describes the
# superseded weighted sum.
# Step 1 verifier -- opaque types, against OpenSSL 1.1.0l.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/01-opaque-types/tests/test.sh; edit it here and regenerate, never edit
# the copy under tasks/.
#
#
# WHAT THIS STEP IS
#
# The repository builds its crypto engine against a copy of OpenSSL 1.0.2b
# vendored into the tree: headers in lib/openssl/, static archives in
# lib/x86-64/.  Those headers publish every struct, and the engine reaches
# inside them -- ctxt->digest->ctx_size, ctxt->md_data, EC_GROUP and EC_POINT
# fields out of the OpenSSL-internal <openssl/ec_lcl.h> -- and holds EVP_MD_CTX
# by value in its own structures.  1.1.0 stopped publishing the layouts of the
# types it reaches into, EVP_MD_CTX among them.  Not of all of them: AES_KEY is
# a complete public struct at every version in this migration, and gate 4 is
# written so that holding one by value is not a finding here.  Turning the
# engine's symmetric path onto EVP is step 4's work, and step 4 asks for it.
# The step is: delete the vendored copy, build against the system OpenSSL
# 1.1.0l, and get back to byte-identical behaviour through the accessors.
#
#
# HOW IT IS SCORED
#
#     reward = 0.45 * behaviour-equivalence + 0.55 * migrated-ness
#
#         behaviour = mean( breadth, worst )
#             breadth = mean( every engine case, every e2e case )
#             worst   = the lowest pass fraction reached by any one family,
#                       over all 39 engine and 17 e2e families
#         migrated  = the fraction of the six gates below that pass
#
# Both families are recorded from the untouched baseline; nothing here is an
# authored expectation.  The split is 45/55 rather than 55/45 so that migrated-
# ness is the larger half: a tree that reproduces every recorded answer and has
# migrated nothing cannot reach 0.5 on behaviour alone.  0.45 is the shape of
# that floor for a step whose predecessor already builds against the system
# libcrypto.  For THIS step the floor is lower, and by design: the predecessor
# is the untouched baseline, which fails P3 below and reports 0 outright.  It is
# not that the baseline fails every gate -- it does -- but that it never reaches
# them.  See "WHAT SCORES ZERO OUTRIGHT" for why P3 is where that lands.
#
# `worst` is here because a mean over a corpus this size cannot see a defect,
# and because the obvious repair -- weighting the families the author expects
# the defect to land in -- is worse than the disease.  Both were measured.
#
# Two calibrated defects: a port that keeps a live EVP_MD_CTX * inline in
# CPRI_HASH_STATE, which compiles and migrates and is wrong (a TPM hash
# *sequence* is copied bitwise by the layer above -- ContextSave moves the
# OBJECT that holds it -- so two containers share one context); and a port that
# leaves the public EC point unrepaired, which destroys ECC verification and
# takes the TPM into failure mode.  Under a plain case-count mean they scored
# 0.989 and 0.988.  Under the first version of this file, which weighted a
# hand-picked sequence slice at half the behaviour family, the first defect
# still cost about 1% -- and the second was actively *shielded*, because its
# slice was untouched at 1.000 and made up half the family.  A hand-picked slice
# can only anticipate the defect its author already thought of.
#
# `worst` picks nothing.  It asks which behaviour this tree handles worst and
# how badly, which is the instruction's own contract -- "a change in an answer
# is a defect in the port, whatever else improved" -- and it costs the same
# whichever family the defect lands in.  Paired with breadth in a mean it still
# pays partial credit: a tree that destroys one family outright keeps half of
# breadth, and a tree that loses a case or two anywhere loses a case or two.
# NOTES § 5.3 records what each calibration scores under it.
#
# It also puts a standing obligation on every later step: any inherited case
# that a step's own requirements make unpassable stops being a rounding error
# and becomes the whole `worst` term.  Steps re-record exactly what they change
# (rule 5's must-flip exception) or they do not change it.
#
#
# WHAT SCORES ZERO OUTRIGHT, AND WHY IT IS NOT A GATE
#
# Five preconditions.  Each is a statement about whether there is anything here
# to score at all, so each reports 0 and says so rather than contributing a
# fraction:
#
#   P0  /app arrived intact               environment failure, not the candidate
#   P1  the image binds OpenSSL 1.1.0l    environment failure, not the candidate
#   P2  the tree configures and builds    nothing to measure
#   P3  every built object gets OpenSSL from the loader and none of them
#       defines an OpenSSL symbol itself
#   P4  an outside consumer still compiles against the public headers
#
# P3 is the one that makes the reward contract work.  The untouched baseline
# builds happily in this image -- it is self-contained against its own vendored
# archives -- and reproduces both corpora exactly, so behaviour alone would hand
# a nop 0.45.  P3 is the fact a nop cannot fake: libCryptoEngine.so has to have
# a libcrypto in DT_NEEDED and define zero OpenSSL symbols, and the baseline has
# it exactly backwards (no libcrypto, and 2451 OpenSSL symbols defined by the
# engine itself, because 1.0.2b is linked in statically).  "OpenSSL symbol" is
# whatever the image's own libcrypto exports, so that count is universe-relative
# -- the same object reads 2414 against 3.5.4's export list.  No gate reads the
# figure; P3 reads only the zero.
#
# P4 is rule 1: cpridrv.c includes CryptoEngine.h and links -lCryptoEngine
# -lplatform, nothing else.  A tree whose public header no longer compiles
# against a consumer has not migrated the engine, it has replaced it.  The
# instruction says this in as many words.
#
#
# THE SIX GATES
#
# Every one of them is false on the untouched baseline, which is what rule 5
# requires of the migrated-ness family: the previous tree must fail it
# completely, not collect credit for checks that are vacuously true.  That is
# why "zero suppressions" is not here -- the baseline has zero suppressions
# already.  It arrives with step 10, which is the step that asks for it.
#
#   1  no file under /app declares OpenSSL's API            (baseline: 76)
#   2  no archive under /app defines OpenSSL's symbols      (baseline: 7)
#   3  zero member accesses on OpenSSL-declared records    (baseline: 35)
#   4  zero OpenSSL structs held by value                  (baseline: 6)
#   5  the engine allocates digest contexts through the
#      1.1.0 lifecycle: EVP_MD_CTX_new and _free both
#      imported and both called                           (baseline: 0 imports)
#   6  the engine's own init entry point calls
#      OPENSSL_init_crypto                                 (baseline: 0 imports)
#
# 1 and 2 are facts about file contents rather than file names: `git mv
# lib/openssl lib/ossl-compat` passed the first version of both.
#
# 3 and 4 are AST facts over exactly what the build compiled, counted as
# expressions and sites.  5 is both an ELF fact and an AST one, and the source
# text is not the property either way: EVP_MD_CTX_create and _destroy are macros
# for _new and _free in 1.1.0, so what the object imports and what the AST sees
# called after preprocessing are the same for both spellings.
#
# 6 is both, and needs both.  An import proves some translation unit mentions
# the symbol; it does not prove the engine's startup path reaches it, and a call
# sitting in a file nothing calls would satisfy an import-only test while the
# library stayed implicitly initialised.  So the object has to import
# OPENSSL_init_crypto and the AST has to find a call to it inside
# _cpri__InitCryptoUnits, which is the entry point the engine already has for
# exactly this.
#
# Gate 6 names OPENSSL_init_crypto and nothing else, because a step's gates are
# written for the OpenSSL that step targets and 1.1.0 is where explicit
# initialisation is spelled that way.  Every step is scored by its own gates
# against its own submission, so a later step that moves the engine to some
# other initialisation is a later step's gate to write; accepting that spelling
# here would pass this step for work this step never asked for -- and for a
# symbol this image's OpenSSL does not even export.
set -u
source /tests/helpers.sh

harden_reward_dir
stamp_step

# ---------------------------------------------------------- P0, P1: the image
assert_app
assert_ossl

# --------------------------------------------------------------- P2: it builds
#
# The exact command is the one the instruction names:
#     cmake -S /app/cmake -B <build> -DSPEC_VERSION=116
#     cmake --build <build>
# out of tree, unprivileged, and all four of platform, CryptoEngine, TPM and
# Simulator have to appear.
if ! build_app; then
    vlog "P2 FAILED: the tree does not configure and build -- scoring 0"
    detail_add "$CHECK_OWNER" "precondition:builds" 0
    report 0
fi
detail_add "$CHECK_OWNER" "precondition:builds" 1

# ------------------------------------------- P3: OpenSSL comes from the loader
ENGINE=$WORK/build/OsslCryptoEngine/libCryptoEngine.so

# All four of the artifacts the build produces, not just the three that have an
# obvious reason to touch OpenSSL.  "Every object the build produces" is the
# requirement, and a rebuilt OpenSSL compiled into libplatform.so would leave
# the other three looking exactly right.
scan_elf "$ENGINE"                  > "$LOG/elf-engine.json" 2>>"$LOG/verify.log"
scan_elf "$WORK/build/platform/libplatform.so" \
                                    > "$LOG/elf-plat.json"   2>>"$LOG/verify.log"
scan_elf "$WORK/build/tpm/libTPM.so" > "$LOG/elf-tpm.json"   2>>"$LOG/verify.log"
scan_elf "$WORK/build/Simulator"    > "$LOG/elf-sim.json"    2>>"$LOG/verify.log"

needs=$(jhas "$LOG/elf-engine.json" needed '^libcrypto\.so')
defs=$(jnum "$LOG/elf-engine.json" openssl_defines)
imps=$(jnum "$LOG/elf-engine.json" openssl_imports)
platdefs=$(jnum "$LOG/elf-plat.json" openssl_defines)
tpmdefs=$(jnum "$LOG/elf-tpm.json" openssl_defines)
simdefs=$(jnum "$LOG/elf-sim.json" openssl_defines)

# Which libcrypto, and not only that there is one.  The image installs this
# step's release under /usr/local and /opt/ossl/activate.sh sweeps away any
# other OpenSSL a compiler or linker could find, so the ordinary way to link is
# also the only convenient one.  What that leaves is a tree that names a
# versioned runtime object outright: Debian's libcrypto.so.3 is on the box
# because git and curl are linked against it, and it is reachable by soname.
# Below 3.0 that is a different soname from the installed release and this
# check is redundant; from 3.0 up it is the same one, and a tree compiled
# against the step's headers but loaded against Debian's build would come
# through the corpora looking correct.  Ask the loader where it ends up.  ldd
# runs the object's interpreter, so it runs unprivileged, like the build.
resolved=$(run_unprivileged ldd "$ENGINE" 2>/dev/null |
           awk '/libcrypto\.so/ {print $3; exit}')
case "$resolved" in
    /usr/local/lib/*) osslpath=1 ;;
    *)                osslpath=0 ;;
esac

vlog "link: libCryptoEngine.so  DT_NEEDED libcrypto=$needs" \
     "openssl defines=$defs imports=$imps"
vlog "link: libplatform.so defines=$platdefs  libTPM.so defines=$tpmdefs" \
     "Simulator defines=$simdefs"
vlog "link: the loader resolves libcrypto to '${resolved:-nothing}'"

if [ "$needs" != 1 ] || [ "$defs" != 0 ] || [ "$imps" -lt 1 ] \
   || [ "$platdefs" != 0 ] || [ "$tpmdefs" != 0 ] || [ "$simdefs" != 0 ] \
   || [ "$osslpath" != 1 ]; then
    vlog "P3 FAILED: this tree does not get OpenSSL from the system library."
    vlog "  wanted: libcrypto in libCryptoEngine.so's DT_NEEDED, at least one"
    vlog "          OpenSSL symbol imported, zero defined by any object, and"
    vlog "          the loader resolving that libcrypto under /usr/local/lib,"
    vlog "          where this step's release is installed."
    vlog "  a tree that still links the vendored 1.0.2 archives looks exactly"
    vlog "  like this, whatever else it has changed."
    detail_add "$CHECK_OWNER" "precondition:openssl-from-loader" 0
    report 0
fi
detail_add "$CHECK_OWNER" "precondition:openssl-from-loader" 1

# --------------------------------------- the public interface an outside
# ------------------------------------------ consumer compiles against
#
# This was a fifth precondition once, reporting 0 for the whole step when the
# driver did not compile.  That was wrong, and not by a little: the preceding
# four are statements about whether there is anything to measure, and this one
# is not.  If CryptoEngine.h stops compiling for an outside consumer, the
# vendored copy is still gone or still there, the AST still says what the tree
# does with OpenSSL's types, the engine is still linked the way it is linked,
# and the Simulator still answers 1048 e2e cases -- none of which needs
# cpridrv to be measured.  A tree that migrated OpenSSL correctly and broke one
# prototype in a header scored 0.000000, which said nothing about how much of
# the step it had done.
#
# So the driver's failure costs exactly what it actually takes away: the engine
# corpus, all 1676 cases of it, scored as a replay that produced no answers.
# That is 39 of the 56 families at 0, which puts `worst` at 0 and halves
# `breadth` -- about three quarters of the behaviour family -- and leaves the
# gates and the e2e corpus to report what they measured.
if build_driver; then
    driver_ok=1
    detail_add "$CHECK_OWNER" "public-api-compiles" 1
else
    driver_ok=0
    vlog "an outside consumer no longer compiles and links against the public"
    vlog "  headers (CryptoEngine.h + -lCryptoEngine -lplatform).  The engine's"
    vlog "  C API is an interface this step must preserve, so every engine case"
    vlog "  scores as unanswered; the gates and the TCP corpus are unaffected."
    detail_add "$CHECK_OWNER" "public-api-compiles" 0
fi

# ================================================================ the gates ==

# --- 1, 2: the vendored copy is gone from the tree, not merely unreferenced.
#
# Over the shipped tree, not over the build: a copy that is still on disk is a
# copy the next step can quietly start using again.
#
# By content, not by path.  The first version of these two gates was
#
#     find /app -name '*.h' -path '*/openssl/*'
#     find /app \( -name libcrypto.a -o -name libssl.a -o -name libeay32.lib \)
#
# and `mv lib/openssl lib/ossl-compat` passes both of them with the whole of
# 1.0.2b still in the tree.  The instruction says "not moved aside"; these two
# gates are 18% of the step's reward; so the check reads the bytes.
# vendorscan.py calls a file an OpenSSL header when it declares OpenSSL's API
# (a HEADER_*_H guard on a preprocessor line, or the 3.x OPENSSL_*_H spelling
# together with OpenSSL's copyright block) or carries OpenSSL's copyright under
# a header's name, and an archive an OpenSSL archive when its ar symbol index
# defines names only OpenSSL defines.  Both tests are applied through gzip, xz,
# bzip2, zip and tar as well, so a compressed copy is still a copy.  On the
# untouched baseline it reports the same 76 and 7 the name-based check did.
#
# A scan that did not run fails both gates, for the same reason the AST scan
# does: unmeasured is not the same as clean.  A container it could not open is
# the same statement about one file, and fails them too.
if scan_vendor > "$LOG/vendor-counts.json" 2>>"$LOG/verify.log"; then
    vend_h=$(jnum "$LOG/vendor-counts.json" headers)
    vend_a=$(jnum "$LOG/vendor-counts.json" archives)
    vend_n=$(jnum "$LOG/vendor-counts.json" scanned)
    vend_u=$(jnum "$LOG/vendor-counts.json" unopened)
    vlog "vendor: $vend_n files scanned, headers=$vend_h archives=$vend_a" \
         "unreadable containers=$vend_u"
else
    vend_h=-1; vend_a=-1; vend_n=0; vend_u=0
    vlog "vendor: the scan did not run"
fi

if [ "$vend_n" -lt 100 ]; then
    # /app has 172 C files and 184 headers before anything is added to it; a
    # scan that saw fewer than 100 files of any kind did not see this tree.
    gate 0 vendored-headers "the vendor scan is not trustworthy" \
        "($vend_n files seen)"
    gate 0 vendored-archives "the vendor scan is not trustworthy"
elif [ "$vend_u" -ne 0 ]; then
    gate 0 vendored-headers "$vend_u archive(s) under /app could not be" \
        "opened, so what is inside them has not been read" \
        "(see vendor.json for paths)"
    gate 0 vendored-archives "$vend_u archive(s) under /app could not be opened"
else
    if [ "$vend_h" -eq 0 ]; then
        gate 1 vendored-headers "no file under /app declares OpenSSL's API"
    else
        gate 0 vendored-headers "$vend_h OpenSSL header(s) still in the tree" \
            "(baseline 76; see vendor.json for paths)"
    fi
    if [ "$vend_a" -eq 0 ]; then
        gate 1 vendored-archives "no archive under /app defines OpenSSL symbols"
    else
        gate 0 vendored-archives "$vend_a OpenSSL archive(s) still in the" \
            "tree (baseline 7)"
    fi
fi

# --- 3, 4: what the AST says about how the tree touches OpenSSL's types.
#
# Driven from the compile database the build just wrote, so it sees exactly the
# translation units that were compiled, with the flags they were compiled with.
# A scan that did not run, or that could not parse what it was given, fails both
# gates: unmeasured is not the same as clean.
# The same scan answers the source half of gates 5 and 6, so both are asked for
# here rather than run again: --call-in names the caller/callee pair gate 6 is
# about and is reported under call_reach, --calls names the function gate 5
# wants to see called at all and is reported under called.
if scan_ast --call-in _cpri__InitCryptoUnits:OPENSSL_init_crypto \
        --calls EVP_MD_CTX_new --calls EVP_MD_CTX_free \
        > "$LOG/ast-counts.json" 2>>"$LOG/verify.log"; then
    ast_parsed=$(jnum "$LOG/ast-counts.json" parsed)
    ast_errors=$(jnum "$LOG/ast-counts.json" errors)
    ast_member=$(jnum "$LOG/ast-counts.json" member_access)
    ast_value=$(jnum "$LOG/ast-counts.json" value_decl)
    ast_init=$(jnum "$LOG/ast-counts.json" call_reach)
    ast_mknew=$(jhas "$WORK/ast.json" called '^EVP_MD_CTX_new$')
    ast_mkfree=$(jhas "$WORK/ast.json" called '^EVP_MD_CTX_free$')
    vlog "ast: $ast_parsed TU(s), $ast_errors parse error(s)," \
         "member_access=$ast_member value_decl=$ast_value" \
         "init reached=$ast_init EVP_MD_CTX_new called=$ast_mknew" \
         "EVP_MD_CTX_free called=$ast_mkfree"
else
    ast_parsed=0; ast_errors=1; ast_member=-1; ast_value=-1; ast_init=-1
    ast_mknew=0; ast_mkfree=0
    vlog "ast: the scan did not run"
fi

# One verdict on whether the scan can be believed, read by gates 3, 4 and 6
# alike: a scan that did not run, or that could not parse what it was given,
# has not shown anything about any of them.
if [ "$ast_errors" -ne 0 ] || [ "$ast_parsed" -lt 100 ]; then
    ast_ok=0
else
    ast_ok=1
fi

if [ "$ast_ok" != 1 ]; then
    gate 0 no-member-access "the AST scan is not trustworthy on this tree" \
        "($ast_parsed TUs, $ast_errors errors)"
    gate 0 no-struct-by-value "the AST scan is not trustworthy on this tree"
else
    if [ "$ast_member" -eq 0 ]; then
        gate 1 no-member-access "0 accesses on OpenSSL-declared records"
    else
        gate 0 no-member-access "$ast_member accesses remain" \
            "(baseline 35; see ast.json for file:line)"
    fi
    if [ "$ast_value" -eq 0 ]; then
        gate 1 no-struct-by-value "0 OpenSSL structs held by value"
    else
        gate 0 no-struct-by-value "$ast_value remain (baseline 6)"
    fi
fi

# --- 5, 6: what the built engine asks the system OpenSSL for, and where from.
#
# Imports first, not source text.  EVP_MD_CTX_create and EVP_MD_CTX_init are
# macros in 1.1.0 (for _new and _reset), so the spelling in the source proves
# nothing about which lifecycle the object actually uses; the dynamic symbol it
# needs does.
#
# Imports alone are not enough either, in the other direction.  A symbol in the
# dynamic table says some translation unit mentioned it, and a tree can mention
# both halves in a function nothing calls while every digest context it really
# uses stays on the stack.  So the source half asks the AST whether the
# allocator is called at all -- through the macro or by its own name, both of
# which are the same CALL_EXPR once the preprocessor has run.
#
# Both halves, symmetrically.  The requirement is "obtains every EVP_MD_CTX
# from libcrypto's own allocator *and gives it back*", and an earlier version
# asked the AST about _new and the dynamic table about _free, which is not the
# same question asked twice: a tree whose only _free is the one the linker saw
# in a dummy nothing runs satisfied the import while every live context went
# back through something else.  Neither half proves "every" -- gates 3 and 4 and
# the sequence families are the rest of that sentence -- but each half should at
# least be a call in code the tree wrote.  EVP_MD_CTX_create and
# EVP_MD_CTX_destroy are 1.1.0 macros for _new and _free, so a tree spelling the
# old names reaches the same two CALL_EXPRs after the preprocessor has run.
new=$(jhas "$LOG/elf-engine.json" openssl_imports '^EVP_MD_CTX_new$')
free=$(jhas "$LOG/elf-engine.json" openssl_imports '^EVP_MD_CTX_free$')
if [ "$ast_ok" != 1 ]; then
    gate 0 evp-md-ctx-lifecycle "the AST scan is not trustworthy on this" \
        "tree, so how digest contexts are allocated has not been measured" \
        "($ast_parsed TUs, $ast_errors errors)"
elif [ "$new" = 1 ] && [ "$free" = 1 ] \
        && [ "$ast_mknew" = 1 ] && [ "$ast_mkfree" = 1 ]; then
    gate 1 evp-md-ctx-lifecycle "EVP_MD_CTX_new and EVP_MD_CTX_free imported," \
        "and the tree calls both"
else
    gate 0 evp-md-ctx-lifecycle "imported new=$new free=$free," \
        "called new=$ast_mknew free=$ast_mkfree -- digest contexts are not" \
        "allocated and released through the 1.1.0 lifecycle"
fi

# Gate 6 is a statement about reachability from the engine's entry point, not
# about one line of source: a tree that factors the call into a helper of its
# own and calls the helper has done what was asked, and call_reach counts that
# as reached (one hop, through a function the tree itself defines).  The import
# half is what stops a tree from defining its own OPENSSL_init_crypto and
# calling that.
init=$(jhas "$LOG/elf-engine.json" openssl_imports '^OPENSSL_init_crypto$')
if [ "$ast_ok" != 1 ]; then
    gate 0 explicit-init "the AST scan is not trustworthy on this tree, so" \
        "where the engine initialises libcrypto has not been measured" \
        "($ast_parsed TUs, $ast_errors errors)"
elif [ "$init" = 1 ] && [ "$ast_init" -ge 1 ]; then
    gate 1 explicit-init "_cpri__InitCryptoUnits reaches OPENSSL_init_crypto," \
        "and the engine imports it"
else
    gate 0 explicit-init "imported=$init, reached from" \
        "_cpri__InitCryptoUnits=$ast_init -- the engine is relying on" \
        "implicit initialisation"
fi

# ============================================================ behaviour ======
#
# Both corpora in full, one replay each.  The per-family breakdown `worst` reads
# is written by the same scoring pass, so nothing is replayed twice.
#
# Without a driver there is nothing to replay the engine cases with, and an
# absent answer file is scored as a file of no answers -- every expectation a
# miss -- which is the truth about a replay that did not happen.
if [ "$driver_ok" = 1 ]; then
    run_engine
else
    rm -f "$WORK/engine.txt"
    vlog "engine: no driver, so all 1676 cases score as unanswered"
fi
run_e2e

eng_all=$(score_engine - engine-report)
e2e_all=$(score_e2e    - e2e-report)

breadth=$(mean "$eng_all" "$e2e_all")
read -r worst worst_name <<<"$(worst_family "$LOG/engine-report.json" \
                                            "$LOG/e2e-report.json")"
behaviour=$(mean "$breadth" "$worst")
migrated=$(gates_mean)

# The 56 families go into the detail record under step 01: behaviour is step
# 01's demand, and every later step inherits it unchanged.  See helpers.sh.
detail_families 01 "$LOG/engine-report.json" "$LOG/e2e-report.json"

# =============================================================== the reward ==
# Product scoring (user decision 2026-10-13): reward = T1 x T2, with
# T1 = migrated (this step's gates) and T2 = behaviour.  Was
# 0.45 * behaviour + 0.55 * migrated.
reward=$(awk -v a="$behaviour" -v b="$migrated" \
             'BEGIN {printf "%.6f", b * a}')
vlog "product: T1 migrated $migrated x T2 behaviour $behaviour = $reward"


vlog "----------------------------------------------------------------"
vlog "engine  all=$eng_all"
vlog "e2e     all=$e2e_all"
vlog "worst family: $worst_name at $worst"
vlog "behaviour  breadth=$breadth  worst=$worst  -> $behaviour  (x0.45)"
vlog "migrated   ${#GATES[@]} gates                 -> $migrated  (x0.55)"
vlog "----------------------------------------------------------------"

report "$reward"
