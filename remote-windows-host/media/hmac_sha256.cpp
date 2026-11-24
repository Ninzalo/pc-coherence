
#include "media/hmac_sha256.hpp"

#include "core/sha256.hpp"

namespace remotewindows::media {

namespace {
constexpr size_t kSha256BlockBytes = 64;
}

std::array<uint8_t, 32> hmacSha256(const uint8_t* key, size_t key_len,
                                   const uint8_t* data, size_t data_len) {
  // RFC 2104: reduce the key to at most one block.
  std::vector<uint8_t> block_key(kSha256BlockBytes, 0);
  if (key_len > kSha256BlockBytes) {
    remotewindows::core::Sha256 kh;
    kh.update(key, key_len);
    const std::vector<uint8_t> kd = kh.finalize();
    for (size_t i = 0; i < kd.size() && i < kSha256BlockBytes; ++i)
      block_key[i] = kd[i];
  } else {
    for (size_t i = 0; i < key_len; ++i) block_key[i] = key[i];
  }

  std::vector<uint8_t> ipad(kSha256BlockBytes), opad(kSha256BlockBytes);
  for (size_t i = 0; i < kSha256BlockBytes; ++i) {
    ipad[i] = uint8_t(block_key[i] ^ 0x36);
    opad[i] = uint8_t(block_key[i] ^ 0x5C);
  }

  // inner = sha256(ipad || data)
  remotewindows::core::Sha256 inner;
  inner.update(ipad.data(), ipad.size());
  if (data_len > 0) inner.update(data, data_len);
  const std::vector<uint8_t> inner_digest = inner.finalize();

  // result = sha256(opad || inner)
  remotewindows::core::Sha256 outer;
  outer.update(opad.data(), opad.size());
  outer.update(inner_digest.data(), inner_digest.size());
  const std::vector<uint8_t> digest = outer.finalize();

  std::array<uint8_t, 32> mac{};
  for (size_t i = 0; i < 32 && i < digest.size(); ++i) mac[i] = digest[i];
  return mac;
}

}  // namespace remotewindows::media
