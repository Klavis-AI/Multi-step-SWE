// Family "handshake", checkpoint 02 (SRT v1.3.0). Three scored groups:
//
//   g01_version    new   the version surface: a socket's own protocol version,
//                        the peer's before and at the moment `srt_connect`
//                        returns, the state binding of a set, and the
//                        minimum-peer-version default.
//   g02_latency    new   the latency split: SRTO_RCVLATENCY / SRTO_PEERLATENCY
//                        on an unconnected socket, the max() negotiation that
//                        replaces them on connect, and SRTO_LATENCY as the
//                        alias for the pair.
//   g03_streamid   new   SRTO_STREAMID: the local read-back and its optlen
//                        contract, the ride across the handshake to the
//                        accepted socket, the listener's empty value, and the
//                        512-byte limit.
//
// ------------------------------------------------------------------- tiers
//
// All three groups are `new`, so this family is compiled only against the
// milestone it scores. It names SRTO_MINVERSION, SRTO_RCVLATENCY,
// SRTO_PEERLATENCY and SRTO_STREAMID directly, and none of the four exists in
// v1.2.0's <srt/srt.h> -- that header's SRT_SOCKOPT enum ends at
// SRTO_LOSSMAXTTL, four slots below SRTO_RCVLATENCY -- so the family does not
// build against the previous milestone's prefix at all.
//
// That is deliberate and it is the intended outcome, not an oversight.
// DRIVER_RULES' dlsym / guarded-#ifndef restatement rules exist so that a
// family holding an `inherited` or `peripheral` group stays buildable at the
// previous step, where that group is meant to be earned. This family holds no
// such group. Restating the four option ids here would only let a v1.2.0 build
// reach assertions it then fails -- while adding the standing risk that a
// restated number silently addresses a different option. tests/test.sh builds
// each family on its own and fails only that family's groups when it does not
// compile, so the cost of not building at v1.2.0 falls exactly on the three
// groups that are supposed to fail there.
//
// ------------------------------------------------------------ traceability
//
// Every assertion below comes from the step's own definition -- stepdefs/02/
// scope.md and interface.md, restated by instruction.md -- and from the rows of
// deltas/02.md (D3, D8) and deltas/02-headline.md (H2, H3, H4, H5, H6, H7) that
// this family is assigned. Where `srt-docs/` and interface.md disagree,
// interface.md is authoritative by its own first paragraph, and it disagrees
// about SRTO_MINVERSION (readable, non-zero default) and about SRTO_VERSION
// (settable while unconnected; never reports the peer's version).
//
// Deliberately NOT asserted, with reasons:
//
//   * any particular version NUMBER. scope.md: "The version *value* you report
//     is yours -- the verifier asserts that it is non-zero, that it has that
//     shape, that the minimum-version default does not exceed it, and that two
//     peers of your own library agree." So g01 asserts exactly those four
//     things and never compares against 0x010300 or against SRT_VERSION_VALUE.
//   * the live preset's latency of 120 ms, in any form. interface.md: "The live
//     preset's SRTO_RCVLATENCY = 120 is not observable as a read-back ... No
//     group asserts a latency value through the transmission-type round trip,
//     and none may." g02 therefore configures BOTH numbers it reads back and
//     never reads a latency that a default contributed to: on each connected
//     socket it reads only the one figure that is a max() of two values this
//     file chose.
//   * the latency options on a BOUND but unconnected socket. interface.md
//     reserves that state ("no group reads either option on a bound
//     unconnected socket").
//   * which side of a handshake reports what about HSv4 vs HSv5. The handshake
//     version is not exposed through the public C API at all (deltas/
//     02-headline.md, "Negative result"), so the groups observe its effects --
//     version exchange timing, the latency split, the streamid ride -- and
//     never the version itself.
//   * whether a set of SRTO_VERSION to a value OTHER than the socket's own is
//     honoured. interface.md documents that the set is accepted before connect
//     and refused after; it does not document that a foreign value takes
//     effect, and m_lSrtVersion feeds the handshake, so asserting a round trip
//     there would be asserting an undocumented side effect.
//   * SRTO_STREAMID on the listener BEFORE it accepts. A listening socket is
//     not connected, so the released library accepts a set there; nothing
//     candidate-facing says it must, so g03 only ever reads the listener.
//
// ------------------------------------------------------------ local helpers
//
// srtkit.h has no string GETTER (st_set_str sets, nothing reads back), and
// g03's whole subject is what the getter does with `optlen`. hs_get_sid below
// is that reader and it lives here rather than in drv/ because drv/ headers
// must compile at v1.2.0, where SRTO_STREAMID does not exist.

#include "stest.h"
#include "srtkit.h"
#include "relay.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------- naming

static const char* hs_errname(int e) {
    switch (e) {
    case SRT_SUCCESS:       return "SRT_SUCCESS";
    case SRT_EINVOP:        return "SRT_EINVOP";
    case SRT_EBOUNDSOCK:    return "SRT_EBOUNDSOCK";
    case SRT_ECONNSOCK:     return "SRT_ECONNSOCK";
    case SRT_EINVPARAM:     return "SRT_EINVPARAM";
    case SRT_EINVSOCK:      return "SRT_EINVSOCK";
    case SRT_EUNBOUNDSOCK:  return "SRT_EUNBOUNDSOCK";
    case SRT_ENOLISTEN:     return "SRT_ENOLISTEN";
    case SRT_ECONNLOST:     return "SRT_ECONNLOST";
    case SRT_ENOCONN:       return "SRT_ENOCONN";
    case SRT_ECONNSETUP:    return "SRT_ECONNSETUP";
    case SRT_ECONNREJ:      return "SRT_ECONNREJ";
    default:                return "an unlisted SRT error";
    }
}

