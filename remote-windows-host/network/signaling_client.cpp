
#include "network/signaling_client.hpp"

#include "core/protocol_json.hpp"
#include "core/protocol_validation.hpp"
#include "network/framing.hpp"

namespace remotewindows::net {

SignalingClient::~SignalingClient() { close(); }

bool SignalingClient::connectTo(const std::string& host, uint16_t port,
                                std::string& err) {
  return sock_.connectTo(host, port, err);
}

bool SignalingClient::sendEnvelope(const remotewindows::protocol::Envelope& e,
                                   std::string& err) {
  std::string json;
  if (!remotewindows::protocol::encodeEnvelopeSafe(e, json)) {
    err = "encode_failed";
    return false;
  }
  auto frame = encodeFrame(json);
  return sock_.sendAll(frame.data(), frame.size(), err);
}

std::optional<remotewindows::protocol::Envelope> SignalingClient::readEnvelope(
    int timeout_ms, std::string& err, std::string& err_code) {
  if (timeout_ms > 0) {
    std::string terr;
    if (!sock_.setRecvTimeoutMs(timeout_ms, terr)) {
      err = terr;
      return std::nullopt;
    }
  }
  std::string json;
  if (!readFrame(sock_, json, err)) return std::nullopt;
  remotewindows::protocol::Envelope env;
  if (!remotewindows::protocol::decodeEnvelope(json, env, err_code)) {
    if (err_code.empty()) err_code = "malformed_json";
    return std::nullopt;
  }
  if (!remotewindows::protocol::validate(env).ok) {
    err_code = "validation_failed";
    return std::nullopt;
  }
  return env;
}

void SignalingClient::close() { sock_.close(); }

}  // namespace remotewindows::net
