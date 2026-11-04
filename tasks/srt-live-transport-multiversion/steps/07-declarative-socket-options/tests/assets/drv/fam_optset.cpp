// Family "optset", checkpoint 07 (SRT v1.4.3). Four scored groups, all `new`:
//
//   g06_value_range       srt_setsockopt refuses, with SRT_EINVPARAM, a value
//                         below an option's documented minimum, and the refused
//                         call is INERT: the option still reports what it held
//                         before. -1 stays legal where the Range admits it.
//   g07_filter_config     SRTO_PACKETFILTER validates its configuration inside
//                         the srt_setsockopt call: an unknown key, a `cols`
//                         below 2, or a bad `layout`/`arq` value is refused
//                         there and then; four well-formed strings are accepted.
//   g08_bind_boundary     bound-but-not-listening is NOT a restricted state for
//                         a pre-connect option -- six of them are accepted and
//                         read back -- while the pre-bind SRTO_IPV6ONLY is
//                         refused on that same socket with SRT_EBOUNDSOCK.
//   g09_listen_boundary   a LISTENING socket refuses nine options with
//                         SRT_ECONNSOCK, including ones that bind alone does
//                         not restrict, while SRTO_RCVSYN and SRTO_RCVTIMEO
//                         (no flags entry at all) stay accepted.
//
// ---------------------------------------------------------------------------
// Tier: every group here is `new` and SCORED (meta.env E2E_TIER=new; the gate lives in
// the `live` family, not this one). There is no `inherited` or `peripheral`
// group in this file, so no group must be earnable by a candidate who stopped
// at v1.4.2, and DRIVER_RULES' dlsym/restate machinery buys nothing here -- it
// is deliberately not used. A `new` group that still compiles AND PASSES at
// v1.4.2 would be a group that fails to discriminate, which is worse than one
// that does not build there. That is not what happens: this file names no
// identifier absent from v1.4.2's <srt/srt.h> (SRTO_MININPUTBW, the step's only
// new enumerator, belongs to the `optget` family), so the family COMPILES at
// v1.4.2 and each group fails on its assertions. Measured, and reported as such.
//
// ---------------------------------------------------------------------------
// Traceability. Every assertion traces to a numbered requirement of the frozen
// step-07 list (tmp/<task-id>/REQUIREMENTS.md, "Step 07", frozen 2026-09-21) and
// to the candidate-facing text of stepdefs/07/interface.md (TEST_INTERFACE.md
// section 5). The requirement is named per case below. The deltas/07.md row is
// named where the row is the discriminator this step is scored on (D6/D7, D9,
// D11/D12, D13). Nothing here is asserted from the pinned srt-docs/ alone.
//
// Where interface.md corrects the pinned documentation, this file follows the
// CORRECTION, because the brief makes a correction a correction to reality:
//
//   * "A refused set is a no-op on the value, not a clamp." The per-option
//     Range column in API-socket-options.md says nothing about what happens to
//     a value outside the Range; interface.md states it is refused AND the old
//     value survives. Every negative case in g06 is paired with that readback,
//     which is what makes the case more than "the call returned -1".
//   * "The packet-filter configuration is validated in the set call, and an
//     unknown key is fatal." The filter documentation defers conflicting
//     configurations to connection time. g07 binds nothing, listens on nothing
//     and connects to nothing, so a candidate that defers validation has no
//     later moment at which to fail and cannot score the group by accident.
//   * "A bound socket is not a restricted state for a pre-connect option." The
//     previous milestone locked both classes at bind; g08 asserts BOTH
//     directions on ONE socket so neither a lock-at-bind nor a lock-at-connect
//     tree can pass it.
//
// The refusal codes are the observable, not the message: MN_INVAL, MN_ISBOUND
// and MN_ISCONNECTED surface through SRT_EINVPARAM, SRT_EBOUNDSOCK and
// SRT_ECONNSOCK respectively (srt.h:517-519). Every negative case asserts the
// exact code with srt_getlasterror(), not merely SRT_ERROR.
//
// ---------------------------------------------------------------------------
// Deliberately NOT asserted, and why:
//
//   * D10 (SRTO_KMREFRESHRATE=5000 then SRTO_KMPREANNOUNCE=0). A `certain` row
//     of deltas/07.md, but it is a cross-option constraint and none of the four
//     group descriptions in scored.txt names it; the frozen list folds it into
//     no requirement either. RQ-07-06 is about each option's own documented
//     minimum. Reported rather than added.
//   * D7's SRTO_SNDTIMEO and SRTO_RCVTIMEO at < -1. Both are in the delta row's
//     parenthetical list, but RQ-07-06 enumerates eight options and neither of
//     these is among them. A case for one would score behaviour the candidate
//     was never shown for this group. Reported rather than added.
//   * The *value* a refused set leaves behind is asserted only where the option
//     is readable on that socket; no case in g09 reads back a refused option,
//     because interface.md says nothing about a listening socket's getters and
//     this file will not invent it.
//   * Whether a *clamp* is observable at all. g06's subject is the refusal plus
//     inertness; v1.4.2's SRTO_FC clamps to 32 rather than refusing, and that is
//     how the family discriminates at that tag, but no case asserts a clamp.
//
// No case here opens a socket it does not close, no case binds anything but an
// ephemeral loopback port, no case connects, listens on a real service, or
// moves a byte, and no case uses a sleep of any kind. g06 and g07 are pure
// option-layer operations; g08 and g09 exercise bind/listen state transitions
// and nothing beyond them.
#include "stest.h"
#include "srtkit.h"

