#!/usr/bin/env bash
# PRODUCT SCORING (2026-10-13): the reward is now T1 x T2 -- T1 = this
# step's own gates (`migrated`), T2 = behaviour and the retained gates of
# earlier steps at their old relative weights -- see `Product scoring` at
# the reward.  Any weight or floor arithmetic stated below describes the
# superseded weighted sum.
# Step 2 verifier -- the private pool and the library's own ECDSA, against
# OpenSSL 1.1.1w.
#
# Authoring source.  build/mkstep.sh installs this as
# steps/02-rng-and-key-objects/tests/test.sh; edit it here and regenerate,
# never edit the copy under tasks/.
#
#
# WHAT THIS STEP IS
#
# Step 1 got the engine off its vendored 1.0.2b and onto the system library
# through the accessors 1.1.0 published.  It is still, in every other respect,
# an engine written in 2014: one random pool for everything, a fixed-width
# big-endian conversion open-coded five times, signature nonces left lying in
# the frame that drew them, a private-key exponentiation the library is never
# told is private, and its own ECDSA -- k^-1(z + rd) on the way out, u1G + u2Q
# on the way back -- built out of BIGNUM and EC_POINT primitives.
#
# 1.1.1 is where the first of those stops being style: it splits the DRBG in
# two, a public one and a private one, so that "this draw becomes a key" is
# something the code can say.  RAND_priv_bytes is the only one of this step's
# five that 1.1.1 brought.  BN_bn2binpad -- the whole of BnTo2B in one call,
# with the overflow case right -- arrived in 1.1.0; OPENSSL_cleanse,
# BN_FLG_CONSTTIME, ECDSA_do_sign/ECDSA_do_verify and the EC_KEY/ECDSA_SIG
# objects are older than that and were available to this engine the day it was
# written.  The instruction says so in the same terms: a step whose motivation
# is a version number has to get the version number right, and four fifths of
# this step is not a new release, it is work that was skipped.
#
# The step is: say which draws are secret and draw them from the private pool,
# handle secret BIGNUMs and secret bytes with the functions the library
# provides for secrets, and let the library do standard ECDSA.
#
#
# HOW IT IS SCORED
#
#     reward = 0.40 * behaviour + 0.52 * migrated + 0.08 * retained
#
#         behaviour = mean( breadth, worst )      -- exactly step 1's
#         migrated  = the fraction of THIS step's seven gates that pass
#         retained  = the fraction of step 1's six gates that still pass
#
# The three-way split is forced by build-process item 6's floor.  Step 2
# changes no recorded answer -- see "NOTHING MUST FLIP" below -- so step 1's
# finished tree comes through the corpora at 1.000 without touching anything.
# Under step 1's own 0.45/0.55 shape with one gate family of thirteen, that
# tree scores 0.45 + 0.55*(6/13) = 0.704 for having done none of step 2.  The
# predecessor's work must not be able to pay for the successor's, so the gates
# split: `migrated` is this step's, `retained` is step 1's re-asked, and
# `retained` is worth a token.  Every one of the seven is false on step 1's
# tree, so that tree scores
#
#     0.40*1.000 + 0.52*0.000 + 0.08*1.000 = 0.480
#
# which is the floor item 6 asks for, and is the right 0.48: everything it can
# still prove, nothing for the leap.  The nop -- an untouched baseline, because
# a nop agent did nothing at step 1 either -- fails P3 and reports 0.
#
# 0.08 is not a rounding error either.  A tree that does all of step 2 and puts
# one OpenSSL header back loses 0.013, which is visible next to a 1.000 and is
# not the step.  That is the intended weight of a regression: it costs, and it
# does not dominate.
#
# The arithmetic objection to that is "then a tree could undo ALL of step 1 for
# 0.08", and the answer is that it could not, because four of step 1's six are
# not undoable by a tree that still builds and still scores:
#
#   * no-member-access and no-struct-by-value cannot regress against the
#     installed 1.1.1 at all -- the types whose layout they are about are
#     opaque there, so the regressed code does not compile, and a tree that
#     does not compile fails P2 and reports 0;
#   * evp-md-ctx-lifecycle cannot regress either: EVP_MD_CTX_create is a macro
#     for EVP_MD_CTX_new from 1.1.0 onward and EVP_MD_CTX_init for
#     EVP_MD_CTX_reset, so the 1.0.2 spelling compiles to the 1.1.1 lifecycle;
#   * vendored-archives can be failed only by putting an archive back in the
#     tree, and linking one is P3, which reports 0.
#
# What is left is a header nobody includes, an archive nobody links and a
# missing OPENSSL_init_crypto: about 0.04, which is what the paragraph above
# prices.  And the weight cannot be raised to buy more, because item 6 wants
# step 1's finished tree at or below 0.5 and that tree earns 0.40 + w_retained
# outright -- w_retained > 0.10 breaks the floor.  0.08 is inside that, with
# room for the behaviour half to move.
#
#
# NOTHING MUST FLIP, AND THAT IS A MEASUREMENT
#
# Rule 5 lets a step re-record an expectation it deliberately changes.  This
# step changes none, and the reason is worth stating because two of its three
# requirements look like they should:
#
#   * Moving a draw to the private pool changes which bytes come out.  Nothing
#     records those bytes.  The engine corpus records the *shape* of a random
#     answer -- how much came out, and that separate draws differ -- because
#     the recording pass could not have done otherwise.
#   * Replacing the engine's ECDSA with the library's changes which signature
#     comes out, because the nonce changes.  No recorded case reads an ECDSA
#     signature's bytes: `eccsign`, the family that records `sigr` and `sigs`,
#     is 30 ECDAA and 30 ECSCHNORR cases and no ECDSA at all -- ECDSA is the
#     one scheme _cpri__SignEcc drops its kIn for, so its bytes were never a
#     function of its inputs and could not be recorded.  ECDSA appears in
#     `eccsignverify`, which records a return code and two range predicates
#     that every correct signature satisfies.
#
# Which leaves the verification side, where something does have to come out the
# same and it is not obvious.  ValidateSignatureEcdsa truncates the digest to
# the length of *r* -- a signature component -- where the signing side
# truncates to the length of the order.  The two agree except when r is short,
# which is 1 encoding in 256, and `eccverify/adv/011-014` pin all four corners
# of that deterministically.  ECDSA_do_verify computes e from exactly the bytes
# it is handed, so a port that passes the tree's own truncated length
# reproduces the tree's answers and a port that passes digest->size -- the
# mechanical translation, and the more correct one -- flips adv/012.  That case
# is the step's semantic trap (NOTES § 1.7), and it is scored by `worst`, which
# is what makes it cost something rather than 1/2723.
#
#
# WHAT SCORES ZERO OUTRIGHT
#
# The same five preconditions as step 1, inherited unchanged and filed in the
# detail record under step 01, because they are step 01's statements:
#
#   P0  /app arrived intact               environment failure, not the candidate
#   P1  the image binds OpenSSL 1.1.1w    environment failure, not the candidate
#   P2  the tree configures and builds    nothing to measure
#   P3  every built object gets OpenSSL from the loader, and from /usr/local
#   P4  an outside consumer still compiles against the public headers
#       (not a precondition: it costs the engine corpus, as at step 1)
#
# What is deliberately NOT a precondition is any step-1 gate.  A tree that does
# all of step 2 and leaves one OpenSSL header behind has not made step 2
# unmeasurable; it has regressed, which `retained` prices.
#
#
# THE SEVEN GATES
#
# Every one is false on step 1's finished tree, which is what rule 5 asks of
# the migrated-ness family at this step: the predecessor must fail them
# completely rather than collect credit for checks that are vacuously true.
#
#   1  the engine imports RAND_priv_bytes and calls it
#   2  the two pools are separated -- the two functions that draw a secret
#      number reach RAND_priv_bytes and no longer reach RAND_bytes, the two
#      that delegate still delegate, and the published draw and the three
#      paddings still come from the public pool
#   3  BN_bn2bin is neither imported nor called; BN_bn2binpad is both
#   4  the two functions that keep a signature nonce in their own storage
#      erase it with OPENSSL_cleanse before they return
#   5  the private-key exponentiation marks its exponent constant-time
#   6  SignEcdsa reaches ECDSA_do_sign, and the engine imports the ECDSA_SIG
#      accessors it needs to read the result
#   7  ValidateSignatureEcdsa reaches ECDSA_do_verify
#
# Gate 2 is one gate and not two on purpose.  Its public-pool half is true of
# step 1's tree in every pair, so as a gate of its own it would be a check the
# predecessor passes for having done nothing -- forbidden by rule 5, and
# enough on its own to lift step 1's tree to 0.545 and over item 6's floor.
# Conjoined with the private half it is not a free check, it is the half that
# says requirement 1 is a separation and not a rename.
#
# Gates 2, 4, 5, 6 and 7 are reachability, not source text, and reachability
# with one intermediate hop through a function the tree itself defines.  That
# is deliberate: a port is entitled to put its private draw behind a helper of
# its own -- the tree's own shape already is one, GetRandomPrivate -> the pool
# -- and a gate that demanded a direct call would fail a correct tree for
# having a name for something.  What one hop will not accept is two, which is
# why gate 2 asks the delegating callers about the delegation rather than
# about the pool; the calibrations in NOTES § 6.3 are the evidence that no
# plausible-correct arrangement needs two hops from where it is asked.
set -u
source /tests/helpers.sh

