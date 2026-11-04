// Step 04 (v1.3.3) -- family `hook`: the listener accept callback.
//
// Groups, all tier `new` (scored.txt):
//   g06_listen_install  install returns 0, a NULL hook returns -1/EINVPARAM,
//                       and the hook fires exactly once per incoming
//                       connection with the caller's address, the caller's
//                       stream id, handshake version 5 and the install-time
//                       opaque pointer.
//   g07_listen_reject   a hook returning -1 fails the caller's srt_connect
//                       with SRT_ECONNREJ, leaves srt_accept with nothing, and
//                       does not break the listener for the next connection.
//   g08_listen_retval   only the exact value -1 rejects; 0, 1 and -2 accept.
//
// The family is uniformly `new`, so this file is NOT made to compile at
// v1.3.2: it names `srt_listen_callback` directly rather than resolving it
// with dlsym, and it restates no constant. DRIVER_RULES' dlsym/#ifndef rules
// exist to protect `inherited` and `peripheral` groups sharing a translation
// unit with a `new` one; there are none here. A driver that still built and
// passed at v1.3.2 would be a driver that fails to discriminate, so the
// intended v1.3.2 outcome is a compile error on the undeclared symbol.
//
// ---------------------------------------------------------------- why not srtkit's Link
//
// `st_connect`/`st_try_connect` always interpose a `Relay`, which re-sources
// every datagram from the relay's own UDP socket. The hook's `peeraddr` would
// then be the relay's forwarding port, not the caller's, and
// g06_listen_install's central assertion -- "the peer address is the caller's
// address" (interface.md) -- could not be written. These groups need no lossy
// channel, so they connect straight over loopback with the small helpers
// below. Everything else (st_new_socket, st_loopback, st_set_int, st_set_str,
// st_payload, st_recv_stream) comes from srtkit.h unchanged.
//
// ---------------------------------------------------------------- why accept is epoll-driven
//
// At v1.3.3 `srt_accept` does NOT honour SRTO_RCVTIMEO: CUDTUnited::accept
// loops on an untimed `pthread_cond_wait(&m_AcceptCond)` and only consults
// m_bSynRecving (SRTO_RCVSYN) to decide whether to block at all. A blocking
// accept on a rejected handshake therefore hangs until the case alarm, and the
// group would report a timeout instead of the refusal it was written to
// observe. So every listener here sets SRTO_RCVSYN=0 and the wait is a bounded
// `srt_epoll_wait` on the listener's SRT_EPOLL_IN -- an event wait, not a
// sleep, as DRIVER_RULES 3 and 4 require. srtkit.h's own comment about
// SRTO_RCVTIMEO bounding srt_accept does not hold at this tag; the header is
// shared and is not edited here.
//
// ---------------------------------------------------------------- not asserted here
//
//   * that the hook runs on the receiver worker thread, and that an option set
//     inside the hook takes effect for that connection. interface.md states
//     both, but neither is named by any of the three group descriptions and
//     "one subject per group" (DRIVER_RULES 7) rules them out of these.
//   * the `ns` argument. Nothing the candidate is told says how it relates to
//     the socket `srt_accept` later returns, so it is recorded and not
//     asserted.
//   * a rejection *reason*. SRT_REJ_* arrives at v1.4.0; at this tag the
//     caller learns only SRT_ECONNREJ.
//   * installing a hook on a non-listening or closed socket: undocumented.
#include "stest.h"
#include "srtkit.h"

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// ------------------------------------------------------------------ recorder
//
// The hook is called from SRT's receiver worker thread while the handshake is
// still in flight. It must therefore never throw: `runAcceptHook` wraps the
// call in `catch (...)` and turns any exception into a rejection, so an
// ST_REQUIRE fired from inside would silently change the behaviour under test.
// It only records; every assertion is made on the main thread afterwards.
namespace {

struct HookHit {
    void* opaque = nullptr;
    SRTSOCKET ns = SRT_INVALID_SOCK;
    int hsversion = -1;
    bool streamid_null = true;
    std::string streamid;
    sockaddr_in peer;
    int peer_family = -1;
};

std::mutex g_mtx;
std::vector<HookHit> g_hits;
// Return value per hit index; the last entry repeats for any further hit, so a
// case that plans {-1, 0} rejects the first connection and accepts every one
// after it.
std::vector<int> g_plan;

void hk_reset(const std::vector<int>& plan) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_hits.clear();
    g_plan = plan;
}

