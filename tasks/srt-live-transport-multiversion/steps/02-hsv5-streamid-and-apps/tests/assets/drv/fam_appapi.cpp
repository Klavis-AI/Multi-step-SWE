// Family "appapi", checkpoint 02 (SRT v1.3.0). Six scored groups, one subject
// each: the entry points this milestone adds, the transmission-type preset,
// the declared payload size, the message-size refusals, the option slots this
// specification leaves undefined or retires, and the byte-statistics call.
//
// Every group in this family is tier `new`, so the file names this milestone's
// identifiers directly -- SRTO_TRANSTYPE, SRTO_PAYLOADSIZE, srt_bistats,
// srt_create_socket and the rest. That is deliberate. None of them exists at
// v1.2.0, the family holds no `inherited` or `peripheral` group that a
// candidate who has not done this milestone could earn, and a driver that
// still compiled there would be one that cannot discriminate. tests/test.sh
// builds each family separately, so this file refusing to build against the
// previous release costs only this family's groups -- which is the intended
// outcome, not a defect.
//
// Every assertion is traceable to this step's own interface.md (the corrected
// option table, the entry-point signatures, the message-sizing table, the
// asymmetric refusal for undefined slots and the statistics section), to the
// pinned srt-docs/API.md rows those corrections leave standing, or to
// deltas/02.md rows D1/D2/D5/D6/D7/D9/D10/D11/D12/D13. Nothing here asserts a
// number the documentation leaves open, and nothing here names an identifier
// the candidate was not told to provide: the two payload-size constants the
// released header happens to declare are spelled out locally below rather
// than included from <srt/srt.h>, because neither name appears in
// interface.md or in srt-docs, and an unresolved name would cost the whole
// family a build rather than costing one group a point.
#include "srtkit.h"

#include <dlfcn.h>
#include <sys/socket.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------- helpers

// srt-docs/API.md, the SRTO_PAYLOADSIZE row of the option table: the live
// default is 1316 and the range is "up to MTUsize-28-16, usually 1456" -- the
// largest payload that fits in one path MTU, which the same document repeats
// as a flat 1456-byte limit. Local constants, not header macros: see above.
static const int kApLiveDefPayload = 1316;
static const int kApLiveMaxPayload = 1456;

static const char* ap_errname(int e) {
    switch (e) {
    // The zero code by its value, not by its name: the previous release spells
    // it SRT_ESUCCESS and this one spells it SRT_SUCCESS, and no document in
    // either milestone mentions the rename, so the driver names neither.
    case 0:                  return "no error";
    case SRT_EINVOP:         return "SRT_EINVOP";
    case SRT_EBOUNDSOCK:     return "SRT_EBOUNDSOCK";
    case SRT_ECONNSOCK:      return "SRT_ECONNSOCK";
    case SRT_EINVPARAM:      return "SRT_EINVPARAM";
    case SRT_EINVSOCK:       return "SRT_EINVSOCK";
    case SRT_EINVALMSGAPI:   return "SRT_EINVALMSGAPI";
    case SRT_ELARGEMSG:      return "SRT_ELARGEMSG";
    case SRT_ENOCONN:        return "SRT_ENOCONN";
    case SRT_ECONNLOST:      return "SRT_ECONNLOST";
    case SRT_ETIMEOUT:       return "SRT_ETIMEOUT";
    case SRT_EASYNCRCV:      return "SRT_EASYNCRCV";
    default:                 return "<other>";
    }
}

static const char* ap_state_name(SRT_SOCKSTATUS s) {
    switch (s) {
    case SRTS_INIT:       return "SRTS_INIT";
    case SRTS_OPENED:     return "SRTS_OPENED";
    case SRTS_LISTENING:  return "SRTS_LISTENING";
    case SRTS_CONNECTING: return "SRTS_CONNECTING";
    case SRTS_CONNECTED:  return "SRTS_CONNECTED";
    case SRTS_BROKEN:     return "SRTS_BROKEN";
    case SRTS_CLOSING:    return "SRTS_CLOSING";
    case SRTS_CLOSED:     return "SRTS_CLOSED";
    case SRTS_NONEXIST:   return "SRTS_NONEXIST";
    }
    return "<unknown>";
}

// The outcome of one option or transfer call: the return code and, when it
// failed, the error the library recorded. The last error is cleared on both
// sides of the call so that a success can never report a stale code from an
// earlier probe -- which is how a negative case silently becomes vacuous.
struct ApCall {
    int rc;
    int err;
};

static ApCall ap_try_get(SRTSOCKET s, SRT_SOCKOPT o) {
    char buf[600];
    std::memset(buf, 0, sizeof buf);
    int n = (int)sizeof buf;
    ApCall r;
    srt_clearlasterror();
    r.rc = srt_getsockflag(s, o, buf, &n);
    r.err = srt_getlasterror(NULL);
    srt_clearlasterror();
    return r;
}

static ApCall ap_try_set_int(SRTSOCKET s, SRT_SOCKOPT o, int v) {
    ApCall r;
    srt_clearlasterror();
    r.rc = srt_setsockflag(s, o, &v, sizeof v);
    r.err = srt_getlasterror(NULL);
    srt_clearlasterror();
    return r;
}

static ApCall ap_try_sendmsg(SRTSOCKET s, const char* buf, int len) {
    ApCall r;
    srt_clearlasterror();
    r.rc = srt_sendmsg(s, buf, len, -1, 1);
    r.err = srt_getlasterror(NULL);
    srt_clearlasterror();
    return r;
}

static ApCall ap_try_recvmsg(SRTSOCKET s, char* buf, int len) {
    ApCall r;
    srt_clearlasterror();
    r.rc = srt_recvmsg(s, buf, len);
    r.err = srt_getlasterror(NULL);
    srt_clearlasterror();
    return r;
}

// A refusal is only the refusal the specification names: both that the call
// failed and that it failed with that code. Reported with actual and expected
// side by side, because a reviewer reads failures, not code.
#define AP_REFUSED(res, want, what)                                             \
    do {                                                                        \
        const ApCall& r_ = (res);                                               \
        ST_REQUIRE_MSG(r_.rc == SRT_ERROR,                                      \
                       "%s: expected a refusal with %s (%d), but the call was " \
                       "accepted and returned %d",                              \
                       (what), ap_errname((int)(want)), (int)(want), r_.rc);    \
        ST_REQUIRE_MSG(r_.err == (int)(want),                                   \
                       "%s: expected %s (%d), got %s (%d: %s)", (what),         \
                       ap_errname((int)(want)), (int)(want),                    \
                       ap_errname(r_.err), r_.err, srt_strerror(r_.err, 0));    \
    } while (0)

#define AP_ACCEPTED(res, what)                                                  \
    do {                                                                        \
        const ApCall& r_ = (res);                                               \
        ST_REQUIRE_MSG(r_.rc != SRT_ERROR,                                      \
                       "%s: expected the call to be accepted, got %s (%d: %s)", \
                       (what), ap_errname(r_.err), r_.err,                      \
                       srt_strerror(r_.err, 0));                                \
    } while (0)

// interface.md: boolean options do not agree on their width. Zero-fill four
// bytes, pass optlen 4, test for non-zero, accept a written length of 1 or 4.
static bool ap_get_bool(SRTSOCKET s, SRT_SOCKOPT o, const char* label) {
    int32_t v = 0;
    int n = (int)sizeof v;
    if (srt_getsockflag(s, o, &v, &n) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "get %s failed: %s", label, srt_getlasterror_str());
    if (n != 1 && n != 4)
        st_failf(__FILE__, __LINE__,
                 "get %s wrote optlen %d; a boolean option must report 1 or 4", label, n);
    return v != 0;
}

