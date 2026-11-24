
#include "media/stream_keys.hpp"

#include <cctype>

#include "core/sha256.hpp"

namespace remotewindows::media {

namespace {

// §9.3: session_key_hex is the 64 lowercase-hex string form of the
// 32-byte session key (core::PairingManager::deriveSessionKey output).
bool validSessionKeyHex(const std::string& session_key_hex) {
  if (session_key_hex.size() != 64) return false;
  for (char c : session_key_hex) {
    if (!std::isxdigit(static_cast<unsigned char>(c)) ||
        (c >= 'A' && c <= 'F'))
      return false;  // uppercase rejected: the format is lowercase-hex
  }
  return true;
}

// sha256 of "prefix || session_key_hex || '|' || window_id" → out digest.
bool deriveDigest(const char* prefix, const std::string& session_key_hex,
                  const std::string& window_id, std::vector<uint8_t>& out) {
  if (!validSessionKeyHex(session_key_hex)) return false;
  remotewindows::core::Sha256 h;
  h.update(prefix);
  h.update(session_key_hex);
  h.update("|");
  h.update(window_id);
  out = h.finalize();
  return true;
}

}  // namespace

bool deriveMediaKey(const std::string& session_key_hex,
                    const std::string& window_id,
                    std::array<uint8_t, 32>& key) {
  std::vector<uint8_t> digest;
  if (!deriveDigest("rwh-media-v1|", session_key_hex, window_id, digest))
    return false;
  if (digest.size() != 32) return false;
  for (size_t i = 0; i < 32; ++i) key[i] = digest[i];
  return true;
}

bool deriveNoncePrefix(const std::string& session_key_hex,
                       const std::string& window_id,
                       std::array<uint8_t, kMediaNoncePrefixBytes>& prefix) {
  std::vector<uint8_t> digest;
  if (!deriveDigest("rwh-media-nonce-v1|", session_key_hex, window_id,
                    digest))
    return false;
  if (digest.size() < kMediaNoncePrefixBytes) return false;
  for (size_t i = 0; i < kMediaNoncePrefixBytes; ++i) prefix[i] = digest[i];
  return true;
}

}  // namespace remotewindows::media
