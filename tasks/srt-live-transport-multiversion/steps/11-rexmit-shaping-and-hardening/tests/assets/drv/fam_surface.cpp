// Step 11 (v1.5.7) -- `surface` family.
//
// Two groups, both `new`: g11_constants_compile and g12_maxrexmitbw. The gate
// lives in `session`, not here.
//
// This is the ONLY family at this step that names identifiers absent from the
// previous milestone's tree -- SRT_KM_S_E_SIZE, SYSSOCKET_INVALID and
// SRTO_MAXREXMITBW -- which is exactly why driver-notes.md puts the two groups
// that need them in one file and nothing else with them. There is therefore
// deliberately NO dlsym, NO `#ifndef` restatement and NO SRT_VERSION_VALUE
// static_assert guard in this file: the three names are used directly. Against
// the v1.5.2 prefix this translation unit does not compile, and that is the
// intended outcome for an all-`new` family. A `surface` driver that still built
// and passed at v1.5.2 would be a driver that discriminates nothing.
//
// SRTO_MAXREXMITBW sits inside `#ifdef ENABLE_MAXREXMITBW` in the shipped
// `<srt/srt.h>`, and SRTO_CRYPTOMODE inside `#ifdef ENABLE_AEAD_API_PREVIEW`.
// Both flags reach the library through `add_definitions`, which lands in
// neither `srt.pc` nor any generated header, so meta.env's
// DRV_DEFS (-DENABLE_AEAD_API_PREVIEW -DENABLE_MAXREXMITBW) puts them on this
// file's own compile line. Without them this file does not compile at v1.5.7
// either, and the failure looks like a missing capability rather than a missing
// `-D`.
//
// ---------------------------------------------------------------- measured
//
// Everything asserted below was measured against the v1.5.7 prefix before a
// case was written, and agrees with interface.md and with the shipped
// `srt-docs/`:
//
//   * SRTO_MAXREXMITBW is 63; SRTO_CRYPTOMODE is 62; SRTO_E_SIZE is 64 under
//     both defines. SRT_KM_S_E_SIZE is 6 and SRT_KM_S_BADCRYPTOMODE is 5, both
//     outside any `#ifdef` in this milestone's header. SYSSOCKET_INVALID is -1.
//   * get of SRTO_MAXREXMITBW on a fresh socket: rc 0, 8 bytes written, value
//     -1, and the getter reports 8 as the written length.
//   * set of -1, 0 and 1000000 as eight-byte values: rc 0, each reads back.
//   * set of -2 (and of any value below -1): SRT_ERROR / SRT_EINVPARAM (5003),
//     stored value untouched.
//   * set with optlen 4 or 16: SRT_ERROR / 5003, stored value untouched.
//     `cast_optval<int64_t>` refuses any optlen > 0 that is not 8.
//   * get with optlen 4 or 7: SRT_ERROR / 5003 -- the getter refuses a buffer
//     below 8 rather than truncating -- and leaves *optlen as it found it.
//   * the option is in `srt_post_opt_list` and carries no restriction flag, so
//     a set on a connected caller or on an accepted socket succeeds and reads
//     back on that socket alone.
//   * SRTO_KMSTATE reads SRT_KM_S_UNSECURED (0) on a fresh socket and on both
//     ends of a plain link, and SRT_KM_S_SECURED (2) on both ends of a link
//     with one matching passphrase.
//   * srt_epoll_wait with a registered system socket and nothing ready returns
//     SRT_ERROR / SRT_ETIMEOUT (6003) and writes no lrfds slot; with a datagram
//     waiting it returns 1, sets *lrnum to 1, writes the real fd into slot 0
//     and leaves slot 1 as the caller left it.
//
// Two things interface.md states that this file does NOT assert, reported to
// the orchestrator rather than scored:
//
//   * That SRT_KM_S_BADCRYPTOMODE is declared UNCONDITIONALLY here (it was
//     inside `#ifdef ENABLE_AEAD_API_PREVIEW` at v1.5.2) is not observable from
//     a translation unit that is itself compiled with
//     -DENABLE_AEAD_API_PREVIEW, and DRV_DEFS requires that it is. The header
//     is included once, so it cannot be re-included with the macro undefined.
//     What is pinned below is its VALUE, 5, which is all g11's scored.txt row
//     asks for and all that interface.md promises the driver checks.
//   * SRTO_E_SIZE == 64 is true here, but interface.md explicitly says no group
//     asserts it because it scores the configuration rather than the behaviour.
//     Nothing below turns on it.
//
// Nothing here sleeps as a synchronisation device; every receive is bounded by
// SRTO_RCVTIMEO and every epoll wait by its own msTimeOut.
#include "srtkit.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

