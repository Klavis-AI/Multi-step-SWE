// Step 11 (v1.5.7) -- `session` family.
//
// Mixed kind, deliberately. `g04_listen_status_codes`,
// `g05_accept_addrlen_guard`, `g06_epoll_uwait_cap`, `g07_startup_repeat` and
// `g08_null_hook_allowed` are tier `new` and SCORED. `g00_endtoend`
// (endtoend.txt, E2E_TIER=inherited) is this milestone's end-to-end path, but
// the path itself is plain live transport that v1.5.2 already carried --
// measured green at the no-op prefix -- so it is GATE, not scored.
// `g09_loss_recovery` and `g10_epoll_leveltrg` are tier `inherited` and are
// GATE too: those three with proof of life form this step's whole gate, they
// earn nothing, and one failure among them zeroes the step.
//
// Because this file holds gate groups it MUST build at the no-op prefix --
// v1.5.2's source configured with this step's CMAKE_EXTRA, which
// dev/noopref.sh 11 produces. A gate that cannot be compiled there reports
// `reward = 0` without ever being evaluated, which is indistinguishable from a
// candidate that did nothing. Verified with dev/fam-at.sh noopref 11.
//
// This file compiles unchanged against the v1.5.2 tree and against this one,
// and it carries NO guarded restatement, no dlsym and no static_assert.
// stepdefs/11/driver-notes.md states the reason and it was re-checked against
// both trees before a line was written: every identifier named below --
// SRT_ECONNSOCK, SRT_ERDVNOSERV, SRT_EUNBOUNDSOCK, SRT_EINVPARAM,
// SRT_EASYNCRCV, SRT_ETIMEOUT, SRT_EPOLLEMPTY, SRT_EINVPOLLID, SRT_EPOLL_IN,
// SRT_EPOLL_EVENT, srt_epoll_uwait, srt_listen_callback, srt_connect_callback
// and both SRT_TRACEBSTATS fields the guards read -- exists in
// <srt/srt.h> at v1.5.2 as well. The two tiers therefore differ only in what
// they assert, never in what they can name.
//
// What each tier owes:
//   * g09, g10 and g00_endtoend must pass at BOTH tags. g09 and g10 are the
//     regression guards: a candidate who breaks recovery or the
//     level-triggered poll to make the new groups pass loses the whole step,
//     because they are the gate.
//   * g04..g08 must pass at v1.5.7 and fail at v1.5.2 on an ASSERTION.
//     Measured at v1.5.2 against the released tree:
//       - srt_listen on a connected caller, an accepted socket and an UNBOUND
//         rendezvous socket all report SRT_EUNBOUNDSOCK (5005); this milestone
//         reports 5002, 5002 and 5007 (g04);
//       - the short-addrlen srt_accept dequeues before it checks, so the
//         connection is lost and the retry reports SRT_EASYNCRCV (g05);
//       - srt_epoll_uwait counts past the end of the caller's array: 2 with a
//         one-slot array and 1 with a zero-slot one (g06);
//       - a second srt_startup returns 1 (g07);
//       - a null hook is refused with SRT_EINVPARAM and a hook installs
//         happily on a listening or connected socket (g08).
//
// Every assertion here is traceable to stepdefs/11/interface.md, which ships
// as TEST_INTERFACE.md section 5. Where that document and `srt-docs/` differ
// the document is the measurement and is what is asserted; the one place where
// a `scored.txt` one-liner and the release disagree is called out at
// g10_epoll_leveltrg below.
#include "srtkit.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ----------------------------------------------------------------- helpers

static int64_t now_ms() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Failure messages name the code, not just the number: a reviewer reads
// failures. Only codes this family can actually produce are listed.
static const char* errname(int code) {
    switch (code) {
    case SRT_SUCCESS:       return "SRT_SUCCESS";
    case SRT_ESCLOSED:      return "SRT_ESCLOSED";
    case SRT_ECONNSOCK:     return "SRT_ECONNSOCK";
    case SRT_EINVPARAM:     return "SRT_EINVPARAM";
    case SRT_EINVSOCK:      return "SRT_EINVSOCK";
    case SRT_EUNBOUNDSOCK:  return "SRT_EUNBOUNDSOCK";
    case SRT_ENOLISTEN:     return "SRT_ENOLISTEN";
    case SRT_ERDVNOSERV:    return "SRT_ERDVNOSERV";
    case SRT_EINVPOLLID:    return "SRT_EINVPOLLID";
    case SRT_EPOLLEMPTY:    return "SRT_EPOLLEMPTY";
    case SRT_EASYNCRCV:     return "SRT_EASYNCRCV";
    case SRT_ETIMEOUT:      return "SRT_ETIMEOUT";
    default:                return "<some other code>";
    }
}

static int last_err() { return srt_getlasterror(NULL); }

// The options this family reads as booleans answer with one byte, so the
// four-byte readers in srtkit.h cannot be used on them.
static int get_bool(SRTSOCKET s, SRT_SOCKOPT o) {
    bool v = false;
    int n = (int)sizeof v;
    if (srt_getsockflag(s, o, &v, &n) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "getsockflag(%d) failed: %s", (int)o,
                 srt_getlasterror_str());
    if (n != (int)sizeof(bool))
        st_failf(__FILE__, __LINE__, "getsockflag(%d) returned %d bytes, expected %zu",
                 (int)o, n, sizeof(bool));
    return v ? 1 : 0;
}

template <class T>
static bool has(const std::vector<T>& v, T x) {
    for (size_t i = 0; i < v.size(); i++)
        if (v[i] == x) return true;
    return false;
}

static SRT_TRACEBSTATS bstats_of(SRTSOCKET s, int clear) {
    SRT_TRACEBSTATS p;
    std::memset(&p, 0, sizeof p);
    ST_REQUIRE_MSG(srt_bstats(s, &p, clear) != SRT_ERROR, "srt_bstats: %s",
                   srt_getlasterror_str());
    return p;
}

// The live-transport shape the transfer groups start from, with the payload
// size pinned to what the stream actually carries. The pinning is not
// decoration: live mode derives the sender's buffer from SRTO_PAYLOADSIZE at
// connect time and a stream of a different size than the peer's setting is
// not delivered at all.
// Microseconds between sends for any stream long enough to matter. 1000 us is
// about 10 Mbit/s at a 1316-byte payload -- step 09 measured it as the figure
// at which the relay keeps up on a loaded host -- and costs 3 s of pacing for
// a 3000-message stream against a 25 s receive budget. See the g00_endtoend
// header below and misc/<task-id>/NOTES.md.
static const int LIVE_GAP_US = 1000;

static SockConf live_sized(int sz, int latency_ms) {
    return [sz, latency_ms](SRTSOCKET s) {
        st_set_int(s, SRTO_TSBPDMODE, 1);
        st_set_int(s, SRTO_LATENCY, latency_ms);
        st_set_int(s, SRTO_TLPKTDROP, 0);
        st_set_int(s, SRTO_PAYLOADSIZE, sz);
    };
}

// How much of `sent` arrived, position by position, and whether `got` holds
// every one of `sent` in order with nothing substituted.
static void compare_stream(const std::vector<std::string>& sent,
                           const std::vector<std::string>& got, int* same, int* ooo,
                           int* complete) {
    *same = 0;
    for (size_t k = 0; k < got.size() && k < sent.size(); k++)
        if (got[k] == sent[k]) (*same)++;
    *ooo = st_first_out_of_order(sent, got);
    *complete = (got.size() == sent.size() && *ooo == -1 && *same == (int)sent.size()) ? 1 : 0;
}

// srt_epoll_wait in its four-array form, with the plumbing kept out of the
// cases. Sets the caller did not ask for are passed as NULL, which the API
// documents as "not interested"; the unfilled slots are sentinel-initialised
// so a membership test can never read a stale one.
static const int EP_CAP = 8;

