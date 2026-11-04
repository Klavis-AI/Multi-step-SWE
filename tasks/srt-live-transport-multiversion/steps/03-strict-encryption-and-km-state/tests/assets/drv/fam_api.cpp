// Family "api", checkpoint 03 (SRT v1.3.2). Three scored groups, one subject
// each:
//
//   g05_snddropdelay  SRTO_SNDDROPDELAY: exists, reads back, and is the one
//                     transmission option that may also be set after connect.
//   g06_kmrefresh     SRTO_KMREFRESHRATE / SRTO_KMPREANNOUNCE: write-only,
//                     pre-connect only, and the KMPREANNOUNCE <= (R-1)/2
//                     relation.
//   g09_sockopt       the milestone-02 options (SRTO_STREAMID, SRTO_MINVERSION,
//                     SRTO_TRANSTYPE, SRTO_MESSAGEAPI, SRTO_PAYLOADSIZE) at
//                     their documented restriction level.
//
// Traceability. Every assertion is either in the step's own definition
// (stepdefs/03/scope.md and interface.md, which instruction.md §1 restates --
// docs/API.md at v1.3.2 documents none of SRTO_SNDDROPDELAY,
// SRTO_KMREFRESHRATE, SRTO_KMPREANNOUNCE), or in srt-docs/API.md's option
// table for the milestone-02 options: its "Binding" column (`pre` = the set is
// refused once connected), its "Default" column, its documented ranges, and
// the [GET or SET] / [SET] markers that say which options are readable.
//
// Deliberately NOT asserted, because nothing candidate-facing states them:
//   * the KMPREANNOUNCE ceiling that applies when KMREFRESHRATE was never set
//     (it derives from HAICRYPT_DEF_KM_REFRESH_RATE, an internal constant);
//   * the automatic clamp of KMPREANNOUNCE when KMREFRESHRATE is set -- the
//     option is write-only, so the clamped value is not observable; only the
//     ceiling that the new rate imposes on the NEXT set is, and that is what
//     g06 checks;
//   * SRTO_TRANSTYPE's side effect on SRTO_SNDDROPDELAY (SRTT_FILE sets it to
//     -1) -- real, but stated in no document this task ships;
//   * any timing consequence of SRTO_SNDDROPDELAY. It shifts a sender-side
//     too-late-drop threshold; the shift is not deterministically observable
//     through the public API, so this family asserts the option's contract,
//     not its effect.
//
// No case here sends payload, so none of them drains anything; the two groups
// that need a live socket bring one up through srtkit's Link (whose accept is
// bounded by SRTO_RCVSYN=0 and a poll, srt_accept honouring no timeout at this
// release) over a clean RelaySched and assert on socket state, never on
// elapsed time.
#include "stest.h"
#include "srtkit.h"
#include "relay.h"

#include <cstring>
#include <string>

// ----------------------------------------------- option constants, restated
//
// DRIVER_RULES, "Naming an identifier the candidate may not have": this family
// holds one inherited group (g09_sockopt), so it must compile against the
// PREVIOUS step's header, where these three enumerators do not exist yet.
// Naming them directly made g09 unearnable for a compile reason rather than a
// behavioural one. They are restated here, AFTER <srt/srt.h> has been pulled in
// by stest.h -- restating before the include would rewrite the enumerator in
// the header's own declaration. At v1.3.2 the macro shadows an enumerator of
// identical value; at v1.3.0 it supplies the number the library rejects, which
// is exactly the v1.3.0 behaviour g05 and g06 assert.
//
// Rule 2 asks for explicit values. SRTO_SNDDROPDELAY is explicit (= 32 in
// v1.3.2 srt.h). SRTO_KMREFRESHRATE and SRTO_KMPREANNOUNCE are not: they are
// implicit successors of the explicit SRTO_SNDKMSTATE = 40. What makes them
// safe to restate at this step -- and unsafe at the bonding steps 06-11 -- is
// that v1.3.2's SRT_SOCKOPT enum carries no preprocessor conditionals at all,
// so no build configuration can shift them. That is not taken on trust: the
// static_asserts re-derive all three from the header wherever the header has
// them, so a wrong number is a compile error, never a silently mis-addressed
// option.
//
// Why the pins live here even though this file also carries the GATE group
// g09_sockopt. fam_live.cpp states the opposite rule and follows it: a
// static_assert is a file-scope check, it fails the whole translation unit, and
// a gate group has to be unable to fail for any reason except a regression.
// That rule is departed from here, deliberately, and the two reasons it is safe
// to depart from it are the two that do not apply to fam_live.cpp's case.
// First, TEST_INTERFACE.md section 5 pins all four of this milestone's numbers
// explicitly -- in prose and in a generated table -- so the values are part of
// the stated interface rather than something to be guessed, and a conforming
// candidate cannot trip these asserts. Second, tests/test.sh builds one target
// per family, so a family that fails to compile costs its own groups and
// nothing else: g09_sockopt's veto cannot reach the groups in fam_kmx.cpp or
// fam_live.cpp. fam_kmx.cpp has the same shape -- a pin alongside the gate
// group g07_keylen32 -- for the same two reasons. What fam_live.cpp avoids is a
// narrower thing: a pin on SRTO_STRICTENC, a constant its gate group
// g08_recovery never names, which would have coupled loss recovery to a number
// that has nothing to do with it and is already pinned in fam_kmx.cpp. The two
// idioms are a deliberate split, not an oversight.
#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION_VALUE(1, 3, 2)
static_assert(SRTO_SNDDROPDELAY  == 32, "restated SRTO_SNDDROPDELAY disagrees with <srt/srt.h>");
static_assert(SRTO_KMREFRESHRATE == 51, "restated SRTO_KMREFRESHRATE disagrees with <srt/srt.h>");
static_assert(SRTO_KMPREANNOUNCE == 52, "restated SRTO_KMPREANNOUNCE disagrees with <srt/srt.h>");
#endif