// ------------------------------------------------- g11: the compile-time pins
//
// These sit at file scope on purpose. A static_assert is a property of the
// translation unit, not of a case, so a header that is missing any one of the
// three identifiers -- or that gives it a different value -- fails the compile
// of the whole family and costs BOTH surface groups their point. scored.txt and
// interface.md both say so, and it is why these two groups share a file and
// nothing else shares it with them.
//
// The values are the ones interface.md states and the ones this milestone's
// installed header carries: the SRT_KM_STATE ladder is UNSECURED 0, SECURING 1,
// SECURED 2, NOSECRET 3, BADSECRET 4, BADCRYPTOMODE 5, E_SIZE 6. E_SIZE has no
// explicit `= 6` in the header, so it is pinned twice: once against the literal
// and once against BADCRYPTOMODE + 1, which is what makes it terminal rather
// than merely sixth.
static_assert(SRT_KM_S_UNSECURED == 0, "SRT_KM_S_UNSECURED disagrees with <srt/srt.h>");
static_assert(SRT_KM_S_SECURING == 1, "SRT_KM_S_SECURING disagrees with <srt/srt.h>");
static_assert(SRT_KM_S_SECURED == 2, "SRT_KM_S_SECURED disagrees with <srt/srt.h>");
static_assert(SRT_KM_S_NOSECRET == 3, "SRT_KM_S_NOSECRET disagrees with <srt/srt.h>");
static_assert(SRT_KM_S_BADSECRET == 4, "SRT_KM_S_BADSECRET disagrees with <srt/srt.h>");
static_assert(SRT_KM_S_BADCRYPTOMODE == 5, "SRT_KM_S_BADCRYPTOMODE is not 5");
static_assert(SRT_KM_S_E_SIZE == 6, "SRT_KM_S_E_SIZE is not 6");
static_assert(SRT_KM_S_E_SIZE == SRT_KM_S_BADCRYPTOMODE + 1,
              "SRT_KM_S_E_SIZE is not the terminal member of SRT_KM_STATE");

// "an enumerator of SRT_KM_STATE", not a macro and not a free constant: a
// header that supplied either would satisfy the value pins above and fail here.
static_assert(std::is_same<decltype(SRT_KM_S_E_SIZE), SRT_KM_STATE>::value,
              "SRT_KM_S_E_SIZE is not an enumerator of SRT_KM_STATE");
static_assert(std::is_same<decltype(SRT_KM_S_BADCRYPTOMODE), SRT_KM_STATE>::value,
              "SRT_KM_S_BADCRYPTOMODE is not an enumerator of SRT_KM_STATE");

// "an integral constant equal to -1", usable in a constant expression -- which
// this static_assert is itself the proof of.
static_assert(std::is_integral<decltype(SYSSOCKET_INVALID)>::value,
              "SYSSOCKET_INVALID is not an integral constant");
static_assert(SYSSOCKET_INVALID == -1, "SYSSOCKET_INVALID is not -1");
static_assert(static_cast<SYSSOCKET>(SYSSOCKET_INVALID) == static_cast<SYSSOCKET>(-1),
              "SYSSOCKET_INVALID does not round-trip through SYSSOCKET");

// ---------------------------------------------------------------- fixtures

// Inside SRTO_PASSPHRASE's documented 10..79 byte range, and the same on both
// ends of the encrypted link: the point of that link is SRT_KM_S_SECURED, not
// a mismatch.
static const char* const PW = "k^e#y!m4t3r";