static int ep_wait(int eid, int64_t timeo_ms, std::vector<SRTSOCKET>* rd,
                   std::vector<SRTSOCKET>* wr, std::vector<SYSSOCKET>* lrd,
                   std::vector<SYSSOCKET>* lwr) {
    SRTSOCKET rbuf[EP_CAP], wbuf[EP_CAP];
    SYSSOCKET lrbuf[EP_CAP], lwbuf[EP_CAP];
    for (int i = 0; i < EP_CAP; i++) {
        rbuf[i] = SRT_INVALID_SOCK;
        wbuf[i] = SRT_INVALID_SOCK;
        lrbuf[i] = -1;
        lwbuf[i] = -1;
    }
    int rn = EP_CAP, wn = EP_CAP, lrn = EP_CAP, lwn = EP_CAP;
    if (rd) rd->clear();
    if (wr) wr->clear();
    if (lrd) lrd->clear();
    if (lwr) lwr->clear();

    int rc = srt_epoll_wait(eid, rd ? rbuf : NULL, rd ? &rn : NULL, wr ? wbuf : NULL,
                            wr ? &wn : NULL, timeo_ms, lrd ? lrbuf : NULL, lrd ? &lrn : NULL,
                            lwr ? lwbuf : NULL, lwr ? &lwn : NULL);
    if (rc <= 0) return rc;
    if (rn < 0 || rn > EP_CAP) rn = EP_CAP;
    if (wn < 0 || wn > EP_CAP) wn = EP_CAP;
    if (lrn < 0 || lrn > EP_CAP) lrn = EP_CAP;
    if (lwn < 0 || lwn > EP_CAP) lwn = EP_CAP;
    if (rd)
        for (int i = 0; i < rn; i++)
            if (rbuf[i] != SRT_INVALID_SOCK) rd->push_back(rbuf[i]);
    if (wr)
        for (int i = 0; i < wn; i++)
            if (wbuf[i] != SRT_INVALID_SOCK) wr->push_back(wbuf[i]);
    if (lrd)
        for (int i = 0; i < lrn; i++)
            if (lrbuf[i] != -1) lrd->push_back(lrbuf[i]);
    if (lwr)
        for (int i = 0; i < lwn; i++)
            if (lwbuf[i] != -1) lwr->push_back(lwbuf[i]);
    return rc;
}

static SRTSOCKET bound_socket(uint16_t* out_port) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    sockaddr_in a = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(s, (sockaddr*)&a, sizeof a) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    if (out_port) {
        int n = (int)sizeof a;
        ST_REQUIRE_MSG(srt_getsockname(s, (sockaddr*)&a, &n) != SRT_ERROR, "srt_getsockname: %s",
                       srt_getlasterror_str());
        *out_port = ntohs(a.sin_port);
    }
    return s;
}

// st_connect in srtkit.h binds and listens in one step, which leaves no window
// between them. This milestone only permits a listen hook to be installed or
// removed in exactly that window, so g08 needs its own bring-up with a hook
// point there. Everything else matches st_try_connect, including the bounded
// accept on its own thread.
static void connect_with_prelisten(Link& L, const SockConf& prelisten,
                                   int accept_grace_ms = 3000) {
    L.lsn = st_new_socket();
    ST_REQUIRE_MSG(L.lsn != SRT_INVALID_SOCK, "srt_socket (listener): %s",
                   srt_getlasterror_str());
    // The accept is bounded by st_accept_polled, not by a timeout: a blocking
    // srt_accept does not consult SRTO_RCVTIMEO at any prefix in this ladder.

    sockaddr_in la = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(L.lsn, (sockaddr*)&la, sizeof la) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    int nl = (int)sizeof la;
    ST_REQUIRE_MSG(srt_getsockname(L.lsn, (sockaddr*)&la, &nl) != SRT_ERROR,
                   "srt_getsockname: %s", srt_getlasterror_str());

    // The window: bound, not yet listening.
    prelisten(L.lsn);

    ST_REQUIRE_MSG(srt_listen(L.lsn, 5) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());

    RelaySched sched;
    L.relay = new Relay(ntohs(la.sin_port), sched);


    L.cal = st_new_socket();
    ST_REQUIRE_MSG(L.cal != SRT_INVALID_SOCK, "srt_socket (caller): %s", srt_getlasterror_str());
    st_set_int(L.cal, SRTO_CONNTIMEO, 8000);

    sockaddr_in ra = st_loopback(L.relay->port());

    SRTSOCKET accepted = SRT_INVALID_SOCK;
    std::atomic<bool> connect_done(false);
    std::thread acceptor(
        [&] { accepted = st_accept_polled(L.lsn, connect_done, nullptr, nullptr,
                                       accept_grace_ms); });

    L.connect_rc = srt_connect(L.cal, (sockaddr*)&ra, sizeof ra);
    L.connect_err = srt_getlasterror(nullptr);
    connect_done.store(true);
    acceptor.join();
    L.acc = accepted;

    ST_REQUIRE_MSG(L.connect_rc != SRT_ERROR, "srt_connect: %s", srt_strerror(L.connect_err, 0));
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK, "srt_accept: %s", srt_getlasterror_str());
}

// One message each way over an established pair, so a case that claims a
// socket is usable has shown that it is. Bounded by SRTO_RCVTIMEO on both
// sides; nothing here sleeps.
static void exchange_one_message(SRTSOCKET a, SRTSOCKET b, int tag, int timeo_ms) {
    st_set_int(a, SRTO_RCVTIMEO, timeo_ms);
    st_set_int(b, SRTO_RCVTIMEO, timeo_ms);
    st_set_int(a, SRTO_SNDSYN, 1);
    st_set_int(b, SRTO_SNDSYN, 1);

    for (int dir = 0; dir < 2; dir++) {
        SRTSOCKET from = dir == 0 ? a : b;
        SRTSOCKET to = dir == 0 ? b : a;
        std::string m = st_payload(tag * 2 + dir, 256);
        int sn = srt_sendmsg(from, m.data(), (int)m.size(), -1, 1);
        ST_REQUIRE_MSG(sn == (int)m.size(), "srt_sendmsg (direction %d) returned %d: %s", dir,
                       sn, srt_getlasterror_str());
        std::string buf(2048, '\0');
        int rn = srt_recvmsg(to, &buf[0], (int)buf.size());
        ST_REQUIRE_MSG(rn == (int)m.size(),
                       "srt_recvmsg (direction %d) returned %d, expected %d: %s", dir, rn,
                       (int)m.size(), srt_getlasterror_str());
        ST_REQUIRE_MSG(buf.compare(0, m.size(), m) == 0,
                       "the message delivered in direction %d is not the one that was sent", dir);
    }
}

// ================================================================
// g04_listen_status_codes  (new -- D8, D9)
//
//   interface.md: backlog <= 0 yields SRT_EINVPARAM; then, on a valid socket,
//   SRTO_RENDEZVOUS true yields SRT_ERDVNOSERV regardless of bind state; then
//   the state -- never bound yields SRT_EUNBOUNDSOCK, bound yields success,
//   already listening yields success, connecting or connected (which includes
//   a caller after srt_connect returned and a socket srt_accept returned)
//   yields SRT_ECONNSOCK. The group does not reach the broken state and does
//   not assert SRT_ESCLOSED.
// ================================================================

// The two states the previous milestone collapsed into SRT_EUNBOUNDSOCK and
// this one separates out. The positive control is in the same case: the link
// really is up, so the two sockets really are connected and accepted.
SRT_CASE(g04_listen_status_codes, a_connected_caller_and_an_accepted_socket_report_connsock) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);
    exchange_one_message(L.cal, L.acc, 1, 5000);

    srt_clearlasterror();
    int rc = srt_listen(L.cal, 5);
    int err = last_err();
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_listen on a connected caller returned %d, expected "
                   "SRT_ERROR", rc);
    ST_REQUIRE_MSG(err == SRT_ECONNSOCK,
                   "srt_listen on a connected caller reported %d (%s), expected SRT_ECONNSOCK "
                   "(%d)", err, errname(err), (int)SRT_ECONNSOCK);

    srt_clearlasterror();
    rc = srt_listen(L.acc, 5);
    err = last_err();
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_listen on an accepted socket returned %d, expected "
                   "SRT_ERROR", rc);
    ST_REQUIRE_MSG(err == SRT_ECONNSOCK,
                   "srt_listen on an accepted socket reported %d (%s), expected SRT_ECONNSOCK "
                   "(%d)", err, errname(err), (int)SRT_ECONNSOCK);

    // Neither refusal disturbed the sockets it refused.
    ST_REQUIRE_EQ_INT(srt_getsockstate(L.cal), SRTS_CONNECTED);
    ST_REQUIRE_EQ_INT(srt_getsockstate(L.acc), SRTS_CONNECTED);
}

