// RW-H-011 Stage 2 (H-MEDIA, plan FR-03): Windows.Graphics.Capture
// implementation of core::ICaptureSurfaceSource. WIN32 ONLY — see the
// header for the doc-check notes (2026-11-27) and the documented pixel
// path (bounded per-window D3D11 staging copy → tight CPU BGRA8).
//
// SDK 10.0.26100.0 / cppwinrt v2.0.250303.5 API notes (verified
// against the installed headers AND probe-verified on the runner OS
// build, 2026-11-27):
//   * Item creation: IGraphicsCaptureItemInterop::CreateForWindow
//     (HWND-based; interface uuid 3628E81B-3CAC-4C60-B7F4-
//     23CE0E0C3356, um/Windows.Graphics.Capture.Interop.h; obtained
//     via winrt::get_activation_factory<GraphicsCaptureItem,
//     IGraphicsCaptureItemInterop>). The DWM-window-id route
//     (DwmGetWindowAttribute + TryCreateFromWindowId) is DEAD on the
//     26100-era OS build: no DWM attribute in 1..64 — probed against
//     Progman, other composited windows, and own windows — returns a
//     nonzero window id (S_OK + 0), so TryCreateFromWindowId can only
//     get WindowId{0} and fails. CreateForWindow needs no DWM id at
//     all and was probe-verified end-to-end (item + frames).
//   * WGC device view: in SDK 26100 Windows::Graphics::DirectX::
//     Direct3D11::IDirect3DDevice is a projected IInspectable-based
//     type (NOVTABLE abi, Trim()), NOT a raw COM vtable — QI of the
//     D3D11 device by its GUID (A37624AB-8D5F-4650-9D3E-
//     9EAE3D9BC670) returns E_NOINTERFACE (probe-verified). The
//     documented route: QI IDXGIDevice, then the
//     CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice*,
//     IInspectable**) factory (um/
//     Windows.Graphics.DirectX.Direct3D11.Interop.h) returns the
//     projected IInspectable; construct the projected type from the
//     ABI pointer via take_ownership_from_abi.
//   * Frame surface: Direct3D11CaptureFrame::Surface() (this SDK's
//     projection name; ABI get_Surface) →
//     Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface — in
//     SDK 26100 a projected IInspectable-based interface exposing
//     Description() (Direct3DSurfaceDescription). The 26100 rework
//     REMOVED the raw COM IDirect3DSurface (GetBuffer) from the um
//     interop header; the actual D3D11 texture comes from
//     IDirect3DDxgiInterfaceAccess::GetInterface (uuid
//     A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1, um/
//     windows.graphics.directx.direct3d11.interop.h) — the documented
//     WGC pixel path (probe-verified: 320x200 B8G8R8A8 frame read
//     back in the interactive session).
//   * Frame pool: Direct3D11CaptureFramePool::CreateFreeThreaded takes
//     Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice (the
//     projected type obtained via the factory above). There is NO
//     IsCreationDenied member in this SDK and NO session/item Closed
//     events in the projection — a vanished/occluded source is
//     detected via consecutive TryGetNextFrame()==nullptr (registry
//     destroy news is the primary teardown driver; this is the safety
//     net).
//   * Events: winrt::event_token + token overloads (attach =
//     FrameArrived(handler), detach = FrameArrived(token)); there is no
//     winrt::event_registration in this SDK's cppwinrt.
//   * Errors: winrt::hresult_error::code() (this cppwinrt has no
//     hresult_error::to_hresult member).
//   * SDK 26100 D3D11 rework (verified 2026-11-27 against the
//     installed um/d3d11.h): ID3D11DeviceContext::CopyResource (and
//     peers like UpdateSubresource) now return void and throw on
//     failure — HRESULT checks do NOT compile (C2440). The frame
//     handler catches, counts (copy_failures) and skips the frame.
//   * Consent: GraphicsCaptureAccess::RequestAccessAsync(Programmatic)
//     → IAsyncOperation<AppCapabilityAccessStatus>; polled via
//     IAsyncInfo::Status() with a bounded 2 s deadline (the OS prompt
//     is a USER ACTION — never a hang), then GetResults().

