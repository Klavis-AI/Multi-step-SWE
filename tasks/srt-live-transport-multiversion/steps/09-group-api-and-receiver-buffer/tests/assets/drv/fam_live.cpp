// Step 09 (v1.5.0) live-transport family: the two new counters the milestone
// changes, and the two inherited live behaviours that form this step's gate.
//
// This milestone's end-to-end path is NOT here. It carries a stream over a
// socket group, so it needs the group API, and the group API is only declared
// when the tree is configured with this step's `-DENABLE_BONDING=ON`. A no-op
// candidate leaves the v1.4.4 tree in /app, which that flag does not reach, so
// a gate family holding the end-to-end case would fail to COMPILE at the no-op
// prefix -- and a gate that cannot be built reports `reward = 0` without ever
// being evaluated, which is indistinguishable from a candidate that did
// nothing. `g00_endtoend` therefore lives in fam_group.cpp with the rest of the
// group-API work; this file must build at BOTH prefixes and does.
//
// Every case here was measured against the installed library at v1.5.0
// (`dev/bondroot/09`) and at v1.4.4 (`dev/bondroot/08`) before it was written,
// so the numbers in the messages are the numbers the library prints.
//
// Two habits this file keeps, both forced by the measurements:
//
//   * Statistics are read while the connection is still open. At v1.5.0
//     closing the peer socket zeroes the whole `SRT_TRACEBSTATS` block, so a
//     dump taken after `srt_close` reads zeros and a case built on it would
//     pass vacuously at v1.5.0 by comparing 0 against 0.
//   * `SRTO_ENFORCEDENCRYPTION` is set *before* `SRTO_PASSPHRASE`. With the
//     reverse order the receiver keeps the default (on) and refuses a
//     mismatched handshake even when the case asked for the lenient mode --
//     measured, not assumed.
//
// Everything named below exists at both v1.4.4 and v1.5.0, so no identifier is
// restated under a version guard: the file builds unchanged at both prefixes.

#include <srt/srt.h>

#include "srtkit.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

// The live profile every group starts from: TSBPD on, latency generous enough
// that the relay's schedule rather than the clock decides what arrives, and
// too-late-to-play dropping off so that a late packet is an observation, not a
// deletion.
SockConf live_conf(int latency_ms) {
    return [latency_ms](SRTSOCKET s) {
        st_set_int(s, SRTO_TSBPDMODE, 1);
        st_set_int(s, SRTO_LATENCY, latency_ms);
        st_set_int(s, SRTO_TLPKTDROP, 0);
    };
}

// The pre-reset view of the receiver's counters. `srt_bstats(s, &p, 1)` fills
// `p` with the trace values and only then zeroes the trace, so a case may ask
// for the interval counters without losing the totals.
SRT_TRACEBSTATS stats_of(SRTSOCKET s, int clear = 0) {
    SRT_TRACEBSTATS p;
    std::memset(&p, 0, sizeof p);
    ST_SRT_OK(srt_bstats(s, &p, clear));
    return p;
}

// True when the receive that just failed only ran out its `SRTO_RCVTIMEO`, in
// which case the caller keeps waiting until its own deadline. Any other error
// ends the read at once: a broken connection must not be waited out.
//
// This distinction is load-bearing, not defensive. A reader that treats the
// first expiry as end-of-stream returns nothing whenever the first message is
// due later than one timeout -- which is the normal case under TSBPD, where the
// first delivery is held for the negotiated latency. Measured at v1.4.4: the
// same readers that pass at v1.5.0 returned 0 of 5, 0 of 20 and 0 of 200
// because of exactly that.
bool recv_timed_out() {
    int eg = srt_getlasterror(nullptr);
    if (eg == SRT_ETIMEOUT || eg == SRT_EASYNCRCV) {
        srt_clearlasterror();
        return true;
    }
    return false;
}