// Versions are read as ints but written as 0xXXYYZZ, so every message that
// names one shows both forms -- a reviewer comparing "66304" against "0"
// learns nothing, and 0x010300 vs 0x000000 is the whole story.
static std::string hs_vstr(int v) {
    char b[64];
    std::snprintf(b, sizeof b, "0x%06x (%d.%d.%d)", (unsigned)v & 0xFFFFFFu,
                  (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return std::string(b);
}

// A printable, bounded rendering of a stream id, for failure messages.
static std::string hs_show(const std::string& s) {
    std::string out;
    const size_t lim = 24;
    for (size_t i = 0; i < s.size() && i < lim; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c < 0x7F) out += (char)c;
        else {
            char b[8];
            std::snprintf(b, sizeof b, "\\x%02x", c);
            out += b;
        }
    }
    if (s.size() > lim) out += "...";
    return out;
}

// ------------------------------------------------------- non-throwing probes
//
// The groups whose subject is a refusal need the return code AND the errno
// that came with it, which srtkit's throwing helpers swallow.

struct HsRc {
    int rc = 0;
    int err = SRT_SUCCESS;
};

static HsRc hs_set_int(SRTSOCKET s, SRT_SOCKOPT o, int v) {
    HsRc r;
    r.rc = srt_setsockflag(s, o, &v, sizeof v);
    if (r.rc == SRT_ERROR) r.err = srt_getlasterror(NULL);
    return r;
}

static HsRc hs_set_sid(SRTSOCKET s, const char* p, int len) {
    HsRc r;
    r.rc = srt_setsockflag(s, SRTO_STREAMID, p, len);
    if (r.rc == SRT_ERROR) r.err = srt_getlasterror(NULL);
    return r;
}

// ------------------------------------------------------- the stream id reader
//
// Reads SRTO_STREAMID into a buffer of exactly `bufsize` declared bytes with a
// guard area behind it, so the case can see three things at once: what the
// getter returned, what it wrote through `optlen`, and whether it respected
// the size it was given.

static const char HS_FILL = (char)0xA5;
static const int  HS_GUARD = 32;

struct HsSid {
    int rc = 0;
    int err = SRT_SUCCESS;
    int optlen = -1;         // what the getter wrote back through optlen
    std::string value;       // the first `optlen` bytes it produced
    bool nul_after = false;  // the byte at [optlen] was '\0'
    bool guard_ok = true;    // nothing was written past the declared buffer
};

static HsSid hs_get_sid(SRTSOCKET s, int bufsize) {
    HsSid g;
    std::vector<char> buf((size_t)bufsize + (size_t)HS_GUARD, HS_FILL);
    int n = bufsize;
    g.rc = srt_getsockflag(s, SRTO_STREAMID, buf.data(), &n);
    if (g.rc == SRT_ERROR) {
        g.err = srt_getlasterror(NULL);
        return g;
    }
    g.optlen = n;
    if (n >= 0 && n <= bufsize) {
        g.value.assign(buf.data(), (size_t)n);
        if (n < bufsize) g.nul_after = (buf[(size_t)n] == '\0');
    }
    for (int i = bufsize; i < bufsize + HS_GUARD; i++)
        if (buf[(size_t)i] != HS_FILL) g.guard_ok = false;
    return g;
}

// A buffer comfortably larger than the 512-byte limit, for reads that are
// expected to succeed whatever is stored.
static const int HS_SID_BUF = 600;

// The documented stream id limit, in bytes (scope.md: "a short string ... a
// value beyond the documented length limit is refused"; interface.md: "at most
// 512 characters ... refused with SRT_EINVPARAM when optlen exceeds 512").
static const int HS_SID_MAX = 512;

// =====================================================================
// g01_version
//
// scored.txt: "a fresh socket reports its own protocol version in the
// documented three-byte form and zero for the peer's, accepts a set of that
// option while unconnected and refuses it once connected, reports the
// minimum-peer-version option at a non-zero default no greater than its own
// version, and reports the peer's version as equal to its own the moment
// `srt_connect` returns, before any payload has crossed".
// Rows: D3, H4, H5, H6.
// =====================================================================

// H4 + the SRTO_PEERVERSION half of H5's precondition. The positive and the
// negative sit in one case on one socket: the same getter machinery must
// produce a real version for the local option and exactly zero for the peer's,
// so neither "everything reads zero" nor "everything reads the local version"
// survives.
SRT_CASE(g01_version, fresh_socket_reports_own_and_zero_peer) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    const int own = st_get_int(s, SRTO_VERSION);
    ST_REQUIRE_MSG(own != 0,
                   "SRTO_VERSION on a fresh socket: expected this library's own "
                   "non-zero protocol version, got 0");
    ST_REQUIRE_MSG(((unsigned)own >> 24) == 0,
                   "SRTO_VERSION on a fresh socket: expected the documented "
                   "three-byte 0xXXYYZZ form, got 0x%08x with bits set above "
                   "byte 3", (unsigned)own);

    const int peer = st_get_int(s, SRTO_PEERVERSION);
    ST_REQUIRE_MSG(peer == 0,
                   "SRTO_PEERVERSION on a fresh, never-connected socket: "
                   "expected 0, got %s (own version is %s)",
                   hs_vstr(peer).c_str(), hs_vstr(own).c_str());

    srt_close(s);
}

