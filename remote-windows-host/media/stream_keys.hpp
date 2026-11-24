
#ifndef RWH_MEDIA_STREAM_KEYS_HPP
#define RWH_MEDIA_STREAM_KEYS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "media/media_framing.hpp"

namespace remotewindows::media {

// PROTOCOL.md §9.3 (normative): per-window stream key separation.
//
// Inputs are the lowercase-hex forms exactly as produced by
// core::PairingManager::deriveSessionKey (64 lowercase-hex chars).
//
//   media_key(window_id)  = sha256("rwh-media-v1|"    + session_key_hex +
//                              "|" + window_id)           — 32 bytes (AES-256)
//   nonce_prefix(window_id) = first 8 bytes of
//                              sha256("rwh-media-nonce-v1|" + session_key_hex
//                                     + "|" + window_id)
//
// `window_id` is the window_announce UUID string (not the stream_id).
// Both return false (and leave `out` unchanged) when `session_key_hex`
// is not the 64-lowercase-hex session-key form of §9.3/§7.4.

bool deriveMediaKey(const std::string& session_key_hex,
                    const std::string& window_id,
                    std::array<uint8_t, 32>& key);

bool deriveNoncePrefix(const std::string& session_key_hex,
                       const std::string& window_id,
                       std::array<uint8_t, kMediaNoncePrefixBytes>& prefix);

}  // namespace remotewindows::media

#endif  // RWH_MEDIA_STREAM_KEYS_HPP
