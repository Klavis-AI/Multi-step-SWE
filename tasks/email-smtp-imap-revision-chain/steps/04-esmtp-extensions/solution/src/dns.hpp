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

}  // namespace mail
