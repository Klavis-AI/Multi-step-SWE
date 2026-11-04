// Family `kmx` for checkpoint 01 (SRT v1.2.0): payload encryption and the
// Key Material exchange state reported through the socket options.
//
//   g09_encryption  the payload crosses intact when both sides share a
//                   passphrase and is never on the wire in clear; it does not
//                   arrive when only the sender has a passphrase, or when the
//                   two passphrases differ.
//   g10_kmstate     SRTO_RCVKMSTATE / SRTO_SNDPEERKMSTATE (and SRTO_KMSTATE,
//                   the pre-TWOWAY name for the same thing) reach the KM state
//                   the milestone documents for the unsecured, secured,
//                   no-secret and bad-secret connections.
//
// Three properties of v1.2.0 shape everything below, and each was read out of
// the milestone's own source before it was asserted:
//
//   1. HSv4. The SRT extensions, key material included, travel as user-defined
//      control packets *after* the UDT handshake, and only the side that
//      declared SRTO_SENDER=1 before connect sends KMREQ. A caller that does
//      not declare itself exchanges no key material at all.
//   2. SRTO_PBKEYLEN defaults to 0 on a fresh socket, and 0 means "no
//      encryption" -- the header's {16,24,32} are the *valid* lengths, not the
//      default. Setting a passphrase alone leaves the stream in clear, so every
//      encrypted case here sets a key length on the sender as well.
//   3. The sender emits KMREQ from its periodic send-side timer, which only
//      runs while it has data to push. On an idle connection the KM state never
//      leaves SRT_KM_S_UNSECURED, and the packets sent before KMRSP comes back
//      are counted as undecryptable at the receiver. Every case that reads a KM
//      state therefore drives the exchange with warm-up traffic first and exits
//      on the observed state, never on a timer; the measured stream is sent
//      afterwards and is the only thing whose delivery is asserted.
//
// What this family deliberately does NOT assert, because v1.2.0 does not do it:
// a mismatched passphrase is not refused here. The connection comes up, the KM
// state goes to SRT_KM_S_BADSECRET and the payload simply never decrypts.
// Rejection arrives with SRTO_STRICTENC at v1.3.2 and belongs to that step.
#include "srtkit.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Shared vocabulary
// ---------------------------------------------------------------------------

static const char kPassA[] = "srt-step01-passphrase-A";
static const char kPassB[] = "srt-step01-passphrase-B";

// A marker long enough that its appearance in a forwarded datagram cannot be a
// coincidence, and short enough to repeat inside every payload.
static const char kNeedle[] = "CANARY-9f3a2b7c-IN-THE-CLEAR";
static const char kWarmTag[] = "WARMUP";

static const int kMsgSize = 512;   // >= st_payload's 32-byte minimum
static const int kKmWarmMax = 4000; // bound on the warm-up, ~8 s at 2 ms apart
static const int kWarmGapUs = 2000;

static const char* km_name(int v) {
    switch (v) {
    case SRT_KM_S_UNSECURED: return "UNSECURED";
    case SRT_KM_S_SECURING:  return "SECURING";
    case SRT_KM_S_SECURED:   return "SECURED";
    case SRT_KM_S_NOSECRET:  return "NOSECRET";
    case SRT_KM_S_BADSECRET: return "BADSECRET";
    default: return "not a KM state";
    }
}

// The receiver end: live-lossless defaults plus an optional passphrase.
static SockConf rx_conf(const char* pass) {
    std::string p = pass ? pass : "";
    return [p](SRTSOCKET s) {
        st_live_lossless(s);
        if (!p.empty()) st_set_str(s, SRTO_PASSPHRASE, p);
    };
}

// The sending end. SRTO_SENDER must be set before connect at HSv4 or the side
// never sends KMREQ; SRTO_PBKEYLEN must be non-zero or nothing is encrypted.
static SockConf tx_conf(const char* pass, int keylen) {
    std::string p = pass ? pass : "";
    return [p, keylen](SRTSOCKET s) {
        st_set_int(s, SRTO_SENDER, 1);
        st_live_lossless(s);
        if (keylen != 0) st_set_int(s, SRTO_PBKEYLEN, keylen);
        if (!p.empty()) st_set_str(s, SRTO_PASSPHRASE, p);
    };
}

