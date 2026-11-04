// Family "live", checkpoint 04 (SRT v1.3.3). Five groups:
//
//   g00_endtoend    inherited (E2E_TIER)  GATE, not scored: a live stream of
//                                    4000 messages crosses a clean relay and
//                                    arrives byte-identical, in order, with no
//                                    gaps. The previous milestone already
//                                    carried this path, so it vetoes and earns
//                                    nothing.
//   g09_bstats_bw   new              srt_bstats on a freshly connected socket
//                                    reports the raised "no limit" ceiling, at
//                                    the -1 default and at an explicit 0.
//   g10_transfer    inherited        live messages over an intact channel
//                                    arrive complete, in order, byte-identical.
//   g11_recovery    inherited        datagrams dropped at fixed relay indexes
//                                    are retransmitted and the payload arrives
//                                    complete and in order, with too-late drop
//                                    off and a generous latency budget.
//   g12_encryption  inherited        a matching passphrase and explicit key
//                                    length carry the payload intact, nothing
//                                    readable crosses the wire, and both sides
//                                    report SRT_KM_S_SECURED.
//
// ------------------------------------------------------------------- tiers
//
// This family MIXES TIERS. g09_bstats_bw is `new`, and is this family's only
// SCORED group. g00_endtoend, g10_transfer, g11_recovery and g12_encryption are
// `inherited` and exist to be earned by a candidate who shipped v1.3.2 and did
// nothing at all for this milestone. g00's tier is not a property of this file:
// it follows E2E_TIER in meta.env, which carries the measurement, and at this
// step that is `inherited` -- see the g00 header below and endtoend.txt. Do not
// restate it here as `new`; the shipped groups.txt grades it `gate 0`. One family is one compiled `.cpp`, so all five verdicts come out
// of a single binary that must BUILD at the v1.3.2 prefix as well as at v1.3.3.
// A compile or link error there would cost the three inherited groups their
// point for a reason having nothing to do with the candidate.
//
// DRIVER_RULES, "Naming an identifier the candidate may not have", is therefore
// binding. Here it is satisfied WITHOUT dlsym and WITHOUT a restated constant,
// because this family needs no identifier that is new at v1.3.3. That is
// checked, not assumed; the full list of what this file names is:
//
//   functions  srt_socket, srt_bind, srt_listen, srt_accept, srt_connect,
//              srt_close, srt_getsockname, srt_getsockflag, srt_setsockflag,
//              srt_sendmsg, srt_recvmsg, srt_bstats, srt_getsockstate,
//              srt_getlasterror, srt_getlasterror_str, srt_strerror,
//              srt_clearlasterror
//   options    SRTO_TSBPDMODE, SRTO_LATENCY, SRTO_TLPKTDROP, SRTO_RCVTIMEO,
//              SRTO_MAXBW, SRTO_PBKEYLEN, SRTO_PASSPHRASE, SRTO_RCVKMSTATE,
//              SRTO_SNDKMSTATE
//   stats      pktRecvTotal, pktSentTotal, pktRcvLossTotal, pktRetransTotal,
//              pktRcvDropTotal, pktSndDropTotal, pktRcvUndecryptTotal,
//              mbpsMaxBW, usPktSndPeriod
//   enums      SRT_KM_S_*, SRT_SOCKSTATUS, SRT_ERRNO
//
// Every one of those is present and unchanged in `git show v1.3.2:srtcore/srt.h`
// -- SRTO_SNDKMSTATE is `= 40` and SRTO_RCVKMSTATE follows it at both tags, and
// CBytePerfMon carries `double mbpsMaxBW` and `double usPktSndPeriod` at both.
// `api-delta.md` §Step 04 adds only `srt_listen_callback`, `SRTO_IPV6ONLY` and
// `SRTO_PEERIDLETIMEO`, none of which this family names: they belong to the
// `hook` and `api` families. So there is nothing here to restate and nothing to
// resolve dynamically, and adding a decorative `#ifndef`/`#define` block would
// only introduce a number that could drift.
//
// What the discrimination rests on instead is a VALUE, not a symbol.
// g09_bstats_bw reads two fields that exist at both releases and asserts bounds
// that only the raised BW_INFINITE can satisfy (deltas/04.md D6 and D7):
// measured on this harness, v1.3.2 reports mbpsMaxBW 30.0000 with
// usPktSndPeriod 362.0000, and v1.3.3 reports 1000.0000 with 10.0000. At the
// previous prefix the group therefore fails on its own assertion, naming actual
// and expected, which is what an unimplemented milestone is supposed to look
// like -- never on a link error.
//
// ------------------------------- g00_endtoend does NOT discriminate, so it is
// ------------------------------- a GATE group here, and earns nothing
//
// Recorded here because it is a property of this file that a reader must not
// have to rediscover. `endtoend.txt` for step 04 reads "a live stream of 4000
// messages crosses a clean relay and arrives byte-identical, in order, with no
// gaps" -- word for word the first of step 01's two end-to-end checks, with
// nothing in it that v1.3.3 added. Implemented faithfully, as it is below, it
// PASSES at the v1.3.2 prefix as well as at v1.3.3 (measured: 4000/4000 in
// 2860 ms and 2691 ms respectively, zero relay drops, zero loss on both).
//
// That measurement is the whole argument for its tier. Under reward = gate x
// score a group that a no-op already passes must not be SCORED, or the no-op
// collects credit for work it did not do: with `E2E_TIER=new` this row paid a
// no-op continuation 0.157895 at this step, where a no-op must score exactly
// 0. `meta.env` therefore carries `E2E_TIER=inherited`, and this row joins the
// four `inherited` groups in scored.txt and proof of life as the step's gate.
// It vetoes and earns nothing.
//
// The alternative -- quietly folding a v1.3.3-only observation into this row so
// that it "fails at the previous prefix" -- would score something the candidate
// was never shown, which interface.md forbids and DRIVER_RULES rule 7 forbids
// again. The fix was one line of `meta.env`, not one line of this file.
//
// ---------------------------------------------------------- traceability
//
// g00_endtoend. endtoend.txt gives the claim in full. The transport behaviour it
// rests on is srt-docs/live-streaming.md ("SRT... will retransmit lost packets
// ... within the latency window") and API.md on SRTO_TLPKTDROP, both of which
// the configuration below follows; interface.md's closing paragraph names the
// same configuration -- TSBPD on, a generous SRTO_LATENCY, SRTO_TLPKTDROP off
// on both sides -- as the precondition for requiring a complete payload.
//
// g09_bstats_bw. scored.txt gives the claim; interface.md is the source for
// every number in it: "on a freshly connected socket, mbpsMaxBW is above 500
// and usPktSndPeriod is below 100 microseconds, both read before any payload is
// sent, and the same two bounds hold after the cap is explicitly set to 0 ...
// Both fields must already be populated when the connection completes; a tree
// that leaves usPktSndPeriod at 0 until the first packet goes out fails the
// group." scope.md licenses the third case's control: for "the rate the
// congestion controller assumes when SRTO_MAXBW is left at its -1 default or
// set to 0", "srt_bstats() must report it as mbpsMaxBW, and as the
// corresponding send period in usPktSndPeriod" -- the same field, on a socket
// given a real figure instead of the sentinel, is what shows the field is a
// report of the ceiling in force. The two fields' meanings are srt.h's own:
// mbpsMaxBW is the "Transmit
// Bandwidth ceiling (Mbps)" and usPktSndPeriod the "packet sending period, in
// microseconds". SRTO_MAXBW's -1 default and its `= 0` setting are API.md's
// (option table row `SRTO_MAXBW ... bytes/sec ... Default -1`).
//
// g10_transfer. Milestone-01 transport surface that this step inherits, named
// in inherit.md ("a live stream crosses an intact or lossy channel exactly as
// before") and in handoff-keep.md. Message boundaries are API.md's live-mode
// contract: SRTT_LIVE sets SRTO_MESSAGEAPI true, so one srt_sendmsg is one
// message and one srt_recvmsg returns exactly that message.
//
// g11_recovery. The same inherited surface, with the precondition its
// description names. srt-docs/API.md on SRTO_TLPKTDROP: "When true (default),
// it will drop the packets that haven't been retransmitted on time ... You can
// turn this off to always ensure a clean delivery"; the counter it feeds,
// pktRcvDropTotal, is "number of too-late-to play missing packets" (srt.h).
// live-streaming.md on latency: it "should cover both any unexpected
// transmission delays for a UDP packet, as well as allowing extra time for the
// case where a packet is lost and has to be retransmitted". pktRcvLossTotal and
// pktRetransTotal are what turn "it arrived" into "it arrived BECAUSE it was
// retransmitted". interface.md repeats the precondition verbatim for this step.
//
// g12_encryption. srt-docs/encryption.md is the authority for the mechanism;
// API.md for SRTO_PASSPHRASE ("Sets the passphrase for encryption. This turns
// encryption on on this side", range "[10..79]") and for SRTO_PBKEYLEN
// ("Sender encryption key length", range "0 16(128/8) 24(192/8) 32(256/8)");
// srt.h for SRTO_RCVKMSTATE / SRTO_SNDKMSTATE and the SRT_KM_STATE values.
// interface.md for this step adds nothing that contradicts them, and inherit.md
// requires "the encryption groups from milestone 01 still pass".
//
// --------------------------------------------------------- determinism
//
// Nothing here waits on a clock for control flow. Loss comes from RelaySched at
// fixed DATA-packet indexes, every receiver runs on its own thread bounded by
// SRTO_RCVTIMEO, the one place that waits for keying to settle waits on the
// OBSERVED state with a deadline, and no case sleeps to drain.
//
// relay.h indexes DATA packets only and forwards every control packet
// untouched, so `L.relay->dropped_fwd()` is exactly how many payload packets
// the schedule removed and the cases assert that number exactly. A datagram
// counter that also counted ACKs and NAKs would name a different packet on
// every run.
//
// The loss THRESHOLDS are bands, not equalities, and the two bands do not
// touch; both are documented at their definitions below. This is a userspace
// UDP relay on a VM shared with nine other build sessions, and
// "pktRcvLossTotal == 0 on a clean channel" is not a reproducible observation
// on this harness even though every clean run measured for this file reported
// exactly that.
//
// ------------------------------------------- deliberately NOT asserted
//
//   * any EXACT value of mbpsMaxBW or usPktSndPeriod. interface.md gives the
//     bounds the candidate is graded against, and scope.md gives the milestone's
//     figure; the measured 1000.0 and 10.0 are recorded in comments and never
//     asserted, so a tree that lands on the same ceiling by a different
//     arithmetic still passes.
//   * that the caller-side and accepted-side sockets report the SAME figures.
//     They do, and it would be a stronger statement than any document makes:
//     the ceiling is per-socket state and nothing in interface.md or scope.md
//     says the two agree, so the cases read the socket whose cap they set.
//   * a positive cap on the ACCEPTED socket. SRTO_MAXBW is inherited by the
//     accepted socket from the listener in this release, but no document in
//     this step states that, so the control below sets it on the caller only.
//   * any exact value of pktRcvLossTotal or pktRetransTotal. How many
//     retransmissions occur depends on how many NAKs cross and on whether a
//     retransmission is itself caught by the schedule, and no document fixes
//     it. DRIVER_RULES rule 5: shape, never a particular number.
//   * how LONG recovery takes. The latency is a budget, not a deadline any
//     document states, so the cases assert that delivery happened inside a
//     generous SRTO_RCVTIMEO and never that it happened by a particular time.
//   * anything about SRTO_CONGESTION / SRTO_SMOOTHER. deltas/04.md puts the
//     rename in "do not score these", and the `api` family owns slot 47.
//   * SRTO_SNDDROPDELAY and SRTO_NAKREPORT. Both exist at both tags and both
//     contribute to why recovery works here, but no document in this step
//     requires a candidate to have them, so no case names them.
//   * loss in the BACKWARD direction. RelaySched::drop_bwd is not data-indexed
//     -- the backward path of a live stream is almost entirely control -- so a
//     schedule there would drop a timing-dependent set of ACKs and the group
//     would stop being reproducible.
//   * the KM state of a socket whose handshake did not complete. Nothing here
//     provokes that; g12's negative half is a wire observation, not a refusal.
#include "stest.h"
#include "srtkit.h"
#include "relay.h"

