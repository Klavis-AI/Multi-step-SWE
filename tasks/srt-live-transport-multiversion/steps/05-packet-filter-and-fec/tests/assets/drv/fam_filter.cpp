// fam_filter.cpp -- the packet-filter family at SRT v1.4.0 (step 05).
//
// Four groups, one subject each:
//
//   g00_endtoend           scored: a 60-message live stream crosses a relay
//                          that drops datagrams at three fixed indexes, FEC is
//                          driving the channel, and the payload arrives
//                          complete and in order
//   g01_filter_config      SRTO_PACKETFILTER setter and getter semantics:
//                          well-formed FEC string accepted, unknown filter name
//                          and unparseable syntax refused with EINVPARAM,
//                          getter returns the stored string or nothing
//   g02_filter_payloadsize order-dependent clamp: payload-size then filter
//                          lowers the stored payload size by the filter's
//                          extra-size requirement; filter then payload-size
//                          leaves the requested value alone
//   g03_filter_stats       srt_bstats reports non-zero pktSndFilterExtraTotal
//                          after FEC traffic and carries the four new filter
//                          counter pairs
//
// ---------------------------------------------------------------------------
// Tier: every group here is `new`.  This family carries NO gate group -- the
// two inherited groups live in fam_gate.cpp, a translation unit that names no
// identifier new at this milestone.  That separation is what makes the
// restatements below safe: a family is one translation unit and one
// executable, a translation unit that does not build is attributed
// `driver-did-not-compile` and scores zero for every group it carries, and
// nothing in this file can therefore take a gate down with it.  Steps 08 and
// 11 follow the same convention.
//
// The file is nevertheless written to COMPILE at both the v1.3.3 and the
// v1.4.0 install prefix, so that against a workspace still at the previous
// milestone each group fails on an assertion that names actual versus expected
// rather than on a build error -- a far more useful verdict than
// `driver-did-not-compile`, and the shape the ladder's no-op leg measures.
// Everything that would not compile at v1.3.3 is reached through a guard: the
// option number by a locally restated CONSTANT whose static_assert reads the
// header's own enumerator wherever that enumerator exists, and the four
// counter pairs by a member-detection idiom that asks the struct rather than
// the version macros.
//
// What that does NOT buy: a tree that claims v1.4.0 in SRT_VERSION_VALUE
// without declaring SRTO_PACKETFILTER fails the static_assert and loses this
// whole family, all four groups at once.  The name is required to compile, and
// TEST_INTERFACE.md section 5 lists it that way.
//
// ---------------------------------------------------------------------------
// How SRTO_PACKETFILTER is named at v1.3.3
//
// SRTO_PACKETFILTER = 60 is absent from <srt/srt.h> at v1.3.3 (measured:
// v1.4.0 srtcore/srt.h:185 writes the value explicitly).  Naming it directly
// would kill the whole translation unit against a workspace that has not
// reached this milestone, costing all four groups for one missing name.
// It is therefore restated locally, as a constant, and pinned with the
// required static_assert net.  The outer #ifdef is mandatory: SRT_VERSION_VALUE comes from
// <srt/version.h>, which is itself new at v1.3.0, so at v1.2.0 a bare
// `#if SRT_VERSION_VALUE >= ...` reduces to `0 >= 0 (1, 3, 2)` -- a
// preprocessor syntax error, not a false condition.
//
// At v1.3.3, srt_setsockflag(s, <slot 60>, ...) returns
// -1 / EINVPARAM because slot 60 is unrecognised.  Every new group asserts the
// set succeeds, so each fails on its own assertion with the observed error
// string, which is exactly the shape the tier rule asks for.
//
// ---------------------------------------------------------------------------
// Deliberately NOT asserted, with the reason:
//
//   * sizeof(SRT_TRACEBSTATS).  The struct does grow by the four int pairs
//     (measured 372 -> 432 at v1.4.0), but TEST_INTERFACE.md sections 4 and 5
//     ask only that the four pairs exist and are readable, and a struct size is
//     not something a public interface promises.  The fields are proved to
//     exist by reading them instead.
//
//   * An exact pktSndFilterExtraTotal.  Measured 6 for 60 messages with
//     cols:10, but the count is an implementation detail of the FEC packet
//     cadence; only "> 0" is stated.
//
//   * A filter-config mismatch rejecting the handshake with SRT_REJ_FILTER.
//     Not this family's group (it is g05_rejectreason_values), and measured
//     unreachable from the public API at this release: the framework merges the
//     two parameter sets, so a one-sided or differing configuration connects.
//     TEST_INTERFACE.md section 5 states the merge; nothing here asserts a
//     rejection that the library does not perform.
//
//   * Documented-but-unmeasurable claims: "cols is mandatory" (a bare "fec"
//     sets successfully at this release), and the `rows`/`layout` defaults,
//     which the getter does not expand into the stored string and which are
//     therefore not observable through the public interface.  Asserting either
//     would fail against genuine v1.4.0.
//
//   * SRTO_TRANSTYPE's numeric value, the SRTO_STRICTENC ->
//     SRTO_ENFORCEDENCRYPTION rename (a header-only rename at the same slot
//     with the same default), srt_setrejectreason's absence, and SRTO_VERSION
//     as a version canary.  None of the four is a behavioural difference at
//     this milestone.
//
// No case uses a wall-clock sleep for synchronisation.  Every receive is
// bounded by SRTO_RCVTIMEO.  The microsecond pacing in st_send_stream only
// keeps the sender from outrunning the live rate limiter; it is not a gate.
// ---------------------------------------------------------------------------
#include "srtkit.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// SRTO_PACKETFILTER restatement and net (see the header note)
// ---------------------------------------------------------------------------
// The option is restated as a local CONSTANT, not as an #ifndef/#define
// fallback for the header's own spelling.  Two reasons, and the second is the
// one that matters:
//
//   * A macro named SRTO_PACKETFILTER shadows the header's enumerator wherever
//     the header does declare it.  The preprocessor cannot see an enumerator,
//     so `#ifndef SRTO_PACKETFILTER` is true even at v1.4.0, and the fallback
//     fires there too -- leaving any static_assert placed after it comparing
//     60 with 60, a check that can never fail and so checks nothing.  The
//     previous revision worked around that by ordering the assert before the
//     #define, which is a trap the next editor has to know about.  A constant
//     cannot shadow anything, so the trap is gone rather than documented.
//
//   * The #ifndef form also made TEST_INTERFACE.md section 5 lie.  That table
//     is generated from these drivers by tools/doc-coverage.py, which reads an
//     `#ifndef NAME` as "the driver has a fallback, this name is optional" and
//     rendered SRTO_PACKETFILTER as `optional (guarded)`.  It is not optional:
//     the static_assert below names the enumerator, so a tree that claims
//     v1.4.0 without declaring the option fails to build this family and loses
//     all four groups.  With the constant form the generator classifies the
//     name `required to compile`, which is the truth.  This is also exactly
//     how the SRT_REJ_* constants are handled in fam_reject.cpp.
//
// The pin stays, and its guard is nested on purpose.  The floor is 1.4.0, not
// 1.3.2: <srt/version.h> (and so SRT_VERSION_VALUE) exists from v1.3.0, so a
// 1.3.2 floor is satisfied at v1.3.3 and would fire the assert against a name
// v1.3.3 does not have.  The outer #ifdef is still required: at v1.2.0 there is
// no <srt/version.h> and a bare #if on SRT_MAKE_VERSION_VALUE is a preprocessor
// syntax error, not a false condition.
#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 0)
static_assert(SRTO_PACKETFILTER == 60,
              "restated SRTO_PACKETFILTER disagrees with <srt/srt.h>: "
              "update the restatement in this file");
