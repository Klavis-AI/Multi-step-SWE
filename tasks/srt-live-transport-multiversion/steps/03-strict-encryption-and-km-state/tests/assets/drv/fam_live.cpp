// Family "live", checkpoint 03 (SRT v1.3.2). Two groups:
//
//   g00_endtoend   new        SCORED, this step's end-to-end path: an encrypted live stream with
//                             strict enforcement enabled crosses a clean relay
//                             and arrives byte-identical and in order, and a
//                             passphrase mismatch is refused at connect time.
//   g08_recovery   inherited  datagrams dropped at fixed indexes are
//                             retransmitted and the payload is delivered intact
//                             when too-late drop is disabled.
//
// ------------------------------------------------------------------- tiers
//
// This family MIXES TIERS, and that is the fact that shapes the top of the
// file. g00_endtoend is `new`: it must fail against the previous release,
// because strict encryption enforcement is what this milestone adds.
// g08_recovery is `inherited`: it exists to be earned by a candidate who
// shipped v1.3.0 and did nothing at all for this milestone, so it must PASS
// against the previous release. One family is one compiled `.cpp`, so both
// verdicts have to come out of a single binary that BUILDS at both prefixes.
//
// DRIVER_RULES, "Naming an identifier the candidate may not have", is therefore
// binding here and is answered the same way fam_kmx.cpp in this directory
// answers it. g00_endtoend needs SRTO_STRICTENC -- the gate group in this
// file does not -- and it does not exist at v1.3.0.
// Naming it unguardedly would stop this file compiling at the previous prefix,
// and g08_recovery would then score zero for a COMPILE reason having nothing to
// do with the candidate -- the exact defect that cost this package a step's
// worth of groups once already. So the constant is restated below, after
// <srt/srt.h> has been pulled in, guarded by #ifndef, and pinned with a
// static_assert under a version guard so that the number is re-derived from the
// header wherever the header has it. Nothing else in this file needs
// restating: every other identifier it names -- SRTO_TSBPDMODE, SRTO_LATENCY,
// SRTO_TLPKTDROP, SRTO_RCVTIMEO, SRTO_RCVSYN, SRTO_PBKEYLEN, SRTO_PASSPHRASE,
// SRTO_RCVKMSTATE, SRTO_SNDKMSTATE, and the srt_bstats counters pktRecvTotal,
// pktRcvLossTotal, pktRetransTotal, pktRcvDropTotal and pktSndDropTotal --
// exists at v1.3.0 and is unchanged at v1.3.2.
//
// What that buys, concretely: at the previous prefix g00_endtoend fails on an
// assertion this file wrote ("srt_setsockflag(SRTO_STRICTENC, 1) ... was
// refused"), which is what an unimplemented feature is supposed to look like,
// while g08_recovery runs and passes.
//
// ---------------------------------------------------------- traceability
//
// g00_endtoend. endtoend.txt gives the claim; the step's own scope.md and
// interface.md are the source for the behaviour, because docs/API.md and
// docs/encryption.md at v1.3.2 document SRTO_STRICTENC nowhere (deltas/03.md,
// "Documentation gap"). scope.md: the option "defaults to 1 (strict mode
// enabled). When enabled, a connection is refused at handshake time ... if one
// peer sets a passphrase and the other does not, or if both set passphrases but
// they differ. The refusal is reported as SRT_ECONNREJ from srt_connect, and
// the caller socket falls back to SRTS_OPENED rather than SRTS_BROKEN".
// interface.md: "An explicit SRTO_PBKEYLEN fixes the key length; it does not
// switch encryption on" -- which is why g00_endtoend sets the key length
// explicitly and then proves encryption from the KM state and the undecrypt
// counter, rather than from the presence of a passphrase.
//
// g08_recovery. The subject is milestone-01 transport surface that this step
// inherits, and the step says so itself in inherit.md -- "loss recovery with
// too-late drop disabled must still deliver the payload intact when the relay
// drops datagrams at fixed indexes". It is documented behaviour in srt-docs/
// as well:
//
//   * API.md, SRTO_TLPKTDROP: "When true (default), it will drop the packets
//     that haven't been retransmitted on time ... You can turn this off to
//     always ensure a clean delivery." Turning it off is the precondition this
//     group's description names, and "clean delivery" is what it asserts.
//   * API.md, SRTO_TLPKTDROP option row: "When enabled on receiver, it skips
//     missing packets that have not been delivered in time"; the counter it
//     feeds, pktRcvDropTotal, is "number of too-late-to play missing packets"
//     (srt.h). With the option off, that counter must stay at zero.
//   * live-streaming.md on latency: it "should cover both any unexpected
//     transmission delays for a UDP packet, as well as allowing extra time for
//     the case where a packet is lost and has to be retransmitted".
//   * srt.h on the counters read here: pktRcvLossTotal is the "total number of
//     lost packets (receiver side)" and pktRetransTotal the "total number of
//     retransmitted packets". They are what turn "it arrived" into "it arrived
//     BECAUSE it was retransmitted".
//
// --------------------------------------------------------- determinism
//
// Nothing here waits on a clock for control flow. Loss comes from RelaySched at
// fixed indexes, every receiver runs on its own thread bounded by
// SRTO_RCVTIMEO, the one place that waits for a socket to settle waits on the
// OBSERVED state with a deadline, and no case sleeps to drain.
//
// The index space matters, and it is why this file uses relay.h rather than
// counting datagrams itself: relay.h indexes DATA packets only and forwards
// every control packet untouched. How many ACKs, ACKACKs and NAKs cross in a
// given second depends on timing, so an index space that counted them would
// name a different data packet on every run and `drop_fwd = {40..49}` would
// remove an unpredictable number of payload packets. With data-only indexing,
// `L.relay->dropped_fwd()` is exactly how many payload packets the schedule
// removed, and the cases below assert that number exactly.
//
// The loss THRESHOLDS are bands, not equalities, and the two bands do not
// touch. This is a userspace UDP relay on a VM shared with nine other build
// sessions, and the kernel occasionally drops a datagram underneath it; SRT
// recovers those, so the payload assertions are unaffected, but
// "pktRcvLossTotal == 0 on a clean channel" is not a reproducible observation
// on this harness. The two bounds are documented at their definitions below.
//
// ------------------------------------------- deliberately NOT asserted
//
//   * any exact value of pktRcvLossTotal or pktRetransTotal. The number of
//     retransmissions depends on how many NAKs cross and on whether a
//     retransmission is itself caught by the schedule (the relay's index space
//     includes retransmitted data packets), and no document fixes it.
//     DRIVER_RULES rule 5: shape, never a particular number.
//   * how LONG recovery takes. The latency is a budget, not a deadline any
//     document states, so the cases assert that delivery happened inside a
//     generous SRTO_RCVTIMEO and never that it happened by a particular time.
//   * which SIDE rejects a mismatched passphrase, or with what handshake code.
//     The caller sees SRT_ECONNREJ and that is all this group claims; fam_kmx.cpp
//     owns the detail of the rejection and makes the same choice.
//   * the KM state of a REJECTED socket. The handshake never completed, so
//     there is no negotiated state to read and no document says what one
//     should report.
//   * SRTO_NAKREPORT, the periodic loss-report timer. It is on by default in
//     live mode and it is part of why recovery works here, but this step's
//     documents do not require a candidate to have it, so no case names it.
//   * SRTO_SNDDROPDELAY, which at v1.3.2 shifts the sender-side too-late-drop
//     threshold. It does not exist at v1.3.0, so the inherited group cannot
//     name it; the sender-side drop is disabled here the way both releases
//     support, by SRTO_TLPKTDROP=0 on the receiver (the sender consults the
//     peer's handshake flag before discarding anything).
//   * loss in the BACKWARD direction. RelaySched::drop_bwd is not data-indexed
//     -- the backward path of a live stream is almost entirely control -- so a
//     schedule there would drop a timing-dependent set of ACKs and the group
//     would stop being reproducible.
#include "stest.h"
#include "srtkit.h"
#include "relay.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ------------------------------------------------ option constant, restated
//
// DRIVER_RULES rule 2, and deliberately the same block fam_kmx.cpp carries, so
// that this package has one idiom for this problem rather than two.
//
// The value is IMPLICIT in v1.3.2's srt.h (`SRTO_STRICTENC,` with no `= N`),
// the last member after the explicit SRTO_SNDKMSTATE = 40: RCVKMSTATE 41,
// LOSSMAXTTL 42, RCVLATENCY 43, PEERLATENCY 44, MINVERSION 45, STREAMID 46,
// SMOOTHER 47, MESSAGEAPI 48, PAYLOADSIZE 49, TRANSTYPE 50, KMREFRESHRATE 51,
// KMPREANNOUNCE 52, STRICTENC 53. Rule 2 permits restating an implicit value
// only when the enum carries no preprocessor conditionals, because a
// conditional member would give the same name different numbers in different
// build configurations. v1.3.2's SRT_SOCKOPT has no `#if` of any kind -- the
// conditional members arrive with the bonding options later in the chain. At
// v1.3.0 the #define supplies the number that library rejects, which is exactly
// what g00_endtoend means to observe.
//
// The static_assert that re-derives the number from the header lives in
// fam_kmx.cpp and deliberately NOT here. A static_assert is a file-scope check:
// it fails the whole translation unit, and this one also carries the GATE group
// g08_recovery, which never names SRTO_STRICTENC and must not fail to BUILD
// over its number -- a gate group has to be unable to fail for any reason
// except a regression. Nothing is lost by keeping the pin in one place:
// tools/build-step.py collects the TEST_INTERFACE.md section-5 table of pinned
// constants from every family in the step and dedupes by name, so the value
// stays documented, and a candidate who numbers SRTO_STRICTENC differently
// still fails to compile fam_kmx.cpp -- which carries the gate group
// g07_keylen32, so such a candidate is vetoed there rather than here.
// (Deleting the pin from fam_kmx.cpp would take the constant out of the
// rendered table; it belongs there, and only there.)
//
// The #define comes AFTER <srt/srt.h> (pulled in by stest.h). Restating before
// the include would rewrite the enumerator inside the header's own declaration.
#ifndef SRTO_STRICTENC
#define SRTO_STRICTENC ((SRT_SOCKOPT)53)
#endif

