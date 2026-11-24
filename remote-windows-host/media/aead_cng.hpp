
#ifndef RWH_MEDIA_AEAD_CNG_HPP
#define RWH_MEDIA_AEAD_CNG_HPP

// Windows CNG (BCrypt*) backend for the media AES-256-GCM interface.
// WIN32-only: this class is declared (and the translation unit compiled)
// only on Windows. It is unit-tested only on the Windows runner; on
// macOS the backend test is NOT RUN.

#ifdef _WIN32

#include "media/aead.hpp"

namespace remotewindows::media {

// Thread-safe: creates and destroys the algorithm and key objects inside
// each call; no member state.
class AeadCng : public IAead {
 public:
  bool seal(const uint8_t key[32], const uint8_t nonce[12],
            const uint8_t* in, size_t in_len, uint8_t* out_ct_tag,
            size_t& out_len) const override;
  bool open(const uint8_t key[32], const uint8_t nonce[12],
            const uint8_t* in, size_t in_len, uint8_t* out_pt,
            size_t& out_len) const override;
};

}  // namespace remotewindows::media

#endif  // _WIN32

#endif  // RWH_MEDIA_AEAD_CNG_HPP
