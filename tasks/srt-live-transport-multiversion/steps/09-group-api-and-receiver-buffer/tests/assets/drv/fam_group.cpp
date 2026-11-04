// fam_group.cpp -- step 09, `group` family.
//
// Five SCORED groups, all tier `new`: the four group-API groups from
// scored.txt, plus `g00_endtoend` (endtoend.txt, E2E_TIER=new), which was moved
// here from the `live` family because it needs the group API and so cannot
// compile at the no-op prefix. Nothing in this file is a GATE group -- the
// step's gate is proof of life plus the three `inherited` groups in
// fam_live.cpp (`g10_recovery`, `g11_tsbpd`, `g12_encryption`), which build at
// both prefixes:
//
//   g04_group_default  RQ-09-04 / deltas/09.md D5 -- a group with no member
//                      sockets answers the minimum link stability timeout
//                      getter with 60 and a length of 4, instead of refusing
//                      with SRT_EINVPARAM.
//   g05_group_set      RQ-09-05 / D6 -- the group's OWN setter (a second
//                      implementation, not a delegation to the socket one)
//                      refuses 10 and 59 with SRT_EINVPARAM, accepts 60 and
//                      5000, and the memberless getter keeps answering 60
//                      whatever it accepted.
//   g06_group_pruned   RQ-09-06 / D7 -- of the five group functions the
//                      previous milestone exported, exactly `srt_create_group`
//                      and `srt_connect_group` still resolve at run time;
//                      `srt_include`, `srt_exclude` and `srt_group_configure`
//                      resolve to nothing.
//   g07_group_types    RQ-09-07 / D8 -- SRT_GROUP_TYPE names undefined,
//                      broadcast and backup as 0, 1 and 2 with an end marker
//                      of 3, and `srt_create_group` returns a group id that
//                      `srt_connect_group` accepts for both named types.
//
// ---------------------------------------------------------------- tiers
//
// Every group here is `new`. This file therefore does NOT have to compile
// against v1.4.4 and is deliberately not contorted to: DRIVER_RULES' "dlsym
// the function, restate the constant" machinery is a device for keeping an
// `inherited` group earnable at the previous prefix, and there is no
// `inherited` group in this family. A `new` group that still builds and passes
// at v1.4.4 is a group that fails to discriminate, which is worse than one
// that does not build there.
//
// Concretely, at v1.4.4 `<srt/srt.h>` puts the whole group API behind
// `#if ENABLE_EXPERIMENTAL_BONDING` (srt.h:164-165 self-default it to 0, and
// the block runs srt.h:772-835), and this translation unit defines no such
// macro -- interface.md is explicit that "the group API must be declared by
// the public header without the including translation unit defining any macro
// first". So `SRT_GROUP_TYPE`, `SRT_GTYPE_*`, `srt_create_group`,
// `srt_prepare_endpoint`, `SRT_SOCKGROUPCONFIG` and `srt_connect_group` are all
// undeclared there and the family fails to build. That is the intended
// outcome at the previous milestone and is reported as such, not as a defect.
//
// At v1.5.0 the same declarations are unconditional (srt.h:939-995) and the
// three group option positions sit in an `SRT_SOCKOPT` enum with no
// preprocessor conditional of any kind, so nothing needs defining to see them.
//
// One use of `dlsym` remains, and it is a subject rather than a compatibility
// device: g06's whole claim is about what the installed shared library
// EXPORTS. A driver that declared `extern "C" int srt_include(SRTSOCKET,
// SRTSOCKET);` would assert the same thing with a link error, which costs the
// other three groups their measurement. Resolution keeps the failure inside
// g06.
//
// ------------------------------------------------ reaching the option
//
// Socket options are reached by their NUMERIC POSITION, per interface.md §5's
// opening paragraph and exactly as `fam_stability.cpp` does it. This file
// spells no `SRTO_*` identifier for the stability-timeout option itself: the
// enum member names changed
// across the chain (`SRTO_GROUPSTABTIMEO` at v1.4.4, `SRTO_GROUPMINSTABLETIMEO`
// here) while the position did not, interface.md tells the candidate the
// position is the contract, and naming the enumerator would make a candidate
// who kept the stale spelling lose four groups to a compile error instead of
// losing the ones he actually got wrong. (The one option identifier the file
// does name, `SRTO_GROUPCONNECT`, exists at every tag in the chain and is in
// interface.md's required-to-compile table.)
//
// **Measured, and the candidate-facing document agrees.** interface.md §5
// gives the minimum link stability timeout as position 58, with group
// connection at 57 and group type at 59. The installed v1.5.0 header and
// library say the same, and so does every other document in this package:
//
//     SRTO_BINDTODEVICE 56, SRTO_GROUPCONNECT 57, SRTO_GROUPMINSTABLETIMEO 58,
//     SRTO_GROUPTYPE 59, SRTO_PACKETFILTER 60, SRTO_RETRANSMITALGO 61
//
// printed straight out of the installed `<srt/srt.h>` against the v1.5.0
// prefix, and matching `deltas/09.md` D1 ("srt_getsockflag(fresh, 58, ...)"),
// `bonding-delta-09.md`, `behaviour-delta-09-11.md:71` and REQUIREMENTS.md's
// own Step 09 freeze note, which cites `srt.h:239-244` for 56/57/58/59/60/61
// and then records the stepdef as having been set to 57 -- the correction was
// applied in the wrong direction. `fam_stability.cpp` reached the same
// measurement independently. The numbers below are the measured ones.
//
// The position is not merely trusted here. On a memberless group the release's
// `getOptDefault` (group.cpp:615-724) has an arm for the minimum link
// stability timeout and for nothing else in that neighbourhood, so a driver
// pointed at 57 or 59 instead would be refused outright and g04 would fail on
// its first assertion rather than pass against the wrong option. The floor in
// g05 pins it a second time from the setter side.
//
// --------------------------------------------------------- traceability
//
// Every case traces to a numbered requirement in
// tmp/srt-live-transport-multiversion/REQUIREMENTS.md "Step 09" and asserts
// only behaviour stated in stepdefs/09/interface.md (shipped as
// TEST_INTERFACE.md §5) or stepdefs/09/scope.md.
//
// Deliberately NOT asserted, and why:
//
//   * Failover timing, link stability decisions, and anything that reads the
//     internal microsecond storage. interface.md §5's last paragraph and
//     REQUIREMENTS.md's "deliberately not required" list both forbid it: the
//     option is checked as an accessor only.
//   * That the group setter's ceiling follows an SRTO_PEERIDLETIMEO stored on
//     the GROUP. It does -- measured: with the group's peer idle timeout set
//     to 3000, 3000 is accepted and 3001 is refused with 5003 -- but
//     interface.md states the ceiling only in terms of "the socket's peer idle
//     timeout" and with the 5000 default. Only the documented pair is scored.
//   * Any refusal code for a neighbouring option position on a group. The
//     release answers SRT_EINVPARAM for positions 55, 57 and 59 on a memberless
//     group, which would be a crisp position pin, but no candidate-facing
//     document says so.
//   * `srt_groupof`, `srt_group_data`, `srt_create_config`/`srt_config_add`/
//     `srt_delete_config` and the `SRT_SOCKGROUPDATA` layout: REQUIREMENTS.md
//     lists all of them as not required at this step. g04 therefore does not
//     use `srt_group_data` to prove a group is memberless -- it is memberless
//     by construction, having been created and never connected.
//   * That `srt_create_group` rejects an out-of-range type. Measured: it does
//     not, at either tag (deltas/09.md "plausible" list), so there is nothing
//     to score.
//   * Data transfer over a connected group. RQ-09-07 fixes "usable" as
//     "`srt_connect_group` accepts them as a group id"; carrying a stream is
//     `g00_endtoend` further down this file, and duplicating it here would make
//     one defect cost two points.