// ---------------------------------------------------------------- sizes
//
// The documented live payload size (srt-docs/API.md: "SRTO_PAYLOADSIZE = 1316"
// among the SRTT_LIVE defaults). One message is then exactly one data packet,
// which is what lets a drop schedule expressed in data-packet indexes be read
// as "these messages went missing".
static const int LIVE_PAYLOAD = 1316;

// The end-to-end stream. Every step's g00_endtoend in this chain carries a few
// thousand messages, and the point of the size is that "byte-identical and in
// order" is a claim about a stream rather than about a handful of packets.
static const int E2E_N = 4000;
static const int E2E_GAP_US = 300;
static const int E2E_RECV_MS = 10000;

// Messages per recovery transfer, and the pacing between them. The pacing is
// not decoration: this is a userspace relay on a shared box, and a heavier
// burst makes the kernel itself lose the occasional datagram. At 2 ms the relay
// was measured clean, and 250 messages is long enough for a schedule that drops
// forty consecutive datagrams to still leave a clean tail.
static const int LIVE_N = 250;
static const int LIVE_GAP_US = 2000;

// Receive budgets, in milliseconds. These bound SRTO_RCVTIMEO -- they are exit
// conditions for a stalled channel, never sleeps. A clean transfer of LIVE_N
// messages costs about (LIVE_N * LIVE_GAP_US) + one 900 ms latency, well under
// two seconds, so both budgets are several times the expected cost.
static const int LIVE_CLEAN_MS = 8000;
static const int LIVE_LOSSY_MS = 10000;

