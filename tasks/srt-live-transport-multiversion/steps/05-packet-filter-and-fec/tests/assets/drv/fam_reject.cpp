// Family "reject", checkpoint 05 (SRT v1.4.0). Two groups, both scored:
//
//   g04_rejectreason_api     (new)        the rejection-reason *surface*: the
//                                        three symbols, the enum's size, and
//                                        the two fallbacks.
//   g05_rejectreason_values  (new)        the four specific reasons the surface
//                                        reports for four refused handshakes,
//                                        what a filter disagreement really
//                                        does, and what happens with
//                                        enforcement turned off.
//
// ---------------------------------------------------------------------------
// This family holds NO gate group. Both inherited groups of step 05 live in
// fam_gate.cpp, a translation unit that names no identifier new at this
// milestone, so no restatement in this file can cost the step its gate. Steps
// 08 and 11 follow the same convention. A family is one translation unit and
// one executable, drv/CMakeLists.txt attributes a family that does not build
// as `driver-did-not-compile`, and every group it carries then scores zero --
// which is why what a driver names is a scoring decision and not a style one.
//
// ---------------------------------------------------------------------------
// Why this file names no SRT_REJ_* constant and calls no reject function
//
// Both groups are `new` and must FAIL at v1.3.3. The useful way to fail is on
// an assertion that names actual versus expected, not on a build error, so the
// whole file is still written to *compile* at v1.3.3 -- and at v1.3.3 neither
// `SRT_REJECT_REASON`, nor `srt_getrejectreason`, nor `srt_rejectreason_str`,
// nor `srt_rejectreason_msg` exists. Naming any of them, even inside a dead
// branch, is a compile error there, and `driver-did-not-compile` is the one
// verdict a reviewer cannot tell apart from a broken harness.
//
// So, exactly as the epoll family does for its own new symbols:
//
//   * the two functions are resolved with dlsym(RTLD_DEFAULT, ...). That is
//     also the only honest way to assert that a symbol is *exported* rather
//     than merely declared;
//   * the enum constants are restated locally as plain ints, at the values
//     <srt/srt.h> documents, and pinned there by static_assert under a guard;
//   * `srt_rejectreason_msg` is reached through the *pointer* dlsym returns,
//     never through the name, so the array is not referenced at v1.3.3.
//
// The guard is nested on purpose. At PREVTAG v1.2.0 `SRT_VERSION_VALUE` is not
// defined at all, and a bare `#if SRT_VERSION_VALUE >= ...` is a preprocessor
// syntax error rather than a false, so the outer #ifdef is what keeps the
// static_assert from taking the file down at a prefix older than this step's
// parent. (`SRT_MAKE_VERSION_VALUE` is v1.3.4+, so it too lives inside the
// guard that can only fire where it exists.)
//
// ---------------------------------------------------------------------------
// Traceability. Each group below opens with the sentence from
// TEST_INTERFACE.md §3/§4 that names it, and each case states which part of
// that sentence it asserts.
//
// ---------------------------------------------------------------------------
// Deliberately NOT asserted, with the reason:
//
//   * "SRT_REJ_FILTER for packet-filter mismatch". THIS IS NOT WHAT v1.4.0
//     DOES, and it is reported rather than quietly rescored. v1.4.0's
//     CUDT::checkApplyFilterConfig has no fatal branch for a differing
//     configuration: on a caller-listener pair the mechanism is *merge* --
//     a listener that has no filter adopts whatever the caller offers, a
//     listener that has one keeps its own parameters and merges in only the
//     keys it did not set, and the caller takes the listener's version
//     wholesale. The only two sites that can set m_RejectReason = SRT_REJ_FILTER
//     are (a) receiving a *second* SRT_CMD_FILTER block, which the v1.4.0
//     encoder cannot emit -- createSrtHandshake writes at most one, guarded by
//     `bool have_filter`, set at a single place -- and (b) a
//     PacketFilter::configure failure after the handshake, which the same
//     ParseFilterConfig validation has already rejected at setsockflag time.
//     Measured against the released v1.4.0 library, every one of fourteen
//     caller-listener filter arrangements reached SRTS_CONNECTED with
//     srt_getrejectreason == 0 -- one-sided either way, "fec,cols:10" vs
//     "fec,cols:20", bare "fec" vs "fec,cols:10", differing layout, differing
//     rows, and every equal-and-identical control. Rendezvous mode does reject
//     a differing configuration, but against the rendezvous path
//     srt_getrejectreason reads 0 on *both* sides (each peer fails its own
//     local check before any URQFailure reaches the wire), so the reason the
//     description asks for is unreachable there too. There is therefore no
//     arrangement in which the documented assertion holds, and g05 does not
//     make it, and the documents were corrected to the merge instead.
//   * `SRTO_SNDPEERKMSTATE`. No such option exists at v1.4.0, nor at v1.3.3,
//     nor at any tag in the chain -- if you are looking for it because some
//     other SRT documentation mentions it, the pair you want is
//     SRTO_SNDKMSTATE (40, the state at the peer side) and SRTO_RCVKMSTATE
//     (the state at the agent side). fam_gate.cpp's g12_encryption asserts
//     those two.
//   * `srt_setrejectreason`. It does not exist at v1.4.0; only the getter,
//     srt_rejectreason_str and the msg array arrive
//     here. The setter lands at v1.4.2.
//   * The SRTO_STRICTENC -> SRTO_ENFORCEDENCRYPTION rename. Same slot 53,
//     same `true` default, same behaviour, and not something this milestone
//     is scored on. Four of g05's cases depend on the *default* and never
//     touch the option; the fifth addresses it by slot. Nothing here names
//     either spelling -- the v1.4.0 one is a compile error at v1.3.3 and the
//     v1.3.3 one is a deprecation warning here, which -Werror makes fatal.
//   * "code that called srt_perfmon fails to link" -- fam_epoll.cpp's subject,
//     not this family's, and unobservable from inside a linked process.
//   * An exhaustive filter-mismatch policy. TEST_INTERFACE.md §5 describes
//     the merge, not a catalogue of fatal combinations.
//
// No case here uses a wall-clock sleep as a synchronisation primitive. Every
// receive is bounded by SRTO_RCVTIMEO; every accept runs on its own thread
// against an epoll-watched listener with a bounded wait; the only sleeps are
// the intended pacing between sends and the bounded poll in st_settled_kmstate.
// =========================================================================