static int ap_get_int(SRTSOCKET s, SRT_SOCKOPT o, const char* label) {
    int v = -1;
    int n = (int)sizeof v;
    if (srt_getsockflag(s, o, &v, &n) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "get %s failed: %s", label, srt_getlasterror_str());
    if (n != (int)sizeof(int))
        st_failf(__FILE__, __LINE__, "get %s wrote optlen %d, expected %zu", label, n,
                 sizeof(int));
    return v;
}

// interface.md: the congestion-control name comes back NUL-terminated with
// optlen reporting the length without the terminator.
static std::string ap_get_str(SRTSOCKET s, SRT_SOCKOPT o, const char* label) {
    char buf[256];
    std::memset(buf, 0, sizeof buf);
    int n = (int)sizeof buf;
    if (srt_getsockflag(s, o, buf, &n) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "get %s failed: %s", label, srt_getlasterror_str());
    if (n < 0 || n >= (int)sizeof buf)
        st_failf(__FILE__, __LINE__, "get %s reported optlen %d, outside the %zu-byte buffer",
                 label, n, sizeof buf);
    if (buf[n] != '\0')
        st_failf(__FILE__, __LINE__,
                 "get %s: byte %d is 0x%02x, so the string was not NUL-terminated at the "
                 "reported length", label, n, (unsigned)(unsigned char)buf[n]);
    return std::string(buf, (size_t)n);
}

static void ap_set_int(SRTSOCKET s, SRT_SOCKOPT o, int v, const char* label) {
    if (srt_setsockflag(s, o, &v, sizeof v) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "set %s = %d failed: %s", label, v,
                 srt_getlasterror_str());
}

// The transmission type is passed as a plain int holding the live or the file
// value. TEST_INTERFACE.md fixes both the names and the numbers -- `SRT_TRANSTYPE`
// is a `typedef enum` whose members are `SRTT_LIVE` then `SRTT_FILE`, unnumbered
// and therefore 0 and 1 -- and says in the same sentence that no group requires
// the candidate to expose any of the three names. The driver therefore spells
// the numbers and never the names, in either the type or the enumerators: this
// whole family is one translation unit carrying six groups, so a name the
// document says is optional would have cost all six a build instead of costing
// one group a point, which is exactly the hazard DRIVER_RULES.md's "Naming an
// identifier the candidate may not have" exists to prevent. A static_assert
// tying these to the enumerators is not available for the same reason: an
// enumerator cannot be probed with #ifdef, so the assert would reintroduce the
// dependency it was meant to check.
static const int AP_TRANSTYPE_LIVE = 0;
static const int AP_TRANSTYPE_FILE = 1;

static void ap_set_transtype(SRTSOCKET s, int t, const char* label) {
    if (srt_setsockflag(s, SRTO_TRANSTYPE, &t, sizeof t) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "set SRTO_TRANSTYPE = %s failed: %s", label,
                 srt_getlasterror_str());
}

static void ap_bind_loopback(SRTSOCKET s) {
    sockaddr_in a = st_loopback(0);
    if (srt_bind(s, (sockaddr*)&a, sizeof a) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "srt_bind: %s", srt_getlasterror_str());
}

// ------------------------------------------------------- a direct link
//
// srtkit's st_connect always builds its sockets with srt_socket, and always
// routes them through the relay. g04 has to bring the connection up with the
// constructor this milestone adds, and no group in this family needs a lossy
// channel, so the family carries its own loopback link: same shape as
// st_try_connect, same bounded accept, parameterised by the constructor.
typedef SRTSOCKET (*ApSocketFactory)();

static SRTSOCKET ap_make_with_srt_socket() { return st_new_socket(); }
static SRTSOCKET ap_make_with_srt_create_socket() { return srt_create_socket(); }

struct ApLink {
    SRTSOCKET lsn;
    SRTSOCKET cal;
    SRTSOCKET acc;

    ApLink() : lsn(SRT_INVALID_SOCK), cal(SRT_INVALID_SOCK), acc(SRT_INVALID_SOCK) {}
    ApLink(const ApLink&);
    ApLink& operator=(const ApLink&);
    ~ApLink() {
        if (acc != SRT_INVALID_SOCK) srt_close(acc);
        if (cal != SRT_INVALID_SOCK) srt_close(cal);
        if (lsn != SRT_INVALID_SOCK) srt_close(lsn);
    }
};

// Lossless live transport: the relay is not in the path, so nothing is lost,
// but too-late drop is off and the latency is explicit so that a scheduling
// hiccup on a loaded box cannot turn into a dropped packet and a flaky count.
static void ap_live_lossless(SRTSOCKET s) {
    ap_set_int(s, SRTO_TSBPDMODE, 1, "SRTO_TSBPDMODE");
    ap_set_int(s, SRTO_LATENCY, 120, "SRTO_LATENCY");
    ap_set_int(s, SRTO_TLPKTDROP, 0, "SRTO_TLPKTDROP");
}

static void ap_connect(ApLink& L, ApSocketFactory mk, const SockConf& lconf,
                       const SockConf& cconf) {
    L.lsn = mk();
    ST_REQUIRE_MSG(L.lsn != SRT_INVALID_SOCK, "listener socket: %s", srt_getlasterror_str());
    // Bounded accept: a refused handshake must cost this, not the case budget.
    // The accept is bounded by st_accept_polled, not by a timeout: a blocking
    // srt_accept does not consult SRTO_RCVTIMEO at any prefix in this ladder.
    lconf(L.lsn);
    ap_bind_loopback(L.lsn);

    sockaddr_in la = st_loopback(0);
    int nl = (int)sizeof la;
    ST_REQUIRE_MSG(srt_getsockname(L.lsn, (sockaddr*)&la, &nl) != SRT_ERROR,
                   "srt_getsockname: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(srt_listen(L.lsn, 1) != SRT_ERROR, "srt_listen: %s",
                   srt_getlasterror_str());

    // The caller socket is built and configured before the acceptor thread
    // starts: nothing between the thread's construction and its join may
    // throw, or the throw would run std::thread's destructor on a joinable
    // thread and take the whole case down as a terminate() rather than as the
    // assertion that actually failed.
    L.cal = mk();
    ST_REQUIRE_MSG(L.cal != SRT_INVALID_SOCK, "caller socket: %s", srt_getlasterror_str());
    cconf(L.cal);

    SRTSOCKET accepted = SRT_INVALID_SOCK;
    std::atomic<bool> connect_done(false);
    std::thread acceptor(
        [&] { accepted = st_accept_polled(L.lsn, connect_done); });

    int rc = srt_connect(L.cal, (sockaddr*)&la, sizeof la);
    int err = srt_getlasterror(NULL);
    connect_done.store(true);
    acceptor.join();
    L.acc = accepted;

    ST_REQUIRE_MSG(rc != SRT_ERROR, "srt_connect: %s (%d)", srt_strerror(err, 0), err);
    ST_REQUIRE_MSG(L.acc != SRT_INVALID_SOCK, "srt_accept returned SRT_INVALID_SOCK: %s",
                   srt_getlasterror_str());
}

static void ap_require_state(SRTSOCKET s, SRT_SOCKSTATUS want, const char* who) {
    SRT_SOCKSTATUS got = srt_getsockstate(s);
    ST_REQUIRE_MSG(got == want, "%s: expected state %s, got %s", who, ap_state_name(want),
                   ap_state_name(got));
}

