// fam_gate.cpp -- the inherited (gate) family at SRT v1.4.0 (step 05).
//
// Two groups, both inherited, both gates:
//
//   g11_transfer    live messages cross an intact relay complete and in order
//                   at the default live payload size, and a message larger
//                   than the negotiated limit is refused without disturbing
//                   the stream
//   g12_encryption  a matching passphrase at both key lengths carries the
//                   payload intact and both KM states reach SRT_KM_S_SECURED,
//                   while a one-sided or mismatched passphrase delivers
//                   nothing and the KM state reports the condition
//
// ---------------------------------------------------------------------------
// WHY THIS FILE EXISTS, AND WHAT MUST NEVER BE ADDED TO IT
//
// A family is one translation unit and one executable.  A translation unit
// that does not compile is attributed to the driver and every group it carries
// scores zero -- including a gate, whose failure zeroes the whole step.  So a
// gate group must never share a translation unit with a name that only exists
// at this milestone.
//
// Restating such a name locally is not enough to make the sharing safe.  The
// idiom used elsewhere in this step is
//
//     static_assert(SRTO_PACKETFILTER == 60, ...);   // read the header first
//     #ifndef SRTO_PACKETFILTER                       // then restate
//     #define SRTO_PACKETFILTER ((SRT_SOCKOPT)60)
//     #endif
//
// and the fallback cannot shield the assert above it: SRTO_PACKETFILTER is an
// *enumerator*, the preprocessor cannot see enumerators, and the assert is
// guarded on SRT_VERSION_VALUE >= 1.4.0 rather than on the name.  A workspace
// that bumps SRT_VERSION_* without adding the option -- which is exactly the
// half-finished state a gate is supposed to survive -- fails to compile the
// translation unit on that static_assert, with
//
//     error: 'SRTO_PACKETFILTER' was not declared in this scope
//
// and loses the gate to a feature the gate is not about.  (Measured against a
// v1.4.0 header with the SRTO_PACKETFILTER line removed.)
//
// The fix is structural, and it is the convention steps 08 and 11 already
// follow: the gate groups live in their own translation unit that names NO
// identifier new at this milestone, so no new-milestone name can take them
// down.  Everything below is reachable at BOTH the v1.3.3 and the v1.4.0
// prefix -- SRTO_SENDER, SRTO_RCVTIMEO, SRTO_PASSPHRASE, SRTO_PBKEYLEN,
// SRTO_RCVKMSTATE, SRTO_SNDKMSTATE, SRT_KM_S_*, srt_sendmsg, srt_recvmsg,
// srt_bstats and its pktRcvUndecryptTotal field are all v1.3.3-era, and the
// v1.4.0 header still defines every one of them.
//
// DO NOT add to this file: SRTO_PACKETFILTER, SRT_REJ_*, srt_getrejectreason,
// srt_rejectreason_str, srt_rejectreason_msg, SRT_EPOLL_ENABLE_EMPTY, the
// pktRcvFilter*/pktSndFilter* statistics members, or any other name this
// milestone introduces -- not even behind a local restatement, and not even
// behind a version guard.  A new group belongs in fam_filter.cpp,
// fam_reject.cpp or fam_epoll.cpp.  The point of this translation unit is that
// there is nothing in it for a missing v1.4.0 name to break.
//
// ---------------------------------------------------------------------------
// Provenance.  The two groups and their scaffolding were moved here verbatim
// from fam_filter.cpp (g11_transfer) and fam_reject.cpp (g12_encryption), with
// only these merges, because the two donors had separately-named copies of the
// same helper: one now_ms(), one rx_collect(), one Joiner, one LIVE_PAYLOAD.
// The dlsym block that fam_reject.cpp uses to reach srt_getrejectreason was
// deliberately NOT brought along -- g12 does not need it, and it names this
// milestone's own surface.
//
// ---------------------------------------------------------------------------
// Deliberately NOT asserted, with the reason:
//
//   * Any specific error code from an oversize srt_sendmsg.  g11_transfer's
//     description is "a message larger than the negotiated limit is refused"
//     -- refused, with no code named -- and the code is controller-dependent:
//     core.cpp:5663 consults m_CongCtl->checkTransArgs() first and throws
//     MN_INVALMSGAPI (5009) for any size over m_zMaxPayloadSize, so the
//     MN_XSIZE (SRT_ELARGEMSG, 5012) throw at core.cpp:5689 is unreachable for
//     a 400000-byte call under live CC; under FileCC the base
//     SrtCongestion::checkTransArgs (congctl.h:182) returns true and 5012 is
//     live instead.  The frozen requirement names no code, so the case asserts
//     the refusal (SRT_ERROR, nothing injected into the stream, connection
//     still usable) and not a number.
//
//   * That the keyed payload is unreadable on the wire.  A fourth g12 case,
//     `the_payload_is_not_in_clear_on_the_wire_when_keyed`, was deleted in an
//     earlier round together with the recording tap it owned: its acceptor
//     thread wrote W.acc while the main thread read it with no join between,
//     and a second srt_accept on the main thread raced the same variable.
//     Measured 3 failures in 5 runs against the previous milestone's library
//     and 0 in 5 against this one -- flaky exactly where a gate's verdict
//     matters.  Do not restore it without joining the acceptor first, and not
//     in this group at all.  pktRcvUndecryptTotal == 0 after a keyed stream is
//     what separates "arrived because it was decrypted" from "arrived because
//     it was never encrypted", and that is asserted below.
//
// No case here uses a wall-clock sleep as a synchronisation primitive.  Every
// receive is bounded by SRTO_RCVTIMEO; the accept runs on its own thread
// against an epoll-watched listener with a bounded wait; the only sleeps are
// the intended pacing between sends and the bounded poll in rj_settled_kmstate.
// ---------------------------------------------------------------------------