// The loss floor. LIVE_SCHEDULED_MIN is what a case with a drop schedule must
// clear: the smallest schedule below removes thirty datagrams and the receiver
// was measured reporting all thirty (30/30, 40/41 and 36/38 for the three lossy
// schedules, identically at v1.3.0 and v1.3.2), so the floor sits under every
// measurement while a clean channel -- measured at pktRcvLossTotal = 0 and
// pktRetransTotal = 0 at both prefixes -- stays far below it. A floor is safe
// in a gate group in a way its mirror image is not: a busy host can only ADD
// incidental loss, so it can never push a lossy run under the floor, while a
// CEILING on a clean run is a statement about the host. The ceiling
// LIVE_INCIDENTAL_MAX = 20 that used to stand here was deleted with the two
// assertions that read it; see NOTES.md, "Gate cases deleted for
// non-determinism".
//
// Every schedule here runs on an undelayed relay, which matters for the floor:
// a NAK and its retransmission complete in well under a millisecond on
// loopback, so the loss accounting has long settled before the transfer ends.
// The floor is not safe across a delayed link, where the detection of the last
// loss cluster can still be in flight when the stats are read; the case that
// combined the two was deleted (NOTES.md, "Gate cases deleted").
static const int LIVE_SCHEDULED_MIN = 25;

// Passphrases for g00_endtoend. Two of them, because one of its two claims is
// about what happens when they disagree.
static const char E2E_PW_A[] = "live-step03-gate-passphrase-alpha";
static const char E2E_PW_B[] = "live-step03-gate-passphrase-bravo";

// --------------------------------------------------------------- naming
//
// These helpers are local rather than added to drv/srtkit.h: that header has to
// compile at every tag in the chain and must not grow API surface for one
// family. They exist so a failure line names an error symbolically instead of
// printing 5002.

static const char* live_errname(int e) {
    switch (e) {
    case SRT_SUCCESS:      return "SRT_SUCCESS";
    case SRT_ECONNREJ:     return "SRT_ECONNREJ";
    case SRT_ECONNLOST:    return "SRT_ECONNLOST";
    case SRT_ENOCONN:      return "SRT_ENOCONN";
    case SRT_ECONNSETUP:   return "SRT_ECONNSETUP";
    case SRT_ENOSERVER:    return "SRT_ENOSERVER";
    case SRT_EINVOP:       return "SRT_EINVOP";
    case SRT_EBOUNDSOCK:   return "SRT_EBOUNDSOCK";
    case SRT_ECONNSOCK:    return "SRT_ECONNSOCK";
    case SRT_EINVPARAM:    return "SRT_EINVPARAM";
    case SRT_EINVSOCK:     return "SRT_EINVSOCK";
    case SRT_EUNBOUNDSOCK: return "SRT_EUNBOUNDSOCK";
    case SRT_EASYNCRCV:    return "SRT_EASYNCRCV";
    default:               return "<other SRT_ERRNO>";
    }
}

static const char* live_statename(SRT_SOCKSTATUS s) {
    switch (s) {
    case SRTS_INIT:       return "SRTS_INIT";
    case SRTS_OPENED:     return "SRTS_OPENED";
    case SRTS_LISTENING:  return "SRTS_LISTENING";
    case SRTS_CONNECTING: return "SRTS_CONNECTING";
    case SRTS_CONNECTED:  return "SRTS_CONNECTED";
    case SRTS_BROKEN:     return "SRTS_BROKEN";
    case SRTS_CLOSING:    return "SRTS_CLOSING";
    case SRTS_CLOSED:     return "SRTS_CLOSED";
    case SRTS_NONEXIST:   return "SRTS_NONEXIST";
    }
    return "<unknown SRT_SOCKSTATUS>";
}

static const char* live_kmname(int v) {
    switch (v) {
    case SRT_KM_S_UNSECURED: return "SRT_KM_S_UNSECURED";
    case SRT_KM_S_SECURING:  return "SRT_KM_S_SECURING";
    case SRT_KM_S_SECURED:   return "SRT_KM_S_SECURED";
    case SRT_KM_S_NOSECRET:  return "SRT_KM_S_NOSECRET";
    case SRT_KM_S_BADSECRET: return "SRT_KM_S_BADSECRET";
    default:                 return "<other SRT_KM_STATE>";
    }
}

// --------------------------------------------------------------- transfer

// st_payload() stamps "MSG%08d" into the first bytes of every message, so a
// received message names the index it was sent as. That is what turns "250
// messages arrived" into "messages 0..249 arrived, in order, with no gaps and
// nothing substituted". Returns -1 for a message that carries no readable
// index, which is itself a finding worth reporting.
static int live_msg_index(const std::string& m) {
    if (m.size() < 12 || m.compare(0, 3, "MSG") != 0) return -1;
    char digits[9];
    std::memcpy(digits, m.data() + 3, 8);
    digits[8] = '\0';
    char* end = NULL;
    long v = std::strtol(digits, &end, 10);
    if (end != digits + 8) return -1;
    return (int)v;
}