static std::string ap_send_one(SRTSOCKET s, int index, int size) {
    std::string m = st_payload(index, size);
    int rc = srt_sendmsg(s, m.data(), (int)m.size(), -1, 1);
    ST_REQUIRE_MSG(rc == (int)m.size(), "srt_sendmsg of %d bytes returned %d: %s", size, rc,
                   srt_getlasterror_str());
    return m;
}

static std::string ap_recv_one(SRTSOCKET s, int bufsize, int timeo_ms) {
    ap_set_int(s, SRTO_RCVTIMEO, timeo_ms, "SRTO_RCVTIMEO");
    std::vector<char> buf((size_t)bufsize, '\0');
    int rc = srt_recvmsg(s, &buf[0], bufsize);
    ST_REQUIRE_MSG(rc > 0, "srt_recvmsg into a %d-byte buffer returned %d: %s", bufsize, rc,
                   srt_getlasterror_str());
    return std::string(&buf[0], (size_t)rc);
}

// ====================================================== g04_entrypoints
//
// D13 and D1. The subject is the eight application-facing entry points this
// milestone adds, plus the general constructor's type argument becoming
// advisory. interface.md lists all eight signatures and says every one of them
// must compile and link exactly as written, that srt_create_socket must
// produce a socket that binds, listens, accepts and carries a message, and
// that srt_socket ignoring its type and protocol arguments is documented.
//
// Each entry point is named once here through a pointer of the documented
// signature: a signature that disagrees is a compile error, a name the library
// does not define is a link error, and the pointers are volatile so that the
// reference survives optimisation rather than being folded away. dlsym then
// asks the question the group description actually poses -- is it resolvable
// in the installed library -- and names the one that is not.
typedef void (*ApAnyFn)();

// interface.md, "The eight entry points": srt_msgctrl_init fills its argument
// with the same defaults as the srt_msgctrl_default object, which must also
// exist -- msgttl -1, everything else zero.  Checked field by field rather
// than with memcmp so that structure padding, which no document fixes, cannot
// decide the verdict.  Every field named here is named in srt-docs/API.md's
// description of the srt_sendmsg2 / srt_recvmsg2 control object.
static void ap_require_msgctrl_defaults(const SRT_MSGCTRL& c, const char* what) {
    ST_REQUIRE_MSG(c.msgttl == -1,
                   "%s: msgttl is %d; this milestone's default is -1", what, c.msgttl);
    ST_REQUIRE_MSG(c.flags == 0,
                   "%s: flags is %d; every field but msgttl defaults to zero", what, c.flags);
    ST_REQUIRE_MSG(c.inorder == 0,
                   "%s: inorder is %d; every field but msgttl defaults to zero", what,
                   c.inorder);
    ST_REQUIRE_MSG(c.boundary == 0,
                   "%s: boundary is %d; every field but msgttl defaults to zero", what,
                   c.boundary);
    ST_REQUIRE_MSG(c.srctime == 0,
                   "%s: srctime is %llu; every field but msgttl defaults to zero", what,
                   (unsigned long long)c.srctime);
    ST_REQUIRE_MSG(c.pktseq == 0,
                   "%s: pktseq is %d; every field but msgttl defaults to zero", what,
                   (int)c.pktseq);
    ST_REQUIRE_MSG(c.msgno == 0,
                   "%s: msgno is %d; every field but msgttl defaults to zero", what,
                   (int)c.msgno);
}

static SRTSOCKET (*volatile ap_fn_create_socket)(void) = &srt_create_socket;
static int (*volatile ap_fn_connect_debug)(SRTSOCKET, const struct sockaddr*, int, int) =
    &srt_connect_debug;
static int (*volatile ap_fn_rendezvous)(SRTSOCKET, const struct sockaddr*, int,
                                        const struct sockaddr*, int) = &srt_rendezvous;
static int64_t (*volatile ap_fn_sendfile)(SRTSOCKET, const char*, int64_t*, int64_t, int) =
    &srt_sendfile;
static int64_t (*volatile ap_fn_recvfile)(SRTSOCKET, const char*, int64_t*, int64_t, int) =
    &srt_recvfile;
static void (*volatile ap_fn_msgctrl_init)(SRT_MSGCTRL*) = &srt_msgctrl_init;
static int (*volatile ap_fn_bistats)(SRTSOCKET, SRT_TRACEBSTATS*, int, int) = &srt_bistats;
static int (*volatile ap_fn_getsndbuffer)(SRTSOCKET, size_t*, size_t*) = &srt_getsndbuffer;

SRT_CASE(g04_entrypoints, the_eight_new_entry_points_resolve_in_the_installed_library) {
    struct Entry {
        const char* name;
        ApAnyFn linked;
    };
    const Entry eps[8] = {
        {"srt_create_socket", (ApAnyFn)ap_fn_create_socket},
        {"srt_connect_debug", (ApAnyFn)ap_fn_connect_debug},
        {"srt_rendezvous",    (ApAnyFn)ap_fn_rendezvous},
        {"srt_sendfile",      (ApAnyFn)ap_fn_sendfile},
        {"srt_recvfile",      (ApAnyFn)ap_fn_recvfile},
        {"srt_msgctrl_init",  (ApAnyFn)ap_fn_msgctrl_init},
        {"srt_bistats",       (ApAnyFn)ap_fn_bistats},
        {"srt_getsndbuffer",  (ApAnyFn)ap_fn_getsndbuffer},
    };

    // Controls first: a probe that answers the same way whatever it is asked
    // proves nothing about the eight below it.
    ST_REQUIRE_MSG(dlsym(RTLD_DEFAULT, "srt_startup") != NULL,
                   "control: srt_startup, which exists at every milestone in this chain, "
                   "did not resolve, so the symbol probe itself is broken");
    ST_REQUIRE_MSG(dlsym(RTLD_DEFAULT, "srt_no_such_entry_point_exists") == NULL,
                   "control: a name no library defines resolved to a non-null address, so "
                   "the symbol probe cannot tell present from absent");

    for (int i = 0; i < 8; i++) {
        ST_REQUIRE_MSG(eps[i].linked != NULL,
                       "%s: the linker resolved it to a null address", eps[i].name);
        void* sym = dlsym(RTLD_DEFAULT, eps[i].name);
        ST_REQUIRE_MSG(sym != NULL,
                       "%s: expected an exported entry point in the installed library, got "
                       "no such symbol (dlsym returned NULL)", eps[i].name);
    }

    // Resolving srt_msgctrl_init says nothing about what it does: a library
    // that ships it as an empty function, and ships no srt_msgctrl_default at
    // all, passes every probe above.  interface.md states both obligations, so
    // both are asserted -- the argument is pre-dirtied so that a function
    // which writes nothing cannot be mistaken for one which writes zeroes.
    SRT_MSGCTRL filled;
    std::memset(&filled, 0x5a, sizeof filled);
    srt_msgctrl_init(&filled);
    ap_require_msgctrl_defaults(filled, "the structure srt_msgctrl_init filled");
    ap_require_msgctrl_defaults(srt_msgctrl_default, "the srt_msgctrl_default object");
}

