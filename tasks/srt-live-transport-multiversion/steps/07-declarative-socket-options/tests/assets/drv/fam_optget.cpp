// Family "optget", checkpoint 07 (SRT v1.4.3). Five scored groups, all `new`:
//
//   g01_latency_defaults  the getter on an unconnected socket reports the
//                         CONFIGURATION: 120 / 120 / 0 for RCVLATENCY /
//                         LATENCY / PEERLATENCY, and afterwards whatever was
//                         last accepted into each of them.
//   g02_bool_width        a bool option's getter writes one byte and narrows
//                         *optlen to 1 even when four bytes are offered, and
//                         SRTO_TLPKTDROP set to 0 reads back 0.
//   g03_config_readback   the key-refresh pair and the retransmission-algorithm
//                         selector are readable on a fresh socket, and
//                         SRTO_MININPUTBW exists, reads and writes.
//   g04_get_buflen        a get whose declared length is below the option's own
//                         width is refused with SRT_EINVPARAM, writes nothing,
//                         and does not update *optlen.
//   g05_clock_type        srt_clock_type is EXPORTED from the shared library and
//                         returns exactly SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY.
//
// ---------------------------------------------------------------------------
// Tier: every group here is `new` and SCORED (meta.env E2E_TIER=new; the gate lives in the
// `live` family, not this one). There is therefore no `inherited` or
// `peripheral` group in this file, and no group must be earned by a candidate
// who stopped at v1.4.2. DRIVER_RULES' "dlsym the function" half IS used, for
// srt_clock_type, because g05's subject is the exported symbol itself; the
// "restate the identifier" half is used for SRTO_MININPUTBW, and the two
// paragraphs below state exactly what that restatement does and does not buy.
//
// What the restatement below protects, precisely -- the note that stood here
// before was wrong about it. This translation unit is compiled against the
// header the CANDIDATE installed, and an enumerator is never a preprocessor
// macro, so `#ifndef SRTO_MININPUTBW` is true whenever the header does not
// #define the name, which is every header, whether or not it declares the
// enumerator. The guard therefore does NOT rescue a candidate at this milestone
// who omitted the enumerator or gave it another number: at
// SRT_VERSION_VALUE >= 1.4.3 the static_assert fires first and the whole family
// fails to build. That outcome is intended and it is STATED -- TEST_INTERFACE.md
// section 5's names table now marks SRTO_MININPUTBW and
// SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY **required to compile** for exactly this
// reason, and "Constants whose numeric value the verifier pins" gives the value
// 38 and says in as many words that an implementation which
// numbers it differently fails to compile rather than failing an assertion.
//
// What the guard does protect is the PREVIOUS milestone's header. A no-op
// continuation installs v1.4.2's <srt/srt.h>, which has no SRTO_MININPUTBW at
// all and reports SRT_VERSION_VALUE = 1.4.2: the version test suppresses the
// static_assert, the #define supplies the enumerator, and the family COMPILES
// there and fails g01-g05 on assertions naming actual versus expected instead
// of losing all five to a build error. Measured, not assumed:
// `dev/fam-at.sh noopref 07 fam_optget.cpp` builds and lists all five groups.
// The static_assert is what keeps the restatement honest at this milestone --
// without it a drifting value would silently address the neighbouring option
// and a case would pass or fail for a reason that has nothing to do with what
// it claims to measure.
//
// ---------------------------------------------------------------------------
// Traceability. Every assertion comes from stepdefs/07/interface.md (which
// ships to the candidate as TEST_INTERFACE.md section 5) or from
// stepdefs/07/scope.md; the deltas/07.md row is named per case where one
// applies. Where interface.md corrects the pinned upstream documentation, this
// file follows the CORRECTION, not the document:
//
//   * the option table describes SRTO_RCVLATENCY's live default as "120 ms
//     (depicted as 0)" and README/API-socket-options.md says a four-byte get of
//     a boolean option must fail. interface.md states the measured behaviour
//     (`120` and success-with-*optlen=1 respectively) and the brief says a
//     correction to the documentation is a correction to reality. Measured
//     against the Oracle by these groups.
//
// ---------------------------------------------------------------------------
// Deliberately NOT asserted, and why:
//
//   * D1, D2 (`srt_strerror` table and the ignored `errnoval`). Both are
//     `certain` rows in deltas/07.md but neither is named by any of the five
//     group descriptions in scored.txt, and this family is `optget`. Scoring
//     them here would score behaviour the candidate was never shown for these
//     groups. Reported to the orchestrator rather than added.
//   * The *value* a string option lands in an oversized buffer beyond `*optlen`
//     (D8, get side). interface.md says to "compare exactly *optlen bytes rather
//     than relying on a terminator being present", so a case asserts `*optlen`
//     and the first `*optlen` bytes and says nothing about byte `*optlen`.
//     v1.4.3's srtcore/core.cpp does `strcpy` there and therefore writes a NUL
//     one past the reported length; that is incidental, undocumented, and the
//     sort of thing a correct implementation is free to change.
//   * Every `SRT_EINVOP` vs `SRT_EINVPARAM` distinction outside the five
//     descriptions. deltas/07.md D5's 5000 codes are the v1.4.2 side of a
//     discriminator, not something this milestone promises.
//
// No case here uses a wall-clock sleep, no case opens a socket it does not
// close, and every case is a pure option-layer operation on a local socket:
// this family never binds, listens, connects or moves a byte, so no case can
// inherit a timing dependency from the relay or the scheduler.
#include "stest.h"
#include "srtkit.h"

#include <dlfcn.h>

#include <cstdint>
#include <cstring>
#include <string>

// ------------------------------------------------- SRTO_MININPUTBW, restated
//
// v1.4.3's <srt/srt.h> writes it explicitly -- `SRTO_MININPUTBW = 38,` -- which
// is the case DRIVER_RULES rule 2 prefers, because an explicit value cannot be
// shifted by a preprocessor conditional elsewhere in the enum. The enum here
// DOES contain one (`#if ENABLE_EXPERIMENTAL_BONDING` for the three group
// options), which is exactly why an implicit member must not be restated; 38
// sits before that block, so the conditional cannot move it, and the
// static_assert re-derives the number from the header at every tag from this
// milestone on. The number itself is a stated part of the interface, not a
// guess: TEST_INTERFACE.md section 5, "Constants whose numeric value the
// verifier pins".
//
// The outer #ifdef is not decoration. SRT_VERSION_VALUE comes from
// <srt/version.h>, which is itself new at v1.3.0 and is not included by
// <srt/srt.h> at every tag; a bare `#if SRT_VERSION_VALUE >= ...` where both
// names expand to nothing is not a quietly-false condition, it is a
// preprocessor syntax error that kills the translation unit before the compiler
// sees a line of it. Nesting costs nothing where the macro does exist.
#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 3)
static_assert(SRTO_MININPUTBW == 38, "restated SRTO_MININPUTBW disagrees with <srt/srt.h>");
#endif
#endif

