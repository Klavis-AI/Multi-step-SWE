#pragma once

#include "config.hpp"

#include <atomic>
#include <string>

namespace mail {

class ImapServer {
public:
  explicit ImapServer(DovecotConfig cfg);
  bool start(std::atomic<bool>* stop);
  void handle_client(int fd);

private:
  DovecotConfig cfg_;
  std::string bind_;
  int port_ = 2143;
};

int dovecot_main(int argc, char** argv);

}  // namespace mail