// Warm-up bodies are tagged so the measured receive can tell them apart from
// the stream it is counting.
static std::string warm_payload(int index) {
    std::string s((size_t)kMsgSize, '\0');
    std::snprintf(&s[0], (size_t)kMsgSize, "%s%06d", kWarmTag, index);
    for (int i = 16; i < kMsgSize; i++) s[(size_t)i] = (char)('a' + ((index + i) % 26));
    return s;
}

// A body that is nothing but the marker, so that a datagram carrying it in
// clear contains the marker verbatim and one carrying it encrypted cannot.
static std::string marked_payload(int index) {
    std::string s((size_t)kMsgSize, '\0');
    std::snprintf(&s[0], (size_t)kMsgSize, "MARK%06d", index);
    const size_t nlen = std::strlen(kNeedle);
    for (size_t pos = 16; pos < (size_t)kMsgSize; pos += nlen)
        std::memcpy(&s[pos], kNeedle, std::min(nlen, (size_t)kMsgSize - pos));
    return s;
}

// Receives `want` measured messages, skipping whatever the key-exchange warm-up
// left in the buffer. Bounded by SRTO_RCVTIMEO: a channel that delivers nothing
// costs `timeo_ms`, not the case budget.
static std::vector<std::string> recv_measured(SRTSOCKET s, int want, int timeo_ms) {
    st_set_int(s, SRTO_RCVTIMEO, timeo_ms);
    std::vector<std::string> got;
    std::string buf((size_t)kMsgSize + 256, '\0');
    while ((int)got.size() < want) {
        int rc = srt_recvmsg(s, &buf[0], (int)buf.size());
        if (rc <= 0) break;
        std::string m = buf.substr(0, (size_t)rc);
        if (m.compare(0, std::strlen(kWarmTag), kWarmTag) == 0) continue;
        got.push_back(m);
    }
    return got;
}

// Drives the KM exchange with tagged traffic and returns the sender's view of
// the peer's KM state once it has left the transient states. The exit condition
// is the state itself; the pacing gap is only there so live mode's rate limiter
// is not outrun.
static int km_warm(SRTSOCKET snd) {
    for (int i = 0; i < kKmWarmMax; i++) {
        int v = st_get_int(snd, SRTO_SNDPEERKMSTATE);
        if (v == SRT_KM_S_SECURED || v == SRT_KM_S_NOSECRET || v == SRT_KM_S_BADSECRET) return v;
        std::string m = warm_payload(i);
        int rc = srt_sendmsg(snd, m.data(), (int)m.size(), -1, 1);
        ST_REQUIRE_MSG(rc == (int)m.size(), "warm-up srt_sendmsg #%d returned %d: %s", i, rc,
                       srt_getlasterror_str());
        std::this_thread::sleep_for(std::chrono::microseconds(kWarmGapUs));
    }
    return st_get_int(snd, SRTO_SNDPEERKMSTATE);
}

static void km_warm_to(SRTSOCKET snd, int want) {
    int got = km_warm(snd);
    ST_REQUIRE_MSG(got == want,
                   "after %d warm-up messages the sender reports peer KM state %d (%s), expected "
                   "%d (%s)",
                   kKmWarmMax, got, km_name(got), want, km_name(want));
}

// Both names for a socket's KM state, checked together: the whole point of the
// group is that they agree with each other and with the documented value.
static void expect_km(SRTSOCKET s, const char* who, int want) {
    int r = st_get_int(s, SRTO_RCVKMSTATE);
    ST_REQUIRE_MSG(r == want, "%s SRTO_RCVKMSTATE = %d (%s), expected %d (%s)", who, r, km_name(r),
                   want, km_name(want));
    int p = st_get_int(s, SRTO_SNDPEERKMSTATE);
    ST_REQUIRE_MSG(p == want, "%s SRTO_SNDPEERKMSTATE = %d (%s), expected %d (%s)", who, p,
                   km_name(p), want, km_name(want));
}

