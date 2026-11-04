// Family "msgctrl", checkpoint 06 (SRT v1.4.2). Four scored groups, all `new`:
//
//   g01_msgctrl_defaults    the message-control initialiser and the constant
//                           object: sentinel values, infinite TTL, clear
//                           in-order flag, subsequent-packet boundary, and
//                           both of the newly-visible trailing fields.
//   g02_srctime_writeback   srctime and pktseq are written back into the
//                           CALLER's block by a successful srt_sendmsg2, and
//                           the payload still crosses intact.
//   g03_srctime_bound       a source time strictly older than the connection's
//                           own start time is refused with SRT_EINVALMSGAPI
//                           and transmits nothing, while one taken at the
//                           connection start time is accepted.
//   g04_send_accounting     the reorder-tolerance report and the
//                           original-only (unique) transmission counters.
//
// ------------------------------------------------------------ tiers
//
// Every group here is `new` and SCORED (scored.txt, and E2E_TIER in meta.env
// names the `live` family for the end-to-end group, not this one). This file
// holds no gate group, and therefore does NOT have
// to compile against v1.4.0 and is deliberately not contorted to: the milestone
// identifiers it names -- SRT_SEQNO_NONE, SRT_MSGNO_NONE, SRT_MSGTTL_INF,
// srt_connection_time, the SRT_MSGCTRL trailing pair and the four new
// statistics fields -- are absent there, and a `new` group that still compiles
// and passes at the previous milestone is a group that fails to discriminate.
// The v1.4.0 run is expected to fail at COMPILE time; that is the outcome
// DRIVER_RULES asks for and what the report records.
//
// The three sentinel constants and the TTL value are restated below under
// `#ifndef`, because a restatement costs nothing at a tag where the header has
// them -- the header wins -- and it means a candidate who declared the option
// surface but reordered the header still has to agree with the published
// number. Each restatement is pinned with the static_assert DRIVER_RULES
// requires, so it cannot silently drift from <srt/srt.h>.
//
// ------------------------------------------------------------ traceability
//
// Every assertion below comes from the step's own definition, which is also
// what the candidate is shown: stepdefs/06/interface.md (shipped as §5 of
// TEST_INTERFACE.md) and stepdefs/06/scope.md. The pinned srt-docs/ documents
// are wrong on three of these points and interface.md corrects them; the
// corrections are what is asserted here:
//
//   * API-functions.md comments SRT_MSGCTRL.pktseq "(unused for sending)" and
//     API.md's field list for srt_sendmsg2 says "pktseq: unused". interface.md
//     §2 says the block is an IN/OUT parameter on send. g02 scores the
//     corrected behaviour.
//   * API-functions.md never states the defaults at all. interface.md gives
//     them in declaration order. g01 scores those and the trailing pair the
//     srt-docs struct listing omits.
//   * API-functions.md's `srt_msgctrl_default` is described as "a constant
//     object" without contents; interface.md fixes the contents as identical
//     to the initialiser's. g01 case 2 scores that identity.
//
// statistics.md is NOT wrong about the reorder tolerance and g04 is not
// correcting it: its pktReorderTolerance entry says "SRT starts from tolerance
// value set in SRTO_LOSSMAXTTL (initial tolerance is set to 0 in SRT v1.4.0
// and prior versions)", and the parenthetical scopes the zero to the releases
// before this one. g04 scores the behaviour that sentence's main clause states
// for this release, plus the listener-to-accepted inheritance that only
// interface.md states.
//
// Deliberately NOT asserted, because nothing candidate-facing states them:
//
//   * SRT_MSGCTRL.msgno on send. interface.md §2 says so explicitly: msgno was
//     written back before this milestone too, "do not treat msgno as evidence
//     either way", and deltas/06.md D10 repeats it. g02 asserts nothing about
//     it.
//   * whether srt_sendmsg2 writes pktseq back before or after it validates
//     srctime. Nothing documents the state of the block after a REFUSED send,
//     so g03 asserts only the two things interface.md does state for that
//     path: the error code and that nothing reaches the peer.
//   * what the receiver's own block holds for fields other than srctime. The
//     receive side is unchanged by this milestone (deltas/06.md: "Receive-side
//     SRT_MSGCTRL.pktseq/msgno are already filled at v1.4.0"), so those are not
//     a discriminator and are not scored.
//   * the numeric value of any srctime. The clock is gettimeofday-derived at
//     both tags (deltas/06.md, "Not discriminators"), so only the relation
//     between the stamped time and the caller's own observations is asserted,
//     never an absolute figure.
//   * byteSentUniqueTotal as an exact count. bstats adds the SRT+UDP+IP header
//     size to every byte counter (core.cpp), which is not part of the
//     candidate-facing contract; g04 asserts the packet counter exactly and
//     the byte counter only for the monotone/ordering shape that interface.md
//     does state.
//   * anything about socket groups. deltas/06.md D14 makes the group entry
//     points `new` at this milestone, but they belong to the `access` family's
//     g08_entry_points; a second family asserting them would be scoring the
//     same capability twice.
#include "stest.h"
#include "srtkit.h"
#include "relay.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ------------------------------------------------------ restated constants
//
// Placed after "stest.h" has pulled in <srt/srt.h>, so that where the header
// has the name the macro shadows an identifier of identical value rather than
// rewriting the header's own declaration. Both the static_assert and the
// #define are guarded: SRT_VERSION_VALUE comes from <srt/version.h>, which is
// only present from v1.3.0, and this file is compiled against v1.4.0 in the
// discrimination check, where the assert's operand would be SRT_MSGNO_NONE
// undeclared. (The file is expected to fail to compile there anyway -- see the
// tier note -- but a syntax error is a worse failure report than a missing
// identifier, and nesting costs nothing.)
#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 2)
static_assert(SRT_SEQNO_NONE == -1, "restated SRT_SEQNO_NONE disagrees with <srt/srt.h>");
static_assert(SRT_MSGNO_NONE == -1, "restated SRT_MSGNO_NONE disagrees with <srt/srt.h>");
static_assert(SRT_MSGTTL_INF == -1, "restated SRT_MSGTTL_INF disagrees with <srt/srt.h>");
#endif
#endif

