
#include <gtest/gtest.h>
#include <thread>

#include "core/pairing.hpp"
#include "network/framing.hpp"
#include "network/signaling_client.hpp"
#include "network/signaling_server.hpp"

using namespace remotewindows;
namespace proto = remotewindows::protocol;
namespace netns = remotewindows::net;

namespace {

struct EchoHandler : netns::SignalingServer::Handler {
  bool onEnvelope(netns::Connection& c, const proto::Envelope& e) override {
    if (e.kind == "hello") {
      proto::Envelope reply;
      reply.kind = "hello";
      proto::Hello h;
      h.product_name = "windows-host";
      h.app_version = "0.1.0";
      h.capabilities = {proto::Capability::APP_CATALOG};
      reply.payload = h;
      std::string err;
      if (!c.sendEnvelope(reply, err)) return false;
    }
    return true;
  }
};

}  // namespace

TEST(SignalingE2E, HelloReply) {
  EchoHandler handler;
  netns::SignalingServer server(&handler);
  std::string err;
  ASSERT_TRUE(server.start("127.0.0.1", 0, err)) << err;
  auto port = server.port(err);

  netns::SignalingClient client;
  ASSERT_TRUE(client.connectTo("127.0.0.1", port, err)) << err;

  proto::Envelope hello;
  hello.kind = "hello";
  proto::Hello h;
  h.product_name = "macos-client";
  h.app_version = "0.1.0";
  hello.payload = h;
  ASSERT_TRUE(client.sendEnvelope(hello, err)) << err;

  std::string code;
  auto reply = client.readEnvelope(5000, err, code);
  ASSERT_TRUE(reply.has_value()) << err << " / " << code;
  EXPECT_EQ(reply->kind, "hello");
  EXPECT_EQ(reply->schema_major, 1u);

  client.close();
  server.stop();
}

TEST(SignalingE2E, MalformedFramesCountedNotCrash) {
  EchoHandler handler;
  netns::SignalingServer server(&handler);
  std::string err;
  ASSERT_TRUE(server.start("127.0.0.1", 0, err)) << err;
  auto port = server.port(err);

  netns::TcpSocket raw;
  ASSERT_TRUE(raw.connectTo("127.0.0.1", port, err)) << err;
  // 3 bogus frames (not JSON), then a valid HELLO.
  auto bad = netns::encodeFrame("this is not json");
  for (int i = 0; i < 3; ++i)
    ASSERT_TRUE(raw.sendAll(bad.data(), bad.size(), err)) << err;

  netns::SignalingClient client;
  ASSERT_TRUE(client.connectTo("127.0.0.1", port, err)) << err;
  proto::Envelope hello;
  hello.kind = "hello";
  proto::Hello h;
  h.product_name = "macos-client";
  h.app_version = "0.1.0";
  hello.payload = h;
  ASSERT_TRUE(client.sendEnvelope(hello, err)) << err;

  std::string code;
  auto reply = client.readEnvelope(5000, err, code);
  ASSERT_TRUE(reply.has_value()) << err;
  EXPECT_EQ(server.malformedFrames(), 3u);
  EXPECT_GE(server.envelopesReceived(), 1u);

  client.close();
  raw.close();
  server.stop();
}

TEST(SignalingE2E, MultipleConcurrentClients) {
  EchoHandler handler;
  netns::SignalingServer server(&handler);
  std::string err;
  ASSERT_TRUE(server.start("127.0.0.1", 0, err)) << err;
  auto port = server.port(err);

  std::vector<std::thread> threads;
  std::atomic<int> ok{0};
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&, port] {
      netns::SignalingClient c;
      std::string e;
      if (!c.connectTo("127.0.0.1", port, e)) return;
      proto::Envelope hello;
      hello.kind = "hello";
      proto::Hello h;
      h.product_name = "macos-client";
      h.app_version = "0.1.0";
      hello.payload = h;
      if (!c.sendEnvelope(hello, e)) return;
      std::string code;
      auto reply = c.readEnvelope(5000, e, code);
      if (reply && reply->kind == "hello") ++ok;
      c.close();
    });
  }
  for (auto& t : threads) t.join();
  EXPECT_EQ(ok.load(), 4);
  server.stop();
}