size_t hk_count() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_hits.size();
}

HookHit hk_hit(size_t i) {
    std::lock_guard<std::mutex> lk(g_mtx);
    ST_REQUIRE_MSG(i < g_hits.size(), "wanted hook hit #%zu but the hook fired %zu time(s)", i,
                   g_hits.size());
    return g_hits[i];
}

int hk_hook(void* opaq, SRTSOCKET ns, int hsversion, const struct sockaddr* peeraddr,
            const char* streamid) {
    std::lock_guard<std::mutex> lk(g_mtx);
    HookHit h;
    std::memset(&h.peer, 0, sizeof h.peer);
    h.opaque = opaq;
    h.ns = ns;
    h.hsversion = hsversion;
    h.streamid_null = (streamid == nullptr);
    if (streamid) h.streamid.assign(streamid);
    if (peeraddr) {
        h.peer_family = peeraddr->sa_family;
        if (peeraddr->sa_family == AF_INET)
            std::memcpy(&h.peer, peeraddr, sizeof(sockaddr_in));
    }
    const size_t idx = g_hits.size();
    g_hits.push_back(h);
    if (g_plan.empty()) return 0;
    return idx < g_plan.size() ? g_plan[idx] : g_plan.back();
}

// ------------------------------------------------------------------- timings
//
// One-sided and generous, per DRIVER_RULES 3. ACCEPT_WAIT is how long a case
// waits for a connection it expects to arrive; QUIET_WAIT is how long it
// watches a listener that must stay empty. Neither is a synchronisation sleep:
// ACCEPT_WAIT returns as soon as the listener signals, and QUIET_WAIT is the
// full cost only on the two cases that assert a refusal.
const int HK_ACCEPT_WAIT_MS = 5000;
const int HK_QUIET_WAIT_MS = 1500;
const int HK_CONNTIMEO_MS = 4000;
const int HK_RECV_TIMEO_MS = 4000;
// st_recv_stream allocates size + 64, and live-mode srt_recvmsg refuses a
// buffer smaller than the negotiated 1316-byte payload, so a smaller message
// would read as an empty channel rather than as a failure.
const int HK_MSG = 1300;

struct Listener {
    SRTSOCKET s = SRT_INVALID_SOCK;
    int eid = -1;
    sockaddr_in addr;

    Listener() { std::memset(&addr, 0, sizeof addr); }
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    ~Listener() {
        if (eid >= 0) srt_epoll_release(eid);
        if (s != SRT_INVALID_SOCK) srt_close(s);
    }

    void open() {
        s = st_new_socket();
        ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket (listener): %s", srt_getlasterror_str());
        // Non-blocking accept; see the header comment.
        st_set_int(s, SRTO_RCVSYN, 0);
        sockaddr_in a = st_loopback(0);
        ST_REQUIRE_MSG(srt_bind(s, (sockaddr*)&a, sizeof a) != SRT_ERROR, "srt_bind (listener): %s",
                       srt_getlasterror_str());
        int n = (int)sizeof a;
        ST_REQUIRE_MSG(srt_getsockname(s, (sockaddr*)&a, &n) != SRT_ERROR,
                       "srt_getsockname (listener): %s", srt_getlasterror_str());
        ST_REQUIRE_MSG(srt_listen(s, 5) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());
        addr = a;
        eid = srt_epoll_create();
        ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
        // SRT_EPOLL_IN only. Readability on a listening socket is exactly
        // "a connection is queued for srt_accept", which is the documented
        // claim these groups make; adding SRT_EPOLL_ERR would let an
        // unrelated error event stand in for an accepted connection.
        int ev = SRT_EPOLL_IN;
        ST_REQUIRE_MSG(srt_epoll_add_usock(eid, s, &ev) != SRT_ERROR, "srt_epoll_add_usock: %s",
                       srt_getlasterror_str());
    }
};

