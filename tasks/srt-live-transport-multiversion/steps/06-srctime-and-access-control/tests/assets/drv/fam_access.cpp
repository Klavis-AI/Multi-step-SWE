// Family "access", checkpoint 06 (SRT v1.4.2). Four scored groups, all `new`:
//
//   g05_hook_reject_reason   (new)  a listener hook that refuses without naming
//                                   a code leaves the caller reading
//                                   SRT_REJ_UNKNOWN, not the SRT_REJ_RESOURCE
//                                   the previous milestone pre-seeded.
//   g06_app_reject_reason    (new)  srt_setrejectreason: the code the hook sets
//                                   reaches the caller verbatim, and the setter
//                                   refuses below SRT_REJC_PREDEFINED and an
//                                   invalid socket.
//   g07_connect_timeout      (new)  a connect to a dead loopback port fails on
//                                   SRTO_CONNTIMEO and reports SRT_REJ_TIMEOUT.
//   g08_entry_points         (new)  the four group entry points and the renamed
//                                   bind call resolve out of the installed
//                                   library; the old name no longer does.
//
// ---------------------------------------------------------------------------
// Why this family is uniformly `new` and names new symbols only through dlsym
//
// scored.txt records the family as "Every group here is new; this file may name
// anything this milestone adds", and the brief's identifier-surface note says a
// `new` group that still builds against the previous milestone is a group that
// fails to discriminate. So this file is NOT contorted to compile at v1.4.0 --
// `#include <srt/access_control.h>`, which does not exist in the v1.4.0 install
// prefix, is deliberately unconditional, and is what makes `dev/fam.sh 05` stop
// at the preprocessor rather than at an assertion. That is the intended
// outcome for this family (author prompt, "This family is uniformly new").
//
// The four functions the milestone adds -- srt_setrejectreason and the four
// group entry points -- are still resolved with dlsym(RTLD_DEFAULT, ...) rather
// than called by name. Not for portability, but because "is exported from the
// shared library" is the claim g08 makes, and dlsym is the only honest way to
// test it: a candidate whose header declares srt_create_group but whose library
// does not export it must lose g08 on an assertion that names the missing
// symbol, not on a link error that says nothing about which entry point is
// missing. See scored.txt's own "NEW SYMBOLS" note.
//
// ---------------------------------------------------------------------------
// Traceability (REQUIREMENTS.md §"Step 06", frozen 2026-09-21)
//
//   g05  RQ-06-17 (hook refusal -> caller reads SRT_REJ_UNKNOWN),
//        RQ-06-18 (so UNKNOWN no longer distinguishes "refused without a code"
//        from "nothing rejected"; that is what the pre-seed assertion measures).
//   g06  RQ-06-19 (the setter's return values and the 1000 boundary),
//        RQ-06-20 (the code reaches the caller unclamped),
//        RQ-06-21 (SRT_REJC_PREDEFINED is in the main header) -- the two-header
//        half of RQ-06-21 is g08's subject.
//   g07  RQ-06-22 (SRT_ENOSERVER on the socket, SRT_REJ_TIMEOUT as the reason,
//        elapsed time in the neighbourhood of the configured option).
//   g08  RQ-06-26 (the four group entry points declared and exported),
//        RQ-06-27 (the old bind name is not an exported symbol, the new one is),
//        RQ-06-21 (access_control.h installs as a second public header).
//
// Every case here was shown to be able to fail before it was accepted: each
// one was broken on the behaviour it targets -- not on its assertion -- and
// checked to report the failure with a message naming actual against expected.
// Twelve such breaks cover the eleven cases; they were applied through a
// compiled-in selector that is deliberately NOT part of this file, so that the
// shipped driver has exactly one behaviour. The list of what each break moved
// and what it printed is in the report filed with this file.
//
// =========================================================================

// The group API is inside `#if ENABLE_EXPERIMENTAL_BONDING` in the public
// header, and the library is built with that flag while the driver's own
// translation unit is not (meta.env CMAKE_EXTRA; scored.txt's "BONDING ENUM
// COLLISION"). The step declares no DRV_DEFS -- build-step.py threads DRV_DEFS
// into the shipped compile line and nothing else -- so the driver defines it
// here instead, before srt.h is first reached, which is the only place it can
// be defined from. Doing this turns the group entries at or after
// SRTO_PEERIDLETIMEO into the library's own numbering in this TU, which
// removes the collision rather than working around it; it does not lift the
// family's rule about not naming those options, and none is named here.
//
// The `#ifndef` is for the case where the shipped compile line ever learns to
// pass the flag itself: then this is inert and the flag arrives once.
#ifndef ENABLE_EXPERIMENTAL_BONDING
#define ENABLE_EXPERIMENTAL_BONDING 1
#endif
#include <srt/srt.h>

#include "stest.h"
#include "srtkit.h"
#include "relay.h"

// The second public header. Unconditional on purpose: it is absent at the
// previous milestone, and this family is not required to build there.
#include <srt/access_control.h>

#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// =========================================================================
// Restated values, each pinned rather than assumed.
//
// Only two numbers in this file are not taken from a header, and both are
// pinned here: SRT_REJ_UNKNOWN (0, the first member of SRT_REJECT_REASON, so
// its value follows from the declaration rather than from a literal) and the
// remote-endpoint address family used by srt_connect_group (AF_INET, the
// family st_loopback builds). The static_asserts under the SRT_VERSION_VALUE
// guard are what stop either from drifting if a header in the chain renumbers
// something: the guard is nested because a bare
// `#if SRT_VERSION_VALUE >= ...` is a *preprocessor syntax error* at a prefix
// that ships no <srt/version.h>, not a quiet false.
//
// The access-control constants are deliberately NOT restated: the phrase "it
// may name anything this milestone adds" is taken at its word, and the header
// that defines them is the thing under test. That is also why the ordinary
// post-v1.3.0 guard shape is enough here and the #ifndef restatement block is
// absent -- there is nothing to restate when the header is present.
// =========================================================================

static const int AC_UNKNOWN        = 0;     // first member of SRT_REJECT_REASON
static const int AC_ROGUE          = 4;     // SRT_REJ_ROGUE
static const int AC_TIMEOUT        = 16;    // SRT_REJ_TIMEOUT, last of the set
static const int AC_PREDEFINED     = 1000;  // SRT_REJC_PREDEFINED
static const int AC_USERDEFINED    = 2000;  // SRT_REJC_USERDEFINED
static const int AC_FORBIDDEN      = 1403;  // SRT_REJX_FORBIDDEN
static const int AC_BELOW_RANGE    = 999;   // one below the application's first code
static const int AC_EXACT_BOUNDARY = 1000;  // exactly SRT_REJC_PREDEFINED: accepted
static const int AC_AF_INET        = 2;     // AF_INET

#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 0)
static_assert(SRT_REJ_UNKNOWN == AC_UNKNOWN,
              "restated SRT_REJ_UNKNOWN disagrees with <srt/srt.h>");
