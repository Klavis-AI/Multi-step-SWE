///////////////////////////////////////////////////////////////////////////////
// Characterisation cases for CustomHttpClient and HttpClient.
//
// c_http.cpp covers the parts of the HTTP stack that need no socket. This file
// covers the rest: the request the client puts on the wire, the byte-at-a-time
// response-header reader, the redirect loop, and the two body loops
// (Content-Length and chunked). Between them they are the least-covered
// migration-relevant code in the project, and they are written in exactly the
// idiom eleven steps of migration will rewrite -- raw recv loops over a char,
// hand-rolled line splitting, strtol on a chunk size, INT64 arithmetic.
//
// The peer is canned, not real: a listening socket on 127.0.0.1 with a
// kernel-chosen port, answering from a script fixed before the client runs.
// Nothing here talks to a network, resolves a name, or reads a clock -- with
// one exception, http_client_header_timeout, whose expectation is monotone in
// load (a slower machine can only time out harder).
//
// The kernel picks the port, so the port is the one thing in a transcript that
// moves between runs. Every line that could carry it goes through mask().
///////////////////////////////////////////////////////////////////////////////

#include "drv.h"

#include "ise/main/ise.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

using namespace ise;
using drv::out;
using drv::outf;
using drv::esc;

namespace
{

// A port nothing listens on. Port 1 is reserved (tcpmux) and unprivileged
// processes in the container cannot bind it, so a connect is refused at once
// rather than waiting out the client's twenty-second connect timeout.
const int REFUSED_PORT = 1;

///////////////////////////////////////////////////////////////////////////////
// The canned peer.

class CannedServer
{
public:
    CannedServer() : listener_(-1), port_(0), running_(false),
        stopping_(false), holdMSecs_(0) {}
    ~CannedServer() { stop(); }

    // Keep each connection open this long after the request has arrived,
    // before sending the reply. Only http_client_header_timeout uses it; the
    // hold is polled in short slices so stop() does not have to wait it out.
    void holdFor(int msecs) { holdMSecs_ = msecs; }

    // Binding and serving are separate because a redirect has to name the port
    // it redirects to, and the port is not known until the kernel has picked
    // one. bind() reports it; run() starts answering.
    bool bind();

    // One reply per connection, in order. The i-th connection the client makes
    // is answered with replies[i]; a connection past the end of the script is
    // accepted and closed without a reply.
    bool run(const char *const *replies, int count);

    // bind() and run() together, for the cases that need no port in advance.
    bool start(const char *const *replies, int count)
    {
        return bind() && run(replies, count);
    }

    // Joins the serving thread and closes the listener. Safe to call twice;
    // the destructor calls it so a case that throws still tears down.
    void stop();

    int port() const { return port_; }

    // The bytes the client sent on the i-th connection.
    string seen(int index) const
    {
        return (index >= 0 && index < (int)seen_.size() ? seen_[index] : string());
    }
    int connections() const { return (int)seen_.size(); }

    // The port appears in Host: and in a redirect the case built from port().
    string mask(const string& text) const
    {
        string result = text, needle = ":" + intToStr(port_);
        string::size_type at;
        while ((at = result.find(needle)) != string::npos)
            result.replace(at, needle.size(), ":PORT");
        return result;
    }

private:
    static void* threadMain(void *arg)
    {
        static_cast<CannedServer*>(arg)->serve();
        return NULL;
    }
    void serve();

    int listener_;
    int port_;
    bool running_;
    volatile bool stopping_;
    int holdMSecs_;
    pthread_t thread_;
    std::vector<string> replies_;
    std::vector<string> seen_;
};

bool CannedServer::bind()
{
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0)
        return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(listener_, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
        ::listen(listener_, 4) != 0)
    {
        ::close(listener_);
        listener_ = -1;
        return false;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(listener_, (struct sockaddr*)&addr, &len) != 0)
    {
        ::close(listener_);
        listener_ = -1;
        return false;
    }
    port_ = ntohs(addr.sin_port);
    return true;
}

bool CannedServer::run(const char *const *replies, int count)
{
    replies_.clear();
    seen_.clear();
    for (int i = 0; i < count; i++)
        replies_.push_back(string(replies[i]));

    running_ = (::pthread_create(&thread_, NULL, threadMain, this) == 0);
    if (!running_)
    {
        ::close(listener_);
        listener_ = -1;
    }
    return running_;
}