#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------- sizes
//
// The documented live payload size (srt-docs/API.md: "SRTO_PAYLOADSIZE = 1316"
// among the SRTT_LIVE defaults). One message is then exactly one data packet,
// which is what lets a drop schedule expressed in data-packet indexes be read
// as "these messages went missing".
static const int LIVE_PAYLOAD = 1316;

// The gate's stream. Every gate in this chain carries a few thousand messages:
// the point of the size is that "byte-identical and in order" is a claim about
// a stream rather than about a handful of packets. 300 us of pacing is the
// figure steps 01, 02 and 03 all use, and it was measured here at 4000/4000
// with zero loss at both prefixes.
static const int GATE_N = 4000;
static const int GATE_GAP_US = 300;
static const int GATE_RECV_MS = 10000;

// Messages per non-gate transfer, and the pacing between them. The pacing is
// not decoration: this is a userspace relay on a shared box, and a heavier
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
// was measured reporting all thirty, so the floor sits under every measurement
// while a clean channel -- measured at pktRcvLossTotal = 0 and pktRetransTotal
// = 0 at both prefixes -- stays far below it. A floor is safe in a gate group
// in a way its mirror image is not: a busy host can only ADD incidental loss,
// so it can never push a lossy run under the floor, while a CEILING on a clean
// run is a statement about the host. The ceiling LIVE_INCIDENTAL_MAX = 20 that
// used to stand here was deleted with the three assertions that read it; see
// NOTES.md, "Gate cases deleted for non-determinism".
static const int LIVE_SCHEDULED_MIN = 25;

