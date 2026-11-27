
#ifndef RWH_CORE_CAPTURE_SESSION_MANAGER_HPP
#define RWH_CORE_CAPTURE_SESSION_MANAGER_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/capture_encoder.hpp"
#include "core/capture_gate.hpp"
#include "core/capture_source.hpp"
#include "core/fps_policy.hpp"
#include "core/window_registry.hpp"

namespace remotewindows::core {

// Per-session encoded-frame sink (defined in the .cpp): updates the
// session metrics, then forwards to the app-layer IEncodedFrameConsumer.
struct CountingSink;

// RW-H-011 (H-MEDIA, FR-03/FR-04/FR-06 excerpt): the per-window
// capture-session pipeline.
//
// REGISTRY INTEGRATION (real WindowRegistry, core/window_registry.hpp):
// the registry is notification-RETURNING — applyEvent()/reconcile()
// yield std::vector<RegistryNotification>; it has no observer
// registration (that interface does not exist and the registry files
// are read-only for this task). The app layer (H-FOUND) therefore
// forwards every registry notification it receives to
// onRegistryNotification(s). The policy implemented on that stream:
//
//  * Announce / Reannounce — an IndependentTopLevel surface belonging
//    to an application session that is ALREADY SHARED (any role, ANY
//    pid: session correlation comes from the registry, ADR-0006) is
//    captured AUTOMATICALLY — no manual request (FR-03 auto-share,
//    FR-04: auxiliary windows need no separate share action).
//    ChildControl surfaces are never captured (their pixels are inside
//    the parent surface — no duplicate stream).
//  * Update (geometry: width/height/dpi scale changed) — the session is
//    RECREATED (stop + start with the new geometry, IDR on restart).
//    Position-only moves do NOT recreate (WGC tracks the window).
//    Rapid resize updates are coalesced (recreate_coalesce_ms).
//  * Update (minimized) — configurable policy: Pause (default) stops
//    the surface; restore restarts it with a fresh frame request + IDR.
//    LowFps keeps the surface running and throttles encoding to
//    minimize_low_fps via core::FpsGate (frames the gate rejects are
//    counted as dropped, never queued).
//  * Remove (closed / unavailable) — stops and releases ONLY that
//    session (FR-04: a dialog closing must not stop the parent stream).
//  * SourceClosed (adapter, async via IFrameConsumer) — the session is
//    marked Ended and the adapter surface is recreated (bounded
//    attempts), IDR on the restart path; other sessions are untouched.
//  * Parent retention: no session ever stops or recreates another
//    session. A dialog's whole lifecycle (auto-start, pause, close)
//    leaves the parent session running.
//
// ENCODING (RW-H-007 integration): gated frames are encoded by a
// PER-SESSION IVideoEncoder (created via IVideoEncoderFactory) into
// IEncodedFrameConsumer. IDR is requested at session start, after
// recreation, and after minimize-resume. The RW-H-007 media core has no
// H.264 encoder backend yet (ADR-0007: proposed) — see
// core/capture_encoder.hpp; the seam contract is what this pipeline
// uses and tests.
//
// THreading: every public method is thread-safe. The registry hook
// thread calls onRegistryNotification(s); adapter threads call the
// IFrameConsumer callbacks; the app thread calls share/unshare; metrics
// may be read at any time.

enum class MinimizePolicy {
  Pause,   // stop the surface while minimized (default)
  LowFps,  // keep the surface running, encode at minimize_low_fps
};

enum class CaptureSessionState {
  Inactive,  // no session for this window_id
  Running,   // surface running, frames being encoded
  Paused,    // minimized with the Pause policy: surface stopped
  Ended,     // source closed; pending registry-driven recreation
};

// Per-session metrics (FR-06: FPS/bitrate/queue-depth/dropped-frames
// tracking — the later STREAM_STATUS / PERFORMANCE_STATS surface reads
// these). Counters are cumulative for the window_id's lifetime in this
// manager; queue_depth / last_frame_ts are live.
struct CaptureSessionMetrics {
  CaptureSessionState state = CaptureSessionState::Inactive;
  uint32_t width_px = 0;
  uint32_t height_px = 0;
  uint32_t dpi_scale_percent = 100;
  bool minimized = false;
  bool auto_shared = false;    // started by the auto-share policy
  uint32_t fps_target = 0;     // active encode fps target (0 = unthrottled)
  uint64_t frames_received = 0;  // source frames accepted at the gate
  uint64_t frames_delivered = 0; // encoded frames handed to the sink
  uint64_t frames_dropped = 0;   // obsolete-evictions + throttle drops
  size_t queue_depth = 0;        // live
  size_t queue_max_depth = 0;    // fixed bound
  uint64_t last_frame_ts_us = 0; // last source frame timestamp
  uint64_t error_count = 0;      // source errors + encoder failures
  uint64_t keyframe_requests = 0;  // IDR requests issued (start/recreate/resume)
  uint64_t surface_starts = 0;   // adapter startSurface successes
  uint64_t last_recreate_ts_us = 0;
};

struct CaptureManagerConfig {
  // Fixed max depth of each session's pre-encode frame queue.
  size_t queue_depth = 8;
  // Minimized-window behavior (default: pause).
  MinimizePolicy minimize_policy = MinimizePolicy::Pause;
  // Encode fps while minimized under the LowFps policy.
  uint32_t minimize_low_fps = 5;
  // Static fps hint passed to encoder start (client-driven dynamic FPS
  // policy is FR-09 — a later task).
  uint32_t fps_hint = 30;
  // Ignore further geometry-change recreations for this long after the
  // last one (coalesce interactive resize; FR-06).
  uint32_t recreate_coalesce_ms = 250;
  // Max adapter-surface recreation attempts after a SourceClosed before
  // the session stays Ended (a later registry notification can retry).
  uint32_t max_recreate_attempts = 3;
};

class CaptureSessionManager : public IFrameConsumer {
 public:
  // `source` / `encoder_factory` / `encoded_sink` must outlive the
  // manager. The manager is the IFrameConsumer the source delivers to.
  CaptureSessionManager(ICaptureSurfaceSource& source,
                        IVideoEncoderFactory& encoder_factory,
                        IEncodedFrameConsumer& encoded_sink,
                        CaptureManagerConfig config = {});
  ~CaptureSessionManager();
  CaptureSessionManager(const CaptureSessionManager&) = delete;
  CaptureSessionManager& operator=(const CaptureSessionManager&) = delete;
  friend struct CountingSink;  // defined in the .cpp; updates metrics