#include <cstring>
#include <string>
#include <vector>

// --------------------------------------------------------------- small helpers
//
// The three below are shared by every group and are deliberately not in
// srtkit.h: the kit is compiled at every tag in the chain and may not name
// anything version-specific, while these read the *current* position's
// behaviour. They are local to this family and no other driver depends on them.

// An int-valued set that is expected to be REFUSED, asserting the exact error
// code rather than merely SRT_ERROR: "it failed" and "it failed with
// SRT_EINVPARAM" are different claims and only the second is the requirement.
static void st_expect_set_refused(SRTSOCKET s, SRT_SOCKOPT o, int v, int want_err,
                                  const char* what) {
    const int rc = srt_setsockflag(s, o, &v, (int)sizeof v);
    const int got = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "%s: srt_setsockflag(%d, %d) returned %d, expected refusal %d",
                   what, (int)o, v, rc, want_err);
    ST_REQUIRE_MSG(got == want_err, "%s: refusal code: expected %d (%s), got %d (%s)", what,
                   want_err, srt_strerror(want_err, 0), got, srt_strerror(got, 0));
}

// A 64-bit set expected to be refused, for an option whose argument is an
// int64_t. Separate from the int form because passing the wrong width would
// trip cast_optval's own length check (socketconfig.h:364-371) and the case
// would pass for a reason that has nothing to do with the range.
static void st_expect_set_refused64(SRTSOCKET s, SRT_SOCKOPT o, int64_t v, int want_err,
                                    const char* what) {
    const int rc = srt_setsockflag(s, o, &v, (int)sizeof v);
    const int got = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc == SRT_ERROR, "%s: srt_setsockflag(%d, %lld) returned %d, expected refusal %d",
                   what, (int)o, (long long)v, rc, want_err);
    ST_REQUIRE_MSG(got == want_err, "%s: refusal code: expected %d (%s), got %d (%s)", what,
                   want_err, srt_strerror(want_err, 0), got, srt_strerror(got, 0));
}

static void st_expect_set_ok(SRTSOCKET s, SRT_SOCKOPT o, int v, const char* what) {
    const int rc = srt_setsockflag(s, o, &v, (int)sizeof v);
    const int got = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc != SRT_ERROR, "%s: srt_setsockflag(%d, %d) failed: %s (code %d)", what,
                   (int)o, v, srt_getlasterror_str(), got);
}

// Reads an int-valued option and returns what the library reported. Uses the
// kit's throwing getter, so a getter that refuses a W option (interface.md's
// third correction) fails the case by name rather than by returning -1.
static int st_read_int(SRTSOCKET s, SRT_SOCKOPT o, const char* what) {
    int v = -1;
    int n = (int)sizeof v;
    if (srt_getsockflag(s, o, &v, &n) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "%s: srt_getsockflag(%d) failed: %s", what, (int)o,
                 srt_getlasterror_str());
    return v;
}

// A boolean option's getter narrows *optlen to 1 (interface.md's second
// correction, RQ-07-02, which the optget family scores). Groups here read
// booleans -- SRTO_TSBPDMODE, SRTO_TLPKTDROP, SRTO_RCVSYN -- and must decode
// whatever width came back rather than demand four bytes, so this accepts
// either the documented 1-byte width or a 4-byte one and compares the value.
static long st_read_bool_loose(SRTSOCKET s, SRT_SOCKOPT o, const char* what) {
    unsigned char buf[8];
    std::memset(buf, 0xFF, sizeof buf);
    int n = (int)sizeof buf;
    if (srt_getsockflag(s, o, buf, &n) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "%s: srt_getsockflag(%d) failed: %s", what, (int)o,
                 srt_getlasterror_str());
    ST_REQUIRE_MSG(n == 1 || n == (int)sizeof(int32_t),
                   "%s: getsockflag(%d) reported %d bytes, expected 1 or 4", what, (int)o, n);
    if (n == 1) return (long)buf[0];
    int32_t v;
    std::memcpy(&v, buf, sizeof v);
    return (long)v;
}

// Binds to an ephemeral loopback port and returns the socket, so a case can
// reach the bound state without guessing a port. The port itself is irrelevant
// to every case here; only the state transition is.
static SRTSOCKET st_bound_socket() {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    sockaddr_in a = st_loopback(0);
    if (srt_bind(s, (sockaddr*)&a, sizeof a) == SRT_ERROR) {
        const char* e = srt_getlasterror_str();
        srt_close(s);
        st_failf(__FILE__, __LINE__, "srt_bind(127.0.0.1:0): %s", e);
    }
    return s;
}