harden_reward_dir
stamp_step

# ------------------------------------------------- P0-P3: step 01's statements
#
# Filed under step 01: they are the properties step 01 introduced, and
# check_oracle_nop.py reads the inherited/introduced split out of the owner
# number.  Every later step re-asks them for the same reason.
CHECK_OWNER=01

assert_app
assert_ossl

if ! build_app; then
    vlog "P2 FAILED: the tree does not configure and build -- scoring 0"
    detail_add "$CHECK_OWNER" "precondition:builds" 0
    report 0
fi
detail_add "$CHECK_OWNER" "precondition:builds" 1

ENGINE=$WORK/build/OsslCryptoEngine/libCryptoEngine.so

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
    detail_add "$CHECK_OWNER" "precondition:openssl-from-loader" 0
    report 0
fi
detail_add "$CHECK_OWNER" "precondition:openssl-from-loader" 1

if build_driver; then
    driver_ok=1
    detail_add "$CHECK_OWNER" "public-api-compiles" 1
else
    driver_ok=0
    vlog "an outside consumer no longer compiles and links against the public"
    vlog "  headers (CryptoEngine.h + -lCryptoEngine -lplatform).  Every engine"
    vlog "  case scores as unanswered; the gates and the TCP corpus are"
    vlog "  unaffected."
    detail_add "$CHECK_OWNER" "public-api-compiles" 0
fi

# ================================================================== the scans

