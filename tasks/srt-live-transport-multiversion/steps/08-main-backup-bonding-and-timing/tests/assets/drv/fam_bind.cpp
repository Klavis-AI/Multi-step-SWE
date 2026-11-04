// Step 08 (`v1.4.4`), family `bind` -- binding-conflict detection.
//
// Groups: g05_conflict_errcode, g06_wildcard_conflict, g07_separate_muxers,
//         g08_settings_conflict.
//
// This family is uniformly `new` and SCORED (meta.env E2E_TIER=new; the
// end-to-end group,
// g00_endtoend, belongs to `live`), so it is *not* required to build against
// v1.4.3 and is deliberately not contorted to. A group here that still
// compiled and passed at v1.4.3 would be a group that fails to discriminate,
// which is worse than a build failure at the previous prefix.
//
// Requirements: tmp/<task>/REQUIREMENTS.md §"Step 08" RQ-08-05 .. RQ-08-08.
// Each case below names the requirement it traces to.
//
// Every constant and every line number the requirement list gives was
// re-measured against the two installed prefixes before this file was
// written; the measurements are in the report that accompanies it.

#include "srtkit.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdio>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// The one new constant this file names, and why it is spelled as a number.
//
// `SRT_EBINDCONFLICT` (RQ-08-05) is `MN(NOTSUP, BUSYPORT)` = 1000*5 + 15 =
// 5015, with `MN_BUSYPORT = 15` newly appended to `enum CodeMinor` between
// `MN_EEMPTY = 14` and the `// MJ_AGAIN` comment.  Nothing is renumbered.
//
// Both enumerators are named here **unguarded**, and that is the point.  A
// header that omits either one fails to compile this translation unit, which
// is exactly what `required-ids.txt` marks them (`hard`) and what
// TEST_INTERFACE.md section 5 promises the reader: arriving at 5015 by another
// route -- an integer literal, a differently spelled enumerator, a minor code
// kept in a private header -- does not fail one assertion, it loses the whole
// `bind` family.  An `#ifdef` here would not enforce that and would not even
// fire: neither name is ever a preprocessor macro (both are enumerators, of
// `SRT_ERRNO` and `CodeMinor`), so `#ifdef` on them is false at every tag and
// silently discards the pin.  Measured at both prefixes on 2026-09-21.
//
// Losing the family at v1.4.3 is intended, not a hazard -- see the file header:
// this family is uniformly `new` and SCORED, every gate group lives in `live`,
// and a bind group that still built and passed at v1.4.3 would be a group that
// fails to discriminate.
//
// `srt_strerror` takes a plain `int`, so the case bodies still spell 5015 as a
// number: there the subject is the library's behaviour, not the header.  The
// pin below re-derives 5015 from the header, so that literal cannot silently
// drift into addressing some other code.
// ---------------------------------------------------------------------------
static_assert(SRT_EBINDCONFLICT == 5015,
              "SRT_EBINDCONFLICT disagrees with <srt/srt.h>: RQ-08-05 is 5015");
static_assert(MN_BUSYPORT == 15, "MN_BUSYPORT disagrees with <srt/srt.h>: RQ-08-05 is 15");

// MJ_NOTSUP = 5, MN_BUSY = 11.  Both exist at every tag in the chain, so
// these do not need a guard; they are here to make the two arithmetic claims
// in the case bodies checkable rather than asserted in prose.
static_assert(SRT_EDUPLISTEN == 5011, "SRT_EDUPLISTEN is not 5011; re-derive g05's literals");
static_assert(SRT_ESOCKFAIL == 1003, "SRT_ESOCKFAIL is not 1003; re-derive g08's control");

namespace {

const int kEbindconflict = 5015;  // RQ-08-05.  Pinned to the header unconditionally above.
const int kUnassignedMinor = 5016;  // one past the last MN_NOTSUP entry (RQ-08-05).

sockaddr_in st_addr(const char* ip, uint16_t port) {
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) {
        st_failf(__FILE__, __LINE__, "inet_pton(\"%s\") failed", ip);
    }
    return a;
}

