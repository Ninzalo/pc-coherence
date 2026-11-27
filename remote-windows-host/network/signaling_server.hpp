
#ifndef RWH_NETWORK_SIGNALING_SERVER_HPP
#define RWH_NETWORK_SIGNALING_SERVER_HPP

// Host-side LAN signaling server (FR-10). Per-connection worker threads;
// malformed frames are counted and dropped (no crash, PROTOCOL §3/§8).

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/protocol_types.hpp"
#include "network/tcp_socket.hpp"

namespace remotewindows::net {

class Connection;

class SignalingServer {
 public:
  struct Handler {
    virtual ~Handler() = default;
    // Return false to close the connection.
    virtual bool onEnvelope(Connection& c,
                            const remotewindows::protocol::Envelope& e) = 0;
    virtual void onConnection(Connection& c) {}
    virtual void onDisconnection(Connection& c) {}
  };

  explicit SignalingServer(Handler* handler);
  ~SignalingServer();

  // The handler must outlive the server (or outlive the last stop()):
  // worker threads call back into it until stop() returns.
  bool start(const std::string& addr, uint16_t port, std::string& err);
  uint16_t port(std::string& err);
  // Stops the listener, closes all connections and JOINS every
  // per-connection worker thread. When stop() returns, no worker can
  // still touch this server or the handler (the previous detached-thread
  // design let workers outlive stop()/destruction and crash on the freed
  // handler; RW-H-013).
  void stop();
  bool isRunning() const;
  size_t connectionCount() const;
  uint64_t malformedFrames() const;
  uint64_t envelopesReceived() const;

 private:
  void acceptLoop();
  void connectionLoop(std::shared_ptr<Connection> conn);

  Handler* handler_;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
  TcpSocket listener_;
  mutable std::mutex conns_mutex_;
  std::vector<std::shared_ptr<Connection>> conns_;
  // Per-connection worker threads, tracked (never detached). A worker
  // may exit before stop() without removing its handle: stop() joins
  // every entry (joining an already-finished thread returns immediately)
  // and leaves the list empty, so it cannot grow unbounded between
  // start()/stop() cycles.
  std::vector<std::thread> workers_;
  std::atomic<uint64_t> malformed_{0};
  std::atomic<uint64_t> envelopes_{0};
};

class Connection {
 public:
  Connection(TcpSocket socket, SignalingServer* server)
      : socket_(std::move(socket)), server_(server) {}
  bool sendEnvelope(const remotewindows::protocol::Envelope& e,
                    std::string& err);
  std::string peerName() const { return socket_.peerName(); }
  SignalingServer* server() const { return server_; }
  bool isOpen() const { return socket_.isOpen(); }

 private:
  friend class SignalingServer;
  TcpSocket socket_;
  SignalingServer* server_;
};

}  // namespace remotewindows::net

#endif