SRT_CASE(g04_entrypoints, srt_create_socket_binds_listens_accepts_and_carries_a_message) {
    ApLink L;
    ap_connect(L, &ap_make_with_srt_create_socket, ap_live_lossless, ap_live_lossless);

    ap_require_state(L.lsn, SRTS_LISTENING, "the listener built by srt_create_socket");
    ap_require_state(L.cal, SRTS_CONNECTED, "the caller built by srt_create_socket");
    ap_require_state(L.acc, SRTS_CONNECTED, "the socket srt_accept returned");

    const int kCount = 8;
    const int kSize = kApLiveDefPayload;
    std::vector<std::string> sent = st_send_stream(L.cal, kCount, kSize, 1000);
    std::vector<std::string> got = st_recv_stream(L.acc, kCount, kSize, 5000);

    ST_REQUIRE_MSG((int)got.size() == kCount,
                   "a link of srt_create_socket sockets delivered %d of %d messages",
                   (int)got.size(), kCount);
    for (int i = 0; i < kCount; i++)
        ST_REQUIRE_MSG(got[(size_t)i] == sent[(size_t)i],
                       "message %d came back different: expected %d bytes beginning \"%.11s\", "
                       "got %d bytes beginning \"%.11s\"", i, (int)sent[(size_t)i].size(),
                       sent[(size_t)i].c_str(), (int)got[(size_t)i].size(),
                       got[(size_t)i].c_str());

    // The channel really carried the messages rather than the comparison
    // succeeding on two empty vectors.
    SRT_TRACEBSTATS st;
    std::memset(&st, 0, sizeof st);
    ST_SRT_OK(srt_bstats(L.acc, &st, 0));
    ST_REQUIRE_MSG(st.pktRecvTotal >= (int64_t)kCount,
                   "the accepted socket reports pktRecvTotal %lld after %d messages, so the "
                   "payload did not cross the connection",
                   (long long)st.pktRecvTotal, kCount);
}

SRT_CASE(g04_entrypoints, srt_socket_accepts_a_type_argument_it_does_not_use) {
    struct Kind {
        int type;
        int protocol;
        const char* label;
    };
    const Kind kinds[4] = {
        {0, 0, "type 0, protocol 0"},
        {SOCK_STREAM, 0, "SOCK_STREAM, protocol 0"},
        {SOCK_DGRAM, 0, "SOCK_DGRAM, protocol 0"},
        {SOCK_DGRAM, 99, "SOCK_DGRAM, protocol 99"},
    };

    for (int i = 0; i < 4; i++) {
        srt_clearlasterror();
        SRTSOCKET s = srt_socket(AF_INET, kinds[i].type, kinds[i].protocol);
        int err = srt_getlasterror(NULL);
        srt_clearlasterror();
        ST_REQUIRE_MSG(s != SRT_INVALID_SOCK,
                       "srt_socket(AF_INET, %s): expected a socket, the type and protocol "
                       "arguments being unused, got SRT_INVALID_SOCK with %s (%d: %s)",
                       kinds[i].label, ap_errname(err), err, srt_strerror(err, 0));
        ap_require_state(s, SRTS_INIT, kinds[i].label);
        // The argument was ignored rather than honoured: every socket is a
        // datagram socket now, so the message API is on whatever was asked for.
        ST_REQUIRE_MSG(ap_get_bool(s, SRTO_MESSAGEAPI, "SRTO_MESSAGEAPI"),
                       "srt_socket(AF_INET, %s): SRTO_MESSAGEAPI reads 0, so the type "
                       "argument selected a stream socket instead of being ignored",
                       kinds[i].label);
        srt_close(s);
    }
}

// ========================================================= g05_transtype
//
// D6. One option selects a whole preset. interface.md pins down how to read it
// back: SRTO_TLPKTDROP answers from a field that is refreshed when the socket
// is bound, so the six options are read on a *bound* socket, and each
// direction of the round trip gets its own socket because a socket cannot be
// bound twice. The live latency the preset also sets is deliberately not read
// -- interface.md says it is not observable as a read-back, so no case here
// asks for it.
struct ApPreset {
    bool tsbpd;
    bool tlpktdrop;
    bool nakreport;
    bool messageapi;
    int payloadsize;
    std::string smoother;
};

static ApPreset ap_read_preset(SRTSOCKET s) {
    ApPreset p;
    p.tsbpd = ap_get_bool(s, SRTO_TSBPDMODE, "SRTO_TSBPDMODE");
    p.tlpktdrop = ap_get_bool(s, SRTO_TLPKTDROP, "SRTO_TLPKTDROP");
    p.nakreport = ap_get_bool(s, SRTO_NAKREPORT, "SRTO_NAKREPORT");
    p.messageapi = ap_get_bool(s, SRTO_MESSAGEAPI, "SRTO_MESSAGEAPI");
    p.payloadsize = ap_get_int(s, SRTO_PAYLOADSIZE, "SRTO_PAYLOADSIZE");
    p.smoother = ap_get_str(s, SRTO_SMOOTHER, "SRTO_SMOOTHER");
    return p;
}

static void ap_require_preset(const ApPreset& got, const ApPreset& want, const char* what) {
    ST_REQUIRE_MSG(got.tsbpd == want.tsbpd,
                   "%s: SRTO_TSBPDMODE expected %d, got %d", what, (int)want.tsbpd,
                   (int)got.tsbpd);
    ST_REQUIRE_MSG(got.tlpktdrop == want.tlpktdrop,
                   "%s: SRTO_TLPKTDROP expected %d, got %d", what, (int)want.tlpktdrop,
                   (int)got.tlpktdrop);
    ST_REQUIRE_MSG(got.nakreport == want.nakreport,
                   "%s: SRTO_NAKREPORT expected %d, got %d", what, (int)want.nakreport,
                   (int)got.nakreport);
    ST_REQUIRE_MSG(got.messageapi == want.messageapi,
                   "%s: SRTO_MESSAGEAPI expected %d, got %d", what, (int)want.messageapi,
                   (int)got.messageapi);
    ST_REQUIRE_MSG(got.payloadsize == want.payloadsize,
                   "%s: SRTO_PAYLOADSIZE expected %d, got %d", what, want.payloadsize,
                   got.payloadsize);
    ST_REQUIRE_MSG(got.smoother == want.smoother,
                   "%s: SRTO_SMOOTHER expected \"%s\", got \"%s\"", what,
                   want.smoother.c_str(), got.smoother.c_str());
}

static ApPreset ap_live_preset() {
    ApPreset p;
    p.tsbpd = true;
    p.tlpktdrop = true;
    p.nakreport = true;
    p.messageapi = true;
    p.payloadsize = kApLiveDefPayload;
    p.smoother = "live";
    return p;
}

static ApPreset ap_file_preset() {
    ApPreset p;
    p.tsbpd = false;
    p.tlpktdrop = false;
    p.nakreport = false;
    p.messageapi = false;
    p.payloadsize = 0;
    p.smoother = "file";
    return p;
}

// The baseline the other two cases are measured against: without it, a library
// that reported the file preset for everything would still pass the file case.
SRT_CASE(g05_transtype, a_socket_that_was_never_told_otherwise_reports_the_live_preset) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ap_bind_loopback(s);
    ap_require_state(s, SRTS_OPENED, "a bound, unconnected socket");
    ap_require_preset(ap_read_preset(s), ap_live_preset(),
                      "the default transmission type");
    srt_close(s);
}

SRT_CASE(g05_transtype, the_file_type_turns_off_the_live_machinery) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    ap_set_transtype(s, AP_TRANSTYPE_FILE, "the file type (1)");
    ap_bind_loopback(s);
    ap_require_state(s, SRTS_OPENED, "a bound, unconnected socket");
    ap_require_preset(ap_read_preset(s), ap_file_preset(),
                      "SRTO_TRANSTYPE = SRTT_FILE on an unconnected socket");
    srt_close(s);
}

