// Family "live", checkpoint 06 (SRT v1.4.2). Four scored groups, MIXED tier:
//
//   g00_endtoend        NEW        the refused post-bind configuration (six
//                                  options, SRT_EBOUNDSOCK, value preserved) plus a
//                                  4000-message clean-relay carry.
//   g09_recovery        inherited  loss recovered, statistics agree the loss
//                                  happened.
//   g10_tsbpd           inherited  the negotiated latency reported by srt_bstats,
//                                  stream intact under it.
//   g11_fec             inherited  an FEC filter supplies the missing packets with
//                                  no retransmission.
//
// ------------------------------------------------------------ tiers, and why
//                                                       this file is contorted
//
// endtoend.txt puts g00_endtoend in THIS family and E2E_TIER in meta.env is `new`.
// The other three are `inherited` (scored.txt). One family is one .cpp compiled
// ONCE, so this translation unit is compiled against the candidate's install
// prefix, which for a candidate still on the previous milestone is v1.4.0. Every
// group must therefore BUILD at v1.4.0:
//
//   * the three inherited groups must then report EXECUTED n FAILED 0, because
//     their whole purpose is to be earned by a candidate who has not built this
//     milestone yet;
//   * g00_endtoend must fail there ON AN ASSERTION naming actual versus expected,
//     never on a compile or link error. Its teeth are the six options whose guard
//     moved from m_bConnected to m_bOpened (deltas/06.md D3): at v1.4.0 all six
//     are accepted after a bind, at v1.4.2 they are refused with SRT_EBOUNDSOCK.
//
// The identifier surface is fixed by scored.txt's own note: SRTO_TSBPDMODE,
// SRTO_LATENCY, SRTO_RCVLATENCY, SRTO_PEERLATENCY, SRTO_TLPKTDROP, SRTO_NAKREPORT,
// SRTO_PACKETFILTER, SRT_EBOUNDSOCK, pktRcvLossTotal, pktSndFilterExtraTotal and
// msRcvTsbPdDelay all exist at v1.4.0 -- measured against the v1.4.0 install
// prefix, not assumed -- and nothing this milestone introduces may be added.
// srt_connection_time() would have been the natural way to spell the g00 stale-
// source-time case; it is new at v1.4.2 and is therefore reached through dlsym
// with a bounded fallback, so that the name never appears in a link-time
// reference. Nothing else here names a v1.4.2-only symbol.
//
// ------------------------------------------------------------ the version guard
//
// SRT_VERSION_VALUE comes from <srt/version.h>. That header is itself new at
// v1.3.0 and v1.2.0 ships none at all, so a bare `#if SRT_VERSION_VALUE >= ...`
// is a PREPROCESSOR SYNTAX ERROR wherever the header is absent -- both names
// expand to nothing and the directive reduces to `0 >= 0 (1, 4, 0)`. This family
// only ever builds at v1.4.0 or later, but the nesting below costs nothing and is
// the shape the brief asks for, so it is used throughout:
//
//     #ifdef SRT_VERSION_VALUE
//     #if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 0)
//     static_assert(<restated constant> == <number>, "...");
//     #endif
//     #endif
//
// ------------------------------------------------------------ enum collision
//
// The library is built -DENABLE_EXPERIMENTAL_BONDING=ON (meta.env CMAKE_EXTRA);
// this driver TU is not, and this stepdef sets no DRV_DEFS. Three SRT_SOCKOPT
// enumerators exist only in the library's translation unit, so every enumerator
// at or after SRTO_PEERIDLETIMEO (55) means a DIFFERENT NUMBER on the two sides of
// the call -- a driver writing a name up there silently addresses some other
// option. No case in this file names an option above 55. SRTO_PACKETFILTER is the
// one name above the block that this family needs, and it is explicitly numbered
// 60 upstream so it does not shift; it is still restated under #ifndef with the
// static_assert net, per DRIVER_RULES, so that a reordered header cannot move it
// silently.
//
// ------------------------------------------------------------ traceability
//
// Requirement ids are the frozen rows in tmp/<task>/REQUIREMENTS.md,
// "### Step 06 -- v1.4.2 (frozen 2026-09-21)"; each case carries its own.
//
// Deliberately NOT asserted, because nothing candidate-facing states it:
//
//   * nothing about SRT_MSGCTRL or the message-control API. That is family
//     msgctrl's surface and belongs in its file, not here.
//   * the listener access-control hook, srt_setrejectreason and the timeout
//     reason: family access. This family must keep compiling at v1.4.0, where
//     srt_setrejectreason does not exist and SRT_REJ_TIMEOUT is not in the
//     installed enumeration, so none of it may be named here even inside a case
//     that is never run at that prefix.
//   * anything about the group entry points or srt_bind_peerof: same reason, and
//     family access carries them.
//   * the reorder-tolerance report and the unique counters: family msgctrl's
//     g04_send_accounting, which is a `new` group and can therefore name the
//     fields this family must not.
//   * srt_epoll_wait on an empty EID (D1) and srt_strerror's new text (D2):
//     carryable at both tags, but neither is live-transport behaviour and neither
//     belongs in this family's four one-line groups.

#include "srtkit.h"

#include <dlfcn.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ------------------------------------------------- option constants, restated
//
// Every name here exists at v1.4.0 (measured against the v1.4.0 install prefix:
// SRTO_TLPKTDROP=31, SRTO_NAKREPORT=33, SRTO_LOSSMAXTTL=42, SRTO_LATENCY=23... see
// the static_asserts below, which re-derive each number from whatever header the
// file is actually compiled against). The #ifndef restatements are therefore
// inert at both prefixes and exist so that a candidate who reordered or renumbered
// the published enumeration still has to agree with the number the milestone
// documents -- silently addressing a different option is the worst failure this
// harness can have, and it is invisible without the net.

#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 0)
static_assert(SRTO_TSBPDMODE == 22, "restated SRTO_TSBPDMODE disagrees with <srt/srt.h>");
static_assert(SRTO_LATENCY == 23, "restated SRTO_LATENCY disagrees with <srt/srt.h>");
static_assert(SRTO_TLPKTDROP == 31, "restated SRTO_TLPKTDROP disagrees with <srt/srt.h>");
static_assert(SRTO_NAKREPORT == 33, "restated SRTO_NAKREPORT disagrees with <srt/srt.h>");
static_assert(SRTO_LOSSMAXTTL == 42, "restated SRTO_LOSSMAXTTL disagrees with <srt/srt.h>");
static_assert(SRTO_RCVLATENCY == 43, "restated SRTO_RCVLATENCY disagrees with <srt/srt.h>");
static_assert(SRTO_PEERLATENCY == 44, "restated SRTO_PEERLATENCY disagrees with <srt/srt.h>");
static_assert(SRTO_PACKETFILTER == 60, "restated SRTO_PACKETFILTER disagrees with <srt/srt.h>");
static_assert(SRT_EBOUNDSOCK == 5001, "restated SRT_EBOUNDSOCK disagrees with <srt/srt.h>");
#endif
#endif

