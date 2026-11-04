// Step 10 (SRT v1.5.2) -- `connect` family. Three scored groups:
//
//   g04_argcheck     new  srt_connect with a zero address length, and srt_bind
//                         of the IPv6 wildcard on a socket whose IPv6-only flag
//                         is still at its -1 default, are both refused with
//                         SRT_EINVPARAM -- while the same connect at the true
//                         length and the same bind with the flag set to 0 or to
//                         1 still succeed.
//   g05_muxer_reuse  new  a socket at the -1 default binds an address and port
//                         already bound by a socket that set the flag to 0 and
//                         succeeds by sharing the multiplexer; the same two
//                         binds in the opposite order are still refused with
//                         SRT_EBINDCONFLICT.
//   g06_reject_hook  new  a srt_listen_callback hook returning -1 without
//                         naming a reason leaves the caller SRT_REJC_PREDEFINED
//                         (1000); a hook that does name one leaves that value.
//
// ---------------------------------------------------------------- tiers
//
// Every group here is `new` (the gate lives in the `live` family, not this
// one), so nothing in this file has to be earnable by a candidate who stopped
// at v1.5.0 and DRIVER_RULES' dlsym / restatement / static_assert machinery
// buys nothing here. It is deliberately NOT used: a `new` group that still
// compiles AND PASSES at the previous milestone is a group that fails to
// discriminate, which is worse than one that does not build there.
//
// What happens at v1.5.0 is better than "does not build", and that is by
// design rather than by luck: stepdefs/10/driver-notes.md records that this
// family names no identifier this milestone introduces, and it was re-checked
// before a line was written. SRT_EINVPARAM, SRT_EBINDCONFLICT,
// SRT_REJC_PREDEFINED, SRT_REJC_USERDEFINED, SRTO_IPV6ONLY, SRTO_CONNTIMEO and
// srt_listen_callback / srt_setrejectreason / srt_getrejectreason all exist in
// v1.5.0's tree. So the family COMPILES there and each group fails on an
// ASSERTION naming actual against expected, which is what a `new` group is
// supposed to demonstrate.
//
// ----------------------------------------------------------- traceability
//
// Every assertion below traces to a sentence of stepdefs/10/interface.md,
// which ships to the candidate as TEST_INTERFACE.md section 5, and to a row of
// deltas/10.md (D2, D3 -> g04; D9 -> g05; D5 -> g06). The row is named on each
// group. Nothing is asserted from the pinned srt-docs/ alone -- and where the
// two disagree, interface.md is what this file checks, because the designer
// measured it against the tag:
//
//   * srt-docs/ says a listener hook's rejection "is predefined as
//     SRT_REJ_RESOURCE" (3). It is not; interface.md records 1000 and 1000 is
//     what g06 asserts.
//   * srt-docs/'s binding table says nothing about how SRTO_IPV6ONLY affects
//     multiplexer sharing for an ordinary IPv4 address in either order.
//     interface.md states the asymmetry and g05 asserts exactly that shape.
//
// Measured at both installed prefixes with a standalone probe before this file
// was written, and reproduced by the family itself:
//
//   v1.5.2                                  v1.5.0
//   connect(namelen=0)  -1 / 5003           0, socket reaches SRTS_CONNECTED
//   bind([::]:0, flag -1)  -1 / 5003        0, the bind succeeds
//   bind(127.0.0.1:P) 0-then--1   0         -1 / 5015 (SRT_EBINDCONFLICT)
//   bind(127.0.0.1:P) -1-then-0   -1 / 5015 -1 / 5015   (the negative control)
//   hook -1, no reason   reject 1000        reject 4 (SRT_REJ_ROGUE)
//   hook -1, reason 2007 reject 2007        reject 2007 (the positive control)
//
// ------------------------------------------------- deliberately NOT asserted
//
//   * How long a rejected connect takes. interface.md says in as many words
//     not to: the rejection is prompt at both milestones and only the reported
//     reason distinguishes them. deltas/10.md's "Driver hazards" records that
//     the row's "~3000 ms vs < 500 ms" timing form is false.
//   * Any namelen other than 0. interface.md asserts only 0, and deltas/10.md
//     records that 8 and 15 are refused at both tags, so they discriminate
//     nothing. No case ever passes a null address pointer: v1.5.0 dereferences
//     it before the length is looked at.
//   * srt_rejectreason_str() text. deltas/10.md measured entry 17 changing
//     under the LIBRARY's build flag, so reason strings are not assertable by
//     text here.
//   * SRT_REJ_E_SIZE / SRTO_E_SIZE, which move under this driver's own
//     -DENABLE_AEAD_API_PREVIEW (63 and 18 with it, 62 and 17 without). No
//     sweep here is bounded by either.
//   * Which SIDE observes the refusals. interface.md names the caller for the
//     rejection reason and says nothing about the listener's view, so g06 asks
//     only the caller.
#include "srtkit.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// =========================================================== shared shape ===
//
// Nothing in this section reimplements srtkit.h. st_new_socket, st_loopback,
// st_set_int, st_payload, st_send_stream and st_recv_stream are used as they
// come; what is added here is the three things the kit cannot offer, each
// noted because DRIVER_RULES asks for it:
//
//   1. an error code captured at the instant of the failing call. Every
//      srt_* call overwrites the thread's last error, including the ones that
//      SUCCEED -- a getsockname between the failure and the read reports
//      "Success" and the case would assert against the wrong number. The
//      probe that measured this family hit exactly that.
//   2. a bind/connect that is ALLOWED to fail. srtkit's st_set_int and
//      st_connect assert success, which is the opposite of what two of these
//      three groups are about.
//   3. a bounded accept. srt_accept does not honour SRTO_RCVTIMEO on this
//      release -- the `live` family measured the same -- so a listener whose
//      handshake was refused leaves a blocking accept hanging past the case
//      deadline, and the group would report a timeout instead of the refusal
//      it was written to observe.