// What a bounded attempt to accept produced. `sock` is SRT_INVALID_SOCK when
// the listener queued nothing within the window, and `err` is srt_accept's
// reason (SRT_EASYNCRCV when there was simply nothing to take).
struct AcceptOutcome {
    int epoll_rc = -1;
    SRTSOCKET sock = SRT_INVALID_SOCK;
    int err = 0;
};

AcceptOutcome hk_accept_within(const Listener& L, int timeo_ms) {
    AcceptOutcome o;
    SRTSOCKET ready[4];
    int nready = 4;
    o.epoll_rc = srt_epoll_wait(L.eid, ready, &nready, nullptr, nullptr, timeo_ms, nullptr, nullptr,
                                nullptr, nullptr);
    // On timeout srt_epoll_wait returns -1 and leaves `nready` untouched, so
    // the return value is the only honest signal here.
    if (o.epoll_rc <= 0) {
        o.err = SRT_EASYNCRCV;
        return o;
    }
    o.sock = srt_accept(L.s, nullptr, nullptr);
    o.err = srt_getlasterror(nullptr);
    return o;
}

struct Caller {
    SRTSOCKET s = SRT_INVALID_SOCK;
    sockaddr_in local;     // the address the hook must report as the peer
    int rc = -1;
    int err = 0;

    Caller() { std::memset(&local, 0, sizeof local); }
    Caller(const Caller&) = delete;
    Caller& operator=(const Caller&) = delete;
    ~Caller() {
        if (s != SRT_INVALID_SOCK) srt_close(s);
    }

    // `streamid` may be null, which is the "caller set none" case.
    void dial(const Listener& L, const char* streamid) {
        s = st_new_socket();
        ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket (caller): %s", srt_getlasterror_str());
        st_set_int(s, SRTO_CONNTIMEO, HK_CONNTIMEO_MS);
        if (streamid) st_set_str(s, SRTO_STREAMID, streamid);
        // Bound explicitly so the case knows the exact address the hook is
        // required to report, rather than inferring it after the fact.
        sockaddr_in ca = st_loopback(0);
        ST_REQUIRE_MSG(srt_bind(s, (sockaddr*)&ca, sizeof ca) != SRT_ERROR, "srt_bind (caller): %s",
                       srt_getlasterror_str());
        int n = (int)sizeof ca;
        ST_REQUIRE_MSG(srt_getsockname(s, (sockaddr*)&ca, &n) != SRT_ERROR,
                       "srt_getsockname (caller): %s", srt_getlasterror_str());
        local = ca;
        rc = srt_connect(s, (sockaddr*)&L.addr, sizeof L.addr);
        err = srt_getlasterror(nullptr);
    }
};

// srt_strerror() formats into a single process-wide static CUDTException and
// returns a pointer to it, so two calls in one printf argument list both come
// back as whichever code was formatted last -- an "expected X, got Y" message
// that renders X and Y identically. Copy the text before the next call.
std::string hk_errname(int code) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%d (%s)", code, srt_strerror(code, 0));
    return std::string(buf);
}

std::string hk_ipport(const sockaddr_in& a) {
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
    char out[64];
    std::snprintf(out, sizeof out, "%s:%d", ip, (int)ntohs(a.sin_port));
    return std::string(out);
}