#ifndef SRTO_SNDDROPDELAY
#define SRTO_SNDDROPDELAY  ((SRT_SOCKOPT)32)
#endif
#ifndef SRTO_KMREFRESHRATE
#define SRTO_KMREFRESHRATE ((SRT_SOCKOPT)51)
#endif
#ifndef SRTO_KMPREANNOUNCE
#define SRTO_KMPREANNOUNCE ((SRT_SOCKOPT)52)
#endif

// ---------------------------------------------------------------- helpers
//
// These four are local rather than added to drv/srtkit.h: srtkit.h has to
// compile at every tag from v1.2.0 to v1.5.7 and must not grow API surface for
// one family. st_errname and the must_pass/must_fail pair exist so that a
// failure line names the error symbolically instead of printing 5002.

static const char* st_errname(int e) {
    switch (e) {
    case SRT_SUCCESS:      return "SRT_SUCCESS";
    case SRT_ECONNREJ:     return "SRT_ECONNREJ";
    case SRT_ECONNLOST:    return "SRT_ECONNLOST";
    case SRT_ENOCONN:      return "SRT_ENOCONN";
    case SRT_EINVOP:       return "SRT_EINVOP";
    case SRT_EBOUNDSOCK:   return "SRT_EBOUNDSOCK";
    case SRT_ECONNSOCK:    return "SRT_ECONNSOCK";
    case SRT_EINVPARAM:    return "SRT_EINVPARAM";
    case SRT_EINVSOCK:     return "SRT_EINVSOCK";
    case SRT_EUNBOUNDSOCK: return "SRT_EUNBOUNDSOCK";
    default:               return "<other SRT_ERRNO>";
    }
}

static const char* st_statename(SRT_SOCKSTATUS s) {
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

// srtkit's st_get_int insists on a 4-byte result; SRTO_MESSAGEAPI is written
// by getOpt as a `bool` and reports one byte, which is also what the option
// table types it as. Checking the width is half of "the documented type".
static bool st_get_bool(SRTSOCKET s, SRT_SOCKOPT o) {
    bool v = false;
    int n = (int)sizeof v;
    if (srt_getsockflag(s, o, &v, &n) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "getsockflag(opt %d) failed: %s (%s)", (int)o,
                 srt_getlasterror_str(), st_errname(srt_getlasterror(NULL)));
    if (n != (int)sizeof(bool))
        st_failf(__FILE__, __LINE__, "getsockflag(opt %d) returned %d bytes, expected %zu for a bool",
                 (int)o, n, sizeof(bool));
    return v;
}

static std::string st_get_str(SRTSOCKET s, SRT_SOCKOPT o) {
    char buf[1024];
    std::memset(buf, 0, sizeof buf);
    int n = (int)sizeof buf;
    if (srt_getsockflag(s, o, buf, &n) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "getsockflag(opt %d) as a string failed: %s (%s)", (int)o,
                 srt_getlasterror_str(), st_errname(srt_getlasterror(NULL)));
    if (n < 0 || n >= (int)sizeof buf)
        st_failf(__FILE__, __LINE__, "getsockflag(opt %d) reported length %d, outside 0..%zu",
                 (int)o, n, sizeof buf - 1);
    return std::string(buf, (size_t)n);
}

static int st_try_set_str(SRTSOCKET s, SRT_SOCKOPT o, const std::string& v) {
    return srt_setsockflag(s, o, v.data(), (int)v.size());
}

static void st_set_must_pass(SRTSOCKET s, SRT_SOCKOPT o, int v, const char* what) {
    srt_clearlasterror();
    int rc = st_try_set_int(s, o, v);
    ST_REQUIRE_MSG(rc == 0,
                   "%s: srt_setsockflag(opt %d, %d) returned %d with %s (\"%s\"); expected 0",
                   what, (int)o, v, rc, st_errname(srt_getlasterror(NULL)),
                   srt_getlasterror_str());
}

static void st_set_must_fail(SRTSOCKET s, SRT_SOCKOPT o, int v, int want, const char* what) {
    srt_clearlasterror();
    int rc = st_try_set_int(s, o, v);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "%s: srt_setsockflag(opt %d, %d) returned %d (the set was accepted); "
                   "expected -1 with %s (%d)",
                   what, (int)o, v, rc, st_errname(want), want);
    int e = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(e == want,
                   "%s: srt_setsockflag(opt %d, %d) was refused with %s (%d, \"%s\"); "
                   "expected %s (%d)",
                   what, (int)o, v, st_errname(e), e, srt_getlasterror_str(),
                   st_errname(want), want);
}