#include "win32/capture/WgcCaptureSource.hpp"

#include <chrono>
#include <cinttypes>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

// Win32 environment (pins Win10, undefs colliding SDK macros) BEFORE
// the SDK / WinRT headers.
#include "app/Win32Env.hpp"

#include <d3d11.h>
// Raw COM interop header (SDK 26100): declares
// IDirect3DDxgiInterfaceAccess (uuid A9B3D012-3DF2-4EE3-B8D1-
// 8695F457D3C1) — the documented way to obtain the DXGI/D3D11
// interfaces from a WGC frame surface — and the
// CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice*, IInspectable**)
// factory that yields the projected WGC device view (the 26100 rework
// removed the raw COM IDirect3DDevice/IDirect3DSurface interfaces;
// see the notes above).
#include <windows.graphics.directx.direct3d11.interop.h>
// HWND-based capture-item interop (SDK 26100):
// IGraphicsCaptureItemInterop (uuid 3628E81B-3CAC-4C60-B7F4-
// 23CE0E0C3356) — the working item-creation path on this OS build
// (the DWM window-id route is dead; see the notes above).
#include <windows.graphics.capture.interop.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

namespace {

using winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureAccess;
using winrt::Windows::Graphics::Capture::GraphicsCaptureAccessKind;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::SizeInt32;
using winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus;

// Consecutive null TryGetNextFrame results before the surface reports
// SourceClosed (creation denied or item gone; no IsCreationDenied /
// Closed events exist in this SDK's projection).
constexpr uint32_t kNullFrameThreshold = 30;
constexpr uint32_t kConsentPollMs = 50;
constexpr std::chrono::seconds kConsentDeadline{2};

uint64_t monotonicUs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// RW-H-011b: structured diagnostic log (stderr). Structured fields ONLY:
// stage / window_id / typed error token / counts / sizes / timestamps —
// never pixels or window contents. A few lines per surface lifecycle
// (start / stop / error); the per-stage counter table is read via
// frameStats() (tests / host).
void diagLog(const char* stage, const char* window_id, const char* extra,
             uint64_t ts_us) {
  std::fprintf(stderr, "[wgc] %s window_id=%s %s ts_us=%llu\n", stage,
               window_id, extra, static_cast<unsigned long long>(ts_us));
  std::fflush(stderr);
}

// (Global/anonymous scope: qualify the core namespace — `core` alone is
// not visible outside remotewindows::win32.)
const char* errorToken(remotewindows::core::CaptureError e) {
  switch (e) {
    case remotewindows::core::CaptureError::None: return "none";
    case remotewindows::core::CaptureError::ConsentRequired:
      return "consent_required";
    case remotewindows::core::CaptureError::SourceClosed:
      return "source_closed";
    case remotewindows::core::CaptureError::SourceUnavailable:
      return "source_unavailable";
    case remotewindows::core::CaptureError::InternalError:
      return "internal_error";
  }
  return "unknown";
}

// The Win32 HWND token is "0x%llX" (win32/windows/Win32WindowSource.cpp
// HwndKeyImpl); parse it back. Unparseable tokens → nullptr.
HWND ParseHwndToken(const std::string& token) {
  if (token.size() < 4 || token.rfind("0x", 0) != 0) return nullptr;
  errno = 0;
  unsigned long long v = std::strtoull(token.c_str() + 2, nullptr, 16);
  if (errno != 0 || v == 0) return nullptr;
  return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(v));
}

}  // namespace