SRT_CASE(g05_transtype, selecting_the_live_type_again_restores_every_live_value) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    // Away from the defaults first, so that the restore has something to undo:
    // a library that ignored SRTO_TRANSTYPE entirely would otherwise pass.
    ap_set_transtype(s, AP_TRANSTYPE_FILE, "the file type (1)");
    ap_set_transtype(s, AP_TRANSTYPE_LIVE, "the live type (0)");
    ap_bind_loopback(s);
    ap_require_state(s, SRTS_OPENED, "a bound, unconnected socket");
    ap_require_preset(ap_read_preset(s), ap_live_preset(),
                      "SRTO_TRANSTYPE = SRTT_LIVE after SRTT_FILE");
    srt_close(s);
}

// ====================================================== g06_payloadsize
//
// D7. Both numbers come from the SRTO_PAYLOADSIZE row of the shipped option
// table -- 1316 for the live default, 1456 for "the largest payload that fits
// in one path MTU" -- so neither is invented here.
SRT_CASE(g06_payloadsize, the_declared_payload_size_reads_back_at_its_live_default) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    int v = ap_get_int(s, SRTO_PAYLOADSIZE, "SRTO_PAYLOADSIZE");
    ST_REQUIRE_MSG(v == kApLiveDefPayload,
                   "SRTO_PAYLOADSIZE on a fresh socket: expected the live default %d, got %d",
                   kApLiveDefPayload, v);
    srt_close(s);
}

SRT_CASE(g06_payloadsize, every_value_up_to_one_path_mtu_is_accepted) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    const int values[4] = {188, 1000, kApLiveDefPayload, kApLiveMaxPayload};
    for (int i = 0; i < 4; i++) {
        char what[96];
        std::snprintf(what, sizeof what, "set SRTO_PAYLOADSIZE = %d on an unconnected socket",
                      values[i]);
        AP_ACCEPTED(ap_try_set_int(s, SRTO_PAYLOADSIZE, values[i]), what);
        int back = ap_get_int(s, SRTO_PAYLOADSIZE, "SRTO_PAYLOADSIZE");
        ST_REQUIRE_MSG(back == values[i],
                       "SRTO_PAYLOADSIZE after setting %d: expected %d, got %d", values[i],
                       values[i], back);
    }
    srt_close(s);
}

SRT_CASE(g06_payloadsize, a_payload_larger_than_one_path_mtu_is_a_bad_parameter) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    // Positive control on the far side of the same boundary.
    AP_ACCEPTED(ap_try_set_int(s, SRTO_PAYLOADSIZE, kApLiveMaxPayload),
                "set SRTO_PAYLOADSIZE to the maximum payload that fits in one path MTU");

    const int toobig[2] = {kApLiveMaxPayload + 1, 1500};
    for (int i = 0; i < 2; i++) {
        char what[96];
        std::snprintf(what, sizeof what, "set SRTO_PAYLOADSIZE = %d, above the %d-byte limit",
                      toobig[i], kApLiveMaxPayload);
        AP_REFUSED(ap_try_set_int(s, SRTO_PAYLOADSIZE, toobig[i]), SRT_EINVPARAM, what);
        int back = ap_get_int(s, SRTO_PAYLOADSIZE, "SRTO_PAYLOADSIZE");
        ST_REQUIRE_MSG(back == kApLiveMaxPayload,
                       "SRTO_PAYLOADSIZE after the refused set of %d: expected the previous "
                       "value %d to survive, got %d", toobig[i], kApLiveMaxPayload, back);
    }
    srt_close(s);
}

SRT_CASE(g06_payloadsize, a_set_is_refused_once_the_socket_is_connected) {
    // The same value on an unconnected socket, as the positive control: the
    // refusal must be about the socket's state, not about the value.
    SRTSOCKET fresh = st_new_socket();
    ST_REQUIRE_MSG(fresh != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());
    AP_ACCEPTED(ap_try_set_int(fresh, SRTO_PAYLOADSIZE, 1000),
                "set SRTO_PAYLOADSIZE = 1000 on an unconnected socket");
    srt_close(fresh);

    ApLink L;
    ap_connect(L, &ap_make_with_srt_socket, ap_live_lossless, ap_live_lossless);
    ap_require_state(L.cal, SRTS_CONNECTED, "the caller");
    ap_require_state(L.acc, SRTS_CONNECTED, "the socket srt_accept returned");

    AP_REFUSED(ap_try_set_int(L.cal, SRTO_PAYLOADSIZE, 1000), SRT_ECONNSOCK,
               "set SRTO_PAYLOADSIZE = 1000 on the connected caller");
    AP_REFUSED(ap_try_set_int(L.acc, SRTO_PAYLOADSIZE, 1000), SRT_ECONNSOCK,
               "set SRTO_PAYLOADSIZE = 1000 on the connected accepted socket");

    int back = ap_get_int(L.cal, SRTO_PAYLOADSIZE, "SRTO_PAYLOADSIZE");
    ST_REQUIRE_MSG(back == kApLiveDefPayload,
                   "SRTO_PAYLOADSIZE on the connected caller after the refused set: expected "
                   "the value in force %d to be unchanged, got %d", kApLiveDefPayload, back);
}

// ======================================================== g07_msgsizing
//
// D9, D10, D11 and interface.md's message-sizing table. The refusals are
// raised only on a connected socket under the live transmission type, and the
// size they compare against is the declared payload size that was in force
// when the connection was made -- so three of the four cases declare a payload
// size of their own rather than leaning on the live default, which is what
// makes the assertion about the declaration and not about the constant 1316.
static const int kAp7Declared = 1000;

// interface.md, "There is no size above which a different code appears":
// SRT_ELARGEMSG (5012) is declared at this milestone and raised nowhere, so a
// send far above the declared size is refused with SRT_EINVALMSGAPI exactly
// like a one-byte overshoot.  100000 is one of the sizes that sentence names.
static const int kAp7FarOver = 100000;

static void ap_live_declared_1000(SRTSOCKET s) {
    ap_live_lossless(s);
    ap_set_int(s, SRTO_PAYLOADSIZE, kAp7Declared, "SRTO_PAYLOADSIZE");
}

SRT_CASE(g07_msgsizing, a_send_longer_than_the_declared_payload_size_is_refused) {
    ApLink L;
    ap_connect(L, &ap_make_with_srt_socket, ap_live_declared_1000, ap_live_declared_1000);

    // Sized for the largest overshoot probed below, not for the largest legal
    // payload: interface.md says the refusal is the same at any size above the
    // declared one, and the only way to assert "no second threshold" is to ask
    // at a size far above it.
    std::vector<char> big((size_t)kAp7FarOver, 'x');

    // Positive control: exactly the declared size is carried, so the refusals
    // below are about the length and not about a connection that cannot send.
    AP_ACCEPTED(ap_try_sendmsg(L.cal, &big[0], kAp7Declared),
                "srt_sendmsg of exactly the declared payload size");

    const int over[3] = {kAp7Declared + 1, kApLiveDefPayload, kAp7FarOver};
    for (int i = 0; i < 3; i++) {
        char what[128];
        std::snprintf(what, sizeof what,
                      "srt_sendmsg of %d bytes with the declared payload size at %d", over[i],
                      kAp7Declared);
        AP_REFUSED(ap_try_sendmsg(L.cal, &big[0], over[i]), SRT_EINVALMSGAPI, what);
    }

    // Refused, not quietly truncated: the one message that was accepted is the
    // one that arrives, whole.
    std::string got = ap_recv_one(L.acc, kApLiveMaxPayload, 5000);
    ST_REQUIRE_MSG((int)got.size() == kAp7Declared,
                   "after one accepted send of %d bytes and two refused ones, the receiver "
                   "got a %d-byte message", kAp7Declared, (int)got.size());
}