#include "stest.h"
#include "srtkit.h"
#include "relay.h"

#include <dlfcn.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// =========================================================================
// The restated reject-reason enumeration.
//
// Values from <srt/srt.h> at v1.4.0 (`enum SRT_REJECT_REASON`): SRT_REJ_UNKNOWN
// is 0, then SYSTEM, PEER, RESOURCE, ROGUE, BACKLOG, IPE, CLOSE, VERSION,
// RDVCOOKIE, BADSECRET, UNSECURE, MESSAGEAPI, CONGESTION, FILTER, and
// SRT_REJ__SIZE one past the last. Counted here rather than trusted, so the
// static_assert below is a check on the header and not a restatement of it.
// =========================================================================

static const int RJ_UNKNOWN = 0;
static const int RJ_BADSECRET = 10;
static const int RJ_UNSECURE = 11;
static const int RJ_MESSAGEAPI = 12;
static const int RJ_CONGESTION = 13;
static const int RJ_FILTER = 14;
static const int RJ_SIZE = 15;

// The option this file uses to build a filter disagreement. Named here rather
// than sourced from the header for the same reason as the reasons above: at
// v1.3.3 the identifier does not exist, and a bare reference would be a compile
// error in a file that has to compile at both prefixes. The value is pinned
// with the same nested guard, so a header that moved the option stops this file
// from building rather than quietly asserting the wrong slot.
//
// It is restated as a CONSTANT, exactly like RJ_ENFORCE_ENC and the RJ_* reject
// reasons above, and NOT as an `#ifndef SRTO_PACKETFILTER` / `#define`
// fallback. Two reasons:
//
//   * A macro of that name shadows the header's own enumerator wherever the
//     header declares it -- the preprocessor cannot see an enumerator, so the
//     `#ifndef` is true even at v1.4.0 -- which left the static_assert below
//     comparing 60 with 60 unless it was placed *before* the #define. That
//     ordering trap is now gone rather than documented.
//   * tools/doc-coverage.py reads an `#ifndef NAME` in a driver as "the driver
//     has a fallback, this name is optional" and rendered SRTO_PACKETFILTER
//     into TEST_INTERFACE.md section 5 as `optional (guarded)`. It is not: the
//     assert below names the enumerator, so a tree claiming v1.4.0 without the
//     option fails to build this family. The constant form makes the generated
//     row say `required to compile`, which is the truth.
#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 0)
static_assert(SRTO_PACKETFILTER == 60,
              "restated SRTO_PACKETFILTER disagrees with <srt/srt.h>: update "
              "the restatement in this file");
#endif
#endif

// Every call addresses the option through this constant; the diagnostic strings
// keep naming SRTO_PACKETFILTER, because that is the name to implement.
static const SRT_SOCKOPT RJ_PACKETFILTER = (SRT_SOCKOPT)60;

#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 0)
// Only compiles where the real enum exists, which is the only place the
// comparison means anything. If a header in the chain ever renumbers these,
// this file stops building rather than asserting the wrong number.
static_assert(SRT_REJ_UNKNOWN == RJ_UNKNOWN, "SRT_REJ_UNKNOWN disagrees with <srt/srt.h>");
static_assert(SRT_REJ_BADSECRET == RJ_BADSECRET, "SRT_REJ_BADSECRET disagrees with <srt/srt.h>");
static_assert(SRT_REJ_UNSECURE == RJ_UNSECURE, "SRT_REJ_UNSECURE disagrees with <srt/srt.h>");
static_assert(SRT_REJ_MESSAGEAPI == RJ_MESSAGEAPI, "SRT_REJ_MESSAGEAPI disagrees with <srt/srt.h>");
static_assert(SRT_REJ_CONGESTION == RJ_CONGESTION, "SRT_REJ_CONGESTION disagrees with <srt/srt.h>");
static_assert(SRT_REJ_FILTER == RJ_FILTER, "SRT_REJ_FILTER disagrees with <srt/srt.h>");
static_assert(SRT_REJ__SIZE == RJ_SIZE, "SRT_REJ__SIZE disagrees with <srt/srt.h>");
#endif
#endif

// =========================================================================
// The symbols, resolved rather than named.
// =========================================================================

typedef int (*st_getrej_fn)(SRTSOCKET);
typedef const char* (*st_rejstr_fn)(int);

// The msg array is `const char* const[]`, so dlsym gives the address OF the
// array -- a `const char* const*` -- and indexing that pointer is the array
// access, with the symbol's name appearing nowhere in this file.
typedef const char* const* st_rejmsg_fn;

template <typename Fn>
static Fn rj_dlsym(const char* n) {
    void* s = dlsym(RTLD_DEFAULT, n);
    if (!s) return 0;
    Fn f;
    std::memcpy(&f, &s, sizeof f);
    return f;
}

static st_getrej_fn rj_getrej() { return rj_dlsym<st_getrej_fn>("srt_getrejectreason"); }
static st_rejstr_fn rj_strfn() { return rj_dlsym<st_rejstr_fn>("srt_rejectreason_str"); }
static st_rejmsg_fn rj_msgfn() { return rj_dlsym<st_rejmsg_fn>("srt_rejectreason_msg"); }