// The rendezvous test runs above the state test, so it answers the same way on
// a socket that was never bound and on one that was.
SRT_CASE(g04_listen_status_codes, a_rendezvous_socket_reports_rdvnoserv_bound_or_not) {
    SRTSOCKET unbound = st_new_socket();
    ST_REQUIRE_MSG(unbound != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    st_set_int(unbound, SRTO_RENDEZVOUS, 1);
    ST_REQUIRE_MSG(get_bool(unbound, SRTO_RENDEZVOUS) == 1,
                   "SRTO_RENDEZVOUS did not read back as true on the unbound socket");
    ST_REQUIRE_EQ_INT(srt_getsockstate(unbound), SRTS_INIT);

    srt_clearlasterror();
    int rc = srt_listen(unbound, 5);
    int err = last_err();
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_listen on an unbound rendezvous socket returned %d, "
                   "expected SRT_ERROR", rc);
    ST_REQUIRE_MSG(err == SRT_ERDVNOSERV,
                   "srt_listen on an UNBOUND rendezvous socket reported %d (%s), expected "
                   "SRT_ERDVNOSERV (%d): the rendezvous test must run above the bind-state "
                   "test", err, errname(err), (int)SRT_ERDVNOSERV);

    SRTSOCKET bnd = st_new_socket();
    ST_REQUIRE_MSG(bnd != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    st_set_int(bnd, SRTO_RENDEZVOUS, 1);
    sockaddr_in a = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(bnd, (sockaddr*)&a, sizeof a) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(bnd), SRTS_OPENED);

    srt_clearlasterror();
    rc = srt_listen(bnd, 5);
    err = last_err();
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_listen on a bound rendezvous socket returned %d, "
                   "expected SRT_ERROR", rc);
    ST_REQUIRE_MSG(err == SRT_ERDVNOSERV,
                   "srt_listen on a BOUND rendezvous socket reported %d (%s), expected "
                   "SRT_ERDVNOSERV (%d)", err, errname(err), (int)SRT_ERDVNOSERV);
    ST_REQUIRE_EQ_INT(srt_getsockstate(bnd), SRTS_OPENED);

    // Positive control: the same bind, without the rendezvous flag, listens.
    // Without this the case would pass against a library that refuses every
    // srt_listen for the same reason.
    uint16_t port = 0;
    SRTSOCKET plain = bound_socket(&port);
    ST_REQUIRE_MSG(get_bool(plain, SRTO_RENDEZVOUS) == 0,
                   "a fresh socket reports SRTO_RENDEZVOUS true; the control is not a control");
    ST_REQUIRE_MSG(srt_listen(plain, 5) == 0, "srt_listen on a bound non-rendezvous socket "
                   "failed: %s", srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(plain), SRTS_LISTENING);

    srt_close(plain);
    srt_close(bnd);
    srt_close(unbound);
}

// The one state that still answers SRT_EUNBOUNDSOCK, with the bind that fixes
// it in the same case.
SRT_CASE(g04_listen_status_codes, an_unbound_plain_socket_still_reports_unboundsock) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(get_bool(s, SRTO_RENDEZVOUS) == 0,
                   "a fresh socket reports SRTO_RENDEZVOUS true, so this is not the "
                   "non-rendezvous case the group means to test");
    ST_REQUIRE_EQ_INT(srt_getsockstate(s), SRTS_INIT);

    srt_clearlasterror();
    int rc = srt_listen(s, 5);
    int err = last_err();
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_listen on an unbound plain socket returned %d, "
                   "expected SRT_ERROR", rc);
    ST_REQUIRE_MSG(err == SRT_EUNBOUNDSOCK,
                   "srt_listen on an unbound plain socket reported %d (%s), expected "
                   "SRT_EUNBOUNDSOCK (%d)", err, errname(err), (int)SRT_EUNBOUNDSOCK);
    ST_REQUIRE_EQ_INT(srt_getsockstate(s), SRTS_INIT);

    // The refusal was about the bind state and nothing else: bind it and the
    // same call succeeds.
    sockaddr_in a = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(s, (sockaddr*)&a, sizeof a) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(s, 5) == 0, "srt_listen after bind failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(s), SRTS_LISTENING);
    srt_close(s);
}

// The argument check sits above every state test, and a repeated listen is
// success rather than an error.
SRT_CASE(g04_listen_status_codes, a_zero_backlog_is_invparam_and_a_repeated_listen_succeeds) {
    uint16_t port = 0;
    SRTSOCKET s = bound_socket(&port);

    srt_clearlasterror();
    int rc = srt_listen(s, 0);
    int err = last_err();
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_listen with backlog 0 returned %d, expected SRT_ERROR",
                   rc);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                   "srt_listen with backlog 0 reported %d (%s), expected SRT_EINVPARAM (%d)",
                   err, errname(err), (int)SRT_EINVPARAM);
    ST_REQUIRE_MSG(srt_getsockstate(s) == SRTS_OPENED,
                   "the refused srt_listen left the socket in state %d, expected SRTS_OPENED "
                   "(%d): a refused backlog must not change the socket",
                   (int)srt_getsockstate(s), (int)SRTS_OPENED);

    ST_REQUIRE_MSG(srt_listen(s, 5) == 0, "srt_listen with backlog 5 failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(s), SRTS_LISTENING);

    // Already listening: success, and the socket stays listening.
    ST_REQUIRE_MSG(srt_listen(s, 3) == 0,
                   "a second srt_listen on a socket that is already listening failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(s), SRTS_LISTENING);
    srt_close(s);
}

// ================================================================
// g05_accept_addrlen_guard  (new -- D10)
//
//   interface.md: with a non-blocking listener holding one pending
//   connection, srt_accept with len == 4 returns SRT_INVALID_SOCK with
//   SRT_EINVPARAM AND DOES NOT DEQUEUE the connection; an immediate retry
//   with len == sizeof(sockaddr_in) returns the accepted socket, writes 16
//   back into len, and that socket carries a message each way.
// ================================================================

// A listener that is deliberately NOT accepting, so the connection sits in its
// queue where the group needs it. The wait for it to arrive is an epoll on the
// listener's accept readiness, never a sleep -- and the listener is registered
// BEFORE the caller connects, because a socket added to an epoll afterwards is
// not back-filled with a listener's pending-accept notice.
struct PendingLink {
    Link L;
    int eid = -1;
    ~PendingLink() {
        if (eid >= 0) srt_epoll_release(eid);
    }
};

static void link_with_one_pending(PendingLink& P, int wait_ms) {
    Link& L = P.L;
    L.lsn = st_new_socket();
    ST_REQUIRE_MSG(L.lsn != SRT_INVALID_SOCK, "srt_socket (listener): %s",
                   srt_getlasterror_str());
    // Non-blocking accept, which is what interface.md specifies: a blocking
    // srt_accept would not return at all on the refused call.
    st_set_int(L.lsn, SRTO_RCVSYN, 0);
    ST_REQUIRE_MSG(get_bool(L.lsn, SRTO_RCVSYN) == 0,
                   "SRTO_RCVSYN did not read back false on the listener");

    sockaddr_in la = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(L.lsn, (sockaddr*)&la, sizeof la) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    int nl = (int)sizeof la;
    ST_REQUIRE_MSG(srt_getsockname(L.lsn, (sockaddr*)&la, &nl) != SRT_ERROR,
                   "srt_getsockname: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(L.lsn, 5) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());

    P.eid = srt_epoll_create();
    ST_REQUIRE_MSG(P.eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int in_only = SRT_EPOLL_IN;
    ST_SRT_OK(srt_epoll_add_usock(P.eid, L.lsn, &in_only));

    RelaySched sched;
    L.relay = new Relay(ntohs(la.sin_port), sched);

    L.cal = st_new_socket();
    ST_REQUIRE_MSG(L.cal != SRT_INVALID_SOCK, "srt_socket (caller): %s", srt_getlasterror_str());
    st_set_int(L.cal, SRTO_CONNTIMEO, 8000);

    sockaddr_in ra = st_loopback(L.relay->port());
    L.connect_rc = srt_connect(L.cal, (sockaddr*)&ra, sizeof ra);
    L.connect_err = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(L.connect_rc != SRT_ERROR, "srt_connect: %s", srt_strerror(L.connect_err, 0));

    std::vector<SRTSOCKET> rd;
    int rc = ep_wait(P.eid, wait_ms, &rd, NULL, NULL, NULL);
    ST_REQUIRE_MSG(rc > 0 && has(rd, L.lsn),
                   "the listener did not report a pending connection within %d ms "
                   "(srt_epoll_wait returned %d, %s); the case has nothing queued to accept",
                   wait_ms, rc, errname(last_err()));
}

SRT_CASE(g05_accept_addrlen_guard, a_short_addrlen_is_refused_and_leaves_the_connection_queued) {
    PendingLink P;
    link_with_one_pending(P, 8000);
    Link& L = P.L;

    sockaddr_in sa;
    std::memset(&sa, 0, sizeof sa);
    int len = 4;
    srt_clearlasterror();
    SRTSOCKET a = srt_accept(L.lsn, (sockaddr*)&sa, &len);
    int err = last_err();
    ST_REQUIRE_MSG(a == SRT_INVALID_SOCK,
                   "srt_accept with addrlen 4 returned socket %d, expected SRT_INVALID_SOCK",
                   (int)a);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                   "srt_accept with addrlen 4 reported %d (%s), expected SRT_EINVPARAM (%d)",
                   err, errname(err), (int)SRT_EINVPARAM);

    // The whole point of the milestone: the connection is still there. A tree
    // that dequeues before it checks answers SRT_EASYNCRCV here.
    std::memset(&sa, 0, sizeof sa);
    len = (int)sizeof(sockaddr_in);
    srt_clearlasterror();
    L.acc = srt_accept(L.lsn, (sockaddr*)&sa, &len);
    err = last_err();
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK,
                   "the retry with addrlen %zu reported %d (%s); the refused srt_accept must "
                   "leave the connection queued, not consume it", sizeof(sockaddr_in), err,
                   errname(err));
    ST_REQUIRE_MSG(len == (int)sizeof(sockaddr_in),
                   "srt_accept wrote %d back into addrlen, expected %zu", len,
                   sizeof(sockaddr_in));
    ST_REQUIRE_MSG(sa.sin_family == AF_INET,
                   "srt_accept wrote address family %d, expected AF_INET (%d)",
                   (int)sa.sin_family, (int)AF_INET);

    // The socket it handed back is a working one. The accepted socket inherits
    // the listener's non-blocking receive, so the bound has to be put back.
    st_set_int(L.acc, SRTO_RCVSYN, 1);
    exchange_one_message(L.cal, L.acc, 2, 5000);
}

// The positive control the negative above needs, as its own case: the same
// listener, the same one pending connection, and an addrlen that is large
// enough from the start. Without this the group would pass against a library
// whose srt_accept refuses every address buffer.
SRT_CASE(g05_accept_addrlen_guard, a_sufficient_addrlen_accepts_on_the_first_call) {
    PendingLink P;
    link_with_one_pending(P, 8000);
    Link& L = P.L;

    sockaddr_in sa;
    std::memset(&sa, 0, sizeof sa);
    int len = (int)sizeof(sockaddr_in);
    srt_clearlasterror();
    L.acc = srt_accept(L.lsn, (sockaddr*)&sa, &len);
    int err = last_err();
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK,
                   "srt_accept with addrlen %zu reported %d (%s), expected a socket",
                   sizeof(sockaddr_in), err, errname(err));
    ST_REQUIRE_MSG(len == (int)sizeof(sockaddr_in),
                   "srt_accept wrote %d back into addrlen, expected %zu", len,
                   sizeof(sockaddr_in));
    ST_REQUIRE_MSG(sa.sin_family == AF_INET,
                   "srt_accept wrote address family %d, expected AF_INET (%d)",
                   (int)sa.sin_family, (int)AF_INET);

    // And the queue really is empty now, which is what makes the other case's
    // retry meaningful: this accept DID consume the connection.
    sockaddr_in sb;
    std::memset(&sb, 0, sizeof sb);
    int len2 = (int)sizeof(sockaddr_in);
    srt_clearlasterror();
    SRTSOCKET again = srt_accept(L.lsn, (sockaddr*)&sb, &len2);
    int err2 = last_err();
    ST_REQUIRE_MSG(again == SRT_INVALID_SOCK,
                   "a second srt_accept returned socket %d with nothing left queued", (int)again);
    ST_REQUIRE_MSG(err2 == SRT_EASYNCRCV,
                   "a second srt_accept on an empty queue reported %d (%s), expected "
                   "SRT_EASYNCRCV (%d)", err2, errname(err2), (int)SRT_EASYNCRCV);

    st_set_int(L.acc, SRTO_RCVSYN, 1);
    exchange_one_message(L.cal, L.acc, 3, 5000);
}

