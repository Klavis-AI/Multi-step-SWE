// Family "epoll", checkpoint 05 (SRT v1.4.0). Five scored groups, one subject
// each:
//
//   g06_epoll_empty   the empty-EID guard on srt_epoll_wait, and the
//                     SRT_EPOLL_ENABLE_EMPTY flag that reverses it.
//   g07_epoll_et      the SRT_EPOLL_ET bit may not be the whole event mask.
//   g08_epoll_update  srt_epoll_update_usock with a zero mask unsubscribes.
//   g09_epoll_uwait   srt_epoll_uwait: shape, count, and 0-on-timeout.
//   g10_api_removal   srt_perfmon is gone; srt_bstats survives.
//
// ---------------------------------------------------------------------------
// Why this file resolves three functions through dlsym instead of calling them
//
// srt_epoll_set, srt_epoll_uwait, SRT_EPOLL_EVENT, SRT_EPOLL_ET and
// SRT_EPOLL_ENABLE_EMPTY are all absent from <srt/srt.h> at v1.3.3, and
// srt_perfmon is absent at v1.4.0. Naming any of them directly would make the
// whole family fail to compile against a workspace that has not reached this
// milestone -- and drv/CMakeLists.txt attributes a family that does not build
// as `driver-did-not-compile`, which would cost a candidate all five groups
// for one missing function instead of the one group that measures it. So this
// file names none of them: the functions come from dlsym (which is also the
// only honest way to assert that a symbol is *exported*, not merely declared),
// and the three header constants are restated locally against the v1.4.0
// header. The file therefore compiles unchanged at v1.3.3 and at v1.4.0, and
// each group fails on its own at v1.3.3.
//
// ---------------------------------------------------------------------------
// Traceability. Every assertion below is stated in one of:
//   * instruction.md §1 -- the epoll enhancements and the srt_perfmon
//     removal;
//   * TEST_INTERFACE.md §5 -- the srt_epoll_set 0/1/1 return sequence, the
//     zero-mask removal, and the dlsym approach to proving the removal;
//   * srt-docs/API.md (upstream docs/API.md at v1.4.0), "SRT Usage" under the
//     epoll synopsis: the SRT_EPOLL_EVENT layout `{SRTSOCKET fd; int events;}`,
//     "Every item reports a single socket with all events as flags", and the
//     explicit contrast -- srt_epoll_wait "reports error of type SRT_ETIMEOUT
//     when no socket is ready as the timeout elapses (including 0). This
//     behavior is different in srt_epoll_uwait", against srt_epoll_uwait "When
//     the timeout is not -1, and no sockets are ready until the timeout time
//     passes, this function returns 0."
//
// ---------------------------------------------------------------------------
// Deliberately NOT asserted, with the reason:
//
//   * "srt_epoll_uwait on an empty EID with a non-negative timeout returns
//     zero rather than EINVPARAM", which an earlier draft of this
//     milestone's documents claimed. THIS IS NOT WHAT v1.4.0 DOES. CEPoll::uwait opens with the same
//     guard as CEPoll::wait -- `if (!ed.flags(SRT_EPOLL_ENABLE_EMPTY) &&
//     ed.watch_empty()) throw CUDTException(MJ_NOTSUP, MN_INVAL);` -- and the
//     released library was measured returning -1/SRT_EINVPARAM in 1 ms. The
//     true statement, and the one g09 asserts, is the one srt-docs/API.md
//     actually makes: with the empty EID *permitted* (ENABLE_EMPTY set), uwait
//     returns 0 where srt_epoll_wait raises SRT_ETIMEOUT. The documents were
//     corrected to that, and g09 also asserts the empty-EID guard itself --
//     both functions refuse an empty EID with EINVPARAM without the flag,
//     which is the half the mistaken claim had hidden.
//   * srt_epoll_set(eid, -1) as a query that does not modify. Real in the
//     source and documented (srt-docs/API-functions.md documents the -1 query
//     form), but asserting it here would pin the release's convenience path
//     rather than this milestone's requirement, so it is left unasserted.
//   * srt_epoll_uwait's fdsSize truncation. With two sockets ready and
//     fdsSize == 1 the released library fills one entry and returns 2; the
//     upstream document states the `fdsSize + 1` return, but it is incidental
//     to the call's job, so it is documented and deliberately not asserted.
//   * "code that called srt_perfmon at the previous milestone fails to link"
//     (g10's second clause). A link failure cannot be observed from inside an
//     already-linked process, and TEST_INTERFACE.md §5 says so explicitly:
//     "The groups
//     use dlsym to prove the absence, not a hardcoded link-time assertion,
//     because the harness must run the same binary against multiple
//     checkpoints." g10 therefore asserts the cause (the symbol is not
//     exported), which is what makes the link fail.
//
// No case here uses a wall-clock sleep for synchronisation. Every receive is
// bounded by SRTO_RCVTIMEO (st_recv_stream sets it); every epoll wait is
// bounded by its own timeout argument; the accept inside srtkit's Link is
// bounded too. The three timing assertions are one-sided and generous: "came
// back in well under the timeout it was given" and "did not come back before
// the timeout it was given".
#include "stest.h"
#include "srtkit.h"
#include "relay.h"

#include <dlfcn.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <type_traits>

// ------------------------------------------------------------------ locals
//
// These are local rather than added to drv/srtkit.h: srtkit.h must compile at
// every tag from v1.2.0 to v1.5.7, and none of this exists before v1.4.0.

// srt.h at v1.4.0:
//   typedef struct SRT_EPOLL_EVENT_STR { SRTSOCKET fd; int events; }
//       SRT_EPOLL_EVENT;
// restated here because the type is absent at v1.3.3. Two ints, in that order;
// srt-docs/API.md prints the same declaration.
struct StEpollEvent {
    SRTSOCKET fd;
    int       events;
};

// enum SRT_EPOLL_OPT { ... SRT_EPOLL_ET = 1u << 31 } at v1.4.0; absent at
// v1.3.3, where the same bit is simply an unrecognised flag.
static const int ST_EPOLL_ET = (int)(1u << 31);

// enum SRT_EPOLL_FLAGS { SRT_EPOLL_ENABLE_EMPTY = 1, ... } at v1.4.0.
static const int32_t ST_EPOLL_ENABLE_EMPTY = 1;