// Binds and returns the port actually taken (srt_bind(0) autoselects one).
// A failure here is the case's own setup failing, not a verdict.
uint16_t st_bind_ok(SRTSOCKET s, const char* ip, uint16_t port) {
    sockaddr_in a = st_addr(ip, port);
    if (srt_bind(s, (sockaddr*)&a, sizeof a) == SRT_ERROR) {
        st_failf(__FILE__, __LINE__, "setup: srt_bind(%s:%u) failed: %s", ip, (unsigned)port,
                 srt_getlasterror_str());
    }
    int n = (int)sizeof a;
    std::memset(&a, 0, sizeof a);
    if (srt_getsockname(s, (sockaddr*)&a, &n) == SRT_ERROR) {
        st_failf(__FILE__, __LINE__, "setup: srt_getsockname(%s:%u) failed: %s", ip,
                 (unsigned)port, srt_getlasterror_str());
    }
    return ntohs(a.sin_port);
}

// The address a bound socket reports, as "ip:port".  Deliberately compares
// address *and* port in one string so a failure names both halves.
std::string st_name_of(SRTSOCKET s) {
    sockaddr_in a;
    int n = (int)sizeof a;
    std::memset(&a, 0, sizeof a);
    if (srt_getsockname(s, (sockaddr*)&a, &n) == SRT_ERROR) {
        st_failf(__FILE__, __LINE__, "srt_getsockname failed: %s", srt_getlasterror_str());
    }
    char buf[64];
    if (a.sin_family != AF_INET) {
        st_failf(__FILE__, __LINE__, "getsockname reported family %d, expected AF_INET (%d)",
                 (int)a.sin_family, AF_INET);
    }
    if (inet_ntop(AF_INET, &a.sin_addr, buf, sizeof buf) == nullptr) {
        st_failf(__FILE__, __LINE__, "inet_ntop failed on the bound address");
    }
    char out[96];
    std::snprintf(out, sizeof out, "%s:%u", buf, (unsigned)ntohs(a.sin_port));
    return std::string(out);
}

