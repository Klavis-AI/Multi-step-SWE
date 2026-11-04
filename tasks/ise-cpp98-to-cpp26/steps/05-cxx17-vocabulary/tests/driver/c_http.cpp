///////////////////////////////////////////////////////////////////////////////
// Characterisation cases for ise_http.h: the header list, the three header-info
// layers, and the request/response objects.
//
// These are the parts of the HTTP stack that can be exercised without a
// socket. HttpClient itself needs a peer and belongs to the wire suite.
//
// The header machinery is the hand-rolled part of the project most likely to
// be rewritten during a migration -- it is string splitting, case-insensitive
// name matching and list reordering, all of which the standard library now
// does -- so its edges are pinned closely.
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <string>

using namespace ise;
using drv::out;
using drv::outf;
using drv::esc;

namespace
{

// A header list rendered on one line, so a case that changes one entry shows
// the whole list either side of the change.
string dumpHeaders(const HttpHeaderStrList& headers)
{
    string result;
    for (int i = 0; i < headers.getCount(); i++)
    {
        if (i > 0) result += " | ";
        result += headers.getString(i);
    }
    return result;
}

// The bytes a Buffer holds, as a string.
string dumpBuffer(const Buffer& buffer)
{
    return string(static_cast<const char*>(buffer.data()), buffer.getSize());
}

// The nine range triples the two Content-Range cases share: each corner of the
// two predicates that decide whether the header is emitted at all, and each
// corner of the two that decide whether a half falls back to '*'.
struct RangeCase { INT64 start; INT64 end; INT64 instance; };
const RangeCase RANGE_CASES[] =
{
    { 0, 0, 0 },
    { 0, 499, 1234 },
    { 1, 65536, 102400 },
    { 0, 0, 102400 },
    { 1, 65536, 0 },
    { 0, 1, 0 },
    { -1, 10, 20 },
    { 5, -1, 20 },
    { 5, 10, -1 }
};

} // namespace

// getHttpProtoVerStr, getHttpMethodStr and getHttpStatusMessage are defined in
// ise_http.cpp and declared in no header. The two of them that reach the wire
// are pinned through the public calls that use them -- setStatusCode for the
// status table, the request line for the version table -- rather than by
// declaring them here, because a step that gives a file-local function
// internal linkage would otherwise break this file's link rather than its
// expectations.

DRV_CASE(http_proto_version_strings)
{
    // The version string appears in the request line, which is the only place
    // a caller can see it.
    // Both enumerators and nothing else. HTTP_PROTO_VER names 0 and 1, so its
    // value range is one bit wide and every other value is an indeterminate
    // load, not an unknown version -- a row casting 7 in here reached
    // getHttpProtoVerStr's default arm only by undefined behaviour, which
    // sanitize.py reported and which no expectation may rest on (NOTES B17).
    // That default arm is therefore unreachable in a defined program.
    static const HTTP_PROTO_VER VERSIONS[] =
        { DRV_ENUM(HTTP_PROTO_VER, HPV_1_0), DRV_ENUM(HTTP_PROTO_VER, HPV_1_1) };
    static const char *NAMES[] = { "HPV_1_0", "HPV_1_1" };

    for (int i = 0; i < DRV_COUNT(VERSIONS); i++)
    {
        HttpRequest request;
        request.setMethod("GET");
        request.setUrl("/");
        request.setProtocolVersion(VERSIONS[i]);
        request.setCacheControl("");
        request.setPragma("");
        request.setAccept("");
        request.setUserAgent("");
        request.setConnection(string(""));
        Buffer buffer;
        request.makeRequestHeaderBuffer(buffer);
        outf("%s -> [%s]", NAMES[i], esc(dumpBuffer(buffer)).c_str());
    }
}

DRV_CASE(http_status_messages)
{
    // Every code the table names, in the order it names them, plus the
    // neighbours of each block so a mis-edited case label shows up. The table
    // is reached through setStatusCode, which is how a server reaches it.
    static const int CODES[] =
    {
        100, 101,
        200, 201, 202, 203, 204, 205, 206, 207,
        300, 301, 302, 303, 304, 305, 306,
        400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412,
        413, 414, 415, 416, 417, 418,
        500, 501, 502, 503, 504, 505, 506,
        0, -1, 999, 2147483647
    };

    for (int i = 0; i < DRV_COUNT(CODES); i++)
    {
        HttpResponse response;
        response.setStatusCode(CODES[i]);
        outf("%d -> [%s]", CODES[i], esc(response.getStatusLine()).c_str());
    }
}

DRV_CASE(http_header_list_basics)
{
    HttpHeaderStrList headers;
    outf("empty: count=%d text=[%s]",
        headers.getCount(), esc(headers.getText()).c_str());

    int first = headers.add("Host: example.com");
    outf("add -> %d", first);
    int second = headers.add("Content-Length: 42");
    outf("add -> %d", second);
    outf("count=%d", headers.getCount());
    outf("dump: [%s]", esc(dumpHeaders(headers)).c_str());

    outf("getString(0)=[%s] operator[](1)=[%s]",
        esc(headers.getString(0)).c_str(), esc(headers[1]).c_str());
    outf("getName(0)=[%s] getValue(0)=[%s]",
        esc(headers.getName(0)).c_str(), esc(headers.getValue(0)).c_str());
    outf("getName(1)=[%s] getValue(1)=[%s]",
        esc(headers.getName(1)).c_str(), esc(headers.getValue(1)).c_str());

    outf("text: [%s]", esc(headers.getText()).c_str());

    headers.del(0);
    outf("after del(0): [%s]", esc(dumpHeaders(headers)).c_str());

    headers.clear();
    outf("after clear: count=%d text=[%s]",
        headers.getCount(), esc(headers.getText()).c_str());
}

