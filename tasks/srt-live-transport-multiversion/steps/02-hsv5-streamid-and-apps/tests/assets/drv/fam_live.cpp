// Family "live", checkpoint 02 (SRT v1.3.0). Three groups -- one scored and
// two gate. tests/assets/groups.txt is the statement of record for the kinds:
//
//   g00_endtoend    new        SCORED, this step's end-to-end path: 4000 live messages cross a
//                              clean relay byte-identically and in order, and
//                              a second stream of the same length travels back
//                              down that same connection -- from the accepted
//                              socket to the caller, which declared itself the
//                              sender -- and arrives the same way, leaving both
//                              sockets connected.
//   g10_recovery    inherited  datagrams dropped at fixed indexes are
//                              retransmitted and the payload is delivered
//                              complete and in order with too-late drop off,
//                              the sender's retransmission counter and the
//                              receiver's loss counter both showing the loss.
//   g12_encryption  inherited  a matching passphrase with an explicit key
//                              length carries the payload intact at 128 and
//                              256 bits and never in clear on the wire; two
//                              different passphrases deliver nothing; the
//                              receiver's keying state reaches SECURED in the
//                              matching case.
//
// ===========================================================================
// TIERS, AND THE IDENTIFIERS THIS FILE IS ALLOWED TO NAME
// ===========================================================================
//
// This family MIXES TIERS. g00_endtoend is `new` (meta.env: E2E_TIER=new) and
// must FAIL against the previous release, v1.2.0. g10 and g12 are
// `inherited`: they exist to be earned by a candidate who shipped v1.2.0 and
// did nothing at all for this milestone, so they must PASS there. One family is
// one compiled `.cpp`, so all three verdicts have to come out of a single binary
// that BUILDS at both prefixes. DRIVER_RULES, "Naming an identifier the
// candidate may not have", is therefore binding on every line below.
//
// The answer it gets here is NOT the restatement idiom of stepdefs/03. It is
// the stronger one, and it is this file's standing rule:
//
//   The end-to-end group shares the `live` family with both regression
//   groups, and it is safe here only because that family names nothing this
//   milestone introduces: it reaches the reverse stream through
//   `SRTO_SENDER`, which milestone 01 already had. Keep it that way. The
//   moment a `live` case names `SRTO_RCVLATENCY`, `SRTO_STREAMID`,
//   `srt_bistats` or any other name from this milestone, the whole file stops
//   compiling against a tree that has not done this milestone at all, and
//   both regression groups score zero for a compile reason rather than a
//   behavioural one -- and a gate group lost for any reason zeroes the step.
//
// So this file restates no constant and dlsym's no function, because it needs
// neither. g00_endtoend's discriminator is purely BEHAVIOURAL. deltas/02-headline.md
// row H1: on a connection whose caller set SRTO_SENDER=1, reverse data at
// v1.2.0 gives `srt_recvmsg` -1 / SRT_ECONNLOST (2001) at the caller and breaks
// both sockets ("receiving data in SRT sender-only side: breaking connection"),
// while at v1.3.0 both directions work freely. SRTO_SENDER is slot 21,
// explicitly numbered and present in both headers, so g00_endtoend fails at
// the previous prefix on an assertion of its own -- "N of 4000 messages came back"
// -- and not on a compile or link error.
//
// The complete list of library identifiers this file names, each checked
// against BOTH installed headers before it was used:
//
//   options    SRTO_SENDER (21), SRTO_TSBPDMODE (22), SRTO_LATENCY (23),
//              SRTO_PASSPHRASE (26), SRTO_PBKEYLEN (27), SRTO_TLPKTDROP (31),
//              SRTO_RCVTIMEO (14), SRTO_RCVKMSTATE (41)
//   calls      srt_socket (via st_new_socket), srt_bind, srt_listen,
//              srt_accept, srt_connect, srt_close, srt_sendmsg, srt_recvmsg,
//              srt_getsockflag, srt_setsockflag, srt_getsockstate, srt_bstats,
//              srt_getlasterror, srt_getlasterror_str, srt_strerror,
//              srt_clearlasterror
//   enums      SRT_SOCKSTATUS (SRTS_*), SRT_KM_STATE (SRT_KM_S_*), the
//              SRT_ERRNO members common to both tags
//   counters   pktRecvTotal, pktSentTotal, pktRcvLossTotal, pktRetransTotal,
//              pktRcvDropTotal, pktRcvUndecrypt
//
// Four names that would have been the obvious way to write parts of this file
// are deliberately ABSENT, and each would have cost the two inherited groups
// their point for a compile reason:
//
//   * SRTO_RCVLATENCY (43). Does not exist at v1.2.0 (deltas/02-headline.md
//     H2), so naming it anywhere in this file would stop the whole family
//     compiling against the inherited tag and cost both GATE groups in it for
//     a compile reason rather than a regression -- and a GATE group lost for
//     any reason zeroes the whole step, because reward = gate x score.
//   * SRTO_SNDPEERKMSTATE (40). Present at v1.2.0, RENAMED to SRTO_SNDKMSTATE
//     at v1.3.0 -- so neither name compiles at both tags. g12 therefore reads
//     the keying state only through SRTO_RCVKMSTATE (41), which is what its
//     description asks for ("the receiver's keying state").
//   * SRTO_SNDPBKEYLEN / SRTO_RCVPBKEYLEN (38/39). Retired at this milestone
//     (deltas/02.md D5); appapi's g08_optslots owns what is left of them.
//   * srt_bistats. New at this milestone (deltas/02.md D13); appapi's g09_stats
//     owns it. g12 and g10 use srt_bstats, which both tags export.
//
// One step-02-specific trap, recorded because the worked example in
// stepdefs/03 cannot be copied literally here. DRIVER_RULES' static_assert net
// is written
//
//     #if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 3, 2)
//
// and that form is a PREPROCESSOR SYNTAX ERROR at v1.2.0: <srt/version.h> is
// itself new at v1.3.0, so neither macro is defined, `SRT_MAKE_VERSION_VALUE`
// is replaced by 0 and the directive reads `0 >= 0 (1, 3, 2)`. Anything in this
// step that needs the net must nest it:
//
//     #ifdef SRT_VERSION_VALUE
//     #if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 3, 0)
//     static_assert(...);
//     #endif
//     #endif
//
// Nothing here needs it, because nothing here is restated. No static_assert is
// placed on the header's OWN enumerators either: this driver is compiled
// against the candidate's header, and an assert on a value this file did not
// invent could only turn a wrong option number into a build failure for the
// whole family -- losing the two inherited groups for exactly the reason the
// rule exists to prevent.
//
// ===========================================================================
// TRACEABILITY
// ===========================================================================
//
// g00_endtoend. endtoend.txt gives the claim. stepdefs/02/scope.md is the source
// for the behaviour: "Both directions on one connection. Either end may send;
// either end may receive. Data arriving at a socket that set the sender flag is
// delivered to its application instead of breaking the connection. One key, one
// latency negotiation and one set of buffers serve both directions."
// interface.md fixes the shape of the experiment: "The gate sends its forward
// stream from the caller, which set the sender flag, then sends a second stream
// of the same length from the accepted socket back to that same caller, on the
// same connection, with no further configuration on either side -- no second
// connect, no second passphrase, no second latency setting. Both sockets must
// still be connected when the reverse stream has been read."
//
// g10_recovery. inherit.md names it: "NAK-driven retransmission with too-late
// drop switched off". interface.md: "Loss recovery still needs too-late drop
// switched off. g10_recovery sets it off and a generous latency before asking
// for a complete payload, and reads the sender's retransmission counter and the
// receiver's loss counter to confirm the loss happened." srt-docs/ API.md on
// SRTO_TLPKTDROP: "When true (default), it will drop the packets that haven't
// been retransmitted on time ... You can turn this off to always ensure a clean
// delivery"; srt.h on the counters read here: pktRcvLossTotal is the "total
// number of lost packets (receiver side)", pktRetransTotal the "total number of
// retransmitted packets", pktRcvDropTotal the "number of too-late-to play
// missing packets".
//
// g12_encryption. inherit.md: "AES payload encryption reaching the secured
// keying state and delivering nothing when the two passphrases differ."
// interface.md: "A passphrase alone still does not encrypt anything.
// SRTO_PBKEYLEN still defaults to 0 ... so g12_encryption sets the key length
// explicitly to 16 or to 32 on the sending side before the passphrase has any
// effect", and "The key exchange is still driven by traffic, not by the
// handshake ... g12_encryption drives it with tagged warm-up traffic, skips
// that traffic in the receive loop, and waits on the observed state rather than
// on a timer." handoff-keep.md fixes the mismatch outcome: "a connection that
// completes, a keying state that says so, and payload that never decrypts".
//
// ===========================================================================
// DETERMINISM
// ===========================================================================
//
// Nothing here waits on a clock for control flow. Loss and delay come from
// RelaySched at fixed DATA-packet indexes; every receive runs on its own thread
// bounded by SRTO_RCVTIMEO; the key exchange is waited out on the OBSERVED
// keying state with a bounded number of warm-up messages, never on a timer; no
// case sleeps to drain. NO clock reading decides a verdict anywhere in the
// file: the only two that did were g11_tsbpd's, and they were deleted with
// that group on 2026-09-21 (NOTES.md, "Gate cases deleted for
// non-determinism"). live_now_ms survives for one purpose, a failure message
// that reports how long a key exchange took, and no assertion reads it.
//
// The statistics this file reads are g00_endtoend's pktRecvTotal,
// pktSentTotal and pktRcvDropTotal, g10_recovery's pktRcvLossTotal,
// pktRetransTotal and pktRcvDropTotal, and g12_encryption's pktRcvUndecrypt.
// Every bound on a counter a busy host can RAISE is a floor, never a ceiling:
// a busy host can only ADD incidental loss, so it can never push a lossy run
// under a floor, while a ceiling on a clean run is a statement about the host
// rather than about the library. The two ceilings that used to stand in
// g10_recovery were deleted under that rule and under the same NOTES.md
// heading, and "pktRcvLossTotal == 0 on a clean channel" -- which is not a
// reproducible observation on a userspace relay sharing a VM with nine other
// build sessions -- is asserted here in no form. The zeroes that remain are
// on pktRcvDropTotal, which the case has configured the library not to touch
// by disabling too-late drop, and on pktRcvUndecrypt with a matching
// passphrase, where a non-zero value is a decryption failure and not load.
// Both surviving floors are documented at their definitions.
//
// ===========================================================================
// DELIBERATELY NOT ASSERTED
// ===========================================================================
//
//   * any exact value of pktRcvLossTotal or pktRetransTotal. How many
//     retransmissions a schedule provokes depends on how many NAKs cross and on
//     whether a retransmission is itself caught by the schedule (the relay's
//     index space includes retransmitted data packets), and no document fixes
//     it. DRIVER_RULES rule 5: shape, never a particular number.
//   * how LONG recovery takes. The latency is a budget, not a deadline any
//     document states, so the cases assert that delivery happened inside a
//     generous SRTO_RCVTIMEO and never that it happened by a particular time.
//   * which direction of g00_endtoend's connection is "the" data direction, or
//     ordering between the two streams beyond the one interface.md specifies
//     (forward first, then reverse, on the same connection).
//   * the SENDER's view of the peer's keying state. The option that carries it
//     is SRTO_SNDPEERKMSTATE at v1.2.0 and SRTO_SNDKMSTATE at v1.3.0; no single
//     spelling compiles at both, and g12's description asks only for the
//     receiver's state.
//   * any rejection of a mismatched passphrase. This milestone completes the
//     connection and reports BADSECRET (handoff-keep.md); rejection arrives
//     with SRTO_STRICTENC at v1.3.2 and belongs to stepdefs/03.
//   * loss in the BACKWARD direction. RelaySched::drop_bwd is not data-indexed
//     -- the backward path of a one-way live stream is almost entirely control
//     -- so a schedule there would drop a timing-dependent set of ACKs and the
//     group would stop being reproducible.
//   * SRTO_NAKREPORT. It is on by default and it is part of why recovery works
//     here, but this step's documents do not require a candidate to have it, so
//     no case names it.
#include "stest.h"
#include "srtkit.h"
#include "relay.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ===========================================================================
// sizes, budgets and bands
// ===========================================================================

