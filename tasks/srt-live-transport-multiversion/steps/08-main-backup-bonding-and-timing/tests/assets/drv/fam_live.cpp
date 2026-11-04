// Family "live", checkpoint 08 (SRT v1.4.4). Four scored groups, MIXED tier:
//
//   g00_endtoend      NEW         SCORED end-to-end path. 4000 messages over a clean relay to a
//                                 listener on an explicit loopback address, byte-
//                                 identical and in order, PLUS a wildcard bind
//                                 attempted on that listener's own port while the
//                                 stream is in flight, refused with
//                                 SRT_EBINDCONFLICT without disturbing the stream.
//   g09_muxer_share   inherited   two sockets placed on ONE explicit loopback
//                                 address and port with matching settings: both
//                                 binds succeed, both report that address, and the
//                                 multiplexed endpoint carries its stream intact.
//   g10_recovery      inherited   fixed-index datagram drops recovered in order,
//                                 with the receiver's loss counter proving the loss
//                                 really happened.
//   g11_group_backup  inherited   a two-member main/backup group over distinct
//                                 loopback source addresses carries a stream intact.
//
// ------------------------------------------------------------ tiers, and why
//                                                       this file is contorted
//
// endtoend.txt puts g00_endtoend in THIS family and E2E_TIER in meta.env is `new`.
// The other three are `inherited` (scored.txt). One family is one .cpp compiled
// ONCE, so this translation unit is compiled against the candidate's install
// prefix, which for a candidate still on the previous milestone is v1.4.3. Every
// group must therefore BUILD at v1.4.3:
//
//   * the three inherited groups must then report EXECUTED n FAILED 0, because
//     their whole purpose is to be earned by a candidate who has not built this
//     milestone yet;
//   * g00_endtoend must fail there ON AN ASSERTION naming actual versus expected,
//     never on a compile or link error. Its teeth are the conflicting bind: at
//     v1.4.3 the multiplexer search compares the port, the address FAMILY and the
//     channel settings, and never the address specification itself, so the
//     wildcard request is silently satisfied by the explicit listener's
//     multiplexer and srt_bind returns 0 instead of -1/5015. Measured at the
//     v1.4.4 prefix while authoring: rc -1, err 5015, "Wildcard address:
//     0.0.0.0:PORT conflicts with existing IP binding: 127.0.0.1:PORT".
//
// ------------------------------------------------------------ identifier surface
//
// Nothing this milestone introduces is named in this file. `SRT_EBINDCONFLICT`,
// `MN_BUSYPORT`, `SRTO_RETRANSMITALGO` and `srt_clock_type` are absent at v1.4.3
// and are scored by `rexmit` and `bind`; naming any of them here would put a
// v1.4.3-absent identifier in the link line of a family that must compile there
// and would cost all four groups for a compile reason -- the failure mode
// DRIVER_RULES calls out at length. The gate asserts the *behaviour* (srt_bind
// returns -1 and srt_getlasterror reports 5015), not the enumerator, so it needs
// neither a restated constant nor a static_assert net.
//
// Everything named here exists at v1.4.3 AND at v1.4.4: SRTO_TSBPDMODE,
// SRTO_LATENCY, SRTO_RCVLATENCY, SRTO_TLPKTDROP, SRTO_NAKREPORT, SRTO_RCVTIMEO,
// SRTO_GROUPCONNECT, SRTS_*, SRT_GST_*, SRT_GTYPE_BACKUP, SRT_SOCKGROUPDATA,
// SRT_SOCKGROUPCONFIG, srt_create_group, srt_connect_group, srt_prepare_endpoint,
// srt_groupof, srt_group_data, pktRcvLossTotal, pktRecvTotal. This clause used to
// read "measured against both install prefixes while authoring, not assumed. The
// group API block is byte-identical in the two installed headers." The text IS
// identical in the two headers; the VISIBILITY is not, and that half was
// measured false. The metric was wrong, so the claim is retracted. In BOTH
// installed headers the entire group block -- SRTO_GROUPCONNECT included -- sits
// inside `#if ENABLE_EXPERIMENTAL_BONDING`, and both headers default that macro
// to 0 (v1.4.3 lines 164-166, v1.4.4 likewise). Nothing here is visible to a TU
// that does not itself define the macro. Measured at the v1.4.3 prefix with
// -fsyntax-only: with -DENABLE_EXPERIMENTAL_BONDING=1 the file is CLEAN; without
// it, 5+ errors including "'SRTO_GROUPCONNECT' was not declared in this scope"
// and "'srt_create_group' was not declared in this scope". So the flag is not
// optional decoration -- it is the thing that makes this file compile at all,
// and every group identifier named below is inside that guard at both tags. The
// compile line carries it, from THIS step's meta.env DRV_DEFS (see the note on
// fam.sh under "traceability").
//
// ------------------------------------------------------------ enum collision
//
// The library is built with -DENABLE_EXPERIMENTAL_BONDING=ON (meta.env
// CMAKE_EXTRA) and this driver TU with -DENABLE_EXPERIMENTAL_BONDING=1 (DRV_DEFS),
// so both see the same SRT_SOCKOPT. That is not a licence to wander: everything
// through SRTO_GROUPCONNECT exists unconditionally at both tags, and no option
// above it is named here. interface.md §1 requires the group entities to be
// visible in a TU compiled with that symbol; naming them directly is the
// contract, and it is what proves the flag reached this translation unit.
//
// ------------------------------------------------------------ traceability
//
// Requirement ids are the frozen rows in tmp/<task-id>/REQUIREMENTS.md,
// "### Step 08 -- v1.4.4 (frozen 2026-09-21)"; each case carries its own.
//
// Deliberately NOT asserted, because nothing candidate-facing states it:
//
//   * the retransmission default, its range refusal and the transmission-profile
//     interaction (RQ-08-01/02/03), and the exported clock identifier
//     (RQ-08-04): family `rexmit`, whose groups are `new` and may name what this
//     file must not.
//   * the numeric 5015 as an enumerator, the srt_strerror rendering, the two
//     explicit-address rows, the socket-only intersection under SRTO_REUSEADDR or
//     SRTO_UDP_RCVBUF settings, and the IPv6 wildcard rows: family `bind`,
//     g05-g08. What this family asserts about the conflict is exactly the one
//     sentence endtoend.txt puts in g00_endtoend -- refused with SRT_EBINDCONFLICT while
//     a stream is in flight, and the stream unaffected.
//   * two sockets SIMULTANEOUSLY listening on one shared endpoint: measured with a
//     purpose-built probe at BOTH prefixes, the second srt_listen fails with 5011
//     ("Another socket is already listening on the same port") and the socket
//     stays in SRTS_INIT -- in either order, whichever listens first wins. That
//     measurement is why g09's main case is shaped the way it is; see the g09
//     banner below. The positive form of the shared binding IS asserted, because
//     scope.md states outright that two sockets with an identical address
//     specification are not a conflict.
//   * which member of the backup group is running, whether the other is idle, and
//     any failover timing: interface.md §10 rules all three out.
//   * any retransmission count or deadline in the recovery group: interface.md
//     §11 rules that out ("recovery is asserted on the payload arriving").