DRV_CASE(http_header_list_name_parsing)
{
    // A header line is split on the first colon, and both halves are trimmed.
    // These are the lines that decide whether a rewritten splitter behaves the
    // same way.
    static const char *LINES[] =
    {
        "Host: example.com",
        "Host:example.com",
        "Host:   example.com   ",
        "   Host : example.com",
        "Host:",
        "Host: ",
        ":value",
        ":",
        "",
        "   ",
        "NoColonAtAll",
        "Location: http://example.com:8080/a?b=c",
        "X-Empty-Name-After-Trim:  \t  ",
        "\tHost\t:\tvalue\t",
        "Set-Cookie: a=1; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT",
        "X-Weird: line\twith\ttabs",
        "A: b: c: d",
        "Content-Length: 0",
        "x-lower-case: v",
        "X-UPPER-CASE: V"
    };

    HttpHeaderStrList headers;
    for (int i = 0; i < DRV_COUNT(LINES); i++)
    {
        headers.clear();
        headers.add(LINES[i]);
        outf("[%s] -> name=[%s] value=[%s] text=[%s]",
            esc(LINES[i]).c_str(), esc(headers.getName(0)).c_str(),
            esc(headers.getValue(0)).c_str(), esc(headers.getText()).c_str());
    }
}

DRV_CASE(http_header_list_lookup)
{
    HttpHeaderStrList headers;
    headers.add("Host: example.com");
    headers.add("Content-Type: text/plain");
    headers.add("Content-Length: 7");
    headers.add("Host: second.example.com");

    // Name lookup is case-insensitive and stops at the first match, so a
    // duplicate name is reachable only by index.
    static const char *QUERIES[] =
    {
        "Host", "host", "HOST", "HoSt",
        "Content-Type", "content-type",
        "Content-Length",
        "Missing", "", " Host", "Host ", "Host:"
    };

    for (int i = 0; i < DRV_COUNT(QUERIES); i++)
        outf("indexOfName([%s]) = %d, getValue = [%s]",
            esc(QUERIES[i]).c_str(), headers.indexOfName(QUERIES[i]),
            esc(headers.getValue(QUERIES[i])).c_str());
}

DRV_CASE(http_header_list_set_value)
{
    HttpHeaderStrList headers;

    // setValue on a name that is not present appends it.
    headers.setValue("Host", "example.com");
    outf("after first set: [%s]", esc(dumpHeaders(headers)).c_str());

    // ... on one that is present rewrites the line in place, keeping position.
    headers.setValue("Content-Type", "text/plain");
    headers.setValue("Host", "other.example.com");
    outf("after rewrite: [%s]", esc(dumpHeaders(headers)).c_str());

    // The name is matched without regard to case but the *stored* spelling is
    // the one last supplied.
    headers.setValue("HOST", "third.example.com");
    outf("after case-different set: [%s]", esc(dumpHeaders(headers)).c_str());

    // An empty value deletes the entry rather than storing an empty one.
    headers.setValue("Host", "");
    outf("after empty value: [%s]", esc(dumpHeaders(headers)).c_str());

    // Deleting something that is not there is not an error.
    headers.setValue("Nothing", "");
    outf("after deleting a missing name: count=%d", headers.getCount());

    // Name and value are both trimmed before storage.
    headers.setValue("  Spaced  ", "  value  ");
    outf("after trimmed set: [%s]", esc(dumpHeaders(headers)).c_str());

    // A value that is only whitespace trims to empty, so it deletes too.
    headers.setValue("Spaced", "   ");
    outf("after whitespace value: count=%d", headers.getCount());
}

DRV_CASE(http_header_list_add_strings_and_move)
{
    HttpHeaderStrList base;
    base.add("A: 1");
    base.add("B: 2");

    HttpHeaderStrList extra;
    extra.add("C: 3");
    extra.add("D: 4");

    base.addStrings(extra);
    outf("after addStrings: [%s]", esc(dumpHeaders(base)).c_str());

    // addStrings of an empty list is a no-op; of itself it would read the list
    // it is writing, which is why it gets its own case.
    HttpHeaderStrList nothing;
    base.addStrings(nothing);
    outf("after adding nothing: count=%d", base.getCount());

    // moveToTop walks its argument backwards, so the resulting order is the
    // argument's order.
    StrList order;
    order.add("D");
    order.add("B");
    base.moveToTop(order);
    outf("after moveToTop(D,B): [%s]", esc(dumpHeaders(base)).c_str());

    // Names that are not present are skipped silently.
    StrList missing;
    missing.add("Z");
    missing.add("A");
    missing.add("Y");
    base.moveToTop(missing);
    outf("after moveToTop(Z,A,Y): [%s]", esc(dumpHeaders(base)).c_str());

    // An empty order list leaves everything alone.
    StrList none;
    base.moveToTop(none);
    outf("after moveToTop(): [%s]", esc(dumpHeaders(base)).c_str());

    // Every name, in reverse, is a full reversal of the list.
    StrList all;
    all.add("A");
    all.add("B");
    all.add("C");
    all.add("D");
    base.moveToTop(all);
    outf("after moveToTop(A,B,C,D): [%s]", esc(dumpHeaders(base)).c_str());
}

DRV_CASE(http_header_list_copy)
{
    HttpHeaderStrList source;
    source.add("A: 1");
    source.add("B: 2");

    HttpHeaderStrList copy(source);
    outf("copy: [%s]", esc(dumpHeaders(copy)).c_str());

    copy.add("C: 3");
    outf("after adding to the copy: source=[%s] copy=[%s]",
        esc(dumpHeaders(source)).c_str(), esc(dumpHeaders(copy)).c_str());

    HttpHeaderStrList assigned;
    assigned.add("Z: 26");
    assigned = source;
    outf("assigned: [%s]", esc(dumpHeaders(assigned)).c_str());
}

