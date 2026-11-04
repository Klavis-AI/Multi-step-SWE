// Step 10 (v1.5.2) -- `crypto` family.
//
// Uniformly `new`. All three groups -- g01_cryptomode, g02_gcm_session,
// g03_badsecret -- score this milestone and nothing else, and the gate lives in
// the `live` family, not here. driver-notes.md settles the consequence: this
// file is the only one in the step that names SRTO_CRYPTOMODE (62),
// SRT_REJ_CRYPTO (17) and SRT_KM_S_BADCRYPTOMODE (5), all three of which are
// absent from the v1.5.0 tree, and it is never compiled against anything but
// this milestone. So there is deliberately NO dlsym, NO `#ifndef` restatement
// and NO static_assert here: the identifiers are named directly. Compiled
// against the previous prefix this translation unit does not build, and that is
// the intended outcome for an all-`new` family -- a driver that still built and
// passed at v1.5.0 would be a driver that measures nothing.
//
// The three identifiers sit behind `ENABLE_AEAD_API_PREVIEW` in the shipped
// `<srt/srt.h>`, which the library gets through `add_definitions` and which
// therefore reaches neither `srt.pc` nor any generated header. meta.env's
// DRV_DEFS puts the same `-D` on this file's own compile line; without it this
// file does not compile at v1.5.2 either.
//
// ---------------------------------------------------------------- measured
//
// Everything below was measured against the v1.5.2 prefix before a case was
// written, and every value agrees with the shipped `srt-docs/` negotiation
// matrix and with interface.md:
//
//   * a fresh socket reads SRTO_CRYPTOMODE as 0 and the getter reports a
//     length of 4 even when handed an 8-byte buffer, writing only the first
//     four bytes;
//   * 0, 1 and 2 are accepted pre-connect; -2, -1, 3 and 4 are refused with
//     SRT_EINVPARAM (5003) and leave the stored value untouched;
//   * with SRTO_TSBPDMODE switched off, 2 is refused with SRT_EINVPARAM at the
//     moment of the set, while 0 and 1 are still accepted;
//   * negotiated readback on a CONNECTED socket: caller 2 / listener 0 -> 2 on
//     both; caller 0 / listener 0 -> 1 on both; caller 0 / listener 1 -> 1.
//     The LISTENING socket keeps reporting what was configured on it, so every
//     readback below is taken from the accepted socket, never from the listener;
//   * AES-GCM with one matching passphrase connects at PBKEYLEN 16, 24 and 32,
//     reports SRT_KM_S_SECURED on the receiver and carries 1316-byte messages
//     intact;
//   * caller and listener naming different modes are refused in BOTH directions
//     and the CALLER reports SRT_REJ_CRYPTO (17) -- including when enforced
//     encryption is off on both sides, because the v1.5.2 rejection for a bad
//     crypto mode sits outside the enforced-encryption branch;
//   * different passphrases with the listener's enforced encryption off: the
//     caller's connect fails and srt_getrejectreason(caller) is
//     SRT_REJ_BADSECRET (10). One-sided passphrase, either direction, in the
//     same fixture: SRT_REJ_UNSECURE (11). So 10 and 11 are both reachable here
//     and g03's distinction is a real one.
//
// Two things interface.md states that this file does NOT score, both reported
// to the orchestrator rather than asserted:
//
//   * SRT_KM_S_BADCRYPTOMODE is not observable through any public getter. It is
//     set on the listener's half-built socket, which is then rejected and never
//     accepted, so no socket survives to be read. After a mode mismatch the
//     caller reads SRTO_KMSTATE 0, SRTO_SNDKMSTATE 1, SRTO_RCVKMSTATE 0 --
//     measured three times, identical. Its documented VALUE (5) is pinned in
//     g02's mismatch case, which is all that is reachable.
//   * SRTO_E_SIZE == 63 and SRT_REJ_E_SIZE == 18 under the preview define are
//     true here, but no group's one-line description in scored.txt mentions the
//     enumeration lengths, so nothing below turns on them.
//
// Timings are one-sided and generous everywhere; nothing here sleeps as a
// synchronisation device and every receive is bounded by SRTO_RCVTIMEO. No
// case asserts how long a rejected connect takes -- interface.md forbids it and
// the rejection is prompt at both milestones anyway.
#include "srtkit.h"

#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ------------------------------------------------------------- fixture data

// The passphrase pair from deltas/10.md's fixture E. Both are inside
// SRTO_PASSPHRASE's 10..79 byte range and differ only in the trailing byte, so
// a mismatch cannot be confused with a malformed passphrase.
static const char* const PW_A = "s!t@r#i$c^t";
static const char* const PW_B = "s!t@r#i$c^tu";

