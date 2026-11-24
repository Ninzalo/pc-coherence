
// Windows CNG AES-256-GCM backend test (PROTOCOL.md §9.4, ADR-0008).
// Compiled and run ONLY on the Windows runner (MSVC); on macOS this test
// is NOT RUN. Reproduces EVERY vector in
// protocol/test-vectors/media/aes-256-gcm.json exactly (ciphertext AND
// tag), plus round-trip and tamper-detection checks.

#if !defined(_WIN32)
#error "media_aead_cng_test.cpp is a Windows-only test (CNG/BCrypt)"
#endif

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "media/aead_cng.hpp"
#include "media/media_framing.hpp"

using namespace remotewindows::media;

namespace {

bool hexDecode(const std::string& hex, std::vector<uint8_t>& out) {
  if (hex.size() % 2 != 0) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  out.clear();
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int hi = nibble(hex[i]);
    const int lo = nibble(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out.push_back(uint8_t((hi << 4) | lo));
  }
  return true;
}

bool vectorsEqual(const std::vector<uint8_t>& actual,
                  const std::vector<uint8_t>& expected, const char* what) {
  if (actual != expected) {
    auto hexOf = [](const std::vector<uint8_t>& v) {
      static const char* d = "0123456789abcdef";
      std::string s;
      for (uint8_t b : v) {
        s.push_back(d[b >> 4]);
        s.push_back(d[b & 0xF]);
      }
      return s;
    };
    ADD_FAILURE() << what << " mismatch:\n  expected " << hexOf(expected)
                  << "\n  actual   " << hexOf(actual);
    return false;
  }
  return true;
}

}  // namespace

TEST(MediaAeadCng, GoldenVectorsReproducedExactly) {
  const std::string path =
      std::string(RWH_TEST_VECTORS_DIR) + "/media/aes-256-gcm.json";
  std::ifstream f(path);
  ASSERT_TRUE(f.good()) << "cannot open " << path;
  std::stringstream ss;
  ss << f.rdbuf();
  const nlohmann::json doc = nlohmann::json::parse(ss);
  const nlohmann::json& vectors = doc["vectors"];
  ASSERT_EQ(vectors.size(), 3u);

  AeadCng aead;
  for (const auto& v : vectors) {
    std::vector<uint8_t> key, nonce, pt, ct, tag;
    ASSERT_TRUE(hexDecode(v["key"].get<std::string>(), key));
    ASSERT_TRUE(hexDecode(v["nonce"].get<std::string>(), nonce));
    ASSERT_TRUE(hexDecode(v["plaintext"].get<std::string>(), pt));
    ASSERT_TRUE(hexDecode(v["ciphertext"].get<std::string>(), ct));
    ASSERT_TRUE(hexDecode(v["tag"].get<std::string>(), tag));
    ASSERT_EQ(key.size(), 32u);
    ASSERT_EQ(nonce.size(), 12u);
    ASSERT_EQ(tag.size(), kMediaGcmTagBytes);

    // Seal with empty AAD; CNG writes the tag at the END of the output.
    std::vector<uint8_t> out(pt.size() + kMediaGcmTagBytes);
    size_t out_len = 0;
    ASSERT_TRUE(aead.seal(key.data(), nonce.data(), pt.data(), pt.size(),
                          out.data(), out_len))
        << v["name"].get<std::string>();
    EXPECT_EQ(out_len, pt.size() + kMediaGcmTagBytes);
    EXPECT_TRUE(vectorsEqual(
        std::vector<uint8_t>(out.begin(), out.begin() + pt.size()), ct,
        (v["name"].get<std::string>() + " ciphertext").c_str()));
    EXPECT_TRUE(vectorsEqual(
        std::vector<uint8_t>(out.begin() + pt.size(), out.end()), tag,
        (v["name"].get<std::string>() + " tag").c_str()));

    // The sealed blob opens back to the original plaintext.
    std::vector<uint8_t> back(pt.size());
    size_t back_len = 0;
    ASSERT_TRUE(aead.open(key.data(), nonce.data(), out.data(), out.size(),
                          back.data(), back_len))
        << v["name"].get<std::string>();
    EXPECT_EQ(back_len, pt.size());
    EXPECT_TRUE(vectorsEqual(back, pt,
                             (v["name"].get<std::string>() + " round-trip").c_str()));
  }
}

