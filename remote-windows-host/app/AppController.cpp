
#include "app/AppController.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "app/Logger.hpp"
#include "app/TrayController.hpp"
#include "core/pairing.hpp"
#include "core/protocol_types.hpp"
#include "network/signaling_client.hpp"
#include "network/signaling_server.hpp"

#ifdef _WIN32
#include "app/Win32Env.hpp"
#endif

using namespace remotewindows;

namespace {

// Host-side answers for the signaling channel (foundation scope).
// Returns true to keep the connection open.
class AppSignalingHandler : public net::SignalingServer::Handler {
 public:
  bool onEnvelope(net::Connection& c,
                  const protocol::Envelope& e) override {
    if (e.kind != "hello") return true;  // all other kinds: H-NET
    std::string err;

    protocol::Envelope reply;
    reply.kind = "hello";
    protocol::Hello rh;
    rh.product_name = "windows-host";
    rh.app_version = "0.1.0";
    rh.capabilities = {protocol::Capability::APP_CATALOG,
                       protocol::Capability::INPUT};
    reply.payload = rh;
    if (!c.sendEnvelope(reply, err)) return false;

    protocol::Envelope challenge;
    challenge.kind = "pairing_challenge";
    protocol::PairingChallenge pc;
    pc.host_name = hostName();
    pc.challenge_nonce = core::makeChallengeNonce(
        std::chrono::steady_clock::now().time_since_epoch().count());
    challenge.payload = pc;
    return c.sendEnvelope(challenge, err);
  }

 private:
  static std::string hostName() {
#ifdef _WIN32
    wchar_t buf[64] = {0};
    DWORD n = 64;
    if (GetComputerNameW(buf, &n)) {
      std::string s;
      for (DWORD i = 0; i < n; ++i) s += (char)buf[i];
      return s;
    }
#endif
    return "windows-host";
  }
};

// Machine-readable selftest report (consumed by the build/test runner).
void printSelftest(const SelftestResult& res) {
  std::printf("{\"selftest\":true,\"passed\":%s,\"stage\":\"%s\","
              "\"port\":%u,\"reply_kind\":\"%s\",\"envelopes\":%zu,"
              "\"error\":\"%s\"}\n",
              res.passed ? "true" : "false", res.stage.c_str(),
              (unsigned)res.port, res.reply_kind.c_str(), res.envelopes,
              res.error.c_str());
}

}  // namespace

int AppController::runGui(const Options& opts) {
  Logger& log = Logger::instance();
  std::string err;
  if (!log.init(Logger::defaultPath(err))) {
    std::fprintf(stderr, "warning: log file not opened: %s\n",
                 err.c_str());
  }

  AppSignalingHandler handler;
  net::SignalingServer server(&handler);
  const std::string addr = opts.addr.empty() ? "0.0.0.0" : opts.addr;
  if (!server.start(addr, opts.port, err)) {
    log.write(LogLevel::Error, "host.start_failed",
              {{"addr", addr}, {"port", std::to_string(opts.port)},
               {"error", err}});
    return 2;
  }
  uint16_t actual = server.port(err);
  log.write(LogLevel::Info, "host.start",
            {{"addr", addr}, {"port", std::to_string(actual)}});

  TrayController tray(
      [&server]() -> TrayStatus {
        std::string e2;
        return TrayStatus{server.port(e2), server.connectionCount()};
      });
  int rc = tray.run();

  server.stop();
  log.write(LogLevel::Info, "host.stop",
            {{"envelopes", std::to_string(server.envelopesReceived())}});
  return rc;
}

int AppController::runSelftest(const Options& opts) {
  Logger& log = Logger::instance();
  std::string err;
  if (!log.init(Logger::defaultPath(err))) {
    std::fprintf(stderr, "warning: log file not opened: %s\n",
                 err.c_str());
  }

  SelftestResult res;

  AppSignalingHandler handler;
  net::SignalingServer server(&handler);
  if (!server.start("127.0.0.1", opts.port, err)) {
    res.stage = "start";
    res.error = err;
    log.write(LogLevel::Error, "selftest.fail",
              {{"stage", "start"}, {"error", err}});
    printSelftest(res);
    return 1;
  }
  res.port = server.port(err);
  log.write(LogLevel::Info, "selftest.server_up",
            {{"port", std::to_string(res.port)}});

  // In-process client (the server runs its accept/worker threads itself).
  {
    net::SignalingClient client;
    if (client.connectTo("127.0.0.1", res.port, err)) {
      protocol::Envelope hello;
      hello.kind = "hello";
      protocol::Hello h;
      h.product_name = "macos-client";
      h.app_version = "0.1.0";
      h.capabilities = {protocol::Capability::APP_CATALOG,
                        protocol::Capability::INPUT};
      hello.payload = h;
      if (client.sendEnvelope(hello, err)) {
        std::string err_code;
        auto reply = client.readEnvelope(5000, err, err_code);
        if (reply) {
          res.reply_kind = reply->kind;
          res.passed = reply->kind == "hello" &&
                       reply->schema_major == protocol::kProtocolMajor;
        } else {
          res.stage = "exchange";
          res.error = err.empty() ? err_code : err;
        }
      } else {
        res.stage = "exchange";
        res.error = err;
      }
      client.close();
    } else {
      res.stage = "exchange";
      res.error = err;
    }
  }

  res.envelopes = server.envelopesReceived();
  server.stop();
  if (res.passed) {
    log.write(LogLevel::Info, "selftest.pass",
              {{"port", std::to_string(res.port)}});
  } else {
    log.write(LogLevel::Error, "selftest.fail",
              {{"stage", res.stage}, {"error", res.error}});
  }
  printSelftest(res);
  return res.passed ? 0 : 1;
}
