// Family "sockopt", checkpoint 04 (SRT v1.3.3). One group, and it is a GATE:
//
//   g13_optmatrix      inherited    the socket-option restriction matrix this
//                                   chain has carried since milestone 02:
//                                   which sets a connected socket refuses,
//                                   and how an undefined option id is refused.
//
// ----------------------------------------------------------------- tiers
//
// g13_optmatrix is `inherited`, so it vetoes and earns nothing, and the tier
// is measured rather than argued: every assertion below was run against the
// v1.3.2 install prefix (dev/noopref/04) and against the released v1.3.3
// (dev/root/04) before it was written, and the two runs agree line for line --
// ECONNSOCK for a post-connect set of slots 45, 49, 50 and 53, EINVOP for a
// get of an undefined id, EINVPARAM for a set of one. Nothing here changes at
// this milestone, so nothing here may be scored: scored, it would pay a
// continuation for work the candidate does not do.
//
// What it does instead is charge a REGRESSION that nothing else in the chain
// charges. WORKSPACE_HANDOFF.md names "the socket-option restriction matrix
// from milestone 02" among the things that must continue to work, and before
// this group that sentence was unbacked: the step's other gates are
// proof-of-life, transfer, recovery and encryption, `congestion` is about slot
// 47 alone, and the two option groups that do test pre/post-connect
// restrictions, g01 and g02, test them only on the two options this milestone
// ADDS. Elsewhere in the chain the matrix IS covered -- step 03 gates it (api
// g09_sockopt, 6 cases) and step 11 scores parts of it (sockopt
// g01_optlen_guard, g02_version_readonly, g03_payloadsize_connected) -- but a
// step is graded only by its own groups, so neither of those charges a tree
// that regressed the matrix HERE. Step 04 charged it nowhere.
//
// That hole matters at THIS step in particular, more than at any other in the
// chain, because this is the step whose whole job is surgery on the option
// enum: the candidate adds three members to it and renames a fourth. The
// compile-time pins in fam_api.cpp (SRTO_SNDKMSTATE == 40, SRTO_STREAMID ==
// 46) catch renumbering of the inherited slots. Nothing caught a tree that
// kept the numbering and broke the BEHAVIOUR behind it -- dropping the
// connected-socket guard while rearranging the setter's switch is an ordinary
// way to do that, and it is invisible to every other group in the step.
//
// --------------------------------------------------- why its own family
//
// Because a gate must not be hostage to a scored family's compile
// dependencies. A family is one translation unit and one translation unit is
// all of its groups, so a gate sharing a file with a scored group inherits
// every name that group needs. Dropped into `api`, this group would have been
// vetoing on `api`'s static_asserts: a candidate who numbered SRTO_IPV6ONLY
// something other than 54 while bumping the version macro fails that
// translation unit to compile, which today costs four scored groups -- the
// right price for a numbering mistake -- and with a gate in the file would
// have cost the entire step. Dropped into `cong`, it would have been vetoing
// on whether the deprecated spelling of slot 47 still exists, which has
// nothing to do with it.
//
// Alone here it has no compile dependency at all to be hostage to. Every
// option below is reached by casting its integer position to SRT_SOCKOPT, so
// this file names no SRT_SOCKOPT enumerator whatsoever and compiles against
// any tree that ships <srt/srt.h> -- which is the same technique, and the same
// reason, as the `api` family's, and it is doubly appropriate here: the
// positions ARE the thing under test. Naming the constants would have let a
// tree that renumbered the enum renumber the assertions with it.
//
// ---------------------------------------------------------- traceability
//
// stepdefs/04/interface.md (TEST_INTERFACE.md section 5, "The inherited
// option restriction matrix") states every value asserted below, and
// REQUIREMENTS.md RQ-04-30 is the frozen form. Both were written from the
// measurement described above, not from a document: no shipped document
// states this matrix, which is how it came to be uncovered in the first place.

#include <srt/srt.h>

#include "stest.h"
#include "srtkit.h"

// ------------------------------------------------ option positions, restated
//
// DRIVER_RULES, "Naming an identifier the candidate may not have", rule 2 --
// though rule 2 is not really what is going on here. These are not names the
// candidate may be missing; they are inherited options that exist at both tags
// under these very numbers. They are reached positionally because the contract
// under test is positional: TEST_INTERFACE.md section 5 states that positions
// 0-46 keep the values the previous milestone gave them and that the three new
// options take 47, 54 and 55, so a group asserting inherited behaviour must
// assert it at an inherited POSITION or it asserts nothing about numbering.
//
// Verified present and identically behaved at v1.3.2 and v1.3.3.
static const SRT_SOCKOPT OM_MINVERSION  = (SRT_SOCKOPT)45;  // SRTO_MINVERSION
static const SRT_SOCKOPT OM_PAYLOADSIZE = (SRT_SOCKOPT)49;  // SRTO_PAYLOADSIZE
static const SRT_SOCKOPT OM_TRANSTYPE   = (SRT_SOCKOPT)50;  // SRTO_TRANSTYPE
static const SRT_SOCKOPT OM_STRICTENC   = (SRT_SOCKOPT)53;  // SRTO_STRICTENC