#ifndef SRTO_PACKETFILTER
#define SRTO_PACKETFILTER ((SRT_SOCKOPT)60)  // v1.4.0 srt.h, explicit = 60
#endif

// ----------------------------------------------------------------- sizes
//
// In live mode the congestion controller refuses a RECEIVE buffer smaller than
// the maximum possible payload: congctl.cpp's `LiveCC::checkTransArgs` answers
// `size < m_zMaxPayloadSize` for TransDir_RCV with "buffer size: N is too small
// for the maximum possible M" and returns false, so `srt_recvmsg` fails with
// SRT_EINVOP and the channel reads as empty. Measured against the v1.4.2 install
// prefix while authoring: with a 1264-byte buffer every receive failed with
// "LiveCC: buffer size: 1264 is too small for the maximum possible 1316".
// st_recv_stream allocates `size + 64`, so the message size must leave at least
// SRT_LIVE_MAX_PLSIZE (1456) minus 16 (the SRT header) = 1316 bytes of buffer,
// i.e. a message of 1252 or more. 1300 clears that with margin and still fits
// one datagram, which is what lets a group drop "datagrams at fixed indexes" and
// know which messages it dropped.
//
// That check is identical at v1.4.0 (congctl.cpp:122), so the figure is safe for
// the three inherited groups too.
static const int LIVE_MSG = 1300;

// The same, for the FEC group. 1200 rather than 1300, because a configured
// packet filter lowers the maximum payload by its own extra budget -- two bytes
// for the FES flag spec and two for the length clip, 4 in total -- and a case
// that both needs headroom under the reduced maximum and must keep every message
// inside one datagram is safer smaller. The receive buffer argument is what the
// floor applies to, and st_recv_stream allocates `size + 64`, so 1200 leaves
// 1264 -- which is BELOW the 1316 floor measured above. The FEC cases therefore
// receive with LIVE_MSG, not FEC_MSG: the size argument to st_recv_stream is a
// buffer size, and the buffer is what must clear the floor. What must stay small
// is the SENT message, and that is what FEC_MSG is for.
static const int FEC_MSG = 1200;

// The relay's schedule indexes DATA packets only (relay.h), and one 1200-byte
// message is one data packet, so index i is message i. A case that sends N
// messages therefore drops a known set of messages regardless of how the
// control traffic interleaves.
static const int LIVE_GAP_US = 100;

// The two cases that require a loss or retransmission counter to be EXACTLY
// zero pace at 1 ms instead of LIVE_GAP_US. Any packet the host loses for its
// own reasons fails an exact zero, and relay.h forwards through a plain
// userspace UDP socket that it never resizes, so a packet that arrives while
// the relay thread is descheduled has only that socket's default receive
// buffer to wait in -- 208 KB on this kernel (net.core.rmem_default), and
// dropped_fwd() cannot see a packet the kernel discarded before the relay read
// it. What decides whether that matters is not the rate but whether the whole
// transfer fits in the buffer: at 100 x 1300 bytes this control offers about
// 136 KB and fits with roughly a third to spare, which is why it passes
// unpaced, whereas step 09's 200 x 1316 = 270 KB does not fit and its author
// measured 124 spurious losses in 200 packets before pacing to 1 ms.
//
// So the pacing here is insurance rather than a repair -- measured 11 times
// unpaced at this milestone, including under four spinning cores, with no
// spurious loss -- and it is cheap, 90 ms on a 100-message case. It is worth
// keeping because the headroom is finite: raising either case much above 150
// messages spends it, and then the exact zero becomes a race with nothing to
// warn you. It is deliberately NOT folded into LIVE_GAP_US: the groups that
// assert a lower bound on loss are indifferent to spurious loss, and g00's
// 4000-message case would pay four extra seconds for nothing.
static const int LIVE_CLEAN_GAP_US = 1000;

// The latency every lossless case configures, on BOTH sockets, before bind.
// Large enough that the relay's worst delay, not the clock, decides what
// arrives: relay.h holds a dropped packet for at most a few ms and the recovery
// takes an RTT, so 900 ms is two orders of magnitude of headroom.
static const int LIVE_LATENCY = 900;

// ---------------------------------------------------------- helper
//
// A live-lossless configuration plus the periodic loss report the groups in
// this family name. "Live lossless" is st_live_lossless from srtkit.h (TSBPD on, latency
// 900, TLPKTDROP off) with the plus-one-millisecond difference that this file
// wants a case to be able to see: SRTO_NAKREPORT on, so the receiver says what
// it is missing rather than relying on the sender's timeout.
static void live_conf(SRTSOCKET s) {
    st_set_int(s, SRTO_TSBPDMODE, 1);
    st_set_int(s, SRTO_LATENCY, LIVE_LATENCY);
    st_set_int(s, SRTO_TLPKTDROP, 0);
    st_set_int(s, SRTO_NAKREPORT, 1);
}

// A receive-side configuration for a channel that must be lossless. The sender
// keeps the same three settings through live_conf; both are applied before bind.
static void live_rcv_conf(SRTSOCKET s) {
    st_set_int(s, SRTO_TSBPDMODE, 1);
    st_set_int(s, SRTO_RCVLATENCY, LIVE_LATENCY);
    st_set_int(s, SRTO_TLPKTDROP, 0);
    st_set_int(s, SRTO_NAKREPORT, 1);
}

// Reads one bounded statistics snapshot. Returning by value keeps the callers
// free of a partially-initialised struct: a zeroed SRT_TRACEBSTATS is a legal
// input, but a case that read a field the library never filled would compare
// against zero and pass for the wrong reason.
static SRT_TRACEBSTATS bstats_of(SRTSOCKET s) {
    SRT_TRACEBSTATS st;
    std::memset(&st, 0, sizeof st);
    ST_REQUIRE_MSG(srt_bstats(s, &st, 0) != SRT_ERROR, "srt_bstats failed: %s",
                   srt_getlasterror_str());
    return st;
}

