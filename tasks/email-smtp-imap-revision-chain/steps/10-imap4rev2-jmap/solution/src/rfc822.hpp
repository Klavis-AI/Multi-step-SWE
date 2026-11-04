#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mail {

struct Address {
  std::string display;
  std::string local;
  std::string domain;
  std::string source_route;  // RFC 821 @host,@host:
  std::string original;
  bool empty() const { return local.empty() && domain.empty(); }
  std::string mailbox() const {
    if (domain.empty()) return local;
    return local + "@" + domain;
  }
};

struct Rfc822Message {
  std::string raw;
  std::vector<std::pair<std::string, std::string>> headers;  // original names, unfolded
  std::string body;

  std::string header(std::string_view name) const;
  std::vector<std::string> headers_all(std::string_view name) const;
};

Rfc822Message parse_rfc822(std::string_view raw);
std::string ensure_crlf(std::string_view s);

// RFC 821 reverse-path / forward-path, including source routes.
Address parse_path(std::string_view s);
// RFC 822 addr-spec or mailbox.
Address parse_mailbox(std::string_view s);
std::vector<Address> parse_address_list(std::string_view s);

std::string crlf_join_headers(const Rfc822Message& m);

}  // namespace mail
