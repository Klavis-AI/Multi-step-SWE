// Family "api", checkpoint 04 (SRT v1.3.3). Four scored groups:
//
//   g01_peeridletimeo  new         SRTO_PEERIDLETIMEO (slot 55): default,
//                                  pre-connect set, the late-set refusal, and
//                                  derivation by the accepted socket.
//   g02_ipv6only       new         SRTO_IPV6ONLY (slot 54): default, the three
//                                  documented values read back exactly, the
//                                  set that stays legal after bind and after
//                                  listen, and the connected-socket refusal.
//   g03_epoll_lwfds    new         srt_epoll_wait with only the writable
//                                  system-socket array supplied.
//   g04_header_macros  new         <srt/srt.h> no longer leaks NO_BUSY_WAITING
//                                  but still defines its own macros.
//
// --------------------------------------------------- what is NOT here
//
// Slot 47. It used to be, as g05_congestion; it is now its own family,
// fam_cong.cpp. It is the one group in the step that must name SRTO_SMOOTHER,
// the deprecated spelling, and that name admits no `#ifndef` fallback -- it is
// an enumerator at v1.3.2 and a `static const SRT_SOCKOPT` at v1.3.3, never a
// macro at either tag. A family is one translation unit, so while slot 47 sat
// here a tree that renamed it without keeping the alias failed to compile this
// file and lost the four groups above as well, for work it had done. The note
// at the top of fam_cong.cpp carries the full reasoning.
//
// ----------------------------------------------------------------- tiers
//
// All four groups here are `new`. A family is one .cpp compiled once, so this
// file must COMPILE AND RUN against the v1.3.2 header, where SRTO_IPV6ONLY and
// SRTO_PEERIDLETIMEO do not exist. Hence the restatements below. Without them
// the whole family would fail to build there and every group would score zero
// for a compile reason rather than a behavioural one.
//
// stepdefs/04/scope.md makes the numbering part of the contract: every option
// here is reached by its numeric position in SRT_SOCKOPT, never by a name the
// candidate may not have defined, so a tree missing one new name still
// compiles and still scores the other groups.
//
// ---------------------------------------------------------- traceability
//
// Every assertion comes from stepdefs/04/interface.md (which becomes
// TEST_INTERFACE.md section 5) and stepdefs/04/scope.md, and each was measured
// against the released v1.3.3 and v1.3.2 libraries before it was written.
//
// One document/release disagreement was found and has since been repaired:
// interface.md originally said slot 54 was refused AFTER BIND with
// SRT_EBOUNDSOCK. It is not. Measured on the release and confirmed at source:
// the setter's `case SRTO_IPV6ONLY` guards on the connected state only
// (MN_ISCONNECTED -> SRT_ECONNSOCK 5002) and there is no bound-state check, so
// a set after srt_bind and after srt_listen returns 0 and reads back. The
// documents now say that, and g02 asserts it in both directions: the post-bind
// and post-listen sets must SUCCEED, and the connected-socket set must fail
// with SRT_ECONNSOCK exactly.
//
// Deliberately NOT asserted, with reasons:
//   * values outside {-1,0,1} for slot 54. interface.md states explicitly that
//     the group "asserts nothing about values outside that range".
//   * the dual-stack two-listener scenario (deltas/04.md, "plausible"): it
//     depends on the host's IPv6 configuration and is not `certain`.
//   * SRTO_VERSION. deltas/04.md: trivially hardcodable, canary only.
//   * mbpsMaxBW / usPktSndPeriod (D6, D7): they belong to the `live` family.

// --------------------------------------------------------------------------
// g04's subject is a property of the INSTALLED HEADER, so it is captured here,
// where <srt/srt.h> is the only thing this translation unit has included. The
// three booleans below are the whole observation; everything after this point
// may include what it likes without changing them.
#include <srt/srt.h>

#ifdef NO_BUSY_WAITING
static const bool API_SRTH_LEAKS_NO_BUSY_WAITING = true;
#else
static const bool API_SRTH_LEAKS_NO_BUSY_WAITING = false;
#endif

#ifdef SRT_API
static const bool API_SRTH_DEFINES_SRT_API = true;
#else
static const bool API_SRTH_DEFINES_SRT_API = false;
#endif

