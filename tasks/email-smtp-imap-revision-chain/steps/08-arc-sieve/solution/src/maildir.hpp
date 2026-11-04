#pragma once

#include "rfc822.hpp"

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace mail {

struct MailEntry {
  uint32_t uid = 0;
  uint32_t seq = 0;
  std::string unique;   // unique name without :2,flags
  std::string path;
  std::string flags;    // IMAP system flags as letters: S R F T D  (+ keywords later)
  bool recent = false;
  std::time_t internaldate = 0;
  std::string raw;
};

class Maildir {
public:
  explicit Maildir(std::string root);

  bool ensure();
  // Deliver RFC822 bytes into new/. Returns unique name.
  std::string deliver(std::string_view raw, std::time_t when = 0);

  std::vector<MailEntry> load(bool read_bodies = true);
  bool save_flags(const MailEntry& e);
  bool expunge_deleted(std::vector<MailEntry>& entries);
  bool rename_mailbox(const std::string& new_root);
  bool remove_mailbox();

  std::string root() const { return root_; }
  uint32_t uidvalidity() const { return uidvalidity_; }
  uint32_t uidnext() const { return uidnext_; }

  static std::string flags_to_imap(const std::string& letters, bool recent);
  static std::string imap_to_letters(const std::string& imap_flags);

  // Maildir++ child: INBOX is root; "Sent" is root/.Sent when sep is '/'.
  static std::string folder_path(const std::string& inbox_root, const std::string& mailbox,
                                 char sep);

private:
  std::string root_;
  uint32_t uidvalidity_ = 0;
  uint32_t uidnext_ = 1;
  std::string hostname_;

  void load_uidlist(std::vector<MailEntry>& entries);
  void save_uidlist(const std::vector<MailEntry>& entries);
};

}  // namespace mail