#include "stest.h"
#include "srtkit.h"
#include "relay.h"

#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// shared scaffolding
// ---------------------------------------------------------------------------

// The documented live payload size (SRT_LIVE_DEF_PLSIZE = 1316, the default a
// fresh socket reports).  One message is one datagram at this size.  Restated
// as a literal rather than read from the header macro: this file must behave
// identically at both prefixes, and the number is what both groups were
// written against.
static const int LIVE_PAYLOAD = 1316;

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

// The receiving side of a live link: TSBPD on, latency generous enough that the
// relay's schedule rather than the clock decides what arrives, too-late drop
// off so recovery is actually lossless.
static void conf_rcv_live(SRTSOCKET s) { st_live_lossless(s); }

// The sending side of the same link.  SRTO_SENDER first: the data sender must
// declare itself for TSBPD to be negotiated at all.
static void conf_snd_live(SRTSOCKET s) {
    st_set_int(s, SRTO_SENDER, 1);
    st_live_lossless(s);
}

// ===========================================================================
// g11_transfer -- inherited
//
// TEST_INTERFACE.md §4, this group: "live messages cross an intact relay
// complete and in order at the default SRTO_PAYLOADSIZE for live mode, and a message larger than the
// negotiated limit is refused"
// ===========================================================================

SRT_CASE(g11_transfer, live_messages_cross_intact_relay_complete_and_in_order) {
    // The inherited shape: messages at the default live payload size cross a
    // relay that drops and delays nothing, byte-identical and in order.
    const int N = 400;
    Link L;
    RelaySched sc;
    st_connect(L, sc, conf_rcv_live, conf_snd_live);

    Xfer x = st_exchange(L, N, LIVE_PAYLOAD, 1000, 8000);
    require_complete_stream(x, N, "live_messages_cross");
    ST_REQUIRE_EQ_INT(st_first_out_of_order(x.sent, x.got), -1);

    for (int i = 0; i < N; i++)
        ST_REQUIRE_MSG((int)x.got[(size_t)i].size() == LIVE_PAYLOAD,
                       "message %d arrived with %d bytes, sent %d", i,
                       (int)x.got[(size_t)i].size(), LIVE_PAYLOAD);

    ST_REQUIRE_MSG(L.relay->seen_fwd() >= N,
                   "relay saw only %d forward data packets for %d messages", L.relay->seen_fwd(), N);
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 0, "intact relay dropped %d forward data packets",
                   L.relay->dropped_fwd());
}