static void st_get_must_fail(SRTSOCKET s, SRT_SOCKOPT o, int want, const char* what) {
    srt_clearlasterror();
    int v = -12345;
    int n = (int)sizeof v;
    int rc = srt_getsockflag(s, o, &v, &n);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "%s: srt_getsockflag(opt %d) returned %d and yielded %d in %d bytes "
                   "(the read was accepted); expected -1 with %s (%d)",
                   what, (int)o, rc, v, n, st_errname(want), want);
    int e = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(e == want,
                   "%s: srt_getsockflag(opt %d) was refused with %s (%d, \"%s\"); expected %s (%d)",
                   what, (int)o, st_errname(e), e, srt_getlasterror_str(), st_errname(want), want);
}

static void st_require_connected(SRTSOCKET s, const char* which) {
    SRT_SOCKSTATUS st = srt_getsockstate(s);
    ST_REQUIRE_MSG(st == SRTS_CONNECTED,
                   "%s socket is %s (%d), expected SRTS_CONNECTED (%d); the post-connect "
                   "assertions below mean nothing on a socket that is not connected",
                   which, st_statename(st), (int)st, (int)SRTS_CONNECTED);
}

static SRTSOCKET st_fresh_socket() {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    return s;
}

// =========================================================================
// g05_snddropdelay
//
// scored.txt: "`SRTO_SNDDROPDELAY` accepts values before and after connect,
// reads back the set value, and defaults to a documented value distinct from
// the absence of the option at v1.3.0".
//
// At v1.3.0 the option does not exist at all, so "distinct from its absence"
// is: srt_getsockflag succeeds and yields the documented default. The default
// is 0: both scope.md ("defaults to `0`") and interface.md
// ("`SRTO_SNDDROPDELAY` defaults to 0, and is settable after connect") state
// it, and -1 is the explicit off sentinel rather than the default, which is
// what the second case below sets and reads back.
// =========================================================================