#endif
#endif

// Every call below addresses the option through this constant, so the header's
// own spelling appears in the code of this file exactly once -- in the assert
// above.  The diagnostic strings keep saying SRTO_PACKETFILTER, because that is
// the name the reader has to go and implement.
static const SRT_SOCKOPT ST_PACKETFILTER = (SRT_SOCKOPT)60;

// ---------------------------------------------------------------------------
// shared scaffolding
// ---------------------------------------------------------------------------

// The documented live payload size (SRT_LIVE_DEF_PLSIZE = 1316, the default a
// fresh socket reports).  One message is one datagram at this size.
static const int LIVE_PAYLOAD = 1316;

// The FEC filter needs 4 extra bytes of payload budget (instruction.md §1:
// two for the flag spec, two for the length clip), so a filter leaves
// 1456 minus 4 available.
static const int FEC_EXTRA_SIZE = 4;

// The payload-size numbers g02 scores, written as literals on purpose.
//
// <srt/srt.h> declares `static const int SRT_LIVE_DEF_PLSIZE = 1316;` and
// `static const int SRT_LIVE_MAX_PLSIZE = 1456;` at BOTH install prefixes, so
// naming them compiles everywhere -- but a driver that asserts the getter
// against them is comparing the candidate's tree with itself.  A tree whose
// header said 1400 would report 1400 from srt_getsockflag, the constant would
// read 1400, and the assertion would pass while TEST_INTERFACE.md §5 and
// RQ-05-08/09/10 -- which fix these as the numbers 1316, 1456 and 1452 -- were
// both violated.  The contract states numbers, so this group asserts numbers.
//
// Deliberately NOT pinned with a static_assert against the header's constants.
// A static_assert failure is a build failure, a family that does not build is
// attributed `driver-did-not-compile`, and all four groups here would score
// zero.  A tree that renumbered the live default should lose g02 with a message
// naming the number it reported and the number §5 states, and keep its chance
// at the other three.
static const int ST_LIVE_DEF_PLSIZE = 1316;
static const int ST_LIVE_MAX_PLSIZE = 1456;