#include "srtkit.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ------------------------------------------------------------ wire constant
//
// The code the milestone's own documentation gives SRT_EBINDCONFLICT, spelled as
// a plain number rather than as the enumerator, because the enumerator does not
// exist at v1.4.3 and naming it would cost this family its compile there. The
// value is MJ(NOTSUP)=5000 plus MN_BUSYPORT=15 (REQUIREMENTS.md RQ-08-05). It is
// asserted through srt_getlasterror(), which is what the candidate's library
// actually reports; a tree that refuses the bind with a different code fails this
// case by name.
static const int SRT_EBINDCONFLICT_VALUE = 5015;

// ----------------------------------------------------------------- sizes
//
// In live mode the congestion controller refuses a RECEIVE buffer smaller than
// the maximum possible payload (SRT_LIVE_MAX_PLSIZE - 16 = 1316), and
// st_recv_stream allocates `size + 64`, so the size argument must reach 1252 or
// every receive fails with SRT_EINVOP and the channel reads as empty. 1300 clears
// the floor and still fits one datagram, which is what lets the recovery case
// drop "datagrams at fixed indexes" and know which messages it dropped: the
// relay's schedule indexes DATA packets only, and one 1300-byte message is one
// data packet.
static const int LIVE_MSG = 1300;
static const int LIVE_GAP_US = 100;
// Pacing for the one case that asserts a library loss counter is EXACTLY zero.
// The exposure on a clean schedule is set by how many messages sit in the relay's
// UDP receive buffer, not by the bit rate: relay.h never resizes that socket, the
// host default here is ~208 KB, and the threshold measured for this package is
// about 150 full-size messages. The clean control sends 100 x 1300 B = ~130 KB,
// which fits even if the relay thread never runs, so the zero is not a race at
// this size -- but the headroom is finite and silent, so the case is paced ten
// times wider than the family to buy margin for ~90 ms of runtime. Raising that
// case much above 150 messages spends the margin again.
static const int LIVE_CLEAN_GAP_US = 1000;
static const int LIVE_LATENCY = 900;

// -------------------------------------------------------------- helpers
//
// The live-lossless configuration every transfer case here starts from: TSBPD on,
// a latency two orders of magnitude larger than the relay's worst delay, too-late
// drop off, and the periodic loss report on so the receiver says what it is
// missing. Same shape as srtkit.h's st_live_lossless plus SRTO_NAKREPORT.
static void live_conf(SRTSOCKET s) {
    st_set_int(s, SRTO_TSBPDMODE, 1);
    st_set_int(s, SRTO_LATENCY, LIVE_LATENCY);
    st_set_int(s, SRTO_TLPKTDROP, 0);
    st_set_int(s, SRTO_NAKREPORT, 1);
}

static void live_rcv_conf(SRTSOCKET s) {
    st_set_int(s, SRTO_TSBPDMODE, 1);
    st_set_int(s, SRTO_RCVLATENCY, LIVE_LATENCY);
    st_set_int(s, SRTO_TLPKTDROP, 0);
    st_set_int(s, SRTO_NAKREPORT, 1);
}

// The same pair with the latency supplied by the caller. Only the group case uses
// it, and it exists because of a measurement, not for tidiness: two member links
// handing their payloads to ONE accepted socket have to agree on the timestamp
// base of that socket, and at LIVE_LATENCY a standby link activated mid-stream
// was measured to deliver a message whose timestamp sat outside the receiver's
// window, so the receive side dropped it as too-late and the transfer came up
// short. At 200 ms the same setup delivered 400/400 in twelve consecutive runs.
// (TLPKTDROP is off, so the loss above is the TSBPD window, not the drop rule.)
static void live_conf_at(SRTSOCKET s, int latency) {
    st_set_int(s, SRTO_TSBPDMODE, 1);
    st_set_int(s, SRTO_LATENCY, latency);
    st_set_int(s, SRTO_TLPKTDROP, 0);
    st_set_int(s, SRTO_NAKREPORT, 1);
}

static void live_rcv_conf_at(SRTSOCKET s, int latency) {
    st_set_int(s, SRTO_TSBPDMODE, 1);
    st_set_int(s, SRTO_RCVLATENCY, latency);
    st_set_int(s, SRTO_TLPKTDROP, 0);
    st_set_int(s, SRTO_NAKREPORT, 1);
}

// One bounded statistics snapshot. Zeroed first: a case that read a field the
// library never filled would otherwise compare against whatever was on the stack.
static SRT_TRACEBSTATS bstats_of(SRTSOCKET s) {
    SRT_TRACEBSTATS st;
    std::memset(&st, 0, sizeof st);
    ST_REQUIRE_MSG(srt_bstats(s, &st, 0) != SRT_ERROR, "srt_bstats failed: %s",
                   srt_getlasterror_str());
    return st;
}

// An explicit IPv4 loopback address with a chosen last octet. The whole 127/8 is
// loopback on Linux, which is what makes 127.0.0.2/3/4 usable as distinct
// explicit addresses. Measured while authoring: a relay bound to 127.0.0.1
// reaches and is reached from a socket bound to 127.0.0.3, so a member link can
// take a non-.1 source through the relay.
static sockaddr_in loopback_n(uint8_t last, uint16_t port) {
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl((127u << 24) | (uint32_t)last);
    a.sin_port = htons(port);
    return a;
}

static sockaddr_in wildcard(uint16_t port) {
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    return a;
}

