// Family "api", checkpoint 01 (SRT v1.2.0). Four scored groups, one subject
// each: the socket lifecycle, socket options, error reporting and epoll.
//
// Every assertion here is traceable either to the candidate-facing
// documentation (srt-docs/API.md: the option table with its "pre"/"post"
// column, its type column and its default column, plus the epoll section) or
// to the public header <srt/srt.h> (the SRT_SOCKSTATUS enumeration, the
// SRT_ERRNO enumeration and the signatures). Values the documentation leaves
// open, or on which the documentation contradicts itself, are asserted for
// shape only or not at all -- see the notes on the individual cases.
#include "srtkit.h"

#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------- helpers

static const char* st_state_name(SRT_SOCKSTATUS s) {
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
    return "<unknown>";
}

// srtkit's st_get_int insists on a 4-byte result, so the options the
// documentation types as `bool` need their own reader. It also checks the
// width, which is half of what "the documented type" means.
static bool st_get_bool(SRTSOCKET s, SRT_SOCKOPT o) {
    bool v = false;
    int n = (int)sizeof v;
    if (srt_getsockflag(s, o, &v, &n) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "getsockflag(%d) failed: %s", (int)o, srt_getlasterror_str());
    if (n != (int)sizeof(bool))
        st_failf(__FILE__, __LINE__, "getsockflag(%d) returned %d bytes, expected %zu for a bool",
                 (int)o, n, sizeof(bool));
    return v;
}

// An id that was never handed out. How ids are allocated is the candidate's
// design decision -- a conforming library may draw them upward, downward, or
// from anywhere -- so this must not assume a direction. It probes outward in
// both directions from a known-live id and asks the library which candidate it
// does not recognise.
//
// The probe uses srt_getsockname deliberately, because neither caller asserts
// it: g01_lifecycle asserts srt_getsockstate answers SRTS_NONEXIST for this id,
// and g03_errors asserts srt_getsockflag fails on it with SRT_EINVSOCK.
// Selecting the id with either of those calls would make one of the two cases
// assert the thing it had just used to choose its input. A third entry point
// leaves both contracts genuinely under test.
//
// The failure mode is one-sided. If a probe ever mistook a live-but-unbound
// socket for an unallocated id, the caller's assertion would fail loudly rather
// than pass wrongly, so this can cost a false red but never a false green.
static SRTSOCKET st_never_allocated(SRTSOCKET live) {
    const int kStride = 4096;
    const int kProbes = 256;
    sockaddr_storage ss;
    for (int i = 1; i <= kProbes; i++) {
        const SRTSOCKET cand[2] = {live + (SRTSOCKET)i * kStride,
                                   live - (SRTSOCKET)i * kStride};
        for (int k = 0; k < 2; k++) {
            if (cand[k] == live || cand[k] == SRT_INVALID_SOCK) continue;
            int n = (int)sizeof ss;
            if (srt_getsockname(cand[k], (sockaddr*)&ss, &n) == SRT_ERROR) {
                srt_clearlasterror();
                return cand[k];
            }
        }
    }
    st_failf(__FILE__, __LINE__,
             "no unallocated socket id found within %d probes either side of %d",
             kProbes, (int)live);
    return SRT_INVALID_SOCK;
}

static int64_t st_now_ms() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

template <class T>
static bool st_has(const std::vector<T>& v, T x) {
    for (size_t i = 0; i < v.size(); i++)
        if (v[i] == x) return true;
    return false;
}

// srt_epoll_wait with the array plumbing kept out of the cases. A set the
// caller did not ask for is passed as NULL, which the API documents as "not
// interested". The returned counts are clamped and the unfilled slots are
// sentinel-initialised, so a membership test can never read a stale slot.
static const int ST_EP_CAP = 8;

static int st_ep_wait(int eid, int64_t timeo_ms, std::vector<SRTSOCKET>* rd,
                      std::vector<SRTSOCKET>* wr, std::vector<SYSSOCKET>* lrd,
                      std::vector<SYSSOCKET>* lwr) {
    SRTSOCKET rbuf[ST_EP_CAP], wbuf[ST_EP_CAP];
    SYSSOCKET lrbuf[ST_EP_CAP], lwbuf[ST_EP_CAP];
    for (int i = 0; i < ST_EP_CAP; i++) {
        rbuf[i] = SRT_INVALID_SOCK;
        wbuf[i] = SRT_INVALID_SOCK;
        lrbuf[i] = -1;
        lwbuf[i] = -1;
    }
    int rn = ST_EP_CAP, wn = ST_EP_CAP, lrn = ST_EP_CAP, lwn = ST_EP_CAP;
    if (rd) rd->clear();
    if (wr) wr->clear();
    if (lrd) lrd->clear();
    if (lwr) lwr->clear();

    int rc = srt_epoll_wait(eid, rd ? rbuf : NULL, rd ? &rn : NULL, wr ? wbuf : NULL,
                            wr ? &wn : NULL, timeo_ms, lrd ? lrbuf : NULL, lrd ? &lrn : NULL,
                            lwr ? lwbuf : NULL, lwr ? &lwn : NULL);
    if (rc <= 0) return rc;
    if (rn < 0 || rn > ST_EP_CAP) rn = ST_EP_CAP;
    if (wn < 0 || wn > ST_EP_CAP) wn = ST_EP_CAP;
    if (lrn < 0 || lrn > ST_EP_CAP) lrn = ST_EP_CAP;
    if (lwn < 0 || lwn > ST_EP_CAP) lwn = ST_EP_CAP;
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

static SRTSOCKET st_bound_socket(uint16_t* out_port) {
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

// ================================================================
// g01_lifecycle
//   socket creation, bind, listen, accept, connect and close drive
//   srt_getsockstate through the documented states, and operations invalid
//   for a state are refused.
// ================================================================

SRT_CASE(g01_lifecycle, a_new_socket_is_init_and_an_unallocated_id_is_nonexist) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    SRT_SOCKSTATUS st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_INIT, "a freshly created socket reports %s, expected SRTS_INIT",
                   st_state_name(st));

    SRTSOCKET never = st_never_allocated(s);
    SRT_SOCKSTATUS ns = srt_getsockstate(never);
    ST_REQUIRE_MSG(ns == SRTS_NONEXIST, "an id that was never allocated reports %s, expected SRTS_NONEXIST",
                   st_state_name(ns));

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g01_lifecycle, bind_then_listen_walk_opened_then_listening) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    SRT_SOCKSTATUS st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_INIT, "before srt_bind the state is %s, expected SRTS_INIT",
                   st_state_name(st));

    sockaddr_in a = st_loopback(0);
    ST_SRT_OK(srt_bind(s, (sockaddr*)&a, sizeof a));
    st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_OPENED, "after srt_bind the state is %s, expected SRTS_OPENED",
                   st_state_name(st));

    int n = (int)sizeof a;
    ST_SRT_OK(srt_getsockname(s, (sockaddr*)&a, &n));
    ST_REQUIRE_MSG(ntohs(a.sin_port) != 0,
                   "srt_getsockname after a wildcard bind still reports port 0");

    ST_SRT_OK(srt_listen(s, 1));
    st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_LISTENING, "after srt_listen the state is %s, expected SRTS_LISTENING",
                   st_state_name(st));

    ST_SRT_OK(srt_close(s));
}