// Live-mode default payload, so no size option has to be pinned to move a
// message across.
static const int MSG = 1316;

// The number of members SRT_KM_STATE has including the terminal one.
static const int KM_LADDER = 7;

// ---------------------------------------------------------------- helpers
//
// srtkit.h has int-width option access only; SRTO_MAXREXMITBW is an int64_t.
// These four are the int64 equivalents and live here rather than in drv/,
// which may not be edited because the other two families depend on it.

// Non-throwing: the width and value cases ARE the failures, so they need the
// raw return code and the error number, not an assertion.
static int mrb_try_set(SRTSOCKET s, int64_t v, int optlen) {
    return srt_setsockflag(s, SRTO_MAXREXMITBW, &v, optlen);
}

// `optlen` is in/out, as srt_getsockflag defines it; the caller keeps it so a
// refused get can be shown to have left it alone.
static int mrb_try_get(SRTSOCKET s, int64_t* v, int* optlen) {
    return srt_getsockflag(s, SRTO_MAXREXMITBW, v, optlen);
}

// Throwing eight-byte get, asserting the documented written length of 8.
static int64_t mrb_get(SRTSOCKET s, const char* where) {
    int64_t v = 0x5A5A5A5A5A5A5A5ALL;  // a value the option can never hold
    int n = (int)sizeof v;
    ST_REQUIRE_MSG(mrb_try_get(s, &v, &n) != SRT_ERROR, "%s: get of SRTO_MAXREXMITBW failed: %s",
                   where, srt_getlasterror_str());
    ST_REQUIRE_MSG(n == 8, "%s: get of SRTO_MAXREXMITBW wrote %d bytes, expected 8", where, n);
    return v;
}

// Throwing eight-byte set.
static void mrb_set(SRTSOCKET s, int64_t v, const char* where) {
    ST_REQUIRE_MSG(mrb_try_set(s, v, 8) != SRT_ERROR, "%s: set of SRTO_MAXREXMITBW to %lld: %s",
                   where, (long long)v, srt_getlasterror_str());
}

static int last_err() { return srt_getlasterror(nullptr); }

// SRTO_MAXBW is the other int64_t bandwidth ceiling and its default is also -1,
// so it is the option a driver that addressed the wrong enumerator would most
// plausibly be hitting. Reading it is the anti-aliasing control.
static int64_t maxbw_get(SRTSOCKET s) {
    int64_t v = 0;
    int n = (int)sizeof v;
    ST_REQUIRE_MSG(srt_getsockflag(s, SRTO_MAXBW, &v, &n) != SRT_ERROR,
                   "get of SRTO_MAXBW failed: %s", srt_getlasterror_str());
    return v;
}

static void conf_passphrase(SRTSOCKET s) { st_set_str(s, SRTO_PASSPHRASE, PW); }

static const char* km_name(int v) {
    switch (v) {
        case SRT_KM_S_UNSECURED: return "SRT_KM_S_UNSECURED";
        case SRT_KM_S_SECURING: return "SRT_KM_S_SECURING";
        case SRT_KM_S_SECURED: return "SRT_KM_S_SECURED";
        case SRT_KM_S_NOSECRET: return "SRT_KM_S_NOSECRET";
        case SRT_KM_S_BADSECRET: return "SRT_KM_S_BADSECRET";
        case SRT_KM_S_BADCRYPTOMODE: return "SRT_KM_S_BADCRYPTOMODE";
        case SRT_KM_S_E_SIZE: return "SRT_KM_S_E_SIZE (not a state)";
        default: return "outside SRT_KM_STATE";
    }
}

// ====================================================================
// g11_constants_compile
//
// The discrimination this group carries is at compile time: the three
// identifiers do not exist in the previous milestone's tree, so the
// static_asserts above cannot even be reached there. The two cases below are
// what keeps the group from being a vacuous pass at this milestone -- each puts
// one of the constants to the use it exists for and asserts an observation that
// can come out wrong.
// ====================================================================