static int undecrypt_since(SRTSOCKET s, bool clear_now) {
    SRT_TRACEBSTATS st;
    std::memset(&st, 0, sizeof st);
    ST_SRT_OK(srt_bstats(s, &st, clear_now ? 1 : 0));
    return st.pktRcvUndecrypt;
}

static int64_t pkt_recv(SRTSOCKET s) {
    SRT_TRACEBSTATS st;
    std::memset(&st, 0, sizeof st);
    ST_SRT_OK(srt_bstats(s, &st, 0));
    return st.pktRecv;
}

static void require_same_stream(const std::vector<std::string>& sent,
                                const std::vector<std::string>& got, int want) {
    ST_REQUIRE_MSG((int)got.size() == want, "%d of %d messages arrived", (int)got.size(), want);
    int bad = st_first_out_of_order(sent, got);
    ST_REQUIRE_MSG(bad < 0, "message %d differs from what was sent or arrived out of order", bad);
    ST_REQUIRE_MSG(got == sent, "the delivered stream is not byte-identical to the sent stream");
}

// ---------------------------------------------------------------------------
// A UDP relay that keeps what it forwards.
//
// relay.h's Relay exposes only counters, and this file may not modify it, so
// the on-the-wire check owns its own socket pair. It forwards everything --
// no schedule, no loss -- and records every datagram travelling from the caller
// (the sender) towards the listener, which is the direction the payload takes.
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
            int ready = ::poll(p, 2, 20);
            if (ready > 0 && (p[0].revents & POLLIN)) {
                fl = sizeof from;
                ssize_t n = ::recvfrom(front_, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
                if (n > 0) {
                    client = from;
                    have_client = true;
                    {
                        std::lock_guard<std::mutex> g(mx_);
                        fwd_.push_back(std::string(buf, (size_t)n));
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

// Same shape as st_try_connect, with the recording tap in place of the relay.
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

    int rc = srt_connect(L.cal, (sockaddr*)&ra, sizeof ra);
    int err = srt_getlasterror(nullptr);
    connect_done.store(true);
    acceptor.join();
    L.acc = accepted;

    ST_REQUIRE_MSG(rc != SRT_ERROR, "srt_connect through the tap: %s", srt_strerror(err, 0));
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK, "srt_accept through the tap: %s",
                   srt_getlasterror_str());
}

// One half of the on-the-wire experiment: carry `count` marker-filled messages
// across a tapped channel, encrypted or not, and report how many forwarded
// datagrams carried the marker verbatim.
struct WireResult {
    int delivered = 0;
    int datagrams = 0;
    int marker_hits = 0;
};

static WireResult wire_run(bool encrypted, int count) {
    TapLink L;
    const char* pass = encrypted ? kPassA : nullptr;
    tap_connect(L, rx_conf(pass), tx_conf(pass, encrypted ? 16 : 0));
    if (encrypted) km_warm_to(L.cal, SRT_KM_S_SECURED);

    std::vector<std::string> sent;
    sent.reserve((size_t)count);
    for (int i = 0; i < count; i++) {
        std::string m = marked_payload(i);
        int rc = srt_sendmsg(L.cal, m.data(), (int)m.size(), -1, 1);
        ST_REQUIRE_MSG(rc == (int)m.size(), "srt_sendmsg #%d returned %d: %s", i, rc,
                       srt_getlasterror_str());
        sent.push_back(m);
        std::this_thread::sleep_for(std::chrono::microseconds(kWarmGapUs));
    }
    std::vector<std::string> got = recv_measured(L.acc, count, 4000);
    require_same_stream(sent, got, count);

    WireResult r;
    r.delivered = (int)got.size();
    std::vector<std::string> seen = L.tap->forwarded();
    r.datagrams = (int)seen.size();
    for (size_t i = 0; i < seen.size(); i++)
        if (seen[i].find(kNeedle) != std::string::npos) r.marker_hits++;
    return r;
}

// ===========================================================================
// g09_encryption
// ===========================================================================

SRT_CASE(g09_encryption, a_shared_passphrase_carries_every_message_intact) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, rx_conf(kPassA), tx_conf(kPassA, 16));
    km_warm_to(L.cal, SRT_KM_S_SECURED);

    const int want = 24;
    (void)undecrypt_since(L.acc, true); // forget the warm-up's undecryptable packets
    std::vector<std::string> sent = st_send_stream(L.cal, want, kMsgSize, kWarmGapUs);
    std::vector<std::string> got = recv_measured(L.acc, want, 4000);
    require_same_stream(sent, got, want);

    int undec = undecrypt_since(L.acc, false);
    ST_REQUIRE_MSG(undec == 0,
                   "receiver counted %d undecryptable packets after the key exchange completed",
                   undec);
    ST_REQUIRE_MSG(pkt_recv(L.acc) >= want,
                   "receiver's pktRecv is %lld, fewer than the %d messages it delivered",
                   (long long)pkt_recv(L.acc), want);
    ST_REQUIRE_MSG(L.relay->seen_fwd() >= want, "the relay forwarded only %d datagrams",
                   L.relay->seen_fwd());
}

