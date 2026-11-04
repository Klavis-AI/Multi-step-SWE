// Step 10 (v1.5.2) -- `live` family.
//
// Mixed tier. `g07_onepacket`, `g08_rcvloss_bytes`, `g09_undecrypted` are new
// at this milestone and are SCORED; `g10_transfer`, `g11_recovery` and
// `g12_encryption` are inherited regression guards, and `g00_endtoend` is
// inherited too (meta.env E2E_TIER=inherited), because the plain live path it
// describes already passed at v1.5.0 -- measured green at the no-op prefix by
// dev/gate-noop-run.sh. `inherited` is the TIER keyword; the KIND it produces
// in assets/groups.txt is `gate`, and assets/groups.txt is the statement of
// record. All four of those groups, together with proof of life, ARE this
// step's GATE: they veto and earn nothing, and one failure among them zeroes
// the step.
//
// This file compiles unchanged against the v1.5.0 tree and against this one.
// driver-notes.md states the reason and it was re-checked before a line was
// written: every identifier named below -- SRT_ELARGEMSG, SRT_EINVPARAM,
// SRT_REJ_BADSECRET, and every SRT_TRACEBSTATS field the cases read -- exists
// in both trees. So there is no guarded restatement, no dlsym and no
// static_assert here, and the two tiers differ only in what they assert, never
// in what they can name.
//
// Three tiers, three jobs:
//   * g00 and the three inherited groups must pass at BOTH tags. They are the
//     regression guards: a candidate who breaks ordinary transfer, recovery or
//     encryption to make the new groups pass loses these.
//   * g07/g08/g09 must pass at v1.5.2 and fail at v1.5.0 on an ASSERTION, not
//     on a compile or link error. Measured, at v1.5.0:
//       - the sender accepts a 1316-byte message at every MSS and writes it to
//         the wire (g07: asserted 956-byte bound -> FAIL, and the 1200-byte
//         message is not refused with SRT_ELARGEMSG);
//       - the interval byteRcvLoss on a link where nothing was lost reads the
//         whole byteRecv of the sample, not zero (g08: 27200 vs 0 -> FAIL);
//       - a session that cannot decrypt delivers all 26320 bytes to the
//         application and leaves the interval byteRcvDrop at zero (g09: FAIL).
//
// Counters, and the two conventions they use, are the subject of two cases and
// are stated in interface.md: `pktRcvUndecrypt` and `byteRcvUndecrypt` count
// payload only (1316 per 1316-byte message), while the interval `byteRcvDrop`
// counts the same datagrams with their 44 bytes of header (1360 each).
#include "srtkit.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

// ---------- shared shape ----------

// The live-transport defaults every group here starts from: TSBPD on, a
// latency generous enough that the harness schedule -- and never the clock --
// decides what arrives, and too-late packet drop off so a dropped datagram is
// recovered rather than discarded on arrival. `st_live_lossless` is the same
// pair; this only adds the payload bound.
static void live_conf(SRTSOCKET s) {
    st_live_lossless(s);
}

// A configuration that also pins the maximum segment size. The single-packet
// bound this milestone introduces is a function of SRTO_MSS, so a group that
// tests the bound must set it on BOTH sides -- the negotiated size is the
// smaller of the two.
static SockConf live_mss(int mss) {
    return [mss](SRTSOCKET s) {
        live_conf(s);
        st_set_int(s, SRTO_MSS, mss);
    };
}

// A configuration for a stream of `sz`-byte messages: the live defaults with
// the payload size pinned to what this case actually sends.
//
// This is not decoration. Live mode derives the congestion controller's buffer
// from SRTO_PAYLOADSIZE at connect time, and that option defaults to the full
// 1316 rather than to what the stream carries. Measured on this release: a
// stream of 1200-byte messages over a peer that left the option at its default
// reaches the receiver's counters (pktRecvTotal 40, pktRcvDrop 0) and is
// delivered to nobody -- srt_recvmsg times out on every call. With the option
// pinned to 1200 on both sides the same stream arrives 40/40 byte-identical.
// The size that does arrive unpinned is 1316, the option's own default, which
// is why only that size works by accident.
static SockConf live_sized(int sz) {
    return [sz](SRTSOCKET s) {
        live_conf(s);
        st_set_int(s, SRTO_PAYLOADSIZE, sz);
    };
}

// Microseconds between sends for any stream long enough to matter.
//
// relay.h is a userspace hop with an ordinary socket buffer: it reads one
// datagram per loop iteration, so an unpaced burst of a few hundred datagrams
// is dropped by the kernel before the relay ever sees it -- BELOW the layer
// SRT retransmits from, which with SRTO_TLPKTDROP=0 stalls the receiver on the
// gap. Measured unpaced on this box at load average 14-22: the 3000-message
// g00_endtoend stream lost 2889/3000 to 2616/3000 on five of eight case-runs,
// on the clean arm as well as the delayed one. 1000 us -- about 10 Mbit/s at
// this payload -- is step 09's measured-robust figure and costs 3 s of pacing
// against a 25 s receive budget. See misc/<task-id>/NOTES.md.
static const int LIVE_GAP_US = 1000;

static SRT_TRACEBSTATS bstats_of(SRTSOCKET s, int clear) {
    SRT_TRACEBSTATS p;
    std::memset(&p, 0, sizeof p);
    ST_REQUIRE_MSG(srt_bstats(s, &p, clear) != SRT_ERROR, "srt_bstats: %s",
                   srt_getlasterror_str());
    return p;
}

// SRTO_ENFORCEDENCRYPTION's getter answers with a one-byte boolean, so the
// four-byte helpers in srtkit.h cannot read it -- they reject the short reply.
// It is read and written here as what it is. The value is written as a raw
// char for the same reason. The read half is also the negative control the
// relaxation in g09 needs: the option starts at its default and only the
// explicit set below changes it.
static int enforced_of(SRTSOCKET s) {
    char b = -1;
    int n = (int)sizeof b;
    ST_REQUIRE_MSG(srt_getsockflag(s, SRTO_ENFORCEDENCRYPTION, &b, &n) != SRT_ERROR,
                   "getsockflag(SRTO_ENFORCEDENCRYPTION): %s", srt_getlasterror_str());
    return (int)b;
}