// The positive control every accept case carries: an accepted socket that
// cannot actually carry a message is not an accepted connection. One message,
// compared byte for byte, distinguishes arrival from an empty channel
// (DRIVER_RULES 1).
void hk_expect_payload(SRTSOCKET from, SRTSOCKET to, int index, const char* what) {
    const std::string m = st_payload(index, HK_MSG);
    const int rc = srt_sendmsg(from, m.data(), (int)m.size(), -1, 1);
    ST_REQUIRE_MSG(rc == (int)m.size(), "%s: srt_sendmsg returned %d, expected %d: %s", what, rc,
                   (int)m.size(), srt_getlasterror_str());
    const std::vector<std::string> got = st_recv_stream(to, 1, HK_MSG, HK_RECV_TIMEO_MS);
    ST_REQUIRE_MSG(got.size() == 1, "%s: expected 1 message on the accepted socket, got %zu", what,
                   got.size());
    ST_REQUIRE_MSG(got[0] == m, "%s: payload differs from what was sent (%zu bytes in, %zu out)",
                   what, m.size(), got[0].size());
}

void hk_expect_connected(const Caller& C, const char* what) {
    ST_REQUIRE_MSG(C.rc != SRT_ERROR, "%s: srt_connect failed with %s, expected success", what,
                   hk_errname(C.err).c_str());
}

void hk_expect_rejected(const Caller& C, const char* what) {
    ST_REQUIRE_MSG(C.rc == SRT_ERROR, "%s: srt_connect returned %d (success), expected failure",
                   what, C.rc);
    const std::string got = hk_errname(C.err);
    const std::string want = hk_errname(SRT_ECONNREJ);
    ST_REQUIRE_MSG(C.err == SRT_ECONNREJ, "%s: srt_connect failed with %s, expected %s", what,
                   got.c_str(), want.c_str());
}

void hk_expect_nothing_to_accept(const Listener& L, const char* what) {
    const AcceptOutcome o = hk_accept_within(L, HK_QUIET_WAIT_MS);
    ST_REQUIRE_MSG(o.sock == SRT_INVALID_SOCK,
                   "%s: srt_accept produced socket %d within %d ms, expected nothing", what,
                   (int)o.sock, HK_QUIET_WAIT_MS);
    ST_REQUIRE_MSG(o.epoll_rc <= 0,
                   "%s: the listener signalled readable (epoll rc %d) with nothing accepted", what,
                   o.epoll_rc);
}

SRTSOCKET hk_expect_accepted(const Listener& L, const char* what) {
    const AcceptOutcome o = hk_accept_within(L, HK_ACCEPT_WAIT_MS);
    ST_REQUIRE_MSG(o.sock != SRT_INVALID_SOCK,
                   "%s: srt_accept yielded nothing within %d ms (epoll rc %d, %s), expected a socket",
                   what, HK_ACCEPT_WAIT_MS, o.epoll_rc, hk_errname(o.err).c_str());
    // The accepted socket inherits the listener's options, including the
    // SRTO_RCVSYN=0 that made the accept non-blocking. Left as it is,
    // srt_recvmsg returns SRT_EASYNCRCV at once and SRTO_RCVTIMEO is never
    // consulted, so the payload control would read an empty channel on a
    // perfectly good connection. Restore blocking receive here, once, rather
    // than in every case.
    st_set_int(o.sock, SRTO_RCVSYN, 1);
    return o.sock;
}

}  // namespace

// ===================================================== g06_listen_install ===