static SRTSOCKET st_listening_socket() {
    SRTSOCKET s = st_bound_socket();
    if (srt_listen(s, 1) == SRT_ERROR) {
        const char* e = srt_getlasterror_str();
        srt_close(s);
        st_failf(__FILE__, __LINE__, "srt_listen: %s", e);
    }
    return s;
}

// ===========================================================================
// g06_value_range -- RQ-07-06 (deltas/07.md D6, D7)
//
// "srt_setsockopt refuses with SRT_EINVPARAM a value below an option's
// documented minimum ... and leaves each option reporting the value it held
// before the refused call rather than clamping the new one into range, while
// -1 remains acceptable for SRTO_MAXBW and SRTO_SNDDROPDELAY."
//
// The failure mode this group is built to catch is a tree that clamps: a
// clamped SRTO_FC reads 32 where 100000 was set, a clamped SRTO_RCVLATENCY
// reads 0 where 250 was set. Every negative case therefore sets a value first,
// asserts the refusal, and then asserts the OLD value is still there. A tree
// that returns -1 and stores the bad value anyway fails; a tree that clamps
// fails; only a tree that refuses and is inert passes.
// ===========================================================================

// RQ-07-06, SRTO_FC. The step's minimum is DEF_MIN_FLIGHT_PKT, whose value is
// 32; the case sets 100000 and then 16, which is below it, and asserts the
// refusal is inert. 16 is chosen because deltas/07.md D6 records v1.4.2
// accepting it and silently clamping to 32 -- so the readback is the whole
// discrimination at the previous tag, not the return code alone.
SRT_CASE(g06_value_range, a_flight_flag_below_the_minimum_is_refused_and_the_old_value_survives) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    st_expect_set_ok(s, SRTO_FC, 100000, "SRTO_FC=100000 (a legal in-range value)");
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_FC, "SRTO_FC after the accepted set"), 100000);

    st_expect_set_refused(s, SRTO_FC, 16, SRT_EINVPARAM, "SRTO_FC=16 (below the minimum)");

    const int after = st_read_int(s, SRTO_FC, "SRTO_FC after the refused set");
    ST_REQUIRE_MSG(after == 100000,
                   "SRTO_FC after a refused 16: expected the value it held before, 100000, got %d "
                   "(a clamp to the minimum would read 32)",
                   after);

    srt_close(s);
}

// RQ-07-06, the eight option/minimum pairs the requirement enumerates. The
// subject is not one option but the rule: each of these rejects -1 with
// SRT_EINVPARAM and each keeps the non-negative value it held. SRTO_SNDTIMEO
// and SRTO_RCVTIMEO are in deltas/07.md D7's parenthetical but not in the
// requirement, so they are not scored here; see the header note.
SRT_CASE(g06_value_range, minus_one_is_refused_by_every_option_whose_minimum_is_zero) {
    struct Subject {
        SRT_SOCKOPT opt;
        int         held;   // a legal value to hold before the refused call
        const char* name;
    };
    const Subject subjects[] = {
        {SRTO_CONNTIMEO, 3000, "SRTO_CONNTIMEO"},
        {SRTO_PAYLOADSIZE, 1316, "SRTO_PAYLOADSIZE"},
        {SRTO_PEERIDLETIMEO, 5000, "SRTO_PEERIDLETIMEO"},
        {SRTO_RCVLATENCY, 250, "SRTO_RCVLATENCY"},
        {SRTO_PEERLATENCY, 130, "SRTO_PEERLATENCY"},
        {SRTO_LATENCY, 200, "SRTO_LATENCY"},
        {SRTO_KMREFRESHRATE, 5000, "SRTO_KMREFRESHRATE"},
    };

    for (const Subject& sub : subjects) {
        SRTSOCKET s = st_new_socket();
        ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

        st_expect_set_ok(s, sub.opt, sub.held, sub.name);
        const int held = st_read_int(s, sub.opt, sub.name);
        ST_REQUIRE_MSG(held == sub.held, "%s: after setting %d the getter reported %d", sub.name,
                       sub.held, held);

        // The negative, with the code named, and the positive control for it in
        // the next statement: a library that refuses everything would pass the
        // refusal and fail the readback.
        st_expect_set_refused(s, sub.opt, -1, SRT_EINVPARAM, sub.name);

        const int after = st_read_int(s, sub.opt, sub.name);
        ST_REQUIRE_MSG(after == sub.held,
                       "%s after a refused -1: expected the value it held before, %d, got %d", sub.name,
                       sub.held, after);

        srt_close(s);
    }

    // SRTO_LATENCY is a setter for the other two as well (RQ-07-01), so the
    // refusal above must leave both of them alone too -- otherwise the case
    // would pass while a set that was refused had already moved its peers.
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    st_expect_set_ok(s, SRTO_RCVLATENCY, 111, "SRTO_RCVLATENCY=111");
    st_expect_set_ok(s, SRTO_PEERLATENCY, 222, "SRTO_PEERLATENCY=222");
    st_expect_set_refused(s, SRTO_LATENCY, -1, SRT_EINVPARAM, "SRTO_LATENCY=-1");
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_RCVLATENCY, "SRTO_RCVLATENCY after the refused SRTO_LATENCY"),
                      111);
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_PEERLATENCY, "SRTO_PEERLATENCY after the refused SRTO_LATENCY"),
                      222);

    // SRTO_KMPREANNOUNCE is the eighth option of the requirement, but it shares
    // its setter's cross-option rule with SRTO_KMREFRESHRATE, so it is set on
    // its own socket, after the refresh rate it is compared against, and only
    // then refused. Setting it before a refresh rate would make the refusal
    // come from the cross-option rule and not from the minimum, which is the
    // one thing this case must not confuse.
    SRTSOCKET k = st_new_socket();
    ST_REQUIRE_MSG(k != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    st_expect_set_ok(k, SRTO_KMREFRESHRATE, 5000, "SRTO_KMREFRESHRATE=5000");
    st_expect_set_ok(k, SRTO_KMPREANNOUNCE, 1000, "SRTO_KMPREANNOUNCE=1000");
    ST_REQUIRE_EQ_INT(st_read_int(k, SRTO_KMPREANNOUNCE, "SRTO_KMPREANNOUNCE readback"), 1000);
    st_expect_set_refused(k, SRTO_KMPREANNOUNCE, -1, SRT_EINVPARAM, "SRTO_KMPREANNOUNCE");
    ST_REQUIRE_EQ_INT(st_read_int(k, SRTO_KMPREANNOUNCE, "SRTO_KMPREANNOUNCE after a refused -1"), 1000);
    srt_close(k);

    srt_close(s);
}