// The live-mode payload every transfer case uses. 1316 is SRTO_PAYLOADSIZE's
// own default, so a stream of this size crosses without the option having to be
// pinned on either side.
static const int MSG = 1316;

// ---------------------------------------------------------------- helpers
//
// Everything in this section is here because `srtkit.h` does not have it and
// `drv/` headers may not be edited (other families depend on them).

// SRTO_TSBPDMODE and SRTO_ENFORCEDENCRYPTION are declared `bool` options: their
// getters answer with ONE byte, and srtkit's st_get_int rejects a reply that is
// not four bytes wide. They are read here as what they are. Both are written
// with st_set_int, which the library accepts for a bool option.
static int bool_opt(SRTSOCKET s, SRT_SOCKOPT o) {
    char b = -1;
    int n = (int)sizeof b;
    ST_REQUIRE_MSG(srt_getsockflag(s, o, &b, &n) != SRT_ERROR, "getsockflag(%d) failed: %s", (int)o,
                   srt_getlasterror_str());
    return (int)b;
}

static void set_enforced(SRTSOCKET s, int v) {
    char b = (char)v;
    ST_REQUIRE_MSG(srt_setsockflag(s, SRTO_ENFORCEDENCRYPTION, &b, 1) != SRT_ERROR,
                   "setsockflag(SRTO_ENFORCEDENCRYPTION, %d): %s", v, srt_getlasterror_str());
}

static const char* reason_name(int r) {
    if (r == SRT_REJ_UNKNOWN) return "SRT_REJ_UNKNOWN";
    if (r == SRT_REJ_BADSECRET) return "SRT_REJ_BADSECRET";
    if (r == SRT_REJ_UNSECURE) return "SRT_REJ_UNSECURE";
    if (r == SRT_REJ_CRYPTO) return "SRT_REJ_CRYPTO";
    if (r == SRT_REJ_ROGUE) return "SRT_REJ_ROGUE";
    if (r == SRT_REJ_PEER) return "SRT_REJ_PEER";
    if (r == SRT_REJ_TIMEOUT) return "SRT_REJ_TIMEOUT";
    return "some other reason";
}

static int64_t ms_now() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Bring up listener -> relay -> caller where the handshake is expected to be
// REFUSED, and bound the wait.
//
// st_try_connect in srtkit.h accepts on its own thread with SRTO_RCVTIMEO set
// on the listener, which is right for a handshake that completes. It is not
// enough here: measured on this release, srt_accept does not honour
// SRTO_RCVTIMEO, so a refused handshake leaves that thread blocked for ever and
// the case dies on its alarm instead of reporting the refusal it exists to
// observe. The listener below is non-blocking and polled, which does bound it.
//
// The poll is cleanup, not an assertion: for a cipher-mode mismatch the
// listener rejects and never accepts anything, while for a relaxed-enforcement
// passphrase mismatch it does accept. No case turns on which happened, and no
// case reads the accepted socket here -- measured, it is not reliably in
// SRTS_CONNECTED by the time the caller's connect has returned.
static void connect_refused(Link& L, const SockConf& lconf, const SockConf& cconf,
                            int accept_poll_ms) {
    RelaySched sched;

    L.lsn = st_new_socket();
    ST_REQUIRE_MSG(L.lsn != SRT_INVALID_SOCK, "srt_socket (listener): %s", srt_getlasterror_str());
    // Non-blocking BEFORE the group's own configuration, so a SockConf cannot
    // put the listener back into blocking mode and silently undo the bound.
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
    ST_REQUIRE_MSG(L.cal != SRT_INVALID_SOCK, "srt_socket (caller): %s", srt_getlasterror_str());
    cconf(L.cal);

    sockaddr_in ra = st_loopback(L.relay->port());
    L.connect_rc = srt_connect(L.cal, (sockaddr*)&ra, sizeof ra);
    L.connect_err = srt_getlasterror(nullptr);

    const int64_t t0 = ms_now();
    for (;;) {
        SRTSOCKET a = srt_accept(L.lsn, nullptr, nullptr);
        if (a != SRT_INVALID_SOCK) {
            L.acc = a;
            break;
        }
        if (srt_getlasterror(nullptr) != SRT_EASYNCRCV) break;
        if (ms_now() - t0 > accept_poll_ms) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// The configuration every connected case starts from: live transport with a
// latency generous enough that the clock never decides what arrives, TSBPD on
// (which AES-GCM requires), and optionally a passphrase, a key length and a
// cipher mode. `mode < 0` leaves SRTO_CRYPTOMODE untouched at its default,
// which is a distinct case from setting it to 0.
static SockConf crypto_conf(int mode, const char* pw, int keylen) {
    std::string p = pw ? pw : std::string();
    return [mode, p, keylen](SRTSOCKET s) {
        st_live_lossless(s);
        if (!p.empty()) {
            st_set_str(s, SRTO_PASSPHRASE, p);
            if (keylen > 0) st_set_int(s, SRTO_PBKEYLEN, keylen);
        }
        if (mode >= 0) st_set_int(s, SRTO_CRYPTOMODE, mode);
    };
}

// The same, plus an explicit SRTO_ENFORCEDENCRYPTION. `enf < 0` leaves the
// option at its default, which measures 1 on this release and is what g03's
// caller must keep.
static SockConf pass_conf(const char* pw, int enf) {
    std::string p = pw ? pw : std::string();
    return [p, enf](SRTSOCKET s) {
        st_live_lossless(s);
        if (!p.empty()) st_set_str(s, SRTO_PASSPHRASE, p);
        if (enf >= 0) set_enforced(s, enf);
    };
}

// Sends `count` messages and requires every one of them back, byte-identical
// and in order. This is what keeps the encryption cases from passing on an
// empty channel: a session that delivers nothing, or delivers ciphertext,
// fails on the comparison rather than on the count alone.
static void require_stream_intact(Link& L, int count, const char* what) {
    std::vector<std::string> sent = st_send_stream(L.cal, count, MSG, 1500);
    std::vector<std::string> got = st_recv_stream(L.acc, count, MSG, 4000);
    ST_REQUIRE_MSG((int)got.size() == count, "%s: %d messages of %d bytes were sent but %d arrived",
                   what, count, MSG, (int)got.size());
    for (int i = 0; i < count; i++)
        ST_REQUIRE_MSG(got[(size_t)i] == sent[(size_t)i],
                       "%s: message #%d came back altered or out of order (%d bytes sent, %d "
                       "received, first difference at byte %d)",
                       what, i, (int)sent[(size_t)i].size(), (int)got[(size_t)i].size(),
                       (int)got[(size_t)i].compare(sent[(size_t)i]));
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) == -1,
                   "%s: the %d messages did not arrive in the order they were sent", what, count);
}