#include "stest.h"
#include "srtkit.h"

#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

// --------------------------------------------------------------- positions
//
// Measured against the installed v1.5.0 header; see the note above.
static const int kMinStabOpt     = 58;   // minimum link stability timeout
static const int kGroupConnectOpt = 57;  // group connection, set on the listener

// The values interface.md §5 and scope.md fix for this release.
static const int kDefMinStabMs = 60;     // the default, and the floor
static const int kPeerIdleMs   = 5000;   // the peer idle timeout's own default,
                                         // therefore the setter's ceiling

// Bounds. Every receive and every connect in this file is bounded; nothing
// sleeps.
static const int kConnectTimeoMs = 4000;
// How long an accept keeps offering AFTER the caller's connect attempt has
// returned. It is a grace, not a deadline: while the caller is still in flight
// the poll waits indefinitely, so a loaded host cannot turn a pass into a
// failure. See st_accept_polled in srtkit.h.
static const int kAcceptGraceMs  = 3000;

// --------------------------------------------------------------- naming
//
// A failure that prints "expected 5003, got 0" sends the reviewer to the
// header; one that prints the enumerator does not. Only the codes this family
// can actually produce are listed, and all of them are present and identically
// valued at both milestones.
static const char* grp_errname(int e) {
    switch (e) {
    case SRT_SUCCESS:    return "SRT_SUCCESS";
    case SRT_ECONNSOCK:  return "SRT_ECONNSOCK";
    case SRT_ECONNREJ:   return "SRT_ECONNREJ";
    case SRT_ECONNLOST:  return "SRT_ECONNLOST";
    case SRT_ENOCONN:    return "SRT_ENOCONN";
    case SRT_EINVOP:     return "SRT_EINVOP";
    case SRT_EINVPARAM:  return "SRT_EINVPARAM";
    case SRT_EINVSOCK:   return "SRT_EINVSOCK";
    case SRT_EUNBOUNDSOCK: return "SRT_EUNBOUNDSOCK";
    default:             return "<other SRT_ERRNO>";
    }
}