// SRT_KM_S_E_SIZE is the terminal size member of SRT_KM_STATE, so its job is to
// bound the states the library can actually report. The case reads SRTO_KMSTATE
// off four live sockets and one fresh one and checks every answer lands inside
// the pinned ladder -- with a plain link and an encrypted link side by side, so
// the reading is demonstrably not a constant. The encrypted link is the
// positive control for the plain one: without it, a library whose SRTO_KMSTATE
// always answered 0 would satisfy every other assertion here.
SRT_CASE(g11_constants_compile, km_state_ladder) {
    // Read through an array, so the ladder is walked at run time rather than
    // folded away into the file-scope static_asserts.
    const SRT_KM_STATE ladder[KM_LADDER] = {
        SRT_KM_S_UNSECURED, SRT_KM_S_SECURING,      SRT_KM_S_SECURED, SRT_KM_S_NOSECRET,
        SRT_KM_S_BADSECRET, SRT_KM_S_BADCRYPTOMODE, SRT_KM_S_E_SIZE};
    for (int i = 0; i < KM_LADDER; i++)
        ST_REQUIRE_MSG((int)ladder[i] == i, "SRT_KM_STATE member #%d: expected %d, got %d", i, i,
                       (int)ladder[i]);
    ST_REQUIRE_EQ_INT((int)SRT_KM_S_E_SIZE, 6);
    ST_REQUIRE_EQ_INT((int)SRT_KM_S_BADCRYPTOMODE, 5);
    ST_REQUIRE_EQ_INT((int)SYSSOCKET_INVALID, -1);

    SRTSOCKET fresh = st_new_socket();
    ST_REQUIRE_MSG(fresh != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    const int fresh_km = st_get_int(fresh, SRTO_KMSTATE);
    srt_close(fresh);

    Link plain;
    st_connect(plain, RelaySched());
    const int plain_cal = st_get_int(plain.cal, SRTO_KMSTATE);
    const int plain_acc = st_get_int(plain.acc, SRTO_KMSTATE);

    Link enc;
    st_connect(enc, RelaySched(), conf_passphrase, conf_passphrase);
    const int enc_cal = st_get_int(enc.cal, SRTO_KMSTATE);
    const int enc_acc = st_get_int(enc.acc, SRTO_KMSTATE);

    // Every state the library reports is a member of the pinned enumeration and
    // strictly below the terminal member. That is what SRT_KM_S_E_SIZE is for.
    const int seen[5] = {fresh_km, plain_cal, plain_acc, enc_cal, enc_acc};
    const char* where[5] = {"fresh socket", "plain caller", "plain accepted", "encrypted caller",
                            "encrypted accepted"};
    for (int i = 0; i < 5; i++)
        ST_REQUIRE_MSG(seen[i] >= 0 && seen[i] < (int)SRT_KM_S_E_SIZE,
                       "SRTO_KMSTATE on %s is %d (%s), outside [0, SRT_KM_S_E_SIZE=%d)", where[i],
                       seen[i], km_name(seen[i]), (int)SRT_KM_S_E_SIZE);

    ST_REQUIRE_MSG(fresh_km == SRT_KM_S_UNSECURED,
                   "SRTO_KMSTATE on a fresh socket: expected SRT_KM_S_UNSECURED (%d), got %d (%s)",
                   (int)SRT_KM_S_UNSECURED, fresh_km, km_name(fresh_km));
    ST_REQUIRE_MSG(plain_cal == SRT_KM_S_UNSECURED && plain_acc == SRT_KM_S_UNSECURED,
                   "SRTO_KMSTATE on an unencrypted link: expected SRT_KM_S_UNSECURED (%d) on both "
                   "ends, got caller %d (%s) and accepted %d (%s)",
                   (int)SRT_KM_S_UNSECURED, plain_cal, km_name(plain_cal), plain_acc,
                   km_name(plain_acc));

    // The positive control: a second, different member of the same enumeration
    // is genuinely reachable, so the ladder is being read and not assumed.
    ST_REQUIRE_MSG(enc_cal == SRT_KM_S_SECURED && enc_acc == SRT_KM_S_SECURED,
                   "SRTO_KMSTATE on a link with one matching passphrase: expected "
                   "SRT_KM_S_SECURED (%d) on both ends, got caller %d (%s) and accepted %d (%s)",
                   (int)SRT_KM_S_SECURED, enc_cal, km_name(enc_cal), enc_acc, km_name(enc_acc));
    ST_REQUIRE_MSG(enc_cal != plain_cal,
                   "the encrypted and the plain link report the same SRTO_KMSTATE (%d); the getter "
                   "is not distinguishing them",
                   enc_cal);
}

// SYSSOCKET_INVALID is the name this milestone gives the value a SYSSOCKET can
// never be, so its use is as a sentinel: prefill an lrfds array with it and
// whatever is still equal to it afterwards was not written. The case runs the
// same epoll id twice -- once with nothing ready, once with a datagram waiting
// -- so the sentinel is shown both surviving and being overwritten. Either half
// alone would pass against a library that never reported anything.
SRT_CASE(g11_constants_compile, syssocket_invalid_sentinel) {
    ST_REQUIRE_EQ_INT((int)SYSSOCKET_INVALID, -1);

    const int eid = srt_epoll_create();
    ST_REQUIRE_MSG(eid >= 0, "srt_epoll_create: %s", srt_getlasterror_str());

    uint16_t port = 0;
    const int udp = udp_socket_bound(0, &port);
    int ev = SRT_EPOLL_IN;
    ST_REQUIRE_MSG(srt_epoll_add_ssock(eid, udp, &ev) != SRT_ERROR, "srt_epoll_add_ssock: %s",
                   srt_getlasterror_str());

    // A valid descriptor is never the sentinel: that is the whole premise of
    // using it as one.
    ST_REQUIRE_MSG((SYSSOCKET)udp != (SYSSOCKET)SYSSOCKET_INVALID,
                   "the bound UDP socket is %d, which equals SYSSOCKET_INVALID", udp);

    // Negative control: nothing has been sent, so the wait must time out and
    // must leave both sentinel slots as it found them.
    SYSSOCKET lr[2] = {SYSSOCKET_INVALID, SYSSOCKET_INVALID};
    int lrn = 2;
    int rc = srt_epoll_wait(eid, nullptr, nullptr, nullptr, nullptr, 300, lr, &lrn, nullptr,
                            nullptr);
    const int idle_err = last_err();
    ST_REQUIRE_MSG(rc == SRT_ERROR, "srt_epoll_wait with nothing ready returned %d, expected "
                                    "SRT_ERROR",
                   rc);
    ST_REQUIRE_MSG(idle_err == SRT_ETIMEOUT,
                   "srt_epoll_wait with nothing ready reported %d, expected SRT_ETIMEOUT (%d)",
                   idle_err, (int)SRT_ETIMEOUT);
    ST_REQUIRE_MSG((int)lr[0] == (int)SYSSOCKET_INVALID && (int)lr[1] == (int)SYSSOCKET_INVALID,
                   "a timed-out srt_epoll_wait wrote lrfds: expected both slots still "
                   "SYSSOCKET_INVALID (%d), got %d and %d",
                   (int)SYSSOCKET_INVALID, (int)lr[0], (int)lr[1]);

    // Positive control: one datagram waiting, so slot 0 must carry the real
    // descriptor and slot 1 must still carry the sentinel.
    uint16_t sport = 0;
    const int peer = udp_socket_bound(0, &sport);
    sockaddr_in to = st_loopback(port);
    const char probe[4] = {'p', 'i', 'n', 'g'};
    ST_REQUIRE_MSG(::sendto(peer, probe, sizeof probe, 0, (sockaddr*)&to, sizeof to) ==
                       (ssize_t)sizeof probe,
                   "sendto on the loopback UDP pair failed");

    lr[0] = SYSSOCKET_INVALID;
    lr[1] = SYSSOCKET_INVALID;
    lrn = 2;
    rc = srt_epoll_wait(eid, nullptr, nullptr, nullptr, nullptr, 3000, lr, &lrn, nullptr, nullptr);
    ST_REQUIRE_MSG(rc == 1, "srt_epoll_wait with one ready system socket returned %d (%s), "
                            "expected 1",
                   rc, rc == SRT_ERROR ? srt_getlasterror_str() : "no error");
    ST_REQUIRE_MSG(lrn == 1, "srt_epoll_wait reported %d ready system sockets, expected 1", lrn);
    ST_REQUIRE_MSG((int)lr[0] != (int)SYSSOCKET_INVALID,
                   "srt_epoll_wait left lrfds[0] at SYSSOCKET_INVALID (%d) although it reported "
                   "one ready system socket",
                   (int)SYSSOCKET_INVALID);
    ST_REQUIRE_MSG((int)lr[0] == udp, "srt_epoll_wait reported system socket %d, expected %d",
                   (int)lr[0], udp);
    ST_REQUIRE_MSG((int)lr[1] == (int)SYSSOCKET_INVALID,
                   "srt_epoll_wait wrote past the %d entries it reported: lrfds[1] is %d, expected "
                   "SYSSOCKET_INVALID (%d)",
                   lrn, (int)lr[1], (int)SYSSOCKET_INVALID);

    ::close(peer);
    srt_epoll_remove_ssock(eid, udp);
    ::close(udp);
    srt_epoll_release(eid);
}

// ====================================================================
// g12_maxrexmitbw
//
// The option's contract, and nothing about the rate it actually achieves on a
// lossy link -- interface.md rules that out explicitly. Six claims, five cases.
// ====================================================================

// Claim 1: the getter on a fresh socket writes eight bytes holding -1 and
// reports 8 as the written length. The enumerator's own value is checked here
// rather than with a static_assert on purpose: a wrong number must cost g12 its
// point without also failing the compile and taking g11 down with it.
SRT_CASE(g12_maxrexmitbw, fresh_default_is_unlimited) {
    ST_REQUIRE_MSG((int)SRTO_MAXREXMITBW == 63,
                   "SRTO_MAXREXMITBW is %d, expected 63 (SRTO_CRYPTOMODE is %d)",
                   (int)SRTO_MAXREXMITBW, (int)SRTO_CRYPTOMODE);

    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    int64_t v = 0x5A5A5A5A5A5A5A5ALL;
    int n = (int)sizeof v;
    const int rc = mrb_try_get(s, &v, &n);
    ST_REQUIRE_MSG(rc != SRT_ERROR, "get of SRTO_MAXREXMITBW on a fresh socket failed with %d: %s",
                   last_err(), srt_getlasterror_str());
    ST_REQUIRE_MSG(n == 8, "get of SRTO_MAXREXMITBW reported %d bytes written, expected 8", n);
    ST_REQUIRE_MSG(v == -1,
                   "SRTO_MAXREXMITBW on a fresh socket is %lld, expected -1 (no ceiling)",
                   (long long)v);
    srt_close(s);
}

// Claim 2: -1, 0 and 1000000 as eight-byte values each succeed and read back as
// set. This is the positive control the two refusal cases lean on: without it a
// library whose setter refused everything would score them both. The
// SRTO_MAXBW readings are the anti-aliasing control -- it is the other int64_t
// bandwidth ceiling and its default is also -1, so it is what a driver aimed at
// the wrong enumerator would most plausibly be writing to.
SRT_CASE(g12_maxrexmitbw, set_roundtrip) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    const int64_t before_maxbw = maxbw_get(s);

    const int64_t vals[3] = {-1, 0, 1000000};
    for (int i = 0; i < 3; i++) {
        ST_REQUIRE_MSG(mrb_try_set(s, vals[i], 8) != SRT_ERROR,
                       "set of SRTO_MAXREXMITBW to %lld with optlen 8 was refused with %d: %s",
                       (long long)vals[i], last_err(), srt_getlasterror_str());
        const int64_t got = mrb_get(s, "after an accepted set");
        ST_REQUIRE_MSG(got == vals[i], "SRTO_MAXREXMITBW after setting %lld reads %lld",
                       (long long)vals[i], (long long)got);
    }

    const int64_t after_maxbw = maxbw_get(s);
    ST_REQUIRE_MSG(after_maxbw == before_maxbw,
                   "writing SRTO_MAXREXMITBW changed SRTO_MAXBW from %lld to %lld; the two options "
                   "are not distinct",
                   (long long)before_maxbw, (long long)after_maxbw);
    ST_REQUIRE_MSG(mrb_get(s, "final") == 1000000,
                   "SRTO_MAXREXMITBW did not hold the last value written");
    srt_close(s);
}

