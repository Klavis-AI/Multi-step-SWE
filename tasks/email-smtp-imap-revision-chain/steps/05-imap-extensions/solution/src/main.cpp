#include "imap.hpp"
#include "smtp.hpp"
#include "util.hpp"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
  std::string name = mail::basename_of(argv[0] ? argv[0] : "");
  if (name == "postfix") return mail::postfix_main(argc, argv);
  if (name == "dovecot") return mail::dovecot_main(argc, argv);
  if (argc >= 2) {
    std::string a = argv[1];
    if (a == "postfix") return mail::postfix_main(argc - 1, argv + 1);
    if (a == "dovecot") return mail::dovecot_main(argc - 1, argv + 1);
  }
  std::fprintf(stderr, "mailstack: invoke as postfix or dovecot\n");
  return 1;
}