// ================================================================
// g06_epoll_uwait_cap  (new -- D3, D4)
//
//   interface.md: the return value is the number of entries WRITTEN, bounded
//   by fdsSize. Two ready sockets return 2 with a four-slot array, 1 with a
//   one-slot array, and 0 with fdsSize == 0 and a null array. Entries beyond
//   the returned count are never touched. The three calls run in the order
//   1, 4, 0 on the same epoll id with nothing consumed in between;
//   level-triggered subscriptions do not clear on report.
// ================================================================

SRT_CASE(g06_epoll_uwait_cap, the_written_count_never_exceeds_the_caller_s_array) {
    // Two connected sockets, each the receiving end of its own link, each
    // holding one unread message. Two links rather than the two ends of one:
    // a message sent from an accepted socket to its caller before any traffic
    // has gone the other way is not delivered at all by this release (measured
    // on a direct connection as well as through the relay, so it is the
    // library and not the harness), and a group about epoll bookkeeping must
    // not be built on top of that.
    Link A, B;
    RelaySched sched;
    st_connect(A, sched);
    st_connect(B, sched);

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int in_only = SRT_EPOLL_IN;
    ST_SRT_OK(srt_epoll_add_usock(eid, A.acc, &in_only));
    ST_SRT_OK(srt_epoll_add_usock(eid, B.acc, &in_only));

    std::string to_a = st_payload(41, 256);
    std::string to_b = st_payload(42, 256);
    int sn = srt_sendmsg(A.cal, to_a.data(), (int)to_a.size(), -1, 1);
    ST_REQUIRE_MSG(sn == (int)to_a.size(), "srt_sendmsg on the first link returned %d: %s", sn,
                   srt_getlasterror_str());
    sn = srt_sendmsg(B.cal, to_b.data(), (int)to_b.size(), -1, 1);
    ST_REQUIRE_MSG(sn == (int)to_b.size(), "srt_sendmsg on the second link returned %d: %s", sn,
                   srt_getlasterror_str());

    // Wait for both to be ready, driven by the poll itself rather than by a
    // sleep. A four-slot call answers 2 at either milestone, so this settling
    // step cannot mask the difference the case is there to see, and it
    // consumes nothing.
    SRT_EPOLL_EVENT warm[4];
    int64_t t0 = now_ms();
    int ready = 0;
    while (now_ms() - t0 < 10000) {
        for (int i = 0; i < 4; i++) { warm[i].fd = SRT_INVALID_SOCK; warm[i].events = 0; }
        ready = srt_epoll_uwait(eid, warm, 4, 500);
        if (ready == 2) break;
    }
    ST_REQUIRE_MSG(ready == 2,
                   "only %d of the 2 sockets holding an unread message became readable (first "
                   "%d state %d, second %d state %d); the case has nothing to cap", ready,
                   (int)A.acc, (int)srt_getsockstate(A.acc), (int)B.acc,
                   (int)srt_getsockstate(B.acc));

    // (1) one slot, fdsSize 1 -> exactly one entry, and it is one of the two.
    SRT_EPOLL_EVENT one[2];
    for (int i = 0; i < 2; i++) { one[i].fd = SRT_INVALID_SOCK; one[i].events = 0; }
    srt_clearlasterror();
    int rc1 = srt_epoll_uwait(eid, one, 1, 500);
    ST_REQUIRE_MSG(rc1 == 1,
                   "srt_epoll_uwait with a one-slot array and two ready sockets returned %d, "
                   "expected 1: the return value is the number of entries WRITTEN, never more "
                   "than fdsSize", rc1);
    ST_REQUIRE_MSG(one[0].fd == A.acc || one[0].fd == B.acc,
                   "the single reported entry names socket %d, which is neither of the two "
                   "ready sockets (%d, %d)", (int)one[0].fd, (int)A.acc, (int)B.acc);
    ST_REQUIRE_MSG((one[0].events & SRT_EPOLL_IN) != 0,
                   "the reported entry for socket %d carries events 0x%x, which does not "
                   "include SRT_EPOLL_IN (0x%x)", (int)one[0].fd, one[0].events,
                   (int)SRT_EPOLL_IN);
    ST_REQUIRE_MSG(one[1].fd == SRT_INVALID_SOCK && one[1].events == 0,
                   "srt_epoll_uwait wrote past the end of a one-slot array: slot 1 holds "
                   "fd %d events 0x%x", (int)one[1].fd, one[1].events);

    // (2) four slots -> both, and exactly two slots filled. Nothing was
    // consumed in between, so the same two are still ready.
    SRT_EPOLL_EVENT four[4];
    for (int i = 0; i < 4; i++) { four[i].fd = SRT_INVALID_SOCK; four[i].events = 0; }
    int rc4 = srt_epoll_uwait(eid, four, 4, 500);
    ST_REQUIRE_MSG(rc4 == 2,
                   "srt_epoll_uwait with a four-slot array and two ready sockets returned %d, "
                   "expected 2", rc4);
    ST_REQUIRE_MSG(four[0].fd != four[1].fd,
                   "both reported entries name the same socket %d", (int)four[0].fd);
    for (int i = 0; i < 2; i++) {
        ST_REQUIRE_MSG(four[i].fd == A.acc || four[i].fd == B.acc,
                       "entry %d names socket %d, which is neither of the two ready sockets "
                       "(%d, %d)", i, (int)four[i].fd, (int)A.acc, (int)B.acc);
        ST_REQUIRE_MSG((four[i].events & SRT_EPOLL_IN) != 0,
                       "entry %d for socket %d carries events 0x%x, which does not include "
                       "SRT_EPOLL_IN (0x%x)", i, (int)four[i].fd, four[i].events,
                       (int)SRT_EPOLL_IN);
    }
    for (int i = 2; i < 4; i++)
        ST_REQUIRE_MSG(four[i].fd == SRT_INVALID_SOCK && four[i].events == 0,
                       "srt_epoll_uwait touched slot %d beyond the 2 entries it reported: it "
                       "holds fd %d events 0x%x", i, (int)four[i].fd, four[i].events);

    // (3) no array at all -> nothing written, so nothing reported.
    srt_clearlasterror();
    int rc0 = srt_epoll_uwait(eid, NULL, 0, 500);
    int err0 = last_err();
    ST_REQUIRE_MSG(rc0 == 0,
                   "srt_epoll_uwait with a null array and fdsSize 0 returned %d (%s) with two "
                   "sockets ready, expected 0: with nowhere to write, nothing is reported",
                   rc0, errname(err0));

    // The readiness the three calls reported was real: both messages are there
    // and are the ones that were sent. Without this the case would pass
    // against a library that reports readiness on sockets holding nothing.
    st_set_int(A.acc, SRTO_RCVTIMEO, 5000);
    st_set_int(B.acc, SRTO_RCVTIMEO, 5000);
    std::string buf(2048, '\0');
    int n = srt_recvmsg(A.acc, &buf[0], (int)buf.size());
    ST_REQUIRE_MSG(n == (int)to_a.size() && buf.compare(0, to_a.size(), to_a) == 0,
                   "the first socket reported readable delivered %d bytes, expected the %d "
                   "that were sent to it", n, (int)to_a.size());
    n = srt_recvmsg(B.acc, &buf[0], (int)buf.size());
    ST_REQUIRE_MSG(n == (int)to_b.size() && buf.compare(0, to_b.size(), to_b) == 0,
                   "the second socket reported readable delivered %d bytes, expected the %d "
                   "that were sent to it", n, (int)to_b.size());

    ST_SRT_OK(srt_epoll_release(eid));
}

