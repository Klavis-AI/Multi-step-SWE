// Deterministic lossy UDP relay: a black-box channel between two SRT endpoints.
//
// Upstream exposes no injectable clock and no injectable channel, and the
// tests use no real network, so loss/reorder/duplication/delay are produced here,
// on loopback, by dropping datagrams at fixed INDEXES rather than by
// probability. The same schedule drops the same datagrams on every run.
//
// The relay gives identical verdicts at both ends of the release chain (v1.2.0
// and v1.5.7), with srt_bstats().pktRcvLoss non-zero in every drop scenario
// and zero in the clean one -- a built-in positive control that the relay
// really dropped and the library really recovered.
//
// Rules for callers:
//   1. Never drain with a fixed sleep. Bound every receive with SRTO_RCVTIMEO
//      and drive the test from the receiver.
//   2. Live mode drops late packets ON PURPOSE. A group that wants lossless
//      recovery must set SRTO_TLPKTDROP=0 and a generous SRTO_LATENCY (both
//      exist at every tag in this chain). A group that wants to observe the
//      drop itself must leave the defaults alone.
//   3. Delay and reordering are queued, never slept for, in the forwarding
//      path. Sleeping in the poll loop would let one 20 ms delay serialise
//      the whole channel and lose most of the datagrams, which would read as
//      packet loss rather than as a harness defect. Held datagrams wait in a
//      release-time queue while the loop keeps forwarding everything else.
//   4. The forward schedule indexes DATA packets only. Control packets --
//      handshakes, ACKs, ACKACKs, NAKs, keep-alives, KM messages -- are always
//      forwarded and never consume an index. They have to be: how many of them
//      cross in a given second depends on timing, so an index space that
//      counted them would name a different data packet on every run, and
//      `drop_fwd = {60,61,62,63}` would drop four datagrams of which an
//      unpredictable number were data, and `pktRcvLossTotal` would differ
//      between runs of one unchanged case. UDT/SRT puts the packet class
//      in the top bit of the first header word: 0 data, 1 control.
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>

inline int udp_socket_bound(uint16_t port, uint16_t* out_port) {
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) throw std::runtime_error("socket");
    sockaddr_in a; std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(port);
    if (::bind(s, (sockaddr*)&a, sizeof a) != 0) { ::close(s); throw std::runtime_error("bind"); }
    socklen_t l = sizeof a;
    if (::getsockname(s, (sockaddr*)&a, &l) != 0) { ::close(s); throw std::runtime_error("getsockname"); }
    if (out_port) *out_port = ntohs(a.sin_port);
    return s;
}

inline uint16_t pick_port() {
    uint16_t p = 0; int s = udp_socket_bound(0, &p); ::close(s); return p;
}

struct RelaySched {
    std::set<int> drop_fwd;      // 0-based DATA packet index, client -> server
    std::set<int> drop_bwd;      // 0-based datagram index, server -> client.
                                 // Not data-only: the backward path of a live
                                 // stream is almost entirely control, and a
                                 // caller dropping here is dropping ACKs on
                                 // purpose.
    int drop_every_fwd = 0;      // additionally drop every n-th forward data packet
    int dup_fwd = -1;            // duplicate this forward data packet index
    int delay_fwd_ms = 0;        // one-way delay added to every forward datagram
    int delay_bwd_ms = 0;        // one-way delay added to every backward datagram
    std::set<int> hold_fwd;      // forward data indexes released `hold_ms` late, so they
                                 // arrive after their successors: reordering, not loss
    int hold_ms = 0;
};

class Relay {
public:
    Relay(uint16_t backend_port, const RelaySched& s) : sched_(s) {
        front_ = udp_socket_bound(0, &front_port_);
        back_  = udp_socket_bound(0, nullptr);
        std::memset(&backend_, 0, sizeof backend_);
        backend_.sin_family = AF_INET;
        backend_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        backend_.sin_port = htons(backend_port);
        run_ = true;
        th_ = std::thread([this]{ loop(); });
    }
    ~Relay() { run_ = false; if (th_.joinable()) th_.join(); ::close(front_); ::close(back_); }
    uint16_t port() const { return front_port_; }
    // Data packets only, both of them: `dropped_fwd()` is exactly how many SRT
    // data packets the schedule removed, so a case may compare it against
    // `pktRcvLossTotal` rather than against a bound.
    int dropped_fwd() const { return dropped_fwd_; }
    int seen_fwd() const { return seen_fwd_; }
    int control_fwd() const { return control_fwd_; }
private:
    // A datagram waiting for its release time. Queued rather than slept for:
    // the loop must keep draining both sockets while anything is held.
    struct Pending {
        bool to_backend;
        sockaddr_in dest;
        std::string bytes;
    };