// ================================================================= g01
//
// RQ-10-01. The cipher-mode option itself: its default, its accepted range,
// how it refuses, the one configuration in which AES-GCM is not allowed, and
// what a CONNECTED socket reports. Nothing in this group transfers payload --
// that is g02's subject.

// interface.md, first paragraph: "The cipher-mode option is reached by its
// numeric position in SRT_SOCKOPT, and that position is 62. The drivers cast
// integers, so the numbering is part of the contract and no shipped document
// gives it." Asserted here rather than left implicit, so the claim the
// candidate is shown is one a group actually checks.
//
// The default and the width come from the same paragraph and from scope.md
// ("It reads 0 on a fresh socket"). The width is observed with a buffer WIDER
// than the answer, so "four bytes" is the library's report and not an echo of
// what was asked for: measured, the second four bytes are left untouched.
SRT_CASE(g01_cryptomode, default_range_and_numeric_position) {
    ST_REQUIRE_MSG((int)SRTO_CRYPTOMODE == 62,
                   "the cipher-mode option must sit at position 62 of SRT_SOCKOPT, but "
                   "SRTO_CRYPTOMODE is %d",
                   (int)SRTO_CRYPTOMODE);

    // --- reads 0 on a fresh socket, as a four-byte integer
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    struct {
        int32_t v;
        int32_t tail;
    } box;
    box.v = -7;
    box.tail = -7;
    int n = (int)sizeof box;
    ST_REQUIRE_MSG(srt_getsockflag(s, SRTO_CRYPTOMODE, &box, &n) != SRT_ERROR,
                   "the cipher-mode option must be readable on a fresh socket, but "
                   "srt_getsockflag failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(n == 4,
                   "the cipher-mode option is a four-byte integer, but the getter reported a "
                   "length of %d bytes",
                   n);
    ST_REQUIRE_MSG(box.tail == -7,
                   "the getter reported four bytes but wrote past them: the next four bytes of "
                   "the buffer changed from -7 to %d",
                   (int)box.tail);
    ST_REQUIRE_MSG(box.v == 0, "a fresh socket must report cipher mode 0 (automatic), not %d",
                   (int)box.v);

    // --- the option really is the one at position 62
    const SRT_SOCKOPT raw62 = (SRT_SOCKOPT)62;
    int two = 2;
    ST_REQUIRE_MSG(srt_setsockflag(s, raw62, &two, (int)sizeof two) != SRT_ERROR,
                   "setting option 62 to the AES-GCM value must be accepted on a fresh socket: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(st_get_int(s, SRTO_CRYPTOMODE) == 2,
                   "a value written through option number 62 must be the value the cipher-mode "
                   "option reads back, but it reads %d",
                   st_get_int(s, SRTO_CRYPTOMODE));
    srt_close(s);

    // --- 0, 1 and 2 are accepted before connect, and echo back
    for (int m = 0; m <= 2; m++) {
        SRTSOCKET t = st_new_socket();
        ST_REQUIRE_MSG(st_try_set_int(t, SRTO_CRYPTOMODE, m) != SRT_ERROR,
                       "cipher mode %d must be accepted before connect, but the set failed: %s", m,
                       srt_getlasterror_str());
        int back = st_get_int(t, SRTO_CRYPTOMODE);
        ST_REQUIRE_MSG(back == m,
                       "cipher mode %d was accepted before connect but reads back as %d", m, back);
        srt_close(t);
    }

    // --- -1 and 3 are refused with SRT_EINVPARAM, and change nothing
    const int bad[] = {-1, 3};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SRTSOCKET t = st_new_socket();
        // Park a legal value first, so the refusal can be shown to leave the
        // stored setting alone rather than merely to match the default.
        st_set_int(t, SRTO_CRYPTOMODE, 1);
        int rc = st_try_set_int(t, SRTO_CRYPTOMODE, bad[i]);
        int err = srt_getlasterror(nullptr);
        ST_REQUIRE_MSG(rc == SRT_ERROR,
                       "cipher mode %d is outside the accepted range and must be refused, but "
                       "srt_setsockflag returned %d",
                       bad[i], rc);
        ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                       "cipher mode %d must be refused with SRT_EINVPARAM (%d), but the error was "
                       "%d (%s)",
                       bad[i], (int)SRT_EINVPARAM, err, srt_getlasterror_str());
        int back = st_get_int(t, SRTO_CRYPTOMODE);
        ST_REQUIRE_MSG(back == 1,
                       "a refused set of cipher mode %d must leave the previous value in place, "
                       "but the option now reads %d instead of 1",
                       bad[i], back);
        srt_close(t);
    }
}