SRT_CASE(g07_msgsizing, a_receive_into_a_buffer_shorter_than_the_declared_size_is_refused) {
    ApLink L;
    ap_connect(L, &ap_make_with_srt_socket, ap_live_declared_1000, ap_live_declared_1000);

    std::string sent = ap_send_one(L.cal, 0, kAp7Declared);

    // Bound the refusals the same way as a real receive, so that a library
    // which blocked instead of refusing costs a timeout rather than the case.
    ap_set_int(L.acc, SRTO_RCVTIMEO, 5000, "SRTO_RCVTIMEO");
    std::vector<char> buf((size_t)kApLiveMaxPayload, '\0');

    const int shortlen[2] = {kAp7Declared - 1, 100};
    for (int i = 0; i < 2; i++) {
        char what[128];
        std::snprintf(what, sizeof what,
                      "srt_recvmsg into a %d-byte buffer with the declared payload size at %d",
                      shortlen[i], kAp7Declared);
        AP_REFUSED(ap_try_recvmsg(L.acc, &buf[0], shortlen[i]), SRT_EINVALMSGAPI, what);
    }

    // Positive control, and the proof that nothing was truncated away: a
    // buffer of exactly the declared size is accepted and the message is whole.
    ApCall ok = ap_try_recvmsg(L.acc, &buf[0], kAp7Declared);
    AP_ACCEPTED(ok, "srt_recvmsg into a buffer of exactly the declared payload size");
    ST_REQUIRE_MSG(ok.rc == kAp7Declared,
                   "srt_recvmsg into a %d-byte buffer returned %d bytes", kAp7Declared, ok.rc);
    ST_REQUIRE_MSG(std::string(&buf[0], (size_t)ok.rc) == sent,
                   "the message that survived the two refused receives came back different: "
                   "expected %d bytes beginning \"%.11s\", got %d bytes beginning \"%.11s\"",
                   (int)sent.size(), sent.c_str(), ok.rc, &buf[0]);
}

SRT_CASE(g07_msgsizing, a_zero_length_receive_is_a_bad_parameter) {
    ApLink L;
    ap_connect(L, &ap_make_with_srt_socket, ap_live_declared_1000, ap_live_declared_1000);

    std::string sent = ap_send_one(L.cal, 7, kAp7Declared);
    ap_set_int(L.acc, SRTO_RCVTIMEO, 5000, "SRTO_RCVTIMEO");
    std::vector<char> buf((size_t)kApLiveMaxPayload, '\0');

    AP_REFUSED(ap_try_recvmsg(L.acc, &buf[0], 0), SRT_EINVPARAM,
               "srt_recvmsg with a zero-length buffer");

    // A zero-length send is not the same call and is documented not to fail:
    // asserting it here keeps the group from claiming a symmetry it does not
    // have, and keeps the refusal above about the receive side alone.
    ApCall zsend = ap_try_sendmsg(L.cal, &buf[0], 0);
    ST_REQUIRE_MSG(zsend.rc == 0,
                   "srt_sendmsg with a zero-length buffer: expected 0, got %d (%s)", zsend.rc,
                   ap_errname(zsend.err));

    ApCall ok = ap_try_recvmsg(L.acc, &buf[0], kAp7Declared);
    AP_ACCEPTED(ok, "srt_recvmsg into a buffer of exactly the declared payload size");
    ST_REQUIRE_MSG(std::string(&buf[0], (size_t)(ok.rc > 0 ? ok.rc : 0)) == sent,
                   "after the refused zero-length receive the message came back different: "
                   "expected %d bytes, got %d bytes", (int)sent.size(), ok.rc);
}

SRT_CASE(g07_msgsizing, the_live_default_declares_the_same_boundary) {
    ApLink L;
    ap_connect(L, &ap_make_with_srt_socket, ap_live_lossless, ap_live_lossless);

    std::vector<char> buf((size_t)4096, '\0');
    std::string sent = ap_send_one(L.cal, 3, kApLiveDefPayload);

    AP_REFUSED(ap_try_sendmsg(L.cal, &buf[0], 1400), SRT_EINVALMSGAPI,
               "srt_sendmsg of 1400 bytes at the live default payload size");

    ap_set_int(L.acc, SRTO_RCVTIMEO, 5000, "SRTO_RCVTIMEO");
    AP_REFUSED(ap_try_recvmsg(L.acc, &buf[0], 100), SRT_EINVALMSGAPI,
               "srt_recvmsg into a 100-byte buffer at the live default payload size");

    ApCall ok = ap_try_recvmsg(L.acc, &buf[0], kApLiveDefPayload);
    AP_ACCEPTED(ok, "srt_recvmsg into a buffer of exactly the live default payload size");
    ST_REQUIRE_MSG(ok.rc == kApLiveDefPayload,
                   "srt_recvmsg into a %d-byte buffer returned %d bytes", kApLiveDefPayload,
                   ok.rc);
    ST_REQUIRE_MSG(std::string(&buf[0], (size_t)ok.rc) == sent,
                   "the %d-byte message came back different: expected \"%.11s\", got \"%.11s\"",
                   kApLiveDefPayload, sent.c_str(), &buf[0]);
}

// ========================================================= g08_optslots
//
// D2 and D5. Two refusals with different codes for the same slot, and the two
// per-direction key-length options retired. interface.md says a program that
// names the retired pair must still compile, so they are named through the
// header's own deprecated constants rather than through invented numbers --
// the deprecation attribute is silenced here and nowhere else.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static const SRT_SOCKOPT kApRetiredSndKeyLen = SRTO_SNDPBKEYLEN;
static const SRT_SOCKOPT kApRetiredRcvKeyLen = SRTO_RCVPBKEYLEN;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// interface.md: g08_optslots probes the literal id 99, which this
// specification's enumeration must leave unassigned.
static const SRT_SOCKOPT kApUndefinedSlot = (SRT_SOCKOPT)99;

SRT_CASE(g08_optslots, an_undefined_option_id_refuses_a_get_and_a_set_differently) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // Positive controls: an id this specification does define answers both
    // calls, so the refusals below are about the id and not about the socket.
    AP_ACCEPTED(ap_try_set_int(s, SRTO_RCVTIMEO, 1234), "set SRTO_RCVTIMEO, a defined option");
    AP_ACCEPTED(ap_try_get(s, SRTO_RCVTIMEO), "get SRTO_RCVTIMEO, a defined option");
    int back = ap_get_int(s, SRTO_RCVTIMEO, "SRTO_RCVTIMEO");
    ST_REQUIRE_MSG(back == 1234,
                   "control: SRTO_RCVTIMEO read back %d after being set to 1234, so the "
                   "option surface is not answering at all", back);

    AP_REFUSED(ap_try_get(s, kApUndefinedSlot), SRT_EINVOP,
               "get of option id 99, which this specification does not define");
    AP_REFUSED(ap_try_set_int(s, kApUndefinedSlot, 1), SRT_EINVPARAM,
               "set of option id 99, which this specification does not define");

    // The asymmetry is the subject, so say it out loud: a library that refused
    // both the same way would satisfy neither line above but would also be
    // worth naming if the codes ever converged.
    ST_REQUIRE_MSG((int)SRT_EINVOP != (int)SRT_EINVPARAM,
                   "SRT_EINVOP and SRT_EINVPARAM are the same value (%d), so the asymmetric "
                   "refusal this group asserts cannot be observed", (int)SRT_EINVOP);
    srt_close(s);
}

