///////////////////////////////////////////////////////////////////////////////
// Characterisation cases for the pure parts of ise_socket.h: address
// conversion and InetAddress.
//
// Nothing here opens a socket or touches the network -- the wire suite already
// covers the servers end to end. What the wire suite cannot reach is the
// address arithmetic, which is where the byte-order mistakes live.
//
// getLocalIp(), getLocalIpList() and lookupHostAddr() are deliberately absent:
// their answers are properties of the machine and of DNS, not of the library.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <string>

using namespace ise;
using drv::out;
using drv::outf;
using drv::esc;

DRV_CASE(socket_ip_to_string)
{
    // Host byte order in, dotted quad out. The boundary values are the ones a
    // byte-order slip shows up in.
    static const UINT IPS[] =
    {
        0x00000000u, 0x7F000001u, 0xC0A80001u, 0xFFFFFFFFu,
        0x01020304u, 0x04030201u, 0x0A000001u, 0x000000FFu,
        0xFF000000u, 0x80000000u, 0x0000FF00u, 0x00FF0000u,
        0xE0000001u, 0xAC100001u
    };

    for (int i = 0; i < DRV_COUNT(IPS); i++)
        outf("0x%08X -> [%s]", IPS[i], ipToString(IPS[i]).c_str());
}

DRV_CASE(socket_string_to_ip)
{
    static const char *INPUTS[] =
    {
        "0.0.0.0",
        "127.0.0.1",
        "192.168.0.1",
        "255.255.255.255",
        "1.2.3.4",
        "10.0.0.1",
        // Forms inet_addr historically accepted but a strict parser would not.
        "1.2.3",
        "1.2",
        "1",
        "010.020.030.040",
        "0x7f.0.0.1",
        "1.2.3.4.5",
        // Outright malformed.
        "",
        " ",
        "...",
        "256.1.1.1",
        "-1.0.0.0",
        "1.2.3.-4",
        "a.b.c.d",
        "1.2.3.4 ",
        " 1.2.3.4",
        "1.2.3.4\n",
        "localhost",
        "::1",
        "99999999999999999999.1.1.1"
    };

    for (int i = 0; i < DRV_COUNT(INPUTS); i++)
    {
        UINT ip = stringToIp(INPUTS[i]);
        outf("[%s] -> 0x%08X -> [%s]", esc(INPUTS[i]).c_str(), ip,
            ipToString(ip).c_str());
    }
}

DRV_CASE(socket_ip_roundtrip)
{
    // Every dotted quad the formatter produces must parse back to the same
    // number. Walking one octet at a time over all 256 values, four times,
    // covers every byte position without a 4-billion-iteration loop.
    bool allSame = true;
    int checked = 0;
    for (int shift = 0; shift < 32; shift += 8)
    {
        for (int value = 0; value < 256; value++)
        {
            UINT ip = (0x0A0B0C0Du & ~(0xFFu << shift)) |
                      (static_cast<UINT>(value) << shift);
            if (stringToIp(ipToString(ip)) != ip) allSame = false;
            checked++;
        }
    }
    outf("round-tripped %d addresses, all identical: %d", checked,
        (allSame ? 1 : 0));
}