// RQ-07-06, the other half of the rule: "Where a Range is written -1.. the
// sentinel is genuinely legal". These two must keep accepting it. Without this
// case a tree that rejects -1 everywhere passes the case above.
SRT_CASE(g06_value_range, minus_one_stays_legal_where_the_range_admits_the_sentinel) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // SRTO_MAXBW is 64-bit; passing an int here would be refused by the length
    // check rather than the range and the case would pass for the wrong reason.
    {
        int64_t v = 1000000;
        ST_REQUIRE_MSG(srt_setsockflag(s, SRTO_MAXBW, &v, (int)sizeof v) != SRT_ERROR,
                       "SRTO_MAXBW=1000000 (int64_t): %s", srt_getlasterror_str());
    }
    {
        int64_t v = -1;
        const int rc = srt_setsockflag(s, SRTO_MAXBW, &v, (int)sizeof v);
        const int got = srt_getlasterror(nullptr);
        ST_REQUIRE_MSG(rc != SRT_ERROR, "SRTO_MAXBW=-1 must be accepted as the sentinel, got rc %d (%s, "
                                        "code %d)",
                       rc, srt_getlasterror_str(), got);
    }
    {
        int64_t back = 0;
        int n = (int)sizeof back;
        ST_REQUIRE_MSG(srt_getsockflag(s, SRTO_MAXBW, &back, &n) != SRT_ERROR,
                       "SRTO_MAXBW readback: %s", srt_getlasterror_str());
        ST_REQUIRE_MSG(back == -1, "SRTO_MAXBW after a -1: expected -1, got %lld", (long long)back);
    }

    // And the boundary is genuinely -1, not "anything": -2 is below it and is
    // refused, which is what keeps this case from being satisfied by a tree
    // that simply removed the check.
    st_expect_set_refused64(s, SRTO_MAXBW, -2, SRT_EINVPARAM, "SRTO_MAXBW=-2 (below the sentinel)");
    {
        int64_t back = 0;
        int n = (int)sizeof back;
        ST_REQUIRE_MSG(srt_getsockflag(s, SRTO_MAXBW, &back, &n) != SRT_ERROR,
                       "SRTO_MAXBW readback: %s", srt_getlasterror_str());
        ST_REQUIRE_MSG(back == -1, "SRTO_MAXBW after a refused -2: expected the held -1, got %lld",
                       (long long)back);
    }

    // SRTO_SNDDROPDELAY is the other option whose Range admits the -1 sentinel,
    // so it must keep taking it. A get of -1 on a fresh socket is the default,
    // so the case sets a real value first, then the sentinel, then reads back.
    st_expect_set_ok(s, SRTO_SNDDROPDELAY, 100, "SRTO_SNDDROPDELAY=100");
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_SNDDROPDELAY, "SRTO_SNDDROPDELAY readback"), 100);
    st_expect_set_ok(s, SRTO_SNDDROPDELAY, -1, "SRTO_SNDDROPDELAY=-1 (the sentinel)");
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_SNDDROPDELAY, "SRTO_SNDDROPDELAY readback"), -1);
    st_expect_set_refused(s, SRTO_SNDDROPDELAY, -2, SRT_EINVPARAM, "SRTO_SNDDROPDELAY=-2 (below sentinel)");
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_SNDDROPDELAY, "SRTO_SNDDROPDELAY after a refused -2"), -1);

    srt_close(s);
}

