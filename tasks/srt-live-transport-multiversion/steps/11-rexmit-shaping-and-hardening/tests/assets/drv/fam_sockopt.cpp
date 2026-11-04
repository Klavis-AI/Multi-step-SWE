// Family "sockopt", checkpoint 11 (SRT v1.5.7). Three scored groups, all `new`:
//
//   g01_optlen_guard           every option setter refuses a NEGATIVE optlen
//                              with SRT_EINVPARAM before it looks at the value,
//                              and the refused call is INERT; optlen 0 is still
//                              accepted for a numeric option ("trust the
//                              pointer's width"), and a string option set with
//                              its true length is accepted and reads back.
//   g02_version_readonly       SRTO_VERSION has no setter: a four-byte set is
//                              refused with SRT_EINVPARAM, while the GETTER on
//                              that same socket still succeeds, writes four
//                              bytes, and reports what srt_getversion() does.
//   g03_payloadsize_connected  SRTO_PAYLOADSIZE reports the CONFIGURED value on
//                              an unconnected socket (0 file-mode, 1316
//                              live-mode) and, on a CONNECTED file-mode socket
//                              whose configured value is 0, the largest payload
//                              one packet carries at the negotiated segment
//                              size -- MSS - 44 = 1456 at SRTO_MSS 1500 -- on
//                              the caller and on the accepted socket alike.
//
// ---------------------------------------------------------------------------
// Tier, and why there is no dlsym and no guarded restatement in this file.
//
// Every group here is `new` and SCORED (the gate lives in the `session` family, not this
// one; stepdefs/11/endtoend.txt column 1). There is no `inherited` and no
// `peripheral` group, so DRIVER_RULES' "must compile against the previous
// step's prefix" requirement does not apply and its dlsym / #ifndef-restate /
// static_assert machinery buys nothing: a `new` group that still compiles AND
// PASSES at v1.5.2 is a group that fails to discriminate, which is worse than
// one that does not build there.
//
// This family none the less DOES compile at v1.5.2, deliberately, because it
// names no identifier this milestone introduces. SRTO_MSS, SRTO_CONGESTION,
// SRTO_VERSION, SRTO_MINVERSION, SRTO_PAYLOADSIZE, SRTO_TRANSTYPE, SRTT_FILE
// and SRT_EINVPARAM all exist at v1.5.2; what changed is validation, dispatch
// and what the payload-size getter reports. So the discrimination this step is
// scored on is an ASSERTION failure at v1.5.2, not a build failure -- measured,
// and reported as such. Nothing here is behind ENABLE_MAXREXMITBW or
// ENABLE_AEAD_API_PREVIEW either, so the family is indifferent to DRV_DEFS.
//
// ---------------------------------------------------------------------------
// Traceability. Every assertion traces to a row of deltas/11.md and to the
// candidate-facing text of stepdefs/11/interface.md (TEST_INTERFACE.md §5):
//
//   g01  D1  (negative optlen on a numeric option) and D11 (negative optlen on
//            a string option). interface.md, "A negative `optlen` is refused by
//            every setter, and a zero `optlen` is not."
//   g02  D2. interface.md, "`SRTO_VERSION` has no setter, and a set is refused
//            as a bad parameter."
//   g03  D7. interface.md, "`SRTO_PAYLOADSIZE` is readable, and on a connected
//            file-mode socket it reports the per-packet maximum, not the
//            configured zero."
//
// Where interface.md corrects the pinned srt-docs/, this file follows the
// CORRECTION, because the brief makes a correction a correction to reality:
//
//   * SRTO_CONGESTION is marked `Dir: W` in API-socket-options.md, and the
//     getter none the less works: it copies the string, NUL-terminates it and
//     reports the length WITHOUT the NUL. g01 reads it through a 16-byte
//     buffer and expects length 4, exactly as interface.md states. Measured at
//     both prefixes: identical there, so the readback is a control, never the
//     discriminator.
//   * SRTO_PAYLOADSIZE is marked `W` and documented with a file-mode default of
//     0. Both milestones have a working getter; only what it reports on a
//     CONNECTED socket changes. g03 therefore asserts the unconnected values as
//     the control and the connected value as the subject.
//
// Every negative case asserts the exact refusal code through
// srt_getlasterror(), not merely SRT_ERROR: "it failed" and "it failed with
// SRT_EINVPARAM" are different claims and only the second is the requirement.
// Every negative case is paired with a positive control on the SAME socket and
// the SAME option, so a library that refuses everything scores nothing.
//
// ---------------------------------------------------------------------------
// Deliberately NOT asserted, and why:
//
//   * The setter's rejection of a non-zero optlen that is not sizeof(int)
//     (interface.md says it is "still refused ... as before"). It is refused
//     identically at v1.5.2 -- measured -- so it discriminates nothing, and
//     neither scored.txt line names it. The zero-optlen case already catches
//     the most likely wrong repair (refusing every optlen != sizeof(T)).
//   * A second negative optlen such as -8. interface.md states the rule as
//     "optlen < 0", but the scored description names -1, and a candidate is
//     scored against the description they are shown.
//   * srt_setsockopt (the level-taking spelling). The scored descriptions name
//     srt_setsockflag; both funnel into the same CUDT::setsockopt, so the
//     second spelling would add a call, not an observation.
//   * The getter's tolerance of *optlen == -1 on SRTO_MSS. interface.md says it
//     still succeeds and that no group asserts on it; deltas/11.md D1 says
//     outright "do not test the getter".
//   * SRTO_BINDTODEVICE, the other string option whose -1 handling was removed
//     (D11). It needs SRT_ENABLE_BINDTODEVICE and a real interface name;
//     SRTO_CONGESTION is the option the scored description names.
//
// No case here binds anything but an ephemeral loopback port, no case uses a
// sleep of any kind, and the one case that connects bounds its receive with
// SRTO_RCVTIMEO. g01 and g02 are pure option-layer operations on unbound
// sockets.
#include "stest.h"
#include "srtkit.h"