DRV_CASE(socket_inet_address)
{
    InetAddress empty;
    outf("default: ip=0x%08X port=%d isEmpty=%d display=[%s]",
        empty.ip, static_cast<int>(empty.port), (empty.isEmpty() ? 1 : 0),
        empty.getDisplayStr().c_str());

    InetAddress numeric(0x7F000001u, 8080);
    outf("numeric: display=[%s] isEmpty=%d",
        numeric.getDisplayStr().c_str(), (numeric.isEmpty() ? 1 : 0));

    InetAddress fromString(string("192.168.1.100"), 443);
    outf("from string: ip=0x%08X display=[%s]",
        fromString.ip, fromString.getDisplayStr().c_str());

    // A port of zero with a non-zero address is not "empty".
    InetAddress portZero(0x08080808u, 0);
    outf("port zero: isEmpty=%d display=[%s]",
        (portZero.isEmpty() ? 1 : 0), portZero.getDisplayStr().c_str());

    // An address of zero with a non-zero port is not "empty" either.
    InetAddress ipZero(0u, 99);
    outf("ip zero: isEmpty=%d display=[%s]",
        (ipZero.isEmpty() ? 1 : 0), ipZero.getDisplayStr().c_str());

    // The port is a WORD, so the values either side of 65535 are worth pinning.
    static const int PORTS[] = { 0, 1, 80, 1024, 32767, 32768, 65534, 65535 };
    for (int i = 0; i < DRV_COUNT(PORTS); i++)
    {
        InetAddress address(0x01020304u, static_cast<WORD>(PORTS[i]));
        outf("port %d -> [%s]", PORTS[i], address.getDisplayStr().c_str());
    }

    outf("equal to itself: %d", (numeric == numeric ? 1 : 0));
    InetAddress same(0x7F000001u, 8080);
    outf("equal to a twin: %d, unequal: %d",
        (numeric == same ? 1 : 0), (numeric != same ? 1 : 0));
    InetAddress otherPort(0x7F000001u, 8081);
    outf("differing port: == %d != %d",
        (numeric == otherPort ? 1 : 0), (numeric != otherPort ? 1 : 0));
    InetAddress otherIp(0x7F000002u, 8080);
    outf("differing ip: == %d != %d",
        (numeric == otherIp ? 1 : 0), (numeric != otherIp ? 1 : 0));

    numeric.clear();
    outf("after clear: isEmpty=%d display=[%s]",
        (numeric.isEmpty() ? 1 : 0), numeric.getDisplayStr().c_str());
}

DRV_CASE(socket_sockaddr_roundtrip)
{
    // getSockAddr() converts to network byte order and the SockAddr
    // constructor converts back. A slip in either direction shows up as a
    // byte-swapped address, which the round trip would hide -- so the network
    // form is inspected directly as well.
    InetAddress source(0x01020304u, 0x1234);
    SockAddr raw = source.getSockAddr();

    const unsigned char *bytes =
        reinterpret_cast<const unsigned char*>(&raw.sin_addr.s_addr);
    outf("network address bytes: %02X %02X %02X %02X",
        bytes[0], bytes[1], bytes[2], bytes[3]);

    const unsigned char *portBytes =
        reinterpret_cast<const unsigned char*>(&raw.sin_port);
    outf("network port bytes: %02X %02X", portBytes[0], portBytes[1]);
    outf("family is AF_INET: %d", (raw.sin_family == AF_INET ? 1 : 0));

    InetAddress back(raw);
    outf("round trip: ip=0x%08X port=%d identical=%d",
        back.ip, static_cast<int>(back.port), (back == source ? 1 : 0));

    // The padding must be zeroed, or a comparison of two sockaddrs by bytes
    // would depend on stack rubbish.
    bool zeroed = true;
    for (size_t i = 0; i < sizeof(raw.sin_zero); i++)
        if (raw.sin_zero[i] != 0) zeroed = false;
    outf("padding zeroed: %d", (zeroed ? 1 : 0));
}