#ifndef SRT_SEQNO_NONE
#define SRT_SEQNO_NONE ((int32_t)-1)
#endif

#ifndef SRT_MSGNO_NONE
#define SRT_MSGNO_NONE ((int32_t)-1)
#endif

#ifndef SRT_MSGTTL_INF
#define SRT_MSGTTL_INF (-1)
#endif

// ------------------------------------------------------------- constants

// One message, one packet: 1300 bytes is above the 1252-byte floor below which
// live mode refuses a receive buffer (congctl.cpp), and below the payload size
// the handshake picks for a loopback MSS.
static const int MC_MSG = 1300;

// Messages carried by the accounting group. Deliberately far below 50:
// deltas/06.md, "Driver hazards at this step" -- core.cpp counts consecutive
// in-order original deliveries and, on every fiftieth, resets the counter and
// decrements m_iReorderTolerance by one. A transfer of 50 or more clean
// messages would therefore read back one less than was configured, and would
// fail a candidate who implemented the milestone correctly.
static const int MC_ACCT_COUNT = 20;

// Pacing between sends, microseconds, for the same reason: never let a
// live-mode rate limiter be why a message is late or missing.
static const int MC_GAP_US = 2000;

// Receive bounds. Nothing here is a sleep used as synchronisation -- both are
// handed to SRTO_RCVTIMEO and the receiver drives the case.
static const int MC_EMPTY_MS = 700;   // a receive that is EXPECTED to yield nothing
static const int MC_FULL_MS  = 4000;  // a receive that is expected to yield everything (>= one latency)

// The latency both ends of a g02/g03 connection configure before bind. Short,
// so a case costs about one latency rather than a second; the cases assert
// arrival and scheduling, not timing, and the receive bound below is what
// keeps a message that never plays from costing the case budget.
static const int MC_LATENCY_MS = 120;

// --------------------------------------------------------------- naming

static const char* mc_errname(int e) {
    switch (e) {
    case SRT_SUCCESS:       return "SRT_SUCCESS";
    case SRT_EINVALMSGAPI:  return "SRT_EINVALMSGAPI";
    case SRT_EINVPARAM:     return "SRT_EINVPARAM";
    case SRT_EINVSOCK:      return "SRT_EINVSOCK";
    case SRT_EINVOP:        return "SRT_EINVOP";
    case SRT_EBOUNDSOCK:    return "SRT_EBOUNDSOCK";
    case SRT_ECONNSOCK:     return "SRT_ECONNSOCK";
    case SRT_ENOCONN:       return "SRT_ENOCONN";
    case SRT_ENOSERVER:     return "SRT_ENOSERVER";
    case SRT_ECONNLOST:     return "SRT_ECONNLOST";
    default:                return "<other SRT_ERRNO>";
    }
}

// ------------------------------------------------------ connection helper

// A live connection: listener, caller, accepted socket on loopback, with the
// harness's relay on the path carrying an EMPTY schedule -- forward everything,
// drop nothing, delay nothing. deltas/06.md D12 asks for a clean path here
// because the counters under test are about original versus retransmitted
// transmissions, and an empty schedule is what "clean" means; it is not the
// same thing as no relay at all. st_connect always wires the caller through a
// Relay, and that Relay owns the UDP sockets the connection's datagrams
// actually travel over, so it has to outlive the transfer. (It cost a round of
// debugging to learn what that looks like when it does not: both ends still
// report state 5, srt_sendmsg2 still returns the payload length and still
// writes back srctime and pktseq, and the receiver simply never sees anything,
// because the path the packets were addressed to has gone.)
//
// st_connect asserts the link came up on both sides; a case whose subject is
// a REFUSED send still needs a live connection, so the refusal is observed on
// the send rather than on the connect.
struct McLink {
    SRTSOCKET lsn = SRT_INVALID_SOCK;
    SRTSOCKET cal = SRT_INVALID_SOCK;
    SRTSOCKET acc = SRT_INVALID_SOCK;
    Relay* relay = nullptr;     // owned: the path, not a schedule
    McLink() {}
    McLink(const McLink&) = delete;
    McLink& operator=(const McLink&) = delete;
    ~McLink() { close(); }
    void close() {
        if (acc != SRT_INVALID_SOCK) { srt_close(acc); acc = SRT_INVALID_SOCK; }
        if (cal != SRT_INVALID_SOCK) { srt_close(cal); cal = SRT_INVALID_SOCK; }
        if (lsn != SRT_INVALID_SOCK) { srt_close(lsn); lsn = SRT_INVALID_SOCK; }
        delete relay;
        relay = nullptr;
    }
};

// The live-mode configuration both ends need: TSBPD on, a latency, too-late
// drop off. Applied before bind by st_connect, which is where interface.md's
// §5 boundary requires it. Kept to the same shape as st_live_lossless (the
// harness helper every other family's transfers use) rather than a shortened
// latency: the receiver's drain window in the cases below is bounded by
// SRTO_RCVTIMEO, so a longer latency costs a little wall clock and removes any
// question of a message being held past the window.
static void mc_live_conf(SRTSOCKET s) {
    st_set_int(s, SRTO_TSBPDMODE, 1);
    st_set_int(s, SRTO_LATENCY, MC_LATENCY_MS);
    st_set_int(s, SRTO_TLPKTDROP, 0);
}

// Moves everything st_connect built -- the three sockets AND the relay that
// carries their datagrams -- out of the temporary Link and into L, so that one
// object's lifetime governs the whole connection. Dropping the relay here is
// what made five cases of this family fail with a receiver timeout while the
// sender reported every send as accepted.
static void mc_adopt(McLink& L, Link& tmp) {
    L.lsn = tmp.lsn; tmp.lsn = SRT_INVALID_SOCK;
    L.cal = tmp.cal; tmp.cal = SRT_INVALID_SOCK;
    L.acc = tmp.acc; tmp.acc = SRT_INVALID_SOCK;
    L.relay = tmp.relay; tmp.relay = nullptr;
}