// The return code of a call and the error the library recorded AT THAT MOMENT.
struct Outcome {
    int rc;
    int err;
};

static Outcome conn_bind(SRTSOCKET s, const sockaddr* a, int len) {
    Outcome o;
    o.rc  = srt_bind(s, a, len);
    o.err = srt_getlasterror(NULL);
    return o;
}

static Outcome conn_connect(SRTSOCKET s, const sockaddr* a, int len) {
    Outcome o;
    o.rc  = srt_connect(s, a, len);
    o.err = srt_getlasterror(NULL);
    return o;
}

// The error text belonging to a captured code, so a failure message names the
// number AND what the library calls it. srt_getlasterror_str() cannot be used
// for this: by the time a case formats its message the last error has moved on.
static const char* conn_errname(int code) {
    return srt_strerror(code, 0);
}

static SRTSOCKET conn_socket() {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    return s;
}

// The port a socket is actually bound to, from either address family.
static uint16_t conn_port_of(SRTSOCKET s) {
    sockaddr_storage ss;
    std::memset(&ss, 0, sizeof ss);
    int n = (int)sizeof ss;
    ST_REQUIRE_MSG(srt_getsockname(s, (sockaddr*)&ss, &n) != SRT_ERROR, "srt_getsockname: %s",
                   srt_getlasterror_str());
    if (ss.ss_family == AF_INET6) {
        sockaddr_in6 a6;
        std::memcpy(&a6, &ss, sizeof a6);
        return ntohs(a6.sin6_port);
    }
    sockaddr_in a4;
    std::memcpy(&a4, &ss, sizeof a4);
    return ntohs(a4.sin_port);
}

static int conn_family_of(SRTSOCKET s) {
    sockaddr_storage ss;
    std::memset(&ss, 0, sizeof ss);
    int n = (int)sizeof ss;
    ST_REQUIRE_MSG(srt_getsockname(s, (sockaddr*)&ss, &n) != SRT_ERROR, "srt_getsockname: %s",
                   srt_getlasterror_str());
    return (int)ss.ss_family;
}

// The IPv6 wildcard, `[::]:port`. Built here rather than in srtkit.h because
// the kit must compile at v1.2.0 and offers IPv4 only.
static sockaddr_in6 conn_v6_wildcard(uint16_t port) {
    sockaddr_in6 a;
    std::memset(&a, 0, sizeof a);
    a.sin6_family = AF_INET6;
    a.sin6_addr   = in6addr_any;
    a.sin6_port   = htons(port);
    return a;
}