// Receives until the deadline, driven by the receiver and bounded by
// `SRTO_RCVTIMEO`; never a fixed sleep. Collects the messages verbatim so the
// caller can compare them, and stops early once `want` have arrived.
std::vector<std::string> read_until(SRTSOCKET s, int want, int timeo_ms, int budget_ms) {
    st_set_int(s, SRTO_RCVTIMEO, timeo_ms);
    std::vector<std::string> got;
    std::string buf(4096, '\0');
    auto t0 = std::chrono::steady_clock::now();
    while ((int)got.size() < want &&
           std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count() < budget_ms) {
        int rc = srt_recv(s, &buf[0], (int)buf.size());
        if (rc > 0) { got.push_back(buf.substr(0, (size_t)rc)); continue; }
        if (!recv_timed_out()) break;
    }
    return got;
}

}  // namespace

// Waits until the receiver's own packet counter reaches `want`, or gives up
// after `budget_ms`. The counters are read with `clear = 0`: probing must not
// disturb the interval a case is about to assert on. A burst sent immediately
// after connect is not yet visible to a reader that starts at once -- measured
// once at 0 of 5 and once at 5 of 5 -- so a case that samples immediately is
// racing the library rather than testing it.
bool wait_deliver(const std::atomic<int>& got, int want, int budget_ms) {
    for (int waited = 0; waited < budget_ms; waited += 50) {
        if (got.load() >= want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return got.load() >= want;
}

// ---------------------------------------------------------------------------
// g08_rcvloss_bytes -- `byteRcvLoss` is a byte count.
//
// At v1.5.0 the interval field carries received bytes; at v1.4.4 it carried
// retransmitted bytes. The case pins the value to the received byte count on a
// channel whose only loss is retransmitted, and to zero on a clean channel.
// ---------------------------------------------------------------------------
namespace {

SRT_CASE(g08_rcvloss_bytes, counter_follows_received_bytes) {
    const int N = 20, SZ = 1000;

    // Leg 1: a clean channel. Nothing is lost, so both views of loss are zero.
    {
        Link L;
        st_connect(L, RelaySched{}, live_conf(900), live_conf(900));
        std::atomic<int> got(0);
        std::atomic<bool> done(false);
        std::thread rd([&] {
            st_set_int(L.acc, SRTO_RCVTIMEO, 300);
            std::string buf(4096, '\0');
            while (!done.load()) {
                int rc = srt_recv(L.acc, &buf[0], (int)buf.size());
                if (rc > 0) { got++; continue; }
                if (!recv_timed_out()) break;
            }
        });
        std::vector<std::string> sent;
        for (int i = 0; i < 5; i++) sent.push_back(st_payload(i, SZ));
        for (size_t i = 0; i < sent.size(); i++)
            ST_SRT_OK(srt_sendmsg(L.cal, sent[i].data(), (int)sent[i].size(), -1, 1));
        int settled = wait_deliver(got, 5, 3000);
        SRT_TRACEBSTATS p = stats_of(L.acc, 1);
        done.store(true);
        srt_close(L.cal);
        rd.join();

        ST_REQUIRE_MSG(settled, "clean channel: the receiver's own count never reached 5 "
                                "packets within 3 s");
        ST_REQUIRE_EQ_INT(got.load(), 5);
        ST_REQUIRE_EQ_INT(p.pktRcvLoss, 0);
        ST_REQUIRE_EQ_INT(p.pktRecv, 5);
        ST_REQUIRE_MSG(p.byteRecv > 0,
                       "byteRecv on a clean channel that carried 5 messages: expected > 0, got 0");
        ST_REQUIRE_MSG(p.byteRcvLoss == p.byteRecv,
                       "interval byteRcvLoss on a clean channel: expected the interval byteRecv "
                       "of the same sample, %llu, got %llu",
                       (unsigned long long)p.byteRecv, (unsigned long long)p.byteRcvLoss);
        ST_REQUIRE_MSG(p.byteRcvLoss > 0,
                       "interval byteRcvLoss on a clean channel: expected a non-zero byte count, "
                       "got 0");
        ST_REQUIRE_MSG(p.byteRcvLossTotal == 0,
                       "accumulated byteRcvLossTotal on a clean channel: expected 0, got %llu",
                       (unsigned long long)p.byteRcvLossTotal);
    }

    // Leg 2: two lost packets, both retransmitted. The received byte count and
    // the reported loss must not disagree -- at v1.5.0 byteRcvLoss tracks what
    // arrived (20880), not what was retransmitted (2704).
    {
        RelaySched sched;
        sched.drop_fwd.insert(2);
        sched.drop_fwd.insert(3);
        Link L;
        st_connect(L, sched, live_conf(900), live_conf(900));
        st_set_int(L.acc, SRTO_RCVTIMEO, 400);
        std::atomic<int> got(0);
        std::atomic<bool> done(false);
        std::thread rd([&] {
            std::string buf(4096, '\0');
            while (!done.load()) {
                int rc = srt_recv(L.acc, &buf[0], (int)buf.size());
                if (rc > 0) { got++; continue; }
                if (!recv_timed_out()) break;
            }
        });
        std::vector<std::string> sent;
        for (int i = 0; i < N; i++) sent.push_back(st_payload(i, SZ));
        for (size_t i = 0; i < sent.size(); i++)
            ST_SRT_OK(srt_sendmsg(L.cal, sent[i].data(), (int)sent[i].size(), -1, 1));
        int settled = wait_deliver(got, N, 5000);
        SRT_TRACEBSTATS p = stats_of(L.acc, 1);
        done.store(true);
        srt_close(L.cal);
        rd.join();

        ST_REQUIRE_MSG(settled, "lossy channel: the receiver's own count never reached %d "
                                "packets within 5 s", N);
        ST_REQUIRE_EQ_INT(L.relay->dropped_fwd(), 2);
        ST_REQUIRE_EQ_INT(got.load(), N);
        ST_REQUIRE_EQ_INT(p.pktRecv, N);
        ST_REQUIRE_EQ_INT(p.pktRcvLoss, 2);
        ST_REQUIRE_MSG(p.byteRecv > 0, "byteRecv on the lossy channel: expected > 0, got 0");
        ST_REQUIRE_MSG(p.byteRcvLoss == p.byteRecv,
                       "byteRcvLoss on a retransmitted channel: expected the received byte "
                       "count %llu, got %llu", (unsigned long long)p.byteRecv,
                       (unsigned long long)p.byteRcvLoss);
        ST_REQUIRE_MSG(p.byteRcvLossTotal > 0,
                       "byteRcvLossTotal with packets lost and retransmitted: expected > 0, got 0");
        srt_close(L.acc);
        L.acc = SRT_INVALID_SOCK;
    }
}

// ---------------------------------------------------------------------------
// g09_undecrypted -- packets that cannot be decrypted are counted, not passed.
//
// The channel is built with two different passphrases and enforcement off on
// both sides, which is the only shape that survives the handshake and lets the
// stream run: with enforcement on the same pair is refused and the counters
// never move. Measured at v1.5.0: 20 of 20 messages arrive as ciphertext and
// every undecrypt counter moves. The matched-passphrase leg beside it is the
// positive control -- same code path, no mismatching key, all counters zero.
// ---------------------------------------------------------------------------
struct UndecRes {
    int recvd = 0, same = 0, mism = 0;
    SRT_TRACEBSTATS p;
};

// Runs the sample and reads the counters while the connection is still open.
UndecRes undec_run(const std::string& send_pw, const std::string& rcv_pw) {
    UndecRes r;
    std::memset(&r.p, 0, sizeof r.p);

    // Enforcement first, passphrase second: see the file header.
    SockConf sc = [&](SRTSOCKET s) {
        st_live_lossless(s);
        st_set_int(s, SRTO_ENFORCEDENCRYPTION, 0);
        st_set_str(s, SRTO_PASSPHRASE, send_pw);
    };
    SockConf rc = [&](SRTSOCKET s) {
        st_live_lossless(s);
        st_set_int(s, SRTO_ENFORCEDENCRYPTION, 0);
        st_set_str(s, SRTO_PASSPHRASE, rcv_pw);
    };
    Link L;
    st_connect(L, RelaySched{}, sc, rc);

    st_set_int(L.acc, SRTO_RCVTIMEO, 400);
    std::vector<std::string> sent;
    for (int i = 0; i < 20; i++) sent.push_back(st_payload(i, 1316));
    std::atomic<bool> done(false);
    std::thread rd([&] {
        std::string buf(4096, '\0');
        while (!done.load()) {
            int rc = srt_recv(L.acc, &buf[0], (int)buf.size());
            if (rc <= 0) { if (recv_timed_out()) continue; break; }
            r.recvd++;
            std::string m = buf.substr(0, (size_t)rc);
            if (m == sent[(size_t)r.recvd - 1]) r.same++;
            else r.mism++;
        }
    });
    for (size_t i = 0; i < sent.size(); i++)
        ST_SRT_OK(srt_sendmsg(L.cal, sent[i].data(), (int)sent[i].size(), -1, 1));
    for (int i = 0; i < 60 && r.recvd < 20; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    r.p = stats_of(L.acc, 0);
    done.store(true);
    srt_close(L.cal);
    rd.join();
    srt_close(L.acc);
    L.acc = SRT_INVALID_SOCK;
    return r;
}

SRT_CASE(g09_undecrypted, ciphertext_is_counted_and_delivered) {
    const std::string good = "0123456789abcdef";
    const std::string bad  = "fedcba9876543210";

    // Positive control: the same channel with matching key material.
    UndecRes ctl = undec_run(good, good);
    ST_REQUIRE_MSG(ctl.recvd == 20,
                   "matched-passphrase received count: expected 20, got %d", ctl.recvd);
    ST_REQUIRE_MSG(ctl.same == 20,
                   "matched-passphrase messages matching what was sent: expected 20, got %d",
                   ctl.same);
    ST_REQUIRE_EQ_INT(ctl.mism, 0);
    ST_REQUIRE_MSG(ctl.p.pktRcvUndecrypt == 0,
                   "pktRcvUndecrypt with matching key material: expected 0, got %d",
                   (int)ctl.p.pktRcvUndecrypt);
    ST_REQUIRE_MSG(ctl.p.byteRcvUndecrypt == 0,
                   "byteRcvUndecrypt with matching key material: expected 0, got %llu",
                   (unsigned long long)ctl.p.byteRcvUndecrypt);
    ST_REQUIRE_MSG(ctl.p.pktRcvDrop == 0,
                   "pktRcvDrop with matching key material: expected 0, got %d",
                   (int)ctl.p.pktRcvDrop);
    ST_REQUIRE_MSG(ctl.p.byteRcvDropTotal == 0,
                   "byteRcvDropTotal with matching key material: expected 0, got %llu",
                   (unsigned long long)ctl.p.byteRcvDropTotal);

    // The observation: mismatched key material, enforcement off.
    UndecRes mis = undec_run(good, bad);
    ST_REQUIRE_MSG(mis.recvd == 20,
                   "mismatched-passphrase received count: expected 20, got %d", mis.recvd);
    ST_REQUIRE_MSG(mis.mism == 20,
                   "mismatched-passphrase messages differing from what was sent: expected 20, "
                   "got %d", mis.mism);
    ST_REQUIRE_EQ_INT(mis.same, 0);
    ST_REQUIRE_MSG(mis.p.pktRcvUndecrypt == 20,
                   "pktRcvUndecrypt on undecryptable packets: expected 20, got %d",
                   (int)mis.p.pktRcvUndecrypt);
    ST_REQUIRE_MSG(mis.p.byteRcvUndecrypt > 0,
                   "byteRcvUndecrypt on undecryptable packets: expected > 0, got 0");
    ST_REQUIRE_MSG(mis.p.pktRcvUndecryptTotal == 20,
                   "pktRcvUndecryptTotal on undecryptable packets: expected 20, got %d",
                   (int)mis.p.pktRcvUndecryptTotal);
    ST_REQUIRE_MSG(mis.p.byteRcvUndecryptTotal == mis.p.byteRcvUndecrypt,
                   "byteRcvUndecryptTotal against byteRcvUndecrypt on the same sample: expected "
                   "%llu, got %llu", (unsigned long long)mis.p.byteRcvUndecrypt,
                   (unsigned long long)mis.p.byteRcvUndecryptTotal);

    // The pair the undecrypted bytes are charged to, not to the loss counter.
    ST_REQUIRE_MSG(mis.p.pktRcvDrop == 20,
                   "pktRcvDrop on undecryptable packets: expected 20, got %d",
                   (int)mis.p.pktRcvDrop);
    ST_REQUIRE_MSG(mis.p.byteRcvDrop == 0,
                   "byteRcvDrop on undecryptable packets: expected 0, got %llu",
                   (unsigned long long)mis.p.byteRcvDrop);
    ST_REQUIRE_MSG(mis.p.byteRcvDropTotal > 0,
                   "byteRcvDropTotal on undecryptable packets: expected > 0, got 0");
}

// ---------------------------------------------------------------------------
// g10_recovery -- loss is retransmitted, and order survives it.
//
// The relay removes five forward data packets; the receiver must still be
// handed all two hundred messages, byte for byte, in the order they were sent.
// The relay's own count is asserted beside `pktRcvLoss` so the case cannot pass
// by losing nothing.
//
// The stream is paced 1 ms apart. Unpaced, the relay's own socket buffer
// overflows under load and loses packets the schedule never asked for --
// measured once at 124 of 200 -- which would make `pktRcvLoss == 5` a race.
// ---------------------------------------------------------------------------
SRT_CASE(g10_recovery, retransmission_is_ordered) {
    const int N = 200, SZ = 1316;

    RelaySched sched;
    for (int i = 40; i <= 44; i++) sched.drop_fwd.insert(i);

    Link L;
    st_connect(L, sched, live_conf(1200), live_conf(1200));

    auto sent = st_send_stream(L.cal, N, SZ, 1000);
    std::vector<std::string> got;
    st_set_int(L.acc, SRTO_RCVTIMEO, 400);
    std::string buf(4096, '\0');
    auto t0 = std::chrono::steady_clock::now();
    while ((int)got.size() < N &&
           std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count() < 15000) {
        int rc = srt_recv(L.acc, &buf[0], (int)buf.size());
        if (rc > 0) { got.push_back(buf.substr(0, (size_t)rc)); continue; }
        if (!recv_timed_out()) break;
    }
    SRT_TRACEBSTATS p = stats_of(L.acc, 0);

    ST_REQUIRE_EQ_INT(L.relay->dropped_fwd(), 5);
    ST_REQUIRE_MSG(got.size() == (size_t)N,
                   "messages received after retransmission: expected %d, got %zu",
                   N, got.size());
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) == -1,
                   "delivery order over a channel with retransmission: first out-of-order "
                   "index %d, expected -1", st_first_out_of_order(sent, got));
    ST_REQUIRE_MSG(p.pktRcvLoss == 5,
                   "pktRcvLoss against the relay's five forward drops: expected 5, got %d",
                   (int)p.pktRcvLoss);
    ST_REQUIRE_MSG(p.pktRcvRetrans > 0,
                   "pktRcvRetrans on a channel that had to retransmit: expected > 0, got 0");
    srt_close(L.acc);
    L.acc = SRT_INVALID_SOCK;
}

// ---------------------------------------------------------------------------
// g11_tsbpd -- the latency the receiver reports is the one the handshake
// agreed.
//
// This milestone REPLACES the receiver buffer (`CRcvBufferNew`,
// `ENABLE_NEW_RCVBUFFER`), and TSBPD delivery is that buffer's job. A rewrite
// whose counters are all correct and which honours no negotiated latency is a
// failure nothing else in this step would see: g08 and g09 read counters, g10
// reads order, g12 reads key material.
//
// What is asserted is the negotiation, not a hold. `srt-docs`
// (API/API-socket-options.md, SRTO_RCVLATENCY) defines the receiver buffering
// delay `Ln` as "the maximum of the SRTO_RCVLATENCY value and the value of
// SRTO_PEERLATENCY set by the peer", determined during the handshake, and says
// that reading SRTO_RCVLATENCY on a connected socket gives that `Ln`; reading
// SRTO_PEERLATENCY on the sender reports "the effective receiver buffering
// latency of the peer", the same number from the other end. Both legs below
// make the two sides ask for different values and then read the result from
// each socket, so a tree that simply echoes back whatever was set -- the
// cheapest way to pass a one-sided latency check -- fails on whichever side's
// own request lost the maximum. The unconfigured direction is read before the
// connect and asserted unchanged after it, so a single global latency also
// fails.
//
// No clock, deliberately. RQ-09-11 also asks that no message be delivered
// before the negotiated latency has elapsed and that a packet later than the
// budget be dropped and counted; the first is a floor on elapsed time and the
// second needs a timed relay delay, and this package deleted that shape
// package-wide as nondeterministic. Those two clauses are recorded as out of
// scope in the milestone contract rather than asserted here with a sleep --
// round 1 finding F7, REVIEW_LOG.md.
// ---------------------------------------------------------------------------

// The two halves of a latency pair, set one at a time. `SRTO_LATENCY` is
// deliberately not used in this group: it writes BOTH halves, which is exactly
// the distinction the legs below are about. TSBPD is on because a socket with
// TSBPD off has no receiver buffering delay to negotiate, and too-late drop is
// off so that nothing here depends on when a packet arrives.
SockConf g11_rcv_conf(int rcv_ms, int* pre_peer) {
    return [rcv_ms, pre_peer](SRTSOCKET s) {
        st_set_int(s, SRTO_TSBPDMODE, 1);
        st_set_int(s, SRTO_TLPKTDROP, 0);
        st_set_int(s, SRTO_RCVLATENCY, rcv_ms);
        *pre_peer = st_get_int(s, SRTO_PEERLATENCY);
    };
}

SockConf g11_snd_conf(int peer_ms, int* pre_rcv) {
    return [peer_ms, pre_rcv](SRTSOCKET s) {
        st_set_int(s, SRTO_TSBPDMODE, 1);
        st_set_int(s, SRTO_TLPKTDROP, 0);
        st_set_int(s, SRTO_PEERLATENCY, peer_ms);
        *pre_rcv = st_get_int(s, SRTO_RCVLATENCY);
    };
}

// The sender asks for more than the receiver does, so the receiver must end up
// reporting the sender's number and not its own.
SRT_CASE(g11_tsbpd, the_larger_peer_value_becomes_the_receivers_latency) {
    const int kRcvAsks = 120, kPeerAsks = 400;
    int lsn_pre_peer = -1, cal_pre_rcv = -1;

    Link L;
    // The listener is the receiving end in this kit: `L.acc` is the socket it
    // accepted and `L.cal` is the sender.
    st_connect(L, RelaySched{}, g11_rcv_conf(kRcvAsks, &lsn_pre_peer),
               g11_snd_conf(kPeerAsks, &cal_pre_rcv));

    const int rcv_side = st_get_int(L.acc, SRTO_RCVLATENCY);
    const int snd_side = st_get_int(L.cal, SRTO_PEERLATENCY);
    const int reverse  = st_get_int(L.cal, SRTO_RCVLATENCY);

    ST_REQUIRE_MSG(rcv_side == kPeerAsks,
                   "SRTO_RCVLATENCY on the connected receiver: expected the negotiated %d ms "
                   "(the receiver asked for %d, the peer's SRTO_PEERLATENCY asked for %d, and "
                   "the negotiated value is the larger), got %d",
                   kPeerAsks, kRcvAsks, kPeerAsks, rcv_side);
    ST_REQUIRE_MSG(snd_side == kPeerAsks,
                   "SRTO_PEERLATENCY on the connected sender reports the peer's effective "
                   "receiver latency: expected the same negotiated %d ms the receiver reports, "
                   "got %d",
                   kPeerAsks, snd_side);
    ST_REQUIRE_MSG(reverse == cal_pre_rcv,
                   "SRTO_RCVLATENCY on the sender, whose own receive direction nothing in this "
                   "case configured: expected the %d ms it read before the connect, got %d -- "
                   "the negotiation is per direction, not one latency for the link",
                   cal_pre_rcv, reverse);
}

// The mirror image: the receiver asks for more than the sender does, so its own
// number survives and the sender learns it.
SRT_CASE(g11_tsbpd, the_receivers_own_value_survives_a_smaller_peer_value) {
    const int kRcvAsks = 500, kPeerAsks = 80;
    int lsn_pre_peer = -1, cal_pre_rcv = -1;

    Link L;
    st_connect(L, RelaySched{}, g11_rcv_conf(kRcvAsks, &lsn_pre_peer),
               g11_snd_conf(kPeerAsks, &cal_pre_rcv));

    const int rcv_side = st_get_int(L.acc, SRTO_RCVLATENCY);
    const int snd_side = st_get_int(L.cal, SRTO_PEERLATENCY);

    ST_REQUIRE_MSG(rcv_side == kRcvAsks,
                   "SRTO_RCVLATENCY on the connected receiver: expected its own %d ms (the "
                   "peer asked for the smaller %d, and the negotiated value is the larger), "
                   "got %d",
                   kRcvAsks, kPeerAsks, rcv_side);
    ST_REQUIRE_MSG(snd_side == kRcvAsks,
                   "SRTO_PEERLATENCY on the connected sender after the handshake: expected the "
                   "receiver's %d ms, got %d -- the sender must learn the negotiated value, not "
                   "keep the %d it asked for",
                   kRcvAsks, snd_side, kPeerAsks);
}

// The reported number belongs to a connection that is actually delivering: a
// socket that reports the negotiated latency and hands nothing to the
// application would pass the two legs above.
SRT_CASE(g11_tsbpd, the_negotiated_latency_is_reported_by_a_delivering_socket) {
    const int kRcvAsks = 120, kPeerAsks = 400, N = 20, SZ = 1000;
    int lsn_pre_peer = -1, cal_pre_rcv = -1;

    Link L;
    st_connect(L, RelaySched{}, g11_rcv_conf(kRcvAsks, &lsn_pre_peer),
               g11_snd_conf(kPeerAsks, &cal_pre_rcv));

    std::vector<std::string> sent = st_send_stream(L.cal, N, SZ, 1000);
    // Bounded by the receiver's own deliveries, never by a sleep: the budget is
    // an upper bound that a healthy link never reaches, not a measurement.
    std::vector<std::string> got = read_until(L.acc, N, 400, 15000);
    const int during = st_get_int(L.acc, SRTO_RCVLATENCY);

    ST_REQUIRE_MSG(got.size() == (size_t)N,
                   "messages delivered over a link negotiated to %d ms: expected %d, got %zu",
                   kPeerAsks, N, got.size());
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) == -1,
                   "delivery order under TSBPD at the negotiated latency: first out-of-order "
                   "index %d, expected -1", st_first_out_of_order(sent, got));
    ST_REQUIRE_MSG(during == kPeerAsks,
                   "SRTO_RCVLATENCY on the receiver after it delivered %d messages: expected "
                   "the negotiated %d ms, got %d",
                   N, kPeerAsks, during);
}