SRT_CASE(g11_transfer, oversize_message_is_refused_not_truncated) {
    // A message far larger than anything the connection negotiated is refused
    // outright: srt_sendmsg returns SRT_ERROR, nothing of it reaches the
    // receiver, and the connection carries the messages sent afterwards.
    //
    // Deliberately no error CODE.  TEST_INTERFACE.md §4 says "refused"; the
    // code that comes back is controller-dependent (5009 under live CC via
    // LiveCC::checkTransArgs, 5012 under FileCC via the base SrtCongestion::
    // checkTransArgs), so pinning either would assert an implementation detail
    // the requirement does not name.  See the header note.
    const int HALF = 6;
    Link L;
    RelaySched sc;
    st_connect(L, sc, conf_rcv_live, conf_snd_live);

    std::vector<std::string> sent, got;
    st_set_int(L.acc, SRTO_RCVTIMEO, 6000);
    int send_refused = -1;
    std::string refusal_msg;
    {
        Joiner rx(std::thread(rx_collect, L.acc, 2 * HALF, LIVE_PAYLOAD, &got));

        for (int i = 0; i < HALF; i++) {
            std::string m = st_payload(i, LIVE_PAYLOAD);
            ST_REQUIRE_MSG(srt_sendmsg(L.cal, m.data(), (int)m.size(), -1, 1) == (int)m.size(),
                           "priming message %d rejected: %s", i, srt_getlasterror_str());
            sent.push_back(m);
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
        }

        // Comfortably past the negotiated payload limit for a live channel.
        std::string huge(400000, 'x');
        send_refused = srt_sendmsg(L.cal, huge.data(), (int)huge.size(), -1, 1);
        int err = srt_getlasterror(nullptr);
        ST_REQUIRE_MSG(send_refused == SRT_ERROR,
                       "srt_sendmsg of %d bytes returned %d; a message larger than the "
                       "negotiated limit must be refused, not accepted or truncated",
                       (int)huge.size(), send_refused);
        refusal_msg = srt_strerror(err, 0);

        for (int i = HALF; i < 2 * HALF; i++) {
            std::string m = st_payload(i, LIVE_PAYLOAD);
            ST_REQUIRE_MSG(srt_sendmsg(L.cal, m.data(), (int)m.size(), -1, 1) == (int)m.size(),
                           "message %d after the refusal (%s) was rejected: %s -- the oversize "
                           "refusal must leave the connection usable",
                           i, refusal_msg.c_str(), srt_getlasterror_str());
            sent.push_back(m);
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
        }
    }

    ST_REQUIRE_MSG((int)got.size() == 2 * HALF,
                   "received %d of %d messages around the refusal (%s): the refused message "
                   "must not contribute anything to the stream",
                   (int)got.size(), 2 * HALF, refusal_msg.c_str());
    for (int i = 0; i < 2 * HALF; i++) {
        int idx = msg_index(got[(size_t)i]);
        ST_REQUIRE_MSG(idx == i,
                       "position %d carries message index %d (expected %d); a fragment of the "
                       "refused message may have been injected",
                       i, idx, i);
        ST_REQUIRE_MSG(got[(size_t)i] == sent[(size_t)i], "message %d differs from what was sent",
                       i);
        ST_REQUIRE_MSG((int)got[(size_t)i].size() == LIVE_PAYLOAD,
                       "message %d arrived with %d bytes, sent %d", i,
                       (int)got[(size_t)i].size(), LIVE_PAYLOAD);
    }
}

// =========================================================================
// The keyed link, moved here with g12_encryption.
// =========================================================================