// The argument checks interface.md records as UNCHANGED at this milestone.
// They are the control for the case above: they show the new bound is a bound
// on what gets written and not a validator that refuses arrays, and that the
// zero the null-array call returns is not the same thing as an error.
SRT_CASE(g06_epoll_uwait_cap, the_argument_checks_are_unchanged) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());

    // An epoll id with no subscriptions at all is still SRT_EPOLLEMPTY.
    SRT_EPOLL_EVENT ev[4];
    for (int i = 0; i < 4; i++) { ev[i].fd = SRT_INVALID_SOCK; ev[i].events = 0; }
    srt_clearlasterror();
    int rc = srt_epoll_uwait(eid, ev, 4, 200);
    int err = last_err();
    ST_REQUIRE_MSG(rc == SRT_ERROR && err == SRT_EPOLLEMPTY,
                   "srt_epoll_uwait on an epoll id with no subscriptions returned %d / %d (%s), "
                   "expected SRT_ERROR / SRT_EPOLLEMPTY (%d)", rc, err, errname(err),
                   (int)SRT_EPOLLEMPTY);

    int in_only = SRT_EPOLL_IN;
    ST_SRT_OK(srt_epoll_add_usock(eid, L.acc, &in_only));

    srt_clearlasterror();
    rc = srt_epoll_uwait(eid, ev, -1, 200);
    err = last_err();
    ST_REQUIRE_MSG(rc == SRT_ERROR && err == SRT_EINVPARAM,
                   "srt_epoll_uwait with fdsSize -1 returned %d / %d (%s), expected SRT_ERROR / "
                   "SRT_EINVPARAM (%d)", rc, err, errname(err), (int)SRT_EINVPARAM);

    srt_clearlasterror();
    rc = srt_epoll_uwait(eid, NULL, 4, 200);
    err = last_err();
    ST_REQUIRE_MSG(rc == SRT_ERROR && err == SRT_EINVPARAM,
                   "srt_epoll_uwait with a null array and fdsSize 4 returned %d / %d (%s), "
                   "expected SRT_ERROR / SRT_EINVPARAM (%d)", rc, err, errname(err),
                   (int)SRT_EINVPARAM);

    // And with a real array on a subscribed-but-idle socket the call is a
    // plain timeout reporting nothing, not an error.
    for (int i = 0; i < 4; i++) { ev[i].fd = SRT_INVALID_SOCK; ev[i].events = 0; }
    srt_clearlasterror();
    rc = srt_epoll_uwait(eid, ev, 4, 300);
    err = last_err();
    ST_REQUIRE_MSG(rc == 0, "srt_epoll_uwait with nothing ready returned %d (%s), expected 0",
                   rc, errname(err));
    ST_REQUIRE_MSG(ev[0].fd == SRT_INVALID_SOCK,
                   "srt_epoll_uwait reported socket %d while nothing was ready", (int)ev[0].fd);

    ST_SRT_OK(srt_epoll_release(eid));
}

// ================================================================
// g07_startup_repeat  (new -- D5)
//
//   interface.md: with the library already running, a second and a third
//   srt_startup each return 0, srt_getversion still reports the running
//   version between them, and each of the two balancing srt_cleanup calls
//   returns 0 while the library keeps serving the sockets created before
//   them. The harness calls srt_startup once before the case runs, so the
//   calls below are all "already started".
// ================================================================

SRT_CASE(g07_startup_repeat, a_second_and_a_third_startup_report_plain_success) {
    const uint32_t v0 = srt_getversion();
    ST_REQUIRE_MSG(v0 != 0, "srt_getversion reported 0 with the library running");

    const int rc2 = srt_startup();
    ST_REQUIRE_MSG(rc2 == 0,
                   "the second srt_startup returned %d, expected 0: with the library already "
                   "running a repeat start is plain success, not the first-start code", rc2);

    const uint32_t v1 = srt_getversion();
    ST_REQUIRE_MSG(v1 == v0,
                   "srt_getversion reported 0x%06x after the second srt_startup and 0x%06x "
                   "before it", (unsigned)v1, (unsigned)v0);

    const int rc3 = srt_startup();
    ST_REQUIRE_MSG(rc3 == 0, "the third srt_startup returned %d, expected 0", rc3);

    const uint32_t v2 = srt_getversion();
    ST_REQUIRE_MSG(v2 == v0,
                   "srt_getversion reported 0x%06x after the third srt_startup and 0x%06x "
                   "before the extra starts", (unsigned)v2, (unsigned)v0);

    const int c1 = srt_cleanup();
    ST_REQUIRE_MSG(c1 == 0, "the first balancing srt_cleanup returned %d, expected 0", c1);
    const int c2 = srt_cleanup();
    ST_REQUIRE_MSG(c2 == 0, "the second balancing srt_cleanup returned %d, expected 0", c2);

    // Positive control: the library is still running after the balance, so a
    // socket can still be created and the count really was balanced rather
    // than torn down.
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK,
                   "srt_socket failed after two extra startups and two balancing cleanups: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(s), SRTS_INIT);
    srt_close(s);
}