// interface.md: "Selecting AES-GCM (2) on a socket whose SRTO_TSBPDMODE is off
// is refused with SRT_EINVPARAM at the moment the option is set, not later at
// connect time."
//
// Three controls sit beside the refusal, because a library that refused every
// cipher-mode set, or that refused every set on a socket with TSBPD off, would
// otherwise score the point: 0 and 1 must still be accepted on that same
// socket, and 2 must be accepted on a socket that left TSBPD at its default.
SRT_CASE(g01_cryptomode, aes_gcm_is_refused_while_tsbpd_is_off) {
    SRTSOCKET off = st_new_socket();
    ST_REQUIRE_MSG(off != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    st_set_int(off, SRTO_TSBPDMODE, 0);
    ST_REQUIRE_MSG(bool_opt(off, SRTO_TSBPDMODE) == 0,
                   "the fixture needs timestamp-based delivery switched off, but the option reads "
                   "%d after being set to 0",
                   bool_opt(off, SRTO_TSBPDMODE));

    int rc = st_try_set_int(off, SRTO_CRYPTOMODE, 2);
    int err = srt_getlasterror(nullptr);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "AES-GCM (cipher mode 2) must be refused while timestamp-based delivery is "
                   "off, but srt_setsockflag returned %d and accepted it",
                   rc);
    ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                   "AES-GCM on a socket with timestamp-based delivery off must be refused with "
                   "SRT_EINVPARAM (%d), but the error was %d (%s)",
                   (int)SRT_EINVPARAM, err, srt_getlasterror_str());
    int back = st_get_int(off, SRTO_CRYPTOMODE);
    ST_REQUIRE_MSG(back == 0,
                   "the refused AES-GCM set must leave the cipher mode at 0, but it reads %d",
                   back);

    // Control 1 and 2: the other two values are still accepted on this socket,
    // so the refusal is about AES-GCM and not about the socket.
    for (int m = 0; m <= 1; m++) {
        ST_REQUIRE_MSG(st_try_set_int(off, SRTO_CRYPTOMODE, m) != SRT_ERROR,
                       "cipher mode %d must still be accepted while timestamp-based delivery is "
                       "off, but the set failed: %s",
                       m, srt_getlasterror_str());
        ST_REQUIRE_MSG(st_get_int(off, SRTO_CRYPTOMODE) == m,
                       "cipher mode %d was accepted with timestamp-based delivery off but reads "
                       "back as %d",
                       m, st_get_int(off, SRTO_CRYPTOMODE));
    }
    srt_close(off);

    // Control 3: on a socket that left timestamp-based delivery at its default,
    // the same value is accepted. Without this the case would pass against a
    // library that never accepts AES-GCM at all.
    SRTSOCKET on = st_new_socket();
    ST_REQUIRE_MSG(bool_opt(on, SRTO_TSBPDMODE) == 1,
                   "this control needs timestamp-based delivery on by default, but a fresh socket "
                   "reports %d",
                   bool_opt(on, SRTO_TSBPDMODE));
    ST_REQUIRE_MSG(st_try_set_int(on, SRTO_CRYPTOMODE, 2) != SRT_ERROR,
                   "AES-GCM must be accepted on a socket whose timestamp-based delivery is on, "
                   "but the set failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(st_get_int(on, SRTO_CRYPTOMODE) == 2,
                   "AES-GCM was accepted with timestamp-based delivery on but the option reads "
                   "back as %d",
                   st_get_int(on, SRTO_CRYPTOMODE));
    srt_close(on);
}