DRV_CASE(http_entity_header_defaults)
{
    HttpEntityHeaderInfo info;
    outf("cacheControl=[%s]", esc(info.getCacheControl()).c_str());
    outf("connection=[%s]", esc(info.getConnection()).c_str());
    outf("contentType=[%s]", esc(info.getContentType()).c_str());
    outf("pragma=[%s]", esc(info.getPragma()).c_str());
    outf("contentLength=%s", intToStr(info.getContentLength()).c_str());
    outf("contentRange start=%s end=%s instance=%s",
        intToStr(info.getContentRangeStart()).c_str(),
        intToStr(info.getContentRangeEnd()).c_str(),
        intToStr(info.getContentRangeInstanceLength()).c_str());
    outf("hasContentLength=%d hasContentRange=%d hasContentRangeInstance=%d",
        (info.hasContentLength() ? 1 : 0), (info.hasContentRange() ? 1 : 0),
        (info.hasContentRangeInstance() ? 1 : 0));
    outf("rawHeaders=%d customHeaders=%d",
        info.getRawHeaders().getCount(), info.getCustomHeaders().getCount());

    // The three has* predicates read three different comparisons, so each gets
    // its boundary walked.
    static const int LENGTHS[] = { -2, -1, 0, 1 };
    for (int i = 0; i < DRV_COUNT(LENGTHS); i++)
    {
        info.setContentLength(LENGTHS[i]);
        outf("contentLength %d -> hasContentLength=%d",
            LENGTHS[i], (info.hasContentLength() ? 1 : 0));
    }
    for (int i = 0; i < DRV_COUNT(LENGTHS); i++)
    {
        info.setContentRangeEnd(LENGTHS[i]);
        outf("contentRangeEnd %d -> hasContentRange=%d",
            LENGTHS[i], (info.hasContentRange() ? 1 : 0));
    }
    for (int i = 0; i < DRV_COUNT(LENGTHS); i++)
    {
        info.setContentRangeInstanceLength(LENGTHS[i]);
        outf("contentRangeInstanceLength %d -> hasContentRangeInstance=%d",
            LENGTHS[i], (info.hasContentRangeInstance() ? 1 : 0));
    }

    // setConnection has a bool overload that picks the two token spellings.
    info.setConnection(true);
    outf("setConnection(true) -> [%s]", esc(info.getConnection()).c_str());
    info.setConnection(false);
    outf("setConnection(false) -> [%s]", esc(info.getConnection()).c_str());

    info.clear();
    outf("after clear: connection=[%s] contentLength=%s",
        esc(info.getConnection()).c_str(),
        intToStr(info.getContentLength()).c_str());
}

DRV_CASE(http_entity_header_parse)
{
    HttpEntityHeaderInfo info;
    HttpHeaderStrList& raw = info.getRawHeaders();
    raw.add("Cache-control: max-age=0");
    raw.add("Connection: keep-alive");
    raw.add("Content-Version: 1.2");
    raw.add("Content-Disposition: attachment; filename=\"x.txt\"");
    raw.add("Content-Encoding: gzip");
    raw.add("Content-Language: en-GB");
    raw.add("Content-Type: application/json; charset=utf-8");
    raw.add("Content-Length: 1234");
    raw.add("Date: Thu, 01 Jan 1970 00:00:00 GMT");
    raw.add("Last-Modified: Fri, 02 Jan 1970 00:00:00 GMT");
    raw.add("Expires: Sat, 03 Jan 1970 00:00:00 GMT");
    raw.add("ETag: \"deadbeef\"");
    raw.add("Pragma: no-cache");
    raw.add("Transfer-Encoding: chunked");
    info.parseHeaders();

    outf("cacheControl=[%s]", esc(info.getCacheControl()).c_str());
    outf("connection=[%s]", esc(info.getConnection()).c_str());
    outf("contentVersion=[%s]", esc(info.getContentVersion()).c_str());
    outf("contentDisposition=[%s]", esc(info.getContentDisposition()).c_str());
    outf("contentEncoding=[%s]", esc(info.getContentEncoding()).c_str());
    outf("contentLanguage=[%s]", esc(info.getContentLanguage()).c_str());
    outf("contentType=[%s]", esc(info.getContentType()).c_str());
    outf("contentLength=%s", intToStr(info.getContentLength()).c_str());
    outf("date=[%s]", esc(info.getDate()).c_str());
    outf("lastModified=[%s]", esc(info.getLastModified()).c_str());
    outf("expires=[%s]", esc(info.getExpires()).c_str());
    outf("eTag=[%s]", esc(info.getETag()).c_str());
    outf("pragma=[%s]", esc(info.getPragma()).c_str());
    outf("transferEncoding=[%s]", esc(info.getTransferEncoding()).c_str());

    // Parsing an empty set clears every field rather than leaving the defaults
    // the constructor installed.
    HttpEntityHeaderInfo emptyInfo;
    emptyInfo.parseHeaders();
    outf("parse of nothing: cacheControl=[%s] connection=[%s] pragma=[%s] length=%s",
        esc(emptyInfo.getCacheControl()).c_str(),
        esc(emptyInfo.getConnection()).c_str(),
        esc(emptyInfo.getPragma()).c_str(),
        intToStr(emptyInfo.getContentLength()).c_str());
}

DRV_CASE(http_entity_header_content_range)
{
    // Content-Range is the one header with its own tokeniser, and the header
    // it must cope with comes off the wire in three shapes plus whatever a
    // broken peer sends.
    static const char *RANGES[] =
    {
        "bytes 1-65536/102400",
        "bytes 0-0/1",
        "bytes */102400",
        "bytes 1-65536/*",
        "bytes */*",
        "bytes 0-499/1234",
        "bytes 9223372036854775806-9223372036854775807/9223372036854775807",
        // Shapes a strict parser would reject.
        "bytes",
        "bytes ",
        "1-2/3",
        "bytes 1-2",
        "bytes -5/10",
        "bytes 5-/10",
        "bytes /10",
        "bytes 1-2/",
        "bytes a-b/c",
        "bytes 1-2/3 extra",
        "  bytes  1-2/3  ",
        "BYTES 1-2/3",
        ""
    };

    for (int i = 0; i < DRV_COUNT(RANGES); i++)
    {
        HttpEntityHeaderInfo info;
        HttpHeaderStrList& raw = info.getRawHeaders();
        raw.add(string("Content-Range: ") + RANGES[i]);
        info.parseHeaders();
        outf("[%s] -> start=%s end=%s instance=%s has=%d/%d",
            esc(RANGES[i]).c_str(),
            intToStr(info.getContentRangeStart()).c_str(),
            intToStr(info.getContentRangeEnd()).c_str(),
            intToStr(info.getContentRangeInstanceLength()).c_str(),
            (info.hasContentRange() ? 1 : 0),
            (info.hasContentRangeInstance() ? 1 : 0));
    }
}