static int64_t now_ms() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// The connection start time, as the milestone itself measures it.
//
// srt_connection_time() is new at v1.4.2 (api-delta.md), so naming it directly
// would put a v1.4.2-only symbol in this family's link line and cost all four
// groups at v1.4.0 for a link reason -- exactly what the brief forbids. It is
// resolved by dlsym instead, and where it is absent the case falls back to a
// reading taken by the case itself at connect time, which is the same quantity
// from the other side of the API. Returns false when neither is available.
typedef int64_t (*connection_time_fn)(SRTSOCKET);

static bool connection_time(SRTSOCKET s, int64_t* out) {
    static connection_time_fn fn = nullptr;
    static bool probed = false;
    if (!probed) {
        probed = true;
        void* h = dlopen("libsrt.so", RTLD_NOW | RTLD_NOLOAD);
        if (!h) h = dlopen("libsrt.so.1", RTLD_NOW | RTLD_NOLOAD);
        if (h) fn = (connection_time_fn)dlsym(h, "srt_connection_time");
    }
    if (fn) {
        int64_t v = fn(s);
        if (v > 0) {
            *out = v;
            return true;
        }
    }
    return false;
}

// =====================================================================
// g00_endtoend -- this step's end-to-end path. SCORED, tier `new` (E2E_TIER
// in meta.env). It is NOT a gate group: the gate is proof of life plus the
// `inherited` groups, and it earns nothing.
//
// endtoend.txt composes this group from two halves:
//
//   (a) a live stream of 4000 messages configured entirely before bind --
//       timestamp-based delivery on, a receive latency, too-late drop off and
//       periodic loss reports on -- crosses a clean relay and arrives
//       byte-identical, in order, with no gaps and both sockets still connected;
//   (b) on a separate socket that has been bound but neither connected nor
//       listening all six of the options TEST_INTERFACE.md section 5 lists as
//       refused after bind -- timestamp-based delivery, the overall latency, the
//       receive latency, the peer latency, too-late drop and periodic loss
//       reports -- are refused with SRT_EBOUNDSOCK and each leaves the option
//       reporting the value it held before the refused call.
//
// (b) is what makes the group new: at v1.4.0 the guard on those six options is
// m_bConnected, so a bound-but-unconnected socket accepts all of them (D3, and
// measured at source for each of the six while freezing -- RQ-06-23).
// =====================================================================

// RQ-06-23, RQ-06-24 (negative) and RQ-06-25 (the positive control that keeps the
// boundary from being generalised). One case, because the subject is one rule:
// "these six are refused after bind, with this code, before storage".
SRT_CASE(g00_endtoend, bound_socket_refuses_the_six_and_keeps_the_old_value) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // Before bind: every setter is accepted, so the refusal below cannot be
    // attributed to a socket that never took a configuration. NOTE that the
    // GETTER for the three latency options does not report what was set -- at
    // all three tags in this chain `getOpt` answers SRTO_LATENCY/RCVLATENCY with
    // `m_iTsbPdDelay_ms`, which is only filled in at connect (measured: a fresh
    // socket that has had SRTO_LATENCY set to 400 reads back 0). Asserting a
    // round-trip here would therefore fail against a correct library, so the
    // round-trip half is asserted only for SRTO_TSBPDMODE and the preservation
    // half is asserted for all six against whatever the getter reported
    // immediately before the refused call -- which is the comparison that
    // actually distinguishes "guard before storage" from "guard after storage".
    st_set_int(s, SRTO_TSBPDMODE, 1);
    st_set_int(s, SRTO_TSBPDMODE, 0);
    st_set_int(s, SRTO_TSBPDMODE, 1);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_TSBPDMODE), 1);
    st_set_int(s, SRTO_LATENCY, LIVE_LATENCY);
    st_set_int(s, SRTO_RCVLATENCY, LIVE_LATENCY);
    st_set_int(s, SRTO_PEERLATENCY, LIVE_LATENCY);

    sockaddr_in a = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(s, (sockaddr*)&a, sizeof a) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());

    // The six options endtoend.txt names and TEST_INTERFACE.md section 5 lists as
    // refused after bind -- the four the stream above configures plus the two the
    // same guard covers, SRTO_RCVLATENCY and SRTO_PEERLATENCY. Each is paired with
    // the value the option held before the call, so "refused before storage" is
    // measured and not inferred: a guard placed after the assignment returns the
    // right error and still fails here (RQ-06-24).
    struct Probe {
        const char* name;
        SRT_SOCKOPT opt;
        int attempt;
    } probes[] = {
        {"SRTO_TSBPDMODE", SRTO_TSBPDMODE, 0},
        {"SRTO_LATENCY", SRTO_LATENCY, 250},
        {"SRTO_RCVLATENCY", SRTO_RCVLATENCY, 250},
        {"SRTO_PEERLATENCY", SRTO_PEERLATENCY, 250},
        {"SRTO_TLPKTDROP", SRTO_TLPKTDROP, 1},
        {"SRTO_NAKREPORT", SRTO_NAKREPORT, 0},
    };

    for (const Probe& p : probes) {
        // The baseline is read raw, not with the int helper: SRTO_NAKREPORT's
        // getter answers `*(bool *)` with optlen 1 (measured; see the note above
        // unbound_socket_accepts_the_six), so the helper's length check would fail
        // this case on a correct library. The raw read is the right instrument
        // here because the comparison below is between two readings of the SAME
        // getter -- a length the case does not care about cannot make it pass or
        // fail for the wrong reason, and a genuine read failure is still caught,
        // because SRT leaves the buffer untouched on error and the compare below
        // then sees a stale value.
        int before = -12345;
        ST_REQUIRE_MSG(st_try_get_int(s, p.opt, &before) != SRT_ERROR,
                       "%s could not be read on a bound socket: %s", p.name,
                       srt_getlasterror_str());

        int rc = st_try_set_int(s, p.opt, p.attempt);
        ST_REQUIRE_MSG(rc == SRT_ERROR,
                       "%s on a bound-but-unconnected socket returned %d, expected -1: "
                       "a socket that has been bound is past the boundary at this "
                       "milestone (v1.4.0 accepts all six here)",
                       p.name, rc);
        int err = srt_getlasterror(nullptr);
        ST_REQUIRE_MSG(err == SRT_EBOUNDSOCK,
                       "%s refused with %d, expected SRT_EBOUNDSOCK (%d): got \"%s\"",
                       p.name, err, SRT_EBOUNDSOCK, srt_strerror(err, 0));

        int after = -12345;
        ST_REQUIRE_MSG(st_try_get_int(s, p.opt, &after) != SRT_ERROR,
                       "%s could not be read after the refused set: %s", p.name,
                       srt_getlasterror_str());
        ST_REQUIRE_MSG(after == before,
                       "%s reports %d after the refused set, expected the value it "
                       "held before the call (%d): the refusal must run before the "
                       "value is stored",
                       p.name, after, before);
    }

    // The one option whose getter DOES reflect the pre-bind configuration at this
    // stage is SRTO_TSBPDMODE, and 1 is exactly what it was set to above. Asserting
    // it turns the loop's "the value did not change" into "the value did not change
    // FROM THE VALUE THAT WAS CONFIGURED" for at least one member of the set, so a
    // library that answered every getter with a constant would not slip through.
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_TSBPDMODE), 1);

    // RQ-06-25, positive control: the boundary is NOT generalised to every `pre`
    // option. SRTO_LOSSMAXTTL is settable after bind -- only its connected-state
    // reset is guarded -- so a candidate who refuses every `pre` option here fails
    // this group. Without this half, "refuse everything after bind" would score.
    st_set_int(s, SRTO_LOSSMAXTTL, 7);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_LOSSMAXTTL), 7);

    srt_close(s);
}

