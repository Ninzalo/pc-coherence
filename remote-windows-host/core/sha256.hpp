#ifndef RWH_CORE_SHA256_HPP
#define RWH_CORE_SHA256_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace remotewindows::core {

// Minimal self-contained SHA-256 (no platform dependencies).
// Used for pairing-key derivation, catalog app_id generation, and test
// bundle checksums. NOT for performance-critical paths.
class Sha256 {
 public:
  Sha256();
  void update(const uint8_t* data, size_t len);
  void update(const std::string& s);
  // Finalizes and returns 32 raw bytes; object is reset for reuse.
  std::vector<uint8_t> finalize();
  static std::string hex(const std::vector<uint8_t>& bytes);
  static std::string digest_hex(const std::string& input);

 private:
  void reset_state();
  uint32_t h_[8];
  uint8_t buf_[64];
  size_t buf_len_;
  uint64_t total_len_;
};

}  // namespace remotewindows::core

#endif