// A listener on 127.0.0.1, closed with the case. `lconf` is applied before the
// bind so a pre-bind option (SRTO_IPV6ONLY) and a listen hook both land in
// time.
//
// The listener is put in non-blocking receive mode BEFORE `lconf` runs, so a
// group's own configuration cannot put it back into blocking mode and silently
// undo the bound on conn_accept_within below.
struct Listener {
    SRTSOCKET lsn = SRT_INVALID_SOCK;
    uint16_t port = 0;

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    explicit Listener(const SockConf& lconf = st_noconf) {
        lsn = conn_socket();
        st_set_int(lsn, SRTO_RCVSYN, 0);
        lconf(lsn);
        sockaddr_in a = st_loopback(0);
        ST_REQUIRE_MSG(srt_bind(lsn, (sockaddr*)&a, sizeof a) != SRT_ERROR,
                       "srt_bind(127.0.0.1:0) on the listener: %s", srt_getlasterror_str());
        port = conn_port_of(lsn);
        ST_REQUIRE_MSG(port != 0, "the listener bound to port 0, so no caller could reach it");
        ST_REQUIRE_MSG(srt_listen(lsn, 5) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());
    }
    ~Listener() {
        if (lsn != SRT_INVALID_SOCK) srt_close(lsn);
    }
};