DRV_CASE(http_entity_header_content_length_parsing)
{
    // Content-Length goes through strToInt64 with -1 as the failure value, so
    // what counts as a failure is worth pinning.
    static const char *LENGTHS[] =
    {
        "0", "1", "-1", "-2", "1234567890123456789",
        "9223372036854775807", "9223372036854775808",
        "", " ", "  42  ", "42abc", "abc", "0x10", "+7", "1e3", "4.5",
        "007", "999999999999999999999999"
    };

    for (int i = 0; i < DRV_COUNT(LENGTHS); i++)
    {
        HttpEntityHeaderInfo info;
        HttpHeaderStrList& raw = info.getRawHeaders();
        raw.add(string("Content-Length: ") + LENGTHS[i]);
        info.parseHeaders();
        outf("[%s] -> %s has=%d", esc(LENGTHS[i]).c_str(),
            intToStr(info.getContentLength()).c_str(),
            (info.hasContentLength() ? 1 : 0));
    }
}

DRV_CASE(http_entity_header_build)
{
    HttpEntityHeaderInfo info;
    info.setConnection(string("keep-alive"));
    info.setContentType("text/plain");
    info.setContentLength(99);
    info.buildHeaders();
    outf("defaults plus three: [%s]",
        esc(dumpHeaders(info.getRawHeaders())).c_str());

    // A field left empty contributes no line at all, and Content-Length is
    // gated on >= 0 rather than on emptiness.
    HttpEntityHeaderInfo sparse;
    sparse.setCacheControl("");
    sparse.setConnection(string(""));
    sparse.setPragma("");
    sparse.buildHeaders();
    outf("everything emptied: count=%d [%s]",
        sparse.getRawHeaders().getCount(),
        esc(dumpHeaders(sparse.getRawHeaders())).c_str());

    sparse.setContentLength(0);
    sparse.buildHeaders();
    outf("length zero: [%s]",
        esc(dumpHeaders(sparse.getRawHeaders())).c_str());

    sparse.setContentLength(-1);
    sparse.buildHeaders();
    outf("length -1: count=%d", sparse.getRawHeaders().getCount());

    // Custom headers are appended after the built ones, verbatim, without
    // going through setValue -- so a custom header may duplicate a built one.
    HttpHeaderStrList custom;
    custom.add("X-Custom: 1");
    custom.add("Connection: close");
    HttpEntityHeaderInfo withCustom;
    withCustom.setConnection(string("keep-alive"));
    withCustom.setCustomHeaders(custom);
    withCustom.buildHeaders();
    outf("with custom: [%s]",
        esc(dumpHeaders(withCustom.getRawHeaders())).c_str());

    // buildHeaders clears first, so calling it twice is not cumulative.
    withCustom.buildHeaders();
    outf("built twice: [%s]",
        esc(dumpHeaders(withCustom.getRawHeaders())).c_str());

    // A round trip through build and parse must not lose a field.
    HttpEntityHeaderInfo roundTrip;
    roundTrip.setConnection(string("keep-alive"));
    roundTrip.setContentType("application/octet-stream");
    roundTrip.setContentLength(4096);
    roundTrip.setETag("\"abc\"");
    roundTrip.setDate("Thu, 01 Jan 1970 00:00:00 GMT");
    roundTrip.buildHeaders();
    string built = roundTrip.getRawHeaders().getText();
    roundTrip.parseHeaders();
    outf("round trip: connection=[%s] type=[%s] length=%s eTag=[%s] date=[%s]",
        esc(roundTrip.getConnection()).c_str(),
        esc(roundTrip.getContentType()).c_str(),
        intToStr(roundTrip.getContentLength()).c_str(),
        esc(roundTrip.getETag()).c_str(),
        esc(roundTrip.getDate()).c_str());
    outf("built text: [%s]", esc(built).c_str());
}

DRV_CASE(http_request_header_info)
{
    HttpRequestHeaderInfo info;
    outf("default accept=[%s]", esc(info.getAccept()).c_str());
    outf("default userAgent=[%s]", esc(info.getUserAgent()).c_str());
    outf("default host=[%s] range=[%s] from=[%s] referer=[%s]",
        esc(info.getHost()).c_str(), esc(info.getRange()).c_str(),
        esc(info.getFrom()).c_str(), esc(info.getReferer()).c_str());

    HttpHeaderStrList& raw = info.getRawHeaders();
    raw.add("Host: example.com:8080");
    raw.add("Accept: text/html");
    raw.add("Accept-Charset: utf-8");
    raw.add("Accept-Encoding: gzip, deflate");
    raw.add("Accept-Language: en");
    raw.add("From: someone@example.com");
    raw.add("Referer: http://example.com/prev");
    raw.add("User-Agent: probe/1.0");
    raw.add("Range: bytes=100-200");
    info.parseHeaders();

    outf("host=[%s] accept=[%s] charset=[%s]",
        esc(info.getHost()).c_str(), esc(info.getAccept()).c_str(),
        esc(info.getAcceptCharSet()).c_str());
    outf("encoding=[%s] language=[%s]",
        esc(info.getAcceptEncoding()).c_str(),
        esc(info.getAcceptLanguage()).c_str());
    outf("from=[%s] referer=[%s] userAgent=[%s]",
        esc(info.getFrom()).c_str(), esc(info.getReferer()).c_str(),
        esc(info.getUserAgent()).c_str());
    outf("range after stripping bytes=: [%s]", esc(info.getRange()).c_str());

    // The Range parser drops everything up to the first '=', so headers
    // without one, or with several, are where it shows its shape.
    static const char *RANGES[] =
    {
        "bytes=0-499", "bytes=500-", "bytes=-500", "bytes=0-0,-1",
        "0-499", "bytes 0-499", "=0-499", "bytes==0-499", "bytes=", "",
        "  bytes=0-499  "
    };
    for (int i = 0; i < DRV_COUNT(RANGES); i++)
    {
        HttpRequestHeaderInfo ranged;
        HttpHeaderStrList& rangedRaw = ranged.getRawHeaders();
        rangedRaw.add(string("Range: ") + RANGES[i]);
        ranged.parseHeaders();
        outf("Range [%s] -> [%s]", esc(RANGES[i]).c_str(),
            esc(ranged.getRange()).c_str());
    }

    HttpRequestHeaderInfo cleared;
    cleared.setHost("example.com");
    cleared.clear();
    outf("after clear: host=[%s] accept=[%s] userAgent=[%s]",
        esc(cleared.getHost()).c_str(), esc(cleared.getAccept()).c_str(),
        esc(cleared.getUserAgent()).c_str());
}