namespace remotewindows::win32 {

// ------------------------------------------------------------ Surface

struct WgcCaptureSource::Surface {
  std::string window_id;
  HWND hwnd = nullptr;
  uint32_t max_w = 0;  // pool max dimension (request geometry)
  uint32_t max_h = 0;
  core::IFrameConsumer* consumer = nullptr;

  // WGC objects. DECLARATION ORDER IS THE DESTRUCTION CONTRACT
  // (members destroy in reverse order): the frame event token is
  // revoked explicitly by the worker BEFORE session/pool are closed,
  // the session must die before its pool, and device/ctx/staging must
  // outlive any in-flight frame copy.
  winrt::event_token frame_token{};
  GraphicsCaptureSession session = nullptr;
  Direct3D11CaptureFramePool pool = nullptr;
  GraphicsCaptureItem item = nullptr;
  winrt::com_ptr<ID3D11Texture2D> staging;
  winrt::com_ptr<ID3D11DeviceContext> ctx;
  winrt::com_ptr<ID3D11Device> device;
  uint32_t staging_w = 0;
  uint32_t staging_h = 0;

  std::atomic<uint64_t> seq{0};
  std::atomic<uint32_t> null_frames{0};
  // RW-H-011b per-stage frame-path counters (diagnostics).
  std::atomic<uint64_t> frames_arrived{0};
  std::atomic<uint64_t> frames_wait_returned{0};
  std::atomic<uint64_t> size_skips{0};
  std::atomic<uint64_t> qi_failures{0};
  std::atomic<uint64_t> copy_failures{0};
  std::atomic<uint64_t> emitted{0};
  std::atomic<uint64_t> handler_errors{0};
  std::atomic<bool> stopping{false};
  std::atomic<bool> reported{false};
  std::atomic<bool> start_done{false};
  core::CaptureError start_err{core::CaptureError::InternalError};

  std::mutex start_mu;
  std::condition_variable start_cv;
  std::mutex stop_mu;
  std::condition_variable stop_cv;
  std::mutex frame_mu;  // serializes in-flight frame work vs teardown

  std::thread worker;

  void requestStop() {
    stopping.store(true);
    stop_cv.notify_all();
  }
};

struct WgcCaptureSource::Impl {
  std::mutex mu;
  std::map<std::string, std::unique_ptr<Surface>> surfaces;
  std::atomic<bool> consent_granted{false};  // process-level cache
};

WgcCaptureSource::WgcCaptureSource() : impl_(std::make_unique<Impl>()) {}

WgcCaptureSource::~WgcCaptureSource() {
  // Stop every live surface (joins workers, releases WGC objects).
  std::vector<std::unique_ptr<Surface>> victims;
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    for (auto& [id, s] : impl_->surfaces) {
      victims.push_back(std::move(s));
      s->requestStop();
    }
    impl_->surfaces.clear();
  }
  for (auto& v : victims) v->worker.join();
}

// ---------------------------------------------------------- consent

core::CaptureError WgcCaptureSource::consentGate() {
  if (impl_->consent_granted.load()) return core::CaptureError::None;
  // WinRT must be initialized on the calling thread (MTA; idempotent).
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  try {
    auto op =
        GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::
                                                      Programmatic);
    // Bounded wait: the OS may present a one-time consent prompt — a
    // USER ACTION (a human at the PC). Never block on it; an
    // unanswered prompt is "not granted yet". Poll IAsyncInfo::Status
    // (no Completed handler needed in this cppwinrt's projection).
    const auto deadline =
        std::chrono::steady_clock::now() + kConsentDeadline;
    bool completed = false;
    for (;;) {
      // Fully qualified: the UMP include chain
      // (windows.graphics.capture.interop.h -> windows.ui.composition.h)
      // pulls in the 26100 legacy winrt projection headers
      // (winrt/AsyncInfo.h), which using-declare ABI::Windows::Foundation
      // ::AsyncStatus at global scope — a file-scope using declaration of
      // the winrt type made the unqualified name ambiguous (C2872).
      completed = op.Status() ==
                  winrt::Windows::Foundation::AsyncStatus::Completed;
      if (completed || std::chrono::steady_clock::now() >= deadline)
        break;
      std::this_thread::sleep_for(
          std::chrono::milliseconds(kConsentPollMs));
    }
    // SDK 26100's reworked enum (verified on the runner): values are
    // DeniedBySystem=0, NotDeclaredByApp=1, DeniedByUser=2,
    // UserPromptRequired=3, Allowed=4 — only Allowed means "granted".
    if (completed && op.GetResults() == AppCapabilityAccessStatus::Allowed) {
      impl_->consent_granted.store(true);  // stable for the process
      return core::CaptureError::None;
    }
    return core::CaptureError::ConsentRequired;
  } catch (...) {
    // API unavailable (pre-2104 build) or the async request itself
    // failed: the consent state is indeterminate — do NOT start a
    // capture that could block on a consent dialog.
    return core::CaptureError::ConsentRequired;
  }
}