// Claim 3: a value below -1 is refused with SRT_EINVPARAM and leaves the stored
// value alone. -1 is set immediately afterwards and succeeds, which is what
// distinguishes "refused because the value is below the documented -1.. range"
// from "refused because the value is negative".
SRT_CASE(g12_maxrexmitbw, below_minus_one_refused) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    mrb_set(s, 1000000, "control before the refusals");

    const int64_t bad[2] = {-2, -1000000};
    for (int i = 0; i < 2; i++) {
        const int rc = mrb_try_set(s, bad[i], 8);
        const int err = last_err();
        ST_REQUIRE_MSG(rc == SRT_ERROR, "set of SRTO_MAXREXMITBW to %lld returned %d, expected "
                                        "SRT_ERROR",
                       (long long)bad[i], rc);
        ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                       "set of SRTO_MAXREXMITBW to %lld reported %d, expected SRT_EINVPARAM (%d)",
                       (long long)bad[i], err, (int)SRT_EINVPARAM);
        const int64_t held = mrb_get(s, "after a refused value");
        ST_REQUIRE_MSG(held == 1000000,
                       "a refused set of %lld changed SRTO_MAXREXMITBW to %lld; expected the "
                       "stored 1000000 to be untouched",
                       (long long)bad[i], (long long)held);
    }

    // The adjacent legal value: -1 is the bottom of the documented range and
    // must still be accepted.
    ST_REQUIRE_MSG(mrb_try_set(s, -1, 8) != SRT_ERROR,
                   "set of SRTO_MAXREXMITBW to -1 was refused with %d: %s", last_err(),
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(mrb_get(s, "after setting -1") == -1,
                   "SRTO_MAXREXMITBW does not read back -1 after -1 was accepted");
    srt_close(s);
}