SRT_CASE(g05_snddropdelay, default_is_readable_and_zero) {
    SRTSOCKET s = st_fresh_socket();

    int v = -12345;
    int n = (int)sizeof v;
    srt_clearlasterror();
    int rc = srt_getsockflag(s, SRTO_SNDDROPDELAY, &v, &n);
    ST_REQUIRE_MSG(rc == 0,
                   "srt_getsockflag(SRTO_SNDDROPDELAY) on a fresh socket returned %d with "
                   "%s (\"%s\"); at this milestone the option exists and is readable",
                   rc, st_errname(srt_getlasterror(NULL)), srt_getlasterror_str());
    ST_REQUIRE_MSG(n == (int)sizeof(int),
                   "srt_getsockflag(SRTO_SNDDROPDELAY) reported %d bytes, expected %zu (int)",
                   n, sizeof(int));
    ST_REQUIRE_MSG(v == 0, "SRTO_SNDDROPDELAY default is %d, documented as 0", v);

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g05_snddropdelay, reads_back_every_value_set_before_connect) {
    SRTSOCKET s = st_fresh_socket();

    // -1 is the documented "off" sentinel from the header comment; the rest
    // are ordinary millisecond values. The assertion is the round trip, not
    // any particular number.
    static const int vals[] = {-1, 0, 120, 1000, 5000};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        st_set_must_pass(s, SRTO_SNDDROPDELAY, vals[i], "pre-connect SRTO_SNDDROPDELAY");
        int got = st_get_int(s, SRTO_SNDDROPDELAY);
        ST_REQUIRE_MSG(got == vals[i],
                       "SRTO_SNDDROPDELAY read back %d after being set to %d", got, vals[i]);
    }

    // A refused value would leave the previous one in place; make sure the
    // last write is what is still there after all of them.
    int last = st_get_int(s, SRTO_SNDDROPDELAY);
    ST_REQUIRE_MSG(last == 5000, "SRTO_SNDDROPDELAY after the sequence is %d, expected 5000", last);

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g05_snddropdelay, survives_connect_and_is_settable_on_the_caller) {
    Link L;
    int pre_rc = 1;
    SockConf cconf = [&pre_rc](SRTSOCKET s) {
        pre_rc = st_try_set_int(s, SRTO_SNDDROPDELAY, 333);
    };
    st_connect(L, RelaySched(), st_noconf, cconf);

    ST_REQUIRE_MSG(pre_rc == 0,
                   "control: SRTO_SNDDROPDELAY=333 before connect returned %d, expected 0", pre_rc);
    st_require_connected(L.cal, "caller");

    int got = st_get_int(L.cal, SRTO_SNDDROPDELAY);
    ST_REQUIRE_MSG(got == 333,
                   "SRTO_SNDDROPDELAY across the handshake is %d, expected the 333 set before connect",
                   got);

    // The subject: this option, alone among the transmission options, accepts
    // a set on a connected socket.
    st_set_must_pass(L.cal, SRTO_SNDDROPDELAY, 750, "post-connect SRTO_SNDDROPDELAY on the caller");
    got = st_get_int(L.cal, SRTO_SNDDROPDELAY);
    ST_REQUIRE_MSG(got == 750, "SRTO_SNDDROPDELAY after the post-connect set is %d, expected 750", got);

    // Control for that positive. SRTO_LATENCY is a `pre` option on the very
    // same socket: if it were accepted here, the acceptance above would prove
    // nothing about SRTO_SNDDROPDELAY and a library that never enforces the
    // pre-connect restriction would score the point.
    st_set_must_fail(L.cal, SRTO_LATENCY, 700, SRT_ECONNSOCK,
                     "control: SRTO_LATENCY (a `pre` option) after connect");
}

SRT_CASE(g05_snddropdelay, is_settable_on_the_accepted_socket_too) {
    Link L;
    st_connect(L, RelaySched());
    st_require_connected(L.acc, "accepted");

    st_set_must_pass(L.acc, SRTO_SNDDROPDELAY, 900,
                     "post-connect SRTO_SNDDROPDELAY on the accepted socket");
    int got = st_get_int(L.acc, SRTO_SNDDROPDELAY);
    ST_REQUIRE_MSG(got == 900,
                   "SRTO_SNDDROPDELAY on the accepted socket is %d, expected 900", got);

    // Same control on this side: a `pre` option must still be refused here.
    st_set_must_fail(L.acc, SRTO_PAYLOADSIZE, 1200, SRT_ECONNSOCK,
                     "control: SRTO_PAYLOADSIZE (a `pre` option) on the accepted socket");
}

// =========================================================================
// g06_kmrefresh
//
// scored.txt: "`SRTO_KMREFRESHRATE` and `SRTO_KMPREANNOUNCE` accept values
// before connect only and refuse the set after connect, `srt_getsockflag`
// refuses both with `SRT_EINVOP`, and setting `KMPREANNOUNCE` above
// `(KMREFRESHRATE-1)/2` is refused with `SRT_EINVPARAM`".
// =========================================================================

