// Family "cong", checkpoint 04 (SRT v1.3.3). One group, and it is a GATE:
//
//   g05_congestion     inherited    slot 47: default, accepted names, refusal
//                                   of an unknown name, and the two spellings.
//
// ----------------------------------------------------------------- tiers
//
// g05_congestion is `inherited`, so it vetoes and earns nothing. deltas/04.md
// lists SRTO_SMOOTHER -> SRTO_CONGESTION under "Not discriminators -- do not
// score these": slot 47 behaves identically at v1.3.2 and v1.3.3, the change
// being a header-only rename with a working deprecated alias, and every
// assertion below reads back the same "live"/"file" at both tags. Scored, it
// paid a no-op continuation for a rename the candidate did not have to write.
// What it still does, as a gate, is forbid a REGRESSION: slot 47 is a public
// option at both tags and both of its spellings have to survive the rename.
//
// --------------------------------------------------- why its own family
//
// This group is the only place in the step that names SRTO_SMOOTHER, and that
// name cannot be guarded. It is an enumerator at v1.3.2 and a `static const
// SRT_SOCKOPT` at v1.3.3 -- never a macro at either tag -- so `#ifndef
// SRTO_SMOOTHER` is true in both headers, and a fallback `#define` would make
// "the two spellings are one option" compare 47 against the verifier's own
// definition of 47 and assert nothing. The reference is therefore a real
// compile dependency that no `#ifndef` can soften.
//
// A family is one translation unit, and one translation unit is all of its
// groups. While this group lived in `api` alongside g01..g04, a tree that
// implemented the peer idle timeout, the IPv6-only option, the epoll
// narrowing and the header-macro fix, and then renamed slot 47 without
// keeping the deprecated alias, failed to compile the whole family: it lost
// four scored groups it had earned, and -- this group being a gate -- the
// step. That is precisely the failure mode DRIVER_RULES.md, "Naming an
// identifier the candidate may not have", exists to prevent, and the note the
// `api` family used to carry said so while leaving the hazard standing.
//
// Isolated here, the price of dropping the alias is this family and nothing
// else: g01..g04 still build, still run and still score. The compile
// dependency is stated to the candidate rather than sprung on them --
// stepdefs/04/interface.md, which becomes TEST_INTERFACE.md section 5, says
// the deprecated spelling must still compile and names what it costs.
//
// ---------------------------------------------------------- traceability
//
// Every assertion comes from stepdefs/04/interface.md and stepdefs/04/scope.md,
// and each was measured against the released v1.3.3 and v1.3.2 libraries
// before it was written.

#include <srt/srt.h>

#include "stest.h"
#include "srtkit.h"

#include <cstring>
#include <string>

// ------------------------------------------------ option constants, restated
//
// DRIVER_RULES, "Naming an identifier the candidate may not have", rule 2.
// The value is EXPLICIT in the sense that matters: v1.3.3's SRT_SOCKOPT enum
// contains no preprocessor conditional of any kind (the conditional members
// arrive with the bonding options at v1.4.2), the last explicit anchor is
// SRTO_SNDKMSTATE = 40, and counting forward from it gives RCVKMSTATE 41,
// LOSSMAXTTL 42, RCVLATENCY 43, PEERLATENCY 44, MINVERSION 45, STREAMID 46,
// CONGESTION 47. stepdefs/04/scope.md states that number as contract. The
// static_assert re-derives it from the header wherever the header has it, so
// the number is checked rather than assumed.
//
// The outer #ifdef matters even though this family's PREVTAG is v1.3.2, which
// does ship <srt/version.h>: a bare `#if SRT_VERSION_VALUE >= ...` against a
// tree with no version header is not quietly false but a preprocessor syntax
// error, which would cost the family its group for a build reason.
#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 3, 3)
static_assert(SRTO_CONGESTION == 47, "restated SRTO_CONGESTION disagrees with <srt/srt.h>");
#endif
#endif

#ifndef SRTO_CONGESTION
#define SRTO_CONGESTION ((SRT_SOCKOPT)47)
#endif