// scope.md: "Once a connection exists, reading the option back on either side
// reports the mode actually agreed." Three rows of the negotiation matrix in
// `srt-docs/API/API-socket-options.md`, chosen so that no constant answer can
// satisfy all three:
//
//   caller 0 / listener 0 -> 1   (both asked for automatic, neither gets 0)
//   caller 2 / listener 0 -> 2   (the listener asked for 0 and reports 2)
//   caller 0 / listener 1 -> 1   (the caller asked for 0 and reports 1)
//
// Read from the CALLER and from the ACCEPTED socket. The listening socket is
// deliberately not read: measured, it keeps reporting its own configured value,
// which is correct -- it is not the connected socket -- and srt-docs says the
// same about the socket seen by a listener callback.
SRT_CASE(g01_cryptomode, connected_sockets_report_the_agreed_mode) {
    struct Row {
        int lmode;
        int cmode;
        int agreed;
    };
    const Row rows[] = {{0, 0, 1}, {0, 2, 2}, {1, 0, 1}};

    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        const Row& r = rows[i];
        Link L;
        RelaySched sched;
        st_connect(L, sched, crypto_conf(r.lmode, PW_A, 0), crypto_conf(r.cmode, PW_A, 0));

        int on_caller = st_get_int(L.cal, SRTO_CRYPTOMODE);
        int on_accepted = st_get_int(L.acc, SRTO_CRYPTOMODE);
        ST_REQUIRE_MSG(on_caller == r.agreed,
                       "caller asked for cipher mode %d and listener for %d, so both connected "
                       "sockets must report the agreed mode %d; the caller reports %d",
                       r.cmode, r.lmode, r.agreed, on_caller);
        ST_REQUIRE_MSG(on_accepted == r.agreed,
                       "caller asked for cipher mode %d and listener for %d, so both connected "
                       "sockets must report the agreed mode %d; the accepted socket reports %d",
                       r.cmode, r.lmode, r.agreed, on_accepted);
        // The connection has to be a real, secured one for the readback to mean
        // anything: an unencrypted session negotiates no mode at all.
        //
        // This case is the one that otherwise moves no payload, so four
        // messages are sent here before the key-material state is read. The
        // read must come after traffic: see the note in g02 below and
        // TEST_INTERFACE.md section 5. Four messages are enough to drive the
        // exchange and cost about nothing against the case budget.
        char what[80];
        std::snprintf(what, sizeof what, "caller cipher mode %d against listener %d", r.cmode,
                      r.lmode);
        require_stream_intact(L, 4, what);
        ST_REQUIRE_MSG(st_get_int(L.acc, SRTO_KMSTATE) == SRT_KM_S_SECURED,
                       "caller %d / listener %d: the key material must be secured for a mode to "
                       "have been agreed, but the receiver reports key-material state %d instead "
                       "of %d after %d messages crossed",
                       r.cmode, r.lmode, st_get_int(L.acc, SRTO_KMSTATE), (int)SRT_KM_S_SECURED, 4);
    }
}

// ================================================================= g02
//
// RQ-10-02. The AES-GCM session: that it works at every key length, that a
// peer which expressed no preference adopts it from a caller that did, and
// that two peers naming different modes are refused with the crypto reason.

