
#ifndef RWH_MEDIA_STREAM_REGISTRY_HPP
#define RWH_MEDIA_STREAM_REGISTRY_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "core/fps_policy.hpp"
#include "media/media_framing.hpp"

namespace remotewindows::media {

// PROTOCOL.md §9.5: one active media stream (one announced window).
// State is derived at add() time from the session key + window_id (§9.3).
struct MediaStream {
  std::string window_id;
  uint16_t stream_id = 0;  // first 2 bytes of sha256(window_id), LE
  std::array<uint8_t, 32> key{};  // media_key(window_id), §9.3
  std::array<uint8_t, kMediaNoncePrefixBytes> nonce_prefix{};
  uint32_t packet_counter = 0;  // consumed by buildNonce, then incremented
  uint32_t frame_seq = 0;  // reset on (re)add; incremented per encoded frame
  bool keyframe_due = true;  // §9.5: first packet on (re)start is a keyframe
  core::FpsGate fps_gate;  // FR-09: applied before encoding (core defaults)
};

// Thread-safe registry of active media streams (the media server calls
// from the accept thread and capture threads).
//
// add() is idempotent: re-adding an existing window re-derives the key /
// nonce_prefix from the session key and resets packet_counter, frame_seq
// and keyframe_due (§9.5 — after any (re)connect a keyframe goes out
// first). add() returns false only on key-derivation failure (malformed
// session_key_hex form).
//
// Collision behavior (16-bit stream_id space): stream_ids are unique per
// registered window in practice; if two different window_ids ever hash to
// the same stream_id, the LAST add wins for findByStreamId() lookups,
// while find(window_id) is always by exact window_id.
//
// Pointers returned by find()/findByStreamId() stay valid until the
// referenced entry is removed or re-added; callers using the pointer
// across threads must hold their own synchronization.
class StreamRegistry {
 public:
  // Re-derives state for window_id from session_key_hex (64 lowercase
  // hex, the §7.4 session key). Idempotent; resets counters on re-add.
  bool add(const std::string& window_id, const std::string& session_key_hex);
  // Removes the stream for window_id. Returns false if not registered.
  bool remove(const std::string& window_id);
  // Exact window_id lookup; nullptr if not registered.
  const MediaStream* find(const std::string& window_id) const;
  // stream_id lookup (last add wins on a 16-bit collision); nullptr if
  // no registered stream has the id.
  const MediaStream* findByStreamId(uint16_t stream_id) const;
  size_t size() const;

 private:
  mutable std::mutex mu_;
  std::map<std::string, MediaStream> streams_;       // keyed by window_id
  std::map<uint16_t, std::string> stream_id_index_;  // last add wins
};

}  // namespace remotewindows::media

#endif  // RWH_MEDIA_STREAM_REGISTRY_HPP
