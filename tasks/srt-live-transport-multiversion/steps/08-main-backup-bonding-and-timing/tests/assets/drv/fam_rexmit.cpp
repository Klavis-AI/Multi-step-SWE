// Step 08 (`v1.4.4`), family `rexmit` -- the retransmission-algorithm default
// and range, SRTO_TRANSTYPE's effect on it, and the exported clock identifier.
//
// Groups: g01_algo_default, g02_algo_range, g03_algo_transtype, g04_clock_type.
//
// Requirements: tmp/<task>/REQUIREMENTS.md §"Step 08" RQ-08-01 .. RQ-08-04.
// Every case below names the requirement it traces to, in the case body.
//
// This family is uniformly `new` (scored.txt: all four groups carry tier `new`;
// meta.env E2E_TIER=new prices the end-to-end group `g00_endtoend`, which belongs to
// the `live` family and is not registered here). A `new` group is only worth a
// point if it fails at v1.4.3, so this file is deliberately *not* contorted to
// build against that prefix: DRIVER_RULES' "restate the constant, dlsym the
// function" machinery buys nothing here and is not used for the option
// identifiers. A group that still compiled and passed at v1.4.3 would be a
// group that fails to discriminate, which is worse than a build failure. The
// acceptance test in the brief says the same thing.
//
// No socket is connected and no traffic is carried by this family. Three of
// the four groups need one socket object and nothing else; g04 needs neither a
// socket nor the library's own linking path, only the installed shared object.
//
// Every value and every behaviour asserted below was measured against both
// installed prefixes (dev/fam.sh 08 / dev/fam.sh 07 and the measurement driver
// in dev/scratch) before this file was written. What each prefix actually did
// is quoted in the case comments, because the *reason* a case discriminates is
// the measurement, not the delta table.

#include "srtkit.h"

#include <arpa/inet.h>
#include <dlfcn.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdio>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// The identifiers this file names, and what is pinned.
//
// `SRTO_RETRANSMITALGO = 61`, `SRTO_TRANSTYPE = 50`, `SRTT_LIVE = 2` and
// `SRTT_FILE = 1` are all written explicitly in the shipped `srt/srt.h` (the
// header spells the enumerators `SRTO_TRANSTYPE = 50` and
// `SRTO_RETRANSMITALGO = 61`, and `SRT_TRANSTYPE` spells both profiles with
// `= n`), so none of these numbers depends on where the enumerator sits.
// That matters here more than usual: this step's drivers are compiled with
// `-DENABLE_EXPERIMENTAL_BONDING=1` (meta.env DRV_DEFS), which the library is
// configured with as well, and the option enum carries `#if`-guarded members.
// An explicitly numbered enumerator is immune to that. Each is pinned below
// against the Oracle header so a wrong literal is a compile error rather than
// a case that silently addresses a different option.
//
// The clock identifiers are macros written explicitly in the header too, and
// are pinned the same way. The two literals used in g04 (1 and 4) are the
// documented identifiers, not restatements of a conditional value: the
// *build* is what selects between them, and g04 asserts which one the build
// selected. Nothing here restates a conditional value, so no #ifndef block is
// needed -- which is also why no `new` group in this file is reachable at
// v1.4.3 through a restatement it should not have.
// ---------------------------------------------------------------------------
static_assert(SRTO_RETRANSMITALGO == 61,
              "SRTO_RETRANSMITALGO is not 61 in <srt/srt.h>; re-derive g01-g03");
static_assert(SRTO_TRANSTYPE == 50,
              "SRTO_TRANSTYPE is not 50 in <srt/srt.h>; re-derive g03");

// `SRTT_LIVE` and `SRTT_FILE` are the one pair here that the header does *not*
// number: `enum SRT_TRANSTYPE { SRTT_LIVE, SRTT_FILE, SRTT_INVALID }` has no
// `= n` on either. That is exactly the case DRIVER_RULES warns about, so the
// numbers are *not* restated from memory -- they are derived here from the
// enumerators themselves, at the position where the switch reads them, and the
// two claims the file depends on are pinned relative to each other: the two
// profiles are distinct, and `SRTT_LIVE` is the one the live-profile case
// selects. A reordered enum fails the assert rather than silently selecting the
// other profile. (Their absolute values are irrelevant to every case below:
// nothing reads back SRTO_TRANSTYPE, and the library's own default is
// SRTT_LIVE at every tag in the chain.)
static_assert(SRTT_LIVE != SRTT_FILE, "SRTT_LIVE and SRTT_FILE are the same enumerator");
static_assert(SRTT_INVALID != SRTT_LIVE && SRTT_INVALID != SRTT_FILE,
              "SRT_TRANSTYPE's invalid member collides with a profile");