// Both restatements are pinned against the header that declares them, under
// the same nested guard fam_filter.cpp and fam_reject.cpp use for their own
// restatements.  Without the pin a tree that numbered either constant
// differently would fail g06, g07 and g09 on messages about flags and
// timeouts, never on the number that was actually wrong -- and
// TEST_INTERFACE.md section 5 promises the opposite, that a differently
// numbered constant "fails to compile rather than failing a behavioural
// assertion".
//
// The guard is nested on purpose and its floor is 1.4.0.  <srt/version.h>
// (and so SRT_VERSION_VALUE) exists from v1.3.0, so a lower floor would be
// satisfied at v1.3.3 and would fire the assert against names v1.3.3 does not
// have; and at v1.2.0 there is no <srt/version.h> at all, where a bare
// `#if SRT_VERSION_VALUE >= ...` is a preprocessor syntax error rather than a
// false condition, which is what the outer #ifdef is for.
//
// SRT_EPOLL_ET is compared through an unsigned cast: the enumerator is
// 1u << 31, which does not fit an int, and a direct comparison against a
// signed constant trips -Wsign-compare, which -Werror makes fatal.
//
// Its expected value is spelled as a decimal constant rather than as
// `(1u << 31)` so that tools/build-step.py can read the pair out of this
// static_assert and render it into the pin table in TEST_INTERFACE.md
// section 5.  That matters: a compile-time assertion on a number is only
// legitimate if some document the candidate reads fixes that number, and for
// SRT_EPOLL_ET nothing else does -- the upstream API documents describe
// edge-triggered semantics and never give the bit.  Written as a shift, the
// generator emits no row, and the family would refuse to build over a value
// the candidate was never told.
static const unsigned ST_EPOLL_ET_BIT = 2147483648;  // 1u << 31, 0x80000000

#ifdef SRT_VERSION_VALUE
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 4, 0)
static_assert(SRT_EPOLL_ENABLE_EMPTY == 1,
              "restated SRT_EPOLL_ENABLE_EMPTY disagrees with <srt/srt.h>: "
              "update the restatement in this file");
static_assert((unsigned)SRT_EPOLL_ET == ST_EPOLL_ET_BIT,
              "restated SRT_EPOLL_ET disagrees with <srt/srt.h>: "
              "update the restatement in this file");
#endif
#endif

// The live-mode payload every transfer case here uses. SRT's live congestion
// control refuses a receive whose buffer is smaller than the negotiated
// maximum payload (congctl.cpp: "buffer size: N is too small for the maximum
// possible 1316"), and st_recv_stream sizes its buffer from this argument, so
// anything smaller than the default SRTO_PAYLOADSIZE cannot be received at
// all. 1316 is that default, and the value every live-transport group in
// this task uses.
static const int ST_LIVE_PAYLOAD = 1316;

typedef int32_t (*st_epoll_set_fn)(int, int32_t);
typedef int (*st_epoll_uwait_fn)(int, StEpollEvent*, int, int64_t);

// Converting void* to a function pointer through memcpy rather than a cast:
// the cast is only conditionally supported in C++ and trips -Wpedantic, and
// this file is compiled with -Werror.
template <typename Fn>
static Fn st_dlsym(const char* name) {
    void* sym = dlsym(RTLD_DEFAULT, name);
    if (!sym) return 0;
    Fn fn;
    std::memcpy(&fn, &sym, sizeof fn);
    return fn;
}

static st_epoll_set_fn st_epoll_set_or_fail() {
    st_epoll_set_fn fn = st_dlsym<st_epoll_set_fn>("srt_epoll_set");
    ST_REQUIRE_MSG(fn != 0,
                   "srt_epoll_set is not exported by the library under test: "
                   "dlsym(RTLD_DEFAULT, \"srt_epoll_set\") returned NULL, expected a "
                   "definition (instruction.md §1, \"Epoll enhancements\")");
    return fn;
}

static st_epoll_uwait_fn st_epoll_uwait_or_fail() {
    st_epoll_uwait_fn fn = st_dlsym<st_epoll_uwait_fn>("srt_epoll_uwait");
    ST_REQUIRE_MSG(fn != 0,
                   "srt_epoll_uwait is not exported by the library under test: "
                   "dlsym(RTLD_DEFAULT, \"srt_epoll_uwait\") returned NULL, expected a "
                   "definition (instruction.md §1, \"Epoll enhancements\")");
    return fn;
}

static const char* st_errname(int e) {
    switch (e) {
    case SRT_SUCCESS:    return "SRT_SUCCESS";
    case SRT_EINVPARAM:  return "SRT_EINVPARAM";
    case SRT_ETIMEOUT:   return "SRT_ETIMEOUT";
    case SRT_EINVPOLLID: return "SRT_EINVPOLLID";
    case SRT_EINVSOCK:   return "SRT_EINVSOCK";
    case SRT_EINVOP:     return "SRT_EINVOP";
    case SRT_ECONNLOST:  return "SRT_ECONNLOST";
    case SRT_ENOCONN:    return "SRT_ENOCONN";
    default:             return "<other SRT_ERRNO>";
    }
}

static long long st_now_ms() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// What a wait call did: its return value, the SRT error it left behind when it
// failed, and how long it took. Grouping them keeps every failure message able
// to name all three.
struct StWaitOutcome {
    int       rc;
    int       err;   // only meaningful when rc < 0
    long long ms;
};

static StWaitOutcome st_do_wait(int eid, int64_t timeout_ms) {
    SRTSOCKET rd[16], wr[16];
    int rn = 16, wn = 16;
    srt_clearlasterror();
    StWaitOutcome o;
    long long t0 = st_now_ms();
    o.rc = srt_epoll_wait(eid, rd, &rn, wr, &wn, timeout_ms, NULL, NULL, NULL, NULL);
    o.ms = st_now_ms() - t0;
    o.err = o.rc < 0 ? srt_getlasterror(NULL) : SRT_SUCCESS;
    return o;
}

static StWaitOutcome st_do_uwait(st_epoll_uwait_fn fn, int eid, StEpollEvent* ev, int n,
                                 int64_t timeout_ms) {
    srt_clearlasterror();
    StWaitOutcome o;
    long long t0 = st_now_ms();
    o.rc = fn(eid, ev, n, timeout_ms);
    o.ms = st_now_ms() - t0;
    o.err = o.rc < 0 ? srt_getlasterror(NULL) : SRT_SUCCESS;
    return o;
}