static void set_enforced(SRTSOCKET s, int v) {
    char b = (char)v;
    ST_REQUIRE_MSG(srt_setsockflag(s, SRTO_ENFORCEDENCRYPTION, &b, 1) != SRT_ERROR,
                   "setsockflag(SRTO_ENFORCEDENCRYPTION, %d): %s", v, srt_getlasterror_str());
}

// Bring up listener -> relay -> caller where the connection is expected to be
// REFUSED, and bound the wait.
//
// st_try_connect in srtkit.h puts a timeout on the listener and accepts on its
// own thread, which is right for a handshake that completes. It is not enough
// here: measured on this release, srt_accept does not honour SRTO_RCVTIMEO at
// all, so a refused handshake leaves that accept blocked and the case dies on
// its alarm instead of observing the refusal. The accept below is driven by a
// non-blocking listener in a poll loop, which does bound it.
static void st_connect_refused(Link& L, const RelaySched& sched, const SockConf& lconf,
                               const SockConf& cconf, int wait_ms) {
    L.lsn = st_new_socket();
    ST_REQUIRE_MSG(L.lsn != SRT_INVALID_SOCK, "srt_socket (listener): %s",
                   srt_getlasterror_str());
    // Non-blocking BEFORE the group's own configuration, so that the group's
    // SockConf cannot put the listener back in blocking mode and silently
    // undo the bound below.
    ST_REQUIRE_MSG(srt_setsockflag(L.lsn, SRTO_RCVSYN, (void*)"\0", 1) != SRT_ERROR,
                   "setsockflag(SRTO_RCVSYN, 0): %s", srt_getlasterror_str());
    lconf(L.lsn);

    sockaddr_in la = st_loopback(0);
    ST_REQUIRE_MSG(srt_bind(L.lsn, (sockaddr*)&la, sizeof la) != SRT_ERROR, "srt_bind: %s",
                   srt_getlasterror_str());
    int nl = (int)sizeof la;
    ST_REQUIRE_MSG(srt_getsockname(L.lsn, (sockaddr*)&la, &nl) != SRT_ERROR, "srt_getsockname: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(L.lsn, 1) != SRT_ERROR, "srt_listen: %s", srt_getlasterror_str());

    L.relay = new Relay(ntohs(la.sin_port), sched);

    L.cal = st_new_socket();
    ST_REQUIRE_MSG(L.cal != SRT_INVALID_SOCK, "srt_socket (caller): %s",
                   srt_getlasterror_str());
    cconf(L.cal);

    // srt_connect is what must be refused. It is called first because the
    // listener has to be listening before the caller's handshake arrives, and
    // because the refusal is the caller's to report.
    sockaddr_in ra = st_loopback(L.relay->port());
    L.connect_rc = srt_connect(L.cal, (sockaddr*)&ra, sizeof ra);
    L.connect_err = srt_getlasterror(nullptr);

    // Then the listener is asked, without blocking, whether it ever accepted.
    int64_t t0 = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now().time_since_epoch()).count();
    for (;;) {
        SRTSOCKET acc = srt_accept(L.lsn, nullptr, nullptr);
        if (acc != SRT_INVALID_SOCK) {
            L.acc = acc;
            break;
        }
        if (srt_getlasterror(nullptr) != SRT_EASYNCRCV) break;
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - t0 > wait_ms) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// A socket's key-material state, or -1 when the socket never reached
// SRTS_CONNECTED. Reading it on a dead socket is not meaningful and the
// getter answers a stale zero.
static int kmstate_of(SRTSOCKET s) {
    if (s == SRT_INVALID_SOCK || srt_getsockstate(s) != SRTS_CONNECTED) return -1;
    return st_get_int(s, SRTO_KMSTATE);
}

// How many messages `sent` and `got` share, position by position, and whether
// `got` holds every one of `sent` in order with nothing substituted.
static void compare_stream(const std::vector<std::string>& sent,
                           const std::vector<std::string>& got, int* same, int* ooo,
                           int* complete) {
    *same = 0;
    for (size_t k = 0; k < got.size() && k < sent.size(); k++)
        if (got[k] == sent[k]) (*same)++;
    *ooo = st_first_out_of_order(sent, got);
    *complete = (got.size() == sent.size() && *ooo == -1 && *same == (int)sent.size()) ? 1 : 0;
}

// A payload that fits one packet at the given segment size, computed from the
// documented rule rather than hardcoded: min(1316, SRTO_MSS - 44). The 1316
// ceiling is the live-mode congestion controller's own payload cap, and it is
// what makes the rule non-monotonic at MSS >= 1360.
static int one_packet_bound(int mss) {
    int by_mss = mss - 44;
    return by_mss < 1316 ? by_mss : 1316;
}

// ---------- g07_onepacket: the single-packet payload bound (new) ----------

// RQ-10-07. With MSS pinned, a message larger than one packet must be REFUSED,
// not split: live mode sends each application message in exactly one packet.
// The bound is min(1316, MSS - 44). Measured at v1.5.2 for MSS 700/1000/1100/
// 1316/1500 the accepted maximum is 656/956/1056/1272/1316, each with a refusal
// one byte above it -- SRT_ELARGEMSG where the segment size sets the bound
// (MSS - 44 < 1316), SRT_EINVALMSGAPI where the 1316 cap does; at v1.5.0 the
// accepted maximum is 1316 at every one of those segment sizes.
SRT_CASE(g07_onepacket, bound_is_min_of_payload_cap_and_segment_size) {
    const int msses[] = {700, 1000, 1100, 1316, 1500};
    for (size_t i = 0; i < sizeof msses / sizeof msses[0]; i++) {
        const int mss = msses[i];
        const int bound = one_packet_bound(mss);
        // One connection per segment size, and each one is torn down before the
        // next starts. A message accepted in one iteration is still in flight
        // through the relay while the next iteration runs, and the wire
        // assertion below would read that in-flight datagram as the refused
        // message's.
        Link L;
        RelaySched sched;
        st_connect(L, sched, live_mss(mss), live_mss(mss));

        std::string fits = st_payload(0, bound);
        int rc = srt_sendmsg(L.cal, fits.data(), (int)fits.size(), -1, 1);
        ST_REQUIRE_MSG(rc == bound,
                       "MSS %d: a %d-byte message is the largest that fits one packet, but "
                       "srt_sendmsg returned %d (%s)",
                       mss, bound, rc, rc == SRT_ERROR ? srt_getlasterror_str() : "no error");

        // Let the accepted message actually arrive before taking the baseline.
        // At MSS 700 the message that fits is still crossing the relay when the
        // next send is issued, and counting it as the refused message's would
        // read as a spurious datagram on the wire. Draining it also proves the
        // message that fits really is delivered.
        st_set_int(L.acc, SRTO_RCVTIMEO, 4000);
        char buf[2048];
        int landing = srt_recvmsg(L.acc, buf, (int)sizeof buf);
        ST_REQUIRE_MSG(landing == bound,
                       "MSS %d: the %d-byte message that fits one packet arrived as %d bytes "
                       "(%s)", mss, bound, landing,
                       landing == SRT_ERROR ? srt_getlasterror_str() : "no error");

        // One byte over must be refused with the message-size error, and it
        // must not reach the wire. At v1.5.0 this send succeeds and the
        // datagram is forwarded, so both halves of this assertion fail there.
        std::string over = st_payload(1, bound + 1);
        int before = L.relay->seen_fwd();
        int rc2 = srt_sendmsg(L.cal, over.data(), (int)over.size(), -1, 1);
        ST_REQUIRE_MSG(rc2 == SRT_ERROR,
                       "MSS %d: a %d-byte message exceeds the %d-byte single-packet limit and "
                       "must be refused, but srt_sendmsg accepted it (%d bytes)",
                       mss, bound + 1, bound, rc2);
        int err = srt_getlasterror(nullptr);
        // Two refusals live above the bound and they are not the same error.
        // While the segment size is the binding constraint the sender refuses
        // with SRT_ELARGEMSG. Once the segment size is large enough that
        // MSS - 44 would exceed 1316, the live-mode congestion controller's own
        // payload cap binds first and the refusal comes from the message API
        // instead. Which one applies is decided by the same min() the bound is.
        const int want_err = (mss - 44 < 1316) ? SRT_ELARGEMSG : SRT_EINVALMSGAPI;
        ST_REQUIRE_MSG(err == want_err,
                       "MSS %d: a %d-byte message was refused with %d (%s); with a bound of "
                       "min(1316, MSS - 44) the refusal must be %d (%s)",
                       mss, bound + 1, err, srt_strerror(err, 0), want_err,
                       srt_strerror(want_err, 0));
        ST_REQUIRE_MSG(L.relay->seen_fwd() == before,
                       "MSS %d: the refused %d-byte message put %d datagram(s) on the wire; a "
                       "refused message must transmit nothing",
                       mss, bound + 1, L.relay->seen_fwd() - before);
    }
}

// RQ-10-07's mandatory member: the group pins MSS to 1000. A 1200-byte message
// cannot fit the 956-byte packet that leaves, and the largest that does fit is
// delivered intact -- so the refusal above is the bound doing its job and not
// sendmsg refusing everything.
SRT_CASE(g07_onepacket, mss_1000_refuses_1200_and_delivers_956) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, live_mss(1000), live_mss(1000));

    std::string big = st_payload(7, 1200);
    int before = L.relay->seen_fwd();
    int rc = srt_sendmsg(L.cal, big.data(), (int)big.size(), -1, 1);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "MSS 1000: srt_sendmsg of a 1200-byte message returned %d; it must be refused "
                   "rather than split across two packets",
                   rc);
    int err = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(err == SRT_ELARGEMSG, "MSS 1000: refusal error was %d (%s), expected "
                   "SRT_ELARGEMSG (%d)", err, srt_strerror(err, 0), SRT_ELARGEMSG);
    ST_REQUIRE_MSG(L.relay->seen_fwd() == before,
                   "MSS 1000: the refused 1200-byte message put %d datagram(s) on the wire",
                   L.relay->seen_fwd() - before);

    std::string ok = st_payload(8, 956);
    rc = srt_sendmsg(L.cal, ok.data(), (int)ok.size(), -1, 1);
    ST_REQUIRE_MSG(rc == 956, "MSS 1000: srt_sendmsg of 956 bytes returned %d (%s)", rc,
                   rc == SRT_ERROR ? srt_getlasterror_str() : "no error");

    st_set_int(L.acc, SRTO_RCVTIMEO, 4000);
    char buf[2048];
    int got = srt_recvmsg(L.acc, buf, (int)sizeof buf);
    ST_REQUIRE_MSG(got == 956, "MSS 1000: the 956-byte message arrived as %d bytes (%s)", got,
                   got == SRT_ERROR ? srt_getlasterror_str() : "no error");
    ST_REQUIRE_MSG(std::string(buf, (size_t)got) == ok,
                   "MSS 1000: the 956-byte message arrived corrupted");
}