// The documented live payload size (srt-docs/ API.md, SRTT_LIVE defaults:
// "SRTO_PAYLOADSIZE = 1316"). One message is then exactly one data packet,
// which is what lets a drop schedule expressed in data-packet indexes be read
// as "these messages went missing". It is also at or below the largest message
// a live receiver will hand back in one piece at either tag.
static const int LIVE_PAYLOAD = 1316;

// The end-to-end stream, in each direction. endtoend.txt fixes the count at
// 4000; the
// point of the size is that "byte-identical and in order" is a claim about a
// stream rather than about a handful of packets.
static const int E2E_N = 4000;
static const int E2E_GAP_US = 300;
static const int E2E_RECV_MS = 10000;

// Messages per recovery/TSBPD transfer, and the pacing between them. The pacing
// is not decoration: this is a userspace relay on a shared box, and a heavier
// burst makes the kernel itself lose the occasional datagram. At 2 ms the relay
// was measured clean, and 250 messages is long enough for a schedule that drops
// forty consecutive datagrams to still leave a clean tail.
static const int LIVE_N = 250;
static const int LIVE_GAP_US = 2000;

// Receive budgets, in milliseconds. These bound SRTO_RCVTIMEO -- they are exit
// conditions for a stalled channel, never sleeps. A clean transfer of LIVE_N
// messages costs about (LIVE_N * LIVE_GAP_US) plus one 900 ms latency, well
// under two seconds, so both budgets are several times the expected cost.
static const int LIVE_CLEAN_MS = 8000;
static const int LIVE_LOSSY_MS = 10000;

// The loss floor. LIVE_SCHEDULED_MIN is what a case with a drop schedule must
// clear: the smallest schedule below removes thirty datagrams and the receiver
// has been measured reporting every one of them, so the floor sits under every
// measurement while a clean channel -- measured at 0 loss, 0 retransmission --
// stays far below it. A floor is safe in a gate group in a way its mirror image
// is not: a busy host can only ADD incidental loss, so it can never push a
// lossy run under the floor, while a CEILING on a clean run is a statement
// about the host. The ceiling LIVE_INCIDENTAL_MAX = 20 that used to stand here
// was deleted with the two assertions that read it; see NOTES.md, "Gate cases
// deleted for non-determinism".
static const int LIVE_SCHEDULED_MIN = 25;

// g12's message size. Small enough that a marker-filled body is cheap to scan
// for on the wire, comfortably above st_payload's 32-byte minimum. Note that
// the RECEIVE buffer is always payload + 4096 and never this: from v1.3.0 a
// live srt_recvmsg refuses a buffer below the declared payload size
// (deltas/02.md D9), so a buffer sized to the message would turn every g12
// receive into an error and an empty channel would be indistinguishable from a
// working one.
static const int KM_MSG = 512;
static const int KM_RECV_SLACK = 4096;

// Bound on the key-exchange warm-up: 400 messages 2 ms apart, so at worst
// ~800 ms. The exit condition is the OBSERVED keying state, not this bound.
static const int KM_WARM_MAX = 400;
static const int KM_GAP_US = 2000;

// How long a g12 receive that is EXPECTED to yield everything, and one expected
// to yield nothing, are each given before the case decides. Both bound
// SRTO_RCVTIMEO; neither is slept.
static const int KM_FULL_MS = 4000;
static const int KM_EMPTY_MS = 2500;

static const char KM_PW_A[] = "live-step02-passphrase-alpha";
static const char KM_PW_B[] = "live-step02-passphrase-bravo";

