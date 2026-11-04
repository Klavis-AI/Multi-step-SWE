#!/usr/bin/env bash
# PRODUCT SCORING (2026-10-13): the reward is now T1 x T2 -- T1 = this
# step's own gates (`migrated`), T2 = behaviour and the retained gates of
# earlier steps at their old relative weights -- see `Product scoring` at
# the reward.  Any weight or floor arithmetic stated below describes the
# superseded weighted sum.
# =============================================================================
# Step 06 -- 06-fips-and-libctx.  OpenSSL 3.1.8.
#
# WHAT THIS STEP IS
#
# Steps 03 to 05 moved the engine onto the provider model: a library context of
# its own, a provider loaded into it by name, and every digest, cipher and key
# obtained from that context.  Every one of those fetches passes NULL as the
# property query, and until now that has been the right answer, because there
# has been exactly one provider in the context and nothing for a query to
# choose between.
#
# This step puts a second one there, and NULL stops being a default.
#
# The release this step installs ships a FIPS module.  Loading it is not a
# matter of linking against it: the module is handed its own integrity MAC as
# configuration data, so a library context that has never loaded a
# configuration cannot load the provider at all -- OSSL_PROVIDER_load returns
# NULL with "SELF_TEST_post: missing config data" no matter how correct the
# file on disk is.  So the engine has to do three things it has never done:
# load a configuration into ITS OWN context, load the FIPS provider from that
# context, and ask the provider to self test before trusting it.
#
# And then it has to go on producing the answers it has always produced, which
# is where the property query stops being a formality.  With two providers in
# the context, what an unqualified fetch resolves to is decided by the order
# they were loaded in: the first explicit OSSL_PROVIDER_load wins, and where
# there was no explicit load at all the configuration's own activation wins.
# NOTES-authoring.md section 10.7 has five configurations measured on this
# release.  Two of them -- module loaded first, or module activated by the
# configuration alone -- put the TPM's own algorithms onto a module that does
# not do them:
#
#   RSA-1024   both corpus key sizes are 1024-bit.  FIPS refuses them at
#              EVP_PKEY_encrypt_init -- "invalid key length" -- at e=65537 and
#              at e=3 alike.  Verification is NOT refused; the private side of
#              this engine is still RSADP, where step 02 pinned it.
#   EC         the engine builds all four of its curves from fully explicit
#              parameters, because BN_P256 has no OpenSSL NID and EccCurveInit
#              treats every curve the same way.  The FIPS provider refuses
#              OSSL_PKEY_EC_ENCODING_EXPLICIT as a class, at
#              EVP_PKEY_fromdata, before it has looked at which curve it is --
#              so NIST-P256 is refused exactly as hard as BN_P256.
#
# Those two are the whole of it.  There is NO third family.  This tree
# compiles SM3, SM4 and Camellia out: Implementation.h:84-85 is
# `#define ALG_SM3_256 ALG_NO` and `#define ALG_SM4 ALG_NO`,
# Implementation.h:900-901 defaults ALG_CAMELLIA to NO, CpriSym.c:83-87 is an
# AES-only table, and CpriHash.c:118-120's SM3 row is inside
# `#ifdef TPM_ALG_SM3_256` and aliases to "SHA256" in any case.  The corpus has
# no SM3, SM4 or Camellia case and could not have one.  Earlier drafts of this
# header, of the instruction and of the probe said those three were what the
# module would cost this TPM; they were wrong about the tree.  A step whose
# stated stakes do not exist is a step whose reader stops believing the stakes
# that do.
#
# In the other three orders an unqualified fetch resolves to "default" and
# nothing breaks.  That is the point of the step rather than an argument
# against it.  A port whose behaviour depends on which of its two
# OSSL_PROVIDER_load calls it happened to write first has not recorded that
# decision anywhere, and the next person to swap those two lines -- or to drop
# the explicit load and let the configuration activate the module on its own --
# changes what every fetch in the engine resolves to without touching a fetch.
# A fetch that names the implementation it wants is identical in all of them:
# section 10.7 measures the "provider=default" row as the same row in A, B, C
# and E.
#
# WHAT IT IS NOT
#
# It is not "make the engine FIPS-validated".  A TPM that could only do what
# the FIPS module does would not be this TPM: it could not encrypt with either
# of its 1024-bit RSA keys and could not build a single one of its four curves.
# What the step asks for is that the module be present, loadable and
# self-tested in the engine's context -- which is what an operator who has to
# attest to the module's integrity needs -- and that the engine name what it
# fetches so that its own behaviour does not change underneath it when the
# module is there.  The instruction says so in those words.
#
# It is also not a step about a file path.  Which configuration file the engine
# loads is reported and is not gated; what is gated is that a configuration was
# loaded into the context the engine made, that it returned success, and that a
# provider named "fips" came out of that context afterwards.
#
# THE TEXT OF THE QUERY IS NOT GATED.  WHAT THE QUERY DOES IS.
#
# No gate below asks what the query SAID, and none ever will:
#
#   Which implementation a deployment wants is the deployment's decision, and
#   a verifier that pinned a string would have made itself the authority on
#   it.  What the migration owes is that the decision is written down at every
#   fetch instead of being inherited from whichever provider loaded first.
#
#   And "the query that keeps this engine's behaviour" is a sentence about
#   this release's property table, which moves between releases.  Gating the
#   text would freeze a 3.1.8 fact into a step that steps 07 to 10 inherit.
#
# A WRONG answer is priced by the corpus and needs no gate.  A blanket
# "fips=yes" loses every explicit-parameter EC key and every 1024-bit RSA
# encryption; "?fips=yes" loses the same, in every load order rather than
# only some.  Both land on the families, in proportion, with no gate involved.
#
# NO answer -- and, which is the same thing at the fetch, a query that decides
# nothing -- is the case the corpus does not reliably price, and that is why
# gate 3 exists.  In the load order the golden writes -- configuration, then
# "default", then "fips" -- an unconstrained fetch resolves to "default" and
# every family still passes, so on that tree the corpus says nothing whatever
# about the omission.  Gate 3 is the only instrument that does.
#
# Gate 3 therefore asks two different questions, and it needs both:
#
#   PRESENCE.  Every fetch and every key-operation context the engine built
#   carried a non-NULL, non-empty query.  This is the only instrument for the
#   key-operation contexts: EVP_PKEY_CTX_new_from_name and _from_pkey hand
#   back a context rather than an implementation, and EVP_PKEY_CTX_new hands
#   back one having been given nowhere to say what it wanted, so there is
#   nothing to compare a fetch against.  Presence is what is measurable there.
#
#   EFFECT.  Every digest and cipher fetch resolved to the implementation the
#   query named rather than to the one that happened to rank first.  The probe
#   answers that by building a SECOND library context of its own, loading the
#   same configuration into it and then the same two providers in the REVERSED
#   order, and repeating each of the tree's fetches there with the tree's own
#   query string.  A query that names an implementation lands on the same
#   provider in both contexts -- a pin.  A query that names nothing follows
#   the load order and lands on a different one -- a drift.  The tree cannot
#   see that context, cannot reach it and cannot be written against it: the
#   only input it has to it is the query it was going to pass anyway.
#
# Presence without effect is ceremonial, and measurably so: a tree that writes
# "?x=1" at all eight call sites satisfies presence at every one of them, and
# on the golden's load order computes every recorded answer correctly, because
# OpenSSL drops an optional clause that matches nothing and falls straight back
# to load order.  calib/06-propq-dummy.sh is that tree.  Effect without
# presence would leave the key-operation contexts unmeasured.  Both, and the
# step is gated on what it actually asked for.
#
# WHAT SCORES ZERO OUTRIGHT
#
# P0-P3 are step 01's preconditions, re-asserted here because everything below
# is measured on the objects they produce: the tree is at /app, the image's
# OpenSSL is the one the loader hands the engine, the tree builds, and the
# engine imports its OpenSSL rather than defining it.  A tree that fails any of
# them scores 0 and the run stops there.
#
# HOW IT IS SCORED
#
#   reward = 0.36 * behaviour + 0.54 * migrated + 0.10 * retained
#
#   behaviour  mean(breadth, worst_family) over both corpora in full, replayed
#              against exactly the recordings steps 01-05 were scored on.
#   migrated   this step's four gates, split 0.4 paperwork / 0.6 substance --
#              see "the reward" at the bottom for why they are not flat.
#   retained   the twenty-six gates steps 01-05 introduced and this step still
#              asks: the twenty-two step 05 carried, plus step 05's own four.
#
# The weights are step 04's and step 05's, unchanged, and the floor they put
# under the untouched step 05 tree is the same one:
#
#   0.36 * 1 + 0.54 * 0 + 0.10 * 1 = 0.46
#
# against build-process item 6's requirement that it be at most 0.5.  The step
# 05 tree keeps its 0.36 honestly: it fetches with NULL into a context with
# ONE provider in it, where NULL has nothing to choose between and is the
# right answer.  What it loses is the whole of `migrated` -- it has not
# configured a context, has not loaded a module, has not self-tested one and
# has not named a single query.  NOTES-authoring.md section 10.1 records that
# the release bump alone costs it nothing: 1.000000 on the step 05 verifier at
# 3.1.8.
#
# WHAT DOES NOT FLIP
#
# Nothing is re-recorded at this step.  Both corpora, both expected files and
# the driver are byte-for-byte what steps 01-05 were scored against;
# `stamp_step` and the shared-corpus hash in the image are the check.
#
# THE FOUR GATES
#
#   fips-configured          The engine loads a configuration into the library
#                            context it made -- not the process default, not
#                            somebody else's -- gets 1 back from it, and then
#                            gets a provider named "fips" out of that same
#                            context, beside the one it already had.
#
#   providers-released       Every provider the engine loaded is given back at
#                            shutdown, the FIPS one included, and none of them
#                            is unloaded before the engine stops.
#
#   property-query-everywhere
#                            Two halves, and the gate is their conjunction.
#                            PRESENCE: no fetch and no key-operation context is
#                            created with a NULL or empty property query -- not
#                            EVP_MD_fetch, not EVP_CIPHER_fetch, not
#                            EVP_PKEY_CTX_new_from_name or _from_pkey, not
#                            EVP_Q_digest, and not EVP_PKEY_CTX_new or _new_id,
#                            which cannot carry one at all.  EFFECT: every one
#                            of those calls that resolves an implementation GOT
#                            the one its query named -- the probe repeats each
#                            of them in a library context of its own holding
#                            the same two providers in the opposite order, and
#                            a query that decides nothing lands somewhere else
#                            there.  Both halves are counted against the number
#                            of such calls the probe watched, so the
#                            requirement scales with the workload instead of
#                            with a constant.
#
#   fips-self-tested-and-live
#                            OSSL_PROVIDER_self_test is run at startup on the
#                            provider the engine loaded, its result is
#                            checked, and the module stays loaded for the life
#                            of the engine.  Measured twice: once normally,
#                            and once with the probe forcing the self test to
#                            fail, where a tree that checks the result must
#                            refuse to start.
#
# THE SECOND PROBE RUN, AND WHY IT EXISTS
#
# "Checks the result" cannot be measured on a tree where the result is always
# 1.  A tree that writes
#
#     OSSL_PROVIDER_self_test(s_fips);
#
# and a tree that writes
#
#     if(OSSL_PROVIDER_self_test(s_fips) != 1) FAIL(FATAL_ERROR_INTERNAL);
#
# produce identical counters, identical call graphs and identical behaviour on
# a machine whose module is intact, and the difference between them is the
# whole content of the requirement.
#
# So the probe can be told to lie.  With PROBE_FAIL_SELFTEST=1 in its
# environment its OSSL_PROVIDER_self_test interposer returns 0 without calling
# through -- the module is fine, the answer is not -- and the verifier runs the
# probe a second time under that variable.  A tree that checks takes the TPM
# failure path, which this probe reports as ok=0 with tpmfail set; a tree that
# does not check finishes the run exactly as it did the first time.  The gate
# reads the pair.
#
# The variable is read and DELETED from the environment in the probe's
# .preinit_array, which the loader runs before the init_array of any shared
# object the probe links -- libCryptoEngine.so among them.  So the tree cannot
# read it and cannot tell the two runs apart, and a tree that answered them
# differently would be answering a question it cannot have heard.  A
# constructor was tried first and is not early enough: shared objects'
# constructors run before an executable's, and calib/06-fix8-getenv.sh is the
# tree that measured that.
#
# This is the clean control and the synthetic failure fixture built into one
# instrument, which is where the instrument is least likely to be forgotten.
# It costs one extra probe run, and on a correct tree that run stops at
# startup and costs almost nothing.
#
# WHAT THE PROBE DRIVES
#
# Unchanged from step 05: the hash workload steps 03 and 04 calibrated their
# rates against, the symmetric workload step 04 added, and step 05's thirteen
# asymmetric operations per round.  This step adds counters and two
# interposers and no new round -- its subject is the arguments of the calls
# those rounds already make, so a new workload would move every inherited
# threshold and measure nothing new.
# =============================================================================

set -u
source /tests/helpers.sh

harden_reward_dir
stamp_step


# ------------------------------------------------- P0-P3: step 01's statements
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

# ============================================================== the probe ===
#
# This step's stepprobe.c is step 03's with a symmetric half grafted on, and
# the graft is additive: every counter step 03's gates read is still produced
# by the same interposer against the same workload, and the symmetric calls are
# counted in g_sym_ops rather than folded into g_ops, because step 03 divides
# its fetch counts by that number and folding two workloads into one counter
# would silently move every one of those ratios.  NOTES-authoring.md section
# 8.1 has the before-and-after on both trees.
#
#
# Step 03 is the first step with a requirement that is about an ARGUMENT and a
# requirement that is about a RATE, and a scan can answer neither:
#
#   "its digests fetched from it" is a claim about EVP_MD_fetch's first
#   argument.  A tree that creates a context, loads a provider into it and then
#   fetches with NULL has done all three named things and gets its algorithms
#   from whatever loaded itself into the process default.
#
#   "fetches each digest once and holds it" is a claim about how often.  The
#   AST half below asks it backwards -- EVP_MD_free must not be reached from
#   the hash entry points -- because asking it forwards would fail the lazy
#   cache, which is a correct shape; backwards it is passed by a tree that
#   fetches per operation, leaks every reference, and puts one
#   EVP_MD_free(NULL) somewhere to satisfy "is called".
#
# So the verifier links the candidate's engine into a program of its own that
# interposes the calls those sentences are about, drives a fixed
# workload through the engine's public hash API, and counts.  stepprobe.c
# carries the argument for interposition over tracing and for measuring the
# rate as a difference between two workload halves rather than as a total.
#
# The probe is its own instrument and is treated like one: if it does not
# build, does not run, or does not complete the workload, the gates that depend
# on it read 0 and say so -- exactly as they do when the AST scan is not
# trustworthy.  A gate that quietly fell back to the scan it was added to
# strengthen would be a gate a candidate could switch off.
PROBE=$LOG/probe.json
probe_ok=0

build_probe() {
    stage_lib
    [ -f "$LIB/stepprobe.c" ] || { vlog "probe: stepprobe.c is not in this image"; return 1; }
    install -m 0644 -o root -g root "$LIB/stepprobe.c" "$SLIB/stepprobe.c"
    local ce="$WORK/build/OsslCryptoEngine/libCryptoEngine.so"
    local pf="$WORK/build/platform/libplatform.so"
    # -rdynamic is the whole mechanism: the executable's dynamic symbols are
    # searched before any shared object's, so a definition here captures
    # libCryptoEngine.so's calls without the engine being rebuilt, and
    # dlsym(RTLD_NEXT) hands each one straight on to libcrypto.
    if ! timeout -k 10 300 gcc -std=gnu99 -O1 -g -Wall -rdynamic -o "$SLIB/stepprobe" \
            "$SLIB/stepprobe.c" \
            -I/app/src/include \
            -I/app/src/tpm/include \
            -I/app/src/tpm/include/prototypes \
            -I/app/src/platform/include \
            -I/app/src/platform/include/prototypes \
            "$ce" "$pf" \
            -Wl,-rpath,"$(dirname "$ce")" -Wl,-rpath,"$(dirname "$pf")" \
            -ldl > "$LOG/probe-build.log" 2>&1; then
        vlog "probe: did not compile against the candidate's headers"
        tail -30 "$LOG/probe-build.log" >&2
        return 1
    fi
    chmod 0755 "$SLIB/stepprobe"
    return 0
}

run_probe() {
    rm -f "$WORK/probe.json" "$PROBE"
    run_unprivileged "$SLIB/stepprobe" "$WORK/probe.json" \
        > "$LOG/probe-run.log" 2>&1 \
        || vlog "probe: the run exited $? (see probe-run.log)"
    [ -s "$WORK/probe.json" ] || { vlog "probe: no JSON was written"; return 1; }
    cp "$WORK/probe.json" "$PROBE"
    return 0
}

# pnum <key> -- one integer out of the probe's JSON, or 0 if it is not there.
pnum() {
    [ -f "$PROBE" ] || { printf 0; return; }
    local v
    v=$(sed -n "s/.*\"$1\":[ ]*\(-\{0,1\}[0-9]\{1,\}\).*/\1/p" "$PROBE" | head -1)
    printf '%s' "${v:-0}"
}

# pnum_s <key> -- one string out of the probe's JSON.  Step 06 reports two
# strings it does not gate -- the configuration path and the property query --
# and a reader that returned them as numbers would report both as 0 and read
# like a finding.  It is kept next to pnum so the two stay in step if the
# probe's JSON writer ever changes shape.
pnum_s() {
    [ -f "$PROBE" ] || return
    sed -n "s/.*\"$1\": *\"\([^\"]*\)\".*/\1/p" "$PROBE" | head -1
}

# jlist <json-file> <key> <regex> -- the matching entries, space separated.
# jhas answers "any", which is the right answer for a gate and the wrong one
# for its log line: a gate that refuses a list of symbols has to name the ones
# it found or nobody downstream can act on it.
jlist() {
    python3 - "$1" "$2" "$3" <<'PYJL'
import json, re, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    sys.exit(0)
rx = re.compile(sys.argv[3])
print(" ".join(x for x in d.get(sys.argv[2], []) if rx.search(x)))
PYJL
}

if build_probe && run_probe; then
    probe_ok=$(pnum ok)
    vlog "probe: ok=$(pnum ok) ops=$(pnum ops) (long half $(pnum ops_long))" \
         "tpmfail=$(pnum tpmfail) signal=$(pnum signal)"
    vlog "probe: libctx new=$(pnum ctx_new) (at init $(pnum ctx_new_at_init))" \
         "free=$(pnum ctx_free) (at stop $(pnum ctx_free_at_stop)," \
         "of ones the engine made $(pnum ctx_free_known)) live=$(pnum ctx_live)"
    vlog "probe: provider load=$(pnum prov_load) into the engine's context" \
         "$(pnum prov_ctx_engine), into the process default" \
         "$(pnum prov_ctx_null), into some other $(pnum prov_ctx_other)"
    vlog "probe: provider unload non-NULL=$(pnum prov_unload)" \
         "(at stop $(pnum prov_unload_at_stop)), on NULL" \
         "$(pnum prov_unload_null), on a provider this probe watched loaded" \
         "$(pnum prov_unload_known)"
    vlog "probe: EVP_MD_fetch total=$(pnum fetch)" \
         "(init $(pnum fetch_at_init), warm-up $(pnum fetch_warm)," \
         "long half $(pnum fetch_long));" \
         "context engine=$(pnum fetch_ctx_engine)" \
         "NULL=$(pnum fetch_ctx_null) other=$(pnum fetch_ctx_other)"
    vlog "probe: EVP_MD_free non-NULL=$(pnum md_free)" \
         "(at stop $(pnum md_free_at_stop)), on NULL=$(pnum md_free_null)," \
         "on a digest this probe watched fetched=$(pnum md_free_known)," \
         "references still held=$(pnum md_live)"
    vlog "probe: hashing calls that named a digest=$(pnum init)," \
         "of them on a digest this probe watched fetched=$(pnum init_known);" \
         "re-init against the context's own digest=$(pnum init_null);" \
         "EVP_Q_digest one-shots=$(pnum qdigest)"
    vlog "probe: distinct digests watched=$(pnum md_distinct)" \
         "(table overflowed $(pnum md_over)), distinct providers" \
         "$(pnum prov_distinct) (overflowed $(pnum prov_over))"
    vlog "probe: symmetric operations=$(pnum sym_ops)" \
         "(warm-up $(pnum sym_ops_warm), long half $(pnum sym_ops_long)," \
         "$(pnum sym_per_round) per round over $(pnum nbits) key size(s))"
    vlog "probe: EVP_CIPHER_fetch total=$(pnum ciph_fetch)" \
         "(init $(pnum ciph_fetch_at_init), warm-up $(pnum ciph_fetch_warm)," \
         "long half $(pnum ciph_fetch_long));" \
         "context engine=$(pnum ciph_ctx_engine)" \
         "NULL=$(pnum ciph_ctx_null) other=$(pnum ciph_ctx_other)"
    vlog "probe: EVP_CIPHER_free non-NULL=$(pnum ciph_free)" \
         "(at stop $(pnum ciph_free_at_stop)), on NULL=$(pnum ciph_free_null)," \
         "on a cipher this probe watched fetched=$(pnum ciph_free_known)," \
         "references still held=$(pnum ciph_live)"
    vlog "probe: cipher-initialising calls that named a" \
         "cipher=$(pnum ciph_use), of them on a cipher this probe watched" \
         "fetched=$(pnum ciph_use_known) (long half $(pnum ciph_use_long));" \
         "distinct ciphers watched=$(pnum ciph_distinct)" \
         "(table overflowed $(pnum ciph_over))"
    vlog "probe: EVP data calls=$(pnum evp_update)" \
         "(warm-up $(pnum evp_update_warm), long half $(pnum evp_update_long))"
    vlog "probe: low-level AES key schedules=$(pnum low_setkey)" \
         "(warm-up $(pnum low_setkey_warm), long half $(pnum low_setkey_long))," \
         "block calls=$(pnum low_block) (warm-up $(pnum low_block_warm)," \
         "long half $(pnum low_block_long))"
    vlog "probe: asymmetric operations=$(pnum asym_ops) of" \
         "$(pnum asym_try) attempted (warm-up $(pnum asym_ops_warm)," \
         "long half $(pnum asym_ops_long))"
    vlog "probe: EVP_PKEY_fromdata=$(pnum fromdata)" \
         "(warm-up $(pnum fromdata_warm), long half $(pnum fromdata_long));" \
         "context engine=$(pnum fromdata_ctx_engine)" \
         "NULL=$(pnum fromdata_ctx_null) other=$(pnum fromdata_ctx_other)" \
         "unknown=$(pnum fromdata_ctx_unknown);" \
         "EVP_PKEY_free=$(pnum pkey_free)"
    vlog "probe: EVP_PKEY_CTX_new_from_pkey=$(pnum pkctx_new)" \
         "(long half $(pnum pkctx_new_long)), of them inheriting the key's" \
         "own context $(pnum pkctx_inherit); context" \
         "engine=$(pnum pkctx_ctx_engine) NULL=$(pnum pkctx_ctx_null)" \
         "other=$(pnum pkctx_ctx_other); freed=$(pnum pkctx_free) (long" \
         "half $(pnum pkctx_free_long)); still held=$(pnum pkctx_live)" \
         "(table overflowed $(pnum pkctx_over))"
    vlog "probe: EVP_PKEY data calls sign=$(pnum pk_sign)" \
         "(long half $(pnum pk_sign_long)) verify=$(pnum pk_verify)" \
         "(long half $(pnum pk_verify_long)) encrypt=$(pnum pk_encrypt)" \
         "(long half $(pnum pk_encrypt_long)) decrypt=$(pnum pk_decrypt)" \
         "(long half $(pnum pk_decrypt_long))"
    vlog "probe: low-level ECDSA calls=$(pnum low_ecdsa)" \
         "(warm-up $(pnum low_ecdsa_warm), long half $(pnum low_ecdsa_long));" \
         "EC_KEY objects made=$(pnum low_eckey), RSA objects" \
         "made=$(pnum low_rsanew) -- those two are reported and not gated," \
         "and NOTES-authoring.md section 9.2.3 is why"
    vlog "probe: OSSL_LIB_CTX_load_config=$(pnum cfg_load)" \
         "(at startup $(pnum cfg_load_at_init)); context" \
         "engine=$(pnum cfg_ctx_engine) NULL=$(pnum cfg_ctx_null)" \
         "other=$(pnum cfg_ctx_other); returned 1 $(pnum cfg_ok) time(s)," \
         "$(pnum cfg_ok_engine) of them on the engine's own context;" \
         "first path '$(pnum_s cfg_path)'"
    vlog "probe: providers by name -- fips=$(pnum prov_fips)" \
         "($(pnum prov_fips_ok) of them loaded, $(pnum prov_fips_engine)" \
         "into the engine's context), default=$(pnum prov_default)," \
         "base=$(pnum prov_base); distinct fips handles=$(pnum fips_distinct);" \
         "fips unloads=$(pnum fips_unload) ($(pnum fips_unload_at_stop) at" \
         "shutdown, $(pnum fips_unload_before_stop) before it), still" \
         "held=$(pnum fips_live)"
    vlog "probe: OSSL_PROVIDER_self_test=$(pnum selftest)" \
         "(at startup $(pnum selftest_at_init)); on a provider this probe" \
         "watched loaded $(pnum selftest_known), on the fips one" \
         "$(pnum selftest_fips); returned 1 $(pnum selftest_ok) time(s)"
    vlog "probe: property queries -- EVP_MD_fetch" \
         "NULL=$(pnum pq_md_null) empty=$(pnum pq_md_empty)" \
         "named=$(pnum pq_md_set); EVP_CIPHER_fetch" \
         "NULL=$(pnum pq_cipher_null) empty=$(pnum pq_cipher_empty)" \
         "named=$(pnum pq_cipher_set); EVP_PKEY_CTX_new_from_name" \
         "NULL=$(pnum pq_pkey_name_null) empty=$(pnum pq_pkey_name_empty)" \
         "named=$(pnum pq_pkey_name_set); EVP_PKEY_CTX_new_from_pkey" \
         "NULL=$(pnum pq_pkey_pkey_null) empty=$(pnum pq_pkey_pkey_empty)" \
         "named=$(pnum pq_pkey_pkey_set); EVP_Q_digest" \
         "NULL=$(pnum pq_qdigest_null) empty=$(pnum pq_qdigest_empty)" \
         "named=$(pnum pq_qdigest_set)"
    vlog "probe: property queries in total -- NULL=$(pnum pq_null)" \
         "empty=$(pnum pq_empty) named=$(pnum pq_set); the first named one" \
         "was '$(pnum_s pq_text)' (reported, not gated -- see the top);" \
         "key-operation contexts built with no query at all=$(pnum pq_noquery)"
    vlog "probe: the counterfactual context -- built=$(pnum pq_shadow);" \
         "fetches and key-operation contexts pinned=$(pnum pq_pin)" \
         "drifted=$(pnum pq_drift) not comparable=$(pnum pq_nocmp);" \
         "first drift" \
         "'$(pnum_s pq_drift_at)'; guard re-entries=$(pnum pq_reenter)"