    // UDT/SRT header word 0, bit 31: 0 = data, 1 = control. A datagram shorter
    // than a header cannot be either and is forwarded untouched.
    static bool is_control(const char* b, size_t n) {
        if (n < 4) return true;
        uint32_t w0;
        std::memcpy(&w0, b, 4);
        return (ntohl(w0) & 0x80000000u) != 0;
    }

    void send_now(const Pending& q) {
        ::sendto(q.to_backend ? back_ : front_, q.bytes.data(), q.bytes.size(), 0,
                 (const sockaddr*)&q.dest, sizeof q.dest);
    }

    void forward(bool to_backend, const sockaddr_in& dest, const char* b, size_t n,
                 int delay_ms) {
        Pending q{to_backend, dest, std::string(b, n)};
        if (delay_ms <= 0) { send_now(q); return; }
        queue_.emplace(std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(delay_ms),
                       std::move(q));
    }

    void loop() {
        char buf[2048];
        sockaddr_in from; socklen_t fl;
        bool have_client = false; sockaddr_in client{};
        while (run_) {
            // Wake early enough to release whatever is due next, but never
            // block long enough to miss the run_ flag.
            int timeout_ms = 20;
            if (!queue_.empty()) {
                auto due = std::chrono::duration_cast<std::chrono::milliseconds>(
                               queue_.begin()->first - std::chrono::steady_clock::now())
                               .count();
                if (due < 0) due = 0;
                if (due < timeout_ms) timeout_ms = (int)due;
            }
            pollfd p[2] = {{front_, POLLIN, 0}, {back_, POLLIN, 0}};
            int ready = ::poll(p, 2, timeout_ms);
            if (ready > 0 && (p[0].revents & POLLIN)) {
                fl = sizeof from;
                ssize_t n = ::recvfrom(front_, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
                if (n > 0) {
                    client = from; have_client = true;
                    if (is_control(buf, (size_t)n)) {
                        // Never dropped, never indexed. Losing a handshake or
                        // an ACKACK is a different experiment from losing
                        // payload, and mixing the two makes the schedule
                        // depend on timing.
                        control_fwd_++;
                        forward(true, backend_, buf, (size_t)n, sched_.delay_fwd_ms);
                    } else {
                    int idx = seen_fwd_++;
                    bool drop = sched_.drop_fwd.count(idx) ||
                                (sched_.drop_every_fwd > 0 && idx % sched_.drop_every_fwd == 0 && idx > 0);
                    if (drop) { dropped_fwd_++; }
                    else {
                        int d = sched_.delay_fwd_ms;
                        if (sched_.hold_fwd.count(idx)) d += sched_.hold_ms;
                        forward(true, backend_, buf, (size_t)n, d);
                        if (idx == sched_.dup_fwd)
                            forward(true, backend_, buf, (size_t)n, d);
                    }
                    }
                }
            }
            if (ready > 0 && (p[1].revents & POLLIN)) {
                fl = sizeof from;
                ssize_t n = ::recvfrom(back_, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
                if (n > 0 && have_client) {
                    int idx = seen_bwd_++;
                    if (!sched_.drop_bwd.count(idx))
                        forward(false, client, buf, (size_t)n, sched_.delay_bwd_ms);
                }
            }
            auto now = std::chrono::steady_clock::now();
            while (!queue_.empty() && queue_.begin()->first <= now) {
                send_now(queue_.begin()->second);
                queue_.erase(queue_.begin());
            }
        }
        // Anything still held when the relay is torn down is discarded: the
        // caller has already stopped reading by then.
    }

    int front_ = -1, back_ = -1;
    uint16_t front_port_ = 0;
    sockaddr_in backend_{};
    RelaySched sched_;
    std::atomic<bool> run_{false};
    std::thread th_;
    std::multimap<std::chrono::steady_clock::time_point, Pending> queue_;
    std::atomic<int> seen_fwd_{0}, seen_bwd_{0}, dropped_fwd_{0}, control_fwd_{0};
};
