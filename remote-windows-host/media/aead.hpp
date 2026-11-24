
#ifndef RWH_MEDIA_AEAD_HPP
#define RWH_MEDIA_AEAD_HPP

#include <cstddef>
#include <cstdint>

namespace remotewindows::media {

// PROTOCOL.md §9.4: AES-256-GCM for the media channel (empty AAD).
// Portable interface; backends implement it per platform (Windows CNG:
// AeadCng; the macOS reference is CryptoKit AES.GCM, per ADR-0008).
// Implementations must reproduce protocol/test-vectors/media/aes-256-gcm.json
// EXACTLY (ciphertext and tag).
class IAead {
 public:
  virtual ~IAead() = default;
  // Seal: out buffer must hold in_len + kMediaGcmTagBytes (ciphertext
  // followed by the 16-byte GCM tag). Returns false on failure (no
  // partial write); out_len = in_len + 16 on success.
  virtual bool seal(const uint8_t key[32], const uint8_t nonce[12],
                    const uint8_t* in, size_t in_len,
                    uint8_t* out_ct_tag, size_t& out_len) const = 0;
  // Open: in is ciphertext+tag (in_len >= kMediaGcmTagBytes); out holds
  // at least in_len - kMediaGcmTagBytes. Returns false on any
  // authentication failure (never partially writes).
  virtual bool open(const uint8_t key[32], const uint8_t nonce[12],
                    const uint8_t* in, size_t in_len,
                    uint8_t* out_pt, size_t& out_len) const = 0;
};

}  // namespace remotewindows::media

#endif  // RWH_MEDIA_AEAD_HPP