// The clamped value §5 states outright.  The static_assert below relates the
// three documented numbers to each other; it reads nothing from the candidate,
// so it can only fire on an editing mistake in this file.
static const int ST_FEC_CLAMPED_PLSIZE = 1452;
static_assert(ST_LIVE_MAX_PLSIZE - FEC_EXTRA_SIZE == ST_FEC_CLAMPED_PLSIZE,
              "the documented payload-size numbers must stay consistent: 1456 minus the "
              "filter's 4-byte extra-size requirement is 1452");

// The filter configuration every FEC case uses.
static const char FEC_CONF[] = "fec,cols:10";

static int64_t now_ms() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// st_payload() stamps "MSG%08d" into the first bytes of every message, so a
// received message names the index it was sent as.  That turns "60 messages
// arrived" into "messages 0..59 arrived, in order, with no gaps".
static int msg_index(const std::string& m) {
    if (m.size() < 12 || m.compare(0, 3, "MSG") != 0) return -1;
    char digits[9];
    std::memcpy(digits, m.data() + 3, 8);
    digits[8] = '\0';
    char* end = nullptr;
    long v = std::strtol(digits, &end, 10);
    if (end != digits + 8) return -1;
    return (int)v;
}

// Thread body, deliberately non-throwing: an STFail escaping a std::thread
// would terminate the process and lose the failure message.  Every exit is
// either "got what I was asked for" or "SRTO_RCVTIMEO expired", which the
// caller asserts about.  RCVTIMEO is set on the main thread before this starts.
static void rx_collect(SRTSOCKET s, int want, int bufsize, std::vector<std::string>* out) {
    std::string buf((size_t)bufsize + 4096, '\0');
    while ((int)out->size() < want) {
        int rc = srt_recvmsg(s, &buf[0], (int)buf.size());
        if (rc <= 0) break;
        out->push_back(buf.substr(0, (size_t)rc));
    }
}

// Joins on destruction so an assertion failure on the main thread unwinds
// cleanly instead of hitting std::terminate.  Bounded, because the receive loop
// it joins is bounded by SRTO_RCVTIMEO.
struct Joiner {
    std::thread t;
    explicit Joiner(std::thread&& th) : t(std::move(th)) {}
    Joiner(const Joiner&) = delete;
    Joiner& operator=(const Joiner&) = delete;
    ~Joiner() {
        if (t.joinable()) t.join();
    }
};

struct Xfer {
    std::vector<std::string> sent;
    std::vector<std::string> got;
    int64_t elapsed_ms = 0;
};

// One stream across an established link: receiver on its own thread bounded by
// RCVTIMEO, sender on this one.
static Xfer st_exchange(Link& L, int count, int size, int gap_us, int rcv_timeo_ms) {
    Xfer x;
    st_set_int(L.acc, SRTO_RCVTIMEO, rcv_timeo_ms);
    int64_t t0 = now_ms();
    {
        Joiner rx(std::thread(rx_collect, L.acc, count, size, &x.got));
        x.sent = st_send_stream(L.cal, count, size, gap_us);
    }
    x.elapsed_ms = now_ms() - t0;
    return x;
}

// "Every message arrived, in the order it was sent, none missing, none
// altered."  Asserted by index, so the message that fails is named.
static void require_complete_stream(const Xfer& x, int count, const char* what) {
    ST_REQUIRE_MSG((int)x.got.size() == count, "%s: received %d of %d messages", what,
                   (int)x.got.size(), count);
    for (int i = 0; i < count; i++) {
        int idx = msg_index(x.got[(size_t)i]);
        ST_REQUIRE_MSG(idx == i, "%s: message at position %d carries index %d (expected %d)", what,
                       i, idx, i);
        ST_REQUIRE_MSG(x.got[(size_t)i] == x.sent[(size_t)i],
                       "%s: message %d differs from what was sent (%d bytes received)", what, i,
                       (int)x.got[(size_t)i].size());
    }
}