// g09_bstats_bw's two bounds, both straight out of interface.md. They are
// bounds and not equalities on purpose: interface.md gives a range rather than
// a figure, and the range is what the case is licensed to assert. scope.md does
// name the milestone's figure (1000.0 Mbps / about 10 us) and the one it
// replaces (30.0 Mbps / about 362 us), which is what the failure messages
// quote. Measured: v1.3.3 reports 1000.0000 / 10.0000, v1.3.2 reports 30.0000 /
// 362.0000, so each bound sits well clear of both: the 500 floor is about 16x
// above v1.3.2's 30 and 2x below v1.3.3's 1000. (The ceiling itself moves by a
// factor of 33, 30.0 -> 1000.0 Mbps -- not the "two orders" an earlier draft of
// this comment and of scope.md claimed.)
static const double BW_MBPS_FLOOR = 500.0;
static const double BW_SNDPERIOD_CEIL_US = 100.0;

// Encryption. The passphrase length is API.md's "10-79 characters"; the key
// lengths are its "16, 24, 32". A marker that cannot occur in st_payload()'s
// body -- which is only 'A'..'Z' after the "MSG%08d" prefix -- so finding it in
// a forwarded datagram means the payload crossed in clear and nothing else.
static const char KM_PW[] = "live-step04-passphrase-alpha";
static const int KM_N = 64;
static const int KM_WIRE_N = 16;
static const char KM_NEEDLE[] = "cleartext-needle-0417";

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
                   "cannot tell delivery from an empty channel", which, srt_getlasterror_str());
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
static LiveXfer live_exchange_msgs(SRTSOCKET snd, SRTSOCKET rcv,
                                   const std::vector<std::string>& msgs, int gap_us,
                                   int rcv_timeo_ms) {
    LiveXfer x;
    int biggest = 0;
    for (size_t i = 0; i < msgs.size(); i++)
        if ((int)msgs[i].size() > biggest) biggest = (int)msgs[i].size();
    st_set_int(rcv, SRTO_RCVTIMEO, rcv_timeo_ms);
    const int64_t t0 = live_now_ms();
    {
        LiveJoiner rx(std::thread(live_rx_collect, rcv, (int)msgs.size(), biggest, &x.got));
        for (size_t i = 0; i < msgs.size(); i++) {
            const int rc = srt_sendmsg(snd, msgs[i].data(), (int)msgs[i].size(), -1, 1);
            ST_REQUIRE_MSG(rc == (int)msgs[i].size(),
                           "srt_sendmsg #%d of %d bytes returned %d: %s", (int)i,
                           (int)msgs[i].size(), rc, srt_getlasterror_str());
            x.sent.push_back(msgs[i]);
            if (gap_us > 0) std::this_thread::sleep_for(std::chrono::microseconds(gap_us));
        }
    }
    x.elapsed_ms = live_now_ms() - t0;
    return x;
}

static std::vector<std::string> live_uniform(int count, int size) {
    std::vector<std::string> v;
    v.reserve((size_t)count);
    for (int i = 0; i < count; i++) v.push_back(st_payload(i, size));
    return v;
}

static LiveXfer live_exchange(SRTSOCKET snd, SRTSOCKET rcv, int count, int size, int gap_us,
                              int rcv_timeo_ms) {
    return live_exchange_msgs(snd, rcv, live_uniform(count, size), gap_us, rcv_timeo_ms);
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
                       "the message that was sent, not a truncated or a coalesced one",
                       what, i, (int)x.got[(size_t)i].size(), (int)x.sent[(size_t)i].size());
        ST_REQUIRE_MSG(x.got[(size_t)i] == x.sent[(size_t)i],
                       "%s: message %d differs byte-for-byte from what was sent; the channel "
                       "delivered something, but not the payload", what, i);
    }
    const int bad = st_first_out_of_order(x.sent, x.got);
    ST_REQUIRE_MSG(bad == -1,
                   "%s: the message at position %d is not the one that was sent there; the "
                   "stream arrived, but not in the order it was sent", what, bad);
}

// The channel really carried this traffic, and the schedule really removed what
// the case says it removed. Without this, every payload assertion above could
// be satisfied by a link the relay was never in.
static void live_require_relay_did_it(const Relay* r, int expect_dropped, int min_seen,
                                      const char* what) {
    ST_REQUIRE_MSG(r->seen_fwd() >= min_seen,
                   "%s: the relay indexed only %d forward DATA packets where at least %d were "
                   "expected; the traffic did not go through the channel this case configured, "
                   "so nothing below describes the schedule it claims to", what, r->seen_fwd(),
                   min_seen);
    ST_REQUIRE_MSG(r->dropped_fwd() == expect_dropped,
                   "%s: the relay dropped %d forward DATA packets, expected exactly %d; the drop "
                   "schedule is index-based and must remove the same packets on every run",
                   what, r->dropped_fwd(), expect_dropped);
}

static void live_require_connected(const char* what, SRTSOCKET s) {
    const SRT_SOCKSTATUS st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_CONNECTED, "%s: the socket is %s (%d), expected SRTS_CONNECTED (%d)",
                   what, live_statename(st), (int)st, (int)SRTS_CONNECTED);
}

// The live transport configuration the transfer groups start from: TSBPD on, a
// latency generous enough that the relay schedule rather than the clock decides
// what arrives, and too-late drop OFF -- the precondition g11_recovery's
// description names. Both sides are configured: SRTO_TLPKTDROP is exchanged in
// the HSv5 handshake, and the sender only discards a late packet when the peer
// advertised the flag, so switching it off at the receiver is also what keeps
// the sender from dropping.
static void live_conf(SRTSOCKET s) { st_live_lossless(s); }