// RQ-06-23, positive control: the same six are accepted on a socket that has NOT
// been bound. Without this, a library that refused all six everywhere would pass
// the case above.
//
// The readback assertions cover only the options whose getter, at this stage,
// answers with what the setter stored:
//
//   * SRTO_TSBPDMODE is an int32_t on both sides (`m_bOPT_TsbPd`) and round-trips.
//   * SRTO_NAKREPORT and SRTO_TLPKTDROP do NOT round-trip through the int helper:
//     `getOpt` answers them with `*(bool *)optval` and sets optlen to
//     `sizeof(bool)` = 1, and SRTO_TLPKTDROP additionally reads `m_bTLPktDrop`
//     -- the runtime field, which the pre-bind setter does not touch (it writes
//     `m_bOPT_TLPktDrop`, and the two are only reconciled at connect). Both were
//     measured while authoring this case: the int reader fails on the length
//     check, and the value it does read is the RUNTIME default, 1, not the 0 that
//     was just set.
//   * the three latency names do not round-trip either: `getOpt` answers
//     SRTO_LATENCY and SRTO_RCVLATENCY with `m_iTsbPdDelay_ms` and
//     SRTO_PEERLATENCY with `m_iPeerTsbPdDelay_ms`, all three of which stay 0
//     until the handshake computes them.
//
// What this case proves about those five is the only thing observable at this
// stage, and it is exactly what the group's rule is about: the SET is ACCEPTED.
// st_set_int fails the case on SRT_ERROR, so a candidate who refused any of them
// before bind is caught here; a candidate who accepted them and stored them
// differently is caught by the transfer cases below, which measure the behaviour
// rather than the field.
SRT_CASE(g00_endtoend, unbound_socket_accepts_the_six) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    st_set_int(s, SRTO_TSBPDMODE, 1);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_TSBPDMODE), 1);
    st_set_int(s, SRTO_LATENCY, 400);
    st_set_int(s, SRTO_RCVLATENCY, 410);
    st_set_int(s, SRTO_PEERLATENCY, 420);
    st_set_int(s, SRTO_TLPKTDROP, 0);
    st_set_int(s, SRTO_NAKREPORT, 1);

    srt_close(s);
}

// RQ-06-23 again, from the state that the boundary is NOT about: a listener is
// bound AND listening, and the refusal there is still the bound-socket one, not
// a connection-level one. This pins the error code to SRT_EBOUNDSOCK as the
// milestone specifies rather than to SRT_ECONNSOCK, which is what a candidate who
// reused the connected-state guard would produce.
SRT_CASE(g00_endtoend, listening_socket_reports_boundsock_not_connsock) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    st_set_int(s, SRTO_TSBPDMODE, 1);

    sockaddr_in a = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(s, (sockaddr*)&a, sizeof a) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(s, 1) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());

    int rc = st_try_set_int(s, SRTO_TSBPDMODE, 0);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "SRTO_TSBPDMODE on a listening socket returned %d, "
                                    "expected -1 (v1.4.0 accepts it here)",
                   rc);
    int err = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(err == SRT_EBOUNDSOCK,
                   "SRTO_TSBPDMODE on a listening socket refused with %d, expected "
                   "SRT_EBOUNDSOCK (%d) and not a connection-level code: got \"%s\"",
                   err, SRT_EBOUNDSOCK, srt_strerror(err, 0));
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_TSBPDMODE), 1);

    srt_close(s);
}

// endtoend.txt (a): 4000 messages over a clean relay, configured entirely before
// bind, byte-identical, in order, no gaps, both sockets still connected.
//
// The count is endtoend.txt's own number. 4000 messages of 1200 bytes at a 100 us
// pacing gap is 0.4 s of pacing; the transfer itself is paced by the receiver,
// which is bounded by SRTO_RCVTIMEO and never by a sleep.
SRT_CASE(g00_endtoend, clean_relay_carries_four_thousand_messages_in_order) {
    Link L;
    RelaySched sched;  // clean: nothing dropped, nothing held, nothing delayed
    st_connect(L, sched, live_rcv_conf, live_conf);

    ST_REQUIRE_MSG(L.relay->seen_fwd() >= 0, "relay did not report");

    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, 4000, LIVE_MSG, 8000); });

    std::vector<std::string> sent = st_send_stream(L.cal, 4000, LIVE_MSG, LIVE_GAP_US);
    rcvr.join();

    ST_REQUIRE_MSG((int)got.size() == (int)sent.size(),
                   "receiver got %zu of %zu messages over a clean relay: the stream "
                   "was complete before this milestone and must still be",
                   got.size(), sent.size());

    int bad = st_first_out_of_order(sent, got);
    ST_REQUIRE_MSG(bad < 0, "stream arrived out of order at received index %d", bad);
    for (size_t i = 0; i < got.size(); i++) {
        ST_REQUIRE_MSG(got[i] == sent[i],
                       "message %zu differs from what was sent (%zu vs %zu bytes)",
                       i, got[i].size(), sent[i].size());
    }

    // Positive control on the CHANNEL, not on the application: the relay counts
    // what it actually forwarded and dropped. A clean schedule that silently
    // dropped nothing is the claim this case makes, so it is read rather than
    // assumed. `seen_fwd()` counts data packets only.
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 0,
                   "the relay dropped %d forward data packets, but this schedule is "
                   "clean -- the case did not carry what it claims to",
                   L.relay->dropped_fwd());
    ST_REQUIRE_MSG(L.relay->seen_fwd() >= 4000,
                   "the relay saw only %d forward data packets for 4000 messages",
                   L.relay->seen_fwd());

    // Both sockets still connected at the end.
    ST_REQUIRE_EQ_INT(srt_getsockstate(L.cal), SRTS_CONNECTED);
    ST_REQUIRE_EQ_INT(srt_getsockstate(L.acc), SRTS_CONNECTED);

    // Pin the connection with something other than the state enum: statistics
    // that only a carried stream produces.
    SRT_TRACEBSTATS rs = bstats_of(L.acc);
    ST_REQUIRE_MSG(rs.pktRecvTotal >= 4000,
                   "receiver reports %lld packets received for a 4000-message clean "
                   "stream",
                   (long long)rs.pktRecvTotal);
}