// A marker long enough that its appearance in a forwarded datagram cannot be a
// coincidence, and short enough to repeat inside every payload.
static const char KM_NEEDLE[] = "CANARY-02-4d7e1a90-IN-THE-CLEAR";
static const char KM_WARMTAG[] = "WARMUP";

// ===========================================================================
// naming
//
// Local rather than added to drv/srtkit.h: that header must compile at every
// tag in the chain and must not grow API surface for one family. These exist so
// a failure line names an error symbolically instead of printing 2001.
//
// if/else rather than switch on purpose. The two headers spell SRT_ERRNO
// differently (v1.2.0 casts the UDT_* enumerators, v1.3.0 composes MJ()/MN()
// macros) and a switch would turn any pair that happened to collide at one tag
// into a compile error for the whole family.
// ===========================================================================

static const char* live_errname(int e) {
    if (e == 0) return "SRT_SUCCESS";
    if (e == SRT_ECONNSETUP) return "SRT_ECONNSETUP";
    if (e == SRT_ENOSERVER) return "SRT_ENOSERVER";
    if (e == SRT_ECONNREJ) return "SRT_ECONNREJ";
    if (e == SRT_ECONNFAIL) return "SRT_ECONNFAIL";
    if (e == SRT_ECONNLOST) return "SRT_ECONNLOST";
    if (e == SRT_ENOCONN) return "SRT_ENOCONN";
    if (e == SRT_EINVOP) return "SRT_EINVOP";
    if (e == SRT_EBOUNDSOCK) return "SRT_EBOUNDSOCK";
    if (e == SRT_ECONNSOCK) return "SRT_ECONNSOCK";
    if (e == SRT_EINVPARAM) return "SRT_EINVPARAM";
    if (e == SRT_EINVSOCK) return "SRT_EINVSOCK";
    if (e == SRT_EUNBOUNDSOCK) return "SRT_EUNBOUNDSOCK";
    if (e == SRT_ELARGEMSG) return "SRT_ELARGEMSG";
    if (e == SRT_EASYNCSND) return "SRT_EASYNCSND";
    if (e == SRT_EASYNCRCV) return "SRT_EASYNCRCV";
    if (e == SRT_ETIMEOUT) return "SRT_ETIMEOUT";
    if (e == SRT_ECONGEST) return "SRT_ECONGEST";
    if (e == SRT_EPEERERR) return "SRT_EPEERERR";
    return "<other SRT_ERRNO>";
}

static const char* live_statename(SRT_SOCKSTATUS s) {
    if (s == SRTS_INIT) return "SRTS_INIT";
    if (s == SRTS_OPENED) return "SRTS_OPENED";
    if (s == SRTS_LISTENING) return "SRTS_LISTENING";
    if (s == SRTS_CONNECTING) return "SRTS_CONNECTING";
    if (s == SRTS_CONNECTED) return "SRTS_CONNECTED";
    if (s == SRTS_BROKEN) return "SRTS_BROKEN";
    if (s == SRTS_CLOSING) return "SRTS_CLOSING";
    if (s == SRTS_CLOSED) return "SRTS_CLOSED";
    if (s == SRTS_NONEXIST) return "SRTS_NONEXIST";
    return "<unknown SRT_SOCKSTATUS>";
}

static const char* live_kmname(int v) {
    if (v == SRT_KM_S_UNSECURED) return "SRT_KM_S_UNSECURED";
    if (v == SRT_KM_S_SECURING) return "SRT_KM_S_SECURING";
    if (v == SRT_KM_S_SECURED) return "SRT_KM_S_SECURED";
    if (v == SRT_KM_S_NOSECRET) return "SRT_KM_S_NOSECRET";
    if (v == SRT_KM_S_BADSECRET) return "SRT_KM_S_BADSECRET";
    return "<other SRT_KM_STATE>";
}

// ===========================================================================
// transfer machinery
//
// srtkit's st_send_stream/st_recv_stream cannot serve this family:
//
//   * st_send_stream ASSERTS every send. g00_endtoend's reverse stream is a send
//     the previous release is expected to refuse, and an assertion buried in a
//     helper would report "srt_sendmsg #0 returned -1" -- true, but it names
//     the mechanism rather than the claim. The case must fail on its own
//     sentence, "n of 4000 messages came back", with the refusal quoted
//     alongside it.
//   * st_recv_stream sizes its buffer at payload + 64. From v1.3.0 a live
//     srt_recvmsg refuses any buffer below the declared payload size
//     (deltas/02.md D9), which is 1316 by default, so that buffer is fine for
//     a 1316-byte message and fatally too small for g12's 512-byte one: every
//     receive would fail and an encrypted channel would be indistinguishable
//     from a working one. Every buffer here is payload + 4096.
//   * neither can run on a thread. Both throw STFail, and an exception leaving
//     a std::thread is std::terminate -- the case would die by signal instead
//     of reporting.
//
// So the send loop and the receive loop below are NON-THROWING: they record
// what the library did and hand it back, and the case decides. Every receive
// is still bounded by SRTO_RCVTIMEO and nothing sleeps to synchronise.
// ===========================================================================

static long long live_now_ms() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// The index st_payload stamped into the first bytes, or -1 if this is not one
// of ours. Used only to make failure messages readable.
static int live_msg_index(const std::string& m) {
    if (m.size() < 11) return -1;
    if (m.compare(0, 3, "MSG") != 0) return -1;
    return std::atoi(m.c_str() + 3);
}

static SRT_TRACEBSTATS live_stats(SRTSOCKET s) {
    SRT_TRACEBSTATS st;
    std::memset(&st, 0, sizeof st);
    ST_REQUIRE_MSG(srt_bstats(s, &st, 0) != SRT_ERROR, "srt_bstats: %s", srt_getlasterror_str());
    return st;
}

// One directed transfer: what was offered, what came back, and every refusal
// the library returned on the way.
struct LiveXfer {
    std::vector<std::string> sent;
    std::vector<std::string> got;
    int sends_ok = 0;
    int first_bad_send = -1;
    int first_bad_send_rc = 0;
    int first_bad_send_err = 0;
    bool recv_failed = false;
    int recv_err = 0;
    bool setopt_failed = false;
    long long elapsed_ms = 0;

    // Appended to the delivery assertion so one failure line carries both the
    // count that was wrong and the reason the library gave for it. Without
    // this g00_endtoend at v1.2.0 would say only "0 of 4000 came back" and leave
    // the reader to guess whether the send was refused or the receive was.
    std::string note() const {
        char b[320];
        std::string r;
        if (first_bad_send < 0) {
            std::snprintf(b, sizeof b, "all %d sends accepted", sends_ok);
        } else {
            std::snprintf(b, sizeof b,
                          "%d of %d sends accepted, first refused at #%d with rc=%d %s",
                          sends_ok, (int)sent.size(), first_bad_send, first_bad_send_rc,
                          live_errname(first_bad_send_err));
        }
        r = b;
        if (setopt_failed) r += "; could not set SRTO_RCVTIMEO on the receiver";
        if (recv_failed) {
            std::snprintf(b, sizeof b, "; receive stopped on %s", live_errname(recv_err));
            r += b;
        }
        // Reported, never asserted: no case here passes or fails on a measured
        // duration. It is in the failure line because "0 of 4000 arrived" reads
        // very differently after 20 ms than after the full receive timeout.
        std::snprintf(b, sizeof b, "; the exchange took %lld ms", elapsed_ms);
        r += b;
        return r;
    }
};

// Bounded, non-throwing receive loop. Runs on its own thread so the send loop
// can pace itself on this one; SRTO_RCVTIMEO, not a sleep, is what ends it.
static void live_rx_collect(SRTSOCKET s, int want, int size, int timeo_ms, LiveXfer* x) {
    if (st_try_set_int(s, SRTO_RCVTIMEO, timeo_ms) == SRT_ERROR) x->setopt_failed = true;
    std::vector<char> buf((size_t)size + KM_RECV_SLACK);
    while ((int)x->got.size() < want) {
        int rc = srt_recvmsg(s, buf.data(), (int)buf.size());
        if (rc == SRT_ERROR) {
            x->recv_failed = true;
            x->recv_err = srt_getlasterror(nullptr);
            break;
        }
        if (rc == 0) break;
        x->got.push_back(std::string(buf.data(), (size_t)rc));
    }
}