#ifdef SRT_LOGFA_HAICRYPT
static const bool API_SRTH_DEFINES_LOGFA_HAICRYPT = true;
static const int  API_LOGFA_HAICRYPT_VALUE = (int)SRT_LOGFA_HAICRYPT;
#else
static const bool API_SRTH_DEFINES_LOGFA_HAICRYPT = false;
static const int  API_LOGFA_HAICRYPT_VALUE = -1;
#endif
// --------------------------------------------------------------------------

#include "stest.h"
#include "srtkit.h"
#include "relay.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

// ------------------------------------------------ option constants, restated
//
// DRIVER_RULES, "Naming an identifier the candidate may not have", rule 2.
// Both values are EXPLICIT in the sense that matters: v1.3.3's
// SRT_SOCKOPT enum contains no preprocessor conditional of any kind (the
// conditional members arrive with the bonding options at v1.4.2), the last
// explicit anchor is SRTO_SNDKMSTATE = 40, and counting forward from it gives
// RCVKMSTATE 41, LOSSMAXTTL 42, RCVLATENCY 43, PEERLATENCY 44, MINVERSION 45,
// STREAMID 46, CONGESTION 47, MESSAGEAPI 48, PAYLOADSIZE 49, TRANSTYPE 50,
// KMREFRESHRATE 51, KMPREANNOUNCE 52, STRICTENC 53, IPV6ONLY 54,
// PEERIDLETIMEO 55. stepdefs/04/scope.md states the same numbers as contract.
// The static_asserts below re-derive them from the header wherever the header
// has them, so the numbers are checked rather than assumed.
//
// The same block also pins two INHERITED anchors, SRTO_SNDKMSTATE = 40 and
// SRTO_STREAMID = 46. scope.md's numbering clause is not only "47, 54 and 55
// are these" but "everything below 47 keeps the value the previous milestone
// gave it", and without an anchor below 47 nothing checked the second half:
// a tree that renumbered the inherited enum and then placed the three new
// options at 47/54/55 would have satisfied every assertion in the step. Both
// anchors exist at v1.3.2 and at v1.3.3, so naming them asks for nothing this
// milestone introduces.
//
// The outer #ifdef matters even though this family's PREVTAG is v1.3.2, which
// does ship <srt/version.h>: a bare `#if SRT_VERSION_VALUE >= ...` against a
// tree with no version header is not quietly false but a preprocessor syntax
// error, which would cost the family all four groups for a build reason.
#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 3, 3)
static_assert(SRTO_SNDKMSTATE == 40, "inherited SRTO_SNDKMSTATE moved from slot 40");
static_assert(SRTO_STREAMID == 46, "inherited SRTO_STREAMID moved from slot 46");
static_assert(SRTO_IPV6ONLY == 54, "restated SRTO_IPV6ONLY disagrees with <srt/srt.h>");
static_assert(SRTO_PEERIDLETIMEO == 55, "restated SRTO_PEERIDLETIMEO disagrees with <srt/srt.h>");
#endif
#endif

// The restatements. These are NOT merely a v1.3.2 fallback -- read the
// measurement before changing them.
//
// SRTO_IPV6ONLY and SRTO_PEERIDLETIMEO are ENUMERATORS at v1.3.3 and absent
// altogether at v1.3.2; at neither tag is either one a macro. `#ifndef`
// therefore tests TRUE AT BOTH TAGS, the #define fires at both, and every
// run-time use of either name below is the literal slot number -- exactly what
// fam_sockopt.cpp does for the inherited positions, and what interface.md
// means by "every option a group exercises is reached by its numeric position
// at run time".
//
// Measured, so that it need not be argued again -- round 2's reviewer read the
// version guard above as leaving slots 54 and 55 unchecked on a tree that does
// not bump the version, and it does not:
//
//   probe at dev/root/04    (v1.3.3): neither name is a macro before this
//                                     block; after it both expand to 54 / 55
//   probe at dev/noopref/04 (v1.3.2): the same, and the family BUILDS there
//   fam_api at dev/noopref/04, version NOT bumped:
//                                     g01 EXECUTED 4 FAILED 4,
//                                     g02 EXECUTED 4 FAILED 4, every failure
//                                     message naming "slot 55" / "slot 54"
//
// So a tree that leaves the version at 1.3.2 is still charged on both slots.
// The version-guarded static_asserts above are an ADDITIONAL compile-time
// check of the header's own enumerator, not the only check of the numbering.
//
// Three things follow, and each would quietly un-assert something:
//   * Do not move this block above the static_asserts. The #define shadows the
//     enumerator, so after it `static_assert(SRTO_IPV6ONLY == 54)` reads
//     `54 == 54` and asserts nothing.
//   * Do not add an unconditional run-time `ST_REQUIRE(SRTO_IPV6ONLY == 54)`
//     beside these, for the same reason: below this block the name IS 54, so
//     the check is vacuous. This is the trap fam_cong.cpp's header describes
//     for SRTO_SMOOTHER, in the other direction.
//   * Do not replace the `#ifndef` lines with a plain positional constant.
//     tools/doc-coverage.py classifies a name `hard` -- rendered as
//     "required to compile" in TEST_INTERFACE.md section 5 -- precisely by the
//     ABSENCE of `#ifndef NAME`, and these two names are not required to
//     compile: they are referenced only inside the version guard.
#ifndef SRTO_IPV6ONLY
#define SRTO_IPV6ONLY ((SRT_SOCKOPT)54)
#endif
#ifndef SRTO_PEERIDLETIMEO
#define SRTO_PEERIDLETIMEO ((SRT_SOCKOPT)55)
#endif

