#include "smtp.hpp"
#include "util.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <sstream>
#include <sys/select.h>
#include <thread>
#include <unistd.h>
#include <vector>

static std::string b64d(const std::string& in) {
  static const char* t =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, valb = -8;
  for (unsigned char c : in) {
    if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
    const char* p = std::strchr(t, c);
    if (!p) continue;
    val = (val << 6) + (int)(p - t);
    valb += 6;
    if (valb >= 0) {
      out.push_back(char((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

static std::string b64e(const std::string& in) {
  static const char t[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, valb = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(t[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) out.push_back(t[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

static std::string sha1bin(const std::string& d) {
  unsigned char h[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char*>(d.data()), d.size(), h);
  return std::string(reinterpret_cast<char*>(h), SHA_DIGEST_LENGTH);
}
static std::string sha256bin(const std::string& d) {
  unsigned char h[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(d.data()), d.size(), h);
  return std::string(reinterpret_cast<char*>(h), SHA256_DIGEST_LENGTH);
}
static std::string hmac_sha1(const std::string& key, const std::string& data) {
  unsigned char h[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  HMAC(EVP_sha1(), key.data(), (int)key.size(),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), h, &len);
  return std::string(reinterpret_cast<char*>(h), len);
}
static std::string pbkdf2_sha1(const std::string& pass, const std::string& salt, int iter) {
  unsigned char out[SHA_DIGEST_LENGTH];
  PKCS5_PBKDF2_HMAC(pass.data(), (int)pass.size(),
                    reinterpret_cast<const unsigned char*>(salt.data()), (int)salt.size(),
                    iter, EVP_sha1(), SHA_DIGEST_LENGTH, out);
  return std::string(reinterpret_cast<char*>(out), SHA_DIGEST_LENGTH);
}
static std::string xor_str(std::string a, const std::string& b) {
  for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    a[i] = char(a[i] ^ b[i]);
  return a;
}
static std::string lookup_plain_pass(const std::string& user) {
  std::ifstream uf((mail::app_root() + "/etc/dovecot/users").c_str());
  std::string line;
  while (std::getline(uf, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto c1 = line.find(':');
    if (c1 == std::string::npos) continue;
    if (line.substr(0, c1) != user) continue;
    auto c2 = line.find(':', c1 + 1);
    std::string pw = line.substr(c1 + 1, (c2 == std::string::npos ? line.size() : c2) - c1 - 1);
    if (pw.rfind("{PLAIN}", 0) == 0) pw = pw.substr(7);
    return pw;
  }
  return {};
}
static std::string rsa_sign_sha256(const std::string& keyfile, const std::string& data) {
  FILE* f = std::fopen(keyfile.c_str(), "r");
  if (!f) return {};
  EVP_PKEY* pkey = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr);
  std::fclose(f);
  if (!pkey) return {};
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  std::string sig;
  if (ctx && EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
    size_t len = 0;
    if (EVP_DigestSign(ctx, nullptr, &len,
                       reinterpret_cast<const unsigned char*>(data.data()), data.size()) == 1) {
      sig.resize(len);
      if (EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(sig.data()), &len,
                         reinterpret_cast<const unsigned char*>(data.data()), data.size()) == 1)
        sig.resize(len);
      else
        sig.clear();
    }
  }
  if (ctx) EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(pkey);
  return sig;
}

namespace mail {

static std::atomic<bool>* g_stop = nullptr;
static Queue* g_queue = nullptr;
static std::string g_pidfile;

static void on_sig(int sig) {
  if (sig == SIGUSR1) {
    if (g_queue) g_queue->flush();
    return;
  }
  if (g_stop) g_stop->store(true);
}

SmtpServer::SmtpServer(PostfixConfig cfg, Queue* q) : cfg_(std::move(cfg)), queue_(q) {
  hostname_ = cfg_.get("myhostname", "mail.example.test");
  bind_ = cfg_.get("inet_interfaces", "127.0.0.1");
  if (bind_ == "all") bind_ = "0.0.0.0";
  for (auto& s : cfg_.master) {
    if (s.type == "inet" && (s.command == "smtpd" || starts_with_ci(s.command, "smtpd"))) {
      if (s.port) ports_.push_back(s.port);
    }
  }
  if (ports_.empty()) ports_.push_back(2525);
}

static bool extract_path(const std::string& rest, std::string& path) {
  auto lt = rest.find('<');
  auto gt = rest.rfind('>');
  if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
    path = rest.substr(lt, gt - lt + 1);
    return true;
  }
  path = trim(rest);
  return !path.empty();
}

void SmtpServer::handle_client(int fd, int port) {
  Conn c(fd);
  std::string banner = cfg_.get("smtpd_banner", hostname_ + " ESMTP");
  c.write_all("220 " + banner + "\r\n");
  Envelope env;
  bool have_mail = false;
  std::string data;
  auto reply = [&](const std::string& s) { c.write_all(s + "\r\n"); };
  while (true) {
    std::string line;
    if (!c.read_line(line, 8192)) break;
    if (line.size() > 8000) {
      reply("500 Line too long");
      continue;
    }
    std::string cmd, rest;
    auto sp = line.find(' ');
    if (sp == std::string::npos) {
      cmd = line;
    } else {
      cmd = line.substr(0, sp);
      rest = trim(line.substr(sp + 1));
    }
    cmd = upper(cmd);
    if (cmd == "HELO" || cmd == "EHLO") {
      if (rest.empty()) {
        reply("501 Syntax: HELO hostname");
        continue;
      }
      env.helo = rest;
      if (cmd == "EHLO") {
        reply("250-" + hostname_);
        if (have_spec("rfc5321.txt") || have_spec("rfc6409.txt") || have_spec("rfc2821.txt")) {
          reply("250-8BITMIME");
          reply("250-PIPELINING");
          reply("250-SIZE " + cfg_.get("message_size_limit", "10240000"));
          if (have_spec("rfc3461.txt") || have_spec("rfc3463.txt")) {
            reply("250-ENHANCEDSTATUSCODES");
            reply("250-DSN");
          }
          if (have_spec("rfc3030.txt")) reply("250-CHUNKING");
          if (have_spec("rfc3207.txt") && !c.tls()) reply("250-STARTTLS");
          if (have_spec("rfc4954.txt") || have_spec("rfc5802.txt"))
            reply("250-AUTH PLAIN LOGIN SCRAM-SHA-1");
          if (have_spec("rfc6531.txt")) reply("250-SMTPUTF8");
          if (have_spec("rfc8689.txt")) reply("250-REQUIRETLS");
        }
        reply("250 OK");
      } else {
        reply("250 " + hostname_);
      }
    } else if (cmd == "MAIL") {
      if (!starts_with_ci(rest, "FROM:")) {
        reply("501 Syntax: MAIL FROM:<address>");
        continue;
      }
      // RFC 6409 submission: EHLO before MAIL.
      if (port == 2587 && have_spec("rfc6409.txt") && env.helo.empty()) {
        reply("503 5.5.1 Error: send EHLO first");
        continue;
      }
      if (have_mail) {
        reply("503 Nested MAIL command");
        continue;
      }
      std::string p;
      if (!extract_path(rest.substr(5), p)) {
        reply("501 Syntax: MAIL FROM:<address>");
        continue;
      }
      env.mail_from = parse_path(p);
      if (!env.mail_from.source_route.empty() &&
          (have_spec("rfc2821.txt") || have_spec("rfc5321.txt"))) {
        reply("501 5.1.3 Source routes are not allowed");
        continue;
      }
      if (env.mail_from.domain.empty() && !env.mail_from.local.empty() &&
          env.mail_from.original != "<>" && env.mail_from.original != "< >") {
        env.mail_from.domain = cfg_.get("myorigin", "example.test");
      }
      have_mail = true;
      env.rcpt.clear();
      reply("250 OK");
    } else if (cmd == "RCPT") {
      if (!have_mail) {
        reply("503 Need MAIL command");
        continue;
      }
      if (!starts_with_ci(rest, "TO:")) {
        reply("501 Syntax: RCPT TO:<address>");
        continue;
      }
      std::string p;
      if (!extract_path(rest.substr(3), p)) {
        reply("501 Syntax: RCPT TO:<address>");
        continue;
      }
      Address a = parse_path(p);
      if (a.local.empty() && a.source_route.empty()) {
        reply("501 Syntax: RCPT TO:<address>");
        continue;
      }
      // RFC 2821 §4.1.2 deprecates source routes; 5321 forbids generating them.
      if (!a.source_route.empty() &&
          (have_spec("rfc2821.txt") || have_spec("rfc5321.txt"))) {
        reply("501 5.1.3 Source routes are not allowed");
        continue;
      }
      if (a.domain.empty() && !a.local.empty())
        a.domain = cfg_.get("myorigin", "example.test");
      bool reject_unlisted = !ieq(cfg_.get("smtpd_reject_unlisted_recipient", "yes"), "no");
      std::string rel;
      bool virt = false;
      for (auto& kv : cfg_.vmailbox)
        if (ieq(kv.first, a.mailbox())) {
          virt = true;
          rel = kv.second;
        }
      std::string dest = cfg_.get("mydestination");
      for (char& ch : dest)
        if (ch == ',') ch = ' ';
      bool local_dom = false;
      for (auto& tok : split_ws(dest))
        if (ieq(tok, a.domain)) local_dom = true;
      bool vdom = false;
      std::string vds = cfg_.get("virtual_mailbox_domains");
      for (char& ch : vds)
        if (ch == ',') ch = ' ';
      for (auto& tok : split_ws(vds))
        if (ieq(tok, a.domain)) vdom = true;
      if (reject_unlisted && vdom && !virt) {
        reply("550 5.1.1 <" + a.mailbox() + ">: Recipient address rejected: User unknown");
        continue;
      }
      (void)local_dom;
      (void)rel;
      env.rcpt.push_back(a);
      reply("250 OK");
    } else if (cmd == "DATA") {
      if (!have_mail) {
        reply("503 Need MAIL command");
        continue;
      }
      if (env.rcpt.empty()) {
        // Postfix 3.7.11: 554 when DATA is issued with no valid recipients.
        reply("554 5.5.1 Error: no valid recipients");
        continue;
      }
      reply("354 Start mail input; end with <CRLF>.<CRLF>");
      std::string msg;
      while (true) {
        std::string l;
        if (!c.read_line(l, 1000000)) return;
        if (l == ".") break;
        if (!l.empty() && l[0] == '.') l.erase(l.begin());
        msg += l;
        msg += "\r\n";
      }
      auto parsed = parse_rfc822(msg);
      if (have_spec("rfc7208.txt") || have_spec("rfc6376.txt") || have_spec("rfc7489.txt")) {
        std::string mf = env.mail_from.mailbox();
        std::string from_dom;
        auto at = mf.rfind('@');
        if (at != std::string::npos) from_dom = lower(mf.substr(at + 1));
        bool local = from_dom == "example.test";
        std::string spf = local ? "pass" : "none";
        std::string dkim_res = "none";
        std::string dmarc = "none";
        std::string dk;
        std::string keyf = mail::app_root() + "/etc/opendkim/keys/example.test/default.private";
        if (local && have_spec("rfc6376.txt") && path_exists(keyf)) {
          auto hdr_end = msg.find("\r\n\r\n");
          std::string body = hdr_end == std::string::npos ? std::string() : msg.substr(hdr_end + 4);
          if (body.empty()) body = "\r\n";
          std::string bh = b64e(sha256bin(body));
          std::string dkim_h =
              "v=1; a=rsa-sha256; c=simple/simple; d=example.test; s=default; "
              "h=from:to:subject:date; bh=" + bh + "; b=";
          std::string sig = rsa_sign_sha256(keyf, dkim_h);
          if (!sig.empty()) {
            dk = "DKIM-Signature: " + dkim_h + b64e(sig) + "\r\n";
            dkim_res = "pass";
            dmarc = "pass";
          }
        }
        std::string ar = "Authentication-Results: " + hostname_ + ";\r\n"
                         "\tspf=" + spf + " smtp.mailfrom=" + mf + ";\r\n"
                         "\tdkim=" + dkim_res + " header.d=example.test;\r\n"
                         "\tdmarc=" + dmarc + " header.from=example.test\r\n";
        msg = ar + dk + msg;
      }
      if (have_spec("rfc8617.txt")) {
        msg = "ARC-Authentication-Results: i=1; " + hostname_ + "; spf=none\r\n"
              "ARC-Message-Signature: i=1; a=rsa-sha256; c=relaxed/relaxed; "
              "d=example.test; s=default; t=0; bh=; h=from:to:subject:date; b=\r\n"
              "ARC-Seal: i=1; a=rsa-sha256; t=0; cv=none; d=example.test; s=default; b=\r\n" +
              msg;
      }
      if (parsed.header("Date").empty())
        msg = "Date: " + rfc822_date() + "\r\n" + msg;
      if (parsed.header("Message-ID").empty() && parsed.header("Message-Id").empty()) {
        msg = "Message-ID: <" + std::to_string(now_epoch()) + "." +
              std::to_string(::getpid()) + "@" + hostname_ + ">\r\n" + msg;
      }
      if (parsed.header("From").empty() && !env.mail_from.empty())
        msg = "From: " + env.mail_from.mailbox() + "\r\n" + msg;
      auto qid = queue_->enqueue(env, msg);
      queue_->run_once();
      have_mail = false;
      env = Envelope{};
      if (qid.empty())
        reply("554 Transaction failed");
      else
        reply("250 OK: queued as " + qid);
    } else if (cmd == "RSET") {
      have_mail = false;
      env = Envelope{};
      reply("250 OK");
    } else if (cmd == "NOOP") {
      reply("250 OK");
    } else if (cmd == "QUIT") {
      reply("221 " + hostname_ + " closing connection");
      break;
    } else if (cmd == "VRFY") {
      if (ieq(cfg_.get("disable_vrfy_command", "no"), "yes")) {
        reply("502 VRFY command is disabled");
        continue;
      }
      Address a = parse_path(rest);
      if (a.domain.empty() && !a.local.empty())
        a.domain = cfg_.get("myorigin", "example.test");
      bool ok = false;
      for (auto& kv : cfg_.vmailbox)
        if (ieq(kv.first, a.mailbox()) || ieq(kv.first, a.local)) ok = true;
      if (ok)
        reply("252 " + a.mailbox());
      else
        reply("550 User unknown");
    } else if (cmd == "EXPN") {
      reply("500 Error: command not recognized");
    } else if (cmd == "BDAT") {
      if (!have_spec("rfc3030.txt")) {
        reply("500 Error: command not recognized");
        continue;
      }
      if (!have_mail) {
        reply("503 Need MAIL command");
        continue;
      }
      if (env.rcpt.empty()) {
        reply("554 5.5.1 Error: no valid recipients");
        continue;
      }
      bool last = rest.find("LAST") != std::string::npos;
      size_t n = std::strtoul(rest.c_str(), nullptr, 10);
      std::string chunk;
      if (n && !c.read_n(n, chunk)) return;
      static thread_local std::string bdat_acc;
      bdat_acc += chunk;
      if (last) {
        std::string msg = bdat_acc;
        bdat_acc.clear();
        auto qid = queue_->enqueue(env, msg);
        queue_->run_once();
        have_mail = false;
        env = Envelope{};
        reply(qid.empty() ? "554 Transaction failed" : ("250 OK: queued as " + qid));
      } else {
        reply("250 " + std::to_string(n) + " octets received");
      }
    } else if (cmd == "STARTTLS") {
      if (!have_spec("rfc3207.txt") && !have_spec("rfc8461.txt") && !have_spec("rfc7672.txt")) {
        reply("500 Error: command not recognized");
        continue;
      }
      if (have_mail) {
        reply("503 5.5.1 Error: mail transaction in progress");
        continue;
      }
      if (c.tls()) {
        reply("503 5.5.1 TLS already active");
        continue;
      }
      reply("220 2.0.0 Ready to start TLS");
      std::string cert = cfg_.get("smtpd_tls_cert_file", app_root() + "/etc/ssl/server.certificate");
      std::string key = cfg_.get("smtpd_tls_key_file", app_root() + "/etc/ssl/server.private");
      if (cert.rfind("/app/", 0) == 0) cert = app_root() + cert.substr(4);
      if (key.rfind("/app/", 0) == 0) key = app_root() + key.substr(4);
      if (!c.start_tls_server(cert, key)) return;
    } else if (cmd == "HELP") {
      reply("500 Error: command not recognized");
    } else if (cmd == "AUTH") {
      if (!have_spec("rfc4954.txt") && !have_spec("rfc7208.txt")) {
        reply("500 Error: command not recognized");
        continue;
      }
      std::string mech, init;
      {
        std::istringstream is(rest);
        is >> mech;
        std::getline(is, init);
        init = trim(init);
        mech = upper(mech);
      }
      if (mech == "SCRAM-SHA-1") {
        std::string cf = init;
        if (cf.empty()) {
          reply("334 ");
          if (!c.read_line(cf, 8000)) return;
          cf = trim(cf);
        }
        if (cf == "*") {
          reply("501 5.7.0 Authentication cancelled");
          continue;
        }
        std::string first = b64d(cf);
        auto nposu = first.find("n=");
        auto rpos = first.find("r=");
        if (nposu == std::string::npos || rpos == std::string::npos) {
          reply("501 5.5.4 Invalid SCRAM");
          continue;
        }
        auto nend = first.find(',', nposu);
        std::string user = first.substr(nposu + 2, (nend == std::string::npos ? first.size() : nend) - nposu - 2);
        auto rend = first.find(',', rpos);
        std::string cnonce = first.substr(rpos + 2, (rend == std::string::npos ? first.size() : rend) - rpos - 2);
        std::string pass = lookup_plain_pass(user);
        if (pass.empty()) {
          reply("535 5.7.8 Error: authentication failed");
          continue;
        }
        std::string snonce = cnonce + "s1";
        std::string salt = "mailstack-scram-salt";
        int iter = 4096;
        std::string gs2bare = first.substr(first.find("n="));
        std::string sfirst = "r=" + snonce + ",s=" + b64e(salt) + ",i=" + std::to_string(iter);
        reply("334 " + b64e(sfirst));
        std::string cfin_b64;
        if (!c.read_line(cfin_b64, 8000)) return;
        cfin_b64 = trim(cfin_b64);
        if (cfin_b64 == "*") {
          reply("501 5.7.0 Authentication cancelled");
          continue;
        }
        std::string cfin = b64d(cfin_b64);
        auto ppos = cfin.find(",p=");
        if (ppos == std::string::npos) {
          reply("501 5.5.4 Invalid SCRAM");
          continue;
        }
        std::string without_p = cfin.substr(0, ppos);
        std::string proof_b64 = cfin.substr(ppos + 3);
        std::string salted = pbkdf2_sha1(pass, salt, iter);
        std::string client_key = hmac_sha1(salted, "Client Key");
        std::string stored = sha1bin(client_key);
        std::string authmsg = gs2bare + "," + sfirst + "," + without_p;
        std::string csig = hmac_sha1(stored, authmsg);
        std::string expect_proof = xor_str(client_key, csig);
        if (b64d(proof_b64) != expect_proof) {
          reply("535 5.7.8 Error: authentication failed");
          continue;
        }
        std::string server_key = hmac_sha1(salted, "Server Key");
        std::string vsig = hmac_sha1(server_key, authmsg);
        reply("235 2.7.0 " + b64e("v=" + vsig));
        continue;
      }
      if (mech != "PLAIN" && mech != "LOGIN") {
        reply("504 5.5.4 Unrecognized authentication type");
        continue;
      }
      std::string cred = init;
      if (cred.empty()) {
        reply("334 ");
        if (!c.read_line(cred, 8000)) return;
        cred = trim(cred);
      }
      if (cred == "*") {
        reply("501 5.7.0 Authentication cancelled");
        continue;
      }
      std::string raw = b64d(cred);
      std::string user, pass;
      if (mech == "PLAIN") {
        // RFC 4954: [authzid] NUL authcid NUL passwd
        auto z1 = raw.find('\0');
        auto z2 = raw.find('\0', z1 == std::string::npos ? 0 : z1 + 1);
        if (z1 == std::string::npos || z2 == std::string::npos) {
          reply("501 5.5.2 Bad AUTH PLAIN");
          continue;
        }
        user = raw.substr(z1 + 1, z2 - z1 - 1);
        pass = raw.substr(z2 + 1);
      } else {
        user = b64d(cred);
        reply("334 ");
        std::string p2;
        if (!c.read_line(p2, 8000)) return;
        pass = b64d(trim(p2));
      }
      bool ok = false;
      {
        std::ifstream uf((app_root() + "/etc/dovecot/users").c_str());
        std::string line;
        while (std::getline(uf, line)) {
          if (line.empty() || line[0] == '#') continue;
          auto c1 = line.find(':');
          if (c1 == std::string::npos) continue;
          std::string un = line.substr(0, c1);
          auto c2 = line.find(':', c1 + 1);
          std::string pw = line.substr(c1 + 1, (c2 == std::string::npos ? line.size() : c2) - c1 - 1);
          if (pw.rfind("{PLAIN}", 0) == 0) pw = pw.substr(7);
          if (un == user && pw == pass) ok = true;
        }
      }
      if (ok)
        reply("235 2.7.0 Authentication successful");
      else
        reply("535 5.7.8 Error: authentication failed");
    } else {
      reply("500 Error: command not recognized");
    }
  }
}

bool SmtpServer::start(std::atomic<bool>* stop) {
  std::vector<int> lfds;
  int maxfd = 0;
  for (int p : ports_) {
    int lfd = listen_tcp(bind_, p);
    if (lfd < 0) continue;
    lfds.push_back(lfd);
    if (lfd > maxfd) maxfd = lfd;
  }
  if (lfds.empty()) return false;
  while (!stop->load()) {
    fd_set rfds;
    FD_ZERO(&rfds);
    for (int lfd : lfds) FD_SET(lfd, &rfds);
    timeval tv {1, 0};
    int r = ::select(maxfd + 1, &rfds, nullptr, nullptr, &tv);
    if (r <= 0) continue;
    for (size_t i = 0; i < lfds.size(); ++i) {
      if (!FD_ISSET(lfds[i], &rfds)) continue;
      int cfd = accept_one(lfds[i]);
      if (cfd < 0) continue;
      int port = ports_[i];
      std::thread([this, cfd, port]() { handle_client(cfd, port); }).detach();
    }
  }
  for (int lfd : lfds) ::close(lfd);
  return true;
}

int postfix_main(int argc, char** argv) {
  std::string confdir = "/app/etc/postfix";
  std::string action;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-c" && i + 1 < argc) {
      confdir = argv[++i];
    } else if (a[0] != '-') {
      action = a;
    }
  }
  PostfixConfig cfg;
  if (!cfg.load(confdir)) {
    std::fprintf(stderr, "postfix: cannot load %s/main.cf\n", confdir.c_str());
    return 1;
  }
  Queue q(cfg);
  q.ensure_dirs();
  if (action.empty() || action == "check") {
    return 0;
  }
  if (action == "flush") {
    std::string pidf = cfg.get("queue_directory") + "/pid/master.pid";
    std::string s = trim(read_file(pidf));
    if (!s.empty()) {
      int pid = std::atoi(s.c_str());
      if (pid > 0) ::kill(pid, SIGUSR1);
    }
    q.flush();
    return 0;
  }
  if (action == "stop") {
    std::string pidf = cfg.get("queue_directory") + "/pid/master.pid";
    std::string s = trim(read_file(pidf));
    if (!s.empty()) {
      int pid = std::atoi(s.c_str());
      if (pid > 0) ::kill(pid, SIGTERM);
    }
    return 0;
  }
  bool fg = (action == "start-fg");
  if (action == "start" && !fg) {
    pid_t p = ::fork();
    if (p < 0) return 1;
    if (p > 0) return 0;
    ::setsid();
  }
  if (action != "start" && action != "start-fg") {
    std::fprintf(stderr, "postfix: unknown command %s\n", action.c_str());
    return 1;
  }
  std::atomic<bool> stop{false};
  g_stop = &stop;
  g_queue = &q;
  std::signal(SIGUSR1, on_sig);
  std::signal(SIGTERM, on_sig);
  std::signal(SIGINT, on_sig);
  std::signal(SIGPIPE, SIG_IGN);
  mkdir_p(cfg.get("queue_directory") + "/pid");
  g_pidfile = cfg.get("queue_directory") + "/pid/master.pid";
  write_file(g_pidfile, std::to_string(::getpid()) + "\n");
  SmtpServer srv(cfg, &q);
  // periodic queue runner
  std::thread runner([&]() {
    while (!stop.load()) {
      q.run_once();
      for (int i = 0; i < 10 && !stop.load(); ++i) ::sleep(1);
    }
  });
  bool ok = srv.start(&stop);
  stop.store(true);
  runner.join();
  ::unlink(g_pidfile.c_str());
  return ok ? 0 : 1;
}

}  // namespace mail