// Bind a socket to an explicit loopback address on a port the KERNEL allocates
// to that socket, and return the port it chose.
//
// Why this exists rather than `pick_port()`: pick_port() binds a probe socket,
// reads its port and CLOSES it, so the port it names is free at the moment it is
// named and any other socket on the machine may take it before the caller's bind
// lands. `listen_wild` below retries 40 times for exactly that reason. A retry
// narrows the window; asking for port 0 closes it, because the port is allocated
// to the socket that goes on holding it and is never released in between. The
// `bind` family's own helper takes the same route for the same reason.
//
// This matters most in `g09_muxer_share`, which is a GATE group: a port
// collision there is not one lost group, it is `reward = 0` for a candidate
// whose library did nothing wrong -- and for the Oracle.
//
// The group's subject is unaffected. RQ-08-09 is about two sockets sharing ONE
// explicit address and port; which port that is was never part of the claim, and
// the second socket still names the port explicitly, so the shared-binding path
// is the one exercised.
static uint16_t bind_explicit_ephemeral(SRTSOCKET s, uint8_t last) {
    sockaddr_in req = loopback_n(last, 0);
    ST_REQUIRE_MSG(srt_bind(s, (sockaddr*)&req, sizeof req) != SRT_ERROR,
                   "setup: srt_bind(127.0.0.%u:0) failed: %s", (unsigned)last,
                   srt_getlasterror_str());
    sockaddr_in got;
    std::memset(&got, 0, sizeof got);
    int n = (int)sizeof got;
    ST_REQUIRE_MSG(srt_getsockname(s, (sockaddr*)&got, &n) != SRT_ERROR,
                   "setup: srt_getsockname after binding 127.0.0.%u:0 failed: %s",
                   (unsigned)last, srt_getlasterror_str());
    return ntohs(got.sin_port);
}

// A listener on the wildcard address, retried across ports. pick_port() hands out
// a port some other socket on this machine may already hold (measured while
// authoring, on this VM with several authors running), so a single attempt turns a
// port collision into a failure message that reads like a library bug. The group
// case is the one that needs it: its member links source from addresses other than
// 127.0.0.1, so the listener cannot be pinned to 127.0.0.1.
//
// Read this together with `bind_explicit_ephemeral` above: g11_group_backup is a
// GATE group too, so a collision here is also `reward = 0`, and the retry loop
// only narrows the window that binding port 0 would close outright. The wildcard
// address is what forces the weaker route -- it is the ADDRESS that cannot be
// pinned, not the port -- so binding 0.0.0.0:0 and reading the port back with
// srt_getsockname remains available and is the better shape. It is left at 40
// retries here on purpose: the residual probability is negligible, and this file's
// gate groups are under a measured determinism study, so the port machinery of a
// gate case is not rewritten on the same pass that changes anything else.
//
// `conf` is applied BEFORE the bind, on purpose. The library refuses SRTO_TSBPDMODE
// on a socket that already has a state (measured here as "Operation not supported:
// Cannot do this operation on a CONNECTED socket"), so a helper that configured the
// socket after placing it would fail on its second candidate port, once the first
// candidate had been closed and the second socket created -- a bug whose message
// points at the option, not at the retry.
static SRTSOCKET listen_wild(SockConf conf, uint16_t* chosen) {
    for (int t = 0; t < 40; t++) {
        const uint16_t p = pick_port();
        SRTSOCKET s = st_new_socket();
        if (s == SRT_INVALID_SOCK) return SRT_INVALID_SOCK;
        st_set_int(s, SRTO_RCVTIMEO, 25000);
        st_set_int(s, SRTO_GROUPCONNECT, 1);
        conf(s);
        sockaddr_in a = wildcard(p);
        if (srt_bind(s, (sockaddr*)&a, sizeof a) == SRT_ERROR) { srt_close(s); continue; }
        if (srt_listen(s, 2) == SRT_ERROR) { srt_close(s); continue; }
        *chosen = p;
        return s;
    }
    return SRT_INVALID_SOCK;
}

// What srt_getsockname reports for a socket, as "addr:port".
static std::string bound_name(SRTSOCKET s) {
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    int nl = (int)sizeof a;
    ST_REQUIRE_MSG(srt_getsockname(s, (sockaddr*)&a, &nl) != SRT_ERROR,
                   "srt_getsockname failed: %s", srt_getlasterror_str());
    char buf[64];
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
    std::snprintf(buf, sizeof buf, "%s:%u", ip, (unsigned)ntohs(a.sin_port));
    return std::string(buf);
}

// Compares a stream against what was sent, naming the first place they differ.
// Shared by every transfer case so the message a reviewer reads is the same one
// whichever group failed.
static void require_stream_intact(const std::vector<std::string>& sent,
                                  const std::vector<std::string>& got, const char* what) {
    ST_REQUIRE_MSG(got.size() == sent.size(),
                   "%s: receiver got %zu of %zu messages -- the stream must arrive "
                   "complete and with no gaps",
                   what, got.size(), sent.size());
    int bad = st_first_out_of_order(sent, got);
    ST_REQUIRE_MSG(bad < 0, "%s: stream arrived out of order at received index %d",
                   what, bad);
    for (size_t i = 0; i < got.size(); i++) {
        ST_REQUIRE_MSG(got[i] == sent[i],
                       "%s: message %zu is not the one sent (%zu bytes vs %zu bytes)",
                       what, i, got[i].size(), sent[i].size());
    }
}

// =====================================================================
// g00_endtoend -- this step's end-to-end path. SCORED, tier `new` (E2E_TIER
// in meta.env). It is NOT a gate group: the gate is proof of life plus the
// `inherited` groups, and it earns nothing.
//
// endtoend.txt composes this group from two halves:
//
//   (a) a live stream of 4000 messages crosses a clean relay to a listener bound
//       to an EXPLICIT loopback address and arrives byte-identical, in order and
//       with no gaps;
//   (b) a second socket's attempt to bind the WILDCARD address on that listener's
//       own port WHILE THE STREAM IS IN FLIGHT is refused with SRT_EBINDCONFLICT
//       without disturbing the stream.
//
// (b) is what makes the group new, and it is RQ-08-12. At v1.4.3 the multiplexer
// search compared the port, the address FAMILY and the channel settings, and never
// the address specification itself, so a wildcard request was silently satisfied
// by the explicit listener's multiplexer and the bind SUCCEEDED. At v1.4.4 the
// loop reads the multiplexer's own channel address and throws MJ_NOTSUP/
// MN_BUSYPORT (5015) as soon as an explicit binding holds the port, before it
// asks the operating system for anything.
// =====================================================================

