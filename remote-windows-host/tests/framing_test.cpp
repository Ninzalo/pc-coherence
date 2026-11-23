
#include <gtest/gtest.h>
#include "network/framing.hpp"

using namespace remotewindows::net;

TEST(Framing, EncodeFrameBigEndianLength) {
  auto f = encodeFrame("abcd");
  ASSERT_EQ(f.size(), 8u);
  EXPECT_EQ(f[0], 0);
  EXPECT_EQ(f[1], 0);
  EXPECT_EQ(f[2], 0);
  EXPECT_EQ(f[3], 4);
  EXPECT_EQ(std::string(f.begin() + 4, f.end()), "abcd");
}

TEST(Framing, EncodeFrameEmpty) {
  auto f = encodeFrame("");
  ASSERT_EQ(f.size(), 4u);
}

TEST(Framing, LoopbackRoundTrip) {
  std::string err;
  TcpSocket server;
  ASSERT_TRUE(server.bindAndListen("127.0.0.1", 0, err)) << err;
  auto port = server.localPort(err);
  ASSERT_NE(port, 0u);

  TcpSocket client;
  ASSERT_TRUE(client.connectTo("127.0.0.1", port, err)) << err;
  auto accepted = server.accept(err);
  ASSERT_TRUE(accepted.has_value()) << err;

  auto frame = encodeFrame("hello world");
  ASSERT_TRUE(client.sendAll(frame.data(), frame.size(), err)) << err;

  std::string json;
  ASSERT_TRUE(readFrame(*accepted, json, err)) << err;
  EXPECT_EQ(json, "hello world");
}

TEST(Framing, PartialReadsReassembled) {
  std::string err;
  TcpSocket server;
  ASSERT_TRUE(server.bindAndListen("127.0.0.1", 0, err)) << err;
  auto port = server.localPort(err);
  TcpSocket client;
  ASSERT_TRUE(client.connectTo("127.0.0.1", port, err)) << err;
  auto accepted = server.accept(err);
  ASSERT_TRUE(accepted.has_value());

  auto frame = encodeFrame(std::string(10000, 'z'));
  // Send in awkward chunks to exercise partial reads.
  ASSERT_TRUE(client.sendAll(frame.data(), 3, err));
  ASSERT_TRUE(client.sendAll(frame.data() + 3, 7, err));
  ASSERT_TRUE(client.sendAll(frame.data() + 10, frame.size() - 10, err));
  std::string json;
  ASSERT_TRUE(readFrame(*accepted, json, err)) << err;
  EXPECT_EQ(json, std::string(10000, 'z'));
}

TEST(Framing, ClosedConnectionReported) {
  std::string err;
  TcpSocket server;
  ASSERT_TRUE(server.bindAndListen("127.0.0.1", 0, err)) << err;
  auto port = server.localPort(err);
  TcpSocket client;
  ASSERT_TRUE(client.connectTo("127.0.0.1", port, err)) << err;
  auto accepted = server.accept(err);
  ASSERT_TRUE(accepted.has_value());
  client.close();
  std::string json;
  EXPECT_FALSE(readFrame(*accepted, json, err));
  EXPECT_NE(err.find("closed"), std::string::npos);
}
