// fam_stability.cpp -- step 09, `stability` family.
//
// The minimum link stability timeout, reached the way the candidate is told to
// reach it in TEST_INTERFACE.md sections 3-5: by its *numeric position in
// SRT_SOCKOPT*, never by the enumerator's name. stepdefs/09/interface.md and
// REQUIREMENTS.md RQ-09-01..03 both give that position as 58, so this
// file spells no group option identifier at all and declares no group symbol --
// it compiles and runs against any tree, and fails on an assertion rather than
// a build, which is what makes it a measurement.
//
// Why the number and not the name: `SRTO_GROUPMINSTABLETIMEO` does not exist in
// v1.4.4's <srt/srt.h> at all (the name there is `SRTO_GROUPSTABTIMEO`,
// unguarded-but-renamed and carrying no explicit value, so it is not a
// restatable constant), and naming it would cost the whole family its three
// groups at the previous milestone for a build reason. The enumeration's
// numbering is stated in interface.md as part of the contract, and case
// `minstab_position` measures it rather than trusting it.
//
// `SRT_EINVPARAM`, `SRT_ECONNSOCK` and `SRT_EINVOP` are present and identical
// in both trees, so naming them costs nothing: 1003, 1002 and 5000.
//
// Every case below traces to a numbered requirement in REQUIREMENTS.md:
//
//   g01_minstab_get   RQ-09-01   (a fresh socket reads 60 with an out-length of 4)
//   g02_minstab_set   RQ-09-02   (the 60 ms floor, the strict peer-idle ceiling, read-back)
//   g03_minstab_pre   RQ-09-03   (R_PRE: bound-but-not-listening accepts, listening refuses)
//
// No case is written that interface.md does not state behaviour for. In
// particular nothing here times a failover: interface.md section 5's last
// paragraph says the option is checked as an accessor only. Nothing under
// `deltas/09.md` is scored from this file either -- D5-D13 are the `group` and
// `live` families' rows, and D1-D4 are the three groups above.

#include "stest.h"
#include "srtkit.h"

#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// The subject: position 58 of `SRT_SOCKOPT`.
//
// **Measured, and the shipped document agrees.** interface.md section 5 opens
// by giving the position as 58, with "the bind-to-device option is 56, group
// connection is 57, group type is 59, the packet filter option is 60". So does
// every other document in this package: `deltas/09.md` D1 spells
// `srt_getsockflag(fresh, 58, ...)`, `bonding-delta-09.md` and
// `behaviour-delta-09-11.md:71` both give `SRTO_GROUPMINSTABLETIMEO == 58`, and
// REQUIREMENTS.md's Step 09 freeze note records the same 56/57/58/59/60/61
// layout. (An earlier edit moved interface.md's sentence to 57; it was the
// defect and it has been retracted -- REVIEW_LOG.md, "Two documents were
// wrong". Nothing shipped says 57 now, so do not "correct" it back.) Measured
// against the v1.5.0 install prefix, that is what the header and the library
// actually do:
//
//     get 57 -> 0, set 57=10 succeeds,  set 57=5001 succeeds    (a boolean)
//     get 58 -> 60, set 58=10 refused with 5003, 5001 with 5003 (the subject)
//
// Option 57 is the *group connection* flag and has no floor at all, so a driver
// written to 57 would score a tree that implements nothing in this milestone --
// exactly the invisible-pass failure DRIVER_RULES.md calls the worst this
// harness can have. The number below is therefore 58 -- the measured value and
// the documented one -- and case `minstab_position` re-derives it at run time
// rather than trusting either this comment or the document.
// ---------------------------------------------------------------------------
static const int kMinStabOpt    = 58;   // the subject (interface.md and measurement agree)
static const int kGroupTypeOpt  = 59;   // the option immediately after the subject
static const int kPeerIdleOpt   = 55;   // ENFORCEDENCRYPTION 53, IPV6ONLY 54, 55
static const int kPktFilterOpt  = 60;   // `<srt/srt.h>` writes this one as `= 60` at both tags