DRV_CASE(http_request_header_build_order)
{
    // buildHeaders ends with a moveToTop over a fixed name list, so the order
    // of the result is part of the wire format.
    HttpRequestHeaderInfo info;
    info.setHost("example.com");
    info.setContentType("text/plain");
    info.setContentLength(10);
    info.setConnection(string("keep-alive"));
    info.setFrom("a@example.com");
    info.setReferer("http://example.com/");
    info.setRange("0-9");
    info.setLastModified("Thu, 01 Jan 1970 00:00:00 GMT");
    info.buildHeaders();
    outf("built: [%s]", esc(dumpHeaders(info.getRawHeaders())).c_str());

    // If-Modified-Since is derived from lastModified_ but Last-Modified is
    // emitted by the base class from the same field, so both appear.
    outf("If-Modified-Since=[%s] Last-Modified=[%s]",
        esc(info.getRawHeaders().getValue("If-Modified-Since")).c_str(),
        esc(info.getRawHeaders().getValue("Last-Modified")).c_str());

    // A minimal request: everything the constructor did not fill is absent.
    HttpRequestHeaderInfo minimal;
    minimal.setCacheControl("");
    minimal.setPragma("");
    minimal.setAccept("");
    minimal.setUserAgent("");
    minimal.buildHeaders();
    outf("minimal: [%s]", esc(dumpHeaders(minimal.getRawHeaders())).c_str());
}

DRV_CASE(http_response_header_info)
{
    HttpResponseHeaderInfo info;
    outf("default contentType=[%s] acceptRanges=[%s] location=[%s] server=[%s]",
        esc(info.getContentType()).c_str(),
        esc(info.getAcceptRanges()).c_str(),
        esc(info.getLocation()).c_str(), esc(info.getServer()).c_str());

    HttpHeaderStrList& raw = info.getRawHeaders();
    raw.add("Accept-Ranges: bytes");
    raw.add("Location: http://example.com/next");
    raw.add("Server: ise/test");
    info.parseHeaders();
    outf("acceptRanges=[%s] location=[%s] server=[%s]",
        esc(info.getAcceptRanges()).c_str(),
        esc(info.getLocation()).c_str(), esc(info.getServer()).c_str());

    // Content-Range is emitted only when one of the two range predicates
    // holds, and each half of the header falls back to '*' independently.
    //
    // Only the *shape* is scored here. The numeric half is built with
    // formatString("%I64d-%I64d", ...), an MSVC spelling that on glibc means
    // the 'I' flag followed by a field width of 64 and a %d conversion -- so a
    // 64-bit argument is read as an int. Those bytes are undefined behaviour,
    // and pinning them would be pinning this ABI rather than this library.
    // They are recorded, unscored, in http_response_content_range_defect.
    for (int i = 0; i < DRV_COUNT(RANGE_CASES); i++)
    {
        HttpResponseHeaderInfo ranged;
        ranged.setContentRangeStart(RANGE_CASES[i].start);
        ranged.setContentRangeEnd(RANGE_CASES[i].end);
        ranged.setContentRangeInstanceLength(RANGE_CASES[i].instance);
        ranged.buildHeaders();
        string header = ranged.getRawHeaders().getValue("Content-Range");
        string::size_type slash = header.rfind('/');
        bool numericRange = header.find("bytes *") != 0 && !header.empty();
        bool numericInstance = slash != string::npos &&
            header.substr(slash + 1) != "*";
        outf("start=%s end=%s instance=%s -> present=%d numericRange=%d"
            " numericInstance=%d",
            intToStr(RANGE_CASES[i].start).c_str(),
            intToStr(RANGE_CASES[i].end).c_str(),
            intToStr(RANGE_CASES[i].instance).c_str(),
            (header.empty() ? 0 : 1), (numericRange ? 1 : 0),
            (numericInstance ? 1 : 0));
    }

    HttpResponseHeaderInfo built;
    built.setAcceptRanges("bytes");
    built.setContentType("image/png");
    built.setContentLength(2048);
    built.buildHeaders();
    outf("built: [%s]", esc(dumpHeaders(built.getRawHeaders())).c_str());

    HttpResponseHeaderInfo cleared;
    cleared.setServer("ise/test");
    cleared.clear();
    outf("after clear: server=[%s] contentType=[%s]",
        esc(cleared.getServer()).c_str(), esc(cleared.getContentType()).c_str());
}