// Claims 4 and 5: a set given any width other than eight bytes is refused with
// SRT_EINVPARAM, and a get given a buffer below eight bytes is refused rather
// than truncating. The eight-byte set and get either side of them are the
// positive controls, and the stored value is re-read after every refusal so
// that "refused" also means "changed nothing".
SRT_CASE(g12_maxrexmitbw, width_refused) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    mrb_set(s, 1000000, "control before the width refusals");
    ST_REQUIRE_MSG(mrb_get(s, "control") == 1000000, "the eight-byte control did not take");

    // Setter: optlen 4 is the width an `int` would arrive with and must not be
    // silently widened; optlen 16 is the same rule from the other side.
    const int widths[2] = {4, 16};
    for (int i = 0; i < 2; i++) {
        int64_t room[2] = {7, 7};  // 16 bytes, so no width under test can overrun
        const int rc = srt_setsockflag(s, SRTO_MAXREXMITBW, room, widths[i]);
        const int err = last_err();
        ST_REQUIRE_MSG(rc == SRT_ERROR,
                       "set of SRTO_MAXREXMITBW with optlen %d returned %d, expected SRT_ERROR",
                       widths[i], rc);
        ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                       "set of SRTO_MAXREXMITBW with optlen %d reported %d, expected "
                       "SRT_EINVPARAM (%d)",
                       widths[i], err, (int)SRT_EINVPARAM);
        const int64_t held = mrb_get(s, "after a refused width");
        ST_REQUIRE_MSG(held == 1000000,
                       "a set refused for optlen %d changed SRTO_MAXREXMITBW to %lld; expected the "
                       "stored 1000000 to be untouched",
                       widths[i], (long long)held);
    }

    // Getter: a buffer below eight bytes is refused, not truncated.
    const int short_widths[2] = {4, 7};
    for (int i = 0; i < 2; i++) {
        int64_t out = 0x5A5A5A5A5A5A5A5ALL;
        int n = short_widths[i];
        const int rc = mrb_try_get(s, &out, &n);
        const int err = last_err();
        ST_REQUIRE_MSG(rc == SRT_ERROR,
                       "get of SRTO_MAXREXMITBW into a %d-byte buffer returned %d, expected "
                       "SRT_ERROR (a truncated read would be worse than a refusal)",
                       short_widths[i], rc);
        ST_REQUIRE_MSG(err == SRT_EINVPARAM,
                       "get of SRTO_MAXREXMITBW into a %d-byte buffer reported %d, expected "
                       "SRT_EINVPARAM (%d)",
                       short_widths[i], err, (int)SRT_EINVPARAM);
    }

    // And the option is still readable at its documented width afterwards.
    ST_REQUIRE_MSG(mrb_get(s, "after the refused gets") == 1000000,
                   "SRTO_MAXREXMITBW is no longer readable at eight bytes after the refused gets");
    srt_close(s);
}