// SRT_REJ_RESOURCE is the value the milestone is moving *away* from; g05's
// whole claim is that the two are distinguishable, so make that a compile-time
// requirement rather than a comment.
static_assert(SRT_REJ_RESOURCE != AC_UNKNOWN,
              "SRT_REJ_RESOURCE must be distinguishable from SRT_REJ_UNKNOWN");
static_assert(SRT_REJ_TIMEOUT == AC_TIMEOUT,
              "restated SRT_REJ_TIMEOUT disagrees with <srt/srt.h>");
static_assert(SRT_REJ_ROGUE == AC_ROGUE, "restated SRT_REJ_ROGUE disagrees with <srt/srt.h>");
static_assert(AF_INET == AC_AF_INET, "restated AF_INET disagrees with <sys/socket.h>");
#endif
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 2)
static_assert(SRT_REJC_PREDEFINED == AC_PREDEFINED,
              "restated SRT_REJC_PREDEFINED disagrees with <srt/srt.h>");
static_assert(SRT_REJC_USERDEFINED == AC_USERDEFINED,
              "restated SRT_REJC_USERDEFINED disagrees with <srt/srt.h>");
static_assert(SRT_REJC_INTERNAL == 0, "restated SRT_REJC_INTERNAL disagrees with <srt/srt.h>");
static_assert(SRT_REJX_FORBIDDEN == AC_FORBIDDEN,
              "restated SRT_REJX_FORBIDDEN disagrees with <srt/access_control.h>");
// RQ-06-21: the split is not where a first guess puts it. The category codes
// are in the MAIN header (they compile above, with access_control.h also
// included, so this pins only that they are reachable), and the catalogue is
// in the second. That the second header is a *second* header -- installed
// beside the first, includable without the first -- is g08's subject and is
// asserted there by opening the installed directory.
static_assert(SRT_REJC_VALUE(1403) == 1000 && SRT_REJC_VALUE(2999) == 2000,
              "SRT_REJC_VALUE disagrees with the documented 1000-wide categories");
#endif
#endif

// =========================================================================
// The milestone's new symbols, resolved rather than named.
// =========================================================================

typedef SRTSOCKET (*ac_create_group_fn)(SRT_GROUP_TYPE);
typedef int (*ac_connect_group_fn)(SRTSOCKET, SRT_SOCKGROUPCONFIG[], int);
typedef int (*ac_group_data_fn)(SRTSOCKET, SRT_SOCKGROUPDATA*, size_t*);
typedef SRTSOCKET (*ac_groupof_fn)(SRTSOCKET);
typedef int (*ac_bind_acquire_fn)(SRTSOCKET, UDPSOCKET);
typedef int (*ac_setreject_fn)(SRTSOCKET, int);

template <typename Fn>
static Fn ac_dlsym(const char* n) {
    void* s = dlsym(RTLD_DEFAULT, n);
    if (!s) return 0;
    Fn f;
    std::memcpy(&f, &s, sizeof f);
    return f;
}

// The handle g08 works on. RTLD_NOW so a library that would fail to bind is
// reported here rather than at the first call, and RTLD_GLOBAL so that what it
// exports is visible in the default scope too.
static void* ac_lib_handle() {
    const char* names[] = {"libsrt.so.1", "libsrt.so", "libsrt.so.1.4.2", 0};
    for (int i = 0; names[i]; i++) {
        void* h = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
        if (h) return h;
    }
    return 0;
}

template <typename Fn>
static Fn ac_dlsym_in(void* h, const char* n) {
    void* s = h ? dlsym(h, n) : 0;
    // The default-scope fallback exists so that a library already loaded by the
    // link line is still searched.
    if (!s) s = dlsym(RTLD_DEFAULT, n);
    if (!s) return 0;
    Fn f;
    std::memcpy(&f, &s, sizeof f);
    return f;
}

static ac_setreject_fn ac_setreject() { return ac_dlsym<ac_setreject_fn>("srt_setrejectreason"); }

// The value SRT rejects *without* a hook having said anything, restated as a
// plain int so that neither a dlsym miss nor a missing declaration can turn
// "the library named a code" into "the driver failed to compile".
static const int AC_RESOURCE = 3;

static const char* ac_errname(int e) {
    switch (e) {
    case SRT_SUCCESS: return "SRT_SUCCESS";
    case SRT_ECONNREJ: return "SRT_ECONNREJ";
    case SRT_ENOSERVER: return "SRT_ENOSERVER";
    case SRT_EINVPARAM: return "SRT_EINVPARAM";
    case SRT_EINVSOCK: return "SRT_EINVSOCK";
    case SRT_ETIMEOUT: return "SRT_ETIMEOUT";
    default: return "?";
    }
}

static long long ac_now_ms() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// =========================================================================
// The link: listener -> relay -> caller, with the accept behind an epoll wait.
//
// Not st_try_connect, for the reason fam_reject.cpp gives and which was
// re-measured here: srt_accept on a refused handshake does not wake, so a
// blocking accept costs its own full SRTO_RCVTIMEO on every refusal. The
// acceptor therefore waits on an EID and is bounded by ACCEPT_MS, and the
// listener's SO_RCVTIMEO is kept short so a silently-refused handshake still
// surfaces as the caller's own error rather than as a timeout.
// =========================================================================

struct AcLink {
    SRTSOCKET lsn = SRT_INVALID_SOCK;
    SRTSOCKET cal = SRT_INVALID_SOCK;
    SRTSOCKET acc = SRT_INVALID_SOCK;
    Relay* relay = nullptr;
    int rc = -1;         // what srt_connect returned
    int err = 0;         // srt_getlasterror() at that moment
    long long elapsed_ms = 0;
    int epoll_rc = -1;   // what the acceptor's bounded wait saw
    std::atomic<bool> connect_done{false};  // set once srt_connect has returned
    std::thread worker;

    AcLink() {}
    AcLink(const AcLink&) = delete;
    AcLink& operator=(const AcLink&) = delete;
    ~AcLink() { close(); }

    void close() {
        if (worker.joinable()) worker.join();
        if (acc != SRT_INVALID_SOCK) { srt_close(acc); acc = SRT_INVALID_SOCK; }
        if (cal != SRT_INVALID_SOCK) { srt_close(cal); cal = SRT_INVALID_SOCK; }
        if (lsn != SRT_INVALID_SOCK) { srt_close(lsn); lsn = SRT_INVALID_SOCK; }
        delete relay;
        relay = nullptr;
    }
};