// Poll a non-blocking listener to a deadline. Returns SRT_INVALID_SOCK when
// nothing was accepted inside `wait_ms`.
//
// The exit condition is the accept itself or SRT_EASYNCRCV plus a bound, never
// a sleep standing in for synchronisation: when the handshake completes the
// loop returns on its first or second pass, and only a connection that was
// genuinely refused pays the full bound.
//
// Blocking receive is restored on whatever comes back. SRTO_RCVSYN is
// INHERITED by the accepted socket, and an accepted socket left non-blocking
// makes every later srt_recvmsg return immediately with SRT_EASYNCRCV -- a
// transfer control would then read an empty channel and report loss that never
// happened.
static SRTSOCKET conn_accept_within(SRTSOCKET lsn, int wait_ms) {
    typedef std::chrono::steady_clock clk;
    clk::time_point deadline = clk::now() + std::chrono::milliseconds(wait_ms);
    for (;;) {
        SRTSOCKET acc = srt_accept(lsn, NULL, NULL);
        if (acc != SRT_INVALID_SOCK) {
            st_set_int(acc, SRTO_RCVSYN, 1);
            return acc;
        }
        if (srt_getlasterror(NULL) != SRT_EASYNCRCV) return SRT_INVALID_SOCK;
        if (clk::now() >= deadline) return SRT_INVALID_SOCK;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// How long to wait for an accept that is EXPECTED to arrive. Generous, and
// paid only if something is wrong: the loop returns as soon as the socket is
// queued.
static const int CONN_ACCEPT_MS = 6000;

// How long a listener that must NEVER produce an accepted socket is watched
// before the claim is made. The caller's connect has already returned by then,
// so the handshake is over and this is a confirmation, not a race.
static const int CONN_NO_ACCEPT_MS = 700;

// Bounds every caller's blocking connect. A refusal is reported in single-digit
// milliseconds at both milestones; this only stops a tree that answers nothing
// at all from eating the case deadline.
static const int CONN_CONNTIMEO_MS = 4000;

// Transfer shape for the positive controls. 1316 is the live payload size the
// transport negotiates by default, so no case here has to touch
// SRTO_PAYLOADSIZE to be delivered; eight messages make "everything arrived" a
// real claim and cost about one latency, not a second of streaming.
static const int CONN_MSG   = 1316;
static const int CONN_COUNT = 8;
static const int CONN_GAP_US = 2000;
static const int CONN_RECV_MS = 4000;

// The live-transport defaults the transfer controls start from: TSBPD on,
// latency generous enough that the clock never decides what arrives, and
// too-late packet drop off.
static void conn_live(SRTSOCKET s) {
    st_live_lossless(s);
}

// A caller socket with its connect bounded.
static SRTSOCKET conn_caller(const SockConf& cconf = st_noconf) {
    SRTSOCKET c = conn_socket();
    st_set_int(c, SRTO_CONNTIMEO, CONN_CONNTIMEO_MS);
    cconf(c);
    return c;
}

// Send CONN_COUNT messages from `from` and require every one of them back from
// `to`, byte-identical and in order. This is what makes a positive control
// non-vacuous: a library that accepts the connection and then delivers nothing
// fails here rather than scoring the point.
static void conn_require_stream(SRTSOCKET from, SRTSOCKET to, const char* what) {
    std::vector<std::string> sent = st_send_stream(from, CONN_COUNT, CONN_MSG, CONN_GAP_US);
    std::vector<std::string> got  = st_recv_stream(to, CONN_COUNT, CONN_MSG, CONN_RECV_MS);
    ST_REQUIRE_MSG((int)got.size() == CONN_COUNT,
                   "%s: sent %d messages of %d bytes and received %d", what, CONN_COUNT, CONN_MSG,
                   (int)got.size());
    int ooo = st_first_out_of_order(sent, got);
    ST_REQUIRE_MSG(ooo == -1, "%s: message %d arrived out of order or altered", what, ooo);
    for (int i = 0; i < CONN_COUNT; i++)
        ST_REQUIRE_MSG(got[(size_t)i] == sent[(size_t)i],
                       "%s: message %d differs from what was sent", what, i);
}

// ===========================================================================
// g04_argcheck -- deltas/10.md D2 and D3.
//
// interface.md: "`srt_connect` with a zero address length fails with
// `SRT_EINVPARAM`. No shipped document says what a zero `namelen` means. It
// must be rejected rather than inferred from `sa_family`. Only 0 is asserted;
// the group passes the true length as its positive control and never passes a
// null address pointer." and "A bind of the IPv6 wildcard with the flag unset
// fails with `SRT_EINVPARAM`. ... the group asserts the positive control too:
// with the flag set to either value the same bind succeeds."
//
// Both halves of each pair live in ONE case wherever they can, which is what
// makes them un-fakeable: a tree that refuses everything fails the control
// beside the refusal, and a tree that refuses nothing -- v1.5.0's -- fails the
// refusal itself.
// ===========================================================================

// D2. The refusal, with the address proven good by the same address at the
// true length.
SRT_CASE(g04_argcheck, connect_with_a_zero_address_length_is_refused) {
    Listener L;
    sockaddr_in ra = st_loopback(L.port);

    SRTSOCKET bad = conn_caller();
    Outcome o = conn_connect(bad, (sockaddr*)&ra, 0);
    ST_REQUIRE_MSG(o.rc == SRT_ERROR,
                   "srt_connect(127.0.0.1:%d, namelen=0) returned %d: a zero address length must "
                   "be refused, not inferred from sa_family",
                   (int)L.port, o.rc);
    ST_REQUIRE_MSG(o.err == SRT_EINVPARAM,
                   "srt_connect(namelen=0) failed with %d (%s); expected SRT_EINVPARAM %d",
                   o.err, conn_errname(o.err), (int)SRT_EINVPARAM);
    // A refused connect must leave the socket unconnected. At v1.5.0 the same
    // call reaches SRTS_CONNECTED, which is precisely the behaviour corrected
    // here.
    SRT_SOCKSTATUS st = srt_getsockstate(bad);
    ST_REQUIRE_MSG(st != SRTS_CONNECTED,
                   "after the refused connect the caller is in state %d (SRTS_CONNECTED); a "
                   "refused connect must not leave a connected socket",
                   (int)st);
    srt_close(bad);

    // Control, on the very same listener and the very same address: at the true
    // length the connect succeeds and the listener accepts. So the refusal
    // above is attributable to the length and to nothing else.
    SRTSOCKET good = conn_caller();
    Outcome g = conn_connect(good, (sockaddr*)&ra, (int)sizeof ra);
    ST_REQUIRE_MSG(g.rc != SRT_ERROR,
                   "control: srt_connect(127.0.0.1:%d, namelen=%d) must still succeed but failed "
                   "with %d (%s)",
                   (int)L.port, (int)sizeof ra, g.err, conn_errname(g.err));
    SRTSOCKET acc = conn_accept_within(L.lsn, CONN_ACCEPT_MS);
    ST_REQUIRE_MSG(acc != SRT_INVALID_SOCK,
                   "control: the connect at the true address length succeeded but the listener "
                   "never accepted it");
    srt_close(acc);
    srt_close(good);
}

// D2, the positive control carried all the way to delivered payload. A tree
// that refuses a zero namelen by refusing every connect passes the case above
// and fails this one.
SRT_CASE(g04_argcheck, connect_at_the_true_address_length_carries_a_stream) {
    Listener L((SockConf)conn_live);
    sockaddr_in ra = st_loopback(L.port);

    SRTSOCKET cal = conn_caller((SockConf)conn_live);
    Outcome o = conn_connect(cal, (sockaddr*)&ra, (int)sizeof ra);
    ST_REQUIRE_MSG(o.rc != SRT_ERROR, "srt_connect at the true address length failed with %d (%s)",
                   o.err, conn_errname(o.err));
    SRTSOCKET acc = conn_accept_within(L.lsn, CONN_ACCEPT_MS);
    ST_REQUIRE_MSG(acc != SRT_INVALID_SOCK, "the listener never accepted the connection");

    conn_require_stream(cal, acc, "connect at the true address length");

    srt_close(acc);
    srt_close(cal);
}

// D3. The refusal, with the very same bind proven good the moment the flag is
// given a value.
SRT_CASE(g04_argcheck, ipv6_wildcard_bind_at_the_default_flag_is_refused) {
    sockaddr_in6 wild = conn_v6_wildcard(0);

    SRTSOCKET bad = conn_socket();
    Outcome o = conn_bind(bad, (sockaddr*)&wild, (int)sizeof wild);
    ST_REQUIRE_MSG(o.rc == SRT_ERROR,
                   "srt_bind([::]:0) returned %d on a socket whose SRTO_IPV6ONLY is still at its "
                   "-1 default; the flag must be set explicitly first",
                   o.rc);
    ST_REQUIRE_MSG(o.err == SRT_EINVPARAM,
                   "srt_bind([::]:0) at the default flag failed with %d (%s); expected "
                   "SRT_EINVPARAM %d",
                   o.err, conn_errname(o.err), (int)SRT_EINVPARAM);
    srt_close(bad);

    // Control: the identical bind, on a fresh socket, with the flag set. The
    // refusal is the unset flag and not the address, the family or the host's
    // IPv6 support.
    SRTSOCKET good = conn_socket();
    st_set_int(good, SRTO_IPV6ONLY, 0);
    Outcome g = conn_bind(good, (sockaddr*)&wild, (int)sizeof wild);
    ST_REQUIRE_MSG(g.rc != SRT_ERROR,
                   "control: srt_bind([::]:0) with SRTO_IPV6ONLY=0 must succeed but failed with "
                   "%d (%s)",
                   g.err, conn_errname(g.err));
    srt_close(good);
}

// D3, the positive control at BOTH permitted values, each carried past the
// return code to a real bound endpoint.
SRT_CASE(g04_argcheck, ipv6_wildcard_bind_succeeds_at_either_flag_value) {
    for (int v = 0; v <= 1; v++) {
        SRTSOCKET s = conn_socket();
        st_set_int(s, SRTO_IPV6ONLY, v);
        sockaddr_in6 wild = conn_v6_wildcard(0);
        Outcome o = conn_bind(s, (sockaddr*)&wild, (int)sizeof wild);
        ST_REQUIRE_MSG(o.rc != SRT_ERROR,
                       "srt_bind([::]:0) with SRTO_IPV6ONLY=%d failed with %d (%s); the flag is "
                       "set explicitly, so the bind must succeed",
                       v, o.err, conn_errname(o.err));
        // "Succeeded" is not a return code here: the socket must really hold an
        // IPv6 endpoint on a real port.
        int fam = conn_family_of(s);
        ST_REQUIRE_MSG(fam == AF_INET6,
                       "SRTO_IPV6ONLY=%d: the bound socket reports family %d, expected AF_INET6 "
                       "(%d)",
                       v, fam, (int)AF_INET6);
        uint16_t p = conn_port_of(s);
        ST_REQUIRE_MSG(p != 0, "SRTO_IPV6ONLY=%d: the bound socket reports port 0", v);
        srt_close(s);
    }
}

// ===========================================================================
// g05_muxer_reuse -- deltas/10.md D9.
//
// interface.md: "Multiplexer sharing across the IPv6-only flag is asymmetric.
// A socket left at the -1 default may bind an IPv4 address and port already
// bound by a socket that set `SRTO_IPV6ONLY` to 0, and succeeds by sharing the
// multiplexer. The opposite order -- the socket with the flag at 0 binding
// second, over an existing socket at the -1 default -- is still refused with
// `SRT_EBINDCONFLICT`."
//
// The asymmetry is the whole subject, so the two orders are two cases and each
// carries the control that rules out the boring explanation: in the permissive
// order, that the port was never really occupied; in the refused order, that
// the port is simply unavailable to anyone.
// ===========================================================================

// The permissive order. At v1.5.0 the multiplexer comparison requires the two
// iIpV6Only values to be EQUAL and this bind is refused with 5015.
SRT_CASE(g05_muxer_reuse, a_default_flag_binds_over_an_explicit_zero) {
    // First in: the flag explicitly relaxed.
    SRTSOCKET first = conn_socket();
    st_set_int(first, SRTO_IPV6ONLY, 0);
    sockaddr_in a = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(first, (sockaddr*)&a, sizeof a) != SRT_ERROR,
                   "srt_bind(127.0.0.1:0) with SRTO_IPV6ONLY=0: %s", srt_getlasterror_str());
    uint16_t P = conn_port_of(first);
    ST_REQUIRE_MSG(P != 0, "the first socket bound to port 0");

    // Second in: the flag untouched, over the same endpoint.
    SRTSOCKET second = conn_socket();
    sockaddr_in b = st_loopback(P);
    Outcome o = conn_bind(second, (sockaddr*)&b, (int)sizeof b);
    ST_REQUIRE_MSG(o.rc != SRT_ERROR,
                   "srt_bind(127.0.0.1:%d) on a socket at the -1 SRTO_IPV6ONLY default failed "
                   "with %d (%s); a socket that expressed no IPv6 preference must share the "
                   "multiplexer of one that set the flag to 0",
                   (int)P, o.err, conn_errname(o.err));

    // Sharing, not a bind that quietly landed somewhere else: both sockets must
    // report the same endpoint.
    uint16_t p2 = conn_port_of(second);
    ST_REQUIRE_MSG(p2 == P,
                   "the second socket reports port %d, expected the shared port %d", (int)p2,
                   (int)P);
    uint16_t p1 = conn_port_of(first);
    ST_REQUIRE_MSG(p1 == P,
                   "the first socket now reports port %d, expected to still hold %d", (int)p1,
                   (int)P);

    srt_close(second);
    srt_close(first);
}

// The refused order, unchanged at this milestone, with the control that makes
// the refusal mean something.
SRT_CASE(g05_muxer_reuse, an_explicit_zero_over_a_default_is_refused) {
    // First in: the flag untouched.
    SRTSOCKET first = conn_socket();
    sockaddr_in a = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(first, (sockaddr*)&a, sizeof a) != SRT_ERROR,
                   "srt_bind(127.0.0.1:0) at the SRTO_IPV6ONLY default: %s",
                   srt_getlasterror_str());
    uint16_t P = conn_port_of(first);
    ST_REQUIRE_MSG(P != 0, "the first socket bound to port 0");

    // Second in: the flag explicitly relaxed. This is the direction that does
    // NOT relax.
    SRTSOCKET second = conn_socket();
    st_set_int(second, SRTO_IPV6ONLY, 0);
    sockaddr_in b = st_loopback(P);
    Outcome o = conn_bind(second, (sockaddr*)&b, (int)sizeof b);
    ST_REQUIRE_MSG(o.rc == SRT_ERROR,
                   "srt_bind(127.0.0.1:%d) with SRTO_IPV6ONLY=0 over a socket at the -1 default "
                   "returned %d; that order must still be refused",
                   (int)P, o.rc);
    ST_REQUIRE_MSG(o.err == SRT_EBINDCONFLICT,
                   "the refused bind reported %d (%s); expected SRT_EBINDCONFLICT %d", o.err,
                   conn_errname(o.err), (int)SRT_EBINDCONFLICT);
    srt_close(second);

    // Control: a third socket that leaves the flag alone -- the same value the
    // first socket has -- binds the same endpoint and succeeds. So the refusal
    // above is the flag mismatch and not the port being unavailable in
    // general.
    SRTSOCKET third = conn_socket();
    sockaddr_in c = st_loopback(P);
    Outcome t = conn_bind(third, (sockaddr*)&c, (int)sizeof c);
    ST_REQUIRE_MSG(t.rc != SRT_ERROR,
                   "control: srt_bind(127.0.0.1:%d) from a socket at the same -1 default failed "
                   "with %d (%s); equal settings have always shared the multiplexer",
                   (int)P, t.err, conn_errname(t.err));
    srt_close(third);
    srt_close(first);
}

