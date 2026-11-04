// Connection and transfer helpers shared by every family driver at every step.
//
// One rule governs this file: it must compile unchanged against every tag in
// the chain, v1.2.0 through v1.5.7. So it uses only the API elements present
// at all eleven checkpoints.
// In particular it must not mention SRTO_PAYLOADSIZE, SRTO_TRANSTYPE,
// SRTO_MESSAGEAPI, SRTO_STREAMID or anything under SRT_REJ_*, none of which
// exist at v1.2.0; and not SRTO_CC, SRTO_MSGTTL, SRTO_TSBPDDELAY,
// SRTO_SMOOTHER, SRTO_TWOWAYDATA or SRTO_SNDPEERKMSTATE, all of which are
// gone by v1.4.2. A step that needs a version-specific option names it in its
// own fam_*.cpp, where it is compiled only against the version that has it.
//
// The second rule is in relay.h: never drain a socket with a fixed sleep.
// Every receive here is bounded by SRTO_RCVTIMEO and driven by the receiver.
#pragma once

#include <srt/srt.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "relay.h"
#include "stest.h"

// Options a caller wants applied before bind/connect. Applied to the listener
// and to the caller separately, because several groups need them to disagree.
typedef std::function<void(SRTSOCKET)> SockConf;

// How often the acceptor loop offers srt_accept again. Measured at every
// prefix in this ladder (v1.2.0 through v1.5.7): a BLOCKING srt_accept does
// not consult SRTO_RCVTIMEO at all -- it waits on the listener's
// queued-socket condition variable and returns only when a socket arrives or
// the listener is closed -- while a listener with SRTO_RCVSYN = 0 returns
// SRT_EASYNCRCV immediately. So the accept is polled, not timed out, and the
// listener's RCVTIMEO is not part of the contract.
//
// 25 ms rather than something tighter because SRT logs every empty
// non-blocking accept at error level, and a refusal case sitting out its
// whole grace at 5 ms would bury its own failure message under hundreds of
// log lines.
static const int ST_ACCEPT_POLL_MS = 25;

inline void st_noconf(SRTSOCKET) {}

inline void st_set_int(SRTSOCKET s, SRT_SOCKOPT o, int v) {
    if (srt_setsockflag(s, o, &v, sizeof v) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "setsockflag(%d, %d) failed: %s", (int)o, v,
                 srt_getlasterror_str());
}

inline int st_get_int(SRTSOCKET s, SRT_SOCKOPT o) {
    int v = -1;
    int n = (int)sizeof v;
    if (srt_getsockflag(s, o, &v, &n) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "getsockflag(%d) failed: %s", (int)o,
                 srt_getlasterror_str());
    if (n != (int)sizeof(int))
        st_failf(__FILE__, __LINE__, "getsockflag(%d) returned %d bytes, expected %zu",
                 (int)o, n, sizeof(int));
    return v;
}

// Non-throwing variants, for the groups whose subject IS the failure.
inline int st_try_set_int(SRTSOCKET s, SRT_SOCKOPT o, int v) {
    return srt_setsockflag(s, o, &v, sizeof v);
}

inline int st_try_get_int(SRTSOCKET s, SRT_SOCKOPT o, int* out) {
    int n = (int)sizeof(int);
    return srt_getsockflag(s, o, out, &n);
}

inline void st_set_str(SRTSOCKET s, SRT_SOCKOPT o, const std::string& v) {
    if (srt_setsockflag(s, o, v.data(), (int)v.size()) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "setsockflag(%d, \"%s\") failed: %s", (int)o, v.c_str(),
                 srt_getlasterror_str());
}

// `srt_socket` is the only socket constructor present at every tag:
// `srt_create_socket` arrives at v1.3.0, and from v1.5.x the three-argument
// form carries a deprecation attribute. It is deprecated, never removed, and
// still the documented equivalent, so the kit keeps using it and silences the
// attribute here rather than in every driver. A step whose subject IS the
// newer constructor calls it directly in its own fam_*.cpp.
inline SRTSOCKET st_new_socket() {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    return srt_socket(AF_INET, SOCK_DGRAM, 0);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}

inline sockaddr_in st_loopback(uint16_t port) {
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    return a;
}