// ===========================================================================
// g07_filter_config -- RQ-07-07 (deltas/07.md D9)
//
// "SRTO_PACKETFILTER refuses with SRT_EINVPARAM, in the srt_setsockopt call
// itself and not later at connect time, a configuration that names a key other
// than cols, rows, layout or arq, that gives cols a value below 2, or that
// gives layout or arq a value outside its documented set, while fec,cols:10 and
// fec,cols:10,rows:5 and fec,cols:10,layout:staircase and fec,cols:10,arq:never
// are accepted."
//
// None of these cases binds, listens or connects. A tree that defers
// validation to connect time has no later moment at which to refuse and cannot
// score the group by accident -- which is the discrimination D9 records.
// ===========================================================================

// RQ-07-07, the unknown key. Two spellings, because the two-stage path in
// packetfilter.cpp:29-48 reaches a refusal through two different stages: a
// well-formed unknown key survives SrtParseConfig and dies in verifyConfig's
// "Extra parameters" branch, while a malformed element dies in the parser. Both
// surface as SRT_EINVPARAM from the same call, which is what the requirement
// asks for, and the case asserts both so neither stage can regress silently.
SRT_CASE(g07_filter_config, a_configuration_naming_a_key_the_filter_does_not_define_is_refused) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // The positive control first: a complete, well-formed configuration on the
    // same socket, so the refusals below are not the socket's state.
    st_set_str(s, SRTO_PACKETFILTER, "fec,cols:10");
    {
        char buf[128];
        std::memset(buf, 0, sizeof buf);
        int n = (int)sizeof buf;
        ST_REQUIRE_MSG(srt_getsockflag(s, SRTO_PACKETFILTER, buf, &n) != SRT_ERROR,
                       "SRTO_PACKETFILTER readback: %s", srt_getlasterror_str());
        ST_REQUIRE_MSG(std::string(buf, (size_t)(n > 0 ? n : 0)) == std::string("fec,cols:10"),
                       "SRTO_PACKETFILTER readback: expected \"fec,cols:10\", got \"%s\" (%d bytes)", buf,
                       n);
    }

    struct Refused {
        const char* cfg;
        const char* why;
    };
    const Refused bad[] = {
        {"fec,cols:10,bogus:1", "an unknown key (verifyConfig: Extra parameters)"},
        {"fec,cols", "an element with no ':' (SrtParseConfig rejects it)"},
        {"fec,cols:10,,rows:5", "an element with no ':' inside a longer list"},
    };
    for (const Refused& r : bad) {
        const std::string cfg = r.cfg;
        const int rc = srt_setsockflag(s, SRTO_PACKETFILTER, cfg.data(), (int)cfg.size());
        const int got = srt_getlasterror(nullptr);
        ST_REQUIRE_MSG(rc == SRT_ERROR, "SRTO_PACKETFILTER=\"%s\" (%s) returned %d, expected refusal", r.cfg,
                       r.why, rc);
        ST_REQUIRE_MSG(got == SRT_EINVPARAM,
                       "SRTO_PACKETFILTER=\"%s\" (%s): expected SRT_EINVPARAM (%d), got %d (%s)", r.cfg,
                       r.why, SRT_EINVPARAM, got, srt_strerror(got, 0));
    }

    // The control again, AFTER the refusals: the accepted configuration from
    // before must still be the one stored, so a refused filter cannot have
    // replaced it.
    {
        char buf[128];
        std::memset(buf, 0, sizeof buf);
        int n = (int)sizeof buf;
        ST_REQUIRE_MSG(srt_getsockflag(s, SRTO_PACKETFILTER, buf, &n) != SRT_ERROR,
                       "SRTO_PACKETFILTER readback: %s", srt_getlasterror_str());
        ST_REQUIRE_MSG(std::string(buf, (size_t)(n > 0 ? n : 0)) == std::string("fec,cols:10"),
                       "SRTO_PACKETFILTER after three refused sets: expected the accepted "
                       "\"fec,cols:10\", got \"%s\"",
                       buf);
    }

    srt_close(s);
}