// Claim 6: the option is `post`-restricted, so a set on a connected socket
// succeeds and reads back on that socket. Both ends are exercised, each with a
// different value, so a library that stored the ceiling once per process rather
// than once per socket fails here. The three messages at the end are not a rate
// measurement -- they are there so "connected" means a link that still carries
// data after the ceiling was changed under it.
SRT_CASE(g12_maxrexmitbw, set_after_connect) {
    Link L;
    st_connect(L, RelaySched());

    ST_REQUIRE_MSG(mrb_get(L.cal, "connected caller before the set") == -1,
                   "SRTO_MAXREXMITBW on a connected caller does not start at -1");
    ST_REQUIRE_MSG(mrb_get(L.acc, "accepted socket before the set") == -1,
                   "SRTO_MAXREXMITBW on an accepted socket does not start at -1");

    ST_REQUIRE_MSG(mrb_try_set(L.cal, 2000000, 8) != SRT_ERROR,
                   "set of SRTO_MAXREXMITBW to 2000000 on a CONNECTED caller was refused with %d: "
                   "%s -- the option is documented `post`",
                   last_err(), srt_getlasterror_str());
    ST_REQUIRE_MSG(mrb_try_set(L.acc, 0, 8) != SRT_ERROR,
                   "set of SRTO_MAXREXMITBW to 0 on an ACCEPTED socket was refused with %d: %s",
                   last_err(), srt_getlasterror_str());

    const int64_t cal_v = mrb_get(L.cal, "connected caller after the set");
    const int64_t acc_v = mrb_get(L.acc, "accepted socket after the set");
    ST_REQUIRE_MSG(cal_v == 2000000,
                   "SRTO_MAXREXMITBW on the connected caller reads %lld, expected 2000000",
                   (long long)cal_v);
    ST_REQUIRE_MSG(acc_v == 0, "SRTO_MAXREXMITBW on the accepted socket reads %lld, expected 0",
                   (long long)acc_v);
    ST_REQUIRE_MSG(cal_v != acc_v,
                   "both ends read the same SRTO_MAXREXMITBW (%lld) after being set differently; "
                   "the ceiling is not per-socket",
                   (long long)cal_v);

    // The link is still a link. Clean relay, so nothing is retransmitted and
    // neither ceiling is engaged; this asserts delivery, never a rate.
    const std::vector<std::string> sent = st_send_stream(L.cal, 3, MSG, 2000);
    const std::vector<std::string> got = st_recv_stream(L.acc, 3, MSG, 4000);
    ST_REQUIRE_MSG(got.size() == sent.size(),
                   "after the post-connect set the link carried %zu of %zu messages",
                   got.size(), sent.size());
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) == -1,
                   "the messages that crossed after the post-connect set were reordered or "
                   "substituted at index %d",
                   st_first_out_of_order(sent, got));
    for (size_t i = 0; i < got.size(); i++)
        ST_REQUIRE_MSG(got[i] == sent[i], "message #%zu differs from what was sent", i);
}