// Sends `count` messages from `tx` while `rx` reads them, and reports what
// happened on both sides. Nothing here asserts: a refusal is data.
static void live_exchange(SRTSOCKET tx, SRTSOCKET rx, int count, int size, int gap_us,
                          int timeo_ms, LiveXfer* x) {
    x->sent.reserve((size_t)count);
    for (int i = 0; i < count; i++) x->sent.push_back(st_payload(i, size));

    long long t0 = live_now_ms();
    std::thread reader([&] { live_rx_collect(rx, count, size, timeo_ms, x); });
    for (int i = 0; i < count; i++) {
        const std::string& m = x->sent[(size_t)i];
        int rc = srt_sendmsg(tx, m.data(), (int)m.size(), -1, 1);
        if (rc == (int)m.size()) {
            x->sends_ok++;
        } else if (x->first_bad_send < 0) {
            x->first_bad_send = i;
            x->first_bad_send_rc = rc;
            x->first_bad_send_err = srt_getlasterror(nullptr);
            srt_clearlasterror();
        }
        if (gap_us > 0) std::this_thread::sleep_for(std::chrono::microseconds(gap_us));
    }
    reader.join();
    x->elapsed_ms = live_now_ms() - t0;
}

// "byte-identical, in order, with no gaps", asserted as one claim per message
// rather than as a checksum, so the message that went wrong is named.
static void live_require_complete_stream(const char* what, const LiveXfer& x) {
    ST_REQUIRE_MSG(x.got.size() == x.sent.size(),
                   "%s: expected all %d messages to arrive, %d arrived (%s)", what,
                   (int)x.sent.size(), (int)x.got.size(), x.note().c_str());
    for (size_t i = 0; i < x.sent.size(); i++) {
        ST_REQUIRE_MSG(x.got[i].size() == x.sent[i].size(),
                       "%s: message at position %d: expected %d bytes, got %d", what, (int)i,
                       (int)x.sent[i].size(), (int)x.got[i].size());
        ST_REQUIRE_MSG(x.got[i] == x.sent[i],
                       "%s: message at position %d: expected the payload stamped MSG%08d, got the "
                       "one stamped MSG%08d -- out of order or substituted",
                       what, (int)i, live_msg_index(x.sent[i]), live_msg_index(x.got[i]));
    }
}

// The relay's own account of what it did. This is the positive control for
// every loss case: without it, a case that dropped nothing because the
// schedule named indexes past the end of the stream would still pass.
static void live_require_relay_did_it(const Relay& r, int expect_dropped, int data_offered) {
    ST_REQUIRE_MSG(r.control_fwd() > 0,
                   "relay forwarded %d control datagrams, expected at least the handshake",
                   r.control_fwd());
    ST_REQUIRE_MSG(r.seen_fwd() >= data_offered,
                   "relay indexed %d forward data packets, expected at least the %d offered",
                   r.seen_fwd(), data_offered);
    ST_REQUIRE_MSG(r.dropped_fwd() == expect_dropped,
                   "relay dropped %d forward data packets, expected exactly the %d its schedule "
                   "names",
                   r.dropped_fwd(), expect_dropped);
}

static void live_require_connected(const char* who, SRTSOCKET s) {
    SRT_SOCKSTATUS st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_CONNECTED, "%s: expected SRTS_CONNECTED, got %s", who,
                   live_statename(st));
}

// The configuration every group in this family starts from.
//
// SRTO_SENDER=1 on the caller is not decoration and it is not a hint to the
// harness. At v1.2.0 the extension exchange is HSv4 (deltas/02-headline.md
// H4): the two-phase SRT handshake is initiated by whichever side declares
// itself the sender, and without the flag neither TSBPD nor the key exchange
// is negotiated at all. So every group here sets it, at both tags -- which is
// also precisely what makes the reverse stream a discriminator rather
// than a trivial transfer, and what keeps this file free of any name that
// arrived at this milestone.
static void live_conf_sender(SRTSOCKET s) {
    st_set_int(s, SRTO_SENDER, 1);
    st_live_lossless(s);
}

static void live_conf_peer(SRTSOCKET s) { st_live_lossless(s); }

// ===========================================================================
// g00_endtoend -- the end-to-end path (tier: new, so SCORED, not GATE)
//
// endtoend.txt: "a live stream of 4000 messages crosses a clean relay and arrives
// byte-identical, in order, with no gaps, and a second stream of the same
// length sent back down that same connection -- from the side that accepted it
// to the side that declared itself the sender -- arrives byte-identical and in
// order as well, leaving both sockets connected".
//
// Read as an experiment, that is four claims in sequence, and the order is
// what makes the group discriminate cleanly:
//
//   1. the connection comes up through the relay              (both versions)
//   2. the forward stream arrives complete and in order       (both versions)
//   3. the REVERSE stream, on that same connection, with no
//      reconfiguration, arrives complete and in order         (v1.3.0 only)
//   4. both sockets are still connected afterwards            (v1.3.0 only)
//
// Claims 1 and 2 are the positive control for claims 3 and 4. A build that
// cannot carry data at all fails on claim 2 and says so; a v1.2.0 build
// carries the forward stream perfectly and then fails on claim 3, which is
// exactly the milestone. deltas/02-headline.md H1 gives the v1.2.0 behaviour:
// "srt_recvmsg at the caller returns -1 with SRT_ECONNLOST (2001) ... and both
// sockets go to SRTS_BROKEN", logged upstream as "%s: Packet DATA (0)
// received in SENDER-ONLY connection; breaking the connection". Claim 3's
// message therefore quotes both the delivered count and the refusal, so the
// failure at v1.2.0 names actual versus expected and cannot be mistaken for a
// timeout, a link error or a harness fault.
//
// SRTO_SENDER is set on the caller in both cases. That is required, not
// incidental: endtoend.txt says "from the side that accepted it to the side that
// declared itself the sender", and without the declaration there is no HSv4
// extension exchange at the previous release and hence no meaningful forward
// stream to control against.
// ===========================================================================