static_assert(SRT_SYNC_CLOCK_GETTIME_MONOTONIC == 1,
              "SRT_SYNC_CLOCK_GETTIME_MONOTONIC is not 1; re-derive g04");
static_assert(SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY == 4,
              "SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY is not 4; re-derive g04");

namespace {

const int kAlgoDefault = 1;             // RQ-08-01: iRetransmitAlgo(1) at v1.4.4.
const int kAlgoOff     = 0;
const int kEinvparam   = SRT_EINVPARAM;  // 5003, the code the new range check throws.
const int kMonotonic   = 1;              // SRT_SYNC_CLOCK_GETTIME_MONOTONIC.
const int kGetTimeOfDay = 4;             // SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY.

// A failure message that prints "expected 5003, got 0" makes the reviewer open
// the header; one that prints the name does not. Only the codes these groups
// can actually reach are named here.
const char* st_errname(int e) {
    switch (e) {
    case SRT_SUCCESS:    return "SRT_SUCCESS";
    case SRT_EINVPARAM:  return "SRT_EINVPARAM";
    case SRT_ECONNSOCK:  return "SRT_ECONNSOCK";
    case SRT_ESOCKFAIL:  return "SRT_ESOCKFAIL";
    case SRT_EINVSOCK:   return "SRT_EINVSOCK";
    default:             return "(unnamed)";
    }
}

std::string st_errs(int e) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d %s", e, st_errname(e));
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// The option accessors. `st_try_set_int` / `st_try_get_int` come from srtkit.h
// and are the non-throwing pair, which is what a group whose *subject* is a
// refusal needs: a throwing accessor would turn the refusal into a case
// failure with the wrong message. The throwing pair is used only where a
// failure genuinely is the case's own setup failing.
// ---------------------------------------------------------------------------

int algo_of(SRTSOCKET s) {
    int v = -999;
    if (st_try_get_int(s, SRTO_RETRANSMITALGO, &v) == SRT_ERROR)
        st_failf(__FILE__, __LINE__,
                 "setup: srt_getsockflag(SRTO_RETRANSMITALGO=%d) failed: %s",
                 (int)SRTO_RETRANSMITALGO, srt_getlasterror_str());
    return v;
}

// Binds the socket to IPv4 loopback on an autoselected port. A failure here is
// the case's own setup failing, not a verdict -- which is why it throws with a
// "setup:" prefix rather than reporting a verdict.
void bind_loopback(SRTSOCKET s) {
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (srt_bind(s, (sockaddr*)&a, sizeof a) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "setup: srt_bind(127.0.0.1:0) failed: %s",
                 srt_getlasterror_str());
}

// ---------------------------------------------------------------------------
// g04's subject: a symbol resolved out of the *installed shared library*.
//
// `scored.txt`'s driver note and RQ-08-04 both require the resolution to go to
// the candidate's installed `libsrt.so` rather than to RTLD_DEFAULT: the
// question the group asks is whether the candidate's build *exported* the
// symbol, and a symbol the driver happens to have resolved out of some other
// object would answer a different question. It is opened by the name the
// loader knows it by -- the SONAME the library's own link line uses -- so the
// driver does not bake in a versioned filename.
//
// Opening it and resolving by handle, rather than calling the function, is the
// whole point: a `static` definition, an inline-only header definition or a
// hidden-visibility symbol all compile and still resolve to nothing here.
// Nothing here declares `srt_clock_type`, so this family never depends on the
// candidate's header spelling it.
// ---------------------------------------------------------------------------
void* st_open_libsrt() {
    static const char* kNames[] = {"libsrt.so.1", "libsrt.so", "srt"};
    const char* tried = "";
    for (size_t i = 0; i < sizeof kNames / sizeof kNames[0]; i++) {
        void* h = dlopen(kNames[i], RTLD_NOW | RTLD_LOCAL);
        if (h) return h;
        tried = kNames[i];
    }
    st_failf(__FILE__, __LINE__,
             "could not open the installed shared library (tried libsrt.so.1, libsrt.so, srt; "
             "last attempt \"%s\"): %s -- g04 requires the installed libsrt.so to be loadable",
             tried, dlerror() ? dlerror() : "(no dlerror)");
    return nullptr;
}

void* st_dlsym_in(void* handle, const char* name) {
    dlerror();  // clear any stale error so the check below is about this call
    void* sym = dlsym(handle, name);
    return sym;
}

}  // namespace