#ifndef SRTO_MININPUTBW
#define SRTO_MININPUTBW ((SRT_SOCKOPT)38)
#endif

// The latency default the g01 cases compare against is named by the constant
// below rather than by the literal 120, so that a candidate who renamed the
// enumerator is still measured. That alone would be self-referential -- a
// candidate who redefined the live default to 200 and reported 200 everywhere
// would agree with itself -- so the constant's own value is pinned here, as
// TEST_INTERFACE.md section 5 requires (120 ms on a fresh socket). It is
// declared in <srt/srt.h> at this milestone's tag and at the previous
// milestone's -- both the tags this family is measured against -- so no
// version guard is needed.
static_assert(SRT_LIVE_DEF_LATENCY_MS == 120,
              "restated SRT_LIVE_DEF_LATENCY_MS disagrees with <srt/srt.h>");

// SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY is new at this milestone too, and more
// thoroughly absent than SRTO_MININPUTBW: v1.4.2's <srt/sync.h> defines no
// SRT_SYNC_CLOCK at all and its <srt/srt.h> has no srt_clock_type. Naming the
// identifier bare would therefore be a compile error at the previous prefix
// *in the whole family*, which loses g01-g04's assertion-level discrimination
// along with g05 -- the failure mode the driver note in scored.txt exists to
// prevent. Same guard shape and same reasoning as the option above; the
// static_assert is what makes the restatement safe, and it fires at this
// milestone where the header can speak.
#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 3)
static_assert(SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY == 4,
              "restated SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY disagrees with <srt/srt.h>");
#endif
#endif

#ifndef SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY
#define SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY 4
#endif

// The declaration itself, for the one case that reads the symbol through the
// header rather than through dlsym. At the previous prefix there is no
// declaration to read, so the case falls back to the resolved pointer and says
// so in its assertion message instead of failing to build.
#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 3)
#define OG_HAVE_CLOCK_TYPE_DECL 1
#endif
#endif

// --------------------------------------------------------------- error names
//
// A failure message that prints "expected 5003, got 0" makes the reviewer open
// the header; one that prints the enumerator does not. Only the codes these
// five groups can actually reach are named.
static const char* og_errname(int e)
{
    switch (e)
    {
    case SRT_SUCCESS:    return "SRT_SUCCESS";
    case SRT_EINVPARAM:  return "SRT_EINVPARAM";
    case SRT_EINVOP:     return "SRT_EINVOP";
    case SRT_EINVSOCK:   return "SRT_EINVSOCK";
    case SRT_EBOUNDSOCK: return "SRT_EBOUNDSOCK";
    case SRT_ECONNSOCK:  return "SRT_ECONNSOCK";
    default:             return "<other SRT_ERRNO>";
    }
}

// ------------------------------------------------------------ get, raw
//
// srtkit's st_get_int asserts that the library reported back exactly
// sizeof(int). That is the right contract for its own callers and the wrong one
// for every group in this file: g02's whole subject is a getter that reports a
// WIDTH OTHER than the one it was offered, and g04's is a getter that refuses
// the width it was offered. So these groups drive srt_getsockflag directly and
// keep `*optlen` as an observation rather than as a precondition.
//
// The buffer is caller-supplied and is never resized here. `declared` is what
// goes in through *optlen; `got_len` is what comes back; the return value is
// srt_getsockflag's. No SRT state is inspected on the error path, so the caller
// names the code itself with srt_getlasterror.
struct OgGet {
    int rc;        // srt_getsockflag's return value
    int err;       // srt_getlasterror at that moment, meaningful when rc < 0
    int got_len;   // what the library wrote back into *optlen
};

static OgGet og_get(SRTSOCKET s, SRT_SOCKOPT o, void* buf, int declared)
{
    int n = declared;
    srt_clearlasterror();
    OgGet g;
    g.rc = srt_getsockflag(s, o, buf, &n);
    g.err = g.rc < 0 ? srt_getlasterror(NULL) : SRT_SUCCESS;
    g.got_len = n;
    return g;
}

// The same, for the int options. The buffer is zeroed first so a case that
// reads a field the library never wrote sees 0 rather than stack noise.
static OgGet og_get_int(SRTSOCKET s, SRT_SOCKOPT o, int* out)
{
    *out = -12345;
    return og_get(s, o, out, (int)sizeof *out);
}

// ------------------------------------------------------------------- cases

// ------------------------------------------------------------ g01_latency_defaults
//
// deltas/07.md D3. scored.txt: "on a socket that has been created but not
// bound, SRTO_RCVLATENCY and SRTO_LATENCY report 120 and SRTO_PEERLATENCY
// reports 0; setting any one of the three before bind makes srt_getsockopt
// report the value that was set, and setting SRTO_LATENCY sets all three".
//
// interface.md: "On an unconnected socket the getter reports the configuration
// ... and afterwards whatever was last accepted. SRTO_LATENCY ... its getter
// returns the receive-side value, so setting SRTO_LATENCY to 300 makes all
// three read 300, while setting SRTO_PEERLATENCY alone changes only that one."
//
// The default is named by SRT_LIVE_DEF_LATENCY_MS rather than by the literal
// 120: the constant is in <srt/srt.h> at every tag in this chain, so naming it
// costs nothing, and a candidate who redefines the live default fails the
// static_assert above rather than passing by agreeing with itself.