// The positive control for the whole group: the states the refusals below are
// measured against really are reachable, and the link they describe really
// carries a payload.
SRT_CASE(g01_lifecycle, connect_and_accept_report_connected_on_a_working_link) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);

    SRT_SOCKSTATUS st = srt_getsockstate(L.cal);
    ST_REQUIRE_MSG(st == SRTS_CONNECTED, "the caller reports %s after srt_connect, expected SRTS_CONNECTED",
                   st_state_name(st));
    st = srt_getsockstate(L.acc);
    ST_REQUIRE_MSG(st == SRTS_CONNECTED, "the accepted socket reports %s, expected SRTS_CONNECTED",
                   st_state_name(st));
    st = srt_getsockstate(L.lsn);
    ST_REQUIRE_MSG(st == SRTS_LISTENING, "the listener reports %s after an accept, expected SRTS_LISTENING",
                   st_state_name(st));

    std::string sent = st_payload(0, 256);
    int rc = srt_sendmsg(L.cal, sent.data(), (int)sent.size(), -1, 1);
    ST_REQUIRE_MSG(rc == (int)sent.size(), "srt_sendmsg on the connected caller returned %d: %s", rc,
                   srt_getlasterror_str());
    std::vector<std::string> got = st_recv_stream(L.acc, 1, 256, 5000);
    ST_REQUIRE_MSG(got.size() == 1, "the CONNECTED link delivered %zu of 1 messages", got.size());
    ST_REQUIRE_MSG(got[0] == sent, "the CONNECTED link delivered %zu bytes that differ from the %zu sent",
                   got[0].size(), sent.size());
}

SRT_CASE(g01_lifecycle, close_moves_a_socket_to_closed) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ST_SRT_OK(srt_close(s));
    SRT_SOCKSTATUS st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_CLOSED, "an unbound socket reports %s after srt_close, expected SRTS_CLOSED",
                   st_state_name(st));

    Link L;
    RelaySched sched;
    st_connect(L, sched);
    SRTSOCKET cal = L.cal;
    st = srt_getsockstate(cal);
    ST_REQUIRE_MSG(st == SRTS_CONNECTED, "the caller reports %s before srt_close, expected SRTS_CONNECTED",
                   st_state_name(st));
    ST_SRT_OK(srt_close(cal));
    L.cal = SRT_INVALID_SOCK;  // the Link must not close it a second time
    st = srt_getsockstate(cal);
    ST_REQUIRE_MSG(st == SRTS_CLOSED, "a connected socket reports %s after srt_close, expected SRTS_CLOSED",
                   st_state_name(st));
}

SRT_CASE(g01_lifecycle, listen_before_bind_is_refused_and_leaves_the_state_alone) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    int rc = srt_listen(s, 1);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_listen on an unbound socket returned %d, expected SRT_ERROR",
                   rc);
    ST_REQUIRE_MSG(err == SRT_EUNBOUNDSOCK, "srt_listen on an unbound socket set error %d, expected SRT_EUNBOUNDSOCK (%d)",
                   err, (int)SRT_EUNBOUNDSOCK);
    SRT_SOCKSTATUS st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_INIT, "a refused srt_listen left the socket in %s, expected SRTS_INIT",
                   st_state_name(st));

    // Positive control: the same call succeeds once the socket is bound.
    sockaddr_in a = st_loopback(0);
    ST_SRT_OK(srt_bind(s, (sockaddr*)&a, sizeof a));
    ST_SRT_OK(srt_listen(s, 1));
    st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_LISTENING, "after bind the same srt_listen left the socket in %s, expected SRTS_LISTENING",
                   st_state_name(st));
    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g01_lifecycle, accept_without_listen_is_refused_and_leaves_the_state_alone) {
    SRTSOCKET s = st_bound_socket(NULL);
    SRT_SOCKSTATUS st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_OPENED, "a bound socket reports %s, expected SRTS_OPENED",
                   st_state_name(st));

    SRTSOCKET acc = srt_accept(s, NULL, NULL);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(acc == SRT_INVALID_SOCK, "srt_accept on a bound-but-not-listening socket returned %d, expected SRT_INVALID_SOCK",
                   (int)acc);
    ST_REQUIRE_MSG(err == SRT_ENOLISTEN, "srt_accept without srt_listen set error %d, expected SRT_ENOLISTEN (%d)",
                   err, (int)SRT_ENOLISTEN);
    st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_OPENED, "a refused srt_accept left the socket in %s, expected SRTS_OPENED",
                   st_state_name(st));
    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g01_lifecycle, connect_on_a_listening_socket_is_refused) {
    SRTSOCKET s = st_bound_socket(NULL);
    ST_SRT_OK(srt_listen(s, 1));
    SRT_SOCKSTATUS st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_LISTENING, "the socket reports %s before srt_connect, expected SRTS_LISTENING",
                   st_state_name(st));

    sockaddr_in ra = st_loopback(pick_port());
    int rc = srt_connect(s, (sockaddr*)&ra, sizeof ra);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_connect on a LISTENING socket returned %d, expected SRT_ERROR",
                   rc);
    ST_REQUIRE_MSG(err == SRT_ECONNSOCK, "srt_connect on a LISTENING socket set error %d, expected SRT_ECONNSOCK (%d)",
                   err, (int)SRT_ECONNSOCK);
    st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_LISTENING, "a refused srt_connect left the socket in %s, expected SRTS_LISTENING",
                   st_state_name(st));
    ST_SRT_OK(srt_close(s));
}