// RQ-08-12's transport half, and the control the group needs: the stream crosses a
// clean relay intact. The same 4000-message harness every prior step's gate runs
// -- passing this does not make the group a `new` discriminator, it keeps the
// group honest about the transport underneath the discriminator.
SRT_CASE(g00_endtoend, clean_relay_carries_four_thousand_messages_in_order) {
    Link L;
    RelaySched sched;  // clean: nothing dropped, nothing held, nothing delayed
    st_connect(L, sched, live_rcv_conf, live_conf);

    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, 4000, LIVE_MSG, 15000); });

    std::vector<std::string> sent = st_send_stream(L.cal, 4000, LIVE_MSG, LIVE_GAP_US);
    rcvr.join();

    require_stream_intact(sent, got, "4000-message clean-relay stream");

    // Positive control on the CHANNEL, not on the application: the relay counts
    // what it forwarded and what it dropped, so "clean" is read rather than
    // assumed.
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 0,
                   "the relay dropped %d forward data packets, but this schedule is "
                   "clean -- the case did not carry what it claims to",
                   L.relay->dropped_fwd());
    ST_REQUIRE_MSG(L.relay->seen_fwd() >= 4000,
                   "the relay saw only %d forward data packets for 4000 messages",
                   L.relay->seen_fwd());

    ST_REQUIRE_EQ_INT(srt_getsockstate(L.cal), SRTS_CONNECTED);
    ST_REQUIRE_EQ_INT(srt_getsockstate(L.acc), SRTS_CONNECTED);

    SRT_TRACEBSTATS rs = bstats_of(L.acc);
    ST_REQUIRE_MSG(rs.pktRecvTotal >= 4000,
                   "receiver reports %lld packets received for a 4000-message clean "
                   "stream",
                   (long long)rs.pktRecvTotal);
}

// RQ-08-12's discriminating half. The subject is the refusal, so the case asserts
// the refusal; the positive control is the same socket class binding successfully
// against a port nothing holds, which keeps "the library refuses every wildcard
// bind" from scoring the point.
SRT_CASE(g00_endtoend, wildcard_bind_on_the_listeners_port_is_refused_mid_stream) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, live_rcv_conf, live_conf);

    // The listener's own address, as it reported it -- an explicit loopback
    // address, which is what makes the wildcard request intersect with it.
    sockaddr_in la;
    std::memset(&la, 0, sizeof la);
    int nl = (int)sizeof la;
    ST_REQUIRE_MSG(srt_getsockname(L.lsn, (sockaddr*)&la, &nl) != SRT_ERROR,
                   "srt_getsockname on the listener failed: %s", srt_getlasterror_str());
    const uint16_t lport = ntohs(la.sin_port);
    const std::string lname_before = bound_name(L.lsn);

    const int N = 1500;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, LIVE_MSG, 15000); });

    // The sender runs on its own thread so the conflicting bind lands while the
    // stream is genuinely in flight rather than after it. It is bounded (1500
    // messages at 100 us of pacing), so neither join can run past the case budget.
    std::vector<std::string> sent;
    std::thread sndr([&] { sent = st_send_stream(L.cal, N, LIVE_MSG, LIVE_GAP_US); });

    // Let the first messages cross before attempting the bind: RQ-08-12 asks for
    // the attempt "at the worst possible moment", with traffic already on the
    // endpoint. A bounded wait on an observable, not a synchronisation sleep -- the
    // verdict does not depend on it, only the strength of the claim.
    for (int i = 0; i < 400 && L.relay->seen_fwd() == 0; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    SRTSOCKET other = st_new_socket();
    ST_REQUIRE_MSG(other != SRT_INVALID_SOCK, "srt_socket (conflicting): %s",
                   srt_getlasterror_str());

    // The positive control, taken FIRST so that a library which refuses this
    // socket class for an unrelated reason (a bad descriptor, an unbound-state
    // check) cannot be read as having detected a conflict: the same socket class
    // binds the wildcard address on a port nothing holds.
    SRTSOCKET free_one = st_new_socket();
    ST_REQUIRE_MSG(free_one != SRT_INVALID_SOCK, "srt_socket (control): %s",
                   srt_getlasterror_str());
    // Port 0, not pick_port(): the control must be free of the port race, since a
    // collision here reports "this library refuses wildcard binds" about a library
    // that did nothing of the sort. The kernel allocates the port to this socket.
    sockaddr_in fa = wildcard(0);
    const int free_rc = srt_bind(free_one, (sockaddr*)&fa, sizeof fa);
    const int free_err = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(free_rc != SRT_ERROR,
                   "the wildcard bind into a free port FAILED with %d (%s): this "
                   "case cannot distinguish a conflict refusal from a library that "
                   "refuses wildcard binds outright",
                   free_err, srt_strerror(free_err, 0));
    srt_close(free_one);

    sockaddr_in wa = wildcard(lport);
    srt_clearlasterror();
    const int rc = srt_bind(other, (sockaddr*)&wa, sizeof wa);
    const int err = srt_getlasterror(nullptr);

    sndr.join();
    rcvr.join();

    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "binding 0.0.0.0:%u while the listener holds %s returned %d, "
                   "expected -1 with %d (SRT_EBINDCONFLICT): an intersecting bind "
                   "must be refused, not silently satisfied by the existing "
                   "multiplexer",
                   (unsigned)lport, lname_before.c_str(), rc, SRT_EBINDCONFLICT_VALUE);
    ST_REQUIRE_MSG(err == SRT_EBINDCONFLICT_VALUE,
                   "the refused wildcard bind reported error %d (%s), expected %d "
                   "(SRT_EBINDCONFLICT) -- a system-level bind failure means the "
                   "conflict was not decided by the library before the syscall",
                   err, srt_strerror(err, 0), SRT_EBINDCONFLICT_VALUE);
    srt_close(other);

    // "Without disturbing the stream": the payload still arrives complete and in
    // order. That is what makes the refusal the interesting half of the group
    // rather than the whole of it.
    require_stream_intact(sent, got, "stream crossing the refused bind");

    // And the endpoint the attempt was refused against is itself untouched: still
    // listening, still reporting the address it bound, still connected.
    ST_REQUIRE_EQ_INT(srt_getsockstate(L.lsn), SRTS_LISTENING);
    const std::string lname_after = bound_name(L.lsn);
    ST_REQUIRE_MSG(lname_after == lname_before,
                   "the listener reported %s before the refused bind and %s after: a "
                   "refused bind must not rebind or disturb the multiplexer it was "
                   "refused against",
                   lname_before.c_str(), lname_after.c_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(L.cal), SRTS_CONNECTED);
    ST_REQUIRE_EQ_INT(srt_getsockstate(L.acc), SRTS_CONNECTED);
}