// ---------- g08_rcvloss_bytes: the interval loss counter (new) ----------

// RQ-10-08. On a link where nothing was lost the interval `byteRcvLoss` is
// zero. Measured: at v1.5.2 it reads 0 for a clean 20-message exchange, and at
// v1.5.0 it reads the entire byteRecv of the sample (27200 for 20x1316) -- so
// the counter is the milestone's, not the sample's. `byteRcvLossTotal` is 0 at
// BOTH tags and does not discriminate; only the interval form does.
SRT_CASE(g08_rcvloss_bytes, clean_link_reports_no_loss) {
    Link L;
    RelaySched sched;
    st_connect(L, sched, live_conf, live_conf);

    const int N = 20, SZ = 1316;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, SZ, 15000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, SZ, 0);
    rcvr.join();
    ST_REQUIRE_MSG(got.size() == (size_t)N, "clean link delivered %zu of %d messages",
                   got.size(), N);
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 0,
                   "the clean schedule dropped %d datagram(s); this case is about a link on "
                   "which nothing was lost", L.relay->dropped_fwd());

    SRT_TRACEBSTATS p = bstats_of(L.acc, 0);
    // byteRecv counts every byte that arrived on the wire, headers included:
    // 20 packets of 1316 payload plus 44 header bytes is 27200, not 26320.
    // The payload only figure is byteRcvUndecrypt's convention, in g09.
    //
    // These two are FLOORS, not equalities. They are supporting evidence that
    // the sample really covers the 20 messages the case sent -- the messages
    // themselves were already proved delivered above -- and neither is the
    // counter this group discriminates on. TEST_INTERFACE.md section 5 puts no
    // ceiling over what a receiver counts: incidental traffic between the two
    // processes and the periodic loss report SRTO_NAKREPORT leaves on by
    // default can raise an arrival count without any defect in the candidate's
    // library. The discriminators below stay exact, because section 5 makes
    // those exact.
    ST_REQUIRE_MSG(p.byteRecv >= (uint64_t)N * (uint64_t)(SZ + 44),
                   "the receiver counted byteRecv=%lld for %d messages of %d bytes; with the 44 "
                   "header bytes per packet that is at least %d",
                   (long long)p.byteRecv, N, SZ, N * (SZ + 44));
    ST_REQUIRE_MSG(p.pktRecv >= N, "the receiver counted pktRecv=%lld for %d messages",
                   (long long)p.pktRecv, N);
    // The discriminator: v1.5.0 reports byteRcvLoss == byteRecv == 27200 here.
    ST_REQUIRE_MSG(p.byteRcvLoss == 0u,
                   "on a link where nothing was lost the interval byteRcvLoss must be 0, but "
                   "this sample reports %lld (its byteRecv is %lld)",
                   (long long)p.byteRcvLoss, (long long)p.byteRecv);
    ST_REQUIRE_MSG(p.pktRcvLoss == 0,
                   "on a link where nothing was lost pktRcvLoss must be 0, but this sample "
                   "reports %lld", (long long)p.pktRcvLoss);
}

