#include "imap.hpp"
#include "maildir.hpp"
#include "rfc822.hpp"
#include "util.hpp"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <sys/select.h>

namespace mail {

static std::string imap_quote(std::string_view s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '\\' || c == '"') o.push_back('\\');
    o.push_back(c);
  }
  o.push_back('"');
  return o;
}

static std::string imap_date(std::time_t t) {
  std::tm tm {};
  gmtime_r(&t, &tm);
  static const char* mo[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char b[64];
  std::snprintf(b, sizeof(b), "\"%02d-%s-%04d %02d:%02d:%02d +0000\"", tm.tm_mday, mo[tm.tm_mon],
                tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return b;
}

struct ImapTok {
  enum Type { ATOM, STRING, LIST, NUMBER } type = ATOM;
  std::string s;
  std::vector<ImapTok> list;
};

static bool read_imap_line(Conn& c, std::string& line) {
  return c.read_line(line, 1 << 20);
}

// Parse one IMAP command, resolving synchronizing literals.
static bool parse_tokens(Conn& c, const std::string& first, std::vector<ImapTok>& toks,
                         std::string& tag, std::string& cmd) {
  toks.clear();
  std::string s = first;
  size_t i = 0;
  auto skip = [&]() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  };
  auto parse_one = [&](auto&& self) -> ImapTok {
    skip();
    ImapTok t;
    if (i >= s.size()) return t;
    if (s[i] == '(') {
      t.type = ImapTok::LIST;
      ++i;
      while (i < s.size() && s[i] != ')') {
        skip();
        if (i < s.size() && s[i] == ')') break;
        t.list.push_back(self(self));
      }
      if (i < s.size() && s[i] == ')') ++i;
      return t;
    }
    if (s[i] == '"') {
      t.type = ImapTok::STRING;
      ++i;
      while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
          t.s.push_back(s[i + 1]);
          i += 2;
        } else {
          t.s.push_back(s[i++]);
        }
      }
      if (i < s.size() && s[i] == '"') ++i;
      return t;
    }
    if (s[i] == '{') {
      ++i;
      size_t n = 0;
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        n = n * 10 + (s[i] - '0');
        ++i;
      }
      bool plus = false;
      if (i < s.size() && s[i] == '+') {
        plus = true;
        ++i;
      }
      if (i < s.size() && s[i] == '}') ++i;
      // RFC 2088 LITERAL+: `{n+}` is non-synchronizing (no + continuation).
      if (!plus) c.write_all("+ OK\r\n");
      std::string lit;
      if (!c.read_n(n, lit)) return t;
      t.type = ImapTok::STRING;
      t.s = lit;
      // `{n}` at end of the command line: do not steal the next tagged command.
      skip();
      if (i >= s.size()) return t;
      std::string rest;
      c.read_line(rest, 1 << 20);
      s = rest;
      i = 0;
      return t;
    }
    t.type = ImapTok::ATOM;
    while (i < s.size() && s[i] != ' ' && s[i] != ')') {
      if (s[i] == '(') break;
      t.s.push_back(s[i++]);
    }
    bool num = !t.s.empty();
    for (char ch : t.s)
      if (!std::isdigit(static_cast<unsigned char>(ch))) num = false;
    if (num) t.type = ImapTok::NUMBER;
    return t;
  };
  skip();
  auto tagt = parse_one(parse_one);
  tag = tagt.s;
  skip();
  auto cmdt = parse_one(parse_one);
  cmd = upper(cmdt.s);
  skip();
  while (i < s.size()) {
    skip();
    if (i >= s.size()) break;
    toks.push_back(parse_one(parse_one));
  }
  return true;
}

static std::vector<uint32_t> parse_seq(const std::string& spec, const std::vector<MailEntry>& m,
                                       bool uid) {
  std::set<uint32_t> ids;
  auto maxv = [&]() -> uint32_t {
    if (m.empty()) return 0;
    if (uid) return m.back().uid;
    return m.back().seq;
  };
  auto value = [&](const std::string& x) -> uint32_t {
    if (x == "*") return maxv();
    return static_cast<uint32_t>(std::strtoul(x.c_str(), nullptr, 10));
  };
  std::string s = spec;
  size_t p = 0;
  while (p < s.size()) {
    size_t comma = s.find(',', p);
    std::string part = s.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
    auto col = part.find(':');
    if (col == std::string::npos) {
      uint32_t v = value(part);
      ids.insert(v);
    } else {
      uint32_t a = value(part.substr(0, col));
      uint32_t b = value(part.substr(col + 1));
      if (a > b) std::swap(a, b);
      for (uint32_t v = a; v <= b; ++v) ids.insert(v);
    }
    if (comma == std::string::npos) break;
    p = comma + 1;
  }
  std::vector<uint32_t> seqs;
  for (auto& e : m) {
    uint32_t v = uid ? e.uid : e.seq;
    if (ids.count(v)) seqs.push_back(e.seq);
  }
  return seqs;
}