# One AST pass answers both gate families.  Every pair either family asks about
# is named here; astscan reports the ones that hold, and jreach/jcall read each
# pair out by name so that a gate is never decided by a count over a set it
# does not own.
#
# astscan's reachability stops at one hop through a function the tree defines,
# and stopping there is what decides the shape of the pairs below.  A port that
# gives the private draw a name of its own -- a `_cpri__GeneratePrivateRandom`
# beside `_cpri__GenerateRandom`, which is the obvious way to write it -- puts
# a helper between every draw site and RAND_priv_bytes.  Asking
# `_cpri__GetEphemeralEcc` to reach RAND_priv_bytes would then be asking it to
# reach two hops, and it would fail a tree that did the right thing.  So the
# pairs are split the way the call graph is: the three functions that actually
# draw a number are asked about the pool, and the two that delegate are asked
# about the delegation.  Each question is one hop from where it is asked.
#
# The two counted facts step 1's gates 3 and 4 need -- member_access and
# value_decl -- come out of the same pass, as does `called`.
# The pairs are a list rather than a run of flags so that the count in the
# log line below is the list's own length.  A hand-written total drifts the
# first time a pair is added, and a log line that miscounts is a log line a
# reader stops trusting.
# The three groups gate 2 works in, named once.  The request list below is
# built from them and gate 2 loops over them, so the pairs the scan is asked
# for and the pairs the gate reads are the same pairs by construction rather
# than by two lists agreeing.
#
#   DRAW_SECRET    draw a secret number themselves
#   DRAW_HELPER    the same, but are internal helpers a port may inline away
#   DRAW_DELEGATE  hand the draw to GetRandomPrivate
#   DRAW_PUBLIC    draw bytes that are not secret and must stay public
DRAW_SECRET=(SignSM2)
DRAW_HELPER=(GetRandomPrivate)
DRAW_DELEGATE=(_cpri__GetEphemeralEcc SchnorrEcc)
DRAW_PUBLIC=(_cpri__GenerateRandom OaepEncode RSAES_PKSC1v1_5Encode PssEncode)

CALL_IN=()
for fn in "${DRAW_SECRET[@]}" "${DRAW_HELPER[@]}" "${DRAW_DELEGATE[@]}" \
          "${DRAW_PUBLIC[@]}"; do
    CALL_IN+=("$fn:RAND_priv_bytes" "$fn:RAND_bytes")
done
for fn in "${DRAW_DELEGATE[@]}"; do
    CALL_IN+=("$fn:GetRandomPrivate")
done
CALL_IN+=(
    SchnorrEcc:OPENSSL_cleanse
    SignSM2:OPENSSL_cleanse
    RSADP:BN_set_flags
    RSADP:BN_mod_exp_mont_consttime
    RSADP:_math__ModExp
    _math__ModExp:BN_set_flags
    _math__ModExp:BN_mod_exp_mont_consttime
    SignEcdsa:ECDSA_do_sign
    SignEcdsa:BN_mod_inverse
    SignEcdsa:_cpri__EccPointMultiply
    ValidateSignatureEcdsa:ECDSA_do_verify
    ValidateSignatureEcdsa:BN_mod_inverse
    ValidateSignatureEcdsa:_cpri__EccPointMultiply
    _cpri__InitCryptoUnits:OPENSSL_init_crypto
)
CALLS=(
    EVP_MD_CTX_new
    EVP_MD_CTX_free
    RAND_priv_bytes
    BN_bn2bin
    BN_bn2binpad
)
scan_args=()
for p in "${CALL_IN[@]}"; do scan_args+=(--call-in "$p"); done
for c in "${CALLS[@]}";   do scan_args+=(--calls   "$c"); done

if scan_ast "${scan_args[@]}" \
        > "$LOG/ast-counts.json" 2>>"$LOG/verify.log"; then
    ast_parsed=$(jnum "$LOG/ast-counts.json" parsed)
    ast_errors=$(jnum "$LOG/ast-counts.json" errors)
    ast_member=$(jnum "$LOG/ast-counts.json" member_access)
    ast_value=$(jnum "$LOG/ast-counts.json" value_decl)
    vlog "ast: $ast_parsed TU(s), $ast_errors parse error(s)," \
         "member_access=$ast_member value_decl=$ast_value," \
         "$(jnum "$LOG/ast-counts.json" call_reach) of ${#CALL_IN[@]}"\
         "pairs reached"
else
    ast_parsed=0; ast_errors=1; ast_member=-1; ast_value=-1
    vlog "ast: the scan did not run"
fi

# One verdict on whether the scan can be believed, read by every gate that
# depends on it.  A scan that did not run, or that could not parse what it was
# given, has shown nothing about any of them: unmeasured is not clean.
if [ "$ast_errors" -ne 0 ] || [ "$ast_parsed" -lt 100 ]; then
    ast_ok=0
else
    ast_ok=1
fi

AST=$WORK/ast.json

# reach <caller> <callee> -> 1/0: CALLER's body calls CALLEE, or calls one
# function the tree defines that calls it.  0 whenever the scan is not
# trustworthy, so an unparsed tree never reads as a clean one.
reach() {
    [ "$ast_ok" = 1 ] || { printf 0; return; }
    jreach "$AST" "$1" "$2"
}

# direct <caller> <callee> -> 1/0: the call is written in CALLER's own body.
# reach() follows one intermediate hop; direct() follows none.  A gate that has
# to tell "this function does it" from "this function reaches something that
# does it" needs the second, and the two are not interchangeable.
direct() {
    [ "$ast_ok" = 1 ] || { printf 0; return; }
    jcall "$AST" "$1" "$2"
}