SRT_CASE(g09_encryption, a_256_bit_key_carries_the_payload_and_reaches_the_receiver) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, rx_conf(kPassA), tx_conf(kPassA, 32));
    km_warm_to(L.cal, SRT_KM_S_SECURED);

    // The receiver never saw the key length as an option: it can only have
    // learned it from the key material the sender sent.
    int snd_len = st_get_int(L.cal, SRTO_SNDPBKEYLEN);
    ST_REQUIRE_MSG(snd_len == 32, "sender SRTO_SNDPBKEYLEN = %d, expected 32", snd_len);
    int rcv_len = st_get_int(L.acc, SRTO_RCVPBKEYLEN);
    ST_REQUIRE_MSG(rcv_len == 32,
                   "receiver SRTO_RCVPBKEYLEN = %d, expected the sender's 32 from the exchanged "
                   "key material",
                   rcv_len);

    const int want = 24;
    (void)undecrypt_since(L.acc, true);
    std::vector<std::string> sent = st_send_stream(L.cal, want, kMsgSize, kWarmGapUs);
    std::vector<std::string> got = recv_measured(L.acc, want, 4000);
    require_same_stream(sent, got, want);
    int undec = undecrypt_since(L.acc, false);
    ST_REQUIRE_MSG(undec == 0, "receiver counted %d undecryptable packets under a 256-bit key",
                   undec);
}

// The one case that must not be vacuous in either direction: it runs the very
// same marker through the very same tap twice, once in clear and once
// encrypted, and requires the marker to be visible exactly once out of the two.
// Without the clear half, a library that sent nothing would pass.
SRT_CASE(g09_encryption, the_payload_is_on_the_wire_in_clear_only_without_a_passphrase) {
    const int count = 16;

    WireResult clear = wire_run(false, count);
    ST_REQUIRE_MSG(clear.datagrams >= count, "the tap forwarded only %d datagrams in clear",
                   clear.datagrams);
    ST_REQUIRE_MSG(clear.marker_hits >= count,
                   "without a passphrase only %d of %d forwarded datagrams carried the marker; the "
                   "check cannot detect plaintext and proves nothing about the encrypted half",
                   clear.marker_hits, count);

    WireResult enc = wire_run(true, count);
    ST_REQUIRE_MSG(enc.datagrams >= count, "the tap forwarded only %d datagrams when encrypted",
                   enc.datagrams);
    ST_REQUIRE_MSG(enc.marker_hits == 0,
                   "%d of %d forwarded datagrams carried the payload marker in clear although both "
                   "sides had the same passphrase",
                   enc.marker_hits, enc.datagrams);
}