// "Succeeds by sharing the multiplexer" is a claim about a live endpoint, not
// about a return code. The shared port must still carry a connection and its
// payload while both sockets hold it.
SRT_CASE(g05_muxer_reuse, the_shared_multiplexer_still_carries_a_connection) {
    // First in, with the flag relaxed, and listening.
    SRTSOCKET lsn = conn_socket();
    st_set_int(lsn, SRTO_RCVSYN, 0);
    st_set_int(lsn, SRTO_IPV6ONLY, 0);
    conn_live(lsn);
    sockaddr_in a = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(lsn, (sockaddr*)&a, sizeof a) != SRT_ERROR,
                   "srt_bind(127.0.0.1:0) on the listener: %s", srt_getlasterror_str());
    uint16_t P = conn_port_of(lsn);
    ST_REQUIRE_MSG(srt_listen(lsn, 5) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());

    // Second in, at the -1 default, sharing that same endpoint.
    SRTSOCKET sharer = conn_socket();
    sockaddr_in b = st_loopback(P);
    Outcome o = conn_bind(sharer, (sockaddr*)&b, (int)sizeof b);
    ST_REQUIRE_MSG(o.rc != SRT_ERROR,
                   "srt_bind(127.0.0.1:%d) at the -1 SRTO_IPV6ONLY default, over a listening "
                   "socket that set the flag to 0, failed with %d (%s)",
                   (int)P, o.err, conn_errname(o.err));

    // The shared multiplexer is still the listener's: a caller reaches it and
    // the payload crosses while both sockets hold the port.
    SRTSOCKET cal = conn_caller((SockConf)conn_live);
    sockaddr_in ra = st_loopback(P);
    Outcome c = conn_connect(cal, (sockaddr*)&ra, (int)sizeof ra);
    ST_REQUIRE_MSG(c.rc != SRT_ERROR,
                   "after the second socket shared port %d the listener stopped answering: "
                   "srt_connect failed with %d (%s)",
                   (int)P, c.err, conn_errname(c.err));
    SRTSOCKET acc = conn_accept_within(lsn, CONN_ACCEPT_MS);
    ST_REQUIRE_MSG(acc != SRT_INVALID_SOCK,
                   "the listener sharing port %d never accepted the connection", (int)P);

    conn_require_stream(cal, acc, "a connection over a shared multiplexer");

    srt_close(acc);
    srt_close(cal);
    srt_close(sharer);
    srt_close(lsn);
}