TEST(MediaAeadCng, RoundTrip1000Bytes) {
  const std::string key_hex =
      "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
  const std::string nonce_hex = "000102030405060708090a0b";
  std::vector<uint8_t> key, nonce;
  ASSERT_TRUE(hexDecode(key_hex, key));
  ASSERT_TRUE(hexDecode(nonce_hex, nonce));

  std::vector<uint8_t> pt(1000);
  for (size_t i = 0; i < pt.size(); ++i) pt[i] = uint8_t(i * 7 + 3);

  AeadCng aead;
  std::vector<uint8_t> out(pt.size() + kMediaGcmTagBytes);
  size_t out_len = 0;
  ASSERT_TRUE(aead.seal(key.data(), nonce.data(), pt.data(), pt.size(),
                        out.data(), out_len));
  ASSERT_EQ(out_len, pt.size() + kMediaGcmTagBytes);

  std::vector<uint8_t> back(pt.size());
  size_t back_len = 0;
  ASSERT_TRUE(aead.open(key.data(), nonce.data(), out.data(), out.size(),
                        back.data(), back_len));
  EXPECT_EQ(back_len, pt.size());
  EXPECT_TRUE(vectorsEqual(back, pt, "1000-byte round-trip"));
}

TEST(MediaAeadCng, OpenRejectsTamperedCiphertext) {
  const std::string key_hex =
      "4242424242424242424242424242424242424242424242424242424242424242";
  const std::string nonce_hex = "000000000000000000000002";
  std::vector<uint8_t> key, nonce, pt;
  ASSERT_TRUE(hexDecode(key_hex, key));
  ASSERT_TRUE(hexDecode(nonce_hex, nonce));
  pt.push_back(0x41);  // vec_c_1byte plaintext

  AeadCng aead;
  std::vector<uint8_t> blob(pt.size() + kMediaGcmTagBytes);
  size_t out_len = 0;
  ASSERT_TRUE(aead.seal(key.data(), nonce.data(), pt.data(), pt.size(),
                        blob.data(), out_len));
  ASSERT_EQ(out_len, pt.size() + kMediaGcmTagBytes);

  // Tamper one ciphertext byte (the first byte is plaintext-length 1, so
  // the only ciphertext byte).
  std::vector<uint8_t> tampered = blob;
  tampered[0] ^= 0xFF;
  std::vector<uint8_t> back(1);
  size_t back_len = 0;
  EXPECT_FALSE(aead.open(key.data(), nonce.data(), tampered.data(),
                         tampered.size(), back.data(), back_len));

  // Tamper one tag byte.
  tampered = blob;
  tampered[blob.size() - 1] ^= 0x01;
  back_len = 0;
  EXPECT_FALSE(aead.open(key.data(), nonce.data(), tampered.data(),
                         tampered.size(), back.data(), back_len));

  // Wrong nonce.
  std::vector<uint8_t> wrong_nonce = nonce;
  wrong_nonce[11] ^= 0x01;
  back_len = 0;
  EXPECT_FALSE(aead.open(key.data(), wrong_nonce.data(), blob.data(),
                         blob.size(), back.data(), back_len));

  // Control: the unmodified blob still opens.
  ASSERT_TRUE(aead.open(key.data(), nonce.data(), blob.data(), blob.size(),
                        back.data(), back_len));
  EXPECT_EQ(back_len, pt.size());
  EXPECT_TRUE(vectorsEqual(back, pt, "untampered control"));
}