SRT_CASE(g07_startup_repeat, the_library_keeps_serving_a_socket_created_before_the_cleanups) {
    // The link is established BEFORE the extra startups, so it is exactly the
    // kind of socket interface.md says must survive the balancing cleanups.
    Link L;
    RelaySched sched;
    st_connect(L, sched);
    exchange_one_message(L.cal, L.acc, 4, 5000);

    ST_REQUIRE_MSG(srt_startup() == 0, "the second srt_startup did not return 0");
    ST_REQUIRE_MSG(srt_startup() == 0, "the third srt_startup did not return 0");
    ST_REQUIRE_MSG(srt_cleanup() == 0, "the first balancing srt_cleanup did not return 0");
    ST_REQUIRE_MSG(srt_cleanup() == 0, "the second balancing srt_cleanup did not return 0");

    ST_REQUIRE_MSG(srt_getsockstate(L.cal) == SRTS_CONNECTED,
                   "the caller is in state %d after the balancing cleanups, expected "
                   "SRTS_CONNECTED (%d)", (int)srt_getsockstate(L.cal), (int)SRTS_CONNECTED);
    ST_REQUIRE_MSG(srt_getsockstate(L.acc) == SRTS_CONNECTED,
                   "the accepted socket is in state %d after the balancing cleanups, expected "
                   "SRTS_CONNECTED (%d)", (int)srt_getsockstate(L.acc), (int)SRTS_CONNECTED);

    // The real proof: the link still carries traffic both ways.
    exchange_one_message(L.cal, L.acc, 5, 5000);
}

// ================================================================
// g08_null_hook_allowed  (new -- D6)
//
//   interface.md: srt_listen_callback(s, NULL, NULL) and
//   srt_connect_callback(s, NULL, NULL) return 0 on a socket that is not
//   listening, connecting, connected or broken; the null install removes a
//   hook installed before it; and either installer on a socket that IS
//   listening, connecting, connected, accepted or broken returns SRT_ERROR
//   with SRT_ECONNSOCK, null hook or not. The group installs the listen hook
//   on the bound socket before srt_listen, never after, and does not reach
//   the broken state.
// ================================================================

static std::atomic<int> g_listen_hook_calls(0);

static int counting_listen_hook(void* opaq, SRTSOCKET ns, int hsversion,
                                const struct sockaddr* peeraddr, const char* streamid) {
    (void)opaq;
    (void)ns;
    (void)hsversion;
    (void)peeraddr;
    (void)streamid;
    g_listen_hook_calls++;
    return 0;
}

static void noop_connect_hook(void* opaq, SRTSOCKET ns, int errorcode,
                              const struct sockaddr* peeraddr, int token) {
    (void)opaq;
    (void)ns;
    (void)errorcode;
    (void)peeraddr;
    (void)token;
}

SRT_CASE(g08_null_hook_allowed, a_null_listen_hook_returns_zero_and_removes_the_installed_hook) {
    // Positive control first, so the case knows the hook it later removes is a
    // hook that would otherwise fire.
    g_listen_hook_calls = 0;
    {
        Link L;
        connect_with_prelisten(L, [](SRTSOCKET lsn) {
            ST_REQUIRE_MSG(srt_listen_callback(lsn, counting_listen_hook, NULL) == 0,
                           "srt_listen_callback on a bound, not-yet-listening socket failed: %s",
                           srt_getlasterror_str());
        });
        exchange_one_message(L.cal, L.acc, 6, 5000);
        ST_REQUIRE_MSG(g_listen_hook_calls.load() == 1,
                       "the installed listen hook was invoked %d time(s) for one accepted "
                       "connection, expected 1; the removal below would prove nothing",
                       g_listen_hook_calls.load());
    }

    // The subject: install, then remove with a null hook, both in the window
    // between bind and listen.
    g_listen_hook_calls = 0;
    {
        Link L;
        connect_with_prelisten(L, [](SRTSOCKET lsn) {
            ST_REQUIRE_MSG(srt_listen_callback(lsn, counting_listen_hook, NULL) == 0,
                           "srt_listen_callback (install) failed: %s", srt_getlasterror_str());
            srt_clearlasterror();
            int rc = srt_listen_callback(lsn, NULL, NULL);
            int err = srt_getlasterror(NULL);
            ST_REQUIRE_MSG(rc == 0,
                           "srt_listen_callback with a null hook on a bound, not-yet-listening "
                           "socket returned %d and reported %d (%s), expected 0", rc, err,
                           errname(err));
        });
        // The connection still comes up -- removing the hook does not refuse
        // callers -- and the removed hook was never invoked.
        exchange_one_message(L.cal, L.acc, 7, 5000);
        ST_REQUIRE_MSG(g_listen_hook_calls.load() == 0,
                       "the listen hook was invoked %d time(s) after it had been removed with "
                       "a null hook, expected 0", g_listen_hook_calls.load());
    }
}

SRT_CASE(g08_null_hook_allowed, a_null_connect_hook_on_a_fresh_socket_returns_zero) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(s), SRTS_INIT);

    srt_clearlasterror();
    int rc = srt_connect_callback(s, NULL, NULL);
    int err = last_err();
    ST_REQUIRE_MSG(rc == 0,
                   "srt_connect_callback with a null hook on a fresh socket returned %d and "
                   "reported %d (%s), expected 0", rc, err, errname(err));

    // Both orders work on a socket that is not busy, and a real hook is still
    // accepted: the zero above is not "this installer always answers zero for
    // a null hook and refuses everything else".
    ST_REQUIRE_MSG(srt_connect_callback(s, noop_connect_hook, NULL) == 0,
                   "srt_connect_callback with a real hook on a fresh socket failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_connect_callback(s, NULL, NULL) == 0,
                   "srt_connect_callback with a null hook after a real one failed: %s",
                   srt_getlasterror_str());

    // The same on a bound-but-idle socket, which is the other not-busy state.
    uint16_t port = 0;
    SRTSOCKET b = bound_socket(&port);
    ST_REQUIRE_MSG(srt_connect_callback(b, NULL, NULL) == 0,
                   "srt_connect_callback with a null hook on a bound socket failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen_callback(b, NULL, NULL) == 0,
                   "srt_listen_callback with a null hook on a bound socket failed: %s",
                   srt_getlasterror_str());

    srt_close(b);
    srt_close(s);
}

SRT_CASE(g08_null_hook_allowed, installing_a_hook_on_a_busy_socket_is_refused_with_connsock) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);
    exchange_one_message(L.cal, L.acc, 8, 5000);

    struct Probe {
        const char* what;
        SRTSOCKET s;
    };
    const Probe probes[] = {
        {"a listening socket", L.lsn},
        {"a connected caller", L.cal},
        {"an accepted socket", L.acc},
    };

    for (size_t i = 0; i < sizeof probes / sizeof probes[0]; i++) {
        const Probe& p = probes[i];
        for (int nullhook = 0; nullhook < 2; nullhook++) {
            srt_clearlasterror();
            int rc = srt_listen_callback(p.s, nullhook ? NULL : counting_listen_hook, NULL);
            int err = last_err();
            ST_REQUIRE_MSG(rc == SRT_ERROR,
                           "srt_listen_callback with a %s hook on %s returned %d, expected "
                           "SRT_ERROR", nullhook ? "null" : "real", p.what, rc);
            ST_REQUIRE_MSG(err == SRT_ECONNSOCK,
                           "srt_listen_callback with a %s hook on %s reported %d (%s), expected "
                           "SRT_ECONNSOCK (%d)", nullhook ? "null" : "real", p.what, err,
                           errname(err), (int)SRT_ECONNSOCK);

            srt_clearlasterror();
            rc = srt_connect_callback(p.s, nullhook ? NULL : noop_connect_hook, NULL);
            err = last_err();
            ST_REQUIRE_MSG(rc == SRT_ERROR,
                           "srt_connect_callback with a %s hook on %s returned %d, expected "
                           "SRT_ERROR", nullhook ? "null" : "real", p.what, rc);
            ST_REQUIRE_MSG(err == SRT_ECONNSOCK,
                           "srt_connect_callback with a %s hook on %s reported %d (%s), "
                           "expected SRT_ECONNSOCK (%d)", nullhook ? "null" : "real", p.what,
                           err, errname(err), (int)SRT_ECONNSOCK);
        }
    }

    // Positive control: a socket that is none of those three states takes both
    // installers, so the refusals above are about the state and not about the
    // installers being broken.
    SRTSOCKET idle = st_new_socket();
    ST_REQUIRE_MSG(idle != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen_callback(idle, counting_listen_hook, NULL) == 0,
                   "srt_listen_callback on an idle socket failed: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_connect_callback(idle, noop_connect_hook, NULL) == 0,
                   "srt_connect_callback on an idle socket failed: %s", srt_getlasterror_str());
    srt_close(idle);
}

// ================================================================
// g09_loss_recovery  (inherited)
//
//   Must pass at BOTH tags. interface.md: reads pktRcvLoss from srt_bstats
//   with `clear` set and asserts it non-zero after the scripted drops,
//   exactly as the previous milestone's g11_recovery did.
// ================================================================