// A failed bind must be reported as a library-level conflict (5015) and not
// as whatever the operating system said.  It also makes what the OS *would*
// have said visible in the failure message, which is the interesting half at
// v1.4.3 where the syscall is reached and returns SRT_ESOCKFAIL (1003).
void st_expect_conflict(int rc, int err, const char* what) {
    if (rc != SRT_ERROR) {
        st_failf(__FILE__, __LINE__,
                 "%s: expected srt_bind to fail with SRT_EBINDCONFLICT (%d), but it returned %d "
                 "(bind accepted)",
                 what, kEbindconflict, rc);
    }
    if (err != kEbindconflict) {
        st_failf(__FILE__, __LINE__,
                 "%s: expected srt_bind to fail with SRT_EBINDCONFLICT (%d), got %d "
                 "(a system-level failure, or a different code)",
                 what, kEbindconflict, err);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// g05_conflict_errcode -- RQ-08-05.
//
// `SRT_EBINDCONFLICT` is 5015 and renders its own message.  Three rendered
// strings, pairwise different, the first non-empty.  The stored text is not
// compared against a fixed string: `interface.md` §8 says to word it however
// you like, and requiring particular words would be asserting something the
// candidate was never told.  What is forbidden is a `srt_strerror` that falls
// back to one string for anything it does not recognise -- and 5011 is the
// right second comparison rather than a generic not-supported string, because
// MN_BUSY (11) already has text of its own.
//
// Zero sockets, zero network, zero timing.
// ---------------------------------------------------------------------------

SRT_CASE(g05_conflict_errcode, new_code_is_own_message) {
    const std::string conflict = srt_strerror(kEbindconflict, 0);
    const std::string unassigned = srt_strerror(kUnassignedMinor, 0);
    const std::string duplisten = srt_strerror(SRT_EDUPLISTEN, 0);

    ST_REQUIRE_MSG(!conflict.empty(),
                   "srt_strerror(SRT_EBINDCONFLICT=%d, 0) rendered an empty string",
                   kEbindconflict);
    ST_REQUIRE_MSG(conflict != unassigned,
                   "srt_strerror(%d) and srt_strerror(%d) render identically (\"%s\"): the new "
                   "code has no message of its own and is falling through to the undefined-code "
                   "string",
                   kEbindconflict, kUnassignedMinor, conflict.c_str());
    ST_REQUIRE_MSG(conflict != duplisten,
                   "srt_strerror(%d) and srt_strerror(SRT_EDUPLISTEN=%d) render identically "
                   "(\"%s\"): the new code is sharing the already-listening message",
                   kEbindconflict, SRT_EDUPLISTEN, conflict.c_str());
    ST_REQUIRE_MSG(unassigned != duplisten,
                   "srt_strerror(%d) and srt_strerror(%d) render identically (\"%s\"): the "
                   "comparison is degenerate, so the two checks above prove nothing",
                   kUnassignedMinor, SRT_EDUPLISTEN, unassigned.c_str());
}

// The code is a new *minor* of the existing major, not a renumbering.  A tree
// that appended the entry by shifting later minors would still render a
// message for 5015 and would still pass the case above; it fails here,
// because a code that already existed has moved.  5014 (MN_EEMPTY) and 5011
// (MN_BUSY) are the neighbours the append sits between.
//
// "Has moved" is observable without knowing any message text: if a row was
// inserted rather than appended, two of the minors around it stop being
// distinguishable from one another, and one of them starts rendering the
// text that the new code was supposed to have to itself.  So the neighbours
// are compared *against each other and against the new code*, not against
// remembered strings -- which is also why this case does not pin the text of
// MN_EEMPTY or MN_BUSY, neither of which the candidate was told to word any
// particular way.
SRT_CASE(g05_conflict_errcode, neighbours_unnumbered) {
    const std::string conflict = srt_strerror(kEbindconflict, 0);
    const std::string eempty = srt_strerror(5014, 0);
    const std::string duplisten = srt_strerror(SRT_EDUPLISTEN, 0);
    const std::string undefined = srt_strerror(kUnassignedMinor, 0);

    ST_REQUIRE_MSG(!eempty.empty() && eempty != undefined,
                   "srt_strerror(5014) renders \"%s\", the same string as an unassigned "
                   "minor (%d): MN_EEMPTY's row is gone and the new minor was inserted by "
                   "renumbering rather than appended",
                   5014, eempty.c_str(), kUnassignedMinor);

    ST_REQUIRE_MSG(!duplisten.empty() && duplisten != undefined,
                   "srt_strerror(SRT_EDUPLISTEN=%d) renders \"%s\", the same string as an "
                   "unassigned minor (%d): a pre-existing code was renumbered",
                   SRT_EDUPLISTEN, duplisten.c_str(), kUnassignedMinor);

    ST_REQUIRE_MSG(eempty != duplisten,
                   "srt_strerror(5014) and srt_strerror(SRT_EDUPLISTEN=%d) both render "
                   "\"%s\": two codes that had their own messages now share one, so the "
                   "table was edited in place rather than appended to",
                   SRT_EDUPLISTEN, eempty.c_str());

    ST_REQUIRE_MSG(eempty != conflict && duplisten != conflict,
                   "srt_strerror(%d) renders \"%s\", which is also what %s renders: the new "
                   "code did not get a fresh row, a neighbour's row was renamed in place",
                   kEbindconflict, conflict.c_str(),
                   eempty == conflict ? "srt_strerror(5014)" : "srt_strerror(SRT_EDUPLISTEN)");
}

// ---------------------------------------------------------------------------
// g06_wildcard_conflict -- RQ-08-06.
//
// Intersecting bindings are refused before the operating system is asked, in
// both orders, and the socket that bound first is not disturbed.  All of it
// is `updateMux` (api.cpp:2839-2995): the wildcard-versus-explicit branch at
// :2904-:2910 and the reverse at :2933-:2937.
//
// At v1.4.3 the whole reuse search was gated on `bReuseAddr` and matched on
// `(m_iIPversion == addr.family()) && (m_mcfg == config)` -- an address
// specification was never compared at all -- so a loopback bind was silently
// satisfied by a wildcard multiplexer and both binds returned 0.  That is
// what these two cases catch.
// ---------------------------------------------------------------------------

// `srt_getsockstate` reports `SRT_SOCKSTATUS`, which has no "bound" member:
// a bound-but-not-listening socket is `SRTS_OPENED` (srt.h:172-182, SRTS_INIT
// = 1, SRTS_OPENED = 2, SRTS_LISTENING = 3).  Named here so a wrong constant
// fails loudly rather than silently comparing two equal numbers.
static_assert(SRTS_INIT == 1 && SRTS_OPENED == 2 && SRTS_LISTENING == 3,
              "SRT_SOCKSTATUS has been renumbered; re-derive g06's state check");

SRT_CASE(g06_wildcard_conflict, wildcard_then_explicit) {
    SRTSOCKET a = st_new_socket();
    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK && b != SRT_INVALID_SOCK,
                   "srt_socket failed: %s", srt_getlasterror_str());

    const uint16_t port = st_bind_ok(a, "0.0.0.0", 0);

    sockaddr_in sb = st_addr("127.0.0.1", port);
    int rc = srt_bind(b, (sockaddr*)&sb, sizeof sb);
    int err = srt_getlasterror(nullptr);
    st_expect_conflict(rc, err, "bind 127.0.0.1 onto a port already held by 0.0.0.0");

    // The first socket is undisturbed: it keeps the wildcard address it asked
    // for, and the refused attempt did not unref or close its multiplexer.
    ST_REQUIRE_MSG(st_name_of(a) == std::string("0.0.0.0:") + std::to_string(port),
                   "the wildcard socket's own address changed to %s after the refused bind "
                   "(expected 0.0.0.0:%u)",
                   st_name_of(a).c_str(), (unsigned)port);
    ST_REQUIRE_MSG(srt_getsockstate(a) == SRTS_OPENED ||
                       srt_getsockstate(a) == SRTS_LISTENING,
                   "the wildcard socket is in state %d after the refused bind, expected OPENED "
                   "(%d) or LISTENING (%d): the refusal disturbed it",
                   (int)srt_getsockstate(a), (int)SRTS_OPENED, (int)SRTS_LISTENING);
    ST_REQUIRE_MSG(srt_listen(a, 1) != SRT_ERROR,
                   "the wildcard socket can no longer be listened on after the refused bind: %s",
                   srt_getlasterror_str());

    srt_close(a);
    srt_close(b);
}

SRT_CASE(g06_wildcard_conflict, explicit_then_wildcard) {
    SRTSOCKET a = st_new_socket();
    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK && b != SRT_INVALID_SOCK,
                   "srt_socket failed: %s", srt_getlasterror_str());

    const uint16_t port = st_bind_ok(a, "127.0.0.1", 0);

    sockaddr_in sb = st_addr("0.0.0.0", port);
    int rc = srt_bind(b, (sockaddr*)&sb, sizeof sb);
    int err = srt_getlasterror(nullptr);
    st_expect_conflict(rc, err, "bind 0.0.0.0 onto a port already held by 127.0.0.1");

    ST_REQUIRE_MSG(st_name_of(a) == std::string("127.0.0.1:") + std::to_string(port),
                   "the explicit socket's own address changed to %s after the refused bind "
                   "(expected 127.0.0.1:%u)",
                   st_name_of(a).c_str(), (unsigned)port);
    ST_REQUIRE_MSG(srt_listen(a, 1) != SRT_ERROR,
                   "the explicit socket can no longer be listened on after the refused bind: %s",
                   srt_getlasterror_str());

    srt_close(a);
    srt_close(b);
}

