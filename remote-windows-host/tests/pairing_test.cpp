
#include <gtest/gtest.h>
#include "core/pairing.hpp"

using namespace remotewindows::core;

namespace {
PairingManager makeManager(std::string code = "123456") {
  PairingConfig cfg;
  cfg.one_time_code = std::move(code);
  cfg.max_attempts = 3;
  cfg.lockout_seconds = 60;
  cfg.code_ttl_seconds = 300;
  return PairingManager(cfg);
}
}  // namespace

TEST(Pairing, CorrectCodePairs) {
  auto m = makeManager();
  auto st = m.verifyCode("123456", 1000);
  EXPECT_TRUE(st.paired);
  EXPECT_TRUE(st.error.empty());
  // Subsequent calls stay paired.
  EXPECT_TRUE(m.verifyCode("whatever", 2000).paired);
}

TEST(Pairing, WrongCodeFails) {
  auto m = makeManager();
  auto st = m.verifyCode("000000", 1000);
  EXPECT_FALSE(st.paired);
  EXPECT_EQ(st.error, "invalid_code");
}

TEST(Pairing, LockoutAfterMaxAttempts) {
  auto m = makeManager();
  m.verifyCode("111111", 1000);
  m.verifyCode("222222", 2000);
  auto st = m.verifyCode("333333", 3000);
  EXPECT_EQ(st.error, "locked");
  // Even the right code is rejected during lockout.
  st = m.verifyCode("123456", 4000);
  EXPECT_FALSE(st.paired);
  EXPECT_EQ(st.error, "locked");
  // After lockout expires the code works again. Lockout starts from the
  // last failed attempt (t=3000) and lasts 60s -> until 63000.
  st = m.verifyCode("123456", 3000 + 61 * 1000);
  EXPECT_TRUE(st.paired);
}

TEST(Pairing, ExpiredCode) {
  auto m = makeManager();
  auto st = m.verifyCode("123456", 1000 + 301 * 1000);
  EXPECT_FALSE(st.paired);
  EXPECT_EQ(st.error, "expired");
}

TEST(Pairing, RotateCodeChangesCode) {
  auto m = makeManager("111111");
  std::string rotated;
  m.rotateCode(rotated, 1000);
  EXPECT_EQ(rotated.size(), 6u);
  EXPECT_NE(rotated, "111111");
  // Old code no longer works; new one does.
  EXPECT_FALSE(m.verifyCode("111111", 2000).paired);
  EXPECT_TRUE(m.verifyCode(rotated, 3000).paired);
}

TEST(Pairing, SessionKeyDeterministic) {
  auto k1 = PairingManager::deriveSessionKey("123456", "DESKTOP-PC");
  auto k2 = PairingManager::deriveSessionKey("123456", "DESKTOP-PC");
  auto k3 = PairingManager::deriveSessionKey("123457", "DESKTOP-PC");
  EXPECT_EQ(k1, k2);
  EXPECT_NE(k1, k3);
  EXPECT_EQ(k1.size(), 64u);
}

TEST(Pairing, ChallengeNonceHex32) {
  auto n = makeChallengeNonce(42);
  EXPECT_EQ(n.size(), 32u);
  EXPECT_EQ(n, makeChallengeNonce(42));
  EXPECT_NE(n, makeChallengeNonce(43));
}