static SRT_TRACEBSTATS stats_of(SRTSOCKET s, int clear) {
    SRT_TRACEBSTATS b;
    std::memset(&b, 0, sizeof b);
    ST_SRT_OK(srt_bstats(s, &b, clear));
    return b;
}

// ---------------------------------------------------------------------------
// The four filter counter pairs, read by presence and not by version
// ---------------------------------------------------------------------------
// SRT_TRACEBSTATS gains pktSndFilterExtra{,Total}, pktRcvFilterExtra{,Total},
// pktRcvFilterSupply{,Total} and pktRcvFilterLoss{,Total} at v1.4.0, so this
// file must compile where they are absent.  An earlier revision keyed the whole
// of g03 on SRT_VERSION_VALUE, which got the discrimination wrong in both
// directions: a tree that implements all four pairs and leaves SRT_VERSION_* at
// the previous milestone lost the group for a reason unrelated to the feature,
// and a tree that bumps the version without adding the fields failed to
// compile the family -- which, while the gate still lived here, cost the
// inherited g11_transfer with it.
//
// Whether the members exist is a property of the struct, so the struct is what
// is asked.  The C++14 detection idiom below names each member only in the
// specialisation that is instantiated when it exists; an absent field reads as
// -1 and fails its own assertion with its own name in the message, so a partial
// implementation costs exactly the fields it is missing.
#define ST_FILTER_FIELD(NAME)                                                       \
    template <class T, class = void>                                                \
    struct StHas_##NAME : std::false_type {};                                       \
    template <class T>                                                              \
    struct StHas_##NAME<T, decltype((void)&T::NAME)> : std::true_type {};            \
    template <class T>                                                              \
    static long long st_rd_##NAME(const T& s, std::true_type) {                      \
        return (long long)s.NAME;                                                    \
    }                                                                                \
    template <class T>                                                              \
    static long long st_rd_##NAME(const T&, std::false_type) {                       \
        return -1;                                                                   \
    }                                                                                \
    static long long st_##NAME(const SRT_TRACEBSTATS& b) {                           \
        return st_rd_##NAME(b, StHas_##NAME<SRT_TRACEBSTATS>());                      \
    }

ST_FILTER_FIELD(pktSndFilterExtraTotal)
ST_FILTER_FIELD(pktSndFilterExtra)
ST_FILTER_FIELD(pktRcvFilterExtraTotal)
ST_FILTER_FIELD(pktRcvFilterExtra)
ST_FILTER_FIELD(pktRcvFilterSupplyTotal)
ST_FILTER_FIELD(pktRcvFilterSupply)
ST_FILTER_FIELD(pktRcvFilterLossTotal)
ST_FILTER_FIELD(pktRcvFilterLoss)

// The live-transport configuration every case here starts from, plus a packet
// filter on both ends.  The set is
// asserted, so at v1.3.3 -- where slot 60 is not an option -- the case fails
// here, on an assertion naming the observed error, instead of silently
// testing a filterless channel.
static void conf_rcv_fec(SRTSOCKET s) {
    // TSBPD on, latency generous enough that the relay's schedule rather than
    // the clock decides what arrives, too-late drop off so recovery is really
    // lossless.
    st_live_lossless(s);
    ST_REQUIRE_MSG(srt_setsockflag(s, ST_PACKETFILTER, FEC_CONF,
                                   (int)strlen(FEC_CONF)) == 0,
                   "listener SRTO_PACKETFILTER \"%s\" refused: %s -- "
                   "SRTO_PACKETFILTER (slot 60) requires SRT v1.4.0",
                   FEC_CONF, srt_getlasterror_str());
}

static void conf_snd_fec(SRTSOCKET s) {
    // SRTO_SENDER first: the data sender must declare itself for TSBPD to be
    // negotiated at all.
    st_set_int(s, SRTO_SENDER, 1);
    st_live_lossless(s);
    ST_REQUIRE_MSG(srt_setsockflag(s, ST_PACKETFILTER, FEC_CONF,
                                   (int)strlen(FEC_CONF)) == 0,
                   "caller SRTO_PACKETFILTER \"%s\" refused: %s -- "
                   "SRTO_PACKETFILTER (slot 60) requires SRT v1.4.0",
                   FEC_CONF, srt_getlasterror_str());
}

// ===========================================================================
// g01_filter_config -- new
//
// TEST_INTERFACE.md §4, this group: "SRTO_PACKETFILTER accepts a well-formed
// FEC configuration string, rejects an unknown filter name and unparseable syntax with
// EINVPARAM, and the getter retrieves the stored string or an empty string
// when unset"
// ===========================================================================