// ---------------------------------------------------------------------------
// g12_encryption -- key material is exchanged, and a wrong key is refused.
//
// Both key lengths the option documents (16 and 32 bytes) are carried through
// the relay, with each side reporting that its key material reached the secured
// state. The negative leg is the one the listener enforces: a caller with a
// different passphrase must be rejected, with the documented reason and no
// accepted connection. It is built without an acceptor thread -- `srt_accept`
// blocks for its whole timeout on a handshake refused at key exchange -- and
// the listener socket is closed by `Link` without ever being accepted from.
// ---------------------------------------------------------------------------
SRT_CASE(g12_encryption, key_material_end_to_end) {
    for (int i = 0; i < 2; i++) {
        const size_t pwlen = i ? 32 : 16;
        const std::string pw(pwlen, 'x');
        SockConf conf = [&](SRTSOCKET s) {
            st_live_lossless(s);
            st_set_str(s, SRTO_PASSPHRASE, pw);
            st_set_int(s, SRTO_PBKEYLEN, (int)pwlen);
        };
        Link L;
        st_connect(L, RelaySched{}, conf, conf);
        auto sent = st_send_stream(L.cal, 20, 1316, 0);
        auto got = read_until(L.acc, 20, 400, 8000);

        ST_REQUIRE_MSG(got.size() == sent.size(),
                       "encrypted transfer at a %zu-byte key: expected %zu messages, got %zu",
                       pwlen, sent.size(), got.size());
        ST_REQUIRE_MSG(st_first_out_of_order(sent, got) == -1,
                       "encrypted transfer at a %zu-byte key: first out-of-order index %d, "
                       "expected -1", pwlen, st_first_out_of_order(sent, got));
        ST_REQUIRE_MSG(st_get_int(L.acc, SRTO_KMSTATE) == SRT_KM_S_SECURED,
                       "receiver key material state at a %zu-byte key: expected SRT_KM_S_SECURED "
                       "(%d), got %d", pwlen, (int)SRT_KM_S_SECURED,
                       st_get_int(L.acc, SRTO_KMSTATE));
        ST_REQUIRE_MSG(st_get_int(L.cal, SRTO_KMSTATE) == SRT_KM_S_SECURED,
                       "sender key material state at a %zu-byte key: expected SRT_KM_S_SECURED "
                       "(%d), got %d", pwlen, (int)SRT_KM_S_SECURED,
                       st_get_int(L.cal, SRTO_KMSTATE));
        srt_close(L.acc);
        L.acc = SRT_INVALID_SOCK;
    }

    // A wrong passphrase against a listener that enforces encryption. Built by
    // hand rather than through `st_try_connect`: that helper accepts on its own
    // thread, and `srt_accept` blocks for its whole timeout when the handshake
    // is refused at key exchange -- measured at 60 s, one case per run, which is
    // the case budget. Here nothing is accepted from, so nothing can block.
    {
        SRTSOCKET lsn = st_new_socket();
        ST_REQUIRE_MSG(lsn != SRT_INVALID_SOCK, "srt_socket (listener): %s",
                       srt_getlasterror_str());
        st_live_lossless(lsn);
        st_set_int(lsn, SRTO_ENFORCEDENCRYPTION, 1);
        st_set_str(lsn, SRTO_PASSPHRASE, "0123456789abcdef");
        sockaddr_in la = st_loopback(0);
        ST_SRT_OK(srt_bind(lsn, (sockaddr*)&la, sizeof la));
        int nl = (int)sizeof la;
        ST_SRT_OK(srt_getsockname(lsn, (sockaddr*)&la, &nl));
        ST_SRT_OK(srt_listen(lsn, 1));
        Relay relay(ntohs(la.sin_port), RelaySched{});

        SRTSOCKET cal = st_new_socket();
        ST_REQUIRE_MSG(cal != SRT_INVALID_SOCK, "srt_socket (caller): %s",
                       srt_getlasterror_str());
        st_live_lossless(cal);
        st_set_int(cal, SRTO_ENFORCEDENCRYPTION, 1);
        st_set_str(cal, SRTO_PASSPHRASE, "fedcba9876543210");

        sockaddr_in ra = st_loopback(relay.port());
        int rc = srt_connect(cal, (sockaddr*)&ra, sizeof ra);
        int err = srt_getlasterror(nullptr);
        int rej = srt_getrejectreason(cal);

        ST_REQUIRE_MSG(rc == SRT_ERROR,
                       "srt_connect with a mismatched passphrase against an enforcing listener: "
                       "expected SRT_ERROR (%d), got %d", (int)SRT_ERROR, rc);
        ST_REQUIRE_MSG(err == SRT_ECONNREJ,
                       "error code for the refused handshake: expected SRT_ECONNREJ (%d), got %d "
                       "(%s)", (int)SRT_ECONNREJ, err, srt_strerror(err, 0));
        ST_REQUIRE_MSG(rej == SRT_REJ_BADSECRET,
                       "rejection reason for the mismatched passphrase: expected "
                       "SRT_REJ_BADSECRET (%d), got %d", (int)SRT_REJ_BADSECRET, rej);

        srt_close(cal);
        srt_close(lsn);
    }
}

}  // namespace