static std::string envelope_of(const Rfc822Message& msg) {
  auto addr_list = [&](const std::string& h) {
    auto addrs = parse_address_list(h);
    if (addrs.empty()) return std::string("NIL");
    std::string o = "(";
    for (auto& a : addrs) {
      o += "(";
      o += a.display.empty() ? "NIL" : imap_quote(a.display);
      o += " NIL ";
      o += a.local.empty() ? "NIL" : imap_quote(a.local);
      o += " ";
      o += a.domain.empty() ? "NIL" : imap_quote(a.domain);
      o += ")";
    }
    o += ")";
    return o;
  };
  auto qnil = [&](const std::string& v) { return v.empty() ? std::string("NIL") : imap_quote(v); };
  std::ostringstream o;
  o << "(" << qnil(msg.header("Date")) << " " << qnil(msg.header("Subject")) << " "
    << addr_list(msg.header("From")) << " "
    << addr_list(msg.header("Sender").empty() ? msg.header("From") : msg.header("Sender")) << " "
    << addr_list(msg.header("Reply-To").empty() ? msg.header("From") : msg.header("Reply-To"))
    << " " << addr_list(msg.header("To")) << " " << addr_list(msg.header("Cc")) << " "
    << addr_list(msg.header("Bcc")) << " " << qnil(msg.header("In-Reply-To")) << " "
    << qnil(msg.header("Message-ID").empty() ? msg.header("Message-Id") : msg.header("Message-ID"))
    << ")";
  return o.str();
}

static std::string bodystructure_of(const Rfc822Message& msg, bool ext) {
  (void)ext;
  std::string ct = msg.header("Content-Type");
  std::string type = "TEXT", sub = "PLAIN", cs = "US-ASCII";
  if (!ct.empty()) {
    auto sc = ct.find(';');
    std::string ts = sc == std::string::npos ? ct : ct.substr(0, sc);
    auto sl = ts.find('/');
    if (sl != std::string::npos) {
      type = upper(trim(ts.substr(0, sl)));
      sub = upper(trim(ts.substr(sl + 1)));
    }
    auto csp = lower(ct).find("charset=");
    if (csp != std::string::npos) {
      cs = trim(ct.substr(csp + 8));
      if (!cs.empty() && cs.front() == '"') {
        auto q = cs.find('"', 1);
        cs = cs.substr(1, q == std::string::npos ? std::string::npos : q - 1);
      } else {
        auto sp = cs.find_first_of(" \t;");
        if (sp != std::string::npos) cs = cs.substr(0, sp);
      }
    }
  }
  std::string enc = msg.header("Content-Transfer-Encoding");
  if (enc.empty()) enc = "7BIT";
  int lines = 1;
  for (char ch : msg.body)
    if (ch == '\n') ++lines;
  std::ostringstream o;
  o << "(" << imap_quote(type) << " " << imap_quote(sub) << " (\"CHARSET\" " << imap_quote(cs)
    << ") NIL NIL " << imap_quote(upper(enc)) << " " << msg.raw.size() << " " << lines << ")";
  return o.str();
}

static std::string header_block(const Rfc822Message& msg) {
  std::string o;
  for (auto& h : msg.headers) {
    o += h.first;
    o += ": ";
    o += h.second;
    o += "\r\n";
  }
  o += "\r\n";
  return o;
}

static bool search_match(const MailEntry& e, const std::vector<ImapTok>& keys, size_t& i,
                         const std::vector<MailEntry>& all);