// RQ-01-04 names three operations invalid for a socket's state: listening on
// a connected socket, sending on an unconnected one, and binding twice. The
// three cases above cover the neighbouring refusals; these are the three the
// requirement actually names.
//
// Only the refusal is asserted, not the code. The released library answers
// two of these with a code whose name does not describe the situation -- a
// second bind reports "operation not supported" and a listen on a connected
// socket reports "unbound socket" -- and pinning either would be asserting an
// upstream quirk rather than an interface. `TEST_INTERFACE.md` says so.
SRT_CASE(g01_lifecycle, the_three_operations_invalid_for_a_state_are_refused) {
    // Binding twice.
    SRTSOCKET s = st_bound_socket(NULL);
    SRT_SOCKSTATUS before = srt_getsockstate(s);
    ST_REQUIRE_MSG(before == SRTS_OPENED, "a bound socket reports %s, expected SRTS_OPENED",
                   st_state_name(before));
    sockaddr_in again = st_loopback(0);
    srt_clearlasterror();
    int rc = srt_bind(s, (sockaddr*)&again, sizeof again);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "a second srt_bind on the same socket returned %d, expected "
                                    "SRT_ERROR",
                   rc);
    ST_REQUIRE_MSG(srt_getlasterror(NULL) != SRT_ESUCCESS,
                   "a refused second srt_bind left the last error at SRT_ESUCCESS");
    SRT_SOCKSTATUS after = srt_getsockstate(s);
    ST_REQUIRE_MSG(after == SRTS_OPENED, "a refused second srt_bind left the socket in %s, "
                                         "expected SRTS_OPENED",
                   st_state_name(after));

    // Sending on an unconnected socket.
    std::string msg = st_payload(3, 256);
    srt_clearlasterror();
    int sn = srt_sendmsg(s, msg.data(), (int)msg.size(), -1, 1);
    ST_REQUIRE_MSG(sn == SRT_ERROR, "srt_sendmsg on a bound-but-unconnected socket returned %d, "
                                    "expected SRT_ERROR",
                   sn);
    ST_REQUIRE_MSG(srt_getlasterror(NULL) == SRT_ENOCONN,
                   "srt_sendmsg on an unconnected socket reported %d, expected SRT_ENOCONN (%d)",
                   srt_getlasterror(NULL), (int)SRT_ENOCONN);
    ST_SRT_OK(srt_close(s));

    // Listening on a connected socket.
    Link L;
    RelaySched sched;
    st_connect(L, sched);
    SRT_SOCKSTATUS cs = srt_getsockstate(L.cal);
    ST_REQUIRE_MSG(cs == SRTS_CONNECTED, "the caller reports %s before srt_listen, expected "
                                         "SRTS_CONNECTED",
                   st_state_name(cs));
    srt_clearlasterror();
    int lr = srt_listen(L.cal, 1);
    ST_REQUIRE_MSG(lr == SRT_ERROR, "srt_listen on a CONNECTED socket returned %d, expected "
                                    "SRT_ERROR",
                   lr);
    ST_REQUIRE_MSG(srt_getlasterror(NULL) != SRT_ESUCCESS,
                   "a refused srt_listen left the last error at SRT_ESUCCESS");
    cs = srt_getsockstate(L.cal);
    ST_REQUIRE_MSG(cs == SRTS_CONNECTED, "a refused srt_listen left the caller in %s, expected "
                                         "SRTS_CONNECTED",
                   st_state_name(cs));
}

// The documentation gives SRTO_SNDSYN as the blocking flag for srt_connect
// and SRTO_RCVSYN as the one for srt_accept; both are cleared here so the
// case reads "non-blocking mode" on either reading.
SRT_CASE(g01_lifecycle, a_non_blocking_connect_reports_connecting) {
    uint16_t silent_port = 0;
    int silent = udp_socket_bound(0, &silent_port);  // bound, and never answers

    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    st_set_int(s, SRTO_SNDSYN, 0);
    st_set_int(s, SRTO_RCVSYN, 0);

    sockaddr_in ra = st_loopback(silent_port);
    int rc = srt_connect(s, (sockaddr*)&ra, sizeof ra);
    ST_REQUIRE_MSG(rc != SRT_ERROR, "a non-blocking srt_connect returned SRT_ERROR: %s",
                   srt_getlasterror_str());
    SRT_SOCKSTATUS st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_CONNECTING,
                   "a non-blocking connect to a peer that never answers reports %s, expected SRTS_CONNECTING",
                   st_state_name(st));

    ST_SRT_OK(srt_close(s));
    ::close(silent);
}

// ================================================================
// g02_sockopt
//   the documented socket options are readable at their documented defaults,
//   accept values of the documented type and width, and refuse a set that the
//   documentation places before connect once the socket is connected.
// ================================================================