// An EID that is released even when an assertion throws out of the case.
struct StEid {
    int id;
    StEid() : id(srt_epoll_create()) {
        ST_REQUIRE_MSG(id >= 0, "srt_epoll_create failed: %s", srt_getlasterror_str());
    }
    ~StEid() { srt_epoll_release(id); }
    StEid(const StEid&) = delete;
    StEid& operator=(const StEid&) = delete;
};

// A socket that is closed even when an assertion throws out of the case.
struct StSock {
    SRTSOCKET s;
    StSock() : s(st_new_socket()) {
        ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    }
    ~StSock() { srt_close(s); }
    StSock(const StSock&) = delete;
    StSock& operator=(const StSock&) = delete;
};

// ===========================================================================
// g06_epoll_empty
//
// TEST_INTERFACE.md §4, this group: "srt_epoll_wait on an empty EID with a
// non-negative timeout
// fails immediately with EINVPARAM, srt_epoll_set with SRT_EPOLL_ENABLE_EMPTY
// returns the previous flag value and makes the wait revert to ETIMEOUT, and
// clearing the flag restores the immediate EINVPARAM behavior."
//
// instruction.md §1: "srt_epoll_wait on an empty EID without
// SRT_EPOLL_ENABLE_EMPTY now
// fails immediately with SRT_EINVPARAM regardless of the timeout, whereas the
// previous milestone only rejected it when the timeout was negative".
// ===========================================================================

// The negative. Its positive control is in the same case: the identical call
// on an EID that has one socket in it must NOT be EINVPARAM, so a library that
// simply refused every srt_epoll_wait could not score this.
SRT_CASE(g06_epoll_empty, wait_on_empty_eid_rejects_regardless_of_timeout) {
    StEid ep;

    // A generous timeout, so that "fails immediately" is a claim about the
    // guard and not about the clock: v1.3.3 spins here for the full 2000 ms and
    // then reports ETIMEOUT.
    StWaitOutcome o = st_do_wait(ep.id, 2000);
    ST_REQUIRE_MSG(o.rc == SRT_ERROR,
                   "srt_epoll_wait on an empty EID (timeout 2000 ms): expected SRT_ERROR, "
                   "got rc=%d",
                   o.rc);
    ST_REQUIRE_MSG(o.err == SRT_EINVPARAM,
                   "srt_epoll_wait on an empty EID (timeout 2000 ms): expected "
                   "SRT_EINVPARAM (%d), got %s (%d)",
                   (int)SRT_EINVPARAM, st_errname(o.err), o.err);
    // No elapsed-time CEILING is asserted here, deliberately.  "Fails
    // immediately regardless of the timeout" is already carried in full by the
    // error CODE: the previous milestone waits out the 2000 ms and reports
    // SRT_ETIMEOUT, this one refuses with SRT_EINVPARAM, and the two verdicts
    // are what the assertions above read.  A `< 800 ms` clock assertion on top
    // of that adds no discrimination and one failure mode the library does not
    // own -- a scheduling stall on a loaded build host.  The FLOORS elsewhere
    // in this family stay, because load can only make a wait longer.

    // Timeout 0 is the other non-negative case the description covers.
    StWaitOutcome z = st_do_wait(ep.id, 0);
    ST_REQUIRE_MSG(z.rc == SRT_ERROR && z.err == SRT_EINVPARAM,
                   "srt_epoll_wait on an empty EID (timeout 0): expected SRT_ERROR with "
                   "SRT_EINVPARAM (%d), got rc=%d with %s (%d)",
                   (int)SRT_EINVPARAM, z.rc, st_errname(z.err), z.err);

    // Positive control: the EID itself is usable; only emptiness is refused.
    StSock s;
    int in = (int)SRT_EPOLL_IN;
    ST_REQUIRE_MSG(srt_epoll_add_usock(ep.id, s.s, &in) == 0,
                   "srt_epoll_add_usock(SRT_EPOLL_IN) failed: %s", srt_getlasterror_str());
    StWaitOutcome n = st_do_wait(ep.id, 300);
    ST_REQUIRE_MSG(!(n.rc == SRT_ERROR && n.err == SRT_EINVPARAM),
                   "srt_epoll_wait on a NON-empty EID still reported SRT_EINVPARAM (%d): "
                   "the guard is refusing every wait, not just the empty one",
                   (int)SRT_EINVPARAM);
}

// srt_epoll_set's return value is the subject here, not its side effect.
// TEST_INTERFACE.md §5: "Setting SRT_EPOLL_ENABLE_EMPTY on a fresh EID
// returns 0 (no
// flags were set before). Setting it again returns 1 (the flag was already
// set). Clearing it with srt_epoll_set(eid, 0) returns 1 (the flag was set
// before the clear)." The 0,1,1 sequence is what distinguishes a real
// implementation from one that always returns 0, always returns 1, or errors.
SRT_CASE(g06_epoll_empty, epoll_set_returns_the_previous_flag_value) {
    st_epoll_set_fn ep_set = st_epoll_set_or_fail();
    StEid ep;

    int32_t first = ep_set(ep.id, ST_EPOLL_ENABLE_EMPTY);
    ST_REQUIRE_MSG(first == 0,
                   "srt_epoll_set(SRT_EPOLL_ENABLE_EMPTY) on a fresh EID: expected the "
                   "previous flag value 0, got %d",
                   (int)first);

    int32_t second = ep_set(ep.id, ST_EPOLL_ENABLE_EMPTY);
    ST_REQUIRE_MSG(second == ST_EPOLL_ENABLE_EMPTY,
                   "srt_epoll_set(SRT_EPOLL_ENABLE_EMPTY) when it was already set: "
                   "expected the previous flag value %d, got %d",
                   (int)ST_EPOLL_ENABLE_EMPTY, (int)second);

    int32_t cleared = ep_set(ep.id, 0);
    ST_REQUIRE_MSG(cleared == ST_EPOLL_ENABLE_EMPTY,
                   "srt_epoll_set(0) clearing a set flag: expected the previous flag "
                   "value %d, got %d",
                   (int)ST_EPOLL_ENABLE_EMPTY, (int)cleared);

    int32_t after = ep_set(ep.id, 0);
    ST_REQUIRE_MSG(after == 0,
                   "srt_epoll_set(0) on an EID whose flags were already clear: expected "
                   "the previous flag value 0, got %d",
                   (int)after);
}