else
    vlog "probe: not usable on this tree -- every gate that reads it scores 0"
fi


# ------------------------------------------------- the second run, the fixture
#
# The instrument's own control.  See "THE SECOND PROBE RUN" at the top: with
# PROBE_FAIL_SELFTEST=1 the probe's OSSL_PROVIDER_self_test interposer returns
# 0 without calling through, so a tree that checks the result must not finish
# its own startup and a tree that ignores it finishes exactly as it did in the
# first run.  Nothing else about the run changes -- same binary, same workload,
# same engine.
#
# It is run only when the first run worked.  A tree whose probe did not build,
# did not run, or did not complete has nothing for the fixture to discriminate
# between, and `fips-self-tested-and-live` already reads 0 on all three.
#
# `fixture_ok` means the second run PRODUCED A MEASUREMENT, which is not the
# same as the second run succeeding: on a correct tree the engine refuses to
# start, the probe exits non-zero, and that is the passing shape.  What has to
# be true is that the JSON was written and that the forced failure actually
# engaged -- selftest_forced >= 1.  A fixture that silently did not fire would
# make every tree look like one that checks, which is trap 6 exactly: the
# finding would be about the instrument and would read as a finding about the
# tree.
FIXTURE=$LOG/probe-fixture.json
fixture_ok=0

# pnum2 <key> -- one integer out of the fixture run's JSON.  Deliberately a
# second function rather than a parameter on pnum: every inherited gate on this
# page calls pnum, and a pnum that could be pointed at another file is a pnum
# that can be pointed at the wrong one.
pnum2() {
    [ -f "$FIXTURE" ] || { printf 0; return; }
    local v
    v=$(sed -n "s/.*\"$1\":[ ]*\(-\{0,1\}[0-9]\{1,\}\).*/\1/p" "$FIXTURE" | head -1)
    printf '%s' "${v:-0}"
}

if [ "$probe_ok" = 1 ]; then
    rm -f "$WORK/probe-fixture.json"
    run_unprivileged env PROBE_FAIL_SELFTEST=1 \
        "$SLIB/stepprobe" "$WORK/probe-fixture.json" \
        > "$LOG/probe-fixture.log" 2>&1 \
        || vlog "probe: the fixture run exited $? -- expected on a tree that" \
                "refuses to start on a failed self test"
    if [ -s "$WORK/probe-fixture.json" ]; then
        cp "$WORK/probe-fixture.json" "$FIXTURE"
        if [ "$(pnum2 selftest_forced)" -ge 1 ]; then
            fixture_ok=1
        else
            vlog "probe: the fixture run wrote its JSON but the forced" \
                 "self-test failure never fired (selftest_forced=0," \
                 "self tests seen $(pnum2 selftest)) -- the fixture cannot" \
                 "discriminate on this tree"
        fi
    else
        vlog "probe: the fixture run wrote no JSON"
    fi
    vlog "probe: fixture run ok=$(pnum2 ok) tpmfail=$(pnum2 tpmfail)" \
         "signal=$(pnum2 signal) self tests=$(pnum2 selftest)" \
         "(forced $(pnum2 selftest_forced)) ops=$(pnum2 ops)" \
         "-> usable=$fixture_ok"
fi
# ================================================================== the scans

# One AST pass answers all three gate families -- this step's six, step 02's
# seven and step 01's two counted facts.  Every pair any of them asks about is
# named here; astscan reports the ones that hold, and jreach/jcall read each
# pair out by name so that a gate is never decided by a count over a set it
# does not own.
#
# astscan's bounded reachability stops at one hop through a function the tree
# defines, and this step also asks it for the unbounded walk.  Step 02's note
# on why the bound decides the shape of its pairs applies unchanged to the
# pairs the bans below are written on, and the inherited gates are all of that
# kind; this step's own four gates ask their positive questions with reach_any
# instead, for the reason written beside it.  The two entry points the library
# context and the provider are asked about are _cpri__InitCryptoUnits and
# _cpri__StopCryptoUnits, which are the engine's own startup and shutdown and
# are named in the instruction as such.
#
#   HASH_WORK   the entry points that do hash work on behalf of a caller.
#               Gate 5 asks all of them the same negative question, so the
#               list is written once and the request list is built from it.
#               _cpri__CompleteHMAC is on it because the sentence the gate
#               enforces is about any operation done on a caller's behalf, and
#               that is one: it is in the probe's workload, so the rate half
#               has always covered it, and leaving it off the list meant the
#               AST half asked the question of six of the seven places the
#               sentence names.  _cpri__KDFa and _cpri__KDFe went on for
#               the same reason and had been left off for the same one: both
#               are public entry points that run hash work in a loop on a
#               caller's behalf -- KDFa an HMAC per block, KDFe a plain hash
#               per block -- so a port that fetched and freed a digest inside
#               either of them was doing the thing gate 5 forbids, in a place
#               gate 5 was not looking.  The probe's workload drives them too,
#               so the rate half and the AST half now name the same nine
#               places.
HASH_WORK=(_cpri__StartHash _cpri__UpdateHash _cpri__CompleteHash
           _cpri__HashBlock _cpri__MGF1 _cpri__StartHMAC _cpri__CompleteHMAC
           _cpri__KDFa _cpri__KDFe)

# Step 02's four groups.  Three are inherited verbatim -- see that step's
# test.sh for why they are split this way.  DRAW_PUBLIC is narrowed, and this
# is the only inherited threshold on the ladder that any step changes.
#
# Step 02 named four public draws and required each to reach RAND_bytes rather
# than RAND_priv_bytes.  Two of the four, OaepEncode and
# RSAES_PKSC1v1_5Encode, are exactly the encoders this step deletes: their
# replacement is EVP_PKEY_encrypt, which draws its OAEP seed and its PKCS#1
# padding string inside libcrypto where no scan of /app can see them.  The gate
# is not wrong and, with those two still in the list, it is not passable.
#
# PssEncode stays, and the distinction is the one the whole step turns on: it
# is the encode half of *signing*, which is the private side, and the private
# side does not move (NOTES-authoring.md sections 9.1.2a, 9.1.5, 9.1.6).  It
# still draws a salt, still draws it through _cpri__GenerateRandom, and is
# still a public draw in step 02's sense.
#
# Narrowing beats retiring: the private/public separation step 02's instruction
# actually asked for -- SignSM2, SchnorrEcc, _cpri__GetEphemeralEcc,
# GetRandomPrivate -- is still asked in full, and what remains askable on the
# public side stays asked.
DRAW_SECRET=(SignSM2)
DRAW_HELPER=(GetRandomPrivate)
DRAW_DELEGATE=(_cpri__GetEphemeralEcc SchnorrEcc)
DRAW_PUBLIC=(_cpri__GenerateRandom PssEncode)


# Step 04's symmetric surface.  Eight compiled entry points do symmetric work
# on a caller's behalf; `ciphers-fetched-once` asks all eight the same negative
# question, so the list is written once and the request list is built from it.
#
# Nine public names resolve to these eight: CpriSym_fp.h defines
# _cpri__AESDecryptCTR and _cpri__AESDecryptOFB as macros onto the encrypt
# side, because both modes are their own inverse.  The macros are not
# functions, so there is nothing for the AST to reach from and nothing to put
# on this list -- the bodies they expand to are already on it.
#
# _cpri__AESDecryptCBC is here and cannot be driven by the probe: it is not in
# CpriSym_fp.h's declarations, only in its definition, so an outside consumer
# has no prototype for it.  The scan does not care.  See the note at the top.
SYM_WORK=(_cpri__AESEncryptCBC _cpri__AESDecryptCBC
          _cpri__AESEncryptCFB _cpri__AESDecryptCFB
          _cpri__AESEncryptCTR
          _cpri__AESEncryptECB _cpri__AESDecryptECB
          _cpri__AESEncryptOFB)

# The built-in cipher constructors and the legacy by-name lookup, which are to
# ciphers what EVP_sha256 and EVP_get_digestbyname were to digests at step 03.
# Generated rather than typed out, because there are twenty-six of them and a
# list that long, maintained by hand beside a regex that has to agree with it,
# is a place for the two to drift.  BUILTIN_CIPHER below is the authority and
# this loop is that regex enumerated; they are built from the same three words
# and the same eight, so they cannot disagree.
CIPHER_BUILTIN=()
for b in 128 192 256; do
    for m in ecb cbc cfb cfb1 cfb8 cfb128 ofb ctr; do
        CIPHER_BUILTIN+=("EVP_aes_${b}_${m}")
    done
done
CIPHER_BUILTIN+=(EVP_get_cipherbyname EVP_get_cipherbynid)

# <openssl/aes.h>, which is what this step takes away.  AES_KEY is on the list
# and is not a function: it is the stack object every one of the eight entry
# points declares, it is the thing the header exists to declare, and a tree
# that still has one has not finished.  It is asked of the source text only --
# a type is not a call and not a relocation.
AES_LOWLEVEL=(AES_set_encrypt_key AES_set_decrypt_key AES_encrypt AES_decrypt
              AES_ecb_encrypt AES_cbc_encrypt AES_cfb128_encrypt
              AES_cfb1_encrypt AES_cfb8_encrypt AES_ofb128_encrypt
              AES_ctr128_encrypt AES_ige_encrypt AES_bi_ige_encrypt
              AES_wrap_key AES_unwrap_key AES_options)


# Step 05's asymmetric surface.  Four compiled entry points do public-key work
# on a caller's behalf and are what this step moves; two do the private half
# and are what it deliberately leaves alone.
#
# The split is not a matter of taste.  RSADP is the private-key exponentiation,
# and step 02's `rsa-consttime` gate requires it to set BN_FLG_CONSTTIME and go
# through BN_mod_exp_mont_consttime.  Doing the private operations with
# EVP_PKEY_sign and EVP_PKEY_decrypt would satisfy the sentence "the private
# operation is constant time" and would delete the evidence step 02 is scored
# on -- trap 9 in its second shape, where a later step makes an earlier
# requirement unpassable.  So the private half stays where it is, the
# instruction says so in those words, and `rsa-through-evp` asserts it rather
# than leaving it to good manners.
#
# The public half carries no such obligation.  RSAEP is e=65537 against a
# public modulus with nothing secret in it; what this step hands to the library
# is the *padding* around it, which is the part that has been collecting CVEs
# since Bleichenbacher in 1998 and the part the engine currently writes out by
# hand in C.
ASYM_PUBLIC=(_cpri__EncryptRSA _cpri__ValidateSignatureRSA
             SignEcdsa ValidateSignatureEcdsa)
ASYM_PRIVATE=(_cpri__DecryptRSA _cpri__SignRSA)
ECDSA_WORK=(SignEcdsa ValidateSignatureEcdsa)

# The four hand-rolled encoders this step deletes and the four it keeps.  Both
# lists are named and `no-handrolled-padding` asserts both, because a gate that
# only asked about the first four is passed by a tree that deleted all eight --
# which would have taken the private side's padding with it and is a different
# migration from this one.
#
# The pairing is exact and runs in opposite directions, which is why the list
# is not "all the Encodes".  OAEP and PKCS#1 v1.5 encryption padding is applied
# by whoever encrypts, which is the public side, and removed by whoever
# decrypts, which is the private side.  PSS and PKCS#1 v1.5 signature padding
# is applied by whoever signs, which is the private side, and removed by
# whoever verifies, which is the public side.  So each name on PAD_GONE is the
# public-key half of a scheme and the name beside it on PAD_STAYS is the
# private-key half of the same scheme.
PAD_GONE=(OaepEncode RSAES_PKSC1v1_5Encode PssDecode RSASSA_Decode)
PAD_STAYS=(OaepDecode RSAES_Decode PssEncode RSASSA_Encode)

# <openssl/ec.h>'s EC_KEY object and the ECDSA_* API that drives it.  All of it
# is OSSL_DEPRECATEDIN_3_0, none of it consults a library context or reaches a
# provider, and every use of it in this tree is inside the two functions on
# ECDSA_WORK -- which is what makes "no longer imported" an achievable sentence
# rather than an aspiration.
#
# EC_KEY is a type, so `called` can never hold it; it is asked of the source
# text only, exactly as AES_KEY was at step 04.
#
# ECDSA_SIG is NOT on this list, and the omission is deliberate.  It is a type
# too, and it looks like it belongs beside EC_KEY, but the signature object and
# its i2d/d2i pair are not deprecated at 3.0 and are still the only supported
# way to get at the r and s of an ECDSA signature -- which is exactly what this
# engine needs, because TPM2B_ECC_PARAMETER is r and s and nothing else.
# EVP_PKEY_sign hands back a DER SEQUENCE.  Banning the object that parses it
# would leave a correct port with hand-written ASN.1 as its only route, and a
# gate that makes the good answer harder to write than the bad one is a gate
# that has stopped measuring the migration.  What IS banned is the deprecated
# API that drives a key: EC_KEY_* and the ECDSA_do_*/ECDSA_sign*/ECDSA_verify
# family.  NOTES-authoring.md section 9.3.5 records the narrowing and why.
#
# The prefixes are `EC_KEY_` and `ECDSA_`, not `EC_`.  EC_GROUP, EC_POINT and
# BN_CTX are not deprecated at 3.0, are how _cpri__EccPointMultiply and
# EccCurveInit are written, and this step does not touch them.  And the TPM
# layer above the engine has names of its own that a prefix regex would eat:
# ECDSA_VALUE in Implementation.h, ECDSA_Marshal and ECDSA_Unmarshal in
# marshal.c.  Every entry below is an exact name and astscan's text pass
# matches on word boundaries, so ECDSA_Marshal is not ECDSA_sign and
# ECDSA_VALUE is not ECDSA_verify.
EC_LOWLEVEL=(EC_KEY_new EC_KEY_new_by_curve_name EC_KEY_free EC_KEY_dup
             EC_KEY_set_group EC_KEY_get0_group EC_KEY_set_conv_form
             EC_KEY_set_public_key EC_KEY_get0_public_key
             EC_KEY_set_private_key EC_KEY_get0_private_key
             EC_KEY_set_public_key_affine_coordinates
             EC_KEY_generate_key EC_KEY_check_key EC_KEY_set_asn1_flag
             EC_KEY_precompute_mult
             ECDSA_do_sign ECDSA_do_sign_ex ECDSA_do_verify
             ECDSA_sign ECDSA_sign_ex ECDSA_sign_setup ECDSA_verify
             ECDSA_size)

# <openssl/rsa.h>'s low-level API.  The engine has never used a line of it --
# it does RSA with BIGNUMs directly -- and the probe confirms it from the other
# side: RSA_new is called zero times on the untouched step 04 tree.
#
# The list is here because "stop writing your own padding" has a wrong answer
# that looks exactly like the right one: RSA_public_encrypt(...,
# RSA_PKCS1_OAEP_PADDING) against an RSA object built with RSA_new and
# RSA_set0_key.  That is genuinely the library's padding, it is genuinely not
# hand-rolled, and it is the 1.0.2 way of doing it -- deprecated at 3.0, no
# library context, no provider, no FIPS module can see it.  A tree that took it
# would satisfy every sentence about hand-rolled padding and none of the
# reasons for them.  Counted zero on both calibrations, so it can never be the
# reason a gate fails by accident; it is here to close a door, not to measure.
RSA_LOWLEVEL=(RSA_new RSA_free RSA_set0_key RSA_set0_factors RSA_get0_key
              RSA_public_encrypt RSA_private_decrypt
              RSA_public_decrypt RSA_private_encrypt
              RSA_sign RSA_verify RSA_size RSA_generate_key_ex
              RSA_padding_add_PKCS1_OAEP RSA_padding_check_PKCS1_OAEP
              RSA_padding_add_PKCS1_OAEP_mgf1 RSA_padding_check_PKCS1_OAEP_mgf1
              RSA_padding_add_PKCS1_type_1 RSA_padding_check_PKCS1_type_1
              RSA_padding_add_PKCS1_type_2 RSA_padding_check_PKCS1_type_2
              RSA_padding_add_PKCS1_PSS RSA_verify_PKCS1_PSS
              RSA_padding_add_PKCS1_PSS_mgf1 RSA_verify_PKCS1_PSS_mgf1)
CALL_IN=()
for fn in "${HASH_WORK[@]}"; do
    CALL_IN+=("$fn:EVP_MD_free")
done
for fn in "${DRAW_SECRET[@]}" "${DRAW_HELPER[@]}" "${DRAW_DELEGATE[@]}" \
          "${DRAW_PUBLIC[@]}"; do
    CALL_IN+=("$fn:RAND_priv_bytes"    "$fn:RAND_bytes")
    CALL_IN+=("$fn:RAND_priv_bytes_ex" "$fn:RAND_bytes_ex")
done
for fn in "${DRAW_DELEGATE[@]}"; do
    CALL_IN+=("$fn:GetRandomPrivate")
done
CALL_IN+=(
    # step 03's own
    _cpri__InitCryptoUnits:OSSL_LIB_CTX_new
    _cpri__InitCryptoUnits:OSSL_PROVIDER_load
    _cpri__InitCryptoUnits:OSSL_PROVIDER_try_load
    _cpri__StopCryptoUnits:OSSL_LIB_CTX_free
    _cpri__StopCryptoUnits:OSSL_PROVIDER_unload
    _cpri__StopCryptoUnits:EVP_MD_free
    _cpri__HashStartup:EVP_MD_fetch
    # step 02's, re-asked
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
    # step 01's, re-asked
    _cpri__InitCryptoUnits:OPENSSL_init_crypto
)
for fn in "${SYM_WORK[@]}"; do
    CALL_IN+=("$fn:EVP_CIPHER_free")
done
CALL_IN+=(
    # step 04's own
    _cpri__SymStartup:EVP_CIPHER_fetch
    _cpri__StopCryptoUnits:EVP_CIPHER_free
)
# Step 05's own.  Every public-key entry point is asked the same two questions
# through the loop -- did it build its EVP_PKEY, and did it give it back -- and
# then each is asked the questions that are its own.
#
# The free question is asked FORWARDS here, and that is the opposite of how
# steps 03 and 04 ask it.  There the object is an algorithm: one EVP_MD per
# digest, one EVP_CIPHER per mode and key size, fetched once at startup and
# held for the life of the engine, so EVP_MD_free reached from a hash entry
# point means a fetch per operation and is the defect.  Here the object is a
# KEY, and the key arrives in the caller's argument list on every single call.
# There is nothing to cache and nothing to hold: an EVP_PKEY built inside
# _cpri__EncryptRSA belongs to that call and has to be freed before it returns,
# and a tree where EVP_PKEY_free is NOT reachable from these four is a tree
# that leaks one key object per TPM command.  Same instrument, opposite
# direction, because the thing being counted is a different kind of thing.
for fn in "${ASYM_PUBLIC[@]}"; do
    CALL_IN+=("$fn:EVP_PKEY_fromdata" "$fn:EVP_PKEY_free")
done
CALL_IN+=(
    # The RSA public half: where its bytes go now, and the three things it must
    # no longer reach.  _cpri__MGF1 is the interesting one -- it is the mask
    # generation function OaepEncode and PssDecode drive, it stays in the tree
    # because OaepDecode and PssEncode still drive it, and so "is it gone" is
    # not a question about the tree at all.  It is a question about what these
    # two entry points can still reach, which is why it is asked as a pair.
    _cpri__EncryptRSA:EVP_PKEY_encrypt
    _cpri__EncryptRSA:OaepEncode
    _cpri__EncryptRSA:RSAES_PKSC1v1_5Encode
    _cpri__EncryptRSA:_cpri__MGF1
    _cpri__ValidateSignatureRSA:EVP_PKEY_verify
    _cpri__ValidateSignatureRSA:PssDecode
    _cpri__ValidateSignatureRSA:RSASSA_Decode
    _cpri__ValidateSignatureRSA:_cpri__MGF1
    # The RSA private half, which does not move.  RSADP is asked of both
    # through the ASYM_PRIVATE loop below, because `rsa-through-evp` asks it
    # of the array rather than of two names and the two cannot be allowed to
    # drift; these four say the padding that belongs to the private side is
    # still the tree's own.
    _cpri__DecryptRSA:OaepDecode
    _cpri__DecryptRSA:RSAES_Decode
    _cpri__SignRSA:PssEncode
    _cpri__SignRSA:RSASSA_Encode
    # ECDSA.  EC_KEY_new stands for the whole deprecated object here: it is the
    # first call of the six-call sequence both functions currently open with,
    # so a tree that still reaches it has not rewritten the sequence, whatever
    # it did to the rest.  The ban list proper is asked of the import table and
    # the source text, below.
    SignEcdsa:EVP_PKEY_sign
    SignEcdsa:EC_KEY_new
    ValidateSignatureEcdsa:EVP_PKEY_verify
    ValidateSignatureEcdsa:EC_KEY_new
)
for fn in "${ASYM_PRIVATE[@]}"; do
    CALL_IN+=("$fn:RSADP")