// ------------------------------------------------- worker-thread body

core::CaptureError WgcCaptureSource::startOnThread(Surface& s) {
  using core::CaptureError;
  try {
    // 1. Item for the HWND via the HWND-based interop factory.
    //    (The DWM-window-id route is dead on the 26100-era OS build —
    //    see the file-header notes; CreateForWindow needs no DWM id.)
    GraphicsCaptureItem item = nullptr;
    {
      winrt::com_ptr<IGraphicsCaptureItemInterop> item_factory;
      try {
        item_factory = winrt::get_activation_factory<
            winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
            IGraphicsCaptureItemInterop>();
      } catch (...) {
        item_factory = nullptr;
      }
      void* raw_item = nullptr;
      HRESULT chr = E_NOINTERFACE;
      if (item_factory) {
        chr = item_factory->CreateForWindow(
            s.hwnd,
            winrt::guid_of<
                winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
            &raw_item);
      }
      if (SUCCEEDED(chr) && raw_item) {
        item =
            GraphicsCaptureItem(raw_item, winrt::take_ownership_from_abi);
      }
    }
    if (!item) {
      // The window vanished between the IsWindow check and now, the
      // interop factory is unavailable, or the surface cannot be
      // captured at all.
      return IsWindow(s.hwnd) ? CaptureError::SourceUnavailable
                              : CaptureError::SourceClosed;
    }
    s.item = item;

    // 2. D3D11 device (hardware, WARP fallback) + the WGC device view.
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, s.device.put(), nullptr, s.ctx.put());
    if (FAILED(hr) || !s.device || !s.ctx) {
      hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                             nullptr, 0, D3D11_SDK_VERSION, s.device.put(),
                             nullptr, s.ctx.put());
    }
    if (FAILED(hr) || !s.device || !s.ctx) return CaptureError::InternalError;

    // SDK 26100 (2026 rework, probe-verified): the projected
    // IDirect3DDevice is an IInspectable-based projected type, NOT a
    // raw COM vtable — QI of the D3D11 device by its GUID returns
    // E_NOINTERFACE. The documented route: QI IDXGIDevice, then the
    // CreateDirect3D11DeviceFromDXGIDevice factory (um/
    // Windows.Graphics.DirectX.Direct3D11.Interop.h) yields the
    // projected IInspectable; construct the projected type via its
    // take_ownership_from_abi constructor (the pool takes its own
    // reference).
    IDXGIDevice* dxgi_raw = nullptr;
    hr = s.device->QueryInterface(IID_IDXGIDevice,
                                  reinterpret_cast<void**>(&dxgi_raw));
    winrt::com_ptr<IDXGIDevice> dxgi_device;
    dxgi_device.attach(dxgi_raw);  // takes ownership of the QI reference
    if (FAILED(hr) || !dxgi_device) return CaptureError::InternalError;
    IInspectable* wgc_device_raw = nullptr;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.get(),
                                              &wgc_device_raw);
    if (FAILED(hr) || !wgc_device_raw) return CaptureError::InternalError;
    IDirect3DDevice device_view(
        wgc_device_raw, winrt::take_ownership_from_abi);

    // 3. Free-threaded frame pool (Win10 1809+): FrameArrived fires on
    //    the pool's internal worker thread — no message pump needed.
    SizeInt32 maxDim{static_cast<int32_t>(s.max_w),
                     static_cast<int32_t>(s.max_h)};
    s.pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        device_view, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, maxDim);
    if (!s.pool) return CaptureError::InternalError;

    // 4. Session (no capture border).
    s.session = s.pool.CreateCaptureSession(s.item);
    s.session.IsBorderRequired(false);

    // 5. Frame event. The handler runs on a WGC-internal thread; it
    //    NEVER takes the adapter map lock (it may call back into the
    //    manager, which takes the manager lock — the adapter map lock
    //    is only ever taken by start/stop/worker threads).
    s.frame_token = s.pool.FrameArrived([this, &s](
                                            const Direct3D11CaptureFramePool& pool,
                                            winrt::Windows::Foundation::
                                                IInspectable const&) {
      s.frames_arrived.fetch_add(1);
      if (s.stopping.load()) return;
      Direct3D11CaptureFrame frame = nullptr;
      try {
        frame = pool.TryGetNextFrame();
      } catch (...) {
        frame = nullptr;
      }
      if (!frame) {
        // Consecutive null frames: creation denied (e.g. consent
        // revoked) or the item is gone. No IsCreationDenied/Closed
        // events exist in this SDK's projection — the threshold is the
        // detection (the registry's destroy news is the primary
        // teardown driver; this is the safety net).
        if (s.null_frames.fetch_add(1) + 1 >= kNullFrameThreshold)
          reportOnce(s, CaptureError::SourceClosed);
        return;
      }
      s.null_frames.store(0);
      s.frames_wait_returned.fetch_add(1);
      try {
        std::lock_guard<std::mutex> lk(s.frame_mu);
        if (s.stopping.load() || !s.consumer) return;
        auto const dsurface = frame.Surface();
        // This SDK's IDirect3DSurface projection exposes only
        // Description() (Direct3DSurfaceDescription: Width/Height/
        // Format/MultisampleDescription).
        auto const desc = dsurface.Description();
        const int32_t w = desc.Width;
        const int32_t h = desc.Height;
        if (w <= 0 || h <= 0) {
          s.size_skips.fetch_add(1);
          return;
        }
        if (static_cast<uint32_t>(w) > s.max_w ||
            static_cast<uint32_t>(h) > s.max_h) {
          // The window outgrew the pool max dimension: skip the frame
          // (the registry's geometry update makes the manager
          // recreate the session with the new size).
          s.size_skips.fetch_add(1);
          return;
        }
        // SDK 26100 (2026 rework, runner-verified): the projected
        // surface is an IInspectable wrapper — its ABI pointer is NOT
        // the D3D11 texture's vtable, and the raw COM
        // IDirect3DSurface (GetBuffer) is gone from the SDK. The
        // documented WGC pixel path: QI the surface for
        // IDirect3DDxgiInterfaceAccess, then GetInterface
        // (IID_ID3D11Texture2D). get_abi yields the raw COM object;
        // QI through the raw COM IInspectable (the um interop header
        // pulls in the global ::IInspectable — the projected winrt one
        // is a different, non-raw type).
        auto* surface_inspectable =
            static_cast<IInspectable*>(winrt::get_abi(dsurface));
        ::Windows::Graphics::DirectX::Direct3D11::
            IDirect3DDxgiInterfaceAccess* dxgi_access = nullptr;
        if (FAILED(surface_inspectable->QueryInterface(
                __uuidof(
                    ::Windows::Graphics::DirectX::Direct3D11::
                        IDirect3DDxgiInterfaceAccess),
                reinterpret_cast<void**>(&dxgi_access))) ||
            !dxgi_access) {
          s.qi_failures.fetch_add(1);
          return;
        }
        ID3D11Texture2D* texture = nullptr;
        const HRESULT access_hr = dxgi_access->GetInterface(
            __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
        dxgi_access->Release();
        if (FAILED(access_hr) || !texture) {
          s.qi_failures.fetch_add(1);
          return;
        }
        // (Re)create the staging texture for the current frame size.
        if (!s.staging || s.staging_w != static_cast<uint32_t>(w) ||
            s.staging_h != static_cast<uint32_t>(h)) {
          D3D11_TEXTURE2D_DESC desc{};
          desc.Width = static_cast<UINT>(w);
          desc.Height = static_cast<UINT>(h);
          desc.MipLevels = 1;
          desc.ArraySize = 1;
          desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
          desc.SampleDesc.Count = 1;
          desc.Usage = D3D11_USAGE_STAGING;
          desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
          if (FAILED(s.device->CreateTexture2D(&desc, nullptr,
                                               s.staging.put()))) {
            s.staging = nullptr;
            s.copy_failures.fetch_add(1);
            return;
          }
          s.staging_w = static_cast<uint32_t>(w);
          s.staging_h = static_cast<uint32_t>(h);
        }
        // Bounded copy: this window's frame only (no full-screen
        // readback), row pitch handled. SDK 26100 (verified 2026-11-27
        // against the installed d3d11.h): CopyResource now returns
        // void and THROWS on failure — catch, count, skip the frame
        // (same policy as the QI/Map failure points below).
        try {
          s.ctx->CopyResource(s.staging.get(), texture);
        } catch (...) {
          texture->Release();
          s.copy_failures.fetch_add(1);
          return;
        }
        texture->Release();  // GetInterface reference; copy is done
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(s.ctx->Map(s.staging.get(), 0, D3D11_MAP_READ, 0,
                              &mapped))) {
          s.copy_failures.fetch_add(1);
          return;
        }
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4u);
        const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
        for (int32_t y = 0; y < h; ++y) {
          std::memcpy(pixels.data() + static_cast<size_t>(y) * w * 4u,
                      src + static_cast<size_t>(y) * mapped.RowPitch,
                      static_cast<size_t>(w) * 4u);
        }
        s.ctx->Unmap(s.staging.get(), 0);

        core::CapturedFrame cf;
        cf.window_id = s.window_id;
        cf.seq = s.seq.fetch_add(1);
        cf.timestamp_us = monotonicUs();
        cf.width = static_cast<uint32_t>(w);
        cf.height = static_cast<uint32_t>(h);
        cf.format = core::CaptureFrameFormat::Bgra8;
        cf.pixels = std::move(pixels);
        s.emitted.fetch_add(1);
        s.consumer->onCapturedFrame(cf);
      } catch (...) {
        // Any WinRT/D3D failure mid-frame: stop the surface and report
        // once (the manager decides recreation vs permanent end).
        s.handler_errors.fetch_add(1);
        s.stopping.store(true);
        s.stop_cv.notify_all();
        reportOnce(s, CaptureError::SourceClosed);
      }
    });

    // 6. Start. E_ACCESSDENIED here = consent not actually granted
    //    (revoked mid-flight or the gate raced) → ConsentRequired.
    s.session.StartCapture();
    return CaptureError::None;
  } catch (winrt::hresult_error const& e) {
    if (e.code() == winrt::hresult(E_ACCESSDENIED))
      return core::CaptureError::ConsentRequired;
    return core::CaptureError::SourceUnavailable;
  } catch (...) {
    return core::CaptureError::InternalError;
  }
}