SRT_CASE(g01_latency_defaults, unbound_socket_reports_the_configured_defaults) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    int rcv = -1, lat = -1, peer = -1;
    OgGet g1 = og_get_int(s, SRTO_RCVLATENCY, &rcv);
    ST_REQUIRE_MSG(g1.rc == 0,
                   "srt_getsockflag(SRTO_RCVLATENCY) on a fresh socket returned %d (%s), "
                   "expected 0 with the live-mode receive latency",
                   g1.rc, og_errname(g1.err));
    OgGet g2 = og_get_int(s, SRTO_LATENCY, &lat);
    ST_REQUIRE_MSG(g2.rc == 0,
                   "srt_getsockflag(SRTO_LATENCY) on a fresh socket returned %d (%s), "
                   "expected 0 with the receive-side value",
                   g2.rc, og_errname(g2.err));
    OgGet g3 = og_get_int(s, SRTO_PEERLATENCY, &peer);
    ST_REQUIRE_MSG(g3.rc == 0,
                   "srt_getsockflag(SRTO_PEERLATENCY) on a fresh socket returned %d (%s), "
                   "expected 0 (TEST_INTERFACE.md section 5: the peer latency reports zero before "
                   "anything is set)",
                   g3.rc, og_errname(g3.err));

    ST_REQUIRE_MSG(rcv == SRT_LIVE_DEF_LATENCY_MS,
                   "SRTO_RCVLATENCY on a fresh unbound socket reported %d, expected %d "
                   "(SRT_LIVE_DEF_LATENCY_MS; TEST_INTERFACE.md section 5: on an "
                   "unconnected socket the getter reports the configuration)",
                   rcv, SRT_LIVE_DEF_LATENCY_MS);
    ST_REQUIRE_MSG(lat == SRT_LIVE_DEF_LATENCY_MS,
                   "SRTO_LATENCY on a fresh unbound socket reported %d, expected %d "
                   "(its getter returns the receive-side value)",
                   lat, SRT_LIVE_DEF_LATENCY_MS);
    ST_REQUIRE_MSG(peer == 0,
                   "SRTO_PEERLATENCY on a fresh unbound socket reported %d, expected 0 "
                   "(the peer latency has no live-mode default)",
                   peer);

    ST_REQUIRE_MSG(g1.got_len == (int)sizeof(int) && g2.got_len == (int)sizeof(int) &&
                       g3.got_len == (int)sizeof(int),
                   "the three latency getters reported *optlen %d/%d/%d, expected %d each: "
                   "the latency options are 32-bit, not bool",
                   g1.got_len, g2.got_len, g3.got_len, (int)sizeof(int));

    srt_close(s);
}

SRT_CASE(g01_latency_defaults, a_set_before_bind_is_what_the_getter_reports) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    st_set_int(s, SRTO_RCVLATENCY, 200);
    int rcv = -1, lat = -1, peer = -1;
    ST_REQUIRE_MSG(og_get_int(s, SRTO_RCVLATENCY, &rcv).rc == 0,
                   "srt_getsockflag(SRTO_RCVLATENCY) failed after the option was set");
    ST_REQUIRE_MSG(og_get_int(s, SRTO_LATENCY, &lat).rc == 0,
                   "srt_getsockflag(SRTO_LATENCY) failed after SRTO_RCVLATENCY was set");
    ST_REQUIRE_MSG(og_get_int(s, SRTO_PEERLATENCY, &peer).rc == 0,
                   "srt_getsockflag(SRTO_PEERLATENCY) failed after SRTO_RCVLATENCY was set");

    ST_REQUIRE_MSG(rcv == 200,
                   "SRTO_RCVLATENCY read back %d after being set to 200: a getter that "
                   "samples a runtime field returns 0 here",
                   rcv);
    ST_REQUIRE_MSG(lat == 200,
                   "SRTO_LATENCY read back %d after only SRTO_RCVLATENCY was set to 200, "
                   "expected 200 (TEST_INTERFACE.md section 5: its getter returns the "
                   "receive-side value)",
                   lat);
    ST_REQUIRE_MSG(peer == 0,
                   "SRTO_PEERLATENCY read back %d after only SRTO_RCVLATENCY was set to "
                   "200, expected 0: setting the receive latency is not setting the peer "
                   "latency",
                   peer);

    srt_close(s);
}

SRT_CASE(g01_latency_defaults, setting_latency_sets_all_three_and_peerlatency_alone_does_not) {
    // Two subjects in one case, deliberately: the second is the positive control
    // for the first. "Setting SRTO_LATENCY makes all three read 300" is only
    // evidence of the setter fanning out if setting the peer latency ALONE
    // leaves the other two where they were -- otherwise a getter that reports
    // the last value written to any of the three passes just as well.
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    st_set_int(s, SRTO_LATENCY, 300);
    int rcv = -1, lat = -1, peer = -1;
    ST_REQUIRE_MSG(og_get_int(s, SRTO_RCVLATENCY, &rcv).rc == 0, "get SRTO_RCVLATENCY failed");
    ST_REQUIRE_MSG(og_get_int(s, SRTO_LATENCY, &lat).rc == 0, "get SRTO_LATENCY failed");
    ST_REQUIRE_MSG(og_get_int(s, SRTO_PEERLATENCY, &peer).rc == 0, "get SRTO_PEERLATENCY failed");
    ST_REQUIRE_MSG(rcv == 300 && lat == 300 && peer == 300,
                   "after srt_setsockflag(SRTO_LATENCY, 300) the three latency getters "
                   "reported RCVLATENCY %d, LATENCY %d, PEERLATENCY %d; "
                   "TEST_INTERFACE.md section 5 says setting SRTO_LATENCY makes all three "
                   "read 300",
                   rcv, lat, peer);

    // Control: the same socket, one option, peer side only.
    st_set_int(s, SRTO_PEERLATENCY, 77);
    rcv = lat = peer = -1;
    ST_REQUIRE_MSG(og_get_int(s, SRTO_RCVLATENCY, &rcv).rc == 0, "get SRTO_RCVLATENCY failed");
    ST_REQUIRE_MSG(og_get_int(s, SRTO_LATENCY, &lat).rc == 0, "get SRTO_LATENCY failed");
    ST_REQUIRE_MSG(og_get_int(s, SRTO_PEERLATENCY, &peer).rc == 0, "get SRTO_PEERLATENCY failed");
    ST_REQUIRE_MSG(peer == 77,
                   "SRTO_PEERLATENCY read back %d after being set to 77",
                   peer);
    ST_REQUIRE_MSG(rcv == 300 && lat == 300,
                   "after srt_setsockflag(SRTO_PEERLATENCY, 77) the receive side moved: "
                   "RCVLATENCY %d, LATENCY %d, expected 300 and 300 -- TEST_INTERFACE.md "
                   "section 5 says setting SRTO_PEERLATENCY alone changes only that one",
                   rcv, lat);

    srt_close(s);
}