// The accept every family in this ladder uses. Two things it gets right that a
// bare `std::thread([&]{ acc = srt_accept(lsn, ...); })` does not:
//
//   * it is BOUNDED. A blocking srt_accept ignores SRTO_RCVTIMEO (see
//     ST_ACCEPT_POLL_MS), and a connection refused during the handshake never
//     enters the listener's queue, so a blocking acceptor waiting for one that
//     will never arrive returns only when the case's own alarm fires, and the
//     case reports `FAIL timed out after 60 s` instead of the refusal.
//   * its patience is tied to the CALLER, not to a clock. While srt_connect is
//     still in flight the acceptor simply keeps offering; once the attempt has
//     returned, a grace is allowed for the listener to produce its socket and
//     then the accept gives up. Nothing here has to be beaten by a deadline, so
//     a busy host cannot turn a passing case into a failing one.
//
// `caller_done` must be stored true by the thread running srt_connect, the
// moment it returns -- otherwise this never gives up. The returned socket is
// put back into blocking receive mode: SRTO_RCVSYN is a pre option and the
// accepted socket inherits the listener's value.
inline SRTSOCKET st_accept_polled(SRTSOCKET lsn, const std::atomic<bool>& caller_done,
                                  sockaddr* addr = nullptr, int* addrlen = nullptr,
                                  int grace_ms = 3000) {
    const int off = 0;
    srt_setsockflag(lsn, SRTO_RCVSYN, &off, sizeof off);
    bool grace_started = false;
    std::chrono::steady_clock::time_point deadline;
    for (;;) {
        int len = addrlen != nullptr ? *addrlen : 0;
        SRTSOCKET a = srt_accept(lsn, addr, addrlen != nullptr ? &len : nullptr);
        if (a != SRT_INVALID_SOCK) {
            if (addrlen != nullptr) *addrlen = len;
            const int on = 1;
            srt_setsockflag(a, SRTO_RCVSYN, &on, sizeof on);
            return a;
        }
        if (caller_done.load()) {
            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            if (!grace_started) {
                grace_started = true;
                deadline = now + std::chrono::milliseconds(grace_ms);
            } else if (now >= deadline) {
                return SRT_INVALID_SOCK;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ST_ACCEPT_POLL_MS));
    }
}

// A listener, a caller, the socket the listener accepted, and the relay that
// sits between them. Non-copyable; closed in reverse order of creation.
struct Link {
    SRTSOCKET lsn = SRT_INVALID_SOCK;
    SRTSOCKET cal = SRT_INVALID_SOCK;
    SRTSOCKET acc = SRT_INVALID_SOCK;
    Relay* relay = nullptr;
    int connect_rc = -1;      // what srt_connect returned
    int connect_err = 0;      // srt_getlasterror() at that moment

    Link() {}
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;
    ~Link() { close(); }

    void close() {
        if (acc != SRT_INVALID_SOCK) { srt_close(acc); acc = SRT_INVALID_SOCK; }
        if (cal != SRT_INVALID_SOCK) { srt_close(cal); cal = SRT_INVALID_SOCK; }
        if (lsn != SRT_INVALID_SOCK) { srt_close(lsn); lsn = SRT_INVALID_SOCK; }
        delete relay;
        relay = nullptr;
    }
};

// Brings up listener -> relay -> caller and accepts, without asserting that it
// worked: the return value is srt_connect's, and `L.acc` is SRT_INVALID_SOCK
// if nothing was accepted. Groups that expect a refusal use this directly;
// groups that expect a connection use st_connect below.
//
// The accept runs on its own thread because srt_accept blocks and the caller
// must already be connecting for the handshake to complete.
inline int st_try_connect(Link& L, const RelaySched& sched, const SockConf& lconf,
                          const SockConf& cconf, int accept_grace_ms = 3000) {
    L.lsn = st_new_socket();
    ST_REQUIRE_MSG(L.lsn != SRT_INVALID_SOCK, "srt_socket (listener): %s",
                   srt_getlasterror_str());
    // The listener accepts non-blocking, and is put that way BEFORE the
    // group's own configuration runs, so no SockConf can quietly put it back
    // into blocking mode and remove the acceptor's only bound.
    const int rcvsyn_off = 0;
    srt_setsockflag(L.lsn, SRTO_RCVSYN, &rcvsyn_off, sizeof rcvsyn_off);
    lconf(L.lsn);

    sockaddr_in la = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(L.lsn, (sockaddr*)&la, sizeof la) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    int nl = (int)sizeof la;
    ST_REQUIRE_MSG(srt_getsockname(L.lsn, (sockaddr*)&la, &nl) != SRT_ERROR,
                   "srt_getsockname: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(L.lsn, 1) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());

    L.relay = new Relay(ntohs(la.sin_port), sched);

    // The caller socket is built BEFORE the acceptor starts, so the acceptor's
    // lifetime contains the handshake and nothing else.
    L.cal = st_new_socket();
    ST_REQUIRE_MSG(L.cal != SRT_INVALID_SOCK, "srt_socket (caller): %s", srt_getlasterror_str());
    cconf(L.cal);

    SRTSOCKET accepted = SRT_INVALID_SOCK;
    std::atomic<bool> connect_done(false);
    std::thread acceptor(
        [&] { accepted = st_accept_polled(L.lsn, connect_done, nullptr, nullptr,
                                          accept_grace_ms); });

    sockaddr_in ra = st_loopback(L.relay->port());
    L.connect_rc = srt_connect(L.cal, (sockaddr*)&ra, sizeof ra);
    L.connect_err = srt_getlasterror(nullptr);
    connect_done.store(true);
    acceptor.join();
    L.acc = accepted;
    return L.connect_rc;
}

// The same, asserting that the connection came up on both sides.
inline void st_connect(Link& L, const RelaySched& sched, const SockConf& lconf = st_noconf,
                       const SockConf& cconf = st_noconf, int accept_grace_ms = 3000) {
    st_try_connect(L, sched, lconf, cconf, accept_grace_ms);
    ST_REQUIRE_MSG(L.connect_rc != SRT_ERROR, "srt_connect: %s", srt_strerror(L.connect_err, 0));
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK, "srt_accept: %s", srt_getlasterror_str());
}

