
#ifndef RWH_CORE_CAPTURE_ENCODER_HPP
#define RWH_CORE_CAPTURE_ENCODER_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/capture_source.hpp"

namespace remotewindows::core {

// RW-H-011 (H-MEDIA): the video-encoder seam the capture pipeline feeds.
//
// ENCODER BACKEND STATUS (verified 2026-11-27 against this tree):
// the RW-H-007 portable media core (`media/`) implements PROTOCOL §9
// framing, per-window stream keys, the stream registry (with its
// core::FpsGate + keyframe_due state) and the AES-256-GCM AEAD — but it
// contains NO H.264 encoder: ADR-0007's status is "proposed (video
// pipeline not yet integrated)". This seam is therefore the H-MEDIA-
// owned integration point the pipeline feeds; the first concrete
// backend (D3D11/Media-Foundation H.264, Win32-only) is a follow-up
// H-MEDIA task filed with the RW-H-011 report (cross-owner request).
// The pipeline's per-session + IDR semantics below are the contract that
// backend must honor.

// One encoded video frame handed to IEncodedFrameConsumer (the PROTOCOL
// §9 channel in a later app-layer task — NOT wired in RW-H-011).
// `payload` is one access unit of the encoded bitstream (H.264 per
// ADR-0007); the codec is an encoder-backend detail.
struct EncodedFrame {
  std::string window_id;
  uint64_t source_seq = 0;  // seq of the source CapturedFrame it covers
  uint32_t frame_seq = 0;   // per-session encoded frame index
  bool keyframe = false;    // true: access unit contains an IDR frame
  std::vector<uint8_t> payload;
};

// Sink for encoded frames (thread-safe; called on encoder/worker
// threads).
class IEncodedFrameConsumer {
 public:
  virtual ~IEncodedFrameConsumer() = default;
  virtual void onEncodedFrame(const EncodedFrame& frame) = 0;
  // `reason` is a short structured token (e.g. "start_failed",
  // "encode_failed") — never pixel/contents data.
  virtual void onEncoderError(const std::string& window_id,
                              const std::string& reason) = 0;
};

// Portable video encoder seam — ONE INSTANCE PER CAPTURE SESSION
// (FR-04: independent video stream per window). Created via
// IVideoEncoderFactory; the pipeline requests IDR (requestKeyframe)
// at session start, after every recreation (size change / source
// restart), and after minimize-resume.
class IVideoEncoder {
 public:
  virtual ~IVideoEncoder() = default;
  // Initialize for the surface geometry. `fps_hint` is a static hint
  // only (client-driven dynamic FPS policy is FR-09, a later task).
  // Returns false on failure (the pipeline treats the session as failed
  // and reports it).
  virtual bool start(uint32_t width, uint32_t height, uint32_t fps_hint) = 0;
  // Encode one source frame; 0..N encoded frames are pushed to the sink
  // given at construction (B-frames may reorder). Returns false on
  // failure (the pipeline counts the error and keeps the session alive).
  virtual bool encodeFrame(const CapturedFrame& frame) = 0;
  // Force an IDR in the next encoded output.
  virtual void requestKeyframe() = 0;
  // Flush and release codec state. Idempotent; safe after a failed start.
  virtual void stop() = 0;
};

// Factory for per-session encoder instances (dependency injection;
// tests use a deterministic fake, the app layer will use the real
// backend).
class IVideoEncoderFactory {
 public:
  virtual ~IVideoEncoderFactory() = default;
  virtual std::unique_ptr<IVideoEncoder> create(const std::string& window_id,
                                                IEncodedFrameConsumer& sink) = 0;
};

}  // namespace remotewindows::core

#endif  // RWH_CORE_CAPTURE_ENCODER_HPP
