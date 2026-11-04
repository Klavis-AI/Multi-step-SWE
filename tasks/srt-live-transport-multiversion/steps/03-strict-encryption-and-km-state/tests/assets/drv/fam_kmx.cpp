// Family "kmx", checkpoint 03 (SRT v1.3.2). Five scored groups:
//
//   g01_strictenc    new        SRTO_STRICTENC: default, accepted values,
//                               `pre` binding, and the v1.3.0 outcome that
//                               setting it to 0 restores.
//   g02_caller_pw    new        strict enforcement when only the CALLER has a
//                               passphrase.
//   g03_listener_pw  new        strict enforcement when only the LISTENER has
//                               a passphrase.
//   g04_mismatch     new        strict enforcement when the two passphrases
//                               differ.
//   g07_keylen32     inherited  milestone-01 encryption at SRTO_PBKEYLEN=32:
//                               the payload crosses intact and never appears
//                               in clear on the wire.
//
// ----------------------------------------------------------------- tiers
//
// g07_keylen32 is `inherited`: it exists to be earned by a candidate that
// shipped v1.3.0 and did nothing for this milestone. A family is one .cpp
// compiled once, so this file must COMPILE against the v1.3.0 header, where
// SRTO_STRICTENC does not exist. See the restatement below; without it the
// whole family would fail to build there and g07 would score zero for a
// compile reason rather than a behavioural one. g07's own cases therefore
// never name SRTO_STRICTENC at all: matching passphrases connect under strict
// mode exactly as they do without it, so the group needs nothing from this
// milestone.
//
// ---------------------------------------------------------- traceability
//
// Every assertion comes from the step's own definition (stepdefs/03/scope.md
// and interface.md, restated by instruction.md §1) -- docs/API.md and
// docs/encryption.md at v1.3.2 document SRTO_STRICTENC nowhere -- or, for
// g07, from srt-docs/ on SRTO_PASSPHRASE / SRTO_PBKEYLEN, which are
// milestone-01 surface.
//
// Deliberately NOT asserted, because nothing candidate-facing states them:
//   * which SIDE reports the rejection. At v1.3.2 the listener refuses in
//     interpretSrtHandshake and answers `rsp(REJECT): 1002`; the caller only
//     ever sees SRT_ECONNREJ, and that is all these groups assert.
//   * any KM state on the REJECTED sockets. The handshake never completed, so
//     there is no negotiated state to read and no document says what an
//     unconnected socket should report.
//   * key rotation (deltas/03.md: "Key rotation is not publicly observable").
//   * SRTO_STRICTENC's effect on an already-established connection. The option
//     is `pre`-bound, so the question cannot arise.
//   * SRT_LOGFA_HAICRYPT (deltas/03.md D8, confidence "weak"): its existence
//     is compile-time only, and a compile-time-only discriminator cannot be
//     asserted from a file that must also compile at v1.3.0 -- which every
//     family in this step must, each carrying a gate group. interface.md says
//     so in as many words, so the document does not promise a check that does
//     not exist; step 04 is where the macro is actually asserted.
#include "stest.h"
#include "srtkit.h"
#include "relay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// ------------------------------------------------ option constant, restated
//
// DRIVER_RULES, "Naming an identifier the candidate may not have", rule 2.
// SRTO_STRICTENC is new at v1.3.2 and this family must compile at v1.3.0 for
// g07_keylen32's sake, so the name is restated here -- AFTER <srt/srt.h> has
// been pulled in by stest.h, so that at v1.3.2 the macro shadows an
// enumerator of identical value instead of rewriting the header's own
// declaration.
//
// The value is IMPLICIT in v1.3.2's srt.h (`SRTO_STRICTENC,` with no `= N`),
// the last member after the explicit SRTO_SNDKMSTATE = 40: RCVKMSTATE 41,
// LOSSMAXTTL 42, RCVLATENCY 43, PEERLATENCY 44, MINVERSION 45, STREAMID 46,
// SMOOTHER 47, MESSAGEAPI 48, PAYLOADSIZE 49, TRANSTYPE 50, KMREFRESHRATE 51,
// KMPREANNOUNCE 52, STRICTENC 53. Rule 2 permits restating an implicit value
// only when the enum carries no preprocessor conditionals, because a
// conditional member would give the same name different numbers in different
// build configurations. v1.3.2's SRT_SOCKOPT has no `#if` of any kind -- the
// conditional members arrive with the bonding options at v1.4.2 -- and the
// static_assert below re-derives the number from the header wherever the
// header has it, so this is checked rather than assumed. At v1.3.0 the guard
// is inert and the #define supplies the number that library rejects, which is
// exactly what the `new` groups mean to observe.
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 3, 2)
static_assert(SRTO_STRICTENC == 53, "restated SRTO_STRICTENC disagrees with <srt/srt.h>");
#endif

#ifndef SRTO_STRICTENC
#define SRTO_STRICTENC ((SRT_SOCKOPT)53)
#endif

// ----------------------------------------------------------------- sizes
//
// In live mode srt_recvmsg refuses a buffer smaller than the negotiated
// payload size (congctl.cpp: "buffer size: N is too small for the maximum
// possible 1316") and returns an error, so a group that receives into a small
// buffer sees an empty channel and cannot tell that from real loss.
// st_recv_stream allocates size + 64, so the message size must be at least
// 1252. 1300 clears that and still fits one datagram.
static const int KMX_MSG = 1300;

// Messages per transfer. Enough that "everything arrived" is a real claim and
// small enough that a case costs about one TSBPD latency, not a second of
// streaming.
static const int KMX_COUNT = 12;

// Pacing between sends, microseconds: st_send_stream's gap, so a live-mode
// rate limiter is never the reason a message is missing.
static const int KMX_GAP_US = 2000;

// How long a receive that is EXPECTED to yield nothing is given before it is
// called empty. Bounded by SRTO_RCVTIMEO inside st_recv_stream, never slept.
static const int KMX_EMPTY_MS = 1500;

// How long a receive that is expected to yield everything is given. One TSBPD
// latency (st_live_lossless sets 900 ms) plus a wide margin.
static const int KMX_FULL_MS = 4000;

static const char KMX_PW_A[] = "kmx-step03-passphrase-alpha";
static const char KMX_PW_B[] = "kmx-step03-passphrase-bravo";

// --------------------------------------------------------------- naming