static const char* rj_errname(int e) {
    switch (e) {
    case SRT_SUCCESS: return "SRT_SUCCESS";
    case SRT_ECONNREJ: return "SRT_ECONNREJ";
    case SRT_EINVPARAM: return "SRT_EINVPARAM";
    case SRT_EINVSOCK: return "SRT_EINVSOCK";
    case SRT_ETIMEOUT: return "SRT_ETIMEOUT";
    default: return "?";
    }
}

static const char* rj_statename(int v) {
    switch (v) {
    case SRT_KM_S_UNSECURED: return "SRT_KM_S_UNSECURED";
    case SRT_KM_S_SECURING: return "SRT_KM_S_SECURING";
    case SRT_KM_S_SECURED: return "SRT_KM_S_SECURED";
    case SRT_KM_S_NOSECRET: return "SRT_KM_S_NOSECRET";
    case SRT_KM_S_BADSECRET: return "SRT_KM_S_BADSECRET";
    default: return "?";
    }
}

// ------------------------------------------------------------------ the link

// listener -> relay -> caller, with the accept on its own thread.
//
// This is NOT st_try_connect.  One half of g12 is a *refused* handshake, and
// st_try_connect's contract is the opposite one: it loops the caller on
// srt_recvmsg after the handshake and reports a receive timeout when there is
// no connection, which is precisely the outcome that cannot be told apart from
// a hang.  Here every connection is either
//
//   * refused -- srt_connect returns SRT_ERROR with SRT_ECONNREJ, the accept
//     is not ready, and the case reads the KM state; or
//   * established -- both ends come up and the case then exchanges payload.
//
// The accept runs on its own thread and is woken by an epoll wait rather than
// by the accept's own timeout.  Both halves of that sentence were measured:
//
//   * WITH a blocking srt_accept and a refused handshake, nothing wakes the
//     acceptor -- the listener never becomes readable, no socket is ever
//     acceptable, and the call sits there past the case deadline.  The refusal
//     case then reports "timed out after 60 s", which is the one verdict that
//     cannot be told apart from a genuine hang.  So the thread cannot be parked
//     inside srt_accept.
//   * WITH the listener in non-blocking mode (SRTO_RCVSYN=0), the accepted
//     socket inherits that mode, and every srt_recvmsg here returns before the
//     data is there.  The keyed cases then delivered 0 of 64 messages while
//     srt_bstats reported all 64 packets received and none undecryptable -- a
//     harness defect wearing the costume of a library defect, which is exactly
//     the shape a reviewer cannot debug.  So the listener must stay blocking,
//     and only the wait is bounded.
//
// Hence: a blocking listener, a non-blocking wait for readiness, and an accept
// that is only called once the EID says a connection is pending.  The caller's
// own expiry is separate: srt_connect returns on its own when the listener
// refuses, and its return value is what the cases assert on.
//
// The acceptor's patience is tied to the CALLER, not to the clock.
//
// The acceptor runs concurrently with srt_connect, so a single bounded
// srt_epoll_wait had to cover the whole handshake, and that is a race: on a
// loaded host the handshake outran the budget and the case then reported
// "srt_connect succeeded but nothing was accepted", which is a statement about
// this machine rather than about the library.  Observed on the 05:08Z oracle
// ladder at step 06, and in this family at step 05 under the same shape.
//
// So the wait is a loop of short slices with two exits: the listener signals
// (accept and stop), or the connect attempt has returned AND a further
// RJ_ACCEPT_GRACE_MS ms has passed with the listener still silent.  While the
// connect is still in flight the acceptor simply keeps waiting -- srt_connect
// is itself bounded by SRTO_CONNTIMEO and the whole case by the harness
// deadline, so there is nothing here for an unbounded wait to hang on.  The
// refusal case stays quick, because a refused connect returns promptly and only
// the grace is added; the accepted cases get as long as the handshake takes.
static const int RJ_ACCEPT_GRACE_MS = 3000;
static const int RJ_ACCEPT_SLICE_MS = 100;