SRT_CASE(g01_filter_config, well_formed_string_accepted_and_getter_returns_it) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // The unset getter succeeds and reports nothing.  TEST_INTERFACE.md §5:
    // at the previous milestone this failed with SRT_EINVOP; here it succeeds and the
    // configuration is empty.  This is the positive control for the getter's
    // non-emptiness after the set below.
    {
        char buf[512];
        int len = (int)sizeof buf;
        int grc = srt_getsockflag(s, ST_PACKETFILTER, buf, &len);
        int gerr = srt_getlasterror(NULL);
        ST_REQUIRE_MSG(grc == 0,
                       "getter on a socket with no filter returned %d (%s); expected 0 -- "
                       "the getter must succeed and report an empty configuration, not EINVOP",
                       grc, srt_strerror(gerr, 0));
        ST_REQUIRE_MSG(len == 0, "getter on a socket with no filter reported length %d; "
                                 "expected 0 (empty string when unset)",
                       len);
    }

    int src = srt_setsockflag(s, ST_PACKETFILTER, FEC_CONF, (int)strlen(FEC_CONF));
    ST_REQUIRE_MSG(src == 0,
                   "srt_setsockflag(SRTO_PACKETFILTER, \"%s\") returned %d (%s); expected 0",
                   FEC_CONF, src, srt_getlasterror_str());

    char stored[512];
    int slen = (int)sizeof stored;
    int grc2 = srt_getsockflag(s, ST_PACKETFILTER, stored, &slen);
    ST_REQUIRE_MSG(grc2 == 0, "getter after a successful set returned %d (%s); expected 0", grc2,
                   srt_strerror(srt_getlasterror(NULL), 0));
    std::string got_str(stored, (size_t)slen);
    ST_REQUIRE_MSG(got_str == FEC_CONF,
                   "getter returned \"%s\" (len=%d); expected the stored \"%s\"", got_str.c_str(),
                   slen, FEC_CONF);

    srt_close(s);
}

SRT_CASE(g01_filter_config, unknown_filter_name_refused_with_einvparam) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    const char* bad = "nosuchfilter,x:1";
    int rc = srt_setsockflag(s, ST_PACKETFILTER, bad, (int)strlen(bad));
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "srt_setsockflag(SRTO_PACKETFILTER, \"%s\") returned %d; expected SRT_ERROR",
                   bad, rc);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                   "srt_setsockflag(SRTO_PACKETFILTER, \"%s\") failed with %d (%s); expected "
                   "SRT_EINVPARAM (%d)",
                   bad, err, srt_strerror(err, 0), (int)SRT_EINVPARAM);

    // The refused set installed nothing: the socket is still usable and the
    // getter still reports an empty configuration.
    {
        char buf[512];
        int len = (int)sizeof buf;
        int grc = srt_getsockflag(s, ST_PACKETFILTER, buf, &len);
        ST_REQUIRE_MSG(grc == 0 && len == 0,
                       "getter after the refused set: rc=%d len=%d; a refused setsockflag must "
                       "not install a filter",
                       grc, len);
    }

    // A well-formed string is still accepted on the same socket.
    int src2 = srt_setsockflag(s, ST_PACKETFILTER, FEC_CONF, (int)strlen(FEC_CONF));
    ST_REQUIRE_MSG(src2 == 0,
                   "well-formed set after the refused one returned %d (%s); the socket must "
                   "remain usable after a refused setsockflag",
                   src2, srt_getlasterror_str());

    srt_close(s);
}

SRT_CASE(g01_filter_config, unparseable_syntax_refused_with_einvparam) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // Semicolons and '=' where the documented grammar wants commas and ':'.
    // TEST_INTERFACE.md §5: rejected the same as an unknown filter name.
    const char* bad = "fec;cols=10";
    int rc = srt_setsockflag(s, ST_PACKETFILTER, bad, (int)strlen(bad));
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "srt_setsockflag(SRTO_PACKETFILTER, \"%s\") returned %d; expected SRT_ERROR",
                   bad, rc);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                   "srt_setsockflag(SRTO_PACKETFILTER, \"%s\") failed with %d (%s); expected "
                   "SRT_EINVPARAM (%d)",
                   bad, err, srt_strerror(err, 0), (int)SRT_EINVPARAM);

    // Positive control: the well-formed spelling of the same intent is
    // accepted, so the refusal above is about the syntax and not about "fec"
    // being rejected on this socket.
    int src2 = srt_setsockflag(s, ST_PACKETFILTER, FEC_CONF, (int)strlen(FEC_CONF));
    ST_REQUIRE_MSG(src2 == 0, "well-formed \"%s\" after the refused \"%s\" returned %d (%s)",
                   FEC_CONF, bad, src2, srt_getlasterror_str());

    char buf[512];
    int len = (int)sizeof buf;
    int grc = srt_getsockflag(s, ST_PACKETFILTER, buf, &len);
    ST_REQUIRE_MSG(grc == 0 && std::string(buf, (size_t)len) == FEC_CONF,
                   "getter after the valid set returned rc=%d \"%s\" (len=%d); expected \"%s\" -- "
                   "the refused syntax must have stored nothing",
                   grc, std::string(buf, (size_t)len).c_str(), len, FEC_CONF);

    srt_close(s);
}