// RQ-07-07, the value rules: cols below 2, layout outside its set, arq outside
// its set. cols:1 is the documented boundary (verifyConfig rejects < 2), and
// cols:2 is its control -- so the case distinguishes "refuses a bad cols" from
// "refuses cols".
SRT_CASE(g07_filter_config, a_known_key_given_a_value_outside_its_set_is_refused) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    struct Refused {
        const char* cfg;
        const char* why;
    };
    const Refused bad[] = {
        {"fec,cols:1", "cols below 2"},
        {"fec,cols:0", "cols below 2"},
        {"fec,cols:10,layout:diagonal", "layout outside {even, staircase}"},
        {"fec,cols:10,arq:zzz", "arq outside {always, onreq, never}"},
    };
    for (const Refused& r : bad) {
        const std::string cfg = r.cfg;
        const int rc = srt_setsockflag(s, SRTO_PACKETFILTER, cfg.data(), (int)cfg.size());
        const int got = srt_getlasterror(nullptr);
        ST_REQUIRE_MSG(rc == SRT_ERROR, "SRTO_PACKETFILTER=\"%s\" (%s) returned %d, expected refusal", r.cfg,
                       r.why, rc);
        ST_REQUIRE_MSG(got == SRT_EINVPARAM,
                       "SRTO_PACKETFILTER=\"%s\" (%s): expected SRT_EINVPARAM (%d), got %d (%s)", r.cfg,
                       r.why, SRT_EINVPARAM, got, srt_strerror(got, 0));
    }

    // The positive controls, in the same group and on the same socket as the
    // negatives: the four configurations RQ-07-07 names as accepted. cols:2 is
    // the boundary the four refusals above are measured against.
    const char* const good[] = {
        "fec,cols:2",
        "fec,cols:10",
        "fec,cols:10,rows:5",
        "fec,cols:10,layout:staircase",
        "fec,cols:10,arq:never",
    };
    for (const char* cfg : good) {
        const std::string c = cfg;
        const int rc = srt_setsockflag(s, SRTO_PACKETFILTER, c.data(), (int)c.size());
        ST_REQUIRE_MSG(rc != SRT_ERROR, "SRTO_PACKETFILTER=\"%s\" must be accepted, got %d (%s)", cfg, rc,
                       srt_getlasterror_str());
        // And it is the value stored, not merely a call that returned 0.
        char buf[128];
        std::memset(buf, 0, sizeof buf);
        int n = (int)sizeof buf;
        ST_REQUIRE_MSG(srt_getsockflag(s, SRTO_PACKETFILTER, buf, &n) != SRT_ERROR,
                       "SRTO_PACKETFILTER readback: %s", srt_getlasterror_str());
        ST_REQUIRE_MSG(std::string(buf, (size_t)(n > 0 ? n : 0)) == c,
                       "SRTO_PACKETFILTER readback after a successful set: expected \"%s\", got \"%s\"", cfg,
                       buf);
    }

    srt_close(s);
}

// ===========================================================================
// g08_bind_boundary -- RQ-07-08 (deltas/07.md D11, D12)
//
// "on a socket that is bound but not yet listening or connected,
// srt_setsockopt accepts SRTO_RCVLATENCY, SRTO_PEERLATENCY, SRTO_LATENCY,
// SRTO_TSBPDMODE, SRTO_TLPKTDROP and SRTO_NAKREPORT and each new value reads
// back, while on that same socket the pre-bind option SRTO_IPV6ONLY is refused
// with SRT_EBOUNDSOCK."
//
// Both directions live on ONE socket inside each case, which is what makes the
// pair un-fakeable: a tree that still locks options at bind (v1.4.2's
// behaviour) fails the first case, and a tree that locks nothing at all fails
// the second.
// ===========================================================================

// RQ-07-08, the accepted half. Each option is set to a value different from its
// default and then read back, so "accepted" is never scored on a return code
// alone.
SRT_CASE(g08_bind_boundary, a_bound_socket_accepts_and_reads_back_the_six_pre_connect_options) {
    SRTSOCKET s = st_bound_socket();

    // Distinguishable from the v1.4.3 defaults: RCVLATENCY 120, PEERLATENCY 0,
    // TSBPDMODE true, TLPKTDROP true, NAKREPORT true (RQ-07-01 and the config
    // defaults). Setting the value it already has would pass a tree that
    // dropped the set on the floor.
    st_expect_set_ok(s, SRTO_RCVLATENCY, 250, "SRTO_RCVLATENCY on a bound socket");
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_RCVLATENCY, "SRTO_RCVLATENCY readback"), 250);

    st_expect_set_ok(s, SRTO_PEERLATENCY, 333, "SRTO_PEERLATENCY on a bound socket");
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_PEERLATENCY, "SRTO_PEERLATENCY readback"), 333);

    // SRTO_LATENCY is a setter for the other two (RQ-07-01); here it must be
    // accepted at all, and both peers must move with it.
    st_expect_set_ok(s, SRTO_LATENCY, 500, "SRTO_LATENCY on a bound socket");
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_LATENCY, "SRTO_LATENCY readback"), 500);
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_RCVLATENCY, "SRTO_RCVLATENCY after SRTO_LATENCY=500"), 500);
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_PEERLATENCY, "SRTO_PEERLATENCY after SRTO_LATENCY=500"), 500);

    st_expect_set_ok(s, SRTO_TSBPDMODE, 0, "SRTO_TSBPDMODE=0 on a bound socket");
    ST_REQUIRE_MSG(st_read_bool_loose(s, SRTO_TSBPDMODE, "SRTO_TSBPDMODE readback") == 0,
                   "SRTO_TSBPDMODE on a bound socket: expected the value just set, 0");

    st_expect_set_ok(s, SRTO_TLPKTDROP, 0, "SRTO_TLPKTDROP=0 on a bound socket");
    ST_REQUIRE_MSG(st_read_bool_loose(s, SRTO_TLPKTDROP, "SRTO_TLPKTDROP readback") == 0,
                   "SRTO_TLPKTDROP on a bound socket: expected the value just set, 0 (the "
                   "v1.4.2 default is 1)");

    st_expect_set_ok(s, SRTO_NAKREPORT, 0, "SRTO_NAKREPORT=0 on a bound socket");
    ST_REQUIRE_MSG(st_read_bool_loose(s, SRTO_NAKREPORT, "SRTO_NAKREPORT readback") == 0,
                   "SRTO_NAKREPORT on a bound socket: expected the value just set, 0");

    srt_close(s);
}