// The positive control for the two refusals above: a candidate that refuses
// *every* bind would pass both of them.  Two non-intersecting explicit
// addresses on one port must both succeed.
SRT_CASE(g06_wildcard_conflict, distinct_addresses_both_succeed) {
    SRTSOCKET a = st_new_socket();
    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK && b != SRT_INVALID_SOCK,
                   "srt_socket failed: %s", srt_getlasterror_str());

    const uint16_t port = st_bind_ok(a, "127.0.0.1", 0);

    sockaddr_in sb = st_addr("127.0.0.2", port);
    int rc = srt_bind(b, (sockaddr*)&sb, sizeof sb);
    int err = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc != SRT_ERROR,
                   "control: srt_bind(127.0.0.2:%u) was refused (%d) although 127.0.0.1:%u is a "
                   "non-intersecting binding",
                   (unsigned)port, err, (unsigned)port);

    srt_close(a);
    srt_close(b);
}

// ---------------------------------------------------------------------------
// g07_separate_muxers -- RQ-08-07.
//
// Two different explicit loopback addresses on one and the same port each get
// their own UDP endpoint, and `srt_getsockname` reports on each the address
// that socket itself passed to `srt_bind` -- before any connection exists.
//
// At v1.4.3 the reuse path set `s->m_SelfAddr.family(addr.family())` and
// nothing else (api.cpp:2879), so the second socket reported the *first*
// socket's address: the endpoint was silently shared and only the family was
// recorded.  The first case below is the changed behaviour; the second is the
// positive control that the same call does report the shared address when the
// multiplexer really is shared, so the assertion is about which address is
// reported and not about `getsockname` being broken.
// ---------------------------------------------------------------------------