// ===========================================================================
// g02_filter_payloadsize -- new
//
// TEST_INTERFACE.md §4, this group: "setting SRTO_PACKETFILTER after
// SRTO_PAYLOADSIZE silently clamps the payload size down by the filter's extra-size requirement, while setting
// the filter first then the payload size leaves the payload size at the
// requested value"
//
// The two cases are each other's positive control: the first proves the clamp
// happens, the second proves it does not happen unconditionally.
// ===========================================================================

SRT_CASE(g02_filter_payloadsize, payloadsize_then_filter_clamps_down) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // TEST_INTERFACE.md §5: the live-mode default is 1316, not 1456.
    int def_pls = st_get_int(s, SRTO_PAYLOADSIZE);
    ST_REQUIRE_MSG(def_pls == ST_LIVE_DEF_PLSIZE,
                   "fresh socket SRTO_PAYLOADSIZE=%d; expected %d -- TEST_INTERFACE.md §5 "
                   "states the live-mode default as the number 1316, which is not the %d "
                   "ceiling and is not whatever SRT_LIVE_DEF_PLSIZE happens to say in this "
                   "tree's <srt/srt.h>",
                   def_pls, ST_LIVE_DEF_PLSIZE, ST_LIVE_MAX_PLSIZE);

    int max_pls = ST_LIVE_MAX_PLSIZE;
    ST_REQUIRE_MSG(srt_setsockflag(s, SRTO_PAYLOADSIZE, &max_pls, (int)sizeof max_pls) == 0,
                   "set SRTO_PAYLOADSIZE=%d returned %s", max_pls, srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_PAYLOADSIZE), ST_LIVE_MAX_PLSIZE);

    int frc = srt_setsockflag(s, ST_PACKETFILTER, FEC_CONF, (int)strlen(FEC_CONF));
    ST_REQUIRE_MSG(frc == 0,
                   "set SRTO_PACKETFILTER \"%s\" on a socket with SRTO_PAYLOADSIZE=%d returned "
                   "%d (%s); expected 0",
                   FEC_CONF, ST_LIVE_MAX_PLSIZE, frc, srt_getlasterror_str());

    const int EXPECTED = ST_FEC_CLAMPED_PLSIZE;  // 1456 - 4, §5's number
    int stored = st_get_int(s, SRTO_PAYLOADSIZE);
    ST_REQUIRE_MSG(stored == EXPECTED,
                   "after setting \"%s\" on a %d-byte socket SRTO_PAYLOADSIZE reads %d; expected "
                   "%d (%d minus the filter's %d-byte extra-size requirement), silently and with "
                   "no error reported",
                   FEC_CONF, ST_LIVE_MAX_PLSIZE, stored, EXPECTED, ST_LIVE_MAX_PLSIZE,
                   FEC_EXTRA_SIZE);

    srt_close(s);
}

SRT_CASE(g02_filter_payloadsize, filter_then_payloadsize_keeps_requested_value) {
    // The reverse order.  The payload-size setter compares the value already
    // stored against the filter's budget, so after the filter is installed on a
    // fresh socket (stored 1316, well inside the 1452 budget) a request for
    // 1456 is accepted and stored as 1456.  TEST_INTERFACE.md §5 states this
    // directly; a driver asserting "always clamps" would be scoring the wrong rule.
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    int frc = srt_setsockflag(s, ST_PACKETFILTER, FEC_CONF, (int)strlen(FEC_CONF));
    ST_REQUIRE_MSG(frc == 0, "set SRTO_PACKETFILTER \"%s\" first returned %d (%s); expected 0",
                   FEC_CONF, frc, srt_getlasterror_str());
    ST_REQUIRE_MSG(st_get_int(s, SRTO_PAYLOADSIZE) == ST_LIVE_DEF_PLSIZE,
                   "installing a filter on a fresh socket changed SRTO_PAYLOADSIZE to %d; the "
                   "stored default %d is already within the filter's budget and must not move",
                   st_get_int(s, SRTO_PAYLOADSIZE), ST_LIVE_DEF_PLSIZE);

    int max_pls = ST_LIVE_MAX_PLSIZE;
    int src = srt_setsockflag(s, SRTO_PAYLOADSIZE, &max_pls, (int)sizeof max_pls);
    ST_REQUIRE_MSG(src == 0, "set SRTO_PAYLOADSIZE=%d after the filter returned %d (%s)", max_pls,
                   src, srt_getlasterror_str());

    int stored = st_get_int(s, SRTO_PAYLOADSIZE);
    ST_REQUIRE_MSG(stored == ST_LIVE_MAX_PLSIZE,
                   "filter-then-payloadsize: SRTO_PAYLOADSIZE reads %d; expected %d -- the setter "
                   "compares the stored value against the filter's budget, not the incoming one, "
                   "so no clamp applies here",
                   stored, ST_LIVE_MAX_PLSIZE);

    srt_close(s);
}