// An id this specification does not define, at either tag. 99 is the same
// probe milestone 02's own `optslots` group used, kept deliberately: it is far
// above the highest defined position at this milestone (55) and far below
// anything a later milestone in this chain reaches, so it stays undefined for
// the whole chain and this group means the same thing wherever it is carried.
static const SRT_SOCKOPT OM_UNDEFINED = (SRT_SOCKOPT)99;

// The live-mode payload default and the one-MTU ceiling. The default is
// asserted as a RANGE rather than as the literal 1316 both tags report: the
// contract is "a positive default that fits in one path MTU", nothing in this
// milestone touches it, and pinning the exact figure would turn a gate -- a
// veto -- into a tripwire on a constant no document in this step states.
static const int OM_PLSIZE_MAX_MTU = 1500;
// Comfortably over one MTU, so it must be refused however the ceiling is
// computed from the configured MTU.
static const int OM_PLSIZE_TOO_BIG = 65535;

// ----------------------------------------------------------------- helpers

static int om_try_set(SRTSOCKET s, SRT_SOCKOPT o, int v) {
    srt_clearlasterror();
    return srt_setsockflag(s, o, &v, sizeof v);
}

static int om_try_get(SRTSOCKET s, SRT_SOCKOPT o, int* out) {
    int n = (int)sizeof(int);
    srt_clearlasterror();
    return srt_getsockflag(s, o, out, &n);
}

// A refusal is only evidence if the same operation succeeds somewhere. Every
// case below pairs each refusal with the set that is expected to work.
static void om_require_refused(SRTSOCKET s, SRT_SOCKOPT o, int v, int want_err,
                               const char* what, const char* state) {
    int rc = om_try_set(s, o, v);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "set of %s (slot %d) on a %s socket: expected -1, got rc=%d -- the "
                   "inherited restriction matrix refuses this set and milestone 04 does "
                   "not relax it",
                   what, (int)o, state, rc);
    ST_REQUIRE_MSG(err == want_err,
                   "set of %s (slot %d) on a %s socket: expected error %d (%s), got %d (%s)",
                   what, (int)o, state, want_err, srt_strerror(want_err, 0), err,
                   srt_strerror(err, 0));
}

// ========================================================= g13_optmatrix ===
//
// Inherited from milestone 02 and carried unchanged through 03 and 04.
// Measured at v1.3.2 and at v1.3.3; the two agree, which is why this group
// gates rather than scores.

// The pre/post-connect half of the matrix. Four inherited options that a fresh
// socket accepts and a connected socket refuses, each refusal paired with the
// accepting set on a fresh socket so that a library which refuses everything
// cannot earn the case.
SRT_CASE(g13_optmatrix, connected_socket_refuses_the_inherited_presets) {
    struct Probe { SRT_SOCKOPT opt; int value; const char* what; };
    const Probe probes[4] = {
        {OM_MINVERSION,  0x010300, "minimum peer version"},
        {OM_PAYLOADSIZE, 1200,     "declared payload size"},
        {OM_TRANSTYPE,   1,        "transmission type"},
        {OM_STRICTENC,   0,        "strict encryption"},
    };

    // Positive control, on a socket that is not connected to anything.
    for (int i = 0; i < 4; i++) {
        SRTSOCKET s = st_new_socket();
        ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
        int rc = om_try_set(s, probes[i].opt, probes[i].value);
        ST_REQUIRE_MSG(rc != SRT_ERROR,
                       "positive control: set of %s (slot %d) on a FRESH socket was "
                       "refused with error %d (%s); this set is required to succeed, and "
                       "the refusal asserted below means nothing without it",
                       probes[i].what, (int)probes[i].opt, srt_getlasterror(NULL),
                       srt_getlasterror_str());
        srt_close(s);
    }

    Link L;
    RelaySched sched;
    st_connect(L, sched);

    // The caller, which reached CONNECTED through srt_connect.
    for (int i = 0; i < 4; i++)
        om_require_refused(L.cal, probes[i].opt, probes[i].value, SRT_ECONNSOCK,
                           probes[i].what, "CONNECTED caller");

    // And the socket srt_accept returned, which reached CONNECTED by the other
    // route. A guard written only into the connect path passes the caller half
    // and fails here.
    om_require_refused(L.acc, OM_PAYLOADSIZE, 1200, SRT_ECONNSOCK,
                       "declared payload size", "CONNECTED accepted");
}