// interface.md: the two slots this milestone removes outright, 32 and 37.
// Named by number for the same reason id 99 is -- what is asserted is the
// behaviour of the slot, not the spelling of a constant, and the surviving
// header aliases are deprecated.
static const SRT_SOCKOPT kApRemovedSlot32 = (SRT_SOCKOPT)32;
static const SRT_SOCKOPT kApRemovedSlot37 = (SRT_SOCKOPT)37;

SRT_CASE(g08_optslots, the_two_slots_this_milestone_removes_answer_like_undefined_ones) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // Positive control: a defined slot still answers both calls on this
    // socket, so the four refusals below are about the slots and not about a
    // socket that refuses everything.
    AP_ACCEPTED(ap_try_set_int(s, SRTO_RCVTIMEO, 1234), "set SRTO_RCVTIMEO, a defined option");
    AP_ACCEPTED(ap_try_get(s, SRTO_RCVTIMEO), "get SRTO_RCVTIMEO, a defined option");

    AP_REFUSED(ap_try_get(s, kApRemovedSlot32), SRT_EINVOP,
               "get of option id 32, which the previous milestone defined and this one removes");
    AP_REFUSED(ap_try_set_int(s, kApRemovedSlot32, 1), SRT_EINVPARAM,
               "set of option id 32, which the previous milestone defined and this one removes");
    AP_REFUSED(ap_try_get(s, kApRemovedSlot37), SRT_EINVOP,
               "get of option id 37, which the previous milestone defined and this one removes");
    AP_REFUSED(ap_try_set_int(s, kApRemovedSlot37, 1), SRT_EINVPARAM,
               "set of option id 37, which the previous milestone defined and this one removes");
    srt_close(s);
}

SRT_CASE(g08_optslots, the_retired_send_key_length_option_refuses_a_get) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    // The surviving single key-length option is the positive control: one key
    // length serves both directions now, and it still answers a get.
    AP_ACCEPTED(ap_try_get(s, SRTO_PBKEYLEN), "get SRTO_PBKEYLEN, the surviving key length");
    int keylen = ap_get_int(s, SRTO_PBKEYLEN, "SRTO_PBKEYLEN");
    ST_REQUIRE_MSG(keylen == 0,
                   "control: SRTO_PBKEYLEN on a fresh socket expected 0 (no encryption), got %d",
                   keylen);

    AP_REFUSED(ap_try_get(s, kApRetiredSndKeyLen), SRT_EINVOP,
               "get SRTO_SNDPBKEYLEN, the retired per-direction send key length");
    srt_close(s);
}

SRT_CASE(g08_optslots, the_retired_receive_key_length_option_refuses_a_get) {
    SRTSOCKET s = st_new_socket();
    ST_REQUIRE_MSG(s != SRT_INVALID_SOCK, "srt_socket: %s", srt_getlasterror_str());

    AP_ACCEPTED(ap_try_get(s, SRTO_KMSTATE), "get SRTO_KMSTATE, a defined key-material option");

    AP_REFUSED(ap_try_get(s, kApRetiredRcvKeyLen), SRT_EINVOP,
               "get SRTO_RCVPBKEYLEN, the retired per-direction receive key length");
    srt_close(s);
}

// ============================================================ g09_stats
//
// D12 and D13. srt_bistats is srt_bstats with one extra argument; with the
// instantaneous flag clear the cumulative counters must be the same numbers,
// and mbpsMaxBW must report the infinite-bandwidth ceiling rather than the
// negative artefact the previous milestone produced.
static bool ap_same_cumulative(const SRT_TRACEBSTATS& a, const SRT_TRACEBSTATS& b) {
    return a.pktSentTotal == b.pktSentTotal && a.pktRecvTotal == b.pktRecvTotal &&
           a.byteSentTotal == b.byteSentTotal && a.byteRecvTotal == b.byteRecvTotal;
}

// A connection whose counters are not moving, sampled bstats / bistats /
// bstats. The sandwich is what makes the comparison honest: if the two
// bracketing reads of the established call disagree, traffic moved between
// them and the middle sample could differ for that reason alone, so the
// attempt is discarded rather than reported. On a quiesced loopback link the
// first attempt settles; the retry exists so a loaded box cannot make a
// correct library look wrong.
static void ap_require_same_totals(const SRT_TRACEBSTATS& got, const SRT_TRACEBSTATS& want,
                                   const char* who, const char* how) {
    ST_REQUIRE_MSG(got.pktSentTotal == want.pktSentTotal,
                   "%s: srt_bistats %s reports pktSentTotal %lld, srt_bstats reports %lld",
                   who, how, (long long)got.pktSentTotal, (long long)want.pktSentTotal);
    ST_REQUIRE_MSG(got.pktRecvTotal == want.pktRecvTotal,
                   "%s: srt_bistats %s reports pktRecvTotal %lld, srt_bstats reports %lld",
                   who, how, (long long)got.pktRecvTotal, (long long)want.pktRecvTotal);
    ST_REQUIRE_MSG(got.byteSentTotal == want.byteSentTotal,
                   "%s: srt_bistats %s reports byteSentTotal %llu, srt_bstats reports %llu",
                   who, how, (unsigned long long)got.byteSentTotal,
                   (unsigned long long)want.byteSentTotal);
    ST_REQUIRE_MSG(got.byteRecvTotal == want.byteRecvTotal,
                   "%s: srt_bistats %s reports byteRecvTotal %llu, srt_bstats reports %llu",
                   who, how, (unsigned long long)got.byteRecvTotal,
                   (unsigned long long)want.byteRecvTotal);
}

static void ap_require_bistats_agrees(SRTSOCKET s, const char* who) {
    SRT_TRACEBSTATS before, mid, inst, after;
    bool settled = false;
    const int kAttempts = 40;
    for (int i = 0; i < kAttempts && !settled; i++) {
        std::memset(&before, 0, sizeof before);
        std::memset(&mid, 0, sizeof mid);
        std::memset(&inst, 0, sizeof inst);
        std::memset(&after, 0, sizeof after);
        ST_SRT_OK(srt_bstats(s, &before, 0));
        ST_SRT_OK(srt_bistats(s, &mid, 0, 0));
        // The instantaneous flag is the only thing srt_bistats adds over
        // srt_bstats, so a family that never sets it never asserts the
        // difference.  interface.md: the flag changes only the
        // buffer-occupancy fields and touches no *Total counter.  Those
        // fields are moving averages, so what is asserted is the negative --
        // the cumulative counters are the same numbers with the flag set --
        // and never a value of the fields the flag does move.
        ST_SRT_OK(srt_bistats(s, &inst, 0, 1));
        ST_SRT_OK(srt_bstats(s, &after, 0));
        settled = ap_same_cumulative(before, after);
    }
    ST_REQUIRE_MSG(settled,
                   "%s: the cumulative counters never held still across %d back-to-back "
                   "reads of srt_bstats, so the comparison with srt_bistats could not be "
                   "made", who, kAttempts);

    ap_require_same_totals(mid, before, who, "with its instantaneous flag clear");
    ap_require_same_totals(inst, before, who, "with its instantaneous flag set");
}