SRT_CASE(g02_sockopt, int_options_read_their_documented_defaults) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_MSS), 1500);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_FC), 25600);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_CONNTIMEO), 3000);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_SNDTIMEO), -1);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_RCVTIMEO), -1);

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g02_sockopt, buffer_options_read_their_documented_defaults) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // Documented as 8192 * (1500-28), i.e. the packet count times the payload
    // left by an IPv4+UDP header at the default MSS.
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_SNDBUF), 8192 * (1500 - 28));
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_RCVBUF), 8192 * (1500 - 28));
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_UDP_SNDBUF), 65536);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_UDP_RCVBUF), 8192 * 1500);

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g02_sockopt, bool_options_read_their_documented_defaults_at_the_documented_width) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    ST_REQUIRE_MSG(st_get_bool(s, SRTO_SNDSYN), "SRTO_SNDSYN default is false, documented as true");
    ST_REQUIRE_MSG(st_get_bool(s, SRTO_RCVSYN), "SRTO_RCVSYN default is false, documented as true");
    ST_REQUIRE_MSG(st_get_bool(s, SRTO_REUSEADDR),
                   "SRTO_REUSEADDR default is false, documented as true");
    ST_REQUIRE_MSG(st_get_bool(s, SRTO_NAKREPORT),
                   "SRTO_NAKREPORT default is false, documented as true");
    ST_REQUIRE_MSG(!st_get_bool(s, SRTO_RENDEZVOUS),
                   "SRTO_RENDEZVOUS default is true, documented as false");

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g02_sockopt, wide_options_read_at_their_documented_type) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // SRTO_MAXBW is documented as int64_t, default -1 (infinite).
    int64_t bw = 0;
    int n = (int)sizeof bw;
    ST_SRT_OK(srt_getsockflag(s, SRTO_MAXBW, &bw, &n));
    ST_REQUIRE_MSG(n == (int)sizeof(int64_t), "SRTO_MAXBW returned %d bytes, expected %zu (int64_t)",
                   n, sizeof(int64_t));
    ST_REQUIRE_MSG(bw == -1, "SRTO_MAXBW default is %lld, documented as -1", (long long)bw);

    // SRTO_LINGER is documented as `linger`, default on with 180 s.
    struct linger lg;
    std::memset(&lg, 0, sizeof lg);
    int nl = (int)sizeof lg;
    ST_SRT_OK(srt_getsockflag(s, SRTO_LINGER, &lg, &nl));
    ST_REQUIRE_MSG(nl == (int)sizeof(struct linger), "SRTO_LINGER returned %d bytes, expected %zu (struct linger)",
                   nl, sizeof(struct linger));
    ST_REQUIRE_MSG(lg.l_onoff != 0, "SRTO_LINGER default l_onoff is %d, documented as on",
                   (int)lg.l_onoff);
    ST_REQUIRE_MSG(lg.l_linger == 180, "SRTO_LINGER default l_linger is %d, documented as 180",
                   (int)lg.l_linger);

    ST_SRT_OK(srt_close(s));
}

// "The setting option function may accept both int and bool types."
SRT_CASE(g02_sockopt, set_accepts_the_documented_types_and_widths) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    bool b = false;
    ST_SRT_OK(srt_setsockflag(s, SRTO_RCVSYN, &b, (int)sizeof b));
    ST_REQUIRE_MSG(!st_get_bool(s, SRTO_RCVSYN),
                   "SRTO_RCVSYN set to false as a %zu-byte bool still reads true", sizeof(bool));

    int iv = 1;
    ST_SRT_OK(srt_setsockflag(s, SRTO_RCVSYN, &iv, (int)sizeof iv));
    ST_REQUIRE_MSG(st_get_bool(s, SRTO_RCVSYN),
                   "SRTO_RCVSYN set to 1 as a %zu-byte int still reads false", sizeof(int));

    int64_t bw = 3000000;
    ST_SRT_OK(srt_setsockflag(s, SRTO_MAXBW, &bw, (int)sizeof bw));
    int64_t rb = 0;
    int n = (int)sizeof rb;
    ST_SRT_OK(srt_getsockflag(s, SRTO_MAXBW, &rb, &n));
    ST_REQUIRE_MSG(n == (int)sizeof(int64_t), "SRTO_MAXBW returned %d bytes, expected %zu (int64_t)",
                   n, sizeof(int64_t));
    ST_REQUIRE_MSG(rb == bw, "SRTO_MAXBW set to %lld reads back %lld", (long long)bw, (long long)rb);

    // SRTO_LATENCY is documented as int32_t, "positive only".
    st_set_int(s, SRTO_LATENCY, 350);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_LATENCY), 350);

    ST_SRT_OK(srt_close(s));
}