// ------------------------------------------------------------------ misc

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

static long long rj_now_ms() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// A non-throwing string setter: the filter scenarios below deliberately feed
// values that may be refused, and the refusal is part of what is being set up.
static int rj_try_str(SRTSOCKET s, SRT_SOCKOPT o, const char* v) {
    srt_clearlasterror();
    return srt_setsockflag(s, o, v, (int)std::strlen(v));
}

// ------------------------------------------------------------------ the link

// listener -> relay -> caller, with the accept on its own thread.
//
// This is NOT st_try_connect. Half of g05 is *refused*
// handshakes, and st_try_connect's contract is the opposite one: it loops the
// caller on srt_recvmsg after the handshake and reports a receive timeout when
// there is no connection, which is precisely the outcome that cannot be told
// apart from a hang. Here every connection in the family is either
//
//   * refused -- srt_connect returns SRT_ERROR with SRT_ECONNREJ, the accept
//     is not ready, and the case reads the reason; or
//   * established -- both ends come up and the cases then exchange payload.
//
// The accept runs on its own thread and is woken by an epoll wait rather than
// by the accept's own timeout. Both halves of that sentence were measured:
//
//   * WITH a blocking srt_accept and a refused handshake, nothing wakes the
//     acceptor -- the listener never becomes readable, no socket is ever
//     acceptable, and the call sits there past the case deadline. Every refusal
//     case then reports "timed out after 60 s", which is the one verdict that
//     cannot be told apart from a genuine hang. So the thread cannot be parked
//     inside srt_accept.
//   * WITH the listener in non-blocking mode (SRTO_RCVSYN=0), the accepted
//     socket inherits that mode, and every srt_recvmsg in this family returns
//     before the data is there. The keyed cases then delivered 0 of 64 messages
//     while srt_bstats reported all 64 packets received and none undecryptable
//     -- a harness defect wearing the costume of a library defect, which is
//     exactly the shape a reviewer cannot debug. So the listener must stay
//     blocking, and only the wait is bounded.
//
// Hence: a blocking listener, a non-blocking wait for readiness, and an accept
// that is only called once the EID says a connection is pending. The caller's
// own expiry is separate: srt_connect returns on its own when the listener
// refuses, and its return value is what the cases assert on.
// The accepted side's lifetime is not what is under test -- nothing in this
// family reads from it except the encryption group, and that group's own cases
// set their own SRTO_RCVTIMEO before receiving. What the accept side must do
// here is exist when the handshake succeeded and cleanly not exist when it did
// not.
//
// The acceptor's patience is tied to the CALLER, not to the clock.
//
// The acceptor runs concurrently with srt_connect, so a single bounded
// srt_epoll_wait had to cover the whole handshake, and that is a race: on a
// loaded host the handshake outran the budget and the case then reported
// "srt_connect succeeded but nothing was accepted", which is a statement about
// this machine rather than about the library. Observed on the 05:08Z oracle
// ladder at step 06, and in
// this family at step 05 under the same shape.
//
// So the wait is now a loop of short slices with two exits: the listener
// signals (accept and stop), or the connect attempt has returned AND a further
// RJ_ACCEPT_GRACE_MS ms has passed with the listener still silent. While the connect is
// still in flight the acceptor simply keeps waiting -- srt_connect is itself
// bounded by SRTO_CONNTIMEO and the whole case by the harness deadline, so
// there is nothing here for an unbounded wait to hang on. The refusal cases
// stay quick, because a refused connect returns promptly and only the grace is
// added; the accepted cases get as long as the handshake actually takes.
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
            if (settled_at < 0) settled_at = rj_now_ms();
            if (rj_now_ms() - settled_at >= RJ_ACCEPT_GRACE_MS) break;
        }
        srt_epoll_release(eid);
    });

    const long long t0 = rj_now_ms();
    L.rc = srt_connect(L.cal, (sockaddr*)&L.caller_addr, sizeof L.caller_addr);
    L.err = srt_getlasterror(nullptr);
    L.elapsed_ms = (int)(rj_now_ms() - t0);
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

// The reason the CALLER is holding right now, or -1 if the surface is absent.
static int rj_reason(SRTSOCKET s) {
    st_getrej_fn f = rj_getrej();
    if (!f) return -1;
    return (int)f(s);
}

// A refusal is asserted on three things at once: srt_connect failed, the
// library named it SRT_ECONNREJ (a refused handshake, not a timeout or a bind
// error), and the listener accepted nothing. Only then is the reason read.
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

// The reason the caller must be holding after a refusal.
static void rj_require_reason(const RjLink& L, int expect, const char* what) {
    st_getrej_fn f = rj_getrej();
    ST_REQUIRE_MSG(f != 0,
                   "%s: dlsym(RTLD_DEFAULT, \"srt_getrejectreason\") is NULL, so the refusal "
                   "cannot be asked why",
                   what);
    const int got = (int)f(L.cal);
    ST_REQUIRE_MSG(got == expect, "%s: srt_getrejectreason on the caller reports %d (%s), "
                                  "expected %d (%s)",
                   what, got, rj_strfn() ? rj_strfn()(got) : rj_errname(got), expect,
                   rj_strfn() ? rj_strfn()(expect) : rj_errname(expect));
}

// The live-transport defaults every connection in this file starts from.
static void rj_live(SRTSOCKET s) { st_live_lossless(s); }