// scored.txt: "with AES-GCM selected on both sides and one matching passphrase
// the connection is established at each of the three key lengths and carries
// the payload intact while the receiver reports the secured key-material
// state."
//
// The three key lengths are SRTO_PBKEYLEN's documented values, 16, 24 and 32.
// Each session is torn down before the next begins, so an in-flight message
// from one key length cannot be counted against another.
SRT_CASE(g02_gcm_session, aes_gcm_carries_the_payload_at_every_key_length) {
    const int keylens[] = {16, 24, 32};
    for (size_t i = 0; i < sizeof keylens / sizeof keylens[0]; i++) {
        const int kl = keylens[i];
        Link L;
        RelaySched sched;
        st_connect(L, sched, crypto_conf(2, PW_A, kl), crypto_conf(2, PW_A, kl));

        ST_REQUIRE_MSG(st_get_int(L.cal, SRTO_CRYPTOMODE) == 2,
                       "key length %d: both sides selected AES-GCM, so the caller must report "
                       "cipher mode 2, not %d",
                       kl, st_get_int(L.cal, SRTO_CRYPTOMODE));
        ST_REQUIRE_MSG(st_get_int(L.acc, SRTO_CRYPTOMODE) == 2,
                       "key length %d: both sides selected AES-GCM, so the receiver must report "
                       "cipher mode 2, not %d",
                       kl, st_get_int(L.acc, SRTO_CRYPTOMODE));
        ST_REQUIRE_MSG(st_get_int(L.acc, SRTO_PBKEYLEN) == kl,
                       "the session was configured for a %d-byte key, but the receiver reports a "
                       "key length of %d",
                       kl, st_get_int(L.acc, SRTO_PBKEYLEN));

        char what[64];
        std::snprintf(what, sizeof what, "AES-GCM with a %d-byte key", kl);
        require_stream_intact(L, 8, what);

        // The key-material state is read only AFTER messages have crossed.
        // Immediately after srt_connect returns it may still read
        // SRT_KM_S_UNSECURED on a library whose key exchange settles with the
        // first traffic rather than in the handshake, and that is not something
        // this package promises against; TEST_INTERFACE.md section 5 states the
        // observation point. The assertion itself is unchanged.
        ST_REQUIRE_MSG(st_get_int(L.acc, SRTO_KMSTATE) == SRT_KM_S_SECURED,
                       "key length %d: the receiver must report the secured key-material state "
                       "(%d) after the stream crossed, but reports %d",
                       kl, (int)SRT_KM_S_SECURED, st_get_int(L.acc, SRTO_KMSTATE));
    }
}

// scored.txt: "a peer left at the automatic setting adopts AES-GCM from a
// caller that asked for it." srt-docs' matrix: caller AES-GCM (2) against a
// listener at Auto (0) negotiates AES-GCM (2).
//
// The control is the same fixture with the caller ALSO at the automatic
// setting: the agreed mode is then AES-CTR (1), which is the matrix's first
// row. Without it the case would pass against a library that simply always
// reports 2. Both sessions must also carry their payload, so a library that
// reports the right number over a dead channel scores nothing.
SRT_CASE(g02_gcm_session, an_automatic_peer_adopts_gcm_from_the_caller) {
    {
        Link L;
        RelaySched sched;
        st_connect(L, sched, crypto_conf(0, PW_A, 0), crypto_conf(2, PW_A, 0));
        ST_REQUIRE_MSG(st_get_int(L.acc, SRTO_CRYPTOMODE) == 2,
                       "a listener left at the automatic setting must adopt AES-GCM from a caller "
                       "that asked for it, so the accepted socket must report cipher mode 2, not "
                       "%d",
                       st_get_int(L.acc, SRTO_CRYPTOMODE));
        ST_REQUIRE_MSG(st_get_int(L.cal, SRTO_CRYPTOMODE) == 2,
                       "the caller asked for AES-GCM against an automatic listener, so it must "
                       "report cipher mode 2, not %d",
                       st_get_int(L.cal, SRTO_CRYPTOMODE));
        require_stream_intact(L, 8, "AES-GCM adopted by an automatic listener");
        // The key-material state is read only AFTER messages have crossed.
        // Immediately after srt_connect returns it may still read
        // SRT_KM_S_UNSECURED on a library whose key exchange settles with the
        // first traffic rather than in the handshake, and that is not something
        // this package promises against; TEST_INTERFACE.md section 5 states the
        // observation point. The assertion itself is unchanged.
        ST_REQUIRE_MSG(st_get_int(L.acc, SRTO_KMSTATE) == SRT_KM_S_SECURED,
                       "the adopted AES-GCM session must be secured after its stream crossed, but "
                       "the receiver reports key-material state %d instead of %d",
                       st_get_int(L.acc, SRTO_KMSTATE), (int)SRT_KM_S_SECURED);
    }
    {
        // Control: neither side expresses a preference, so AES-CTR results.
        Link L;
        RelaySched sched;
        st_connect(L, sched, crypto_conf(0, PW_A, 0), crypto_conf(0, PW_A, 0));
        ST_REQUIRE_MSG(st_get_int(L.acc, SRTO_CRYPTOMODE) == 1,
                       "when neither side names a cipher mode the agreed mode must be AES-CTR "
                       "(1), but the accepted socket reports %d -- so the adoption above was not "
                       "the caller's doing",
                       st_get_int(L.acc, SRTO_CRYPTOMODE));
        require_stream_intact(L, 8, "AES-CTR agreed by two automatic peers");
        // The key-material state is read only AFTER messages have crossed.
        // Immediately after srt_connect returns it may still read
        // SRT_KM_S_UNSECURED on a library whose key exchange settles with the
        // first traffic rather than in the handshake, and that is not something
        // this package promises against; TEST_INTERFACE.md section 5 states the
        // observation point. The assertion itself is unchanged.
        ST_REQUIRE_MSG(st_get_int(L.acc, SRTO_KMSTATE) == SRT_KM_S_SECURED,
                       "the AES-CTR control session must be secured after its stream crossed, but "
                       "the receiver reports key-material state %d instead of %d",
                       st_get_int(L.acc, SRTO_KMSTATE), (int)SRT_KM_S_SECURED);
    }
}