// ----------------------------------------------------------------- constants

// The documented default of SRTO_PEERIDLETIMEO, in ms (core.h
// COMM_RESPONSE_TIMEOUT_MS = 5*1000; interface.md and the option table).
static const int API_PEERIDLE_DEFAULT_MS = 5000;

// A value that is not the default, so "read back what I set" and "derived from
// the listener" cannot be satisfied by returning the default.
static const int API_PEERIDLE_SET_MS = 7000;
static const int API_PEERIDLE_SET2_MS = 9000;

// The documented default of SRTO_IPV6ONLY: -1, the sentinel for "leave the
// system's own IPV6_V6ONLY default alone" (interface.md).
static const int API_IPV6ONLY_DEFAULT = -1;

// How long a blocked srt_epoll_wait is given before the case releases the eid
// out from under it. This is a deadline, not a synchronisation sleep: at
// v1.3.3 both epoll calls return in microseconds and the watchdog never fires,
// so nothing in a passing run depends on it. It exists so that a library which
// blocks where the milestone says it must not costs the group four seconds and
// an assertion naming the error, instead of the 60-second case deadline and an
// anonymous timeout. Measured: v1.3.2 blocks forever on the all-NULL call once
// anything is registered in the eid, because its deadlock guard has the
// negation on the wrong operand.
static const int API_EPOLL_WATCHDOG_MS = 4000;
static const int API_EPOLL_WATCHDOG_STEP_MS = 20;

// ----------------------------------------------------------------- helpers

// Releases `eid` if it is still armed after API_EPOLL_WATCHDOG_MS. See the
// constant's comment for why this is a deadline and not a sleep.
struct ApiEpollWatchdog {
    int eid;
    std::atomic<bool> disarmed;
    std::thread th;

    explicit ApiEpollWatchdog(int e) : eid(e), disarmed(false) {
        th = std::thread([this] {
            for (int waited = 0; waited < API_EPOLL_WATCHDOG_MS;
                 waited += API_EPOLL_WATCHDOG_STEP_MS) {
                if (disarmed.load()) return;
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(API_EPOLL_WATCHDOG_STEP_MS));
            }
            if (!disarmed.load()) srt_epoll_release(eid);
        });
    }
    ApiEpollWatchdog(const ApiEpollWatchdog&) = delete;
    ApiEpollWatchdog& operator=(const ApiEpollWatchdog&) = delete;
    void disarm() { disarmed.store(true); }
    ~ApiEpollWatchdog() { disarmed.store(true); if (th.joinable()) th.join(); }
};

// A system socket that is always ready for writing: an unconnected UDP socket
// has no send queue to fill, so EPOLLOUT is set the moment it is registered.
static int api_writable_ssock() {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    ST_REQUIRE_MSG(fd >= 0, "::socket(AF_INET, SOCK_DGRAM) failed: %s", std::strerror(errno));
    return fd;
}

// ===================================================== g01_peeridletimeo ===
//
// interface.md: "The option table states the 5000 ms default, so a fresh
// socket reading 5000 is documented ground."