// RQ-06-10's exemption seen from the transport side, and the milestone's central
// send-side change (D10): a source time left at zero means "stamp it now" and is
// never refused, so an application that never fills the field keeps working.
// The message-control write-back itself is family msgctrl's subject; what this
// case establishes is that the exemption does not disturb the live path.
SRT_CASE(g00_endtoend, zero_source_time_still_carries_the_payload) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, live_rcv_conf, live_conf);

    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, 20, LIVE_MSG, 6000); });

    std::vector<std::string> sent;
    for (int i = 0; i < 20; i++) {
        std::string m = st_payload(i, LIVE_MSG);
        SRT_MSGCTRL mc = srt_msgctrl_default;
        mc.srctime = 0;  // "stamp it now"
        int rc = srt_sendmsg2(L.cal, m.data(), (int)m.size(), &mc);
        ST_REQUIRE_MSG(rc == (int)m.size(),
                       "srt_sendmsg2 with srctime 0 returned %d, expected the payload "
                       "length %zu (a zero source time is exempt from the past-time "
                       "check and is never refused): %s",
                       rc, m.size(), srt_getlasterror_str());
        sent.push_back(m);
        std::this_thread::sleep_for(std::chrono::microseconds(LIVE_GAP_US));
    }
    rcvr.join();

    ST_REQUIRE_MSG(got.size() == sent.size(),
                   "receiver got %zu of %zu messages sent with a zero source time",
                   got.size(), sent.size());
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) < 0, "stream arrived out of order");
    for (size_t i = 0; i < got.size(); i++)
        ST_REQUIRE_MSG(got[i] == sent[i], "message %zu differs from what was sent", i);
}

// RQ-06-08's other half, on the transport: the refused stale-source-time send
// transmits NOTHING and leaves the connection usable for the next valid send.
// This is the one case in g00_endtoend that needs a v1.4.2-only symbol, and it
// reaches it through dlsym with a fallback, so the file still links at v1.4.0.
SRT_CASE(g00_endtoend, stale_source_time_transmits_nothing_and_leaves_the_link_usable) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, live_rcv_conf, live_conf);

    // A reading of the connection start time taken from this side. Where the
    // library exports srt_connection_time (v1.4.2) it is the library's own; where
    // it does not (v1.4.0), the case reads the clock immediately after the
    // handshake completed, which is the same quantity to within the handshake's
    // own duration and is EARLIER-or-equal, so the "stale" value below is stale
    // under either reading.
    int64_t conn_t = 0;
    bool have_lib_time = connection_time(L.cal, &conn_t);
    if (!have_lib_time) conn_t = now_ms() * 1000;

    st_set_int(L.acc, SRTO_RCVTIMEO, 1500);

    // A stale source time: one second before the connection started.
    std::string stale = st_payload(0, LIVE_MSG);
    SRT_MSGCTRL mc = srt_msgctrl_default;
    mc.srctime = (int64_t)(conn_t - 1000000);
    int before_pkts = L.relay->seen_fwd();
    int rc = srt_sendmsg2(L.cal, stale.data(), (int)stale.size(), &mc);
    if (rc != SRT_ERROR) {
        // v1.4.0 accepts this silently (D11). That is what this case is FOR, so
        // say so plainly rather than reporting a harness problem.
        st_failf(__FILE__, __LINE__,
                 "srt_sendmsg2 with a source time 1 s older than the connection start "
                 "(%lld us) returned %d, expected -1 with SRT_EINVALMSGAPI (5009): the "
                 "past-time check does not exist at v1.4.0",
                 (long long)mc.srctime, rc);
    }
    int err = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(err == 5009,
                   "stale source time refused with %d, expected SRT_EINVALMSGAPI (5009): "
                   "got \"%s\"",
                   err, srt_strerror(err, 0));

    // It transmitted nothing. Read this two ways, because either alone is
    // weaker: (i) the bounded receive below must time out rather than return
    // bytes, and (ii) the relay -- which counts forwarded DATA packets and never
    // invents them -- must have seen no new one. A refused send that still put a
    // packet on the wire fails (ii) even if the receiver's own picture were
    // somehow empty.
    char buf[LIVE_MSG + 64];
    int rrc = srt_recvmsg(L.acc, buf, (int)sizeof buf);
    ST_REQUIRE_MSG(rrc == SRT_ERROR,
                   "the receiver got %d bytes after a refused send, expected nothing: a "
                   "refused send must not queue or transmit",
                   rrc);
    ST_REQUIRE_MSG(L.relay->seen_fwd() == before_pkts,
                   "the relay forwarded %d data packets across the refused send, "
                   "expected 0: a refused send must not reach the wire",
                   L.relay->seen_fwd() - before_pkts);

    // ...and the connection is still usable for the next, valid, send.
    std::string good = st_payload(1, LIVE_MSG);
    SRT_MSGCTRL mc2 = srt_msgctrl_default;
    mc2.srctime = 0;
    int rc2 = srt_sendmsg2(L.cal, good.data(), (int)good.size(), &mc2);
    ST_REQUIRE_MSG(rc2 == (int)good.size(),
                   "the valid send after a refused one returned %d, expected %zu: the "
                   "connection must survive the refusal",
                   rc2, good.size());

    // The receiver thread sets its own timeout; the 1500 ms set above for the
    // empty-channel probe is replaced inside st_recv_stream, so the two probes do
    // not fight over SRTO_RCVTIMEO.
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, 1, LIVE_MSG, 4000); });
    rcvr.join();
    ST_REQUIRE_MSG(got.size() == 1, "expected exactly 1 message after the refusal, got %zu",
                   got.size());
    ST_REQUIRE_MSG(got[0] == good, "the message that did arrive differs from the one "
                                   "that was sent after the refusal");
    ST_REQUIRE_EQ_INT(srt_getsockstate(L.cal), SRTS_CONNECTED);
}