SRT_CASE(g09_encryption, a_passphrase_on_the_sender_only_delivers_nothing) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, rx_conf(nullptr), tx_conf(kPassA, 16));
    km_warm_to(L.cal, SRT_KM_S_NOSECRET);

    const int want = 16;
    (void)undecrypt_since(L.acc, true);
    std::vector<std::string> sent = st_send_stream(L.cal, want, kMsgSize, kWarmGapUs);
    std::vector<std::string> got = recv_measured(L.acc, want, 2500);
    ST_REQUIRE_MSG(got.empty(), "%d of %d messages reached a receiver that has no passphrase",
                   (int)got.size(), (int)sent.size());

    // The datagrams must have arrived and been refused, not gone missing: that
    // is what distinguishes encryption from a dead channel.
    int undec = undecrypt_since(L.acc, false);
    ST_REQUIRE_MSG(undec >= want,
                   "receiver counted %d undecryptable packets, expected at least the %d that were "
                   "sent",
                   undec, want);
    ST_REQUIRE_MSG(L.relay->seen_fwd() >= want, "the relay forwarded only %d datagrams",
                   L.relay->seen_fwd());
}

SRT_CASE(g09_encryption, two_different_passphrases_deliver_nothing) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, rx_conf(kPassA), tx_conf(kPassB, 16));
    // v1.2.0 does not refuse the connection for a bad secret; it simply never
    // decrypts. Stated here so the case cannot be read as asserting rejection.
    ST_REQUIRE_MSG(srt_getsockstate(L.cal) == SRTS_CONNECTED,
                   "caller socket state is %d, expected SRTS_CONNECTED (%d): this milestone "
                   "establishes the connection and reports BADSECRET",
                   (int)srt_getsockstate(L.cal), (int)SRTS_CONNECTED);
    km_warm_to(L.cal, SRT_KM_S_BADSECRET);

    const int want = 16;
    (void)undecrypt_since(L.acc, true);
    std::vector<std::string> sent = st_send_stream(L.cal, want, kMsgSize, kWarmGapUs);
    std::vector<std::string> got = recv_measured(L.acc, want, 2500);
    ST_REQUIRE_MSG(got.empty(), "%d of %d messages reached a receiver holding a different "
                                "passphrase",
                   (int)got.size(), (int)sent.size());

    int undec = undecrypt_since(L.acc, false);
    ST_REQUIRE_MSG(undec >= want,
                   "receiver counted %d undecryptable packets, expected at least the %d that were "
                   "sent",
                   undec, want);
}

// ===========================================================================
// g10_kmstate
// ===========================================================================

SRT_CASE(g10_kmstate, an_unsecured_connection_reports_unsecured_on_both_sides) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, rx_conf(nullptr), tx_conf(nullptr, 0));

    // A live connection, not an idle socket: the state is read off a link that
    // demonstrably carries data.
    const int want = 12;
    std::vector<std::string> sent = st_send_stream(L.cal, want, kMsgSize, kWarmGapUs);
    std::vector<std::string> got = recv_measured(L.acc, want, 4000);
    require_same_stream(sent, got, want);

    expect_km(L.cal, "sender", SRT_KM_S_UNSECURED);
    expect_km(L.acc, "receiver", SRT_KM_S_UNSECURED);
}

SRT_CASE(g10_kmstate, a_shared_passphrase_reports_secured_on_both_sides) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, rx_conf(kPassA), tx_conf(kPassA, 16));
    km_warm_to(L.cal, SRT_KM_S_SECURED);

    expect_km(L.cal, "sender", SRT_KM_S_SECURED);
    expect_km(L.acc, "receiver", SRT_KM_S_SECURED);

    // SECURED must mean the stream really decrypts, not just that a state
    // variable was assigned.
    const int want = 12;
    (void)undecrypt_since(L.acc, true);
    std::vector<std::string> sent = st_send_stream(L.cal, want, kMsgSize, kWarmGapUs);
    std::vector<std::string> got = recv_measured(L.acc, want, 4000);
    require_same_stream(sent, got, want);
    int undec = undecrypt_since(L.acc, false);
    ST_REQUIRE_MSG(undec == 0, "state is SECURED but %d packets could not be decrypted", undec);
}

SRT_CASE(g10_kmstate, a_receiver_without_a_passphrase_reports_nosecret_on_both_sides) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, rx_conf(nullptr), tx_conf(kPassA, 16));
    km_warm_to(L.cal, SRT_KM_S_NOSECRET);

    expect_km(L.cal, "sender", SRT_KM_S_NOSECRET);
    expect_km(L.acc, "receiver", SRT_KM_S_NOSECRET);

    int undec = undecrypt_since(L.acc, false);
    ST_REQUIRE_MSG(undec > 0,
                   "state is NOSECRET but the receiver counted no undecryptable packets, so no "
                   "encrypted payload ever reached it");
}