// The value and the bounds the interface fixes for this release.
static const int kDefMinStabMs  = 60;    // the default, and the floor
static const int kPeerIdleMs    = 5000;  // the peer idle timeout's own default

// ---------------------------------------------------------------------------
// Accessors. `srt_getsockflag`/`srt_setsockflag` report the failure through
// `srt_getlasterror`, and that is what the interface asserts -- "The refusal
// codes are also asserted against what `srt_getlasterror` reports, not against
// the return value alone" (RQ-09-02) -- so each status carries both halves.
// ---------------------------------------------------------------------------
struct StGet {
    int rc;        // whatever srt_getsockflag returned
    int value;     // the integer it wrote, when it returned success
    int len;       // the out-length it wrote
    int err;       // srt_getlasterror(nullptr) at that moment
};

static StGet stab_get(SRTSOCKET s) {
    StGet g;
    g.value = -1;
    g.len   = (int)sizeof(int);
    g.rc    = srt_getsockflag(s, (SRT_SOCKOPT)kMinStabOpt, &g.value, &g.len);
    g.err   = srt_getlasterror(nullptr);
    return g;
}

struct StSet {
    int rc;
    int err;
};

static StSet stab_set(SRTSOCKET s, int v) {
    StSet r;
    r.rc  = srt_setsockflag(s, (SRT_SOCKOPT)kMinStabOpt, &v, (int)sizeof v);
    r.err = srt_getlasterror(nullptr);
    return r;
}

// Read a *different* option, used for the controls that prove the socket is in
// the state the case claims it is and for the neighbour check that pins the
// subject's position. Owns and closes the socket it reads from, so a control
// cannot leave state behind for the case's real subject.
//
// `want_len` is checked only when non-zero: one of the anchors
// (`SRTO_PACKETFILTER`) is a string option and reports an out-length of 0 on a
// fresh socket, which is its correct answer and not a failure.
struct StProbe {
    int rc;      // srt_getsockflag's return
    int value;   // what it wrote, when it returned success
    int len;     // the out-length it wrote
};

static StProbe probe_fresh(int opt, int want_len = 0) {
    StProbe p;
    p.value = -1;
    p.len   = (int)sizeof(int);
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket (probe of option %d): %s", opt,
                   srt_getlasterror_str());
    p.rc = srt_getsockflag(s, (SRT_SOCKOPT)opt, &p.value, &p.len);
    srt_close(s);
    if (p.rc != SRT_ERROR && want_len != 0 && p.len != want_len)
        st_failf(__FILE__, __LINE__,
                 "control option %d reported a length of %d bytes, expected %d", opt, p.len,
                 want_len);
    return p;
}

// An init-state socket closed on every exit path, so a case that fails early
// does not leave a bound port or a listener behind inside the child.
struct StSock {
    SRTSOCKET s;
    StSock() { s = st_new_socket(); ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s",
                                                   srt_getlasterror_str()); }
    ~StSock() { if (s != SRT_INVALID_SOCK) srt_close(s); }
    StSock(const StSock&) = delete;
    StSock& operator=(const StSock&) = delete;
    operator SRTSOCKET() const { return s; }
};

static const char* state_name(SRT_SOCKSTATUS st) {
    switch (st) {
    case SRTS_INIT:       return "INIT";
    case SRTS_OPENED:     return "OPENED";
    case SRTS_LISTENING:  return "LISTENING";
    case SRTS_CONNECTING: return "CONNECTING";
    case SRTS_CONNECTED:  return "CONNECTED";
    case SRTS_BROKEN:     return "BROKEN";
    case SRTS_CLOSING:    return "CLOSING";
    case SRTS_CLOSED:     return "CLOSED";
    case SRTS_NONEXIST:   return "NONEXIST";
    default:              return "?";
    }
}