// ---------------------------------------------------------------------------
// g01_algo_default -- RQ-08-01.
//
// `SRTO_RETRANSMITALGO` reads 1 on a freshly created socket and still reads 1
// on a socket that has been bound; a set of 0 before connect is accepted and
// reads back 0; a set of 1 reads back 1.
//
// The constructor default is the whole of the change (`iRetransmitAlgo(1)` at
// socketconfig.h:274, `(0)` at v1.4.3 socketconfig.h:282). The only phase
// restriction is `flags[SRTO_RETRANSMITALGO] = SRTO_R_PRE`, so bind does not
// disturb the stored value -- which is why the second case reads it either
// side of a bind.
//
// Measured: v1.4.4 fresh = 1, bound = 1; v1.4.3 fresh = 0, bound = 0.
// ---------------------------------------------------------------------------

// The default on a socket that has had no option set: the milestone's own
// change and nothing else. This is the case the retransmission default flip is
// scored on.
SRT_CASE(g01_algo_default, fresh_socket_reads_the_new_default) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());

    const int got = algo_of(s);
    ST_REQUIRE_MSG(got == kAlgoDefault,
                   "SRTO_RETRANSMITALGO on a freshly created socket reads %d, expected %d "
                   "(the efficient algorithm): the constructor default is still the intensive "
                   "one",
                   got, kAlgoDefault);

    srt_close(s);
}

// The same value on a socket that has been bound, which is the half of RQ-08-01
// that distinguishes "the default is right" from "the default survives the
// phase change". Measured both ways at both prefixes: v1.4.3 reads 0 on a
// fresh socket and 0 after bind, v1.4.4 reads 1 both times.
SRT_CASE(g01_algo_default, bound_socket_still_reads_it) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());

    const int before = algo_of(s);
    bind_loopback(s);
    const int after = algo_of(s);

    ST_REQUIRE_MSG(after == before,
                   "binding the socket changed SRTO_RETRANSMITALGO from %d to %d: the option is "
                   "SRTO_R_PRE and bind must not disturb the stored value",
                   before, after);
    ST_REQUIRE_MSG(after == kAlgoDefault,
                   "SRTO_RETRANSMITALGO on a bound, not yet connected socket reads %d, expected "
                   "%d (the efficient algorithm)",
                   after, kAlgoDefault);

    srt_close(s);
}

// A set of 0 before connect is accepted and reads back 0. The positive half of
// the pair, and the value v1.4.3's default happens to be -- so this case alone
// does not discriminate and is here as the control that the *default* cases
// above are about the default and not about the option being unwritable.
SRT_CASE(g01_algo_default, explicit_zero_is_stored) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());

    if (st_try_set_int(s, SRTO_RETRANSMITALGO, kAlgoOff) == SRT_ERROR) {
        st_failf(__FILE__, __LINE__,
                 "control: srt_setsockflag(SRTO_RETRANSMITALGO=%d, %d) failed: %s -- 0 is inside "
                 "the documented range [0, 1] and must be accepted before connect",
                 (int)SRTO_RETRANSMITALGO, kAlgoOff, srt_getlasterror_str());
    }

    const int got = algo_of(s);
    ST_REQUIRE_MSG(got == kAlgoOff,
                   "SRTO_RETRANSMITALGO was set to %d and reads back %d", kAlgoOff, got);

    srt_close(s);
}

// A set of 1 reads back 1 -- the other end of the range, and again a control:
// it proves the option is readable and writable, so the default cases are not
// passing because the option ignores every write.
SRT_CASE(g01_algo_default, explicit_one_reads_back) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());

    if (st_try_set_int(s, SRTO_RETRANSMITALGO, kAlgoDefault) == SRT_ERROR) {
        st_failf(__FILE__, __LINE__,
                 "control: srt_setsockflag(SRTO_RETRANSMITALGO=%d, %d) failed: %s -- 1 is inside "
                 "the documented range [0, 1] and must be accepted before connect",
                 (int)SRTO_RETRANSMITALGO, kAlgoDefault, srt_getlasterror_str());
    }

    const int got = algo_of(s);
    ST_REQUIRE_MSG(got == kAlgoDefault,
                   "SRTO_RETRANSMITALGO was set to %d and reads back %d", kAlgoDefault, got);

    srt_close(s);
}