void WgcCaptureSource::reportOnce(Surface& s, core::CaptureError error) {
  if (s.reported.exchange(true)) return;
  if (s.stopping.load() && s.start_done.load()) {
    // Clean user-initiated stop won the race: the manager already
    // unshared the session; reporting would only count an error.
    return;
  }
  // Structured error line (ids / typed token / timestamp only).
  diagLog("error", s.window_id.c_str(),
          (std::string("err=") + errorToken(error)).c_str(), monotonicUs());
  if (s.consumer) s.consumer->onSourceError(s.window_id, error);
}

void WgcCaptureSource::surfaceWorker(Surface& s) {
  core::CaptureError err;
  bool init_ok = true;
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
  } catch (...) {
    // WinRT/COM unavailable on this thread: a typed error instead of
    // an uncaught exception (std::terminate would kill the whole
    // process — the worker must never take the app down).
    init_ok = false;
  }
  err = init_ok ? startOnThread(s) : core::CaptureError::InternalError;
  {
    std::lock_guard<std::mutex> lk(s.start_mu);
    s.start_err = err;
    s.start_done = true;
  }
  s.start_cv.notify_all();

  // Structured start line (ids / size / typed token / timestamp only).
  std::string extra =
      "err=" + std::string(errorToken(err)) +
      " size=" + std::to_string(s.max_w) + "x" + std::to_string(s.max_h);
  diagLog("start", s.window_id.c_str(), extra.c_str(), monotonicUs());

  if (err == core::CaptureError::None && !s.stopping.load()) {
    std::unique_lock<std::mutex> lk(s.stop_mu);
    s.stop_cv.wait(lk, [&s] { return s.stopping.load(); });
  }

  // Teardown — see the Surface declaration-order contract. Wait for an
  // in-flight frame copy (frame_mu), then revoke the frame event, then
  // close session + pool in order, then release the COM objects.
  {
    std::lock_guard<std::mutex> lk(s.frame_mu);
  }
  if (s.frame_token) {
    try {
      s.pool.FrameArrived(s.frame_token);  // token overload = detach
    } catch (...) {
    }
    s.frame_token = {};
  }
  if (s.session) {
    try {
      s.session.Close();
    } catch (...) {
    }
    s.session = nullptr;
  }
  if (s.pool) {
    try {
      s.pool.Close();
    } catch (...) {
    }
    s.pool = nullptr;
  }
  s.item = nullptr;
  s.staging = nullptr;
  s.ctx = nullptr;
  s.device = nullptr;
  s.consumer = nullptr;
  // Structured stop line: the full per-stage counter table (counts only
  // — the first zero-count stage locates where frame flow stopped).
  {
    char line[256];
    std::snprintf(line, sizeof(line),
                  "err=%s size=%lux%lu arrived=%llu wait_ok=%llu null=%lu "
                  "size_skip=%llu qi_fail=%llu copy_fail=%llu emitted=%llu "
                  "handler_err=%llu",
                  errorToken(s.start_err),
                  static_cast<unsigned long>(s.max_w),
                  static_cast<unsigned long>(s.max_h),
                  static_cast<unsigned long long>(s.frames_arrived.load()),
                  static_cast<unsigned long long>(s.frames_wait_returned.load()),
                  static_cast<unsigned long>(s.null_frames.load()),
                  static_cast<unsigned long long>(s.size_skips.load()),
                  static_cast<unsigned long long>(s.qi_failures.load()),
                  static_cast<unsigned long long>(s.copy_failures.load()),
                  static_cast<unsigned long long>(s.emitted.load()),
                  static_cast<unsigned long long>(s.handler_errors.load()));
    diagLog("stop", s.window_id.c_str(), line, monotonicUs());
  }
  ::CoUninitialize();
  // Self-erase: only if this surface is still the live entry (an
  // explicit stop/start may have already moved it out).
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->surfaces.find(s.window_id);
    if (it != impl_->surfaces.end() && it->second.get() == &s) {
      impl_->surfaces.erase(it);
    }
  }
}