SRT_CASE(g07_separate_muxers, each_reports_its_own_address) {
    SRTSOCKET a = st_new_socket();
    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK && b != SRT_INVALID_SOCK,
                   "srt_socket failed: %s", srt_getlasterror_str());

    const uint16_t port = st_bind_ok(a, "127.0.0.1", 0);

    sockaddr_in sb = st_addr("127.0.0.2", port);
    if (srt_bind(b, (sockaddr*)&sb, sizeof sb) == SRT_ERROR) {
        st_failf(__FILE__, __LINE__,
                 "srt_bind(127.0.0.2:%u) failed: %s -- two non-intersecting explicit addresses "
                 "on one port must both succeed (RQ-08-07)",
                 (unsigned)port, srt_getlasterror_str());
    }

    const std::string na = st_name_of(a);
    const std::string nb = st_name_of(b);
    const std::string want_a = std::string("127.0.0.1:") + std::to_string(port);
    const std::string want_b = std::string("127.0.0.2:") + std::to_string(port);

    ST_REQUIRE_MSG(na == want_a,
                   "socket A bound 127.0.0.1:%u but srt_getsockname reports %s",
                   (unsigned)port, na.c_str());
    ST_REQUIRE_MSG(nb == want_b,
                   "socket B bound 127.0.0.2:%u but srt_getsockname reports %s "
                   "(expected %s): the sockets are sharing one endpoint and B is reporting the "
                   "address of whichever one the implementation chose to carry it",
                   (unsigned)port, nb.c_str(), want_b.c_str());
    ST_REQUIRE_MSG(na != nb,
                   "A and B report the same address (%s) although they bound different ones",
                   na.c_str());

    srt_close(a);
    srt_close(b);
}

// The control: when the multiplexer genuinely *is* shared, both sockets
// report the same address -- which is correct, because reaching that branch
// already required identical address specifications.  Without this, a tree
// that made every socket report something arbitrary would pass the case
// above.
SRT_CASE(g07_separate_muxers, shared_endpoint_reports_shared_address) {
    SRTSOCKET a = st_new_socket();
    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK && b != SRT_INVALID_SOCK,
                   "srt_socket failed: %s", srt_getlasterror_str());

    const uint16_t port = st_bind_ok(a, "127.0.0.1", 0);

    sockaddr_in sb = st_addr("127.0.0.1", port);
    if (srt_bind(b, (sockaddr*)&sb, sizeof sb) == SRT_ERROR) {
        st_failf(__FILE__, __LINE__,
                 "control: srt_bind(127.0.0.1:%u) with settings identical to the socket already "
                 "on that port failed: %s -- identical settings must share the multiplexer",
                 (unsigned)port, srt_getlasterror_str());
    }

    const std::string want = std::string("127.0.0.1:") + std::to_string(port);
    ST_REQUIRE_MSG(st_name_of(a) == want, "socket A reports %s, expected %s",
                   st_name_of(a).c_str(), want.c_str());
    ST_REQUIRE_MSG(st_name_of(b) == want, "socket B reports %s, expected %s",
                   st_name_of(b).c_str(), want.c_str());

    srt_close(a);
    srt_close(b);
}

// ---------------------------------------------------------------------------
// g08_settings_conflict -- RQ-08-08.
//
// The reuse decision is `channelSettingsMatch` (api.cpp:2860-2863):
// `m.m_mcfg.bReuseAddr && m.m_mcfg == s->core().m_config`.  So an *identical*
// address specification is refused as a conflict rather than as a system
// failure whenever the settings differ, and shared when they all match.
// `CSrtMuxerConfig::operator==` (socketconfig.h:91-101) compares iIpTTL,
// iIpToS, iIpV6Only, bReuseAddr, sBindToDevice and the two UDP buffer sizes,
// and not the address -- the address is handled by the loop's branching.
//
// At v1.4.3 the same address never even reached a settings comparison: the
// loop's gate was `bReuseAddr` on the *requesting* socket, so with REUSEADDR
// false the search was skipped entirely and the two sockets went to two
// separate `::bind` calls, of which the second failed with SRT_ESOCKFAIL
// (1003) -- the system-level failure RQ-08-08 says must not be what the
// candidate reports.
// ---------------------------------------------------------------------------