# Both helpers read call_reach, which holds one entry per requested pair and
# nothing else, so a pair nobody requested reads 0 from either -- which is also
# what "the caller does not reach the callee" reads.  A gate asking about an
# unrequested pair is not failing; it is answering a question nobody asked, and
# a gate that refuses a shape on a 1 would quietly stop refusing it.  The pairs
# the gates below name literally are read back out of this file and matched
# against the request list; the ones they name through a loop variable are the
# groups the list is built from, so those cannot drift.  A miss marks the scan
# untrustworthy, which every gate already knows how to read.
ask_missing=()
ask_n=0
while read -r a b; do
    ask_n=$(( ask_n + 1 ))
    case " ${CALL_IN[*]} " in
        *" $a:$b "*) ;;
        *) ask_missing+=("$a:$b") ;;
    esac
done < <(grep -oE '\$\((reach|direct) [A-Za-z0-9_]+ [A-Za-z0-9_]+\)' "$0" |
         sed -E 's/^.\((reach|direct) //; s/.$//' | sort -u)
vlog "ast: $ask_n literal gate pair(s) checked against the" \
     "${#CALL_IN[@]}-pair request list"
if [ ${#ask_missing[@]} -ne 0 ]; then
    vlog "BUG: gate pair(s) never requested from the scan: ${ask_missing[*]}"
    ast_ok=0
fi

# ========================================== step 01's gates, re-asked (0.08) ==
#
# The code is step 1's, unchanged, with `rgate` in place of `gate` and the
# owner still 01.  A property does not change hands because a later step also
# depends on it.
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
    rgate 0 vendored-headers "the vendor scan is not trustworthy" \
        "($vend_n files seen)"
    rgate 0 vendored-archives "the vendor scan is not trustworthy"
elif [ "$vend_u" -ne 0 ]; then
    rgate 0 vendored-headers "$vend_u archive(s) under /app could not be" \
        "opened, so what is inside them has not been read"
    rgate 0 vendored-archives "$vend_u archive(s) under /app could not be opened"
else
    if [ "$vend_h" -eq 0 ]; then
        rgate 1 vendored-headers "no file under /app declares OpenSSL's API"
    else
        rgate 0 vendored-headers "$vend_h OpenSSL header(s) back in the tree" \
            "(see vendor.json for paths)"
    fi
    if [ "$vend_a" -eq 0 ]; then
        rgate 1 vendored-archives "no archive under /app defines OpenSSL symbols"
    else
        rgate 0 vendored-archives "$vend_a OpenSSL archive(s) back in the tree"
    fi
fi

if [ "$ast_ok" != 1 ]; then
    rgate 0 no-member-access "the AST scan is not trustworthy on this tree" \
        "($ast_parsed TUs, $ast_errors errors)"
    rgate 0 no-struct-by-value "the AST scan is not trustworthy on this tree"
else
    if [ "$ast_member" -eq 0 ]; then
        rgate 1 no-member-access "0 accesses on OpenSSL-declared records"
    else
        rgate 0 no-member-access "$ast_member accesses have come back" \
            "(see ast.json for file:line)"
    fi
    if [ "$ast_value" -eq 0 ]; then
        rgate 1 no-struct-by-value "0 OpenSSL structs held by value"
    else
        rgate 0 no-struct-by-value "$ast_value have come back"
    fi
fi

mdnew=$(jhas "$LOG/elf-engine.json" openssl_imports '^EVP_MD_CTX_new$')
mdfree=$(jhas "$LOG/elf-engine.json" openssl_imports '^EVP_MD_CTX_free$')
ast_mknew=$(jhas "$AST" called '^EVP_MD_CTX_new$')
ast_mkfree=$(jhas "$AST" called '^EVP_MD_CTX_free$')
if [ "$ast_ok" != 1 ]; then
    rgate 0 evp-md-ctx-lifecycle "the AST scan is not trustworthy on this tree"
elif [ "$mdnew" = 1 ] && [ "$mdfree" = 1 ] \
        && [ "$ast_mknew" = 1 ] && [ "$ast_mkfree" = 1 ]; then
    rgate 1 evp-md-ctx-lifecycle "EVP_MD_CTX_new and EVP_MD_CTX_free imported," \
        "and the tree calls both"
else
    rgate 0 evp-md-ctx-lifecycle "imported new=$mdnew free=$mdfree," \
        "called new=$ast_mknew free=$ast_mkfree"
fi

init=$(jhas "$LOG/elf-engine.json" openssl_imports '^OPENSSL_init_crypto$')
init_reach=$(reach _cpri__InitCryptoUnits OPENSSL_init_crypto)
if [ "$ast_ok" != 1 ]; then
    rgate 0 explicit-init "the AST scan is not trustworthy on this tree"
elif [ "$init" = 1 ] && [ "$init_reach" = 1 ]; then
    rgate 1 explicit-init "_cpri__InitCryptoUnits reaches OPENSSL_init_crypto," \
        "and the engine imports it"
else
    rgate 0 explicit-init "imported=$init, reached=$init_reach -- the engine" \
        "is back to relying on implicit initialisation"
fi

CHECK_OWNER=02

# ============================================== step 02's own gates (0.52) ==

# --- 1: there is a private pool, and the engine uses it.
#
# The import is the fact that some translation unit needs the symbol; the call
# is the fact that the tree wrote one.  Neither alone: a tree can declare and
# never call, and a tree can call in dead code the linker keeps.  Which draws
# reach it is gate 2's question, not this one's -- this one is "the pool
# exists here at all", and it is the gate that fails first and most clearly on
# a tree that has done nothing.
rpb_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^RAND_priv_bytes$')
rpb_called=$(jhas "$AST" called '^RAND_priv_bytes$')
if [ "$ast_ok" != 1 ]; then
    gate 0 private-pool-present "the AST scan is not trustworthy on this tree" \
        "($ast_parsed TUs, $ast_errors errors)"
elif [ "$rpb_imp" = 1 ] && [ "$rpb_called" = 1 ]; then
    gate 1 private-pool-present "RAND_priv_bytes imported and called"
else
    gate 0 private-pool-present "imported=$rpb_imp called=$rpb_called --" \
        "the engine has no private DRBG draw in it"
fi

# --- 2: the two pools are separated -- secret draws come from the private
#        one, everything else still comes from the public one.
#
# Four functions, in two groups, because the tree's own shape is not uniform.
#
# Two of them draw the number themselves: GetRandomPrivate, whose entire
# contract is "a random scalar in [1, n-1]", and SignSM2, which open-codes its
# own nonce instead of calling GetRandomPrivate.  Each has to reach
# RAND_priv_bytes and stop reaching RAND_bytes.
#
# GetRandomPrivate is asked that only while it exists.  It is a file-static
# helper of this tree's own, not a published entry point, and a port that
# inlines it into its two callers and deletes it has changed nothing about who
# draws what -- the obligation moves to the callers, where the DRAW_DELEGATE
# arm below already collects it, because `via` goes to 0 for a helper that is
# not there and `own` is then the only way through.  Failing that port would be
# failing it for keeping a function this step never mentions.  SignSM2 gets no
# such licence: it is a scheme the engine publishes, and a tree without it is
# a tree that lost a scheme rather than a tree that tidied one.
#
# _cpri__GenerateKeyRSA also draws, and is deliberately not here.  Its one draw
# looks like key material and is not: the bytes become the plaintext of the
# trial encrypt-decrypt that closes key generation (CpriRSA.c:2186).  The primes
# are not drawn at all -- they come out of a KDF over the seed, which is what
# makes a seeded RSA key reproducible -- so there is no key material in this
# function's use of the generator.  A candidate who reads "key generation" and
# moves that draw anyway is not wrong about anything either, so the gate does
# not price it in either direction and the instruction does not name it.
#
# RSAKeySieve.c draws in four more places and none of them is here, for a
# blunter reason: the whole file sits inside `#ifdef RSA_KEY_SIEVE`, nothing in
# the tree or the build defines that macro, and CpriRSA.c's `#ifndef
# RSA_KEY_SIEVE` key generator is the one that compiles.  Not one line of the
# sieve survives preprocessing, so none of it reaches the AST and none of it is
# in the engine.
#
# Two of them delegate: _cpri__GetEphemeralEcc and SchnorrEcc both hand the
# question to GetRandomPrivate.  Asking those two to reach RAND_priv_bytes
# would be asking for two hops through any port that names its private draw --
# see the note on the scan above -- so what is asked of them is that the
# delegation is still there: either the call to GetRandomPrivate is still
# written in the body, or the body draws privately itself.  Either way they
# must not reach RAND_bytes, which is the half that fails on step 1's tree.
#
# Listing the callers rather than only the helper is what keeps the gate false
# for a port that fixes GetRandomPrivate and leaves SignSM2 drawing publicly:
# that port passes three quarters of this gate's evidence and fails the gate.
priv_ok=1
priv_detail=""
for fn in "${DRAW_SECRET[@]}"; do
    got=$(reach "$fn" RAND_priv_bytes)
    pub=$(reach "$fn" RAND_bytes)
    priv_detail="$priv_detail $fn:priv=$got,pub=$pub"
    { [ "$got" = 1 ] && [ "$pub" = 0 ]; } || priv_ok=0
done
for fn in "${DRAW_HELPER[@]}"; do
    if [ "$(jhas "$AST" defines "^$fn\$")" != 1 ]; then
        priv_detail="$priv_detail $fn:inlined-away"
        continue
    fi
    got=$(reach "$fn" RAND_priv_bytes)
    pub=$(reach "$fn" RAND_bytes)
    priv_detail="$priv_detail $fn:priv=$got,pub=$pub"
    { [ "$got" = 1 ] && [ "$pub" = 0 ]; } || priv_ok=0
done
for fn in "${DRAW_DELEGATE[@]}"; do
    via=$(direct "$fn" GetRandomPrivate)
    own=$(reach "$fn" RAND_priv_bytes)
    pub=$(reach "$fn" RAND_bytes)
    priv_detail="$priv_detail $fn:via=$via,priv=$own,pub=$pub"
    { [ "$pub" = 0 ] && { [ "$via" = 1 ] || [ "$own" = 1 ]; }; } || priv_ok=0
done

# The other side of the same requirement.  _cpri__GenerateRandom is the
# engine's published "give me random bytes" -- what the TPM layer calls for
# TPM2_GetRandom and for the commit nonce -- and beside it are the three
# paddings: OAEP's seed, PKCS#1 v1.5's non-zero filler, PSS's salt.  None of
# that is key material; it is public the moment the ciphertext or the
# signature leaves the TPM, and it must not start consuming the private DRBG.
#
# This half is true of step 1's tree, every pair of it, and that is why it is
# half of a gate rather than a gate.  On its own it would be a check the
# predecessor passes for having done nothing, which rule 5 forbids and which
# would also lift step 1's tree over item 6's floor.  Conjoined, it is what
# makes requirement 1 a separation rather than a rename: the cheapest wrong
# way to pass the first half is to point every draw at RAND_priv_bytes, and a
# gate family with no answer to that is a family that rewards it.
pub_detail=""
for fn in "${DRAW_PUBLIC[@]}"; do
    keep=$(reach "$fn" RAND_bytes)
    leak=$(reach "$fn" RAND_priv_bytes)
    pub_detail="$pub_detail $fn:pub=$keep,priv=$leak"
    { [ "$keep" = 1 ] && [ "$leak" = 0 ]; } || priv_ok=0
done

if [ "$ast_ok" != 1 ]; then
    gate 0 pools-separated "the AST scan is not trustworthy on this tree"
elif [ "$priv_ok" = 1 ]; then
    gate 1 pools-separated "secret draws private, public draws public --" \
        "$priv_detail --$pub_detail"
else
    gate 0 pools-separated "the two pools are not separated --" \
        "$priv_detail --$pub_detail"
fi

# --- 3: fixed-width conversion is the library's.
#
# Five open-coded copies of BN_bn2binpad in the baseline -- BnTo2B, the two
# private-exponent writers, and two in MathFunctions.c -- each of them a
# zero-fill followed by BN_bn2bin into the middle of the buffer.  The gate is
# the absence of the old call and the presence of the new one, in both the
# dynamic table and the AST, because either alone can be satisfied without the
# other: an import survives dead code, and a call the linker dropped is not a
# call the object makes.
b2b_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^BN_bn2bin$')
b2b_call=$(jhas "$AST" called '^BN_bn2bin$')
pad_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^BN_bn2binpad$')
pad_call=$(jhas "$AST" called '^BN_bn2binpad$')
if [ "$ast_ok" != 1 ]; then
    gate 0 padded-conversion "the AST scan is not trustworthy on this tree"
elif [ "$b2b_imp" = 0 ] && [ "$b2b_call" = 0 ] \
        && [ "$pad_imp" = 1 ] && [ "$pad_call" = 1 ]; then
    gate 1 padded-conversion "BN_bn2binpad has replaced BN_bn2bin everywhere"
else
    gate 0 padded-conversion "BN_bn2bin imported=$b2b_imp called=$b2b_call," \
        "BN_bn2binpad imported=$pad_imp called=$pad_call"
fi

# --- 4: the signature nonces are erased before their storage dies.
#
# SchnorrEcc and SignSM2, and not the two private-exponent writers.  The three
# lines in RsaPrivateExponent and _cpri__TestKeyRSA that look like an erase --
# BN_bn2bin into the middle of a buffer, then memset of the bytes in front of
# it -- do not erase a secret at all.  They zero the leading pad of a buffer
# that is the function's live output; it is an open-coded BN_bn2binpad, which
# is gate 3's business and not this gate's.
#
# What does die is the nonce.  Each of these two functions keeps its own
# TPM2B_ECC_PARAMETER k in engine storage, fills it with the per-signature
# secret scalar, uses it, and returns; that storage is whatever runs next, and
# nothing in the tree clears it in between.  A leaked ECDSA-family nonce
# recovers the private key from the signature it produced, so of every buffer
# in the engine this is the one with the shortest distance between "left
# behind" and "key disclosed".
#
# Both, not one: it is the same omission twice, in two functions that do not
# otherwise resemble each other -- SchnorrEcc hands k to EcDaa and returns what
# EcDaa returns, SignSM2 converts its two results and returns success -- and a
# port that fixes the one it found first leaves the other.
#
# OPENSSL_cleanse rather than memset because the compiler is entitled to delete
# a memset whose buffer is dead afterwards, and in both of these it is.  The
# gate cannot see the deletion, so it reads the call the standard says survives.
cl1=$(reach SchnorrEcc OPENSSL_cleanse)
cl2=$(reach SignSM2 OPENSSL_cleanse)
cl_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^OPENSSL_cleanse$')
if [ "$ast_ok" != 1 ]; then
    gate 0 secret-bytes-cleansed "the AST scan is not trustworthy on this tree"
elif [ "$cl_imp" = 1 ] && [ "$cl1" = 1 ] && [ "$cl2" = 1 ]; then
    gate 1 secret-bytes-cleansed "both signature nonces are erased before return"
else
    gate 0 secret-bytes-cleansed "imported=$cl_imp," \
        "SchnorrEcc=$cl1 SignSM2=$cl2"
fi

# --- 5: the private-key exponentiation is constant-time in the exponent.
#
# RSADP, not _math__ModExp.  _math__ModExp is a published entry point that the
# `modexp` family scores and that RSAEP -- a public-exponent operation on
# public data -- also goes through; it takes its exponent as bytes and cannot
# know which it has.  So the requirement is about the decryption path.
#
# RSADP as it stands does no BIGNUM work of its own: it hands three byte strings
# to _math__ModExp and that is the whole of it.  So there are two shapes a
# correct port can take, and the gate has to accept both.  It can give the
# private operation an exponentiation of its own -- a _math__ModExpPrivate
# beside _math__ModExp -- and then RSADP reaches BN_set_flags, which is how a
# BIGNUM is told it is secret and is what makes BN_mod_exp select the
# montgomery constant-time ladder, in one hop.  Or it can factor the two into a
# shared static core that is told which kind of exponent it has, and then RSADP
# reaches the flag in two hops, which is one more than the scan follows.  The
# second shape is not the worse one; it is what a tree that dislikes duplicated
# arithmetic does.
#
# So the gate asks for the flag to be imported and called somewhere in the
# engine, and then for either arm: RSADP reaches it within one hop, or RSADP no
# longer goes through _math__ModExp at all -- which is the fingerprint of the
# shared core, because under that shape the arithmetic is reached by a
# different name.  Step 1's tree passes neither arm.  It has no BN_set_flags
# anywhere in it, and its RSADP calls _math__ModExp directly.
#
# Two ways of asking, not one.  BN_set_flags(BN_FLG_CONSTTIME) is the
# documented one -- BN_mod_exp reads the flag and dispatches to the montgomery
# ladder -- and it is what the man page for BN_mod_exp points a reader at.  But
# BN_mod_exp_mont_consttime is public at every release in this migration, takes
# a NULL montgomery context, and is a one-line substitution for BN_mod_exp on
# an odd modulus, which an RSA modulus is.  A port that calls it reaches the
# same arithmetic by the same path and has satisfied the requirement as the
# instruction states it -- "so that the exponentiation takes the path whose
# running time does not depend on it" -- without ever setting a flag.  Refusing
# that would be failing a correct tree for choosing the other of two documented
# spellings, so the gate takes either.  02-consttime-ladder is the calibration
# that says the second one scores 1.000.
#
# Both arms then ask the same second thing: that RSADP no longer goes through
# _math__ModExp.  That is not a stylistic preference, it is the only way this
# scan can tell a marked exponentiation from a marked BIGNUM sitting next to an
# unmarked one.  _math__ModExp takes its exponent as bytes
# (MathFunctions.c:534-542, `const UINT32 eSize, const BYTE *e`) and builds its
# own BIGNUM out of them, so a flag RSADP sets on a BIGNUM of its own cannot
# cross into it: any tree where RSADP both marks something and still calls
# _math__ModExp is a tree whose private exponentiation is the variable-time one,
# whatever it marked.  Without this conjunct the gate takes
# `BN_set_flags(BN_new(), BN_FLG_CONSTTIME);` written above an untouched call --
# one line, no arithmetic moved -- because "RSADP reaches BN_set_flags" is all
# it was asking.  02-consttime-dummy is that tree, and it is calibrated at
# 0.926.  The cost of the conjunct is that it will not credit a port that keeps
# the published entry point on the decryption path and somehow marks through
# it, which is a tree that cannot exist while the entry point takes bytes.
#
# And one shape is refused before either arm is considered: the flag set inside
# _math__ModExp itself.  That is the cheapest thing to write -- one line in the
# function both paths already went through -- and it is the one the requirement
# names as wrong, because the published entry point is handed an exponent as
# bytes by a caller it does not know, and a port that marks it there has decided
# for RSAEP as well.  It is refused by the shape of the call rather than by
# where the flag ends up: the check is the direct call, so the shared static core
# that is TOLD whether its exponent is secret -- which _math__ModExp reaches, and
# reaches with FALSE -- is not caught by it.  That distinction is an argument
# value, and an argument value is not something this scan reads; a core that
# marks unconditionally passes this gate.  See NOTES-authoring.md, the step 2
# calibrations, for the measurement of that limit.
ct_old=$(reach RSADP _math__ModExp)

# route 1: the flag.          route 2: the ladder, called by name.
ct=$(reach RSADP BN_set_flags)
ct_l=$(reach RSADP BN_mod_exp_mont_consttime)
ct_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^BN_set_flags$')
ct_l_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^BN_mod_exp_mont_consttime$')
ct_call=$(jhas "$AST" called '^BN_set_flags$')
ct_l_call=$(jhas "$AST" called '^BN_mod_exp_mont_consttime$')

# ...and, for either route, the same thing refused: done inside the published
# entry point, where it decides for RSAEP as well.
ct_pub=$(direct _math__ModExp BN_set_flags)
ct_pub_l=$(direct _math__ModExp BN_mod_exp_mont_consttime)

# Which arm let it through is worth printing, and so is how far down the
# constant-time call sits: "in one hop" is the flag (or the ladder) written in
# RSADP itself, and "further down" is the shared-core shape, where RSADP calls
# a core of its own that marks.  The oracle is the second one, so a message
# that named only the first would be describing a tree nobody has.
ONE_HOP="reached in one hop"
FAR="reached more than one hop down, in a core of the port's own"

route=0
how=
if [ "$ct_old" = 0 ]; then
    if [ "$ct_imp" = 1 ] && [ "$ct_call" = 1 ]; then
        route=flag
        if [ "$ct" = 1 ]; then how=$ONE_HOP; else how=$FAR; fi
    elif [ "$ct_l_imp" = 1 ] && [ "$ct_l_call" = 1 ]; then
        route=ladder
        if [ "$ct_l" = 1 ]; then how=$ONE_HOP; else how=$FAR; fi
    fi
fi

if [ "$ast_ok" != 1 ]; then
    gate 0 consttime-private-exp "the AST scan is not trustworthy on this tree"
elif [ "$ct_pub" = 1 ] || [ "$ct_pub_l" = 1 ]; then
    gate 0 consttime-private-exp "_math__ModExp takes the constant-time path" \
        "itself (flag=$ct_pub ladder=$ct_pub_l) -- the published entry point" \
        "takes an exponent as bytes from a caller and cannot know which kind" \
        "it has, so deciding there decides for RSAEP's public exponent too and" \
        "changes the entry point the requirement leaves alone"
elif [ "$route" != 0 ]; then
    gate 1 consttime-private-exp "the decryption path takes the constant-time" \
        "exponentiation by the $route, $how, and has left the published" \
        "byte-exponent entry point -- reached: flag=$ct ladder=$ct_l," \
        "through-_math__ModExp=$ct_old"
else
    gate 0 consttime-private-exp "BN_set_flags imported=$ct_imp" \
        "called=$ct_call reached=$ct; BN_mod_exp_mont_consttime" \
        "imported=$ct_l_imp called=$ct_l_call reached=$ct_l;" \
        "RSADP->_math__ModExp=$ct_old -- the private-key exponentiation is the" \
        "same variable-time one the public operation uses (a decryption path" \
        "still going through _math__ModExp is that path however it was" \
        "marked, because that entry point takes its exponent as bytes)"
fi

# --- 6, 7: standard ECDSA is the library's.
#
# Signing and verification are two gates, not one, because they are two
# independent pieces of work and because the trap lives in exactly one of them:
# a tree can move signing and leave verification hand-rolled, and it would be
# right about every recorded answer while having done half the requirement.
#
# The import half of gate 6 asks for the accessors rather than for
# ECDSA_do_sign alone.  An ECDSA_SIG is opaque at every release in this
# migration, so a tree that produced one and could not read r and s out of it
# has not produced a signature; ECDSA_SIG_get0 is how it reads them, and the
# engine must import it.
#
# Both gates also ask that the arithmetic left.  "Calls into libcrypto's own
# ECDSA" is satisfied on paper by one call written above the existing body, and
# a tree that does that is a tree that still signs and verifies with its own
# point arithmetic; the instruction says those two become library calls, and
# what makes that checkable is the departure rather than the arrival.  The two
# names are the ones the requirement itself spells out: BN_mod_inverse is the
# k^-1 of "k^-1(z + rd)" and the w of "u1 = zw, u2 = rw", and
# _cpri__EccPointMultiply is the kG and the u1G + u2Q.  Neither survives in a
# body that hands the work to ECDSA_do_sign or ECDSA_do_verify -- the golden
# calls neither, at either site -- and neither has any other use inside these
# two functions, so requiring 0 refuses the stub without putting any shape on
# the port beyond the one asked for.  Both remain in the tree, used by the
# schemes that keep their own arithmetic; this is about these two bodies.
sign_reach=$(reach SignEcdsa ECDSA_do_sign)
sign_inv=$(reach SignEcdsa BN_mod_inverse)
sign_mul=$(reach SignEcdsa _cpri__EccPointMultiply)
sign_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^ECDSA_do_sign$')
sig_get=$(jhas "$LOG/elf-engine.json" openssl_imports '^ECDSA_SIG_get0')
if [ "$ast_ok" != 1 ]; then
    gate 0 ecdsa-sign-library "the AST scan is not trustworthy on this tree"
elif [ "$sign_imp" = 1 ] && [ "$sign_reach" = 1 ] && [ "$sig_get" = 1 ] \
        && [ "$sign_inv" = 0 ] && [ "$sign_mul" = 0 ]; then
    gate 1 ecdsa-sign-library "SignEcdsa reaches ECDSA_do_sign, reads the" \
        "result through the ECDSA_SIG accessors, and no longer does the" \
        "inversion or the point multiplication itself"
else
    gate 0 ecdsa-sign-library "ECDSA_do_sign imported=$sign_imp" \
        "reached=$sign_reach, ECDSA_SIG_get0 imported=$sig_get," \
        "still-reaches BN_mod_inverse=$sign_inv" \
        "_cpri__EccPointMultiply=$sign_mul"
fi

ver_reach=$(reach ValidateSignatureEcdsa ECDSA_do_verify)
ver_inv=$(reach ValidateSignatureEcdsa BN_mod_inverse)
ver_mul=$(reach ValidateSignatureEcdsa _cpri__EccPointMultiply)
ver_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^ECDSA_do_verify$')
if [ "$ast_ok" != 1 ]; then
    gate 0 ecdsa-verify-library "the AST scan is not trustworthy on this tree"
elif [ "$ver_imp" = 1 ] && [ "$ver_reach" = 1 ] \
        && [ "$ver_inv" = 0 ] && [ "$ver_mul" = 0 ]; then
    gate 1 ecdsa-verify-library "ValidateSignatureEcdsa reaches" \
        "ECDSA_do_verify and no longer does the inversion or the point" \
        "multiplication itself"
else
    gate 0 ecdsa-verify-library "ECDSA_do_verify imported=$ver_imp" \
        "reached=$ver_reach, still-reaches BN_mod_inverse=$ver_inv" \
        "_cpri__EccPointMultiply=$ver_mul -- verification is still hand-rolled" \
        "point arithmetic"
fi

# ============================================================ behaviour ======
#
# Both corpora in full, one replay each, exactly as at step 1 and against
# exactly the same recordings.  Nothing here is re-recorded: see "NOTHING MUST
# FLIP" above for why this step is entitled to say that.
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
retained=$(rgates_mean)

# Behaviour belongs to step 01 forever: the corpus is fixed and every later
# step must still reproduce it.
detail_families 01 "$LOG/engine-report.json" "$LOG/e2e-report.json"

# =============================================================== the reward ==
# Product scoring (user decision 2026-10-13): reward = T1 x T2, with
# T1 = migrated (this step's own gates) and T2 = preservation, the old
# weights of behaviour and of the retained gates of earlier steps,
# renormalised: (0.40 * behaviour + 0.08 * retained) / 0.48.  Was
# 0.40 * behaviour + 0.52 * migrated + 0.08 * retained.
reward=$(awk -v a="$behaviour" -v b="$migrated" -v c="$retained" \
             'BEGIN {printf "%.6f", b * ((0.40 * a + 0.08 * c) / 0.48)}')
vlog "product: T1 migrated $migrated x T2 (0.40 behaviour + 0.08 retained)/0.48 = $reward"


vlog "----------------------------------------------------------------"
vlog "engine  all=$eng_all"
vlog "e2e     all=$e2e_all"
vlog "worst family: $worst_name at $worst"
vlog "behaviour  breadth=$breadth  worst=$worst  -> $behaviour  (x0.40)"
vlog "migrated   ${#GATES[@]} gates                 -> $migrated  (x0.52)"
vlog "retained   ${#RGATES[@]} gates from step 01   -> $retained  (x0.08)"
vlog "----------------------------------------------------------------"

report "$reward"