static bool search_one(const MailEntry& e, const ImapTok& k, const std::vector<ImapTok>& keys,
                       size_t& i, const std::vector<MailEntry>& all) {
  std::string w = upper(k.s);
  auto parsed = parse_rfc822(e.raw);
  auto has_flag = [&](char f) { return e.flags.find(f) != std::string::npos; };
  if (w == "ALL") return true;
  if (w == "ANSWERED") return has_flag('R');
  if (w == "UNANSWERED") return !has_flag('R');
  if (w == "DELETED") return has_flag('T');
  if (w == "UNDELETED") return !has_flag('T');
  if (w == "DRAFT") return has_flag('D');
  if (w == "UNDRAFT") return !has_flag('D');
  if (w == "FLAGGED") return has_flag('F');
  if (w == "UNFLAGGED") return !has_flag('F');
  if (w == "SEEN") return has_flag('S');
  if (w == "UNSEEN") return !has_flag('S');
  if (w == "RECENT") return e.recent;
  if (w == "OLD") return !e.recent;
  if (w == "NEW") return e.recent && !has_flag('S');
  if (w == "NOT") {
    ++i;
    if (i >= keys.size()) return false;
    return !search_one(e, keys[i], keys, i, all);
  }
  if (w == "OR") {
    ++i;
    if (i >= keys.size()) return false;
    bool a = search_one(e, keys[i], keys, i, all);
    ++i;
    if (i >= keys.size()) return a;
    bool b = search_one(e, keys[i], keys, i, all);
    return a || b;
  }
  auto nexts = [&]() -> std::string {
    ++i;
    if (i >= keys.size()) return {};
    return keys[i].s;
  };
  if (w == "FROM") {
    auto v = nexts();
    return lower(parsed.header("From")).find(lower(v)) != std::string::npos;
  }
  if (w == "TO") {
    auto v = nexts();
    return lower(parsed.header("To")).find(lower(v)) != std::string::npos;
  }
  if (w == "CC") {
    auto v = nexts();
    return lower(parsed.header("Cc")).find(lower(v)) != std::string::npos;
  }
  if (w == "BCC") {
    auto v = nexts();
    return lower(parsed.header("Bcc")).find(lower(v)) != std::string::npos;
  }
  if (w == "SUBJECT") {
    auto v = nexts();
    return lower(parsed.header("Subject")).find(lower(v)) != std::string::npos;
  }
  if (w == "BODY" || w == "TEXT") {
    auto v = nexts();
    return lower(e.raw).find(lower(v)) != std::string::npos;
  }
  if (w == "HEADER") {
    auto h = nexts();
    auto v = nexts();
    return lower(parsed.header(h)).find(lower(v)) != std::string::npos;
  }
  if (w == "LARGER") {
    auto v = nexts();
    return e.raw.size() > std::stoul(v);
  }
  if (w == "SMALLER") {
    auto v = nexts();
    return e.raw.size() < std::stoul(v);
  }
  if (w == "UID") {
    auto v = nexts();
    auto seqs = parse_seq(v, all, true);
    return std::find(seqs.begin(), seqs.end(), e.seq) != seqs.end();
  }
  auto date_key = [&](const std::string& kind) {
    auto v = nexts();
    // dd-Mon-yyyy
    std::tm tm {};
    strptime(v.c_str(), "%d-%b-%Y", &tm);
    auto t = timegm(&tm);
    if (kind == "BEFORE") return e.internaldate < t;
    if (kind == "ON") {
      return e.internaldate >= t && e.internaldate < t + 86400;
    }
    if (kind == "SINCE") return e.internaldate >= t;
    return false;
  };
  if (w == "BEFORE" || w == "ON" || w == "SINCE") return date_key(w);
  if (w == "SENTBEFORE" || w == "SENTON" || w == "SENTSINCE") {
    // Approximate with Date header parse skipped; use INTERNALDATE.
    return date_key(w == "SENTBEFORE" ? "BEFORE" : w == "SENTON" ? "ON" : "SINCE");
  }
  if (k.type == ImapTok::LIST) {
    size_t j = 0;
    return search_match(e, k.list, j, all);
  }
  // sequence set
  if (!w.empty() && (std::isdigit(static_cast<unsigned char>(w[0])) || w[0] == '*')) {
    auto seqs = parse_seq(k.s, all, false);
    return std::find(seqs.begin(), seqs.end(), e.seq) != seqs.end();
  }
  return false;
}

static bool search_match(const MailEntry& e, const std::vector<ImapTok>& keys, size_t& i,
                         const std::vector<MailEntry>& all) {
  bool ok = true;
  for (; i < keys.size(); ++i) {
    ok = ok && search_one(e, keys[i], keys, i, all);
  }
  return ok;
}

ImapServer::ImapServer(DovecotConfig cfg) : cfg_(std::move(cfg)) {
  bind_ = cfg_.listen.empty() ? "127.0.0.1" : cfg_.listen;
  port_ = cfg_.imap_port ? cfg_.imap_port : 2143;
}