SRT_CASE(g01_peeridletimeo, default_is_5000) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    int v = -12345;
    int rc = st_try_get_int(s, SRTO_PEERIDLETIMEO, &v);
    ST_REQUIRE_MSG(rc != SRT_ERROR,
                   "get of SRTO_PEERIDLETIMEO (slot 55) on a fresh socket: expected it to "
                   "succeed and read %d, got rc=%d, error %d (%s)",
                   API_PEERIDLE_DEFAULT_MS, rc, srt_getlasterror(NULL),
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(v == API_PEERIDLE_DEFAULT_MS,
                   "SRTO_PEERIDLETIMEO (slot 55) on a fresh socket: expected %d ms, got %d ms",
                   API_PEERIDLE_DEFAULT_MS, v);
    srt_close(s);
}

// interface.md: "A set before srt_bind and a set after bind but before connect
// both succeed and both read back."
SRT_CASE(g01_peeridletimeo, set_before_and_after_bind_reads_back) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    int rc = st_try_set_int(s, SRTO_PEERIDLETIMEO, API_PEERIDLE_SET_MS);
    ST_REQUIRE_MSG(rc != SRT_ERROR,
                   "set of SRTO_PEERIDLETIMEO (slot 55) to %d before srt_bind: expected 0, "
                   "got rc=%d, error %d (%s)",
                   API_PEERIDLE_SET_MS, rc, srt_getlasterror(NULL), srt_getlasterror_str());
    int v = -12345;
    ST_REQUIRE_MSG(st_try_get_int(s, SRTO_PEERIDLETIMEO, &v) != SRT_ERROR,
                   "get of SRTO_PEERIDLETIMEO after a pre-bind set failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(v == API_PEERIDLE_SET_MS,
                   "SRTO_PEERIDLETIMEO after a pre-bind set of %d: expected %d, got %d",
                   API_PEERIDLE_SET_MS, API_PEERIDLE_SET_MS, v);

    sockaddr_in la = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(s, (sockaddr*)&la, sizeof la) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());

    rc = st_try_set_int(s, SRTO_PEERIDLETIMEO, API_PEERIDLE_SET2_MS);
    ST_REQUIRE_MSG(rc != SRT_ERROR,
                   "set of SRTO_PEERIDLETIMEO (slot 55) to %d after srt_bind but before "
                   "connect: expected 0, got rc=%d, error %d (%s)",
                   API_PEERIDLE_SET2_MS, rc, srt_getlasterror(NULL), srt_getlasterror_str());
    v = -12345;
    ST_REQUIRE_MSG(st_try_get_int(s, SRTO_PEERIDLETIMEO, &v) != SRT_ERROR,
                   "get of SRTO_PEERIDLETIMEO after a post-bind set failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(v == API_PEERIDLE_SET2_MS,
                   "SRTO_PEERIDLETIMEO after a post-bind set of %d: expected %d, got %d",
                   API_PEERIDLE_SET2_MS, API_PEERIDLE_SET2_MS, v);
    srt_close(s);
}

// interface.md: "It is SRT_ECONNSOCK (5002) on a connected socket -- not
// SRT_EINVPARAM and not a silently ignored set that returns 0."
// The positive control is in the same case: the same value on an unconnected
// socket is accepted and reads back, so a library that refuses every set
// cannot earn this.
SRT_CASE(g01_peeridletimeo, late_set_refused_with_econnsock) {
    SRTSOCKET fresh = st_new_socket();
    ST_REQUIRE_MSG(fresh != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(st_try_set_int(fresh, SRTO_PEERIDLETIMEO, API_PEERIDLE_SET_MS) != SRT_ERROR,
                   "positive control: set of SRTO_PEERIDLETIMEO to %d on an UNCONNECTED "
                   "socket was refused with error %d (%s); expected 0",
                   API_PEERIDLE_SET_MS, srt_getlasterror(NULL), srt_getlasterror_str());
    int fv = -12345;
    st_try_get_int(fresh, SRTO_PEERIDLETIMEO, &fv);
    ST_REQUIRE_MSG(fv == API_PEERIDLE_SET_MS,
                   "positive control: SRTO_PEERIDLETIMEO on an unconnected socket after a "
                   "set of %d: expected %d, got %d",
                   API_PEERIDLE_SET_MS, API_PEERIDLE_SET_MS, fv);
    srt_close(fresh);

    Link L;
    RelaySched sched;
    st_connect(L, sched);

    int rc = st_try_set_int(L.cal, SRTO_PEERIDLETIMEO, API_PEERIDLE_SET2_MS);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "set of SRTO_PEERIDLETIMEO (slot 55) on a CONNECTED socket: expected -1, "
                   "got rc=%d (the set was not refused)",
                   rc);
    ST_REQUIRE_MSG(err == SRT_ECONNSOCK,
                   "set of SRTO_PEERIDLETIMEO on a CONNECTED socket: expected SRT_ECONNSOCK "
                   "(%d), got %d (%s)",
                   (int)SRT_ECONNSOCK, err, srt_strerror(err, 0));
}