static int64_t live_now_ms() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static SRT_TRACEBSTATS live_stats(SRTSOCKET s, const char* which) {
    SRT_TRACEBSTATS b;
    std::memset(&b, 0, sizeof b);
    ST_REQUIRE_MSG(srt_bstats(s, &b, 0) == 0,
                   "srt_bstats on the %s socket failed: %s; without the counters this case "
                   "cannot tell recovery from an empty channel", which, srt_getlasterror_str());
    return b;
}

// Thread body. Deliberately non-throwing: an STFail escaping a std::thread
// would call std::terminate and lose the failure message the reviewer needs.
// Every exit is either "collected what I was asked for" or "SRTO_RCVTIMEO
// expired", and the caller on the main thread is what asserts about it. The
// timeout is set by the caller, before this starts.
static void live_rx_collect(SRTSOCKET s, int want, int bufsize, std::vector<std::string>* out) {
    std::string buf((size_t)bufsize + 4096, '\0');
    while ((int)out->size() < want) {
        int rc = srt_recvmsg(s, &buf[0], (int)buf.size());
        if (rc <= 0) break;
        out->push_back(buf.substr(0, (size_t)rc));
    }
}

// Joins on destruction, so an assertion failure on the main thread unwinds
// cleanly instead of hitting std::terminate. Bounded, because the loop it joins
// is bounded by SRTO_RCVTIMEO.
struct LiveJoiner {
    std::thread t;
    explicit LiveJoiner(std::thread&& th) : t(std::move(th)) {}
    LiveJoiner(const LiveJoiner&) = delete;
    LiveJoiner& operator=(const LiveJoiner&) = delete;
    ~LiveJoiner() {
        if (t.joinable()) t.join();
    }
};

struct LiveXfer {
    std::vector<std::string> sent;
    std::vector<std::string> got;
    int64_t elapsed_ms = 0;
};

// One stream across an established link: receiver on its own thread bounded by
// RCVTIMEO, sender on this one. The receiver runs CONCURRENTLY on purpose -- a
// send-then-receive shape would park the whole stream in the receive buffer and
// test buffering rather than delivery, and a live consumer does not work that
// way. Returns what went in and what came out.
static LiveXfer live_exchange(SRTSOCKET snd, SRTSOCKET rcv, int count, int size, int gap_us,
                              int rcv_timeo_ms) {
    LiveXfer x;
    st_set_int(rcv, SRTO_RCVTIMEO, rcv_timeo_ms);
    const int64_t t0 = live_now_ms();
    {
        LiveJoiner rx(std::thread(live_rx_collect, rcv, count, size, &x.got));
        x.sent = st_send_stream(snd, count, size, gap_us);
    }
    x.elapsed_ms = live_now_ms() - t0;
    return x;
}

// "Every message arrived, in the order it was sent, none missing, none
// truncated, none altered." Asserted per message so a failure names the one
// that gave way, and the `what` prefix names the configuration that was in
// force.
static void live_require_complete_stream(const LiveXfer& x, int count, const char* what) {
    ST_REQUIRE_MSG((int)x.got.size() == count,
                   "%s: %d of %d messages were delivered in %lld ms; the stream must arrive "
                   "complete", what, (int)x.got.size(), count, (long long)x.elapsed_ms);
    for (int i = 0; i < count; i++) {
        const int idx = live_msg_index(x.got[(size_t)i]);
        ST_REQUIRE_MSG(idx == i,
                       "%s: the message delivered at position %d carries index %d; the stream "
                       "arrived with a gap or out of order, which a lossless live transport must "
                       "not produce", what, i, idx);
        ST_REQUIRE_MSG((int)x.got[(size_t)i].size() == (int)x.sent[(size_t)i].size(),
                       "%s: message %d arrived with %d bytes, sent with %d; what arrives must be "
                       "the message that was sent, not a truncated one",
                       what, i, (int)x.got[(size_t)i].size(), (int)x.sent[(size_t)i].size());
        ST_REQUIRE_MSG(x.got[(size_t)i] == x.sent[(size_t)i],
                       "%s: message %d differs byte-for-byte from what was sent; the channel "
                       "delivered something, but not the payload", what, i);
    }
}

// The channel really carried this traffic, and the schedule really removed what
// the case says it removed. Without this, every payload assertion above could
// be satisfied by a link the relay was never in.
static void live_require_relay_did_it(const Relay* r, int expect_dropped, int min_seen,
                                      const char* what) {
    ST_REQUIRE_MSG(r->seen_fwd() >= min_seen,
                   "%s: the relay indexed only %d forward DATA packets for %d messages; the "
                   "traffic did not go through the channel this case configured, so nothing "
                   "below describes the schedule it claims to", what, r->seen_fwd(), min_seen);
    ST_REQUIRE_MSG(r->dropped_fwd() == expect_dropped,
                   "%s: the relay dropped %d forward DATA packets, expected exactly %d; the drop "
                   "schedule is index-based and must remove the same packets on every run",
                   what, r->dropped_fwd(), expect_dropped);
}

// The live transport configuration both groups start from: TSBPD on, a latency
// generous enough that the relay schedule rather than the clock decides what
// arrives, and too-late drop OFF -- the precondition g08_recovery's description
// names. Both sides are configured: SRTO_TLPKTDROP is exchanged in the HSv5
// handshake, and the sender only discards a late packet when the peer
// advertised the flag, so switching it off at the receiver is also what keeps
// the sender from dropping.
static void live_conf(SRTSOCKET s) { st_live_lossless(s); }

