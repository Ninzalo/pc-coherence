
#include "media/stream_registry.hpp"

#include "media/stream_keys.hpp"

namespace remotewindows::media {

bool StreamRegistry::add(const std::string& window_id,
                         const std::string& session_key_hex) {
  // Derive everything BEFORE taking the lock: add() returns false only
  // on key-derivation failure, and partial state must never be published.
  MediaStream stream;
  if (!streamIdFor(window_id, stream.stream_id)) return false;
  if (!deriveMediaKey(session_key_hex, window_id, stream.key)) return false;
  if (!deriveNoncePrefix(session_key_hex, window_id, stream.nonce_prefix))
    return false;
  stream.window_id = window_id;
  // Counters reset on (re)add: §9.5 — after (re)connect the first packet
  // on the stream is a keyframe; per-connection frame_seq/packet_counter
  // start from 0. A fresh gate with the core defaults (target_fps = 30);
  // the capture loop re-applies the Client's frame_rate_request.
  stream.packet_counter = 0;
  stream.frame_seq = 0;
  stream.keyframe_due = true;
  stream.fps_gate = core::FpsGate{};

  std::lock_guard<std::mutex> lock(mu_);
  streams_[window_id] = std::move(stream);
  stream_id_index_[streams_.at(window_id).stream_id] = window_id;  // last add wins
  return true;
}

bool StreamRegistry::remove(const std::string& window_id) {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = streams_.find(window_id);
  if (it == streams_.end()) return false;
  const uint16_t stream_id = it->second.stream_id;
  streams_.erase(it);
  // Keep the index entry only if it still points at this window (a later
  // add may have moved the id to a colliding window).
  const auto idx = stream_id_index_.find(stream_id);
  if (idx != stream_id_index_.end() && idx->second == window_id)
    stream_id_index_.erase(idx);
  return true;
}

const MediaStream* StreamRegistry::find(const std::string& window_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = streams_.find(window_id);
  if (it == streams_.end()) return nullptr;
  return &it->second;
}

const MediaStream* StreamRegistry::findByStreamId(uint16_t stream_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto idx = stream_id_index_.find(stream_id);
  if (idx == stream_id_index_.end()) return nullptr;
  const auto it = streams_.find(idx->second);
  if (it == streams_.end()) return nullptr;  // stale index entry (impossible, but safe)
  return &it->second;
}

size_t StreamRegistry::size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return streams_.size();
}

}  // namespace remotewindows::media
