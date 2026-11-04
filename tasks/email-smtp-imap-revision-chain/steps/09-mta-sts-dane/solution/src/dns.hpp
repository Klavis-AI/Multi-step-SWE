#pragma once

#include <string>
#include <vector>

namespace mail {

struct MxTarget {
  int pref = 0;
  std::string host;
  std::string addr;  // IPv4 dotted
  int port = 25;
};

// RFC 974: MX lookup, implicit MX if none. A-record for each MX.
std::vector<MxTarget> resolve_mx(const std::string& domain);

// RFC 7672 / 6698: TLSA 3 1 1 SHA-256 from the shipped BIND zone
// `_25._tcp.<domain>`. Empty if the zone has no such RR.
std::string lookup_tlsa_sha256(const std::string& domain);
// RFC 8460: rua= from `_smtp._tls.example.test` TXT in the zone.
std::string lookup_tlsrpt_rua();

}  // namespace mail