// ------------------------------------------- the deprecated spelling of 47
//
// The one reference, captured once. It carries SRT_ATR_DEPRECATED at v1.3.3,
// so naming it warns BY CONSTRUCTION, and the pragma below is what silences
// that warning.
//
// THE PRAGMA IS LOAD-BEARING AND THIS IS A GATE FAMILY. A compile error here
// does not cost one assertion, it costs g05 and with it the step's gate, so
// what protects the reference is worth stating rather than leaving to be
// rediscovered. Measured at the Oracle prefix (dev/root/04), all four
// combinations:
//
//   pragma present, -Wall -Wextra -Werror : compiles clean
//   pragma present, -Wall -Wextra         : compiles clean
//   pragma absent,  -Wall -Wextra -Werror : FAILS -- "'SRTO_SMOOTHER' is
//                                           deprecated [-Werror=deprecated-
//                                           declarations]"
//   pragma absent,  -Wall -Wextra         : compiles, emits the warning
//
// So the safety comes from the PRAGMA, not from the shipped driver build
// happening not to pass -Werror: with the pragma the reference is safe under
// -Werror too, and without it the development compile line
// (tmp/<task>/dev/fam.sh, which does pass -Wall -Wextra -Werror) fails at
// once. Do not delete the pragma on the grounds that the shipped verifier
// sets neither flag -- that is true and beside the point.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static const SRT_SOCKOPT CONG_OPT_SMOOTHER = SRTO_SMOOTHER;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// ----------------------------------------------------------------- helpers

// srtkit.h has st_set_str but no string getter, and slot 47 is a string
// option. Kept local rather than added to drv/: no other family reads a
// string option, and drv/ headers must compile at every tag.
//
// The length convention is the inherited one, established at milestone 02 by
// SRTO_STREAMID and restated in TEST_INTERFACE.md section 5: srt_getsockflag
// reports strlen, not strlen+1, and srt_setsockflag is given strlen. A getter
// that reported the NUL would make the comparisons below fail.
static std::string cong_get_str(SRTSOCKET s, SRT_SOCKOPT o) {
    char buf[128];
    std::memset(buf, 0, sizeof buf);
    int n = (int)sizeof buf - 1;
    if (srt_getsockflag(s, o, buf, &n) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "getsockflag(%d) failed: %s", (int)o,
                 srt_getlasterror_str());
    if (n < 0 || n >= (int)sizeof buf)
        st_failf(__FILE__, __LINE__, "getsockflag(%d) reported length %d, out of range",
                 (int)o, n);
    return std::string(buf, (size_t)n);
}

static int cong_try_set_str(SRTSOCKET s, SRT_SOCKOPT o, const char* v) {
    return srt_setsockflag(s, o, v, (int)std::strlen(v));
}

// ======================================================== g05_congestion ===
//
// interface.md / the option table: "live" and "file" are the accepted names
// and "live" is the default. deltas/04.md: slot 47 behaves identically at
// v1.3.2 and v1.3.3, which is why this group is `inherited` and gates rather
// than scores.

SRT_CASE(g05_congestion, default_is_live) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    std::string v = cong_get_str(s, SRTO_CONGESTION);
    ST_REQUIRE_MSG(v == "live",
                   "congestion control (slot 47) on a fresh socket: expected \"live\", "
                   "got \"%s\"",
                   v.c_str());
    srt_close(s);
}

SRT_CASE(g05_congestion, accepts_live_and_file_and_reads_back) {
    const char* names[2] = {"file", "live"};
    for (int i = 0; i < 2; i++) {
        SRTSOCKET s = st_new_socket();
        ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
        int rc = cong_try_set_str(s, SRTO_CONGESTION, names[i]);
        ST_REQUIRE_MSG(rc != SRT_ERROR,
                       "set of congestion control (slot 47) to \"%s\": expected 0, got "
                       "rc=%d, error %d (%s)",
                       names[i], rc, srt_getlasterror(NULL), srt_getlasterror_str());
        std::string got = cong_get_str(s, SRTO_CONGESTION);
        ST_REQUIRE_MSG(got == names[i],
                       "congestion control read back after setting \"%s\": expected \"%s\", "
                       "got \"%s\"",
                       names[i], names[i], got.c_str());
        srt_close(s);
    }
}