#include <cstdint>
#include <cstring>
#include <string>

// --------------------------------------------------------------- helpers
//
// These are local to this family and deliberately not in srtkit.h: the kit is
// compiled at every tag in the chain and may not name anything version
// specific, and srtkit.h's own st_set_int/st_get_int throw on the failure that
// IS the subject here. Nothing else in drv/ was changed for this family.

static SRTSOCKET new_sock() {
    const SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    return s;
}

// A set that must be REFUSED, and refused with SRT_EINVPARAM specifically --
// not SRT_EINVOP, not SRT_EBOUNDSOCK (interface.md names those two by name).
static void expect_refused_einval(SRTSOCKET s, SRT_SOCKOPT o, const void* val, int optlen,
                                  const char* what) {
    srt_clearlasterror();
    const int rc  = srt_setsockflag(s, o, val, optlen);
    const int err = srt_getlasterror(NULL);
    ST_REQUIRE_MSG(rc == SRT_ERROR,
                   "%s: srt_setsockflag(opt %d, optlen %d) returned %d, expected %d (SRT_ERROR)",
                   what, (int)o, optlen, rc, (int)SRT_ERROR);
    ST_REQUIRE_MSG(err == (int)SRT_EINVPARAM,
                   "%s: refusal code: expected %d (%s), got %d (%s)", what, (int)SRT_EINVPARAM,
                   srt_strerror((int)SRT_EINVPARAM, 0), err, srt_strerror(err, 0));
    srt_clearlasterror();
}

static void expect_set_ok(SRTSOCKET s, SRT_SOCKOPT o, const void* val, int optlen,
                          const char* what) {
    srt_clearlasterror();
    const int rc = srt_setsockflag(s, o, val, optlen);
    ST_REQUIRE_MSG(rc != SRT_ERROR, "%s: srt_setsockflag(opt %d, optlen %d) failed: %s (code %d)",
                   what, (int)o, optlen, srt_getlasterror_str(), srt_getlasterror(NULL));
}