// The install call itself: the success return, the NULL refusal beside it as
// the negative, and then a real connection proving the successful install is
// still the one in force -- without which "returns 0" could be any function
// that returns 0.
SRT_CASE(g06_listen_install, install_returns_zero_and_null_hook_is_refused) {
    hk_reset({0});
    Listener L;
    L.open();

    int token = 0x5A5A04;
    const int rc_install = srt_listen_callback(L.s, &hk_hook, &token);
    ST_REQUIRE_MSG(rc_install == 0, "srt_listen_callback(lsn, hook, opaque) returned %d (%s), expected 0",
                   rc_install, srt_getlasterror_str());

    const int rc_null = srt_listen_callback(L.s, nullptr, nullptr);
    const int err_null = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc_null == -1, "srt_listen_callback(lsn, NULL, NULL) returned %d, expected -1",
                   rc_null);
    const std::string got_null = hk_errname(err_null);
    const std::string want_null = hk_errname(SRT_EINVPARAM);
    ST_REQUIRE_MSG(err_null == SRT_EINVPARAM,
                   "srt_listen_callback(lsn, NULL, NULL) reported %s, expected %s",
                   got_null.c_str(), want_null.c_str());

    // A NULL opaque with a real hook is legal -- only the hook pointer is
    // checked -- so the refusal above must be about the hook and nothing else.
    const int rc_null_opaque = srt_listen_callback(L.s, &hk_hook, nullptr);
    ST_REQUIRE_MSG(rc_null_opaque == 0,
                   "srt_listen_callback(lsn, hook, NULL) returned %d (%s), expected 0",
                   rc_null_opaque, srt_getlasterror_str());

    // Re-install the token hook and show the installation is live.
    ST_REQUIRE_MSG(srt_listen_callback(L.s, &hk_hook, &token) == 0,
                   "re-installing the hook failed: %s", srt_getlasterror_str());
    Caller C;
    C.dial(L, "sid-install-live");
    hk_expect_connected(C, "install");
    const SRTSOCKET acc = hk_expect_accepted(L, "install");
    ST_REQUIRE_MSG(hk_count() == 1, "the installed hook fired %zu time(s), expected 1", hk_count());
    ST_REQUIRE_MSG(hk_hit(0).opaque == &token,
                   "the hook was called with opaque %p, expected the re-installed %p",
                   hk_hit(0).opaque, (void*)&token);
    hk_expect_payload(C.s, acc, 1, "install");
    srt_close(acc);
}

// Every argument the description names, on one connection.
SRT_CASE(g06_listen_install, hook_receives_peer_streamid_hsversion_and_opaque) {
    hk_reset({0});
    Listener L;
    L.open();

    int token = 0x04C0FFEE;
    ST_REQUIRE_MSG(srt_listen_callback(L.s, &hk_hook, &token) == 0, "srt_listen_callback: %s",
                   srt_getlasterror_str());

    const char* kSid = "sid-install-04";
    Caller C;
    C.dial(L, kSid);
    hk_expect_connected(C, "hook-args");
    const SRTSOCKET acc = hk_expect_accepted(L, "hook-args");

    ST_REQUIRE_MSG(hk_count() == 1, "the hook fired %zu time(s) for one connection, expected 1",
                   hk_count());
    const HookHit h = hk_hit(0);

    ST_REQUIRE_MSG(h.opaque == &token, "hook opaque was %p, expected the pointer passed at install %p",
                   h.opaque, (void*)&token);
    ST_REQUIRE_MSG(*(const int*)h.opaque == token,
                   "hook opaque dereferenced to 0x%X, expected 0x%X", *(const int*)h.opaque, token);
    ST_REQUIRE_EQ_INT(h.hsversion, 5);
    ST_REQUIRE_MSG(!h.streamid_null, "hook streamid was NULL, expected \"%s\"", kSid);
    ST_REQUIRE_MSG(h.streamid == kSid, "hook streamid was \"%s\", expected \"%s\"",
                   h.streamid.c_str(), kSid);
    ST_REQUIRE_MSG(h.peer_family == AF_INET, "hook peeraddr family was %d, expected AF_INET (%d)",
                   h.peer_family, (int)AF_INET);
    ST_REQUIRE_MSG(h.peer.sin_addr.s_addr == C.local.sin_addr.s_addr &&
                       h.peer.sin_port == C.local.sin_port,
                   "hook peeraddr was %s, expected the caller's own address %s",
                   hk_ipport(h.peer).c_str(), hk_ipport(C.local).c_str());

    hk_expect_payload(C.s, acc, 2, "hook-args");
    srt_close(acc);
}