// ------------------------------------------------------------ g02_bool_width
//
// deltas/07.md D4. scored.txt: "srt_getsockopt for SRTO_SENDER, SRTO_TSBPDMODE,
// SRTO_TLPKTDROP, SRTO_DRIFTTRACER and SRTO_ENFORCEDENCRYPTION writes one byte
// and sets *optlen to 1 even when a four-byte buffer is offered, and
// SRTO_TLPKTDROP set to 0 before connect reads back as 0 in that one byte".
//
// interface.md: "It does not fail. It succeeds, writes exactly one byte at the
// front of the buffer, sets *optlen to 1, and leaves the remaining bytes as the
// caller left them. ... The options this applies to are the ones the table
// types as bool ... Zero the buffer before the call and compare only *optlen
// bytes."
//
// The five names are the milestone's own list, and the case asserts exactly the
// three things the description names -- the return value, *optlen, and the byte
// at the front -- plus the untouched tail, which is the difference between
// "wrote one byte" and "wrote four and then said one".

// A four-byte window onto an eight-byte buffer, so that "wrote exactly one
// byte" is checkable and "wrote four bytes" is visible rather than harmless.
static const int OG_BOOL_CANARY = 8;

// Everything a bool getter is expected to do, in one place. `opt` and `label`
// go into the failure message so a reviewer knows which of the five broke.
static void og_check_bool_width(SRTSOCKET s, SRT_SOCKOPT opt, const char* label)
{
    unsigned char buf[OG_BOOL_CANARY];
    std::memset(buf, 0xAA, sizeof buf);

    OgGet g = og_get(s, opt, buf, 4);
    ST_REQUIRE_MSG(g.rc == 0,
                   "srt_getsockflag(%s) with a four-byte buffer returned %d (%s), expected "
                   "0: TEST_INTERFACE.md section 5 states a four-byte get of a boolean "
                   "option succeeds",
                   label, g.rc, og_errname(g.err));
    ST_REQUIRE_MSG(g.got_len == 1,
                   "srt_getsockflag(%s) with a four-byte buffer set *optlen to %d, "
                   "expected 1 (sizeof(bool)): the library reports the option's own width, "
                   "not the width the caller offered",
                   label, g.got_len);
    ST_REQUIRE_MSG(buf[0] == 0 || buf[0] == 1,
                   "srt_getsockflag(%s) wrote byte %u, expected the boolean value 0 or 1",
                   label, (unsigned)buf[0]);
    for (int i = 1; i < OG_BOOL_CANARY; i++)
    {
        ST_REQUIRE_MSG(buf[i] == 0xAA,
                       "srt_getsockflag(%s) wrote byte %d of a four-byte get (value %u); "
                       "TEST_INTERFACE.md section 5 says exactly one byte is written and "
                       "the remaining bytes are left as the caller left them",
                       label, i, (unsigned)buf[i]);
    }
}

SRT_CASE(g02_bool_width, every_bool_option_narrows_optlen_to_one) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    og_check_bool_width(s, SRTO_SENDER, "SRTO_SENDER");
    og_check_bool_width(s, SRTO_TSBPDMODE, "SRTO_TSBPDMODE");
    og_check_bool_width(s, SRTO_TLPKTDROP, "SRTO_TLPKTDROP");
    og_check_bool_width(s, SRTO_DRIFTTRACER, "SRTO_DRIFTTRACER");
    og_check_bool_width(s, SRTO_ENFORCEDENCRYPTION, "SRTO_ENFORCEDENCRYPTION");

    srt_close(s);
}

SRT_CASE(g02_bool_width, one_byte_and_four_byte_gets_agree_on_the_value) {
    // The width changing must not change the VALUE. A getter that reports the
    // right width but reads the wrong field, or that truncates something wider
    // than a bool into the first byte, would satisfy the previous case's
    // "0 or 1" check and still be wrong. This case compares the two widths
    // against each other, which is the only thing that catches it, and uses a
    // set-then-get so the compared value is not a default that both reads could
    // be getting wrong in the same direction.
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    st_set_int(s, SRTO_TLPKTDROP, 0);
    st_set_int(s, SRTO_DRIFTTRACER, 0);

    const SRT_SOCKOPT opts[4] = {SRTO_TLPKTDROP, SRTO_DRIFTTRACER, SRTO_TSBPDMODE,
                                 SRTO_SENDER};
    const char* labels[4] = {"SRTO_TLPKTDROP", "SRTO_DRIFTTRACER", "SRTO_TSBPDMODE",
                             "SRTO_SENDER"};
    for (int i = 0; i < 4; i++)
    {
        unsigned char b1[OG_BOOL_CANARY], b4[OG_BOOL_CANARY];
        std::memset(b1, 0xAA, sizeof b1);
        std::memset(b4, 0xAA, sizeof b4);

        OgGet g1 = og_get(s, opts[i], b1, 1);
        ST_REQUIRE_MSG(g1.rc == 0 && g1.got_len == 1,
                       "srt_getsockflag(%s) with a one-byte buffer returned %d (%s) and "
                       "*optlen %d, expected 0 and 1",
                       labels[i], g1.rc, og_errname(g1.err), g1.got_len);
        OgGet g4 = og_get(s, opts[i], b4, 4);
        ST_REQUIRE_MSG(g4.rc == 0 && g4.got_len == 1,
                       "srt_getsockflag(%s) with a four-byte buffer returned %d (%s) and "
                       "*optlen %d, expected 0 and 1",
                       labels[i], g4.rc, og_errname(g4.err), g4.got_len);
        ST_REQUIRE_MSG(b1[0] == b4[0],
                       "srt_getsockflag(%s) reported byte %u at *optlen=1 and byte %u at "
                       "*optlen=4: the value must not depend on the buffer the caller "
                       "offered",
                       labels[i], (unsigned)b1[0], (unsigned)b4[0]);
    }

    srt_close(s);
}

SRT_CASE(g02_bool_width, tlpktdrop_set_to_zero_reads_back_zero) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // Positive control first, on a second socket, so that "reads back 0" cannot
    // be a getter that always answers 0: the same code reads back 1 for a
    // socket that was set to 1. Without this the case scores a stub.
    SRTSOCKET on = st_new_socket();
    ST_REQUIRE_MSG(on != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    st_set_int(on, SRTO_TLPKTDROP, 1);
    unsigned char b[OG_BOOL_CANARY];
    std::memset(b, 0xAA, sizeof b);
    OgGet gon = og_get(on, SRTO_TLPKTDROP, b, 4);
    ST_REQUIRE_MSG(gon.rc == 0 && gon.got_len == 1,
                   "control: srt_getsockflag(SRTO_TLPKTDROP) on a socket set to 1 returned "
                   "%d (%s) with *optlen %d, expected 0 and 1",
                   gon.rc, og_errname(gon.err), gon.got_len);
    ST_REQUIRE_MSG(b[0] == 1,
                   "control: SRTO_TLPKTDROP set to 1 before connect read back %u, expected "
                   "1 -- a getter that answers 0 to everything would otherwise pass the "
                   "case below",
                   (unsigned)b[0]);
    srt_close(on);

    st_set_int(s, SRTO_TLPKTDROP, 0);
    std::memset(b, 0xAA, sizeof b);
    OgGet g = og_get(s, SRTO_TLPKTDROP, b, 4);
    ST_REQUIRE_MSG(g.rc == 0 && g.got_len == 1,
                   "srt_getsockflag(SRTO_TLPKTDROP) after setting it to 0 returned %d (%s) "
                   "with *optlen %d, expected 0 and 1",
                   g.rc, og_errname(g.err), g.got_len);
    ST_REQUIRE_MSG(b[0] == 0,
                   "SRTO_TLPKTDROP set to 0 before connect read back %u in the one byte the "
                   "getter reports, expected 0",
                   (unsigned)b[0]);

    srt_close(s);
}