DRV_UNSCORED_CASE(http_response_content_range_defect,
    "Content-Range is built with the MSVC %I64d; see NOTES-authoring.md B8")
{
    // NOT SCORED. ise_http.cpp builds the numeric half of Content-Range with
    //
    //     formatString("%I64d-%I64d", contentRangeStart_, contentRangeEnd_)
    //
    // and HttpRequestHeaderInfo::setRange(INT64, INT64) does the same for the
    // request side. "%I64d" is the MSVC spelling of a 64-bit conversion; glibc
    // reads it as the 'I' flag, a field width of 64, and a %d conversion, so
    // each INT64 argument is consumed as an int. The digits that come out are
    // undefined behaviour and the 64-column padding is emitted into a header
    // that no peer can parse.
    //
    // This case exists so the defect is in the transcript and in the review
    // record rather than only in a note. Its expectations are excluded from
    // scoring, because a migration that reproduced them byte for byte would be
    // preserving this ABI rather than this library.
    for (int i = 0; i < DRV_COUNT(RANGE_CASES); i++)
    {
        HttpResponseHeaderInfo ranged;
        ranged.setContentRangeStart(RANGE_CASES[i].start);
        ranged.setContentRangeEnd(RANGE_CASES[i].end);
        ranged.setContentRangeInstanceLength(RANGE_CASES[i].instance);
        ranged.buildHeaders();
        string header = ranged.getRawHeaders().getValue("Content-Range");
        // The padding is reported as a count rather than as 64 spaces, so a
        // reader of the transcript can see what is wrong with it.
        string collapsed;
        int run = 0;
        for (string::size_type c = 0; c < header.size(); c++)
        {
            if (header[c] == ' ')
            {
                run++;
                continue;
            }
            if (run > 1) collapsed += formatString("<%d sp>", run);
            else if (run == 1) collapsed += ' ';
            run = 0;
            collapsed += header[c];
        }
        if (run > 1) collapsed += formatString("<%d sp>", run);
        else if (run == 1) collapsed += ' ';

        outf("start=%s end=%s instance=%s -> [%s] len=%d",
            intToStr(RANGE_CASES[i].start).c_str(),
            intToStr(RANGE_CASES[i].end).c_str(),
            intToStr(RANGE_CASES[i].instance).c_str(),
            esc(collapsed).c_str(), static_cast<int>(header.size()));
    }

    // The request side, through setRange(INT64, INT64).
    struct Pair { INT64 start; INT64 end; };
    static const Pair PAIRS[] =
    {
        { 0, 499 }, { 500, -1 }, { 0, 0 }, { 1, 65536 },
        { 4294967296LL, 4294967297LL }
    };
    for (int i = 0; i < DRV_COUNT(PAIRS); i++)
    {
        HttpRequestHeaderInfo request;
        request.setRange(PAIRS[i].start, PAIRS[i].end);
        string range = request.getRange();
        outf("setRange(%s, %s) -> len=%d trimmed=[%s]",
            intToStr(PAIRS[i].start).c_str(), intToStr(PAIRS[i].end).c_str(),
            static_cast<int>(range.size()), esc(trimString(range)).c_str());
    }
}

DRV_CASE(http_parse_request_line)
{
    static const char *LINES[] =
    {
        // Accepted.
        "GET / HTTP/1.1",
        "GET /images/logo.gif HTTP/1.1",
        "POST /submit HTTP/1.0",
        "HEAD /x HTTP/1.0",
        "  GET / HTTP/1.1  ",
        "GET  /  HTTP/1.1",
        "GET /a?b=c&d=e HTTP/1.1",
        "GET /%20space HTTP/1.1",
        // Rejected: wrong method.
        "get / HTTP/1.1",
        "PUT / HTTP/1.1",
        "DELETE / HTTP/1.1",
        "OPTIONS / HTTP/1.1",
        "TRACE / HTTP/1.1",
        "CONNECT example.com:443 HTTP/1.1",
        "GETX / HTTP/1.1",
        "",
        // Rejected: wrong version.
        "GET / HTTP/1.2",
        "GET / HTTP/2",
        "GET / HTTP/1.1x",
        "GET / http/1.1",
        "GET / HTTP1.1",
        "GET /",
        // Rejected: wrong field count.
        "GET",
        "GET / HTTP/1.1 extra",
        "GET  /  HTTP/1.1  extra",
        "   ",
        "\t GET / HTTP/1.1 \t",
        "GET\t/\tHTTP/1.1"
    };

    // Whether a rejected line leaves the caller's version alone cannot be read
    // off a single out-of-range sentinel: HTTP_PROTO_VER has a one-bit value
    // range, so a sentinel of 7 is an indeterminate load rather than a marker.
    // Seeding each line twice, once from each enumerator, answers the same
    // question with defined values: two different answers mean parseRequestLine
    // left the argument untouched, one answer means it wrote it.
    for (int i = 0; i < DRV_COUNT(LINES); i++)
    {
        string method = "<unset>";
        string url = "<unset>";
        HTTP_PROTO_VER from0 = DRV_ENUM(HTTP_PROTO_VER, HPV_1_0);
        HTTP_PROTO_VER from1 = DRV_ENUM(HTTP_PROTO_VER, HPV_1_1);
        bool ok = HttpRequest::parseRequestLine(LINES[i], method, url, from0);
        string method1 = "<unset>";
        string url1 = "<unset>";
        HttpRequest::parseRequestLine(LINES[i], method1, url1, from1);
        outf("[%s] -> %d method=[%s] url=[%s] ver(from1.0)=%d ver(from1.1)=%d",
            esc(LINES[i]).c_str(), (ok ? 1 : 0), esc(method).c_str(),
            esc(url).c_str(), static_cast<int>(from0),
            static_cast<int>(from1));
    }
}

