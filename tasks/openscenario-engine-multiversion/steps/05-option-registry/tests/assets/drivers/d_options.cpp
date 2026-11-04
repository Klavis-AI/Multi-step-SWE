// The runtime option registry, new at this release.
//
// SE_SetOption takes only a NAME (the option is set, not valued);
// SE_SetOptionValue takes a name and a string value. Both have a "Persistent"
// variant that survives an SE_Init/SE_Close cycle. Queries are
// SE_GetOptionSet (bool) and SE_GetOptionValue (const char*).
//
// usage: d_options <xosc>
//
// The reset cycles below run a REAL scenario file (SE_Init must succeed),
// because R2 promises the reset "as part of an SE_Close/SE_Init cycle" and
// says nothing about a failed load; a solution that resets only after a
// successful load is conforming and must pass.
#include <esminiLib.hpp>
#include <esminiRMLib.hpp>
#include <cstdio>
#include <cstring>

static int eq(const char* a, const char* b) {
    return (a && b && strcmp(a, b) == 0) ? 1 : 0;
}

// GetOptionValue returns a pointer into ONE shared static buffer, filled by a
// function-local static inside the lib. That makes `printf("%s", f(x) ? f(x) :
// "(null)")` a trap -- both calls run before the argument is read -- and every
// value print below therefore takes the pointer once, into a local.
//
// It also means RM_GetOptionValue's value is NOT usable as ground truth, and
// nothing here reads an RM_ value. Measured on the built v2.44.2 tree: after
// `RM_SetOptionValue("o1","v1")` the getter returns "v1", but after storing
// further values under other names every subsequent RM_ read -- including a
// read of the very name just stored -- comes back "v1" as long as the first
// read happened. `RM_GetOptionValue("o2")` right after
// `RM_SetOptionValue("o2","v2")` answers "v1". The lib assigns the buffer with
// `static std::string val = GetOptionArg(name);`, whose initialiser runs once
// for the life of the process. RM_GetOptionSet and the SE_ side are unaffected,
// so the RM_ assertions below are flag assertions and the value round-trip is
// checked on the SE_ side, which does assign the buffer on every call.

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <xosc>\n", argv[0]);
        return 1;
    }
    const char* osc = argv[1];

    // R2 says the RM_ entry points carry "the same signatures and semantics"
    // as the SE_ ones, and R05.21's row is "RM option store with the semantics
    // of R05.4". Only RM_SetOption and RM_GetOptionSet were reached before, so
    // the value and persistent variants are exercised here -- with NO scenario
    // loaded, which is what the "and no scenario loaded" clause of the row asks
    // for, and which the pair below is the only place able to check: everything
    // later in this file runs after a load.
    //
    // The order is load-bearing. These two names are set BEFORE the
    // persistence cycle further down, so a build whose RM_ persistent variant
    // is really non-persistent is caught there, in the "persist" prints.
    printf("rm_pre_set=%d\n", RM_GetOptionSet("verifier_rm_noinit") ? 1 : 0);
    RM_SetOptionValue("verifier_rm_noinit", "alpha");
    printf("rm_set=%d\n", RM_GetOptionSet("verifier_rm_noinit") ? 1 : 0);
    // RM_SetOptionValue's own return code is the one reliable value-side signal
    // on the RM_ path (see the header note on the getter's cached buffer): 0
    // means the value was stored, -1 means the option was not registered with
    // an argument. The value itself is read back through SE_GetOptionValue,
    // which re-fills its buffer on every call.
    // Printed RAW. R2 pins the SUCCESS code exactly (`0`) and only the SIGN of a
    // failure, so the success side is asserted with need_kv 0 and the failure
    // side below with need_neg -- neither of which can read a return code that
    // has already been collapsed to 0/1 by this driver.
    printf("rm_set_rc=%d\n", RM_SetOptionValue("verifier_rm_rcprobe", "beta"));
    printf("rm_shared_se_sees=%d\n", SE_GetOptionSet("verifier_rm_noinit") ? 1 : 0);
    { const char* v2 = SE_GetOptionValue("verifier_rm_noinit"); printf("rm_shared_se_val=%s\n", v2 ? v2 : "(null)"); }
    RM_SetOptionPersistent("verifier_rm_keep");
    RM_SetOptionValuePersistent("verifier_rm_keepval", "77");
    printf("rm_keep_before=%d\n", RM_GetOptionSet("verifier_rm_keep") ? 1 : 0);
    // UnsetOption only acts "on a pure option, without arguments" (its own
    // comment in CommonMini.cpp, and the guard is `option->opt_arg_.empty()`).
    // A VALUED option is therefore left set and keeps its value, at both tags.
    // That is measured behaviour, not an assumption: `RM_UnsetOption` on a
    // valued name prints rm_unset=1 below on v2.44.2. The clearing behaviour is
    // already graded on the SE_ side where the documented case lives
    // (`verifier_flag`, a pure option), and nothing here asserts the valued
    // case either way -- a candidate is not being asked to change it.
    RM_SetOption("verifier_rm_pure");
    RM_UnsetOption("verifier_rm_pure");
    printf("rm_pure_unset=%d\n", RM_GetOptionSet("verifier_rm_pure") ? 1 : 0);

    // Before anything is set, the store reports unset.
    printf("pre_set=%d\n", SE_GetOptionSet("verifier_flag") ? 1 : 0);
    { const char* v4 = SE_GetOptionValue("verifier_flag"); printf("pre_val=%s\n", v4 ? v4 : "(null)"); }

    // A boolean-ish option: set by name alone.
    SE_SetOption("verifier_flag");
    printf("flag_set=%d\n", SE_GetOptionSet("verifier_flag") ? 1 : 0);

    // A valued option.
    SE_SetOptionValue("verifier_num", "42.5");
    printf("num_set=%d\n", SE_GetOptionSet("verifier_num") ? 1 : 0);
    { const char* v5 = SE_GetOptionValue("verifier_num"); printf("num_val=%s\n", v5 ? v5 : "(null)"); }

    SE_SetOptionValue("verifier_word", "hello");
    { const char* v6 = SE_GetOptionValue("verifier_word"); printf("word_val=%s\n", v6 ? v6 : "(null)"); }
    // Round-trip must be checked here, immediately after the set: the two
    // SE_Init/SE_Close cycles further down deliberately clear this
    // non-persistent option, so checking it after them would test the reset
    // behaviour instead of the round-trip.
    printf("roundtrip=%d\n", eq(SE_GetOptionValue("verifier_word"), "hello"));

    // Unset clears it.
    SE_UnsetOption("verifier_flag");
    printf("flag_after_unset=%d\n", SE_GetOptionSet("verifier_flag") ? 1 : 0);

    // The one REJECTION R2 documents: storing a value under a name that was
    // already registered as a pure presence flag with no argument slot. R2 says
    // SetOption registers a name it has not seen before exactly that way, so the
    // two lines below are the documented failure, reached with nothing but the
    // entry points R2 itself defines. Only the SIGN is printed on -- R2 pins the
    // sign, not the magnitude -- and the value-side success probe above proves
    // the same call answers 0 on a name it registers itself, so a build that
    // returned a constant could not satisfy both.
    SE_SetOption("verifier_pureflag");
    printf("pureflag_reject_rc=%d\n", SE_SetOptionValue("verifier_pureflag", "x"));
    // ...and the same on the mirrored RM_ entry point, which R2 gives the same
    // convention and the same failure.
    RM_SetOption("verifier_rm_pureflag");
    printf("rm_pureflag_reject_rc=%d\n", RM_SetOptionValue("verifier_rm_pureflag", "x"));
    // Nothing is asserted about what GetOptionValue answers for a name whose
    // store was rejected: R2 says "nullptr if the option has no value" and this
    // release answers an empty string there, so the edge is a disagreement
    // between contract and artifact that this step does not ask anyone to fix.

    // Non-persistent options are dropped by a scenario run.
    SE_SetOption("verifier_ephemeral");
    SE_SetOptionValue("verifier_ephemval", "7");
    printf("ephem_before=%d\n", SE_GetOptionSet("verifier_ephemeral") ? 1 : 0);
    int init1 = SE_Init(osc, 0, 0, 0, 0);
    printf("init1_ok=%d\n", init1 == 0 ? 1 : 0);
    SE_Close();
    printf("ephem_after=%d\n", SE_GetOptionSet("verifier_ephemeral") ? 1 : 0);
    printf("ephemval_after=%d\n", SE_GetOptionSet("verifier_ephemval") ? 1 : 0);

    // Persistent options survive it.
    SE_SetOptionPersistent("verifier_keep");
    SE_SetOptionValuePersistent("verifier_keepval", "99");
    int init2 = SE_Init(osc, 0, 0, 0, 0);
    printf("init2_ok=%d\n", init2 == 0 ? 1 : 0);
    SE_Close();
    printf("keep_after=%d\n", SE_GetOptionSet("verifier_keep") ? 1 : 0);
    { const char* v7 = SE_GetOptionValue("verifier_keepval"); printf("keepval_after=%s\n", v7 ? v7 : "(null)"); }
    // The RM_ persistent pair was set before these two cycles ran, so the
    // lifecycle clause of R05.21 is checked on the RM_ side too rather than
    // only on the SE_ side. What is asserted is the SET FLAG, not the value:
    // RM_GetOptionValue is unusable here for the reason recorded at the top of
    // this file, and `rm_shared_se_keep` proves the value side a second way --
    // if the RM_ persistent store had been dropped by the cycle, the shared
    // store would not still be reporting that name set.
    printf("rm_keep_after=%d\n", RM_GetOptionSet("verifier_rm_keep") ? 1 : 0);
    printf("rm_keepval_after=%d\n", RM_GetOptionSet("verifier_rm_keepval") ? 1 : 0);
    // ...and the VALUE the RM_ persistent write stored, read back through the
    // SE_ getter. R2 says the two stores are the same store, and the SE_ getter
    // re-fills its buffer on every call (the RM_ one does not -- see the note at
    // the top of this file), so this is the one reliable way to show that
    // RM_SetOptionValuePersistent stored the STRING and not merely the flag.
    // Without it a build whose RM_ persistent path kept the name and dropped
    // the value would still pass every line above.
    { const char* v8 = SE_GetOptionValue("verifier_rm_keepval"); printf("rm_keepval_after_val=%s\n", v8 ? v8 : "(null)"); }
    printf("rm_shared_se_keep=%d\n", SE_GetOptionSet("verifier_rm_keep") ? 1 : 0);

    // The store is process-global, shared with the road manager.
    RM_SetOption("verifier_rmflag");
    printf("shared_se_sees=%d\n", SE_GetOptionSet("verifier_rmflag") ? 1 : 0);
    SE_SetOption("verifier_seflag");
    printf("shared_rm_sees=%d\n", RM_GetOptionSet("verifier_seflag") ? 1 : 0);

    printf("survived=1\n");
    return 0;
}