// A payload that is different in every message and cheap to check: the message
// index in the first bytes, then a byte pattern derived from it. A group that
// compares whole messages therefore catches reordering and substitution, not
// just loss.
inline std::string st_payload(int index, int size) {
    // Below 32 bytes the index prefix and the pattern would overlap and two
    // different messages could compare equal, so the comparison groups would
    // stop detecting substitution.
    ST_REQUIRE_MSG(size >= 32, "st_payload: size %d is below the 32-byte minimum", size);
    std::string s(size, '\0');
    std::snprintf(&s[0], (size_t)size, "MSG%08d", index);
    for (int i = 16; i < size; i++) s[(size_t)i] = (char)('A' + ((index + i) % 26));
    return s;
}

// Sends `count` messages of `size` bytes, pacing them `gap_us` apart so the
// sender does not outrun a live-mode rate limiter. Returns the payloads sent.
inline std::vector<std::string> st_send_stream(SRTSOCKET s, int count, int size, int gap_us) {
    std::vector<std::string> sent;
    sent.reserve((size_t)count);
    for (int i = 0; i < count; i++) {
        std::string m = st_payload(i, size);
        int rc = srt_sendmsg(s, m.data(), (int)m.size(), -1, 1);
        ST_REQUIRE_MSG(rc == (int)m.size(), "srt_sendmsg #%d returned %d: %s", i, rc,
                       srt_getlasterror_str());
        sent.push_back(m);
        if (gap_us > 0) std::this_thread::sleep_for(std::chrono::microseconds(gap_us));
    }
    return sent;
}

// Receives until `want` messages have arrived or a receive times out. The
// timeout is the exit condition, never a sleep: SRTO_RCVTIMEO is set here so
// that a stalled channel costs `timeo_ms`, not the case budget.
inline std::vector<std::string> st_recv_stream(SRTSOCKET s, int want, int size, int timeo_ms) {
    st_set_int(s, SRTO_RCVTIMEO, timeo_ms);
    std::vector<std::string> got;
    std::string buf((size_t)size + 64, '\0');
    while ((int)got.size() < want) {
        int rc = srt_recvmsg(s, &buf[0], (int)buf.size());
        if (rc == SRT_ERROR) break;
        if (rc == 0) break;
        got.push_back(buf.substr(0, (size_t)rc));
    }
    return got;
}

// How many of `sent` arrived, in order, with no substitution. Returns the
// index of the first mismatch, or -1 when `got` is a prefix-preserving
// subsequence of `sent`.
inline int st_first_out_of_order(const std::vector<std::string>& sent,
                                 const std::vector<std::string>& got) {
    size_t si = 0;
    for (size_t gi = 0; gi < got.size(); gi++) {
        while (si < sent.size() && sent[si] != got[gi]) si++;
        if (si == sent.size()) return (int)gi;
        si++;
    }
    return -1;
}

// The live-transport defaults every transfer group starts from: TSBPD on with
// a latency generous enough that the relay's schedule, not the clock, decides
// what arrives. Groups that want to observe a drop override these.
inline void st_live_lossless(SRTSOCKET s) {
    st_set_int(s, SRTO_TSBPDMODE, 1);
    st_set_int(s, SRTO_LATENCY, 900);
    st_set_int(s, SRTO_TLPKTDROP, 0);
}