// ----------------------------------------------------- bringing a link up
//
// Both groups use srtkit's Link and st_try_connect.
//
// This file used to carry a local copy of that machinery. The reason it did is
// worth recording, because it is a real property of the library: a blocking
// srt_accept has no bound this release honours. SRTO_RCVTIMEO never reaches
// CUDTUnited::accept, which waits on m_AcceptCond with a plain
// pthread_cond_wait and consults only m_bSynRecving. Half of what g00_endtoend
// asserts is a REFUSED handshake, and against an unbounded accept that case
// would hang until the 60 s SRT_CASE_TIMEOUT instead of reporting the refusal
// it was written to observe. srtkit's acceptor has since been rewritten around
// exactly that fact -- it sets SRTO_RCVSYN=0 on the listener BEFORE the group's
// own SockConf runs, so nothing a case configures can remove the bound, polls
// srt_accept, and gives up once the caller has finished connecting and a grace
// has elapsed -- so the local copy was duplication, and two idioms for one
// problem in one package is how one of them goes stale.
//
// st_try_connect rather than st_connect: st_connect asserts that the handshake
// came up, and half of what this group asserts is that it did not. The grace
// below is short because the only case that waits it out is the refusal case,
// where nothing will ever be accepted and the wait is pure latency.
static const int LIVE_ACCEPT_GRACE_MS = 1500;

// The post-connect check st_connect would have made, spelled out here so that
// the failure message names the configuration that did not come up.
static void live_require_up(Link& L, const char* what) {
    ST_REQUIRE_MSG(L.connect_rc != SRT_ERROR, "%s: srt_connect failed with %s (\"%s\")", what,
                   live_errname(L.connect_err), srt_strerror(L.connect_err, 0));
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK,
                   "%s: nothing was accepted although srt_connect reported success", what);
    const SRT_SOCKSTATUS st = srt_getsockstate(L.cal);
    ST_REQUIRE_MSG(st == SRTS_CONNECTED,
                   "%s: the caller socket is %s (%d), expected SRTS_CONNECTED (%d)", what,
                   live_statename(st), (int)st, (int)SRTS_CONNECTED);
}