// The reversibility claim: the flag turns the immediate refusal back into the
// timeout-driven one, and clearing it turns it back again. Both halves are in
// one case because the claim is that they are the same switch.
SRT_CASE(g06_epoll_empty, enable_empty_turns_the_refusal_back_into_a_timeout) {
    st_epoll_set_fn ep_set = st_epoll_set_or_fail();
    StEid ep;

    ep_set(ep.id, ST_EPOLL_ENABLE_EMPTY);
    StWaitOutcome o = st_do_wait(ep.id, 400);
    ST_REQUIRE_MSG(o.rc == SRT_ERROR,
                   "srt_epoll_wait on an empty EID with SRT_EPOLL_ENABLE_EMPTY set: "
                   "expected SRT_ERROR (timed out), got rc=%d",
                   o.rc);
    ST_REQUIRE_MSG(o.err == SRT_ETIMEOUT,
                   "srt_epoll_wait on an empty EID with SRT_EPOLL_ENABLE_EMPTY set: "
                   "expected SRT_ETIMEOUT (%d), got %s (%d)",
                   (int)SRT_ETIMEOUT, st_errname(o.err), o.err);
    // One-sided: it must have actually waited out its 400 ms rather than
    // refusing under another name.
    ST_REQUIRE_MSG(o.ms >= 300,
                   "srt_epoll_wait with SRT_EPOLL_ENABLE_EMPTY returned SRT_ETIMEOUT "
                   "after only %lld ms of a 400 ms timeout: it did not wait",
                   o.ms);

    // And back: clearing the flag restores the immediate EINVPARAM.
    ep_set(ep.id, 0);
    StWaitOutcome b = st_do_wait(ep.id, 2000);
    ST_REQUIRE_MSG(b.rc == SRT_ERROR && b.err == SRT_EINVPARAM,
                   "srt_epoll_wait on an empty EID after clearing "
                   "SRT_EPOLL_ENABLE_EMPTY: expected SRT_ERROR with SRT_EINVPARAM (%d), "
                   "got rc=%d with %s (%d)",
                   (int)SRT_EINVPARAM, b.rc, st_errname(b.err), b.err);
    // No elapsed-time ceiling; the SRT_EINVPARAM above is the whole
    // discrimination.  See the note in the first case of this group.
}

// ===========================================================================
// g07_epoll_et
//
// TEST_INTERFACE.md §4, this group: "srt_epoll_add_usock and
// srt_epoll_update_usock with only the SRT_EPOLL_ET bit set are both rejected
// with EINVPARAM, while SRT_EPOLL_IN combined with SRT_EPOLL_ET is accepted
// by both."
//
// instruction.md §1: "srt_epoll_add_usock and srt_epoll_update_usock now
// reject an event mask containing only SRT_EPOLL_ET (the edge-triggered bit)
// with SRT_EINVPARAM, requiring at least one of IN, OUT, or ERR to accompany
// it."
// ===========================================================================

// The negative, proved twice over: the call fails, AND nothing was subscribed
// (the EID is still empty, which g06's rule makes observable).
SRT_CASE(g07_epoll_et, et_alone_is_rejected_and_subscribes_nothing) {
    StEid ep;
    StSock s;

    int et_only = ST_EPOLL_ET;
    srt_clearlasterror();
    int rc = srt_epoll_add_usock(ep.id, s.s, &et_only);
    int err = rc < 0 ? srt_getlasterror(NULL) : SRT_SUCCESS;
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "srt_epoll_add_usock with events = SRT_EPOLL_ET only (0x%08x): "
                   "expected SRT_ERROR, got rc=%d",
                   (unsigned)ST_EPOLL_ET, rc);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                   "srt_epoll_add_usock with events = SRT_EPOLL_ET only: expected "
                   "SRT_EINVPARAM (%d), got %s (%d)",
                   (int)SRT_EINVPARAM, st_errname(err), err);

    // The rejection has to mean the socket was not subscribed, not just that a
    // status code was returned. An empty EID is refused by srt_epoll_wait.
    StWaitOutcome o = st_do_wait(ep.id, 0);
    ST_REQUIRE_MSG(o.rc == SRT_ERROR && o.err == SRT_EINVPARAM,
                   "after a rejected ET-only srt_epoll_add_usock the EID should still be "
                   "empty, so srt_epoll_wait should give SRT_EINVPARAM (%d); got rc=%d "
                   "with %s (%d) -- the socket was subscribed anyway",
                   (int)SRT_EINVPARAM, o.rc, st_errname(o.err), o.err);
}

// The positive control the rule requires: the ET bit is not itself forbidden,
// only an ET-only mask is. Accepting is proved by the subscription landing,
// not just by rc == 0.
SRT_CASE(g07_epoll_et, in_combined_with_et_is_accepted) {
    StEid ep;
    StSock s;

    int in_et = (int)SRT_EPOLL_IN | ST_EPOLL_ET;
    srt_clearlasterror();
    int rc = srt_epoll_add_usock(ep.id, s.s, &in_et);
    ST_REQUIRE_MSG(rc == 0,
                   "srt_epoll_add_usock with events = SRT_EPOLL_IN | SRT_EPOLL_ET "
                   "(0x%08x): expected 0, got rc=%d (%s)",
                   (unsigned)in_et, rc, srt_getlasterror_str());

    StWaitOutcome o = st_do_wait(ep.id, 300);
    ST_REQUIRE_MSG(!(o.rc == SRT_ERROR && o.err == SRT_EINVPARAM),
                   "srt_epoll_add_usock(SRT_EPOLL_IN | SRT_EPOLL_ET) returned 0 but the "
                   "EID is still empty: srt_epoll_wait gave SRT_EINVPARAM (%d), so "
                   "nothing was actually subscribed",
                   (int)SRT_EINVPARAM);
}