// The acceptor's patience is tied to the CALLER, not to the clock.
//
// The acceptor runs concurrently with srt_connect, so a single bounded
// srt_epoll_wait had to cover the whole handshake, and that is a race: on a
// loaded host the handshake outran the budget and the case then reported
// "srt_connect succeeded but nothing was accepted", which is a statement about
// this machine rather than about the library. Observed on the 05:08Z oracle
// ladder at step 06.
//
// So the wait is now a loop of short slices with two exits: the listener
// signals (accept and stop), or the connect attempt has returned AND a further
// AC_ACCEPT_GRACE_MS ms has passed with the listener still silent. While the connect is
// still in flight the acceptor simply keeps waiting -- srt_connect is itself
// bounded by SRTO_CONNTIMEO and the whole case by the harness deadline, so
// there is nothing here for an unbounded wait to hang on. The refusal cases
// stay quick, because a refused connect returns promptly and only the grace is
// added; the accepted cases get as long as the handshake actually takes.
static const int AC_ACCEPT_GRACE_MS = 4000;
static const int AC_ACCEPT_SLICE_MS = 100;
static const int AC_LSN_TIMEO = 2500;

// Brings the listener up, arms the acceptor, connects the caller, joins the
// acceptor. Nothing throws after the thread is armed.
static void ac_run(AcLink& L, const SockConf& lconf, const SockConf& cconf) {
    L.lsn = st_new_socket();
    ST_REQUIRE_MSG(L.lsn != SRT_INVALID_SOCK, "srt_socket (listener): %s", srt_getlasterror_str());
    st_set_int(L.lsn, SRTO_RCVTIMEO, AC_LSN_TIMEO);
    st_set_int(L.lsn, SRTO_RCVSYN, 0);
    lconf(L.lsn);

    sockaddr_in la = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(L.lsn, (sockaddr*)&la, sizeof la) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    int nl = (int)sizeof la;
    ST_REQUIRE_MSG(srt_getsockname(L.lsn, (sockaddr*)&la, &nl) != SRT_ERROR, "srt_getsockname: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(L.lsn, 1) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());

    RelaySched sc;
    L.relay = new Relay(ntohs(la.sin_port), sc);
    const sockaddr_in ra = st_loopback(L.relay->port());

    const SRTSOCKET lsn = L.lsn;
    L.worker = std::thread([&L, lsn] {
        const int eid = srt_epoll_create();
        if (eid == SRT_INVALID_SOCK) return;
        int in = (int)SRT_EPOLL_IN;
        if (srt_epoll_add_usock(eid, lsn, &in) != SRT_SUCCESS) {
            srt_epoll_release(eid);
            return;
        }
        long long settled_at = -1;
        for (;;) {
            SRTSOCKET rd[4];
            int rn = 4;
            L.epoll_rc = srt_epoll_wait(eid, rd, &rn, NULL, NULL, AC_ACCEPT_SLICE_MS, NULL, NULL,
                                        NULL, NULL);
            if (L.epoll_rc > 0) {
                L.acc = srt_accept(lsn, NULL, NULL);
                break;
            }
            if (!L.connect_done) continue;
            if (settled_at < 0) settled_at = ac_now_ms();
            if (ac_now_ms() - settled_at >= AC_ACCEPT_GRACE_MS) break;
        }
        srt_epoll_release(eid);
    });

    L.cal = st_new_socket();
    ST_REQUIRE_MSG(L.cal != SRT_INVALID_SOCK, "srt_socket (caller): %s", srt_getlasterror_str());
    cconf(L.cal);
    const long long t0 = ac_now_ms();
    L.rc = srt_connect(L.cal, (sockaddr*)&ra, sizeof ra);
    L.err = srt_getlasterror(nullptr);
    L.elapsed_ms = ac_now_ms() - t0;
    L.connect_done = true;
    L.worker.join();
}

// A refusal is asserted on three things at once, so that "the listener said no"
// cannot be confused with "the caller gave up": the connect failed, the library
// named it a rejected handshake, and nothing was accepted.
static void ac_require_refused(const AcLink& L, const char* what) {
    ST_REQUIRE_MSG(L.rc == SRT_ERROR, "%s: srt_connect returned %d, expected failure: %s", what,
                   L.rc, srt_strerror(L.err, 0));
    ST_REQUIRE_MSG(L.err == SRT_ECONNREJ,
                   "%s: srt_connect failed with %s (%d), expected %s (%d, \"%s\"); the case is "
                   "about a handshake the listener *refused*",
                   what, ac_errname(L.err), L.err, ac_errname(SRT_ECONNREJ), (int)SRT_ECONNREJ,
                   srt_strerror(SRT_ECONNREJ, 0));
    ST_REQUIRE_MSG(L.acc == SRT_INVALID_SOCK,
                   "%s: the listener accepted a socket for a handshake the caller reports as "
                   "refused (%s / %d)",
                   what, ac_errname(L.err), L.err);
}

static void ac_require_connected(const AcLink& L, const char* what) {
    ST_REQUIRE_MSG(L.rc != SRT_ERROR, "%s: srt_connect failed: %s", what,
                   srt_strerror(L.err, 0));
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK,
                   "%s: srt_connect returned %d but srt_accept produced no socket: %s", what, L.rc,
                   srt_getlasterror_str());
}

// =========================================================================
// g05_hook_reject_reason   (new)   RQ-06-17, RQ-06-18
//
// scored.txt: "a listener whose registered access-control hook returns a
// negative value refuses the connection, the caller's connect fails, and the
// caller's rejection reason reads as unknown rather than as whatever value that
// socket's rejection reason held before the attempt, so the refusal is reported
// through the hook path and not left at a stale value".
//
// The "stale value" the description means is named by interface.md §5.3: the
// pinned AccessControl.md says the accepted socket arrives pre-seeded with
// SRT_REJ_RESOURCE, and at this milestone it arrives pre-seeded with
// SRT_REJ_UNKNOWN. So what discriminates is not a bare "the reason is 0" --
// that is also what a socket that was never refused reports -- but "the reason
// is not the pre-seed, while the hook demonstrably refused and the previous
// milestone would have left 3". Both halves are asserted together below, and
// the positive control in the same group requires the same hook returning 0 to
// produce a live connection, so a library that refuses everything
// unconditionally cannot score the point.
//
// The measured value at the shipped v1.4.2 prefix is SRT_REJ_ROGUE (4), not the
// SRT_REJ_UNKNOWN (0) that interface.md §5.3 and deltas/06.md D7 both name. The
// long comment at the assertion explains why -- the 0 is what the listener
// sends and the caller then overwrites it from its own handshake
// interpretation -- and the assertion is written to the shape the description
// states rather than to either single number. Reported to the orchestrator.
// =========================================================================