// The undefined-id half. The two directions are refused DIFFERENTLY, and that
// difference is the assertion: a get of an id the specification does not
// define is an unsupported operation, a set of the same id is a bad parameter.
// A tree that collapses both onto one error code fails this case while a tree
// that simply refuses both passes a weaker one.
SRT_CASE(g13_optmatrix, undefined_option_ids_are_refused_by_kind) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // Positive control on the same socket: a DEFINED inherited slot answers
    // both directions, so the refusals below are about the id and not about
    // the socket or the call.
    int v = -1;
    ST_REQUIRE_MSG(om_try_get(s, OM_PAYLOADSIZE, &v) != SRT_ERROR,
                   "positive control: get of the declared payload size (slot 49) failed "
                   "with error %d (%s); it is a defined inherited option and must answer",
                   srt_getlasterror(NULL), srt_getlasterror_str());
    ST_REQUIRE_MSG(om_try_set(s, OM_PAYLOADSIZE, 1200) != SRT_ERROR,
                   "positive control: set of the declared payload size (slot 49) on a "
                   "fresh socket failed with error %d (%s)",
                   srt_getlasterror(NULL), srt_getlasterror_str());

    int junk = -12345;
    int rc = om_try_get(s, OM_UNDEFINED, &junk);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "get of undefined option id 99: expected -1, got rc=%d -- an id this "
                   "specification does not define must not answer a get",
                   rc);
    ST_REQUIRE_MSG(err == SRT_EINVOP,
                   "get of undefined option id 99: expected SRT_EINVOP (%d), got %d (%s)",
                   (int)SRT_EINVOP, err, srt_strerror(err, 0));

    rc = om_try_set(s, OM_UNDEFINED, 1);
    err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "set of undefined option id 99: expected -1, got rc=%d -- an id this "
                   "specification does not define must not accept a set",
                   rc);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                   "set of undefined option id 99: expected SRT_EINVPARAM (%d), got %d "
                   "(%s) -- the two directions are refused with DIFFERENT codes and this "
                   "is the one that distinguishes them",
                   (int)SRT_EINVPARAM, err, srt_strerror(err, 0));

    srt_close(s);
}

// The value-range half, on the one inherited option that has a ceiling. A
// refused set must also leave the previous value standing: a setter that
// validates after assigning passes the error-code assertion and fails this.
SRT_CASE(g13_optmatrix, payload_size_keeps_its_default_its_ceiling_and_its_value) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    int dflt = -1;
    ST_REQUIRE_MSG(om_try_get(s, OM_PAYLOADSIZE, &dflt) != SRT_ERROR,
                   "get of the declared payload size (slot 49) on a fresh socket: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(dflt > 0 && dflt <= OM_PLSIZE_MAX_MTU,
                   "the declared payload size on a fresh live socket reads %d; the "
                   "inherited default is a positive size that fits within one path MTU "
                   "(1..%d)",
                   dflt, OM_PLSIZE_MAX_MTU);

    ST_REQUIRE_MSG(om_try_set(s, OM_PAYLOADSIZE, 1000) != SRT_ERROR,
                   "set of the declared payload size to 1000: expected 0, got error %d "
                   "(%s)",
                   srt_getlasterror(NULL), srt_getlasterror_str());
    int back = -1;
    ST_REQUIRE_MSG(om_try_get(s, OM_PAYLOADSIZE, &back) != SRT_ERROR,
                   "get of the declared payload size after an accepted set: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(back == 1000,
                   "declared payload size read back after setting 1000: expected 1000, "
                   "got %d",
                   back);

    int rc = om_try_set(s, OM_PAYLOADSIZE, OM_PLSIZE_TOO_BIG);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "set of the declared payload size to %d, far above one path MTU: "
                   "expected -1, got rc=%d -- the inherited ceiling is not removed at "
                   "this milestone",
                   OM_PLSIZE_TOO_BIG, rc);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                   "set of the declared payload size above the ceiling: expected "
                   "SRT_EINVPARAM (%d), got %d (%s)",
                   (int)SRT_EINVPARAM, err, srt_strerror(err, 0));

    int kept = -1;
    ST_REQUIRE_MSG(om_try_get(s, OM_PAYLOADSIZE, &kept) != SRT_ERROR,
                   "get of the declared payload size after a refused set: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(kept == 1000,
                   "after a refused set the declared payload size must keep its previous "
                   "value: expected 1000, got %d",
                   kept);

    srt_close(s);
}