// The same counter on a link where something WAS lost, so that a receiver that
// simply always answers zero does not pass the case above. This one passes at
// both tags and exists to make the zero above mean something.
SRT_CASE(g08_rcvloss_bytes, lossy_link_reports_the_loss) {
    Link L;
    RelaySched sched;
    for (int i = 200; i < 205; i++) sched.drop_fwd.insert(i);
    // A generous latency budget, as g11 uses: a retransmission of a datagram
    // dropped at index 200 needs time to arrive before the play-out deadline
    // closes, and at 900 ms the tail of a 400-message stream does not make it.
    SockConf recover = [](SRTSOCKET s) {
        st_set_int(s, SRTO_TSBPDMODE, 1);
        st_set_int(s, SRTO_LATENCY, 4000);
        st_set_int(s, SRTO_TLPKTDROP, 0);
    };
    st_connect(L, sched, recover, recover);

    const int N = 400, SZ = 1316;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, SZ, 20000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, SZ, LIVE_GAP_US);
    rcvr.join();

    int same = 0, ooo = 0, complete = 0;
    compare_stream(sent, got, &same, &ooo, &complete);
    ST_REQUIRE_MSG(complete, "the lossy link delivered %zu of %d messages in order (%d same, "
                   "first mismatch at %d)", got.size(), N, same, ooo);
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 5,
                   "the schedule removed %d datagram(s), expected 5", L.relay->dropped_fwd());

    // The receiver's loss counters must show that loss really happened. They
    // are asserted as non-zero rather than as an exact count on purpose: a
    // retransmission can itself be lost, so the receiver's total can exceed the
    // datagram count the schedule removed, and how many retransmissions cross
    // is a timing property rather than a property of this milestone.
    SRT_TRACEBSTATS p = bstats_of(L.acc, 0);
    ST_REQUIRE_MSG(p.pktRcvLoss > 0,
                   "the receiver's interval pktRcvLoss is 0 after the schedule removed %d "
                   "datagram(s)", L.relay->dropped_fwd());
    ST_REQUIRE_MSG(p.pktRcvLossTotal > 0,
                   "the receiver's pktRcvLossTotal is 0 after the schedule removed %d "
                   "datagram(s)", L.relay->dropped_fwd());
    // The counter this group is about, on the link where it is non-zero at both
    // tags: the case above is only meaningful because this one can move.
    ST_REQUIRE_MSG(p.byteRcvLoss > 0u,
                   "the receiver's interval byteRcvLoss is 0 after %d datagram(s) were lost",
                   L.relay->dropped_fwd());
}

// ---------- g09_undecrypted: what a session that cannot decrypt does (new) ----------