// The other half of the same rule: srt_epoll_update_usock refuses an ET-only
// mask too. It is a separate entry point with its own validation, so a tree
// that guarded only the add path passes the two cases above and fails here.
// The socket is subscribed with a valid mask first, because "update" on an
// unsubscribed socket is a different error entirely and would not be a test
// of the ET rule.
SRT_CASE(g07_epoll_et, update_usock_also_rejects_an_et_only_mask) {
    StEid ep;
    StSock s;

    int in = (int)SRT_EPOLL_IN;
    ST_REQUIRE_MSG(srt_epoll_add_usock(ep.id, s.s, &in) == 0,
                   "srt_epoll_add_usock(SRT_EPOLL_IN) failed: %s", srt_getlasterror_str());

    int et_only = ST_EPOLL_ET;
    srt_clearlasterror();
    int rc = srt_epoll_update_usock(ep.id, s.s, &et_only);
    int err = rc < 0 ? srt_getlasterror(NULL) : SRT_SUCCESS;
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "srt_epoll_update_usock with events = SRT_EPOLL_ET only (0x%08x) on a "
                   "socket already subscribed with SRT_EPOLL_IN: expected SRT_ERROR, got "
                   "rc=%d -- the ET-only rule applies to update_usock as well as to "
                   "add_usock",
                   (unsigned)ST_EPOLL_ET, rc);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                   "srt_epoll_update_usock with events = SRT_EPOLL_ET only: expected "
                   "SRT_EINVPARAM (%d), got %s (%d)",
                   (int)SRT_EINVPARAM, st_errname(err), err);

    // The refused update left the existing subscription alone: the EID is not
    // empty, so srt_epoll_wait does not hit the empty-EID guard. A refusal
    // that silently unsubscribed would be a different, worse bug than one
    // that silently accepted.
    StWaitOutcome o = st_do_wait(ep.id, 300);
    ST_REQUIRE_MSG(!(o.rc == SRT_ERROR && o.err == SRT_EINVPARAM),
                   "after a rejected ET-only srt_epoll_update_usock the EID reports "
                   "SRT_EINVPARAM (%d) on srt_epoll_wait, so the refused update dropped "
                   "the subscription it was refusing to change",
                   (int)SRT_EINVPARAM);

    // The positive control, mirroring the add path: IN | ET through the same
    // entry point is accepted.
    int in_et = (int)SRT_EPOLL_IN | ST_EPOLL_ET;
    srt_clearlasterror();
    ST_REQUIRE_MSG(srt_epoll_update_usock(ep.id, s.s, &in_et) == 0,
                   "srt_epoll_update_usock with events = SRT_EPOLL_IN | SRT_EPOLL_ET "
                   "(0x%08x): expected 0, got %s",
                   (unsigned)in_et, srt_getlasterror_str());
}

// ===========================================================================
// g08_epoll_update
//
// TEST_INTERFACE.md §4, this group: "srt_epoll_update_usock with a zero
// event mask succeeds and
// removes the subscription, leaving the EID empty, so a subsequent wait
// without SRT_EPOLL_ENABLE_EMPTY set fails with EINVPARAM."
//
// TEST_INTERFACE.md §5: "The call returns 0, not an error. The socket is
// erased from
// all event sets, and if it was the last socket in the EID, the EID becomes
// empty."
// ===========================================================================

SRT_CASE(g08_epoll_update, zero_mask_update_succeeds_and_empties_the_eid) {
    StEid ep;
    StSock s;

    int all = (int)SRT_EPOLL_IN | (int)SRT_EPOLL_OUT | (int)SRT_EPOLL_ERR;
    ST_REQUIRE_MSG(srt_epoll_add_usock(ep.id, s.s, &all) == 0,
                   "srt_epoll_add_usock(IN|OUT|ERR) failed: %s", srt_getlasterror_str());

    // Baseline: the EID is not empty yet, so the wait below is measuring the
    // update and not the starting state.
    StWaitOutcome before = st_do_wait(ep.id, 300);
    ST_REQUIRE_MSG(!(before.rc == SRT_ERROR && before.err == SRT_EINVPARAM),
                   "the EID reported SRT_EINVPARAM (%d) while a socket was still "
                   "subscribed: the baseline for this case is wrong",
                   (int)SRT_EINVPARAM);

    int zero = 0;
    srt_clearlasterror();
    int rc = srt_epoll_update_usock(ep.id, s.s, &zero);
    ST_REQUIRE_MSG(rc == 0,
                   "srt_epoll_update_usock with a zero event mask: expected 0 "
                   "(it succeeds and unsubscribes), got rc=%d (%s)",
                   rc, srt_getlasterror_str());

    StWaitOutcome after = st_do_wait(ep.id, 2000);
    ST_REQUIRE_MSG(after.rc == SRT_ERROR,
                   "after a zero-mask srt_epoll_update_usock the EID should be empty and "
                   "srt_epoll_wait should fail: got rc=%d",
                   after.rc);
    ST_REQUIRE_MSG(after.err == SRT_EINVPARAM,
                   "after a zero-mask srt_epoll_update_usock: expected srt_epoll_wait to "
                   "fail with SRT_EINVPARAM (%d) because the EID is now empty, got "
                   "%s (%d)",
                   (int)SRT_EINVPARAM, st_errname(after.err), after.err);
    // No elapsed-time ceiling: SRT_EINVPARAM is reported only for an EMPTY
    // EID, so the error code above already says the subscription was removed
    // rather than that the wait expired.  See the note in g06's first case.
}

// The positive control: an update is not a removal by itself. Without this a
// library whose srt_epoll_update_usock always unsubscribed would score the
// point.
SRT_CASE(g08_epoll_update, nonzero_mask_update_keeps_the_subscription) {
    StEid ep;
    StSock s;

    int all = (int)SRT_EPOLL_IN | (int)SRT_EPOLL_OUT | (int)SRT_EPOLL_ERR;
    ST_REQUIRE_MSG(srt_epoll_add_usock(ep.id, s.s, &all) == 0,
                   "srt_epoll_add_usock(IN|OUT|ERR) failed: %s", srt_getlasterror_str());

    int in = (int)SRT_EPOLL_IN;
    srt_clearlasterror();
    int rc = srt_epoll_update_usock(ep.id, s.s, &in);
    ST_REQUIRE_MSG(rc == 0,
                   "srt_epoll_update_usock narrowing IN|OUT|ERR to IN: expected 0, got "
                   "rc=%d (%s)",
                   rc, srt_getlasterror_str());

    StWaitOutcome o = st_do_wait(ep.id, 300);
    ST_REQUIRE_MSG(!(o.rc == SRT_ERROR && o.err == SRT_EINVPARAM),
                   "after a NON-zero srt_epoll_update_usock the socket should still be "
                   "subscribed, but srt_epoll_wait reported SRT_EINVPARAM (%d): the "
                   "update removed the subscription when it should only have narrowed it",
                   (int)SRT_EINVPARAM);
}