// ------------------------------------------------------ public API

core::CaptureError WgcCaptureSource::startSurface(
    const core::CaptureSurfaceRequest& req, core::IFrameConsumer& consumer) {
  using core::CaptureError;
  const CaptureError consent = consentGate();
  if (consent != CaptureError::None) return consent;
  const HWND hwnd = ParseHwndToken(req.hwnd);
  if (!hwnd) return CaptureError::SourceUnavailable;
  if (!IsWindow(hwnd)) return CaptureError::SourceClosed;

  auto surface = std::make_unique<Surface>();
  surface->window_id = req.window_id;
  surface->hwnd = hwnd;
  surface->max_w = req.width_px;
  surface->max_h = req.height_px;
  surface->consumer = &consumer;
  Surface* raw = surface.get();

  std::unique_ptr<Surface> retired;
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->surfaces.find(req.window_id);
    if (it != impl_->surfaces.end()) {
      retired = std::move(it->second);
      impl_->surfaces.erase(it);
      retired->requestStop();
    }
    impl_->surfaces.emplace(req.window_id, std::move(surface));
  }
  raw->worker = std::thread([this, raw] { surfaceWorker(*raw); });
  if (retired) retired->worker.join();  // fully gone before the new one

  // Bounded wait for the start result (item/device/pool/session +
  // StartCapture; normally well under a second).
  std::unique_lock<std::mutex> lk(raw->start_mu);
  const bool done = raw->start_cv.wait_for(
      lk, std::chrono::seconds(10),
      [&raw] { return raw->start_done.load(); });
  if (!done) {
    raw->requestStop();
    lk.unlock();
    raw->worker.join();
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->surfaces.find(raw->window_id);
    if (it != impl_->surfaces.end() && it->second.get() == raw) {
      impl_->surfaces.erase(it);
    }
    return CaptureError::InternalError;
  }
  const CaptureError err = raw->start_err;
  lk.unlock();
  if (err != CaptureError::None) {
    raw->requestStop();
    raw->worker.join();
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->surfaces.find(raw->window_id);
    if (it != impl_->surfaces.end() && it->second.get() == raw) {
      impl_->surfaces.erase(it);
    }
    return err;
  }
  return CaptureError::None;
}

