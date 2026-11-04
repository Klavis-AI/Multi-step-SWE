#pragma once

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mail {

std::string lower(std::string_view s);
std::string upper(std::string_view s);
std::string trim(std::string_view s);
std::vector<std::string> split_ws(std::string_view s);
bool starts_with_ci(std::string_view s, std::string_view pfx);
bool ieq(std::string_view a, std::string_view b);

std::string read_file(const std::string& path);
bool write_file(const std::string& path, std::string_view data);
bool append_file(const std::string& path, std::string_view data);
bool mkdir_p(const std::string& path);
bool path_exists(const std::string& path);
std::vector<std::string> list_dir(const std::string& path);

// Frozen clock: SOURCE_DATE_EPOCH if set, else time(nullptr).
std::time_t now_epoch();
std::string rfc822_date(std::time_t t = 0);
std::string basename_of(std::string_view path);

class Conn {
public:
  explicit Conn(int fd);
  ~Conn();
  Conn(const Conn&) = delete;
  Conn& operator=(const Conn&) = delete;

  int fd() const { return fd_; }
  bool read_line(std::string& line, size_t max = 8192);
  bool read_n(size_t n, std::string& out);
  bool write_all(std::string_view data);
  bool start_tls_server(const std::string& cert, const std::string& key);
  bool start_tls_client();
  std::string peer_cert_sha256_hex() const;
  bool tls() const { return ssl_ != nullptr; }
  void close();

private:
  ssize_t raw_read(char* tmp, size_t n);
  ssize_t raw_write(const char* p, size_t n);
  int fd_;
  std::string buf_;
  void* ssl_ = nullptr;
  void* ssl_ctx_ = nullptr;
};

int listen_tcp(const std::string& addr, int port, int backlog = 128);
int connect_tcp(const std::string& addr, int port, int timeout_sec = 10);
int accept_one(int listen_fd);

std::string getenv_str(const char* key, const std::string& def = "");
std::string app_root();
bool have_spec(const std::string& filename);

// File lock (fcntl) around a path; RAII.
class FileLock {
public:
  explicit FileLock(const std::string& path);
  ~FileLock();
  FileLock(const FileLock&) = delete;
  bool ok() const { return fd_ >= 0; }

private:
  int fd_;
};

}  // namespace mail