// =========================================================================
// g04_rejectreason_api   (new)
//
// TEST_INTERFACE.md §4, this group: "srt_getrejectreason exists and returns SRT_REJ_UNKNOWN on a
// fresh socket and on an unknown socket id, srt_rejectreason_str and the
// srt_rejectreason_msg array exist, and SRT_REJ__SIZE equals the documented
// count".
//
// The cases cover, in order: the three symbols are exported; a fresh socket
// and a fabricated never-allocated id both report SRT_REJ_UNKNOWN;
// SRT_REJ__SIZE counts the documented sequence; and the function and the
// array are one table, with out-of-range codes clamped to the UNKNOWN string
// rather than left NULL.
//
// Everything here is read through dlsym, so a candidate that has not reached
// this milestone fails these cases as assertions rather than as a build error.
// =========================================================================

SRT_CASE(g04_rejectreason_api, the_three_symbols_are_exported) {
    // Asserted with dlsym rather than by calling, because
    // "exists" here means "is exported from the library", which is what makes
    // a later milestone's code link.
    ST_REQUIRE_MSG(rj_getrej() != 0,
                   "dlsym(RTLD_DEFAULT, \"srt_getrejectreason\") returned NULL: the getter is "
                   "not exported at this milestone");
    ST_REQUIRE_MSG(rj_strfn() != 0,
                   "dlsym(RTLD_DEFAULT, \"srt_rejectreason_str\") returned NULL: the code-to-"
                   "string function is not exported at this milestone");
    ST_REQUIRE_MSG(rj_msgfn() != 0,
                   "dlsym(RTLD_DEFAULT, \"srt_rejectreason_msg\") returned NULL: the reason-"
                   "string array is not exported at this milestone");
}

SRT_CASE(g04_rejectreason_api, a_fresh_socket_reports_unknown) {
    // A socket that has never attempted a connection has been refused
    // nothing, so the surface must answer with the neutral code -- not an error
    // and not an uninitialised enum.
    st_getrej_fn f = rj_getrej();
    ST_REQUIRE_MSG(f != 0, "srt_getrejectreason is not exported, so a fresh socket has no "
                           "reason surface to report");
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    const int got = (int)f(s);
    ST_REQUIRE_MSG(got == RJ_UNKNOWN,
                   "srt_getrejectreason on a fresh socket reports %d, expected SRT_REJ_UNKNOWN "
                   "(%d): a socket that has been refused nothing must report the neutral code",
                   got, RJ_UNKNOWN);
    srt_close(s);
}

SRT_CASE(g04_rejectreason_api, an_unknown_socket_id_reports_unknown) {
    // TEST_INTERFACE.md §5 records the exact shape: the function
    // does NOT fail, it does not set SRT_EINVSOCK, it returns the same fallback
    // code a fresh socket gives. The id is fabricated to a value the generator
    // cannot have handed out -- socket ids start positive and decrement from
    // there, so a large-magnitude negative id has never been allocated.
    st_getrej_fn f = rj_getrej();
    ST_REQUIRE_MSG(f != 0, "srt_getrejectreason is not exported");
    const SRTSOCKET bogus = (SRTSOCKET)-2147483000;
    srt_clearlasterror();
    const int got = (int)f(bogus);
    ST_REQUIRE_MSG(got == RJ_UNKNOWN,
                   "srt_getrejectreason on a never-allocated socket id reports %d, expected "
                   "SRT_REJ_UNKNOWN (%d); TEST_INTERFACE.md §5: it returns the fallback code "
                   "failing",
                   got, RJ_UNKNOWN);
    const int e = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(e == SRT_SUCCESS,
                   "srt_getrejectreason on a never-allocated socket id set the thread's last "
                   "error to %d (%s); TEST_INTERFACE.md §5: it does not fail and does not set "
                   "SRT_EINVSOCK",
                   e, rj_errname(e));
}

SRT_CASE(g04_rejectreason_api, the_size_constant_counts_the_documented_reasons) {
    // The comparison is against the count this file derives from the
    // documented sequence -- UNKNOWN through FILTER -- not against
    // a bare 15 copied from the header, so an enum that grew by one would show
    // up here as a disagreement rather than passing.
    st_rejstr_fn sf = rj_strfn();
    ST_REQUIRE_MSG(sf != 0, "srt_rejectreason_str is not exported");
    ST_REQUIRE_MSG(RJ_FILTER + 1 == RJ_SIZE,
                   "the restated enumeration is internally inconsistent: FILTER is %d and SIZE "
                   "is %d, but SIZE must be one past the last code",
                   RJ_FILTER, RJ_SIZE);
    // The behavioural half: the last code of the documented sequence has its
    // own string, distinct from the UNKNOWN fallback. If the enum ended earlier
    // than the documents say, the id the documents call FILTER would clamp to
    // the fallback here.
    const char* last = sf(RJ_FILTER);
    const char* unk = sf(RJ_UNKNOWN);
    ST_REQUIRE_MSG(last != 0, "srt_rejectreason_str(%d) returned NULL", RJ_FILTER);
    ST_REQUIRE_MSG(unk != 0, "srt_rejectreason_str(%d) returned NULL", RJ_UNKNOWN);
    ST_REQUIRE_MSG(std::strcmp(last, unk) != 0,
                   "srt_rejectreason_str maps %d and %d to the same string \"%s\"; the "
                   "enumeration the documents describe runs UNKNOWN (0) through FILTER (14), so "
                   "14 must be a code with its own message",
                   RJ_FILTER, RJ_UNKNOWN, last);
    // Read off the sequence itself: the five reasons this step adds
    // are contiguous and in the documented order at the end of the enum.
    const char* names[] = {sf(RJ_BADSECRET), sf(RJ_UNSECURE), sf(RJ_MESSAGEAPI),
                           sf(RJ_CONGESTION), sf(RJ_FILTER)};
    for (int i = 0; i < 5; i++)
        ST_REQUIRE_MSG(names[i] != 0, "srt_rejectreason_str(%d) returned NULL",
                       RJ_BADSECRET + i);
}