struct RjLink {
    SRTSOCKET lsn = SRT_INVALID_SOCK;
    SRTSOCKET cal = SRT_INVALID_SOCK;
    SRTSOCKET acc = SRT_INVALID_SOCK;
    Relay* relay = nullptr;
    int rc = -1;       // what srt_connect returned
    int err = 0;       // srt_getlasterror() at that moment
    int elapsed_ms = 0;
    int epoll_rc = -1;  // ready descriptors the acceptor's wait saw, or -1
    std::atomic<bool> connect_done{false};  // set once srt_connect has returned
    std::thread worker;

    RjLink() {}
    RjLink(const RjLink&) = delete;
    RjLink& operator=(const RjLink&) = delete;
    ~RjLink() { close(); }

    void close() {
        if (worker.joinable()) worker.join();
        if (acc != SRT_INVALID_SOCK) { srt_close(acc); acc = SRT_INVALID_SOCK; }
        if (cal != SRT_INVALID_SOCK) { srt_close(cal); cal = SRT_INVALID_SOCK; }
        if (lsn != SRT_INVALID_SOCK) { srt_close(lsn); lsn = SRT_INVALID_SOCK; }
        delete relay;
        relay = nullptr;
    }

    // Listener up, relay up, acceptor thread armed. Everything that can throw
    // has thrown before this returns, and the thread is joined by rj_finish
    // before any assertion runs.
    void begin(const SockConf& lconf, int rcvtimeo_ms) {
        lsn = st_new_socket();
        ST_REQUIRE_MSG(lsn != SRT_INVALID_SOCK, "srt_socket (listener): %s",
                       srt_getlasterror_str());
        st_set_int(lsn, SRTO_RCVTIMEO, rcvtimeo_ms);
        lconf(lsn);

        sockaddr_in la = st_loopback(0);
        ST_REQUIRE_MSG(srt_bind(lsn, (sockaddr*)&la, sizeof la) != SRT_ERROR, "srt_bind: %s",
                       srt_getlasterror_str());
        int nl = (int)sizeof la;
        ST_REQUIRE_MSG(srt_getsockname(lsn, (sockaddr*)&la, &nl) != SRT_ERROR,
                       "srt_getsockname: %s", srt_getlasterror_str());
        ST_REQUIRE_MSG(srt_listen(lsn, 1) != SRT_ERROR, "srt_listen: %s",
                       srt_getlasterror_str());

        RelaySched sc;
        relay = new Relay(ntohs(la.sin_port), sc);

        cal = st_new_socket();
        ST_REQUIRE_MSG(cal != SRT_INVALID_SOCK, "srt_socket (caller): %s",
                       srt_getlasterror_str());
        caller_addr = st_loopback(relay->port());
    }
    sockaddr_in caller_addr;
};

// Accept on the armed listener, on its own thread, then connect and join. See
// the note above the struct for why the wait is an EID and not the accept's own
// timeout. The thread is joined here, so no assertion can unwind past a live
// thread.
static void rj_finish(RjLink& L) {
    const SRTSOCKET lsn = L.lsn;
    L.worker = std::thread([&L, lsn] {
        const int eid = srt_epoll_create();
        if (eid == SRT_INVALID_SOCK) return;
        int in = (int)SRT_EPOLL_IN;
        if (srt_epoll_add_usock(eid, lsn, &in) != SRT_SUCCESS) {
            srt_epoll_release(eid);
            return;
        }
        // A slice returning 0 means "nothing has come yet"; a value above zero
        // is the only thing that makes srt_accept below safe to call on a
        // refused handshake, and L.acc stays SRT_INVALID_SOCK either way.
        long long settled_at = -1;
        for (;;) {
            SRTSOCKET rd[4];
            int rn = 4;
            L.epoll_rc =
                srt_epoll_wait(eid, rd, &rn, NULL, NULL, RJ_ACCEPT_SLICE_MS, NULL, NULL, NULL,
                               NULL);
            if (L.epoll_rc > 0) {
                L.acc = srt_accept(lsn, NULL, NULL);
                break;
            }
            if (!L.connect_done) continue;
            if (settled_at < 0) settled_at = (long long)now_ms();
            if ((long long)now_ms() - settled_at >= RJ_ACCEPT_GRACE_MS) break;
        }
        srt_epoll_release(eid);
    });

    const long long t0 = (long long)now_ms();
    L.rc = srt_connect(L.cal, (sockaddr*)&L.caller_addr, sizeof L.caller_addr);
    L.err = srt_getlasterror(nullptr);
    L.elapsed_ms = (int)((long long)now_ms() - t0);
    L.connect_done = true;
    L.worker.join();
}