// SRTO_RCVKMSTATE / SRTO_SNDKMSTATE, reported by name. A bounded poll on the
// OBSERVED state, never a timer: it returns the moment the state leaves
// SECURING and at worst costs `budget_ms`. At this release the key material
// rides the handshake, so the state is already settled when srt_connect returns
// and this returns on the first read; the poll is here so that an
// implementation which settles a moment later is not failed for the timing.
static int live_settled_kmstate(SRTSOCKET s, SRT_SOCKOPT which, int budget_ms, const char* what) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
    for (;;) {
        int v = -1;
        int n = (int)sizeof v;
        srt_clearlasterror();
        ST_REQUIRE_MSG(srt_getsockflag(s, which, &v, &n) == 0,
                       "%s: srt_getsockflag(opt %d) failed with %s (\"%s\")", what, (int)which,
                       live_errname(srt_getlasterror(NULL)), srt_getlasterror_str());
        if (v != SRT_KM_S_SECURING) return v;
        if (std::chrono::steady_clock::now() >= deadline) return v;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

// --------------------------------------------- g00_endtoend's configuration
//
// An encrypted live socket with strict enforcement turned on EXPLICITLY.
//
// Explicitly, although scope.md says the default is already 1, for two
// reasons. It is what makes the group's claim "with strict enforcement enabled"
// an observation rather than an assumption about a default; and it is the one
// call that a library without this milestone cannot satisfy, so it is what
// makes this `new`-tier group fail at the previous release on an assertion of
// its own rather than on a timeout or a crash.
//
// SRTO_PBKEYLEN is set explicitly so the key length is a stated part of this
// configuration rather than a library fallback, which is exactly what
// interface.md now tells the candidate the option is for: "An explicit
// SRTO_PBKEYLEN fixes the key length; it does not switch encryption on."
// v1.3.2's CCryptoControl::init takes the branch
// `if (hasPassphrase()) { if (m_iSndKmKeyLen == 0) m_iSndKmKeyLen = 16; }`, so
// a passphrase on its own already selects a 16-byte key -- removing this line
// and keeping the passphrase was measured to leave the stream encrypted and the
// case passing. What proves encryption here is therefore the KM state and the
// undecrypt counter below, never the presence of this call.
//
// The set's return code is handed back rather than asserted here: this runs
// inside a SockConf, and a case reads it immediately after the connect so the
// failure is reported from the main line of the case.
static int live_conf_encrypted(SRTSOCKET s, const char* passphrase, int* strict_rc) {
    live_conf(s);
    st_set_int(s, SRTO_PBKEYLEN, 16);
    st_set_str(s, SRTO_PASSPHRASE, passphrase);
    srt_clearlasterror();
    *strict_rc = st_try_set_int(s, SRTO_STRICTENC, 1);
    return *strict_rc;
}

// The assertion that names the milestone. At v1.3.2 both sets return 0; at
// v1.3.0 the option does not exist and srt_setsockflag refuses it, which is the
// message a candidate who has not built this milestone must see.
static void live_require_strict_enforcement(int rc_lsn, int rc_cal) {
    ST_REQUIRE_MSG(rc_lsn == 0,
                   "srt_setsockflag(SRTO_STRICTENC, 1) on the listener returned %d, expected 0; "
                   "strict encryption enforcement is the capability this milestone adds, and an "
                   "encrypted stream cannot be said to cross \"with strict enforcement enabled\" "
                   "on a library that will not accept the option", rc_lsn);
    ST_REQUIRE_MSG(rc_cal == 0,
                   "srt_setsockflag(SRTO_STRICTENC, 1) on the caller returned %d, expected 0; "
                   "strict encryption enforcement is the capability this milestone adds, and the "
                   "refusal asserted by this group is the refusal that option produces", rc_cal);
}

// =========================================================================
// g00_endtoend   (new -- SCORED, this step's end-to-end path)
//
// endtoend.txt: "an encrypted live stream with strict enforcement enabled crosses
// a clean relay, arrives byte-identical and in order, and a passphrase mismatch
// is refused at connect time".
//
// Two cases, and they are each other's control. The first is the positive: the
// same option, the same key length, matching passphrases, and everything gets
// through. The second is the negative: change one thing, the passphrase on one
// side, and the connection must be refused before any payload moves. Without
// the first, a library that refused every connection would score the second;
// without the second, a library that ignored SRTO_STRICTENC entirely would
// score the first.
// =========================================================================

SRT_CASE(g00_endtoend, an_encrypted_stream_with_strict_enforcement_crosses_intact) {
    Link L;
    int strict_lsn = -1, strict_cal = -1;
    st_try_connect(
        L, RelaySched(),
        [&strict_lsn](SRTSOCKET s) { live_conf_encrypted(s, E2E_PW_A, &strict_lsn); },
        [&strict_cal](SRTSOCKET s) { live_conf_encrypted(s, E2E_PW_A, &strict_cal); },
        LIVE_ACCEPT_GRACE_MS);

    live_require_strict_enforcement(strict_lsn, strict_cal);
    live_require_up(L, "matching passphrases with SRTO_STRICTENC=1");

    LiveXfer x = live_exchange(L.cal, L.acc, E2E_N, LIVE_PAYLOAD, E2E_GAP_US, E2E_RECV_MS);
    live_require_complete_stream(x, E2E_N, "encrypted live stream with strict enforcement");

    const int bad = st_first_out_of_order(x.sent, x.got);
    ST_REQUIRE_MSG(bad == -1,
                   "the message at position %d is not the one that was sent there: the stream "
                   "arrived, but not in the order it was sent", bad);

    live_require_relay_did_it(L.relay, 0, E2E_N, "encrypted live stream over a clean relay");

    // Not a vacuous "it arrived": the stream really was encrypted end to end,
    // and nothing had to be thrown away undecrypted on the way. Without these
    // three, a library that ignored SRTO_PASSPHRASE and sent in clear would
    // score this case.
    const int rcv_km = live_settled_kmstate(L.acc, SRTO_RCVKMSTATE, 2000, "accepted socket");
    ST_REQUIRE_MSG(rcv_km == SRT_KM_S_SECURED,
                   "SRTO_RCVKMSTATE on the accepted socket is %s (%d), expected SRT_KM_S_SECURED "
                   "(%d); the payload arrived, but not over an encrypted connection",
                   live_kmname(rcv_km), rcv_km, (int)SRT_KM_S_SECURED);
    const int snd_km = live_settled_kmstate(L.cal, SRTO_SNDKMSTATE, 2000, "caller socket");
    ST_REQUIRE_MSG(snd_km == SRT_KM_S_SECURED,
                   "SRTO_SNDKMSTATE on the caller is %s (%d), expected SRT_KM_S_SECURED (%d)",
                   live_kmname(snd_km), snd_km, (int)SRT_KM_S_SECURED);

    const SRT_TRACEBSTATS rb = live_stats(L.acc, "receiving");
    ST_REQUIRE_MSG(rb.pktRcvUndecryptTotal == 0,
                   "srt_bstats().pktRcvUndecryptTotal is %d on a correctly keyed connection, "
                   "expected 0: every packet that arrived was decrypted",
                   rb.pktRcvUndecryptTotal);
    ST_REQUIRE_MSG(rb.pktRecvTotal >= (int64_t)E2E_N,
                   "the receiver reports pktRecvTotal=%lld for %d delivered messages; the "
                   "counters must describe traffic that really crossed",
                   (long long)rb.pktRecvTotal, E2E_N);
}

SRT_CASE(g00_endtoend, a_passphrase_mismatch_is_refused_at_connect_time) {
    Link L;
    int strict_lsn = -1, strict_cal = -1;
    st_try_connect(
        L, RelaySched(),
        [&strict_lsn](SRTSOCKET s) { live_conf_encrypted(s, E2E_PW_A, &strict_lsn); },
        [&strict_cal](SRTSOCKET s) { live_conf_encrypted(s, E2E_PW_B, &strict_cal); },
        LIVE_ACCEPT_GRACE_MS);

    // Read first: if the option was never accepted, the refusal below -- or the
    // absence of one -- says nothing about strict enforcement.
    live_require_strict_enforcement(strict_lsn, strict_cal);

    ST_REQUIRE_MSG(L.connect_rc == SRT_ERROR,
                   "two different passphrases with SRTO_STRICTENC=1: srt_connect returned %d and "
                   "the handshake COMPLETED; it must be refused with -1 / SRT_ECONNREJ (%d). "
                   "Completing here is the v1.3.0 outcome, where the connection came up and the "
                   "mismatch showed only as BADSECRET KM state and undecryptable payload",
                   L.connect_rc, (int)SRT_ECONNREJ);
    ST_REQUIRE_MSG(L.connect_err == SRT_ECONNREJ,
                   "two different passphrases: srt_connect failed with %s (%d, \"%s\"); expected "
                   "SRT_ECONNREJ (%d), the documented report for a connection refused at "
                   "handshake time", live_errname(L.connect_err), L.connect_err,
                   srt_strerror(L.connect_err, 0), (int)SRT_ECONNREJ);
    ST_REQUIRE_MSG(L.acc == SRT_INVALID_SOCK,
                   "two different passphrases: srt_connect was refused but the listener still "
                   "accepted a socket (%d); a rejected handshake must leave no connection on "
                   "either side", (int)L.acc);

    const SRT_SOCKSTATUS st = srt_getsockstate(L.cal);
    ST_REQUIRE_MSG(st == SRTS_OPENED,
                   "after the refusal the caller socket is %s (%d), expected SRTS_OPENED (%d): it "
                   "falls back to OPENED, not to SRTS_BROKEN (%d). That state is a report and not "
                   "an invitation -- the refusal spends the socket, and g02_caller_pw asserts that "
                   "a second srt_connect on it is refused with SRT_ECONNSOCK",
                   live_statename(st), (int)st, (int)SRTS_OPENED, (int)SRTS_BROKEN);
}

// =========================================================================
// g08_recovery   (inherited)
//
// scored.txt: "datagrams dropped at fixed indexes are retransmitted and the
// payload is delivered intact when too-late drop is disabled".
//
// One subject: ARQ recovery under a fixed loss schedule. The first case is the
// control that gives the rest their meaning -- it pins the counters on a
// channel with no scheduled drops, so the floors the lossy cases assert cannot
// be met by a library that reports the same number whatever happened.
//
// Nothing in this group names an identifier that is new at this milestone, and
// that is deliberate: it must run, and pass, against the previous release.
// =========================================================================

// The control. Same configuration, same transfer, no drops: everything arrives,
// the counters describe traffic that really crossed, and nothing is skipped as
// too late to play.
//
// It used to bound the observed loss counters from above as well, which is what
// ruled out a library answering "25" to every loss query. Those two ceilings
// were deleted (NOTES.md, "Gate cases deleted for non-determinism") because
// this group is GATE: it vetoes and earns nothing, so it needs to be
// unflakeable rather than discriminating, and a ceiling on incidental loss is a
// statement about the host's load. The scored recovery group at the step that
// introduced it keeps its ceilings and carries that argument.
SRT_CASE(g08_recovery, a_clean_channel_needs_no_retransmission) {
    Link L;
    RelaySched sc;
    st_connect(L, sc, live_conf, live_conf);

    LiveXfer x = live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US, LIVE_CLEAN_MS);
    live_require_complete_stream(x, LIVE_N, "clean channel");
    live_require_relay_did_it(L.relay, 0, LIVE_N, "clean channel");

    const SRT_TRACEBSTATS rb = live_stats(L.acc, "receiving");
    const SRT_TRACEBSTATS sb = live_stats(L.cal, "sending");

    ST_REQUIRE_MSG(rb.pktRecvTotal >= (int64_t)LIVE_N,
                   "the receiver reports pktRecvTotal=%lld for %d delivered messages; the "
                   "counters must describe traffic that really crossed",
                   (long long)rb.pktRecvTotal, LIVE_N);
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
                   "SRTO_TLPKTDROP is 0 and the latency is 900 ms, but the receiver reports "
                   "pktRcvDropTotal=%d too-late-to-play packets; with too-late drop disabled "
                   "that counter must not move at all", rb.pktRcvDropTotal);
    // The sender's mirror of the same claim, and the reason this case reads the
    // sender's counters at all. Both are floors or zeroes, never ceilings: the
    // sender must have put at least the messages on the wire that the receiver
    // got, and with too-late drop off it must never have discarded one as too
    // late to send. Neither depends on the host's load.
    ST_REQUIRE_MSG(sb.pktSndDropTotal == 0,
                   "SRTO_TLPKTDROP is 0 on both sides, but the sender reports "
                   "pktSndDropTotal=%d packets discarded as too late to send; with too-late "
                   "drop disabled that counter must not move at all", sb.pktSndDropTotal);
    ST_REQUIRE_MSG(sb.pktSentTotal >= (int64_t)LIVE_N,
                   "the sender reports pktSentTotal=%lld for %d messages handed to srt_sendmsg; "
                   "the counters must describe traffic that really crossed",
                   (long long)sb.pktSentTotal, LIVE_N);
}

