#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <openssl/err.h>
#include <openssl/ssl.h>

namespace mail {

std::string lower(std::string_view s) {
  std::string o(s);
  for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}
std::string upper(std::string_view s) {
  std::string o(s);
  for (char& c : o) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return o;
}
std::string trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return std::string(s.substr(a, b - a));
}
std::vector<std::string> split_ws(std::string_view s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}
bool starts_with_ci(std::string_view s, std::string_view pfx) {
  if (s.size() < pfx.size()) return false;
  for (size_t i = 0; i < pfx.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(s[i])) !=
        std::tolower(static_cast<unsigned char>(pfx[i])))
      return false;
  }
  return true;
}
bool ieq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
bool write_file(const std::string& path, std::string_view data) {
  std::string tmp = path + ".tmp";
  std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  out.close();
  if (!out) return false;
  return ::rename(tmp.c_str(), path.c_str()) == 0;
}
bool append_file(const std::string& path, std::string_view data) {
  std::ofstream out(path, std::ios::binary | std::ios::app);
  if (!out) return false;
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  return bool(out);
}
bool mkdir_p(const std::string& path) {
  if (path.empty()) return false;
  std::string cur;
  for (size_t i = 0; i < path.size(); ++i) {
    cur.push_back(path[i]);
    if (path[i] == '/' || i + 1 == path.size()) {
      if (cur == "/") continue;
      if (::mkdir(cur.c_str(), 0755) < 0 && errno != EEXIST) {
        struct stat st {};
        if (::stat(cur.c_str(), &st) < 0 || !S_ISDIR(st.st_mode)) return false;
      }
    }
  }
  return true;
}
bool path_exists(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0;
}
std::vector<std::string> list_dir(const std::string& path) {
  std::vector<std::string> names;
  DIR* d = ::opendir(path.c_str());
  if (!d) return names;
  while (auto* e = ::readdir(d)) {
    if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
    names.emplace_back(e->d_name);
  }
  ::closedir(d);
  return names;
}

std::time_t now_epoch() {
  const char* e = ::getenv("SOURCE_DATE_EPOCH");
  if (e && *e) {
    char* end = nullptr;
    long long v = std::strtoll(e, &end, 10);
    if (end && end != e) return static_cast<std::time_t>(v);
  }
  return std::time(nullptr);
}

std::string rfc822_date(std::time_t t) {
  if (t == 0) t = now_epoch();
  std::tm tm {};
  gmtime_r(&t, &tm);
  static const char* wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* mo[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d +0000",
                wd[tm.tm_wday], tm.tm_mday, mo[tm.tm_mon], tm.tm_year + 1900,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

std::string basename_of(std::string_view path) {
  auto p = path.find_last_of('/');
  if (p == std::string_view::npos) return std::string(path);
  return std::string(path.substr(p + 1));
}

Conn::Conn(int fd) : fd_(fd) {}
Conn::~Conn() { close(); }
void Conn::close() {
  if (ssl_) {
    SSL_shutdown(static_cast<SSL*>(ssl_));
    SSL_free(static_cast<SSL*>(ssl_));
    ssl_ = nullptr;
  }
  if (ssl_ctx_) {
    SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
    ssl_ctx_ = nullptr;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}
ssize_t Conn::raw_read(char* tmp, size_t n) {
  if (ssl_) return SSL_read(static_cast<SSL*>(ssl_), tmp, static_cast<int>(n));
  return ::recv(fd_, tmp, n, 0);
}
ssize_t Conn::raw_write(const char* p, size_t n) {
  if (ssl_) return SSL_write(static_cast<SSL*>(ssl_), p, static_cast<int>(n));
  return ::send(fd_, p, n, MSG_NOSIGNAL);
}
bool Conn::start_tls_server(const std::string& cert, const std::string& key) {
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();
  SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
  if (!ctx) return false;
  if (SSL_CTX_use_certificate_file(ctx, cert.c_str(), SSL_FILETYPE_PEM) != 1 ||
      SSL_CTX_use_PrivateKey_file(ctx, key.c_str(), SSL_FILETYPE_PEM) != 1) {
    SSL_CTX_free(ctx);
    return false;
  }
  SSL* ssl = SSL_new(ctx);
  SSL_set_fd(ssl, fd_);
  if (SSL_accept(ssl) != 1) {
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return false;
  }
  ssl_ = ssl;
  ssl_ctx_ = ctx;
  return true;
}
bool Conn::read_line(std::string& line, size_t max) {
  line.clear();
  while (true) {
    auto pos = buf_.find('\n');
    if (pos != std::string::npos) {
      line = buf_.substr(0, pos);
      buf_.erase(0, pos + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      return true;
    }
    if (buf_.size() > max) return false;
    char tmp[4096];
    ssize_t n = raw_read(tmp, sizeof(tmp));
    if (n <= 0) return false;
    buf_.append(tmp, static_cast<size_t>(n));
  }
}
bool Conn::read_n(size_t n, std::string& out) {
  out.clear();
  out.reserve(n);
  while (out.size() < n) {
    if (!buf_.empty()) {
      size_t take = std::min(n - out.size(), buf_.size());
      out.append(buf_.data(), take);
      buf_.erase(0, take);
      continue;
    }
    char tmp[4096];
    size_t want = std::min(sizeof(tmp), n - out.size());
    ssize_t r = raw_read(tmp, want);
    if (r <= 0) return false;
    out.append(tmp, static_cast<size_t>(r));
  }
  return true;
}
bool Conn::write_all(std::string_view data) {
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = raw_write(data.data() + off, data.size() - off);
    if (n <= 0) return false;
    off += static_cast<size_t>(n);
  }
  return true;
}

int listen_tcp(const std::string& addr, int port, int backlog) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int yes = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in sa {};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(static_cast<uint16_t>(port));
  if (::inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) != 1) {
    ::close(fd);
    return -1;
  }
  if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
    ::close(fd);
    return -1;
  }
  if (::listen(fd, backlog) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

int connect_tcp(const std::string& addr, int port, int timeout_sec) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  timeval tv {};
  tv.tv_sec = timeout_sec;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  sockaddr_in sa {};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(static_cast<uint16_t>(port));
  if (::inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) != 1) {
    ::close(fd);
    return -1;
  }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

int accept_one(int listen_fd) {
  sockaddr_in sa {};
  socklen_t sl = sizeof(sa);
  return ::accept(listen_fd, reinterpret_cast<sockaddr*>(&sa), &sl);
}

std::string getenv_str(const char* key, const std::string& def) {
  const char* v = ::getenv(key);
  if (!v || !*v) return def;
  return v;
}

std::string app_root() { return getenv_str("MAILSTACK_APP", "/app"); }

bool have_spec(const std::string& filename) {
  return path_exists(app_root() + "/SPEC/" + filename);
}

FileLock::FileLock(const std::string& path) : fd_(-1) {
  fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd_ < 0) return;
  struct flock fl {};
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  if (::fcntl(fd_, F_SETLKW, &fl) < 0) {
    ::close(fd_);
    fd_ = -1;
  }
}
FileLock::~FileLock() {
  if (fd_ >= 0) {
    struct flock fl {};
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    ::fcntl(fd_, F_SETLK, &fl);
    ::close(fd_);
  }
}

}  // namespace mail