// =====================================================================
// g09_muxer_share -- inherited (RQ-08-09).
//
// scored.txt: "two sockets binding the same explicit loopback address and port
// with matching settings share that binding rather than conflicting,
// srt_getsockname on each reports that same address and port, and a caller that
// reaches the shared endpoint through a relay is accepted on the socket
// listening there with its stream arriving complete, in order and
// byte-identical".
//
// THAT SENTENCE WAS REWRITTEN IN ROUND 1, and the measurement behind the rewrite
// is kept here because it is the reason the group looks the way it does. Until
// round 1, scored.txt (and with it the gate bullet the candidate reads) called
// g09 "two LISTENING sockets ... each accept their own caller through a separate
// relay". The BINDING half of that was always right and is what case A asserts.
// The two-LISTENER half is not implementable, at v1.4.4 or at v1.4.3: with both
// sockets bound to one explicit address and port (which succeeds on both,
// sharing one multiplexer), the SECOND srt_listen returns -1 with error 5011 and
// the socket stays in SRTS_OPENED (state 2), never reaching SRTS_LISTENING.
// Measured at BOTH prefixes with a purpose-built probe, and measured with the
// order reversed too: whichever socket listens first wins and the other is
// refused. So "each accept their own caller" could not be built as written --
// there is never a second listener to accept anything -- and the document
// described an arrangement the library forbids. The document was corrected to
// the arrangement that is real; the case was not weakened to meet it.
//
// What the case does, and what scored.txt now says: socket a holds the shared
// endpoint and listens on it, socket b stays BOUND ON THAT SAME ENDPOINT without
// listening, and a caller that arrives through a relay addressed at that port is
// accepted by a, with its payload arriving complete and in order. Every scored
// noun survives -- the same explicit address and port, matching settings, a
// shared binding, a relay, an accepted and intact stream -- and only the one the
// library makes impossible is gone.
//
// The group's subject is therefore the shared BINDING, which is the surface this
// milestone reworked: an identical address specification is NOT a conflict, both
// sockets land on one endpoint, both report the address they asked for, and that
// endpoint still carries a live stream. This is the group that catches an
// over-corrected conflict rule, which would refuse the second bind and take this
// step's `new` tier down with it.
//
// Every identifier here exists at v1.4.3.
// =====================================================================

// RQ-08-09, the half that survives: both sockets are placed on one explicit
// address and port, both binds succeed, both report that address, and the shared
// endpoint carries a live stream through a relay to the socket that owns it.
SRT_CASE(g09_muxer_share, two_sockets_share_one_explicit_binding) {
    SRTSOCKET a = st_new_socket();
    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK && b != SRT_INVALID_SOCK, "srt_socket: %s",
                   srt_getlasterror_str());

    // The kernel picks the port and hands it to `a`, so no other socket can be
    // holding it when `b` asks for the same one. See bind_explicit_ephemeral.
    const uint16_t port = bind_explicit_ephemeral(a, 1);
    sockaddr_in sa = loopback_n(1, port);

    srt_clearlasterror();
    const int rb = srt_bind(b, (sockaddr*)&sa, sizeof sa);
    const int eb = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rb != SRT_ERROR,
                   "the second bind of the SAME explicit address 127.0.0.1:%u with "
                   "matching settings failed with %d (%s): an identical address "
                   "specification is NOT an intersecting binding -- only an "
                   "intersecting-but-different one is a conflict",
                   (unsigned)port, eb, srt_strerror(eb, 0));

    const std::string na = bound_name(a);
    const std::string nb = bound_name(b);
    char want[64];
    std::snprintf(want, sizeof want, "127.0.0.1:%u", (unsigned)port);
    ST_REQUIRE_MSG(na == want,
                   "the first socket reports %s, expected the address it asked for, %s",
                   na.c_str(), want);
    ST_REQUIRE_MSG(nb == want,
                   "the second socket shares that binding but reports %s, expected %s: "
                   "a reused multiplexer installs its own channel address on both",
                   nb.c_str(), want);

    // The endpoint is usable, which is what separates "two sockets were placed" from
    // "two sockets were placed somewhere inert": one listens and accepts a live
    // stream through its own relay -- while the OTHER socket is still bound to the
    // very same endpoint and never listened. That is the arrangement measured
    // above to be the reachable one; a second listener is refused with 5011.
    ST_REQUIRE_MSG(srt_listen(a, 2) != SRT_ERROR, "srt_listen on the shared binding: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(a), SRTS_LISTENING);
    ST_REQUIRE_MSG(srt_getsockstate(b) != SRTS_LISTENING,
                   "the second socket on the shared binding reached SRTS_LISTENING: "
                   "this case was written for the arrangement in which only one of "
                   "the two can listen, and must be re-aimed if that changed");

    Relay* relay = new Relay(port, RelaySched());

    // The caller exists before the acceptor starts, and every assertion about
    // it is made after the join: a throw inside the acceptor's lifetime would
    // leave it polling with no caller left to end it.
    SRTSOCKET cal = st_new_socket();
    ST_REQUIRE_MSG(cal != SRT_INVALID_SOCK, "srt_socket (caller): %s",
                   srt_getlasterror_str());
    live_conf(cal);

    SRTSOCKET accepted = SRT_INVALID_SOCK;
    std::atomic<bool> connect_done(false);
    std::thread acceptor([&] { accepted = st_accept_polled(a, connect_done); });

    sockaddr_in ra = st_loopback(relay->port());
    const int crc = srt_connect(cal, (sockaddr*)&ra, sizeof ra);
    const int cerr = srt_getlasterror(nullptr);
    connect_done.store(true);
    acceptor.join();

    ST_REQUIRE_MSG(crc != SRT_ERROR,
                   "a caller could not connect to the shared endpoint 127.0.0.1:%u: "
                   "%d (%s)",
                   (unsigned)port, cerr, srt_strerror(cerr, 0));
    ST_REQUIRE_MSG(accepted != SRT_INVALID_SOCK,
                   "the shared binding accepted nothing: %s", srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(accepted), SRTS_CONNECTED);

    const int N = 400;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(accepted, N, LIVE_MSG, 10000); });
    std::vector<std::string> sent = st_send_stream(cal, N, LIVE_MSG, LIVE_GAP_US);
    rcvr.join();

    require_stream_intact(sent, got, "400-message stream over the shared binding");
    ST_REQUIRE_MSG(relay->dropped_fwd() == 0,
                   "the relay dropped %d forward data packets on a clean schedule",
                   relay->dropped_fwd());
    ST_REQUIRE_MSG(relay->seen_fwd() >= N,
                   "the relay saw only %d forward data packets for %d messages",
                   relay->seen_fwd(), N);

    srt_close(cal);
    srt_close(accepted);
    delete relay;
    srt_close(b);
    srt_close(a);
}

