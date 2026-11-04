#pragma once

#include <map>
#include <string>
#include <vector>

namespace mail {

struct MasterService {
  std::string name;
  std::string type;
  std::string command;
  int port = 0;
  std::map<std::string, std::string> overrides;
};

struct PostfixConfig {
  std::map<std::string, std::string> raw;
  std::vector<MasterService> master;
  std::map<std::string, std::string> vmailbox;   // addr -> rel path
  std::map<std::string, std::string> transport;  // domain/addr -> "smtp:[ip]:port" or similar

  std::string get(const std::string& key, const std::string& def = "") const;
  std::string expand(std::string value) const;
  bool load(const std::string& confdir);
};

struct DovecotUser {
  std::string name;
  std::string password;  // plaintext after {PLAIN}
  std::string home;
  int uid = 1000;
  int gid = 1000;
};

struct DovecotConfig {
  std::string listen = "127.0.0.1";
  std::string mail_location;
  std::string base_dir;
  std::string passwd_file;
  int imap_port = 143;
  char ns_separator = '/';
  std::vector<DovecotUser> users;

  bool load(const std::string& path);
  const DovecotUser* find_user(const std::string& name) const;
};

}  // namespace mail