// Fixture E of interface.md, and the shape RQ-10-09 describes. Both sides hold
// a different passphrase and enforced encryption is switched off on BOTH, so
// the connection is established and then every data packet fails to decrypt.
// The application must receive nothing, must never be told the socket is
// readable, and the counters must still account for every datagram that
// arrived. Measured at v1.5.2: 0 bytes delivered, epoll_wait -1, interval
// byteRcvDrop 27200 (20 x 1360, headers included), pktRcvUndecrypt 20 and
// byteRcvUndecrypt 26320 (20 x 1316, payload only). At v1.5.0 the same
// exchange delivers all 26320 bytes and leaves byteRcvDrop at 0.
SRT_CASE(g09_undecrypted, mismatched_secret_delivers_nothing) {
    const int N = 20, SZ = 1316;
    Link L;
    RelaySched sched;
    SockConf listener = [](SRTSOCKET s) {
        live_conf(s);
        st_set_str(s, SRTO_PASSPHRASE, "s!t@r#i$c^t");
        set_enforced(s, 0);
    };
    SockConf caller = [](SRTSOCKET s) {
        live_conf(s);
        st_set_str(s, SRTO_PASSPHRASE, "s!t@r#i$c^tu");  // one byte longer: different secret
        set_enforced(s, 0);
    };
    st_connect(L, sched, listener, caller);

    // Both relaxations really took, and the connection came up anyway: the
    // relaxation is what makes an undecryptable session reachable at all. It
    // is asserted on the LISTENER and on the caller, because one side left at
    // its default refuses the caller instead (that refusal is g12's case).
    ST_REQUIRE_MSG(enforced_of(L.lsn) == 0 && enforced_of(L.cal) == 0,
                   "enforced encryption is %d on the listener and %d on the caller; this case "
                   "is about a session where it was switched off on both",
                   enforced_of(L.lsn), enforced_of(L.cal));

    // The connection is up on both sides, and the key material says why the
    // data will not come through.
    ST_REQUIRE_MSG(srt_getsockstate(L.acc) == SRTS_CONNECTED,
                   "the listener's socket is in state %d, not connected",
                   srt_getsockstate(L.acc));
    ST_REQUIRE_MSG(kmstate_of(L.acc) == SRT_KM_S_BADSECRET,
                   "the listener's key-material state is %d; a session whose two sides hold "
                   "different passphrases must report SRT_KM_S_BADSECRET (%d)",
                   kmstate_of(L.acc), SRT_KM_S_BADSECRET);

    for (int i = 0; i < N; i++) {
        std::string m = st_payload(i, SZ);
        int rc = srt_sendmsg(L.cal, m.data(), (int)m.size(), -1, 1);
        ST_REQUIRE_MSG(rc == SZ, "undecryptable exchange: send %d returned %d (%s)", i, rc,
                       rc == SRT_ERROR ? srt_getlasterror_str() : "no error");
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }

    // Readability first, while the datagrams are certainly still in flight or
    // already dropped. A socket registered for SRT_EPOLL_IN must never be
    // reported readable on account of a packet that could not be decrypted.
    int ep = srt_epoll_create();
    ST_REQUIRE_MSG(ep != SRT_INVALID_SOCK, "srt_epoll_create: %s", srt_getlasterror_str());
    int ev = SRT_EPOLL_IN;
    ST_REQUIRE_MSG(srt_epoll_add_usock(ep, L.acc, &ev) == 0, "srt_epoll_add_usock: %s",
                   srt_getlasterror_str());
    for (int tries = 0; tries < 3; tries++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        // The read array gets its OWN storage. Passing one object as both
        // the array and its capacity aliases the returned count into the
        // array, and the capacity is what the library copies against; one
        // slot is enough, there is one socket in the set.
        SRTSOCKET rfd[1] = {SRT_INVALID_SOCK};
        int rnum = 1;
        int rv = srt_epoll_wait(ep, rfd, &rnum, nullptr, nullptr, 400, 0, 0, 0, 0);
        const int werr = srt_getlasterror(nullptr);
        ST_REQUIRE_MSG(rv < 0,
                       "the receiver was reported readable (%d socket(s)) after %d undecryptable "
                       "message(s); packets that fail to decrypt must not make the socket "
                       "readable", rv, N);
        // A bare `rv < 0` is also satisfied by the wait FAILING, which would
        // pass this case for the wrong reason. SRT_ETIMEOUT is the library's
        // own report that the wait expired with nothing ready, and that is the
        // only negative return which proves the socket was never readable.
        // Same treatment step 11 keeps on its copy of this shape.
        ST_REQUIRE_MSG(werr == SRT_ETIMEOUT,
                       "srt_epoll_wait returned %d and reported error %d (%s); the wait must "
                       "expire with SRT_ETIMEOUT (%d), which is what distinguishes a socket that "
                       "was never readable from a wait that failed",
                       rv, werr, srt_getlasterror_str(), (int)SRT_ETIMEOUT);
    }
    srt_epoll_release(ep);

    // Nothing reaches the application, and the read says so rather than
    // blocking: a case that waited on a stalled channel would cost its budget.
    st_set_int(L.acc, SRTO_RCVTIMEO, 2500);
    char buf[2048];
    int total = 0, calls = 0;
    for (;;) {
        int rc = srt_recvmsg(L.acc, buf, (int)sizeof buf);
        if (rc <= 0) break;
        total += rc;
        calls++;
    }
    ST_REQUIRE_MSG(total == 0,
                   "the application received %d byte(s) in %d call(s) from a session that cannot "
                   "decrypt; it must receive nothing", total, calls);

    // The accounting. Both conventions are asserted, and they differ: the
    // undecrypt counters carry payload only, the interval drop counter carries
    // the headers too.
    SRT_TRACEBSTATS p = bstats_of(L.acc, 0);
    // A FLOOR, for the same reason as g08's arrival counters: pktRecvTotal is
    // what the receiver observed arriving, not something this milestone
    // changed, and nothing puts a ceiling over it. The three counters below
    // are the discriminators and stay exact.
    ST_REQUIRE_MSG(p.pktRecvTotal >= N,
                   "the receiver accepted %lld packet(s), expected at least %d",
                   (long long)p.pktRecvTotal, N);
    ST_REQUIRE_MSG(p.pktRcvUndecrypt == N,
                   "pktRcvUndecrypt is %lld after %d undecryptable packet(s) arrived",
                   (long long)p.pktRcvUndecrypt, N);
    ST_REQUIRE_MSG(p.byteRcvUndecrypt == (uint64_t)N * (uint64_t)SZ,
                   "byteRcvUndecrypt is %lld; it counts payload only, so %d x %d bytes is %d",
                   (long long)p.byteRcvUndecrypt, N, SZ, N * SZ);
    // The discriminator: at v1.5.0 this interval stays 0 on the same exchange.
    ST_REQUIRE_MSG(p.byteRcvDrop == (uint64_t)N * (uint64_t)(SZ + 44),
                   "the interval byteRcvDrop is %lld; it accounts for every arrived packet with "
                   "its headers, so %d x (%d + 44) is %d",
                   (long long)p.byteRcvDrop, N, SZ, N * (SZ + 44));
}