// The first of the three conditions, and the one that names the
// nodecision-before-the-syscall property most directly: REUSEADDR false on
// both sockets, identical address, refused as a conflict.
SRT_CASE(g08_settings_conflict, reuseaddr_off_is_conflict) {
    SRTSOCKET a = st_new_socket();
    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK && b != SRT_INVALID_SOCK,
                   "srt_socket failed: %s", srt_getlasterror_str());

    const int zero = 0;
    st_set_int(a, SRTO_REUSEADDR, zero);
    st_set_int(b, SRTO_REUSEADDR, zero);

    const uint16_t port = st_bind_ok(a, "127.0.0.1", 0);

    sockaddr_in sb = st_addr("127.0.0.1", port);
    int rc = srt_bind(b, (sockaddr*)&sb, sizeof sb);
    int err = srt_getlasterror(nullptr);
    st_expect_conflict(rc, err, "identical address on one port with SRTO_REUSEADDR false on both");

    srt_close(a);
    srt_close(b);
}

// The third condition: identical address and port, but different
// UDP channel settings.  Same-address is the point -- the settings are what
// differ, and `SRTO_UDP_RCVBUF` is one of the six fields `operator==` reads.
SRT_CASE(g08_settings_conflict, differing_channel_settings_is_conflict) {
    SRTSOCKET a = st_new_socket();
    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK && b != SRT_INVALID_SOCK,
                   "srt_socket failed: %s", srt_getlasterror_str());

    const int big = 1 << 20;
    st_set_int(a, SRTO_UDP_RCVBUF, big);

    const uint16_t port = st_bind_ok(a, "127.0.0.1", 0);

    // B keeps the default UDP receive buffer, so its muxer config differs
    // from A's in exactly one field and its bind must be refused.
    sockaddr_in sb = st_addr("127.0.0.1", port);
    int rc = srt_bind(b, (sockaddr*)&sb, sizeof sb);
    int err = srt_getlasterror(nullptr);
    st_expect_conflict(rc, err,
                       "identical address on one port where the two sockets carry different "
                       "SRTO_UDP_RCVBUF values");

    srt_close(a);
    srt_close(b);
}

// The positive control the whole group needs: an identical address and port
// with settings that all match shares the multiplexer and both binds succeed.
// Without it, a tree that refused every intersecting bind -- the
// over-correction RQ-08-09 warns about -- would take this group for free.
SRT_CASE(g08_settings_conflict, matching_settings_share_binding) {
    SRTSOCKET a = st_new_socket();
    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK && b != SRT_INVALID_SOCK,
                   "srt_socket failed: %s", srt_getlasterror_str());

    const uint16_t port = st_bind_ok(a, "127.0.0.1", 0);

    sockaddr_in sb = st_addr("127.0.0.1", port);
    int rc = srt_bind(b, (sockaddr*)&sb, sizeof sb);
    int err = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc != SRT_ERROR,
                   "control: srt_bind(127.0.0.1:%u) was refused with %d although both sockets "
                   "have SRTO_REUSEADDR true and identical channel settings -- the three sharing "
                   "conditions all hold and the bind must succeed",
                   (unsigned)port, err);

    const std::string want = std::string("127.0.0.1:") + std::to_string(port);
    ST_REQUIRE_MSG(st_name_of(a) == want, "socket A reports %s, expected %s",
                   st_name_of(a).c_str(), want.c_str());
    ST_REQUIRE_MSG(st_name_of(b) == want, "socket B reports %s, expected %s",
                   st_name_of(b).c_str(), want.c_str());

    srt_close(a);
    srt_close(b);
}

// A fourth field, to show the refusal is the settings comparison and not a
// special case written for REUSEADDR or for the buffer sizes.  `SRTO_IPTTL`
// is one of the six fields `operator==` reads that no other case here
// touches.
SRT_CASE(g08_settings_conflict, differing_ttl_is_conflict) {
    SRTSOCKET a = st_new_socket();
    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK && b != SRT_INVALID_SOCK,
                   "srt_socket failed: %s", srt_getlasterror_str());

    const int ttl = 32;
    st_set_int(a, SRTO_IPTTL, ttl);

    const uint16_t port = st_bind_ok(a, "127.0.0.1", 0);

    sockaddr_in sb = st_addr("127.0.0.1", port);
    int rc = srt_bind(b, (sockaddr*)&sb, sizeof sb);
    int err = srt_getlasterror(nullptr);
    st_expect_conflict(rc, err,
                       "identical address on one port where the two sockets carry different "
                       "SRTO_IPTTL values");

    srt_close(a);
    srt_close(b);
}
