#include "config.hpp"
#include "util.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace mail {

static std::map<std::string, std::string> load_texthash(const std::string& path) {
  std::map<std::string, std::string> m;
  std::string text = read_file(path);
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    auto sp = t.find_first_of(" \t");
    if (sp == std::string::npos) {
      m[t] = "";
    } else {
      m[trim(t.substr(0, sp))] = trim(t.substr(sp + 1));
    }
  }
  return m;
}

std::string PostfixConfig::get(const std::string& key, const std::string& def) const {
  auto it = raw.find(key);
  if (it == raw.end()) return def;
  return expand(it->second);
}

std::string PostfixConfig::expand(std::string value) const {
  for (int guard = 0; guard < 16; ++guard) {
    auto d = value.find('$');
    if (d == std::string::npos) break;
    size_t start = d + 1;
    std::string name;
    size_t end;
    if (start < value.size() && value[start] == '{') {
      auto cl = value.find('}', start);
      if (cl == std::string::npos) break;
      name = value.substr(start + 1, cl - start - 1);
      end = cl + 1;
    } else {
      end = start;
      while (end < value.size() &&
             (std::isalnum(static_cast<unsigned char>(value[end])) || value[end] == '_'))
        ++end;
      name = value.substr(start, end - start);
    }
    auto it = raw.find(name);
    std::string repl = it == raw.end() ? "" : it->second;
    value.replace(d, end - d, repl);
  }
  return value;
}

static bool parse_main_cf(const std::string& path, std::map<std::string, std::string>& raw) {
  std::string text = read_file(path);
  if (text.empty() && !path_exists(path)) return false;
  std::istringstream in(text);
  std::string line, key, acc;
  auto flush = [&]() {
    if (!key.empty()) raw[key] = trim(acc);
    key.clear();
    acc.clear();
  };
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
      if (!key.empty()) {
        acc.push_back(' ');
        acc += trim(line);
      }
      continue;
    }
    flush();
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    auto eq = t.find('=');
    if (eq == std::string::npos) continue;
    key = trim(t.substr(0, eq));
    acc = t.substr(eq + 1);
  }
  flush();
  return true;
}

static int service_port(const std::string& name) {
  bool all_digit = !name.empty();
  for (char c : name)
    if (c < '0' || c > '9') all_digit = false;
  if (all_digit) return std::stoi(name);
  if (name == "smtp") return 25;
  if (name == "submission") return 587;
  if (name == "smtps") return 465;
  if (name == "imap") return 143;
  if (name == "imaps") return 993;
  return 0;
}

static bool parse_master_cf(const std::string& path, std::vector<MasterService>& master) {
  std::string text = read_file(path);
  std::istringstream in(text);
  std::string line;
  MasterService* cur = nullptr;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (line[0] == ' ' || line[0] == '\t') {
      std::string t = trim(line);
      if (starts_with_ci(t, "-o")) {
        auto rest = trim(t.substr(2));
        auto eq = rest.find('=');
        if (eq != std::string::npos && cur)
          cur->overrides[trim(rest.substr(0, eq))] = trim(rest.substr(eq + 1));
      }
      continue;
    }
    auto f = split_ws(line);
    if (f.size() < 8) continue;
    MasterService s;
    s.name = f[0];
    s.type = f[1];
    s.command = f[7];
    s.port = (s.type == "inet") ? service_port(s.name) : 0;
    master.push_back(std::move(s));
    cur = &master.back();
  }
  return true;
}

bool PostfixConfig::load(const std::string& confdir) {
  raw.clear();
  master.clear();
  if (!parse_main_cf(confdir + "/main.cf", raw)) return false;
  parse_master_cf(confdir + "/master.cf", master);
  auto vm = get("virtual_mailbox_maps");
  auto pos = vm.find(':');
  std::string vpath = pos == std::string::npos ? vm : vm.substr(pos + 1);
  if (vpath.rfind("/app/", 0) == 0) vpath = app_root() + vpath.substr(4);
  if (!vpath.empty()) vmailbox = load_texthash(vpath);
  auto tm = get("transport_maps");
  pos = tm.find(':');
  std::string tpath = pos == std::string::npos ? tm : tm.substr(pos + 1);
  if (tpath.rfind("/app/", 0) == 0) tpath = app_root() + tpath.substr(4);
  if (!tpath.empty()) transport = load_texthash(tpath);
  return true;
}

