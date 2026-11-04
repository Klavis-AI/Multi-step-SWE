#include "dns.hpp"
#include "util.hpp"

#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <cstring>
#include <netdb.h>
#include <resolv.h>
#include <algorithm>
#include <sstream>

namespace mail {

static std::string a_record(const std::string& host) {
  addrinfo hints {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return {};
  char buf[INET_ADDRSTRLEN] = {};
  auto* sa = reinterpret_cast<sockaddr_in*>(res->ai_addr);
  ::inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
  ::freeaddrinfo(res);
  return buf;
}

std::vector<MxTarget> resolve_mx(const std::string& domain) {
  std::vector<MxTarget> out;
  unsigned char buf[NS_PACKETSZ];
  int len = ::res_query(domain.c_str(), ns_c_in, ns_t_mx, buf, sizeof(buf));
  if (len > 0) {
    ns_msg msg;
    if (ns_initparse(buf, len, &msg) >= 0) {
      int n = ns_msg_count(msg, ns_s_an);
      for (int i = 0; i < n; ++i) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) continue;
        if (ns_rr_type(rr) != ns_t_mx) continue;
        const unsigned char* rdata = ns_rr_rdata(rr);
        int pref = ntohs(*reinterpret_cast<const unsigned short*>(rdata));
        char host[NS_MAXDNAME];
        if (dn_expand(ns_msg_base(msg), ns_msg_end(msg), rdata + 2, host, sizeof(host)) < 0)
          continue;
        MxTarget t;
        t.pref = pref;
        t.host = host;
        t.addr = a_record(t.host);
        if (!t.addr.empty()) out.push_back(t);
      }
    }
  }
  if (out.empty()) {
    // Implicit MX: the domain itself, pref 0 (RFC 974).
    MxTarget t;
    t.pref = 0;
    t.host = domain;
    t.addr = a_record(domain);
    if (!t.addr.empty()) out.push_back(t);
  }
  std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.pref < b.pref; });
  return out;
}

static std::string zone_rr(const std::string& kind, const std::string& owner) {
  std::string zone = read_file(app_root() + "/etc/bind/example.test.zone");
  std::string origin = "example.test";
  std::istringstream in(zone);
  std::string line;
  std::string want = lower(owner);
  if (!want.empty() && want.back() == '.') want.pop_back();
  while (std::getline(in, line)) {
    std::string stripped;
    bool inq = false;
    for (char ch : line) {
      if (ch == '"') inq = !inq;
      if (ch == ';' && !inq) break;
      stripped.push_back(ch);
    }
    line = trim(stripped);
    if (line.empty()) continue;
    if (starts_with_ci(line, "$ORIGIN")) {
      auto sp = line.find_first_of(" \t");
      origin = trim(line.substr(sp == std::string::npos ? line.size() : sp));
      if (!origin.empty() && origin.back() == '.') origin.pop_back();
      origin = lower(origin);
      continue;
    }
    auto toks = split_ws(line);
    if (toks.size() < 4) continue;
    std::string name = toks[0];
    if (name == "@") name = origin;
    else if (!name.empty() && name.back() == '.') name.pop_back();
    else if (name.find('.') == std::string::npos) name += "." + origin;
    name = lower(name);
    std::string typ;
    for (auto& t : toks)
      if (ieq(t, "TXT") || ieq(t, "TLSA") || ieq(t, "A")) {
        typ = upper(t);
        break;
      }
    if (!ieq(typ, kind) || name != want) continue;
    auto q1 = line.find('"');
    if (q1 != std::string::npos) {
      auto q2 = line.find('"', q1 + 1);
      if (q2 != std::string::npos) return line.substr(q1 + 1, q2 - q1 - 1);
    }
    if (ieq(kind, "TLSA") && toks.size() >= 4) return lower(toks.back());
  }
  return {};
}

std::string lookup_tlsa_sha256(const std::string& domain) {
  std::string d = lower(domain);
  if (!d.empty() && d.back() == '.') d.pop_back();
  return zone_rr("TLSA", "_25._tcp." + d);
}

std::string lookup_tlsrpt_rua() {
  std::string txt = zone_rr("TXT", "_smtp._tls.example.test");
  auto p = txt.find("rua=");
  if (p == std::string::npos) return {};
  auto s = txt.substr(p + 4);
  auto sc = s.find(';');
  if (sc != std::string::npos) s = s.substr(0, sc);
  return trim(s);
}

}  // namespace mail