SRT_CASE(g10_kmstate, a_mismatched_passphrase_reports_badsecret_on_both_sides) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, rx_conf(kPassA), tx_conf(kPassB, 16));
    km_warm_to(L.cal, SRT_KM_S_BADSECRET);

    expect_km(L.cal, "sender", SRT_KM_S_BADSECRET);
    expect_km(L.acc, "receiver", SRT_KM_S_BADSECRET);

    int undec = undecrypt_since(L.acc, false);
    ST_REQUIRE_MSG(undec > 0,
                   "state is BADSECRET but the receiver counted no undecryptable packets, so no "
                   "encrypted payload ever reached it");
}

// SRTO_KMSTATE is the pre-TWOWAY name for a socket's own decryption state, the
// same quantity SRTO_RCVKMSTATE reports. At this milestone the library answers
// only the newer name: the getOpt switch in core.cpp has no arm for
// SRTO_KMSTATE at all, so reading it reports SRT_EINVOP. No document in the
// milestone's set fixes either outcome, so the assertion here is the one thing
// that must hold under both readings -- the older name must never contradict
// the newer one -- checked on a fresh socket and on each of the three connected
// scenarios the group covers.
static void kmstate_agrees(SRTSOCKET s, const char* who) {
    int rcv = st_get_int(s, SRTO_RCVKMSTATE);
    int v = -1;
    if (st_try_get_int(s, SRTO_KMSTATE, &v) == SRT_ERROR) {
        int e = srt_getlasterror(nullptr);
        srt_clearlasterror();
        ST_REQUIRE_MSG(e == SRT_EINVOP,
                       "%s SRTO_KMSTATE was refused with error %d (%s); this milestone either "
                       "reports the local KM state %d (%s) or refuses the option with SRT_EINVOP "
                       "(%d)",
                       who, e, srt_strerror(e, 0), rcv, km_name(rcv), (int)SRT_EINVOP);
        return;
    }
    ST_REQUIRE_MSG(v == rcv,
                   "%s SRTO_KMSTATE = %d (%s) but SRTO_RCVKMSTATE = %d (%s); the two names for the "
                   "local KM state disagree",
                   who, v, km_name(v), rcv, km_name(rcv));
}

SRT_CASE(g10_kmstate, kmstate_never_contradicts_rcvkmstate) {
    SRTSOCKET fresh = st_new_socket();
    ST_REQUIRE_MSG(fresh != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    kmstate_agrees(fresh, "a fresh socket");
    srt_close(fresh);

    struct Row {
        const char* label;
        const char* rx_pass;
        const char* tx_pass;
        int keylen;
        int want;
    };
    const Row rows[] = {
        {"unsecured", nullptr, nullptr, 0, SRT_KM_S_UNSECURED},
        {"secured", kPassA, kPassA, 16, SRT_KM_S_SECURED},
        {"bad secret", kPassA, kPassB, 16, SRT_KM_S_BADSECRET},
    };

    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        const Row& r = rows[i];
        Link L;
        RelaySched sched;
        st_connect(L, sched, rx_conf(r.rx_pass), tx_conf(r.tx_pass, r.keylen));
        if (r.keylen != 0) {
            km_warm_to(L.cal, r.want);
        } else {
            // Nothing to exchange; prove the link is live before reading it.
            std::vector<std::string> sent = st_send_stream(L.cal, 8, kMsgSize, kWarmGapUs);
            std::vector<std::string> got = recv_measured(L.acc, 8, 4000);
            require_same_stream(sent, got, 8);
        }
        char who[96];
        std::snprintf(who, sizeof who, "the %s sender", r.label);
        expect_km(L.cal, who, r.want);
        kmstate_agrees(L.cal, who);
        std::snprintf(who, sizeof who, "the %s receiver", r.label);
        expect_km(L.acc, who, r.want);
        kmstate_agrees(L.acc, who);
    }
}