// scored.txt: "a caller and listener that name different cipher modes are
// refused with the crypto-mode rejection reason on the caller."
// interface.md: "The caller's srt_connect fails and srt_getrejectreason on the
// caller yields 17, for AES-GCM against an AES-CTR listener and for AES-CTR
// against an AES-GCM listener alike."
//
// Both directions are exercised. The positive control is the matching pair,
// which must connect with no rejection reason at all -- otherwise a library
// that refused every encrypted connection would score the point. No elapsed
// time is asserted: interface.md forbids it and the rejection is prompt at
// both milestones.
//
// SRT_KM_S_BADCRYPTOMODE is the key-material state behind this rejection, but
// it lives on the listener's half-built socket, which is rejected and never
// accepted, so no public getter reaches it. Its documented value is pinned
// here, which is as far as this release allows the state to be checked.
SRT_CASE(g02_gcm_session, mismatched_modes_are_refused_with_the_crypto_reason) {
    ST_REQUIRE_MSG((int)SRT_REJ_CRYPTO == 17,
                   "the crypto-mode rejection reason must be 17, but SRT_REJ_CRYPTO is %d",
                   (int)SRT_REJ_CRYPTO);
    ST_REQUIRE_MSG((int)SRT_KM_S_BADCRYPTOMODE == 5,
                   "the bad-crypto-mode key-material state behind that rejection must be 5, but "
                   "SRT_KM_S_BADCRYPTOMODE is %d",
                   (int)SRT_KM_S_BADCRYPTOMODE);

    struct Pair {
        int lmode;
        int cmode;
    };
    const Pair pairs[] = {{1, 2}, {2, 1}};
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        const Pair& p = pairs[i];
        Link L;
        connect_refused(L, crypto_conf(p.lmode, PW_A, 0), crypto_conf(p.cmode, PW_A, 0), 400);

        ST_REQUIRE_MSG(L.connect_rc == SRT_ERROR,
                       "caller asked for cipher mode %d against a listener at %d: the two cannot "
                       "agree, so srt_connect must fail, but it returned %d",
                       p.cmode, p.lmode, L.connect_rc);
        int reason = srt_getrejectreason(L.cal);
        ST_REQUIRE_MSG(reason == SRT_REJ_CRYPTO,
                       "caller asked for cipher mode %d against a listener at %d: the caller must "
                       "report SRT_REJ_CRYPTO (%d), but reports %d (%s)",
                       p.cmode, p.lmode, (int)SRT_REJ_CRYPTO, reason, reason_name(reason));
    }

    // Control: the same two modes, agreeing. This must connect and report no
    // rejection reason.
    {
        Link L;
        RelaySched sched;
        st_connect(L, sched, crypto_conf(2, PW_A, 0), crypto_conf(2, PW_A, 0));
        int reason = srt_getrejectreason(L.cal);
        ST_REQUIRE_MSG(reason == SRT_REJ_UNKNOWN,
                       "two peers that both named AES-GCM must connect with no rejection reason, "
                       "but the caller reports %d (%s)",
                       reason, reason_name(reason));
        require_stream_intact(L, 4, "two peers that both named AES-GCM");
    }
}

// ================================================================= g03
//
// RQ-10-03 / delta D6. The reason a caller reports when the two passphrases
// differ. At v1.5.0 the KMRSP branch sets SRT_REJ_UNSECURE unconditionally;
// at v1.5.2 it distinguishes the key-material state and reports
// SRT_REJ_BADSECRET. The listener must relax enforced encryption or it rejects
// first and the caller reports the listener's reason instead; the caller must
// keep its own enforcement at the default or it does not fail at all.