SRT_CASE(g09_loss_recovery, dropped_datagrams_are_recovered_and_the_loss_is_counted) {
    Link L;
    RelaySched sched;
    for (int i = 60; i < 67; i++) sched.drop_fwd.insert(i);
    const int N = 200, SZ = 1316, DROPS = 7;
    SockConf recover = live_sized(SZ, 4000);
    st_connect(L, sched, recover, recover);

    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, SZ, 20000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, SZ, 200);
    rcvr.join();

    int same = 0, ooo = 0, complete = 0;
    compare_stream(sent, got, &same, &ooo, &complete);
    ST_REQUIRE_MSG(complete,
                   "recovery: %zu of %d messages delivered in order (%d identical, first "
                   "out-of-order at %d)", got.size(), N, same, ooo);
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == DROPS,
                   "the schedule removed %d datagram(s), expected %d; the case is not testing "
                   "recovery unless the loss really happened", L.relay->dropped_fwd(), DROPS);

    SRT_TRACEBSTATS p = bstats_of(L.acc, 1);
    ST_REQUIRE_MSG(p.pktRcvLoss > 0,
                   "the receiver's interval pktRcvLoss is %lld after %d datagram(s) were "
                   "removed from the channel; the payload arrived, so either nothing was lost "
                   "or the loss was not counted", (long long)p.pktRcvLoss,
                   L.relay->dropped_fwd());
}

// The control the case above needs: the same stream over a channel that drops
// nothing arrives the same way and the loss counter reads zero. Without it a
// library whose pktRcvLoss is always non-zero would score the point.
SRT_CASE(g09_loss_recovery, a_clean_channel_delivers_the_same_stream_with_no_loss_counted) {
    Link L;
    RelaySched sched;
    const int N = 200, SZ = 1316;
    SockConf cfg = live_sized(SZ, 4000);
    st_connect(L, sched, cfg, cfg);

    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, SZ, 20000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, SZ, 200);
    rcvr.join();

    int same = 0, ooo = 0, complete = 0;
    compare_stream(sent, got, &same, &ooo, &complete);
    ST_REQUIRE_MSG(complete,
                   "clean channel: %zu of %d messages delivered in order (%d identical, first "
                   "out-of-order at %d)", got.size(), N, same, ooo);
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 0,
                   "the clean schedule removed %d datagram(s), expected 0",
                   L.relay->dropped_fwd());

    SRT_TRACEBSTATS p = bstats_of(L.acc, 1);
    ST_REQUIRE_MSG(p.pktRcvLoss == 0,
                   "the receiver's interval pktRcvLoss is %lld on a channel that dropped "
                   "nothing, expected 0", (long long)p.pktRcvLoss);
}

// ================================================================
// g10_epoll_leveltrg  (inherited)
//
//   Must pass at BOTH tags. interface.md: srt_epoll_wait in its four-array
//   form, with a system UDP socket registered through srt_epoll_add_ssock,
//   as step 01's g04_epoll did.
//
//   MEASUREMENT NOTE. An earlier scored.txt one-liner said "a wait on the id
//   with nothing ready returns 0 within its timeout". srt_epoll_wait does not
//   return 0 -- at v1.5.2 and at v1.5.7 alike it returns SRT_ERROR and reports
//   SRT_ETIMEOUT, which is what `srt-docs/API/API-functions.md` documents for
//   it and what step 01's g04_epoll already asserts. Returning 0 on timeout is
//   srt_epoll_uwait's convention, not this one. scored.txt, inherit.md and the
//   generated step documents now say what the release does, and the cases
//   below assert it: no socket is reported in any of the four arrays, and the
//   call fails with SRT_ETIMEOUT. They do NOT time the call -- see the note at
//   the timeout case itself.
// ================================================================

// A bound UDP socket and a sender for it, so the system-socket half of the
// poll has a real datagram behind it. udp_socket_bound comes from relay.h.
struct SysPair {
    int rx = -1;
    int tx = -1;
    uint16_t port = 0;
    SysPair() {
        rx = udp_socket_bound(0, &port);
        tx = ::socket(AF_INET, SOCK_DGRAM, 0);
    }
    ~SysPair() {
        if (rx >= 0) ::close(rx);
        if (tx >= 0) ::close(tx);
    }
    SysPair(const SysPair&) = delete;
    SysPair& operator=(const SysPair&) = delete;

    void send(const std::string& m) const {
        sockaddr_in to = st_loopback(port);
        ssize_t n = ::sendto(tx, m.data(), m.size(), 0, (const sockaddr*)&to, sizeof to);
        ST_REQUIRE_MSG(n == (ssize_t)m.size(), "sendto on the system socket wrote %lld of %zu "
                       "bytes", (long long)n, m.size());
    }
};

SRT_CASE(g10_epoll_leveltrg, an_srt_socket_is_reported_until_its_message_is_consumed) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);
    st_set_int(L.acc, SRTO_RCVTIMEO, 5000);

    SysPair sys;
    ST_REQUIRE_MSG(sys.rx >= 0 && sys.tx >= 0, "could not create the system UDP pair");

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int in_only = SRT_EPOLL_IN;
    ST_SRT_OK(srt_epoll_add_usock(eid, L.acc, &in_only));
    ST_SRT_OK(srt_epoll_add_ssock(eid, sys.rx, &in_only));

    std::vector<SRTSOCKET> rd;
    std::vector<SYSSOCKET> lrd;
    int rc = ep_wait(eid, 400, &rd, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR && rd.empty() && lrd.empty(),
                   "srt_epoll_wait returned %d and reported %zu SRT and %zu system socket(s) "
                   "with neither ready, expected a timeout reporting none", rc, rd.size(),
                   lrd.size());

    std::string msg = st_payload(21, 256);
    int sn = srt_sendmsg(L.cal, msg.data(), (int)msg.size(), -1, 1);
    ST_REQUIRE_MSG(sn == (int)msg.size(), "srt_sendmsg returned %d: %s", sn,
                   srt_getlasterror_str());

    rc = ep_wait(eid, 8000, &rd, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc > 0 && has(rd, L.acc),
                   "srt_epoll_wait returned %d and did not report the receiver readable after "
                   "one message arrived", rc);
    ST_REQUIRE_MSG(lrd.empty(),
                   "srt_epoll_wait reported %zu system socket(s) readable with nothing written "
                   "to the UDP socket", lrd.size());

    // Level-triggered: nothing was consumed, so the same readiness is reported
    // again rather than only once as an edge.
    rc = ep_wait(eid, 1000, &rd, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc > 0 && has(rd, L.acc),
                   "srt_epoll_wait returned %d on the second ask with the message still "
                   "unread: readability is not level-triggered", rc);

    std::string buf(2048, '\0');
    int n = srt_recvmsg(L.acc, &buf[0], (int)buf.size());
    ST_REQUIRE_MSG(n == (int)msg.size(), "srt_recvmsg returned %d, expected %d: %s", n,
                   (int)msg.size(), srt_getlasterror_str());
    ST_REQUIRE_MSG(buf.compare(0, msg.size(), msg) == 0,
                   "the message the poll reported readable was not the one that was sent");

    rc = ep_wait(eid, 400, &rd, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR && rd.empty(),
                   "srt_epoll_wait returned %d and reported %zu SRT socket(s) after the only "
                   "message was consumed, expected a timeout reporting none", rc, rd.size());

    ST_SRT_OK(srt_epoll_release(eid));
}