// ------------------------------------------------------- g03_config_readback
//
// deltas/07.md D5. scored.txt: "SRTO_KMREFRESHRATE, SRTO_KMPREANNOUNCE and
// SRTO_RETRANSMITALGO are readable on an unbound socket and report the stored 0
// rather than failing, and SRTO_MININPUTBW exists, reports 0 into an eight-byte
// buffer, accepts a set and reports the value that was set".
//
// interface.md, on the key-refresh pair: "On a fresh socket the getter answers
// 0, the number that is stored, not the number it is substituted by. The
// substitution happens where the value is used". So 0 is the assertion, not
// 16777216 and not 4096 -- and the case says so in its message, because the
// documentation's "0 - corresponds to 16777216 packets" is the reading that
// invites the mistake.
//
// The negative half of D5 -- at v1.4.2 all four gets are -1/SRT_EINVOP and the
// MININPUTBW set is -1 -- is what the positive half discriminates against, so
// the whole case is: a fresh socket answers these four, with these values. A
// library that refuses them fails at the rc check with the code in the message.
//
// SRTO_MININPUTBW is named through the restatement at the top of the file,
// which is pinned to the header by static_assert wherever the header has it,
// and to 38 by #define where it does not. The eight-byte buffer is not
// decoration either: the option is 64-bit, and interface.md says four bytes is
// too small for it (g04 turns that into a scored case), so a five-byte get
// would be refused rather than answered.

SRT_CASE(g03_config_readback, key_refresh_and_retransmit_algo_are_readable_on_a_fresh_socket) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    int kref = -1, kpre = -1, ralgo = -1;
    OgGet g1 = og_get_int(s, SRTO_KMREFRESHRATE, &kref);
    ST_REQUIRE_MSG(g1.rc == 0,
                   "srt_getsockflag(SRTO_KMREFRESHRATE) on a fresh unbound socket returned "
                   "%d (%s), expected 0: the key-refresh pair had no getter before this "
                   "milestone and is readable now",
                   g1.rc, og_errname(g1.err));
    OgGet g2 = og_get_int(s, SRTO_KMPREANNOUNCE, &kpre);
    ST_REQUIRE_MSG(g2.rc == 0,
                   "srt_getsockflag(SRTO_KMPREANNOUNCE) on a fresh unbound socket returned "
                   "%d (%s), expected 0",
                   g2.rc, og_errname(g2.err));
    OgGet g3 = og_get_int(s, SRTO_RETRANSMITALGO, &ralgo);
    ST_REQUIRE_MSG(g3.rc == 0,
                   "srt_getsockflag(SRTO_RETRANSMITALGO) on a fresh unbound socket returned "
                   "%d (%s), expected 0: the retransmission-algorithm selector is readable "
                   "from this milestone",
                   g3.rc, og_errname(g3.err));

    ST_REQUIRE_MSG(kref == 0,
                   "SRTO_KMREFRESHRATE on a fresh socket reported %d, expected 0: the "
                   "getter reports the stored sentinel, not the number of packets it "
                   "stands for (16777216)",
                   kref);
    ST_REQUIRE_MSG(kpre == 0,
                   "SRTO_KMPREANNOUNCE on a fresh socket reported %d, expected 0: the "
                   "getter reports the stored sentinel, not the number of packets it "
                   "stands for (4096)",
                   kpre);
    ST_REQUIRE_MSG(ralgo == 0,
                   "SRTO_RETRANSMITALGO on a fresh socket reported %d, expected the "
                   "default 0",
                   ralgo);

    ST_REQUIRE_MSG(g1.got_len == (int)sizeof(int) && g2.got_len == (int)sizeof(int) &&
                       g3.got_len == (int)sizeof(int),
                   "the three getters reported *optlen %d/%d/%d, expected %d each",
                   g1.got_len, g2.got_len, g3.got_len, (int)sizeof(int));

    srt_close(s);
}

SRT_CASE(g03_config_readback, mininputbw_exists_reads_zero_and_reads_back_a_set) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // Eight bytes, as the description says. The value is 64-bit, and a get that
    // understates the width is refused rather than truncated (g04).
    int64_t v = -1;
    OgGet g = og_get(s, SRTO_MININPUTBW, &v, (int)sizeof v);
    ST_REQUIRE_MSG(g.rc == 0,
                   "srt_getsockflag(SRTO_MININPUTBW) on a fresh unbound socket returned %d "
                   "(%s), expected 0: the option joins the table at this milestone",
                   g.rc, og_errname(g.err));
    ST_REQUIRE_MSG(g.got_len == (int)sizeof(int64_t),
                   "srt_getsockflag(SRTO_MININPUTBW) reported *optlen %d, expected %d: the "
                   "minimum input bandwidth is 64-bit",
                   g.got_len, (int)sizeof(int64_t));
    ST_REQUIRE_MSG(v == 0,
                   "SRTO_MININPUTBW on a fresh socket reported %lld, expected 0 (its "
                   "default)",
                   (long long)v);

    // The set, and the read-back that stops a getter which ignores the set from
    // passing the line above. 200000 is well above the 0 default and far below
    // any internal ceiling, so a refusal here is the library's own and not the
    // option's range.
    const int64_t want = 200000;
    srt_clearlasterror();
    if (srt_setsockflag(s, SRTO_MININPUTBW, &want, (int)sizeof want) == SRT_ERROR)
        st_failf(__FILE__, __LINE__,
                 "srt_setsockflag(SRTO_MININPUTBW, 200000) returned SRT_ERROR: %s "
                 "(expected 0)",
                 srt_getlasterror_str());

    v = -1;
    OgGet g2 = og_get(s, SRTO_MININPUTBW, &v, (int)sizeof v);
    ST_REQUIRE_MSG(g2.rc == 0,
                   "srt_getsockflag(SRTO_MININPUTBW) after the set returned %d (%s)",
                   g2.rc, og_errname(g2.err));
    ST_REQUIRE_MSG(v == want,
                   "SRTO_MININPUTBW read back %lld after being set to %lld, expected the "
                   "value that was set",
                   (long long)v, (long long)want);

    srt_close(s);
}