done
CALLS=(
    EVP_MD_CTX_new
    EVP_MD_CTX_free
    EVP_MD_fetch
    EVP_MD_free
    RAND_priv_bytes
    RAND_priv_bytes_ex
    BN_bn2bin
    BN_bn2binpad
    BN_set_flags
    BN_mod_exp_mont_consttime
    # Gate 6's two ban lists, spelled out.  astscan reports one entry per name
    # it was asked about and nothing else, so `called` holds the answer to the
    # questions on this list and to no others: a ban whose names are not here
    # reads as an empty `called` on every tree, which is indistinguishable from
    # a clean one.  The gate matches them back out with its own regexes, and
    # those regexes are the authority -- what is written here is that anchored
    # spelling enumerated, and the two have to be changed together.
    EVP_MD_CTX_md EVP_MD_CTX_get0_md_data EVP_MD_meth_get_app_datasize
    # Gate 4's negative list, here for the same reason.
    EVP_sha1 EVP_sha224 EVP_sha256 EVP_sha384 EVP_sha512 EVP_sm3
    EVP_get_digestbyname EVP_get_digestbynid
    SHA1 SHA1_Init SHA1_Update SHA1_Final SHA1_Transform
    SHA224 SHA224_Init SHA224_Update SHA224_Final SHA224_Transform
    SHA256 SHA256_Init SHA256_Update SHA256_Final SHA256_Transform
    SHA384 SHA384_Init SHA384_Update SHA384_Final SHA384_Transform
    SHA512 SHA512_Init SHA512_Update SHA512_Final SHA512_Transform
    # Step 04's own: the fetch and the free it requires, the two context calls
    # that are logged rather than required, and both ban lists.  AES_KEY is not
    # here -- it is a type, so `called` can never hold it; it is asked of the
    # text, below.
    EVP_CIPHER_fetch
    EVP_CIPHER_free
    EVP_CIPHER_CTX_new
    EVP_CIPHER_CTX_free
)
for c in "${CIPHER_BUILTIN[@]}" "${AES_LOWLEVEL[@]}"; do CALLS+=("$c"); done
# The same two bans asked of the source text rather than of the call graph,
# because that is what the instruction says: "gone from the sources the build
# compiles", "None of that API appears anywhere in the sources the build
# compiles".  `called` cannot answer that and neither can the import table --
# `dlsym(RTLD_DEFAULT, "SHA256_Init")` is a string, so it is not a CALL_EXPR
# and not an undefined reloc, and both halves come back clean on a tree that
# hashes with it.  astscan takes the comments out first, so the golden saying
# in CpriHash.c which getters 3.0 emptied is still a description of the
# migration rather than a use of them.
#
# Not every name on CALLS can be asked this way, and the five that cannot are
# the reason this is a separate list rather than the same one.  `SHA256` alone
# is the one-shot function *and* the digest's name: the golden's own fetch
# table is `{ TPM_ALG_SHA256, "SHA256", NULL }`, and a text scan for that token
# would fail the correct tree for naming the digest it fetches.  The suffixed
# spellings have no such double life.  What the bare five leave open -- the
# loader reached with a name this list cannot hold -- is closed at the
# mechanism instead: dlsym, dlvsym and dlopen appear nowhere in the tree, have
# no use in an engine that links what it calls, and are the only way a string
# becomes a call.
TEXTS=(
    EVP_MD_CTX_md EVP_MD_CTX_get0_md_data EVP_MD_meth_get_app_datasize
    EVP_sha1 EVP_sha224 EVP_sha256 EVP_sha384 EVP_sha512 EVP_sm3
    EVP_get_digestbyname EVP_get_digestbynid
    SHA1_Init SHA1_Update SHA1_Final SHA1_Transform
    SHA224_Init SHA224_Update SHA224_Final SHA224_Transform
    SHA256_Init SHA256_Update SHA256_Final SHA256_Transform
    SHA384_Init SHA384_Update SHA384_Final SHA384_Transform
    SHA512_Init SHA512_Update SHA512_Final SHA512_Transform
    dlsym dlvsym dlopen
    AES_KEY
)
for t in "${CIPHER_BUILTIN[@]}" "${AES_LOWLEVEL[@]}"; do TEXTS+=("$t"); done
# Step 05's own.  The construction, the four data calls, the two frees, and
# both ways of writing the OSSL_PARAM list the construction is given -- the
# builder with the two frees it owes, or an array closed with
# OSSL_PARAM_construct_end or with the OSSL_PARAM_END terminator (a compound
# literal, so it is on the TEXTS list rather than this one), and neither of
# those owes anything.  EVP_PKEY_decrypt and EVP_PKEY_decrypt_init are asked
# about and required to be absent from nothing:
# they are logged, because a tree that moved the private half to EVP as well is
# a tree whose step 02 gates are about to fail and the log line is what makes
# that legible in the trajectory rather than mysterious.
CALLS+=(
    EVP_PKEY_fromdata EVP_PKEY_fromdata_init
    EVP_PKEY_CTX_new_from_name EVP_PKEY_CTX_new_from_pkey
    EVP_PKEY_sign_init EVP_PKEY_sign
    EVP_PKEY_verify_init EVP_PKEY_verify
    EVP_PKEY_encrypt_init EVP_PKEY_encrypt
    EVP_PKEY_decrypt_init EVP_PKEY_decrypt
    EVP_PKEY_free EVP_PKEY_CTX_free
    OSSL_PARAM_BLD_new OSSL_PARAM_BLD_to_param OSSL_PARAM_BLD_free
    OSSL_PARAM_free OSSL_PARAM_construct_end
)
for c in "${EC_LOWLEVEL[@]}" "${RSA_LOWLEVEL[@]}"; do CALLS+=("$c"); done
# Both ban lists asked of the source text as well, for the reason the step 03
# and step 04 lists above are: `called` does not see a name that reached the
# loader as a string, and the import table does not either.  Every name on both
# lists can be asked this way -- none of them is also the spelling of something
# a correct port has to write down, which is what kept the bare SHA256 off the
# list above -- so the two lists are the same two lists and not a subset.
for t in "${EC_LOWLEVEL[@]}" "${RSA_LOWLEVEL[@]}"; do TEXTS+=("$t"); done
# The type.  It is not a call and not a relocation; it is the stack object the
# sequence this step deletes is built around, and a tree that still declares
# one has kept the API whatever else it did.  ECDSA_SIG is not here: see the
# note beside EC_LOWLEVEL.
TEXTS+=(EC_KEY)
# And the OSSL_PARAM array terminator, for the reason given beside the route
# block further down: OSSL_PARAM_END is a compound literal, so no call list
# can see it and the text scan is the only instrument that can.  There is no
# second meaning of this spelling to trip over.
TEXTS+=(OSSL_PARAM_END)

# Step 06's own.  Two call sites and where each of them has to be reached
# from.  Both are asked of _cpri__InitCryptoUnits, which is the function
# steps 01 and 03 already name as the engine's startup, so a candidate is not
# being asked to guess which entry point counts as startup at this step
# either.
#
# The configuration load is asked with reach_any rather than reach, for the
# reason written beside reach_any: it is a POSITIVE claim about whose account
# the work is done on.  A port that puts its context setup in an
# OsslContextStartup() helper that _cpri__InitCryptoUnits calls has loaded the
# configuration at startup exactly as much as one that writes the call inline,
# and bounding the question at one hop would be measuring how the port divided
# its helpers up.  The golden writes it inline and would pass either way,
# which is precisely why the looser reading is the honest one to require.
CALL_IN+=(
    _cpri__InitCryptoUnits:OSSL_LIB_CTX_load_config
    _cpri__InitCryptoUnits:OSSL_PROVIDER_self_test
)
CALLS+=(
    OSSL_LIB_CTX_load_config
    OSSL_PROVIDER_self_test
    # Logged, not required.  CONF_modules_load_file is the pre-3.0 way of
    # reading a configuration and it configures the PROCESS, not a context; a
    # tree that reached for it has made the mistake this step is about and the
    # log line is what makes that legible in a trajectory instead of leaving
    # the author to guess from a failed gate.  Same for the two spellings that
    # configure the default context explicitly.
    CONF_modules_load_file CONF_modules_load_file_ex
    OPENSSL_init_crypto
    OSSL_PROVIDER_available
    EVP_default_properties_is_fips_enabled EVP_default_properties_enable_fips
    EVP_set_default_properties
)
# --reach-deep is this step's own request and costs the earlier steps
# nothing: astscan fills call_reach_deep only when it is asked, and the
# bounded call_reach it has always emitted is untouched.  Why this step needs
# both is beside reach_any below.
scan_args=(--reach-deep)
for p in "${CALL_IN[@]}"; do scan_args+=(--call-in "$p"); done
for c in "${CALLS[@]}";   do scan_args+=(--calls   "$c"); done
for t in "${TEXTS[@]}";   do scan_args+=(--text    "$t"); done

if scan_ast "${scan_args[@]}" \
        > "$LOG/ast-counts.json" 2>>"$LOG/verify.log"; then
    ast_parsed=$(jnum "$LOG/ast-counts.json" parsed)
    ast_errors=$(jnum "$LOG/ast-counts.json" errors)
    ast_member=$(jnum "$LOG/ast-counts.json" member_access)
    ast_value=$(jnum "$LOG/ast-counts.json" value_decl)
    vlog "ast: $ast_parsed TU(s), $ast_errors parse error(s)," \
         "member_access=$ast_member value_decl=$ast_value," \
         "$(jnum "$LOG/ast-counts.json" call_reach) of ${#CALL_IN[@]}"\
         "pairs reached within one hop," \
         "$(jnum "$LOG/ast-counts.json" call_reach_deep) at any depth" \
         "over $(jnum "$LOG/ast-counts.json" call_graph) intra-tree edge(s)"
else
    ast_parsed=0; ast_errors=1; ast_member=-1; ast_value=-1
    vlog "ast: the scan did not run"
fi

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

# reach_any <caller> <callee> -> 1/0: CALLER reaches CALLEE through any
# number of the tree's own functions.  Same 0-on-an-unparsed-tree rule.
#
# Which of the two a gate uses is decided by what its sentence claims, and the
# two claims are not the same shape.  A POSITIVE claim -- "this entry point's
# operation goes through the library" -- is about whose account the work is
# done on, and a port that puts its key construction in a function its padding
# helper calls has done it on that entry point's account exactly as much as
# one that writes it in the entry point itself.  Bounding that question at one
# hop does not measure the claim; it measures how the port divided its own
# helpers up, and fails a correct tree for dividing them one level further
# than the golden did.  A NEGATIVE claim -- "this entry point cannot still
# reach the hand-rolled encoder" -- goes the other way: unbounded, it sweeps
# in any utility three hops down that the ban was never about, and the ban is
# already carried by the harder half beside it, which asks whether the code is
# compiled at all.  So the positive questions below read this and the bans
# read reach.
reach_any() {
    [ "$ast_ok" = 1 ] || { printf 0; return; }
    jreachx "$AST" "$1" "$2"
}

# reach_draw <caller> <old> <new> -> 1/0: CALLER reaches the draw, under
# either spelling.  On 3.x each draw also comes as an _ex form that takes a
# library context (RAND_priv_bytes_ex, RAND_bytes_ex).  The inherited claim is
# about which pool the bytes come from -- the instruction names the private and
# the public generator, not a function -- and that claim is the same whichever
# spelling a tree writes, so the two inherited RNG gates accept both, as step
# 08's copy of them already does.  (Migration rubric review round 2,
# 2026-10-05: a tree that moved its draws onto its own context early lost both
# gates here.)
reach_draw() {
    [ "$ast_ok" = 1 ] || { printf 0; return; }
    if [ "$(reach "$1" "$2")" = 1 ] || [ "$(reach "$1" "$3")" = 1 ]; then
        printf 1
    else
        printf 0
    fi
}

# direct <caller> <callee> -> 1/0: the call is written in CALLER's own body.
direct() {
    [ "$ast_ok" = 1 ] || { printf 0; return; }
    jcall "$AST" "$1" "$2"
}

# Every pair the gates below name literally, checked against the request list.
# A gate asking about a pair nobody requested reads 0 and is not failing -- it
# is answering a question nobody asked -- so the mismatch is caught here rather
# than silently scored.  Pairs named through a loop variable cannot drift: the
# request list is built from the same arrays the loops walk.
ask_missing=()
ask_n=0
while read -r a b; do
    ask_n=$(( ask_n + 1 ))
    case " ${CALL_IN[*]} " in
        *" $a:$b "*) ;;
        *) ask_missing+=("$a:$b") ;;
    esac
done < <(grep -oE '\$\((reach_any|reach|direct) [A-Za-z0-9_]+ [A-Za-z0-9_]+\)' "$0" |
         sed -E 's/^.\((reach_any|reach|direct) //; s/.$//' | sort -u)
vlog "ast: $ask_n literal gate pair(s) checked against the" \
     "${#CALL_IN[@]}-pair request list"