// Runs the whole gate experiment over the supplied schedule. Both cases below
// differ only in the channel.
static void live_gate_both_directions(const RelaySched& sc, const char* channel) {
    Link L;
    st_connect(L, sc, live_conf_peer, live_conf_sender);
    live_require_connected("the caller, after connect", L.cal);
    live_require_connected("the accepted socket, after connect", L.acc);

    // Claim 2 -- and the positive control for everything after it.
    LiveXfer fwd;
    live_exchange(L.cal, L.acc, E2E_N, LIVE_PAYLOAD, E2E_GAP_US, E2E_RECV_MS, &fwd);
    {
        char what[160];
        std::snprintf(what, sizeof what, "forward stream over %s, caller -> accepted socket",
                      channel);
        live_require_complete_stream(what, fwd);
    }
    ST_REQUIRE_MSG(st_first_out_of_order(fwd.sent, fwd.got) == -1,
                   "forward stream over %s: first message out of order at position %d", channel,
                   st_first_out_of_order(fwd.sent, fwd.got));

    // The relay really carried it: "arrives with no gaps" must not be
    // satisfiable by a channel that never dropped because it never ran.
    live_require_relay_did_it(*L.relay, 0, E2E_N);

    SRT_TRACEBSTATS rb = live_stats(L.acc);
    ST_REQUIRE_MSG(rb.pktRecvTotal >= (int64_t)E2E_N,
                   "forward stream over %s: the receiver reports pktRecvTotal=%lld for %d "
                   "messages",
                   channel, (long long)rb.pktRecvTotal, E2E_N);
    ST_REQUIRE_MSG(rb.pktRcvDropTotal == 0,
                   "forward stream over %s: too-late drop is off and the latency is 900 ms, but "
                   "the receiver dropped %d packets as late",
                   channel, rb.pktRcvDropTotal);

    // Claim 3. Nothing is reconfigured between the two streams: no second
    // connect, no second latency, no second passphrase -- the direction is
    // simply reversed on the sockets that are already up.
    LiveXfer rev;
    live_exchange(L.acc, L.cal, E2E_N, LIVE_PAYLOAD, E2E_GAP_US, E2E_RECV_MS, &rev);
    {
        char what[200];
        std::snprintf(what, sizeof what,
                      "reverse stream over %s on the same connection, accepted socket -> the "
                      "caller that declared itself the sender",
                      channel);
        live_require_complete_stream(what, rev);
    }
    ST_REQUIRE_MSG(st_first_out_of_order(rev.sent, rev.got) == -1,
                   "reverse stream over %s: first message out of order at position %d", channel,
                   st_first_out_of_order(rev.sent, rev.got));

    // The caller received on a socket that had declared itself the sender, and
    // its own counters have to show it: a build that satisfied the comparison
    // above from some cache rather than from the wire would not move these.
    SRT_TRACEBSTATS cb = live_stats(L.cal);
    ST_REQUIRE_MSG(cb.pktRecvTotal >= (int64_t)E2E_N,
                   "reverse stream over %s: the caller reports pktRecvTotal=%lld for %d "
                   "messages received on the socket that set SRTO_SENDER",
                   channel, (long long)cb.pktRecvTotal, E2E_N);
    ST_REQUIRE_MSG(cb.pktSentTotal >= (int64_t)E2E_N,
                   "the caller reports pktSentTotal=%lld after sending %d messages",
                   (long long)cb.pktSentTotal, E2E_N);

    // Claim 4. "leaving both sockets connected" -- the previous release breaks
    // both of them at the first reverse data packet, so this is asserted on
    // each end separately and by name.
    live_require_connected("the caller, after the reverse stream", L.cal);
    live_require_connected("the accepted socket, after the reverse stream", L.acc);
}

SRT_CASE(g00_endtoend, four_thousand_each_way_on_one_connection) {
    RelaySched sc;
    live_gate_both_directions(sc, "a clean relay");
}

// The same gate over a link with a real one-way delay in both directions. An
// end-to-end claim that only holds at zero RTT is not an end-to-end claim, and
// 15 ms each way is well inside the 900 ms latency both sides configured, so
// the required verdict is unchanged.
SRT_CASE(g00_endtoend, four_thousand_each_way_across_a_delayed_link) {
    RelaySched sc;
    sc.delay_fwd_ms = 15;
    sc.delay_bwd_ms = 15;
    live_gate_both_directions(sc, "a relay delaying 15 ms each way");
}

// ===========================================================================
// g10_recovery -- ARQ with too-late drop disabled (tier: inherited)
//
// scored.txt: "datagrams dropped at fixed indexes are retransmitted and the
// payload is delivered complete and in order when too-late drop is disabled,
// with the sender's retransmission counter and the receiver's loss counter
// both showing that the loss really happened".
//
// Three things are asserted for every schedule, and all three are needed:
//
//   relay->dropped_fwd() == the schedule   the channel really removed them
//   the payload arrives complete, in order  the transport really recovered them
//   pktRcvLossTotal and pktRetransTotal     it was retransmission that did it
//     both above LIVE_SCHEDULED_MIN
//
// Drop the second and a library that delivers nothing passes. Drop the third
// and a library that quietly never lost anything -- because the relay's
// schedule named indexes past the end of the stream, say -- passes. Drop the
// first and the case is measuring the harness.
//
// pktRcvDropTotal == 0 is asserted alongside, because it is the precondition
// the description names: "when too-late drop is disabled". A build that
// recovered the payload by some other route while still skipping packets as
// late is not the build this group describes.
//
// The clean control comes first and uses the identical configuration with an
// empty schedule. Without it, "loss counters above 25" would be a claim about
// a number nobody had shown could ever be small.
// ===========================================================================

// The body every schedule below shares: run the transfer, then make the three
// assertions in the order that fails most informatively.
static void live_recovery_run(const RelaySched& sc, int expect_dropped, const char* what,
                              int timeo_ms) {
    Link L;
    st_connect(L, sc, live_conf_peer, live_conf_sender);

    LiveXfer x;
    live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US, timeo_ms, &x);

    live_require_relay_did_it(*L.relay, expect_dropped, LIVE_N);
    live_require_complete_stream(what, x);
    ST_REQUIRE_MSG(st_first_out_of_order(x.sent, x.got) == -1,
                   "%s: first message out of order at position %d", what,
                   st_first_out_of_order(x.sent, x.got));

    const SRT_TRACEBSTATS rb = live_stats(L.acc);
    const SRT_TRACEBSTATS sb = live_stats(L.cal);

    ST_REQUIRE_MSG(rb.pktRcvLossTotal >= LIVE_SCHEDULED_MIN,
                   "%s: %d forward DATA packets were removed, but the receiver reports "
                   "pktRcvLossTotal=%d, below the floor of %d -- the loss was never detected",
                   what, expect_dropped, rb.pktRcvLossTotal, LIVE_SCHEDULED_MIN);
    ST_REQUIRE_MSG(sb.pktRetransTotal >= LIVE_SCHEDULED_MIN,
                   "%s: the payload arrived complete, but the sender reports pktRetransTotal=%d, "
                   "below the floor of %d -- retransmission is not what recovered it",
                   what, sb.pktRetransTotal, LIVE_SCHEDULED_MIN);
    ST_REQUIRE_MSG(rb.pktRcvDropTotal == 0,
                   "%s: too-late drop was disabled and the latency is 900 ms, but the receiver "
                   "reports pktRcvDropTotal=%d packets skipped as too late to play",
                   what, rb.pktRcvDropTotal);
}

// The control. Same configuration, empty schedule: everything arrives, in order,
// and nothing is skipped as too late to play.
//
// It used to carry the other half of that argument -- ceilings on the observed
// loss counters, which made the floors in the cases below an observation rather
// than a restatement of an allowance. Those ceilings were deleted (NOTES.md,
// "Gate cases deleted for non-determinism") because this group is GATE: it
// vetoes and earns nothing, so it needs to be unflakeable rather than
// discriminating, and a ceiling on incidental loss is a statement about the
// host's load. The discrimination the floors needed now rests on the scored
// recovery group at step 01, which keeps its ceilings.
SRT_CASE(g10_recovery, a_clean_channel_needs_no_retransmission) {
    Link L;
    RelaySched sc;
    st_connect(L, sc, live_conf_peer, live_conf_sender);

    LiveXfer x;
    live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US, LIVE_CLEAN_MS, &x);

    live_require_relay_did_it(*L.relay, 0, LIVE_N);
    live_require_complete_stream("a clean channel", x);

    const SRT_TRACEBSTATS rb = live_stats(L.acc);
    // The sender's statistics were read here too, for the pktRetransTotal
    // ceiling; the read went with the ceiling, because -Werror
    // =unused-but-set-variable makes a dead SRT_TRACEBSTATS a build failure
    // for the whole family -- both GATE groups included.
    //
    // Two ceilings on observed counters -- pktRcvLossTotal and pktRetransTotal
    // <= LIVE_INCIDENTAL_MAX -- stood here and were DELETED as gate cases that
    // cannot be proved deterministic: on a box shared with nine other build
    // sessions, incidental loss on loopback is a fact about the host's load at
    // that instant, not about the library. This group is GATE, so a flake in it
    // is a zero for the whole step. What the case still asserts -- the complete,
    // in-order stream and a relay that removed nothing -- is the part that
    // describes the library. See NOTES.md, "Gate cases deleted for
    // non-determinism".
    ST_REQUIRE_MSG(rb.pktRcvDropTotal == 0,
                   "a clean channel with too-late drop disabled still reports pktRcvDropTotal=%d",
                   rb.pktRcvDropTotal);
}