// ===========================================================================
// g06_reject_hook -- deltas/10.md D5.
//
// interface.md: "A listener hook that rejects without naming a reason yields
// `SRT_REJC_PREDEFINED` (1000). This contradicts `srt-docs/` directly: the
// access-control document states that 'in a normal situation, the rejection
// code is predefined as `SRT_REJ_RESOURCE`', which is 3. That is not what this
// milestone does. ... If the hook does call `srt_setrejectreason` first, the
// value it set is what the caller reports, and that path is unchanged."
//
// The hook runs on the library's own receiver-queue thread, so what it records
// is read through std::atomic. Each case forks into its own process, so the
// file-scope state below starts clean every time.
//
// No case here asserts elapsed time: interface.md says in as many words not to.
// ===========================================================================

// What the hook did, and what it should do. `reason` < 0 means "return -1
// without naming a reason"; >= 0 means "name this reason, then return -1";
// CONN_HOOK_ACCEPT means "let it through".
static const int CONN_HOOK_ACCEPT = -2;

static std::atomic<int> g_hook_calls(0);
static std::atomic<int> g_hook_setrc(0);

static int conn_hook(void* opaq, SRTSOCKET ns, int /*hsversion*/, const sockaddr* /*peer*/,
                     const char* /*streamid*/) {
    const int mode = *(const int*)opaq;
    g_hook_calls++;
    if (mode == CONN_HOOK_ACCEPT) return 0;
    if (mode >= 0) g_hook_setrc.store(srt_setrejectreason(ns, mode));
    return -1;
}

