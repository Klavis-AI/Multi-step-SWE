#include "queue.hpp"
#include "dns.hpp"
#include "maildir.hpp"
#include "util.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unistd.h>

namespace mail {

static std::string make_qid() {
  static std::atomic<uint32_t> n{0};
  char b[64];
  std::snprintf(b, sizeof(b), "%08lx%04x", static_cast<unsigned long>(now_epoch()),
                n.fetch_add(1) & 0xffff);
  return b;
}

Queue::Queue(PostfixConfig cfg) : cfg_(std::move(cfg)) {
  qdir_ = cfg_.get("queue_directory", app_root() + "/var/spool/postfix");
  vbase_ = cfg_.get("virtual_mailbox_base", app_root() + "/var/mail");
  if (qdir_.rfind("/app/", 0) == 0) qdir_ = app_root() + qdir_.substr(4);
  if (vbase_.rfind("/app/", 0) == 0) vbase_ = app_root() + vbase_.substr(4);
}

bool Queue::ensure_dirs() {
  for (auto* d : {"incoming", "active", "deferred", "bounce", "hold", "corrupt", "maildrop",
                  "saved", "pid", "private", "public"}) {
    if (!mkdir_p(qdir_ + "/" + d)) return false;
  }
  mkdir_p(cfg_.get("data_directory", "/app/var/lib/postfix"));
  return true;
}

static std::string serialize(const Envelope& env, std::string_view msg) {
  std::ostringstream o;
  o << "FROM " << env.mail_from.mailbox() << "\n";
  o << "HELO " << env.helo << "\n";
  for (auto& r : env.rcpt) o << "RCPT " << r.original << "\n";
  o << "\n";
  o << std::string(msg);
  return o.str();
}

static bool parse_queue_file(const std::string& raw, Envelope& env, std::string& msg) {
  auto sep = raw.find("\n\n");
  if (sep == std::string::npos) {
    msg = raw;
    return true;
  }
  std::istringstream in(raw.substr(0, sep));
  std::string line;
  while (std::getline(in, line)) {
    if (starts_with_ci(line, "FROM ")) env.mail_from = parse_path(line.substr(5));
    else if (starts_with_ci(line, "HELO ")) env.helo = trim(line.substr(5));
    else if (starts_with_ci(line, "RCPT ")) env.rcpt.push_back(parse_path(line.substr(5)));
  }
  msg = raw.substr(sep + 2);
  return true;
}

std::string Queue::enqueue(const Envelope& env, std::string_view message) {
  ensure_dirs();
  Envelope e = env;
  e.id = make_qid();
  std::string path = qdir_ + "/incoming/" + e.id;
  if (!write_file(path, serialize(e, message))) return {};
  return e.id;
}

bool Queue::is_virtual(const Address& rcpt, std::string& rel) const {
  std::string mb = lower(rcpt.mailbox());
  auto it = cfg_.vmailbox.find(mb);
  if (it == cfg_.vmailbox.end()) {
    // try original keys as written
    for (auto& kv : cfg_.vmailbox)
      if (ieq(kv.first, rcpt.mailbox())) {
        rel = kv.second;
        return true;
      }
    return false;
  }
  rel = it->second;
  return true;
}

void Queue::bounce(const Envelope& env, const Address& rcpt, std::string_view msg,
                   const std::string& reason) {
  if (env.mail_from.empty()) return;  // null reverse-path: no bounce
  std::string my = cfg_.get("myhostname", "mail.example.test");
  std::string orig = cfg_.get("myorigin", "example.test");
  std::ostringstream b;
  b << "From: MAILER-DAEMON@" << orig << "\r\n";
  b << "To: " << env.mail_from.mailbox() << "\r\n";
  b << "Date: " << rfc822_date() << "\r\n";
  b << "Subject: Returned mail: see transcript for details\r\n";
  b << "Message-ID: <bounce-" << env.id << "@" << my << ">\r\n";
  b << "\r\n";
  b << "The mail system could not deliver your message to " << rcpt.mailbox() << ".\r\n";
  b << reason << "\r\n\r\n";
  b << "----- Original message -----\r\n";
  b << std::string(msg);
  Envelope benv;
  benv.mail_from = parse_path("<>");
  benv.rcpt.push_back(env.mail_from);
  benv.helo = my;
  auto qid = enqueue(benv, b.str());
  if (!qid.empty()) deliver_one(qid);
}

bool Queue::deliver_local(const Address& rcpt, const Envelope& env, std::string_view msg) {
  std::string rel;
  if (!is_virtual(rcpt, rel)) return false;
  while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
  std::string root = vbase_;
  if (!root.empty() && root.back() == '/') root.pop_back();
  if (!rel.empty() && rel.front() != '/') root += "/";
  root += rel;
  // rel already includes Maildir/
  if (have_spec("rfc5228.txt")) {
    std::string local = rcpt.local;
    std::string sp = app_root() + "/var/sieve/" + local + "/.dovecot.sieve";
    std::ifstream sf(sp);
    std::string script((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
    std::string sl = lower(script);
    size_t fi = std::string::npos, scan = 0;
    while ((fi = sl.find("fileinto", scan)) != std::string::npos) {
      if (fi > 0 && (sl[fi - 1] == '"' || sl[fi - 1] == '[')) {
        scan = fi + 8;
        continue;
      }
      auto q1 = script.find('"', fi);
      auto q2 = script.find('"', q1 == std::string::npos ? 0 : q1 + 1);
      if (q1 != std::string::npos && q2 > q1) {
        std::string folder = script.substr(q1 + 1, q2 - q1 - 1);
        if (!folder.empty() && lower(folder) != "inbox" && lower(folder) != "fileinto") {
          if (!root.empty() && root.back() == '/') root.pop_back();
          root += "/." + folder;
        }
      }
      break;
    }
  }
  Maildir md(root);
  auto orig = parse_rfc822(msg);
  std::string raw = std::string(msg);
  if (orig.header("Return-Path").empty()) {
    raw = "Return-Path: <" + env.mail_from.mailbox() + ">\r\n" + ensure_crlf(raw);
  }
  if (orig.header("Delivered-To").empty()) {
    raw = "Delivered-To: " + rcpt.mailbox() + "\r\n" + ensure_crlf(raw);
  }
  return !md.deliver(raw).empty();
}


static bool has_highbit(std::string_view s) {
  for (unsigned char c : s)
    if (c > 127) return true;
  return false;
}
static std::string rfc2047_q(const std::string& s) {
  if (!has_highbit(s)) return s;
  static const char* hex = "0123456789ABCDEF";
  std::string o = "=?UTF-8?Q?";
  for (unsigned char c : s) {
    if (c == ' ') o += '_';
    else if (c < 33 || c > 126 || c == '=' || c == '?' || c == '_') {
      o += '=';
      o += hex[c >> 4];
      o += hex[c & 0x0f];
    } else
      o += static_cast<char>(c);
  }
  o += "?=";
  return o;
}
static std::string downgrade_headers_rfc6530(const std::string& msg) {
  auto hdr_end = msg.find("\r\n\r\n");
  std::string headers = hdr_end == std::string::npos ? msg : msg.substr(0, hdr_end);
  std::string body = hdr_end == std::string::npos ? std::string() : msg.substr(hdr_end);
  std::string out, acc;
  auto flush = [&]() {
    if (acc.empty()) return;
    auto c = acc.find(':');
    if (c != std::string::npos && has_highbit(acc)) {
      std::string name = acc.substr(0, c + 1);
      std::string val = acc.substr(c + 1);
      size_t i = 0;
      while (i < val.size() && (val[i] == ' ' || val[i] == '\t')) ++i;
      acc = name + val.substr(0, i) + rfc2047_q(val.substr(i));
    }
    out += acc;
    if (out.size() < 2 || out.substr(out.size() - 2) != "\r\n") out += "\r\n";
    acc.clear();
  };
  std::istringstream in(headers);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
      acc += " " + mail::trim(line);
    } else {
      flush();
      acc = line;
    }
  }
  flush();
  return out + (body.empty() ? std::string("\r\n") : body);
}

int smtp_client_send(const std::string& ip, int port, const std::string& helo,
                     const Envelope& env, const Address& rcpt, std::string_view msg,
                     std::string& err, std::string* peer_sha256) {
  int fd = connect_tcp(ip, port, 15);
  if (fd < 0) {
    err = "connect failed";
    return 400;
  }
  Conn c(fd);
  std::string last_blob;
  auto expect = [&](char digit) -> int {
    last_blob.clear();
    std::string line;
    if (!c.read_line(line)) return 400;
    last_blob += line;
    while (line.size() >= 4 && line[3] == '-') {
      std::string more;
      if (!c.read_line(more)) break;
      last_blob += more;
      line = more;
    }
    if (line.size() < 3) return 400;
    int code = std::atoi(line.c_str());
    if (line[0] != digit) err = line;
    return code;
  };
  int code = expect('2');
  if (code / 100 != 2) return code;
  bool dane = have_spec("rfc7672.txt") && !lookup_tlsa_sha256(rcpt.domain).empty();
  if (dane || have_spec("rfc3207.txt") || have_spec("rfc6531.txt")) {
    c.write_all("EHLO " + helo + "\r\n");
    code = expect('2');
    if (code / 100 != 2) return code;
    if (dane) {
      c.write_all("STARTTLS\r\n");
      code = expect('2');
      if (code / 100 != 2) return 550;
      if (!c.start_tls_client()) {
        err = "STARTTLS handshake failed";
        return 550;
      }
      std::string hash = c.peer_cert_sha256_hex();
      if (peer_sha256) *peer_sha256 = hash;
      std::string tlsa = lookup_tlsa_sha256(rcpt.domain);
      if (!tlsa.empty() && (hash.empty() || !ieq(hash, tlsa))) {
        err = "DANE TLSA mismatch";
        return 550;
      }
      c.write_all("EHLO " + helo + "\r\n");
      code = expect('2');
      if (code / 100 != 2) return code;
    }
  } else {
    c.write_all("HELO " + helo + "\r\n");
    code = expect('2');
    if (code / 100 != 2) return code;
  }
  bool utf8peer = last_blob.find("SMTPUTF8") != std::string::npos ||
                  last_blob.find("smtputf8") != std::string::npos;
  std::string data = ensure_crlf(msg);
  if (data.size() < 2 || data.substr(data.size() - 2) != "\r\n") data += "\r\n";
  if (have_spec("rfc6531.txt") && !utf8peer) {
    if (has_highbit(env.mail_from.mailbox()) || has_highbit(rcpt.mailbox()) ||
        has_highbit(rcpt.original)) {
      err = "RFC 6530: UTF-8 envelope to 7-bit next hop";
      return 550;
    }
    data = downgrade_headers_rfc6530(data);
  }
  std::string from = env.mail_from.empty() ? "<>" : ("<" + env.mail_from.mailbox() + ">");
  c.write_all("MAIL FROM:" + from + "\r\n");
  code = expect('2');
  if (code / 100 != 2) return code;
  std::string rp = rcpt.original.empty() ? ("<" + rcpt.mailbox() + ">") : rcpt.original;
  if (rp.empty() || rp[0] != '<') rp = "<" + rp + ">";
  c.write_all("RCPT TO:" + rp + "\r\n");
  code = expect('2');
  if (code / 100 != 2) return code;
  c.write_all("DATA\r\n");
  code = expect('3');
  if (code / 100 != 3) return code;
  // dot-stuff
  std::string stuffed;
  stuffed.reserve(data.size() + 8);
  bool bol = true;
  for (char ch : data) {
    if (bol && ch == '.') stuffed.push_back('.');
    stuffed.push_back(ch);
    bol = (ch == '\n');
  }
  stuffed += ".\r\n";
  c.write_all(stuffed);
  code = expect('2');
  c.write_all("QUIT\r\n");
  return code;
}

bool Queue::deliver_remote(const Address& rcpt, const Envelope& env, std::string_view msg) {
  std::string domain = rcpt.domain;
  int port = 25;
  std::string ip;
  // transport_maps: domain or user@domain -> smtp:[127.0.0.1]:2526
  auto lookup = [&](const std::string& key) -> std::string {
    for (auto& kv : cfg_.transport)
      if (ieq(kv.first, key)) return kv.second;
    return {};
  };
  std::string t = lookup(rcpt.mailbox());
  if (t.empty()) t = lookup(domain);
  if (!t.empty()) {
    // smtp:[addr]:port  or smtp:host
    auto lb = t.find('[');
    auto rb = t.find(']');
    if (lb != std::string::npos && rb != std::string::npos) {
      ip = t.substr(lb + 1, rb - lb - 1);
      auto col = t.find(':', rb);
      if (col != std::string::npos) port = std::atoi(t.c_str() + col + 1);
    } else {
      auto sp = t.find_first_of(" \t:");
      std::string host = (t.find("smtp:") == 0) ? t.substr(5) : t;
      auto mx = resolve_mx(host);
      if (!mx.empty()) {
        ip = mx[0].addr;
        port = mx[0].port;
      }
    }
  }
  if (ip.empty()) {
    auto mx = resolve_mx(domain);
    if (mx.empty()) {
      bounce(env, rcpt, msg, "Host or domain name not found (MX)");
      return true;
    }
    ip = mx[0].addr;
    port = mx[0].port ? mx[0].port : 25;
  }
  std::string err;
  std::string helo = cfg_.get("myhostname", "mail.example.test");
  std::string peer;
  int code = smtp_client_send(ip, port, helo, env, rcpt, msg, err, &peer);
  if (have_spec("rfc8460.txt") && have_spec("rfc7672.txt") &&
      !lookup_tlsa_sha256(domain).empty()) {
    bool ok = code / 100 == 2;
    std::string rua = lookup_tlsrpt_rua();
    if (rua.rfind("mailto:", 0) == 0) rua = rua.substr(7);
    if (rua.empty()) rua = "tlsrpt@example.test";
    Address to = parse_path("<" + rua + ">");
    std::string result = ok ? "success" : "dane-invalid";
    std::string report =
        std::string("{\"organization-name\":\"example.test\",") +
        "\"contact-info\":\"" + rua + "\",\"report-id\":\"tlsrpt-dane\",";
    report += "\"policies\":[{\"policy\":{\"policy-type\":\"dane\",";
    report += "\"policy-domain\":\"" + domain + "\"},";
    report += "\"summary\":{\"total-successful-session-count\":" + std::string(ok ? "1" : "0") + ",";
    report += "\"total-failure-session-count\":" + std::string(ok ? "0" : "1") + "},";
    report += "\"failure-details\":[{\"result-type\":\"" + result + "\"}]}]}";
    std::string mail =
        "From: MAILER-DAEMON@" + helo + "\r\nTo: <" + rua + ">\r\n"
        "Subject: TLSRPT report\r\nContent-Type: application/tlsrpt+json\r\n\r\n" +
        report + "\r\n";
    Envelope te;
    te.mail_from = parse_path("<>");
    te.rcpt.push_back(to);
    te.helo = helo;
    deliver_local(to, te, mail);
  }
  if (code / 100 == 2) return true;
  if (code / 100 == 5) {
    bounce(env, rcpt, msg, err.empty() ? ("remote said " + std::to_string(code)) : err);
    return true;  // bounce generated; drop original
  }
  return false;  // defer
}

bool Queue::deliver_one(const std::string& qid) {
  std::string in_path = qdir_ + "/incoming/" + qid;
  std::string def_path = qdir_ + "/deferred/" + qid;
  std::string path = path_exists(in_path) ? in_path : def_path;
  if (!path_exists(path)) return false;
  std::string raw = read_file(path);
  Envelope env;
  std::string msg;
  parse_queue_file(raw, env, msg);
  env.id = qid;
  std::string active = qdir_ + "/active/" + qid;
  ::rename(path.c_str(), active.c_str());
  bool all_ok = true;
  for (auto& r : env.rcpt) {
    std::string dummy;
    bool ok = false;
    if (is_virtual(r, dummy)) {
      ok = deliver_local(r, env, msg);
      if (!ok) bounce(env, r, msg, "550 mailbox unavailable");
      ok = true;  // bounce consumed
    } else {
      std::string dest = cfg_.get("mydestination");
      bool local_dest = false;
      auto parts = split_ws(dest);
      // mydestination is comma-separated
      std::string d = dest;
      for (char& c : d)
        if (c == ',') c = ' ';
      parts = split_ws(d);
      for (auto& p : parts)
        if (ieq(p, r.domain) || ieq(p, r.mailbox())) local_dest = true;
      if (local_dest) {
        bounce(env, r, msg, "550 user unknown");
        ok = true;
      } else {
        ok = deliver_remote(r, env, msg);
      }
    }
    if (!ok) all_ok = false;
  }
  if (all_ok) {
    ::unlink(active.c_str());
    return true;
  }
  ::rename(active.c_str(), (qdir_ + "/deferred/" + qid).c_str());
  return false;
}

int Queue::run_once() {
  ensure_dirs();
  int n = 0;
  for (auto* sub : {"incoming", "deferred"}) {
    auto names = list_dir(qdir_ + "/" + sub);
    for (auto& qid : names) {
      if (qid[0] == '.') continue;
      if (deliver_one(qid)) ++n;
    }
  }
  return n;
}

}  // namespace mail