static void mc_connect(McLink& L) {
    Link tmp;
    RelaySched no_loss;
    st_connect(tmp, no_loss, mc_live_conf, mc_live_conf);
    mc_adopt(L, tmp);
}

// -------------------------------------------------- message-control helper

// Fills a block with a byte pattern that is neither 0 nor 0xff, so that a
// library which merely zero-fills, and one which memsets to -1, both fail to
// produce the sentinels by accident.
static void mc_poison(SRT_MSGCTRL* m) {
    std::memset(m, 0x5a, sizeof *m);
}

static const char* mc_boolword(int v) { return v ? "set" : "clear"; }

// ------------------------------------------------------------ g01 defaults

SRT_CASE(g01_msgctrl_defaults, init_leaves_the_documented_defaults) {
    // interface.md: "You are free to use either of these two methods". Nothing
    // in this case opens a socket -- interface.md says the initialiser works
    // "without a socket existing", and that is half of what the group claims.
    SRT_MSGCTRL m;
    mc_poison(&m);
    srt_msgctrl_init(&m);

    ST_REQUIRE_MSG(m.flags == 0,
                   "srt_msgctrl_init left flags at %d; TEST_INTERFACE.md section 5 requires 0 "
                   "(no flags set)", m.flags);
    ST_REQUIRE_MSG(m.msgttl == SRT_MSGTTL_INF,
                   "srt_msgctrl_init left msgttl at %d; TEST_INTERFACE.md section 5 requires the "
                   "infinite-lifetime value SRT_MSGTTL_INF (%d)",
                   m.msgttl, (int)SRT_MSGTTL_INF);
    ST_REQUIRE_MSG(m.inorder == 0,
                   "srt_msgctrl_init left the in-order flag %s (%d); TEST_INTERFACE.md section 5 "
                   "requires it clear (0)", mc_boolword(m.inorder), m.inorder);
    ST_REQUIRE_MSG(m.boundary == 0,
                   "srt_msgctrl_init left boundary at %d; TEST_INTERFACE.md section 5 requires 0, "
                   "the subsequent-packet value (a block cleared with memset would "
                   "land here by accident, which is why the other fields matter)",
                   m.boundary);
    ST_REQUIRE_MSG(m.srctime == 0,
                   "srt_msgctrl_init left srctime at %lld; TEST_INTERFACE.md section 5 requires 0, "
                   "the \"stamp it now\" request", (long long)m.srctime);
    ST_REQUIRE_MSG(m.pktseq == SRT_SEQNO_NONE,
                   "srt_msgctrl_init left pktseq at %d; TEST_INTERFACE.md section 5 requires "
                   "SRT_SEQNO_NONE (%d) -- zero is a VALID sequence number, so a "
                   "zero-filled block is not an initialised one",
                   (int)m.pktseq, (int)SRT_SEQNO_NONE);
    ST_REQUIRE_MSG(m.msgno == SRT_MSGNO_NONE,
                   "srt_msgctrl_init left msgno at %d; TEST_INTERFACE.md section 5 requires "
                   "SRT_MSGNO_NONE (%d)", (int)m.msgno, (int)SRT_MSGNO_NONE);
    // The two fields the srt-docs struct listing stops short of. interface.md
    // calls them out by name; a block brace-initialised with seven values does
    // not initialise them.
    ST_REQUIRE_MSG(m.grpdata == nullptr,
                   "srt_msgctrl_init left grpdata at %p; TEST_INTERFACE.md section 5 requires NULL",
                   (const void*)m.grpdata);
    ST_REQUIRE_MSG(m.grpdata_size == 0,
                   "srt_msgctrl_init left grpdata_size at %zu; TEST_INTERFACE.md "
                   "section 5 requires 0",
                   m.grpdata_size);
}

SRT_CASE(g01_msgctrl_defaults, default_object_matches_the_init_result) {
    // interface.md: srt_msgctrl_default "holds exactly the same contents" as
    // the initialiser writes, and is "an exported constant object of type
    // const SRT_MSGCTRL". Asserting the identity is what stops a candidate
    // from implementing one of the two paths and leaving the other at the
    // old zero-filled values.
    SRT_MSGCTRL inited;
    mc_poison(&inited);
    srt_msgctrl_init(&inited);

    const SRT_MSGCTRL& d = srt_msgctrl_default;

    ST_REQUIRE_MSG(d.flags == inited.flags,
                   "srt_msgctrl_default.flags is %d; srt_msgctrl_init wrote %d",
                   d.flags, inited.flags);
    ST_REQUIRE_MSG(d.msgttl == inited.msgttl,
                   "srt_msgctrl_default.msgttl is %d; srt_msgctrl_init wrote %d",
                   d.msgttl, inited.msgttl);
    ST_REQUIRE_MSG(d.inorder == inited.inorder,
                   "srt_msgctrl_default.inorder is %d; srt_msgctrl_init wrote %d",
                   d.inorder, inited.inorder);
    ST_REQUIRE_MSG(d.boundary == inited.boundary,
                   "srt_msgctrl_default.boundary is %d; srt_msgctrl_init wrote %d",
                   d.boundary, inited.boundary);
    ST_REQUIRE_MSG(d.srctime == inited.srctime,
                   "srt_msgctrl_default.srctime is %lld; srt_msgctrl_init wrote %lld",
                   (long long)d.srctime, (long long)inited.srctime);
    ST_REQUIRE_MSG(d.pktseq == inited.pktseq,
                   "srt_msgctrl_default.pktseq is %d; srt_msgctrl_init wrote %d "
                   "(both must be SRT_SEQNO_NONE, %d)",
                   (int)d.pktseq, (int)inited.pktseq, (int)SRT_SEQNO_NONE);
    ST_REQUIRE_MSG(d.msgno == inited.msgno,
                   "srt_msgctrl_default.msgno is %d; srt_msgctrl_init wrote %d "
                   "(both must be SRT_MSGNO_NONE, %d)",
                   (int)d.msgno, (int)inited.msgno, (int)SRT_MSGNO_NONE);
    ST_REQUIRE_MSG(d.grpdata == inited.grpdata,
                   "srt_msgctrl_default.grpdata is %p; srt_msgctrl_init wrote %p",
                   (const void*)d.grpdata, (const void*)inited.grpdata);
    ST_REQUIRE_MSG(d.grpdata_size == inited.grpdata_size,
                   "srt_msgctrl_default.grpdata_size is %zu; srt_msgctrl_init wrote %zu",
                   d.grpdata_size, inited.grpdata_size);

    // The identity above is only meaningful if the left-hand side is itself
    // the required value, so restate the two sentinels on the constant object
    // directly: an implementation that makes both paths equally wrong would
    // otherwise satisfy the comparison alone.
    ST_REQUIRE_MSG(d.pktseq == SRT_SEQNO_NONE && d.msgno == SRT_MSGNO_NONE,
                   "srt_msgctrl_default itself carries pktseq=%d msgno=%d; "
                   "TEST_INTERFACE.md section 5 requires SRT_SEQNO_NONE/SRT_MSGNO_NONE (%d/%d)",
                   (int)d.pktseq, (int)d.msgno, (int)SRT_SEQNO_NONE, (int)SRT_MSGNO_NONE);
    ST_REQUIRE_MSG(d.msgttl == SRT_MSGTTL_INF,
                   "srt_msgctrl_default itself carries msgttl=%d; TEST_INTERFACE.md section 5 "
                   "requires SRT_MSGTTL_INF (%d)", d.msgttl, (int)SRT_MSGTTL_INF);
}