// The positive control the case above needs: the same shape with a MATCHING
// passphrase delivers everything and leaves every drop and undecrypt counter
// at zero. Without this a receiver that dropped all traffic unconditionally
// would pass the case above.
SRT_CASE(g09_undecrypted, matching_secret_delivers_everything) {
    const int N = 20, SZ = 1316;
    Link L;
    RelaySched sched;
    SockConf both = [](SRTSOCKET s) {
        live_conf(s);
        st_set_str(s, SRTO_PASSPHRASE, "s!t@r#i$c^t");
        set_enforced(s, 0);  // the same relaxation; it is not what makes this work
    };
    st_connect(L, sched, both, both);

    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, SZ, 15000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, SZ, 0);
    rcvr.join();

    // After the traffic, not before it -- see the note in g12_encryption. This
    // group is scored rather than gate, so the cost of reading too early is a
    // lost weight instead of a zeroed step, but the assertion is no more
    // reliable here than it is there.
    ST_REQUIRE_MSG(kmstate_of(L.acc) == SRT_KM_S_SECURED,
                   "the listener's key-material state is %d after the stream crossed; with one "
                   "matching passphrase and no relaxation needed it must report "
                   "SRT_KM_S_SECURED (%d)", kmstate_of(L.acc), SRT_KM_S_SECURED);

    int same = 0, ooo = 0, complete = 0;
    compare_stream(sent, got, &same, &ooo, &complete);
    ST_REQUIRE_MSG(complete,
                   "the encrypted control delivered %zu of %d messages in order (%d same, first "
                   "mismatch at %d)", got.size(), N, same, ooo);

    SRT_TRACEBSTATS p = bstats_of(L.acc, 0);
    ST_REQUIRE_MSG(p.pktRcvUndecrypt == 0,
                   "the control's pktRcvUndecrypt is %lld with one matching passphrase",
                   (long long)p.pktRcvUndecrypt);
    ST_REQUIRE_MSG(p.byteRcvDrop == 0u,
                   "the control's interval byteRcvDrop is %lld with one matching passphrase",
                   (long long)p.byteRcvDrop);
}

// ---------- g10_transfer: ordinary transfer (inherited) ----------
//
// RQ-10-10. Must pass at both tags. A message at the negotiated payload size
// crosses an intact channel complete, in order and byte-identical, and the
// stream keeps flowing where nothing exceeds what one packet carries.
SRT_CASE(g10_transfer, messages_cross_intact_and_in_order) {
    const int N = 40;
    // Both the sizes the group names. Each gets its own connection with the
    // payload size pinned: a receiver that left SRTO_PAYLOADSIZE at its
    // default cannot deliver the 1200-byte stream at all (see live_sized), and
    // a stream cannot change size mid-connection in any case.
    const int sizes[] = {1200, 1316};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        const int SZ = sizes[i];
        Link L;
        RelaySched sched;
        st_connect(L, sched, live_sized(SZ), live_sized(SZ));

        std::vector<std::string> got;
        std::thread rcvr([&] { got = st_recv_stream(L.acc, N, SZ, 15000); });
        std::vector<std::string> sent = st_send_stream(L.cal, N, SZ, 200);
        rcvr.join();
        int same = 0, ooo = 0, complete = 0;
        compare_stream(sent, got, &same, &ooo, &complete);
        ST_REQUIRE_MSG(complete,
                       "%d x %d bytes over an intact channel: %zu delivered, %d identical, "
                       "first out-of-order at %d", N, SZ, got.size(), same, ooo);
        // The stream must keep flowing: the schedule dropped nothing, so any
        // loss the receiver reports would be the connection's own.
        //
        // These two are EQUALITIES, deliberately, and they are the opposite
        // shape from g11_recovery's four floors below. A floor is right where
        // a channel removed datagrams and incidental loss can only add to the
        // count; here the schedule removes nothing, the only number a clean
        // run can produce is 0, and TEST_INTERFACE.md section 5 promises
        // exactly that half exactly ("Where nothing was lost the receiver's
        // loss counters must read 0, and that half IS asserted exactly").
        // The headroom that makes an exact 0 safe in a GATE group is
        // arithmetic, not optimism: 40 datagrams of at most 1316 + 44 bytes is
        // 54 kB against the ~208 kB net.core.rmem_default this box gives the
        // relay's UDP socket -- the same budget g11_recovery's comment
        // invokes, where 200 datagrams DO exceed it and the floors follow.
        // Do not copy this shape into a case that sends enough to approach
        // that figure, and do not "repair" it into a floor: a floor here would
        // pass on a connection that really did lose packets.
        SRT_TRACEBSTATS p = bstats_of(L.acc, 0);
        ST_REQUIRE_MSG(p.pktRcvLossTotal == 0 && p.pktRcvDropTotal == 0,
                       "%d x %d bytes over an intact channel: the receiver reports %lld lost "
                       "and %lld dropped packet(s)", N, SZ, (long long)p.pktRcvLossTotal,
                       (long long)p.pktRcvDropTotal);
        ST_REQUIRE_MSG(L.relay->dropped_fwd() == 0,
                       "the intact channel dropped %d datagram(s)", L.relay->dropped_fwd());
    }
}

// ---------- g11_recovery: loss recovery (inherited) ----------