// ===========================================================================
// g09_epoll_uwait
//
// TEST_INTERFACE.md §4, this group: "srt_epoll_uwait exists and returns the
// count of ready sockets as SRT_EPOLL_EVENT structures with fd and events
// fields, it fails with EINVPARAM on an empty EID without
// SRT_EPOLL_ENABLE_EMPTY exactly as srt_epoll_wait does whatever the timeout,
// and on an EID carrying SRT_EPOLL_ENABLE_EMPTY with nothing ready it returns
// zero where srt_epoll_wait returns -1 with SRT_ETIMEOUT."
//
// An earlier draft of that sentence said uwait returns zero on an empty EID
// with a non-negative timeout. It does not; see the header comment. The two
// functions differ on *nothing ready*, not on *nothing subscribed*, and the
// cases below assert both halves of that.
// ===========================================================================

// Exists, and reports a socket that really is readable. The readiness is
// corroborated by then reading the message and comparing it, so a uwait that
// returned 1 unconditionally could not pass.
SRT_CASE(g09_epoll_uwait, reports_a_ready_socket_as_fd_and_events) {
    st_epoll_uwait_fn uwait = st_epoll_uwait_or_fail();

    Link L;
    RelaySched clean;
    st_connect(L, clean, st_live_lossless, st_live_lossless);

    StEid ep;
    int in = (int)SRT_EPOLL_IN;
    ST_REQUIRE_MSG(srt_epoll_add_usock(ep.id, L.acc, &in) == 0,
                   "srt_epoll_add_usock(receiver, SRT_EPOLL_IN) failed: %s",
                   srt_getlasterror_str());

    const int kSize = ST_LIVE_PAYLOAD;
    std::string msg = st_payload(0, kSize);
    int sent = srt_sendmsg(L.cal, msg.data(), (int)msg.size(), -1, 1);
    ST_REQUIRE_MSG(sent == (int)msg.size(), "srt_sendmsg returned %d, expected %d: %s",
                   sent, (int)msg.size(), srt_getlasterror_str());

    // Bounded by the uwait timeout, not by a sleep. The TSBPD latency that
    // st_live_lossless configures is 900 ms, so 8000 ms is generous.
    StEpollEvent ev[4];
    std::memset(ev, 0, sizeof ev);
    StWaitOutcome o = st_do_uwait(uwait, ep.id, ev, 4, 8000);
    ST_REQUIRE_MSG(o.rc == 1,
                   "srt_epoll_uwait after one message was sent: expected 1 ready socket, "
                   "got rc=%d (%s)",
                   o.rc, o.rc < 0 ? st_errname(o.err) : "no error");
    ST_REQUIRE_MSG(ev[0].fd == L.acc,
                   "srt_epoll_uwait filled SRT_EPOLL_EVENT.fd = %d, expected the "
                   "subscribed receiver socket %d",
                   (int)ev[0].fd, (int)L.acc);
    ST_REQUIRE_MSG((ev[0].events & (int)SRT_EPOLL_IN) != 0,
                   "srt_epoll_uwait filled SRT_EPOLL_EVENT.events = 0x%08x, expected the "
                   "SRT_EPOLL_IN bit (0x%08x) to be set",
                   (unsigned)ev[0].events, (unsigned)SRT_EPOLL_IN);

    // The readiness was real: the message is there and it is the one sent.
    std::vector<std::string> got = st_recv_stream(L.acc, 1, kSize, 3000);
    ST_REQUIRE_MSG(got.size() == 1,
                   "srt_epoll_uwait reported the receiver readable but %zu messages "
                   "could be read within 3000 ms, expected 1",
                   got.size());
    ST_REQUIRE_MSG(got[0] == msg,
                   "the message the readable socket delivered differs from the one sent "
                   "(%zu bytes received, %zu sent)",
                   got[0].size(), msg.size());
}

// The documented contrast, on a subscribed but idle EID: srt_epoll_uwait
// returns 0, srt_epoll_wait raises SRT_ETIMEOUT. srt-docs/API.md states both
// halves and says of each that "This behavior is different in" the other.
// srt_epoll_wait is the positive control: it proves the EID really had nothing
// ready, so uwait's 0 is a timeout and not a miscount.
SRT_CASE(g09_epoll_uwait, returns_zero_on_timeout_where_wait_raises_etimeout) {
    st_epoll_uwait_fn uwait = st_epoll_uwait_or_fail();

    Link L;
    RelaySched clean;
    st_connect(L, clean, st_live_lossless, st_live_lossless);

    StEid ep;
    int in = (int)SRT_EPOLL_IN;  // nothing is ever sent, so IN never fires
    ST_REQUIRE_MSG(srt_epoll_add_usock(ep.id, L.acc, &in) == 0,
                   "srt_epoll_add_usock(receiver, SRT_EPOLL_IN) failed: %s",
                   srt_getlasterror_str());

    StEpollEvent ev[4];
    std::memset(ev, 0, sizeof ev);
    StWaitOutcome u = st_do_uwait(uwait, ep.id, ev, 4, 400);
    ST_REQUIRE_MSG(u.rc == 0,
                   "srt_epoll_uwait with nothing ready and a 400 ms timeout: expected 0 "
                   "(srt-docs/API.md: \"this function returns 0\"), got rc=%d with %s "
                   "(%d)",
                   u.rc, st_errname(u.err), u.err);
    // One-sided: it must have waited, not returned 0 straight away.
    ST_REQUIRE_MSG(u.ms >= 300,
                   "srt_epoll_uwait returned 0 after only %lld ms of a 400 ms timeout: "
                   "it did not wait for the timeout to pass",
                   u.ms);

    StWaitOutcome w = st_do_wait(ep.id, 400);
    ST_REQUIRE_MSG(w.rc == SRT_ERROR && w.err == SRT_ETIMEOUT,
                   "srt_epoll_wait on the same idle EID: expected SRT_ERROR with "
                   "SRT_ETIMEOUT (%d) -- the behaviour srt_epoll_uwait differs from -- "
                   "got rc=%d with %s (%d)",
                   (int)SRT_ETIMEOUT, w.rc, st_errname(w.err), w.err);
}