// -------------------------------------------- g02 source-time write-back

SRT_CASE(g02_srctime_writeback, zero_srctime_send_reports_stamp_and_sequence) {
    // interface.md §2, correcting API-functions.md's "(unused for sending)" and
    // API.md's "pktseq: unused": on a successful srt_sendmsg2 the library
    // writes back srctime (non-zero even when the caller passed zero) and
    // pktseq (the first packet's sequence number, non-negative). msgno is
    // written at both tags and is deliberately not asserted.
    McLink L;
    mc_connect(L);

    const std::string m = st_payload(0, MC_MSG);
    SRT_MSGCTRL mc;
    srt_msgctrl_init(&mc);
    ST_REQUIRE_MSG(mc.srctime == 0,
                   "the initialised block did not start with srctime 0; this case "
                   "is about the write-back of a zero source time, and it would "
                   "prove nothing if it passed a non-zero one (got %lld)",
                   (long long)mc.srctime);

    int rc = srt_sendmsg2(L.cal, m.data(), (int)m.size(), &mc);
    int e  = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == (int)m.size(),
                   "srt_sendmsg2 with srctime 0 returned %d (errno %s (%d), \"%s\"); "
                   "TEST_INTERFACE.md section 5 requires the payload length %zu",
                   rc, mc_errname(e), e, srt_getlasterror_str(), m.size());

    ST_REQUIRE_MSG(mc.srctime != 0,
                   "after a successful srt_sendmsg2 with srctime 0 the caller's "
                   "block still carries srctime 0. TEST_INTERFACE.md section 5: the library writes "
                   "back \"the microsecond timestamp the message was actually "
                   "stamped with, which is non-zero even when the caller passed "
                   "zero\". A v1.4.0 library leaves it at 0 here.");
    ST_REQUIRE_MSG(mc.pktseq >= 0,
                   "after a successful srt_sendmsg2 the caller's block carries "
                   "pktseq %d; TEST_INTERFACE.md section 5 requires \"a non-negative value\" (the "
                   "sentinel SRT_SEQNO_NONE, %d, means the field was never written)",
                   (int)mc.pktseq, (int)SRT_SEQNO_NONE);

    // The stamp must be a real time near now, not an arbitrary non-zero. One
    // hour is far wider than any scheduling artefact on a loopback send and
    // far narrower than a sentinel or an uninitialised field.
    int64_t t0 = (int64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count();
    ST_REQUIRE_MSG(mc.srctime > t0 - 3600000000LL && mc.srctime < t0 + 3600000000LL,
                   "the srctime written back is %lld us, which is more than an hour "
                   "from the wall clock at the send (%lld us); the write-back has to "
                   "be the time the message was stamped with, not an arbitrary "
                   "non-zero value", (long long)mc.srctime, (long long)t0);

    // Positive control: the write-back is only half the claim. A library that
    // returns the length and scribbles into the block while transmitting
    // nothing would satisfy everything above, so the payload has to cross.
    std::vector<std::string> got = st_recv_stream(L.acc, 1, MC_MSG, MC_FULL_MS);
    ST_REQUIRE_MSG(got.size() == 1,
                   "the sender reported %zu bytes accepted under pktseq %d, but the "
                   "receiver got %zu message(s) within %d ms; the write-back does "
                   "not by itself prove anything was transmitted",
                   m.size(), (int)mc.pktseq, got.size(), MC_FULL_MS);
    ST_REQUIRE_MSG(got[0] == m,
                   "the message that arrived is not the one that was sent (%zu vs "
                   "%zu bytes)", got[0].size(), m.size());
}