// The whole conversation: arm the listener, key the caller, connect, accept.
static int rj_run(RjLink& L, const SockConf& lconf, const SockConf& cconf) {
    L.begin(lconf, 20000);
    cconf(L.cal);
    rj_finish(L);
    return L.rc;
}

// A refusal is asserted on three things at once: srt_connect failed, the
// library named it SRT_ECONNREJ (a refused handshake, not a timeout or a bind
// error), and the listener accepted nothing.
static void rj_require_refused(const RjLink& L, const char* what) {
    ST_REQUIRE_MSG(L.rc == SRT_ERROR, "%s: srt_connect returned %d, expected failure: %s", what,
                   L.rc, srt_strerror(L.err, 0));
    ST_REQUIRE_MSG(L.err == SRT_ECONNREJ,
                   "%s: srt_connect failed with %s (%d), expected %s (%d, \"%s\") -- the case "
                   "is about a *rejected* handshake, and no other failure has a rejection "
                   "reason to read",
                   what, rj_errname(L.err), L.err, rj_errname(SRT_ECONNREJ), (int)SRT_ECONNREJ,
                   srt_strerror(SRT_ECONNREJ, 0));
    ST_REQUIRE_MSG(L.acc == SRT_INVALID_SOCK,
                   "%s: the listener accepted a socket for a handshake that srt_connect "
                   "reported as refused; there is no consistent connection to describe",
                   what);
}

// The live-transport defaults every keyed connection starts from.
static void rj_live(SRTSOCKET s) { st_live_lossless(s); }

// =========================================================================
// g12_encryption   (inherited)
//
// TEST_INTERFACE.md §3, this group: "a matching passphrase at both key
// lengths carries the payload intact, SRTO_RCVKMSTATE on the receiver and
// SRTO_SNDKMSTATE on the sender reach SRT_KM_S_SECURED, and a one-sided or
// mismatched passphrase delivers nothing to the application while the KM
// state reports the condition".
//
// Inherited from milestone 04. This group must pass at the previous
// milestone's tag as well as at this one, so it names only API elements
// present at both tags -- SRTO_PASSPHRASE, SRTO_PBKEYLEN, SRTO_RCVKMSTATE,
// SRTO_SNDKMSTATE, SRT_KM_S_*. All of those are v1.3.3-era; the v1.4.0 header
// still defines them.
//
// The KM-state pair is SRTO_SNDKMSTATE, "the state of the encryption at the
// peer side", and SRTO_RCVKMSTATE, "at the agent side".
// =========================================================================

static const int RJ_KM_N = 64;
static const int RJ_KM_GAP_US = 2000;
static const int RJ_KM_CLEAN_MS = 8000;
static const char RJ_KM_PW[] = "reject-05-passphrase";