// D3 / interface.md: the option table's "(r)" marking is wrong -- a set is
// accepted while the socket is unconnected and refused with SRT_ECONNSOCK once
// it is connected. Both halves are on the SAME socket, in order, so a library
// that refuses every set fails the first half and one that accepts every set
// fails the second. Neither can be passed by accident.
SRT_CASE(g01_version, set_accepted_unconnected_refused_connected) {
    struct Pre {
        int before = -1;
        int rc = -99;
        int err = SRT_SUCCESS;
        int after = -1;
    } pre;

    Link L;
    RelaySched sched;
    st_connect(L, sched, st_noconf, [&pre](SRTSOCKET c) {
        pre.before = st_get_int(c, SRTO_VERSION);
        HsRc r = hs_set_int(c, SRTO_VERSION, pre.before);
        pre.rc = r.rc;
        pre.err = r.err;
        if (r.rc != SRT_ERROR) pre.after = st_get_int(c, SRTO_VERSION);
    });

    ST_REQUIRE_MSG(pre.rc == 0,
                   "srt_setsockflag(SRTO_VERSION, %s) on the UNCONNECTED caller: "
                   "expected 0, got %d / %s(%d)",
                   hs_vstr(pre.before).c_str(), pre.rc, hs_errname(pre.err), pre.err);
    ST_REQUIRE_MSG(pre.after == pre.before,
                   "SRTO_VERSION after an accepted set on the unconnected caller: "
                   "expected the local version to still read %s, got %s",
                   hs_vstr(pre.before).c_str(), hs_vstr(pre.after).c_str());

    // Same socket, now connected: the set must be refused, and refused as a
    // state error rather than as an unknown option.
    const int cal_v = st_get_int(L.cal, SRTO_VERSION);
    HsRc post = hs_set_int(L.cal, SRTO_VERSION, cal_v);
    ST_REQUIRE_MSG(post.rc == SRT_ERROR,
                   "srt_setsockflag(SRTO_VERSION, %s) on the CONNECTED caller: "
                   "expected SRT_ERROR (-1), got %d",
                   hs_vstr(cal_v).c_str(), post.rc);
    ST_REQUIRE_MSG(post.err == SRT_ECONNSOCK,
                   "srt_setsockflag(SRTO_VERSION) on the CONNECTED caller: "
                   "expected SRT_ECONNSOCK(%d), got %s(%d)",
                   (int)SRT_ECONNSOCK, hs_errname(post.err), post.err);

    const int acc_v = st_get_int(L.acc, SRTO_VERSION);
    HsRc post2 = hs_set_int(L.acc, SRTO_VERSION, acc_v);
    ST_REQUIRE_MSG(post2.rc == SRT_ERROR,
                   "srt_setsockflag(SRTO_VERSION, %s) on the CONNECTED accepted "
                   "socket: expected SRT_ERROR (-1), got %d",
                   hs_vstr(acc_v).c_str(), post2.rc);
    ST_REQUIRE_MSG(post2.err == SRT_ECONNSOCK,
                   "srt_setsockflag(SRTO_VERSION) on the CONNECTED accepted "
                   "socket: expected SRT_ECONNSOCK(%d), got %s(%d)",
                   (int)SRT_ECONNSOCK, hs_errname(post2.err), post2.err);
}

// H6 / interface.md: the option table marks SRTO_MINVERSION "(writeonly)" with
// a default of 0 and is wrong twice -- "A get succeeds at every socket state
// and a fresh socket reports 0x010000 ... g01_version asserts that the default
// is non-zero and does not exceed what SRTO_VERSION reports".
//
// Exactly those two properties are asserted, and the value 0x010000 is NOT,
// because the scored description deliberately leaves the number to the
// candidate ("a non-zero default no greater than its own version"). The
// three-byte guard is there because `minv <= own` alone would accept a
// negative int, which the document's own 0x010000 rules out.
//
// interface.md's "at every socket state" is why the same two properties are
// then read off a CONNECTED socket: a getter wired only into the unconnected
// path fails there.
//
// NOT asserted: what a set of SRTO_MINVERSION reads back. interface.md
// corrects the "(writeonly)" marking and gives the default, but says nothing
// about a read-back of a configured value, so this file does not require one.
SRT_CASE(g01_version, minversion_default_nonzero_and_within_own) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    const int own = st_get_int(s, SRTO_VERSION);
    ST_REQUIRE_MSG(own != 0,
                   "SRTO_VERSION on a fresh socket: expected a non-zero version "
                   "to compare the minimum against, got 0");

    const int minv = st_get_int(s, SRTO_MINVERSION);
    ST_REQUIRE_MSG(minv != 0,
                   "SRTO_MINVERSION on a fresh socket: expected a non-zero "
                   "default minimum peer version, got 0");
    ST_REQUIRE_MSG(((unsigned)minv >> 24) == 0,
                   "SRTO_MINVERSION on a fresh socket: expected the documented "
                   "three-byte 0xXXYYZZ form, got 0x%08x", (unsigned)minv);
    ST_REQUIRE_MSG(minv <= own,
                   "SRTO_MINVERSION default %s: expected it not to exceed this "
                   "socket's own SRTO_VERSION %s -- a socket that demands more "
                   "than it is could not talk to itself",
                   hs_vstr(minv).c_str(), hs_vstr(own).c_str());
    srt_close(s);

    // "A get succeeds at every socket state."
    Link L;
    RelaySched sched;
    st_connect(L, sched);
    const int c_own = st_get_int(L.cal, SRTO_VERSION);
    const int c_min = st_get_int(L.cal, SRTO_MINVERSION);
    ST_REQUIRE_MSG(c_min != 0,
                   "SRTO_MINVERSION on a CONNECTED socket: expected the same "
                   "non-zero default the unconnected socket reported (%s), got 0",
                   hs_vstr(minv).c_str());
    ST_REQUIRE_MSG(c_min <= c_own,
                   "SRTO_MINVERSION %s on a connected socket: expected it not to "
                   "exceed that socket's own SRTO_VERSION %s",
                   hs_vstr(c_min).c_str(), hs_vstr(c_own).c_str());
}