static void skip_ws(const std::string& s, size_t& i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

// Minimal Dovecot conf: key = value and one-level { } blocks we care about.
bool DovecotConfig::load(const std::string& path) {
  std::string text = read_file(path);
  if (text.empty() && !path_exists(path)) return false;
  // Flatten comments.
  std::string flat;
  {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
      auto hash = line.find('#');
      if (hash != std::string::npos) line = line.substr(0, hash);
      flat += line;
      flat += '\n';
    }
  }
  auto grab_eq = [&](const std::string& key) -> std::string {
    auto k = flat.find(key);
    while (k != std::string::npos) {
      size_t i = k + key.size();
      skip_ws(flat, i);
      if (i < flat.size() && flat[i] == '=') {
        ++i;
        skip_ws(flat, i);
        size_t j = i;
        while (j < flat.size() && flat[j] != '\n' && flat[j] != '{') ++j;
        return trim(flat.substr(i, j - i));
      }
      k = flat.find(key, k + 1);
    }
    return {};
  };
  listen = grab_eq("listen");
  if (listen.empty()) listen = "127.0.0.1";
  mail_location = grab_eq("mail_location");
  base_dir = grab_eq("base_dir");
  auto sep = grab_eq("separator");
  if (!sep.empty()) ns_separator = sep[0];

  // inet_listener imap { port = N }
  auto imap_pos = flat.find("inet_listener imap");
  if (imap_pos != std::string::npos) {
    auto p = flat.find("port", imap_pos);
    auto brace_end = flat.find('}', imap_pos);
    if (p != std::string::npos && (brace_end == std::string::npos || p < brace_end)) {
      size_t i = p + 4;
      skip_ws(flat, i);
      if (i < flat.size() && flat[i] == '=') {
        ++i;
        skip_ws(flat, i);
        imap_port = std::atoi(flat.c_str() + i);
      }
    }
  }

  auto args = grab_eq("args");
  // last args= that mentions users file
  size_t search = 0;
  while (true) {
    auto a = flat.find("args", search);
    if (a == std::string::npos) break;
    size_t i = a + 4;
    skip_ws(flat, i);
    if (i < flat.size() && flat[i] == '=') {
      ++i;
      skip_ws(flat, i);
      size_t j = i;
      while (j < flat.size() && flat[j] != '\n') ++j;
      std::string v = trim(flat.substr(i, j - i));
      auto tok = split_ws(v);
      for (auto& t : tok) {
        if (t.find('/') != std::string::npos) passwd_file = t;
      }
    }
    search = a + 4;
  }
  if (passwd_file.empty()) passwd_file = app_root() + "/etc/dovecot/users";
  if (passwd_file.rfind("/app/", 0) == 0) passwd_file = app_root() + passwd_file.substr(4);

  auto utext = read_file(passwd_file);
  std::istringstream uin(utext);
  std::string uline;
  while (std::getline(uin, uline)) {
    if (!uline.empty() && uline.back() == '\r') uline.pop_back();
    uline = trim(uline);
    if (uline.empty() || uline[0] == '#') continue;
    // user:{PLAIN}pass:uid:gid::home::
    std::vector<std::string> f;
    std::string cur;
    for (char c : uline) {
      if (c == ':') {
        f.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    f.push_back(cur);
    if (f.size() < 6) continue;
    DovecotUser u;
    u.name = f[0];
    u.password = f[1];
    if (starts_with_ci(u.password, "{PLAIN}")) u.password = u.password.substr(7);
    u.uid = std::atoi(f[2].c_str());
    u.gid = std::atoi(f[3].c_str());
    u.home = f[5];
    users.push_back(std::move(u));
  }
  return true;
}

const DovecotUser* DovecotConfig::find_user(const std::string& name) const {
  for (auto& u : users)
    if (u.name == name) return &u;
  return nullptr;
}

}  // namespace mail