// =====================================================================
// g09_recovery -- inherited. downstream of the send path's origin-time change.
//
// scored.txt: "with too-late packet drop switched off before bind and a receive
// latency large enough to cover the path, a message stream whose datagrams are
// dropped at fixed indexes arrives complete and in order, and the statistics call
// reports a non-zero cumulative receive-loss count on the receiver and a non-zero
// cumulative retransmitted-packet count on the sender, so the loss is known to
// have happened rather than to have been absent".
//
// Every identifier here exists at v1.4.0. Measured by running the group against
// the v1.4.0 prefix (see the report): it passes there, which is what `inherited`
// means.
// =====================================================================

SRT_CASE(g09_recovery, dropped_datagrams_are_recovered_in_order) {
    Link L;
    RelaySched sched;
    sched.drop_fwd = {10, 11, 40, 70, 71, 72, 120};
    st_connect(L, sched, live_rcv_conf, live_conf);

    const int N = 200;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, LIVE_MSG, 8000); });

    std::vector<std::string> sent = st_send_stream(L.cal, N, LIVE_MSG, LIVE_GAP_US);
    rcvr.join();

    // The channel really did drop: the relay counts the data packets it removed,
    // so this is the honest way to prove the case is not passing on a clean
    // channel by accident.
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 7,
                   "the relay dropped %d forward data packets, expected the 7 the "
                   "schedule names",
                   L.relay->dropped_fwd());

    ST_REQUIRE_MSG((int)got.size() == N,
                   "receiver got %zu of %d messages after 7 dropped datagrams, with "
                   "TLPKTDROP off and %d ms of receive latency (recovery must fill "
                   "every gap)",
                   got.size(), N, LIVE_LATENCY);
    int bad = st_first_out_of_order(sent, got);
    ST_REQUIRE_MSG(bad < 0, "stream arrived out of order at received index %d", bad);
    for (size_t i = 0; i < got.size(); i++)
        ST_REQUIRE_MSG(got[i] == sent[i],
                       "message %zu differs from what was sent (%zu vs %zu bytes)",
                       i, got[i].size(), sent[i].size());

    SRT_TRACEBSTATS rs = bstats_of(L.acc);
    ST_REQUIRE_MSG(rs.pktRcvLossTotal > 0,
                   "receiver reports pktRcvLossTotal = %d after %d datagrams were "
                   "dropped: the loss must be REPORTED, not merely survived",
                   rs.pktRcvLossTotal, L.relay->dropped_fwd());

    SRT_TRACEBSTATS ss = bstats_of(L.cal);
    ST_REQUIRE_MSG(ss.pktRetransTotal > 0,
                   "sender reports pktRetransTotal = %d after the receiver reported "
                   "%d lost packets: the recovery must show up as retransmission",
                   ss.pktRetransTotal, rs.pktRcvLossTotal);
}

// Positive control for the case above. The case above requires the loss
// counters to be non-zero after the schedule drops seven datagrams; on its own
// that is satisfied by a library whose counters are non-zero for reasons of
// their own -- one that reports loss unconditionally, or reports a constant, or
// leaves the field uninitialised -- which would earn the group its weight
// without ever detecting a drop. This case runs the same transfer over a
// schedule that drops nothing and requires zero, so the pair together say the
// counters TRACK the channel rather than merely being non-zero when it is
// lossy. The exact zero is the point of the case and is deliberate; a tolerance
// here would readmit the constant-non-zero library the pair exists to exclude.
//
// Two things keep the exact zero honest rather than a race. The relay's own
// deterministic drop count is asserted first, so a schedule that misbehaved
// fails on the relay's count and not on the library's. And the stream is paced
// at LIVE_CLEAN_GAP_US rather than LIVE_GAP_US, because dropped_fwd() counts
// only the packets the relay removed on purpose -- a packet lost to the relay's
// own receive buffer overflowing never increments it, and pacing is the only
// thing that prevents it.
SRT_CASE(g09_recovery, clean_relay_reports_no_loss) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, live_rcv_conf, live_conf);

    const int N = 100;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, LIVE_MSG, 6000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, LIVE_MSG, LIVE_CLEAN_GAP_US);
    rcvr.join();

    ST_REQUIRE_MSG(got.size() == sent.size(), "clean relay delivered %zu of %zu messages",
                   got.size(), sent.size());
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) < 0, "stream arrived out of order");

    // Checked before the library's counters: this is the relay's own count of
    // the packets it removed, it is deterministic, and the schedule named none.
    // If it ever fails, the zeros below were never going to mean anything.
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 0,
                   "the relay removed %d forward data packets on a schedule that "
                   "names none; the loss counters below would be measuring the "
                   "relay and not the library",
                   L.relay->dropped_fwd());

    SRT_TRACEBSTATS rs = bstats_of(L.acc);
    ST_REQUIRE_MSG(rs.pktRcvLossTotal == 0,
                   "receiver reports pktRcvLossTotal = %d on a clean relay, expected 0",
                   rs.pktRcvLossTotal);
    SRT_TRACEBSTATS ss = bstats_of(L.cal);
    ST_REQUIRE_MSG(ss.pktRetransTotal == 0,
                   "sender reports pktRetransTotal = %d on a clean relay, expected 0",
                   ss.pktRetransTotal);
}

// The receive-latency half of the group's sentence: with too-late drop off, a
// latency large enough to cover the path, and a drop that takes longer than one
// RTT to notice, the message is still delivered rather than discarded. This is
// what "the latency is large enough to cover the path" means operationally, and
// it bounds the case above: raising the latency is what turns the drop into a
// recoverable one.
SRT_CASE(g09_recovery, a_large_receive_latency_is_what_makes_the_drop_recoverable) {
    Link L;
    RelaySched sched;
    sched.drop_fwd = {5, 6};
    // 1500 ms of receive latency: three times the case's usual budget.
    st_connect(L, sched,
               [](SRTSOCKET s) {
                   st_set_int(s, SRTO_TSBPDMODE, 1);
                   st_set_int(s, SRTO_RCVLATENCY, 1500);
                   st_set_int(s, SRTO_TLPKTDROP, 0);
               },
               [](SRTSOCKET s) {
                   st_set_int(s, SRTO_TSBPDMODE, 1);
                   st_set_int(s, SRTO_LATENCY, 1500);
                   st_set_int(s, SRTO_TLPKTDROP, 0);
               });

    const int N = 60;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, LIVE_MSG, 8000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, LIVE_MSG, LIVE_GAP_US);
    rcvr.join();

    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 2, "relay dropped %d, expected 2",
                   L.relay->dropped_fwd());
    ST_REQUIRE_MSG(got.size() == sent.size(),
                   "receiver got %zu of %zu messages with a 1500 ms receive latency "
                   "covering a 2-datagram drop",
                   got.size(), sent.size());
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) < 0, "stream arrived out of order");

    // The latency in force is reported, not assumed (RQ-06-16 carries the same
    // claim in g10; restated here because this case's premise is that the figure
    // is large).
    SRT_TRACEBSTATS rs = bstats_of(L.acc);
    ST_REQUIRE_MSG(rs.msRcvTsbPdDelay >= 1500,
                   "receiver reports msRcvTsbPdDelay = %d, expected at least 1500",
                   rs.msRcvTsbPdDelay);
}

