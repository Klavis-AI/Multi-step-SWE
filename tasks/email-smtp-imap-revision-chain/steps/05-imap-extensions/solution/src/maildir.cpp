#include "maildir.hpp"
#include "util.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace mail {

static uint32_t parse_unique_time(const std::string& unique) {
  return static_cast<uint32_t>(std::strtoul(unique.c_str(), nullptr, 10));
}

Maildir::Maildir(std::string root) : root_(std::move(root)) {
  hostname_ = getenv_str("MAILSTACK_HOSTNAME", "mail.example.test");
}

bool Maildir::ensure() {
  return mkdir_p(root_ + "/tmp") && mkdir_p(root_ + "/new") && mkdir_p(root_ + "/cur");
}

std::string Maildir::folder_path(const std::string& inbox_root, const std::string& mailbox,
                                 char sep) {
  std::string mb = mailbox;
  if (ieq(mb, "INBOX")) return inbox_root;
  if (!mb.empty() && (mb[0] == '/' || mb[0] == sep)) mb.erase(mb.begin());
  if (ieq(mb, "INBOX")) return inbox_root;
  // Map configured separator to Maildir++ dots.
  std::string dotted;
  for (char c : mb) dotted.push_back(c == sep ? '.' : c);
  if (starts_with_ci(dotted, "INBOX.")) dotted = dotted.substr(6);
  return inbox_root + "/." + dotted;
}

std::string Maildir::deliver(std::string_view raw, std::time_t when) {
  ensure();
  if (when == 0) when = now_epoch();
  static uint32_t seq = 0;
  ++seq;
  char uniq[256];
  std::snprintf(uniq, sizeof(uniq), "%ld.P%dQ%u.%s", static_cast<long>(when), int(::getpid()),
                seq, hostname_.c_str());
  std::string tmp = root_ + "/tmp/" + uniq;
  std::string dst = root_ + "/new/" + uniq;
  std::string data = ensure_crlf(raw);
  if (!write_file(tmp, data)) return {};
  if (::rename(tmp.c_str(), dst.c_str()) != 0) {
    ::unlink(tmp.c_str());
    return {};
  }
  return uniq;
}

std::string Maildir::flags_to_imap(const std::string& letters, bool recent) {
  std::string o;
  auto add = [&](const char* f) {
    if (!o.empty()) o += " ";
    o += f;
  };
  if (letters.find('S') != std::string::npos) add("\\Seen");
  if (letters.find('R') != std::string::npos) add("\\Answered");
  if (letters.find('F') != std::string::npos) add("\\Flagged");
  if (letters.find('T') != std::string::npos) add("\\Deleted");
  if (letters.find('D') != std::string::npos) add("\\Draft");
  if (recent) add("\\Recent");
  return o;
}

std::string Maildir::imap_to_letters(const std::string& imap_flags) {
  std::string letters;
  auto has = [&](const char* n) {
    return imap_flags.find(n) != std::string::npos;
  };
  // Case-insensitive scan of tokens.
  std::string u = lower(imap_flags);
  if (u.find("\\seen") != std::string::npos) letters.push_back('S');
  if (u.find("\\answered") != std::string::npos) letters.push_back('R');
  if (u.find("\\flagged") != std::string::npos) letters.push_back('F');
  if (u.find("\\deleted") != std::string::npos) letters.push_back('T');
  if (u.find("\\draft") != std::string::npos) letters.push_back('D');
  (void)has;
  return letters;
}

void Maildir::load_uidlist(std::vector<MailEntry>& entries) {
  std::string path = root_ + "/dovecot-uidlist";
  FileLock lock(path);
  std::string text = read_file(path);
  std::istringstream in(text);
  std::string line;
  std::map<std::string, uint32_t> by_unique;
  if (std::getline(in, line)) {
    auto f = split_ws(line);
    // 3 V<uidvalidity> G...
    for (auto& tok : f) {
      if (tok.size() > 1 && tok[0] == 'V')
        uidvalidity_ = static_cast<uint32_t>(std::strtoul(tok.c_str() + 1, nullptr, 10));
    }
    if (f.size() && f[0] == "3") {
      /* version 3 */
    }
  }
  while (std::getline(in, line)) {
    auto f = split_ws(line);
    if (f.size() < 2) continue;
    uint32_t uid = static_cast<uint32_t>(std::strtoul(f[0].c_str(), nullptr, 10));
    std::string un = f[1];
    if (!un.empty() && un[0] == ':') un.erase(un.begin());
    by_unique[un] = uid;
    if (uid + 1 > uidnext_) uidnext_ = uid + 1;
  }
  if (uidvalidity_ == 0) uidvalidity_ = static_cast<uint32_t>(now_epoch());
  for (auto& e : entries) {
    auto it = by_unique.find(e.unique);
    if (it != by_unique.end()) {
      e.uid = it->second;
    } else {
      e.uid = uidnext_++;
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](auto& a, auto& b) { return a.uid < b.uid; });
  save_uidlist(entries);
}