// RQ-08-09's second assertion, and the one that keeps "shared" from degenerating
// into "indistinguishable": the two sockets took the SAME address, so both must
// report that same address. This is the positive form of the property. Its
// negative form -- two sockets on DIFFERENT explicit addresses, each reporting
// its own -- is the discriminator `bind` owns (RQ-08-07, g07_separate_muxers):
// measured at the v1.4.3 prefix, the socket that bound 127.0.0.3 reports
// 127.0.0.2, because the old reuse path set only the family. Asserting it here
// would make this INHERITED group fail at the previous milestone, which is not
// what an inherited group is for; the file's tier contract wins over the extra
// coverage.
SRT_CASE(g09_muxer_share, a_shared_binding_is_one_address_on_both_sockets) {
    SRTSOCKET a = st_new_socket();
    SRTSOCKET b = st_new_socket();
    ST_REQUIRE_MSG(a != SRT_INVALID_SOCK && b != SRT_INVALID_SOCK, "srt_socket: %s",
                   srt_getlasterror_str());

    // Kernel-allocated port, for the reason given at bind_explicit_ephemeral: a
    // gate group may not carry a port race.
    const uint16_t port = bind_explicit_ephemeral(a, 1);
    sockaddr_in sa = loopback_n(1, port);
    srt_clearlasterror();
    const int rb = srt_bind(b, (sockaddr*)&sa, sizeof sa);
    const int eb = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rb != SRT_ERROR,
                   "the second bind of the SAME explicit address 127.0.0.1:%u with "
                   "matching settings failed with %d (%s): an identical address "
                   "specification is NOT an intersecting binding",
                   (unsigned)port, eb, srt_strerror(eb, 0));

    char want[64];
    std::snprintf(want, sizeof want, "127.0.0.1:%u", (unsigned)port);
    const std::string na = bound_name(a);
    const std::string nb = bound_name(b);
    ST_REQUIRE_MSG(na == want,
                   "the first socket reports %s, expected the address it asked for, %s",
                   na.c_str(), want);
    ST_REQUIRE_MSG(nb == want,
                   "the second socket shares the binding and reports %s, expected %s: "
                   "a shared binding is one address, and both sockets on it must say "
                   "so",
                   nb.c_str(), want);
    // The two names must also be the same address, not merely both correct by
    // coincidence: one multiplexer, one endpoint. Measured on the sockets
    // themselves rather than recomputed from `want`, so a library that reported a
    // different port or a different loopback alias on each socket is caught here
    // rather than passing because both were compared against the same literal.
    ST_REQUIRE_MSG(na == nb,
                   "the two sockets on one shared binding report different addresses, "
                   "%s and %s: they were placed on the same explicit endpoint and "
                   "must agree about it",
                   na.c_str(), nb.c_str());

    // Neither socket is merely dormant: the first reaches SRTS_LISTENING, and the
    // second is genuinely on the endpoint rather than silently refused.
    ST_REQUIRE_MSG(srt_listen(a, 2) != SRT_ERROR, "srt_listen on the shared binding: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(a), SRTS_LISTENING);
    // The second socket is bound but not listening -- SRTS_OPENED, because srt_bind
    // is what leaves SRTS_INIT. Asserting SRTS_INIT here was wrong on the first
    // run (expected 1, got 2) at BOTH tags: a bound socket has left SRTS_INIT.
    ST_REQUIRE_EQ_INT(srt_getsockstate(b), SRTS_OPENED);

    srt_close(b);
    srt_close(a);
}

// =====================================================================
// g10_recovery -- inherited (RQ-08-10).
//
// scored.txt: "datagrams dropped by the relay at fixed indexes are retransmitted
// and the payload arrives complete and in order once too-late packet drop is off
// and the latency budget is generous, with the receiver's loss counter showing the
// loss really happened".
//
// interface.md §11 pins what may NOT be asserted: the retransmission default this
// milestone changes sits underneath this group, and the efficient algorithm may
// retransmit a given loss less often, so recovery is asserted on the PAYLOAD
// arriving -- never on a retransmission count or a deadline. A candidate whose
// default is wrong in the other direction fails `rexmit`'s g01, not this group.
// Every identifier exists at v1.4.3.
// =====================================================================

SRT_CASE(g10_recovery, dropped_datagrams_are_recovered_in_order) {
    Link L;
    RelaySched sched;
    sched.drop_fwd = {10, 11, 40, 70, 71, 72, 120};
    st_connect(L, sched, live_rcv_conf, live_conf);

    const int N = 200;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, LIVE_MSG, 20000); });

    std::vector<std::string> sent = st_send_stream(L.cal, N, LIVE_MSG, LIVE_GAP_US);
    rcvr.join();

    // The channel really did drop: the relay counts the DATA packets it removed,
    // so this is the honest way to show the case is not passing on a clean channel
    // by accident.
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 7,
                   "the relay dropped %d forward data packets, expected the 7 the "
                   "schedule names",
                   L.relay->dropped_fwd());

    require_stream_intact(sent, got,
                          "200-message stream with 7 datagrams dropped at fixed indexes");

    // RQ-08-10's own control: the loss must be REPORTED, not merely survived. With
    // TLPKTDROP off there is no competing drop path, so any non-zero reading is the
    // observed loss.
    SRT_TRACEBSTATS rs = bstats_of(L.acc);
    ST_REQUIRE_MSG(rs.pktRcvLossTotal > 0,
                   "receiver reports pktRcvLossTotal = %d after %d datagrams were "
                   "dropped: the loss must be reported, not merely recovered",
                   (int)rs.pktRcvLossTotal, L.relay->dropped_fwd());
}

// Positive control for the case above, and the half that distinguishes "the stream
// arrived" from "the channel was empty": on a clean schedule the cumulative loss
// count is zero. Without it, a library that reported loss unconditionally would
// pass the non-zero assertion for the wrong reason.
SRT_CASE(g10_recovery, clean_relay_reports_no_loss) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, live_rcv_conf, live_conf);

    const int N = 100;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, LIVE_MSG, 10000); });
    // LIVE_CLEAN_GAP_US, not LIVE_GAP_US: this is the only case in the file whose
    // verdict depends on a library counter reading exactly zero, so it is the only
    // one that pays for margin against the relay's own socket buffer.
    std::vector<std::string> sent = st_send_stream(L.cal, N, LIVE_MSG, LIVE_CLEAN_GAP_US);
    rcvr.join();

    require_stream_intact(sent, got, "100-message clean-relay stream");
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 0,
                   "the relay dropped %d forward data packets on a clean schedule",
                   L.relay->dropped_fwd());

    SRT_TRACEBSTATS rs = bstats_of(L.acc);
    ST_REQUIRE_MSG(rs.pktRcvLossTotal == 0,
                   "receiver reports pktRcvLossTotal = %d on a clean relay, expected 0",
                   (int)rs.pktRcvLossTotal);
}