DRV_CASE(http_request_buffer)
{
    HttpRequest request;
    outf("defaults: method=[%s] url=[%s] ver=%d contentStream=%d",
        esc(request.getMethod()).c_str(), esc(request.getUrl()).c_str(),
        static_cast<int>(request.getProtocolVersion()),
        (request.getContentStream() == NULL ? 0 : 1));

    bool accepted = request.setRequestLine("GET /index.html HTTP/1.0");
    outf("setRequestLine -> %d method=[%s] url=[%s] ver=%d",
        (accepted ? 1 : 0), esc(request.getMethod()).c_str(),
        esc(request.getUrl()).c_str(),
        static_cast<int>(request.getProtocolVersion()));

    // A rejected request line leaves every field as it was.
    bool rejected = request.setRequestLine("BREW /coffee HTCPCP/1.0");
    outf("bad setRequestLine -> %d method=[%s] url=[%s] ver=%d",
        (rejected ? 1 : 0), esc(request.getMethod()).c_str(),
        esc(request.getUrl()).c_str(),
        static_cast<int>(request.getProtocolVersion()));

    request.setHost("example.com");
    request.setContentLength(0);
    request.setConnection(string("close"));

    Buffer buffer;
    request.makeRequestHeaderBuffer(buffer);
    outf("request bytes: [%s]", esc(dumpBuffer(buffer)).c_str());
    outf("buffer size = %d", buffer.getSize());

    // Building twice must produce the same bytes: buildHeaders clears first.
    Buffer again;
    request.makeRequestHeaderBuffer(again);
    outf("stable across calls: %d",
        (dumpBuffer(again) == dumpBuffer(buffer) ? 1 : 0));

    // A POST with a body length and a custom header.
    HttpRequest post;
    post.setRequestLine("POST /submit HTTP/1.1");
    post.setHost("example.com");
    post.setContentType("application/x-www-form-urlencoded");
    post.setContentLength(9);
    HttpHeaderStrList custom;
    custom.add("X-Trace: abc123");
    post.setCustomHeaders(custom);
    Buffer postBuffer;
    post.makeRequestHeaderBuffer(postBuffer);
    outf("post bytes: [%s]", esc(dumpBuffer(postBuffer)).c_str());

    // The content stream is held, not owned, and is not part of the header.
    MemoryStream body;
    body.write("form=data", 9);
    post.setContentStream(&body);
    outf("content stream attached: %d size=%d",
        (post.getContentStream() == &body ? 1 : 0),
        static_cast<int>(body.getSize()));

    post.clear();
    outf("after clear: method=[%s] url=[%s] host=[%s] contentStream=%d",
        esc(post.getMethod()).c_str(), esc(post.getUrl()).c_str(),
        esc(post.getHost()).c_str(),
        (post.getContentStream() == NULL ? 0 : 1));
}

DRV_CASE(http_response_status)
{
    HttpResponse response;
    outf("default statusLine=[%s] code=%d ver=%d keepAlive=%d",
        esc(response.getStatusLine()).c_str(), response.getStatusCode(),
        static_cast<int>(response.getResponseVersion()),
        (response.getKeepAlive() ? 1 : 0));

    // setStatusCode composes the line from the version the line already has,
    // so the first call reads an empty line and falls back to 1.1.
    static const int CODES[] = { 200, 206, 301, 404, 500, 999, 0, -1 };
    for (int i = 0; i < DRV_COUNT(CODES); i++)
    {
        HttpResponse composed;
        composed.setStatusCode(CODES[i]);
        outf("setStatusCode(%d) -> [%s] readBack=%d",
            CODES[i], esc(composed.getStatusLine()).c_str(),
            composed.getStatusCode());
    }

    // Once a 1.0 line is in place, the next setStatusCode keeps 1.0.
    HttpResponse sticky;
    sticky.setStatusLine("HTTP/1.0 200 OK");
    outf("1.0 line: ver=%d", static_cast<int>(sticky.getResponseVersion()));
    sticky.setStatusCode(404);
    outf("after setStatusCode: [%s]", esc(sticky.getStatusLine()).c_str());

    // getStatusCode and getResponseVersion read the line by position and by
    // token, so a line that is short, odd or absent is where they differ.
    static const char *LINES[] =
    {
        "HTTP/1.1 200 OK",
        "HTTP/1.0 404 Not Found",
        "HTTP/1.1 206 Partial Content",
        "HTTP/1.1 200",
        "HTTP/1.1 200 ",
        "HTTP/1.1  200  OK",
        "  HTTP/1.1 200 OK",
        "HTTP/1.1 2x0 OK",
        "HTTP/1.1 OK",
        "HTTP/1.1",
        "HTTP/",
        "HTTP",
        "",
        " ",
        "200 OK",
        "HTTP/1.1 200.5 OK",
        "HTTP/9.9 200 OK",
        "http/1.1 200 OK",
        "HTTP/1.1 -1 Nope",
        "HTTP/1.1 99999999999 Big"
    };

    for (int i = 0; i < DRV_COUNT(LINES); i++)
    {
        HttpResponse parsed;
        parsed.setStatusLine(LINES[i]);
        outf("[%s] -> code=%d ver=%d", esc(LINES[i]).c_str(),
            parsed.getStatusCode(),
            static_cast<int>(parsed.getResponseVersion()));
    }
}

DRV_CASE(http_response_keep_alive)
{
    // Anything that is neither empty nor "close" in any case counts as
    // keep-alive, which is looser than the RFC and worth pinning as such.
    static const char *CONNECTIONS[] =
    {
        "keep-alive", "Keep-Alive", "KEEP-ALIVE", "close", "Close", "CLOSE",
        "", " ", "closed", "close ", " close", "keep_alive", "upgrade",
        "close, keep-alive"
    };

    for (int i = 0; i < DRV_COUNT(CONNECTIONS); i++)
    {
        HttpResponse response;
        response.setConnection(string(CONNECTIONS[i]));
        outf("[%s] -> keepAlive=%d", esc(CONNECTIONS[i]).c_str(),
            (response.getKeepAlive() ? 1 : 0));
    }
}

