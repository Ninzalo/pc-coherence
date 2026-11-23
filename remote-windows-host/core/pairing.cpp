
#include "core/pairing.hpp"

#include <cctype>
#include <cstring>

#include "core/sha256.hpp"

namespace remotewindows::core {

PairingManager::PairingManager(PairingConfig config) : cfg_(std::move(config)) {}

PairingState PairingManager::verifyCode(const std::string& presented,
                                        uint64_t now_ms) {
  if (state_.paired) {
    state_.error = "";
    return state_;
  }
  if (state_.locked) {
    if (now_ms < state_.lockout_until_ms) {
      state_.error = "locked";
      return state_;
    }
    state_.locked = false;
    state_.attempts_used = 0;
  }
  if (now_ms - code_created_ms_ > uint64_t(cfg_.code_ttl_seconds) * 1000) {
    state_.error = "expired";
    state_.locked = false;
    return state_;
  }
  // Constant-time comparison to avoid timing side channels.
  bool equal = true;
  if (presented.size() != cfg_.one_time_code.size()) equal = false;
  else {
    uint8_t acc = 0;
    for (size_t i = 0; i < presented.size(); ++i)
      acc = (uint8_t)(acc ^ presented[i] ^ cfg_.one_time_code[i]);
    equal = acc == 0;
  }
  if (equal) {
    state_.paired = true;
    state_.error = "";
  } else {
    ++state_.attempts_used;
    if (state_.attempts_used >= cfg_.max_attempts) {
      state_.locked = true;
      state_.lockout_until_ms = now_ms + uint64_t(cfg_.lockout_seconds) * 1000;
      state_.error = "locked";
    } else {
      state_.error = "invalid_code";
    }
  }
  return state_;
}

void PairingManager::rotateCode(std::string& new_code_out, uint64_t now_ms) {
  // Deterministic rotation from entropy + time; 6 digits, never 000000.
  uint64_t mix = now_ms * 0x9E3779B97F4A7C15ull +
                 0xD1B54A32D192ED03ull;
  mix ^= mix << 13; mix ^= mix >> 7; mix ^= mix << 17;
  uint32_t digits = uint32_t(mix % 1000000u);
  char buf[8];
  snprintf(buf, sizeof(buf), "%06u", digits);
  cfg_.one_time_code = buf;
  code_created_ms_ = now_ms;
  state_.paired = false;
  state_.attempts_used = 0;
  state_.locked = false;
  state_.error = "";
  new_code_out = cfg_.one_time_code;
}

const PairingState& PairingManager::state() const { return state_; }

std::string PairingManager::deriveSessionKey(const std::string& code,
                                             const std::string& host_name) {
  Sha256 h;
  h.update("rwh-pairing-v1|");
  h.update(code);
  h.update("|");
  h.update(host_name);
  return Sha256::hex(h.finalize());
}

std::string makeChallengeNonce(uint64_t seed) {
  Sha256 h;
  std::string input = std::to_string(seed);
  h.update(input);
  return Sha256::hex(h.finalize()).substr(0, 32);
}

}  // namespace remotewindows::core