SRT_CASE(g06_kmrefresh, both_options_are_accepted_before_connect) {
    SRTSOCKET s = st_fresh_socket();

    // Order matters and is documented: setting KMREFRESHRATE first fixes the
    // ceiling that the following KMPREANNOUNCE must respect. 1024 <= (4096-1)/2.
    st_set_must_pass(s, SRTO_KMREFRESHRATE, 4096, "pre-connect SRTO_KMREFRESHRATE");
    st_set_must_pass(s, SRTO_KMPREANNOUNCE, 1024, "pre-connect SRTO_KMPREANNOUNCE");

    // And on the socket configuration these options actually govern: an
    // encrypted sender. 2048 <= (8192-1)/2.
    st_set_must_pass(s, SRTO_PBKEYLEN, 16, "SRTO_PBKEYLEN");
    st_set_str(s, SRTO_PASSPHRASE, "g06-kmrefresh-passphrase");
    st_set_must_pass(s, SRTO_KMREFRESHRATE, 8192,
                     "pre-connect SRTO_KMREFRESHRATE on an encrypted socket");
    st_set_must_pass(s, SRTO_KMPREANNOUNCE, 2048,
                     "pre-connect SRTO_KMPREANNOUNCE on an encrypted socket");

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g06_kmrefresh, both_options_are_write_only) {
    SRTSOCKET s = st_fresh_socket();

    // Control: getsockflag works on this socket. Without this, "the read was
    // refused" would also be true of a library whose getsockflag is broken.
    int t = st_get_int(s, SRTO_RCVTIMEO);
    ST_REQUIRE_MSG(t == -1,
                   "control: SRTO_RCVTIMEO default read back %d, expected -1; srt_getsockflag "
                   "must work on this socket for the refusals below to mean anything", t);

    st_get_must_fail(s, SRTO_KMREFRESHRATE, SRT_EINVOP, "SRTO_KMREFRESHRATE before any set");
    st_get_must_fail(s, SRTO_KMPREANNOUNCE, SRT_EINVOP, "SRTO_KMPREANNOUNCE before any set");

    // Write-only, not merely unset: still refused after a set that succeeded.
    st_set_must_pass(s, SRTO_KMREFRESHRATE, 4096, "SRTO_KMREFRESHRATE");
    st_set_must_pass(s, SRTO_KMPREANNOUNCE, 1024, "SRTO_KMPREANNOUNCE");
    st_get_must_fail(s, SRTO_KMREFRESHRATE, SRT_EINVOP, "SRTO_KMREFRESHRATE after a successful set");
    st_get_must_fail(s, SRTO_KMPREANNOUNCE, SRT_EINVOP, "SRTO_KMPREANNOUNCE after a successful set");

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g06_kmrefresh, both_options_are_refused_after_connect) {
    Link L;
    int pre_rate = 1, pre_pre = 1;
    SockConf cconf = [&pre_rate, &pre_pre](SRTSOCKET s) {
        // The positive control, on the same socket with the same values: these
        // must succeed before connect, or "refused after connect" says nothing.
        pre_rate = st_try_set_int(s, SRTO_KMREFRESHRATE, 4096);
        pre_pre  = st_try_set_int(s, SRTO_KMPREANNOUNCE, 1024);
    };
    st_connect(L, RelaySched(), st_noconf, cconf);

    ST_REQUIRE_MSG(pre_rate == 0,
                   "control: SRTO_KMREFRESHRATE=4096 before connect returned %d, expected 0", pre_rate);
    ST_REQUIRE_MSG(pre_pre == 0,
                   "control: SRTO_KMPREANNOUNCE=1024 before connect returned %d, expected 0", pre_pre);
    st_require_connected(L.cal, "caller");

    st_set_must_fail(L.cal, SRTO_KMREFRESHRATE, 8192, SRT_ECONNSOCK,
                     "SRTO_KMREFRESHRATE after connect");
    st_set_must_fail(L.cal, SRTO_KMPREANNOUNCE, 2048, SRT_ECONNSOCK,
                     "SRTO_KMPREANNOUNCE after connect");

    // Write-only at every socket state, not only before connect.
    st_get_must_fail(L.cal, SRTO_KMREFRESHRATE, SRT_EINVOP,
                     "SRTO_KMREFRESHRATE while connected");
    st_get_must_fail(L.cal, SRTO_KMPREANNOUNCE, SRT_EINVOP,
                     "SRTO_KMPREANNOUNCE while connected");

    // Control for the two refusals: a `post` option is still settable here, so
    // they are these options' restriction and not a socket refusing everything.
    st_set_must_pass(L.cal, SRTO_RCVTIMEO, 500,
                     "control: SRTO_RCVTIMEO (a `post` option) after connect");
}

SRT_CASE(g06_kmrefresh, preannounce_ceiling_tracks_the_refresh_rate) {
    SRTSOCKET s = st_fresh_socket();

    // The documented relation is KMPREANNOUNCE <= (KMREFRESHRATE-1)/2. Assert
    // it at two different refresh rates so the case proves the ceiling MOVES
    // with the rate rather than matching one hard-coded number.
    static const int rates[] = {4096, 512};
    for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        const int rate = rates[i];
        const int cap = (rate - 1) / 2;

        st_set_must_pass(s, SRTO_KMREFRESHRATE, rate, "SRTO_KMREFRESHRATE");

        // Positive control for the refusal that follows: the ceiling itself is
        // accepted.
        st_set_must_pass(s, SRTO_KMPREANNOUNCE, cap, "SRTO_KMPREANNOUNCE at the ceiling");

        st_set_must_fail(s, SRTO_KMPREANNOUNCE, cap + 1, SRT_EINVPARAM,
                         "SRTO_KMPREANNOUNCE one above the ceiling");

        // The refused set must not have taken effect: the ceiling value is
        // still accepted afterwards.
        st_set_must_pass(s, SRTO_KMPREANNOUNCE, cap,
                         "SRTO_KMPREANNOUNCE at the ceiling after a refused set");
    }

    ST_SRT_OK(srt_close(s));
}

