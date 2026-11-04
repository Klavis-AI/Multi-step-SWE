#pragma once

#include "config.hpp"
#include "rfc822.hpp"

#include <string>
#include <vector>

namespace mail {

struct Envelope {
  Address mail_from;
  std::vector<Address> rcpt;
  std::string helo;
  std::string id;
};

class Queue {
public:
  explicit Queue(PostfixConfig cfg);

  bool ensure_dirs();
  std::string enqueue(const Envelope& env, std::string_view message);
  // Attempt delivery of every message in incoming/deferred. Returns number delivered.
  int run_once();
  void flush() { run_once(); }

  std::string queue_dir() const { return qdir_; }

private:
  PostfixConfig cfg_;
  std::string qdir_;
  std::string vbase_;

  bool deliver_one(const std::string& qid);
  bool deliver_local(const Address& rcpt, const Envelope& env, std::string_view msg);
  bool deliver_remote(const Address& rcpt, const Envelope& env, std::string_view msg);
  void bounce(const Envelope& env, const Address& rcpt, std::string_view msg,
              const std::string& reason);
  bool is_virtual(const Address& rcpt, std::string& rel) const;
};

int smtp_client_send(const std::string& ip, int port, const std::string& helo,
                     const Envelope& env, const Address& rcpt, std::string_view msg,
                     std::string& err);

}  // namespace mail