// H5, the headline of this group: with HSv5 the version exchange happens inside
// the connection handshake, so the peer's version is known the instant
// srt_connect returns -- not after the first data exchange, which is the
// milestone-01 behaviour this must fail on.
//
// "Before any payload has crossed" is guaranteed by construction rather than
// asserted: this case never calls srt_sendmsg or srt_recvmsg, and the peer
// versions are the first thing read after st_connect returns. An srt_bstats
// counter check here would assert something interface.md does not state about
// what the packet counters include during a handshake, so it is left out.
//
// Two controls keep the case honest:
//   * the LISTENER's SRTO_PEERVERSION must still be 0 -- it never handshook, so
//     a getter that simply echoes the local version fails here. interface.md
//     leaves the table's "reports 0 when not connected" standing for that state
//     and corrects only *when* a connected socket stops reporting 0.
//   * the accepted socket must report the caller's version too, which is the
//     "two peers of your own library agree" clause of scope.md.
SRT_CASE(g01_version, peer_version_known_when_connect_returns) {
    Link L;
    RelaySched sched;
    st_connect(L, sched);

    // Read the peer versions FIRST: any other call in between would weaken the
    // "before any payload" claim.
    const int cal_peer = st_get_int(L.cal, SRTO_PEERVERSION);
    const int acc_peer = st_get_int(L.acc, SRTO_PEERVERSION);
    const int lsn_peer = st_get_int(L.lsn, SRTO_PEERVERSION);
    const int cal_own = st_get_int(L.cal, SRTO_VERSION);
    const int acc_own = st_get_int(L.acc, SRTO_VERSION);

    ST_REQUIRE_MSG(cal_peer != 0,
                   "SRTO_PEERVERSION on the caller the moment srt_connect "
                   "returned success: expected the peer's version, got 0 -- the "
                   "peer version was not exchanged inside the handshake");
    ST_REQUIRE_MSG(cal_peer == cal_own,
                   "SRTO_PEERVERSION on the caller: expected it to equal the "
                   "version the peer reports for itself, %s, got %s (both ends "
                   "are this same library)",
                   hs_vstr(cal_own).c_str(), hs_vstr(cal_peer).c_str());
    ST_REQUIRE_MSG(acc_peer == acc_own,
                   "SRTO_PEERVERSION on the accepted socket: expected %s, got %s",
                   hs_vstr(acc_own).c_str(), hs_vstr(acc_peer).c_str());

    // The listening socket performed no handshake of its own.
    ST_REQUIRE_MSG(lsn_peer == 0,
                   "SRTO_PEERVERSION on the LISTENING socket after it accepted a "
                   "connection: expected 0, got %s -- the listener itself never "
                   "handshook, so the option must not report the accepted "
                   "socket's peer", hs_vstr(lsn_peer).c_str());

    // interface.md: SRTO_VERSION reports the LOCAL version at every state; it
    // does not switch to the peer's on connect. Both ends run this library, so
    // the observable form of that claim is that the caller still reports a
    // valid local version after connecting.
    ST_REQUIRE_MSG(cal_own != 0 && ((unsigned)cal_own >> 24) == 0,
                   "SRTO_VERSION on the connected caller: expected the local "
                   "version in three-byte form, got 0x%08x", (unsigned)cal_own);
}

// =====================================================================
// g02_latency
//
// scored.txt: "the receive latency and the peer latency are separately settable
// on an unconnected socket and both read back zero there, and after a
// connection in which one side asks for a receive latency and the other
// proposes a larger peer latency the receiving side reads back the larger of
// the two rather than the value it configured, with the combined latency option
// reporting that same negotiated figure".
// Rows: H2, H3.
// =====================================================================

// Every latency this group reads back is a max() of two numbers this file
// chose, so no assertion ever depends on the live preset's default.
static const int HS_RCV_ASKED   = 180;   // what a receiver asks for
static const int HS_PEER_BIGGER = 640;   // a sender's proposal, larger than it
static const int HS_RCV_BIGGER  = 740;   // a receiver's request, larger than...
static const int HS_PEER_SMALL  = 220;   // ...this sender's proposal
static const int HS_COMBINED_LO = 300;   // SRTO_LATENCY on the listener
static const int HS_COMBINED_HI = 820;   // SRTO_LATENCY on the caller

