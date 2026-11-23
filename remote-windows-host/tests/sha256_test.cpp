
#include <gtest/gtest.h>
#include "core/sha256.hpp"

using remotewindows::core::Sha256;

TEST(Sha256, EmptyStringRfc4231) {
  EXPECT_EQ(Sha256::digest_hex(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, AsciiRfc4231) {
  EXPECT_EQ(Sha256::digest_hex("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256, LongerThanBlockRfc4231) {
  // 56 bytes — exercises the >64-byte padding path.
  // Verified against coreutils shasum -a 256 and CPython hashlib.sha256.
  std::string s(56, 'a');
  EXPECT_EQ(Sha256::digest_hex(s),
            "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
}

TEST(Sha256, OneMillionOfZerosRfc4231) {
  std::string s(1000000, '0');
  EXPECT_EQ(Sha256::digest_hex(s),
            "ba4b3010e2d91c08bd1987998d82b89b52ae1bdbc360f066607c7ee5a9c5830e");
}

TEST(Sha256, StreamingMatchesOneShot) {
  Sha256 h;
  h.update("ab");
  h.update("c");
  EXPECT_EQ(Sha256::hex(h.finalize()), Sha256::digest_hex("abc"));
}