// The other half of "typed as documented": an option the table marks (r) is
// readable and not writable. Three of them are tried, so a library that
// happens to reject one name by accident does not pass. The refusal is
// asserted, not the code -- the released library answers all three with
// "operation not supported", which is reasonable but is not documented
// anywhere, so it is not required.
SRT_CASE(g02_sockopt, a_read_only_option_refuses_a_set) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    struct RO {
        SRT_SOCKOPT opt;
        const char* name;
    };
    const RO ro[] = {
        {SRTO_STATE, "SRTO_STATE"},
        {SRTO_EVENT, "SRTO_EVENT"},
        {SRTO_RCVDATA, "SRTO_RCVDATA"},
    };
    for (int i = 0; i < 3; i++) {
        int readable = -1;
        int n = (int)sizeof readable;
        ST_REQUIRE_MSG(srt_getsockflag(s, ro[i].opt, &readable, &n) != SRT_ERROR,
                       "%s is documented (r) but could not be read: %s", ro[i].name,
                       srt_getlasterror_str());

        int v = 1;
        srt_clearlasterror();
        int rc = srt_setsockflag(s, ro[i].opt, &v, (int)sizeof v);
        ST_REQUIRE_MSG(rc == SRT_ERROR, "%s is documented read-only but a set returned %d",
                       ro[i].name, rc);
        ST_REQUIRE_MSG(srt_getlasterror(NULL) != SRT_ESUCCESS,
                       "a refused set of %s left the last error at SRT_ESUCCESS", ro[i].name);
    }

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g02_sockopt, pbkeylen_takes_the_documented_lengths_and_refuses_others) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_PBKEYLEN), 0);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_SENDER), 0);

    // SRTO_PBKEYLEN is "the sender encryption key length", so the sender flag
    // has to be on for the value to be the one being read back.
    st_set_int(s, SRTO_SENDER, 1);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_SENDER), 1);

    const int documented[3] = {16, 24, 32};
    for (int i = 0; i < 3; i++) {
        st_set_int(s, SRTO_PBKEYLEN, documented[i]);
        int back = st_get_int(s, SRTO_PBKEYLEN);
        ST_REQUIRE_MSG(back == documented[i], "SRTO_PBKEYLEN set to %d reads back %d", documented[i],
                       back);
    }

    int rc = st_try_set_int(s, SRTO_PBKEYLEN, 20);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "SRTO_PBKEYLEN accepted 20, which is outside the documented set {0,16,24,32} (returned %d)",
                   rc);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM, "a refused SRTO_PBKEYLEN set error %d, expected SRT_EINVPARAM (%d)",
                   err, (int)SRT_EINVPARAM);
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_PBKEYLEN), 32);

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g02_sockopt, a_pre_option_is_refused_once_the_socket_is_connected) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);

    // SRTO_FC is documented "pre".
    int fc_before = st_get_int(L.cal, SRTO_FC);
    int rc = st_try_set_int(L.cal, SRTO_FC, 8192);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "SRTO_FC, documented as a pre option, was accepted on a CONNECTED socket (returned %d)",
                   rc);
    ST_REQUIRE_MSG(err == SRT_ECONNSOCK, "a refused post-connect SRTO_FC set error %d, expected SRT_ECONNSOCK (%d)",
                   err, (int)SRT_ECONNSOCK);
    int fc_after = st_get_int(L.cal, SRTO_FC);
    ST_REQUIRE_MSG(fc_after == fc_before, "a refused SRTO_FC set changed the value from %d to %d",
                   fc_before, fc_after);

    // SRTO_TSBPDMODE is documented "pre" as well.
    rc = st_try_set_int(L.cal, SRTO_TSBPDMODE, 0);
    err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "SRTO_TSBPDMODE, documented as a pre option, was accepted on a CONNECTED socket (returned %d)",
                   rc);
    ST_REQUIRE_MSG(err == SRT_ECONNSOCK, "a refused post-connect SRTO_TSBPDMODE set error %d, expected SRT_ECONNSOCK (%d)",
                   err, (int)SRT_ECONNSOCK);

    // Positive control 1: a "post" option is accepted on the very same socket,
    // so the refusals above are about the option, not about the socket.
    st_set_int(L.cal, SRTO_RCVTIMEO, 1234);
    ST_REQUIRE_EQ_INT(st_get_int(L.cal, SRTO_RCVTIMEO), 1234);

    // Positive control 2: the same "pre" option is accepted before connect.
    SRTSOCKET fresh = st_new_socket();
    ST_REQUIRE_MSG(fresh != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    st_set_int(fresh, SRTO_FC, 8192);
    ST_REQUIRE_EQ_INT(st_get_int(fresh, SRTO_FC), 8192);
    ST_SRT_OK(srt_close(fresh));
}

// ================================================================
// g03_errors
//   a failed call sets a documented error code that srt_getlasterror
//   reports, srt_strerror renders and srt_clearlasterror resets, and a
//   successful call leaves it alone.
// ================================================================

SRT_CASE(g03_errors, a_failed_call_sets_the_documented_code_and_getlasterror_reports_it) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    srt_clearlasterror();

    SRTSOCKET never = st_never_allocated(s);
    int v = 0;
    int n = (int)sizeof v;
    int rc = srt_getsockflag(never, SRTO_MSS, &v, &n);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_getsockflag on an id that was never allocated returned %d, expected SRT_ERROR",
                   rc);

    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(err == SRT_EINVSOCK, "srt_getlasterror reported %d, expected SRT_EINVSOCK (%d)",
                   err, (int)SRT_EINVSOCK);

    std::string reported = srt_getlasterror_str();
    ST_REQUIRE_MSG(!reported.empty(), "srt_getlasterror_str rendered an empty message for SRT_EINVSOCK");
    std::string rendered = srt_strerror(SRT_EINVSOCK, 0);
    ST_REQUIRE_MSG(rendered == reported, "srt_strerror(SRT_EINVSOCK) is \"%s\" but srt_getlasterror_str is \"%s\"",
                   rendered.c_str(), reported.c_str());

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g03_errors, strerror_renders_each_documented_code_distinctly) {
    struct Entry {
        int code;
        const char* name;
    };
    const Entry table[] = {
        {SRT_ESUCCESS, "SRT_ESUCCESS"},   {SRT_ENOCONN, "SRT_ENOCONN"},
        {SRT_ECONNSOCK, "SRT_ECONNSOCK"}, {SRT_EINVPARAM, "SRT_EINVPARAM"},
        {SRT_EINVSOCK, "SRT_EINVSOCK"},   {SRT_EUNBOUNDSOCK, "SRT_EUNBOUNDSOCK"},
        {SRT_ENOLISTEN, "SRT_ENOLISTEN"}, {SRT_EINVPOLLID, "SRT_EINVPOLLID"},
        {SRT_ETIMEOUT, "SRT_ETIMEOUT"},
    };
    const int count = (int)(sizeof table / sizeof table[0]);

    std::vector<std::string> msgs;
    for (int i = 0; i < count; i++) {
        std::string m = srt_strerror(table[i].code, 0);
        ST_REQUIRE_MSG(!m.empty(), "srt_strerror(%s = %d) rendered an empty message", table[i].name,
                       table[i].code);
        msgs.push_back(m);
    }
    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++)
            ST_REQUIRE_MSG(msgs[(size_t)i] != msgs[(size_t)j],
                           "srt_strerror renders %s and %s identically as \"%s\"", table[i].name,
                           table[j].name, msgs[(size_t)i].c_str());
}