DRV_CASE(http_set_connection_overload)
{
    // setConnection is overloaded on const string& and on bool. A string
    // *literal* is a const char*, and pointer-to-bool is a standard conversion
    // while const char* to std::string is a user-defined one -- so the bool
    // overload wins and every literal, including "close" and "", sets
    // "keep-alive".
    //
    // This is the behaviour of the interface as it stands, and every caller in
    // the project that writes setConnection("...") gets it. It is pinned here
    // so that a step which fixes the overload has to say it is doing so.
    static const char *LITERALS[] = { "close", "keep-alive", "", "anything" };

    for (int i = 0; i < DRV_COUNT(LITERALS); i++)
    {
        HttpEntityHeaderInfo viaLiteral;
        viaLiteral.setConnection(LITERALS[i]);
        HttpEntityHeaderInfo viaString;
        viaString.setConnection(string(LITERALS[i]));
        outf("[%s] -> literal=[%s] string=[%s] same=%d",
            esc(LITERALS[i]).c_str(),
            esc(viaLiteral.getConnection()).c_str(),
            esc(viaString.getConnection()).c_str(),
            (viaLiteral.getConnection() == viaString.getConnection() ? 1 : 0));
    }

    // The two bool values, spelt as bools, for comparison.
    HttpEntityHeaderInfo trueCase;
    trueCase.setConnection(true);
    HttpEntityHeaderInfo falseCase;
    falseCase.setConnection(false);
    outf("bool true=[%s] bool false=[%s]",
        esc(trueCase.getConnection()).c_str(),
        esc(falseCase.getConnection()).c_str());

    // A NULL const char* also selects the bool overload, and converts to
    // false, so it is the one spelling of a literal that yields "close".
    HttpEntityHeaderInfo nullCase;
    nullCase.setConnection(static_cast<const char*>(NULL));
    outf("null pointer -> [%s]", esc(nullCase.getConnection()).c_str());

    // A std::string variable, as opposed to a literal, takes the string
    // overload -- so the trap is specific to the literal at the call site.
    string closeText("close");
    HttpEntityHeaderInfo variable;
    variable.setConnection(closeText);
    outf("string variable -> [%s]", esc(variable.getConnection()).c_str());

    // What the defect costs in practice: a response built the way the project
    // builds one.
    HttpResponse response;
    response.setStatusCode(200);
    response.setConnection("close");
    outf("setConnection(\"close\") on a response -> header=[%s] keepAlive=%d",
        esc(response.getConnection()).c_str(),
        (response.getKeepAlive() ? 1 : 0));
}

DRV_CASE(http_response_buffer)
{
    HttpResponse response;
    response.setStatusCode(200);
    response.setContentType("text/html");
    response.setContentLength(13);
    response.setConnection(string("close"));
    response.setServer("ise/test");

    Buffer buffer;
    response.makeResponseHeaderBuffer(buffer);
    outf("response bytes: [%s]", esc(dumpBuffer(buffer)).c_str());
    outf("buffer size = %d", buffer.getSize());

    Buffer again;
    response.makeResponseHeaderBuffer(again);
    outf("stable across calls: %d",
        (dumpBuffer(again) == dumpBuffer(buffer) ? 1 : 0));

    // A 206 exercises the response-only build arm. Only the instance length is
    // set, so the Content-Range that comes out is the "bytes */1234" form --
    // the numeric form goes through the %I64d defect and is recorded, unscored,
    // in http_response_content_range_defect.
    HttpResponse partial;
    partial.setStatusCode(206);
    partial.setAcceptRanges("bytes");
    partial.setContentRangeInstanceLength(1234);
    partial.setContentLength(500);
    Buffer partialBuffer;
    partial.makeResponseHeaderBuffer(partialBuffer);
    outf("partial bytes: [%s]", esc(dumpBuffer(partialBuffer)).c_str());

    // An empty status line still produces a leading CRLF and a terminator.
    HttpResponse bare;
    bare.setCacheControl("");
    bare.setPragma("");
    bare.setConnection(string(""));
    bare.setContentType("");
    Buffer bareBuffer;
    bare.makeResponseHeaderBuffer(bareBuffer);
    outf("bare bytes: [%s] size=%d", esc(dumpBuffer(bareBuffer)).c_str(),
        bareBuffer.getSize());
}

DRV_CASE(http_response_content_stream_ownership)
{
    // setContentStream deletes the previous stream only if it was handed over
    // with ownsObject true, and the destructor releases through the same path.
    // Everything here is observed through the stream's own size so that
    // nothing prints an address.
    HttpResponse response;
    outf("default content stream: %d",
        (response.getContentStream() == NULL ? 0 : 1));

    MemoryStream borrowed;
    borrowed.write("borrowed", 8);
    response.setContentStream(&borrowed, false);
    outf("borrowed attached: %d size=%d",
        (response.getContentStream() == &borrowed ? 1 : 0),
        static_cast<int>(borrowed.getSize()));

    // Replacing a borrowed stream must not delete it: the local is still
    // readable afterwards.
    response.setContentStream(new MemoryStream(), true);
    outf("borrowed survived replacement: size=%d",
        static_cast<int>(borrowed.getSize()));

    response.getContentStream()->write("owned", 5);
    outf("owned stream size=%d",
        static_cast<int>(response.getContentStream()->getSize()));

    // Replacing an owned stream deletes it; the only way to see that without
    // reading freed memory is that the new one is in place and clean.
    response.setContentStream(new MemoryStream(), true);
    outf("replacement is empty: size=%d",
        static_cast<int>(response.getContentStream()->getSize()));

    // Detaching with NULL frees the owned stream and leaves nothing behind.
    response.setContentStream(NULL, false);
    outf("after detach: %d",
        (response.getContentStream() == NULL ? 0 : 1));

    // clear() goes through init(), which drops the pointer without deleting --
    // so a stream owned at the time of clear() is leaked. That is the
    // behaviour as it stands; the case is here so a step that fixes it has to
    // say so.
    HttpResponse cleared;
    cleared.setContentStream(&borrowed, false);
    cleared.clear();
    outf("after clear: contentStream=%d borrowed still readable size=%d",
        (cleared.getContentStream() == NULL ? 0 : 1),
        static_cast<int>(borrowed.getSize()));

    // A scoped response owning its stream must destruct without complaint.
    {
        HttpResponse scoped;
        scoped.setContentStream(new MemoryStream(), true);
        scoped.getContentStream()->write("scoped", 6);
        outf("scoped size=%d",
            static_cast<int>(scoped.getContentStream()->getSize()));
    }
    out("scoped response destructed");
}