static const char* grp_statename(SRT_SOCKSTATUS s) {
    switch (s) {
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

static const char* grp_typename(int t) {
    switch (t) {
    case 0:  return "SRT_GTYPE_UNDEFINED";
    case 1:  return "SRT_GTYPE_BROADCAST";
    case 2:  return "SRT_GTYPE_BACKUP";
    default: return "<unnamed group type>";
    }
}

// ------------------------------------------------------------- ownership
//
// Every id this file creates is closed on every exit path, including the paths
// an assertion throws out of. Cases run in a forked child, but a leaked
// listener inside one case would still be holding its port when the next
// assertion in the same case runs.
struct Owned {
    SRTSOCKET s;
    explicit Owned(SRTSOCKET x = SRT_INVALID_SOCK) : s(x) {}
    ~Owned() { if (s != SRT_INVALID_SOCK) srt_close(s); }
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
    SRTSOCKET get() const { return s; }
};

static SRTSOCKET grp_create(int type) {
    srt_clearlasterror();
    SRTSOCKET g = srt_create_group((SRT_GROUP_TYPE)type);
    ST_REQUIRE_MSG(g != SRT_INVALID_SOCK,
                   "srt_create_group(%s, %d) returned SRT_INVALID_SOCK with %s (\"%s\"); the "
                   "milestone requires a usable group id for this type",
                   grp_typename(type), type, grp_errname(srt_getlasterror(nullptr)),
                   srt_getlasterror_str());
    return g;
}

// ------------------------------------------------------- option accessors
//
// Both halves of every outcome are carried: the return value AND what
// srt_getlasterror reports at that moment, because the descriptions name codes
// and not just refusals.
struct GetResult {
    int rc;
    int value;
    int len;
    int err;
};

static GetResult grp_get(SRTSOCKET id, int opt) {
    GetResult g;
    g.value = -1;
    g.len   = (int)sizeof(int);
    srt_clearlasterror();
    g.rc  = srt_getsockflag(id, (SRT_SOCKOPT)opt, &g.value, &g.len);
    g.err = srt_getlasterror(nullptr);
    return g;
}

struct SetResult {
    int rc;
    int err;
};

static SetResult grp_set(SRTSOCKET id, int opt, int v) {
    SetResult r;
    srt_clearlasterror();
    r.rc  = srt_setsockflag(id, (SRT_SOCKOPT)opt, &v, (int)sizeof v);
    r.err = srt_getlasterror(nullptr);
    return r;
}

// ===========================================================================
// g04_group_default -- RQ-09-04, deltas/09.md D5
//
// scored.txt: "reading the minimum link stability timeout on a group that has
//  no member sockets yet succeeds and yields 60, instead of being refused with
//  `SRT_EINVPARAM`"
//
// interface.md §5: "`srt_getsockflag` succeeds on a socket and on a group,
//  writes an `int32_t`, and reports a length of 4" and "A group with no members
//  reports the default from its getter."
// ===========================================================================

// The subject. A group straight out of `srt_create_group` has never had a
// member added, so the getter can only be answering from the group layer's own
// default table -- which at v1.4.4 has no entry for this option and refuses.
SRT_CASE(g04_group_default, memberless_group_reads_60) {
    Owned g(grp_create(1));

    GetResult r = grp_get(g.get(), kMinStabOpt);
    ST_REQUIRE_MSG(r.rc != SRT_ERROR,
                   "getsockflag(position %d) on a memberless group failed with %s (%d, \"%s\"); "
                   "this milestone answers it from the group's own default table, expected "
                   "success and the value %d",
                   kMinStabOpt, grp_errname(r.err), r.err, srt_getlasterror_str(), kDefMinStabMs);
    ST_REQUIRE_MSG(r.value == kDefMinStabMs,
                   "memberless group's minimum link stability timeout: expected %d ms, got %d",
                   kDefMinStabMs, r.value);
}

// The refusal the description names, on its own. A tree whose group layer has
// no case for the option falls through to its default arm and answers
// SRT_EINVPARAM -- a *different* failure from answering the wrong number, and
// the one this row exists to distinguish. The success half is asserted by the
// three cases around it; repeating it here would make this case fire on the
// return value rather than on the code it exists to name.
SRT_CASE(g04_group_default, not_refused_with_einvparam) {
    Owned g(grp_create(1));

    GetResult r = grp_get(g.get(), kMinStabOpt);
    ST_REQUIRE_MSG(!(r.rc == SRT_ERROR && r.err == SRT_EINVPARAM),
                   "getsockflag(position %d) on a memberless group was refused with "
                   "SRT_EINVPARAM (%d): the group layer has no default for this option and "
                   "there is no member socket to delegate to, which is exactly what this "
                   "milestone changes",
                   kMinStabOpt, SRT_EINVPARAM);
}

// The width half of interface.md's sentence, and the quantity it rules out.
// The previous milestone's field held MICROSECONDS and its default was
// 80 * 1000, so a tree that kept the old constant and the old unit answers
// 80000 through an otherwise correct accessor; 60000 is the same mistake made
// against the new constant. Both are four bytes wide, so the length assertion
// alone would not see them.
//
// The buffer is deliberately larger than the declared length, with the tail
// poisoned: "writes an int32_t" is a statement about how much it writes.
SRT_CASE(g04_group_default, writes_four_bytes_and_says_so) {
    Owned g(grp_create(1));

    unsigned char buf[16];
    std::memset(buf, 0xAB, sizeof buf);
    int len = (int)sizeof(int);
    srt_clearlasterror();
    int rc  = srt_getsockflag(g.get(), (SRT_SOCKOPT)kMinStabOpt, buf, &len);
    int err = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc != SRT_ERROR, "getsockflag(position %d) on a memberless group: %s (%d)",
                   kMinStabOpt, grp_errname(err), err);
    ST_REQUIRE_MSG(len == (int)sizeof(int),
                   "memberless group getter out-length: expected %d bytes, got %d",
                   (int)sizeof(int), len);

    for (size_t i = sizeof(int); i < sizeof buf; i++)
        ST_REQUIRE_MSG(buf[i] == 0xAB,
                       "the getter wrote byte %zu of the caller's buffer (0x%02x); it reports a "
                       "length of %d and must write an int32_t and nothing past it",
                       i, (unsigned)buf[i], len);

    int value = 0;
    std::memcpy(&value, buf, sizeof value);
    ST_REQUIRE_MSG(value == kDefMinStabMs,
                   "memberless group getter wrote %d; expected %d ms. %d would be the previous "
                   "milestone's microsecond constant and %d would be this milestone's constant "
                   "left in microseconds -- both are four bytes wide and neither is what this "
                   "option reports",
                   value, kDefMinStabMs, 80000, kDefMinStabMs * 1000);
}

// The default belongs to the group layer, not to the broadcast type: a backup
// group that has never had a member answers identically. This is the positive
// control for "the answer came from the group's default table" -- a tree that
// wired the default into one type's construction path and not the other passes
// the three cases above and fails here.
SRT_CASE(g04_group_default, backup_group_reads_the_same_default) {
    Owned b(grp_create(2));

    GetResult r = grp_get(b.get(), kMinStabOpt);
    ST_REQUIRE_MSG(r.rc != SRT_ERROR,
                   "getsockflag(position %d) on a memberless %s group failed with %s (%d); the "
                   "default is a property of the group layer and does not depend on the type",
                   kMinStabOpt, grp_typename(2), grp_errname(r.err), r.err);
    ST_REQUIRE_MSG(r.value == kDefMinStabMs,
                   "memberless %s group reports %d ms, expected the same default %d ms the "
                   "%s group reports",
                   grp_typename(2), r.value, kDefMinStabMs, grp_typename(1));
    ST_REQUIRE_MSG(r.len == (int)sizeof(int),
                   "memberless %s group getter out-length: expected %d bytes, got %d",
                   grp_typename(2), (int)sizeof(int), r.len);
}

// ===========================================================================
// g05_group_set -- RQ-09-05, deltas/09.md D6
//
// scored.txt: "the group's own setter for the minimum link stability timeout
//  refuses 10 and 59 with `SRT_EINVPARAM` and accepts 60 and 5000, while a
//  memberless group keeps reporting 60 from its getter whatever value was
//  accepted"
//
// interface.md §5: "A value below the 60 ms floor is refused with
//  `SRT_EINVPARAM`. A value strictly greater than the socket's peer idle
//  timeout is refused with `SRT_EINVPARAM`; a value *equal* to it is accepted,
//  so with the peer idle timeout left at its 5000 ms default, 5000 is a legal
//  setting and 5001 is not." and "The group setter validates and accepts, but
//  until a member link exists the getter yields 60 whatever value was stored.
//  This is asserted as-is."
//
// This is a SECOND implementation of the same rules, not a delegation: the
// group reaches its own validator, so a candidate who implemented the socket
// path and forwarded the group path to it will not produce the shape below.
// At v1.4.4 the same setter has no floor at all and its ceiling test is
// `val >= idletmo`, so the previous milestone accepts 10 and refuses 5000 --
// the exact inverse of what the two boundary cases here require.
// ===========================================================================

// The floor, inclusive, with its positive control in the same case: 60 itself
// must be accepted, so a candidate who refuses everything up to and including
// the floor fails here and nowhere else.
SRT_CASE(g05_group_set, floor_is_60_inclusive) {
    Owned g(grp_create(1));

    SetResult lo = grp_set(g.get(), kMinStabOpt, 10);
    ST_REQUIRE_MSG(lo.rc == SRT_ERROR,
                   "the group setter accepted %d for position %d; the floor is %d ms so %d must "
                   "be refused (at the previous milestone this value is accepted, which is what "
                   "this case measures)",
                   10, kMinStabOpt, kDefMinStabMs, 10);
    ST_REQUIRE_MSG(lo.err == SRT_EINVPARAM,
                   "the group setter refused %d with %s (%d, \"%s\"); expected SRT_EINVPARAM (%d)",
                   10, grp_errname(lo.err), lo.err, srt_getlasterror_str(), SRT_EINVPARAM);

    SetResult just_below = grp_set(g.get(), kMinStabOpt, kDefMinStabMs - 1);
    ST_REQUIRE_MSG(just_below.rc == SRT_ERROR,
                   "the group setter accepted %d; %d ms is one below the %d ms floor and must be "
                   "refused",
                   kDefMinStabMs - 1, kDefMinStabMs - 1, kDefMinStabMs);
    ST_REQUIRE_MSG(just_below.err == SRT_EINVPARAM,
                   "the group setter refused %d with %s (%d); expected SRT_EINVPARAM (%d)",
                   kDefMinStabMs - 1, grp_errname(just_below.err), just_below.err, SRT_EINVPARAM);

    SetResult at = grp_set(g.get(), kMinStabOpt, kDefMinStabMs);
    ST_REQUIRE_MSG(at.rc != SRT_ERROR,
                   "the group setter refused %d with %s (%d); the floor itself is a legal value "
                   "and the two refusals above mean nothing unless it is",
                   kDefMinStabMs, grp_errname(at.err), at.err);
}

// The ceiling, and the strictness that makes it worth a case: with the peer
// idle timeout left at its default the boundary value itself is legal and only
// a strictly greater one is not. At v1.4.4 the test is `>=`, so the previous
// milestone refuses exactly the value this one accepts.
SRT_CASE(g05_group_set, ceiling_is_strict_at_the_peer_idle_default) {
    Owned g(grp_create(1));

    SetResult eq = grp_set(g.get(), kMinStabOpt, kPeerIdleMs);
    ST_REQUIRE_MSG(eq.rc != SRT_ERROR,
                   "the group setter refused %d with %s (%d); with the peer idle timeout left at "
                   "its %d ms default a value EQUAL to it is accepted, only a strictly greater "
                   "one is not",
                   kPeerIdleMs, grp_errname(eq.err), eq.err, kPeerIdleMs);

    SetResult over = grp_set(g.get(), kMinStabOpt, kPeerIdleMs + 1);
    ST_REQUIRE_MSG(over.rc == SRT_ERROR,
                   "the group setter accepted %d; it exceeds the %d ms peer idle timeout in "
                   "force and must be refused",
                   kPeerIdleMs + 1, kPeerIdleMs);
    ST_REQUIRE_MSG(over.err == SRT_EINVPARAM,
                   "the group setter refused %d with %s (%d, \"%s\"); expected SRT_EINVPARAM (%d)",
                   kPeerIdleMs + 1, grp_errname(over.err), over.err, srt_getlasterror_str(),
                   SRT_EINVPARAM);
}

// The asserted-as-is half of the description. Every accepted value leaves the
// memberless getter answering the default, because the getter takes the group's
// default path until a member link exists and never reads what the setter
// stored. interface.md: "do not make the getter echo the pending value."
//
// Each set is checked for acceptance first, so a tree that refuses everything
// fails here rather than passing on a getter that happens to read 60.
SRT_CASE(g05_group_set, memberless_getter_keeps_reporting_the_default) {
    Owned g(grp_create(1));

    const int accepted[3] = { kDefMinStabMs, 1000, kPeerIdleMs };
    for (int i = 0; i < 3; i++) {
        SetResult s = grp_set(g.get(), kMinStabOpt, accepted[i]);
        ST_REQUIRE_MSG(s.rc != SRT_ERROR,
                       "the group setter refused %d with %s (%d); it is at or above the %d ms "
                       "floor and at or below the %d ms ceiling, so it is a legal value",
                       accepted[i], grp_errname(s.err), s.err, kDefMinStabMs, kPeerIdleMs);

        GetResult r = grp_get(g.get(), kMinStabOpt);
        ST_REQUIRE_MSG(r.rc != SRT_ERROR,
                       "getsockflag(position %d) after an accepted set of %d failed with %s (%d)",
                       kMinStabOpt, accepted[i], grp_errname(r.err), r.err);
        ST_REQUIRE_MSG(r.value == kDefMinStabMs,
                       "after the group setter accepted %d, the memberless getter reports %d; it "
                       "must keep reporting the default %d until a member link exists -- the "
                       "getter takes the group's default path and does not echo the stored value",
                       accepted[i], r.value, kDefMinStabMs);
    }
}

// The same validator on a backup group. The setter belongs to the group layer,
// not to the broadcast type, and backup mode is the mode the option is FOR, so
// a candidate who wired the floor into one construction path and not the other
// is caught here. Same subject, different group type.
SRT_CASE(g05_group_set, the_same_rules_hold_on_a_backup_group) {
    Owned b(grp_create(2));

    SetResult lo = grp_set(b.get(), kMinStabOpt, 10);
    ST_REQUIRE_MSG(lo.rc == SRT_ERROR && lo.err == SRT_EINVPARAM,
                   "%s group: setting position %d to %d returned rc=%d with %s (%d); expected "
                   "refusal with SRT_EINVPARAM (%d), the same floor the %s group enforces",
                   grp_typename(2), kMinStabOpt, 10, lo.rc, grp_errname(lo.err), lo.err,
                   SRT_EINVPARAM, grp_typename(1));

    SetResult ok = grp_set(b.get(), kMinStabOpt, kPeerIdleMs);
    ST_REQUIRE_MSG(ok.rc != SRT_ERROR,
                   "%s group: setting position %d to %d was refused with %s (%d); it equals the "
                   "peer idle timeout default and is legal",
                   grp_typename(2), kMinStabOpt, kPeerIdleMs, grp_errname(ok.err), ok.err);

    GetResult r = grp_get(b.get(), kMinStabOpt);
    ST_REQUIRE_MSG(r.rc != SRT_ERROR && r.value == kDefMinStabMs,
                   "%s group: after accepting %d the memberless getter reports rc=%d value=%d; "
                   "expected success and the default %d",
                   grp_typename(2), kPeerIdleMs, r.rc, r.value, kDefMinStabMs);
}

// ===========================================================================
// g06_group_pruned -- RQ-09-06, deltas/09.md D7
//
// scored.txt: "the library exports `srt_create_group` and `srt_connect_group`
//  but resolves `srt_include`, `srt_exclude` and `srt_group_configure` to
//  nothing, so a program that looks all five up at run time finds exactly the
//  first two"
//
// interface.md §5: "`srt_include`, `srt_exclude` and `srt_group_configure` must
//  be absent from the header and unexported from the library; a program that
//  resolves them at run time must find nothing. ... `srt_create_group` and
//  `srt_connect_group` are looked up the same way in the same group and must
//  both resolve."
//
// Run-time resolution is the subject and not a convenience. It is the only way
// to catch a definition that compiles and is not exported, and -- the reason
// the description says "looks all five up" -- it lets a tree that kept the
// three withdrawn functions lose this one group rather than fail to link the
// family. Confirmed against the two install prefixes with `nm -D`: all five are
// exported at v1.4.4, only two at v1.5.0.
// ===========================================================================

// void* -> function pointer by memcpy rather than by cast: the cast is only
// conditionally supported and trips -Wpedantic under -Werror. Same device as
// fam_optget.cpp and fam_epoll.cpp.
template <typename Fn>
static Fn grp_dlsym(const char* name) {
    void* sym = dlsym(RTLD_DEFAULT, name);
    if (!sym) return 0;
    Fn fn;
    std::memcpy(&fn, &sym, sizeof fn);
    return fn;
}

static const char* const kSurviving[2] = { "srt_create_group", "srt_connect_group" };
static const char* const kWithdrawn[3] = { "srt_include", "srt_exclude", "srt_group_configure" };

// A name that has never existed at any milestone in this chain. If the
// resolver answers for it, the resolver answers for everything and every
// absence below would be unprovable.
static const char kNeverExisted[] = "srt_no_such_group_function_at_any_milestone";

// The description's own sentence: all five looked up, exactly the first two
// found. The control is inside the case, before either half.
SRT_CASE(g06_group_pruned, exactly_two_of_the_five_resolve) {
    ST_REQUIRE_MSG(dlsym(RTLD_DEFAULT, kNeverExisted) == 0,
                   "control: dlsym(RTLD_DEFAULT, \"%s\") resolved to %p, so this build answers "
                   "the resolver for names that do not exist and neither half of this group "
                   "would prove anything",
                   kNeverExisted, dlsym(RTLD_DEFAULT, kNeverExisted));

    int found = 0;
    for (int i = 0; i < 2; i++) {
        void* p = dlsym(RTLD_DEFAULT, kSurviving[i]);
        ST_REQUIRE_MSG(p != 0,
                       "dlsym(RTLD_DEFAULT, \"%s\") found nothing: the library must EXPORT it, "
                       "and this group resolves rather than declares, so a name present only in "
                       "a header cannot satisfy it",
                       kSurviving[i]);
        found++;
    }
    for (int i = 0; i < 3; i++) {
        void* p = dlsym(RTLD_DEFAULT, kWithdrawn[i]);
        ST_REQUIRE_MSG(p == 0,
                       "dlsym(RTLD_DEFAULT, \"%s\") resolved to %p: this milestone withdraws the "
                       "function, and an exported symbol under that name means the previous "
                       "milestone's group API is still in the library",
                       kWithdrawn[i], p);
    }

    ST_REQUIRE_MSG(found == 2,
                   "looking up the five group functions found %d of them; expected exactly 2 "
                   "(%s and %s)",
                   found, kSurviving[0], kSurviving[1]);
}

// The three withdrawals on their own, one assertion apiece, with the surviving
// pair as the positive control in the same case. Without the control, a build
// whose library failed to load at all would score this point.
SRT_CASE(g06_group_pruned, the_three_withdrawn_functions_are_unexported) {
    for (int i = 0; i < 2; i++)
        ST_REQUIRE_MSG(dlsym(RTLD_DEFAULT, kSurviving[i]) != 0,
                       "control: dlsym(RTLD_DEFAULT, \"%s\") found nothing, so the library's "
                       "group surface is not resolvable at all and the three absences below "
                       "would be meaningless",
                       kSurviving[i]);

    for (int i = 0; i < 3; i++) {
        void* p = dlsym(RTLD_DEFAULT, kWithdrawn[i]);
        ST_REQUIRE_MSG(p == 0,
                       "\"%s\" is still exported (resolved to %p). A member link joins a group "
                       "only through the SRT_SOCKGROUPCONFIG array handed to srt_connect_group "
                       "at this milestone, and this function is withdrawn with the other two",
                       kWithdrawn[i], p);
    }
}

// "Resolves" has to mean "is the working function", or a tree that exported an
// empty stub under the right name would pass the two cases above. The resolved
// `srt_create_group` is called through its pointer and must produce a group id.
SRT_CASE(g06_group_pruned, the_resolved_create_group_is_the_working_function) {
    typedef SRTSOCKET (*create_group_fn)(SRT_GROUP_TYPE);

    create_group_fn fn = grp_dlsym<create_group_fn>(kSurviving[0]);
    ST_REQUIRE_MSG(fn != 0, "dlsym(RTLD_DEFAULT, \"%s\") found nothing", kSurviving[0]);

    srt_clearlasterror();
    SRTSOCKET g = fn((SRT_GROUP_TYPE)1);
    Owned owner(g);
    ST_REQUIRE_MSG(g != SRT_INVALID_SOCK,
                   "the exported \"%s\" returned SRT_INVALID_SOCK with %s (\"%s\") for %s: the "
                   "symbol resolves but is not the working function",
                   kSurviving[0], grp_errname(srt_getlasterror(nullptr)), srt_getlasterror_str(),
                   grp_typename(1));

    // The id it produced is the library's own: the same option the rest of this
    // family reads answers on it.
    GetResult r = grp_get(g, kMinStabOpt);
    ST_REQUIRE_MSG(r.rc != SRT_ERROR && r.value == kDefMinStabMs,
                   "the group produced by the exported \"%s\" answers position %d with rc=%d "
                   "value=%d (%s); expected success and %d, which is what a real group id does",
                   kSurviving[0], kMinStabOpt, r.rc, r.value, grp_errname(r.err), kDefMinStabMs);
}

// ===========================================================================
// g07_group_types -- RQ-09-07, deltas/09.md D8
//
// scored.txt: "the group type enumeration names the undefined, broadcast and
//  backup types as 0, 1 and 2 and its end marker is 3, and `srt_create_group`
//  returns a usable group id for the broadcast type and for the backup type"
//
// interface.md §5: "Undefined is 0, broadcast is 1, backup is 2, and the end
//  marker is exactly 3 -- the two reserved types the previous milestone
//  declared are gone, and nothing takes their numbers."
//
// The values are asserted POSITIVELY, per deltas/09.md D8 ("score the positive
// value, never the `#ifdef`-absence form"), so the two withdrawn type names
// are never mentioned: naming them to prove they are gone would be a compile
// error at this tag, and their numbers are covered by the end marker being 3.
//
// The assertion is at run time and not a `static_assert` on purpose. The values
// are compile-time constants either way, but a static_assert that fires takes
// the whole family's four groups with it, and only one of them is about the
// enumeration.
// ===========================================================================

SRT_CASE(g07_group_types, enumeration_is_0_1_2_and_ends_at_3) {
    const int undef  = (int)SRT_GTYPE_UNDEFINED;
    const int bcast  = (int)SRT_GTYPE_BROADCAST;
    const int backup = (int)SRT_GTYPE_BACKUP;
    const int end    = (int)SRT_GTYPE_E_END;

    ST_REQUIRE_MSG(undef == 0, "SRT_GTYPE_UNDEFINED is %d, expected 0", undef);
    ST_REQUIRE_MSG(bcast == 1, "SRT_GTYPE_BROADCAST is %d, expected 1", bcast);
    ST_REQUIRE_MSG(backup == 2, "SRT_GTYPE_BACKUP is %d, expected 2", backup);
    ST_REQUIRE_MSG(end == 3,
                   "SRT_GTYPE_E_END is %d, expected 3. The previous milestone declared two "
                   "further types between the backup type and the end marker, which put the end "
                   "marker at 5; this milestone withdraws both and nothing takes their numbers",
                   end);

    // Nothing is renumbered around the withdrawal: the three named types are
    // consecutive from zero and all of them are below the end marker. A tree
    // that closed the gap by moving a type down satisfies "end == 3" and fails
    // here.
    ST_REQUIRE_MSG(bcast == undef + 1 && backup == bcast + 1,
                   "the named group types are %d, %d and %d; they must be consecutive from zero "
                   "(0, 1, 2)", undef, bcast, backup);
    ST_REQUIRE_MSG(end == backup + 1,
                   "the end marker is %d and the backup type is %d; the end marker must be the "
                   "next value after the last named type, with nothing reserved between them",
                   end, backup);
}

// "Usable" in the sense RQ-09-07 fixes: `srt_connect_group` accepts the id as a
// group id. Carrying a stream is `g00_endtoend`, further down this same file;
// this group stops at the id.
//
// The listener has group connection enabled -- scope.md: "a listener with group
// connection enabled returns a group id from `srt_accept`" -- reached by its
// numeric position for the same reason as the option above.
struct GrpListener {
    Owned sock;
    uint16_t port;

    GrpListener() : sock(st_new_socket()), port(0) {
        ST_REQUIRE_MSG(sock.get() != SRT_INVALID_SOCK, "srt_socket (listener): %s",
                       srt_getlasterror_str());
        SetResult gc = grp_set(sock.get(), kGroupConnectOpt, 1);
        ST_REQUIRE_MSG(gc.rc != SRT_ERROR,
                       "enabling group connection (position %d) on the listener failed with "
                       "%s (%d); without it the listener refuses a group caller and this case "
                       "would be measuring the listener, not the group id",
                       kGroupConnectOpt, grp_errname(gc.err), gc.err);
        // No SRTO_RCVTIMEO here, deliberately: a BLOCKING srt_accept does not
        // consult it at any tag in this ladder (srtkit.h records the
        // measurement), so setting it would bound nothing and would tell the
        // next author it does. The accept below is bounded by being POLLED --
        // st_accept_polled turns SRTO_RCVSYN off on this listener and gives up
        // a grace after the caller's connect has returned.

        sockaddr_in la = st_loopback(0);
        ST_REQUIRE_MSG(srt_bind(sock.get(), (sockaddr*)&la, sizeof la) != SRT_ERROR,
                       "srt_bind (listener): %s", srt_getlasterror_str());
        int nl = (int)sizeof la;
        ST_REQUIRE_MSG(srt_getsockname(sock.get(), (sockaddr*)&la, &nl) != SRT_ERROR,
                       "srt_getsockname (listener): %s", srt_getlasterror_str());
        ST_REQUIRE_MSG(srt_listen(sock.get(), 5) != SRT_ERROR, "srt_listen: %s",
                       srt_getlasterror_str());
        port = ntohs(la.sin_port);
        ST_REQUIRE_MSG(port != 0, "the listener did not take an ephemeral port");
    }
};

// Shared body for the two named types. The negative control runs first: a
// PLAIN SOCKET id handed to srt_connect_group must be refused, or "the group id
// was accepted" below is a statement about nothing.
static void grp_id_is_accepted_as_a_group_id(int type) {
    GrpListener lsn;

    sockaddr_in ra = st_loopback(lsn.port);

    {
        Owned plain(st_new_socket());
        ST_REQUIRE_MSG(plain.get() != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
        SRT_SOCKGROUPCONFIG ctl = srt_prepare_endpoint(nullptr, (sockaddr*)&ra, sizeof ra);
        srt_clearlasterror();
        int rc = srt_connect_group(plain.get(), &ctl, 1);
        ST_REQUIRE_MSG(rc == SRT_ERROR,
                       "control: srt_connect_group accepted a plain socket id and returned %d, "
                       "so it does not check that its first argument is a group and the group "
                       "id accepted below would prove nothing",
                       rc);
    }

    Owned g(grp_create(type));

    // Bounded, so a tree that never completes the handshake costs the timeout
    // rather than the case deadline.
    SetResult to = grp_set(g.get(), SRTO_CONNTIMEO, kConnectTimeoMs);
    ST_REQUIRE_MSG(to.rc != SRT_ERROR, "setting the connect timeout on the group failed: %s (%d)",
                   grp_errname(to.err), to.err);

    SRT_SOCKGROUPCONFIG member = srt_prepare_endpoint(nullptr, (sockaddr*)&ra, sizeof ra);
    srt_clearlasterror();
    int rc  = srt_connect_group(g.get(), &member, 1);
    int err = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc != SRT_ERROR,
                   "srt_connect_group refused the %s group id with %s (%d, \"%s\") and the "
                   "member link reported %d; srt_create_group must return an id the connect "
                   "call accepts as a group",
                   grp_typename(type), grp_errname(err), err, srt_getlasterror_str(),
                   member.errorcode);

    SRT_SOCKSTATUS st = srt_getsockstate(g.get());
    ST_REQUIRE_MSG(st == SRTS_CONNECTED,
                   "after srt_connect_group returned success the %s group reports state %s; "
                   "expected CONNECTED",
                   grp_typename(type), grp_statename(st));

    // The listener's side of the same handshake: without this the case would
    // pass against a connect that reported success and reached nobody.
    //
    // srt_connect_group has already returned, so `caller_done` starts true and
    // the poll gives up one grace later. A blocking srt_accept here would have
    // no bound at all and a tree that never queues the socket would cost the
    // case's 60 s alarm instead of this assertion.
    std::atomic<bool> caller_done(true);
    Owned accepted(st_accept_polled(lsn.sock.get(), caller_done, nullptr, nullptr,
                                    kAcceptGraceMs));
    ST_REQUIRE_MSG(accepted.get() != SRT_INVALID_SOCK,
                   "the group-connection listener accepted nothing within %d ms after the %s "
                   "group connected: %s",
                   kAcceptGraceMs, grp_typename(type), srt_getlasterror_str());
}

SRT_CASE(g07_group_types, broadcast_id_is_accepted_as_a_group_id) {
    grp_id_is_accepted_as_a_group_id(1);
}

SRT_CASE(g07_group_types, backup_id_is_accepted_as_a_group_id) {
    grp_id_is_accepted_as_a_group_id(2);
}


// --------------------------------------------------- helpers for g00_endtoend
//
// g00 carries a live stream over the group, so it needs the live profile and
// the receive-loop predicate the `live` family uses. They are restated here,
// as statics, rather than shared: this file and `fam_live.cpp` are separate
// translation units and the drivers ship no common source of their own.

// TSBPD on, latency generous enough that the relay's schedule rather than the
// clock decides what arrives, and too-late-to-play dropping off.
static SockConf live_conf(int latency_ms) {
    return [latency_ms](SRTSOCKET s) {
        st_set_int(s, SRTO_TSBPDMODE, 1);
        st_set_int(s, SRTO_LATENCY, latency_ms);
        st_set_int(s, SRTO_TLPKTDROP, 0);
    };
}

// True when the receive that just failed only ran out its `SRTO_RCVTIMEO`, in
// which case the caller keeps waiting until its own deadline. A reader that
// treats the first expiry as end-of-stream returns nothing whenever the first
// delivery is held for the negotiated latency, which under TSBPD is normal.
static bool recv_timed_out() {
    int eg = srt_getlasterror(nullptr);
    if (eg == SRT_ETIMEOUT || eg == SRT_EASYNCRCV) {
        srt_clearlasterror();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// g00_endtoend -- this milestone's end-to-end path (SCORED, tier `new`).
//
// A broadcast group connects over one member link, through a clean relay, to a
// listener that has opted into group connections, and carries 2000 messages
// that must arrive byte-identical, in order and with no gaps.
//
// The shape is hand-rolled rather than taken from `st_connect`/`st_try_connect`
// because those helpers connect the caller with plain `srt_connect`. That
// handshake carries no group extension, so `srt_accept` hands back a plain
// socket -- measured: `accepted & SRTGROUP_MASK == 0`, `srt_groupof` -1 -- and
// a group opened separately afterwards delivers to nobody (measured: the relay
// forwarded every datagram, the application received 0). Here the *caller* is
// the group, via `srt_connect_group`, which is the only way the listener ever
// sees a group handshake.
//
// `SRTO_GROUPCONNECT` is set before `srt_bind`: it is a pre-bind option and a
// set after `srt_listen` is refused with SRT_ECONNSOCK (measured).
//
// The accept is polled rather than blocked on, through the shared
// `st_accept_polled`. `SRTO_RCVTIMEO` does not bound `srt_accept`: measured
// with the listener's group opt-in turned off, the caller's
// `srt_connect_group` returned SRT_ERROR after 10 ms and the blocking
// `srt_accept` beside it never returned at all -- the case died on the 60 s
// harness timeout instead of on an assertion. The helper's patience is tied to
// the CALLER and not to a clock: it keeps offering while
// `srt_connect_group` is in flight, however long the host takes, and gives up
// only `kAcceptGraceMs` after the connect has returned. The hand-rolled
// version this replaced expired on a fixed 8000 ms measured from before the
// connect started, so a slow host could report "srt_accept: ..." for a
// connection that was still being made -- round 1 finding F3.
// `SRTO_RCVSYN` is restored on the accepted socket by the helper, which would
// otherwise inherit the listener's non-blocking receives.
//
// The reader runs on its own thread because a send-then-drain loop stalls once
// the receive buffer fills -- measured at 2000 messages: 318 arrived, then
// nothing -- while a concurrent reader completes every run.
//
// The sends are paced, for the same reason `st_send_stream` paces its own: the
// relay is a userspace hop with an ordinary socket buffer, and an unpaced
// burst overflows it below the layer SRT can retransmit from. Measured
// unpaced on a loaded machine: one run in eighteen delivered 1999 of 2000.
// One millisecond apart -- about 10 Mbit/s at this payload -- the relay keeps
// up and the transfer still finishes well inside the case budget.
// ---------------------------------------------------------------------------
SRT_CASE(g00_endtoend, broadcast_group_transfer) {
    const int N = 2000, SZ = 1316;  // 1316 B: the size measured to complete at both prefixes
    const int GAP_US = 1000;        // paced: an unpaced burst overflows the relay, not SRT

    SRTSOCKET lsn = st_new_socket();
    ST_REQUIRE_MSG(lsn != SRT_INVALID_SOCK, "srt_socket (listener): %s", srt_getlasterror_str());
    live_conf(900)(lsn);
    st_set_int(lsn, SRTO_GROUPCONNECT, 1);

    sockaddr_in la = st_loopback(0);
    ST_SRT_OK(srt_bind(lsn, (sockaddr*)&la, sizeof la));
    int nl = (int)sizeof la;
    ST_SRT_OK(srt_getsockname(lsn, (sockaddr*)&la, &nl));
    st_set_int(lsn, SRTO_RCVSYN, 0);   // see the note above: srt_accept must be bounded
    ST_SRT_OK(srt_listen(lsn, 1));

    Relay relay(ntohs(la.sin_port), RelaySched{});

    SRTSOCKET accepted = SRT_INVALID_SOCK;
    std::atomic<bool> connect_done(false);
    std::thread acceptor(
        [&] { accepted = st_accept_polled(lsn, connect_done, nullptr, nullptr,
                                          kAcceptGraceMs); });

    SRTSOCKET grp = srt_create_group(SRT_GTYPE_BROADCAST);
    int grp_err = srt_getlasterror(nullptr);
    int crc = SRT_ERROR, member = (int)SRT_INVALID_SOCK, cerr = 0;
    if (grp != SRT_INVALID_SOCK) {
        live_conf(900)(grp);
        sockaddr_in ra = st_loopback(relay.port());
        SRT_SOCKGROUPCONFIG ep = srt_prepare_endpoint(nullptr, (sockaddr*)&ra, sizeof ra);
        crc = srt_connect_group(grp, &ep, 1);
        cerr = srt_getlasterror(nullptr);
        member = (int)ep.id;
    }
    connect_done.store(true);   // the acceptor's only bound; see the note above
    acceptor.join();   // joined before any assertion: a live thread at exit aborts the child

    ST_REQUIRE_MSG(grp != SRT_INVALID_SOCK, "srt_create_group(SRT_GTYPE_BROADCAST): expected a "
                   "group id, got SRT_INVALID_SOCK (%s)", srt_strerror(grp_err, 0));
    ST_REQUIRE_MSG(crc != SRT_ERROR, "srt_connect_group over the relay: expected a connected "
                   "member id, got SRT_ERROR (%s)", srt_strerror(cerr, 0));
    ST_REQUIRE_MSG(member != (int)SRT_INVALID_SOCK,
                   "group member id after srt_connect_group: expected a connected member id, "
                   "got SRT_INVALID_SOCK");
    ST_REQUIRE_MSG(accepted != SRT_INVALID_SOCK, "srt_accept: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG((accepted & SRTGROUP_MASK) != 0,
                   "srt_accept on a group connection: expected an id with SRTGROUP_MASK (0x%x) "
                   "set, got 0x%x -- the listener accepted a plain socket, so the stream would "
                   "not be carried by the group", (unsigned)SRTGROUP_MASK, (unsigned)accepted);

    std::vector<std::string> sent;
    sent.reserve((size_t)N);
    for (int i = 0; i < N; i++) sent.push_back(st_payload(i, SZ));

    std::vector<std::string> got;
    got.reserve((size_t)N);

    // The reader's bound is DERIVED from the send schedule rather than being a
    // round number: the sender paces N messages GAP_US apart, so the transfer
    // cannot finish before N * GAP_US however fast the host is, and four times
    // that plus five seconds leaves room for the 900 ms latency budget, the
    // final drain and a host running several of these at once. It bounds a
    // READ so a stalled stream costs this assertion instead of the case's 60 s
    // alarm; it is not an assertion about elapsed time, and its expiry is
    // reported below as a deadline rather than as a short message count, which
    // would read like a capability the candidate is missing.
    const int kReadDeadlineMs = 4 * (N * GAP_US / 1000) + 5000;

    std::atomic<bool> read_deadline_hit(false);
    std::thread reader([&] {
        st_set_int(accepted, SRTO_RCVTIMEO, 500);
        std::string buf(4096, '\0');   // >= one payload unit: a shorter buffer is refused (5009)
        auto t0 = std::chrono::steady_clock::now();
        while ((int)got.size() < N) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count() >= kReadDeadlineMs) {
                read_deadline_hit.store(true);
                break;
            }
            int rc = srt_recv(accepted, &buf[0], (int)buf.size());
            if (rc > 0) { got.push_back(buf.substr(0, (size_t)rc)); continue; }
            if (!recv_timed_out()) break;
        }
    });

    int sendfail = -1, sendrc = 0, senderr = 0;
    for (int i = 0; i < N; i++) {
        int rc = srt_sendmsg(grp, sent[(size_t)i].data(), (int)sent[(size_t)i].size(), -1, 1);
        if (rc != (int)sent[(size_t)i].size()) {
            sendfail = i; sendrc = rc; senderr = srt_getlasterror(nullptr);
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(GAP_US));
    }
    reader.join();

    int seen = (int)relay.seen_fwd(), dropped = relay.dropped_fwd();
    srt_close(grp);
    srt_close(accepted);
    srt_close(lsn);

    ST_REQUIRE_MSG(sendfail < 0, "srt_sendmsg #%d on the group returned %d, expected %d: %s",
                   sendfail, sendrc, SZ, srt_strerror(senderr, 0));
    ST_REQUIRE_MSG(!read_deadline_hit.load(),
                   "READ DEADLINE: the reader gave up after %d ms with %d of %d messages in "
                   "hand. That bound is 4x the %d ms the paced send schedule needs plus 5 s, so "
                   "the stream had stalled rather than merely being slow; this is the read "
                   "giving up, not a count assertion",
                   kReadDeadlineMs, (int)got.size(), N, N * GAP_US / 1000);
    ST_REQUIRE_MSG((int)got.size() == N,
                   "end-to-end message count over the group: expected %d, got %d", N,
                   (int)got.size());
    int ooo = st_first_out_of_order(sent, got);
    ST_REQUIRE_MSG(ooo == -1,
                   "end-to-end delivery over the group: first index whose payload differs from "
                   "what was sent is %d, expected -1 (byte-identical and in order)", ooo);
    ST_REQUIRE_MSG(dropped == 0,
                   "relay forward drops on a clean schedule: expected 0, got %d", dropped);
    ST_REQUIRE_MSG(seen >= N,
                   "forward datagrams the relay carried: expected at least %d, got %d", N, seen);
}