SRT_CASE(g03_errors, clearlasterror_resets_the_error_without_disabling_reporting) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(s, 1) == SRT_ERROR, "srt_listen on an unbound socket unexpectedly succeeded");
    ST_REQUIRE_MSG(srt_getlasterror(NULL) == SRT_EUNBOUNDSOCK,
                   "srt_listen on an unbound socket reported %d, expected SRT_EUNBOUNDSOCK (%d)",
                   srt_getlasterror(NULL), (int)SRT_EUNBOUNDSOCK);

    srt_clearlasterror();
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(err == SRT_ESUCCESS, "after srt_clearlasterror the code is %d, expected SRT_ESUCCESS (0)",
                   err);
    std::string cleared = srt_getlasterror_str();
    std::string success = srt_strerror(SRT_ESUCCESS, 0);
    ST_REQUIRE_MSG(cleared == success, "after srt_clearlasterror the message is \"%s\", expected \"%s\"",
                   cleared.c_str(), success.c_str());

    // The reset must not be a permanent mute: the next failure is reported.
    SRTSOCKET s2 = st_new_socket();
    ST_REQUIRE_MSG(s2 != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(s2, 1) == SRT_ERROR, "srt_listen on an unbound socket unexpectedly succeeded");
    err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(err == SRT_EUNBOUNDSOCK, "after a clear, the next failure reported %d, expected SRT_EUNBOUNDSOCK (%d)",
                   err, (int)SRT_EUNBOUNDSOCK);

    ST_SRT_OK(srt_close(s));
    ST_SRT_OK(srt_close(s2));
}

SRT_CASE(g03_errors, a_successful_call_leaves_the_last_error_alone) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(s, 1) == SRT_ERROR, "srt_listen on an unbound socket unexpectedly succeeded");
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(err == SRT_EUNBOUNDSOCK, "srt_listen on an unbound socket reported %d, expected SRT_EUNBOUNDSOCK (%d)",
                   err, (int)SRT_EUNBOUNDSOCK);
    std::string before = srt_getlasterror_str();

    // A run of calls that all succeed.
    sockaddr_in a = st_loopback(0);
    ST_SRT_OK(srt_bind(s, (sockaddr*)&a, sizeof a));
    ST_SRT_OK(srt_listen(s, 1));
    ST_REQUIRE_EQ_INT(st_get_int(s, SRTO_MSS), 1500);
    st_set_int(s, SRTO_RCVTIMEO, 500);

    int after = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(after == err, "a run of successful calls changed the last error from %d to %d",
                   err, after);
    std::string after_msg = srt_getlasterror_str();
    ST_REQUIRE_MSG(after_msg == before, "a run of successful calls changed the last error message from \"%s\" to \"%s\"",
                   before.c_str(), after_msg.c_str());

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g03_errors, sending_on_an_unconnected_socket_reports_enoconn) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    std::string m = st_payload(0, 128);

    srt_clearlasterror();
    int rc = srt_sendmsg(s, m.data(), (int)m.size(), -1, 1);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_sendmsg on an unconnected socket returned %d, expected SRT_ERROR",
                   rc);
    ST_REQUIRE_MSG(err == SRT_ENOCONN, "srt_sendmsg on an unconnected socket reported %d, expected SRT_ENOCONN (%d)",
                   err, (int)SRT_ENOCONN);
    std::string reported = srt_getlasterror_str();
    std::string rendered = srt_strerror(SRT_ENOCONN, 0);
    ST_REQUIRE_MSG(rendered == reported, "srt_strerror(SRT_ENOCONN) is \"%s\" but srt_getlasterror_str is \"%s\"",
                   rendered.c_str(), reported.c_str());
    ST_SRT_OK(srt_close(s));

    // Positive control: the same call over a connection succeeds and reports
    // no error, so the failure above is the missing connection.
    Link L;
    RelaySched sched;
    st_connect(L, sched);
    srt_clearlasterror();
    rc = srt_sendmsg(L.cal, m.data(), (int)m.size(), -1, 1);
    ST_REQUIRE_MSG(rc == (int)m.size(), "srt_sendmsg on a connected socket returned %d, expected %d: %s",
                   rc, (int)m.size(), srt_getlasterror_str());
    err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(err == SRT_ESUCCESS, "a successful srt_sendmsg left the last error at %d, expected SRT_ESUCCESS (0)",
                   err);
}

SRT_CASE(g03_errors, epoll_wait_on_a_released_id_reports_einvpollid) {
    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    ST_SRT_OK(srt_epoll_release(eid));
    srt_clearlasterror();

    SRTSOCKET rbuf[2];
    int rn = 2;
    int rc = srt_epoll_wait(eid, rbuf, &rn, NULL, NULL, 100, NULL, NULL, NULL, NULL);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_epoll_wait on a released epoll id returned %d, expected SRT_ERROR",
                   rc);
    ST_REQUIRE_MSG(err == SRT_EINVPOLLID, "srt_epoll_wait on a released epoll id reported %d, expected SRT_EINVPOLLID (%d)",
                   err, (int)SRT_EINVPOLLID);
    // Only the code and the presence of a rendering are asserted here: this
    // exception carries a system errno, so the rendered text is not the
    // errno-free rendering srt_strerror(code, 0) produces.
    std::string reported = srt_getlasterror_str();
    ST_REQUIRE_MSG(!reported.empty(), "srt_getlasterror_str rendered an empty message for SRT_EINVPOLLID");
}

// ================================================================
// g04_epoll
//   srt_epoll_wait reports readability and writability level-triggered, over
//   SRT sockets and system sockets in the same set, and honours its timeout.
// ================================================================