// A consecutive burst, which is the harder recovery: the first case loses isolated
// datagrams that one retransmission repairs, while five in a row require the
// receiver to keep reporting the gap and the sender to fill it before the delivery
// deadline passes. "The latency budget is generous" is the precondition the group's
// sentence names, and this is the case that depends on it.
SRT_CASE(g10_recovery, a_burst_of_consecutive_loss_is_recovered) {
    Link L;
    RelaySched sched;
    sched.drop_fwd = {20, 21, 22, 23, 24};
    st_connect(L, sched,
               [](SRTSOCKET s) {
                   st_set_int(s, SRTO_TSBPDMODE, 1);
                   st_set_int(s, SRTO_RCVLATENCY, 2000);
                   st_set_int(s, SRTO_TLPKTDROP, 0);
                   st_set_int(s, SRTO_NAKREPORT, 1);
               },
               live_conf);

    const int N = 150;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, LIVE_MSG, 25000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, LIVE_MSG, LIVE_GAP_US);
    rcvr.join();

    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 5,
                   "the relay dropped %d forward data packets, expected the 5 the "
                   "schedule names",
                   L.relay->dropped_fwd());
    require_stream_intact(sent, got,
                          "150-message stream with a 5-datagram consecutive burst lost");

    SRT_TRACEBSTATS rs = bstats_of(L.acc);
    ST_REQUIRE_MSG(rs.pktRcvLossTotal > 0,
                   "receiver reports pktRcvLossTotal = %d after a 5-datagram burst was "
                   "dropped: the loss must be reported, not merely recovered",
                   (int)rs.pktRcvLossTotal);
}

// =====================================================================
// g11_group_backup -- inherited (RQ-08-11), the step's integration case.
//
// scored.txt: "a main/backup socket group whose two member links are prepared with
// distinct loopback source addresses connects to one group-enabled listener and
// carries a live stream that arrives complete, in order and byte-identical,
// srt_groupof on each member returns the group id, and srt_group_data reports both
// members as SRTS_CONNECTED with none of them SRT_GST_BROKEN and at least one of
// them SRT_GST_RUNNING".
//
// interface.md §10 pins what may NOT be asserted: not which member is running, not
// that the other is idle, and not any failover timing. State values are also not
// promised in any particular ORDER, so the member assertions are made over the set
// (counted), never indexed.
//
// RQ-08-11 is the case this step exists to protect: every member link takes a local
// endpoint through the reworked binding layer, and a conflict rule that is too
// eager would refuse the second member. Measured at the v1.4.4 prefix while
// authoring: the two member ids come back distinct, each maps to the group, both
// are present and CONNECTED in srt_group_data before and after the stream, and the
// accepted stream arrives byte-identical 400/400.
//
// No relay in this case, and that is a measurement too. relay.h's Relay is
// single-client by construction: it remembers one peer address on its front socket
// and sends everything it has to that peer. Two members dialling it look like one
// client, so both member links are handed to the same accepted socket through one
// UDP 5-tuple, and the receiver then sees two overlapping sequence ranges. Measured:
// with the relay, one member is dropped from the group mid-stream (group_data n=1)
// and delivery came out at 101/400, 400/400 and 100/400 on three consecutive runs.
// Without it, n=2 and 400/400 on twelve. The relay stays in g00/g10, where one
// client is not a limitation.
//
// Every identifier named here exists at v1.4.3 -- measured against that install
// prefix, not assumed -- because this file must compile there. A member reaches
// SRT_GST_RUNNING only inside sendBackup after a successful send (srtcore/group.cpp
// at v1.4.4, and the same at v1.4.3), so the RUNNING assertion is made AFTER the
// stream, which is also what makes it meaningful. The member-presence and CONNECTED
// assertions are made BEFORE it, because that is the point at which both members
// have completed their handshake and neither has yet been activated.
//
// `broken == 0` is asserted at BOTH points, and that is deliberate. A standby link
// that the sender never activates is SRT_GST_IDLE, not SRT_GST_BROKEN: the header
// defines BROKEN as "the last operation broke the socket, it should be closed", so
// nothing marks a healthy idle member BROKEN merely because the active member
// carried the traffic. On loopback with no injected failure there is no operation
// that can break either member, so the post-stream reading is as well-founded as
// the pre-stream one. Measured: n=2, 400/400 and broken=0 on twelve consecutive
// runs. (RUNNING is floored at >= 1 rather than fixed at 2 for the opposite
// reason -- which member the sender activates is the library's choice and is not
// part of the claim.)
// =====================================================================