namespace {

struct HookCtx {
    int calls = 0;
    SRTSOCKET named = SRT_INVALID_SOCK;
    int set_rc = -1;      // what srt_setrejectreason returned, when it is called
    int set_value = -1;   // the value it was called with
    SRTSOCKET set_on = SRT_INVALID_SOCK;
};

HookCtx* g_plain_ctx = 0;   // the hook whose only job is to accept or refuse

// The positive control's hook. Deliberately *not* `ac_hook_plain` with a flag
// flipped: the control has to be able to connect even on a run that is
// exercising a mutation of the refusing hook, otherwise "break the case" and
// "break the control" would be the same act and the group would have no
// standing baseline.
int ac_hook_accepting(void*, SRTSOCKET ns, int, const sockaddr*, const char*) {
    if (!g_plain_ctx) return -1;
    g_plain_ctx->calls++;
    g_plain_ctx->named = ns;
    return 0;
}

// Returns -1 (refuse). Never names a code.
int ac_hook_plain(void*, SRTSOCKET ns, int, const sockaddr*, const char*) {
    if (!g_plain_ctx) return -1;
    g_plain_ctx->calls++;
    g_plain_ctx->named = ns;
    return -1;
}

// Refuses *and* names the code SRT_REJX_FORBIDDEN on the accepted socket.
int ac_hook_naming(void*, SRTSOCKET ns, int, const sockaddr*, const char*) {
    if (!g_plain_ctx) return -1;
    g_plain_ctx->calls++;
    g_plain_ctx->named = ns;
    ac_setreject_fn f = ac_setreject();
    if (f) {
        const int v = AC_FORBIDDEN;
        g_plain_ctx->set_rc = f(ns, v);
        g_plain_ctx->set_value = v;
        g_plain_ctx->set_on = ns;
    }
    return -1;
}

// A hook used by the cases that need a listener armed with *a* hook and no
// particular outcome from it.

}  // namespace

SRT_CASE(g05_hook_reject_reason, a_refusing_hook_leaves_the_caller_reading_unknown) {
    // RQ-06-17. A listener whose hook refuses without naming a code.
    HookCtx ctx;
    g_plain_ctx = &ctx;

    AcLink L;
    ac_run(L, [](SRTSOCKET l) {
        ST_REQUIRE_MSG(srt_listen_callback(l, ac_hook_plain, 0) != SRT_ERROR,
                       "srt_listen_callback: %s", srt_getlasterror_str());
    }, st_noconf);

    ac_require_refused(L, "a refusing access-control hook");
    ST_REQUIRE_MSG(ctx.calls == 1,
                   "the listener's access-control hook ran %d times, expected exactly 1; the "
                   "case cannot say the refusal came through the hook path if the hook never ran",
                   ctx.calls);
    // The hook is handed the socket the listener is building for this
    // connection -- which, precisely because the hook refused, the application
    // never gets to see through srt_accept. So the check is that the hook was
    // given *a* live socket id of this connection and not the listener's, which
    // is what tells "the hook ran per-connection" apart from "the hook ran once
    // for the listener".
    ST_REQUIRE_MSG(ctx.named != SRT_INVALID_SOCK && ctx.named != L.lsn,
                   "the hook was called with socket id %d, which is %s; a per-connection hook "
                   "must be handed the socket being accepted, not the listener (%d) and not an "
                   "unset id",
                   (int)ctx.named,
                   ctx.named == SRT_INVALID_SOCK ? "SRT_INVALID_SOCK" : "the listener id",
                   (int)L.lsn);

    // The subject: the caller's reason must not be the pre-seeded
    // SRT_REJ_RESOURCE that the pinned AccessControl.md describes and that the
    // previous milestone actually produces.
    //
    // MEASURED, and the reason this case asserts a *set* rather than the single
    // value the documents name. interface.md §5.3 and deltas/06.md D7 both say
    // the caller reads SRT_REJ_UNKNOWN (0) here. Against the shipped v1.4.2
    // prefix it reads SRT_REJ_ROGUE (4) -- at dev/root/06 and dev/bondroot/06
    // alike, with and without the relay in the path, on every iteration. The
    // mechanism is visible in the library's own log: the listener refuses with
    // URQ_FAILURE carrying SRT_REJ_UNKNOWN ("rsp(REJECT): 1000 - Unknown or
    // erroneous"), and the caller's processConnectResponse maps that back to 0
    // through RejectReasonForURQ -- but the rejection response carries no
    // handshake extension, so the caller's own interpretSrtHandshake
    // (core.cpp:2886 at v1.4.2, logging "HS VERSION=5 but no handshake
    // extension found!") overwrites the reason with SRT_REJ_ROGUE before
    // postConnect returns CONN_REJECT. The documented 0 is what the *listener*
    // sends; 4 is what the caller is left holding. v1.4.0 reaches the same call
    // site with its reason still at the pre-seed and reports 3, so the
    // discrimination this step needs is intact -- it is 3-vs-4, not 3-vs-0.
    //
    // So the assertion is the shape scored.txt actually describes -- "reads as
    // unknown rather than as whatever value that socket's rejection reason held
    // before the attempt" -- expressed as two halves: not the pre-seed, and one
    // of the two values the hook path can leave behind. Asserting == 4 would
    // pin an incidental value no document states; asserting == 0 would fail
    // against the genuine release.
    const int got = srt_getrejectreason(L.cal);
    ST_REQUIRE_MSG(got != AC_RESOURCE,
                   "after a hook refusal that named no code, the caller's "
                   "srt_getrejectreason reports SRT_REJ_RESOURCE (%d) -- the value the pinned "
                   "AccessControl.md describes as pre-set, and the value the previous milestone "
                   "leaves untouched because its hook path never writes one. This milestone must "
                   "pre-seed the accepted socket with SRT_REJ_UNKNOWN (%d) before the hook runs, "
                   "so a refusal that names no code cannot come back as %d",
                   got, AC_UNKNOWN, AC_RESOURCE);
    // The complement, so the case cannot be satisfied by an arbitrary value:
    // 0 is what the listener puts on the wire once the pre-seed is UNKNOWN, and
    // 4 is what the caller's own handshake interpretation leaves after a
    // rejection response that carries no extension. Anything else means the
    // refusal was reported through some other mechanism.
    ST_REQUIRE_MSG(got == AC_UNKNOWN || got == AC_ROGUE,
                   "after a hook refusal that named no code, the caller's "
                   "srt_getrejectreason reports %d; expected SRT_REJ_UNKNOWN (%d), which is what "
                   "the listener sends once the accepted socket is pre-seeded with it, or "
                   "SRT_REJ_ROGUE (%d), which is what the caller's own handshake interpretation "
                   "leaves after a rejection response that carries no extension. A third value "
                   "means the refusal was not reported through the hook path at all",
                   got, AC_UNKNOWN, AC_ROGUE);
}