// ---------------------------------------------------------------------------
// g02_algo_range -- RQ-08-02.
//
// A set of 2, of 3 or of -1 returns -1 with SRT_EINVPARAM and leaves the
// previously stored value unchanged, while 0 and 1 are both accepted.
//
// `CSrtConfigSetter<SRTO_RETRANSMITALGO>` (socketconfig.cpp:870-879 at v1.4.4)
// is new at this step: it range-checks `val < 0 || val > 1` and throws
// `CUDTException(MJ_NOTSUP, MN_INVAL, 0)` *before* the assignment, which is
// what makes a refused set inert. At v1.4.3 the option went through the
// generic path and stored whatever it was handed.
//
// Measured: v1.4.4 -- set 2/3/-1 all return -1 with 5003 and the stored value
// is untouched; v1.4.3 -- all three return 0 and the stored value becomes the
// rejected argument (set 2 then read 2, set -1 then read -1).
//
// The "leaves the previously stored value unchanged" half is the one a
// plausible wrong implementation fails: a range check that stores first and
// validates afterwards refuses the same values and fails only these cases.
// ---------------------------------------------------------------------------

// Sets the option to `store` (accepted), then attempts `reject` and requires
// both the refusal and the unchanged stored value. `store` is deliberately
// never the default, so "unchanged" cannot be satisfied by the option being
// reset to its default on every failed call.
void check_refused_set(SRTSOCKET s, int store, int reject, const char* label) {
    if (st_try_set_int(s, SRTO_RETRANSMITALGO, store) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "setup: setting SRTO_RETRANSMITALGO to %d failed: %s",
                 store, srt_getlasterror_str());

    const int stored = algo_of(s);
    ST_REQUIRE_MSG(stored == store,
                   "setup: SRTO_RETRANSMITALGO was set to %d and reads back %d", store, stored);

    const int rc = st_try_set_int(s, SRTO_RETRANSMITALGO, reject);
    const int err = srt_getlasterror(nullptr);

    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "srt_setsockflag(SRTO_RETRANSMITALGO=%d, %d) returned %d, expected -1: %s is "
                   "outside the documented range [0, 1] and must be refused",
                   (int)SRTO_RETRANSMITALGO, reject, rc, label);
    ST_REQUIRE_MSG(err == kEinvparam,
                   "srt_setsockflag(SRTO_RETRANSMITALGO=%d, %d) was refused with %s, expected %s "
                   "(%d)",
                   (int)SRTO_RETRANSMITALGO, reject, st_errs(err).c_str(),
                   st_errname(kEinvparam), kEinvparam);

    const int after = algo_of(s);
    ST_REQUIRE_MSG(after == store,
                   "a refused set of %d changed the stored SRTO_RETRANSMITALGO from %d to %d: "
                   "the range check must throw before the assignment, so a refused set is inert",
                   reject, store, after);
    ST_REQUIRE_MSG(after != reject,
                   "the stored SRTO_RETRANSMITALGO is %d after a refused set of %d: the rejected "
                   "argument was stored anyway (the range check runs after the assignment)",
                   after, reject);
}

SRT_CASE(g02_algo_range, value_two_is_refused_and_inert) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());
    check_refused_set(s, kAlgoOff, 2, "2");
    srt_close(s);
}

SRT_CASE(g02_algo_range, value_three_is_refused_and_inert) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());
    check_refused_set(s, kAlgoDefault, 3, "3");
    srt_close(s);
}

SRT_CASE(g02_algo_range, negative_one_is_refused_and_inert) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());
    check_refused_set(s, kAlgoDefault, -1, "-1");
    srt_close(s);
}