if [ ${#ask_missing[@]} -ne 0 ]; then
    vlog "BUG: gate pair(s) never requested from the scan: ${ask_missing[*]}"
    ast_ok=0
fi

# ========================================= steps 01-02's gates, re-asked (0.08)
#
# All thirteen, not step 02's seven.  Trap 9 is a step-local mean hiding an
# inherited test that this step's own requirements made unpassable, and the way
# to not have that happen is to keep asking.  Two of the thirteen are worth
# naming in advance because this step lands directly on top of them:
#
#   * evp-md-ctx-lifecycle.  The digest CONTEXT is still obtained from
#     libcrypto's allocator and given back to it; what moved at 3.0 is the
#     state behind the context, not the context.  A port that stops calling
#     EVP_MD_CTX_new has misread this step.
#   * explicit-init.  _cpri__InitCryptoUnits still initialises libcrypto
#     itself.  A library context is an addition to that call, not a replacement
#     for it, and the instruction says so in as many words -- this is the one
#     inherited requirement a reasonable candidate might otherwise decide is
#     now redundant.
#
# The code is steps 01 and 02's, unchanged, with `rgate` in place of `gate` and
# the owner still theirs.  A property does not change hands because a later
# step also depends on it.
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

rpb_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^RAND_priv_bytes(_ex)?$')
rpb_called=$(jhas "$AST" called '^RAND_priv_bytes(_ex)?$')
if [ "$ast_ok" != 1 ]; then
    rgate 0 private-pool-present "the AST scan is not trustworthy on this tree" \
        "($ast_parsed TUs, $ast_errors errors)"
elif [ "$rpb_imp" = 1 ] && [ "$rpb_called" = 1 ]; then
    rgate 1 private-pool-present "a private-pool draw is imported and called (either spelling)"
else
    rgate 0 private-pool-present "imported=$rpb_imp called=$rpb_called --" \
        "the engine has no private DRBG draw in it"
fi

priv_ok=1
priv_detail=""
for fn in "${DRAW_SECRET[@]}"; do
    got=$(reach_draw "$fn" RAND_priv_bytes RAND_priv_bytes_ex)
    pub=$(reach_draw "$fn" RAND_bytes RAND_bytes_ex)
    priv_detail="$priv_detail $fn:priv=$got,pub=$pub"
    { [ "$got" = 1 ] && [ "$pub" = 0 ]; } || priv_ok=0
done
for fn in "${DRAW_HELPER[@]}"; do
    if [ "$(jhas "$AST" defines "^$fn\$")" != 1 ]; then
        priv_detail="$priv_detail $fn:inlined-away"
        continue
    fi
    got=$(reach_draw "$fn" RAND_priv_bytes RAND_priv_bytes_ex)
    pub=$(reach_draw "$fn" RAND_bytes RAND_bytes_ex)
    priv_detail="$priv_detail $fn:priv=$got,pub=$pub"
    { [ "$got" = 1 ] && [ "$pub" = 0 ]; } || priv_ok=0
done
for fn in "${DRAW_DELEGATE[@]}"; do
    via=$(direct "$fn" GetRandomPrivate)
    own=$(reach_draw "$fn" RAND_priv_bytes RAND_priv_bytes_ex)
    pub=$(reach_draw "$fn" RAND_bytes RAND_bytes_ex)
    priv_detail="$priv_detail $fn:via=$via,priv=$own,pub=$pub"
    { [ "$pub" = 0 ] && { [ "$via" = 1 ] || [ "$own" = 1 ]; }; } || priv_ok=0
done
pub_detail=""
for fn in "${DRAW_PUBLIC[@]}"; do
    keep=$(reach_draw "$fn" RAND_bytes RAND_bytes_ex)
    leak=$(reach_draw "$fn" RAND_priv_bytes RAND_priv_bytes_ex)
    pub_detail="$pub_detail $fn:pub=$keep,priv=$leak"
    { [ "$keep" = 1 ] && [ "$leak" = 0 ]; } || priv_ok=0
done

if [ "$ast_ok" != 1 ]; then
    rgate 0 pools-separated "the AST scan is not trustworthy on this tree"
elif [ "$priv_ok" = 1 ]; then
    rgate 1 pools-separated "secret draws private, public draws public --" \
        "$priv_detail --$pub_detail"
else
    rgate 0 pools-separated "the two pools are not separated --" \
        "$priv_detail --$pub_detail"
fi

b2b_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^BN_bn2bin$')
b2b_call=$(jhas "$AST" called '^BN_bn2bin$')
pad_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^BN_bn2binpad$')
pad_call=$(jhas "$AST" called '^BN_bn2binpad$')
if [ "$ast_ok" != 1 ]; then
    rgate 0 padded-conversion "the AST scan is not trustworthy on this tree"
elif [ "$b2b_imp" = 0 ] && [ "$b2b_call" = 0 ] \
        && [ "$pad_imp" = 1 ] && [ "$pad_call" = 1 ]; then
    rgate 1 padded-conversion "BN_bn2binpad has replaced BN_bn2bin everywhere"
else
    rgate 0 padded-conversion "BN_bn2bin imported=$b2b_imp called=$b2b_call," \
        "BN_bn2binpad imported=$pad_imp called=$pad_call"
fi

cl1=$(reach SchnorrEcc OPENSSL_cleanse)
cl2=$(reach SignSM2 OPENSSL_cleanse)
cl_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^OPENSSL_cleanse$')
if [ "$ast_ok" != 1 ]; then
    rgate 0 secret-bytes-cleansed "the AST scan is not trustworthy on this tree"
elif [ "$cl_imp" = 1 ] && [ "$cl1" = 1 ] && [ "$cl2" = 1 ]; then
    rgate 1 secret-bytes-cleansed "both signature nonces are erased before return"
else
    rgate 0 secret-bytes-cleansed "imported=$cl_imp," \
        "SchnorrEcc=$cl1 SignSM2=$cl2"
fi

ct_old=$(reach RSADP _math__ModExp)
ct=$(reach RSADP BN_set_flags)
ct_l=$(reach RSADP BN_mod_exp_mont_consttime)
ct_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^BN_set_flags$')
ct_l_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^BN_mod_exp_mont_consttime$')
ct_call=$(jhas "$AST" called '^BN_set_flags$')
ct_l_call=$(jhas "$AST" called '^BN_mod_exp_mont_consttime$')
ct_pub=$(direct _math__ModExp BN_set_flags)
ct_pub_l=$(direct _math__ModExp BN_mod_exp_mont_consttime)

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
    rgate 0 consttime-private-exp "the AST scan is not trustworthy on this tree"
elif [ "$ct_pub" = 1 ] || [ "$ct_pub_l" = 1 ]; then
    rgate 0 consttime-private-exp "_math__ModExp takes the constant-time path" \
        "itself (flag=$ct_pub ladder=$ct_pub_l)"
elif [ "$route" != 0 ]; then
    rgate 1 consttime-private-exp "the decryption path takes the constant-time" \
        "exponentiation by the $route, $how -- reached: flag=$ct" \
        "ladder=$ct_l, through-_math__ModExp=$ct_old"
else
    rgate 0 consttime-private-exp "BN_set_flags imported=$ct_imp" \
        "called=$ct_call reached=$ct; BN_mod_exp_mont_consttime" \
        "imported=$ct_l_imp called=$ct_l_call reached=$ct_l;" \
        "RSADP->_math__ModExp=$ct_old"
fi

# --- `ecdsa-sign-library` and `ecdsa-verify-library` are retired here.
#
# They are the only two gates on this ladder that any step retires rather than
# narrows, and the reason is that they are not narrowable: their entire content
# is that SignEcdsa reaches ECDSA_do_sign and ValidateSignatureEcdsa reaches
# ECDSA_do_verify.  Both of those are OSSL_DEPRECATEDIN_3_0, and this step is
# the one that takes them out.  Re-asking either here would be requiring the
# tree to keep the thing the instruction requires it to remove -- trap 9 in its
# sharpest form, an inherited test that a later step's own requirement makes
# unpassable.
#
# Nothing they asked is dropped.  Step 05's `ecdsa-through-evp` carries all
# three of their questions forward in the 3.0 form: the library does the
# signing, the port does not do the modular inversion, the port does not do the
# point multiplication.  What changes is which library call answers the first
# one.  NOTES-authoring.md section 9.0.2 has the full accounting.

CHECK_OWNER=03

# ================================== step 03's gates, re-asked (part of 0.10)
#
# Verbatim from 03-providers-and-fetch.test.sh, `gate` changed to `rgate` and
# nothing else.  The thresholds come with them unchanged -- OPS_FLOOR is 256
# against a measured 1536 on this step's probe, which drives step 03's hash
# workload in full beside its own.  That is the point of the merged probe:
# six of these seven gates read run-time counters, and a step that re-asked
# them from the scans alone would be asking a weaker question than the step
# that introduced them.

# --- 1: the engine owns a library context.
#
# OSSL_LIB_CTX_new imported, and reached from the engine's own startup.  The
# import alone is a symbol some translation unit needs; the reach is where it
# was decided.  _cpri__InitCryptoUnits is the function the instruction names as
# the place the engine takes the library up, and it is where step 01 already
# requires OPENSSL_init_crypto to be reached from, so a candidate is not being
# asked to guess which of the engine's entry points counts as startup.
# The probe adds the half the scan cannot reach: a context was actually
# constructed while _cpri__InitCryptoUnits was on the stack.  A call site the
# AST can see is a call site; a count taken during startup is the context.
ctx_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^OSSL_LIB_CTX_new$')
ctx_new=$(reach _cpri__InitCryptoUnits OSSL_LIB_CTX_new)
p_ctx_init=$(pnum ctx_new_at_init)
if [ "$ast_ok" != 1 ]; then
    rgate 0 libctx-owned "the AST scan is not trustworthy on this tree" \
        "($ast_parsed TUs, $ast_errors errors)"
elif [ "$probe_ok" != 1 ]; then
    rgate 0 libctx-owned "the run-time probe did not complete on this tree," \
        "so no run-time evidence about the context exists"
elif [ "$ctx_imp" = 1 ] && [ "$ctx_new" = 1 ] && [ "$p_ctx_init" -ge 1 ]; then
    rgate 1 libctx-owned "_cpri__InitCryptoUnits creates a library context of" \
        "the engine's own ($p_ctx_init created during startup)"
else
    rgate 0 libctx-owned "OSSL_LIB_CTX_new imported=$ctx_imp" \
        "reached-from-init=$ctx_new created-during-startup=$p_ctx_init --" \
        "this engine still takes whatever default context the process" \
        "happens to have"
fi

# --- 2: and gives it back.
#
# A library context is an object with a lifetime, and the half of that which a
# port forgets is the end of it.  _cpri__StopCryptoUnits is the engine's own
# shutdown entry point -- CryptStopUnits() calls it and nothing else does --
# and it is `{ return; }` in the baseline, so a tree that frees anything there
# has decided to, rather than inherited it.
# And the probe closes it: the free happened during shutdown, its argument was
# a context this engine had created, and nothing the engine created is still
# alive when the process is done with it.  "Frees something" and "frees the
# context it made" are different claims and only the second one is this.
ctxf_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^OSSL_LIB_CTX_free$')
ctx_free=$(reach _cpri__StopCryptoUnits OSSL_LIB_CTX_free)
p_ctx_stop=$(pnum ctx_free_at_stop)
p_ctx_known=$(pnum ctx_free_known)
p_ctx_live=$(pnum ctx_live)
if [ "$ast_ok" != 1 ]; then
    rgate 0 libctx-released "the AST scan is not trustworthy on this tree"
elif [ "$probe_ok" != 1 ]; then
    rgate 0 libctx-released "the run-time probe did not complete on this tree"
elif [ "$ctxf_imp" = 1 ] && [ "$ctx_free" = 1 ] \
        && [ "$p_ctx_stop" -ge 1 ] && [ "$p_ctx_known" -ge 1 ] \
        && [ "$p_ctx_live" = 0 ]; then
    rgate 1 libctx-released "_cpri__StopCryptoUnits frees the library context" \
        "it made ($p_ctx_stop freed at shutdown, none left live)"
else
    rgate 0 libctx-released "OSSL_LIB_CTX_free imported=$ctxf_imp" \
        "reached-from-stop=$ctx_free freed-at-shutdown=$p_ctx_stop" \
        "of-ones-the-engine-made=$p_ctx_known still-live=$p_ctx_live --" \
        "the context the engine created is never given back"
fi

# --- 3: the provider is loaded by name, and unloaded.
#
# One gate for both ends, unlike the context, because a provider is loaded INTO
# a context and the pair is one decision: an engine that loads the default
# provider explicitly and never unloads it has not got a different design, it
# has got half of this one written down.  The context is two gates because
# "created but never released" is a distinct and common shape that is worth
# naming in the log on its own.
#
# What this gate is not asking is which provider.  "default" is what the golden
# loads and what the instruction names as the one this step needs; a port that
# also loads others has not failed anything here, and step 06 is where the
# provider list stops being a formality.
#
# What it does ask, and only the probe can, is WHICH context the provider went
# into.  "A provider is loaded into a context, so this and the context are one
# decision" is the instruction's sentence, and a load with a NULL context
# activates the default provider in the process default -- which is where it
# already was.  That tree has written the call and not made the decision.
#
# Two spellings, one operation.  3.0 publishes OSSL_PROVIDER_load and
# OSSL_PROVIDER_try_load, and libcrypto implements the first as `return
# OSSL_PROVIDER_try_load(libctx, name, 0)` (crypto/provider.c).  A port that
# loads the default provider into its own context with try_load has done
# exactly what the instruction asks -- "loaded into it, by name, at startup" --
# and pinning the gate to one of the two spellings would fail it for the
# spelling rather than for the decision.  Both are accepted everywhere the gate
# looks: the import, the reach from startup, and the probe's counter.
#
# Counting both is safe and not double counting: libcrypto's own call from
# OSSL_PROVIDER_load into OSSL_PROVIDER_try_load binds inside libcrypto and
# does not come back out through the probe's interposer.  Measured, not
# assumed -- an -rdynamic program that interposes both and calls
# OSSL_PROVIDER_load once reads load=1 try=0, and calling try_load once reads
# load=0 try=1.
prov_imp=$(jhas "$LOG/elf-engine.json" openssl_imports \
                '^OSSL_PROVIDER_(try_)?load$')
prov_unimp=$(jhas "$LOG/elf-engine.json" openssl_imports '^OSSL_PROVIDER_unload$')
prov_load=$(reach _cpri__InitCryptoUnits OSSL_PROVIDER_load)
[ "$prov_load" = 1 ] \
    || prov_load=$(reach _cpri__InitCryptoUnits OSSL_PROVIDER_try_load)
# Unbounded, like the config load and the self test beside it: the
# instruction says Stop *reaches* the unload, and a second helper in between
# is still reaching it.  (Migration rubric review round 2, 2026-10-05.)
prov_unload=$(reach_any _cpri__StopCryptoUnits OSSL_PROVIDER_unload)
p_prov_eng=$(pnum prov_ctx_engine)
p_prov_null=$(pnum prov_ctx_null)
p_prov_other=$(pnum prov_ctx_other)
# The unload is asked of the run time as well, the way the context free is.
# "Written and reached from shutdown" and "ran" are different facts: a
# shutdown that reads `if (s_prov) OSSL_PROVIDER_unload(s_prov)` with s_prov
# never set is reached from _cpri__StopCryptoUnits, imports the symbol, and
# gives nothing back.  The context half of this step has been measured that
# way since the probe arrived; the provider half was not, and the instruction
# pairs the two in one sentence.
p_prov_unstop=$(pnum prov_unload_at_stop)
p_prov_unknown=$(pnum prov_unload_known)
p_prov_try=$(pnum prov_try_load)
# The probe watches which providers it saw loaded in a fixed table, and answers
# "one it loaded" out of it.  Past the end of that table the answer becomes
# yes-for-everything, which is the honest reading of "stopped watching" and the
# wrong reading for a gate whose requirement is positive: the tautology passes.
# Overflow is therefore a failure of the instrument and is reported as one.
p_prov_over=$(pnum prov_over)
if [ "$ast_ok" != 1 ]; then
    rgate 0 provider-explicit "the AST scan is not trustworthy on this tree"
elif [ "$probe_ok" != 1 ]; then
    rgate 0 provider-explicit "the run-time probe did not complete on this tree"
elif [ "$p_prov_over" != 0 ]; then
    rgate 0 provider-explicit "the probe watched more distinct providers than" \
        "its table holds ($(pnum prov_distinct)), so \"one it loaded\" is" \
        "true of every provider and this gate cannot be measured on this tree"
elif [ "$prov_imp" = 1 ] && [ "$prov_unimp" = 1 ] \
        && [ "$prov_load" = 1 ] && [ "$prov_unload" = 1 ] \
        && [ "$p_prov_eng" -ge 1 ] && [ "$p_prov_null" = 0 ] \
        && [ "$p_prov_unstop" -ge 1 ] && [ "$p_prov_unknown" -ge 1 ]; then
    rgate 1 provider-explicit "the engine loads a provider into its own" \
        "context at startup ($p_prov_eng load(s), $p_prov_try of them" \
        "spelled try_load, none into the process default) and gives it back" \
        "at shutdown ($p_prov_unstop unload(s) there, $p_prov_unknown of one" \
        "it loaded)"
else
    rgate 0 provider-explicit "OSSL_PROVIDER_load or _try_load" \
        "imported=$prov_imp reached-from-init=$prov_load, OSSL_PROVIDER_unload" \
        "imported=$prov_unimp reached-from-stop=$prov_unload;" \
        "loads into the engine's context=$p_prov_eng into the process" \
        "default=$p_prov_null into some other=$p_prov_other;" \
        "unloads at shutdown=$p_prov_unstop, of ones the engine" \
        "loaded=$p_prov_unknown"
fi

# --- 4: the digests come from a fetch, by name.
#
# The presence of EVP_MD_fetch and the absence of every way of getting a digest
# that does not go through a provider.  Both halves, because either alone is
# passable without doing the step: a tree can fetch one algorithm and keep
# EVP_sha256() for the rest, and a tree can delete the getters by moving the
# switch somewhere else.
#
# EVP_get_digestbyname and EVP_get_digestbynid are on the negative list beside
# the five constructors for the same reason they exist: they are the by-name
# lookup that predates fetching, they return a built-in EVP_MD from the legacy
# table rather than anything a provider supplied, and a port that swapped the
# switch for one of them would have written code that LOOKS like this step and
# is not it.
#
# The third half, which is the one the scan cannot see at all: the context the
# fetch was made against.  "obtained by fetching it by name from the engine's
# library context" is one sentence, and a tree that creates a context, loads a
# provider into it, and then calls EVP_MD_fetch(NULL, "SHA256", NULL) satisfies
# every presence check on this page while taking its digests from the process
# default, provided by whatever loaded itself there.  The probe compares the
# pointer against the ones it watched OSSL_LIB_CTX_new hand the engine, so this
# is identity and not spelling.
fetch_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^EVP_MD_fetch$')
fetch_call=$(jhas "$AST" called '^EVP_MD_fetch$')
p_fetch=$(pnum fetch)
p_fetch_eng=$(pnum fetch_ctx_engine)
p_fetch_null=$(pnum fetch_ctx_null)
p_fetch_other=$(pnum fetch_ctx_other)
#
# And the same identity question asked at the other end.  "Every digest the
# engine hashes with" is a claim about the EVP_MD * handed to a hashing call,
# not about how many fetches happened and against what: a tree can fetch three
# digests from its own context at startup, hold them, free them at shutdown,
# and hash with an EVP_MD it built itself, and every count above still reads
# correct.  So the probe interposes the four EVP calls that take a digest and
# start hashing with it, and reports how many of them were handed a pointer
# some EVP_MD_fetch had returned.  A re-init that passes NULL re-runs the
# digest the context already holds and names no new one, so it is counted
# apart and is not evidence either way.
p_init=$(pnum init)
p_init_known=$(pnum init_known)
p_init_null=$(pnum init_null)
p_init_stray=$(( p_init - p_init_known ))
#
# Both halves of "gone from the sources the build compiles and from what the
# built engine asks the loader for", and they are not the same question.  A
# getter called on a path the optimiser proved dead is written and not
# imported; a getter in a translation unit the AST pass could not parse is
# imported and not seen.  Asking only the object leaves the first unscored,
# which is the half a candidate can arrange on purpose -- so the name is on
# the CALLS list above and the answer comes back in `called`.
builtin_found=""
builtin_src=""
for sym in EVP_sha1 EVP_sha224 EVP_sha256 EVP_sha384 EVP_sha512 EVP_sm3 \
           EVP_get_digestbyname EVP_get_digestbynid; do
    if [ "$(jhas "$LOG/elf-engine.json" openssl_imports "^$sym\$")" = 1 ]; then
        builtin_found="$builtin_found $sym"
    fi
    if [ "$ast_ok" = 1 ] && [ "$(jhas "$AST" called "^$sym\$")" = 1 ]; then
        builtin_src="$builtin_src $sym"
    fi
done
# The third half of "gone from the sources": the name written down, whether or
# not it is called and whether or not it is imported.  See TEXTS above.
BUILTIN_TEXT='^(EVP_sha(1|224|256|384|512)|EVP_sm3|EVP_get_digestby(name|nid))$'
builtin_text=""
if [ "$ast_ok" = 1 ]; then
    builtin_text=$(jlist "$AST" text_names "$BUILTIN_TEXT")
    dl_text=$(jlist "$AST" text_names '^dl(sym|vsym|open)$')
else
    dl_text=""
fi
p_md_over=$(pnum md_over)
if [ "$ast_ok" != 1 ]; then
    rgate 0 digests-by-name "the AST scan is not trustworthy on this tree"
elif [ "$probe_ok" != 1 ]; then
    rgate 0 digests-by-name "the run-time probe did not complete on this tree," \
        "so which context the fetches were made against is unknown"
elif [ "$p_md_over" != 0 ]; then
    rgate 0 digests-by-name "the probe watched more distinct digests than its" \
        "table holds ($(pnum md_distinct)), so \"hashed with a digest it" \
        "fetched\" is true of every digest and the data-path half of this" \
        "gate cannot be measured on this tree"
elif [ "$fetch_imp" = 1 ] && [ "$fetch_call" = 1 ] \
        && [ -z "$builtin_found" ] && [ -z "$builtin_src" ] \
        && [ -z "$builtin_text" ] && [ -z "$dl_text" ] \
        && [ "$p_fetch_eng" -ge 1 ] && [ "$p_fetch_null" = 0 ] \
        && [ "$p_fetch_other" = 0 ] && [ "$p_init_stray" = 0 ]; then
    rgate 1 digests-by-name "the engine fetches its digests from its own" \
        "library context ($p_fetch_eng of $p_fetch fetches, none against the" \
        "process default), hashes with nothing else ($p_init_known of" \
        "$p_init hashing calls named a digest it had fetched, $p_init_null" \
        "re-ran the context's own), neither calls nor imports any of the" \
        "built-in constructors, and does not write their names down or ask" \
        "the loader for anything"
else
    rgate 0 digests-by-name "EVP_MD_fetch imported=$fetch_imp" \
        "called=$fetch_call; still imported:${builtin_found:- none};" \
        "still called:${builtin_src:- none};" \
        "still in the sources:${builtin_text:- none};" \
        "loader lookups:${dl_text:- none};" \
        "fetches against the engine's context=$p_fetch_eng against the" \
        "process default=$p_fetch_null against some other=$p_fetch_other;" \
        "hashing calls handed a digest this engine never fetched=$p_init_stray" \
        "(of $p_init that named one)"
fi

# --- 5: what was fetched is held, not taken and given back per operation.
#
# A fetched EVP_MD is reference counted.  Whether a port caches it eagerly in
# the hash service's startup or lazily in the lookup its three call sites
# already go through, it is holding one reference for many operations and gives
# it back once.  A port that fetches per operation has to free per operation --
# or leak, which is the same mistake with the evidence removed.
#
# So the question is asked on the returning side rather than the taking side:
# EVP_MD_free has to be called somewhere, and must not be reached from any of
# the nine entry points that do hash work on a caller's behalf.  Asking instead
# that _cpri__StartHash not reach EVP_MD_fetch would fail the lazy-cache shape,
# which is correct and is one hop below StartHash through GetHashServer; asking
# the nine not to reach the free is true of both correct shapes and false of
# every per-operation one.
#
# Where the free IS reached from is logged rather than required.  The golden
# frees from _cpri__StopCryptoUnits, which is the obvious place and the one the
# instruction names, but a port that frees from a re-fetch path in its own
# startup has still held what it fetched, and a gate that insisted on the
# shutdown site would be putting a shape on the port beyond the one asked for.
# On its own, though, that negative is passable without doing the step, and
# the AST cannot see how: a tree whose lookup fetches on every call and never
# frees anything reaches EVP_MD_free from nowhere at all, so put one
# EVP_MD_free(NULL) in the shutdown path and the two AST halves are both true
# while every operation has taken a reference and dropped it on the floor.
# "Once" is a rate, and a rate is the probe's question.
#
# It is asked as a difference, not a total.  The probe runs its workload twice:
# a warm-up that touches every algorithm through every hash entry point, then a
# long half that touches them all again.  A cache -- eager in startup or lazy
# in the lookup -- does all of its fetching in the warm-up; a fetch per
# operation does it in proportion to the work.  So the measurement is how many
# fetches happened in the long half, and a fixed overhead from anywhere,
# libcrypto's own internals included, cancels.
#
# FETCH_SLACK is not zero because "nothing at all in the long half" would put a
# shape on the port beyond the one asked for -- a port that keys its cache on
# something the probe varies would re-fetch a handful of times and still be
# holding one reference per digest for many operations.  It is small enough
# that the smallest per-operation shape, one fetch per completed sequence, is
# two orders of magnitude above it.
#
# OPS_FLOOR is trap 4 in its own clothes: a fetch count over an operation count
# nobody checked is a rate over an empty set, and the predecessor tree -- whose
# _cpri__StartHash fails at 3.0 and therefore performs no operations at all --
# is exactly the tree that would collect a free pass from it.
#
# "Every reference given back" is counted against the digests the probe watched
# come out of a fetch, not against every EVP_MD_free the tree makes.  A port
# that swapped the fetch for the legacy lookup frees a statically defined
# EVP_MD -- a documented no-op at 3.0 -- and counting that as a return would run
# the ledger negative and read as a leak in reverse.  That tree fails gate 4 for
# what it did; it is not this gate's business.
FETCH_SLACK=8
OPS_FLOOR=256
mdf_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^EVP_MD_free$')
mdf_call=$(jhas "$AST" called '^EVP_MD_free$')
mdf_stop=$(reach _cpri__StopCryptoUnits EVP_MD_free)
fetch_start=$(reach _cpri__HashStartup EVP_MD_fetch)
p_fetch_long=$(pnum fetch_long)
p_ops_long=$(pnum ops_long)
p_mdfree=$(pnum md_free)
p_mdlive=$(pnum md_live)
perop=""
for fn in "${HASH_WORK[@]}"; do
    if [ "$(reach "$fn" EVP_MD_free)" = 1 ]; then
        perop="$perop $fn"
    fi
done
if [ "$ast_ok" != 1 ]; then
    rgate 0 digests-fetched-once "the AST scan is not trustworthy on this tree"
elif [ "$probe_ok" != 1 ]; then
    rgate 0 digests-fetched-once "the run-time probe did not complete on this" \
        "tree, so how often this engine fetches is unknown"
elif [ "$p_ops_long" -lt "$OPS_FLOOR" ]; then
    rgate 0 digests-fetched-once "the probe completed only $p_ops_long" \
        "operation(s) in its long half, against a floor of $OPS_FLOOR --" \
        "a fetch rate over that many operations is not a measurement"
elif [ "$mdf_imp" = 1 ] && [ "$mdf_call" = 1 ] && [ -z "$perop" ] \
        && [ "$p_fetch_long" -le "$FETCH_SLACK" ] \
        && [ "$p_mdfree" -ge 1 ] && [ "$p_mdlive" = 0 ]; then
    rgate 1 digests-fetched-once "what the engine fetched it holds --" \
        "$p_fetch_long fetch(es) across $p_ops_long operations, every" \
        "reference given back ($p_mdfree freed, $p_mdlive still held), and" \
        "none of the ${#HASH_WORK[@]} hash entry points reaches EVP_MD_free" \
        "(freed from _cpri__StopCryptoUnits=$mdf_stop, fetched from" \
        "_cpri__HashStartup=$fetch_start)"
else
    rgate 0 digests-fetched-once "EVP_MD_free imported=$mdf_imp" \
        "called=$mdf_call; reached from hash entry point(s):${perop:- none};" \
        "$p_fetch_long fetch(es) across $p_ops_long operations in the long" \
        "half against a slack of $FETCH_SLACK; references freed=$p_mdfree" \
        "still held=$p_mdlive -- a digest taken per operation, given back or" \
        "not, is what this gate refuses"
fi

# --- 6: the digest's private state is neither read nor reached around.
#
# Two halves, one gate; the note under THE SIX GATES above says why they cannot
# be two.  The first half is the 1.1.x mechanism: EVP_MD_CTX_md to get at the
# algorithm behind a context, EVP_MD_CTX_get0_md_data to get at its private
# bytes, EVP_MD_meth_get_app_datasize to find out how many of them there are.
# All three still link at 3.0 and all three have stopped meaning anything for a
# provided digest -- the second returns NULL and the third returns 0 -- so a
# tree that still calls them is a tree that still believes in a picture of the
# digest that the release no longer takes.
#
# The second half is the low-level API: SHA256_Init and its eleven relatives,
# whose contexts are ordinary structures with published layouts that memcpy
# correctly and survive being written to a context blob.  They are deprecated
# at 3.0, not removed, so they compile and link and produce exactly the right
# answers; a port that reaches for them passes every behaviour family in the
# corpus and has moved the engine off EVP rather than onto providers.  That is
# the one shape at this step that scores well for doing the opposite of the
# requirement, and this is where it is refused.
#
# EVP_MD_CTX_md_data is deliberately not on either list.  It is a macro at 3.0
# for EVP_MD_CTX_get0_md_data, which is, so the spelling that appears in the
# object file is the one already named.  Neither are EVP_MD_size,
# EVP_MD_CTX_size or their relatives: those are macros too, for the 3.0 getters
# that replaced them, and a tree that uses either spelling emits the same
# symbol.  A gate cannot tell them apart and does not pretend to.
# Two lists, and each is read out of BOTH the object and the sources, because
# the instruction says both: "neither the sources the build compiles nor the
# built engine's imports".  An import is what the loader will be asked for; a
# call in the sources is what was written.  They differ in both directions.  A
# call the optimiser folded away -- SHA256_Init on a buffer nothing reads, or a
# call under a condition the compiler proved false -- is written and not
# imported.  A symbol imported by a translation unit the AST pass could not
# parse is imported and not seen.  Asking the object alone would have left the
# first case unscored, which is the half a candidate can arrange on purpose.
#
# The low-level list is the anchored spelling of exactly what this release
# exports below EVP: SHA1, SHA224, SHA256, SHA384 and SHA512, each bare (the
# one-shot) and each with _Init, _Update, _Final or _Transform.  The bare names
# matter on their own -- SHA256(d, n, md) computes a correct digest in one call
# with no context to peek at, and a port that reached for it would pass every
# behaviour family in the corpus while having moved the engine off EVP rather
# than onto providers.  _Transform is the block compressor underneath them all,
# and a state kept as "a chaining value plus a buffer" driven by _Transform is
# the same evasion with more code.
LOWSHA='^SHA(1|224|256|384|512)(_(Init|Update|Final|Transform))?$'
PEEKS='^(EVP_MD_CTX_md|EVP_MD_CTX_get0_md_data|EVP_MD_meth_get_app_datasize)$'
peek_imp=$(jlist "$LOG/elf-engine.json" openssl_imports "$PEEKS")
low_imp=$(jlist "$LOG/elf-engine.json" openssl_imports "$LOWSHA")
# The text half, for the same reason gate 4 has one: these two bans are on an
# API appearing in the sources, not on a call site.  `LOWTEXT` is `LOWSHA`
# without the five bare one-shot spellings, which are also the digests' own
# names -- see TEXTS above -- and `dl_text` is what stands in for them.
LOWTEXT='^SHA(1|224|256|384|512)_(Init|Update|Final|Transform)$'
if [ "$ast_ok" = 1 ]; then
    peek_call=$(jlist "$AST" called "$PEEKS")
    low_call=$(jlist "$AST" called "$LOWSHA")
    peek_text=$(jlist "$AST" text_names "$PEEKS")
    low_text=$(jlist "$AST" text_names "$LOWTEXT")
else
    peek_call=""; low_call=""; peek_text=""; low_text=""
fi
if [ "$ast_ok" != 1 ]; then
    rgate 0 no-state-peek "the AST scan is not trustworthy on this tree, so" \
        "only the object could be read and the sources could not" \
        "(imports -- state accessors:${peek_imp:- none};" \
        "low-level digest functions:${low_imp:- none})"
elif [ -z "$peek_imp" ] && [ -z "$low_imp" ] \
        && [ -z "$peek_call" ] && [ -z "$low_call" ] \
        && [ -z "$peek_text" ] && [ -z "$low_text" ] && [ -z "$dl_text" ]; then
    rgate 1 no-state-peek "the engine neither reads a context's private state" \
        "nor computes its digests below EVP, in its sources, in its imports" \
        "or through the loader"
else
    rgate 0 no-state-peek "still present --" \
        "state accessors imported:${peek_imp:- none}" \
        "called:${peek_call:- none}" \
        "written:${peek_text:- none};" \
        "low-level digest functions imported:${low_imp:- none}" \
        "called:${low_call:- none}" \
        "written:${low_text:- none};" \
        "loader lookups:${dl_text:- none}"
fi

CHECK_OWNER=04
# ================================== step 04's gates, re-asked (part of 0.10)
#
# Verbatim from 04-evp-symmetric.test.sh, `gate` changed to `rgate` and nothing
# else -- the same conversion step 04 applied to step 03's seven, for the same
# reason.  All four read the merged probe's counters, and this step's probe
# still drives step 04's symmetric workload in full beside its own asymmetric
# one, so CIPH_FETCH_SLACK and the `symmetric-through-evp` floor arrive with
# the numbers they were calibrated against rather than with numbers a new
# workload happened to produce.

# --- 1: the ciphers come from a fetch, by name, and nothing else encrypts.
#
# Step 03's `digests-by-name`, asked about the other half of the engine, and
# built the same way because the same three things can go wrong.
#
# The presence of EVP_CIPHER_fetch and the absence of every way of getting a
# cipher that does not go through a provider -- EVP_aes_128_cbc() and its
# twenty-three siblings, and EVP_get_cipherbyname/bynid, which are the by-name
# lookup that predates fetching and hand back a built-in EVP_CIPHER from the
# legacy table.  A port that swapped CpriSym.c's switch for one of those has
# written code that looks like this step and is not it: the cipher it gets is
# not a provider's, so no configuration and no FIPS module can reach it, which
# is the whole reason the step exists.
#
# The context the fetch was made against, which no scan can see.  A tree that
# calls EVP_CIPHER_fetch(NULL, "AES-128-CBC", NULL) has fetched by name and is
# taking its ciphers from the process default.  The probe compares the pointer
# against the contexts it watched OSSL_LIB_CTX_new hand this engine, so this is
# identity and not spelling.
#
# And the same identity question at the other end.  "Encrypts with" is a claim
# about the EVP_CIPHER * handed to a cipher-initialising call, not about how
# many fetches happened: a tree can fetch ten ciphers from its own context at
# startup, hold them, free them at shutdown, and encrypt with EVP_aes_128_cbc()
# anyway, and every count on the fetching side still reads correct.  So the
# probe interposes the nine EVP calls that take a cipher and start using it,
# and reports how many were handed a pointer some EVP_CIPHER_fetch returned.
# An init that passes NULL for the cipher re-keys the context's own and names
# nothing new, so it moves no counter here and is evidence neither way.
#
# Those nine are every Init spelling this libcrypto publishes (evp.h:751-794)
# and every one this engine's interface can be ported onto.  They are not every
# function in it that hands a cipher to a context: EVP_OpenInit (evp.h:844),
# EVP_SealInit (:849) and BIO_set_cipher (:895) take an EVP_CIPHER * too, and
# reach the context by calling one of the nine from inside libcrypto, where the
# probe does not see it.  None of the three is interposed and none needs to be
# -- the first two take an EVP_PKEY and produce the symmetric key themselves,
# so neither can implement an entry point whose key arrives as caller-supplied
# bytes, BIO_set_cipher wants a BIO chain this engine's interface has nowhere
# to put, and neither the reference tree nor the golden tree calls or imports
# any of them.  A complete list is not what this gate wants anyway: it asks
# whether the cipher-initialising calls this engine made were handed something
# it had fetched, so a spelling nobody interposed can only cost a correct port
# a count it needs, and cannot let through a tree that encrypts with something
# else -- the cipher such a tree would have to come by is what the conjuncts
# above refuse, by import, by call, by name in the text and by loader lookup.
#
# FORWARD OBLIGATION, for whoever writes the first step whose libcrypto is 3.5
# -- on this ladder step 9, the opaque-symmetric-keys rung, and not one rung
# earlier.  3.5.4's evp.h publishes three cipher-init entry points 3.0.17 does
# not: EVP_CipherInit_SKEY (:823), which takes an EVP_SKEY handle where these
# take a byte pointer, and EVP_CipherPipelineEncryptInit (:833) and
# EVP_CipherPipelineDecryptInit (:838), which take a pipe count and an array of
# IVs.  The 3.1.8, 3.2.5 and 3.4.2 images of steps 6, 7 and 8 publish none of
# them, so there is nothing here for those steps to do.  This step's libcrypto
# publishes none of them either, so the probe deliberately interposes none --
# an interposer for a symbol nothing can call is evidence of nothing.  That is
# safe only for as long as the engine cannot reach them.  When the step arrives
# that can, the probe must record a cipher handed to each of the three the same
# way it records the nine, or this gate reads zero uses on a correct port and
# fails it for no fault of its own.
#
# At that step the gate's own sentence needs re-reading too, not just the
# probe's interposer list.  Under EVP_SKEY the key is no longer bytes the
# caller passes but an object it holds, obtained from
# EVP_SKEY_import(OSSL_LIB_CTX *libctx, const char *skeymgmtname, ...)
# (openssl-3.5.4/include/openssl/evp.h:2288), which takes a library context of
# its own -- so "fetched from the engine's own context" may have to be asked of
# the key object and not only of the EVP_CIPHER *.  The same note sits at the
# cipher-init interposers in the probe.
cfetch_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^EVP_CIPHER_fetch$')
cfetch_call=$(jhas "$AST" called '^EVP_CIPHER_fetch$')
p_cfetch=$(pnum ciph_fetch)
p_cfetch_eng=$(pnum ciph_ctx_engine)
p_cfetch_null=$(pnum ciph_ctx_null)
p_cfetch_other=$(pnum ciph_ctx_other)
p_cuse=$(pnum ciph_use)
p_cuse_known=$(pnum ciph_use_known)
p_cuse_stray=$(( p_cuse - p_cuse_known ))
p_ciph_over=$(pnum ciph_over)
# Both halves of "gone from the sources the build compiles and from what the
# built engine asks the loader for", for the same reason step 03 asks both: a
# constructor called on a path the optimiser proved dead is written and not
# imported, and one in a translation unit the AST pass could not parse is
# imported and not seen.
cbuiltin_found=""
cbuiltin_src=""
for sym in "${CIPHER_BUILTIN[@]}"; do
    if [ "$(jhas "$LOG/elf-engine.json" openssl_imports "^$sym\$")" = 1 ]; then
        cbuiltin_found="$cbuiltin_found $sym"
    fi
    if [ "$ast_ok" = 1 ] && [ "$(jhas "$AST" called "^$sym\$")" = 1 ]; then
        cbuiltin_src="$cbuiltin_src $sym"
    fi
done
# The third half: the name written down.  Unlike step 03's digest list there is
# no double life to work around here -- a fetched cipher is named "AES-128-CBC"
# with hyphens, so no correct tree writes any of these twenty-six tokens.
BUILTIN_CIPHER='^(EVP_aes_(128|192|256)_(ecb|cbc|cfb|cfb1|cfb8|cfb128|ofb|ctr)|EVP_get_cipherby(name|nid))$'
cbuiltin_text=""
if [ "$ast_ok" = 1 ]; then
    cbuiltin_text=$(jlist "$AST" text_names "$BUILTIN_CIPHER")
    cdl_text=$(jlist "$AST" text_names '^dl(sym|vsym|open)$')
else
    cdl_text=""
fi
if [ "$ast_ok" != 1 ]; then
    rgate 0 ciphers-by-name "the AST scan is not trustworthy on this tree"
elif [ "$probe_ok" != 1 ]; then
    rgate 0 ciphers-by-name "the run-time probe did not complete on this tree," \
        "so which context the fetches were made against is unknown"
elif [ "$p_ciph_over" != 0 ]; then
    rgate 0 ciphers-by-name "the probe watched more distinct ciphers than its" \
        "table holds ($(pnum ciph_distinct)), so \"encrypted with a cipher it" \
        "fetched\" is true of every cipher and the data-path half of this" \
        "gate cannot be measured on this tree"
elif [ "$cfetch_imp" = 1 ] && [ "$cfetch_call" = 1 ] \
        && [ -z "$cbuiltin_found" ] && [ -z "$cbuiltin_src" ] \
        && [ -z "$cbuiltin_text" ] && [ -z "$cdl_text" ] \
        && [ "$p_cfetch_eng" -ge 1 ] && [ "$p_cfetch_null" = 0 ] \
        && [ "$p_cfetch_other" = 0 ] \
        && [ "$p_cuse" -ge 1 ] && [ "$p_cuse_stray" = 0 ]; then
    rgate 1 ciphers-by-name "the engine fetches its ciphers from its own" \
        "library context ($p_cfetch_eng of $p_cfetch fetches, none against" \
        "the process default), encrypts with nothing else ($p_cuse_known of" \
        "$p_cuse cipher-initialising calls named a cipher it had fetched," \
        "$(pnum ciph_distinct) distinct), neither calls nor imports any of" \
        "the ${#CIPHER_BUILTIN[@]} built-in constructors, and does not write" \
        "their names down or ask the loader for anything"
else
    rgate 0 ciphers-by-name "EVP_CIPHER_fetch imported=$cfetch_imp" \
        "called=$cfetch_call; still imported:${cbuiltin_found:- none};" \
        "still called:${cbuiltin_src:- none};" \
        "still in the sources:${cbuiltin_text:- none};" \
        "loader lookups:${cdl_text:- none};" \
        "fetches against the engine's context=$p_cfetch_eng against the" \
        "process default=$p_cfetch_null against some other=$p_cfetch_other;" \
        "cipher-initialising calls handed a cipher this engine never" \
        "fetched=$p_cuse_stray (of $p_cuse that named one)"
fi

# --- 2: what was fetched is held.
#
# Step 03's `digests-fetched-once`, asked of ciphers, and every line of that
# gate's reasoning transfers: the question is asked on the returning side
# because asking it forwards would fail the lazy-cache shape, and the negative
# alone is passable by a tree that fetches per operation and leaks every
# reference, so the rate is measured too and it is measured as a difference
# between the probe's two halves rather than as a total.
#
# What is different is the count.  The digest side holds three; the cipher side
# holds up to fifteen -- three key sizes by five modes -- and a port may hold
# them eagerly in _cpri__SymStartup or lazily in the lookup its eight call
# sites go through.  CIPH_FETCH_SLACK is sized for the lazy shape: a cache
# keyed on (mode, key size) fills during the warm-up, which touches every
# combination the probe drives, and re-fetches nothing in the long half.  A
# port that keys its cache on something the probe varies per round would
# re-fetch a handful of times and is still holding one reference per cipher
# across many operations, which is what the sentence asks for.  A fetch per
# operation is 1152 in the long half, two orders of magnitude above the slack.
#
# SYM_OPS_FLOOR is trap 4: a fetch rate over an operation count nobody checked
# is a rate over an empty set.  Here the tree that would collect the free pass
# is not hypothetical -- it is step 03's own finished tree, which performs
# every symmetric operation through AES_encrypt and makes zero fetches, so
# every ratio on this page reads 0/0 on it unless the denominator is checked.
CIPH_FETCH_SLACK=8
SYM_OPS_FLOOR=256
cfree_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^EVP_CIPHER_free$')
cfree_call=$(jhas "$AST" called '^EVP_CIPHER_free$')
cfree_stop=$(reach _cpri__StopCryptoUnits EVP_CIPHER_free)
cfetch_start=$(reach _cpri__SymStartup EVP_CIPHER_fetch)
p_cfetch_long=$(pnum ciph_fetch_long)
p_sym_long=$(pnum sym_ops_long)
p_cfree=$(pnum ciph_free)
p_clive=$(pnum ciph_live)
cperop=""
for fn in "${SYM_WORK[@]}"; do
    if [ "$(reach "$fn" EVP_CIPHER_free)" = 1 ]; then
        cperop="$cperop $fn"
    fi
done
if [ "$ast_ok" != 1 ]; then
    rgate 0 ciphers-fetched-once "the AST scan is not trustworthy on this tree"
elif [ "$probe_ok" != 1 ]; then
    rgate 0 ciphers-fetched-once "the run-time probe did not complete on this" \
        "tree, so how often this engine fetches is unknown"
elif [ "$p_sym_long" -lt "$SYM_OPS_FLOOR" ]; then
    rgate 0 ciphers-fetched-once "the probe completed only $p_sym_long" \
        "symmetric operation(s) in its long half, against a floor of" \
        "$SYM_OPS_FLOOR -- a fetch rate over that many operations is not a" \
        "measurement"
elif [ "$cfree_imp" = 1 ] && [ "$cfree_call" = 1 ] && [ -z "$cperop" ] \
        && [ "$p_cfetch_long" -le "$CIPH_FETCH_SLACK" ] \
        && [ "$p_cfree" -ge 1 ] && [ "$p_clive" = 0 ]; then
    rgate 1 ciphers-fetched-once "what the engine fetched it holds --" \
        "$p_cfetch_long fetch(es) across $p_sym_long symmetric operations," \
        "every reference given back ($p_cfree freed, $p_clive still held)," \
        "and none of the ${#SYM_WORK[@]} symmetric entry points reaches" \
        "EVP_CIPHER_free (freed from _cpri__StopCryptoUnits=$cfree_stop," \
        "fetched from _cpri__SymStartup=$cfetch_start)"
else
    rgate 0 ciphers-fetched-once "EVP_CIPHER_free imported=$cfree_imp" \
        "called=$cfree_call; reached from symmetric entry" \
        "point(s):${cperop:- none}; $p_cfetch_long fetch(es) across" \
        "$p_sym_long operations in the long half against a slack of" \
        "$CIPH_FETCH_SLACK; references freed=$p_cfree still held=$p_clive" \
        "-- a cipher taken per operation, given back or not, is what this" \
        "gate refuses"
fi

# --- 3: the bytes go through EVP.
#
# The positive, and the only gate here that a partially ported tree fails.
#
# Everything above is about where the cipher came from, and all of it is true
# of a tree that fetches ten ciphers, holds them, frees them, and then encrypts
# with the AES_encrypt loop it already had.  Everything below is a ban, and a
# ban is satisfied by deletion: a tree that takes <openssl/aes.h> out and
# writes two hundred lines of its own AES in C passes gate 4 completely.  This
# gate is what makes both of those fail.
#
# It is asked at the one place an EVP cipher cannot avoid: the data call.
# Whatever shape a port takes -- a context per operation, a context per mode
# re-keyed with EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv), a one-shot -- the
# plaintext has to be handed to EVP_EncryptUpdate, EVP_DecryptUpdate,
# EVP_CipherUpdate or EVP_Cipher, and the probe counts all four as one number.
# Asking at the init instead would have failed the re-keying shape, which is
# correct: it names no cipher after the first time.
#
# The threshold is one data call per symmetric operation in the long half, and
# that is a floor rather than an equality because a port that feeds EVP a block
# at a time is doing the same thing more often.  The probe's long half drives
# every one of its symmetric operations with four blocks of data and no
# zero-length input, so a correct port cannot come in under it -- the baseline's
# own low-level counters confirm it from the other side, 1152 key schedules and
# 4608 block calls across 1152 operations.
#
# What the probe counts is a data call that carried bytes.  An update handed an
# empty input is not counted, because the question is where an operation's
# bytes went and a call with no bytes in it answers nothing: without that rule
# a tree could do every operation by hand and issue one empty update beside it,
# and this gate would read one data call per operation and pass.  The rule is
# free of consequences for a correct port precisely because of the sentence
# above -- the operations this count is measured against are the probe's own,
# and it never drives an empty one.  Zero-length symmetric operations do exist
# and are recorded, but they come from the corpus, which drives the engine
# through the driver and does not reach these counters at all.
p_evp_upd=$(pnum evp_update)
p_evp_upd_long=$(pnum evp_update_long)
p_low_setkey=$(pnum low_setkey)
p_low_block=$(pnum low_block)
if [ "$probe_ok" != 1 ]; then
    rgate 0 symmetric-through-evp "the run-time probe did not complete on this" \
        "tree, so where the engine's symmetric bytes went is unknown"
elif [ "$p_sym_long" -lt "$SYM_OPS_FLOOR" ]; then
    rgate 0 symmetric-through-evp "the probe completed only $p_sym_long" \
        "symmetric operation(s) in its long half, against a floor of" \
        "$SYM_OPS_FLOOR -- there is not enough work here to say where it went"
elif [ "$p_evp_upd_long" -ge "$p_sym_long" ]; then
    rgate 1 symmetric-through-evp "every symmetric operation the probe drove" \
        "moved its bytes through EVP -- $p_evp_upd_long data call(s) across" \
        "$p_sym_long operations in the long half ($p_evp_upd in total), and" \
        "the low-level API ran $p_low_setkey time(s) for a key schedule and" \
        "$p_low_block time(s) for a block"
else
    rgate 0 symmetric-through-evp "$p_evp_upd_long EVP data call(s) across" \
        "$p_sym_long symmetric operations in the probe's long half, so" \
        "$(( p_sym_long - p_evp_upd_long )) of them did their work somewhere" \
        "else; the low-level API ran $p_low_setkey time(s) for a key schedule" \
        "and $p_low_block time(s) for a block, which accounts for some or all" \
        "of it"
fi

# --- 4: the low-level API is gone.
#
# Three halves again -- imported, called, written down -- and the third is not
# redundant here, it is the only one that sees the thing this step actually has
# to remove.
#
# CpriSym.c's SM4 support is a typedef and four #defines aliasing
# SM4_set_encrypt_key and friends onto AES_set_encrypt_key and friends, and the
# functions that use them are inside `#ifdef TPM_ALG_SM4`.  Implementation.h
# says `#define ALG_SM4 ALG_NO`, so none of that compiles: no AES symbol is
# imported on its account, no CALL_EXPR exists for the AST to see, and the
# import half and the call half of this gate both come back clean on a tree
# that has not touched it.  The names are still written in the file.  astscan
# keeps non-taken preprocessor branches for exactly this, and the instruction
# says the shim goes with the block it serves, in those words.
#
# AES_KEY is on the text list and on neither of the other two.  It is a type,
# so it is not a relocation and not a call, and it is the object every one of
# the eight entry points puts on its stack -- a tree that still declares one
# has kept the API whatever it does with it.
#
# And the run-time half, which is none of those three.  A tree can satisfy
# every scan on this page by resolving AES_encrypt through the loader, or by
# reaching it from a translation unit the compile database does not list.  The
# probe interposes all four low-level entry points and counts them, so "the
# API does not run" is asked of the running engine and not only of its text.
low_imp=""
low_src=""
for sym in "${AES_LOWLEVEL[@]}"; do
    if [ "$(jhas "$LOG/elf-engine.json" openssl_imports "^$sym\$")" = 1 ]; then
        low_imp="$low_imp $sym"
    fi
    if [ "$ast_ok" = 1 ] && [ "$(jhas "$AST" called "^$sym\$")" = 1 ]; then
        low_src="$low_src $sym"
    fi
done
LOWSYM_TEXT='^(AES_KEY|AES_(set_(en|de)crypt_key|encrypt|decrypt|ecb_encrypt|cbc_encrypt|cfb(1|8|128)_encrypt|ofb128_encrypt|ctr128_encrypt|ige_encrypt|bi_ige_encrypt|wrap_key|unwrap_key|options))$'
low_text=""
if [ "$ast_ok" = 1 ]; then
    low_text=$(jlist "$AST" text_names "$LOWSYM_TEXT")
fi
if [ "$ast_ok" != 1 ]; then
    rgate 0 no-lowlevel-symmetric "the AST scan is not trustworthy on this" \
        "tree, so neither the call half nor the text half can be read"
elif [ "$probe_ok" != 1 ]; then
    rgate 0 no-lowlevel-symmetric "the run-time probe did not complete on this" \
        "tree, so whether the low-level API ran is unknown"
elif [ -z "$low_imp" ] && [ -z "$low_src" ] && [ -z "$low_text" ] \
        && [ -z "$cdl_text" ] \
        && [ "$p_low_setkey" = 0 ] && [ "$p_low_block" = 0 ]; then
    rgate 1 no-lowlevel-symmetric "the low-level symmetric API is gone --" \
        "none of the ${#AES_LOWLEVEL[@]} names is imported or called, none of" \
        "them and no AES_KEY appears in the sources the build compiles, the" \
        "loader is not asked for anything, and across the probe's" \
        "$(pnum sym_ops) symmetric operations it built 0 key schedules and" \
        "encrypted 0 blocks"
else
    rgate 0 no-lowlevel-symmetric "still imported:${low_imp:- none};" \
        "still called:${low_src:- none};" \
        "still in the sources:${low_text:- none};" \
        "loader lookups:${cdl_text:- none};" \
        "and at run time $p_low_setkey key schedule(s) and $p_low_block" \
        "block call(s) went through it"
fi

CHECK_OWNER=05
# ================================== step 05's gates, re-asked (part of 0.10)
#
# Verbatim from 05-evp-asymmetric.test.sh, `gate` changed to `rgate` and
# nothing else -- the same conversion step 05 applied to step 04's four, for
# the same reason.  All four read the merged probe's counters, and this step's
# probe drives step 05's asymmetric workload unchanged beside everything below
# it: no round is added and no phase is moved, so ECDSA_SIGN_FLOOR,
# ECDSA_VERIFY_FLOOR and the two RSA floors arrive with the numbers they were
# calibrated against.
#
# Two of the four are where this step can break them, and neither is an
# accident waiting to happen.  `asymmetric-keys-from-params` requires
# EVP_PKEY_fromdata against the engine's own context, and gate 1 below puts a
# second provider in that context.  On a tree that loads the module before the
# default and leaves its queries NULL, the key management resolves to the
# module, and the module refuses explicit curve parameters at fromdata itself
# -- not later, at use.  This rgate survives that anyway, because what it
# counts is the CALL and the context it was made against, both of which are
# unchanged when the call returns 0.  The refusal lands where it belongs: on
# the behaviour corpus, and on `ecdsa-through-evp`, which reads results.  That
# split is deliberate and it is what makes the property query cost something
# real without any gate reading its text.  (RSA is the other way round: its
# fromdata succeeds under the module and the refusal comes later, at
# EVP_PKEY_encrypt_init.  Both paths end on the corpus.  NOTES section 10.7.)

# --- 1: the keys are built from parameters, in the engine's own context.
#
# This is the paperwork gate for the object every other gate on this page
# depends on.  Both halves of the step -- RSA and ECDSA -- start at the same
# place: the engine is handed a TPM key structure full of BIGNUM-shaped byte
# strings and has to turn it into something EVP will operate on.  At 1.0.2 and
# at 3.0-with-deprecation-warnings-off there are two ways to do that, RSA_new
# plus RSA_set0_key and EC_KEY_new plus EC_KEY_set_group, and both produce an
# object no provider can see.  The 3.x way is EVP_PKEY_fromdata against an
# OSSL_PARAM list, and it is the only way that reaches a provider.
#
# WHAT THE SCAN CAN SEE
#
# That EVP_PKEY_fromdata and its _init are imported and called, that the
# OSSL_PARAM builder is used, and that every one of the four public-key entry
# points reaches the construction on its own account rather than one of them
# doing it for a test.
#
# And that every one of them reaches EVP_PKEY_free.  That question is asked
# FORWARDS here, which is the opposite of how steps 03 and 04 ask theirs, and
# the reason is in the note beside CALL_IN above: an EVP_MD is an algorithm and
# is fetched once for the life of the engine, an EVP_PKEY is a key and arrives
# in the caller's argument list on every call.  A tree where EVP_PKEY_free is
# not reachable from these four leaks a key object per TPM command.
#
# WHAT ONLY THE PROBE CAN SEE
#
# Which library context the construction was made against.  This is step 03's
# `digests-by-name` argument in a new place and it is not a formality:
# EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL) compiles, runs, produces a
# working key, and takes its key management from whatever loaded itself into
# the process default.  The engine made a context of its own at step 03 and
# loaded a provider into it; a key built outside that context is a key that
# step 06's FIPS work cannot reach.  So `fromdata_ctx_engine` must be the only
# non-zero one of the four.
#
# The corresponding question about EVP_PKEY_CTX_new_from_pkey is REPORTED and
# not gated, and the distinction is worth stating because it looks like an
# inconsistency.  new_from_pkey takes a key that already has a library context
# of its own and inherits it -- passing NULL there is correct, and is what the
# documentation shows -- so a gate on `pkctx_ctx_null` would fail a correct
# port.  new_from_name has no key to inherit from and NULL there means the
# process default.  Same-looking argument, opposite meaning, and the gate goes
# where the meaning is.  NOTES-authoring.md section 9.1.7 has the measurement.
#
# And the rate, which points the other way from steps 03 and 04.  There the
# sentence was "fetch once and hold", asked as a near-zero count in the long
# half.  Here the sentence is "build one per operation", asked as a count that
# SCALES with the long half, because the key is the caller's and there is
# nothing to hold.  The threshold is half the probe's own long-half operation
# count rather than a constant, so it cannot drift if the workload changes:
# what is being asserted is a ratio, and writing the denominator down is how
# trap 4 gets closed at the same time.
#
# The contexts are asked the same way and against the same denominator.  "A
# context belongs to the call that made it and does not outlive it" is a
# statement about a rate and not about a total: a tree that made one context,
# kept it in a function-static and freed it in _cpri__StopCryptoUnits has a
# clean ledger -- nothing live at the end, nothing leaked -- and has not done
# what the instruction asked.  The long half's free count against the long
# half's operation count is the difference between the two arrangements.  The
# ledger questions (pkctx_live, pkctx_over) stay exactly where they are
# because they answer the other half of the same sentence.
ASYM_OPS_FLOOR=256
fd_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^EVP_PKEY_fromdata$')
fd_call=$(jhas "$AST" called '^EVP_PKEY_fromdata$')
fdi_call=$(jhas "$AST" called '^EVP_PKEY_fromdata_init$')
fnm_call=$(jhas "$AST" called '^EVP_PKEY_CTX_new_from_name$')
# The parameter list, by either route.  OSSL_PARAM_BLD is one way to write one
# and a plain OSSL_PARAM array is the other; the instruction asks for a list,
# not for a spelling, so pinning the builder here would fail a correct tree
# that took the array.  The array is closed either by calling
# OSSL_PARAM_construct_end or by writing the OSSL_PARAM_END terminator, and
# the second of those is a compound literal with no call node anywhere in the
# AST -- it is how the fromdata example in the library's own documentation is
# written -- so the terminator is asked of the source text and the call is
# asked of the call list.  Each route then carries its own obligation: the
# builder allocates twice and has to give both back, the array allocates
# nothing.  The builder is asked FIRST and its two frees are unconditional, so
# a terminator sitting in some unrelated array cannot discharge them; half of
# the builder route is not a route at all, which is the reading that makes the
# two frees the instruction names mean something to a gate rather than only to
# a reader.
b2p_call=$(jhas "$AST" called '^OSSL_PARAM_BLD_to_param$')
bfree_call=$(jhas "$AST" called '^OSSL_PARAM_BLD_free$')
pfree_call=$(jhas "$AST" called '^OSSL_PARAM_free$')
pend_call=$(jhas "$AST" called '^OSSL_PARAM_construct_end$')
pend_text=$(jhas "$AST" text_names '^OSSL_PARAM_END$')
if [ "$b2p_call" = 1 ]; then
    if [ "$bfree_call" = 1 ] && [ "$pfree_call" = 1 ]; then
        bld_call=1
        bld_route="the builder, with OSSL_PARAM_BLD_free and OSSL_PARAM_free"
    else
        bld_call=0
        bld_route="the builder without all of its frees"
        bld_route="$bld_route (OSSL_PARAM_BLD_free=$bfree_call"
        bld_route="$bld_route OSSL_PARAM_free=$pfree_call)"
    fi
elif [ "$pend_call" = 1 ]; then
    bld_call=1
    bld_route="a constructed array, closed with OSSL_PARAM_construct_end"
elif [ "$pend_text" = 1 ]; then
    bld_call=1
    bld_route="a constructed array, closed with OSSL_PARAM_END"
else
    bld_call=0
    bld_route="neither route (OSSL_PARAM_BLD_to_param=0"
    bld_route="$bld_route OSSL_PARAM_construct_end=0 OSSL_PARAM_END=0)"
fi
p_fd=$(pnum fromdata)
p_fd_long=$(pnum fromdata_long)
p_fd_eng=$(pnum fromdata_ctx_engine)
p_fd_null=$(pnum fromdata_ctx_null)
p_fd_other=$(pnum fromdata_ctx_other)
p_fd_unk=$(pnum fromdata_ctx_unknown)
p_pkey_free=$(pnum pkey_free)
p_asym_long=$(pnum asym_ops_long)
p_pkctx_over=$(pnum pkctx_over)
p_pkctx_live=$(pnum pkctx_live)
p_pkctx_free_long=$(pnum pkctx_free_long)
nofrom=""
nofree=""
for fn in "${ASYM_PUBLIC[@]}"; do
    [ "$(reach_any "$fn" EVP_PKEY_fromdata)" = 1 ] || nofrom="$nofrom $fn"
    [ "$(reach_any "$fn" EVP_PKEY_free)" = 1 ]     || nofree="$nofree $fn"
done
if [ "$ast_ok" != 1 ]; then
    rgate 0 asymmetric-keys-from-params "the AST scan is not trustworthy on" \
        "this tree, so where the key objects come from cannot be read"
elif [ "$probe_ok" != 1 ]; then
    rgate 0 asymmetric-keys-from-params "the run-time probe did not complete" \
        "on this tree, so the context the keys were built against and how" \
        "often they were built are both unknown"
elif [ "$p_asym_long" -lt "$ASYM_OPS_FLOOR" ]; then
    rgate 0 asymmetric-keys-from-params "the probe completed only" \
        "$p_asym_long asymmetric operation(s) in its long half, against a" \
        "floor of $ASYM_OPS_FLOOR -- a construction rate over that many" \
        "operations is not a measurement"
elif [ "$fd_imp" = 1 ] && [ "$fd_call" = 1 ] && [ "$fdi_call" = 1 ] \
        && [ "$fnm_call" = 1 ] && [ "$bld_call" = 1 ] \
        && [ -z "$nofrom" ] && [ -z "$nofree" ] \
        && [ "$p_fd" -ge 1 ] && [ "$p_fd_eng" -ge 1 ] \
        && [ "$p_fd_null" = 0 ] && [ "$p_fd_other" = 0 ] \
        && [ "$p_fd_unk" = 0 ] \
        && [ $(( p_fd_long * 2 )) -ge "$p_asym_long" ] \
        && [ "$p_pkey_free" -ge "$p_fd" ] \
        && [ "$p_pkctx_over" = 0 ] && [ "$p_pkctx_live" = 0 ] \
        && [ $(( p_pkctx_free_long * 2 )) -ge "$p_asym_long" ]; then
    rgate 1 asymmetric-keys-from-params "every key EVP operates on was built" \
        "from an OSSL_PARAM list, made by $bld_route --" \
        "EVP_PKEY_fromdata imported and called," \
        "reached from all ${#ASYM_PUBLIC[@]} public-key entry points along" \
        "with EVP_PKEY_free, $p_fd construction(s) at run time and all of" \
        "them against a library context this engine made ($p_fd_eng engine," \
        "$p_fd_null process-default, $p_fd_other other, $p_fd_unk" \
        "unattributable), $p_fd_long of them across the long half's" \
        "$p_asym_long operations, $p_pkey_free key(s) given back," \
        "$p_pkctx_free_long context(s) freed across the same half and none" \
        "left live"
else
    rgate 0 asymmetric-keys-from-params "EVP_PKEY_fromdata imported=$fd_imp" \
        "called=$fd_call, _init called=$fdi_call," \
        "EVP_PKEY_CTX_new_from_name called=$fnm_call;" \
        "the parameter list came from $bld_route; entry point(s) that do" \
        "not reach the construction:${nofrom:- none}; that do not reach" \
        "EVP_PKEY_free:${nofree:- none}; at run time $p_fd construction(s)" \
        "(engine context $p_fd_eng, process default $p_fd_null, other" \
        "$p_fd_other, unattributable $p_fd_unk), $p_fd_long in the long" \
        "half against $p_asym_long operations, $p_pkey_free key(s) freed," \
        "$p_pkctx_free_long context(s) freed in the long half," \
        "$p_pkctx_live context(s) still live (table overflowed" \
        "$p_pkctx_over)"
fi

# --- 2: the hand-rolled padding is gone, and only the half that should be.
#
# Four of CpriRSA.c's eight padding routines are the public-key half of a
# scheme and go; four are the private-key half and stay.  This gate asserts
# both directions, and the second direction is the whole point of it.
#
# A gate that only said "OaepEncode, RSAES_PKSC1v1_5Encode, PssDecode and
# RSASSA_Decode are gone" is passed by a tree that deleted all eight -- which
# is not this migration, it is a tree that moved the private operations to EVP
# as well and in doing so deleted the RSADP that step 02's `rsa-consttime` is
# scored on.  That tree would collect this gate, fail step 02's re-asked one,
# and the trajectory would show a puzzle instead of a reason.  Asserting the
# four that stay turns it into a sentence about which half moved.
#
# The question is asked of `defines` rather than of reachability, because the
# claim is about the tree and not about a path through it: a port that left
# OaepEncode compiled but unreachable has left a hand-rolled OAEP encoder in
# the engine, and the next person to need one will find it.  astscan reports
# `defines` only for names some --call-in pair mentions, which is why every one
# of the eight is on that list beside the entry point it belongs to.
#
# Nothing here reads the probe.  Padding that is not compiled cannot run, and
# padding that is compiled and not called is still there; both are questions
# about the source, and a run-time counter would answer neither.
gone_still=""
stays_gone=""
stays_unreached=""
for fn in "${PAD_GONE[@]}"; do
    [ "$(jhas "$AST" defines "^$fn\$")" = 0 ] || gone_still="$gone_still $fn"
done
for fn in "${PAD_STAYS[@]}"; do
    [ "$(jhas "$AST" defines "^$fn\$")" = 1 ] || stays_gone="$stays_gone $fn"
done
for pair in _cpri__DecryptRSA:OaepDecode _cpri__DecryptRSA:RSAES_Decode \
            _cpri__SignRSA:PssEncode _cpri__SignRSA:RSASSA_Encode; do
    if [ "$(reach_any "${pair%%:*}" "${pair##*:}")" != 1 ]; then
        stays_unreached="$stays_unreached $pair"
    fi
done
if [ "$ast_ok" != 1 ]; then
    rgate 0 no-handrolled-padding "the AST scan is not trustworthy on this" \
        "tree, so which padding routines it still compiles cannot be read"
elif [ -z "$gone_still" ] && [ -z "$stays_gone" ] \
        && [ -z "$stays_unreached" ]; then
    rgate 1 no-handrolled-padding "the public-key half of the padding is" \
        "gone and the private-key half is not -- none of the" \
        "${#PAD_GONE[@]} encoders this step replaces is still compiled," \
        "all ${#PAD_STAYS[@]} that belong to the private side still are," \
        "and each is still reached from the entry point that owns it"
else
    rgate 0 no-handrolled-padding "still compiled and should not" \
        "be:${gone_still:- none}; no longer compiled and should" \
        "be:${stays_gone:- none}; no longer reached from the private-key" \
        "entry point that owns it:${stays_unreached:- none} -- this step" \
        "moves the public-key half of RSA and leaves the private half where" \
        "step 02 put it"
fi

# --- 3: RSA's public operations go through EVP, and its private ones do not.
#
# The first of the two substance gates.  Everything in gate 1 is true of a tree
# that builds a beautiful EVP_PKEY, frees it correctly, and then runs the bytes
# through the OaepEncode it already had; everything in gate 2 is a ban, and a
# ban is satisfied by deletion.  This gate is what a partially ported tree
# fails.
#
# It is asked at three places and each one answers something the others do not.
#
# The call graph, forwards: _cpri__EncryptRSA reaches EVP_PKEY_encrypt and
# _cpri__ValidateSignatureRSA reaches EVP_PKEY_verify.  That is where the two
# public operations have to end up.
#
# The call graph, backwards, at _cpri__MGF1.  This is the sharpest instrument
# on the page and it is worth explaining because it looks odd.  _cpri__MGF1 is
# the tree's own mask generation function.  It does not go anywhere at this
# step -- OaepDecode and PssEncode still drive it, so a ban on the tree having
# one would be a ban on the private half of OAEP and PSS.  What moves is who
# can reach it: after this step neither public-key entry point can, because the
# only routes were through the four encoders gate 2 deleted.  So "is the
# hand-rolled padding still in the path" is asked as a question about two
# specific paths rather than about the tree, and a port that deleted the
# encoders but kept a private copy of the mask loop inline fails it anyway.
#
# The call graph at RSADP, forwards, from the two entry points that keep it.
# Step 02 requires RSADP to set BN_FLG_CONSTTIME and go through
# BN_mod_exp_mont_consttime; that gate is re-asked above and would still pass
# on a tree where nothing reaches RSADP any more, because it asks about RSADP's
# own body.  This is trap 9 in the shape where the earlier gate stays green
# while the thing it protects stops happening.  Naming the two callers here is
# what keeps the private operation in the tree that step 02 measured.
#
# The import table and the source text, at <openssl/rsa.h>.  RSA_new and
# RSA_public_encrypt(..., RSA_PKCS1_OAEP_PADDING) are the 1.0.2 way of doing
# exactly what this step asks for -- the library's padding, not hand-rolled,
# and invisible to every provider.  Both lists read zero on both calibrations,
# so this clause can never be the reason a gate fails by accident.
#
# The probe, at EVP_PKEY_encrypt and at EVP_PKEY_verify.  `pk_encrypt` can only
# come from _cpri__EncryptRSA.  `pk_verify` is shared with ECDSA, so the total
# is still only reported; what is gated is `pk_verify_rsa_long`, which the
# probe attributes by the engine operation that was in flight when the call
# arrived rather than by asking libcrypto what the key was.  Before that the
# verify half of this gate was reachability and nothing else: the _cpri__MGF1
# clause above and gate 2's assertion that RSASSA_Decode is not compiled
# together forbid checking a PKCS#1 signature by hand, but neither of them
# requires that the EVP call which replaced it is ever reached at run time.
#
# Both floors are a quarter of what the workload produces.  asym_round drives
# _cpri__EncryptRSA twice and _cpri__ValidateSignatureRSA twice per key over
# two keys, so LONG_ROUNDS 64 gives 4 * 64 = 256 of each; the golden tree
# measures pk_encrypt_long 256 and pk_verify_rsa_long 256.
RSA_ENC_FLOOR=64
RSA_VERIFY_FLOOR=64
rsa_imp=$(jlist "$LOG/elf-engine.json" openssl_imports \
                "^($(printf '%s|' "${RSA_LOWLEVEL[@]}" | sed 's/|$//'))\$")
rsa_src=$(jlist "$AST" called \
                "^($(printf '%s|' "${RSA_LOWLEVEL[@]}" | sed 's/|$//'))\$")
rsa_text=$(jlist "$AST" text_names \
                 "^($(printf '%s|' "${RSA_LOWLEVEL[@]}" | sed 's/|$//'))\$")
enc_evp=$(reach_any _cpri__EncryptRSA EVP_PKEY_encrypt)
ver_evp=$(reach_any _cpri__ValidateSignatureRSA EVP_PKEY_verify)
enc_mgf=$(reach _cpri__EncryptRSA _cpri__MGF1)
ver_mgf=$(reach _cpri__ValidateSignatureRSA _cpri__MGF1)
norsadp=""
for fn in "${ASYM_PRIVATE[@]}"; do
    [ "$(reach_any "$fn" RSADP)" = 1 ] || norsadp="$norsadp $fn"
done
p_pk_enc=$(pnum pk_encrypt)
p_pk_enc_long=$(pnum pk_encrypt_long)
p_pk_ver=$(pnum pk_verify)
p_pk_ver_rsa=$(pnum pk_verify_rsa)
p_pk_ver_rsa_long=$(pnum pk_verify_rsa_long)
if [ "$ast_ok" != 1 ]; then
    rgate 0 rsa-through-evp "the AST scan is not trustworthy on this tree"
elif [ "$probe_ok" != 1 ]; then
    rgate 0 rsa-through-evp "the run-time probe did not complete on this" \
        "tree, so whether RSA's public operations ran through EVP is unknown"
elif [ "$enc_evp" = 1 ] && [ "$ver_evp" = 1 ] \
        && [ "$enc_mgf" = 0 ] && [ "$ver_mgf" = 0 ] \
        && [ -z "$norsadp" ] \
        && [ -z "$rsa_imp" ] && [ -z "$rsa_src" ] && [ -z "$rsa_text" ] \
        && [ "$p_pk_enc_long" -ge "$RSA_ENC_FLOOR" ] \
        && [ "$p_pk_ver_rsa_long" -ge "$RSA_VERIFY_FLOOR" ]; then
    rgate 1 rsa-through-evp "RSA's public operations are the library's --" \
        "_cpri__EncryptRSA reaches EVP_PKEY_encrypt and" \
        "_cpri__ValidateSignatureRSA reaches EVP_PKEY_verify, neither can" \
        "still reach _cpri__MGF1, the low-level <openssl/rsa.h> API is" \
        "absent from the import table, the call graph and the source text," \
        "and at run time $p_pk_enc EVP_PKEY_encrypt call(s) carried data" \
        "($p_pk_enc_long in the long half, against a floor of" \
        "$RSA_ENC_FLOOR) and $p_pk_ver_rsa of the $p_pk_ver" \
        "EVP_PKEY_verify call(s) arrived from an RSA operation" \
        "($p_pk_ver_rsa_long in the long half, floor $RSA_VERIFY_FLOOR)." \
        "The private half is where step 02 left it: all" \
        "${#ASYM_PRIVATE[@]} private-key entry points still reach RSADP"
else
    rgate 0 rsa-through-evp "_cpri__EncryptRSA -> EVP_PKEY_encrypt=$enc_evp," \
        "-> _cpri__MGF1=$enc_mgf; _cpri__ValidateSignatureRSA ->" \
        "EVP_PKEY_verify=$ver_evp, -> _cpri__MGF1=$ver_mgf;" \
        "private-key entry point(s) that no longer reach" \
        "RSADP:${norsadp:- none}; low-level RSA still" \
        "imported:${rsa_imp:- none}, called:${rsa_src:- none}, in the" \
        "sources:${rsa_text:- none}; at run time $p_pk_enc_long" \
        "EVP_PKEY_encrypt call(s) in the long half against a floor of" \
        "$RSA_ENC_FLOOR, and $p_pk_ver_rsa_long EVP_PKEY_verify call(s)" \
        "from an RSA operation against a floor of $RSA_VERIFY_FLOOR (out of" \
        "$p_pk_ver verify call(s) in all)"
fi

# --- 4: ECDSA goes through EVP, and EC_KEY is gone.
#
# The second substance gate, and the one with the cleanest instrument on the
# ladder: `low_ecdsa` reads 330 on the untouched step 04 tree and 0 on a
# hand-built spike that does the same three signatures and three verifications
# through EVP.  Trap 6 says verify the instrument before the finding, and both
# halves of that pair are recorded in NOTES-authoring.md section 9.2.3 --
# including the two counters beside it that did NOT survive the check.
# `low_eckey` discriminates as well as `low_ecdsa` does but is left reported
# rather than gated because steps 06-10 move to 3.5.4, where the EC_KEY
# interposer's linkage is not the one it has here; `low_rsanew` reads 0 on both
# sides of the pair, which means it separates nothing and can never carry a
# gate under any threshold.  It is logged for the same reason a control is.
#
# The scan half is the ban list: EC_KEY_new and the twenty-three names around
# it, asked of the import table, the call graph and the source text, plus the
# one type name asked of the text alone.  Every one of them lives inside
# SignEcdsa or ValidateSignatureEcdsa today and nowhere else in the tree, which
# is what makes "no longer imported" a sentence a correct port can satisfy
# rather than an aspiration about a file it is not allowed to touch.
#
# The list stops where the deprecation does.  ECDSA_SIG, ECDSA_SIG_get0 and
# d2i_ECDSA_SIG are current 3.x API and are not banned, because EVP_PKEY_sign
# returns a DER SEQUENCE and this engine's output is r and s; the note beside
# EC_LOWLEVEL has the argument in full.  The gate stays sharp without them:
# nothing in that family can produce a signature, and `low_ecdsa` below counts
# the four entry points that can.
#
# Two of step 02's clauses come across unchanged, and they are the reason the
# retirement above costs nothing.  `ecdsa-sign-library` and
# `ecdsa-verify-library` each asserted three things: that the library did the
# work, that the tree no longer did the modular inversion, and that it no
# longer did the point multiplication.  Only the first of the three named a
# call this step removes.  The other two are asked here of the same two
# functions in the same words -- neither may reach BN_mod_inverse and neither
# may reach _cpri__EccPointMultiply -- so a port that answered "stop using the
# deprecated ECDSA API" by writing the point arithmetic back out by hand fails
# this gate exactly as it would have failed step 02's.
#
# WHAT THIS GATE DOES NOT ASK
#
# ValidateSignatureEcdsa truncates the caller's digest to the length of the
# signature's own r, and the 22 ECDSA rows in the eccverify family only
# reproduce if it still does.  That requirement is real and it is not here,
# because it is two struct field reads and a comparison -- no call, no import,
# no name -- and there is nothing for a scan to see.  It is carried by
# behaviour, where it was already carried, and stating that plainly is better
# than a text-scan proxy that would pass any tree that kept the variable name.
#
# The verify side has the same shape the RSA gate's did and is closed the same
# way: reachability says ValidateSignatureEcdsa can get to EVP_PKEY_verify and
# says nothing about whether it does, so `pk_verify_ec_long` is gated beside
# the sign rate.  asym_round verifies on three curves and signs on two per
# round, so LONG_ROUNDS 64 gives 3 * 64 = 192 verifies and 2 * 64 = 128
# signatures; the golden tree measures pk_verify_ec_long 192 and
# pk_sign_long 128, and both floors are a quarter of that.
ECDSA_SIGN_FLOOR=32
ECDSA_VERIFY_FLOOR=48
ec_imp=$(jlist "$LOG/elf-engine.json" openssl_imports \
               "^($(printf '%s|' "${EC_LOWLEVEL[@]}" | sed 's/|$//'))\$")
ec_src=$(jlist "$AST" called \
               "^($(printf '%s|' "${EC_LOWLEVEL[@]}" | sed 's/|$//'))\$")
ec_text=$(jlist "$AST" text_names \
                "^($(printf '%s|' "${EC_LOWLEVEL[@]}" | sed 's/|$//')|EC_KEY)\$")
sign_evp=$(reach_any SignEcdsa EVP_PKEY_sign)
vfy_evp=$(reach_any ValidateSignatureEcdsa EVP_PKEY_verify)
sign_eck=$(reach SignEcdsa EC_KEY_new)
vfy_eck=$(reach ValidateSignatureEcdsa EC_KEY_new)
handrolled=""
for fn in "${ECDSA_WORK[@]}"; do
    [ "$(reach "$fn" BN_mod_inverse)" = 0 ] \
        || handrolled="$handrolled $fn:BN_mod_inverse"
    [ "$(reach "$fn" _cpri__EccPointMultiply)" = 0 ] \
        || handrolled="$handrolled $fn:_cpri__EccPointMultiply"
done
p_low_ecdsa=$(pnum low_ecdsa)
p_low_eckey=$(pnum low_eckey)
p_pk_sign=$(pnum pk_sign)
p_pk_sign_long=$(pnum pk_sign_long)
p_pk_ver_ec=$(pnum pk_verify_ec)
p_pk_ver_ec_long=$(pnum pk_verify_ec_long)
if [ "$ast_ok" != 1 ]; then
    rgate 0 ecdsa-through-evp "the AST scan is not trustworthy on this tree"
elif [ "$probe_ok" != 1 ]; then
    rgate 0 ecdsa-through-evp "the run-time probe did not complete on this" \
        "tree, so whether the deprecated ECDSA API still runs is unknown"
elif [ "$sign_evp" = 1 ] && [ "$vfy_evp" = 1 ] \
        && [ "$sign_eck" = 0 ] && [ "$vfy_eck" = 0 ] \
        && [ -z "$handrolled" ] \
        && [ -z "$ec_imp" ] && [ -z "$ec_src" ] && [ -z "$ec_text" ] \
        && [ "$p_low_ecdsa" = 0 ] \
        && [ "$p_pk_sign_long" -ge "$ECDSA_SIGN_FLOOR" ] \
        && [ "$p_pk_ver_ec_long" -ge "$ECDSA_VERIFY_FLOOR" ]; then
    rgate 1 ecdsa-through-evp "ECDSA is the library's -- SignEcdsa reaches" \
        "EVP_PKEY_sign and ValidateSignatureEcdsa reaches EVP_PKEY_verify," \
        "neither still reaches EC_KEY_new and neither has taken the" \
        "inversion or the point multiplication back, none of the" \
        "${#EC_LOWLEVEL[@]}" \
        "deprecated names and not the EC_KEY type appears in the import" \
        "table, the call graph or the sources, and across the probe's run" \
        "the low-level" \
        "ECDSA entry points were called $p_low_ecdsa time(s) while" \
        "$p_pk_sign EVP_PKEY_sign call(s) produced a signature" \
        "($p_pk_sign_long in the long half, against a floor of" \
        "$ECDSA_SIGN_FLOOR) and $p_pk_ver_ec EVP_PKEY_verify call(s)" \
        "arrived from an EC operation ($p_pk_ver_ec_long in the long half," \
        "floor $ECDSA_VERIFY_FLOOR).  EC_KEY objects made: $p_low_eckey," \
        "reported and not gated"
else
    rgate 0 ecdsa-through-evp "SignEcdsa -> EVP_PKEY_sign=$sign_evp, ->" \
        "EC_KEY_new=$sign_eck; ValidateSignatureEcdsa ->" \
        "EVP_PKEY_verify=$vfy_evp, -> EC_KEY_new=$vfy_eck; hand-rolled" \
        "arithmetic reachable again:${handrolled:- none}; still" \
        "imported:${ec_imp:- none}; still called:${ec_src:- none}; still in" \
        "the sources:${ec_text:- none}; at run time $p_low_ecdsa low-level" \
        "ECDSA call(s) and $p_low_eckey EC_KEY object(s) against" \
        "$p_pk_sign_long EVP_PKEY_sign call(s) in the long half (floor" \
        "$ECDSA_SIGN_FLOOR) and $p_pk_ver_ec_long EVP_PKEY_verify call(s)" \
        "from an EC operation (floor $ECDSA_VERIFY_FLOOR)"
fi

CHECK_OWNER=06
# ============================================== step 06's own gates (0.54) ==

# --- 1: the engine configures its own context, and the module loads into it.
#
# This is the paperwork gate for the object the other three depend on.  Every
# one of them is a statement about a library context that has a FIPS provider
# in it, and on this release getting one there takes two calls in a particular
# order, neither of which the ladder has needed before.
#
# WHAT THE SCAN CAN SEE
#
# That OSSL_LIB_CTX_load_config is imported, is called, and is reached from
# _cpri__InitCryptoUnits -- the function steps 01 and 03 already name as the
# engine's startup, so which entry point counts as startup is not a guess.
#
# WHAT ONLY THE PROBE CAN SEE
#
# Which context was configured, which is the entire content of the sentence.
# CONF_modules_load_file(path, NULL, 0) configures the PROCESS.  So does
# OSSL_LIB_CTX_load_config(NULL, path), which is the same mistake written in
# this step's own vocabulary.  Both compile, both return 1, both make the FIPS
# provider loadable, and both undo step 03: the context the engine fetches
# from is the one it made, and a configuration that went somewhere else is not
# that context's configuration.  The probe compares the pointer against the
# contexts it watched OSSL_LIB_CTX_new hand this engine, so this is identity
# and not spelling.
#
# And whether the call worked.  OSSL_LIB_CTX_load_config returns 0 on a file
# that is missing, unreadable or malformed, and a tree that drops the return
# proceeds to an OSSL_PROVIDER_load that will fail for a reason it has thrown
# away.  cfg_ok_engine is the conjunction -- returned 1, on a context this
# engine made -- rather than two counters a gate has to hope are about the
# same call.
#
# And that a provider named "fips" actually came back.  The name is a string
# in the source and the provider is a pointer at run time, and no scan can
# join the two; prov_fips_ok_engine is that conjunction for the same reason
# cfg_ok_engine is.  The load has to be an explicit one: activating the module
# from the configuration file alone does not come out through this
# interposer -- libcrypto activates it internally -- so a tree that only
# writes the config reads prov_fips 0 here.  That is the intended reading.
# The instruction asks for a handle, because gates 2 and 4 need one: a module
# nobody holds cannot be self-tested and cannot be given back.
#
# And that the provider the engine already had is still there beside it.  This
# step ADDS a provider; it does not swap one.  A tree that replaced "default"
# with "fips" would satisfy every sentence about the module and would lose
# every 1024-bit RSA encryption and every EC key this engine builds -- which
# the corpus prices, but the gate should not read as a pass on the way past.
cfg_imp=$(jhas "$LOG/elf-engine.json" openssl_imports \
               '^OSSL_LIB_CTX_load_config$')
cfg_call=$(jhas "$AST" called '^OSSL_LIB_CTX_load_config$')
cfg_init=$(reach_any _cpri__InitCryptoUnits OSSL_LIB_CTX_load_config)
p_cfg=$(pnum cfg_load)
p_cfg_init=$(pnum cfg_load_at_init)
p_cfg_eng=$(pnum cfg_ctx_engine)
p_cfg_null=$(pnum cfg_ctx_null)
p_cfg_other=$(pnum cfg_ctx_other)
p_cfg_ok=$(pnum cfg_ok)
p_cfg_okeng=$(pnum cfg_ok_engine)
p_fips=$(pnum prov_fips)
p_fips_okeng=$(pnum prov_fips_ok_engine)
p_dflt=$(pnum prov_default)
# The process-wide route, reported beside the gate rather than banned by it.
# A tree that reaches CONF_modules_load_file has configured the process, which
# the probe already reports as cfg_ctx_engine 0 -- the ban would be a second
# charge for one defect.  The log line is what turns "gate 1 failed" into
# "gate 1 failed and here is the call that did it".
#
# cfg_ctx_other is reported the same way and for a stronger reason: the
# instruction bans loading this engine's configuration into the PROCESS -- that
# is cfg_ctx_null, and it is gated -- and says nothing at all about a third
# context.  A port that configures the engine's context and also a scratch
# context it made for a dry run has done everything the ticket asks; failing it
# here would be a conjunct the candidate was never given.  So the gate reads
# cfg_ok_engine >= 1 and cfg_ctx_null = 0, and cfg_ctx_other goes in the log.
#
# (cfg_ctx_engine >= 1 is not a separate conjunct because cfg_ok_engine >= 1
# already implies it -- a load cannot return success on the engine's context
# without having been made on the engine's context.  A tenth term that can
# never fire would read like a check and be one only in the way it hides.)
conf_proc=$(jhas "$AST" called '^CONF_modules_load_file(_ex)?$')
if [ "$conf_proc" = 1 ]; then
    vlog "note: this tree calls CONF_modules_load_file, which configures the" \
         "process rather than a library context"
fi
if [ "$ast_ok" != 1 ]; then
    gate 0 fips-configured "the AST scan is not trustworthy on this tree, so" \
        "where the configuration is loaded from cannot be read"
elif [ "$probe_ok" != 1 ]; then
    gate 0 fips-configured "the run-time probe did not complete on this" \
        "tree, so which context was configured and which provider came back" \
        "are both unknown"
elif [ "$cfg_imp" = 1 ] && [ "$cfg_call" = 1 ] && [ "$cfg_init" = 1 ] \
        && [ "$p_cfg_init" -ge 1 ] && [ "$p_cfg_okeng" -ge 1 ] \
        && [ "$p_cfg_null" = 0 ] \
        && [ "$p_fips_okeng" -ge 1 ] && [ "$p_dflt" -ge 1 ]; then
    gate 1 fips-configured "the engine configures the library context it made" \
        "and loads the FIPS module into it -- OSSL_LIB_CTX_load_config" \
        "imported, called and reached from _cpri__InitCryptoUnits," \
        "$p_cfg call(s) at run time ($p_cfg_init of them during startup)," \
        "$p_cfg_eng of them on a context this engine created and" \
        "$p_cfg_okeng returning success on one, from '$(pnum_s cfg_path)'" \
        "(on some other context $p_cfg_other time(s), reported and not" \
        "gated); $p_fips_okeng" \
        "provider(s) named fips loaded into that same context, beside the" \
        "$p_dflt named default that step 03 already required"
else
    gate 0 fips-configured "OSSL_LIB_CTX_load_config imported=$cfg_imp" \
        "called=$cfg_call reached-from-startup=$cfg_init; at run time $p_cfg" \
        "call(s) ($p_cfg_init during startup), context engine=$p_cfg_eng" \
        "process-default=$p_cfg_null other=$p_cfg_other, returned success" \
        "$p_cfg_ok time(s) of which $p_cfg_okeng on the engine's own" \
        "context; provider loads naming fips=$p_fips, of them into the" \
        "engine's context and successful=$p_fips_okeng; loads naming" \
        "default=$p_dflt"
fi

# --- 2: every provider the engine loaded is given back, and not early.
#
# Step 03's `provider-explicit` asks that A provider is loaded at startup and
# A provider is unloaded at shutdown.  Both of its numbers are 1 on a tree
# that has one provider, which is every tree until this one, and the gate was
# written when that was the only shape there was -- its own forward reference
# says so: "step 06 is where the provider list stops being a formality".
#
# It is a formality no longer, and the arithmetic is why.  With two providers
# held, "at least one unload at shutdown" is satisfied by a tree that gives
# back the default and leaks the module.  A leaked FIPS provider is not a
# leaked allocation: the module holds its self-test state and its DRBG chain,
# and a shutdown that does not release it leaves a TPM that cannot be restarted
# in the same process -- which is exactly what _cpri__StopCryptoUnits exists to
# make possible.  So this gate counts, rather than asking whether.
#
# The second half is the one a scan cannot even approach.  "Not unloaded
# early" is a claim about WHEN, and the probe's phases are the only place that
# exists: a tree that loads the module at startup, self-tests it, unloads it
# on the next line and then fetches everything out of the default for the rest
# of the run would pass every sentence gate 1 and gate 4 make and would have
# a FIPS module for the length of one function.  fips_unload_before_stop is
# that shape, counted.
#
# Nothing here reads the module's name a second time.  It reads the pointer
# table gate 1 filled, which is the same distinction gate 1 draws between the
# string and the handle, applied at the other end of the run.
p_prov_over=$(pnum prov_over)
p_prov_distinct=$(pnum prov_distinct)
p_prov_unstop=$(pnum prov_unload_at_stop)
p_prov_unknown=$(pnum prov_unload_known)
p_fips_distinct=$(pnum fips_distinct)
p_fips_unstop=$(pnum fips_unload_at_stop)
p_fips_unearly=$(pnum fips_unload_before_stop)
p_fips_live=$(pnum fips_live)
# Unbounded -- see prov_unload above.  (Migration rubric review round 2.)
prov_unl=$(reach_any _cpri__StopCryptoUnits OSSL_PROVIDER_unload)
if [ "$ast_ok" != 1 ]; then
    gate 0 providers-released "the AST scan is not trustworthy on this tree"
elif [ "$probe_ok" != 1 ]; then
    gate 0 providers-released "the run-time probe did not complete on this" \
        "tree, so what it gave back and when cannot be read"
elif [ "$p_prov_over" != 0 ]; then
    gate 0 providers-released "the probe watched more distinct providers" \
        "($p_prov_distinct) than its table holds, so \"one it loaded\" is" \
        "true of every provider and this gate cannot be measured on this tree"
elif [ "$prov_unl" = 1 ] && [ "$p_prov_distinct" -ge 2 ] \
        && [ "$p_fips_distinct" -ge 1 ] \
        && [ "$p_prov_unknown" -ge "$p_prov_distinct" ] \
        && [ "$p_prov_unstop" -ge "$p_prov_distinct" ] \
        && [ "$p_fips_unstop" -ge 1 ] && [ "$p_fips_unearly" = 0 ] \
        && [ "$p_fips_live" = 0 ]; then
    gate 1 providers-released "the engine holds $p_prov_distinct provider(s)" \
        "for the length of the run and gives all of them back at shutdown --" \
        "$p_prov_unstop unload(s) there, $p_prov_unknown of one this probe" \
        "watched loaded, the FIPS module among them ($p_fips_unstop unload(s)," \
        "none before shutdown, none still held)"
else
    gate 0 providers-released "OSSL_PROVIDER_unload reached from" \
        "_cpri__StopCryptoUnits=$prov_unl; $p_prov_distinct distinct" \
        "provider(s) watched loaded, $p_prov_unknown of them given back," \
        "$p_prov_unstop unload(s) at shutdown; FIPS handles" \
        "watched=$p_fips_distinct, unloaded at shutdown=$p_fips_unstop," \
        "unloaded before it=$p_fips_unearly, still held=$p_fips_live"
fi

# --- 3: every fetch names what it wants.
#
# The substance of the step, and the only gate on the ladder whose subject is
# an argument that has been correct until now.
#
# Steps 03, 04 and 05 required nine call sites to name the engine's library
# context.  All nine pass NULL as the property query, and while there was one
# provider in that context NULL was not a defect -- there was nothing for a
# query to choose between and the fetch resolved to the only implementation
# there was.  Gate 1 puts a second one in, and the same NULL now means "the
# highest-priority implementation of this algorithm, whichever that turns out
# to be", which on this release is the FIPS module for everything the module
# implements.  NOTES-authoring.md section 10.2 has what that costs.
#
# WHAT THE SCAN CANNOT SEE, WHICH IS ALL OF IT
#
# NULL and "provider=default" are both the third argument of EVP_MD_fetch and
# both are one token.  The call graph is identical, the import table is
# identical, the source differs by a string literal that a scan has no way to
# connect to an outcome.  There is no AST half of this gate and none is
# pretended: the conjunction below reads the probe and says so.
#
# WHAT IS COUNTED -- THE PRESENCE HALF
#
# The five calls in this libcrypto that take a property query and that this
# engine's ported shape can reach: EVP_MD_fetch, EVP_CIPHER_fetch,
# EVP_PKEY_CTX_new_from_name, EVP_PKEY_CTX_new_from_pkey and EVP_Q_digest.
# NULL and "" are counted apart, because they are the same thing to libcrypto
# and different things to whoever reads the tree next, and both are required
# to be zero.
#
# EVP_PKEY_CTX_new and EVP_PKEY_CTX_new_id are counted too, and they are the
# reason this half survives the second half below.  Neither takes a property
# query -- there is nowhere in either signature to put one -- so a port that
# builds its key-operation contexts with those two has named nothing and has
# also passed no NULL query, because it made no call that could carry one.
# The probe counts them as pq_noquery and folds them into the NULL total, and
# pq_want counts them among the calls that have to be accounted for.  The
# effect half cannot close this hole for them: a context that cannot express a
# query resolves by load order in both contexts and would pin, which is
# exactly the wrong answer.  So the presence conjunct stays, and it is what
# prices these two spellings.
#
# WHAT IS MEASURED -- THE EFFECT HALF
#
# Naming an implementation and pinning one are different acts, and until now
# only the first was read.  "?x=1" is a named, non-empty, syntactically valid
# query whose one clause is optional and matches nothing, so libcrypto drops
# it and falls back to load order -- it is NULL wearing a string.  Counting
# characters cannot tell it from "provider=default"; calib/06-propq-dummy.sh
# is the tree that does exactly that and used to score a clean 1.
#
# So the probe builds a second library context the tree cannot see, loads this
# tree's own configuration into it, and then loads "fips" and "default" in the
# REVERSED order -- the one thing the tree controls that a query is supposed
# to override.  At every fetch the probe repeats the tree's fetch in that
# context, with the tree's own algorithm and the tree's own query, and
# compares the provider name each side landed on.  Equal is a pin: the query
# decided.  Different is a drift: the load order decided, and the query was
# decoration.  The query TEXT is still not gated -- "provider=default" and
# "fips=yes" and "provider!=fips" all pin, and all three are legitimate
# answers to a question this ticket leaves to the port.
#
# THE KEY-OPERATION CONTEXTS ARE IN THE EFFECT HALF TOO, AND THEY MATTER MOST
#
# EVP_PKEY_CTX_new_from_name and _from_pkey hand back a context rather than an
# implementation, so there is no object of the tree's to read a provider name
# off -- which is why they were presence-only when this half was first built.
# It is not a limit.  The query those calls take is the query
# EVP_KEYMGMT_fetch takes: building the context is what picks the key
# management implementation the whole operation runs on.  So the probe fetches
# that keymgmt itself, on the library context the call named and with the
# call's own query, and compares it the same way -- the same thing it already
# did for the digest EVP_Q_digest resolves inside libcrypto.  _from_pkey has
# no name argument, so the name is the key's own: EVP_PKEY_get0_type_name.
#
# This is the half whose query text actually moves this corpus.  SHA-256 and
# AES compute the same bytes whichever provider answers them; the keymgmt is
# what decides whether a 1024-bit RSA key can be encrypted with and whether an
# explicit-parameter EC curve is accepted at all, and probe/step06/
# measured.txt puts those at fromdata and encrypt_init.  With the comparison
# on the eighteen startup fetches alone, a tree that names an implementation
# there and writes "?x=1" on all 1716 key-operation contexts pinned 18,
# drifted 0, named 1734 and scored a clean 1 with every key operation still
# decided by load order.  calib/06-propq-split.sh is that tree.
#
# EVP_PKEY_CTX_new and EVP_PKEY_CTX_new_id stay presence-only, and must: they
# take no query at all, so both sides would resolve by load order and both
# would pin, which is exactly the wrong answer.  The presence conjunct above
# is what prices those two.
#
# The counterfactual is why the text does not have to be gated: it prices what
# the query DOES without ever having to know what it should say.
#
# THE INSTRUMENT IS CHECKED BEFORE THE FINDING
#
# "No NULL queries" is true of a tree that makes no fetches at all, and it is
# also true of a probe whose note_pq call was dropped from an interposer.  The
# first is closed by steps 03, 04 and 05's own gates, which require the fetches
# and tie their counts to the workload.  The second is closed here, by
# requiring the named-query count to ACCOUNT FOR the calls the probe watched:
# every EVP_MD_fetch and EVP_Q_digest against the digest fetch total, every
# EVP_CIPHER_fetch against the cipher fetch total, every _from_name, _from_pkey
# and no-query spelling against the EVP_PKEY_CTX creation total.  Those three
# are equalities by construction on a correctly wired probe, which is exactly
# why a mismatch is a finding about the probe; and because every one of the
# three totals is a workload-scaled number that other gates already pin, there
# is no constant here to drift.
#
# The effect half gets the same treatment, and needs it more.  Two things can
# make it silently say "pinned" when nothing was measured: the counterfactual
# context failing to build, and a watched call the comparison never reached.
# Both are read before the finding.  pq_shadow must be 1, and pin + drift +
# not-compared must equal the digest fetch, cipher fetch and key-operation
# context totals exactly -- one outcome per watched call, no more and no fewer.
# An equality rather than a floor, because a comparison that ran twice for one
# call is as wrong as one that never ran.  And the pass requires at least one
# PIN, not merely no drift: a tree whose every fetch failed on both sides has
# no drift and has measured nothing.
#
# The shadow context's own work must not reach any counter in this file.  The
# probe guards it with a re-entry flag on every interposer that the shadow
# path can reach -- the fetches, the frees, the context calls, the provider
# calls -- and counts the re-entries as pq_reenter.  On the golden that number
# is 0: this libcrypto resolves its own internal fetches without going back
# out through its exported entry points, so the shadow's work never re-enters
# this file at all.  It is reported and NOT gated anyway, in both directions.
# Gating it to 0 would be a conjunct that has never been anything else, and a
# non-zero value is not a fault -- it is the guard doing its job on a tree
# whose fetches nest differently.  What actually proves the guard is the
# golden scoring what it scored before this gate learned to do any of this,
# with gates 1, 2 and 4 reading the same counters they always read.
#
# What the query SAYS is still reported and not gated.  The top of this file
# argues it at length; what the counterfactual adds is that the text no longer
# has to be gated for the gate to be substantive.
p_pq_null=$(pnum pq_null)
p_pq_empty=$(pnum pq_empty)
p_pq_set=$(pnum pq_set)
p_pq_md=$(pnum pq_md_set)
p_pq_q=$(pnum pq_qdigest_set)
p_pq_ci=$(pnum pq_cipher_set)
p_pq_pkn=$(pnum pq_pkey_name_set)
p_pq_pkp=$(pnum pq_pkey_pkey_set)
# The totals the counts above have to account for.  `fetch` is the digest side
# and already includes EVP_Q_digest, which is why the two digest counters are
# summed against it; `ciph_fetch` is the cipher side; `pkctx_new` is the two
# _from_ spellings and `pkctx_inherit` the two that take no property query at
# all.  The second of those is in pq_want deliberately: a key-operation
# context built by a call with no query argument is still a key-operation
# context that named nothing, and leaving it out of the total is how it used
# to pass.
p_fetch_all=$(pnum fetch)
p_ciph_fetch=$(pnum ciph_fetch)
p_pkctx_new=$(pnum pkctx_new)
p_pkctx_noq=$(pnum pkctx_inherit)
pq_want=$(( p_fetch_all + p_ciph_fetch + p_pkctx_new + p_pkctx_noq ))
# The effect half.
p_pq_noq=$(pnum pq_noquery)
p_pq_shadow=$(pnum pq_shadow)
p_pq_pin=$(pnum pq_pin)
p_pq_drift=$(pnum pq_drift)
p_pq_nocmp=$(pnum pq_nocmp)
cmp_seen=$(( p_pq_pin + p_pq_drift + p_pq_nocmp ))
# The calls the effect half has to reach: both fetch totals and both _from_
# spellings.  pkctx_inherit is deliberately NOT here -- EVP_PKEY_CTX_new and
# _new_id carry no query, so a comparison of them would pin on every tree and
# would say the opposite of what it looked like it said.  Presence is their
# instrument and pq_want above is where they are counted.
cmp_want=$(( p_fetch_all + p_ciph_fetch + p_pkctx_new ))
# Every call the probe watched has to come back classified as one of named,
# NULL or empty -- that is the instrument invariant, and it is true whatever
# the tree passes.  It is deliberately NOT "how many were named": a tree that
# names none of them is the finding this gate exists for, and reading that as
# a broken instrument would hand the step 05 tree an excuse instead of a
# verdict.  Each sum is over the same counters the failure branch below prints,
# so a mismatch here and a finding there cannot disagree about what was seen.
md_seen=$(( p_pq_md + p_pq_q + $(pnum pq_md_null) + $(pnum pq_md_empty) \
            + $(pnum pq_qdigest_null) + $(pnum pq_qdigest_empty) ))
ci_seen=$(( p_pq_ci + $(pnum pq_cipher_null) + $(pnum pq_cipher_empty) ))
pk_seen=$(( p_pq_pkn + p_pq_pkp + p_pq_noq + $(pnum pq_pkey_name_null) \
            + $(pnum pq_pkey_name_empty) + $(pnum pq_pkey_pkey_null) \
            + $(pnum pq_pkey_pkey_empty) ))
pk_want=$(( p_pkctx_new + p_pkctx_noq ))
if [ "$probe_ok" != 1 ]; then
    gate 0 property-query-everywhere "the run-time probe did not complete on" \
        "this tree, so what its fetches asked for cannot be read"
elif [ "$p_fetch_all" -lt 1 ] || [ "$p_ciph_fetch" -lt 1 ] \
        || [ "$p_pkctx_new" -lt 1 ]; then
    gate 0 property-query-everywhere "the probe watched $p_fetch_all digest" \
        "fetch(es), $p_ciph_fetch cipher fetch(es) and $p_pkctx_new key" \
        "operation context(s) -- with any of those at zero \"no fetch used" \
        "the default\" is vacuous and this gate cannot be measured"
elif [ "$md_seen" -lt "$p_fetch_all" ] || [ "$ci_seen" -lt "$p_ciph_fetch" ] \
        || [ "$pk_seen" -lt "$pk_want" ]; then
    gate 0 property-query-everywhere "the probe's property-query counters do" \
        "not account for the calls it watched -- digests $md_seen classified" \
        "of $p_fetch_all, ciphers $ci_seen of $p_ciph_fetch, key contexts" \
        "$pk_seen of $pk_want.  That is a fault in this instrument, not a" \
        "finding about this tree, and it is scored 0 rather than passed"
elif [ "$p_pq_shadow" != 1 ]; then
    gate 0 property-query-everywhere "the probe could not build the" \
        "counterfactual library context it compares this tree's fetches" \
        "against, so what this tree's queries DO cannot be read (it loads" \
        "the configuration this tree loaded, from" \
        "'$(pnum_s cfg_path)').  That is a fault in this instrument, not a" \
        "finding about this tree, and it is scored 0 rather than passed"
elif [ "$cmp_seen" != "$cmp_want" ]; then
    gate 0 property-query-everywhere "the probe compared $cmp_seen call(s)" \
        "against the counterfactual context ($p_pq_pin pinned, $p_pq_drift" \
        "drifted, $p_pq_nocmp not comparable) but watched $cmp_want" \
        "($p_fetch_all digest fetch(es), $p_ciph_fetch cipher fetch(es)," \
        "$p_pkctx_new key-operation context(s)) -- one outcome per watched" \
        "call is an equality by construction.  That is a fault in" \
        "this instrument, not a finding about this tree, and it is scored 0" \
        "rather than passed"
elif [ "$p_pq_null" != 0 ] || [ "$p_pq_empty" != 0 ] \
        || [ "$p_pq_set" -lt "$pq_want" ]; then
    gate 0 property-query-everywhere "$p_pq_null call(s) passed a NULL" \
        "property query or could not carry one, and $p_pq_empty passed an" \
        "empty one, out of $pq_want that resolve an implementation --" \
        "EVP_MD_fetch NULL=$(pnum pq_md_null)" \
        "empty=$(pnum pq_md_empty) named=$p_pq_md; EVP_CIPHER_fetch" \
        "NULL=$(pnum pq_cipher_null) empty=$(pnum pq_cipher_empty)" \
        "named=$p_pq_ci; EVP_PKEY_CTX_new_from_name" \
        "NULL=$(pnum pq_pkey_name_null) empty=$(pnum pq_pkey_name_empty)" \
        "named=$p_pq_pkn; EVP_PKEY_CTX_new_from_pkey" \
        "NULL=$(pnum pq_pkey_pkey_null) empty=$(pnum pq_pkey_pkey_empty)" \
        "named=$p_pq_pkp; EVP_Q_digest NULL=$(pnum pq_qdigest_null)" \
        "empty=$(pnum pq_qdigest_empty) named=$p_pq_q; EVP_PKEY_CTX_new and" \
        "EVP_PKEY_CTX_new_id, which cannot carry one at all, $p_pq_noq"
elif [ "$p_pq_drift" != 0 ] || [ "$p_pq_pin" -lt 1 ]; then
    gate 0 property-query-everywhere "every fetch and key-operation context" \
        "on this tree names an implementation, but $p_pq_drift of them did" \
        "not GET the one they named: repeated in a library context holding" \
        "the same two providers in the opposite order, with the same" \
        "algorithm and the same query, $p_pq_drift of them landed somewhere" \
        "else and only $p_pq_pin landed in the same place (first drift:" \
        "'$(pnum_s pq_drift_at)'; $p_pq_nocmp resolved to nothing on one" \
        "side or both).  A query whose every clause is optional and matches" \
        "nothing is dropped, and what decides is then the load order --" \
        "which is what this step asks the port to stop depending on.  The" \
        "key-operation contexts are compared as the EVP_KEYMGMT_fetch their" \
        "query is for, and they are the ones this TPM's 1024-bit RSA and" \
        "explicit-parameter EC keys ride on.  The first query was" \
        "'$(pnum_s pq_text)'"
else
    gate 1 property-query-everywhere "every one of the $p_pq_set fetch(es)" \
        "and key-operation context(s) the probe watched named the" \
        "implementation it wanted -- $p_pq_md EVP_MD_fetch, $p_pq_q" \
        "EVP_Q_digest, $p_pq_ci EVP_CIPHER_fetch, $p_pq_pkn" \
        "EVP_PKEY_CTX_new_from_name, $p_pq_pkp EVP_PKEY_CTX_new_from_pkey," \
        "none with a NULL or empty query and none built by a call that" \
        "cannot carry one, against $pq_want such call(s); and every one of" \
        "the $p_pq_pin compared call(s) GOT what it named -- repeated in a" \
        "library context holding the same two providers in the opposite" \
        "order, the fetches as fetches and the key-operation contexts as the" \
        "EVP_KEYMGMT_fetch their query is for, all $p_pq_pin resolved to the" \
        "same implementation and none drifted.  The first query was" \
        "'$(pnum_s pq_text)' (reported, not" \
        "gated -- what it does is what is gated)"
fi

# --- 4: the module is asked whether it works, and the answer is used.
#
# The other half of the substance, and the only gate on this ladder that is
# measured twice.
#
# Loading a FIPS provider and running its self test are different acts.
# OSSL_PROVIDER_load succeeds on a module whose integrity MAC matches the file
# on disk; OSSL_PROVIDER_self_test runs the algorithm self tests the module
# carries and is what an operator who has to attest to the module is actually
# attesting to.  The first is gate 1's; this is the second.
#
# WHAT THE SCAN AND THE FIRST RUN CAN SEE
#
# That OSSL_PROVIDER_self_test is imported, called and reached from startup;
# that it ran during startup; that its argument was the provider this engine
# loaded and not some other; and that it returned 1.
#
# WHAT NEITHER CAN SEE, AND WHAT THE SECOND RUN IS FOR
#
# Whether the tree USED the answer.  On a machine whose module is intact,
#
#     OSSL_PROVIDER_self_test(s_fips);
#
# and
#
#     if(OSSL_PROVIDER_self_test(s_fips) != 1) FAIL(FATAL_ERROR_INTERNAL);
#
# are the same call, the same counter, the same call graph and the same
# behaviour.  The difference between them is the requirement.
#
# So the probe is run a second time with PROBE_FAIL_SELFTEST=1, which makes
# its interposer return 0 without calling through.  The module is untouched;
# only the answer is a lie.  A tree that checks does not finish its startup,
# and HOW it does not finish is part of the requirement: FAIL() reaches TpmFail
# and this probe reports it as ok=0 with tpmfail set, which is the outcome the
# gate asks for.  Returning something other than CRYPT_SUCCESS also ends the
# run at ok=0, but with tpmfail still 0, and the probe reports that separately
# as init_rc; three paragraphs down is why those two are not scored the same.
# A tree that does not check completes the fixture run exactly as it completed
# the first one, ops and all.
#
# What "does not finish" means here is the engine's own failure path.  The
# instruction asks that a failed self test take the failure path this engine
# already has for a startup that cannot proceed -- the one the provider load
# beside it takes -- and the engine has two honest spellings of it: FAIL(),
# which the probe sees as tpmfail, and a non-success return from
# _cpri__InitCryptoUnits, which the probe sees as init_rc != CRYPT_SUCCESS (0)
# and which the TPM above the engine turns into the same failure mode
# (src/tpm/crypt/CryptUtil.c: `if(_cpri__InitCryptoUnits(&TpmFail) !=
# CRYPT_SUCCESS) FAIL(FATAL_ERROR_INTERNAL);`).  So the fixture run has to
# show: ok 0, no operations attempted, and one of those two.
#
# An earlier version required tpmfail alone, on the claim that a returned
# failure never reaches TpmFail.  It does, one frame up, and step 03 never says
# which of the two a failed provider load takes -- so a tree whose load and
# self test both refuse at the return lost this gate for following the
# instruction.  The probe's own comment says the same thing ("FAIL() is one of
# the two honest answers ... refusing at the return is the other").  An engine
# that neither FAILs nor returns failure still starts, still runs operations,
# and still fails here.  (Migration rubric review round 2, 2026-10-05.)
#
# The first run's selftest_forced is NOT a conjunct.  That run does not set
# PROBE_FAIL_SELFTEST, so the counter is 0 on every tree that has ever been
# graded and on every tree that ever will be: a term that cannot fail is not a
# check.  fixture_ok below is the real instrument guard -- it reads the SECOND
# run's selftest_forced, where a 0 means the lie was not told and the
# measurement is worthless.
#
# And the fixture is checked before it is believed.  fixture_ok is set only
# when the second run wrote its JSON AND selftest_forced came back at least 1
# -- that is, the lie was actually told.  A fixture that silently did not fire
# would make every tree look like one that checks, which is the failure mode
# this whole construction exists to avoid.  When it does not fire the gate
# scores 0 and says the instrument is why.
#
# The last conjunct is the module's lifetime, and it belongs here rather than
# in gate 2 because it is the difference between a self test and a ritual: a
# tree that loads the module, self-tests it and unloads it before doing any
# work has attested to a module it then stopped using.  Gate 2 counts the
# unloads; this one requires that none of them happened before shutdown, which
# is the same number read for a different sentence.
st_imp=$(jhas "$LOG/elf-engine.json" openssl_imports '^OSSL_PROVIDER_self_test$')
st_call=$(jhas "$AST" called '^OSSL_PROVIDER_self_test$')
st_init=$(reach_any _cpri__InitCryptoUnits OSSL_PROVIDER_self_test)
p_st=$(pnum selftest)
p_st_init=$(pnum selftest_at_init)
p_st_known=$(pnum selftest_known)
p_st_fips=$(pnum selftest_fips)
p_st_ok=$(pnum selftest_ok)
p_st_forced=$(pnum selftest_forced)
f_ok=$(pnum2 ok)
f_tpmfail=$(pnum2 tpmfail)
f_rc=$(pnum2 init_rc)
f_ops=$(pnum2 ops)
f_forced=$(pnum2 selftest_forced)
if [ "$f_ok" = 0 ] && [ "$f_ops" = 0 ] \
        && { [ "$f_tpmfail" != 0 ] || [ "$f_rc" != 0 ]; }; then
    refused=1
else
    refused=0
fi
if [ "$ast_ok" != 1 ]; then
    gate 0 fips-self-tested-and-live "the AST scan is not trustworthy on this" \
        "tree, so where the self test is run from cannot be read"
elif [ "$probe_ok" != 1 ]; then
    gate 0 fips-self-tested-and-live "the run-time probe did not complete on" \
        "this tree, so the self test cannot be measured"
elif [ "$p_st" = 0 ]; then
    gate 0 fips-self-tested-and-live "this tree never runs" \
        "OSSL_PROVIDER_self_test -- 0 call(s) across the whole probe run" \
        "(imported=$st_imp called=$st_call reached-from-startup=$st_init)." \
        "The forced-failure fixture has nothing to discriminate on a tree" \
        "that never asks, so this is a finding about the tree and not about" \
        "the instrument"
elif [ "$fixture_ok" != 1 ]; then
    gate 0 fips-self-tested-and-live "this tree runs OSSL_PROVIDER_self_test" \
        "$p_st time(s), but the forced-failure fixture run did not produce a" \
        "measurement (selftest_forced=$f_forced), so whether the result is" \
        "USED is unknown.  That is a fault in this instrument, not a finding" \
        "about this tree, and it is scored 0 rather than passed"
elif [ "$st_imp" = 1 ] && [ "$st_call" = 1 ] && [ "$st_init" = 1 ] \
        && [ "$p_st" -ge 1 ] && [ "$p_st_init" -ge 1 ] \
        && [ "$p_st_fips" -ge 1 ] && [ "$p_st_ok" -ge 1 ] \
        && [ "$p_fips_unearly" = 0 ] \
        && [ "$refused" = 1 ]; then
    gate 1 fips-self-tested-and-live "the engine self-tests the module it" \
        "loaded and acts on the answer -- OSSL_PROVIDER_self_test imported," \
        "called and reached from _cpri__InitCryptoUnits, $p_st call(s) at" \
        "run time ($p_st_init during startup, $p_st_fips of them on the FIPS" \
        "provider this engine loaded), $p_st_ok returning success, and the" \
        "module held for the whole run; and when the fixture run forced that" \
        "self test to fail the engine refused to start (ok=$f_ok" \
        "tpmfail=$f_tpmfail init_rc=$f_rc, $f_ops operation(s) attempted)"
else
    gate 0 fips-self-tested-and-live "OSSL_PROVIDER_self_test imported=$st_imp" \
        "called=$st_call reached-from-startup=$st_init; at run time $p_st" \
        "call(s) ($p_st_init during startup), on a provider this probe" \
        "watched loaded $p_st_known, on the FIPS one $p_st_fips, returning" \
        "success $p_st_ok, forced to fail $p_st_forced (reported, not gated" \
        "-- this run does not force it); FIPS unloads before" \
        "shutdown=$p_fips_unearly;" \
        "with the self test forced to fail the engine reported ok=$f_ok" \
        "tpmfail=$f_tpmfail init_rc=$f_rc and still attempted $f_ops" \
        "operation(s) -- refused=$refused"
fi
# ============================================================ behaviour ======
#
# Both corpora in full, one replay each, against exactly the recordings steps 1
# and 2 were scored on.  Nothing here is re-recorded: see "WHAT DOES NOT FLIP"
# above.
if [ "$driver_ok" = 1 ]; then
    run_engine
else
    rm -f "$WORK/engine.txt"
    vlog "engine: no driver, so all 1676 cases score as unanswered"
fi
run_e2e

eng_all=$(score_engine - engine-report)
e2e_all=$(score_e2e    - e2e-report)

CHECK_OWNER=03
# --- 7: a sequence state is still a value after the layers above move it.
#
# Step 03's seventh gate, re-asked as an rgate like the six above it.  It sits
# below the behaviour block rather than with them because it reads the engine
# report, and CHECK_OWNER goes back to 03 here for the same reason it was 03
# there: the detail record files a property under the step that first demanded
# it, and check_oracle_nop.py reads the inherited/introduced split out of that.
#
# The other six gates are bookkeeping, and bookkeeping is most of what a port
# to providers is: a context made and freed, a provider named, a fetch held,
# three accessors gone.  All six can be true of a tree that never solved what
# the instruction calls this step's whole difficulty -- the engine's hash
# sequence state can no longer be a copy of libcrypto's private bytes, because
# at 3.0 there are none to copy, and whatever replaces it still has to survive
# being relocated, duplicated and abandoned by callers that do not ask.
#
# Without this gate that tree collects the entire 0.52.  It loses only on
# behaviour -- one family to nothing takes worst_family to 0 -- and lands well
# above the floor for having done the paperwork.  So the thing the step is for
# gets a gate of its own, weighed against the whole of the paperwork rather
# than against one piece of it, and the cases that record the property are what
# it reads.
#
# Deliberately a re-reading of the corpus rather than a new measurement: these
# cases already say whether relocation works, they were recorded from the
# untouched baseline, and a second instrument asking the same question
# differently would be a second thing to keep true.  What the gate adds is
# weight, not evidence.
#
# Perfect, not "mostly": every one of them is a recording of what the baseline
# answered, and a representation that survives relocation answers all of them.
# A selection that comes back empty, or a report that did not replay the
# families it was drawn from, fails the gate and says which -- a mean over an
# empty set is trap 4 and a gate over one is the same mistake.
# Scored on the cases that actually relocate, duplicate or abandon a sequence
# state, not on whole families.  Two of the four families this gate first read
# are not that: `hashseq` is 45 one-shot hashes beside 51 sequences that run
# start-to-finish where they were started, and `e2ehashseq` drives
# HashSequenceStart/Update/Complete inside one process with no ContextSave in
# it.  Both are already in `behaviour`, and neither says anything about the
# property this gate is named for -- so a correct port could have lost the gate
# for dropping a one-shot SHA-1, and the gate's name would not have described
# what failed.  `e2eseqctx` has the same problem from the other end: about a
# quarter of it is context save and load in general -- object contexts, a
# tampered blob, a permanent handle -- which a port that never touches
# CpriHash.c can still break.
#
# `hashstate` has it too, and the first narrowing stopped one family short of
# seeing it.  135 cases carry that family name and 105 of them move a sequence
# state; the other 30 are 12 `hashafter` (a completed state is inert, which is
# a state machine and not a representation), 12 `hashcopy` (which copy a
# state with `sequence` 0) and 6 `hashexport`.  The copies are the reason this
# matters rather than a tidiness argument: what this step requires to be a
# value is a *sequence* state, and the golden leaves a one-shot state exactly
# what it was, a live EVP_MD_CTX * duplicated by EVP_MD_CTX_copy_ex.  A port
# that got the sequence representation right and broke one-shot copying would
# have failed the gate named for sequences.  It still loses those cases in
# `behaviour` and in `worst_family`, which is where they belong.
#
# The same reading takes four cases off the e2e side.  `ctx-head` saves a
# sequence and reads the 16-byte plaintext header of the blob -- seqno, saved
# handle, hierarchy -- and never loads it back, so it can neither fail for a
# representation reason nor pass for one.  `ctx-event-start` is a
# HashSequenceStart return code with no save in it.  `ctx-event-move`,
# `ctx-event-complete` and `ctx-event-pcr16` stay: the last two read the
# digests an event sequence produced *after* it was loaded back, which is
# exactly where a state that did not travel shows up.
#
# `ctx-load-again` was left out of the first narrowing and belongs in.  It
# saves a sequence, loads it back, and then loads the same blob a second time,
# and the corpus's own note on the scenario says why that is a representation
# question: the first copy is gone by the time the second load happens, so a
# state whose real contents live somewhere the blob does not name has nothing
# left to point at.  Both of its rows are rc=0 with a digest in them, so both
# can fail for the reason this gate is named for.  `ctx-hmac-*` comes in with
# them, and only because the corpus was re-recorded: those three rows used to
# be rc=708 and rc=388, error returns that said nothing about a state that
# travelled, because the driver misread the sequence handle out of a response
# that had one and no sequence ever started.  Each of them now starts an HMAC
# sequence under a key whose scheme matches the algorithm asked for, updates
# it, saves it, loads it back and completes it, and the digest on the row is
# the HMAC the baseline produced.  They are the only cases in either corpus
# where the state that travels carries a key beside its hash context, which is
# the half of HASH_OBJECT a port can get right for hashing and still get wrong
# here.
#
# What is left is the measurement: 105 + 116 = 221 cases, all of them
# recordings of what the baseline answered, and a representation that is still
# a value under memcpy answers all of them.  The denominator is still drawn
# from the whole of both families, so a report that skipped the cases this
# selection does not name still fails the completeness check below.
SEQ_ENG_RX='^hashstate/'
SEQ_ENG_OP_RX='^hashmove(2|3|drop|after)?$'
SEQ_E2E_RX='^e2eseqctx/'
SEQ_TAG_RX='^ctx-(move|fork-[ab]|chain|live-(orig|copy|load)|load-again|after-complete-(orig|copy)|event-(move|complete|pcr16)|hmac-(SHA1|SHA256|SHA384))(-|$)'

# seqsel <expected-file> <report-json> <id-regex> [<tag-rx>] [<cases> <op-rx>]
#        -> "<m> <t>"
#
# Three ways to narrow, because the two recordings say different things about
# their own cases.  The e2e recording carries the scenario in a `t=` field on
# the line, so a tag regex is enough.  The engine recording carries only the
# case id and the answer, and the operation lives beside the inputs in
# cases.txt -- which is the file the driver reads, ships in the same directory,
# and is a recording of the authoring run rather than of the candidate's.  So
# the engine side joins on it by case id.
#
# The denominator is read out of the shipped recording rather than out of the
# report, because a report is the candidate's run and a recording is not: a
# family that did not replay at all comes back with no mismatches in it, and a
# numerator over an empty selection is trap 4 with a different shape.  So the
# selection is made from the recording, the report is required to be complete
# over every family the selection draws from, and only then are the mismatches
# subtracted.
seqsel() {
    python3 - "$1" "$2" "$3" "${4:-}" "${5:-}" "${6:-}" <<'PYSS'
import json, re, sys
exp, rep, idrx = sys.argv[1], sys.argv[2], re.compile(sys.argv[3])
tagrx = re.compile(sys.argv[4]) if sys.argv[4] else None
oprx = re.compile(sys.argv[6]) if sys.argv[5] and sys.argv[6] else None
ops = {}
if oprx is not None:
    try:
        for line in open(sys.argv[5]).read().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            f = line.split()
            if len(f) >= 2:
                ops[f[0]] = f[1]
    except Exception:
        print("0 0"); sys.exit(0)
sel, fams = set(), {}
try:
    lines = open(exp).read().splitlines()
except Exception:
    print("0 0"); sys.exit(0)
for line in lines:
    line = line.strip()
    if not line or line.startswith("#"):
        continue
    cid = line.split()[0]
    fams[cid.split("/")[0]] = fams.get(cid.split("/")[0], 0) + 1
    if not idrx.search(cid):
        continue
    if tagrx is not None:
        m = re.search(r"\bt=(\S+)", line)
        if m is None or not tagrx.search(m.group(1)):
            continue
    if oprx is not None and not oprx.search(ops.get(cid, "")):
        continue
    sel.add(cid)
try:
    r = json.load(open(rep))
except Exception:
    print("0 %d" % len(sel)); sys.exit(0)
rfams = r.get("families", {})
for fam in {c.split("/")[0] for c in sel}:
    v = rfams.get(fam)
    if not isinstance(v, dict) or v.get("total") != fams[fam]:
        print("0 %d" % len(sel)); sys.exit(0)
bad = {m.get("case") for m in r.get("mismatches", [])}
print("%d %d" % (len(sel - bad), len(sel)))
PYSS
}

read -r seq_em seq_et <<<"$(seqsel "$LIB/expected.txt" \
                                   "$LOG/engine-report.json" "$SEQ_ENG_RX" \
                                   "" "$LIB/cases.txt" "$SEQ_ENG_OP_RX")"
read -r seq_xm seq_xt <<<"$(seqsel "$LIB/expected-e2e.txt" \
                                   "$LOG/e2e-report.json" "$SEQ_E2E_RX" \
                                   "$SEQ_TAG_RX")"
seq_note="relocated in the engine $seq_em/$seq_et, saved and loaded through"
seq_note="$seq_note the simulator $seq_xm/$seq_xt"
if [ "$seq_et" -ge 1 ] && [ "$seq_xt" -ge 1 ] \
        && [ "$seq_em" = "$seq_et" ] && [ "$seq_xm" = "$seq_xt" ]; then
    seq_ok=1
    rgate 1 sequences-are-values "a hash sequence state is still a value after" \
        "the layers above the engine move, duplicate and abandon it --" \
        "$seq_note"
else
    seq_ok=0
    rgate 0 sequences-are-values "the sequence state does not survive what the" \
        "layers above the engine do to it, or is not there to survive it --" \
        "$seq_note"
fi

breadth=$(mean "$eng_all" "$e2e_all")
read -r worst worst_name <<<"$(worst_family "$LOG/engine-report.json" \
                                            "$LOG/e2e-report.json")"
behaviour=$(mean "$breadth" "$worst")
# `migrated` is a weighted mean, for the same reason steps 03, 04 and 05's are.
# Two of the four gates are the bookkeeping of the module: that a configuration
# was loaded into the engine's own context and a provider named `fips` came
# back from it, and that both providers are released at shutdown.  Both are
# true of a tree that writes those four calls into _cpri__InitCryptoUnits and
# _cpri__StopCryptoUnits and changes nothing else -- which is to say, of a tree
# that has put a FIPS module into a context and then goes on fetching out of it
# with a NULL property query and never asking the module whether it works.
# Priced flat that tree collects 0.5 of `migrated`, and because a NULL query
# resolves to the module for everything the module implements it collects that
# 0.5 while its behaviour score falls -- so the paperwork would be paying for
# the damage.  So the two halves are weighed against each other: the two
# bookkeeping gates share 0.4 of `migrated` and the two that ask whether the
# module is used correctly share 0.6.
#
# The ordering the split relies on is asserted rather than assumed.  A gate
# added or reordered above without this line moving with it scores the family
# 0, which is visible, rather than silently weighting the wrong pair.
if [ ${#GATES[@]} -ne 4 ]; then
    vlog "gates: expected four, got ${#GATES[@]} -- scoring the family 0"
    migrated=0.000000
else
    migrated=$(awk -v p="$(mean "${GATES[@]:0:2}")" \
                   -v s="$(mean "${GATES[@]:2:2}")" \
                   'BEGIN {printf "%.6f", 0.4 * p + 0.6 * s}')
fi

# The same assertion for the inherited family.  Twenty-six: step 05's twenty-two
# -- eighteen from steps 01-03, which is their twenty less `ecdsa-sign-library`
# and `ecdsa-verify-library`, plus step 04's four -- and step 05's own four,
# demoted here.  Nothing is retired at this step; every gate steps 01-05 asked
# is still asked.
#
# A crashed rgate leaves the mean over a smaller set and reads as a HIGHER
# score, so the count is checked rather than trusted.  That matters more at
# this step than at any before it: gate 1 puts a second provider into the
# library context every one of those twenty-six inherited gates runs against,
# and a tree that got the property query wrong will be failing inherited gates
# for reasons that originate here.  Those failures are the point.  A count
# that quietly shrank would hide exactly the damage the step exists to price.
RGATES_EXPECTED=26
if [ ${#RGATES[@]} -ne "$RGATES_EXPECTED" ]; then
    vlog "rgates: expected $RGATES_EXPECTED, got ${#RGATES[@]} --" \
         "scoring the inherited family 0"
    retained=0.000000
else
    retained=$(rgates_mean)
fi

# Behaviour belongs to step 01 forever: the corpus is fixed and every later
# step must still reproduce it.
detail_families 01 "$LOG/engine-report.json" "$LOG/e2e-report.json"

# =============================================================== the reward ==
# Product scoring (user decision 2026-10-13): reward = T1 x T2, with
# T1 = migrated (this step's own gates) and T2 = preservation, the old
# weights of behaviour and of the retained gates of earlier steps,
# renormalised: (0.36 * behaviour + 0.10 * retained) / 0.46.  Was
# 0.36 * behaviour + 0.54 * migrated + 0.10 * retained.
reward=$(awk -v a="$behaviour" -v b="$migrated" -v c="$retained" \
             'BEGIN {printf "%.6f", b * ((0.36 * a + 0.10 * c) / 0.46)}')
vlog "product: T1 migrated $migrated x T2 (0.36 behaviour + 0.10 retained)/0.46 = $reward"


vlog "----------------------------------------------------------------"
vlog "engine  all=$eng_all"
vlog "e2e     all=$e2e_all"
vlog "worst family: $worst_name at $worst"
vlog "behaviour  breadth=$breadth  worst=$worst  -> $behaviour  (x0.36)"
vlog "migrated   ${#GATES[@]} gates, 2 paperwork + 2 substance" \
     " -> $migrated  (x0.54)"
vlog "retained   ${#RGATES[@]} gates from steps 01-05 -> $retained  (x0.10)"
vlog "----------------------------------------------------------------"

report "$reward"