// -------------------------------------------------------------- g04_get_buflen
//
// deltas/07.md D8. scored.txt: "srt_getsockopt refuses with SRT_EINVPARAM and
// leaves every byte of the caller's buffer untouched when the declared length
// is smaller than the option's own width -- four bytes for the 64-bit
// SRTO_MAXBW or SRTO_INPUTBW, two bytes for the string SRTO_CONGESTION --
// while the same three reads with a large enough buffer succeed and report the
// value".
//
// interface.md: "It is refused with SRT_EINVPARAM, *optlen is not updated, and
// not one byte of the caller's buffer is touched -- no partial copy, no
// truncated string, no overrun past the declared length. ... For a string
// option the width is the length of the text: SRTO_CONGESTION on a fresh
// socket returns the four characters live and sets *optlen to 4, so a
// two-byte buffer is refused and an oversized one succeeds -- compare exactly
// *optlen bytes rather than relying on a terminator being present."
//
// The two halves are in ONE case per option group, because the negative is the
// whole point and a negative without its positive control scores a library that
// refuses every get. The three options share a case structure but not a case,
// so a candidate who gets the 64-bit guard right and the string guard wrong
// loses one case rather than the group -- and the group's case count is what
// test.sh checks, so all three must exist.

// Beyond the declared length, so that "wrote nothing" can be told apart from
// "wrote the right first bytes and something after them". v1.4.2 copies an
// unbounded amount here -- `strcpy` for the string options and a plain
// `int64_t` store for the bandwidth ones -- and the difference only shows if
// there is somewhere for it to show.
static const int OG_GUARD_BYTES = 32;
static const unsigned char OG_GUARD_FILL = 0x5C;

static void og_check_tail_untouched(const unsigned char* buf, int from, int to,
                                    const char* label)
{
    for (int i = from; i < to; i++)
    {
        ST_REQUIRE_MSG(buf[i] == OG_GUARD_FILL,
                       "srt_getsockflag(%s) wrote byte %d of the caller's buffer (value "
                       "0x%02X, expected 0x%02X): TEST_INTERFACE.md section 5 says a get "
                       "that is refused leaves every byte untouched, and that a get that "
                       "succeeds writes only *optlen bytes",
                       label, i, (unsigned)buf[i], (unsigned)OG_GUARD_FILL);
    }
}

SRT_CASE(g04_get_buflen, a_four_byte_get_of_the_64_bit_bandwidths_is_refused_and_writes_nothing) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    const SRT_SOCKOPT opts[2] = {SRTO_MAXBW, SRTO_INPUTBW};
    const char* labels[2] = {"SRTO_MAXBW", "SRTO_INPUTBW"};

    for (int i = 0; i < 2; i++)
    {
        unsigned char buf[OG_GUARD_BYTES];
        std::memset(buf, OG_GUARD_FILL, sizeof buf);

        OgGet g = og_get(s, opts[i], buf, 4);
        ST_REQUIRE_MSG(g.rc < 0,
                       "srt_getsockflag(%s) with a four-byte buffer returned %d, expected a "
                       "refusal: the option is 64-bit and four bytes is smaller than its "
                       "width (TEST_INTERFACE.md section 5)",
                       labels[i], g.rc);
        ST_REQUIRE_MSG(g.err == SRT_EINVPARAM,
                       "srt_getsockflag(%s) with a four-byte buffer failed with %d (%s), "
                       "expected %d (SRT_EINVPARAM)",
                       labels[i], g.err, og_errname(g.err), (int)SRT_EINVPARAM);
        ST_REQUIRE_MSG(g.got_len == 4,
                       "srt_getsockflag(%s) with a four-byte buffer left *optlen at %d, "
                       "expected the 4 that went in: TEST_INTERFACE.md section 5 says "
                       "*optlen is not updated when the get is refused",
                       labels[i], g.got_len);
        og_check_tail_untouched(buf, 0, OG_GUARD_BYTES, labels[i]);

        // The positive control: the SAME read, one width larger, succeeds.
        // Without this, a library that refuses every get of these two options
        // passes everything above.
        int64_t v = -1;
        OgGet g2 = og_get(s, opts[i], &v, (int)sizeof v);
        ST_REQUIRE_MSG(g2.rc == 0,
                       "control: srt_getsockflag(%s) with an eight-byte buffer returned %d "
                       "(%s), expected 0 and the value",
                       labels[i], g2.rc, og_errname(g2.err));
        ST_REQUIRE_MSG(g2.got_len == (int)sizeof(int64_t),
                       "control: srt_getsockflag(%s) with an eight-byte buffer reported "
                       "*optlen %d, expected %d",
                       labels[i], g2.got_len, (int)sizeof(int64_t));
    }

    srt_close(s);
}