// =========================================================================
// g00_endtoend   (GATE; E2E_TIER=inherited -- see the header: it does not
//                 discriminate v1.3.2 from v1.3.3, is not written as if it
//                 did, and therefore earns no weight)
//
// endtoend.txt: "a live stream of 4000 messages crosses a clean relay and arrives
// byte-identical, in order, with no gaps".
//
// One claim, one case. The relay counters and the receiver's own totals are
// asserted beside the payload so that "nothing was lost" cannot be satisfied by
// a channel that never carried anything: the relay must have indexed at least
// 4000 forward DATA packets, it must have dropped none, and the receiver must
// report having received at least as many packets as there were messages.
// =========================================================================

SRT_CASE(g00_endtoend, a_live_stream_of_4000_messages_crosses_a_clean_relay_intact) {
    Link L;
    RelaySched sc;
    st_connect(L, sc, live_conf, live_conf);
    live_require_connected("the caller of the gate stream", L.cal);
    live_require_connected("the accepted socket of the gate stream", L.acc);

    const LiveXfer x =
        live_exchange(L.cal, L.acc, GATE_N, LIVE_PAYLOAD, GATE_GAP_US, GATE_RECV_MS);
    live_require_complete_stream(x, GATE_N, "4000 live messages over a clean relay");
    live_require_relay_did_it(L.relay, 0, GATE_N, "4000 live messages over a clean relay");

    const SRT_TRACEBSTATS rb = live_stats(L.acc, "receiving");
    ST_REQUIRE_MSG(rb.pktRecvTotal >= (int64_t)GATE_N,
                   "the receiver reports pktRecvTotal=%lld for %d delivered messages; the "
                   "counters must describe traffic that really crossed",
                   (long long)rb.pktRecvTotal, GATE_N);
    ST_REQUIRE_MSG(rb.pktRcvDropTotal == 0,
                   "too-late drop is disabled and the latency budget is 900 ms, but the receiver "
                   "reports pktRcvDropTotal=%d packets skipped as too late to play; a clean "
                   "relay must leave that counter at 0", rb.pktRcvDropTotal);
    // Two ceilings on observed counters -- pktRcvLossTotal and pktRetransTotal
    // <= LIVE_INCIDENTAL_MAX -- stood here and were DELETED as gate cases that
    // cannot be proved deterministic: on a box shared with nine other build
    // sessions, incidental loss on loopback is a fact about the host's load at
    // that instant, not about the library. This group is GATE, so a flake in it
    // is a zero for the whole step. What the case still asserts -- the complete,
    // in-order stream and a relay that removed nothing -- is the part that
    // describes the library. See NOTES.md, "Gate cases deleted for
    // non-determinism".
}

// =========================================================================
// g09_bstats_bw   (new -- deltas/04.md D6 and D7)
//
// scored.txt: "srt_bstats on a freshly connected socket whose bandwidth cap is
// at its default reports mbpsMaxBW above 500 and usPktSndPeriod below 100
// microseconds, and reports the same pair of bounds after the cap is set to 0".
//
// Two clauses, two cases. Every number is interface.md's; nothing here asserts
// the measured 1000.0 / 10.0 that v1.3.3 actually produces, because no shipped
// document states a figure for the ceiling.
//
// Both cases read the fields on a connection that carries no payload at all,
// and assert pktSentTotal == 0 at the moment of the read. interface.md requires
// exactly that -- "both read before any payload is sent" and "both fields must
// already be populated when the connection completes" -- and the assertion is
// what stops a tree that only fills the fields once traffic starts from passing
// by accident.
// =========================================================================

// SRTO_MAXBW is int64_t (API.md: "bytes/sec"), so srtkit's int accessors are
// the wrong width for it and it gets its own two lines here rather than a
// widened helper in a header every other family shares.
static int64_t live_get_maxbw(SRTSOCKET s) {
    int64_t v = -999;
    int n = (int)sizeof v;
    ST_REQUIRE_MSG(srt_getsockflag(s, SRTO_MAXBW, &v, &n) == 0,
                   "srt_getsockflag(SRTO_MAXBW) failed with %s (\"%s\")",
                   live_errname(srt_getlasterror(NULL)), srt_getlasterror_str());
    ST_REQUIRE_MSG(n == (int)sizeof(int64_t),
                   "srt_getsockflag(SRTO_MAXBW) returned %d bytes, expected %d: the option is an "
                   "int64_t at this milestone", n, (int)sizeof(int64_t));
    return v;
}

static void live_set_maxbw(SRTSOCKET s, int64_t v) {
    ST_REQUIRE_MSG(srt_setsockflag(s, SRTO_MAXBW, &v, sizeof v) == 0,
                   "srt_setsockflag(SRTO_MAXBW, %lld) failed with %s (\"%s\")", (long long)v,
                   live_errname(srt_getlasterror(NULL)), srt_getlasterror_str());
}

// The shared body of the -1 and 0 cases: bring a link up with `cconf` on the caller,
// read the caller's statistics before a byte of payload moves, and require the
// two bounds. `what` names the configuration so a failure line says which of
// the two clauses gave way.
static void live_require_infinite_ceiling(const SockConf& cconf, int64_t expect_maxbw,
                                          const char* what) {
    Link L;
    RelaySched sc;
    st_connect(L, sc, st_noconf, cconf);
    live_require_connected(what, L.cal);

    const int64_t cap = live_get_maxbw(L.cal);
    ST_REQUIRE_MSG(cap == expect_maxbw,
                   "%s: SRTO_MAXBW reads %lld, expected %lld; the bounds below describe the "
                   "ceiling that stands in for \"no limit\", so the cap has to be the one this "
                   "case claims it is", what, (long long)cap, (long long)expect_maxbw);

    SRT_TRACEBSTATS mon;
    std::memset(&mon, 0, sizeof mon);
    ST_REQUIRE_MSG(srt_bstats(L.cal, &mon, 1) == 0, "%s: srt_bstats failed: %s", what,
                   srt_getlasterror_str());

    ST_REQUIRE_MSG(mon.pktSentTotal == 0,
                   "%s: the socket reports pktSentTotal=%lld before the case sent anything; the "
                   "two bounds below are only about a FRESHLY connected socket",
                   what, (long long)mon.pktSentTotal);

    ST_REQUIRE_MSG(mon.mbpsMaxBW > BW_MBPS_FLOOR,
                   "%s: srt_bstats reports mbpsMaxBW=%.4f Mbps, expected above %.1f. This "
                   "milestone raises the ceiling that stands in for \"no bandwidth limit\" by "
                   "a factor of 33, 30.0 Mbps to 1000.0 Mbps; the previous milestone "
                   "reports about 30.0 here, which is what a tree that kept the old constant "
                   "looks like",
                   what, mon.mbpsMaxBW, BW_MBPS_FLOOR);

    ST_REQUIRE_MSG(mon.usPktSndPeriod > 0.0,
                   "%s: srt_bstats reports usPktSndPeriod=%.4f us on a connection that has "
                   "completed; the field must already be populated when the handshake finishes, "
                   "not left at 0 until the first packet goes out", what, mon.usPktSndPeriod);
    ST_REQUIRE_MSG(mon.usPktSndPeriod < BW_SNDPERIOD_CEIL_US,
                   "%s: srt_bstats reports usPktSndPeriod=%.4f us, expected below %.1f us. It is "
                   "the inverse of the same ceiling mbpsMaxBW reports; the previous milestone "
                   "reports about 362 us here",
                   what, mon.usPktSndPeriod, BW_SNDPERIOD_CEIL_US);
}