// A listener whose hook is installed before bind and listen.
static SockConf conn_hooked(int* mode) {
    return [mode](SRTSOCKET s) {
        ST_REQUIRE_MSG(srt_listen_callback(s, conn_hook, mode) != SRT_ERROR,
                       "srt_listen_callback: %s", srt_getlasterror_str());
    };
}

// D5. The discriminator: a hook that rejects and names nothing.
SRT_CASE(g06_reject_hook, a_hook_rejecting_without_a_reason_yields_the_predefined_code) {
    int mode = -1;  // reject, name nothing
    Listener L(conn_hooked(&mode));

    SRTSOCKET cal = conn_caller();
    sockaddr_in ra = st_loopback(L.port);
    Outcome o = conn_connect(cal, (sockaddr*)&ra, (int)sizeof ra);

    // The hook has to have run, or the reason below would be about something
    // else entirely.
    ST_REQUIRE_MSG(g_hook_calls.load() >= 1,
                   "the srt_listen_callback hook was never called (%d calls), so nothing this "
                   "case observes is about the hook",
                   g_hook_calls.load());
    ST_REQUIRE_MSG(o.rc == SRT_ERROR,
                   "srt_connect returned %d although the listener's hook returned -1; a rejected "
                   "connect must fail",
                   o.rc);

    const int reason = srt_getrejectreason(cal);
    ST_REQUIRE_MSG(reason == SRT_REJC_PREDEFINED,
                   "srt_getrejectreason on the caller reports %d; a hook that returns -1 without "
                   "calling srt_setrejectreason must leave SRT_REJC_PREDEFINED %d",
                   reason, (int)SRT_REJC_PREDEFINED);

    // And nothing was accepted: the connect has already returned, so the
    // handshake is over and this confirms the rejection rather than racing it.
    SRTSOCKET acc = conn_accept_within(L.lsn, CONN_NO_ACCEPT_MS);
    ST_REQUIRE_MSG(acc == SRT_INVALID_SOCK,
                   "the hook returned -1 but the listener accepted the connection anyway");
    srt_close(cal);
}