SRT_CASE(g05_hook_reject_reason, an_accepting_hook_is_the_control) {
    // The positive control the group needs: the same listener, the same relay,
    // the same call, and a hook that differs only in its return value. Without
    // it, a library that refuses every connection unconditionally -- or one
    // whose srt_listen_callback is a no-op that happens to leave the caller
    // reading 0 -- would score the case above.
    HookCtx ctx;
    g_plain_ctx = &ctx;
    AcLink L;
    ac_run(L, [](SRTSOCKET l) {
        ST_REQUIRE_MSG(srt_listen_callback(l, ac_hook_accepting, 0) != SRT_ERROR,
                       "srt_listen_callback: %s", srt_getlasterror_str());
    }, st_noconf);
    ac_require_connected(L, "an accepting access-control hook");
    ST_REQUIRE_MSG(ctx.calls == 1,
                   "the listener's access-control hook ran %d times for one connection, "
                   "expected exactly 1",
                   ctx.calls);
    // Here the socket the hook was handed *is* observable: srt_accept returns
    // the same connection. Asserting identity between the two is the strongest
    // available statement that the hook and the accept both describe one
    // connection, and it is only assertable on the accepting path.
    ST_REQUIRE_MSG(ctx.named == L.acc,
                   "the hook was given socket id %d and srt_accept returned id %d; the hook "
                   "must be called with the socket being accepted",
                   (int)ctx.named, (int)L.acc);
    // And the connection is real: bytes cross it.
    const std::string m = st_payload(0, 64);
    ST_REQUIRE_MSG(srt_sendmsg(L.cal, m.data(), (int)m.size(), -1, 1) == (int)m.size(),
                   "srt_sendmsg on an accepted connection: %s", srt_getlasterror_str());
    // srt_accept inherits the listener's SRTO_RCVSYN=0; without this the
    // receive below would return SRT_EASYNCRCV immediately, before the sent
    // payload could arrive, and the case would fail for the wrong reason.
    st_set_int(L.acc, SRTO_RCVSYN, 1);
    st_set_int(L.acc, SRTO_RCVTIMEO, 4000);
    // The live congestion controller refuses a receive buffer smaller than the
    // connection's maximum payload (1444 bytes of SRT payload at the default
    // MTU), so the buffer is sized for the largest legal packet and the check
    // is on the length returned, not on the buffer.
    std::string buf((size_t)st_get_int(L.acc, SRTO_PAYLOADSIZE) + 256, '\0');
    ST_REQUIRE_MSG((int)buf.size() >= 1444,
                   "SRTO_PAYLOADSIZE on the accepted socket is %d, leaving a receive buffer of "
                   "%zu bytes; a live-mode receive smaller than the maximum payload is refused "
                   "by the library before any data is read",
                   st_get_int(L.acc, SRTO_PAYLOADSIZE), buf.size());
    const int rc = srt_recvmsg(L.acc, &buf[0], (int)buf.size());
    ST_REQUIRE_MSG(rc == (int)m.size() && buf.substr(0, (size_t)rc) == m,
                   "the accepted connection delivered %d bytes, expected the %zu sent; an "
                   "accepting hook must produce a usable connection",
                   rc, m.size());
}

// =========================================================================
// g06_app_reject_reason   (new)   RQ-06-19, RQ-06-20
//
// scored.txt: "a hook that sets a rejection code in the application-defined
// range on the socket being accepted and then returns a negative value causes
// the caller to read back that exact code rather than a code clamped into the
// library's own small set, and the call that sets it refuses a code below the
// application range as a bad parameter and an invalid socket as a bad socket,
// while accepting the lowest code in that range".
// =========================================================================

SRT_CASE(g06_app_reject_reason, a_named_code_reaches_the_caller_verbatim) {
    // RQ-06-20. The hook names SRT_REJX_FORBIDDEN (1403) -- a catalogue code
    // from the second header, far above SRT_REJ__SIZE -- and refuses.
    ac_setreject_fn setf = ac_setreject();
    ST_REQUIRE_MSG(setf != 0,
                   "dlsym(RTLD_DEFAULT, \"srt_setrejectreason\") returned NULL: the library does "
                   "not export the setter, so an application cannot name a code at all");

    HookCtx ctx;
    g_plain_ctx = &ctx;
    AcLink L;
    ac_run(L, [](SRTSOCKET l) {
        ST_REQUIRE_MSG(srt_listen_callback(l, ac_hook_naming, 0) != SRT_ERROR,
                       "srt_listen_callback: %s", srt_getlasterror_str());
    }, st_noconf);

    ac_require_refused(L, "a hook naming SRT_REJX_FORBIDDEN");
    ST_REQUIRE_MSG(ctx.calls == 1, "the hook ran %d times, expected exactly 1", ctx.calls);
    ST_REQUIRE_MSG(ctx.set_rc == 0,
                   "srt_setrejectreason(%d, %d) on the accepted socket returned %d (%s), "
                   "expected 0: %d is above SRT_REJC_PREDEFINED (%d) and is the application's "
                   "to set",
                   (int)ctx.set_on, ctx.set_value, ctx.set_rc, ac_errname(srt_getlasterror(nullptr)),
                   ctx.set_value, AC_PREDEFINED);
    ST_REQUIRE_MSG(ctx.set_on == ctx.named,
                   "the setter was called on socket %d but the hook was handed socket %d; the "
                   "code must be set on the socket being accepted, which is the one the caller "
                   "is waiting on",
                   (int)ctx.set_on, (int)ctx.named);

    srt_clearlasterror();
    const int got = srt_getrejectreason(L.cal);
    ST_REQUIRE_MSG(got == AC_FORBIDDEN,
                   "the caller reads rejection reason %d, expected %d (SRT_REJX_FORBIDDEN); the "
                   "code the hook names must cross the handshake response unchanged, not be "
                   "clamped into the library's own enumeration (whose size is SRT_REJ__SIZE, "
                   "%d, far below %d) and not be replaced by SRT_REJ_UNKNOWN (%d)",
                   got, AC_FORBIDDEN, (int)SRT_REJ__SIZE, AC_FORBIDDEN, AC_UNKNOWN);
}

SRT_CASE(g06_app_reject_reason, the_setter_refuses_below_the_application_range) {
    // RQ-06-19, first half. Driven directly on a socket owned by the case, so
    // the assertion is about the setter and nothing else.
    ac_setreject_fn setf = ac_setreject();
    ST_REQUIRE_MSG(setf != 0,
                   "dlsym(RTLD_DEFAULT, \"srt_setrejectreason\") returned NULL: the setter is "
                   "not exported");

    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    const int v = AC_BELOW_RANGE;
    srt_clearlasterror();
    const int rc = setf(s, v);
    const int e = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc == SRT_ERROR && e == SRT_EINVPARAM,
                   "srt_setrejectreason(sock, %d) returned %d with last error %d (%s), expected "
                   "SRT_ERROR with SRT_EINVPARAM (%d): %d is below SRT_REJC_PREDEFINED (%d) and "
                   "belongs to the protocol, not to the application",
                   v, rc, e, ac_errname(e), (int)SRT_EINVPARAM, v, AC_PREDEFINED);

    // And it stored nothing: the socket's reason is still the neutral one.
    srt_clearlasterror();
    const int got = srt_getrejectreason(s);
    ST_REQUIRE_MSG(got == AC_UNKNOWN,
                   "after a refused srt_setrejectreason the socket reports reason %d, expected "
                   "SRT_REJ_UNKNOWN (%d): the bound must be checked before anything is stored",
                   got, AC_UNKNOWN);
    srt_close(s);
}