// Thirty datagrams removed at fixed indexes, in three clusters of ten. Ten in
// a row is far more than the handful of control packets that could be in
// flight at any index, so the clusters are certainly payload. Thirty is the
// smallest schedule in the group and LIVE_SCHEDULED_MIN is set against it.
SRT_CASE(g10_recovery, three_clusters_of_ten_dropped_datagrams_are_retransmitted) {
    RelaySched sc;
    for (int i = 40; i < 50; i++) sc.drop_fwd.insert(i);
    for (int i = 80; i < 90; i++) sc.drop_fwd.insert(i);
    for (int i = 120; i < 130; i++) sc.drop_fwd.insert(i);
    live_recovery_run(sc, 30, "three clusters of ten dropped datagrams", LIVE_LOSSY_MS);
}

// A burst: forty consecutive datagrams gone, so one NAK covers the whole
// range. This exercises range loss reporting rather than forty separate
// single-packet events.
SRT_CASE(g10_recovery, a_burst_of_forty_consecutive_datagrams_is_recovered) {
    RelaySched sc;
    for (int i = 100; i < 140; i++) sc.drop_fwd.insert(i);
    live_recovery_run(sc, 40, "a burst of 40 consecutive dropped datagrams", LIVE_LOSSY_MS);
}

// Loss spread over the whole transfer instead of clustered: every sixth
// forward data packet. Recovery has to keep up for the length of the stream,
// not merely recover from three isolated events.
//
// The schedule stops short of the tail on purpose. SRT reveals a loss by the
// gap it leaves -- a NAK goes out when a packet with a HIGHER sequence number
// arrives -- so a dropped packet with no successor is never reported and never
// retransmitted. A live stream does not end; this finite one does, and
// dropping its last datagram would be asking for a recovery the protocol
// cannot perform. The relay's index space counts retransmitted data packets
// too, so index 222 falls well before message 250.
SRT_CASE(g10_recovery, periodic_loss_is_recovered_for_the_whole_stream) {
    RelaySched sc;
    int scheduled = 0;
    for (int i = 6; i <= 222; i += 6) {
        sc.drop_fwd.insert(i);
        scheduled++;
    }
    live_recovery_run(sc, scheduled, "every sixth datagram dropped", LIVE_LOSSY_MS);
}

// ===========================================================================
// g12_encryption -- AES payload encryption (tier: inherited)
//
// scored.txt: "with a matching passphrase and an explicitly configured key
// length the payload is carried intact at both key lengths and does not appear
// in clear in the datagrams on the wire, while two different passphrases
// deliver nothing to the application, and the receiver's keying state reaches
// the secured value in the matching case".
//
// Three properties of this milestone's encryption shape every case here, and
// all three are recorded in stepdefs/02/interface.md:
//
//   1. A passphrase alone encrypts NOTHING. SRTO_PBKEYLEN still defaults to 0
//      and must be set explicitly, to 16 or to 32, on the sending side. A case
//      that set only the passphrase would be asserting that plaintext is
//      plaintext.
//   2. The key exchange is driven by TRAFFIC, not by the handshake. So each
//      case drives it with tagged warm-up messages and waits on the OBSERVED
//      keying state -- never on a timer -- and the measured receive skips
//      anything carrying the warm-up tag.
//   3. A mismatch is not a rejection at this milestone. The connection comes
//      up, the keying state says BADSECRET and the payload never decrypts.
//      Rejection arrives with SRTO_STRICTENC at v1.3.2 and is stepdefs/03's
//      subject, so nothing here asserts it.
//
// The keying state is read through SRTO_RCVKMSTATE on the RECEIVING socket and
// nowhere else. The sender's view of its peer is SRTO_SNDPEERKMSTATE at
// v1.2.0 and SRTO_SNDKMSTATE at v1.3.0 -- the option moved, so neither
// spelling compiles at both tags, and this group is inherited. scored.txt asks
// for "the receiver's keying state", which is exactly what SRTO_RCVKMSTATE is.
//
// Every "nothing was delivered" assertion is paired with pktRcvUndecrypt on
// the receiver. Without that pairing, a dead channel and a correctly refused
// one are the same observation.
// ===========================================================================

// Warm-up bodies are tagged so the measured receive can tell them apart from
// the stream it is counting.
static std::string km_warm_payload(int index) {
    std::string s((size_t)KM_MSG, '\0');
    std::snprintf(&s[0], (size_t)KM_MSG, "%s%06d", KM_WARMTAG, index);
    for (int i = 16; i < KM_MSG; i++) s[(size_t)i] = (char)('a' + ((index + i) % 26));
    return s;
}

// A body that is nothing but the marker, so a datagram carrying it in clear
// contains the marker verbatim and one carrying it encrypted cannot.
static std::string km_marked_payload(int index) {
    std::string s((size_t)KM_MSG, '\0');
    std::snprintf(&s[0], (size_t)KM_MSG, "MARK%06d", index);
    const size_t nlen = std::strlen(KM_NEEDLE);
    for (size_t pos = 16; pos < (size_t)KM_MSG; pos += nlen)
        std::memcpy(&s[pos], KM_NEEDLE, std::min(nlen, (size_t)KM_MSG - pos));
    return s;
}

// The receiving end: live-lossless defaults plus an optional passphrase.
static SockConf km_rx_conf(const char* pass) {
    std::string p = pass ? pass : "";
    return [p](SRTSOCKET s) {
        st_live_lossless(s);
        if (!p.empty()) st_set_str(s, SRTO_PASSPHRASE, p);
    };
}

// The sending end. SRTO_SENDER before connect, or at HSv4 this side never
// sends KMREQ and no key is ever exchanged; SRTO_PBKEYLEN non-zero, or the
// passphrase encrypts nothing.
static SockConf km_tx_conf(const char* pass, int keylen) {
    std::string p = pass ? pass : "";
    return [p, keylen](SRTSOCKET s) {
        st_set_int(s, SRTO_SENDER, 1);
        st_live_lossless(s);
        if (keylen != 0) st_set_int(s, SRTO_PBKEYLEN, keylen);
        if (!p.empty()) st_set_str(s, SRTO_PASSPHRASE, p);
    };
}

// Receives `want` measured messages, skipping whatever the warm-up left in the
// buffer. Bounded by SRTO_RCVTIMEO: a channel that delivers nothing costs
// `timeo_ms`, not the case budget. The buffer is KM_MSG + 4096 and not
// KM_MSG + something small, because from v1.3.0 a live receive into a buffer
// below the declared payload size is refused outright (deltas/02.md D9) --
// with a 512-byte buffer every case below would observe an empty channel and
// the mismatch cases would pass for the wrong reason.
static std::vector<std::string> km_recv_measured(SRTSOCKET s, int want, int timeo_ms) {
    st_set_int(s, SRTO_RCVTIMEO, timeo_ms);
    std::vector<std::string> got;
    std::vector<char> buf((size_t)KM_MSG + KM_RECV_SLACK);
    while ((int)got.size() < want) {
        int rc = srt_recvmsg(s, buf.data(), (int)buf.size());
        if (rc <= 0) break;
        std::string m(buf.data(), (size_t)rc);
        if (m.compare(0, std::strlen(KM_WARMTAG), KM_WARMTAG) == 0) continue;
        got.push_back(m);
    }
    return got;
}

// Drives the key exchange with tagged traffic and returns the RECEIVER's
// keying state once it has left the transient values. The exit condition is
// the state itself; the pacing gap only keeps live mode's rate limiter happy.
static int km_warm(SRTSOCKET snd, SRTSOCKET rcv) {
    for (int i = 0; i < KM_WARM_MAX; i++) {
        const int v = st_get_int(rcv, SRTO_RCVKMSTATE);
        if (v == SRT_KM_S_SECURED || v == SRT_KM_S_NOSECRET || v == SRT_KM_S_BADSECRET) return v;
        const std::string m = km_warm_payload(i);
        const int rc = srt_sendmsg(snd, m.data(), (int)m.size(), -1, 1);
        ST_REQUIRE_MSG(rc == (int)m.size(), "warm-up srt_sendmsg #%d returned %d: %s", i, rc,
                       srt_getlasterror_str());
        std::this_thread::sleep_for(std::chrono::microseconds(KM_GAP_US));
    }
    return st_get_int(rcv, SRTO_RCVKMSTATE);
}