// The control that stops the case above from passing on a constant: when the
// hook DOES name a reason, that reason -- not 1000 -- is what the caller reads.
// This path is unchanged at this milestone and passes at both, which is exactly
// what a control is for.
SRT_CASE(g06_reject_hook, a_hook_that_names_its_own_reason_has_that_reason_reported) {
    int mode = SRT_REJC_USERDEFINED + 7;  // 2007
    Listener L(conn_hooked(&mode));

    SRTSOCKET cal = conn_caller();
    sockaddr_in ra = st_loopback(L.port);
    Outcome o = conn_connect(cal, (sockaddr*)&ra, (int)sizeof ra);

    ST_REQUIRE_MSG(g_hook_calls.load() >= 1,
                   "the srt_listen_callback hook was never called (%d calls)",
                   g_hook_calls.load());
    ST_REQUIRE_MSG(g_hook_setrc.load() != SRT_ERROR,
                   "srt_setrejectreason(%d) inside the hook returned SRT_ERROR, so the reason the "
                   "caller reads would not be the one this case set",
                   mode);
    ST_REQUIRE_MSG(o.rc == SRT_ERROR,
                   "srt_connect returned %d although the listener's hook returned -1", o.rc);

    const int reason = srt_getrejectreason(cal);
    ST_REQUIRE_MSG(reason == mode,
                   "srt_getrejectreason on the caller reports %d; a hook that called "
                   "srt_setrejectreason(%d) must have that value reported instead of the "
                   "predefined %d",
                   reason, mode, (int)SRT_REJC_PREDEFINED);
    srt_close(cal);
}

// The other control: the hook is not a way of breaking every connection. When
// it returns 0 the connection comes up, nothing is rejected, and the payload
// crosses.
SRT_CASE(g06_reject_hook, a_hook_that_accepts_lets_the_connection_and_its_payload_through) {
    int mode = CONN_HOOK_ACCEPT;
    SockConf lconf = [&mode](SRTSOCKET s) {
        conn_live(s);
        ST_REQUIRE_MSG(srt_listen_callback(s, conn_hook, &mode) != SRT_ERROR,
                       "srt_listen_callback: %s", srt_getlasterror_str());
    };
    Listener L(lconf);

    SRTSOCKET cal = conn_caller((SockConf)conn_live);
    sockaddr_in ra = st_loopback(L.port);
    Outcome o = conn_connect(cal, (sockaddr*)&ra, (int)sizeof ra);
    ST_REQUIRE_MSG(g_hook_calls.load() >= 1,
                   "the srt_listen_callback hook was never called (%d calls)",
                   g_hook_calls.load());
    ST_REQUIRE_MSG(o.rc != SRT_ERROR,
                   "the hook returned 0 but srt_connect failed with %d (%s)", o.err,
                   conn_errname(o.err));

    const int reason = srt_getrejectreason(cal);
    ST_REQUIRE_MSG(reason == SRT_REJ_UNKNOWN,
                   "the connection was accepted but srt_getrejectreason on the caller reports "
                   "%d, expected SRT_REJ_UNKNOWN %d",
                   reason, (int)SRT_REJ_UNKNOWN);

    SRTSOCKET acc = conn_accept_within(L.lsn, CONN_ACCEPT_MS);
    ST_REQUIRE_MSG(acc != SRT_INVALID_SOCK,
                   "the hook returned 0 but the listener never accepted the connection");

    conn_require_stream(cal, acc, "a connection an accepting hook let through");

    srt_close(acc);
    srt_close(cal);
}