// The positive control the three refusals need: a tree that refused every value
// would take them for free. Both ends of the documented range must be accepted
// and must read back.
SRT_CASE(g02_algo_range, both_range_ends_are_accepted) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());

    if (st_try_set_int(s, SRTO_RETRANSMITALGO, kAlgoOff) == SRT_ERROR)
        st_failf(__FILE__, __LINE__,
                 "control: srt_setsockflag(SRTO_RETRANSMITALGO=%d, %d) failed: %s -- 0 is inside "
                 "the documented range [0, 1]",
                 (int)SRTO_RETRANSMITALGO, kAlgoOff, srt_getlasterror_str());
    int got = algo_of(s);
    ST_REQUIRE_MSG(got == kAlgoOff, "SRTO_RETRANSMITALGO set to %d reads back %d", kAlgoOff, got);

    if (st_try_set_int(s, SRTO_RETRANSMITALGO, kAlgoDefault) == SRT_ERROR)
        st_failf(__FILE__, __LINE__,
                 "control: srt_setsockflag(SRTO_RETRANSMITALGO=%d, %d) failed: %s -- 1 is inside "
                 "the documented range [0, 1]",
                 (int)SRTO_RETRANSMITALGO, kAlgoDefault, srt_getlasterror_str());
    got = algo_of(s);
    ST_REQUIRE_MSG(got == kAlgoDefault, "SRTO_RETRANSMITALGO set to %d reads back %d",
                   kAlgoDefault, got);

    // Back down again, so the case also covers a write that follows a write:
    // an implementation that only honours the first set would pass the two
    // checks above.
    if (st_try_set_int(s, SRTO_RETRANSMITALGO, kAlgoOff) == SRT_ERROR)
        st_failf(__FILE__, __LINE__,
                 "control: a second srt_setsockflag(SRTO_RETRANSMITALGO, %d) failed: %s",
                 kAlgoOff, srt_getlasterror_str());
    got = algo_of(s);
    ST_REQUIRE_MSG(got == kAlgoOff,
                   "SRTO_RETRANSMITALGO was set back to %d and reads %d: only the first set of "
                   "the session took effect",
                   kAlgoOff, got);

    srt_close(s);
}

// ---------------------------------------------------------------------------
// g03_algo_transtype -- RQ-08-03.
//
// Setting SRTO_TRANSTYPE to SRTT_LIVE leaves SRTO_RETRANSMITALGO reading 1 even
// when it was explicitly set to 0 beforehand, and setting it to SRTT_FILE
// leaves the option reading 0 even when it was explicitly set to 1 beforehand.
//
// `case SRTT_LIVE` gained `co.iRetransmitAlgo = 1;` and `case SRTT_FILE` gained
// `co.iRetransmitAlgo = 0;` (socketconfig.cpp:659 and :680 at v1.4.4). At
// v1.4.3 neither profile touched the field, so the option kept whatever it had
// been set to.
//
// Measured: v1.4.3 -- set 0 then SRTT_LIVE reads 0; set 1 then SRTT_FILE reads
// 1. v1.4.4 -- 1 and 0 respectively.
//
// SRTO_TRANSTYPE is SRTO_R_PREBIND, so all of this happens before bind; the
// cases below set it on an unbound socket, and the last one also after a bind,
// to show the profile write is not conditioned on the socket being unbound.
// ---------------------------------------------------------------------------

SRT_CASE(g03_algo_transtype, live_profile_overwrites_an_explicit_zero) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());

    if (st_try_set_int(s, SRTO_RETRANSMITALGO, kAlgoOff) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "setup: setting SRTO_RETRANSMITALGO to %d failed: %s",
                 kAlgoOff, srt_getlasterror_str());
    ST_REQUIRE_MSG(algo_of(s) == kAlgoOff,
                   "setup: SRTO_RETRANSMITALGO was set to %d but reads %d", kAlgoOff, algo_of(s));

    if (st_try_set_int(s, SRTO_TRANSTYPE, SRTT_LIVE) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "setup: srt_setsockflag(SRTO_TRANSTYPE, SRTT_LIVE) failed: %s",
                 srt_getlasterror_str());

    const int got = algo_of(s);
    ST_REQUIRE_MSG(got == kAlgoDefault,
                   "after SRTO_TRANSTYPE = SRTT_LIVE, SRTO_RETRANSMITALGO reads %d, expected %d: "
                   "the live profile must set the efficient algorithm even when the option was "
                   "explicitly set to %d beforehand",
                   got, kAlgoDefault, kAlgoOff);

    srt_close(s);
}