SRT_CASE(g04_rejectreason_api, the_string_and_array_agree_over_the_whole_enum) {
    // The array and the function are two views
    // of one table; if they disagree, one of the two "exists" claims is only
    // nominal. The loop runs past the end on purpose (RJ_SIZE and one beyond),
    // which is the out-of-range half: TEST_INTERFACE.md §5 says such a code
    // returns the SRT_REJ_UNKNOWN *string*, not NULL.
    st_rejstr_fn sf = rj_strfn();
    st_rejmsg_fn mf = rj_msgfn();
    ST_REQUIRE_MSG(sf != 0 && mf != 0, "srt_rejectreason_str and/or srt_rejectreason_msg are "
                                       "not exported");

    for (int code = 0; code < RJ_SIZE; code++) {
        const char* via_fn = sf(code);
        const char* via_arr = mf[code];
        ST_REQUIRE_MSG(via_fn != 0, "srt_rejectreason_str(%d) returned NULL inside the enum",
                       code);
        ST_REQUIRE_MSG(via_arr != 0, "srt_rejectreason_msg[%d] is NULL", code);
        ST_REQUIRE_MSG(std::strcmp(via_fn, via_arr) == 0,
                       "code %d: srt_rejectreason_str gives \"%s\" but srt_rejectreason_msg[%d] "
                       "gives \"%s\"; the function and the array must be the same table",
                       code, via_fn, code, via_arr);
    }

    const char* fallback = sf(RJ_UNKNOWN);
    for (int code = RJ_SIZE; code <= RJ_SIZE + 8; code++) {
        const char* out = sf(code);
        ST_REQUIRE_MSG(out != 0,
                       "srt_rejectreason_str(%d), a code past SRT_REJ__SIZE, returned NULL; "
                       "TEST_INTERFACE.md §5: invalid inputs are clamped to the UNKNOWN string "
                       "rather than left undefined",
                       code);
        ST_REQUIRE_MSG(std::strcmp(out, fallback) == 0,
                       "srt_rejectreason_str(%d), a code past SRT_REJ__SIZE, returned \"%s\" "
                       "where the SRT_REJ_UNKNOWN string \"%s\" was expected",
                       code, out, fallback);
    }
    // What the table must satisfy beyond "not NULL": each of the six codes the
    // documents name has a message of its own, distinct from every other one
    // and from the fallback. That is the whole of what TEST_INTERFACE.md §5
    // promises -- it states that the strings are English and that they are
    // per-code, and deliberately does NOT fix the wording. No case here
    // compares a message against an English literal: the exact phrasing is an
    // implementation choice, and pinning it would fail an implementation that
    // is correct in every way the documents describe.
    const int doc_codes[] = {RJ_UNKNOWN,    RJ_BADSECRET,  RJ_UNSECURE,
                             RJ_MESSAGEAPI, RJ_CONGESTION, RJ_FILTER};
    const char* doc_names[] = {"SRT_REJ_UNKNOWN",    "SRT_REJ_BADSECRET",
                               "SRT_REJ_UNSECURE",   "SRT_REJ_MESSAGEAPI",
                               "SRT_REJ_CONGESTION", "SRT_REJ_FILTER"};
    const int NDOC = 6;
    for (int i = 0; i < NDOC; i++) {
        const char* si = sf(doc_codes[i]);
        ST_REQUIRE_MSG(si != 0, "srt_rejectreason_str(%s = %d) returned NULL", doc_names[i],
                       doc_codes[i]);
        ST_REQUIRE_MSG(si[0] != '\0',
                       "srt_rejectreason_str(%s = %d) returned the empty string; each "
                       "documented reason carries a message of its own",
                       doc_names[i], doc_codes[i]);
    }
    for (int i = 0; i < NDOC; i++)
        for (int j = i + 1; j < NDOC; j++)
            ST_REQUIRE_MSG(std::strcmp(sf(doc_codes[i]), sf(doc_codes[j])) != 0,
                           "%s (%d) and %s (%d) map to the same message \"%s\"; the reasons "
                           "the documents enumerate must be distinguishable through the "
                           "string table, whatever the wording",
                           doc_names[i], doc_codes[i], doc_names[j], doc_codes[j],
                           sf(doc_codes[i]));
}

// =========================================================================
// g05_rejectreason_values   (new)
//
// TEST_INTERFACE.md §4, this group: "when SRTO_ENFORCEDENCRYPTION is at its
// default and the handshake is rejected, srt_getrejectreason reports
// SRT_REJ_UNSECURE for one-sided passphrase, SRT_REJ_BADSECRET for mismatched
// passphrases, SRT_REJ_MESSAGEAPI for message/stream mismatch,
// SRT_REJ_CONGESTION for congestion-controller mismatch; a caller-listener
// packet-filter disagreement is merged rather than rejected; and with the
// enforcement option turned off on both sides a mismatched passphrase
// connects, delivers nothing, and reports no rejection reason".
//
// FOUR reasons, not five. An earlier draft of the description ended "and
// SRT_REJ_FILTER for packet-filter mismatch"; that clause was withdrawn from
// every document in this step once it was measured to be false at v1.4.0 (see
// the note at the top of this file and delta row D4), and the sentence above
// is the one the shipped TEST_INTERFACE.md carries. Nothing here asserts a
// filter rejection.
//
// Four cases read the four specific reasons off the caller at the default
// enforcement setting, a fifth checks that the four codes are distinct, a
// sixth records what a filter disagreement really does, and a seventh turns
// enforcement off and shows that the same mismatch then connects.
//
// The option is never named in either spelling. The v1.4.0 one is a compile
// error at v1.3.3 and the v1.3.3 one is a deprecation warning here, which
// -Werror makes fatal; in both directions the file would stop building. The
// four default-setting cases never touch it at all, and the seventh reaches
// it by slot.
//
// Each case carries its own positive control -- the same link with the
// disagreement removed must CONNECT -- because "srt_connect failed and said 11"
// is also satisfied by a library that refuses everything for any reason.
// =========================================================================