  // ------------------------------------------------------ app layer
  // Start (or idempotently re-assert) the capture session for the
  // window described by its CURRENT registry record. The explicit path
  // is for the shared (anchor) window; auxiliary windows start via the
  // auto-share policy, not this call. Typed error from the adapter
  // (e.g. ConsentRequired) is returned and recorded.
  CaptureError shareWindow(const WindowRecord& record);
  // Stop and release ONLY this session. Typed error (None when the
  // session existed or already stopped).
  CaptureError unshareWindow(const std::string& window_id);

  // ------------------------------------------- registry stream (forwarded)
  // Apply one registry notification to the capture policy. Cheap; call
  // once per notification in delivery order (the same order the app
  // layer would emit WindowAnnounce/WindowUpdate/WindowRemove).
  void onRegistryNotification(const RegistryNotification& n);
  void onRegistryNotifications(const std::vector<RegistryNotification>& ns);

  // ----------------------------------------------------------- queries
  // True when a session for this window_id exists in ANY state
  // (Running/Paused/Ended).
  bool isShared(const std::string& window_id) const;
  CaptureSessionState stateOf(const std::string& window_id) const;
  CaptureSessionMetrics metrics(const std::string& window_id) const;
  std::vector<std::string> sharedWindowIds() const;
  size_t sharedCount() const;
  // Adapter surfaces currently running (leak check; == Running sessions).
  size_t runningSurfaceCount() const;

  // -------------------------------------------------- IFrameConsumer
  void onCapturedFrame(const CapturedFrame& frame) override;
  void onSourceError(const std::string& window_id, CaptureError error) override;

 private:
  struct Session;
  struct Impl;
  std::unique_ptr<Impl> impl_;

  // Start/recreate a session for `record` (worker + encoder + surface).
  // `auto_shared` marks the auto-share path. Returns the typed error.
  CaptureError startSession(const WindowRecord& record, bool auto_shared,
                            uint64_t now_us);
  // Stop the session's worker + encoder + surface. `next` is the state
  // to record. Holds no lock while calling adapter/encoder methods
  // (those may call back into the manager). Must be called with
  // teardown_mu held and no manager lock held.
  void stopSessionLocked(Session& s, CaptureSessionState next_state);
  // Join the worker and release the encoder. teardown_mu held; the
  // manager lock is NOT held (the worker's last callbacks take it).
  static void joinAndRelease(Session& s);
  // True when `rec`'s geometry differs from the session's (size or dpi
  // scale — position is not part of the capture surface).
  static bool geometryChanged(const Session& s, const WindowRecord& rec);
  // IEncodedFrameConsumer bookkeeping (unlock-before-callback).
  void noteEncoded(const std::string& window_id, const EncodedFrame& frame);
  void noteEncoderError(const std::string& window_id, const std::string& reason);
  uint64_t nowUs() const;
};

}  // namespace remotewindows::core

#endif  // RWH_CORE_CAPTURE_SESSION_MANAGER_HPP