static const char* kmx_errname(int e) {
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

static const char* kmx_statename(SRT_SOCKSTATUS s) {
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

static const char* kmx_kmname(int v) {
    switch (v) {
    case SRT_KM_S_UNSECURED: return "SRT_KM_S_UNSECURED";
    case SRT_KM_S_SECURING:  return "SRT_KM_S_SECURING";
    case SRT_KM_S_SECURED:   return "SRT_KM_S_SECURED";
    case SRT_KM_S_NOSECRET:  return "SRT_KM_S_NOSECRET";
    case SRT_KM_S_BADSECRET: return "SRT_KM_S_BADSECRET";
    default:                 return "<other SRT_KM_STATE>";
    }
}

// ------------------------------------------------- option access helpers

static SRTSOCKET kmx_fresh_socket() {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    return s;
}

static void kmx_set_must_pass(SRTSOCKET s, SRT_SOCKOPT o, int v, const char* what) {
    srt_clearlasterror();
    int rc = st_try_set_int(s, o, v);
    ST_REQUIRE_MSG(rc == 0,
                   "%s: srt_setsockflag(opt %d, %d) returned %d with %s (\"%s\"); expected 0",
                   what, (int)o, v, rc, kmx_errname(srt_getlasterror(NULL)),
                   srt_getlasterror_str());
}

static void kmx_set_must_fail(SRTSOCKET s, SRT_SOCKOPT o, int v, int want, const char* what) {
    srt_clearlasterror();
    int rc = st_try_set_int(s, o, v);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "%s: srt_setsockflag(opt %d, %d) returned %d (the set was accepted); "
                   "expected -1 with %s (%d)",
                   what, (int)o, v, rc, kmx_errname(want), want);
    int e = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(e == want,
                   "%s: srt_setsockflag(opt %d, %d) was refused with %s (%d, \"%s\"); "
                   "expected %s (%d)",
                   what, (int)o, v, kmx_errname(e), e, srt_getlasterror_str(),
                   kmx_errname(want), want);
}

// SRTO_KMSTATE / SRTO_RCVKMSTATE / SRTO_SNDKMSTATE, reported by name.
static int kmx_kmstate(SRTSOCKET s, SRT_SOCKOPT which, const char* what) {
    int v = -1;
    int n = (int)sizeof v;
    srt_clearlasterror();
    ST_REQUIRE_MSG(srt_getsockflag(s, which, &v, &n) == 0,
                   "%s: srt_getsockflag(opt %d) failed with %s (\"%s\")", what, (int)which,
                   kmx_errname(srt_getlasterror(NULL)), srt_getlasterror_str());
    return v;
}

// A group waits on the OBSERVED keying state and never on a timer
// (interface.md, "The key material travels in the handshake"). This is a
// bounded poll: it returns the moment the state leaves SECURING, and at worst
// costs `budget_ms`. At this release the key material rides the handshake, so
// the state is already settled when srt_connect returns and this returns on the
// first read; the poll is here so that an implementation which settles a moment
// later is not failed for the timing, not because a wait is expected.
static int kmx_settled_kmstate(SRTSOCKET s, SRT_SOCKOPT which, int budget_ms, const char* what) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
    int v = kmx_kmstate(s, which, what);
    while (v == SRT_KM_S_SECURING && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        v = kmx_kmstate(s, which, what);
    }
    return v;
}

static int kmx_undecrypted(SRTSOCKET s) {
    SRT_TRACEBSTATS st;
    std::memset(&st, 0, sizeof st);
    ST_REQUIRE_MSG(srt_bstats(s, &st, 0) == 0, "srt_bstats: %s", srt_getlasterror_str());
    return st.pktRcvUndecryptTotal;
}

static int64_t kmx_pkt_recv(SRTSOCKET s) {
    SRT_TRACEBSTATS st;
    std::memset(&st, 0, sizeof st);
    ST_REQUIRE_MSG(srt_bstats(s, &st, 0) == 0, "srt_bstats: %s", srt_getlasterror_str());
    return (int64_t)st.pktRecvTotal;
}

// ------------------------------------------------------------- the wire tap
//
// g07 has to show the payload is not readable on the wire, and relay.h -- which
// this family must not edit, other families depend on it -- forwards datagrams
// without exposing their bytes. KmxTap is therefore local to this file: the
// same loopback UDP proxy shape as Relay, with no schedule (this group wants a
// clean channel) and one addition, a byte-for-byte search of every forward
// DATA datagram for a needle taken from the payload. Control datagrams are
// classified exactly as relay.h does it -- UDT/SRT header word 0, bit 31 --
// and are forwarded without being searched or counted, so `data_fwd()` is a
// count of real payload packets.
class KmxTap {
public:
    KmxTap(uint16_t backend_port, const std::string& needle) : needle_(needle) {
        front_ = udp_socket_bound(0, &front_port_);
        back_ = udp_socket_bound(0, NULL);
        std::memset(&backend_, 0, sizeof backend_);
        backend_.sin_family = AF_INET;
        backend_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        backend_.sin_port = htons(backend_port);
        run_ = true;
        th_ = std::thread([this] { loop(); });
    }
    ~KmxTap() {
        run_ = false;
        if (th_.joinable()) th_.join();
        ::close(front_);
        ::close(back_);
    }
    KmxTap(const KmxTap&) = delete;
    KmxTap& operator=(const KmxTap&) = delete;

    uint16_t port() const { return front_port_; }
    // Forward DATA datagrams seen, and how many of them contained the needle
    // in clear.
    int data_fwd() const { return data_fwd_; }
    int clear_hits() const { return clear_hits_; }

private:
    static bool is_control(const char* b, size_t n) {
        if (n < 4) return true;
        uint32_t w0;
        std::memcpy(&w0, b, 4);
        return (ntohl(w0) & 0x80000000u) != 0;
    }