// RQ-07-08, the refused half, on a socket in the same state. SRTO_IPV6ONLY is
// SRTO_R_PREBIND and is refused the moment srt_bind succeeds, whereas the six
// above are SRTO_R_PRE and are not. The control is a pre-bind option accepted
// BEFORE the bind on a second socket, so the refusal is the bind and not the
// option.
SRT_CASE(g08_bind_boundary, a_bound_socket_refuses_the_pre_bind_option_with_eboundsock) {
    // Control 1: SRTO_IPV6ONLY is accepted on an UNBOUND socket, so the refusal
    // below is attributable to the bind.
    {
        SRTSOCKET u = st_new_socket();
        ST_REQUIRE_MSG(u != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
        st_expect_set_ok(u, SRTO_IPV6ONLY, 1, "SRTO_IPV6ONLY on an unbound socket");
        ST_REQUIRE_EQ_INT(st_read_int(u, SRTO_IPV6ONLY, "SRTO_IPV6ONLY readback"), 1);
        srt_close(u);
    }

    // The subject: the same option, the same value, on the same socket once it
    // is bound.
    SRTSOCKET s = st_bound_socket();
    st_expect_set_refused(s, SRTO_IPV6ONLY, 1, SRT_EBOUNDSOCK, "SRTO_IPV6ONLY on a bound socket");

    // Control 2: the bound socket is otherwise usable. If the bind had left the
    // socket broken, the refusal above would be reporting the wrong thing.
    st_expect_set_ok(s, SRTO_RCVLATENCY, 250, "SRTO_RCVLATENCY on the same bound socket");
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_RCVLATENCY, "SRTO_RCVLATENCY readback"), 250);

    // Control 3: the bound socket is NOT yet listening or connected, so the
    // pre-connect refusal does not apply here. SRTO_PACKETFILTER is SRTO_R_PRE,
    // and on this socket it must be accepted -- a tree that refused it in the
    // bound state (the v1.4.2 behaviour for this option was to accept it, so
    // the mistake to catch here is refusing everything once bound) fails right
    // here with SRT_ECONNSOCK.
    {
        const char* pf = "fec,cols:10";
        const int rc = srt_setsockflag(s, SRTO_PACKETFILTER, pf, (int)std::strlen(pf));
        const int got = srt_getlasterror(nullptr);
        ST_REQUIRE_MSG(rc != SRT_ERROR,
                       "SRTO_PACKETFILTER on a bound-but-not-listening socket must be accepted, "
                       "got rc %d (%s, code %d)",
                       rc, srt_getlasterror_str(), got);
    }
    srt_close(s);
}

// ===========================================================================
// g09_listen_boundary -- RQ-07-09 (deltas/07.md D13)
//
// "on a listening socket srt_setsockopt refuses SRTO_CONNTIMEO, SRTO_STREAMID,
// SRTO_PAYLOADSIZE, SRTO_PACKETFILTER, SRTO_RETRANSMITALGO, SRTO_FC,
// SRTO_CONGESTION, SRTO_MESSAGEAPI and SRTO_RCVLATENCY with SRT_ECONNSOCK,
// while the unrestricted SRTO_RCVSYN and SRTO_RCVTIMEO are still accepted."
//
// Every one of the nine is refused with SRT_ECONNSOCK, and the code is the
// whole point: at v1.4.2 SRTO_RCVLATENCY, SRTO_FC and SRTO_PACKETFILTER happen
// to be refused on a listening socket too, but as SRT_EBOUNDSOCK, and the other
// six are not refused at all. Asserting the code rather than SRT_ERROR is what
// separates the two tags here.
// ===========================================================================