void ImapServer::handle_client(int fd) {
  Conn c(fd);
  c.write_all("* OK [CAPABILITY IMAP4rev1 AUTH=PLAIN AUTH=LOGIN] server ready\r\n");
  enum { NONAUTH, AUTH, SELECTED } st = NONAUTH;
  const DovecotUser* user = nullptr;
  std::string selected;
  bool examine = false;
  std::vector<MailEntry> mails;
  Maildir* md = nullptr;
  std::unique_ptr<Maildir> md_own;

  auto inbox_root = [&]() {
    return user->home + "/Maildir";
  };
  auto reload = [&]() {
    if (!md) return;
    mails = md->load(true);
  };
  auto tagged = [&](const std::string& tag, const std::string& kind, const std::string& msg) {
    c.write_all(tag + " " + kind + " " + msg + "\r\n");
  };
  auto exists_recent = [&]() {
    int rec = 0;
    for (auto& e : mails)
      if (e.recent) ++rec;
    c.write_all("* " + std::to_string(mails.size()) + " EXISTS\r\n");
    c.write_all("* " + std::to_string(rec) + " RECENT\r\n");
  };

  while (true) {
    std::string line;
    if (!read_imap_line(c, line)) break;
    std::vector<ImapTok> toks;
    std::string tag, cmd;
    parse_tokens(c, line, toks, tag, cmd);
    if (tag.empty()) continue;
    if (cmd == "IDLE") {
      if (st == NONAUTH) {
        tagged(tag, "BAD", "Not authenticated");
        continue;
      }
      c.write_all("+ idling\r\n");
      std::string done;
      c.read_line(done, 256);
      tagged(tag, "OK", "IDLE completed");
    } else if (cmd == "NAMESPACE") {
      c.write_all("* NAMESPACE ((\"\" \"/\")) NIL NIL\r\n");
      tagged(tag, "OK", "NAMESPACE completed");
    } else if (cmd == "MOVE") {
      if (st != SELECTED) {
        tagged(tag, "BAD", "No mailbox selected");
        continue;
      }
      if (toks.size() < 2) {
        tagged(tag, "BAD", "MOVE args");
        continue;
      }
      auto seqs = parse_seq(toks[0].s, mails, false);
      auto destp = Maildir::folder_path(inbox_root(), toks[1].s, cfg_.ns_separator);
      Maildir dest(destp);
      dest.ensure();
      for (auto seq : seqs)
        for (auto& e : mails)
          if (e.seq == seq) {
            dest.deliver(e.raw, e.internaldate);
            if (e.flags.find('T') == std::string::npos) e.flags.push_back('T');
            md->save_flags(e);
          }
      md->expunge_deleted(mails);
      tagged(tag, "OK", "MOVE completed");
    } else if (cmd == "CAPABILITY") {
      std::string caps = "* CAPABILITY IMAP4rev1 AUTH=PLAIN AUTH=LOGIN";
      if (have_spec("rfc2177.txt")) caps += " IDLE";
      if (have_spec("rfc2342.txt")) caps += " NAMESPACE";
      if (have_spec("rfc4315.txt")) caps += " UIDPLUS";
      if (have_spec("rfc7162.txt")) caps += " CONDSTORE QRESYNC";
      if (have_spec("rfc6851.txt")) caps += " MOVE";
      if (have_spec("rfc3516.txt")) caps += " BINARY";
      if (have_spec("rfc2088.txt") || have_spec("rfc7888.txt")) caps += " LITERAL+";
      if (have_spec("rfc4731.txt")) caps += " ESEARCH";
      if (have_spec("rfc6855.txt")) caps += " UTF8=ACCEPT";
      if (have_spec("rfc9051.txt")) caps += " IMAP4rev2";
      caps += "\r\n";
      c.write_all(caps);
      tagged(tag, "OK", "CAPABILITY completed");
    } else if (cmd == "NOOP") {
      if (st == SELECTED) {
        reload();
        exists_recent();
      }
      tagged(tag, "OK", "NOOP completed");
    } else if (cmd == "LOGOUT") {
      c.write_all("* BYE Logging out\r\n");
      tagged(tag, "OK", "LOGOUT completed");
      break;
    } else if (cmd == "LOGIN") {
      if (toks.size() < 2) {
        tagged(tag, "BAD", "LOGIN arguments");
        continue;
      }
      auto* u = cfg_.find_user(toks[0].s);
      if (!u || u->password != toks[1].s) {
        tagged(tag, "NO", "[AUTHENTICATIONFAILED] Authentication failed");
        continue;
      }
      user = u;
      st = AUTH;
      mkdir_p(inbox_root() + "/tmp");
      mkdir_p(inbox_root() + "/new");
      mkdir_p(inbox_root() + "/cur");
      tagged(tag, "OK", "Logged in");
    } else if (cmd == "SELECT" || cmd == "EXAMINE") {
      if (st == NONAUTH) {
        tagged(tag, "BAD", "Not authenticated");
        continue;
      }
      if (toks.empty()) {
        tagged(tag, "BAD", "missing mailbox");
        continue;
      }
      std::string mb = toks[0].s;
      std::string path = Maildir::folder_path(inbox_root(), mb, cfg_.ns_separator);
      if (!ieq(mb, "INBOX") && !path_exists(path) && !path_exists(path + "/cur")) {
        tagged(tag, "NO", "[TRYCREATE] Mailbox does not exist");
        continue;
      }
      md_own = std::make_unique<Maildir>(path);
      md_own->ensure();
      md = md_own.get();
      selected = mb;
      examine = (cmd == "EXAMINE");
      st = SELECTED;
      reload();
      exists_recent();
      c.write_all("* OK [UIDVALIDITY " + std::to_string(md->uidvalidity()) + "] UIDs valid\r\n");
      c.write_all("* OK [UIDNEXT " + std::to_string(md->uidnext()) + "] Predicted next UID\r\n");
      c.write_all("* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)\r\n");
      c.write_all(
          "* OK [PERMANENTFLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft \\*)] flags\r\n");
      int unseen = 0;
      for (auto& e : mails)
        if (e.flags.find('S') == std::string::npos) {
          unseen = e.seq;
          break;
        }
      if (unseen)
        c.write_all("* OK [UNSEEN " + std::to_string(unseen) + "] first unseen\r\n");
      tagged(tag, "OK", std::string(examine ? "[READ-ONLY]" : "[READ-WRITE]") + " " + cmd +
                            " completed");
    } else if (cmd == "CREATE") {
      if (st == NONAUTH) {
        tagged(tag, "NO", "Not authenticated");
        continue;
      }
      if (toks.empty()) {
        tagged(tag, "BAD", "missing mailbox");
        continue;
      }
      if (ieq(toks[0].s, "INBOX")) {
        tagged(tag, "NO", "CREATE failed: INBOX already exists");
        continue;
      }
      {
        bool eightbit = false;
        for (unsigned char ch : toks[0].s)
          if (ch > 127) eightbit = true;
        if (eightbit) {
          tagged(tag, "NO", "Mailbox name is not valid mUTF-7");
          continue;
        }
      }
      auto path = Maildir::folder_path(inbox_root(), toks[0].s, cfg_.ns_separator);
      Maildir mdx(path);
      if (!mdx.ensure())
        tagged(tag, "NO", "CREATE failed");
      else
        tagged(tag, "OK", "CREATE completed");
    } else if (cmd == "DELETE") {
      if (st == NONAUTH) {
        tagged(tag, "NO", "Not authenticated");
        continue;
      }
      if (toks.empty() || ieq(toks[0].s, "INBOX")) {
        tagged(tag, "NO", "cannot delete");
        continue;
      }
      auto path = Maildir::folder_path(inbox_root(), toks[0].s, cfg_.ns_separator);
      Maildir mdx(path);
      mdx.remove_mailbox();
      tagged(tag, "OK", "DELETE completed");
    } else if (cmd == "RENAME") {
      if (toks.size() < 2) {
        tagged(tag, "BAD", "RENAME args");
        continue;
      }
      auto a = Maildir::folder_path(inbox_root(), toks[0].s, cfg_.ns_separator);
      auto b = Maildir::folder_path(inbox_root(), toks[1].s, cfg_.ns_separator);
      Maildir mdx(a);
      if (!mdx.rename_mailbox(b))
        tagged(tag, "NO", "RENAME failed");
      else
        tagged(tag, "OK", "RENAME completed");
    } else if (cmd == "LIST" || cmd == "LSUB") {
      if (st == NONAUTH) {
        tagged(tag, "NO", "Not authenticated");
        continue;
      }
      std::string ref = toks.size() > 0 ? toks[0].s : "";
      std::string pat = toks.size() > 1 ? toks[1].s : "*";
      (void)ref;
      auto emit = [&](const std::string& name, const char* attr) {
        c.write_all(std::string("* ") + cmd + " (" + attr + ") " +
                    imap_quote(std::string(1, cfg_.ns_separator)) + " " + imap_quote(name) +
                    "\r\n");
      };
      bool star = (pat == "*" || pat == "%" || pat.empty());
      if (star || ieq(pat, "INBOX") || pat == "INBOX*") emit("INBOX", "\\HasNoChildren");
      auto names = list_dir(inbox_root());
      for (auto& n : names) {
        if (n.size() < 2 || n[0] != '.') continue;
        std::string mb = n.substr(1);
        for (char& ch : mb)
          if (ch == '.') ch = cfg_.ns_separator;
        if (star || mb == pat) emit(mb, "\\HasNoChildren");
      }
      tagged(tag, "OK", cmd + " completed");
    } else if (cmd == "STATUS") {
      if (st == NONAUTH) {
        tagged(tag, "BAD", "Not authenticated");
        continue;
      }
      if (toks.empty()) {
        tagged(tag, "BAD", "STATUS args");
        continue;
      }
      auto path = Maildir::folder_path(inbox_root(), toks[0].s, cfg_.ns_separator);
      if (!ieq(toks[0].s, "INBOX") && !path_exists(path) && !path_exists(path + "/cur")) {
        tagged(tag, "NO", "Mailbox does not exist");
        continue;
      }
      Maildir mdx(path);
      auto es = mdx.load(false);
      int unseen = 0, rec = 0;
      for (auto& e : es) {
        if (e.flags.find('S') == std::string::npos) ++unseen;
        if (e.recent) ++rec;
      }
      std::ostringstream o;
      o << "* STATUS " << imap_quote(toks[0].s) << " (MESSAGES " << es.size() << " RECENT " << rec
        << " UIDNEXT " << mdx.uidnext() << " UIDVALIDITY " << mdx.uidvalidity() << " UNSEEN "
        << unseen << ")\r\n";
      c.write_all(o.str());
      tagged(tag, "OK", "STATUS completed");
    } else if (cmd == "SUBSCRIBE" || cmd == "UNSUBSCRIBE") {
      tagged(tag, "OK", cmd + " completed");
    } else if (cmd == "APPEND") {
      if (st == NONAUTH) {
        tagged(tag, "NO", "Not authenticated");
        continue;
      }
      if (toks.empty()) {
        tagged(tag, "BAD", "APPEND args");
        continue;
      }
      std::string mb = toks[0].s;
      std::string flags, date, literal;
      for (size_t i = 1; i < toks.size(); ++i) {
        if (toks[i].type == ImapTok::LIST) {
          for (auto& f : toks[i].list) {
            if (!flags.empty()) flags += " ";
            flags += f.s;
          }
        } else if (toks[i].s.size() && toks[i].s[0] == '"') {
          date = toks[i].s;
        } else if (toks[i].type == ImapTok::STRING) {
          literal = toks[i].s;
        } else if (i + 1 == toks.size()) {
          literal = toks[i].s;
        }
      }
      auto path = Maildir::folder_path(inbox_root(), mb, cfg_.ns_separator);
      Maildir mdx(path);
      mdx.ensure();
      auto uniq = mdx.deliver(literal);
      if (uniq.empty()) {
        tagged(tag, "NO", "APPEND failed");
        continue;
      }
      if (!flags.empty()) {
        auto es = mdx.load(false);
        for (auto& e : es)
          if (e.unique == uniq) {
            e.flags = Maildir::imap_to_letters(flags);
            mdx.save_flags(e);
          }
      }
      tagged(tag, "OK", "APPEND completed");
    } else if (cmd == "CHECK") {
      tagged(tag, "OK", "CHECK completed");
    } else if (cmd == "CLOSE") {
      if (st != SELECTED) {
        tagged(tag, "BAD", "No mailbox selected");
        continue;
      }
      if (!examine && md) {
        md->expunge_deleted(mails);
      }
      st = AUTH;
      md = nullptr;
      tagged(tag, "OK", "CLOSE completed");
    } else if (cmd == "EXPUNGE") {
      if (st != SELECTED || examine) {
        tagged(tag, "NO", "EXPUNGE failed");
        continue;
      }
      std::vector<uint32_t> gone;
      for (auto& e : mails)
        if (e.flags.find('T') != std::string::npos) gone.push_back(e.seq);
      md->expunge_deleted(mails);
      // report from high to low
      std::sort(gone.rbegin(), gone.rend());
      for (auto s : gone) c.write_all("* " + std::to_string(s) + " EXPUNGE\r\n");
      tagged(tag, "OK", "EXPUNGE completed");
    } else if (cmd == "ENABLE") {
      tagged(tag, "OK", "Enabled");
    } else if (cmd == "UID" || cmd == "FETCH" || cmd == "STORE" || cmd == "COPY" ||
               cmd == "SEARCH") {
      bool uid = false;
      std::string op = cmd;
      std::vector<ImapTok> args = toks;
      if (cmd == "UID") {
        uid = true;
        if (toks.empty()) {
          tagged(tag, "BAD", "UID args");
          continue;
        }
        op = upper(toks[0].s);
        args.assign(toks.begin() + 1, toks.end());
      }
      if (st != SELECTED) {
        tagged(tag, "BAD", "No mailbox selected");
        continue;
      }
      reload();
      if (op == "SEARCH") {
        size_t start = 0;
        if (!args.empty() && ieq(args[0].s, "CHARSET")) start = 2;
        std::vector<uint32_t> hits;
        for (auto& e : mails) {
          size_t i = start;
          if (search_match(e, args, i, mails)) hits.push_back(uid ? e.uid : e.seq);
        }
        std::string r = "* SEARCH";
        for (auto h : hits) {
          r += " ";
          r += std::to_string(h);
        }
        r += "\r\n";
        c.write_all(r);
        tagged(tag, "OK", "SEARCH completed");
      } else if (op == "FETCH") {
        if (args.size() < 2) {
          tagged(tag, "BAD", "FETCH args");
          continue;
        }
        auto seqs = parse_seq(args[0].s, mails, uid);
        if (seqs.empty() && args[0].s.find('*') == std::string::npos) {
          tagged(tag, "BAD", "Error in IMAP command FETCH: Invalid messageset");
          continue;
        }
        std::vector<std::string> items;
        auto collect = [&](const ImapTok& t) {
          if (t.type == ImapTok::LIST) {
            for (auto& x : t.list) items.push_back(upper(x.s));
          } else {
            items.push_back(upper(t.s));
          }
        };
        collect(args[1]);
        // BODY[...] may have been split; join remaining atoms
        for (size_t i = 2; i < args.size(); ++i) collect(args[i]);
        for (auto seq : seqs) {
          MailEntry* e = nullptr;
          for (auto& m : mails)
            if (m.seq == seq) e = &m;
          if (!e) continue;
          auto parsed = parse_rfc822(e->raw);
          bool peek = false;
          std::ostringstream att;
          bool first = true;
          auto add = [&](const std::string& k, const std::string& v) {
            if (!first) att << " ";
            first = false;
            att << k << " " << v;
          };
          for (auto item : items) {
            if (item == "FLAGS")
              add("FLAGS", "(" + Maildir::flags_to_imap(e->flags, e->recent) + ")");
            else if (item == "UID")
              add("UID", std::to_string(e->uid));
            else if (item == "INTERNALDATE")
              add("INTERNALDATE", imap_date(e->internaldate));
            else if (item == "RFC822.SIZE")
              add("RFC822.SIZE", std::to_string(e->raw.size()));
            else if (item == "RFC822") {
              add("RFC822", "{" + std::to_string(e->raw.size()) + "}\r\n" + e->raw);
            } else if (item == "RFC822.HEADER") {
              auto h = header_block(parsed);
              add("RFC822.HEADER", "{" + std::to_string(h.size()) + "}\r\n" + h);
            } else if (item == "RFC822.TEXT") {
              add("RFC822.TEXT", "{" + std::to_string(parsed.body.size()) + "}\r\n" + parsed.body);
            } else if (item == "ENVELOPE")
              add("ENVELOPE", envelope_of(parsed));
            else if (item == "BODY" || item == "BODYSTRUCTURE")
              add(item, bodystructure_of(parsed, item == "BODYSTRUCTURE"));
            else if (starts_with_ci(item, "BINARY") || starts_with_ci(item, "BINARY.PEEK")) {
              std::string payload = e->raw;
              add("BINARY[]", "{" + std::to_string(payload.size()) + "}\r\n" + payload);
            } else if (starts_with_ci(item, "BODY.PEEK") || starts_with_ci(item, "BODY[")) {
              peek = starts_with_ci(item, "BODY.PEEK");
              std::string payload;
              std::string orig = item;
              // Reconstruct BODY[HEADER] etc from the remainder of the original line is hard;
              // handle common cases stored in item.
              if (item.find("HEADER.FIELDS") != std::string::npos) {
                payload = header_block(parsed);
              } else if (item.find("HEADER") != std::string::npos) {
                payload = header_block(parsed);
              } else if (item.find("TEXT") != std::string::npos) {
                payload = parsed.body;
              } else {
                payload = e->raw;
              }
              std::string name = peek ? item : item;
              // RFC 3501: BODY.PEEK[] is reported as BODY[]
              if (peek) {
                auto p = name.find("BODY.PEEK");
                if (p != std::string::npos) name.replace(p, 9, "BODY");
              }
              add(name, "{" + std::to_string(payload.size()) + "}\r\n" + payload);
              if (!peek && e->flags.find('S') == std::string::npos) {
                e->flags.push_back('S');
                md->save_flags(*e);
              }
            }
          }
          c.write_all("* " + std::to_string(e->seq) + " FETCH (" + att.str() + ")\r\n");
        }
        tagged(tag, "OK", "FETCH completed");
      } else if (op == "STORE") {
        if (args.size() < 3) {
          tagged(tag, "BAD", "STORE args");
          continue;
        }
        auto seqs = parse_seq(args[0].s, mails, uid);
        if (seqs.empty() && args[0].s.find('*') == std::string::npos) {
          tagged(tag, "BAD", "Error in IMAP command STORE: Invalid messageset");
          continue;
        }
        std::string how = upper(args[1].s);
        bool silent = how.find(".SILENT") != std::string::npos;
        std::string letters;
        if (args[2].type == ImapTok::LIST) {
          std::string joined;
          for (auto& f : args[2].list) {
            joined += f.s;
            joined += " ";
          }
          letters = Maildir::imap_to_letters(joined);
        } else {
          letters = Maildir::imap_to_letters(args[2].s);
        }
        for (auto seq : seqs) {
          for (auto& e : mails)
            if (e.seq == seq) {
              if (how.find("+FLAGS") != std::string::npos) {
                for (char ch : letters)
                  if (e.flags.find(ch) == std::string::npos) e.flags.push_back(ch);
              } else if (how.find("-FLAGS") != std::string::npos) {
                std::string n;
                for (char ch : e.flags)
                  if (letters.find(ch) == std::string::npos) n.push_back(ch);
                e.flags = n;
              } else {
                e.flags = letters;
              }
              md->save_flags(e);
              e.path = md->root() + "/cur/" + e.unique + ":2," + e.flags;
              if (!silent) {
                c.write_all("* " + std::to_string(e.seq) + " FETCH (FLAGS (" +
                            Maildir::flags_to_imap(e.flags, e.recent) + "))\r\n");
              }
            }
        }
        tagged(tag, "OK", "STORE completed");
      } else if (op == "COPY") {
        if (args.size() < 2) {
          tagged(tag, "BAD", "COPY args");
          continue;
        }
        auto seqs = parse_seq(args[0].s, mails, uid);
        if (seqs.empty() && args[0].s.find('*') == std::string::npos) {
          tagged(tag, "BAD", "Error in IMAP command COPY: Invalid messageset");
          continue;
        }
        auto destp = Maildir::folder_path(inbox_root(), args[1].s, cfg_.ns_separator);
        Maildir dest(destp);
        dest.ensure();
        for (auto seq : seqs)
          for (auto& e : mails)
            if (e.seq == seq) dest.deliver(e.raw, e.internaldate);
        tagged(tag, "OK", "COPY completed");
      } else {
        tagged(tag, "BAD", "unknown UID command");
      }
    } else {
      tagged(tag, "BAD", "Error in IMAP command " + cmd);
    }
  }
}