// SRTO_RCVKMSTATE / SRTO_SNDKMSTATE, polled until the state leaves SECURING.
// The key exchange rides in the handshake, so the first read after srt_connect
// is normally already settled; the poll exists so that a slow handshake on a
// loaded box is a late answer rather than a wrong one.
static int rj_settled_kmstate(SRTSOCKET s, SRT_SOCKOPT which, int budget_ms, const char* what) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
    for (;;) {
        int v = -1;
        int n = (int)sizeof v;
        srt_clearlasterror();
        ST_REQUIRE_MSG(srt_getsockflag(s, which, &v, &n) == 0,
                       "%s: srt_getsockflag(opt %d) failed: %s", what, (int)which,
                       srt_getlasterror_str());
        if (v != SRT_KM_S_SECURING) return v;
        if (std::chrono::steady_clock::now() >= deadline) return v;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

static void rj_require_kmstate(SRTSOCKET s, SRT_SOCKOPT which, int expect, const char* side,
                               const char* what) {
    const int v = rj_settled_kmstate(s, which, 2000, side);
    ST_REQUIRE_MSG(v == expect, "%s: %s on the %s is %s (%d), expected %s (%d)", what,
                   which == SRTO_RCVKMSTATE ? "SRTO_RCVKMSTATE" : "SRTO_SNDKMSTATE", side,
                   rj_statename(v), v, rj_statename(expect), expect);
}

static SockConf rj_keyed_conf(const char* passphrase, int keylen) {
    return [passphrase, keylen](SRTSOCKET s) {
        rj_live(s);
        st_set_int(s, SRTO_PBKEYLEN, keylen);
        st_set_str(s, SRTO_PASSPHRASE, passphrase);
    };
}

// One stream across an established link: the receiver runs concurrently on its
// own thread bounded by SRTO_RCVTIMEO, the sender on this one. A send-then-
// receive shape would park the whole stream in the receive buffer and test
// buffering rather than delivery.
static std::vector<std::string> rj_exchange(SRTSOCKET snd, SRTSOCKET rcv, int count, int size,
                                            int gap_us, int rcv_timeo_ms) {
    std::vector<std::string> got;
    st_set_int(rcv, SRTO_RCVTIMEO, rcv_timeo_ms);
    {
        Joiner rx(std::thread(rx_collect, rcv, count, size, &got));
        for (int i = 0; i < count; i++) {
            std::string m = st_payload(i, size);
            const int rc = srt_sendmsg(snd, m.data(), (int)m.size(), -1, 1);
            ST_REQUIRE_MSG(rc == (int)m.size(), "srt_sendmsg #%d returned %d: %s", i, rc,
                           srt_getlasterror_str());
            if (gap_us > 0) std::this_thread::sleep_for(std::chrono::microseconds(gap_us));
        }
    }
    return got;
}

static void rj_require_intact(const std::vector<std::string>& got, int count, int size,
                              const char* what) {
    ST_REQUIRE_MSG((int)got.size() == count,
                   "%s: %d of %d messages were delivered; the stream must arrive complete", what,
                   (int)got.size(), count);
    std::vector<std::string> sent;
    for (int i = 0; i < count; i++) sent.push_back(st_payload(i, size));
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) == -1,
                   "%s: the delivered stream is not the sent one in order", what);
}

// A matching passphrase on both sides at one key length: the payload arrives
// intact, both KM states reach SECURED, and nothing was counted as
// undecryptable. The last of those is what separates "the payload arrived
// because it was decrypted" from "the payload arrived because it was never
// encrypted and the KM state is a constant".
static void rj_keyed_intact(int keylen) {
    char what[128];
    std::snprintf(what, sizeof what,
                  "matching passphrase with SRTO_PBKEYLEN=%d on both sides", keylen);

    RjLink L;
    rj_run(L, rj_keyed_conf(RJ_KM_PW, keylen), rj_keyed_conf(RJ_KM_PW, keylen));
    ST_REQUIRE_MSG(L.rc != SRT_ERROR, "%s: srt_connect: %s", what, srt_strerror(L.err, 0));
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK, "%s: srt_accept: %s", what, srt_getlasterror_str());

    rj_require_kmstate(L.acc, SRTO_RCVKMSTATE, SRT_KM_S_SECURED, "accepted socket", what);
    rj_require_kmstate(L.cal, SRTO_SNDKMSTATE, SRT_KM_S_SECURED, "caller", what);

    const std::vector<std::string> got =
        rj_exchange(L.cal, L.acc, RJ_KM_N, LIVE_PAYLOAD, RJ_KM_GAP_US, RJ_KM_CLEAN_MS);
    rj_require_intact(got, RJ_KM_N, LIVE_PAYLOAD, what);

    SRT_TRACEBSTATS rb;
    std::memset(&rb, 0, sizeof rb);
    ST_REQUIRE_MSG(srt_bstats(L.acc, &rb, 0) != SRT_ERROR, "%s: srt_bstats: %s", what,
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(rb.pktRcvUndecryptTotal == 0,
                   "%s: srt_bstats().pktRcvUndecryptTotal is %d, expected 0; on a correctly "
                   "keyed connection every packet that arrived must have been decrypted",
                   what, rb.pktRcvUndecryptTotal);

    // Read again after the stream: the state must not decay once real payload
    // was flowing, which is the moment a wrong or missing key shows.
    rj_require_kmstate(L.acc, SRTO_RCVKMSTATE, SRT_KM_S_SECURED,
                       "accepted socket, after the stream", what);
    rj_require_kmstate(L.cal, SRTO_SNDKMSTATE, SRT_KM_S_SECURED, "caller, after the stream",
                       what);
}