// "Exactly once per incoming connection": two connections on one listener give
// exactly two hits, in order, and the count is re-read after payload has
// crossed both links so that a hook firing per handshake packet -- or per data
// packet -- would show up. The second caller sets no stream id, which is the
// empty-string half of the description.
SRT_CASE(g06_listen_install, hook_fires_once_per_connection_and_reports_empty_streamid) {
    hk_reset({0});
    Listener L;
    L.open();
    int token = 0x04AB;
    ST_REQUIRE_MSG(srt_listen_callback(L.s, &hk_hook, &token) == 0, "srt_listen_callback: %s",
                   srt_getlasterror_str());

    const char* kSid = "sid-first-only";
    Caller C1;
    C1.dial(L, kSid);
    hk_expect_connected(C1, "first");
    const SRTSOCKET acc1 = hk_expect_accepted(L, "first");
    ST_REQUIRE_MSG(hk_count() == 1, "after one connection the hook had fired %zu time(s), expected 1",
                   hk_count());

    Caller C2;
    C2.dial(L, nullptr);
    hk_expect_connected(C2, "second");
    const SRTSOCKET acc2 = hk_expect_accepted(L, "second");
    ST_REQUIRE_MSG(hk_count() == 2,
                   "after two connections the hook had fired %zu time(s), expected 2", hk_count());

    const HookHit h1 = hk_hit(0);
    const HookHit h2 = hk_hit(1);
    ST_REQUIRE_MSG(h1.streamid == kSid, "first hook streamid was \"%s\", expected \"%s\"",
                   h1.streamid.c_str(), kSid);
    ST_REQUIRE_MSG(!h2.streamid_null,
                   "second hook streamid was NULL, expected the empty string for a caller that set none");
    ST_REQUIRE_MSG(h2.streamid.empty(),
                   "second hook streamid was \"%s\", expected \"\" for a caller that set none",
                   h2.streamid.c_str());
    ST_REQUIRE_MSG(h2.peer.sin_port != h1.peer.sin_port,
                   "both hooks reported peer %s; the two callers have different addresses",
                   hk_ipport(h1.peer).c_str());
    ST_REQUIRE_MSG(h2.peer.sin_addr.s_addr == C2.local.sin_addr.s_addr &&
                       h2.peer.sin_port == C2.local.sin_port,
                   "second hook peeraddr was %s, expected the second caller's address %s",
                   hk_ipport(h2.peer).c_str(), hk_ipport(C2.local).c_str());

    hk_expect_payload(C1.s, acc1, 3, "first");
    hk_expect_payload(C2.s, acc2, 4, "second");
    ST_REQUIRE_MSG(hk_count() == 2,
                   "the hook fired %zu time(s) once payload had crossed both links, expected 2",
                   hk_count());
    srt_close(acc1);
    srt_close(acc2);
}

// ====================================================== g07_listen_reject ===

// The refusal itself. The hit count is part of the assertion: without it a
// library whose handshake never reached the hook at all would look identical
// to one that rejected on purpose.
SRT_CASE(g07_listen_reject, reject_fails_connect_with_econnrej_and_accept_yields_nothing) {
    hk_reset({-1});
    Listener L;
    L.open();
    int token = 0x07;
    ST_REQUIRE_MSG(srt_listen_callback(L.s, &hk_hook, &token) == 0, "srt_listen_callback: %s",
                   srt_getlasterror_str());

    Caller C;
    C.dial(L, "sid-rejected");
    hk_expect_rejected(C, "reject");
    ST_REQUIRE_MSG(hk_count() == 1,
                   "the hook fired %zu time(s), expected 1 -- the connection must have reached the "
                   "hook for the failure to be a rejection",
                   hk_count());
    ST_REQUIRE_MSG(hk_hit(0).streamid == "sid-rejected",
                   "the rejecting hook saw streamid \"%s\", expected \"sid-rejected\"",
                   hk_hit(0).streamid.c_str());
    hk_expect_nothing_to_accept(L, "reject");
}

