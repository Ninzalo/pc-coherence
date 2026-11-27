
#ifndef RWH_CORE_CAPTURE_GATE_HPP
#define RWH_CORE_CAPTURE_GATE_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

#include "core/capture_source.hpp"

namespace remotewindows::core {

// RW-H-011 (H-MEDIA, FR-06 excerpt): the pre-encode frame gate —
// "gate frames before encoding; drop obsolete frames rather than
// queueing stale video; bounded queues".
//
// Fixed maximum depth. A frame is "obsolete" as soon as a newer one has
// arrived for the same surface: when the queue is full the OLDEST frame
// (the most obsolete one) is dropped and the newest accepted. The queue
// can never grow past max_depth. Thread-safe (adapter thread submits,
// the session worker pops).
class FrameGate {
 public:
  // Default depth matches CaptureManagerConfig::queue_depth.
  FrameGate() : FrameGate(8) {}
  explicit FrameGate(size_t max_depth);
  // Accept `frame`. When the queue is full, drops the oldest retained
  // frame first (counted in droppedCount()). Returns true when `frame`
  // is retained, false only when max_depth == 0.
  bool submit(const CapturedFrame& frame);
  // Pop the oldest retained frame (FIFO, so the encoder sees frames in
  // order; under backpressure the obsolete ones are already gone).
  bool pop(CapturedFrame& out);
  // Clear the queue and set a new fixed bound (session (re)start).
  // NOT counted as drops: the stream stops, the frames are not obsolete.
  void reset(size_t max_depth);
  // Discard all retained frames WITHOUT counting them (session end).
  void discard();
  size_t depth() const;
  size_t maxDepth() const;
  // Frames dropped as obsolete (queue-full evictions).
  uint64_t droppedCount() const;

 private:
  mutable std::mutex mu_;
  std::deque<CapturedFrame> q_;
  size_t max_depth_;
  uint64_t dropped_ = 0;
};

}  // namespace remotewindows::core

#endif  // RWH_CORE_CAPTURE_GATE_HPP