// H2, unconnected half. Both options are settable, separately, on a socket that
// has been created and never bound, and both read back zero there regardless.
// The positive control for "reads zero" is the rest of the group: the very same
// getters report the negotiated figures on a connected socket, so a library
// whose latency getters always answer zero fails the next three cases.
SRT_CASE(g02_latency, unconnected_socket_reads_zero) {
    // Settable separately: one socket touches only SRTO_RCVLATENCY, the next
    // only SRTO_PEERLATENCY, so neither set can be riding on the other.
    SRTSOCKET a = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    HsRc ra = hs_set_int(a, SRTO_RCVLATENCY, HS_RCV_ASKED);
    ST_REQUIRE_MSG(ra.rc == 0,
                   "srt_setsockflag(SRTO_RCVLATENCY, %d) on an unconnected "
                   "socket: expected 0, got %d / %s(%d)",
                   HS_RCV_ASKED, ra.rc, hs_errname(ra.err), ra.err);
    int v = st_get_int(a, SRTO_RCVLATENCY);
    ST_REQUIRE_MSG(v == 0,
                   "SRTO_RCVLATENCY on a created-and-never-bound socket after "
                   "setting it to %d: expected 0 (the getter reports the value "
                   "in force, and nothing is in force yet), got %d",
                   HS_RCV_ASKED, v);
    srt_close(a);

    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(b != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    HsRc rb = hs_set_int(b, SRTO_PEERLATENCY, HS_PEER_BIGGER);
    ST_REQUIRE_MSG(rb.rc == 0,
                   "srt_setsockflag(SRTO_PEERLATENCY, %d) on an unconnected "
                   "socket: expected 0, got %d / %s(%d)",
                   HS_PEER_BIGGER, rb.rc, hs_errname(rb.err), rb.err);
    v = st_get_int(b, SRTO_PEERLATENCY);
    ST_REQUIRE_MSG(v == 0,
                   "SRTO_PEERLATENCY on a created-and-never-bound socket after "
                   "setting it to %d: expected 0, got %d", HS_PEER_BIGGER, v);
    srt_close(b);

    // Both on one socket, plus the combined alias, all still zero.
    SRTSOCKET c = st_new_socket();
    ST_REQUIRE_MSG(c != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    HsRc rc1 = hs_set_int(c, SRTO_RCVLATENCY, HS_RCV_ASKED);
    HsRc rc2 = hs_set_int(c, SRTO_PEERLATENCY, HS_PEER_BIGGER);
    ST_REQUIRE_MSG(rc1.rc == 0 && rc2.rc == 0,
                   "setting SRTO_RCVLATENCY=%d and SRTO_PEERLATENCY=%d on one "
                   "unconnected socket: expected 0 and 0, got %d / %s(%d) and "
                   "%d / %s(%d)",
                   HS_RCV_ASKED, HS_PEER_BIGGER,
                   rc1.rc, hs_errname(rc1.err), rc1.err,
                   rc2.rc, hs_errname(rc2.err), rc2.err);
    const int rl = st_get_int(c, SRTO_RCVLATENCY);
    const int pl = st_get_int(c, SRTO_PEERLATENCY);
    const int cl = st_get_int(c, SRTO_LATENCY);
    ST_REQUIRE_MSG(rl == 0 && pl == 0 && cl == 0,
                   "on a created-and-never-bound socket configured with "
                   "SRTO_RCVLATENCY=%d and SRTO_PEERLATENCY=%d: expected all "
                   "three getters to report 0, got SRTO_RCVLATENCY=%d "
                   "SRTO_PEERLATENCY=%d SRTO_LATENCY=%d",
                   HS_RCV_ASKED, HS_PEER_BIGGER, rl, pl, cl);
    srt_close(c);
}

// H2/H3, the described scenario: the listener asks for a receive latency, the
// caller proposes a LARGER peer latency, and the receiving side reads back the
// larger of the two rather than what it configured. The combined SRTO_LATENCY
// must report that same figure.
SRT_CASE(g02_latency, receiver_takes_the_larger_peer_proposal) {
    Link L;
    RelaySched sched;
    st_connect(L, sched,
               [](SRTSOCKET l) {
                   st_set_int(l, SRTO_TSBPDMODE, 1);
                   st_set_int(l, SRTO_RCVLATENCY, HS_RCV_ASKED);
               },
               [](SRTSOCKET c) {
                   st_set_int(c, SRTO_TSBPDMODE, 1);
                   st_set_int(c, SRTO_PEERLATENCY, HS_PEER_BIGGER);
               });

    const int acc_rcv = st_get_int(L.acc, SRTO_RCVLATENCY);
    ST_REQUIRE_MSG(acc_rcv == HS_PEER_BIGGER,
                   "SRTO_RCVLATENCY on the receiving (accepted) socket: it asked "
                   "for %d and the peer proposed %d, so the negotiated figure is "
                   "max(%d,%d)=%d; got %d%s",
                   HS_RCV_ASKED, HS_PEER_BIGGER, HS_RCV_ASKED, HS_PEER_BIGGER,
                   HS_PEER_BIGGER, acc_rcv,
                   acc_rcv == HS_RCV_ASKED
                       ? " -- that is the value it configured, not the negotiated one"
                       : "");

    const int acc_comb = st_get_int(L.acc, SRTO_LATENCY);
    ST_REQUIRE_MSG(acc_comb == acc_rcv,
                   "SRTO_LATENCY on the receiving (accepted) socket: expected the "
                   "same negotiated figure SRTO_RCVLATENCY reports, %d, got %d",
                   acc_rcv, acc_comb);

    // The sending side's own view of the pair: its proposal wins there too,
    // because it is the larger of (its proposal, the peer's request).
    const int cal_peer = st_get_int(L.cal, SRTO_PEERLATENCY);
    ST_REQUIRE_MSG(cal_peer == HS_PEER_BIGGER,
                   "SRTO_PEERLATENCY on the proposing (caller) socket: it "
                   "proposed %d and the peer asked %d, so the negotiated figure "
                   "is max(%d,%d)=%d; got %d",
                   HS_PEER_BIGGER, HS_RCV_ASKED, HS_PEER_BIGGER, HS_RCV_ASKED,
                   HS_PEER_BIGGER, cal_peer);
}

// The control the previous case needs. Same topology, the two numbers swapped:
// the receiver's own request is now the larger one, so it must read back its
// OWN value. Without this case a library that simply reports whatever the peer
// proposed -- ignoring its own request entirely -- would score the point.
SRT_CASE(g02_latency, receiver_keeps_its_own_larger_request) {
    Link L;
    RelaySched sched;
    st_connect(L, sched,
               [](SRTSOCKET l) {
                   st_set_int(l, SRTO_TSBPDMODE, 1);
                   st_set_int(l, SRTO_RCVLATENCY, HS_RCV_BIGGER);
               },
               [](SRTSOCKET c) {
                   st_set_int(c, SRTO_TSBPDMODE, 1);
                   st_set_int(c, SRTO_PEERLATENCY, HS_PEER_SMALL);
               });

    const int acc_rcv = st_get_int(L.acc, SRTO_RCVLATENCY);
    ST_REQUIRE_MSG(acc_rcv == HS_RCV_BIGGER,
                   "SRTO_RCVLATENCY on the receiving (accepted) socket: it asked "
                   "for %d and the peer proposed only %d, so the negotiated "
                   "figure is max(%d,%d)=%d; got %d%s",
                   HS_RCV_BIGGER, HS_PEER_SMALL, HS_RCV_BIGGER, HS_PEER_SMALL,
                   HS_RCV_BIGGER, acc_rcv,
                   acc_rcv == HS_PEER_SMALL
                       ? " -- that is the peer's proposal, so the larger of the "
                         "two was not taken"
                       : "");

    const int acc_comb = st_get_int(L.acc, SRTO_LATENCY);
    ST_REQUIRE_MSG(acc_comb == acc_rcv,
                   "SRTO_LATENCY on the receiving (accepted) socket: expected the "
                   "same negotiated figure SRTO_RCVLATENCY reports, %d, got %d",
                   acc_rcv, acc_comb);

    const int cal_peer = st_get_int(L.cal, SRTO_PEERLATENCY);
    ST_REQUIRE_MSG(cal_peer == HS_RCV_BIGGER,
                   "SRTO_PEERLATENCY on the proposing (caller) socket: it "
                   "proposed %d and the peer asked for %d, so the negotiated "
                   "figure is max(%d,%d)=%d; got %d",
                   HS_PEER_SMALL, HS_RCV_BIGGER, HS_PEER_SMALL, HS_RCV_BIGGER,
                   HS_RCV_BIGGER, cal_peer);
}

// H3: SRTO_LATENCY is the alias for the pair in BOTH directions. Neither side
// touches the split options here -- each sets only SRTO_LATENCY -- and the
// caller's figure is the larger one. The accepted socket's receive latency can
// then only have come from the caller's PROPOSAL field, which nothing but the
// combined set ever wrote: that is what proves a set of SRTO_LATENCY assigned
// both halves. The get half is proved by SRTO_LATENCY answering the negotiated
// figure rather than the value that socket configured.
SRT_CASE(g02_latency, combined_option_sets_both_and_reads_negotiated) {
    Link L;
    RelaySched sched;
    st_connect(L, sched,
               [](SRTSOCKET l) {
                   st_set_int(l, SRTO_TSBPDMODE, 1);
                   st_set_int(l, SRTO_LATENCY, HS_COMBINED_LO);
               },
               [](SRTSOCKET c) {
                   st_set_int(c, SRTO_TSBPDMODE, 1);
                   st_set_int(c, SRTO_LATENCY, HS_COMBINED_HI);
               });

    const int acc_rcv = st_get_int(L.acc, SRTO_RCVLATENCY);
    const int acc_comb = st_get_int(L.acc, SRTO_LATENCY);
    ST_REQUIRE_MSG(acc_rcv == HS_COMBINED_HI,
                   "SRTO_RCVLATENCY on the accepted socket: only SRTO_LATENCY was "
                   "set, %d here and %d on the caller, so the caller's combined "
                   "set must have carried a peer proposal of %d and the "
                   "negotiated figure is max(%d,%d)=%d; got %d%s",
                   HS_COMBINED_LO, HS_COMBINED_HI, HS_COMBINED_HI,
                   HS_COMBINED_LO, HS_COMBINED_HI, HS_COMBINED_HI, acc_rcv,
                   acc_rcv == HS_COMBINED_LO
                       ? " -- that is the value this socket configured, so either "
                         "the peer's combined set did not assign the proposal or "
                         "the negotiation did not happen"
                       : "");
    ST_REQUIRE_MSG(acc_comb == HS_COMBINED_HI,
                   "SRTO_LATENCY on the accepted socket: expected the negotiated "
                   "figure %d (the same one SRTO_RCVLATENCY reports), got %d%s",
                   HS_COMBINED_HI, acc_comb,
                   acc_comb == HS_COMBINED_LO
                       ? " -- the combined option reported the configured value "
                         "instead of the negotiated one"
                       : "");

    // And on the caller, whose own request was already the larger: the alias
    // must agree with SRTO_RCVLATENCY there too.
    const int cal_rcv = st_get_int(L.cal, SRTO_RCVLATENCY);
    const int cal_comb = st_get_int(L.cal, SRTO_LATENCY);
    ST_REQUIRE_MSG(cal_rcv == HS_COMBINED_HI,
                   "SRTO_RCVLATENCY on the caller: it set SRTO_LATENCY=%d and the "
                   "peer proposed %d, so the negotiated figure is max(%d,%d)=%d; "
                   "got %d",
                   HS_COMBINED_HI, HS_COMBINED_LO, HS_COMBINED_HI,
                   HS_COMBINED_LO, HS_COMBINED_HI, cal_rcv);
    ST_REQUIRE_MSG(cal_comb == cal_rcv,
                   "SRTO_LATENCY on the caller: expected it to answer exactly as "
                   "SRTO_RCVLATENCY does, %d, got %d", cal_rcv, cal_comb);
}

// =====================================================================
// g03_streamid
//
// scored.txt: "a stream id set on a caller before connect reads back on that
// socket with its byte length reported through the option length, reads back
// with the same bytes on the socket `srt_accept` returns, reads back empty on
// the listener that accepted it, and a value longer than the documented limit
// is refused rather than truncated".
// Rows: D8, H7.
// =====================================================================

static const char HS_SID[] = "hs02/stream:alpha-42";

// D8 + interface.md's optlen contract. The set takes exactly optlen bytes; the
// get writes the string with a terminating NUL, reports the length WITHOUT it,
// needs a buffer at least one byte longer than the string, and refuses a
// shorter one with SRT_EINVPARAM. The refusals have their positive controls in
// the same case: the len+1 buffer succeeds while the len buffer does not, so a
// getter that refuses everything fails here too.
SRT_CASE(g03_streamid, set_reads_back_with_its_byte_length) {
    const std::string sid(HS_SID);
    const int len = (int)sid.size();

    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // Unset: an empty string of length 0, not an error.
    HsSid empty = hs_get_sid(s, HS_SID_BUF);
    ST_REQUIRE_MSG(empty.rc != SRT_ERROR,
                   "SRTO_STREAMID on a fresh socket with a %d-byte buffer: "
                   "expected success and an empty value, got SRT_ERROR / %s(%d)",
                   HS_SID_BUF, hs_errname(empty.err), empty.err);
    ST_REQUIRE_MSG(empty.optlen == 0,
                   "SRTO_STREAMID on a fresh socket: expected optlen 0 for an "
                   "unset stream id, got %d (value \"%s\")",
                   empty.optlen, hs_show(empty.value).c_str());

    HsRc set = hs_set_sid(s, sid.data(), len);
    ST_REQUIRE_MSG(set.rc == 0,
                   "srt_setsockflag(SRTO_STREAMID, \"%s\", optlen=%d) on an "
                   "unconnected socket: expected 0, got %d / %s(%d)",
                   sid.c_str(), len, set.rc, hs_errname(set.err), set.err);

    HsSid big = hs_get_sid(s, HS_SID_BUF);
    ST_REQUIRE_MSG(big.rc != SRT_ERROR,
                   "SRTO_STREAMID with a %d-byte buffer after setting \"%s\": "
                   "expected success, got SRT_ERROR / %s(%d)",
                   HS_SID_BUF, sid.c_str(), hs_errname(big.err), big.err);
    ST_REQUIRE_MSG(big.optlen == len,
                   "SRTO_STREAMID optlen after setting the %d-byte id \"%s\": "
                   "expected %d, got %d", len, sid.c_str(), len, big.optlen);
    ST_REQUIRE_MSG(big.value == sid,
                   "SRTO_STREAMID value: expected \"%s\", got \"%s\"",
                   sid.c_str(), hs_show(big.value).c_str());
    ST_REQUIRE_MSG(big.nul_after,
                   "SRTO_STREAMID with a %d-byte buffer: expected a terminating "
                   "NUL at offset %d, because optlen (%d) reports the string "
                   "WITHOUT the terminator; no NUL was written there",
                   HS_SID_BUF, big.optlen, big.optlen);
    ST_REQUIRE_MSG(big.guard_ok,
                   "SRTO_STREAMID wrote past the %d bytes of buffer it was given",
                   HS_SID_BUF);

    // Exactly one byte more than the string: the smallest buffer that works.
    HsSid tight = hs_get_sid(s, len + 1);
    ST_REQUIRE_MSG(tight.rc != SRT_ERROR,
                   "SRTO_STREAMID with a buffer of exactly len+1 = %d bytes: "
                   "expected success, got SRT_ERROR / %s(%d)",
                   len + 1, hs_errname(tight.err), tight.err);
    ST_REQUIRE_MSG(tight.optlen == len && tight.value == sid,
                   "SRTO_STREAMID with a len+1 = %d byte buffer: expected optlen "
                   "%d and \"%s\", got optlen %d and \"%s\"",
                   len + 1, len, sid.c_str(), tight.optlen,
                   hs_show(tight.value).c_str());

    // Exactly the string's length: one byte short of what the NUL needs.
    HsSid shortbuf = hs_get_sid(s, len);
    ST_REQUIRE_MSG(shortbuf.rc == SRT_ERROR,
                   "SRTO_STREAMID with a buffer of exactly len = %d bytes: "
                   "expected SRT_ERROR (the buffer must be at least one byte "
                   "longer than the stored string), got rc %d with optlen %d",
                   len, shortbuf.rc, shortbuf.optlen);
    ST_REQUIRE_MSG(shortbuf.err == SRT_EINVPARAM,
                   "SRTO_STREAMID with a %d-byte buffer for a %d-byte id: "
                   "expected SRT_EINVPARAM(%d), got %s(%d)",
                   len, len, (int)SRT_EINVPARAM, hs_errname(shortbuf.err),
                   shortbuf.err);

    HsSid zerobuf = hs_get_sid(s, 0);
    ST_REQUIRE_MSG(zerobuf.rc == SRT_ERROR && zerobuf.err == SRT_EINVPARAM,
                   "SRTO_STREAMID with a zero-length buffer: expected SRT_ERROR / "
                   "SRT_EINVPARAM(%d), got rc %d / %s(%d)",
                   (int)SRT_EINVPARAM, zerobuf.rc, hs_errname(zerobuf.err),
                   zerobuf.err);

    srt_close(s);
}

// H7: the id rides the handshake. The caller sets it before connect, and the
// socket srt_accept returns reports the same bytes -- it was never set there.
// The case also pins the state binding interface.md gives the setter: once the
// socket is connected the set is refused with SRT_ECONNSOCK, on both ends.
SRT_CASE(g03_streamid, accepted_socket_carries_the_callers_id) {
    const std::string sid(HS_SID);
    const int len = (int)sid.size();

    Link L;
    RelaySched sched;
    st_connect(L, sched, st_noconf,
               [&sid](SRTSOCKET c) { st_set_str(c, SRTO_STREAMID, sid); });

    HsSid cal = hs_get_sid(L.cal, HS_SID_BUF);
    ST_REQUIRE_MSG(cal.rc != SRT_ERROR,
                   "SRTO_STREAMID on the connected caller: expected success, got "
                   "SRT_ERROR / %s(%d)", hs_errname(cal.err), cal.err);
    ST_REQUIRE_MSG(cal.optlen == len && cal.value == sid,
                   "SRTO_STREAMID on the caller that set it before connect: "
                   "expected optlen %d and \"%s\", got optlen %d and \"%s\"",
                   len, sid.c_str(), cal.optlen, hs_show(cal.value).c_str());

    HsSid acc = hs_get_sid(L.acc, HS_SID_BUF);
    ST_REQUIRE_MSG(acc.rc != SRT_ERROR,
                   "SRTO_STREAMID on the socket srt_accept returned: expected "
                   "success, got SRT_ERROR / %s(%d)", hs_errname(acc.err), acc.err);
    ST_REQUIRE_MSG(acc.optlen == len,
                   "SRTO_STREAMID optlen on the accepted socket: expected %d, the "
                   "byte length of the id the caller set, got %d",
                   len, acc.optlen);
    ST_REQUIRE_MSG(acc.value == sid,
                   "SRTO_STREAMID on the accepted socket: expected the caller's "
                   "\"%s\" to have been carried across the handshake, got \"%s\"",
                   sid.c_str(), hs_show(acc.value).c_str());

    // interface.md: the set is refused with SRT_ECONNSOCK once connected, so
    // the accepted socket's value cannot have been written locally.
    HsRc late_acc = hs_set_sid(L.acc, "late", 4);
    ST_REQUIRE_MSG(late_acc.rc == SRT_ERROR && late_acc.err == SRT_ECONNSOCK,
                   "srt_setsockflag(SRTO_STREAMID) on the CONNECTED accepted "
                   "socket: expected SRT_ERROR / SRT_ECONNSOCK(%d), got rc %d / "
                   "%s(%d)", (int)SRT_ECONNSOCK, late_acc.rc,
                   hs_errname(late_acc.err), late_acc.err);
    HsRc late_cal = hs_set_sid(L.cal, "late", 4);
    ST_REQUIRE_MSG(late_cal.rc == SRT_ERROR && late_cal.err == SRT_ECONNSOCK,
                   "srt_setsockflag(SRTO_STREAMID) on the CONNECTED caller: "
                   "expected SRT_ERROR / SRT_ECONNSOCK(%d), got rc %d / %s(%d)",
                   (int)SRT_ECONNSOCK, late_cal.rc, hs_errname(late_cal.err),
                   late_cal.err);

    // The value survived the refused sets.
    HsSid after = hs_get_sid(L.acc, HS_SID_BUF);
    ST_REQUIRE_MSG(after.rc != SRT_ERROR && after.value == sid,
                   "SRTO_STREAMID on the accepted socket after a refused set: "
                   "expected \"%s\" untouched, got \"%s\"",
                   sid.c_str(), hs_show(after.value).c_str());
}

// H7's other half: the id is transported to the accepted socket and is never
// stored on the listener. The accepted socket is read in the SAME case, so the
// empty listener is asserted against a non-empty accepted socket on the same
// connection -- a library that reports an empty stream id everywhere cannot
// pass this.
SRT_CASE(g03_streamid, listener_reads_empty_after_accepting_one) {
    const std::string sid(HS_SID);
    const int len = (int)sid.size();

    Link L;
    RelaySched sched;
    st_connect(L, sched, st_noconf,
               [&sid](SRTSOCKET c) { st_set_str(c, SRTO_STREAMID, sid); });

    HsSid acc = hs_get_sid(L.acc, HS_SID_BUF);
    ST_REQUIRE_MSG(acc.rc != SRT_ERROR && acc.optlen == len && acc.value == sid,
                   "control: SRTO_STREAMID on the accepted socket should carry "
                   "\"%s\" with optlen %d, got rc %d optlen %d \"%s\"",
                   sid.c_str(), len, acc.rc, acc.optlen,
                   hs_show(acc.value).c_str());

    HsSid lsn = hs_get_sid(L.lsn, HS_SID_BUF);
    ST_REQUIRE_MSG(lsn.rc != SRT_ERROR,
                   "SRTO_STREAMID on the listening socket: expected success and "
                   "an empty value, got SRT_ERROR / %s(%d)",
                   hs_errname(lsn.err), lsn.err);
    ST_REQUIRE_MSG(lsn.optlen == 0,
                   "SRTO_STREAMID on the listener that accepted a connection "
                   "carrying \"%s\": expected optlen 0 -- the id rides the "
                   "handshake to the accepted socket and is never stored on the "
                   "listener -- got optlen %d and \"%s\"",
                   sid.c_str(), lsn.optlen, hs_show(lsn.value).c_str());
    ST_REQUIRE_MSG(lsn.value.empty(),
                   "SRTO_STREAMID on the listener: expected an empty string, got "
                   "\"%s\"", hs_show(lsn.value).c_str());
}

// D8 / interface.md: the limit is 512 bytes, a set beyond it is refused with
// SRT_EINVPARAM, and refused means the stored value is untouched -- not
// truncated to 512. The 512-byte set that precedes the refusals is the positive
// control: a library that refuses every set fails on it.
SRT_CASE(g03_streamid, over_the_limit_is_refused_not_truncated) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    const std::string atlimit((size_t)HS_SID_MAX, 'x');
    HsRc ok = hs_set_sid(s, atlimit.data(), HS_SID_MAX);
    ST_REQUIRE_MSG(ok.rc == 0,
                   "srt_setsockflag(SRTO_STREAMID, optlen=%d): the documented "
                   "limit is %d characters, so exactly %d must be accepted; got "
                   "%d / %s(%d)",
                   HS_SID_MAX, HS_SID_MAX, HS_SID_MAX, ok.rc,
                   hs_errname(ok.err), ok.err);

    HsSid stored = hs_get_sid(s, HS_SID_BUF);
    ST_REQUIRE_MSG(stored.rc != SRT_ERROR && stored.optlen == HS_SID_MAX &&
                       stored.value == atlimit,
                   "SRTO_STREAMID after an accepted %d-byte set: expected optlen "
                   "%d and %d 'x' bytes, got rc %d optlen %d",
                   HS_SID_MAX, HS_SID_MAX, HS_SID_MAX, stored.rc, stored.optlen);

    // One byte over.
    const std::string over((size_t)HS_SID_MAX + 1, 'y');
    HsRc bad = hs_set_sid(s, over.data(), HS_SID_MAX + 1);
    ST_REQUIRE_MSG(bad.rc == SRT_ERROR,
                   "srt_setsockflag(SRTO_STREAMID, optlen=%d): expected "
                   "SRT_ERROR (-1) for a value past the %d-byte limit, got %d",
                   HS_SID_MAX + 1, HS_SID_MAX, bad.rc);
    ST_REQUIRE_MSG(bad.err == SRT_EINVPARAM,
                   "srt_setsockflag(SRTO_STREAMID, optlen=%d): expected "
                   "SRT_EINVPARAM(%d), got %s(%d)",
                   HS_SID_MAX + 1, (int)SRT_EINVPARAM, hs_errname(bad.err),
                   bad.err);

    HsSid after = hs_get_sid(s, HS_SID_BUF);
    ST_REQUIRE_MSG(after.rc != SRT_ERROR,
                   "SRTO_STREAMID after a refused over-limit set: expected "
                   "success, got SRT_ERROR / %s(%d)", hs_errname(after.err),
                   after.err);
    ST_REQUIRE_MSG(after.optlen == HS_SID_MAX,
                   "SRTO_STREAMID optlen after a refused %d-byte set: expected "
                   "the previous value's %d, got %d",
                   HS_SID_MAX + 1, HS_SID_MAX, after.optlen);
    ST_REQUIRE_MSG(after.value == atlimit,
                   "SRTO_STREAMID after a refused %d-byte set: expected the "
                   "previously stored %d 'x' bytes to be untouched, got a value "
                   "beginning \"%s\" -- the over-long value was stored or "
                   "truncated instead of being refused",
                   HS_SID_MAX + 1, HS_SID_MAX, hs_show(after.value).c_str());

    // Far over, to separate "off by one" from "no limit at all".
    const std::string way((size_t)HS_SID_MAX * 2, 'z');
    HsRc worse = hs_set_sid(s, way.data(), (int)way.size());
    ST_REQUIRE_MSG(worse.rc == SRT_ERROR && worse.err == SRT_EINVPARAM,
                   "srt_setsockflag(SRTO_STREAMID, optlen=%d): expected "
                   "SRT_ERROR / SRT_EINVPARAM(%d), got rc %d / %s(%d)",
                   (int)way.size(), (int)SRT_EINVPARAM, worse.rc,
                   hs_errname(worse.err), worse.err);

    HsSid last = hs_get_sid(s, HS_SID_BUF);
    ST_REQUIRE_MSG(last.rc != SRT_ERROR && last.optlen == HS_SID_MAX &&
                       last.value == atlimit,
                   "SRTO_STREAMID after a refused %d-byte set: expected the "
                   "stored %d 'x' bytes to be untouched, got rc %d optlen %d",
                   (int)way.size(), HS_SID_MAX, last.rc, last.optlen);

    srt_close(s);
}