core::CaptureError WgcCaptureSource::stopSurface(
    const std::string& window_id) {
  std::unique_ptr<Surface> victim;
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->surfaces.find(window_id);
    if (it == impl_->surfaces.end()) return core::CaptureError::None;
    victim = std::move(it->second);
    impl_->surfaces.erase(it);
  }
  victim->requestStop();
  victim->worker.join();
  return core::CaptureError::None;
}

bool WgcCaptureSource::isRunning(const std::string& window_id) const {
  std::lock_guard<std::mutex> mu(impl_->mu);
  auto it = impl_->surfaces.find(window_id);
  return it != impl_->surfaces.end() && it->second->start_done.load() &&
         !it->second->stopping.load() &&
         it->second->start_err == core::CaptureError::None;
}

size_t WgcCaptureSource::surfaceCount() const {
  std::lock_guard<std::mutex> mu(impl_->mu);
  return impl_->surfaces.size();
}

bool WgcCaptureSource::frameStats(const std::string& window_id,
                                  FrameStats& out) const {
  std::lock_guard<std::mutex> mu(impl_->mu);
  auto it = impl_->surfaces.find(window_id);
  if (it == impl_->surfaces.end()) return false;
  const Surface& s = *it->second;
  out.frames_arrived = s.frames_arrived.load();
  out.frames_wait_returned = s.frames_wait_returned.load();
  out.null_frames = s.null_frames.load();
  out.size_skips = s.size_skips.load();
  out.qi_failures = s.qi_failures.load();
  out.copy_failures = s.copy_failures.load();
  out.emitted = s.emitted.load();
  out.handler_errors = s.handler_errors.load();
  return true;
}

}  // namespace remotewindows::win32