SRT_CASE(g12_encryption, a_matching_passphrase_with_a_16_byte_key_carries_the_payload_intact) {
    // First clause, at the default key length.
    rj_keyed_intact(16);
}

SRT_CASE(g12_encryption, a_matching_passphrase_with_a_32_byte_key_carries_the_payload_intact) {
    // First clause, at the other key length the API lists. Two key
    // lengths rather than one, because a library that ignored SRTO_PBKEYLEN
    // entirely would satisfy a single-length case.
    rj_keyed_intact(32);
}

SRT_CASE(g12_encryption, a_one_sided_or_mismatched_passphrase_delivers_nothing) {
    // Last clause: "a one-sided or mismatched passphrase delivers
    // nothing to the application while the KM state reports the condition".
    //
    // The strongest form of "delivers nothing" is that there is no connection
    // to deliver over, and that is what both prefixes in this chain do: the
    // enforcement setting defaults to on, so a one-sided or mismatched
    // passphrase is a fatal handshake disagreement. The refusal is ASSERTED,
    // not branched on. An earlier shape of this case skipped the rest of the
    // loop when the handshake was refused, which is what the library actually
    // does at both tags -- so the case ran to completion having executed no
    // assertion at all and could not fail. Nothing below is conditional.
    //
    // The KM state is read off the caller after the refusal, which is where
    // "the KM state reports the condition" is observable when no socket was
    // ever accepted.
    struct Case {
        const char* what;
        SockConf lconf;
        SockConf cconf;
    };
    Case cs[2];
    cs[0].what = "a one-sided passphrase";
    cs[0].lconf = rj_live;
    cs[0].cconf = rj_keyed_conf(RJ_KM_PW, 16);
    cs[1].what = "a mismatched passphrase";
    cs[1].lconf = rj_keyed_conf("0123456789abcdef", 16);
    cs[1].cconf = rj_keyed_conf(RJ_KM_PW, 16);

    for (int i = 0; i < 2; i++) {
        RjLink L;
        rj_run(L, cs[i].lconf, cs[i].cconf);
        rj_require_refused(L, cs[i].what);

        const int rcv = rj_settled_kmstate(L.cal, SRTO_RCVKMSTATE, 2000, cs[i].what);
        ST_REQUIRE_MSG(rcv != SRT_KM_S_SECURED,
                       "%s: the handshake was refused but the caller reports SRTO_RCVKMSTATE "
                       "= %s, which claims a secured key exchange that did not happen",
                       cs[i].what, rj_statename(rcv));
        ST_REQUIRE_MSG(rcv != SRT_KM_S_SECURING,
                       "%s: the caller's SRTO_RCVKMSTATE is still %s two seconds after the "
                       "handshake was refused; the state must settle on the condition, not "
                       "stay mid-exchange",
                       cs[i].what, rj_statename(rcv));
    }
}