// interface.md: "a listener set to 7000 before srt_bind must produce an
// accepted socket that reads 7000, not 5000. A tree that gives every accepted
// socket the default fails peeridletimeo."
// The caller socket in the same link is the control for the other direction: a
// tree that simply returns 7000 for everything fails on it.
SRT_CASE(g01_peeridletimeo, accepted_socket_derives_listener_value) {
    Link L;
    RelaySched sched;
    st_connect(L, sched,
               [](SRTSOCKET lsn) {
                   // Explicit rather than st_set_int, so that a tree without
                   // slot 55 fails here with actual-versus-expected text of
                   // this case's own rather than with srtkit's generic throw.
                   ST_REQUIRE_MSG(
                       st_try_set_int(lsn, SRTO_PEERIDLETIMEO,
                                      API_PEERIDLE_SET_MS) != SRT_ERROR,
                       "set of SRTO_PEERIDLETIMEO (slot 55) to %d on the LISTENER "
                       "before srt_bind: expected 0, got rc=-1, error %d (%s)",
                       API_PEERIDLE_SET_MS, srt_getlasterror(NULL),
                       srt_getlasterror_str());
               },
               st_noconf);

    int av = -12345;
    ST_REQUIRE_MSG(st_try_get_int(L.acc, SRTO_PEERIDLETIMEO, &av) != SRT_ERROR,
                   "get of SRTO_PEERIDLETIMEO on the ACCEPTED socket failed: rc=-1, "
                   "error %d (%s); expected it to read the listener's %d",
                   srt_getlasterror(NULL), srt_getlasterror_str(), API_PEERIDLE_SET_MS);
    ST_REQUIRE_MSG(av == API_PEERIDLE_SET_MS,
                   "accepted socket derived SRTO_PEERIDLETIMEO: expected the listener's %d, "
                   "got %d",
                   API_PEERIDLE_SET_MS, av);

    int cv = -12345;
    ST_REQUIRE_MSG(st_try_get_int(L.cal, SRTO_PEERIDLETIMEO, &cv) != SRT_ERROR,
                   "get of SRTO_PEERIDLETIMEO on the CALLER socket failed: error %d (%s)",
                   srt_getlasterror(NULL), srt_getlasterror_str());
    ST_REQUIRE_MSG(cv == API_PEERIDLE_DEFAULT_MS,
                   "the caller, whose option was never set, must still read the default: "
                   "expected %d, got %d (the derivation is from the listener, not global)",
                   API_PEERIDLE_DEFAULT_MS, cv);
}

// ========================================================== g02_ipv6only ===
//
// interface.md: "The number behind 'platform default' is -1 ... A tree that
// starts the option at 0 or 1 fails ipv6only."

SRT_CASE(g02_ipv6only, default_is_minus_one) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    int v = -12345;
    int rc = st_try_get_int(s, SRTO_IPV6ONLY, &v);
    ST_REQUIRE_MSG(rc != SRT_ERROR,
                   "get of SRTO_IPV6ONLY (slot 54) on a fresh socket: expected it to succeed "
                   "and read %d, got rc=%d, error %d (%s)",
                   API_IPV6ONLY_DEFAULT, rc, srt_getlasterror(NULL), srt_getlasterror_str());
    ST_REQUIRE_MSG(v == API_IPV6ONLY_DEFAULT,
                   "SRTO_IPV6ONLY (slot 54) on a fresh socket: expected %d (the "
                   "'leave the system default alone' sentinel), got %d",
                   API_IPV6ONLY_DEFAULT, v);
    srt_close(s);
}