// ===========================================================================
// g01_minstab_get -- RQ-09-01
//
// "reading the minimum link stability timeout on a fresh unconnected socket
//  succeeds, yields 60 and reports a length of four bytes, instead of being
//  refused with `SRT_EINVOP`"
// ===========================================================================

// The position is a fact about the enumeration that only interface.md gives, so
// it is measured here rather than trusted. This case is the group's guard
// against the failure mode a wrong number would cause: at 57 the subject would
// be the group-connection flag, which has no floor, no ceiling and no `R_PRE`
// restriction of its own, and the two cases below would pass against a tree
// that implements nothing. The check that catches that is the floor -- but a
// floor that is simply absent is only visible if the case says so, which is
// what the last assertion here does.
SRT_CASE(g01_minstab_get, minstab_position) {
    // The anchor: `SRTO_PACKETFILTER = 60` is written explicitly in <srt/srt.h>
    // at both tags, so it fixes the numbering around the subject from the
    // header the driver is actually compiled against.
    StProbe pf = probe_fresh(kPktFilterOpt);
    ST_REQUIRE_MSG(pf.rc != SRT_ERROR,
                   "packet filter (position %d, which <srt/srt.h> writes as `= 60` in both "
                   "milestones) is not readable: %s",
                   kPktFilterOpt, srt_getlasterror_str());

    // The option immediately after the subject. It must exist and be readable
    // on a fresh socket, which is what makes the subject's own slot provable.
    StProbe gt = probe_fresh(kGroupTypeOpt);
    ST_REQUIRE_MSG(gt.rc != SRT_ERROR,
                   "group type (position %d) is not readable on a fresh socket: %s -- the "
                   "enumeration does not have the layout this driver was written to, so the "
                   "stability timeout cannot be assumed to sit at position %d",
                   kGroupTypeOpt, srt_getlasterror_str(), kMinStabOpt);

    // The option before it, pinned to a default both the release and the
    // documents give. A shift of the enum around the subject moves this too, so
    // the check names the neighbour rather than only the subject.
    StProbe idle = probe_fresh(kPeerIdleOpt);
    ST_REQUIRE_MSG(idle.rc != SRT_ERROR,
                   "peer idle timeout (position %d) is not readable: %s", kPeerIdleOpt,
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(idle.value == kPeerIdleMs,
                   "the option at position %d reports %d; position %d is the peer idle timeout "
                   "and its default is %d, so the enumeration's numbering is not the one this "
                   "driver was written to",
                   kPeerIdleOpt, idle.value, kPeerIdleOpt, kPeerIdleMs);

    // The decisive one: the subject must *reject* a value below its floor. The
    // group-connection flag one position earlier accepts every integer, so this
    // is what separates the subject from the option a driver written to the
    // neighbouring position would have been addressing.
    StSock s;
    StSet low = stab_set(s, 10);
    ST_REQUIRE_MSG(low.rc == SRT_ERROR,
                   "position %d accepts 10; the option at %d has no floor, so the subject is not "
                   "where this driver thinks it is",
                   kMinStabOpt, kMinStabOpt);
    ST_REQUIRE_MSG(low.err == SRT_EINVPARAM,
                   "position %d refused 10 with %s (%d), expected SRT_EINVPARAM (%d)",
                   kMinStabOpt, srt_strerror(low.err, 0), low.err, SRT_EINVPARAM);
}

// The group's subject: a fresh socket reads 60, not 80000 and not a refusal.
SRT_CASE(g01_minstab_get, fresh_default_is_60) {
    StSock s;

    // Positive control for "fresh": the socket is INIT, so nothing below can
    // be explained by a state the case did not establish.
    ST_REQUIRE_MSG(srt_getsockstate(s) == SRTS_INIT,
                   "fresh socket is in state %s, expected INIT",
                   state_name(srt_getsockstate(s)));

    StGet g = stab_get(s);
    ST_REQUIRE_MSG(g.rc != SRT_ERROR,
                   "getsockflag(position %d) on a fresh socket failed with %s (%d); the "
                   "milestone makes it readable, expected success and value %d",
                   kMinStabOpt, srt_strerror(g.err, 0), g.err, kDefMinStabMs);
    ST_REQUIRE_MSG(g.value == kDefMinStabMs,
                   "minimum link stability timeout default: expected %d ms, got %d ms",
                   kDefMinStabMs, g.value);
}

// The length half of the same sentence. This is also the half that separates
// the milestone from its predecessor: the previous release's field held
// microseconds and the same accessor path reported `80 * 1000`, so a tree that
// kept the old constant and the old unit answers 80000 here.
SRT_CASE(g01_minstab_get, fresh_out_length_is_four) {
    StSock s;

    StGet g = stab_get(s);
    ST_REQUIRE_MSG(g.rc != SRT_ERROR, "getsockflag(position %d): %s", kMinStabOpt,
                   srt_strerror(g.err, 0));
    ST_REQUIRE_MSG(g.len == (int)sizeof(int),
                   "minimum link stability timeout out-length: expected %d bytes, got %d",
                   (int)sizeof(int), g.len);

    // A 4-byte length carrying the old microsecond value is still wrong, and
    // that is the interesting malformed answer: the right width, the wrong
    // quantity.
    ST_REQUIRE_MSG(g.value != 80000,
                   "minimum link stability timeout reports %d, which is the previous "
                   "milestone's microsecond value, not this one's %d ms",
                   g.value, kDefMinStabMs);
}

// The refusal the description names. The code is the asserted shape and not
// incidental: a tree that has not declared the option at all answers with
// SRT_EINVOP, and that is a *different* failure from a wrong default.
//
// The success of the same call is left to the three cases above rather than
// repeated here. Repeating it would blunt this case: its mutant is "the tree
// answers EINVOP", and an added "and it succeeded" clause makes the case fire
// on the mutant's rc rather than on the code it exists to distinguish.
SRT_CASE(g01_minstab_get, not_refused_with_einvop) {
    StSock s;

    StGet g = stab_get(s);
    ST_REQUIRE_MSG(!(g.rc == SRT_ERROR && g.err == SRT_EINVOP),
                   "getsockflag(position %d) was refused with SRT_EINVOP (%d): the option is "
                   "not handled by this tree at all, so it cannot be read at any value",
                   kMinStabOpt, g.err);
}

// The default is per socket, not in the option table. Without this, a case
// could pass against storage that a previous set had already written.
SRT_CASE(g01_minstab_get, default_is_per_socket) {
    StSock a;
    StSock b;

    StSet set_a = stab_set(a, 1000);
    ST_REQUIRE_MSG(set_a.rc != SRT_ERROR, "setsockflag(1000) on socket A failed: %s",
                   srt_strerror(set_a.err, 0));

    StGet ga = stab_get(a);
    ST_REQUIRE_MSG(ga.rc != SRT_ERROR && ga.value == 1000,
                   "socket A reads %d after being set to 1000: %s", ga.value,
                   srt_strerror(ga.err, 0));

    StGet gb = stab_get(b);
    ST_REQUIRE_MSG(gb.rc != SRT_ERROR, "getsockflag on socket B: %s", srt_strerror(gb.err, 0));
    ST_REQUIRE_MSG(gb.value == kDefMinStabMs,
                   "a socket created after another was set to 1000 reports %d, expected the "
                   "default %d: the storage is shared between sockets",
                   gb.value, kDefMinStabMs);
}

// ===========================================================================
// g02_minstab_set -- RQ-09-02
//
// "setting the minimum link stability timeout on a fresh socket refuses 10 and
//  59 with `SRT_EINVPARAM`, accepts 60 and accepts 5000 while the peer idle
//  timeout is at its 5000 default, and reads each accepted value back
//  unchanged"
// ===========================================================================

// The floor, on one socket, in one case, because the point is the boundary:
// 10 and 59 are refused and 60 is accepted, so a candidate who refused
// everything up to and including 60 fails here and nowhere else.
SRT_CASE(g02_minstab_set, floor_is_60_inclusive) {
    StSock s;

    // Positive control for the ceiling this case bounds against: the idle
    // timeout is its documented default, so an accepted 60 cannot be an
    // accident of a raised ceiling.
    StProbe idle = probe_fresh(kPeerIdleOpt);
    ST_REQUIRE_MSG(idle.rc != SRT_ERROR, "peer idle timeout (position %d): %s", kPeerIdleOpt,
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(idle.value == kPeerIdleMs,
                   "case precondition: peer idle timeout is %d, expected the default %d",
                   idle.value, kPeerIdleMs);

    StSet lo = stab_set(s, 10);
    ST_REQUIRE_MSG(lo.rc == SRT_ERROR,
                   "setsockflag(position %d, 10) succeeded; the floor is %d ms so 10 must be "
                   "refused",
                   kMinStabOpt, kDefMinStabMs);
    ST_REQUIRE_MSG(lo.err == SRT_EINVPARAM,
                   "setsockflag(10) refusal: expected SRT_EINVPARAM (%d), got %s (%d)",
                   SRT_EINVPARAM, srt_strerror(lo.err, 0), lo.err);

    StSet below = stab_set(s, 59);
    ST_REQUIRE_MSG(below.rc == SRT_ERROR,
                   "setsockflag(59) succeeded; 59 ms is below the %d ms floor and must be "
                   "refused", kDefMinStabMs);
    ST_REQUIRE_MSG(below.err == SRT_EINVPARAM,
                   "setsockflag(59) refusal: expected SRT_EINVPARAM (%d), got %s (%d)",
                   SRT_EINVPARAM, srt_strerror(below.err, 0), below.err);

    StSet at = stab_set(s, kDefMinStabMs);
    ST_REQUIRE_MSG(at.rc != SRT_ERROR,
                   "setsockflag(%d) was refused with %s (%d); the floor itself is legal",
                   kDefMinStabMs, srt_strerror(at.err, 0), at.err);
}

// The ceiling, and the strictness that makes it interesting: a value *equal*
// to the peer idle timeout is accepted and a strictly greater one is not.
// interface.md states both ("a value equal to it is accepted").
SRT_CASE(g02_minstab_set, ceiling_is_strict_at_idle_timeout) {
    StSock s;

    StProbe idle = probe_fresh(kPeerIdleOpt);
    ST_REQUIRE_MSG(idle.rc != SRT_ERROR, "peer idle timeout (position %d): %s", kPeerIdleOpt,
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(idle.value == kPeerIdleMs,
                   "case precondition: peer idle timeout is %d, expected the default %d",
                   idle.value, kPeerIdleMs);

    StSet eq = stab_set(s, idle.value);
    ST_REQUIRE_MSG(eq.rc != SRT_ERROR,
                   "setsockflag(%d) was refused with %s (%d); a value equal to the peer idle "
                   "timeout is accepted, only a strictly greater one is not",
                   idle.value, srt_strerror(eq.err, 0), eq.err);

    StSet over = stab_set(s, idle.value + 1);
    ST_REQUIRE_MSG(over.rc == SRT_ERROR,
                   "setsockflag(%d) succeeded; %d exceeds the peer idle timeout in force and "
                   "must be refused",
                   idle.value + 1, idle.value + 1);
    ST_REQUIRE_MSG(over.err == SRT_EINVPARAM,
                   "setsockflag(%d) refusal: expected SRT_EINVPARAM (%d), got %s (%d)",
                   idle.value + 1, SRT_EINVPARAM, srt_strerror(over.err, 0), over.err);
}

// The read-back, which is what makes the accepted half of the two cases above
// a statement about stored state rather than about a return value. 60 is
// excluded on purpose: it is the default, so reading it back would prove
// nothing. 1000 and 5000 are both values the default cannot produce.
SRT_CASE(g02_minstab_set, accepted_values_read_back) {
    StSock s;

    const int vals[2] = { 1000, 5000 };
    for (int i = 0; i < 2; i++) {
        StSet r = stab_set(s, vals[i]);
        ST_REQUIRE_MSG(r.rc != SRT_ERROR, "setsockflag(%d) failed: %s", vals[i],
                       srt_strerror(r.err, 0));
        StGet g = stab_get(s);
        ST_REQUIRE_MSG(g.rc != SRT_ERROR, "getsockflag after setting %d failed: %s", vals[i],
                       srt_strerror(g.err, 0));
        ST_REQUIRE_MSG(g.value == vals[i],
                       "read-back after setting %d: expected %d, got %d (the set path and the "
                       "get path disagree about the field)",
                       vals[i], vals[i], g.value);
    }

    // The setter stores; it does not write through to the peer idle timeout. A
    // candidate who implemented the ceiling by copying the value into the idle
    // timeout would otherwise pass every assertion above.
    StProbe idle = probe_fresh(kPeerIdleOpt);
    ST_REQUIRE_MSG(idle.rc != SRT_ERROR, "peer idle timeout (position %d): %s", kPeerIdleOpt,
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(idle.value == kPeerIdleMs,
                   "setting the stability timeout to 5000 moved the peer idle timeout to %d; "
                   "expected it to remain %d",
                   idle.value, kPeerIdleMs);
}

// A refused set leaves the previous value alone. Without this, a candidate who
// validated *after* storing would pass every case above and still corrupt the
// field.
SRT_CASE(g02_minstab_set, refusal_does_not_disturb_the_field) {
    StSock s;

    StSet ok = stab_set(s, 1000);
    ST_REQUIRE_MSG(ok.rc != SRT_ERROR, "setsockflag(1000): %s", srt_strerror(ok.err, 0));
    StGet before = stab_get(s);
    ST_REQUIRE_MSG(before.rc != SRT_ERROR && before.value == 1000,
                   "read-back after setting 1000: expected 1000, got %d", before.value);

    StSet bad = stab_set(s, 59);
    ST_REQUIRE_MSG(bad.rc == SRT_ERROR && bad.err == SRT_EINVPARAM,
                   "setsockflag(59) refusal: expected failure with SRT_EINVPARAM (%d), got "
                   "rc=%d err=%s (%d)",
                   SRT_EINVPARAM, bad.rc, srt_strerror(bad.err, 0), bad.err);

    StGet after = stab_get(s);
    ST_REQUIRE_MSG(after.rc != SRT_ERROR, "getsockflag after a refused set: %s",
                   srt_strerror(after.err, 0));
    ST_REQUIRE_MSG(after.value == 1000,
                   "a refused set changed the stored value: expected 1000, got %d", after.value);
}

// ===========================================================================
// g03_minstab_pre -- RQ-09-03
//
// "the minimum link stability timeout is still accepted on a socket that is
//  bound but not yet listening, and is refused with `SRT_ECONNSOCK` once
//  `srt_listen` has been called on that same socket"
// ===========================================================================

// The bound half. The option is R_PRE and not R_PREBIND, so binding does not
// end it -- and this is the half a candidate who read only the option table's
// `pre` restriction and implemented it at the bind path gets wrong.
SRT_CASE(g03_minstab_pre, bound_still_accepts) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // A real ephemeral port, so the state is genuinely bound and not merely
    // "no longer INIT".
    sockaddr_in la = st_loopback(0);
    if (srt_bind(s, (sockaddr*)&la, sizeof la) == SRT_ERROR) {
        int e = srt_getlasterror(nullptr);
        srt_close(s);
        ST_REQUIRE_MSG(false, "srt_bind: %s (%d)", srt_strerror(e, 0), e);
    }
    int nl = (int)sizeof la;
    if (srt_getsockname(s, (sockaddr*)&la, &nl) == SRT_ERROR) {
        int e = srt_getlasterror(nullptr);
        srt_close(s);
        ST_REQUIRE_MSG(false, "srt_getsockname: %s (%d)", srt_strerror(e, 0), e);
    }
    ST_REQUIRE_MSG(ntohs(la.sin_port) != 0,
                   "the bound socket has port 0; the case did not establish a bound state");
    ST_REQUIRE_MSG(srt_getsockstate(s) == SRTS_OPENED,
                   "bound socket is in state %s, expected OPENED",
                   state_name(srt_getsockstate(s)));

    StSet r = stab_set(s, 100);
    srt_close(s);
    ST_REQUIRE_MSG(r.rc != SRT_ERROR,
                   "setsockflag(position %d, 100) on a bound, not-yet-listening socket was "
                   "refused with %s (%d); the option is a pre-connect option and binding does "
                   "not end it",
                   kMinStabOpt, srt_strerror(r.err, 0), r.err);
}

// The listening half, and the code. The refusal is SRT_ECONNSOCK and not
// SRT_EINVPARAM or SRT_EBOUNDSOCK, which is the distinction the description
// names and which TEST_INTERFACE.md states twice: §4's `minstab-pre` row and
// §5's "refusals are specific" paragraph both name SRT_ECONNSOCK for a
// listening socket. The code is asserted because it is documented -- do not
// weaken this to a bare SRT_ERROR.
SRT_CASE(g03_minstab_pre, listening_refuses_with_econnsock) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    sockaddr_in la = st_loopback(0);
    if (srt_bind(s, (sockaddr*)&la, sizeof la) == SRT_ERROR) {
        int e = srt_getlasterror(nullptr);
        srt_close(s);
        ST_REQUIRE_MSG(false, "srt_bind: %s (%d)", srt_strerror(e, 0), e);
    }

    // The same socket, the same value, accepted a moment ago: the positive
    // control for this negative is inside the case, so the refusal cannot be
    // attributed to the value or to the socket.
    StSet before = stab_set(s, 100);
    ST_REQUIRE_MSG(before.rc != SRT_ERROR,
                   "precondition: setsockflag(100) before srt_listen was refused with %s (%d)",
                   srt_strerror(before.err, 0), before.err);

    if (srt_listen(s, 1) == SRT_ERROR) {
        int e = srt_getlasterror(nullptr);
        srt_close(s);
        ST_REQUIRE_MSG(false, "srt_listen: %s (%d)", srt_strerror(e, 0), e);
    }
    ST_REQUIRE_MSG(srt_getsockstate(s) == SRTS_LISTENING,
                   "socket is in state %s after srt_listen, expected LISTENING",
                   state_name(srt_getsockstate(s)));

    StSet after = stab_set(s, 100);
    ST_REQUIRE_MSG(after.rc == SRT_ERROR,
                   "setsockflag(position %d, 100) on a listening socket succeeded; the option "
                   "is refused once srt_listen has been called",
                   kMinStabOpt);
    ST_REQUIRE_MSG(after.err == SRT_ECONNSOCK,
                   "listening refusal: expected SRT_ECONNSOCK (%d), got %s (%d)",
                   SRT_ECONNSOCK, srt_strerror(after.err, 0), after.err);

    // The stored value survived: the refusal is a state restriction and not a
    // rejection of the number, so the listener still holds the 100 it accepted.
    StGet g = stab_get(s);
    int close_err = g.err;
    srt_close(s);
    ST_REQUIRE_MSG(g.rc != SRT_ERROR, "the option became unreadable after srt_listen: %s (%d)",
                   srt_strerror(close_err, 0), close_err);
    ST_REQUIRE_MSG(g.value == 100,
                   "after the listening refusal the stored value is %d, expected the 100 "
                   "accepted before srt_listen",
                   g.value);
}

// The refusal's reach: it is a property of the socket's state, not of the
// number, so every value the option would otherwise accept is refused once the
// socket is listening. The two ends of the legal range are used -- the floor
// and the value equal to the peer idle timeout -- because a candidate who
// enforced the phase rule only for some values would still pass a case that
// tried one value.
//
// Deliberately only LEGAL values. An out-of-range value on a listening socket
// violates two rules at once, and which error it reports depends on the order
// in which the implementation evaluates them. interface.md states both
// refusals unconditionally and states no precedence between them, and a
// precedence is a fact about upstream's `CUDT::setOpt` rather than about this
// milestone's contract, so asserting it would fail an implementation that
// validates the argument first and is otherwise correct. Round 1 finding F2;
// see REVIEW_LOG.md.
SRT_CASE(g03_minstab_pre, state_restriction_applies_to_every_legal_value) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    sockaddr_in la = st_loopback(0);
    if (srt_bind(s, (sockaddr*)&la, sizeof la) == SRT_ERROR) {
        int e = srt_getlasterror(nullptr);
        srt_close(s);
        ST_REQUIRE_MSG(false, "srt_bind: %s (%d)", srt_strerror(e, 0), e);
    }

    // The positive controls, on this socket and before srt_listen: both ends
    // of the legal range are accepted here, so the refusals below cannot be
    // attributed to the values.
    StSet pre_floor = stab_set(s, kDefMinStabMs);
    StSet pre_ceil  = stab_set(s, kPeerIdleMs);
    if (pre_floor.rc == SRT_ERROR || pre_ceil.rc == SRT_ERROR) {
        srt_close(s);
        ST_REQUIRE_MSG(false,
                       "precondition: on a bound socket setsockflag(%d) reported %s (%d) and "
                       "setsockflag(%d) reported %s (%d); both are legal values before "
                       "srt_listen",
                       kDefMinStabMs, srt_strerror(pre_floor.err, 0), pre_floor.err,
                       kPeerIdleMs, srt_strerror(pre_ceil.err, 0), pre_ceil.err);
    }

    if (srt_listen(s, 1) == SRT_ERROR) {
        int e = srt_getlasterror(nullptr);
        srt_close(s);
        ST_REQUIRE_MSG(false, "srt_listen: %s (%d)", srt_strerror(e, 0), e);
    }
    ST_REQUIRE_MSG(srt_getsockstate(s) == SRTS_LISTENING,
                   "socket is in state %s after srt_listen, expected LISTENING",
                   state_name(srt_getsockstate(s)));

    StSet floor_v = stab_set(s, kDefMinStabMs);   // the floor: legal off a listener
    StSet mid     = stab_set(s, 100);             // mid-range: legal off a listener
    StSet ceil_v  = stab_set(s, kPeerIdleMs);     // equal to the ceiling: legal off a listener
    srt_close(s);

    ST_REQUIRE_MSG(floor_v.rc == SRT_ERROR && floor_v.err == SRT_ECONNSOCK,
                   "setsockflag(%d) on a listening socket: expected SRT_ECONNSOCK (%d), got "
                   "rc=%d err=%s (%d) -- %d is the floor and is accepted on the same socket "
                   "before srt_listen, so the refusal is the state restriction",
                   kDefMinStabMs, SRT_ECONNSOCK, floor_v.rc, srt_strerror(floor_v.err, 0),
                   floor_v.err, kDefMinStabMs);

    ST_REQUIRE_MSG(mid.rc == SRT_ERROR && mid.err == SRT_ECONNSOCK,
                   "setsockflag(100) on a listening socket: expected SRT_ECONNSOCK (%d), got "
                   "rc=%d err=%s (%d)",
                   SRT_ECONNSOCK, mid.rc, srt_strerror(mid.err, 0), mid.err);

    ST_REQUIRE_MSG(ceil_v.rc == SRT_ERROR && ceil_v.err == SRT_ECONNSOCK,
                   "setsockflag(%d) on a listening socket: expected SRT_ECONNSOCK (%d), got "
                   "rc=%d err=%s (%d) -- %d equals the peer idle timeout and is accepted on "
                   "the same socket before srt_listen",
                   kPeerIdleMs, SRT_ECONNSOCK, ceil_v.rc, srt_strerror(ceil_v.err, 0),
                   ceil_v.err, kPeerIdleMs);
}