// ===========================================================================
// g03_filter_stats -- new
//
// TEST_INTERFACE.md §4, this group: "with FEC configured on both ends and
// traffic crossing the channel, srt_bstats reports non-zero
// pktSndFilterExtraTotal on the sender, all eight new filter counter fields
// (four totals and their four interval counterparts) are members of
// SRT_TRACEBSTATS, and no total is below its own interval counterpart"
//
// One case.  Its two halves are the sender side (traffic must produce filter
// control packets) and the receiver side (the eight documented fields must be
// there and readable), and a case that cannot read the fields cannot fail on
// the counter, so splitting them would create a half that cannot fail on its
// own.
// ===========================================================================

SRT_CASE(g03_filter_stats, fec_traffic_reports_filter_counters) {
    const int N = 60;
    Link L;
    RelaySched sc;
    st_connect(L, sc, conf_rcv_fec, conf_snd_fec);

    Xfer x = st_exchange(L, N, LIVE_PAYLOAD, 1000, 8000);

    // Read both ends before asserting, so the values appear in any failure.
    SRT_TRACEBSTATS sb = stats_of(L.cal, 0);
    SRT_TRACEBSTATS rb = stats_of(L.acc, 0);

    // First half: the four counter pairs are members of SRT_TRACEBSTATS and are
    // readable.  A field this milestone has not added reads as -1 here (see the
    // detection block above), so one loop covers both "the field exists" and
    // "a counter is never negative", and names the field that failed.
    const char* const field_name[8] = {
        "pktSndFilterExtraTotal",  "pktSndFilterExtra",
        "pktRcvFilterExtraTotal",  "pktRcvFilterExtra",
        "pktRcvFilterSupplyTotal", "pktRcvFilterSupply",
        "pktRcvFilterLossTotal",   "pktRcvFilterLoss"};
    const long long field_val[8] = {
        st_pktSndFilterExtraTotal(sb),  st_pktSndFilterExtra(sb),
        st_pktRcvFilterExtraTotal(rb),  st_pktRcvFilterExtra(rb),
        st_pktRcvFilterSupplyTotal(rb), st_pktRcvFilterSupply(rb),
        st_pktRcvFilterLossTotal(rb),   st_pktRcvFilterLoss(rb)};
    for (int i = 0; i < 8; i++)
        ST_REQUIRE_MSG(field_val[i] >= 0,
                       "SRT_TRACEBSTATS.%s reads %lld: the field is not a member of the "
                       "statistics struct at this prefix (-1), or a counter is negative.  "
                       "This milestone adds the four filter counter pairs "
                       "pktSndFilterExtra, pktRcvFilterExtra, pktRcvFilterSupply and "
                       "pktRcvFilterLoss, each with a *Total counterpart",
                       field_name[i], field_val[i]);

    // Second half: the sender's counter moved, so the filter really drove the
    // channel rather than merely being configured.
    ST_REQUIRE_MSG(field_val[0] > 0,
                   "pktSndFilterExtraTotal=%lld after %d messages with \"%s\" on both ends; "
                   "expected > 0 -- the filter must have supplied at least one control packet",
                   field_val[0], N, FEC_CONF);

    // The documented invariant between the total and the interval on the same
    // socket: the interval is flushed into the total, so the total is never
    // smaller.  Shape, not a number.
    ST_REQUIRE_MSG(field_val[0] >= field_val[1],
                   "pktSndFilterExtraTotal=%lld is below its own interval counterpart "
                   "pktSndFilterExtra=%lld; the interval is folded into the total",
                   field_val[0], field_val[1]);
    ST_REQUIRE_MSG(field_val[2] >= field_val[3],
                   "pktRcvFilterExtraTotal=%lld is below its own interval counterpart "
                   "pktRcvFilterExtra=%lld",
                   field_val[2], field_val[3]);
    ST_REQUIRE_MSG(field_val[4] >= field_val[5],
                   "pktRcvFilterSupplyTotal=%lld is below its own interval counterpart "
                   "pktRcvFilterSupply=%lld",
                   field_val[4], field_val[5]);
    ST_REQUIRE_MSG(field_val[6] >= field_val[7],
                   "pktRcvFilterLossTotal=%lld is below its own interval counterpart "
                   "pktRcvFilterLoss=%lld",
                   field_val[6], field_val[7]);

    // The traffic that produced the counter has to have arrived; otherwise
    // "> 0" would be satisfiable by a channel that carried nothing.
    require_complete_stream(x, N, "fec_stats_transfer");
}