// interface.md: "The group sets only -1, 0 and 1, requires each to be accepted
// before `srt_bind` and to read back exactly as given, and asserts nothing
// about values outside that range."
//
// A fresh socket per value, so no set can be satisfied by the residue of the
// one before it -- and each value is set on top of a DIFFERENT value that was
// itself read back first. Without that priming step the -1 iteration would be
// vacuous: -1 is also the default, so "set -1, read -1" on a fresh socket is
// satisfied by a setter that stores nothing at all.
SRT_CASE(g02_ipv6only, accepts_minus_one_zero_and_one_before_bind) {
    const int wanted[3] = {-1, 0, 1};
    for (int i = 0; i < 3; i++) {
        const int prime = (wanted[i] == 0) ? 1 : 0;   // != default, != wanted
        SRTSOCKET s = st_new_socket();
        ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

        ST_REQUIRE_MSG(st_try_set_int(s, SRTO_IPV6ONLY, prime) != SRT_ERROR,
                       "priming set of SRTO_IPV6ONLY (slot 54) to %d before srt_bind: "
                       "expected 0, got error %d (%s)",
                       prime, srt_getlasterror(NULL), srt_getlasterror_str());
        int primed = -12345;
        ST_REQUIRE_MSG(st_try_get_int(s, SRTO_IPV6ONLY, &primed) != SRT_ERROR,
                       "get of SRTO_IPV6ONLY after the priming set of %d failed: "
                       "error %d (%s)",
                       prime, srt_getlasterror(NULL), srt_getlasterror_str());
        ST_REQUIRE_MSG(primed == prime,
                       "SRTO_IPV6ONLY read back after the priming set of %d: expected %d, "
                       "got %d (the option is not storing what it is given)",
                       prime, prime, primed);

        int rc = st_try_set_int(s, SRTO_IPV6ONLY, wanted[i]);
        ST_REQUIRE_MSG(rc != SRT_ERROR,
                       "set of SRTO_IPV6ONLY (slot 54) to %d before srt_bind: expected 0, "
                       "got rc=%d, error %d (%s)",
                       wanted[i], rc, srt_getlasterror(NULL), srt_getlasterror_str());
        int got = -12345;
        ST_REQUIRE_MSG(st_try_get_int(s, SRTO_IPV6ONLY, &got) != SRT_ERROR,
                       "get of SRTO_IPV6ONLY after setting it to %d failed: error %d (%s)",
                       wanted[i], srt_getlasterror(NULL), srt_getlasterror_str());
        ST_REQUIRE_MSG(got == wanted[i],
                       "SRTO_IPV6ONLY read back after a pre-bind set of %d (over a stored "
                       "%d): expected %d, got %d",
                       wanted[i], prime, wanted[i], got);
        srt_close(s);
    }
}

// interface.md: "A set on a socket that has been bound, or bound and then put
// into listening, is accepted: it returns 0 and the value reads back. Do not
// add a bind-time guard to make the option look tidier -- the group sets the
// option after `srt_bind` and requires that set to succeed."
//
// This is the case that catches a candidate who guards the setter on the bound
// state instead of the connected one. It is a positive assertion, so it needs
// no separate control: a library that refuses every set fails it outright.
// Both states are covered because they are different states of the same guard
// -- srt_bind creates the underlying UDP socket, srt_listen additionally moves
// the socket to SRTS_LISTENING -- and a guard written against either one has
// to be caught.
SRT_CASE(g02_ipv6only, set_after_bind_and_after_listen_is_accepted) {
    struct Stage { const char* name; bool listening; int value; };
    const Stage stages[2] = {{"srt_bind", false, 1}, {"srt_bind + srt_listen", true, 0}};

    for (int i = 0; i < 2; i++) {
        SRTSOCKET s = st_new_socket();
        ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

        sockaddr_in a = st_loopback(0);
        ST_REQUIRE_MSG(srt_bind(s, (sockaddr*)&a, sizeof a) != SRT_ERROR,
                       "srt_bind on a loopback ephemeral port failed: %s",
                       srt_getlasterror_str());
        if (stages[i].listening)
            ST_REQUIRE_MSG(srt_listen(s, 1) != SRT_ERROR,
                           "srt_listen failed: %s", srt_getlasterror_str());

        // The state really is the one the case claims. Without this the case
        // would still pass on a tree where srt_bind silently did nothing.
        SRT_SOCKSTATUS st = srt_getsockstate(s);
        ST_REQUIRE_MSG(st == (stages[i].listening ? SRTS_LISTENING : SRTS_OPENED),
                       "precondition: after %s the socket state is %d, expected %d",
                       stages[i].name, (int)st,
                       (int)(stages[i].listening ? SRTS_LISTENING : SRTS_OPENED));

        int rc = st_try_set_int(s, SRTO_IPV6ONLY, stages[i].value);
        int err = srt_getlasterror(NULL);
        ST_REQUIRE_MSG(rc != SRT_ERROR,
                       "set of SRTO_IPV6ONLY (slot 54) to %d after %s: expected 0, got "
                       "rc=%d, error %d (%s). This option is guarded on the CONNECTED "
                       "state only; a bind-time guard is not part of this milestone",
                       stages[i].value, stages[i].name, rc, err, srt_strerror(err, 0));

        int got = -12345;
        ST_REQUIRE_MSG(st_try_get_int(s, SRTO_IPV6ONLY, &got) != SRT_ERROR,
                       "get of SRTO_IPV6ONLY after a set of %d following %s failed: "
                       "error %d (%s)",
                       stages[i].value, stages[i].name, srt_getlasterror(NULL),
                       srt_getlasterror_str());
        ST_REQUIRE_MSG(got == stages[i].value,
                       "SRTO_IPV6ONLY read back after a set of %d following %s: "
                       "expected %d, got %d",
                       stages[i].value, stages[i].name, stages[i].value, got);
        srt_close(s);
    }
}

