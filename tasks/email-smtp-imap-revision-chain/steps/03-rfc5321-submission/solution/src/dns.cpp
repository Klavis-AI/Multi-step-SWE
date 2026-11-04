#include "dns.hpp"
#include "util.hpp"

#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <cstring>
#include <netdb.h>
#include <resolv.h>
#include <algorithm>

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

}  // namespace mail