SRT_CASE(g04_get_buflen, a_two_byte_get_of_the_congestion_string_is_refused_and_writes_nothing) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // What the fresh socket holds. interface.md states both of these outright:
    // the four characters `live`, and *optlen = 4. They are asserted here
    // because the two-byte refusal below is only about the width of the text --
    // if the string were shorter than two bytes, the refusal would be proving
    // something else.
    char full[OG_GUARD_BYTES];
    std::memset(full, OG_GUARD_FILL, sizeof full);
    OgGet gf = og_get(s, SRTO_CONGESTION, full, OG_GUARD_BYTES);
    ST_REQUIRE_MSG(gf.rc == 0,
                   "srt_getsockflag(SRTO_CONGESTION) with a %d-byte buffer returned %d (%s), "
                   "expected 0",
                   OG_GUARD_BYTES, gf.rc, og_errname(gf.err));
    ST_REQUIRE_MSG(gf.got_len == 4,
                   "srt_getsockflag(SRTO_CONGESTION) on a fresh socket reported *optlen %d, "
                   "expected 4: the declared width of a string option is the length of the "
                   "text, and a fresh socket is configured `live`",
                   gf.got_len);
    ST_REQUIRE_MSG(std::memcmp(full, "live", 4) == 0,
                   "srt_getsockflag(SRTO_CONGESTION) on a fresh socket wrote \"%.*s\", "
                   "expected \"live\" (TEST_INTERFACE.md section 5)",
                   4, full);
    // Exactly *optlen bytes were described, so everything after them is the
    // caller's. Note that v1.4.3's own getter writes a NUL ONE PAST the four
    // characters it reports (`strcpy` in the CONGESTION branch of
    // CUDT::getOpt), so byte 4 is NOT the caller's on a success path -- which
    // is why the check starts at index 4 + 1 rather than at 4. This is a real
    // discrepancy with interface.md's "writes only *optlen bytes" and is
    // reported rather than asserted; see the header's "Deliberately NOT
    // asserted" note.
    og_check_tail_untouched((const unsigned char*)full, 4 + 1, OG_GUARD_BYTES, "SRTO_CONGESTION");

    // The subject: two bytes, which is smaller than the four characters the
    // option needs.
    unsigned char small[OG_GUARD_BYTES];
    std::memset(small, OG_GUARD_FILL, sizeof small);
    OgGet g = og_get(s, SRTO_CONGESTION, small, 2);
    ST_REQUIRE_MSG(g.rc < 0,
                   "srt_getsockflag(SRTO_CONGESTION) with a two-byte buffer returned %d, "
                   "expected a refusal: the text is four characters long",
                   g.rc);
    ST_REQUIRE_MSG(g.err == SRT_EINVPARAM,
                   "srt_getsockflag(SRTO_CONGESTION) with a two-byte buffer failed with %d "
                   "(%s), expected %d (SRT_EINVPARAM): the documented error list for "
                   "srt_getsockopt names only SRT_EINVSOCK and SRT_EINVOP, and "
                   "TEST_INTERFACE.md section 5 records the measured code",
                   g.err, og_errname(g.err), (int)SRT_EINVPARAM);
    ST_REQUIRE_MSG(g.got_len == 2,
                   "srt_getsockflag(SRTO_CONGESTION) with a two-byte buffer left *optlen at "
                   "%d, expected the 2 that went in",
                   g.got_len);
    og_check_tail_untouched(small, 0, OG_GUARD_BYTES, "SRTO_CONGESTION");

    srt_close(s);
}

SRT_CASE(g04_get_buflen, the_refusal_is_the_width_rule_and_not_the_option) {
    // The two cases above could be passed by a library that refuses a
    // four-byte get of *everything*. This one pins the rule to the widths: on
    // the same fresh socket, four bytes is enough for a 32-bit option and not
    // enough for the 64-bit ones, and a get whose *optlen is larger than the
    // option needs succeeds rather than being refused for being wrong in the
    // other direction.
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // Positive control on the same width that was just refused for MAXBW: a
    // 32-bit option accepts four bytes and answers.
    int rcvlat = -1;
    OgGet ok = og_get(s, SRTO_RCVLATENCY, &rcvlat, 4);
    ST_REQUIRE_MSG(ok.rc == 0,
                   "control: srt_getsockflag(SRTO_RCVLATENCY) with a four-byte buffer "
                   "returned %d (%s), expected 0: four bytes is exactly a 32-bit option's "
                   "width, and the refusal above is about the width, not about four bytes",
                   ok.rc, og_errname(ok.err));
    ST_REQUIRE_MSG(rcvlat == SRT_LIVE_DEF_LATENCY_MS,
                   "control: SRTO_RCVLATENCY with a four-byte buffer reported %d, expected "
                   "%d",
                   rcvlat, SRT_LIVE_DEF_LATENCY_MS);

    // An OVERSIZED get of a 64-bit option succeeds, so the refusal is a floor
    // and not an exact-match requirement.
    int64_t v = -1;
    OgGet big = og_get(s, SRTO_MAXBW, &v, (int)sizeof v);
    ST_REQUIRE_MSG(big.rc == 0 && big.got_len == (int)sizeof(int64_t),
                   "srt_getsockflag(SRTO_MAXBW) with an eight-byte buffer returned %d (%s) "
                   "and *optlen %d, expected 0 and %d",
                   big.rc, og_errname(big.err), big.got_len, (int)sizeof(int64_t));

    // And the floor is real for a THIRD width, so this is a per-option table
    // and not "large enough for the largest option": three bytes is short for
    // the 64-bit pair and long enough for nothing else either, but four bytes
    // is the exact boundary the description names and is asserted above.
    unsigned char three[OG_GUARD_BYTES];
    std::memset(three, OG_GUARD_FILL, sizeof three);
    OgGet g3 = og_get(s, SRTO_MAXBW, three, 3);
    ST_REQUIRE_MSG(g3.rc < 0 && g3.err == SRT_EINVPARAM,
                   "srt_getsockflag(SRTO_MAXBW) with a three-byte buffer returned %d (%s), "
                   "expected a refusal with %d (SRT_EINVPARAM)",
                   g3.rc, og_errname(g3.err), (int)SRT_EINVPARAM);
    og_check_tail_untouched(three, 0, OG_GUARD_BYTES, "SRTO_MAXBW");

    srt_close(s);
}

// ------------------------------------------------------------- g05_clock_type
//
// deltas/07.md D14. scored.txt: "the installed shared library exports
// srt_clock_type with C linkage and calling it returns exactly 4,
// SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY, the identifier for the clock this build
// selects when neither the monotonic-clock nor the C++ sync build option is
// enabled".
//
// interface.md: "srt_clock_type must be an exported symbol with C linkage ...
// Derive it from the build options rather than hard-coding a constant."
//
// "Derive it from the build options" is what the <srt/srt.h> of this build
// does: SRT_SYNC_CLOCK is chosen there from ENABLE_MONOTONIC_CLOCK and
// ENABLE_CXX11_SYNC_CLOCK, and everything that branches on the choice in the
// library is what makes the observed 4 authoritative. The driver does not
// restate the choice -- that would be re-deriving it from an assumption about
// the build, not from the build -- it asserts the number the description
// names, and names the macro in the message so a build where the two disagree
// says so.
//
// Resolution is by dlsym against RTLD_DEFAULT, and that is the whole subject:
// what the milestone must do is EXPORT the function, so the question belongs to
// the shared object and not to the header. A driver that declared
// `extern "C" int srt_clock_type(void);` and called it directly would fail to
// LINK against a v1.4.2 library, and a link failure costs the family all five
// of its groups rather than costing this group its three cases; dlsym turns the
// same absence into an assertion that names what it looked for. A resolved
// symbol is an exported symbol.

