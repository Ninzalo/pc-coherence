
#include "core/capture_gate.hpp"

namespace remotewindows::core {

FrameGate::FrameGate(size_t max_depth) : max_depth_(max_depth) {}

bool FrameGate::submit(const CapturedFrame& frame) {
  std::lock_guard<std::mutex> lk(mu_);
  if (max_depth_ == 0) return false;
  while (q_.size() >= max_depth_) {
    q_.pop_front();  // oldest = most obsolete
    ++dropped_;
  }
  q_.push_back(frame);
  return true;
}

bool FrameGate::pop(CapturedFrame& out) {
  std::lock_guard<std::mutex> lk(mu_);
  if (q_.empty()) return false;
  out = std::move(q_.front());
  q_.pop_front();
  return true;
}

void FrameGate::reset(size_t max_depth) {
  std::lock_guard<std::mutex> lk(mu_);
  q_.clear();
  max_depth_ = max_depth;
  dropped_ = 0;
}

void FrameGate::discard() {
  std::lock_guard<std::mutex> lk(mu_);
  q_.clear();
}

size_t FrameGate::depth() const {
  std::lock_guard<std::mutex> lk(mu_);
  return q_.size();
}

size_t FrameGate::maxDepth() const {
  std::lock_guard<std::mutex> lk(mu_);
  return max_depth_;
}

uint64_t FrameGate::droppedCount() const {
  std::lock_guard<std::mutex> lk(mu_);
  return dropped_;
}

}  // namespace remotewindows::core