// The negative. Its positive control is the accepted "file" set immediately
// before it, in the same case: a library that refuses every name cannot earn
// this.
SRT_CASE(g05_congestion, unknown_name_is_refused_and_value_kept) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    ST_REQUIRE_MSG(cong_try_set_str(s, SRTO_CONGESTION, "file") != SRT_ERROR,
                   "positive control: set of congestion control to \"file\" was refused "
                   "with error %d (%s); expected 0",
                   srt_getlasterror(NULL), srt_getlasterror_str());
    ST_REQUIRE_MSG(cong_get_str(s, SRTO_CONGESTION) == "file",
                   "positive control: congestion control did not read back \"file\" after "
                   "an accepted set");

    int rc = cong_try_set_str(s, SRTO_CONGESTION, "nosuchcongctl");
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "set of congestion control (slot 47) to an unknown name: expected -1, "
                   "got rc=%d (the unknown name was accepted)",
                   rc);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                   "set of congestion control to an unknown name: expected SRT_EINVPARAM "
                   "(%d), got %d (%s)",
                   (int)SRT_EINVPARAM, err, srt_strerror(err, 0));

    std::string kept = cong_get_str(s, SRTO_CONGESTION);
    ST_REQUIRE_MSG(kept == "file",
                   "after a refused set the option must keep its previous value: expected "
                   "\"file\", got \"%s\"",
                   kept.c_str());
    srt_close(s);
}

// interface.md: "Both spellings of the option -- the current one and the
// deprecated one kept for source compatibility -- are the same position and
// behave identically; the deprecated spelling must still compile."
//
// Two halves. (a) The deprecated spelling must still COMPILE: this file names
// SRTO_SMOOTHER once, near the top, so a tree that deleted the name does not
// build this family -- see the note there for why that price is confined to
// this one group. (b) It must denote the SAME SLOT and behave identically:
// the slot is checked below, then the option is set through one spelling and
// read through the other, in both directions. A tree that gave the deprecated
// spelling its own slot fails here even though both names compiled.
SRT_CASE(g05_congestion, both_spellings_are_one_option) {
    ST_REQUIRE_MSG((int)CONG_OPT_SMOOTHER == 47,
                   "the deprecated spelling of the congestion option denotes slot %d: "
                   "expected 47, the slot the current spelling occupies -- the two "
                   "spellings are required to be one option, not two",
                   (int)CONG_OPT_SMOOTHER);

    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    ST_REQUIRE_MSG(cong_try_set_str(s, CONG_OPT_SMOOTHER, "file") != SRT_ERROR,
                   "set of congestion control through the DEPRECATED spelling: expected 0, "
                   "got error %d (%s)",
                   srt_getlasterror(NULL), srt_getlasterror_str());
    std::string via_current = cong_get_str(s, SRTO_CONGESTION);
    ST_REQUIRE_MSG(via_current == "file",
                   "set \"file\" through the deprecated spelling, read through the current "
                   "one: expected \"file\", got \"%s\"",
                   via_current.c_str());

    ST_REQUIRE_MSG(cong_try_set_str(s, SRTO_CONGESTION, "live") != SRT_ERROR,
                   "set of congestion control through the CURRENT spelling: expected 0, "
                   "got error %d (%s)",
                   srt_getlasterror(NULL), srt_getlasterror_str());
    std::string via_deprecated = cong_get_str(s, CONG_OPT_SMOOTHER);
    ST_REQUIRE_MSG(via_deprecated == "live",
                   "set \"live\" through the current spelling, read through the deprecated "
                   "one: expected \"live\", got \"%s\"",
                   via_deprecated.c_str());
    srt_close(s);
}