SRT_CASE(g03_algo_transtype, file_profile_overwrites_an_explicit_one) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());

    if (st_try_set_int(s, SRTO_RETRANSMITALGO, kAlgoDefault) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "setup: setting SRTO_RETRANSMITALGO to %d failed: %s",
                 kAlgoDefault, srt_getlasterror_str());
    ST_REQUIRE_MSG(algo_of(s) == kAlgoDefault,
                   "setup: SRTO_RETRANSMITALGO was set to %d but reads %d",
                   kAlgoDefault, algo_of(s));

    if (st_try_set_int(s, SRTO_TRANSTYPE, SRTT_FILE) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "setup: srt_setsockflag(SRTO_TRANSTYPE, SRTT_FILE) failed: %s",
                 srt_getlasterror_str());

    const int got = algo_of(s);
    ST_REQUIRE_MSG(got == kAlgoOff,
                   "after SRTO_TRANSTYPE = SRTT_FILE, SRTO_RETRANSMITALGO reads %d, expected %d: "
                   "the file profile must set the intensive algorithm even when the option was "
                   "explicitly set to %d beforehand",
                   got, kAlgoOff, kAlgoDefault);

    srt_close(s);
}

// The both-directions property in one case, in the order a candidate is most
// likely to have got wrong: an implementation that writes the live value only,
// or that writes each value only when the option has not been set explicitly,
// passes one of the two cases above and fails this one, because the second
// profile switch has to overwrite what the first one wrote.
SRT_CASE(g03_algo_transtype, both_directions_in_one_session) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());

    st_set_int(s, SRTO_TRANSTYPE, SRTT_LIVE);
    ST_REQUIRE_MSG(algo_of(s) == kAlgoDefault,
                   "after SRTO_TRANSTYPE = SRTT_LIVE (from the default), "
                   "SRTO_RETRANSMITALGO reads %d, expected %d",
                   algo_of(s), kAlgoDefault);

    st_set_int(s, SRTO_TRANSTYPE, SRTT_FILE);
    const int after_file = algo_of(s);
    ST_REQUIRE_MSG(after_file == kAlgoOff,
                   "after SRTO_TRANSTYPE = SRTT_FILE, SRTO_RETRANSMITALGO reads %d, expected %d "
                   "(it read %d under the live profile)",
                   after_file, kAlgoOff, kAlgoDefault);

    st_set_int(s, SRTO_TRANSTYPE, SRTT_LIVE);
    const int after_live = algo_of(s);
    ST_REQUIRE_MSG(after_live == kAlgoDefault,
                   "after switching back to SRTO_TRANSTYPE = SRTT_LIVE, "
                   "SRTO_RETRANSMITALGO reads %d, expected %d: the profile write happens once "
                   "and not on every switch",
                   after_live, kAlgoDefault);

    srt_close(s);
}

// A fourth shape, and the one that keeps the group honest about *when* the
// profile write happens. SRTO_TRANSTYPE is SRTO_R_PREBIND, so it can only be
// set before bind -- measured, not assumed: after a bind the library refuses it
// with "Cannot do this operation on a BOUND socket". What RQ-08-03 buys for a
// candidate that binds early is therefore not "the profile can be selected
// afterwards" (it cannot, and a case asserting it would fail the genuine
// release), but that the write the profile made *before* bind survives it: the
// value a later srt_getsockflag returns on the bound socket is the one the live
// profile wrote, not the 0 that was set on top of it. This is the assertion the
// earlier bound-socket case was trying to make, made the way the library
// actually allows it.
SRT_CASE(g03_algo_transtype, profile_write_survives_bind) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket failed: %s", srt_getlasterror_str());

    // Both options are SRTO_R_* phase options and both are set before bind.
    st_set_int(s, SRTO_RETRANSMITALGO, kAlgoOff);
    ST_REQUIRE_MSG(algo_of(s) == kAlgoOff,
                   "setup: SRTO_RETRANSMITALGO was set to %d but reads %d", kAlgoOff, algo_of(s));
    st_set_int(s, SRTO_TRANSTYPE, SRTT_LIVE);
    ST_REQUIRE_MSG(algo_of(s) == kAlgoDefault,
                   "setup: after SRTO_TRANSTYPE = SRTT_LIVE, SRTO_RETRANSMITALGO reads %d, "
                   "expected %d",
                   algo_of(s), kAlgoDefault);

    bind_loopback(s);

    const int got = algo_of(s);
    ST_REQUIRE_MSG(got == kAlgoDefault,
                   "SRTO_RETRANSMITALGO reads %d on the socket after bind, expected %d: the "
                   "value the live profile wrote before bind (over an explicit %d) must survive "
                   "the bind",
                   got, kAlgoDefault, kAlgoOff);

    srt_close(s);
}