SRT_CASE(g06_app_reject_reason, the_lowest_application_code_is_accepted) {
    // RQ-06-19, second half, on the boundary itself.
    ac_setreject_fn setf = ac_setreject();
    ST_REQUIRE_MSG(setf != 0, "srt_setrejectreason is not exported");

    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    const int v = AC_EXACT_BOUNDARY;
    srt_clearlasterror();
    const int rc = setf(s, v);
    ST_REQUIRE_MSG(rc == 0,
                   "srt_setrejectreason(sock, %d) returned %d (%s), expected 0: %d is exactly "
                   "SRT_REJC_PREDEFINED (%d), the lowest code the application owns, and the test "
                   "is not strict -- a value equal to the boundary is in range",
                   v, rc, ac_errname(srt_getlasterror(nullptr)), v, AC_PREDEFINED);
    srt_clearlasterror();
    const int got = srt_getrejectreason(s);
    ST_REQUIRE_MSG(got == v,
                   "after srt_setrejectreason(sock, %d) the socket reports %d; the setter must "
                   "store the value it accepted",
                   v, got);

    // The socket is still usable afterwards, so the setter did not damage it.
    sockaddr_in la = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(s, (sockaddr*)&la, sizeof la) != SRT_ERROR,
                   "srt_bind after a successful srt_setrejectreason: %s", srt_getlasterror_str());
    srt_close(s);
}

SRT_CASE(g06_app_reject_reason, an_invalid_socket_is_a_bad_socket) {
    // RQ-06-19, third part. The id is fabricated to a value the generator
    // cannot have handed out: ids start positive and decrement from there.
    ac_setreject_fn setf = ac_setreject();
    ST_REQUIRE_MSG(setf != 0, "srt_setrejectreason is not exported");

    const SRTSOCKET bogus = (SRTSOCKET)-2147483000;
    srt_clearlasterror();
    const int rc = setf(bogus, AC_FORBIDDEN);
    const int e = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc == SRT_ERROR && e == SRT_EINVSOCK,
                   "srt_setrejectreason(%d, %d) returned %d with last error %d (%s), expected "
                   "SRT_ERROR with SRT_EINVSOCK (%d); a never-allocated socket id is a bad "
                   "socket, which is a different error from the bad parameter the range check "
                   "gives",
                   (int)bogus, AC_FORBIDDEN, rc, e, ac_errname(e), (int)SRT_EINVSOCK);

    // The positive control beside it: the same call on a real socket succeeds,
    // so "it failed" is about the id and not about the call being refused
    // unconditionally.
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    srt_clearlasterror();
    const int v = AC_FORBIDDEN;
    const int ok = setf(s, v);
    ST_REQUIRE_MSG(ok == 0,
                   "srt_setrejectreason on a live socket with the in-range code %d returned %d "
                   "(%s); the control for the invalid-socket case requires the same call to "
                   "succeed on a valid socket",
                   v, ok, ac_errname(srt_getlasterror(nullptr)));
    srt_close(s);
}

// =========================================================================
// g07_connect_timeout   (new)   RQ-06-22
//
// scored.txt: "a caller with a short connect timeout configured that connects
// to a loopback port nothing is listening on fails after approximately that
// timeout and reports a rejection reason that names the timeout specifically,
// rather than reporting the unknown reason".
//
// interface.md §5 does not pin a particular reason code, but REQUIREMENTS.md
// RQ-06-22 does -- SRT_REJ_TIMEOUT (16) -- and names it from API-functions.md.
// The positive control is a real connection from a socket configured the same
// way, so "the timeout fired" cannot be a socket that cannot connect at all.
// =========================================================================

// One attempt at a loopback port nothing is listening on, at the given
// SRTO_CONNTIMEO. Returns what the caller can observe afterwards: how long the
// call took, the error it set, and the rejection reason left on the socket.
struct AcTimeo {
    long long ms;
    int err;
    int reason;
};

static AcTimeo ac_dead_connect(int conntimeo_ms) {
    // The UDP socket is closed before the connect, so on loopback the datagram
    // is refused immediately and the caller retries until SRTO_CONNTIMEO
    // expires rather than sitting in a black hole.
    uint16_t dead = 0;
    {
        const int s = udp_socket_bound(0, &dead);
        ::close(s);
    }

    SRTSOCKET c = st_new_socket();
    ST_REQUIRE_MSG(c != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    st_set_int(c, SRTO_CONNTIMEO, conntimeo_ms);

    const sockaddr_in ra = st_loopback(dead);
    srt_clearlasterror();
    const long long t0 = ac_now_ms();
    const int rc = srt_connect(c, (const sockaddr*)&ra, sizeof ra);
    AcTimeo t;
    t.err = srt_getlasterror(nullptr);
    t.ms = ac_now_ms() - t0;
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "srt_connect to loopback port %u, where nothing is listening, returned %d "
                   "(success); a connect to a dead port must fail",
                   (unsigned)dead, rc);

    // RQ-06-22: the reason is read from the socket that timed out, before it
    // is closed -- the requirement is that it survives the failed connect well
    // enough to answer.
    srt_clearlasterror();
    t.reason = srt_getrejectreason(c);
    srt_close(c);
    return t;
}

// How far BELOW the configured figure the wait may land: jitter only. There is
// deliberately no upper bound to match it. A ceiling on a wall-clock elapsed
// time fails on a loaded host for reasons that have nothing to do with the
// library, and it buys no discrimination that is not already bought
// deterministically: "waited less than it was told to" is caught by the bound
// below, and "waited an interval of its own choosing regardless of the option"
// is caught by the two-leg span in the case itself, which a fixed wait fails by
// showing no change. The sibling case in this group declines a wall-clock bound
// for the same reason and says so. A connect that never returns at all is
// caught by the harness's own per-case alarm (SRT_CASE_TIMEOUT), not by an
// assertion this group has to tune.
static const long long AC_TIMEO_EARLY = 300;

static void ac_require_timeout_leg(const AcTimeo& t, int requested) {
    ST_REQUIRE_MSG(t.err == SRT_ENOSERVER,
                   "with SRTO_CONNTIMEO at %d ms, srt_connect to a dead loopback port failed "
                   "with %s (%d), expected %s (%d, \"%s\"); the failure must be the absent peer "
                   "and not a socket setup error that would have no timeout to report",
                   requested, ac_errname(t.err), t.err, ac_errname(SRT_ENOSERVER),
                   (int)SRT_ENOSERVER, srt_strerror(SRT_ENOSERVER, 0));

    // RQ-06-22: the reason is read back from that same socket.
    ST_REQUIRE_MSG(t.reason == AC_TIMEOUT,
                   "after a connect that expired its %d ms SRTO_CONNTIMEO, the caller reads "
                   "rejection reason %d on that same socket, expected SRT_REJ_TIMEOUT (%d); the "
                   "previous milestone reports SRT_REJ_UNKNOWN (%d) here, and "
                   "TEST_INTERFACE.md section 5's point is that \"nobody answered\" and "
                   "\"someone said no\" are now "
                   "distinguishable",
                   requested, t.reason, AC_TIMEOUT, AC_UNKNOWN);

    ST_REQUIRE_MSG(t.ms >= (long long)requested - AC_TIMEO_EARLY,
                   "srt_connect reported the timeout after %lld ms with SRTO_CONNTIMEO at %d ms; "
                   "it cannot have waited for the configured timeout",
                   t.ms, requested);
}

