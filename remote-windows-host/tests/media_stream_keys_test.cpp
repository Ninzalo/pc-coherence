
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/pairing.hpp"
#include "core/sha256.hpp"
#include "media/stream_keys.hpp"

using namespace remotewindows::media;

namespace {

nlohmann::json loadVectorFile() {
  const std::string path = std::string(RWH_TEST_VECTORS_DIR) + "/media/key-derivation.json";
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "cannot open " << path;
  std::stringstream ss;
  ss << f.rdbuf();
  return nlohmann::json::parse(ss);
}

std::string hexBytes(const std::array<uint8_t, 32>& a) {
  return remotewindows::core::Sha256::hex(std::vector<uint8_t>(a.begin(),
                                                               a.end()));
}

std::string hexBytes(const uint8_t* p, size_t n) {
  return remotewindows::core::Sha256::hex(std::vector<uint8_t>(p, p + n));
}

}  // namespace

// PROTOCOL.md §9.3: reproduce EVERY field of
// protocol/test-vectors/media/key-derivation.json exactly.
TEST(MediaStreamKeys, GoldenVectorKeyDerivation) {
  const nlohmann::json doc = loadVectorFile();
  const std::string code = doc["inputs"]["code"].get<std::string>();
  const std::string host_name = doc["inputs"]["host_name"].get<std::string>();
  const std::string window_id = doc["inputs"]["window_id"].get<std::string>();
  const std::string session_key = doc["values"]["session_key"].get<std::string>();
  const std::string media_key = doc["values"]["media_key"].get<std::string>();
  const std::string nonce_prefix = doc["values"]["nonce_prefix"].get<std::string>();

  // Pin the synthetic inputs (guard against an edited vector file).
  EXPECT_EQ(code, "RWH-DEADBEEF");
  EXPECT_EQ(host_name, "TESTHOST");
  EXPECT_EQ(window_id, "11111111-2222-3333-4444-555555555555");

  // session_key: hex(sha256("rwh-pairing-v1|" + code + "|" + host_name)) —
  // exactly core::PairingManager::deriveSessionKey (core/pairing.cpp).
  EXPECT_EQ(
      remotewindows::core::PairingManager::deriveSessionKey(code, host_name),
      session_key);

  // media_key(window_id) = sha256("rwh-media-v1|" + session_key_hex + "|" +
  // window_id).
  std::array<uint8_t, 32> key{};
  ASSERT_TRUE(deriveMediaKey(session_key, window_id, key));
  EXPECT_EQ(hexBytes(key), media_key);

  // nonce_prefix(window_id) = first 8 bytes of
  // sha256("rwh-media-nonce-v1|" + session_key_hex + "|" + window_id).
  std::array<uint8_t, kMediaNoncePrefixBytes> prefix{};
  ASSERT_TRUE(deriveNoncePrefix(session_key, window_id, prefix));
  EXPECT_EQ(hexBytes(prefix.data(), prefix.size()), nonce_prefix);
}

TEST(MediaStreamKeys, DerivationFailsOnMalformedSessionKeyHex) {
  const std::string window_id = "11111111-2222-3333-4444-555555555555";
  std::array<uint8_t, 32> key{};
  std::array<uint8_t, kMediaNoncePrefixBytes> prefix{};

  // Not 64 chars.
  EXPECT_FALSE(deriveMediaKey("ccce75c3700c06a60acb90392eefb9b9", window_id, key));
  // 64 chars but not hex.
  EXPECT_FALSE(deriveMediaKey(std::string(64, 'z'), window_id, key));
  // 64 chars but uppercase (§9.3 form is lowercase-hex).
  EXPECT_FALSE(
      deriveMediaKey("CCCE75C3700C06A60ACB90392EEFB9B90AC94AD7CE6D68A92825FF64BDFB66AB",
                     window_id, key));
  // Same for the nonce prefix derivation.
  EXPECT_FALSE(deriveNoncePrefix(std::string(64, 'z'), window_id, prefix));
}

TEST(MediaStreamKeys, PerWindowKeySeparation) {
  // Synthetic 64-lowercase-hex session key (not a real secret).
  const std::string session_key =
      "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
  std::array<uint8_t, 32> key_a{}, key_b{};
  std::array<uint8_t, kMediaNoncePrefixBytes> prefix_a{}, prefix_b{};
  ASSERT_TRUE(deriveMediaKey(session_key, "11111111-2222-3333-4444-555555555555", key_a));
  ASSERT_TRUE(deriveMediaKey(session_key, "99999999-8888-7777-6666-555555555555", key_b));
  ASSERT_TRUE(deriveNoncePrefix(session_key, "11111111-2222-3333-4444-555555555555", prefix_a));
  ASSERT_TRUE(deriveNoncePrefix(session_key, "99999999-8888-7777-6666-555555555555", prefix_b));
  // §9.7: per-window key separation — different window_id ⇒ different key
  // and nonce prefix.
  EXPECT_NE(hexBytes(key_a), hexBytes(key_b));
  EXPECT_NE(hexBytes(prefix_a.data(), prefix_a.size()),
            hexBytes(prefix_b.data(), prefix_b.size()));
}
