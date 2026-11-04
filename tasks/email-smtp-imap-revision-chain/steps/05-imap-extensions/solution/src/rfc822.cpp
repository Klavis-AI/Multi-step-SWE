#include "rfc822.hpp"
#include "util.hpp"

#include <cctype>

namespace mail {

std::string ensure_crlf(std::string_view s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\n') {
      if (i == 0 || s[i - 1] != '\r') o.push_back('\r');
      o.push_back('\n');
    } else {
      o.push_back(s[i]);
    }
  }
  return o;
}

static std::string unfold(std::string_view v) {
  std::string o;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '\r') continue;
    if (v[i] == '\n') {
      if (i + 1 < v.size() && (v[i + 1] == ' ' || v[i + 1] == '\t')) {
        o.push_back(' ');
        ++i;
        while (i + 1 < v.size() && (v[i + 1] == ' ' || v[i + 1] == '\t')) ++i;
        continue;
      }
      continue;
    }
    o.push_back(v[i]);
  }
  return o;
}

Rfc822Message parse_rfc822(std::string_view raw_in) {
  Rfc822Message m;
  m.raw = std::string(raw_in);
  std::string s = ensure_crlf(raw_in);
  auto sep = s.find("\r\n\r\n");
  std::string head, body;
  if (sep == std::string::npos) {
    head = s;
  } else {
    head = s.substr(0, sep);
    body = s.substr(sep + 4);
  }
  m.body = body;
  std::string cur_name, cur_val;
  auto flush = [&]() {
    if (!cur_name.empty()) m.headers.emplace_back(cur_name, trim(unfold(cur_val)));
    cur_name.clear();
    cur_val.clear();
  };
  size_t i = 0;
  while (i < head.size()) {
    auto nl = head.find("\r\n", i);
    std::string line = head.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
    i = (nl == std::string::npos) ? head.size() : nl + 2;
    if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
      cur_val += "\r\n" + line;
      continue;
    }
    flush();
    auto col = line.find(':');
    if (col == std::string::npos) continue;
    cur_name = line.substr(0, col);
    cur_val = line.substr(col + 1);
  }
  flush();
  return m;
}

std::string Rfc822Message::header(std::string_view name) const {
  for (auto& h : headers)
    if (ieq(h.first, name)) return h.second;
  return {};
}
std::vector<std::string> Rfc822Message::headers_all(std::string_view name) const {
  std::vector<std::string> o;
  for (auto& h : headers)
    if (ieq(h.first, name)) o.push_back(h.second);
  return o;
}

Address parse_path(std::string_view s) {
  Address a;
  std::string t = trim(s);
  a.original = t;
  if (t.size() >= 2 && t.front() == '<' && t.back() == '>')
    t = t.substr(1, t.size() - 2);
  t = trim(t);
  if (t.empty() || t == "<>") return a;  // null reverse-path
  // source route: @host,@host:user@domain
  if (!t.empty() && t[0] == '@') {
    auto col = t.find(':');
    if (col != std::string::npos) {
      a.source_route = t.substr(0, col);
      t = t.substr(col + 1);
    }
  }
  auto at = t.rfind('@');
  if (at == std::string::npos) {
    a.local = t;
  } else {
    a.local = t.substr(0, at);
    a.domain = t.substr(at + 1);
  }
  return a;
}

Address parse_mailbox(std::string_view s) {
  Address a;
  std::string t = trim(s);
  a.original = t;
  // display <addr> or bare addr-spec
  auto lt = t.find('<');
  auto gt = t.rfind('>');
  if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
    a.display = trim(t.substr(0, lt));
    if (!a.display.empty() && a.display.front() == '"' && a.display.back() == '"')
      a.display = a.display.substr(1, a.display.size() - 2);
    return parse_path(t.substr(lt, gt - lt + 1));
  }
  return parse_path(t);
}

std::vector<Address> parse_address_list(std::string_view s) {
  std::vector<Address> out;
  int depth = 0;
  size_t start = 0;
  std::string str(s);
  for (size_t i = 0; i <= str.size(); ++i) {
    char c = i < str.size() ? str[i] : ',';
    if (c == '"') {
      ++i;
      while (i < str.size() && str[i] != '"') {
        if (str[i] == '\\') ++i;
        ++i;
      }
      continue;
    }
    if (c == '(') {
      ++depth;
      continue;
    }
    if (c == ')' && depth) {
      --depth;
      continue;
    }
    if ((c == ',' && depth == 0) || i == str.size()) {
      auto piece = trim(str.substr(start, i - start));
      if (!piece.empty()) out.push_back(parse_mailbox(piece));
      start = i + 1;
    }
  }
  return out;
}

std::string crlf_join_headers(const Rfc822Message& m) {
  std::string o;
  for (auto& h : m.headers) {
    o += h.first;
    o += ": ";
    o += h.second;
    o += "\r\n";
  }
  o += "\r\n";
  o += m.body;
  return o;
}

}  // namespace mail