// The subject, in its plainest form. Thirty datagrams removed at fixed indexes,
// in three clusters of ten so that each cluster is certainly payload rather
// than a lone packet at the edge of the schedule. With too-late drop off and
// 900 ms of latency the payload must still arrive complete -- and the
// receiver's loss counter and the sender's retransmission counter must both
// show the recovery, so neither a library that delivered nothing nor one that
// never noticed the gap can pass.
SRT_CASE(g08_recovery, datagrams_dropped_at_fixed_indexes_are_retransmitted) {
    Link L;
    RelaySched sc;
    for (int i = 40; i < 50; i++) sc.drop_fwd.insert(i);
    for (int i = 80; i < 90; i++) sc.drop_fwd.insert(i);
    for (int i = 120; i < 130; i++) sc.drop_fwd.insert(i);
    st_connect(L, sc, live_conf, live_conf);

    LiveXfer x = live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US, LIVE_LOSSY_MS);

    live_require_relay_did_it(L.relay, 30, LIVE_N, "three clusters of ten dropped datagrams");
    live_require_complete_stream(x, LIVE_N, "three clusters of ten dropped datagrams");
    const int bad = st_first_out_of_order(x.sent, x.got);
    ST_REQUIRE_MSG(bad == -1,
                   "the message at position %d is not the one that was sent there: a "
                   "retransmitted packet was delivered out of sequence instead of being held "
                   "until its place came up", bad);

    const SRT_TRACEBSTATS rb = live_stats(L.acc, "receiving");
    const SRT_TRACEBSTATS sb = live_stats(L.cal, "sending");

    ST_REQUIRE_MSG(rb.pktRcvLossTotal >= LIVE_SCHEDULED_MIN,
                   "30 forward DATA packets were removed by the relay, but the receiver reports "
                   "pktRcvLossTotal=%d, below the floor of %d; the loss was never detected, so "
                   "the complete stream above was not the result of recovering from it",
                   rb.pktRcvLossTotal, LIVE_SCHEDULED_MIN);
    ST_REQUIRE_MSG(sb.pktRetransTotal >= LIVE_SCHEDULED_MIN,
                   "the payload arrived complete but the sender reports pktRetransTotal=%d, "
                   "below the floor of %d; it was not retransmission that recovered it",
                   sb.pktRetransTotal, LIVE_SCHEDULED_MIN);
    ST_REQUIRE_MSG(rb.pktRcvDropTotal == 0,
                   "too-late drop was disabled but the receiver reports pktRcvDropTotal=%d "
                   "packets skipped as too late to play; with SRTO_TLPKTDROP=0 the receiver must "
                   "wait for the retransmission instead of skipping the hole",
                   rb.pktRcvDropTotal);
    ST_REQUIRE_MSG(sb.pktSndDropTotal == 0,
                   "the sender reports pktSndDropTotal=%d packets discarded as too late to send; "
                   "the receiver did not advertise SRTO_TLPKTDROP, so the sender must keep every "
                   "packet available for retransmission", sb.pktSndDropTotal);
}