SRT_CASE(g07_connect_timeout, a_dead_port_reports_the_timeout_reason) {
    // Two attempts at two configured timeouts, because a single attempt cannot
    // tell "the option governed the wait" from "the library waited its own
    // fixed interval, which happens to be near the value I picked". That
    // discrimination is exactly what interface.md §5 asks for: "a hard-coded
    // internal minimum that ignores the option fails it".
    //
    // Measured against the shipped v1.4.2 prefix: the caller re-sends its
    // induction request on a one-second tick and only notices the deadline on a
    // tick boundary, so the wait is the configured value rounded up to the next
    // whole second, with a one-second floor -- 250 -> 1001 ms, 800 -> 1001 ms,
    // 1000 -> 1001 ms, 1500 -> 2005 ms, 2500 -> 3001 ms, 4000 -> 4001 ms, each
    // within a couple of ms over repeats. Both values below therefore sit on a
    // tick boundary, where the release lands within a few ms of the configured
    // figure, and neither of them is the library's own 3000 ms default, so
    // "the option was never applied" cannot pass either leg.
    const int SHORT_MS = 1000;
    const int LONG_MS = 2000;

    const AcTimeo shortleg = ac_dead_connect(SHORT_MS);
    ac_require_timeout_leg(shortleg, SHORT_MS);

    const AcTimeo longleg = ac_dead_connect(LONG_MS);
    ac_require_timeout_leg(longleg, LONG_MS);

    // And the option's effect, not just its neighbourhood: raising it must
    // lengthen the wait. This is the assertion that carries the group's
    // discrimination, and since the per-leg bounds above are lower bounds only
    // it is the ONLY one that catches a library ignoring SRTO_CONNTIMEO: a
    // fixed internal interval long enough to clear both lower bounds shows up
    // here, and only here, as no change between the legs. It is stated as a
    // difference between two measurements on purpose -- the difference is what
    // the option is claimed to control, and it is far less sensitive to host
    // load than either measurement is on its own.
    const long long span = longleg.ms - shortleg.ms;
    const long long want = (long long)LONG_MS - SHORT_MS;
    ST_REQUIRE_MSG(span >= want / 2,
                   "raising SRTO_CONNTIMEO from %d ms to %d ms changed the wait by %lld ms "
                   "(%lld ms, then %lld ms), less than half the %lld ms it was raised by; the "
                   "configured timeout must govern the wait, and an internal interval that "
                   "ignores the option shows up here as no change at all",
                   SHORT_MS, LONG_MS, span, shortleg.ms, longleg.ms, want);
}

SRT_CASE(g07_connect_timeout, the_same_configuration_connects_to_a_live_peer) {
    // The positive control. The caller is configured exactly as above and
    // connects through the relay to a listener that is up, so the timeout
    // reason in the case above is about the dead port and not about the socket.
    AcLink L;
    ac_run(L, st_live_lossless, [](SRTSOCKET c) {
        st_set_int(c, SRTO_CONNTIMEO, 1500);
        st_set_int(c, SRTO_RCVTIMEO, 4000);
    });
    ac_require_connected(L, "a live peer with the same SRTO_CONNTIMEO");
    // No bound on L.elapsed_ms here. "The connection was not reported as a
    // timeout" is asserted directly, and deterministically, two lines below:
    // the caller connected and its rejection reason reads SRT_REJ_UNKNOWN. A
    // wall-clock ceiling on top of that adds no discrimination -- a connect
    // that returned success did not time out however long it took -- and on a
    // loaded host it is the one assertion in this case that could fail for a
    // reason that has nothing to do with the library.
    srt_clearlasterror();
    const int reason = srt_getrejectreason(L.cal);
    ST_REQUIRE_MSG(reason == AC_UNKNOWN,
                   "a successfully connected caller reports rejection reason %d, expected "
                   "SRT_REJ_UNKNOWN (%d): nothing was rejected, so the timeout reason must not "
                   "be left behind on a live connection",
                   reason, AC_UNKNOWN);
}

// =========================================================================
// g08_entry_points   (new)   RQ-06-21, RQ-06-26, RQ-06-27
//
// scored.txt: "the installed library resolves by name, at run time, the four
// entry points that create a socket group, connect it, read its member data and
// report a socket's group, and resolves the renamed call that binds a socket to
// an existing system socket, while the previous milestone's name for that same
// call is no longer an exported symbol, and the access-control header naming
// the application rejection codes is installed alongside the main header".
//
// interface.md §5 states the scope exactly: "Nothing in this milestone asserts
// how a group *behaves* at run time; only that the entry points exist and
// resolve." So nothing here calls srt_connect_group or srt_group_data; the
// case requires the pointers to be non-NULL, and that is the whole claim.
// =========================================================================

SRT_CASE(g08_entry_points, the_four_group_entry_points_resolve) {
    // RQ-06-26. Resolved out of the loaded library, which is what makes this a
    // statement about *export* rather than about the header.
    void* h = ac_lib_handle();
    ST_REQUIRE_MSG(h != 0, "dlopen of the installed libsrt failed: %s", dlerror());

    const char* names[] = {"srt_create_group", "srt_connect_group", "srt_group_data",
                           "srt_groupof"};
    for (int i = 0; i < 4; i++) {
        void* p = ac_dlsym_in<void*>(h, names[i]);
        ST_REQUIRE_MSG(p != 0,
                       "dlsym(handle, \"%s\") returned NULL: the grouped-connection entry point "
                       "is not exported by the installed library; this milestone's build "
                       "configuration is required to export all four",
                       names[i]);
    }

    // And the enumerations the entry points are used with are in the same
    // build. These are compile-time names -- the case reaches them through the
    // driver's own translation unit, which is the point: a header that omits
    // them fails here as a build error naming the symbol.
    const int gtypes[] = {(int)SRT_GTYPE_UNDEFINED, (int)SRT_GTYPE_BROADCAST,
                          (int)SRT_GTYPE_BACKUP, (int)SRT_GTYPE_BALANCING,
                          (int)SRT_GTYPE_MULTICAST};
    const int gsts[] = {(int)SRT_GST_PENDING, (int)SRT_GST_IDLE, (int)SRT_GST_RUNNING,
                        (int)SRT_GST_BROKEN};
    // The types are distinct and ordered, and UNDEFINED is not one of the real
    // ones: asserted by shape rather than by number, because the documents do
    // not fix the numbers.
    for (int i = 1; i < 5; i++) {
        ST_REQUIRE_MSG(gtypes[i] != gtypes[0],
                       "SRT_GTYPE_* member %d has the same value as SRT_GTYPE_UNDEFINED (%d); "
                       "the undefined type must be distinguishable from a real one",
                       i, gtypes[0]);
    }
    for (int i = 1; i < 4; i++) {
        for (int j = 0; j < i; j++) {
            ST_REQUIRE_MSG(gsts[i] != gsts[j],
                           "two SRT_GST_* states share the value %d; the group state must be "
                           "readable apart from its neighbours",
                           gsts[i]);
        }
    }
    ST_REQUIRE_MSG((int)SRT_GTYPE_E_END > gtypes[0],
                   "SRT_GTYPE_E_END is %d, at or below SRT_GTYPE_UNDEFINED (%d); it must be one "
                   "past the last real group type",
                   (int)SRT_GTYPE_E_END, gtypes[0]);

    // The connectivity control for this case: a symbol that must exist proves
    // the library really loaded and dlsym really searched it, so a NULL above
    // is a missing export and not a failed dlopen.
    ST_REQUIRE_MSG(ac_dlsym_in<void*>(h, "srt_bind") != 0,
                   "dlsym(handle, \"srt_bind\") returned NULL, so the handle does not describe "
                   "the SRT library at all and the four NULLs above would prove nothing");
}

