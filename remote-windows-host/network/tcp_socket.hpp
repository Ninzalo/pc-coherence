
#ifndef RWH_NETWORK_TCP_SOCKET_HPP
#define RWH_NETWORK_TCP_SOCKET_HPP

// Thin portable TCP wrapper (POSIX / Winsock). The LAN-only transport
// for signaling in v0.x (ADR-0001).

#include <cstdint>
#include <optional>
#include <string>

namespace remotewindows::net {

class TcpSocket {
 public:
  TcpSocket() = default;
  ~TcpSocket();
  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;
  TcpSocket(TcpSocket&& other) noexcept;
  TcpSocket& operator=(TcpSocket&& other) noexcept;

  // Server side. addr: "" or "0.0.0.0" = all interfaces; use a specific
  // LAN address in production (PROTOCOL §7).
  bool bindAndListen(const std::string& addr, uint16_t port, std::string& err);
  // Actual port after listening (useful with port 0 in tests).
  uint16_t localPort(std::string& err);
  // Blocking accept. Returns nullopt on error/close.
  std::optional<TcpSocket> accept(std::string& err);
  // Client side.
  bool connectTo(const std::string& host, uint16_t port, std::string& err);

  bool sendAll(const uint8_t* data, size_t len, std::string& err);
  ssize_t recvSome(uint8_t* data, size_t len, std::string& err);
  bool setRecvTimeoutMs(int ms, std::string& err);
  bool close();
  bool isOpen() const;
  std::string peerName() const;
  int nativeFd() const;

 private:
  int fd_ = -1;
};

}  // namespace remotewindows::net

#endif