static void km_warm_to(SRTSOCKET snd, SRTSOCKET rcv, int want) {
    const int got = km_warm(snd, rcv);
    ST_REQUIRE_MSG(got == want,
                   "after at most %d warm-up messages the receiver reports SRTO_RCVKMSTATE = %d "
                   "(%s), expected %d (%s)",
                   KM_WARM_MAX, got, live_kmname(got), want, live_kmname(want));
}

static int km_undecrypt(SRTSOCKET s, bool clear_now) {
    SRT_TRACEBSTATS st;
    std::memset(&st, 0, sizeof st);
    ST_REQUIRE_MSG(srt_bstats(s, &st, clear_now ? 1 : 0) != SRT_ERROR, "srt_bstats: %s",
                   srt_getlasterror_str());
    return st.pktRcvUndecrypt;
}

static void km_require_same_stream(const char* what, const std::vector<std::string>& sent,
                                   const std::vector<std::string>& got) {
    ST_REQUIRE_MSG(got.size() == sent.size(), "%s: %d of %d messages arrived", what,
                   (int)got.size(), (int)sent.size());
    const int bad = st_first_out_of_order(sent, got);
    ST_REQUIRE_MSG(bad < 0, "%s: the message at position %d was substituted or arrived out of "
                            "order",
                   what, bad);
    ST_REQUIRE_MSG(got == sent, "%s: the delivered stream is not byte-identical to the sent one",
                   what);
}

// The body the two "carried intact" cases share: differ only in key length.
static void km_intact_at_keylen(int keylen) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, km_rx_conf(KM_PW_A), km_tx_conf(KM_PW_A, keylen));
    km_warm_to(L.cal, L.acc, SRT_KM_S_SECURED);

    const int want = 24;
    (void)km_undecrypt(L.acc, true); // forget whatever the warm-up could not decrypt yet

    std::vector<std::string> sent;
    sent.reserve((size_t)want);
    for (int i = 0; i < want; i++) {
        const std::string m = st_payload(i, KM_MSG);
        const int rc = srt_sendmsg(L.cal, m.data(), (int)m.size(), -1, 1);
        ST_REQUIRE_MSG(rc == (int)m.size(), "srt_sendmsg #%d under a %d-bit key returned %d: %s",
                       i, keylen * 8, rc, srt_getlasterror_str());
        sent.push_back(m);
        std::this_thread::sleep_for(std::chrono::microseconds(KM_GAP_US));
    }
    const std::vector<std::string> got = km_recv_measured(L.acc, want, KM_FULL_MS);

    char what[96];
    std::snprintf(what, sizeof what, "a matching passphrase with a %d-bit key", keylen * 8);
    km_require_same_stream(what, sent, got);

    const int undec = km_undecrypt(L.acc, false);
    ST_REQUIRE_MSG(undec == 0,
                   "%s: the receiver counted %d undecryptable packets after the key exchange "
                   "completed, expected 0",
                   what, undec);

    // The state is re-read AFTER the payload crossed, so it is the state that
    // actually carried it and not a transient the warm-up happened to observe.
    const int st_after = st_get_int(L.acc, SRTO_RCVKMSTATE);
    ST_REQUIRE_MSG(st_after == SRT_KM_S_SECURED,
                   "%s: the receiver reports SRTO_RCVKMSTATE = %d (%s) after carrying the "
                   "payload, expected %d (%s)",
                   what, st_after, live_kmname(st_after), (int)SRT_KM_S_SECURED,
                   live_kmname(SRT_KM_S_SECURED));

    ST_REQUIRE_MSG(L.relay->seen_fwd() >= want,
                   "%s: the relay forwarded only %d data packets for %d messages", what,
                   L.relay->seen_fwd(), want);
}

SRT_CASE(g12_encryption, a_matching_passphrase_with_a_128_bit_key_carries_the_payload_intact) {
    km_intact_at_keylen(16);
}

SRT_CASE(g12_encryption, a_matching_passphrase_with_a_256_bit_key_carries_the_payload_intact) {
    km_intact_at_keylen(32);
}

// ---------------------------------------------------------------------------
// A UDP relay that keeps a copy of what it forwards.
//
// relay.h's Relay exposes counters only, and this file may not modify it --
// other families depend on that header -- so the on-the-wire half of this
// group owns its own socket pair. It forwards everything, with no schedule and
// no loss, and records every datagram travelling from the caller (the sending
// side) towards the listener, which is the direction the payload takes.
// ---------------------------------------------------------------------------

class WireTap {
public:
    explicit WireTap(uint16_t backend_port) {
        front_ = udp_socket_bound(0, &front_port_);
        back_ = udp_socket_bound(0, nullptr);
        std::memset(&backend_, 0, sizeof backend_);
        backend_.sin_family = AF_INET;
        backend_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        backend_.sin_port = htons(backend_port);
        run_ = true;
        th_ = std::thread([this] { loop(); });
    }
    ~WireTap() {
        run_ = false;
        if (th_.joinable()) th_.join();
        ::close(front_);
        ::close(back_);
    }
    WireTap(const WireTap&) = delete;
    WireTap& operator=(const WireTap&) = delete;

    uint16_t port() const { return front_port_; }