SRT_CASE(g09_bstats_bw, the_default_cap_reports_an_unlimited_ceiling_and_a_short_send_period) {
    live_require_infinite_ceiling(st_noconf, -1,
                                  "a freshly connected socket with SRTO_MAXBW at its -1 default");
}

// The cap set EXPLICITLY to 0, which API.md's option table binds as `pre`, so
// it is set before the connect rather than after it: this case is about the
// value of the ceiling, not about when the option may be written.
SRT_CASE(g09_bstats_bw, an_explicit_zero_cap_reports_the_same_two_bounds) {
    live_require_infinite_ceiling([](SRTSOCKET s) { live_set_maxbw(s, 0); }, 0,
                                  "a freshly connected socket with SRTO_MAXBW explicitly 0");
}

// The control that gives the two cases above their meaning: the same field, read
// the same way, on a socket whose cap is a real number rather than the
// "no limit" sentinel. mbpsMaxBW must follow the cap, which is what shows it is
// a REPORT of the ceiling in force and not a constant that happens to sit above
// 500 -- the reading the two cases above would otherwise be satisfied by.
//
// 80 Mbps is deliberately far below this milestone's 1000 Mbps ceiling and far
// above the previous milestone's 30 Mbps, so the assertion has the same content
// at both tags. scope.md licenses it: "srt_bstats() must report it as
// mbpsMaxBW, and as the corresponding send period in usPktSndPeriod", for "the
// rate the congestion controller assumes when SRTO_MAXBW is left at its -1
// default or set to 0" -- the sentence pairs the sentinel with a figure the
// option can also be given, and the field is the same field either way. A tree
// that clamped every cap up to its own ceiling, or that ignored the option when
// filling the field, fails here while passing the -1 and 0 cases.
SRT_CASE(g09_bstats_bw, the_field_tracks_an_explicit_positive_cap) {
    const int64_t CAP_BPS = 10000000;   // 10 MB/s = 80 Mbps
    const double CAP_MBPS = 80.0;

    // Pre-connect: API.md's option table binds SRTO_MAXBW as `pre`.
    Link L;
    RelaySched sc;
    st_connect(L, sc, [](SRTSOCKET s) { live_set_maxbw(s, CAP_BPS); },
               [](SRTSOCKET s) { live_set_maxbw(s, CAP_BPS); });

    const int64_t readback = live_get_maxbw(L.cal);
    ST_REQUIRE_MSG(readback == CAP_BPS,
                   "SRTO_MAXBW read back as %lld, expected the %lld that was set",
                   (long long)readback, (long long)CAP_BPS);

    const SRT_TRACEBSTATS mon = live_stats(L.cal, "caller");
    ST_REQUIRE_MSG(mon.pktSentTotal == 0,
                   "mbpsMaxBW was read after %lld packets had already been sent; the field is "
                   "required as soon as the connection completes, and this case must read it "
                   "there", (long long)mon.pktSentTotal);

    const double lo = CAP_MBPS * 0.9;
    const double hi = CAP_MBPS * 1.1;
    ST_REQUIRE_MSG(mon.mbpsMaxBW >= lo && mon.mbpsMaxBW <= hi,
                   "the socket's cap is 10 MB/s, but srt_bstats reports mbpsMaxBW=%.4f Mbps "
                   "where about %.1f was expected. The field reports the ceiling in force for "
                   "THIS socket; a tree that fills it from the \"no bandwidth limit\" "
                   "sentinel alone, or that clamps every cap up to its own ceiling, reports "
                   "the wrong number here while still reporting above 500 for -1 and 0",
                   mon.mbpsMaxBW, CAP_MBPS);
    ST_REQUIRE_MSG(mon.usPktSndPeriod > BW_SNDPERIOD_CEIL_US,
                   "the socket's cap is 10 MB/s -- well under the \"no limit\" ceiling -- but "
                   "usPktSndPeriod reports %.4f us, at or below the %.1f us the unlimited "
                   "ceiling produces: the send period is the inverse of the ceiling in force, "
                   "so a real cap must make it LARGER, not leave it at the unlimited value",
                   mon.usPktSndPeriod, BW_SNDPERIOD_CEIL_US);
}

// =========================================================================
// g10_transfer   (inherited)
//
// scored.txt: "live messages sent over an intact channel arrive complete, in
// order and byte-identical on the receiving side".
//
// One subject: what a channel that loses nothing must deliver. Three cases,
// which differ only in what is offered to the channel -- uniform payload-sized
// messages, messages of assorted sizes, and the same stream across a link that
// delays every datagram but drops none. None of them names an identifier that
// is new at this milestone: this group must run, and pass, against v1.3.2.
//
// Every case asserts the relay counters as well as the payload, so a run in
// which the traffic never went through the relay -- or in which the relay
// silently dropped something -- is a failure and not a pass.
// =========================================================================

SRT_CASE(g10_transfer, payload_sized_messages_arrive_complete_in_order_and_byte_identical) {
    Link L;
    RelaySched sc;
    st_connect(L, sc, live_conf, live_conf);

    const LiveXfer x = live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US,
                                     LIVE_CLEAN_MS);
    live_require_complete_stream(x, LIVE_N, "payload-sized messages over an intact channel");
    live_require_relay_did_it(L.relay, 0, LIVE_N, "payload-sized messages over an intact channel");

    const SRT_TRACEBSTATS rb = live_stats(L.acc, "receiving");
    ST_REQUIRE_MSG(rb.pktRecvTotal >= (int64_t)LIVE_N,
                   "the receiver reports pktRecvTotal=%lld for %d delivered messages; the "
                   "counters must describe traffic that really crossed",
                   (long long)rb.pktRecvTotal, LIVE_N);
    ST_REQUIRE_MSG(rb.pktRcvDropTotal == 0,
                   "the channel was intact but the receiver reports pktRcvDropTotal=%d packets "
                   "skipped as too late to play", rb.pktRcvDropTotal);
}

