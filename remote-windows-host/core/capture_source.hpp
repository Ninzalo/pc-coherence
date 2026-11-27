
#ifndef RWH_CORE_CAPTURE_SOURCE_HPP
#define RWH_CORE_CAPTURE_SOURCE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace remotewindows::core {

// RW-H-011 (H-MEDIA, FR-03/FR-04 per-window capture): portable capture
// surface types + the source seam.
//
// PORTABLE: no Win32 / D3D includes. Native handles are opaque strings
// (the WindowRegistry's `hwnd` token); pixel payloads are plain CPU
// buffers. The Win32 adapter (win32/capture/, Windows.Graphics.Capture)
// implements ICaptureSurfaceSource; the portable pipeline and its tests
// build and run on any host.

// Pixel format of a CPU-delivered captured frame. WGC delivers
// B8G8R8A8 UNORM; the adapter converts row-pitch to a tight buffer.
enum class CaptureFrameFormat {
  Bgra8,  // 8 bits/channel BGRA, little-endian, 4 bytes/px, no row padding
};

// One captured frame (FR-03: "each independently rendered source window
// = independent capture session, window_id, video stream").
struct CapturedFrame {
  std::string window_id;    // durable remote identity (registry UUID)
  uint64_t seq = 0;         // monotonic per source
  uint64_t timestamp_us = 0;  // monotonic microseconds at capture time
  uint32_t width = 0;       // physical pixels (DPI-aware client area)
  uint32_t height = 0;
  CaptureFrameFormat format = CaptureFrameFormat::Bgra8;
  std::vector<uint8_t> pixels;  // tight w*h*4 buffer for Bgra8
};

// Typed capture errors (B1). No exceptions across the boundary.
enum class CaptureError {
  None,
  // The OS user consent for screen capture has not been granted for this
  // process. On Windows 11 22000+ this is GraphicsCaptureItem
  // .IsUserConsentGranted(); the consent dialog needs a HUMAN at the PC
  // (a user action), so the adapter reports this error instead of
  // attempting a capture that would block on the dialog.
  ConsentRequired,
  // The source window was destroyed / the capture session or item was
  // closed by the OS (WGC session end / item close). The session is
  // marked ended; the manager restarts (recreation path, IDR) while the
  // window is still tracked.
  SourceClosed,
  // The surface cannot be captured at all (e.g. protected content,
  // session-0 secure surfaces, or a non-independent surface).
  SourceUnavailable,
  InternalError,
};

// Frame sink for a capture surface: the per-window capture pipeline
// (pre-encode gate) consumes frames; source failures arrive here
// asynchronously (adapter thread). Implementations must be thread-safe.
class IFrameConsumer {
 public:
  virtual ~IFrameConsumer() = default;
  // Delivered on the source's capture thread. Must not block for long.
  virtual void onCapturedFrame(const CapturedFrame& frame) = 0;
  // Asynchronous source failure (e.g. WGC session ended, item closed,
  // or a start-time error surfaced late).
  virtual void onSourceError(const std::string& window_id, CaptureError error) = 0;
};

// A request to start capturing one surface.
struct CaptureSurfaceRequest {
  std::string window_id;   // durable remote identity (registry UUID)
  std::string hwnd;        // transient native handle (registry opaque token)
  uint32_t width_px = 0;   // expected client-area size, physical pixels
  uint32_t height_px = 0;
  uint32_t dpi_scale_percent = 100;
};

// Portable capture-source seam (FR-03). One adapter instance may run
// multiple per-window surfaces concurrently; each surface is fully
// independent (per-window WGC session + frame pool in the Win32
// adapter). A failing or closed surface MUST NOT affect other surfaces
// or the adapter itself (FR-04: "releasing capture resources when each
// individual source closes").
class ICaptureSurfaceSource {
 public:
  virtual ~ICaptureSurfaceSource() = default;
  // Start capturing `req.window_id`. On success, frames are delivered to
  // `consumer` on the adapter's thread(s). Typed error return:
  //   ConsentRequired   - user consent not granted (never blocked on it)
  //   SourceClosed      - source no longer exists
  //   SourceUnavailable - surface cannot be captured
  //   InternalError     - any other failure
  virtual CaptureError startSurface(const CaptureSurfaceRequest& req,
                                    IFrameConsumer& consumer) = 0;
  // Stop capturing the surface and release its resources. Safe to call
  // for a surface that is not (or no longer) running.
  virtual CaptureError stopSurface(const std::string& window_id) = 0;
  virtual bool isRunning(const std::string& window_id) const = 0;
};

}  // namespace remotewindows::core

#endif  // RWH_CORE_CAPTURE_SOURCE_HPP