// The positive control beside it, and the "listener is not broken" half: one
// listener, the first connection rejected and the second accepted, with the
// second carrying real payload.
SRT_CASE(g07_listen_reject, listener_survives_a_rejection_and_accepts_the_next_connection) {
    hk_reset({-1, 0});
    Listener L;
    L.open();
    int token = 0x077;
    ST_REQUIRE_MSG(srt_listen_callback(L.s, &hk_hook, &token) == 0, "srt_listen_callback: %s",
                   srt_getlasterror_str());

    Caller C1;
    C1.dial(L, "sid-rejected-first");
    hk_expect_rejected(C1, "first");
    hk_expect_nothing_to_accept(L, "first");
    ST_REQUIRE_MSG(hk_count() == 1, "after the rejected attempt the hook had fired %zu time(s), expected 1",
                   hk_count());

    Caller C2;
    C2.dial(L, "sid-accepted-second");
    hk_expect_connected(C2, "second");
    const SRTSOCKET acc = hk_expect_accepted(L, "second");
    ST_REQUIRE_MSG(hk_count() == 2,
                   "after the second attempt the hook had fired %zu time(s), expected 2", hk_count());
    ST_REQUIRE_MSG(hk_hit(1).streamid == "sid-accepted-second",
                   "the accepting hook saw streamid \"%s\", expected \"sid-accepted-second\"",
                   hk_hit(1).streamid.c_str());
    hk_expect_payload(C2.s, acc, 5, "second");
    srt_close(acc);
}

// ====================================================== g08_listen_retval ===

// 0, 1 and -2 all accept. A tree that reads the return value as a boolean
// passes the 0 iteration and fails the other two, which is why the message
// names the value.
SRT_CASE(g08_listen_retval, zero_one_and_minus_two_all_accept) {
    const int values[] = {0, 1, -2};
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
        const int v = values[i];
        char what[48];
        std::snprintf(what, sizeof what, "hook returning %d", v);

        hk_reset({v});
        Listener L;
        L.open();
        int token = 0x800 + v;
        ST_REQUIRE_MSG(srt_listen_callback(L.s, &hk_hook, &token) == 0,
                       "%s: srt_listen_callback: %s", what, srt_getlasterror_str());

        Caller C;
        C.dial(L, "sid-retval");
        hk_expect_connected(C, what);
        const SRTSOCKET acc = hk_expect_accepted(L, what);
        ST_REQUIRE_MSG(hk_count() == 1, "%s: the hook fired %zu time(s), expected 1", what,
                       hk_count());
        hk_expect_payload(C.s, acc, 6 + (int)i, what);
        srt_close(acc);
    }
}

// The contrast the milestone actually turns on, on ONE listener: -1 rejects,
// and -2 -- the neighbouring negative value -- does not. A tree that rejects
// on "any negative" or "any non-zero" fails the second half; a tree that never
// rejects fails the first.
SRT_CASE(g08_listen_retval, only_minus_one_rejects) {
    hk_reset({-1, -2});
    Listener L;
    L.open();
    int token = 0x801;
    ST_REQUIRE_MSG(srt_listen_callback(L.s, &hk_hook, &token) == 0, "srt_listen_callback: %s",
                   srt_getlasterror_str());

    Caller C1;
    C1.dial(L, "sid-minus-one");
    hk_expect_rejected(C1, "hook returning -1");
    hk_expect_nothing_to_accept(L, "hook returning -1");
    ST_REQUIRE_MSG(hk_count() == 1, "after the -1 attempt the hook had fired %zu time(s), expected 1",
                   hk_count());

    Caller C2;
    C2.dial(L, "sid-minus-two");
    hk_expect_connected(C2, "hook returning -2");
    const SRTSOCKET acc = hk_expect_accepted(L, "hook returning -2");
    ST_REQUIRE_MSG(hk_count() == 2, "after the -2 attempt the hook had fired %zu time(s), expected 2",
                   hk_count());
    hk_expect_payload(C2.s, acc, 9, "hook returning -2");
    srt_close(acc);
}