// Assorted sizes, each one a single message and each one below the live payload
// size (API.md: SRTT_LIVE "implies Message API", and one sending instruction
// carries "only so many data that fit in one UDP packet ... limited to the
// value defined first in SRTO_PAYLOADSIZE"). "Complete and byte-identical"
// means the receiver hands back exactly the message that was handed in, so a
// transport that coalesced two short messages, or padded one out to the payload
// size, fails here while passing the uniform case above.
SRT_CASE(g10_transfer, messages_of_assorted_sizes_keep_their_boundaries) {
    static const int sizes[] = {64, 137, 512, 991, 1316, 40, 1200, 33};
    const int nsizes = (int)(sizeof sizes / sizeof sizes[0]);

    Link L;
    RelaySched sc;
    st_connect(L, sc, live_conf, live_conf);

    std::vector<std::string> msgs;
    msgs.reserve((size_t)LIVE_N);
    for (int i = 0; i < LIVE_N; i++) msgs.push_back(st_payload(i, sizes[i % nsizes]));

    const LiveXfer x = live_exchange_msgs(L.cal, L.acc, msgs, LIVE_GAP_US, LIVE_CLEAN_MS);
    live_require_complete_stream(x, LIVE_N, "messages of assorted sizes over an intact channel");
    live_require_relay_did_it(L.relay, 0, LIVE_N,
                              "messages of assorted sizes over an intact channel");
}

// The same claim over a link that delays every datagram 15 ms in each
// direction and drops nothing. An "intact channel" is one that loses nothing,
// not one with no propagation time, and a transfer group that only holds at
// zero RTT is not measuring delivery.
SRT_CASE(g10_transfer, the_stream_survives_an_intact_but_delayed_link) {
    Link L;
    RelaySched sc;
    sc.delay_fwd_ms = 15;
    sc.delay_bwd_ms = 15;
    st_connect(L, sc, live_conf, live_conf);

    const LiveXfer x = live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US,
                                     LIVE_CLEAN_MS);
    live_require_complete_stream(x, LIVE_N, "an intact link with 15 ms of delay each way");
    live_require_relay_did_it(L.relay, 0, LIVE_N, "an intact link with 15 ms of delay each way");

    const SRT_TRACEBSTATS rb = live_stats(L.acc, "receiving");
    ST_REQUIRE_MSG(rb.pktRcvDropTotal == 0,
                   "a delayed but intact channel had %d packets skipped as too late to play; "
                   "with too-late drop off and 900 ms of latency against a 30 ms round trip "
                   "that counter must not move", rb.pktRcvDropTotal);
}

// =========================================================================
// g11_recovery   (inherited)
//
// scored.txt: "messages whose datagrams the relay drops at fixed indexes are
// retransmitted and arrive complete and in order once too-late packet drop is
// off and the latency budget is generous".
//
// One subject: ARQ recovery under a fixed loss schedule. The first case is the
// control that gives the rest their meaning -- it pins the counters on a
// channel with no scheduled drops, so the floors the lossy cases assert cannot
// be met by a library that reports the same number whatever happened.
//
// The schedules and the two bands are the ones stepdefs/03/fam_live.cpp
// measured across v1.3.0 and v1.3.2 (30/30, 40/41 and 36/38 reported for the
// three lossy schedules, identically at both); they are re-measured here at
// v1.3.2 and v1.3.3 rather than carried on trust. All three run on an
// undelayed relay, which is what keeps the floors deterministic -- see
// stepdefs/03/fam_live.cpp on LIVE_SCHEDULED_MIN, and NOTES.md for the
// delayed-link case that was deleted because its floor raced the end of the
// stream.
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
SRT_CASE(g11_recovery, a_clean_channel_needs_no_retransmission) {
    Link L;
    RelaySched sc;
    st_connect(L, sc, live_conf, live_conf);

    const LiveXfer x = live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US,
                                     LIVE_CLEAN_MS);
    live_require_complete_stream(x, LIVE_N, "clean channel");
    live_require_relay_did_it(L.relay, 0, LIVE_N, "clean channel");

    const SRT_TRACEBSTATS rb = live_stats(L.acc, "receiving");
    // The sender's counters are read and the result deliberately discarded.
    // Nothing in this case reads a field of them any more -- the two ceilings
    // that did were deleted, see the note below -- but live_stats() asserts
    // that srt_bstats() succeeds on the socket it is given, and that assertion
    // is worth keeping for the sending side: it is the only place this gate
    // touches srt_bstats on a caller. Binding the result to a named local is
    // what is dropped, not the call. (It was bound until this round, which
    // made the development compile line, -Wall -Wextra -Werror, fail on
    // -Wunused-but-set-variable; the shipped verifier passes neither flag and
    // built it happily, which is how the dead binding survived.)
    live_stats(L.cal, "sending");

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
}