// The negative, with its positive control in the same case.
//
// interface.md: "IPv6-only is refused only once the socket is connected, and
// the error is SRT_ECONNSOCK. ... it raises it only on a connected socket."
// Measured against the release and confirmed at source: `case SRTO_IPV6ONLY`
// in the setter tests the connected state and nothing else. The positive
// control is a set on an unbound socket in the same case, so a library that
// refuses every set cannot earn this.
SRT_CASE(g02_ipv6only, set_on_connected_socket_is_refused_with_econnsock) {
    SRTSOCKET fresh = st_new_socket();
    ST_REQUIRE_MSG(fresh != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(st_try_set_int(fresh, SRTO_IPV6ONLY, 1) != SRT_ERROR,
                   "positive control: set of SRTO_IPV6ONLY to 1 on an UNBOUND socket was "
                   "refused with error %d (%s); expected 0",
                   srt_getlasterror(NULL), srt_getlasterror_str());
    int fv = -12345;
    st_try_get_int(fresh, SRTO_IPV6ONLY, &fv);
    ST_REQUIRE_MSG(fv == 1,
                   "positive control: SRTO_IPV6ONLY on an unbound socket after a set of 1: "
                   "expected 1, got %d",
                   fv);
    srt_close(fresh);

    Link L;
    RelaySched sched;
    st_connect(L, sched);

    int rc = st_try_set_int(L.cal, SRTO_IPV6ONLY, 1);
    int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "set of SRTO_IPV6ONLY (slot 54) on a CONNECTED socket: expected -1 "
                   "(refused), got rc=%d and error %d (%s)",
                   rc, err, srt_strerror(err, 0));
    ST_REQUIRE_MSG(err == SRT_ECONNSOCK,
                   "set of SRTO_IPV6ONLY on a CONNECTED socket: expected SRT_ECONNSOCK "
                   "(%d), got %d (%s)",
                   (int)SRT_ECONNSOCK, err, srt_strerror(err, 0));
}

// ======================================================= g03_epoll_lwfds ===
//
// interface.md: "A call that supplies only the writable system-socket array is
// satisfiable, so it must proceed and return the registered ready system
// socket in that array."

SRT_CASE(g03_epoll_lwfds, lwfds_only_returns_the_ready_system_socket) {
    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int fd = api_writable_ssock();
    int events = SRT_EPOLL_OUT;
    ST_REQUIRE_MSG(srt_epoll_add_ssock(eid, fd, &events) != SRT_ERROR,
                   "srt_epoll_add_ssock(SRT_EPOLL_OUT): %s", srt_getlasterror_str());

    SYSSOCKET lw[4];
    for (int i = 0; i < 4; i++) lw[i] = -1;
    int lwnum = 4;
    int rc;
    int err;
    {
        ApiEpollWatchdog wd(eid);
        rc = srt_epoll_wait(eid, NULL, NULL, NULL, NULL, -1, NULL, NULL, lw, &lwnum);
        err = srt_getlasterror(NULL);
        wd.disarm();
    }

    ST_REQUIRE_MSG(rc == 1,
                   "srt_epoll_wait(eid, all NULL except lwfds, timeout -1) with one ready "
                   "system socket registered for SRT_EPOLL_OUT: expected 1, got %d "
                   "(error %d: %s)",
                   rc, err, srt_strerror(err, 0));
    ST_REQUIRE_MSG(lwnum == 1,
                   "srt_epoll_wait reported %d writable system sockets, expected 1", lwnum);
    ST_REQUIRE_MSG(lw[0] == fd,
                   "srt_epoll_wait returned system socket %d in lwfds, expected the "
                   "registered one, %d",
                   (int)lw[0], fd);

    srt_epoll_release(eid);
    ::close(fd);
}