// The same contrast on the OTHER EID shape, which is the one the documents
// actually enumerate: an EMPTY EID.
//
//   * without SRT_EPOLL_ENABLE_EMPTY, srt_epoll_uwait carries the identical
//     guard srt_epoll_wait does and fails with SRT_EINVPARAM whatever the
//     timeout -- the two functions do NOT differ on "nothing subscribed";
//   * with the flag set and still nothing ready, they do differ, and this is
//     where uwait's 0 and wait's SRT_ETIMEOUT sit side by side.
//
// The case above makes the same 0-vs-ETIMEOUT contrast on a subscribed idle
// EID. Both shapes are asserted because a library could easily get one right
// and the other wrong: the empty-EID guard and the nothing-ready return are
// different code paths.
SRT_CASE(g09_epoll_uwait, empty_eid_is_refused_until_enable_empty_then_returns_zero) {
    st_epoll_uwait_fn uwait = st_epoll_uwait_or_fail();
    st_epoll_set_fn ep_set = st_epoll_set_or_fail();
    StEid ep;

    StEpollEvent ev[4];
    std::memset(ev, 0, sizeof ev);

    // (1) Empty, no flag, generous timeout: refused, and refused immediately.
    StWaitOutcome u0 = st_do_uwait(uwait, ep.id, ev, 4, 2000);
    ST_REQUIRE_MSG(u0.rc == SRT_ERROR,
                   "srt_epoll_uwait on an empty EID without SRT_EPOLL_ENABLE_EMPTY "
                   "(timeout 2000 ms): expected SRT_ERROR, got rc=%d -- uwait carries "
                   "the same empty-EID guard as srt_epoll_wait and differs from it only "
                   "on *nothing ready*",
                   u0.rc);
    ST_REQUIRE_MSG(u0.err == SRT_EINVPARAM,
                   "srt_epoll_uwait on an empty EID without SRT_EPOLL_ENABLE_EMPTY: "
                   "expected SRT_EINVPARAM (%d), got %s (%d)",
                   (int)SRT_EINVPARAM, st_errname(u0.err), u0.err);
    // No elapsed-time ceiling; the SRT_EINVPARAM above carries it.  See the
    // note in g06's first case.

    // (2) Timeout 0 is the other non-negative case, and the one a library that
    // special-cased "timeout 0 means poll" would get wrong.
    std::memset(ev, 0, sizeof ev);
    StWaitOutcome uz = st_do_uwait(uwait, ep.id, ev, 4, 0);
    ST_REQUIRE_MSG(uz.rc == SRT_ERROR && uz.err == SRT_EINVPARAM,
                   "srt_epoll_uwait on an empty EID (timeout 0) without "
                   "SRT_EPOLL_ENABLE_EMPTY: expected SRT_ERROR with SRT_EINVPARAM (%d), "
                   "got rc=%d with %s (%d)",
                   (int)SRT_EINVPARAM, uz.rc, st_errname(uz.err), uz.err);

    // (3) With the flag, the same empty EID with nothing ready: uwait returns
    // 0 after waiting out the timeout.
    ep_set(ep.id, ST_EPOLL_ENABLE_EMPTY);
    std::memset(ev, 0, sizeof ev);
    StWaitOutcome u1 = st_do_uwait(uwait, ep.id, ev, 4, 400);
    ST_REQUIRE_MSG(u1.rc == 0,
                   "srt_epoll_uwait on an empty EID with SRT_EPOLL_ENABLE_EMPTY set and "
                   "nothing ready: expected 0, got rc=%d with %s (%d)",
                   u1.rc, st_errname(u1.err), u1.err);
    ST_REQUIRE_MSG(u1.ms >= 300,
                   "srt_epoll_uwait returned 0 after only %lld ms of a 400 ms timeout: "
                   "it did not wait for the timeout to pass, so the 0 is the refusal "
                   "under another name",
                   u1.ms);

    // (4) And srt_epoll_wait on the very same EID reports it the other way.
    // This is the pair the documents describe: same EID, same flag, same
    // emptiness, two different answers.
    StWaitOutcome w = st_do_wait(ep.id, 400);
    ST_REQUIRE_MSG(w.rc == SRT_ERROR && w.err == SRT_ETIMEOUT,
                   "srt_epoll_wait on the same empty SRT_EPOLL_ENABLE_EMPTY EID: "
                   "expected SRT_ERROR with SRT_ETIMEOUT (%d) where srt_epoll_uwait "
                   "returned 0; got rc=%d with %s (%d)",
                   (int)SRT_ETIMEOUT, w.rc, st_errname(w.err), w.err);
}

// "Returns the count of ready sockets": two of them, in one array, each with
// its own fd. Both ends of a live connection are writable as soon as it is up,
// so SRT_EPOLL_OUT gives two simultaneously ready sockets without depending on
// two deliveries landing in the same instant.
SRT_CASE(g09_epoll_uwait, counts_two_ready_sockets_in_one_array) {
    st_epoll_uwait_fn uwait = st_epoll_uwait_or_fail();

    Link L;
    RelaySched clean;
    st_connect(L, clean, st_live_lossless, st_live_lossless);

    StEid ep;
    int out = (int)SRT_EPOLL_OUT;
    ST_REQUIRE_MSG(srt_epoll_add_usock(ep.id, L.cal, &out) == 0,
                   "srt_epoll_add_usock(sender, SRT_EPOLL_OUT) failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_epoll_add_usock(ep.id, L.acc, &out) == 0,
                   "srt_epoll_add_usock(receiver, SRT_EPOLL_OUT) failed: %s",
                   srt_getlasterror_str());

    // Bounded retry rather than one shot: writability is set as each side
    // finishes connecting, and the two need not be flagged in the same
    // instant. Each round is bounded by its own timeout; the loop is bounded
    // by the round count. No sleep.
    StEpollEvent ev[8];
    int last_rc = -1;
    for (int round = 0; round < 4; round++) {
        std::memset(ev, 0, sizeof ev);
        StWaitOutcome o = st_do_uwait(uwait, ep.id, ev, 8, 1000);
        last_rc = o.rc;
        ST_REQUIRE_MSG(o.rc >= 0,
                       "srt_epoll_uwait on two writable sockets failed: rc=%d, %s (%d)",
                       o.rc, st_errname(o.err), o.err);
        if (o.rc == 2) break;
    }
    ST_REQUIRE_MSG(last_rc == 2,
                   "srt_epoll_uwait with both ends of a live connection subscribed for "
                   "SRT_EPOLL_OUT: expected a count of 2 ready sockets, got %d",
                   last_rc);

    std::set<SRTSOCKET> seen;
    for (int i = 0; i < 2; i++) {
        ST_REQUIRE_MSG((ev[i].events & (int)SRT_EPOLL_OUT) != 0,
                       "srt_epoll_uwait entry %d has events = 0x%08x, expected the "
                       "SRT_EPOLL_OUT bit (0x%08x) to be set",
                       i, (unsigned)ev[i].events, (unsigned)SRT_EPOLL_OUT);
        seen.insert(ev[i].fd);
    }
    ST_REQUIRE_MSG(seen.size() == 2,
                   "srt_epoll_uwait returned 2 entries but only %zu distinct fds: the "
                   "same socket was reported twice",
                   seen.size());
    ST_REQUIRE_MSG(seen.count(L.cal) == 1 && seen.count(L.acc) == 1,
                   "srt_epoll_uwait reported fds that are not the two subscribed sockets "
                   "(expected %d and %d, got %d and %d)",
                   (int)L.cal, (int)L.acc, (int)ev[0].fd, (int)ev[1].fd);
}