// The memcpy conversion, not a cast: converting void* to a function pointer by
// cast is only conditionally supported and trips -Wpedantic under -Werror.
// Taken from fam_epoll.cpp, which resolves part of its surface the same way.
template <typename Fn>
static Fn og_dlsym(const char* name) {
    void* sym = dlsym(RTLD_DEFAULT, name);
    if (!sym) return 0;
    Fn fn;
    std::memcpy(&fn, &sym, sizeof fn);
    return fn;
}

typedef int (*og_clock_type_fn)(void);

SRT_CASE(g05_clock_type, the_symbol_is_exported_and_reports_the_posix_gettimeofday_clock) {
    // A negative control first, so the positive one below is known to be
    // capable of missing: this name exists at no milestone, and a resolver
    // that answers for everything would be caught here rather than letting the
    // real lookup pass for the wrong reason.
    og_clock_type_fn absent = og_dlsym<og_clock_type_fn>("srt_no_such_function_at_any_milestone");
    ST_REQUIRE_MSG(absent == 0,
                   "control: dlsym(RTLD_DEFAULT, \"srt_no_such_function_at_any_milestone\") "
                   "resolved to a function, so this build answers dlsym for names that do "
                   "not exist and the lookup below would prove nothing");

    og_clock_type_fn fn = og_dlsym<og_clock_type_fn>("srt_clock_type");
    ST_REQUIRE_MSG(fn != 0,
                   "dlsym(RTLD_DEFAULT, \"srt_clock_type\") found nothing: the library must "
                   "export srt_clock_type with C linkage, and this driver resolves it "
                   "rather than declaring it, so a symbol that is present only in a "
                   "header cannot satisfy this");

    const int got = fn();
    ST_REQUIRE_MSG(got == 4,
                   "srt_clock_type() returned %d, expected 4 "
                   "(SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY): the clock this build selects "
                   "when neither ENABLE_MONOTONIC_CLOCK nor ENABLE_CXX11_SYNC_CLOCK is "
                   "enabled",
                   got);
    ST_REQUIRE_MSG(got == SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY,
                   "srt_clock_type() returned %d but this build's <srt/srt.h> names %d as "
                   "SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY: the two must agree, and the "
                   "description fixes the value at exactly 4",
                   got, (int)SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY);
}

SRT_CASE(g05_clock_type, reaching_it_through_the_header_and_through_dlsym_agrees) {
    // Reading the same symbol two ways, where the header side is available: the
    // compiler needs a declaration to call it at all; dlsym needs the library to
    // carry it. A candidate who added only the declaration gets the first half
    // and not the second, and a candidate who changed the clock selection gets a
    // disagreement rather than a silent pass.
    //
    // At the previous prefix there is no declaration, so this case does not
    // exist there in the sense that matters: it still builds and still runs, and
    // it fails on the resolution below. That is deliberate -- a family that
    // fails to build takes g01-g04 with it.
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    og_clock_type_fn fn = og_dlsym<og_clock_type_fn>("srt_clock_type");
    ST_REQUIRE_MSG(fn != 0,
                   "dlsym(RTLD_DEFAULT, \"srt_clock_type\") found nothing, so there is no "
                   "exported symbol to compare a declaration against");

    const int via_dlsym = fn();

#ifdef OG_HAVE_CLOCK_TYPE_DECL
    const int via_header = srt_clock_type();
    ST_REQUIRE_MSG(via_header == via_dlsym,
                   "srt_clock_type() through the header returned %d and through dlsym "
                   "returned %d: the exported symbol and the declaration must be the same "
                   "function",
                   via_header, via_dlsym);
    ST_REQUIRE_MSG(via_header == 4,
                   "srt_clock_type() returned %d, expected exactly 4 "
                   "(SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY)",
                   via_header);
#else
    // No declaration to compare against: the exported symbol alone still has to
    // answer 4.
    ST_REQUIRE_MSG(via_dlsym == 4,
                   "srt_clock_type() through dlsym returned %d, expected exactly 4: this "
                   "prefix's <srt/srt.h> has no declaration to compare against, so the "
                   "exported symbol is the only witness",
                   via_dlsym);
#endif

    // The clock type is a property of the build, not of any socket, so it must
    // not vary with what has been created: the same answer after a second
    // socket and after the first is closed.
    SRTSOCKET s2 = st_new_socket();
    ST_REQUIRE_MSG(s2 != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(fn() == via_dlsym,
                   "srt_clock_type() returned %d with a second socket open, expected the "
                   "same %d: the clock is chosen at build time and does not depend on the "
                   "socket set",
                   fn(), via_dlsym);

    srt_close(s2);
    srt_close(s);
}

SRT_CASE(g05_clock_type, the_clock_is_the_one_every_library_symbol_agrees_on) {
    // The positive control for the whole group: the same resolution machinery
    // that finds srt_clock_type must find another exported function of the
    // same library, so a failing lookup above is about srt_clock_type and not
    // about RTLD_DEFAULT being unable to see this library at all.
    typedef uint32_t (*og_version_fn)(void);
    og_version_fn ver = og_dlsym<og_version_fn>("srt_getversion");
    ST_REQUIRE_MSG(ver != 0,
                   "control: dlsym(RTLD_DEFAULT, \"srt_getversion\") found nothing, so no "
                   "lookup through RTLD_DEFAULT can be believed, including the one for "
                   "srt_clock_type");

    // Deliberately NOT asserted here: that srt_getversion() reports 1.4.3 or
    // later. The release version number is not scored anywhere in this step --
    // deltas/07.md files it under "not discriminators, do not score these",
    // because it is a two-line edit any tree can make without implementing
    // anything -- and this group's subject is the clock, not the version. What
    // the control has to establish is that RTLD_DEFAULT can see this library at
    // all, and RESOLVING a second exported function of it establishes exactly
    // that. The returned value is still read and reported below, so that a
    // failing srt_clock_type lookup names the library it was talking to instead
    // of leaving it to be guessed; it is not compared against anything.
    const uint32_t v = ver();

    og_clock_type_fn fn = og_dlsym<og_clock_type_fn>("srt_clock_type");
    ST_REQUIRE_MSG(fn != 0,
                   "dlsym(RTLD_DEFAULT, \"srt_clock_type\") found nothing while "
                   "srt_getversion resolved from the same library and reported "
                   "0x%06x: the symbol is genuinely absent from the shared object "
                   "rather than unreachable",
                   (unsigned)v);
    ST_REQUIRE_MSG(fn() == 4,
                   "srt_clock_type() returned %d, expected 4 "
                   "(SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY)",
                   fn());
}