// Reads an int-valued option and asserts the written length the getter reports,
// which is part of what every one of these groups claims.
static int read_int_opt(SRTSOCKET s, SRT_SOCKOPT o, const char* what) {
    int v = -424242;
    int n = (int)sizeof v;
    srt_clearlasterror();
    const int rc = srt_getsockflag(s, o, &v, &n);
    ST_REQUIRE_MSG(rc != SRT_ERROR, "%s: srt_getsockflag(opt %d) failed: %s", what, (int)o,
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(n == (int)sizeof(int), "%s: srt_getsockflag(opt %d) wrote %d bytes, expected %d",
                   what, (int)o, n, (int)sizeof(int));
    return v;
}

// SRTO_CONGESTION through a 16-byte buffer, as interface.md specifies. The
// getter reports the string's length WITHOUT the NUL it also writes, so the
// reported length is checked to be inside the buffer before it is believed.
static std::string read_congestion(SRTSOCKET s, int* out_len, const char* what) {
    char buf[16];
    std::memset(buf, 0, sizeof buf);
    int n = (int)sizeof buf;
    srt_clearlasterror();
    const int rc = srt_getsockflag(s, SRTO_CONGESTION, buf, &n);
    ST_REQUIRE_MSG(rc != SRT_ERROR, "%s: srt_getsockflag(SRTO_CONGESTION) failed: %s", what,
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(n >= 0 && n < (int)sizeof buf,
                   "%s: srt_getsockflag(SRTO_CONGESTION) reported %d bytes for a %d-byte buffer",
                   what, n, (int)sizeof buf);
    *out_len = n;
    return std::string(buf, (size_t)n);
}

// =========================================================== g01_optlen_guard
//
// D1 + D11. At v1.5.2 CUDT::setsockopt checked only `!optval`, and
// cast_optval(optval, optlen) rejects only `optlen > 0 && optlen != sizeof(T)`,
// so a negative length slipped through and was applied; the string setters read
// optlen == -1 as "NUL-terminated". At v1.5.7 the API entry refuses optlen < 0
// outright, for every option, before dispatch.

// The negative length is refused, the refusal changes nothing, and the SAME
// value at the SAME option with a well-formed length is accepted -- so the
// length is demonstrably the only reason for the refusal.
SRT_CASE(g01_optlen_guard, c1_mss_negative_optlen_refused_and_inert) {
    const SRTSOCKET s = new_sock();

    const int before = read_int_opt(s, SRTO_MSS, "SRTO_MSS before any set");
    int target = before - 100;   // a legal MSS the option does not currently hold

    expect_refused_einval(s, SRTO_MSS, &target, -1, "SRTO_MSS with optlen -1");

    const int after = read_int_opt(s, SRTO_MSS, "SRTO_MSS after the refused optlen -1 set");
    ST_REQUIRE_MSG(after == before,
                   "the refused SRTO_MSS set was not inert: expected %d (unchanged), got %d "
                   "(the offered value was %d)",
                   before, after, target);

    // Positive control, same socket, same option, same value.
    expect_set_ok(s, SRTO_MSS, &target, (int)sizeof target, "SRTO_MSS with optlen 4");
    const int accepted = read_int_opt(s, SRTO_MSS, "SRTO_MSS after the accepted optlen 4 set");
    ST_REQUIRE_MSG(accepted == target, "SRTO_MSS after the accepted optlen 4 set: expected %d, got %d",
                   target, accepted);

    srt_close(s);
}

// optlen 0 is NOT refused: for a numeric option it means "trust the pointer's
// width". The guard below is what stops this case passing vacuously if some
// future default happened to be 1400 already.
SRT_CASE(g01_optlen_guard, c2_mss_zero_optlen_accepted) {
    const SRTSOCKET s = new_sock();

    const int before = read_int_opt(s, SRTO_MSS, "SRTO_MSS before the optlen 0 set");
    int v = 1400;
    ST_REQUIRE_MSG(v != before,
                   "SRTO_MSS already holds %d, so setting %d could not be observed", before, v);

    expect_set_ok(s, SRTO_MSS, &v, 0, "SRTO_MSS with optlen 0");
    const int got = read_int_opt(s, SRTO_MSS, "SRTO_MSS after the optlen 0 set");
    ST_REQUIRE_MSG(got == v, "SRTO_MSS after the optlen 0 set: expected %d, got %d", v, got);

    srt_close(s);
}

// The same rule on a STRING option, where v1.5.2 read -1 as "NUL-terminated"
// and returned success. The readback through a 16-byte buffer is the inertness
// half; the accepted set of the same string with its true length is the control.
SRT_CASE(g01_optlen_guard, c3_congestion_negative_optlen_refused_and_inert) {
    const SRTSOCKET s = new_sock();

    int n0 = -1;
    const std::string before = read_congestion(s, &n0, "SRTO_CONGESTION on a fresh socket");
    ST_REQUIRE_MSG(before == "live" && n0 == 4,
                   "SRTO_CONGESTION on a fresh socket: expected \"live\" with length 4, "
                   "got \"%s\" with length %d",
                   before.c_str(), n0);

    expect_refused_einval(s, SRTO_CONGESTION, "live", -1, "SRTO_CONGESTION \"live\" with optlen -1");

    int n1 = -1;
    const std::string after = read_congestion(s, &n1, "SRTO_CONGESTION after the refused set");
    ST_REQUIRE_MSG(after == before && n1 == n0,
                   "the refused SRTO_CONGESTION set was not inert: expected \"%s\" with length %d, "
                   "got \"%s\" with length %d",
                   before.c_str(), n0, after.c_str(), n1);

    // Positive control, same socket, same option, same string, true length.
    expect_set_ok(s, SRTO_CONGESTION, "live", 4, "SRTO_CONGESTION \"live\" with optlen 4");
    int n2 = -1;
    const std::string ok = read_congestion(s, &n2, "SRTO_CONGESTION after the accepted set");
    ST_REQUIRE_MSG(ok == "live" && n2 == 4,
                   "SRTO_CONGESTION after the accepted optlen 4 set: expected \"live\" with "
                   "length 4, got \"%s\" with length %d",
                   ok.c_str(), n2);

    srt_close(s);
}

// "read back as set" needs a set that CHANGES the value, or the readback proves
// only that the default survived. "file" is the other builtin congestion
// controller and its name is also four bytes long.
SRT_CASE(g01_optlen_guard, c4_congestion_optlen_four_reads_back_as_set) {
    const SRTSOCKET s = new_sock();

    int n0 = -1;
    const std::string before = read_congestion(s, &n0, "SRTO_CONGESTION on a fresh socket");
    ST_REQUIRE_MSG(before == "live" && n0 == 4,
                   "SRTO_CONGESTION on a fresh socket: expected \"live\" with length 4, "
                   "got \"%s\" with length %d",
                   before.c_str(), n0);

    expect_set_ok(s, SRTO_CONGESTION, "file", 4, "SRTO_CONGESTION \"file\" with optlen 4");

    int n1 = -1;
    const std::string got = read_congestion(s, &n1, "SRTO_CONGESTION after the accepted set");
    ST_REQUIRE_MSG(got == "file" && n1 == 4,
                   "SRTO_CONGESTION after the accepted optlen 4 set: expected \"file\" with "
                   "length 4, got \"%s\" with length %d",
                   got.c_str(), n1);

    srt_close(s);
}

// ======================================================= g02_version_readonly
//
// D2. v1.5.2 carried CSrtConfigSetter<SRTO_VERSION> and a DISPATCH entry for
// it, so the set succeeded and overwrote the socket's reported version. At
// v1.5.7 both are gone, CSrtConfig::set returns -1 for the option, and
// CUDT::setOpt turns that into MJ_NOTSUP/MN_INVAL -- SRT_EINVPARAM.

// The refusal, with the exact code, and a positive control that a four-byte set
// of a version-shaped option is not refused in general on this socket.
SRT_CASE(g02_version_readonly, c1_set_version_refused_with_einvparam) {
    const SRTSOCKET s = new_sock();

    int32_t v = 0x010203;
    expect_refused_einval(s, SRTO_VERSION, &v, (int)sizeof v,
                          "SRTO_VERSION set with a four-byte value");

    // Control: SRTO_MINVERSION is the neighbouring writable version option and
    // takes exactly the same four-byte argument on exactly this socket.
    uint32_t minv = 0x010000;
    expect_set_ok(s, SRTO_MINVERSION, &minv, (int)sizeof minv,
                  "SRTO_MINVERSION set with a four-byte value");
    uint32_t back = 0;
    int n = (int)sizeof back;
    srt_clearlasterror();
    ST_REQUIRE_MSG(srt_getsockflag(s, SRTO_MINVERSION, &back, &n) != SRT_ERROR,
                   "srt_getsockflag(SRTO_MINVERSION) failed: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(back == minv && n == (int)sizeof minv,
                   "SRTO_MINVERSION after the accepted set: expected 0x%06x in %d bytes, "
                   "got 0x%06x in %d bytes",
                   (unsigned)minv, (int)sizeof minv, (unsigned)back, n);

    srt_close(s);
}

// The getter is untouched by the removal: it still succeeds on the socket whose
// set was just refused, still writes four bytes, and still reports what
// srt_getversion() reports. Compared to srt_getversion(), never to a literal.
SRT_CASE(g02_version_readonly, c2_get_version_still_works_after_refused_set) {
    const SRTSOCKET s = new_sock();

    const uint32_t running = srt_getversion();
    int32_t attempt = 0x010203;
    ST_REQUIRE_MSG((uint32_t)attempt != running,
                   "the value offered to the refused set (0x%06x) equals srt_getversion() "
                   "(0x%06x), so an applied set would be indistinguishable from a refused one",
                   (unsigned)attempt, (unsigned)running);

    expect_refused_einval(s, SRTO_VERSION, &attempt, (int)sizeof attempt,
                          "SRTO_VERSION set with a four-byte value");

    int32_t got = 0;
    int n = (int)sizeof got;
    srt_clearlasterror();
    const int rc = srt_getsockflag(s, SRTO_VERSION, &got, &n);
    ST_REQUIRE_MSG(rc != SRT_ERROR, "srt_getsockflag(SRTO_VERSION) after the refused set failed: %s",
                   srt_getlasterror_str());
    ST_REQUIRE_MSG(n == (int)sizeof(int32_t),
                   "srt_getsockflag(SRTO_VERSION) wrote %d bytes, expected %d", n,
                   (int)sizeof(int32_t));
    ST_REQUIRE_MSG((uint32_t)got == running,
                   "SRTO_VERSION: expected srt_getversion()'s 0x%06x, got 0x%06x "
                   "(the refused set offered 0x%06x)",
                   (unsigned)running, (unsigned)got, (unsigned)attempt);

    srt_close(s);
}

// =================================================== g03_payloadsize_connected
//
// D7. Both milestones answer SRTO_PAYLOADSIZE with four bytes. v1.5.2 always
// returns the configured m_config.zExpPayloadSize; v1.5.7 returns it only while
// the socket is unconnected or the configured value is non-zero, and otherwise
// the per-packet maximum m_iMaxDataPayloadSize = MSS - (UDP 28 + SRT 16).

static const int kFileMss        = 1500;
static const int kFilePayloadMax = kFileMss - 44;   // UDP hdr 28 + SRT hdr 16
static const int kLiveDefPlSize  = 1316;
static const int kXferBytes      = 256;

// The control half, and the one both milestones agree on: unconnected, the
// getter reports what the socket was configured with. Asserting both modes on
// two sockets is what stops "0" being read as "the getter is broken".
SRT_CASE(g03_payloadsize_connected, c1_unconnected_reports_the_configured_value) {
    const SRTSOCKET live = new_sock();
    const int lv = read_int_opt(live, SRTO_PAYLOADSIZE, "SRTO_PAYLOADSIZE on a fresh live socket");
    ST_REQUIRE_MSG(lv == kLiveDefPlSize,
                   "SRTO_PAYLOADSIZE on a fresh live-mode socket: expected %d, got %d",
                   kLiveDefPlSize, lv);
    srt_close(live);

    const SRTSOCKET file = new_sock();
    int tt = SRTT_FILE;
    expect_set_ok(file, SRTO_TRANSTYPE, &tt, (int)sizeof tt, "SRTO_TRANSTYPE = SRTT_FILE");
    const int fv = read_int_opt(file, SRTO_PAYLOADSIZE,
                                "SRTO_PAYLOADSIZE on an unconnected file-mode socket");
    ST_REQUIRE_MSG(fv == 0,
                   "SRTO_PAYLOADSIZE on an unconnected file-mode socket: expected 0, got %d", fv);
    srt_close(file);
}

// The subject. The caller socket is read BEFORE connect inside cconf and again
// after, so the case observes the transition on one and the same socket rather
// than comparing two sockets; the accepted socket is read too, because the
// listener's configuration has to reach it. The 256-byte stream transfer is the
// proof that "connected" means connected and not merely "srt_connect returned".
SRT_CASE(g03_payloadsize_connected, c2_connected_file_mode_reports_the_packet_maximum) {
    int pre_caller = -1;

    SockConf lconf = [](SRTSOCKET s) {
        int tt = SRTT_FILE;
        st_set_int(s, SRTO_TRANSTYPE, tt);
        st_set_int(s, SRTO_MSS, kFileMss);
    };
    SockConf cconf = [&pre_caller](SRTSOCKET s) {
        int tt = SRTT_FILE;
        st_set_int(s, SRTO_TRANSTYPE, tt);
        st_set_int(s, SRTO_MSS, kFileMss);
        pre_caller = read_int_opt(s, SRTO_PAYLOADSIZE,
                                  "SRTO_PAYLOADSIZE on the caller before connect");
    };

    Link L;
    RelaySched clean;                 // no loss, no delay, no reordering
    st_connect(L, clean, lconf, cconf);

    ST_REQUIRE_MSG(pre_caller == 0,
                   "SRTO_PAYLOADSIZE on the file-mode caller before connect: expected 0 "
                   "(the configured value), got %d",
                   pre_caller);
    ST_REQUIRE_MSG(srt_getsockstate(L.cal) == SRTS_CONNECTED,
                   "caller socket state: expected SRTS_CONNECTED (%d), got %d",
                   (int)SRTS_CONNECTED, (int)srt_getsockstate(L.cal));
    ST_REQUIRE_MSG(srt_getsockstate(L.acc) == SRTS_CONNECTED,
                   "accepted socket state: expected SRTS_CONNECTED (%d), got %d",
                   (int)SRTS_CONNECTED, (int)srt_getsockstate(L.acc));

    const int cal_v = read_int_opt(L.cal, SRTO_PAYLOADSIZE,
                                   "SRTO_PAYLOADSIZE on the connected caller");
    ST_REQUIRE_MSG(cal_v == kFilePayloadMax,
                   "SRTO_PAYLOADSIZE on the connected file-mode caller at SRTO_MSS %d: "
                   "expected %d, got %d (it reported %d before connect)",
                   kFileMss, kFilePayloadMax, cal_v, pre_caller);

    const int acc_v = read_int_opt(L.acc, SRTO_PAYLOADSIZE,
                                   "SRTO_PAYLOADSIZE on the accepted socket");
    ST_REQUIRE_MSG(acc_v == kFilePayloadMax,
                   "SRTO_PAYLOADSIZE on the accepted file-mode socket at SRTO_MSS %d: "
                   "expected %d, got %d",
                   kFileMss, kFilePayloadMax, acc_v);

    // The link really carries data, so the number above is a connected socket's
    // report and not a disconnected socket's leftover configuration.
    const std::string msg = st_payload(0, kXferBytes);
    const int sent = srt_send(L.cal, msg.data(), (int)msg.size());
    ST_REQUIRE_MSG(sent == (int)msg.size(), "srt_send returned %d, expected %d: %s", sent,
                   (int)msg.size(), srt_getlasterror_str());

    st_set_int(L.acc, SRTO_RCVTIMEO, 5000);   // bounded receive, never a sleep
    std::string got(msg.size(), '\0');
    int total = 0;
    while (total < (int)msg.size()) {
        const int r = srt_recv(L.acc, &got[(size_t)total], (int)msg.size() - total);
        if (r <= 0) break;
        total += r;
    }
    ST_REQUIRE_MSG(total == (int)msg.size(),
                   "the connected file-mode link delivered %d of %d bytes: %s", total,
                   (int)msg.size(), srt_getlasterror_str());
    ST_REQUIRE_MSG(got == msg,
                   "the connected file-mode link delivered %d bytes that differ from what was sent",
                   total);
}
