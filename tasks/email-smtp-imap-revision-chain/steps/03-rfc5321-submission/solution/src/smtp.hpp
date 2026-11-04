#pragma once

#include "config.hpp"
#include "queue.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace mail {

class SmtpServer {
public:
  SmtpServer(PostfixConfig cfg, Queue* q);
  bool start(std::atomic<bool>* stop);
  void handle_client(int fd, int port);

private:
  PostfixConfig cfg_;
  Queue* queue_;
  std::vector<int> ports_;
  std::string hostname_;
  std::string bind_;
};

int postfix_main(int argc, char** argv);

}  // namespace mail