// =========================================================================
// g09_sockopt
//
// scored.txt: "the options brought by milestone 02 (`SRTO_STREAMID`,
// `SRTO_MINVERSION`, `SRTO_TRANSTYPE`, `SRTO_MESSAGEAPI`, `SRTO_PAYLOADSIZE`)
// are readable and settable at the documented restriction level".
//
// The documented restriction level, from srt-docs/API.md's option table: all
// five are Binding `pre`, so the set is refused once the socket is connected;
// SRTO_STREAMID is [GET or SET] and SRTO_TRANSTYPE is [SET] only, i.e. it has
// no getter.
// =========================================================================

SRT_CASE(g09_sockopt, defaults_on_a_fresh_socket) {
    SRTSOCKET s = st_fresh_socket();

    int pls = st_get_int(s, SRTO_PAYLOADSIZE);
    ST_REQUIRE_MSG(pls == SRT_LIVE_DEF_PLSIZE,
                   "SRTO_PAYLOADSIZE default is %d, documented as %d (the live-mode default)",
                   pls, SRT_LIVE_DEF_PLSIZE);

    ST_REQUIRE_MSG(st_get_bool(s, SRTO_MESSAGEAPI),
                   "SRTO_MESSAGEAPI default is false, documented as true (message API is the "
                   "live-mode default)");

    std::string sid = st_get_str(s, SRTO_STREAMID);
    ST_REQUIRE_MSG(sid.empty(),
                   "SRTO_STREAMID default is \"%s\" (%zu bytes), documented as empty",
                   sid.c_str(), sid.size());

    // SRTO_MINVERSION is the one row of the option table the released library
    // contradicts: the table gives the default as 0, the implementation
    // initialises the floor to SRT_VERSION_MAJ1 and getsockflag returns
    // 0x010000. PROFILE.md §5.1 makes a documented default binding, but
    // DRIVER_RULES requires every group to pass against the Oracle, and the
    // Oracle is 0x010000 -- so this asserts only the part the disagreement
    // leaves intact: the option is readable, and its default floor does not
    // exclude an HSv5-capable peer, which is the functional content of "no
    // restriction by default". The exact figure is reported, not scored.
    int minver = st_get_int(s, SRTO_MINVERSION);
    ST_REQUIRE_MSG(minver >= 0 && minver <= SRT_VERSION_FEAT_HSv5,
                   "SRTO_MINVERSION default is 0x%06x, outside 0..0x%06x; whatever the default "
                   "peer-version floor is, it must not exclude an HSv5-capable peer",
                   minver, (unsigned)SRT_VERSION_FEAT_HSv5);

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g09_sockopt, all_five_are_settable_before_connect_and_read_back) {
    SRTSOCKET s = st_fresh_socket();

    const std::string sid = "step03/g09/stream-id";
    st_set_str(s, SRTO_STREAMID, sid);
    std::string gsid = st_get_str(s, SRTO_STREAMID);
    ST_REQUIRE_MSG(gsid == sid, "SRTO_STREAMID read back \"%s\", expected \"%s\"",
                   gsid.c_str(), sid.c_str());

    st_set_must_pass(s, SRTO_MINVERSION, 0x010300, "SRTO_MINVERSION");
    int mv = st_get_int(s, SRTO_MINVERSION);
    ST_REQUIRE_MSG(mv == 0x010300, "SRTO_MINVERSION read back 0x%06x, expected 0x010300", mv);

    st_set_must_pass(s, SRTO_PAYLOADSIZE, 1200, "SRTO_PAYLOADSIZE");
    int pls = st_get_int(s, SRTO_PAYLOADSIZE);
    ST_REQUIRE_MSG(pls == 1200, "SRTO_PAYLOADSIZE read back %d, expected 1200", pls);

    st_set_must_pass(s, SRTO_MESSAGEAPI, 0, "SRTO_MESSAGEAPI=0");
    ST_REQUIRE_MSG(!st_get_bool(s, SRTO_MESSAGEAPI),
                   "SRTO_MESSAGEAPI read back true after being set to 0");
    st_set_must_pass(s, SRTO_MESSAGEAPI, 1, "SRTO_MESSAGEAPI=1");
    ST_REQUIRE_MSG(st_get_bool(s, SRTO_MESSAGEAPI),
                   "SRTO_MESSAGEAPI read back false after being set to 1");

    // SRTO_TRANSTYPE has no getter, so its own case checks it by its effect.
    st_set_must_pass(s, SRTO_TRANSTYPE, (int)SRTT_LIVE, "SRTO_TRANSTYPE=SRTT_LIVE");

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g09_sockopt, transtype_is_write_only_but_not_inert) {
    SRTSOCKET s = st_fresh_socket();

    // Documented [SET]: there is no getter for SRTO_TRANSTYPE.
    st_get_must_fail(s, SRTO_TRANSTYPE, SRT_EINVOP, "SRTO_TRANSTYPE on a fresh socket");

    // Control: SRTO_STREAMID is documented [GET or SET] and does read back, so
    // the refusal above is this option's restriction and not a socket that
    // reads nothing back.
    st_set_str(s, SRTO_STREAMID, "g09-getset-control");
    ST_REQUIRE_MSG(st_get_str(s, SRTO_STREAMID) == "g09-getset-control",
                   "control: SRTO_STREAMID is documented [GET or SET] but read back \"%s\"",
                   st_get_str(s, SRTO_STREAMID).c_str());

    // Unreadable is not the same as ignored. SRTT_FILE is documented to select
    // the file-mode defaults: the buffer API, and a payload size of 0.
    st_set_must_pass(s, SRTO_TRANSTYPE, (int)SRTT_FILE, "SRTO_TRANSTYPE=SRTT_FILE");
    ST_REQUIRE_MSG(!st_get_bool(s, SRTO_MESSAGEAPI),
                   "after SRTO_TRANSTYPE=SRTT_FILE, SRTO_MESSAGEAPI is true; file mode is "
                   "documented to select the buffer API");
    int pls = st_get_int(s, SRTO_PAYLOADSIZE);
    ST_REQUIRE_MSG(pls == 0,
                   "after SRTO_TRANSTYPE=SRTT_FILE, SRTO_PAYLOADSIZE is %d, documented as 0 in "
                   "file mode", pls);

    st_set_must_pass(s, SRTO_TRANSTYPE, (int)SRTT_LIVE, "SRTO_TRANSTYPE=SRTT_LIVE");
    ST_REQUIRE_MSG(st_get_bool(s, SRTO_MESSAGEAPI),
                   "after SRTO_TRANSTYPE=SRTT_LIVE, SRTO_MESSAGEAPI is false; live mode is "
                   "documented to imply the message API");
    pls = st_get_int(s, SRTO_PAYLOADSIZE);
    ST_REQUIRE_MSG(pls == SRT_LIVE_DEF_PLSIZE,
                   "after SRTO_TRANSTYPE=SRTT_LIVE, SRTO_PAYLOADSIZE is %d, documented as %d",
                   pls, SRT_LIVE_DEF_PLSIZE);

    // Still no getter, even once it has been set.
    st_get_must_fail(s, SRTO_TRANSTYPE, SRT_EINVOP, "SRTO_TRANSTYPE after a successful set");

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g09_sockopt, the_pre_binding_is_enforced_after_connect) {
    Link L;
    int pre[5] = {1, 1, 1, 1, 1};
    // Values chosen so the handshake still succeeds: SRTO_MESSAGEAPI must
    // agree with the listener (a mismatch is rejected at HSREQ), and the peer
    // really is >= 1.3.0, so the SRTO_MINVERSION floor is satisfied.
    SockConf cconf = [&pre](SRTSOCKET s) {
        pre[0] = srt_setsockflag(s, SRTO_STREAMID, "g09-pre", 7);
        pre[1] = st_try_set_int(s, SRTO_MINVERSION, 0x010300);
        pre[2] = st_try_set_int(s, SRTO_TRANSTYPE, (int)SRTT_LIVE);
        pre[3] = st_try_set_int(s, SRTO_MESSAGEAPI, 1);
        pre[4] = st_try_set_int(s, SRTO_PAYLOADSIZE, SRT_LIVE_DEF_PLSIZE);
    };
    st_connect(L, RelaySched(), st_noconf, cconf);

    static const char* names[5] = {"SRTO_STREAMID", "SRTO_MINVERSION", "SRTO_TRANSTYPE",
                                   "SRTO_MESSAGEAPI", "SRTO_PAYLOADSIZE"};
    for (int i = 0; i < 5; i++)
        ST_REQUIRE_MSG(pre[i] == 0,
                       "control: %s set before connect returned %d, expected 0; the refusals "
                       "below only mean something if the same set works pre-connect",
                       names[i], pre[i]);
    st_require_connected(L.cal, "caller");

    srt_clearlasterror();
    int rc = st_try_set_str(L.cal, SRTO_STREAMID, "g09-post");
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "SRTO_STREAMID after connect returned %d (accepted); expected -1 with "
                   "SRT_ECONNSOCK, the option is Binding `pre`", rc);
    int e = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(e == SRT_ECONNSOCK,
                   "SRTO_STREAMID after connect was refused with %s (%d, \"%s\"); expected "
                   "SRT_ECONNSOCK (%d)", st_errname(e), e, srt_getlasterror_str(),
                   (int)SRT_ECONNSOCK);

    st_set_must_fail(L.cal, SRTO_MINVERSION, 0x010302, SRT_ECONNSOCK,
                     "SRTO_MINVERSION after connect");
    st_set_must_fail(L.cal, SRTO_TRANSTYPE, (int)SRTT_FILE, SRT_ECONNSOCK,
                     "SRTO_TRANSTYPE after connect");
    st_set_must_fail(L.cal, SRTO_MESSAGEAPI, 0, SRT_ECONNSOCK,
                     "SRTO_MESSAGEAPI after connect");
    st_set_must_fail(L.cal, SRTO_PAYLOADSIZE, 1200, SRT_ECONNSOCK,
                     "SRTO_PAYLOADSIZE after connect");

    // [GET or SET] means the read still works while connected, and the refused
    // write above must not have changed anything.
    std::string sid = st_get_str(L.cal, SRTO_STREAMID);
    ST_REQUIRE_MSG(sid == "g09-pre",
                   "SRTO_STREAMID while connected is \"%s\", expected \"g09-pre\"; the refused "
                   "post-connect set must not have taken effect", sid.c_str());

    // Control for all five refusals: a documented `post` option is still
    // settable on this socket.
    st_set_must_pass(L.cal, SRTO_RCVTIMEO, 500,
                     "control: SRTO_RCVTIMEO (a `post` option) after connect");
}

