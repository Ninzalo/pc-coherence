
#include "network/tcp_socket.hpp"

#include <cctype>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define RWH_CLOSESOCKET closesocket
#pragma comment(lib, "ws2_32")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#define RWH_CLOSESOCKET ::close
#endif

namespace remotewindows::net {

namespace {
const char* errnoStr() {
#ifdef _WIN32
  char buf[128] = {0};
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, WSAGetLastError(), 0, buf, sizeof(buf), nullptr);
  return buf;
#else
  return strerror(errno);
#endif
}

void initOnce() {
#ifdef _WIN32
  static bool done = false;
  if (!done) {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    done = true;
  }
#else
  (void)0;
#endif
}
}  // namespace

TcpSocket::~TcpSocket() { close(); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

bool TcpSocket::bindAndListen(const std::string& addr, uint16_t port,
                              std::string& err) {
  initOnce();
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    err = std::string("socket: ") + errnoStr();
    return false;
  }
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&one), sizeof(one));
  sockaddr_in addr_in{};
  addr_in.sin_family = AF_INET;
  addr_in.sin_port = htons(port);
  std::string bind_addr = addr.empty() ? "0.0.0.0" : addr;
  if (::inet_pton(AF_INET, bind_addr.c_str(), &addr_in.sin_addr) != 1) {
    err = "invalid bind address: " + bind_addr;
    RWH_CLOSESOCKET(fd);
    return false;
  }
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr_in), sizeof(addr_in)) != 0) {
    err = std::string("bind: ") + errnoStr();
    RWH_CLOSESOCKET(fd);
    return false;
  }
  if (::listen(fd, 8) != 0) {
    err = std::string("listen: ") + errnoStr();
    RWH_CLOSESOCKET(fd);
    return false;
  }
  fd_ = fd;
  return true;
}

uint16_t TcpSocket::localPort(std::string& err) {
  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    err = std::string("getsockname: ") + errnoStr();
    return 0;
  }
  return ntohs(addr.sin_port);
}

std::optional<TcpSocket> TcpSocket::accept(std::string& err) {
  int fd = ::accept(fd_, nullptr, nullptr);
  if (fd < 0) {
    err = std::string("accept: ") + errnoStr();
    return std::nullopt;
  }
  int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&one), sizeof(one));
  TcpSocket s;
  s.fd_ = fd;
  return s;
}

bool TcpSocket::connectTo(const std::string& host, uint16_t port,
                          std::string& err) {
  initOnce();
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  std::string portStr = std::to_string(port);
  int rc = ::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
  if (rc != 0 || !res) {
    err = std::string("getaddrinfo: ") + (rc != 0 ? gai_strerror(rc) : "null");
    if (res) ::freeaddrinfo(res);
    return false;
  }
  int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    err = std::string("socket: ") + errnoStr();
    ::freeaddrinfo(res);
    return false;
  }
  if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    err = std::string("connect: ") + errnoStr();
    RWH_CLOSESOCKET(fd);
    ::freeaddrinfo(res);
    return false;
  }
  int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&one), sizeof(one));
  fd_ = fd;
  ::freeaddrinfo(res);
  return true;
}

bool TcpSocket::sendAll(const uint8_t* data, size_t len, std::string& err) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = ::send(fd_, reinterpret_cast<const char*>(data + sent),
                       len - sent, 0);
    if (n <= 0) {
      err = std::string("send: ") + errnoStr();
      return false;
    }
    sent += (size_t)n;
  }
  return true;
}

ssize_t TcpSocket::recvSome(uint8_t* data, size_t len, std::string& err) {
  ssize_t n = ::recv(fd_, reinterpret_cast<char*>(data), len, 0);
  if (n < 0) err = std::string("recv: ") + errnoStr();
  return n;
}

bool TcpSocket::setRecvTimeoutMs(int ms, std::string& err) {
#ifdef _WIN32
  DWORD timeout = (DWORD)ms;
  if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0) {
    err = std::string("setsockopt: ") + errnoStr();
    return false;
  }
#else
  timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv)) != 0) {
    err = std::string("setsockopt: ") + errnoStr();
    return false;
  }
#endif
  return true;
}

bool TcpSocket::close() {
  if (fd_ < 0) return true;
  int fd = fd_;
  fd_ = -1;
#ifdef _WIN32
  ::closesocket(fd);
#else
  ::shutdown(fd, SHUT_RDWR);
  ::close(fd);
#endif
  return true;
}

bool TcpSocket::isOpen() const { return fd_ >= 0; }

std::string TcpSocket::peerName() const {
  if (fd_ < 0) return "";
  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  if (::getpeername(fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
    return "";
  char buf[INET_ADDRSTRLEN] = {0};
  if (inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf)) == nullptr)
    return "";
  return std::string(buf) + ":" + std::to_string(ntohs(addr.sin_port));
}

int TcpSocket::nativeFd() const { return fd_; }

}  // namespace remotewindows::net