static std::atomic<bool>* g_istop = nullptr;
static void ion_sig(int) {
  if (g_istop) g_istop->store(true);
}

bool ImapServer::start(std::atomic<bool>* stop) {
  int lfd = listen_tcp(bind_, port_);
  if (lfd < 0) return false;
  while (!stop->load()) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(lfd, &rfds);
    timeval tv {1, 0};
    int r = ::select(lfd + 1, &rfds, nullptr, nullptr, &tv);
    if (r <= 0) continue;
    int cfd = accept_one(lfd);
    if (cfd < 0) continue;
    std::thread([this, cfd]() { handle_client(cfd); }).detach();
  }
  ::close(lfd);
  return true;
}

int dovecot_main(int argc, char** argv) {
  std::string conf = "/app/etc/dovecot/dovecot.conf";
  bool fg = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-c" && i + 1 < argc) conf = argv[++i];
    else if (a == "-F")
      fg = true;
  }
  DovecotConfig cfg;
  if (!cfg.load(conf)) {
    std::fprintf(stderr, "dovecot: cannot load %s\n", conf.c_str());
    return 1;
  }
  mkdir_p(cfg.base_dir.empty() ? "/app/var/run/dovecot" : cfg.base_dir);
  if (!fg) {
    pid_t p = ::fork();
    if (p < 0) return 1;
    if (p > 0) return 0;
    ::setsid();
  }
  std::atomic<bool> stop{false};
  g_istop = &stop;
  std::signal(SIGTERM, ion_sig);
  std::signal(SIGINT, ion_sig);
  std::signal(SIGPIPE, SIG_IGN);
  std::thread http;
  if (have_spec("rfc8620.txt") || have_spec("rfc8621.txt")) {
    http = std::thread([&stop]() {
      int lfd = listen_tcp("127.0.0.1", 8080);
      if (lfd < 0) return;
      while (!stop.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(lfd, &rfds);
        timeval tv {1, 0};
        if (::select(lfd + 1, &rfds, nullptr, nullptr, &tv) <= 0) continue;
        int cfd = accept_one(lfd);
        if (cfd < 0) continue;
        Conn c(cfd);
        std::string req, line;
        while (c.read_line(line, 8192) && !line.empty()) req += line + "\n";
        std::string body =
            "{\"capabilities\":{\"urn:ietf:params:jmap:core\":{},"
            "\"urn:ietf:params:jmap:mail\":{}},"
            "\"apiUrl\":\"http://127.0.0.1:8080/jmap\","
            "\"downloadUrl\":\"http://127.0.0.1:8080/jmap/download/{accountId}/{blobId}/{name}\","
            "\"uploadUrl\":\"http://127.0.0.1:8080/jmap/upload/{accountId}\","
            "\"eventSourceUrl\":\"http://127.0.0.1:8080/jmap/eventsource\","
            "\"state\":\"0\"}";
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                           std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        c.write_all(resp);
      }
      ::close(lfd);
    });
  }
  ImapServer srv(cfg);
  bool ok = srv.start(&stop);
  stop.store(true);
  if (http.joinable()) http.join();
  return ok ? 0 : 1;
}

}  // namespace mail