// scored.txt: "when the two sides hold different passphrases and the listener
// has enforced encryption switched off, the caller's rejection reason is the
// one that says the passphrases differ, not the one that says only one side
// set a passphrase."
//
// The positive control is the same fixture with the passphrases matching: the
// connection comes up, the receiver reports the secured key-material state and
// the payload crosses. Without it a library that refused every connection
// would score the point.
SRT_CASE(g03_badsecret, differing_passphrases_report_badsecret_on_the_caller) {
    // The caller's own enforcement must be at its default for the refusal to
    // be the caller's; assert the default rather than assume it.
    {
        SRTSOCKET probe = st_new_socket();
        ST_REQUIRE_MSG(bool_opt(probe, SRTO_ENFORCEDENCRYPTION) == 1,
                       "this group needs the caller's enforced encryption left at its default of "
                       "1, but a fresh socket reports %d",
                       bool_opt(probe, SRTO_ENFORCEDENCRYPTION));
        srt_close(probe);
    }

    Link L;
    connect_refused(L, pass_conf(PW_A, 0), pass_conf(PW_B, -1), 400);

    ST_REQUIRE_MSG(L.connect_rc == SRT_ERROR,
                   "the two sides hold different passphrases, so the caller's connect must fail, "
                   "but srt_connect returned %d",
                   L.connect_rc);
    int reason = srt_getrejectreason(L.cal);
    ST_REQUIRE_MSG(reason != SRT_REJ_UNSECURE,
                   "both sides set a passphrase and they differ, so the caller must not report "
                   "SRT_REJ_UNSECURE (%d), which means only one side set one",
                   (int)SRT_REJ_UNSECURE);
    ST_REQUIRE_MSG(reason == SRT_REJ_BADSECRET,
                   "with different passphrases and the listener's enforced encryption off, the "
                   "caller must report SRT_REJ_BADSECRET (%d), but reports %d (%s)",
                   (int)SRT_REJ_BADSECRET, reason, reason_name(reason));

    // Control: the identical fixture with one matching passphrase.
    Link OK;
    RelaySched sched;
    st_connect(OK, sched, pass_conf(PW_A, 0), pass_conf(PW_A, -1));
    int ok_reason = srt_getrejectreason(OK.cal);
    ST_REQUIRE_MSG(ok_reason == SRT_REJ_UNKNOWN,
                   "one matching passphrase in the same fixture must connect with no rejection "
                   "reason, but the caller reports %d (%s)",
                   ok_reason, reason_name(ok_reason));
    require_stream_intact(OK, 6, "one matching passphrase");
    // The key-material state is read only AFTER messages have crossed.
    // Immediately after srt_connect returns it may still read
    // SRT_KM_S_UNSECURED on a library whose key exchange settles with the
    // first traffic rather than in the handshake, and that is not something
    // this package promises against; TEST_INTERFACE.md section 5 states the
    // observation point. The assertion itself is unchanged.
    ST_REQUIRE_MSG(st_get_int(OK.acc, SRTO_KMSTATE) == SRT_KM_S_SECURED,
                   "one matching passphrase must leave the receiver in the secured key-material "
                   "state (%d) once its stream has crossed, but it reports %d",
                   (int)SRT_KM_S_SECURED, st_get_int(OK.acc, SRTO_KMSTATE));
}

// The other half of the distinction the group turns on. `srt-docs` defines
// SRT_REJ_UNSECURE as "Only one connection party has set up a password", so
// that configuration must still produce it -- in either direction. Without
// this case the group above would be satisfied by a library that had simply
// renamed 11 to 10, and the sentence "not the one that says only one side set
// a passphrase" would be asserting nothing.
SRT_CASE(g03_badsecret, one_sided_passphrase_still_reports_unsecure) {
    struct Side {
        const char* lpw;
        const char* cpw;
        const char* which;
    };
    const Side sides[] = {{nullptr, PW_B, "only the caller"}, {PW_A, nullptr, "only the listener"}};

    for (size_t i = 0; i < sizeof sides / sizeof sides[0]; i++) {
        const Side& s = sides[i];
        Link L;
        connect_refused(L, pass_conf(s.lpw, 0), pass_conf(s.cpw, -1), 400);

        ST_REQUIRE_MSG(L.connect_rc == SRT_ERROR,
                       "%s set a passphrase, so the caller's connect must fail, but srt_connect "
                       "returned %d",
                       s.which, L.connect_rc);
        int reason = srt_getrejectreason(L.cal);
        ST_REQUIRE_MSG(reason != SRT_REJ_BADSECRET,
                       "%s set a passphrase, so there are no two differing passphrases and the "
                       "caller must not report SRT_REJ_BADSECRET (%d)",
                       s.which, (int)SRT_REJ_BADSECRET);
        ST_REQUIRE_MSG(reason == SRT_REJ_UNSECURE,
                       "%s set a passphrase, so the caller must report SRT_REJ_UNSECURE (%d), but "
                       "reports %d (%s)",
                       s.which, (int)SRT_REJ_UNSECURE, reason, reason_name(reason));
    }
}