// A burst: forty consecutive datagrams gone. One NAK covers the whole range, so
// this exercises range loss reporting rather than forty separate single
// reports, and it is a harder recovery than the clustered case because nothing
// of the stream arrives for the length of the gap.
SRT_CASE(g08_recovery, a_burst_of_consecutive_loss_is_recovered) {
    Link L;
    RelaySched sc;
    for (int i = 100; i < 140; i++) sc.drop_fwd.insert(i);
    st_connect(L, sc, live_conf, live_conf);

    LiveXfer x = live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US, LIVE_LOSSY_MS);

    live_require_relay_did_it(L.relay, 40, LIVE_N, "a burst of 40 consecutive dropped datagrams");
    live_require_complete_stream(x, LIVE_N, "a burst of 40 consecutive dropped datagrams");

    const SRT_TRACEBSTATS rb = live_stats(L.acc, "receiving");
    const SRT_TRACEBSTATS sb = live_stats(L.cal, "sending");

    ST_REQUIRE_MSG(rb.pktRcvLossTotal >= LIVE_SCHEDULED_MIN,
                   "a burst of 40 consecutive forward DATA packets was removed, but the receiver "
                   "reports pktRcvLossTotal=%d, below the floor of %d: the burst went unnoticed",
                   rb.pktRcvLossTotal, LIVE_SCHEDULED_MIN);
    ST_REQUIRE_MSG(sb.pktRetransTotal >= LIVE_SCHEDULED_MIN,
                   "the stream arrived complete across a 40-packet burst but the sender reports "
                   "pktRetransTotal=%d, below the floor of %d: the gap was filled by something "
                   "other than retransmission", sb.pktRetransTotal, LIVE_SCHEDULED_MIN);
    ST_REQUIRE_MSG(rb.pktRcvDropTotal == 0,
                   "too-late drop was disabled but %d packets were skipped as too late to play",
                   rb.pktRcvDropTotal);
}

// Loss spread over the whole transfer rather than clustered: every sixth
// forward data packet. Recovery has to keep up for the length of the stream,
// not merely recover from three isolated events.
//
// The drops stop short of the tail on purpose. SRT reveals a loss by the gap it
// leaves -- a NAK goes out when a packet with a higher sequence number arrives
// -- so a dropped packet with no successor is never reported and never
// retransmitted. A live stream does not end; this finite one does, and dropping
// its last datagram would be asking the transport for a recovery the protocol
// cannot perform. The relay's indexes count every forward data packet,
// retransmissions included, so the retransmissions this schedule provokes push
// index 222 back to well before the last message and leave that tail clean.
SRT_CASE(g08_recovery, periodic_loss_is_recovered_for_the_whole_stream) {
    Link L;
    RelaySched sc;
    for (int i = 6; i <= 222; i += 6) sc.drop_fwd.insert(i);
    const int scheduled = (int)sc.drop_fwd.size();
    st_connect(L, sc, live_conf, live_conf);

    LiveXfer x = live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US, LIVE_LOSSY_MS);

    live_require_relay_did_it(L.relay, scheduled, LIVE_N, "every sixth datagram dropped");
    live_require_complete_stream(x, LIVE_N, "every sixth datagram dropped");

    const SRT_TRACEBSTATS rb = live_stats(L.acc, "receiving");
    const SRT_TRACEBSTATS sb = live_stats(L.cal, "sending");

    ST_REQUIRE_MSG(rb.pktRcvLossTotal >= LIVE_SCHEDULED_MIN,
                   "%d forward DATA packets were removed one every six, but the receiver reports "
                   "pktRcvLossTotal=%d, below the floor of %d: periodic loss went unnoticed",
                   scheduled, rb.pktRcvLossTotal, LIVE_SCHEDULED_MIN);
    ST_REQUIRE_MSG(sb.pktRetransTotal >= LIVE_SCHEDULED_MIN,
                   "periodic loss was recovered without retransmission: the sender reports "
                   "pktRetransTotal=%d, below the floor of %d", sb.pktRetransTotal,
                   LIVE_SCHEDULED_MIN);
    ST_REQUIRE_MSG(rb.pktRcvDropTotal == 0,
                   "too-late drop was disabled but %d packets were skipped as too late to play",
                   rb.pktRcvDropTotal);
}
