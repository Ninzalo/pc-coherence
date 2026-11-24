
#include <gtest/gtest.h>

#include <array>
#include <cctype>
#include <string>

#include "core/sha256.hpp"
#include "media/hmac_sha256.hpp"

using namespace remotewindows::media;

namespace {

std::string hex(const std::array<uint8_t, 32>& a) {
  return remotewindows::core::Sha256::hex(
      std::vector<uint8_t>(a.begin(), a.end()));
}

}  // namespace

// RFC 4231, Test Case 1:
// key = 0x0b x 20, data = "Hi There"
TEST(HmacSha256, Rfc4231TestCase1) {
  std::array<uint8_t, 20> key{};
  for (auto& b : key) b = 0x0b;
  const std::string data = "Hi There";
  const std::array<uint8_t, 32> mac =
      hmacSha256(key.data(), key.size(),
                 reinterpret_cast<const uint8_t*>(data.data()), data.size());
  EXPECT_EQ(hex(mac),
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

// RFC 4231, Test Case 2:
// key = "Jefe", data = "what do ya want for nothing?"
TEST(HmacSha256, Rfc4231TestCase2) {
  const std::string key = "Jefe";
  const std::string data = "what do ya want for nothing?";
  const std::array<uint8_t, 32> mac =
      hmacSha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                 reinterpret_cast<const uint8_t*>(data.data()), data.size());
  EXPECT_EQ(hex(mac),
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

// PROTOCOL §9.2-shaped handshake MAC: 64-hex session key decoded to 32
// raw bytes, 16-byte host nonce, message "rwh-media-v1|" || nonce.
// No golden value is invented: assert self-consistency — the MAC is a
// stable 32-byte digest (64 lowercase-hex chars), deterministic across
// calls, and changes when any input changes.
TEST(HmacSha256, MediaHandshakeShapeStable32ByteHex) {
  // Synthetic session key (not a real secret): 64 lowercase-hex chars.
  const std::string session_key_hex =
      "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
  std::array<uint8_t, 32> key{};
  for (size_t i = 0; i < 32; ++i)
    key[i] = uint8_t(std::stoi(session_key_hex.substr(2 * i, 2), nullptr, 16));

  std::array<uint8_t, 16> nonce{};
  for (size_t i = 0; i < nonce.size(); ++i) nonce[i] = uint8_t(0x40 + i);

  std::string message = "rwh-media-v1|";
  message.append(reinterpret_cast<const char*>(nonce.data()), nonce.size());

  const std::array<uint8_t, 32> mac =
      hmacSha256(key.data(), key.size(),
                 reinterpret_cast<const uint8_t*>(message.data()),
                 message.size());

  // Stable 32-byte hex: 64 lowercase-hex chars.
  const std::string mac_hex = hex(mac);
  ASSERT_EQ(mac_hex.size(), 64u);
  for (char c : mac_hex)
    EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c)) &&
                    !(c >= 'A' && c <= 'F'))
        << "non-lowercase-hex char in " << mac_hex;

  // Self-consistency: identical inputs ⇒ identical MAC.
  const std::array<uint8_t, 32> mac_again =
      hmacSha256(key.data(), key.size(),
                 reinterpret_cast<const uint8_t*>(message.data()),
                 message.size());
  EXPECT_EQ(mac, mac_again);

  // Sensitivity: a different nonce changes the MAC.
  nonce[15] ^= 0x01;
  std::string message2 = "rwh-media-v1|";
  message2.append(reinterpret_cast<const char*>(nonce.data()), nonce.size());
  const std::array<uint8_t, 32> mac2 =
      hmacSha256(key.data(), key.size(),
                 reinterpret_cast<const uint8_t*>(message2.data()),
                 message2.size());
  EXPECT_NE(mac, mac2);
}