static const int kAp9Count = 40;

// Puts a known amount of traffic on the link, so that "the same counters"
// cannot be satisfied by two structures full of zeroes.
static void ap_run_stream(ApLink& L) {
    std::vector<std::string> sent = st_send_stream(L.cal, kAp9Count, kApLiveDefPayload, 1000);
    std::vector<std::string> got = st_recv_stream(L.acc, kAp9Count, kApLiveDefPayload, 5000);
    ST_REQUIRE_MSG((int)got.size() == kAp9Count, "the link delivered %d of %d messages",
                   (int)got.size(), kAp9Count);
    ST_REQUIRE_MSG(st_first_out_of_order(sent, got) == -1,
                   "the link delivered message %d out of order",
                   st_first_out_of_order(sent, got));
}

SRT_CASE(g09_stats, the_two_statistics_calls_agree_on_the_senders_cumulative_counters) {
    ApLink L;
    ap_connect(L, &ap_make_with_srt_socket, ap_live_lossless, ap_live_lossless);
    ap_run_stream(L);

    SRT_TRACEBSTATS probe;
    std::memset(&probe, 0, sizeof probe);
    ST_SRT_OK(srt_bistats(L.cal, &probe, 0, 0));
    ST_REQUIRE_MSG(probe.pktSentTotal >= (int64_t)kAp9Count,
                   "the sender reports pktSentTotal %lld after %d messages, so there is no "
                   "traffic for the two calls to agree about",
                   (long long)probe.pktSentTotal, kAp9Count);
    ST_REQUIRE_MSG(probe.byteSentTotal >= (uint64_t)(kAp9Count * kApLiveDefPayload),
                   "the sender reports byteSentTotal %llu after %d messages of %d bytes",
                   (unsigned long long)probe.byteSentTotal, kAp9Count, kApLiveDefPayload);

    ap_require_bistats_agrees(L.cal, "the sending caller");
}

SRT_CASE(g09_stats, the_two_statistics_calls_agree_on_the_receivers_cumulative_counters) {
    ApLink L;
    ap_connect(L, &ap_make_with_srt_socket, ap_live_lossless, ap_live_lossless);
    ap_run_stream(L);

    SRT_TRACEBSTATS probe;
    std::memset(&probe, 0, sizeof probe);
    ST_SRT_OK(srt_bistats(L.acc, &probe, 0, 0));
    ST_REQUIRE_MSG(probe.pktRecvTotal >= (int64_t)kAp9Count,
                   "the receiver reports pktRecvTotal %lld after %d messages, so there is no "
                   "traffic for the two calls to agree about",
                   (long long)probe.pktRecvTotal, kAp9Count);
    ST_REQUIRE_MSG(probe.byteRecvTotal >= (uint64_t)(kAp9Count * kApLiveDefPayload),
                   "the receiver reports byteRecvTotal %llu after %d messages of %d bytes",
                   (unsigned long long)probe.byteRecvTotal, kAp9Count, kApLiveDefPayload);

    ap_require_bistats_agrees(L.acc, "the receiving accepted socket");
}

// A second link whose sender declares an absolute bandwidth limit before it
// connects. The option's documented binding is "pre", so the limit is set on
// a fresh socket rather than on a connected one.
static const int64_t kAp9LimitBps = 1250000;  // bytes/s -> 10 Mbps

static void ap_live_limited(SRTSOCKET s) {
    ap_live_lossless(s);
    if (srt_setsockflag(s, SRTO_MAXBW, &kAp9LimitBps, sizeof kAp9LimitBps) == SRT_ERROR)
        st_failf(__FILE__, __LINE__, "set SRTO_MAXBW = %lld: %s", (long long)kAp9LimitBps,
                 srt_getlasterror_str());
}

SRT_CASE(g09_stats, both_calls_report_the_infinite_bandwidth_ceiling_at_the_default_setting) {
    ApLink L;
    ap_connect(L, &ap_make_with_srt_socket, ap_live_lossless, ap_live_lossless);

    int64_t maxbw = 0;
    int n = (int)sizeof maxbw;
    ST_REQUIRE_MSG(srt_getsockflag(L.cal, SRTO_MAXBW, &maxbw, &n) != SRT_ERROR,
                   "get SRTO_MAXBW: %s", srt_getlasterror_str());
    ST_REQUIRE_MSG(maxbw == -1,
                   "SRTO_MAXBW on a connected socket that never set it: expected the "
                   "documented -1 default, got %lld -- the rest of this case is about that "
                   "default", (long long)maxbw);

    // The ceiling is reported by the congestion control in force, which only
    // exists once the socket is connected, so put the connection to work first.
    ap_send_one(L.cal, 0, kApLiveDefPayload);
    std::string got = ap_recv_one(L.acc, kApLiveDefPayload, 5000);
    ST_REQUIRE_MSG((int)got.size() == kApLiveDefPayload,
                   "the connection carried %d bytes, expected %d", (int)got.size(),
                   kApLiveDefPayload);

    const double kCeiling = 30.0;  // the infinite-bandwidth ceiling, 30 Mbps
    SRT_TRACEBSTATS b, i;
    std::memset(&b, 0, sizeof b);
    std::memset(&i, 0, sizeof i);
    ST_SRT_OK(srt_bstats(L.cal, &b, 0));
    ST_SRT_OK(srt_bistats(L.cal, &i, 0, 0));

    ST_REQUIRE_MSG(b.mbpsMaxBW > 0.0,
                   "srt_bstats().mbpsMaxBW at the default SRTO_MAXBW: expected the "
                   "infinite-bandwidth ceiling %.1f, got %.8f -- not a negative number and "
                   "not zero", kCeiling, b.mbpsMaxBW);
    ST_REQUIRE_MSG(std::fabs(b.mbpsMaxBW - kCeiling) < 0.001,
                   "srt_bstats().mbpsMaxBW at the default SRTO_MAXBW: expected %.1f, got %.8f",
                   kCeiling, b.mbpsMaxBW);
    ST_REQUIRE_MSG(std::fabs(i.mbpsMaxBW - kCeiling) < 0.001,
                   "srt_bistats().mbpsMaxBW at the default SRTO_MAXBW: expected %.1f, got %.8f",
                   kCeiling, i.mbpsMaxBW);

    // Positive control on a second connection: the field reports the ceiling
    // actually in force, so a sender that declared one must report it. Without
    // this, a library that hard-coded 30.0 into the field would pass.
    ApLink M;
    ap_connect(M, &ap_make_with_srt_socket, ap_live_lossless, ap_live_limited);
    ap_send_one(M.cal, 1, kApLiveDefPayload);
    std::string got2 = ap_recv_one(M.acc, kApLiveDefPayload, 5000);
    ST_REQUIRE_MSG((int)got2.size() == kApLiveDefPayload,
                   "the bandwidth-limited connection carried %d bytes, expected %d",
                   (int)got2.size(), kApLiveDefPayload);

    SRT_TRACEBSTATS c;
    std::memset(&c, 0, sizeof c);
    ST_SRT_OK(srt_bistats(M.cal, &c, 0, 0));
    ST_REQUIRE_MSG(std::fabs(c.mbpsMaxBW - 10.0) < 0.001,
                   "control: with SRTO_MAXBW set to %lld bytes/s before connect the ceiling "
                   "should read 10.0 Mbps, got %.8f -- the field is not reporting the "
                   "ceiling in force", (long long)kAp9LimitBps, c.mbpsMaxBW);
}