SRT_CASE(g02_srctime_writeback, supplied_srctime_is_carried_not_overwritten) {
    // The complement of the case above, and the check that keeps it honest. If
    // the library simply overwrote srctime with "now" unconditionally, the
    // first case would still pass -- so this one hands it a time it did not
    // choose and requires the SEND-SIDE block to come back carrying that exact
    // value, and requires the resulting message to be scheduled from it.
    //
    // What the RECEIVER reports in its own block is a different field, and the
    // case is careful about that: in TSBPD mode the receive-side srctime is the
    // PLAY time the scheduler computed (packet timebase + the negotiated
    // latency), not the value the sender handed to srt_sendmsg2. Both are
    // meaningful and they differ by the latency. interface.md §2 describes the
    // send-side in/out contract and is what this case scores; it does not
    // define the receive-side value, so the two messages below are compared
    // against each other rather than against an absolute figure -- the supplied
    // time has to move the scheduled time by exactly the amount supplied.
    McLink L;
    mc_connect(L);

    // srt_connection_time returns int64_t: the microsecond stamp itself, not a
    // relative count, so truncating it into an int is the difference between a
    // real bound and 0.
    int64_t conn = srt_connection_time(L.cal);
    ST_REQUIRE_MSG(conn > 0,
                   "srt_connection_time on a connected socket returned %lld; the "
                   "bound this case needs cannot be established from that",
                   (long long)conn);
    int64_t t0 = (int64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count();
    ST_REQUIRE_MSG(t0 > conn,
                   "the wall clock at the send (%lld us) is not after the "
                   "connection's own start time (%lld us), so a valid source time "
                   "cannot be constructed here", (long long)t0, (long long)conn);

    // A time safely after the connection opened (srt_connection_time is the
    // socket's own start time, taken when it was opened at bind) and safely
    // before now, so it is neither stale nor in the future. The second message
    // is stamped SRC_GAP_US later, so the two scheduled times differ by that.
    const int64_t SRC_GAP_US = 2000000;   // 2 s: far wider than any scheduling artefact
    const int64_t supplied   = t0;

    SRT_MSGCTRL rm;
    int64_t play_a = 0, play_b = 0;

    for (int pass = 0; pass < 2; pass++) {
        const std::string m = st_payload(pass, MC_MSG);
        SRT_MSGCTRL mc;
        srt_msgctrl_init(&mc);
        mc.srctime = pass == 0 ? supplied : supplied + SRC_GAP_US;

        int rc = srt_sendmsg2(L.cal, m.data(), (int)m.size(), &mc);
        int e  = srt_getlasterror(NULL);
        ST_REQUIRE_MSG(rc == (int)m.size(),
                       "srt_sendmsg2 with a source time of %lld us, which is after "
                       "the connection start time %lld us, returned %d (errno %s (%d), "
                       "\"%s\"); TEST_INTERFACE.md section 5: \"a srctime equal to "
                       "srt_connection_time() on the sending socket is accepted\", and "
                       "anything later still more so",
                       (long long)mc.srctime, (long long)conn, rc, mc_errname(e), e,
                       srt_getlasterror_str());

        // The send-side half: the block is in/out, and the input must survive
        // it. A library that stamped every message with "now" would return a
        // different figure here, and the second pass would show it.
        ST_REQUIRE_MSG(mc.srctime == supplied + (pass == 0 ? 0 : SRC_GAP_US),
                       "the message was handed over with source time %lld us but "
                       "the caller's own block reports %lld us after the send; "
                       "TEST_INTERFACE.md section 5: the field is an in/out parameter and a "
                       "non-zero source time is the one the message is stamped "
                       "with, so the library must not substitute one of its own",
                       (long long)(supplied + (pass == 0 ? 0 : SRC_GAP_US)),
                       (long long)mc.srctime);
        ST_REQUIRE_MSG(mc.pktseq >= 0,
                       "the send succeeded but the caller's block carries pktseq "
                       "%d; TEST_INTERFACE.md section 5 requires a non-negative "
                       "value", (int)mc.pktseq);

        // Positive control: it has to be a real message, scheduled from the
        // time that was handed over.
        st_set_int(L.acc, SRTO_RCVTIMEO, MC_FULL_MS + (int)MC_LATENCY_MS);
        std::string buf((size_t)MC_MSG + 64, '\0');
        srt_msgctrl_init(&rm);
        int rrc = srt_recvmsg2(L.acc, &buf[0], (int)buf.size(), &rm);
        ST_REQUIRE_MSG(rrc == MC_MSG,
                       "the receiver returned %d (errno %s (%d)) for message %d "
                       "after a send the sender reported as successful; expected "
                       "%d bytes", rrc, mc_errname(srt_getlasterror(NULL)),
                       srt_getlasterror(NULL), pass, MC_MSG);
        ST_REQUIRE_MSG(buf.substr(0, (size_t)rrc) == m,
                       "message %d that arrived differs from the one sent", pass);
        if (pass == 0) play_a = rm.srctime; else play_b = rm.srctime;
    }

    ST_REQUIRE_MSG(play_a > 0 && play_b > 0,
                   "the receiver reported a zero source time for one of the two "
                   "stamped messages (%lld and %lld)", (long long)play_a, (long long)play_b);
    // Both messages were sent within microseconds of each other, so the two
    // play times differ by the difference between the stamps the caller
    // supplied -- not by an amount the library chose. This is what makes the
    // case a statement about the supplied time reaching the message rather than
    // about the caller's struct: a library that ignored the input entirely
    // would give two play times an equal distance apart as the wall clock
    // between the two sends, which is under a millisecond against the 2 s here.
    const int64_t gap = play_b - play_a;
    ST_REQUIRE_MSG(gap > SRC_GAP_US - 100000 && gap < SRC_GAP_US + 100000,
                   "the two messages were stamped %lld us apart but the receiver "
                   "scheduled their delivery %lld us apart; the supplied source "
                   "time is what the message is timed by, so the difference has "
                   "to carry through",
                   (long long)SRC_GAP_US, (long long)gap);
}

// ------------------------------------------------- g03 source-time bound

SRT_CASE(g03_srctime_bound, stale_srctime_is_refused_and_sends_nothing) {
    // interface.md: SRT_EINVALMSGAPI (5009) for a source time strictly earlier
    // than the connection start time; the refusal is total. The stale value is
    // 1 us since the epoch, which no socket in this process can have been
    // opened before.
    McLink L;
    mc_connect(L);

    const std::string m = st_payload(0, MC_MSG);
    SRT_MSGCTRL mc;
    srt_msgctrl_init(&mc);
    mc.srctime = 1;

    srt_clearlasterror();
    int rc = srt_sendmsg2(L.cal, m.data(), (int)m.size(), &mc);
    int e  = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "srt_sendmsg2 with a source time of 1 us *since 1970*, far "
                   "earlier than the connection's own start time (%lld us), "
                   "returned %d (the send was accepted); TEST_INTERFACE.md section 5 requires -1",
                   (long long)srt_connection_time(L.cal), rc);
    // Numeric comparison, never the text: srt_strerror composes a major and a
    // minor code into one sentence (deltas/06.md, driver hazards).
    ST_REQUIRE_MSG(e == SRT_EINVALMSGAPI,
                   "the stale source time was refused with %s (%d, \"%s\"); "
                   "TEST_INTERFACE.md section 5 requires SRT_EINVALMSGAPI (%d)",
                   mc_errname(e), e, srt_getlasterror_str(), (int)SRT_EINVALMSGAPI);

    // "transmits nothing": the receiver is given a bounded window and must see
    // nothing at all. The v1.4.0 library accepts this send, so the payload
    // arrives and this assertion is the one it fails.
    std::vector<std::string> got = st_recv_stream(L.acc, 1, MC_MSG, MC_EMPTY_MS);
    ST_REQUIRE_MSG(got.empty(),
                   "the refused send still put %zu message(s) on the wire; "
                   "TEST_INTERFACE.md section 5: the refusal \"transmits nothing\"", got.size());

    // ... and the connection survives it, which is the other half of the same
    // sentence -- a library that tears the connection down on the bad time
    // would also "transmit nothing" here.
    const std::string m2 = st_payload(1, MC_MSG);
    SRT_MSGCTRL mc2;
    srt_msgctrl_init(&mc2);
    int rc2 = srt_sendmsg2(L.cal, m2.data(), (int)m2.size(), &mc2);
    ST_REQUIRE_MSG(rc2 == (int)m2.size(),
                   "the send after a refused one returned %d (errno %s (%d)); "
                   "TEST_INTERFACE.md section 5 requires the connection to remain usable for the "
                   "next, valid send", rc2, mc_errname(srt_getlasterror(NULL)),
                   srt_getlasterror(NULL));
    std::vector<std::string> got2 = st_recv_stream(L.acc, 1, MC_MSG, MC_FULL_MS);
    ST_REQUIRE_MSG(got2.size() == 1 && got2[0] == m2,
                   "the valid send after the refusal was reported as accepted but "
                   "%zu message(s) arrived and the first %s the one sent; the "
                   "refusal must not have poisoned the connection",
                   got2.size(),
                   (got2.empty() || got2[0] != m2) ? "is not" : "is");
}