// RQ-10-11. Must pass at both tags. With too-late packet drop off and a
// generous latency budget, payload the relay removes at fixed indexes is
// retransmitted and arrives complete and in order, and the receiver's own loss
// counter and the sender's own retransmission counters confirm the loss really
// happened and that retransmission is what repaired it -- so the case cannot
// pass by the schedule having quietly done nothing.
//
// Every counter below is a FLOOR. See the note above the statistics block.
SRT_CASE(g11_recovery, dropped_datagrams_are_recovered) {
    Link L;
    RelaySched sched;
    for (int i = 60; i < 67; i++) sched.drop_fwd.insert(i);
    const int N = 200, SZ = 1316;
    SockConf recover = [](SRTSOCKET s) {
        // The payload size has to be pinned here too: the sender's
        // retransmission budget is derived from it, and left at the default
        // the repaired stream is not deliverable (see live_sized).
        live_sized(SZ)(s);
        st_set_int(s, SRTO_LATENCY, 4000);
    };
    st_connect(L, sched, recover, recover);

    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, SZ, 20000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, SZ, LIVE_GAP_US);
    rcvr.join();

    int same = 0, ooo = 0, complete = 0;
    compare_stream(sent, got, &same, &ooo, &complete);
    ST_REQUIRE_MSG(complete,
                   "recovery: %zu of %d messages delivered in order (%d identical, first "
                   "out-of-order at %d)", got.size(), N, same, ooo);
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 7,
                   "the schedule removed %d datagram(s), expected 7; the case is not testing "
                   "recovery unless the loss really happened", L.relay->dropped_fwd());

    // FLOORS, never equalities. These four read exactly 7 and 7 x 1360 when
    // measured, but this is a GATE group: it vetoes and earns nothing, so it
    // has to be unflakeable rather than maximally precise, and an equality is
    // a ceiling as well as a floor. Three mechanisms raise these numbers with
    // no candidate defect. A kernel-level drop at the userspace relay is
    // incidental loss that is a fact about the host's load at that instant,
    // not about the library -- relay.h never resizes its UDP socket, and 200
    // datagrams of 1360 bytes is more than net.core.rmem_default on this box.
    // SRTO_NAKREPORT, documented in srt-docs/API/API-socket-options.md as
    // defaulting to true in live mode and untouched by this case, repeats a
    // loss report whose retransmission timed out, so the same datagram can be
    // retransmitted more than once. And a retransmission can itself be lost,
    // which is the reason g08_rcvloss_bytes gives for its own floors, 200
    // lines above. The asymmetry is what makes the floor safe: a busy host can
    // only ADD loss and retransmission, so it can never push a lossy run under
    // the count the schedule removed. This is the package's standing rule for
    // counters in a gate group (misc/<task-id>/NOTES.md, "Gate cases deleted
    // for non-determinism"), and it is the shape step 04's g11_recovery
    // already uses. TEST_INTERFACE.md section 5 states these floors, so the
    // contract promises exactly what the case checks.
    SRT_TRACEBSTATS p = bstats_of(L.acc, 0);
    ST_REQUIRE_MSG(p.pktRcvLossTotal >= 7,
                   "the receiver's pktRcvLossTotal is %lld, under the %d datagram(s) the "
                   "schedule removed; the loss was never detected, so the complete stream "
                   "above did not come from recovering it",
                   (long long)p.pktRcvLossTotal, L.relay->dropped_fwd());
    ST_REQUIRE_MSG(p.pktRcvLoss >= 7,
                   "the receiver's interval pktRcvLoss is %lld, under the %d datagram(s) the "
                   "schedule removed",
                   (long long)p.pktRcvLoss, L.relay->dropped_fwd());
    // The retransmission is asserted on the SENDER, where it happens: the
    // receiver's trace carries no sender-side retransmission field.
    // srt-docs/API/statistics.md documents pktRetrans, byteRetransTotal and
    // the 44-byte header convention the byte floor uses.
    SRT_TRACEBSTATS q = bstats_of(L.cal, 0);
    ST_REQUIRE_MSG(q.pktRetrans >= 7,
                   "the payload arrived complete but the sender reports %lld retransmitted "
                   "packet(s), under the %d datagram(s) the schedule removed: the gap was "
                   "filled by something other than retransmission",
                   (long long)q.pktRetrans, L.relay->dropped_fwd());
    ST_REQUIRE_MSG(q.byteRetransTotal >= (uint64_t)L.relay->dropped_fwd() * (uint64_t)(SZ + 44),
                   "the sender's byteRetransTotal is %lld, under the %d bytes that %d "
                   "retransmitted datagram(s) of %d payload plus 44 header bytes come to",
                   (long long)q.byteRetransTotal, L.relay->dropped_fwd() * (SZ + 44),
                   L.relay->dropped_fwd(), SZ);
}

// ---------- g12_encryption: encryption end to end (inherited) ----------

// RQ-10-12. Must pass at both tags. A matching passphrase carries the payload
// intact at each of the THREE key lengths (16, 24, 32), with the RECEIVING
// side reporting the secured key-material state. Only the accepted socket is
// read: kmstate_of() is never called on L.cal here or anywhere else in this
// step, so "both sides" would be a promise the case does not keep -- scored.txt
// and TEST_INTERFACE.md section 3 say "the receiving side" for the same reason.
//
// The key length is what varies, and SRTO_PBKEYLEN is what varies it. Setting
// only the passphrase length would leave all three iterations at the default
// 16-byte key and the case would sweep nothing, while TEST_INTERFACE.md
// section 3 promises a sweep of the three key lengths. The passphrase is also
// made a different length each time, which costs nothing and keeps the three
// iterations from collapsing into one fixture.
SRT_CASE(g12_encryption, matching_key_lengths_carry_payload) {
    const size_t lens[] = {16, 24, 32};
    const int N = 20, SZ = 1316;
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        const size_t plen = lens[i];
        // A passphrase of exactly this length, printable and different per
        // length so a case cannot pass by the lengths collapsing together.
        std::string pass(plen, '.');
        for (size_t k = 0; k < plen; k++) pass[k] = (char)('a' + (int)((k + plen) % 26));

        Link L;
        RelaySched sched;
        SockConf both = [&pass, plen](SRTSOCKET s) {
            live_conf(s);
            st_set_str(s, SRTO_PASSPHRASE, pass.c_str());
            // Pre-connect, on both ends: this is the key length the case
            // claims to sweep. 16, 24 and 32 are SRTO_PBKEYLEN's documented
            // values and predate this milestone, so a group setting it still
            // passes at the previous release -- which is what keeps this one
            // in the gate.
            st_set_int(s, SRTO_PBKEYLEN, (int)plen);
        };
        st_connect(L, sched, both, both);

        std::vector<std::string> got;
        std::thread rcvr([&] { got = st_recv_stream(L.acc, N, SZ, 15000); });
        std::vector<std::string> sent = st_send_stream(L.cal, N, SZ, 0);
        rcvr.join();
        int same = 0, ooo = 0, complete = 0;
        compare_stream(sent, got, &same, &ooo, &complete);
        ST_REQUIRE_MSG(complete, "key length %zu: %zu of %d messages delivered in order (%d "
                       "identical, first out-of-order at %d)", plen, got.size(), N, same, ooo);

        // Read the key-material state AFTER the stream, never straight out of
        // st_connect(). This group is GATE: it earns nothing and only vetoes,
        // so an assertion here that is right most of the time zeroes the whole
        // step for every candidate it catches, the Oracle included. The state
        // immediately after a connect returns is not something the package
        // promises -- TEST_INTERFACE.md at milestone 07 says in as many words
        // that both sides may still read SRT_KM_S_UNSECURED at that instant and
        // must be read once messages have moved, and milestone 03's says the
        // exchange is traffic-driven. The messages above have now moved and
        // arrived intact, so this is the documented observation point. The
        // assertion itself is unchanged: still exactly SRT_KM_S_SECURED.
        ST_REQUIRE_MSG(kmstate_of(L.acc) == SRT_KM_S_SECURED,
                       "key length %zu: the listener's key-material state is %d after %d "
                       "messages crossed, expected SRT_KM_S_SECURED (%d)", plen,
                       kmstate_of(L.acc), N, SRT_KM_S_SECURED);
    }
}