    std::vector<std::string> forwarded() const {
        std::lock_guard<std::mutex> g(mx_);
        return fwd_;
    }

private:
    void loop() {
        char buf[2048];
        sockaddr_in from;
        socklen_t fl;
        bool have_client = false;
        sockaddr_in client;
        std::memset(&client, 0, sizeof client);
        while (run_) {
            pollfd p[2] = {{front_, POLLIN, 0}, {back_, POLLIN, 0}};
            const int ready = ::poll(p, 2, 20);
            if (ready > 0 && (p[0].revents & POLLIN)) {
                fl = sizeof from;
                const ssize_t n = ::recvfrom(front_, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
                if (n > 0) {
                    client = from;
                    have_client = true;
                    {
                        std::lock_guard<std::mutex> g(mx_);
                        fwd_.push_back(std::string(buf, (size_t)n));
                    }
                    ::sendto(back_, buf, (size_t)n, 0, (const sockaddr*)&backend_,
                             sizeof backend_);
                }
            }
            if (ready > 0 && (p[1].revents & POLLIN)) {
                fl = sizeof from;
                const ssize_t n = ::recvfrom(back_, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
                if (n > 0 && have_client)
                    ::sendto(front_, buf, (size_t)n, 0, (const sockaddr*)&client, sizeof client);
            }
        }
    }

    int front_ = -1, back_ = -1;
    uint16_t front_port_ = 0;
    sockaddr_in backend_;
    std::atomic<bool> run_{false};
    std::thread th_;
    mutable std::mutex mx_;
    std::vector<std::string> fwd_;
};

struct TapLink {
    SRTSOCKET lsn = SRT_INVALID_SOCK;
    SRTSOCKET cal = SRT_INVALID_SOCK;
    SRTSOCKET acc = SRT_INVALID_SOCK;
    WireTap* tap = nullptr;

    TapLink() {}
    TapLink(const TapLink&) = delete;
    TapLink& operator=(const TapLink&) = delete;
    ~TapLink() {
        if (acc != SRT_INVALID_SOCK) srt_close(acc);
        if (cal != SRT_INVALID_SOCK) srt_close(cal);
        if (lsn != SRT_INVALID_SOCK) srt_close(lsn);
        delete tap;
    }
};

// The same shape as st_try_connect, with the recording tap in place of the
// relay. Every assertion is placed either before the acceptor thread starts or
// after it is joined, so a failure never throws past a live thread.
static void tap_connect(TapLink& L, const SockConf& lconf, const SockConf& cconf) {
    L.lsn = st_new_socket();
    ST_REQUIRE_MSG(L.lsn != SRT_INVALID_SOCK, "srt_socket (listener): %s", srt_getlasterror_str());
    // The accept is bounded by st_accept_polled, not by a timeout: a blocking
    // srt_accept does not consult SRTO_RCVTIMEO at any prefix in this ladder.
    lconf(L.lsn);

    sockaddr_in la = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(L.lsn, (sockaddr*)&la, sizeof la) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    int nl = (int)sizeof la;
    ST_REQUIRE_MSG(srt_getsockname(L.lsn, (sockaddr*)&la, &nl) != SRT_ERROR, "srt_getsockname: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(L.lsn, 1) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());

    L.tap = new WireTap(ntohs(la.sin_port));

    L.cal = st_new_socket();
    ST_REQUIRE_MSG(L.cal != SRT_INVALID_SOCK, "srt_socket (caller): %s", srt_getlasterror_str());
    cconf(L.cal);

    sockaddr_in ra = st_loopback(L.tap->port());

    SRTSOCKET accepted = SRT_INVALID_SOCK;
    std::atomic<bool> connect_done(false);
    std::thread acceptor(
        [&] { accepted = st_accept_polled(L.lsn, connect_done); });

    const int rc = srt_connect(L.cal, (sockaddr*)&ra, sizeof ra);
    const int err = srt_getlasterror(nullptr);
    connect_done.store(true);
    acceptor.join();
    L.acc = accepted;

    ST_REQUIRE_MSG(rc != SRT_ERROR, "srt_connect through the tap: %s", srt_strerror(err, 0));
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK, "srt_accept through the tap: %s",
                   srt_getlasterror_str());
}

struct WireResult {
    int delivered = 0;
    int datagrams = 0;
    int marker_hits = 0;
};

// One half of the on-the-wire experiment: carry `count` marker-filled messages
// across a tapped channel, encrypted or not, and report how many forwarded
// datagrams carried the marker verbatim.
static WireResult wire_run(bool encrypted, int count) {
    TapLink L;
    const char* pass = encrypted ? KM_PW_A : nullptr;
    tap_connect(L, km_rx_conf(pass), km_tx_conf(pass, encrypted ? 16 : 0));
    if (encrypted) km_warm_to(L.cal, L.acc, SRT_KM_S_SECURED);

    std::vector<std::string> sent;
    sent.reserve((size_t)count);
    for (int i = 0; i < count; i++) {
        const std::string m = km_marked_payload(i);
        const int rc = srt_sendmsg(L.cal, m.data(), (int)m.size(), -1, 1);
        ST_REQUIRE_MSG(rc == (int)m.size(), "srt_sendmsg #%d through the tap returned %d: %s", i,
                       rc, srt_getlasterror_str());
        sent.push_back(m);
        std::this_thread::sleep_for(std::chrono::microseconds(KM_GAP_US));
    }
    const std::vector<std::string> got = km_recv_measured(L.acc, count, KM_FULL_MS);
    km_require_same_stream(encrypted ? "the encrypted half of the wire check"
                                     : "the clear half of the wire check",
                           sent, got);

    WireResult r;
    r.delivered = (int)got.size();
    const std::vector<std::string> seen = L.tap->forwarded();
    r.datagrams = (int)seen.size();
    for (size_t i = 0; i < seen.size(); i++)
        if (seen[i].find(KM_NEEDLE) != std::string::npos) r.marker_hits++;
    return r;
}

// The one case that must not be vacuous in either direction: it runs the very
// same marker through the very same tap twice, once in clear and once
// encrypted, and requires the marker to be visible in exactly one of the two.
// Without the clear half, a library that sent nothing at all would pass; with
// it, the check is shown to be able to see plaintext before it is asked to
// report the absence of plaintext.
SRT_CASE(g12_encryption, the_payload_is_on_the_wire_in_clear_only_without_a_passphrase) {
    const int count = 16;

    const WireResult clear = wire_run(false, count);
    ST_REQUIRE_MSG(clear.datagrams >= count,
                   "the tap forwarded only %d datagrams for %d unencrypted messages",
                   clear.datagrams, count);
    ST_REQUIRE_MSG(clear.marker_hits >= count,
                   "with no passphrase only %d of %d forwarded datagrams carried the marker in "
                   "clear, expected at least %d: the check cannot detect plaintext and so proves "
                   "nothing about the encrypted half",
                   clear.marker_hits, clear.datagrams, count);

    const WireResult enc = wire_run(true, count);
    ST_REQUIRE_MSG(enc.delivered == count,
                   "the encrypted half delivered %d of %d messages", enc.delivered, count);
    ST_REQUIRE_MSG(enc.datagrams >= count,
                   "the tap forwarded only %d datagrams for %d encrypted messages", enc.datagrams,
                   count);
    ST_REQUIRE_MSG(enc.marker_hits == 0,
                   "%d of %d forwarded datagrams carried the payload marker in clear even though "
                   "both sides held the same passphrase and the key length was set to 16, "
                   "expected 0",
                   enc.marker_hits, enc.datagrams);
}

// Two different passphrases. The connection completes -- this milestone does
// not refuse it, and nothing here asserts that it does -- the receiver's
// keying state reaches BADSECRET, and the application is handed nothing.
//
// "Nothing" is only meaningful next to the proof that the datagrams did arrive
// and were refused: pktRcvUndecrypt on the receiver must account for at least
// every message sent. Otherwise a dead channel and a correctly refused one are
// the same observation.
SRT_CASE(g12_encryption, two_different_passphrases_deliver_nothing) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, km_rx_conf(KM_PW_A), km_tx_conf(KM_PW_B, 16));

    live_require_connected("the caller under two different passphrases", L.cal);
    live_require_connected("the accepted socket under two different passphrases", L.acc);

    km_warm_to(L.cal, L.acc, SRT_KM_S_BADSECRET);

    const int want = 16;
    (void)km_undecrypt(L.acc, true); // forget the warm-up's undecryptable packets

    std::vector<std::string> sent;
    sent.reserve((size_t)want);
    for (int i = 0; i < want; i++) {
        const std::string m = st_payload(i, KM_MSG);
        const int rc = srt_sendmsg(L.cal, m.data(), (int)m.size(), -1, 1);
        ST_REQUIRE_MSG(rc == (int)m.size(), "srt_sendmsg #%d returned %d: %s", i, rc,
                       srt_getlasterror_str());
        sent.push_back(m);
        std::this_thread::sleep_for(std::chrono::microseconds(KM_GAP_US));
    }
    const std::vector<std::string> got = km_recv_measured(L.acc, want, KM_EMPTY_MS);
    ST_REQUIRE_MSG(got.empty(),
                   "%d of %d messages reached an application whose socket holds a different "
                   "passphrase from the sender's, expected 0",
                   (int)got.size(), want);

    const int undec = km_undecrypt(L.acc, false);
    ST_REQUIRE_MSG(undec >= want,
                   "the receiver counted %d undecryptable packets, expected at least the %d "
                   "messages that were sent: the datagrams have to have arrived and been "
                   "refused, not gone missing",
                   undec, want);

    const int st_after = st_get_int(L.acc, SRTO_RCVKMSTATE);
    ST_REQUIRE_MSG(st_after == SRT_KM_S_BADSECRET,
                   "the receiver reports SRTO_RCVKMSTATE = %d (%s) after refusing the payload, "
                   "expected %d (%s)",
                   st_after, live_kmname(st_after), (int)SRT_KM_S_BADSECRET,
                   live_kmname(SRT_KM_S_BADSECRET));

    ST_REQUIRE_MSG(L.relay->seen_fwd() >= want,
                   "the relay forwarded only %d data packets for %d messages",
                   L.relay->seen_fwd(), want);
}