// ===========================================================================
// g00_endtoend -- scored, not the gate
//
// This milestone's own end-to-end path: the FEC recovery route is new here, so
// a candidate who has not implemented the milestone must score zero on it
// rather than be vetoed by it.  This family holds no gate; the step's gate is
// fam_gate.cpp's g11_transfer and g12_encryption.
//
// TEST_INTERFACE.md section 4: "with the FEC filter configured on both ends, a
// live stream of 60 messages crosses a relay that drops datagrams at fixed
// indexes, the filter injects control packets into the channel, and the payload
// still arrives complete and in order"
// ===========================================================================

SRT_CASE(g00_endtoend, fec_rebuilds_packets_dropped_at_fixed_indexes) {
    // The lossy arm.  Three forward data packets are dropped at indexes far
    // enough apart that the relay is the only thing that removed them, and the
    // stream must still arrive complete and in order.  Which of the two
    // recovery mechanisms did it is not asserted; see the note below the
    // transfer.
    const int N = 60;
    Link L;
    RelaySched sc;
    sc.drop_fwd.insert(10);
    sc.drop_fwd.insert(30);
    sc.drop_fwd.insert(50);
    st_connect(L, sc, conf_rcv_fec, conf_snd_fec);

    Xfer x = st_exchange(L, N, LIVE_PAYLOAD, 1000, 8000);

    // The relay must have really removed three forward data packets, or the
    // "recovery" claim is about a channel nothing was ever lost on.
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 3,
                   "the relay dropped %d forward data packets; the schedule names three "
                   "(indexes 10, 30, 50)",
                   L.relay->dropped_fwd());

    require_complete_stream(x, N, "fec_e2e_lossy");

    // And the filter must have been active, not merely configured: on a
    // dropping channel the sender's filter-control counter is the evidence that
    // a filter was driving the channel at all.
    //
    // What this case proves, stated exactly: FEC was configured on both ends,
    // the filter injected control packets, three forward datagrams were removed
    // by the relay, and the stream still arrived complete and in order.  It does
    // NOT prove that FEC rather than ARQ performed the repair -- the link runs
    // with SRTO_TLPKTDROP off and 900 ms of latency, so retransmission is
    // available and three isolated drops are what ARQ handles best, and the
    // milestone says the two run in parallel with the loss covered by whichever
    // finishes first.  Attributing the repair to one of two parallel mechanisms
    // is not observable through the public interface, so the case asserts the
    // part that is.
    //
    // The counter is read by member presence, not by SRT_VERSION_VALUE, so a
    // tree that added the statistics fields without bumping the version macros
    // is still measured on them.
    const long long snd_extra = st_pktSndFilterExtraTotal(stats_of(L.cal, 0));
    ST_REQUIRE_MSG(snd_extra > 0,
                   "pktSndFilterExtraTotal=%lld after a lossy %d-message stream with \"%s\" on "
                   "both ends (-1 means the field is not a member of SRT_TRACEBSTATS here); the "
                   "filter supplied no control packets, so no filter was driving the channel",
                   snd_extra, N, FEC_CONF);
}

SRT_CASE(g00_endtoend, fec_configured_stream_arrives_intact_on_a_clean_relay) {
    // The positive control for the case above -- this group is scored, not the
    // gate.  The same FEC configuration over a relay that drops nothing:
    // without it, a library that delivered every message by refusing to carry
    // any would score the lossy case.
    const int N = 60;
    Link L;
    RelaySched sc;
    st_connect(L, sc, conf_rcv_fec, conf_snd_fec);

    Xfer x = st_exchange(L, N, LIVE_PAYLOAD, 1000, 8000);
    require_complete_stream(x, N, "fec_e2e_clean");

    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 0, "clean relay dropped %d forward data packets",
                   L.relay->dropped_fwd());
    ST_REQUIRE_MSG(L.relay->seen_fwd() >= N,
                   "relay saw only %d forward data packets for %d messages", L.relay->seen_fwd(), N);
}
