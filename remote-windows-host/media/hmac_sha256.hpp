
#ifndef RWH_MEDIA_HMAC_SHA256_HPP
#define RWH_MEDIA_HMAC_SHA256_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace remotewindows::media {

// RFC 2104 HMAC-SHA256 on top of core::Sha256 (block size 64).
// Keys longer than the block are pre-hashed; shorter keys are
// zero-padded, per RFC 2104. Used by the PROTOCOL §9.2 media-channel
// handshake: HMAC-SHA256(session_key_bytes, "rwh-media-v1|" || host_nonce).
std::array<uint8_t, 32> hmacSha256(const uint8_t* key, size_t key_len,
                                   const uint8_t* data, size_t data_len);

}  // namespace remotewindows::media

#endif  // RWH_MEDIA_HMAC_SHA256_HPP