// The other half, and the reason the first half cannot be earned by deleting
// the guard: the same eid, with the same socket registered, and EVERY array
// NULL, must still be refused with SRT_EINVPARAM. The socket stays registered
// deliberately -- with an empty eid the refusal comes from a different guard
// further in, and deleting the deadlock guard would go unnoticed.
SRT_CASE(g03_epoll_lwfds, every_array_null_is_still_refused) {
    int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());
    int fd = api_writable_ssock();
    int events = SRT_EPOLL_OUT;
    ST_REQUIRE_MSG(srt_epoll_add_ssock(eid, fd, &events) != SRT_ERROR,
                   "srt_epoll_add_ssock(SRT_EPOLL_OUT): %s", srt_getlasterror_str());

    int rc;
    int err;
    {
        ApiEpollWatchdog wd(eid);
        rc = srt_epoll_wait(eid, NULL, NULL, NULL, NULL, -1, NULL, NULL, NULL, NULL);
        err = srt_getlasterror(NULL);
        wd.disarm();
    }

    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "srt_epoll_wait(eid, every array NULL, timeout -1): expected -1, got %d "
                   "(the unsatisfiable call was not refused)",
                   rc);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                   "srt_epoll_wait(eid, every array NULL, timeout -1): expected "
                   "SRT_EINVPARAM (%d), got %d (%s)",
                   (int)SRT_EINVPARAM, err, srt_strerror(err, 0));

    srt_epoll_release(eid);
    ::close(fd);
}

// ===================================================== g04_header_macros ===
//
// interface.md: "The installed <srt/srt.h> must not define NO_BUSY_WAITING ...
// this milestone stops leaking it into every translation unit that includes
// the header."
//
// The observation is compile-time (deltas/04.md D9, "certain (compile-only)")
// and was taken at the top of this file, where <srt/srt.h> was the only
// include this translation unit had seen. Nothing between there and here
// defines or undefines either name: drv/stest.h, drv/srtkit.h and drv/relay.h
// pull in only <srt/srt.h> itself and system headers.

SRT_CASE(g04_header_macros, no_busy_waiting_is_not_defined) {
    ST_REQUIRE_MSG(!API_SRTH_LEAKS_NO_BUSY_WAITING,
                   "a translation unit whose only include is <srt/srt.h> has "
                   "NO_BUSY_WAITING defined: expected it to be undefined, got it defined. "
                   "It is a private build-configuration switch and must not reach a caller");
}

// The positive control, so that a header which is empty, truncated or replaced
// by a stub cannot pass the case above by accident.
SRT_CASE(g04_header_macros, header_still_defines_its_own_macros) {
    ST_REQUIRE_MSG(API_SRTH_DEFINES_SRT_API,
                   "positive control: SRT_API is not defined in a translation unit whose "
                   "only include is <srt/srt.h>: expected defined, got undefined");
    ST_REQUIRE_MSG(API_SRTH_DEFINES_LOGFA_HAICRYPT,
                   "positive control: SRT_LOGFA_HAICRYPT is not defined in a translation "
                   "unit whose only include is <srt/srt.h>: expected defined, got undefined");
    ST_REQUIRE_MSG(API_LOGFA_HAICRYPT_VALUE >= 0,
                   "positive control: SRT_LOGFA_HAICRYPT expands to %d, which is not a "
                   "usable logging functional-area id (expected >= 0)",
                   API_LOGFA_HAICRYPT_VALUE);
    // And that the macro really is a functional-area id the library accepts:
    // both entry points are declared SRT_API in the same header.
    srt_addlogfa(API_LOGFA_HAICRYPT_VALUE);
    srt_dellogfa(API_LOGFA_HAICRYPT_VALUE);
}