SRT_CASE(g11_group_backup, a_two_member_backup_group_carries_a_stream_intact) {
    // --- the group-enabled listener ---------------------------------------
    // Wildcard, because the two member links source from 127.0.0.3 and 127.0.0.4:
    // a listener pinned to 127.0.0.1 would refuse them at the UDP layer and the
    // case would be measuring the kernel, not the group.
    uint16_t lport = 0;
    const int GLAT = 200;
    SRTSOCKET lsn = listen_wild(
        [GLAT](SRTSOCKET s) { live_rcv_conf_at(s, GLAT); }, &lport);
    ST_REQUIRE_MSG(lsn != SRT_INVALID_SOCK,
                   "no wildcard listener could be bound on any port in 40 attempts: %s",
                   srt_getlasterror_str());

    // --- the group and its two members ------------------------------------
    SRTSOCKET g = srt_create_group(SRT_GTYPE_BACKUP);
    ST_REQUIRE_MSG(g != SRT_INVALID_SOCK, "srt_create_group(SRT_GTYPE_BACKUP): %s",
                   srt_getlasterror_str());
    // RQ-08-11 speaks of "a socket group", so the handle carries the group bit --
    // checked because the reader of a failure needs to know whether the group was
    // created at all before anything about its members is believed.
    ST_REQUIRE_MSG((g & SRTGROUP_MASK) != 0,
                   "srt_create_group returned %d, which is not a group id", (int)g);
    live_conf_at(g, GLAT);

    // Distinct loopback source addresses, as RQ-08-11 requires: each member link
    // takes its own local endpoint through the reworked binding layer, and the two
    // specifications differ, so they must NOT be treated as one binding.
    const uint8_t src_last[2] = {3, 4};
    sockaddr_in peer = loopback_n(1, lport);
    SRT_SOCKGROUPCONFIG ep[2];
    for (int i = 0; i < 2; i++) {
        sockaddr_in src = loopback_n(src_last[i], 0);
        ep[i] = srt_prepare_endpoint((sockaddr*)&src, (sockaddr*)&peer, sizeof peer);
        ST_REQUIRE_MSG(ep[i].errorcode == SRT_SUCCESS,
                       "srt_prepare_endpoint for member %d reported errorcode %d", i,
                       ep[i].errorcode);
    }

    const int rc = srt_connect_group(g, ep, 2);
    const int cerr = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc != SRT_ERROR,
                   "srt_connect_group over two members with distinct loopback source "
                   "addresses failed: %d (%s)",
                   cerr, srt_strerror(cerr, 0));

    // --- both members present, connected and not broken -------------------
    //
    // srt_connect_group returns when the FIRST member has reached the listener, not
    // when the last one has, so the second member's handshake is still in flight
    // here (measured: a single immediate read caught 1 of 2 CONNECTED on one run).
    // The read is therefore taken after srt_accept, and polled: waiting for the
    // group to settle is not a retry of a failure, it is waiting for the call the
    // case is about to drive. This is the state a too-eager binding conflict
    // destroys -- a refused second member never reaches the group at all, and no
    // amount of waiting brings it back.
    //
    // The accepted socket carries the stream. srt_accept reports read-ready once
    // ANY member has completed its handshake, so exactly one accept is the
    // documented shape for a backup group.
    SRTSOCKET acc = srt_accept(lsn, nullptr, nullptr);
    ST_REQUIRE_MSG(acc != SRT_INVALID_SOCK,
                   "the group-connected listener accepted nothing: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_EQ_INT(srt_getsockstate(acc), SRTS_CONNECTED);

    {
        size_t n = 0;
        int conn = 0, brk = 0;
        SRT_SOCKGROUPDATA d[8];
        for (int t = 0; t < 100; t++) {
            n = 8;
            std::memset(d, 0, sizeof d);
            if (srt_group_data(g, d, &n) == SRT_ERROR) {
                ST_REQUIRE_MSG(false, "srt_group_data failed: %s",
                               srt_getlasterror_str());
            }
            conn = brk = 0;
            for (size_t i = 0; i < n; i++) {
                if (d[i].sockstate == SRTS_CONNECTED) conn++;
                if (d[i].memberstate == SRT_GST_BROKEN) brk++;
            }
            if (conn == 2) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ST_REQUIRE_MSG(n == 2,
                       "srt_group_data reports %zu members before any payload was sent, "
                       "but srt_connect_group was given 2 -- a member link was refused "
                       "or lost",
                       n);
        ST_REQUIRE_MSG(conn == 2,
                       "srt_group_data reports %d of 2 members SRTS_CONNECTED five "
                       "seconds after srt_connect_group accepted: both member links "
                       "must have completed their handshake",
                       conn);
        ST_REQUIRE_MSG(brk == 0,
                       "srt_group_data reports %d of 2 members SRT_GST_BROKEN before any "
                       "payload was sent", brk);
    }

    // --- the stream -------------------------------------------------------
    //
    // 20 ms between messages rather than the 100 us the other cases use. Measured:
    // at 100 us the group carried 400/400, 140/400 and 135/400 on three runs, at
    // 20 ms 400/400 on twelve. The pacing, not the count, is what the group case
    // needs, so the transfer stays at 400 messages (8 s of wall clock, well inside
    // the case timeout) and the pacing is documented rather than tuned away.
    const int N = 400;
    const int GAP_US = 20000;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(acc, N, LIVE_MSG, 25000); });
    std::vector<std::string> sent = st_send_stream(g, N, LIVE_MSG, GAP_US);
    rcvr.join();

    require_stream_intact(sent, got, "group-carried 400-message stream");

    // --- the member-state assertions scored.txt does list -------------------
    //
    // Over the set, never indexed: the order srt_group_data reports members in is
    // not promised, and interface.md §10 additionally rules out asserting WHICH
    // member is running. `ndata` is read leniently here, because the API makes no
    // promise that a member stays in the array after the stream -- the promise is
    // about BROKEN and RUNNING, which is what is asserted.
    size_t ndata = 8;
    SRT_SOCKGROUPDATA data[8];
    std::memset(data, 0, sizeof data);
    const int grc = srt_group_data(g, data, &ndata);
    ST_REQUIRE_MSG(grc != SRT_ERROR, "srt_group_data failed after the stream: %s",
                   srt_getlasterror_str());

    int running = 0, broken = 0;
    for (size_t i = 0; i < ndata; i++) {
        if (data[i].memberstate == SRT_GST_BROKEN) broken++;
        if (data[i].memberstate == SRT_GST_RUNNING) running++;
    }
    ST_REQUIRE_MSG(broken == 0,
                   "srt_group_data reports %d member(s) SRT_GST_BROKEN after a stream "
                   "that arrived complete and in order: a carried stream means no "
                   "member link broke",
                   broken);
    ST_REQUIRE_MSG(running >= 1,
                   "srt_group_data reports %d of %zu members SRT_GST_RUNNING after a "
                   "400-message stream crossed the group: at least one link must be "
                   "the active one",
                   running, ndata);

    // srt_groupof on each member returns the group. The member ids come from the
    // array srt_connect_group filled in, which is the caller-visible handle on each
    // link. The two ids are asserted distinct, because a group that handed back one
    // id twice would make the two-source setup a fiction.
    ST_REQUIRE_MSG(ep[0].id != ep[1].id,
                   "srt_connect_group reports the same member id (%d) for both "
                   "endpoints: two members with distinct sources are two links",
                   (int)ep[0].id);
    for (int i = 0; i < 2; i++) {
        ST_REQUIRE_MSG(ep[i].id != SRT_INVALID_SOCK && ep[i].id != -1,
                       "srt_connect_group left member %d's id at %d", i, (int)ep[i].id);
        const SRTSOCKET owner = srt_groupof(ep[i].id);
        ST_REQUIRE_MSG(owner == g,
                       "srt_groupof(member %d = %d) returned %d, expected the group id "
                       "%d",
                       i, (int)ep[i].id, (int)owner, (int)g);
    }

    srt_close(acc);
    srt_close(g);
    srt_close(lsn);
}