DRV_CASE(socket_error_messages)
{
    // The text is the project's own, not strerror()'s: ise_socket.h defines a
    // SSEM_ string per code and iseSocketGetErrorMsg switches over all of them.
    // So the whole answer is scoreable by value, and it should be -- a
    // thirty-seven arm switch over macros is exactly the shape a migration
    // turns into a table, and a table is exactly where an arm goes missing.
    //
    // The codes are named rather than written as numbers because the names are
    // what the switch is written in; a migration that keeps the names and
    // renumbers nothing still has to produce these strings.
    struct Named { const char *name; int code; };
    static const Named CODES[] =
    {
        { "SS_EINTR",           SS_EINTR },
        { "SS_EBADF",           SS_EBADF },
        { "SS_EACCES",          SS_EACCES },
        { "SS_EFAULT",          SS_EFAULT },
        { "SS_EINVAL",          SS_EINVAL },
        { "SS_EMFILE",          SS_EMFILE },
        { "SS_EWOULDBLOCK",     SS_EWOULDBLOCK },
        { "SS_EINPROGRESS",     SS_EINPROGRESS },
        { "SS_EALREADY",        SS_EALREADY },
        { "SS_ENOTSOCK",        SS_ENOTSOCK },
        { "SS_EDESTADDRREQ",    SS_EDESTADDRREQ },
        { "SS_EMSGSIZE",        SS_EMSGSIZE },
        { "SS_EPROTOTYPE",      SS_EPROTOTYPE },
        { "SS_ENOPROTOOPT",     SS_ENOPROTOOPT },
        { "SS_EPROTONOSUPPORT", SS_EPROTONOSUPPORT },
        { "SS_ESOCKTNOSUPPORT", SS_ESOCKTNOSUPPORT },
        { "SS_EOPNOTSUPP",      SS_EOPNOTSUPP },
        { "SS_EPFNOSUPPORT",    SS_EPFNOSUPPORT },
        { "SS_EAFNOSUPPORT",    SS_EAFNOSUPPORT },
        { "SS_EADDRINUSE",      SS_EADDRINUSE },
        { "SS_EADDRNOTAVAIL",   SS_EADDRNOTAVAIL },
        { "SS_ENETDOWN",        SS_ENETDOWN },
        { "SS_ENETUNREACH",     SS_ENETUNREACH },
        { "SS_ENETRESET",       SS_ENETRESET },
        { "SS_ECONNABORTED",    SS_ECONNABORTED },
        { "SS_ECONNRESET",      SS_ECONNRESET },
        { "SS_ENOBUFS",         SS_ENOBUFS },
        { "SS_EISCONN",         SS_EISCONN },
        { "SS_ENOTCONN",        SS_ENOTCONN },
        { "SS_ESHUTDOWN",       SS_ESHUTDOWN },
        { "SS_ETOOMANYREFS",    SS_ETOOMANYREFS },
        { "SS_ETIMEDOUT",       SS_ETIMEDOUT },
        { "SS_ECONNREFUSED",    SS_ECONNREFUSED },
        { "SS_ELOOP",           SS_ELOOP },
        { "SS_ENAMETOOLONG",    SS_ENAMETOOLONG },
        { "SS_EHOSTDOWN",       SS_EHOSTDOWN },
        { "SS_EHOSTUNREACH",    SS_EHOSTUNREACH },
        { "SS_ENOTEMPTY",       SS_ENOTEMPTY }
    };

    for (int i = 0; i < DRV_COUNT(CODES); i++)
        outf("%-20s [%s]", CODES[i].name,
            esc(iseSocketGetErrorMsg(CODES[i].code)).c_str());

    // Codes with no arm fall through to the empty string the function starts
    // with, so the wrapper still formats a message. Zero and a negative are
    // the two a caller reaches by handing on an unchecked return value.
    static const int UNLISTED[] = { 0, -1, 3, 999999, 2147483647 };
    for (int i = 0; i < DRV_COUNT(UNLISTED); i++)
        outf("unlisted %-11d [%s]", UNLISTED[i],
            esc(iseSocketGetErrorMsg(UNLISTED[i])).c_str());

    // Two arms share a value on glibc only if the C library made them equal;
    // the switch would not compile if they did, so this records that the
    // library the project was built against keeps them apart.
    int collisions = 0;
    for (int i = 0; i < DRV_COUNT(CODES); i++)
        for (int j = i + 1; j < DRV_COUNT(CODES); j++)
            if (CODES[i].code == CODES[j].code)
                collisions++;
    outf("codes sharing a value: %d", collisions);
}

DRV_CASE(socket_network_init)
{
    // On Linux these are no-ops that maintain a flag, and the flag's behaviour
    // under repeated calls is what a migration to a standard mechanism could
    // change.
    outf("initialised at startup: %d", (isNetworkInited() ? 1 : 0));

    networkInitialize();
    outf("after initialize: %d", (isNetworkInited() ? 1 : 0));

    networkInitialize();
    outf("after second initialize: %d", (isNetworkInited() ? 1 : 0));

    ensureNetworkInited();
    outf("after ensure: %d", (isNetworkInited() ? 1 : 0));
}