SRT_CASE(g03_srctime_bound, connection_start_time_is_accepted) {
    // interface.md: "the comparison is strict -- a srctime *equal* to
    // srt_connection_time() on the sending socket is accepted, only a strictly
    // smaller one is refused". Exactly equal is the boundary itself, so this is
    // the only case that separates a correct `<` from an off-by-one `<=`.
    //
    // srt_connection_time returns the socket's own start time, which is
    // recorded when the socket is opened (a bind or an accept). On the caller
    // it is therefore the moment of bind, before the handshake, and it is
    // stable afterwards -- the same figure the library compares against.
    McLink L;
    mc_connect(L);

    int64_t conn = srt_connection_time(L.cal);
    ST_REQUIRE_MSG(conn > 0,
                   "srt_connection_time on the connected sending socket returned "
                   "%lld; TEST_INTERFACE.md section 5 defines the accepted source times relative "
                   "to this value and it must be a real stamp", (long long)conn);

    const std::string m = st_payload(2, MC_MSG);
    SRT_MSGCTRL mc;
    srt_msgctrl_init(&mc);
    mc.srctime = conn;

    srt_clearlasterror();
    int rc = srt_sendmsg2(L.cal, m.data(), (int)m.size(), &mc);
    int e  = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == (int)m.size(),
                   "srt_sendmsg2 with srctime exactly equal to the connection start "
                   "time (%lld us, which is what srt_connection_time reports on this "
                   "socket) returned %d (errno %s (%d), \"%s\"); TEST_INTERFACE.md section 5: \"a "
                   "srctime equal to srt_connection_time() on the sending socket is "
                   "accepted\" -- only a strictly smaller one is refused",
                   (long long)conn, rc, mc_errname(e), e, srt_getlasterror_str());

    // Positive control: the boundary value has to carry a real message, or a
    // library that returns the length and drops everything would pass.
    std::vector<std::string> got = st_recv_stream(L.acc, 1, MC_MSG, MC_FULL_MS);
    ST_REQUIRE_MSG(got.size() == 1,
                   "the send at the connection start time was reported as accepted "
                   "(%d bytes) but %zu message(s) arrived within %d ms",
                   rc, got.size(), MC_FULL_MS);
    ST_REQUIRE_MSG(got[0] == m,
                   "the message sent at the connection start time arrived altered "
                   "(%zu vs %zu bytes)", got[0].size(), m.size());
}