void CannedServer::serve()
{
    for (size_t i = 0; i < replies_.size(); i++)
    {
        int fd = ::accept(listener_, NULL, NULL);
        if (fd < 0)
            break;

        // Read the request header, then whatever body it declares. Reading
        // exactly what was announced rather than until EOF is what lets the
        // client keep the connection for the reply.
        string request;
        char buffer[4096];
        string::size_type end;
        while ((end = request.find("\r\n\r\n")) == string::npos)
        {
            int n = (int)::recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0)
                break;
            request.append(buffer, n);
        }
        if (end != string::npos)
        {
            int want = 0;
            string header = lowerCase(request.substr(0, end));
            string::size_type at = header.find("content-length:");
            if (at != string::npos)
                want = strToInt(trimString(header.substr(at + 15,
                    header.find("\r\n", at) - at - 15)), 0);

            int have = (int)(request.size() - (end + 4));
            while (have < want)
            {
                int n = (int)::recv(fd, buffer, sizeof(buffer), 0);
                if (n <= 0)
                    break;
                request.append(buffer, n);
                have += n;
            }
        }
        seen_.push_back(request);

        for (int waited = 0; waited < holdMSecs_ && !stopping_; waited += 50)
            sleepSeconds(0.05, true);

        const string& reply = replies_[i];
        if (!reply.empty())
            ::send(fd, reply.data(), reply.size(), 0);
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

void CannedServer::stop()
{
    stopping_ = true;
    if (running_)
    {
        // Unblock an accept() the client never made: the script may declare
        // more connections than the case turned out to need.
        ::shutdown(listener_, SHUT_RDWR);
        ::pthread_join(thread_, NULL);
        running_ = false;
    }
    if (listener_ >= 0)
    {
        ::close(listener_);
        listener_ = -1;
    }
}

///////////////////////////////////////////////////////////////////////////////
// Shared shapes.

string urlFor(const CannedServer& server, const string& path)
{
    return "http://127.0.0.1:" + intToStr(server.port()) + path;
}

// The request the client emitted, one header per line. The wire format is the
// single most migration-sensitive thing about this class -- a rewrite of
// makeRequestHeaderBuffer in terms of the standard library changes it silently
// -- so it is printed in full rather than summarised.
void dumpRequest(const CannedServer& server, int index)
{
    StrList lines;
    lines.setLineBreak("\r\n");
    string text = server.mask(server.seen(index));
    lines.setText(text.c_str());

    outf("request bytes: %d", (int)server.seen(index).size());
    for (int i = 0; i < lines.getCount(); i++)
        outf("  | %s", esc(lines[i]).c_str());
}

// What the client made of a reply. Everything printed is the client's own
// state; none of it is a time, a port or a socket number.
void dumpOutcome(HttpClient& client, int code, Stream *content)
{
    outf("result: %d", code);
    outf("status line: [%s]", esc(client.getStatusLine()).c_str());
    outf("status code: %d", client.getStatusCode());
    outf("redirect count: %d", client.getRedirectCount());
    outf("connected after: %d", (client.isConnected() ? 1 : 0));

    HttpResponse& response = client.httpResponse();
    outf("content length: %s", intToStr(response.getContentLength()).c_str());
    outf("content type: [%s]", esc(response.getContentType()).c_str());
    outf("transfer encoding: [%s]", esc(response.getTransferEncoding()).c_str());
    outf("keep alive: %d", (response.getKeepAlive() ? 1 : 0));

    if (content != NULL)
    {
        MemoryStream *memory = dynamic_cast<MemoryStream*>(content);
        if (memory != NULL)
            outf("content: [%s]",
                esc(memory->getMemory(), (int)memory->getSize()).c_str());
        else
            outf("content size: %s", intToStr(content->getSize()).c_str());
    }
}

// A CustomHttpClient is abstract only by intent -- the parsing entry points are
// protected because they are the client's business, not because they are
// unfinished. Characterising them directly is the only way to reach the reject
// branches: the wire cases can only get there through a response the header
// reader would have refused first.
class ParsingClient : public CustomHttpClient
{
public:
    bool parse(const string& text)
    {
        return parseResponseHeader((void*)text.data(), (int)text.size());
    }
    void check(const string& text, bool& finished, bool& error)
    {
        std::vector<char> buffer(text.begin(), text.end());
        buffer.push_back('\0');
        checkResponseHeader(&buffer[0], (int)text.size(), finished, error);
    }
    HTTP_NEXT_OP process() { return processResponseHeader(); }
    HttpResponse& response() { return response_; }

    // The only validation a URL gets, and the whole of the request builder up
    // to the point a socket is needed. Calling it here rather than through
    // get() is what keeps the case off the network: get() on a URL that names
    // a host would try to resolve it, and the answer to that is a property of
    // the container's resolver, not of the library.
    int before(HTTP_METHOD_TYPE method, const string& url, Stream *request)
    {
        return beforeRequest(method, url, request, NULL,
            (request != NULL ? request->getPosition() : 0), 0);
    }
    void bumpRedirectCount() { redirectCount_++; }
    Url& currentUrl() { return url_; }
};

const char *nextOpName(HTTP_NEXT_OP op)
{
    switch (op)
    {
    case DRV_ENUM(HTTP_NEXT_OP, HNO_REDIRECT):     return "redirect";
    case DRV_ENUM(HTTP_NEXT_OP, HNO_RECV_CONTENT): return "recv content";
    case DRV_ENUM(HTTP_NEXT_OP, HNO_EXIT):         return "exit";
    }
    return "?";
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// No peer: the paths that fail before a socket is opened.

DRV_CASE(http_client_url_handling)
{
    // beforeRequest() is the only place a URL is validated, and it validates
    // exactly one thing: that a host was found. Everything else it accepts and
    // turns into a request line and a Host header, which is where a rewrite of
    // Url would show up. The port is dropped from Host when it is the default
    // -- and the default is decided from the URL just parsed, which matters
    // for a relative redirect.
    static const char *URLS[] =
    {
        "",
        "/no/scheme/or/host",
        "http://",
        "http:///path",
        "://host/path",
        "host-without-scheme/path",
        "http://host",
        "http://host/",
        "http://host:80/p",
        "http://host:8080/a/b?c=d#e",
        "http://host:notanumber/p",
        "http://host:0/p",
        "http://host:65535/p",
        "http://host:99999/p",
        "HTTP://HOST/Path",
        "ftp://host/path",
        "http://user:pass@host:81/path",
        "http://host/a%20b/c+d?q=%2F&r=#frag"
    };

    for (int i = 0; i < DRV_COUNT(URLS); i++)
    {
        ParsingClient client;
        int code = client.before(DRV_ENUM(HTTP_METHOD_TYPE, HMT_GET), URLS[i], NULL);
        outf("[%s] -> %d%s", esc(URLS[i]).c_str(), code,
            (code == EC_HTTP_URL_ERROR ? " (url error)" : ""));
        if (code == EC_HTTP_SUCCESS)
            outf("    method [%s] url [%s] host [%s] length %s",
                esc(client.httpRequest().getMethod()).c_str(),
                esc(client.httpRequest().getUrl()).c_str(),
                esc(client.httpRequest().getHost()).c_str(),
                intToStr(client.httpRequest().getContentLength()).c_str());
    }
}

DRV_CASE(http_client_relative_redirect_host)
{
    // After a redirect the URL is merged with the one before it, but the port
    // that decides whether Host carries a port is read from the new URL alone.
    // A relative redirect therefore loses a non-default port from Host while
    // keeping it for the connection -- see NOTES-authoring.md B14. That is
    // baseline behaviour, so it is pinned rather than corrected: a migration
    // that rewrites this function has to preserve it.
    struct Hop { const char *first; const char *second; };
    static const Hop HOPS[] =
    {
        { "http://host:8080/a",  "/b" },
        { "http://host:8080/a",  "/b?q=1" },
        { "http://host:80/a",    "/b" },
        { "http://host/a",       "/b" },
        { "http://host:8080/a",  "http://other:9090/b" },
        { "http://host:8080/a",  "http://other/b" },
        { "http://host:8080/a",  "" },
        { "http://host:8080/a/b/c", "../d" }
    };

    for (int i = 0; i < DRV_COUNT(HOPS); i++)
    {
        ParsingClient client;
        int first = client.before(DRV_ENUM(HTTP_METHOD_TYPE, HMT_GET), HOPS[i].first, NULL);
        string firstHost = client.httpRequest().getHost();

        client.bumpRedirectCount();
        int second = client.before(DRV_ENUM(HTTP_METHOD_TYPE, HMT_GET), HOPS[i].second, NULL);

        outf("[%s] then [%s]", HOPS[i].first, esc(HOPS[i].second).c_str());
        outf("    first %d host [%s]", first, esc(firstHost).c_str());
        outf("    second %d url [%s] host [%s] connects to [%s]:[%s]",
            second, esc(client.httpRequest().getUrl()).c_str(),
            esc(client.httpRequest().getHost()).c_str(),
            esc(client.currentUrl().getHost()).c_str(),
            esc(client.currentUrl().getPort()).c_str());
    }
}

DRV_CASE(http_client_before_request_streams)
{
    // The content length is the stream's remaining bytes, not its size, and
    // the response stream is truncated to where it was told to start. Both are
    // easy to get wrong when Stream is reworked.
    static const int POSITIONS[] = { 0, 1, 7, 25 };

    for (int i = 0; i < DRV_COUNT(POSITIONS); i++)
    {
        ParsingClient client;
        MemoryStream request;
        const char *BODY = "0123456789abcdefghijklmno";
        request.write(BODY, (int)strlen(BODY));
        request.setPosition(POSITIONS[i]);

        MemoryStream response;
        response.write(BODY, (int)strlen(BODY));
        response.setPosition(POSITIONS[i]);

        int code = client.before(DRV_ENUM(HTTP_METHOD_TYPE, HMT_POST),
            "http://host:8080/p", &request);
        outf("position %2d -> %d length %s method [%s] type [%s]",
            POSITIONS[i], code,
            intToStr(client.httpRequest().getContentLength()).c_str(),
            esc(client.httpRequest().getMethod()).c_str(),
            esc(client.httpRequest().getContentType()).c_str());
        outf("    request stream left at %s of %s",
            intToStr(request.getPosition()).c_str(),
            intToStr(request.getSize()).c_str());
    }

    // No content stream at all is the GET shape: a length of -1, which the
    // header builder reads as "omit Content-Length".
    ParsingClient client;
    int code = client.before(DRV_ENUM(HTTP_METHOD_TYPE, HMT_GET), "http://host/p", NULL);
    outf("no stream -> %d length %s", code,
        intToStr(client.httpRequest().getContentLength()).c_str());
}

DRV_CASE(http_client_connect_refused)
{
    string url = "http://127.0.0.1:" + intToStr(REFUSED_PORT) + "/nothing";

    HttpClient client;
    MemoryStream content;
    outf("get: %d", client.get(url, &content));
    outf("content after failure: %s", intToStr(content.getSize()).c_str());
    outf("connected: %d", (client.isConnected() ? 1 : 0));
    outf("status code: %d", client.getStatusCode());

    // requestFile() takes the same failure down a different arm, and post()
    // takes it through the POST half of beforeRequest() first.
    HttpClient other;
    outf("requestFile: %d", other.requestFile(url));

    MemoryStream request;
    request.write("k=v", 3);
    request.setPosition(0);
    MemoryStream response;
    HttpClient third;
    outf("post: %d", third.post(url, &request, &response));
    outf("post left the request stream at: %s",
        intToStr(request.getPosition()).c_str());
}

DRV_CASE(http_client_download_file_rejects)
{
    // The file is created before the request is made, so a URL that never
    // reaches a socket still leaves the filesystem as it found it.
    string path = drv::tmpPath("download-fail.bin");
    HttpClient client;
    outf("bad url: %d", client.downloadFile("http://", path));
    outf("file exists after bad url: %d", (fileExists(path) ? 1 : 0));

    string refused = "http://127.0.0.1:" + intToStr(REFUSED_PORT) + "/x";
    outf("refused: %d", client.downloadFile(refused, path));
    outf("file exists after refused: %d", (fileExists(path) ? 1 : 0));

    // A path that cannot be created at all.
    outf("undirectable path: %d",
        client.downloadFile(refused, "/proc/self/cannot/be/made"));
}

///////////////////////////////////////////////////////////////////////////////
// No peer: the protected parsing entry points, reached through a subclass.

DRV_CASE(http_client_check_response_header)
{
    // Two independent decisions off the same buffer, and both look only at
    // four bytes -- the first four for the protocol, the last four for the
    // terminator. A buffer shorter than four bytes decides neither.
    static const char *BUFFERS[] =
    {
        "",
        "H",
        "HTT",
        "HTTP",
        "http",
        "HtTp",
        "XXXX",
        "HTTP/1.1 200 OK\r\n\r\n",
        "HTTP/1.1 200 OK\r\n",
        "HTTP/1.1 200 OK\r\n\r\nbody",
        "\r\n\r\n",
        "GET / HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\n\r\n\r\n\r\n"
    };

    for (int i = 0; i < DRV_COUNT(BUFFERS); i++)
    {
        ParsingClient client;
        bool finished = true, error = true;
        client.check(BUFFERS[i], finished, error);
        outf("[%-28s] finished %d error %d", esc(BUFFERS[i]).c_str(),
            (finished ? 1 : 0), (error ? 1 : 0));
    }
}

DRV_CASE(http_client_parse_response_header)
{
    // The status line decides whether anything is parsed at all, and it is
    // matched on seven characters.
    static const char *HEADERS[] =
    {
        "",
        "\r\n",
        "HTTP/1.1 200 OK\r\n\r\n",
        "HTTP/1.0 404 Not Found\r\n\r\n",
        "http/1.1 200 ok\r\n\r\n",
        "HTTP/2 200 OK\r\n\r\n",
        "HTTP/1. 200 OK\r\n\r\n",
        "HTTP/1.1\r\n\r\n",
        "200 OK\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 42\r\nContent-Type: text/plain\r\n\r\n",
        "HTTP/1.1 301 Moved\r\nLocation: http://elsewhere/x\r\n\r\n",
        "HTTP/1.1 200 OK\r\n\r\nContent-Length: 42\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nContent-Length: 2\r\n\r\n",
        "HTTP/1.1 200 OK\nContent-Length: 7\n\n",
        "HTTP/1.1 200 OK\r\n\r\nTransfer-Encoding: chunked\r\n\r\n"
    };

    for (int i = 0; i < DRV_COUNT(HEADERS); i++)
    {
        ParsingClient client;
        bool ok = client.parse(HEADERS[i]);
        outf("[%s]", esc(HEADERS[i]).c_str());
        outf("  parsed %d status %d length %s type [%s] encoding [%s] location [%s]",
            (ok ? 1 : 0), client.getStatusCode(),
            intToStr(client.response().getContentLength()).c_str(),
            esc(client.response().getContentType()).c_str(),
            esc(client.response().getTransferEncoding()).c_str(),
            esc(client.response().getLocation()).c_str());
    }
}

DRV_CASE(http_client_parse_then_process)
{
    // processResponseHeader() reads a status code and a Location and decides
    // what the request loop does next. The 304 and 201 carve-outs are the two
    // places the rule is not simply "3xx redirects".
    static const char *HEADERS[] =
    {
        "HTTP/1.1 100 Continue\r\n\r\n",
        "HTTP/1.1 200 OK\r\n\r\n",
        "HTTP/1.1 201 Created\r\n\r\n",
        "HTTP/1.1 201 Created\r\nLocation: /new\r\n\r\n",
        "HTTP/1.1 204 No Content\r\n\r\n",
        "HTTP/1.1 301 Moved\r\nLocation: /a\r\n\r\n",
        "HTTP/1.1 302 Found\r\nLocation: /b\r\n\r\n",
        "HTTP/1.1 303 See Other\r\n\r\n",
        "HTTP/1.1 304 Not Modified\r\n\r\n",
        "HTTP/1.1 304 Not Modified\r\nLocation: /c\r\n\r\n",
        "HTTP/1.1 307 Temporary Redirect\r\nLocation: /d\r\n\r\n",
        "HTTP/1.1 400 Bad Request\r\n\r\n",
        "HTTP/1.1 404 Not Found\r\nLocation: /e\r\n\r\n",
        "HTTP/1.1 500 Server Error\r\n\r\n"
    };

    for (int i = 0; i < DRV_COUNT(HEADERS); i++)
    {
        for (int handle = 0; handle <= 1; handle++)
        {
            ParsingClient client;
            client.setHandleRedirects(handle != 0);
            client.parse(HEADERS[i]);
            HTTP_NEXT_OP op = client.process();
            outf("%-3d redirects=%d -> %-12s count %d",
                client.getStatusCode(), handle, nextOpName(op),
                client.getRedirectCount());
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// A canned peer: the request on the wire, and the two body loops.

DRV_CASE(http_client_get_content_length)
{
    static const char *REPLIES[] =
    {
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 11\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello world"
    };

    CannedServer server;
    if (!server.start(REPLIES, DRV_COUNT(REPLIES)))
    {
        out("canned server would not start");
        return;
    }

    HttpClient client;
    MemoryStream content;
    int code = client.get(urlFor(server, "/hello?a=1&b=2"), &content);
    server.stop();

    dumpRequest(server, 0);
    dumpOutcome(client, code, &content);
}

DRV_CASE(http_client_get_chunked)
{
    // The chunked reader is hand-rolled: a line per size, strtol base 16, a
    // trailing CRLF per chunk, and a chunk extension after a semicolon. Each
    // of those is a place a rewrite changes behaviour.
    static const char *REPLIES[] =
    {
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\nfirst\r\n"
        "A\r\nsecond--xx\r\n"
        "3;ext=ignored\r\nend\r\n"
        "0\r\n\r\n"
    };

    CannedServer server;
    if (!server.start(REPLIES, DRV_COUNT(REPLIES)))
    {
        out("canned server would not start");
        return;
    }

    HttpClient client;
    MemoryStream content;
    int code = client.get(urlFor(server, "/chunks"), &content);
    server.stop();

    dumpOutcome(client, code, &content);
}

DRV_CASE(http_client_body_edges)
{
    // Every one of these is a reply the content loop has to decide about
    // without help: no length at all, a zero length, a length that disagrees
    // with the body, a chunked stream that ends at once. A short body is the
    // interesting one -- the loop cannot tell "peer closed" from "nothing yet"
    // except through the -1 that doSyncRecvBuffer returns when index is 0.
    struct Reply { const char *name; const char *text; };
    static const Reply REPLIES[] =
    {
        { "no length",
          "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbody" },
        { "zero length",
          "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" },
        { "negative length",
          "HTTP/1.1 200 OK\r\nContent-Length: -5\r\nConnection: close\r\n\r\nbody" },
        { "length longer than body",
          "HTTP/1.1 200 OK\r\nContent-Length: 40\r\nConnection: close\r\n\r\nshort" },
        { "length shorter than body",
          "HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\nlonger body" },
        { "chunked, empty",
          "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n0\r\n\r\n" },
        { "chunked, no terminator",
          "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n4\r\nabcd\r\n" },
        { "chunked size not hex",
          "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\nzz\r\n\r\n" },
        { "encoding cased oddly",
          "HTTP/1.1 200 OK\r\nTransfer-Encoding: ChUnKeD\r\nConnection: close\r\n\r\n0\r\n\r\n" },
        { "not http",
          "PROXY line\r\n\r\n" },
        { "header only, no terminator",
          "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n" },
        { "continue then ok",
          "HTTP/1.1 100 Continue\r\n\r\n"
          "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nhi" }
    };

    for (int i = 0; i < DRV_COUNT(REPLIES); i++)
    {
        const char *script[1];
        script[0] = REPLIES[i].text;

        CannedServer server;
        if (!server.start(script, 1))
        {
            out("canned server would not start");
            return;
        }

        HttpClient client;
        // The two replies that never terminate a header would otherwise sit
        // out the default thirty-second read timeout. Half a second is enough
        // to decide the peer is finished, and the outcome is monotone in load:
        // a slower machine can only time out harder.
        client.options().recvResHeaderTimeout = 500;
        client.options().recvResContBlockTimeout = 500;

        MemoryStream content;
        int code = client.get(urlFor(server, "/edge"), &content);
        server.stop();

        outf("--- %s", REPLIES[i].name);
        outf("  result %d status %d length %s content [%s]",
            code, client.getStatusCode(),
            intToStr(client.httpResponse().getContentLength()).c_str(),
            esc(content.getMemory(), (int)content.getSize()).c_str());
    }
}

DRV_CASE(http_client_peer_closes_silently)
{
    // A peer that reads the request and closes without answering. Nothing here
    // waits on a clock: the empty reply means the script is done, so the
    // socket is shut down as soon as the request has arrived and the header
    // reader sees a closed connection rather than a slow one.
    static const char *REPLIES[] = { "" };

    CannedServer server;
    if (!server.start(REPLIES, DRV_COUNT(REPLIES)))
    {
        out("canned server would not start");
        return;
    }

    HttpClient client;
    MemoryStream content;
    int code = client.get(urlFor(server, "/silence"), &content);
    server.stop();

    outf("result: %d", code);
    outf("status code: %d", client.getStatusCode());
    outf("content: %s", intToStr(content.getSize()).c_str());
    string request = server.seen(0);
    outf("request reached the peer: %d",
        (request.find("GET /silence") != string::npos ? 1 : 0));
}

DRV_CASE(http_client_header_timeout)
{
    // The one case in this file that involves a clock, and the reason it is
    // safe to score is that its outcome is monotone in load: the peer holds
    // the connection open far longer than the client is willing to wait, so a
    // slower machine can only time out harder. Nothing derived from how long
    // it took is printed.
    static const char *REPLIES[] = { "" };

    CannedServer server;
    server.holdFor(10000);
    if (!server.start(REPLIES, DRV_COUNT(REPLIES)))
    {
        out("canned server would not start");
        return;
    }

    HttpClient client;
    client.options().recvResHeaderTimeout = 300;
    MemoryStream content;
    int code = client.get(urlFor(server, "/held"), &content);
    server.stop();

    outf("result: %d (recv timeout is %d)", code, EC_HTTP_RECV_TIMEOUT);
    outf("status code: %d", client.getStatusCode());
    outf("content: %s", intToStr(content.getSize()).c_str());
    string request = server.seen(0);
    outf("request reached the peer: %d",
        (request.find("GET /held") != string::npos ? 1 : 0));
}

DRV_CASE(http_client_redirect_absolute)
{
    // Two hops, each on its own connection because both replies close. The
    // second request is the one worth reading: it must carry the new path.
    static const char *REPLIES[] =
    {
        "HTTP/1.1 302 Found\r\n"
        "Location: http://127.0.0.1:%PORT%/second\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n",

        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 6\r\n"
        "Connection: close\r\n"
        "\r\n"
        "landed"
    };

    CannedServer server;
    if (!server.bind())
    {
        out("canned server would not bind");
        return;
    }

    // The script names the port it redirects to, which the kernel only chose
    // at bind(). Patch it, then start answering.
    string located(REPLIES[0]);
    located.replace(located.find("%PORT%"), 6, intToStr(server.port()));
    const char *script[2];
    script[0] = located.c_str();
    script[1] = REPLIES[1];

    if (!server.run(script, 2))
    {
        out("canned server would not start");
        return;
    }

    HttpClient client;
    MemoryStream content;
    int code = client.get(urlFor(server, "/first"), &content);
    server.stop();

    outf("connections: %d", server.connections());
    dumpRequest(server, 0);
    dumpRequest(server, 1);
    dumpOutcome(client, code, &content);
}

DRV_CASE(http_client_redirect_relative)
{
    // A Location with no host: beforeRequest() keeps the protocol, host and
    // port of the previous URL and takes everything else from the new one.
    // This is the only path through that half of the function.
    static const char *REPLIES[] =
    {
        "HTTP/1.1 301 Moved Permanently\r\n"
        "Location: /moved/here?q=1\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n",

        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 7\r\n"
        "Connection: close\r\n"
        "\r\n"
        "arrived"
    };

    CannedServer server;
    if (!server.start(REPLIES, DRV_COUNT(REPLIES)))
    {
        out("canned server would not start");
        return;
    }

    HttpClient client;
    MemoryStream content;
    int code = client.get(urlFor(server, "/start/point"), &content);
    server.stop();

    outf("connections: %d", server.connections());
    dumpRequest(server, 0);
    dumpRequest(server, 1);
    dumpOutcome(client, code, &content);
}

DRV_CASE(http_client_redirect_disabled)
{
    static const char *REPLIES[] =
    {
        "HTTP/1.1 302 Found\r\n"
        "Location: /elsewhere\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n"
    };

    CannedServer server;
    if (!server.start(REPLIES, DRV_COUNT(REPLIES)))
    {
        out("canned server would not start");
        return;
    }

    HttpClient client;
    client.setHandleRedirects(false);
    MemoryStream content;
    int code = client.get(urlFor(server, "/start"), &content);
    server.stop();

    // A redirect that is not followed and a body that was asked for is the
    // one path to EC_HTTP_CANNOT_RECV_CONTENT.
    outf("connections: %d", server.connections());
    outf("handle redirects: %d", (client.getHandleRedirects() ? 1 : 0));
    dumpOutcome(client, code, &content);
}

DRV_CASE(http_client_post)
{
    // POST rewrites the request before it is built: protocol forced to 1.0, a
    // default content type, and a Content-Length taken from the stream's
    // remaining bytes rather than its size.
    static const char *REPLIES[] =
    {
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n"
        "\r\n"
        "ok"
    };

    CannedServer server;
    if (!server.start(REPLIES, DRV_COUNT(REPLIES)))
    {
        out("canned server would not start");
        return;
    }

    MemoryStream request;
    const char *BODY = "name=value&other=%20thing";
    request.write(BODY, (int)strlen(BODY));
    request.setPosition(4);          // not the whole stream

    HttpClient client;
    client.httpRequest().setProtocolVersion(DRV_ENUM(HTTP_PROTO_VER, HPV_1_1));
    client.httpRequest().setContentType("text/html");
    MemoryStream content;
    int code = client.post(urlFor(server, "/submit"), &request, &content);
    server.stop();

    dumpRequest(server, 0);
    outf("protocol version after post: %d",
        (int)client.httpRequest().getProtocolVersion());
    outf("content type after post: [%s]",
        esc(client.httpRequest().getContentType()).c_str());
    dumpOutcome(client, code, &content);
}

DRV_CASE(http_client_request_file_and_receive)
{
    // requestFile() stops after the header and hands the socket back. The body
    // is then read by hand, which is the only use of receiveFile().
    static const char *REPLIES[] =
    {
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: 16\r\n"
        "\r\n"
        "0123456789abcdef"
    };

    CannedServer server;
    if (!server.start(REPLIES, DRV_COUNT(REPLIES)))
    {
        out("canned server would not start");
        return;
    }

    HttpClient client;
    int code = client.requestFile(urlFor(server, "/file.bin"));
    outf("requestFile: %d", code);
    outf("status code: %d", client.getStatusCode());
    outf("content length: %s",
        intToStr(client.httpResponse().getContentLength()).c_str());
    outf("connected after header: %d", (client.isConnected() ? 1 : 0));

    char buffer[32];
    memset(buffer, 0, sizeof(buffer));
    int got = client.receiveFile(buffer, 16, 2000);
    outf("receiveFile: %d [%s]", got, esc(buffer, (got > 0 ? got : 0)).c_str());

    // A second read past the end of a closed stream.
    int again = client.receiveFile(buffer, 8, 200);
    outf("receiveFile again: %d", (again > 0 ? 1 : (again == 0 ? 0 : -1)));

    client.disconnect();
    outf("connected after disconnect: %d", (client.isConnected() ? 1 : 0));
    server.stop();
}

DRV_CASE(http_client_download_file)
{
    static const char *REPLIES[] =
    {
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 21\r\n"
        "Connection: close\r\n"
        "\r\n"
        "downloaded file bytes"
    };

    CannedServer server;
    if (!server.start(REPLIES, DRV_COUNT(REPLIES)))
    {
        out("canned server would not start");
        return;
    }

    // A directory that does not exist yet: downloadFile() is supposed to make
    // the whole path before it opens anything.
    string path = drv::tmpPath("dl/nested/payload.txt");
    HttpClient client;
    int code = client.downloadFile(urlFor(server, "/payload.txt"), path);
    server.stop();

    outf("downloadFile: %d", code);
    outf("exists: %d", (fileExists(path) ? 1 : 0));

    FileStream file;
    if (file.open(path, FM_OPEN_READ | FM_SHARE_DENY_NONE))
    {
        string text;
        text.resize((size_t)file.getSize());
        if (!text.empty())
            file.read(&text[0], (int)text.size());
        outf("size: %s", intToStr(file.getSize()).c_str());
        outf("content: [%s]", esc(text).c_str());
    }
    else
        out("could not reopen the downloaded file");
}

DRV_CASE(http_client_options_and_state)
{
    // The defaults are part of the interface -- a caller that shortens one
    // timeout relies on the others staying where they were -- and the client's
    // own state before any request is the baseline every case above starts
    // from.
    HttpClient client;
    HttpClientOptions& options = client.options();
    outf("tcpConnectTimeout: %d", options.tcpConnectTimeout);
    outf("sendReqHeaderTimeout: %d", options.sendReqHeaderTimeout);
    outf("sendReqContBlockTimeout: %d", options.sendReqContBlockTimeout);
    outf("recvResHeaderTimeout: %d", options.recvResHeaderTimeout);
    outf("recvResContBlockTimeout: %d", options.recvResContBlockTimeout);
    outf("socketOpTimeout: %d", options.socketOpTimeout);

    outf("handle redirects: %d", (client.getHandleRedirects() ? 1 : 0));
    outf("redirect count: %d", client.getRedirectCount());
    outf("connected: %d", (client.isConnected() ? 1 : 0));
    outf("status line: [%s]", esc(client.getStatusLine()).c_str());
    outf("status code: %d", client.getStatusCode());
    outf("request protocol: %d",
        (int)client.httpRequest().getProtocolVersion());
    outf("response content length: %s",
        intToStr(client.httpResponse().getContentLength()).c_str());

    // disconnect() on a client that never connected must be quiet.
    client.disconnect();
    client.disconnect();
    outf("connected after two disconnects: %d", (client.isConnected() ? 1 : 0));
}