// The subject, in its plainest form. Thirty datagrams removed at fixed indexes,
// in three clusters of ten so that each cluster is certainly payload rather
// than a lone packet at the edge of the schedule.
SRT_CASE(g11_recovery, datagrams_dropped_at_fixed_indexes_are_retransmitted) {
    Link L;
    RelaySched sc;
    for (int i = 40; i < 50; i++) sc.drop_fwd.insert(i);
    for (int i = 80; i < 90; i++) sc.drop_fwd.insert(i);
    for (int i = 120; i < 130; i++) sc.drop_fwd.insert(i);
    st_connect(L, sc, live_conf, live_conf);

    const LiveXfer x = live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US,
                                     LIVE_LOSSY_MS);

    live_require_relay_did_it(L.relay, 30, LIVE_N, "three clusters of ten dropped datagrams");
    live_require_complete_stream(x, LIVE_N, "three clusters of ten dropped datagrams");

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
SRT_CASE(g11_recovery, a_burst_of_consecutive_loss_is_recovered) {
    Link L;
    RelaySched sc;
    for (int i = 100; i < 140; i++) sc.drop_fwd.insert(i);
    st_connect(L, sc, live_conf, live_conf);

    const LiveXfer x = live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US,
                                     LIVE_LOSSY_MS);

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
SRT_CASE(g11_recovery, periodic_loss_is_recovered_for_the_whole_stream) {
    Link L;
    RelaySched sc;
    for (int i = 6; i <= 222; i += 6) sc.drop_fwd.insert(i);
    const int scheduled = (int)sc.drop_fwd.size();
    st_connect(L, sc, live_conf, live_conf);

    const LiveXfer x = live_exchange(L.cal, L.acc, LIVE_N, LIVE_PAYLOAD, LIVE_GAP_US,
                                     LIVE_LOSSY_MS);

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

// =========================================================================
// g12_encryption   (inherited)
//
// scored.txt: "a matching passphrase and explicit key length on both sides
// carry the payload intact while no clear payload appears in the datagrams on
// the wire, and both sides report the documented secured key material state".
//
// One subject: a correctly keyed live connection. Three cases. Two carry a
// stream at the two key lengths API.md lists beside the default (16 and 32)
// and require the payload intact, both KM states SRT_KM_S_SECURED and nothing
// counted as undecryptable. The third is the one that cannot be vacuous in
// either direction: it runs a marked payload through a recording tap twice,
// once without a passphrase and once with, and requires the marker to be
// visible on the wire in exactly one of the two. The clear half is the
// positive control -- it shows the tap CAN see plaintext, and that the KM
// state reads UNSECURED when nothing is keyed -- before the encrypted half is
// asked to report the absence of plaintext and the presence of SECURED.
//
// Nothing here names an identifier new at this milestone; SRTO_PASSPHRASE,
// SRTO_PBKEYLEN and the two KM-state options exist at every tag in the chain.
// =========================================================================

// Live configuration plus keying. The key length is set on BOTH sides, as the
// group's description says, and to the same value: API.md's "Bidirectional"
// guidance is that the listener's value wins if both are set, and its
// "unwanted behavior" case is two DIFFERENT values -- two equal ones are the
// arrangement it recommends for rendezvous and are unambiguous here.
static SockConf live_keyed_conf(const char* passphrase, int keylen) {
    return [passphrase, keylen](SRTSOCKET s) {
        live_conf(s);
        st_set_int(s, SRTO_PBKEYLEN, keylen);
        st_set_str(s, SRTO_PASSPHRASE, passphrase);
    };
}

// How long a key-state read will wait for SECURING to resolve before it
// reports whatever it last saw.
//
// This is a HANG DETECTOR, not a timing assertion, and the distinction is the
// reason for the size of the number. The key exchange rides in the HSv5
// handshake, so on every connection measured for this file the state is
// already SECURED on the first read after srt_connect returns and the poll
// never sleeps once: no passing run depends on this value, and no verdict is a
// function of how long anything took. What it protects against is a loaded box
// descheduling the handshake between connect and the first read, where a tight
// budget would turn a scheduling stall into a wrong answer -- in a GATE group,
// which vetoes. Ten seconds is several hundred times the measured settle time
// on loopback and still well inside the 60-second per-case alarm, so a state
// that genuinely never leaves SECURING is still reported as SECURING, by name,
// rather than as an anonymous timeout.
static const int LIVE_KMSTATE_SETTLE_MS = 10000;

// SRTO_RCVKMSTATE / SRTO_SNDKMSTATE, reported by name. The poll is bounded by
// `budget_ms` and returns the moment the state leaves SECURING.
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

static void live_require_kmstate(SRTSOCKET s, SRT_SOCKOPT which, int expect, const char* side,
                                 const char* what) {
    const int v = live_settled_kmstate(s, which, LIVE_KMSTATE_SETTLE_MS, side);
    ST_REQUIRE_MSG(v == expect,
                   "%s: %s on the %s is %s (%d), expected %s (%d)", what,
                   which == SRTO_RCVKMSTATE ? "SRTO_RCVKMSTATE" : "SRTO_SNDKMSTATE", side,
                   live_kmname(v), v, live_kmname(expect), expect);
}

// The keyed stream at one key length: the payload arrives intact through the
// relay, both sides report SECURED, and the receiver counted nothing as
// undecryptable. The last of those is what separates "the payload arrived
// because it was decrypted" from "the payload arrived because it was never
// encrypted and the KM state is a constant".
static void live_keyed_stream_intact(int keylen) {
    char what[96];
    std::snprintf(what, sizeof what, "matching passphrase with SRTO_PBKEYLEN=%d on both sides",
                  keylen);

    Link L;
    RelaySched sc;
    st_connect(L, sc, live_keyed_conf(KM_PW, keylen), live_keyed_conf(KM_PW, keylen));
    live_require_connected(what, L.cal);

    live_require_kmstate(L.acc, SRTO_RCVKMSTATE, SRT_KM_S_SECURED, "accepted socket", what);
    live_require_kmstate(L.cal, SRTO_SNDKMSTATE, SRT_KM_S_SECURED, "caller", what);

    const LiveXfer x = live_exchange(L.cal, L.acc, KM_N, LIVE_PAYLOAD, LIVE_GAP_US, LIVE_CLEAN_MS);
    live_require_complete_stream(x, KM_N, what);
    live_require_relay_did_it(L.relay, 0, KM_N, what);

    const SRT_TRACEBSTATS rb = live_stats(L.acc, "receiving");
    ST_REQUIRE_MSG(rb.pktRecvTotal >= (int64_t)KM_N,
                   "%s: the receiver reports pktRecvTotal=%lld for %d delivered messages; the "
                   "counters must describe traffic that really crossed",
                   what, (long long)rb.pktRecvTotal, KM_N);
    ST_REQUIRE_MSG(rb.pktRcvUndecryptTotal == 0,
                   "%s: srt_bstats().pktRcvUndecryptTotal is %d, expected 0; on a correctly keyed "
                   "connection every packet that arrived must have been decrypted",
                   what, rb.pktRcvUndecryptTotal);

    // Read again AFTER the stream: the state must not have decayed once real
    // payload was flowing, which is the moment a wrong or missing key shows.
    live_require_kmstate(L.acc, SRTO_RCVKMSTATE, SRT_KM_S_SECURED, "accepted socket, after the stream", what);
    live_require_kmstate(L.cal, SRTO_SNDKMSTATE, SRT_KM_S_SECURED, "caller, after the stream", what);
}

SRT_CASE(g12_encryption, a_matching_passphrase_with_a_16_byte_key_carries_the_payload_intact) {
    live_keyed_stream_intact(16);
}

SRT_CASE(g12_encryption, a_matching_passphrase_with_a_32_byte_key_carries_the_payload_intact) {
    live_keyed_stream_intact(32);
}

// ---------------------------------------------------------------------------
// A UDP relay that keeps a copy of what it forwards.
//
// relay.h's Relay exposes counters only, and this file may not modify it --
// other families depend on that header -- so the on-the-wire half of this
// group owns its own socket pair. It forwards everything, with no schedule and
// no loss, and records every datagram travelling from the caller (the sending
// side) towards the listener, which is the direction the payload takes. This
// is the same shape stepdefs/02/fam_live.cpp uses for the same purpose; it is
// restated here rather than shared because the brief allows this file no
// write outside itself.
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

// The same shape as st_connect, with the recording tap in place of the relay.
// Every assertion is placed either before the acceptor thread starts or after
// it is joined, so a failure never throws past a live thread.
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

// A payload-sized message with the needle written verbatim at a fixed offset
// past the index prefix, so that the datagram that carries it in clear carries
// the needle as one contiguous run of bytes. The rest is st_payload's pattern.
static std::string live_marked_payload(int index) {
    std::string s = st_payload(index, LIVE_PAYLOAD);
    const size_t nlen = std::strlen(KM_NEEDLE);
    std::memcpy(&s[64], KM_NEEDLE, nlen);
    return s;
}

struct WireResult {
    int delivered = 0;
    int datagrams = 0;
    int marker_hits = 0;
    int rcv_km = -1;
    int snd_km = -1;
};

// One half of the on-the-wire experiment: carry KM_WIRE_N marked messages
// across a tapped channel, keyed or not, and report how many forwarded
// datagrams carried the marker verbatim, along with the KM state each side
// reported once the stream was through.
static WireResult wire_run(bool keyed) {
    TapLink L;
    const char* what = keyed ? "the keyed half of the wire check" : "the clear half of the wire check";
    if (keyed)
        tap_connect(L, live_keyed_conf(KM_PW, 16), live_keyed_conf(KM_PW, 16));
    else
        tap_connect(L, live_conf, live_conf);
    live_require_connected(what, L.cal);

    std::vector<std::string> msgs;
    msgs.reserve((size_t)KM_WIRE_N);
    for (int i = 0; i < KM_WIRE_N; i++) msgs.push_back(live_marked_payload(i));

    const LiveXfer x = live_exchange_msgs(L.cal, L.acc, msgs, LIVE_GAP_US, LIVE_CLEAN_MS);
    live_require_complete_stream(x, KM_WIRE_N, what);

    WireResult r;
    r.delivered = (int)x.got.size();
    const std::vector<std::string> seen = L.tap->forwarded();
    r.datagrams = (int)seen.size();
    for (size_t i = 0; i < seen.size(); i++)
        if (seen[i].find(KM_NEEDLE) != std::string::npos) r.marker_hits++;
    r.rcv_km = live_settled_kmstate(L.acc, SRTO_RCVKMSTATE, LIVE_KMSTATE_SETTLE_MS, "accepted socket");
    r.snd_km = live_settled_kmstate(L.cal, SRTO_SNDKMSTATE, LIVE_KMSTATE_SETTLE_MS, "caller");
    return r;
}

// The one case that must not be vacuous in either direction. The clear half
// proves the tap sees plaintext and that the KM state reads UNSECURED when
// nothing is keyed ("no decryption", API.md); only then is the keyed half
// asked to show zero plaintext and SECURED. Without the clear half a library
// that sent nothing recognisable, or that reported SECURED as a constant,
// would pass the keyed half.
SRT_CASE(g12_encryption, the_payload_is_in_clear_on_the_wire_only_without_a_passphrase) {
    const WireResult clear = wire_run(false);
    ST_REQUIRE_MSG(clear.datagrams >= KM_WIRE_N,
                   "the tap forwarded only %d datagrams for %d unkeyed messages, expected at "
                   "least %d: the traffic did not cross the tap", clear.datagrams, KM_WIRE_N,
                   KM_WIRE_N);
    ST_REQUIRE_MSG(clear.marker_hits >= KM_WIRE_N,
                   "with no passphrase only %d of %d forwarded datagrams carried the marker in "
                   "clear, expected at least %d: the tap cannot detect plaintext and so proves "
                   "nothing about the keyed half", clear.marker_hits, clear.datagrams, KM_WIRE_N);
    ST_REQUIRE_MSG(clear.rcv_km == SRT_KM_S_UNSECURED,
                   "with no passphrase SRTO_RCVKMSTATE on the accepted socket is %s (%d), "
                   "expected SRT_KM_S_UNSECURED (%d); a state that does not read UNSECURED on "
                   "an unkeyed connection cannot be trusted to mean SECURED on a keyed one",
                   live_kmname(clear.rcv_km), clear.rcv_km, (int)SRT_KM_S_UNSECURED);
    ST_REQUIRE_MSG(clear.snd_km == SRT_KM_S_UNSECURED,
                   "with no passphrase SRTO_SNDKMSTATE on the caller is %s (%d), expected "
                   "SRT_KM_S_UNSECURED (%d)", live_kmname(clear.snd_km), clear.snd_km,
                   (int)SRT_KM_S_UNSECURED);

    const WireResult keyed = wire_run(true);
    ST_REQUIRE_MSG(keyed.delivered == KM_WIRE_N,
                   "the keyed half delivered %d of %d messages", keyed.delivered, KM_WIRE_N);
    ST_REQUIRE_MSG(keyed.datagrams >= KM_WIRE_N,
                   "the tap forwarded only %d datagrams for %d keyed messages, expected at least "
                   "%d", keyed.datagrams, KM_WIRE_N, KM_WIRE_N);
    ST_REQUIRE_MSG(keyed.marker_hits == 0,
                   "%d of %d forwarded datagrams carried the payload marker in clear even though "
                   "both sides held the same passphrase and SRTO_PBKEYLEN was 16 on both, "
                   "expected 0: the stream was delivered but it was not encrypted on the wire",
                   keyed.marker_hits, keyed.datagrams);
    ST_REQUIRE_MSG(keyed.rcv_km == SRT_KM_S_SECURED,
                   "with a matching passphrase SRTO_RCVKMSTATE on the accepted socket is %s (%d), "
                   "expected SRT_KM_S_SECURED (%d)", live_kmname(keyed.rcv_km), keyed.rcv_km,
                   (int)SRT_KM_S_SECURED);
    ST_REQUIRE_MSG(keyed.snd_km == SRT_KM_S_SECURED,
                   "with a matching passphrase SRTO_SNDKMSTATE on the caller is %s (%d), expected "
                   "SRT_KM_S_SECURED (%d)", live_kmname(keyed.snd_km), keyed.snd_km,
                   (int)SRT_KM_S_SECURED);
}