SRT_CASE(g03_srctime_bound, zero_srctime_is_exempt_from_the_bound) {
    // interface.md: "srctime == 0 is exempt from the check entirely; it is the
    // 'stamp it now' request and must never be refused for being in the past."
    // A library that applied the bound to every value -- including the zero
    // that g02 requires to succeed -- would fail here with the same error the
    // stale case requires, so this is the control that keeps the two apart.
    McLink L;
    mc_connect(L);

    const std::string m = st_payload(3, MC_MSG);
    SRT_MSGCTRL mc;
    srt_msgctrl_init(&mc);
    ST_REQUIRE_MSG(mc.srctime == 0,
                   "srt_msgctrl_init left srctime at %lld, not 0, so this case no "
                   "longer tests the zero exemption", (long long)mc.srctime);

    srt_clearlasterror();
    int rc = srt_sendmsg2(L.cal, m.data(), (int)m.size(), &mc);
    int e  = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == (int)m.size(),
                   "srt_sendmsg2 with srctime 0, the \"stamp it now\" request, "
                   "returned %d with %s (%d, \"%s\"); TEST_INTERFACE.md section 5 "
                   "requires it to be accepted and never refused for being in the past",
                   rc, mc_errname(e), e, srt_getlasterror_str());
    ST_REQUIRE_MSG(mc.srctime != 0,
                   "the zero source time was accepted but no stamp was written back "
                   "(srctime is still 0)");

    std::vector<std::string> got = st_recv_stream(L.acc, 1, MC_MSG, MC_FULL_MS);
    ST_REQUIRE_MSG(got.size() == 1 && got[0] == m,
                   "the exempt zero-source-time send was reported as accepted but "
                   "%zu message(s) arrived and the first %s the one sent",
                   got.size(), (got.empty() || got[0] != m) ? "is not" : "is");
}

// ---------------------------------------------- g04 send-side accounting

// The reorder tolerance the accounting cases configure. It is distinguishable
// from the built-in default, which APISocketOptions.md gives as 0, so neither a
// zero-filled statistics structure nor an untouched default reads as a pass.
static const int MC_ACCT_TOL = 7;

// Sends `count` one-packet messages and then reads the statistics of BOTH ends.
// Shared by the cases below so that the assertions of this group describe one
// transfer rather than several transfers that happen to be configured alike;
// the group's subject is the counters, not the transfer.
//
// `caller_tol` and `listener_tol` are the SRTO_LOSSMAXTTL values configured
// before bind on the connecting socket and on the listener; a negative value
// leaves the option untouched on that side. That is what lets the inheritance
// case below tell "the accepted socket started at the listener's value" apart
// from "each socket happened to read back its own".
static void mc_acct_transfer(int count, int caller_tol, int listener_tol,
                             SRT_TRACEBSTATS* sperf, SRT_TRACEBSTATS* rperf) {
    McLink L;

    // TEST_INTERFACE.md section 5: the reorder tolerance "starts at the value
    // configured in SRTO_LOSSMAXTTL, and an accepted socket starts at the value
    // configured on the listener it was accepted from". SRTO_LOSSMAXTTL is a
    // `pre` option, so it goes on before bind -- st_connect applies the
    // lconf/cconf callbacks after srt_socket and before srt_bind.
    Link tmp;
    RelaySched no_loss;
    st_connect(tmp, no_loss, [listener_tol](SRTSOCKET s) {
                    mc_live_conf(s);
                    if (listener_tol >= 0) st_set_int(s, SRTO_LOSSMAXTTL, listener_tol);
                },
                [caller_tol](SRTSOCKET s) {
                    mc_live_conf(s);
                    if (caller_tol >= 0) st_set_int(s, SRTO_LOSSMAXTTL, caller_tol);
                });
    mc_adopt(L, tmp);

    // Exactly `count` messages, one packet each, on a clean loopback path.
    st_send_stream(L.cal, count, MC_MSG, MC_GAP_US);

    // The receiver is drained BEFORE either end's counters are read, so the
    // transfer is genuinely complete when they are; a send that never left the
    // sender's buffer would make "original transmissions" mean something weaker.
    //
    // The drain is what makes the send-side read below well defined, and it is
    // load-bearing rather than tidy. pktSentTotal and pktSentUniqueTotal are
    // incremented on the sending queue's worker thread inside packData, not when
    // srt_sendmsg2 returns, so a read taken straight after the send loop races
    // that thread and can see fewer packets than were handed over -- against an
    // assertion below that is an exact equality. Waiting for the receiver to hand
    // up all `count` messages means every one of them has been on the wire, so the
    // increments have already happened. Do NOT move these reads back above the
    // drain, and do not rely on st_send_stream's trailing inter-message sleep to
    // close the window: that sleep belongs to a shared helper, it is not a
    // synchronisation point, and at gap_us = 0 there would be none.
    std::vector<std::string> got = st_recv_stream(L.acc, count, MC_MSG, MC_FULL_MS);
    ST_REQUIRE_MSG((int)got.size() == count,
                   "the accounting transfer sent %d messages over a clean loopback "
                   "path and %zu arrived within %d ms; the counters below would be "
                   "about a transfer that did not happen as described",
                   count, got.size(), MC_FULL_MS);

    // The sender's own counters. bstats refuses an unconnected socket, so
    // reaching here at all is part of the control.
    std::memset(sperf, 0, sizeof *sperf);
    srt_clearlasterror();
    int rc = srt_bstats(L.cal, sperf, 0);
    ST_REQUIRE_MSG(rc == 0,
                   "srt_bstats on the sending socket returned %d (errno %s (%d), "
                   "\"%s\")", rc, mc_errname(srt_getlasterror(NULL)),
                   srt_getlasterror(NULL), srt_getlasterror_str());

    // And the accepted socket's counters, read beside the sender's. The reorder
    // tolerance is a receiver-side figure -- statistics.md's pktReorderTolerance
    // entry says "Receiver side" -- so the socket that reports it for this group
    // is the one the listener accepted, not the one that sent.
    std::memset(rperf, 0, sizeof *rperf);
    srt_clearlasterror();
    rc = srt_bstats(L.acc, rperf, 0);
    ST_REQUIRE_MSG(rc == 0,
                   "srt_bstats on the accepted (receiving) socket returned %d (errno "
                   "%s (%d), \"%s\")", rc, mc_errname(srt_getlasterror(NULL)),
                   srt_getlasterror(NULL), srt_getlasterror_str());
}

