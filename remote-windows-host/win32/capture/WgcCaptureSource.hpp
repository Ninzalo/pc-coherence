#ifndef RWH_WIN32_CAPTURE_WGCCAPTURESOURCE_HPP
#define RWH_WIN32_CAPTURE_WGCCAPTURESOURCE_HPP

// WIN32 ONLY (RW-H-011 Stage 2, H-MEDIA). Windows.Graphics.Capture
// implementation of the portable core::ICaptureSurfaceSource seam
// (core/capture_source.hpp). Compiles only on Windows; NOT RUN on macOS
// by design (root AGENTS.md: Win32 targets do not compile there).
//
// Doc-check 2026-11-27 — Microsoft Learn (learn.microsoft.com) plus
// the installed Windows SDK 10.0.26100.0 headers (cppwinrt
// v2.0.250303.5), verified on the build runner:
//   * Item from HWND: GraphicsCaptureItem::TryCreateFromWindowId
//     (IGraphicsCaptureItemStatics2, Win10 2004 (19041)+) with the
//     WindowId from DwmGetWindowAttribute(DWMWA_WINDOW_ID). The
//     IGraphicsCaptureItemInterop::CreateForWindow path (Win10 1903+)
//     is NOT used: SDK 26100's windows.graphics.capture.interop.h
//     declares only the interface IID (3628E81B-3CAC-4C60-B7F4-
//     23CE0E0C3356) — no CLSID_GraphicsCaptureItemInterop — so the
//     interop factory CLSID is unverifiable offline; TryCreateFrom-
//     WindowId is the documented WinRT path and exists in this SDK.
//   * Direct3D11CaptureFramePool::CreateFreeThreaded(IDirect3DDevice,
//     DirectXPixelFormat, Int32 maxFrames, SizeInt32 maxDimension) —
//     Win10 1809 (17763)+; FrameArrived is raised on the pool's internal
//     worker thread (no message pump / DispatcherQueue needed).
//   * GraphicsCaptureSession: CreateCaptureSession(item),
//     IsBorderRequired (1809+), StartCapture(), Close(). NOTE: in
//     SDK 26100's C++/WinRT projection the session Closed event
//     (GraphicsCaptureSessionClosedReason) and the item Closed event
//     are NOT exposed — a vanished source is detected via consecutive
//     TryGetNextFrame()==nullptr (30×) → SourceClosed; the registry's
//     destroy news remains the primary teardown driver.
//   * Frames: Direct3D11CaptureFrame::Surface() (this SDK's projection
//     name; ABI get_Surface) returns
//     Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface whose
//     ABI is ID3D11Texture2D (winrt::get_abi); correct pixel
//     dimensions at any DPI (physical pixels; the request geometry is
//     physical client size × window DPI scale).
//   * Consent (mandatory gate): GraphicsCaptureAccess.RequestAccessAsync(
//     GraphicsCaptureAccessKind::Programmatic) →
//     IAsyncOperation<AppCapabilityAccessStatus> — Win10 2104 (20348)+.
//     The Borderless kind needs the packaged
//     "graphicsCaptureWithoutBorder" capability (N/A for this desktop
//     host). NOTE: "IsUserConsentGranted" is NOT a documented API (both
//     candidate Learn pages 404 on 2026-11-27); RequestAccessAsync is
//     the documented consent path. The request may present a one-time OS
//     consent prompt (a USER ACTION — a human at the PC), so the adapter
//     polls the operation status only a bounded 2 s and, if the answer
//     is not Granted, returns ConsentRequired instead of starting a
//     capture. It NEVER waits indefinitely on the prompt.
//   * Minimized/occluded: WGC simply stops delivering frames (no
//     event); the portable manager drives the pause/resume policy and
//     the resume path restarts the surface (fresh frames + IDR).
//
// Pixel path (documented choice): each WGC D3D11 frame (B8G8R8A8 UNORM)
// is copied through a bounded D3D11 STAGING texture (CPU_ACCESS_READ)
// to a tight CPU BGRA8 buffer (CapturedFrame::pixels). The portable
// encoder seam (core::IVideoEncoder) takes CPU pixels; the media core
// has NO D3D11 encoder input path yet (no H.264 encoder exists in the
// tree — ADR-0007 video pipeline not yet integrated), so a zero-copy
// GPU→encoder path is impossible until the real encoder backend lands
// (cross-owner follow-up, filed in the RW-H-011 report). No
// full-screen GPU readback is done — only the per-window frame, and
// only the rows of the actual frame size (row pitch handled).
//
// hwnd token: the portable core carries the registry's opaque HWND
// token; the Win32 token format is "0x%llX" (win32/windows/
// Win32WindowSource.cpp HwndKeyImpl) and is parsed back here.

#ifdef _WIN32

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/capture_source.hpp"

namespace remotewindows::win32 {

// Windows.Graphics.Capture adapter for the portable capture pipeline.
// One instance may run any number of per-window surfaces concurrently;
// each surface owns its own GraphicsCaptureItem + free-threaded D3D11
// frame pool + capture session + worker thread (FR-03/FR-04
// independence: one dead or slow surface never affects the others).
//
// Thread-safety: startSurface/stopSurface/isRunning may be called from
// any thread; frames and error reports arrive on WGC-internal threads.
// The class is NOT copyable and must outlive the pipeline that uses it.
class WgcCaptureSource : public core::ICaptureSurfaceSource {
 public:
  WgcCaptureSource();
  ~WgcCaptureSource() override;
  WgcCaptureSource(const WgcCaptureSource&) = delete;
  WgcCaptureSource& operator=(const WgcCaptureSource&) = delete;

  // core::ICaptureSurfaceSource
  core::CaptureError startSurface(const core::CaptureSurfaceRequest& req,
                                  core::IFrameConsumer& consumer) override;
  core::CaptureError stopSurface(const std::string& window_id) override;
  bool isRunning(const std::string& window_id) const override;

  // Number of live surfaces (leak check in tests / host).
  size_t surfaceCount() const;

 private:
  struct Surface;  // per-window WGC state (defined in the .cpp)
  struct Impl;
  std::unique_ptr<Impl> impl_;

  // Process-level consent gate: Granted is cached (stable for the
  // process lifetime); non-Granted results are re-checked on each start
  // attempt (the user may grant consent later). Bounded wait — the
  // OS prompt is a user action, never a hang.
  core::CaptureError consentGate();
  // Worker-thread body: create the WGC objects, start the session, wait
  // for stop, tear down, self-erase from the surface map.
  void surfaceWorker(Surface& s);
  // WGC setup on the worker thread (item/device/pool/session/start).
  core::CaptureError startOnThread(Surface& s);
  // Report a source failure to the consumer at most once per surface.
  void reportOnce(Surface& s, core::CaptureError error);
};

}  // namespace remotewindows::win32

#endif  // _WIN32
#endif  // RWH_WIN32_CAPTURE_WGCCAPTURESOURCE_HPP