void Maildir::save_uidlist(const std::vector<MailEntry>& entries) {
  std::string path = root_ + "/dovecot-uidlist";
  std::ostringstream o;
  o << "3 V" << uidvalidity_ << " G" << std::hex << uidvalidity_ << std::dec << "\n";
  for (auto& e : entries) {
    if (e.uid == 0) continue;
    o << e.uid << " " << e.unique << "\n";
    if (e.uid + 1 > uidnext_) uidnext_ = e.uid + 1;
  }
  write_file(path, o.str());
}

static std::string unique_of(const std::string& filename) {
  auto p = filename.find(":2,");
  if (p == std::string::npos) return filename;
  return filename.substr(0, p);
}
static std::string letters_of(const std::string& filename) {
  auto p = filename.find(":2,");
  if (p == std::string::npos) return {};
  return filename.substr(p + 3);
}

std::vector<MailEntry> Maildir::load(bool read_bodies) {
  std::vector<MailEntry> entries;
  ensure();
  auto add_dir = [&](const std::string& sub, bool recent) {
    auto names = list_dir(root_ + "/" + sub);
    for (auto& n : names) {
      if (n[0] == '.') continue;
      MailEntry e;
      e.unique = unique_of(n);
      e.flags = letters_of(n);
      e.recent = recent;
      e.path = root_ + "/" + sub + "/" + n;
      e.internaldate = parse_unique_time(e.unique);
      if (e.internaldate == 0) e.internaldate = now_epoch();
      if (read_bodies) e.raw = read_file(e.path);
      entries.push_back(std::move(e));
    }
  };
  add_dir("new", true);
  add_dir("cur", false);
  load_uidlist(entries);
  uint32_t seq = 1;
  for (auto& e : entries) e.seq = seq++;
  return entries;
}

bool Maildir::save_flags(const MailEntry& e) {
  // Move to cur/ with :2,flags
  std::string letters = e.flags;
  std::sort(letters.begin(), letters.end());
  letters.erase(std::unique(letters.begin(), letters.end()), letters.end());
  std::string dest_name = e.unique + ":2," + letters;
  std::string dest = root_ + "/cur/" + dest_name;
  if (e.path == dest) return true;
  if (::rename(e.path.c_str(), dest.c_str()) != 0) return false;
  return true;
}

bool Maildir::expunge_deleted(std::vector<MailEntry>& entries) {
  std::vector<MailEntry> keep;
  for (auto& e : entries) {
    if (e.flags.find('T') != std::string::npos) {
      ::unlink(e.path.c_str());
    } else {
      keep.push_back(e);
    }
  }
  entries.swap(keep);
  uint32_t seq = 1;
  for (auto& e : entries) e.seq = seq++;
  FileLock lock(root_ + "/dovecot-uidlist");
  save_uidlist(entries);
  return true;
}

bool Maildir::rename_mailbox(const std::string& new_root) {
  return ::rename(root_.c_str(), new_root.c_str()) == 0;
}
bool Maildir::remove_mailbox() {
  // Best-effort recursive delete of tmp/new/cur.
  for (auto* sub : {"tmp", "new", "cur"}) {
    auto names = list_dir(root_ + "/" + sub);
    for (auto& n : names) ::unlink((root_ + "/" + sub + "/" + n).c_str());
    ::rmdir((root_ + "/" + sub).c_str());
  }
  ::unlink((root_ + "/dovecot-uidlist").c_str());
  return ::rmdir(root_.c_str()) == 0 || !path_exists(root_);
}

}  // namespace mail