    void loop() {
        char buf[2048];
        sockaddr_in from;
        socklen_t fl;
        bool have_client = false;
        sockaddr_in client;
        std::memset(&client, 0, sizeof client);
        while (run_) {
            pollfd p[2] = {{front_, POLLIN, 0}, {back_, POLLIN, 0}};
            int ready = ::poll(p, 2, 20);
            if (ready > 0 && (p[0].revents & POLLIN)) {
                fl = sizeof from;
                ssize_t n = ::recvfrom(front_, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
                if (n > 0) {
                    client = from;
                    have_client = true;
                    if (!is_control(buf, (size_t)n)) {
                        data_fwd_++;
                        if (!needle_.empty() &&
                            std::search(buf, buf + n, needle_.begin(), needle_.end()) != buf + n)
                            clear_hits_++;
                    }
                    ::sendto(back_, buf, (size_t)n, 0, (const sockaddr*)&backend_, sizeof backend_);
                }
            }
            if (ready > 0 && (p[1].revents & POLLIN)) {
                fl = sizeof from;
                ssize_t n = ::recvfrom(back_, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
                if (n > 0 && have_client)
                    ::sendto(front_, buf, (size_t)n, 0, (const sockaddr*)&client, sizeof client);
            }
        }
    }

    int front_ = -1, back_ = -1;
    uint16_t front_port_ = 0;
    sockaddr_in backend_;
    std::string needle_;
    std::atomic<bool> run_{false};
    std::thread th_;
    std::atomic<int> data_fwd_{0}, clear_hits_{0};
};

// ------------------------------------------------------ bringing a link up
//
// Why this family carries its own link rather than using srtkit's: every link
// here has to run through KmxTap, the wire-side observer that counts cleartext
// hits for g07_keylen32, and it has to survive a caller socket being reused
// across two listeners (the spent-socket case in g02_caller_pw). Neither is
// something srtkit's Link models.
//
// The accept is bounded, and has to be: srt_accept honours no timeout at this
// release -- CUDTUnited::accept waits on m_AcceptCond with a plain
// pthread_cond_wait and consults only m_bSynRecving -- so when a handshake is
// REFUSED, which is the subject of three of the five groups here, an
// unbounded accept never returns and the case dies on the 60 s
// SRT_CASE_TIMEOUT instead of reporting the refusal. Measured: the three
// refusal probes each burned the full 60 s. srtkit's st_accept_polled is now
// built around the same measurement; this is the same bound, applied the only
// way the library allows:
// SRTO_RCVSYN=0 turns srt_accept into a poll that returns SRT_EASYNCRCV when
// nothing is queued, and the wait between polls is srt_epoll_wait -- a
// readiness wait with a deadline, not a sleep used as synchronisation. No
// second thread is needed: CUDTUnited::newConnection queues the accepted
// socket from the receive-queue worker, so it is already waiting by the time
// srt_connect returns on the caller.
struct KmxLink {
    SRTSOCKET lsn = SRT_INVALID_SOCK;
    SRTSOCKET cal = SRT_INVALID_SOCK;
    SRTSOCKET acc = SRT_INVALID_SOCK;
    Relay* relay = NULL;
    KmxTap* tap = NULL;
    int connect_rc = -1;   // what srt_connect returned
    int connect_err = 0;   // srt_getlasterror() at that moment
    // Set false when the caller socket belongs to an earlier link -- the
    // second-attempt case reuses one socket across two listeners.
    bool own_cal = true;

    KmxLink() {}
    KmxLink(const KmxLink&) = delete;
    KmxLink& operator=(const KmxLink&) = delete;
    ~KmxLink() { close(); }

    void close() {
        if (acc != SRT_INVALID_SOCK) { srt_close(acc); acc = SRT_INVALID_SOCK; }
        if (cal != SRT_INVALID_SOCK) { if (own_cal) srt_close(cal); cal = SRT_INVALID_SOCK; }
        if (lsn != SRT_INVALID_SOCK) { srt_close(lsn); lsn = SRT_INVALID_SOCK; }
        delete relay; relay = NULL;
        delete tap; tap = NULL;
    }
};

// Polls for an accepted socket until one appears, the caller's connect has
// already failed, or `budget_ms` runs out.
static SRTSOCKET kmx_accept_bounded(SRTSOCKET lsn, int budget_ms) {
    int eid = srt_epoll_create();
    int ev = SRT_EPOLL_IN;
    if (eid >= 0) srt_epoll_add_usock(eid, lsn, &ev);
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
    SRTSOCKET out = SRT_INVALID_SOCK;
    for (;;) {
        srt_clearlasterror();
        SRTSOCKET a = srt_accept(lsn, NULL, NULL);
        if (a != SRT_INVALID_SOCK) { out = a; break; }
        if (srt_getlasterror(NULL) != SRT_EASYNCRCV) break;  // a real error, not "none queued"
        if (std::chrono::steady_clock::now() >= deadline) break;
        if (eid >= 0) {
            SRTSOCKET rd[2];
            int rn = 2;
            srt_epoll_wait(eid, rd, &rn, NULL, NULL, 40, NULL, NULL, NULL, NULL);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    if (eid >= 0) srt_epoll_release(eid);
    return out;
}

typedef std::function<uint16_t(uint16_t)> ChannelOpener;

// listener -> channel -> caller, with no assertion that the handshake
// succeeded: the return value is srt_connect's, and L.acc stays
// SRT_INVALID_SOCK when nothing was accepted. `reuse_caller` connects an
// existing caller socket instead of making a new one (and then cconf is not
// applied, because the socket is already configured).
static int kmx_connect_core(KmxLink& L, const SockConf& lconf, const SockConf& cconf,
                            const ChannelOpener& open_channel,
                            SRTSOCKET reuse_caller = SRT_INVALID_SOCK) {
    L.lsn = kmx_fresh_socket();
    lconf(L.lsn);
    // Must come after lconf so nothing can turn the bounded accept back off.
    st_set_int(L.lsn, SRTO_RCVSYN, 0);

    sockaddr_in la = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(L.lsn, (sockaddr*)&la, sizeof la) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    int nl = (int)sizeof la;
    ST_REQUIRE_MSG(srt_getsockname(L.lsn, (sockaddr*)&la, &nl) != SRT_ERROR,
                   "srt_getsockname: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(L.lsn, 1) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());

    const uint16_t front = open_channel(ntohs(la.sin_port));

    if (reuse_caller != SRT_INVALID_SOCK) {
        L.cal = reuse_caller;
        L.own_cal = false;
    } else {
        L.cal = kmx_fresh_socket();
        cconf(L.cal);
    }

    sockaddr_in ra = st_loopback(front);
    srt_clearlasterror();
    L.connect_rc = srt_connect(L.cal, (sockaddr*)&ra, sizeof ra);
    L.connect_err = srt_getlasterror(NULL);

    // A refused handshake must not leave an accepted socket behind either, so
    // the failure path still looks -- just briefly, because by the time
    // srt_connect has returned the listener has long since decided.
    L.acc = kmx_accept_bounded(L.lsn, L.connect_rc == SRT_ERROR ? 300 : 5000);
    if (L.acc != SRT_INVALID_SOCK) {
        // The accepted socket inherits the listener's options, and the
        // listener had to be non-blocking for the accept to be bounded.
        // Give the accepted socket back its blocking receive.
        st_set_int(L.acc, SRTO_RCVSYN, 1);
    }
    return L.connect_rc;
}

// Over a clean relay: the same channel every other family uses, so a group
// here is not quietly testing a different transport.
static int kmx_try_connect(KmxLink& L, const SockConf& lconf, const SockConf& cconf,
                           SRTSOCKET reuse_caller = SRT_INVALID_SOCK) {
    ChannelOpener open = [&L](uint16_t backend) -> uint16_t {
        L.relay = new Relay(backend, RelaySched());
        return L.relay->port();
    };
    return kmx_connect_core(L, lconf, cconf, open, reuse_caller);
}

// Over the tap, for the one group that inspects the wire.
static int kmx_try_connect_tapped(KmxLink& L, const std::string& needle, const SockConf& lconf,
                                  const SockConf& cconf) {
    ChannelOpener open = [&L, &needle](uint16_t backend) -> uint16_t {
        L.tap = new KmxTap(backend, needle);
        return L.tap->port();
    };
    return kmx_connect_core(L, lconf, cconf, open);
}

static void kmx_require_up(KmxLink& L, const char* what) {
    ST_REQUIRE_MSG(L.connect_rc != SRT_ERROR, "%s: srt_connect failed with %s (\"%s\")", what,
                   kmx_errname(L.connect_err), srt_strerror(L.connect_err, 0));
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK, "%s: nothing was accepted although srt_connect "
                   "reported success", what);
    SRT_SOCKSTATUS st = srt_getsockstate(L.cal);
    ST_REQUIRE_MSG(st == SRTS_CONNECTED, "%s: the caller socket is %s (%d), expected "
                   "SRTS_CONNECTED (%d)", what, kmx_statename(st), (int)st, (int)SRTS_CONNECTED);
}

// The shared shape of every refusal assertion in g02/g03/g04: srt_connect
// returns -1 with SRT_ECONNREJ, nothing is accepted, and the caller socket
// falls back to SRTS_OPENED rather than SRTS_BROKEN. `subject` names the
// configuration so a failure line says which one gave way.
//
// SRTS_OPENED here is a REPORT and not an invitation: the state is what the
// socket says, and g02's third case is where the file pins down what that
// state is actually worth. Nothing in this helper may be read as a claim that
// the socket can be connected again.
static void kmx_require_refused(KmxLink& L, const char* subject) {
    ST_REQUIRE_MSG(L.connect_rc == SRT_ERROR,
                   "%s: srt_connect returned %d and the handshake COMPLETED; with strict "
                   "encryption enabled it must be refused with -1 / SRT_ECONNREJ (%d). This is "
                   "the v1.3.0 outcome, where the connection came up and the failure showed "
                   "only as KM state",
                   subject, L.connect_rc, (int)SRT_ECONNREJ);
    ST_REQUIRE_MSG(L.connect_err == SRT_ECONNREJ,
                   "%s: srt_connect failed with %s (%d, \"%s\"); expected SRT_ECONNREJ (%d)",
                   subject, kmx_errname(L.connect_err), L.connect_err,
                   srt_strerror(L.connect_err, 0), (int)SRT_ECONNREJ);
    ST_REQUIRE_MSG(L.acc == SRT_INVALID_SOCK,
                   "%s: srt_connect was refused but the listener still accepted a socket (%d); "
                   "a rejected handshake must leave no connection on either side",
                   subject, (int)L.acc);
    SRT_SOCKSTATUS st = srt_getsockstate(L.cal);
    ST_REQUIRE_MSG(st == SRTS_OPENED,
                   "%s: after the refusal the caller socket is %s (%d), expected SRTS_OPENED "
                   "(%d) -- the fallback is to OPENED and never to SRTS_BROKEN (%d)",
                   subject, kmx_statename(st), (int)st, (int)SRTS_OPENED, (int)SRTS_BROKEN);
}

// Sends KMX_COUNT messages caller -> accepted and requires every one of them
// back, in order and byte-identical.
static void kmx_require_payload_crosses(KmxLink& L, const char* what) {
    std::vector<std::string> sent = st_send_stream(L.cal, KMX_COUNT, KMX_MSG, KMX_GAP_US);
    std::vector<std::string> got = st_recv_stream(L.acc, KMX_COUNT, KMX_MSG, KMX_FULL_MS);
    ST_REQUIRE_MSG(got.size() == sent.size(),
                   "%s: %zu of %zu messages arrived", what, got.size(), sent.size());
    int bad = st_first_out_of_order(sent, got);
    ST_REQUIRE_MSG(bad == -1,
                   "%s: message %d is not the one that was sent in that position", what, bad);
    ST_REQUIRE_MSG(got == sent,
                   "%s: all %zu messages arrived but the bytes differ from what was sent", what,
                   got.size());
}

// =========================================================================
// g01_strictenc   (new)
//
// scored.txt: "`SRTO_STRICTENC` is readable at its default of 1, accepts 0 and
// 1 before connect, refuses the set after connect, and setting it to 0
// restores the v1.3.0 outcome where a passphrase mismatch connects and
// delivers `BADSECRET` KM state".
//
// interface.md is the source for both the default and the width: "A fresh
// socket reads back 1 before any explicit set", and "SRTO_STRICTENC is held
// and returned as an int32_t: a read of it succeeds and reports sizeof(int) --
// 4 -- in *optlen, even though its only meaningful values are 0 and 1". The
// width is stated because "boolean" alone does not settle it and this very
// package reads the inherited SRTO_MESSAGEAPI in one byte; scope.md repeats
// both. The v1.3.2 header comment documents neither, and docs/API.md does not
// mention the option at all.
// =========================================================================

SRT_CASE(g01_strictenc, default_is_readable_and_one) {
    SRTSOCKET s = kmx_fresh_socket();

    // Control: srt_getsockflag works on this socket at all. Without it, "the
    // option is readable" would also be satisfied by a library whose whole
    // getter is broken, and the failure below would be misattributed.
    int t = st_get_int(s, SRTO_RCVTIMEO);
    ST_REQUIRE_MSG(t == -1,
                   "control: SRTO_RCVTIMEO on a fresh socket read back %d, expected its default "
                   "of -1; srt_getsockflag must work here for the read below to mean anything",
                   t);

    int v = -12345;
    int n = (int)sizeof v;
    srt_clearlasterror();
    int rc = srt_getsockflag(s, SRTO_STRICTENC, &v, &n);
    ST_REQUIRE_MSG(rc == 0,
                   "srt_getsockflag(SRTO_STRICTENC) on a fresh socket returned %d with %s "
                   "(\"%s\"); at this milestone the option exists and is readable before connect",
                   rc, kmx_errname(srt_getlasterror(NULL)), srt_getlasterror_str());
    ST_REQUIRE_MSG(n == (int)sizeof(int),
                   "srt_getsockflag(SRTO_STRICTENC) reported %d bytes, expected %zu (int32). "
                   "The option is boolean in meaning but int32 in width -- instruction.md and "
                   "TEST_INTERFACE.md section 5 both state it -- so registering it the way the "
                   "inherited SRTO_MESSAGEAPI is registered returns the right value in the wrong "
                   "number of bytes", n, sizeof(int));
    ST_REQUIRE_MSG(v == 1,
                   "SRTO_STRICTENC on a fresh socket is %d; strict encryption is enabled by "
                   "default at this milestone, so the default is 1", v);

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g01_strictenc, accepts_zero_and_one_before_connect) {
    SRTSOCKET s = kmx_fresh_socket();

    // Both values, and back again: a library that accepted the set but ignored
    // it, or that latched the first value, fails the read-back.
    static const int vals[] = {0, 1, 0};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        kmx_set_must_pass(s, SRTO_STRICTENC, vals[i], "pre-connect SRTO_STRICTENC");
        int got = st_get_int(s, SRTO_STRICTENC);
        ST_REQUIRE_MSG(got == vals[i], "SRTO_STRICTENC read back %d after being set to %d", got,
                       vals[i]);
    }

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g01_strictenc, the_set_is_refused_after_connect) {
    KmxLink L;
    int pre_rc = 1;
    SockConf cconf = [&pre_rc](SRTSOCKET s) {
        st_live_lossless(s);
        pre_rc = st_try_set_int(s, SRTO_STRICTENC, 0);
    };
    kmx_try_connect(L, st_live_lossless, cconf);

    // The positive control for the refusal: the same option, the same value,
    // on the same socket, before connect.
    ST_REQUIRE_MSG(pre_rc == 0,
                   "SRTO_STRICTENC=0 before connect returned %d, expected 0; the option must be "
                   "settable before connect for the post-connect refusal to mean anything",
                   pre_rc);
    kmx_require_up(L, "plain connection for the post-connect binding check");

    int got = st_get_int(L.cal, SRTO_STRICTENC);
    ST_REQUIRE_MSG(got == 0,
                   "SRTO_STRICTENC across the handshake is %d, expected the 0 set before connect",
                   got);

    kmx_set_must_fail(L.cal, SRTO_STRICTENC, 1, SRT_ECONNSOCK,
                      "SRTO_STRICTENC on a connected caller");
    kmx_set_must_fail(L.acc, SRTO_STRICTENC, 0, SRT_ECONNSOCK,
                      "SRTO_STRICTENC on the accepted socket");

    // The refused set must not have taken effect.
    got = st_get_int(L.cal, SRTO_STRICTENC);
    ST_REQUIRE_MSG(got == 0,
                   "after the refused post-connect set SRTO_STRICTENC is %d, expected the "
                   "previous 0", got);

    // Control for both refusals: a documented `post` option is still settable
    // on these sockets, so the refusals are this option's binding and not a
    // socket refusing every set.
    kmx_set_must_pass(L.cal, SRTO_RCVTIMEO, 500,
                      "control: SRTO_RCVTIMEO (a `post` option) after connect");
}

SRT_CASE(g01_strictenc, zero_restores_the_v1_3_0_mismatch_outcome) {
    KmxLink L;
    int off_l = 1, off_c = 1;
    SockConf lconf = [&off_l](SRTSOCKET s) {
        st_live_lossless(s);
        off_l = st_try_set_int(s, SRTO_STRICTENC, 0);
        st_set_int(s, SRTO_PBKEYLEN, 32);
        st_set_str(s, SRTO_PASSPHRASE, KMX_PW_A);
    };
    SockConf cconf = [&off_c](SRTSOCKET s) {
        st_live_lossless(s);
        off_c = st_try_set_int(s, SRTO_STRICTENC, 0);
        st_set_int(s, SRTO_PBKEYLEN, 32);
        st_set_str(s, SRTO_PASSPHRASE, KMX_PW_B);
    };
    kmx_try_connect(L, lconf, cconf);

    ST_REQUIRE_MSG(off_l == 0 && off_c == 0,
                   "SRTO_STRICTENC=0 was refused before connect (listener rc=%d, caller rc=%d); "
                   "the escape hatch that restores the v1.3.0 outcome must be settable",
                   off_l, off_c);

    // The subject: with strict enforcement switched off, the mismatch behaves
    // as it did at v1.3.0 -- it CONNECTS.
    ST_REQUIRE_MSG(L.connect_rc != SRT_ERROR,
                   "with SRTO_STRICTENC=0 a passphrase mismatch was still refused: srt_connect "
                   "returned %d with %s (\"%s\"). Setting 0 must restore the v1.3.0 outcome, "
                   "where the connection came up",
                   L.connect_rc, kmx_errname(L.connect_err), srt_strerror(L.connect_err, 0));
    kmx_require_up(L, "mismatched passphrases with SRTO_STRICTENC=0");

    // ...and the failure shows only as KM state.
    int rcv = kmx_settled_kmstate(L.acc, SRTO_RCVKMSTATE, 2000,
                                  "accepted socket after a mismatch with strict encryption off");
    ST_REQUIRE_MSG(rcv == SRT_KM_S_BADSECRET,
                   "SRTO_RCVKMSTATE on the accepted socket is %s (%d), expected "
                   "SRT_KM_S_BADSECRET (%d): the peer's key material was undecryptable",
                   kmx_kmname(rcv), rcv, (int)SRT_KM_S_BADSECRET);
    int snd = kmx_kmstate(L.cal, SRTO_SNDKMSTATE, "caller socket after a mismatch");
    ST_REQUIRE_MSG(snd == SRT_KM_S_BADSECRET,
                   "SRTO_SNDKMSTATE on the caller is %s (%d), expected SRT_KM_S_BADSECRET (%d)",
                   kmx_kmname(snd), snd, (int)SRT_KM_S_BADSECRET);

    // And the payload never decrypts. The undecrypt counter is what makes this
    // a real observation rather than "the channel was empty": the datagrams
    // did arrive, and the receiver could not read them.
    std::vector<std::string> sent = st_send_stream(L.cal, KMX_COUNT, KMX_MSG, KMX_GAP_US);
    std::vector<std::string> got = st_recv_stream(L.acc, KMX_COUNT, KMX_MSG, KMX_EMPTY_MS);
    ST_REQUIRE_MSG(got.empty(),
                   "%zu of %zu messages were delivered although the KM state is BADSECRET; a "
                   "payload encrypted under a passphrase the receiver does not have must never "
                   "decrypt", got.size(), sent.size());
    int undec = kmx_undecrypted(L.acc);
    ST_REQUIRE_MSG(undec >= 1,
                   "srt_bstats().pktRcvUndecryptTotal on the receiver is %d after %d messages "
                   "were sent (pktRecvTotal %lld); nothing arrived at all, so this case has not "
                   "shown that the payload FAILED TO DECRYPT -- only that the channel was empty",
                   undec, KMX_COUNT, (long long)kmx_pkt_recv(L.acc));
}

SRT_CASE(g01_strictenc, zero_still_delivers_when_the_passphrases_match) {
    // The positive control for the case above: with the escape hatch set the
    // same way, a MATCHING passphrase still carries the payload. Without this,
    // a library that delivers nothing under any circumstances would score the
    // "never decrypts" assertion for free.
    KmxLink L;
    int off_l = 1, off_c = 1;
    SockConf lconf = [&off_l](SRTSOCKET s) {
        st_live_lossless(s);
        off_l = st_try_set_int(s, SRTO_STRICTENC, 0);
        st_set_int(s, SRTO_PBKEYLEN, 32);
        st_set_str(s, SRTO_PASSPHRASE, KMX_PW_A);
    };
    SockConf cconf = [&off_c](SRTSOCKET s) {
        st_live_lossless(s);
        off_c = st_try_set_int(s, SRTO_STRICTENC, 0);
        st_set_int(s, SRTO_PBKEYLEN, 32);
        st_set_str(s, SRTO_PASSPHRASE, KMX_PW_A);
    };
    kmx_try_connect(L, lconf, cconf);

    ST_REQUIRE_MSG(off_l == 0 && off_c == 0,
                   "SRTO_STRICTENC=0 was refused before connect (listener rc=%d, caller rc=%d)",
                   off_l, off_c);
    kmx_require_up(L, "matching passphrases with SRTO_STRICTENC=0");
    kmx_require_payload_crosses(L, "matching passphrases with SRTO_STRICTENC=0");

    int rcv = kmx_settled_kmstate(L.acc, SRTO_RCVKMSTATE, 2000,
                                  "accepted socket with matching passphrases");
    ST_REQUIRE_MSG(rcv == SRT_KM_S_SECURED,
                   "SRTO_RCVKMSTATE on the accepted socket is %s (%d), expected "
                   "SRT_KM_S_SECURED (%d)", kmx_kmname(rcv), rcv, (int)SRT_KM_S_SECURED);
    int undec = kmx_undecrypted(L.acc);
    ST_REQUIRE_MSG(undec == 0,
                   "srt_bstats().pktRcvUndecryptTotal is %d on a correctly keyed connection, "
                   "expected 0", undec);
}

// =========================================================================
// g02_caller_pw   (new)
//
// scored.txt: "with strict encryption enabled, when the caller sets a
// passphrase and the listener does not, `srt_connect` returns -1 with
// `SRT_ECONNREJ` and the caller socket falls back to `SRTS_OPENED`".
//
// At v1.3.0 this configuration connects and reports KM state NOSECRET
// (deltas/03.md D2), so the discriminator is the refusal itself.
//
// These groups do not merely rely on the default: each one sets
// SRTO_STRICTENC=1 explicitly, records the rc, and asserts the behaviour
// FIRST. That ordering is deliberate -- against v1.3.0 the enable is refused,
// and the message the group must print there is the behavioural one ("the
// handshake COMPLETED"), not "setsockflag(53) failed".
// =========================================================================

// Enables strict encryption and records whether the option existed, so the
// failure message can say so.
static SockConf kmx_strict(int* rc_out, const char* passphrase) {
    return [rc_out, passphrase](SRTSOCKET s) {
        st_live_lossless(s);
        *rc_out = st_try_set_int(s, SRTO_STRICTENC, 1);
        if (passphrase != NULL) {
            st_set_int(s, SRTO_PBKEYLEN, 32);
            st_set_str(s, SRTO_PASSPHRASE, passphrase);
        }
    };
}

static void kmx_require_strict_was_enabled(int rc_l, int rc_c) {
    ST_REQUIRE_MSG(rc_l == 0 && rc_c == 0,
                   "SRTO_STRICTENC=1 was refused before connect (listener rc=%d, caller rc=%d); "
                   "the option must exist and be settable for this group to be asserting strict "
                   "enforcement rather than the default of some other library",
                   rc_l, rc_c);
}

SRT_CASE(g02_caller_pw, a_caller_passphrase_against_a_bare_listener_is_refused) {
    KmxLink L;
    int rc_l = 1, rc_c = 1;
    kmx_try_connect(L, kmx_strict(&rc_l, NULL), kmx_strict(&rc_c, KMX_PW_A));

    kmx_require_refused(L, "caller has a passphrase, listener has none");
    kmx_require_strict_was_enabled(rc_l, rc_c);
}

SRT_CASE(g02_caller_pw, a_bare_pair_and_a_matching_pair_both_still_connect) {
    // Two positive controls in one case, because they answer the same
    // objection: a library that refused EVERY handshake would pass the case
    // above for free. Strict enforcement must reject only the asymmetric
    // configuration.
    {
        KmxLink L;
        int rc_l = 1, rc_c = 1;
        kmx_try_connect(L, kmx_strict(&rc_l, NULL), kmx_strict(&rc_c, NULL));
        kmx_require_strict_was_enabled(rc_l, rc_c);
        kmx_require_up(L, "control: neither side has a passphrase, strict encryption enabled");
        kmx_require_payload_crosses(L, "control: unencrypted pair under strict encryption");
    }
    {
        KmxLink L;
        int rc_l = 1, rc_c = 1;
        kmx_try_connect(L, kmx_strict(&rc_l, KMX_PW_A), kmx_strict(&rc_c, KMX_PW_A));
        kmx_require_strict_was_enabled(rc_l, rc_c);
        kmx_require_up(L, "control: both sides share a passphrase, strict encryption enabled");
        kmx_require_payload_crosses(L, "control: matching passphrases under strict encryption");
    }
}

SRT_CASE(g02_caller_pw, the_refused_caller_socket_stays_opened_and_is_spent) {
    // What "falls back to SRTS_OPENED, not SRTS_BROKEN" (interface.md) is
    // worth, which is not what it sounds like. The state really is OPENED and
    // it stays there -- measured 25 times at v1.3.2, sampled every 2 ms for
    // 2.5 s after the refusal, zero transitions -- but OPENED is only what the
    // socket REPORTS. srt_getsockstate answers from the socket-level status,
    // which CUDTUnited::connect's exception interceptor puts back to OPENED;
    // CUDT::startConnect's own m_bConnecting is left set by the refusal and is
    // cleared nowhere but on a successful connect, and it is that flag the
    // next srt_connect trips on. So the second attempt is refused with
    // SRT_ECONNSOCK, deterministically, 25 runs out of 25, with the socket
    // still reporting OPENED on both sides of it.
    //
    // The case asserts the state and the refusal together because either one
    // alone is cheap: asserting only OPENED cannot tell this library from one
    // that keeps the socket genuinely reusable, and asserting only the
    // ECONNSOCK cannot tell it from one that marked the socket BROKEN and
    // refuses for that reason instead. The third link is the positive control
    // this group owes a negative subject -- a FRESH caller carrying the same
    // passphrase to the same listener configuration connects and delivers, so
    // the refusal above is a fact about the spent socket and not about the
    // listener the case built to receive it.
    KmxLink first;
    int rc_l = 1, rc_c = 1;
    kmx_try_connect(first, kmx_strict(&rc_l, NULL), kmx_strict(&rc_c, KMX_PW_A));
    kmx_require_refused(first, "caller has a passphrase, listener has none");
    kmx_require_strict_was_enabled(rc_l, rc_c);

    KmxLink second;
    int rc_l2 = 1;
    // `first` still owns the caller socket; `second` borrows it.
    kmx_try_connect(second, kmx_strict(&rc_l2, KMX_PW_A), st_noconf, first.cal);
    ST_REQUIRE_MSG(rc_l2 == 0, "SRTO_STRICTENC=1 was refused on the second listener (rc=%d)",
                   rc_l2);
    ST_REQUIRE_MSG(second.connect_rc == SRT_ERROR,
                   "a second srt_connect on the refused caller socket returned %d and the "
                   "handshake COMPLETED; the refusal spends the socket, so the reconnect must "
                   "be refused with -1 / SRT_ECONNSOCK (%d)",
                   second.connect_rc, (int)SRT_ECONNSOCK);
    ST_REQUIRE_MSG(second.connect_err == SRT_ECONNSOCK,
                   "the second srt_connect on the refused caller socket failed with %s (%d, "
                   "\"%s\"); expected SRT_ECONNSOCK (%d) -- the socket is not broken, it is "
                   "already spent on the attempt that was refused",
                   kmx_errname(second.connect_err), second.connect_err,
                   srt_strerror(second.connect_err, 0), (int)SRT_ECONNSOCK);
    SRT_SOCKSTATUS st = srt_getsockstate(first.cal);
    ST_REQUIRE_MSG(st == SRTS_OPENED,
                   "after the reconnect was refused the caller socket is %s (%d); it must still "
                   "report SRTS_OPENED (%d) -- neither refusal ever moves it to SRTS_BROKEN "
                   "(%d), which is why the state alone does not say whether it is usable",
                   kmx_statename(st), (int)st, (int)SRTS_OPENED, (int)SRTS_BROKEN);

    KmxLink control;
    int rc_l3 = 1, rc_c3 = 1;
    kmx_try_connect(control, kmx_strict(&rc_l3, KMX_PW_A), kmx_strict(&rc_c3, KMX_PW_A));
    kmx_require_strict_was_enabled(rc_l3, rc_c3);
    kmx_require_up(control, "control: a fresh caller with the same passphrase, same listener "
                            "options as the reconnect the library just refused");
    kmx_require_payload_crosses(control, "control: fresh caller, same listener options");
}

// =========================================================================
// g03_listener_pw   (new)
//
// scored.txt: "with strict encryption enabled, when the listener sets a
// passphrase and the caller does not, `srt_connect` returns -1 with
// `SRT_ECONNREJ` and the caller socket falls back to `SRTS_OPENED`".
//
// The mirror of g02, and a separate group because the two are separate code
// paths at v1.3.2: the caller-passphrase case is refused where the agent finds
// a KMREQ it has no passphrase for, the listener-passphrase case where the
// agent has a passphrase and the peer declared none.
// =========================================================================

SRT_CASE(g03_listener_pw, a_listener_passphrase_against_a_bare_caller_is_refused) {
    KmxLink L;
    int rc_l = 1, rc_c = 1;
    kmx_try_connect(L, kmx_strict(&rc_l, KMX_PW_A), kmx_strict(&rc_c, NULL));

    kmx_require_refused(L, "listener has a passphrase, caller has none");
    kmx_require_strict_was_enabled(rc_l, rc_c);
}

SRT_CASE(g03_listener_pw, the_same_listener_accepts_a_caller_that_shares_its_passphrase) {
    // Positive control: the listener's configuration is not the problem -- the
    // asymmetry is. Same listener options as the case above, and this time the
    // caller brings the passphrase.
    KmxLink L;
    int rc_l = 1, rc_c = 1;
    kmx_try_connect(L, kmx_strict(&rc_l, KMX_PW_A), kmx_strict(&rc_c, KMX_PW_A));
    kmx_require_strict_was_enabled(rc_l, rc_c);
    kmx_require_up(L, "control: caller supplies the listener's passphrase");
    kmx_require_payload_crosses(L, "control: caller supplies the listener's passphrase");

    int rcv = kmx_settled_kmstate(L.acc, SRTO_RCVKMSTATE, 2000, "accepted socket");
    ST_REQUIRE_MSG(rcv == SRT_KM_S_SECURED,
                   "control: SRTO_RCVKMSTATE on the accepted socket is %s (%d), expected "
                   "SRT_KM_S_SECURED (%d)", kmx_kmname(rcv), rcv, (int)SRT_KM_S_SECURED);
}

SRT_CASE(g03_listener_pw, a_bare_pair_still_connects_through_the_same_listener_options) {
    // Second positive control: with strict encryption on and NO passphrase
    // anywhere, the handshake completes. Together with the case above this
    // pins the refusal to the asymmetry rather than to strict mode itself.
    KmxLink L;
    int rc_l = 1, rc_c = 1;
    kmx_try_connect(L, kmx_strict(&rc_l, NULL), kmx_strict(&rc_c, NULL));
    kmx_require_strict_was_enabled(rc_l, rc_c);
    kmx_require_up(L, "control: no passphrase on either side, strict encryption enabled");
    kmx_require_payload_crosses(L, "control: unencrypted pair under strict encryption");

    int rcv = kmx_settled_kmstate(L.acc, SRTO_RCVKMSTATE, 2000, "accepted socket");
    ST_REQUIRE_MSG(rcv == SRT_KM_S_UNSECURED,
                   "control: SRTO_RCVKMSTATE on an unencrypted connection is %s (%d), expected "
                   "SRT_KM_S_UNSECURED (%d)", kmx_kmname(rcv), rcv, (int)SRT_KM_S_UNSECURED);
}

// =========================================================================
// g04_mismatch   (new)
//
// scored.txt: "with strict encryption enabled, two different passphrases are
// rejected at connect time with `SRT_ECONNREJ` rather than connecting and
// reporting `BADSECRET` KM state".
//
// The "rather than" is the whole point, and it is what g01's
// zero_restores_the_v1_3_0_mismatch_outcome case shows from the other side:
// the BADSECRET outcome is still reachable, but only by explicitly disabling
// strict enforcement.
// =========================================================================

SRT_CASE(g04_mismatch, two_different_passphrases_are_refused_at_connect) {
    KmxLink L;
    int rc_l = 1, rc_c = 1;
    kmx_try_connect(L, kmx_strict(&rc_l, KMX_PW_A), kmx_strict(&rc_c, KMX_PW_B));

    kmx_require_refused(L, "two entirely different passphrases");
    kmx_require_strict_was_enabled(rc_l, rc_c);
}

SRT_CASE(g04_mismatch, a_one_character_difference_is_enough) {
    // Same length, same prefix, one byte apart: the refusal is a key check,
    // not a length check or a "the peer sent something odd" check.
    static const char nearly[] = "kmx-step03-passphrase-alphb";
    static_assert(sizeof nearly == sizeof KMX_PW_A,
                  "the near-miss passphrase must be the same length as KMX_PW_A");

    KmxLink L;
    int rc_l = 1, rc_c = 1;
    kmx_try_connect(L, kmx_strict(&rc_l, KMX_PW_A), kmx_strict(&rc_c, nearly));

    kmx_require_refused(L, "passphrases differing in one character");
    kmx_require_strict_was_enabled(rc_l, rc_c);
}

SRT_CASE(g04_mismatch, the_matching_passphrase_connects_and_delivers) {
    // Positive control for both refusals above, with the same key length and
    // the same options: only the passphrase changes.
    KmxLink L;
    int rc_l = 1, rc_c = 1;
    kmx_try_connect(L, kmx_strict(&rc_l, KMX_PW_A), kmx_strict(&rc_c, KMX_PW_A));
    kmx_require_strict_was_enabled(rc_l, rc_c);
    kmx_require_up(L, "control: identical passphrases");
    kmx_require_payload_crosses(L, "control: identical passphrases");

    int undec = kmx_undecrypted(L.acc);
    ST_REQUIRE_MSG(undec == 0,
                   "control: srt_bstats().pktRcvUndecryptTotal is %d on a correctly keyed "
                   "connection, expected 0", undec);
}

// =========================================================================
// g07_keylen32   (inherited)
//
// scored.txt: "with a matching passphrase and `SRTO_PBKEYLEN=32` on both
// sides, the payload is carried intact and does not appear in clear in
// datagrams on the wire".
//
// This is milestone-01 surface (srt-docs/ on SRTO_PASSPHRASE and
// SRTO_PBKEYLEN) and must pass against the PREVIOUS release, so nothing here
// names SRTO_STRICTENC or any other v1.3.2 identifier. It does not need to:
// matching passphrases connect identically with strict enforcement on or off.
// =========================================================================

static SockConf kmx_keylen32(const char* passphrase) {
    return [passphrase](SRTSOCKET s) {
        st_live_lossless(s);
        st_set_int(s, SRTO_PBKEYLEN, 32);
        st_set_str(s, SRTO_PASSPHRASE, passphrase);
    };
}

SRT_CASE(g07_keylen32, the_payload_crosses_intact_at_keylen_32) {
    KmxLink L;
    kmx_try_connect(L, kmx_keylen32(KMX_PW_A), kmx_keylen32(KMX_PW_A));
    kmx_require_up(L, "matching passphrase at SRTO_PBKEYLEN=32");
    kmx_require_payload_crosses(L, "matching passphrase at SRTO_PBKEYLEN=32");

    // Not a vacuous "it arrived": the connection really was encrypted, and
    // nothing had to be thrown away undecrypted on the way.
    int rcv = kmx_settled_kmstate(L.acc, SRTO_RCVKMSTATE, 2000,
                                  "accepted socket at SRTO_PBKEYLEN=32");
    ST_REQUIRE_MSG(rcv == SRT_KM_S_SECURED,
                   "SRTO_RCVKMSTATE on the accepted socket is %s (%d), expected "
                   "SRT_KM_S_SECURED (%d); the payload arrived, but not over an encrypted "
                   "connection", kmx_kmname(rcv), rcv, (int)SRT_KM_S_SECURED);
    int snd = kmx_kmstate(L.cal, SRTO_SNDKMSTATE, "caller at SRTO_PBKEYLEN=32");
    ST_REQUIRE_MSG(snd == SRT_KM_S_SECURED,
                   "SRTO_SNDKMSTATE on the caller is %s (%d), expected SRT_KM_S_SECURED (%d)",
                   kmx_kmname(snd), snd, (int)SRT_KM_S_SECURED);
    int undec = kmx_undecrypted(L.acc);
    ST_REQUIRE_MSG(undec == 0,
                   "srt_bstats().pktRcvUndecryptTotal is %d, expected 0: every packet that "
                   "arrived was decrypted", undec);
}

SRT_CASE(g07_keylen32, the_key_length_is_32_on_both_sides) {
    KmxLink L;
    kmx_try_connect(L, kmx_keylen32(KMX_PW_A), kmx_keylen32(KMX_PW_A));
    kmx_require_up(L, "matching passphrase at SRTO_PBKEYLEN=32");
    // Payload first, then the length: read after the stream has crossed, the
    // length asserted below is the one the connection actually ran on and not
    // merely the one that was configured. (The key material itself rides the
    // handshake at this release -- interface.md, "The key material travels in
    // the handshake" -- so the ordering is about what the assertion means, not
    // about when the key becomes available.)
    kmx_require_payload_crosses(L, "matching passphrase at SRTO_PBKEYLEN=32");

    int kl_cal = st_get_int(L.cal, SRTO_PBKEYLEN);
    ST_REQUIRE_MSG(kl_cal == 32,
                   "SRTO_PBKEYLEN on the caller reads %d after connect, expected the 32 that was "
                   "set", kl_cal);
    int kl_acc = st_get_int(L.acc, SRTO_PBKEYLEN);
    ST_REQUIRE_MSG(kl_acc == 32,
                   "SRTO_PBKEYLEN on the accepted socket reads %d, expected 32: the receiving "
                   "side must end up on the same key length", kl_acc);

    // Control: a 16-byte key is a different, equally valid configuration, so
    // "32" above is the value that was asked for and not a constant the
    // library reports whatever it is given. The same fresh socket carries the
    // inherited default first: SRTO_PBKEYLEN reads back 0 before anything has
    // been set on it. TEST_INTERFACE.md section 5 states that default and then
    // states that 0 does NOT mean "transmits in clear"; the first half was
    // asserted nowhere in this step until this read, so a library that reported
    // some other configured length on a fresh socket -- 16, say, matching the
    // key it actually falls back to -- passed every group here. It is a read of
    // an unconnected socket for an exact inherited value, so it depends on no
    // clock, no host load and no ordering, which is what lets it sit in a gate
    // group.
    SRTSOCKET s = kmx_fresh_socket();
    int kl0 = st_get_int(s, SRTO_PBKEYLEN);
    ST_REQUIRE_MSG(kl0 == 0,
                   "SRTO_PBKEYLEN on a fresh socket reads %d, expected the inherited default 0; "
                   "an explicit set is what fixes the key length, and until one arrives there is "
                   "no configured length to report", kl0);
    kmx_set_must_pass(s, SRTO_PBKEYLEN, 16, "control: SRTO_PBKEYLEN=16 on a fresh socket");
    int kl16 = st_get_int(s, SRTO_PBKEYLEN);
    ST_REQUIRE_MSG(kl16 == 16,
                   "control: SRTO_PBKEYLEN set to 16 reads back %d; if this socket also reported "
                   "32 the assertions above would be meaningless", kl16);
    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g07_keylen32, the_payload_is_not_readable_on_the_wire) {
    // The needle is a 256-byte slice out of the middle of one of the messages
    // that will be sent -- long enough that a chance match in a datagram is
    // not a thing that happens, and taken from st_payload so the group is
    // searching for exactly the bytes it sent.
    const std::string needle = st_payload(3, KMX_MSG).substr(64, 256);

    int enc_hits = -1, enc_data = -1;
    {
        KmxLink L;
        kmx_try_connect_tapped(L, needle, kmx_keylen32(KMX_PW_A), kmx_keylen32(KMX_PW_A));
        kmx_require_up(L, "encrypted link through the tap");
        kmx_require_payload_crosses(L, "encrypted link through the tap");
        enc_hits = L.tap->clear_hits();
        enc_data = L.tap->data_fwd();
    }

    // The positive control, and it is not optional: the same tap, the same
    // payload, the same needle, with encryption removed. If this run does not
    // find the payload in clear then the tap is not reading the wire and the
    // zero above means nothing at all.
    int clear_hits = -1, clear_data = -1;
    {
        KmxLink L;
        kmx_try_connect_tapped(L, needle, st_live_lossless, st_live_lossless);
        kmx_require_up(L, "control: unencrypted link through the tap");
        kmx_require_payload_crosses(L, "control: unencrypted link through the tap");
        clear_hits = L.tap->clear_hits();
        clear_data = L.tap->data_fwd();
    }

    ST_REQUIRE_MSG(clear_data >= KMX_COUNT,
                   "control: the tap forwarded %d data datagrams on the unencrypted link, fewer "
                   "than the %d messages sent; the tap is not seeing the traffic", clear_data,
                   KMX_COUNT);
    ST_REQUIRE_MSG(clear_hits >= 1,
                   "control: the payload was NOT found in clear in any of the %d unencrypted "
                   "data datagrams the tap forwarded; the tap cannot detect cleartext, so the "
                   "encrypted result below proves nothing", clear_data);

    ST_REQUIRE_MSG(enc_data >= KMX_COUNT,
                   "the tap forwarded %d data datagrams on the encrypted link, fewer than the %d "
                   "messages sent", enc_data, KMX_COUNT);
    ST_REQUIRE_MSG(enc_hits == 0,
                   "the payload appeared in clear in %d of the %d data datagrams the tap "
                   "forwarded on a connection with SRTO_PBKEYLEN=32 and a matching passphrase; "
                   "expected 0 (the unencrypted control found it in %d of %d)",
                   enc_hits, enc_data, clear_hits, clear_data);
}