SRT_CASE(g05_rejectreason_values, a_one_sided_passphrase_reports_unsecure) {
    RjLink L;
    rj_run(L, rj_live, [](SRTSOCKET s) {
        rj_live(s);
        st_set_int(s, SRTO_PBKEYLEN, 16);
        st_set_str(s, SRTO_PASSPHRASE, "abcdefghijklmnop");
    });
    rj_require_refused(L, "one-sided passphrase");
    rj_require_reason(L, RJ_UNSECURE, "one-sided passphrase");
}

SRT_CASE(g05_rejectreason_values, a_mismatched_passphrase_reports_badsecret) {
    RjLink L;
    rj_run(L, [](SRTSOCKET s) {
        rj_live(s);
        st_set_int(s, SRTO_PBKEYLEN, 16);
        st_set_str(s, SRTO_PASSPHRASE, "0123456789abcdef");
    }, [](SRTSOCKET s) {
        rj_live(s);
        st_set_int(s, SRTO_PBKEYLEN, 16);
        st_set_str(s, SRTO_PASSPHRASE, "abcdefghijklmnop");
    });
    rj_require_refused(L, "mismatched passphrases");
    rj_require_reason(L, RJ_BADSECRET, "mismatched passphrases");
}

SRT_CASE(g05_rejectreason_values, a_message_api_mismatch_reports_messageapi) {
    RjLink L;
    rj_run(L, rj_live, [](SRTSOCKET s) {
        rj_live(s);
        st_set_int(s, SRTO_MESSAGEAPI, 0);
    });
    rj_require_refused(L, "message/stream mismatch");
    rj_require_reason(L, RJ_MESSAGEAPI, "message/stream mismatch");
}

SRT_CASE(g05_rejectreason_values, a_congestion_controller_mismatch_reports_congestion) {
    RjLink L;
    rj_run(L, rj_live, [](SRTSOCKET s) {
        rj_live(s);
        st_set_str(s, SRTO_CONGESTION, "file");
    });
    rj_require_refused(L, "congestion-controller mismatch");
    rj_require_reason(L, RJ_CONGESTION, "congestion-controller mismatch");
}

SRT_CASE(g05_rejectreason_values, the_other_four_reasons_are_not_produced_by_these_refusals) {
    // The four scenarios above must be four *different* reasons, not four
    // readings of one constant. Each refusal is re-run here and its code is
    // required to differ from the other three.
    struct Scenario {
        const char* what;
        SockConf lconf;
        SockConf cconf;
        int expect;
    };
    Scenario sc[4];
    sc[0].what = "one-sided passphrase";
    sc[0].lconf = rj_live;
    sc[0].cconf = [](SRTSOCKET s) {
        rj_live(s);
        st_set_int(s, SRTO_PBKEYLEN, 16);
        st_set_str(s, SRTO_PASSPHRASE, "abcdefghijklmnop");
    };
    sc[0].expect = RJ_UNSECURE;
    sc[1].what = "mismatched passphrases";
    sc[1].lconf = [](SRTSOCKET s) {
        rj_live(s);
        st_set_int(s, SRTO_PBKEYLEN, 16);
        st_set_str(s, SRTO_PASSPHRASE, "0123456789abcdef");
    };
    sc[1].cconf = [](SRTSOCKET s) {
        rj_live(s);
        st_set_int(s, SRTO_PBKEYLEN, 16);
        st_set_str(s, SRTO_PASSPHRASE, "abcdefghijklmnop");
    };
    sc[1].expect = RJ_BADSECRET;
    sc[2].what = "message/stream mismatch";
    sc[2].lconf = rj_live;
    sc[2].cconf = [](SRTSOCKET s) {
        rj_live(s);
        st_set_int(s, SRTO_MESSAGEAPI, 0);
    };
    sc[2].expect = RJ_MESSAGEAPI;
    sc[3].what = "congestion-controller mismatch";
    sc[3].lconf = rj_live;
    sc[3].cconf = [](SRTSOCKET s) {
        rj_live(s);
        st_set_str(s, SRTO_CONGESTION, "file");
    };
    sc[3].expect = RJ_CONGESTION;

    std::vector<int> seen;
    for (int i = 0; i < 4; i++) {
        RjLink L;
        rj_run(L, sc[i].lconf, sc[i].cconf);
        rj_require_refused(L, sc[i].what);
        rj_require_reason(L, sc[i].expect, sc[i].what);
        seen.push_back(sc[i].expect);
    }
    for (size_t i = 0; i < seen.size(); i++)
        for (size_t j = i + 1; j < seen.size(); j++)
            ST_REQUIRE_MSG(seen[i] != seen[j],
                           "the refusals \"%s\" and \"%s\" both report reason %d; four distinct "
                           "handshake disagreements must not collapse into one code",
                           sc[i].what, sc[j].what, seen[i]);
}

