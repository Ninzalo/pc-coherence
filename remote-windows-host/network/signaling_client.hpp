
#ifndef RWH_NETWORK_SIGNALING_CLIENT_HPP
#define RWH_NETWORK_SIGNALING_CLIENT_HPP

// Client-side signaling (macOS Client's Network/ layer will wrap this).

#include <optional>
#include <string>

#include "core/protocol_types.hpp"
#include "network/tcp_socket.hpp"

namespace remotewindows::net {

class SignalingClient {
 public:
  SignalingClient() = default;
  ~SignalingClient();
  SignalingClient(const SignalingClient&) = delete;
  SignalingClient& operator=(const SignalingClient&) = delete;

  bool connectTo(const std::string& host, uint16_t port, std::string& err);
  bool sendEnvelope(const remotewindows::protocol::Envelope& e,
                    std::string& err);
  // Blocking read of one decoded envelope; optional timeout ms (0 = wait).
  std::optional<remotewindows::protocol::Envelope> readEnvelope(int timeout_ms,
                                                                std::string& err,
                                                                std::string& err_code);
  bool isOpen() const { return sock_.isOpen(); }
  void close();

 private:
  TcpSocket sock_;
};

}  // namespace remotewindows::net

#endif