SRT_CASE(g09_sockopt, payloadsize_honours_its_documented_range) {
    SRTSOCKET s = st_fresh_socket();

    // Documented range: "up to MTUsize-28-16, usually 1456", which the header
    // names SRT_LIVE_MAX_PLSIZE.
    st_set_must_pass(s, SRTO_PAYLOADSIZE, SRT_LIVE_MAX_PLSIZE,
                     "SRTO_PAYLOADSIZE at the documented maximum");
    int v = st_get_int(s, SRTO_PAYLOADSIZE);
    ST_REQUIRE_MSG(v == SRT_LIVE_MAX_PLSIZE,
                   "SRTO_PAYLOADSIZE read back %d, expected %d", v, SRT_LIVE_MAX_PLSIZE);

    st_set_must_fail(s, SRTO_PAYLOADSIZE, SRT_LIVE_MAX_PLSIZE + 1, SRT_EINVPARAM,
                     "SRTO_PAYLOADSIZE one byte above the documented maximum");

    v = st_get_int(s, SRTO_PAYLOADSIZE);
    ST_REQUIRE_MSG(v == SRT_LIVE_MAX_PLSIZE,
                   "after the refused set SRTO_PAYLOADSIZE is %d, expected the previous %d",
                   v, SRT_LIVE_MAX_PLSIZE);

    ST_SRT_OK(srt_close(s));
}