SRT_CASE(g04_send_accounting, reorder_tolerance_is_the_configured_loss_report_delay) {
    SRT_TRACEBSTATS sperf, rperf;
    mc_acct_transfer(MC_ACCT_COUNT, MC_ACCT_TOL, MC_ACCT_TOL, &sperf, &rperf);

    // TEST_INTERFACE.md section 5 states the starting value this milestone
    // requires: the configured SRTO_LOSSMAXTTL, decaying by at most one per
    // fifty in-order original deliveries -- which is why MC_ACCT_COUNT is 20.
    // The read is on the RECEIVER, which is the side statistics.md scopes the
    // figure to and the side the requirement names.
    ST_REQUIRE_MSG(rperf.pktReorderTolerance == MC_ACCT_TOL,
                   "srt_bstats reports pktReorderTolerance %d on the receiver after a "
                   "%d-message clean transfer; TEST_INTERFACE.md section 5 requires the "
                   "configured SRTO_LOSSMAXTTL, %d. (A transfer of 50 or more messages "
                   "would legitimately read one less.)",
                   rperf.pktReorderTolerance, MC_ACCT_COUNT, MC_ACCT_TOL);
    ST_REQUIRE_MSG(rperf.pktReorderTolerance > 0,
                   "pktReorderTolerance came back as %d; a zero here is also what a "
                   "default-initialised structure reports, so the transfer would not "
                   "be measuring the configured delay",
                   rperf.pktReorderTolerance);
}

SRT_CASE(g04_send_accounting, an_accepted_socket_starts_at_the_listeners_tolerance) {
    // The inheritance half of the same requirement: SRTO_LOSSMAXTTL is
    // configured before bind on the LISTENER only, and the socket it accepts
    // starts there rather than at its own default. Without this case the group
    // cannot tell inheritance from each end reading back what it was handed.
    SRT_TRACEBSTATS sperf, rperf;
    mc_acct_transfer(MC_ACCT_COUNT, -1, MC_ACCT_TOL, &sperf, &rperf);

    ST_REQUIRE_MSG(rperf.pktReorderTolerance == MC_ACCT_TOL,
                   "SRTO_LOSSMAXTTL was configured as %d on the listener and not at all "
                   "on the caller; the accepted socket reports pktReorderTolerance %d. "
                   "TEST_INTERFACE.md section 5 requires an accepted socket to start at "
                   "the value configured on the listener it was accepted from",
                   MC_ACCT_TOL, rperf.pktReorderTolerance);

    // The complement, and the reason the case discriminates: the caller never
    // configured the option, so its own tolerance stays at the documented
    // default of 0 (APISocketOptions.md gives SRTO_LOSSMAXTTL default 0, and the
    // tolerance is never raised above the maximum the option sets). A library
    // that reported one figure for every socket would satisfy the assertion
    // above and fail this one.
    ST_REQUIRE_MSG(sperf.pktReorderTolerance == 0,
                   "the caller never set SRTO_LOSSMAXTTL, yet reports pktReorderTolerance "
                   "%d; with the option left at its documented default of 0 the tolerance "
                   "has no maximum to rise to, so the %d the accepted socket reports must "
                   "come from the listener and not from a figure shared by every socket",
                   sperf.pktReorderTolerance, rperf.pktReorderTolerance);
}

SRT_CASE(g04_send_accounting, unique_sent_count_equals_the_messages_sent) {
    SRT_TRACEBSTATS sperf, rperf;
    mc_acct_transfer(MC_ACCT_COUNT, MC_ACCT_TOL, MC_ACCT_TOL, &sperf, &rperf);

    // TEST_INTERFACE.md section 5: "On a clean loopback connection over which
    // the application sent exactly N messages, each small enough for one packet,
    // pktSentUniqueTotal must equal N." The field is new in this milestone's
    // structure, so a build that did not add it cannot even name it.
    ST_REQUIRE_MSG(sperf.pktSentUniqueTotal == MC_ACCT_COUNT,
                   "srt_bstats reports pktSentUniqueTotal %lld after the "
                   "application sent exactly %d one-packet messages over a clean "
                   "loopback connection; TEST_INTERFACE.md section 5 requires the two "
                   "to be equal",
                   (long long)sperf.pktSentUniqueTotal, MC_ACCT_COUNT);
    ST_REQUIRE_MSG(sperf.pktSentUniqueTotal > 0,
                   "pktSentUniqueTotal is %lld, which is also what an untouched "
                   "statistics structure carries -- the read proves nothing",
                   (long long)sperf.pktSentUniqueTotal);

    // The counter is "original transmissions only, excluding retransmissions
    // and excluding protocol control packets", so the total-sent counter must
    // be at least as large. A library that aliased one to the other would
    // satisfy the equality above and fail the distinction that
    // TEST_INTERFACE.md section 5 draws between them.
    ST_REQUIRE_MSG(sperf.pktSentTotal >= sperf.pktSentUniqueTotal,
                   "pktSentTotal (%lld) is smaller than pktSentUniqueTotal (%lld); "
                   "the unique counter is a subset of the total by definition",
                   (long long)sperf.pktSentTotal, (long long)sperf.pktSentUniqueTotal);

    // The bytes counterpart is asserted for shape only, never for a value:
    // bstats adds the SRT+UDP+IP header size to every byte counter, which is
    // not part of the candidate-facing contract, but it must still bracket the
    // payload the application handed over.
    ST_REQUIRE_MSG(sperf.byteSentUniqueTotal >= (uint64_t)(MC_ACCT_COUNT * MC_MSG),
                   "byteSentUniqueTotal is %llu after %d messages of %d bytes; it "
                   "cannot be less than the %d payload bytes the application sent",
                   (unsigned long long)sperf.byteSentUniqueTotal, MC_ACCT_COUNT,
                   MC_MSG, MC_ACCT_COUNT * MC_MSG);
    ST_REQUIRE_MSG(sperf.byteSentUniqueTotal <= sperf.byteSentTotal,
                   "byteSentUniqueTotal (%llu) exceeds byteSentTotal (%llu); the "
                   "unique counter counts a subset of the transmissions",
                   (unsigned long long)sperf.byteSentUniqueTotal,
                   (unsigned long long)sperf.byteSentTotal);
}