SRT_CASE(g04_epoll, a_connected_socket_is_reported_writable_every_time_it_is_asked) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int out_only = SRT_EPOLL_OUT;
    ST_SRT_OK(srt_epoll_add_usock(eid, L.cal, &out_only));

    std::vector<SRTSOCKET> wr;
    int rc = st_ep_wait(eid, 3000, NULL, &wr, NULL, NULL);
    ST_REQUIRE_MSG(rc > 0 && st_has(wr, L.cal),
                   "the first srt_epoll_wait returned %d and did not report the connected caller writable",
                   rc);
    // Level-triggered: nothing was consumed, so the same readiness is reported
    // again rather than only once as an edge.
    rc = st_ep_wait(eid, 1000, NULL, &wr, NULL, NULL);
    ST_REQUIRE_MSG(rc > 0 && st_has(wr, L.cal),
                   "the second srt_epoll_wait returned %d and did not report the still-writable caller: writability is not level-triggered",
                   rc);

    // Negative control: a socket that was never connected is not writable, so
    // the two reports above are not "everything is always ready".
    int eid2 = srt_epoll_create();
    ST_REQUIRE_MSG(eid2 >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    SRTSOCKET idle = st_new_socket();
    ST_REQUIRE_MSG(idle != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ST_SRT_OK(srt_epoll_add_usock(eid2, idle, &out_only));
    rc = st_ep_wait(eid2, 400, NULL, &wr, NULL, NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_epoll_wait returned %d for an unconnected socket registered for SRT_EPOLL_OUT",
                   rc);
    ST_SRT_OK(srt_close(idle));

    ST_SRT_OK(srt_epoll_release(eid));
    ST_SRT_OK(srt_epoll_release(eid2));
}

SRT_CASE(g04_epoll, readability_appears_only_with_data_and_is_reported_every_time) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int in_only = SRT_EPOLL_IN;
    ST_SRT_OK(srt_epoll_add_usock(eid, L.acc, &in_only));

    std::vector<SRTSOCKET> rd;
    int rc = st_ep_wait(eid, 400, &rd, NULL, NULL, NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_epoll_wait returned %d before anything was sent, expected a timeout",
                   rc);

    std::string sent = st_payload(3, 256);
    int sn = srt_sendmsg(L.cal, sent.data(), (int)sent.size(), -1, 1);
    ST_REQUIRE_MSG(sn == (int)sent.size(), "srt_sendmsg returned %d: %s", sn, srt_getlasterror_str());

    rc = st_ep_wait(eid, 5000, &rd, NULL, NULL, NULL);
    ST_REQUIRE_MSG(rc > 0 && st_has(rd, L.acc),
                   "srt_epoll_wait returned %d and did not report the receiver readable after one message",
                   rc);

    // Level-triggered: the message has not been read, so the readiness is
    // still reported on the next call.
    rc = st_ep_wait(eid, 1000, &rd, NULL, NULL, NULL);
    ST_REQUIRE_MSG(rc > 0 && st_has(rd, L.acc),
                   "srt_epoll_wait returned %d on the second ask with the message still unread: readability is not level-triggered",
                   rc);

    ST_SRT_OK(srt_epoll_release(eid));
}

SRT_CASE(g04_epoll, readability_stops_being_reported_once_the_data_is_read) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);
    st_set_int(L.acc, SRTO_RCVTIMEO, 5000);

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int in_only = SRT_EPOLL_IN;
    ST_SRT_OK(srt_epoll_add_usock(eid, L.acc, &in_only));

    std::string sent = st_payload(5, 256);
    int sn = srt_sendmsg(L.cal, sent.data(), (int)sent.size(), -1, 1);
    ST_REQUIRE_MSG(sn == (int)sent.size(), "srt_sendmsg returned %d: %s", sn, srt_getlasterror_str());

    std::vector<SRTSOCKET> rd;
    int rc = st_ep_wait(eid, 5000, &rd, NULL, NULL, NULL);
    ST_REQUIRE_MSG(rc > 0 && st_has(rd, L.acc),
                   "srt_epoll_wait returned %d and did not report the receiver readable", rc);

    std::string buf(1024, '\0');
    int n = srt_recvmsg(L.acc, &buf[0], (int)buf.size());
    ST_REQUIRE_MSG(n == (int)sent.size(), "srt_recvmsg returned %d, expected %d: %s", n,
                   (int)sent.size(), srt_getlasterror_str());
    ST_REQUIRE_MSG(buf.compare(0, sent.size(), sent) == 0,
                   "the message the epoll reported readable was not the one that was sent");

    rc = st_ep_wait(eid, 400, &rd, NULL, NULL, NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_epoll_wait returned %d after the only message was read, expected a timeout",
                   rc);

    ST_SRT_OK(srt_epoll_release(eid));
}

SRT_CASE(g04_epoll, one_wait_reports_an_srt_socket_and_a_system_socket_together) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);
    st_set_int(L.acc, SRTO_RCVTIMEO, 5000);

    int fds[2];
    ST_REQUIRE_MSG(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0, "socketpair failed");

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int in_only = SRT_EPOLL_IN;
    ST_SRT_OK(srt_epoll_add_usock(eid, L.acc, &in_only));
    ST_SRT_OK(srt_epoll_add_ssock(eid, fds[0], &in_only));

    std::vector<SRTSOCKET> rd;
    std::vector<SYSSOCKET> lrd;
    int rc = st_ep_wait(eid, 400, &rd, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_epoll_wait returned %d with neither socket ready, expected a timeout",
                   rc);

    std::string sent = st_payload(9, 256);
    int sn = srt_sendmsg(L.cal, sent.data(), (int)sent.size(), -1, 1);
    ST_REQUIRE_MSG(sn == (int)sent.size(), "srt_sendmsg returned %d: %s", sn, srt_getlasterror_str());
    char one = 'x';
    ST_REQUIRE_MSG(::write(fds[1], &one, 1) == 1, "write to the system socket failed");

    // Both are level-triggered and neither is consumed, so once the SRT side
    // has arrived a single call must report both of them.
    bool both = false;
    int64_t t0 = st_now_ms();
    while (st_now_ms() - t0 < 8000) {
        rc = st_ep_wait(eid, 500, &rd, NULL, &lrd, NULL);
        if (rc > 0 && st_has(rd, L.acc) && st_has(lrd, fds[0])) {
            both = true;
            break;
        }
    }
    ST_REQUIRE_MSG(both, "no single srt_epoll_wait reported both the SRT socket and the system socket readable");

    // Both readinesses were real, not labels.
    std::string buf(1024, '\0');
    int n = srt_recvmsg(L.acc, &buf[0], (int)buf.size());
    ST_REQUIRE_MSG(n == (int)sent.size(), "srt_recvmsg returned %d, expected %d: %s", n,
                   (int)sent.size(), srt_getlasterror_str());
    ST_REQUIRE_MSG(buf.compare(0, sent.size(), sent) == 0,
                   "the SRT message reported readable was not the one that was sent");
    char back = 0;
    ST_REQUIRE_MSG(::read(fds[0], &back, 1) == 1, "read from the system socket failed");
    ST_REQUIRE_MSG(back == one, "the system socket reported readable delivered '%c', expected '%c'",
                   back, one);

    ST_SRT_OK(srt_epoll_release(eid));
    ::close(fds[0]);
    ::close(fds[1]);
}