// ---------------------------------------------------------------------------
// g04_clock_type -- RQ-08-04.
//
// The installed shared library exports `srt_clock_type` with C linkage and a
// call to it returns exactly 1, SRT_SYNC_CLOCK_GETTIME_MONOTONIC -- not the 4,
// SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY, the same call returned before this
// milestone.
//
// Resolution is by `dlsym` against the candidate's installed `libsrt.so`.
// Declaring `extern "C" int srt_clock_type(void);` and linking would test the
// wrong thing twice over: it would need the candidate's header to declare it
// (losing the whole family to a compile error if it does not, which the driver
// note at the head of scored.txt exists to prevent), and it would accept a
// symbol that is defined but not exported. A resolved symbol is an exported
// symbol; the second case makes that observable by resolving a name that
// certainly is not there and requiring the loader to report the lookup as a
// failure on the same handle.
//
// The build-configuration half of RQ-08-04 is not assertable from a driver and
// is not attempted here: a tree that hardcoded `return 1;` would pass these
// cases. What the group measures is the observable consequence, and the
// adjacent milestone asserting 4 from the same call is what keeps a hardcoded
// answer from scoring at one end of the chain or the other.
//
// Measured: v1.4.4 -- the symbol resolves and returns 1. v1.4.3 -- it resolves
// (the definition and the declaration are unchanged) and returns 4.
// ---------------------------------------------------------------------------

SRT_CASE(g04_clock_type, exported_symbol_reports_the_monotonic_clock) {
    void* h = st_open_libsrt();
    void* sym = st_dlsym_in(h, "srt_clock_type");

    ST_REQUIRE_MSG(sym != nullptr,
                   "the installed shared library does not export `srt_clock_type` (dlsym: %s): "
                   "it must be a C-linkage definition with default visibility, not a static, "
                   "inline-only or hidden symbol",
                   dlerror() ? dlerror() : "(no dlerror)");

    // The conversion through the declared function-pointer type is the one the
    // harness uses elsewhere: a cast from void* is only conditionally
    // supported, and this spelling says what the symbol is being called as.
    int (*clock_type)(void) = nullptr;
    static_assert(sizeof clock_type == sizeof sym,
                  "this platform's function pointers and object pointers differ in width; the "
                  "dlsym result cannot be called through a function pointer");
    std::memcpy(&clock_type, &sym, sizeof clock_type);

    const int got = clock_type();
    ST_REQUIRE_MSG(got == kMonotonic,
                   "srt_clock_type() returned %d, expected %d (SRT_SYNC_CLOCK_GETTIME_MONOTONIC): "
                   "the build must select the monotonic clock for itself, and %d "
                   "(SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY) is what this call returned before this "
                   "milestone",
                   got, kMonotonic, kGetTimeOfDay);
    ST_REQUIRE_MSG(got != kGetTimeOfDay,
                   "srt_clock_type() returned %d (SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY), the "
                   "identifier this call returned before this milestone",
                   got);
}

// The negative control the case above needs: it proves the lookup is a real
// lookup. Without it, a driver that resolved *any* symbol -- or a handle that
// answered every name -- would satisfy "the symbol is exported" for free, and
// the failure mode a `static` or hidden definition produces would never be
// distinguished from a successful resolve.
SRT_CASE(g04_clock_type, the_lookup_path_can_fail) {
    void* h = st_open_libsrt();

    void* missing = st_dlsym_in(h, "srt_clock_type_that_does_not_exist");
    ST_REQUIRE_MSG(missing == nullptr,
                   "dlsym on the installed library resolved `srt_clock_type_that_does_not_exist` "
                   "to %p: the lookup path does not discriminate, so the resolution asserted by "
                   "this group proves nothing",
                   missing);

    const char* err = dlerror();
    ST_REQUIRE_MSG(err != nullptr && err[0] != '\0',
                   "dlsym reported no error for a name that cannot exist: the loader's failure "
                   "path is not being observed");

    // And the real symbol still resolves on the same handle, so the failure
    // above is about the name and not about the handle being unusable.
    void* real = st_dlsym_in(h, "srt_clock_type");
    ST_REQUIRE_MSG(real != nullptr,
                   "after a failed lookup the same handle failed to resolve `srt_clock_type` "
                   "too: the handle was left unusable (dlsym: %s)",
                   dlerror() ? dlerror() : "(no dlerror)");
}