// ===========================================================================
// g10_api_removal
//
// TEST_INTERFACE.md §4, this group: "dlsym for srt_perfmon returns NULL
// while srt_bstats remains
// defined, and code that called srt_perfmon at the previous milestone fails to
// link."
//
// instruction.md §1: "srt_perfmon is deleted; the definition and the
// declaration are
// both gone, and code written against the previous milestone that calls it
// fails to link; srt_bstats remains and is the only performance-tracking
// function from this milestone forward". The link failure is asserted through
// its cause -- see the header comment.
// ===========================================================================

// The negative, with the controls that stop it being vacuous: dlsym must be
// shown to find a symbol that is present and to miss one that is not, in this
// same process, before its NULL for srt_perfmon means anything.
SRT_CASE(g10_api_removal, perfmon_is_not_exported_but_bstats_is) {
    // Control 1: dlsym finds what is there.
    void* bstats = dlsym(RTLD_DEFAULT, "srt_bstats");
    ST_REQUIRE_MSG(bstats != 0,
                   "dlsym(RTLD_DEFAULT, \"srt_bstats\") returned NULL: srt_bstats must "
                   "survive this milestone (instruction.md §1, \"Function "
                   "removal\")");
    void* epoll_create = dlsym(RTLD_DEFAULT, "srt_epoll_create");
    ST_REQUIRE_MSG(epoll_create != 0,
                   "dlsym(RTLD_DEFAULT, \"srt_epoll_create\") returned NULL: symbol "
                   "lookup is not working in this process, so a NULL for srt_perfmon "
                   "would prove nothing");

    // Control 2: dlsym misses what is not there, so NULL is informative.
    void* absent = dlsym(RTLD_DEFAULT, "srt_no_such_function_at_any_milestone");
    ST_REQUIRE_MSG(absent == 0,
                   "dlsym(RTLD_DEFAULT, ...) returned %p for a name that exists at no "
                   "milestone: lookup is not discriminating",
                   absent);

    // The subject.
    void* perfmon = dlsym(RTLD_DEFAULT, "srt_perfmon");
    ST_REQUIRE_MSG(perfmon == 0,
                   "dlsym(RTLD_DEFAULT, \"srt_perfmon\") returned %p, expected NULL: "
                   "srt_perfmon is deleted at this milestone, and while it is still "
                   "exported, code from the previous milestone that calls it still links",
                   perfmon);

    // The other half of the same requirement, and it can only be asserted at
    // compile time: "SRT_TRACEINFO and CPerfMon survive as type aliases".
    // Removing the FUNCTION is not licence to remove the TYPE it took -- both
    // <srt/srt.h> at the previous milestone and at this one declare
    //
    //     typedef struct CPerfMon SRT_TRACEINFO;
    //
    // and CPerfMon is never defined, so the alias can be named but not sized
    // or dereferenced.  std::is_same is therefore the whole assertion: it is
    // true exactly when SRT_TRACEINFO still names struct CPerfMon, it needs no
    // complete type, and a header that dropped either name, or retyped the
    // alias onto something else, stops this family from building -- which is
    // the failure the requirement describes.  This family carries only
    // peripheral groups and no gate, which is what makes a compile-time
    // assertion affordable here.
    static_assert(std::is_same<SRT_TRACEINFO, CPerfMon>::value,
                  "SRT_TRACEINFO must still be a typedef for struct CPerfMon: this "
                  "milestone deletes srt_perfmon, not the type aliases it used");
}

// The other half of the claim: srt_bstats is not merely exported, it still
// works. Without this the group would be satisfied by a library that exported
// an empty srt_bstats and deleted everything else.
SRT_CASE(g10_api_removal, bstats_still_reports_traffic) {
    Link L;
    RelaySched clean;
    st_connect(L, clean, st_live_lossless, st_live_lossless);

    const int kCount = 8;
    const int kSize = ST_LIVE_PAYLOAD;
    std::vector<std::string> sent = st_send_stream(L.cal, kCount, kSize, 2000);
    std::vector<std::string> got = st_recv_stream(L.acc, kCount, kSize, 4000);
    ST_REQUIRE_MSG((int)got.size() == kCount,
                   "%zu of %d messages arrived over an intact channel, expected all of "
                   "them",
                   got.size(), kCount);
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) == -1,
                   "messages arrived out of order or altered, first mismatch at index %d",
                   st_first_out_of_order(sent, got));

    SRT_TRACEBSTATS snd;
    std::memset(&snd, 0, sizeof snd);
    ST_REQUIRE_MSG(srt_bstats(L.cal, &snd, 0) == 0, "srt_bstats on the sender failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(snd.pktSentTotal >= kCount,
                   "srt_bstats on the sender reports pktSentTotal = %lld after %d "
                   "messages were sent, expected at least %d",
                   (long long)snd.pktSentTotal, kCount, kCount);

    SRT_TRACEBSTATS rcv;
    std::memset(&rcv, 0, sizeof rcv);
    ST_REQUIRE_MSG(srt_bstats(L.acc, &rcv, 0) == 0,
                   "srt_bstats on the receiver failed: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(rcv.pktRecvTotal >= kCount,
                   "srt_bstats on the receiver reports pktRecvTotal = %lld after %d "
                   "messages arrived, expected at least %d",
                   (long long)rcv.pktRecvTotal, kCount, kCount);
}