// =====================================================================
// g10_tsbpd -- inherited.
//
// scored.txt: "with timestamp-based delivery enabled and the same receive
// latency configured on both sides before bind, the receiver reports that same
// figure as the delay actually in force, and the stream still arrives complete
// and in order".
//
// This group is a GATE group, and a gate case must be provably deterministic
// or not exist. The clause about delivery being HELD until the latency has
// elapsed used to be asserted here, by timing a message end to end and
// requiring at least 700 ms of a 900 ms latency. That is an assertion about
// wall-clock elapsed time on a shared VM, so it was deleted -- see
// misc/<task-id>/NOTES.md. What remains is the pair of claims that can be
// settled by reading values back: the negotiated figure the receiver reports,
// and the stream arriving complete and in order under it.
// =====================================================================

// RQ-06-16: msRcvTsbPdDelay reflects the delay actually in force, not a constant.
// Two sockets, two configured latencies, two different reported figures -- which
// is what makes this a measurement rather than a hard-coded comparison.
//
// The sender is queried too, and reports the SAME figure, because the filler is
// `perf->msRcvTsbPdDelay = isOPT_TsbPd() ? m_iTsbPdDelay_ms : 0;` on both ends
// (measured against the v1.4.2 prefix; the assignment sites are core.cpp's
// reset path, fillSrtHandshake under `if (m_bOPT_TsbPd)` and
// processSrtMsg_HSREQ). Two readings that track the same configured figure from
// opposite ends is stronger than one: a constant cannot satisfy both at two
// different latencies.
SRT_CASE(g10_tsbpd, reported_delay_tracks_the_configured_latency) {
    struct Case {
        int latency;
    } cases[] = {{300}, {900}};

    for (const Case& c : cases) {
        Link L;
        RelaySched sched;
        int lat = c.latency;
        st_connect(L, sched,
                   [lat](SRTSOCKET s) {
                       st_set_int(s, SRTO_TSBPDMODE, 1);
                       st_set_int(s, SRTO_RCVLATENCY, lat);
                       st_set_int(s, SRTO_TLPKTDROP, 0);
                   },
                   [lat](SRTSOCKET s) {
                       st_set_int(s, SRTO_TSBPDMODE, 1);
                       st_set_int(s, SRTO_LATENCY, lat);
                       st_set_int(s, SRTO_TLPKTDROP, 0);
                   });

        SRT_TRACEBSTATS rs = bstats_of(L.acc);
        ST_REQUIRE_MSG(rs.msRcvTsbPdDelay == lat,
                       "with %d ms configured on both sides the receiver reports "
                       "msRcvTsbPdDelay = %d, expected %d",
                       lat, rs.msRcvTsbPdDelay, lat);
        SRT_TRACEBSTATS ss = bstats_of(L.cal);
        ST_REQUIRE_MSG(ss.msRcvTsbPdDelay == lat,
                       "with %d ms configured on both sides the sender reports "
                       "msRcvTsbPdDelay = %d, expected the same %d it was configured "
                       "with: the field is the delay in force at the reading end, and "
                       "both ends are in timestamp-based mode here",
                       lat, ss.msRcvTsbPdDelay, lat);
    }
}

// The group's second clause: the stream still arrives complete and in order under
// a latency as large as 900 ms.
SRT_CASE(g10_tsbpd, stream_arrives_complete_and_in_order_under_latency) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, live_rcv_conf, live_conf);

    const int N = 60;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, LIVE_MSG, 8000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, LIVE_MSG, LIVE_GAP_US);
    rcvr.join();

    ST_REQUIRE_MSG(got.size() == sent.size(),
                   "receiver got %zu of %zu messages under a %d ms latency", got.size(),
                   sent.size(), LIVE_LATENCY);
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) < 0, "stream arrived out of order");
    for (size_t i = 0; i < got.size(); i++)
        ST_REQUIRE_MSG(got[i] == sent[i], "message %zu differs from what was sent", i);

    SRT_TRACEBSTATS rs = bstats_of(L.acc);
    ST_REQUIRE_EQ_INT(rs.msRcvTsbPdDelay, LIVE_LATENCY);
}

// =====================================================================
// g11_fec -- inherited.
//
// scored.txt: "with the same well-formed packet-filter configuration string set
// on both ends before bind and datagrams dropped at fixed indexes, the stream
// arrives complete and in order without any retransmission being required, and
// the statistics call reports a non-zero cumulative count of extra packets
// generated by the filter on the sender".
//
// "without any retransmission being required" is what distinguishes this group
// from g09: the same shape of loss, but the recovery comes from the filter. The
// case reads the sender's retransmission counter to say so rather than assuming
// it.
// =====================================================================

// The filter configuration. `fec,cols:10` is a complete configuration
// (REQUIREMENTS.md RQ-07-07 records the same fact for the next step: `cols` is
// required and `rows`, `layout` and `arq` all have defaults), and one FEC control
// packet is generated per column, which is what makes pktSndFilterExtraTotal
// non-zero on the sender.
//
// `arq:never` is what makes the group's "without any retransmission being
// required" clause CHECKABLE rather than merely likely. The filter's default
// fallback level is `arq:onreq` (measured at v1.4.2, fec.cpp: the parse leaves
// m_fallback_level at SRT_ARQ_ONREQ when no `arq` parameter is present), and at
// that level the filter REPORTS the packets it could not rebuild back to SRT's
// loss list, which asks for a retransmission -- so a drop that lands outside its
// FEC group produces pktRetransTotal > 0 through no fault of the filter. Measured
// while authoring: with a bare `fec,cols:10` and 11 scattered drops the sender
// reported pktRetransTotal = 9, and the case failed on a correct library.
// `arq:never` suppresses that fallback, so the drops are the filter's whole job
// and the retransmission counter is an honest measurement of "the filter did it".
// The parser accepts `never` as one of never/onreq/always (fec.cpp:92-113) and
// the control case below uses the same string with the filter absent, so the
// comparison isolates the filter and not the string.
static const char FEC_CONF[] = "fec,cols:10,arq:never";

