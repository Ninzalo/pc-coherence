
// rwh-signaling-probe — development/test helper for the LAN signaling
// channel.
//
//   rwh-signaling-probe serve --port 47987 [--addr 0.0.0.0] [--duration N]
//     Serves: answers HELLO with HELLO + PAIRING_CHALLENGE, counts
//     connections and envelopes, prints a JSON report and exits.
//
//   rwh-signaling-probe connect --host H --port P
//     Connects, sends HELLO (macos-client), reads one reply envelope,
//     prints its kind + schema version. Exit 0 on a sane reply.
//
// Used by the remote build/test workflow (plan §18): the Windows runner
// serves, the macOS side connects, proving an authenticated end-to-end
// channel without product media involved.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>

#include "core/pairing.hpp"
#include "core/protocol_json.hpp"
#include "core/protocol_types.hpp"
#include "network/signaling_client.hpp"
#include "network/signaling_server.hpp"

using namespace remotewindows;

namespace {

std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }

int runServe(int port, const std::string& addr, int duration_s) {
  struct ProbeHandler : net::SignalingServer::Handler {
    bool onEnvelope(net::Connection& c, const protocol::Envelope& e) override {
      if (e.kind == "hello") {
        protocol::Envelope reply;
        reply.kind = "hello";
        auto hello = std::get<protocol::Hello>(e.payload);
        protocol::Hello rh;
        rh.product_name = "windows-host";
        rh.app_version = "0.1.0";
        rh.capabilities = {protocol::Capability::APP_CATALOG,
                           protocol::Capability::INPUT};
        reply.payload = rh;
        std::string err;
        if (!c.sendEnvelope(reply, err)) return false;

        protocol::Envelope challenge;
        challenge.kind = "pairing_challenge";
        protocol::PairingChallenge pc;
        pc.host_name = "probe-host";
        pc.challenge_nonce = core::makeChallengeNonce(
            std::chrono::steady_clock::now().time_since_epoch().count());
        challenge.payload = pc;
        if (!c.sendEnvelope(challenge, err)) return false;
      }
      return true;
    }
  };

  ProbeHandler handler;
  net::SignalingServer server(&handler);
  std::string err;
  if (!server.start(addr, (uint16_t)port, err)) {
    std::fprintf(stderr, "serve failed: %s\n", err.c_str());
    return 2;
  }
  uint16_t actual = server.port(err);
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  auto start = std::chrono::steady_clock::now();
  while (!g_stop) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    if (duration_s > 0 && elapsed >= duration_s) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  server.stop();
  std::printf("{\"served\":true,\"port\":%u,\"connections\":%zu,"
              "\"envelopes\":%llu,\"malformed\":%llu}\n",
              (unsigned)actual, server.connectionCount(),
              (unsigned long long)server.envelopesReceived(),
              (unsigned long long)server.malformedFrames());
  return 0;
}

int runConnect(const std::string& host, int port) {
  net::SignalingClient client;
  std::string err;
  if (!client.connectTo(host, (uint16_t)port, err)) {
    std::fprintf(stderr, "connect failed: %s\n", err.c_str());
    return 1;
  }
  protocol::Envelope hello;
  hello.kind = "hello";
  protocol::Hello h;
  h.product_name = "macos-client";
  h.app_version = "0.1.0";
  h.capabilities = {protocol::Capability::APP_CATALOG,
                    protocol::Capability::INPUT};
  hello.payload = h;
  if (!client.sendEnvelope(hello, err)) {
    std::fprintf(stderr, "send failed: %s\n", err.c_str());
    return 1;
  }
  std::string err_code;
  auto reply = client.readEnvelope(5000, err, err_code);
  if (!reply) {
    std::fprintf(stderr, "no reply: %s (%s)\n", err.c_str(),
                 err_code.c_str());
    return 1;
  }
  std::printf("{\"connected\":true,\"reply_kind\":\"%s\",\"schema_major\":%u,"
              "\"schema_minor\":%u}\n",
              reply->kind.c_str(), (unsigned)reply->schema_major,
              (unsigned)reply->schema_minor);
  client.close();
  return reply->kind == "hello" ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::string mode = argc > 1 ? argv[1] : "";
  std::string host, addr;
  int port = 0, duration = 0;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](int& v) {
      if (i + 1 < argc) v = atoi(argv[++i]);
    };
    auto nextStr = [&](std::string& s) {
      if (i + 1 < argc) s = argv[++i];
    };
    if (a == "--port") next(port);
    else if (a == "--host") nextStr(host);
    else if (a == "--addr") nextStr(addr);
    else if (a == "--duration") next(duration);
  }
  if (mode == "serve") return runServe(port ? port : 47987, addr, duration);
  if (mode == "connect") return runConnect(host.empty() ? "127.0.0.1" : host,
                                           port ? port : 47987);
  std::fprintf(stderr, "usage: rwh-signaling-probe serve --port N | "
                       "connect --host H --port N\n");
  return 2;
}