SRT_CASE(g04_epoll, a_system_socket_reports_read_and_write_readiness_level_triggered) {
    int fds[2];
    ST_REQUIRE_MSG(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0, "socketpair failed");

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int in_only = SRT_EPOLL_IN;
    ST_SRT_OK(srt_epoll_add_ssock(eid, fds[0], &in_only));

    std::vector<SYSSOCKET> lrd, lwr;
    int rc = st_ep_wait(eid, 400, NULL, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_epoll_wait returned %d for a system socket with nothing to read",
                   rc);

    char one = 'q';
    ST_REQUIRE_MSG(::write(fds[1], &one, 1) == 1, "write to the system socket failed");
    rc = st_ep_wait(eid, 3000, NULL, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc > 0 && st_has(lrd, fds[0]),
                   "srt_epoll_wait returned %d and did not report the written-to system socket readable",
                   rc);
    rc = st_ep_wait(eid, 1000, NULL, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc > 0 && st_has(lrd, fds[0]),
                   "srt_epoll_wait returned %d on the second ask with the byte still unread: readability is not level-triggered",
                   rc);

    char back = 0;
    ST_REQUIRE_MSG(::read(fds[0], &back, 1) == 1, "read from the system socket failed");
    ST_REQUIRE_MSG(back == one, "the system socket delivered '%c', expected '%c'", back, one);
    rc = st_ep_wait(eid, 400, NULL, NULL, &lrd, NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_epoll_wait returned %d after the byte was read, expected a timeout",
                   rc);

    int out_only = SRT_EPOLL_OUT;
    ST_SRT_OK(srt_epoll_update_ssock(eid, fds[0], &out_only));
    rc = st_ep_wait(eid, 3000, NULL, NULL, NULL, &lwr);
    ST_REQUIRE_MSG(rc > 0 && st_has(lwr, fds[0]),
                   "srt_epoll_wait returned %d and did not report an empty system socket writable", rc);

    ST_SRT_OK(srt_epoll_release(eid));
    ::close(fds[0]);
    ::close(fds[1]);
}

SRT_CASE(g04_epoll, wait_honours_its_timeout) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int in_only = SRT_EPOLL_IN;
    ST_SRT_OK(srt_epoll_add_usock(eid, L.acc, &in_only));

    const int64_t timeo = 700;
    std::vector<SRTSOCKET> rd;
    int64_t t0 = st_now_ms();
    int rc = st_ep_wait(eid, timeo, &rd, NULL, NULL, NULL);
    int err = srt_getlasterror(NULL);
    int64_t dt = st_now_ms() - t0;

    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_epoll_wait returned %d with nothing to read, expected a timeout",
                   rc);
    ST_REQUIRE_MSG(err == SRT_ETIMEOUT, "an expired srt_epoll_wait reported %d, expected SRT_ETIMEOUT (%d)",
                   err, (int)SRT_ETIMEOUT);
    // One-sided and generous: the wait must not come back early.
    ST_REQUIRE_MSG(dt >= timeo - 50, "srt_epoll_wait(%lld ms) returned after only %lld ms",
                   (long long)timeo, (long long)dt);
    ST_REQUIRE_MSG(dt < timeo + 10000, "srt_epoll_wait(%lld ms) took %lld ms", (long long)timeo,
                   (long long)dt);

    // Positive control: the same eid does report readiness, so the timeout
    // above was the timeout and not a broken registration.
    std::string sent = st_payload(11, 256);
    int sn = srt_sendmsg(L.cal, sent.data(), (int)sent.size(), -1, 1);
    ST_REQUIRE_MSG(sn == (int)sent.size(), "srt_sendmsg returned %d: %s", sn, srt_getlasterror_str());
    rc = st_ep_wait(eid, 5000, &rd, NULL, NULL, NULL);
    ST_REQUIRE_MSG(rc > 0 && st_has(rd, L.acc),
                   "the same srt_epoll_wait returned %d after a message was sent, expected the receiver to be reported readable",
                   rc);

    ST_SRT_OK(srt_epoll_release(eid));
}

// RQ-01-10 names srt_epoll_remove_usock as part of managing the set. Removal
// is the only one of the four with no observable effect of its own, so it
// needs the before/after pair: the same wait on the same eid reports the
// socket, then does not.
SRT_CASE(g04_epoll, a_removed_socket_stops_being_reported) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);

    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int out_only = SRT_EPOLL_OUT;
    ST_SRT_OK(srt_epoll_add_usock(eid, L.cal, &out_only));

    std::vector<SRTSOCKET> wr;
    int rc = st_ep_wait(eid, 3000, NULL, &wr, NULL, NULL);
    ST_REQUIRE_MSG(rc > 0 && st_has(wr, L.cal),
                   "srt_epoll_wait returned %d and did not report the registered caller writable "
                   "before the removal",
                   rc);

    ST_SRT_OK(srt_epoll_remove_usock(eid, L.cal));

    rc = st_ep_wait(eid, 700, NULL, &wr, NULL, NULL);
    ST_REQUIRE_MSG(!st_has(wr, L.cal),
                   "srt_epoll_wait still reports a socket that was removed from the set");
    ST_REQUIRE_MSG(rc <= 0, "srt_epoll_wait returned %d for an empty set, expected no readiness",
                   rc);

    ST_SRT_OK(srt_epoll_release(eid));
}