// The filter needs 4 bytes of payload budget (two for the flag spec, two for the
// length clip), so the messages here stay at 1200 and clear SRT_LIVE_MAX_PLSIZE
// (1456) minus that budget with room to spare.
static void fec_conf(SRTSOCKET s) {
    st_set_int(s, SRTO_TSBPDMODE, 1);
    st_set_int(s, SRTO_LATENCY, LIVE_LATENCY);
    st_set_int(s, SRTO_TLPKTDROP, 0);
    st_set_str(s, SRTO_PACKETFILTER, FEC_CONF);
}

static void fec_rcv_conf(SRTSOCKET s) {
    st_set_int(s, SRTO_TSBPDMODE, 1);
    st_set_int(s, SRTO_RCVLATENCY, LIVE_LATENCY);
    st_set_int(s, SRTO_TLPKTDROP, 0);
    st_set_str(s, SRTO_PACKETFILTER, FEC_CONF);
}

// RQ-06-23's neighbour, and the precondition for the rest of the group: the
// filter configuration is accepted on an unbound socket and reads back. Without
// this a candidate whose filter parser rejects the string would fail the group
// for a reason the group never states.
SRT_CASE(g11_fec, filter_configuration_is_accepted_and_reads_back) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    st_set_str(s, SRTO_PACKETFILTER, FEC_CONF);

    char buf[256];
    int len = (int)sizeof buf;
    ST_REQUIRE_MSG(srt_getsockflag(s, SRTO_PACKETFILTER, buf, &len) != SRT_ERROR,
                   "reading SRTO_PACKETFILTER back failed: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(len > 0, "SRTO_PACKETFILTER read back as 0 bytes");
    std::string got(buf, (size_t)len);
    ST_REQUIRE_MSG(got == std::string(FEC_CONF),
                   "SRTO_PACKETFILTER reads back as \"%s\", expected \"%s\"", got.c_str(),
                   FEC_CONF);

    srt_close(s);
}

// The group's main case: drops at fixed indexes, complete and in order, no
// retransmission required, and a non-zero filter-packet count on the sender.
SRT_CASE(g11_fec, filter_recovers_the_drops_without_retransmission) {
    Link L;
    RelaySched sched;
    const int N = 120;
    // ONE drop per FEC group. `cols:10` makes a horizontal group ten data packets
    // wide, so dropping a single member of three widely separated groups is the
    // exact case the filter exists for: each group is closed by its own control
    // packet, one member is missing, and the filter rebuilds it. A cluster of
    // drops -- one whole column of ten, which an earlier draft of this case used
    // -- can exceed what one group can repair, and the filter then legitimately
    // hands the remainder back to the ARQ, which is a true report about the drop
    // and not about the filter.
    sched.drop_fwd = {10, 27, 46};
    st_connect(L, sched, fec_rcv_conf, fec_conf);

    std::vector<std::string> got;
    // The buffer is LIVE_MSG (the floor applies to the RECEIVE buffer), the
    // message is FEC_MSG (the filter reduced the maximum payload).
    //
    // Paced at LIVE_CLEAN_GAP_US for the reason given where that constant is
    // declared: the last assertion of this case requires the sender's
    // cumulative retransmission count to be exactly zero, so a packet lost to
    // the relay's own receive buffer -- which dropped_fwd() cannot see -- would
    // be repaired by the ARQ and fail an assertion about the filter.
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, LIVE_MSG, 8000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, FEC_MSG, LIVE_CLEAN_GAP_US);
    rcvr.join();

    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 3,
                   "the relay dropped %d forward data packets, expected the 3 the "
                   "schedule names",
                   L.relay->dropped_fwd());

    ST_REQUIRE_MSG(got.size() == sent.size(),
                   "receiver got %zu of %zu messages after 3 dropped datagrams -- one "
                   "in each of three separate FEC groups -- with the filter configured "
                   "identically on both ends",
                   got.size(), sent.size());
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) < 0, "stream arrived out of order");
    for (size_t i = 0; i < got.size(); i++)
        ST_REQUIRE_MSG(got[i] == sent[i],
                       "message %zu differs from what was sent (%zu vs %zu bytes)", i,
                       got[i].size(), sent[i].size());

    SRT_TRACEBSTATS ss = bstats_of(L.cal);
    ST_REQUIRE_MSG(ss.pktSndFilterExtraTotal > 0,
                   "sender reports pktSndFilterExtraTotal = %d with an FEC filter "
                   "configured: the filter must report the extra packets it generated",
                   ss.pktSndFilterExtraTotal);

    // "without any retransmission being required". The assertion is on the
    // sender's cumulative retransmission count, which is the honest reading of
    // that clause: the filter supplied what the ARQ would otherwise have had to.
    // A loss this size with no filter needs retransmission (g09 case 1 measures
    // exactly that), so the pair is a controlled comparison.
    ST_REQUIRE_MSG(ss.pktRetransTotal == 0,
                   "sender reports pktRetransTotal = %d: with an FEC filter covering "
                   "the drops the recovery must not fall back on retransmission",
                   ss.pktRetransTotal);
}

// The positive control the group's "without any retransmission" clause needs: the
// SAME drop pattern with no filter configured does require retransmission. Without
// this, a library that simply never counts retransmissions would pass the case
// above; with it, the two counters are shown to be responsive. The only difference
// between the two cases is the filter's presence: the drop schedule, the message
// size and the latency are identical.
SRT_CASE(g11_fec, the_same_drops_without_a_filter_do_require_retransmission) {
    Link L;
    RelaySched sched;
    sched.drop_fwd = {10, 27, 46};
    st_connect(L, sched, live_rcv_conf, live_conf);

    const int N = 120;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, LIVE_MSG, 8000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, FEC_MSG, LIVE_GAP_US);
    rcvr.join();

    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 3, "relay dropped %d, expected 3",
                   L.relay->dropped_fwd());
    ST_REQUIRE_MSG(got.size() == sent.size(),
                   "receiver got %zu of %zu messages with no filter configured",
                   got.size(), sent.size());
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) < 0, "stream arrived out of order");

    SRT_TRACEBSTATS ss = bstats_of(L.cal);
    ST_REQUIRE_MSG(ss.pktRetransTotal > 0,
                   "sender reports pktRetransTotal = 0 after 3 dropped datagrams with "
                   "NO filter configured: without the filter the drops can only be "
                   "recovered by retransmission, so this counter must move");
    ST_REQUIRE_MSG(ss.pktSndFilterExtraTotal == 0,
                   "sender reports pktSndFilterExtraTotal = %d with no filter "
                   "configured, expected 0",
                   ss.pktSndFilterExtraTotal);
}