// The negative half of the same requirement, and the one interface.md states:
// with enforced encryption left at its DEFAULT, a mismatched passphrase is
// still refused at connect time and the caller is told the passphrases differ.
// The refusal is the point -- it is why the relaxation in g09 has to be asked
// for explicitly.
SRT_CASE(g12_encryption, default_enforcement_refuses_a_mismatched_secret) {
    Link L;
    RelaySched sched;
    SockConf listener = [](SRTSOCKET s) {
        live_conf(s);
        st_set_str(s, SRTO_PASSPHRASE, "s!t@r#i$c^t");
    };
    SockConf caller = [](SRTSOCKET s) {
        live_conf(s);
        st_set_str(s, SRTO_PASSPHRASE, "s!t@r#i$c^tu");
        st_set_int(s, SRTO_CONNTIMEO, 4000);
    };
    st_connect_refused(L, sched, listener, caller, 6000);
    ST_REQUIRE_MSG(L.connect_rc == SRT_ERROR,
                   "with enforced encryption at its default a caller holding a different "
                   "passphrase from the listener must be refused, but srt_connect returned %d",
                   L.connect_rc);
    ST_REQUIRE_MSG(L.acc == SRT_INVALID_SOCK,
                   "the listener accepted a socket from a caller whose passphrase differs");
    int reason = srt_getrejectreason(L.cal);
    ST_REQUIRE_MSG(reason == SRT_REJ_BADSECRET,
                   "the caller reports rejection reason %d; it must report the one that says the "
                   "passphrases differ, SRT_REJ_BADSECRET (%d)", reason, SRT_REJ_BADSECRET);
}

// -------- g00_endtoend: this step's end-to-end path (GATE, inherited) -------

// RQ-10-13. 3000 messages, each within the payload limit in force, cross a
// clean relay and arrive byte-identical, in order and with no gaps; the same
// stream does the same over a relay that adds 15 ms each way. Both checks live
// in this one group.
//
// The latency is 8000 ms and the messages are 1316 bytes. That combination is
// not arbitrary: live mode paces the sender, so a shorter latency budget lets
// the sender run ahead of a window that has not drained yet and the tail of the
// stream is discarded before it can cross.
//
// The sends are paced at LIVE_GAP_US. The first version of this case was
// unpaced, and the note above it recorded a clean measurement -- "clean
// 3000/3000 in 8030 ms, 15 ms each way 3000/3000 in 8055 ms" -- taken on an
// idle box. It does not hold on a loaded one: re-measured at load average
// 14-22 the unpaced form lost between 2616 and 2983 of 3000, on the CLEAN arm
// as often as the delayed one, because the burst overflows the relay's socket
// buffer rather than anything SRT controls. Paced, 4 of 4 case-runs passed
// under the same load. The assertions below are unchanged; only the send rate
// is.
static void gate_stream(const char* tag, bool delayed) {
    Link L;
    RelaySched sched;
    if (delayed) {
        sched.delay_fwd_ms = 15;
        sched.delay_bwd_ms = 15;
    }
    SockConf cfg = [](SRTSOCKET s) {
        st_set_int(s, SRTO_TSBPDMODE, 1);
        st_set_int(s, SRTO_LATENCY, 8000);
        st_set_int(s, SRTO_TLPKTDROP, 0);
    };
    st_connect(L, sched, cfg, cfg);

    const int N = 3000, SZ = 1316;
    std::vector<std::string> got;
    std::thread rcvr([&] { got = st_recv_stream(L.acc, N, SZ, 25000); });
    std::vector<std::string> sent = st_send_stream(L.cal, N, SZ, LIVE_GAP_US);
    rcvr.join();

    int same = 0, ooo = 0, complete = 0;
    compare_stream(sent, got, &same, &ooo, &complete);
    ST_REQUIRE_MSG(got.size() == (size_t)N, "%s: %zu of %d messages arrived (gaps)",
                   tag, got.size(), N);
    ST_REQUIRE_MSG(ooo == -1, "%s: the stream arrived out of order; the first out-of-order "
                   "message is at index %d", tag, ooo);
    ST_REQUIRE_MSG(same == N, "%s: %d of %d messages are byte-identical; the stream must "
                   "arrive intact as well as complete", tag, same, N);
    ST_REQUIRE_MSG(L.relay->dropped_fwd() == 0, "%s: the channel dropped %d datagram(s)",
                   tag, L.relay->dropped_fwd());
}

SRT_CASE(g00_endtoend, three_thousand_over_a_clean_relay) {
    gate_stream("clean relay", false);
}

SRT_CASE(g00_endtoend, three_thousand_over_a_relay_delaying_each_way) {
    gate_stream("15 ms each way", true);
}