// RQ-07-09, the nine refusals. The values are all legal in range, so the code
// that comes back is the phase and not the value -- SRTO_FC gets -1 here
// deliberately, which RQ-07-06 shows is below its minimum on a fresh socket.
SRT_CASE(g09_listen_boundary, a_listening_socket_refuses_the_nine_pre_connect_options_with_econnsock) {
    // Half of g09's discrimination is that SRTO_FC=-1 is refused for the PHASE
    // rather than the range: the same call on a fresh socket is SRT_EINVPARAM,
    // so a tree that checked the range first and threw would fail right here.
    {
        SRTSOCKET fresh = st_new_socket();
        ST_REQUIRE_MSG(fresh != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
        st_expect_set_refused(fresh, SRTO_FC, -1, SRT_EINVPARAM,
                              "SRTO_FC=-1 on a fresh socket (a range refusal)");
        srt_close(fresh);
    }

    SRTSOCKET s = st_listening_socket();

    struct Refused {
        SRT_SOCKOPT opt;
        int         value;
        const char* name;
    };
    const Refused subjects[] = {
        {SRTO_CONNTIMEO, 3000, "SRTO_CONNTIMEO"},
        {SRTO_PAYLOADSIZE, 1316, "SRTO_PAYLOADSIZE"},
        {SRTO_RETRANSMITALGO, 0, "SRTO_RETRANSMITALGO"},
        {SRTO_FC, 100, "SRTO_FC"},
        {SRTO_MESSAGEAPI, 0, "SRTO_MESSAGEAPI"},
        {SRTO_RCVLATENCY, 250, "SRTO_RCVLATENCY"},
    };
    for (const Refused& r : subjects)
        st_expect_set_refused(s, r.opt, r.value, SRT_ECONNSOCK, r.name);

    // SRTO_FC with a value the fresh socket refuses for the wrong reason: on
    // the listening socket the phase is checked first (core.cpp:366-371), so
    // -1 must come back as SRT_ECONNSOCK and not as SRT_EINVPARAM.
    st_expect_set_refused(s, SRTO_FC, -1, SRT_ECONNSOCK,
                          "SRTO_FC=-1 on a listening socket (phase before range)");

    // The three string options. Each is refused for the phase, so the value is
    // a legal one for the option in every other respect.
    {
        const char* sid = "stream-1";
        const int rc = srt_setsockflag(s, SRTO_STREAMID, sid, (int)std::strlen(sid));
        const int got = srt_getlasterror(nullptr);
        ST_REQUIRE_MSG(rc == SRT_ERROR, "SRTO_STREAMID on a listening socket returned %d, expected refusal",
                       rc);
        ST_REQUIRE_MSG(got == SRT_ECONNSOCK, "SRTO_STREAMID on a listening socket: expected SRT_ECONNSOCK "
                                             "(%d), got %d (%s)",
                       SRT_ECONNSOCK, got, srt_strerror(got, 0));
    }
    {
        const char* cong = "live";
        const int rc = srt_setsockflag(s, SRTO_CONGESTION, cong, (int)std::strlen(cong));
        const int got = srt_getlasterror(nullptr);
        ST_REQUIRE_MSG(rc == SRT_ERROR, "SRTO_CONGESTION on a listening socket returned %d, expected refusal",
                       rc);
        ST_REQUIRE_MSG(got == SRT_ECONNSOCK, "SRTO_CONGESTION on a listening socket: expected SRT_ECONNSOCK "
                                             "(%d), got %d (%s)",
                       SRT_ECONNSOCK, got, srt_strerror(got, 0));
    }
    {
        const char* pf = "fec,cols:10";
        const int rc = srt_setsockflag(s, SRTO_PACKETFILTER, pf, (int)std::strlen(pf));
        const int got = srt_getlasterror(nullptr);
        ST_REQUIRE_MSG(rc == SRT_ERROR, "SRTO_PACKETFILTER on a listening socket returned %d, expected "
                                        "refusal",
                       rc);
        ST_REQUIRE_MSG(got == SRT_ECONNSOCK, "SRTO_PACKETFILTER on a listening socket: expected SRT_ECONNSOCK "
                                             "(%d), got %d (%s)",
                       SRT_ECONNSOCK, got, srt_strerror(got, 0));
    }

    // The listening socket is still usable: srt_close must not fail, and the
    // state that caused the refusals must not have been disturbed by them.
    ST_REQUIRE_MSG(srt_close(s) != SRT_ERROR, "srt_close on the listening socket: %s",
                   srt_getlasterror_str());
}

// RQ-07-09, the contrast case: SRTO_RCVSYN and SRTO_RCVTIMEO have no flags
// entry at all, so they are accepted in every state -- including this one. This
// is the positive control that keeps the case above from being satisfied by a
// library that refuses everything on a listening socket.
SRT_CASE(g09_listen_boundary, the_two_unrestricted_options_are_still_accepted_on_a_listening_socket) {
    SRTSOCKET s = st_listening_socket();

    // RCVTIMEO is also what the kit uses to bound every receive, so a tree that
    // refused it here would break the transfer groups too; asserting it reads
    // back is asserting the value, not the return code.
    st_expect_set_ok(s, SRTO_RCVTIMEO, 2500, "SRTO_RCVTIMEO on a listening socket");
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_RCVTIMEO, "SRTO_RCVTIMEO readback"), 2500);

    st_expect_set_ok(s, SRTO_RCVSYN, 0, "SRTO_RCVSYN=0 on a listening socket");
    ST_REQUIRE_MSG(st_read_bool_loose(s, SRTO_RCVSYN, "SRTO_RCVSYN readback") == 0,
                   "SRTO_RCVSYN on a listening socket: expected the value just set, 0");

    // And both are still accepted after the option calls that this group's
    // sibling case shows to be refused, so the acceptance is not an artefact of
    // the socket having seen no refused call.
    st_expect_set_refused(s, SRTO_FC, 100, SRT_ECONNSOCK, "SRTO_FC on a listening socket");
    st_expect_set_ok(s, SRTO_RCVTIMEO, 1500, "SRTO_RCVTIMEO after a refused option");
    ST_REQUIRE_EQ_INT(st_read_int(s, SRTO_RCVTIMEO, "SRTO_RCVTIMEO readback"), 1500);

    srt_close(s);
}