SRT_CASE(g10_epoll_leveltrg, the_system_socket_is_reported_when_a_datagram_waits_on_it) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);
    st_set_int(L.acc, SRTO_RCVTIMEO, 5000);

    SysPair sys;
    ST_REQUIRE_MSG(sys.rx >= 0 && sys.tx >= 0, "could not create the system UDP pair");

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int in_only = SRT_EPOLL_IN;
    ST_SRT_OK(srt_epoll_add_usock(eid, L.acc, &in_only));
    ST_SRT_OK(srt_epoll_add_ssock(eid, sys.rx, &in_only));

    std::vector<SRTSOCKET> rd;
    std::vector<SYSSOCKET> lrd;

    const std::string dgram = "datagram-for-the-system-socket";
    sys.send(dgram);

    int rc = ep_wait(eid, 5000, &rd, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc > 0 && has(lrd, (SYSSOCKET)sys.rx),
                   "srt_epoll_wait returned %d and did not report the system socket readable "
                   "with a datagram waiting on it", rc);
    ST_REQUIRE_MSG(rd.empty(),
                   "srt_epoll_wait reported %zu SRT socket(s) readable with nothing sent over "
                   "SRT", rd.size());

    rc = ep_wait(eid, 1000, &rd, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc > 0 && has(lrd, (SYSSOCKET)sys.rx),
                   "srt_epoll_wait returned %d on the second ask with the datagram still "
                   "unread: the system socket is not reported level-triggered", rc);

    // Both kinds in one wait, which is the thing an epoll id holding both is
    // for. The SRT message is added while the datagram is still unread.
    std::string msg = st_payload(23, 256);
    int sn = srt_sendmsg(L.cal, msg.data(), (int)msg.size(), -1, 1);
    ST_REQUIRE_MSG(sn == (int)msg.size(), "srt_sendmsg returned %d: %s", sn,
                   srt_getlasterror_str());

    bool both = false;
    int64_t t0 = now_ms();
    while (now_ms() - t0 < 8000) {
        rc = ep_wait(eid, 500, &rd, NULL, &lrd, NULL);
        if (rc > 0 && has(rd, L.acc) && has(lrd, (SYSSOCKET)sys.rx)) {
            both = true;
            break;
        }
    }
    ST_REQUIRE_MSG(both, "no single srt_epoll_wait reported both the SRT socket and the system "
                   "socket readable");

    // Both readinesses were real, not labels.
    char dbuf[128];
    ssize_t dn = ::recv(sys.rx, dbuf, sizeof dbuf, 0);
    ST_REQUIRE_MSG(dn == (ssize_t)dgram.size() &&
                       std::string(dbuf, (size_t)dn) == dgram,
                   "the system socket reported readable delivered %lld bytes, expected the %zu "
                   "that were sent", (long long)dn, dgram.size());
    std::string buf(2048, '\0');
    int n = srt_recvmsg(L.acc, &buf[0], (int)buf.size());
    ST_REQUIRE_MSG(n == (int)msg.size() && buf.compare(0, msg.size(), msg) == 0,
                   "the SRT socket reported readable delivered %d bytes, expected the %d that "
                   "were sent", n, (int)msg.size());

    rc = ep_wait(eid, 400, &rd, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR && rd.empty() && lrd.empty(),
                   "srt_epoll_wait returned %d and reported %zu SRT and %zu system socket(s) "
                   "after both were consumed, expected a timeout reporting none", rc, rd.size(),
                   lrd.size());

    ST_SRT_OK(srt_epoll_release(eid));
}

SRT_CASE(g10_epoll_leveltrg, a_wait_with_nothing_ready_reports_nothing_and_times_out) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);
    st_set_int(L.acc, SRTO_RCVTIMEO, 5000);

    SysPair sys;
    ST_REQUIRE_MSG(sys.rx >= 0 && sys.tx >= 0, "could not create the system UDP pair");

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int in_only = SRT_EPOLL_IN;
    ST_SRT_OK(srt_epoll_add_usock(eid, L.acc, &in_only));
    ST_SRT_OK(srt_epoll_add_ssock(eid, sys.rx, &in_only));

    const int64_t timeo = 700;
    std::vector<SRTSOCKET> rd, wr;
    std::vector<SYSSOCKET> lrd, lwr;
    srt_clearlasterror();
    int rc = ep_wait(eid, timeo, &rd, &wr, &lrd, &lwr);
    int err = last_err();

    ST_REQUIRE_MSG(rd.empty() && wr.empty() && lrd.empty() && lwr.empty(),
                   "a wait with nothing ready reported %zu/%zu SRT and %zu/%zu system "
                   "socket(s) read/write ready, expected none of each", rd.size(), wr.size(),
                   lrd.size(), lwr.size());
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "srt_epoll_wait returned %d with nothing ready, expected the timeout "
                   "report", rc);
    ST_REQUIRE_MSG(err == SRT_ETIMEOUT,
                   "an expired srt_epoll_wait reported %d (%s), expected SRT_ETIMEOUT (%d)",
                   err, errname(err), (int)SRT_ETIMEOUT);
    // This case deliberately does NOT time the wait. `SRT_ETIMEOUT` above is
    // the library's own report that the wait expired rather than failing, which
    // is the whole of the contract; a clock reading adds no coverage and does
    // add the one thing a GATE case may never have, a way to fail for a reason
    // that is not a regression. Two elapsed-time assertions were deleted here
    // -- see NOTES.md, "Gate cases deleted for non-determinism".

    // Positive control: the same id on the same two sockets does report, so
    // the silence above was the timeout and not a broken registration.
    std::string msg = st_payload(25, 256);
    int sn = srt_sendmsg(L.cal, msg.data(), (int)msg.size(), -1, 1);
    ST_REQUIRE_MSG(sn == (int)msg.size(), "srt_sendmsg returned %d: %s", sn,
                   srt_getlasterror_str());
    rc = ep_wait(eid, 8000, &rd, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc > 0 && has(rd, L.acc),
                   "the same srt_epoll_wait returned %d after a message was sent; the "
                   "registration is not reporting anything at all", rc);

    sys.send("x");
    bool saw_sys = false;
    int64_t t1 = now_ms();
    while (now_ms() - t1 < 5000) {
        rc = ep_wait(eid, 500, &rd, NULL, &lrd, NULL);
        if (rc > 0 && has(lrd, (SYSSOCKET)sys.rx)) { saw_sys = true; break; }
    }
    ST_REQUIRE_MSG(saw_sys, "the same srt_epoll_wait never reported the system socket after a "
                   "datagram was written to it");

    ST_SRT_OK(srt_epoll_release(eid));
}

// ================================================================
// g00_endtoend  (GATE, weight 0 -- the shared end-to-end path, tier `inherited`)
//
//   endtoend.txt: a live stream of 3000 messages, each within the single-packet
//   payload limit in force, crosses a clean relay and arrives byte-identical,
//   in order and with no gaps, and the same stream does the same over a relay
//   that adds 15 ms each way. Two checks, both in this one group.
//
//   The latency is 8000 ms and the messages are 1316 bytes -- the live-mode
//   single-packet payload limit at the default MSS of 1500, since
//   min(1316, 1500 - 44) is 1316. The combination is not arbitrary: live mode
//   paces the sender, and a shorter latency budget lets the sender run ahead
//   of a window that has not drained, discarding the tail of the stream
//   before it can cross.
//
//   The sends are paced at LIVE_GAP_US, and that is load-bearing. relay.h is
//   a userspace hop with an ordinary socket buffer, so an unpaced burst of
//   3000 datagrams is dropped by the kernel before the relay sees it -- BELOW
//   the layer SRT retransmits from, which with SRTO_TLPKTDROP=0 stalls the
//   receiver on the gap. Measured unpaced on this box at load average 14-22:
//   four of six case-runs lost between 2616 and 2983 of 3000, on the CLEAN
//   arm as often as the delayed one. Paced, four of four passed under the
//   same load. The assertions are unchanged; only the send rate is.
// ================================================================

static void gate_stream(const char* tag, bool delayed) {
    Link L;
    RelaySched sched;
    if (delayed) {
        sched.delay_fwd_ms = 15;
        sched.delay_bwd_ms = 15;
    }
    const int N = 3000, SZ = 1316;
    SockConf cfg = live_sized(SZ, 8000);
    st_connect(L, sched, cfg, cfg);

    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, SZ, 25000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, SZ, LIVE_GAP_US);
    rcvr.join();

    int same = 0, ooo = 0, complete = 0;
    compare_stream(sent, got, &same, &ooo, &complete);
    ST_REQUIRE_MSG(got.size() == (size_t)N, "%s: %zu of %d messages arrived (gaps)", tag,
                   got.size(), N);
    ST_REQUIRE_MSG(ooo == -1, "%s: the stream arrived out of order; the first out-of-order "
                   "message is at index %d", tag, ooo);
    ST_REQUIRE_MSG(same == N, "%s: %d of %d messages are byte-identical; the stream must "
                   "arrive intact as well as complete", tag, same, N);
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 0, "%s: the channel dropped %d datagram(s)", tag,
                   L.relay->dropped_fwd());
    ST_REQUIRE_MSG(L.relay->seen_fwd() >= N,
                   "%s: the relay forwarded only %d data packet(s) for %d messages; the stream "
                   "did not cross the channel the case claims it did", tag,
                   L.relay->seen_fwd(), N);
}

SRT_CASE(g00_endtoend, three_thousand_over_a_clean_relay) {
    gate_stream("clean relay", false);
}

SRT_CASE(g00_endtoend, three_thousand_over_a_relay_delaying_each_way) {
    gate_stream("15 ms each way", true);
}