SRT_CASE(g09_sockopt, streamid_honours_its_documented_length_limit) {
    SRTSOCKET s = st_fresh_socket();

    // Documented: "A string limited to 512 characters".
    const std::string ok(512, 'x');
    st_set_str(s, SRTO_STREAMID, ok);
    std::string got = st_get_str(s, SRTO_STREAMID);
    ST_REQUIRE_MSG(got.size() == 512,
                   "SRTO_STREAMID set to 512 bytes read back %zu bytes", got.size());
    ST_REQUIRE_MSG(got == ok, "SRTO_STREAMID read back 512 bytes that differ from the 512 set");

    const std::string toolong(513, 'y');
    srt_clearlasterror();
    int rc = st_try_set_str(s, SRTO_STREAMID, toolong);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "SRTO_STREAMID at 513 bytes returned %d (accepted); expected -1 with "
                   "SRT_EINVPARAM, the documented limit is 512", rc);
    int e = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(e == SRT_EINVPARAM,
                   "SRTO_STREAMID at 513 bytes was refused with %s (%d, \"%s\"); expected "
                   "SRT_EINVPARAM (%d)", st_errname(e), e, srt_getlasterror_str(),
                   (int)SRT_EINVPARAM);

    got = st_get_str(s, SRTO_STREAMID);
    ST_REQUIRE_MSG(got == ok,
                   "after the refused 513-byte set the stream id is %zu bytes, expected the "
                   "previous 512", got.size());

    ST_SRT_OK(srt_close(s));
}
