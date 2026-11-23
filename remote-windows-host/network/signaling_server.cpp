
#include "network/signaling_server.hpp"

#include "core/protocol_json.hpp"
#include "core/protocol_validation.hpp"
#include "network/framing.hpp"

namespace remotewindows::net {

namespace {
constexpr int kMaxConsecutiveMalformed = 10;
}  // namespace

bool Connection::sendEnvelope(const remotewindows::protocol::Envelope& e,
                              std::string& err) {
  std::string json;
  if (!remotewindows::protocol::encodeEnvelopeSafe(e, json)) {
    err = "encode_failed";
    return false;
  }
  auto frame = encodeFrame(json);
  return socket_.sendAll(frame.data(), frame.size(), err);
}

SignalingServer::SignalingServer(Handler* handler) : handler_(handler) {}

SignalingServer::~SignalingServer() { stop(); }

bool SignalingServer::start(const std::string& addr, uint16_t port,
                            std::string& err) {
  if (running_.exchange(true)) return true;
  if (!listener_.bindAndListen(addr, port, err)) return false;
  accept_thread_ = std::thread([this] { acceptLoop(); });
  return true;
}

uint16_t SignalingServer::port(std::string& err) {
  return listener_.localPort(err);
}

void SignalingServer::stop() {
  if (!running_.exchange(false)) return;
  if (listener_.isOpen()) {
    // Unblock accept(): connect a throwaway socket to the listener.
    TcpSocket poke;
    std::string err;
    uint16_t p = listener_.localPort(err);
    if (p != 0) poke.connectTo("127.0.0.1", p, err);
    poke.close();
  }
  if (accept_thread_.joinable()) accept_thread_.join();
  std::vector<std::shared_ptr<Connection>> snapshot;
  {
    std::lock_guard<std::mutex> lk(conns_mutex_);
    snapshot = conns_;
    conns_.clear();
  }
  for (auto& c : snapshot) c->socket_.close();
}

bool SignalingServer::isRunning() const { return running_; }

size_t SignalingServer::connectionCount() const {
  std::lock_guard<std::mutex> lk(conns_mutex_);
  return conns_.size();
}

uint64_t SignalingServer::malformedFrames() const { return malformed_; }
uint64_t SignalingServer::envelopesReceived() const { return envelopes_; }

void SignalingServer::acceptLoop() {
  while (running_) {
    std::string err;
    auto sock = listener_.accept(err);
    if (!sock) {
      if (!running_) break;
      continue;
    }
    auto conn = std::make_shared<Connection>(std::move(*sock), this);
    {
      std::lock_guard<std::mutex> lk(conns_mutex_);
      conns_.push_back(conn);
    }
    if (handler_) handler_->onConnection(*conn);
    std::thread([this, conn] { connectionLoop(conn); }).detach();
  }
}

void SignalingServer::connectionLoop(std::shared_ptr<Connection> conn) {
  int consecutive_malformed = 0;
  while (running_ && conn->isOpen()) {
    std::string json, err;
    if (!readFrame(conn->socket_, json, err)) {
      break;
    }
    remotewindows::protocol::Envelope env;
    std::string code;
    if (!remotewindows::protocol::decodeEnvelope(json, env, code)) {
      ++malformed_;
      if (++consecutive_malformed >= kMaxConsecutiveMalformed) break;
      continue;
    }
    consecutive_malformed = 0;
    if (!remotewindows::protocol::validate(env).ok) {
      ++malformed_;
      continue;
    }
    ++envelopes_;
    if (handler_ && !handler_->onEnvelope(*conn, env)) break;
  }
  conn->socket_.close();
  if (handler_) handler_->onDisconnection(*conn);
  {
    std::lock_guard<std::mutex> lk(conns_mutex_);
    for (auto it = conns_.begin(); it != conns_.end(); ++it)
      if (*it == conn) {
        conns_.erase(it);
        break;
      }
  }
}

}  // namespace remotewindows::net