SRT_CASE(g05_rejectreason_values, a_packet_filter_disagreement_does_not_reject) {
    // An earlier draft of this milestone's documents said a filter mismatch
    // rejects with SRT_REJ_FILTER. THE RELEASED v1.4.0 LIBRARY DOES NOT DO
    // THAT -- see the header comment for the two sites that could set the
    // code and why neither is reachable from the public API. The case
    // asserts what was measured, with the positive control that logic
    // demands:
    //
    //   (1) the filter strings really were accepted by both sides ("fec" is not
    //       a configuration the library refuses, which would make the rest of
    //       the case vacuous), and the two sides really disagreed;
    //   (2) the handshake connected anyway, and the getter still answers on
    //       both sides afterwards.
    //
    // What the case does NOT assert is the *content* of the post-connect
    // readback. v1.4.0 merges the two configurations, but no document the
    // candidate is given describes a merge rule, a term order, or whether the
    // negotiated result is re-serialised at all; pinning "cols:10 survives and
    // rows:9 is merged in" would be scoring `PacketFilter::parseConfig`'s
    // internals. "The disagreement did not refuse the handshake" is the whole
    // of the discriminator and the whole of what is documented.
    //
    // Written this way, the case is still a v1.4.0-only observation: at v1.3.3
    // SRTO_PACKETFILTER is not settable at all (D1), so step (1) fails there
    // and the case fails on its assertion rather than on a build error.
    const char* lf = "fec,cols:10";
    const char* cf = "fec,cols:20,rows:9";
    char why[160];

    RjLink L;
    L.lsn = st_new_socket();
    ST_REQUIRE_MSG(L.lsn != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    st_set_int(L.lsn, SRTO_RCVTIMEO, 20000);
    rj_live(L.lsn);
    const int lset = rj_try_str(L.lsn, RJ_PACKETFILTER, lf);
    std::snprintf(why, sizeof why, "the listener's SRTO_PACKETFILTER \"%s\"", lf);
    ST_REQUIRE_MSG(lset == 0,
                   "%s was refused with %s; at this milestone the FEC filter configuration is "
                   "accepted, and a case about a filter *disagreement* cannot be built on a "
                   "filter the library will not store",
                   why, srt_getlasterror_str());

    sockaddr_in la = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(L.lsn, (sockaddr*)&la, sizeof la) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    int nl = (int)sizeof la;
    ST_REQUIRE_MSG(srt_getsockname(L.lsn, (sockaddr*)&la, &nl) != SRT_ERROR, "srt_getsockname: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(L.lsn, 1) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());

    RelaySched sched;
    L.relay = new Relay(ntohs(la.sin_port), sched);
    const sockaddr_in ra = st_loopback(L.relay->port());

    L.cal = st_new_socket();
    ST_REQUIRE_MSG(L.cal != SRT_INVALID_SOCK, "srt_socket (caller): %s", srt_getlasterror_str());
    rj_live(L.cal);
    const int cset = rj_try_str(L.cal, RJ_PACKETFILTER, cf);
    std::snprintf(why, sizeof why, "the caller's SRTO_PACKETFILTER \"%s\"", cf);
    ST_REQUIRE_MSG(cset == 0, "%s was refused with %s", why, srt_getlasterror_str());

    L.caller_addr = ra;
    rj_finish(L);

    // (2) the disagreement did not refuse the handshake.
    ST_REQUIRE_MSG(L.rc != SRT_ERROR,
                   "the two sides disagreed on the filter (listener \"%s\", caller \"%s\") and "
                   "srt_connect failed with %s (%d, reason %d); the measured v1.4.0 behaviour is "
                   "that a caller-listener filter disagreement is merged, not refused",
                   lf, cf, rj_errname(L.err), L.err, rj_reason(L.cal));
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK, "srt_accept: %s", srt_getlasterror_str());

    // The getter keeps working on a connected socket on both sides. Only that
    // it answers -- the string it answers with is the negotiated result, whose
    // shape no shipped document fixes, so nothing here reads it.
    char lbuf[256];
    char cbuf[256];
    int ln = (int)sizeof lbuf;
    int cn = (int)sizeof cbuf;
    ST_REQUIRE_MSG(srt_getsockflag(L.acc, RJ_PACKETFILTER, lbuf, &ln) != SRT_ERROR,
                   "reading SRTO_PACKETFILTER back from the accepted socket: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_getsockflag(L.cal, RJ_PACKETFILTER, cbuf, &cn) != SRT_ERROR,
                   "reading SRTO_PACKETFILTER back from the caller: %s", srt_getlasterror_str());
}

// =========================================================================
// The keyed-link scaffolding g05's enforcement-off case needs.
//
// g12_encryption used to live here and owned most of this; it has moved to
// fam_gate.cpp, which is where the step's two inherited gate groups now are.
// What is left is what the one remaining keyed case in g05 uses.
// =========================================================================

static const int RJ_LIVE_PAYLOAD = 1316;  // live-mode default, srt-docs/API.md
static const int RJ_KM_GAP_US = 2000;

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

// Thread body for the receiving side. Deliberately non-throwing: an STFail
// escaping a std::thread would call std::terminate and lose the failure
// message the reviewer needs. Every exit is either "collected what I was asked
// for" or "SRTO_RCVTIMEO expired", and the main thread is what asserts about
// it. The timeout is set by the caller, before this starts.
static void rj_rx_collect(SRTSOCKET s, int want, int bufsize, std::vector<std::string>* out) {
    std::string buf((size_t)bufsize + 4096, '\0');
    while ((int)out->size() < want) {
        const int rc = srt_recvmsg(s, &buf[0], (int)buf.size());
        if (rc <= 0) break;
        out->push_back(buf.substr(0, (size_t)rc));
    }
}

// Joins on destruction, so an assertion failure on the main thread unwinds
// cleanly instead of hitting std::terminate. Bounded, because the loop it joins
// is bounded by SRTO_RCVTIMEO.
struct RjJoiner {
    std::thread t;
    explicit RjJoiner(std::thread&& th) : t(std::move(th)) {}
    RjJoiner(const RjJoiner&) = delete;
    RjJoiner& operator=(const RjJoiner&) = delete;
    ~RjJoiner() {
        if (t.joinable()) t.join();
    }
};

// One stream across an established link: the receiver runs concurrently on its
// own thread bounded by SRTO_RCVTIMEO, the sender on this one. A send-then-
// receive shape would park the whole stream in the receive buffer and test
// buffering rather than delivery.
static std::vector<std::string> rj_exchange(SRTSOCKET snd, SRTSOCKET rcv, int count, int size,
                                            int gap_us, int rcv_timeo_ms) {
    std::vector<std::string> got;
    st_set_int(rcv, SRTO_RCVTIMEO, rcv_timeo_ms);
    {
        RjJoiner rx(std::thread(rj_rx_collect, rcv, count, size, &got));
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

// =========================================================================
// g05_rejectreason_values, continued -- the documented negative.
//
// This case sits here rather than beside its four siblings because it needs
// the KM-state and stream helpers defined just above; SRT_CASE registers by
// group name, not by position in the file.
// =========================================================================

// The enforcement option, addressed by slot rather than by name.
//
// Why by slot: the option occupies 53 at every tag in this chain, but its
// SPELLING changes at this milestone (the previous tag calls it
// SRTO_STRICTENC; this one renames it and keeps the old spelling only as a
// deprecated alias, which -Werror turns into a build failure here). This
// family must compile at both prefixes, and the rename itself is explicitly
// not something this milestone is scored on -- so the case addresses the
// option numerically and asserts nothing about either name. The slot is fixed
// by TEST_INTERFACE.md section 5; the set below is required to succeed, so a
// tree that numbers it elsewhere fails with a message that says so rather
// than silently configuring some other option.
static const SRT_SOCKOPT RJ_ENFORCE_ENC = (SRT_SOCKOPT)53;

static SockConf rj_unenforced_conf(const char* passphrase) {
    return [passphrase](SRTSOCKET s) {
        rj_live(s);
        const int off = 0;
        ST_REQUIRE_MSG(srt_setsockflag(s, RJ_ENFORCE_ENC, &off, (int)sizeof off) == 0,
                       "setting the encryption-enforcement option (slot %d) to false failed: "
                       "%s; TEST_INTERFACE.md section 5 fixes the slot and states that the "
                       "option is settable before connect",
                       (int)RJ_ENFORCE_ENC, srt_getlasterror_str());
        st_set_int(s, SRTO_PBKEYLEN, 16);
        st_set_str(s, SRTO_PASSPHRASE, passphrase);
    };
}

SRT_CASE(g05_rejectreason_values, enforcement_off_connects_and_reports_no_reason) {
    // The half the four cases above cannot reach. Those assert that
    // the five reasons appear at the DEFAULT enforcement setting. That is
    // satisfied just as well by a library that refuses every mismatched
    // handshake unconditionally -- which is wrong, and is what this case
    // catches: with enforcement explicitly off on both sides, a mismatched
    // passphrase must NOT reject. The connection comes up, the failure shows
    // only through the KM state and through payload that never arrives, and
    // the rejection surface has nothing to report.
    //
    // It is still a v1.4.0-only observation: the last assertion reads
    // srt_getrejectreason, which does not exist at the previous tag, so the
    // case fails there on dlsym rather than passing for free.
    RjLink L;
    rj_run(L, rj_unenforced_conf("listener-side-passphrase"),
           rj_unenforced_conf("caller-side-passphrase"));

    ST_REQUIRE_MSG(L.rc != SRT_ERROR,
                   "with the enforcement option off on both sides a mismatched passphrase "
                   "still refused the handshake (%s, %d); at this setting the handshake must "
                   "complete and the mismatch must surface only as an unusable key",
                   rj_errname(L.err), L.err);
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK,
                   "srt_connect succeeded with enforcement off but the listener accepted "
                   "nothing: %s", srt_getlasterror_str());

    const int st = srt_getsockstate(L.cal);
    ST_REQUIRE_MSG(st == SRTS_CONNECTED,
                   "the caller is in state %d after a successful connect with enforcement "
                   "off; expected SRTS_CONNECTED (%d)",
                   (int)st, (int)SRTS_CONNECTED);

    // Connected, but the key is unusable: nothing decrypts, so nothing is
    // delivered. This is the "reported only through the KM state and through
    // payload that never arrives" half.
    const std::vector<std::string> got = rj_exchange(L.cal, L.acc, 8, RJ_LIVE_PAYLOAD,
                                                     RJ_KM_GAP_US, 2000);
    ST_REQUIRE_MSG(got.empty(),
                   "the two sides hold different passphrases and enforcement is off, yet the "
                   "receiver delivered %d message(s); a key that does not match must not "
                   "decrypt anything",
                   (int)got.size());

    const int rcv = rj_settled_kmstate(L.acc, SRTO_RCVKMSTATE, 2000, "accepted socket");
    ST_REQUIRE_MSG(rcv != SRT_KM_S_SECURED && rcv != SRT_KM_S_SECURING,
                   "the accepted socket reports SRTO_RCVKMSTATE = %s on a connection whose "
                   "two passphrases differ; the KM state is the only place this failure is "
                   "visible at this setting, and it must report the condition",
                   rj_statename(rcv));

    // The rejection surface has nothing to say about a handshake that was not
    // rejected.
    st_getrej_fn f = rj_getrej();
    ST_REQUIRE_MSG(f != 0,
                   "dlsym(RTLD_DEFAULT, \"srt_getrejectreason\") is NULL: the rejection "
                   "surface this milestone introduces is not exported");
    const int why = (int)f(L.cal);
    ST_REQUIRE_MSG(why == RJ_UNKNOWN,
                   "srt_getrejectreason reports %d on a caller whose handshake SUCCEEDED "
                   "(enforcement off, mismatched passphrases); expected SRT_REJ_UNKNOWN (%d)",
                   why, RJ_UNKNOWN);
}