SRT_CASE(g08_entry_points, the_bind_call_was_renamed) {
    // RQ-06-27, which the mutation check measures as the *stronger* half of the
    // pair. The old name survives only as a static-inline forwarder in the
    // public header, so it is not an exported symbol -- but a static inline is
    // emitted as a weak, local, or out-of-line copy in whichever object file
    // calls it, and a weak out-of-line copy IS in .dynsym at some
    // configurations. So the case probes three spellings rather than one.
    //
    // The C name "srt_bind_peerof" is the real subject, and it is the third
    // probe that does the work: that is the name v1.4.0 exports and the name
    // that must be absent here. The two mangled spellings beside it are
    // belt-and-braces and are not expected to fire against any tree --
    // "bind2PeerSocket" appears nowhere in this project, in either direction
    // of the v1.4.0-to-v1.4.2 release patch, and the surviving forwarder is
    // `extern "C"`, so a C++ mangling of it would not arise in the first
    // place. They are kept because a probe that cannot fire cannot cause a
    // false failure and they cost nothing; do not read them as evidence that
    // this check is stronger than its third entry.
    void* h = ac_lib_handle();
    ST_REQUIRE_MSG(h != 0, "dlopen of the installed libsrt failed: %s", dlerror());

    void* newfn = ac_dlsym_in<void*>(h, "srt_bind_acquire");
    ST_REQUIRE_MSG(newfn != 0,
                   "dlsym(handle, \"srt_bind_acquire\") returned NULL: the replacement bind "
                   "call is not exported, so the deprecated header-only forwarder has nothing "
                   "to forward to");

    const char* oldnames[] = {"_ZN4CUDT15bind2PeerSocketEiPv",
                              "_ZN4CUDT15bind2PeerSocketEiPNS_6CUDTSocketE",
                              "srt_bind_peerof"};
    bool found_old = false;
    const char* found_name = "";
    for (int i = 0; i < 3; i++) {
        if (ac_dlsym_in<void*>(h, oldnames[i]) != 0) {
            found_old = true;
            found_name = oldnames[i];
            break;
        }
    }
    ST_REQUIRE_MSG(!found_old,
                   "the installed library still exports \"%s\": the previous milestone's name "
                   "for the bind-by-peer call must no longer be an exported symbol here -- the "
                   "two names are supposed to be one function with one exported spelling, "
                   "srt_bind_acquire, and TEST_INTERFACE.md section 5 requires a "
                   "run-time lookup of the old name to fail",
                   found_name);
}

SRT_CASE(g08_entry_points, the_access_control_header_is_installed_beside_the_main_one) {
    // RQ-06-21. The include at the top of this file proves it compiles; this
    // case proves it is *installed*, and installed as a second header beside
    // the main one rather than in place of it or in some other directory.
    //
    // The path is derived rather than hard-coded: the library is located
    // through its loaded object, and the header directory is the one the
    // install layout puts beside it.
    void* h = ac_lib_handle();
    ST_REQUIRE_MSG(h != 0, "dlopen of the installed libsrt failed: %s", dlerror());

    std::string libpath;
    {
        Dl_info info;
        void* bind = ac_dlsym_in<void*>(h, "srt_bind");
        ST_REQUIRE_MSG(bind != 0, "srt_bind is not resolvable, so the library's path cannot be "
                                  "derived");
        if (dladdr(bind, &info) && info.dli_fname) libpath = info.dli_fname;
    }
    ST_REQUIRE_MSG(!libpath.empty(),
                   "dladdr could not name the file the library was loaded from, so the install "
                   "layout cannot be inspected from here");

    // <prefix>/lib/libsrt.so.1.4.2 -> <prefix>/include/srt/
    const std::string::size_type slash = libpath.find_last_of('/');
    ST_REQUIRE_MSG(slash != std::string::npos, "the library path \"%s\" has no directory part",
                   libpath.c_str());
    const std::string libdir = libpath.substr(0, slash);
    const std::string incdir = libdir.substr(0, libdir.find_last_of('/')) + "/include/srt/";

    const std::string main_h = incdir + "srt.h";
    const std::string ac_h = incdir + "access_control.h";
    std::FILE* f = std::fopen(main_h.c_str(), "r");
    ST_REQUIRE_MSG(f != 0,
                   "the main public header is not installed at \"%s\", beside the library at "
                   "\"%s\"; the install layout is part of what every group depends on",
                   main_h.c_str(), libdir.c_str());
    std::fclose(f);
    f = std::fopen(ac_h.c_str(), "r");
    ST_REQUIRE_MSG(f != 0,
                   "the access-control header is not installed at \"%s\"; "
                   "TEST_INTERFACE.md section 5: this milestone installs it as a second "
                   "public header in the same directory as "
                   "the main one",
                   ac_h.c_str());
    std::fclose(f);

    // And it is the one this file compiled against: the same content carries
    // the code the cases above named. A stale or empty file in that slot would
    // otherwise satisfy "the file exists".
    std::string body;
    {
        std::FILE* f = std::fopen(ac_h.c_str(), "rb");
        ST_REQUIRE_MSG(f != 0, "the access-control header at \"%s\" could not be reopened",
                       ac_h.c_str());
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
        std::fclose(f);
    }
    ST_REQUIRE_MSG(body.find("SRT_REJX_FORBIDDEN") != std::string::npos,
                   "the installed access-control header at \"%s\" does not name "
                   "SRT_REJX_FORBIDDEN, so it is not the header that carries the catalogue this "
                   "milestone's hook codes come from",
                   ac_h.c_str());
    ST_REQUIRE_MSG(body.find("SRT_REJX_FALLBACK") != std::string::npos,
                   "the installed access-control header at \"%s\" does not name "
                   "SRT_REJX_FALLBACK (%d), the lowest catalogue code",
                   ac_h.c_str(), AC_PREDEFINED);
}
